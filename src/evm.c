/* The EVM back end of langc, the core half (asm.h): the assembler, the
 * dispatcher over the entry list of domain/entries.c, checked add, the
 * ballot tally, the verdict table, the cast and amend entries and the
 * creation code. The domain owns the storage layout and the entries that
 * move value.
 *
 * Arrow-Debreu: the orbit rule is a byte table at the end of the runtime
 * code, read with CODECOPY, so no entry can write it. With k decision
 * values, ballot code j < k weighs (members + 1)^(j - 1) and code k weighs
 * 0, and the sum of the weights indexes the table of decision codes (k = 3:
 * codes 1, 2 and 3 weigh 1, members + 1 and 0, index r + (members + 1) f).
 *
 * Every failure is REVERT with empty output: a short calldata, an unknown
 * selector, a call value sent to an entry that is not payable, a failed
 * guard and an overflow of add. */
#include "asm.h"
#include "keccak.h"
#include <stdarg.h>
#include <string.h>

enum {
  EVM_ENTRIES = 16,         /* entries of one regime */
  EVM_RUNTIME_MAX = 24576,  /* EIP-170 */
  EVM_PACK_BITS = 256,      /* lang_entry_amend packs ceil(log2(k + 1)) bits per tally into one word, when they fit */
  EVM_TABLE_MAX = 4096,     /* bytes of the verdict table; it sets the member bound (members_max) */
  EVM_SIGNATURE = 512       /* bytes of the text name(uint256,...) of one entry, with its NUL */
};

static int fail(FILE *err, const char *code, const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(err, "interestc: %s: ", code);
  vfprintf(err, format, args);
  fputc('\n', err);
  va_end(args);
  return 1;
}

/* Records the first overflow of A: WHY when OVER. Nonzero when A is full. */
static int overflow(Asm *a, int over, AsmFull why) {
  a->full = a->full == ASM_ROOM && over ? why : a->full;
  return a->full != ASM_ROOM;
}

void asm_put(Asm *a, unsigned value) {
  if (overflow(a, a->size >= EVM_CAPACITY, ASM_FULL_CODE))
    return;
  a->code[a->size] = (unsigned char)value;
  a->size++;
}

void asm_op(Asm *a, Op code) { asm_put(a, (unsigned)code); }

/* The shortest PUSH of a big-endian word: PUSH0 for zero. */
void asm_push_word(Asm *a, const unsigned char word[32]) {
  size_t lead = 0;
  while (lead < 32 && word[lead] == 0)
    lead++;
  asm_put(a, (unsigned)OP_PUSH0 + (unsigned)(32 - lead));
  for (size_t i = lead; i < 32; i++)
    asm_put(a, word[i]);
}

void asm_push(Asm *a, unsigned long value) {
  unsigned char word[32] = {0};
  for (size_t i = 0; i < sizeof value && i < 32; i++)
    word[31 - i] = (unsigned char)(value >> (8 * i));
  asm_push_word(a, word);
}

void asm_push_label(Asm *a, Label label) {
  if (overflow(a, a->sites >= EVM_FIXUPS, ASM_FULL_FIXUPS))
    return;
  asm_op(a, OP_PUSH2);
  a->site[a->sites] = a->size;
  a->target[a->sites] = label;
  a->sites++;
  asm_put(a, 0);
  asm_put(a, 0);
}

void asm_bind(Asm *a, Label label) {
  a->at[label] = a->size;
  a->bound[label] = 1;
}

void asm_jumpdest(Asm *a, Label label) {
  asm_bind(a, label);
  asm_op(a, OP_JUMPDEST);
}

void asm_jump(Asm *a, Label label) {
  asm_push_label(a, label);
  asm_op(a, OP_JUMP);
}

void asm_jump_if(Asm *a, Label label) {
  asm_push_label(a, label);
  asm_op(a, OP_JUMPI);
}

void asm_revert_if(Asm *a) { asm_jump_if(a, LABEL_REVERT); }

