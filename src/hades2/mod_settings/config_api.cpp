#include "mod_settings.hpp"

#include <algorithm>
#include <any>
#include <cctype>
#include <climits>
#include <format>
#include <fstream>
#include <lua/lua_manager.hpp>
#include <lua/lua_module.hpp>
#include <map>
#include <mutex>
#include <optional>
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
	// Author-declared per-setting metadata registry, populated from each mod's config.lua by
	// rom.mod_settings.load. Keyed by guid + '\0' + section + '\0' + key. Holds the display-name
	// override, numeric bounds, enum options, ordering, and the restart-required flag that the
	// settings menu reads to pick and drive a widget. Only settings whose config.lua description is
	// a rich table are registered; the rest fall back to type-based rendering.
	static std::mutex g_metadata_mutex;
	static std::map<std::string, setting_metadata> g_setting_metadata;

	// Per-setting appearance order (rank of a key's definition in config.lua), populated for EVERY
	// bound key (not just those with rich metadata). Keyed the same way as g_setting_metadata. The
	// menu uses it to order rows that have no author-declared `order` in their config-file source
	// order, because Lua pairs() and the alphabetical config map both lose the config.lua order.
	static std::map<std::string, int> g_appearance_order;

	// Serialized config.lua default for every bound key (whether or not it has a rich metadata
	// table), captured at load. The settings menu's Reset action restores a setting to this value.
	// Keyed the same way as g_setting_metadata (guid + '\0' + section + '\0' + key).
	static std::map<std::string, std::string> g_setting_default;

	// Guids of mods that called rom.mod_settings.opt_out(), i.e. asked not to be configured through
	// the in-game menu. Guarded by g_metadata_mutex. Cleared and rebuilt on each Lua-state init (see
	// bind_config_api) because opt_out re-runs with each mod's main.lua.
	static std::set<std::string> g_opted_out_mods;

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

	// Drops a mod's metadata before it re-registers: config.lua may change between loads, and the
	// Lua state is recreated on App::Reset (so load runs again for every mod).
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

	// Finds the byte offset of a key's definition ("<key> =") in config.lua source, whole-word and
	// not "==", or npos. The first match is the key's place in the returned `config` defaults table
	// (defined before configDesc), which is the author's intended display order. Occurrences inside
	// strings/prose don't match because they are not followed by a bare '='.
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

	static std::string serialize_option(const sol::object& v); // defined below

	// Parses a user-facing string field that is either a plain scalar or a localization table (keyed by
	// the game's language folder codes, e.g. { en = "...", ["zh-TW"] = "..." }). A scalar is stored under
	// the empty key; a table contributes one entry per string-keyed string value. An empty or absent
	// value yields an empty map (i.e. no override).
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

	// Resolves a localized string to a single language-independent value for on-disk use (the .cfg
	// comment), which is not re-written per language: English, then the unlocalized value, then any
	// entry. The in-game menu resolves to the live game language separately at render time.
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

	// Extracts the (possibly localized) description from a config.lua description value, which may be a
	// plain string, or a rich table with a `description` field (or `[1]` shorthand) that is itself a
	// plain string or a localization table.
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

	// True if a config.lua description table declares `restart_required = true`.
	static bool description_requires_restart(const sol::object& desc)
	{
		if (!desc.is<sol::table>())
		{
			return false;
		}
		sol::object flag = desc.as<sol::table>()["restart_required"];
		return flag.is<bool>() && flag.as<bool>();
	}

	// Serializes a Lua enum-option value (bool/number/string) into the exact string form a config
	// entry serializes to, so the menu can match an option against the stored value. Numbers use
	// the same locale-invariant std::format the toml converter uses, and every config number is
	// stored as a double.
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

	// Builds a setting_metadata from a config.lua description table for a flat (non-table) value.
	// Missing fields keep their defaults. The widget kind is not stored: the menu derives it from
	// the config value's type plus the presence of `values` (enum), so authors never declare a
	// `type`. Author-only inputs that cannot be inferred (name, bounds, enum options/labels,
	// order, hidden, restart) are what this captures.
	static setting_metadata extract_metadata(const sol::table& desc)
	{
		setting_metadata m;
		m.description = describe(desc);

		// Display-name override (`display_name`); empty -> the menu prettifies the key. May be a plain
		// string or a localization table.
		sol::object display_name = desc["display_name"];
		m.name                   = parse_localized(display_name);

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
		// Enum option display labels (parallel to `values`); each may be a plain string or a
		// localization table.
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

		sol::object freetext_field = desc["freetext"];
		if (freetext_field.is<bool>())
		{
			m.freetext = freetext_field.as<bool>();
		}

		sol::object show_pct_field = desc["show_as_percentage"];
		if (show_pct_field.is<bool>())
		{
			m.show_as_percentage = show_pct_field.as<bool>();
		}
		sol::object is_pct_field = desc["is_percentage"];
		if (is_pct_field.is<bool>())
		{
			m.is_percentage = is_pct_field.as<bool>();
		}

		m.restart_required = description_requires_restart(desc);

		return m;
	}

	// Finds the config entry for (section, key), or nullptr. m_entries is keyed by config_definition,
	// so this is a direct map lookup.
	static toml_v2::config_file::config_entry_base* find_entry(toml_v2::config_file* cf, const std::string& section, const std::string& key)
	{
		toml_v2::config_definition def(section, key);
		return cf->try_get_entry(def);
	}

	// True if `section` is a bound section or the parent of one (some entry's section equals
	// `section` or starts with `section + "."`). Used to expose nested config tables via the proxy.
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

	// Writes a Lua value into a config entry, dispatching on the value's Lua type (matching the
	// toml_v2 config_entry:set overloads: bool/number/string).
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

	// Live read/write view over a config_file section, returned to the mod as its `config` object.
	// Reads/writes go straight through to the underlying config entries (so the in-game menu and the
	// mod always see the same values); nested sections resolve to child proxies. It holds a raw
	// config_file pointer (not a sol reference): the config_file is owned by the mod and both it and
	// this proxy are recreated together per Lua state, so nothing dangles across an App::Reset.
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
				return sol::make_object(ts, mod_config_proxy{cf, child});
			}
			return sol::lua_nil;
		}

		void new_index(const std::string& key, const sol::object& value) const
		{
			if (auto* entry = find_entry(cf, section, key))
			{
				entry_set(entry, value);
			}
		}
	};

	// A setting's extracted metadata together with the section/key it belongs to, collected while
	// walking config.lua and then folded into the registry.
	struct collected_metadata
	{
		std::string section;
		std::string key;
		setting_metadata meta;
	};

	// Recursively binds a config.lua `defaults` table into `cf` under `section`, forwarding each
	// leaf's description. Nested tables become sub-sections ("section.key"). Each flat leaf whose
	// description is a rich table has its metadata extracted into `meta_out` (keyed by section+key).
	// config_file::bind adopts a value already saved in the .cfg, preserving user edits, and binds
	// under section "config" so the .cfg stays byte-compatible with what SGG_Modding-Chalk wrote.
	static void bind_defaults(toml_v2::config_file* cf, const sol::table& defaults, const sol::object& desc_obj, const std::string& section, std::vector<collected_metadata>& meta_out, std::vector<std::tuple<std::string, std::string, std::string>>& defaults_out)
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

			const sol::type vt = value_obj.get_type();
			std::optional<std::any> default_any;
			switch (vt)
			{
			case sol::type::table:
				bind_defaults(cf, value_obj.as<sol::table>(), desc, section + "." + key, meta_out, defaults_out);
				break;
			case sol::type::boolean:
				cf->bind(section, key, value_obj.as<bool>(), localized_fallback(describe(desc)));
				default_any = std::any(value_obj.as<bool>());
				break;
			case sol::type::number:
				cf->bind(section, key, value_obj.as<double>(), localized_fallback(describe(desc)));
				default_any = std::any(value_obj.as<double>());
				break;
			case sol::type::string:
				cf->bind(section, key, value_obj.as<std::string>(), localized_fallback(describe(desc)));
				default_any = std::any(value_obj.as<std::string>());
				break;
			default: continue;
			}

			// Capture the config.lua default, serialized exactly as the entry serializes its own
			// value, so the menu's Reset can round-trip it back through set_serialized_value.
			if (default_any)
			{
				defaults_out.emplace_back(section, key, toml_v2::toml_type_converter::convert_to_string(*default_any));
			}

			// A rich description table carries metadata. For a leaf it is the setting's metadata; for a
			// nested group (a table value) it is group-level metadata (e.g. order/display_name/hidden)
			// declared alongside the child descriptions. Registered under (section, key) either way.
			if (desc.is<sol::table>())
			{
				meta_out.push_back({section, key, extract_metadata(desc.as<sol::table>())});
			}
		}
	}

	// rom.mod_settings.load(config_lua): native replacement for chalk.auto. Uses the calling mod
	// (this_environment) to derive its <config folder>/<guid>.cfg path and create a native
	// config_file owned by that mod, loads the mod's config.lua, binds its defaults/descriptions
	// into that config_file, records any restart-required settings, and returns a live read/write
	// proxy over the config.
	static sol::object load(sol::this_state ts, sol::this_environment this_env, const std::string& config_lua)
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

		// .cfg path = rom.path.combine(rom.paths.config(), guid .. ".cfg") - identical to the
		// path Chalk used, so an existing .cfg is reused.
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

		// Bind the defaults into the config_file (section root "config", matching Chalk) and collect
		// each rich setting's metadata, then persist the file.
		std::vector<collected_metadata> collected;
		std::vector<std::tuple<std::string, std::string, std::string>> collected_defaults; // (section, key, serialized)
		if (defaults.is<sol::table>())
		{
			bind_defaults(cf.get(), defaults.as<sol::table>(), descriptions, "config", collected, collected_defaults);
		}
		cf->save();

		// Read config.lua source to recover the author's key order (Lua pairs() and the alphabetical
		// config map both lose it), then rank every bound key by where it is defined.
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
		std::vector<std::tuple<std::size_t, std::string, std::string>> by_offset; // (offset, section, key)
		for (const auto& [def, entry] : cf->m_entries)
		{
			const std::size_t off = source_text.empty() ? std::string::npos : find_key_definition(source_text, def.m_key);
			by_offset.emplace_back(off, def.m_section, def.m_key);
		}
		std::stable_sort(by_offset.begin(),
		                 by_offset.end(),
		                 [](const auto& a, const auto& b)
		                 {
			                 return std::get<0>(a) < std::get<0>(b);
		                 });

		// Register this mod's setting metadata + appearance order (replacing any from a previous load
		// of the same mod).
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
			int rank = 0;
			for (const auto& [off, section, key] : by_offset)
			{
				g_appearance_order[metadata_key(guid, section, key)] = rank++;
			}
		}

		return sol::make_object(ts, mod_config_proxy{cf.get(), "config"});
	}

	// rom.mod_settings.opt_out(): the calling mod asks not to be configured through the in-game mod
	// settings menu. The mod is still listed there (removing it would look like a missing/broken mod),
	// but its row is greyed, cannot be opened, and shows a note pointing the user back to the mod's own
	// description for configuration. Keyed by the calling mod's guid (which matches its config-file
	// stem), so it applies however the mod manages its config (Chalk or rom.mod_settings.load).
	static void opt_out(sol::this_environment this_env)
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
		std::scoped_lock lock(g_metadata_mutex);
		g_opted_out_mods.insert(module->guid());
	}

	void bind_config_api(sol::state_view& state, sol::table& lua_ext)
	{
		// A fresh Lua state re-runs every mod's main.lua, so drop all per-mod registries before those
		// calls re-register them. load() also clears its own guid, but a mod uninstalled since the last
		// state would never call load again, so its stale entries would otherwise linger forever; the
		// opt-out set has no load() to hang a per-guid clear off either. Clearing everything here keeps
		// all four registries bounded to the currently-loaded mods.
		{
			std::scoped_lock lock(g_metadata_mutex);
			g_setting_metadata.clear();
			g_appearance_order.clear();
			g_setting_default.clear();
			g_opted_out_mods.clear();
		}

		// Register the live-config proxy usertype once per state (mods never construct it; instances
		// are returned from load). Its index/new_index read/write the underlying config entries.
		lua_ext.new_usertype<mod_config_proxy>("mod_config_proxy", sol::no_constructor, sol::meta_function::index, &mod_config_proxy::index, sol::meta_function::new_index, &mod_config_proxy::new_index);

		sol::table ns = lua_ext.create_named("mod_settings");
		ns.set_function("load", &load);
		ns.set_function("opt_out", &opt_out);
	}
} // namespace big::mod_settings
