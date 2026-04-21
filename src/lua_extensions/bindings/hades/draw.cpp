#include "draw.hpp"

#include "hades_ida.hpp"

#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <lua/lua_manager.hpp>
#include <atomic>
#include <map>
#include <memory/gm_address.hpp>
#include <mutex>
#include <shared_mutex>
#include <string/string.hpp>
#include <tuple>
#include <unordered_map>

namespace lua::hades::draw
{
	// ─── Game structs ─────────────────────────────────────────────────
	//
	// Only the fields we touch are named; unknown regions are
	// `char pad[]` so `sizeof` and offsetof match the engine's layout.
	// Access is always through safe_read_* helpers (see below) because
	// these pointers can be half-constructed during load and a wrong
	// guess here should log a fault, not crash.

	struct ModelDataNode;

	struct ModelDataHashTable
	{
		char pad_00[0x08];
		ModelDataNode** buckets; // +0x08
		uint64_t bucket_count;   // +0x10
	};
	static_assert(offsetof(ModelDataHashTable, buckets) == 0x08);
	static_assert(offsetof(ModelDataHashTable, bucket_count) == 0x10);

	struct GrannyMeshData
	{
		char pad_00[0x40];
		uint32_t texture_name_hash; // +0x40: feeds GetTexture
		uint32_t texture_handle;    // +0x44: resolved bindless handle
		uint32_t mesh_name_hash;    // +0x48: sgg::HashGuid of the mesh's name
		uint8_t mesh_type;          // +0x4C: 0 main, 1 outline, 2 shadow/hidden
		char pad_4D[3];
	};
	static_assert(offsetof(GrannyMeshData, texture_name_hash) == 0x40);
	static_assert(offsetof(GrannyMeshData, texture_handle) == 0x44);
	static_assert(offsetof(GrannyMeshData, mesh_name_hash) == 0x48);
	static_assert(offsetof(GrannyMeshData, mesh_type) == 0x4C);
	static_assert(sizeof(GrannyMeshData) == 0x50);

	struct ModelDataNode
	{
		uint32_t id;                       // +0x00: HashGuid.mId of the entry
		char pad_04[0x0C];
		GrannyMeshData* mesh_vec_begin;    // +0x10: eastl::vector<GrannyMeshData>::mpBegin
		GrannyMeshData* mesh_vec_end;      // +0x18: ::mpEnd
		char pad_20[0xA0];
		ModelDataNode* next;               // +0xC0: EASTL hashtable chain
	};
	static_assert(offsetof(ModelDataNode, id) == 0x00);
	static_assert(offsetof(ModelDataNode, mesh_vec_begin) == 0x10);
	static_assert(offsetof(ModelDataNode, mesh_vec_end) == 0x18);
	static_assert(offsetof(ModelDataNode, next) == 0xC0);

	// ForgeRenderer-side types used only by draw_dump_pool_stats.
	struct ForgeBuffer
	{
		char pad_00[0x38];
		uint64_t size_raw; // +0x38: low dword = byte size, high dword = flags
	};
	static_assert(offsetof(ForgeBuffer, size_raw) == 0x38);

	struct ForgeGeometryBuffers
	{
		char pad_00[0x20];
		ForgeBuffer* buffer;     // +0x20
		char pad_28[0x18];
		uint32_t vertex_cursor;  // +0x40: next free vertex slot
		char pad_44[0x04];
	};
	static_assert(offsetof(ForgeGeometryBuffers, buffer) == 0x20);
	static_assert(offsetof(ForgeGeometryBuffers, vertex_cursor) == 0x40);
	static_assert(sizeof(ForgeGeometryBuffers) == 72);

	// EASTL vector header (first 16 bytes are mpBegin / mpEnd; we don't
	// touch mpCapacityEnd).  Templatized so we can reuse it for both
	// eastl::vector<ForgeGeometryBuffers> and any future consumer.
	template <class T>
	struct EastlVector
	{
		T* mpBegin;   // +0x00
		T* mpEnd;     // +0x08
	};

