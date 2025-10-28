#include "asi_loader/asi_loader.hpp"
#include "capstone/capstone.h"
#include "config/config.hpp"
#include "dll_proxy/dll_proxy.hpp"
#include "gui/gui.hpp"
#include "gui/renderer.hpp"
#include "hades2/hooks.hpp"
#include "hooks/hooking.hpp"
#include "logger/exception_handler.hpp"
#include "lua/lua_manager.hpp"
#include "memory/all.hpp"
#include "memory/byte_patch.hpp"
#include "memory/byte_patch_manager.hpp"
#include "paths/paths.hpp"
#include "safetyhook.hpp"
#include "threads/thread_pool.hpp"
#include "threads/util.hpp"
#include "version.hpp"

#include <DbgHelp.h>
#include <hades2/pdb_symbol_map.hpp>
#include <lua_extensions/bindings/hades/hades_ida.hpp>
#include <lua_extensions/bindings/hades/inputs.hpp>
#include <lua_extensions/bindings/paths_ext.hpp>
#include <lua_extensions/bindings/tolk/tolk.hpp>
#include <memory/gm_address.hpp>
#include <new>
#include <PDB.h>
#include <PDB_DBIStream.h>
#include <PDB_ImageSectionStream.h>
#include <PDB_InfoStream.h>
#include <PDB_RawFile.h>
#include <PDB_TPIStream.h>

//#include "debug/debug.hpp"

std::vector<SafetyHookMid> g_sgg_sBuffer_mid_hooks;
// Mult by 8 the original limit.
constexpr uint32_t extended_sgg_sBuffer_size = sizeof(char) * 8'388'608 * 8;
char *extended_sgg_sBuffer;

void hook_mid_sgg_sBuffer_rax(SafetyHookContext &ctx)
{
	ctx.rax = (uintptr_t)extended_sgg_sBuffer;

	// Skip the original lea instruction.
	ctx.rip += 7;
}

void hook_mid_sgg_sBuffer_rbx(SafetyHookContext &ctx)
{
	ctx.rbx = (uintptr_t)extended_sgg_sBuffer;

	// Skip the original lea instruction.
	ctx.rip += 7;
}

void hook_mid_sgg_sBuffer_rdi(SafetyHookContext &ctx)
{
	ctx.rdi = (uintptr_t)extended_sgg_sBuffer;

	// Skip the original lea instruction.
	ctx.rip += 7;
}

bool patch_lea_sgg_sBuffer_usage(cs_insn *insn, uint64_t target_addr, const uintptr_t debug_start_offset, const uintptr_t debug_end_offset, const uintptr_t instruction_address, const uintptr_t instruction_address_offset)
{
	// Only support lea for now.
	if (insn->id != X86_INS_LEA)
	{
		return false;
	}

	if (instruction_address_offset >= debug_start_offset && instruction_address_offset <= debug_end_offset)
	{
		//LOG(INFO) << insn->mnemonic << " " << insn->op_str << " at " << HEX_TO_UPPER(instruction_address_offset);
	}

	for (size_t i = 0; i < insn->detail->x86.op_count; i++)
	{
		cs_x86_op op = insn->detail->x86.operands[i];
		if (op.type == X86_OP_MEM)
		{
			uint64_t full_addr = op.mem.disp;

			// RIP-relative addressing (LEA, MOV, CMP)
			if (op.mem.base == X86_REG_RIP)
			{
				full_addr = instruction_address + insn->size + op.mem.disp;
			}

			if (full_addr == target_addr)
			{
				LOG(DEBUG) << "Found sgg:sBuffer usage at " << HEX_TO_UPPER(instruction_address) << "(" << HEX_TO_UPPER(instruction_address_offset) << "). Instruction: " << insn->mnemonic << " " << insn->op_str;

				if (insn->detail->x86.operands[0].reg == X86_REG_RBX)
				{
					LOG(DEBUG) << "lea rbx patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sBuffer_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sBuffer_rbx));
				}
				else if (insn->detail->x86.operands[0].reg == X86_REG_RAX)
				{
					LOG(DEBUG) << "lea rax patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sBuffer_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sBuffer_rax));
				}
				else if (insn->detail->x86.operands[0].reg == X86_REG_RDI)
				{
					LOG(DEBUG) << "lea rdi patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sBuffer_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sBuffer_rdi));
				}
				else 
				{
					LOG(ERROR) << "unhandled lea patch for " << HEX_TO_UPPER(instruction_address);
				}

				return true;
			}
		}
	}
	return false;
}

bool extend_sgg_sBufferLen_max_size()
{
	// sgg::HashGuid::StringIntern(const char *s, unsigned __int64 length)

	// .text:000000014019ECF9 8B 3D 69 C5 9C 03		mov     edi, cs:sgg__sBufferLen
	// .text:000000014019ECFF 8D 47 01				lea     eax, [rdi+1]
	// .text:000000014019ED02 41 03 C7				add     eax, r15d
	// .text:000000014019ED05 3D 00 00 80 00		cmp     eax, 0C00000h <---- max_size
	static auto sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr = gmAddress::scan("41 03 C7 3D 00 00 C0 00");
	if (sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr)
	{
		const auto sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size =
		    sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr.offset(4).as<uint32_t *>();

		memory::byte_patch::make(sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size, extended_sgg_sBuffer_size)->apply();

		static auto sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr2 =
		    gmAddress::scan("81 FB 00 00 C0 00");
		if (sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr2)
		{
			const auto sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size2 =
			    sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr2.offset(2).as<uint32_t *>();

			memory::byte_patch::make(sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size2, extended_sgg_sBuffer_size)->apply();

			return true;
		}
		else
		{
			LOG(ERROR) << "sgg::HashGuid::StringIntern max_size 2 not found.";
		}
	}
	else
	{
		LOG(ERROR) << "sgg::HashGuid::StringIntern max_size not found.";
	}

	return false;
}

void extend_sgg_Sbuffer()
{
	extended_sgg_sBuffer = (char *)_aligned_malloc(extended_sgg_sBuffer_size, 16);

	if (!extend_sgg_sBufferLen_max_size())
	{
		LOG(WARNING) << "sgg::sBufferLen <= max_size not found, not extending its size.";
		return;
	}

	memory::module game_module{"Hades2.exe"};
	const auto sgg_sBuffer = big::hades2_symbol_to_address["sgg::sBuffer"];
	if (!sgg_sBuffer)
	{
		LOG(ERROR) << "sgg::sBuffer not found, not extending its size.";
		return;
	}

	csh handle;
	if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle) != CS_ERR_OK)
	{
		LOG(ERROR) << "Failed to initialize Capstone. Can't extend sgg::sBuffer";
		return;
	}

	cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
	cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);

	// allocate memory cache for 1 instruction, to be used by cs_disasm_iter later.
	cs_insn *insn = cs_malloc(handle);

	const auto dosHeader = game_module.begin().as<IMAGE_DOS_HEADER *>();
	const auto ntHeader  = game_module.begin().add(dosHeader->e_lfanew).as<IMAGE_NT_HEADERS *>();

	memory::range m_text_section(0, 0);

	// Locate .text section
	const auto section = IMAGE_FIRST_SECTION(ntHeader);
	for (WORD i = 0; i < ntHeader->FileHeader.NumberOfSections; i++)
	{
		const IMAGE_SECTION_HEADER &hdr = section[i];
		if (memcmp(hdr.Name, ".text", 5) == 0)
		{
			m_text_section = memory::range{game_module.begin().add(hdr.VirtualAddress), static_cast<size_t>(hdr.Misc.VirtualSize)};
			LOG(INFO) << "Found text section start at " << HEX_TO_UPPER_OFFSET(m_text_section.begin().as<uintptr_t>()) << ". End at "
			          << HEX_TO_UPPER_OFFSET(m_text_section.end().as<uintptr_t>());
			break;
		}
	}

	const uint8_t *code = m_text_section.begin().as<const uint8_t *>();
	size_t code_size    = m_text_section.size(); // size of @code buffer above
	uint64_t address    = m_text_section.begin().as<uint64_t>();

	const auto module_base_address = (uintptr_t)GetModuleHandleA(0);

	while (true)
	{
		if ((uintptr_t)code >= m_text_section.end().as<uintptr_t>())
		{
			break;
		}

		const uintptr_t instruction_address        = (uintptr_t)code;
		const uintptr_t instruction_address_offset = instruction_address - module_base_address;
		if (cs_disasm_iter(handle, &code, &code_size, &address, insn))
		{
			patch_lea_sgg_sBuffer_usage(insn, sgg_sBuffer.as<uintptr_t>(), 0x19'E9'DC, 0x19'EB'49, instruction_address, instruction_address_offset);
		}
	}

	// release the cache memory when done
	cs_free(insn, 1);
}

