/* The lexer of langc: bytes to tokens (syntax.h). */
#include "syntax.h"
#include <limits.h>
#include <string.h>

enum { LEX_CLIP = 32 };

typedef struct {
  const char *file;
  const char *text;
  size_t size;
  size_t pos;
  Loc loc;
  Span def;  /* the name after the last 'def', 'def rec' or 'mu', for diagnostics */
  Token *tokens;
  size_t count;
  Diag *diag;
} Lexer;

typedef struct {
  const char *text;
  TokenKind kind;
} Spelling;

static const Spelling KEYWORDS[] = {
  {"def", TOK_DEF}, {"rec", TOK_REC}, {"mu", TOK_MU}, {"fun", TOK_FUN},
  {"case", TOK_CASE}, {"match", TOK_MATCH}, {"with", TOK_WITH}, {"as", TOK_AS},
  {"in", TOK_IN}, {"return", TOK_RETURN}, {"inj", TOK_INJ}, {"of", TOK_OF},
  {"tuple", TOK_TUPLE}, {"prod", TOK_PROD}, {"sum", TOK_SUM}, {"Type", TOK_TYPE}
};

/* Two-byte spellings come first, so ':=' wins over ':'. */
static const Spelling PUNCTUATION[] = {
  {":=", TOK_DEFINE}, {"->", TOK_ARROW}, {"=>", TOK_FATARROW}, {"(", TOK_LPAREN},
  {")", TOK_RPAREN}, {",", TOK_COMMA}, {":", TOK_COLON}, {"|", TOK_BAR},
  {"*", TOK_STAR}, {".", TOK_DOT}
};

static int is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_word(char c) { return is_letter(c) || is_digit(c) || c == '_'; }

/* The byte OFFSET bytes ahead, or NUL past the end. */
static char ahead(const Lexer *lx, size_t offset) {
  size_t pos = lx->pos + offset;
  return pos < lx->size ? lx->text[pos] : '\0';
}

static void step(Lexer *lx, size_t length) {
  lx->pos += length;
  lx->loc.col += (int)length;
}

static int lex_fail(Lexer *lx, const char *code, const char *format, ...) {
  va_list args;
  va_start(args, format);
  diag_vat(lx->diag, code, lx->def, lx->file, lx->loc, format, args);
  va_end(args);
  return 0;
}

static size_t run_of(const Lexer *lx, int (*accept)(char)) {
  size_t length = 0;
  while (lx->pos + length < lx->size && accept(lx->text[lx->pos + length]))
    length++;
  return length;
}

static TokenKind word_kind(const char *start, size_t length) {
  for (size_t i = 0; i < sizeof KEYWORDS / sizeof KEYWORDS[0]; i++)
    if (strlen(KEYWORDS[i].text) == length && memcmp(KEYWORDS[i].text, start, length) == 0)
      return KEYWORDS[i].kind;
  return TOK_NAME;
}

static size_t punctuation(const Lexer *lx, TokenKind *kind) {
  for (size_t i = 0; i < sizeof PUNCTUATION / sizeof PUNCTUATION[0]; i++) {
    size_t length = strlen(PUNCTUATION[i].text);
    int fits = lx->pos + length <= lx->size;
    if (fits && memcmp(lx->text + lx->pos, PUNCTUATION[i].text, length) == 0) {
      *kind = PUNCTUATION[i].kind;
      return length;
    }
  }
  return 0;
}

/* Skips one blank, newline or comment. Returns 0 at a token or at the end. */
static int skip(Lexer *lx) {
  char c = ahead(lx, 0);
  if (lx->pos >= lx->size)
    return 0;
  if (c == '\n') {
    lx->pos++;
    lx->loc.line++;
    lx->loc.col = 1;
    return 1;
  }
  if (c == ' ' || c == '\t' || c == '\r') {
    step(lx, 1);
    return 1;
  }
  if (c != '-' || ahead(lx, 1) != '-')
    return 0;
  while (lx->pos < lx->size && lx->text[lx->pos] != '\n')
    step(lx, 1);
  return 1;
}

