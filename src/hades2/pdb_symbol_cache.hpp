#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <hades2/pdb_symbol_map.hpp>

namespace big::pdb_cache
{
	// Reading the game's PDB symbol table on every launch is cheap on the ship build's small
	// PDB but costs ~30s on the debug build's much larger one, blocking startup. The debug
	// build (and its PDB) is frozen, so we ship a prebuilt cache next to the PDB and load that
	// instead of parsing. Keyed by the PDB guid/signature/age: a missing or mismatched cache is
	// ignored and we fall back to a full parse, so the ship build (which ships no cache) is
	// unaffected. Addresses are stored as RVAs so the loader stays ASLR-safe.
	//
	// Format (little-endian, x64):
	//   'H2MP' (u32) | version (u32) | guid[16] | signature (u32) | age (u32)
	//   addr_count (u32) | addr_count * ( name_len u16 | name | rva u32 )
	//   size_count (u32) | size_count * ( name_len u16 | name | code_size u32 )

	constexpr uint32_t magic   = 0x504D3248; // 'H2MP'
	constexpr uint32_t version = 1;

	struct file_key
	{
		uint8_t guid[16];
		uint32_t signature;
		uint32_t age;
	};

	inline std::filesystem::path cache_path_for(const std::filesystem::path& pdb_path)
	{
		std::filesystem::path p = pdb_path;
		p += ".h2mcache";
		return p;
	}

	// Populates the global symbol maps from a valid matching cache and returns true, else false.
	inline bool try_load(const std::filesystem::path& path, const file_key& expected, uintptr_t base)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
		{
			return false;
		}

		uint32_t m = 0, v = 0;
		f.read((char*)&m, sizeof(m));
		f.read((char*)&v, sizeof(v));
		if (!f || m != magic || v != version)
		{
			return false;
		}

		file_key k{};
		f.read((char*)&k, sizeof(k));
		if (!f || std::memcmp(&k, &expected, sizeof(k)) != 0)
		{
			return false;
		}

		const auto read_section = [&](auto&& insert) -> bool
		{
			uint32_t count = 0;
			f.read((char*)&count, sizeof(count));
			if (!f)
			{
				return false;
			}

			std::string name;
			for (uint32_t i = 0; i < count; ++i)
			{
				uint16_t len = 0;
				f.read((char*)&len, sizeof(len));
				name.resize(len);
				if (len)
				{
					f.read(name.data(), len);
				}
				uint32_t value = 0;
				f.read((char*)&value, sizeof(value));
				if (!f)
				{
					return false;
				}
				insert(name, value);
			}
			return true;
		};

		// Names are already unique in the cache, so assign directly (no dedup needed). If a read
		// fails partway, clear so the fallback full parse starts from empty maps.
		if (!read_section([&](const std::string& n, uint32_t rva) { hades2_symbol_to_address[n] = base + rva; })
		    || !read_section([&](const std::string& n, uint32_t sz) { hades2_symbol_to_code_size[n] = sz; }))
		{
			hades2_symbol_to_address.clear();
			hades2_symbol_to_code_size.clear();
			return false;
		}

		return true;
	}

	// Serialises the current global maps as a cache. Used by the offline generation step only;
	// the shipped dll never calls this.
	inline bool write_from_globals(const std::filesystem::path& path, const file_key& key, uintptr_t base)
	{
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f)
		{
			return false;
		}

		f.write((const char*)&magic, sizeof(magic));
		f.write((const char*)&version, sizeof(version));
		f.write((const char*)&key, sizeof(key));

		const auto write_section = [&](const auto& map, auto get_value)
		{
			uint32_t count = (uint32_t)map.size();
			f.write((const char*)&count, sizeof(count));
			for (const auto& [name, val] : map)
			{
				uint16_t len = (uint16_t)name.size();
				f.write((const char*)&len, sizeof(len));
				f.write(name.data(), len);
				uint32_t v = get_value(val);
				f.write((const char*)&v, sizeof(v));
			}
		};

		write_section(hades2_symbol_to_address, [&](const gmAddress& a) -> uint32_t { return (uint32_t)((uint64_t)a - base); });
		write_section(hades2_symbol_to_code_size, [](size_t s) -> uint32_t { return (uint32_t)s; });

		return (bool)f;
	}
} // namespace big::pdb_cache
