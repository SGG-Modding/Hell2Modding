#include "tethers.hpp"

#include "hades_ida.hpp"

#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <lua/lua_manager.hpp>
#include <lua_extensions/lua_manager_extension.hpp>
#include <lua_extensions/lua_module_ext.hpp>
#include <memory/gm_address.hpp>

namespace lua::hades::tethers
{
	// H2 sgg::Thing partial struct for field access via raw offsets.
	struct Thing_H2
	{
		char pad_0[0x48];
		int mId;                        // 0x48
		char pad_1[0x08];               // 0x4C..0x53
		float mZLocation;               // 0x54
		char pad_2[0x18];               // 0x58..0x6F
		Vectormath::Vector2 mLocation;  // 0x70
		char pad_3[0x40];               // 0x78..0xB7
		void *pPhysics;                 // 0xB8
	};

	static_assert(offsetof(Thing_H2, mId) == 0x48);
	static_assert(offsetof(Thing_H2, mZLocation) == 0x54);
	static_assert(offsetof(Thing_H2, mLocation) == 0x70);
	static_assert(offsetof(Thing_H2, pPhysics) == 0xB8);

	// H2 PhysicsComponent partial struct for velocity and gravity access.
	struct PhysicsComponent_H2_Velocity
	{
		char pad_0[0x35];
		bool mIgnoreGravity;            // 0x35
		char pad_1[0x06];               // 0x36..0x3B
		float mGravity;                 // 0x3C
		char pad_2[0x08];               // 0x40..0x47
		Vectormath::Vector2 mVelocity;  // 0x48
	};

	static_assert(offsetof(PhysicsComponent_H2_Velocity, mIgnoreGravity) == 0x35);
	static_assert(offsetof(PhysicsComponent_H2_Velocity, mGravity) == 0x3C);
	static_assert(offsetof(PhysicsComponent_H2_Velocity, mVelocity) == 0x48);

	static void **g_world_ptr = nullptr;

	using GetActiveThingFn = Thing_H2 *(*)(void *world, int id);
	static GetActiveThingFn g_get_active_thing = nullptr;

	using ShiftLocationFn = void (*)(void *thing, Vectormath::Vector2 delta);
	static ShiftLocationFn g_shift_location = nullptr;

	// Helper -------------------------------

	static Thing_H2 *get_thing(int id)
	{
		if (!g_world_ptr || !*g_world_ptr || !g_get_active_thing)
		{
			return nullptr;
		}

		return g_get_active_thing(*g_world_ptr, id);
	}

	// H2's PhysicsComponent has tether fields in the PDB but no engine code
	// accesses them. We store tether data in a sidecar map keyed by Thing ID.

	struct TetherLink
	{
		int target_id        = 0;
		float distance       = 0.0f;
		float elasticity     = 0.0f;
		float retract_speed  = 0.0f;
		float track_z_ratio  = 0.0f;
		float rest_angle     = 0.0f;
	};

	struct TetherData
	{
		std::vector<TetherLink> links;
		std::vector<int> tethered_from;
	};

	static std::recursive_mutex g_tether_mutex;
	static std::unordered_map<int, TetherData> g_tether_data;

