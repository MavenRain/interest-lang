/* Test driver of the EVM back end, run with:
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c [-k K] creation|runtime N debreu CODE...
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c [-k K] creation|runtime N impossibility [CODE...]
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c [-k K] amend N debreu CODE...
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c keccak TEXT
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c token VARIANT
 * K is the number of decision values (3 when not given: the sample domain).
 * amend writes only the body of lang_entry_amend (lang_evm_amend). Codes go to
 * the back end unchecked (0 to 255), so the tests reach its EVM_TABLE
 * refusals. token writes the runtime code of a stub ERC-20 token for the
 * carrier cases (O5b, MY CALL 153). Exit 0 ok, 1 refused by the back end,
 * 2 usage. */
#include "../src/asm.h"
#include "../src/keccak.h"
#include <stdlib.h>
#include <string.h>

enum { TOOL_CODES = 4096, TOOL_DIGITS = 9 };  /* TOOL_CODES: the tallies of the largest verdict table */

typedef enum { VERB_CREATION, VERB_RUNTIME, VERB_AMEND, VERB_NONE } Verb;

static int usage(void) {
  fputs("usage: evmtool [-k K] creation|runtime|amend N debreu CODE... | evmtool [-k K] creation|runtime N impossibility [CODE...]"
        " | evmtool keccak TEXT | evmtool token standard|noreturn|false|revert|fee|hook\n", stderr);
  return 2;
}

/* A decimal number of at most TOOL_DIGITS digits, or -1. */
static long number(const char *text) {
  size_t size = strlen(text);
  int ok = size >= 1 && size <= TOOL_DIGITS && strspn(text, "0123456789") == size;
  return ok ? strtol(text, NULL, 10) : -1;
}

static int keccak(const char *text) {
  unsigned char digest[32];
  lang_keccak256((const unsigned char *)text, strlen(text), digest);
  for (size_t i = 0; i < 32; i++)
    printf("%02x", digest[i]);
  putchar('\n');
  return 0;
}

/* The stub ERC-20 tokens (MY CALLs 153, 157): balanceOf, allowance, transfer
 * and transferFrom. Storage as solc writes it: balanceOf[a] at
 * keccak256(a . 0), allowance[o][s] at keccak256(s . keccak256(o . 1)); slot
 * 2 is the hook flag. No Transfer record. standard returns true; noreturn
 * returns no data (the USDT form); false returns false and moves nothing;
 * revert reverts; fee keeps 1 unit of each move; hook is standard and, in
 * its first transferFrom, calls deposit(0, value) back on the caller. */
typedef enum { TOKEN_STANDARD, TOKEN_NORETURN, TOKEN_FALSE, TOKEN_REVERT, TOKEN_FEE, TOKEN_HOOK, TOKEN_NONE } Variant;

static const char *const VARIANTS[TOKEN_NONE] = { "standard", "noreturn", "false", "revert", "fee", "hook" };

static Variant variant_of(const char *text) {
  size_t i = 0;
  while (i < TOKEN_NONE && strcmp(text, VARIANTS[i]) != 0)
    i++;
  return (Variant)i;
}

/* spender owner -> the allowance slot. */
static void allowance_slot(Asm *a) {
  asm_slot(a, 1);
  asm_push(a, 0x20);
  asm_op(a, OP_MSTORE);
  asm_push(a, 0);
  asm_op(a, OP_MSTORE);
  asm_push(a, 0x40);
  asm_push(a, 0);
  asm_op(a, OP_SHA3);
}

/* slot -> ; slot -= calldata word j, reverts below zero. */
static void debit(Asm *a, unsigned j) {
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_argument(a, j);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_DUP3);
  asm_op(a, OP_LT);
  asm_revert_if(a);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
}

