#pragma once

namespace lua::hades::inputs
{
	extern bool enable_vanilla_debug_keybinds;
	extern bool let_game_input_go_through_gui_layer;

	struct keybind_callback
	{
		std::string name;
		sol::coroutine cb;
	};

	extern std::map<std::string, std::vector<keybind_callback>> vanilla_key_callbacks;

	void bind(sol::state_view &state, sol::table &lua_ext);
} // namespace lua::hades::inputs
