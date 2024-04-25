#pragma once
#include "gui_element.hpp"

namespace lua::gui
{
	class raw_imgui_callback : public gui_element
	{
		sol::protected_function m_callback;

	public:
		raw_imgui_callback(sol::protected_function callback);

		void draw() override;
	};
} // namespace lua::gui
