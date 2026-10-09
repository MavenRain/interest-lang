# interest-lang specification (draft)

Status: draft, slices I1 to I5 complete (section 10). lang-template generated
this language with the tcc-evm-dao host. `domain/` holds the interest claim
algebra and contract entries. `interest-lang` is a working name.

## 1. Purpose

interest-lang is a language for one self-referential ownership-interest DAO.
The token is a residual claim on an LLC or an SPV. It is not a deed. The type
formers are F1 to F15 of `formers/FORMERS.md` (section 3). The core data
types and core operations are only the types and operations of
`design/DESIGN.md` (the design).

The design keeps the governance half of escrow-lang without change. The
treasury is an action of the aggregation. It is not a second aggregation.
The meaning of a program is the dependent pair of the design section
"Semantic domain":

```
OwnershipDAO F = (L : Aggregation act F) * InterestState
InterestState  = (mu, W, R, treasury)
```

A program gives a membership size, the charters, a genesis measure and, when
one exists, an aggregation for the constitution. The compiler `interestc`
checks the program and writes the EVM bytecode of one contract (section 7).
The compiler is the host, and it writes the target directly (section 8). The
build writes the compiler to `build/interestc` (O8).

The contract is a representation of the dependent pair. The compiler is
correct when each contract entry denotes the operation of the design section
"Operations are homomorphisms". The checker evaluates the prelude
operations. That evaluation is the executable meaning. `test/claims.py` (I4)
is the reference model of the claim algebra. The differential tests run
operation sequences through the model and through the contract in geth, and
compare the results.

The deed, the land registry and the operating agreement are an external
interpretation of the claim. They are not in the meaning. A transfer that
the contract accepts is not a conveyance of legal title. Liquidity and a
compliant secondary market are also outside the meaning.

## 2. Programs

A program is one `.lang` file of definitions in the subset of assay syntax
that the prelude `domain/domain.lang` uses (section 8):

```
def members : Nat := N
def NAME : TYPE := TERM
```

The first definition must be `members` with a positive integer literal no
larger than C's `UINT_MAX`, otherwise `REFUSE_MEMBERS`. It fixes the number
of voting members. The core types `Config` and `Tally` (section 4) read it.
The compiler checks `members` first, then the prelude, then the rest of the
program.

The compiler refuses these host forms in a program: `mu` (`REFUSE_MU`),
`def rec` (`REFUSE_REC`), and `nu`, `axiom`, `contract`, `storage`, `entry`,
`payable`, `constructor`, `fallback`, `error`, `invariant`, `predicate`,
`proof`, `guard`, `sload` and `sstore` (`REFUSE_FORM`). It also refuses a
definition that uses a prelude name (`REFUSE_PRELUDE_NAME`). Thus a program
cannot add a data type, an unproved fact or general recursion. Recursion
comes only from the folds of the prelude. The compiler writes the contract;
a program cannot.

The domain refusals (I2) add to the host forms. The surface admits only the
types of section 4, the operations of section 5 and the formers of section
3. The compiler refuses at least:

- a mint or a burn after genesis;
- a write to `mu` other than `transfer`;
- a waterfall gate other than `pass` and `retain`;
- a restriction that reads `mu` or a wallet (R reads only identity claims);
- a charter change outside `amend`;
- an issuer change outside a charter;
- a payout by floor division outside `withdraw`;
- an ERC storage surface (`balanceOf`, `allowance`);
- document-hash data that changes W or R.

A program that does not check is refused with a `TYPE_`, `LEX_` or `PARSE_`
code (`docs/host/README.md`, section Refusals). A refusal is one line on
stderr, `interestc: CODE: DEF: message`, and exit 1. A usage or IO error exits 2.
`test/refusal.sh` and the mutants in `test/mutants/` test the refusals.

## 3. Type formers

The type formers are F1 to F15 of `formers/FORMERS.md`. This language uses
the tcc-evm-dao column of the realization matrix. `formers/tcc-evm-dao.md`
gives the host form of each former. The table gives only the formers that
are not DONE on this host.

