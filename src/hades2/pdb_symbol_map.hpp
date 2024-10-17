#pragma once

#include "memory/gm_address.hpp"

namespace big
{
	inline std::unordered_map<std::string, gmAddress> hades2_symbol_to_address;

	// Function to insert symbols with unique names into the map
	inline void hades2_insert_symbol_to_map(const std::string& name, uintptr_t address)
	{
		if (hades2_symbol_to_address.find(name) == hades2_symbol_to_address.end())
		{
			hades2_symbol_to_address[name] = address;
			return;
		}

		const std::string original_name = name;
		std::string new_name            = name;
		int counter                     = 2;

		while (hades2_symbol_to_address.find(new_name) != hades2_symbol_to_address.end())
		{
			new_name = original_name + "_" + std::to_string(counter);
			counter++;
		}

		//LOG(DEBUG) << new_name << " at " HEX_TO_UPPER_OFFSET(address);

		hades2_symbol_to_address[new_name] = address;
	}
} // namespace big