Label asm_label(Asm *a) {
  a->labels_full = a->labels_full || LABEL_FREE + a->labels >= EVM_LABELS;
  if (a->labels_full)
    return LABEL_REVERT;
  a->labels++;
  return LABEL_FREE + a->labels - 1;
}

static int resolve(Asm *a) {
  int ok = a->full == ASM_ROOM;
  for (size_t i = 0; ok && i < a->sites; i++) {
    Label label = a->target[i];
    ok = a->bound[label] && a->at[label] <= 0xffff;
    a->code[a->site[i]] = (unsigned char)(a->at[label] >> 8);
    a->code[a->site[i] + 1] = (unsigned char)a->at[label];
  }
  return ok;
}

/* Calldata word j of the arguments, at byte 4 + 32 j. */
void asm_argument(Asm *a, unsigned j) {
  asm_push(a, 4ul + 32ul * j);
  asm_op(a, OP_CALLDATALOAD);
}

/* key -> keccak256(key . base), the slot of a mapping entry. */
void asm_slot(Asm *a, unsigned base) {
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_MSTORE);
  asm_push(a, base);
  asm_push(a, 0x20);
  asm_op(a, OP_MSTORE);
  asm_push(a, 0x40);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_SHA3);
}

/* x y -> x + y; reverts when the sum wraps (then it is below y). */
void asm_checked_add(Asm *a) {
  asm_op(a, OP_DUP2);
  asm_op(a, OP_ADD);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SWAP2);
  asm_op(a, OP_GT);
  asm_revert_if(a);
}

void asm_return_top(Asm *a) {
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_MSTORE);
  asm_push(a, 0x20);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_RETURN);
}

void asm_load(Asm *a, unsigned address) {
  asm_push(a, address);
  asm_op(a, OP_MLOAD);
}

void asm_store(Asm *a, unsigned address) {
  asm_push(a, address);
  asm_op(a, OP_MSTORE);
}

static int signature(char *text, const char *name, unsigned words, const char *types) {
  size_t size = strlen(name);
  if (types != NULL) {
    int fits = size + strlen(types) + 3 <= EVM_SIGNATURE;
    if (fits)
      snprintf(text, EVM_SIGNATURE, "%s(%s)", name, types);
    return fits;
  }
  /* Parentheses and NUL, with one fewer comma when arguments are present. */
  size_t suffix = words == 0 ? 3 : 2;
  int ok = size <= EVM_SIGNATURE - suffix && words <= (EVM_SIGNATURE - suffix - size) / 8;
  if (!ok)
    return 0;
  memcpy(text, name, size);
  text[size] = '(';
  size++;
  for (unsigned i = 0; i < words; i++) {
    memcpy(text + size, i == 0 ? "uint256" : ",uint256", i == 0 ? 7 : 8);
    size += i == 0 ? 7 : 8;
  }
  text[size] = ')';
  text[size + 1] = '\0';
  return 1;
}

/* With the selector on the stack: jump to label on name(uint256 x words),
 * or on name(types) when types is set. */
static void dispatch(Asm *a, const char *name, unsigned words, const char *types, Label label) {
  char text[EVM_SIGNATURE];
  unsigned char digest[32] = {0};
  if (overflow(a, !signature(text, name, words, types), ASM_FULL_SIGNATURE))
    return;
  lang_keccak256((const unsigned char *)text, strlen(text), digest);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_PUSH4);
  for (size_t i = 0; i < 4; i++)
    asm_put(a, digest[i]);
  asm_op(a, OP_EQ);
  asm_jump_if(a, label);
}

static void dispatch_head(Asm *a) {
  asm_push(a, 4);
  asm_op(a, OP_CALLDATASIZE);
  asm_op(a, OP_LT);
  asm_revert_if(a);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_CALLDATALOAD);
  asm_push(a, 0xe0);
  asm_op(a, OP_SHR);
}

