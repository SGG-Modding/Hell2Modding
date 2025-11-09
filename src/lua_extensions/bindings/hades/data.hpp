#pragma once

namespace lua::hades::data
{
	const char *get_string_from_hash_guid(unsigned int hash_guid);
	void bind(sol::state_view &state, sol::table &lua_ext);
}
