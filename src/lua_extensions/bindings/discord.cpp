#include "discord.hpp"

#include "discord/discord_presence.hpp"

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace lua::discord
{
	static std::string activity_field(const sol::table& table, const char* name, std::size_t max_bytes, bool optional = false)
	{
		sol::object value = table.raw_get<sol::object>(name);
		if (optional && value.get_type() == sol::type::nil)
		{
			return {};
		}
		if (value.get_type() != sol::type::string)
		{
			throw std::invalid_argument(std::string("Discord activity field must be a string: ") + name);
		}
		auto text = value.as<std::string>();
		if (text.size() > max_bytes)
		{
			throw std::invalid_argument(std::string("Discord activity field exceeds its byte limit: ") + name);
		}
		if (!text.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) == 0)
		{
			throw std::invalid_argument(std::string("Discord activity field must be valid UTF-8: ") + name);
		}
		return text;
	}

	// Lua API: Class
	// Name: discord.activity
	// Table describing a Discord activity in UTF-8

	// Lua API: Field
	// Class: discord.activity
	// Field: details: string
	// Activity description, at most 128 bytes

	// Lua API: Field
	// Class: discord.activity
	// Field: large_image: string
	// Artwork key uploaded to the Discord application, at most 32 bytes

	// Lua API: Field
	// Class: discord.activity
	// Field: large_text: string
	// Large image hover text, at most 128 bytes

	// Lua API: Field
	// Class: discord.activity
	// Field: state: string|nil
	// Optional secondary status, at most 128 bytes

	// Lua API: Field
	// Class: discord.activity
	// Field: start_timestamp: integer|nil
	// Optional positive 64-bit Unix timestamp in seconds for the elapsed gameplay timer
	static big::discord::activity parse_activity(const sol::table& table)
	{
		std::int64_t start_timestamp = 0;
		sol::object timestamp = table.raw_get<sol::object>("start_timestamp");
		if (timestamp.get_type() != sol::type::nil)
		{
			if (!timestamp.is<std::int64_t>() || timestamp.as<std::int64_t>() <= 0)
			{
				throw std::invalid_argument("Discord activity start_timestamp must be a positive Unix timestamp in seconds");
			}
			start_timestamp = timestamp.as<std::int64_t>();
		}
		return {
		    activity_field(table, "details", 128),
		    activity_field(table, "large_image", 32),
		    activity_field(table, "large_text", 128),
		    activity_field(table, "state", 128, true),
		    start_timestamp,
		};
	}

	// Lua API: Function
	// Table: discord
	// Name: start
	// Param: application_id: string: Nonzero decimal Discord application ID
	// Param: menu_activity: {LUA_API_NAMESPACE}.discord.activity: Initial activity and main-menu fallback, with an omitted timestamp defaulting to the current time
	// Returns: boolean: true if accepted, false if validation or startup fails
	// Starts presence or updates its main-menu fallback
	static bool start(sol::object application_id_value, sol::object menu_activity)
	{
		try
		{
			if (application_id_value.get_type() != sol::type::string || menu_activity.get_type() != sol::type::table)
			{
				throw std::invalid_argument("Discord start requires an application ID string and an activity table");
			}
			const auto application_id = application_id_value.as<std::string>();
			std::uint64_t id = 0;
			const auto result = std::from_chars(application_id.data(), application_id.data() + application_id.size(), id);
			if (result.ec != std::errc{} || result.ptr != application_id.data() + application_id.size() || id == 0)
			{
				throw std::invalid_argument("Discord application ID must be a nonzero decimal snowflake");
			}
			big::discord::service().start(application_id, parse_activity(menu_activity.as<sol::table>()));
			return true;
		}
		catch (const std::exception& error)
		{
			LOG(WARNING) << "[Discord] start failed: " << error.what();
			return false;
		}
	}

	// Lua API: Function
	// Table: discord
	// Name: set_activity
	// Param: value: {LUA_API_NAMESPACE}.discord.activity: Replacement activity, with an omitted timestamp inheriting from the current activity or defaulting to the current time if no activity exists
	// Returns: boolean: true if queued, false if validation fails
	// Queues the latest activity, suppressing duplicates and waiting only for any outstanding command's acknowledgement
	static bool set_activity(sol::object value)
	{
		try
		{
			if (value.get_type() != sol::type::table)
			{
				throw std::invalid_argument("Discord set_activity requires an activity table");
			}
			big::discord::service().submit_activity(parse_activity(value.as<sol::table>()));
			return true;
		}
		catch (const std::exception& error)
		{
			LOG(WARNING) << "[Discord] set_activity failed: " << error.what();
			return false;
		}
	}

	void bind(sol::table& state)
	{
		// Lua API: Table
		// Name: discord
		// Discord Rich Presence transport
		// Validation and service errors are logged and return false
		// A return value of true means the request was accepted locally, not necessarily delivered to Discord
		auto ns = state.create_named("discord");
		ns.set_function("start", start);
		ns.set_function("set_activity", set_activity);
	}
}
