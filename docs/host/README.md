# TinyCC EVM DAO host kit

This kit compiles a small dependently typed language for a
self-constituting DAO to EVM bytecode. A C front end (lexer, parser and
NbE checker) and an EVM writer build with TinyCC into one executable,
`build/interestc`. The checker evaluates the constitution of the program at
each tally. The writer puts these decisions into a verdict table in the
contract. There is no IR.

From this kit directory, or from a generated language directory:

```sh
make check
build/interestc check examples/arrow-debreu.lang
build/interestc table examples/arrow-debreu.lang
build/interestc verdicts examples/arrow-debreu.lang F
build/interestc build examples/arrow-debreu.lang -o debreu.hex
build/interestc build examples/arrow-debreu.lang --runtime -o debreu-runtime.hex
```

The build uses TinyCC (`TCC`, default `tcc`). `make check-clang` also
compiles the sources with `cc -Wall -Wextra -Wswitch-enum -Werror
-fsyntax-only` (`CC`, default `cc`). The gate needs `python3`, geth `evm`,
foundry `cast` and `rg`. The versions are in `docs/CAPABILITY.md`.

Names in comments: "host README" is this file (`docs/host/README.md` in a
generated language). "host CAPABILITY.md" is `docs/CAPABILITY.md`
(`docs/host/CAPABILITY.md`). "host FORMERS.md" is `FORMERS.md`
(`formers/tcc-evm-dao.md`).

## Commands

| Command | Result |
|---|---|
| `interestc check PROG` | Checks the program. Prints `ok debreu` or `ok impossibility` (the regime). |
| `interestc table PROG` | Prints the regime, the member count and the decision code of each tally, on one line. |
| `interestc verdicts PROG NAME` | Prints one decision code for each ballot vector of the ChoiceRule `NAME`, compact digits for k <= 9 or space-separated decimal codes for k >= 10. |
| `interestc eval PROG NAME` | Prints the normal form of `NAME`. |
| `interestc data PROG` | Prints the program data that the domain reads (`lang_domain_print`, The domain): one line each for `start`, `charters`, `genesis`, `restrict`, `waterfall` and `issuers`. |
| `interestc build PROG [--runtime] -o OUT` | Checks the program and writes the creation code (or the runtime code with `--runtime`) to `OUT` as lowercase hex, without `0x`, with a final newline. `--runtime` comes before `-o`. |

A refused build preserves an existing `OUT` and does not create an absent
`OUT`. An output path that names the source, including a hard link or
symlink, is rejected with `IO_WRITE` and exit 2. A failed write to stdout
gives `interestc: IO_WRITE: -: stdout: message` and exit 2.

Exit 0 is success. Exit 1 is a refused program. Exit 2 is a usage or IO
error. A refusal writes one line to stderr: `interestc: CODE: NAME: message`.

## Layout

| Path | Part | Content |
|---|---|---|
| `src/` | core | Lexer, parser, printer, checker (`check.c`), EVM core (`evm.c`), the assembler API for domains (`asm.h`), keccak, arena, diagnostics |
| `domain/domain.lang` | domain | The prelude: the types and operations of the language. `gen/embed.c` embeds it in `build/interestc` at build time. |
| `domain/entries.c` | domain | The storage layout, the contract entries of each regime and the program data hooks |
| `domain/data.h` | domain | The program data struct `LangDomainData` and its reader `lang_data` (`src/check.c`) |
| `examples/` | sample | The three sample programs: one for each regime, and an ERC-721 Dirac measure |
| `test/` | gate | `test/gate.sh` and its tests |

A new language edits `domain/domain.lang` and `domain/entries.c`. It does
not edit `src/`, except for a program data reader that needs the checker
internals (The domain).

## The domain

A program holds `def members : Nat := N` (N >= 1) as its first
declaration. The checker reads the members declaration, the prelude and
then the rest of the program. A program cannot declare a `mu`, declare a
`def rec` or redeclare a prelude name. Thus each family and each recursive
function is in `domain/domain.lang`. Each `-- @section` line of the
prelude starts one section. Sections 1 to 6 are the governance core
(decisions, equality, Option and Sum, ballots, tallies, constitution,
aggregation and amendment). Sections 7 to 11 are the interest-lang claim algebra (SPEC sections 4 and 5). The contract entries in `domain/entries.c` are the EVM form of the section 11 operations (see The interest domain below).

The decision space D is the codomain of `ChoiceRule` in the prelude. It
must be a `mu` with no indices and 2 to 64 nullary constructors
(`TABLE_DECISION`). Let k be the constructor count. Code j is constructor
j, from 1, in declaration order. A ballot is a code in 1 to k. A tally
counts each code over the n members. The tallies are the compositions of
n into k parts, in lexicographic order on parts 1 to k-1. The last part is
the remainder. There are C(n+k-1, k-1) tallies.

