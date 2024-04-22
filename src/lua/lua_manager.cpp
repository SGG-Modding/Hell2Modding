#include "lua_manager.hpp"

#include "bindings/gui.hpp"
#include "bindings/imgui.hpp"
#include "bindings/log.hpp"
#include "bindings/memory.hpp"
#include "bindings/path.hpp"
#include "bindings/paths.hpp"
#include "bindings/toml/toml_lua.hpp"
#include "file_manager/file_manager.hpp"
#include "string/string.hpp"

#include <thunderstore/v1/manifest.hpp>
#include <unordered_set>

namespace big
{
	static std::unique_ptr<wtr::watch> g_lua_file_watcher = nullptr;

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

		const std::string guid = folder_name;
		return {{
		    .m_path              = module_path,
		    .m_folder_path       = current_folder,
		    .m_guid              = guid,
		    .m_guid_with_version = guid + "-" + manifest.version_number,
		    .m_manifest          = manifest,
		}};
	}

	static std::unordered_map<std::wstring, size_t> modify_reload_count;

	lua_manager::lua_manager(lua_State* game_lua_state, folder config_folder, folder plugins_data_folder, folder plugins_folder) :
	    m_state(game_lua_state),
	    m_config_folder(config_folder),
	    m_plugins_data_folder(plugins_data_folder),
	    m_plugins_folder(plugins_folder)
	{
		g_lua_manager = this;

		init_lua_state();

		load_all_modules();

		lua::window::deserialize();

		if (g_lua_file_watcher == nullptr)
		{
			g_lua_file_watcher =
			    std::make_unique<wtr::watch>(m_plugins_folder.get_path(),
			                                 [](const wtr::event& e)
			                                 {
				                                 if (!g_lua_manager || !g_lua_manager->m_is_all_mods_loaded)
				                                 {
					                                 return;
				                                 }

				                                 if (e.effect_type == wtr::event::effect_type::owner || e.effect_type == wtr::event::effect_type::other)
				                                 {
					                                 return;
				                                 }

				                                 if (e.path_name.extension() != ".lua")
				                                 {
					                                 return;
				                                 }

				                                 // Windows file modify notification get triggered twice on file saves it seems.
				                                 {
					                                 if (e.effect_type == wtr::event::effect_type::modify)
					                                 {
						                                 const auto path_name_wstring = e.path_name.wstring();
						                                 modify_reload_count[path_name_wstring]++;
						                                 if (modify_reload_count[path_name_wstring] % 2 == 0)
						                                 {
							                                 return;
						                                 }
					                                 }
				                                 }

				                                 const std::filesystem::path root_folder = g_file_manager.get_base_dir();
				                                 g_lua_manager->for_each_module(
				                                     [&](const auto& module)
				                                     {
					                                     const std::filesystem::path module_path = module->path();
					                                     const auto module_info = get_module_info(module_path);
					                                     if (module_info)
					                                     {
						                                     const auto& module_folder = (*module_info).m_folder_path;
						                                     std::filesystem::path current_folder = e.path_name;
						                                     while (true)
						                                     {
							                                     if (current_folder == root_folder)
							                                     {
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

							                                     if (current_folder == module_folder)
							                                     {
								                                     module->cleanup();
								                                     module->load_and_call_plugin(g_lua_manager->m_state);
								                                     return;
							                                     }
						                                     }
					                                     }
				                                     });
			                                 });
		}
	}

	lua_manager::~lua_manager()
	{
		lua::window::serialize();

		unload_all_modules();

		g_lua_manager = nullptr;
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

	void lua_manager::set_folder_for_lua_require()
	{
		std::string plugins_search_path = m_plugins_folder.get_path().string() + "/?.lua;";

		for (const auto& entry : std::filesystem::recursive_directory_iterator(m_plugins_folder.get_path(), std::filesystem::directory_options::skip_permission_denied))
		{
			if (!entry.is_directory())
			{
				continue;
			}

			plugins_search_path += entry.path().string() + "/?.lua;";
		}
		// Remove final ';'
		plugins_search_path.pop_back();

		m_state["package"]["path"] = plugins_search_path;
	}

	void lua_manager::sandbox_lua_os_library()
	{
		const auto& os = m_state["os"];
		sol::table sandbox_os(m_state, sol::create);

		sandbox_os["clock"]    = os["clock"];
		sandbox_os["date"]     = os["date"];
		sandbox_os["difftime"] = os["difftime"];
		sandbox_os["time"]     = os["time"];

		m_state["os"] = sandbox_os;
	}

	template<size_t N>
	static constexpr auto not_supported_lua_function(const char (&function_name)[N])
	{
		return [function_name](sol::this_environment env, sol::variadic_args args)
		{
			LOG(FATAL) << big::lua_module::guid_from(env) << " tried calling a currently not supported lua function: " << function_name;
			Logger::FlushQueue();
		};
	}

	void lua_manager::sandbox_lua_loads()
	{
		// That's from lua base lib, luaB
		m_loadfile = m_state["loadfile"];

		// That's from lua package lib.
		// We only allow dependencies between .lua files, no DLLs.
		m_state["package"]["loadlib"] = not_supported_lua_function("package.loadlib");
		m_state["package"]["cpath"]   = "";

		// 1                   2               3            4
		// {searcher_preload, searcher_Lua, searcher_C, searcher_Croot, NULL};
		m_state["package"]["searchers"][3] = not_supported_lua_function("package.searcher C");
		m_state["package"]["searchers"][4] = not_supported_lua_function("package.searcher Croot");

		// Custom require for setting environment on required modules, the setenv is based on
		// which folder (and so ultimately package/mod) contains the required module file.
		// If no match is found with the folder path then we just set the same env as the require caller.
		// TODO: This is hacked together, need to be cleaned up at some point.
		// TODO: sub folders are not supported currently.
		m_state["require"] = [&](std::string path, sol::variadic_args args, sol::this_environment this_env) -> sol::object
		{
			sol::environment& env = this_env;

			// Example of a non local require (mod is requiring a file from another mod/package):
			// require "Hell2Modding-DebugToolkit/lib_debug"
			const auto is_non_local_require = !path.starts_with("./") && path.contains('-') && path.contains('/');
			std::string required_module_guid{};
			if (is_non_local_require)
			{
				LOG(INFO) << "Non Local require: " << path;
				required_module_guid = string::split(path, '/')[0];
				if (std::ranges::count(path, '/') > 1)
				{
					LOG(WARNING) << "Require with sub folders are currently not supported";
				}
			}
			else
			{
				LOG(INFO) << "Local require: " << path;
				if (path.starts_with("./"))
				{
					path.erase(0, 2);
				}
				if (path.contains('/'))
				{
					LOG(WARNING) << "Require with sub folders are currently not supported";
				}
				required_module_guid = lua_module::guid_from(this_env);
			}

			const auto path_stem = std::filesystem::path(path).stem();

			for (const auto& entry : std::filesystem::recursive_directory_iterator(m_plugins_folder.get_path(), std::filesystem::directory_options::skip_permission_denied))
			{
				if (entry.path().extension() == ".lua")
				{
					if (entry.path().stem() == path_stem)
					{
						const auto full_path_ = entry.path().u8string();
						const auto full_path  = (char*)full_path_.c_str();

						if (!strstr(full_path, required_module_guid.c_str()))
						{
							LOG(WARNING) << "Skipping " << full_path << " because the required module guid was not in the path " << required_module_guid;
							continue;
						}

						const auto guid_from_path = get_module_info(full_path);
						if (!guid_from_path)
						{
							LOG(WARNING) << "Couldnt get module info from path " << full_path;
							break;
						}

						static std::unordered_map<std::string, sol::protected_function> required_module_cache;

						if (!required_module_cache.contains(full_path))
						{
							auto fresh_result = m_loadfile(full_path);
							if (!fresh_result.valid() || fresh_result.get_type() == sol::type::nil /*LuaJIT*/)
							{
								const auto error_msg =
								    !fresh_result.valid() ? fresh_result.get<sol::error>().what() : fresh_result.get<const char*>(1) /*LuaJIT*/;

								LOG(FATAL) << "Failed require: " << error_msg;
								Logger::FlushQueue();
								break;
							}
							required_module_cache[full_path] = fresh_result.get<sol::protected_function>();
						}

						auto& result = required_module_cache[full_path];

						bool found_the_other_module = false;
						for (const auto& mod : m_modules)
						{
							if (guid_from_path.value().m_guid == mod->guid())
							{
								env                    = mod->env();
								found_the_other_module = true;

								break;
							}
						}

						if (!found_the_other_module && is_non_local_require)
						{
							LOG(FATAL) << "You require'd a module called " << path << " but did not have a package manifest.json level dependency on it. Which lead to the owning package of that module to not be properly init yet. Expect unstable behaviors related to your dependencies.";
						}

						env.set_on(result);

						//LOG(INFO) << "Calling require on " << full_path;

						const auto res = result(args);

						//LOG(INFO) << "type: " << (int)res.get_type();

						return res;
					}
				}
			}

			return {};
		};

		set_folder_for_lua_require();
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

		init_lua_api();
	}

	void lua_manager::init_lua_api()
	{
		sol::table lua_ext = m_state.create_named_table(lua_ext_namespace);
		sol::table mods    = lua_ext.create_named("mods");
		// Lua API: Function
		// Table: mods
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

		// Let's keep that list sorted the same as the solution file explorer
		lua::toml_lua::bind(lua_ext);
		lua::gui::bind(lua_ext);
		lua::imgui::bind(lua_ext);
		lua::log::bind(m_state, lua_ext);
		lua::memory::bind(lua_ext);
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
		std::lock_guard guard(m_module_lock);

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
		std::lock_guard guard(m_module_lock);

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
		std::lock_guard guard(m_module_lock);

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
		std::lock_guard guard(m_module_lock);

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

		std::lock_guard guard(m_module_lock);
		for (const auto& module : m_modules)
		{
			if (module->guid() == module_info.m_guid)
			{
				LOG(WARNING) << "Module with the guid " << module_info.m_guid << " already loaded.";
				return load_module_result::ALREADY_LOADED;
			}
		}

		const auto module      = std::make_shared<lua_module>(module_info, m_state);
		const auto load_result = module->load_and_call_plugin(m_state);
		if (load_result == load_module_result::SUCCESS || (load_result == load_module_result::FAILED_TO_LOAD && ignore_failed_to_load))
		{
			m_modules.push_back(module);

			if (m_is_all_mods_loaded)
			{
				for (const auto& cb : module->m_data.m_on_all_mods_loaded_callbacks)
				{
					cb();
				}
			}
		}

		return load_result;
	}

	bool lua_manager::module_exists(const std::string& module_guid)
	{
		std::lock_guard guard(m_module_lock);

		for (const auto& module : m_modules)
		{
			if (module->guid() == module_guid)
			{
				return true;
			}
		}

		return false;
	}

	std::weak_ptr<lua_module> lua_manager::get_module(const std::string& module_guid)
	{
		std::lock_guard guard(m_module_lock);

		for (const auto& module : m_modules)
		{
			if (module->guid() == module_guid)
			{
				return module;
			}
		}

		return {};
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

		std::lock_guard guard(m_module_lock);
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
		std::lock_guard guard(m_module_lock);

		for (auto& module : m_modules)
		{
			module.reset();
		}

		m_modules.clear();
	}
} // namespace big
