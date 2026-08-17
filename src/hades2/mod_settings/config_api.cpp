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
#pragma region Metadata registries and accessors

	static std::mutex g_metadata_mutex;
	static std::map<std::string, setting_metadata> g_setting_metadata;

	static std::map<std::string, std::string> g_setting_default;

	static std::set<std::string> g_described_keys;

	static std::map<std::string, localized_text> g_opted_out_mods;

	static std::map<std::string, std::vector<action_info>> g_actions;

	static std::map<std::string, std::vector<virtual_row_info>> g_virtual_rows;

	// Author-declared menu categories that do not correspond to config sections.
	static std::map<std::string, std::vector<menu_group>> g_menu_groups;

	// Guids that loaded their settings through mod_settings.load.
	static std::set<std::string> g_mod_settings_mods;

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

	static void clear_metadata_for(const std::string& guid)
	{
		const std::string prefix = guid + '\0';
		for (auto it = g_setting_metadata.begin(); it != g_setting_metadata.end();)
		{
			it = (it->first.rfind(prefix, 0) == 0) ? g_setting_metadata.erase(it) : std::next(it);
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

	bool setting_is_described(const std::string& guid, const std::string& section, const std::string& key)
	{
		std::scoped_lock lock(g_metadata_mutex);
		return g_described_keys.contains(metadata_key(guid, section, key));
	}

	// True while the mod declared anything at all in its configDesc: a described key, an action, or a virtual row.
	bool mod_has_described_content(const std::string& guid)
	{
		std::scoped_lock lock(g_metadata_mutex);
		const std::string prefix = guid + '\0';
		for (const auto& k : g_described_keys)
		{
			if (k.rfind(prefix, 0) == 0)
			{
				return true;
			}
		}
		if (const auto it = g_actions.find(guid); it != g_actions.end() && !it->second.empty())
		{
			return true;
		}
		if (const auto it = g_virtual_rows.find(guid); it != g_virtual_rows.end() && !it->second.empty())
		{
			return true;
		}
		return false;
	}

	// True while the mod loaded its settings through mod_settings.load rather than Chalk.
	bool mod_declares_settings(const std::string& guid)
	{
		std::scoped_lock lock(g_metadata_mutex);
		return g_mod_settings_mods.contains(guid);
	}

	// True while the key is one the mod declared in its config table this session.
	bool setting_is_declared(const std::string& guid, const std::string& section, const std::string& key)
	{
		std::scoped_lock lock(g_metadata_mutex);
		if (!g_mod_settings_mods.contains(guid))
		{
			return true;
		}
		return g_setting_default.contains(metadata_key(guid, section, key));
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

	localized_text mod_opt_out_description(const std::string& guid)
	{
		std::scoped_lock lock(g_metadata_mutex);
		const auto it = g_opted_out_mods.find(guid);
		return it != g_opted_out_mods.end() ? it->second : localized_text{};
	}

	std::vector<menu_group> mod_menu_groups(const std::string& guid)
	{
		std::scoped_lock lock(g_metadata_mutex);
		const auto it = g_menu_groups.find(guid);
		return it != g_menu_groups.end() ? it->second : std::vector<menu_group>{};
	}

#pragma endregion

#pragma region Config.lua parsing helpers

	static std::string serialize_option(const sol::object& v);
	static editable_context parse_editable_context(const sol::object& o, editable_context fallback);

	// Accepts a plain scalar or a language-code table. Empty or absent yields an empty map.
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

	// Picks one language-independent value for the on-disk .cfg comment.
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

	// Reads a plain description or a rich table's `description` or `[1]` field.
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

	// Accepts a string or string array as a menu path. Empty means no override.
	static std::vector<std::string> parse_group(const sol::object& o)
	{
		std::vector<std::string> out;
		if (o.get_type() == sol::type::string)
		{
			out.push_back(o.as<std::string>());
		}
		else if (o.is<sol::table>())
		{
			sol::table t = o.as<sol::table>();
			for (std::size_t i = 1; i <= t.size(); ++i)
			{
				sol::object seg = t[i];
				if (seg.get_type() == sol::type::string)
				{
					out.push_back(seg.as<std::string>());
				}
			}
		}
		// '.' is the menu-path separator, so reject ambiguous segments.
		for (const auto& seg : out)
		{
			if (seg.find('.') != std::string::npos)
			{
				LOG(WARNING) << "[mod_settings] ignoring `group` override: segment '" << seg << "' contains '.', which is reserved as the menu-path separator (use an array of segments to nest).";
				return {};
			}
		}
		return out;
	}

	// Parses configDesc `groups` into menu_group nodes. Siblings sort by `order` then id because Lua order is lost.
	static std::vector<menu_group> parse_menu_groups(const sol::object& groups_obj)
	{
		std::vector<menu_group> out;
		if (!groups_obj.is<sol::table>())
		{
			return out;
		}
		groups_obj.as<sol::table>().for_each(
		    [&out](const sol::object& k, const sol::object& v)
		    {
			    if (k.get_type() != sol::type::string || !v.is<sol::table>())
			    {
				    return;
			    }
			    sol::table gt = v.as<sol::table>();
			    menu_group g;
			    g.id = k.as<std::string>();
			    if (g.id.find('.') != std::string::npos)
			    {
				    LOG(WARNING) << "[mod_settings] ignoring menu group id '" << g.id << "' containing '.', which is reserved as the menu-path separator (nest via a `groups` sub-table instead).";
				    return;
			    }
			    g.name                 = parse_localized(gt["displayName"]);
			    g.description          = parse_localized(gt["description"]);
			    g.disabled_description = parse_localized(gt["disabledDescription"]);
			    if (sol::object order = gt["order"]; order.get_type() == sol::type::number)
			    {
				    g.has_order = true;
				    g.order     = order.as<double>();
			    }
			    if (sol::object d = gt["disabled"]; d.is<bool>())
			    {
				    g.disabled = d.as<bool>();
			    }
			    g.context = parse_editable_context(gt["editableContext"], editable_context::any);

			    // Lua-function fields are re-evaluated by resolve_menu_group.
			    for (const char* field : {"displayName", "description", "disabledDescription", "disabled"})
			    {
				    if (gt[field].get_type() == sol::type::function)
				    {
					    g.has_dynamic = true;
					    break;
				    }
			    }
			    g.children = parse_menu_groups(gt["groups"]);
			    out.push_back(std::move(g));
		    });
		return out;
	}

	static bool description_requires_restart(const sol::object& desc)
	{
		if (!desc.is<sol::table>())
		{
			return false;
		}
		sol::object flag = desc.as<sol::table>()["restartRequired"];
		return flag.is<bool>() && flag.as<bool>();
	}

	// Parses an `editableContext` field, returning `fallback` for unknown values.
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

	// Serializes a Lua enum option exactly as a config entry serializes it.
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

#pragma endregion

#pragma region Metadata extraction

	// The menu derives widget kind from the value type plus `values`.
	static setting_metadata extract_metadata(const sol::table& desc)
	{
		setting_metadata m;
		m.description = describe(desc);

		sol::object display_name = desc["displayName"];
		m.name                   = parse_localized(display_name);

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

		// Virtual-row `type` pins the widget when get() may be nil at build time.
		if (sol::object type_field = desc["type"]; type_field.get_type() == sol::type::string)
		{
			const std::string t = type_field.as<std::string>();
			if (t == "boolean" || t == "bool")
			{
				m.type = widget_type::boolean;
			}
			else if (t == "number")
			{
				m.type = widget_type::number;
			}
			else if (t == "string")
			{
				m.type = widget_type::string;
			}
			else if (t == "enum" || t == "enumeration")
			{
				m.type = widget_type::enumeration;
			}
		}

		// Virtual-row `default` is restored by Reset.
		if (sol::object default_field = desc["default"]; default_field.valid() && default_field.get_type() != sol::type::lua_nil)
		{
			m.has_default   = true;
			m.default_value = serialize_option(default_field);
		}

		// The menu forces the master toggle and restartRequired settings to main_menu.
		m.context = parse_editable_context(desc["editableContext"], editable_context::any);

		// Lua-function fields are re-evaluated at render.
		for (const char* field : {"displayName", "description", "disabledDescription", "min", "max", "step", "values", "labels", "order", "disabled"})
		{
			if (desc[field].get_type() == sol::type::function)
			{
				m.has_dynamic = true;
				break;
			}
		}

		m.group = parse_group(desc["group"]);

		return m;
	}

#pragma endregion

#pragma region Description navigation and dynamic-field resolution

	// Lua-owned configDesc registry. Recreated each Lua state, so C++ sol references never dangle.
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

	// Internal keys are always the stringified form, while configDesc mirrors config's shape, so a numeric segment
	// has to be tried as a number too.
	static sol::object desc_child(const sol::table& node, const std::string& part)
	{
		sol::object child = node[part];
		if (child.valid() && child.get_type() != sol::type::lua_nil)
		{
			return child;
		}
		if (part.empty())
		{
			return sol::lua_nil;
		}
		for (const char c : part)
		{
			if (c < '0' || c > '9')
			{
				return sol::lua_nil;
			}
		}
		return node[std::stoll(part)];
	}

	// configDesc mirrors the config table under the "config" root.
	static sol::object navigate_description(const sol::object& root, const std::string& section, const std::string& key)
	{
		if (!root.is<sol::table>())
		{
			return sol::lua_nil;
		}
		sol::table node = root.as<sol::table>();


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
			sol::object child      = desc_child(node, part);
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
		return desc_child(node, key);
	}

	// Avoids ReturnOfModding's traceback logging and error tally.
	static int silent_error_handler(lua_State* /*L*/)
	{
		return 1; // keep the error object on the stack.
	}

	// Uses the silent handler so callers can report one concise warning.
	template<typename... Args>
	static sol::protected_function_result call_mod_callback(sol::protected_function fn, Args&&... args)
	{
		const lua_CFunction handler = &silent_error_handler;
		fn.set_error_handler(sol::object(fn.lua_state(), sol::in_place, handler));
		return fn(std::forward<Args>(args)...);
	}

	static sol::object evaluate_field(const sol::object& value, const std::string& guid, const char* field)
	{
		if (value.get_type() != sol::type::function)
		{
			return value;
		}
		sol::protected_function fn        = value;
		sol::protected_function_result rv = call_mod_callback(fn);
		if (!rv.valid())
		{
			const sol::error err = rv;
			LOG(WARNING) << "[mod_settings] dynamic '" << field << "' for " << guid << " failed: " << err.what();
			return sol::lua_nil;
		}
		return rv.get<sol::object>();
	}

	// Evaluates dynamic fields while preserving event and virtual-row callbacks.
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
			if (field == "onChanged" || field == "action" || field == "get" || field == "set" || field == "text")
			{
				out[k] = v;
				continue;
			}
			out[k] = evaluate_field(v, guid, field.c_str());
		}
		return out;
	}

#pragma endregion

#pragma region Action and virtual-row collection

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
		a.group   = parse_group(entry["group"]);
	}

	// Collects action buttons from configDesc, guided by config defaults.
	static void collect_actions(const sol::table& config_tbl, const sol::object& desc_obj, const std::string& section, std::vector<action_info>& out)
	{
		if (desc_obj.is<sol::table>())
		{
			sol::table desc = desc_obj.as<sol::table>();
			for (const auto& [k, v] : desc)
			{
				if (!v.is<sol::table>())
				{
					continue;
				}
				// configDesc may be written array-shaped, mirroring an array in config.
				std::string desc_key;
				if (k.get_type() == sol::type::string)
				{
					desc_key = k.as<std::string>();
				}
				else if (k.get_type() == sol::type::number)
				{
					desc_key = std::to_string(k.as<long long>());
				}
				else
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
				a.key     = desc_key;
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
		for (const auto& [k, v] : config_tbl)
		{
			if (!v.is<sol::table>())
			{
				continue;
			}
			// Array elements are bound under their stringified index, so recurse into those sections too.
			std::string child_key;
			if (k.get_type() == sol::type::string)
			{
				child_key = k.as<std::string>();
			}
			else if (k.get_type() == sol::type::number)
			{
				child_key = std::to_string(k.as<long long>());
			}
			else
			{
				continue;
			}
			const sol::object child_desc = desc_obj.is<sol::table>() ? desc_obj.as<sol::table>()[child_key] : sol::object(sol::lua_nil);
			collect_actions(v.as<sol::table>(), child_desc, section + "." + child_key, out);
		}
	}

	// Entry metadata fields are skipped when walking desc child rows.
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
		    "restartRequired",
		    "editableContext",
		    "showAsPercentage",
		    "isPercentage",
		    "onChanged",
		    "action",
		    "virtual",
		    "get",
		    "set",
		    "text",
		    "type",
		    "default",
		    "group",  // per-entry menu placement.
		    "groups", // root author group tree.
		};
		return reserved.contains(key);
	}

	// Collects virtual rows and logs desc entries that resolve to no config value, action, or virtual row.
	static void collect_virtual_rows(const std::string& guid, const sol::table& config_tbl, const sol::object& desc_obj, const std::string& section, std::vector<virtual_row_info>& out)
	{
		if (desc_obj.is<sol::table>())
		{
			sol::table desc = desc_obj.as<sol::table>();
			for (const auto& [k, v] : desc)
			{
				// configDesc may be written array-shaped, mirroring an array in config.
				std::string key;
				if (k.get_type() == sol::type::string)
				{
					key = k.as<std::string>();
				}
				else if (k.get_type() == sol::type::number)
				{
					key = std::to_string(k.as<long long>());
				}
				else
				{
					continue;
				}
				if (is_reserved_desc_field(key))
				{
					continue;
				}

				const std::string path    = section + "." + key;
				// Indexed with the original key, so configDesc mirrors whatever shape config uses.
				const sol::object cfg_val = config_tbl[k];
				const bool has_config     = cfg_val.valid() && cfg_val.get_type() != sol::type::lua_nil;
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
					if (is_virtual)
					{
						LOG(WARNING) << "[mod_settings] " << guid << ": configDesc entry '" << path << "' is marked virtual = true but also has a config value; treating it as a normal config setting.";
					}
					continue;
				}
				if (is_action)
				{
					continue;
				}
				if (!is_virtual)
				{
					LOG(WARNING) << "[mod_settings] " << guid << ": configDesc entry '" << path << "' has no matching config value and is not marked `virtual = true` or given an `action`. Did you forget to add '" << key << "' to config?";
					continue;
				}

				virtual_row_info vr;
				vr.section = section;
				vr.key     = key;
				vr.group   = parse_group(entry["group"]);
				if (sol::object o = entry["order"]; o.get_type() == sol::type::number)
				{
					vr.has_order = true;
					vr.order     = o.as<double>();
				}

				const bool has_get  = entry["get"].get_type() == sol::type::function;
				const bool has_set  = entry["set"].get_type() == sol::type::function;
				const sol::object t = entry["text"];
				const bool has_text = t.get_type() == sol::type::string || t.get_type() == sol::type::function;
				vr.interactive      = has_set;
				for (const char* field : {"displayName", "description", "text", "values", "min", "max", "step", "labels"})
				{
					if (entry[field].get_type() == sol::type::function)
					{
						vr.has_dynamic = true;
						break;
					}
				}
				if (has_get || entry["values"].valid()) // interactive rows with get/values are dynamic.
				{
					vr.has_dynamic = vr.has_dynamic || vr.interactive;
				}

				if (vr.interactive)
				{
					if (!has_get)
					{
						LOG(WARNING) << "[mod_settings] " << guid << ": interactive virtual row '" << path << "' has a `set` but no `get`, so its widget cannot read a value; add a `get` callback.";
						continue;
					}
				}
				else if (!has_text)
				{
					LOG(WARNING) << "[mod_settings] " << guid << ": virtual row '" << path << "' has no `text` (a string or a function returning one) and no `set` (to be interactive), so it has nothing to show.";
				}
				out.push_back(std::move(vr));
			}
		}
		for (const auto& [k, v] : config_tbl)
		{
			if (!v.is<sol::table>())
			{
				continue;
			}
			// Array elements are bound under their stringified index, so recurse into those sections too.
			std::string child_key;
			if (k.get_type() == sol::type::string)
			{
				child_key = k.as<std::string>();
			}
			else if (k.get_type() == sol::type::number)
			{
				child_key = std::to_string(k.as<long long>());
			}
			else
			{
				continue;
			}
			const sol::object child_desc = desc_obj.is<sol::table>() ? desc_obj.as<sol::table>()[child_key] : sol::object(sol::lua_nil);
			collect_virtual_rows(guid, v.as<sol::table>(), child_desc, section + "." + child_key, out);
		}
	}

