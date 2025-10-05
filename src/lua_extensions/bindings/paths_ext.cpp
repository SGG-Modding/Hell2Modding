#pragma once
#include "paths_ext.hpp"

#include "lua/lua_manager.hpp"

namespace lua::paths_ext
{
	std::filesystem::path get_game_executable_folder()
	{
		constexpr size_t max_path = MAX_PATH * 2;
		wchar_t buffer[max_path];
		DWORD length = GetModuleFileNameW(nullptr, buffer, max_path);
		if (length == 0 || length == max_path)
		{
			return {};
		}

		std::filesystem::path exe_path(buffer);
		return exe_path.parent_path();
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
