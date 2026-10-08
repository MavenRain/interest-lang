/* The parser of langc: tokens to the AST (syntax.h has the grammar). */
#include "syntax.h"
#include <string.h>

enum { TEXT_CLIP = 24 };

typedef struct {
  Arena *arena;
  const char *file;
  const Token *tokens;
  size_t count;
  size_t pos;
  int depth;  /* parse_term calls in progress */
  Span def;   /* the declaration being parsed, for diagnostics */
  Diag *diag;
} Parser;

/* ---- tokens ---- */

static const Token *peek_at(const Parser *p, size_t offset) {
  size_t pos = p->pos + offset;
  return &p->tokens[pos < p->count ? pos : p->count - 1];
}

static const Token *peek(const Parser *p) { return peek_at(p, 0); }
static int at(const Parser *p, TokenKind kind) { return peek(p)->kind == kind; }

static const Token *advance(Parser *p) {
  const Token *token = peek(p);
  p->pos += token->kind == TOK_EOF ? 0 : 1;
  return token;
}

/* ---- errors: each records the first error and returns NULL ---- */

static void *fail(Parser *p, const char *code, Loc loc, const char *format, ...) {
  va_list args;
  va_start(args, format);
  diag_vat(p->diag, code, p->def, p->file, loc, format, args);
  va_end(args);
  return NULL;
}

static int clip(const Token *token) {
  return token->text.length < TEXT_CLIP ? (int)token->text.length : TEXT_CLIP;
}

static void *fail_found(Parser *p, const char *what) {
  const Token *token = peek(p);
  if (token->kind == TOK_EOF)
    return fail(p, "PARSE_EXPECT", token->loc, "expected %s, found the end of the file", what);
  if (token->kind == TOK_RPAREN)
    return fail(p, "PARSE_PAREN", token->loc, "unmatched ')' where %s was expected", what);
  return fail(p, "PARSE_EXPECT", token->loc, "expected %s, found '%.*s'", what, clip(token),
              token->text.start);
}

/* The assay forms that langc refuses (host README, Refusals). */
static const char *const REFUSED_FORMS[] = {
  "nu", "axiom", "contract", "storage", "entry", "payable", "constructor", "fallback", "error",
  "invariant", "predicate", "proof", "guard", "sload", "sstore"
};

/* The refused form that TOKEN names, or NULL. A form name ends the spine of
 * an application, so a form after a declaration reaches fail_top. */
static const char *refused_form(const Token *token) {
  const char *form = NULL;
  for (size_t i = 0; token->kind == TOK_NAME && i < sizeof REFUSED_FORMS / sizeof REFUSED_FORMS[0]; i++) {
    size_t length = strlen(REFUSED_FORMS[i]);
    int hit = token->text.length == length && memcmp(token->text.start, REFUSED_FORMS[i], length) == 0;
    form = hit ? REFUSED_FORMS[i] : form;
  }
  return form;
}

/* A declaration starts with neither 'def' nor 'mu'. */
static void fail_top(Parser *p) {
  const Token *token = peek(p);
  const char *form = refused_form(token);
  if (form != NULL) {
    p->def = span_of("-");
    fail(p, "REFUSE_FORM", token->loc, "interestc refuses the %s form", form);
    return;
  }
  fail_found(p, "'def' or 'mu'");
}

static void *no_memory(Parser *p, Loc loc) {
  return fail(p, "MEMORY", loc, "no memory for the AST");
}

static const Token *expect(Parser *p, TokenKind kind, const char *what) {
  if (!at(p, kind))
    return fail_found(p, what);
  return advance(p);
}

static const Token *expect_close(Parser *p, Loc open) {
  const Token *token = peek(p);
  if (token->kind == TOK_RPAREN)
    return advance(p);
  if (token->kind == TOK_EOF)
    return fail(p, "PARSE_PAREN", token->loc, "the '(' at %d:%d is not closed", open.line, open.col);
  return fail(p, "PARSE_PAREN", token->loc, "the '(' at %d:%d is not closed: found '%.*s'", open.line,
              open.col, clip(token), token->text.start);
}

