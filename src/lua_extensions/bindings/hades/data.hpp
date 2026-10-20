#pragma once

namespace lua::hades::data
{
	extern unsigned int get_hash_guid_from_string(std::string str);

	void bind(sol::state_view &state, sol::table &lua_ext);
}
