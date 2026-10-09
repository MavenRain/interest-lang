/* Exercise signature limits through the public writer and a small domain. */
#include "asm.h"
#include <string.h>

static Entry fixture;

static void constant(Asm *a, const EntryContext *c) {
  (void)c;
  asm_push(a, 1);
  asm_return_top(a);
}

const Entry *lang_domain_entries(LangRegime regime, size_t *count) {
  (void)regime;
  *count = 1;
  return &fixture;
}

void lang_domain_genesis(Asm *a, const LangContract *contract) {
  (void)a;
  (void)contract;
}

void lang_domain_data(Asm *a, const EntryContext *c) {
  (void)a;
  (void)c;
}

static int check(const char *label, const char *name, int ballots, int accepted) {
  unsigned char codes[2080];
  memset(codes, 1, sizeof codes);
  LangContract contract = {ballots ? 63u : 1u, LANG_REGIME_DEBREU,
                           codes, ballots ? sizeof codes : 3u, 3, NULL};
  fixture = (Entry){name, 0, ballots, ENTRY_NONPAYABLE,
                    ballots ? lang_entry_cast : constant, NULL};
  FILE *out = tmpfile();
  FILE *err = tmpfile();
  if (out == NULL || err == NULL) {
    if (out != NULL) fclose(out);
    if (err != NULL) fclose(err);
    fputs("evm-boundaries: cannot open temporary streams\n", stderr);
    return 1;
  }
  int status = lang_evm_write(&contract, LANG_PART_RUNTIME, out, err);
  long size = ftell(out);
  rewind(err);
  char diagnostic[1024] = {0};
  size_t read = fread(diagnostic, 1, sizeof diagnostic - 1, err);
  diagnostic[read] = '\0';
  int failed = accepted ? status != 0 || size <= 0 || read != 0
                        : status == 0 || size != 0 || strstr(diagnostic, "EVM_SIZE") == NULL;
  if (failed)
    fprintf(stderr, "evm-boundaries: %s: expected %s, status=%d output=%ld: %s\n",
            label, accepted ? "acceptance" : "EVM_SIZE", status, size, diagnostic);
  fclose(out);
  fclose(err);
  return failed;
}

int main(void) {
  char name[511];
  memset(name, 'a', sizeof name);
  name[509] = '\0';
  int failed = check("512 bytes, no arguments", name, 0, 1);
  name[509] = 'a';
  name[510] = '\0';
  failed += check("513 bytes, no arguments", name, 0, 0);
  failed += check("512 bytes, 63 arguments", "settle", 1, 1);
  failed += check("513 bytes, 63 arguments", "settler", 1, 0);
  if (failed == 0)
    puts("evm-boundaries: 4 checks passed");
  return failed ? 1 : 0;
}
