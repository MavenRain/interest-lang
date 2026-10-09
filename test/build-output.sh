#!/bin/sh
# Failed builds preserve files, and failed stdout writes must report an IO error.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
interestc=$root/build/interestc
out=$root/build/test-output
mkdir -p "$out"
failures=0
checks=0

status() {
  name=$1 wanted=$2
  shift 2
  "$@" > "$out/stdout" 2> "$out/stderr"
  actual=$?
  checks=$((checks + 1))
  if [ "$actual" -ne "$wanted" ] || [ -s "$out/stdout" ]; then
    printf 'FAIL %s: exit %s, expected %s\n' "$name" "$actual" "$wanted"
    failures=$((failures + 1))
  fi
}

same() {
  checks=$((checks + 1))
  if ! cmp -s "$2" "$3"; then
    printf 'FAIL %s: file changed\n' "$1"
    failures=$((failures + 1))
  fi
}

cat > "$out/good.lang" <<'EOF'
def members : Nat := 3
EOF
cat > "$out/limited.lang" <<'EOF'
def members : Nat := 15
def F : ChoiceRule := fun (x : Config) => open
def agg : Aggregation F := mkAgg F (fun (t : Tally) => open) (fun (x : Config) => reflDec open)
EOF
for part in creation runtime; do
  if [ "$part" = runtime ]; then set -- --runtime; else set --; fi
  status "$part reference build" 0 "$interestc" build "$out/good.lang" "$@" -o "$out/reference.hex"
  cp "$out/reference.hex" "$out/existing.hex"
  status "$part refused build" 1 "$interestc" build "$out/limited.lang" "$@" -o "$out/existing.hex"
  same "$part artifact after refusal" "$out/reference.hex" "$out/existing.hex"
  rm -f "$out/absent.hex"
  status "$part refused new output" 1 "$interestc" build "$out/limited.lang" "$@" -o "$out/absent.hex"
  checks=$((checks + 1))
  if [ -e "$out/absent.hex" ]; then
    printf 'FAIL %s refused build created output\n' "$part"
    failures=$((failures + 1))
  fi
  printf 'old artifact\n' > "$out/existing.hex"
  status "$part replaces artifact" 0 "$interestc" build "$out/good.lang" "$@" -o "$out/existing.hex"
  same "$part replacement bytes" "$out/reference.hex" "$out/existing.hex"
done

cp "$out/good.lang" "$out/source.lang"
status "source as output" 2 "$interestc" build "$out/source.lang" -o "$out/source.lang"
same "source preserved" "$out/good.lang" "$out/source.lang"
for alias in hardlink symlink; do
  cp "$out/good.lang" "$out/source.lang"
  rm -f "$out/alias.lang"
  if [ "$alias" = hardlink ]; then
    ln "$out/source.lang" "$out/alias.lang"
  else
    ln -s "$out/source.lang" "$out/alias.lang"
  fi
  status "$alias source as output" 2 "$interestc" build "$out/source.lang" --runtime -o "$out/alias.lang"
  same "$alias source preserved" "$out/good.lang" "$out/source.lang"
done

# Exercise the data hook with an unbuffered stdout too. Then fflush gives 0,
# and only ferror(stdout) shows the failed write. build/interestc cannot make
# stdout unbuffered, so the fixture wraps the formatter of the domain. The
# unbuffered case writes one line with fputs, as the kit fixture does: after
# a failed fputc, the stdio of macOS keeps the byte, and fflush fails again.
cat > "$out/data-domain.c" <<'EOF'
#define lang_domain_print domain_print
#include "domain/entries.c"
#undef lang_domain_print
#include <stdlib.h>
void lang_domain_print(const LangDomainData *data, FILE *out) {
  if (getenv("LANG_TEST_OUTPUT_UNBUFFERED") == NULL) {
    domain_print(data, out);
    return;
  }
  setvbuf(out, NULL, _IONBF, 0);
  fputs("domain data\n", out);
}
EOF
set --
for source in main arena diag lexer parser printer check evm keccak; do
  set -- "$@" "$root/src/$source.c"
done
if ! "${TCC:-tcc}" -std=c99 -Wall -Werror -I"$root/src" -I"$root" \
  -o "$out/data-interestc" "$@" "$out/data-domain.c" "$root/build/domain.c"; then
  exit 1
fi
python3 - "$root" "$out" <<'PY'
import os
from pathlib import Path
import subprocess
import sys

root, out = map(Path, sys.argv[1:])
fixture, interestc = out / 'data-interestc', root / 'build/interestc'
good = str(out / 'good.lang')
data = subprocess.run([str(interestc), 'data', good], capture_output=True, text=True)
assert (data.returncode, data.stderr) == (0, '') and data.stdout.startswith('start ')
normal = subprocess.run([str(fixture), 'data', good], capture_output=True, text=True)
assert (normal.returncode, normal.stdout, normal.stderr) == (0, data.stdout, '')
readonly = out / 'readonly.txt'
readonly.write_bytes(b'sentinel\n')
cases = [(fixture, ['data', good], False), (fixture, ['data', good], True),
         (interestc, ['check', good], False), (interestc, ['table', good], False),
         (interestc, ['eval', good, 'members'], False),
         (interestc, ['verdicts', str(root / 'examples/arrow-debreu.lang'), 'F'], False)]
for compiler, args, unbuffered in cases:
    env = {key: value for key, value in os.environ.items() if key != 'LANG_TEST_OUTPUT_UNBUFFERED'}
    if unbuffered:
        env['LANG_TEST_OUTPUT_UNBUFFERED'] = '1'
    with readonly.open('rb') as stdout:
        result = subprocess.run([str(compiler), *args], stdout=stdout, stderr=subprocess.PIPE,
                                text=True, env=env)
    assert result.returncode == 2 and result.stderr.startswith('interestc: IO_WRITE: -: stdout:'), (
        args, unbuffered, result.returncode, result.stderr)
assert readonly.read_bytes() == b'sentinel\n'
PY
actual=$?
checks=$((checks + 7))
if [ "$actual" -ne 0 ]; then
  printf 'FAIL stdout write errors\n'
  failures=$((failures + 1))
fi
printf 'build-output.sh: %s checks, %s failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ]
