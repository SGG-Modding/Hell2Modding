#pragma once

#include "nlohmann/json.hpp"
#include "semver/semver.hpp"

namespace ts::v1
{
	struct dependency
	{
		std::string team_name{};
		std::string name{};
		semver::version version{};
	};

	struct manifest
	{
		std::string name{};

		std::string version_number{};
		semver::version version{};

		std::string website_url{};

		std::string description{};

		std::vector<std::string> dependencies{};
		std::vector<std::string> dependencies_no_version_number{};

		NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(manifest, name, version_number, website_url, description, dependencies)
	};
} // namespace ts::v1
