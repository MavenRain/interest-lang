/* The domain entries of interestc: the interest storage, the genesis
 * writes, the code data and the entry list of each regime (I3 plan, MY
 * CALLs 12 to 14; host README, The domain). src/evm.c writes the dispatcher
 * and the head of each entry, then the bodies below.
 *
 * Storage (MY CALL 13): MU (slot 0, identity -> units), REGISTRY (slot 1,
 * wallet -> identity + 1, 0 = no identity), PROFILE (slot 2, identity ->
 * profile code 0 .. 3), CHARTER (slot 3, the active charter code 1 .. K),
 * RESERVE (slot 4, kind code 0 rent, 1 sale -> wei), INDEX (slot 5),
 * CHECKPOINT (slot 6, identity -> INDEX at the last settle), NUM (slot 7,
 * identity -> numerator in units of 1/S wei), DUST (slot 8, the treasury
 * dust in units of 1/S wei, L6) and CARRIER (slot 9, the ERC-20 carrier
 * address, token mode only, O5b). ALLOWANCE (slot 10, both modes, O5c) maps
 * an owner identity, then a spender identity, to units, as a Solidity
 * nested mapping. BALLOT (slot 11, identity -> code 0 .. k, 0 = no ballot)
 * and WEIGHT (slot 12, code -> units, the sum of MU over the identities
 * with that ballot) hold the weighted tally (O2, MY CALLs 182, 183 and
 * 186). In token mode, each amount in wei is an
 * amount in carrier units. S, the sum of the genesis units, is a code
 * constant. A mapping entry lives at keccak256(key . slot), as in Solidity.
 *
 * Code data at LABEL_DATA: the R limit words, rows x 4 x 4 (charter code - 1,
 * profile of the sender, profile of the receiver; "admits q" = q < L), then
 * the W gate words, rows x 2 (pass 1, retain 0), 32 bytes each.
 *
 * Program data (data.h): lang_domain_read gives the data that lang_data
 * (src/check.c) reads, and lang_domain_print writes it for the `data` verb. */
#include "asm.h"
#include "data.h"
#include "keccak.h"
#include <string.h>

enum {
  SLOT_MU = 0, SLOT_REGISTRY = 1, SLOT_PROFILE = 2, SLOT_CHARTER = 3,
  SLOT_RESERVE = 4, SLOT_INDEX = 5, SLOT_CHECKPOINT = 6, SLOT_NUM = 7,
  SLOT_DUST = 8, SLOT_CARRIER = 9, SLOT_ALLOWANCE = 10, SLOT_BALLOT = 11,
  SLOT_WEIGHT = 12
};

/* MEM_CALL: the calldata of a carrier call, the selector at MEM_CALL and
 * word j at MEM_CALL + 4 + 32 j. The selector store also writes zero to
 * 0xc4 .. 0xdf, which no entry uses; MEM_ID and MEM_CHARTER stay clear.
 * MEM_SEAT: the amend scratch after the carrier words (O2). */
enum { MEM_ID = 0x80, MEM_CHARTER = 0xa0, MEM_CALL = 0xe0, MEM_SEAT = 0x160 };

enum { WORD = 32, LIMITS = LANG_PROFILES * LANG_PROFILES };

/* The charter rows of the code data: k (Debreu) or K, whichever is
 * larger, so that every charter code that genesis or amend can write has a
 * row. */
static unsigned data_rows(const EntryContext *c) {
  unsigned charters = c->data == NULL ? 0u : c->data->charters;
  unsigned rows = c->decisions > charters ? c->decisions : charters;
  return rows > LANG_DECISIONS_MAX ? LANG_DECISIONS_MAX : rows;
}

/* The big-endian word of value + extra, extra 0 or 1 (no wrap below 2^65). */
static void word_of(unsigned char word[WORD], unsigned long long value, unsigned extra) {
  memset(word, 0, WORD);
  unsigned long long low = value + extra;
  for (size_t i = 0; i < sizeof low; i++)
    word[WORD - 1 - i] = (unsigned char)(low >> (8 * i));
  word[WORD - 1 - sizeof low] = (unsigned char)(low < value);
}

static void put_word(Asm *a, const unsigned char word[WORD]) {
  for (size_t i = 0; i < WORD; i++)
    asm_put(a, word[i]);
}

/* The limit word L of a cap: deny 0, upTo n -> n + 1, any 2^256 - 1. */
static void limit_word(unsigned char word[WORD], LangCap cap) {
  switch (cap.tag) {
    case LANG_CAP_DENY:
      memset(word, 0, WORD);
      return;
    case LANG_CAP_UP_TO:
      word_of(word, cap.n, 1);
      return;
    case LANG_CAP_ANY:
      memset(word, 0xff, WORD);
      return;
  }
  memset(word, 0, WORD);
}

/* -> code data word INDEX (from LABEL_DATA), read with CODECOPY into the
 * core scratch. */
static void data_word(Asm *a) {
  asm_push(a, WORD);
  asm_op(a, OP_MUL);
  asm_push_label(a, LABEL_DATA);
  asm_op(a, OP_ADD);
  asm_push(a, WORD);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_CODECOPY);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_MLOAD);
}

/* -> id(CALLER) = REGISTRY[CALLER] - 1; reverts when REGISTRY[CALLER] = 0. */
static void caller_identity(Asm *a) {
  asm_op(a, OP_CALLER);
  asm_slot(a, SLOT_REGISTRY);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_push(a, 1);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
}

/* Reverts unless calldata word j is a kind code, 0 or 1. */
static void kind_guard(Asm *a, unsigned j) {
  asm_push(a, 1);
  asm_argument(a, j);
  asm_op(a, OP_GT);
  asm_revert_if(a);
}

/* The sum of the genesis units of the program data (0 without data). */
static unsigned long long total_units(const LangDomainData *data) {
  unsigned long long sum = 0;
  for (size_t i = 0; data != NULL && i < data->holders; i++)
    sum += data->holder[i].units;
  return sum;
}

/* -> S, the code constant. */
static void push_supply(Asm *a, const EntryContext *c) {
  unsigned char word[WORD];
  word_of(word, total_units(c->data), 0);
  asm_push_word(a, word);
}

/* Reverts when S = 0. */
static void supply_guard(Asm *a, const EntryContext *c) {
  push_supply(a, c);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
}

/* x y -> x * y; reverts when the product wraps (x = 0, or p / x = y). */
static void checked_mul(Asm *a) {
  asm_op(a, OP_DUP2);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_MUL);
  asm_op(a, OP_SWAP2);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_ISZERO);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_DUP4);
  asm_op(a, OP_DIV);
  asm_op(a, OP_DUP3);
  asm_op(a, OP_EQ);
  asm_op(a, OP_ADD);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_op(a, OP_POP);
}

