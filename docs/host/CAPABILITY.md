# TinyCC EVM DAO host capability

This file holds host facts only. Put the facts of your domain in the
`probe/CAPABILITY.md` of your language. Paths are relative to the host
kit, or to the root of a generated language.

## Toolchain

- tcc 0.9.28rc 2026-09-04 mob@0fb54300 (AArch64 Darwin) builds
  `build/interestc` with `-std=c99 -Wall -Werror -Isrc` (`Makefile`).
- tcc has no `-Wswitch-enum`. `make check-clang` checks the same sources
  with `cc -std=c99 -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only`, so
  a switch that does not name each enumerator fails the gate.
- `tcc -run gen/embed.c domain/domain.lang build/domain.c` embeds the
  prelude at build time. The prelude is part of the executable.
- `interestc` uses only the C standard library.
- The gate needs geth `evm` 1.14.12-stable, foundry `cast`, `python3` and
  `rg`. `test/settlement.py` runs `tcc -Isrc src/evm.c src/keccak.c
  domain/entries.c -run test/evmtool.c`.

## Kernel

The prelude retains the family and recursion restrictions of the assay
kernel probe P2 (2026-10-06). The C checker supports Sigma projections and
eta conversion, as described below.

- A constructor of a `mu` family with a parameter cannot appear in a term.
  A family with a type index lives in `Type 1`, and its `match` cannot use
  a function on the outer type. Thus there is one equality family for
  each index type (`EqNat`, `EqDec`, `EqTally`) and one list family for
  each element type (`Ballots`, `Holders`). `Option A` and `Sum A B` are
  definitions over the built-in `sum`. Monad, `fold` and `filter` have one
  set for `Ballots`; `Holders` has only its constructors.
- There is no `unfold`. Built-in `Nat` has no eliminator, so fuel cannot
  be a `Nat`. Recursion comes only from structural `def rec` (`fold`).
  `nu` is a refused form.
- There is no Sigma pattern. The C checker accepts dependent projections
  `.0` and `.1` and eta conversion, including
  `fun (s : S) => (s.0, s.1)` for `S := (n : Nat) * EqNat n 3`.
  `Config`, `Tally` and `Aggregation F` remain `mu` records read by `match`.
- `Nat` is a 64-bit word. `natAdd 18446744073709551615 1` is refused with
  `TYPE_NAT` (`test/refusal.sh`).

## Limits

- Stack depth (measured on escrowc, 2026-10-07): the checker evaluates by
  C recursion. On an 8 MB main-thread stack the tcc build crashed between
  depth 12288 and 16384. Thus `CHECK_DEPTH` is 4096 (`src/check.c:16`).
- Fuel: 2^24 evaluation steps for each declaration, each tally and each
  ballot vector (`src/check.c:15`). Past the depth or the fuel the checker
  refuses the program with `TYPE_FUEL` (exit 1). It does not crash.
- Memory: one arena for each run, 256 MiB (`src/syntax.h:37`). Source
  files are at most 1 MiB. The parser depth guard is 512.
- `table` takes at most 1000 members and 501501 tallies (`TABLE_LIMIT`).
  `verdicts` takes at most 59049 ballot vectors (`VERDICT_LIMIT`).

## Output

- `interestc build` writes the creation code, or the runtime code with
  `--runtime`, as hex. The creation code copies the runtime. The runtime
  is the dispatcher, the entries and then the verdict table, which
  `asm_tally` reads with `CODECOPY`.
- A ballot with code j < k weighs (n+1)^(j-1). Code k weighs 0. The sum is
  the table index, so the table has n(n+1)^(k-2) + 1 bytes (at most 4096,
  `EVM_TABLE`). Each ballot must be in 1 to k, or the call reverts.
- `amend` returns the code of each tally packed in one word, with
  ceil(log2(k+1)) bits for each code (k = 3: 2 bits). Thus the members
  limit of Arrow-Debreu depends on k: C(n+k-1, k-1) codes must fit 256
  bits (k = 3: 14 members, k = 4: 6 members; `EVM_LIMIT`).
- At a discrete decision space `gov F L` agrees with `F` on each
  configuration. Thus the canonical `amend` changes no verdict, and the
  packed word is the verdict table at the canonical amendment.
- Arrow-impossibility has no verdict table. Its writer takes the full
  member range of the checker and requires no decision codes.
- The runtime code is at most 24576 bytes (EIP-170) and the code buffer
  is 8192 bytes (`EVM_SIZE`).

## Gates (2026-10-07)

`make clean && make check`: exit 0, `gate: 0 failures`. parse, check,
refusal, normal-forms and domains all pass. `test/differential.py` at
k = 3: `vectors=27 codes=111123133123222323133323333`, geth agrees with
`interestc`. At k = 4 (`plural4.lang`): 64 vectors, `amend`, `cast` and
`settle` in geth agree with `interestc`. `test/settlement.py`: `cases=60
deploy=2`, geth agrees with the model.
