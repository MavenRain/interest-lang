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
of seats: the resolution of the weighted vote (O2). The core types `Config`
and `Tally` (section 4) read it.
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
- document-hash data that changes W or R.

The compiler writes the ERC-20 facade (section 7): the views and, at
Debreu, `transfer(address,uint256)` and the allowance surface
`approve`, `allowance` and `transferFrom` (O5c). A program cannot add to
it.

The optional def `facade : Facade` selects the token facade: `erc20` (the
default when the def is absent) or `erc721` (O5d). At `erc721` the
compiler writes the ERC-721 entries in place of the ERC-20 write facade
(section 7). The check refuses an `erc721` program with `CONTRACT_GENESIS`
when the genesis is not one unit in classA (S = 1), and with
`CONTRACT_VALUE` when the asset is `token` or the regime is
impossibility. A `facade` def of another type is `CONTRACT_TYPE`.

The optional def `asset : AssetMode` selects the program asset: `wei`
(native wei, the default when the def is absent) or `token` (the units of
one ERC-20 carrier, O5b). The facade is the same in both modes. In token
mode the contract also calls `transferFrom` on the carrier; that call is to
another contract.

The optional defs `name : Text` and `symbol : Text` give the ERC-20 name
and symbol (O5c). Each byte of the text is in the range 32 .. 126, and a
def has a maximum of 32 bytes. If not, the check refuses the program with
`CONTRACT_VALUE`. If a def is absent, the name is "interest" and the
symbol is "INT".

The optional def `quorum : Nat` gives the quorum q in mass units (O2b,
MY CALL 238): `amend()` reverts when the WEIGHT sum is below q. If the def
is absent, q is 1. The check refuses the program with `QUORUM_LIMIT` when
q is 0, when q is above the supply S (the sum of the genesis units), or
when the regime is impossibility. The verb `data` gives the line
`quorum q` only when q is not 1.

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

O2 adds `Votes`, `wtally` and `seats` before the governance rows. `wtally`
is the image of `mu` along the ballot map (a measure on D), and `seats`
takes it to a `Tally` of n = `members` seats by the largest remainder (ties
go to the lower code). The governance rows act on that `Tally` without
change.