	// Mixing constants pulled from the game's integer-key hashtable
	// (multiply-xor-shift finalizer).  We have to match them exactly to
	// reproduce the same bucket index the game assigns to a given entry
	// hash: otherwise our bucket walk finds the wrong chain.
	constexpr uint32_t kEastlHashMix1 = 0x7feb352d;
	constexpr uint32_t kEastlHashMix2 = 0x846ca68b;

	// Sanity bounds: a live mModelData never exceeds these.
	constexpr uint64_t kMaxBucketCount  = 0x100000;
	constexpr int      kBucketWalkGuard = 32;
	constexpr size_t   kMaxMeshesPerEntry = 128;

	// Sentinel mesh_type used to hide a single mesh inside an entry:
	// DoDraw3D's own mesh-type switch skips meshes with this value.
	constexpr uint8_t kMeshTypeHidden = 2;

	// ─── SEH-protected pointer reads ──────────────────────────────────
	// The mModelData walk touches pointers that can be null or
	// half-initialized during load/unload.

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

	// ─── Hashtable walker (shared between bindings) ───────────────────
	//
	// Returns the ModelDataNode for `entry_hash`, or nullptr if not
	// found / any read faults.  Every binding that reads per-mesh state
	// uses this: before extraction, each one inlined the same EASTL
	// mix + bucket walk.

	static ModelDataNode* find_model_data_node(uint32_t entry_hash)
	{
		auto mdata_addr = big::hades2_symbol_to_address["sgg::Granny3D::mModelData"];
		if (!mdata_addr) return nullptr;

		auto* table = mdata_addr.as<const ModelDataHashTable*>();

		ModelDataNode** buckets = nullptr;
		uint64_t bucket_count = 0;
		if (!safe_read_ptr(&table->buckets, (void**)&buckets)) return nullptr;
		if (!safe_read_u64(&table->bucket_count, &bucket_count)) return nullptr;
		if (!buckets || !bucket_count || bucket_count > kMaxBucketCount) return nullptr;

		uint32_t h = entry_hash;
		h = ((h >> 16) ^ h) * kEastlHashMix1;
		h = ((h >> 15) ^ h) * kEastlHashMix2;
		h = (h >> 16) ^ h;

		ModelDataNode* node = nullptr;
		if (!safe_read_ptr(&buckets[h % bucket_count], (void**)&node)) return nullptr;

		for (int guard = 0; node && guard < kBucketWalkGuard; ++guard)
		{
			uint32_t id = 0;
			if (!safe_read_u32(&node->id, &id)) return nullptr;
			if (id == entry_hash) return node;
			ModelDataNode* nxt = nullptr;
			if (!safe_read_ptr(&node->next, (void**)&nxt)) return nullptr;
			node = nxt;
		}
		return nullptr;
	}

	// Resolve the node's GrannyMeshData range.  Returns false on any
	// fault or if the range is empty / implausibly large.
	static bool get_entry_meshes(const ModelDataNode* node,
	                             GrannyMeshData** out_begin,
	                             size_t* out_count)
	{
		GrannyMeshData* vb = nullptr;
		GrannyMeshData* ve = nullptr;
		if (!safe_read_ptr(&node->mesh_vec_begin, (void**)&vb)) return false;
		if (!safe_read_ptr(&node->mesh_vec_end, (void**)&ve)) return false;
		if (!vb || !ve || ve < vb) return false;
		size_t count = (size_t)((uintptr_t)ve - (uintptr_t)vb) / sizeof(GrannyMeshData);
		if (count == 0 || count > kMaxMeshesPerEntry) return false;
		*out_begin = vb;
		*out_count = count;
		return true;
	}

	// ─── State ─────────────────────────────────────────────────────────

	static std::shared_mutex g_mutex;
	static std::unordered_set<unsigned int> g_hidden_entries;
	static std::unordered_map<unsigned int, unsigned int> g_remap; // original → variant

