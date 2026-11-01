// spalm_extension.cpp
#include "spalm_extension.hpp"

#include "duckdb/common/string_util.hpp"
#include "duckdb/planner/operator/logical_aggregate.hpp"
#include "duckdb/planner/operator/logical_comparison_join.hpp"
#include "duckdb/planner/operator/logical_join.hpp"
#include "duckdb/planner/expression/bound_aggregate_expression.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "logical_spalm.hpp"
#include "physical_spalm.hpp"

namespace duckdb {

// Whether an operator below a side can turn a group into NULL: an outer join
// pads its columns, and ROLLUP and the like drop groups. The operator fails
// on a NULL row ID at run time, too late to leave the query to DuckDB.
static bool CanAddNullGroups(const LogicalOperator &op) {
	switch (op.type) {
	case LogicalOperatorType::LOGICAL_COMPARISON_JOIN:
	case LogicalOperatorType::LOGICAL_ASOF_JOIN:
	case LogicalOperatorType::LOGICAL_DELIM_JOIN:
	case LogicalOperatorType::LOGICAL_ANY_JOIN: {
		const auto join_type = op.Cast<LogicalJoin>().join_type;
		if (IsLeftOuterJoin(join_type) || IsRightOuterJoin(join_type) || join_type == JoinType::SINGLE) {
			return true;
		}
		break;
	}
	case LogicalOperatorType::LOGICAL_POSITIONAL_JOIN:
		return true;
	case LogicalOperatorType::LOGICAL_AGGREGATE_AND_GROUP_BY:
		if (op.Cast<LogicalAggregate>().grouping_sets.size() > 1) {
			return true;
		}
		break;
	default:
		break;
	}
	for (auto &child : op.children) {
		if (CanAddNullGroups(*child)) {
			return true;
		}
	}
	return false;
}

static bool IsMatMulPattern(LogicalAggregate &agg, LogicalComparisonJoin &join) {
	// Matrix multiplication only reproduces an INNER equi-join: outer joins
	// emit unmatched rows, and an ON-clause predicate filters rows that SPALM
	// would otherwise include.
	if (join.join_type != JoinType::INNER || join.predicate) {
		return false;
	}
	// A plain GROUP BY: no GROUPING SETS / ROLLUP / CUBE or GROUPING().
	if (agg.grouping_sets.size() > 1 || !agg.grouping_functions.empty()) {
		return false;
	}
	// Must have exactly 1 aggregate expression: SUM(...)
	if (agg.expressions.size() != 1) {
		return false;
	}
	if (agg.expressions[0]->expression_class != ExpressionClass::BOUND_AGGREGATE) {
		return false;
	}
	auto &agg_expr = agg.expressions[0]->Cast<BoundAggregateExpression>();
	if (agg_expr.function.name != "sum") {
		return false;
	}
	// SUM(DISTINCT ...), FILTER (WHERE ...) and ORDER BY change the result.
	if (agg_expr.IsDistinct() || agg_expr.filter || agg_expr.order_bys) {
		return false;
	}
	// The operator reads INTEGER keys and DOUBLE values and writes a DOUBLE
	// sum. Other types (BIGINT keys, INTEGER/DECIMAL values with HUGEINT or
	// DECIMAL sums) were reinterpreted as raw memory; leave them to DuckDB.
	// SUM has no FLOAT overload, so FLOAT products arrive wrapped in a cast
	// and never reach this point.
	if (agg_expr.return_type != LogicalType::DOUBLE) {
		return false;
	}
	// SUM must have exactly 1 child: the multiply function
	if (agg_expr.children.size() != 1) {
		return false;
	}
	if (agg_expr.children[0]->expression_class != ExpressionClass::BOUND_FUNCTION) {
		return false;
	}
	auto &mul_expr = agg_expr.children[0]->Cast<BoundFunctionExpression>();
	if (mul_expr.function.name != "*") {
		return false;
	}
	// Multiply must have exactly 2 children, both column refs
	if (mul_expr.children.size() != 2) {
		return false;
	}
	if (mul_expr.children[0]->expression_class != ExpressionClass::BOUND_COLUMN_REF ||
	    mul_expr.children[1]->expression_class != ExpressionClass::BOUND_COLUMN_REF) {
		return false;
	}

	// Must have exactly 2 group-by expressions, both column refs
	if (agg.groups.size() != 2) {
		return false;
	}
	if (agg.groups[0]->expression_class != ExpressionClass::BOUND_COLUMN_REF ||
	    agg.groups[1]->expression_class != ExpressionClass::BOUND_COLUMN_REF) {
		return false;
	}

	// Must have exactly 1 join condition with COMPARE_EQUAL
	if (join.conditions.size() != 1) {
		return false;
	}
	if (join.conditions[0].comparison != ExpressionType::COMPARE_EQUAL) {
		return false;
	}
	if (join.conditions[0].left->expression_class != ExpressionClass::BOUND_COLUMN_REF ||
	    join.conditions[0].right->expression_class != ExpressionClass::BOUND_COLUMN_REF) {
		return false;
	}

	auto &join_left = join.conditions[0].left->Cast<BoundColumnRefExpression>();
	auto &join_right = join.conditions[0].right->Cast<BoundColumnRefExpression>();
	auto &grp0 = agg.groups[0]->Cast<BoundColumnRefExpression>();
	auto &grp1 = agg.groups[1]->Cast<BoundColumnRefExpression>();
	auto &val0 = mul_expr.children[0]->Cast<BoundColumnRefExpression>();
	auto &val1 = mul_expr.children[1]->Cast<BoundColumnRefExpression>();

	// Key and group columns are read as int32, values as double.
	if (join_left.return_type != LogicalType::INTEGER || join_right.return_type != LogicalType::INTEGER ||
	    grp0.return_type != LogicalType::INTEGER || grp1.return_type != LogicalType::INTEGER ||
	    val0.return_type != LogicalType::DOUBLE || val1.return_type != LogicalType::DOUBLE) {
		return false;
	}

	if (CanAddNullGroups(*join.children[0]) || CanAddNullGroups(*join.children[1])) {
		return false;
	}

	// One group-by + one value from LHS, one group-by + one value from RHS
	SpalmSides sides;
	return LogicalSpalm::FindSides(
	    agg, join, join.children[0]->GetColumnBindings(), join.children[1]->GetColumnBindings(), sides
	);
}

void SpalmExtension::InjectSpalm(unique_ptr<LogicalOperator> &plan) {
	if (!plan) {
		return;
	}

	if (plan->type == LogicalOperatorType::LOGICAL_AGGREGATE_AND_GROUP_BY) {
		if (plan->children[0]->type == LogicalOperatorType::LOGICAL_COMPARISON_JOIN) {
			auto &agg_ref = plan->Cast<LogicalAggregate>();
			auto &join_ref = plan->children[0]->Cast<LogicalComparisonJoin>();
			if (!IsMatMulPattern(agg_ref, join_ref)) {
				// Not a matmul pattern, skip
				for (auto &child : plan->children) {
					InjectSpalm(child);
				}
				return;
			}
			auto agg_op = unique_ptr_cast<LogicalOperator, LogicalAggregate>(std::move(plan));
			auto comparison_join =
			    unique_ptr_cast<LogicalOperator, LogicalComparisonJoin>(std::move(agg_op->children[0]));
			// Extract children before moving comparison_join
			auto left_child = std::move(comparison_join->children[0]);
			auto right_child = std::move(comparison_join->children[1]);
			auto spalm = make_uniq<LogicalSpalm>(
			    std::move(agg_op), std::move(comparison_join), std::move(left_child), std::move(right_child)
			);
			plan = std::move(spalm);
		}
	}

	for (auto &child : plan->children) {
		InjectSpalm(child);
	}
}

static void OptimizePlan(OptimizerExtensionInput &input, unique_ptr<LogicalOperator> &plan) {
	SpalmExtension::InjectSpalm(plan);
}

// Rejects an unknown mode at SET, not in the next query.
static void CheckSpalmDuplicates(ClientContext &context, SetScope scope, Value &parameter) {
	ParseSpalmDuplicates(parameter.ToString());
}

// The join order optimizer of our DuckDB reads this one.
static void CheckMpfPlanner(ClientContext &context, SetScope scope, Value &parameter) {
	const auto planner = StringUtil::Lower(parameter.ToString());
	if (planner != "off" && planner != "cs" && planner != "cs_plus") {
		throw InvalidInputException("mpf_planner must be 'off', 'cs' or 'cs_plus' (got '%s')", parameter.ToString());
	}
}

static void CheckMpfForcePlan(ClientContext &context, SetScope scope, Value &parameter) {
	if (parameter.GetValue<int64_t>() < -1) {
		throw InvalidInputException("mpf_force_plan must be a plan number, or -1 for no plan (got %s)",
		                            parameter.ToString());
	}
}

static void LoadInternal(ExtensionLoader &loader) {
	try {
		printf("Loading Spalm extension...\n");

		auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
		OptimizerExtension optimizer_extension;
		optimizer_extension.optimize_function = OptimizePlan;
		OptimizerExtension::Register(config, std::move(optimizer_extension));

		// Configuration without relying on the working directory. Empty means
		// "not set": fall back to $SPALM_CONF / ./spalm.conf and its so_dir.
		config.AddExtensionOption(
		    "spalm_conf", "SPALM configuration file (default: $SPALM_CONF, then ./spalm.conf)", LogicalType::VARCHAR,
		    Value("")
		);
		config.AddExtensionOption(
		    "spalm_so_dir", "Directory of the SPALM native library (overrides so_dir in the configuration file)",
		    LogicalType::VARCHAR, Value("")
		);
		config.AddExtensionOption(
		    "spalm_duplicates",
		    "Entries of one input with the same row and join key: 'merge' sums them, 'error' fails the query, "
		    "'ignore' does not check (wrong results if there are any)",
		    LogicalType::VARCHAR, Value("merge"), CheckSpalmDuplicates
		);
		config.AddExtensionOption(
		    "mpf_planner",
		    "Planner for SUM(product) queries in the join order optimizer: 'off', 'cs' or 'cs_plus'. "
		    "For now, it reports whether a query qualifies and its candidate plans, and mpf_force_plan runs one of them",
		    LogicalType::VARCHAR, Value("off"), CheckMpfPlanner
		);
		config.AddExtensionOption(
		    "mpf_force_plan", "The candidate plan of mpf_planner to run, or -1 to report all of them and run none",
		    LogicalType::BIGINT, Value::BIGINT(-1), CheckMpfForcePlan
		);

		printf("Spalm extension loaded successfully\n");
	} catch (const std::exception &e) {
		printf("Error loading Spalm extension: %s\n", e.what());
	}
}

void SpalmExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string SpalmExtension::Name() {
	return "spalm";
}

std::string SpalmExtension::Version() const {
#ifdef EXT_VERSION_SPALM
	return EXT_VERSION_SPALM;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(spalm, loader) {
	duckdb::LoadInternal(loader);
}
}
