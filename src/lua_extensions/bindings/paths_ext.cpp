#pragma once
#include "paths_ext.hpp"

#include "lua/lua_manager.hpp"
#include "string/string.hpp"

#include <array>
#include <cstring>

namespace lua::paths_ext
{
	namespace
	{
		constexpr std::array<uint8_t, 8> gaming_root_header = {0x52, 0x47, 0x42, 0x58, 0x01, 0x00, 0x00, 0x00};
		constexpr std::string_view microsoft_store_identity = R"(Name="SupergiantGamesLLC.HadesII")";

		std::filesystem::path get_module_folder(HMODULE module)
		{
			constexpr size_t max_path = MAX_PATH * 2;
			wchar_t buffer[max_path];
			const DWORD length = GetModuleFileNameW(module, buffer, max_path);
			if (length == 0 || length == max_path)
			{
				return {};
			}

			std::filesystem::path module_path(buffer);
			return module_path.parent_path();
		}

		std::optional<std::filesystem::path> read_gaming_library(const std::filesystem::path& drive)
		{
			std::ifstream stream(drive / ".GamingRoot", std::ios::binary);
			if (!stream.is_open())
			{
				return std::nullopt;
			}

			const std::vector<uint8_t> data{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
			if (data.size() <= gaming_root_header.size()
			    || std::memcmp(data.data(), gaming_root_header.data(), gaming_root_header.size()) != 0
			    || (data.size() - gaming_root_header.size()) % sizeof(wchar_t) != 0)
			{
				return std::nullopt;
			}

			std::wstring folder_name((data.size() - gaming_root_header.size()) / sizeof(wchar_t), L'\0');
			std::memcpy(folder_name.data(), data.data() + gaming_root_header.size(), data.size() - gaming_root_header.size());
			if (const auto terminator = folder_name.find(L'\0'); terminator != std::wstring::npos)
			{
				folder_name.resize(terminator);
			}
			if (folder_name.empty())
			{
				return std::nullopt;
			}

			return drive / folder_name;
		}

		bool is_microsoft_store_install(const std::filesystem::path& install_folder)
		{
			std::ifstream config(install_folder / "MicrosoftGame.Config", std::ios::binary);
			if (!config.is_open())
			{
				return false;
			}

			const std::string contents{std::istreambuf_iterator<char>(config), std::istreambuf_iterator<char>()};
			return contents.find(microsoft_store_identity) != std::string::npos;
		}

		std::filesystem::path find_microsoft_store_install()
		{
			std::array<wchar_t, 512> drives{};
			const DWORD length = GetLogicalDriveStringsW(static_cast<DWORD>(drives.size()), drives.data());
			if (length == 0 || length >= drives.size())
			{
				return {};
			}

			for (const wchar_t* drive = drives.data(); *drive != L'\0'; drive += wcslen(drive) + 1)
			{
				const auto gaming_library = read_gaming_library(drive);
				if (!gaming_library)
				{
					continue;
				}

				std::error_code ec;
				for (std::filesystem::directory_iterator it(*gaming_library, std::filesystem::directory_options::skip_permission_denied, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
				{
					if (!it->is_directory(ec))
					{
						continue;
					}

					const auto install_folder = it->path() / "Content";
					if (is_microsoft_store_install(install_folder))
					{
						return install_folder;
					}
				}
			}

			return {};
		}
	} // namespace

	std::filesystem::path get_game_executable_folder()
	{
		constexpr size_t max_path = MAX_PATH * 2;
		wchar_t buffer[max_path];
		DWORD length = GetModuleFileNameW(nullptr, buffer, max_path);
		if (length == 0 || length == max_path)
		{
			return {};
		}

		std::filesystem::path exe_path(buffer);
		return exe_path.parent_path();
	}

	std::filesystem::path get_game_install_folder()
	{
		static const std::filesystem::path install_folder = []
		{
			const auto module_folder            = get_module_folder(big::g_hmodule);
			const auto normalized_module_folder = big::string::to_lower(module_folder.string());
			if (!module_folder.empty() && normalized_module_folder.find("\\program files\\windowsapps\\") == std::string::npos && std::filesystem::is_regular_file(module_folder / "Hades2.exe"))
			{
				return module_folder;
			}

			if (const auto microsoft_store_folder = find_microsoft_store_install(); !microsoft_store_folder.empty())
			{
				return microsoft_store_folder;
			}

			return get_game_executable_folder();
		}();

		return install_folder;
	}

	bool is_microsoft_store_game()
	{
		const auto executable_folder = big::string::to_lower(get_game_executable_folder().string());
		return executable_folder.find("\\program files\\windowsapps\\") != std::string::npos || is_microsoft_store_install(get_game_install_folder());
	}

	// Lua API: Function
	// Table: paths
	// Name: Content
	// Returns: string: Returns the GameFolder/Content folder path
	std::string hades_Content()
	{
		const auto executable_folder = get_game_executable_folder();
		auto folder                  = executable_folder / "Content";
		std::error_code ec;
		if (!std::filesystem::is_directory(folder / "Game", ec))
		{
			folder = executable_folder.parent_path() / "Content";
		}

		return (char*)folder.u8string().c_str();
	}

	// Lua API: Function
	// Table: paths
	// Name: Ship
	// Returns: string: Returns the GameFolder/Ship folder path
	static std::string hades_Ship()
	{
		return (char*)get_game_executable_folder().u8string().c_str();
	}

	// Lua API: Function
	// Table: paths
	// Name: GameInstall
	// Returns: string: Returns the physical folder containing the game executable
	static std::string hades_GameInstall()
	{
		return (char*)get_game_install_folder().u8string().c_str();
	}

	void bind(sol::table& state)
	{
		auto ns = state["paths"].get_or_create<sol::table>();

		ns["Content"]     = hades_Content;
		ns["Ship"]        = hades_Ship;
		ns["GameInstall"] = hades_GameInstall;
	}
} // namespace lua::paths_ext