	// Fast-path flag for the code cave: avoids the function-call overhead
	// on every draw entry when nothing is active (hidden or remapped).
	static volatile uint8_t g_any_active = 0;

	static void update_active_flag()
	{
		g_any_active = (g_hidden_entries.empty() && g_remap.empty()) ? 0 : 1;
	}

	// Return values for check_draw_entry.  Numeric layout is load-bearing:
	// the code cave compares the returned eax against the raw integers, so
	// the enum stays `: int` and the values must not be reordered.
	enum class DrawEntryDecision : int
	{
		PassThrough = 0,
		Hidden      = 1,
		Remapped    = 2,
	};

	// Called from the code cave via function pointer.
	static DrawEntryDecision check_draw_entry(uint32_t hash, uint32_t* out_hash)
	{
		std::shared_lock l(g_mutex);
		auto it = g_remap.find(hash);
		if (it != g_remap.end())
		{
			*out_hash = it->second;
			return DrawEntryDecision::Remapped;
		}
		if (g_hidden_entries.count(hash))
			return DrawEntryDecision::Hidden;
		return DrawEntryDecision::PassThrough;
	}

	// ─── Detour hooks (DoDraw3D, DoDrawShadow3D, DoDraw3DThumbnail) ──
	// Each hook inlines its own remap + hidden-set check.  Main-pass
	// DoDraw3D participates in both; shadow and thumbnail paths honour
	// the hidden-set for visibility-gate parity but skip the remap:
	// DoDrawShadowCast3D has a different signature (no HashGuid
	// parameter), so its remap would need a code-cave approach not
	// included here.

	static void hook_DoDraw3D(void* vec_ref, unsigned int index, int param, sgg::HashGuid hash)
	{
		{
			std::shared_lock l(g_mutex);
			auto it = g_remap.find(hash.mId);
			if (it != g_remap.end())
				hash.mId = it->second;
			if (g_hidden_entries.count(hash.mId))
				return;
		}
		big::g_hooking->get_original<hook_DoDraw3D>()(vec_ref, index, param, hash);
	}

	// Shadow + thumbnail paths honour the hidden-set but do NOT
	// participate in hash remap.  Reason: DoDrawShadowCast3D has a
	// different signature (no HashGuid param) so its remap would need
	// a code-cave approach we don't ship here.  Remapping the other
	// shadow paths while ShadowCast stays stock leaves the engine with
	// inconsistent per-entry state (main=variant, shadow_cast=stock,
	// shadow3D=variant) that appears to wedge the render thread.
	// Keeping shadow/thumbnail on stock is the safe subset: the main
	// DoDraw3D swap carries the visual change on its own.
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
			           << HEX_TO_UPPER(patch_site) << ": skipping";
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

		// Code section: hand-assembled x86-64
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

		// .remap: rewrite [r10+0x28] with remapped hash, then pass through
		size_t remap_off = cur();
		*((uint8_t*)cave + je_remap) = (uint8_t)(remap_off - je_remap - 1);
		emit({0x8B, 0x44, 0x24, 0x28});                             // mov eax, [rsp+0x28] (out_hash)
		emit({0x41, 0x89, 0x42, 0x28});                             // mov [r10+0x28], eax (overwrite!)
		emit({0x48, 0x83, 0xC4, 0x30});                             // add rsp, 0x30
		emit({0x5A, 0x59});                                         // pop rdx; pop rcx
		emit({0xEB}); size_t jmp_not_hidden2 = cur(); emit({0x00});  // jmp .not_hidden

