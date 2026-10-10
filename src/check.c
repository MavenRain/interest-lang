/* The langc checker (CHUNK3-DESIGN.md): normalization by evaluation over
 * the AST of src/syntax.h. One Bind list is both the environment of
 * evaluation and the typing context; a name resolves to the locals first,
 * then to the globals declared so far. Evaluation is call by value and
 * unfolds every def. A def rec unfolds when it has all its arguments and
 * the decreasing one is a saturated constructor; natAdd, natSub, natMul,
 * natDiv, natMod, natEq and natLt reduce on literals. After the first error every function returns
 * at once. */
#include "check.h"
#include "../domain/data.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

enum {
  CHECK_FUEL = 1 << 24,  /* evaluation steps of one declaration, tally or ballot vector */
  CHECK_DEPTH = 4096,    /* nested eval, apply, conv, quote, check and infer calls; tcc on an 8 MB stack crashed between 12288 and 16384 (10-07) */
  VERDICT_VECTORS = 59049, /* ballot vectors of verdicts: 3^10 */
  TABLE_MAX = 1000,      /* members of table */
  TALLY_MAX = 501501,    /* tallies of table: C(1002, 2), k = 3 at TABLE_MAX */
  SINK_BYTES = 8192      /* the allocations after a MEMORY error land here */
};

typedef struct Value Value;
typedef struct Bind Bind;
typedef struct Frame Frame;
typedef struct Global Global;
typedef struct Names Names;

typedef enum {
  V_TYPE, V_NAT_TYPE, V_NAT, V_PI, V_SIGMA, V_LAM, V_PAIR, V_TUPLE, V_UNIT,
  V_PROD0, V_PROD, V_SUM, V_INJ, V_DATA, V_CON, V_NEU
} ValueKind;

typedef enum { G_DEF, G_REC, G_MU, G_CTOR, G_PRIM } GlobalKind;
typedef enum { P_ADD, P_SUB, P_MUL, P_DIV, P_MOD, P_EQ, P_LT } Prim;
typedef enum { F_APP, F_PROJ, F_CASE, F_MATCH } FrameKind;

struct Bind {
  const char *name;  /* NULL for the binder of A -> B */
  Value *value;
  Value *type;       /* NULL in an environment of evaluation only */
  int erased;
  Bind *next;
};

struct Frame {
  FrameKind kind;
  Value *arg;      /* F_APP */
  unsigned index;  /* F_PROJ */
  Bind *env;       /* F_CASE, F_MATCH */
  const Ast *ast;  /* F_CASE, F_MATCH: the eliminator */
  Frame *next;     /* the older frames */
};

struct Value {
  ValueKind kind;
  unsigned long long nat;  /* V_TYPE level, V_NAT, V_INJ tag */
  const char *name;        /* V_PI, V_SIGMA binder; V_NEU variable */
  int erased;              /* V_PI, V_SIGMA */
  Value *left;             /* V_PI, V_SIGMA domain; V_PAIR, V_TUPLE, V_PROD, V_SUM; V_INJ value */
  Value *right;            /* V_PAIR, V_TUPLE, V_PROD, V_SUM; V_PI codomain of a builtin arrow */
  Bind *env;               /* V_PI, V_SIGMA, V_LAM closure */
  const Ast *body;         /* V_PI, V_SIGMA codomain (NULL for a builtin arrow); V_LAM node */
  Global *global;          /* V_DATA family, V_CON ctor, V_NEU global head */
  Value **args;            /* V_DATA, V_CON */
  size_t nargs;
  int level;               /* V_NEU variable head; -1 for a global head */
  Frame *spine;            /* V_NEU, newest first */
};

struct Global {
  const char *name;
  GlobalKind kind;
  Prim prim;          /* G_PRIM */
  int prelude;        /* a builtin or a prelude declaration */
  Value *type;
  Value *value;
  const Decl *decl;   /* G_DEF, G_REC, G_MU */
  size_t arity;       /* G_REC lambdas, G_MU indices, G_CTOR fields, G_PRIM 2 */
  size_t decreasing;  /* G_REC: the lambda that the top match takes apart */
  size_t index;       /* G_CTOR: the position in its mu */
  Global *family;     /* G_CTOR */
  Value *body;        /* G_REC: the lambda chain, once checked */
  Global *next;
};

/* The scope of the structural check of a def rec. */
struct Names {
  const char *name;
  int smaller;  /* a pattern variable of the top match */
  Names *next;
};

struct LangChecked {
  Arena *arena;
  Diag *diag;
  int failed;
  long fuel;
  int depth;
  const char *file;
  Span def;
  Loc loc;
  Global *globals;  /* newest first */
  unsigned members;
  LangRegime regime;
  Global *rule;     /* Debreu: G of agg : Aggregation G */
  Global *agg;
  Global *decision; /* D, the codomain of ChoiceRule, once found */
  unsigned decisions;
  Value **ballots;  /* the constructors of D in declaration order */
  const char *stuck;
  Value bad;
  Ast bad_ast;
  union { long double ld; void *p; unsigned long long u; unsigned char bytes[SINK_BYTES]; } sink;
};

typedef LangChecked C;

static Value *eval(C *c, Bind *env, const Ast *t);
static Value *apply(C *c, Value *f, Value *a);
static int conv(C *c, Value *a, Value *b, int lvl);
static Ast *quote(C *c, Value *v, int lvl);
static Value *infer(C *c, Bind *ctx, int lvl, const Ast *t, int relevant);
static void check(C *c, Bind *ctx, int lvl, const Ast *t, Value *want, int relevant);

/* ---- errors, memory, names ---- */

static Value *fail(C *c, const char *code, Loc loc, const char *format, ...) {
  va_list args;
  va_start(args, format);
  diag_vat(c->diag, code, c->def, c->file, loc, format, args);
  va_end(args);
  c->failed = 1;
  return &c->bad;
}

static int refuse(C *c, const char *code, const char *name, const char *text) {
  diag_set(c->diag, code, span_of(name), "%s %s", name, text);
  c->failed = 1;
  return LANG_EXIT_REFUSED;
}

static void *alloc(C *c, size_t size) {
  void *p = c->failed || size > SINK_BYTES ? NULL : arena_alloc(c->arena, size);
  if (p != NULL)
    return p;
  if (!c->failed)
    fail(c, "MEMORY", c->loc, "the arena is full");
  memset(c->sink.bytes, 0, sizeof c->sink.bytes);
  return c->sink.bytes;
}

static int enter(C *c) {
  if (c->failed)
    return 0;
  if (c->depth >= CHECK_DEPTH || c->fuel <= 0) {
    fail(c, "TYPE_FUEL", c->loc, "%s", c->fuel <= 0 ? "evaluation ran out of fuel" : "evaluation nests too deep");
    return 0;
  }
  c->depth++;
  c->fuel--;
  return 1;
}

static int same(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}

static Bind *bind(C *c, Bind *next, const char *name, Value *value, Value *type, int erased) {
  Bind *b = alloc(c, sizeof *b);
  b->name = name;
  b->value = value;
  b->type = type;
  b->erased = erased;
  b->next = next;
  return b;
}

static Bind *find_local(Bind *env, const char *name) {
  while (env != NULL && !same(env->name, name))
    env = env->next;
  return env;
}

static Global *find_global(const C *c, const char *name) {
  Global *g = c->globals;
  while (g != NULL && !same(g->name, name))
    g = g->next;
  return g;
}

static const MatchArm *find_arm(const Ast *t, const char *ctor) {
  for (size_t i = 0; i < t->u.match.narms; i++)
    if (same(t->u.match.arms[i].ctor, ctor))
      return &t->u.match.arms[i];
  return NULL;
}

/* ---- values ---- */

static Value *mk(C *c, ValueKind kind) {
  Value *v = alloc(c, sizeof *v);
  v->kind = kind;
  v->level = -1;
  return v;
}

static Value *mk_nat(C *c, unsigned long long n) {
  Value *v = mk(c, V_NAT);
  v->nat = n;
  return v;
}

static Value *mk_type(C *c, unsigned long long level) {
  Value *v = mk(c, V_TYPE);
  v->nat = level;
  return v;
}

static Value *mk_two(C *c, ValueKind kind, Value *left, Value *right) {
  Value *v = mk(c, kind);
  v->left = left;
  v->right = right;
  return v;
}

static Value *mk_var(C *c, int level, const char *name) {
  Value *v = mk(c, V_NEU);
  v->level = level;
  v->name = name != NULL ? name : "_";
  return v;
}

static Value *mk_global(C *c, ValueKind kind, Global *g) {
  Value *v = mk(c, kind);
  v->global = g;
  return v;
}

static Value *mk_bool(C *c, int truth) {
  Value *v = mk_two(c, V_INJ, mk(c, V_UNIT), NULL);
  v->nat = truth ? 1 : 0;
  return v;
}

static Frame *frame(C *c, FrameKind kind, Bind *env, const Ast *ast) {
  Frame *f = alloc(c, sizeof *f);
  f->kind = kind;
  f->env = env;
  f->ast = ast;
  return f;
}

static Value *push(C *c, Value *neutral, Frame *f) {
  Value *v = mk(c, V_NEU);
  if (c->failed)
    return &c->bad;
  *v = *neutral;
  f->next = neutral->spine;
  v->spine = f;
  return v;
}

/* ---- evaluation ---- */

static Value *instantiate(C *c, Value *pi, Value *a) {
  if (c->failed)
    return &c->bad;
  if (pi->body == NULL)
    return pi->right;
  return eval(c, bind(c, pi->env, pi->name, a, NULL, 0), pi->body);
}

static Value *do_proj(C *c, Value *v, unsigned index) {
  if (c->failed)
    return &c->bad;
  if (v->kind == V_PAIR || v->kind == V_TUPLE)
    return index == 0 ? v->left : v->right;
  if (v->kind != V_NEU)
    return fail(c, "TYPE_INTERNAL", c->loc, "a projection of a value that is not a pair");
  Frame *f = frame(c, F_PROJ, NULL, NULL);
  f->index = index;
  return push(c, v, f);
}

