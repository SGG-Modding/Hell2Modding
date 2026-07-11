#include "mod_settings.hpp"

#include <lua/lua_manager.hpp>
#include <lua/lua_module.hpp>
#include <map>
#include <mutex>
#include <toml_v2/config_file.hpp>
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
	// rom.mod_settings.load. Keyed by guid + '\0' + section + '\0' + key. Currently records only
	// whether a setting requires a game restart to take effect (set via `restart_required = true`
	// in a setting's config.lua description table); extensible to context/visibility later. This
	// replaces the old sjson-hook auto-detection, which could not see a mod that starts disabled
	// (it registers no hooks until enabled), and never covered restarts needed for other reasons.
	static std::mutex g_metadata_mutex;
	static std::map<std::string, bool> g_restart_required_settings;

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
		for (auto it = g_restart_required_settings.begin(); it != g_restart_required_settings.end();)
		{
			it = (it->first.rfind(prefix, 0) == 0) ? g_restart_required_settings.erase(it) : std::next(it);
		}
	}

	bool setting_requires_restart(const std::string& guid, const std::string& section, const std::string& key)
	{
		std::scoped_lock lock(g_metadata_mutex);
		const auto it = g_restart_required_settings.find(metadata_key(guid, section, key));
		return it != g_restart_required_settings.end() && it->second;
	}

	// Extracts a description string from a config.lua description value, which may be a plain string
	// or a table with a `description` field (or `[1]` shorthand).
	static std::string describe(const sol::object& desc)
	{
		if (desc.get_type() == sol::type::string)
		{
			return desc.as<std::string>();
		}
		if (desc.is<sol::table>())
		{
			sol::table t         = desc.as<sol::table>();
			sol::object as_field = t["description"];
			if (as_field.get_type() == sol::type::string)
			{
				return as_field.as<std::string>();
			}
			sol::object as_first = t[1];
			if (as_first.get_type() == sol::type::string)
			{
				return as_first.as<std::string>();
			}
		}
		return "";
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

	// Recursively binds a config.lua `defaults` table into `cf` under `section`, forwarding each
	// leaf's description. Nested tables become sub-sections ("section.key"). Leaf keys whose
	// description declares restart_required are appended to `restart_out` as (section, key) pairs.
	// config_file::bind adopts a value already saved in the .cfg, preserving user edits, and binds
	// under section "config" so the .cfg stays byte-compatible with what SGG_Modding-Chalk wrote.
	static void bind_defaults(toml_v2::config_file* cf, const sol::table& defaults, const sol::object& desc_obj, const std::string& section, std::vector<std::pair<std::string, std::string>>& restart_out)
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
			switch (vt)
			{
			case sol::type::table:
				bind_defaults(cf, value_obj.as<sol::table>(), desc, section + "." + key, restart_out);
				break;
			case sol::type::boolean: cf->bind(section, key, value_obj.as<bool>(), describe(desc)); break;
			case sol::type::number:  cf->bind(section, key, value_obj.as<double>(), describe(desc)); break;
			case sol::type::string:  cf->bind(section, key, value_obj.as<std::string>(), describe(desc)); break;
			default:                 continue;
			}

			if (vt != sol::type::table && description_requires_restart(desc))
			{
				restart_out.emplace_back(section, key);
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
		// the author-declared restart-required settings, then persist the file.
		std::vector<std::pair<std::string, std::string>> restart_settings;
		if (defaults.is<sol::table>())
		{
			bind_defaults(cf.get(), defaults.as<sol::table>(), descriptions, "config", restart_settings);
		}
		cf->save();

		// Register this mod's restart-required settings into the metadata registry (replacing any
		// from a previous load of the same mod).
		{
			std::scoped_lock lock(g_metadata_mutex);
			clear_metadata_for(guid);
			for (const auto& [section, key] : restart_settings)
			{
				g_restart_required_settings[metadata_key(guid, section, key)] = true;
			}
		}

		return sol::make_object(ts, mod_config_proxy{cf.get(), "config"});
	}

	void bind_config_api(sol::state_view& state, sol::table& lua_ext)
	{
		// Register the live-config proxy usertype once per state (mods never construct it; instances
		// are returned from load). Its index/new_index read/write the underlying config entries.
		lua_ext.new_usertype<mod_config_proxy>("mod_config_proxy", sol::no_constructor, sol::meta_function::index, &mod_config_proxy::index, sol::meta_function::new_index, &mod_config_proxy::new_index);

		sol::table ns = lua_ext.create_named("mod_settings");
		ns.set_function("load", &load);
	}
} // namespace big::mod_settings
