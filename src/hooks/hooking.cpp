#include "hooks/hooking.hpp"

#include "gui/gui.hpp"
#include "gui/renderer.hpp"
#include "hades2/disable_sgg_analytics/disable_sgg_analytics.hpp"
#include "hades2/lua/sgg_lua.hpp"
#include "hades2/sgg_exception_handler/disable_sgg_handler.hpp"
#include "memory/module.hpp"
#include "pointers.hpp"
#include "threads/util.hpp"

#include <cstdarg>
#include <lua/bindings/gui.hpp>
#include <lua/bindings/imgui.hpp>
#include <lua/bindings/log.hpp>
#include <lua/lua_manager.hpp>
#include <memory/gm_address.hpp>
#include <polyhook2/pe/IatHook.hpp>

namespace big
{
	static void hook_tg(void* this_, __int64 arg)
	{
		{
			std::scoped_lock l(g_lua_manager_mutex);
		}
		{
			big::g_hooking->get_original<hook_tg>()(this_, arg);
		}
		{
			std::scoped_lock l(g_lua_manager_mutex);
		}
	}

	static void hook_log_write(char level, const char* filename, int line_number, const char* message, ...)
	{
		va_list args;

		va_start(args, message);
		int size = vsnprintf(nullptr, 0, message, args);
		va_end(args);

		// Allocate a buffer to hold the formatted string
		std::string result(size + 1, '\0'); // +1 for the null terminator

		// Format the string into the buffer
		va_start(args, message);
		vsnprintf(&result[0], size + 1, message, args);
		va_end(args);

		big::g_hooking->get_original<hook_log_write>()(level, filename, line_number, result.c_str());

		result.pop_back();

		const char* levelStr;
		switch (level)
		{
		case 8:  levelStr = "WARN"; break;
		case 4:  levelStr = "INFO"; break;
		case 2:  levelStr = "DBG"; break;
		case 16: levelStr = "ERR"; break;
		default: levelStr = "UNK"; break;
		}

		if (strlen(filename) > 41)
		{
			LOG(INFO) << "[" << levelStr << "] [" << (filename + 41) << ":" << line_number << "] " << result;
		}
		else
		{
			LOG(INFO) << "[" << levelStr << "] [" << filename << ":" << line_number << "] " << result;
		}
	}

	static std::unique_ptr<sol::state_view> g_lua_state_view;

	void delete_everything()
	{
		std::scoped_lock l(g_lua_manager_mutex);

		g_is_lua_state_valid = false;
		g_lua_modules.clear();
		g_lua_state_view.reset();
		LOG(FATAL) << "state is no longer valid!";
	}

	int the_state_is_going_down(lua_State* L)
	{
		delete_everything();

		return 0;
	}

	void hook_in(lua_State* L)
	{
		std::scoped_lock l(g_lua_manager_mutex);

		g_lua_state_view = std::make_unique<sol::state_view>(L);

		const std::string my_inscrutable_key =
		    "..catchy_id.\xF0\x9F\x8F\xB4 \xF0\x9F\x8F\xB4 \xF0\x9F\x8F\xB4 \xF0\x9F\x8F\xB4 \xF0\x9F\x8F\xB4";
		sol::table my_takedown_metatable                           = g_lua_state_view->create_table_with();
		my_takedown_metatable[sol::meta_function::garbage_collect] = the_state_is_going_down;
		sol::table my_takedown_table = g_lua_state_view->create_named_table(my_inscrutable_key, sol::metatable_key, my_takedown_metatable);

		sol::table g_table = g_lua_state_view->globals();
		//lua::gui::bind(g_table);
		g_table.create_named("gui").set_function("add_imgui",
		                                         [](sol::protected_function cb)
		                                         {
			                                         for (auto& lua_mod : g_lua_modules)
			                                         {
				                                         if (lua_mod.m_file_entry == g_lua_current_guid)
				                                         {
					                                         lua_mod.m_imgui_callbacks.push_back(cb);
				                                         }
			                                         }
		                                         });
		lua::imgui::bind(g_table);
		//lua::log::bind(g_table);

		const auto script_folder = g_file_manager.get_project_folder("plugins");
		for (const auto& entry : std::filesystem::recursive_directory_iterator(script_folder.get_path(), std::filesystem::directory_options::skip_permission_denied))
		{
			if (!entry.exists() || entry.path().extension() != ".lua")
			{
				continue;
			}

			g_lua_modules.push_back({.m_file_entry = entry, .m_imgui_callbacks = {}});
			g_lua_current_guid = entry;
			auto result = g_lua_state_view->safe_script_file((char*)entry.path().u8string().c_str(), &sol::script_pass_on_error, sol::load_mode::text);

			if (!result.valid())
			{
				LOG(FATAL) << (char*)entry.path().u8string().c_str() << " failed to load: " << result.get<sol::error>().what();
				Logger::FlushQueue();

				g_lua_modules.pop_back();
			}
		}

		g_is_lua_state_valid = true;
		LOG(FATAL) << "state is valid";
	}

