#!/bin/bash
# Compares SPALM with DuckDB's own join and aggregate on zeros, negative numbers, sums of
# zero, numbers that underflow or overflow, NaN and Inf, and both with
# tests/spalm_values_test.expected: which way each input goes, and its warnings.
# Needs the library: SPALM_CONF, as for any query that runs on SPALM.
# Build: make release
# Run from the repository root: tests/spalm_values_test.sh [duckdb binary]
set -u

DUCKDB="$(realpath "${1:-build/release/duckdb}")"
ROOT="$(pwd)"
ACTUAL="$(mktemp)"
trap 'rm -f "$ACTUAL"' EXIT

# Matrices this small go to MKL, which the library finds in ./bin
[ -d spalm-release/bin ] && cd spalm-release
"$DUCKDB" -c ".read $ROOT/tests/spalm_values_test.sql" 2>&1 |
	grep -oE '^== .*|^(differences|rows),.*|^\[result-values\].*|^\[spalm-warning\].*|PHYSICAL_SPALM|.*Error.*' > "$ACTUAL"
if diff -u "$ROOT/tests/spalm_values_test.expected" "$ACTUAL"; then
	echo "SPALM values: $(grep -c '^differences,0' "$ACTUAL") queries return the result of DuckDB, as expected"
else
	echo "SPALM values: unexpected report" >&2
	exit 1
fi
