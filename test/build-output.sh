#!/bin/sh
# Failed builds and output aliases must preserve source and existing artifacts.
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
printf 'build-output.sh: %s checks, %s failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ]