	static TetherData *get_tether_data(int thing_id)
	{
		auto it = g_tether_data.find(thing_id);
		if (it != g_tether_data.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	// Global tether update ------------------------

	static void update_all_tethers(float dt)
	{
		for (auto &[id, data] : g_tether_data)
		{
			if (data.links.empty())
			{
				continue;
			}

			auto *thing = get_thing(id);
			if (!thing)
			{
				continue;
			}

			for (auto &link : data.links)
			{
				auto *target = get_thing(link.target_id);
				if (!target)
				{
					continue;
				}

				float dx      = target->mLocation.mX - thing->mLocation.mX;
				float dy      = target->mLocation.mY - thing->mLocation.mY;
				float dist_sq = dx * dx + dy * dy;
				float dist    = (dist_sq > 0.0f) ? sqrtf(dist_sq) : 0.0f;

				if (!std::isfinite(dist_sq))
				{
					continue;
				}

				if (link.elasticity > 0.0f)
				{
					// Elastic tether: critically damped spring
					if (!thing->pPhysics)
					{
						continue;
					}

					auto *pc = static_cast<PhysicsComponent_H2_Velocity *>(thing->pPhysics);

					if (dist < 0.001f)
					{
						float angle = (float)(id % 360) * 0.0174533f;
						pc->mVelocity.mX = cosf(angle) * link.distance * 2.0f;
						pc->mVelocity.mY = sinf(angle) * link.distance * 2.0f;
						continue;
					}

					float ideal_x = target->mLocation.mX + cosf(link.rest_angle) * link.distance;
					float ideal_y = target->mLocation.mY + sinf(link.rest_angle) * link.distance;

					float diff_x = ideal_x - thing->mLocation.mX;
					float diff_y = ideal_y - thing->mLocation.mY;

					float omega = sqrtf(link.elasticity) * 0.1f;
					float damping_coeff = 2.0f * omega;

					float ax = omega * omega * diff_x - damping_coeff * pc->mVelocity.mX;
					float ay = omega * omega * diff_y - damping_coeff * pc->mVelocity.mY;

					pc->mVelocity.mX += ax * dt;
					pc->mVelocity.mY += ay * dt;
				}
				else
				{
					// Non-elastic (chain) tether
					if (link.retract_speed <= 0.0f && link.track_z_ratio <= 0.0f)
					{
						// Passive anchor: hard clamp only
						if (dist > link.distance && g_shift_location)
						{
							float nx = dx / dist;
							float ny = dy / dist;
							float overshoot = dist - link.distance;
							Vectormath::Vector2 delta = {nx * overshoot, ny * overshoot};
							if (std::isfinite(delta.mX) && std::isfinite(delta.mY))
							{
								g_shift_location(thing, delta);
							}
						}
						continue;
					}

					float nx = dx / dist;
					float ny = dy / dist;

					if (dist > link.distance && g_shift_location)
					{
						// Hard clamp at distance + cascade to chain target
						float overshoot = dist - link.distance;
						if (std::isfinite(overshoot))
						{
							Vectormath::Vector2 delta = {nx * overshoot, ny * overshoot};
							g_shift_location(thing, delta);

							// Cascade scales down with overshoot to prevent amplification
							float ratio = link.distance / dist;
							float cascade_frac = 0.5f + 0.45f * ratio;
							Vectormath::Vector2 cascade = {-nx * overshoot * cascade_frac,
							                               -ny * overshoot * cascade_frac};
							g_shift_location(target, cascade);
						}
					}
					else if (dist > link.distance * 0.65f && g_shift_location)
					{
						// Gradual retraction when beyond resting threshold
						float pull = std::min(dist, link.retract_speed * dt);
						Vectormath::Vector2 delta = {nx * pull, ny * pull};
						if (std::isfinite(delta.mX) && std::isfinite(delta.mY))
						{
							g_shift_location(thing, delta);
						}
					}
				}

				// Z tracking handled per-source below, not per-link
			}

			// Z tracking: each segment tracks predecessor's Z (toward head)
			if (!data.tethered_from.empty() && !data.links.empty())
			{
				float my_track_z = 0.0f;
				for (auto &link : data.links)
				{
					if (link.track_z_ratio > my_track_z)
						my_track_z = link.track_z_ratio;
				}

				if (my_track_z > 0.0f)
				{
					for (int from_id : data.tethered_from)
					{
						auto *from_thing = get_thing(from_id);
						if (!from_thing)
							continue;

						// Track toward predecessor Z
						float dz = from_thing->mZLocation - thing->mZLocation;
						float abs_dz = (dz >= 0.0f) ? dz : -dz;
						float speed = abs_dz * my_track_z * 30.0f;
						float step = speed * dt;
						if (abs_dz > 0.001f)
						{
							float move = (abs_dz < step) ? dz : (dz > 0 ? step : -step);
							thing->mZLocation += move;
						}
						// Reduce gravity on neck segments
						if (thing->pPhysics)
						{
							auto *pc = static_cast<PhysicsComponent_H2_Velocity *>(thing->pPhysics);
							pc->mGravity = 200.0f;
						}
						break;
					}
				}
			}

			// Predecessor pull: spread stacked segments by following the head-side neighbor
			if (!data.tethered_from.empty() && g_shift_location)
			{
				for (int from_id : data.tethered_from)
				{
					auto *from_thing = get_thing(from_id);
					if (!from_thing)
						continue;

					float fdx = from_thing->mLocation.mX - thing->mLocation.mX;
					float fdy = from_thing->mLocation.mY - thing->mLocation.mY;
					float fdist = sqrtf(fdx * fdx + fdy * fdy);

					if (fdist > 1.0f && std::isfinite(fdist))
					{
						auto *from_data = get_tether_data(from_id);
						float link_dist = 73.0f;
						float link_retract = 500.0f;
						if (from_data)
						{
							for (auto &fl : from_data->links)
							{
								if (fl.target_id == id)
								{
									link_dist = fl.distance;
									link_retract = fl.retract_speed;
									break;
								}
							}
						}

						if (fdist > link_dist * 0.65f && link_retract > 0.0f)
						{
							float fnx = fdx / fdist;
							float fny = fdy / fdist;
							float pull = std::min(fdist, link_retract * dt);
							Vectormath::Vector2 delta = {fnx * pull, fny * pull};
							if (std::isfinite(delta.mX) && std::isfinite(delta.mY))
							{
								g_shift_location(thing, delta);
							}
						}
					}
					break;
				}
			}

			// Chain straightening: pull toward midpoint of two neighbors
			if (!data.tethered_from.empty() && !data.links.empty() && g_shift_location)
			{
				auto *from_thing = get_thing(data.tethered_from[0]);
				Thing_H2 *chain_target = nullptr;
				for (auto &link : data.links)
				{
					if (link.retract_speed > 0.0f || link.track_z_ratio > 0.0f)
					{
						chain_target = get_thing(link.target_id);
						break;
					}
				}

				if (from_thing && chain_target)
				{
					float mid_x = (from_thing->mLocation.mX + chain_target->mLocation.mX) * 0.5f;
					float mid_y = (from_thing->mLocation.mY + chain_target->mLocation.mY) * 0.5f;
					float sdx = mid_x - thing->mLocation.mX;
					float sdy = mid_y - thing->mLocation.mY;
					float sdist = sqrtf(sdx * sdx + sdy * sdy);

					if (sdist > 1.0f && std::isfinite(sdist))
					{
						float snx = sdx / sdist;
						float sny = sdy / sdist;
						float pull = std::min(sdist, data.links[0].retract_speed * dt * 0.5f);
						Vectormath::Vector2 delta = {snx * pull, sny * pull};
						if (std::isfinite(delta.mX) && std::isfinite(delta.mY))
						{
							g_shift_location(thing, delta);
						}
					}
				}
			}
		}
	}

	static void cleanup_stale_tethers();

	static char hook_PhysicsSystem_UpdateThing(void *this_, Thing_H2 *thing, float elapsedSeconds)
	{
		char result = big::g_hooking->get_original<hook_PhysicsSystem_UpdateThing>()(this_, thing, elapsedSeconds);

		{
			std::scoped_lock l(g_tether_mutex);

			static float g_last_update_time = -1.0f;
			if (elapsedSeconds != g_last_update_time)
			{
				g_last_update_time = elapsedSeconds;
				cleanup_stale_tethers();
				update_all_tethers(elapsedSeconds);
			}
		}

		return result;
	}

	// Clamps elastic tethers during unit movement.
	static bool hook_Unit_ApplyShift(void *this_, Vectormath::Vector2 destination, Vectormath::Vector2 shift, bool checkAnimState)
	{
		bool result = big::g_hooking->get_original<hook_Unit_ApplyShift>()(this_, destination, shift, checkAnimState);

		{
			std::scoped_lock l(g_tether_mutex);

			auto *thing = reinterpret_cast<Thing_H2 *>(this_);
			if (thing)
			{
				auto *data = get_tether_data(thing->mId);
				if (data && !data->links.empty())
				{
					for (auto &link : data->links)
					{
						if (link.elasticity <= 0.0f)
							continue;
						auto *target = get_thing(link.target_id);
						if (!target)
							continue;
						float dx      = thing->mLocation.mX - target->mLocation.mX;
						float dy      = thing->mLocation.mY - target->mLocation.mY;
						float dist_sq = dx * dx + dy * dy;
						if (dist_sq > link.distance * link.distance)
						{
							float dist         = sqrtf(dist_sq);
							thing->mLocation.mX = target->mLocation.mX + (dx / dist) * link.distance;
							thing->mLocation.mY = target->mLocation.mY + (dy / dist) * link.distance;
						}
					}
				}
			}
		}

		return result;
	}

	static void cleanup_stale_tethers()
	{
		std::vector<int> to_remove;

		for (auto &[id, data] : g_tether_data)
		{
			auto *thing = get_thing(id);
			if (!thing)
			{
				to_remove.push_back(id);
				continue;
			}

			std::erase_if(data.links,
			              [](const TetherLink &link)
			              {
				              return get_thing(link.target_id) == nullptr;
			              });

			std::erase_if(data.tethered_from,
			              [](int tid)
			              {
				              return get_thing(tid) == nullptr;
			              });
		}

		for (int id : to_remove)
		{
			auto it = g_tether_data.find(id);
			if (it != g_tether_data.end())
			{
				for (auto &link : it->second.links)
				{
					auto *target_data = get_tether_data(link.target_id);
					if (target_data)
					{
						std::erase(target_data->tethered_from, id);
					}
				}
				for (int source_id : it->second.tethered_from)
				{
					auto *source_data = get_tether_data(source_id);
					if (source_data)
					{
						std::erase_if(source_data->links,
						              [id](const TetherLink &link)
						              {
							              return link.target_id == id;
						              });
					}
				}
				g_tether_data.erase(it);
			}
		}
	}

	// Lua API: Function
	// Table: tethers
	// Name: add
	// Param: source_id: integer: The Thing Id of the object to be constrained.
	// Param: target_id: integer: The Thing Id of the anchor object.
	// Param: distance: number: Maximum distance before the tether pulls the source back.
	// Param: retract_speed: number: optional. Retraction speed when beyond distance. Default 0.
	// Param: elasticity: number: optional. Elasticity coefficient for spring tethers. Default 0.
	// Param: track_z_ratio: number: optional. Z-axis tracking ratio (0.0-1.0). Default 0.
	// Returns: boolean: Whether the tether was created successfully.
	// Creates a tether constraint between two game objects.
	static bool add_tether(int source_id, int target_id, float distance, sol::optional<float> retract_speed, sol::optional<float> elasticity, sol::optional<float> track_z_ratio)
	{
		auto *source = get_thing(source_id);
		if (!source)
		{
			LOG(WARNING) << "tethers.add: source thing " << source_id << " does not exist";
			return false;
		}

		auto *target = get_thing(target_id);
		if (!target)
		{
			LOG(WARNING) << "tethers.add: target thing " << target_id << " does not exist";
			return false;
		}

		std::scoped_lock l(g_tether_mutex);

		auto &src_data = g_tether_data[source_id];

		// Find or create link for this target
		TetherLink *link = nullptr;
		for (auto &l : src_data.links)
		{
			if (l.target_id == target_id) { link = &l; break; }
		}
		if (!link)
		{
			src_data.links.push_back({});
			link = &src_data.links.back();
			link->target_id = target_id;

			float dx = source->mLocation.mX - target->mLocation.mX;
			float dy = source->mLocation.mY - target->mLocation.mY;
			if (dx * dx + dy * dy > 0.01f)
				link->rest_angle = atan2f(dy, dx);
			else
				link->rest_angle = (float)(source_id % 360) * 0.0174533f;
		}

		link->distance      = distance;
		link->retract_speed = retract_speed.value_or(0.0f);
		link->elasticity    = elasticity.value_or(0.0f);
		link->track_z_ratio = track_z_ratio.value_or(0.0f);

		auto &tgt_data = g_tether_data[target_id];
		if (std::find(tgt_data.tethered_from.begin(), tgt_data.tethered_from.end(), source_id) == tgt_data.tethered_from.end())
		{
			tgt_data.tethered_from.push_back(source_id);
		}

		return true;
	}

	// Lua API: Function
	// Table: tethers
	// Name: remove
	// Param: source_id: integer: The Thing Id of the constrained object.
	// Param: target_id: integer: The Thing Id of the anchor object.
	// Returns: boolean: Whether the tether was removed.
	// Removes a specific tether constraint between two game objects.
	static bool remove_tether(int source_id, int target_id)
	{
		std::scoped_lock l(g_tether_mutex);

		auto *src_data = get_tether_data(source_id);
		if (src_data)
		{
			std::erase_if(src_data->links,
			              [target_id](const TetherLink &l) { return l.target_id == target_id; });
		}

		auto *tgt_data = get_tether_data(target_id);
		if (tgt_data)
		{
			std::erase(tgt_data->tethered_from, source_id);
		}

		return true;
	}

	// Lua API: Function
	// Table: tethers
	// Name: remove_all
	// Param: thing_id: integer: The Thing Id to remove all tethers from.
	// Removes all tether constraints involving the specified object.
	static void remove_all_tethers(int thing_id)
	{
		std::scoped_lock l(g_tether_mutex);

		auto *data = get_tether_data(thing_id);
		if (!data) return;

		for (auto &link : data->links)
		{
			auto *tgt = get_tether_data(link.target_id);
			if (tgt) std::erase(tgt->tethered_from, thing_id);
		}

		for (int sid : data->tethered_from)
		{
			auto *src = get_tether_data(sid);
			if (src)
			{
				std::erase_if(src->links,
				              [thing_id](const TetherLink &l) { return l.target_id == thing_id; });
			}
		}

		g_tether_data.erase(thing_id);
	}

	static void clear_all_tether_data()
	{
		std::scoped_lock l(g_tether_mutex);
		g_tether_data.clear();
	}

	void bind(sol::state_view &state, sol::table &lua_ext)
	{
		clear_all_tether_data();
		// Resolve engine symbols
		auto world_addr = big::hades2_symbol_to_address["sgg::world"];
		if (world_addr)
		{
			g_world_ptr = world_addr.as<void **>();
		}
		else
		{
			LOG(ERROR) << "tethers: failed to resolve sgg::world";
			return;
		}

		auto get_active_thing_addr = big::hades2_symbol_to_address["sgg::World::GetActiveThing"];
		if (get_active_thing_addr)
		{
			g_get_active_thing = get_active_thing_addr.as_func<Thing_H2 *(void *, int)>();
		}
		else
		{
			LOG(ERROR) << "tethers: failed to resolve sgg::World::GetActiveThing";
			return;
		}

		auto shift_location_addr = big::hades2_symbol_to_address["sgg::Thing::ShiftLocation"];
		if (shift_location_addr)
		{
			g_shift_location = shift_location_addr.as_func<void(void *, Vectormath::Vector2)>();
			LOG(INFO) << "tethers: resolved ShiftLocation";
		}
		else
		{
			LOG(WARNING) << "tethers: failed to resolve sgg::Thing::ShiftLocation - direct position updates disabled";
		}

		// Try hooking PhysicsSystem::UpdateThing for per-frame tether updates
		auto update_thing_addr = big::hades2_symbol_to_address["sgg::PhysicsSystem::UpdateThing"];
		if (update_thing_addr)
		{
			static auto hook_ = big::hooking::detour_hook_helper::add<hook_PhysicsSystem_UpdateThing>("hook_PhysicsSystem_UpdateThing", update_thing_addr);
			LOG(INFO) << "tethers: hooked PhysicsSystem::UpdateThing";
		}
		else
		{
			LOG(WARNING) << "tethers: sgg::PhysicsSystem::UpdateThing not found in PDB - per-frame tether updates unavailable";
		}

		// Hook Unit::ApplyShift for position clamping on unit movement
		auto apply_shift_addr = big::hades2_symbol_to_address["sgg::Unit::ApplyShift"];
		if (apply_shift_addr)
		{
			static auto hook_ = big::hooking::detour_hook_helper::add<hook_Unit_ApplyShift>("hook_Unit_ApplyShift", apply_shift_addr);
			LOG(INFO) << "tethers: hooked Unit::ApplyShift";
		}
		else
		{
			LOG(WARNING) << "tethers: sgg::Unit::ApplyShift not found - position clamping for units unavailable";
		}

		// Register Lua API
		auto ns = lua_ext.create_named("tethers");
		ns.set_function("add", add_tether);
		ns.set_function("remove", remove_tether);
		ns.set_function("remove_all", remove_all_tethers);
		ns.set_function("clear_all", clear_all_tether_data);

		LOG(INFO) << "tethers: Lua API registered";
	}
} // namespace lua::hades::tethers
