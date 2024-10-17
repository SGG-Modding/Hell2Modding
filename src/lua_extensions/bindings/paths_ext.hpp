#pragma once

namespace lua::paths_ext
{
	std::filesystem::path get_game_executable_folder();

	void bind(sol::table& state);
} // namespace lua::paths_ext