static void revert_block(Asm *a) {
  asm_jumpdest(a, LABEL_REVERT);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_REVERT);
}

static void entry(Asm *a, Label label, unsigned words, Payment payment) {
  asm_jumpdest(a, label);
  asm_op(a, OP_POP);
  switch (payment) {
    case ENTRY_PAYABLE:
      break;
    case ENTRY_NONPAYABLE:
      asm_op(a, OP_CALLVALUE);
      asm_revert_if(a);
      break;
  }
  asm_push(a, 4ul + 32ul * words);
  asm_op(a, OP_CALLDATASIZE);
  asm_op(a, OP_LT);
  asm_revert_if(a);
}

/* Reverts unless calldata word j is below 2^160. */
void asm_address_guard(Asm *a, unsigned j) {
  asm_argument(a, j);
  asm_push(a, 0xa0);
  asm_op(a, OP_SHR);
  asm_revert_if(a);
}

/* acc b -> acc + weight of ballot b. The term of code 1 stays under b, the
 * middle terms add into it and the term of code k - 1 consumes b. */
static void weigh(Asm *a, unsigned members, unsigned k) {
  unsigned long weight = 1;
  for (unsigned j = 1; j < k; j++, weight *= members + 1ul) {
    int last = j + 1 == k;
    int first = j == 1;
    if (!last)
      asm_op(a, OP_DUP1);
    asm_push(a, j);
    asm_op(a, OP_EQ);
    if (!first) {
      asm_push(a, weight);
      asm_op(a, OP_MUL);
    }
    if (first && !last)
      asm_op(a, OP_SWAP1);
    if (!first && !last) {
      asm_op(a, OP_SWAP1);
      asm_op(a, OP_SWAP2);
      asm_op(a, OP_ADD);
      asm_op(a, OP_SWAP1);
    }
    if (last && !first)
      asm_op(a, OP_ADD);
  }
  asm_op(a, OP_ADD);
}

/* The decision code of the ballots in calldata words first .. first + n - 1.
 * Each ballot must be 1 to k. */
void asm_tally(Asm *a, unsigned first, unsigned members, unsigned k) {
  asm_op(a, OP_PUSH0);
  for (unsigned m = 0; m < members; m++) {
    asm_argument(a, first + m);
    asm_op(a, OP_DUP1);
    asm_push(a, 1);
    asm_op(a, OP_SWAP1);
    asm_op(a, OP_SUB);
    asm_push(a, k - 1ul);
    asm_op(a, OP_LT);
    asm_revert_if(a);
    weigh(a, members, k);
  }
  asm_push_label(a, LABEL_TABLE);
  asm_op(a, OP_ADD);
  asm_push(a, 0x20);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_CODECOPY);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_MLOAD);
  asm_push(a, 0xf8);
  asm_op(a, OP_SHR);
}

/* cast x: the decision of the ballots. It writes nothing. */
void lang_entry_cast(Asm *a, const EntryContext *c) {
  asm_tally(a, 0, c->members, c->decisions);
  asm_return_top(a);
}

/* amend at the canonical Phi: the packed table sum C_i * 2^(bits i), bits =
 * ceil(log2(k + 1)) (host CAPABILITY.md, Output; k = 3: C_i * 4^i). When the
 * codes do not fit one word (packed NULL), amend reverts with no output. */
void lang_entry_amend(Asm *a, const EntryContext *c) {
  if (c->packed == NULL) {
    asm_op(a, OP_PUSH0);
    asm_op(a, OP_PUSH0);
    asm_op(a, OP_REVERT);
    return;
  }
  asm_push_word(a, c->packed);
  asm_return_top(a);
}

static unsigned entry_words(const Entry *e, unsigned members) {
  return e->words + (e->ballots ? members : 0u);
}

/* The dispatcher over the domain entries of REGIME, then the head and the
 * body of each entry, in list order. */
