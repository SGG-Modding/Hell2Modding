#include "lua_module.hpp"

#include "file_manager/file_manager.hpp"
#include "lua_manager.hpp"

namespace big
{
	lua_module::lua_module(const module_info& module_info, sol::state_view& state) :
	    m_info(module_info),
	    m_env(state, sol::create, state.globals())
	{
		// Lua API: Table
		// Name: _ENV - Plugin Specific Global Table
		// Each mod/plugin have their own global table containing helpers, such as:
		// - Their own guid
		//
		// - Path to their own folder inside `config`: Used for data that must persist between sessions that can be manipulated by the user.
		//
		// - Path to their own folder inside `plugins_data`: Used for data that must persist between sessions but not be manipulated by the user.
		//
		// - Path to their own folder inside `plugins`: Location of .lua, README, manifest.json files.
		//
		// You can access other mods helpers through the `mods[OTHER_MOD_GUID]` table.
		//
		// **Example Usage:**
		//
		// ```lua
		// print(_ENV["!guid"])
		//
		// for n in pairs(h2m.mods[_ENV["!guid"]]) do
		//     log.info(n)
		// end
		// ```

		// Lua API: Field
		// Table: _ENV - Plugin Specific Global Table
		// Field: !guid: string
		// Guid of the mod.
		m_env["!guid"] = m_info.m_guid;

		// Lua API: Field
		// Table: _ENV - Plugin Specific Global Table
		// Field: !config_mod_folder_path: string
		// Path to the mod folder inside `config`
		auto config_mod_folder_path = g_file_manager.get_project_folder("config").get_path() / m_info.m_guid;
		auto config_mod_folder_path_string = std::string(reinterpret_cast<const char*>(config_mod_folder_path.u8string().c_str()));
		m_env["!config_mod_folder_path"] = config_mod_folder_path_string;

		// Lua API: Field
		// Table: _ENV - Plugin Specific Global Table
		// Field: !plugins_data_mod_folder_path: string
		// Path to the mod folder inside `plugins_data`
		auto plugins_data_mod_folder_path = g_file_manager.get_project_folder("plugins_data").get_path() / m_info.m_guid;
		auto plugins_data_mod_folder_path_string =
		    std::string(reinterpret_cast<const char*>(plugins_data_mod_folder_path.u8string().c_str()));
		m_env["!plugins_data_mod_folder_path"] = plugins_data_mod_folder_path_string;

		// Lua API: Field
		// Table: _ENV - Plugin Specific Global Table
		// Field: !plugins_mod_folder_path: string
		// Path to the mod folder inside `plugins`
		auto plugins_mod_folder_path_string = std::string(reinterpret_cast<const char*>(m_info.m_folder_path.u8string().c_str()));
		m_env["!plugins_mod_folder_path"] = plugins_mod_folder_path_string;

		// Lua API: Field
		// Table: _ENV - Plugin Specific Global Table
		// Field: !this: lua_module*
		m_env["!this"] = this;
	}

	void lua_module::cleanup()
	{
		for (auto memory : m_data.m_allocated_memory)
		{
			delete[] memory;
		}

		m_data = {};
	}

	lua_module::~lua_module()
	{
		cleanup();
	}

	const std::filesystem::path& lua_module::path() const
	{
		return m_info.m_path;
	}

	const ts::v1::manifest& lua_module::manifest() const
	{
		return m_info.m_manifest;
	}

	const std::string& lua_module::guid() const
	{
		return m_info.m_guid;
	}

	sol::environment& lua_module::env()
	{
		return m_env;
	}

	load_module_result lua_module::load_and_call_plugin(sol::state_view& state)
	{
		auto result = state.safe_script_file(m_info.m_path.string(), m_env, &sol::script_pass_on_error, sol::load_mode::text);

		if (!result.valid())
		{
			LOG(FATAL) << m_info.m_guid << " failed to load: " << result.get<sol::error>().what();
			Logger::FlushQueue();

			return load_module_result::FAILED_TO_LOAD;
		}
		else
		{
			LOG(INFO) << "Loaded " << m_info.m_guid;

			// Lua API: Field
			// Table: h2m.mods
			// Field: [Mod GUID]: string
			// Each mod once loaded will have a key in this table, the key will be their guid string and the value their `_ENV`.
			state.traverse_set(lua_manager::lua_api_namespace, "mods", m_info.m_guid, m_env);
		}

		return load_module_result::SUCCESS;
	}

	bool lua_module::update_lua_file_entries(const std::string& new_hash)
	{
		const bool is_different = m_info.m_lua_file_entries_hash != new_hash;

		m_info.m_lua_file_entries_hash = new_hash;

		return is_different;
	}

	std::string lua_module::guid_from(sol::this_environment this_env)
	{
		sol::environment& env            = this_env;
		sol::optional<std::string> _guid = env["!guid"];
		if (_guid)
		{
			return _guid.value();
		}

		return g_lua_manager->get_fallback_module()->guid();
	}

	big::lua_module* lua_module::this_from(sol::this_environment this_env)
	{
		sol::environment& env                 = this_env;
		sol::optional<big::lua_module*> _this = env["!this"];
		if (_this)
		{
			return _this.value();
		}

		return g_lua_manager->get_fallback_module();
	}
} // namespace big
