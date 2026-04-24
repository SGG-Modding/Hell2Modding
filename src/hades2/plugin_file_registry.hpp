#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>

struct vo_file_registry
{
	std::unordered_map<std::string, std::string> fsb_files;
	std::unordered_map<std::string, std::string> txt_files;
};

extern vo_file_registry additional_vo_files;
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
