#pragma once

#include <cstddef>
#include <cstdint>

// Minimal views over the native Hades II option-screen GUI objects, limited to the fields this feature reads or writes.
// Offsets are validated with static_assert against the current game build. The matching engine functions are resolved
// by PDB symbol name at runtime (see big::hades2_symbol_to_address). Only sgg::GUIComponent base fields and
// MiscSettingsScreen members are used, which stay stable across the button-layout changes that occur between game
// versions.
namespace big::mod_settings::sgg
{
	// sgg::Vectormath Vector2: two floats, 8 bytes. As a function argument this is an integer-class aggregate, so it is
	// passed in a general-purpose register (RDX/R8/...), not an XMM register - the by-value POD typing below reproduces
	// that ABI.
	struct Vec2
	{
		float x;
		float y;
	};

	static_assert(sizeof(Vec2) == 8);

	// eastl::vector<T> stores three pointers (begin, end, capacity) followed by its allocator begin/end are enough to
	// iterate an existing vector.
	template<typename T>
	struct eastl_vector
	{
		T* m_begin;
		T* m_end;
		T* m_capacity;

		T* begin() const
		{
			return m_begin;
		}

		T* end() const
		{
			return m_end;
		}

		std::size_t size() const
		{
			return static_cast<std::size_t>(m_end - m_begin);
		}
	};

	static_assert(sizeof(eastl_vector<void*>) == 0x18);

	struct GUIComponentButton;

	// sgg::GUIComponent, the base of every menu widget.
	struct GUIComponent
	{
		char m_pad0[0x0C];
		bool m_hidden;  // +0x0C
		bool m_useable; // +0x0D
		char m_pad1[0x02];
		float m_location_x; // +0x10
		float m_location_y; // +0x14
		char m_pad2[0x0F];
		bool m_is_useable;     // +0x27
		bool m_can_be_focused; // +0x28
		char m_pad3[0x13];
		float m_fade_opacity; // +0x3C
		char m_pad4[0x04];
		float m_fade_target;          // +0x44
		std::int32_t m_custom_width;  // +0x48
		std::int32_t m_custom_height; // +0x4C
		char m_pad5[0x4'E8];
		std::uint64_t m_id; // +0x538
	};

