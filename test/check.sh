#!/bin/sh
# Checker and verb tests of interestc, run by test/gate.sh (make check): the gate
# checker fixtures, the mutants in test/mutants and the guards.
# Files go to build/test. Each run stays far under 4 GB: the arena of one run
# takes at most LANG_ARENA_MAX (256 MiB, src/syntax.h).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
interestc=$root/build/interestc
programs=$root/examples
out=$root/build/test
mkdir -p "$out"
failures=0

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }

# expect NAME WANT_STATUS WANT_STDOUT ARGS...: exit status and stdout, with an
# empty stderr on exit 0.
expect() {
  name=$1 want_status=$2 want=$3
  shift 3
  "$interestc" "$@" > "$out/check.out" 2> "$out/check.err"
  status=$?
  got=$(cat "$out/check.out")
  err=$(cat "$out/check.err")
  if [ "$status" -eq "$want_status" ] && [ "$got" = "$want" ] && { [ "$status" -ne 0 ] || [ -z "$err" ]; }; then
    pass "$name"
  else
    fail "$name: exit $status, stdout: $got, stderr: $err"
  fi
}

# refuse NAME CODE DEF SUFFIX ARGS...: exit 1, stderr "interestc: CODE: DEF: ..."
# that ends in SUFFIX.
refuse() {
  name=$1 code=$2 def=$3 suffix=$4
  shift 4
  "$interestc" "$@" > /dev/null 2> "$out/check.err"
  status=$?
  err=$(cat "$out/check.err")
  case $err in
    "interestc: $code: $def: "*"$suffix") shape=1 ;;
    *) shape=0 ;;
  esac
  if [ "$status" -eq 1 ] && [ "$shape" -eq 1 ]; then pass "$name"; else fail "$name: exit $status, stderr: $err"; fi
}

expect "check arrow-debreu" 0 "ok debreu" check "$programs/arrow-debreu.lang"
expect "check arrow-debreu-token" 0 "ok debreu" check "$programs/arrow-debreu-token.lang"
expect "check arrow-impossibility" 0 "ok impossibility" check "$programs/arrow-impossibility.lang"
expect "table arrow-debreu" 0 "debreu 3 3 3 2 2 3 3 2 1 1 1" table "$programs/arrow-debreu.lang"
expect "table arrow-impossibility" 0 "impossibility 3" table "$programs/arrow-impossibility.lang"
expect "verdicts arrow-debreu F" 0 "111123133123222323133323333" verdicts "$programs/arrow-debreu.lang" F
expect "verdicts arrow-impossibility first" 0 "111111111222222222333333333" verdicts "$programs/arrow-impossibility.lang" first
expect "check erc721-dirac" 0 "ok debreu" check "$programs/erc721-dirac.lang"
expect "table erc721-dirac" 0 "debreu 3 3 3 3 2 3 3 2 3 1 1" table "$programs/erc721-dirac.lang"
expect "verdicts erc721-dirac F" 0 "113123333123223333333333333" verdicts "$programs/erc721-dirac.lang" F
expect "eval vetoX" 0 "reflDec frozen" eval "$programs/erc721-dirac.lang" vetoX
expect "eval transferMoves" 0 "reflNat 1" eval "$programs/erc721-dirac.lang" transferMoves
expect "eval restrictedRetail" 0 "reflNat 1" eval "$programs/erc721-dirac.lang" restrictedRetail
expect "eval conserved" 0 "reflNat 5" eval "$programs/arrow-debreu.lang" conserved
expect "eval transferConserves" 0 "reflNat 5" eval "$programs/arrow-debreu.lang" transferConserves
expect "eval amendCharter" 0 "reflDec open" eval "$programs/arrow-debreu.lang" amendCharter
expect "eval withdrawLaw" 0 "reflNat 17" eval "$programs/arrow-debreu.lang" withdrawLaw
expect "eval withdrawCarry" 0 "reflNat 41" eval "$programs/arrow-debreu.lang" withdrawCarry
expect "eval distributeSum" 0 "reflNat 52" eval "$programs/arrow-debreu.lang" distributeSum
expect "eval attestRegistry" 0 "reflNat 1" eval "$programs/arrow-debreu.lang" attestRegistry
expect "eval firstX" 0 "reflDec open" eval "$programs/arrow-impossibility.lang" firstX
expect "eval members" 0 "3" eval "$programs/arrow-impossibility.lang" members

