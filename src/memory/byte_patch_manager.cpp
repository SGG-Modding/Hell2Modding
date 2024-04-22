#include "byte_patch_manager.hpp"

#include "hooks/hooking.hpp"
#include "memory/byte_patch.hpp"
#include "pointers.hpp"

namespace big
{
	static void init()
	{
		//memory::byte_patch::make(g_pointers->m_gta.m_crash_trigger.add(4).as<uint8_t*>(), 0x00)->apply();

		//memory::byte_patch::make(g_pointers->m_sc.m_read_attribute_patch, std::vector{0x90, 0x90})->apply();
	}

	byte_patch_manager::byte_patch_manager()
	{
		init();

		g_byte_patch_manager = this;
	}

	byte_patch_manager::~byte_patch_manager()
	{
		memory::byte_patch::restore_all();

		g_byte_patch_manager = nullptr;
	}
} // namespace big