#pragma endregion

#pragma region Config entry access and change hooks

	// A mod's config_file is destroyed and rebuilt on every hot reload and Lua state reset, and a stale one can
	// briefly outlive a reload, so the most recently registered file for a guid is the live one.
	toml_v2::config_file* live_config_file(const std::string& guid)
	{
		toml_v2::config_file* found = nullptr;
		for (auto* cfg : toml_v2::config_file::g_config_files)
		{
			if (cfg && cfg->m_config_file_stem_as_str == guid)
			{
				found = cfg;
			}
		}
		return found;
	}

	static toml_v2::config_file::config_entry_base* find_entry(toml_v2::config_file* cf, const std::string& section, const std::string& key)
	{
		if (!cf)
		{
			return nullptr;
		}
		toml_v2::config_definition def(section, key);
		return cf->try_get_entry(def);
	}

	// Treats a section as present if it has bound leaves or child sections.
	static bool has_section(toml_v2::config_file* cf, const std::string& section)
	{
		if (!cf)
		{
			return false;
		}
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

	// onChanged fires only for options-menu edits. The entry owns the callback for the Lua state's lifetime.
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
			sol::protected_function_result result = call_mod_callback(callback, changed->m_definition.m_key, value);
			if (!result.valid())
			{
				const sol::error err = result;
				LOG(WARNING) << "[mod_settings] onChanged callback failed for " << changed->m_definition.m_section << "."
				             << changed->m_definition.m_key << ": " << err.what();
			}
		};
	}

	// Positive integer keys make array-like sections work with #, ipairs, and inext.
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