void *operator new[](size_t size)
{
	void *ptr = _aligned_malloc(size, 16);
	assert(ptr);
	return ptr;
}

// Used by EASTL.
void *operator new[](size_t size, const char * /* name */, int /* flags */, unsigned /* debug_flags */, const char * /* file */, int /* line */
)
{
	void *ptr = _aligned_malloc(size, 16);
	assert(ptr);
	return ptr;
}

// Used by EASTL.
void *operator new[](size_t size, size_t alignment, size_t alignment_offset, const char * /* name */, int /* flags */, unsigned /* debug_flags */, const char * /* file */, int /* line */
)
{
	void *ptr = _aligned_offset_malloc(size, alignment, alignment_offset);
	assert(ptr);
	return ptr;
}

void operator delete[](void *ptr)
{
	if (ptr)
	{
		_aligned_free(ptr);
	}
}

static void message(const char *txt)
{
	MessageBoxA(0, txt, "rom", 0);
}

static bool hook_skipcrashpadinit()
{
	LOG(INFO) << "Skipping crashpad init";
	return false;
}

static bool hook_CrashpadClient_WaitForHandlerStart(uintptr_t this_, DWORD timeout_ms)
{
	return false;
}

static void hook_CrashpadClient_SetFirstChanceExceptionHandler(uintptr_t func_ptr)
{

}

std::unordered_set<DWORD> seenThreads;
std::mutex seenThreadsMutex;

std::string wstring_to_utf8(const std::wstring &wstr);

void log_stacktrace_()
{
	__try
	{
		*(int *)1 = 1;
	}
	__except (big::big_exception_handler(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER)
	{
		Logger::FlushQueue();
	}
}

static void hook_luaV_execute(lua_State *L)
{
	std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);
	static auto hades_func = big::hades2_symbol_to_address["luaV_execute"].as_func<void(lua_State *)>();

	return hades_func(L);
}

static void hook_lua_callk(lua_State *L, int nargs, int nresults, int ctx, lua_CFunction k)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_callk"].as_func<void(lua_State *, int, int, int, lua_CFunction)>();
	return hades_func(L, nargs, nresults, ctx, k);
}

static void hook_lua_checkstack(lua_State *L, int sz)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_checkstack"].as_func<void(lua_State *, int)>();
	return hades_func(L, sz);
}

static void hook_lua_createtable(lua_State *L, int narray, int nrec)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_createtable"].as_func<void(lua_State *, int, int)>();
	return hades_func(L, narray, nrec);
}

static int hook_lua_error(lua_State *L)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_error"].as_func<int(lua_State *)>();
	return hades_func(L);
}

static int hook_lua_gc(lua_State *L, int what, int data)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_gc"].as_func<int(lua_State *, int, int)>();
	return hades_func(L, what, data);
}

static void hook_lua_getfield(lua_State *L, int idx, const char *k)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_getfield"].as_func<void(lua_State *, int, const char *)>();
	return hades_func(L, idx, k);
}

static int hook_lua_getmetatable(lua_State *L, int objindex)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_getmetatable"].as_func<int(lua_State *, int)>();
	return hades_func(L, objindex);
}

static void hook_lua_gettable(lua_State *L, int idx)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_gettable"].as_func<void(lua_State *, int)>();
	return hades_func(L, idx);
}

static void hook_lua_insert(lua_State *L, int idx)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_insert"].as_func<void(lua_State *, int)>();
	return hades_func(L, idx);
}

static int hook_lua_pcallk(lua_State *L, int nargs, int nresults, int errfunc, int ctx, lua_CFunction k)
{
	std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);

	static auto hades_func = big::hades2_symbol_to_address["lua_pcallk"].as_func<int(lua_State *, int, int, int, int, lua_CFunction)>();
	return hades_func(L, nargs, nresults, errfunc, ctx, k);
}

static int hook_lua_load(lua_State *L, const char *(__fastcall *reader)(lua_State *, void *, unsigned __int64 *), void *data, const char *chunkname, char *mode)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_load"].as_func<decltype(hook_lua_load)>();
	return hades_func(L, reader, data, chunkname, mode);
}

static int hook_luaL_ref(lua_State *L, int t)
{
	static auto hades_func = big::hades2_symbol_to_address["luaL_ref"].as_func<decltype(hook_luaL_ref)>();
	return hades_func(L, t);
}

static void hook_lua_rawseti(lua_State * L, int idx, int n)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_rawseti"].as_func<decltype(hook_lua_rawseti)>();
	return hades_func(L, idx, n);
}

static void hook_luaH_setint(lua_State *L, void *t, int key, void *value)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_setint"].as_func<decltype(hook_luaH_setint)>();
	return hades_func(L, t, key, value);
}

static TValue *hook_luaH_newkey(lua_State *L, Table *t, const TValue *key)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_newkey"].as_func<decltype(hook_luaH_newkey)>();
	return hades_func(L, t, key);
}

static void hook_luaH_resize(lua_State *L, Table *t, unsigned int nasize, unsigned int nhsize)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_resize"].as_func<decltype(hook_luaH_resize)>();
	return hades_func(L, t, nasize, nhsize);
}

static void *hook_luaM_realloc_(lua_State *L, void *block, size_t osize, size_t nsize)
{
	static auto hades_func = big::hades2_symbol_to_address["luaM_realloc_"].as_func<decltype(hook_luaM_realloc_)>();
	return hades_func(L, block, osize, nsize);
}

static void hook_luaH_free(lua_State *L, Table *t)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_free"].as_func<void(lua_State *, Table *)>();
	return hades_func(L, t);
}

static int hook_luaH_getn(Table *t)
{
	std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);

	//DWORD threadId = GetThreadId(GetCurrentThread());

	//{
	//	std::lock_guard<std::mutex> lock(seenThreadsMutex);
	//	if (seenThreads.find(threadId) == seenThreads.end())
	//	{
	//		seenThreads.insert(threadId);
	//		LOG(ERROR) << "New thread entered: ID = " << threadId;

	//		log_stacktrace_();

	//		PWSTR data;
	//		HRESULT hr = GetThreadDescription(GetCurrentThread(), &data);
	//		if (SUCCEEDED(hr))
	//		{
	//			wprintf(L"%ls\n", data);

	//			LOG(ERROR) << "Thread name: " << wstring_to_utf8(data);

	//			LocalFree(data);
	//		}
	//	}
	//}

	static auto hades_func = big::hades2_symbol_to_address["luaH_getn"].as_func<int(Table *)>();
	return hades_func(t);
}

static Table *hook_luaH_new(lua_State *L)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_new"].as_func<Table *(lua_State *)>();
	return hades_func(L);
}

static void hook_luaH_resizearray(lua_State *L, Table *t, int a3)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_resizearray"].as_func<void(lua_State *, Table *, int)>();

	return hades_func(L, t, a3);
}

static void hook_luaL_checkversion_(lua_State* L, lua_Number ver)
{
}

static int hook_game_lua_pcallk(lua_State *L, int nargs, int nresults, int errfunc, int ctx, lua_CFunction k)
{
	std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);

	/*DWORD threadId = GetThreadId(GetCurrentThread());

	{
		std::lock_guard<std::mutex> lock(seenThreadsMutex);
		if (seenThreads.find(threadId) == seenThreads.end())
		{
			seenThreads.insert(threadId);
			LOG(ERROR) << "New thread entered: ID = " << threadId;

			log_stacktrace_();

			PWSTR data;
			HRESULT hr = GetThreadDescription(GetCurrentThread(), &data);
			if (SUCCEEDED(hr))
			{
				wprintf(L"%ls\n", data);

				LOG(ERROR) << "Thread name: " << wstring_to_utf8(data);

				LocalFree(data);
			}
		}
	}*/

	return big::g_hooking->get_original<hook_game_lua_pcallk>()(L, nargs, nresults, errfunc, ctx, k);
}

//static void hook_sgg_WorkerManager_ExecuteCmd(uintptr_t this_, unsigned __int64 arg)
//{
//	PWSTR data;
//	HRESULT hr = GetThreadDescription(GetCurrentThread(), &data);
//	if (SUCCEEDED(hr))
//	{
//		if (wcscmp(data, L"LoadTaskScheduler") == 0)
//		{
//			LocalFree(data);
//
//			std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);
//
//			big::g_hooking->get_original<hook_sgg_WorkerManager_ExecuteCmd>()(this_, arg);
//
//			return;
//		}
//
//		LocalFree(data);
//	}
//
//	big::g_hooking->get_original<hook_sgg_WorkerManager_ExecuteCmd>()(this_, arg);
//}