/* A NUMBER token with the value WANT, else PARSE_ARITY with WHAT. */
static const Token *expect_number(Parser *p, unsigned long long want, const char *what) {
  const Token *token = expect(p, TOK_NUMBER, "a number");
  if (token == NULL)
    return NULL;
  if (token->value != want)
    return fail(p, "PARSE_ARITY", token->loc, "%s, found %llu", what, token->value);
  return token;
}

/* ---- arena helpers ---- */

static const char *name_of(Parser *p, const Token *token) {
  char *name = arena_alloc(p->arena, token->text.length + 1);
  if (name == NULL)
    return no_memory(p, token->loc);
  memcpy(name, token->text.start, token->text.length);
  name[token->text.length] = '\0';
  return name;
}

static const char *expect_name(Parser *p, const char *what) {
  const Token *token = expect(p, TOK_NAME, what);
  return token == NULL ? NULL : name_of(p, token);
}

/* ITEMS (COUNT used of *CAP elements of SIZE bytes) with room for one more. */
static void *grow(Parser *p, void *items, size_t count, size_t *cap, size_t size) {
  if (count < *cap)
    return items;
  size_t next = *cap == 0 ? 4 : *cap * 2;
  unsigned char *bigger = arena_alloc(p->arena, next * size);
  if (bigger == NULL)
    return no_memory(p, peek(p)->loc);
  if (count > 0)
    memcpy(bigger, items, count * size);
  *cap = next;
  return bigger;
}

static int deeper(int depth, const Ast *ast) {
  int below = ast == NULL ? 0 : ast->depth;
  return below > depth ? below : depth;
}

/* A node of KIND whose deepest child has depth BELOW. */
static Ast *make(Parser *p, AstKind kind, Loc loc, int below) {
  if (below >= LANG_DEPTH_MAX)
    return fail(p, "PARSE_DEPTH", loc, "the term nests deeper than %d", LANG_DEPTH_MAX);
  Ast *ast = arena_alloc(p->arena, sizeof *ast);
  if (ast == NULL)
    return no_memory(p, loc);
  ast->kind = kind;
  ast->loc = loc;
  ast->depth = below + 1;
  return ast;
}

static Ast *make_bind(Parser *p, AstKind kind, Binder binder, Ast *body) {
  Ast *ast = make(p, kind, binder.loc, deeper(deeper(0, binder.type), body));
  if (ast == NULL)
    return NULL;
  ast->u.bind.binder = binder;
  ast->u.bind.body = body;
  return ast;
}

static Ast *make_pair(Parser *p, AstKind kind, Loc loc, Ast *left, Ast *right) {
  Ast *ast = make(p, kind, loc, deeper(deeper(0, left), right));
  if (ast == NULL)
    return NULL;
  ast->u.pair.left = left;
  ast->u.pair.right = right;
  return ast;
}

/* ---- terms ---- */

static Ast *parse_term(Parser *p);

/* '(' ['0'] NAME ':' starts a binder. */
static int binder_start(const Parser *p) {
  const Token *first = peek_at(p, 1);
  int erased = first->kind == TOK_NUMBER && first->value == 0;
  const Token *name = peek_at(p, erased ? 2 : 1);
  const Token *colon = peek_at(p, erased ? 3 : 2);
  return at(p, TOK_LPAREN) && name->kind == TOK_NAME && colon->kind == TOK_COLON;
}

static int parse_binder(Parser *p, Binder *binder) {
  const Token *open = expect(p, TOK_LPAREN, "'(' of a binder");
  if (open == NULL)
    return 0;
  binder->erased = at(p, TOK_NUMBER) && peek(p)->value == 0;
  p->pos += binder->erased ? 1 : 0;
  binder->loc = open->loc;
  binder->name = expect_name(p, "the name of a binder");
  if (binder->name == NULL || expect(p, TOK_COLON, "':'") == NULL)
    return 0;
  binder->type = parse_term(p);
  return binder->type != NULL && expect_close(p, open->loc) != NULL;
}

/* An atom starts here: NAME (not a refused form), NUMBER or '('. */
static int atom_start(const Parser *p) {
  return (at(p, TOK_NAME) && refused_form(peek_at(p, 0)) == NULL) || at(p, TOK_NUMBER) || at(p, TOK_LPAREN);
}

