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

    static bool g_draw_extents = false;

    static void draw_line(float startX, float startY, float endX, float endY, float width, unsigned int raw_color, bool screenSpace, float parallax, float zShift)
    {
        Vectormath::Vector2 start = { startX, startY };
        Vectormath::Vector2 end = { endX, endY };

        sgg::Color color;
        color.v = raw_color;

        big::hades2_symbol_to_address["sgg::DrawManager::DrawLine"].as<Drawmanager__DrawLine>()(start, end, width, raw_color, screenSpace, parallax, zShift);
    }

    static bool type_check(sgg::Rtti__Type a, sgg::Rtti__Type b){
        return (a & b) == b;
    }

    static uint8_t stops_unit(sgg::Obstacle_partial *obstacle)
    {
        return (obstacle->mGridSets & 8) != 0;
    }

    static uint8_t stops_projectile(sgg::Obstacle_partial *obstacle)
    {
        return (obstacle->mGridSets & 4) != 0;
    }

    static void hook_Thing_SubmitDraw(sgg::Thing_partial *thing, float w)
    {
        big::g_hooking->get_original<hook_Thing_SubmitDraw>()(thing, w);
        auto DrawExtentsForPassable = big::hades2_symbol_to_address["sgg::ConfigOptions::DrawExtentsForPassable"].as<bool *>();
        if (*DrawExtentsForPassable) {
            auto DrawExtents = big::hades2_symbol_to_address["sgg::ConfigOptions::DrawExtents"].as<bool *>();
            auto DebugDraw = big::hades2_symbol_to_address["sgg::ConfigOptions::DebugDraw"].as<bool *>();
            auto DebugDrawBounds = big::hades2_symbol_to_address["sgg::ConfigOptions::DebugDrawBounds"].as<bool *>();
            *DrawExtents = true;
            *DebugDraw = true;
            *DebugDrawBounds = true;

            eastl::optional<sgg::Polygon> polygon_optional = thing->mGeometry;
            sgg::Polygon polygon;
            if (polygon_optional.has_value()) {
                polygon = *polygon_optional;
            }
            sgg::Rtti__Type type = thing->mType;
            unsigned int color = 0xff00ff00;
            bool draw_polygon = true;
            if (type_check(type, sgg::Rtti__Type::Projectile_t)) {
                big::hades2_symbol_to_address["sgg::Projectile::DrawExtents"].as<Projectile__DrawExtents>()(thing);
                color = 0xff0000ff;
            } else if (type_check(type, sgg::Rtti__Type::Unit_t)) {
                big::hades2_symbol_to_address["sgg::Thing::DrawExtents"].as<Projectile__DrawExtents>()(thing);
                color = 0xffffff00;
            } else{
                big::hades2_symbol_to_address["sgg::Thing::DrawExtents"].as<Projectile__DrawExtents>()(thing);
                draw_polygon = stops_projectile((sgg::Obstacle_partial *)thing) || stops_unit((sgg::Obstacle_partial *)thing);
            }

            if (polygon_optional.has_value() && draw_polygon) {
                auto count = polygon.mPoints.end() - polygon.mPoints.begin();
                if (count > 1) {
                    for (auto i = 0; i < count; i++){
                        Vectormath::Vector2 first = polygon.mPoints[i];
                        Vectormath::Vector2 second = polygon.mPoints[(i + 1) % count];
                        big::hades2_symbol_to_address["sgg::DrawManager::DrawLine"].as<Drawmanager__DrawLine>()(first, second, 3.0, color, false, 1, 0);
                    }
                }
            }
        }
    }

    void bind(sol::state_view &state, sol::table &lua_ext)
	{
		// Register Lua API

        auto submit_draw_addr = big::hades2_symbol_to_address["sgg::Thing::SubmitDraw"];

        big::hooking::detour_hook_helper::add<hook_Thing_SubmitDraw>("hook_Thing_SubmitDraw", submit_draw_addr);

		auto ns = lua_ext.create_named("extent");
		ns.set_function("drawline", draw_line);
	}
}