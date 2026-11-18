#include "discord_ipc.hpp"

#include <algorithm>
#include <system_error>
#include <utility>
#include <vector>

namespace big::discord
{
	namespace
	{
		constexpr std::size_t maximum_payload = 64 * 1024;

		enum class io_result
		{
			complete,
			timeout,
			cancelled,
			error
		};

		std::string windows_error(const char* operation, DWORD code)
		{
			std::string detail = std::system_category().message(static_cast<int>(code));
			std::string result = std::string(operation) + " failed (Windows error " + std::to_string(code) + ")";
			while (!detail.empty() && (detail.back() == '\r' || detail.back() == '\n'))
			{
				detail.pop_back();
			}
			if (!detail.empty())
			{
				result += ": " + detail;
			}
			return result;
		}

		class deadline
		{
		public:
			explicit deadline(DWORD timeout) :
			    m_start(GetTickCount64()),
			    m_timeout(timeout)
			{
			}

			DWORD remaining() const
			{
				const ULONGLONG elapsed = GetTickCount64() - m_start;
				return elapsed >= m_timeout ? 0 : m_timeout - static_cast<DWORD>(elapsed);
			}

		private:
			ULONGLONG m_start;
			DWORD m_timeout;
		};

		io_result transfer(HANDLE pipe, bool writing, void* buffer, DWORD size, HANDLE cancel, const deadline& limit, DWORD& transferred, std::string& error)
		{
			transferred = 0;
			if (cancel)
			{
				const DWORD state = WaitForSingleObject(cancel, 0);
				if (state == WAIT_OBJECT_0)
				{
					return io_result::cancelled;
				}
				if (state == WAIT_FAILED)
				{
					error = windows_error("Checking IPC cancellation event", GetLastError());
					return io_result::error;
				}
			}

			OVERLAPPED operation{};
			operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!operation.hEvent)
			{
				error = windows_error("Creating IPC completion event", GetLastError());
				return io_result::error;
			}

			const BOOL immediate = writing ? WriteFile(pipe, buffer, size, nullptr, &operation) : ReadFile(pipe, buffer, size, nullptr, &operation);
			const DWORD initial_error = immediate ? ERROR_SUCCESS : GetLastError();
			if (!immediate && initial_error != ERROR_IO_PENDING)
			{
				CloseHandle(operation.hEvent);
				error = windows_error(writing ? "Writing Discord IPC" : "Reading Discord IPC", initial_error);
				return io_result::error;
			}

			io_result result = io_result::complete;
			if (!immediate)
			{
				HANDLE events[2] = {cancel ? cancel : operation.hEvent, operation.hEvent};
				const DWORD wait = WaitForMultipleObjects(cancel ? 2 : 1, events, FALSE, limit.remaining());
				if (cancel && wait == WAIT_OBJECT_0)
				{
					result = io_result::cancelled;
				}
				else if (wait == WAIT_TIMEOUT)
				{
					result = io_result::timeout;
				}
				else if (wait != WAIT_OBJECT_0 + (cancel ? 1 : 0))
				{
					error  = windows_error("Waiting for Discord IPC", GetLastError());
					result = io_result::error;
				}
			}

			// Cancel and drain before releasing OVERLAPPED, its event, or the caller's buffer
			if (result != io_result::complete)
			{
				CancelIoEx(pipe, &operation);
			}
			const BOOL completed         = GetOverlappedResult(pipe, &operation, &transferred, TRUE);
			const DWORD completion_error = completed ? ERROR_SUCCESS : GetLastError();
			CloseHandle(operation.hEvent);
			if (result != io_result::complete)
			{
				return result;
			}
			if (!completed)
			{
				error = windows_error(writing ? "Completing Discord IPC write" : "Completing Discord IPC read", completion_error);
				return io_result::error;
			}
			if (transferred == 0)
			{
				error = "Discord IPC peer closed the connection or made no I/O progress";
				return io_result::error;
			}
			return io_result::complete;
		}

		std::uint32_t decode_integer(const unsigned char* bytes)
		{
			return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
		}