# Inclusive caps must work at the largest Nat without computing n + 1.
cat > "$out/cap-boundaries.lang" <<'EOF'
def members : Nat := 3
def bit : Option (prod ()) -> Nat := fun (o : Option (prod ())) =>
  case o with | 0 (u : prod ()) => 0 | 1 (u : prod ()) => 1
def capZero : EqNat (bit (admits (upTo 0) 0)) 1 := reflNat 1
def capReject : EqNat (bit (admits (upTo 0) 1)) 0 := reflNat 0
def capMaxZero : EqNat (bit (admits (upTo 18446744073709551615) 0)) 1 := reflNat 1
def capMaxBelow : EqNat (bit (admits (upTo 18446744073709551615) 18446744073709551614)) 1 := reflNat 1
def capMaxEqual : EqNat (bit (admits (upTo 18446744073709551615) 18446744073709551615)) 1 := reflNat 1
EOF
expect "inclusive cap boundaries" 0 "ok impossibility" check "$out/cap-boundaries.lang"

# Distinct wallets may share an identity, but each genesis wallet has one row.
cat > "$out/genesis-wallets.lang" <<'EOF'
def members : Nat := 3
def genesis : Holders := hrow 4096 1 (tuple (domestic, retail)) 2 classA
  (hrow 4097 1 (tuple (foreign, accredited)) 3 classA hnil)
EOF
expect "genesis shared identity" 0 "start 1
charters 3
genesis 4096:1:0:2:1 4097:1:3:3:1
restrict 1 d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d
restrict 2 d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d
waterfall rr rr rr
issuers
name interest
symbol INT" data "$out/genesis-wallets.lang"
for identity in 1 2; do
  cat > "$out/genesis-duplicate.lang" <<EOF
def members : Nat := 3
def genesis : Holders := hrow 4096 1 (tuple (domestic, retail)) 2 classA
  (hrow 4096 $identity (tuple (foreign, accredited)) 3 classA hnil)
EOF
  refuse "duplicate genesis wallet identity $identity data" CONTRACT_GENESIS genesis \
    "repeats wallet 4096" data "$out/genesis-duplicate.lang"
  refuse "duplicate genesis wallet identity $identity build" CONTRACT_GENESIS genesis \
    "repeats wallet 4096" build "$out/genesis-duplicate.lang" -o "$out/genesis-duplicate.hex"
done

# Identity 0 is no identity: the registry stores identity + 1 (MY CALL 150 (b)).
cat > "$out/genesis-identity-0.lang" <<'EOF'
def members : Nat := 3
def genesis : Holders := hrow 4096 0 (tuple (domestic, retail)) 2 classA hnil
EOF
refuse "genesis identity 0 data" CONTRACT_GENESIS genesis \
  "has a row with identity 0" data "$out/genesis-identity-0.lang"
refuse "genesis identity 0 build" CONTRACT_GENESIS genesis \
  "has a row with identity 0" build "$out/genesis-identity-0.lang" -o "$out/genesis-identity-0.hex"

# A Text def has bytes 32 .. 126 and at most 32 bytes (O5c, MY CALL 169).
cat > "$out/text-low.lang" <<'EOF'
def members : Nat := 3
def name : Text := char 65 (char 31 end)
EOF
refuse "text byte below 32" CONTRACT_VALUE name \
  "has a byte outside 32 .. 126" data "$out/text-low.lang"
cat > "$out/text-high.lang" <<'EOF'
def members : Nat := 3
def symbol : Text := char 127 end
EOF
refuse "text byte above 126" CONTRACT_VALUE symbol \
  "has a byte outside 32 .. 126" data "$out/text-high.lang"
