/* The langc checker (host README, Commands and Refusals).
 *
 * lang_check checks the program's `def members : Nat := N` first, then
 * the embedded prelude, then the rest of the program. The regime is Debreu
 * when the program defines `agg : Aggregation G` for a program definition
 * `G : ChoiceRule`, and impossibility otherwise. Errors go to the Diag of
 * the run as "interestc: CODE: DEF: message":
 *
 *   REFUSE_MEMBERS, REFUSE_MU, REFUSE_REC, REFUSE_FORM, REFUSE_PRELUDE_NAME
 *     the refusal list of the host README, Refusals;
 *   TYPE_SCOPE, TYPE_DUPLICATE, TYPE_MISMATCH (with both normal forms),
 *   TYPE_SHAPE, TYPE_INFER, TYPE_UNIVERSE, TYPE_ERASED, TYPE_MATCH, TYPE_MU,
 *   TYPE_REC, TYPE_NAT, TYPE_FUEL, TYPE_INTERNAL, MEMORY
 *     the checker;
 *   VERDICT_TYPE, VERDICT_LIMIT, TABLE_STUCK, TABLE_LIMIT, TABLE_DECISION,
 *   CONTRACT_TYPE, CONTRACT_VALUE, CONTRACT_GENESIS
 *     the verbs. */
#ifndef LANG_CHECK_H
#define LANG_CHECK_H
#include "evm.h"
#include "syntax.h"

typedef struct LangChecked LangChecked;

/* Returns LANG_EXIT_OK and the checked program in *CHECKED, or
 * LANG_EXIT_REFUSED with the first error in DIAG. DIAG must stay alive
 * while *CHECKED is used. */
int lang_check(Arena *arena, const Program *prelude, const Program *program,
                 LangChecked **checked, Diag *diag);
LangRegime lang_regime(const LangChecked *checked);
unsigned lang_members(const LangChecked *checked);
/* k, the constructors of D (the codomain of ChoiceRule), after lang_table; 0 before. */
unsigned lang_decisions(const LangChecked *checked);

/* Debreu: one decision code (code j = constructor j - 1 of D) per tally, in
 * the order of lang_tally_next. Impossibility: no codes. */
int lang_table(LangChecked *checked, const unsigned char **codes, size_t *count);
/* One decision code per ballot vector of the ChoiceRule NAME, in the product
 * order of test/differential.py, then a newline. Codes are compact digits for
 * up to 9 decisions, and space-separated decimal numbers above that. */
int lang_verdicts(LangChecked *checked, const char *name, FILE *out);
/* The normal form of NAME, then a newline. */
int lang_eval(LangChecked *checked, const char *name, FILE *out);

/* The program data hooks of the domain (domain/entries.c). The domain
 * defines LangDomainData (evm.h). lang_domain_read returns LANG_EXIT_OK and
 * the data of the program in *DATA (NULL: the defaults of the domain), or
 * LANG_EXIT_REFUSED with the first error in the Diag of lang_check. The
 * `build` verb passes *DATA to the contract (LangContract.data). The `data`
 * verb writes only what lang_domain_print writes for *DATA. */
int lang_domain_read(LangChecked *checked, const LangDomainData **data);
void lang_domain_print(const LangDomainData *data, FILE *out);
#endif