/* h -> claimOf(h) = NUM[h] + MU[h] * (INDEX - CHECKPOINT[h]) (checked). */
static void claim_of(Asm *a) {
  asm_op(a, OP_DUP1);
  asm_slot(a, SLOT_CHECKPOINT);
  asm_op(a, OP_SLOAD);
  asm_push(a, SLOT_INDEX);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_SUB);
  asm_op(a, OP_DUP2);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_SLOAD);
  checked_mul(a);
  asm_op(a, OP_SWAP1);
  asm_slot(a, SLOT_NUM);
  asm_op(a, OP_SLOAD);
  asm_checked_add(a);
}

/* h -> (settle h): NUM[h] := claimOf(h); CHECKPOINT[h] := INDEX. */
static void settle(Asm *a) {
  asm_op(a, OP_DUP1);
  claim_of(a);
  asm_op(a, OP_DUP2);
  asm_slot(a, SLOT_NUM);
  asm_op(a, OP_SSTORE);
  asm_push(a, SLOT_INDEX);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_SWAP1);
  asm_slot(a, SLOT_CHECKPOINT);
  asm_op(a, OP_SSTORE);
}

/* (nothing) -> (nothing): the selector of the carrier function SIG, the
 * first 4 bytes of keccak256(SIG), at MEM_CALL, as the last 4 bytes of a
 * zero word at MEM_CALL - 28. */
static void carrier_selector(Asm *a, const char *sig) {
  unsigned char hash[WORD];
  lang_keccak256((const unsigned char *)sig, strlen(sig), hash);
  unsigned char word[WORD] = {0};
  memcpy(word + WORD - 4, hash, 4);
  asm_push_word(a, word);
  asm_store(a, MEM_CALL - (WORD - 4));
}

/* (nothing) -> the carrier address (CARRIER, slot 9). */
static void carrier(Asm *a) {
  asm_push(a, SLOT_CARRIER);
  asm_op(a, OP_SLOAD);
}

/* (nothing) -> balanceOf(this) of the carrier, by STATICCALL. A failed call,
 * or return data of less than 32 bytes, reverts the entry. */
static void carrier_balance(Asm *a) {
  carrier_selector(a, "balanceOf(address)");
  asm_op(a, OP_ADDRESS);
  asm_store(a, MEM_CALL + 4);
  asm_push(a, WORD);
  asm_op(a, OP_PUSH0);
  asm_push(a, 4 + WORD);
  asm_push(a, MEM_CALL);
  carrier(a);
  asm_op(a, OP_GAS);
  asm_op(a, OP_STATICCALL);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_push(a, WORD - 1);
  asm_op(a, OP_RETURNDATASIZE);
  asm_op(a, OP_GT);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_MLOAD);
}

/* (nothing) -> (nothing): a CALL of the carrier, no value, with the selector
 * and WORDS words at MEM_CALL. The SafeERC20 rule (MY CALL 148): the entry
 * reverts unless the call succeeds and the return data is empty, or is 32
 * bytes or more with the first word 1. The two conditions are exclusive, so
 * ADD gives their OR. */
static void token_call(Asm *a, unsigned words) {
  asm_push(a, WORD);
  asm_op(a, OP_PUSH0);
  asm_push(a, 4 + WORD * words);
  asm_push(a, MEM_CALL);
  asm_op(a, OP_PUSH0);
  carrier(a);
  asm_op(a, OP_GAS);
  asm_op(a, OP_CALL);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_op(a, OP_RETURNDATASIZE);
  asm_op(a, OP_ISZERO);
  asm_push(a, WORD - 1);
  asm_op(a, OP_RETURNDATASIZE);
  asm_op(a, OP_GT);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_MLOAD);
  asm_push(a, 1);
  asm_op(a, OP_EQ);
  asm_op(a, OP_AND);
  asm_op(a, OP_ADD);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
}

/* (nothing) -> (nothing): the pull of a (word 1) for deposit in token mode
 * (MY CALL 147). b0 = balanceOf(this); transferFrom(CALLER, this, a) by
 * token_call; b1 = balanceOf(this). The entry reverts unless b1 = b0 + a
 * exactly (b0 + a is checked). Thus a fee-on-transfer carrier reverts, and
 * so does a call back into deposit or withdraw that changes the balance. */
static void carrier_pull(Asm *a) {
  carrier_balance(a);
  asm_argument(a, 1);
  asm_checked_add(a);
  carrier_selector(a, "transferFrom(address,address,uint256)");
  asm_op(a, OP_CALLER);
  asm_store(a, MEM_CALL + 4);
  asm_op(a, OP_ADDRESS);
  asm_store(a, MEM_CALL + 4 + WORD);
  asm_argument(a, 1);
  asm_store(a, MEM_CALL + 4 + 2 * WORD);
  token_call(a, 3);
  carrier_balance(a);
  asm_op(a, OP_EQ);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
}

/* (nothing) -> the deposit amount: CALLVALUE (wei), or a, word 1 (token). */
static void deposit_amount(Asm *a, LangAsset asset) {
  switch (asset) {
    case LANG_ASSET_WEI:
      asm_op(a, OP_CALLVALUE);
      return;
    case LANG_ASSET_TOKEN:
      asm_argument(a, 1);
      return;
  }
}

/* The body of deposit (wei) and deposit_token: in token mode, first the
 * pull of a; then RESERVE[kind] += the amount (checked); returns the
 * reserve. One body for the two modes. */
static void deposit_body(Asm *a, LangAsset asset) {
  kind_guard(a, 0);
  if (asset == LANG_ASSET_TOKEN)
    carrier_pull(a);
  asm_argument(a, 0);
  asm_slot(a, SLOT_RESERVE);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  deposit_amount(a, asset);
  asm_checked_add(a);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SWAP2);
  asm_op(a, OP_SSTORE);
  asm_return_top(a);
}

/* deposit kind: RESERVE[kind] += CALLVALUE (checked); returns the reserve. */
static void deposit(Asm *a, const EntryContext *c) {
  (void)c;
  deposit_body(a, LANG_ASSET_WEI);
}

/* deposit kind a (token mode, O5b; not payable): the pull of a, then
 * RESERVE[kind] += a (checked); returns the reserve. */
static void deposit_token(Asm *a, const EntryContext *c) {
  (void)c;
  deposit_body(a, LANG_ASSET_TOKEN);
}

/* distribute kind: S = 0 reverts; d = W[CHARTER][kind] ? RESERVE[kind] : 0
 * (the W word is 1 or 0, so d = W * RESERVE); RESERVE[kind] -= d; INDEX += d
 * (checked); returns d. */