cat > "$out/text-long.lang" <<'EOF'
def members : Nat := 3
def name : Text := char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 (char 65 end))))))))))))))))))))))))))))))))
EOF
refuse "text of 33 bytes" CONTRACT_VALUE name \
  "has more than 32 bytes" data "$out/text-long.lang"

# Erased Sigma fields may be constructed from erased variables and used
# in types, while the second field remains available at run time.
cat > "$out/erased-sigma.lang" <<'EOF'
def members : Nat := 3
def pack : (0 n : Nat) -> (0 x : Nat) * EqNat x n :=
  fun (0 n : Nat) => (n, reflNat n)
def unpack : (p : (0 x : Nat) * Nat) -> Nat := fun (p : (0 x : Nat) * Nat) => p.1
def proof : (p : (0 x : Nat) * EqNat x x) -> EqNat p.0 p.0 :=
  fun (p : (0 x : Nat) * EqNat x x) => p.1
def witness : EqNat (unpack (1, 2)) 2 := reflNat 2
EOF
expect "erased Sigma construction and type projection" 0 "ok impossibility" check "$out/erased-sigma.lang"

# A definitionally equal annotation must choose the same regime and
# contract, including through an alias of the Aggregation type function.
for annotation in 'Aggregation F' AggF 'AggType F'; do
  cat > "$out/aggregation-alias.lang" <<EOF
def members : Nat := 1
def F : ChoiceRule := fun (x : Config) => open
def AggF : Type 0 := Aggregation F
def AggType : (0 F : ChoiceRule) -> Type 0 := Aggregation
def agg : $annotation := mkAgg F (fun (t : Tally) => open) (fun (x : Config) => reflDec open)
EOF
  expect "check aggregation annotation $annotation" 0 "ok debreu" check "$out/aggregation-alias.lang"
  expect "table aggregation annotation $annotation" 0 "debreu 1 1 1 1" table "$out/aggregation-alias.lang"
  expect "build aggregation annotation $annotation" 0 "" build "$out/aggregation-alias.lang" -o "$out/aggregation.hex"
  if [ "$annotation" = 'Aggregation F' ]; then
    cp "$out/aggregation.hex" "$out/aggregation-direct.hex"
  elif cmp -s "$out/aggregation.hex" "$out/aggregation-direct.hex"; then
    pass "aggregation annotation $annotation preserves bytecode"
  else
    fail "aggregation annotation $annotation changes bytecode"
  fi
done

refuse "mutant debreu conserved reflNat 4" TYPE_MISMATCH conserved \
  "the types differ: expected EqNat 5 5, found EqNat 4 4" check "$root/test/mutants/debreu-conserve-4.lang"
refuse "mutant debreu transferMoves reflNat 3" TYPE_MISMATCH transferMoves \
  "the types differ: expected EqNat 2 2, found EqNat 3 3" check "$root/test/mutants/debreu-transfer-3.lang"
refuse "mutant debreu withdrawPays reflNat 4" TYPE_MISMATCH withdrawPays \
  "the types differ: expected EqNat 3 3, found EqNat 4 4" check "$root/test/mutants/debreu-withdraw-4.lang"
refuse "mutant debreu distributeShare reflNat 30" TYPE_MISMATCH distributeShare \
  "the types differ: expected EqNat 31 31, found EqNat 30 30" check "$root/test/mutants/debreu-share-30.lang"
refuse "mutant debreu attestNonIssuer stated at 4" TYPE_MISMATCH attestNonIssuer \
  "the types differ: expected EqNat 0 4, found EqNat 4 4" check "$root/test/mutants/debreu-attest-4.lang"
refuse "mutant debreu distributeSum reflNat 51" TYPE_MISMATCH distributeSum \
  "the types differ: expected EqNat 52 52, found EqNat 51 51" check "$root/test/mutants/debreu-sum-51.lang"