The regime comes from the program. A program that defines an
`Aggregation G` for a ChoiceRule `G` of the program is Arrow-Debreu: at a
discrete decision space an aggregation of `G` is an orbit rule and a proof
that `G` factors through it. The writer puts the decision of each tally in
a verdict table. Any other program is Arrow-impossibility, with no verdict
table.

`domain/entries.c` gives `lang_domain_entries(regime, &count)` from
`src/asm.h`: the entries of a regime in dispatch order (1 to 16). Each
`Entry` has:

- `name`: the function name of the selector;
- `words`: the count of `uint256` calldata words before the ballots;
- `ballots`: 1 if n ballot words follow (Arrow-Debreu only), else 0;
- `payment`: `ENTRY_PAYABLE` or `ENTRY_NONPAYABLE`;
- `emit`: the function that writes the body.

The core writes the dispatcher, then for each entry a head (callvalue
guard unless payable, calldata size check), then calls `emit`. An unknown
selector reverts. `emit` gets an `EntryContext`: `members` (n), `decisions`
(k, or 0 in Arrow-impossibility), `packed` (the amend word, Arrow-Debreu
only; NULL when the codes do not fit one word) and `data` (the program data, below). The core entries `lang_entry_cast` and `lang_entry_amend` can go in
a list. The `asm_*` helpers of `src/asm.h` write opcodes, pushes, labels
(64 for each contract), jumps, calldata words, mapping slots
(keccak(key . slot)), checked addition, memory words, an address guard and
`asm_tally` (the decision code of the n ballots from the verdict table).
Memory 0x00 to 0x3f is core scratch. A domain uses 0x80 and up.

`test/domains/k4.lang` and `test/domains/decision-arg.lang` are the sample
prelude with a fourth decision value (nullary, then with an argument).
They test k = 4 and `TABLE_DECISION`. A new domain must update them, or
remove them from `DOMAINS` in the `Makefile`. The small
`test/domains/review-k10-domain.lang` checks decimal verdict output for
k = 10. `test/settlement.py`, the entry sequences of `test/differential.py`
and `test/claims.py` (the reference model of the interest operations) model
the interest domain. A new domain replaces them.

The program data of a domain is data that the program gives and the
contract keeps. For this domain it is the start charter, the genesis rows,
the R caps, the W gates and the issuer pairs (SPEC section 7). The core
does not know its form. `src/evm.h` declares `LangDomainData`, and
`domain/data.h` defines the struct. The core passes only a pointer
(`LangContract.data` and `EntryContext.data`; NULL gives the defaults).
`domain/entries.c` gives four hooks:

- `lang_domain_read(checked, &data)` (`src/check.h`) reads the data from
  the checked program. `interestc data` and `interestc build` call it after
  the check. It calls `lang_data` (`src/check.c`), which reads the program
  defs `start`, `genesis`, `restrict`, `waterfall` and `issuers`, and gives
  the defaults for each absent def.
- `lang_domain_print(data, out)` (`src/check.h`) writes the data for
  `interestc data`.
- `lang_domain_genesis(a, contract)` (`src/asm.h`) writes the genesis
  storage in the creation code, before the runtime copy.
- `lang_domain_data(a, c)` (`src/asm.h`) writes the code data of the
  runtime at `LABEL_DATA`, after the entries and the verdict table, so that
  no data byte comes before code.

`lang_data` must evaluate program definitions, so it is in `src/check.c`
and not in `domain/`.

## The interest domain

The decision space is `Charter` = {open, restricted, frozen}, so k = 3. The
active charter selects the R caps and the W gates.

Storage (mappings live at keccak(key . slot)):

| Slot | Name | Contents |
|---|---|---|
| 0 | MU | identity to units |
| 1 | REGISTRY | wallet to identity + 1 (0: no identity) |
| 2 | PROFILE | identity to profile code 0 to 3 |
| 3 | CHARTER | the active charter code 1 to K |
| 4 | RESERVE | kind code (0 rent, 1 sale) to wei |
| 5 | INDEX | the sum of the distributed wei |
| 6 | CHECKPOINT | identity to INDEX at its last settle |
| 7 | NUM | identity to its claim numerator, in units of 1/S wei |
| 8 | DUST | the treasury dust, in units of 1/S wei (less than S) |

S, the sum of the genesis units, is a code constant. The code data at
`LABEL_DATA` holds the R limit words (rows x 4 x 4: charter code - 1, the
profile of the sender, the profile of the receiver; "admits q" is q < L),
then the W gate words (rows x 2: 1 pass, 0 retain). rows is the larger of
k and K. id(CALLER) is REGISTRY[CALLER] - 1; a caller with no identity
reverts. claimOf(h) is NUM[h] + MU[h] x (INDEX - CHECKPOINT[h]), with a
revert on a wrap. To settle h is to set NUM[h] := claimOf(h) and
CHECKPOINT[h] := INDEX.