| Type | Meaning (design section) | Definition |
|---|---|---|
| `Nat` | units, wei, counts | built-in |
| `Identity` | a verified identity, not an address ("Semantic domain") | `Nat` (identity id) |
| `Wallet` | representation layer only ("Semantic domain") | `Nat` (address); the registry maps `Wallet -> Identity`, many to one |
| `Juris`, `Status` | identity claims ("Semantic domain") | finite `mu` enums in `domain/domain.lang` (O9): `Juris` has `domestic`, `foreign`; `Status` has `retail`, `accredited` (RULED 2026-10-08 (USER)) |
| `Profile` | the claims that R reads | `prod (Juris, Status)` |
| `Measure` | `mu : Identity -> Q>=0`, finite support ("Semantic domain") | `Identity -> Nat` with `empty`, `mass`, `credit`, `debit`. The meaning of n is n units of `1/S` of the interest, an injective monoid map into `Q>=0` (R2) |
| `Partition` | an ERC-1400 partition (O4) | `mu` in `domain/domain.lang` (MY CALL 202): `classA`, `classB` (codes 1 and 2) |
| `Book` | a coproduct of measures, one for each partition (O4) | `Partition -> Measure`. `bookTotal` is the codiagonal: the mass of h in the total is the sum of the masses of h in the partitions |
| `Kind` | inflow kind ("Semantic domain") | `mu`: `rent`, `sale` |
| `Gate` | `W(kind)` as a selector | `mu`: `pass`, `retain`. The meaning of `pass` is the identity and the meaning of `retain` is 0. Both are monoid endomorphisms, so W stays a homomorphism |
| `Waterfall` | `W : Kind -> Asset -> Asset` ("Semantic domain") | `Kind -> Gate` |
| `Cap` | the q-dependence of R | `mu`: `deny`, `upTo n`, `any` |
| `Restriction` | `R : Identity x Identity x Q>=0 -> Prop` ("Semantic domain") | `Profile -> Profile -> Cap`; `R(h, k, q) := admits (rule (prof h) (prof k)) q`. R reads only identity claims |
| `Charter` | an object of D: an admissible (R, W) and the trusted issuers ("Semantic domain", "One meaning, three representations") | finite `mu` of named charters in `domain/domain.lang`, decision codes in declaration order (O9); a program gives, for each charter, a `Restriction`, a `Waterfall` and an issuer list. M0 has 3 charters, `open`, `restricted`, `frozen` (codes 1 to 3), so k = 3 (RULED 2026-10-08 (USER)) |
| `Treasury` | the treasury as an action of the aggregation | a reserve for each kind (asset units), an accrual index, a claim numerator for each identity (units of `1/S` asset unit, R2) and the dust (units of `1/S` asset unit, O11): `prod (Kind -> Nat, prod (Nat, prod (Identity -> Nat, Nat)))`. The embedded language and the EVM contract have the same fields |
| `InterestState` | `(mu, W, R, treasury)` ("Semantic domain") | `prod (Book, prod (Charter, Treasury))` (O4); W and R come from the active charter, with one R for each partition |
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
| `transferIn p h k q s` | `Option InterestState` | O4. `some` (the book with `debit h q ; credit k q` in partition p) if and only if `k > 0`, `R p (h, k, q)` (the R of partition p, MY CALL 207) and `q <= mass (s.0 p) h`; otherwise `none` (identity 0 is no identity, O5b). The other partition does not change. Laws: identity at `q = 0` where defined, conservation of supply, associativity on admissible chains, commutativity of disjoint transfers |
| `transfer h k q s` | `Option InterestState` | `transferIn classA h k q s` (O4, MY CALL 203): a move without a partition moves classA units |
| `approve o p v A` | `Allowance` | O5c. `Allowance := Identity -> Identity -> Nat` is outside InterestState, as the registry is: `A f p` is the count of units of `f` that the spender `p` can move. `approve` sets `A o p` to `v`, and no other allowance changes. The state does not change. The contract also reverts when `o` or `p` is 0 (section 7, Guards). Laws: `approveSets`, `approveOther` |
| `transferFrom p f k q s A` | `Option (prod (Allowance, InterestState))` | O5c. `none` when `A f p < q`; else `transfer f k q s` with `A f p` := `A f p - q`, and `none` when that transfer is `none`. The spender needs an allowance also when `p = f`. Thus `transfer` stays the only write to `mu`. Laws: `transferFromMoves` (the move of `transfer` by `f`), `transferFromAllowance` (the allowance falls by q), `transferFromOver` (q above the allowance: `none`), `transferFromRestricted` (R refuses: `none` with an allowance) |
| `approveNft a n`, `setOperator o p v n` | `Nft` | O5d. `Nft := prod (Identity, Identity -> Identity -> Nat)` is outside InterestState, as `Allowance` is: `n.0` is the approved identity of the one unit (0: none), and `n.1 f p` is not 0 when `p` is an operator of `f`. `approveNft` sets the approved identity to `a`. `setOperator` sets `n.1 o p` to `v` (the `approve` form on `n.1`). The state does not change |
| `transferNft R P p f k s n e` | `Option (prod (Nft, InterestState))` | O5d. `mayMoveNft p f n` is leg 1 when the caller `p` is not 0 and `p` is `f`, the approved identity `n.0` or an operator of `f`. Then the result is `transfer f k 1 s` with the approved identity := 0, and `none` when that transfer is `none`. Else the result is `none`. Laws: `ownerAfterTransfer`, `approvalClears`, `approvedMoves`, `operatorMoves`, `strangerRefused`, `balanceDirac` |
| `deposit kind a s` | `InterestState` | `reserve[kind] += a`; the identity on `mu` and on L |
| `distribute F L kind s` | `Option InterestState` | Needs L. At impossibility L has no inhabitant, so the result is `none`. The result is `none` when `S = 0`, because the sum law cannot hold. Else `d = W(kind)(reserve[kind])`; the claim numerator of each identity h grows by `mass(h) * d`; `reserve[kind] -= d`. Sum law: the numerators grow by `S * d` in total, exactly |
| `withdraw h s` | `prod (Nat, InterestState)` | one meaning for the embedded language and the EVM contract (O11). It pays `paid = floor(num h / S)` asset units. When `paid >= 1`, `num h` := 0, the remainder `num h mod S` goes to the dust, `reserve[rent] += dust / S` and dust := `dust mod S`. When `paid = 0`, also at `S = 0` in the language, the state does not change. Local law: `withdraw` changes only `num h`, the dust and `reserve[rent]`, and `num h + dust + S * reserve[rent]` falls by exactly `S * paid`. EVM solvency law: `S * balance >= sum of the claims + dust + S * (reserve[rent] + reserve[sale])`, where `balance` is the asset balance of the contract (the wei balance, or `balanceOf(this)` of the carrier); the two sides are equal when no direct transfer adds to the balance |
| `attest I G P c w h p s` | `Option (prod (Registry, Profiles))` | a registry write by `c`, a trusted issuer (`I`) of the active charter, when `h > 0`: the registry maps `w` to `h`, and the profile of `h` becomes `p`. The state does not change. `none` otherwise (ruled 2026-10-08; `h > 0`: MY CALL 162 (b), O5b) |
| `cast`, `castOrbit`, `homAmend`, `reconstitute`, `canonical` | escrow-lang types | escrow-lang `SPEC.md` section 5, without change |
| `amend F L x s` | `InterestState` | active charter := `gov F L x`. It does not move `mu` or the treasury. Law: `mass` and `Treasury` do not change (a checked equality) |
| `vote h c V` | `Votes` | O2. `Votes := Identity -> Option Ballot`: `none` (EVM code 0) is no ballot. `vote` sets the ballot of `h` to `c`, and no other ballot changes. A second vote replaces the first, and `none` withdraws it. The state does not change. `wtally m V xs` is the image of `m` along the ballots of the listed identities `xs` (`Voters`), counting each identity once even when it is repeated. Laws: `tallyMass`, `repeatedVoters`, `recastMoves`. Image facts on the live measure: `transferMovesVote`, `noDoubleVote` |
| `amendR F L q r xs s e` | `prod (InterestState, Round)` | O2b. `Round := prod (Measure, Votes)`: the snapshot measure and the ballots of a round. `openRound s` is `(bookTotal s.0, noVotes)`: the snapshot at `s` and no ballots. `voteR h c r` is `vote h c` on the ballots of `r`, and the snapshot does not change. W is the total of `wtally r.0 r.1 xs`. When W >= q and W >= 1, active charter := `rule F L` of the tally `seats members (wtally r.0 r.1 xs)`, and the round ends: the result is the new state and `openRound` of the new state. Else `s` and `r` do not change. `e` proves that `seatTally members (wtally r.0 r.1 xs)` has `members` seats (by evaluation, as for `Config`); `seatTally` pads W = 0 with `(members, 0, 0)` solely for this proof, while `seats` gives `(0, 0, 0)`. It does not move `mu` or the treasury. `amendR` replaces the O2 function `amendW` (the live measure and no quorum); `amendW` stays in the prelude until O2b B4. Laws: `seatsTotal`, `seatsQuota`, `amendWeighted`, `repeatedVotersAmend`, `amendKeeps`, `amendEmpty`, `quorumBlocks`, `quorumMet`, `roundResets`, `transferKeepsTally`, `lateMassNoVote`, `noDoubleVoteRound`, `diracDictator`, `diracQuorum` |
| `recover I c h k q s` | `Option InterestState` | ERC-1644 forced recovery (O3): `recoverIn I classA c h k q s` (O4), with `k > 0` and `q <= mass (s.0 classA) h`. R does not gate it, and it does not quotient through the voter orbit. Laws: conservation of supply; the claim of each identity and `Treasury` do not change (a checked equality). Arrow-Debreu only |
| `recoverIn I p c h k q s` | `Option InterestState` | O4. `recover` in partition p: `some` (the book with `debit h q ; credit k q` in partition p) if and only if `c` is a trusted issuer of the active charter, `k > 0` and `q <= mass (s.0 p) h`; otherwise `none`. R does not gate it. `recover I c h k q s` is `recoverIn I classA c h k q s` |