static int hook_game_luaL_loadbufferx(lua_State* L, const char* original_file_content_ptr, size_t original_file_content_size, const char* name, char* mode)
{
	const auto *apply_buffer_result =
	    lovely_apply_buffer_patches(original_file_content_ptr,
	                                original_file_content_size,
	                                name,
	                                (const char *)big::g_file_manager.get_project_folder("plugins").get_path().u8string().c_str());

	assert(apply_buffer_result);

	if (apply_buffer_result->status != lovely_ApplyBufferPatchesResultEnum::Ok)
	{
		LOG(DEBUG) << "lovely_apply_buffer_patches returned " << static_cast<int>(apply_buffer_result->status);
	}

	const auto res = big::g_hooking->get_original<hook_game_luaL_loadbufferx>()(L, apply_buffer_result->data_ptr, apply_buffer_result->data_len, name, mode);

	if (apply_buffer_result->status == lovely_ApplyBufferPatchesResultEnum::Ok)
	{
		free(apply_buffer_result->data_ptr);
	}

	free((void *)apply_buffer_result);

	return res;
}

enum class ResourceDirectory : int
{
	Scripts = 0xE,
};

static bool hook_sgg_IsContentFolderModified(ResourceDirectory directory, bool initialValue, bool checkForAddedFiles)
{
	if (directory == ResourceDirectory::Scripts)
	{
		return true;
	}

	if (big::g_hooking->get_original<hook_sgg_IsContentFolderModified>()(directory, initialValue, checkForAddedFiles))
	{
		LOG(WARNING) << "Resource directory modified: " << static_cast<int>(directory);
	}

	return false;
}

// void initRenderer(char *appName, const RendererDesc *pDesc, Renderer **)
static void hook_initRenderer(char *appName, const void *pDesc, void **a3)
{
	LOG(INFO) << "initRenderer called";

	big::g_hooking->get_original<hook_initRenderer>()(appName, pDesc, a3);

	big::g_renderer->hook();

	LOG(INFO) << "initRenderer finished";
}

static void hook_SGD_Deserialize_ThingDataDef(void *ctx, int loc, sgg::ThingDataDef *val)
{
	big::g_hooking->get_original<hook_SGD_Deserialize_ThingDataDef>()(ctx, loc, val);
	//val->mScale *= 2;
}

static void sgg__GUIComponentTextBox__GUIComponentTextBox(GUIComponentTextBox *this_, Vectormath::Vector2 location)
{
	//std::scoped_lock l(g_GUIComponentTextBoxes_mutex);

	big::g_hooking->get_original<sgg__GUIComponentTextBox__GUIComponentTextBox>()(this_, location);

	g_GUIComponentTextBoxes.insert(this_);
}

static void sgg__GUIComponentTextBox__Update(GUIComponentTextBox *this_)
{
	std::scoped_lock l(g_GUIComponentTextBoxes_mutex);

	big::g_hooking->get_original<sgg__GUIComponentTextBox__Update>()(this_);

	g_GUIComponentTextBoxes.insert(this_);
}

static void sgg__GUIComponentTextBox__GUIComponentTextBox_dctor(GUIComponentTextBox *this_)
{
	std::scoped_lock l(g_GUIComponentTextBoxes_mutex);

	big::g_hooking->get_original<sgg__GUIComponentTextBox__GUIComponentTextBox_dctor>()(this_);

	g_GUIComponentTextBoxes.erase(this_);
}

static void hook_GUIComponentButton_OnSelected(GUIComponentButton *this_, GUIComponentTextBox *prevSelection)
{
	std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);

	big::g_hooking->get_original<hook_GUIComponentButton_OnSelected>()(this_, prevSelection);

	g_currently_selected_gui_comp = this_;

	auto gui_button = g_currently_selected_gui_comp;
	auto gui_text   = gui_button->mTextBox;

	std::vector<std::string> lines;
	for (auto i = gui_text->mLines.mpBegin; i < gui_text->mLines.mpEnd; i++)
	{
		if (i->mText.size())
		{
			lines.push_back(i->mText.c_str());
		}
	}

	for (const auto &mod_ : big::g_lua_manager->m_modules)
	{
		auto mod = (big::lua_module_ext *)mod_.get();
		for (const auto &f : mod->m_data_ext.m_on_button_hover)
		{
			f(lines);
		}
	}
}

// void sgg::Granny3D::LoadAllModelAndAnimationData(void)
static void hook_LoadAllModelAndAnimationData()
{
	// Not calling it ever again because it crashes inside the func when hotreloading game data.
	// Make sure it's atleast called once though on game start.

	static bool call_it_once = true;
	if (call_it_once)
	{
		call_it_once = false;
		big::g_hooking->get_original<hook_LoadAllModelAndAnimationData>()();
	}
}

static void hook_ReadAllAnimationData()
{
	// Not calling it ever again because it crashes inside the func when hotreloading game data.
	// Make sure it's atleast called once though on game start.

	static bool call_it_once = true;
	if (call_it_once)
	{
		call_it_once = false;
		big::g_hooking->get_original<hook_ReadAllAnimationData>()();
	}
}

// TODO: Cleanup all this
template<class T>
static void ForceWrite(T &dst, const T &src)
{
	DWORD old_flag;
	::VirtualProtect(&dst, sizeof(T), PAGE_EXECUTE_READWRITE, &old_flag);
	dst = src;
	::VirtualProtect(&dst, sizeof(T), old_flag, &old_flag);
}

static void hook_PlayerHandleInput(void *this_, float elapsedSeconds, void *input)
{
	static auto jump_stuff = gmAddress::scan("74 7C 38 05").as<uint8_t *>();

	if (big::g_gui && big::g_gui->is_open() && !lua::hades::inputs::let_game_input_go_through_gui_layer)
	{
		if (jump_stuff && *jump_stuff != 0x75)
		{
			ForceWrite<uint8_t>(*jump_stuff, 0x75);
		}

		return;
	}

	if (jump_stuff && *jump_stuff != 0x74)
	{
		ForceWrite<uint8_t>(*jump_stuff, 0x74);
	}

	big::g_hooking->get_original<hook_PlayerHandleInput>()(this_, elapsedSeconds, input);
}

extern "C"
{
	uintptr_t lpRemain = 0;
}

struct sgg_config_values_fixed
{
	bool *addr     = nullptr;
	bool new_value = false;
};

static bool sgg_config_values_thread_can_loop = false;
static std::vector<sgg_config_values_fixed> sgg_config_values;

static void set_sgg_config_values_thread_loop()
{
	while (true)
	{
		if (sgg_config_values_thread_can_loop)
		{
			for (auto &cfg_value : sgg_config_values)
			{
				*cfg_value.addr = cfg_value.new_value;
			}
		}

		using namespace std::chrono_literals;
		std::this_thread::sleep_for(1s);
	}
}

static void hook_sgg_App_Initialize(void *this_)
{
	auto AudioMemoryPoolVoiceSize = big::hades2_symbol_to_address["sgg::ConfigOptions::AudioMemoryPoolVoiceSize"].as<int *>();
	*AudioMemoryPoolVoiceSize = 0;

	LOG(INFO) << "Setting AudioMemoryPoolVoiceSize to " << *AudioMemoryPoolVoiceSize << ", mods will hit the limit otherwise.";

	auto AudioMemoryPoolSize  = big::hades2_symbol_to_address["sgg::ConfigOptions::AudioMemoryPoolSize"].as<int *>();
	*AudioMemoryPoolSize     = 0;

	LOG(INFO) << "Setting AudioMemoryPoolSize to " << *AudioMemoryPoolSize << ", mods will hit the limit otherwise.";

	return big::g_hooking->get_original<hook_sgg_App_Initialize>()(this_);
	}

static void hook_sgg_ScriptManager_InitLua()
	{
	auto LuaMemoryPoolSize = big::hades2_symbol_to_address["sgg::ConfigOptions::LuaMemoryPoolSize"].as<int *>();
	*LuaMemoryPoolSize     = 0;

	LOG(INFO) << "Setting LuaMemoryPoolSize to " << *LuaMemoryPoolSize << ", mods will hit the limit otherwise.";

	return big::g_hooking->get_original<hook_sgg_ScriptManager_InitLua>()();
}