static int runtime_entries(Asm *a, LangRegime regime, const EntryContext *c, FILE *err) {
  size_t count = 0;
  const Entry *list = lang_domain_entries(regime, c, &count);
  if (list == NULL || count < 1 || count > EVM_ENTRIES)
    return fail(err, "EVM_INTERNAL", "the domain lists %zu entries for regime %d, need 1 to %d",
                count, (int)regime, EVM_ENTRIES);
  Label labels[EVM_ENTRIES] = {0};
  for (size_t i = 0; i < count; i++)
    labels[i] = asm_label(a);
  dispatch_head(a);
  for (size_t i = 0; i < count; i++)
    dispatch(a, list[i].name, entry_words(&list[i], c->members), list[i].types, labels[i]);
  revert_block(a);
  for (size_t i = 0; i < count; i++) {
    entry(a, labels[i], entry_words(&list[i], c->members), list[i].payment);
    list[i].emit(a, c);
  }
  return 0;
}

size_t lang_tally_count(unsigned k, unsigned n, size_t limit) {
  size_t count = 1;
  for (unsigned i = 1; i < k && count <= limit; i++)
    count = count * (n + i) / i;
  return count > limit ? limit + 1 : count;
}

int lang_tally_next(unsigned *counts, unsigned k, unsigned n) {
  if (k < 2)
    return 0;
  if (counts[k - 1] > 0) {
    counts[k - 2]++;
    counts[k - 1]--;
    return 1;
  }
  unsigned p = k - 1;  /* p - 1 is the last positive part before the remainder */
  while (p > 0 && counts[p - 1] == 0)
    p--;
  if (p <= 1)
    return 0;
  counts[p - 2]++;
  counts[p - 1] = 0;
  unsigned used = 0;
  for (unsigned j = 0; j + 1 < k; j++)
    used += counts[j];
  counts[k - 1] = n - used;
  return 1;
}

/* ceil(log2(k + 1)): the bits of one packed decision code. */
static unsigned code_bits(unsigned k) {
  unsigned bits = 0;
  while ((1ul << bits) <= k)
    bits++;
  return bits;
}

/* n (n + 1)^(k - 2) + 1, the table bytes; EVM_TABLE_MAX + 1 when larger. */
static size_t table_bytes(unsigned n, unsigned k) {
  size_t bytes = n;
  for (unsigned j = 2; j < k && bytes <= EVM_TABLE_MAX; j++)
    bytes *= n + 1ul;
  return bytes >= EVM_TABLE_MAX ? EVM_TABLE_MAX + 1ul : bytes + 1;
}

/* The most members whose verdict table fits EVM_TABLE_MAX bytes (k = 3: 63,
 * k = 4: 15). */
static unsigned members_max(unsigned k) {
  unsigned n = 0;
  while (table_bytes(n + 1, k) <= EVM_TABLE_MAX)
    n++;
  return n;
}

/* The sum of counts[j] (n + 1)^j over j < k - 1. */
static size_t table_index(const unsigned *counts, unsigned n, unsigned k) {
  size_t index = 0;
  size_t weight = 1;
  for (unsigned j = 0; j + 1 < k; j++, weight *= n + 1ul)
    index += counts[j] * weight;
  return index;
}

/* "1 or 2", "1, 2 or 3", ... */
static void code_list(char *text, size_t size, unsigned k) {
  size_t at = 0;
  for (unsigned j = 1; j <= k && at < size; j++) {
    const char *sep = j == 1 ? "" : j == k ? " or " : ", ";
    int wrote = snprintf(text + at, size - at, "%s%u", sep, j);
    at += wrote > 0 ? (size_t)wrote : size;
  }
}

/* The amend word (Alternative A): the code of tally i at bits bits i ..
 * bits i + bits - 1, when the codes of all the tallies fit one word. Else
 * NULL, and lang_entry_amend reverts. */
