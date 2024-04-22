#pragma once

#include <memory/handle.hpp>

namespace big
{
	// needed for serialization of the pointers cache
#pragma pack(push, 1)

	struct hades2_pointers
	{
		lua_State** m_lua_state;
		void (*m_init_lua)();
		void (*m_scriptmanager_clear)();

		bool (*m_sgg_BacktraceHandleException)(_EXCEPTION_POINTERS*);
		void (*m_sgg_ForgeRenderer_PrintErrorMessageAndAssert)();
	};

#pragma pack(pop)
	static_assert(sizeof(hades2_pointers) % 8 == 0, "Pointers are not properly aligned");
} // namespace big
