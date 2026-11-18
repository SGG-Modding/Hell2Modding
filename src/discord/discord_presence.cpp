#include "discord_presence.hpp"

#include "discord_ipc.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>

namespace big::discord
{
	using namespace std::chrono_literals;
	using clock = std::chrono::steady_clock;
	using json  = nlohmann::json;

	static std::int64_t unix_seconds()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	presence_service::~presence_service()
	{
		stop();
	}

	void presence_service::start(std::string application_id, activity menu_activity)
	{
		std::scoped_lock lifecycle(m_lifecycle_mutex);
		if (m_stopped)
		{
			throw std::runtime_error("Discord service has stopped");
		}
		std::scoped_lock lock(m_mutex);
		if (!m_started)
		{
			m_cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!m_cancel)
			{
				throw std::runtime_error("Cannot create Discord cancellation event");
			}
			m_application_id = std::move(application_id);
			try
			{
				m_worker = std::thread([this]
				{
					try
					{
						run();
					}
					catch (const std::exception& error)
					{
						update_connection(false, error.what());
					}
				});
			}
			catch (...)
			{
				CloseHandle(m_cancel);
				m_cancel = nullptr;
				m_application_id.clear();
				throw;
			}
			m_started = true;
		}
		if (!menu_activity.start_timestamp)
		{
			menu_activity.start_timestamp = unix_seconds();
		}
		m_menu_activity = std::move(menu_activity);
		m_activity      = m_menu_activity;
	}

	void presence_service::stop()
	{
		std::scoped_lock lifecycle(m_lifecycle_mutex);
		m_stopped = true;
		if (m_cancel)
		{
			SetEvent(m_cancel);
		}
		if (m_worker.joinable())
		{
			m_worker.join();
		}
		if (m_cancel)
		{
			CloseHandle(m_cancel);
			m_cancel = nullptr;
		}
	}

	void presence_service::submit_activity(activity value)
	{
		std::scoped_lock lock(m_mutex);
		if (!value.start_timestamp)
		{
			value.start_timestamp = m_activity ? m_activity->start_timestamp : unix_seconds();
		}
		m_activity = std::move(value);
	}

	void presence_service::reset_to_menu()
	{
		std::scoped_lock lock(m_mutex);
		m_activity = m_menu_activity;
		if (m_activity)
		{
			m_activity->start_timestamp = unix_seconds();
		}
	}

	void presence_service::update_connection(bool connected, std::string error, bool informational)
	{
		bool report_error     = false;
		bool report_connected = false;
		{
			std::scoped_lock lock(m_mutex);
			report_connected = connected && !m_connected;
			if (!error.empty() && error != m_reported_error)
			{
				report_error     = true;
				m_reported_error = error;
			}
			if (report_connected)
			{
				m_reported_error.clear();
			}
			m_connected = connected;
		}
		if (report_error)
		{
			if (informational)
			{
				LOG(INFO) << "[Discord] " << error;
			}
			else
			{
				LOG(WARNING) << "[Discord] " << error;
			}
		}
		else if (report_connected)
		{
			LOG(INFO) << "[Discord] connected";
		}
	}

	static json activity_payload(const activity& value)
	{
		json payload = {
		    {"type", 0},
		    {"details", value.details},
		    {"timestamps", {{"start", value.start_timestamp}}},
		    {"assets", {{"large_image", value.large_image}, {"large_text", value.large_text}}},
		};
		if (!value.state.empty())
		{
			payload["state"] = value.state;
		}
		return payload;
	}

	void presence_service::run()
	{
		ipc_connection connection;
		std::size_t retry                      = 0;
		constexpr std::array<DWORD, 6> backoff = {1000, 2000, 4000, 8000, 16'000, 30'000};
		std::uint64_t nonce                    = 0;
		auto cancelled                         = [this]
		{
			return WaitForSingleObject(m_cancel, 0) == WAIT_OBJECT_0;
		};
		while (!cancelled())
		{
			std::string error;
			bool ready = false;
			update_connection(false);
			const bool opened = connection.connect(error);
			if (opened && connection.write(0, json({{"v", 1}, {"client_id", m_application_id}}).dump(), m_cancel, 5000, error))
			{
				const auto deadline = clock::now() + 5s;
				while (!cancelled() && clock::now() < deadline)
				{
					ipc_frame frame;
					const auto read = connection.read(frame, m_cancel, 250, error);
					if (read == read_result::timeout)
					{
						continue;
					}
					if (read != read_result::frame)
					{
						break;
					}
					if (frame.opcode == 3)
					{
						if (!connection.write(4, frame.payload, m_cancel, 5000, error))
						{
							break;
						}
						continue;
					}
					if (frame.opcode != 1)
					{
						error = "Discord closed the handshake or sent an unexpected opcode";
						break;
					}
					auto message = json::parse(frame.payload, nullptr, false);
					if (message.is_object() && message.contains("evt") && message["evt"] == "READY" && message.contains("data")
					    && message["data"].is_object() && message["data"].contains("v") && message["data"]["v"] == 1)
					{
						ready = true;
					}
					else
					{
						error = "Discord rejected the handshake or sent an invalid READY response";
					}
					break;
				}
				if (!ready && error.empty() && !cancelled())
				{
					error = "Discord handshake timed out";
				}
			}

			if (ready)
			{
				retry = 0;
				update_connection(true);
				std::optional<activity> sent;
				bool has_sent = false;
				std::string pending_nonce;
				auto ack_deadline = clock::time_point::max();
				while (!cancelled())
				{
					std::optional<activity> current;
					{
						std::scoped_lock lock(m_mutex);
						current = m_activity;
					}
					if (pending_nonce.empty() && (!has_sent || current != sent))
					{
						pending_nonce = std::to_string(++nonce);
						json command  = {
                            {"cmd", "SET_ACTIVITY"},
                            {"nonce", pending_nonce},
                            {"args", {{"pid", GetCurrentProcessId()}, {"activity", current ? activity_payload(*current) : json(nullptr)}}},
                        };
						if (!connection.write(1, command.dump(), m_cancel, 5000, error))
						{
							break;
						}
						sent         = std::move(current);
						has_sent     = true;
						ack_deadline = clock::now() + 5s;
					}
					if (!pending_nonce.empty() && clock::now() >= ack_deadline)
					{
						error = "Discord activity acknowledgement timed out";
						break;
					}
					ipc_frame frame;
					const auto read = connection.read(frame, m_cancel, 250, error);
					if (read == read_result::timeout)
					{
						continue;
					}
					if (read != read_result::frame)
					{
						break;
					}
					if (frame.opcode == 3)
					{
						if (!connection.write(4, frame.payload, m_cancel, 5000, error))
						{
							break;
						}
						continue;
					}
					if (frame.opcode == 4)
					{
						continue;
					}
					if (frame.opcode != 1)
					{
						error = "Discord closed the connection or sent an unexpected opcode";
						break;
					}
					auto message = json::parse(frame.payload, nullptr, false);
					if (!message.is_object())
					{
						error = "Discord sent invalid RPC JSON";
						break;
					}
					if (message.contains("evt") && message["evt"] == "ERROR")
					{
						error = "Discord rejected an RPC command";
						break;
					}
					if (!pending_nonce.empty() && message.contains("nonce") && message["nonce"] == pending_nonce
					    && message.contains("cmd") && message["cmd"] == "SET_ACTIVITY")
					{
						pending_nonce.clear();
					}
				}
			}
			if (cancelled())
			{
				if (ready)
				{
					const auto clear = json({
					                            {"cmd", "SET_ACTIVITY"},
					                            {"nonce", std::to_string(++nonce)},
					                            {"args", {{"pid", GetCurrentProcessId()}, {"activity", nullptr}}},
					                        })
					                       .dump();
					std::string clear_error;
					if (!connection.write(1, clear, nullptr, 500, clear_error))
					{
						LOG(WARNING) << "[Discord] Activity clear failed: " << clear_error;
					}
				}
				break;
			}
			connection.close();
			const bool unavailable = !opened && error.empty();
			update_connection(false, unavailable ? "Discord client not found" : error.empty() ? "Discord connection ended" : error, unavailable);
			WaitForSingleObject(m_cancel, backoff[std::min(retry++, backoff.size() - 1)]);
		}
		connection.close();
		update_connection(false);
	}

	presence_service& service()
	{
		// The exit hook stops this process-owned service before termination, outside loader lock
		static auto* instance = new presence_service;
		return *instance;
	}
} // namespace big::discord
