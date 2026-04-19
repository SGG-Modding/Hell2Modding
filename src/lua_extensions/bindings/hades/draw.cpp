/// @file draw.cpp
/// @brief Draw-call control for 3D model entries.
///
/// Lua-driven draw-path control.  Current bindings:
///
///   rom.data.set_draw_visible(entry, visible)
///     Show/hide an entire entry across all draw passes.
///
///   rom.data.set_mesh_visible(entry, mesh_name, visible)
///     Show/hide a single mesh inside an entry.
///
///   rom.data.dump_pool_stats()
///     Diagnostic: log vertex/index pool cursor + capacity per
///     shader effect.
///
/// Hooks DoDraw3D / DoDrawShadow3D / DoDraw3DThumbnail (share the
/// `static void(const vector<RenderMesh*>&, uint, int, HashGuid)`
/// signature) with standard detours.  DoDrawShadowCast3D has a
/// different signature (no HashGuid); it's handled via a code cave
/// below.

#include "draw.hpp"

#include "hades_ida.hpp"

#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <lua/lua_manager.hpp>
#include <map>
#include <memory/gm_address.hpp>
#include <mutex>
#include <shared_mutex>
#include <string/string.hpp>
#include <tuple>

namespace lua::hades::draw
{
	// ─── State ─────────────────────────────────────────────────────────

	static std::shared_mutex g_mutex;
	static std::unordered_set<unsigned int> g_hidden_entries;

	// Fast-path flag for the code cave — avoids the function-call overhead
	// on every draw entry when nothing is hidden.
	static volatile uint8_t g_any_active = 0;

	static void update_active_flag()
	{
		g_any_active = g_hidden_entries.empty() ? 0 : 1;
	}

	// Called from the code cave via function pointer.
	// Returns: 0 = pass through, 1 = hidden (skip).  The *out_hash
	// parameter is reserved so the cave's ABI stays stable if a later
	// change adds a remap path (return 2 + *out_hash set).
	static int check_draw_entry(uint32_t hash, uint32_t* /*out_hash*/)
	{
		std::shared_lock l(g_mutex);
		if (g_hidden_entries.count(hash))
			return 1;
		return 0;
	}

	// ─── Detour hooks (DoDraw3D, DoDrawShadow3D, DoDraw3DThumbnail) ──
	// Each hook checks the hidden-set and skips the iteration if its
	// entry is hidden.  DoDrawShadowCast3D has a different signature
	// (no HashGuid parameter), so it's handled via a code cave below.

	static void hook_DoDraw3D(void* vec_ref, unsigned int index, int param, sgg::HashGuid hash)
	{
		{
			std::shared_lock l(g_mutex);
			if (g_hidden_entries.count(hash.mId))
				return;
		}
		big::g_hooking->get_original<hook_DoDraw3D>()(vec_ref, index, param, hash);
	}

	static void hook_DoDrawShadow3D(void* vec_ref, unsigned int index, int param, sgg::HashGuid hash)
	{
		{
			std::shared_lock l(g_mutex);
			if (g_hidden_entries.count(hash.mId))
				return;
		}
		big::g_hooking->get_original<hook_DoDrawShadow3D>()(vec_ref, index, param, hash);
	}

	static void hook_DoDraw3DThumbnail(void* vec_ref, unsigned int index, int param, sgg::HashGuid hash)
	{
		{
			std::shared_lock l(g_mutex);
			if (g_hidden_entries.count(hash.mId))
				return;
		}
		big::g_hooking->get_original<hook_DoDraw3DThumbnail>()(vec_ref, index, param, hash);
	}

	// ─── Code cave for DoDrawShadowCast3D ─────────────────────────────
	//
	// Overwrites 7 bytes at the dispatch's shadow-flag check:
	//   cmp byte [r10+0x2d], 0   (5B)
	//   je  <main_path>          (2B)
	// with:
	//   jmp code_cave            (5B)
	//   nop; nop                 (2B)
	//
	// The cave checks [r10+0x28] against the hidden set.  If hidden it
	// skips to the loop-next target; otherwise it replays the original
	// cmp+je and resumes normal execution.