| Entry | Regime | Calldata | Effect |
|---|---|---|---|
| `deposit` (payable) | both | kind | Kind above 1 reverts. RESERVE[kind] += callvalue (checked). Returns the new reserve. |
| `distribute` | Arrow-Debreu | kind | S = 0 reverts. d = RESERVE[kind] if the W gate of (CHARTER, kind) is pass, else 0. RESERVE[kind] -= d, INDEX += d (checked). Returns d. |
| `withdraw` | both | none | S = 0 reverts. Settles id(CALLER). paid = NUM / S and r = NUM mod S. If paid > 0, NUM := 0 and x = DUST + r; else NUM := r and x = DUST. RESERVE[0] += x / S (checked), DUST := x mod S. Logs `Paid`. All stores and the log occur before a CALL of paid wei to the caller. A failed CALL reverts. Returns paid. |
| `transfer` | Arrow-Debreu | to, q | `to + 1` overflowing the registry encoding reverts. q >= R[CHARTER][PROFILE h][PROFILE to] reverts. q > MU[h] reverts. Settles h, then to. MU[h] -= q, then MU[to] += q. Returns 1. |
| `attest` | both | w, h, p | The caller must be an issuer of the active charter. w >= 2^160 or p > 3 reverts. REGISTRY[w] := h + 1 (checked), PROFILE[h] := p. Returns 1. |
| `cast` | Arrow-Debreu | n ballots | Returns the charter code of the ballots. Writes nothing. |
| `amend` | Arrow-Debreu | n ballots | CHARTER := the charter code of the ballots. Returns it. |
| `mass`, `claimOf` | both | h | MU[h]; claimOf(h). |
| `supply`, `charter` | both | none | S; CHARTER. |
| `reserve` | both | kind | Kind above 1 reverts. RESERVE[kind]. |
| `selfConstituting` | both | none | 1 in Arrow-Debreu, 0 in Arrow-impossibility. |
| `balanceOf` | both | h (ABI type `address`) | MU[h], as `mass` (ERC-20 facade). |
| `totalSupply` | both | none | S (ERC-20 facade). |

Events: a successful `transfer` or `recover` logs `Transfer(from, to, q)`
(LOG3; topic 0 = keccak256("Transfer(address,address,uint256)"), the
identities in topics 1 and 2, q in the data). The creation code logs
`Transfer(0, h, MU[h])` for each identity with units. A successful
`withdraw` logs `Paid(h, wallet, paid, dust)` (LOG3; topic 0 =
keccak256("Paid(uint256,address,uint256,uint256)"), h and the calling
wallet in topics 1 and 2, paid and the moved dust in the data), also at
paid = 0.

Genesis: CHARTER := the start charter (1 with no program data). Each
genesis row writes REGISTRY[wallet] := identity + 1. Each identity gets MU
:= the sum of its units and PROFILE := the profile of its last row. Zero
words are not written. Repeated wallets are refused with `CONTRACT_GENESIS`;
distinct wallets may share an identity, with their units accumulated.

Limits of the domain (not of the core): `distribute` and `withdraw` are
open to all callers. A `withdraw` that pays 1 wei or more moves the
remainder NUM mod S to DUST, and the whole wei of DUST go to the rent
reserve, so less than 1 wei stays in DUST. A `withdraw` that pays 0 wei
keeps the remainder for the identity. `withdraw` sends to the calling
wallet, and the identity can have more than one wallet.

## Refusals

| Codes | Cause |
|---|---|
| `REFUSE_MEMBERS` | The first declaration is not `def members : Nat := N` with N >= 1 |
| `REFUSE_MU`, `REFUSE_REC`, `REFUSE_PRELUDE_NAME` | The program declares a `mu`, a `def rec` or a prelude name |
| `REFUSE_FORM` | A refused assay form: `nu`, `axiom`, `contract`, `storage`, `entry`, `payable`, `constructor`, `fallback`, `error`, `invariant`, `predicate`, `proof`, `guard`, `sload`, `sstore` |
| `LEX_TOKEN`, `LEX_NUMBER`, `PARSE_EXPECT`, `PARSE_PAREN`, `PARSE_ARITY`, `PARSE_DEPTH` | Lexer and parser errors |
| `TYPE_SCOPE`, `TYPE_DUPLICATE`, `TYPE_MISMATCH`, `TYPE_SHAPE`, `TYPE_ERASED`, `TYPE_MATCH`, `TYPE_UNIVERSE`, `TYPE_INFER`, `TYPE_REC`, `TYPE_MU`, `TYPE_NAT`, `TYPE_FUEL` | Checker errors. `TYPE_NAT` is a Nat overflow. `TYPE_FUEL` is out of fuel or too deep. |
| `TABLE_DECISION`, `TABLE_STUCK`, `TABLE_LIMIT` | D is not a valid decision space, the rule does not reduce at a tally, or too many members or tallies |
| `VERDICT_TYPE`, `VERDICT_LIMIT` | `NAME` is not a ChoiceRule, or k^n is more than 59049 |
| `CONTRACT_TYPE`, `CONTRACT_VALUE`, `CONTRACT_GENESIS` | A program-data definition has the wrong type or value, or the genesis list is invalid (including a repeated wallet) |
| `EVM_LIMIT`, `EVM_TABLE`, `EVM_SIZE` | Too many members for the verdict table, a bad verdict table, or too much code, too many jump label sites or a too long entry signature |
| `EVM_INTERNAL`, `EVM_USAGE`, `EVM_IO` | A writer fault, a bad writer call, or an output error |
| `MEMORY`, `IO_READ`, `IO_SIZE`, `IO_WRITE`, `USAGE`, `TYPE_INTERNAL` | Arena full, file errors, bad arguments, checker fault |