/* '(' term ')' or the pair '(' term ',' term ')'. */
static Ast *parse_parens(Parser *p) {
  const Token *open = advance(p);
  Ast *left = parse_term(p);
  if (left == NULL)
    return NULL;
  if (!at(p, TOK_COMMA))
    return expect_close(p, open->loc) == NULL ? NULL : left;
  advance(p);
  Ast *right = parse_term(p);
  if (right == NULL || expect_close(p, open->loc) == NULL)
    return NULL;
  return make_pair(p, AST_PAIR, open->loc, left, right);
}

static Ast *parse_atom(Parser *p) {
  const Token *token = peek(p);
  if (token->kind == TOK_LPAREN)
    return parse_parens(p);
  if (token->kind != TOK_NAME && token->kind != TOK_NUMBER)
    return fail_found(p, "a term");
  advance(p);
  Ast *ast = make(p, token->kind == TOK_NAME ? AST_VAR : AST_NAT, token->loc, 0);
  if (ast == NULL)
    return NULL;
  if (token->kind == TOK_NUMBER)
    ast->u.nat = token->value;
  if (token->kind == TOK_NAME)
    ast->u.name = name_of(p, token);
  return token->kind == TOK_NAME && ast->u.name == NULL ? NULL : ast;
}

/* atom ('.' NUM)* */
static Ast *parse_postfix(Parser *p) {
  Ast *ast = parse_atom(p);
  while (ast != NULL && at(p, TOK_DOT)) {
    const Token *dot = advance(p);
    const Token *index = expect(p, TOK_NUMBER, "a projection index after '.'");
    if (index == NULL)
      return NULL;
    if (index->value > 1)
      return fail(p, "PARSE_ARITY", index->loc, "a projection is .0 or .1, found .%llu", index->value);
    Ast *proj = make(p, AST_PROJ, dot->loc, ast->depth);
    if (proj == NULL)
      return NULL;
    proj->u.proj.term = ast;
    proj->u.proj.index = (unsigned)index->value;
    ast = proj;
  }
  return ast;
}

/* KEYWORD '(' [term ',' term] ')' for tuple, prod and sum. */
static Ast *parse_components(Parser *p, AstKind none, AstKind two, const char *what) {
  const Token *keyword = advance(p);
  const Token *open = expect(p, TOK_LPAREN, "'('");
  if (open == NULL)
    return NULL;
  if (at(p, TOK_RPAREN) && none == two)
    return fail(p, "PARSE_ARITY", peek(p)->loc, "%s takes 2 components, found 0", what);
  if (at(p, TOK_RPAREN)) {
    advance(p);
    return make(p, none, keyword->loc, 0);
  }
  Ast *left = parse_term(p);
  if (left == NULL)
    return NULL;
  if (!at(p, TOK_COMMA) && at(p, TOK_RPAREN))
    return fail(p, "PARSE_ARITY", peek(p)->loc, "%s takes 2 components, found 1", what);
  if (expect(p, TOK_COMMA, "','") == NULL)
    return NULL;
  Ast *right = parse_term(p);
  if (right == NULL)
    return NULL;
  if (at(p, TOK_COMMA))
    return fail(p, "PARSE_ARITY", peek(p)->loc, "%s takes 2 components, found more", what);
  if (expect_close(p, open->loc) == NULL)
    return NULL;
  return make_pair(p, two, keyword->loc, left, right);
}

/* 'Type' NUM */
static Ast *parse_universe(Parser *p) {
  const Token *keyword = advance(p);
  const Token *level = expect(p, TOK_NUMBER, "a universe level");
  if (level == NULL)
    return NULL;
  if (level->value > 1)
    return fail(p, "PARSE_ARITY", level->loc, "a universe is Type 0 or Type 1, found Type %llu",
                level->value);
  Ast *ast = make(p, AST_TYPE, keyword->loc, 0);
  if (ast != NULL)
    ast->u.level = (unsigned)level->value;
  return ast;
}

