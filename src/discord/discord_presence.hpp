#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <windows.h>

namespace big::discord
{
	struct activity
	{
		std::string details;
		std::string large_image;
		std::string large_text;
		std::string state;
		std::int64_t start_timestamp = 0;
		bool operator==(const activity&) const = default;
	};

	class presence_service
	{
	public:
		presence_service() = default;
		~presence_service();
		presence_service(const presence_service&)            = delete;
		presence_service& operator=(const presence_service&) = delete;

		void start(std::string application_id, activity menu_activity);
		void stop();
		void submit_activity(activity value);
		void reset_to_menu();

	private:
		void run();
		void update_connection(bool connected, std::string error = {}, bool informational = false);
		std::mutex m_mutex;
		std::mutex m_lifecycle_mutex;
		std::optional<activity> m_activity;
		std::optional<activity> m_menu_activity;
		std::string m_application_id;
		HANDLE m_cancel     = nullptr;
		std::thread m_worker;
		bool m_started   = false;
		bool m_stopped   = false;
		bool m_connected = false;
		std::string m_reported_error;
	};

	presence_service& service();
} // namespace big::discord