		void encode_integer(unsigned char* bytes, std::uint32_t value)
		{
			for (unsigned int i = 0; i < 4; ++i)
			{
				bytes[i] = static_cast<unsigned char>(value >> (i * 8));
			}
		}
	} // namespace

	ipc_connection::~ipc_connection()
	{
		close();
	}

	bool ipc_connection::connect(std::string& error)
	{
		error.clear();
		for (int index = 0; index < 10; ++index)
		{
			const DWORD code = connect_path(L"\\\\?\\pipe\\discord-ipc-" + std::to_wstring(index));
			if (code == ERROR_SUCCESS)
			{
				error.clear();
				return true;
			}
			if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND && error.empty())
			{
				error = windows_error("Opening Discord IPC pipe", code);
			}
		}
		return false;
	}

	DWORD ipc_connection::connect_path(const std::wstring& path)
	{
		close();
		m_pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		if (!is_open())
		{
			return GetLastError();
		}
		return ERROR_SUCCESS;
	}

	bool ipc_connection::write(std::uint32_t opcode, std::string_view payload, HANDLE cancel, DWORD timeout_ms, std::string& error)
	{
		error.clear();
		if (!is_open())
		{
			error = "Cannot write Discord IPC: connection is closed";
			return false;
		}
		if (payload.size() > maximum_payload)
		{
			error = "Discord IPC payload exceeds the 65536-byte limit";
			return false;
		}
		const deadline limit(timeout_ms);
		std::vector<unsigned char> bytes(8 + payload.size());
		encode_integer(bytes.data(), opcode);
		encode_integer(bytes.data() + 4, static_cast<std::uint32_t>(payload.size()));
		std::copy(payload.begin(), payload.end(), bytes.begin() + 8);

		std::size_t position = 0;
		while (position < bytes.size())
		{
			DWORD count = 0;
			const io_result result = transfer(m_pipe, true, bytes.data() + position, static_cast<DWORD>(bytes.size() - position), cancel, limit, count, error);
			if (result != io_result::complete)
			{
				if (result == io_result::timeout)
				{
					error = "Discord IPC write timed out";
				}
				else if (result == io_result::cancelled)
				{
					error = "Discord IPC write cancelled";
				}
				// A partially sent frame cannot be retried on the same byte stream
				close();
				return false;
			}
			position += count;
			if (position < bytes.size() && limit.remaining() == 0)
			{
				error = "Discord IPC write timed out";
				close();
				return false;
			}
		}
		return true;
	}

	read_result ipc_connection::read(ipc_frame& frame, HANDLE cancel, DWORD timeout_ms, std::string& error)
	{
		error.clear();
		if (!is_open())
		{
			error = "Cannot read Discord IPC: connection is closed";
			return read_result::error;
		}
		const deadline limit(timeout_ms);
		for (;;)
		{
			if (m_header_size == m_header.size() && !m_header_decoded)
			{
				m_opcode                   = decode_integer(m_header.data());
				const std::uint32_t length = decode_integer(m_header.data() + 4);
				if (length > maximum_payload)
				{
					error = "Discord IPC peer sent a payload exceeding the 65536-byte limit";
					close();
					return read_result::error;
				}
				m_payload.resize(length);
				m_header_decoded = true;
			}
			if (m_header_decoded && m_payload_size == m_payload.size())
			{
				frame.opcode     = m_opcode;
				frame.payload    = std::move(m_payload);
				m_header_size    = 0;
				m_header_decoded = false;
				m_payload_size   = 0;
				return read_result::frame;
			}

			const bool reading_header = !m_header_decoded;
			void* buffer =
			    reading_header ? static_cast<void*>(m_header.data() + m_header_size) : static_cast<void*>(m_payload.data() + m_payload_size);
			const std::size_t remaining = reading_header ? m_header.size() - m_header_size : m_payload.size() - m_payload_size;
			DWORD count = 0;
			const io_result result = transfer(m_pipe, false, buffer, static_cast<DWORD>(remaining), cancel, limit, count, error);
			if (reading_header)
			{
				m_header_size += count;
			}
			else
			{
				m_payload_size += count;
			}
			if (result == io_result::timeout)
			{
				return read_result::timeout;
			}
			if (result == io_result::cancelled)
			{
				return read_result::cancelled;
			}
			if (result == io_result::error)
			{
				close();
				return read_result::error;
			}
			// Preserve every completed byte, even when the frame exceeds this call's deadline
			if (limit.remaining() == 0)
			{
				return read_result::timeout;
			}
		}
	}

	void ipc_connection::close()
	{
		if (is_open())
		{
			CloseHandle(m_pipe);
		}
		m_pipe           = INVALID_HANDLE_VALUE;
		m_header_size    = 0;
		m_header_decoded = false;
		m_payload.clear();
		m_payload_size = 0;
		m_opcode       = 0;
	}

	bool ipc_connection::is_open() const
	{
		return m_pipe != INVALID_HANDLE_VALUE;
	}
} // namespace big::discord
