/* The langc front end: diagnostics, tokens, the AST of interest-lang, the
 * lexer, the parser and the printer of the canonical form.
 *
 * The syntax is the assay subset that domain/domain.lang and the programs
 * in examples use (host FORMERS.md; test/parser-arms.lang has each form):
 *
 *   program := decl*
 *   decl    := 'def' ['rec'] NAME ':' term ':=' term
 *            | 'mu' NAME ':' term ':=' ('|' NAME binder* ':' term)+
 *   term    := 'fun' binder+ '=>' term
 *            | 'case' term 'with' '|' '0' binder '=>' term '|' '1' binder '=>' term
 *            | 'match' term 'as' NAME 'in' NAME NAME* 'return' term 'with'
 *                ('|' NAME (['0'] NAME)* '=>' term)+
 *            | binder '->' term | binder '*' term
 *            | app ['->' term]
 *   app     := 'Type' NUM | 'inj' NUM 'of' NUM postfix
 *            | ('tuple' | 'prod' | 'sum') '(' [term ',' term] ')'
 *            | postfix postfix*
 *   postfix := atom ('.' NUM)*
 *   atom    := NAME | NUM | '(' term ')' | '(' term ',' term ')'
 *   binder  := '(' ['0'] NAME ':' term ')'
 *
 * A '0' before a name marks an erased binder or pattern variable. A comment
 * runs from '--' to the end of the line. The arms of a case and of a match
 * reach as far as they can, so an arm that is not the last one puts a case
 * or a match in parentheses. */
#ifndef LANG_SYNTAX_H
#define LANG_SYNTAX_H
#include "arena.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

enum {
  LANG_DEPTH_MAX = 512,        /* parser nesting and AST depth: recursion over an AST stays this deep */
  LANG_SOURCE_MAX = 1 << 20,   /* bytes of one source file */
  LANG_ARENA_MAX = 256 << 20   /* bytes of the arena of one run */
};

typedef enum { LANG_EXIT_OK = 0, LANG_EXIT_REFUSED = 1, LANG_EXIT_USAGE = 2 } LangExit;

typedef struct { int line; int col; } Loc;  /* both from 1; col counts bytes */
typedef struct { const char *start; size_t length; } Span;

/* ---- diagnostics ---- */

enum { DIAG_DEF = 64, DIAG_TEXT = 384 };

/* The first error of a run, written as "interestc: CODE: DEF: TEXT". DEF is
 * the declaration in which the error lies, or "-". A type mismatch adds
 * " expected E, found F" with both normal forms. */
typedef struct {
  const char *code;  /* NULL while there is no error */
  char def[DIAG_DEF];
  char text[DIAG_TEXT];
  const struct Ast *expected;  /* NULL, or the expected normal form */
  const struct Ast *found;     /* NULL, or the normal form found */
} Diag;

Span span_of(const char *text);
void diag_init(Diag *diag);
/* Each records the error unless DIAG holds one already. diag_vat puts
 * "FILE:LINE:COL: " before the message. */
void diag_set(Diag *diag, const char *code, Span def, const char *format, ...);
void diag_vat(Diag *diag, const char *code, Span def, const char *file, Loc loc, const char *format,
              va_list args);
/* Writes the error, if there is one, to ERR. */
void diag_print(const Diag *diag, FILE *err);

/* Reads PATH into the arena, with a NUL after the last byte. Returns
 * LANG_EXIT_OK, or LANG_EXIT_USAGE with IO_READ, IO_SIZE or MEMORY. */
int lang_read_source(Arena *arena, const char *path, const char **text, size_t *size, Diag *diag);

/* ---- tokens ---- */

typedef enum {
  TOK_EOF, TOK_NAME, TOK_NUMBER,
  TOK_DEF, TOK_REC, TOK_MU, TOK_FUN, TOK_CASE, TOK_MATCH, TOK_WITH, TOK_AS, TOK_IN,
  TOK_RETURN, TOK_INJ, TOK_OF, TOK_TUPLE, TOK_PROD, TOK_SUM, TOK_TYPE,
  TOK_LPAREN, TOK_RPAREN, TOK_COMMA, TOK_COLON, TOK_DEFINE, TOK_ARROW, TOK_FATARROW,
  TOK_BAR, TOK_STAR, TOK_DOT
} TokenKind;

typedef struct {
  TokenKind kind;
  Span text;
  Loc loc;
  unsigned long long value;  /* TOK_NUMBER */
} Token;

/* Splits TEXT (SIZE bytes) into tokens that end with one TOK_EOF. Returns
 * LANG_EXIT_OK, or LANG_EXIT_REFUSED with LEX_TOKEN, LEX_NUMBER or
 * MEMORY. */
