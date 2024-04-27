#pragma once

#include <Psapi.h>

struct module_info_helper
{
	static inline void get_module_base_and_size(uintptr_t* base, size_t* size, const char* module_name = nullptr)
	{
		*base = {};
		*size = {};

		MODULEINFO module_info = {};

		HMODULE module = GetModuleHandleA(module_name);
		if (module)
		{
			GetModuleInformation(GetCurrentProcess(), module, &module_info, sizeof MODULEINFO);
		}

		if (base)
		{
			*base = uintptr_t(module_info.lpBaseOfDll);
		}
		if (size)
		{
			*size = size_t(module_info.SizeOfImage);
		}
	}
};

/**
 * \brief Provides compact utility to scan patterns and manipulate addresses.
 */
struct gmAddress
{
	uint64_t m_value = 0;

	gmAddress(uint64_t value) :
	    m_value(value)
	{
	}

	gmAddress() :
	    gmAddress(0)
	{
	}

private:
	static inline uint64_t s_module_base_default_module;
	static inline uint64_t s_module_size_default_module;

	static inline void init_if_needed_default_module_info()
	{
		static bool is_init = false;
		if (!is_init)
		{
			module_info_helper::get_module_base_and_size(&s_module_base_default_module, &s_module_size_default_module);
			is_init = true;
		}
	}

	static inline uint64_t s_module_base_our_module;
	static inline uint64_t s_module_size_our_module;

	static inline void init_if_needed_our_module_info()
	{
		static bool is_init = false;
		if (!is_init)
		{
			module_info_helper::get_module_base_and_size(&s_module_base_our_module, &s_module_size_our_module, "d3d12.dll");
			is_init = true;
		}
	}

	static gmAddress scan_internal(const char* pattern_str, const char* debug_name, uint64_t module_base, uint64_t module_size)
	{
		// Convert string pattern into byte array form
		int16_t pattern[256];
		uint8_t pattern_size = 0;
		for (size_t i = 0; i < strlen(pattern_str); i += 3)
		{
			const char* cursor = pattern_str + i;

			if (cursor[0] == '?')
			{
				pattern[pattern_size] = -1;
			}
			else
			{
				pattern[pattern_size] = static_cast<int16_t>(strtol(cursor, nullptr, 16));
			}

			// Support single '?' (we're incrementing by 3 expecting ?? and space, but with ? we must increment by 2)
			if (cursor[1] == ' ')
			{
				i--;
			}

			pattern_size++;
		}

		// In two-end comparison we approach from both sides (left & right) so size is twice smaller
		uint8_t scan_size = pattern_size;
		if (scan_size % 2 == 0)
		{
			scan_size /= 2;
		}
		else
		{
			scan_size = pattern_size / 2 + 1;
		}

		// Search for string through whole module
		// We use two-end comparison, nothing fancy but better than just brute force
		for (uint64_t i = 0; i < module_size; i += 1)
		{
			const uint8_t* module_position = (uint8_t*)(module_base + i);
			for (uint8_t j = 0; j < scan_size; j++)
			{
				int16_t left_expected = pattern[j];
				int16_t left_actual   = module_position[j];

				if (left_expected != -1 && left_actual != left_expected)
				{
					goto miss;
				}

				int16_t right_expected = pattern[pattern_size - j - 1];
				int16_t right_actual   = module_position[pattern_size - j - 1];

				if (right_expected != -1 && right_actual != right_expected)
				{
					goto miss;
				}
			}
			return {module_base + i};
		miss:;
		}

		if (debug_name)
		{
			LOG(FATAL) << "Missed " << debug_name;
		}
		return {0};
	}

public:
	static inline gmAddress scan(const char* pattern_str, const char* debug_name = nullptr)
	{
		init_if_needed_default_module_info();

		return scan_internal(pattern_str, debug_name, s_module_base_default_module, s_module_size_default_module);
	}

	static inline gmAddress scan_me(const char* pattern_str, const char* debug_name = nullptr)
	{
		init_if_needed_our_module_info();

		return scan_internal(pattern_str, debug_name, s_module_base_our_module, s_module_size_our_module);
	}

	gmAddress offset(int32_t offset) const
	{
		return m_value + offset;
	}

	gmAddress rip(int32_t offset = 0) const
	{
		return m_value + offset + 4 + *(int32_t*)(m_value + offset);
	}

	gmAddress get_call() const
	{
		return rip(1);
	}

	template<typename T>
	T as() const
	{
		return (T)m_value;
	}

	template<typename T>
	T* as_func() const
	{
		return as<T*>();
	}

	gmAddress& operator=(uint64_t value)
	{
		m_value = value;
		return *this;
	}

	operator uint64_t() const
	{
		return m_value;
	}

	operator void*() const
	{
		return (void*)m_value;
	}
};
