#include "dll_proxy/dll_proxy.hpp"
#include "gui/gui.hpp"
#include "gui/renderer.hpp"
#include "hades2/hooks.hpp"
#include "hooks/hooking.hpp"
#include "logger/exception_handler.hpp"
#include "ltable.h"
#include "lua/lua_manager.hpp"
#include "memory/byte_patch_manager.hpp"
#include "paths/paths.hpp"
#include "pointers.hpp"
#include "threads/thread_pool.hpp"
#include "threads/util.hpp"
#include "version.hpp"

#include <lua_extensions/bindings/hades/hades_ida.hpp>
#include <lua_extensions/bindings/tolk/tolk.hpp>
#include <memory/gm_address.hpp>

//#include "debug/debug.hpp"

static bool hook_skipcrashpadinit()
{
	LOG(INFO) << "Skipping crashpad init";
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

static void hook_luaH_free(lua_State *L, Table *t)
{
	static auto hades_func = gmAddress::scan("E8 ?? ?? ?? ?? E9 AB 00 00 00 48 8B D3", "hades_luaH_free").get_call().as_func<void(lua_State *, Table *)>();
	return hades_func(L, t);
}

static __int64 hook_luaH_getn(Table *t)
{
	static auto hades_func = gmAddress::scan("48 8B E9 85 DB", "hades_luaH_getn").offset(-0xD).as_func<__int64(Table *)>();
	return hades_func(t);
}

static Table *hook_luaH_new(lua_State *L)
{
	static auto hades_func = gmAddress::scan("44 8D 43 40 E8", "hades_luaH_new").offset(-0x12).as_func<Table *(lua_State *)>();
	return hades_func(L);
}

static TValue *hook_luaH_newkey(lua_State *L, Table *t, const TValue *key)
{
	static auto hades_func = gmAddress::scan("83 F8 03 75 15", "hades_luaH_newkey").offset(-0x25).as_func<TValue *(lua_State *, Table *, const TValue *)>();
	return hades_func(L, t, key);
}

static void hook_luaH_resize(lua_State *L, Table *t, int a3, int a4)
{
	static auto hades_func = gmAddress::scan("44 3B EF 7E 6A", "hades_luaH_resize").offset(-0x47).as_func<void(lua_State *, Table *, int, int)>();
	return hades_func(L, t, a3, a4);
}

static void hook_luaH_resizearray(lua_State *L, Table *t, int a3)
{
	static auto hades_func = gmAddress::scan("E8 ?? ?? ?? ?? 4C 63 FB", "hades_luaH_resizearray").get_call().as_func<void(lua_State *, Table *, int)>();
	return hades_func(L, t, a3);
}

static __int64 hook_setnodevector(lua_State *L, __int64 a2, int a3)
{
	static auto hades_func = gmAddress::scan("45 85 C0 75 15", "hades_setnodevector").offset(-0x1E).as_func<__int64(lua_State *, __int64, int)>();
	return hades_func(L, a2, a3);
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

struct GUIComponentButton
{
	char m_pad[0x6'68];
	GUIComponentTextBox *mTextBox;
};

static void hook_GUIComponentButton_OnSelected(GUIComponentTextBox *this_, GUIComponentTextBox *prevSelection)
{
	big::g_hooking->get_original<hook_GUIComponentButton_OnSelected>()(this_, prevSelection);

	g_currently_selected_gui_comp = this_;

	auto gui_button = (GUIComponentButton *)g_currently_selected_gui_comp;
	auto gui_text   = gui_button->mTextBox;

	std::vector<std::string> lines;
	for (auto i = gui_text->mLines.mpBegin; i < gui_text->mLines.mpEnd; i++)
	{
		const auto text = i->mText.get_text();
		if (text.size())
		{
			lines.push_back(text);
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

static void hook_HandleInput(void *this_, float elapsedSeconds)
{
	if (!big::g_gui || !big::g_gui->is_open())
	{
		big::g_hooking->get_original<hook_HandleInput>()(this_, elapsedSeconds);
	}
}

BOOL APIENTRY DllMain(HMODULE hmod, DWORD reason, PVOID)
{
	using namespace big;

	if (reason == DLL_PROCESS_ATTACH)
	{
		if (!rom::is_rom_enabled())
		{
			return true;
		}

		// Lua API: Namespace
		// Name: rom
		rom::init("Hell2Modding", "Hades2.exe", "rom");

		// This will inevitably break when the game release on game pass or some other platforms.
		/*const auto steam_env_env_var     = _wgetenv(L"SteamEnv");
		const std::wstring good_steam_env_var = L"1";
		if (!steam_env_env_var || steam_env_env_var != good_steam_env_var)
		{
			return true;
		}*/

		// Purposely leak it, we are not unloading this module in any case.
		auto exception_handling = new exception_handler();

		big::hooking::detour_hook_helper::add_now<hook_initRenderer>(
		    "initRenderer",
		    gmAddress::scan("C1 E0 07 33 C1", "initRenderer").offset(-0x83).as<void *>());

		big::hooking::detour_hook_helper::add_now<hook_skipcrashpadinit>(
		    "backtrace::initializeCrashpad",
		    gmAddress::scan("74 13 48 8B C8", "backtrace::initializeCrashpad").offset(-0x4C).as_func<bool()>());

		// If that block fails lua will crash.
		// This is because lua dummynode_ is a static variable and its address is used in various lua table checks.
		// Since the target game statically link against lua, we have to do it too,
		// a duplicate dummynode_ is made, and will eventually get out of sync.
		{
			// clang-format off
			big::hooking::detour_hook_helper::add_now<hook_luaH_free>("luaH_free", gmAddress::scan_me("48 39 41 20 74 3A", "our luaH_free").offset(-0x1B));
			big::hooking::detour_hook_helper::add_now<hook_luaH_getn>("luaH_getn", gmAddress::scan_me("83 F8 01 76 49", "our luaH_getn").offset(-0x51));
			big::hooking::detour_hook_helper::add_now<hook_luaH_newkey>("luaH_newkey", gmAddress::scan_me("75 12 48 8D 05", "our luaH_newkey").offset(-0x95));
			big::hooking::detour_hook_helper::add_now<hook_luaH_resize>("luaH_resize", gmAddress::scan_me("0F 8D E5 00 00 00", "our luaH_resize").offset(-0x86));
			big::hooking::detour_hook_helper::add_now<hook_luaH_resizearray>("luaH_resizearray", gmAddress::scan_me("48 39 41 20 75 0A", "our luaH_resizearray").offset(-0x20));
			big::hooking::detour_hook_helper::add_now<hook_setnodevector>("setnodevector", gmAddress::scan_me("39 44 24 24 7D 3E", "our setnodevector").offset(-0xE6));
			// clang-format on
		}

		/*{
			static auto GUIComponentTextBox_ctor_ptr = gmAddress::scan("89 BB 2C 06 00 00", "sgg::GUIComponentTextBox::GUIComponentTextBox");
			if (GUIComponentTextBox_ctor_ptr)
			{
				static auto GUIComponentTextBox_ctor = GUIComponentTextBox_ctor_ptr.offset(-0x3B);
				static auto hook_ = hooking::detour_hook_helper::add_now<sgg__GUIComponentTextBox__GUIComponentTextBox>("sgg__GUIComponentTextBox__GUIComponentTextBox", GUIComponentTextBox_ctor);
			}
		}*/

		{
			static auto GUIComponentTextBox_update_ptr = gmAddress::scan("76 30 8B 43 74", "sgg::GUIComponentTextBox::Update");
			if (GUIComponentTextBox_update_ptr)
			{
				static auto GUIComponentTextBox_update = GUIComponentTextBox_update_ptr.offset(-0x51);
				static auto hook_ = hooking::detour_hook_helper::add_now<sgg__GUIComponentTextBox__Update>(
				    "sgg__GUIComponentTextBox__Update",
				    GUIComponentTextBox_update);
			}
		}

		{
			static auto GUIComponentTextBox_dctor_ptr = gmAddress::scan("8D 05 ? ? ? ? 48 8B F1 4C 8D B1", "sgg::GUIComponentTextBox::GUIComponentTextBox_dctor");
			if (GUIComponentTextBox_dctor_ptr)
			{
				static auto GUIComponentTextBox_dctor = GUIComponentTextBox_dctor_ptr.offset(-0x19);
				static auto hook_ = hooking::detour_hook_helper::add<sgg__GUIComponentTextBox__GUIComponentTextBox_dctor>("sgg__GUIComponentTextBox__GUIComponentTextBox_dctor", GUIComponentTextBox_dctor);
			}
		}

		{
			static auto GUIComponentButton_OnSelected_ptr = gmAddress::scan("8B D9 E8 ? ? ? ? 80 BB AA", "sgg::GUIComponentButton::OnSelected");
			if (GUIComponentButton_OnSelected_ptr)
			{
				static auto GUIComponentButton_OnSelected = GUIComponentButton_OnSelected_ptr.offset(-0x7);
				static auto hook_ = hooking::detour_hook_helper::add<hook_GUIComponentButton_OnSelected>(
				    "GUIComponentButton_OnSelected",
				    GUIComponentButton_OnSelected);
			}
		}

		{
			static auto read_anim_data_ptr = gmAddress::scan("BA 2A 00 00 00", "ReadAllAnimationData");
			if (read_anim_data_ptr)
			{
				static auto read_anim_data = read_anim_data_ptr.offset(-0x1'97).as_func<void()>();

				static auto hook_ =
				    hooking::detour_hook_helper::add<hook_ReadAllAnimationData>("ReadAllAnimationData Hook", read_anim_data);
			}
		}

		{
			static auto hook_ = hooking::detour_hook_helper::add<hook_HandleInput>("HandleInput Hook", gmAddress::scan("40 53 41 56 41 57 48 83 EC 30", "HandleInput"));
		}

		/*big::hooking::detour_hook_helper::add_now<hook_SGD_Deserialize_ThingDataDef>(
		    "void __fastcall sgg::SGD_Deserialize(sgg::SGD_Context *ctx, int loc, sgg::ThingDataDef *val)",
		    gmAddress::scan("44 88 74 24 21", "SGD_Deserialize ThingData").offset(-0x59));*/

		dll_proxy::init();

		DisableThreadLibraryCalls(hmod);
		g_hmodule     = hmod;
		g_main_thread = CreateThread(
		    nullptr,
		    0,
		    [](PVOID) -> DWORD
		    {
			    // https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/setlocale-wsetlocale?view=msvc-170#utf-8-support
			    setlocale(LC_ALL, ".utf8");
			    // This also change things like stringstream outputs and add comma to numbers and things like that, we don't want that, so just set locale on the C apis instead.
			    //std::locale::global(std::locale(".utf8"));

			    std::filesystem::path root_folder = paths::get_project_root_folder();
			    g_file_manager.init(root_folder);
			    paths::init_dump_file_path();

			    constexpr auto is_console_enabled = true;
			    auto logger_instance = std::make_unique<logger>(rom::g_project_name, g_file_manager.get_project_file("./LogOutput.log"), is_console_enabled);

			    LOG(INFO) << rom::g_project_name;
			    LOGF(INFO, "Build (GIT SHA1): {}", version::GIT_SHA1);

#ifdef FINAL
			    LOG(INFO) << "This is a final build";
#endif

			    auto thread_pool_instance = std::make_unique<thread_pool>();
			    LOG(INFO) << "Thread pool initialized.";

			    auto pointers_instance = std::make_unique<pointers>();
			    LOG(INFO) << "Pointers initialized.";

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

			    g_running = true;

			    if (g_abort)
			    {
				    LOG(FATAL) << rom::g_project_name << "failed to init properly, exiting.";
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

			    pointers_instance.reset();
			    LOG(INFO) << "Pointers uninitialized.";

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