| ID | Status on tcc-evm-dao | Effect on this language | Open item |
|---|---|---|---|
| F4 | PARTIAL: one `mu` for each element type | The prelude has `Ballots` and the genesis list `Holders` | none |
| F5 | PARTIAL over lists | `pureBallots`, `mapBallots` and `bindBallots`; no monad operations for `Holders` | none |
| F6 | PARTIAL: structural `def rec` folds | `foldBallots`; no fold on `Nat` (built-in `Nat` has no eliminator) | none |
| F7 | ABSENT | No `unfold`. The design has no coinductive data: self-reference is an object-wise fixed point (design section "Self-reference") | none |
| F11 | DONE: dependent pairs, projections and eta | `OwnershipDAO F` is built as a pair. The contract reads `L` only through the verdict table | none |
| F12, F13 | PARTIAL: one `Eq` family for each index type | `EqNat`, the charter family and `EqTally` carry the checked laws of section 5 | O1 |
| F14 | PARTIAL: `Type 0` and `Type 1` | Each core type is in `Type 0` | none |
| F15 | PARTIAL, in `domain/` only | `Aggregation` is indexed by `ChoiceRule` | none |

The structures, with the carriers that the design uses:

- **Monad**: `pure*`, `map*`, `bind*` on `Option`, `Sum E`, `Ballots` and
  the list families of I2.
- **Algebra**: `foldBallots` and the folds of the I2 list families. There is
  no `unfold` (F7).
- **Filterable**: `filterOption`, `filterBallots` and the filters of the I2
  list families.

`Option` comes with the structures. It is not a domain type. Partiality is
`Option`, never an exception (design section "Stance").

One dependent form goes past a first-order language: `Aggregation F` depends
on the constitution `F`, and `amend` changes the active charter (section 5).

## 4. Core types

Each core type is in `Type 0`. The governance rows (`Ballot`, `Ballots`,
`T3`, `Config`, `Tally`, `ChoiceRule`, `Aggregation F`, `act`) are the rows of
escrow-lang `SPEC.md` section 4, without change, with the decision space
`D = Charter`. A definition is a design decision that is not ruled, unless a
ruling is given.

| Type | Meaning (design section) | Definition |
|---|---|---|
| `Nat` | units, wei, counts | built-in |
| `Identity` | a verified identity, not an address ("Semantic domain") | `Nat` (identity id) |
| `Wallet` | representation layer only ("Semantic domain") | `Nat` (address); the registry maps `Wallet -> Identity`, many to one |
| `Juris`, `Status` | identity claims ("Semantic domain") | finite `mu` enums in `domain/domain.lang` (O9): `Juris` has `domestic`, `foreign`; `Status` has `retail`, `accredited` (RULED 2026-10-08 (USER)) |
| `Profile` | the claims that R reads | `prod (Juris, Status)` |
| `Measure` | `mu : Identity -> Q>=0`, finite support ("Semantic domain") | `Identity -> Nat` with `empty`, `mass`, `credit`, `debit`. The meaning of n is n units of `1/S` of the interest, an injective monoid map into `Q>=0` (R2) |
| `Kind` | inflow kind ("Semantic domain") | `mu`: `rent`, `sale` |
| `Gate` | `W(kind)` as a selector | `mu`: `pass`, `retain`. The meaning of `pass` is the identity and the meaning of `retain` is 0. Both are monoid endomorphisms, so W stays a homomorphism |
| `Waterfall` | `W : Kind -> Asset -> Asset` ("Semantic domain") | `Kind -> Gate` |
| `Cap` | the q-dependence of R | `mu`: `deny`, `upTo n`, `any` |
| `Restriction` | `R : Identity x Identity x Q>=0 -> Prop` ("Semantic domain") | `Profile -> Profile -> Cap`; `R(h, k, q) := admits (rule (prof h) (prof k)) q`. R reads only identity claims |
| `Charter` | an object of D: an admissible (R, W) and the trusted issuers ("Semantic domain", "One meaning, three representations") | finite `mu` of named charters in `domain/domain.lang`, decision codes in declaration order (O9); a program gives, for each charter, a `Restriction`, a `Waterfall` and an issuer list. M0 has 3 charters, `open`, `restricted`, `frozen` (codes 1 to 3), so k = 3 (RULED 2026-10-08 (USER)) |
| `Treasury` | the treasury as an action of the aggregation | a reserve for each kind (wei), an accrual index, and a claim numerator for each identity (units of `1/S` wei, R2) |
| `InterestState` | `(mu, W, R, treasury)` ("Semantic domain") | `prod (Measure, prod (Charter, Treasury))`; W and R come from the active charter |
| `OwnershipDAO F` | `Sigma (L : Aggregation act F). InterestState` | `(L : Aggregation F) * InterestState` |

