#include "tethers.hpp"

#include "hades_ida.hpp"

#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <lua/lua_manager.hpp>
#include <lua_extensions/lua_manager_extension.hpp>
#include <lua_extensions/lua_module_ext.hpp>
#include <memory/gm_address.hpp>

namespace lua::hades::extent
{
    constexpr float LINE_THICKNESS = 3.0;
    constexpr float Z_SHIFT = 0;
    constexpr float PARALLAX = 1;
    constexpr unsigned int DEFAULT_LINE_COLOR = 0XFFFFFFFF;

    using Drawmanager__DrawLine = void(*)(
        Vectormath::Vector2 start,
        Vectormath::Vector2 end,
        float width,
        unsigned int color,
        bool screenSpace,
        float parallax,
        float zShift
    );

    using Projectile__DrawExtents = void(*)(
        sgg::Thing_partial *thing
    );

    using Thing__GetLocation = Vectormath::Vector2 *(*)(
        sgg::Thing_partial *thing,
        Vectormath::Vector2 *result
    );

    using DrawManager__DrawEllipse = void(*)(
        Vectormath::Vector2 center,
        float radius,
        float scaleX,
        float scaleY,
        float segments,
        unsigned int color,
        bool screenSpace
    );

    bool g_draw_extents = false;

    static bool type_check(sgg::Rtti__Type type, sgg::Rtti__Type type_enum){
        return (type & type_enum) == type_enum;
    }

    static uint8_t stops_unit(sgg::Obstacle_partial *obstacle)
    {
        return (obstacle->mGridSets & 8) != 0;
    }

    static uint8_t stops_projectile(sgg::Obstacle_partial *obstacle)
    {
        return (obstacle->mGridSets & 4) != 0;
    }

    static void draw_polygon_extent(sgg::Polygon polygon, unsigned int color, bool screenSpace)
    {
        auto count = polygon.mPoints.end() - polygon.mPoints.begin();
        if (count > 1) {
            for (auto i = 0; i < count; i++){
                Vectormath::Vector2 first = polygon.mPoints[i];
                Vectormath::Vector2 second = polygon.mPoints[(i + 1) % count];
                big::hades2_symbol_to_address["sgg::DrawManager::DrawLine"].as<Drawmanager__DrawLine>()(first, second, LINE_THICKNESS, color, screenSpace, PARALLAX, Z_SHIFT);
            }
        }
    }

    static void thing_draw_extent(sgg::Thing_partial *thing)
    {
        eastl::optional<sgg::Polygon> polygon = thing->mGeometry;
        sgg::Rtti__Type type = thing->mType;
        unsigned int color = 0xff00ff00;
        bool draw_polygon = true;
        if (type_check(type, sgg::Rtti__Type::Projectile_t)) {
            big::hades2_symbol_to_address["sgg::Projectile::DrawExtents"].as<Projectile__DrawExtents>()(thing);
            color = 0xff0000ff;
        } else if (type_check(type, sgg::Rtti__Type::Unit_t)) {
            big::hades2_symbol_to_address["sgg::Unit::DrawExtents"].as<Projectile__DrawExtents>()(thing);
            color = 0xffffff00;
        } else{
            big::hades2_symbol_to_address["sgg::Thing::DrawExtents"].as<Projectile__DrawExtents>()(thing);
            draw_polygon = stops_projectile((sgg::Obstacle_partial *)thing) || stops_unit((sgg::Obstacle_partial *)thing);
        }

        if (polygon.has_value() && draw_polygon) {
            draw_polygon_extent(*polygon, color, thing->mUseScreenLocation);
        }
    }

    static void hook_Thing_SubmitDraw(sgg::Thing_partial *thing, float w)
    {
        big::g_hooking->get_original<hook_Thing_SubmitDraw>()(thing, w);
        if (g_draw_extents) {
            thing_draw_extent(thing);
        }
    }

    void set_internal_debugdraw_config()
    {
        auto DrawExtentsForPassable = big::hades2_symbol_to_address["sgg::ConfigOptions::DrawExtentsForPassable"].as<bool *>();
        auto DrawExtents = big::hades2_symbol_to_address["sgg::ConfigOptions::DrawExtents"].as<bool *>();
        auto DebugDraw = big::hades2_symbol_to_address["sgg::ConfigOptions::DebugDraw"].as<bool *>();
        auto DebugDrawBounds = big::hades2_symbol_to_address["sgg::ConfigOptions::DebugDrawBounds"].as<bool *>();
        *DrawExtentsForPassable = g_draw_extents;
        *DrawExtents = g_draw_extents;
        *DebugDraw = g_draw_extents;
        *DebugDrawBounds = g_draw_extents;
    }

    // Lua API: Function
	// Table: extent
	// Name: drawline
	// Param: startX: number: X coordinate of the first point.
	// Param: startY: number: Y coordinate of the first point.
    // Param: endX: number: X coordinate of the second point.
    // Param: startY: number: Y coordinate of the second point.
    // Param: width: number: Line width. Default 3.0.
    // Param: raw_color: integer: Line color, ABGR format. Default 0xFFFFFFFF.
    // Param: screenSpace: boolean: Whether coordinate are for screen space. Default false.
    // Param: parallax: number: Unknown. Default 1.0.
    // Param: zShift: number: Z offset for the line. Default 0.
	// Draws a line between two given points.
    static void draw_line(float startX, float startY, float endX, float endY, sol::optional<float> width, sol::optional<unsigned int> raw_color, 
        sol::optional<bool> screenSpace, sol::optional<float> parallax, sol::optional<float> zShift)
    {
        Vectormath::Vector2 start = { startX, startY };
        Vectormath::Vector2 end = { endX, endY };

        big::hades2_symbol_to_address["sgg::DrawManager::DrawLine"].as<Drawmanager__DrawLine>()(start, end, width.value_or(LINE_THICKNESS), raw_color.value_or(DEFAULT_LINE_COLOR), 
            screenSpace.value_or(false), parallax.value_or(PARALLAX), zShift.value_or(Z_SHIFT));
    }

    // Lua API: Function
	// Table: extent
	// Name: toggle
    // Returns: boolean: Whether extents are being drawn or not.
    static bool toggle_extents()
    {
        g_draw_extents = !g_draw_extents;
        set_internal_debugdraw_config();
        return g_draw_extents;
    }

    void bind(sol::state_view &state, sol::table &lua_ext)
	{
        // Register debug draw hooks
        auto submit_draw_addr = big::hades2_symbol_to_address["sgg::Thing::SubmitDraw"];
        big::hooking::detour_hook_helper::add<hook_Thing_SubmitDraw>("hook_Thing_SubmitDraw", submit_draw_addr);

        // Register Lua API
		auto ns = lua_ext.create_named("extent");
		ns.set_function("drawline", draw_line);
        ns.set_function("toggle", toggle_extents);
	}
}