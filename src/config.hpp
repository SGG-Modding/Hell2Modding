#pragma once

#include "toml_v2/config_file.hpp"

namespace big::config
{
	inline std::unique_ptr<toml_v2::config_file> general_config = nullptr;

	inline void init_general()
	{
		general_config = std::make_unique<toml_v2::config_file>(
		    (char*)g_file_manager.get_project_file("config/Hell2Modding-Hell2Modding-General.cfg").get_path().u8string().c_str(),
		    true,
		    "Hell2Modding-Hell2Modding");
	}

	inline toml_v2::config_file& general()
	{
		return *general_config;
	}
} // namespace big::config
