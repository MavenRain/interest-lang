# Validation

Date: 2026-10-09. TinyCC: tcc 0.9.28rc 2026-09-04 mob@0fb54300 (AArch64
Darwin). geth: evm 1.14.12-stable. Foundry: cast 0.3.0 (5a8bd89
2024-12-20). Python: 3.14.7. Host executable: `build/interestc`, built by
`make` from this tree (the tree has commits). Host kit: lang-template
`hosts/tcc-evm-dao` at `1aa27ae`.

`make check` passes after slice O11L. It builds `build/interestc`,
`build/parsetool`, `build/evm-boundaries` and the three test-domain
compilers with tcc, compiles the sources and the test tools with the C
compiler as a second check (`check-clang`), and runs `test/gate.sh`. The
gate runs these steps: parse 28 round trips, embed-safety, check 53 cases,
build output 27 checks, EVM signature boundaries 4 checks, refusal 49
cases, normal forms 10, the differential test (27 vectors at k = 3),
domain tests 11, the differential test of the k = 4 domain (64 vectors),
settlement 191 cases with 5 deploys and a gas ceiling on their 191 EVM
calls (`test/gas-baseline.txt`), and claims (20 sequences totaling 500
steps, 100 law calls, 87 contract checks).
Then it looks for an em-dash or an en-dash in the kit. The result is
`gate: 0 failures`. The original I5 gate took 85 seconds of wall time,
with `make clean`.

The I1 slice generated the tree from lang-template with
`bin/new-lang.sh` and wrote `SPEC.md` sections 1 to 10. The gate passed on
the sample escrow domain: parse 27, check 25, refusal 35, normal forms 7,
differential 27 and 64 vectors, domain tests 11, settlement 60 cases.

The I2 slice wrote the prelude claim algebra in `domain/domain.lang`: the
charters, the measure, the treasury, the operations of SPEC section 5 and
their refl laws. It added the Nat built-ins `natMul`, `natDiv` and
`natMod`. The mutants in `test/mutants` state a false law, and the checker
refuses each with `TYPE_MISMATCH` (`test/check.sh`). Thirteen domain
refusals in `test/refusal.sh` check that a program cannot replace a prelude
operation, declare a `mu`, or give a waterfall, a restriction or an issuer
table a wrong argument. After I2:
check 36, refusal 49, normal forms 10.

The I3 slice wrote the contract entries in `domain/entries.c`: deposit,
distribute, withdraw, transfer, attest, cast, amend, and the views mass,
supply, claimOf, charter, reserve and selfConstituting (slice O5a adds the
views balanceOf and totalSupply). The creation code
writes the genesis rows and the start charter. `interestc data` prints the
program data. `test/settlement.py` runs each entry in geth, with the
reverts; its cases went from 60 to 125.

The I4 slice added `test/claims.py`, a Python reference model of the
contract with unbounded integers. It runs random operation sequences on the
model and in geth, and compares the return word and the storage after each
step. Law vectors check conservation, the sum to inflow, the solvency (slice
L6), transfer then
distribute against distribute on the image, that amend moves neither the
measure nor the treasury, that the impossibility reverts leave the state
unchanged, that the R rejections revert, and that the dust goes to the
rent reserve (slice L6).

The I5 slice added `examples/erc721-dirac.lang`, an ERC-721 token as a
Dirac measure (S = 1) under a veto charter rule. `test/check.sh` checks it,
prints its table, verdicts, laws and program data; `test/parse.sh` round
trips it. `test/claims.py` runs 6 sequences of 40 steps on its contract and
checks after each step that one identity holds the one unit. Its 18 law
calls check the restricted charter (an accredited receiver only) and that a
withdraw keeps no remainder. The slice also wrote the docs and
`probe/CAPABILITY.md`.

The staged-change review added regressions for four defects. `test/check.sh`
checks inclusive caps at zero and the largest 64-bit Nat, and refuses
repeated genesis wallets in both data and build while allowing distinct
wallets for one identity. `test/claims.py` checks that the largest 256-bit
identity is refused by transfer and attest, while the largest encodable
identity can receive, accrue and withdraw its claim. `test/build-output.sh`
checks creation and runtime output: compiler refusals preserve artifacts,
source aliases are rejected, and successful builds replace the output.
Each regression reproduced its defect before the fix. The full gate
passes with all four fixes.

The I6 slice removed the two differences from the kit that
`docs/KIT-DEBT.md` records. The program data uses the kit hooks:
`src/evm.h` declares `LangDomainData`, `domain/data.h` defines it, and
`lang_domain_read` and `lang_domain_print` (`domain/entries.c`) read and
write it. `src/main.c` is the kit driver, so a failed write to stdout gives
`IO_WRITE` with exit 2. `test/build-output.sh` has the 7 stdout checks of
the kit and now has 27 checks; the other counts did not change. The
outputs of `data`, `table`, `build` and `build --runtime` for the three
examples and for `test/domains/plural4.lang` (k = 4) did not change, byte
for byte. Two mutants make the gate fail: `lang_domain_read` that gives
NULL data (`test/check.sh` fails first), and a driver without the
`ferror(stdout)` check (`test/build-output.sh` fails).