`Asset` is native wei, `uint256` (RULED 2026-10-07 (USER), R3). The supply S
is fixed at genesis. Transfer conserves it (RULED 2026-10-07 (USER), R2).

### 4.1 The aggregation at a discrete decision space

`D = Charter` is discrete. Thus the facts of escrow-lang `SPEC.md` section
4.1 hold here without change. When `Aggregation F` is inhabited, it has one
inhabitant up to L. `IsSelfConstituting F` holds if and only if
`Aggregation F` is inhabited, and if and only if F is constant on each orbit.
The Schelling-Ising regime needs two object-distinct aggregations, so a
discrete D cannot reach it (escrow-lang ruling 2026-10-06, inherited).

The honesty fix of the design carries over: a fork is a fork in the number
of `Aggregation` objects. It is not a fork of `IsSelfConstituting`.

## 5. Core operations

| Operation | Type | Meaning (design section "Operations are homomorphisms") |
|---|---|---|
| `transfer h k q s` | `Option InterestState` | `some (debit h q ; credit k q)` if and only if `R(h, k, q)` and `q <= mass h`; otherwise `none`. Laws: identity at `q = 0` where defined, conservation of supply, associativity on admissible chains, commutativity of disjoint transfers |
| `deposit kind a s` | `InterestState` | `reserve[kind] += a`; the identity on `mu` and on L |
| `distribute F L kind s` | `Option InterestState` | Needs L. At impossibility L has no inhabitant, so the result is `none`. The result is `none` when `S = 0`, because the sum law cannot hold. Else `d = W(kind)(reserve[kind])`; the claim numerator of each identity h grows by `mass(h) * d`; `reserve[kind] -= d`. Sum law: the numerators grow by `S * d` in total, exactly |
| `withdraw h s` | `prod (Nat, InterestState)` | pays `floor(num h / S)` wei and keeps `num h mod S` as the claim of h (R2) |
| `attest I G P c w h p s` | `Option (prod (Registry, Profiles))` | a registry write by `c`, a trusted issuer (`I`) of the active charter: the registry maps `w` to `h`, and the profile of `h` becomes `p`. The state does not change. `none` otherwise (ruled 2026-10-08) |
| `cast`, `castOrbit`, `homAmend`, `reconstitute`, `canonical` | escrow-lang types | escrow-lang `SPEC.md` section 5, without change |
| `amend F L x s` | `InterestState` | active charter := `gov F L x`. It does not move `mu` or the treasury. Law: `mass` and `Treasury` do not change (a checked equality) |
| `recover` | not in M0 to M3 (O3) | ERC-1644 forced recovery. It denotes only when `(Gov L).obj X` authorizes it. R does not gate it, and it does not quotient through the voter orbit |

Transfer then distribute: the claim travels with the token. Before a
transfer moves `mu`, the contract checkpoints both identities: it settles
their accrued numerators. Thus a later distribution reads the image measure,
and transfer then distribute equals distribute on the image measure.

## 6. Regimes

The representation lands in one regime of `self_governance_trichotomy`
(design section "Three fates, inherited").

| Regime | Aggregation F | Entries |
|---|---|---|
| Arrow-impossibility | none | `deposit`, `withdraw`, `attest` (genesis issuers, O7), views. `transfer`, `distribute`, `cast` and `amend` revert. Claims freeze as measures; funds stay in the treasury, not burned and not paid |
| Arrow-Debreu | one orbit rule | all entries |
| Schelling-Ising | unreachable at a discrete D (section 4.1) | none |

## 7. What the compiler writes

