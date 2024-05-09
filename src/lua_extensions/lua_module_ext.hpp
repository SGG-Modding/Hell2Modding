#pragma once

#include "lua/lua_module.hpp"

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
	};

	class lua_module_ext : public lua_module
	{
	public:
		lua_module_data_ext m_data_ext;

		lua_module_ext(const module_info& module_info, sol::state_view& state) :
		    lua_module(module_info, state)
		{
		}

		inline void cleanup() override
		{
			lua_module::cleanup();

			m_data_ext = {};
		}
	};
} // namespace big