The L7 slice added a gas check to `test/settlement.py`. Each EVM call runs
with a gas limit of 16777216, and geth gives the gas that the call used.
`test/gas-baseline.txt` has one `name gas` line for each of the 141 calls
(133 cases and 8 deploy calls), in the run order. The gate is red if a call
uses more gas than its line, or if the calls are not the calls of the file.
A call that uses less gas prints a `GAS note` line, and the gate stays green.
`python3 test/settlement.py --write-gas` writes the file again. The value is
the execution gas: geth `evm run` does not charge the intrinsic gas of a
transaction. The claims calls have no gas check.

The L1 slice moved the members limit of Arrow-Debreu from the amend word
to the verdict table of at most 4096 bytes (k = 3: 63 members, k = 4: 15
members). It added 35 settlement cases: 32 cast and amend calls at k = 3
n = 15 and 63 and at k = 4 n = 7 and 15, one more refusal row above the
limit, and 2 checks of the `lang_entry_amend` body (`evmtool amend`:
k = 3 n = 14 gives the packed word, n = 15 gives the revert). The 34 new
lines of `test/gas-baseline.txt` are the 32 calls and the 2 deploy calls
at k = 3 n = 63. Thus the file has 175 calls (165 cases and 10 deploy
calls).

The O5a slice added the ERC-20 read facade: the views `balanceOf(address)`
and `totalSupply()`, and the `Transfer` event (LOG3) of `transfer`,
`recover` and the creation code. `test/settlement.py` reads the records of
each call; its cases went to 188 (deploy 5). `test/claims.py` compares the
records of each step with the model, and after each step the fold of the
records since the deploy is mu: sequences 20, steps 500, laws 99,
contract 73. The gas of each successful transfer went up by E_t = 1787,
of each successful recover by E_r = 1787, and of each deploy by
E_g = 1777 for each genesis record.

The L6 slice moved the remainder of `withdraw` to the treasury dust (SPEC
O11). A `withdraw` that pays 1 wei or more moves NUM mod S to DUST (slot
8), the whole wei of DUST go to `RESERVE[rent]`, and `withdraw` logs
`Paid(h, wallet, paid, dust)`, also at paid = 0. `test/settlement.py` has
3 new cases (recycle, recycle-wrap and kept-dust) and the `Paid` records
of the successful withdraw cases; its cases went to 191 (deploy 5).
`test/claims.py` models DUST and the `Paid` record, and the fold reads
only the `Transfer` records. After each step the laws hold with DUST and
the recycled wei, and the solvency law S x balance = the claims + DUST +
S x (R0 + R1) holds on the geth state. The contract check of `withdraw`
runs at DUST 0 and S - 1, and the law vector `dust-recycle` moves 1 wei
of dust to the rent reserve and distributes it by mass: sequences 20,
steps 500, laws 100, contract 87. The gas of each successful `withdraw`
and of each deploy with code went up; no gas line went down. Two mutants
make the gate fail: the recycle to the sale reserve
(`accrual-debreu-withdraw-recycle` fails first), and the `Transfer`
signature in `paid_log` (`accrual-debreu-withdraw-floor` fails first).

The O11L slice made the embedded language follow O11. `Treasury` in
`domain/domain.lang` has the dust as its last field (units of 1/S wei). A
`withdraw` that pays 1 wei or more sets the claim of h to 0 and moves
NUM mod S to the dust, and the whole wei of the dust go to the rent
reserve. A `withdraw` that pays 0 wei, also at S = 0, does not change the
state. `examples/arrow-debreu.lang` checks the local law of `withdraw` by
refl: `withdraw` changes only `num h`, the dust and `reserve[rent]`, and
`num h + dust + S x reserve[rent]` falls by exactly S x paid. At S = 5
and dust 0, the claim of 17 units pays 3 wei and 2 units go to the dust
(`withdrawLaw`, `withdrawClaim` 0, `withdrawDust` 2). At dust 4 and rent
reserve 4, the same claim pays 3 wei, 1 wei goes to the rent reserve and
1 unit stays in the dust (`withdrawCarry` 41, `withdrawCarryReserve` 5,
`withdrawCarryDust` 1, `withdrawCarrySale` 4). `withdrawSmall` checks
that a payment of 0 wei keeps the claim and the dust. `withdrawZero`
went from 0 to 17: at S = 0 the R2 form lost the claim, and O11L keeps
it. The 6 mutants in `test/mutants` that copy the head of the example
(transfer-3, amend-4, withdraw-4, share-30, attest-4, sum-51) were made
again from the new example; their mutated lines and their refusal
messages did not change. `test/check.sh` has 1 new case
(`eval withdrawCarry`), so check went from 52 to 53. The other counts did
not change, because the contract and the model did not change. Two
mutants make the gate fail: a `withdraw` that keeps the remainder as the
claim of h (the R2 form; `withdrawClaim` fails first), and a `recycle`
that moves the whole wei of the dust to the sale reserve
(`withdrawCarry` fails first).

`test/evm-boundaries.c` exercises the public writer with a small domain. It
accepts signatures needing exactly 512 bytes including the NUL and refuses
513-byte signatures with `EVM_SIZE` and no output. Both an empty argument
list and 63 ballot arguments are covered. The 63-argument `settle` entry
failed before the review fix because the size check rejected an exact fit.
