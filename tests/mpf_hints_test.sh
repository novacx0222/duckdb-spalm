#!/bin/bash
# Checks SET mpf_hints: wrong hints must fail, and every plan that hints add must
# return the result of the query. Compares with tests/mpf_hints_test.expected.
# Build: make release
# Run from the repository root: tests/mpf_hints_test.sh [duckdb binary]
set -u

DUCKDB="${1:-build/release/duckdb}"
ACTUAL="$(mktemp)"
trap 'rm -f "$ACTUAL"' EXIT

"$DUCKDB" -c ".read tests/mpf_hints_test.sql" 2>&1 |
	grep -E '^== |^\[mpf\]   .* keys |^\[mpf\] plans|^\[mpf\] plan |^(differences|rows|hints),|Error' > "$ACTUAL"
if diff -u tests/mpf_hints_test.expected "$ACTUAL"; then
	echo "MPF hints: $(grep -c 'mpf_hints:' "$ACTUAL") errors, $(grep -c '^differences,0' "$ACTUAL") plans return the result of the query, as expected"
else
	echo "MPF hints: unexpected report" >&2
	exit 1
fi