#pragma endregion

#pragma region Config proxy

	// Shared metatable plus weak-keyed wrapper maps for config_file and section.
	static constexpr const char* k_proxy_metatable   = "h2m_mod_config_metatable";
	static constexpr const char* k_proxy_cf_map      = "h2m_mod_config_cf";
	static constexpr const char* k_proxy_section_map = "h2m_mod_config_section";

	// Chalk writes this placeholder key into expandable sections and hides it from reads and iteration. A config
	// migrating from Chalk still has them in its .cfg, so they stay hidden here too.
	static constexpr const char* section_empty_key = "...";

	static sol::object make_proxy(sol::this_state ts, const std::string& guid, const std::string& section);

	// Live config view. The mod's config_file is destroyed and rebuilt on every hot reload and Lua state reset, so
	// the proxy stores the owning guid and looks the file up on each access instead of holding a pointer that would
	// be left dangling.
	struct mod_config_proxy
	{
		std::string guid;
		std::string section;

		toml_v2::config_file* file() const
		{
			return live_config_file(guid);
		}

		sol::object index(sol::this_state ts, const std::string& key) const
		{
			if (key == section_empty_key)
			{
				return sol::lua_nil;
			}
			auto* cf = file();
			if (auto* entry = find_entry(cf, section, key))
			{
				return entry_get(ts, entry);
			}
			const std::string child = section + "." + key;
			if (has_section(cf, child))
			{
				return make_proxy(ts, guid, child);
			}
			return sol::lua_nil;
		}

		void new_index(const std::string& key, const sol::object& value) const
		{
			if (key == section_empty_key)
			{
				return;
			}
			auto* cf = file();
			if (auto* entry = find_entry(cf, section, key))
			{
				// Lua removes a key when it is assigned nil, and table.remove relies on that to shrink an array.
				// Without it a config array could grow but never shrink, leaving stray keys in the .cfg.
				if (value.get_type() == sol::type::lua_nil || value.get_type() == sol::type::none)
				{
					toml_v2::config_definition def(section, key);
					cf->remove(def);
					return;
				}
				entry_set(entry, value);
				return;
			}

			if (value.get_type() == sol::type::lua_nil || value.get_type() == sol::type::none)
			{
				return;
			}

			// Chalk binds an unknown key on assignment instead of dropping it, and does so recursively for tables.
			const std::string child = section + "." + key;
			if (value.is<sol::table>())
			{
				const mod_config_proxy child_proxy{guid, child};
				for (const auto& [k, v] : value.as<sol::table>())
				{
					if (k.get_type() == sol::type::string)
					{
						child_proxy.new_index(k.as<std::string>(), v);
					}
				}
				return;
			}
			if (!cf)
			{
				return;
			}
			switch (value.get_type())
			{
			case sol::type::boolean: cf->bind(section, key, value.as<bool>(), ""); break;
			case sol::type::number:  cf->bind(section, key, value.as<double>(), ""); break;
			case sol::type::string:  cf->bind(section, key, value.as<std::string>(), ""); break;
			default:                 break;
			}
		}

		sol::table children_snapshot(sol::this_state ts) const
		{
			sol::state_view lua(ts);
			sol::table out           = lua.create_table();
			const std::string prefix = section + ".";
			std::set<std::string> seen_children;
			auto* cf = file();
			if (!cf)
			{
				return out;
			}
			for (const auto& [def, entry] : cf->m_entries)
			{
				if (def.m_key == section_empty_key)
				{
					continue;
				}
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
						out[child] = make_proxy(ts, guid, prefix + child);
					}
				}
			}
			return out;
		}

		// __len returns the highest positive-integer leaf key.
		std::size_t length() const
		{
			std::size_t n = 0;
			auto* cf      = file();
			if (!cf)
			{
				return n;
			}
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

		// __pairs walks one level like a plain table.
		std::tuple<sol::object, sol::object, sol::object> pairs(sol::this_state ts) const
		{
			sol::state_view lua(ts);
			sol::table snapshot              = children_snapshot(ts);
			sol::protected_function pairs_fn = lua["pairs"];
			sol::protected_function_result r = pairs_fn(snapshot);
			return std::make_tuple(r.get<sol::object>(0), r.get<sol::object>(1), r.get<sol::object>(2));
		}

		// __ipairs is consulted by ipairs on Lua 5.2.
		std::tuple<sol::object, sol::object, sol::object> ipairs(sol::this_state ts) const
		{
			sol::state_view lua(ts);
			sol::table sequence = lua.create_table();
			const std::size_t n = length();
			auto* cf            = file();
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

		// __next is used by ModUtil's next/qrawpairs.
		std::tuple<sol::object, sol::object> next(sol::this_state ts, sol::object key) const
		{
			sol::state_view lua(ts);
			sol::table snapshot              = children_snapshot(ts);
			sol::protected_function next_fn  = lua["next"];
			sol::protected_function_result r = next_fn(snapshot, key);
			return std::make_tuple(r.get<sol::object>(0), r.get<sol::object>(1));
		}

		// __inext is used by ModUtil's inext/qrawipairs.
		std::tuple<sol::object, sol::object> inext(sol::this_state ts, sol::object index) const
		{
			long i = 0;
			if (index.is<long>())
			{
				i = index.as<long>();
			}
			const long next_index = i + 1;
			if (auto* entry = find_entry(file(), section, std::to_string(next_index)))
			{
				return std::make_tuple(sol::make_object(ts, next_index), entry_get(ts, entry));
			}
			return std::make_tuple(sol::object(sol::lua_nil), sol::object(sol::lua_nil));
		}
	};

	static sol::object install_proxy(sol::this_state ts, sol::table target, const std::string& guid, const std::string& section)
	{
		sol::state_view lua(ts);
		sol::table registry = lua.registry();

		std::vector<sol::object> keys;
		for (const auto& [k, v] : target)
		{
			keys.push_back(k);
		}
		for (const auto& k : keys)
		{
			target[k] = sol::lua_nil;
		}

		sol::table metatable      = registry[k_proxy_metatable];
		sol::table cf_map         = registry[k_proxy_cf_map];
		sol::table section_map    = registry[k_proxy_section_map];
		target[sol::metatable_key] = metatable;
		cf_map[target]             = guid;
		section_map[target]        = section;
		return target;
	}

	sol::object make_proxy(sol::this_state ts, const std::string& guid, const std::string& section)
	{
		sol::state_view lua(ts);
		// The empty wrapper keeps state in weak-keyed maps, so rawpairs stays empty.
		return install_proxy(ts, lua.create_table(), guid, section);
	}

	static mod_config_proxy recover(sol::this_state ts, const sol::table& wrapper)
	{
		sol::state_view lua(ts);
		sol::table registry       = lua.registry();
		sol::table cf_map         = registry[k_proxy_cf_map];
		sol::table section_map    = registry[k_proxy_section_map];
		const std::string guid    = cf_map[wrapper];
		const std::string section = section_map[wrapper];
		return mod_config_proxy{guid, section};
	}

	// Chalk stringifies numeric config keys.
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

#pragma endregion

#pragma region Default binding and config.lua load

	struct collected_metadata
	{
		std::string section;
		std::string key;
		setting_metadata meta;
	};

	// Existing .cfg values are adopted under section "config" to stay byte-compatible with SGG_Modding-Chalk.
	static void bind_defaults(toml_v2::config_file* cf, const sol::table& defaults, const sol::object& desc_obj, const std::string& section, std::vector<collected_metadata>& meta_out, std::vector<std::tuple<std::string, std::string, std::string>>& defaults_out, std::vector<std::pair<std::string, std::string>>& described_out)
	{
		sol::table desc_tbl;
		const bool has_desc = desc_obj.is<sol::table>();
		if (has_desc)
		{
			desc_tbl = desc_obj.as<sol::table>();
		}

		auto bind_one = [&](const std::string& key, const sol::object& value_obj, const sol::object& desc)
		{
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
			default: return;
			}

			// Capture the serialized default for Reset.
			if (default_any)
			{
				defaults_out.emplace_back(section, key, toml_v2::toml_type_converter::convert_to_string(*default_any));

				if (described)
				{
					described_out.emplace_back(section, key);
				}
			}

			// Rich description tables carry leaf or group metadata.
			if (desc.is<sol::table>())
			{
				meta_out.push_back({section, key, extract_metadata(desc.as<sol::table>())});

				if (bound_entry)
				{
					sol::object on_changed = desc.as<sol::table>()["onChanged"];
					if (on_changed.is<sol::protected_function>())
					{
						attach_on_change(bound_entry, on_changed.as<sol::protected_function>());
					}
				}
			}
		};

		// The array part first, described by the matching array entry in configDesc, then the string keys.
		for (std::size_t i = 1;; ++i)
		{
			sol::object v = defaults[i];
			if (!v.valid() || v.get_type() == sol::type::lua_nil)
			{
				break;
			}
			bind_one(std::to_string(i), v, has_desc ? sol::object(desc_tbl[i]) : sol::object(sol::lua_nil));
		}

		for (const auto& [key_obj, value_obj] : defaults)
		{
			if (key_obj.get_type() == sol::type::string)
			{
				const std::string key = key_obj.as<std::string>();
				bind_one(key, value_obj, has_desc ? sol::object(desc_tbl[key]) : sol::object(sol::lua_nil));
			}
		}
	}

	// Lua API: Function. Table: mod_settings. Name: load. Param: configFilePath: string: Path, relative to the mod's
	// folder, of the `config.lua` that returns `config` and `configDesc`. Returns: table: A live read/write proxy over
	// the mod's config. Index it to read a setting and assign to write one. Registers the mod's settings under the Mods
	// tab of the in-game Options menu. Also manages the mod's `.cfg` file, setting default values for new options and
	// loading values saved to it by users. When using this, your mod does not need to depend on or use `Chalk`.
	static sol::object load(sol::this_state ts, sol::this_environment this_env, const std::string& config_file_path)
	{
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

		// Reuses Chalk's .cfg path.
		sol::table rom               = env["rom"];
		sol::function path_combine   = rom["path"]["combine"];
		sol::function config_folder  = rom["paths"]["config"];
		const std::string cfg_folder = config_folder();
		const std::string cfg_path   = path_combine(cfg_folder, guid + ".cfg");

		auto& cf = module->m_data.m_config_files.emplace_back(std::make_unique<toml_v2::config_file>(cfg_path, true, guid));

		const std::string mod_folder      = env["_PLUGIN"]["plugins_mod_folder_path"];
		const std::string config_lua_path = mod_folder + "/" + config_file_path;

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

		// Root section matches Chalk for .cfg compatibility.
		std::vector<collected_metadata> collected;
		std::vector<std::tuple<std::string, std::string, std::string>> collected_defaults; // (section, key, serialized)
		std::vector<std::pair<std::string, std::string>> collected_described;              // (section, key) with a desc
		if (defaults.is<sol::table>())
		{
			bind_defaults(cf.get(), defaults.as<sol::table>(), descriptions, "config", collected, collected_defaults, collected_described);
		}
		cf->save();

		// Keep configDesc Lua-owned so dynamic fields and action callbacks do not dangle across state resets.
		if (sol::object ms_ns = rom["mod_settings"]; ms_ns.is<sol::table>())
		{
			if (sol::object descs = ms_ns.as<sol::table>()["_descs"]; descs.is<sol::table>())
			{
				descs.as<sol::table>()[guid] = descriptions;
			}
		}

		std::vector<action_info> actions;
		if (defaults.is<sol::table>())
		{
			collect_actions(defaults.as<sol::table>(), descriptions, root_section, actions);
		}

		std::vector<virtual_row_info> virtual_rows;
		if (defaults.is<sol::table>())
		{
			collect_virtual_rows(guid, defaults.as<sol::table>(), descriptions, root_section, virtual_rows);
		}

		std::vector<menu_group> menu_groups;
		if (descriptions.is<sol::table>())
		{
			menu_groups = parse_menu_groups(descriptions.as<sol::table>()["groups"]);
		}

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
			g_actions[guid]      = std::move(actions);
			g_virtual_rows[guid] = std::move(virtual_rows);
			g_menu_groups[guid]  = std::move(menu_groups);
			g_mod_settings_mods.insert(guid);
		}

		// Reuses the mod's own config table so `config` stays the same object it declared, now reading live values.
		if (defaults.is<sol::table>())
		{
			return install_proxy(ts, defaults.as<sol::table>(), guid, "config");
		}
		return make_proxy(ts, guid, "config");
	}