static Value *do_case(C *c, Value *v, Bind *env, const Ast *t) {
  if (c->failed)
    return &c->bad;
  if (v->kind == V_NEU)
    return push(c, v, frame(c, F_CASE, env, t));
  if (v->kind != V_INJ)
    return fail(c, "TYPE_INTERNAL", t->loc, "a case of a value that is not an injection");
  const CaseArm *arm = &t->u.cases.arms[v->nat == 0 ? 0 : 1];
  return eval(c, bind(c, env, arm->binder.name, v->left, NULL, 0), arm->body);
}

static Bind *bind_args(C *c, Bind *env, const MatchArm *arm, Value **args) {
  for (size_t i = 0; i < arm->nvars; i++)
    env = bind(c, env, arm->vars[i].name, args[i], NULL, 0);
  return env;
}

static Value *do_match(C *c, Value *v, Bind *env, const Ast *t) {
  if (c->failed)
    return &c->bad;
  if (v->kind == V_NEU)
    return push(c, v, frame(c, F_MATCH, env, t));
  const MatchArm *arm = v->kind == V_CON ? find_arm(t, v->global->name) : NULL;
  if (arm == NULL || arm->nvars != v->nargs)
    return fail(c, "TYPE_INTERNAL", t->loc, "a match with no arm for its value");
  return eval(c, bind_args(c, env, arm, v->args), arm->body);
}

static Value *closure(C *c, ValueKind kind, Bind *env, const Ast *t) {
  Value *v = mk(c, kind);
  v->name = t->u.bind.binder.name;
  v->erased = t->u.bind.binder.erased;
  v->left = eval(c, env, t->u.bind.binder.type);
  v->env = env;
  v->body = t->u.bind.body;
  return v;
}

static Value *eval_var(C *c, Bind *env, const Ast *t) {
  Bind *b = find_local(env, t->u.name);
  Global *g = b == NULL ? find_global(c, t->u.name) : NULL;
  if (b != NULL)
    return b->value;
  if (g == NULL || g->value == NULL)
    return fail(c, "TYPE_SCOPE", t->loc, "%s is not in scope", t->u.name);
  return g->value;
}

static Value *eval_body(C *c, Bind *env, const Ast *t) {
  switch (t->kind) {
  case AST_VAR: return eval_var(c, env, t);
  case AST_NAT: return mk_nat(c, t->u.nat);
  case AST_TYPE: return mk_type(c, t->u.level);
  case AST_PI: return closure(c, V_PI, env, t);
  case AST_SIGMA: return closure(c, V_SIGMA, env, t);
  case AST_LAM: {
    Value *v = mk(c, V_LAM);
    v->env = env;
    v->body = t;
    return v;
  }
  case AST_APP: return apply(c, eval(c, env, t->u.app.fun), eval(c, env, t->u.app.arg));
  case AST_PAIR: return mk_two(c, V_PAIR, eval(c, env, t->u.pair.left), eval(c, env, t->u.pair.right));
  case AST_TUPLE0: return mk(c, V_UNIT);
  case AST_TUPLE: return mk_two(c, V_TUPLE, eval(c, env, t->u.pair.left), eval(c, env, t->u.pair.right));
  case AST_PROD0: return mk(c, V_PROD0);
  case AST_PROD: return mk_two(c, V_PROD, eval(c, env, t->u.pair.left), eval(c, env, t->u.pair.right));
  case AST_SUM: return mk_two(c, V_SUM, eval(c, env, t->u.pair.left), eval(c, env, t->u.pair.right));
  case AST_INJ: {
    Value *v = mk_two(c, V_INJ, eval(c, env, t->u.inj.value), NULL);
    v->nat = t->u.inj.tag;
    return v;
  }
  case AST_CASE: return do_case(c, eval(c, env, t->u.cases.subject), env, t);
  case AST_MATCH: return do_match(c, eval(c, env, t->u.match.subject), env, t);
  case AST_PROJ: return do_proj(c, eval(c, env, t->u.proj.term), t->u.proj.index);
  }
  return fail(c, "TYPE_INTERNAL", t->loc, "an unknown term");
}

static Value *eval(C *c, Bind *env, const Ast *t) {
  if (!enter(c))
    return &c->bad;
  Value *v = eval_body(c, env, t);
  c->depth--;
  return v;
}

static Value *prim(C *c, Prim p, Value *a, Value *b, Value *stuck) {
  if (a->kind != V_NAT || b->kind != V_NAT)
    return stuck;
  switch (p) {
  case P_ADD:
    if (a->nat > ULLONG_MAX - b->nat)
      return fail(c, "TYPE_NAT", c->loc, "natAdd %llu %llu overflows", a->nat, b->nat);
    return mk_nat(c, a->nat + b->nat);
  case P_SUB: return mk_nat(c, a->nat > b->nat ? a->nat - b->nat : 0);
  case P_MUL:
    if (b->nat != 0 && a->nat > ULLONG_MAX / b->nat)
      return fail(c, "TYPE_NAT", c->loc, "natMul %llu %llu overflows", a->nat, b->nat);
    return mk_nat(c, a->nat * b->nat);
  /* As the EVM DIV and MOD: a divisor of 0 gives 0. */
  case P_DIV: return mk_nat(c, b->nat == 0 ? 0 : a->nat / b->nat);
  case P_MOD: return mk_nat(c, b->nat == 0 ? 0 : a->nat % b->nat);
  case P_EQ: return mk_bool(c, a->nat == b->nat);
  case P_LT: return mk_bool(c, a->nat < b->nat);
  }
  return stuck;
}

static Value *unfold(C *c, Global *g, Value **args, Value *stuck) {
  Value *d = args[g->decreasing];
  if (g->body == NULL || d->kind != V_CON || d->nargs != d->global->arity)
    return stuck;
  Value *v = g->body;
  for (size_t i = 0; i < g->arity; i++)
    v = apply(c, v, args[i]);
  return v;
}

/* A neutral with a global head reduces once it has all its arguments. */
static Value *reduce(C *c, Value *n) {
  if (c->failed || n->global == NULL)
    return n;
  Global *g = n->global;
  size_t count = 0;
  Frame *f = n->spine;
  for (; f != NULL && f->kind == F_APP; f = f->next)
    count++;
  if (f != NULL || count != g->arity || count == 0)
    return n;
  Value **args = alloc(c, count * sizeof *args);
  if (c->failed)
    return &c->bad;
  size_t i = count;
  for (f = n->spine; f != NULL; f = f->next)
    args[--i] = f->arg;
  switch (g->kind) {
  case G_PRIM: return prim(c, g->prim, args[0], args[1], n);
  case G_REC: return unfold(c, g, args, n);
  case G_DEF:
  case G_MU:
  case G_CTOR: break;
  }
  return n;
}

static Value *extend(C *c, Value *f, Value *a) {
  if (f->nargs >= f->global->arity)
    return fail(c, "TYPE_INTERNAL", c->loc, "%s has too many arguments", f->global->name);
  Value **args = alloc(c, (f->nargs + 1) * sizeof *args);
  Value *v = mk_global(c, f->kind, f->global);
  for (size_t i = 0; i < f->nargs; i++)
    args[i] = f->args[i];
  args[f->nargs] = a;
  v->args = args;
  v->nargs = f->nargs + 1;
  return v;
}

static Value *apply_body(C *c, Value *f, Value *a) {
  switch (f->kind) {
  case V_LAM: {
    const Ast *lam = f->body;
    return eval(c, bind(c, f->env, lam->u.bind.binder.name, a, NULL, 0), lam->u.bind.body);
  }
  case V_DATA:
  case V_CON: return extend(c, f, a);
  case V_NEU: {
    Frame *fr = frame(c, F_APP, NULL, NULL);
    fr->arg = a;
    return reduce(c, push(c, f, fr));
  }
  case V_TYPE: case V_NAT_TYPE: case V_NAT: case V_PI: case V_SIGMA: case V_PAIR: case V_TUPLE:
  case V_UNIT: case V_PROD0: case V_PROD: case V_SUM: case V_INJ: break;
  }
  return fail(c, "TYPE_INTERNAL", c->loc, "an application of a value that is not a function");
}

static Value *apply(C *c, Value *f, Value *a) {
  if (!enter(c))
    return &c->bad;
  Value *v = apply_body(c, f, a);
  c->depth--;
  return v;
}

/* ---- conversion ---- */

static int applicable(const Value *v) {
  int partial = (v->kind == V_DATA || v->kind == V_CON) && v->nargs < v->global->arity;
  return v->kind == V_LAM || v->kind == V_NEU || partial;
}

static int projectable(const Value *v) {
  return v->kind == V_PAIR || v->kind == V_TUPLE || v->kind == V_NEU;
}

static int conv_apply(C *c, Value *a, Value *b, int lvl) {
  const Value *lam = a->kind == V_LAM ? a : b;
  Value *x = mk_var(c, lvl, lam->body->u.bind.binder.name);
  return conv(c, apply(c, a, x), apply(c, b, x), lvl + 1);
}

static int conv_closure(C *c, Value *a, Value *b, int lvl) {
  Value *x = mk_var(c, lvl, a->name);
  return conv(c, instantiate(c, a, x), instantiate(c, b, x), lvl + 1);
}

static int conv_args(C *c, const Value *a, const Value *b, int lvl) {
  size_t i = 0;
  if (a->nargs != b->nargs)
    return 0;
  while (i < a->nargs && conv(c, a->args[i], b->args[i], lvl))
    i++;
  return i == a->nargs;
}

