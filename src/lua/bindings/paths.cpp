#pragma once
#include "paths.hpp"

#include "lua/lua_manager.hpp"

namespace lua::paths
{
	// Lua API: Table
	// Name: h2m.paths
	// Table containing helpers for retrieving Hell2Modding related IO file/folder paths.

	// Lua API: Function
	// Table: h2m.paths
	// Name: config
	// Returns: string: Returns the Hell2Modding/config folder path
	// Used for data that must persist between sessions and that can be manipulated by the user.
	static std::string config()
	{
		std::scoped_lock l(big::g_lua_manager_mutex);

		return (char*)big::g_lua_manager->get_config_folder().get_path().u8string().c_str();
	}

	// Lua API: Function
	// Table: h2m.paths
	// Name: plugins_data
	// Returns: string: Returns the Hell2Modding/plugins_data folder path
	// Used for data that must persist between sessions but not be manipulated by the user.
	static std::string plugins_data()
	{
		std::scoped_lock l(big::g_lua_manager_mutex);

		return (char*)big::g_lua_manager->get_plugins_data_folder().get_path().u8string().c_str();
	}

	static std::filesystem::path get_game_executable_folder()
	{
		char module_file_path[MAX_PATH];
		const auto path_size              = GetModuleFileNameA(nullptr, module_file_path, MAX_PATH);
		std::filesystem::path root_folder = std::string(module_file_path, path_size);
		root_folder                       = root_folder.parent_path();

		return root_folder;
	}

	// Lua API: Function
	// Table: h2m.paths
	// Name: Content
	// Returns: string: Returns the GameFolder/Content folder path
	static std::string hades_Content()
	{
		auto folder  = get_game_executable_folder().parent_path();
		folder      /= "Content";

		return (char*)folder.u8string().c_str();
	}

	// Lua API: Function
	// Table: h2m.paths
	// Name: Ship
	// Returns: string: Returns the GameFolder/Ship folder path
	static std::string hades_Ship()
	{
		return (char*)get_game_executable_folder().u8string().c_str();
	}

	void bind(sol::table& state)
	{
		auto ns            = state.create_named("paths");
		ns["config"]       = config;
		ns["plugins_data"] = plugins_data;
		ns["Content"]      = hades_Content;
		ns["Ship"]         = hades_Ship;
	}
} // namespace lua::paths
