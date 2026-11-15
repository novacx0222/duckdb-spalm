#include "logical_spalm.hpp"
#include "physical_spalm.hpp"

#include <algorithm>

namespace duckdb {
LogicalSpalm::LogicalSpalm(
    unique_ptr<LogicalAggregate> agg_op, unique_ptr<LogicalComparisonJoin> comp_join_op,
    unique_ptr<LogicalOperator> left, unique_ptr<LogicalOperator> right
)
    : agg_op(std::move(agg_op)), comp_join_op(std::move(comp_join_op)) {
	children.push_back(std::move(left));
	children.push_back(std::move(right));
}

idx_t LogicalSpalm::FindBindingPos(const vector<ColumnBinding> &bindings, ColumnBinding target) {
	for (idx_t i = 0; i < bindings.size(); i++) {
		if (bindings[i] == target) {
			return i;
		}
	}
	throw InternalException("SPALM: column binding not found in child output");
}

static bool HasBinding(const vector<ColumnBinding> &bindings, ColumnBinding target) {
	return std::find(bindings.begin(), bindings.end(), target) != bindings.end();
}

bool LogicalSpalm::FindSides(
    const LogicalAggregate &agg, const LogicalComparisonJoin &join, const vector<ColumnBinding> &lhs_bindings,
    const vector<ColumnBinding> &rhs_bindings, SpalmSides &sides
) {
	const auto join_left = join.conditions[0].left->Cast<BoundColumnRefExpression>().binding;
	const auto join_right = join.conditions[0].right->Cast<BoundColumnRefExpression>().binding;
	if (!HasBinding(lhs_bindings, join_left) || !HasBinding(rhs_bindings, join_right)) {
		return false;
	}

	const auto grp0 = agg.groups[0]->Cast<BoundColumnRefExpression>().binding;
	const auto grp1 = agg.groups[1]->Cast<BoundColumnRefExpression>().binding;
	// A group on the key keeps it, so the join is not a matrix product.
	if (grp0 == join_left || grp0 == join_right || grp1 == join_left || grp1 == join_right) {
		return false;
	}
	if (HasBinding(rhs_bindings, grp0) && HasBinding(lhs_bindings, grp1)) {
		sides.rhs_group = 0;
	} else if (HasBinding(lhs_bindings, grp0) && HasBinding(rhs_bindings, grp1)) {
		sides.rhs_group = 1;
	} else {
		return false;
	}

	auto &agg_expr = agg.expressions[0]->Cast<BoundAggregateExpression>();
	auto &mul_expr = agg_expr.children[0]->Cast<BoundFunctionExpression>();
	const auto val0 = mul_expr.children[0]->Cast<BoundColumnRefExpression>().binding;
	const auto val1 = mul_expr.children[1]->Cast<BoundColumnRefExpression>().binding;
	if (HasBinding(rhs_bindings, val0) && HasBinding(lhs_bindings, val1)) {
		sides.rhs_value = 0;
	} else if (HasBinding(lhs_bindings, val0) && HasBinding(rhs_bindings, val1)) {
		sides.rhs_value = 1;
	} else {
		return false;
	}
	return true;
}

PhysicalOperator &LogicalSpalm::CreatePlan(ClientContext &context, PhysicalPlanGenerator &generator) {
	D_ASSERT(children.size() == 2);
	idx_t lhs_cardinality = children[0]->EstimateCardinality(context);
	idx_t rhs_cardinality = children[1]->EstimateCardinality(context);

	// === Resolve column bindings using logical children ===
	auto lhs_bindings = children[0]->GetColumnBindings();
	auto rhs_bindings = children[1]->GetColumnBindings();

	// Get join condition column bindings
	auto &join_left_expr = comp_join_op->conditions[0].left->Cast<BoundColumnRefExpression>();
	auto &join_right_expr = comp_join_op->conditions[0].right->Cast<BoundColumnRefExpression>();

	SpalmSides sides;
	if (!FindSides(*agg_op, *comp_join_op, lhs_bindings, rhs_bindings, sides)) {
		throw InternalException("SPALM: columns of the matmul do not match the sides of the join");
	}

	SpalmColumnBindings col_bindings;
	col_bindings.lhs_join_idx = FindBindingPos(lhs_bindings, join_left_expr.binding);
	col_bindings.rhs_join_idx = FindBindingPos(rhs_bindings, join_right_expr.binding);

	// Resolve group-by columns
	auto &rhs_grp = agg_op->groups[sides.rhs_group]->Cast<BoundColumnRefExpression>();
	auto &lhs_grp = agg_op->groups[1 - sides.rhs_group]->Cast<BoundColumnRefExpression>();
	col_bindings.rhs_agg_idx = FindBindingPos(rhs_bindings, rhs_grp.binding);
	col_bindings.rhs_output_idx = sides.rhs_group;
	col_bindings.lhs_agg_idx = FindBindingPos(lhs_bindings, lhs_grp.binding);
	col_bindings.lhs_output_idx = 1 - sides.rhs_group;

	// Resolve multiply value columns
	auto &agg_expr = agg_op->expressions[0]->Cast<BoundAggregateExpression>();
	auto &mul_expr = agg_expr.children[0]->Cast<BoundFunctionExpression>();
	auto &rhs_val = mul_expr.children[sides.rhs_value]->Cast<BoundColumnRefExpression>();
	auto &lhs_val = mul_expr.children[1 - sides.rhs_value]->Cast<BoundColumnRefExpression>();
	col_bindings.rhs_val_idx = FindBindingPos(rhs_bindings, rhs_val.binding);
	col_bindings.lhs_val_idx = FindBindingPos(lhs_bindings, lhs_val.binding);

	// === Type resolution ===
	bool use_float = (rhs_val.return_type == LogicalType::FLOAT && lhs_val.return_type == LogicalType::FLOAT);

	// === Create physical plans for children ===
	auto &left = generator.CreatePlan(*children[0]);
	auto &right = generator.CreatePlan(*children[1]);
	left.estimated_cardinality = lhs_cardinality;
	right.estimated_cardinality = rhs_cardinality;

	return generator.Make<PhysicalSpalm>(types, left, right, col_bindings, use_float, estimated_cardinality);
}

vector<ColumnBinding> LogicalSpalm::GetColumnBindings() {
	D_ASSERT(agg_op->groupings_index != DConstants::INVALID_INDEX || agg_op->grouping_functions.empty());
	vector<ColumnBinding> result;
	result.reserve(agg_op->groups.size() + agg_op->expressions.size() + agg_op->grouping_functions.size());
	for (idx_t i = 0; i < agg_op->groups.size(); i++) {
		result.emplace_back(agg_op->group_index, i);
	}
	for (idx_t i = 0; i < agg_op->expressions.size(); i++) {
		result.emplace_back(agg_op->aggregate_index, i);
	}
	for (idx_t i = 0; i < agg_op->grouping_functions.size(); i++) {
		result.emplace_back(agg_op->groupings_index, i);
	}
	return result;
}

void LogicalSpalm::ResolveTypes() {
	if (children.empty()) {
		throw InternalException("Spalm operator needs a child");
	}
	D_ASSERT(agg_op->groupings_index != DConstants::INVALID_INDEX || agg_op->grouping_functions.empty());
	for (auto &expr : agg_op->groups) {
		types.push_back(expr->return_type);
	}
	// get the chunk types from the projection list
	for (auto &expr : agg_op->expressions) {
		types.push_back(expr->return_type);
	}
	for (idx_t i = 0; i < agg_op->grouping_functions.size(); i++) {
		types.emplace_back(LogicalType::BIGINT);
	}
}

string LogicalSpalm::GetExtensionName() const {
	return "spalm";
}
} // namespace duckdb
