/* The printer of langc: the AST to the canonical form (syntax.h). */
#include "syntax.h"

/* Where a term is printed, from the loosest place to the tightest. */
typedef enum {
  AT_TERM,  /* anything */
  AT_ARM,   /* a term before '|': no case or match at its right end */
  AT_APP,   /* an application: the domain of an arrow, the body of a Sigma */
  AT_HEAD,  /* the function of an application */
  AT_ARG    /* an argument, the term of a projection, the value of inj */
} Place;

static void print_at(FILE *out, const Ast *ast, Place place);

/* A case or a match is at the right end of AST: its last arm would take a following '|'. */
static int ends_open(const Ast *ast) {
  switch (ast->kind) {
  case AST_CASE:
  case AST_MATCH:
    return 1;
  case AST_PI:
  case AST_SIGMA:
  case AST_LAM:
    return ends_open(ast->u.bind.body);
  case AST_VAR:
  case AST_NAT:
  case AST_TYPE:
  case AST_APP:
  case AST_PAIR:
  case AST_TUPLE0:
  case AST_TUPLE:
  case AST_PROD0:
  case AST_PROD:
  case AST_SUM:
  case AST_INJ:
  case AST_PROJ:
    return 0;
  }
  return 0;
}

/* The tightest place at which AST prints without parentheses. */
static Place tightest(const Ast *ast) {
  switch (ast->kind) {
  case AST_VAR:
  case AST_NAT:
  case AST_PAIR:
  case AST_PROJ:
    return AT_ARG;
  case AST_APP:
    return AT_HEAD;
  case AST_TYPE:
  case AST_TUPLE0:
  case AST_TUPLE:
  case AST_PROD0:
  case AST_PROD:
  case AST_SUM:
  case AST_INJ:
    return AT_APP;
  case AST_PI:
  case AST_SIGMA:
  case AST_LAM:
  case AST_CASE:
  case AST_MATCH:
    return ends_open(ast) ? AT_TERM : AT_ARM;
  }
  return AT_TERM;
}

static void print_binder(FILE *out, const Binder *binder) {
  fprintf(out, "(%s%s : ", binder->erased ? "0 " : "", binder->name);
  print_at(out, binder->type, AT_TERM);
  fputc(')', out);
}

static void print_components(FILE *out, const char *keyword, const Ast *ast) {
  fprintf(out, "%s (", keyword);
  print_at(out, ast->u.pair.left, AT_TERM);
  fputs(", ", out);
  print_at(out, ast->u.pair.right, AT_TERM);
  fputc(')', out);
}

static void print_lam(FILE *out, const Ast *ast) {
  fputs("fun", out);
  while (ast->kind == AST_LAM) {
    fputc(' ', out);
    print_binder(out, &ast->u.bind.binder);
    ast = ast->u.bind.body;
  }
  fputs(" => ", out);
  print_at(out, ast, AT_TERM);
}

static void print_quantifier(FILE *out, const Ast *ast, const char *symbol, Place body) {
  const Binder *binder = &ast->u.bind.binder;
  if (binder->name == NULL)
    print_at(out, binder->type, AT_APP);
  if (binder->name != NULL)
    print_binder(out, binder);
  fprintf(out, " %s ", symbol);
  print_at(out, ast->u.bind.body, body);
}

static void print_case(FILE *out, const Ast *ast) {
  fputs("case ", out);
  print_at(out, ast->u.cases.subject, AT_TERM);
  fputs(" with", out);
  for (unsigned tag = 0; tag < 2; tag++) {
    fprintf(out, " | %u ", tag);
    print_binder(out, &ast->u.cases.arms[tag].binder);
    fputs(" => ", out);
    print_at(out, ast->u.cases.arms[tag].body, AT_ARM);
  }
}

