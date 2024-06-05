#include "data.hpp"

#include "hades_ida.hpp"

#include <hooks/hooking.hpp>
#include <lua/lua_manager.hpp>
#include <lua_extensions/lua_module_ext.hpp>
#include <memory/gm_address.hpp>
#include <string/string.hpp>

namespace lua::hades::data
{
	static std::recursive_mutex g_FileStream_to_filename_mutex;
	static std::unordered_map<void*, std::filesystem::path> g_FileStream_to_filename;

	static void* original_GetFileSize = nullptr;
	static void* current_file_stream  = nullptr;

	static size_t hook_FileStreamGetFileSize(uintptr_t pFile)
	{
		std::scoped_lock l(g_FileStream_to_filename_mutex);

		auto size = *(size_t*)(pFile + 0x20);

		// Used for allocating the output buffer and the Read call.
		auto it = g_FileStream_to_filename.find((void*)pFile);
		if (it != g_FileStream_to_filename.end() && it->second.extension() == ".sjson")
		{
			size *= 2;
		}

		return size;
	}

	static void hook_fsAppendPathComponent(const char* basePath, const char* pathComponent, char* output /*size: 512*/)
	{
		big::g_hooking->get_original<hook_fsAppendPathComponent>()(basePath, pathComponent, output);

		if (current_file_stream && output)
		{
			std::filesystem::path output_ = output;
			if (output_.is_absolute() && std::filesystem::exists(output_))
			{
				std::scoped_lock l(g_FileStream_to_filename_mutex);

				g_FileStream_to_filename[current_file_stream] = output_;
			}
		}
	}

	static bool hook_FileStreamOpen(int64_t resourceDir, const char* fileName, int64_t mode, void* file_stream)
	{
		current_file_stream = file_stream;

		const auto res = big::g_hooking->get_original<hook_FileStreamOpen>()(resourceDir, fileName, mode, file_stream);
		if (res)
		{
			// We need to hook GetFileSize too because the buffer is preallocated through malloc before the Read call happens.
			// This is dirty as hell but we'll just double the size as I don't think
			// it's possible to know in advance what our patches to the game files will do to the file size.
			{
				void** FileStream_vtable = *(void***)file_stream;
				if (original_GetFileSize == nullptr)
				{
					original_GetFileSize = FileStream_vtable[6];
				}
				FileStream_vtable[6] = hook_FileStreamGetFileSize;
			}
		}

		current_file_stream = nullptr;

		return res;
	}

	static bool check_path(const std::filesystem::path& firstPath, const std::filesystem::path& secondPath)
	{
		std::string full_file_path = (char*)firstPath.u8string().c_str();
		std::string user_path      = (char*)secondPath.u8string().c_str();

		full_file_path = big::string::replace(full_file_path, "\\", "/");
		user_path      = big::string::replace(user_path, "\\", "/");

		return full_file_path.contains(user_path);
	}

