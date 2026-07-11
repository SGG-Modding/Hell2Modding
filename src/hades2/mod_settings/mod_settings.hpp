#pragma once

#include <string>

namespace big::mod_settings
{
	void register_hooks();
	void bind_config_api(sol::state_view& state, sol::table& lua_ext);

	// True if a mod author declared this setting as requiring a game restart to take effect
	// (via `restart_required = true` in the setting's config.lua description). Populated by
	// rom.mod_settings.load; consulted by the settings menu when a value changes.
	bool setting_requires_restart(const std::string& guid, const std::string& section, const std::string& key);
} // namespace big::mod_settings
