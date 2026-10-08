# TinyCC EVM DAO realization

Paths below are relative to this host kit, or to the root of a generated
language. The template's `formers/FORMERS.md` gives the host-neutral laws.
Each status covers `interestc check` and `interestc eval`. A program cannot
declare a `mu` or a `def rec`, so each family and each recursive function
is in `domain/domain.lang`. The kernel limits that shape the prelude are
in `docs/CAPABILITY.md`, section Kernel.

| ID | Former | Status and evidence |
|---|---|---|
| F1 | Product | DONE: built-in product, tuples, `.0` and `.1`; `T3` in `domain/domain.lang` |
| F2 | Coproduct | DONE: built-in `sum`, `inj`, `case` with 2 arms; `Sum`, `pureSum`, `mapSum`, `bindSum` in `domain/domain.lang` |
| F3 | Option | DONE: `Option A` is a definition over `sum`; `none`, `pureOption`, `mapOption`, `bindOption` in `domain/domain.lang` |
| F4 | List | PARTIAL: no generic `List`. One `mu` for each element type: `Ballots`, `Holders` |
| F5 | Monad pure/map/bind | DONE over Option and Sum. PARTIAL over lists: one set for `Ballots`; no monad operations for `Holders` |
| F6 | Algebra fold | PARTIAL: `foldBallots` (structural `def rec`), generic in the result type only |
| F7 | Algebra unfold | ABSENT: no structural measure (`docs/CAPABILITY.md`, section Kernel); `nu` is a refused form |
| F8 | Filter | DONE over Option (`filterOption`). PARTIAL over lists: `filterBallots` |
| F9 | Non-dependent Pi | DONE: `A -> B` |
| F10 | Dependent Pi | DONE: `(x : A) -> B` and the erased binder `(0 x : A)`; `cast` in `domain/domain.lang` |
| F11 | Sigma | DONE: `(x : A) * B`, pairs, dependent projections `.0` and `.1`, and eta conversion. `Config` and `Tally` remain `mu` records. |
| F12 | Eq refl/symm/trans | PARTIAL: one family for each index type (`EqNat`, `EqDec`, `EqTally`), each with symm and trans; no generic `Eq` |
| F13 | Eq transport/cong | PARTIAL: `transportNat`, `transportDec`, `transportTally` and the `cong` functions of each family |
| F14 | Universes | PARTIAL: `Type 0` and `Type 1` only; no example program uses `Type 1` |
| F15 | Generic indexed family | PARTIAL, in `domain/` only: `EqNat` and `Aggregation` (indexed by `ChoiceRule`); a family with a parameter cannot appear in a term |

The EVM writer has no value encoding of these formers. A contract reads
only the decision codes of the verdict table (`README.md`, section The
domain).