static bool hook_ConfigOption_registerField_bool(char *name, bool *addr, unsigned int flags, bool defaultValue)
{
	bool is_UseAnalytics = false;
	if (name && strstr(name, "UseAnalytics"))
	{
		defaultValue    = false;
		is_UseAnalytics = true;
	}

	bool is_DebugKeysEnabled = false;
	if (name && strstr(name, "DebugKeysEnabled"))
	{
		defaultValue        = true;
		is_DebugKeysEnabled = true;
	}

	bool is_UnsafeDebugKeysEnabled = false;
	if (name && strstr(name, "UnsafeDebugKeysEnabled"))
	{
		defaultValue              = true;
		is_UnsafeDebugKeysEnabled = true;
	}

	auto res = big::g_hooking->get_original<hook_ConfigOption_registerField_bool>()(name, addr, flags, defaultValue);

	static std::thread set_sgg_config_values_thread = []()
	{
		auto t = std::thread(set_sgg_config_values_thread_loop);
		t.detach();
		return t;
	}();

	if (is_UseAnalytics)
	{
		LOG(INFO) << "Making sure UseAnalytics is false.";
		res = false;
		sgg_config_values.emplace_back(addr, false);
	}

	if (is_DebugKeysEnabled)
	{
		LOG(INFO) << "Making sure DebugKeysEnabled is true.";
		res = true;
		sgg_config_values.emplace_back(addr, true);
	}

	if (is_UnsafeDebugKeysEnabled)
	{
		LOG(INFO) << "Making sure UnsafeDebugKeysEnabled is true.";
		res = true;
		sgg_config_values.emplace_back(addr, true);
		sgg_config_values_thread_can_loop = true;
	}

	return res;
}

static void hook_PlatformAnalytics_Start()
{
	LOG(INFO) << "PlatformAnalytics_Start denied";
}

static void hook_sgg_DetectFreezeThread(HWND__* pData)
{
	LOG(INFO) << "DetectFreezeThread denied";
}

static void hook_disable_f10_launch(void *bugInfo)
{
	LOG(WARNING) << "sgg::LaunchBugReporter denied";

	static bool once = true;
	if (once)
	{
		MessageBoxA(0, "The game has encountered a fatal error, the error is in the log file and in the console.", "Hell2Modding", MB_ICONERROR | MB_OK);
		once = false;
	}
}

static int ends_with(const char *str, const char *suffix)
{
	if (!str || !suffix)
	{
		return 0;
	}
	size_t lenstr    = strlen(str);
	size_t lensuffix = strlen(suffix);
	if (lensuffix > lenstr)
	{
		return 0;
	}
	return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

static bool extension_matches(const char *first, const char *second)
{
	auto get_extension = [](const char *str) -> std::string_view
	{
		size_t len = strlen(str);
		std::string_view ext;
		for (size_t i = 0; i < len; i++)
		{
			if (str[i] == '.')
			{
				ext = std::string_view(&str[i], &str[len - 1]);
			}
		}

		return ext;
	};

	std::string_view first_ext  = get_extension(first);
	std::string_view second_ext = get_extension(second);

	if (first_ext.empty() && second_ext.empty())
	{
		return true;
	}

	if (first_ext.empty() || second_ext.empty())
	{
		return false;
	}

	return !strcmp(first_ext.data(), second_ext.data());
}

template<class F>
bool EachImportFunction(HMODULE module, const char *dllname, const F &f)
{
	if (module == 0)
	{
		return false;
	}

	size_t ImageBase             = (size_t)module;
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)ImageBase;
	if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
	{
		return false;
	}
	PIMAGE_NT_HEADERS pNTHeader = (PIMAGE_NT_HEADERS)(ImageBase + pDosHeader->e_lfanew);

	size_t RVAImports = pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
	if (RVAImports == 0)
	{
		return false;
	}

	IMAGE_IMPORT_DESCRIPTOR *pImportDesc = (IMAGE_IMPORT_DESCRIPTOR *)(ImageBase + RVAImports);
	while (pImportDesc->Name != 0)
	{
		if (!dllname || stricmp((const char *)(ImageBase + pImportDesc->Name), dllname) == 0)
		{
			IMAGE_IMPORT_BY_NAME **func_names = (IMAGE_IMPORT_BY_NAME **)(ImageBase + pImportDesc->Characteristics);
			void **import_table               = (void **)(ImageBase + pImportDesc->FirstThunk);
			for (size_t i = 0;; ++i)
			{
				if ((size_t)func_names[i] == 0)
				{
					break;
				}
				const char *funcname = (const char *)(ImageBase + (size_t)func_names[i]->Name);
				f(funcname, import_table[i]);
			}
		}
		++pImportDesc;
	}
	return true;
}

using fmodstudio_getevent_t = __int64 (*)(void *fmodstudio_event_system_this, const char *event_name, void **event_description_result);

fmodstudio_getevent_t fmodstudio_getevent_orig = nullptr;

static __int64 hook_fmodstudio_getevent(void *fmodstudio_event_system_this, const char *event_name, void **event_description_result)
{
	if (strstr(event_name, "event:{") && strstr(event_name, "}"))
	{
		event_name = &event_name[6];
	}

	//LOG(INFO) << event_name;

	const auto res = fmodstudio_getevent_orig(fmodstudio_event_system_this, event_name, event_description_result);
	if (res != 0)
	{
		LOG(WARNING) << "Failed playing " << event_name << " - " << res << " - " << event_description_result << " - " << *event_description_result;
	}

	return res;
}

using setlocale_t = char* (__cdecl*)(int Category, const char *Locale);

setlocale_t setlocale_orig = nullptr;

char *__cdecl hook_setlocale(int Category, const char *Locale)
{
	if (strcmp(Locale, "C") == 0)
	{
		Locale = ".utf8";
	}

	return setlocale_orig(Category, Locale);
}

static void init_import_hooks()
{
	EachImportFunction(::GetModuleHandleA(0),
	                   "fmodstudio.dll",
	                   [](const char *funcname, void *&func)
	                   {
		                   if (strcmp(funcname, "?getEvent@System@Studio@FMOD@@QEBA?AW4FMOD_RESULT@@PEBDPEAPEAVEventDescription@23@@Z") == 0)
		                   {
			                   fmodstudio_getevent_orig = (fmodstudio_getevent_t)func;
			                   ForceWrite<void *>(func, hook_fmodstudio_getevent);
		                   }
	                   });

	EachImportFunction(::GetModuleHandleA(0),
	                   "api-ms-win-crt-locale-l1-1-0.dll",
	                   [](const char *funcname, void *&func)
	                   {
		                   if (strcmp(funcname, "setlocale") == 0)
		                   {
			                   setlocale_orig = (setlocale_t)func;
			                   LOG(INFO) << "Hooked setlocale";
			                   ForceWrite<void *>(func, hook_setlocale);
		                   }
	                   });
}

// Lua: Enforce AuthorName-ModName to be part of the std::filesystem::path.filename() of the file.
// See the binding data.cpp file for the implementation.
std::unordered_map<std::string, std::string> additional_package_files;

std::unordered_map<std::string, std::string> additional_granny_files;

//static std::string g_current_custom_package_stem;

static void hook_fsAppendPathComponent_packages(const char *basePath, const char *pathComponent, char *output /*size: 512*/)
{
	//g_current_custom_package_stem = "";

	big::g_hooking->get_original<hook_fsAppendPathComponent_packages>()(basePath, pathComponent, output);

	if (strlen(pathComponent) > 0)
	{
		for (const auto &[filename, full_file_path] : additional_package_files)
		{
			if (strstr(pathComponent, filename.c_str()) && extension_matches(pathComponent, filename.c_str()))
			{
				LOG(DEBUG) << pathComponent << " | " << filename << " | " << full_file_path;
				//g_current_custom_package_stem = (char *)std::filesystem::path(filename).stem().u8string().c_str();
				strcpy(output, full_file_path.c_str());
				break;
			}
			else if (strcmp(filename.c_str(), pathComponent) == 0 && extension_matches(pathComponent, filename.c_str()))
			{
				LOG(DEBUG) << pathComponent << " | " << filename << " | " << full_file_path;
				//g_current_custom_package_stem = (char *)std::filesystem::path(filename).stem().u8string().c_str();
				strcpy(output, full_file_path.c_str());
				break;
			}
		}

		for (const auto &[filename, full_file_path] : additional_granny_files)
		{
			if (strstr(pathComponent, filename.c_str()))
			{
				LOG(DEBUG) << pathComponent << " | " << filename << " | " << full_file_path;

				strcpy(output, full_file_path.c_str());
				break;
			}
		}
	}
}