Transfer then distribute: the claim travels with the token. Before a
transfer moves `mu`, the contract checkpoints both identities: it settles
their accrued numerators. Thus a later distribution reads the image measure,
and transfer then distribute equals distribute on the image measure.

The partitions (O4) are the prelude types `Partition` and `Book`.
`examples/arrow-debreu.lang` checks 5 partition laws as equalities
(`EqNat`): `partTotal` (the total mass of h is the sum of its partition
masses), `transferKeepsPart` (a classB transfer moves classB units),
`transferOtherPart` (a classB transfer does not change classA),
`restrictByPartition` (each partition reads its own R) and
`recoverInConserves` (a classB recover keeps the classB mass). A wrong
value in a law gives TYPE_MISMATCH.

The allowance (O5c) is the prelude type `Allowance`.
`examples/arrow-debreu.lang` checks its 6 laws as equalities (`EqNat`):
`approveSets`, `approveOther`, `transferFromMoves`,
`transferFromAllowance`, `transferFromOver` and `transferFromRestricted`.
A wrong value in a law gives TYPE_MISMATCH.

The weighted vote (O2) and its rounds (O2b) are the prelude types `Votes`,
`Voters` and `Round` and the functions `wtally`, `seats`, `vote`,
`openRound`, `voteR` and `amendR`. `examples/arrow-debreu.lang` checks its
15 laws as equalities (`EqNat`, `EqT3` and `EqDec`): `tallyMass`
(the tally total is the sum of the distinct balloted voter masses),
`repeatedVoters` (repeating or reordering identities does not add weight),
`seatsTotal` (the seats sum to `members` when W is not 0),
`seatsQuota` (the largest remainder at (5, 0, 5) and
(3, 0, 7)), `recastMoves` (a second vote and a withdraw),
`amendWeighted` (the seats (2, 0, 1) of a round give `open`),
`repeatedVotersAmend` (repeats do not change the amendment), `amendKeeps`
(`mu` and the treasury do not change), `amendEmpty` (no change at W = 0),
`quorumBlocks` (W = 5 is below the quorum 6: the state and the round do
not change), `quorumMet` (W = 10 meets the quorum: the charter is `open`),
`roundResets` (the next round has no ballots, and its snapshot is the new
state), `transferKeepsTally` (a transfer after the round start does not
change the tally of the round), `lateMassNoVote` (mass that an identity
receives after the round start does not vote) and `noDoubleVoteRound`
(moved mass votes one time, at the ballot of its holder at the snapshot).
It also checks 2 image facts on the live measure: `transferMovesVote` (in
`wtally` of the live measure, the vote moves with the mass) and
`noDoubleVote` (in that tally, moved mass counts one time).
`examples/erc721-dirac.lang` checks `diracDictator` (the one holder gets
all the seats) and `diracQuorum` (at q = 1 and S = 1 the holder alone ends
the round).

