#include "detour_hook.hpp"

#include "memory/handle.hpp"
#include "threads/util.hpp"

namespace big
{
	detour_hook::detour_hook()
	{
	}

	detour_hook::detour_hook(const std::string& name, void* detour)
	{
		set_instance(name, detour);
	}

	detour_hook::detour_hook(const std::string& name, void* target, void* detour)
	{
		set_instance(name, target, detour);
	}

	void big::detour_hook::set_instance(const std::string& name, void* detour)
	{
		m_name   = name;
		m_detour = detour;
	}

	void big::detour_hook::set_instance(const std::string& name, void* target, void* detour)
	{
		m_name   = name;
		m_target = target;
		m_detour = detour;

		create_hook();
	}

	void detour_hook::set_target_and_create_hook(void* target)
	{
		m_target = target;
		create_hook();
	}

	void detour_hook::create_hook()
	{
		if (!m_target)
		{
			return;
		}

		fix_hook_address();

		m_detour_object = std::make_unique<PLH::x64Detour>((uintptr_t)m_target, (uintptr_t)m_detour, (uintptr_t*)&m_original);
	}

	detour_hook::~detour_hook() noexcept
	{
		if (!m_target)
		{
			return;
		}
	}

	detour_hook::detour_hook(detour_hook&& that) :
	    m_name(std::move(that.m_name)),
	    m_original(std::move(that.m_original)),
	    m_target(std::move(that.m_target)),
	    m_detour(std::move(that.m_detour)),
	    m_detour_object(std::move(that.m_detour_object))
	{
	}

	void detour_hook::enable()
	{
		if (!m_target)
		{
			return;
		}

		if (!m_detour_object->isHooked())
		{
			auto suspended_thread_here = false;
			if (!threads::are_suspended)
			{
				suspended_thread_here = true;

				threads::suspend_all_but_one();
			}

			if (!m_detour_object->hook())
			{
				LOG(FATAL) << std::format("Failed to create hook '{}' at 0x{:X}", m_name, uintptr_t(m_target));
			}

			if (suspended_thread_here)
			{
				threads::resume_all();
			}
		}
	}

	void detour_hook::disable()
	{
		if (!m_target)
		{
			return;
		}

		if (m_detour_object->isHooked())
		{
			if (!threads::are_suspended)
			{
				threads::suspend_all_but_one();
			}

			if (m_detour_object->unHook())
			{
				LOG(FATAL) << "Failed to disable hook '" << m_name << "' at 0x" << HEX_TO_UPPER(uintptr_t(m_target)) << "(error: " << m_name << ")";
			}

			if (threads::are_suspended)
			{
				threads::resume_all();
			}
		}
	}

	DWORD exp_handler(PEXCEPTION_POINTERS exp, const std::string& name)
	{
		return exp->ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
	}

	void detour_hook::fix_hook_address()
	{
		auto ptr = memory::handle(m_target);
		while (ptr.as<uint8_t&>() == 0xE9)
		{
			ptr = ptr.add(1).rip();
		}
		m_target = ptr.as<void*>();
	}
} // namespace big
