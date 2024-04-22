#include "sgg_lua.hpp"

#include "threads/util.hpp"

#include <hooks/hooking.hpp>
#include <lua/lua_manager.hpp>
#include <memory/gm_address.hpp>
#include <pointers.hpp>

namespace big
{
	static std::unique_ptr<lua_manager> lua_manager_instance;

	void hook_luaL_checkversion_(lua_State* L, lua_Number ver)
	{
	}

	void hook_sgg_ScriptManager_Clear()
	{
		threads::suspend_all_but_one();

		lua_manager_instance.reset();
		LOG(INFO) << "Lua manager reset.";

		big::g_hooking->get_original<hook_sgg_ScriptManager_Clear>()();
	}

	void hook_InitLua()
	{
		big::g_hooking->get_original<hook_InitLua>()();

		lua_manager_instance = std::make_unique<lua_manager>(*g_pointers->m_hades2.m_lua_state,
		                                                     g_file_manager.get_project_folder("config"),
		                                                     g_file_manager.get_project_folder("plugins_data"),
		                                                     g_file_manager.get_project_folder("plugins"));
		LOG(INFO) << "Lua manager initialized.";

		threads::resume_all();
	}
} // namespace big
