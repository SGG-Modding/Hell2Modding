#pragma once
#include "load_module_result.hpp"
#include "lua_module.hpp"
#include "module_info.hpp"

namespace big
{
	class lua_manager
	{
	private:
		sol::state_view m_state;

		std::recursive_mutex m_module_lock;
		std::vector<std::unique_ptr<lua_module>> m_modules;

		folder m_config_folder;
		folder m_plugins_data_folder;
		folder m_plugins_folder;

		bool m_is_all_mods_loaded{};

	public:
		static inline constexpr auto lua_api_namespace = "h2m";

		lua_manager(lua_State* game_lua_state, folder config_folder, folder plugins_data_folder, folder plugins_folder);
		~lua_manager();

		void init_lua_state();
		void init_lua_api();

		void load_fallback_module();
		lua_module* get_fallback_module();

		void load_all_modules();
		void unload_all_modules();

		void update_file_watch_reload_modules();

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

		void unload_module(const std::string& module_guid);
		load_module_result load_module(const module_info& module_info, bool ignore_failed_to_load = false);

		inline void for_each_module(auto func)
		{
			std::scoped_lock guard(m_module_lock);

			for (auto& module : m_modules)
			{
				func(module);
			}
		}
	};

	inline std::recursive_mutex g_lua_manager_mutex;
	inline bool g_is_lua_state_valid = false;
	inline std::unique_ptr<lua_manager> g_lua_manager_instance;
	inline lua_manager* g_lua_manager;
} // namespace big