The ERC-721 approvals (O5d) are the prelude type `Nft`.
`examples/erc721-dirac.lang` checks its 6 laws as equalities (`EqNat`):
`ownerAfterTransfer` (the owner moves the unit), `approvalClears` (a move
sets the approved identity to 0), `approvedMoves` (the approved identity
moves the unit), `operatorMoves` (an operator of the owner moves the
unit), `strangerRefused` (another caller gets `none`) and `balanceDirac`
(the masses sum to 1 after a move). A wrong value in a law gives
TYPE_MISMATCH.

## 6. Regimes

The representation lands in one regime of `self_governance_trichotomy`
(design section "Three fates, inherited").

| Regime | Aggregation F | Entries |
|---|---|---|
| Arrow-impossibility | none | `deposit`, `withdraw`, `attest` (genesis issuers, O7), views. `transfer`, `distribute`, `cast`, `vote` and `amend` revert. Claims freeze as measures; funds and dust stay in the treasury, not burned and not paid |
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
- **Storage.** The `mu` mapping (MU, slot 0), the registry mapping (wallet
  to identity, slot 1), the profile mapping (slot 2), the active charter
  slot (CHARTER, slot 3), `reserve[rent]` and `reserve[sale]` (RESERVE,
  slot 4), the accrual index (INDEX, slot 5), the checkpoint and numerator
  mappings for each identity (slots 6 and 7), the dust (DUST, slot 8,
  O11), the carrier address (CARRIER, slot 9, token mode only, O5b), the
  allowance mapping (ALLOWANCE, slot 10, keyed by owner then spender
  identity, O5c), the ballot mapping (BALLOT, slot 11, identity to t 2^128
  + c, the code c of 0 to k in the round of tag t, 0 = no ballot), the
  weight mapping (WEIGHT, slot 12, code to mass) (O2), the partition
  mapping (PART, slot 13, keyed by partition code then identity, O4), the
  round counter (ROUND, slot 17, the number of successful amends) and the
  snapshot mapping (SNAP, slot 18, identity to t 2^128 + the mass of the
  identity at the start of the round of tag t) (O2b). The current round
  has the tag t = ROUND + 1. A BALLOT or SNAP word with a different tag is
  no ballot and no snapshot. At the `erc721` facade (O5d), the storage also has
  the owner identity of the one unit (OWNER, slot 14), the approved
  identity (APPROVED, slot 15, 0 = none) and the operator mapping
  (OPERATOR, slot 16, keyed by owner then operator identity). A mapping
  slot is `keccak256(key word, base slot word)`. The slot numbers are in `domain/entries.c`.
