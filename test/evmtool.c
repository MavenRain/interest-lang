/* Test driver of the EVM back end, run with:
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c creation|runtime N debreu CODE...
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c creation|runtime N impossibility [CODE...]
 *   tcc -Isrc src/evm.c src/keccak.c domain/entries.c src/check.c src/arena.c src/diag.c src/printer.c -run test/evmtool.c keccak TEXT
 * Codes go to lang_evm_write unchecked (0 to 255), so the tests reach its
 * EVM_TABLE refusals. Exit 0 ok, 1 refused by the back end, 2 usage. */
#include "../src/evm.h"
#include "../src/keccak.h"
#include <stdlib.h>
#include <string.h>

enum { TOOL_CODES = 512, TOOL_DIGITS = 9 };

static int usage(void) {
  fputs("usage: evmtool creation|runtime N debreu CODE... | evmtool creation|runtime N impossibility [CODE...]"
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

static int part_of(const char *text, LangPart *part) {
  *part = strcmp(text, "creation") == 0 ? LANG_PART_CREATION : LANG_PART_RUNTIME;
  return strcmp(text, "creation") == 0 || strcmp(text, "runtime") == 0;
}

static int regime_of(const char *text, LangRegime *regime) {
  *regime = strcmp(text, "debreu") == 0 ? LANG_REGIME_DEBREU : LANG_REGIME_IMPOSSIBILITY;
  return strcmp(text, "debreu") == 0 || strcmp(text, "impossibility") == 0;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "keccak") == 0)
    return keccak(argv[2]);
  LangPart part;
  LangRegime regime;
  if (argc < 4 || argc - 4 > TOOL_CODES || !part_of(argv[1], &part) || !regime_of(argv[3], &regime))
    return usage();
  long members = number(argv[2]);
  if (members < 0)
    return usage();
  unsigned char codes[TOOL_CODES];
  size_t count = (size_t)(argc - 4);
  for (size_t i = 0; i < count; i++) {
    long code = number(argv[4 + i]);
    if (code < 0 || code > 255)
      return usage();
    codes[i] = (unsigned char)code;
  }
  int listed = regime == LANG_REGIME_DEBREU || count > 0;
  LangContract contract = { (unsigned)members, regime, listed ? codes : NULL, count, 3, NULL };  /* the sample domain: k = 3 */
  return lang_evm_write(&contract, part, stdout, stderr) == 0 ? 0 : 1;
}