static int conv_case_arm(C *c, const Frame *fa, const Frame *fb, int i, int lvl) {
  const CaseArm *x = &fa->ast->u.cases.arms[i];
  const CaseArm *y = &fb->ast->u.cases.arms[i];
  Value *v = mk_var(c, lvl, x->binder.name);
  return conv(c, eval(c, bind(c, fa->env, x->binder.name, v, NULL, 0), x->body),
              eval(c, bind(c, fb->env, y->binder.name, v, NULL, 0), y->body), lvl + 1);
}

static int conv_match_arm(C *c, const Frame *fa, const Frame *fb, const MatchArm *x, int lvl) {
  const MatchArm *y = find_arm(fb->ast, x->ctor);
  if (y == NULL || y->nvars != x->nvars)
    return 0;
  Value **vars = alloc(c, (x->nvars + 1) * sizeof *vars);
  for (size_t i = 0; i < x->nvars; i++)
    vars[i] = mk_var(c, lvl + (int)i, x->vars[i].name);
  return conv(c, eval(c, bind_args(c, fa->env, x, vars), x->body),
              eval(c, bind_args(c, fb->env, y, vars), y->body), lvl + (int)x->nvars);
}

static int conv_match(C *c, const Frame *fa, const Frame *fb, int lvl) {
  const Ast *s = fa->ast;
  size_t i = 0;
  if (!same(s->u.match.family, fb->ast->u.match.family) || s->u.match.narms != fb->ast->u.match.narms)
    return 0;
  while (i < s->u.match.narms && conv_match_arm(c, fa, fb, &s->u.match.arms[i], lvl))
    i++;
  return i == s->u.match.narms;
}

static int conv_frame(C *c, const Frame *fa, const Frame *fb, int lvl) {
  if (fa->kind != fb->kind)
    return 0;
  switch (fa->kind) {
  case F_APP: return conv(c, fa->arg, fb->arg, lvl);
  case F_PROJ: return fa->index == fb->index;
  case F_CASE: return conv_case_arm(c, fa, fb, 0, lvl) && conv_case_arm(c, fa, fb, 1, lvl);
  case F_MATCH: return conv_match(c, fa, fb, lvl);
  }
  return 0;
}

static int conv_frames(C *c, const Frame *fa, const Frame *fb, int lvl) {
  while (fa != NULL && fb != NULL && conv_frame(c, fa, fb, lvl)) {
    fa = fa->next;
    fb = fb->next;
  }
  return fa == NULL && fb == NULL;
}

static int conv_body(C *c, Value *a, Value *b, int lvl) {
  if (a->kind == V_LAM || b->kind == V_LAM)
    return applicable(a) && applicable(b) && conv_apply(c, a, b, lvl);
  if (a->kind == V_PAIR || b->kind == V_PAIR || a->kind == V_TUPLE || b->kind == V_TUPLE)
    return projectable(a) && projectable(b) && conv(c, do_proj(c, a, 0), do_proj(c, b, 0), lvl) &&
           conv(c, do_proj(c, a, 1), do_proj(c, b, 1), lvl);
  if (a->kind != b->kind)
    return 0;
  switch (a->kind) {
  case V_TYPE:
  case V_NAT: return a->nat == b->nat;
  case V_NAT_TYPE:
  case V_UNIT:
  case V_PROD0: return 1;
  case V_PI:
  case V_SIGMA: return a->erased == b->erased && conv(c, a->left, b->left, lvl) && conv_closure(c, a, b, lvl);
  case V_PROD:
  case V_SUM: return conv(c, a->left, b->left, lvl) && conv(c, a->right, b->right, lvl);
  case V_INJ: return a->nat == b->nat && conv(c, a->left, b->left, lvl);
  case V_DATA:
  case V_CON: return a->global == b->global && conv_args(c, a, b, lvl);
  case V_NEU: return a->level == b->level && a->global == b->global && conv_frames(c, a->spine, b->spine, lvl);
  case V_LAM:
  case V_PAIR:
  case V_TUPLE: return 0;
  }
  return 0;
}

static int conv(C *c, Value *a, Value *b, int lvl) {
  if (a == b)
    return !c->failed;
  if (!enter(c))
    return 0;
  int equal = conv_body(c, a, b, lvl);
  c->depth--;
  return equal && !c->failed;
}

/* ---- quotation ---- */

static Ast *node(C *c, AstKind kind) {
  Ast *t = alloc(c, sizeof *t);
  t->kind = kind;
  t->depth = 1;
  return t;
}

static Ast *var_node(C *c, const char *name) {
  Ast *t = node(c, AST_VAR);
  t->u.name = name != NULL ? name : "_";
  return t;
}

static Ast *app_node(C *c, Ast *fun, Ast *arg) {
  Ast *t = node(c, AST_APP);
  t->u.app.fun = fun;
  t->u.app.arg = arg;
  return t;
}

static Ast *two_node(C *c, AstKind kind, Value *v, int lvl) {
  Ast *t = node(c, kind);
  t->u.pair.left = quote(c, v->left, lvl);
  t->u.pair.right = quote(c, v->right, lvl);
  return t;
}

/* Print variables by their semantic level, never by a closure's source
 * spelling. Each level has a distinct name, also distinct from globals. */
static const char *quote_name(C *c, int lvl) {
  char name[64];
  unsigned suffix = 0;
  do {
    snprintf(name, sizeof name, "langq%dx%u", lvl, suffix++);
  } while (find_global(c, name) != NULL);
  size_t size = strlen(name) + 1;
  char *out = alloc(c, size);
  memcpy(out, name, size);
  return out;
}

static Ast *quote_bind(C *c, Value *v, int lvl) {
  Ast *t = node(c, v->kind == V_PI ? AST_PI : AST_SIGMA);
  t->u.bind.binder.name = v->name == NULL ? NULL : quote_name(c, lvl);
  t->u.bind.binder.erased = v->erased;
  t->u.bind.binder.type = quote(c, v->left, lvl);
  t->u.bind.body = quote(c, instantiate(c, v, mk_var(c, lvl, v->name)), lvl + 1);
  return t;
}

static Ast *quote_lam(C *c, Value *v, int lvl) {
  const Ast *lam = v->body;
  Ast *t = node(c, AST_LAM);
  t->u.bind.binder = lam->u.bind.binder;
  t->u.bind.binder.name = quote_name(c, lvl);
  t->u.bind.binder.type = quote(c, eval(c, v->env, lam->u.bind.binder.type), lvl);
  t->u.bind.body = quote(c, apply(c, v, mk_var(c, lvl, lam->u.bind.binder.name)), lvl + 1);
  return t;
}

static Ast *quote_args(C *c, Value *v, int lvl) {
  Ast *t = var_node(c, v->global->name);
  for (size_t i = 0; i < v->nargs; i++)
    t = app_node(c, t, quote(c, v->args[i], lvl));
  return t;
}

static void quote_case_arm(C *c, CaseArm *arm, Bind *env, int lvl) {
  const char *name = arm->binder.name;
  arm->binder.name = quote_name(c, lvl);
  arm->binder.type = quote(c, eval(c, env, arm->binder.type), lvl);
  arm->body = quote(c, eval(c, bind(c, env, name, mk_var(c, lvl, name), NULL, 0), arm->body), lvl + 1);
}

static Ast *quote_match(C *c, Ast *subject, const Frame *f, int lvl) {
  Ast *t = node(c, AST_MATCH);
  size_t n = f->ast->u.match.narms;
  MatchArm *arms = alloc(c, (n + 1) * sizeof *arms);
  if (c->failed)
    return &c->bad_ast;
  *t = *f->ast;
  t->u.match.subject = subject;
  t->u.match.arms = arms;
  /* The motive is a closure too: instantiate its indices and subject in
   * the captured environment before printing it under fresh binders. */
  size_t ni = t->u.match.nindices;
  const char **indices = alloc(c, (ni + 1) * sizeof *indices);
  Bind *motive = f->env;
  for (size_t i = 0; i < ni; i++) {
    indices[i] = quote_name(c, lvl + (int)i);
    motive = bind(c, motive, f->ast->u.match.indices[i],
                  mk_var(c, lvl + (int)i, indices[i]), NULL, 0);
  }
  t->u.match.indices = indices;
  t->u.match.self = quote_name(c, lvl + (int)ni);
  motive = bind(c, motive, f->ast->u.match.self,
                mk_var(c, lvl + (int)ni, t->u.match.self), NULL, 0);
  t->u.match.motive = quote(c, eval(c, motive, f->ast->u.match.motive), lvl + (int)ni + 1);
  for (size_t i = 0; i < n; i++) {
    const MatchArm *arm = &f->ast->u.match.arms[i];
    Value **vars = alloc(c, (arm->nvars + 1) * sizeof *vars);
    for (size_t k = 0; k < arm->nvars; k++)
      vars[k] = mk_var(c, lvl + (int)k, arm->vars[k].name);
    arms[i] = *arm;
    arms[i].vars = alloc(c, (arm->nvars + 1) * sizeof *arms[i].vars);
    for (size_t k = 0; k < arm->nvars; k++) {
      arms[i].vars[k] = arm->vars[k];
      arms[i].vars[k].name = quote_name(c, lvl + (int)k);
    }
    arms[i].body = quote(c, eval(c, bind_args(c, f->env, arm, vars), arm->body), lvl + (int)arm->nvars);
  }
  return t;
}

static Ast *quote_frame(C *c, Ast *inner, const Frame *f, int lvl) {
  switch (f->kind) {
  case F_APP: return app_node(c, inner, quote(c, f->arg, lvl));
  case F_PROJ: {
    Ast *t = node(c, AST_PROJ);
    t->u.proj.term = inner;
    t->u.proj.index = f->index;
    return t;
  }
  case F_CASE: {
    Ast *t = node(c, AST_CASE);
    if (c->failed)
      return &c->bad_ast;
    *t = *f->ast;
    t->u.cases.subject = inner;
    quote_case_arm(c, &t->u.cases.arms[0], f->env, lvl);
    quote_case_arm(c, &t->u.cases.arms[1], f->env, lvl);
    return t;
  }
  case F_MATCH: return quote_match(c, inner, f, lvl);
  }
  return &c->bad_ast;
}