- **Code data.** The charter tables (R as a `Cap` table over pairs of
  profiles, the W gates, the issuers) and the verdict table are data in the
  runtime code. The runtime reads them with CODECOPY. The tally index comes
  from the ballot counts.
- **Entries.** In wei mode, `deposit(kind)` is payable. In token mode,
  `deposit(kind, a)` is not payable: it pulls a units of the carrier with
  `transferFrom(caller, this, a)` (O5b). The other entries are
  `distribute(kind)`, `withdraw()`, `transfer(to, q)`, `attest(w, h, p)`,
  `recover(from, to, q)`, `cast(b1..bn)` (a view: the verdict of n seat
  ballots), `vote(c)` and `amend()`. The views are `mass(h)`, `supply()`,
  `claimOf(h)`, `charter()`, `reserve(kind)` and `selfConstituting()` (a
  constant that the compiler computes). The ERC-20 facade adds the views
  `balanceOf(h)` (MU[h], as `mass(h)`) and `totalSupply()` (S). At Debreu
  it also adds `transfer(address,uint256)` (the body of `transfer(to, q)`),
  `approve(spender, v)`, `allowance(owner, spender)` and
  `transferFrom(from, to, q)` (O5c), and the ERC-1400 entries (O4, MY
  CALL 206): the views `balanceOfByPartition(bytes32,address)`
  (PART[p][h]) and `totalSupplyByPartition(bytes32)` (S_p, a code
  constant), and the moves
  `transferByPartition(bytes32,address,uint256,bytes)` (the `transfer`
  move of partition p) and
  `recoverByPartition(bytes32,address,address,uint256)` (the `recover`
  move of partition p). Both moves return p as `bytes32`. For
  `transferByPartition(p, to, q, data)`, the runtime requires only the
  three fixed words p, to and q; it does not read or validate the bytes
  offset or tail. A partition word other than 1 or 2 reverts. In both regimes it adds the views
  `name()` and `symbol()` (ABI strings: the program defs `name` and
  `symbol`, else "interest" and "INT") and `decimals()` (0: the mass is
  whole units, O5c). Each fixed-size argument occupies a 32-byte ABI word;
  partition p has type `bytes32`, and integer arguments have type `uint256`.
  A facade argument of ABI type `address` is an identity word below 2^160;
  the runtime reads it as the identity word.
  The compiler computes each selector as `keccak256` of the signature.
  A ballot is the stored code of the caller identity in the current round
  (O2, O2b). `vote(c)` sets BALLOT[h] := t 2^128 + c and moves the weight
  of h in WEIGHT from the code of the old ballot (no code when its tag is
  not t) to c. The weight of h is the low 128 bits of SNAP[h] when its tag
  is t, else MU[h]: the mass of h at the start of the round. Each move
  (`transfer`, `transferFrom`, `recover`, the two partition moves and the
  moves of the facades) first copies the snapshot of from, then of to
  (SNAP[h] := t 2^128 + MU[h] when the tag of SNAP[h] is not t). Then it
  moves q in MU and in PART[p] (p = classA without a partition word, O4).
  A move does not change WEIGHT or a ballot.
  `amend()` gives the n seats of WEIGHT by the largest remainder (ties to
  the lower code) and reads the verdict table. Then it sets WEIGHT to 0
  and adds 1 to ROUND, so the next round starts with no ballot. The identity of the caller is the registry image of the caller
  address; `to` is an identity (O10).
  At the `erc721` facade (O5d, MY CALLs 214 to 217), the Debreu table is
  `debreu_erc721` (32 rows). It has the entries and views above without
  the ERC-20 write facade (`transfer(address,uint256)`, `approve`,
  `allowance` and `transferFrom`) and without `decimals()`. It adds the
  ERC-721 entries of the one token id TOKEN_ID (1): `ownerOf(uint256)`
  (OWNER), `getApproved(uint256)` (APPROVED), `approve(address,uint256)`,
  `setApprovalForAll(address,bool)`, `isApprovedForAll(address,address)`
  (OPERATOR[owner][operator]), `transferFrom(address,address,uint256)`,
  `safeTransferFrom(address,address,uint256)`,
  `safeTransferFrom(address,address,uint256,bytes)` and
  `supportsInterface(bytes4)` (ERC-165: 1 for 0x01ffc9a7 and 0x80ac58cd,
  else 0). `totalSupply()` is S = 1. The account is the identity, as at
  the ERC-20 facade. `transferFrom` and the two safe forms are
  `transferNft` (section 5). Each move of the unit (q = 1) sets OWNER :=
  `to` and APPROVED := 0; a move of q = 0 does not write OWNER or
  APPROVED. After the move, a safe form calls
  `onERC721Received(caller, from, TOKEN_ID, data)` on `to` when `to` has
  code (the 3-word form gives empty data), and reverts unless the call
  succeeds and the first word of its return data starts with the
  selector 0x150b7a02.
