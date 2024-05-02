#include "gui_ext.hpp"

#include "gui/gui.hpp"

namespace lua::gui_ext
{
	// Lua API: Function
	// Table: gui
	// Name: is_open
	// Returns: bool: Returns true if the GUI is open.
	static bool is_open()
	{
		return big::g_gui->is_open();
	}

	void bind(sol::table& state)
	{
		auto ns = state["gui"].get_or_create<sol::table>();

		ns["is_open"] = is_open;
	}
} // namespace lua::gui_ext
