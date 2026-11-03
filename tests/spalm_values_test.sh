#!/bin/bash
# Compares SPALM with DuckDB's own join and aggregate on zeros, negative numbers, sums of
# zero, numbers that underflow or overflow, NaN and Inf, and both with the expected
# report: which way each input goes, its warnings, and how the rows with zeros are
# emitted. Twice:
#   - with matched_pairs = 0, the output finds the zeros of pairs that matched itself:
#     tests/spalm_values_test.expected, with how many rows of the result look their
#     zeros up or take the pattern;
#   - with init_method = no_fallback, every query runs on SPALM's kernels (not MKL),
#     which mark the pairs that matched: tests/spalm_values_test.matched.expected.
#     Skipped for a library without spalm_matched.
# Needs the library: SPALM_CONF (or ./spalm.conf), as for any query that runs on SPALM.
# Build: make release
# Run from the repository root: tests/spalm_values_test.sh [duckdb binary]
set -u

DUCKDB="$(realpath "${1:-build/release/duckdb}")"
ROOT="$(pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# The configuration, with so_dir absolute where it is relative to the file, so that its
# copies below find the library
CONF="$(realpath "${SPALM_CONF:-spalm.conf}")"
SO_DIR="$(sed -nE 's/^[[:space:]]*so_dir[[:space:]]*=[[:space:]]*(.*[^[:space:]])[[:space:]]*$/\1/p' "$CONF" | tail -n 1)"
case "$SO_DIR" in
/*) ;;
*) [ -d "$(dirname "$CONF")/$SO_DIR" ] && SO_DIR="$(dirname "$CONF")/$SO_DIR" ;;
esac

# run <report> <line>...: the queries with these lines added to the configuration
run() {
	local report="$1"
	shift
	{
		cat "$CONF"
		echo "so_dir = $SO_DIR"
		printf '%s\n' "$@"
	} > "$WORK/spalm.conf"
	(
		# Matrices this small go to MKL, which the library finds in ./bin
		[ -d spalm-release/bin ] && cd spalm-release
		SPALM_CONF="$WORK/spalm.conf" "$DUCKDB" -c ".read $ROOT/tests/spalm_values_test.sql" 2>&1
	) | grep -oE '^== .*|^(differences|rows),.*|^\[result-values\].*|^\[(probed|pattern)-rows\].*|^\[matched-pairs\] marked=.*|^\[spalm-warning\].*|PHYSICAL_SPALM|.*Error.*' > "$report"
}

status=0
run "$WORK/output" "matched_pairs = 0"
if diff -u tests/spalm_values_test.expected "$WORK/output"; then
	echo "SPALM values: $(grep -c '^differences,0' "$WORK/output") queries return the result of DuckDB, as expected"
else
	echo "SPALM values: unexpected report" >&2
	status=1
fi
run "$WORK/matched" "init_method = no_fallback"
if ! grep -q '^\[matched-pairs\]' "$WORK/matched"; then
	echo "SPALM values with matched pairs: skipped, the library has no spalm_matched"
elif diff -u tests/spalm_values_test.matched.expected "$WORK/matched"; then
	echo "SPALM values with matched pairs: $(grep -c '^differences,0' "$WORK/matched") queries return the result of DuckDB, as expected"
else
	echo "SPALM values with matched pairs: unexpected report" >&2
	status=1
fi
exit $status
