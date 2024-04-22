#pragma once
#include "load_module_result.hpp"
#include "lua_module.hpp"
#include "module_info.hpp"

#include <file_manager/file_watcher.hpp>

namespace big
{
	class lua_manager
	{
	private:
		sol::state_view m_state;
		sol::protected_function m_loadfile;

		std::recursive_mutex m_module_lock;
		std::vector<std::shared_ptr<lua_module>> m_modules;

		folder m_config_folder;
		folder m_plugins_data_folder;
		folder m_plugins_folder;

		bool m_is_all_mods_loaded{};

	public:
		static constexpr auto lua_ext_namespace = "LuaExt";

		lua_manager(lua_State* game_lua_state, folder config_folder, folder plugins_data_folder, folder plugins_folder);
		~lua_manager();

		void init_lua_state();
		// used for sandboxing and limiting to only our custom search path for the lua require function
		void set_folder_for_lua_require();
		void sandbox_lua_os_library();
		void sandbox_lua_loads();
		void init_lua_api();

		void load_all_modules();
		void unload_all_modules();

		inline auto get_module_count() const
		{
			return m_modules.size();
		}

		inline const folder& get_config_folder() const
		{
			return m_config_folder;
		}

		inline const folder& get_plugins_data_folder() const
		{
			return m_plugins_data_folder;
		}

		inline const folder& get_plugins_folder() const
		{
			return m_plugins_folder;
		}

		void draw_menu_bar_callbacks();
		void always_draw_independent_gui();
		void draw_independent_gui();

		bool module_exists(const std::string& module_guid);
		std::weak_ptr<lua_module> get_module(const std::string& module_guid);

		void unload_module(const std::string& module_guid);
		load_module_result load_module(const module_info& module_info, bool ignore_failed_to_load = false);

		inline void for_each_module(auto func)
		{
			std::lock_guard guard(m_module_lock);

			for (auto& module : m_modules)
			{
				func(module);
			}
		}
	};

	inline lua_manager* g_lua_manager;
} // namespace big
