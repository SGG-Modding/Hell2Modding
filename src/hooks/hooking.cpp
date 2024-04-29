#include "hooks/hooking.hpp"

#include "gui/gui.hpp"
#include "gui/renderer.hpp"
#include "hades2/disable_sgg_analytics/disable_sgg_analytics.hpp"
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

		al::eLogLevel log_level;
		const char* levelStr;
		switch (level)
		{
		case 8:
			levelStr  = "WARN";
			log_level = WARNING;
			break;
		case 4:
			levelStr  = "INFO";
			log_level = INFO;
			break;
		case 2:
			levelStr  = "DBG";
			log_level = VERBOSE;
			break;
		case 16:
			levelStr  = "ERR";
			log_level = FATAL;
			break;
		default:
			levelStr  = "UNK";
			log_level = INFO;
			break;
		}

		if (strlen(filename) > 41)
		{
			LOG(log_level) << "[" << levelStr << "] [" << (filename + 41) << ":" << line_number << "] " << result;
		}
		else
		{
			LOG(log_level) << "[" << levelStr << "] [" << filename << ":" << line_number << "] " << result;
		}
	}

	void hook_in(lua_State* L)
	{
		/*while (!IsDebuggerPresent())
		{
			Sleep(1000);
		}*/

		std::scoped_lock l(g_lua_manager_mutex);

		g_lua_manager_instance = std::make_unique<lua_manager>(L,
		                                                       g_file_manager.get_project_folder("config"),
		                                                       g_file_manager.get_project_folder("plugins_data"),
		                                                       g_file_manager.get_project_folder("plugins"));


		g_is_lua_state_valid = true;
		LOG(INFO) << "state is valid";
	}

	static sol::optional<sol::environment> env_to_add;

	static int hook_lua_pcallk(lua_State* L, int nargs, int nresults, int errfunc, int ctx, lua_CFunction k)
	{
		if (env_to_add.has_value() && env_to_add.value().valid())
		{
			sol::set_environment(env_to_add.value(), sol::stack_reference(L, -1));
		}

		return big::g_hooking->get_original<hook_lua_pcallk>()(L, nargs, nresults, errfunc, ctx, k);
	}

	static char hook_sgg_ScriptManager_Load(const char* scriptFile)
	{
		if (scriptFile)
		{
			LOG(INFO) << "Game loading lua script: " << scriptFile;

			if (!strcmp(scriptFile, "Main.lua"))
			{
				hook_in(*g_pointers->m_hades2.m_lua_state);
			}

			g_lua_manager->for_each_module(
			    [&](std::unique_ptr<lua_module>& mod)
			    {
				    for (const auto& cb : mod->m_data.m_on_pre_import)
				    {
					    auto res        = cb(scriptFile, env_to_add.has_value() ? env_to_add.value() : sol::lua_nil);
					    auto env_to_set = res.get<sol::optional<sol::environment>>();
					    if (env_to_set && env_to_set.value() && env_to_set.value().valid())
					    {
						    env_to_add = env_to_set;
						    LOG(INFO) << "Setting _ENV for this script to " << mod->guid();
					    }
				    }
			    });
		}

		const auto res = big::g_hooking->get_original<hook_sgg_ScriptManager_Load>()(scriptFile);

		env_to_add = {};

		if (scriptFile)
		{
			g_lua_manager->for_each_module(
			    [&](std::unique_ptr<lua_module>& mod)
			    {
				    for (const auto& cb : mod->m_data.m_on_post_import)
				    {
					    cb(scriptFile);
				    }
			    });
		}

		return res;
	}

	static void hook_luaL_checkversion_(lua_State* L, lua_Number ver)
	{
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

		hooking::detour_hook_helper::add<hook_log_write>("game logger",
		                                                 gmAddress::scan("8B D1 83 E2 08", "game logger").offset(-0x2C).as<void*>());

		const auto backtraceHandleException = gmAddress::scan("B8 B0 FC 00 00", "BacktraceHandleException");
		if (backtraceHandleException)
		{
			hooking::detour_hook_helper::add<hook_sgg_BacktraceHandleException>("Suppress SGG BacktraceHandleException",
			                                                                    backtraceHandleException.offset(-0x20));
		}

		hooking::detour_hook_helper::add<hook_sgg_ForgeRenderer_PrintErrorMessageAndAssert>(
		    "sgg_ForgeRenderer_PrintErrorMessageAndAssert",
		    gmAddress::scan("48 63 44 24 34", "sgg_ForgeRenderer_PrintErrorMessageAndAssert").offset(-0x97));

		// Lua stuff
		{
			hooking::detour_hook_helper::add<hook_lua_pcallk>("lua_pcallk",
			                                                  gmAddress::scan("75 05 44 8B D6", "lua_pcallk").offset(-0x1D));

			hooking::detour_hook_helper::add<hook_sgg_ScriptManager_Load>(
			    "ScriptManager_Load",
			    gmAddress::scan("49 3B DF 76 29", "ScriptManager_Load").offset(-0x6E));

			hooking::detour_hook_helper::add<hook_luaL_checkversion_>("Multiple Lua VM detected patch", luaL_checkversion_);
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
