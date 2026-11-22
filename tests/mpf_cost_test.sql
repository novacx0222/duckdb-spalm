-- The cost model of the MPF planners: SET mpf_cost, mpf_top_k, mpf_cost_ratio and
-- mpf_cost_weights. Run by tests/mpf_cost_test.sh, which leaves out the estimated
-- costs themselves and keeps which plans are kept and chosen.
.bail off
.mode csv
.headers off
-- One thread while the tables are made, so that their statistics are always the same
SET threads = 1;
-- 'extension' keeps SPALM out, so that the chosen plans run without its library
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, common_subplan, extension';

-- The RST query of the m4 experiments: R(i, k) and S(j, k) are sparse matrices, and
-- each j is in T zero, one or two times
CREATE TABLE R AS SELECT DISTINCT (hash(x, 1) % 1000)::INTEGER AS row, (hash(x, 2) % 1000)::INTEGER AS col,
    (1 + x % 10)::DOUBLE AS val FROM range(20000) t(x);
CREATE TABLE S AS SELECT DISTINCT (hash(x, 3) % 1000)::INTEGER AS row, (hash(x, 4) % 1000)::INTEGER AS col,
    (1 + x % 7)::DOUBLE AS val FROM range(20000) t(x);
CREATE TABLE T AS SELECT j::INTEGER AS j FROM range(1000) t(j), range(2) c(n) WHERE n < hash(j, 5) % 3;
-- Chains, for the number of plans
CREATE TABLE c0 (a INTEGER, b INTEGER, val DOUBLE);
CREATE TABLE c1 (a INTEGER, b INTEGER, val DOUBLE);
CREATE TABLE c2 (a INTEGER, b INTEGER, val DOUBLE);
CREATE TABLE c3 (a INTEGER, b INTEGER, val DOUBLE);
CREATE TABLE c4 (a INTEGER, b INTEGER, val DOUBLE);
CREATE TABLE c5 (a INTEGER, b INTEGER, val DOUBLE);
CREATE TABLE c6 (a INTEGER, b INTEGER, val DOUBLE);
CREATE TABLE c7 (a INTEGER, b INTEGER, val DOUBLE);
CREATE TABLE c8 (a INTEGER, b INTEGER, val DOUBLE);
CREATE TABLE c9 (a INTEGER, b INTEGER, val DOUBLE);
SET threads = 2;
CREATE TABLE expected AS SELECT R.row AS i, S.row AS j, SUM(R.val * S.val) AS total FROM R, S, T
    WHERE R.col = S.col AND S.row = T.j GROUP BY R.row, S.row;

.print == defaults: the planners behave as in m4
SELECT 'setting', current_setting('mpf_cost'), current_setting('mpf_top_k'), current_setting('mpf_cost_ratio'),
    current_setting('mpf_cost_weights');
.print == wrong: mpf_cost
SET mpf_cost = 'cheapest';
.print == wrong: mpf_top_k
SET mpf_top_k = -1;
.print == wrong: mpf_cost_ratio between 0 and 1
SET mpf_cost_ratio = 0.5;
.print == wrong: an unknown weight
SET mpf_cost_weights = 'scan=1, nope=2';
.print == wrong: a negative weight
SET mpf_cost_weights = 'scan=-1';
.print == wrong: a weight without a value
SET mpf_cost_weights = 'scan';

SET mpf_planner = 'cs_plus';
SET mpf_hints = 'R(row, col), S(row, col)';
.print == report: the 7 plans with hints; on tables this small, DuckDB's joins are cheapest
SET mpf_cost = 'report';
EXPLAIN SELECT R.row AS i, S.row AS j, SUM(R.val * S.val) AS total FROM R, S, T
    WHERE R.col = S.col AND S.row = T.j GROUP BY R.row, S.row;
.print == report: without SPALM's fixed cost per call, a MatMul plan is cheapest
SET mpf_cost_weights = 'n_spalm=0';
EXPLAIN SELECT R.row AS i, S.row AS j, SUM(R.val * S.val) AS total FROM R, S, T
    WHERE R.col = S.col AND S.row = T.j GROUP BY R.row, S.row;