#pragma endregion

#pragma region Dynamic metadata and game-state accessors

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
		m.has_dynamic             = false; // resolved to concrete values.
		return m;
	}

	std::optional<menu_group> resolve_menu_group(const std::string& guid, const std::vector<std::string>& path)
	{
		if (!big::g_lua_manager || path.empty())
		{
			return std::nullopt;
		}
		sol::state_view state  = big::g_lua_manager->lua_state();
		const sol::object root = stored_descriptions(state, guid);
		if (!root.is<sol::table>())
		{
			return std::nullopt;
		}

		sol::object node = root.as<sol::table>()["groups"];
		sol::table entry;
		for (std::size_t i = 0; i < path.size(); ++i)
		{
			if (!node.is<sol::table>())
			{
				return std::nullopt;
			}
			sol::object child = node.as<sol::table>()[path[i]];
			if (!child.is<sol::table>())
			{
				return std::nullopt;
			}
			entry = child.as<sol::table>();
			node  = entry["groups"];
		}

		const sol::table r = resolve_description(state, entry, guid);
		menu_group g;
		g.id                   = path.back();
		g.name                 = parse_localized(r["displayName"]);
		g.description          = parse_localized(r["description"]);
		g.disabled_description = parse_localized(r["disabledDescription"]);
		if (sol::object order = r["order"]; order.get_type() == sol::type::number)
		{
			g.has_order = true;
			g.order     = order.as<double>();
		}
		if (sol::object d = r["disabled"]; d.is<bool>())
		{
			g.disabled = d.as<bool>();
		}
		g.context = parse_editable_context(r["editableContext"], editable_context::any);
		return g;
	}

	// Reads CurrentHubRoom from the game Lua state. Call on the game thread while the state is alive.
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
				if (section.empty() || a.section == section) // empty section means all sections.
				{
					result.push_back(a);
				}
			}
		}

		// Re-evaluate dynamic fields against the current game state.
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
		sol::protected_function_result rv = call_mod_callback(fn);
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
			if (section.empty() || vr.section == section) // empty section means all sections.
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

		// `text` may be a string or a function returning a stringifiable scalar.
		const sol::object text = t["text"];
		if (text.get_type() == sol::type::string)
		{
			return text.as<std::string>();
		}
		if (text.get_type() == sol::type::function)
		{
			sol::protected_function fn        = text;
			sol::protected_function_result rv = call_mod_callback(fn);
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
		sol::protected_function_result rv = call_mod_callback(fn);
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
		default: break; // kind::none falls back to read-only display.
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
		case virtual_value::kind::boolean: rv = call_mod_callback(fn, value.as_bool); break;
		case virtual_value::kind::number:  rv = call_mod_callback(fn, value.as_number); break;
		case virtual_value::kind::string:  rv = call_mod_callback(fn, value.as_string); break;
		default:                           return;
		}
		if (!rv.valid())
		{
			const sol::error err = rv;
			LOG(WARNING) << "[mod_settings] virtual row set " << section << "." << key << " for " << guid << " failed: " << err.what();
		}
	}

