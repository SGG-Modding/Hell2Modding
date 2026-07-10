#include "mod_settings.hpp"

#include <lua/lua_manager.hpp>
#include <lua/lua_module.hpp>
#include <toml_v2/config_file.hpp>

// clang-format off
#include <AsyncLogger/Logger.hpp>
using namespace al;
// clang-format on
#undef ERROR

namespace big::mod_settings
{
	// Merge + read/write proxy, embedded Lua loaded once at init. It operates purely on a
	// config_file created on the C++ side and returns the `bind(config_file, defaults,
	// descriptions)` function. It binds under section "config" so the .cfg stays byte-compatible
	// with what SGG_Modding-Chalk wrote (and with r2modman). config_file:bind adopts a value
	// already saved in the .cfg, preserving user edits.
	static constexpr const char* g_helper_lua = R"LUA(
local flat_types = { string = true, number = true, boolean = true }
local section_root = "config"

local function find_entry(config_file, section, key)
	for def, entry in pairs(config_file.entries) do
		if def.section == section and def.key == key then
			return entry
		end
	end
	return nil
end

local function has_section(config_file, section)
	local prefix = section .. "."
	for def in pairs(config_file.entries) do
		if def.section == section or def.section:sub(1, #prefix) == prefix then
			return true
		end
	end
	return false
end

local function describe(desc)
	if type(desc) == "string" then return desc end
	if type(desc) == "table" then return desc.description or desc[1] or "" end
	return ""
end

local function bind_defaults(config_file, defaults, desc, section)
	for k, v in pairs(defaults) do
		local key = tostring(k)
		local t = type(v)
		local d = desc and desc[k]
		if t == "table" then
			bind_defaults(config_file, v, (type(d) == "table") and d or nil, section .. "." .. key)
		elseif flat_types[t] then
			config_file:bind(section, key, v, describe(d))
		end
	end
end

local function make_proxy(config_file, section)
	return setmetatable({}, {
		__index = function(_, k)
			local key = tostring(k)
			local entry = find_entry(config_file, section, key)
			if entry then return entry:get() end
			local child = section .. "." .. key
			if has_section(config_file, child) then return make_proxy(config_file, child) end
			return nil
		end,
		__newindex = function(_, k, v)
			local entry = find_entry(config_file, section, tostring(k))
			if entry then entry:set(v) end
		end,
	})
end

return function(config_file, defaults, descriptions)
	bind_defaults(config_file, defaults, descriptions or {}, section_root)
	config_file:save()
	return make_proxy(config_file, section_root)
end
)LUA";

	static sol::protected_function g_bind;

	// rom.mod_settings.load(config_lua): native replacement for chalk.auto. Uses the calling
	// mod (this_environment) to derive its <config folder>/<guid>.cfg path and create a native
	// config_file owned by that mod, loads the mod's config.lua, then binds via the embedded
	// helper and returns a read/write proxy.
	static sol::object load(sol::this_state ts, sol::this_environment this_env, const std::string& config_lua)
	{
		if (!this_env || !g_bind.valid())
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

		sol::object cf_obj                = sol::make_object(ts, cf.get());
		sol::protected_function_result pr = g_bind(cf_obj, defaults, descriptions);
		if (!pr.valid())
		{
			sol::error err = pr;
			LOG(WARNING) << "[mod_settings] load: bind failed: " << err.what();
			return sol::lua_nil;
		}
		return pr;
	}

	void bind_config_api(sol::state_view& state, sol::table& lua_ext)
	{
		sol::load_result loaded = state.load(g_helper_lua, "@h2m_mod_settings_helper");
		if (!loaded.valid())
		{
			sol::error err = loaded;
			LOG(WARNING) << "[mod_settings] failed to load embedded config helper: " << err.what();
			return;
		}
		sol::protected_function chunk             = loaded;
		sol::protected_function_result bind_maker = chunk();
		if (!bind_maker.valid())
		{
			sol::error err = bind_maker;
			LOG(WARNING) << "[mod_settings] failed to init embedded config helper: " << err.what();
			return;
		}
		g_bind = bind_maker;

		sol::table ns = lua_ext.create_named("mod_settings");
		ns.set_function("load", &load);
	}
} // namespace big::mod_settings