static void hook_fsGetFilesWithExtension_packages(PVOID resourceDir, const char *subDirectory, wchar_t *extension, eastl::vector<eastl::string> *out)
{
	big::g_hooking->get_original<hook_fsGetFilesWithExtension_packages>()(resourceDir, subDirectory, extension, out);

	bool has_pkg_manifest = false;
	bool has_pkg          = false;
	bool has_gpk          = false;
	for (const auto &xd : *out)
	{
		if (ends_with(xd.c_str(), ".pkg_manifest"))
		{
			has_pkg_manifest = true;
		}

		if (ends_with(xd.c_str(), ".pkg"))
		{
			has_pkg = true;
		}

		if (ends_with(xd.c_str(), ".gpk"))
		{
			has_gpk = true;
		}
	}

	bool is_pkg_manifest_only = has_pkg_manifest && !has_pkg;
	if (is_pkg_manifest_only)
	{
		for (const auto &[filename, full_file_path] : additional_package_files)
		{
			if (ends_with(filename.c_str(), ".pkg_manifest"))
			{
				out->push_back(filename.c_str());
			}
		}
	}
	else if (has_pkg && has_pkg_manifest)
	{
		for (const auto &[filename, full_file_path] : additional_package_files)
		{
			out->push_back(filename.c_str());
		}
	}
	else if (has_gpk)
	{
		for (const auto &[filename, full_file_path] : additional_granny_files)
		{
			out->push_back(filename.c_str());
		}
	}
}

static void lua_print_traceback()
{
	const auto lua_manager = big::g_lua_manager;
	if (lua_manager && lua_manager->get_module_count())
	{
		sol::state_view(lua_manager->lua_state()).script("if rom and rom.game and rom.game._activeThread then rom.log.warning(debug.traceback(rom.game._activeThread)) else debug.traceback() end");
	}
}

static void hook_ParseLuaErrorMessageAndAssert(lua_State *msg, const char *len, unsigned __int64 state)
{
	lua_print_traceback();

	big::g_hooking->get_original<hook_ParseLuaErrorMessageAndAssert>()(msg, len, state);
}

static eastl::string *hook_ReadCSString(eastl::string *result, void *file_stream_input)
{
	const auto res = big::g_hooking->get_original<hook_ReadCSString>()(result, file_stream_input);

	if (result)
	{
		//LOG(WARNING) << result->c_str();
	}
	//if (result && g_current_custom_package_stem.size())
	{
		//LOG(ERROR) << result->c_str() << " does not contain " << g_current_custom_package_stem;
	}

	return res;
}

namespace MemoryMappedFile
{
	struct Handle
	{
#ifdef _WIN32
		void *file;
		void *fileMapping;
#else
		int file;
#endif
		void *baseAddress;
		size_t len;
	};

	Handle Open(const char *path);
	void Close(Handle &handle);
} // namespace MemoryMappedFile

MemoryMappedFile::Handle MemoryMappedFile::Open(const char *path)
{
#ifdef _WIN32
	void *file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, nullptr);

	if (file == INVALID_HANDLE_VALUE)
	{
		return Handle{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, nullptr, 0};
	}

	void *fileMapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);

	if (fileMapping == nullptr)
	{
		CloseHandle(file);

		return Handle{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, nullptr, 0};
	}

	void *baseAddress = MapViewOfFile(fileMapping, FILE_MAP_READ, 0, 0, 0);

	if (baseAddress == nullptr)
	{
		CloseHandle(fileMapping);
		CloseHandle(file);

		return Handle{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, nullptr, 0};
	}

	BY_HANDLE_FILE_INFORMATION fileInformation;
	const bool getInformationResult = GetFileInformationByHandle(file, &fileInformation);
	if (!getInformationResult)
	{
		UnmapViewOfFile(baseAddress);
		CloseHandle(fileMapping);
		CloseHandle(file);

		return Handle{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, nullptr, 0};
	}

	const size_t fileSizeHighBytes = static_cast<size_t>(fileInformation.nFileSizeHigh) << 32;
	const size_t fileSizeLowBytes  = fileInformation.nFileSizeLow;
	const size_t fileSize          = fileSizeHighBytes | fileSizeLowBytes;
	return Handle{file, fileMapping, baseAddress, fileSize};
#else
	struct stat fileSb;

	int file = open(path, O_RDONLY);

	if (file == INVALID_HANDLE_VALUE)
	{
		return Handle{INVALID_HANDLE_VALUE, nullptr, 0};
	}

	if (fstat(file, &fileSb) == -1)
	{
		close(file);

		return Handle{INVALID_HANDLE_VALUE, nullptr, 0};
	}

	void *baseAddress = mmap(nullptr, fileSb.st_size, PROT_READ, MAP_PRIVATE, file, 0);

	if (baseAddress == MAP_FAILED)
	{
		close(file);

		return Handle{INVALID_HANDLE_VALUE, nullptr, 0};
	}

	return Handle{file, baseAddress, static_cast<size_t>(fileSb.st_size)};
#endif
}

void MemoryMappedFile::Close(Handle &handle)
{
#ifdef _WIN32
	UnmapViewOfFile(handle.baseAddress);
	CloseHandle(handle.fileMapping);
	CloseHandle(handle.file);

	handle.file        = nullptr;
	handle.fileMapping = nullptr;
#else
	munmap(handle.baseAddress, handle.len);
	close(handle.file);

	handle.file = 0;
#endif

	handle.baseAddress = nullptr;
}

PDB_NO_DISCARD static bool IsError(PDB::ErrorCode errorCode)
{
	switch (errorCode)
	{
	case PDB::ErrorCode::Success: return false;

	case PDB::ErrorCode::InvalidSuperBlock: LOGF(ERROR, "Invalid Superblock"); return true;

	case PDB::ErrorCode::InvalidFreeBlockMap: LOGF(ERROR, "Invalid free block map"); return true;

	case PDB::ErrorCode::InvalidStream: LOGF(ERROR, "Invalid stream"); return true;

	case PDB::ErrorCode::InvalidSignature: LOGF(ERROR, "Invalid stream signature"); return true;

	case PDB::ErrorCode::InvalidStreamIndex: LOGF(ERROR, "Invalid stream index"); return true;

	case PDB::ErrorCode::UnknownVersion: LOGF(ERROR, "Unknown version"); return true;
	}

	// only ErrorCode::Success means there wasn't an error, so all other paths have to assume there was an error
	return true;
}

PDB_NO_DISCARD static bool HasValidDBIStreams(const PDB::RawFile &rawPdbFile, const PDB::DBIStream &dbiStream)
{
	// check whether the DBI stream offers all sub-streams we need
	if (IsError(dbiStream.HasValidSymbolRecordStream(rawPdbFile)))
	{
		return false;
	}

	if (IsError(dbiStream.HasValidPublicSymbolStream(rawPdbFile)))
	{
		return false;
	}

	if (IsError(dbiStream.HasValidGlobalSymbolStream(rawPdbFile)))
	{
		return false;
	}

	if (IsError(dbiStream.HasValidSectionContributionStream(rawPdbFile)))
	{
		return false;
	}

	if (IsError(dbiStream.HasValidImageSectionStream(rawPdbFile)))
	{
		return false;
	}

	return true;
}