#pragma endregion

#pragma region Virtual-row value helpers and reset

	static double parse_serialized_number(const std::string& s)
	{
		try
		{
			return std::stod(s);
		}
		catch (...)
		{
			return 0.0;
		}
	}

	// Enum options and strings are both carried as strings.
	static virtual_value::kind kind_of_widget(widget_type t)
	{
		switch (t)
		{
		case widget_type::boolean:     return virtual_value::kind::boolean;
		case widget_type::number:      return virtual_value::kind::number;
		case widget_type::string:
		case widget_type::enumeration: return virtual_value::kind::string;
		default:                       return virtual_value::kind::none;
		}
	}

	static virtual_value virtual_value_from_serialized(virtual_value::kind kind, const std::string& serialized)
	{
		virtual_value v;
		v.type = kind;
		switch (kind)
		{
		case virtual_value::kind::boolean: v.as_bool = (serialized == "true"); break;
		case virtual_value::kind::number:  v.as_number = parse_serialized_number(serialized); break;
		case virtual_value::kind::string:  v.as_string = serialized; break;
		default:                           break;
		}
		return v;
	}

	// Guesses a default's kind when neither get() nor `type` pins it.
	static virtual_value::kind guess_kind_from_serialized(const std::string& s)
	{
		if (s == "true" || s == "false")
		{
			return virtual_value::kind::boolean;
		}
		try
		{
			std::size_t consumed = 0;
			(void)std::stod(s, &consumed);
			if (consumed == s.size())
			{
				return virtual_value::kind::number;
			}
		}
		catch (...)
		{
		}
		return virtual_value::kind::string;
	}

	bool reset_virtual_row_to_default(const std::string& guid, const std::string& section, const std::string& key)
	{
		bool interactive = false;
		{
			std::scoped_lock lock(g_metadata_mutex);
			const auto it = g_virtual_rows.find(guid);
			if (it != g_virtual_rows.end())
			{
				for (const auto& vr : it->second)
				{
					if (vr.section == section && vr.key == key)
					{
						interactive = vr.interactive; // read-only rows have no set() to reset.
						break;
					}
				}
			}
		}
		if (!interactive)
		{
			return false;
		}

		const auto meta = resolve_setting_metadata(guid, section, key);
		if (!meta || !meta->has_default)
		{
			return false; // only rows with `default` reset.
		}

		// Prefer live get() kind, then `type`, enum values, then the serialized default.
		const virtual_value cur  = get_virtual_value(guid, section, key);
		virtual_value::kind kind = cur.type;
		if (kind == virtual_value::kind::none)
		{
			kind = kind_of_widget(meta->type);
		}
		if (kind == virtual_value::kind::none)
		{
			kind = !meta->values.empty() ? virtual_value::kind::string : guess_kind_from_serialized(meta->default_value);
		}

		const virtual_value target = virtual_value_from_serialized(kind, meta->default_value);
		const bool unchanged       = cur.type == target.type
		                       && ((kind == virtual_value::kind::boolean && cur.as_bool == target.as_bool)
		                           || (kind == virtual_value::kind::number && cur.as_number == target.as_number)
		                           || (kind == virtual_value::kind::string && cur.as_string == target.as_string));
		if (unchanged)
		{
			return false;
		}
		set_virtual_value(guid, section, key, target);
		return true;
	}

