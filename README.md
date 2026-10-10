# interest-lang

interest-lang is a language for an ownership-interest DAO on the EVM. A
program gives a constitution (a rule that chooses the active charter from
the ballots of the `members` seats; each identity votes with its mass)
and the program data of the contract: the
start charter, the genesis units of each identity, the transfer
restriction R and the payout waterfall W of each charter, and the trusted
issuers. The compiler `interestc` checks the program, gives the verdict
table of the constitution and writes the EVM contract. The language comes
from lang-template (commit `1aa27ae`) with the tcc-evm-dao host.

## Build and test

```sh
make          # build/interestc and build/parsetool, with tcc
make check    # the gate: test/gate.sh, 0 failures
```

The gate needs tcc 0.9.28rc, geth `evm` 1.14.12, foundry `cast`,
`python3` and `rg`. `docs/VALIDATION.md` gives the counts of the last run.

## Commands

```sh
build/interestc check PROG                    # ok debreu | ok impossibility
build/interestc table PROG                    # the regime, the members, the code of each tally
build/interestc verdicts PROG NAME            # one charter code for each ballot vector
build/interestc eval PROG NAME                # the normal form of NAME
build/interestc data PROG                     # the program data, one line for each table
build/interestc build PROG [--runtime] -o OUT # the creation code (or the runtime code) as hex
```

An error goes to stderr as `interestc: CODE: DEF: message`, with exit 1.

## Examples

| File | Regime | What it shows |
|---|---|---|
| `examples/arrow-debreu.lang` | Arrow-Debreu | A charter vote. The plurality rule reads the orbit of the ballots, so the program writes its aggregation and the contract has `cast`, `vote` and `amend()`: each identity votes with its mass, and `amend()` gives the seats by the largest remainder. 10 genesis units over two identities; the laws of SPEC section 5 as refl proofs. |
| `examples/arrow-impossibility.lang` | Arrow-impossibility | A labelled constitution. The rule reads member position 0, so two configurations in one orbit get two verdicts and no aggregation can exist. The contract has no `cast`, `vote`, `amend`, `transfer` or `distribute`. |
| `examples/erc721-dirac.lang` | Arrow-Debreu | ERC-721 as a Dirac measure. S = 1: one identity holds the one unit, and `transfer` moves it whole. A veto constitution; the restricted charter admits a transfer to an accredited receiver only. |

The gate checks and compiles each example and runs its contract in geth
(`test/check.sh`, `test/parse.sh`, `test/settlement.py`,
`test/claims.py`).

## Files

- `SPEC.md`: the specification.
- `design/DESIGN.md`: the domain design. It is the only source of the core
  types and the core operations.
- `formers/FORMERS.md`: the type formers. `formers/tcc-evm-dao.md` gives
  their status on the tcc-evm-dao host.
- `probe/CAPABILITY.md`: the host probe, answered for this host.
- `docs/STATUS.md` and `docs/VALIDATION.md`: the status and the gate
  results.
- `docs/KIT-DEBT.md`: the core changes of this tree to the host kit,
  applied in lang-template `fa1131a`, and the differences that remain.
- `docs/host/README.md`: the host kit, the domain, the storage and the
  entries. `docs/host/CAPABILITY.md`: the host facts.

## License

MIT OR Apache-2.0 (`LICENSE-MIT`, `LICENSE-APACHE`).
