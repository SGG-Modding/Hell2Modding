#include "data.hpp"

#include "hades_ida.hpp"

#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <lua/lua_manager.hpp>
#include <lua_extensions/lua_manager_extension.hpp>
#include <lua_extensions/lua_module_ext.hpp>
#include <memory/gm_address.hpp>
#include <string/string.hpp>

namespace sgg
{
	enum class PackageGroup : __int8
	{
		Weapons = 0x0,
		Level   = 0x1,
		Base    = 0x2,
	};
}

namespace lua::hades::data
{
	static std::recursive_mutex g_load_packages_overrides_mutex;

	using HashGuidIdType = decltype(sgg::HashGuid::mId);

	static ankerl::unordered_dense::map<HashGuidIdType, std::vector<HashGuidIdType>> g_load_packages_overrides;

	static constexpr size_t g_size_multiplier_for_sjson_patches = 8;

	// Lua API: Function
	// Table: data
	// Name: get_string_from_hash_guid
	// Param: hash_guid: integer: Hash value.
	// Returns: string: Returns the string corresponding to the provided hash value.
	static const char* get_string_from_hash_guid(lua_Number hash_guid)
	{
		static auto gStringBuffer = *big::hades2_symbol_to_address["sgg::HashGuid::gStringBuffer"].as<const char**>();

		return &gStringBuffer[(HashGuidIdType)hash_guid];
	}

	// Lua API: Function
	// Table: data
	// Name: get_hash_guid_from_string
	// Param: str: string: String value.
	// Returns: number: Returns the hash guid corresponding to the provided string value.
	static HashGuidIdType get_hash_guid_from_string(std::string str)
	{
		static auto Lookup = *big::hades2_symbol_to_address["sgg::HashGuid::Lookup"].as_func<sgg::HashGuid*(sgg::HashGuid*, const char*, size_t)>();
		sgg::HashGuid res{};
		Lookup(&res, str.c_str(), str.size());

		return res.mId;
	}

	static void hook_LoadPackage(void* this_, sgg::HashGuid packageName, sgg::PackageGroup group)
	{
		std::scoped_lock l(g_load_packages_overrides_mutex);

		// Example logs: 
		// NikkelM-ColouredBiomeMap
		// MainMenu
		// Launch
		// GUI
		//LOG(ERROR) << get_string_from_hash_guid(packageName.mId);

		auto it = g_load_packages_overrides.find(packageName.mId);
		if (it != g_load_packages_overrides.end())
		{
			for (const auto p : it->second)
			{
				big::g_hooking->get_original<hook_LoadPackage>()(this_, sgg::HashGuid{.mId = p}, group);
			}
		}
		else
		{
			big::g_hooking->get_original<hook_LoadPackage>()(this_, packageName, group);
		}
	}

	// Lua API: Function
	// Table: data
	// Name: load_package_overrides_get
	// Param: hash_guid: number: The HashGuid to look up.
	// Returns: table<number>: A table of HashGuid values that replace the input.
	// Returns the override list for the given HashGuid. If LoadPackage(hash_guid)
	// is called, each HashGuid in this list will be loaded instead.
	sol::table load_package_overrides_get(lua_Number hash_guid)
	{
		std::scoped_lock l(g_load_packages_overrides_mutex);

		auto res = sol::table(big::g_lua_manager->lua_state(), sol::create);

		for (const auto id : g_load_packages_overrides[(HashGuidIdType)hash_guid])
		{
			res.add((lua_Number)id);
		}

		return res;
	}

	// Lua API: Function
	// Table: data
	// Name: load_package_overrides_set
	// Param: hash_guid: number: The original HashGuid that will be replaced.
	// Param: hash_guid_table_override: table<number>: List of HashGuid values that should be used instead.
	// Defines an override list for a given HashGuid. When LoadPackage(hash_guid)
	// is called, it will instead load each HashGuid listed in the override table.
	// 	
	// **Example Usage:**
	// ```lua
	// local gui_hash = rom.data.get_hash_guid_from_string("GUI")
	// local some_custom_hash = rom.data.get_hash_guid_from_string("NikkelM-ColouredBiomeMap")
	// rom.data.load_package_overrides_set(gui_hash, {gui_hash, some_custom_hash})
	// ```
	void load_package_overrides_set(lua_Number hash_guid, sol::table overrides)
	{
		std::scoped_lock l(g_load_packages_overrides_mutex);

		std::stringstream ss;

		ss << "Overrides for " << (HashGuidIdType)hash_guid << "(" << get_string_from_hash_guid(hash_guid) << ")"
		   << ": ";

		bool first = true;

		for (const auto& [k, v] : overrides)
		{
			if (v.is<lua_Number>())
			{
				const auto id = (HashGuidIdType)v.as<lua_Number>();
				g_load_packages_overrides[hash_guid].push_back(id);

				if (!first)
				{
					ss << ", ";
				}
				first = false;

				ss << id;
			}
		}

		LOG(INFO) << ss.str();
	}

