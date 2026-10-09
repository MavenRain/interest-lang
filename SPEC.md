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
- an allowance surface (`allowance`, `approve`, `transferFrom`);
- document-hash data that changes W or R.

The compiler writes the ERC-20 read facade (section 7). A program cannot add
to it.

The optional def `asset : AssetMode` selects the program asset: `wei`
(native wei, the default when the def is absent) or `token` (the units of
one ERC-20 carrier, O5b). The allowance surface stays refused in token mode:
the contract calls `transferFrom` on the carrier, but it does not offer it.

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
| `Treasury` | the treasury as an action of the aggregation | a reserve for each kind (asset units), an accrual index, a claim numerator for each identity (units of `1/S` asset unit, R2) and the dust (units of `1/S` asset unit, O11): `prod (Kind -> Nat, prod (Nat, prod (Identity -> Nat, Nat)))`. The embedded language and the EVM contract have the same fields |
| `InterestState` | `(mu, W, R, treasury)` ("Semantic domain") | `prod (Measure, prod (Charter, Treasury))`; W and R come from the active charter |
| `OwnershipDAO F` | `Sigma (L : Aggregation act F). InterestState` | `(L : Aggregation F) * InterestState` |

`Asset` is a `uint256` amount of the program asset: native wei (the default)
or the units of one ERC-20 carrier (`def asset : AssetMode := token`, O5b).
R3 (RULED 2026-10-07 (USER): native wei) was re-ruled 2026-10-09 (USER):
wei and the carrier, one program data switch. The supply S
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
| `transfer h k q s` | `Option InterestState` | `some (debit h q ; credit k q)` if and only if `k > 0`, `R(h, k, q)` and `q <= mass h`; otherwise `none` (identity 0 is no identity, O5b). Laws: identity at `q = 0` where defined, conservation of supply, associativity on admissible chains, commutativity of disjoint transfers |
| `deposit kind a s` | `InterestState` | `reserve[kind] += a`; the identity on `mu` and on L |
| `distribute F L kind s` | `Option InterestState` | Needs L. At impossibility L has no inhabitant, so the result is `none`. The result is `none` when `S = 0`, because the sum law cannot hold. Else `d = W(kind)(reserve[kind])`; the claim numerator of each identity h grows by `mass(h) * d`; `reserve[kind] -= d`. Sum law: the numerators grow by `S * d` in total, exactly |
| `withdraw h s` | `prod (Nat, InterestState)` | one meaning for the embedded language and the EVM contract (O11). It pays `paid = floor(num h / S)` asset units. When `paid >= 1`, `num h` := 0, the remainder `num h mod S` goes to the dust, `reserve[rent] += dust / S` and dust := `dust mod S`. When `paid = 0`, also at `S = 0` in the language, the state does not change. Local law: `withdraw` changes only `num h`, the dust and `reserve[rent]`, and `num h + dust + S * reserve[rent]` falls by exactly `S * paid`. EVM solvency law: `S * balance >= sum of the claims + dust + S * (reserve[rent] + reserve[sale])`, where `balance` is the asset balance of the contract (the wei balance, or `balanceOf(this)` of the carrier); the two sides are equal when no direct transfer adds to the balance |
| `attest I G P c w h p s` | `Option (prod (Registry, Profiles))` | a registry write by `c`, a trusted issuer (`I`) of the active charter: the registry maps `w` to `h`, and the profile of `h` becomes `p`. The state does not change. `none` otherwise (ruled 2026-10-08) |
| `cast`, `castOrbit`, `homAmend`, `reconstitute`, `canonical` | escrow-lang types | escrow-lang `SPEC.md` section 5, without change |
| `amend F L x s` | `InterestState` | active charter := `gov F L x`. It does not move `mu` or the treasury. Law: `mass` and `Treasury` do not change (a checked equality) |
| `recover I c h k q s` | `Option InterestState` | ERC-1644 forced recovery (O3). `some (debit h q ; credit k q)` if and only if `c` is a trusted issuer (`I`) of the active charter and `q <= mass h`; otherwise `none`. R does not gate it, and it does not quotient through the voter orbit. Laws: conservation of supply; the claim of each identity and `Treasury` do not change (a checked equality). Arrow-Debreu only |

