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

## Acknowledgments

This repository is a fork of
[junyoungkim22/duckdb-spalm-release](https://github.com/junyoungkim22/duckdb-spalm-release),
and its spalm library is a fork of
[junyoungkim22/spalm-release](https://github.com/junyoungkim22/spalm-release),
both by Junyoung Kim, accompanying:

> Junyoung Kim and Kenneth A. Ross. 2026. SPALM: A Sparsity-Pattern-Adaptive
> Library for Matrices. *Proc. ACM Manag. Data* 4, 3 (SIGMOD), Article 236.
> https://doi.org/10.1145/3802113