static Ast *quote_spine(C *c, const Value *v, const Frame *f, int lvl) {
  if (f == NULL)
    return var_node(c, v->global != NULL ? v->global->name : quote_name(c, v->level));
  return quote_frame(c, quote_spine(c, v, f->next, lvl), f, lvl);
}

static Ast *quote_body(C *c, Value *v, int lvl) {
  switch (v->kind) {
  case V_TYPE: {
    Ast *t = node(c, AST_TYPE);
    t->u.level = (unsigned)v->nat;
    return t;
  }
  case V_NAT_TYPE: return var_node(c, "Nat");
  case V_NAT: {
    Ast *t = node(c, AST_NAT);
    t->u.nat = v->nat;
    return t;
  }
  case V_PI:
  case V_SIGMA: return quote_bind(c, v, lvl);
  case V_LAM: return quote_lam(c, v, lvl);
  case V_PAIR: return two_node(c, AST_PAIR, v, lvl);
  case V_TUPLE: return two_node(c, AST_TUPLE, v, lvl);
  case V_UNIT: return node(c, AST_TUPLE0);
  case V_PROD0: return node(c, AST_PROD0);
  case V_PROD: return two_node(c, AST_PROD, v, lvl);
  case V_SUM: return two_node(c, AST_SUM, v, lvl);
  case V_INJ: {
    Ast *t = node(c, AST_INJ);
    t->u.inj.tag = (unsigned)v->nat;
    t->u.inj.arity = 2;
    t->u.inj.value = quote(c, v->left, lvl);
    return t;
  }
  case V_DATA:
  case V_CON: return quote_args(c, v, lvl);
  case V_NEU: return quote_spine(c, v, v->spine, lvl);
  }
  return &c->bad_ast;
}

static Ast *quote(C *c, Value *v, int lvl) {
  if (!enter(c))
    return &c->bad_ast;
  Ast *t = quote_body(c, v, lvl);
  c->depth--;
  return t;
}

/* ---- checking ---- */

static void mismatch(C *c, Loc loc, Value *want, Value *got, int lvl) {
  Ast *expected = quote(c, want, lvl);
  Ast *found = quote(c, got, lvl);
  if (c->failed)
    return;
  fail(c, "TYPE_MISMATCH", loc, "the types differ:");
  c->diag->expected = expected;
  c->diag->found = found;
}

static unsigned long long sort_of(C *c, Bind *ctx, int lvl, const Ast *t) {
  Value *s = infer(c, ctx, lvl, t, 0);
  if (!c->failed && s->kind != V_TYPE)
    fail(c, "TYPE_SHAPE", t->loc, "a type is expected here");
  return c->failed ? 0 : s->nat;
}

static Value *infer_var(C *c, Bind *ctx, const Ast *t, int relevant) {
  Bind *b = find_local(ctx, t->u.name);
  Global *g = find_global(c, t->u.name);
  if (b != NULL && relevant && b->erased)
    return fail(c, "TYPE_ERASED", t->loc, "%s is erased and is used at run time", t->u.name);
  if (b != NULL)
    return b->type;
  if (g == NULL)
    return fail(c, "TYPE_SCOPE", t->loc, "%s is not in scope", t->u.name);
  return g->type;
}

static Value *infer_bind(C *c, Bind *ctx, int lvl, const Ast *t) {
  const Binder *b = &t->u.bind.binder;
  unsigned long long domain = sort_of(c, ctx, lvl, b->type);
  Bind *inner = bind(c, ctx, b->name, mk_var(c, lvl, b->name), eval(c, ctx, b->type), b->erased);
  unsigned long long body = sort_of(c, inner, lvl + 1, t->u.bind.body);
  return mk_type(c, domain > body ? domain : body);
}

static Value *infer_app(C *c, Bind *ctx, int lvl, const Ast *t, int relevant) {
  Value *f = infer(c, ctx, lvl, t->u.app.fun, relevant);
  if (!c->failed && f->kind != V_PI)
    return fail(c, "TYPE_SHAPE", t->loc, "the head of an application is not a function");
  check(c, ctx, lvl, t->u.app.arg, f->left, relevant && !f->erased);
  return instantiate(c, f, eval(c, ctx, t->u.app.arg));
}

static Value *infer_proj(C *c, Bind *ctx, int lvl, const Ast *t, int relevant) {
  Value *ty = infer(c, ctx, lvl, t->u.proj.term, relevant);
  unsigned i = t->u.proj.index;
  if (c->failed)
    return &c->bad;
  if (i > 1 || (ty->kind != V_PROD && ty->kind != V_SIGMA))
    return fail(c, "TYPE_SHAPE", t->loc, "a projection .%u of a term that is not a pair", i);
  if (relevant && ty->kind == V_SIGMA && ty->erased && i == 0)
    return fail(c, "TYPE_ERASED", t->loc, "the first field of this pair is erased and is used at run time");
  if (ty->kind == V_PROD || i == 0)
    return i == 0 ? ty->left : ty->right;
  return instantiate(c, ty, do_proj(c, eval(c, ctx, t->u.proj.term), 0));
}

static Value *motive_at(C *c, Bind *env, const Ast *t, Value **indices, Value *self) {
  if (c->failed)
    return &c->bad;
  for (size_t i = 0; i < t->u.match.nindices; i++)
    env = bind(c, env, t->u.match.indices[i], indices[i], NULL, 0);
  return eval(c, bind(c, env, t->u.match.self, self, NULL, 0), t->u.match.motive);
}

static void check_motive(C *c, Bind *ctx, int lvl, const Ast *t, Global *fam) {
  Value *ty = fam->type;
  Value **indices = alloc(c, (fam->arity + 1) * sizeof *indices);
  for (size_t i = 0; i < fam->arity; i++) {
    const char *name = t->u.match.indices[i];
    indices[i] = mk_var(c, lvl, name);
    ctx = bind(c, ctx, name, indices[i], ty->left, 1);
    ty = instantiate(c, ty, indices[i]);
    lvl++;
  }
  Value *self = mk_global(c, V_DATA, fam);
  self->args = indices;
  self->nargs = fam->arity;
  ctx = bind(c, ctx, t->u.match.self, mk_var(c, lvl, t->u.match.self), self, 1);
  sort_of(c, ctx, lvl + 1, t->u.match.motive);
}

static void check_match_arm(C *c, Bind *ctx, int lvl, const Ast *t, const MatchArm *arm, Global *fam,
                            int relevant) {
  Global *ctor = find_global(c, arm->ctor);
  if (ctor == NULL || ctor->kind != G_CTOR || ctor->family != fam || find_arm(t, arm->ctor) != arm ||
      arm->nvars != ctor->arity) {
    fail(c, "TYPE_MATCH", arm->loc, "the arm %s is not one constructor of %s with its fields", arm->ctor,
         fam->name);
    return;
  }
  Value *ty = ctor->type;
  Value **args = alloc(c, (arm->nvars + 1) * sizeof *args);
  Bind *inner = ctx;
  int l = lvl;
  for (size_t k = 0; k < arm->nvars; k++) {
    const Pattern *p = &arm->vars[k];
    args[k] = mk_var(c, l++, p->name);
    inner = bind(c, inner, p->name, args[k], ty->left, p->erased || ty->erased);
    ty = instantiate(c, ty, args[k]);
  }
  if (c->failed)
    return;
  if (ty->kind != V_DATA || ty->nargs != fam->arity) {
    fail(c, "TYPE_INTERNAL", arm->loc, "%s does not end in %s", arm->ctor, fam->name);
    return;
  }
  Value *con = mk_global(c, V_CON, ctor);
  con->args = args;
  con->nargs = arm->nvars;
  check(c, inner, l, arm->body, motive_at(c, ctx, t, ty->args, con), relevant);
}

static Value *infer_match(C *c, Bind *ctx, int lvl, const Ast *t, int relevant) {
  Value *st = infer(c, ctx, lvl, t->u.match.subject, relevant);
  Global *fam = find_global(c, t->u.match.family);
  if (c->failed)
    return &c->bad;
  if (fam == NULL || fam->kind != G_MU || st->kind != V_DATA || st->global != fam ||
      t->u.match.nindices != fam->arity || st->nargs != fam->arity)
    return fail(c, "TYPE_MATCH", t->loc, "the subject is not of the family %s with %zu indices",
                t->u.match.family, t->u.match.nindices);
  if (t->u.match.narms != fam->decl->nctors)
    return fail(c, "TYPE_MATCH", t->loc, "the match has %zu arms and %s has %zu constructors",
                t->u.match.narms, fam->name, fam->decl->nctors);
  check_motive(c, ctx, lvl, t, fam);
  for (size_t i = 0; i < t->u.match.narms; i++)
    check_match_arm(c, ctx, lvl, t, &t->u.match.arms[i], fam, relevant);
  return motive_at(c, ctx, t, st->args, eval(c, ctx, t->u.match.subject));
}

static Value *infer_body(C *c, Bind *ctx, int lvl, const Ast *t, int relevant) {
  switch (t->kind) {
  case AST_VAR: return infer_var(c, ctx, t, relevant);
  case AST_NAT: return mk(c, V_NAT_TYPE);
  case AST_TYPE:
    return t->u.level == 0 ? mk_type(c, 1) : fail(c, "TYPE_UNIVERSE", t->loc, "Type 1 has no type");
  case AST_PI:
  case AST_SIGMA: return infer_bind(c, ctx, lvl, t);
  case AST_APP: return infer_app(c, ctx, lvl, t, relevant);
  case AST_TUPLE0: return mk(c, V_PROD0);
  case AST_TUPLE:
    return mk_two(c, V_PROD, infer(c, ctx, lvl, t->u.pair.left, relevant),
                  infer(c, ctx, lvl, t->u.pair.right, relevant));
  case AST_PROD0: return mk_type(c, 0);
  case AST_PROD:
  case AST_SUM: {
    unsigned long long a = sort_of(c, ctx, lvl, t->u.pair.left);
    unsigned long long b = sort_of(c, ctx, lvl, t->u.pair.right);
    return mk_type(c, a > b ? a : b);
  }
  case AST_MATCH: return infer_match(c, ctx, lvl, t, relevant);
  case AST_PROJ: return infer_proj(c, ctx, lvl, t, relevant);
  case AST_LAM:
  case AST_PAIR:
  case AST_INJ:
  case AST_CASE: break;
  }
  return fail(c, "TYPE_INFER", t->loc, "this term needs its type from the context");
}