static void distribute(Asm *a, const EntryContext *c) {
  supply_guard(a, c);
  kind_guard(a, 0);
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SLOAD);
  asm_push(a, 1);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
  asm_push(a, LANG_KINDS);
  asm_op(a, OP_MUL);
  asm_argument(a, 0);
  asm_op(a, OP_ADD);
  asm_push(a, data_rows(c) * LIMITS);
  asm_op(a, OP_ADD);
  data_word(a);
  asm_argument(a, 0);
  asm_slot(a, SLOT_RESERVE);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_MUL);
  asm_argument(a, 0);
  asm_slot(a, SLOT_RESERVE);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_SUB);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  asm_op(a, OP_DUP1);
  asm_push(a, SLOT_INDEX);
  asm_op(a, OP_SLOAD);
  asm_checked_add(a);
  asm_push(a, SLOT_INDEX);
  asm_op(a, OP_SSTORE);
  asm_return_top(a);
}

/* h moved paid -> (nothing): LOG3 Paid(h, CALLER, paid, moved), the event
 * of withdraw (L6), with paid at memory 0 and moved at memory 32. topic0 is
 * keccak256("Paid(uint256,address,uint256,uint256)"). It is above withdraw,
 * because C must have the definition before the call. */
static void paid_log(Asm *a) {
  static const char event[] = "Paid(uint256,address,uint256,uint256)";
  unsigned char topic[WORD];
  lang_keccak256((const unsigned char *)event, sizeof event - 1, topic);
  asm_store(a, 0);
  asm_store(a, WORD);
  asm_op(a, OP_CALLER);
  asm_op(a, OP_SWAP1);
  asm_push_word(a, topic);
  asm_push(a, 2 * WORD);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_LOG3);
}

/* (nothing) -> (nothing): pays paid (memory MEM_ID) to CALLER. Wei: a CALL
 * of paid wei, which reverts the entry when it fails. Token (MY CALL 149):
 * when paid is 1 or more, transfer(CALLER, paid) by token_call; at paid = 0
 * there is no carrier call. */
static void pay(Asm *a, LangAsset asset) {
  switch (asset) {
    case LANG_ASSET_WEI:
      asm_op(a, OP_PUSH0);
      asm_op(a, OP_PUSH0);
      asm_op(a, OP_PUSH0);
      asm_op(a, OP_PUSH0);
      asm_load(a, MEM_ID);
      asm_op(a, OP_CALLER);
      asm_op(a, OP_GAS);
      asm_op(a, OP_CALL);
      asm_op(a, OP_ISZERO);
      asm_revert_if(a);
      return;
    case LANG_ASSET_TOKEN: {
      Label done = asm_label(a);
      asm_load(a, MEM_ID);
      asm_op(a, OP_ISZERO);
      asm_jump_if(a, done);
      carrier_selector(a, "transfer(address,uint256)");
      asm_op(a, OP_CALLER);
      asm_store(a, MEM_CALL + 4);
      asm_load(a, MEM_ID);
      asm_store(a, MEM_CALL + 4 + WORD);
      token_call(a, 2);
      asm_jumpdest(a, done);
      return;
    }
  }
}

/* withdraw: S = 0 reverts; h = id(CALLER); settle h; paid = NUM[h] / S and
 * r = NUM[h] mod S. When paid is 1 wei or more, moved = r, else moved = 0
 * (L6). NUM[h] := r - moved; x = DUST + moved; RESERVE[0] += x / S
 * (checked, a wrap reverts); DUST := x mod S; LOG3 Paid(h, CALLER, paid,
 * moved). Then (all stores done) pay gives paid to CALLER, a failed payment
 * reverts the entry; returns paid. One body for withdraw and withdraw_token. */
static void withdraw_body(Asm *a, const EntryContext *c, LangAsset asset) {
  supply_guard(a, c);
  caller_identity(a);
  asm_op(a, OP_DUP1);
  settle(a);
  asm_op(a, OP_DUP1);
  asm_slot(a, SLOT_NUM);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  push_supply(a, c);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_MOD);
  asm_op(a, OP_SWAP1);
  push_supply(a, c);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_DIV);
  /* h slot r paid -> h moved paid, moved = r * (paid != 0), NUM[h] := r - moved */
  asm_op(a, OP_DUP1);
  asm_op(a, OP_ISZERO);
  asm_op(a, OP_ISZERO);
  asm_op(a, OP_DUP3);
  asm_op(a, OP_MUL);
  asm_op(a, OP_SWAP2);
  asm_op(a, OP_DUP3);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
  asm_op(a, OP_DUP4);
  asm_op(a, OP_SSTORE);
  asm_op(a, OP_SWAP2);
  asm_op(a, OP_POP);
  asm_op(a, OP_SWAP1);
  /* x = DUST + moved; RESERVE[0] += x / S (checked); DUST := x mod S */
  asm_op(a, OP_DUP2);
  asm_push(a, SLOT_DUST);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_ADD);
  asm_op(a, OP_PUSH0);
  asm_slot(a, SLOT_RESERVE);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  push_supply(a, c);
  asm_op(a, OP_DUP4);
  asm_op(a, OP_DIV);
  asm_checked_add(a);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  push_supply(a, c);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_MOD);
  asm_push(a, SLOT_DUST);
  asm_op(a, OP_SSTORE);
  asm_op(a, OP_DUP1);
  asm_store(a, MEM_ID);
  paid_log(a);
  pay(a, asset);
  asm_load(a, MEM_ID);
  asm_return_top(a);
}

/* withdraw (wei): the CALL of paid wei. */
static void withdraw(Asm *a, const EntryContext *c) {
  withdraw_body(a, c, LANG_ASSET_WEI);
}

/* withdraw (token mode, O5b): transfer(CALLER, paid) when paid >= 1. */
static void withdraw_token(Asm *a, const EntryContext *c) {
  withdraw_body(a, c, LANG_ASSET_TOKEN);
}

/* second first value -> (nothing): LOG3 EVENT(first, second, value), an
 * ERC-20 event with two indexed identity words, with the value word at
 * memory 0. topic0 is keccak256(EVENT). */
static void erc20_log(Asm *a, const char *event) {
  unsigned char topic[WORD];
  lang_keccak256((const unsigned char *)event, strlen(event), topic);
  asm_store(a, 0);
  asm_push_word(a, topic);
  asm_push(a, WORD);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_LOG3);
}

/* to from value -> (nothing): LOG3 Transfer(from, to, value), the ERC-20
 * event (O5a). */
static void transfer_log(Asm *a) {
  erc20_log(a, "Transfer(address,address,uint256)");
}

/* spender owner value -> (nothing): LOG3 Approval(owner, spender, value),
 * the ERC-20 event of approve (O5c, MY CALL 167). */
static void approval_log(Asm *a) {
  erc20_log(a, "Approval(address,address,uint256)");
}

/* spender owner -> the slot of ALLOWANCE[owner][spender]:
 * keccak256(spender . keccak256(owner . SLOT_ALLOWANCE)), the Solidity
 * nested mapping (MY CALL 168). */
static void allowance_slot(Asm *a) {
  asm_slot(a, SLOT_ALLOWANCE);
  asm_push(a, 0x20);
  asm_op(a, OP_MSTORE);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_MSTORE);
  asm_push(a, 0x40);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_SHA3);
}