.print == top_k 1: one subplan per class, so one plan
SET mpf_top_k = 1;
EXPLAIN SELECT R.row AS i, S.row AS j, SUM(R.val * S.val) AS total FROM R, S, T
    WHERE R.col = S.col AND S.row = T.j GROUP BY R.row, S.row;
.print == top_k 2
SET mpf_top_k = 2;
EXPLAIN SELECT R.row AS i, S.row AS j, SUM(R.val * S.val) AS total FROM R, S, T
    WHERE R.col = S.col AND S.row = T.j GROUP BY R.row, S.row;
.print == ratio 2: the subplans and plans within twice the best of their class
RESET mpf_top_k;
SET mpf_cost_ratio = 2;
EXPLAIN SELECT R.row AS i, S.row AS j, SUM(R.val * S.val) AS total FROM R, S, T
    WHERE R.col = S.col AND S.row = T.j GROUP BY R.row, S.row;
RESET mpf_cost_ratio;

.print == choose: runs the cheapest plan, which returns the result of the query
SET mpf_cost = 'choose';
CREATE OR REPLACE TABLE result AS SELECT R.row AS i, S.row AS j, SUM(R.val * S.val) AS total FROM R, S, T
    WHERE R.col = S.col AND S.row = T.j GROUP BY R.row, S.row;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected)
    UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == choose with the default weights
RESET mpf_cost_weights;
CREATE OR REPLACE TABLE result AS SELECT R.row AS i, S.row AS j, SUM(R.val * S.val) AS total FROM R, S, T
    WHERE R.col = S.col AND S.row = T.j GROUP BY R.row, S.row;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected)
    UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == choose, and top_k 2
SET mpf_cost_weights = 'n_spalm=0';
SET mpf_top_k = 2;
CREATE OR REPLACE TABLE result AS SELECT R.row AS i, S.row AS j, SUM(R.val * S.val) AS total FROM R, S, T
    WHERE R.col = S.col AND S.row = T.j GROUP BY R.row, S.row;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected)
    UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
.print == a forced plan comes first, and is a plan of the pruned list
SET mpf_force_plan = 1;
CREATE OR REPLACE TABLE result AS SELECT R.row AS i, S.row AS j, SUM(R.val * S.val) AS total FROM R, S, T
    WHERE R.col = S.col AND S.row = T.j GROUP BY R.row, S.row;
SELECT 'differences', count(*) FROM (SELECT * FROM (SELECT * FROM result EXCEPT ALL SELECT * FROM expected)
    UNION ALL SELECT * FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM result));
RESET mpf_force_plan;
RESET mpf_top_k;
RESET mpf_cost_weights;
RESET mpf_cost;
RESET mpf_hints;

.print == 10 relations: too many plans to keep all of them
SET mpf_force_plan = 0;
EXPLAIN SELECT c0.a, c9.b, SUM(c0.val * c1.val * c2.val * c3.val * c4.val * c5.val * c6.val * c7.val * c8.val * c9.val) FROM c0, c1, c2, c3, c4, c5, c6, c7, c8, c9 WHERE c0.b = c1.a AND c1.b = c2.a AND c2.b = c3.a AND c3.b = c4.a AND c4.b = c5.a AND c5.b = c6.a AND c6.b = c7.a AND c7.b = c8.a AND c8.b = c9.a GROUP BY 1, 2;
.print == 10 relations: but 2 per class are few
SET mpf_top_k = 2;
EXPLAIN SELECT c0.a, c9.b, SUM(c0.val * c1.val * c2.val * c3.val * c4.val * c5.val * c6.val * c7.val * c8.val * c9.val) FROM c0, c1, c2, c3, c4, c5, c6, c7, c8, c9 WHERE c0.b = c1.a AND c1.b = c2.a AND c2.b = c3.a AND c3.b = c4.a AND c4.b = c5.a AND c5.b = c6.a AND c6.b = c7.a AND c7.b = c8.a AND c8.b = c9.a GROUP BY 1, 2;
.print == cs and top_k 1: a chain of three
SET mpf_planner = 'cs';
SET mpf_top_k = 1;
EXPLAIN SELECT c0.a, c2.b, SUM(c0.val * c1.val * c2.val) FROM c0, c1, c2 WHERE c0.b = c1.a AND c1.b = c2.a GROUP BY 1, 2;
RESET mpf_top_k;
RESET mpf_force_plan;