static Value *infer(C *c, Bind *ctx, int lvl, const Ast *t, int relevant) {
  if (!enter(c))
    return &c->bad;
  Value *v = infer_body(c, ctx, lvl, t, relevant);
  c->depth--;
  return v;
}

/* The binder type of a fun or of a case arm must be the expected domain. */
static Value *binder_domain(C *c, Bind *ctx, int lvl, const Binder *b, Value *want) {
  sort_of(c, ctx, lvl, b->type);
  Value *domain = eval(c, ctx, b->type);
  if (!c->failed && !conv(c, domain, want, lvl))
    mismatch(c, b->loc, want, domain, lvl);
  return want;
}

static void check_lam(C *c, Bind *ctx, int lvl, const Ast *t, Value *want, int relevant) {
  const Binder *b = &t->u.bind.binder;
  if (want->kind != V_PI) {
    fail(c, "TYPE_SHAPE", t->loc, "a fun needs a function type");
    return;
  }
  if (b->erased != want->erased) {
    fail(c, "TYPE_ERASED", b->loc, "the erasure of %s differs from its type", b->name);
    return;
  }
  Value *domain = binder_domain(c, ctx, lvl, b, want->left);
  Value *x = mk_var(c, lvl, b->name);
  check(c, bind(c, ctx, b->name, x, domain, b->erased), lvl + 1, t->u.bind.body, instantiate(c, want, x),
        relevant);
}

static void check_case_arm(C *c, Bind *ctx, int lvl, const CaseArm *arm, Value *leg, Value *want, int relevant) {
  const Binder *b = &arm->binder;
  Value *domain = binder_domain(c, ctx, lvl, b, leg);
  check(c, bind(c, ctx, b->name, mk_var(c, lvl, b->name), domain, b->erased), lvl + 1, arm->body, want,
        relevant);
}

static int subtype(C *c, Value *got, Value *want, int lvl) {
  if (got->kind == V_TYPE && want->kind == V_TYPE)
    return got->nat <= want->nat;
  return conv(c, got, want, lvl);
}

static void check_body(C *c, Bind *ctx, int lvl, const Ast *t, Value *want, int relevant) {
  switch (t->kind) {
  case AST_LAM:
    check_lam(c, ctx, lvl, t, want, relevant);
    return;
  case AST_PAIR:
    if (want->kind != V_SIGMA) {
      fail(c, "TYPE_SHAPE", t->loc, "a pair needs a Sigma type");
      return;
    }
    check(c, ctx, lvl, t->u.pair.left, want->left, relevant && !want->erased);
    check(c, ctx, lvl, t->u.pair.right, instantiate(c, want, eval(c, ctx, t->u.pair.left)), relevant);
    return;
  case AST_TUPLE:
    if (want->kind != V_PROD) {
      fail(c, "TYPE_SHAPE", t->loc, "a tuple needs a prod type");
      return;
    }
    check(c, ctx, lvl, t->u.pair.left, want->left, relevant);
    check(c, ctx, lvl, t->u.pair.right, want->right, relevant);
    return;
  case AST_INJ:
    if (want->kind != V_SUM || t->u.inj.arity != 2) {
      fail(c, "TYPE_SHAPE", t->loc, "an inj needs a sum type of 2 legs");
      return;
    }
    check(c, ctx, lvl, t->u.inj.value, t->u.inj.tag == 0 ? want->left : want->right, relevant);
    return;
  case AST_CASE: {
    Value *s = infer(c, ctx, lvl, t->u.cases.subject, relevant);
    if (!c->failed && s->kind != V_SUM) {
      fail(c, "TYPE_SHAPE", t->loc, "a case needs a subject of a sum type");
      return;
    }
    check_case_arm(c, ctx, lvl, &t->u.cases.arms[0], s->left, want, relevant);
    check_case_arm(c, ctx, lvl, &t->u.cases.arms[1], s->right, want, relevant);
    return;
  }
  case AST_VAR: case AST_NAT: case AST_TYPE: case AST_PI: case AST_SIGMA: case AST_APP: case AST_TUPLE0:
  case AST_PROD0: case AST_PROD: case AST_SUM: case AST_MATCH: case AST_PROJ: break;
  }
  Value *got = infer(c, ctx, lvl, t, relevant);
  if (!c->failed && !subtype(c, got, want, lvl))
    mismatch(c, t->loc, want, got, lvl);
}

static void check(C *c, Bind *ctx, int lvl, const Ast *t, Value *want, int relevant) {
  if (!enter(c))
    return;
  check_body(c, ctx, lvl, t, want, relevant);
  c->depth--;
}

/* ---- the structural check of a def rec ---- */

static Names *push_name(C *c, Names *next, const char *name, int smaller) {
  Names *n = alloc(c, sizeof *n);
  n->name = name;
  n->smaller = smaller;
  n->next = next;
  return n;
}

static const Names *lookup(const Names *scope, const char *name) {
  while (scope != NULL && !same(scope->name, name))
    scope = scope->next;
  return scope;
}

static void walk(C *c, const Global *g, const Ast *t, Names *scope);

static void walk_arm(C *c, const Global *g, const MatchArm *arm, Names *scope, int smaller) {
  for (size_t i = 0; i < arm->nvars; i++)
    scope = push_name(c, scope, arm->vars[i].name, smaller);
  walk(c, g, arm->body, scope);
}

static void walk_match(C *c, const Global *g, const Ast *t, Names *scope, int smaller) {
  Names *motive = push_name(c, scope, t->u.match.self, 0);
  for (size_t i = 0; i < t->u.match.nindices; i++)
    motive = push_name(c, motive, t->u.match.indices[i], 0);
  walk(c, g, t->u.match.subject, scope);
  walk(c, g, t->u.match.motive, motive);
  for (size_t i = 0; i < t->u.match.narms; i++)
    walk_arm(c, g, &t->u.match.arms[i], scope, smaller);
}

/* A call of the def rec has its arity in arguments, and the decreasing one
 * is a pattern variable of the top match. */
static void walk_app(C *c, const Global *g, const Ast *t, Names *scope) {
  size_t n = 0;
  const Ast *head = t;
  for (; head->kind == AST_APP; head = head->u.app.fun)
    n++;
  int call = head->kind == AST_VAR && same(head->u.name, g->name) && lookup(scope, g->name) == NULL;
  const Ast *arg = t;
  for (size_t k = 0; call && n >= g->arity && k + 1 + g->decreasing < n; k++)
    arg = arg->u.app.fun;
  const Names *var = arg->u.app.arg->kind == AST_VAR ? lookup(scope, arg->u.app.arg->u.name) : NULL;
  if (call && (n < g->arity || var == NULL || !var->smaller))
    fail(c, "TYPE_REC", t->loc, "%s recurs without %zu arguments and a smaller one", g->name, g->arity);
  for (const Ast *a = t; a->kind == AST_APP; a = a->u.app.fun)
    walk(c, g, a->u.app.arg, scope);
  if (!call)
    walk(c, g, head, scope);
}

static void walk(C *c, const Global *g, const Ast *t, Names *scope) {
  if (c->failed)
    return;
  switch (t->kind) {
  case AST_VAR:
    if (same(t->u.name, g->name) && lookup(scope, g->name) == NULL)
      fail(c, "TYPE_REC", t->loc, "%s recurs without its arguments", g->name);
    return;
  case AST_NAT: case AST_TYPE: case AST_TUPLE0: case AST_PROD0: return;
  case AST_PI:
  case AST_SIGMA:
  case AST_LAM:
    walk(c, g, t->u.bind.binder.type, scope);
    walk(c, g, t->u.bind.body, push_name(c, scope, t->u.bind.binder.name, 0));
    return;
  case AST_APP: walk_app(c, g, t, scope); return;
  case AST_PAIR:
  case AST_TUPLE:
  case AST_PROD:
  case AST_SUM:
    walk(c, g, t->u.pair.left, scope);
    walk(c, g, t->u.pair.right, scope);
    return;
  case AST_INJ: walk(c, g, t->u.inj.value, scope); return;
  case AST_PROJ: walk(c, g, t->u.proj.term, scope); return;
  case AST_CASE:
    walk(c, g, t->u.cases.subject, scope);
    for (int i = 0; i < 2; i++)
      walk(c, g, t->u.cases.arms[i].binder.type, scope);
    for (int i = 0; i < 2; i++)
      walk(c, g, t->u.cases.arms[i].body, push_name(c, scope, t->u.cases.arms[i].binder.name, 0));
    return;
  case AST_MATCH: walk_match(c, g, t, scope, 0); return;
  }
}

static void structural(C *c, Global *g, const Decl *d) {
  const Ast *body = d->body;
  for (; body->kind == AST_LAM; body = body->u.bind.body)
    ;
  int top = body->kind == AST_MATCH && body->u.match.subject->kind == AST_VAR;
  const char *subject = top ? body->u.match.subject->u.name : NULL;
  Names *scope = NULL;
  size_t n = 0;
  int found = 0;
  for (const Ast *t = d->body; t->kind == AST_LAM; t = t->u.bind.body, n++) {
    int hit = same(t->u.bind.binder.name, subject);
    g->decreasing = hit ? n : g->decreasing;
    found = found || hit;
    scope = push_name(c, scope, t->u.bind.binder.name, 0);
  }
  g->arity = n;
  if (!found) {
    fail(c, "TYPE_REC", d->loc, "def rec %s is not fun ... => match on one of its parameters", d->name);
    return;
  }
  walk_match(c, g, body, scope, 1);
}