Transfer then distribute: the claim travels with the token. Before a
transfer moves `mu`, the contract checkpoints both identities: it settles
their accrued numerators. Thus a later distribution reads the image measure,
and transfer then distribute equals distribute on the image measure.

## 6. Regimes

The representation lands in one regime of `self_governance_trichotomy`
(design section "Three fates, inherited").

| Regime | Aggregation F | Entries |
|---|---|---|
| Arrow-impossibility | none | `deposit`, `withdraw`, `attest` (genesis issuers, O7), views. `transfer`, `distribute`, `cast` and `amend` revert. Claims freeze as measures; funds and dust stay in the treasury, not burned and not paid |
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
  the runtime code. Repeated genesis wallets and a
  genesis row with identity 0 are refused with `CONTRACT_GENESIS`;
  distinct wallets may share an identity.
  In token mode (`asset token`, O5b), the creation code first reads the
  constructor word, the last 32 bytes of the init code, as the carrier
  address. A missing or extra word, the word 0, a word with a bit above
  bit 159, or an address with no code reverts the creation. Then CARRIER
  := the word.
- **Storage.** The `mu` mapping, the registry mapping (wallet to identity),
  the profile mapping, the active charter slot, `reserve[rent]`,
  `reserve[sale]`, the accrual index, the dust (O11), the carrier address
  (CARRIER, slot 9, token mode only, O5b), and the checkpoint
  and numerator mappings for each identity. A mapping slot is `keccak256(key word, base
  slot word)`.
- **Code data.** The charter tables (R as a `Cap` table over pairs of
  profiles, the W gates, the issuers) and the verdict table are data in the
  runtime code. The runtime reads them with CODECOPY. The tally index comes
  from the ballot counts.
- **Entries.** In wei mode, `deposit(kind)` is payable. In token mode,
  `deposit(kind, a)` is not payable: it pulls a units of the carrier with
  `transferFrom(caller, this, a)` (O5b). The other entries are
  `distribute(kind)`, `withdraw()`, `transfer(to, q)`, `attest(w, h, p)`,
  `recover(from, to, q)`, `cast(b1..bn)` and `amend(b1..bn)`. The views are `mass(h)`, `supply()`,
  `claimOf(h)`, `charter()`, `reserve(kind)` and `selfConstituting()` (a
  constant that the compiler computes). The ERC-20 read facade adds the
  views `balanceOf(h)` (MU[h], as `mass(h)`) and `totalSupply()` (S). Each
  argument is a `uint256` word, but the argument of `balanceOf` has the ABI
  type `address`; the runtime reads it as the identity word.
  The compiler computes each selector as `keccak256` of the signature.
  Ballots are call arguments (escrow-lang shape; no ballot authentication,
  O2). The identity of the caller is the registry image of the caller
  address; `to` is an identity (O10).
- **Events.** A successful `transfer` and a successful `recover` log one
  `Transfer(from, to, q)` record (LOG3, the ERC-20 event; `from` and `to`
  are identities), also at `q = 0` and at `to = from`. The creation code
  logs `Transfer(0, h, MU[h])` for each genesis identity with units. A
  successful `withdraw` logs one `Paid(h, wallet, paid, dust)` record
  (LOG3; `h` and the calling wallet are topics, `paid` and the moved dust
  are the data), also at `paid = 0` (O11). No other entry logs, and a
  revert logs nothing.
- **Guards.** Short calldata and an unknown selector revert. A non-payable
  entry reverts on value. An erased proof becomes a guard, and a failed
  guard reverts. A refused transfer (R fails, `q > mass h`, `to = 0`, or
  `to + 1` overflows the registry encoding) reverts and leaves the state
  unchanged. A refused recovery (the caller is not an issuer of the
  active charter, `q > mass from`, `from = 0` or `to = 0`, or `from + 1`
  or `to + 1` overflows the registry encoding) reverts and leaves the
  state unchanged. `attest` reverts at `h = 0`. Identity 0 is no
  identity: the registry keeps `h + 1`, so the word 0 means that the
  wallet has no identity (O5b).
  Like `transfer`, `recover` settles both identities before it moves `mu`.
  An entry of section 6 that has no meaning in
  the regime reverts.
  In token mode every entry reverts on value. A carrier call that fails,
  that returns false, or that returns 1 to 31 bytes reverts the entry: the
  call must succeed with no return data, or with 32 bytes or more and the
  first word 1. `deposit` reads `balanceOf(this)` of the carrier before
  and after the `transferFrom`, and reverts unless the balance grew by
  exactly a (no fee on transfer).
