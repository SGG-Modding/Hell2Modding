#pragma once
#include "log.hpp"

#include "lua/lua_module.hpp"

namespace lua::log
{
	static void log_internal(sol::variadic_args& args, sol::this_environment& env, al::eLogLevel level)
	{
		std::stringstream data;

		size_t i                        = 0;
		const size_t last_element_index = args.size() - 1;
		for (const auto& arg : args)
		{
			data << env.env.value()["_h2m_tostring"](arg).get<const char*>();

			if (i != last_element_index)
			{
				data << '\t';
			}

			i++;
		}

		LOG(level) << big::lua_module::guid_from(env) << ": " << data.str();
	}

	// Lua API: Table
	// Name: h2m.log
	// Table containing functions for printing to console / log file.

	// Lua API: Function
	// Table: h2m.log
	// Name: info
	// Param: args: any
	// Logs an informational message.
	static void info(sol::variadic_args args, sol::this_environment env)
	{
		log_internal(args, env, INFO);
	}

	// Lua API: Function
	// Table: h2m.log
	// Name: warning
	// Param: args: any
	// Logs a warning message.
	static void warning(sol::variadic_args args, sol::this_environment env)
	{
		log_internal(args, env, WARNING);
	}

	// Lua API: Function
	// Table: h2m.log
	// Name: debug
	// Param: args: any
	// Logs a debug message.
	static void debug(sol::variadic_args args, sol::this_environment env)
	{
		log_internal(args, env, VERBOSE);
	}

	// Lua API: Function
	// Table: h2m.log
	// Name: error
	// Param: args: any
	// Logs an error message.
	static sol::reference error(sol::variadic_args args, sol::this_environment env)
	{
		log_internal(args, env, FATAL);

		return env.env.value()["_h2m_error"](args);
	}

	void bind(sol::state_view& state, sol::table& lua_ext)
	{
		state["_h2m_tostring"] = state["tostring"];

		state["print"]      = info;
		state["_h2m_error"] = state["error"];
		state["error"]      = error;

		auto ns       = lua_ext.create_named("log");
		ns["info"]    = info;
		ns["warning"] = warning;
		ns["debug"]   = debug;
		ns["error"]   = error;
	}
} // namespace lua::log
