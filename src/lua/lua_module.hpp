#pragma once
#include "load_module_result.hpp"
#include "lua/bindings/gui_element.hpp"
#include "lua_patch.hpp"
#include "module_info.hpp"

#include <thunderstore/v1/manifest.hpp>

namespace big
{
	class lua_module
	{
		module_info m_info;

		sol::environment m_env;

	public:
		struct lua_module_data
		{
			std::vector<sol::protected_function> m_on_all_mods_loaded_callbacks;

			std::vector<sol::protected_function> m_pre_code_execute_callbacks;
			std::vector<sol::protected_function> m_post_code_execute_callbacks;

			std::unordered_map<void*, std::vector<sol::protected_function>> m_pre_builtin_execute_callbacks;
			std::unordered_map<void*, std::vector<sol::protected_function>> m_post_builtin_execute_callbacks;

			std::unordered_map<void*, std::vector<sol::protected_function>> m_pre_script_execute_callbacks;
			std::unordered_map<void*, std::vector<sol::protected_function>> m_post_script_execute_callbacks;

			std::vector<std::unique_ptr<lua::gui::gui_element>> m_menu_bar_callbacks;
			std::vector<std::unique_ptr<lua::gui::gui_element>> m_always_draw_independent_gui;
			std::vector<std::unique_ptr<lua::gui::gui_element>> m_independent_gui;

			std::vector<std::unique_ptr<lua_patch>> m_registered_patches;

			std::vector<void*> m_allocated_memory;
		};

		lua_module_data m_data;

		lua_module(const module_info& module_info, sol::state_view& state);

		void cleanup();
		~lua_module();

		const std::filesystem::path& path() const;
		const ts::v1::manifest& manifest() const;
		const std::string& guid() const;

		load_module_result load_and_call_plugin(sol::state_view& state);

		// Returns true if any lua file change in the module folder happened.
		bool update_lua_file_entries(const std::string& new_hash);

		sol::environment& env();

		static std::string guid_from(sol::this_environment this_env);
		static big::lua_module* this_from(sol::this_environment this_env);
	};
} // namespace big
