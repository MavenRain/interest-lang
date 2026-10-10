/* The program data of interestc (SPEC section 7, I3 plan MY CALLs 11-12):
 * the start charter, the genesis rows, the charter tables and the asset,
 * read from the optional program defs start, genesis, restrict, waterfall,
 * issuers, asset, name and symbol.
 * src/evm.h declares LangDomainData, and the domain defines it here (host
 * README, The domain). lang_data (src/check.c) reads it, and the hooks
 * lang_domain_read and lang_domain_print (domain/entries.c) give it to the
 * core. */
#ifndef LANG_DOMAIN_DATA_H
#define LANG_DOMAIN_DATA_H
#include "check.h"
enum { LANG_GENESIS_MAX = 32, LANG_PROFILES = 4, LANG_KINDS = 2, LANG_TEXT_MAX = 32, LANG_PARTITIONS = 2 };
typedef enum { LANG_CAP_DENY, LANG_CAP_UP_TO, LANG_CAP_ANY } LangCapTag;
typedef struct { LangCapTag tag; unsigned long long n; } LangCap;
typedef struct {
  unsigned long long wallet, identity, units;
  unsigned profile;            /* 2 (juris - 1) + (status - 1), 0 .. 3 */
  unsigned partition;          /* the Partition code, 1 .. LANG_PARTITIONS (O4) */
} LangHolder;
typedef struct { unsigned charter; unsigned long long identity; } LangIssuer;
/* The asset of the contract (SPEC section 7, O5b). */
typedef enum { LANG_ASSET_WEI, LANG_ASSET_TOKEN } LangAsset;
/* A Text def (SPEC section 7, O5c): at most LANG_TEXT_MAX bytes, each
 * 32 .. 126. The defaults when the def name or symbol is absent. */
typedef struct { size_t length; unsigned char byte[LANG_TEXT_MAX]; } LangText;
#define LANG_NAME_DEFAULT "interest"
#define LANG_SYMBOL_DEFAULT "INT"
struct LangDomainData {
  unsigned start;              /* the active charter at genesis, 1 .. charters */
  unsigned charters;           /* K, the constructors of Charter */
  size_t holders;
  LangHolder holder[LANG_GENESIS_MAX];
  LangCap cap[LANG_PARTITIONS][LANG_DECISIONS_MAX][LANG_PROFILES][LANG_PROFILES];  /* R, partition code - 1 first, then charter code - 1 */
  unsigned char pass[LANG_DECISIONS_MAX][LANG_KINDS];            /* W: 1 pass, 0 retain */
  size_t issuers;
  LangIssuer issuer[LANG_DECISIONS_MAX * LANG_GENESIS_MAX];
  LangAsset asset;             /* the def asset; absent: wei */
  LangText name, symbol;       /* the defs name and symbol; absent: the defaults */
};
/* The reader of lang_domain_read: the optional program defs start, genesis,
 * restrict, waterfall, issuers, asset, name and symbol, read by name and exact
 * type over the finite tables above; the defaults when a def is absent.
 * Errors: CONTRACT_TYPE, CONTRACT_VALUE, CONTRACT_GENESIS. */
int lang_data(LangChecked *checked, LangDomainData *data);
#endif
