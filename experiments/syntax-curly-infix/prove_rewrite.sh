#!/usr/bin/env bash
# Proves the in-tree rewrite from git's copy of each original (dev/syntax-curly-infix-design.md,
# section 7): every .dta changed since BASE reads, with the switch on, to the tree its original
# read, carries the converter's written-infix marks, and keeps the original's comments on
# their lines and its line count (`baker_syntax_curly-infix same`). Also checks that each
# original is kept in samples/ for checkouts without the history.
#
#   experiments/syntax-curly-infix/prove_rewrite.sh \
#       build/experiments/syntax-curly-infix/baker_syntax_curly-infix [BASE]
#
# BASE defaults to 15ce5b6, the commit before the rewrite. Exit 1 if any file fails.
set -euo pipefail
tool=$(realpath "$1")
base=${2:-15ce5b6}
cd "$(git rev-parse --show-toplevel)"
samples=experiments/syntax-curly-infix/samples
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
status=0
files=0
while IFS= read -r f; do
    files=$((files + 1))
    git show "$base:$f" >"$tmp/original.dta"
    "$tool" same "$f" "$tmp/original.dta" || status=1
    kept=""
    for s in "$samples"/*.original.dta; do
        if cmp -s "$s" "$tmp/original.dta"; then kept=$s; fi
    done
    if [ -z "$kept" ]; then
        echo "no copy  $f: no file in $samples is git's original"
        status=1
    fi
done < <(git diff --name-only "$base" -- '*.dta' ':!experiments/')
echo "$files files changed since $base; $([ $status = 0 ] && echo "all proved" || echo "FAILED")"
exit $status
