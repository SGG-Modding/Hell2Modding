#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <windows.h>

namespace big::discord
{
	struct ipc_frame
	{
		std::uint32_t opcode = 0;
		std::string payload;
	};

	enum class read_result
	{
		frame,
		timeout,
		cancelled,
		error
	};

	// All operations, including close, belong to a single worker thread
	class ipc_connection
	{
	public:
		ipc_connection() = default;
		~ipc_connection();
		ipc_connection(const ipc_connection&)            = delete;
		ipc_connection& operator=(const ipc_connection&) = delete;

		bool connect(std::string& error);
		bool write(std::uint32_t opcode, std::string_view payload, HANDLE cancel, DWORD timeout_ms, std::string& error);
		read_result read(ipc_frame& frame, HANDLE cancel, DWORD timeout_ms, std::string& error);
		void close();
		bool is_open() const;

	private:
		DWORD connect_path(const std::wstring& path);
		HANDLE m_pipe = INVALID_HANDLE_VALUE;
		std::array<unsigned char, 8> m_header{};
		std::size_t m_header_size = 0;
		bool m_header_decoded     = false;
		std::uint32_t m_opcode    = 0;
		std::string m_payload;
		std::size_t m_payload_size = 0;
	};
} // namespace big::discord
