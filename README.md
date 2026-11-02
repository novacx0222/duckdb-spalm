# spalm-duckdb

This repository contains code for the DuckDB extension used in the paper 'SPALM: A Sparsity-Pattern-Adaptive Library for Matrices'.

This is a DuckDB extension that enables matrix multiplication using [spalm](https://github.com/novacx0222/spalm-internal). Sparse matrix operations are offloaded to spalm, allowing large-scale matrix multiplications to be expressed as simple SQL queries.

## Cloning

Clone this repository with submodules:

```sh
git clone --recurse-submodules git@github.com:novacx0222/duckdb-spalm.git
```

The spalm library is checked out as the `spalm-release` submodule, from
[novacx0222/spalm-internal](https://github.com/novacx0222/spalm-internal).

## Prerequisites

Before building this extension, you must first build and set up the **spalm-release** submodule along with its Dockerfile.

## Building

```sh
make
```

The main binaries that will be built are:

```sh
./build/release/duckdb
./build/release/extension/spalm/spalm.duckdb_extension
```

## Running

Start the DuckDB shell with the extension loaded, using spalm-release's Docker image:

```sh
./run_duckdb.sh
```

## Example

Matrix multiplication is expressed as a join-aggregate query over sparse matrix tables. Each matrix is stored in coordinate format `(row, col, val)`.

```sql
CREATE TABLE matrix_a (ai INTEGER, ak INTEGER, av DOUBLE);
INSERT INTO matrix_a VALUES (0, 1, 3), (1, 8, 2), (2, 3, 1);
CREATE TABLE matrix_b (bj INTEGER, bk INTEGER, bv DOUBLE);
INSERT INTO matrix_b VALUES (0, 3, 8), (1, 1, 4), (2, 8, 5);

SET preserve_insertion_order = false;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
SET threads = 20;

SELECT ai, bj, SUM(av * bv)
FROM matrix_a, matrix_b WHERE matrix_a.ak = matrix_b.bk
GROUP BY ai, bj;
```

This computes `C = A * B` where the shared dimension `k` is matched via the join predicate `ak = bk`, and the result is grouped by output row `ai` and column `bj`.

### Repeated entries

Neither input needs to be unique on its (row, join key) pair. `spalm_duplicates` says what happens to the entries that repeat a pair:

| Value | Behaviour |
|-------|-----------|
| `merge` (default) | sums them while building the CSR, which gives the result of the join |
| `error` | fails the query |
| `ignore` | no check; the kernels must not see repeated entries, so any of them gives a wrong result without an error |

### MPF planning

`SET mpf_planner = 'cs'` or `'cs_plus'` makes the join order optimizer check whether each aggregate above a join graph is an MPF query: one `SUM` of a product of `DOUBLE` columns, plain `GROUP BY` columns, and inner equi-joins of tables or subqueries, with predicates on one relation only. It prints the outcome to stderr (`[mpf] accepted: ...` or `[mpf] rejected: ...`). The default is `off`.

For an accepted query it also prints the candidate plans (`[mpf] plans: N`, then `[mpf] plan i (k matmuls): ...`), sorted by description so that a query always gets the same numbers. `k` counts the aggregates that SPALM replaces, which need two `INTEGER` groups, one from each side of a join on one `INTEGER` variable, and no operator below that can add NULL groups, such as an outer join. `cs` adds an aggregate only where a node has all factors of the product; `cs_plus` adds one wherever it removes a variable. `S(r)` is a table, `J[v](x y)` a join on `v`, `P[v](x)` a projection that multiplies the values of `x`, and `A[v](x)` an aggregate that groups by `v`. A query with more than 12 relations or more than a million plan nodes is reported as `[mpf] not enumerated: ...`.

`SET mpf_hints = 'R(i, k), S(k, j)'` declares unique keys of tables, which DuckDB does not know: the columns of a key have different values in all rows. Without hints, the planners add no aggregate that only merges repeated rows. With hints, a table without a key can repeat rows, so it gets those aggregates, and a key goes up the plan: a join keeps the keys of its sides where it can, and an aggregate has its groups as a key. An aggregate that then merges nothing becomes a projection, `O[v](x)`. A key only counts if the query joins or groups by all of its columns.

A hint on a table or a column that does not exist fails at `SET`, as anything else that does not have this form. The optimizer checks hints again whenever `mpf_planner` is on, so a query fails if a table was dropped since. Nothing checks that a key is true: a false key can only change the candidates, and all of them return the same result.

There is no cost model yet, so a plan only changes on request. `SET mpf_force_plan = i` prints plan `i` and runs it, and the default `-1` prints all of them and runs DuckDB's own plan. The setting applies to every aggregate of a query, which can have different plans each: where there is no plan `i`, as reported by `[mpf] plans: N, no plan i`, DuckDB's own plan runs. All plans return the same result, up to the order of additions of floating point numbers.

## Timing output

Each query prints one line per stage, `[stage-name] <milliseconds> ms`, in the same format as the spalm library
(see its README for the native stages between `SPALM MATMUL` and `[spalm-total]`). Phase `1` is the build side
(first matrix), phase `2` the probe side. Nested totals overlap; do not add them together.

| Label | Covers |
|-------|--------|
| `[1-collect-input]`, `[2-collect-input]` | scanning a side into the operator, until its preparation starts |
| `[1-combine-input]`, `[2-combine-input]` | merging thread-local inputs (summed over threads) |
| `[N-dimension-scan]`, `[N-csr-allocate]`, `[N-scan-chunks-prepare]` | input validation and dimensions, CSR arrays, scan buffers |
| `[N-csr-histogram-allocate]`, `[N-csr-row-count]`, `[N-csr-prefix-offset]`, `[N-csr-count-total]`, `[N-csr-write]`, `[N-csr-sort]` | CSR construction |
| `[N-csr-merge]` | summing repeated entries (only if there are any, see `duplicates=` in `[N-csr-sort-stats]`) |
| `[N-prepare-total]` | all preparation of one side (phase 2 includes the result buffer) |
| `[1-input-deallocate]` | releasing the collected build-side input |
| `[result-allocate]`, `[result-zero]`, `[result-allocate-zero-total]` | the dense M x N result buffer |
| `[library-load]`, `[library-config-setup]` | configuration, library lookup and options |
| `[prepare-total]` | wrapper work before the native call (both sides, result buffer, library, options) |
| `[library-call-total]` | the native `spalm()` call |
| `[output-total]` | emitting the non-zero result entries |
| `[wrapper-deallocate]` | releasing the CSR arrays and the result |

## Acknowledgments

This repository is a fork of
[junyoungkim22/duckdb-spalm-release](https://github.com/junyoungkim22/duckdb-spalm-release),
and its spalm library is a fork of
[junyoungkim22/spalm-release](https://github.com/junyoungkim22/spalm-release),
both by Junyoung Kim, accompanying:

> Junyoung Kim and Kenneth A. Ross. 2026. SPALM: A Sparsity-Pattern-Adaptive
> Library for Matrices. *Proc. ACM Manag. Data* 4, 3 (SIGMOD), Article 236.
> https://doi.org/10.1145/3802113
