#!/bin/bash
# Compares what the MPF entry reports with tests/mpf_query_test.expected.
# Build: make release
# Run from the repository root: tests/mpf_query_test.sh [duckdb binary]
set -u

DUCKDB="${1:-build/release/duckdb}"
ACTUAL="$(mktemp)"
trap 'rm -f "$ACTUAL"' EXIT

"$DUCKDB" -c ".read tests/mpf_query_test.sql" 2>&1 | grep -E '^(== |\[mpf\]|.*mpf_planner must)' > "$ACTUAL"
if diff -u tests/mpf_query_test.expected "$ACTUAL"; then
	echo "MPF entry: $(grep -c '^\[mpf\] accepted' "$ACTUAL") reports of accepted queries, $(grep -c '^\[mpf\] rejected' "$ACTUAL") of rejected ones, as expected"
else
	echo "MPF entry: unexpected report" >&2
	exit 1
fi
