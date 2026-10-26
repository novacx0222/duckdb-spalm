// spalm_extension.hpp
#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/optimizer/optimizer_extension.hpp"
#include "duckdb/planner/logical_operator.hpp"

namespace duckdb {

class SpalmExtension : public Extension {
public:
	void Load(ExtensionLoader &loader) override;
	std::string Name() override;
	std::string Version() const override;

	static void InjectSpalm(unique_ptr<LogicalOperator> &plan);
};

} // namespace duckdb
