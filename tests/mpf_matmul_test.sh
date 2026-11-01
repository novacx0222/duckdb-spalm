#!/bin/bash
# Compares the MatMul regions that the MPF planners predict for a plan with the SPALM
# operators of its EXPLAIN, and both with tests/mpf_matmul_test.expected.
# Build: make release
# Run from the repository root: tests/mpf_matmul_test.sh [duckdb binary]
set -u

DUCKDB="${1:-build/release/duckdb}"
ACTUAL="$(mktemp)"
trap 'rm -f "$ACTUAL"' EXIT

"$DUCKDB" -c ".read tests/mpf_matmul_test.sql" 2>&1 |
	grep -oE '^== .*|^\[mpf\] plan [0-9]+ \([0-9]+ matmuls\): .*|^\[mpf\] (rejected|not enumerated).*|PHYSICAL_SPALM|.*Error.*' > "$ACTUAL"

# The sections of a query have one EXPLAIN each, so they can be counted one by one.
if ! awk '
	function check() {
		if (section != "" && planned && predicted != operators) {
			print "predicted " predicted ", found " operators ": " section
			wrong = 1
		}
	}
	/^== / { check(); section = $0; predicted = 0; operators = 0; planned = 0 }
	/^\[mpf\] plan / { planned = 1; count = $4; sub(/^\(/, "", count); predicted += count }
	/^PHYSICAL_SPALM$/ { operators++ }
	END { check(); exit wrong }
' "$ACTUAL"; then
	echo "MPF MatMul regions: predictions do not match the plans" >&2
	exit 1
fi
if diff -u tests/mpf_matmul_test.expected "$ACTUAL"; then
	echo "MPF MatMul regions: $(grep -c '^\[mpf\] plan ' "$ACTUAL") predictions match $(grep -c '^PHYSICAL_SPALM' "$ACTUAL") SPALM operators, as expected"
else
	echo "MPF MatMul regions: unexpected report" >&2
	exit 1
fi