static const unsigned char *amend_word(const LangContract *contract, unsigned char packed[32]) {
  unsigned bits = code_bits(contract->decisions);
  int fits = contract->count * bits <= EVM_PACK_BITS;
  for (size_t i = 0; fits && i < contract->count; i++) {
    for (unsigned t = 0; t < bits; t++) {
      size_t at = (size_t)bits * i + t;
      packed[31 - at / 8] |= (unsigned char)(((contract->codes[i] >> t) & 1u) << (at % 8));
    }
  }
  return fits ? packed : NULL;
}

static int runtime_debreu(Asm *a, const LangContract *contract, FILE *err) {
  unsigned n = contract->members;
  unsigned k = contract->decisions;
  unsigned char table[EVM_TABLE_MAX] = {0};
  unsigned char packed[32] = {0};
  unsigned counts[LANG_DECISIONS_MAX] = {0};
  counts[k - 1] = n;
  size_t i = 0;
  for (int more = 1; more; more = lang_tally_next(counts, k, n), i++)
    table[table_index(counts, n, k)] = (unsigned char)contract->codes[i];
  EntryContext context = {n, k, amend_word(contract, packed), contract->data};
  int bad = runtime_entries(a, LANG_REGIME_DEBREU, &context, err);
  if (bad)
    return bad;
  asm_bind(a, LABEL_TABLE);
  size_t bytes = table_bytes(n, k);
  for (size_t b = 0; b < bytes; b++)
    asm_put(a, table[b]);
  asm_bind(a, LABEL_DATA);
  lang_domain_data(a, &context);
  return 0;
}

static int check_impossibility(const LangContract *contract, FILE *err) {
  if (contract->members < 1)
    return fail(err, "EVM_LIMIT", "a contract needs at least 1 member, got 0");
  if (contract->codes != NULL || contract->count != 0)
    return fail(err, "EVM_TABLE", "the impossibility regime takes no decision codes, got %zu",
                contract->count);
  return 0;
}

static int check_debreu(const LangContract *contract, FILE *err) {
  unsigned n = contract->members;
  unsigned k = contract->decisions;
  if (k < 2 || k > LANG_DECISIONS_MAX)
    return fail(err, "EVM_TABLE", "the Debreu regime needs 2 to %d decision values, got %u",
                LANG_DECISIONS_MAX, k);
  unsigned most = members_max(k);
  if (n < 1 || n > most)
    return fail(err, "EVM_LIMIT", "the Debreu regime needs 1 to %u members, got %u", most, n);
  size_t wanted = lang_tally_count(k, n, EVM_TABLE_MAX);
  if (contract->codes == NULL || contract->count != wanted)
    return fail(err, "EVM_TABLE", "%zu decision codes needed for %u members, got %zu",
                wanted, n, contract->codes == NULL ? (size_t)0 : contract->count);
  if (table_bytes(n, k) > EVM_TABLE_MAX)
    return fail(err, "EVM_TABLE", "the verdict table of %u members and %u decision values exceeds %d bytes",
                n, k, EVM_TABLE_MAX);
  for (size_t i = 0; i < wanted; i++) {
    unsigned code = contract->codes[i];
    if (code < 1 || code > k) {
      char list[8 * LANG_DECISIONS_MAX] = {0};
      code_list(list, sizeof list, k);
      return fail(err, "EVM_TABLE", "decision code %zu is %u, need %s", i, code, list);
    }
  }
  return 0;
}

static int build_runtime(Asm *a, const LangContract *contract, FILE *err) {
  switch (contract->regime) {
    case LANG_REGIME_IMPOSSIBILITY: {
      int bad = check_impossibility(contract, err);
      if (bad)
        return bad;
      EntryContext context = {contract->members, 0, NULL, contract->data};
      bad = runtime_entries(a, LANG_REGIME_IMPOSSIBILITY, &context, err);
      if (bad)
        return bad;
      asm_bind(a, LABEL_DATA);
      lang_domain_data(a, &context);
      return 0;
    }
    case LANG_REGIME_DEBREU: {
      int bad = check_debreu(contract, err);
      if (bad)
        return bad;
      return runtime_debreu(a, contract, err);
    }
  }
  return fail(err, "EVM_USAGE", "unknown regime %d", (int)contract->regime);
}

