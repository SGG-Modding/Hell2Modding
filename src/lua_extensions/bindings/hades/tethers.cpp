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

	// Per source-target link: each link has its own physics params so that
	// a single source can be tethered to multiple targets with different
	// distance / retractSpeed / etc. (e.g. Hydra neck9 → neck8 at dist=73
	// AND neck9 → base at dist=60).
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

	static TetherLink *find_link(TetherData &data, int target_id)
	{
		for (auto &link : data.links)
		{
			if (link.target_id == target_id)
			{
				return &link;
			}
		}
		return nullptr;
	}

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

	// ── Tether update ─────────────────────────────────────────────────────
	// Double-buffered: compute all pending shifts first (reading current
	// positions), then apply all at once.  This prevents the chain from
	// cascading to the anchor in a single frame, while still processing
	// ALL tethered things (including obstacles the physics system skips).
	// Multi-frame cascade happens naturally via velocity transfer (67%).

	static int g_debug_log_counter = 0;

	struct PendingOp
	{
		Thing_H2 *thing;
		Vectormath::Vector2 shift_delta;
		// Chain cascade: shift the TARGET to follow the source
		Thing_H2 *cascade_target;
		Vectormath::Vector2 cascade_delta;
		// Z tracking
		bool has_z_track;
		float z_delta;
		bool set_ignore_gravity;
	};

	static void update_all_tethers(float dt)
	{
		bool should_log = (++g_debug_log_counter % 120 == 0);
		std::vector<PendingOp> pending;

		// ── Phase 1: Compute all pending operations (positions unchanged) ──
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

				if (should_log)
				{
					LOG(DEBUG) << "tethers: " << thing->mId << " -> " << link.target_id
					           << " pos=(" << thing->mLocation.mX << "," << thing->mLocation.mY << ")"
					           << " target=(" << target->mLocation.mX << "," << target->mLocation.mY << ")"
					           << " dist=" << dist << " max=" << link.distance
					           << " elast=" << link.elasticity
					           << " retract=" << link.retract_speed
					           << " hasPhys=" << (thing->pPhysics ? "Y" : "N");
				}

				if (!std::isfinite(dist_sq))
				{
					continue;
				}

				if (link.elasticity > 0.0f)
				{
					// ── ELASTIC TETHER — critically damped spring ──
					if (!thing->pPhysics)
					{
						continue;
					}

					auto *pc = static_cast<PhysicsComponent_H2_Velocity *>(thing->pPhysics);

					if (dist < 0.001f)
					{
						float angle = (float)(thing->mId % 360) * 0.0174533f;
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
					// ── NON-ELASTIC (CHAIN) TETHER — H1 CheckTether equivalent ──
					if (dist <= link.distance)
					{
						// Within range — check Z tracking only
					}
					else
					{
						float nx = dx / dist;
						float ny = dy / dist;
						float overshoot = dist - link.distance;

						PendingOp op{};
						op.thing = thing;
						op.shift_delta = {nx * overshoot, ny * overshoot};
						op.cascade_target = nullptr;
						op.cascade_delta = {0.0f, 0.0f};
						op.has_z_track = false;

						// Chain cascade: shift the TARGET by 67% of overshoot in the
						// direction the source was moving (opposite of clamp direction).
						// Skip passive anchors (retract_speed=0) — base must stay fixed.
						if (link.retract_speed > 0.0f)
						{
							constexpr float CASCADE_FRACTION = 0.67f;
							op.cascade_target = target;
							op.cascade_delta = {-nx * overshoot * CASCADE_FRACTION,
							                    -ny * overshoot * CASCADE_FRACTION};
						}

						pending.push_back(op);
					}
				}

				// Z tracking (queue for application in phase 2)
				if (link.track_z_ratio > 0.0f)
				{
					float dz = target->mZLocation - thing->mZLocation;
					PendingOp zop{};
					zop.thing = thing;
					zop.shift_delta = {0.0f, 0.0f};
					zop.cascade_target = nullptr;
					zop.cascade_delta = {0.0f, 0.0f};
					zop.has_z_track = true;
					zop.z_delta = dz * link.track_z_ratio * dt;
					zop.set_ignore_gravity = (thing->pPhysics != nullptr);
					pending.push_back(zop);
				}
			}
		}

		// ── Phase 2: Apply all operations at once ──────────────────────────
		for (auto &op : pending)
		{
			// Clamp source to tether edge
			if ((op.shift_delta.mX != 0.0f || op.shift_delta.mY != 0.0f) && g_shift_location)
			{
				if (std::isfinite(op.shift_delta.mX) && std::isfinite(op.shift_delta.mY))
				{
					g_shift_location(op.thing, op.shift_delta);
				}
			}

			// Cascade: shift target to follow source (replaces H1's velocity transfer)
			if (op.cascade_target && g_shift_location)
			{
				if (std::isfinite(op.cascade_delta.mX) && std::isfinite(op.cascade_delta.mY)
				    && (op.cascade_delta.mX != 0.0f || op.cascade_delta.mY != 0.0f))
				{
					g_shift_location(op.cascade_target, op.cascade_delta);
				}
			}

			if (op.has_z_track)
			{
				op.thing->mZLocation += op.z_delta;
				if (op.set_ignore_gravity && op.thing->pPhysics)
				{
					auto *pc = static_cast<PhysicsComponent_H2_Velocity *>(op.thing->pPhysics);
					pc->mIgnoreGravity = true;
				}
			}
		}
	}

	// Periodic cleanup counter (avoid doing it every frame)
	static int g_cleanup_counter = 0;
	static constexpr int CLEANUP_INTERVAL = 60;

	static void cleanup_stale_tethers();

	// ── Hook: PhysicsSystem::UpdateThing ───────────────────────────────────
	// Runs update_all_tethers once per frame (guarded), processing ALL
	// tethered things including dormant obstacles the engine skips.

	static char hook_PhysicsSystem_UpdateThing(void *this_, Thing_H2 *thing, float elapsedSeconds)
	{
		char result = big::g_hooking->get_original<hook_PhysicsSystem_UpdateThing>()(this_, thing, elapsedSeconds);

		{
			std::scoped_lock l(g_tether_mutex);

			// Run tether update once per frame
			static float s_last_dt = -1.0f;
			if (elapsedSeconds != s_last_dt)
			{
				s_last_dt = elapsedSeconds;
				update_all_tethers(elapsedSeconds);

				if (++g_cleanup_counter >= CLEANUP_INTERVAL)
				{
					g_cleanup_counter = 0;
					cleanup_stale_tethers();
				}
			}

			// Keep tethered things physics-active
			if (thing)
			{
				auto *data = get_tether_data(thing->mId);
				if (data && !data->links.empty())
				{
					result = 1;
				}
			}
		}

		return result;
	}

	// ── Hook: Unit::ApplyShift ─────────────────────────────────────────────
	// In H1, CheckTether only runs for Obstacles, NOT Units. Units get a
	// soft pull via UpdateTethers(RetractSpeed). So we do NOT hard-clamp
	// units here. The batch update_all_tethers handles the constraint.
	// Elastic tethers still need per-shift clamping for responsiveness.

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
						// Only clamp elastic tethers in ApplyShift (they need responsiveness).
						// Non-elastic (chain) tethers are handled by the batch update,
						// matching H1 where CheckTether is Obstacle-only.
						if (link.elasticity <= 0.0f)
						{
							continue;
						}
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

			// Remove dead targets from links
			std::erase_if(data.links,
			              [](const TetherLink &link)
			              {
				              return get_thing(link.target_id) == nullptr;
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

		auto &src_data = g_tether_data[source_id];

		// Find or create a link for this specific target
		TetherLink *link = find_link(src_data, target_id);
		if (!link)
		{
			src_data.links.push_back({});
			link = &src_data.links.back();
			link->target_id = target_id;

			// Capture initial resting angle from spawn offset
			float dx = source->mLocation.mX - target->mLocation.mX;
			float dy = source->mLocation.mY - target->mLocation.mY;
			if (dx * dx + dy * dy > 0.01f)
			{
				link->rest_angle = atan2f(dy, dx);
			}
			else
			{
				link->rest_angle = (float)(source_id % 360) * 0.0174533f;
			}
		}

		link->distance      = distance;
		link->retract_speed = retract_speed.value_or(0.0f);
		link->elasticity    = elasticity.value_or(0.0f);
		link->track_z_ratio = track_z_ratio.value_or(0.0f);

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
			std::erase_if(src_data->links,
			              [target_id](const TetherLink &link)
			              {
				              return link.target_id == target_id;
			              });
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
		for (auto &link : data->links)
		{
			auto *tgt = get_tether_data(link.target_id);
			if (tgt)
			{
				std::erase(tgt->tethered_from, thing_id);
			}
		}

		// Remove this thing from all sources' links
		for (int sid : data->tethered_from)
		{
			auto *src = get_tether_data(sid);
			if (src)
			{
				std::erase_if(src->links,
				              [thing_id](const TetherLink &link)
				              {
					              return link.target_id == thing_id;
				              });
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
			for (auto &link : data->links)
			{
				result[idx++] = link.target_id;
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
		if (!data || data->links.empty())
		{
			return false;
		}

		for (auto &link : data->links)
		{
			link.distance = distance;
		}
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
		if (!data || data->links.empty())
		{
			return false;
		}

		for (auto &link : data->links)
		{
			link.retract_speed = speed;
		}
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
		if (!data || data->links.empty())
		{
			return false;
		}

		for (auto &link : data->links)
		{
			link.elasticity = elasticity;
		}
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
		if (!data || data->links.empty())
		{
			return false;
		}

		for (auto &link : data->links)
		{
			link.track_z_ratio = ratio;
		}
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
		return (data && !data->links.empty()) ? data->links[0].distance : 0.0f;
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
		return data && !data->links.empty();
	}

	// ── Bind ───────────────────────────────────────────────────────────────

	void bind(sol::state_view &state, sol::table &lua_ext)
	{
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

		LOG(INFO) << "tethers: Lua API registered (mLocation offset=0x70)";
	}
} // namespace lua::hades::tethers