	static char hook_sgg_ScriptManager_Load(const char* scriptFile)
	{
		if (!strcmp(scriptFile, "Main.lua"))
		{
			hook_in(*g_pointers->m_hades2.m_lua_state);
		}

		return big::g_hooking->get_original<hook_sgg_ScriptManager_Load>()(scriptFile);
	}

	hooking::hooking()
	{
		for (auto& detour_hook_helper : m_detour_hook_helpers)
		{
			const auto is_lazy_hook = detour_hook_helper.m_on_hooking_available.operator bool();
			if (is_lazy_hook)
			{
				detour_hook_helper.m_detour_hook->set_target_and_create_hook(detour_hook_helper.m_on_hooking_available());
			}
		}

		hooking::detour_hook_helper::add<hook_log_write>("game logger", gmAddress::scan("8B D1 83 E2 08").offset(-0x2C).as<void*>());

		hooking::detour_hook_helper::add<hook_sgg_BacktraceHandleException>("Suppress SGG BacktraceHandleException",
		                                                                    g_pointers->m_hades2.m_sgg_BacktraceHandleException);
		hooking::detour_hook_helper::add<hook_sgg_ForgeRenderer_PrintErrorMessageAndAssert>("HSGGFRPEMAA",
		                                                                                    g_pointers->m_hades2.m_sgg_ForgeRenderer_PrintErrorMessageAndAssert);

		// Lua stuff
		{
			//
			hooking::detour_hook_helper::add<hook_sgg_ScriptManager_Load>(
			    "LC",
			    gmAddress::scan("49 3B DF 76 29").offset(-0x6E).as<void*>());

			/*hooking::detour_hook_helper::add<hook_luaL_checkversion_>("Multiple Lua VM detected patch", luaL_checkversion_);

			hooking::detour_hook_helper::add<hook_InitLua>("LNS", g_pointers->m_hades2.m_init_lua);

			hooking::detour_hook_helper::add<hook_lua_close>(
			    "LC",
			    gmAddress::scan("E8 ? ? ? ? 44 89 3D ? ? ? ? 4C 8D 0D").offset(1).rip().as<void*>());

			hooking::detour_hook_helper::add<hook_sgg_ScriptManager_Clear>("SMC", g_pointers->m_hades2.m_scriptmanager_clear);


			hooking::detour_hook_helper::add<hook_sgg_app_reset>("SGG APP RES", g_pointers->m_hades2.m_sgg_app_reset);

			hooking::detour_hook_helper::add<hook_tg>("worker thread lua safety",
			                                          gmAddress::scan("8B D5 49 8D 4E 10 E8").offset(-0x2A).as<void*>());*/


			//hooking::detour_hook_helper::add<hook_l_alloc>("lua safety 2",
			//gmAddress::scan("33 C0 48 83 C4 28 C3 41").offset(-0x15).as<void*>());

			//hooking::detour_hook_helper::add<hook_malloc>("try stuff 1", malloc);
			//hooking::detour_hook_helper::add<hook_realloc>("try stuff 2", realloc);
			//hooking::detour_hook_helper::add<hook_free>("try stuff 3", free);
		}

		g_hooking = this;
	}

	hooking::~hooking()
	{
		if (m_enabled)
		{
			disable();
		}

		g_hooking = nullptr;
	}

	void hooking::enable()
	{
		threads::suspend_all_but_one();

		for (auto& detour_hook_helper : m_detour_hook_helpers)
		{
			detour_hook_helper.m_detour_hook->enable();
		}

		threads::resume_all();

		m_enabled = true;
	}

	void hooking::disable()
	{
		m_enabled = false;

		threads::suspend_all_but_one();

		for (auto& detour_hook_helper : m_detour_hook_helpers)
		{
			detour_hook_helper.m_detour_hook->disable();
		}

		threads::resume_all();

		m_detour_hook_helpers.clear();
	}

	hooking::detour_hook_helper::~detour_hook_helper()
	{
	}

	void hooking::detour_hook_helper::enable_now()
	{
		m_detour_hook->enable();
	}

	void hooking::detour_hook_helper::enable_hook_if_hooking_is_already_running()
	{
		if (g_hooking && g_hooking->m_enabled)
		{
			if (m_on_hooking_available)
			{
				m_detour_hook->set_target_and_create_hook(m_on_hooking_available());
			}

			m_detour_hook->enable();
		}
	}
} // namespace big
