#pragma once

#include <lua/lua_manager.hpp>

namespace big::lua_manager_extension
{
	void init_lua_manager(sol::state_view& state, sol::table& lua_ext);
	void init_lua_state(sol::state_view& state);
	void init_lua_api(sol::table& lua_ext);

	inline std::recursive_mutex g_manager_mutex;
	inline bool g_is_lua_state_valid = false;
	inline std::unique_ptr<lua_manager> g_lua_manager_instance;
} // namespace big::lua_manager_extension