The compiler writes the creation code and the runtime code of one contract.
The entries below are in `domain/entries.c`. The host core (`src/evm.c`,
`src/asm.h`) gives the assembler, the labels, the dispatcher, the selectors,
the overflow guards, the mapping slots, the tally and the verdict-table read.

- **Creation.** The creation code reverts on value. It writes the genesis
  storage from the program: the `genesis` list of (wallet, identity,
  profile, units), the supply `S` (the sum of the units, fixed), and the
  active charter (the `start` charter of the program, O6). Then it returns
  the runtime code. Repeated genesis wallets are refused with
  `CONTRACT_GENESIS`; distinct wallets may share an identity.
- **Storage.** The `mu` mapping, the registry mapping (wallet to identity),
  the profile mapping, the active charter slot, `reserve[rent]`,
  `reserve[sale]`, the accrual index, and the checkpoint and numerator
  mappings for each identity. A mapping slot is `keccak256(key word, base
  slot word)`.
- **Code data.** The charter tables (R as a `Cap` table over pairs of
  profiles, the W gates, the issuers) and the verdict table are data in the
  runtime code. The runtime reads them with CODECOPY. The tally index comes
  from the ballot counts.
- **Entries.** `deposit(kind)` is payable. The other entries are
  `distribute(kind)`, `withdraw()`, `transfer(to, q)`, `attest(w, h, p)`,
  `cast(b1..bn)` and `amend(b1..bn)`. The views are `mass(h)`, `supply()`,
  `claimOf(h)`, `charter()`, `reserve(kind)` and `selfConstituting()` (a
  constant that the compiler computes). Each argument is a `uint256` word.
  The compiler computes each selector as `keccak256` of the signature.
  Ballots are call arguments (escrow-lang shape; no ballot authentication,
  O2). The identity of the caller is the registry image of the caller
  address; `to` is an identity (O10).
- **Guards.** Short calldata and an unknown selector revert. A non-payable
  entry reverts on value. An erased proof becomes a guard, and a failed
  guard reverts. A refused transfer (R fails, `q > mass h`, or `to + 1`
  overflows the registry encoding) reverts and
  leaves the state unchanged. An entry of section 6 that has no meaning in
  the regime reverts.
- **Withdraw.** `withdraw` writes the new numerator before the CALL that
  pays the wei.

## 8. Host and target

- **Host.** TinyCC 0.9.28rc mob@0fb54300 (C99, `-Wall -Werror`). tcc has no
  `-Wswitch-enum`, so `make check-clang` checks the same sources with `cc`.
  `docs/host/CAPABILITY.md` gives the toolchain and the kernel limits.
- **Kit.** lang-template `hosts/tcc-evm-dao` at commit `1aa27ae`, a fork of
  escrow-lang `b55630b`. The core is in `src/`: lexer, parser, printer, the
  NbE checker `check.c`, and the EVM writer core `evm.c` with the entry API
  `asm.h`. The domain is in `domain/`: the prelude `domain.lang` (embedded by
  `gen/embed.c`) and the contract entries `entries.c`. `docs/host/README.md`
  tells how to replace the sample domain.
- **Target.** EVM bytecode, creation and runtime. The compiler writes it
  directly. There is no Solidity and no external assembler.
- **Gate.** `make check` builds `build/interestc`, runs `make check-clang`, then
  `test/gate.sh`: parse, embed safety, check, build output (preservation
  and stdout errors), refusal, normal forms, the
  differential test against geth, the k-generic domain tests, the
  settlement test, the claims test (the reference model `test/claims.py`
  against geth) and the em-dash scan. The gate needs geth `evm`
  1.14.12-stable, foundry `cast`, `python3` and `rg`.
- **Probe.** `probe/CAPABILITY.md` gives the host facts for interest-lang.
  Slice I5 filled it from `docs/host/CAPABILITY.md` and from the measured
  costs (2026-10-08).

## 9. Open items

- O1. A Lean proof of the discrete-D facts of section 4.1 (escrow-lang O1).
- O2. Ballot authentication and tallies weighted by `mu`. escrow-lang has one
  unweighted ballot for each member.
