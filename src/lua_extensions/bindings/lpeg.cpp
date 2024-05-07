#include "lpeg.hpp"

extern "C"
{
	int luaopen_lpeg(lua_State* L);
}

namespace lua::lpeg
{
	// Lua API: Table
	// Name: lpeg
	// Table containing the lpeb library. Doc here https://www.inf.puc-rio.br/~roberto/lpeg/lpeg.html

	void bind(sol::table& state)
	{
		luaL_requiref(state.lua_state(), "lpeg", luaopen_lpeg, 1);
		lua_pop(state.lua_state(), 1);

		auto sv       = sol::state_view(state.lua_state());
		state["lpeg"] = sv["lpeg"];
		sv["lpeg"]    = sol::lua_nil;
	}
} // namespace lua::lpeg