	static std::recursive_mutex g_FileStream_to_filename_mutex;
	static std::unordered_map<void*, std::filesystem::path> g_FileStream_to_filename;

	static void* g_original_GetFileSize = nullptr;
	static void* g_current_file_stream  = nullptr;

	static size_t hook_FileStreamGetFileSize(uintptr_t pFile)
	{
		std::scoped_lock l2(g_FileStream_to_filename_mutex);

		// that offset can be asserted inside PlatformOpenFile at the bottom
		auto size = *(size_t*)(pFile + 0x20);

		// Used for allocating the output buffer and the Read call.
		auto it = g_FileStream_to_filename.find((void*)pFile);
		if (it != g_FileStream_to_filename.end() && it->second.extension() == ".sjson")
		{
			size *= g_size_multiplier_for_sjson_patches;
		}

		return size;
	}

	static void hook_fsAppendPathComponent(const char* basePath, const char* pathComponent, char* output /*size: 512*/)
	{
		big::g_hooking->get_original<hook_fsAppendPathComponent>()(basePath, pathComponent, output);

		if (g_current_file_stream && output)
		{
			std::filesystem::path output_ = output;
			if (output_.is_absolute() && std::filesystem::exists(output_))
			{
				std::scoped_lock l(g_FileStream_to_filename_mutex);

				g_FileStream_to_filename[g_current_file_stream] = output_;
			}
		}
	}

	static constexpr size_t g_GetFileSize_index = 7;

