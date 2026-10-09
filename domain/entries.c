/* The domain entries of interestc: the interest storage, the genesis
 * writes, the code data and the entry list of each regime (I3 plan, MY
 * CALLs 12 to 14; host README, The domain). src/evm.c writes the dispatcher
 * and the head of each entry, then the bodies below.
 *
 * Storage (MY CALL 13): MU (slot 0, identity -> units), REGISTRY (slot 1,
 * wallet -> identity + 1, 0 = no identity), PROFILE (slot 2, identity ->
 * profile code 0 .. 3), CHARTER (slot 3, the active charter code 1 .. K),
 * RESERVE (slot 4, kind code 0 rent, 1 sale -> wei), INDEX (slot 5),
 * CHECKPOINT (slot 6, identity -> INDEX at the last settle) and NUM (slot 7,
 * identity -> numerator in units of 1/S wei). S, the sum of the genesis
 * units, is a code constant. A mapping entry lives at keccak256(key . slot),
 * as in Solidity.
 *
 * Code data at LABEL_DATA: the R limit words, rows x 4 x 4 (charter code - 1,
 * profile of the sender, profile of the receiver; "admits q" = q < L), then
 * the W gate words, rows x 2 (pass 1, retain 0), 32 bytes each.
 *
 * Program data (data.h): lang_domain_read gives the data that lang_data
 * (src/check.c) reads, and lang_domain_print writes it for the `data` verb. */
#include "asm.h"
#include "data.h"
#include <string.h>

enum {
  SLOT_MU = 0, SLOT_REGISTRY = 1, SLOT_PROFILE = 2, SLOT_CHARTER = 3,
  SLOT_RESERVE = 4, SLOT_INDEX = 5, SLOT_CHECKPOINT = 6, SLOT_NUM = 7
};

enum { MEM_ID = 0x80, MEM_CHARTER = 0xa0 };

enum { WORD = 32, LIMITS = LANG_PROFILES * LANG_PROFILES };

/* The charter rows of the code data: k (Debreu) or K, whichever is
 * larger, so that every charter code that genesis or amend can write has a
 * row. */
static unsigned data_rows(const EntryContext *c) {
  unsigned charters = c->data == NULL ? 0u : c->data->charters;
  unsigned rows = c->decisions > charters ? c->decisions : charters;
  return rows > LANG_DECISIONS_MAX ? LANG_DECISIONS_MAX : rows;
}

/* The big-endian word of value + extra, extra 0 or 1 (no wrap below 2^65). */
static void word_of(unsigned char word[WORD], unsigned long long value, unsigned extra) {
  memset(word, 0, WORD);
  unsigned long long low = value + extra;
  for (size_t i = 0; i < sizeof low; i++)
    word[WORD - 1 - i] = (unsigned char)(low >> (8 * i));
  word[WORD - 1 - sizeof low] = (unsigned char)(low < value);
}

static void put_word(Asm *a, const unsigned char word[WORD]) {
  for (size_t i = 0; i < WORD; i++)
    asm_put(a, word[i]);
}

/* The limit word L of a cap: deny 0, upTo n -> n + 1, any 2^256 - 1. */
static void limit_word(unsigned char word[WORD], LangCap cap) {
  switch (cap.tag) {
    case LANG_CAP_DENY:
      memset(word, 0, WORD);
      return;
    case LANG_CAP_UP_TO:
      word_of(word, cap.n, 1);
      return;
    case LANG_CAP_ANY:
      memset(word, 0xff, WORD);
      return;
  }
  memset(word, 0, WORD);
}

/* -> code data word INDEX (from LABEL_DATA), read with CODECOPY into the
 * core scratch. */
static void data_word(Asm *a) {
  asm_push(a, WORD);
  asm_op(a, OP_MUL);
  asm_push_label(a, LABEL_DATA);
  asm_op(a, OP_ADD);
  asm_push(a, WORD);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_CODECOPY);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_MLOAD);
}

/* -> id(CALLER) = REGISTRY[CALLER] - 1; reverts when REGISTRY[CALLER] = 0. */
static void caller_identity(Asm *a) {
  asm_op(a, OP_CALLER);
  asm_slot(a, SLOT_REGISTRY);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_push(a, 1);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
}

/* Reverts unless calldata word j is a kind code, 0 or 1. */
static void kind_guard(Asm *a, unsigned j) {
  asm_push(a, 1);
  asm_argument(a, j);
  asm_op(a, OP_GT);
  asm_revert_if(a);
}