/* 'inj' NUM 'of' '2' postfix */
static Ast *parse_inj(Parser *p) {
  const Token *keyword = advance(p);
  const Token *tag = expect(p, TOK_NUMBER, "the tag of 'inj'");
  if (tag == NULL || expect(p, TOK_OF, "'of'") == NULL)
    return NULL;
  if (expect_number(p, 2, "a sum has 2 summands") == NULL)
    return NULL;
  if (tag->value > 1)
    return fail(p, "PARSE_ARITY", tag->loc, "the tag of 'inj' is 0 or 1, found %llu", tag->value);
  Ast *value = parse_postfix(p);
  if (value == NULL)
    return NULL;
  Ast *ast = make(p, AST_INJ, keyword->loc, value->depth);
  if (ast == NULL)
    return NULL;
  ast->u.inj.tag = (unsigned)tag->value;
  ast->u.inj.arity = 2;
  ast->u.inj.value = value;
  return ast;
}

static Ast *parse_app(Parser *p) {
  if (at(p, TOK_TYPE))
    return parse_universe(p);
  if (at(p, TOK_INJ))
    return parse_inj(p);
  if (at(p, TOK_TUPLE))
    return parse_components(p, AST_TUPLE0, AST_TUPLE, "tuple");
  if (at(p, TOK_PROD))
    return parse_components(p, AST_PROD0, AST_PROD, "prod");
  if (at(p, TOK_SUM))
    return parse_components(p, AST_SUM, AST_SUM, "sum");
  Ast *ast = parse_postfix(p);
  while (ast != NULL && atom_start(p)) {
    Ast *arg = parse_postfix(p);
    if (arg == NULL)
      return NULL;
    Ast *app = make(p, AST_APP, ast->loc, deeper(ast->depth, arg));
    if (app == NULL)
      return NULL;
    app->u.app.fun = ast;
    app->u.app.arg = arg;
    ast = app;
  }
  return ast;
}

/* 'fun' binder+ '=>' term, as one AST_LAM per binder. */
static Ast *parse_fun(Parser *p) {
  advance(p);
  Binder *binders = NULL;
  size_t count = 0;
  size_t cap = 0;
  if (!at(p, TOK_LPAREN))
    return fail_found(p, "a binder after 'fun'");
  while (at(p, TOK_LPAREN)) {
    binders = grow(p, binders, count, &cap, sizeof *binders);
    if (binders == NULL || !parse_binder(p, &binders[count]))
      return NULL;
    count++;
  }
  if (expect(p, TOK_FATARROW, "'=>'") == NULL)
    return NULL;
  Ast *body = parse_term(p);
  while (body != NULL && count > 0) {
    count--;
    body = make_bind(p, AST_LAM, binders[count], body);
  }
  return body;
}

/* '|' TAG binder '=>' term */
static int parse_case_arm(Parser *p, unsigned tag, CaseArm *arm) {
  if (expect(p, TOK_BAR, "'|' of a case arm") == NULL)
    return 0;
  if (expect_number(p, tag, tag == 0 ? "the first case arm is tagged 0" : "the second case arm is tagged 1") == NULL)
    return 0;
  if (!parse_binder(p, &arm->binder) || expect(p, TOK_FATARROW, "'=>'") == NULL)
    return 0;
  arm->body = parse_term(p);
  return arm->body != NULL;
}

static Ast *parse_case(Parser *p) {
  const Token *keyword = advance(p);
  Ast *subject = parse_term(p);
  CaseArm arms[2];
  if (subject == NULL || expect(p, TOK_WITH, "'with'") == NULL)
    return NULL;
  if (!parse_case_arm(p, 0, &arms[0]) || !parse_case_arm(p, 1, &arms[1]))
    return NULL;
  /* '| 0' and '| 1' can be arms of an enclosing case; '| 2' and up cannot. */
  if (at(p, TOK_BAR) && peek_at(p, 1)->kind == TOK_NUMBER && peek_at(p, 1)->value > 1)
    return fail(p, "PARSE_ARITY", peek(p)->loc, "a case has 2 arms, found more");
  int below = deeper(deeper(deeper(0, subject), arms[0].body), arms[1].body);
  below = deeper(deeper(below, arms[0].binder.type), arms[1].binder.type);
  Ast *ast = make(p, AST_CASE, keyword->loc, below);
  if (ast == NULL)
    return NULL;
  ast->u.cases.subject = subject;
  ast->u.cases.arms[0] = arms[0];
  ast->u.cases.arms[1] = arms[1];
  return ast;
}

