#include "config/config.hpp"
#include "dll_proxy/dll_proxy.hpp"
#include "gui/gui.hpp"
#include "gui/renderer.hpp"
#include "hades2/hooks.hpp"
#include "hooks/hooking.hpp"
#include "logger/exception_handler.hpp"
#include "lua/lua_manager.hpp"
#include "memory/byte_patch_manager.hpp"
#include "paths/paths.hpp"
#include "pointers.hpp"
#include "threads/thread_pool.hpp"
#include "threads/util.hpp"
#include "version.hpp"

#include <lua_extensions/bindings/hades/hades_ida.hpp>
#include <lua_extensions/bindings/hades/inputs.hpp>
#include <lua_extensions/bindings/tolk/tolk.hpp>
#include <memory/gm_address.hpp>
#include <new>

//#include "debug/debug.hpp"

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
struct Table;

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

struct TValue;

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

// The api should return a path that has a matching directory file separator with our recursive .pkg file path iterator
// The user should also be forced somehow to use that returned path, and not the one they pass in.
static std::vector<std::string> additional_package_files;

static void hook_fsAppendPathComponent_packages(const char *basePath, const char *pathComponent, char *output /*size: 512*/)
{
	big::g_hooking->get_original<hook_fsAppendPathComponent_packages>()(basePath, pathComponent, output);

	for (const auto &additional_package_file : additional_package_files)
	{
		if (strstr(pathComponent, additional_package_file.c_str()))
		{
			strcpy(output, additional_package_file.c_str());
			break;
		}
	}
}

static void hook_fsGetFilesWithExtension_packages(PVOID resourceDir, const char *subDirectory, wchar_t *extension, eastl::vector<eastl::string> *out)
{
	big::g_hooking->get_original<hook_fsGetFilesWithExtension_packages>()(resourceDir, subDirectory, extension, out);

	for (const auto &xd : *out)
	{
		if (strstr(xd.c_str(), ".pkg"))
		{
			for (const auto &file : additional_package_files)
			{
				out->push_back(file.c_str());
			}

			break;
		}
	}
}

extern "C"
{
	extern void luaH_free(lua_State *L, Table *t);
	extern int luaH_getn(Table *t);
	extern TValue *luaH_newkey(lua_State *L, Table *t, const TValue *key);
	extern void luaH_resize(lua_State *L, Table *t, int nasize, int nhsize);
	extern void luaH_resizearray(lua_State *L, Table *t, int nasize);
	extern Table *luaH_new(lua_State *L);
}

BOOL APIENTRY DllMain(HMODULE hmod, DWORD reason, PVOID)
{
	using namespace big;

	if (reason == DLL_PROCESS_ATTACH)
	{
		dll_proxy::init();

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
		    gmAddress::scan("E8 ? ? ? ? 90 48 8B 05 ? ? ? ? 48 85 C0", "initRenderer").get_call());

		big::hooking::detour_hook_helper::add_now<hook_skipcrashpadinit>(
		    "backtrace::initializeCrashpad",
		    gmAddress::scan("74 13 48 8B C8", "backtrace::initializeCrashpad").offset(-0x4C).as_func<bool()>());


		// If that block fails lua will crash.
		// This is because lua dummynode_ is a static variable and its address is used in various lua table checks.
		// Since the target game statically link against lua, we have to do it too,
		// a duplicate dummynode_ is made, and will eventually get out of sync.
		{
			// clang-format off
			big::hooking::detour_hook_helper::add_now<hook_luaH_free>("luaH_free", &luaH_free);
			big::hooking::detour_hook_helper::add_now<hook_luaH_getn>("luaH_getn", &luaH_getn);
			big::hooking::detour_hook_helper::add_now<hook_luaH_newkey>("luaH_newkey", &luaH_newkey);
			big::hooking::detour_hook_helper::add_now<hook_luaH_resize>("luaH_resize", &luaH_resize);
			big::hooking::detour_hook_helper::add_now<hook_luaH_resizearray>("luaH_resizearray", &luaH_resizearray);
			big::hooking::detour_hook_helper::add_now<hook_luaH_new>("luaH_new", &luaH_new);
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
			//static auto hook_ = hooking::detour_hook_helper::add<hook_HandleInput>("Global HandleInput Hook", gmAddress::scan("40 53 41 56 41 57 48 83 EC 30", "HandleInput"));
			static auto hook_ = hooking::detour_hook_helper::add<hook_PlayerHandleInput>("Player HandleInput Hook", gmAddress::scan("E8 ? ? ? ? 8B 05 ? ? ? ? 90", "Player HandleInput"));
		}

		{
			static auto hook_ = hooking::detour_hook_helper::add_now<hook_ConfigOption_registerField_bool>(
			    "registerField<bool> hook",
			    gmAddress::scan("4C 8B C1 88 5C 24 38", "registerField<bool>").offset(-0x2B));

			//

			static auto hook_analy_start =
			    hooking::detour_hook_helper::add_now<hook_PlatformAnalytics_Start>("PlatformAnalytics Start", gmAddress::scan("4C 8B DC 48 83 EC 48 80 3D", "Analy Start"));
		}

		{
			static auto ptr = gmAddress::scan("E8 ? ? ? ? B8 ? ? ? ? EB 05", "sgg::LaunchBugReporter");
			if (ptr)
			{
				static auto ptr_func = ptr.get_call();

				static auto hook_ = hooking::detour_hook_helper::add<hook_disable_f10_launch>(
				    "sgg::LaunchBugReporter F10 Disabler Hook",
				    ptr_func);
			}
		}

		{
			static auto ptr = gmAddress::scan("E8 ? ? ? ? 48 8B 7D CF", "fsGetFilesWithExtension");
			if (ptr)
			{
				static auto ptr_func = ptr.get_call();

				static auto hook_ = hooking::detour_hook_helper::add<hook_fsGetFilesWithExtension_packages>(
				    "fsGetFilesWithExtension for packages",
				    ptr_func);
			}
		}

		{
			static auto fsAppendPathComponent_ptr = gmAddress::scan("C6 44 24 30 5C", "fsAppendPathComponent");
			if (fsAppendPathComponent_ptr)
			{
				static auto fsAppendPathComponent = fsAppendPathComponent_ptr.offset(-0x97).as_func<void(const char *, const char *, char *)>();

				static auto hook_once = big::hooking::detour_hook_helper::add<hook_fsAppendPathComponent_packages>(
				    "hook_fsAppendPathComponent for packages",
				    fsAppendPathComponent);
			}
		}

		/*big::hooking::detour_hook_helper::add_now<hook_SGD_Deserialize_ThingDataDef>(
		    "void __fastcall sgg::SGD_Deserialize(sgg::SGD_Context *ctx, int loc, sgg::ThingDataDef *val)",
		    gmAddress::scan("44 88 74 24 21", "SGD_Deserialize ThingData").offset(-0x59));*/

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
					    additional_package_files.push_back((char *)entry.path().u8string().c_str());

					    LOG(INFO) << "Adding to package files: " << (char *)entry.path().u8string().c_str();
				    }
			    }

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
