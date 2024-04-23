#pragma once

class IDXGISwapChain3;

namespace big
{
	using init_callback = std::function<void()>;

	struct dx_callback
	{
		std::function<void()> m_callback;
		int64_t m_priority;
	};

	using wndproc_callback = std::function<void(HWND, UINT32, WPARAM, LPARAM)>;

	class renderer final
	{
	public:
		std::vector<init_callback> m_init_callbacks;

	public:
		std::vector<dx_callback> m_dx_callbacks;

	private:
		std::vector<wndproc_callback> m_wndproc_callbacks;

	public:
		WNDPROC m_og_wndproc = nullptr;

		ImFont* font_title     = nullptr;
		ImFont* font_sub_title = nullptr;
		ImFont* font_small     = nullptr;
		ImFont* font_icon      = nullptr;

	public:
		void init();

		explicit renderer();
		~renderer();

		bool add_init_callback(init_callback callback);

		/**
		 * @brief Add a callback function to draw your ImGui content in
		 *
		 * @param callback function + priority, The higher the priority the value the later it gets drawn on top.
		 * @return true
		 * @return false
		 */
		bool add_dx_callback(dx_callback callback);

		void remove_wndproc_callback(size_t callback_index);

		/**
		 * @brief Add a callback function on wndproc
		 *
		 * @param callback Function
		 */
		size_t add_wndproc_callback(wndproc_callback callback);

		void wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

		void render_imgui(IDXGISwapChain3* pSwapChain);

	public:
		void init_fonts();
		bool hook();

		HWND m_window_handle;
	};

	inline renderer* g_renderer{};
} // namespace big