/* The sum of the genesis units of the program data (0 without data). */
static unsigned long long total_units(const LangDomainData *data) {
  unsigned long long sum = 0;
  for (size_t i = 0; data != NULL && i < data->holders; i++)
    sum += data->holder[i].units;
  return sum;
}

/* -> S, the code constant. */
static void push_supply(Asm *a, const EntryContext *c) {
  unsigned char word[WORD];
  word_of(word, total_units(c->data), 0);
  asm_push_word(a, word);
}

/* Reverts when S = 0. */
static void supply_guard(Asm *a, const EntryContext *c) {
  push_supply(a, c);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
}

/* x y -> x * y; reverts when the product wraps (x = 0, or p / x = y). */
static void checked_mul(Asm *a) {
  asm_op(a, OP_DUP2);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_MUL);
  asm_op(a, OP_SWAP2);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_ISZERO);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_DUP4);
  asm_op(a, OP_DIV);
  asm_op(a, OP_DUP3);
  asm_op(a, OP_EQ);
  asm_op(a, OP_ADD);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_op(a, OP_POP);
}

/* h -> claimOf(h) = NUM[h] + MU[h] * (INDEX - CHECKPOINT[h]) (checked). */
static void claim_of(Asm *a) {
  asm_op(a, OP_DUP1);
  asm_slot(a, SLOT_CHECKPOINT);
  asm_op(a, OP_SLOAD);
  asm_push(a, SLOT_INDEX);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_SUB);
  asm_op(a, OP_DUP2);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_SLOAD);
  checked_mul(a);
  asm_op(a, OP_SWAP1);
  asm_slot(a, SLOT_NUM);
  asm_op(a, OP_SLOAD);
  asm_checked_add(a);
}

/* h -> (settle h): NUM[h] := claimOf(h); CHECKPOINT[h] := INDEX. */
static void settle(Asm *a) {
  asm_op(a, OP_DUP1);
  claim_of(a);
  asm_op(a, OP_DUP2);
  asm_slot(a, SLOT_NUM);
  asm_op(a, OP_SSTORE);
  asm_push(a, SLOT_INDEX);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_SWAP1);
  asm_slot(a, SLOT_CHECKPOINT);
  asm_op(a, OP_SSTORE);
}

/* deposit kind: RESERVE[kind] += CALLVALUE (checked); returns the reserve. */
static void deposit(Asm *a, const EntryContext *c) {
  (void)c;
  kind_guard(a, 0);
  asm_argument(a, 0);
  asm_slot(a, SLOT_RESERVE);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_CALLVALUE);
  asm_checked_add(a);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SWAP2);
  asm_op(a, OP_SSTORE);
  asm_return_top(a);
}

/* distribute kind: S = 0 reverts; d = W[CHARTER][kind] ? RESERVE[kind] : 0
 * (the W word is 1 or 0, so d = W * RESERVE); RESERVE[kind] -= d; INDEX += d
 * (checked); returns d. */
static void distribute(Asm *a, const EntryContext *c) {
  supply_guard(a, c);
  kind_guard(a, 0);
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SLOAD);
  asm_push(a, 1);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
  asm_push(a, LANG_KINDS);
  asm_op(a, OP_MUL);
  asm_argument(a, 0);
  asm_op(a, OP_ADD);
  asm_push(a, data_rows(c) * LIMITS);
  asm_op(a, OP_ADD);
  data_word(a);
  asm_argument(a, 0);
  asm_slot(a, SLOT_RESERVE);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_MUL);
  asm_argument(a, 0);
  asm_slot(a, SLOT_RESERVE);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_SUB);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  asm_op(a, OP_DUP1);
  asm_push(a, SLOT_INDEX);
  asm_op(a, OP_SLOAD);
  asm_checked_add(a);
  asm_push(a, SLOT_INDEX);
  asm_op(a, OP_SSTORE);
  asm_return_top(a);
}

/* withdraw: S = 0 reverts; h = id(CALLER); settle h; paid = NUM[h] / S;
 * NUM[h] := NUM[h] mod S; then (all stores done) a CALL of paid wei to
 * CALLER, which reverts the entry when it fails; returns paid. */
