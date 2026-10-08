#!/bin/sh
# Check that embedding cannot overwrite its input, including path aliases,
# and that source paths cannot break the generated C comment.
set -eu

embed=${1:-$(cd "$(dirname "$0")/.." && pwd)/gen/embed.c}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/embed-safety.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mkdir "$tmp/x*"
source=$tmp/x*/domain.lang
printf 'def members : Nat := 1\n' > "$source"
cp "$source" "$tmp/expected.lang"
ln "$source" "$tmp/hardlink.lang"
ln -s "$source" "$tmp/symlink.lang"

for output in "$source" "$tmp/hardlink.lang" "$tmp/symlink.lang"; do
  if tcc -run "$embed" "$source" "$output" > "$tmp/stdout" 2> "$tmp/stderr"; then
    echo "FAIL embed accepted an input alias: $output"
    exit 1
  fi
  if ! cmp -s "$source" "$tmp/expected.lang"; then
    echo "FAIL embed changed its input: $output"
    exit 1
  fi
done

tcc -run "$embed" "$source" "$tmp/domain.c"
cc -std=c99 -fsyntax-only "$tmp/domain.c"
echo 'embed-safety.sh: all passed'