- **Events.** A successful `transfer` and a successful `recover` log one
  `Transfer(from, to, q)` record (LOG3, the ERC-20 event; `from` and `to`
  are identities), also at `q = 0` and at `to = from`. A successful
  `transferByPartition` or `recoverByPartition` then logs one
  `TransferByPartition(p, operator, from, to, q, "", "")` record (LOG4,
  ERC-1410; the operator is the caller address, O4). The creation code
  logs `Transfer(0, h, MU[h])` for each genesis identity with units. A
  successful `withdraw` logs one `Paid(h, wallet, paid, dust)` record
  (LOG3; `h` and the calling wallet are topics, `paid` and the moved dust
  are the data), also at `paid = 0` (O11). A successful `transferFrom`
  logs one `Transfer(from, to, q)` record and no `Approval` record. A
  successful `approve` logs one `Approval(owner, spender, v)` record (LOG3;
  `owner` and `spender` are identities), also at `v = 0` and at an
  unchanged `v` (O5c). At the `erc721` facade (O5d), the `Transfer` record
  of a move is `Transfer(from, to, TOKEN_ID)` (LOG4: the token id is the
  third topic, and there is no data), at q = 1 only; a move of q = 0 logs
  no `Transfer` record. The creation code logs `Transfer(0, owner,
  TOKEN_ID)`. `approve` logs `Approval(OWNER, to, TOKEN_ID)` (LOG4), and
  `setApprovalForAll` logs `ApprovalForAll(owner, operator, b)` (LOG3, b
  in the data). No other entry logs, and a revert logs nothing.
- **Guards.** Short calldata and an unknown selector revert. A non-payable
  entry reverts on value. An erased proof becomes a guard, and a failed
  guard reverts. A refused transfer (R[p] fails, `q > PART[p][h]`, `to = 0`, or
  `to + 1` overflows the registry encoding) reverts and leaves the state
  unchanged. A refused recovery (the caller is not an issuer of the
  active charter, `q > PART[p][from]`, `from = 0` or `to = 0`, or `from + 1`
  or `to + 1` overflows the registry encoding) reverts and leaves the
  state unchanged. Here p is the supplied partition for `transferByPartition`
  and `recoverByPartition`, and classA for `transfer`, `transferFrom` and
  `recover`; `mass h` and MU[h] remain the total across both partitions.
  `attest` reverts at `h = 0`. `vote` reverts at identity
  0, at c > k and on value. `amend()` reverts when the WEIGHT sum (the ballots of
  the current round) is below the quorum q (the def `quorum`; absent: 1, so a sum of 0).
  Identity 0 is no
  identity: the registry keeps `h + 1`, so the word 0 means that the
  wallet has no identity (O5b).
  Like `transfer`, `recover` settles both identities before it moves `mu`.
  A refused `transferFrom` (an `address` word at or above 2^160, `from = 0`,
  a caller with no identity, `q` above the allowance of the caller
  identity, or a refused transfer from `from` to `to`) reverts and leaves
  the state unchanged; `from = spender` also needs an allowance. `approve`
  reverts at an `address` word at or above 2^160, at `spender = 0` and for
  a caller with no identity (O5c).
  At the `erc721` facade (O5d), a token id other than TOKEN_ID, an
  `address` word at or above 2^160, and a `bytes4` word with a byte other
  than 0 after its 4 high bytes revert. `balanceOf` reverts at identity
  0. `approve` reverts unless the caller identity is OWNER or an operator
  of OWNER. `setApprovalForAll` reverts at operator 0 and at b > 1.
  `transferFrom` and the safe forms revert when `from` is not OWNER, when
  the caller identity is not OWNER, APPROVED or an operator of OWNER, or
  when the transfer is refused. The bytes form reverts when the offset or
  the length of the bytes is out of the calldata.
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

