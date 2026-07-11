#pragma once

#include <optional>
#include <string>
#include <vector>

namespace big::mod_settings
{
	void register_hooks();
	void bind_config_api(sol::state_view& state, sol::table& lua_ext);

	// Author-declared metadata for a single setting, extracted from its config.lua description
	// table by rom.mod_settings.load and consulted by the settings menu. Only settings whose
	// description is a rich table have an entry; the rest fall back to type-based rendering. Every
	// field is an author-only input that cannot be inferred from the config value (the widget kind
	// itself IS inferred from the value + `values`, so there is deliberately no `type` field here).
	// All fields are optional (see the has_* flags).
	struct setting_metadata
	{
		std::string name;        // display-name override (empty -> prettified key)
		std::string description; // same text written to the .cfg comment

		bool has_min  = false;
		double min    = 0.0;
		bool has_max  = false;
		double max    = 0.0;
		bool has_step = false;
		double step   = 0.0;

		// Enum options: serialized option values and parallel display labels (labels default to
		// the values when omitted). Serialized form matches the config entry's serialization.
		std::vector<std::string> values;
		std::vector<std::string> labels;

		bool has_order = false;
		double order   = 0.0; // author-declared sort key (lower first); unset -> map order

		bool hidden           = false; // author asked to omit this row entirely
		bool restart_required = false; // change only takes effect after a game restart
	};

	// True if a mod author declared this setting as requiring a game restart to take effect
	// (via `restart_required = true` in the setting's config.lua description). Populated by
	// rom.mod_settings.load; consulted by the settings menu when a value changes.
	bool setting_requires_restart(const std::string& guid, const std::string& section, const std::string& key);

	// Returns the author-declared metadata for a setting, or std::nullopt when the setting has no
	// rich metadata table (in which case the menu renders it with type-based defaults).
	std::optional<setting_metadata> get_setting_metadata(const std::string& guid, const std::string& section, const std::string& key);
} // namespace big::mod_settings
