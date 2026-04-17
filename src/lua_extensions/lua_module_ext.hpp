#pragma once

#include "bindings/hades/inputs.hpp"
#include "sjson_overlay.hpp"
#include "lua/lua_module.hpp"

#include <paths/paths.hpp>

namespace big
{
	struct lua_module_data_ext
	{
		std::vector<sol::protected_function> m_on_pre_import;
		std::vector<sol::protected_function> m_on_post_import;

		std::vector<sol::protected_function> m_on_button_hover;

		struct on_sjson_game_data_read_t
		{
			std::string m_file_path;
			bool m_is_string_read{};
			sol::protected_function m_callback;
		};

		std::vector<on_sjson_game_data_read_t> m_on_sjson_game_data_read;

		std::map<std::string, std::vector<lua::hades::inputs::keybind_callback>> m_keybinds;
	};

	class lua_module_ext : public lua_module
	{
	public:
		lua_module_data_ext m_data_ext;

		lua_module_ext(const module_info& module_info, sol::environment& env) :
		    lua_module(module_info, env)
		{
			set_sjson_data_path();
		}

		lua_module_ext(const module_info& module_info, sol::state_view& state) :
		    lua_module(module_info, state)
		{
			set_sjson_data_path();
		}

		inline void cleanup() override
		{
			lua_module::cleanup();

			m_data_ext = {};
		}

	private:
		void set_sjson_data_path()
		{
			auto sjson_data_path = g_file_manager.get_project_folder("plugins_data").get_path() / m_info.m_guid / sjson_overlay::SJSON_DATA_DIR_NAME;
			auto sjson_data_path_string = std::string(reinterpret_cast<const char*>(sjson_data_path.u8string().c_str()));

			sol::table ns = m_env["_PLUGIN"];
			if (ns.valid())
			{
				ns["sjson_data_path"] = sjson_data_path_string;
			}
		}
	};
} // namespace big
