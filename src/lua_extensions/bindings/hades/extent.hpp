#pragma once

namespace lua::hades::extent
{
	extern bool g_draw_extents;
	extern bool g_draw_unit_extents;
    extern bool g_draw_projectile_extents;
    extern bool g_draw_obstacle_extents;

	extern unsigned int thing_color;
	extern unsigned int projectile_polygon_color;
	extern unsigned int projectile_ellipse_color;
	extern unsigned int unit_color;

	extern void set_internal_debugdraw_config();

	void bind(sol::state_view &state, sol::table &lua_ext);
}