#pragma endregion

#pragma region Opt-out and API registration

	// Lua API: Function. Table: mod_settings. Name: opt_out. Param: description: string: Optional. A plain string or a
	// localization table `{ en = "...", de = "..." }` shown in place of the generic note when the mod's disabled row is
	// hovered. Excludes the calling mod from the in-game mod settings menu: it stays listed but will be greyed out and
	// cannot be opened. Use it when the mod should not be edited in-game. Works with Chalk or rom.mod_settings.load.
	static void opt_out(sol::this_environment this_env, sol::object description)
	{
		if (!this_env)
		{
			return;
		}
		auto* module = big::lua_module::this_from(this_env);
		if (!module)
		{
			return;
		}
		localized_text note;
		if (description.valid() && description != sol::lua_nil)
		{
			note = parse_localized(description);
		}
		std::scoped_lock lock(g_metadata_mutex);
		g_opted_out_mods[module->guid()] = std::move(note);
	}

	void bind_config_api(sol::state_view& state, sol::table& lua_ext)
	{
		// Each fresh Lua state re-registers loaded mods, so clear per-mod registries first.
		{
			std::scoped_lock lock(g_metadata_mutex);
			g_setting_metadata.clear();
			g_setting_default.clear();
			g_opted_out_mods.clear();
			g_actions.clear();
			g_virtual_rows.clear();
			g_menu_groups.clear();
			g_described_keys.clear();
		}

		// The proxy stays a plain empty Lua table while weak-keyed maps hold its live config state.
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

		// Lua-owned configDesc storage avoids dangling C++ sol references across Lua-state resets.
		ns["_descs"] = state.create_table();
	}

#pragma endregion

} // namespace big::mod_settings
