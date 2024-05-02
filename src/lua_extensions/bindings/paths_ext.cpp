#pragma once
#include "paths_ext.hpp"

#include "lua/lua_manager.hpp"

namespace lua::paths_ext
{
	static std::filesystem::path get_game_executable_folder()
	{
		char module_file_path[MAX_PATH];
		const auto path_size              = GetModuleFileNameA(nullptr, module_file_path, MAX_PATH);
		std::filesystem::path root_folder = std::string(module_file_path, path_size);
		root_folder                       = root_folder.parent_path();

		return root_folder;
	}

	// Lua API: Function
	// Table: paths
	// Name: Content
	// Returns: string: Returns the GameFolder/Content folder path
	static std::string hades_Content()
	{
		auto folder  = get_game_executable_folder().parent_path();
		folder      /= "Content";

		return (char*)folder.u8string().c_str();
	}

	// Lua API: Function
	// Table: paths
	// Name: Ship
	// Returns: string: Returns the GameFolder/Ship folder path
	static std::string hades_Ship()
	{
		return (char*)get_game_executable_folder().u8string().c_str();
	}

	void bind(sol::table& state)
	{
		auto ns = state["paths"].get_or_create<sol::table>();

		ns["Content"] = hades_Content;
		ns["Ship"]    = hades_Ship;
	}
} // namespace lua::paths_ext
