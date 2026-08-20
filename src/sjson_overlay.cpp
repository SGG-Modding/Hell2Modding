#include "sjson_overlay.hpp"

#include <string/string.hpp>

namespace sjson_overlay
{
	static const std::filesystem::path& vanilla_content_root()
	{
		static const std::filesystem::path root = []() -> std::filesystem::path
		{
			constexpr std::size_t max_path = MAX_PATH * 2;
			wchar_t buffer[max_path];
			const DWORD length = GetModuleFileNameW(nullptr, buffer, max_path);
			if (length == 0 || length == max_path)
			{
				return {};
			}
			return std::filesystem::path(buffer).parent_path().parent_path() / "Content";
		}();
		return root;
	}

	bool vanilla_file_exists(const std::string& normalized_relpath, std::filesystem::path& vanilla_path)
	{
		const auto& root = vanilla_content_root();
		if (root.empty())
		{
			return false;
		}

		std::error_code ec;
		std::filesystem::path candidate = root / std::filesystem::path(normalized_relpath);
		if (!std::filesystem::is_regular_file(candidate, ec))
		{
			return false;
		}

		vanilla_path = candidate.make_preferred();
		return true;
	}

	bool replaces_vanilla_file(const std::string& absolute_path, std::filesystem::path& vanilla_path)
	{
		// Vanilla ships the same filenames in both resolutions, so one directory is enough.
		static const std::pair<const char*, const char*> content_directories[] = {
		    {".map_text",     "Maps"            },
		    {".thing_bin",    "Maps/bin"        },
		    {".bik",          "Movies/1080p"    },
		    {".bik_atlas",    "Movies/1080p"    },
		    {".fsb",          "Audio/Desktop/VO"},
		    {".txt",          "Audio/Desktop/VO"},
		    {".pkg",          "Packages/1080p"  },
		    {".pkg_manifest", "Packages/1080p"  },
		    {".gpk",          "GR2/_Optimized"  },
		};

		const std::string normalized = normalize_path(absolute_path);
		const auto last_slash        = normalized.rfind('/');
		const std::string filename   = last_slash != std::string::npos ? normalized.substr(last_slash + 1) : normalized;
		const std::string lower      = big::string::to_lower(filename);

		for (const auto& [extension, directory] : content_directories)
		{
			if (lower.ends_with(extension) && vanilla_file_exists(std::string(directory) + "/" + filename, vanilla_path))
			{
				return true;
			}
		}

		return false;
	}

	std::string normalize_path(const std::string& path)
	{
		std::string result = path;
		std::replace(result.begin(), result.end(), '\\', '/');
		while (!result.empty() && result.back() == '/')
		{
			result.pop_back();
		}
		return result;
	}

	bool is_known_engine_directory(const std::string& normalized_subdir)
	{
		std::string lower = big::string::to_lower(normalized_subdir);

		// Check for text directories: game/text/{locale}
		if (lower.find("game/text/") == 0)
		{
			std::string locale = lower.substr(10); // len("game/text/") = 10
			for (const auto& known_locale : g_known_text_locales)
			{
				if (locale == known_locale)
				{
					return true;
				}
			}
			return false;
		}

		for (const auto& known : g_known_engine_directories)
		{
			if (lower == known)
			{
				return true;
			}
		}

		return false;
	}