`test/refusal.sh` holds one program for each `REFUSE_*` code and one for
each checker error.

## Gate

`make check` builds `build/interestc`, `build/parsetool` and the test domain
compilers, runs `make check-clang`, then `test/gate.sh`. The gate runs, in
order: `test/parse.sh`, `test/embed-safety.sh`, `test/check.sh`,
`test/build-output.sh` (refused builds, source aliases and failed stdout
writes), `test/refusal.sh`,
`test/normal-forms.py`, `test/differential.py` (k = 3, geth against
`interestc table` and `interestc verdicts`), `test/domains.sh`,
`test/differential.py --interestc build/k4/interestc --decisions 4 --program
test/domains/plural4.lang`, `test/settlement.py` (entry cases in geth),
`test/claims.py` (operation sequences in geth against the reference model,
the SPEC M2 law vectors and the contract checks of the refusal list) and a
scan for en and em dashes. It ends with `gate: 0 failures`.
`make clean` removes `build/` and `.gatework/`.

## Limits

| Limit | Value | Source |
|---|---|---|
| Source size | 1 MiB | `src/syntax.h:36` |
| Arena | 256 MiB | `src/syntax.h:37` |
| Parser nesting | 512 | `src/syntax.h:35` |
| Checker depth | 4096 | `src/check.c:17` |
| Checker fuel, for each declaration, tally and ballot vector | 2^24 steps | `src/check.c:16` |
| Members for `table` | 1000 | `src/check.c:19` |
| Tallies for `table` | 501501 | `src/check.c:20` |
| Ballot vectors for `verdicts` | 59049 (3^10) | `src/check.c:18` |
| Decision values k | 2 to 64 | `src/evm.h:7` |
| Members for `build` (Arrow-Debreu) | largest n with a verdict table of at most 4096 bytes (k = 3: 63, k = 4: 15) | `src/evm.c:393` |
| Verdict table | n(n+1)^(k-2) + 1 bytes, at most 4096 | `src/evm.c:384` |
| Code buffer, fixups, labels | 24576 bytes, 512, 64 | `src/asm.h:17` |
| Entry signature text | 512 bytes | `src/evm.c:26` |
| Entries for each regime | 16 | `src/evm.c:22` |
| Runtime code | 24576 bytes (EIP-170) | `src/evm.c:22` |

## Origin

This kit is a fork of escrowc at escrow-lang commit `b55630b` (the
`Makefile`, `src`, `test`, `tools`, `prelude` and `examples` trees). The
escrow-lang docs are not in the kit. Their host notes that still apply are
in `docs/CAPABILITY.md`. Changes after the fork:

- Names follow the sibling TinyCC kits: `build/langc`, the diagnostic
  prefix `langc: `, `.lang` files, `domain/domain.lang` embedded by the
  shared `gen/embed.c`, the targets `build`, `check`, `check-clang` and
  `clean`, and `test/gate.sh`.
- The decision space is the codomain of `ChoiceRule` with k values, not a
  fixed three. The tally, the verdict table, the ballot weights and the
  amend packing take k.
- The EVM writer is split: `src/evm.c` is the core, `src/asm.h` is its API
  for domains, and `domain/entries.c` holds the escrow entries.
- interest-lang renames the kit binary `build/langc` to `build/interestc`
  and the diagnostic prefix `langc: ` to `interestc: ` (SPEC O8). The C
  names `lang_*`, `LANG_*` and `Lang*` and the fresh-name prefix `langq`
  stay the kit names.

At k = 3 the tables, the creation code and the runtime code of both
examples are byte-identical to escrowc at `b55630b`.

`FORMERS.md`, copied to `formers/tcc-evm-dao.md` in a generated language,
gives the status of each former. `docs/CAPABILITY.md` gives the host
facts.
