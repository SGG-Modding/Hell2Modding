#include "memory.hpp"

#include "memory/module.hpp"
#include "memory/pattern.hpp"
#include "pointers.hpp"

namespace lua::memory
{
	pointer::pointer(uintptr_t address) :
	    m_address(address)
	{
	}

	pointer::pointer() :
	    m_address(0)
	{
	}

	pointer pointer::add(uintptr_t offset)
	{
		return pointer(m_address + offset);
	}

	pointer pointer::sub(uintptr_t offset)
	{
		return pointer(m_address - offset);
	}

	pointer pointer::rip()
	{
		return add(*(std::int32_t*)m_address).add(4);
	}

	pointer pointer::rip_cmp()
	{
		return rip().add(1);
	}

	std::string pointer::get_string()
	{
		return std::string((char*)m_address);
	}

	void pointer::set_string(const std::string& string, int max_length)
	{
		strncpy((char*)m_address, string.data(), max_length);
	}

	bool pointer::is_null()
	{
		return m_address == 0;
	}

	bool pointer::is_valid()
	{
		return !is_null();
	}

	pointer pointer::deref()
	{
		return pointer(*(uintptr_t*)m_address);
	}

	uintptr_t pointer::get_address() const
	{
		return m_address;
	}

	// Lua API: Table
	// Name: memory
	// Table containing helper functions related to process memory.

	// Lua API: Function
	// Table: memory
	// Name: scan_pattern
	// Param: pattern: string: byte pattern (IDA format)
	// Returns: pointer: A pointer to the found address.
	// Scans the specified memory pattern within the "Hades2.exe" module and returns a pointer to the found address.
	static pointer scan_pattern(const std::string& pattern)
	{
		return pointer(::memory::module("Hades2.exe").scan(::memory::pattern(pattern)).value().as<uintptr_t>());
	}

	// Lua API: Function
	// Table: memory
	// Name: allocate
	// Param: size: integer: The number of bytes to allocate on the heap.
	// Returns: pointer: A pointer to the newly allocated memory.
	static pointer allocate(int size, sol::this_environment env)
	{
		void* mem = new uint8_t[size]();

		big::lua_module* module = big::lua_module::this_from(env);
		if (module)
		{
			module->m_data.m_allocated_memory.push_back(mem);
		}

		return pointer((uintptr_t)mem);
	}

	// Lua API: Function
	// Table: memory
	// Name: free
	// Param: ptr: pointer: The pointer that must be freed.
	static void free(pointer ptr, sol::this_environment env)
	{
		delete[] (void*)ptr.get_address();
		big::lua_module* module = big::lua_module::this_from(env);
		if (module)
		{
			std::erase_if(module->m_data.m_allocated_memory,
			              [ptr](void* addr)
			              {
				              return ptr.get_address() == (uintptr_t)addr;
			              });
		}
	}

	void bind(sol::table& state)
	{
		auto ns = state["memory"].get_or_create<sol::table>();

		auto pointer_ut = ns.new_usertype<pointer>("pointer", sol::constructors<pointer(uintptr_t)>());

		pointer_ut["add"]         = &pointer::add;
		pointer_ut["sub"]         = &pointer::sub;
		pointer_ut["rip"]         = &pointer::rip;
		pointer_ut["rip_cmp"]     = &pointer::rip_cmp;
		pointer_ut["get_byte"]    = &pointer::get<uint8_t>;
		pointer_ut["get_word"]    = &pointer::get<uint16_t>;
		pointer_ut["get_dword"]   = &pointer::get<uint32_t>;
		pointer_ut["get_qword"]   = &pointer::get<uint64_t>;
		pointer_ut["get_float"]   = &pointer::get<float>;
		pointer_ut["get_string"]  = &pointer::get_string;
		pointer_ut["set_byte"]    = &pointer::set<uint8_t>;
		pointer_ut["set_word"]    = &pointer::set<uint16_t>;
		pointer_ut["set_dword"]   = &pointer::set<uint32_t>;
		pointer_ut["set_qword"]   = &pointer::set<uint64_t>;
		pointer_ut["set_float"]   = &pointer::set<float>;
		pointer_ut["set_string"]  = &pointer::set_string;
		pointer_ut["patch_byte"]  = &pointer::patch<uint8_t>;
		pointer_ut["patch_word"]  = &pointer::patch<uint16_t>;
		pointer_ut["patch_dword"] = &pointer::patch<uint32_t>;
		pointer_ut["patch_qword"] = &pointer::patch<uint64_t>;
		pointer_ut["is_null"]     = &pointer::is_null;
		pointer_ut["is_valid"]    = &pointer::is_valid;
		pointer_ut["deref"]       = &pointer::deref;
		pointer_ut["get_address"] = &pointer::get_address;

		auto patch_ut       = ns.new_usertype<big::lua_patch>("patch", sol::no_constructor);
		patch_ut["apply"]   = &big::lua_patch::apply;
		patch_ut["restore"] = &big::lua_patch::restore;

		ns["scan_pattern"] = scan_pattern;
		ns["allocate"]     = allocate;
		ns["free"]         = free;
	}
} // namespace lua::memory
