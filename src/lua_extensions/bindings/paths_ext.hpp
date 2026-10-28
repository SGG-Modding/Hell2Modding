#pragma once

#include <filesystem>
#include <string>

namespace lua::paths_ext
{
	std::filesystem::path get_game_executable_folder();
	std::filesystem::path get_game_install_folder();
	bool is_microsoft_store_install(const std::filesystem::path& install_folder);
	std::string hades_Content();

	void bind(sol::table& state);
} // namespace lua::paths_ext
