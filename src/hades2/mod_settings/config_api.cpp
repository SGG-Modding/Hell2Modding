#include "mod_settings.hpp"

#include <algorithm>
#include <any>
#include <cctype>
#include <climits>
#include <cstring>
#include <format>
#include <fstream>
#include <lua/lua_manager.hpp>
#include <lua/lua_module.hpp>
#include <map>
#include <mutex>
#include <optional>
#include <rom/rom.hpp>
#include <set>
#include <sstream>
#include <toml_v2/config_file.hpp>
#include <tuple>
#include <utility>
#include <vector>

// clang-format off
#include <AsyncLogger/Logger.hpp>
using namespace al;
// clang-format on
#undef ERROR

namespace big::mod_settings
{

	// Author-declared per-setting metadata registry, populated from each mod's config.lua by rom.mod_settings.load.
	// Keyed by guid + '\0' + section + '\0' + key. Holds the display-name override, numeric bounds, enum options,
	// ordering, and the restart-required flag that the settings menu reads to pick and drive a widget. Only settings
	// whose config.lua description is a rich table are registered. The rest fall back to type-based rendering.
	static std::mutex g_metadata_mutex;
	static std::map<std::string, setting_metadata> g_setting_metadata;

	// Per-setting appearance order (rank of a key's definition in config.lua), populated for EVERY bound key (not just
	// those with rich metadata). Keyed the same way as g_setting_metadata. The menu uses it to order rows that have no
	// author-declared `order` in their config-file source order, because Lua pairs() and the alphabetical config map
	// both lose the config.lua order.
	static std::map<std::string, int> g_appearance_order;

	// Serialized config.lua default for every bound key (whether or not it has a rich metadata table), captured at
	// load. The settings menu's Reset action restores a setting to this value. Keyed the same way as g_setting_metadata
	// (guid + '\0' + section + '\0' + key).
	static std::map<std::string, std::string> g_setting_default;

	// (section, key) pairs that carry a configDesc entry (a description string or a rich table). A config key with no
	// configDesc entry is not shown in the menu, except the mod's master "enabled" toggle (always shown so the mod
	// stays toggleable). Keyed the same way as g_setting_metadata (guid + '\0' + section + '\0' + key).
	static std::set<std::string> g_described_keys;

	// Guids of mods that called rom.mod_settings.opt_out(), i.e. asked not to be configured through the in-game menu.
	// Guarded by g_metadata_mutex. Cleared and rebuilt on each Lua-state init (see bind_config_api) because opt_out
	// re-runs with each mod's main.lua.
	static std::set<std::string> g_opted_out_mods;

	// Action buttons declared in config.lua (configDesc entries with an `action` function, no config value). Keyed by
	// guid, in config.lua source order. Plain data (the callable stays in the Lua-side description registry and is
	// invoked by navigation). Cleared each Lua-state init in bind_config_api.
	static std::map<std::string, std::vector<action_info>> g_actions;

	// Virtual rows declared in config.lua (configDesc entries marked `virtual = true` with no backing config value).
	// Keyed by guid, in config.lua source order. Like g_actions, the get/set/text callables stay in the Lua-side
	// description registry and are resolved at render. Cleared per-mod in clear_metadata_for.
	static std::map<std::string, std::vector<virtual_row_info>> g_virtual_rows;

	// The config section every mod's settings are bound under (matches SGG_Modding-Chalk, keeps the .cfg
	// byte-compatible). Description tables in config.lua mirror the config table under this root.
	static constexpr const char* root_section = "config";

	static std::string metadata_key(const std::string& guid, const std::string& section, const std::string& key)
	{
		std::string k;
		k.reserve(guid.size() + section.size() + key.size() + 2);
		k.append(guid);
		k.push_back('\0');
		k.append(section);
		k.push_back('\0');
		k.append(key);
		return k;
	}

	// Drops a mod's metadata before it re-registers: config.lua may change between loads, and the Lua state is
	// recreated on App::Reset (so load runs again for every mod).
	static void clear_metadata_for(const std::string& guid)
	{
		const std::string prefix = guid + '\0';
		for (auto it = g_setting_metadata.begin(); it != g_setting_metadata.end();)
		{
			it = (it->first.rfind(prefix, 0) == 0) ? g_setting_metadata.erase(it) : std::next(it);
		}
		for (auto it = g_appearance_order.begin(); it != g_appearance_order.end();)
		{
			it = (it->first.rfind(prefix, 0) == 0) ? g_appearance_order.erase(it) : std::next(it);
		}
		for (auto it = g_setting_default.begin(); it != g_setting_default.end();)
		{
			it = (it->first.rfind(prefix, 0) == 0) ? g_setting_default.erase(it) : std::next(it);
		}
		for (auto it = g_described_keys.begin(); it != g_described_keys.end();)
		{
			it = (it->rfind(prefix, 0) == 0) ? g_described_keys.erase(it) : std::next(it);
		}
	}

	bool setting_requires_restart(const std::string& guid, const std::string& section, const std::string& key)
	{
		std::scoped_lock lock(g_metadata_mutex);
		const auto it = g_setting_metadata.find(metadata_key(guid, section, key));
		return it != g_setting_metadata.end() && it->second.restart_required;
	}

