/* Compare compiled metadata with the independent cast ABI encoder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name, *symbol; } Metadata;

static int command(const char *cmd) {
  if (system(cmd) == 0)
    return 1;
  fprintf(stderr, "metadata: command failed: %s\n", cmd);
  return 0;
}

static int text_def(FILE *out, const char *name, const char *text) {
  if (text == NULL)
    return 1;
  if (fprintf(out, "def %s : Text := ", name) < 0)
    return 0;
  size_t n = strlen(text);
  for (size_t i = 0; i < n; i++)
    if (fprintf(out, "char %u (", (unsigned char)text[i]) < 0)
      return 0;
  if (fputs("end", out) == EOF)
    return 0;
  for (size_t i = 0; i < n; i++)
    if (fputc(')', out) == EOF)
      return 0;
  return fputc('\n', out) != EOF;
}

static int read_line(const char *path, char *line, size_t size) {
  FILE *in = fopen(path, "r");
  if (in == NULL)
    return 0;
  int ok = fgets(line, (int)size, in) != NULL;
  if (ok)
    line[strcspn(line, "\r\n")] = '\0';
  return fclose(in) == 0 && ok;
}

static int view(const char *selector, const char *value, unsigned regime,
                unsigned token, size_t row) {
  char cmd[512], got[256], wanted[256], bytes[65];
  snprintf(cmd, sizeof cmd,
           "evm --verbosity 0 run --codefile .gatework/metadata/runtime.hex "
           "--input %s > .gatework/metadata/actual", selector);
  if (!command(cmd))
    return 0;
  size_t length = strlen(value);
  for (size_t i = 0; i < length; i++)
    snprintf(bytes + 2 * i, sizeof bytes - 2 * i, "%02x", (unsigned char)value[i]);
  bytes[2 * length] = '\0';
  /* bytes and string have the same ABI layout. Hex preserves spaces that
   * cast trims from string arguments. */
  snprintf(cmd, sizeof cmd,
           "cast abi-encode 'f(bytes)' 0x%s > .gatework/metadata/expected", bytes);
  if (!command(cmd) || !read_line(".gatework/metadata/actual", got, sizeof got) ||
      !read_line(".gatework/metadata/expected", wanted, sizeof wanted))
    return 0;
  if (strcmp(got, wanted) == 0)
    return 1;
  fprintf(stderr, "metadata: regime=%u token=%u row=%zu selector=%s\n"
          "expected %s\nactual   %s\n", regime, token, row, selector, wanted, got);
  return 0;
}

int main(void) {
  static const Metadata rows[] = {
    {NULL, NULL}, {"", ""}, {" ", "~"}, {"a", NULL}, {NULL, "b"},
    {"1234567890123456789012345678901", "12345678901234567890123456789012"},
    {"12345678901234567890123456789012", "1234567890123456789012345678901"}
  };
  if (!command("mkdir -p .gatework/metadata"))
    return 1;
  unsigned cases = 0;
  for (unsigned regime = 0; regime < 2; regime++)
    for (unsigned token = 0; token < 2; token++)
      for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        FILE *out = fopen(".gatework/metadata/program.lang", "w");
        if (out == NULL)
          return 1;
        int ok = fputs("def members : Nat := 3\n", out) != EOF;
        if (regime)
          ok = ok && fputs("def F : ChoiceRule := fun (x : Config) => open\n"
                          "def agg : Aggregation F := mkAgg F (fun (t : Tally) => open) "
                          "(fun (x : Config) => reflDec open)\n", out) != EOF;
        if (token)
          ok = ok && fputs("def asset : AssetMode := token\n", out) != EOF;
        ok = ok && text_def(out, "name", rows[i].name) && text_def(out, "symbol", rows[i].symbol);
        int closed = fclose(out) == 0;
        if (!ok || !closed ||
            !command("build/interestc build .gatework/metadata/program.lang --runtime "
                     "-o .gatework/metadata/runtime.hex") ||
            !view("06fdde03", rows[i].name == NULL ? "interest" : rows[i].name, regime, token, i) ||
            !view("95d89b41", rows[i].symbol == NULL ? "INT" : rows[i].symbol, regime, token, i))
          return 1;
        cases += 2;
      }
  printf("metadata: %u ABI comparisons\n", cases);
  return 0;
}