- O3. The forced recovery entry (ERC-1644) and its authorization object.
- O4. ERC-1400 partitions: a coproduct of measures with one R for each
  partition. This is a later milestone. M0 to M3 have one partition and an
  ERC-3643-shaped registry. The ERC-721 shape is an example program with
  `S = 1` (a Dirac measure).
- O5. An ERC-20 asset carrier (R3 defers it), and an ERC-20 or ERC-3643 ABI
  facade with events.
- O6. The genesis charter: any declared charter (proposal) or the value of
  `gov` at a genesis configuration.
- O7. `attest` at impossibility. Proposal: allowed under the genesis
  issuers, because the registry is not a claim.
- O8. The compiler name. RULED 2026-10-08 (USER): `interestc`. The build
  writes `build/interestc`, and each diagnostic starts with `interestc: `.
  Slice I2a renamed the host kit name `langc` in the build, the
  diagnostics, the tests and the host docs (2026-10-08).
- O9. Where the finite enums (`Juris`, `Status`, `Charter`) are declared. A
  program cannot declare a `mu` (`REFUSE_MU`), and the kit reads D from the
  `ChoiceRule` of `domain/domain.lang`. RULED 2026-10-08 (USER): the enums
  are in `domain/domain.lang`. The decision codes of `Charter` are in
  declaration order. A program gives the R, W and issuer tables of each
  charter as definitions with fixed names, the same way as `members` (not
  ruled). RULED 2026-10-08 (USER): the constructors are `Juris` =
  `domestic`, `foreign`; `Status` = `retail`, `accredited`; `Charter` =
  `open`, `restricted`, `frozen` (codes 1, 2, 3). Thus M0 has k = 3.
  Slice I2a declared `Juris` and `Status` in `domain/domain.lang`
  (2026-10-08). `Charter` replaces `Decision` in slice I2b.
- O10. The argument `to` of `transfer`. RULED 2026-10-08 (USER): `to` is an
  identity, because R and `mu` read identities. The caller identity is the
  registry image of the caller address; a caller without an identity
  reverts.

## 10. Milestones

| Milestone | Content | Status |
|---|---|---|
| M0 | I1: the language generated from lang-template (host tcc-evm-dao); `SPEC.md` sections 1 to 10. I2: the prelude claim algebra (sections 4 and 5) with checked laws; the domain refusals (section 2); the checker gate and mutants | done 2026-10-08 |
| M1 | I3: the contract entries of section 7 in `domain/entries.c`; the genesis writes of the creation code | done 2026-10-08 |
| M2 | I4: the reference model `test/claims.py`; the differential test against geth on operation sequences; law vectors: conservation, sum to inflow, transfer then distribute equals distribute on the image, `amend` moves neither `mu` nor the treasury, impossibility reverts leave the state unchanged, R rejections revert | done 2026-10-08 |
| M3 | I5: examples (a Debreu charter vote, a labelled-constitution impossibility, an ERC-721 Dirac measure with `S = 1`); `docs/STATUS.md`, `docs/VALIDATION.md`, `README.md`; the final gate | done 2026-10-08 |

Status 2026-10-08: I1 to I5 done, so M0 to M3 are done. lang-template
`bin/new-lang.sh` (with the tcc-evm-dao host added) generated this tree from
lang-template `1aa27ae`. At I1, `make check` passed on the sample escrow
domain: parse 27, check 25, refusal 35, normal forms 7, differential 27
vectors (k = 3) and 64 vectors (k = 4), domain tests 11, settlement 60
cases. After I6, `make check` passes on the interest-lang domain and the
three examples: parse 28, check 52, build output 27, refusal 49, normal
forms 10,
differential 27 and 64 vectors, domain tests 11, settlement 125 cases,
claims 20 sequences (500 steps), 88 law calls and 70 contract checks, 0
failures (`docs/VALIDATION.md`). The kit debt of `docs/KIT-DEBT.md` is
applied in lang-template `fa1131a`, and `a2ce1b8` is the first commit of
this tree. I6 removed the two differences from the kit that
`docs/KIT-DEBT.md` records: the program data uses the kit hooks
`lang_domain_read` and `lang_domain_print`, and a failed write to stdout
gives `IO_WRITE`. The open work is in `docs/STATUS.md`.
