/* Test driver of the interestc front end, built by make as build/parsetool,
 * or run with:
 *   tcc src/arena.c src/diag.c src/lexer.c src/parser.c src/printer.c build/domain.c -run test/parsetool.c FILE
 *   tcc ... -run test/parsetool.c --prelude
 * FILE prints the canonical form of FILE (src/syntax.h); --prelude prints
 * the bytes of the embedded prelude. Exit 0 ok, 1 refused by the parser,
 * 2 usage or IO. */
#include "../src/prelude.h"
#include "../src/syntax.h"
#include <string.h>

static int usage(void) {
  fputs("usage: parsetool FILE | --prelude\n", stderr);
  return LANG_EXIT_USAGE;
}

static int print_prelude(void) {
  size_t written = fwrite(domain_source, 1, domain_source_len, stdout);
  return written == domain_source_len ? LANG_EXIT_OK : LANG_EXIT_USAGE;
}

static int print_file(Arena *arena, const char *path, Diag *diag) {
  const char *text = NULL;
  size_t size = 0;
  int status = lang_read_source(arena, path, &text, &size, diag);
  if (status != LANG_EXIT_OK)
    return status;
  Program program;
  status = lang_parse(arena, path, text, size, &program, diag);
  if (status != LANG_EXIT_OK)
    return status;
  lang_print_program(stdout, &program);
  return LANG_EXIT_OK;
}

int main(int argc, char **argv) {
  if (argc != 2)
    return usage();
  if (strcmp(argv[1], "--prelude") == 0)
    return print_prelude();
  Arena arena;
  Diag diag;
  arena_init(&arena, LANG_ARENA_MAX);
  diag_init(&diag);
  int status = print_file(&arena, argv[1], &diag);
  diag_print(&diag, stderr);
  arena_free(&arena);
  return status;
}