/* Reverts when calldata word TO is 0 or TO + 1 overflows. Every destination
 * must admit the registry encoding identity + 1, and identity 0 is no
 * identity (MY CALL 150 (b)). */
static void destination_guard(Asm *a, unsigned to) {
  asm_argument(a, to);
  asm_push(a, 1);
  asm_checked_add(a);
  asm_op(a, OP_POP);
  asm_argument(a, to);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
}

/* R: reverts when word Q >= R[CHARTER][PROFILE h][PROFILE to], with h at
 * memory MEM_ID and to the calldata word TO. */
static void restriction_guard(Asm *a, unsigned to, unsigned q) {
  asm_argument(a, to);
  asm_slot(a, SLOT_PROFILE);
  asm_op(a, OP_SLOAD);
  asm_load(a, MEM_ID);
  asm_slot(a, SLOT_PROFILE);
  asm_op(a, OP_SLOAD);
  asm_push(a, LANG_PROFILES);
  asm_op(a, OP_MUL);
  asm_op(a, OP_ADD);
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SLOAD);
  asm_push(a, 1);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
  asm_push(a, LIMITS);
  asm_op(a, OP_MUL);
  asm_op(a, OP_ADD);
  data_word(a);
  asm_argument(a, q);
  asm_op(a, OP_LT);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
}

/* code q -> (nothing): WEIGHT[code] := WEIGHT[code] + q (OP_ADD) or
 * WEIGHT[code] - q (OP_SUB); no term when code = 0 (no ballot). */
static void weight_term(Asm *a, Op op) {
  Label skip = asm_label(a);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_ISZERO);
  asm_jump_if(a, skip);
  asm_op(a, OP_DUP2);
  asm_slot(a, SLOT_WEIGHT);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_SLOAD);
  asm_op(a, op);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  asm_op(a, OP_DUP1);
  asm_jumpdest(a, skip);
  asm_op(a, OP_POP);
  asm_op(a, OP_POP);
}

/* from to q -> (nothing) (O2, MY CALL 183 (a)): the vote moves with the
 * mass: WEIGHT[BALLOT[from]] -= q, then WEIGHT[BALLOT[to]] += q (code 0:
 * no term). The caller checks q <= MU[from] first; WEIGHT[BALLOT[from]]
 * includes MU[from], so the debit does not wrap. */
static void move_vote(Asm *a) {
  asm_op(a, OP_DUP3);
  asm_slot(a, SLOT_BALLOT);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_DUP2);
  weight_term(a, OP_SUB);
  asm_op(a, OP_SWAP1);
  asm_slot(a, SLOT_BALLOT);
  asm_op(a, OP_SLOAD);
  asm_op(a, OP_SWAP1);
  weight_term(a, OP_ADD);
  asm_op(a, OP_POP);
}

/* The move of transfer, with h at memory MEM_ID, to the calldata word TO
 * and q the word Q: q > MU[h] reverts; settle h, then settle to (SPEC 5:
 * checkpoints both identities); the vote of q moves from h to to (O2);
 * MU[h] -= q, then MU[to] += q (read after the debit, so to = h keeps the
 * mass); logs Transfer(h, to, q), also at q = 0 and at to = h (O5a);
 * returns 1. */
static void move(Asm *a, unsigned to, unsigned q) {
  asm_load(a, MEM_ID);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_SLOAD);
  asm_argument(a, q);
  asm_op(a, OP_GT);
  asm_revert_if(a);
  asm_load(a, MEM_ID);
  settle(a);
  asm_argument(a, to);
  settle(a);
  asm_load(a, MEM_ID);
  asm_argument(a, to);
  asm_argument(a, q);
  move_vote(a);
  asm_load(a, MEM_ID);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_argument(a, q);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  asm_argument(a, to);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_argument(a, q);
  asm_checked_add(a);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  asm_argument(a, to);
  asm_load(a, MEM_ID);
  asm_argument(a, q);
  transfer_log(a);
  asm_push(a, 1);
  asm_return_top(a);
}

/* transfer to q: h = id(CALLER); q >= R[CHARTER][PROFILE h][PROFILE to]
 * reverts; to = 0, q > MU[h] or to + 1 overflow reverts; then the move of
 * q from h to to; returns 1. */
static void transfer(Asm *a, const EntryContext *c) {
  (void)c;
  destination_guard(a, 0);
  caller_identity(a);
  asm_store(a, MEM_ID);
  restriction_guard(a, 0, 1);
  move(a, 0, 1);
}

/* transfer(address to, uint256 q) of the ERC-20 facade (O5c, MY CALL 164):
 * to >= 2^160 reverts, then the transfer body; returns 1. */
static void transfer_erc20(Asm *a, const EntryContext *c) {
  asm_address_guard(a, 0);
  transfer(a, c);
}

/* approve(address spender, uint256 v) (O5c, MY CALLs 165 and 167):
 * spender >= 2^160 or spender = 0 reverts; owner = id(CALLER);
 * ALLOWANCE[owner][spender] := v; logs Approval(owner, spender, v), also at
 * v = 0 and at an unchanged v; returns 1. */
static void approve(Asm *a, const EntryContext *c) {
  (void)c;
  asm_address_guard(a, 0);
  asm_argument(a, 0);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  caller_identity(a);
  asm_store(a, MEM_ID);
  asm_argument(a, 1);
  asm_argument(a, 0);
  asm_load(a, MEM_ID);
  allowance_slot(a);
  asm_op(a, OP_SSTORE);
  asm_argument(a, 0);
  asm_load(a, MEM_ID);
  asm_argument(a, 1);
  approval_log(a);
  asm_push(a, 1);
  asm_return_top(a);
}

/* allowance(address owner, address spender) (O5c): owner >= 2^160 or
 * spender >= 2^160 reverts; ALLOWANCE[owner][spender]. */
static void allowance(Asm *a, const EntryContext *c) {
  (void)c;
  asm_address_guard(a, 0);
  asm_address_guard(a, 1);
  asm_argument(a, 1);
  asm_argument(a, 0);
  allowance_slot(a);
  asm_op(a, OP_SLOAD);
  asm_return_top(a);
}

/* transferFrom(address from, address to, uint256 q) (O5c, MY CALLs 165 to
 * 167): from >= 2^160, to >= 2^160, from = 0 or to = 0 reverts; spender =
 * id(CALLER); q > ALLOWANCE[from][spender] reverts, else ALLOWANCE -= q (no
 * max rule, and from = spender needs an allowance too); then R from the
 * profile of from to the profile of to, and the move of q from from to to;
 * logs Transfer(from, to, q) and no Approval; returns 1. A later revert
 * undoes the allowance store. */