	static size_t hook_FileStreamRead(void* file_stream, void* outputBuffer, size_t bufferSizeInBytes)
	{
		std::unordered_map<void*, std::filesystem::path>::iterator it;

		bool is_game_data = false;
		if (bufferSizeInBytes > 4)
		{
			std::scoped_lock l(g_FileStream_to_filename_mutex);

			it = g_FileStream_to_filename.find(file_stream);
			if (it != g_FileStream_to_filename.end() && it->second.extension() == ".sjson")
			{
				// The actual size of the buffer in this case is half.
				bufferSizeInBytes /= 2;
				is_game_data       = true;
			}
		}

		auto size_read = big::g_hooking->get_original<hook_FileStreamRead>()(file_stream, outputBuffer, bufferSizeInBytes);

		if (is_game_data)
		{
			bool any_modif_happened = false;
			std::string new_string;
			bool assigned_new_string = false;

			std::scoped_lock l(big::g_lua_manager->m_module_lock);
			for (const auto& mod_ : big::g_lua_manager->m_modules)
			{
				auto mod = (big::lua_module_ext*)mod_.get();
				for (const auto& info : mod->m_data_ext.m_on_sjson_game_data_read)
				{
					if (info.m_file_path.empty() || (info.m_file_path.size() && check_path(it->second, info.m_file_path)))
					{
						if (info.m_is_string_read)
						{
							if (!assigned_new_string)
							{
								new_string.assign((const char*)outputBuffer, bufferSizeInBytes);
								assigned_new_string = true;
							}

							const auto res = info.m_callback((char*)it->second.u8string().c_str(), new_string.data());
							if (res.valid() && res.get_type() == sol::type::string)
							{
								new_string = res.get<std::string>();

								//LOG(INFO) << (char*)it->second.u8string().c_str() << " | " << new_string.size() << " | orig: " << bufferSizeInBytes << " | " << new_string;

								if (bufferSizeInBytes * 2 < new_string.size())
								{
									std::stringstream ss;
									ss << "Your patches won't work correctly because of my bad coding, please make an "
									      "issue on the Hell2Modding repo, make sure to pass this info: Original file "
									      "size "
									   << bufferSizeInBytes << " | Doubled size: " << bufferSizeInBytes * 2
									   << " | Needed size for patch: " << new_string.size()
									   << " | filename: " << (char*)it->second.u8string().c_str();
									MessageBoxA(0, ss.str().c_str(), "Hell2Modding", 0);
								}

								any_modif_happened = true;
							}
						}
					}
				}
			}

			if (any_modif_happened)
			{
				memcpy(outputBuffer, new_string.data(), new_string.size());
				size_read = new_string.size();
			}

			if (original_GetFileSize != nullptr)
			{
				void** FileStream_vtable = *(void***)file_stream;
				FileStream_vtable[6]     = original_GetFileSize;
			}
		}

		return size_read;
	}

	// Lua API: Function
	// Table: data
	// Name: on_sjson_read_as_string
	// Param: function: function: Function called when game data file is read. The function must match signature: (string (file_path_being_read), string (file_content_buffer)) -> returns nothing (nil) or the new file buffer (string)
	// Param: file_path_being_read: string: optional. Use only if you want your lua function to be called for a given file_path.
	static void on_sjson_read_as_string_no_path_filter(sol::protected_function func, sol::this_environment env)
	{
		auto mod = (big::lua_module_ext*)big::lua_module::this_from(env);
		if (mod)
		{
			mod->m_data_ext.m_on_sjson_game_data_read.emplace_back("", true, func);
		}
	}

	static void on_sjson_read_as_string_with_path_filter(sol::protected_function func, const std::string& file_path_being_read, sol::this_environment env)
	{
		auto mod = (big::lua_module_ext*)big::lua_module::this_from(env);
		if (mod)
		{
			mod->m_data_ext.m_on_sjson_game_data_read.emplace_back(file_path_being_read, true, func);
		}
	}

	// Lua API: Function
	// Table: data
	// Name: reload_game_data
	static void reload_game_data()
	{
		static auto read_game_data_ptr = gmAddress::scan("7D 70 4C 8D 05", "ReadGameData");
		if (read_game_data_ptr)
		{
			static auto f = read_game_data_ptr.offset(-0x7B).as_func<void()>();
			f();
		}
	}

	// Lua API: Function
	// Table: data
	// Name: get_string_from_hash_guid
	// Param: hash_guid: integer: Hash value.
	// Returns: string: Returns the string corresponding to the provided hash value.
	static const char* get_string_from_hash_guid(unsigned int hash_guid)
	{
		static auto gStringBuffer =
		    *gmAddress::scan("4C 03 0D ? ? ? ? 48 8B DA 0F B6 54 24", "gStringBuffer").offset(3).rip().as<const char**>();

		return &gStringBuffer[hash_guid];
	}

