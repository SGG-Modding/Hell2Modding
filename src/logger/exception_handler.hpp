#pragma once

namespace big
{
	class exception_handler final
	{
	public:
		exception_handler();
		virtual ~exception_handler();

	private:
		int m_previous_abort_behavior = 0;
		typedef void (*signal_handler)(int);
		signal_handler m_previous_handler = nullptr;

		void* m_exception_handler{};
		uint32_t m_old_error_mode{};
	};

	extern LONG vectored_exception_handler(EXCEPTION_POINTERS* exception_info);
} // namespace big