static void transfer_from(Asm *a, const EntryContext *c) {
  (void)c;
  asm_address_guard(a, 0);
  asm_address_guard(a, 1);
  asm_argument(a, 0);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  destination_guard(a, 1);
  asm_argument(a, 0);
  asm_store(a, MEM_ID);
  caller_identity(a);
  asm_load(a, MEM_ID);
  allowance_slot(a);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_argument(a, 2);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_DUP2);
  asm_op(a, OP_GT);
  asm_revert_if(a);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  restriction_guard(a, 1, 2);
  move(a, 1, 2);
}

/* Reverts unless (CHARTER, id(CALLER)) is an issuer pair (an unrolled
 * compare chain over the pairs of the program). */
static void issuer_guard(Asm *a, const EntryContext *c) {
  Label issuer = asm_label(a);
  caller_identity(a);
  asm_store(a, MEM_ID);
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SLOAD);
  asm_store(a, MEM_CHARTER);
  size_t pairs = c->data == NULL ? 0 : c->data->issuers;
  for (size_t i = 0; i < pairs; i++) {
    unsigned char word[WORD];
    asm_load(a, MEM_CHARTER);
    asm_push(a, c->data->issuer[i].charter);
    asm_op(a, OP_EQ);
    asm_load(a, MEM_ID);
    word_of(word, c->data->issuer[i].identity, 0);
    asm_push_word(a, word);
    asm_op(a, OP_EQ);
    asm_op(a, OP_AND);
    asm_jump_if(a, issuer);
  }
  asm_jump(a, LABEL_REVERT);
  asm_jumpdest(a, issuer);
}

/* attest w h p: the issuer guard; w >= 2^160, h = 0 or p > 3 reverts;
 * REGISTRY[w] := h + 1 (checked); PROFILE[h] := p; returns 1. */
static void attest(Asm *a, const EntryContext *c) {
  issuer_guard(a, c);
  asm_address_guard(a, 0);
  asm_argument(a, 1);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_push(a, LANG_PROFILES - 1);
  asm_argument(a, 2);
  asm_op(a, OP_GT);
  asm_revert_if(a);
  asm_argument(a, 1);
  asm_push(a, 1);
  asm_checked_add(a);
  asm_argument(a, 0);
  asm_slot(a, SLOT_REGISTRY);
  asm_op(a, OP_SSTORE);
  asm_argument(a, 2);
  asm_argument(a, 1);
  asm_slot(a, SLOT_PROFILE);
  asm_op(a, OP_SSTORE);
  asm_push(a, 1);
  asm_return_top(a);
}

/* recover from to q (ERC-1644 forced transfer, O3): the issuer guard;
 * from = 0, to = 0, from + 1 or to + 1 overflow reverts; q > MU[from]
 * reverts; R does not
 * gate it; settle from, then settle to; the vote of q moves from from to
 * to (O2); MU[from] -= q, then MU[to] += q (read after the debit, so
 * to = from keeps the mass); logs Transfer(from, to, q) (O5a); returns 1. */
static void recover(Asm *a, const EntryContext *c) {
  issuer_guard(a, c);
  for (unsigned j = 0; j < 2; j++) {
    asm_argument(a, j);
    asm_push(a, 1);
    asm_checked_add(a);
    asm_op(a, OP_POP);
    asm_argument(a, j);
    asm_op(a, OP_ISZERO);
    asm_revert_if(a);
  }
  asm_argument(a, 0);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_SLOAD);
  asm_argument(a, 2);
  asm_op(a, OP_GT);
  asm_revert_if(a);
  asm_argument(a, 0);
  settle(a);
  asm_argument(a, 1);
  settle(a);
  asm_argument(a, 0);
  asm_argument(a, 1);
  asm_argument(a, 2);
  move_vote(a);
  asm_argument(a, 0);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_argument(a, 2);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SUB);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  asm_argument(a, 1);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_SLOAD);
  asm_argument(a, 2);
  asm_checked_add(a);
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_SSTORE);
  asm_argument(a, 1);
  asm_argument(a, 0);
  asm_argument(a, 2);
  transfer_log(a);
  asm_push(a, 1);
  asm_return_top(a);
}

/* vote c (O2, MY CALLs 181 to 183): h = id(CALLER); c > k reverts; the
 * mass MU[h] moves in WEIGHT from the old ballot BALLOT[h] to c (code 0:
 * no term, so vote(0) withdraws the ballot); BALLOT[h] := c; returns c. */
static void vote(Asm *a, const EntryContext *c) {
  caller_identity(a);
  asm_store(a, MEM_ID);
  asm_push(a, c->decisions);
  asm_argument(a, 0);
  asm_op(a, OP_GT);
  asm_revert_if(a);
  asm_load(a, MEM_ID);
  asm_slot(a, SLOT_BALLOT);
  asm_op(a, OP_SLOAD);
  asm_load(a, MEM_ID);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_SLOAD);
  weight_term(a, OP_SUB);
  asm_argument(a, 0);
  asm_load(a, MEM_ID);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_SLOAD);
  weight_term(a, OP_ADD);
  asm_argument(a, 0);
  asm_load(a, MEM_ID);
  asm_slot(a, SLOT_BALLOT);
  asm_op(a, OP_SSTORE);
  asm_argument(a, 0);
  asm_return_top(a);
}

/* The amend scratch: the remainder of code j at MEM_SEAT + 32 (j - 1), its
 * floor seats at MEM_SEAT + 32 (k + j - 1). */
static unsigned remainder_at(unsigned j) { return MEM_SEAT + WORD * (j - 1); }

static unsigned floor_at(unsigned k, unsigned j) { return MEM_SEAT + WORD * (k + j - 1); }

/* amend() (O2, MY CALLs 184 and 185): the n = members seats of the
 * weighted tally w = WEIGHT[1 .. k], W = the sum, by the largest remainder
 * (Hamilton): code j gets floor(n w_j / W) seats, and the seats left go one
 * each to the codes with the largest n w_j mod W, ties to the lower code
 * (code j gets one when fewer than the seats left rank above it). W = 0
 * reverts. The seat tally sums to n, so its table index (table_index:
 * the sum of seats_j (n + 1)^(j - 1) over j < k) reads the verdict table
 * of KL1 (asm_verdict). CHARTER := the code; returns it. n w_j < 2^75 (the
 * KL4 range), so plain MUL. The core amend (the packed table word) is not
 * in the interest lists. */