static void read_game_pdb()
{
	const auto base_game_address = (uintptr_t)GetModuleHandleA(0);

	const auto game_pdb_path = lua::paths_ext::get_game_executable_folder() / "Hades2.pdb";

	// try to open the PDB file and check whether all the data we need is available
	MemoryMappedFile::Handle pdbFile = MemoryMappedFile::Open((char *)game_pdb_path.u8string().c_str());
	if (!pdbFile.baseAddress)
	{
		LOGF(ERROR, "Cannot memory-map file {}", (char *)game_pdb_path.u8string().c_str());
	}

	if (IsError(PDB::ValidateFile(pdbFile.baseAddress, pdbFile.len)))
	{
		MemoryMappedFile::Close(pdbFile);
	}

	const PDB::RawFile rawPdbFile = PDB::CreateRawFile(pdbFile.baseAddress);
	if (IsError(PDB::HasValidDBIStream(rawPdbFile)))
	{
		MemoryMappedFile::Close(pdbFile);
	}

	const PDB::InfoStream infoStream(rawPdbFile);
	if (infoStream.UsesDebugFastLink())
	{
		LOGF(ERROR, "PDB was linked using unsupported option /DEBUG:FASTLINK");

		MemoryMappedFile::Close(pdbFile);
	}

	const auto h = infoStream.GetHeader();
	LOGF(INFO,
	     std::format("Version {}, signature {}, age {}, GUID "
	                 "{:08x}-{:04x}-{:04x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
	                 static_cast<uint32_t>(h->version),
	                 h->signature,
	                 h->age,
	                 h->guid.Data1,
	                 h->guid.Data2,
	                 h->guid.Data3,
	                 h->guid.Data4[0],
	                 h->guid.Data4[1],
	                 h->guid.Data4[2],
	                 h->guid.Data4[3],
	                 h->guid.Data4[4],
	                 h->guid.Data4[5],
	                 h->guid.Data4[6],
	                 h->guid.Data4[7]));


	const PDB::DBIStream dbiStream = PDB::CreateDBIStream(rawPdbFile);
	if (!HasValidDBIStreams(rawPdbFile, dbiStream))
	{
		MemoryMappedFile::Close(pdbFile);
	}

	const PDB::TPIStream tpiStream = PDB::CreateTPIStream(rawPdbFile);
	if (PDB::HasValidTPIStream(rawPdbFile) != PDB::ErrorCode::Success)
	{
		MemoryMappedFile::Close(pdbFile);
	}

	// in order to keep the example easy to understand, we load the PDB data serially.
	// note that this can be improved a lot by reading streams concurrently.

	// prepare the image section stream first. it is needed for converting section + offset into an RVA
	const PDB::ImageSectionStream imageSectionStream = dbiStream.CreateImageSectionStream(rawPdbFile);


	const PDB::ModuleInfoStream moduleInfoStream = dbiStream.CreateModuleInfoStream(rawPdbFile);


	const PDB::CoalescedMSFStream symbolRecordStream = dbiStream.CreateSymbolRecordStream(rawPdbFile);

	// read global symbols
	const PDB::GlobalSymbolStream globalSymbolStream = dbiStream.CreateGlobalSymbolStream(rawPdbFile);

	const PDB::ArrayView<PDB::HashRecord> hashRecords = globalSymbolStream.GetRecords();
	const size_t count                                = hashRecords.GetLength();

	for (const PDB::HashRecord &hashRecord : hashRecords)
	{
		const PDB::CodeView::DBI::Record *record = globalSymbolStream.GetRecord(symbolRecordStream, hashRecord);

		const char *name = nullptr;
		uint32_t rva     = 0u;
		if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GDATA32)
		{
			name = record->data.S_GDATA32.name;
			rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GDATA32.section, record->data.S_GDATA32.offset);
		}
		else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GTHREAD32)
		{
			name = record->data.S_GTHREAD32.name;
			rva =
			    imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GTHREAD32.section, record->data.S_GTHREAD32.offset);
		}
		else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LDATA32)
		{
			name = record->data.S_LDATA32.name;
			rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LDATA32.section, record->data.S_LDATA32.offset);
		}
		else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LTHREAD32)
		{
			name = record->data.S_LTHREAD32.name;
			rva =
			    imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LTHREAD32.section, record->data.S_LTHREAD32.offset);
		}
		else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_UDT)
		{
			name = record->data.S_UDT.name;
		}
		else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_UDT_ST)
		{
			name = record->data.S_UDT_ST.name;
		}

		if (rva == 0u)
		{
			// certain symbols (e.g. control-flow guard symbols) don't have a valid RVA, ignore those
			continue;
		}

		//LOG(INFO) << "name: " << name << " rva: " << rva;
		big::hades2_insert_symbol_to_map(name, base_game_address + rva);
	}

	// read module symbols
	{
		const PDB::ArrayView<PDB::ModuleInfoStream::Module> modules = moduleInfoStream.GetModules();

		for (const PDB::ModuleInfoStream::Module &module : modules)
		{
			if (!module.HasSymbolStream())
			{
				continue;
			}

			const PDB::ModuleSymbolStream moduleSymbolStream = module.CreateSymbolStream(rawPdbFile);
			moduleSymbolStream.ForEachSymbol(
			    [&imageSectionStream, base_game_address](const PDB::CodeView::DBI::Record *record)
			    //[&symbols, &imageSectionStream](const PDB::CodeView::DBI::Record *record)
			    {
				    const char *name = nullptr;
				    uint32_t rva     = 0u;
				    if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_THUNK32)
				    {
					    if (record->data.S_THUNK32.thunk == PDB::CodeView::DBI::ThunkOrdinal::TrampolineIncremental)
					    {
						    // we have never seen incremental linking thunks stored inside a S_THUNK32 symbol, but better be safe than sorry
						    name = "ILT";
						    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_THUNK32.section,
                                                                               record->data.S_THUNK32.offset);
					    }
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_TRAMPOLINE)
				    {
					    // incremental linking thunks are stored in the linker module
					    name = "ILT";
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_TRAMPOLINE.thunkSection,
                                                                           record->data.S_TRAMPOLINE.thunkOffset);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_BLOCK32)
				    {
					    // blocks never store a name and are only stored for indicating whether other symbols are children of this block
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LABEL32)
				    {
					    // labels don't have a name
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LPROC32)
				    {
					    name = record->data.S_LPROC32.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32.section,
                                                                           record->data.S_LPROC32.offset);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GPROC32)
				    {
					    name = record->data.S_GPROC32.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32.section,
                                                                           record->data.S_GPROC32.offset);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LPROC32_ID)
				    {
					    name = record->data.S_LPROC32_ID.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32_ID.section,
                                                                           record->data.S_LPROC32_ID.offset);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GPROC32_ID)
				    {
					    name = record->data.S_GPROC32_ID.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32_ID.section,
                                                                           record->data.S_GPROC32_ID.offset);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_REGREL32)
				    {
					    name = record->data.S_REGREL32.name;
					    // You can only get the address while running the program by checking the register value and adding the offset
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LDATA32)
				    {
					    name = record->data.S_LDATA32.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LDATA32.section,
                                                                           record->data.S_LDATA32.offset);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LTHREAD32)
				    {
					    name = record->data.S_LTHREAD32.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LTHREAD32.section,
                                                                           record->data.S_LTHREAD32.offset);
				    }

				    if (rva == 0u)
				    {
					    // certain symbols (e.g. control-flow guard symbols) don't have a valid RVA, ignore those
					    return;
				    }

				    //LOG(INFO) << "name: " << name << " rva: " << rva;
				    big::hades2_insert_symbol_to_map(name, base_game_address + rva);
			    });
		}
	}

	MemoryMappedFile::Close(pdbFile);
}

