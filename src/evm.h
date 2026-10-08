#ifndef LANG_EVM_H
#define LANG_EVM_H
#include <stddef.h>
#include <stdio.h>
typedef enum { LANG_REGIME_IMPOSSIBILITY, LANG_REGIME_DEBREU } LangRegime;
typedef enum { LANG_PART_CREATION, LANG_PART_RUNTIME } LangPart;
enum { LANG_DECISIONS_MAX = 64 };  /* k, the constructors of the decision type */
/* The program data of the contract (SPEC section 7, I3 plan MY CALLs 11-12):
 * the start charter, the genesis rows and the charter tables, read from the
 * optional program defs start, genesis, restrict, waterfall and issuers. */
enum { LANG_GENESIS_MAX = 32, LANG_PROFILES = 4, LANG_KINDS = 2 };
typedef enum { LANG_CAP_DENY, LANG_CAP_UP_TO, LANG_CAP_ANY } LangCapTag;
typedef struct { LangCapTag tag; unsigned long long n; } LangCap;
typedef struct {
  unsigned long long wallet, identity, units;
  unsigned profile;            /* 2 (juris - 1) + (status - 1), 0 .. 3 */
} LangHolder;
typedef struct { unsigned charter; unsigned long long identity; } LangIssuer;
typedef struct {
  unsigned start;              /* the active charter at genesis, 1 .. charters */
  unsigned charters;           /* K, the constructors of Charter */
  size_t holders;
  LangHolder holder[LANG_GENESIS_MAX];
  LangCap cap[LANG_DECISIONS_MAX][LANG_PROFILES][LANG_PROFILES];  /* R, charter code - 1 first */
  unsigned char pass[LANG_DECISIONS_MAX][LANG_KINDS];            /* W: 1 pass, 0 retain */
  size_t issuers;
  LangIssuer issuer[LANG_DECISIONS_MAX * LANG_GENESIS_MAX];
} LangDomainData;
typedef struct {
  unsigned members;            /* n >= 1 */
  LangRegime regime;
  const unsigned char *codes;  /* Debreu: one code 1 .. k per tally (code j = constructor j - 1); NULL for impossibility */
  size_t count;                /* Debreu: C(n+k-1, k-1), in the order of lang_tally_next; 0 for impossibility */
  unsigned decisions;          /* Debreu: k, 2 .. LANG_DECISIONS_MAX; the sample domain has k = 3 (1 open, 2 restricted, 3 frozen) */
  const LangDomainData *data;  /* NULL: the defaults (start 1, no genesis, deny, retain, no issuers) */
} LangContract;
/* Writes lowercase hex, no 0x, one trailing newline. Returns 0, or nonzero after writing "interestc: EVM_<CODE>: message\n" to err. */
int lang_evm_write(const LangContract *contract, LangPart part, FILE *out, FILE *err);
/* C(n+k-1, k-1), the tallies of n ballots over k codes; LIMIT + 1 when it is larger than LIMIT. */
size_t lang_tally_count(unsigned k, unsigned n, size_t limit);
/* The tally order: COUNTS has k parts that sum to n, first (0, .., 0, n).
 * Steps to the next composition, lexicographic on parts 0 .. k-2 with part
 * k-1 the remainder (k = 3: r outer, f inner, h = n - r - f). Returns 0
 * after the last one. */
int lang_tally_next(unsigned *counts, unsigned k, unsigned n);
#endif
