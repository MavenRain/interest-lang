# Validation

Date: 2026-10-08. TinyCC: tcc 0.9.28rc 2026-09-04 mob@0fb54300 (AArch64
Darwin). geth: evm 1.14.12-stable. Foundry: cast 0.3.0 (5a8bd89
2024-12-20). Python: 3.14.7. Host executable: `build/interestc`, built by
`make` from this tree (the tree has no commit yet). Host kit: lang-template
`hosts/tcc-evm-dao` at `1aa27ae`.

`make check` passes after the staged-change review fixes. It builds `build/interestc`,
`build/parsetool` and the three test-domain compilers with tcc, compiles the
sources and the test tools with the C compiler as a second check
(`check-clang`), and runs `test/gate.sh`. The gate runs these steps: parse 28 round trips,
embed-safety, check 52 cases, build output 27 checks, refusal
49 cases, normal forms 10, the
differential test (27 vectors at k = 3), domain tests 11, the differential
test of the k = 4 domain (64 vectors), settlement 125 cases with 4 deploys,
and claims (20 sequences totaling 500 steps, 88 law calls, 70 contract checks).
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
supply, claimOf, charter, reserve and selfConstituting. The creation code
writes the genesis rows and the start charter. `interestc data` prints the
program data. `test/settlement.py` runs each entry in geth, with the
reverts; its cases went from 60 to 125.

The I4 slice added `test/claims.py`, a Python reference model of the
contract with unbounded integers. It runs random operation sequences on the
model and in geth, and compares the return word and the storage after each
step. Law vectors check conservation, the sum to inflow, transfer then
distribute against distribute on the image, that amend moves neither the
measure nor the treasury, that the impossibility reverts leave the state
unchanged, and that the R rejections revert.

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
