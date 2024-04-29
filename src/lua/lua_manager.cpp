#include "lua_manager.hpp"

#include "bindings/gui.hpp"
#include "bindings/hades/audio.hpp"
#include "bindings/imgui.hpp"
#include "bindings/log.hpp"
#include "bindings/path.hpp"
#include "bindings/paths.hpp"
#include "bindings/toml/toml_lua.hpp"
#include "file_manager/file_manager.hpp"
#include "string/string.hpp"

#include <hooks/hooking.hpp>
#include <memory/gm_address.hpp>
#include <thunderstore/v1/manifest.hpp>
#include <unordered_set>

namespace big
{
	static std::optional<module_info> get_module_info(const std::filesystem::path& module_path)
	{
		constexpr auto thunderstore_manifest_json_file_name = "manifest.json";
		std::filesystem::path manifest_path;
		std::filesystem::path current_folder = module_path.parent_path();
		std::filesystem::path root_folder    = g_file_manager.get_base_dir();
		while (true)
		{
			if (current_folder == root_folder)
			{
				break;
			}

			const auto potential_manifest_path = current_folder / thunderstore_manifest_json_file_name;
			if (std::filesystem::exists(potential_manifest_path))
			{
				manifest_path = potential_manifest_path;
				break;
			}

			if (current_folder.has_parent_path())
			{
				current_folder = current_folder.parent_path();
			}
			else
			{
				break;
			}
		}

		if (!std::filesystem::exists(manifest_path))
		{
			LOG(WARNING) << "No manifest path, can't load " << reinterpret_cast<const char*>(module_path.u8string().c_str());
			return {};
		}

		std::ifstream manifest_file(manifest_path);
		nlohmann::json manifest_json = nlohmann::json::parse(manifest_file, nullptr, false, true);

		ts::v1::manifest manifest = manifest_json.get<ts::v1::manifest>();

		manifest.version = semver::version::parse(manifest.version_number);

		for (const auto& dep : manifest.dependencies)
		{
			const auto splitted = big::string::split(dep, '-');
			if (splitted.size() == 3)
			{
				manifest.dependencies_no_version_number.push_back(splitted[0] + '-' + splitted[1]);
			}
			else
			{
				LOG(FATAL) << "Invalid dependency string " << dep << " inside the following manifest: " << manifest_path << ". Example format: AuthorName-ModName-1.0.0";
			}
		}

		const std::string folder_name = (char*)current_folder.filename().u8string().c_str();
		const auto sep_count          = std::ranges::count(folder_name, '-');
		if (sep_count != 1)
		{
			LOGF(FATAL,
			     "Bad folder name ({}) for the following mod: {}. Should be the following format: AuthorName-ModName",
			     folder_name,
			     manifest.name);
		}

		std::vector<std::string> lua_file_entries;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(current_folder, std::filesystem::directory_options::skip_permission_denied))
		{
			if (entry.exists() && entry.path().extension() == ".lua")
			{
				std::string lua_file_entry  = (char*)entry.path().filename().u8string().c_str();
				lua_file_entry             += std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(entry.last_write_time().time_since_epoch()).count());
				lua_file_entries.push_back(lua_file_entry);
			}
		}

		std::sort(lua_file_entries.begin(), lua_file_entries.end());
		std::string final_hash = "";
		for (const auto& file_entry : lua_file_entries)
		{
			final_hash += file_entry;
		}

		const std::string guid = folder_name;
		return {{
		    .m_lua_file_entries_hash = final_hash,
		    .m_path                  = module_path,
		    .m_folder_path           = current_folder,
		    .m_guid                  = guid,
		    .m_guid_with_version     = guid + "-" + manifest.version_number,
		    .m_manifest              = manifest,
		}};
	}

	lua_manager::lua_manager(lua_State* game_lua_state, folder config_folder, folder plugins_data_folder, folder plugins_folder) :
	    m_state(game_lua_state),
	    m_config_folder(config_folder),
	    m_plugins_data_folder(plugins_data_folder),
	    m_plugins_folder(plugins_folder)
	{
		g_lua_manager = this;

		init_lua_state();

		load_fallback_module();
		load_all_modules();

		lua::window::deserialize();
	}

	lua_manager::~lua_manager()
	{
		lua::window::serialize();

		unload_all_modules();

		g_lua_manager = nullptr;
	}

	static void delete_everything()
	{
		std::scoped_lock l(g_lua_manager_mutex);

		g_is_lua_state_valid = false;

		g_lua_manager_instance.reset();

		LOG(INFO) << "state is no longer valid!";
	}

	static int the_state_is_going_down(lua_State* L)
	{
		delete_everything();

		return 0;
	}

	// https://sol2.readthedocs.io/en/latest/exceptions.html
	static int exception_handler(lua_State* L, sol::optional<const std::exception&> maybe_exception, sol::string_view description)
	{
		// L is the lua state, which you can wrap in a state_view if necessary
		// maybe_exception will contain exception, if it exists
		// description will either be the what() of the exception or a description saying that we hit the general-case catch(...)
		if (maybe_exception)
		{
			const std::exception& ex = *maybe_exception;
			LOG(FATAL) << ex.what();
		}
		else
		{
			LOG(FATAL) << description;
		}
		Logger::FlushQueue();

		// you must push 1 element onto the stack to be
		// transported through as the error object in Lua
		// note that Lua -- and 99.5% of all Lua users and libraries -- expects a string
		// so we push a single string (in our case, the description of the error)
		return sol::stack::push(L, description);
	}

	static void panic_handler(sol::optional<std::string> maybe_msg)
	{
		LOG(FATAL) << "Lua is in a panic state and will now abort() the application";
		if (maybe_msg)
		{
			const std::string& msg = maybe_msg.value();
			LOG(FATAL) << "error message: " << msg;
		}
		Logger::FlushQueue();

		// When this function exits, Lua will exhibit default behavior and abort()
	}

	static int traceback_error_handler(lua_State* L)
	{
		std::string msg = "An unknown error has triggered the error handler";
		sol::optional<sol::string_view> maybetopmsg = sol::stack::unqualified_check_get<sol::string_view>(L, 1, &sol::no_panic);
		if (maybetopmsg)
		{
			const sol::string_view& topmsg = maybetopmsg.value();
			msg.assign(topmsg.data(), topmsg.size());
		}
		luaL_traceback(L, L, msg.c_str(), 1);
		sol::optional<sol::string_view> maybetraceback = sol::stack::unqualified_check_get<sol::string_view>(L, -1, &sol::no_panic);
		if (maybetraceback)
		{
			const sol::string_view& traceback = maybetraceback.value();
			msg.assign(traceback.data(), traceback.size());
		}
		LOG(FATAL) << msg;
		return sol::stack::push(L, msg);
	}

	void lua_manager::init_lua_state()
	{
		m_state.set_exception_handler(exception_handler);
		m_state.set_panic(sol::c_call<decltype(&panic_handler), &panic_handler>);
		lua_CFunction traceback_function = sol::c_call<decltype(&traceback_error_handler), &traceback_error_handler>;
		sol::protected_function::set_default_handler(sol::object(m_state.lua_state(), sol::in_place, traceback_function));

		// Register our cleanup functions when the state get destroyed.
		{
			const std::string my_inscrutable_key = "..hell2modding\xF0\x9F\x8F\xB4 \xF0\x9F\x8F\xB4 "
			                                       "\xF0\x9F\x8F\xB4 \xF0\x9F\x8F\xB4 \xF0\x9F\x8F\xB4";
			sol::table my_takedown_metatable     = m_state.create_table_with();
			my_takedown_metatable[sol::meta_function::garbage_collect] = the_state_is_going_down;
			sol::table my_takedown_table = m_state.create_named_table(my_inscrutable_key, sol::metatable_key, my_takedown_metatable);
		}

		// clang-format off
		m_state.open_libraries(
			sol::lib::package,
			sol::lib::os,
			sol::lib::debug,
			sol::lib::io);
		// clang-format on

		init_lua_api();
	}

	void lua_manager::init_lua_api()
	{
		sol::table lua_ext = m_state.create_named_table(lua_api_namespace);
		sol::table mods    = lua_ext.create_named("mods");
		// Lua API: Function
		// Table: h2m.mods
		// Name: on_all_mods_loaded
		// Param: callback: function: callback that will be called once all mods are loaded. The callback function should match signature func()
		// Registers a callback that will be called once all mods are loaded. Will be called instantly if mods are already loaded and that you are just hot-reloading your mod.
		mods["on_all_mods_loaded"] = [](sol::protected_function cb, sol::this_environment env)
		{
			big::lua_module* mdl = big::lua_module::this_from(env);
			if (mdl)
			{
				mdl->m_data.m_on_all_mods_loaded_callbacks.push_back(cb);
			}
		};

		// Lua API: Function
		// Table: h2m
		// Name: on_pre_import
		// Param: function: signature (string file_name, current_ENV_for_this_import) return nil or _ENV
		// The passed function will be called before the game loads a .lua script from the game's Content/Scripts folder.
		// The _ENV returned (if not nil) by the passed function gives you a way to define the _ENV of this lua script.
		lua_ext.set_function("on_pre_import",
		                     [](sol::protected_function f, sol::this_environment env)
		                     {
			                     auto mod = lua_module::this_from(env);
			                     if (mod)
			                     {
				                     mod->m_data.m_on_pre_import.push_back(f);
			                     }
		                     });

		// Lua API: Function
		// Table: h2m
		// Name: on_pre_import
		// Param: function: signature (string file_name)
		// The passed function will be called after the game loads a .lua script from the game's Content/Scripts folder.
		lua_ext.set_function("on_post_import",
		                     [](sol::protected_function f, sol::this_environment env)
		                     {
			                     auto mod = lua_module::this_from(env);
			                     if (mod)
			                     {
				                     mod->m_data.m_on_post_import.push_back(f);
			                     }
		                     });

		// Let's keep that list sorted the same as the solution file explorer
		lua::hades::audio::bind(lua_ext);
		lua::toml_lua::bind(lua_ext);
		lua::gui::bind(lua_ext);
		lua::imgui::bind(lua_ext);
		lua::log::bind(m_state, lua_ext);
		lua::path::bind(lua_ext);
		lua::paths::bind(lua_ext);
	}

	static void imgui_text(const char* fmt, const std::string& str)
	{
		if (str.size())
		{
			ImGui::Text(fmt, str.c_str());
		}
	}

	void lua_manager::draw_menu_bar_callbacks()
	{
		std::scoped_lock guard(m_module_lock);

		for (const auto& module : m_modules)
		{
			if (ImGui::BeginMenu(module->guid().c_str()))
			{
				if (ImGui::BeginMenu("Mod Info"))
				{
					const auto& manifest = module->manifest();
					imgui_text("Version: %s", manifest.version_number);
					imgui_text("Website URL: %s", manifest.website_url);
					imgui_text("Description: %s", manifest.description);
					if (manifest.dependencies.size())
					{
						int i = 0;
						for (const auto& dependency : manifest.dependencies)
						{
							imgui_text(std::format("Dependency[{}]: %s", i++).c_str(), dependency);
						}
					}

					ImGui::EndMenu();
				}

				for (const auto& element : module->m_data.m_menu_bar_callbacks)
				{
					element->draw();
				}

				ImGui::EndMenu();
			}
		}
	}

	void lua_manager::always_draw_independent_gui()
	{
		std::scoped_lock guard(m_module_lock);

		for (const auto& module : m_modules)
		{
			for (const auto& element : module->m_data.m_always_draw_independent_gui)
			{
				element->draw();
			}
		}
	}

	void lua_manager::draw_independent_gui()
	{
		std::scoped_lock guard(m_module_lock);

		for (const auto& module : m_modules)
		{
			for (const auto& element : module->m_data.m_independent_gui)
			{
				element->draw();
			}
		}
	}

	void lua_manager::unload_module(const std::string& module_guid)
	{
		std::scoped_lock guard(m_module_lock);

		std::erase_if(m_modules,
		              [&](auto& module)
		              {
			              return module_guid == module->guid();
		              });
	}

	load_module_result lua_manager::load_module(const module_info& module_info, bool ignore_failed_to_load)
	{
		if (!std::filesystem::exists(module_info.m_path))
		{
			return load_module_result::FILE_MISSING;
		}

		std::scoped_lock guard(m_module_lock);
		for (const auto& module : m_modules)
		{
			if (module->guid() == module_info.m_guid)
			{
				LOG(WARNING) << "Module with the guid " << module_info.m_guid << " already loaded.";
				return load_module_result::ALREADY_LOADED;
			}
		}

		const auto module_index = m_modules.size();
		m_modules.push_back(std::make_unique<lua_module>(module_info, m_state));

		const auto load_result = m_modules[module_index]->load_and_call_plugin(m_state);
		if (load_result == load_module_result::SUCCESS || (load_result == load_module_result::FAILED_TO_LOAD && ignore_failed_to_load))
		{
			if (m_is_all_mods_loaded)
			{
				for (const auto& cb : m_modules[module_index]->m_data.m_on_all_mods_loaded_callbacks)
				{
					cb();
				}
			}
		}
		else
		{
			m_modules.pop_back();
		}

		return load_result;
	}

	bool lua_manager::module_exists(const std::string& module_guid)
	{
		std::scoped_lock guard(m_module_lock);

		for (const auto& module : m_modules)
		{
			if (module->guid() == module_guid)
			{
				return true;
			}
		}

		return false;
	}

	static bool topological_sort_visit(const std::string& node, std::stack<std::string>& stack, std::vector<std::string>& sorted_list, const std::function<std::vector<std::string>(const std::string&)>& dependency_selector, std::unordered_set<std::string>& visited, std::unordered_set<std::string>& sorted)
	{
		if (visited.contains(node))
		{
			if (!sorted.contains(node))
			{
				return false;
			}
		}
		else
		{
			visited.insert(node);
			stack.push(node);
			for (const auto& dep : dependency_selector(node))
			{
				if (!topological_sort_visit(dep, stack, sorted_list, dependency_selector, visited, sorted))
				{
					return false;
				}
			}

			sorted.insert(node);
			sorted_list.push_back(node);

			stack.pop();
		}

		return true;
	}

	static std::vector<std::string> topological_sort(std::vector<std::string>& nodes, const std::function<std::vector<std::string>(const std::string&)>& dependency_selector)
	{
		std::vector<std::string> sorted_list;

		std::unordered_set<std::string> visited;
		std::unordered_set<std::string> sorted;

		for (const auto& input : nodes)
		{
			std::stack<std::string> current_stack;
			if (!topological_sort_visit(input, current_stack, sorted_list, dependency_selector, visited, sorted))
			{
				LOG(FATAL) << "Cyclic Dependency: " << input;
				while (!current_stack.empty())
				{
					LOG(FATAL) << current_stack.top();
					current_stack.pop();
				}
			}
		}

		return sorted_list;
	}

	void lua_manager::load_fallback_module()
	{
		try
		{
			auto tmp_path  = std::filesystem::temp_directory_path();
			tmp_path      /= "h2m_fallback_module.lua";
			std::ofstream ofs(tmp_path);
			ofs << "#\n";
			ofs.close();

			const module_info mod_info = {
			    .m_lua_file_entries_hash = "",
			    .m_path                  = tmp_path,
			    .m_folder_path           = m_plugins_folder.get_path(),
			    .m_guid                  = "Hell2Modding-GLOBAL",
			    .m_guid_with_version     = "Hell2Modding-GLOBAL-1.0.0",
			    .m_manifest = {.name = "GLOBAL", .version_number = "1.0.0", .version = semver::version(1, 0, 0), .website_url = "", .description = "Fallback module"},
			};
			const auto load_result = load_module(mod_info);
		}
		catch (const std::exception& e)
		{
			LOG(FATAL) << e.what();
		}
		catch (...)
		{
			LOG(FATAL) << "Unknown exception while trying to create fallback module";
		}
	}

	lua_module* lua_manager::get_fallback_module()
	{
		return m_modules[0].get();
	}

	void lua_manager::load_all_modules()
	{
		// Map for lexicographical ordering.
		std::map<std::string, module_info> module_guid_to_module_info{};

		// Get all the modules from the folder.
		for (const auto& entry : std::filesystem::recursive_directory_iterator(m_plugins_folder.get_path(), std::filesystem::directory_options::skip_permission_denied))
		{
			if (entry.is_regular_file() && entry.path().filename() == "main.lua")
			{
				const auto module_info = get_module_info(entry.path());
				if (module_info)
				{
					const auto& guid = module_info.value().m_guid;

					if (module_guid_to_module_info.contains(guid))
					{
						if (module_info.value().m_manifest.version > module_guid_to_module_info[guid].m_manifest.version)
						{
							LOG(INFO) << "Found a more recent version of " << guid << " ("
							          << module_info.value().m_manifest.version << " > "
							          << module_guid_to_module_info[guid].m_manifest.version << "): Using that instead.";

							module_guid_to_module_info[guid] = module_info.value();
						}
					}
					else
					{
						module_guid_to_module_info.insert({guid, module_info.value()});
					}
				}
			}
		}

		// Get all the guids to prepare for sorting depending on their dependencies.
		std::vector<std::string> module_guids;
		for (const auto& [guid, info] : module_guid_to_module_info)
		{
			module_guids.push_back(guid);
		}

		// Sort depending on module dependencies.
		const auto sorted_modules = topological_sort(module_guids,
		                                             [&](const std::string& guid)
		                                             {
			                                             if (module_guid_to_module_info.contains(guid))
			                                             {
				                                             return module_guid_to_module_info[guid].m_manifest.dependencies_no_version_number;
			                                             }
			                                             return std::vector<std::string>();
		                                             });

		/*
		for (const auto& guid : sorted_modules)
		{
			LOG(VERBOSE) << guid;
		}
		*/

		std::unordered_set<std::string> missing_modules;
		for (const auto& guid : sorted_modules)
		{
			constexpr auto mod_loader_name = "Hell2Modding-Hell2Modding";

			bool not_missing_dependency = true;
			for (const auto& dependency : module_guid_to_module_info[guid].m_manifest.dependencies_no_version_number)
			{
				// The mod loader is not a lua module,
				// but might be put as a dependency in the mod manifest,
				// don't mark the mod as unloadable because of that.
				if (dependency.contains(mod_loader_name))
				{
					continue;
				}

				if (missing_modules.contains(dependency))
				{
					LOG(WARNING) << "Can't load " << guid << " because it's missing " << dependency;
					not_missing_dependency = false;
				}
			}

			if (not_missing_dependency)
			{
				const auto& module_info = module_guid_to_module_info[guid];
				const auto load_result  = load_module(module_info);
				if (load_result == load_module_result::FILE_MISSING)
				{
					// Don't log the fact that the mod loader failed to load, it's normal (see comment above)
					if (!guid.contains(mod_loader_name))
					{
						LOG(WARNING) << guid
						             << " (file path: " << reinterpret_cast<const char*>(module_info.m_path.u8string().c_str()) << " does not exist in the filesystem. Not loading it.";
					}

					missing_modules.insert(guid);
				}
			}
		}

		std::scoped_lock guard(m_module_lock);
		for (const auto& module : m_modules)
		{
			for (const auto& cb : module->m_data.m_on_all_mods_loaded_callbacks)
			{
				cb();
			}
		}

		m_is_all_mods_loaded = true;
	}

	void lua_manager::unload_all_modules()
	{
		std::scoped_lock guard(m_module_lock);

		m_modules.clear();
	}

	static auto g_lua_file_watcher_last_time = std::chrono::high_resolution_clock::now();

	void lua_manager::update_file_watch_reload_modules()
	{
		std::scoped_lock guard(m_module_lock);

		const auto time_now = std::chrono::high_resolution_clock::now();
		if ((time_now - g_lua_file_watcher_last_time) > 500ms)
		{
			g_lua_file_watcher_last_time = time_now;

			std::unordered_set<std::string> already_reloaded_this_frame;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(m_plugins_folder.get_path(), std::filesystem::directory_options::skip_permission_denied))
			{
				if (entry.path().extension() == ".lua")
				{
					const auto module_info = get_module_info(entry.path());
					if (module_info)
					{
						if (already_reloaded_this_frame.contains(module_info.value().m_guid))
						{
							continue;
						}

						for (const auto& already_loaded_module : m_modules)
						{
							if (already_loaded_module->guid() == module_info.value().m_guid)
							{
								if (already_loaded_module->update_lua_file_entries(module_info.value().m_lua_file_entries_hash))
								{
									already_loaded_module->cleanup();
									already_loaded_module->load_and_call_plugin(m_state);
									already_reloaded_this_frame.insert(module_info.value().m_guid);
									break;
								}
							}
						}
					}
				}
			}
		}
	}
} // namespace big
