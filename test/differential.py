#!/usr/bin/env python3
"""Compare the interestc checker with the contract that interestc writes.

The checker side is `interestc verdicts PROG F`: one digit per ballot vector,
in the order of itertools.product over the codes 1 to k (k = 3 unless
`--decisions K`), with the first ballot outermost. The contract side deploys the creation code of `interestc
build` in geth evm (its genesis storage must match `interestc data`), then
runs `cast` and `amend` on each ballot vector. Each result must equal the
checker digit, and `amend` must write that code to the CHARTER slot and
nothing else. No verdict comes from Python. Run `make` first.

usage: python3 test/differential.py [--program PROG] [--interestc INTERESTC --decisions K]
"""
import argparse
import itertools
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys

import settlement as S

ROOT = Path(__file__).resolve().parent.parent
INTERESTC = ROOT / 'build/interestc'
WORK = ROOT / '.gatework/differential'
PROGRAM = ROOT / 'examples/arrow-debreu.lang'
CODES = (1, 2, 3)


def interestc(*args, lines=1):
    result = subprocess.run([str(INTERESTC), *map(str, args)], text=True,
                            capture_output=True, timeout=120)
    S.require(result.returncode == 0 and result.stderr == '',
              f'interestc {args[0]}: exit {result.returncode}: {result.stderr.strip()[:400]}')
    S.require(result.stdout.count('\n') == lines and result.stdout.endswith('\n' * lines),
              f'interestc {args[0]}: not {lines} line(s) on stdout')
    return result.stdout.strip()


def members(program):
    first = program.read_text().splitlines()[0]
    found = re.fullmatch(r'def members : Nat := ([0-9]+)', first)
    S.require(found is not None, f'line 1 of {program} must be def members : Nat := N')
    return int(found.group(1))


def table(program, size):
    words = interestc('table', program).split()
    S.require(words[:2] == ['debreu', str(size)], f'table is not debreu {size}: {words[:2]}')
    codes = tuple(map(int, words[2:]))
    tallies = math.comb(size + len(CODES) - 1, len(CODES) - 1)
    S.require(len(codes) == tallies and set(codes) <= set(CODES),
              f'table has {len(codes)} codes, not one code in 1..{len(CODES)} per tally')
    return codes


def checker_verdicts(program, vectors):
    digits = interestc('verdicts', program, 'F')
    S.require(len(digits) == len(vectors) and set(digits) <= set(''.join(map(str, CODES))),
              f'verdicts gave {len(digits)} digits for {len(vectors)} vectors')
    return dict(zip(vectors, map(int, digits)))


def contract(program):
    def part(name, *flags):
        out = WORK / f'{name}.hex'
        interestc('build', program, *flags, '-o', out, lines=0)
        return out.read_text().strip()

    creation, runtime = part('creation'), part('runtime', '--runtime')
    S.deploy('differential-deploy', creation, runtime, S.data_storage(program, binary=INTERESTC))
    return runtime


def main():
    global INTERESTC, CODES, WORK
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--program', type=Path, default=PROGRAM)
    parser.add_argument('--interestc', type=Path, default=INTERESTC)
    parser.add_argument('--decisions', type=int, choices=range(2, 10), default=len(CODES))
    args = parser.parse_args()
    program = args.program.resolve()
    INTERESTC, CODES = args.interestc.resolve(), tuple(range(1, args.decisions + 1))
    WORK = WORK / program.stem
    S.require(shutil.which('evm') and shutil.which('cast'), 'evm and cast are required')
    S.require(INTERESTC.exists(), f'{INTERESTC} is missing: run make')
    WORK.mkdir(parents=True, exist_ok=True)
    S.WORK = WORK
    size = members(program)
    vectors = tuple(itertools.product(CODES, repeat=size))
    table(program, size)
    verdicts = checker_verdicts(program, vectors)
    runtime = contract(program)
    for index, (vector, code) in enumerate(verdicts.items()):
        S.expect(f'differential-cast-{index}', runtime, S.data('cast', *vector), {}, {}, code)
        S.expect(f'differential-amend-{index}', runtime, S.data('amend', *vector), {},
                 {S.CHARTER: code}, code)
    print(f'DIFFERENTIAL vectors={len(vectors)} '
          f'codes={"".join(map(str, verdicts.values()))} cast,amend geth=interestc OK '
          f'(logs: {WORK})')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'DIFFERENTIAL FAIL: {error}', file=sys.stderr)
        sys.exit(1)