- **Withdraw.** `withdraw` writes NUM, then `reserve[rent]`, then DUST,
  then logs the `Paid` record, before the CALL that pays the asset (O11).
  In token mode the payment is the carrier call `transfer(caller, paid)`,
  and a `withdraw` that pays 0 makes no carrier call (O5b).

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
  RULED 2026-10-08 (USER): the issuer table of the active charter (the
  table that gates `attest`) authorizes `recover`. There is no new storage,
  definition or `data` line. `recover(from, to, q)` is an Arrow-Debreu
  entry, and R does not gate it. At `frozen`, `examples/arrow-debreu.lang`
  has no issuer, so `recover` reverts. The settlement cases
  `example-debreu-recover-issuer`, `example-debreu-recover-non-issuer`,
  `example-debreu-recover-r-free` and `example-impossibility-recover` and
  the claims law calls `law-recover-*` pin this ruling.
- O4. ERC-1400 partitions: a coproduct of measures with one R for each
  partition. This is a later milestone. M0 to M3 have one partition and an
  ERC-3643-shaped registry. The ERC-721 shape is an example program with
  `S = 1` (a Dirac measure).
- O5. An ERC-20 asset carrier (R3 defers it), and an ERC-20 or ERC-3643 ABI
  facade with events. O5a (2026-10-09): the read facade and the `Transfer`
  event. O5b (2026-10-09): the ERC-20 asset carrier (R3 re-ruled,
  section 4). Open: the write facade, O5c.
- O6. The genesis charter. RULED 2026-10-08 (USER): the `start` def of the
  program gives the genesis charter, and it can be any declared charter.
  `examples/arrow-debreu.lang` has `start restricted`, the second declared
  charter. The settlement deploy `example-debreu-deploy` and the settlement
  case `example-debreu-charter` pin this ruling.
- O7. `attest` at impossibility. RULED 2026-10-08 (USER): `attest` is
  allowed at impossibility under the genesis issuers, because the registry
  is not a claim. The settlement cases `example-impossibility-attest-issuer`
  and `example-impossibility-attest-non-issuer` pin this ruling.
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
- O11. The remainder of `withdraw` (L6). RULED 2026-10-09 (USER): a
  `withdraw` that pays 1 wei or more moves r = `NUM[h] mod S` to the
  treasury dust (DUST, slot 8, units of `1/S` wei). Then
  `RESERVE[rent] += DUST / S` (checked, a wrap reverts) and
  DUST := `DUST mod S`. A `withdraw` that pays 0 wei keeps `NUM[h]` and
  moves no dust. This ruling changes R2 (2026-10-07) for the remainder
  only: S is fixed at genesis, and transfer conserves it. A successful
  `withdraw` logs `Paid(uint256 indexed h, address indexed wallet,
  uint256 paid, uint256 dust)` before the CALL, also at paid = 0. There
  is no `dust()` view, and `distribute` stays open to all callers. The
  settlement cases `accrual-debreu-withdraw-recycle`,
  `accrual-debreu-withdraw-recycle-wrap` and
  `accrual-debreu-withdraw-kept-dust` and the claims law calls
  `law-dust-recycle-*` pin this ruling. Slice O11L (2026-10-09): the
  embedded language follows this ruling.

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
cases. After O5b (2026-10-09), `make check` passes on the interest-lang
domain and the four examples: parse 28, check 58, build output 29, EVM
boundaries 4, refusal 49, normal forms 10, differential 27 and 64 vectors,
domain tests 11, settlement 213 cases with 9 deploys (218 EVM calls under
the gas ceiling of `test/gas-baseline.txt`), claims 20 sequences (530
steps), 100 law calls and 87 contract checks, and a token run of 10
sequences (200 steps), 0 failures
(`docs/VALIDATION.md`). The kit debt of `docs/KIT-DEBT.md` is
applied in lang-template `fa1131a`, and `a2ce1b8` is the first commit of
this tree. I6 removed the two differences from the kit that
`docs/KIT-DEBT.md` records: the program data uses the kit hooks
`lang_domain_read` and `lang_domain_print`, and a failed write to stdout
gives `IO_WRITE`. The open work is in `docs/STATUS.md`.
