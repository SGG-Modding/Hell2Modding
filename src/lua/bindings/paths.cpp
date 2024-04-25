#pragma once
#include "paths.hpp"

#include "lua/lua_manager.hpp"

namespace lua::paths
{
	// Lua API: Table
	// Name: paths
	// Table containing helpers for retrieving Hell2Modding related IO file/folder paths.

	// Lua API: Function
	// Table: paths
	// Name: config
	// Returns: string: Returns the Hell2Modding/config folder path
	// Used for data that must persist between sessions and that can be manipulated by the user.
	static std::string config()
	{
		std::scoped_lock l(big::g_lua_manager_mutex);

		return (char*)big::g_lua_manager->get_config_folder().get_path().u8string().c_str();
	}

	// Lua API: Function
	// Table: paths
	// Name: plugins_data
	// Returns: string: Returns the Hell2Modding/plugins_data folder path
	// Used for data that must persist between sessions but not be manipulated by the user.
	static std::string plugins_data()
	{
		std::scoped_lock l(big::g_lua_manager_mutex);

		return (char*)big::g_lua_manager->get_plugins_data_folder().get_path().u8string().c_str();
	}

	void bind(sol::table& state)
	{
		auto ns            = state.create_named("paths");
		ns["config"]       = config;
		ns["plugins_data"] = plugins_data;
	}
} // namespace lua::paths
