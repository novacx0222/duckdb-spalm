-- Whether the MatMul regions that the MPF planners predict are the SPALM operators of
-- a plan. Run by tests/mpf_matmul_test.sh; EXPLAIN only plans the queries, so the
-- library is not needed. common_subplan is off: it can turn two equal regions into one.
SET disabled_optimizers = 'compressed_materialization, statistics_propagation, common_subplan';

CREATE TABLE r (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE, val2 DOUBLE);
CREATE TABLE s (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
CREATE TABLE t (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
CREATE TABLE u (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
-- SPALM reads coordinates as INTEGER
CREATE TABLE big (row_id BIGINT, k BIGINT, val DOUBLE);
CREATE TABLE small (row_id SMALLINT, k SMALLINT, val DOUBLE);
CREATE TABLE text (row_id VARCHAR, k VARCHAR, val DOUBLE);
CREATE TABLE mixed (row_id BIGINT, k INTEGER, val DOUBLE);
CREATE TABLE bigkey (row_id INTEGER, k BIGINT, val DOUBLE);

SET mpf_planner = 'cs_plus';
.print == cs_plus: matrix product, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: chain of three, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: chain of three, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: chain of three, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: chain of three, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 4
SET mpf_force_plan = 4;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 5
SET mpf_force_plan = 5;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 6
SET mpf_force_plan = 6;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 7
SET mpf_force_plan = 7;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 8
SET mpf_force_plan = 8;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 9
SET mpf_force_plan = 9;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 10
SET mpf_force_plan = 10;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 11
SET mpf_force_plan = 11;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 12
SET mpf_force_plan = 12;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 13
SET mpf_force_plan = 13;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 14
SET mpf_force_plan = 14;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 15
SET mpf_force_plan = 15;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 16
SET mpf_force_plan = 16;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 17
SET mpf_force_plan = 17;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 18
SET mpf_force_plan = 18;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: chain of four, plan 19
SET mpf_force_plan = 19;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs_plus: the sides are swapped in the query, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT t.k, r.row_id, SUM(t.val * s.val * r.val) FROM t JOIN s ON s.k = t.row_id JOIN r ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: the sides are swapped in the query, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT t.k, r.row_id, SUM(t.val * s.val * r.val) FROM t JOIN s ON s.k = t.row_id JOIN r ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: the sides are swapped in the query, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT t.k, r.row_id, SUM(t.val * s.val * r.val) FROM t JOIN s ON s.k = t.row_id JOIN r ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: the sides are swapped in the query, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT t.k, r.row_id, SUM(t.val * s.val * r.val) FROM t JOIN s ON s.k = t.row_id JOIN r ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: star with three groups, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, s.k, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON r.k = t.row_id GROUP BY 1, 2, 3;
.print == cs_plus: star with three groups, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id, s.k, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON r.k = t.row_id GROUP BY 1, 2, 3;
.print == cs_plus: star with three groups, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT r.row_id, s.k, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON r.k = t.row_id GROUP BY 1, 2, 3;
.print == cs_plus: star with two groups, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON r.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: star with two groups, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON r.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: star with two groups, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON r.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: cycle, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.extra, SUM(r.val * s.val * t.val) FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
.print == cs_plus: cycle, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT r.extra, SUM(r.val * s.val * t.val) FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
.print == cs_plus: cycle, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT r.extra, SUM(r.val * s.val * t.val) FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
.print == cs_plus: cycle, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT r.extra, SUM(r.val * s.val * t.val) FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
.print == cs_plus: cycle, plan 4
SET mpf_force_plan = 4;
EXPLAIN SELECT r.extra, SUM(r.val * s.val * t.val) FROM r, s, t WHERE r.k = s.row_id AND s.k = t.row_id AND t.extra = r.extra GROUP BY 1;
.print == cs_plus: two measures in one relation, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a measure twice, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a relation without a measure, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, s.k, SUM(r.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a relation in the middle without a measure, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a relation in the middle without a measure, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a relation in the middle without a measure, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a relation in the middle without a measure, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: two join variables, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id AND r.extra = s.extra GROUP BY 1, 2;
.print == cs_plus: no GROUP BY, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id;
.print == cs_plus: no GROUP BY, chain, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
.print == cs_plus: no GROUP BY, chain, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
.print == cs_plus: no GROUP BY, chain, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
.print == cs_plus: no GROUP BY, chain, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id;
.print == cs_plus: one group, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1;
.print == cs_plus: three groups, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, r.extra, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2, 3;
.print == cs_plus: both groups in one relation, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, r.extra, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a group on the join key, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.k, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: self join, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT a.row_id, b.k, SUM(a.val * b.val) FROM s a JOIN s b ON a.k = b.row_id GROUP BY 1, 2;
.print == cs_plus: predicates on one relation, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND s.extra <= s.k GROUP BY 1, 2;
.print == cs_plus: predicates on one relation, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND s.extra <= s.k GROUP BY 1, 2;
.print == cs_plus: predicates on one relation, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND s.extra <= s.k GROUP BY 1, 2;
.print == cs_plus: predicates on one relation, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id WHERE r.val > 1 AND s.extra <= s.k GROUP BY 1, 2;
.print == cs_plus: a subquery with an expression, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, val + 1 AS v FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: BIGINT coordinates, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT a.row_id, b.k, SUM(a.val * b.val) FROM big a JOIN big b ON a.k = b.row_id GROUP BY 1, 2;
.print == cs_plus: SMALLINT coordinates, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT a.row_id, b.k, SUM(a.val * b.val) FROM small a JOIN small b ON a.k = b.row_id GROUP BY 1, 2;
.print == cs_plus: VARCHAR coordinates, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT a.row_id, b.k, SUM(a.val * b.val) FROM text a JOIN text b ON a.k = b.row_id GROUP BY 1, 2;
.print == cs_plus: a BIGINT group, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT m.row_id, s.k, SUM(m.val * s.val) FROM mixed m JOIN s ON m.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a BIGINT join key, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT a.row_id, b.row_id, SUM(a.val * b.val) FROM bigkey a JOIN bigkey b ON a.k = b.k GROUP BY 1, 2;
.print == cs_plus: a chain with one BIGINT group, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT m.row_id, t.k, SUM(m.val * s.val * t.val) FROM mixed m JOIN s ON m.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a chain with one BIGINT group, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT m.row_id, t.k, SUM(m.val * s.val * t.val) FROM mixed m JOIN s ON m.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a chain with one BIGINT group, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT m.row_id, t.k, SUM(m.val * s.val * t.val) FROM mixed m JOIN s ON m.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a chain with one BIGINT group, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT m.row_id, t.k, SUM(m.val * s.val * t.val) FROM mixed m JOIN s ON m.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT r.* FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side of a chain, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM (SELECT r.* FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side of a chain, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM (SELECT r.* FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side of a chain, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM (SELECT r.* FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side of a chain, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM (SELECT r.* FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: RIGHT JOIN below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT r.* FROM u RIGHT JOIN r ON r.k = u.row_id) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: FULL JOIN below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT r.* FROM r FULL JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: ROLLUP below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, SUM(val) AS v FROM r GROUP BY ROLLUP (row_id, k)) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a scalar subquery below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT row_id, k, val, (SELECT u.k FROM u WHERE u.row_id = r.row_id) AS y FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: POSITIONAL JOIN below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT * FROM r POSITIONAL JOIN (SELECT k AS other FROM u)) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: an inner join below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT r.row_id, r.k, SUM(r.val) AS v FROM r JOIN u ON r.k = u.row_id GROUP BY 1, 2) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a window below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, SUM(val) OVER (PARTITION BY k) AS v FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a plain aggregate below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, SUM(val) AS v FROM r GROUP BY 1, 2) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
SET mpf_planner = 'cs';
.print == cs: chain of three, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs: chain of three, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs: chain of four, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs: chain of four, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == cs: a relation in the middle without a measure, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs: a relation in the middle without a measure, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
SET mpf_planner = 'cs_plus';
.print == nested query, every aggregate runs plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT rs.row_id, t.k, SUM(rs.v * t.val) FROM (SELECT r.row_id, s.k, SUM(r.val * s.val) v FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2) rs JOIN t ON rs.k = t.row_id GROUP BY 1, 2;
.print == no query from this planner: the plan of DuckDB matches on its own
SET mpf_planner = 'off';
RESET mpf_force_plan;
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