refuse "mutant debreu amendMass reflNat 4" TYPE_MISMATCH amendMass \
  "the types differ: expected EqNat 5 5, found EqNat 4 4" check "$root/test/mutants/debreu-amend-4.lang"
refuse "mutant debreu transferKeepsPart classA write" TYPE_MISMATCH transferKeepsPart \
  "the types differ: expected EqNat 0 2, found EqNat 2 2" check "$root/test/mutants/debreu-part-93.lang"
refuse "verdicts of a name that is not a ChoiceRule" VERDICT_TYPE agg "agg is not a ChoiceRule" \
  verdicts "$programs/arrow-debreu.lang" agg
refuse "eval of an unknown name" TYPE_SCOPE nothing "nothing is not declared" \
  eval "$programs/arrow-debreu.lang" nothing

# The program data of the contract (SPEC section 7, interestc data).
expect "data of arrow-debreu" 0 "start 2
charters 3
genesis 4096:1:0:5:1 4097:2:3:3:1 4098:2:3:2:2
restrict 1 a,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4 d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d
restrict 2 d,a,d,a,d,a,d,a,d,a,d,a,d,a,d,a d,u4,d,u4,d,u4,d,u4,d,u4,d,u4,d,u4,d,u4 d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d
waterfall pp pr rr
issuers 1:1 2:1
name Arrow-Debreu
symbol AD" data "$programs/arrow-debreu.lang"
expect "data of arrow-debreu-token" 0 "start 2
charters 3
genesis 4096:1:0:5:1 4097:2:3:3:1 4098:2:3:2:1
restrict 1 a,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4 d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d
restrict 2 a,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4,u4 d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d
waterfall pp pr rr
issuers 1:1 2:1
name interest
symbol INT
asset token" data "$programs/arrow-debreu-token.lang"
expect "data of arrow-impossibility" 0 "start 1
charters 3
genesis 4096:1:0:5:1 4097:2:0:0:1
restrict 1 d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d
restrict 2 d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d
waterfall rr rr rr
issuers 1:1 2:1 3:1
name interest
symbol INT" data "$programs/arrow-impossibility.lang"
expect "data of erc721-dirac" 0 "start 1
charters 3
genesis 4096:1:0:1:1 4097:2:3:0:1 4098:3:0:0:1
restrict 1 a,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a d,a,d,a,d,a,d,a,d,a,d,a,d,a,d,a d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d
restrict 2 a,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a d,a,d,a,d,a,d,a,d,a,d,a,d,a,d,a d,d,d,d,d,d,d,d,d,d,d,d,d,d,d,d
waterfall pp pp rr
issuers 1:1 2:1
name interest
symbol INT" data "$programs/erc721-dirac.lang"
printf 'def members : Nat := 1\ndef start : Nat := 1\n' > "$out/start-nat.lang"
refuse "a program def start of another type is CONTRACT_TYPE" CONTRACT_TYPE start \
  "does not have its program data type (SPEC section 7)" data "$out/start-nat.lang"
printf 'def members : Nat := 1\ndef asset : Nat := 1\n' > "$out/asset-nat.lang"
refuse "a program def asset of another type is CONTRACT_TYPE" CONTRACT_TYPE asset \
  "does not have its program data type (SPEC section 7)" data "$out/asset-nat.lang"

# Each append of a list to itself doubles it; the evaluation of the long
# appends nests past the depth cap, and the run stops with TYPE_FUEL.
awk 'BEGIN {
  print "def members : Nat := 1"
  print "def l0 : Ballots := bcons open bnil"
  for (i = 1; i <= 16; i++) printf "def l%d : Ballots := appendBallots l%d l%d\n", i, i - 1, i - 1
}' > "$out/deep.lang"
refuse "deep evaluation is TYPE_FUEL" TYPE_FUEL l11 "evaluation nests too deep" check "$out/deep.lang"

if [ "$failures" -eq 0 ]; then echo "check.sh: all passed"; exit 0; fi
echo "check.sh: $failures failed"
exit 1
