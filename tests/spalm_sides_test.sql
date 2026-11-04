-- SPALM against DuckDB's own join and aggregate, where a side of the MatMul has
-- operators below it. A column has facts only through operators that are known to keep
-- them, so SPALM runs where a side reads columns that cannot be NULL, and DuckDB runs
-- everything else, as the PHYSICAL_SPALM lines show. Run by tests/spalm_sides_test.sh.
SET threads = 4;
SET preserve_insertion_order = false;
.mode csv
.headers off
CREATE TABLE a (i INTEGER, k INTEGER, v DOUBLE);
CREATE TABLE b (k INTEGER, j INTEGER, v DOUBLE);
CREATE TABLE c (k INTEGER, w DOUBLE);
INSERT INTO a VALUES (0, 0, 1), (0, 1, 2), (1, 1, 3), (2, 2, 4), (3, 3, 5);
INSERT INTO b VALUES (0, 0, 2), (1, 0, 1), (1, 1, 4), (2, 2, 3), (3, 1, 6);
-- Two partners for k = 1, none for k = 0 and 3, and k = 9 has none in a
INSERT INTO c VALUES (1, 7), (1, 8), (2, 9), (9, 10);
.print == a base table
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, v FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, v FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, v FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == LEFT JOIN, the side reads the left child, which it keeps
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, a.v FROM a LEFT JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, a.v FROM a LEFT JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, a.v FROM a LEFT JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == RIGHT JOIN, the side reads the right child, which it keeps
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, a.v FROM c RIGHT JOIN a ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, a.v FROM c RIGHT JOIN a ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, a.v FROM c RIGHT JOIN a ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == LEFT JOIN, a value from the child that it pads
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, c.w AS v FROM a LEFT JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, c.w AS v FROM a LEFT JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, c.w AS v FROM a LEFT JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == LEFT JOIN, a group from the child that it pads
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT c.k AS i, a.k, a.v FROM a LEFT JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT c.k AS i, a.k, a.v FROM a LEFT JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT c.k AS i, a.k, a.v FROM a LEFT JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == FULL JOIN, which pads both children
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, a.v FROM a FULL JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, a.v FROM a FULL JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT a.i, a.k, a.v FROM a FULL JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == a semi join
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT * FROM a WHERE k IN (SELECT k FROM c)) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT * FROM a WHERE k IN (SELECT k FROM c)) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT * FROM a WHERE k IN (SELECT k FROM c)) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == an anti join
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT * FROM a WHERE NOT EXISTS (SELECT 1 FROM c WHERE c.k = a.k)) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT * FROM a WHERE NOT EXISTS (SELECT 1 FROM c WHERE c.k = a.k)) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT * FROM a WHERE NOT EXISTS (SELECT 1 FROM c WHERE c.k = a.k)) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == ORDER BY and LIMIT
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT * FROM a ORDER BY i, k LIMIT 3) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT * FROM a ORDER BY i, k LIMIT 3) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT * FROM a ORDER BY i, k LIMIT 3) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == DISTINCT over an inner join
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT DISTINCT a.i, a.k, a.v FROM a JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT DISTINCT a.i, a.k, a.v FROM a JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT DISTINCT a.i, a.k, a.v FROM a JOIN c ON a.k = c.k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == a scalar subquery that the side does not read
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, v, (SELECT max(w) FROM c WHERE c.k = a.k) AS y FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, v, (SELECT max(w) FROM c WHERE c.k = a.k) AS y FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, v, (SELECT max(w) FROM c WHERE c.k = a.k) AS y FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == a scalar subquery as the value
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, (SELECT max(w) FROM c WHERE c.k = a.k) AS v FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, (SELECT max(w) FROM c WHERE c.k = a.k) AS v FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, (SELECT max(w) FROM c WHERE c.k = a.k) AS v FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == UNNEST, which has no facts
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, unnest([v]) AS v FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, unnest([v]) AS v FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, unnest([v]) AS v FROM a) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == a sum with GROUP BY
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, sum(v) AS v FROM a GROUP BY i, k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, sum(v) AS v FROM a GROUP BY i, k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, sum(v) AS v FROM a GROUP BY i, k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == a sum with GROUP BY, without rows
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, sum(v) AS v FROM a WHERE i > 100 GROUP BY i, k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, sum(v) AS v FROM a WHERE i > 100 GROUP BY i, k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT i, k, sum(v) AS v FROM a WHERE i > 100 GROUP BY i, k) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == a sum without GROUP BY, which is NULL without rows
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT 0 AS i, 1 AS k, sum(v) AS v FROM a WHERE i > 100) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
EXPLAIN SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT 0 AS i, 1 AS k, sum(v) AS v FROM a WHERE i > 100) x JOIN b ON x.k = b.k GROUP BY 1, 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.i, b.j, SUM(x.v * b.v) AS s FROM (SELECT 0 AS i, 1 AS k, sum(v) AS v FROM a WHERE i > 100) x JOIN b ON x.k = b.k GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
