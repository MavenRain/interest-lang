/* Diagnostics and source files of the langc front end. */
#include "syntax.h"
#include <errno.h>
#include <string.h>

Span span_of(const char *text) {
  Span span = { text, strlen(text) };
  return span;
}

void diag_init(Diag *diag) {
  diag->code = NULL;
  diag->def[0] = '\0';
  diag->text[0] = '\0';
  diag->expected = NULL;
  diag->found = NULL;
}

static void diag_begin(Diag *diag, const char *code, Span def) {
  int length = def.length < DIAG_DEF ? (int)def.length : DIAG_DEF - 1;
  diag->code = code;
  snprintf(diag->def, sizeof diag->def, "%.*s", length, def.start);
}

void diag_set(Diag *diag, const char *code, Span def, const char *format, ...) {
  if (diag->code != NULL)
    return;
  diag_begin(diag, code, def);
  va_list args;
  va_start(args, format);
  vsnprintf(diag->text, sizeof diag->text, format, args);
  va_end(args);
}

void diag_vat(Diag *diag, const char *code, Span def, const char *file, Loc loc, const char *format,
              va_list args) {
  if (diag->code != NULL)
    return;
  diag_begin(diag, code, def);
  int prefix = snprintf(diag->text, sizeof diag->text, "%s:%d:%d: ", file, loc.line, loc.col);
  size_t used = prefix < 0 ? 0 : (size_t)prefix;
  used = used < sizeof diag->text ? used : sizeof diag->text - 1;
  vsnprintf(diag->text + used, sizeof diag->text - used, format, args);
}

void diag_print(const Diag *diag, FILE *err) {
  if (diag->code == NULL)
    return;
  fprintf(err, "interestc: %s: %s: %s", diag->code, diag->def, diag->text);
  if (diag->expected != NULL && diag->found != NULL) {
    fputs(" expected ", err);
    lang_print_term(err, diag->expected);
    fputs(", found ", err);
    lang_print_term(err, diag->found);
  }
  fputc('\n', err);
}

int lang_read_source(Arena *arena, const char *path, const char **text, size_t *size, Diag *diag) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    diag_set(diag, "IO_READ", span_of("-"), "%s: %s", path, strerror(errno));
    return LANG_EXIT_USAGE;
  }
  char *buffer = arena_alloc(arena, (size_t)LANG_SOURCE_MAX + 2);
  size_t length = buffer == NULL ? 0 : fread(buffer, 1, (size_t)LANG_SOURCE_MAX + 1, file);
  int broken = buffer != NULL && ferror(file);
  int error = errno;
  fclose(file);
  if (buffer == NULL) {
    diag_set(diag, "MEMORY", span_of("-"), "%s: no memory for the source", path);
    return LANG_EXIT_USAGE;
  }
  if (broken) {
    diag_set(diag, "IO_READ", span_of("-"), "%s: %s", path, strerror(error));
    return LANG_EXIT_USAGE;
  }
  if (length > (size_t)LANG_SOURCE_MAX) {
    diag_set(diag, "IO_SIZE", span_of("-"), "%s: the file is larger than %d bytes", path,
             LANG_SOURCE_MAX);
    return LANG_EXIT_USAGE;
  }
  buffer[length] = '\0';
  *text = buffer;
  *size = length;
  return LANG_EXIT_OK;
}
