#!/bin/bash
# Compares the candidate plans of the MPF planners with tests/mpf_plan_test.expected.
# Build: make release
# Run from the repository root: tests/mpf_plan_test.sh [duckdb binary]
set -u

DUCKDB="${1:-build/release/duckdb}"
ACTUAL="$(mktemp)"
trap 'rm -f "$ACTUAL"' EXIT

"$DUCKDB" -c ".read tests/mpf_plan_test.sql" 2>&1 | grep -E '^(== |\[mpf\]|.*mpf_force_plan must)' > "$ACTUAL"
if diff -u tests/mpf_plan_test.expected "$ACTUAL"; then
	echo "MPF plans: $(grep -c '^\[mpf\] plans' "$ACTUAL") reports of plans, as expected"
else
	echo "MPF plans: unexpected report" >&2
	exit 1
fi
