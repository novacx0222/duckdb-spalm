-- Which queries the MPF entry of the join order optimizer accepts, and what it
-- reads from them. Run by tests/mpf_query_test.sh; EXPLAIN only plans them.
SET disabled_optimizers = 'compressed_materialization, statistics_propagation';
SET mpf_planner = 'cs_plus';

CREATE TABLE r (row_id INTEGER, k INTEGER, val DOUBLE, val2 DOUBLE, extra INTEGER, big BIGINT);
CREATE TABLE s (row_id INTEGER, k INTEGER, val DOUBLE);
CREATE TABLE t (row_id INTEGER, k INTEGER, val DOUBLE);
CREATE TABLE u (row_id INTEGER, k INTEGER, val DOUBLE);

.print == accepted: matrix product
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == accepted: comma join
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r, s WHERE r.k = s.row_id GROUP BY 1, 2;
.print == accepted: chain of three
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == accepted: chain of four
EXPLAIN SELECT r.row_id, u.k, SUM(r.val * s.val * t.val * u.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id JOIN u ON t.k = u.row_id GROUP BY 1, 2;
.print == accepted: star, one variable in three relations
EXPLAIN SELECT r.row_id, s.k, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON r.k = t.row_id GROUP BY 1, 2, 3;
.print == accepted: two measures in one relation
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * r.val2 * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == accepted: a measure twice
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == accepted: a relation without a measure
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * t.val) FROM r JOIN s ON r.k = s.row_id JOIN t ON s.k = t.row_id GROUP BY 1, 2;
.print == accepted: one measure
EXPLAIN SELECT r.row_id, s.k, SUM(r.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == accepted: group on the join key
EXPLAIN SELECT r.k, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == accepted: no GROUP BY
EXPLAIN SELECT SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id;
.print == accepted: self join
EXPLAIN SELECT a.row_id, b.k, SUM(a.val * b.val) FROM r a JOIN r b ON a.k = b.row_id GROUP BY 1, 2;
.print == accepted: predicates on one relation
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id WHERE r.val > 1 AND r.row_id % 3 = 0 AND r.row_id < r.extra AND s.val < 4 GROUP BY 1, 2;
.print == accepted: nested query, the outer query first has its relations planned
EXPLAIN SELECT rs.row_id, t.k, SUM(rs.v * t.val) FROM (SELECT r.row_id, s.k, SUM(r.val * s.val) v FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2) rs JOIN t ON rs.k = t.row_id GROUP BY 1, 2;

.print == rejected: not a SUM
EXPLAIN SELECT r.row_id, s.k, AVG(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == rejected: COUNT
EXPLAIN SELECT r.row_id, s.k, COUNT(*) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == rejected: two aggregates
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val), MAX(r.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == rejected: DISTINCT
EXPLAIN SELECT r.row_id, s.k, SUM(DISTINCT r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == rejected: FILTER
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FILTER (WHERE r.val > 1) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == rejected: a sum of products
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val + 1) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == rejected: a constant factor
EXPLAIN SELECT r.row_id, s.k, SUM(2 * r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == rejected: not DOUBLE
EXPLAIN SELECT r.row_id, s.k, SUM(r.extra * s.row_id) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == rejected: GROUP BY of an expression
EXPLAIN SELECT r.row_id + 1, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == rejected: ROLLUP
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY ROLLUP (1, 2);
.print == rejected: LEFT JOIN
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r LEFT JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == rejected: a LEFT JOIN as a relation
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * t.val) FROM r LEFT JOIN s ON r.k = s.row_id JOIN t ON r.row_id = t.row_id GROUP BY 1, 2;
.print == rejected: semi join
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id WHERE r.row_id IN (SELECT row_id FROM t) GROUP BY 1, 2;
.print == rejected: anti join
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id WHERE r.row_id NOT IN (SELECT row_id FROM t) GROUP BY 1, 2;
.print == rejected: predicate over two relations
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id WHERE r.row_id < s.k GROUP BY 1, 2;
.print == rejected: OR over two relations
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id WHERE r.row_id = 1 OR s.k = 2 GROUP BY 1, 2;
.print == rejected: predicate over two relations in the join
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id AND r.val < s.val GROUP BY 1, 2;
.print == rejected: expression join key
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k + 1 = s.row_id GROUP BY 1, 2;
.print == rejected: cast join key
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.big = s.row_id GROUP BY 1, 2;
.print == rejected: cross product
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r, s GROUP BY 1, 2;
.print == rejected: a chain with a cross product
EXPLAIN SELECT r.row_id, t.k, SUM(r.val * s.val * t.val) FROM r JOIN s ON r.k = s.row_id, t GROUP BY 1, 2;
.print == rejected: one table
EXPLAIN SELECT row_id, k, SUM(val * val2) FROM r GROUP BY 1, 2;
.print == rejected: two columns of a table in a variable
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id AND r.extra = s.row_id GROUP BY 1, 2;
.print == rejected: two groups of a variable
EXPLAIN SELECT r.k, s.row_id, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == rejected: a measure in the join
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.val = s.val GROUP BY 1, 2;
.print == rejected: a measure in the GROUP BY
EXPLAIN SELECT r.val, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;

.print == rejected: LIMIT in a relation
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT * FROM r LIMIT 10) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == rejected: UNION in a relation
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT row_id, k, val FROM s UNION ALL SELECT row_id, k, val FROM t) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == scalar subquery in a predicate
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id WHERE r.val > (SELECT avg(val) FROM t) GROUP BY 1, 2;
.print == correlated subquery in a predicate
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id WHERE r.val > (SELECT avg(val) FROM t WHERE t.row_id = r.row_id) GROUP BY 1, 2;
.print == common table expression
EXPLAIN WITH x AS MATERIALIZED (SELECT * FROM r) SELECT x.row_id, s.k, SUM(x.val * s.val) FROM x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == window function in a relation
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT row_id, k, sum(val) OVER (PARTITION BY k) val FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;
.print == DISTINCT in a relation
EXPLAIN SELECT x.row_id, s.k, SUM(x.val * s.val) FROM (SELECT DISTINCT row_id, k, val FROM r) x JOIN s ON x.k = s.row_id GROUP BY 1, 2;

.print == off: nothing to report
SET mpf_planner = 'off';
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
SET mpf_planner = 'CS';
.print == CS in capitals
EXPLAIN SELECT r.row_id, s.k, SUM(r.val * s.val) FROM r JOIN s ON r.k = s.row_id GROUP BY 1, 2;
.print == unknown planner
.bail off
SET mpf_planner = 'cs+';