/* Reverts on a call value, writes the genesis storage (the domain), copies
 * the runtime to memory and returns it. */
static void creation(Asm *a, const Asm *body, const LangContract *contract) {
  asm_op(a, OP_CALLVALUE);
  asm_jump_if(a, LABEL_REVERT);
  lang_domain_genesis(a, contract);
  asm_push(a, body->size);
  asm_op(a, OP_DUP1);
  asm_push_label(a, LABEL_RUNTIME);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_CODECOPY);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_RETURN);
  revert_block(a);
  asm_bind(a, LABEL_RUNTIME);
  for (size_t i = 0; i < body->size; i++)
    asm_put(a, body->code[i]);
  /* The end of the creation code, with no byte: a constructor word of the
   * domain starts here. */
  asm_bind(a, LABEL_END);
}

static int finish(Asm *a, FILE *err) {
  if (a->labels_full)
    return fail(err, "EVM_INTERNAL", "more than %d jump labels", EVM_LABELS);
  switch (a->full) {
    case ASM_ROOM:
      break;
    case ASM_FULL_CODE:
      return fail(err, "EVM_SIZE", "the bytecode exceeds %d bytes", EVM_CAPACITY);
    case ASM_FULL_FIXUPS:
      return fail(err, "EVM_SIZE", "the bytecode has more than %d jump label sites", EVM_FIXUPS);
    case ASM_FULL_SIGNATURE:
      return fail(err, "EVM_SIZE", "an entry signature name(uint256,...) needs more than %d bytes",
                  EVM_SIGNATURE);
  }
  if (!resolve(a))
    return fail(err, "EVM_INTERNAL", "unresolved jump label");
  return 0;
}

static int write_hex(const Asm *a, FILE *out, FILE *err) {
  for (size_t i = 0; i < a->size; i++)
    fprintf(out, "%02x", a->code[i]);
  fputc('\n', out);
  if (fflush(out) != 0 || ferror(out))
    return fail(err, "EVM_IO", "cannot write the bytecode");
  return 0;
}

int lang_evm_write(const LangContract *contract, LangPart part, FILE *out, FILE *err) {
  if (contract == NULL)
    return fail(err, "EVM_USAGE", "no contract");
  Asm body_asm;
  Asm creation_asm;
  memset(&body_asm, 0, sizeof body_asm);
  memset(&creation_asm, 0, sizeof creation_asm);
  int bad = build_runtime(&body_asm, contract, err);
  if (bad)
    return bad;
  bad = finish(&body_asm, err);
  if (bad)
    return bad;
  if (body_asm.size > EVM_RUNTIME_MAX)
    return fail(err, "EVM_SIZE", "the runtime has %zu bytes, the limit is %d",
                body_asm.size, EVM_RUNTIME_MAX);
  switch (part) {
    case LANG_PART_RUNTIME:
      return write_hex(&body_asm, out, err);
    case LANG_PART_CREATION:
      creation(&creation_asm, &body_asm, contract);
      bad = finish(&creation_asm, err);
      return bad ? bad : write_hex(&creation_asm, out, err);
  }
  return fail(err, "EVM_USAGE", "unknown part %d", (int)part);
}

int lang_evm_amend(const LangContract *contract, FILE *out, FILE *err) {
  if (contract == NULL || contract->regime != LANG_REGIME_DEBREU)
    return fail(err, "EVM_USAGE", "amend needs a Debreu contract");
  int bad = check_debreu(contract, err);
  if (bad)
    return bad;
  Asm body;
  memset(&body, 0, sizeof body);
  unsigned char packed[32] = {0};
  EntryContext context = {contract->members, contract->decisions, amend_word(contract, packed),
                          contract->data};
  lang_entry_amend(&body, &context);
  bad = finish(&body, err);
  return bad ? bad : write_hex(&body, out, err);
}