static void amend(Asm *a, const EntryContext *c) {
  unsigned n = c->members;
  unsigned k = c->decisions;
  asm_op(a, OP_PUSH0);
  for (unsigned j = 1; j <= k; j++) {
    asm_push(a, j);
    asm_slot(a, SLOT_WEIGHT);
    asm_op(a, OP_SLOAD);
    asm_op(a, OP_DUP1);
    asm_store(a, remainder_at(j));
    asm_op(a, OP_ADD);
  }
  asm_op(a, OP_DUP1);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_push(a, n);
  for (unsigned j = 1; j <= k; j++) {
    asm_load(a, remainder_at(j));
    asm_push(a, n);
    asm_op(a, OP_MUL);
    asm_op(a, OP_DUP3);
    asm_op(a, OP_DUP2);
    asm_op(a, OP_MOD);
    asm_store(a, remainder_at(j));
    asm_op(a, OP_DUP3);
    asm_op(a, OP_SWAP1);
    asm_op(a, OP_DIV);
    asm_op(a, OP_DUP1);
    asm_store(a, floor_at(k, j));
    asm_op(a, OP_SWAP1);
    asm_op(a, OP_SUB);
  }
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_POP);
  asm_op(a, OP_PUSH0);
  unsigned long weight = 1;
  for (unsigned j = 1; j < k; j++, weight *= n + 1ul) {
    asm_op(a, OP_PUSH0);
    for (unsigned i = 1; i <= k; i++) {
      if (i != j) {
        asm_load(a, remainder_at(j));
        asm_load(a, remainder_at(i));
        asm_op(a, i < j ? OP_LT : OP_GT);
        if (i < j)
          asm_op(a, OP_ISZERO);
        asm_op(a, OP_ADD);
      }
    }
    asm_op(a, OP_DUP3);
    asm_op(a, OP_SWAP1);
    asm_op(a, OP_LT);
    asm_load(a, floor_at(k, j));
    asm_op(a, OP_ADD);
    if (j > 1) {
      asm_push(a, weight);
      asm_op(a, OP_MUL);
    }
    asm_op(a, OP_ADD);
  }
  asm_op(a, OP_SWAP1);
  asm_op(a, OP_POP);
  asm_verdict(a, n, k);
  asm_op(a, OP_DUP1);
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SSTORE);
  asm_return_top(a);
}

/* mass h, and balanceOf(address h) of the ERC-20 facade (O5a): MU[h]. */
static void mass(Asm *a, const EntryContext *c) {
  (void)c;
  asm_argument(a, 0);
  asm_slot(a, SLOT_MU);
  asm_op(a, OP_SLOAD);
  asm_return_top(a);
}

/* supply, and totalSupply() of the ERC-20 facade (O5a): S, the code constant. */
static void supply(Asm *a, const EntryContext *c) {
  push_supply(a, c);
  asm_return_top(a);
}

/* claimOf h: NUM[h] + MU[h] * (INDEX - CHECKPOINT[h]), in units of 1/S wei. */
static void claim(Asm *a, const EntryContext *c) {
  (void)c;
  asm_argument(a, 0);
  claim_of(a);
  asm_return_top(a);
}

/* charter: CHARTER. */
static void charter(Asm *a, const EntryContext *c) {
  (void)c;
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SLOAD);
  asm_return_top(a);
}

/* reserve kind: RESERVE[kind]; a kind code above 1 reverts. */
static void reserve(Asm *a, const EntryContext *c) {
  (void)c;
  kind_guard(a, 0);
  asm_argument(a, 0);
  asm_slot(a, SLOT_RESERVE);
  asm_op(a, OP_SLOAD);
  asm_return_top(a);
}

/* selfConstituting (MY CALL 14): 1 at Debreu, 0 at impossibility. */
static void self_debreu(Asm *a, const EntryContext *c) {
  (void)c;
  asm_push(a, 1);
  asm_return_top(a);
}

static void self_impossibility(Asm *a, const EntryContext *c) {
  (void)c;
  asm_op(a, OP_PUSH0);
  asm_return_top(a);
}

/* The ABI string TEXT as the return data (O5c, MY CALL 169): the offset
 * 0x20 at memory 0, the length at 0x20, the bytes left-aligned at 0x40,
 * then RETURN 0 0x40 for empty text, else 0 0x60. TEXT has at most 32
 * bytes, so one word holds them. */
static void return_text(Asm *a, const LangText *text) {
  unsigned char word[32] = {0};
  memcpy(word, text->byte, text->length);
  asm_push(a, 0x20);
  asm_push(a, 0);
  asm_op(a, OP_MSTORE);
  asm_push(a, text->length);
  asm_push(a, 0x20);
  asm_op(a, OP_MSTORE);
  asm_push_word(a, word);
  asm_push(a, 0x40);
  asm_op(a, OP_MSTORE);
  asm_push(a, text->length == 0 ? 0x40 : 0x60);
  asm_push(a, 0);
  asm_op(a, OP_RETURN);
}

static const LangText default_name = {sizeof LANG_NAME_DEFAULT - 1, LANG_NAME_DEFAULT};
static const LangText default_symbol = {sizeof LANG_SYMBOL_DEFAULT - 1, LANG_SYMBOL_DEFAULT};

/* name() (O5c, MY CALL 169): the def name of the program, else "interest". */
static void erc20_name(Asm *a, const EntryContext *c) {
  return_text(a, c != NULL && c->data != NULL ? &c->data->name : &default_name);
}

/* symbol() (O5c, MY CALL 169): the def symbol of the program, else "INT". */
static void erc20_symbol(Asm *a, const EntryContext *c) {
  return_text(a, c != NULL && c->data != NULL ? &c->data->symbol : &default_symbol);
}

/* decimals() (O5c, MY CALL 169): 0, as the mass is whole units. */
static void erc20_decimals(Asm *a, const EntryContext *c) {
  (void)c;
  asm_push(a, 0);
  asm_return_top(a);
}

static const Entry impossibility[] = {
  {"deposit", 1, 0, ENTRY_PAYABLE, deposit, NULL},
  {"withdraw", 0, 0, ENTRY_NONPAYABLE, withdraw, NULL},
  {"attest", 3, 0, ENTRY_NONPAYABLE, attest, NULL},
  {"mass", 1, 0, ENTRY_NONPAYABLE, mass, NULL},
  {"supply", 0, 0, ENTRY_NONPAYABLE, supply, NULL},
  {"claimOf", 1, 0, ENTRY_NONPAYABLE, claim, NULL},
  {"charter", 0, 0, ENTRY_NONPAYABLE, charter, NULL},
  {"reserve", 1, 0, ENTRY_NONPAYABLE, reserve, NULL},
  {"selfConstituting", 0, 0, ENTRY_NONPAYABLE, self_impossibility, NULL},
  {"balanceOf", 1, 0, ENTRY_NONPAYABLE, mass, "address"},
  {"totalSupply", 0, 0, ENTRY_NONPAYABLE, supply, NULL},
  {"name", 0, 0, ENTRY_NONPAYABLE, erc20_name, NULL},
  {"symbol", 0, 0, ENTRY_NONPAYABLE, erc20_symbol, NULL},
  {"decimals", 0, 0, ENTRY_NONPAYABLE, erc20_decimals, NULL},
};

