#pragma once

#include "input/hotkey.hpp"

namespace big
{
	static inline hotkey g_gui_toggle("gui_toggle", VK_INSERT);

	class gui
	{
	public:
		ImU32 background_color = 3'696'311'571;
		ImU32 text_color       = 4'294'967'295;
		ImU32 button_color     = 2'947'901'213;
		ImU32 frame_color      = 2'942'518'340;
		float scale            = 1.0f;

	public:
		gui();
		virtual ~gui();
		gui(const gui&)                = delete;
		gui(gui&&) noexcept            = delete;
		gui& operator=(const gui&)     = delete;
		gui& operator=(gui&&) noexcept = delete;

		bool is_open();
		void toggle(bool toggle);

		ImGuiMouseCursor m_mouse_cursor = ImGuiMouseCursor_Arrow;

		void dx_init();
		void dx_on_tick();

		void save_default_style();
		void restore_default_style();

		void push_theme_colors();
		void pop_theme_colors();

		void wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	private:
		void toggle_mouse();

		std::filesystem::path m_file_path;
		toml::table m_table;
		void init_pref();
		void save_pref();

	private:
		bool m_is_open;
		toml::node* m_is_open_at_startup;
		toml::node* m_onboarded;

		ImGuiStyle m_default_config;
	};

	inline gui* g_gui;
} // namespace big
