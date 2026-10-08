# Host capability probe: tcc-evm-dao

Date: 2026-10-08. Host tree: lang-template `hosts/tcc-evm-dao` at
`1aa27ae`, read only; this tree is its generated copy with the
interest-lang domain. Driver: `build/interestc` (built 2026-10-08 by
`make`). Other work may have loaded the machine, so each wall time is an
upper bound. The host facts come from `docs/host/CAPABILITY.md` (SPEC
section 8); this file gives them for interest-lang.

To run the probe again: `make check` runs the gate, which holds the
answers of sections 1 to 5 and 10 as tests (`test/parse.sh`,
`test/check.sh`, `test/refusal.sh`). The cost rows of section 6 ran each
command below `python3 probe/guard.py --rss-mb 2048 --timeout 120 --`,
which stops a command above 2048 MB resident memory or above 120 s. The
cost programs are the first 22 lines of `examples/arrow-debreu.lang` and
the first 6 lines of `examples/arrow-impossibility.lang`, with `members`
set to N, written below a new temporary directory. The largest process in
the probe used 257 MB (the 256 MiB arena of one run).

## Questions

Each host must answer these questions. Each answer is one numbered section
below. An answer sets the status of a former in `formers/tcc-evm-dao.md`.

| Question | Section | Formers |
|---|---|---|
| Which commands check a file, print a normal form, build and run? | 1 | all |
| Does the host take several files, or must the driver join them? | 2 | all |
| Which recursion does the host accept, and how does it refuse the rest? | 3 | F6, F7 |
| Which `Nat` primitives exist? Does `Nat` have an eliminator? | 4 | F6, F7 |
| How does a result leave the host? | 5 | target |
| What do check and build cost (time and resident memory) at N = 100 and N = 1000? | 6 | all |
| Can a family take a parameter? Can a constructor of such a family appear in a term? | 7 | F4, F12, F15 |
| Can a program project a Sigma? Does the kernel accept Sigma eta? | 8 | F11 |
| How does a program use equality: transport, cong, an index type? | 9 | F12, F13 |
| How does the host report an axiom? | 10 | R2 |

## 1. Commands

| Job | Command | Result |
| --- | --- | --- |
| Check | `build/interestc check FILE` | exit 0 and `ok debreu` or `ok impossibility`; exit 1 and `interestc: CODE: DEF: message` on stderr |
| Print normal form | `build/interestc eval FILE NAME` | the normal form of NAME, for example `reflNat 5` |
| Build | `build/interestc build FILE [--runtime] -o OUT` | the creation code (or the runtime code) as lowercase hex, one line |
| Run | geth `evm run --prestate P --dump` | one contract call; `test/settlement.py` and `test/claims.py` run one call for each step and chain the storage |

- The verbs `table`, `verdicts` and `data` give the tally codes, the
  verdict of each ballot vector and the program data.
- One program file for each run. The run stops at the first error.
- Not probed: gas (geth `evm run` charges no gas) and a deploy to a live
  chain.

## 2. Several files

A run reads one program file. There is no import form. The prelude
`domain/domain.lang` is embedded in the executable at build time
(`gen/embed.c`), and each verb parses it before the program.

## 3. Recursion and totality

Recursion comes only from structural `def rec` in the prelude (`fold`).
A program cannot declare `def rec`:
`interestc: REFUSE_REC: f: FILE:2:9: a program may not declare def rec f; recursion lives in the prelude`.
`nu` is a refused form:
`interestc: REFUSE_FORM: -: FILE:2:1: interestc refuses the nu form`.

## 4. Nat

`Nat` is a 64-bit word in the checker. The primitives are `natAdd`,
`natSub`, `natMul`, `natDiv`, `natMod`, `natEq` and `natLt`; each reduces
on literals. `natSub 2 5` is 0; `natDiv 7 0` and `natMod 7 0` are 0 (as
EVM DIV and MOD). `natAdd` and `natMul` refuse an overflow with
`TYPE_NAT`. `Nat` has no eliminator, so a definition cannot recurse on a
`Nat`. The largest value that leaves the checker is 2^64 - 1. The contract
computes with 256-bit words and reverts on a wrap.

## 5. How a result leaves the host

The verbs `check`, `table`, `verdicts`, `eval` and `data` print text on
stdout. `build` writes the contract as hex to the file OUT. The contract
returns one 32-byte word for each call.

## 6. Cost

| Step | N = 100 | N = 1000 |
| --- | --- | --- |
| Check (Arrow-Debreu, plurality) | 0.17 s, 4.6 MB | 0.10 s, 4.7 MB |
| Table (Arrow-Debreu, plurality) | 1.94 s, 20.5 MB | exit 1 after 8.6 s, 257 MB: `MEMORY`, the arena is full |
| Build (Arrow-impossibility) | 0.25 s, 4.5 MB | 0.03 s, 4.5 MB |
| Build (Arrow-Debreu) | exit 1 after 1.1 s, 20 MB: `EVM_LIMIT`, 1 to 14 members | exit 1 after 38.9 s, 256 MB: `MEMORY` |
| Run | not probed at N | not probed at N |

The gate runs the contracts of the three examples (3 members) in geth. The
claims test runs 500 sequence steps and 158 more calls.

## 7. Parametric families

A constructor of a `mu` family with a parameter cannot appear in a term. A
family with a type index lives in `Type 1`, and its `match` cannot use a
function on the outer type (assay kernel probe P2, 2026-10-06, in
`docs/host/CAPABILITY.md`). Thus the prelude has one list family for each
element type (`Ballots`, `Holders`), and `Option A` and `Sum A B` are
definitions over the built-in `sum`.

## 8. Sigma projection and eta

There is no Sigma pattern. The C checker accepts dependent projections
`.0` and `.1` and eta conversion, including
`fun (s : S) => (s.0, s.1)` for `S := (n : Nat) * EqNat n 3`.
`Config`, `Tally` and `Aggregation F` remain `mu` records read by `match`.
A program uses the same projections for products (for example `c.1.1` and
`s0.0` in the examples).

## 9. Equality

There is one equality family for each index type: `EqNat`, `EqDec` and
`EqTally`, with the constructors `reflNat`, `reflDec` and `reflTally`. A
program proves a law by refl at closed terms: the checker normalizes both
sides. There is no general `transport` or `cong`.

## 10. Axiom detection

There is no axiom form, so a checked program has no axiom and no command
lists axioms. The checker refuses the form:
`interestc: REFUSE_FORM: -: FILE:2:1: interestc refuses the axiom form`
(`test/refusal.sh`, `form-axiom`).

## Consequences for M0

1. A law is a refl proof at a closed term (sections 3 and 9). The
   examples state each law at a concrete state.
2. One equality family and one list family for each index or element type
   (section 7).
3. The checker and the contract use different words (section 4). The gate
   compares the contract with a Python model of unbounded integers that
   reverts on each wrap (`test/claims.py`).
4. The amend word limits Arrow-Debreu to 14 members at k = 3 (section 6).
