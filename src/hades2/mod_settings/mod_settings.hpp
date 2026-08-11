#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace big::mod_settings
{
	void register_hooks();
	void bind_config_api(sol::state_view& state, sol::table& lua_ext);

	// Plain text or language-code -> text, with a plain string under the empty key.
	using localized_text = std::map<std::string, std::string>;

	// Off-context rows are greyed. main_menu is forced for the master toggle and restartRequired settings.
	enum class editable_context
	{
		any,
		main_menu,
		in_save,
		in_hub,
	};

	// Pins a virtual row's widget kind when get() cannot provide one.
	enum class widget_type
	{
		inferred,
		boolean,
		number,
		string,
		enumeration,
	};

	// Author-declared category with no matching config section.
	struct menu_group
	{
		std::string id;
		localized_text name;
		localized_text description;
		localized_text disabled_description;
		bool has_order           = false;
		double order             = 0.0;
		bool disabled            = false;
		editable_context context = editable_context::any;
		bool has_dynamic         = false;
		std::vector<menu_group> children;
	};

	std::vector<menu_group> mod_menu_groups(const std::string& guid);

	// Re-resolves one menu group's dynamic fields against the current game state.
	std::optional<menu_group> resolve_menu_group(const std::string& guid, const std::vector<std::string>& path);

	// Metadata from a rich config.lua description table. All fields are optional.
	struct setting_metadata
	{
		localized_text name;        // display-name override.
		localized_text description; // .cfg comment text.

		// Author-disabled rows may override `description`.
		localized_text disabled_description;

		bool has_min  = false;
		double min    = 0.0;
		bool has_max  = false;
		double max    = 0.0;
		bool has_step = false;
		double step   = 0.0;

		// Enum options serialize like config values. Labels default to values.
		std::vector<std::string> values;
		std::vector<localized_text> labels;

		bool has_order = false;
		double order   = 0.0; // lowest first, unset means alphabetical by display name.

		bool hidden           = false;
		bool disabled         = false; // greyed and non-interactive, may be dynamic.
		bool restart_required = false;

		editable_context context = editable_context::any;

		// Lua-function fields are re-evaluated at render.
		bool has_dynamic = false;

		// is_percentage scales display by 100. show_as_percentage only appends "%".
		bool show_as_percentage = false;
		bool is_percentage      = false;

		// Virtual-row only. Reset restores `default` through set().
		widget_type type = widget_type::inferred;
		bool has_default = false;
		std::string default_value;

		// Empty means config-section placement. Segments are config child sections or declared groups.
		std::vector<std::string> group;
	};

	bool setting_requires_restart(const std::string& guid, const std::string& section, const std::string& key);

	std::optional<setting_metadata> get_setting_metadata(const std::string& guid, const std::string& section, const std::string& key);

	bool setting_is_described(const std::string& guid, const std::string& section, const std::string& key);

	// Gates `editableContext = "inHub"` rows via the game's `CurrentHubRoom` global.
	bool game_is_in_hub();

	// Re-evaluates dynamic fields against the current game state. Result has no dynamic fields.
	std::optional<setting_metadata> resolve_setting_metadata(const std::string& guid, const std::string& section, const std::string& key);

	// Button backed by a configDesc `action` function with no config value.
	struct action_info
	{
		std::string section;
		std::string key;
		localized_text name;
		localized_text description;
		localized_text disabled_description; // author-disabled override, may be dynamic.
		bool has_order           = false;
		double order             = 0.0;
		editable_context context = editable_context::any;
		bool disabled            = false; // greyed and non-interactive, may be dynamic.
		bool has_dynamic         = false; // Lua-function fields are re-evaluated.
		std::vector<std::string> group;   // empty means config-section placement.
	};

	// Returned action fields are resolved against the current game state.
	std::vector<action_info> get_actions(const std::string& guid, const std::string& section);

	void invoke_action(const std::string& guid, const std::string& section, const std::string& key);

	// Row with no backing config value. Read-only uses `text`, interactive uses `get`/`set`.
	struct virtual_row_info
	{
		std::string section;
		std::string key;
		bool has_order   = false;
		double order     = 0.0;
		bool has_dynamic = false;       // Lua-function fields are re-evaluated.
		bool interactive = false;       // uses get/set instead of read-only text.
		std::vector<std::string> group; // empty means config-section placement.
	};

	std::vector<virtual_row_info> get_virtual_rows(const std::string& guid, const std::string& section);

	std::string get_virtual_display(const std::string& guid, const std::string& section, const std::string& key);

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

	// kind::none means no get() or a failed call.
	virtual_value get_virtual_value(const std::string& guid, const std::string& section, const std::string& key);

	void set_virtual_value(const std::string& guid, const std::string& section, const std::string& key, const virtual_value& value);

	// Restores an interactive virtual row's declared `default` through set().
	bool reset_virtual_row_to_default(const std::string& guid, const std::string& section, const std::string& key);

	std::optional<std::string> get_setting_default(const std::string& guid, const std::string& section, const std::string& key);

	bool mod_opted_out(const std::string& guid);

	localized_text mod_opt_out_description(const std::string& guid);

	// True while menu edits should fire mod onChanged callbacks.
	bool on_change_callbacks_enabled();
} // namespace big::mod_settings