C1 (2026-10-11, USER): interest-lang is closed at O2b. Each item below
is RULED, or it is not planned (closed). The post-O5d rows G1 and K6 are
also not planned (closed): G1 (the intrinsic gas of the tests) stays a
known limit of `docs/STATUS.md`, and K6 (the sync of `docs/KIT-DEBT.md`
items 12, 13, 15 and 16 to lang-template) does not occur, so
`docs/KIT-DEBT.md` stays the record of these items.

- O1. A Lean proof of the discrete-D facts of section 4.1 (escrow-lang O1).
  Not planned (closed) 2026-10-11 (USER, C1).
- O2. Ballot authentication and tallies weighted by `mu`. escrow-lang has one
  unweighted ballot for each member. RULED 2026-10-09 (USER), MY CALLs 181
  to 191: the entry `vote(c)` stores one ballot for each identity (the
  registry image of the caller; identity 0 reverts). A second `vote`
  replaces the ballot, and `vote(0)` removes it. `cast(b1..bn)` stays a
  view, and `amend()` takes no argument (181, 182). In O2 the tally is
  live: WEIGHT[c] is the sum of the mass of the identities with the
  ballot c. `transfer`, `transferFrom` and `recover` move the vote with
  the mass, so there is no snapshot and there are no rounds (183). Since
  O2b the tally is the tally of a round on the snapshot mass (see the end
  of this item). `amend()` gives the
  n = `members` seats by the largest remainder (ties go to the lower code)
  and reads the verdict table. The table, KL1 and the differential vectors
  do not change (184). There is no quorum: `amend()` reverts when the
  WEIGHT sum is 0 (185). BALLOT is the mapping at slot 11, and WEIGHT is
  the mapping at slot 12 (186). `vote` logs no event (187). The language
  has `Votes`, `Voters`, `wtally`, `seats`, `vote`, `amendW` and the laws
  of section 5 (188). The Python harnesses that exist test the EVM
  behavior (189). The claims runs keep their sequences, and a new vote run
  is added (190). The slice has five builds, B1 to B5 (191). The B3
  rulings (2026-10-09): the claims model has BALLOT and WEIGHT from B3
  (193), and the unknown-selector revert uses 22 more gas (194). Under
  MY CALL 183 (a), the image law (`transferMovesVote`) replaces the law of
  the O2 kickoff "a transfer after the snapshot does not change the
  tally". Slice O2 (2026-10-10): B1 the language and its laws
  (`examples/arrow-debreu.lang`, `examples/erc721-dirac.lang`), B2 the
  core split of the verdict read (`asm_verdict`), B3 the contract
  (`vote`, `amend()`, BALLOT, WEIGHT and the move of the vote), B4 the
  claims vote run and the vote law calls. O2b RULED 2026-10-11 (USER, MY
  CALLs 237 to 246 (a)) and built in B1 to B4 (build calls 254 (a), 255
  (a), 256 (a), 257 (a)): the tally is the tally of a round on the
  snapshot mass (SNAP slot 18, the lazy copy at the round start). A
  successful `amend()` ends the round: WEIGHT := 0, ROUND (slot 17) :=
  ROUND + 1, and the BALLOT tag makes the old ballots no ballots.
  `amend()` reverts at W < q (def `quorum`; when it is absent, q = 1).
  `transferKeepsTally` is the vote law (241 (a) supersedes 183 (a)), and
  the image law stays as a fact. O2b logs no event (244).
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
  partition. RULED 2026-10-10 (USER), MY CALLs 202 to 210. M0 to M3 have
  one partition and an ERC-3643-shaped registry. Slice O4 (2026-10-10):
  B1 the partitions in the language and the program data, B2 the
  partition mapping PART in the contract storage (each move also moves
  PART[p]), B3 the 4 ERC-1400 entries (`balanceOfByPartition`,
  `totalSupplyByPartition`, `transferByPartition` and
  `recoverByPartition`), and B4 8 mutants by hand and the docs. The
  ERC-721 shape is an example program with `S = 1` (a Dirac measure);
  O5d gives it the ERC-721 ABI.
  Votes by partition (O4b) and payouts by partition (O4c) are not planned
  (closed) 2026-10-11 (USER, C1).
