#pragma once

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sjson_overlay
{
	// The canonical directory name for SJSON data overlays within each mod's plugins_data folder.
	// Mods place .sjson files in plugins_data/<mod-guid>/<SJSON_DATA_DIR_NAME>/Animations/, Text/{lang}/, etc.
	inline constexpr const char* SJSON_DATA_DIR_NAME = "Hell2Modding-SJSON";

	// Known engine-scanned directories under Content/Game/ (lowercase, forward slashes).
	// .sjson files outside these directories get a validation warning.
	inline const std::vector<std::string> g_known_engine_directories = {
	    "game",
	    "game/animations",
	    "game/gui",
	    "game/obstacles",
	    "game/projectiles",
	    "game/units",
	    "game/weapons",
	};

	// Known locale subdirectories under Game/Text/ (lowercase).
	inline const std::vector<std::string> g_known_text_locales = {
	    "de", "el", "en", "es", "fr", "it", "ja", "ko",
	    "pl", "pt-br", "ru", "tr", "uk", "zh-cn", "zh-tw",
	};

	// Path index: logical_relpath (normalized, forward slashes) -> absolute_path
	inline std::unordered_map<std::string, std::string> g_path_index;

	// Tracks which (subdir, extension) pairs the engine has already enumerated
	inline std::unordered_set<std::string> g_enumerated_directories;

	inline std::shared_mutex g_overlay_mutex;

	std::string normalize_path(const std::string& path);
	bool is_known_engine_directory(const std::string& normalized_subdir);
	bool register_content_file(const std::string& logical_relpath, const std::string& absolute_path);
	void scan_content_directory(const std::filesystem::path& content_base_path);
	void scan_all_plugin_content_directories(const std::filesystem::path& plugins_data_path);
	void mark_directory_enumerated(const std::string& normalized_subdir, const std::string& extension);
	std::string lookup_overlay_path(const std::string& normalized_relpath);
} // namespace sjson_overlay