static void withdraw(Asm *a, const EntryContext *c) {
  supply_guard(a, c);
  caller_identity(a);
  asm_op(a, OP_DUP1);
  settle(a);
  asm_slot(a, SLOT_NUM);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  push_supply(a, c);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_MOD);
  asm_op(a, OP_DUP3);
  asm_op(a, OP_SSTORE);
  push_supply(a, c);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_DIV);
  asm_store(a, MEM_ID);
  asm_op(a, OP_POP);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_PUSH0);
  asm_load(a, MEM_ID);
  asm_op(a, OP_CALLER);
  asm_op(a, OP_GAS);
  asm_op(a, OP_CALL);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_load(a, MEM_ID);
  asm_return_top(a);
}

/* transfer to q: h = id(CALLER); q >= R[CHARTER][PROFILE h][PROFILE to]
 * reverts; q > MU[h] or to + 1 overflow reverts; settle h, then settle to (SPEC 5: checkpoints
 * both identities); MU[h] -= q, then MU[to] += q (read after the debit, so
 * to = h keeps the mass); returns 1. */
static void transfer(Asm *a, const EntryContext *c) {
  (void)c;
  /* Every destination must admit the registry encoding identity + 1. */
  asm_argument(a, 0);
  asm_push(a, 1);
  asm_checked_add(a);
  asm_op(a, OP_POP);
  caller_identity(a);
  asm_store(a, MEM_ID);
  asm_argument(a, 0);
  asm_slot(a, SLOT_PROFILE);
  asm_op(a, OP_SLOAD);
  asm_load(a, MEM_ID);
  asm_slot(a, SLOT_PROFILE);
  asm_op(a, OP_SLOAD);
  asm_push(a, LANG_PROFILES);
  asm_op(a, OP_MUL);
  asm_op(a, OP_ADD);
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SLOAD);
  asm_push(a, 1);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
  asm_push(a, LIMITS);
  asm_op(a, OP_MUL);
  asm_op(a, OP_ADD);
  data_word(a);
  asm_argument(a, 1);
  asm_op(a, OP_LT);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_load(a, MEM_ID);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_SLOAD);
  asm_argument(a, 1);
  asm_op(a, OP_GT);
  asm_revert_if(a);
  asm_load(a, MEM_ID);
  settle(a);
  asm_argument(a, 0);
  settle(a);
  asm_load(a, MEM_ID);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_argument(a, 1);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  asm_argument(a, 0);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_argument(a, 1);
  asm_checked_add(a);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  asm_push(a, 1);
  asm_return_top(a);
}

/* attest w h p: (CHARTER, id(CALLER)) must be an issuer pair (an unrolled
 * compare chain over the pairs of the program); w >= 2^160 or p > 3
 * reverts; REGISTRY[w] := h + 1 (checked); PROFILE[h] := p; returns 1. */
static void attest(Asm *a, const EntryContext *c) {
  Label issuer = asm_label(a);
  caller_identity(a);
  asm_store(a, MEM_ID);
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SLOAD);
  asm_store(a, MEM_CHARTER);
  size_t pairs = c->data == NULL ? 0 : c->data->issuers;
  for (size_t i = 0; i < pairs; i++) {
    unsigned char word[WORD];
    asm_load(a, MEM_CHARTER);
    asm_push(a, c->data->issuer[i].charter);
    asm_op(a, OP_EQ);
    asm_load(a, MEM_ID);
    word_of(word, c->data->issuer[i].identity, 0);
    asm_push_word(a, word);
    asm_op(a, OP_EQ);
    asm_op(a, OP_AND);
    asm_jump_if(a, issuer);
  }
  asm_jump(a, LABEL_REVERT);
  asm_jumpdest(a, issuer);
  asm_address_guard(a, 0);
  asm_push(a, LANG_PROFILES - 1);
  asm_argument(a, 2);
  asm_op(a, OP_GT);
  asm_revert_if(a);
  asm_argument(a, 1);
  asm_push(a, 1);
  asm_checked_add(a);
  asm_argument(a, 0);
  asm_slot(a, SLOT_REGISTRY);
  asm_op(a, OP_SSTORE);
  asm_argument(a, 2);
  asm_argument(a, 1);
  asm_slot(a, SLOT_PROFILE);
  asm_op(a, OP_SSTORE);
  asm_push(a, 1);
  asm_return_top(a);
}

/* amend b1 .. bn (amendState): CHARTER := the tally code; returns it. The
 * core amend (the packed table word) is not in the interest lists. */
