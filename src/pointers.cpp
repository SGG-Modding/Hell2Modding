#include "pointers.hpp"

#include "hades2/hades2_pointers_layout_info.hpp"
#include "hooks/hooking.hpp"
#include "memory/all.hpp"

namespace big
{
	constexpr auto pointers::get_hades2_batch()
	{
		// clang-format off

        constexpr auto batch_and_hash = memory::make_batch<
		{
			"LS",
			"4C 3B D6 74 3A",
			[](memory::handle ptr)
			{
				g_pointers->m_hades2.m_lua_state = ptr.sub(0xD2).add(3).rip().as<lua_State**>();
			}
		}
        >(); // don't leave a trailing comma at the end

		// clang-format on

		return batch_and_hash;
	}

	void pointers::load_pointers_from_cache(const cache_file& cache_file, const uintptr_t pointer_to_cacheable_data_start, const memory::module& mem_region)
	{
		// fill pointers instance fields by reading the file data into it

		LOG(INFO) << "Loading pointers instance from cache";

		// multiple things here:
		// - iterate each cacheable field of the pointers instance
		// - add the base module address to the current offset retrieved from the cache
		// - assign that ptr to the pointers field
		uintptr_t* cache_data = reinterpret_cast<uintptr_t*>(cache_file.data());

		const size_t field_count_from_cache = cache_file.data_size() / sizeof(uintptr_t);
		LOG(INFO) << "Pointers cache: Loading " << field_count_from_cache << " fields from the cache";

		uintptr_t* field_ptr = reinterpret_cast<uintptr_t*>(pointer_to_cacheable_data_start);
		for (size_t i = 0; i < field_count_from_cache; i++)
		{
			uintptr_t offset = cache_data[i];
			uintptr_t ptr    = offset + mem_region.begin().as<uintptr_t>();

			if (mem_region.contains(memory::handle(ptr)))
			{
				*field_ptr = ptr;
			}
			else
			{
				LOG(FATAL) << "Just tried to load from cache a pointer supposedly within the hades2 module range but "
				              "isn't! Offset from start of pointers instance: "
				           << (reinterpret_cast<uintptr_t>(field_ptr) - reinterpret_cast<uintptr_t>(this));
			}

			field_ptr++;
		}
	}

	pointers::pointers() :
	    m_hades2_pointers_cache(g_file_manager.get_project_file("./cache/hades2_pointers.bin"))
	{
		g_pointers = this;

		const auto mem_region = memory::module("Hades2.exe");

		constexpr auto hades2_batch_and_hash = pointers::get_hades2_batch();
		constexpr cstxpr_str hades2_batch_name{"H2"};
		write_to_cache_or_read_from_cache<hades2_batch_name,
		                                  hades2_batch_and_hash.m_hash,
		                                  hades2_pointers_layout_info::offset_of_cache_begin_field,
		                                  hades2_pointers_layout_info::offset_of_cache_end_field,
		                                  hades2_batch_and_hash.m_batch>(m_hades2_pointers_cache, mem_region);
	}

	pointers::~pointers()
	{
		g_pointers = nullptr;
	}
} // namespace big
