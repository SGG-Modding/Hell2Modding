#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace big::mod_settings
{
	void register_hooks();
	void bind_config_api(sol::state_view& state, sol::table& lua_ext);

	// A user-facing string, plain or localized: language-code -> text, with a plain string under the empty key.
	// Resolved to the current game language at render, falling back to English then any entry.
	using localized_text = std::map<std::string, std::string>;

	// Where a setting may be edited: anywhere, only from the main menu, only while a save is loaded, or only in the hub
	// (the Crossroads). Off-context rows are greyed with a note. main_menu is forced for the master "enabled" toggle and
	// any restartRequired setting.
	enum class editable_context
	{
		any,
		main_menu,
		in_save,
		in_hub,
	};

	// Forces a virtual row's widget kind (config.lua `type`) when it cannot be inferred from get(). Ignored for
	// config-backed settings. `enumeration` is only needed without a `values` list.
	enum class widget_type
	{
		inferred,
		boolean,
		number,
		string,
		enumeration,
	};

	// An author-declared menu category (configDesc `groups`) with no matching config section. `id` is what a per-entry
	// `group` path references.
	struct menu_group
	{
		std::string id;
		localized_text name;
		localized_text description;
		localized_text disabled_description;
		bool has_order   = false;
		double order     = 0.0;
		bool disabled    = false;
		editable_context context = editable_context::any;
		bool has_dynamic = false;
		std::vector<menu_group> children;
	};

	// The author-declared menu group tree for mod `guid`, empty when none was declared.
	std::vector<menu_group> mod_menu_groups(const std::string& guid);

	// Re-resolves one author-declared group's dynamic fields against the current game state. `path` is its id chain
	// under the root configDesc `groups` (e.g. { "debugging", "logging" }).
	std::optional<menu_group> resolve_menu_group(const std::string& guid, const std::vector<std::string>& path);

	// Author-declared metadata for one setting, from its config.lua description table. Only settings described with a
	// rich table get an entry - the rest fall back to type-based rendering. All fields are optional (see the has_*).
	struct setting_metadata
	{
		localized_text name;        // display-name override (empty -> prettified key)
		localized_text description; // same text written to the .cfg comment

		// Shown instead of `description` while the row is greyed by its own `disabled` (empty -> use `description`).
		// Not applied to a context-restricted or mod-disabled row, which show their own note.
		localized_text disabled_description;

		bool has_min  = false;
		double min    = 0.0;
		bool has_max  = false;
		double max    = 0.0;
		bool has_step = false;
		double step   = 0.0;

		// Enum options and their parallel display labels (labels default to the values). Serialized like the config
		// entry's value.
		std::vector<std::string> values;
		std::vector<localized_text> labels;

		bool has_order = false;
		double order   = 0.0; // author-declared sort key (lowest first), unset -> alphabetical by display name

		bool hidden           = false; // author asked to omit this row entirely
		bool disabled         = false; // render greyed and non-interactive but still visible (may be dynamic)
		bool restart_required = false; // change only takes effect after a game restart

		editable_context context = editable_context::any;

		// A field written as a Lua function, skipped at load and re-evaluated at render via resolve_setting_metadata.
		bool has_dynamic = false;

		// is_percentage shows a 0..1 value as 0..100 and appends "%", show_as_percentage only appends it. Neither
		// changes the stored value.
		bool show_as_percentage = false;
		bool is_percentage      = false;

		// Virtual-row only. `default` is the value a menu Reset restores through set(), serialized like an enum option.
		widget_type type = widget_type::inferred;
		bool has_default = false;
		std::string default_value;

		// Menu path this entry appears under instead of its config section (configDesc `group`), empty -> its config
		// section. Each segment is a config child section or a declared author group.
		std::vector<std::string> group;
	};

	// True if the author declared this setting as requiring a game restart to take effect.
	bool setting_requires_restart(const std::string& guid, const std::string& section, const std::string& key);

	// The author-declared metadata for a setting, or nullopt when it has no rich metadata table (the menu then renders
	// it with type-based defaults).
	std::optional<setting_metadata> get_setting_metadata(const std::string& guid, const std::string& section, const std::string& key);

	// True if (section, key) carries a configDesc entry. The menu shows only described keys, plus the master "enabled"
	// toggle regardless.
	bool setting_is_described(const std::string& guid, const std::string& section, const std::string& key);

	// True when the game is in the hub (the Crossroads), i.e. the game Lua global `CurrentHubRoom` is non-nil. Gates
	// `editableContext = "inHub"` rows.
	bool game_is_in_hub();

	// Like get_setting_metadata, but re-evaluates the setting's dynamic fields against the current game state. Call
	// when get_setting_metadata reports has_dynamic. The result never has has_dynamic set.
	std::optional<setting_metadata> resolve_setting_metadata(const std::string& guid, const std::string& section, const std::string& key);

	// A configDesc entry with an `action` function and no config value: a button that runs a Lua callback.
	struct action_info
	{
		std::string section;                 // config section the action lives in (drilldown level)
		std::string key;                     // description key of the action
		localized_text name;                 // button label (display_name, or the prettified key)
		localized_text description;          // help text shown while highlighted
		localized_text disabled_description; // shown instead of `description` while author-disabled (may be dynamic)
		bool has_order           = false;    // author-declared sort key present
		double order             = 0.0;
		editable_context context = editable_context::any; // when the button is enabled (main-menu vs in-save)
		bool disabled            = false; // greyed and non-interactive (author-declared, may be dynamic)
		bool has_dynamic         = false; // name/description/order/disabled is a Lua function
		std::vector<std::string> group;   // menu placement override (configDesc `group`), empty -> config section
	};

	// The action buttons declared directly in config `section` of mod `guid` (not recursing), with their dynamic fields
	// resolved against the current game state.
	std::vector<action_info> get_actions(const std::string& guid, const std::string& section);

	// Runs an action button's Lua callback protected, logging errors. No-op if (guid, section, key) is not an action.
	void invoke_action(const std::string& guid, const std::string& section, const std::string& key);

	// A configDesc entry with NO backing config value, marked `virtual = true`. Its value comes from Lua callbacks: a
	// read-only row uses `text`, an interactive row `get`/`set`. The rest of its metadata is read like a setting's.
	struct virtual_row_info
	{
		std::string section;
		std::string key;
		bool has_order = false;
		double order   = 0.0;
		bool has_dynamic = false; // a name/description/values/min/max/text field is a Lua function (re-resolve at render)
		bool interactive = false; // has a `set` callback (an editable get/set row) rather than a read-only `text` row
		std::vector<std::string> group; // menu placement override (configDesc `group`), empty -> config section
	};

	// The virtual rows declared directly in config `section` of mod `guid` (not recursing). Order is unspecified - the
	// menu sorts rows itself.
	std::vector<virtual_row_info> get_virtual_rows(const std::string& guid, const std::string& section);

	// The display string for a READ-ONLY virtual row, from its `text` callback. Empty if it has none.
	std::string get_virtual_display(const std::string& guid, const std::string& section, const std::string& key);

	// A virtual row's current typed value. The kind decides which widget an interactive virtual row builds.
	struct virtual_value
	{
		enum class kind
		{
			none,
			boolean,
			number,
			string,
		};
		kind type        = kind::none;
		bool as_bool     = false;
		double as_number = 0.0;
		std::string as_string;
	};

	// Reads an interactive virtual row's value through its `get()` callback. kind::none if it has no `get` or the call
	// fails.
	virtual_value get_virtual_value(const std::string& guid, const std::string& section, const std::string& key);

	// Writes a virtual row's value through its `set(value)` callback. No-op when it has no `set`.
	void set_virtual_value(const std::string& guid, const std::string& section, const std::string& key, const virtual_value& value);

	// Restores one interactive virtual row to its declared `default` through set(), returning whether it changed. The
	// menu Reset scopes which rows to restore by menu path and calls this per row.
	bool reset_virtual_row_to_default(const std::string& guid, const std::string& section, const std::string& key);

	// The config.lua default for a setting bound via rom.mod_settings.load, serialized like the config entry's value.
	std::optional<std::string> get_setting_default(const std::string& guid, const std::string& section, const std::string& key);

	// True if a mod called rom.mod_settings.opt_out(). The menu still lists it, but greys its row and blocks drilling in.
	bool mod_opted_out(const std::string& guid);

	// The custom description passed to opt_out(), shown in place of the generic note. Empty when none was given.
	localized_text mod_opt_out_description(const std::string& guid);

	// True while a setting change should fire its mod's onChanged callback: a native options screen is open, so menu
	// edits notify the mod but its own config writes do not.
	bool on_change_callbacks_enabled();
} // namespace big::mod_settings
