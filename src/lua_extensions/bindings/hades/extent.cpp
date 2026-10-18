#include "tethers.hpp"

#include "hades_ida.hpp"
#include "data.hpp"

#include <EASTL/string.h>
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
    constexpr std::string INSTANT_ANIMATION = "FadingCircle";
    constexpr unsigned int CIRCLE_COLOR = 0X770000FF; // red, 0.5 opacity
    
    constexpr int MAX_CUSTOM_ELLIPSES = 10;

    unsigned int thing_color = 0xFF00FF00; // green
    unsigned int projectile_polygon_color = 0xFF0000FF; // red
    unsigned int projectile_ellipse_color = 0xFF00EEFF; //yellow
    unsigned int unit_color = 0xFFFFFF00; // cyan

    auto create_aniamtion_pattern = "48 89 5C 24 08 57 48 83 EC 40 8B C2 C6 44 24 28 "
                                    "00 66 49 0F 6E C0 48 8B F9 4C 8B C1 66 48 0F 7E";
    gmAddress create_animation_addr;

    auto set_scale_vector_pattern = "48 83 EC 28 48 8B 51 08 F3 0F 11 49 54 F3 0F 11 "
                                    "51 58 48 85 D2 74 3A 4C 8B 82 38 06 00 00 48 8B";
    gmAddress set_scale_vector_addr;

    struct EllipseDrawParams{
		Vectormath::Vector2 center;
		float radius;
		float scaleX;
		float scaleY;
		unsigned int color;
	};

    std::array<std::mutex, MAX_CUSTOM_ELLIPSES> ellipse_queue_mutex;
    std::array<std::optional<EllipseDrawParams>, MAX_CUSTOM_ELLIPSES> ellipse_params_array;
    int active_ellipse_param_index = 0;

    using Drawmanager__DrawLine = void(*)(
        Vectormath::Vector2 start,
        Vectormath::Vector2 end,
        float width,
        unsigned int color,
        bool screenSpace,
        float parallax,
        float zShift
    );

    using Thing__DrawExtents = void(*)(
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

    using Thing__ToString = eastl::string *(*)(
        sgg::Thing_partial *thing
    );

    // sgg::Animation* sgg::AnimationManager::Create(sgg::AnimationManager *this, sgg::HashGuid name, Vectormath::Vector2 location)
    using Animation__Create = uint8_t*(*)(
        void* animation_manager, 
        sgg::HashGuid name,
        Vectormath::Vector2 location
    );

    // void sgg::Animation::SetColor(sgg::Animation *this, sgg::Color color, sgg::HashGuid applyToName, bool ignoreOpacity, bool applyToChildren)
    using Animation_SetColor = void(*)(
        uint8_t* animation,
        uint32_t color,
        sgg::HashGuid applyToName,
        bool ignoreOpacity,
        bool applyToChildren
    );

    // void sgg::Animation::SetRadius(sgg::Animation *this, float radius)
    using Animation__SetRadius = void(*)(
        uint8_t* animation,
        float radius
    );

    // void sgg::BodyComponent::SetScaleVector(sgg::BodyComponent *this, float scaleX, float scaleY)
    using BodyComponent__SetScaleVector = void(*)(
        uint8_t* body_component,
        float scaleX,
        float scaleY
    );

    bool g_draw_extents = false;
    bool g_draw_unit_extents = true;
    bool g_draw_projectile_extents = true;
    bool g_draw_obstacle_extents = true;

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

    static void polygon_draw_extent(sgg::Polygon polygon, unsigned int color, bool screenSpace)
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
        unsigned int color = thing_color;
        bool draw_polygon = false;
        if (type_check(type, sgg::Rtti__Type::Projectile_t) && g_draw_projectile_extents) {
            big::hades2_symbol_to_address["sgg::Projectile::DrawExtents"].as<Thing__DrawExtents>()(thing);
            color = projectile_polygon_color;
            draw_polygon = true;
        } else if (type_check(type, sgg::Rtti__Type::Unit_t) && g_draw_unit_extents) {
            big::hades2_symbol_to_address["sgg::Unit::DrawExtents"].as<Thing__DrawExtents>()(thing);
            color = unit_color;
            draw_polygon = true;
        } else if (g_draw_obstacle_extents){
            big::hades2_symbol_to_address["sgg::Thing::DrawExtents"].as<Thing__DrawExtents>()(thing);
            draw_polygon = stops_projectile((sgg::Obstacle_partial *)thing) || stops_unit((sgg::Obstacle_partial *)thing);
        }

        if (polygon.has_value() && draw_polygon) {
            polygon_draw_extent(*polygon, color, thing->mUseScreenLocation);
        }

        // draw every available custom ellipse once every player unit draw,
        if (type_check(type, sgg::Rtti__Type::PlayerUnit) && g_draw_projectile_extents) {
            for (int i = 0; i < ellipse_params_array.size(); i++) {
                EllipseDrawParams params;
                bool has_ellipse = false;

                {
                    std::lock_guard<std::mutex> lock(ellipse_queue_mutex[i]);

                    if (ellipse_params_array[i].has_value()) {
                        params = ellipse_params_array[i].value();
                        ellipse_params_array[i].reset();
                        has_ellipse = true;
                    }
                }

                if (has_ellipse) {
                    big::hades2_symbol_to_address["sgg::DrawManager::DrawEllipse"].as<DrawManager__DrawEllipse>()(params.center, params.radius, params.scaleX,
                        params.scaleY, 64.0f, params.color, false);
                }
            }
        }
    }

    static void hook_Thing_SubmitDraw(sgg::Thing_partial *thing, float w)
    {
        big::g_hooking->get_original<hook_Thing_SubmitDraw>()(thing, w);
        if (g_draw_extents) {
            thing_draw_extent(thing);
        }
    }

    static char hook_Projectile_UpdateDetonation(sgg::Projectile *projectile, float elapsedSeconds)
    {
        auto result = big::g_hooking->get_original<hook_Projectile_UpdateDetonation>()(projectile, elapsedSeconds);

        if (g_draw_extents) {
            float blast_radius = projectile->mBlastRadius;
            float blast_speed = projectile->mBlastSpeed;
            float blast_acceleration = projectile->mData.mDef.mBlastSpeedAcceleration;
            float blast_duration = projectile->mData.mDef.mBlastDuration;
            bool multi_detonate = projectile->mData.mDef.mMultiDetonate;
            sgg::ProjectileDataDef__ProjectileType projectile_type = projectile->mData.mDef.mType;
            float scaleX = projectile->mData.mDef.mDamageRadiusScaleX;
            float scaleY = projectile->mData.mDef.mDamageRadiusScaleY;
            Vectormath::Vector2 location;
            big::hades2_symbol_to_address["sgg::Thing::GetLocation"].as<Thing__GetLocation>()(projectile, &location);

            bool is_instant_detonation = blast_speed <= 0.0 && blast_acceleration == 0 && blast_duration <= 0 && projectile_type != sgg::BEAM;

            if ( blast_radius > 0 && !(is_instant_detonation && multi_detonate) ) {
                {
                    std::lock_guard<std::mutex> lock(ellipse_queue_mutex[active_ellipse_param_index]);
                    ellipse_params_array[active_ellipse_param_index].emplace(location, blast_radius, scaleX, scaleY, projectile_ellipse_color);
                    active_ellipse_param_index = (active_ellipse_param_index + 1) % ellipse_params_array.size();
                }
            }

            if ( blast_radius > 0 && is_instant_detonation && !multi_detonate && create_animation_addr ) {
                sgg::HashGuid name{.mId = lua::hades::data::get_hash_guid_from_string(INSTANT_ANIMATION)};
                auto anim_manager = big::hades2_symbol_to_address["sgg::AnimationManager::MANAGERS"].as<void**>()[0];
                auto anim = create_animation_addr.as<Animation__Create>()(anim_manager, name, location);
                big::hades2_symbol_to_address["sgg::Animation::SetColor"].as<Animation_SetColor>()(anim, CIRCLE_COLOR, name, 0, 1);
                big::hades2_symbol_to_address["sgg::Animation::SetRadius"].as<Animation__SetRadius>()(anim, blast_radius);
                auto own_body = anim + 0xD0; // Animation.mOwnBody offset
                if (set_scale_vector_addr) {
                    set_scale_vector_addr.as<BodyComponent__SetScaleVector>()(own_body, scaleX, scaleY);
                }
            }

            auto thing_String = big::hades2_symbol_to_address["sgg::Thing::ToString"].as<Thing__ToString>()(projectile);
            LOG(DEBUG) << thing_String->c_str();
            LOG(DEBUG) << "Location: " << location.mX << ", " << location.mY;
            LOG(DEBUG) << "Radius: " << blast_radius;
            LOG(DEBUG) << "Instant: " << is_instant_detonation;
            LOG(DEBUG) << "Multi: " << multi_detonate;
        }

        return result;
    }

    void set_internal_debugdraw_config()
    {
        auto DrawExtentsForPassable = big::hades2_symbol_to_address["sgg::ConfigOptions::DrawExtentsForPassable"].as<bool *>();
        auto DrawExtents = big::hades2_symbol_to_address["sgg::ConfigOptions::DrawExtents"].as<bool *>();
        *DrawExtentsForPassable = g_draw_extents;
        *DrawExtents = g_draw_extents;
    }

    // Lua API: Function
	// Table: extent
	// Name: toggle
    // Returns: boolean: Whether extents are being drawn or not after the toggle is triggered.
    static bool toggle_extents()
    {
        g_draw_extents = !g_draw_extents;
        set_internal_debugdraw_config();
        return g_draw_extents;
    }

    // Lua API: Function
	// Table: extent
	// Name: are_drawn
    // Returns: boolean: Whether extents are being drawn or not.
    static bool are_extents_drawn()
    {
        return g_draw_extents;
    }

    void bind(sol::state_view &state, sol::table &lua_ext)
	{
        // Register debug draw hooks
        auto submit_draw_addr = big::hades2_symbol_to_address["sgg::Thing::SubmitDraw"];
        big::hooking::detour_hook_helper::add<hook_Thing_SubmitDraw>("hook_Thing_SubmitDraw", submit_draw_addr);

        auto projectile_update_blast_radius_addr = big::hades2_symbol_to_address["sgg::Projectile::UpdateDetonation"];
        big::hooking::detour_hook_helper::add<hook_Projectile_UpdateDetonation>("hook_Projectile_UpdateDetonation", projectile_update_blast_radius_addr);

        create_animation_addr = gmAddress::scan(create_aniamtion_pattern, "sgg::Animation *sgg::AnimationManager::Create(sgg::AnimationManager *this, sgg::HashGuid name, Vectormath::Vector2 location)");
        if (!create_animation_addr) {
            LOG(WARNING) << "Unable to find sgg::AnimationManager::Create address. Instant blast extents will not be drawn.\n";
        }
        set_scale_vector_addr = gmAddress::scan(set_scale_vector_pattern, "void sgg::BodyComponent::SetScaleVector(sgg::BodyComponent *this, float scaleX, float scaleY)");
        if (!set_scale_vector_addr) {
            LOG(WARNING) << "Unable to find sgg::BodyComponent::SetScaleVector address. Instant blast extents will be scaled improperly.\n";
        }
        // Register Lua API
		auto ns = lua_ext.create_named("extent");
        ns.set_function("toggle", toggle_extents);
        ns.set_function("are_drawn", are_extents_drawn);
	}
}