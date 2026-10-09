# interest-lang status

The build implements milestones M0 to M3 of `SPEC.md` (slices I1 to I5,
2026-10-08). `interestc` checks a program, gives the verdict table of its
constitution, gives its program data and writes an EVM contract with the
entries of SPEC section 7. The reference model `test/claims.py` agrees with
geth on operation sequences and on the law vectors of milestone M2. No
milestone of SPEC section 10 is open.

## Implemented

- The prelude claim algebra of SPEC sections 4 and 5 in
  `domain/domain.lang`: `Measure`, `credit`, `debit`, `mass`, `transfer`,
  `deposit`, `distribute`, `withdraw`, `attest`, `amendState`, the
  restriction R and the waterfall W. The examples prove the laws by refl
  (slice I2).
- The domain refusals of SPEC section 2: a program cannot redefine a
  prelude name (no mint, burn, charter write or floor payout of its own),
  declare a `mu` family or `def rec`, or use a contract form such as
  `storage`, `entry` or `payable` (`test/refusal.sh`; slice I2).
- The Nat built-ins `natMul`, `natDiv` and `natMod` next to `natAdd`,
  `natSub`, `natEq` and `natLt`.
- The program data of SPEC section 7: the defs `start`, `genesis`,
  `restrict`, `waterfall` and `issuers`, read by name and type, and the
  verb `interestc data` (slice I3).
- The contract entries of SPEC section 7 in `domain/entries.c`:
  `deposit`, `distribute`, `withdraw`, `transfer`, `attest`, `recover`
  (slice O3), `cast`, `amend` and the views `mass`, `supply`, `claimOf`, `charter`, `reserve`,
  `selfConstituting`, and the ERC-20 read facade `balanceOf` and
  `totalSupply` with the `Transfer` event (slice O5a). The creation code
  writes the genesis storage (slice I3).
- The reference model and the differential test `test/claims.py`:
  operation sequences, the law vectors and the contract checks (slice I4).
- The remainder rule of `withdraw` (slice L6, SPEC O11): the treasury
  dust (slot 8), the move of its whole wei to the rent reserve, and the
  `Paid` event of `withdraw`. The embedded language follows the same rule:
  `Treasury` has the dust as its last field, and `withdraw` moves the
  remainder to the dust (slice O11L).
- The ERC-20 asset carrier (slice O5b): the optional def
  `asset : AssetMode := token` makes the asset the units of one ERC-20
  carrier. The constructor word gives the carrier address (CARRIER, slot
  9). `deposit(kind, a)` pulls a with `transferFrom` and checks the
  balance delta, and `withdraw` pays with `transfer`. The default `wei`
  mode does not change.
- Four examples: a Debreu charter vote, a labelled-constitution
  impossibility and an ERC-721 Dirac measure with S = 1 (slice I5), and
  the Debreu example in token mode (slice O5b).

## Remaining work

1. Done: apply `docs/KIT-DEBT.md` to lang-template. Applied in
   lang-template `fa1131a` (slice K2).
2. Done: the first commit of this tree (`a2ce1b8`).
3. Done: remove the two differences from the kit (`docs/KIT-DEBT.md`,
   "Differences that remain"). The program data uses the kit hooks
   `lang_domain_read` and `lang_domain_print`, and a failed stdout write
   gives `IO_WRITE` (slice I6).

## Known limits

- Arrow-Debreu takes at most 63 members at k = 3 and 15 members at k = 4.
  Cause: the verdict table has at most 4096 bytes (`EVM_LIMIT`). The core
  entry `lang_entry_amend` packs the code of each tally in one 256-bit
  word, and reverts when the codes do not fit one word (more than 14
  members at k = 3).
- The plurality rule at 1000 members fills the 256 MiB arena (`MEMORY`)
  before `TABLE_LIMIT`. Cause: a run has one arena, and `table` evaluates
  the rule at each of the 501501 tallies (`probe/CAPABILITY.md`, section 6).
- The checker stops at depth 4096 and at 2^24 steps for each declaration
  (`TYPE_FUEL`). Cause: it evaluates by C recursion on an 8 MB stack.
- `Nat` in the checker is a 64-bit word (`TYPE_NAT` on an overflow). The
  contract computes with 256-bit words and reverts on a wrap.
- At most 32 genesis rows, 4 profiles and 2 payment kinds
  (`LANG_GENESIS_MAX`, `LANG_PROFILES`, `LANG_KINDS` in `domain/data.h`).
- In the contract, when `withdraw` pays 1 wei or more, it moves NUM mod S of
  the identity to the treasury dust (DUST, slot 8). The whole wei of DUST
  go to the rent reserve, so less than 1 wei stays in DUST. A `withdraw`
  that pays 0 wei keeps the remainder for the identity. `distribute` is
  open to all callers, so a caller picks the time of a distribution, and
  `withdraw` pays the calling wallet of the identity.
- The gate measures only the execution gas of the settlement calls: geth
  `evm run` does not charge the intrinsic gas of a transaction, and
  `test/claims.py` has no gas check. Each settlement call must not use more
  gas than its line in `test/gas-baseline.txt`.
- The ERC-20 facade is read only. SPEC section 2 refuses `allowance`,
  `approve` and `transferFrom`, and the contract has no `name`, `symbol` or
  `decimals`. The account of the facade is the identity: `balanceOf` reads
  MU, so a wallet reads its mass only at the address of its identity. The
  ERC-721 example has the ERC-20 facade, not the ERC-721 ABI (`ownerOf`,
  the `Transfer` event with an indexed token id).
- The carrier must be a standard ERC-20: no fee on transfer and no
  rebase. `deposit` reverts when the balance of the contract does not grow
  by exactly a, and a rebase changes the balance without an entry call.
- A direct transfer of the carrier to the contract stays locked: no entry
  pays it out, so the solvency law holds with `>=`.
- Genesis wallets and identities are below 2^64, because `Nat` in the
  checker is a 64-bit word (KL4).

## Internal boundaries

The front end (`src/lexer.c`, `src/parser.c`, `src/printer.c`,
`src/arena.c`, `src/diag.c`) parses and prints a program.
`test/parse.sh` checks the printer round trip of the prelude, the three
examples and `test/parser-arms.lang`.

The checker (`src/check.c`) evaluates by normalization by evaluation. It
reads the embedded prelude (`gen/embed.c`), gives the verbs `check`,
`table`, `verdicts`, `eval` and `data`, and refuses the forms of SPEC
section 2. `test/check.sh`, `test/refusal.sh`, `test/normal-forms.py` and
`test/domains.sh` check it.

The EVM writer core (`src/evm.c`, `src/asm.h`, `src/keccak.c`) writes the
dispatcher, the verdict table and `cast`. `test/differential.py` checks
`cast` and `amend` against `interestc verdicts` in geth.

The domain (`domain/domain.lang`, `domain/data.h`, `domain/entries.c`)
gives the prelude, the entries, the program data type and the four hooks
`lang_domain_read`, `lang_domain_print`, `lang_domain_genesis` and
`lang_domain_data`. `test/settlement.py` checks each entry in geth, and
`test/claims.py` checks operation sequences against the reference model.
A program cannot use `mu` families, `def rec`, `nu`, `axiom` or the
contract forms; storage is reached only through the entries.