BOOL APIENTRY DllMain(HMODULE hmod, DWORD reason, PVOID)
{
	using namespace big;

	if (reason == DLL_PROCESS_ATTACH)
	{
		//while (!IsDebuggerPresent())
		{
			//Sleep(1000);
		}

		g_hmodule = hmod;

		// https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/setlocale-wsetlocale?view=msvc-170#utf-8-support
		setlocale(LC_ALL, ".utf8");
		// This also change things like stringstream outputs and add comma to numbers and things like that, we don't want that, so just set locale on the C apis instead.
		//std::locale::global(std::locale(".utf8"));

		dll_proxy::init();

		if (!rom::is_rom_enabled())
		{
			return true;
		}

		// Lua API: Namespace
		// Name: rom
		rom::init("Hell2Modding", "Hades2.exe", "rom");

		// Purposely leak it, we are not unloading this module in any case.
		const auto exception_handling = new exception_handler(true, nullptr);

		read_game_pdb();

		{
			extend_sgg_Sbuffer();
		}

		const auto initRenderer_ptr = big::hades2_symbol_to_address["initRenderer"];
		if (initRenderer_ptr)
		{
			big::hooking::detour_hook_helper::add_queue<hook_initRenderer>("initRenderer", initRenderer_ptr);
		}

		const auto backtrace_initializeCrashpad_ptr = big::hades2_symbol_to_address["backtrace::initializeCrashpad"];
		if (backtrace_initializeCrashpad_ptr)
		{
			big::hooking::detour_hook_helper::add_queue<hook_skipcrashpadinit>("backtrace::initializeCrashpad",
			                                                                 backtrace_initializeCrashpad_ptr.as_func<bool()>());
		}

		{
			// The game has Lua statically linked, which means all of Lua's global state lives in its .data section.
			// Because these globals are not exposed, we can't directly call into the game's Lua functions from our DLL.
			// To work with Lua ourselves, we also have to statically link against a Lua library - but that creates a
			// separate set of global variables. To solve this, we hook most of our own set of statically linked
			// Lua functions and redirect them so they call the game's Lua functions instead,
			// ensuring both sides are in sync.
			// clang-format off
			big::hooking::detour_hook_helper::add_queue<hook_luaL_checkversion_>("", &luaL_checkversion_);
			big::hooking::detour_hook_helper::add_queue<hook_luaV_execute>("", &luaV_execute);
			big::hooking::detour_hook_helper::add_queue<hook_lua_callk>("", &lua_callk);
			big::hooking::detour_hook_helper::add_queue<hook_lua_checkstack>("", &lua_checkstack);
			big::hooking::detour_hook_helper::add_queue<hook_lua_createtable>("", &lua_createtable);
			big::hooking::detour_hook_helper::add_queue<hook_lua_error>("", &lua_error);
			big::hooking::detour_hook_helper::add_queue<hook_lua_gc>("", &lua_gc);
			big::hooking::detour_hook_helper::add_queue<hook_lua_getfield>("", &lua_getfield);
			big::hooking::detour_hook_helper::add_queue<hook_lua_getmetatable>("", &lua_getmetatable);
			big::hooking::detour_hook_helper::add_queue<hook_lua_gettable>("", &lua_gettable);
			big::hooking::detour_hook_helper::add_queue<hook_lua_insert>("", &lua_insert);
			big::hooking::detour_hook_helper::add_queue<hook_lua_pcallk>("", &lua_pcallk);
			big::hooking::detour_hook_helper::add_queue<hook_lua_load>("", &lua_load);
			big::hooking::detour_hook_helper::add_queue<hook_luaL_ref>("", &luaL_ref);
			big::hooking::detour_hook_helper::add_queue<hook_lua_rawseti>("", &lua_rawseti);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_setint>("", &luaH_setint);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_newkey>("", &luaH_newkey);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_resize>("", &luaH_resize);
			big::hooking::detour_hook_helper::add_queue<hook_luaM_realloc_>("", &luaM_realloc_);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_free>("", &luaH_free);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_getn>("", &luaH_getn);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_new>("", &luaH_new);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_resizearray>("", &luaH_resizearray);
			// clang-format on

			// lovely injector integration
			big::hooking::detour_hook_helper::add_queue<hook_game_luaL_loadbufferx>("", big::hades2_symbol_to_address["luaL_loadbufferx"]);
			big::hooking::detour_hook_helper::add_queue<hook_game_lua_pcallk>("", big::hades2_symbol_to_address["lua_pcallk"]);

			//big::hooking::detour_hook_helper::add_queue<hook_sgg_WorkerManager_ExecuteCmd>("", big::hades2_symbol_to_address["sgg::WorkerManager::ExecuteCmd"]);
		}

		/*{
			static auto GUIComponentTextBox_ctor_ptr = gmAddress::scan("89 BB 2C 06 00 00", "sgg::GUIComponentTextBox::GUIComponentTextBox");
			if (GUIComponentTextBox_ctor_ptr)
			{
				static auto GUIComponentTextBox_ctor = GUIComponentTextBox_ctor_ptr.offset(-0x3B);
				static auto hook_ = hooking::detour_hook_helper::add_queue<sgg__GUIComponentTextBox__GUIComponentTextBox>("sgg__GUIComponentTextBox__GUIComponentTextBox", GUIComponentTextBox_ctor);
			}
		}*/

		{
			big::hooking::detour_hook_helper::add_queue<hook_sgg_IsContentFolderModified>("", big::hades2_symbol_to_address["sgg::IsContentFolderModified"]);
		}

		{
			static auto GUIComponentTextBox_update_ptr =
			    big::hades2_symbol_to_address["sgg::GUIComponentTextBox::Update"];
			if (GUIComponentTextBox_update_ptr)
			{
				static auto GUIComponentTextBox_update = GUIComponentTextBox_update_ptr;
				static auto hook_ = hooking::detour_hook_helper::add_queue<sgg__GUIComponentTextBox__Update>(
				    "sgg__GUIComponentTextBox__Update",
				    GUIComponentTextBox_update);
			}
		}

		{
			static auto GUIComponentTextBox_dctor_ptr =
			    big::hades2_symbol_to_address["sgg::GUIComponentTextBox::~GUIComponentTextBox"];
			if (GUIComponentTextBox_dctor_ptr)
			{
				static auto GUIComponentTextBox_dctor = GUIComponentTextBox_dctor_ptr;
				static auto hook_ = hooking::detour_hook_helper::add_queue<sgg__GUIComponentTextBox__GUIComponentTextBox_dctor>("sgg__GUIComponentTextBox__GUIComponentTextBox_dctor", GUIComponentTextBox_dctor);
			}
		}

		{
			static auto GUIComponentButton_OnSelected_ptr =
			    big::hades2_symbol_to_address["sgg::GUIComponentButton::OnSelected"];
			if (GUIComponentButton_OnSelected_ptr)
			{
				static auto GUIComponentButton_OnSelected = GUIComponentButton_OnSelected_ptr;
				static auto hook_ = hooking::detour_hook_helper::add_queue<hook_GUIComponentButton_OnSelected>(
				    "GUIComponentButton_OnSelected",
				    GUIComponentButton_OnSelected);
			}
		}

		{
			static auto read_anim_data_ptr =
			    big::hades2_symbol_to_address["sgg::GameDataManager::ReadAllAnimationData"];
			if (read_anim_data_ptr)
			{
				static auto read_anim_data = read_anim_data_ptr.as_func<void()>();

				static auto hook_ =
				    hooking::detour_hook_helper::add_queue<hook_ReadAllAnimationData>("ReadAllAnimationData Hook", read_anim_data);
			}
		}

		{
			static auto read_anim_data_ptr =
			    big::hades2_symbol_to_address["sgg::Granny3D::LoadAllModelAndAnimationData"];
			if (read_anim_data_ptr)
			{
				static auto read_anim_data = read_anim_data_ptr.as_func<void()>();

				static auto hook_ = hooking::detour_hook_helper::add_queue<hook_LoadAllModelAndAnimationData>(
				    "LoadAllModelAndAnimationData Hook",
				    read_anim_data);
			}
		}

		{
			static auto hook_ =
			    hooking::detour_hook_helper::add_queue<hook_PlayerHandleInput>("Player HandleInput Hook", big::hades2_symbol_to_address["sgg::Player::HandleInput"]);
		}

		{
			static auto hook_ =
			    hooking::detour_hook_helper::add_queue<hook_ConfigOption_registerField_bool>("registerField<bool> hook", big::hades2_symbol_to_address["sgg::registerField<bool>"]);
		}

				{
			static auto hook_ =
			    hooking::detour_hook_helper::add_queue<hook_sgg_ScriptManager_InitLua>("", big::hades2_symbol_to_address["sgg::ScriptManager::InitLua"]);

			static auto hook2_ = hooking::detour_hook_helper::add_queue<hook_sgg_App_Initialize>("", big::hades2_symbol_to_address["sgg::App::Initialize"]);

			// ; sgg::ThreadBumpAllocator *__fastcall sgg::App::GetThreadFrameAllocator(sgg::App *this)
			// Extend the allocator max size cause mods hit the limit otherwise.
			constexpr size_t original_threadframe_allocator_size             = 0x03'20'00;
			constexpr size_t extended_threadframe_allocator_size_20mb_in_hex = 0x01'40'00'00;

			const auto patch_1            = gmAddress::scan("B9 00 20 03 00", "mov     ecx, 32000h     ; Size");
			if (patch_1)
			{
				LOG(INFO) << "Patch 1 for sgg::App::GetThreadFrameAllocator to have larger size.";

				ForceWrite<uint32_t>(*patch_1.offset(1).as<uint32_t *>(), extended_threadframe_allocator_size_20mb_in_hex);
			}
			const auto patch_2 = gmAddress::scan("48 C7 43 18 00 20 03 00", "mov     qword ptr [rbx+18h], 32000h");
			if (patch_2)
			{
				LOG(INFO) << "Patch 2 for sgg::App::GetThreadFrameAllocator to have larger size.";

				ForceWrite<uint32_t>(*patch_2.offset(4).as<uint32_t *>(), extended_threadframe_allocator_size_20mb_in_hex);
			}
		}

		{
			static auto hook_ =
			    hooking::detour_hook_helper::add_queue<hook_PlatformAnalytics_Start>("PlatformAnalytics Start", big::hades2_symbol_to_address["sgg::PlatformAnalytics::Start"]);
		}

		{
			static auto hook_ =
			    hooking::detour_hook_helper::add_queue<hook_sgg_DetectFreezeThread>("hook_sgg_DetectFreezeThread", big::hades2_symbol_to_address["sgg::DetectFreezeThread"]);
		}

		{
			init_import_hooks();
		}

		{
			static auto ptr = big::hades2_symbol_to_address["sgg::LaunchBugReporter"];
			if (ptr)
			{
				static auto ptr_func = ptr;

				static auto hook_ = hooking::detour_hook_helper::add_queue<hook_disable_f10_launch>(
				    "sgg::LaunchBugReporter F10 Disabler Hook",
				    ptr_func);
			}
		}

		{
			static auto ptr = big::hades2_symbol_to_address["fsGetFilesWithExtension"];
			if (ptr)
			{
				static auto ptr_func = ptr;

				static auto hook_ = hooking::detour_hook_helper::add_queue<hook_fsGetFilesWithExtension_packages>(
				    "fsGetFilesWithExtension for packages and models",
				    ptr_func);
			}
		}

		{
			static auto ptr = big::hades2_symbol_to_address["ParseLuaErrorMessageAndAssert"];
			if (ptr)
			{
				static auto ptr_func = ptr;

				static auto hook_ = hooking::detour_hook_helper::add_queue<hook_ParseLuaErrorMessageAndAssert>(
				    "ParseLuaErrorMessageAndAssert for better lua stack traces",
				    ptr_func);
			}
		}

		/*{
			static auto ptr = gmAddress::scan("E8 ? ? ? ? 90 49 8B CF", "ReadCSString");
			if (ptr)
			{
				static auto ptr_func = ptr.get_call();

				static auto hook_ =
				    hooking::detour_hook_helper::add<hook_ReadCSString>("ReadCSString for packages guid check", ptr_func);
			}
		}*/

		{
			static auto fsAppendPathComponent_ptr = big::hades2_symbol_to_address["fsAppendPathComponent"];
			if (fsAppendPathComponent_ptr)
			{
				static auto fsAppendPathComponent = fsAppendPathComponent_ptr.as_func<void(const char *, const char *, char *)>();

				static auto hook_once = big::hooking::detour_hook_helper::add_queue<hook_fsAppendPathComponent_packages>(
				    "hook_fsAppendPathComponent for packages and models",
				    fsAppendPathComponent);
			}
			else
			{
				LOG(ERROR) << "hook_fsAppendPathComponent for packages and models failure";
			}
		}

		/*big::hooking::detour_hook_helper::add_now<hook_SGD_Deserialize_ThingDataDef>(
		    "void __fastcall sgg::SGD_Deserialize(sgg::SGD_Context *ctx, int loc, sgg::ThingDataDef *val)",
		    gmAddress::scan("44 88 74 24 21", "SGD_Deserialize ThingData").offset(-0x59));*/

		big::hooking::detour_hook_helper::execute_queue();

		DisableThreadLibraryCalls(hmod);
		g_main_thread = CreateThread(
		    nullptr,
		    0,
		    [](PVOID) -> DWORD
		    {
			    std::filesystem::path root_folder = paths::get_project_root_folder();
			    g_file_manager.init(root_folder);
			    paths::init_dump_file_path();

			    big::config::init_general();

			    auto logger_instance = std::make_unique<logger>(rom::g_project_name, g_file_manager.get_project_file("./LogOutput.log"));
			    static struct logger_cleanup
			    {
				    ~logger_cleanup()
				    {
					    Logger::Destroy();
				    }
			    } g_logger_cleanup;

			    LOG(INFO) << rom::g_project_name;
			    LOGF(INFO, "Build (GIT SHA1): {}", version::GIT_SHA1);

			    // TODO: move this to own file, make sure it's called early enough so that it happens before the initial GameReadData call.
			    for (const auto &entry :
			         std::filesystem::recursive_directory_iterator(g_file_manager.get_project_folder("plugins_data").get_path(), std::filesystem::directory_options::skip_permission_denied | std::filesystem::directory_options::follow_directory_symlink))
			    {
				    if (entry.path().extension() == ".pkg" || entry.path().extension() == ".pkg_manifest")
				    {
					    additional_package_files.emplace((char *)entry.path().filename().u8string().c_str(),
					                                     (char *)entry.path().u8string().c_str());

					    LOG(INFO) << "Adding to package files: " << (char *)entry.path().u8string().c_str();
				    }
				    else if (ends_with((char *)entry.path().u8string().c_str(), ".gpk"))
				    {
					    additional_granny_files.emplace((char *)entry.path().filename().u8string().c_str(),
					                                    (char *)entry.path().u8string().c_str());

					    LOG(INFO) << "Adding to granny files: " << (char *)entry.path().u8string().c_str();
				    }
			    }

				const auto res = lovely_init((const char *)g_file_manager.get_project_folder("plugins").get_path().u8string().c_str());
			    LOG(INFO) << "lovely_init returned " << (int32_t)res;

			    //static auto ptr_for_cave_test =
			    //  gmAddress::scan("E8 ? ? ? ? EB 11 41 80 7D ? ?", "ptr_for_cave_test").get_call().offset(0x37);

			    // config test
			    if (0)
			    {
				    auto cfg_file = toml_v2::config_file(
				        (char *)g_file_manager.get_project_file("./Hell2Modding-Hell2Modding_TEST.cfg")
				            .get_path()
				            .u8string()
				            .c_str(),
				        true,
				        "Hell2Modding-Hell2Modding");

				    {
					    auto my_configurable_value = cfg_file.bind("My Section Name 1", "My Configurable Value 1", false, "This is my configurable value.\nLet's test some stuff\n Shall we");

					    LOG(INFO) << "Value of my_configurable_value: " << my_configurable_value->get_value();

					    my_configurable_value->set_value(true);

					    LOG(INFO) << "Value of my_configurable_value 2: " << my_configurable_value->get_value();
				    }

				    {
					    auto my_configurable_value = cfg_file.bind("My Section Name 1", "My Configurable Value 2", "this is another str", "This is my configurable value.\nLet's test some stuff\n Shall we");

					    LOG(INFO) << "Value of my_configurable_value: " << my_configurable_value->get_value();

					    my_configurable_value->set_value("the another str got a new value");

					    LOG(INFO) << "Value of my_configurable_value 2: " << my_configurable_value->get_value();
				    }

				    {
					    auto my_configurable_value = cfg_file.bind("AAAAAMy Section Name 1", "My Configurable Value 1", 149, "This is my configurable value.\nLet's test some stuff\n Shall we");

					    LOG(INFO) << "Value of my_configurable_value: " << my_configurable_value->get_value();

					    my_configurable_value->set_value(169);

					    LOG(INFO) << "Value of my_configurable_value 2: " << my_configurable_value->get_value();
				    }

				    {
					    auto my_configurable_value = cfg_file.bind("ZZZZZZZZZZZZZZZZZZ", "MyConfigurableValue1", "My default value.......", "This is my configurable value.\nLet's test some stuff\n Shall we");

					    LOG(INFO) << "Value of my_configurable_value: " << my_configurable_value->get_value();

					    my_configurable_value->set_value("yyep");

					    LOG(INFO) << "Value of my_configurable_value 2: " << my_configurable_value->get_value();
				    }
			    }


#ifdef FINAL
			    LOG(INFO) << "This is a final build";
#endif

			    auto thread_pool_instance = std::make_unique<thread_pool>();
			    LOG(INFO) << "Thread pool initialized.";

			    auto byte_patch_manager_instance = std::make_unique<byte_patch_manager>();
			    LOG(INFO) << "Byte Patch Manager initialized.";

			    auto hooking_instance = std::make_unique<hooking>();
			    LOG(INFO) << "Hooking initialized.";

			    big::hades::init_hooks();

			    auto renderer_instance = std::make_unique<renderer>();
			    LOG(INFO) << "Renderer initialized.";

			    hotkey::init_hotkeys();

			    if (!g_abort)
			    {
				    g_hooking->enable();
				    LOG(INFO) << "Hooking enabled.";
			    }

			    asi_loader::init(g_hmodule);

			    g_running = true;

			    if (g_abort)
			    {
				    LOG(ERROR) << rom::g_project_name << "failed to init properly, exiting.";
				    g_running = false;
			    }

			    while (g_running)
			    {
				    std::this_thread::sleep_for(500ms);
			    }

			    g_hooking->disable();
			    LOG(INFO) << "Hooking disabled.";

			    // Make sure that all threads created don't have any blocking loops
			    // otherwise make sure that they have stopped executing
			    thread_pool_instance->destroy();
			    LOG(INFO) << "Destroyed thread pool.";

			    hooking_instance.reset();
			    LOG(INFO) << "Hooking uninitialized.";

			    renderer_instance.reset();
			    LOG(INFO) << "Renderer uninitialized.";

			    byte_patch_manager_instance.reset();
			    LOG(INFO) << "Byte Patch Manager uninitialized.";

			    thread_pool_instance.reset();
			    LOG(INFO) << "Thread pool uninitialized.";

			    LOG(INFO) << "Farewell!";
			    logger_instance->destroy();
			    logger_instance.reset();

			    CloseHandle(g_main_thread);
			    FreeLibraryAndExitThread(g_hmodule, 0);
		    },
		    nullptr,
		    0,
		    &g_main_thread_id);
	}

	return true;
}