/* slot -> ; slot += calldata word j - fee, reverts on a wrap. */
static void credit(Asm *a, unsigned j, unsigned fee) {
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_argument(a, j);
  if (fee > 0) {
    asm_push(a, fee);
    asm_op(a, OP_SWAP1);
    asm_op(a, OP_SUB);
  }
  asm_checked_add(a);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
}

/* The first transferFrom calls deposit(0, value) on the caller and ignores the result. */
static void hook(Asm *a) {
  static const char text[] = "deposit(uint256,uint256)";
  unsigned char digest[32], word[32] = {0};
  lang_keccak256((const unsigned char *)text, strlen(text), digest);
  memcpy(word, digest, 4);
  Label done = asm_label(a);
  asm_push(a, 2);
  asm_op(a, OP_SLOAD);
  asm_jump_if(a, done);
  asm_push(a, 1);
  asm_push(a, 2);
  asm_op(a, OP_SSTORE);
  asm_push_word(a, word);
  asm_push(a, 0x80);
  asm_op(a, OP_MSTORE);
  asm_push(a, 0);
  asm_push(a, 0x84);
  asm_op(a, OP_MSTORE);
  asm_argument(a, 2);
  asm_push(a, 0xa4);
  asm_op(a, OP_MSTORE);
  asm_push(a, 0);
  asm_push(a, 0);
  asm_push(a, 0x44);
  asm_push(a, 0x80);
  asm_push(a, 0);
  asm_op(a, OP_CALLER);
  asm_op(a, OP_GAS);
  asm_op(a, OP_CALL);
  asm_op(a, OP_POP);
  asm_jumpdest(a, done);
}

/* The result of a transfer or transferFrom that moved the value. */
static void moved(Asm *a, Variant variant) {
  if (variant == TOKEN_NORETURN) {
    asm_put(a, 0x00);  /* STOP */
    return;
  }
  asm_push(a, 1);
  asm_return_top(a);
}

/* true: the variant moves no value (false returns false, revert reverts). */
static int refuses(Asm *a, Variant variant) {
  if (variant == TOKEN_REVERT)
    asm_jump(a, LABEL_REVERT);
  if (variant == TOKEN_FALSE) {
    asm_push(a, 0);
    asm_return_top(a);
  }
  return variant == TOKEN_REVERT || variant == TOKEN_FALSE;
}

static void token_code(Asm *a, Variant variant) {
  static const unsigned long selectors[4] = { 0x70a08231UL, 0xdd62ed3eUL, 0xa9059cbbUL, 0x23b872ddUL };
  Label entry[4];
  unsigned fee = variant == TOKEN_FEE ? 1 : 0;
  asm_push(a, 0);
  asm_op(a, OP_CALLDATALOAD);
  asm_push(a, 0xe0);
  asm_op(a, OP_SHR);
  for (size_t i = 0; i < 4; i++) {
    entry[i] = asm_label(a);
    asm_op(a, OP_DUP1);
    asm_push(a, selectors[i]);
    asm_op(a, OP_EQ);
    asm_jump_if(a, entry[i]);
  }
  asm_jumpdest(a, LABEL_REVERT);
  asm_push(a, 0);
  asm_push(a, 0);
  asm_op(a, OP_REVERT);
  asm_jumpdest(a, entry[0]);  /* balanceOf(address) */
  asm_argument(a, 0);
  asm_slot(a, 0);
  asm_op(a, OP_SLOAD);
  asm_return_top(a);
  asm_jumpdest(a, entry[1]);  /* allowance(address,address) */
  asm_argument(a, 1);
  asm_argument(a, 0);
  allowance_slot(a);
  asm_op(a, OP_SLOAD);
  asm_return_top(a);
  asm_jumpdest(a, entry[2]);  /* transfer(address,uint256) */
  if (!refuses(a, variant)) {
    asm_op(a, OP_CALLER);
    asm_slot(a, 0);
    debit(a, 1);
    asm_argument(a, 0);
    asm_slot(a, 0);
    credit(a, 1, fee);
    moved(a, variant);
  }
  asm_jumpdest(a, entry[3]);  /* transferFrom(address,address,uint256) */
  if (!refuses(a, variant)) {
    asm_op(a, OP_CALLER);
    asm_argument(a, 0);
    allowance_slot(a);
    debit(a, 2);
    asm_argument(a, 0);
    asm_slot(a, 0);
    debit(a, 2);
    asm_argument(a, 1);
    asm_slot(a, 0);
    credit(a, 2, fee);
    if (variant == TOKEN_HOOK)
      hook(a);
    moved(a, variant);
  }
}