int lang_lex(Arena *arena, const char *file, const char *text, size_t size, Token **tokens,
               size_t *count, Diag *diag);

/* ---- AST ---- */

typedef struct Ast Ast;

typedef struct {
  const char *name;  /* NULL in the arrow A -> B */
  int erased;        /* (0 x : A) */
  Ast *type;
  Loc loc;
} Binder;

typedef struct {
  Binder binder;  /* | k (x : A) => body */
  Ast *body;
} CaseArm;

typedef struct {
  const char *name;
  int erased;  /* | reflNat 0 z => ... */
  Loc loc;
} Pattern;

typedef struct {
  const char *ctor;
  Loc loc;
  Pattern *vars;
  size_t nvars;
  Ast *body;
} MatchArm;

typedef enum {
  AST_VAR,     /* x; the checker resolves Nat and the Nat built-ins (check.c) */
  AST_NAT,     /* 3 */
  AST_TYPE,    /* Type 0, Type 1 */
  AST_PI,      /* (x : A) -> B, (0 x : A) -> B, A -> B */
  AST_SIGMA,   /* (x : A) * B */
  AST_LAM,     /* fun (x : A) => t; fun (x : A) (y : B) => t is two */
  AST_APP,     /* f a */
  AST_PAIR,    /* (a, b), the pair of a Sigma */
  AST_TUPLE0,  /* tuple () */
  AST_TUPLE,   /* tuple (a, b) */
  AST_PROD0,   /* prod () */
  AST_PROD,    /* prod (A, B) */
  AST_SUM,     /* sum (A, B) */
  AST_INJ,     /* inj k of 2 v */
  AST_CASE,    /* case s with | 0 (x : A) => t | 1 (y : B) => u */
  AST_MATCH,   /* match s as w in F i j return T with | c x 0 y => t ... */
  AST_PROJ     /* t.0, t.1 */
} AstKind;

struct Ast {
  AstKind kind;
  Loc loc;
  int depth;  /* 1 + the depth of the deepest child; at most LANG_DEPTH_MAX */
  union {
    const char *name;                                       /* AST_VAR */
    unsigned long long nat;                                 /* AST_NAT */
    unsigned level;                                         /* AST_TYPE */
    struct { Binder binder; Ast *body; } bind;              /* AST_PI, AST_SIGMA, AST_LAM */
    struct { Ast *fun; Ast *arg; } app;                     /* AST_APP */
    struct { Ast *left; Ast *right; } pair;                 /* AST_PAIR, AST_TUPLE, AST_PROD, AST_SUM */
    struct { unsigned tag; unsigned arity; Ast *value; } inj; /* AST_INJ */
    struct { Ast *subject; CaseArm arms[2]; } cases;        /* AST_CASE */
    struct {
      Ast *subject;
      const char *self;      /* as w */
      const char *family;    /* in F */
      const char **indices;  /* i j */
      size_t nindices;
      Ast *motive;           /* return T */
      MatchArm *arms;
      size_t narms;
    } match;                                                /* AST_MATCH */
    struct { Ast *term; unsigned index; } proj;             /* AST_PROJ */
  } u;
};

typedef struct {
  const char *name;
  Loc loc;
  Ast *type;  /* | c (x : A) : T is c : (x : A) -> T */
} Ctor;

typedef enum { DECL_DEF, DECL_REC, DECL_MU } DeclKind;

typedef struct {
  DeclKind kind;
  const char *name;
  Loc loc;
  Ast *type;
  Ast *body;    /* DECL_DEF, DECL_REC */
  Ctor *ctors;  /* DECL_MU */
  size_t nctors;
} Decl;

typedef struct {
  const char *file;
  Decl *decls;
  size_t ndecls;
} Program;

/* Parses TEXT (SIZE bytes) of FILE. Returns LANG_EXIT_OK, or
 * LANG_EXIT_REFUSED with the first error: LEX_TOKEN, LEX_NUMBER,
 * PARSE_EXPECT, PARSE_PAREN, PARSE_ARITY, PARSE_DEPTH or MEMORY. */
int lang_parse(Arena *arena, const char *file, const char *text, size_t size, Program *program,
                 Diag *diag);

/* The canonical form: one line per declaration and per constructor (a
 * constructor as NAME : TYPE), no comments, and parentheses where the
 * grammar needs them, around a Sigma body that is not an application, and
 * around a tuple, prod or sum that is an argument. The parse of the
 * canonical form prints the same bytes. */
void lang_print_program(FILE *out, const Program *program);
void lang_print_term(FILE *out, const Ast *ast);
#endif
