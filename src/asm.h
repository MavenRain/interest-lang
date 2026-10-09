#ifndef LANG_ASM_H
#define LANG_ASM_H
/* The EVM assembler of langc and the entry table of the domain.
 *
 * src/evm.c is the core: the assembler, the dispatcher, checked add, the
 * ballot tally, the verdict table, the cast and amend entries and the
 * creation code. domain/entries.c is the domain: the storage layout, the
 * entries that move value and the entry list of each regime, in dispatch
 * order. The core writes the dispatch case and the head of each entry, then
 * calls its emit function for the body.
 *
 * Memory: 0x00 to 0x3f is core scratch (keccak256, the table read); the
 * domain owns 0x80 and above. */
#include "evm.h"

enum {
  EVM_CAPACITY = 24576,
  EVM_FIXUPS = 512,
  EVM_LABELS = 64
};

typedef enum {
  OP_ADD = 0x01, OP_MUL = 0x02, OP_SUB = 0x03, OP_DIV = 0x04, OP_MOD = 0x06,
  OP_LT = 0x10, OP_GT = 0x11, OP_EQ = 0x14, OP_ISZERO = 0x15, OP_AND = 0x16,
  OP_SHR = 0x1c, OP_SHA3 = 0x20, OP_CALLER = 0x33, OP_CALLVALUE = 0x34,
  OP_CALLDATALOAD = 0x35, OP_CALLDATASIZE = 0x36, OP_CODECOPY = 0x39,
  OP_POP = 0x50, OP_MLOAD = 0x51, OP_MSTORE = 0x52, OP_SLOAD = 0x54,
  OP_SSTORE = 0x55, OP_JUMP = 0x56, OP_JUMPI = 0x57, OP_GAS = 0x5a,
  OP_JUMPDEST = 0x5b, OP_PUSH0 = 0x5f, OP_PUSH1 = 0x60, OP_PUSH2 = 0x61,
  OP_PUSH4 = 0x63, OP_DUP1 = 0x80, OP_DUP2 = 0x81, OP_DUP3 = 0x82,
  OP_DUP4 = 0x83, OP_SWAP1 = 0x90, OP_SWAP2 = 0x91, OP_SWAP3 = 0x92,
  OP_LOG3 = 0xa3, OP_CALL = 0xf1, OP_RETURN = 0xf3, OP_REVERT = 0xfd
} Op;

/* A jump label. The labels below LABEL_FREE belong to the core; asm_label
 * gives the others (one per entry, and the labels of an entry body). */
typedef unsigned Label;
enum { LABEL_REVERT, LABEL_TABLE, LABEL_RUNTIME, LABEL_DATA, LABEL_FREE };

typedef enum { ENTRY_PAYABLE, ENTRY_NONPAYABLE } Payment;

/* The first overflow of an Asm: the code buffer, the label sites, or the
 * text of an entry signature. finish reports each one as EVM_SIZE. */
typedef enum { ASM_ROOM, ASM_FULL_CODE, ASM_FULL_FIXUPS, ASM_FULL_SIGNATURE } AsmFull;

/* Pass 1 appends code and records each PUSH2 label site; pass 2 (resolve)
 * writes the label offsets into those sites. */
typedef struct {
  unsigned char code[EVM_CAPACITY];
  size_t size;
  size_t at[EVM_LABELS];
  int bound[EVM_LABELS];
  size_t site[EVM_FIXUPS];
  Label target[EVM_FIXUPS];
  size_t sites;
  unsigned labels;   /* labels given by asm_label */
  int labels_full;
  AsmFull full;      /* ASM_ROOM while the code fits */
} Asm;

/* What an entry body knows of the contract. */
typedef struct {
  unsigned members;             /* n */
  unsigned decisions;           /* Debreu: k; impossibility: 0 */
  const unsigned char *packed;  /* Debreu: the amend word; impossibility: NULL */
  const LangDomainData *data;   /* the program data (evm.h); NULL: the defaults */
} EntryContext;

/* One external entry, name(uint256 x (words + n when ballots)), or
 * name(types) when types is set. The core guards the call value (unless
 * payable) and the calldata size (32 bytes per word). */
typedef struct {
  const char *name;
  unsigned words;   /* calldata words before the ballots */
  int ballots;      /* 1: n ballot words follow, from word `words` (Debreu only) */
  Payment payment;
  void (*emit)(Asm *a, const EntryContext *c);
  const char *types;  /* NULL: each word is uint256; else the argument text, as "address" */
} Entry;

/* domain/entries.c: the entries of REGIME in dispatch order, *count set. */
const Entry *lang_domain_entries(LangRegime regime, size_t *count);

/* The core entries, for the Debreu list: cast (n ballots) and amend (0 words). */
void lang_entry_cast(Asm *a, const EntryContext *c);
void lang_entry_amend(Asm *a, const EntryContext *c);

/* domain/entries.c: the genesis storage writes of the creation code, before
 * the runtime copy. */
void lang_domain_genesis(Asm *a, const LangContract *contract);
/* domain/entries.c: the code data of the runtime, at LABEL_DATA, after the
 * entries and the verdict table, so that no data byte precedes code. */
void lang_domain_data(Asm *a, const EntryContext *c);

Label asm_label(Asm *a);
void asm_put(Asm *a, unsigned value);
void asm_op(Asm *a, Op code);
/* The shortest PUSH of a big-endian word: PUSH0 for zero. */
void asm_push_word(Asm *a, const unsigned char word[32]);
void asm_push(Asm *a, unsigned long value);
void asm_push_label(Asm *a, Label label);
void asm_bind(Asm *a, Label label);
void asm_jumpdest(Asm *a, Label label);
void asm_jump(Asm *a, Label label);
void asm_jump_if(Asm *a, Label label);
void asm_revert_if(Asm *a);
/* Calldata word j of the arguments, at byte 4 + 32 j. */
void asm_argument(Asm *a, unsigned j);
/* key -> keccak256(key . base), the slot of a mapping entry. */
void asm_slot(Asm *a, unsigned base);
/* x y -> x + y; reverts when the sum wraps. */
void asm_checked_add(Asm *a);
/* x -> returns x as one word. */
void asm_return_top(Asm *a);
void asm_load(Asm *a, unsigned address);
void asm_store(Asm *a, unsigned address);
/* Reverts unless calldata word j is below 2^160. */
void asm_address_guard(Asm *a, unsigned j);
/* -> the decision code of the n ballots at calldata words first ..
 * first + n - 1, read from the verdict table. Each ballot must be 1 to k. */
void asm_tally(Asm *a, unsigned first, unsigned members, unsigned k);
#endif
