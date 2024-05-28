#pragma once

namespace lua::hades::inputs
{
	extern bool enable_vanilla_debug_keybinds;
	extern bool let_game_input_go_through_gui_layer;

	extern std::unordered_map<std::string, sol::coroutine> key_callbacks;

	void bind(sol::state_view &state, sol::table &lua_ext);
} // namespace lua::hades::inputs
