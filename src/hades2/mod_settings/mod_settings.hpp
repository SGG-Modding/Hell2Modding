#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace big::mod_settings
{
	void register_hooks();
	void bind_config_api(sol::state_view& state, sol::table& lua_ext);

	// A user-facing string an author may write in config.lua either plainly ("Enable feature") or as a localization
	// table keyed by the game's language folder codes ({ en = "...", de = "...", ["zh-TW"] = "..." }). Stored as
	// language-code -> text, with a plain string kept under the empty key. The settings menu resolves it to the current
	// game language at render time (see resolve_localized), falling back to English then any entry.
	using localized_text = std::map<std::string, std::string>;

	// When a setting may be changed, relative to whether a save is loaded. The Lua state is recreated when a save is
	// loaded, so init-time changes (GameData edits, function patches) only take effect if made before that point, while
	// some settings only apply to a live run. A row whose context does not match is greyed with a note.
	// any: anywhere. main_menu: forced for the master "enabled" toggle and any restartRequired setting.
	// in_save: hub and mid-run. in_hub: the Crossroads only, for settings unsafe to change during a run.
	enum class editable_context
	{
		any,
		main_menu,
		in_save,
		in_hub,
	};

	// An author-forced widget kind for a virtual row (config.lua `type`). Virtual rows normally infer their widget
	// from get()'s value type, but get() may return nil at build time (the mod's state is not ready yet), which would
	// fall back to a read-only row. Declaring `type` forces the widget regardless. Ignored for config-backed settings
	// (their value always exists). `enumeration` is only needed when there is no `values` list to imply it.
	enum class widget_type
	{
		inferred,
		boolean,
		number,
		string,
		enumeration,
	};

	// An author-declared menu category (configDesc `groups`) that does NOT correspond to a config section, letting a mod
	// present a flat or differently-nested config under an arbitrary menu tree via a per-entry `group`. `id` is the
	// identity a `group` path references (the table key in configDesc.groups).
	struct menu_group
	{
		std::string id;
		localized_text name;
		localized_text description;
		bool has_order = false;
		double order   = 0.0;
		std::vector<menu_group> children;
	};

	// The author-declared menu group tree (configDesc `groups`) for mod `guid`, empty when none was declared. The
	// settings menu uses it for the display name/order/description of groups a per-entry `group` references but that
	// do not exist as config sections. Populated fresh each Lua-state init by rom.mod_settings.load.
	std::vector<menu_group> mod_menu_groups(const std::string& guid);

	// Author-declared metadata for one setting, extracted from its config.lua description table. Only settings whose
	// description is a rich table have an entry; the rest fall back to type-based rendering. Every field is an
	// author-only input that cannot be inferred from the config value, and all are optional (see the has_* flags).
	struct setting_metadata
	{
		localized_text name;        // display-name override (empty -> prettified key)
		localized_text description; // same text written to the .cfg comment

		// Shown in the description box in place of `description` while the row is greyed by its own `disabled` field,
		// so the author can explain why it is unavailable. Empty -> fall back to `description`. Not applied to a
		// context-restricted row (which shows its own where-to-change note) or a mod-disabled row (the off mod toggle
		// already explains that). May be a plain string, a localization table, or a dynamic (function) field.
		localized_text disabled_description;

		bool has_min  = false;
		double min    = 0.0;
		bool has_max  = false;
		double max    = 0.0;
		bool has_step = false;
		double step   = 0.0;

		// Enum options: serialized option values and parallel display labels (labels default to the values when
		// omitted). Serialized form matches the config entry's serialization. Each label may be localized.
		std::vector<std::string> values;
		std::vector<localized_text> labels;

		bool has_order = false;
		double order   = 0.0; // author-declared sort key (lower first), unset -> map order

		bool hidden           = false; // author asked to omit this row entirely
		bool disabled         = false; // render greyed and non-interactive but still visible (may be dynamic)
		bool restart_required = false; // change only takes effect after a game restart
		bool freetext         = false; // force a bounded number to freetext entry (not the stepper)

		// When this setting may be changed relative to a loaded save (see editable_context). Default `any`. Forced to
		// `main_menu` for the master "enabled" toggle and for restart_required settings.
		editable_context context = editable_context::any;


		// True if any field in this setting's config.lua description is a Lua function (a dynamic field, e.g. `max =
		// function() ... end`). Such fields are skipped at load and re-evaluated at render via
		// resolve_setting_metadata, so the menu reflects the current game state.
		bool has_dynamic = false;

		// Number-display options (mainly for the slider) is_percentage shows a 0..1 value as 0..100 and appends "%"
		// show_as_percentage only appends "%" without scaling. Setting show_as_percentage in addition to is_percentage
		// is a no-op. The stored config value is never modified by either.
		bool show_as_percentage = false;
		bool is_percentage      = false;

		// Virtual-row only (config.lua `type`/`default`). `type` forces the widget kind when get() cannot be relied
		// on to infer it (see widget_type). `default` is the value a menu Reset restores the row to, via its set()
		// callback (config settings recover their own default from the .cfg / config.lua instead), stored serialized
		// like an enum option value. Both are ignored for config-backed settings.
		widget_type type = widget_type::inferred;
		bool has_default = false;
		std::string default_value;

		// Menu placement override (configDesc `group`): the author-declared menu path this entry appears under instead
		// of its config-section default. Empty -> placed by its config section. Each segment is a config child section
		// or an author group declared in configDesc `groups` (see menu_group). Applies to settings, actions and
		// virtual rows alike.
		std::vector<std::string> group;
	};

	// True if a mod author declared this setting as requiring a game restart to take effect (via `restart_required =
	// true` in the setting's config.lua description). Populated by rom.mod_settings.load. Consulted by the settings
	// menu when a value changes.
	bool setting_requires_restart(const std::string& guid, const std::string& section, const std::string& key);

	// Returns the author-declared metadata for a setting, or std::nullopt when the setting has no rich metadata table
	// (in which case the menu renders it with type-based defaults).
	std::optional<setting_metadata> get_setting_metadata(const std::string& guid, const std::string& section, const std::string& key);

	// True if (section, key) carries a configDesc entry (a description string or a table). The menu shows only
	// described keys. An undescribed config key is hidden, so a mod's internal/bookkeeping config values do not clutter
	// the settings page. The mod's master "enabled" toggle is always shown regardless (handled in build_mod_settings).
	bool setting_is_described(const std::string& guid, const std::string& section, const std::string& key);

	// True when the game is currently in the hub (the Crossroads): the game Lua global `CurrentHubRoom` is non-nil.
	// Reads the game's Lua state (shared with mods), so it must be called on the game thread while the state is alive.
	// The settings menu uses it to gate `editableContext = "inHub"` rows (editable only in the hub, not mid-run).
	// Returns false when the Lua state is unavailable.
	bool game_is_in_hub();

	// Like get_setting_metadata, but re-evaluates the setting's dynamic (Lua-function) fields against the current game
	// state. Call on the game thread, while the Lua state is alive, when get_setting_metadata reports has_dynamic. The
	// returned metadata never has has_dynamic set. nullopt for settings with no stored description (e.g. Chalk-bound).
	std::optional<setting_metadata> resolve_setting_metadata(const std::string& guid, const std::string& section, const std::string& key);

	// A menu button declared in config.lua that runs a Lua callback instead of editing a config value: an `action =
	// function() ... end` entry in the configDesc, which has no config counterpart. Placed among the setting rows by
	// `order`.
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

	// The action buttons declared directly in config `section` of mod `guid` (not recursing into child sections).
	// Dynamic fields are resolved against the current game state (call on the game thread).
	std::vector<action_info> get_actions(const std::string& guid, const std::string& section);

	// Runs a config.lua action button's Lua callback protected, with errors logged. No-op if the guid/section/key
	// does not resolve to an action. Call on the game thread while the Lua state is alive.
	void invoke_action(const std::string& guid, const std::string& section, const std::string& key);

	// A configDesc entry with NO backing config value, marked `virtual = true`. Its value comes from Lua callbacks: a
	// read-only row uses `text`, an interactive row `get`/`set`. The callables stay in the Lua descs registry and are
	// resolved at render; the rest of its metadata is read like a config setting's, via resolve_setting_metadata.
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

	// The virtual (non-config) rows declared directly in config `section` of mod `guid` (not recursing into child
	// sections). The order is unspecified (the menu sorts rows itself, by `order` then display name).
	std::vector<virtual_row_info> get_virtual_rows(const std::string& guid, const std::string& section);

	// The display string for a READ-ONLY virtual row, from its `text` (a string or a function returning one) callback.
	// Call on the game thread while the Lua state is alive. Empty when the row is unavailable or has no `text`.
	std::string get_virtual_display(const std::string& guid, const std::string& section, const std::string& key);

	// A virtual row's current typed value, read from its Lua `get()` callback. The kind determines which widget an
	// interactive virtual row builds (like a config value's type does for a config row).
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

	// Reads an interactive virtual row's current value by calling its `get()` callback (protected). Returns kind::none
	// when the row has no `get`, is unavailable, or the call fails. Call on the game thread while the Lua state is
	// alive.
	virtual_value get_virtual_value(const std::string& guid, const std::string& section, const std::string& key);

	// Writes a new value to an interactive virtual row by calling its `set(value)` callback (protected). No-op when the
	// row has no `set`. Call on the game thread while the Lua state is alive.
	void set_virtual_value(const std::string& guid, const std::string& section, const std::string& key, const virtual_value& value);

	// Restores one interactive virtual row of mod `guid` (identified by its config `section` and `key`) to its declared
	// `default`, via its set() callback. No-op returning false if the row is not interactive, declares no `default`, or
	// already holds it. The menu Reset scopes which rows to restore by their menu path and calls this per row (config-
	// backed settings recover their own defaults separately). Call on the game thread while the Lua state is alive.
	bool reset_virtual_row_to_default(const std::string& guid, const std::string& section, const std::string& key);

	// Returns the config.lua default (serialized like the config entry's value) for a setting bound via
	// rom.mod_settings.load, or std::nullopt for keys with no captured default. Used by the settings menu's. Reset
	// action to restore a setting to what config.lua declared.
	std::optional<std::string> get_setting_default(const std::string& guid, const std::string& section, const std::string& key);

	// True if a mod called rom.mod_settings.opt_out() from its Lua (keyed by the calling mod's guid, which matches a
	// mod config's file stem). The settings menu still lists such a mod, but greys its row, blocks drilling into it,
	// and shows an opt-out note in place of its description. Populated fresh each Lua-state init (opt_out re-runs with
	// the mod's main.lua).
	bool mod_opted_out(const std::string& guid);

	// The optional custom description a mod passed to rom.mod_settings.opt_out(description) (empty when none was
	// given). The settings menu shows it (resolved to the current language) in place of the generic opt-out note when
	// the mod's greyed row is highlighted.
	localized_text mod_opt_out_description(const std::string& guid);

	// True while a setting change should notify its mod through an on_change callback: a native options screen is
	// currently open. Consulted by the config API so an on_change fires for any edit made through the options menu (main
	// menu or in a save), but not from a mod's own config write outside the menu.
	bool on_change_callbacks_enabled();
} // namespace big::mod_settings
