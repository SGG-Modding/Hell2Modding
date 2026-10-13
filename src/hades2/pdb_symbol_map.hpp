#pragma once

#include "memory/gm_address.hpp"

namespace big
{
	inline std::unordered_map<std::string, gmAddress> hades2_symbol_to_address;

	inline std::unordered_map<std::string, size_t> hades2_symbol_to_code_size;

	// The Ship Hades2.pdb GUID (lowercase 8-4-4-16 hex, no braces), captured while the symbol map is built (see
	// main.cpp). It uniquely identifies the exact game build, so features that rely on hardcoded engine RVAs / struct
	// offsets (which are only valid for a validated build) can gate themselves on it and disable cleanly after a game
	// update rather than reading stale addresses. Empty if the PDB was not parsed.
	inline std::string hades2_pdb_guid;

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

	inline void hades2_insert_symbol_to_map_code_size(const std::string& name, size_t code_size)
	{
		if (hades2_symbol_to_code_size.find(name) == hades2_symbol_to_code_size.end())
		{
			hades2_symbol_to_code_size[name] = code_size;
			return;
		}

		const std::string original_name = name;
		std::string new_name            = name;
		int counter                     = 2;

		while (hades2_symbol_to_code_size.find(new_name) != hades2_symbol_to_code_size.end())
		{
			new_name = original_name + "_" + std::to_string(counter);
			counter++;
		}

		//LOG(DEBUG) << new_name << " at " HEX_TO_UPPER_OFFSET(code_size);

		hades2_symbol_to_code_size[new_name] = code_size;
	}
} // namespace big
