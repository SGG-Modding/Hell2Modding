#pragma once

#include "hooks/hooking.hpp"
#include "lua_extensions/lua_manager_extension.hpp"

#include <hades2/pdb_symbol_map.hpp>
#include <lua_extensions/lua_module_ext.hpp>

extern "C"
{
#include <lobject.h>
#include <ltable.h>
}

namespace big::hades::lua
{
	inline void lua_panic_handler_with_dump(sol::optional<std::string> maybe_msg)
	{
		LOG(ERROR) << "Lua is in a panic state and will now abort() the application";
		if (maybe_msg)
		{
			const std::string& msg = maybe_msg.value();
			LOG(ERROR) << "error message: " << msg;
		}
		Logger::FlushQueue();

		*(int*)0xDE'AD = 0;
	}

	inline void hook_in(lua_State* L)
	{
		/*while (!IsDebuggerPresent())
		{
			Sleep(1000);
		}*/

		// must ensure dummynode / luaO_nilobject from the game code and not ours.
		{
			static auto game_index2adr = big::hades2_symbol_to_address["index2addr"].as_func<intptr_t(lua_State*, int)>();
			luaO_nilobject_external_address = game_index2adr(L, 999'999);
			static auto game_luaH_new = big::hades2_symbol_to_address["luaH_new"].as_func<Table*(lua_State*, int, int)>();
			auto game_table            = game_luaH_new(L, 0, 0);
			dummynode_external_address = (intptr_t)game_table->node;
		}

		std::scoped_lock l(lua_manager_extension::g_manager_mutex);

		lua_manager_extension::g_lua_manager_instance = std::make_unique<lua_manager>(
		    L,
		    g_file_manager.get_project_folder("config"),
		    g_file_manager.get_project_folder("plugins_data"),
		    g_file_manager.get_project_folder("plugins"),
		    [](sol::state_view& state, sol::table& lua_ext)
		    {
			    lua_manager_extension::init_lua_manager(state, lua_ext);
		    },
		    [](sol::state_view& state) -> sol::environment
		    {
			    // rom.game = _G
			    state[rom::g_lua_api_namespace]["game"] = state["_G"];

			    // local plugin_G = {}
			    sol::table plugin_G = state.create_table();

			    sol::table all_g = state["_G"];
			    for (const auto& [k, v] : all_g)
			    {
				    if (k.is<const char*>())
				    {
					    auto key_str = k.as<const char*>();
					    // Bad heuristic for filtering out native functions from the game code
					    if (!std::isupper(static_cast<unsigned char>(key_str[0])))
					    {
						    plugin_G[k] = v;
					    }
				    }
				    else
				    {
					    plugin_G[k] = v;
				    }
			    }

			    // plugin_G.rom = rom
			    plugin_G[rom::g_lua_api_namespace] = state[rom::g_lua_api_namespace];

			    // plugin_G._G = plugin_G
			    plugin_G["_G"] = plugin_G;

			    // when you give plugins an _ENV, plugin_G is the __index instead
			    return sol::environment(state, sol::create, plugin_G);
		    });

		lua_manager_extension::g_lua_manager_instance->init<lua_module_ext>(true);

		sol::state_view(lua_manager_extension::g_lua_manager_instance->lua_state()).set_panic(sol::c_call<decltype(&lua_panic_handler_with_dump), &lua_panic_handler_with_dump>);

		lua_manager_extension::g_is_lua_state_valid = true;
		LOG(INFO) << "state is valid";
	}

	inline sol::optional<sol::environment> env_to_add;

	inline int hook_lua_pcallk(lua_State* L, int nargs, int nresults, int errfunc, int ctx, lua_CFunction k)
	{
		if (env_to_add.has_value() && env_to_add.value().valid())
		{
			sol::set_environment(env_to_add.value(), sol::stack_reference(L, -1));
		}

		return big::g_hooking->get_original<hook_lua_pcallk>()(L, nargs, nresults, errfunc, ctx, k);
	}

	inline char hook_sgg_ScriptManager_Load(const char* scriptFile)
	{
		if (scriptFile)
		{
			if (!strcmp(scriptFile, "Main.lua"))
			{
				hook_in(*big::hades2_symbol_to_address["sgg::ScriptManager::LuaInterface"].as<lua_State**>());
			}

			for (const auto& mod_ : g_lua_manager->m_modules)
			{
				auto mod = (lua_module_ext*)mod_.get();
				for (const auto& cb : mod->m_data_ext.m_on_pre_import)
				{
					auto res        = cb(scriptFile, env_to_add.has_value() ? env_to_add.value() : sol::lua_nil);
					auto env_to_set = res.get<sol::optional<sol::environment>>();
					if (env_to_set && env_to_set.value() && env_to_set.value().valid())
					{
						env_to_add = env_to_set;
						LOG(INFO) << "Setting _ENV for this script to " << mod->guid();
					}
				}
			}
		}

		if (scriptFile)
		{
			LOG(DEBUG) << "Game loading lua script: " << scriptFile;
		}
		const auto res = big::g_hooking->get_original<hook_sgg_ScriptManager_Load>()(scriptFile);

		env_to_add = {};

		if (scriptFile)
		{
			for (const auto& mod_ : g_lua_manager->m_modules)
			{
				auto mod = (lua_module_ext*)mod_.get();
				for (const auto& cb : mod->m_data_ext.m_on_post_import)
				{
					cb(scriptFile);
				}
			}
		}

		return res;
	}

	inline void hook_luaL_checkversion_(lua_State* L, lua_Number ver)
	{
	}

	inline void init_hooks()
	{
		hooking::detour_hook_helper::add<hook_lua_pcallk>("lua_pcallk", big::hades2_symbol_to_address["lua_pcallk"]);

		hooking::detour_hook_helper::add<hook_sgg_ScriptManager_Load>("ScriptManager_Load", big::hades2_symbol_to_address["sgg::ScriptManager::Load"]);

		hooking::detour_hook_helper::add<hook_luaL_checkversion_>("Multiple Lua VM detected patch", luaL_checkversion_);
	}
} // namespace big::hades::lua
