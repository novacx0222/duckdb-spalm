#!/bin/bash
# Checks the cost model of the MPF planners: wrong settings must fail, pruning must keep
# the expected plans, and the chosen plans must return the result of the query.
# Compares with tests/mpf_cost_test.expected, without the estimated costs themselves.
# Build: make release
# Run from the repository root: tests/mpf_cost_test.sh [duckdb binary]
set -u

DUCKDB="${1:-build/release/duckdb}"
ACTUAL="$(mktemp)"
trap 'rm -f "$ACTUAL"' EXIT

"$DUCKDB" -c ".read tests/mpf_cost_test.sql" 2>&1 |
	grep -E '^== |^\[mpf\] (plans|pruned|plan [0-9]+ \(|cheapest|chosen|cost weights|not enumerated)|^(differences|setting),|Error' |
	sed -E 's/^(\[mpf\] cheapest: plan [0-9]+) \(.*\)$/\1/' > "$ACTUAL"
if diff -u tests/mpf_cost_test.expected "$ACTUAL"; then
	echo "MPF cost: $(grep -c 'Error' "$ACTUAL") errors, $(grep -c '^\[mpf\] chosen' "$ACTUAL") plans chosen by cost," \
		"$(grep -c '^differences,0' "$ACTUAL") plans run that return the result of the query, as expected"
else
	echo "MPF cost: unexpected report" >&2
	exit 1
fi
