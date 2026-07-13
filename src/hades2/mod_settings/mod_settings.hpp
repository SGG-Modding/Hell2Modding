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
	// field is an author-only input that cannot be inferred from the config value; the widget kind
	// itself is inferred from the value and `values`. All fields are optional (see the has_* flags).
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
		bool freetext         = false; // force a bounded number to freetext entry (not the stepper)

		// Number-display options (mainly for the slider). is_percentage shows a 0..1 value as 0..100 and
		// appends "%"; show_as_percentage only appends "%" (no scaling). Setting show_as_percentage in
		// addition to is_percentage is a no-op. The stored config value is never modified by either.
		bool show_as_percentage = false;
		bool is_percentage      = false;
	};

	// True if a mod author declared this setting as requiring a game restart to take effect
	// (via `restart_required = true` in the setting's config.lua description). Populated by
	// rom.mod_settings.load; consulted by the settings menu when a value changes.
	bool setting_requires_restart(const std::string& guid, const std::string& section, const std::string& key);

	// Returns the author-declared metadata for a setting, or std::nullopt when the setting has no
	// rich metadata table (in which case the menu renders it with type-based defaults).
	std::optional<setting_metadata> get_setting_metadata(const std::string& guid, const std::string& section, const std::string& key);

	// Rank of a setting's definition in its config.lua source (0 = first). Used to order rows that
	// have no author-declared `order` in config-file order. Returns INT_MAX for keys not bound via
	// rom.mod_settings.load (e.g. Chalk-bound), so they fall back to the config map order.
	int get_setting_appearance_order(const std::string& guid, const std::string& section, const std::string& key);

	// Returns the config.lua default (serialized like the config entry's value) for a setting bound
	// via rom.mod_settings.load, or std::nullopt for keys with no captured default. Used by the
	// settings menu's Reset action to restore a setting to what config.lua declared.
	std::optional<std::string> get_setting_default(const std::string& guid, const std::string& section, const std::string& key);
} // namespace big::mod_settings
