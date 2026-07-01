#pragma once

#include "memory/gm_address.hpp"

#include <atomic>

namespace big
{
	inline std::unordered_map<std::string, gmAddress> hades2_symbol_to_address;

	inline std::unordered_map<std::string, size_t> hades2_symbol_to_code_size;

	// Cached result of hades2_is_debug_build(), set once at startup. hades2_is_debug_build()
	// reads hades2_symbol_to_address, which other threads mutate via operator[], so it must not be called
	// from multi-threaded hot paths (e.g. the log hook).
	inline std::atomic<bool> g_hades2_is_debug_build_flag{false};

	// The debug build is the only Hades II build that ships the in-engine LiveEditor (map editor);
	// the ship build compiles it out, so a LiveEditor-only symbol tells the builds apart. Only
	// meaningful once the symbol map has been populated by read_game_pdb().
	inline bool hades2_is_debug_build()
	{
		const auto it = hades2_symbol_to_address.find("sgg::LiveEditorWindows::Draw");
		return it != hades2_symbol_to_address.end() && (bool)it->second;
	}

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