	std::optional<setting_metadata> get_setting_metadata(const std::string& guid, const std::string& section, const std::string& key)
	{
		std::scoped_lock lock(g_metadata_mutex);
		const auto it = g_setting_metadata.find(metadata_key(guid, section, key));
		if (it == g_setting_metadata.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	// True if (section, key) carries a configDesc entry (any form: a description string, a setting/action table, or a
	// group table). The menu shows only described keys; an undescribed config key is hidden (see build_mod_settings).
	bool setting_is_described(const std::string& guid, const std::string& section, const std::string& key)
	{
		std::scoped_lock lock(g_metadata_mutex);
		return g_described_keys.contains(metadata_key(guid, section, key));
	}

	int get_setting_appearance_order(const std::string& guid, const std::string& section, const std::string& key)
	{
		std::scoped_lock lock(g_metadata_mutex);
		const auto it = g_appearance_order.find(metadata_key(guid, section, key));
		return it != g_appearance_order.end() ? it->second : INT_MAX;
	}

	std::optional<std::string> get_setting_default(const std::string& guid, const std::string& section, const std::string& key)
	{
		std::scoped_lock lock(g_metadata_mutex);
		const auto it = g_setting_default.find(metadata_key(guid, section, key));
		if (it == g_setting_default.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	bool mod_opted_out(const std::string& guid)
	{
		std::scoped_lock lock(g_metadata_mutex);
		return g_opted_out_mods.count(guid) != 0;
	}

	// Finds the byte offset of a key's definition ("<key> =") in config.lua source, whole-word and not "==", or npos.
	// The first match is the key's place in the returned `config` defaults table (defined before configDesc), which is
	// the author's intended display order. Occurrences inside strings/prose don't match because they are not followed
	// by a bare '='.
	static std::size_t find_key_definition(const std::string& src, const std::string& key)
	{
		auto is_ident = [](char c)
		{
			return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
		};

		for (std::size_t pos = src.find(key); pos != std::string::npos; pos = src.find(key, pos + 1))
		{
			if (pos > 0 && is_ident(src[pos - 1]))
			{
				continue; // not a word boundary on the left (e.g. "my_key" when searching "key")
			}
			std::size_t after = pos + key.size();
			if (after < src.size() && is_ident(src[after]))
			{
				continue; // not a word boundary on the right
			}
			while (after < src.size() && (src[after] == ' ' || src[after] == '\t'))
			{
				++after;
			}
			if (after < src.size() && src[after] == '=' && (after + 1 >= src.size() || src[after + 1] != '='))
			{
				return pos;
			}
		}
		return std::string::npos;
	}

	static std::string serialize_option(const sol::object& v); // defined below.

	// Parses a user-facing string field that is either a plain scalar or a localization table (keyed by the game's
	// language folder codes, e.g. { en = "...", ["zh-TW"] = "..." }). A scalar is stored under the empty key. A table
	// contributes one entry per string-keyed string value. An empty or absent value yields an empty map (i.e. no
	// override).
	static localized_text parse_localized(const sol::object& o)
	{
		localized_text out;
		if (o.is<sol::table>())
		{
			o.as<sol::table>().for_each(
			    [&out](const sol::object& k, const sol::object& v)
			    {
				    if (k.get_type() == sol::type::string && v.get_type() == sol::type::string)
				    {
					    out[k.as<std::string>()] = v.as<std::string>();
				    }
			    });
			return out;
		}
		const std::string s = serialize_option(o);
		if (!s.empty())
		{
			out[""] = s;
		}
		return out;
	}

	// Resolves a localized string to a single language-independent value for on-disk use (the .cfg comment), which is
	// not re-written per language: English, then the unlocalized value, then any entry. The in-game menu resolves to
	// the live game language separately at render time.
	static std::string localized_fallback(const localized_text& t)
	{
		if (t.empty())
		{
			return {};
		}
		if (const auto it = t.find("en"); it != t.end())
		{
			return it->second;
		}
		if (const auto it = t.find(""); it != t.end())
		{
			return it->second;
		}
		return t.begin()->second;
	}

	// Extracts the (possibly localized) description from a config.lua description value, which may be a plain string,
	// or a rich table with a `description` field (or `[1]` shorthand) that is itself a plain string or a localization
	// table.
	static localized_text describe(const sol::object& desc)
	{
		if (desc.get_type() == sol::type::string)
		{
			return parse_localized(desc);
		}
		if (desc.is<sol::table>())
		{
			sol::table t         = desc.as<sol::table>();
			sol::object as_field = t["description"];
			if (as_field.valid() && as_field != sol::lua_nil)
			{
				return parse_localized(as_field);
			}
			sol::object as_first = t[1];
			if (as_first.valid() && as_first != sol::lua_nil)
			{
				return parse_localized(as_first);
			}
		}
		return {};
	}

	// True if a config.lua description table declares `restartRequired = true`.
	static bool description_requires_restart(const sol::object& desc)
	{
		if (!desc.is<sol::table>())
		{
			return false;
		}
		sol::object flag = desc.as<sol::table>()["restartRequired"];
		return flag.is<bool>() && flag.as<bool>();
	}

	// Parses an `editableContext` field ("any"/"mainMenu"/"inSave"/"inHub") returns `fallback` for anything else.
	// Shared by setting metadata and action buttons.
	static editable_context parse_editable_context(const sol::object& o, editable_context fallback)
	{
		if (o.get_type() == sol::type::string)
		{
			const std::string s = o.as<std::string>();
			if (s == "mainMenu")
			{
				return editable_context::main_menu;
			}
			if (s == "inSave")
			{
				return editable_context::in_save;
			}
			if (s == "inHub")
			{
				return editable_context::in_hub;
			}
			if (s == "any")
			{
				return editable_context::any;
			}
		}
		return fallback;
	}

	// Serializes a Lua enum-option value (bool/number/string) into the exact string form a config entry serializes to,
	// so the menu can match an option against the stored value. Numbers use the same locale-invariant std::format the
	// toml converter uses, and every config number is stored as a double.
	static std::string serialize_option(const sol::object& v)
	{
		switch (v.get_type())
		{
		case sol::type::string:  return v.as<std::string>();
		case sol::type::boolean: return v.as<bool>() ? "true" : "false";
		case sol::type::number:  return std::format("{}", v.as<double>());
		default:                 return "";
		}
	}

	// Reads the array part of a Lua list table (ipairs order) applying `transform` to each element.
	template<typename Transform>
	static void read_list(const sol::object& obj, std::vector<std::string>& out, Transform transform)
	{
		if (!obj.is<sol::table>())
		{
			return;
		}
		sol::table t = obj.as<sol::table>();
		for (std::size_t i = 1; i <= t.size(); ++i)
		{
			out.push_back(transform(t[i]));
		}
	}

	// Builds a setting_metadata from a config.lua description table for a flat (non-table) value. Missing fields keep
	// their defaults. The widget kind is not stored: the menu. Derives it from the config value's type plus the
	// presence of `values` (enum), so authors never declare a `type`. Author-only inputs that cannot be inferred (name,
	// bounds, enum options/labels, order, hidden, restart) are what this captures.
	static setting_metadata extract_metadata(const sol::table& desc)
	{
		setting_metadata m;
		m.description = describe(desc);

		// Display-name override (`displayName`) empty -> the menu prettifies the key. May be a plain string or a
		// localization table.
		sol::object display_name = desc["displayName"];
		m.name                   = parse_localized(display_name);

		// Alternative description shown while the row is greyed by its `disabled` field (empty -> fall back to the
		// normal description). String or localization table, like displayName. If it was written as a function it has
		// already been resolved to a concrete value by resolve_description before this runs.
		m.disabled_description = parse_localized(desc["disabledDescription"]);

		sol::object min_field = desc["min"];
		if (min_field.get_type() == sol::type::number)
		{
			m.has_min = true;
			m.min     = min_field.as<double>();
		}
		sol::object max_field = desc["max"];
		if (max_field.get_type() == sol::type::number)
		{
			m.has_max = true;
			m.max     = max_field.as<double>();
		}
		sol::object step_field = desc["step"];
		if (step_field.get_type() == sol::type::number)
		{
			m.has_step = true;
			m.step     = step_field.as<double>();
		}

		read_list(desc["values"],
		          m.values,
		          [](const sol::object& v)
		          {
			          return serialize_option(v);
		          });

		// Enum option display labels (parallel to `values`). Each may be a plain string or a localization table.
		if (sol::object labels_obj = desc["labels"]; labels_obj.is<sol::table>())
		{
			sol::table lt = labels_obj.as<sol::table>();
			for (std::size_t i = 1; i <= lt.size(); ++i)
			{
				sol::object label = lt[i];
				m.labels.push_back(parse_localized(label));
			}
		}

		sol::object order_field = desc["order"];
		if (order_field.get_type() == sol::type::number)
		{
			m.has_order = true;
			m.order     = order_field.as<double>();
		}

		sol::object hidden_field = desc["hidden"];
		if (hidden_field.is<bool>())
		{
			m.hidden = hidden_field.as<bool>();
		}

		sol::object disabled_field = desc["disabled"];
		if (disabled_field.is<bool>())
		{
			m.disabled = disabled_field.as<bool>();
		}

		sol::object freetext_field = desc["freetext"];
		if (freetext_field.is<bool>())
		{
			m.freetext = freetext_field.as<bool>();
		}

		sol::object show_pct_field = desc["showAsPercentage"];
		if (show_pct_field.is<bool>())
		{
			m.show_as_percentage = show_pct_field.as<bool>();
		}
		sol::object is_pct_field = desc["isPercentage"];
		if (is_pct_field.is<bool>())
		{
			m.is_percentage = is_pct_field.as<bool>();
		}

		m.restart_required = description_requires_restart(desc);

		// When the setting may be changed relative to a loaded save (`editableContext`). The menu forces the master
		// "enabled" toggle and restartRequired settings to main_menu regardless, so authors need only annotate the
		// in-between cases.
		m.context = parse_editable_context(desc["editableContext"], editable_context::any);

		// A field written as a Lua function is a dynamic field: It is skipped by the type-guarded reads above (a
		// function is not a number/table/bool/string) and instead re-evaluated at render time by
		// resolve_setting_metadata. Record that any such field is present so the menu knows to resolve. `hidden` is
		// intentionally NOT dynamic: showing/hiding a row shifts the layout and the row set is only re-evaluated on a
		// full rebuild, so a live-changing condition must use `disabled` instead. `editableContext` is a fixed design
		// property of a setting, so it is static too.
		for (const char* field : {"displayName", "description", "disabledDescription", "min", "max", "step", "values", "labels", "order", "disabled"})
		{
			if (desc[field].get_type() == sol::type::function)
			{
				m.has_dynamic = true;
				break;
			}
		}

		return m;
	}

	// The Lua-side registry (rom.mod_settings._descs) mapping guid -> the mod's raw configDesc table, kept alive so
	// dynamic (function) description fields and action callbacks can be evaluated at render time. Lua-owned and
	// recreated with the rom.mod_settings table each Lua state, so it never dangles. Returns a nil object if the guid
	// has no stored description.
	static sol::object stored_descriptions(sol::state_view state, const std::string& guid)
	{
		sol::object ns = state[rom::g_lua_api_namespace];
		if (!ns.is<sol::table>())
		{
			return sol::lua_nil;
		}
		sol::object ms = ns.as<sol::table>()["mod_settings"];
		if (!ms.is<sol::table>())
		{
			return sol::lua_nil;
		}
		sol::object descs = ms.as<sol::table>()["_descs"];
		if (!descs.is<sol::table>())
		{
			return sol::lua_nil;
		}
		return descs.as<sol::table>()[guid];
	}

	// Navigates a mod's stored configDesc to the description of (section, key). The configDesc mirrors the config table
	// under the "config" root, so the section's remaining path (after "config") indexes nested description tables, then
	// `key` selects the leaf/group description. Returns nil if any hop is missing or not a table.
	static sol::object navigate_description(const sol::object& root, const std::string& section, const std::string& key)
	{
		if (!root.is<sol::table>())
		{
			return sol::lua_nil;
		}
		sol::table node = root.as<sol::table>();


		// section is "config" or "config.a.b..." walk the part after the root.
		std::string rel;
		if (section.size() > std::strlen(root_section) && section.compare(0, std::strlen(root_section) + 1, std::string(root_section) + ".") == 0)
		{
			rel = section.substr(std::strlen(root_section) + 1);
		}
		std::size_t pos = 0;
		while (pos < rel.size())
		{
			const std::size_t dot  = rel.find('.', pos);
			const std::string part = rel.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
			sol::object child      = node[part];
			if (!child.is<sol::table>())
			{
				return sol::lua_nil;
			}
			node = child.as<sol::table>();
			if (dot == std::string::npos)
			{
				break;
			}
			pos = dot + 1;
		}
		return node[key];
	}

	// Calls a dynamic description field (a Lua function) protected, returning its result, or nil on error (logged).
	// Non-function values are returned unchanged.
	static sol::object evaluate_field(const sol::object& value, const std::string& guid, const char* field)
	{
		if (value.get_type() != sol::type::function)
		{
			return value;
		}
		sol::protected_function fn        = value;
		sol::protected_function_result rv = fn();
		if (!rv.valid())
		{
			const sol::error err = rv;
			LOG(WARNING) << "[mod_settings] dynamic '" << field << "' for " << guid << " failed: " << err.what();
			return sol::lua_nil;
		}
		return rv.get<sol::object>();
	}

	// Builds a shallow copy of a setting's description table with every dynamic (function) field replaced by its
	// evaluated value, so the existing extract_metadata can read it as if the author had written static values.
	// Callables invoked on their own events (not read as metadata) are intentionally left as-is: `onChange`, `action`,
	// and a virtual row's `get`/`set`/`text` (get/text are called by get_virtual_display; set takes an argument).
	static sol::table resolve_description(sol::state_view state, const sol::table& desc, const std::string& guid)
	{
		sol::table out = state.create_table();
		for (const auto& [k, v] : desc)
		{
			if (k.get_type() != sol::type::string)
			{
				out[k] = v;
				continue;
			}
			const std::string field = k.as<std::string>();
			if (field == "onChange" || field == "action" || field == "get" || field == "set" || field == "text")
			{
				out[k] = v;
				continue;
			}
			out[k] = evaluate_field(v, guid, field.c_str());
		}
		return out;
	}

	// Reads the static (non-function) action metadata common to collection and dynamic re-resolution.
	static void read_action_fields(const sol::table& entry, action_info& a)
	{
		a.name                 = parse_localized(entry["displayName"]);
		a.description          = describe(entry);
		a.disabled_description = parse_localized(entry["disabledDescription"]);
		if (sol::object o = entry["order"]; o.get_type() == sol::type::number)
		{
			a.has_order = true;
			a.order     = o.as<double>();
		}
		if (sol::object d = entry["disabled"]; d.is<bool>())
		{
			a.disabled = d.as<bool>();
		}
		a.context = parse_editable_context(entry["editableContext"], editable_context::any);
	}

	// Walks a mod's configDesc (guided by the config defaults structure, like bind_defaults) collecting action buttons:
	// description entries carrying an `action` function, which have no config value. Recurses into config groups so
	// actions can live at any drilldown level. Static fields are captured now dynamic ones (has_dynamic) are
	// re-resolved at render by get_actions.
	static void collect_actions(const sol::table& config_tbl, const sol::object& desc_obj, const std::string& section, std::vector<action_info>& out)
	{
		if (desc_obj.is<sol::table>())
		{
			sol::table desc = desc_obj.as<sol::table>();
			for (const auto& [k, v] : desc)
			{
				if (k.get_type() != sol::type::string || !v.is<sol::table>())
				{
					continue;
				}
				sol::table entry = v.as<sol::table>();
				if (entry["action"].get_type() != sol::type::function)
				{
					continue;
				}
				action_info a;
				a.section = section;
				a.key     = k.as<std::string>();
				read_action_fields(entry, a);
				for (const char* field : {"displayName", "description", "disabledDescription", "order", "disabled"})
				{
					if (entry[field].get_type() == sol::type::function)
					{
						a.has_dynamic = true;
						break;
					}
				}
				out.push_back(std::move(a));
			}
		}

		// Recurse into child sections following the config structure (a table value is a group).
		for (const auto& [k, v] : config_tbl)
		{
			if (k.get_type() != sol::type::string || !v.is<sol::table>())
			{
				continue;
			}
			const sol::object child_desc = desc_obj.is<sol::table>() ? desc_obj.as<sol::table>()[k] : sol::object(sol::lua_nil);
			collect_actions(v.as<sol::table>(), child_desc, section + "." + k.as<std::string>(), out);
		}
	}

	// configDesc field names that are metadata OF a setting/group/action/virtual row, not child keys. When walking a
	// desc table for child rows (virtual detection + orphan validation), these are skipped so a group's OWN
	// displayName/description/order/... are not mistaken for missing config keys (a group desc table mixes the group's
	// metadata with its child descriptions).
	static bool is_reserved_desc_field(const std::string& key)
	{
		static const std::set<std::string> reserved = {
		    "displayName",
		    "description",
		    "disabledDescription",
		    "min",
		    "max",
		    "step",
		    "values",
		    "labels",
		    "order",
		    "hidden",
		    "disabled",
		    "freetext",
		    "restartRequired",
		    "editableContext",
		    "showAsPercentage",
		    "isPercentage",
		    "onChange",
		    "action",
		    "virtual",
		    "get",
		    "set",
		    "text",
		};
		return reserved.contains(key);
	}

	// Walks a mod's configDesc (guided by the config structure, like collect_actions) collecting virtual rows and
	// validating every entry. A configDesc entry must resolve to one of: a config value (a config-backed setting or a
	// group), an `action` function, or an explicit `virtual = true` marker. An entry that is NONE of these is almost
	// always an author mistake (they described a key but forgot to add it to `config`), so it is logged. A `virtual`
	// row with no `get`/`text` (nothing to display) is logged too. Recurses into config groups only, like the actions
	// and defaults walks, so virtual rows live alongside config rows in a config-backed section.
	static void collect_virtual_rows(const std::string& guid, const sol::table& config_tbl, const sol::object& desc_obj, const std::string& section, std::vector<virtual_row_info>& out)
	{
		if (desc_obj.is<sol::table>())
		{
			sol::table desc = desc_obj.as<sol::table>();
			for (const auto& [k, v] : desc)
			{
				if (k.get_type() != sol::type::string)
				{
					continue;
				}
				const std::string key = k.as<std::string>();

				// Skip the current node's own metadata fields (a group/root desc mixes them with child descriptions),
				// so they are never mistaken for a child config key.
				if (is_reserved_desc_field(key))
				{
					continue;
				}

				const std::string path    = section + "." + key;
				const sol::object cfg_val = config_tbl[key];
				const bool has_config     = cfg_val.valid() && cfg_val.get_type() != sol::type::lua_nil;

				// A plain-string description for a key with no config value is an orphan (a described key never added
				// to config). A string desc for a real config key is fine (bind_defaults handles it).
				if (v.get_type() == sol::type::string)
				{
					if (!has_config)
					{
						LOG(WARNING) << "[mod_settings] " << guid << ": configDesc entry '" << path << "' has a description but no matching config value, and is not an action or a virtual row. Did you forget to add '" << key << "' to config, or mark it virtual = true?";
					}
					continue;
				}
				if (!v.is<sol::table>())
				{
					continue;
				}
				sol::table entry        = v.as<sol::table>();
				const bool is_action    = entry["action"].get_type() == sol::type::function;
				const sol::object vmark = entry["virtual"];
				const bool is_virtual   = vmark.is<bool>() && vmark.as<bool>();

				if (has_config)
				{
					// Config-backed setting or a config group (recursed below). `virtual` here is contradictory.
					if (is_virtual)
					{
						LOG(WARNING) << "[mod_settings] " << guid << ": configDesc entry '" << path << "' is marked virtual = true but also has a config value; treating it as a normal config setting.";
					}
					continue;
				}
				if (is_action)
				{
					continue; // collected by collect_actions.
				}
				if (!is_virtual)
				{
					// No config value, no action, no virtual marker: the author most likely forgot the config entry.
					LOG(WARNING) << "[mod_settings] " << guid << ": configDesc entry '" << path << "' has no matching config value and is not marked `virtual = true` or given an `action`. Did you forget to add '" << key << "' to config?";
					continue;
				}

				virtual_row_info vr;
				vr.section = section;
				vr.key     = key;
				if (sol::object o = entry["order"]; o.get_type() == sol::type::number)
				{
					vr.has_order = true;
					vr.order     = o.as<double>();
				}

				const bool has_get  = entry["get"].get_type() == sol::type::function;
				const bool has_set  = entry["set"].get_type() == sol::type::function;
				const sol::object t = entry["text"];
				const bool has_text = t.get_type() == sol::type::string || t.get_type() == sol::type::function;

				// A row is interactive (an editable get/set widget) when it has a `set`, otherwise it is a read-only
				// `text` row. get/set/text/values/min/max may be functions too (dynamic), so re-evaluate at render.
				vr.interactive = has_set;
				for (const char* field : {"displayName", "description", "text", "values", "min", "max", "step", "labels"})
				{
					if (entry[field].get_type() == sol::type::function)
					{
						vr.has_dynamic = true;
						break;
					}
				}
				if (has_get || entry["values"].valid()) // an interactive row's value is dynamic by nature.
				{
					vr.has_dynamic = vr.has_dynamic || vr.interactive;
				}

				if (vr.interactive)
				{
					// Interactive row: needs `get` to read its current value for the widget. `set` is present here.
					if (!has_get)
					{
						LOG(WARNING) << "[mod_settings] " << guid << ": interactive virtual row '" << path << "' has a `set` but no `get`, so its widget cannot read a value; add a `get` callback.";
						continue;
					}
				}
				else if (!has_text)
				{
					// Read-only row: needs `text`. (A stray `get` with no `set` is not a display path.)
					LOG(WARNING) << "[mod_settings] " << guid << ": virtual row '" << path << "' has no `text` (a string or a function returning one) and no `set` (to be interactive), so it has nothing to show.";
				}
				out.push_back(std::move(vr));
			}
		}

		// Recurse into child config sections (a table config value is a group), like collect_actions.
		for (const auto& [k, v] : config_tbl)
		{
			if (k.get_type() != sol::type::string || !v.is<sol::table>())
			{
				continue;
			}
			const sol::object child_desc = desc_obj.is<sol::table>() ? desc_obj.as<sol::table>()[k] : sol::object(sol::lua_nil);
			collect_virtual_rows(guid, v.as<sol::table>(), child_desc, section + "." + k.as<std::string>(), out);
		}
	}

	// Finds the config entry for (section, key), or nullptr. m_entries is keyed by config_definition, so this is a
	// direct map lookup.
	static toml_v2::config_file::config_entry_base* find_entry(toml_v2::config_file* cf, const std::string& section, const std::string& key)
	{
		toml_v2::config_definition def(section, key);
		return cf->try_get_entry(def);
	}

	// True if `section` is a bound section or the parent of one (some entry's section equals `section` or starts with
	// `section + "."`). Used to expose nested config tables via the proxy.
	static bool has_section(toml_v2::config_file* cf, const std::string& section)
	{
		const std::string prefix = section + ".";
		for (const auto& [def, entry] : cf->m_entries)
		{
			if (def.m_section == section || def.m_section.rfind(prefix, 0) == 0)
			{
				return true;
			}
		}
		return false;
	}

	// Reads a config entry's value as the matching Lua type.
	static sol::object entry_get(sol::this_state ts, toml_v2::config_file::config_entry_base* entry)
	{
		const auto& t = entry->type();
		if (t == typeid(bool))
		{
			return sol::make_object(ts, entry->get_value_base<bool>());
		}
		if (t == typeid(double))
		{
			return sol::make_object(ts, entry->get_value_base<double>());
		}
		if (t == typeid(std::string))
		{
			return sol::make_object(ts, entry->get_value_base<std::string>());
		}
		return sol::lua_nil;
	}

	// Writes a Lua value into a config entry, dispatching on the value's Lua type (matching the toml_v2
	// config_entry:set overloads: bool/number/string).
	static void entry_set(toml_v2::config_file::config_entry_base* entry, const sol::object& value)
	{
		switch (value.get_type())
		{
		case sol::type::boolean: entry->set_value_base<bool>(value.as<bool>()); break;
		case sol::type::number:  entry->set_value_base<double>(value.as<double>()); break;
		case sol::type::string:  entry->set_value_base<std::string>(value.as<std::string>()); break;
		default:                 break;
		}
	}

	// Attaches a Lua onChange callback (from a setting's config.lua description) to its config entry. toml_v2 already
	// fires config_entry::m_setting_changed after a value changes and the file is saved. This routes that to Lua,
	// passing the new value and the setting key. It fires only for an edit made through the in-game options menu
	// (on_change_callbacks_enabled gates on the options screen being open in-game), so it is never called in the main
	// menu - where there is no live run to apply to and Lua game-data edits are discarded when a save loads - nor from
	// a mod's own config write outside the menu. A same-value write is a no-op and does not fire, so a callback that
	// writes back cannot loop. It is stored on the entry, which is owned by the mod's config_file
	// (module->m_data.m_config_files) and destroyed with the Lua state on App::Reset - so the captured sol reference
	// shares the mod's lifecycle and never dangles (unlike a C++ static). Called protected: a Lua error is logged,
	// never propagated.
	static void attach_on_change(toml_v2::config_file::config_entry_base* entry, sol::protected_function callback)
	{
		if (!entry || !callback.valid())
		{
			return;
		}
		entry->m_setting_changed = [callback = std::move(callback)](toml_v2::config_file::config_entry_base* changed)
		{
			if (!on_change_callbacks_enabled())
			{
				return;
			}
			const sol::object value               = entry_get(callback.lua_state(), changed);
			sol::protected_function_result result = callback(changed->m_definition.m_key, value);
			if (!result.valid())
			{
				const sol::error err = result;
				LOG(WARNING) << "[mod_settings] onChange callback failed for " << changed->m_definition.m_section << "."
				             << changed->m_definition.m_key << ": " << err.what();
			}
		};
	}

	// Parses a config key that is a positive-integer array index ("1", "2", ...), used to expose array-like sections
	// through #, ipairs and inext. Returns false for an empty or non-digit key.
	static bool parse_positive_index(const std::string& key, long& out)
	{
		if (key.empty())
		{
			return false;
		}
		long value = 0;
		for (const char c : key)
		{
			if (c < '0' || c > '9')
			{
				return false;
			}
			value = value * 10 + (c - '0');
		}
		out = value;
		return value > 0;
	}

	// Registry keys for the table-based config proxy machinery: one shared metatable, plus two weak-keyed maps from
	// each wrapper table to the config_file and section it points at. Stored in the Lua registry so the free
	// metamethods can recover them per call.
	static constexpr const char* k_proxy_metatable   = "h2m_mod_config_metatable";
	static constexpr const char* k_proxy_cf_map      = "h2m_mod_config_cf";
	static constexpr const char* k_proxy_section_map = "h2m_mod_config_section";

	// Builds the (empty) Lua table wrapper mods receive as their `config`, so `type(config) == "table"` (matching
	// SGG_Modding-Chalk). Defined after mod_config_proxy, but the struct's child accessors call it, so forward-declare.
	static sol::object make_proxy(sol::this_state ts, toml_v2::config_file* cf, const std::string& section);

	// Live read/write view over a config_file section, returned to the mod as its `config` object. Reads/writes go
	// straight through to the underlying config entries (so the in-game menu and the mod always see the same values).
	// Nested sections resolve to child proxies. It holds a raw config_file pointer (not a sol reference): the
	// config_file is owned by the mod and both it and this proxy are recreated together per Lua state, so nothing
	// dangles across an App::Reset.
	struct mod_config_proxy
	{
		toml_v2::config_file* cf = nullptr;
		std::string section;

		sol::object index(sol::this_state ts, const std::string& key) const
		{
			if (auto* entry = find_entry(cf, section, key))
			{
				return entry_get(ts, entry);
			}
			const std::string child = section + "." + key;
			if (has_section(cf, child))
			{
				return make_proxy(ts, cf, child);
			}
			return sol::lua_nil;
		}

		void new_index(const std::string& key, const sol::object& value) const
		{
			if (auto* entry = find_entry(cf, section, key))
			{
				entry_set(entry, value);
				return;
			}

			// Assigning a whole table to a nested section (e.g. config.group = { a = 1, b = 2 }, or a preset order to
			// config.biome_pool.custom_order_data) sets each matching leaf in that child section, recursing for deeper
			// tables. Only existing bound leaves are written. String keys with no entry are ignored, mirroring bind's
			// string-key-only binding.
			const std::string child = section + "." + key;
			if (value.is<sol::table>() && has_section(cf, child))
			{
				const mod_config_proxy child_proxy{cf, child};
				for (const auto& [k, v] : value.as<sol::table>())
				{
					if (k.get_type() == sol::type::string)
					{
						child_proxy.new_index(k.as<std::string>(), v);
					}
				}
			}
		}

		// Snapshots this section's immediate children into a fresh Lua table: each leaf key maps to its current value
		// and each direct sub-section name maps to a child proxy. The iteration metamethods hand this plain table to
		// Lua's own pairs/next so consumers walk the live config exactly like a normal table (mirrors Chalk's wrapper).
		sol::table children_snapshot(sol::this_state ts) const
		{
			sol::state_view lua(ts);
			sol::table out           = lua.create_table();
			const std::string prefix = section + ".";
			std::set<std::string> seen_children;
			for (const auto& [def, entry] : cf->m_entries)
			{
				if (def.m_section == section)
				{
					out[def.m_key] = entry_get(ts, entry.get());
				}
				else if (def.m_section.rfind(prefix, 0) == 0)
				{
					const std::string child =
					    def.m_section.substr(prefix.size(), def.m_section.find('.', prefix.size()) - prefix.size());
					if (seen_children.insert(child).second)
					{
						out[child] = make_proxy(ts, cf, prefix + child);
					}
				}
			}
			return out;
		}

		// __len: highest positive-integer leaf key at this level (array length), 0 for a purely string-keyed section.
		std::size_t length() const
		{
			std::size_t n = 0;
			for (const auto& [def, entry] : cf->m_entries)
			{
				long index = 0;
				if (def.m_section == section && parse_positive_index(def.m_key, index) && static_cast<std::size_t>(index) > n)
				{
					n = static_cast<std::size_t>(index);
				}
			}
			return n;
		}

		// __pairs: `for k, v in pairs(config)` walks one level (leaf values plus child proxies), like a plain table.
		std::tuple<sol::object, sol::object, sol::object> pairs(sol::this_state ts) const
		{
			sol::state_view lua(ts);
			sol::table snapshot              = children_snapshot(ts);
			sol::protected_function pairs_fn = lua["pairs"];
			sol::protected_function_result r = pairs_fn(snapshot);
			return std::make_tuple(r.get<sol::object>(0), r.get<sol::object>(1), r.get<sol::object>(2));
		}

		// __ipairs (consulted by ipairs on Lua 5.2): iterate the 1..n integer-keyed leaves of an array-like section.
		std::tuple<sol::object, sol::object, sol::object> ipairs(sol::this_state ts) const
		{
			sol::state_view lua(ts);
			sol::table sequence = lua.create_table();
			const std::size_t n = length();
			for (std::size_t i = 1; i <= n; ++i)
			{
				if (auto* entry = find_entry(cf, section, std::to_string(i)))
				{
					sequence[i] = entry_get(ts, entry);
				}
			}
			sol::protected_function ipairs_fn = lua["ipairs"];
			sol::protected_function_result r  = ipairs_fn(sequence);
			return std::make_tuple(r.get<sol::object>(0), r.get<sol::object>(1), r.get<sol::object>(2));
		}

		// __next (consulted by ModUtil's next/qrawpairs): step to the pair after `key` at this level.
		std::tuple<sol::object, sol::object> next(sol::this_state ts, sol::object key) const
		{
			sol::state_view lua(ts);
			sol::table snapshot              = children_snapshot(ts);
			sol::protected_function next_fn  = lua["next"];
			sol::protected_function_result r = next_fn(snapshot, key);
			return std::make_tuple(r.get<sol::object>(0), r.get<sol::object>(1));
		}

		// __inext (consulted by ModUtil's inext/qrawipairs): step to index i + 1 of an array-like section.
		std::tuple<sol::object, sol::object> inext(sol::this_state ts, sol::object index) const
		{
			long i = 0;
			if (index.is<long>())
			{
				i = index.as<long>();
			}
			const long next_index = i + 1;
			if (auto* entry = find_entry(cf, section, std::to_string(next_index)))
			{
				return std::make_tuple(sol::make_object(ts, next_index), entry_get(ts, entry));
			}
			return std::make_tuple(sol::object(sol::lua_nil), sol::object(sol::lua_nil));
		}
	};

	sol::object make_proxy(sol::this_state ts, toml_v2::config_file* cf, const std::string& section)
	{
		sol::state_view lua(ts);
		sol::table registry = lua.registry();
		// The wrapper is an empty table: a shared metatable drives every read/write, and its (cf, section) live in the
		// weak-keyed registry maps, so nothing leaks into rawpairs and the wrapper is collected with its section.
		sol::table wrapper          = lua.create_table();
		sol::table metatable        = registry[k_proxy_metatable];
		sol::table cf_map           = registry[k_proxy_cf_map];
		sol::table section_map      = registry[k_proxy_section_map];
		wrapper[sol::metatable_key] = metatable;
		cf_map[wrapper]             = cf;
		section_map[wrapper]        = section;
		return wrapper;
	}

	// Recovers the (cf, section) a wrapper table points at, as a throwaway proxy the free metamethods delegate to.
	static mod_config_proxy recover(sol::this_state ts, const sol::table& wrapper)
	{
		sol::state_view lua(ts);
		sol::table registry       = lua.registry();
		sol::table cf_map         = registry[k_proxy_cf_map];
		sol::table section_map    = registry[k_proxy_section_map];
		toml_v2::config_file* cf  = cf_map[wrapper];
		const std::string section = section_map[wrapper];
		return mod_config_proxy{cf, section};
	}

	// Coerces a Lua index key to the string form config entries use (Chalk stringifies numeric keys). Returns false for
	// a key that is neither a string nor a number.
	static bool coerce_key(const sol::stack_object& key, std::string& out)
	{
		if (key.get_type() == sol::type::string)
		{
			out = key.as<std::string>();
			return true;
		}
		if (key.get_type() == sol::type::number)
		{
			out = std::to_string(key.as<long long>());
			return true;
		}
		return false;
	}

	static sol::object proxy_index(sol::this_state ts, sol::table self, sol::stack_object key)
	{
		std::string k;
		if (!coerce_key(key, k))
		{
			return sol::lua_nil;
		}
		return recover(ts, self).index(ts, k);
	}

	static void proxy_new_index(sol::this_state ts, sol::table self, sol::stack_object key, sol::stack_object value)
	{
		std::string k;
		if (!coerce_key(key, k))
		{
			return;
		}
		recover(ts, self).new_index(k, value);
	}

	static std::size_t proxy_length(sol::this_state ts, sol::table self)
	{
		return recover(ts, self).length();
	}

	static std::tuple<sol::object, sol::object, sol::object> proxy_pairs(sol::this_state ts, sol::table self)
	{
		return recover(ts, self).pairs(ts);
	}

	static std::tuple<sol::object, sol::object, sol::object> proxy_ipairs(sol::this_state ts, sol::table self)
	{
		return recover(ts, self).ipairs(ts);
	}

	static std::tuple<sol::object, sol::object> proxy_next(sol::this_state ts, sol::table self, sol::object key)
	{
		return recover(ts, self).next(ts, key);
	}

	static std::tuple<sol::object, sol::object> proxy_inext(sol::this_state ts, sol::table self, sol::object index)
	{
		return recover(ts, self).inext(ts, index);
	}

	// A setting's extracted metadata together with the section/key it belongs to, collected while walking config.lua
	// and then folded into the registry.
	struct collected_metadata
	{
		std::string section;
		std::string key;
		setting_metadata meta;
	};

	// Recursively binds a config.lua `defaults` table into `cf` under `section`, forwarding each leaf's description.
	// Nested tables become sub-sections ("section.key"). Each flat leaf whose description is a rich table has its
	// metadata extracted into `meta_out` (keyed by section+key), and every leaf that carries any configDesc entry (a
	// string or a table) is recorded in `described_out` so the menu can hide undescribed keys. config_file::bind
	// adopts a value already saved in the .cfg, preserving user edits, and binds under section "config", so the .cfg
	// stays byte-compatible with what SGG_Modding-Chalk wrote.
	static void bind_defaults(toml_v2::config_file* cf, const sol::table& defaults, const sol::object& desc_obj, const std::string& section, std::vector<collected_metadata>& meta_out, std::vector<std::tuple<std::string, std::string, std::string>>& defaults_out, std::vector<std::pair<std::string, std::string>>& described_out)
	{
		sol::table desc_tbl;
		const bool has_desc = desc_obj.is<sol::table>();
		if (has_desc)
		{
			desc_tbl = desc_obj.as<sol::table>();
		}

		for (const auto& [key_obj, value_obj] : defaults)
		{
			if (key_obj.get_type() != sol::type::string)
			{
				continue;
			}
			const std::string key = key_obj.as<std::string>();

			sol::object desc = sol::lua_nil;
			if (has_desc)
			{
				desc = desc_tbl[key];
			}
			const bool described = desc.get_type() != sol::type::lua_nil && desc.get_type() != sol::type::none;

			const sol::type vt = value_obj.get_type();
			std::optional<std::any> default_any;
			toml_v2::config_file::config_entry_base* bound_entry = nullptr;
			switch (vt)
			{
			case sol::type::table:
				bind_defaults(cf, value_obj.as<sol::table>(), desc, section + "." + key, meta_out, defaults_out, described_out);
				break;
			case sol::type::boolean:
				bound_entry = cf->bind(section, key, value_obj.as<bool>(), localized_fallback(describe(desc)));
				default_any = std::any(value_obj.as<bool>());
				break;
			case sol::type::number:
				bound_entry = cf->bind(section, key, value_obj.as<double>(), localized_fallback(describe(desc)));
				default_any = std::any(value_obj.as<double>());
				break;
			case sol::type::string:
				bound_entry = cf->bind(section, key, value_obj.as<std::string>(), localized_fallback(describe(desc)));
				default_any = std::any(value_obj.as<std::string>());
				break;
			default: continue;
			}

			// Capture the config.lua default, serialized exactly as the entry serializes its own value, so the menu's.
			// Reset can round-trip it back through set_serialized_value.
			if (default_any)
			{
				defaults_out.emplace_back(section, key, toml_v2::toml_type_converter::convert_to_string(*default_any));

				// Record a described leaf so the menu shows it; an undescribed leaf is hidden. Only leaves reach here
				// (default_any is set for bool/number/string, not a group table).
				if (described)
				{
					described_out.emplace_back(section, key);
				}
			}

			// A rich description table carries metadata. For a leaf it is the setting's metadata. For a nested group (a
			// table value). It is group-level metadata (e.g. order/displayName/hidden) declared alongside the child
			// descriptions. Registered under (section, key) either way.
			if (desc.is<sol::table>())
			{
				meta_out.push_back({section, key, extract_metadata(desc.as<sol::table>())});

				// A leaf may also declare an onChange callback. Attach it to the bound entry so a menu edit (or the
				// mod's own write) of this setting notifies the mod in Lua.
				if (bound_entry)
				{
					sol::object on_change = desc.as<sol::table>()["onChange"];
					if (on_change.is<sol::protected_function>())
					{
						attach_on_change(bound_entry, on_change.as<sol::protected_function>());
					}
				}
			}
		}
	}

	// Lua API: Function. Table: mod_settings. Name: load. Param: config_lua: string: Path, relative to the mod's
	// folder, of the config.lua that returns `config, configDesc`. Returns: table: A live read/write proxy over the
	// mod's config, index it to read a setting and assign to write one. Loads a mod's config.lua and registers its
	// settings under the Mods tab of the in-game Options menu, returning a live read/write proxy over the config. When
	// using this, you do not need to depend on `Chalk`.
	static sol::object load(sol::this_state ts, sol::this_environment this_env, const std::string& config_lua)
	{
		// Uses the calling mod (this_environment) to derive its <config folder>/<guid>.cfg path and create a native
		// config_file owned by that mod, loads the mod's config.lua, binds its defaults and descriptions into that
		// config_file, records any restart-required settings, and returns the proxy.
		if (!this_env)
		{
			return sol::lua_nil;
		}

		sol::state_view state = ts;
		sol::environment env  = this_env;

		auto* module = big::lua_module::this_from(this_env);
		if (!module)
		{
			return sol::lua_nil;
		}
		const std::string guid = module->guid();

		// .cfg path = rom.path.combine(rom.paths.config(), guid ".cfg") - identical to the path Chalk used, so an
		// existing .cfg is reused.
		sol::table rom               = env["rom"];
		sol::function path_combine   = rom["path"]["combine"];
		sol::function config_folder  = rom["paths"]["config"];
		const std::string cfg_folder = config_folder();
		const std::string cfg_path   = path_combine(cfg_folder, guid + ".cfg");

		// Create the config_file owned by this mod (freed when the mod unloads).
		auto& cf = module->m_data.m_config_files.emplace_back(std::make_unique<toml_v2::config_file>(cfg_path, true, guid));

		// Load the mod's config.lua (returns `config, configDesc`), relative to its folder.
		const std::string mod_folder      = env["_PLUGIN"]["plugins_mod_folder_path"];
		const std::string config_lua_path = mod_folder + "/" + config_lua;

		sol::load_result loaded = state.load_file(config_lua_path);
		if (!loaded.valid())
		{
			sol::error err = loaded;
			LOG(WARNING) << "[mod_settings] load: cannot load " << config_lua_path << ": " << err.what();
			return sol::lua_nil;
		}
		sol::protected_function config_chunk = loaded;
		sol::set_environment(env, config_chunk);
		sol::protected_function_result cfg_result = config_chunk();
		if (!cfg_result.valid())
		{
			sol::error err = cfg_result;
			LOG(WARNING) << "[mod_settings] load: error running " << config_lua_path << ": " << err.what();
			return sol::lua_nil;
		}
		sol::object defaults     = cfg_result[0];
		sol::object descriptions = cfg_result[1];

		// Bind the defaults into the config_file (section root "config", matching. Chalk) and collect each rich
		// setting's metadata, then persist the file.
		std::vector<collected_metadata> collected;
		std::vector<std::tuple<std::string, std::string, std::string>> collected_defaults; // (section.
		std::vector<std::pair<std::string, std::string>> collected_described;              // (section, key) with a desc
		if (defaults.is<sol::table>())
		{
			bind_defaults(cf.get(), defaults.as<sol::table>(), descriptions, "config", collected, collected_defaults, collected_described);
		}
		cf->save();

		// Keep this mod's configDesc alive in Lua so the menu can evaluate dynamic (function) description fields and
		// action callbacks at render time. Stored under rom.mod_settings._descs[guid], which is Lua-owned and recreated
		// per state, so no sol reference is cached in a dangling C++ static.
		if (sol::object ms_ns = rom["mod_settings"]; ms_ns.is<sol::table>())
		{
			if (sol::object descs = ms_ns.as<sol::table>()["_descs"]; descs.is<sol::table>())
			{
				descs.as<sol::table>()[guid] = descriptions;
			}
		}

		// Collect action buttons declared in configDesc (entries with an `action` function no config value).
		std::vector<action_info> actions;
		if (defaults.is<sol::table>())
		{
			collect_actions(defaults.as<sol::table>(), descriptions, root_section, actions);
		}

		// Collect virtual rows (configDesc entries marked `virtual = true` with no config value) and validate that
		// every configDesc entry resolves to a config value, an action, or a virtual marker (warns otherwise).
		std::vector<virtual_row_info> virtual_rows;
		if (defaults.is<sol::table>())
		{
			collect_virtual_rows(guid, defaults.as<sol::table>(), descriptions, root_section, virtual_rows);
		}

		// Read config.lua source to recover the author's key order (Lua pairs() and the alphabetical config map both
		// lose it), then rank every bound key by where it is defined.
		std::string source_text;
		{
			std::ifstream file(config_lua_path, std::ios::binary);
			if (file)
			{
				std::ostringstream ss;
				ss << file.rdbuf();
				source_text = ss.str();
			}
		}
		std::vector<std::tuple<std::size_t, std::string, std::string>> by_offset; // (offset, section, key).
		for (const auto& [def, entry] : cf->m_entries)
		{
			const std::size_t off = source_text.empty() ? std::string::npos : find_key_definition(source_text, def.m_key);
			by_offset.emplace_back(off, def.m_section, def.m_key);
		}
		// Rank virtual rows in the same source-order space as the config entries so they interleave with config rows.
		for (const auto& vr : virtual_rows)
		{
			const std::size_t off = source_text.empty() ? std::string::npos : find_key_definition(source_text, vr.key);
			by_offset.emplace_back(off, vr.section, vr.key);
		}
		std::stable_sort(by_offset.begin(),
		                 by_offset.end(),
		                 [](const auto& a, const auto& b)
		                 {
			                 return std::get<0>(a) < std::get<0>(b);
		                 });

		// Order the collected actions by their position in the config.lua source so their menu order is deterministic
		// and matches how the author wrote them (collect_actions walks in Lua pairs order, which is unspecified).
		std::stable_sort(actions.begin(),
		                 actions.end(),
		                 [&](const action_info& a, const action_info& b)
		                 {
			                 const std::size_t oa = source_text.empty() ? std::string::npos : find_key_definition(source_text, a.key);
			                 const std::size_t ob = source_text.empty() ? std::string::npos : find_key_definition(source_text, b.key);
			                 return oa < ob;
		                 });

		// Same for virtual rows.
		std::stable_sort(virtual_rows.begin(),
		                 virtual_rows.end(),
		                 [&](const virtual_row_info& a, const virtual_row_info& b)
		                 {
			                 const std::size_t oa = source_text.empty() ? std::string::npos : find_key_definition(source_text, a.key);
			                 const std::size_t ob = source_text.empty() ? std::string::npos : find_key_definition(source_text, b.key);
			                 return oa < ob;
		                 });

		// Register this mod's setting metadata + appearance order (replacing any from a previous load of the same mod).
		{
			std::scoped_lock lock(g_metadata_mutex);
			clear_metadata_for(guid);
			for (auto& cm : collected)
			{
				g_setting_metadata[metadata_key(guid, cm.section, cm.key)] = std::move(cm.meta);
			}
			for (auto& [section, key, serialized] : collected_defaults)
			{
				g_setting_default[metadata_key(guid, section, key)] = std::move(serialized);
			}
			for (const auto& [section, key] : collected_described)
			{
				g_described_keys.insert(metadata_key(guid, section, key));
			}
			int rank = 0;
			for (const auto& [off, section, key] : by_offset)
			{
				g_appearance_order[metadata_key(guid, section, key)] = rank++;
			}
			g_actions[guid]      = std::move(actions);
			g_virtual_rows[guid] = std::move(virtual_rows);
		}

		return make_proxy(ts, cf.get(), "config");
	}

	std::optional<setting_metadata> resolve_setting_metadata(const std::string& guid, const std::string& section, const std::string& key)
	{
		if (!big::g_lua_manager)
		{
			return std::nullopt;
		}
		sol::state_view state  = big::g_lua_manager->lua_state();
		const sol::object root = stored_descriptions(state, guid);
		const sol::object desc = navigate_description(root, section, key);
		if (!desc.is<sol::table>())
		{
			return std::nullopt;
		}
		const sol::table resolved = resolve_description(state, desc.as<sol::table>(), guid);
		setting_metadata m        = extract_metadata(resolved);
		m.has_dynamic             = false; // already resolved to concrete values.
		return m;
	}

	// True when the game is in the hub (the Crossroads): the game Lua global `CurrentHubRoom` is non-nil (the game sets
	// it to the current hub room while in the hub and clears it during a run). Reads the game's Lua state directly (the
	// same state mods run in, where `_G` is the game globals - see hades_lua.hpp), so it must be called on the game
	// thread while the state is alive. Returns false when the Lua manager is not up yet.
	bool game_is_in_hub()
	{
		if (!big::g_lua_manager)
		{
			return false;
		}
		sol::state_view state = big::g_lua_manager->lua_state();
		const sol::object chr = state["CurrentHubRoom"];
		return chr.get_type() != sol::type::lua_nil && chr.get_type() != sol::type::none;
	}

	std::vector<action_info> get_actions(const std::string& guid, const std::string& section)
	{
		std::vector<action_info> result;
		{
			std::scoped_lock lock(g_metadata_mutex);
			const auto it = g_actions.find(guid);
			if (it == g_actions.end())
			{
				return result;
			}
			for (const auto& a : it->second)
			{
				if (a.section == section)
				{
					result.push_back(a);
				}
			}
		}

		// Re-evaluate any dynamic fields (name/description/order/disabled) against the current game state, mirroring
		// resolve_setting_metadata for settings.
		if (big::g_lua_manager)
		{
			sol::state_view state  = big::g_lua_manager->lua_state();
			const sol::object root = stored_descriptions(state, guid);
			for (auto& a : result)
			{
				if (!a.has_dynamic)
				{
					continue;
				}
				const sol::object desc = navigate_description(root, a.section, a.key);
				if (desc.is<sol::table>())
				{
					read_action_fields(resolve_description(state, desc.as<sol::table>(), guid), a);
				}
			}
		}
		return result;
	}

	void invoke_action(const std::string& guid, const std::string& section, const std::string& key)
	{
		if (!big::g_lua_manager)
		{
			return;
		}
		sol::state_view state  = big::g_lua_manager->lua_state();
		const sol::object root = stored_descriptions(state, guid);
		const sol::object desc = navigate_description(root, section, key);
		if (!desc.is<sol::table>())
		{
			return;
		}
		const sol::object act = desc.as<sol::table>()["action"];
		if (act.get_type() != sol::type::function)
		{
			return;
		}
		sol::protected_function fn        = act;
		sol::protected_function_result rv = fn();
		if (!rv.valid())
		{
			const sol::error err = rv;
			LOG(WARNING) << "[mod_settings] action " << section << "." << key << " for " << guid << " failed: " << err.what();
		}
	}

	std::vector<virtual_row_info> get_virtual_rows(const std::string& guid, const std::string& section)
	{
		std::vector<virtual_row_info> result;
		std::scoped_lock lock(g_metadata_mutex);
		const auto it = g_virtual_rows.find(guid);
		if (it == g_virtual_rows.end())
		{
			return result;
		}
		for (const auto& vr : it->second)
		{
			if (vr.section == section)
			{
				result.push_back(vr);
			}
		}
		return result;
	}

	std::string get_virtual_display(const std::string& guid, const std::string& section, const std::string& key)
	{
		if (!big::g_lua_manager)
		{
			return {};
		}
		sol::state_view state  = big::g_lua_manager->lua_state();
		const sol::object root = stored_descriptions(state, guid);
		const sol::object desc = navigate_description(root, section, key);
		if (!desc.is<sol::table>())
		{
			return {};
		}
		sol::table t = desc.as<sol::table>();

		// The read-only display comes from `text`: a plain string, or a function returning a bool/number/string that
		// is stringified. Evaluated protected so a mod error cannot crash the menu. (`get`/`set` is the separate
		// editable value pair, added with interactive virtual rows; it is not a display path.)
		const sol::object text = t["text"];
		if (text.get_type() == sol::type::string)
		{
			return text.as<std::string>();
		}
		if (text.get_type() == sol::type::function)
		{
			sol::protected_function fn        = text;
			sol::protected_function_result rv = fn();
			if (!rv.valid())
			{
				const sol::error err = rv;
				LOG(WARNING) << "[mod_settings] virtual row " << section << "." << key << " for " << guid << " failed: " << err.what();
				return {};
			}
			return serialize_option(rv.get<sol::object>());
		}
		return {};
	}

	virtual_value get_virtual_value(const std::string& guid, const std::string& section, const std::string& key)
	{
		virtual_value out;
		if (!big::g_lua_manager)
		{
			return out;
		}
		sol::state_view state  = big::g_lua_manager->lua_state();
		const sol::object root = stored_descriptions(state, guid);
		const sol::object desc = navigate_description(root, section, key);
		if (!desc.is<sol::table>())
		{
			return out;
		}
		const sol::object get = desc.as<sol::table>()["get"];
		if (get.get_type() != sol::type::function)
		{
			return out;
		}
		sol::protected_function fn        = get;
		sol::protected_function_result rv = fn();
		if (!rv.valid())
		{
			const sol::error err = rv;
			LOG(WARNING) << "[mod_settings] virtual row get " << section << "." << key << " for " << guid << " failed: " << err.what();
			return out;
		}
		const sol::object v = rv.get<sol::object>();
		switch (v.get_type())
		{
		case sol::type::boolean:
			out.type    = virtual_value::kind::boolean;
			out.as_bool = v.as<bool>();
			break;
		case sol::type::number:
			out.type      = virtual_value::kind::number;
			out.as_number = v.as<double>();
			break;
		case sol::type::string:
			out.type      = virtual_value::kind::string;
			out.as_string = v.as<std::string>();
			break;
		default: break; // kind::none - the widget falls back to a read-only display.
		}
		return out;
	}

	void set_virtual_value(const std::string& guid, const std::string& section, const std::string& key, const virtual_value& value)
	{
		if (!big::g_lua_manager)
		{
			return;
		}
		sol::state_view state  = big::g_lua_manager->lua_state();
		const sol::object root = stored_descriptions(state, guid);
		const sol::object desc = navigate_description(root, section, key);
		if (!desc.is<sol::table>())
		{
			return;
		}
		const sol::object set = desc.as<sol::table>()["set"];
		if (set.get_type() != sol::type::function)
		{
			return;
		}
		sol::protected_function fn = set;
		sol::protected_function_result rv;
		switch (value.type)
		{
		case virtual_value::kind::boolean: rv = fn(value.as_bool); break;
		case virtual_value::kind::number:  rv = fn(value.as_number); break;
		case virtual_value::kind::string:  rv = fn(value.as_string); break;
		default:                           return; // nothing to write.
		}
		if (!rv.valid())
		{
			const sol::error err = rv;
			LOG(WARNING) << "[mod_settings] virtual row set " << section << "." << key << " for " << guid << " failed: " << err.what();
		}
	}

	// Lua API: Function. Table: mod_settings. Name: opt_out. Excludes the calling mod from the in-game mod settings
	// menu: it stays listed but greyed out and cannot be opened, with a note pointing the player to the mod's own
	// description. Use it when the mod should not be edited in-game. Works with Chalk or rom.mod_settings.load.
	static void opt_out(sol::this_environment this_env)
	{
		// Keyed by the calling mod's guid (which matches its config-file stem), so the menu can grey the matching row
		// however the mod manages its config.
		if (!this_env)
		{
			return;
		}
		auto* module = big::lua_module::this_from(this_env);
		if (!module)
		{
			return;
		}
		std::scoped_lock lock(g_metadata_mutex);
		g_opted_out_mods.insert(module->guid());
	}

	void bind_config_api(sol::state_view& state, sol::table& lua_ext)
	{
		// A fresh Lua state re-runs every mod's main.lua, so drop all per-mod registries before those calls re-register
		// them load() also clears its own guid, but a mod uninstalled since the last state would never call load again,
		// so its stale entries would otherwise linger forever. The opt-out set has no load() to hang a per-guid clear
		// off either. Clearing everything here keeps all four registries bounded to the currently-loaded mods.
		{
			std::scoped_lock lock(g_metadata_mutex);
			g_setting_metadata.clear();
			g_appearance_order.clear();
			g_setting_default.clear();
			g_opted_out_mods.clear();
			g_actions.clear();
			g_virtual_rows.clear();
			g_described_keys.clear();
		}

		// The config object handed to mods is a plain Lua table (so `type(config) == "table"`, matching
		// SGG_Modding-Chalk), driven by one shared metatable. It reproduces Chalk's full metamethod surface so mods
		// migrating off Chalk keep working: index/new_index read/write entries, and len/pairs/ipairs (plus ModUtil's
		// next/inext, which it reads via rawget(getmetatable(t), '__next'/'__inext')) make the config iterable like a
		// normal table. Each wrapper table's (cf, section) live in weak-keyed registry maps, so the wrapper stays empty
		// (nothing leaks into rawpairs) and is collected with it.
		sol::table proxy_metatable    = state.create_table();
		proxy_metatable["__index"]    = &proxy_index;
		proxy_metatable["__newindex"] = &proxy_new_index;
		proxy_metatable["__len"]      = &proxy_length;
		proxy_metatable["__pairs"]    = &proxy_pairs;
		proxy_metatable["__ipairs"]   = &proxy_ipairs;
		proxy_metatable["__next"]     = &proxy_next;
		proxy_metatable["__inext"]    = &proxy_inext;

		sol::table proxy_cf_map               = state.create_table();
		sol::table cf_map_meta                = state.create_table_with("__mode", "k");
		proxy_cf_map[sol::metatable_key]      = cf_map_meta;
		sol::table proxy_section_map          = state.create_table();
		sol::table section_map_meta           = state.create_table_with("__mode", "k");
		proxy_section_map[sol::metatable_key] = section_map_meta;

		sol::table registry           = state.registry();
		registry[k_proxy_metatable]   = proxy_metatable;
		registry[k_proxy_cf_map]      = proxy_cf_map;
		registry[k_proxy_section_map] = proxy_section_map;

		sol::table ns = lua_ext.create_named("mod_settings");
		ns.set_function("load", &load);
		ns.set_function("opt_out", &opt_out);

		// A Lua-owned table holding each mod's raw configDesc (rom.mod_settings._descs[guid]), so the menu can evaluate
		// dynamic (function) description fields and action callbacks at render time without caching sol references in.
		// C++ statics (which would dangle across a Lua-state reset).
		ns["_descs"] = state.create_table();
	}
} // namespace big::mod_settings
