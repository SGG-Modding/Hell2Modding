#pragma once

namespace big::mod_settings
{
	void register_hooks();
	void bind_config_api(sol::state_view& state, sol::table& lua_ext);
} // namespace big::mod_settings
