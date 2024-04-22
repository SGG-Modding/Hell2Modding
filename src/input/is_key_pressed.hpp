#pragma once

namespace big
{
	inline bool is_key_pressed(uint16_t key)
	{
		if (GetAsyncKeyState(key) & 0x80'00)
		{
			return true;
		}

		return false;
	}
} // namespace big
