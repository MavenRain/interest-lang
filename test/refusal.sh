#!/bin/sh
# Refusal tests of interestc, run by test/gate.sh (make check): one program per
# REFUSE_* code (host README, Refusals), then one per checker error. Files go to
# build/test/refusal. Each run stays far under 4 GB: the arena of one run
# takes at most LANG_ARENA_MAX (256 MiB, src/syntax.h).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
interestc=$root/build/interestc
out=$root/build/test/refusal
mkdir -p "$out"
failures=0

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }

# refuse NAME CODE DEF LINE...: interestc check of the program LINE... exits 1
# with stderr "interestc: CODE: DEF: ...".
refuse() {
  name=$1 code=$2 def=$3
  shift 3
  printf '%s\n' "$@" > "$out/$name.lang"
  "$interestc" check "$out/$name.lang" > /dev/null 2> "$out/$name.err"
  status=$?
  err=$(cat "$out/$name.err")
  case $err in
    "interestc: $code: $def: "*) shape=1 ;;
    *) shape=0 ;;
  esac
  if [ "$status" -eq 1 ] && [ "$shape" -eq 1 ]; then pass "$name"; else fail "$name: exit $status, stderr: $err"; fi
}

members='def members : Nat := 3'

refuse members-missing REFUSE_MEMBERS - 'def x : Nat := 0'
refuse members-zero REFUSE_MEMBERS - 'def members : Nat := 0'
refuse members-not-literal REFUSE_MEMBERS - 'def members : Nat := natAdd 1 2'
refuse members-empty REFUSE_MEMBERS - ''
refuse program-mu REFUSE_MU Flag "$members" 'mu Flag : Type 0 := | up : Flag'
refuse program-rec REFUSE_REC f "$members" 'def rec f : Nat -> Nat := fun (n : Nat) => n'
for form in nu axiom contract storage entry payable constructor fallback error invariant predicate proof guard sload sstore; do
  refuse "form-$form" REFUSE_FORM - "$members" "$form x : Nat := 0"
done
refuse prelude-name REFUSE_PRELUDE_NAME decide "$members" 'def decide : Nat := 0'
refuse builtin-name REFUSE_PRELUDE_NAME natAdd "$members" 'def natAdd : Nat := 0'

refuse duplicate TYPE_DUPLICATE x "$members" 'def x : Nat := 0' 'def x : Nat := 1'
refuse scope TYPE_SCOPE x "$members" 'def x : Nat := y'
refuse mismatch TYPE_MISMATCH x "$members" 'def x : Nat := open'
refuse shape TYPE_SHAPE x "$members" 'def x : Nat := 0 1'
refuse infer TYPE_INFER x "$members" 'def x : Nat := (fun (n : Nat) => n) 0'
refuse universe TYPE_UNIVERSE x "$members" 'def x : Type 1 := Type 1'
refuse erased TYPE_ERASED f "$members" 'def f : (0 n : Nat) -> Nat := fun (0 n : Nat) => n'
refuse erased-flag TYPE_ERASED f "$members" 'def f : (0 n : Nat) -> Nat := fun (n : Nat) => 0'
refuse erased-projection TYPE_ERASED f "$members" \
  'def f : (p : (0 n : Nat) * Nat) -> Nat := fun (p : (0 n : Nat) * Nat) => p.0'
refuse match-family TYPE_MATCH x "$members" 'def x : Nat := match open as d in Ballots return Nat with | bnil => 0 | bcons h t => 1'
refuse match-arms TYPE_MATCH x "$members" 'def x : Nat := match open as d in Charter return Nat with | open => 0 | restricted => 1'
refuse nat-overflow TYPE_NAT x "$members" 'def x : Nat := natAdd 18446744073709551615 1'
refuse nat-mul-overflow TYPE_NAT x "$members" 'def x : Nat := natMul 4294967296 4294967296'

# The claim algebra refusals (SPEC section 5; brief section 7). A program
# cannot replace a prelude operation, declare a mu, or give a waterfall,
# a restriction or an issuer table an argument that the prelude type does
# not have.
refuse domain-mint REFUSE_PRELUDE_NAME credit "$members" \
  'def credit : Measure -> Identity -> Nat -> Measure := fun (m : Measure) (h : Identity) (n : Nat) => m'
refuse domain-burn REFUSE_PRELUDE_NAME debit "$members" \
  'def debit : Measure -> Identity -> Nat -> Measure := fun (m : Measure) (h : Identity) (n : Nat) => m'
refuse domain-mu-write REFUSE_PRELUDE_NAME transfer "$members" \
  'def transfer : InterestState -> InterestState := fun (s : InterestState) => s'
refuse domain-gate-third TYPE_SCOPE w "$members" 'def w : Waterfall := fun (k : Kind) => skim'
refuse domain-gate-mu REFUSE_MU Gate "$members" 'mu Gate : Type 0 := | pass : Gate | retain : Gate | skim : Gate'
refuse domain-r-mu TYPE_MISMATCH r "$members" 'def r : Restriction := fun (m : Measure) (b : Profile) => any'
refuse domain-r-wallet TYPE_MISMATCH r "$members" 'def r : Restriction := fun (w : Wallet) (b : Profile) => any'
refuse domain-charter REFUSE_PRELUDE_NAME amendState "$members" \
  'def amendState : InterestState -> InterestState := fun (s : InterestState) => s'
refuse domain-issuer TYPE_MISMATCH iss "$members" \
  'def iss : Charter -> Identity -> Option (prod ()) := fun (k : Nat) (h : Identity) => none (prod ())'
refuse domain-floor-payout REFUSE_PRELUDE_NAME withdraw "$members" \
  'def withdraw : Identity -> Nat -> InterestState -> prod (Nat, InterestState) := fun (h : Identity) (n : Nat) (s : InterestState) => (0, s)'
refuse domain-erc-storage REFUSE_FORM - "$members" 'storage balanceOf : Identity -> Nat := fun (h : Identity) => 0'
refuse domain-hash-w TYPE_MISMATCH w "$members" 'def w : Charter -> Waterfall := fun (c : Charter) (hash : Nat) => pass'
refuse domain-hash-r TYPE_MISMATCH r "$members" \
  'def r : Charter -> Restriction := fun (c : Charter) (hash : Nat) (b : Profile) => any'
refuse domain-hrow-5 TYPE_MISMATCH genesis "$members" \
  'def genesis : Holders := hrow 4096 1 (tuple (domestic, retail)) 2 hnil'
refuse domain-hrow-partition TYPE_MISMATCH genesis "$members" \
  'def genesis : Holders := hrow 4096 1 (tuple (domestic, retail)) 2 open hnil'

if [ "$failures" -eq 0 ]; then echo "refusal.sh: all passed"; exit 0; fi
echo "refusal.sh: $failures failed"
exit 1