static void amend(Asm *a, const EntryContext *c) {
  asm_tally(a, 0, c->members, c->decisions);
  asm_op(a, OP_DUP1);
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SSTORE);
  asm_return_top(a);
}

/* mass h: MU[h]. */
static void mass(Asm *a, const EntryContext *c) {
  (void)c;
  asm_argument(a, 0);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_SLOAD);
  asm_return_top(a);
}

/* supply: S, the code constant. */
static void supply(Asm *a, const EntryContext *c) {
  push_supply(a, c);
  asm_return_top(a);
}

/* claimOf h: NUM[h] + MU[h] * (INDEX - CHECKPOINT[h]), in units of 1/S wei. */
static void claim(Asm *a, const EntryContext *c) {
  (void)c;
  asm_argument(a, 0);
  claim_of(a);
  asm_return_top(a);
}

/* charter: CHARTER. */
static void charter(Asm *a, const EntryContext *c) {
  (void)c;
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SLOAD);
  asm_return_top(a);
}

/* reserve kind: RESERVE[kind]; a kind code above 1 reverts. */
static void reserve(Asm *a, const EntryContext *c) {
  (void)c;
  kind_guard(a, 0);
  asm_argument(a, 0);
  asm_slot(a, SLOT_RESERVE);
  asm_op(a, OP_SLOAD);
  asm_return_top(a);
}

/* selfConstituting (MY CALL 14): 1 at Debreu, 0 at impossibility. */
static void self_debreu(Asm *a, const EntryContext *c) {
  (void)c;
  asm_push(a, 1);
  asm_return_top(a);
}

static void self_impossibility(Asm *a, const EntryContext *c) {
  (void)c;
  asm_op(a, OP_PUSH0);
  asm_return_top(a);
}

static const Entry impossibility[] = {
  {"deposit", 1, 0, ENTRY_PAYABLE, deposit},
  {"withdraw", 0, 0, ENTRY_NONPAYABLE, withdraw},
  {"attest", 3, 0, ENTRY_NONPAYABLE, attest},
  {"mass", 1, 0, ENTRY_NONPAYABLE, mass},
  {"supply", 0, 0, ENTRY_NONPAYABLE, supply},
  {"claimOf", 1, 0, ENTRY_NONPAYABLE, claim},
  {"charter", 0, 0, ENTRY_NONPAYABLE, charter},
  {"reserve", 1, 0, ENTRY_NONPAYABLE, reserve},
  {"selfConstituting", 0, 0, ENTRY_NONPAYABLE, self_impossibility},
};

static const Entry debreu[] = {
  {"deposit", 1, 0, ENTRY_PAYABLE, deposit},
  {"distribute", 1, 0, ENTRY_NONPAYABLE, distribute},
  {"withdraw", 0, 0, ENTRY_NONPAYABLE, withdraw},
  {"transfer", 2, 0, ENTRY_NONPAYABLE, transfer},
  {"attest", 3, 0, ENTRY_NONPAYABLE, attest},
  {"cast", 0, 1, ENTRY_NONPAYABLE, lang_entry_cast},
  {"amend", 0, 1, ENTRY_NONPAYABLE, amend},
  {"mass", 1, 0, ENTRY_NONPAYABLE, mass},
  {"supply", 0, 0, ENTRY_NONPAYABLE, supply},
  {"claimOf", 1, 0, ENTRY_NONPAYABLE, claim},
  {"charter", 0, 0, ENTRY_NONPAYABLE, charter},
  {"reserve", 1, 0, ENTRY_NONPAYABLE, reserve},
  {"selfConstituting", 0, 0, ENTRY_NONPAYABLE, self_debreu},
};

const Entry *lang_domain_entries(LangRegime regime, size_t *count) {
  switch (regime) {
    case LANG_REGIME_IMPOSSIBILITY:
      *count = sizeof impossibility / sizeof impossibility[0];
      return impossibility;
    case LANG_REGIME_DEBREU:
      *count = sizeof debreu / sizeof debreu[0];
      return debreu;
  }
  *count = 0;
  return NULL;
}

/* key value -> the storage word: SSTORE of a constant value at the slot of
 * a constant key in mapping BASE. */
static void store_entry(Asm *a, unsigned base, unsigned long long key, unsigned long long value,
                        unsigned extra) {
  unsigned char word[WORD];
  word_of(word, value, extra);
  asm_push_word(a, word);
  word_of(word, key, 0);
  asm_push_word(a, word);
  asm_slot(a, base);
  asm_op(a, OP_SSTORE);
}

