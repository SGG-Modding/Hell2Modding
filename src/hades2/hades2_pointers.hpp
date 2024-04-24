#pragma once

#include <memory/handle.hpp>

namespace big
{
	// needed for serialization of the pointers cache
#pragma pack(push, 1)

	struct hades2_pointers
	{
		lua_State** m_lua_state;
	};

#pragma pack(pop)
	static_assert(sizeof(hades2_pointers) % 8 == 0, "Pointers are not properly aligned");
} // namespace big
