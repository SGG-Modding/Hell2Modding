#pragma once
#include <hooks/hooking.hpp>

namespace big
{
	inline toml_v2::config_file::config_entry<bool>* g_hook_log_write_enabled = nullptr;

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

		big::g_hooking->get_original<hook_log_write>()(level, filename, line_number, result.c_str());

		if (!g_hook_log_write_enabled->get_value())
		{
			return;
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
			log_level = VERBOSE;
			break;
		case 16:
			levelStr  = "ERR";
			log_level = FATAL;
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
