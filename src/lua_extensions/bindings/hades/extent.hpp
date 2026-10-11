#pragma once

namespace lua::hades::extent
{
	extern bool g_draw_extents;

	extern void set_internal_debugdraw_config();

	void bind(sol::state_view &state, sol::table &lua_ext);
}