static const Entry debreu[] = {
  {"deposit", 1, 0, ENTRY_PAYABLE, deposit, NULL},
  {"distribute", 1, 0, ENTRY_NONPAYABLE, distribute, NULL},
  {"withdraw", 0, 0, ENTRY_NONPAYABLE, withdraw, NULL},
  {"transfer", 2, 0, ENTRY_NONPAYABLE, transfer, NULL},
  {"attest", 3, 0, ENTRY_NONPAYABLE, attest, NULL},
  {"recover", 3, 0, ENTRY_NONPAYABLE, recover, NULL},
  {"cast", 0, 1, ENTRY_NONPAYABLE, lang_entry_cast, NULL},
  {"amend", 0, 0, ENTRY_NONPAYABLE, amend, NULL},
  {"mass", 1, 0, ENTRY_NONPAYABLE, mass, NULL},
  {"supply", 0, 0, ENTRY_NONPAYABLE, supply, NULL},
  {"claimOf", 1, 0, ENTRY_NONPAYABLE, claim, NULL},
  {"charter", 0, 0, ENTRY_NONPAYABLE, charter, NULL},
  {"reserve", 1, 0, ENTRY_NONPAYABLE, reserve, NULL},
  {"selfConstituting", 0, 0, ENTRY_NONPAYABLE, self_debreu, NULL},
  {"balanceOf", 1, 0, ENTRY_NONPAYABLE, mass, "address"},
  {"totalSupply", 0, 0, ENTRY_NONPAYABLE, supply, NULL},
  {"transfer", 2, 0, ENTRY_NONPAYABLE, transfer_erc20, "address,uint256"},
  {"approve", 2, 0, ENTRY_NONPAYABLE, approve, "address,uint256"},
  {"allowance", 2, 0, ENTRY_NONPAYABLE, allowance, "address,address"},
  {"transferFrom", 3, 0, ENTRY_NONPAYABLE, transfer_from, "address,address,uint256"},
  {"name", 0, 0, ENTRY_NONPAYABLE, erc20_name, NULL},
  {"symbol", 0, 0, ENTRY_NONPAYABLE, erc20_symbol, NULL},
  {"decimals", 0, 0, ENTRY_NONPAYABLE, erc20_decimals, NULL},
  {"vote", 1, 0, ENTRY_NONPAYABLE, vote, NULL},
};

/* Token mode (O5b): the same rows, but deposit takes kind and a (not
 * payable) and withdraw pays by the carrier. */
static const Entry impossibility_token[] = {
  {"deposit", 2, 0, ENTRY_NONPAYABLE, deposit_token, NULL},
  {"withdraw", 0, 0, ENTRY_NONPAYABLE, withdraw_token, NULL},
  {"attest", 3, 0, ENTRY_NONPAYABLE, attest, NULL},
  {"mass", 1, 0, ENTRY_NONPAYABLE, mass, NULL},
  {"supply", 0, 0, ENTRY_NONPAYABLE, supply, NULL},
  {"claimOf", 1, 0, ENTRY_NONPAYABLE, claim, NULL},
  {"charter", 0, 0, ENTRY_NONPAYABLE, charter, NULL},
  {"reserve", 1, 0, ENTRY_NONPAYABLE, reserve, NULL},
  {"selfConstituting", 0, 0, ENTRY_NONPAYABLE, self_impossibility, NULL},
  {"balanceOf", 1, 0, ENTRY_NONPAYABLE, mass, "address"},
  {"totalSupply", 0, 0, ENTRY_NONPAYABLE, supply, NULL},
  {"name", 0, 0, ENTRY_NONPAYABLE, erc20_name, NULL},
  {"symbol", 0, 0, ENTRY_NONPAYABLE, erc20_symbol, NULL},
  {"decimals", 0, 0, ENTRY_NONPAYABLE, erc20_decimals, NULL},
};

static const Entry debreu_token[] = {
  {"deposit", 2, 0, ENTRY_NONPAYABLE, deposit_token, NULL},
  {"distribute", 1, 0, ENTRY_NONPAYABLE, distribute, NULL},
  {"withdraw", 0, 0, ENTRY_NONPAYABLE, withdraw_token, NULL},
  {"transfer", 2, 0, ENTRY_NONPAYABLE, transfer, NULL},
  {"attest", 3, 0, ENTRY_NONPAYABLE, attest, NULL},
  {"recover", 3, 0, ENTRY_NONPAYABLE, recover, NULL},
  {"cast", 0, 1, ENTRY_NONPAYABLE, lang_entry_cast, NULL},
  {"amend", 0, 0, ENTRY_NONPAYABLE, amend, NULL},
  {"mass", 1, 0, ENTRY_NONPAYABLE, mass, NULL},
  {"supply", 0, 0, ENTRY_NONPAYABLE, supply, NULL},
  {"claimOf", 1, 0, ENTRY_NONPAYABLE, claim, NULL},
  {"charter", 0, 0, ENTRY_NONPAYABLE, charter, NULL},
  {"reserve", 1, 0, ENTRY_NONPAYABLE, reserve, NULL},
  {"selfConstituting", 0, 0, ENTRY_NONPAYABLE, self_debreu, NULL},
  {"balanceOf", 1, 0, ENTRY_NONPAYABLE, mass, "address"},
  {"totalSupply", 0, 0, ENTRY_NONPAYABLE, supply, NULL},
  {"transfer", 2, 0, ENTRY_NONPAYABLE, transfer_erc20, "address,uint256"},
  {"approve", 2, 0, ENTRY_NONPAYABLE, approve, "address,uint256"},
  {"allowance", 2, 0, ENTRY_NONPAYABLE, allowance, "address,address"},
  {"transferFrom", 3, 0, ENTRY_NONPAYABLE, transfer_from, "address,address,uint256"},
  {"name", 0, 0, ENTRY_NONPAYABLE, erc20_name, NULL},
  {"symbol", 0, 0, ENTRY_NONPAYABLE, erc20_symbol, NULL},
  {"decimals", 0, 0, ENTRY_NONPAYABLE, erc20_decimals, NULL},
  {"vote", 1, 0, ENTRY_NONPAYABLE, vote, NULL},
};

/* The table of REGIME: the token table when the program data selects the
 * token asset (O5b), else the wei table. */
const Entry *lang_domain_entries(LangRegime regime, const EntryContext *c, size_t *count) {
  int token = c != NULL && c->data != NULL && c->data->asset == LANG_ASSET_TOKEN;
  switch (regime) {
    case LANG_REGIME_IMPOSSIBILITY:
      *count = token ? sizeof impossibility_token / sizeof impossibility_token[0]
                     : sizeof impossibility / sizeof impossibility[0];
      return token ? impossibility_token : impossibility;
    case LANG_REGIME_DEBREU:
      *count = token ? sizeof debreu_token / sizeof debreu_token[0] : sizeof debreu / sizeof debreu[0];
      return token ? debreu_token : debreu;
  }
  *count = 0;
  return NULL;
}

/* key value -> the storage word: SSTORE of a constant value at the slot of
 * a constant key in mapping BASE. */
