#!/bin/sh
# Host core tests with the test domains of test/domains (make check builds
# build/NAME/interestc for each NAME.lang there). k4: the decision type D has 4
# constructors, so the tally, table and verdict code runs with k = 4.
# decision-arg: one constructor of D takes a Nat, so the table verbs refuse
# with TABLE_DECISION. test/differential.py runs the k4 contract in geth.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
domains=$root/test/domains
out=$root/build/test
mkdir -p "$out"
failures=0

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }

# expect NAME DOMAIN WANT_STATUS WANT_STDOUT ARGS...: exit status and stdout
# of build/DOMAIN/interestc, with an empty stderr on exit 0.
expect() {
  name=$1 domain=$2 want_status=$3 want=$4
  shift 4
  "$root/build/$domain/interestc" "$@" > "$out/domains.out" 2> "$out/domains.err"
  status=$?
  got=$(cat "$out/domains.out")
  err=$(cat "$out/domains.err")
  if [ "$status" -eq "$want_status" ] && [ "$got" = "$want" ] && { [ "$status" -ne 0 ] || [ -z "$err" ]; }; then
    pass "$name"
  else
    fail "$name: exit $status, stdout: $got, stderr: $err"
  fi
}

# refuse NAME DOMAIN CODE DEF SUFFIX ARGS...: exit 1, stderr
# "interestc: CODE: DEF: ..." that ends in SUFFIX.
refuse() {
  name=$1 domain=$2 code=$3 def=$4 suffix=$5
  shift 5
  "$root/build/$domain/interestc" "$@" > /dev/null 2> "$out/domains.err"
  status=$?
  err=$(cat "$out/domains.err")
  case $err in
    "interestc: $code: $def: "*"$suffix") shape=1 ;;
    *) shape=0 ;;
  esac
  if [ "$status" -eq 1 ] && [ "$shape" -eq 1 ]; then pass "$name"; else fail "$name: exit $status, stderr: $err"; fi
}

# The table is in tally order: compositions (release, refund, hold, defer)
# of 3, lex on the first three counts. The verdicts are the 4^3 ballot
# vectors in product order over the codes 1 to 4.
expect "k4 check plural4" k4 0 "ok debreu" check "$domains/plural4.lang"
expect "k4 table plural4" k4 0 "debreu 3 4 4 4 3 4 4 3 4 2 2 4 3 1 2 1 1 1 1 1 1" \
  table "$domains/plural4.lang"
expect "k4 verdicts plural4" k4 0 \
  "1111111211131234111212241234244411131234133434441234244434444444" \
  verdicts "$domains/plural4.lang" F
expect "k4 build plural4" k4 0 "" build "$domains/plural4.lang" -o "$out/plural4.hex"
expect "k4 check always-release" k4 0 "ok debreu" check "$domains/always-release.lang"
expect "k4 table always-release" k4 0 "debreu 1 1 1 1 1" table "$domains/always-release.lang"

suffix="does not end in a mu of 2 to 64 nullary constructors"
expect "decision-arg check always-release" decision-arg 0 "ok debreu" check "$domains/always-release.lang"
refuse "decision-arg table is TABLE_DECISION" decision-arg TABLE_DECISION ChoiceRule "$suffix" \
  table "$domains/always-release.lang"
refuse "decision-arg verdicts is TABLE_DECISION" decision-arg TABLE_DECISION ChoiceRule "$suffix" \
  verdicts "$domains/always-release.lang" F
refuse "decision-arg build is TABLE_DECISION" decision-arg TABLE_DECISION ChoiceRule "$suffix" \
  build "$domains/always-release.lang" -o "$out/always-release.hex"

expect "k10 verdicts are decimal codes" review-k10-domain 0 \
  "10 10 10 10 10 10 10 10 10 10" \
  verdicts "$domains/review-k10-program.lang" F

if [ "$failures" -eq 0 ]; then echo "domains.sh: all passed"; exit 0; fi
echo "domains.sh: $failures failed"
exit 1