	static bool hook_PlatformOpenFile(int64_t resourceDir, const char* fileName, int64_t mode, void* file_stream)
	{
		g_current_file_stream = file_stream;

		const auto res = big::g_hooking->get_original<hook_PlatformOpenFile>()(resourceDir, fileName, mode, file_stream);
		if (res)
		{
			// We need to hook IFileSystem::GetFileSize too because the buffer is preallocated through malloc before the Read call happens.
			// This is dirty as hell but we'll just multiply the size with g_size_multiplier_for_sjson_patches as I don't think
			// it's possible to know in advance what our patches to the game files will do to the file size.
			// TODO: There seems to be some threading issues as users are reporting the message box appearing randomly
			{
				// Index can be found in Local Types
				// struct IFileSystem
				// XREF: .data:gSystemFileIO
				void** FileStream_vtable = *(void***)file_stream;
				if (g_original_GetFileSize == nullptr)
				{
					g_original_GetFileSize = FileStream_vtable[g_GetFileSize_index];
				}
				FileStream_vtable[g_GetFileSize_index] = hook_FileStreamGetFileSize;
			}
		}

		g_current_file_stream = nullptr;

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
				bufferSizeInBytes /= g_size_multiplier_for_sjson_patches;
				is_game_data       = true;
			}
		}

		auto size_read = big::g_hooking->get_original<hook_FileStreamRead>()(file_stream, outputBuffer, bufferSizeInBytes);

		if (is_game_data)
		{
			bool any_modif_happened = false;
			std::string new_string;
			bool assigned_new_string = false;

			std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);
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

								if (bufferSizeInBytes * g_size_multiplier_for_sjson_patches < new_string.size())
								{
									std::stringstream ss;
									ss << "SJSON mod patches won't work correctly because of my bad coding, please "
									      "make an "
									      "issue on the Hell2Modding repo, make sure to pass this info: Original file "
									      "size "
									   << bufferSizeInBytes << " | New size: " << bufferSizeInBytes * g_size_multiplier_for_sjson_patches
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

			if (g_original_GetFileSize != nullptr)
			{
				void** FileStream_vtable             = *(void***)file_stream;
				FileStream_vtable[g_GetFileSize_index] = g_original_GetFileSize;
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
		static auto read_game_data_ptr = big::hades2_symbol_to_address["sgg::GameDataManager::ReadGameData"];
		if (read_game_data_ptr)
		{
			static auto f = read_game_data_ptr.as_func<void()>();
			f();
		}
	}

	static bool contains_byte_sequence(std::ifstream& file, std::span<const char> sequence)
	{
		if (!file.is_open())
		{
			return false;
		}

		// should be at std::ios:ate at this point.
		const auto file_size = file.tellg();
		file.seekg(0, std::ios::beg);
		std::vector<uint8_t> buffer(file_size);
		file.read((char*)buffer.data(), file_size);

		if (sequence.size() == 0)
		{
			// Empty sequence should not be found
			return false;
		}

		// Search for the sequence in the buffer
		const auto it = std::search(buffer.begin(), buffer.end(), sequence.begin(), sequence.end());
		if (it != buffer.end())
		{
			// Calculate the position in the file
			const auto position = std::distance(buffer.begin(), it);
			LOG(DEBUG) << "Sequence found at position: " << position;
			return true;
		}

		return false;
	}

	void bind(sol::state_view& state, sol::table& lua_ext)
	{
		{
			static auto hook_open =
			    big::hooking::detour_hook_helper::add<hook_PlatformOpenFile>("hook_PlatformOpenFile", big::hades2_symbol_to_address["PlatformOpenFile"]);
		}
		{
			static auto hook_read = big::hooking::detour_hook_helper::add<hook_FileStreamRead>("hook_FileStreamRead", big::hades2_symbol_to_address["FileStreamRead"]);
		}

		{
			//static auto LoadPackage = big::hades2_symbol_to_address["sgg::GameAssetManager::LoadPackage"].as_func<void(void*, sgg::HashGuid, sgg::PackageGroup)>();

			static auto hook_ = big::hooking::detour_hook_helper::add<hook_LoadPackage>("hook_LoadPackage", big::hades2_symbol_to_address["sgg::GameAssetManager::LoadPackage"]);
		}

		{
			static auto fsAppendPathComponent_ptr = big::hades2_symbol_to_address["fsAppendPathComponent"];
			if (fsAppendPathComponent_ptr)
			{
				static auto fsAppendPathComponent = fsAppendPathComponent_ptr.as_func<void(const char*, const char*, char*)>();
				static auto hook_fsAppendPathComponent_ = big::hooking::detour_hook_helper::add<hook_fsAppendPathComponent>("hook_fsAppendPathComponent", fsAppendPathComponent);
			}
			else
			{
				LOG(ERROR) << "fsAppendPathComponent hook failure";
			}
		}

		auto ns = lua_ext.create_named("data");
		ns.set_function("on_sjson_read_as_string", sol::overload(on_sjson_read_as_string_no_path_filter, on_sjson_read_as_string_with_path_filter));
		ns.set_function("reload_game_data", reload_game_data);

		ns.set_function("get_string_from_hash_guid", get_string_from_hash_guid);
		ns.set_function("get_hash_guid_from_string", get_hash_guid_from_string);

		ns.set_function("load_package_overrides_get", load_package_overrides_get);
		ns.set_function("load_package_overrides_set", load_package_overrides_set);

		state["sol.__h2m_LoadPackages__"] = state["LoadPackages"];
		// Lua API: Function
		// Table: game
		// Name: LoadPackages
		// Param: args: table<string, string>: Table contains string key `Name` and its associated `string` value. Associated value should be a full path to the package to load, without the extension. The filename of the .pkg and the .pkg_manifest files should contains the guid of the owning mod. Example `AuthorName-ModName-MyMainPackage`
		// **Example Usage:**
		// ```lua
		// local package_path = rom.path.combine(_PLUGIN.plugins_data_mod_folder_path, _PLUGIN.guid)
		// -- Example package_path: "C:/Program Files (x86)/Steam/steamapps/common/Hades II/Ship/ReturnOfModding/plugins_data/AuthorName-ModName/AuthorName-ModName"
		// game.LoadPackages{Name = package_path}
		// ```
		state["LoadPackages"] = [](sol::table args, sol::this_environment env_, sol::this_state state_)
		{
			std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);

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

						full_path = std::filesystem::path(value);

						// Check if pkg manifest contains the guid anywhere inside it.
						// Not a good or clean check. Ideally use something like ReadCSString and parse the file properly.
						bool is_bad_pkg              = true;
						auto pkg_manifest_file_path  = full_path;
						pkg_manifest_file_path      += ".pkg_manifest";
						std::ifstream file(pkg_manifest_file_path, std::ios::binary | std::ios::ate);
						if (file.is_open())
						{
							if (contains_byte_sequence(file, std::span(stem.data(), stem.size())))
							{
								LOG(DEBUG) << "For file " << (const char*)pkg_manifest_file_path.u8string().c_str() << " | " << stem;
								is_bad_pkg = false;
							}
							if (is_bad_pkg)
							{
								const auto error_msg = std::format(
								    "Hell2Modding requires the .pkg_manifest file in the plugins_data folder to "
								    "contain "
								    "the owner mod guid in its assets paths.\nPackage File Path: {}\nowner mod guid "
								    "needed: {}",
								    (const char*)pkg_manifest_file_path.u8string().c_str(),
								    stem);
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