		// .not_hidden: replay original instructions
		size_t not_hidden = cur();
		*((uint8_t*)cave + je_fast) = (uint8_t)(not_hidden - je_fast - 1);
		*((uint8_t*)cave + jmp_not_hidden) = (uint8_t)(not_hidden - jmp_not_hidden - 1);
		*((uint8_t*)cave + jmp_not_hidden2) = (uint8_t)(not_hidden - jmp_not_hidden2 - 1);
		emit({0x41, 0x80, 0x7A, 0x2D, 0x00});                      // cmp byte [r10+0x2d], 0
		emit({0x74}); size_t je_main = cur(); emit({0x00});          // je .main
		emit({0xFF, 0x25}); emit_rel32(rip_data(0x10, 4));          // jmp [shadow_continue]

		// .main: non-shadow path
		size_t main_off = cur();
		*((uint8_t*)cave + je_main) = (uint8_t)(main_off - je_main - 1);
		emit({0xFF, 0x25}); emit_rel32(rip_data(0x18, 4));          // jmp [main_continue]

		// .skip: entry is hidden, advance loop
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
	// Name: draw_set_visible
	// Param: entry_name: string: Model entry name (e.g. "HecateBattle_Mesh").
	// Param: visible: boolean: true to show, false to hide.
	// Toggles visibility of a model entry.  Takes effect immediately.
	//
	// **Example Usage:**
	// ```lua
	// rom.data.draw_set_visible("HecateBattle_Mesh", false)
	// ```
	static void draw_set_visible(const std::string& entry_name, bool visible)
	{
		static auto Lookup = *big::hades2_symbol_to_address["sgg::HashGuid::Lookup"]
		    .as_func<sgg::HashGuid*(sgg::HashGuid*, const char*, size_t)>();

		sgg::HashGuid guid{};
		Lookup(&guid, entry_name.c_str(), entry_name.size());

		if (guid.mId == 0)
		{
			// HashGuid::Lookup returns 0 either because the string interner
			// isn't populated yet (pre-first-scene) or because the caller
			// passed an entry name the game has never registered (typo,
			// mod not loaded).  Either way there's no entry to toggle, so
			// this call is a no-op; try again after the first scene loads
			// or double-check the name.
			LOG(WARNING) << "draw_set_visible: '" << entry_name
			             << "': no HashGuid found (engine hash table not yet "
			                "populated, or entry name isn't registered); skipping";
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

	// ─── Registration ─────────────────────────────────────────────────

	void bind(sol::state_view& state, sol::table& lua_ext)
	{
		auto ns = lua_ext["data"].get_or_create<sol::table>();
		ns.set_function("draw_set_visible", draw_set_visible);

		// Lua API: Function
		// Table: data
		// Name: draw_dump_pool_stats
		// Returns: number: number of per-shader vertex buffers dumped.
		// Logs vertex-pool and index-pool capacity and cursor usage.
		// Diagnostic only: useful when tuning the pool size config
		// against a mod load-out.
		ns.set_function("draw_dump_pool_stats", []() -> int {
			constexpr size_t kCharacterVertexStride = 40;
			constexpr size_t kIndexByteStride       = 2;
			constexpr size_t kMaxGeoBuffersLogged   = 128;
			constexpr double kBytesPerMiB           = 1024.0 * 1024.0;

			auto draw_buf_vec   = big::hades2_symbol_to_address["sgg::gStaticDrawBuffers"];
			auto idx_buf_ptr    = big::hades2_symbol_to_address["sgg::gStaticIndexBuffers"];
			auto idx_off_ptr    = big::hades2_symbol_to_address["sgg::gStaticIndexBufferOffset"];
			auto shader_effects = big::hades2_symbol_to_address["sgg::gShaderEffects"];
			if (!draw_buf_vec || !idx_buf_ptr || !idx_off_ptr || !shader_effects)
			{
				LOG(ERROR) << "draw_dump_pool_stats: symbols missing";
				return 0;
			}

			const auto* vec = draw_buf_vec.as<const EastlVector<ForgeGeometryBuffers>*>();
			size_t count = (size_t)(vec->mpEnd - vec->mpBegin);
			LOG(INFO) << "=== Static vertex pool stats (" << count << " geo buffers) ===";

			// The shader→geo mapping isn't 1:1 (addShaderEffect calls
			// addStaticVertexBuffers twice with different sizes per
			// effect), so we can't derive the real per-shader byte
			// stride.  Log raw cursor + capacity and a rough estimate
			// at the 40-byte character-vertex stride.
			int logged = 0;
			for (size_t i = 0; i < count && i < kMaxGeoBuffersLogged; ++i)
			{
				const ForgeGeometryBuffers& geo = vec->mpBegin[i];
				if (!geo.buffer) continue;
				uint32_t cursor = geo.vertex_cursor;
				uint32_t msize  = (uint32_t)geo.buffer->size_raw; // low dword = bytes
				double cap_mb = msize / kBytesPerMiB;
				double est_mb = (uint64_t)cursor * kCharacterVertexStride / kBytesPerMiB;
				LOG(INFO) << "  geo[" << i << "]: capacity=" << cap_mb
				          << " MB  cursor=" << cursor << " verts (~"
				          << est_mb << " MB @ " << kCharacterVertexStride << "B/vert)";
				logged++;
			}

			auto* ibuf = *idx_buf_ptr.as<ForgeBuffer* const*>();
			uint32_t ioff = *idx_off_ptr.as<uint32_t *>();
			if (ibuf)
			{
				uint32_t imsize = (uint32_t)ibuf->size_raw;
				uint64_t iused  = (uint64_t)ioff * kIndexByteStride;
				double ipct = imsize ? (100.0 * iused / imsize) : 0.0;
				LOG(INFO) << "  index buf: " << (iused / kBytesPerMiB) << " MB / "
				          << (imsize / kBytesPerMiB) << " MB (" << ipct << "%)";
			}
			return logged;
		});

		// Lua API: Function
		// Table: data
		// Name: draw_populate_entry_textures
		// Param: entry_name: string
		// Returns: integer: number of textures populated.
		// Resolves the entry's mesh textures up-front.  Useful for
		// entries that aren't in the active scene (loaded-but-not-drawn
		// variants) so their first drawn frame doesn't render white.
		ns.set_function("draw_populate_entry_textures", [](const std::string& entry) -> int {
			static auto Lookup = *big::hades2_symbol_to_address["sgg::HashGuid::Lookup"]
			    .as_func<sgg::HashGuid*(sgg::HashGuid*, const char*, size_t)>();
			static auto GetTexture = big::hades2_symbol_to_address["sgg::GameAssetManager::GetTexture"]
			    .as_func<void(void*, uint32_t*, uint32_t)>();
			if (!Lookup || !GetTexture)
			{
				LOG(ERROR) << "draw_populate_entry_textures: required symbols missing";
				return 0;
			}

			sgg::HashGuid guid{};
			Lookup(&guid, entry.c_str(), entry.size());
			if (!guid.mId)
			{
				LOG(WARNING) << "draw_populate_entry_textures: hash=0 for '" << entry << "'";
				return 0;
			}

			ModelDataNode* node = find_model_data_node(guid.mId);
			if (!node)
			{
				LOG(WARNING) << "draw_populate_entry_textures: entry '" << entry << "' not found";
				return 0;
			}

			GrannyMeshData* meshes = nullptr;
			size_t mesh_count = 0;
			if (!get_entry_meshes(node, &meshes, &mesh_count)) return 0;

			int populated = 0;
			for (size_t i = 0; i < mesh_count; i++)
			{
				GrannyMeshData& gmd = meshes[i];

				// PrepDraw only fills the texture handle for main meshes;
				// outlines and shadows resolve through other paths.
				if (gmd.mesh_type != 0) continue;
				if (gmd.texture_name_hash == 0) continue;

				uint32_t old_handle = gmd.texture_handle;
				uint32_t new_handle = 0;
				GetTexture(nullptr, &new_handle, gmd.texture_name_hash);
				gmd.texture_handle = new_handle;

				LOG(INFO) << "draw_populate_entry_textures: '" << entry << "' mesh[" << i << "]"
				          << " name_hash=" << gmd.texture_name_hash
				          << " handle 0x" << std::hex << old_handle
				          << " -> 0x" << new_handle << std::dec;
				populated++;
			}
			LOG(INFO) << "draw_populate_entry_textures: '" << entry
			          << "' populated " << populated << " handle(s)";
			return populated;
		});

		// Lua API: Function
		// Table: data
		// Name: draw_set_mesh_visible
		// Param: entry_name: string: Model entry (e.g. "HecateHub_Mesh").
		// Param: mesh_name: string: Mesh name inside that entry (e.g. "TorusHubMesh").
		// Param: visible: boolean: true to show, false to hide.
		// Returns: boolean: true on success.
		// Finer-grained than draw_set_visible: toggles a single named
		// mesh inside an entry instead of the whole entry.
		ns.set_function("draw_set_mesh_visible", [](const std::string& entry,
		                                             const std::string& mesh_name,
		                                             bool visible) -> bool {
			// Saved original mesh_type, keyed by (entry_hash, mesh_hash,
			// idx).  The tuple key survives GMD vector reallocations:
			// unlikely in Hades II's static-load model, but free to guard.
			using SavedKey = std::tuple<uint32_t, uint32_t, size_t>;
			static std::map<SavedKey, uint8_t> g_saved_mesh_type;
			static std::mutex g_saved_mutex;

			static auto Lookup = *big::hades2_symbol_to_address["sgg::HashGuid::Lookup"]
			    .as_func<sgg::HashGuid*(sgg::HashGuid*, const char*, size_t)>();
			if (!Lookup)
			{
				LOG(ERROR) << "draw_set_mesh_visible: Lookup missing";
				return false;
			}

			sgg::HashGuid entry_guid{};
			Lookup(&entry_guid, entry.c_str(), entry.size());
			if (!entry_guid.mId)
			{
				LOG(WARNING) << "draw_set_mesh_visible: entry hash=0 for '" << entry << "'";
				return false;
			}
			sgg::HashGuid mesh_guid{};
			Lookup(&mesh_guid, mesh_name.c_str(), mesh_name.size());
			if (!mesh_guid.mId)
			{
				LOG(WARNING) << "draw_set_mesh_visible: mesh hash=0 for '" << mesh_name << "'";
				return false;
			}

			ModelDataNode* node = find_model_data_node(entry_guid.mId);
			if (!node)
			{
				LOG(WARNING) << "draw_set_mesh_visible: entry '" << entry << "' not in mModelData";
				return false;
			}

			GrannyMeshData* meshes = nullptr;
			size_t mesh_count = 0;
			if (!get_entry_meshes(node, &meshes, &mesh_count)) return false;

			int matched = 0;
			std::lock_guard lk(g_saved_mutex);
			for (size_t i = 0; i < mesh_count; i++)
			{
				GrannyMeshData& gmd = meshes[i];
				uint32_t gmd_mesh_hash = 0;
				if (!safe_read_u32(&gmd.mesh_name_hash, &gmd_mesh_hash)) continue;
				if (gmd_mesh_hash != mesh_guid.mId) continue;

				uint8_t current_type = gmd.mesh_type;
				SavedKey key{entry_guid.mId, mesh_guid.mId, i};

				if (visible)
				{
					auto it = g_saved_mesh_type.find(key);
					if (it != g_saved_mesh_type.end())
					{
						gmd.mesh_type = it->second;
						g_saved_mesh_type.erase(it);
					}
					// else: already visible: no-op
				}
				else
				{
					if (current_type != kMeshTypeHidden && !g_saved_mesh_type.count(key))
					{
						g_saved_mesh_type[key] = current_type;
						gmd.mesh_type = kMeshTypeHidden;
					}
					// else: already hidden: no-op
				}
				matched++;
				// Don't break: toggle main+outline+shadow variants sharing
				// the same mesh-name hash together.
			}
			if (matched == 0)
			{
				LOG(WARNING) << "draw_set_mesh_visible: mesh '" << mesh_name
				             << "' not in entry '" << entry << "'";
				return false;
			}
			LOG(INFO) << "draw_set_mesh_visible: " << entry << "/" << mesh_name
			          << " -> " << (visible ? "show" : "hide")
			          << " (" << matched << " mesh" << (matched > 1 ? "es" : "") << ")";
			return true;
		});

		// Lua API: Function
		// Table: data
		// Name: draw_swap_to_variant
		// Param: stock_entry: string: Stock entry name (e.g. "HecateHub_Mesh").
		// Param: variant_entry: string: Variant entry name loaded in mModelData.
		// Returns: boolean: true on success.
		// Redirects draw calls for `stock_entry` to `variant_entry`.
		// Use draw_populate_entry_textures on the variant first (ideally
		// from a safe window like the first ImGui frame): this call is a
		// cheap map write and is not safe to call GetTexture from.
		//
		// **Example Usage:**
		// ```lua
		// rom.data.draw_populate_entry_textures("HecateHub_Variant_Mesh")
		// rom.data.draw_swap_to_variant("HecateHub_Mesh", "HecateHub_Variant_Mesh")
		// -- later:
		// rom.data.draw_restore_stock("HecateHub_Mesh")
		// ```
		ns.set_function("draw_swap_to_variant", [](const std::string& stock_entry,
		                                            const std::string& variant_entry) -> bool {
			static auto Lookup = *big::hades2_symbol_to_address["sgg::HashGuid::Lookup"]
			    .as_func<sgg::HashGuid*(sgg::HashGuid*, const char*, size_t)>();
			if (!Lookup) { LOG(ERROR) << "draw_swap_to_variant: Lookup missing"; return false; }

			sgg::HashGuid variant_guid{};
			Lookup(&variant_guid, variant_entry.c_str(), variant_entry.size());
			if (!variant_guid.mId)
			{
				LOG(WARNING) << "draw_swap_to_variant: variant hash=0 ('" << variant_entry << "')";
				return false;
			}
			sgg::HashGuid stock_guid{};
			Lookup(&stock_guid, stock_entry.c_str(), stock_entry.size());
			if (!stock_guid.mId)
			{
				LOG(WARNING) << "draw_swap_to_variant: stock hash=0 ('" << stock_entry << "')";
				return false;
			}

			{
				std::unique_lock l(g_mutex);
				g_remap[stock_guid.mId] = variant_guid.mId;
			}
			update_active_flag();

			LOG(INFO) << "draw_swap_to_variant: '" << stock_entry << "' -> '" << variant_entry << "'";
			return true;
		});

		// Lua API: Function
		// Table: data
		// Name: draw_restore_stock
		// Param: stock_entry: string: Stock entry name to revert to.
		// Returns: boolean: true on success.
		// Clears any active hash remap for the given stock entry.
		ns.set_function("draw_restore_stock", [](const std::string& stock_entry) -> bool {
			static auto Lookup = *big::hades2_symbol_to_address["sgg::HashGuid::Lookup"]
			    .as_func<sgg::HashGuid*(sgg::HashGuid*, const char*, size_t)>();
			sgg::HashGuid stock{};
			Lookup(&stock, stock_entry.c_str(), stock_entry.size());
			if (!stock.mId)
			{
				LOG(WARNING) << "draw_restore_stock: hash=0 ('" << stock_entry << "')";
				return false;
			}
			{
				std::unique_lock l(g_mutex);
				g_remap.erase(stock.mId);
			}
			update_active_flag();
			LOG(INFO) << "draw_restore_stock: '" << stock_entry << "' remap cleared";
			return true;
		});

		// NOTE: No hook on LoadAllModelAndAnimationData: a second detour
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
