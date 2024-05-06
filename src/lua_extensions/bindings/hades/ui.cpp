#include "ui.hpp"

#include <hooks/hooking.hpp>
#include <memory/gm_address.hpp>
#include <string/string.hpp>

namespace lua::hades::ui
{
	// Lua API: Function
	// Table: ui
	// Name: test_stuff
	static void test_stuff()
	{
	}

	void bind(sol::table &state)
	{
		auto ns = state.create_named("ui");
		ns.set_function("test_stuff", test_stuff);
	}
} // namespace lua::hades::ui