/* ---- declarations ---- */

static Global *add_global(C *c, const char *name, GlobalKind kind, Value *type, int prelude) {
  Global *g = alloc(c, sizeof *g);
  g->name = name;
  g->kind = kind;
  g->type = type;
  g->prelude = prelude;
  g->next = c->globals;
  if (!c->failed)
    c->globals = g;
  return g;
}

static Value *closed_type(C *c, const Ast *t) {
  sort_of(c, NULL, 0, t);
  return eval(c, NULL, t);
}

static int occurs(const char *name, const Ast *t);

static int occurs_match(const char *name, const Ast *t) {
  int found = occurs(name, t->u.match.subject) || occurs(name, t->u.match.motive);
  for (size_t i = 0; i < t->u.match.narms; i++)
    found = found || occurs(name, t->u.match.arms[i].body);
  return found;
}

static int occurs(const char *name, const Ast *t) {
  switch (t->kind) {
  case AST_VAR: return same(t->u.name, name);
  case AST_NAT: case AST_TYPE: case AST_TUPLE0: case AST_PROD0: return 0;
  case AST_PI:
  case AST_SIGMA:
  case AST_LAM: return occurs(name, t->u.bind.binder.type) || occurs(name, t->u.bind.body);
  case AST_APP: return occurs(name, t->u.app.fun) || occurs(name, t->u.app.arg);
  case AST_PAIR:
  case AST_TUPLE:
  case AST_PROD:
  case AST_SUM: return occurs(name, t->u.pair.left) || occurs(name, t->u.pair.right);
  case AST_INJ: return occurs(name, t->u.inj.value);
  case AST_PROJ: return occurs(name, t->u.proj.term);
  case AST_CASE:
    return occurs(name, t->u.cases.subject) || occurs(name, t->u.cases.arms[0].binder.type) ||
           occurs(name, t->u.cases.arms[0].body) || occurs(name, t->u.cases.arms[1].binder.type) ||
           occurs(name, t->u.cases.arms[1].body);
  case AST_MATCH: return occurs_match(name, t);
  }
  return 1;
}

/* The number of arguments of NAME a b ..., when no argument mentions NAME;
 * SIZE_MAX otherwise. */
static size_t spine_of(const char *name, const Ast *t) {
  size_t n = 0;
  int clean = 1;
  for (; t->kind == AST_APP; t = t->u.app.fun, n++)
    clean = clean && !occurs(name, t->u.app.arg);
  return clean && t->kind == AST_VAR && same(t->u.name, name) ? n : SIZE_MAX;
}

static Bind *check_field(C *c, const Global *fam, Bind *ctx, int lvl, const Binder *b, unsigned level) {
  unsigned long long sort = sort_of(c, ctx, lvl, b->type);
  if (!c->failed && occurs(fam->name, b->type) && spine_of(fam->name, b->type) == SIZE_MAX)
    fail(c, "TYPE_MU", b->loc, "%s occurs in a field in a position that is not strictly positive", fam->name);
  if (!c->failed && sort > level)
    fail(c, "TYPE_MU", b->loc, "a field of %s lives in Type %llu, above its family", fam->name, sort);
  return bind(c, ctx, b->name, mk_var(c, lvl, b->name), eval(c, ctx, b->type), b->erased);
}

static void check_ctor(C *c, Global *fam, const Ctor *k, size_t index, unsigned level) {
  if (find_global(c, k->name) != NULL) {
    fail(c, "TYPE_DUPLICATE", k->loc, "%s is declared already", k->name);
    return;
  }
  Bind *ctx = NULL;
  int lvl = 0;
  const Ast *t = k->type;
  for (; t->kind == AST_PI && !c->failed; t = t->u.bind.body, lvl++)
    ctx = check_field(c, fam, ctx, lvl, &t->u.bind.binder, level);
  if (!c->failed && spine_of(fam->name, t) != fam->arity) {
    fail(c, "TYPE_MU", k->loc, "%s does not end in %s with %zu indices", k->name, fam->name, fam->arity);
    return;
  }
  sort_of(c, ctx, lvl, t);
  Global *g = add_global(c, k->name, G_CTOR, eval(c, NULL, k->type), 1);
  g->family = fam;
  g->index = index;
  g->arity = (size_t)lvl;
  g->value = mk_global(c, V_CON, g);
}

static void check_mu(C *c, const Decl *d) {
  const Ast *t = d->type;
  size_t arity = 0;
  for (; t->kind == AST_PI; t = t->u.bind.body)
    arity++;
  if (t->kind != AST_TYPE) {
    fail(c, "TYPE_MU", d->loc, "the type of mu %s does not end in Type", d->name);
    return;
  }
  Global *fam = add_global(c, d->name, G_MU, closed_type(c, d->type), 1);
  fam->decl = d;
  fam->arity = arity;
  fam->value = mk_global(c, V_DATA, fam);
  for (size_t i = 0; i < d->nctors && !c->failed; i++)
    check_ctor(c, fam, &d->ctors[i], i, t->u.level);
}

static void check_def(C *c, const Decl *d, int prelude) {
  Value *type = closed_type(c, d->type);
  check(c, NULL, 0, d->body, type, 1);
  Global *g = add_global(c, d->name, G_DEF, type, prelude);
  g->decl = d;
  g->value = eval(c, NULL, d->body);
}

static void check_rec(C *c, const Decl *d) {
  Global *g = add_global(c, d->name, G_REC, closed_type(c, d->type), 1);
  g->decl = d;
  g->value = mk_global(c, V_NEU, g);
  structural(c, g, d);
  check(c, NULL, 0, d->body, g->type, 1);
  g->body = eval(c, NULL, d->body);
}

static void check_decl(C *c, const Program *p, size_t i, int prelude) {
  const Decl *d = &p->decls[i];
  if (c->failed)
    return;
  c->file = p->file;
  c->def = span_of(d->name);
  c->loc = d->loc;
  c->fuel = CHECK_FUEL;
  if (!prelude && d->kind == DECL_MU) {
    fail(c, "REFUSE_MU", d->loc, "a program may not declare mu %s; mu lives in the prelude", d->name);
    return;
  }
  if (!prelude && d->kind == DECL_REC) {
    fail(c, "REFUSE_REC", d->loc, "a program may not declare def rec %s; recursion lives in the prelude",
         d->name);
    return;
  }
  Global *old = find_global(c, d->name);
  if (old != NULL) {
    fail(c, old->prelude && !prelude ? "REFUSE_PRELUDE_NAME" : "TYPE_DUPLICATE", d->loc,
         "%s is declared already", d->name);
    return;
  }
  switch (d->kind) {
  case DECL_DEF: check_def(c, d, prelude); return;
  case DECL_REC: check_rec(c, d); return;
  case DECL_MU: check_mu(c, d); return;
  }
}

static void prim_global(C *c, const char *name, Prim p, Value *type) {
  Global *g = add_global(c, name, G_PRIM, type, 1);
  g->prim = p;
  g->arity = 2;
  g->value = mk_global(c, V_NEU, g);
}

static void builtins(C *c) {
  Value *nat = mk(c, V_NAT_TYPE);
  Value *unit = mk(c, V_PROD0);
  Value *truth = mk_two(c, V_SUM, unit, unit);
  Global *g = add_global(c, "Nat", G_DEF, mk_type(c, 0), 1);
  g->value = nat;
  prim_global(c, "natAdd", P_ADD, mk_two(c, V_PI, nat, mk_two(c, V_PI, nat, nat)));
  prim_global(c, "natSub", P_SUB, mk_two(c, V_PI, nat, mk_two(c, V_PI, nat, nat)));
  prim_global(c, "natMul", P_MUL, mk_two(c, V_PI, nat, mk_two(c, V_PI, nat, nat)));
  prim_global(c, "natDiv", P_DIV, mk_two(c, V_PI, nat, mk_two(c, V_PI, nat, nat)));
  prim_global(c, "natMod", P_MOD, mk_two(c, V_PI, nat, mk_two(c, V_PI, nat, nat)));
  prim_global(c, "natEq", P_EQ, mk_two(c, V_PI, nat, mk_two(c, V_PI, nat, truth)));
  prim_global(c, "natLt", P_LT, mk_two(c, V_PI, nat, mk_two(c, V_PI, nat, truth)));
}

static int members_ok(C *c, const Program *p) {
  const Decl *d = p->ndecls > 0 ? &p->decls[0] : NULL;
  int ok = d != NULL && d->kind == DECL_DEF && same(d->name, "members") && d->type->kind == AST_VAR &&
           same(d->type->u.name, "Nat") && d->body->kind == AST_NAT && d->body->u.nat >= 1 &&
           d->body->u.nat <= UINT_MAX;
  Loc start = {1, 1};
  if (!ok)
    fail(c, "REFUSE_MEMBERS", d != NULL ? d->loc : start,
         "the first declaration must be def members : Nat := N with N >= 1");
  return ok;
}

static void find_regime(C *c) {
  Global *agg = find_global(c, "agg");
  Value *ty = agg != NULL && !agg->prelude ? agg->type : NULL;
  Global *family = find_global(c, "Aggregation");
  Global *choice = find_global(c, "ChoiceRule");
  Global *rule = NULL;
  /* Type aliases have already reduced. Recover a program ChoiceRule from
   * the checked family index rather than requiring a literal annotation. */
  if (ty != NULL && ty->kind == V_DATA && ty->global == family && ty->nargs == 1 && choice != NULL) {
    for (Global *g = c->globals; g != NULL && rule == NULL && !c->failed; g = g->next)
      if (!g->prelude && g->kind == G_DEF && conv(c, g->type, choice->value, 0) &&
          conv(c, g->value, ty->args[0], 0))
        rule = g;
  }
  int debreu = rule != NULL;
  c->regime = debreu ? LANG_REGIME_DEBREU : LANG_REGIME_IMPOSSIBILITY;
  c->rule = debreu ? rule : NULL;
  c->agg = agg;
}