static void print_match(FILE *out, const Ast *ast) {
  fputs("match ", out);
  print_at(out, ast->u.match.subject, AT_TERM);
  fprintf(out, " as %s in %s", ast->u.match.self, ast->u.match.family);
  for (size_t i = 0; i < ast->u.match.nindices; i++)
    fprintf(out, " %s", ast->u.match.indices[i]);
  fputs(" return ", out);
  print_at(out, ast->u.match.motive, AT_TERM);
  fputs(" with", out);
  for (size_t i = 0; i < ast->u.match.narms; i++) {
    const MatchArm *arm = &ast->u.match.arms[i];
    fprintf(out, " | %s", arm->ctor);
    for (size_t j = 0; j < arm->nvars; j++)
      fprintf(out, " %s%s", arm->vars[j].erased ? "0 " : "", arm->vars[j].name);
    fputs(" => ", out);
    print_at(out, arm->body, AT_ARM);
  }
}

static void print_bare(FILE *out, const Ast *ast) {
  switch (ast->kind) {
  case AST_VAR:
    fputs(ast->u.name, out);
    return;
  case AST_NAT:
    fprintf(out, "%llu", ast->u.nat);
    return;
  case AST_TYPE:
    fprintf(out, "Type %u", ast->u.level);
    return;
  case AST_PI:
    print_quantifier(out, ast, "->", AT_TERM);
    return;
  case AST_SIGMA:
    print_quantifier(out, ast, "*", AT_APP);
    return;
  case AST_LAM:
    print_lam(out, ast);
    return;
  case AST_APP:
    print_at(out, ast->u.app.fun, AT_HEAD);
    fputc(' ', out);
    print_at(out, ast->u.app.arg, AT_ARG);
    return;
  case AST_PAIR:
    fputc('(', out);
    print_at(out, ast->u.pair.left, AT_TERM);
    fputs(", ", out);
    print_at(out, ast->u.pair.right, AT_TERM);
    fputc(')', out);
    return;
  case AST_TUPLE0:
    fputs("tuple ()", out);
    return;
  case AST_TUPLE:
    print_components(out, "tuple", ast);
    return;
  case AST_PROD0:
    fputs("prod ()", out);
    return;
  case AST_PROD:
    print_components(out, "prod", ast);
    return;
  case AST_SUM:
    print_components(out, "sum", ast);
    return;
  case AST_INJ:
    fprintf(out, "inj %u of %u ", ast->u.inj.tag, ast->u.inj.arity);
    print_at(out, ast->u.inj.value, AT_ARG);
    return;
  case AST_CASE:
    print_case(out, ast);
    return;
  case AST_MATCH:
    print_match(out, ast);
    return;
  case AST_PROJ:
    print_at(out, ast->u.proj.term, AT_ARG);
    fprintf(out, ".%u", ast->u.proj.index);
    return;
  }
}

static void print_at(FILE *out, const Ast *ast, Place place) {
  int parens = place > tightest(ast);
  if (parens)
    fputc('(', out);
  print_bare(out, ast);
  if (parens)
    fputc(')', out);
}

void lang_print_term(FILE *out, const Ast *ast) { print_at(out, ast, AT_TERM); }

static void print_decl(FILE *out, const Decl *decl) {
  const char *keyword = decl->kind == DECL_MU ? "mu" : decl->kind == DECL_REC ? "def rec" : "def";
  fprintf(out, "%s %s : ", keyword, decl->name);
  print_at(out, decl->type, AT_TERM);
  fputs(" :=", out);
  if (decl->kind != DECL_MU) {
    fputc(' ', out);
    print_at(out, decl->body, AT_TERM);
  }
  for (size_t i = 0; i < decl->nctors; i++) {
    fprintf(out, "\n  | %s : ", decl->ctors[i].name);
    print_at(out, decl->ctors[i].type, AT_ARM);
  }
  fputc('\n', out);
}

void lang_print_program(FILE *out, const Program *program) {
  for (size_t i = 0; i < program->ndecls; i++)
    print_decl(out, &program->decls[i]);
}
