#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>

struct vo_file_registry
{
	std::unordered_map<std::string, std::string> fsb_files;
	std::unordered_map<std::string, std::string> txt_files;
};

struct bik_file_paths
{
	std::string path_1080p;
	std::string path_720p;

	// Returns the path for the requested resolution, falling back to whichever is available.
	// Checks the context string (e.g. a basePath or filename) for "720p" to determine intent.
	const std::string& resolve(const char* context) const
	{
		bool want_720p = context && strstr(context, "720p");
		if (want_720p && !path_720p.empty())
		{
			return path_720p;
		}
		if (!path_1080p.empty())
		{
			return path_1080p;
		}
		return path_720p;
	}
};

extern vo_file_registry additional_vo_files;
extern std::unordered_map<std::string, bik_file_paths> additional_bik_files;
extern std::shared_mutex g_plugin_files_mutex;

// Validates that a voice bank stem name (e.g. "Authornamemodnamevoicebank") is compatible with
// the engine's cue normalization. The engine lowercases characters at positions 2+
// of the bank name (within the "/VO/" prefixed cue string), then re-uppercases the
// first character of the substrings "field" and "keepsake". Any other uppercase
// letters *after the first character* will cause a mismatch between the stored cue key
// and the Lua-side cue reference.
// Returns true if the name is safe, false if it would cause lookup failures.
inline bool validate_vo_bank_name(const std::string& stem)
{
	if (stem.empty())
	{
		return false;
	}

	// Simulate the engine's normalization: lowercase everything after the first character,
	// then re-uppercase 'f' in "field" and 'k' in "keepsake".
	std::string normalized = stem;
	for (size_t i = 1; i < normalized.size(); i++)
	{
		normalized[i] = (char)std::tolower((unsigned char)normalized[i]);
	}

	for (const char* keyword : {"field", "keepsake"})
	{
		auto pos = normalized.find(keyword, 1);
		if (pos != std::string::npos)
		{
			normalized[pos] = (char)std::toupper((unsigned char)normalized[pos]);
		}
	}

	return normalized == stem;
}