/* '|' NAME (['0'] NAME)* '=>' term */
static int parse_match_arm(Parser *p, MatchArm *arm) {
  advance(p);
  arm->loc = peek(p)->loc;
  arm->ctor = expect_name(p, "a constructor");
  if (arm->ctor == NULL)
    return 0;
  size_t cap = 0;
  for (;;) {
    int erased = at(p, TOK_NUMBER) && peek(p)->value == 0 && peek_at(p, 1)->kind == TOK_NAME;
    if (!erased && !at(p, TOK_NAME))
      break;
    arm->vars = grow(p, arm->vars, arm->nvars, &cap, sizeof *arm->vars);
    if (arm->vars == NULL)
      return 0;
    p->pos += erased ? 1 : 0;
    Pattern *var = &arm->vars[arm->nvars];
    var->erased = erased;
    var->loc = peek(p)->loc;
    var->name = name_of(p, advance(p));
    if (var->name == NULL)
      return 0;
    arm->nvars++;
  }
  if (expect(p, TOK_FATARROW, "'=>'") == NULL)
    return 0;
  arm->body = parse_term(p);
  return arm->body != NULL;
}

static Ast *parse_match(Parser *p) {
  const Token *keyword = advance(p);
  Ast *subject = parse_term(p);
  if (subject == NULL || expect(p, TOK_AS, "'as'") == NULL)
    return NULL;
  const char *self = expect_name(p, "the name after 'as'");
  if (self == NULL || expect(p, TOK_IN, "'in'") == NULL)
    return NULL;
  const char *family = expect_name(p, "the family after 'in'");
  const char **indices = NULL;
  size_t nindices = 0;
  size_t cap = 0;
  while (family != NULL && at(p, TOK_NAME)) {
    indices = grow(p, indices, nindices, &cap, sizeof *indices);
    if (indices == NULL)
      return NULL;
    indices[nindices] = name_of(p, advance(p));
    if (indices[nindices] == NULL)
      return NULL;
    nindices++;
  }
  if (family == NULL || expect(p, TOK_RETURN, "'return'") == NULL)
    return NULL;
  Ast *motive = parse_term(p);
  if (motive == NULL || expect(p, TOK_WITH, "'with'") == NULL)
    return NULL;
  if (!at(p, TOK_BAR))
    return fail_found(p, "'|' of a match arm");
  MatchArm *arms = NULL;
  size_t narms = 0;
  cap = 0;
  int below = deeper(deeper(0, subject), motive);
  /* The first arm is required. A later '|' NUMBER starts an enclosing case arm. */
  do {
    arms = grow(p, arms, narms, &cap, sizeof *arms);
    if (arms == NULL)
      return NULL;
    memset(&arms[narms], 0, sizeof arms[narms]);
    if (!parse_match_arm(p, &arms[narms]))
      return NULL;
    below = deeper(below, arms[narms].body);
    narms++;
  } while (at(p, TOK_BAR) && peek_at(p, 1)->kind != TOK_NUMBER);
  Ast *ast = make(p, AST_MATCH, keyword->loc, below);
  if (ast == NULL)
    return NULL;
  ast->u.match.subject = subject;
  ast->u.match.self = self;
  ast->u.match.family = family;
  ast->u.match.indices = indices;
  ast->u.match.nindices = nindices;
  ast->u.match.motive = motive;
  ast->u.match.arms = arms;
  ast->u.match.narms = narms;
  return ast;
}

/* binder ('->' | '*') term */
static Ast *parse_quantifier(Parser *p) {
  Binder binder;
  if (!parse_binder(p, &binder))
    return NULL;
  if (!at(p, TOK_ARROW) && !at(p, TOK_STAR))
    return fail_found(p, "'->' or '*' after a binder");
  AstKind kind = at(p, TOK_ARROW) ? AST_PI : AST_SIGMA;
  advance(p);
  Ast *body = parse_term(p);
  return body == NULL ? NULL : make_bind(p, kind, binder, body);
}

/* app ['->' term] */
static Ast *parse_arrow(Parser *p) {
  Ast *domain = parse_app(p);
  if (domain == NULL || !at(p, TOK_ARROW))
    return domain;
  advance(p);
  Ast *codomain = parse_term(p);
  if (codomain == NULL)
    return NULL;
  Binder binder = { NULL, 0, domain, domain->loc };
  return make_bind(p, AST_PI, binder, codomain);
}

