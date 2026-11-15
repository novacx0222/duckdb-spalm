#!/bin/bash
# Runs the candidate plans of the MPF planners and compares them with
# tests/mpf_execute_test.expected: every plan must return what DuckDB's own plan
# returns (differences,0), and the SPALM operators of a plan are listed.
# Build: make release
# Run from the repository root: tests/mpf_execute_test.sh [duckdb binary]
set -u

DUCKDB="${1:-build/release/duckdb}"
ACTUAL="$(mktemp)"
trap 'rm -f "$ACTUAL"' EXIT

"$DUCKDB" -c ".read tests/mpf_execute_test.sql" 2>&1 |
	grep -oE '^== .*|^\[mpf\] (plan |plans: .*no plan|not enumerated).*|^(differences|rows),.*|PHYSICAL_SPALM|.*Error.*' > "$ACTUAL"
if diff -u tests/mpf_execute_test.expected "$ACTUAL"; then
	echo "MPF execution: $(grep -c '^differences,0' "$ACTUAL") plans return the result of the query, as expected"
else
	echo "MPF execution: unexpected report" >&2
	exit 1
fi
