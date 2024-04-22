#pragma once

#include <cxxopts.hpp>
#include <shellapi.h>

namespace big::paths
{
	extern std::filesystem::path get_project_root_folder();

	extern void init_dump_file_path();
	extern const std::filesystem::path& remove_and_get_dump_file_path();
} // namespace big::paths
