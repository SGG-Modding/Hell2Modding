#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace big::mod_settings
{
	void register_hooks();
	void bind_config_api(sol::state_view& state, sol::table& lua_ext);

	// A user-facing string an author may write in config.lua either plainly ("Enable feature") or as a
	// localization table keyed by the game's language folder codes ({ en = "...", de = "...",
	// ["zh-TW"] = "..." }). Stored as language-code -> text, with a plain string kept under the empty
	// key. The settings menu resolves it to the current game language at render time (see
	// resolve_localized), falling back to English then any entry.
	using localized_text = std::map<std::string, std::string>;

	// When a setting may be changed, relative to whether a save is loaded. The Lua state is recreated
	// when a save is loaded from the main menu, so init-time changes (GameData edits, function patches)
	// only take effect if made before that point, while some settings only apply to a live run. The
	// settings menu greys a row (read-only, with a note) when the current context does not match:
	//  - any:       editable anywhere (default; live-read settings).
	//  - main_menu: only from the main menu (greyed while a save is loaded). Forced for a mod's master
	//               "enabled" toggle and for any restart_required setting.
	//  - in_save:   only while a save is loaded (greyed at the main menu).
	// Authors declare this per setting via `editable_context = "main_menu" | "in_save" | "any"`.
	enum class editable_context
	{
		any,
		main_menu,
		in_save,
	};

	// Author-declared metadata for a single setting, extracted from its config.lua description
	// table by rom.mod_settings.load and consulted by the settings menu. Only settings whose
	// description is a rich table have an entry; the rest fall back to type-based rendering. Every
	// field is an author-only input that cannot be inferred from the config value; the widget kind
	// itself is inferred from the value and `values`. All fields are optional (see the has_* flags).
	struct setting_metadata
	{
		localized_text name;        // display-name override (empty -> prettified key)
		localized_text description; // same text written to the .cfg comment

		bool has_min  = false;
		double min    = 0.0;
		bool has_max  = false;
		double max    = 0.0;
		bool has_step = false;
		double step   = 0.0;

		// Enum options: serialized option values and parallel display labels (labels default to
		// the values when omitted). Serialized form matches the config entry's serialization; each
		// label may be localized.
		std::vector<std::string> values;
		std::vector<localized_text> labels;

		bool has_order = false;
		double order   = 0.0; // author-declared sort key (lower first); unset -> map order

		bool hidden           = false; // author asked to omit this row entirely
		bool restart_required = false; // change only takes effect after a game restart
		bool freetext         = false; // force a bounded number to freetext entry (not the stepper)

		// When this setting may be changed relative to a loaded save (see editable_context). Default
		// `any`; forced to `main_menu` for the master "enabled" toggle and for restart_required settings.
		editable_context context = editable_context::any;

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

	// True if a mod called rom.mod_settings.opt_out() from its Lua (keyed by the calling mod's guid,
	// which matches a mod config's file stem). The settings menu still lists such a mod, but greys its
	// row, blocks drilling into it, and shows an opt-out note in place of its description. Populated
	// fresh each Lua-state init (opt_out re-runs with the mod's main.lua).
	bool mod_opted_out(const std::string& guid);
} // namespace big::mod_settings
