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
INSERT INTO r VALUES (1, 1, 1, 2, 3), (2, 2, 2, 1, 1);
INSERT INTO s VALUES (1, 1, 1, 2), (2, 2, 2, 1);
INSERT INTO t VALUES (1, 1, 1, 2), (2, 2, 2, 1);
INSERT INTO u VALUES (1, 1, 1, 2), (2, 2, 2, 1);
INSERT INTO big VALUES (1, 1, 2);
INSERT INTO small VALUES (1, 1, 2);
INSERT INTO text VALUES ('a', 'b', 2);
INSERT INTO mixed VALUES (1, 1, 2);
INSERT INTO bigkey VALUES (1, 1, 2);
-- SPALM must stay away from what it stops on, and from values that it leaves out of its
-- result: statistics tell which columns have NULL or negative coordinates. Any other
-- value is fine: the operator tells at run time which pairs matched.
CREATE TABLE null_group (row_id INTEGER, k INTEGER, val DOUBLE);
CREATE TABLE null_key (row_id INTEGER, k INTEGER, val DOUBLE);
CREATE TABLE null_value (row_id INTEGER, k INTEGER, val DOUBLE);
CREATE TABLE zero_value (row_id INTEGER, k INTEGER, val DOUBLE);
CREATE TABLE negative_value (row_id INTEGER, k INTEGER, val DOUBLE);
CREATE TABLE negative_group (row_id INTEGER, k INTEGER, val DOUBLE);
CREATE TABLE negative_key (row_id INTEGER, k INTEGER, val DOUBLE);
CREATE TABLE null_extra (row_id INTEGER, k INTEGER, extra INTEGER, val DOUBLE);
CREATE TABLE null_factor (row_id INTEGER, k INTEGER, known DOUBLE, maybe DOUBLE);
CREATE TABLE no_rows (row_id INTEGER, k INTEGER, val DOUBLE);
CREATE TABLE deleted (row_id INTEGER, k INTEGER, val DOUBLE);
CREATE TABLE required (row_id INTEGER NOT NULL, k INTEGER NOT NULL, val DOUBLE NOT NULL);
INSERT INTO null_group VALUES (1, 1, 2), (NULL, 2, 3);
INSERT INTO null_key VALUES (1, 1, 2), (2, NULL, 3);
INSERT INTO null_value VALUES (1, 1, 2), (2, 2, NULL);
INSERT INTO null_extra VALUES (1, 1, NULL, 2), (2, 2, -1, 3);
INSERT INTO null_factor VALUES (1, 1, 2, NULL), (2, 2, 3, 1);
INSERT INTO zero_value VALUES (1, 1, 2), (2, 2, 0);
INSERT INTO negative_value VALUES (1, 1, 2), (2, 2, -3);
INSERT INTO negative_group VALUES (1, 1, 2), (-2, 2, 3);
INSERT INTO negative_key VALUES (1, 1, 2), (2, -2, 3);
INSERT INTO deleted VALUES (1, 1, 2), (NULL, 2, 3);
DELETE FROM deleted WHERE row_id IS NULL;
INSERT INTO required VALUES (1, 1, 2);

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
.print == cs_plus: a subquery of columns, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, val AS v FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a product in a subquery, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, val * val2 AS v FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
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
.print == cs_plus: NULL in a group, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM null_group x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: NULL in a join key, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM null_key x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: NULL in a value, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM null_value x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: zero in a value, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM zero_value x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a negative value, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM negative_value x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a negative group, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM negative_group x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a negative join key, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM negative_key x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a table without rows, so there is nothing to get wrong, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM no_rows x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a deleted NULL, which statistics still have, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM deleted x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: NOT NULL, and statistics agree, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM required x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a bad table in a chain, where another MatMul is safe, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM null_value x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a bad table in a chain, where another MatMul is safe, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM null_value x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a bad table in a chain, where another MatMul is safe, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM null_value x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a bad table in a chain, where another MatMul is safe, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM null_value x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a zero and a negative value in a chain, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * y.val * t.val) FROM zero_value x JOIN negative_value y ON x.k = y.row_id JOIN t ON y.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a zero and a negative value in a chain, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * y.val * t.val) FROM zero_value x JOIN negative_value y ON x.k = y.row_id JOIN t ON y.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a zero and a negative value in a chain, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * y.val * t.val) FROM zero_value x JOIN negative_value y ON x.k = y.row_id JOIN t ON y.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a zero and a negative value in a chain, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * y.val * t.val) FROM zero_value x JOIN negative_value y ON x.k = y.row_id JOIN t ON y.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: NULL in one column of a variable, which the plan does not read, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT r.row_id, x.row_id, SUM(r.val * s.val * x.val) FROM r JOIN s ON r.k = s.row_id JOIN null_key x ON s.k = x.k GROUP BY 1, 2;
.print == cs_plus: NULL in one column of a variable, which the plan does not read, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT r.row_id, x.row_id, SUM(r.val * s.val * x.val) FROM r JOIN s ON r.k = s.row_id JOIN null_key x ON s.k = x.k GROUP BY 1, 2;
.print == cs_plus: NULL in one column of a variable, which the plan does not read, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT r.row_id, x.row_id, SUM(r.val * s.val * x.val) FROM r JOIN s ON r.k = s.row_id JOIN null_key x ON s.k = x.k GROUP BY 1, 2;
.print == cs_plus: NULL in one column of a variable, which the plan does not read, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT r.row_id, x.row_id, SUM(r.val * s.val * x.val) FROM r JOIN s ON r.k = s.row_id JOIN null_key x ON s.k = x.k GROUP BY 1, 2;
.print == cs_plus: a join removes the negative keys of one side, where a group reads them, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT s.row_id, t.k, SUM(n.val * s.val * t.val) FROM negative_key n JOIN s ON n.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a join removes the negative keys of one side, where a group reads them, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT s.row_id, t.k, SUM(n.val * s.val * t.val) FROM negative_key n JOIN s ON n.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a join removes the negative keys of one side, where a group reads them, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT s.row_id, t.k, SUM(n.val * s.val * t.val) FROM negative_key n JOIN s ON n.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: a join removes the NULL keys of both sides, where a group reads them, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.k, t.k, SUM(x.val * y.val * t.val) FROM null_key x JOIN null_key y ON x.k = y.k JOIN t ON y.row_id = t.row_id GROUP BY 1, 2;
.print == cs_plus: a join removes the NULL keys of both sides, where a group reads them, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT x.k, t.k, SUM(x.val * y.val * t.val) FROM null_key x JOIN null_key y ON x.k = y.k JOIN t ON y.row_id = t.row_id GROUP BY 1, 2;
.print == cs_plus: a join removes the NULL keys of both sides, where a group reads them, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT x.k, t.k, SUM(x.val * y.val * t.val) FROM null_key x JOIN null_key y ON x.k = y.k JOIN t ON y.row_id = t.row_id GROUP BY 1, 2;
.print == cs_plus: a value that is an expression, which statistics do not reach, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, val * val2 + 1 AS v FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a sum in a subquery, of positive numbers, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, SUM(val) AS v FROM r GROUP BY 1, 2) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a sum in a subquery, of values that are not all positive, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, SUM(val) AS v FROM zero_value GROUP BY 1, 2) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a sum in a subquery, of values that can be NULL, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, SUM(val) AS v FROM null_value GROUP BY 1, 2) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a product in a subquery, with a negative factor, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, val * -2.0 AS v FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a product in a subquery, with a factor that can be NULL, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, val * 2.0 AS v FROM null_value) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a product in a subquery, with a second factor that can be NULL, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, known * maybe AS v FROM null_factor) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a product in a subquery, with a first factor that can be NULL, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, maybe * known AS v FROM null_factor) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a group that is a product of numbers that are not negative, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.g, s.k, SUM(x.val * s.val) FROM (SELECT row_id * 2 AS g, k, val FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a group that is a product with a negative number, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.g, s.k, SUM(x.val * s.val) FROM (SELECT row_id * -2 AS g, k, val FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a group that is a product with a number that is not known, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.g, s.k, SUM(x.val * s.val) FROM (SELECT row_id * extra AS g, k, val FROM null_extra) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a sum with a filter in a subquery, which can be NULL, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, SUM(val) FILTER (WHERE val > 100) AS v FROM r GROUP BY 1, 2) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: ROLLUP groups, which can be NULL, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, SUM(val) AS v FROM r GROUP BY ROLLUP (row_id, k)) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: an average in a subquery, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, AVG(val) AS v FROM r GROUP BY 1, 2) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side, which keeps the left child that the side reads, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT r.* FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side of a chain, which keeps the left child, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM (SELECT r.* FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side of a chain, which keeps the left child, plan 1
SET mpf_force_plan = 1;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM (SELECT r.* FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side of a chain, which keeps the left child, plan 2
SET mpf_force_plan = 2;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM (SELECT r.* FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side of a chain, which keeps the left child, plan 3
SET mpf_force_plan = 3;
EXPLAIN SELECT x.row_id, t.k, SUM(x.val * s.val * t.val) FROM (SELECT r.* FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == cs_plus: RIGHT JOIN below a side, which keeps the right child that the side reads, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT r.* FROM u RIGHT JOIN r ON r.k = u.row_id) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: FULL JOIN below a side, which pads both children, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT r.* FROM r FULL JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: ROLLUP below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT row_id, k, SUM(val) AS v FROM r GROUP BY ROLLUP (row_id, k)) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a scalar subquery below a side, which the side does not read, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT row_id, k, val, (SELECT u.k FROM u WHERE u.row_id = r.row_id) AS y FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: POSITIONAL JOIN below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT * FROM r POSITIONAL JOIN (SELECT k AS other FROM u)) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side, with a value from the child that it pads, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT r.row_id, r.k, u.val FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: LEFT JOIN below a side, with a group from the child that it pads, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT u.row_id, r.k, r.val FROM r LEFT JOIN u ON r.k = u.row_id) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a scalar subquery as the value of a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT row_id, k, (SELECT u.val FROM u WHERE u.row_id = r.row_id) AS val FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a semi join below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT * FROM r WHERE k IN (SELECT row_id FROM u)) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: an anti join below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT * FROM r WHERE NOT EXISTS (SELECT 1 FROM u WHERE u.row_id = r.k)) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: ORDER BY and LIMIT below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT * FROM r ORDER BY row_id LIMIT 10) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: DISTINCT below a side, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT DISTINCT row_id, k, val FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: UNNEST below a side, which has no facts, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT row_id, k, unnest([val]) AS val FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a sum without GROUP BY, which is NULL without rows, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT 1 AS row_id, 1 AS k, SUM(val) AS v FROM r WHERE row_id > 100) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == cs_plus: a sum with GROUP BY (), which is NULL without rows, plan 0
SET mpf_force_plan = 0;
EXPLAIN SELECT x.row_id, s.k, SUM(x.v * s.val) FROM (SELECT 1 AS row_id, 1 AS k, SUM(val) AS v FROM r WHERE row_id > 100 GROUP BY ()) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
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
.print == no planner, and a NULL: DuckDB runs the query
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM null_group x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == rows of this transaction are not in the statistics
SET mpf_planner = 'cs_plus';
SET mpf_force_plan = 0;
BEGIN;
INSERT INTO r VALUES (9, 9, 9, 9, 9);
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
ROLLBACK;
