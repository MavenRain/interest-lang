/* Test driver of the EVM back end, run with:
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c [-k K] creation|runtime N debreu CODE...
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c [-k K] creation|runtime N impossibility [CODE...]
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c [-k K] amend N debreu CODE...
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c keccak TEXT
 * K is the number of decision values (3 when not given: the sample domain).
 * amend writes only the body of lang_entry_amend (lang_evm_amend). Codes go to
 * the back end unchecked (0 to 255), so the tests reach its EVM_TABLE
 * refusals. Exit 0 ok, 1 refused by the back end, 2 usage. */
#include "../src/evm.h"
#include "../src/keccak.h"
#include <stdlib.h>
#include <string.h>

enum { TOOL_CODES = 4096, TOOL_DIGITS = 9 };  /* TOOL_CODES: the tallies of the largest verdict table */

typedef enum { VERB_CREATION, VERB_RUNTIME, VERB_AMEND, VERB_NONE } Verb;

static int usage(void) {
  fputs("usage: evmtool [-k K] creation|runtime|amend N debreu CODE... | evmtool [-k K] creation|runtime N impossibility [CODE...]"
        " | evmtool keccak TEXT\n", stderr);
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