static int fail_byte(Lexer *lx, unsigned char byte) {
  if (byte >= 0x21 && byte <= 0x7e)
    return lex_fail(lx, "LEX_TOKEN", "unexpected character '%c'", (int)byte);
  return lex_fail(lx, "LEX_TOKEN", "unexpected byte 0x%02x", (unsigned)byte);
}

static int number_value(Lexer *lx, Token *token) {
  unsigned long long value = 0;
  int clip = token->text.length < LEX_CLIP ? (int)token->text.length : LEX_CLIP;
  for (size_t i = 0; i < token->text.length; i++) {
    unsigned digit = (unsigned)(token->text.start[i] - '0');
    if (value > (ULLONG_MAX - digit) / 10)
      return lex_fail(lx, "LEX_NUMBER", "the literal %.*s is larger than %llu", clip,
                      token->text.start, ULLONG_MAX);
    value = value * 10 + digit;
  }
  token->value = value;
  return 1;
}

static void remember_def(Lexer *lx, const Token *token) {
  TokenKind before = lx->count > 0 ? lx->tokens[lx->count - 1].kind : TOK_EOF;
  int named = before == TOK_DEF || before == TOK_REC || before == TOK_MU;
  lx->def = token->kind == TOK_NAME && named ? token->text : lx->def;
}

static int lex_token(Lexer *lx) {
  Token *token = &lx->tokens[lx->count];
  char c = lx->text[lx->pos];
  size_t length = 0;
  TokenKind kind = TOK_EOF;
  if (is_letter(c)) {
    length = run_of(lx, is_word);
    kind = word_kind(lx->text + lx->pos, length);
  }
  if (is_digit(c)) {
    length = run_of(lx, is_digit);
    kind = TOK_NUMBER;
  }
  if (length == 0)
    length = punctuation(lx, &kind);
  if (length == 0)
    return fail_byte(lx, (unsigned char)c);
  token->kind = kind;
  token->text.start = lx->text + lx->pos;
  token->text.length = length;
  token->loc = lx->loc;
  token->value = 0;
  if (kind == TOK_NUMBER && is_word(ahead(lx, length)))
    return lex_fail(lx, "LEX_NUMBER", "a letter follows the literal %.*s",
                    length < LEX_CLIP ? (int)length : LEX_CLIP, token->text.start);
  if (kind == TOK_NUMBER && !number_value(lx, token))
    return 0;
  remember_def(lx, token);
  lx->count++;
  step(lx, length);
  return 1;
}

int lang_lex(Arena *arena, const char *file, const char *text, size_t size, Token **tokens,
               size_t *count, Diag *diag) {
  Lexer lx = { file, text, size, 0, { 1, 1 }, span_of("-"), NULL, 0, diag };
  /* A token takes at least one byte, so SIZE + 1 slots hold the tokens and the end. */
  lx.tokens = size <= (size_t)LANG_SOURCE_MAX ? arena_alloc(arena, (size + 1) * sizeof(Token)) : NULL;
  if (lx.tokens == NULL) {
    diag_set(diag, "MEMORY", lx.def, "%s: no memory for the tokens of %zu bytes", file, size);
    return LANG_EXIT_REFUSED;
  }
  int ok = 1;
  while (ok && lx.pos < lx.size)
    ok = skip(&lx) || lex_token(&lx);
  if (!ok)
    return LANG_EXIT_REFUSED;
  Token *end = &lx.tokens[lx.count];
  end->kind = TOK_EOF;
  end->text.start = text + size;
  end->text.length = 0;
  end->loc = lx.loc;
  end->value = 0;
  *tokens = lx.tokens;
  *count = lx.count + 1;
  return LANG_EXIT_OK;
}
