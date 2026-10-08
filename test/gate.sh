#!/bin/sh
# The kit gate. `make check` runs it from the kit directory after the build.
# Needs tcc, python3, geth evm 1.14.12 and foundry cast. Each step prints
# its own counts.
set -eu
fail=0

step() {
  echo "== $*"
  "$@" || { echo "FAIL $*"; fail=$((fail + 1)); }
}

step sh test/parse.sh
step sh test/embed-safety.sh
step sh test/check.sh
step sh test/build-output.sh
step sh test/refusal.sh
step python3 test/normal-forms.py
step python3 test/differential.py
step sh test/domains.sh
step python3 test/differential.py --interestc build/k4/interestc --decisions 4 \
  --program test/domains/plural4.lang
step python3 test/settlement.py
step python3 test/claims.py

if rg -n '\x{2013}|\x{2014}' . >/dev/null; then
  echo "FAIL: an em-dash or en-dash is in the kit"
  fail=$((fail + 1))
fi
echo "gate: $fail failures"
[ "$fail" -eq 0 ]
