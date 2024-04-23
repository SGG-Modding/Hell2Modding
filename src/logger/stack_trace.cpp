#include "stack_trace.hpp"

#include "memory/module.hpp"

#include <DbgHelp.h>
#include <winternl.h>

namespace big
{
	stack_trace::stack_trace() :
	    m_frame_pointers(32)
	{
		SymInitialize(GetCurrentProcess(), nullptr, true);
	}

	stack_trace::~stack_trace()
	{
		SymCleanup(GetCurrentProcess());
	}

	const std::vector<uintptr_t>& stack_trace::frame_pointers()
	{
		return m_frame_pointers;
	}

	void stack_trace::new_stack_trace(EXCEPTION_POINTERS* exception_info)
	{
		static std::mutex m;
		std::scoped_lock lock(m);

		m_exception_info = exception_info;

		m_dump << exception_code_to_string(exception_info->ExceptionRecord->ExceptionCode) << '\n';

		dump_module_info();
		dump_registers();
		dump_stacktrace();
		dump_cpp_exception();

		m_dump << "\n--------End of exception--------\n";
	}

	std::string stack_trace::str() const
	{
		return m_dump.str();
	}

	// I'd prefer to make some sort of global instance that cache all modules once instead of doing this every time
	void stack_trace::dump_module_info()
	{
		// modules cached already
		if (m_modules.size())
		{
			return;
		}

		m_dump << "Dumping modules:\n";

		const auto peb = reinterpret_cast<PPEB>(NtCurrentTeb()->ProcessEnvironmentBlock);
		if (!peb)
		{
			return;
		}

		const auto ldr_data = peb->Ldr;
		if (!ldr_data)
		{
			return;
		}

		const auto module_list = &ldr_data->InMemoryOrderModuleList;
		auto module_entry      = module_list->Flink;
		for (; module_list != module_entry; module_entry = module_entry->Flink)
		{
			const auto table_entry = CONTAINING_RECORD(module_entry, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
			if (!table_entry)
			{
				continue;
			}

			if (table_entry->FullDllName.Buffer)
			{
				auto mod_info = module_info(table_entry->FullDllName.Buffer, table_entry->DllBase);

				m_dump << mod_info.m_path.filename().string() << " Base Address: " << HEX_TO_UPPER(mod_info.m_base)
				       << " Size: " << mod_info.m_size << '\n';

				m_modules.emplace_back(std::move(mod_info));
			}
		}
	}

	void stack_trace::dump_registers()
	{
		const auto context = m_exception_info->ContextRecord;

		m_dump << "Dumping registers:\n"
		       << "RAX: " << HEX_TO_UPPER(context->Rax) << '\n'
		       << "RCX: " << HEX_TO_UPPER(context->Rcx) << '\n'
		       << "RDX: " << HEX_TO_UPPER(context->Rdx) << '\n'
		       << "RBX: " << HEX_TO_UPPER(context->Rbx) << '\n'
		       << "RSI: " << HEX_TO_UPPER(context->Rsi) << '\n'
		       << "RDI: " << HEX_TO_UPPER(context->Rdi) << '\n'
		       << "RSP: " << HEX_TO_UPPER(context->Rsp) << '\n'
		       << "RBP: " << HEX_TO_UPPER(context->Rbp) << '\n'
		       << "R8:  " << HEX_TO_UPPER(context->R8) << '\n'
		       << "R9:  " << HEX_TO_UPPER(context->R9) << '\n'
		       << "R10: " << HEX_TO_UPPER(context->R10) << '\n'
		       << "R11: " << HEX_TO_UPPER(context->R11) << '\n'
		       << "R12: " << HEX_TO_UPPER(context->R12) << '\n'
		       << "R13: " << HEX_TO_UPPER(context->R13) << '\n'
		       << "R14: " << HEX_TO_UPPER(context->R14) << '\n'
		       << "R15: " << HEX_TO_UPPER(context->R15) << '\n'
		       << "RIP: " << HEX_TO_UPPER(context->Rip) << '\n';
	}

	void stack_trace::dump_stacktrace()
	{
		m_dump << "Dumping stacktrace:";
		grab_stacktrace();

		// alloc once
		char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
		auto symbol          = reinterpret_cast<SYMBOL_INFO*>(buffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen   = MAX_SYM_NAME;

		DWORD64 displacement64;
		DWORD displacement;


		IMAGEHLP_LINE64 line;
		line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

		for (size_t i = 0; i < m_frame_pointers.size() && m_frame_pointers[i]; ++i)
		{
			const auto addr = m_frame_pointers[i];

			m_dump << "\n[" << i << "]\t";

			if (SymFromAddr(GetCurrentProcess(), addr, &displacement64, symbol))
			{
				if (SymGetLineFromAddr64(GetCurrentProcess(), addr, &displacement, &line))
				{
					m_dump << line.FileName << " L: " << line.LineNumber << " " << std::string_view(symbol->Name, symbol->NameLen);
				}
				else
				{
					const auto module_info = get_module_by_address(addr);
					if (module_info)
					{
						const auto module_name = module_info->m_path.filename().wstring();
						if (module_name == L"d3d12.dll")
						{
							m_dump << module_info->m_path.filename().string() << "+" << HEX_TO_UPPER(addr - module_info->m_base);
						}
						else
						{
							m_dump << module_info->m_path.filename().string() << " "
							       << std::string_view(symbol->Name, symbol->NameLen) << " ("
							       << module_info->m_path.filename().string() << "+" << HEX_TO_UPPER(addr - module_info->m_base) << ")";
						}
					}
					else
					{
						m_dump << "No module found for address " << HEX_TO_UPPER(addr);
					}
				}
			}
			else
			{
				const auto module_info = get_module_by_address(addr);
				if (module_info)
				{
					m_dump << module_info->m_path.filename().string() << "+" << HEX_TO_UPPER(addr - module_info->m_base) << " " << HEX_TO_UPPER(addr);
				}
				else
				{
					m_dump << "No module found for address " << HEX_TO_UPPER(addr);
				}
			}
		}
	}

	// Raised by SetThreadName to notify the debugger thread names
	constexpr DWORD SetThreadName_exception_code = 0x40'6D'13'88;

	// Raised by MSVC C++ Exceptions
	constexpr DWORD msvc_exception_code = 0xE0'6D'73'63;

	void stack_trace::dump_cpp_exception()
	{
		if (m_exception_info->ExceptionRecord->ExceptionCode == msvc_exception_code)
		{
			// TODO: This is disabled for now because it's not always a std::exeception in that array??
			// Atleast this completly fail when a YY Code Error happens.
			/*m_dump
			    << "\n\nC++ Exception: "
			    << reinterpret_cast<const std::exception*>(m_exception_info->ExceptionRecord->ExceptionInformation[1])->what() << '\n';*/
		}
		else if (m_exception_info->ExceptionRecord->ExceptionCode == SetThreadName_exception_code)
		{
			m_dump = {};
			m_dump << "SetThreadName Exception raised\n";
		}
	}

	void stack_trace::grab_stacktrace()
	{
		CONTEXT context = *m_exception_info->ContextRecord;

		STACKFRAME64 frame{};
		frame.AddrPC.Mode      = AddrModeFlat;
		frame.AddrFrame.Mode   = AddrModeFlat;
		frame.AddrStack.Mode   = AddrModeFlat;
		frame.AddrPC.Offset    = context.Rip;
		frame.AddrFrame.Offset = context.Rbp;
		frame.AddrStack.Offset = context.Rsp;

		for (size_t i = 0; i < m_frame_pointers.size(); ++i)
		{
			if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
			{
				break;
			}
			m_frame_pointers[i] = frame.AddrPC.Offset;
		}
	}

	const stack_trace::module_info* stack_trace::get_module_by_address(uintptr_t addr) const
	{
		for (auto& mod_info : m_modules)
		{
			if (mod_info.m_base < addr && addr < mod_info.m_base + mod_info.m_size)
			{
				return &mod_info;
			}
		}
		return nullptr;
	}

#define MAP_PAIR_STRINGIFY(x) \
	{                         \
		x, #x                 \
	}
	static const std::map<DWORD, std::string> exceptions = {MAP_PAIR_STRINGIFY(EXCEPTION_ACCESS_VIOLATION), MAP_PAIR_STRINGIFY(EXCEPTION_ARRAY_BOUNDS_EXCEEDED), MAP_PAIR_STRINGIFY(EXCEPTION_DATATYPE_MISALIGNMENT), MAP_PAIR_STRINGIFY(EXCEPTION_FLT_DENORMAL_OPERAND), MAP_PAIR_STRINGIFY(EXCEPTION_FLT_DIVIDE_BY_ZERO), MAP_PAIR_STRINGIFY(EXCEPTION_FLT_INEXACT_RESULT), MAP_PAIR_STRINGIFY(EXCEPTION_FLT_INEXACT_RESULT), MAP_PAIR_STRINGIFY(EXCEPTION_FLT_INVALID_OPERATION), MAP_PAIR_STRINGIFY(EXCEPTION_FLT_OVERFLOW), MAP_PAIR_STRINGIFY(EXCEPTION_FLT_STACK_CHECK), MAP_PAIR_STRINGIFY(EXCEPTION_FLT_UNDERFLOW), MAP_PAIR_STRINGIFY(EXCEPTION_ILLEGAL_INSTRUCTION), MAP_PAIR_STRINGIFY(EXCEPTION_IN_PAGE_ERROR), MAP_PAIR_STRINGIFY(EXCEPTION_INT_DIVIDE_BY_ZERO), MAP_PAIR_STRINGIFY(EXCEPTION_INT_OVERFLOW), MAP_PAIR_STRINGIFY(EXCEPTION_INVALID_DISPOSITION), MAP_PAIR_STRINGIFY(EXCEPTION_NONCONTINUABLE_EXCEPTION), MAP_PAIR_STRINGIFY(EXCEPTION_PRIV_INSTRUCTION), MAP_PAIR_STRINGIFY(EXCEPTION_STACK_OVERFLOW), MAP_PAIR_STRINGIFY(EXCEPTION_BREAKPOINT), MAP_PAIR_STRINGIFY(EXCEPTION_SINGLE_STEP)};

	std::string stack_trace::exception_code_to_string(const DWORD code)
	{
		if (const auto& it = exceptions.find(code); it != exceptions.end())
		{
			return it->second;
		}

		if (code == msvc_exception_code)
		{
			return "C++ MSVC Exception";
		}

		if (code == SetThreadName_exception_code)
		{
			return "SetThreadName Exception";
		}

		std::stringstream exception_code_str;
		exception_code_str << HEX_TO_UPPER(code);
		return "UNKNOWN_EXCEPTION: CODE: " + exception_code_str.str();
	}
} // namespace big
