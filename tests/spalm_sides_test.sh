#!/bin/bash
# Compares SPALM with DuckDB's own join and aggregate where a side of the MatMul has
# operators below it (outer, semi and anti joins, LIMIT, DISTINCT, subqueries, UNNEST,
# sums), and both with tests/spalm_sides_test.expected: which way each query goes, and
# that its result is the result of DuckDB.
# Needs the library: SPALM_CONF (or ./spalm.conf), as for any query that runs on SPALM.
# Build: make release
# Run from the repository root: tests/spalm_sides_test.sh [duckdb binary]
set -u

DUCKDB="$(realpath "${1:-build/release/duckdb}")"
ROOT="$(pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# The configuration, with so_dir absolute where it is relative to the file, as in
# tests/spalm_values_test.sh
CONF="$(realpath "${SPALM_CONF:-spalm.conf}")"
SO_DIR="$(sed -nE 's/^[[:space:]]*so_dir[[:space:]]*=[[:space:]]*(.*[^[:space:]])[[:space:]]*$/\1/p' "$CONF" | tail -n 1)"
case "$SO_DIR" in
/*) ;;
*) [ -d "$(dirname "$CONF")/$SO_DIR" ] && SO_DIR="$(dirname "$CONF")/$SO_DIR" ;;
esac
{
	cat "$CONF"
	echo "so_dir = $SO_DIR"
} > "$WORK/spalm.conf"

(
	# Matrices this small go to MKL, which the library finds in ./bin
	[ -d spalm-release/bin ] && cd spalm-release
	SPALM_CONF="$WORK/spalm.conf" "$DUCKDB" -c ".read $ROOT/tests/spalm_sides_test.sql" 2>&1
) | grep -oE '^== .*|^(differences|rows),.*|PHYSICAL_SPALM|.*Error.*' > "$WORK/output"

if diff -u tests/spalm_sides_test.expected "$WORK/output"; then
	echo "SPALM sides: $(grep -c '^PHYSICAL_SPALM' "$WORK/output") queries on SPALM and $(grep -c '^differences,0' "$WORK/output") in all return the result of DuckDB, as expected"
else
	echo "SPALM sides: unexpected report" >&2
	exit 1
fi