/* The label sites of A, as resolve in src/evm.c. */
static int resolved(Asm *a) {
  int ok = a->full == ASM_ROOM && !a->labels_full;
  for (size_t i = 0; ok && i < a->sites; i++) {
    Label label = a->target[i];
    ok = a->bound[label] && a->at[label] <= 0xffff;
    a->code[a->site[i]] = (unsigned char)(a->at[label] >> 8);
    a->code[a->site[i] + 1] = (unsigned char)a->at[label];
  }
  return ok;
}

static int token(const char *text) {
  static Asm a;
  Variant variant = variant_of(text);
  if (variant == TOKEN_NONE)
    return usage();
  token_code(&a, variant);
  if (!resolved(&a))
    return 1;
  for (size_t i = 0; i < a.size; i++)
    printf("%02x", a.code[i]);
  putchar('\n');
  return 0;
}

static Verb verb_of(const char *text) {
  return strcmp(text, "creation") == 0 ? VERB_CREATION
         : strcmp(text, "runtime") == 0 ? VERB_RUNTIME
         : strcmp(text, "amend") == 0   ? VERB_AMEND
                                        : VERB_NONE;
}

static int regime_of(const char *text, LangRegime *regime) {
  *regime = strcmp(text, "debreu") == 0 ? LANG_REGIME_DEBREU : LANG_REGIME_IMPOSSIBILITY;
  return strcmp(text, "debreu") == 0 || strcmp(text, "impossibility") == 0;
}

static int emit(Verb verb, const LangContract *contract) {
  switch (verb) {
    case VERB_CREATION:
      return lang_evm_write(contract, LANG_PART_CREATION, stdout, stderr);
    case VERB_RUNTIME:
      return lang_evm_write(contract, LANG_PART_RUNTIME, stdout, stderr);
    case VERB_AMEND:
      return lang_evm_amend(contract, stdout, stderr);
    case VERB_NONE:
      return usage();
  }
  return usage();
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "keccak") == 0)
    return keccak(argv[2]);
  if (argc == 3 && strcmp(argv[1], "token") == 0)
    return token(argv[2]);
  int shift = argc >= 3 && strcmp(argv[1], "-k") == 0 ? 2 : 0;
  long decisions = shift == 0 ? 3 : number(argv[2]);  /* the sample domain: k = 3 */
  int argn = argc - shift;
  char **arg = argv + shift;
  Verb verb = argn >= 4 ? verb_of(arg[1]) : VERB_NONE;
  LangRegime regime;
  if (argn < 4 || argn - 4 > TOOL_CODES || decisions < 0 || verb == VERB_NONE || !regime_of(arg[3], &regime))
    return usage();
  long members = number(arg[2]);
  if (members < 0)
    return usage();
  unsigned char codes[TOOL_CODES];
  size_t count = (size_t)(argn - 4);
  for (size_t i = 0; i < count; i++) {
    long code = number(arg[4 + i]);
    if (code < 0 || code > 255)
      return usage();
    codes[i] = (unsigned char)code;
  }
  int listed = regime == LANG_REGIME_DEBREU || count > 0;
  LangContract contract = { (unsigned)members, regime, listed ? codes : NULL, count, (unsigned)decisions, NULL };
  return emit(verb, &contract) == 0 ? 0 : 1;
}
