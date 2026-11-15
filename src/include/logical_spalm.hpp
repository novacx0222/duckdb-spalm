//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb-spalm/src/include/logical_spalm.hpp
//
//
//===----------------------------------------------------------------------===//
#pragma once

#include "duckdb/planner/logical_operator.hpp"
#include "physical_spalm.hpp"
#include "duckdb/planner/operator/logical_extension_operator.hpp"
#include "duckdb/planner/operator/logical_aggregate.hpp"
#include "duckdb/planner/operator/logical_comparison_join.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/planner/expression/bound_aggregate_expression.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"

namespace duckdb {

// Which group (0/1 in agg.groups) and which value (0/1 among the children of
// the multiply) a matmul reads from the join's right child. The other ones
// come from the left child.
struct SpalmSides {
	idx_t rhs_group;
	idx_t rhs_value;
};

class LogicalSpalm : public LogicalExtensionOperator {
public:
	explicit LogicalSpalm(
	    unique_ptr<LogicalAggregate> agg_op, unique_ptr<LogicalComparisonJoin> comp_join_op,
	    unique_ptr<LogicalOperator> left, unique_ptr<LogicalOperator> right
	);
	unique_ptr<LogicalAggregate> agg_op;
	unique_ptr<LogicalComparisonJoin> comp_join_op;

	string GetName() const override {
		return "SPALM";
	}

	string GetExtensionName() const;

	// Assigns the columns of an Agg-Join pair to the sides of the join, given
	// the outputs of its children. A side can be any operator (an aggregate
	// has one table index for its groups and another for its sums), so the
	// columns are looked up in these outputs. Returns false unless each side
	// has the key of the join, one group and one value, and the key is summed
	// out. The pair must already have the shape IsMatMulPattern checks.
	static bool FindSides(
	    const LogicalAggregate &agg, const LogicalComparisonJoin &join, const vector<ColumnBinding> &lhs_bindings,
	    const vector<ColumnBinding> &rhs_bindings, SpalmSides &sides
	);

public:
	vector<ColumnBinding> GetColumnBindings() override;

	PhysicalOperator &CreatePlan(ClientContext &context, PhysicalPlanGenerator &generator) override;

protected:
	void ResolveTypes() override;

private:
	// Find the output position of a ColumnBinding in a logical child's output
	static idx_t FindBindingPos(const vector<ColumnBinding> &bindings, ColumnBinding target);
};
} // namespace duckdb