int lang_check(Arena *arena, const Program *prelude, const Program *program, LangChecked **checked,
                 Diag *diag) {
  C *c = arena_alloc(arena, sizeof *c);
  if (c == NULL) {
    diag_set(diag, "MEMORY", span_of("-"), "the arena is full");
    return LANG_EXIT_REFUSED;
  }
  c->arena = arena;
  c->diag = diag;
  c->fuel = CHECK_FUEL;
  c->file = program->file;
  c->def = span_of("-");
  c->bad.kind = V_UNIT;
  c->bad.level = -1;
  c->bad_ast.kind = AST_TUPLE0;
  c->bad_ast.depth = 1;
  *checked = c;
  if (!members_ok(c, program))
    return LANG_EXIT_REFUSED;
  builtins(c);
  check_decl(c, program, 0, 0);
  c->members = (unsigned)program->decls[0].body->u.nat;
  for (size_t i = 0; i < prelude->ndecls; i++)
    check_decl(c, prelude, i, 1);
  for (size_t i = 1; i < program->ndecls; i++)
    check_decl(c, program, i, 0);
  if (!c->failed)
    find_regime(c);
  return c->failed ? LANG_EXIT_REFUSED : LANG_EXIT_OK;
}

LangRegime lang_regime(const LangChecked *checked) { return checked->regime; }

unsigned lang_members(const LangChecked *checked) { return checked->members; }

/* ---- verbs ---- */

static Value *global_value(C *c, const char *name) {
  Global *g = find_global(c, name);
  if (g == NULL && !c->failed)
    refuse(c, "TYPE_INTERNAL", name, "is not in the prelude");
  return g == NULL ? &c->bad : g->value;
}

static Value *ctor2(C *c, const char *ctor, Value *a, Value *b) {
  return apply(c, apply(c, global_value(c, ctor), a), b);
}

static Value *refl_members(C *c) {
  return apply(c, global_value(c, "reflNat"), mk_nat(c, c->members));
}

/* D, the codomain of the domain's ChoiceRule: a mu with no indices and 2 to
 * LANG_DECISIONS_MAX nullary constructors. Returns 0 after TABLE_DECISION. */
static int decision_family(C *c) {
  if (c->failed || c->decision != NULL)
    return !c->failed;
  Global *choice = find_global(c, "ChoiceRule");
  Value *pi = choice != NULL ? choice->value : NULL;
  Value *d = pi != NULL && pi->kind == V_PI ? instantiate(c, pi, mk_var(c, 0, pi->name)) : NULL;
  Global *fam = d != NULL && d->kind == V_DATA && d->nargs == 0 ? d->global : NULL;
  const Decl *decl = fam != NULL && fam->kind == G_MU ? fam->decl : NULL;
  size_t k = decl != NULL ? decl->nctors : 0;
  int ok = !c->failed && k >= 2 && k <= LANG_DECISIONS_MAX;
  Value **ballots = ok ? arena_alloc(c->arena, k * sizeof *ballots) : NULL;
  size_t text = sizeof "does not reduce to " + 4 * k;
  for (size_t i = 0; ok && i < k; i++) {
    Global *g = find_global(c, decl->ctors[i].name);
    ok = ballots != NULL && g != NULL && g->kind == G_CTOR && g->arity == 0;
    ballots[i] = ok ? g->value : NULL;
    text += strlen(decl->ctors[i].name);
  }
  char *stuck = ok ? arena_alloc(c->arena, text) : NULL;
  if (!ok || stuck == NULL) {
    refuse(c, "TABLE_DECISION", "ChoiceRule", "does not end in a mu of 2 to 64 nullary constructors");
    return 0;
  }
  size_t at = (size_t)snprintf(stuck, text, "does not reduce to ");
  for (size_t i = 0; i < k; i++)
    at += (size_t)snprintf(stuck + at, text - at, "%s%s", i == 0 ? "" : i + 1 == k ? " or " : ", ",
                           decl->ctors[i].name);
  c->decision = fam;
  c->decisions = (unsigned)k;
  c->ballots = ballots;
  c->stuck = stuck;
  return 1;
}

unsigned lang_decisions(const LangChecked *checked) { return checked->decisions; }

/* Code j for constructor j - 1 of D (k = 3: 1 open, 2 restricted, 3 frozen);
 * 0 after TABLE_STUCK. */
static int decision_code(C *c, const Value *d, const char *what) {
  int ok = !c->failed && d->kind == V_CON && d->nargs == 0 && d->global->family == c->decision;
  if (!ok && !c->failed)
    refuse(c, "TABLE_STUCK", what, c->stuck);
  return ok ? (int)d->global->index + 1 : 0;
}

/* The tally value is the right-nested k-tuple of counts (k = 3: (r, (f, h))). */
static int tally_code(C *c, const unsigned *parts) {
  c->fuel = CHECK_FUEL;
  Value *counts = mk_nat(c, parts[c->decisions - 1]);
  for (unsigned j = c->decisions - 1; j > 0; j--)
    counts = mk_two(c, V_TUPLE, mk_nat(c, parts[j - 1]), counts);
  Value *tally = ctor2(c, "mkTally", counts, refl_members(c));
  Value *rule = apply(c, apply(c, global_value(c, "rule"), c->rule->value), c->agg->value);
  return decision_code(c, apply(c, rule, tally), "rule G agg (mkTally ...)");
}

int lang_table(LangChecked *c, const unsigned char **codes, size_t *count) {
  *codes = NULL;
  *count = 0;
  if (c->failed)
    return LANG_EXIT_REFUSED;
  if (c->regime == LANG_REGIME_IMPOSSIBILITY)
    return LANG_EXIT_OK;
  if (!decision_family(c))
    return LANG_EXIT_REFUSED;
  unsigned n = c->members;
  unsigned k = c->decisions;
  size_t total = lang_tally_count(k, n, TALLY_MAX);
  if (n > TABLE_MAX || total > TALLY_MAX)
    return refuse(c, "TABLE_LIMIT", "members", "is too large for a table");
  unsigned char *out = arena_alloc(c->arena, total);
  unsigned *parts = arena_alloc(c->arena, k * sizeof *parts);
  if (out == NULL || parts == NULL)
    return refuse(c, "MEMORY", "table", "does not fit in the arena");
  memset(parts, 0, k * sizeof *parts);
  parts[k - 1] = n;
  size_t i = 0;
  for (int more = 1; more; more = lang_tally_next(parts, k, n))
    out[i++] = (unsigned char)tally_code(c, parts);
  *codes = out;
  *count = total;
  return c->failed ? LANG_EXIT_REFUSED : LANG_EXIT_OK;
}

/* Vector v: ballot i is constructor (v / k^i) mod k of D. */
static int vector_code(C *c, const Global *g, size_t v) {
  c->fuel = CHECK_FUEL;
  Value *xs = global_value(c, "bnil");
  for (unsigned i = 0; i < c->members; i++, v /= c->decisions)
    xs = ctor2(c, "bcons", c->ballots[v % c->decisions], xs);
  Value *config = ctor2(c, "mkConfig", xs, refl_members(c));
  return decision_code(c, apply(c, g->value, config), g->name);
}

int lang_verdicts(LangChecked *c, const char *name, FILE *out) {
  Global *g = find_global(c, name);
  Global *choice = find_global(c, "ChoiceRule");
  if (g == NULL || choice == NULL || !conv(c, g->type, choice->value, 0))
    return refuse(c, "VERDICT_TYPE", name, "is not a ChoiceRule");
  if (!decision_family(c))
    return LANG_EXIT_REFUSED;
  size_t total = 1;
  for (unsigned i = 0; i < c->members && total <= VERDICT_VECTORS; i++)
    total *= c->decisions;
  if (total > VERDICT_VECTORS)
    return refuse(c, "VERDICT_LIMIT", "members", "is too large for verdicts (k^members vectors)");
  unsigned char *codes = arena_alloc(c->arena, total);
  if (codes == NULL)
    return refuse(c, "MEMORY", "verdicts", "do not fit in the arena");
  for (size_t k = 0; k < total; k++)
    codes[k] = (unsigned char)vector_code(c, g, k);
  if (c->failed)
    return LANG_EXIT_REFUSED;
  for (size_t k = 0; k < total; k++) {
    if (c->decisions <= 9)
      fputc('0' + codes[k], out);
    else
      fprintf(out, "%s%u", k == 0 ? "" : " ", (unsigned)codes[k]);
  }
  fputc('\n', out);
  return LANG_EXIT_OK;
}

int lang_eval(LangChecked *c, const char *name, FILE *out) {
  Global *g = find_global(c, name);
  if (g == NULL)
    return refuse(c, "TYPE_SCOPE", name, "is not declared");
  c->fuel = CHECK_FUEL;
  Ast *t = quote(c, g->value, 0);
  if (c->failed)
    return LANG_EXIT_REFUSED;
  lang_print_term(out, t);
  fputc('\n', out);
  return LANG_EXIT_OK;
}

/* ---- program data (SPEC section 7, I3 plan MY CALLs 11-12) ---- */

static Value *data_type(C *c, const char *name) {
  Global *g = find_global(c, name);
  return g != NULL && g->prelude ? g->value : NULL;
}

static Value *data_arrow(C *c, Value *a, Value *b) {
  return a != NULL && b != NULL ? mk_two(c, V_PI, a, b) : NULL;
}