	bool register_content_file(const std::string& logical_relpath, const std::string& absolute_path)
	{
		std::string normalized = normalize_path(logical_relpath);

		// Extract directory and extension
		std::string subdir;
		std::string filename;
		auto last_slash = normalized.rfind('/');
		if (last_slash != std::string::npos)
		{
			subdir   = normalized.substr(0, last_slash);
			filename = normalized.substr(last_slash + 1);
		}
		else
		{
			filename = normalized;
		}

		std::string extension;
		auto dot_pos = filename.rfind('.');
		if (dot_pos != std::string::npos)
		{
			extension = filename.substr(dot_pos);
		}

		if (big::string::to_lower(extension) != ".sjson")
		{
			LOG(WARNING) << "[SJSON] Only .sjson files are supported. Ignoring: " << normalized;
			return false;
		}

		if (!is_known_engine_directory(subdir))
		{
			LOG(WARNING) << "[SJSON] SJSON file '" << normalized
			             << "' is in a directory not known to be scanned by the engine. "
			             << "Known directories: Game/, Game/Animations/, Game/GUI/, Game/Obstacles/, "
			             << "Game/Units/, Game/Weapons/, Game/Projectiles/, Game/Text/{lang}/";
		}

		std::unique_lock lock(g_overlay_mutex);

		if (g_path_index.count(normalized))
		{
			if (g_path_index[normalized] != absolute_path)
			{
				LOG(WARNING) << "[SJSON] File '" << normalized << "' already registered from '" << g_path_index[normalized] << "', ignoring '" << absolute_path << "'";
			}
			return false;
		}

		if (std::filesystem::path vanilla_path; vanilla_file_exists(normalized, vanilla_path))
		{
			LOG(ERROR) << "[SJSON] File '" << absolute_path << "' has the same path as the vanilla file '"
			           << (char*)vanilla_path.u8string().c_str()
			           << "' and would replace it if loaded. Skipping it to keep the vanilla content intact. "
			           << "Give the file a unique name, ideally including your AuthorName-ModName.";
			return false;
		}

		g_path_index[normalized] = absolute_path;

		std::string enum_key = big::string::to_lower(subdir) + "|" + big::string::to_lower(extension);
		if (g_enumerated_directories.count(enum_key))
		{
			LOG(WARNING) << "[SJSON] File '" << normalized
			             << "' registered after engine enumerated '" << subdir
			             << "'. Will not be loaded until next launch.";
		}

		LOG(INFO) << "Adding to SJSON files: " << absolute_path;
		return true;
	}

	void scan_content_directory(const std::filesystem::path& content_base_path)
	{
		if (!std::filesystem::exists(content_base_path))
		{
			return;
		}

		for (const auto& entry : std::filesystem::recursive_directory_iterator(
		         content_base_path,
		         std::filesystem::directory_options::skip_permission_denied | std::filesystem::directory_options::follow_directory_symlink))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".sjson")
			{
				continue;
			}

			auto rel_path   = std::filesystem::relative(entry.path(), content_base_path);
			std::string rel = (char*)rel_path.u8string().c_str();
			std::string abs = (char*)entry.path().u8string().c_str();

			std::string logical_rel = "Game/" + normalize_path(rel);
			register_content_file(logical_rel, abs);
		}
	}

	void scan_all_plugin_content_directories(const std::filesystem::path& plugins_data_path)
	{
		if (!std::filesystem::exists(plugins_data_path))
		{
			return;
		}

		for (const auto& mod_dir : std::filesystem::directory_iterator(
		         plugins_data_path,
		         std::filesystem::directory_options::skip_permission_denied | std::filesystem::directory_options::follow_directory_symlink))
		{
			if (!mod_dir.is_directory())
			{
				continue;
			}

			auto sjson_dir = mod_dir.path() / SJSON_DATA_DIR_NAME;
			if (std::filesystem::exists(sjson_dir) && std::filesystem::is_directory(sjson_dir))
			{
				scan_content_directory(sjson_dir);
			}
		}
	}

	void mark_directory_enumerated(const std::string& normalized_subdir, const std::string& extension)
	{
		std::unique_lock lock(g_overlay_mutex);
		g_enumerated_directories.insert(big::string::to_lower(normalized_subdir) + "|" + big::string::to_lower(extension));
	}

	std::string lookup_overlay_path(const std::string& normalized_relpath)
	{
		std::shared_lock lock(g_overlay_mutex);
		auto it = g_path_index.find(normalized_relpath);
		if (it != g_path_index.end())
		{
			return it->second;
		}
		return "";
	}
} // namespace sjson_overlay