	static void install_shadow_cast_patch(uintptr_t doDraw3D_addr)
	{
		// All offsets are relative to the DoDraw3D PDB symbol address.
		uintptr_t patch_site      = doDraw3D_addr + 0x148E4; // cmp byte [r10+0x2d], 0
		uintptr_t shadow_continue = doDraw3D_addr + 0x148EB; // shadow param setup
		uintptr_t main_continue   = doDraw3D_addr + 0x148FF; // hash load + main path
		uintptr_t loop_next       = doDraw3D_addr + 0x14AC1; // inc rsi (loop next)

		// Verify expected bytes before patching.
		const uint8_t expected[] = {0x41, 0x80, 0x7A, 0x2D, 0x00, 0x74, 0x14};
		if (memcmp((void*)patch_site, expected, 7) != 0)
		{
			LOG(ERROR) << "draw: shadow patch byte mismatch at "
			           << HEX_TO_UPPER(patch_site) << " — skipping";
			return;
		}

		// Allocate code cave within ±2GB of the patch site (required for rel32 jmp).
		void* cave = nullptr;
		uintptr_t try_addr = patch_site & ~0xFFFFULL;
		for (uintptr_t offset = 0x10000; offset < 0x7FFF0000ULL; offset += 0x10000)
		{
			cave = VirtualAlloc((void*)(try_addr - offset), 4096,
			                    MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (cave) break;
			cave = VirtualAlloc((void*)(try_addr + offset), 4096,
			                    MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (cave) break;
		}
		if (!cave)
		{
			LOG(ERROR) << "draw: VirtualAlloc failed for shadow code cave";
			return;
		}

		uintptr_t cave_addr = (uintptr_t)cave;
		uintptr_t data_base = cave_addr + 0x80; // data section after code

		// Data section (5 pointers, filled at runtime)
		*(uintptr_t*)(data_base + 0x00) = (uintptr_t)&g_any_active;
		*(uintptr_t*)(data_base + 0x08) = (uintptr_t)&check_draw_entry;
		*(uintptr_t*)(data_base + 0x10) = shadow_continue;
		*(uintptr_t*)(data_base + 0x18) = main_continue;
		*(uintptr_t*)(data_base + 0x20) = loop_next;

		// Code section — hand-assembled x86-64
		uint8_t* p = (uint8_t*)cave;
		auto emit = [&](std::initializer_list<uint8_t> bytes) {
			for (auto b : bytes) *p++ = b;
		};
		auto emit_rel32 = [&](int32_t disp) {
			memcpy(p, &disp, 4); p += 4;
		};
		auto cur = [&]() -> size_t { return (size_t)(p - (uint8_t*)cave); };
		auto rip_data = [&](size_t data_off, size_t rest) -> int32_t {
			return (int32_t)((data_base + data_off) - ((uintptr_t)p + rest));
		};

		// Fast path: if nothing is active (no hidden, no remap), skip to original
		emit({0x48, 0x8B, 0x05}); emit_rel32(rip_data(0x00, 4)); // mov rax, [rip+g_any_active]
		emit({0x80, 0x38, 0x00});                                  // cmp byte [rax], 0
		emit({0x74}); size_t je_fast = cur(); emit({0x00});         // je .not_hidden

		// Slow path: call check_draw_entry(ecx=hash, rdx=&out_hash)
		// Returns: eax=0 pass, eax=1 hidden, eax=2 remapped
		emit({0x51, 0x52});                                         // push rcx; push rdx
		emit({0x48, 0x83, 0xEC, 0x30});                             // sub rsp, 0x30 (shadow + local)
		emit({0x41, 0x8B, 0x4A, 0x28});                             // mov ecx, [r10+0x28]
		emit({0x48, 0x8D, 0x54, 0x24, 0x28});                      // lea rdx, [rsp+0x28] (out_hash)
		emit({0x48, 0x8B, 0x05}); emit_rel32(rip_data(0x08, 4));   // mov rax, [rip+check_draw_entry]
		emit({0xFF, 0xD0});                                         // call rax
		emit({0x83, 0xF8, 0x01});                                   // cmp eax, 1
		emit({0x74}); size_t je_skip = cur(); emit({0x00});          // je .skip
		emit({0x83, 0xF8, 0x02});                                   // cmp eax, 2
		emit({0x74}); size_t je_remap = cur(); emit({0x00});         // je .remap
		// eax=0: pass through
		emit({0x48, 0x83, 0xC4, 0x30});                             // add rsp, 0x30
		emit({0x5A, 0x59});                                         // pop rdx; pop rcx
		emit({0xEB}); size_t jmp_not_hidden = cur(); emit({0x00});   // jmp .not_hidden

		// .remap — rewrite [r10+0x28] with remapped hash, then pass through
		size_t remap_off = cur();
		*((uint8_t*)cave + je_remap) = (uint8_t)(remap_off - je_remap - 1);
		emit({0x8B, 0x44, 0x24, 0x28});                             // mov eax, [rsp+0x28] (out_hash)
		emit({0x41, 0x89, 0x42, 0x28});                             // mov [r10+0x28], eax (overwrite!)
		emit({0x48, 0x83, 0xC4, 0x30});                             // add rsp, 0x30
		emit({0x5A, 0x59});                                         // pop rdx; pop rcx
		emit({0xEB}); size_t jmp_not_hidden2 = cur(); emit({0x00});  // jmp .not_hidden

		// .not_hidden — replay original instructions
		size_t not_hidden = cur();
		*((uint8_t*)cave + je_fast) = (uint8_t)(not_hidden - je_fast - 1);
		*((uint8_t*)cave + jmp_not_hidden) = (uint8_t)(not_hidden - jmp_not_hidden - 1);
		*((uint8_t*)cave + jmp_not_hidden2) = (uint8_t)(not_hidden - jmp_not_hidden2 - 1);
		emit({0x41, 0x80, 0x7A, 0x2D, 0x00});                      // cmp byte [r10+0x2d], 0
		emit({0x74}); size_t je_main = cur(); emit({0x00});          // je .main
		emit({0xFF, 0x25}); emit_rel32(rip_data(0x10, 4));          // jmp [shadow_continue]

		// .main — non-shadow path
		size_t main_off = cur();
		*((uint8_t*)cave + je_main) = (uint8_t)(main_off - je_main - 1);
		emit({0xFF, 0x25}); emit_rel32(rip_data(0x18, 4));          // jmp [main_continue]

		// .skip — entry is hidden, advance loop
		size_t skip_off = cur();
		*((uint8_t*)cave + je_skip) = (uint8_t)(skip_off - je_skip - 1);
		emit({0x48, 0x83, 0xC4, 0x30});                             // add rsp, 0x30
		emit({0x5A, 0x59});                                         // pop rdx; pop rcx
		emit({0xFF, 0x25}); emit_rel32(rip_data(0x20, 4));          // jmp [loop_next]

		LOG(INFO) << "draw: shadow cave at " << HEX_TO_UPPER(cave_addr)
		          << " (" << cur() << " bytes)";

		// Overwrite the original 7 bytes with jmp + nops.
		DWORD old_protect;
		VirtualProtect((void*)patch_site, 7, PAGE_EXECUTE_READWRITE, &old_protect);
		int32_t rel = (int32_t)(cave_addr - (patch_site + 5));
		uint8_t patch[] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
		memcpy(patch + 1, &rel, 4);
		memcpy((void*)patch_site, patch, 7);
		VirtualProtect((void*)patch_site, 7, old_protect, &old_protect);
		FlushInstructionCache(GetCurrentProcess(), (void*)patch_site, 7);

		LOG(INFO) << "draw: shadow patch installed at " << HEX_TO_UPPER(patch_site);
	}

	// ─── Lua binding ──────────────────────────────────────────────────

	// Lua API: Function
	// Table: data
	// Name: set_draw_visible
	// Param: entry_name: string: Model entry name (e.g. "HecateBattle_Mesh").
	// Param: visible: boolean: true to show, false to hide.
	// Toggles visibility of a model entry by suppressing its draw calls
	// (DoDraw3D, DoDrawShadow3D, DoDraw3DThumbnail, DoDrawShadowCast3D).
	// Takes effect immediately — no rebuild, no restart, no data mutation.
	//
	// **Example Usage:**
	// ```lua
	// rom.data.set_draw_visible("HecateBattle_Mesh", false)
	// ```
	static void set_draw_visible(const std::string& entry_name, bool visible)
	{
		static auto Lookup = *big::hades2_symbol_to_address["sgg::HashGuid::Lookup"]
		    .as_func<sgg::HashGuid*(sgg::HashGuid*, const char*, size_t)>();

		sgg::HashGuid guid{};
		Lookup(&guid, entry_name.c_str(), entry_name.size());

		if (guid.mId == 0)
		{
			LOG(WARNING) << "draw: hash=0 for '" << entry_name
			             << "' — hash system not ready, skipping";
			return;
		}

		size_t old_size, new_size;
		{
			std::unique_lock l(g_mutex);
			old_size = g_hidden_entries.size();
			if (visible)
				g_hidden_entries.erase(guid.mId);
			else
				g_hidden_entries.insert(guid.mId);
			new_size = g_hidden_entries.size();
		}

		update_active_flag();

		LOG(INFO) << "draw: " << entry_name
		          << (visible ? " SHOW" : " HIDE")
		          << " (hash=" << guid.mId
		          << ", set " << old_size << " -> " << new_size << ")";
	}

	// ─── SEH-protected pointer reads ──────────────────────────────────
	// populate_entry_textures / set_mesh_visible / swap_to_variant walk
	// the game's mModelData hash-bucket structure via raw pointers.  Any
	// step can fault if the layout shifts after a game update; these
	// helpers let callers detect and log a fault instead of crashing.
	// Plain C (no C++ objects) so __try / __except are legal.
	static int __stdcall safe_read_u64(const void* addr, uint64_t* out_val)
	{
		__try { *out_val = *(const uint64_t*)addr; return 1; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}

	static int __stdcall safe_read_u32(const void* addr, uint32_t* out_val)
	{
		__try { *out_val = *(const uint32_t*)addr; return 1; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}

	static int __stdcall safe_read_ptr(const void* addr, void** out_val)
	{
		__try { *out_val = *(void* const*)addr; return 1; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}

	// ─── Registration ─────────────────────────────────────────────────

	void bind(sol::state_view& state, sol::table& lua_ext)
	{
		auto ns = lua_ext["data"].get_or_create<sol::table>();
		ns.set_function("set_draw_visible", set_draw_visible);

		// Lua API: Function
		// Table: data
		// Name: dump_pool_stats
		// Returns: integer — number of per-shader vertex buffers dumped.
		// Walks sgg::gStaticDrawBuffers and logs each shader-effect's
		// vertex pool: total capacity (Buffer.mSize &0xFFFFFFFF at +0x38),
		// cursor (+0x40 of ForgeGeometryBuffers — next-free vertex slot),
		// stride (from gShaderEffects[i].+0x95c).  Also logs the single
		// global gStaticIndexBuffers cursor + size.
		ns.set_function("dump_pool_stats", []() -> int {
			auto draw_buf_vec = big::hades2_symbol_to_address["sgg::gStaticDrawBuffers"];
			auto idx_buf_ptr  = big::hades2_symbol_to_address["sgg::gStaticIndexBuffers"];
			auto idx_off_ptr  = big::hades2_symbol_to_address["sgg::gStaticIndexBufferOffset"];
			auto shader_effects = big::hades2_symbol_to_address["sgg::gShaderEffects"];
			if (!draw_buf_vec || !idx_buf_ptr || !idx_off_ptr || !shader_effects)
			{
				LOG(ERROR) << "dump_pool_stats: symbols missing";
				return 0;
			}
			auto vec = draw_buf_vec.as<uint8_t *>();
			auto vec_begin = *(uint8_t **)(vec + 0x00);
			auto vec_end   = *(uint8_t **)(vec + 0x08);
			size_t count = (vec_end - vec_begin) / 72;
			LOG(INFO) << "=== Static vertex pool stats (" << count << " geo buffers) ===";
			// The shader→geo mapping isn't 1:1 (addShaderEffect calls
			// addStaticVertexBuffers twice with different sizes per effect),
			// so deriving the real per-shader byte stride from the geo
			// index alone is wrong.  Instead, report raw cursor + capacity,
			// plus a rough estimate at the common 40 B character-vertex
			// stride.  Only the raw cursor / capacity should be trusted.
			int logged = 0;
			for (size_t i = 0; i < count && i < 128; ++i)
			{
				uint8_t *geo = vec_begin + i * 72;
				uint8_t *buf = *(uint8_t **)(geo + 0x20);
				if (!buf) continue;
				uint32_t cursor = *(uint32_t *)(geo + 0x40);
				uint64_t msize_raw = *(uint64_t *)(buf + 0x38);
				uint32_t msize = (uint32_t)msize_raw;
				double cap_mb = msize / (1024.0 * 1024.0);
				double est_mb = (uint64_t)cursor * 40 / (1024.0 * 1024.0);
				LOG(INFO) << "  geo[" << i << "]: capacity=" << cap_mb
				          << " MB  cursor=" << cursor << " verts (~"
				          << est_mb << " MB @ 40B/vert)";
				logged++;
			}
			// Index buffer uses fixed 2-byte stride — bounds check is
			// exact, so this % is reliable (unlike the vertex-pool one).
			uint8_t *ibuf = *idx_buf_ptr.as<uint8_t **>();
			uint32_t ioff = *idx_off_ptr.as<uint32_t *>();
			if (ibuf)
			{
				uint64_t imsize_raw = *(uint64_t *)(ibuf + 0x38);
				uint32_t imsize = (uint32_t)imsize_raw;
				uint64_t iused = (uint64_t)ioff * 2;
				double ipct = imsize ? (100.0 * iused / imsize) : 0.0;
				LOG(INFO) << "  index buf: " << (iused / (1024.0 * 1024.0)) << " MB / "
				          << (imsize / (1024.0 * 1024.0)) << " MB (" << ipct << "%)";
			}
			return logged;
		});
		// Lua API: Function
		// Table: data
		// Name: set_mesh_visible
		// Param: entry_name: string: Model entry (e.g. "HecateHub_Mesh").
		// Param: mesh_name: string: Mesh name inside that entry (e.g. "TorusHubMesh").
		// Param: visible: boolean: true to show, false to hide.
		// Returns: boolean — true on success.
		//
		// Finer-grained than set_draw_visible (which hides the whole entry):
		// walks the entry's GrannyMeshData vector, finds the mesh whose
		// mesh-name hash at GMD+0x48 matches `mesh_name`, and flips its
		// texture-name hash at GMD+0x40 between 0 (hide) and the original
		// value (show).
		//
		// Hide path uses DoDraw3D's OWN mesh-type switch: setting
		// GMD+0x4C = 2 makes the main-draw function skip to
		// next-iteration at 0x1401ebd25 — no cmdDrawIndexed is issued,
		// no texture lookup attempted.  This is the same branch the
		// engine takes for its own shadow meshes (they're drawn via
		// DoDrawShadow3D instead, which our accessory meshes don't
		// have a shadow entry in, so they stay hidden everywhere).
		// No DX12 validation errors, no command-list poisoning.
		//
		// Used for instant accessory toggle: mesh_add mods merge their
		// meshes INTO stock entries, so the entry-level draw-gate would
		// hide the body alongside the accessory.  Per-mesh visibility
		// keeps the body on and only suppresses the accessory meshes.
		ns.set_function("set_mesh_visible", [](const std::string& entry,
		                                        const std::string& mesh_name,
		                                        bool visible) -> bool {
			// Saved mesh_type keyed by (entry_hash, mesh_hash, idx).  Index
			// distinguishes multiple GMDs sharing the same name hash
			// (e.g. main+outline+shadow variants under a shared name, or
			// duplicate GLB meshes split by material).  Using raw GMD
			// pointer would be invalidated if the GMD vector ever
			// reallocates — unlikely in Hades II's static-load model but
			// not guaranteed by the engine contract.  std::map so we
			// don't have to write a tuple hasher.
			using SavedKey = std::tuple<uint32_t, uint32_t, size_t>;
			static std::map<SavedKey, uint8_t> g_saved_mesh_type;
			static std::mutex g_saved_mutex;

			static auto Lookup = *big::hades2_symbol_to_address["sgg::HashGuid::Lookup"]
			    .as_func<sgg::HashGuid*(sgg::HashGuid*, const char*, size_t)>();
			auto mdata_addr = big::hades2_symbol_to_address["sgg::Granny3D::mModelData"];
			if (!Lookup || !mdata_addr)
			{
				LOG(ERROR) << "set_mesh_visible: required symbols missing";
				return false;
			}

			sgg::HashGuid entry_guid{};
			Lookup(&entry_guid, entry.c_str(), entry.size());
			if (!entry_guid.mId)
			{
				LOG(WARNING) << "set_mesh_visible: entry hash=0 for '" << entry << "'";
				return false;
			}
			sgg::HashGuid mesh_guid{};
			Lookup(&mesh_guid, mesh_name.c_str(), mesh_name.size());
			if (!mesh_guid.mId)
			{
				LOG(WARNING) << "set_mesh_visible: mesh hash=0 for '" << mesh_name << "'";
				return false;
			}

			uint8_t* mdata = mdata_addr.as<uint8_t*>();
			void* buckets_ptr = nullptr;
			uint64_t bucket_count = 0;
			if (!safe_read_ptr(mdata + 0x08, &buckets_ptr)) return false;
			if (!safe_read_u64(mdata + 0x10, &bucket_count)) return false;
			if (!buckets_ptr || !bucket_count || bucket_count > 0x100000) return false;

			uint32_t h = entry_guid.mId;
			h = ((h >> 16) ^ h) * 0x7feb352d;
			h = ((h >> 15) ^ h) * 0x846ca68b;
			h = (h >> 16) ^ h;
			uint8_t* node = (uint8_t*)((void**)buckets_ptr)[h % bucket_count];
			int walk_guard = 0;
			while (node && walk_guard++ < 32)
			{
				uint32_t id = 0;
				if (!safe_read_u32(node, &id)) return false;
				if (id == entry_guid.mId) break;
				void* nxt = nullptr;
				if (!safe_read_ptr(node + 0xC0, &nxt)) return false;
				node = (uint8_t*)nxt;
			}
			if (!node || walk_guard >= 32)
			{
				LOG(WARNING) << "set_mesh_visible: entry '" << entry << "' not in mModelData";
				return false;
			}

			void* vb_p = nullptr; void* ve_p = nullptr;
			if (!safe_read_ptr(node + 0x10, &vb_p)) return false;
			if (!safe_read_ptr(node + 0x18, &ve_p)) return false;
			uint8_t* vec_begin = (uint8_t*)vb_p;
			uint8_t* vec_end   = (uint8_t*)ve_p;
			size_t mesh_count = (vec_end >= vec_begin) ? (size_t)(vec_end - vec_begin) / 0x50 : 0;
			if (mesh_count == 0 || mesh_count > 128) return false;

			// Sentinel byte used for hidden state (= shadow mesh type,
			// which DoDraw3D skips to next-iteration).
			constexpr uint8_t HIDE_TYPE = 2;
			int matched = 0;
			std::lock_guard lk(g_saved_mutex);
			for (size_t i = 0; i < mesh_count; i++)
			{
				uint8_t* gmd = vec_begin + i * 0x50;
				uint32_t gmd_mesh_hash = 0;
				if (!safe_read_u32(gmd + 0x48, &gmd_mesh_hash)) continue;
				if (gmd_mesh_hash != mesh_guid.mId) continue;

				uint8_t current_type = *(uint8_t*)(gmd + 0x4C);
				SavedKey key{entry_guid.mId, mesh_guid.mId, i};

				if (visible)
				{
					auto it = g_saved_mesh_type.find(key);
					if (it != g_saved_mesh_type.end())
					{
						*(uint8_t*)(gmd + 0x4C) = it->second;
						g_saved_mesh_type.erase(it);
					}
					// else: already visible — no-op
				}
				else
				{
					if (current_type != HIDE_TYPE && !g_saved_mesh_type.count(key))
					{
						g_saved_mesh_type[key] = current_type;
						*(uint8_t*)(gmd + 0x4C) = HIDE_TYPE;
					}
					// else: already hidden — no-op
				}
				matched++;
				// Don't break: continue so main+outline+shadow variants
				// with the same mesh-name hash all get toggled together.
			}
			if (matched == 0)
			{
				LOG(WARNING) << "set_mesh_visible: mesh '" << mesh_name
				             << "' not in entry '" << entry << "'";
				return false;
			}
			LOG(INFO) << "set_mesh_visible: " << entry << "/" << mesh_name
			          << " -> " << (visible ? "show" : "hide")
			          << " (" << matched << " mesh" << (matched > 1 ? "es" : "") << ")";
			return true;
		});

		// NOTE: No hook on LoadAllModelAndAnimationData — a second detour
		// are loaded automatically by the game when add_granny_file exposes
		// them.  The double-hook was causing weapon model corruption.

		// Detour hooks on DoDraw3D, DoDrawShadow3D, DoDraw3DThumbnail.
		{
			auto addr = big::hades2_symbol_to_address["sgg::DrawManager::DoDraw3D"];
			if (addr)
			{
				static auto hook_ = big::hooking::detour_hook_helper::add<hook_DoDraw3D>(
				    "drawDoDraw3D", addr);
				LOG(INFO) << "draw: hooked DoDraw3D";
			}
		}
		{
			auto addr = big::hades2_symbol_to_address["sgg::DrawManager::DoDrawShadow3D"];
			if (addr)
			{
				static auto hook_ = big::hooking::detour_hook_helper::add<hook_DoDrawShadow3D>(
				    "drawDoDrawShadow3D", addr);
				LOG(INFO) << "draw: hooked DoDrawShadow3D";
			}
		}
		{
			auto addr = big::hades2_symbol_to_address["sgg::DrawManager::DoDraw3DThumbnail"];
			if (addr)
			{
				static auto hook_ = big::hooking::detour_hook_helper::add<hook_DoDraw3DThumbnail>(
				    "drawDoDraw3DThumbnail", addr);
				LOG(INFO) << "draw: hooked DoDraw3DThumbnail";
			}
		}

		// Manual code cave for DoDrawShadowCast3D.
		{
			auto addr = big::hades2_symbol_to_address["sgg::DrawManager::DoDraw3D"];
			if (addr)
				install_shadow_cast_patch(addr.as<uintptr_t>());
		}
	}

} // namespace lua::hades::draw