/* The genesis writes: CHARTER := start; per row REGISTRY[w] := h + 1; per
 * distinct identity h, MU[h] := the sum of its units and PROFILE[h] := the
 * profile of its last row. Zero words are not written. */
void lang_domain_genesis(Asm *a, const LangContract *contract) {
  const LangDomainData *data = contract->data;
  asm_push(a, data == NULL ? 1u : data->start);
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SSTORE);
  size_t rows = data == NULL ? 0 : data->holders;
  for (size_t i = 0; i < rows; i++) {
    const LangHolder *row = &data->holder[i];
    store_entry(a, SLOT_REGISTRY, row->wallet, row->identity, 1);
    int seen = 0;
    for (size_t j = 0; j < i; j++)
      seen = seen || data->holder[j].identity == row->identity;
    if (seen)
      continue;
    unsigned long long units = 0;
    unsigned profile = 0;
    for (size_t j = i; j < rows; j++) {
      int same = data->holder[j].identity == row->identity;
      units += same ? data->holder[j].units : 0;
      profile = same ? data->holder[j].profile : profile;
    }
    if (units != 0)
      store_entry(a, SLOT_MU, row->identity, units, 0);
    if (profile != 0)
      store_entry(a, SLOT_PROFILE, row->identity, profile, 0);
  }
}

/* The code data at LABEL_DATA: the R limit words, then the W gate words. */
void lang_domain_data(Asm *a, const EntryContext *c) {
  unsigned rows = data_rows(c);
  unsigned char word[WORD];
  for (unsigned r = 0; r < rows; r++)
    for (unsigned p = 0; p < LANG_PROFILES; p++)
      for (unsigned q = 0; q < LANG_PROFILES; q++) {
        LangCap deny = {LANG_CAP_DENY, 0};
        limit_word(word, c->data == NULL ? deny : c->data->cap[r][p][q]);
        put_word(a, word);
      }
  for (unsigned r = 0; r < rows; r++)
    for (unsigned k = 0; k < LANG_KINDS; k++) {
      word_of(word, c->data != NULL && c->data->pass[r][k] ? 1u : 0u, 0);
      put_word(a, word);
    }
}

/* The program data of the program (lang_data). This domain always gives
 * the data, never NULL. */
int lang_domain_read(LangChecked *checked, const LangDomainData **data) {
  static LangDomainData program;
  int status = lang_data(checked, &program);
  if (status != LANG_EXIT_OK)
    return status;
  *data = &program;
  return LANG_EXIT_OK;
}

static const char *cap_text(LangCap cap, char *buf, size_t size) {
  switch (cap.tag) {
  case LANG_CAP_DENY: return "d";
  case LANG_CAP_ANY: return "a";
  case LANG_CAP_UP_TO: snprintf(buf, size, "u%llu", cap.n); return buf;
  }
  return "?";
}

/* The `data` verb: start, charters, genesis rows w:h:p:u, restrict (one
 * group of 16 caps per charter, profile pairs row-major), waterfall (one
 * group per charter: p pass, r retain, for rent then sale), issuers c:h.
 * DATA is not NULL (lang_domain_read). */
void lang_domain_print(const LangDomainData *data, FILE *out) {
  fprintf(out, "start %u\ncharters %u\ngenesis", data->start, data->charters);
  for (size_t i = 0; i < data->holders; i++)
    fprintf(out, " %llu:%llu:%u:%llu", data->holder[i].wallet, data->holder[i].identity,
            data->holder[i].profile, data->holder[i].units);
  fputs("\nrestrict", out);
  char buf[32];
  for (unsigned i = 0; i < data->charters; i++)
    for (unsigned p = 0; p < LANG_PROFILES; p++)
      for (unsigned r = 0; r < LANG_PROFILES; r++)
        fprintf(out, "%s%s", p == 0 && r == 0 ? " " : ",", cap_text(data->cap[i][p][r], buf, sizeof buf));
  fputs("\nwaterfall", out);
  for (unsigned i = 0; i < data->charters; i++)
    fprintf(out, " %c%c", data->pass[i][0] ? 'p' : 'r', data->pass[i][1] ? 'p' : 'r');
  fputs("\nissuers", out);
  for (size_t i = 0; i < data->issuers; i++)
    fprintf(out, " %u:%llu", data->issuer[i].charter, data->issuer[i].identity);
  fputc('\n', out);
}
