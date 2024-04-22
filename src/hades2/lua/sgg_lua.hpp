#pragma once

namespace big
{
	void hook_luaL_checkversion_(lua_State* L, lua_Number ver);

	void hook_InitLua();

	void hook_sgg_ScriptManager_Clear();
} // namespace big
