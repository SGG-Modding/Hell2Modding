#include "gui.hpp"

#include "gui/gui.hpp"
#include "gui_element.hpp"
#include "raw_imgui_callback.hpp"

namespace lua::gui
{
	static void add_menu_bar_callback(sol::this_environment& env, std::unique_ptr<lua::gui::gui_element> element)
	{
		big::lua_module* module = big::lua_module::this_from(env);
		if (module)
		{
			module->m_data.m_menu_bar_callbacks.push_back(std::move(element));
		}
	}

	static void add_always_draw_independent_element(sol::this_environment& env, std::unique_ptr<lua::gui::gui_element> element)
	{
		big::lua_module* module = big::lua_module::this_from(env);
		if (module)
		{
			module->m_data.m_always_draw_independent_gui.push_back(std::move(element));
		}
	}

	static void add_independent_element(sol::this_environment& env, std::unique_ptr<lua::gui::gui_element> element)
	{
		big::lua_module* module = big::lua_module::this_from(env);
		if (module)
		{
			module->m_data.m_independent_gui.push_back(std::move(element));
		}
	}

	// Lua API: Function
	// Table: h2m.gui
	// Name: is_open
	// Returns: bool: Returns true if the GUI is open.
	static bool is_open()
	{
		return big::g_gui->is_open();
	}

	// Lua API: Function
	// Table: h2m.gui
	// Name: add_to_menu_bar
	// Param: imgui_rendering: function: Function that will be called under your dedicated space in the imgui main menu bar.
	// Registers a function that will be called under your dedicated space in the imgui main menu bar.
	// **Example Usage:**
	// ```lua
	// h2m.gui.add_to_menu_bar(function()
	//   if h2m.ImGui.BeginMenu("Ayo") then
	//       if h2m.ImGui.Button("Label") then
	//         h2m.log.info("hi")
	//       end
	//       h2m.ImGui.EndMenu()
	//   end
	// end)
	// ```
	static lua::gui::raw_imgui_callback* add_to_menu_bar(sol::protected_function imgui_rendering, sol::this_environment state)
	{
		auto element = std::make_unique<lua::gui::raw_imgui_callback>(imgui_rendering);
		auto el_ptr  = element.get();
		add_menu_bar_callback(state, std::move(element));
		return el_ptr;
	}

	// Lua API: Function
	// Table: h2m.gui
	// Name: add_always_draw_imgui
	// Param: imgui_rendering: function: Function that will be called every rendering frame, regardless of the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.
	// Registers a function that will be called every rendering frame, regardless of the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.
	// **Example Usage:**
	// ```lua
	// h2m.gui.add_always_draw_imgui(function()
	//   if h2m.ImGui.Begin("My Custom Window") then
	//       if h2m.ImGui.Button("Label") then
	//         h2m.log.info("hi")
	//       end
	//
	//   end
	//   h2m.ImGui.End()
	// end)
	// ```
	static lua::gui::raw_imgui_callback* add_always_draw_imgui(sol::protected_function imgui_rendering, sol::this_environment state)
	{
		auto element = std::make_unique<lua::gui::raw_imgui_callback>(imgui_rendering);
		auto el_ptr  = element.get();
		add_always_draw_independent_element(state, std::move(element));
		return el_ptr;
	}

	// Lua API: Function
	// Table: h2m.gui
	// Name: add_imgui
	// Param: imgui_rendering: function: Function that will be called every rendering frame, only if the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.
	// Registers a function that will be called every rendering frame, only if the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.
	// **Example Usage:**
	// ```lua
	// h2m.gui.add_imgui(function()
	//   if h2m.ImGui.Begin("My Custom Window") then
	//       if h2m.ImGui.Button("Label") then
	//         h2m.log.info("hi")
	//       end
	//
	//   end
	//   h2m.ImGui.End()
	// end)
	// ```
	static lua::gui::raw_imgui_callback* add_imgui(sol::protected_function imgui_rendering, sol::this_environment state)
	{
		auto element = std::make_unique<lua::gui::raw_imgui_callback>(imgui_rendering);
		auto el_ptr  = element.get();
		add_independent_element(state, std::move(element));
		return el_ptr;
	}

	void bind(sol::table& state)
	{
		auto ns                     = state.create_named("gui");
		ns["is_open"]               = is_open;
		ns["add_imgui"]             = add_imgui;
		ns["add_always_draw_imgui"] = add_always_draw_imgui;
		ns["add_to_menu_bar"]       = add_to_menu_bar;
	}
} // namespace lua::gui
