#pragma once

#include <filesystem>
#include <string>

namespace lua::paths_ext
{
	std::filesystem::path get_game_executable_folder();
	std::string hades_Content();

	void bind(sol::table& state);
} // namespace lua::paths_ext