/* The value of the program def NAME; NULL when the program has no def NAME
 * or after CONTRACT_TYPE (its type is not TYPE). */
static Value *data_def(C *c, const char *name, Value *type) {
  Global *g = find_global(c, name);
  if (c->failed || g == NULL || g->prelude)
    return NULL;
  if (g->kind != G_DEF || type == NULL || !conv(c, g->type, type, 0)) {
    refuse(c, "CONTRACT_TYPE", name, "does not have its program data type (SPEC section 7)");
    return NULL;
  }
  return g->value;
}

static Value *data_apply(C *c, Value *f, Value *a) {
  c->fuel = CHECK_FUEL;
  return apply(c, f, a);
}

/* Code j + 1 for constructor j of the mu FAM; 0 after CONTRACT_VALUE. */
static unsigned data_code(C *c, const Value *v, const Global *fam, const char *name) {
  int ok = !c->failed && v->kind == V_CON && v->nargs == 0 && v->global->family == fam;
  if (!ok && !c->failed)
    refuse(c, "CONTRACT_VALUE", name, "does not reduce to a constructor");
  return ok ? (unsigned)v->global->index + 1 : 0;
}

static unsigned long long data_nat(C *c, const Value *v, const char *name) {
  if (!c->failed && v->kind != V_NAT)
    refuse(c, "CONTRACT_VALUE", name, "does not reduce to a number");
  return v->kind == V_NAT ? v->nat : 0;
}

/* Profile code 2 (juris - 1) + (status - 1) (MY CALL 12). */
static unsigned data_profile(C *c, const Value *v, const char *name) {
  int ok = v->kind == V_TUPLE;
  unsigned j = ok ? data_code(c, v->left, find_global(c, "Juris"), name) : 0;
  unsigned s = ok ? data_code(c, v->right, find_global(c, "Status"), name) : 0;
  if (!ok && !c->failed)
    refuse(c, "CONTRACT_VALUE", name, "does not reduce to a profile tuple");
  return j > 0 && s > 0 ? 2 * (j - 1) + (s - 1) : 0;
}

static Value *profile_value(C *c, unsigned p) {
  return mk_two(c, V_TUPLE, global_value(c, p >> 1 ? "foreign" : "domestic"),
                global_value(c, p & 1 ? "accredited" : "retail"));
}

static void data_genesis(C *c, Value *v, LangDomainData *data) {
  Global *row = find_global(c, "hrow");
  unsigned long long total = 0;
  for (; !c->failed && v->kind == V_CON && v->global == row && v->nargs == 5; v = v->args[4]) {
    if (data->holders == LANG_GENESIS_MAX) {
      refuse(c, "CONTRACT_GENESIS", "genesis", "has more than 32 rows");
      return;
    }
    LangHolder *h = &data->holder[data->holders++];
    h->wallet = data_nat(c, v->args[0], "genesis");
    for (size_t j = 0; j + 1 < data->holders; j++)
      if (data->holder[j].wallet == h->wallet) {
        diag_set(c->diag, "CONTRACT_GENESIS", span_of("genesis"), "repeats wallet %llu", h->wallet);
        c->failed = 1;
        return;
      }
    h->identity = data_nat(c, v->args[1], "genesis");
    if (!c->failed && h->identity == 0) {
      refuse(c, "CONTRACT_GENESIS", "genesis", "has a row with identity 0");
      return;
    }
    h->profile = data_profile(c, v->args[2], "genesis");
    h->units = data_nat(c, v->args[3], "genesis");
    if (h->units > ULLONG_MAX - total) {
      refuse(c, "CONTRACT_GENESIS", "genesis", "has more than 2^64 - 1 units in total");
      return;
    }
    total += h->units;
  }
  if (!c->failed && !(v->kind == V_CON && v->global == find_global(c, "hnil")))
    refuse(c, "CONTRACT_VALUE", "genesis", "does not reduce to hrow rows that end in hnil");
}

/* The bytes of the Text def NAME (O5c, MY CALL 169): each byte 32 .. 126,
 * at most LANG_TEXT_MAX bytes, else CONTRACT_VALUE. */
static void data_text(C *c, const Value *v, LangText *text, const char *name) {
  Global *byte = find_global(c, "char");
  text->length = 0;
  for (; !c->failed && v->kind == V_CON && v->global == byte && v->nargs == 2; v = v->args[1]) {
    unsigned long long b = data_nat(c, v->args[0], name);
    if (c->failed)
      return;
    if (b < 32 || b > 126) {
      refuse(c, "CONTRACT_VALUE", name, "has a byte outside 32 .. 126");
      return;
    }
    if (text->length == LANG_TEXT_MAX) {
      refuse(c, "CONTRACT_VALUE", name, "has more than 32 bytes");
      return;
    }
    text->byte[text->length++] = (unsigned char)b;
  }
  if (!c->failed && !(v->kind == V_CON && v->global == find_global(c, "end")))
    refuse(c, "CONTRACT_VALUE", name, "does not reduce to char bytes that end in end");
}

static void text_set(LangText *text, const char *bytes) {
  text->length = strlen(bytes);
  memcpy(text->byte, bytes, text->length);
}

static LangCap data_cap(C *c, const Value *v) {
  LangCap cap = {LANG_CAP_DENY, 0};
  int ok = !c->failed && v->kind == V_CON && v->global->family == find_global(c, "Cap");
  if (!ok) {
    if (!c->failed)
      refuse(c, "CONTRACT_VALUE", "restrict", "does not reduce to a Cap constructor");
    return cap;
  }
  if (same(v->global->name, "upTo") && v->nargs == 1) {
    cap.tag = LANG_CAP_UP_TO;
    cap.n = data_nat(c, v->args[0], "restrict");
  } else if (same(v->global->name, "any")) {
    cap.tag = LANG_CAP_ANY;
  }
  return cap;
}

static int seen_identity(const LangDomainData *data, size_t i) {
  for (size_t j = 0; j < i; j++)
    if (data->holder[j].identity == data->holder[i].identity)
      return 1;
  return 0;
}

int lang_data(LangChecked *c, LangDomainData *data) {
  memset(data, 0, sizeof *data);
  data->start = 1;
  text_set(&data->name, LANG_NAME_DEFAULT);
  text_set(&data->symbol, LANG_SYMBOL_DEFAULT);
  if (c->failed)
    return LANG_EXIT_REFUSED;
  Global *charter = find_global(c, "Charter");
  const Decl *decl = charter != NULL && charter->prelude && charter->kind == G_MU ? charter->decl : NULL;
  size_t k = decl != NULL ? decl->nctors : 0;
  if (k > LANG_DECISIONS_MAX)
    return refuse(c, "CONTRACT_TYPE", "Charter", "has more than 64 constructors");
  data->charters = (unsigned)k;
  Value *ctype = decl != NULL ? charter->value : NULL;
  Value **charters = k > 0 ? arena_alloc(c->arena, k * sizeof *charters) : NULL;
  for (size_t i = 0; i < k && charters != NULL; i++)
    charters[i] = global_value(c, decl->ctors[i].name);
  Value *unit = mk(c, V_PROD0);
  Value *start = data_def(c, "start", ctype);
  if (start != NULL)
    data->start = data_code(c, start, charter, "start");
  Value *asset = data_def(c, "asset", data_type(c, "AssetMode"));
  if (asset != NULL)
    data->asset = data_code(c, asset, find_global(c, "AssetMode"), "asset") == 2 ? LANG_ASSET_TOKEN : LANG_ASSET_WEI;
  Value *name = data_def(c, "name", data_type(c, "Text"));
  if (name != NULL)
    data_text(c, name, &data->name, "name");
  Value *symbol = data_def(c, "symbol", data_type(c, "Text"));
  if (symbol != NULL)
    data_text(c, symbol, &data->symbol, "symbol");
  Value *genesis = data_def(c, "genesis", data_type(c, "Holders"));
  if (genesis != NULL)
    data_genesis(c, genesis, data);
  Value *restrict_ = data_def(c, "restrict", data_arrow(c, ctype, data_type(c, "Restriction")));
  for (size_t i = 0; restrict_ != NULL && i < k; i++)
    for (unsigned p = 0; p < LANG_PROFILES; p++)
      for (unsigned r = 0; r < LANG_PROFILES && !c->failed; r++)
        data->cap[i][p][r] = data_cap(c, data_apply(c, data_apply(c, data_apply(c, restrict_, charters[i]),
                                                               profile_value(c, p)), profile_value(c, r)));
  Value *waterfall = data_def(c, "waterfall", data_arrow(c, ctype, data_type(c, "Waterfall")));
  static const char *const kinds[LANG_KINDS] = {"rent", "sale"};
  for (size_t i = 0; waterfall != NULL && i < k; i++)
    for (unsigned j = 0; j < LANG_KINDS && !c->failed; j++)
      data->pass[i][j] = data_code(c, data_apply(c, data_apply(c, waterfall, charters[i]), global_value(c, kinds[j])),
                                   find_global(c, "Gate"), "waterfall") == 1;
  Value *truth = mk_two(c, V_SUM, unit, unit);
  Value *issuers = data_def(c, "issuers", data_arrow(c, ctype, data_arrow(c, data_type(c, "Identity"), truth)));
  for (size_t i = 0; issuers != NULL && i < k; i++)
    for (size_t h = 0; h < data->holders && !c->failed; h++) {
      if (seen_identity(data, h))
        continue;
      Value *v = data_apply(c, data_apply(c, issuers, charters[i]), mk_nat(c, data->holder[h].identity));
      if (v->kind != V_INJ) {
        refuse(c, "CONTRACT_VALUE", "issuers", "does not reduce to none or some");
        break;
      }
      if (v->nat == 1)
        data->issuer[data->issuers++] = (LangIssuer){(unsigned)i + 1, data->holder[h].identity};
    }
  return c->failed ? LANG_EXIT_REFUSED : LANG_EXIT_OK;
}