static void store_entry(Asm *a, unsigned base, unsigned long long key, unsigned long long value,
                        unsigned extra) {
  unsigned char word[WORD];
  word_of(word, value, extra);
  asm_push_word(a, word);
  word_of(word, key, 0);
  asm_push_word(a, word);
  asm_slot(a, base);
  asm_op(a, OP_SSTORE);
}

/* The genesis event of identity h with units: Transfer(0, h, units) (O5a). */
static void genesis_log(Asm *a, unsigned long long identity, unsigned long long units) {
  unsigned char word[WORD];
  word_of(word, identity, 0);
  asm_push_word(a, word);
  asm_op(a, OP_PUSH0);
  word_of(word, units, 0);
  asm_push_word(a, word);
  transfer_log(a);
}

/* Token mode (MY CALL 146): the constructor word, the last 32 bytes of the
 * init code, is the carrier address. A missing or extra word, the word 0, a
 * bit above bit 159, or an address with no code reverts the creation;
 * CARRIER := the word. */
static void carrier_genesis(Asm *a) {
  asm_push(a, WORD);
  asm_push_label(a, LABEL_END);
  asm_op(a, OP_ADD);
  asm_op(a, OP_CODESIZE);
  asm_op(a, OP_EQ);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_push(a, WORD);
  asm_push_label(a, LABEL_END);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_CODECOPY);
  asm_op(a, OP_PUSH0);
  asm_op(a, OP_MLOAD);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_op(a, OP_DUP1);
  asm_push(a, 160);
  asm_op(a, OP_SHR);
  asm_revert_if(a);
  asm_op(a, OP_DUP1);
  asm_op(a, OP_EXTCODESIZE);
  asm_op(a, OP_ISZERO);
  asm_revert_if(a);
  asm_push(a, SLOT_CARRIER);
  asm_op(a, OP_SSTORE);
}

/* The genesis writes: in token mode first CARRIER := the constructor word
 * (carrier_genesis); CHARTER := start; per row REGISTRY[w] := h + 1; per
 * distinct identity h, MU[h] := the sum of its units and PROFILE[h] := the
 * profile of its last row; each MU write logs Transfer(0, h, MU[h]) (O5a).
 * Zero words are not written. */
void lang_domain_genesis(Asm *a, const LangContract *contract) {
  const LangDomainData *data = contract->data;
  if (data != NULL && data->asset == LANG_ASSET_TOKEN)
    carrier_genesis(a);
  asm_push(a, data == NULL ? 1u : data->start);
  asm_push(a, SLOT_CHARTER);
  asm_op(a, OP_SSTORE);
  size_t rows = data == NULL ? 0 : data->holders;
  for (size_t i = 0; i < rows; i++) {
    const LangHolder *row = &data->holder[i];
    store_entry(a, SLOT_REGISTRY, row->wallet, row->identity, 1);
    int seen = 0;
    for (size_t j = 0; j < i; j++)
      seen = seen || data->holder[j].identity == row->identity;
    if (seen)
      continue;
    unsigned long long units = 0;
    unsigned profile = 0;
    for (size_t j = i; j < rows; j++) {
      int same = data->holder[j].identity == row->identity;
      units += same ? data->holder[j].units : 0;
      profile = same ? data->holder[j].profile : profile;
    }
    if (units != 0) {
      store_entry(a, SLOT_MU, row->identity, units, 0);
      genesis_log(a, row->identity, units);
    }
    if (profile != 0)
      store_entry(a, SLOT_PROFILE, row->identity, profile, 0);
  }
}

/* The code data at LABEL_DATA: the R limit words, then the W gate words. */
void lang_domain_data(Asm *a, const EntryContext *c) {
  unsigned rows = data_rows(c);
  unsigned char word[WORD];
  for (unsigned r = 0; r < rows; r++)
    for (unsigned p = 0; p < LANG_PROFILES; p++)
      for (unsigned q = 0; q < LANG_PROFILES; q++) {
        LangCap deny = {LANG_CAP_DENY, 0};
        limit_word(word, c->data == NULL ? deny : c->data->cap[0][r][p][q]);
        put_word(a, word);
      }
  for (unsigned r = 0; r < rows; r++)
    for (unsigned k = 0; k < LANG_KINDS; k++) {
      word_of(word, c->data != NULL && c->data->pass[r][k] ? 1u : 0u, 0);
      put_word(a, word);
    }
}

/* The program data of the program (lang_data). This domain always gives
 * the data, never NULL. */
int lang_domain_read(LangChecked *checked, const LangDomainData **data) {
  static LangDomainData program;
  int status = lang_data(checked, &program);
  if (status != LANG_EXIT_OK)
    return status;
  *data = &program;
  return LANG_EXIT_OK;
}

static const char *cap_text(LangCap cap, char *buf, size_t size) {
  switch (cap.tag) {
  case LANG_CAP_DENY: return "d";
  case LANG_CAP_ANY: return "a";
  case LANG_CAP_UP_TO: snprintf(buf, size, "u%llu", cap.n); return buf;
  }
  return "?";
}

/* The `data` verb: start, charters, genesis rows w:h:p:u, restrict (one
 * group of 16 caps per charter, profile pairs row-major), waterfall (one
 * group per charter: p pass, r retain, for rent then sale), issuers c:h,
 * name and symbol (O5c), then `asset token` in token mode only (no line
 * for wei, MY CALL 155).
 * DATA is not NULL (lang_domain_read). */
void lang_domain_print(const LangDomainData *data, FILE *out) {
  fprintf(out, "start %u\ncharters %u\ngenesis", data->start, data->charters);
  for (size_t i = 0; i < data->holders; i++)
    fprintf(out, " %llu:%llu:%u:%llu:%u", data->holder[i].wallet, data->holder[i].identity,
            data->holder[i].profile, data->holder[i].units, data->holder[i].partition);
  char buf[32];
  for (unsigned a = 0; a < LANG_PARTITIONS; a++) {
    fprintf(out, "\nrestrict %u", a + 1);
    for (unsigned i = 0; i < data->charters; i++)
      for (unsigned p = 0; p < LANG_PROFILES; p++)
        for (unsigned r = 0; r < LANG_PROFILES; r++)
          fprintf(out, "%s%s", p == 0 && r == 0 ? " " : ",", cap_text(data->cap[a][i][p][r], buf, sizeof buf));
  }
  fputs("\nwaterfall", out);
  for (unsigned i = 0; i < data->charters; i++)
    fprintf(out, " %c%c", data->pass[i][0] ? 'p' : 'r', data->pass[i][1] ? 'p' : 'r');
  fputs("\nissuers", out);
  for (size_t i = 0; i < data->issuers; i++)
    fprintf(out, " %u:%llu", data->issuer[i].charter, data->issuer[i].identity);
  fprintf(out, "\nname %.*s\nsymbol %.*s\n", (int)data->name.length, (const char *)data->name.byte,
          (int)data->symbol.length, (const char *)data->symbol.byte);
  if (data->asset == LANG_ASSET_TOKEN)
    fputs("asset token\n", out);
}