static Ast *parse_term_body(Parser *p) {
  if (at(p, TOK_FUN))
    return parse_fun(p);
  if (at(p, TOK_CASE))
    return parse_case(p);
  if (at(p, TOK_MATCH))
    return parse_match(p);
  if (binder_start(p))
    return parse_quantifier(p);
  return parse_arrow(p);
}

static Ast *parse_term(Parser *p) {
  if (p->depth >= LANG_DEPTH_MAX)
    return fail(p, "PARSE_DEPTH", peek(p)->loc, "the term nests deeper than %d", LANG_DEPTH_MAX);
  p->depth++;
  Ast *ast = parse_term_body(p);
  p->depth--;
  return ast;
}

/* ---- declarations ---- */

/* '|' NAME binder* ':' term, with the binders as a Pi chain. */
static int parse_ctor(Parser *p, Ctor *ctor) {
  advance(p);
  ctor->loc = peek(p)->loc;
  ctor->name = expect_name(p, "a constructor name");
  if (ctor->name == NULL)
    return 0;
  Binder *binders = NULL;
  size_t count = 0;
  size_t cap = 0;
  while (at(p, TOK_LPAREN)) {
    binders = grow(p, binders, count, &cap, sizeof *binders);
    if (binders == NULL || !parse_binder(p, &binders[count]))
      return 0;
    count++;
  }
  if (expect(p, TOK_COLON, "':'") == NULL)
    return 0;
  Ast *type = parse_term(p);
  while (type != NULL && count > 0) {
    count--;
    type = make_bind(p, AST_PI, binders[count], type);
  }
  ctor->type = type;
  return type != NULL;
}

static int parse_decl(Parser *p, Decl *decl) {
  const Token *keyword = advance(p);
  p->def = span_of("-");
  int rec = keyword->kind == TOK_DEF && at(p, TOK_REC);
  p->pos += rec ? 1 : 0;
  decl->kind = keyword->kind == TOK_MU ? DECL_MU : rec ? DECL_REC : DECL_DEF;
  decl->loc = peek(p)->loc;
  const Token *name = expect(p, TOK_NAME, "a declaration name");
  if (name == NULL)
    return 0;
  p->def = name->text;
  decl->name = name_of(p, name);
  if (decl->name == NULL || expect(p, TOK_COLON, "':'") == NULL)
    return 0;
  decl->type = parse_term(p);
  if (decl->type == NULL || expect(p, TOK_DEFINE, "':='") == NULL)
    return 0;
  if (decl->kind != DECL_MU) {
    decl->body = parse_term(p);
    return decl->body != NULL;
  }
  if (!at(p, TOK_BAR))
    return fail_found(p, "'|' of a constructor") != NULL;
  size_t cap = 0;
  while (at(p, TOK_BAR)) {
    decl->ctors = grow(p, decl->ctors, decl->nctors, &cap, sizeof *decl->ctors);
    if (decl->ctors == NULL || !parse_ctor(p, &decl->ctors[decl->nctors]))
      return 0;
    decl->nctors++;
  }
  return 1;
}

int lang_parse(Arena *arena, const char *file, const char *text, size_t size, Program *program,
                 Diag *diag) {
  Token *tokens = NULL;
  size_t count = 0;
  int lexed = lang_lex(arena, file, text, size, &tokens, &count, diag);
  if (lexed != LANG_EXIT_OK)
    return lexed;
  Parser p = { arena, file, tokens, count, 0, 0, span_of("-"), diag };
  program->file = file;
  program->decls = NULL;
  program->ndecls = 0;
  size_t cap = 0;
  while (!at(&p, TOK_EOF)) {
    if (!at(&p, TOK_DEF) && !at(&p, TOK_MU)) {
      fail_top(&p);
      return LANG_EXIT_REFUSED;
    }
    program->decls = grow(&p, program->decls, program->ndecls, &cap, sizeof *program->decls);
    if (program->decls == NULL)
      return LANG_EXIT_REFUSED;
    memset(&program->decls[program->ndecls], 0, sizeof program->decls[program->ndecls]);
    if (!parse_decl(&p, &program->decls[program->ndecls]))
      return LANG_EXIT_REFUSED;
    program->ndecls++;
  }
  return LANG_EXIT_OK;
}