- O5. An ERC-20 asset carrier (R3 defers it), and an ERC-20 or ERC-3643 ABI
  facade with events. O5a (2026-10-09): the read facade and the `Transfer`
  event. O5b (2026-10-09): the ERC-20 asset carrier (R3 re-ruled,
  section 4). O5c (2026-10-09): the write facade. O5d (2026-10-11): the
  ERC-721 ABI at the `erc721` facade (RULED 2026-10-10, MY CALLs 213 to
  224): B1 the language (`Facade`, `Nft`, `transferNft` and the 6 laws of
  `examples/erc721-dirac.lang`) and the program data, B2 the entry table
  `debreu_erc721` (OWNER, APPROVED, OPERATOR and the LOG4 `Transfer`), B3
  the two `safeTransferFrom` forms and the `onERC721Received` check, and
  B4 the claims 721 model, its law calls and 8 mutants by hand.
  O5e (ERC-721 with S > 1: the token ids 1 to S) and O5f (ERC-721
  metadata: `tokenURI`) are not planned (closed) 2026-10-11 (USER, C1).
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
  charter as definitions with fixed names, the same way as `members`
  (RULED as built 2026-10-11, USER, C1). RULED 2026-10-08 (USER): the
  constructors are `Juris` =
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
| O2b | Rounds: the tally of a round on the snapshot mass, the round end in `amend()` and the quorum (MY CALLs 237 to 246 (a)); builds B1 to B4 | done 2026-10-11 |
| C1 | The close at O2b: section 9 marks G1, K6, O1, O4b, O4c, O5e and O5f not planned (closed), and the O9 clause on the fixed definition names RULED as built; `docs/KIT-DEBT.md` stays the record of the differences from the kit | done 2026-10-11 (closed) |

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
(`docs/VALIDATION.md`). After O5c (2026-10-09), `make check` passes with
parse 28, check 61, build output 29, EVM boundaries 4, metadata 56 ABI
comparisons, refusal 49, normal forms 10, differential 27 and 64 vectors,
domain tests 11, settlement 240 cases with 9 deploys (245 EVM calls under
the gas ceiling), the same claims and token runs, and an allowance run of
10 sequences (200 steps) with 27 law calls, 0 failures. After O2 (2026-10-10),
`make check` passes with parse 28, check 61, build output 29, EVM
boundaries 4, metadata 56 ABI comparisons, refusal 49, normal forms 10,
differential 27 and 64 vectors, domain tests 11, settlement 253 cases with
9 deploys (258 EVM calls under the gas ceiling), claims 20 sequences (530
steps) with 126 law calls and 87 contract checks, the same token and
allowance runs, and a vote run of 10 sequences (300 steps), 0 failures.
After O4 (2026-10-10), `make check` passes with parse 28, check 62, build
output 29, EVM boundaries 4, metadata 56 ABI comparisons, refusal 51,
normal forms 10, differential 27 and 64 vectors, domain tests 11,
settlement 293 cases with 9 deploys (298 EVM calls under the gas
ceiling), and the same claims, token, allowance and vote runs, 0 failures.
After O5d (2026-10-11), `make check` passes with parse 28, check 73,
build output 29, EVM boundaries 4, NFT boundaries 16, metadata 56 ABI
comparisons, refusal 51, normal forms 10, differential 27 and 64 vectors,
domain tests 11, settlement 346 cases with 10 deploys (353 EVM calls
under the gas ceiling), claims 20 sequences (530 steps) with 198 law
calls and 87 contract checks, and the same token, allowance and vote
runs, 0 failures.
The kit debt of
`docs/KIT-DEBT.md` is applied in lang-template `fa1131a`, and `a2ce1b8`
is the first commit of
this tree. I6 removed the two differences from the kit that
`docs/KIT-DEBT.md` records: the program data uses the kit hooks
`lang_domain_read` and `lang_domain_print`, and a failed write to stdout
gives `IO_WRITE`. C1 (2026-10-11) closed interest-lang at O2b, so no
work is open (`docs/STATUS.md`).