	void bind(sol::state_view& state, sol::table& lua_ext)
	{
		{
			static auto hook_open = big::hooking::detour_hook_helper::add<hook_FileStreamOpen>(
			    "hook_FileStreamOpen",
			    gmAddress::scan("44 8B C9 33 D2", "FileStreamOpen").offset(-0x97));
		}
		{
			static auto hook_read = big::hooking::detour_hook_helper::add<hook_FileStreamRead>(
			    "hook_FileStreamRead",
			    gmAddress::scan("48 3B C3 74 42", "FileStreamRead").offset(-0x2D));
		}

		{
			static auto fsAppendPathComponent_ptr = gmAddress::scan("C6 44 24 30 5C", "fsAppendPathComponent");
			if (fsAppendPathComponent_ptr)
			{
				static auto fsAppendPathComponent = fsAppendPathComponent_ptr.offset(-0x97).as_func<void(const char*, const char*, char*)>();
				static auto hook_fsAppendPathComponent_ = big::hooking::detour_hook_helper::add<hook_fsAppendPathComponent>("hook_fsAppendPathComponent", fsAppendPathComponent);
			}
		}

		auto ns = lua_ext.create_named("data");
		ns.set_function("on_sjson_read_as_string", sol::overload(on_sjson_read_as_string_no_path_filter, on_sjson_read_as_string_with_path_filter));
		ns.set_function("reload_game_data", reload_game_data);
		ns.set_function("get_string_from_hash_guid", get_string_from_hash_guid);

		state["sol.__h2m_LoadPackages__"] = state["LoadPackages"];
		// Lua API: Function
		// Table: game
		// Name: LoadPackages
		// Param: args: table<string, string>: Table contains string key `Name` and its associated `string` value. Associated value should be a full path to the package to load, without the extension. The filename of the .pkg and the .pkg_manifest files should contains the guid of the owning mod. Example `AuthorName-ModName-MyMainPackage`
		// **Example Usage:**
		// ```lua
		// local package_path = rom.path.combine(_PLUGIN.plugins_data_mod_folder_path, _PLUGIN.guid)
		// -- Example package_path: "C:/Program Files (x86)/Steam/steamapps/common/Hades II/Ship/ReturnOfModding/plugins_data/AuthorName-ModName/AuthorName-ModName"
		// rom.game.LoadPackages{Name = package_path}
		// ```
		state["LoadPackages"] = [](sol::table args, sol::this_environment env_, sol::this_state state_)
		{
			for (const auto& [k, v] : args)
			{
				if (k.is<std::string>() && v.is<std::string>())
				{
					const auto key   = k.as<std::string>();
					const auto value = v.as<std::string>();
					if (key == "Name" && value.contains("plugins_data"))
					{
						auto full_path         = std::filesystem::path(value);
						const std::string stem = (char*)full_path.stem().u8string().c_str();

						// This weirdly implemented check is because for some reason env_ is returning the fallback module for some mods and I don't even wanna know why.
						{
							bool is_bad_pkg = true;
							while (true)
							{
								if (full_path == full_path.parent_path())
								{
									break;
								}

								if (full_path.has_parent_path())
								{
									full_path = full_path.parent_path();
								}
								else
								{
									break;
								}

								const std::string full_path_stem = (char*)full_path.stem().u8string().c_str();
								if (stem.contains(full_path_stem) && stem.contains('-'))
								{
									for (const auto& mod_ : big::g_lua_manager->m_modules)
									{
										if (stem.contains(mod_->guid()))
										{
											is_bad_pkg = false;
											break;
										}
									}
									if (!is_bad_pkg)
									{
										break;
									}
								}
							}

							if (is_bad_pkg)
							{
								const auto error_msg = std::format(
								    "Hell2Modding requires the .pkg file in the plugins_data folder to contain "
								    "the owner mod guid in its filename.\nIt is used to guarantee the "
								    "uniqueness of the name and for internal purposes.\nPackage File Path: {}",
								    value);
								MessageBoxA(0, error_msg.c_str(), "Hell2Modding", MB_ICONERROR | MB_OK);
								TerminateProcess(GetCurrentProcess(), 1);
							}
						}
					}
				}
			}

			return sol::state_view(state_)["sol.__h2m_LoadPackages__"](args);
		};
	}
} // namespace lua::hades::data
