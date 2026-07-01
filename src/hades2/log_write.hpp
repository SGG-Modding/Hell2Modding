#pragma once
#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <mutex>
#include <unordered_set>

namespace big
{
	inline toml_v2::config_file::config_entry<bool>* g_hook_log_write_enabled = nullptr;

	// De-dupe set for the debug build's per-frame asserts, which the assert-dialog patch routes to the log
	inline std::mutex g_seen_asserts_mutex;
	inline std::unordered_set<std::string> g_seen_asserts;

	inline void hook_log_write(char level, const char* filename, int line_number, const char* message, ...)
	{
		va_list args;

		va_start(args, message);
		int size = vsnprintf(nullptr, 0, message, args);
		va_end(args);

		// Allocate a buffer to hold the formatted string
		std::string result(size + 1, '\0'); // +1 for the null terminator

		// Format the string into the buffer
		va_start(args, message);
		vsnprintf(&result[0], size + 1, message, args);
		va_end(args);

		if (strcmp(result.c_str(), "Scripts modification detected.") == 0)
		{
			return;
		}

		// Suppress harmless FMOD error from MarkerCallback when playing GUID-referenced events without a matching .strings.bank entry
		if (strcmp(result.c_str(), "Error: MarkerCallbackInternal: couldn't get event path: FMOD error 74 - The requested event, parameter, bus or vca could not be found.") == 0)
		{
			return;
		}

		// Drop exact-duplicate assert lines so a per-frame debug-build assert is logged once, not thousands
		// of times. Keyed on the full formatted line, so different asserts still each log once.
		if (g_hades2_is_debug_build_flag.load() && result.find("Assert: File = ") != std::string::npos)
		{
			std::scoped_lock lock(g_seen_asserts_mutex);
			if (!g_seen_asserts.insert(result).second)
			{
				return;
			}
		}

		big::g_hooking->get_original<hook_log_write>()(level, filename, line_number, result.c_str());

		if (!result.starts_with("Script er"))
		{
			if (!g_hook_log_write_enabled->get_value())
			{
				return;
			}
		}

		result.pop_back();

		al::eLogLevel log_level;
		const char* levelStr;
		switch (level)
		{
		case 8:
			levelStr  = "WARN";
			log_level = WARNING;
			break;
		case 4:
			levelStr  = "INFO";
			log_level = INFO;
			break;
		case 2:
			levelStr  = "DBG";
			log_level = DEBUG;
			break;
		case 16:
			levelStr  = "ERR";
			log_level = ERROR;
			break;
		default:
			levelStr  = "UNK";
			log_level = INFO;
			break;
		}

		if (strlen(filename) > 41)
		{
			LOG(log_level) << "[" << levelStr << "] [" << (filename + 41) << ":" << line_number << "] " << result;
		}
		else
		{
			LOG(log_level) << "[" << levelStr << "] [" << filename << ":" << line_number << "] " << result;
		}
	}
} // namespace big