	static_assert(offsetof(GUIComponent, m_hidden) == 0x0C);
	static_assert(offsetof(GUIComponent, m_useable) == 0x0D);
	static_assert(offsetof(GUIComponent, m_location_x) == 0x10);
	static_assert(offsetof(GUIComponent, m_is_useable) == 0x27);
	static_assert(offsetof(GUIComponent, m_can_be_focused) == 0x28);
	static_assert(offsetof(GUIComponent, m_fade_opacity) == 0x3C);
	static_assert(offsetof(GUIComponent, m_fade_target) == 0x44);
	static_assert(offsetof(GUIComponent, m_custom_width) == 0x48);
	static_assert(offsetof(GUIComponent, m_custom_height) == 0x4C);
	static_assert(offsetof(GUIComponent, m_id) == 0x5'38);
	static_assert(sizeof(GUIComponent) == 0x5'40);

	// Byte offset of GUIComponentButton::mOwner (MenuScreen*), set after construction.
	inline constexpr std::size_t gui_component_button_owner_offset = 0x5'A0;
	inline constexpr std::size_t gui_component_button_size         = 0x5'B0;

	// Byte offset of GUIComponentButton::mSelectable (bool). GUIComponentButton::IsSelectable returns it.
	// MenuScreen::SetMouseOver skips a component whose IsSelectable is false, so clearing it makes a button
	// non-hoverable and non-selectable (used to fully disable a greyed action button).
	inline constexpr std::size_t gui_component_button_selectable_offset = 0x5'51;

	// Byte offset of GUIComponentButton::mUnderMouseTexture (sgg::TextureHandle, a 32-bit id). GUIComponentButton::Draw
	// draws this hover-highlight overlay only when it is valid and mIsUseable@0x27 is set (gate at Draw+0xBF). A
	// greyed-but-hoverable action (kept useable so it can show its description) clears this so it does not flash a
	// clickable-looking hover glow. mSelectedTexture (selection overlay) is at 0x564 (SetSelectedTexture clears it).
	inline constexpr std::size_t gui_component_button_under_mouse_texture_offset = 0x5'68;

	// Byte offset of GUIComponentButton::mDisplayNameId (sgg::HashGuid: a 32-bit interned-string id). The engine
	// derives a button's visible label from this id:. GUIComponentButton::UseDefaultText resolves the id back to its
	// interned string, looks that up in the localized text data, and sets the label from the result (falling back to
	// the raw string on a miss). UseDefaultText re-runs on every localization pass, including a live language change,
	// so this id - not any string handed to SetDisplayName - is what determines the persistent label.
	inline constexpr std::size_t gui_component_button_display_name_id_offset = 0x1'68;

	// sgg::MenuScreen, the base of MiscSettingsScreen mComponents owns every live widget that is drawn and hit-tested
	// freed components are dropped from it mAnchor is the base location the engine gives freshly created option
	// components.
	struct MenuScreen
	{
		char m_pad_anchor[0x50];
		Vec2 m_anchor; // +0x50
		char m_pad_mo[0x68];
		GUIComponent* m_mouse_over_component;     // +0xC0
		eastl_vector<GUIComponent*> m_components; // +0xC8
		char m_pad_prompts[0xC0];                 // 0xE0 .. 0x1A0
		GUIComponent* m_confirm_button;           // +0x1A0 (bottom "Confirm/Select/Toggle" prompt)
		GUIComponent* m_cancel_button;            // +0x1A8 (bottom "Exit/Back" prompt)
		GUIComponent* m_selected_component;       // +0x1B0
	};

	static_assert(offsetof(MenuScreen, m_anchor) == 0x50);
	static_assert(offsetof(MenuScreen, m_mouse_over_component) == 0xC0);
	static_assert(offsetof(MenuScreen, m_components) == 0xC8);
	static_assert(offsetof(MenuScreen, m_confirm_button) == 0x1'A0);
	static_assert(offsetof(MenuScreen, m_cancel_button) == 0x1'A8);
	static_assert(offsetof(MenuScreen, m_selected_component) == 0x1'B0);

	// sgg::MiscSettingsScreen, the native tabbed options screen. The category buttons are laid out contiguously from
	// +0x388 (Gameplay) to +0x3F8 (Debug). The non-user categories such as Editor follow the eight user-facing ones
	// mOptions holds the current category's option components.
	struct MiscSettingsScreen
	{
		char m_pad_psi[0x3'44];
		std::uint32_t m_page_start_index; // +0x344
		std::uint32_t m_options_per_page; // +0x348
		char m_pad_cf[0x04];
		GUIComponent* m_component_focused;       // +0x350
		GUIComponent* m_current_category_button; // +0x358
		GUIComponent* m_last_option_button;      // +0x360
		char m_pad_a[0x20];
		GUIComponentButton* m_gameplay_options_button; // +0x388
		char m_pad_b[0x30];
		GUIComponentButton* m_credits_options_button; // +0x3C0
		GUIComponentButton* m_editor_options_button;  // +0x3C8
		char m_pad_c[0x28];
		GUIComponentButton* m_debug_options_button; // +0x3F8
		bool m_category_focused;                    // +0x400 (false = option navigation, true = tab navigation)
		char m_pad_d[0x07];                         // 0x401 .. 0x408
		eastl_vector<GUIComponent*> m_options;      // +0x408
		GUIComponent* m_up_arrow;                   // +0x420 (scroll-up arrow button)
		GUIComponent* m_down_arrow;                 // +0x428 (scroll-down arrow button)
		char m_pad_e[0x10];                         // 0x430 .. 0x440 (scroll bar + tracker)
		GUIComponent* m_defaults_button;            // +0x440 (bottom "Reset" prompt)
		char m_pad_f[0x18];                         // 0x448 .. 0x460
		GUIComponent* m_description_box;            // +0x460
	};

	static_assert(offsetof(MiscSettingsScreen, m_page_start_index) == 0x3'44);
	static_assert(offsetof(MiscSettingsScreen, m_options_per_page) == 0x3'48);
	static_assert(offsetof(MiscSettingsScreen, m_component_focused) == 0x3'50);
	static_assert(offsetof(MiscSettingsScreen, m_current_category_button) == 0x3'58);
	static_assert(offsetof(MiscSettingsScreen, m_last_option_button) == 0x3'60);
	static_assert(offsetof(MiscSettingsScreen, m_gameplay_options_button) == 0x3'88);
	static_assert(offsetof(MiscSettingsScreen, m_credits_options_button) == 0x3'C0);
	static_assert(offsetof(MiscSettingsScreen, m_editor_options_button) == 0x3'C8);
	static_assert(offsetof(MiscSettingsScreen, m_debug_options_button) == 0x3'F8);
	static_assert(offsetof(MiscSettingsScreen, m_category_focused) == 0x4'00);
	static_assert(offsetof(MiscSettingsScreen, m_options) == 0x4'08);
	static_assert(offsetof(MiscSettingsScreen, m_up_arrow) == 0x4'20);
	static_assert(offsetof(MiscSettingsScreen, m_down_arrow) == 0x4'28);
	static_assert(offsetof(MiscSettingsScreen, m_defaults_button) == 0x4'40);
	static_assert(offsetof(MiscSettingsScreen, m_description_box) == 0x4'60);
} // namespace big::mod_settings::sgg
