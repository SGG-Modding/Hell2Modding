#include "luasocket.hpp"

extern "C"
{
	int luaopen_socket_core(lua_State* L);
	int luaopen_mime_core(lua_State* L);
	int luaopen_luasocket_scripts(lua_State* L);
}

namespace lua::luasocket
{
	// Lua API: Table
	// Name: socket
	// Table containing the socket library. Documentation here: https://tst2005.github.io/lua-socket/socket.html
	// Can also be accessed through `require`

	// Lua API: Table
	// Name: mime
	// Table containing the mime library. Documentation here: https://tst2005.github.io/lua-socket/mime.html
	// Can also be accessed through `require`

	void bind(sol::table& state)
	{
		luaL_requiref(state.lua_state(), "socket.core", luaopen_socket_core, 1);
		lua_pop(state.lua_state(), 1);

		auto sv = sol::state_view(state.lua_state());

		state["socket.core"] = sv["socket.core"];
		sv["socket.core"]    = sol::lua_nil;

		luaL_requiref(state.lua_state(), "mime.core", luaopen_mime_core, 1);
		lua_pop(state.lua_state(), 1);

		state["mime.core"] = sv["mime.core"];
		sv["mime.core"]    = sol::lua_nil;

		lua_pushcfunction(state.lua_state(), luaopen_luasocket_scripts);
		lua_call(state.lua_state(), 0, 0);
	}
} // namespace lua::luasocket
