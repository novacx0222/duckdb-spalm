-- Runs the candidate plans of the MPF planners. Run by tests/mpf_execute_test.sh.
-- The values are whole numbers, so that all plans have to agree exactly. Without
-- the extension's optimizer, DuckDB runs all of it; SPALM only shows up in EXPLAIN.
SET threads = 2;
.mode csv
.headers off
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, extension';

-- r, s, t and u repeat (row_id, k) and share keys, so that aggregates merge rows
CREATE TABLE r (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE, val2 DOUBLE);
CREATE TABLE s (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
CREATE TABLE t (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
CREATE TABLE u (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
-- w shares the keys of s, for queries with one variable in three relations
CREATE TABLE w (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
-- NULL in the keys, the groups and the measures; zero and negative measures; no rows
CREATE TABLE n (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
CREATE TABLE z (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
CREATE TABLE e (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
INSERT INTO r VALUES (1, 10, 1, 2, 3), (1, 10, 2, 5, 1), (1, 11, 1, 3, 2), (2, 10, 2, 7, 4), (2, 12, 1, 1, 1), (3, 11, 3, 4, 5), (4, 13, 3, 6, 2);
INSERT INTO s VALUES (10, 20, 1, 2), (10, 20, 2, 3), (10, 21, 1, 5), (11, 20, 2, 7), (11, 22, 3, 1), (12, 21, 1, 4), (14, 23, 1, 9);
INSERT INTO t VALUES (20, 30, 1, 3), (20, 30, 1, 1), (20, 31, 2, 2), (21, 30, 2, 6), (22, 32, 3, 5), (22, 31, 1, 4), (25, 33, 2, 8);
INSERT INTO u VALUES (30, 40, 1, 2), (30, 41, 2, 7), (31, 40, 1, 3), (31, 40, 3, 1), (32, 42, 2, 5), (34, 43, 1, 6);
INSERT INTO w VALUES (10, 50, 1, 2), (10, 51, 2, 3), (10, 51, 3, 1), (11, 50, 1, 5), (12, 52, 2, 4), (15, 53, 1, 7);
INSERT INTO n VALUES (1, 10, 1, 2), (NULL, 10, 2, 3), (2, NULL, 1, 5), (2, 11, NULL, NULL), (3, 11, 2, NULL), (NULL, NULL, NULL, NULL), (3, 12, 1, 7);
INSERT INTO z VALUES (1, 10, 1, 0), (1, 10, 2, -2), (2, 11, 1, 3), (2, 11, 2, -3), (3, 12, 1, 0), (3, 12, 2, 0);

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == matrix product
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == comma join
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * s.val) AS c FROM r, s WHERE r.k = s.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * s.val) AS c FROM r, s WHERE r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * s.val) AS c FROM r, s WHERE r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == chain of three
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == chain of four
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 4;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 4;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 5;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 6;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 7;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 8;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 9;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 10;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 11;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 12;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 13;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 14;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 15;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 16;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 17;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 18;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 19;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == star, one variable in three relations
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, s.k AS b, w.k AS d, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2, 3;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, w.k AS d, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, w.k AS d, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, w.k AS d, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, w.k AS d, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, w.k AS d, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, w.k AS d, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == star grouping on two relations
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, w.k AS b, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, w.k AS b, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, w.k AS b, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, w.k AS b, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, w.k AS b, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, w.k AS b, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, w.k AS b, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == cycle of three
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.extra AS a, SUM(r.val * s.val * t.val) AS c FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.extra AS a, SUM(r.val * s.val * t.val) AS c FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.extra AS a, SUM(r.val * s.val * t.val) AS c FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.extra AS a, SUM(r.val * s.val * t.val) AS c FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.extra AS a, SUM(r.val * s.val * t.val) AS c FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.extra AS a, SUM(r.val * s.val * t.val) AS c FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.extra AS a, SUM(r.val * s.val * t.val) AS c FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.extra AS a, SUM(r.val * s.val * t.val) AS c FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 4;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.extra AS a, SUM(r.val * s.val * t.val) AS c FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == two measures in one relation
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * r.val2 * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * r.val2 * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * r.val2 * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == a measure twice
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * r.val * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * r.val * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * r.val * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == a relation without a measure
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, s.k AS b, SUM(r.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == a relation in the middle without a measure
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == measures in one of three relations
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, t.k AS b, SUM(s.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(s.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(s.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(s.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(s.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(s.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(s.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(s.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(s.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == two join variables
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * r.val2 * s.val) AS c FROM r JOIN s ON r.k = s.row_id AND r.extra = s.extra GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * r.val2 * s.val) AS c FROM r JOIN s ON r.k = s.row_id AND r.extra = s.extra GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.k AS b, SUM(r.val * r.val2 * s.val) AS c FROM r JOIN s ON r.k = s.row_id AND r.extra = s.extra GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == no GROUP BY
CREATE OR REPLACE TEMP TABLE expected AS SELECT SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == one group
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == a group on the join key
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.k AS a, s.k AS b, SUM(r.val * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.k AS a, s.k AS b, SUM(r.val * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.k AS a, s.k AS b, SUM(r.val * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == a group in the middle
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, s.extra AS b, t.k AS d, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2, 3;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.extra AS b, t.k AS d, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.extra AS b, t.k AS d, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.extra AS b, t.k AS d, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.extra AS b, t.k AS d, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.extra AS b, t.k AS d, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, s.extra AS b, t.k AS d, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2, 3;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == self join
CREATE OR REPLACE TEMP TABLE expected AS SELECT a.row_id AS a, b.k AS b, SUM(a.val * b.val) AS c FROM s a JOIN s b ON a.row_id = b.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT a.row_id AS a, b.k AS b, SUM(a.val * b.val) AS c FROM s a JOIN s b ON a.row_id = b.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT a.row_id AS a, b.k AS b, SUM(a.val * b.val) AS c FROM s a JOIN s b ON a.row_id = b.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == predicates on one relation
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND s.row_id % 2 = 0 AND t.extra <= t.val GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND s.row_id % 2 = 0 AND t.extra <= t.val GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND s.row_id % 2 = 0 AND t.extra <= t.val GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND s.row_id % 2 = 0 AND t.extra <= t.val GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND s.row_id % 2 = 0 AND t.extra <= t.val GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND s.row_id % 2 = 0 AND t.extra <= t.val GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND s.row_id % 2 = 0 AND t.extra <= t.val GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == a predicate on two columns of a relation
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.extra <= r.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.extra <= r.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.extra <= r.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.extra <= r.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.extra <= r.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.extra <= r.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.extra <= r.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == a disjunction on a relation
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE s.val > 4 OR s.extra = 2 GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE s.val > 4 OR s.extra = 2 GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE s.val > 4 OR s.extra = 2 GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE s.val > 4 OR s.extra = 2 GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE s.val > 4 OR s.extra = 2 GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE s.val > 4 OR s.extra = 2 GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE s.val > 4 OR s.extra = 2 GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == subqueries as relations
CREATE OR REPLACE TEMP TABLE expected AS SELECT x.row_id AS a, y.k AS b, SUM(x.val * y.val) AS c FROM (SELECT * FROM r WHERE val > 2) x JOIN (SELECT row_id, k, val + 1 AS val FROM s) y ON x.k = y.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.row_id AS a, y.k AS b, SUM(x.val * y.val) AS c FROM (SELECT * FROM r WHERE val > 2) x JOIN (SELECT row_id, k, val + 1 AS val FROM s) y ON x.k = y.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT x.row_id AS a, y.k AS b, SUM(x.val * y.val) AS c FROM (SELECT * FROM r WHERE val > 2) x JOIN (SELECT row_id, k, val + 1 AS val FROM s) y ON x.k = y.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == nested query
CREATE OR REPLACE TEMP TABLE expected AS SELECT rs.row_id AS a, t.k AS b, SUM(rs.v * t.val) AS c FROM (SELECT r.row_id, s.k, SUM(r.val * s.val) v FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2) rs JOIN t ON rs.k = t.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT rs.row_id AS a, t.k AS b, SUM(rs.v * t.val) AS c FROM (SELECT r.row_id, s.k, SUM(r.val * s.val) v FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2) rs JOIN t ON rs.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT rs.row_id AS a, t.k AS b, SUM(rs.v * t.val) AS c FROM (SELECT r.row_id, s.k, SUM(r.val * s.val) v FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2) rs JOIN t ON rs.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == NULL in keys, groups and measures
CREATE OR REPLACE TEMP TABLE expected AS SELECT n.row_id AS a, s.k AS b, SUM(n.val * s.val) AS c FROM n JOIN s ON n.k = s.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT n.row_id AS a, s.k AS b, SUM(n.val * s.val) AS c FROM n JOIN s ON n.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT n.row_id AS a, s.k AS b, SUM(n.val * s.val) AS c FROM n JOIN s ON n.k = s.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == NULL in a chain
CREATE OR REPLACE TEMP TABLE expected AS SELECT n.row_id AS a, t.k AS b, SUM(n.val * s.val * t.val) AS c FROM n JOIN s ON n.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT n.row_id AS a, t.k AS b, SUM(n.val * s.val * t.val) AS c FROM n JOIN s ON n.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT n.row_id AS a, t.k AS b, SUM(n.val * s.val * t.val) AS c FROM n JOIN s ON n.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT n.row_id AS a, t.k AS b, SUM(n.val * s.val * t.val) AS c FROM n JOIN s ON n.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT n.row_id AS a, t.k AS b, SUM(n.val * s.val * t.val) AS c FROM n JOIN s ON n.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT n.row_id AS a, t.k AS b, SUM(n.val * s.val * t.val) AS c FROM n JOIN s ON n.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT n.row_id AS a, t.k AS b, SUM(n.val * s.val * t.val) AS c FROM n JOIN s ON n.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == zero and negative measures
CREATE OR REPLACE TEMP TABLE expected AS SELECT z.row_id AS a, t.k AS b, SUM(z.val * s.val * t.val) AS c FROM z JOIN s ON z.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT z.row_id AS a, t.k AS b, SUM(z.val * s.val * t.val) AS c FROM z JOIN s ON z.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT z.row_id AS a, t.k AS b, SUM(z.val * s.val * t.val) AS c FROM z JOIN s ON z.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT z.row_id AS a, t.k AS b, SUM(z.val * s.val * t.val) AS c FROM z JOIN s ON z.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT z.row_id AS a, t.k AS b, SUM(z.val * s.val * t.val) AS c FROM z JOIN s ON z.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT z.row_id AS a, t.k AS b, SUM(z.val * s.val * t.val) AS c FROM z JOIN s ON z.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT z.row_id AS a, t.k AS b, SUM(z.val * s.val * t.val) AS c FROM z JOIN s ON z.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == an empty relation
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, e.k AS b, SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id GROUP BY 1, 2;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, e.k AS b, SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, e.k AS b, SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, e.k AS b, SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, e.k AS b, SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, e.k AS b, SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, e.k AS b, SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == an empty relation without GROUP BY
CREATE OR REPLACE TEMP TABLE expected AS SELECT SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id;
SELECT 'rows', count(*) FROM expected;
SET mpf_planner = 'cs';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 1;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 2;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
SET mpf_force_plan = 3;
CREATE OR REPLACE TEMP TABLE result AS SELECT SUM(r.val * s.val * e.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN e ON s.k = e.row_id;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

-- The settings
SET mpf_planner = 'off';
RESET mpf_force_plan;
.print == the first number without a plan: the plan of the query runs
CREATE OR REPLACE TEMP TABLE expected AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 4;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == a much larger number
SET mpf_force_plan = 99;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == off: nothing changes
SET mpf_planner = 'off';
SET mpf_force_plan = 0;
CREATE OR REPLACE TEMP TABLE result AS SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected) UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));

-- The SPALM operators of each plan: a MatMul region needs an aggregate directly above a join
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
SET mpf_planner = 'cs_plus';
.print == SPALM: matrix product
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id AS a, s.k AS b, SUM(r.val * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == SPALM: chain of three
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SET mpf_force_plan = 2;
EXPLAIN SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SET mpf_force_plan = 3;
EXPLAIN SELECT r.row_id AS a, t.k AS b, SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == SPALM: chain of four
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 2;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 3;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 4;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 5;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 6;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 7;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 8;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 9;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 10;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 11;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 12;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 13;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 14;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 15;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 16;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 17;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 18;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
SET mpf_force_plan = 19;
EXPLAIN SELECT r.row_id AS a, u.k AS b, SUM(r.val * s.val * t.val * u.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == SPALM: star, one variable in three relations
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id AS a, s.k AS b, w.k AS d, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2, 3;
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id AS a, s.k AS b, w.k AS d, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2, 3;
SET mpf_force_plan = 2;
EXPLAIN SELECT r.row_id AS a, s.k AS b, w.k AS d, SUM(r.val * s.val * w.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN w ON r.k = w.row_id GROUP BY 1, 2, 3;
.print == SPALM: two measures in one relation
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id AS a, s.k AS b, SUM(r.val * r.val2 * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == SPALM: a relation without a measure
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id AS a, s.k AS b, SUM(r.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == SPALM: a relation in the middle without a measure
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id AS a, t.k AS b, SUM(r.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id AS a, t.k AS b, SUM(r.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SET mpf_force_plan = 2;
EXPLAIN SELECT r.row_id AS a, t.k AS b, SUM(r.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SET mpf_force_plan = 3;
EXPLAIN SELECT r.row_id AS a, t.k AS b, SUM(r.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == SPALM: two join variables
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id AS a, s.k AS b, SUM(r.val * r.val2 * s.val) AS c FROM r JOIN s ON r.k = s.row_id AND r.extra = s.extra GROUP BY 1, 2;
.print == SPALM: no GROUP BY
SET mpf_force_plan = 0;
EXPLAIN SELECT SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
SET mpf_force_plan = 1;
EXPLAIN SELECT SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
SET mpf_force_plan = 2;
EXPLAIN SELECT SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
SET mpf_force_plan = 3;
EXPLAIN SELECT SUM(r.val * s.val * t.val) AS c FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
.print == SPALM: a group on the join key
SET mpf_force_plan = 0;
EXPLAIN SELECT r.k AS a, s.k AS b, SUM(r.val * s.val) AS c FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == SPALM: nested query
SET mpf_force_plan = 0;
EXPLAIN SELECT rs.row_id AS a, t.k AS b, SUM(rs.v * t.val) AS c FROM (SELECT r.row_id, s.k, SUM(r.val * s.val) v FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2) rs JOIN t ON rs.k = t.row_id GROUP BY 1, 2;
