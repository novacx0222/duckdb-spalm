-- The candidate plans of the MPF planners. Run by tests/mpf_plan_test.sh; EXPLAIN only plans the queries,
-- and tests/mpf_execute_test.sh runs them.
-- S(r) is a table, J[v](x y) a join on v, P[v](x) a projection and A[v](x) an aggregate grouping by v.
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';

CREATE TABLE r (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE, val2 DOUBLE);
CREATE TABLE s (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
CREATE TABLE t (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
CREATE TABLE u (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);

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
CREATE TABLE c10 (a INTEGER, b INTEGER, val DOUBLE);
CREATE TABLE c11 (a INTEGER, b INTEGER, val DOUBLE);
CREATE TABLE c12 (a INTEGER, b INTEGER, val DOUBLE);

SET mpf_planner = 'cs';
.print == cs: matrix product
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs: chain of three
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs: chain of four, with two sides of two relations
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs: star, one variable in three relations
EXPLAIN SELECT r.row_id, s.k, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON r.k = t.row_id GROUP BY 1, 2, 3;
.print == cs: cycle of three
EXPLAIN SELECT r.extra, SUM(r.val * s.val * t.val) FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.k = r.row_id GROUP BY 1;
.print == cs: two measures in one relation need a projection
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs: a measure twice needs a projection
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs: a relation without a measure needs a projection
EXPLAIN SELECT r.row_id, s.k, SUM(r.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs: a relation in the middle without a measure
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs: two join variables are no MatMul
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id AND r.extra = s.extra GROUP BY 1, 2;
.print == cs: no GROUP BY is no MatMul
EXPLAIN SELECT SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id;
.print == cs: one group is no MatMul
EXPLAIN SELECT r.row_id, SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1;
.print == cs: a group on the join key is no MatMul
EXPLAIN SELECT r.k, s.k, SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs: self join
EXPLAIN SELECT a.row_id, b.k, SUM(a.val * b.val) FROM r a JOIN r b ON a.k = b.row_id GROUP BY 1, 2;
.print == cs: predicates change nothing
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND t.row_id % 3 = 0 GROUP BY 1, 2;
.print == cs: nested query, each aggregate has its plans
EXPLAIN SELECT rs.row_id, t.k, SUM(rs.v * t.val) FROM (SELECT r.row_id, s.k, SUM(r.val * s.val) v FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2) rs JOIN t ON rs.k = t.row_id GROUP BY 1, 2;

SET mpf_planner = 'cs_plus';
.print == cs_plus: matrix product
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: chain of three
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, with two sides of two relations
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: star, one variable in three relations
EXPLAIN SELECT r.row_id, s.k, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON r.k = t.row_id GROUP BY 1, 2, 3;
.print == cs_plus: cycle of three
EXPLAIN SELECT r.extra, SUM(r.val * s.val * t.val) FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.k = r.row_id GROUP BY 1;
.print == cs_plus: two measures in one relation need a projection
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a measure twice needs a projection
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a relation without a measure needs a projection
EXPLAIN SELECT r.row_id, s.k, SUM(r.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a relation in the middle without a measure
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: two join variables are no MatMul
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id AND r.extra = s.extra GROUP BY 1, 2;
.print == cs_plus: no GROUP BY is no MatMul
EXPLAIN SELECT SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id;
.print == cs_plus: one group is no MatMul
EXPLAIN SELECT r.row_id, SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1;
.print == cs_plus: a group on the join key is no MatMul
EXPLAIN SELECT r.k, s.k, SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: self join
EXPLAIN SELECT a.row_id, b.k, SUM(a.val * b.val) FROM r a JOIN r b ON a.k = b.row_id GROUP BY 1, 2;
.print == cs_plus: predicates change nothing
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND t.row_id % 3 = 0 GROUP BY 1, 2;
.print == cs_plus: nested query, each aggregate has its plans
EXPLAIN SELECT rs.row_id, t.k, SUM(rs.v * t.val) FROM (SELECT r.row_id, s.k, SUM(r.val * s.val) v FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2) rs JOIN t ON rs.k = t.row_id GROUP BY 1, 2;

SET mpf_planner = 'cs_plus';
.print == a forced plan
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == the last plan
SET mpf_force_plan = 3;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == no such plan
SET mpf_force_plan = 4;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == every plan again
RESET mpf_force_plan;
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;

.print == 9 relations is a lot of plans, so print one
SET mpf_force_plan = 0;
EXPLAIN SELECT c0.a, c8.b, SUM(c0.val * c1.val * c2.val * c3.val * c4.val * c5.val * c6.val * c7.val * c8.val) FROM c0, c1, c2, c3, c4, c5, c6, c7, c8 WHERE c0.b = c1.a AND c1.b = c2.a AND c2.b = c3.a AND c3.b = c4.a AND c4.b = c5.a AND c5.b = c6.a AND c6.b = c7.a AND c7.b = c8.a GROUP BY 1, 2;
.print == 10 relations are too many plans
EXPLAIN SELECT c0.a, c9.b, SUM(c0.val * c1.val * c2.val * c3.val * c4.val * c5.val * c6.val * c7.val * c8.val * c9.val) FROM c0, c1, c2, c3, c4, c5, c6, c7, c8, c9 WHERE c0.b = c1.a AND c1.b = c2.a AND c2.b = c3.a AND c3.b = c4.a AND c4.b = c5.a AND c5.b = c6.a AND c6.b = c7.a AND c7.b = c8.a AND c8.b = c9.a GROUP BY 1, 2;
.print == 13 relations are too many relations
SET mpf_planner = 'cs';
EXPLAIN SELECT c0.a, c12.b, SUM(c0.val * c1.val * c2.val * c3.val * c4.val * c5.val * c6.val * c7.val * c8.val * c9.val * c10.val * c11.val * c12.val) FROM c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12 WHERE c0.b = c1.a AND c1.b = c2.a AND c2.b = c3.a AND c3.b = c4.a AND c4.b = c5.a AND c5.b = c6.a AND c6.b = c7.a AND c7.b = c8.a AND c8.b = c9.a AND c9.b = c10.a AND c10.b = c11.a AND c11.b = c12.a GROUP BY 1, 2;
.print == 12 relations are fine for CS
EXPLAIN SELECT c0.a, c11.b, SUM(c0.val * c1.val * c2.val * c3.val * c4.val * c5.val * c6.val * c7.val * c8.val * c9.val * c10.val * c11.val) FROM c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11 WHERE c0.b = c1.a AND c1.b = c2.a AND c2.b = c3.a AND c3.b = c4.a AND c4.b = c5.a AND c5.b = c6.a AND c6.b = c7.a AND c7.b = c8.a AND c8.b = c9.a AND c9.b = c10.a AND c10.b = c11.a GROUP BY 1, 2;
RESET mpf_force_plan;

SET mpf_planner = 'off';
SET mpf_force_plan = 0;
.print == off: nothing to report
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == unknown plan number
.bail off
SET mpf_force_plan = -2;
