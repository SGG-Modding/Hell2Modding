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
	// ── H2 sgg::Thing field access via raw offsets ──────────────────────────
	// Offsets verified against Ghidra decompilation of H2 binary:
	//   mId=0x48, mZLocation=0x54, mLocation=0x70, pPhysics=0xB8.
	// Cross-checked with existing static_asserts: mName=0x4C, pText=0xF8, pAnim=0x110.
	// Note: mSpawnTime is double (8 bytes) in H2, pushing mLocation to 0x70.

	struct Thing_H2
	{
		char pad_0[0x48];
		int mId;                         // 0x48  (verified: thingId matches)
		char pad_1[0x08];               // 0x4C..0x53 (mName + mAttachOffsetZ)
		float mZLocation;               // 0x54
		char pad_2[0x18];               // 0x58..0x6F (mTallness, mTimeModifierFraction, mElapsedTimeMultiplier, 4-byte pad, mSpawnTime as double)
		Vectormath::Vector2 mLocation;  // 0x70  (confirmed by Ghidra: mSpawnTime is double, shifts mLocation to 0x70)
		char pad_3[0x40];               // 0x78..0xB7
		void *pPhysics;                 // 0xB8
	};

	static_assert(offsetof(Thing_H2, mId) == 0x48, "sgg::Thing->mId wrong offset");
	static_assert(offsetof(Thing_H2, mZLocation) == 0x54, "sgg::Thing->mZLocation wrong offset");
	static_assert(offsetof(Thing_H2, mLocation) == 0x70, "sgg::Thing->mLocation wrong offset");
	static_assert(offsetof(Thing_H2, pPhysics) == 0xB8, "sgg::Thing->pPhysics wrong offset");

	// ── H2 PhysicsComponent — minimal struct for velocity access ───────────
	// H2 PhysicsComponent is 0x58 bytes. mVelocity confirmed at 0x48 by Ghidra.

	struct PhysicsComponent_H2_Velocity
	{
		char pad_0[0x35];
		bool mIgnoreGravity;            // 0x35
		char pad_1[0x12];               // 0x36..0x47
		Vectormath::Vector2 mVelocity;  // 0x48
	};

	static_assert(offsetof(PhysicsComponent_H2_Velocity, mIgnoreGravity) == 0x35, "PhysicsComponent->mIgnoreGravity wrong offset");
	static_assert(offsetof(PhysicsComponent_H2_Velocity, mVelocity) == 0x48, "PhysicsComponent->mVelocity wrong offset");

	static void **g_world_ptr = nullptr;

	using GetActiveThingFn = Thing_H2 *(*)(void *world, int id);
	static GetActiveThingFn g_get_active_thing = nullptr;

	// ApplyForce signature confirmed from H2 Ghidra decompilation:
	//   void ApplyForce(PhysicsComponent* this, Vector2 direction, float magnitude, bool isSelfApplied, float zForce)
	using ApplyForceFn = void (*)(void *physics, Vectormath::Vector2 direction, float magnitude, bool isSelfApplied, float zForce);
	static ApplyForceFn g_apply_force = nullptr;

	// sgg::Thing::ShiftLocation(Thing*, Vector2 delta) — moves obstacle by delta, updates engine state
	using ShiftLocationFn = void (*)(void *thing, Vectormath::Vector2 delta);
	static ShiftLocationFn g_shift_location = nullptr;

	// ── Helper: resolve thing by ID ────────────────────────────────────────

	static Thing_H2 *get_thing(int id)
	{
		if (!g_world_ptr || !*g_world_ptr || !g_get_active_thing)
		{
			return nullptr;
		}

		return g_get_active_thing(*g_world_ptr, id);
	}

	// ── Tether state: sidecar data structure ───────────────────────────────
	// The H2 PhysicsComponent may or may not have tether fields in memory
	// (they exist in the PDB-defined struct but no engine code accesses them,
	// so Ghidra reports a smaller struct). For safety, we store all tether
	// data in our own sidecar map keyed by Thing ID, and use the engine's
	// ApplyForce function to apply forces (avoiding mVelocity offset issues).

	struct TetherData
	{
		float distance       = 0.0f;
		float elasticity     = 0.0f;
		float retract_speed  = 0.0f;
		float track_z_ratio  = 0.0f;
		// Per-target resting angle (radians) — preserves spawn offset direction
		std::unordered_map<int, float> rest_angles;
		std::vector<int> tethered_to;
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

	// ── Global tether update (runs all tethers on every physics tick) ──────

	static int g_debug_log_counter = 0;

	static void update_all_tethers(float dt)
	{
		bool should_log = (++g_debug_log_counter % 120 == 0);

		for (auto &[id, data] : g_tether_data)
		{
			if (data.tethered_to.empty() || data.distance <= 0.0f)
			{
				continue;
			}

			auto *thing = get_thing(id);
			if (!thing)
			{
				continue;
			}

			for (int target_id : data.tethered_to)
			{
				auto *target = get_thing(target_id);
				if (!target)
				{
					continue;
				}

				float dx      = target->mLocation.mX - thing->mLocation.mX;
				float dy      = target->mLocation.mY - thing->mLocation.mY;
				float dist_sq = dx * dx + dy * dy;
				float dist    = (dist_sq > 0.0f) ? sqrtf(dist_sq) : 0.0f;

				if (should_log)
				{
					LOG(DEBUG) << "tethers: " << id << " -> " << target_id
					           << " pos=(" << thing->mLocation.mX << "," << thing->mLocation.mY << ")"
					           << " target=(" << target->mLocation.mX << "," << target->mLocation.mY << ")"
					           << " dist=" << dist << " max=" << data.distance
					           << " elast=" << data.elasticity
					           << " hasPhys=" << (thing->pPhysics ? "Y" : "N");
				}

				if (!std::isfinite(dist_sq))
				{
					continue;
				}

				if (data.elasticity > 0.0f)
				{
					// ── ELASTIC TETHER — critically damped spring ──
					// Writes mVelocity directly; engine's obstacle physics applies it.
					// Uses critically damped spring for smooth follow with minimal overshoot.

					if (!thing->pPhysics)
					{
						continue;
					}

					auto *pc = static_cast<PhysicsComponent_H2_Velocity *>(thing->pPhysics);

					if (dist < 0.001f)
					{
						// At center — push outward to spread apart
						float angle = (float)(id % 360) * 0.0174533f;
						pc->mVelocity.mX = cosf(angle) * data.distance * 2.0f;
						pc->mVelocity.mY = sinf(angle) * data.distance * 2.0f;
						continue;
					}

					// Ideal position: at tether distance from target at the stored resting angle
					float rest_angle = 0.0f;
					auto angle_it = data.rest_angles.find(target_id);
					if (angle_it != data.rest_angles.end())
					{
						rest_angle = angle_it->second;
					}
					float ideal_x = target->mLocation.mX + cosf(rest_angle) * data.distance;
					float ideal_y = target->mLocation.mY + sinf(rest_angle) * data.distance;

					// Displacement from ideal
					float diff_x = ideal_x - thing->mLocation.mX;
					float diff_y = ideal_y - thing->mLocation.mY;

					// Critically damped spring: vel = spring_force - damping * current_vel
					// omega = sqrt(elasticity) gives natural frequency
					// Critical damping coefficient = 2 * omega
					float omega = sqrtf(data.elasticity) * 0.1f;
					float damping_coeff = 2.0f * omega;

					// Spring acceleration = omega^2 * displacement - damping * velocity
					float ax = omega * omega * diff_x - damping_coeff * pc->mVelocity.mX;
					float ay = omega * omega * diff_y - damping_coeff * pc->mVelocity.mY;

					pc->mVelocity.mX += ax * dt;
					pc->mVelocity.mY += ay * dt;
				}
				else
				{
					// ── NON-ELASTIC (CHAIN) TETHER ──
					if (data.retract_speed <= 0.0f && data.track_z_ratio <= 0.0f)
					{
						continue;
					}

					if (dist <= data.distance)
					{
						continue;
					}

					float nx = dx / dist;
					float ny = dy / dist;
					float overshoot = dist - data.distance;

					// Hard clamp + cascade via ShiftLocation (triggers engine rendering updates)
					if (std::isfinite(overshoot) && g_shift_location)
					{
						Vectormath::Vector2 delta = {nx * overshoot, ny * overshoot};
						g_shift_location(thing, delta);

						// Cascade: shift target in the opposite direction (following the source)
						if (data.retract_speed > 0.0f)
						{
							constexpr float CASCADE_FRACTION = 0.95f;
							Vectormath::Vector2 cascade = {-nx * overshoot * CASCADE_FRACTION,
							                               -ny * overshoot * CASCADE_FRACTION};
							g_shift_location(target, cascade);
						}
					}
				}

				// Z tracking
				if (data.track_z_ratio > 0.0f)
				{
					float dz = target->mZLocation - thing->mZLocation;
					thing->mZLocation += dz * data.track_z_ratio * dt;
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

	// ── Hook: Unit::ApplyShift ─────────────────────────────────────────────
	// Also runs global tether update as a fallback tick if UpdateThing isn't available.

	static bool hook_Unit_ApplyShift(void *this_, Vectormath::Vector2 destination, Vectormath::Vector2 shift, bool checkAnimState)
	{
		bool result = big::g_hooking->get_original<hook_Unit_ApplyShift>()(this_, destination, shift, checkAnimState);

		{
			std::scoped_lock l(g_tether_mutex);

			// Clamp this specific unit if it has a tether
			auto *thing = reinterpret_cast<Thing_H2 *>(this_);
			if (thing)
			{
				auto *data = get_tether_data(thing->mId);
				if (data && !data->tethered_to.empty() && data->elasticity > 0.0f)
				{
					// Only clamp elastic tethers in ApplyShift.
					// Non-elastic (chain) tethers are handled by the batch update.
					for (int target_id : data->tethered_to)
					{
						auto *target = get_thing(target_id);
						if (!target)
							continue;
						float dx      = thing->mLocation.mX - target->mLocation.mX;
						float dy      = thing->mLocation.mY - target->mLocation.mY;
						float dist_sq = dx * dx + dy * dy;
						if (dist_sq > data->distance * data->distance)
						{
							float dist         = sqrtf(dist_sq);
							thing->mLocation.mX = target->mLocation.mX + (dx / dist) * data->distance;
							thing->mLocation.mY = target->mLocation.mY + (dy / dist) * data->distance;
						}
					}
				}
			}
		}

		return result;
	}

	// ── Cleanup: remove stale tether references ────────────────────────────

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

			// Remove dead targets from tethered_to
			std::erase_if(data.tethered_to,
			              [](int tid)
			              {
				              return get_thing(tid) == nullptr;
			              });

			// Remove dead sources from tethered_from
			std::erase_if(data.tethered_from,
			              [](int tid)
			              {
				              return get_thing(tid) == nullptr;
			              });
		}

		for (int id : to_remove)
		{
			// Before removing, clean up reverse references
			auto it = g_tether_data.find(id);
			if (it != g_tether_data.end())
			{
				for (int target_id : it->second.tethered_to)
				{
					auto *target_data = get_tether_data(target_id);
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
						std::erase(source_data->tethered_to, id);
					}
				}
				g_tether_data.erase(it);
			}
		}
	}

	// ── Lua API ────────────────────────────────────────────────────────────

	// Lua API: Function
	// Table: tethers
	// Name: add
	// Param: source_id: integer: The Thing Id of the object to be constrained (the one that gets pulled).
	// Param: target_id: integer: The Thing Id of the anchor object (the one that pulls).
	// Param: distance: number: Maximum distance before the tether pulls the source back.
	// Param: retract_speed: number: optional. Speed at which the source retracts toward the target when beyond distance. Default 0.
	// Param: elasticity: number: optional. Elasticity coefficient. When > 0, allows elastic stretching instead of hard clamping. Default 0.
	// Param: track_z_ratio: number: optional. How much the source tracks the target's Z position (0.0-1.0). Default 0.
	// Creates a tether constraint between two game objects.
	// The source object will be pulled toward the target when it exceeds the tether distance.
	//
	// **Example Usage:**
	// ```lua
	// -- Tether a neck segment to the head
	// rom.tethers.add(neckSegmentId, headId, 73, 500, 0, 0.2)
	// ```
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

		auto &src_data         = g_tether_data[source_id];
		src_data.distance      = distance;
		src_data.retract_speed = retract_speed.value_or(0.0f);
		src_data.elasticity    = elasticity.value_or(0.0f);
		src_data.track_z_ratio = track_z_ratio.value_or(0.0f);

		// Add target to source's tethered_to (avoid duplicates)
		if (std::find(src_data.tethered_to.begin(), src_data.tethered_to.end(), target_id) == src_data.tethered_to.end())
		{
			src_data.tethered_to.push_back(target_id);
		}

		// Capture initial resting angle from spawn offset
		if (src_data.rest_angles.find(target_id) == src_data.rest_angles.end())
		{
			float dx = source->mLocation.mX - target->mLocation.mX;
			float dy = source->mLocation.mY - target->mLocation.mY;
			if (dx * dx + dy * dy > 0.01f)
			{
				src_data.rest_angles[target_id] = atan2f(dy, dx);
			}
			else
			{
				// No offset yet — assign based on ID for spread
				src_data.rest_angles[target_id] = (float)(source_id % 360) * 0.0174533f;
			}
		}

		// Add source to target's tethered_from
		auto &tgt_data = g_tether_data[target_id];
		if (std::find(tgt_data.tethered_from.begin(), tgt_data.tethered_from.end(), source_id) == tgt_data.tethered_from.end())
		{
			tgt_data.tethered_from.push_back(source_id);
		}

		LOG(DEBUG) << "tethers.add: " << source_id << " -> " << target_id << " (dist=" << distance << ")";
		return true;
	}

	// Lua API: Function
	// Table: tethers
	// Name: remove
	// Param: source_id: integer: The Thing Id of the constrained object.
	// Param: target_id: integer: The Thing Id of the anchor object.
	// Removes a tether constraint between two game objects.
	static bool remove_tether(int source_id, int target_id)
	{
		std::scoped_lock l(g_tether_mutex);

		auto *src_data = get_tether_data(source_id);
		if (src_data)
		{
			std::erase(src_data->tethered_to, target_id);
			if (src_data->tethered_to.empty())
			{
				src_data->distance      = 0.0f;
				src_data->retract_speed = 0.0f;
				src_data->elasticity    = 0.0f;
				src_data->track_z_ratio = 0.0f;
			}
		}

		auto *tgt_data = get_tether_data(target_id);
		if (tgt_data)
		{
			std::erase(tgt_data->tethered_from, source_id);
		}

		LOG(DEBUG) << "tethers.remove: " << source_id << " -> " << target_id;
		return true;
	}

	// Lua API: Function
	// Table: tethers
	// Name: remove_all
	// Param: thing_id: integer: The Thing Id to remove all tethers from (both as source and target).
	// Removes all tether constraints involving the specified object.
	static void remove_all_tethers(int thing_id)
	{
		std::scoped_lock l(g_tether_mutex);

		auto *data = get_tether_data(thing_id);
		if (!data)
		{
			return;
		}

		// Remove this thing from all targets' tethered_from
		for (int tid : data->tethered_to)
		{
			auto *tgt = get_tether_data(tid);
			if (tgt)
			{
				std::erase(tgt->tethered_from, thing_id);
			}
		}

		// Remove this thing from all sources' tethered_to
		for (int sid : data->tethered_from)
		{
			auto *src = get_tether_data(sid);
			if (src)
			{
				std::erase(src->tethered_to, thing_id);
				if (src->tethered_to.empty())
				{
					src->distance      = 0.0f;
					src->retract_speed = 0.0f;
					src->elasticity    = 0.0f;
					src->track_z_ratio = 0.0f;
				}
			}
		}

		g_tether_data.erase(thing_id);

		LOG(DEBUG) << "tethers.remove_all: " << thing_id;
	}

	// Lua API: Function
	// Table: tethers
	// Name: get_tethered_to
	// Param: thing_id: integer: The Thing Id to query.
	// Returns: table<integer>: List of Thing Ids that this object is tethered TO.
	static sol::table get_tethered_to(int thing_id, sol::this_state state)
	{
		sol::table result(state, sol::create);

		std::scoped_lock l(g_tether_mutex);
		auto *data = get_tether_data(thing_id);
		if (data)
		{
			int idx = 1;
			for (int tid : data->tethered_to)
			{
				result[idx++] = tid;
			}
		}

		return result;
	}

	// Lua API: Function
	// Table: tethers
	// Name: get_tethered_from
	// Param: thing_id: integer: The Thing Id to query.
	// Returns: table<integer>: List of Thing Ids that are tethered FROM this object (things that have this as their anchor).
	static sol::table get_tethered_from(int thing_id, sol::this_state state)
	{
		sol::table result(state, sol::create);

		std::scoped_lock l(g_tether_mutex);
		auto *data = get_tether_data(thing_id);
		if (data)
		{
			int idx = 1;
			for (int sid : data->tethered_from)
			{
				result[idx++] = sid;
			}
		}

		return result;
	}

	// Lua API: Function
	// Table: tethers
	// Name: set_distance
	// Param: thing_id: integer: The Thing Id whose tether distance to set.
	// Param: distance: number: New tether distance.
	// Modifies the tether distance on an existing tether.
	static bool set_distance(int thing_id, float distance)
	{
		std::scoped_lock l(g_tether_mutex);
		auto *data = get_tether_data(thing_id);
		if (!data)
		{
			return false;
		}

		data->distance = distance;
		return true;
	}

	// Lua API: Function
	// Table: tethers
	// Name: set_retract_speed
	// Param: thing_id: integer: The Thing Id whose retract speed to set.
	// Param: speed: number: New retract speed.
	// Modifies the tether retraction speed on an existing tether.
	static bool set_retract_speed(int thing_id, float speed)
	{
		std::scoped_lock l(g_tether_mutex);
		auto *data = get_tether_data(thing_id);
		if (!data)
		{
			return false;
		}

		data->retract_speed = speed;
		return true;
	}

	// Lua API: Function
	// Table: tethers
	// Name: set_elasticity
	// Param: thing_id: integer: The Thing Id whose elasticity to set.
	// Param: elasticity: number: New elasticity value.
	// Modifies the tether elasticity on an existing tether.
	static bool set_elasticity(int thing_id, float elasticity)
	{
		std::scoped_lock l(g_tether_mutex);
		auto *data = get_tether_data(thing_id);
		if (!data)
		{
			return false;
		}

		data->elasticity = elasticity;
		return true;
	}

	// Lua API: Function
	// Table: tethers
	// Name: set_track_z_ratio
	// Param: thing_id: integer: The Thing Id whose Z tracking ratio to set.
	// Param: ratio: number: New Z tracking ratio (0.0-1.0).
	// Modifies the tether Z tracking ratio on an existing tether.
	static bool set_track_z_ratio(int thing_id, float ratio)
	{
		std::scoped_lock l(g_tether_mutex);
		auto *data = get_tether_data(thing_id);
		if (!data)
		{
			return false;
		}

		data->track_z_ratio = ratio;
		return true;
	}

	// Lua API: Function
	// Table: tethers
	// Name: get_distance
	// Param: thing_id: integer: The Thing Id to query.
	// Returns: number: The current tether distance, or 0 if no tether data.
	static float get_distance(int thing_id)
	{
		std::scoped_lock l(g_tether_mutex);
		auto *data = get_tether_data(thing_id);
		return data ? data->distance : 0.0f;
	}

	// Lua API: Function
	// Table: tethers
	// Name: has_tether
	// Param: thing_id: integer: The Thing Id to check.
	// Returns: boolean: Whether this object has any active tethers.
	static bool has_tether(int thing_id)
	{
		std::scoped_lock l(g_tether_mutex);
		auto *data = get_tether_data(thing_id);
		return data && !data->tethered_to.empty();
	}

	// ── Bind ───────────────────────────────────────────────────────────────

	static void clear_all_tether_data()
	{
		std::scoped_lock l(g_tether_mutex);
		g_tether_data.clear();
		LOG(DEBUG) << "tethers: cleared all tether data";
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

		auto apply_force_addr = big::hades2_symbol_to_address["sgg::PhysicsComponent::ApplyForce"];
		if (apply_force_addr)
		{
			g_apply_force = apply_force_addr.as_func<void(void *, Vectormath::Vector2, float, bool, float)>();
			LOG(INFO) << "tethers: resolved ApplyForce";
		}
		else
		{
			LOG(WARNING) << "tethers: failed to resolve sgg::PhysicsComponent::ApplyForce - retraction forces disabled";
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
		ns.set_function("get_tethered_to", get_tethered_to);
		ns.set_function("get_tethered_from", get_tethered_from);
		ns.set_function("set_distance", set_distance);
		ns.set_function("set_retract_speed", set_retract_speed);
		ns.set_function("set_elasticity", set_elasticity);
		ns.set_function("set_track_z_ratio", set_track_z_ratio);
		ns.set_function("get_distance", get_distance);
		ns.set_function("has_tether", has_tether);
		ns.set_function("clear_all", clear_all_tether_data);

		LOG(INFO) << "tethers: Lua API registered (mLocation offset=0x70)";
	}
} // namespace lua::hades::tethers
