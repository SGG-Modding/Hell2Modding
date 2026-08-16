#include "mod_settings.hpp"

#include "sgg_gui.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <gui/renderer.hpp>
#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <lua/lua_manager.hpp>
#include <malloc.h>
#include <map>
#include <memory/gm_address.hpp>
#include <set>
#include <sstream>
#include <string>
#include <toml_v2/config_file.hpp>
#include <vector>

// clang-format off
#include <AsyncLogger/Logger.hpp>
using namespace al;
// clang-format on
#undef ERROR

namespace big::mod_settings
{
#pragma region Native screen offsets, RVAs, and constants

	using sgg::GUIComponent;
	using sgg::MenuScreen;
	using sgg::MiscSettingsScreen;
	using sgg::Vec2;

	static constexpr std::size_t gui_component_name_offset = 0x4'88;

	// Retuning mDef then re-running SetupComponent re-applies the template.
	static constexpr std::size_t component_data_offset = 0x88; // GUIComponent::mData (sgg::ComponentData).
	static constexpr std::size_t component_def_offset  = 0xA8; // mData(0x88) + ComponentData::mDef(0x20).


	static constexpr std::size_t def_use_text_area         = 0x05; // mUseTextArea (bool)
	static constexpr std::size_t def_add_text_area         = 0x06; // mAddTextArea (bool)
	static constexpr std::size_t def_deselect_on_mouse_off = 0x13; // mDeselectOnMouseOff (bool)

	static constexpr std::size_t def_y                 = 0x20; // mY (float) row Y, read by UpdateScrollState
	static constexpr std::size_t def_offset_y          = 0x2C; // mOffsetY (float) template vertical offset
	static constexpr std::size_t def_scale             = 0x34; // mScale (float) uniform component scale
	static constexpr std::size_t def_text_offset_x     = 0x50; // mTextOffsetX (float)
	static constexpr std::size_t def_width             = 0x74; // mWidth (float -> mCustomWidth)
	static constexpr std::size_t def_height            = 0x78; // mHeight (float -> mCustomHeight)
	static constexpr std::size_t def_graphic           = 0x80; // mGraphic (HashGuid)
	static constexpr std::size_t def_selected_graphic  = 0x84; // mSelectedGraphic (HashGuid)
	static constexpr std::size_t def_alternate_graphic = 0x88; // mAlternateGraphic (HashGuid)
	// Base OnClicked plays mPressSound, so copy the native toggle cue there.
	static constexpr std::size_t def_press_sound        = 0x1'B0; // mPressSound (sgg::SoundCue)
	static constexpr std::size_t def_toggle_on_sound    = 0x1'E0; // mToggleOnSound (sgg::SoundCue)
	static constexpr std::size_t def_toggle_off_sound   = 0x1'F0; // mToggleOffSound (sgg::SoundCue)
	static constexpr std::size_t sound_cue_size         = 0x10;   // sizeof sgg::SoundCue
	static constexpr std::size_t def_add_color          = 0x0D;   // mAddColor (bool)
	static constexpr std::size_t def_red                = 0xEC;   // mRed button tint (float)
	static constexpr std::size_t def_green              = 0xF0;   // mGreen button tint (float)
	static constexpr std::size_t def_blue               = 0xF4;   // mBlue button tint (float)
	static constexpr std::size_t def_text_justification = 0xEA;   // mTextJustification (sgg::Justification: LEFT=0)
	static constexpr std::size_t def_text_red           = 0x1'0C; // mTextRed (float)
	static constexpr std::size_t def_text_green         = 0x1'10; // mTextGreen (float)
	static constexpr std::size_t def_text_blue          = 0x1'14; // mTextBlue (float)
	static constexpr std::size_t def_sel_text_red       = 0x1'28; // mSelectedTextRed (float)
	static constexpr std::size_t def_sel_text_green     = 0x1'2C; // mSelectedTextGreen (float)
	static constexpr std::size_t def_sel_text_blue      = 0x1'30; // mSelectedTextBlue (float)
	static constexpr std::size_t def_spacing    = 0x1'5C; // mSpacing (float) row pitch, read by UpdateScrollState
	static constexpr std::size_t def_fade_speed = 0x2'1C; // mFadeSpeed (float) opacity ease rate (component +0x2C4)

	// The ease rate the native option templates use. Text rows carry none of their own, so without this they would
	// never fade in.
	static constexpr float row_fade_speed = 10.0f;

	static constexpr std::size_t message_dialog_size          = 0x2'F0; // sizeof sgg::MessageDialog
	static constexpr std::size_t screen_manager_offset        = 0x48;   // sgg::GameScreen::mScreenManager
	static constexpr std::size_t screen_removed_offset        = 0x21;   // sgg::GameScreen::mRemoved (bool)
	static constexpr std::size_t screen_visible_offset        = 0x22;   // sgg::GameScreen::mIsVisible (bool)
	static constexpr std::size_t screen_block_input_offset    = 0x24;   // sgg::GameScreen::mBlockLowerInput (bool)
	static constexpr std::size_t dialog_title_offset          = 0x1'88; // sgg::MenuScreen::mTitleText
	static constexpr std::size_t dialog_confirm_button_offset = 0x1'A0; // sgg::MenuScreen::mConfirmButton
	static constexpr std::size_t dialog_message_offset        = 0x2'B0; // sgg::MessageDialog::mMessageText

	// MessageDialog.sjson MessageText uses FontSize 26.
	static constexpr std::size_t textbox_font_handle_offset        = 0x6'A4; // GUIComponentTextBox::mFontHandle
	static constexpr std::size_t font_handle_size_ratio_offset     = 0x0C;   // sgg::FontHandle::mFontSizeRatio
	static constexpr std::size_t font_handle_eng_size_ratio_offset = 0x10;   // sgg::FontHandle::mEnglishFontSizeRatio
	static constexpr float restart_message_font_scale              = 0.75f;  // ~26 -> ~19.5

	static constexpr std::uintptr_t anchor_rva              = 0x11'5C'70; // GUIComponentButton::GUIComponentButton
	static constexpr std::uintptr_t message_dialog_ctor_rva = 0x16'EE'60; // sgg::MessageDialog::MessageDialog
	static constexpr std::uintptr_t add_screen_rva          = 0x14'7D'D0; // sgg::ScreenManager::AddScreen

	static constexpr std::uintptr_t numbox_factory_rva = 0x17'A5'30;

	static constexpr std::uintptr_t push_back_rva = 0x14'1E'D0;

	static constexpr std::uintptr_t teleport_cursor_rva = 0x14'03'A0;

	// Config/control globals move with .data/.rdata, so resolve them by name.

	// sgg::GUIComponentNumBox, sizeof 0x5D0.
	static constexpr std::size_t numbox_value_offset         = 0x5'40; // mNumberValue (float)
	static constexpr std::size_t numbox_step_offset          = 0x5'44; // mNumberStepValue (float)
	static constexpr std::size_t numbox_min_offset           = 0x5'48; // mNumberMin (float)
	static constexpr std::size_t numbox_max_offset           = 0x5'4C; // mNumberMax (float)
	static constexpr std::size_t numbox_is_integer_offset    = 0x5'50; // mIsInteger (bool: discrete + integer display)
	static constexpr std::size_t numbox_disable_input_offset = 0x5'63; // mDisableInput (bool: HandleInput early-out)
	static constexpr std::size_t numbox_value_text_offset    = 0x5'B0; // mValueTextBox (GUIComponentTextBox*)
	static constexpr std::size_t numbox_anim_offset          = 0x5'90; // mAnim (GUIComponentAnimation*, box graphic)
	static constexpr std::size_t numbox_left_arrow_offset    = 0x5'98; // mLeftArrow (GUIComponentAnimation*)
	static constexpr std::size_t numbox_right_arrow_offset   = 0x5'A0; // mRightArrow (GUIComponentAnimation*)
	static constexpr std::size_t numbox_label_text_offset    = 0x5'A8; // mTextBox (GUIComponentTextBox*, the label)
	static constexpr std::size_t numbox_sizeof               = 0x5'D0;

	// Wider button boxes need the anim's mScaleX plus mScaleModifierOnlyX.
	static constexpr std::size_t button_anim_offset  = 0x5'70; // GUIComponentButton::mAnim (GUIComponentAnimation*)
	static constexpr std::size_t button_label_offset = 0x5'80; // GUIComponentButton::mLabel (GUIComponentTextBox*)

	static constexpr std::size_t anim_scale_modifier_only_x_offset = 0x5'42; // mScaleModifierOnlyX (bool)
	static constexpr std::size_t component_def_scale_x_offset      = 0x1'14; // mData.mDef.mScaleX (float)

	static constexpr std::size_t component_def_scale_y_offset = 0x1'18; // mData.mDef.mScaleY (float)

	// SearchInDirection adds FreeFormSelectOffset when evaluating candidates.
	static constexpr std::size_t component_free_form_offset_x_offset = 0x1'54;  // mFreeFormSelectOffsetX (float)
	static constexpr std::size_t component_free_form_offset_y_offset = 0x1'58;  // mFreeFormSelectOffsetY (float)
	static constexpr std::size_t component_auto_activate_offset      = 0x00'BC; // mAutoActivateWithGamepad (bool)

	// SearchInDirection reads mFreeFormSelectable before IsSelectable, but mouse hover does not.
	static constexpr std::size_t component_free_form_selectable_offset = 0x00'B1; // mData.mDef.mFreeFormSelectable (bool)

	static constexpr float button_graphic_native_width = 350.0f;

	static constexpr float button_label_capacity = 15.0f;
	static constexpr float button_label_padding  = 2.0f;

	// sgg::GUIComponentSlider is hand-built by DoShowCategory. The vtable RVA is a .rdata fallback.
	static constexpr std::uintptr_t slider_vtable_rva = 0x4D'8A'68;
	static constexpr std::size_t slider_sizeof        = 0x5'B0;
	static constexpr std::size_t image_sizeof         = 0x5'78;       // sgg::GUIComponentImage (mBacking/mFill)
	static constexpr std::size_t textbox_sizeof       = 0x6'C0;       // sgg::GUIComponentTextBox (mLabel/mValueTextBox)
	static constexpr std::size_t menu_screen_container_offset = 0x50; // owner + 0x50 = the IGUIComponentContainer base
	static constexpr std::size_t slider_parent_offset = 0x3'90; // GUIComponent::mParentContainer, SetParent writes

	static constexpr std::size_t slider_owner_offset      = 0x5'40; // mOwner (MenuScreen*)
	static constexpr std::size_t slider_on_changed_offset = 0x5'58; // mOnValueChanged (vector begin/end/cap, 3 qwords)
	static constexpr std::size_t slider_backing_offset    = 0x5'70; // mBacking (GUIComponentImage*, bar background)
	static constexpr std::size_t slider_fill_offset       = 0x5'78; // mFill (GUIComponentImage*, progress fill)
	static constexpr std::size_t slider_label_offset      = 0x5'90; // mLabel (GUIComponentTextBox*, left label)
	static constexpr std::size_t slider_value_text_offset = 0x5'98; // mValueTextBox (GUIComponentTextBox*, right value)
	static constexpr std::size_t slider_fraction_offset   = 0x5'A4; // mFraction (float, normalized 0..1 value)

	// GUIComponentSlider's focus look tracks mFocused directly.
	static constexpr std::size_t slider_focused_offset = 0x5'48; // GUIComponentSlider::mFocused (bool)

	// Matches the native num-box repeat timings.
	static constexpr float slider_repeat_delay                  = 0.6f;
	static constexpr float slider_repeat_interval               = 0.05f;
	static constexpr std::size_t textbox_use_selected_color_off = 0x5'52;  // GUIComponentTextBox::mUseSelectedTextColor
	static constexpr std::size_t vtable_on_mouse_off_offset     = 0x00'60; // GUIComponent::OnMouseOff slot
	static constexpr std::size_t vtable_on_unselected_offset    = 0x00'88; // GUIComponent::OnUnselected slot
	static constexpr std::size_t vtable_on_focus_off_offset     = 0x1'18;  // GUIComponent::OnFocusOff slot
	static constexpr std::size_t vtable_set_location_offset = 0x1'80; // GUIComponent::SetLocation slot (moves the component and its children)

	// Button-style def greying does not reach slider/num-box child text or graphics.
	static constexpr std::size_t textbox_use_disabled_color_off = 0x5'53;  // GUIComponentTextBox::mUseDisabledTextColor
	static constexpr std::size_t textbox_text_red               = 0x1'B4;  // mData.mDef.mTextRed (float)
	static constexpr std::size_t textbox_selected_text_red      = 0x1'D0;  // mData.mDef.mSelectedTextRed (float)
	static constexpr std::size_t textbox_disabled_text_red      = 0x1'E8;  // mData.mDef.mDisabledTextRed (float)
	static constexpr std::size_t textbox_disabled_text_green    = 0x1'EC;  // mDisabledTextGreen (float)
	static constexpr std::size_t textbox_disabled_text_blue     = 0x1'F0;  // mDisabledTextBlue (float)
	static constexpr std::size_t textbox_disabled_text_alpha    = 0x1'F4;  // mDisabledTextAlpha (float)
	static constexpr std::size_t image_color_offset             = 0x5'44;  // GUIComponentImage::mColor (packed RGBA)
	static constexpr std::size_t image_color_target_offset      = 0x00'78; // mColorTarget (packed RGBA)
	static constexpr std::size_t button_graphic_color_offset = 0x5'5C; // GUIComponentButton::mButtonColor - the colour Draw paints the toggle graphic with
	static constexpr std::size_t component_color_target_offset = 0x00'78; // GUIComponent::mColorTarget (Update eases mButtonColor toward this)
	static constexpr std::size_t def_sel_red = 0xFC; // ComponentDataDef::mSelectedRed - set <0 to disable the selected-colour override in Draw/On(Un)Selected
	static constexpr float disabled_text_grey            = 0.22f; // matches set_def_text_grey (toggle/text rows)
	static constexpr std::uint32_t disabled_graphic_grey = 0xFF'66'66'66; // opaque 0.4 grey (packed A,B,G,R)
	// Still-selectable greyed labels need SetTextColor because the template caches a bright colour.
	static constexpr std::size_t vtable_set_text_color_offset = 0x1'60;
	static constexpr std::uint32_t disabled_label_grey_packed = 0xFF'38'38'38;

	static constexpr std::size_t animation_color_offset  = 0x5'58;        // GUIComponentAnimation::mColor (packed ARGB)
	static constexpr std::uint32_t numbox_hover_bg_black = 0xFF'00'00'00; // the num-box's hovered/selected box colour

	static constexpr std::size_t vtable_deleting_dtor_offset = 0x1'88;

	using ctor_fn                = void* (*)(void* button, void* owner_screen);
	using push_back_fn           = void (*)(void* vector, GUIComponent** value);
	using apply_data_fn          = void (*)(void* menu_screen, GUIComponent* component);
	using set_label_fn           = void (*)(void* button, const char* text);
	using update_scroll_fn       = void (*)(void* misc_settings_screen);
	using set_animation_fn       = void (*)(void* button, std::uint32_t graphic_id);
	using setup_component_fn     = void (*)(void* component, void* component_data);
	using set_texture_fn         = void (*)(void* button, std::uint32_t graphic_id, bool reset);
	using set_sel_texture_fn     = void (*)(void* button, std::uint32_t graphic_id);
	using disable_fn             = void (*)(void* button);
	using was_key_pressed_fn     = bool (*)(void* input_handler, int keyboard_button_id);
	using dtor_fn                = void (*)(void* button);
	using message_dialog_ctor_fn = void* (*)(void* self, void* screen_manager, void* eastl_message);
	using add_screen_fn          = void (*)(void* screen_manager, void* screen, bool add_at_end, void* eastl_name);
	using show_text_fn           = void (*)(void* text_box, const char* text);
	using get_lines_fn           = void* (*)(void* text_box);
	using numbox_factory_fn      = void* (*)(const char* file, int line, const char* tag, void** screen);
	using numbox_set_range_fn    = void (*)(void* num_box, float min, float max);
	using numbox_set_value_fn    = void (*)(void* num_box, float value, bool notify);

	// GUIComponent-derived constructors take Vec2 by value in one 64-bit register.
	using gui_component_ctor_fn  = void (*)(void* self, std::uint64_t location_packed);
	using slider_defaults_fn     = void (*)(void* slider);
	using slider_set_fraction_fn = void (*)(void* slider, float fraction, bool notify);
	using teleport_cursor_fn     = void (*)(void* menu_screen, GUIComponent* component);
	using set_mouse_over_fn      = void (*)(void* menu_screen, GUIComponent* component);
	using component_focused_fn   = void (*)(void* misc_settings_screen, GUIComponent* component);
	using input_get_state_fn     = std::uint32_t (*)(void* input_handler, const void* remappable_control);
	using mouse_button_down_fn   = bool (*)(void* input_handler);
	using input_dir_pressed_fn   = bool (*)(void* input_handler);

#pragma endregion

#pragma region Native bindings, panel model, and menu state

	struct HashGuid
	{
		std::uint32_t m_id;
	};

	using hash_lookup_fn = HashGuid* (*)(HashGuid * out, const char* str, std::size_t len);

	// SaveProfile is called synchronous so native settings are written before a forced restart.
	using save_profile_fn = char (*)(void* profile_name, bool show_spinner, bool async);

	static ctor_fn g_button_ctor                        = nullptr;
	static push_back_fn g_push_back                     = nullptr;
	static apply_data_fn g_apply_data                   = nullptr;
	static set_label_fn g_set_label                     = nullptr;
	static update_scroll_fn g_update_scroll             = nullptr;
	static set_animation_fn g_set_animation             = nullptr;
	static hash_lookup_fn g_hash_lookup                 = nullptr;
	static setup_component_fn g_setup_component         = nullptr;
	static set_texture_fn g_set_normal_texture          = nullptr;
	static set_sel_texture_fn g_set_selected_texture    = nullptr;
	static dtor_fn g_button_dtor                        = nullptr;
	static disable_fn g_disable                         = nullptr;
	static was_key_pressed_fn g_was_key_pressed         = nullptr;
	static message_dialog_ctor_fn g_message_dialog_ctor = nullptr;
	static add_screen_fn g_add_screen                   = nullptr;
	static show_text_fn g_show_text                     = nullptr;
	static get_lines_fn g_get_lines                     = nullptr;
	static numbox_factory_fn g_numbox_factory           = nullptr;
	static numbox_set_range_fn g_numbox_set_range       = nullptr;
	static numbox_set_value_fn g_numbox_set_value       = nullptr;
	static gui_component_ctor_fn g_gui_component_ctor   = nullptr;
	static gui_component_ctor_fn g_image_ctor           = nullptr;
	static gui_component_ctor_fn g_textbox_ctor         = nullptr;
	static slider_defaults_fn g_slider_defaults         = nullptr;
	static slider_set_fraction_fn g_slider_set_fraction = nullptr;
	static std::uintptr_t g_slider_vtable               = 0;
	// Native slider GetArea unions its sub-components into a screen-spanning rect.
	static constexpr std::size_t slider_vtable_slot_count = 128;
	static_assert(0x1'80 / sizeof(std::uintptr_t) < slider_vtable_slot_count, "vtable copy buffer too small for the highest patched slot");
	static std::uintptr_t g_slider_vtable_copy[slider_vtable_slot_count] = {};
	static std::uintptr_t g_slider_vtable_patched                        = 0;

	// Centre-column action buttons need a wide one-row GetArea for vertical spatial nav.
	static std::uintptr_t g_button_vtable_copy[slider_vtable_slot_count] = {};
	static std::uintptr_t g_button_vtable_patched                        = 0;
	static teleport_cursor_fn g_teleport_cursor                          = nullptr;
	static set_mouse_over_fn g_set_mouse_over                            = nullptr;
	static const bool* g_use_mouse                                       = nullptr;
	static const char* g_config_language                                 = nullptr;

	static component_focused_fn g_component_focused       = nullptr;
	static input_get_state_fn g_input_get_state           = nullptr;
	static mouse_button_down_fn g_mouse_button_down       = nullptr;
	static input_dir_pressed_fn g_input_was_left_pressed  = nullptr;
	static input_dir_pressed_fn g_input_was_right_pressed = nullptr;
	static input_dir_pressed_fn g_input_is_left_pressed   = nullptr;
	static input_dir_pressed_fn g_input_is_right_pressed  = nullptr;
	static const void* g_controls_cancel                  = nullptr;
	static const void* g_controls_select                  = nullptr;
	static save_profile_fn g_save_profile                 = nullptr;
	static void* g_active_profile                         = nullptr;

	static bool g_feature_enabled = false;

	// sgg::KeyboardButtonId values, validated in the PDB.
	static constexpr int key_escape   = 0;
	static constexpr int key_kp_enter = 113;
	static constexpr int key_return   = 127;

	static std::uint32_t g_blank_graphic = 0;

	// Native 1080p menu coordinates. UpdateScrollState uses row_base_y and row_pitch for page layout.
	static constexpr float row_location_x        = 1560.0f; // component X (right pane), like OptionToggleButton
	static constexpr float row_text_offset_x     = -900.0f; // left-justify the label to the option-name column
	static constexpr float value_text_offset_x   = 15.0f; // right-justify the value, aligning it with the toggle column
	static constexpr float numbox_location_x     = 1365.0f; // native OptionNumBox X (box + arrows clear the scrollbar)
	static constexpr float slider_location_x     = 1330.0f; // native OptionSlider X (bar + value clear the scrollbar)
	static constexpr float button_center_x       = 1130.0f; // centered action button X (clear of the scrollbar)
	static constexpr float row_base_y            = 300.0f;  // first row's Y - matches the vanilla option templates
	static constexpr float row_pitch             = 45.0f;   // vertical distance between rows (vanilla Spacing = 45)
	static constexpr std::uint32_t rows_per_page = 10;      // vanilla ItemsPerPage = 10

	static constexpr float button_extra_lead  = 14.0f;
	static constexpr float button_extra_trail = 14.0f;

	static const std::string root_section = "config";

	// Chalk writes this placeholder per section so empty groups persist.
	static constexpr const char* section_empty_key = "...";

	// Approximate right-column value width in glyph weights.
	static constexpr float value_display_max_width = 30.0f;

	static constexpr std::uint64_t edit_cursor_blink_ms = 500;

	enum class RowKind
	{
		mod_entry,
		group,
		setting,
		action,
		info,
	};

	struct PanelRow
	{
		GUIComponent* component = nullptr;
		RowKind kind            = RowKind::mod_entry;
		std::string stem;
		std::string setting_key;

		toml_v2::config_file::config_entry_base* entry = nullptr;

		bool disabled          = false;
		bool is_enabled_toggle = false;
		bool is_virtual_input  = false;
		bool is_toggle         = false;

		// Fallback flip state when a virtual toggle's get() returns nil.
		bool toggle_value = false;

		std::string description;

		// Mirrors component position each frame.
		GUIComponent* value_component = nullptr;

		bool is_slider      = false;
		bool is_stepper     = false;
		double stepper_min  = 0.0;
		double stepper_max  = 0.0;
		double stepper_step = 1.0;

		bool show_as_percentage = false;
		bool is_percentage      = false;

		bool is_enum = false;
		std::vector<std::string> enum_values;
		std::vector<std::string> enum_labels;

		std::string target_section;

		// Real config section for virtual-row Lua I/O, which may differ from the view path.
		std::string config_section;
	};

	static std::vector<PanelRow> g_rows;

	static bool g_restart_required = false;

	// Restart-causing changes keyed by setting, so re-editing overwrites its popup line.
	static std::map<std::string, std::string> g_restart_changes;

	static std::map<std::string, std::string> g_restart_baselines;

	static GUIComponent* g_restart_confirm_button = nullptr;

	static void* g_restart_dialog = nullptr;

	static bool g_restart_prompt_shown = false;

	enum class View
	{
		mod_list,
		mod_settings,
	};

	static View g_view = View::mod_list;
	static std::string g_view_stem;
	static std::string g_view_section;
	static bool g_nav_pending  = false;
	static View g_pending_view = View::mod_list;
	static std::string g_pending_stem;
	static std::string g_pending_section;

	// Stable row identity for matching after a rebuild frees components.
	struct RowIdentity
	{
		bool valid = false;
		RowKind kind;
		std::string stem;
		std::string section;
		std::string key;
		std::string config_section;
	};

	static RowIdentity g_keep_active_row;

	// Native hover can resolve the stationary cursor a frame late after a rebuild.
	static constexpr int keep_active_frame_count = 3;
	static int g_keep_active_frames              = 0;

	static constexpr float dynamic_refresh_settle_seconds = 0.15f;

	// Sliders fire every frame while dragged, so rebuild only after a quiet gap.
	static float g_dynamic_refresh_settle = 0.0f;

	// Restore stack for backing out without losing scroll or focus.
	struct NavRestore
	{
		std::uint32_t scroll_index = 0;
		std::string focus_stem;
		std::string focus_section;
	};

	static std::vector<NavRestore> g_nav_stack;
	static NavRestore g_pending_restore;
	static bool g_has_pending_restore = false;

	// Typed input is captured in the window procedure and applied on the game thread.
	static bool g_editing                                        = false;
	static GUIComponent* g_edit_component                        = nullptr;
	static toml_v2::config_file::config_entry_base* g_edit_entry = nullptr;

	static std::string g_edit_buffer;
	static std::size_t g_edit_cursor = 0;
	static bool g_edit_numeric       = false;
	static bool g_edit_confirm       = false;
	static bool g_edit_cancel        = false;

	static std::string key_to_display(const std::string& key);

#pragma endregion

#pragma region Mod identity, text, and Mods-tab helpers

	static std::string display_name_from_stem(const std::string& stem)
	{
		const auto dash        = stem.find('-');
		const std::string name = (dash == std::string::npos) ? stem : stem.substr(dash + 1);
		return key_to_display(name);
	}

	static std::string mod_description_from_stem(const std::string& stem)
	{
		if (!big::g_lua_manager)
		{
			return {};
		}
		std::scoped_lock guard(big::g_lua_manager->m_module_lock);
		for (const auto& module : big::g_lua_manager->m_modules)
		{
			if (module && module->guid() == stem)
			{
				return module->manifest().description;
			}
		}
		return {};
	}

	static std::string opt_out_note()
	{
		return "This mod opted out of the in-game settings menu. Check the mod page for how to "
		       "configure it, if applicable.";
	}

	static std::string resolve_localized(const localized_text& t);

	static std::string opt_out_description(const std::string& stem)
	{
		const std::string custom = resolve_localized(mod_opt_out_description(stem));
		return !custom.empty() ? custom : opt_out_note();
	}

	// Parse treats backslash and square brackets as markup. Backslash must be escaped first.
	static std::string escape_markup(const std::string& text)
	{
		std::string out;
		out.reserve(text.size() + 8);
		for (char c : text)
		{
			if (c == '\\' || c == '[' || c == ']')
			{
				out.push_back('\\');
			}
			out.push_back(c);
		}
		return out;
	}

	static int compare_display_names(const std::string& a, const std::string& b)
	{
		std::size_t i = 0;
		std::size_t j = 0;
		while (i < a.size() && j < b.size())
		{
			const unsigned char ra = static_cast<unsigned char>(a[i]);
			const unsigned char rb = static_cast<unsigned char>(b[j]);
			if (ra >= '0' && ra <= '9' && rb >= '0' && rb <= '9')
			{
				std::size_t ea = i;
				while (ea < a.size() && a[ea] >= '0' && a[ea] <= '9')
				{
					++ea;
				}
				std::size_t eb = j;
				while (eb < b.size() && b[eb] >= '0' && b[eb] <= '9')
				{
					++eb;
				}
				std::size_t sa = i;
				while (sa + 1 < ea && a[sa] == '0')
				{
					++sa;
				}
				std::size_t sb = j;
				while (sb + 1 < eb && b[sb] == '0')
				{
					++sb;
				}
				const std::size_t la = ea - sa;
				const std::size_t lb = eb - sb;
				if (la != lb)
				{
					return la < lb ? -1 : 1;
				}
				const int cmp = a.compare(sa, la, b, sb, lb);
				if (cmp != 0)
				{
					return cmp < 0 ? -1 : 1;
				}
				i = ea;
				j = eb;
				continue;
			}

			unsigned char ca = ra;
			unsigned char cb = rb;
			if (ca >= 'A' && ca <= 'Z')
			{
				ca = static_cast<unsigned char>(ca + ('a' - 'A'));
			}
			if (cb >= 'A' && cb <= 'Z')
			{
				cb = static_cast<unsigned char>(cb + ('a' - 'A'));
			}
			if (ca != cb)
			{
				return ca < cb ? -1 : 1;
			}
			++i;
			++j;
		}
		const std::size_t ra = a.size() - i;
		const std::size_t rb = b.size() - j;
		if (ra == rb)
		{
			return 0;
		}
		return ra < rb ? -1 : 1;
	}

	static float glyph_weight(unsigned char c)
	{
		if (c >= 0xC0)
		{
			return 1.0f;
		}
		if (c >= 0x80)
		{
			return 0.0f;
		}
		switch (c)
		{
		case ' ':
		case '!':
		case '\'':
		case ',':
		case '.':
		case ':':
		case ';':
		case '|':
		case '`':
		case '(':
		case ')':
		case '[':
		case ']':
		case '{':
		case '}':
		case 'i':
		case 'j':
		case 'l':
		case 'I':
		case 'f':
		case 't':
		case 'r':  return 0.5f;
		case 'm':
		case 'w':
		case 'M':
		case 'W':
		case '@':
		case '%':  return 1.5f;
		default:   return 1.0f;
		}
	}

	static float measure_width(const std::string& s)
	{
		float w = 0.0f;
		for (char c : s)
		{
			w += glyph_weight(static_cast<unsigned char>(c));
		}
		return w;
	}

	static bool is_word_byte(char c)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		return (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u == '_' || u >= 0x80;
	}

	static std::size_t caret_prev(const std::string& s, std::size_t pos)
	{
		if (pos == 0)
		{
			return 0;
		}
		--pos;
		while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
		{
			--pos;
		}
		return pos;
	}

	static std::size_t caret_next(const std::string& s, std::size_t pos)
	{
		if (pos >= s.size())
		{
			return s.size();
		}
		++pos;
		while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
		{
			++pos;
		}
		return pos;
	}

	static std::size_t caret_prev_word(const std::string& s, std::size_t pos)
	{
		while (pos > 0 && !is_word_byte(s[pos - 1]))
		{
			--pos;
		}
		while (pos > 0 && is_word_byte(s[pos - 1]))
		{
			--pos;
		}
		return pos;
	}

	static std::size_t caret_next_word(const std::string& s, std::size_t pos)
	{
		const std::size_t n = s.size();
		while (pos < n && is_word_byte(s[pos]))
		{
			++pos;
		}
		while (pos < n && !is_word_byte(s[pos]))
		{
			++pos;
		}
		return pos;
	}

	static std::string truncate_value(const std::string& text)
	{
		if (measure_width(text) <= value_display_max_width)
		{
			return text;
		}
		const float avail = value_display_max_width - measure_width("...");
		std::size_t start = text.size();
		float used        = 0.0f;
		while (start > 0)
		{
			const std::size_t prev = caret_prev(text, start);
			const float w          = measure_width(text.substr(prev, start - prev));
			if (used + w > avail)
			{
				break;
			}
			used  += w;
			start  = prev;
		}
		return "..." + text.substr(start);
	}

	static GUIComponent* mods_category_button(MiscSettingsScreen* screen)
	{
		return reinterpret_cast<GUIComponent*>(screen->m_editor_options_button);
	}

	static void show_mods_tab(MiscSettingsScreen* screen)
	{
		auto* button = mods_category_button(screen);
		if (!button)
		{
			return;
		}

		button->m_hidden     = false;
		button->m_is_useable = true;

		if (g_hash_lookup)
		{
			HashGuid id{};
			g_hash_lookup(&id, "Mods", 4);
			*reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(button) + sgg::gui_component_button_display_name_id_offset) = id.m_id;
		}

		if (g_set_label)
		{
			g_set_label(button, "Mods");
		}
	}

#pragma endregion

#pragma region Native row construction and styling

	static void set_sso_string(void* field, const char* text)
	{
		char* bytes   = static_cast<char*>(field);
		std::size_t n = std::strlen(text);
		if (n > 22)
		{
			n = 22;
		}
		std::memset(bytes, 0, 24);
		std::memcpy(bytes, text, n);
		bytes[0x17] = static_cast<char>(0x17 - n);
	}

	// Engine-owned GUI objects must use the game's CRT heap, not H2M's /MT CRT.
	using aligned_malloc_fn = void*(__cdecl*)(std::size_t, std::size_t);
	using aligned_free_fn   = void(__cdecl*)(void*);

	static aligned_malloc_fn g_game_aligned_malloc = nullptr;
	static aligned_free_fn g_game_aligned_free     = nullptr;

	static void* game_alloc(std::size_t size)
	{
		return g_game_aligned_malloc ? g_game_aligned_malloc(size, 8) : nullptr;
	}

	static void game_free(void* block)
	{
		if (block && g_game_aligned_free)
		{
			g_game_aligned_free(block);
		}
	}

	static GUIComponent* create_button(MiscSettingsScreen* screen)
	{
		if (!g_button_ctor || !g_push_back || !g_apply_data)
		{
			return nullptr;
		}

		auto* row = static_cast<GUIComponent*>(game_alloc(sgg::gui_component_button_size));
		if (!row)
		{
			return nullptr;
		}

		g_button_ctor(row, screen);
		*reinterpret_cast<void**>(reinterpret_cast<char*>(row) + sgg::gui_component_button_owner_offset) = screen;
		return row;
	}

	// Off-page rows start transparent to avoid flashing at the top.
	static void finalize_row(MiscSettingsScreen* screen, GUIComponent* row, bool in_options = true)
	{
		GUIComponent* value = row;
		auto* menu          = reinterpret_cast<MenuScreen*>(screen);
		g_push_back(&menu->m_components, &value);
		if (in_options)
		{
			g_push_back(&screen->m_options, &value);
		}

		row->m_location_x   = row_location_x;
		row->m_fade_opacity = 0.0f;

		*reinterpret_cast<float*>(reinterpret_cast<char*>(row) + component_def_offset + def_fade_speed) = row_fade_speed;
	}

	static void set_toggle_graphic(GUIComponent* row, bool is_on)
	{
		if (!g_set_normal_texture)
		{
			return;
		}
		char* def                    = reinterpret_cast<char*>(row) + component_def_offset;
		const std::uint32_t on_hash  = *reinterpret_cast<std::uint32_t*>(def + def_graphic);
		const std::uint32_t off_hash = *reinterpret_cast<std::uint32_t*>(def + def_alternate_graphic);
		g_set_normal_texture(row, is_on ? on_hash : off_hash, false);
	}

	// Copy the produced value's cue into mPressSound for vanilla toggle audio.
	static void stage_toggle_press_sound(GUIComponent* row, bool new_value)
	{
		char* def             = reinterpret_cast<char*>(row) + component_def_offset;
		const std::size_t src = new_value ? def_toggle_on_sound : def_toggle_off_sound;
		std::memcpy(def + def_press_sound, def + src, sound_cue_size);
	}

	// Must run before SetupComponent so the text box receives the greyed colours.
	static void set_def_text_grey(GUIComponent* row)
	{
		char* def                                           = reinterpret_cast<char*>(row) + component_def_offset;
		constexpr float grey                                = disabled_text_grey;
		*reinterpret_cast<float*>(def + def_text_red)       = grey;
		*reinterpret_cast<float*>(def + def_text_green)     = grey;
		*reinterpret_cast<float*>(def + def_text_blue)      = grey;
		*reinterpret_cast<float*>(def + def_sel_text_red)   = grey;
		*reinterpret_cast<float*>(def + def_sel_text_green) = grey;
		*reinterpret_cast<float*>(def + def_sel_text_blue)  = grey;
	}

	static void grey_text_box(void* text_box)
	{
		if (!text_box)
		{
			return;
		}
		char* b                                                      = static_cast<char*>(text_box);
		*reinterpret_cast<float*>(b + textbox_disabled_text_red)     = disabled_text_grey;
		*reinterpret_cast<float*>(b + textbox_disabled_text_green)   = disabled_text_grey;
		*reinterpret_cast<float*>(b + textbox_disabled_text_blue)    = disabled_text_grey;
		*reinterpret_cast<float*>(b + textbox_disabled_text_alpha)   = 1.0f;
		*reinterpret_cast<bool*>(b + textbox_use_disabled_color_off) = true;

		// Still-selectable greyed rows keep mIsUseable=1.
		for (const std::size_t base : {textbox_text_red, textbox_selected_text_red})
		{
			*reinterpret_cast<float*>(b + base + 0x0) = disabled_text_grey;
			*reinterpret_cast<float*>(b + base + 0x4) = disabled_text_grey;
			*reinterpret_cast<float*>(b + base + 0x8) = disabled_text_grey;
		}
	}

	// mColorTarget must be written too or the per-frame lerp undoes the grey.
	static void grey_image(void* image)
	{
		if (!image)
		{
			return;
		}
		char* b                                                          = static_cast<char*>(image);
		*reinterpret_cast<std::uint32_t*>(b + image_color_offset)        = disabled_graphic_grey;
		*reinterpret_cast<std::uint32_t*>(b + image_color_target_offset) = disabled_graphic_grey;
	}

	static void grey_toggle_graphic(GUIComponent* row)
	{
		char* b                                                              = reinterpret_cast<char*>(row);
		*reinterpret_cast<std::uint32_t*>(b + button_graphic_color_offset)   = disabled_graphic_grey;
		*reinterpret_cast<std::uint32_t*>(b + component_color_target_offset) = disabled_graphic_grey;
		*reinterpret_cast<float*>(b + component_def_offset + def_sel_red)    = -1.0f;
	}

	static void set_def_text_normal(GUIComponent* row, bool also_selected = false)
	{
		char* def                                       = reinterpret_cast<char*>(row) + component_def_offset;
		constexpr float option_grey                     = 0.55f;
		*reinterpret_cast<float*>(def + def_text_red)   = option_grey;
		*reinterpret_cast<float*>(def + def_text_green) = option_grey;
		*reinterpret_cast<float*>(def + def_text_blue)  = option_grey;
		if (also_selected)
		{
			*reinterpret_cast<float*>(def + def_sel_text_red)   = option_grey;
			*reinterpret_cast<float*>(def + def_sel_text_green) = option_grey;
			*reinterpret_cast<float*>(def + def_sel_text_blue)  = option_grey;
		}
	}

	static GUIComponent* make_text_row(MiscSettingsScreen* screen, const char* label, bool disabled = false, bool block_input = true, bool no_hover_highlight = false)
	{
		auto* row = create_button(screen);
		if (!row)
		{
			return nullptr;
		}
		auto* row_bytes = reinterpret_cast<char*>(row);

		set_sso_string(row_bytes + gui_component_name_offset, "CategoryOptionsButton");
		g_apply_data(reinterpret_cast<MenuScreen*>(screen), row);

		char* def                                                      = row_bytes + component_def_offset;
		*reinterpret_cast<std::uint8_t*>(def + def_add_text_area)      = 1;
		*reinterpret_cast<std::uint8_t*>(def + def_use_text_area)      = 0;
		*reinterpret_cast<std::uint32_t*>(def + def_graphic)           = 0;
		*reinterpret_cast<std::uint32_t*>(def + def_selected_graphic)  = 0;
		*reinterpret_cast<std::uint32_t*>(def + def_alternate_graphic) = 0;
		*reinterpret_cast<float*>(def + def_width)                     = 0.0f;
		*reinterpret_cast<float*>(def + def_height)                    = 0.0f;
		*reinterpret_cast<std::uint8_t*>(def + def_text_justification) = 0;
		*reinterpret_cast<float*>(def + def_text_offset_x)             = row_text_offset_x;
		*reinterpret_cast<float*>(def + def_y)                         = row_base_y;
		*reinterpret_cast<float*>(def + def_spacing)                   = row_pitch;

		if (disabled)
		{
			set_def_text_grey(row);
		}
		else
		{
			set_def_text_normal(row, no_hover_highlight);
		}

		if (g_setup_component)
		{
			g_setup_component(row, row_bytes + component_data_offset);
		}

		// SetupComponent does not clear textures a prior template already set.
		if (g_set_normal_texture)
		{
			g_set_normal_texture(row, 0, false);
		}
		if (g_set_selected_texture)
		{
			g_set_selected_texture(row, 0);
		}
		if (g_set_animation && g_blank_graphic)
		{
			g_set_animation(row, g_blank_graphic);
		}

		if (g_set_label)
		{
			g_set_label(row, label);
		}

		if (disabled && block_input && g_disable)
		{
			g_disable(row);
		}

		finalize_row(screen, row);
		return row;
	}

	static GUIComponent* make_toggle_row(MiscSettingsScreen* screen, const char* label, bool is_on, bool disabled = false, bool block_input = true)
	{
		auto* row = create_button(screen);
		if (!row)
		{
			return nullptr;
		}
		auto* row_bytes = reinterpret_cast<char*>(row);

		set_sso_string(row_bytes + gui_component_name_offset, "OptionToggleButton");
		g_apply_data(reinterpret_cast<MenuScreen*>(screen), row);

		char* def                                    = row_bytes + component_def_offset;
		*reinterpret_cast<float*>(def + def_y)       = row_base_y;
		*reinterpret_cast<float*>(def + def_spacing) = row_pitch;

		if (disabled)
		{
			set_def_text_grey(row);
			*reinterpret_cast<std::uint8_t*>(def + def_add_color) = 0;
			*reinterpret_cast<float*>(def + def_red)              = 0.4f;
			*reinterpret_cast<float*>(def + def_green)            = 0.4f;
			*reinterpret_cast<float*>(def + def_blue)             = 0.4f;
			if (g_setup_component)
			{
				g_setup_component(row, row_bytes + component_data_offset);
			}
		}

		if (g_set_label)
		{
			g_set_label(row, label);
		}

		set_toggle_graphic(row, is_on);

		if (disabled)
		{
			grey_toggle_graphic(reinterpret_cast<GUIComponent*>(row));

			if (block_input && g_disable)
			{
				g_disable(row);
			}
		}

		finalize_row(screen, row);
		return row;
	}

	static void install_wide_button_nav_rect(GUIComponent* row);

	static GUIComponent* make_button_row(MiscSettingsScreen* screen, const char* label, bool disabled = false, bool block_input = true)
	{
		auto* row = create_button(screen);
		if (!row)
		{
			return nullptr;
		}
		auto* row_bytes = reinterpret_cast<char*>(row);

		set_sso_string(row_bytes + gui_component_name_offset, "CategoryOptionsButton");
		g_apply_data(reinterpret_cast<MenuScreen*>(screen), row);

		const float box_scale_x = std::max(1.0f, (measure_width(label) + button_label_padding) / button_label_capacity);

		constexpr float button_scale = 0.8f;

		char* def                                     = row_bytes + component_def_offset;
		*reinterpret_cast<float*>(def + def_y)        = row_base_y;
		*reinterpret_cast<float*>(def + def_spacing)  = row_pitch;
		*reinterpret_cast<float*>(def + def_offset_y) = 0.0f;
		*reinterpret_cast<float*>(def + def_scale)    = button_scale;

		// GetArea multiplies mCustomWidth by mScale@0x38 and mScaleX@0x114.
		*reinterpret_cast<float*>(def + def_width)  = button_graphic_native_width;
		*reinterpret_cast<float*>(def + def_height) = 58.0f;

		*reinterpret_cast<bool*>(def + def_deselect_on_mouse_off) = true;

		if (disabled)
		{
			set_def_text_grey(row);
		}

		if (g_setup_component)
		{
			g_setup_component(row, row_bytes + component_data_offset);
		}

		// The child label has its own def mWidth, which controls wrapping.
		if (auto* label_box = *reinterpret_cast<char**>(row_bytes + button_label_offset))
		{
			*reinterpret_cast<float*>(label_box + component_def_offset + def_width) = button_graphic_native_width * box_scale_x;
		}

		if (g_set_label)
		{
			g_set_label(row, label);
		}

		if (box_scale_x > 1.0f)
		{
			*reinterpret_cast<float*>(row_bytes + component_def_scale_x_offset) = box_scale_x;
			*reinterpret_cast<float*>(row_bytes + component_def_scale_y_offset) = 1.0f;

			if (void* anim = *reinterpret_cast<void**>(row_bytes + button_anim_offset); anim)
			{
				char* anim_bytes = reinterpret_cast<char*>(anim);
				*reinterpret_cast<bool*>(anim_bytes + anim_scale_modifier_only_x_offset) = true;
				*reinterpret_cast<float*>(anim_bytes + component_def_scale_x_offset)     = box_scale_x;
				*reinterpret_cast<float*>(anim_bytes + component_def_scale_y_offset)     = 1.0f;
			}
		}

		if (disabled && block_input && g_disable)
		{
			g_disable(row);
		}

		// CategoryOptionsButton leaves mFreeFormSelectable unset.
		if (!disabled)
		{
			*reinterpret_cast<bool*>(row_bytes + component_free_form_selectable_offset) = true;
			install_wide_button_nav_rect(row);
		}

		finalize_row(screen, row);

		row->m_location_x = button_center_x;
		return row;
	}

	static GUIComponent* make_value_display(MiscSettingsScreen* screen, const char* text, bool disabled)
	{
		auto* row = create_button(screen);
		if (!row)
		{
			return nullptr;
		}
		auto* row_bytes = reinterpret_cast<char*>(row);

		set_sso_string(row_bytes + gui_component_name_offset, "CategoryOptionsButton");
		g_apply_data(reinterpret_cast<MenuScreen*>(screen), row);

		char* def                                                      = row_bytes + component_def_offset;
		*reinterpret_cast<std::uint8_t*>(def + def_add_text_area)      = 0;
		*reinterpret_cast<std::uint8_t*>(def + def_use_text_area)      = 0;
		*reinterpret_cast<std::uint32_t*>(def + def_graphic)           = 0;
		*reinterpret_cast<std::uint32_t*>(def + def_selected_graphic)  = 0;
		*reinterpret_cast<std::uint32_t*>(def + def_alternate_graphic) = 0;
		*reinterpret_cast<float*>(def + def_width)                     = 0.0f;
		*reinterpret_cast<float*>(def + def_height)                    = 0.0f;
		*reinterpret_cast<std::uint8_t*>(def + def_text_justification) = 1;
		*reinterpret_cast<float*>(def + def_text_offset_x)             = value_text_offset_x;
		*reinterpret_cast<float*>(def + def_y)                         = row_base_y;
		*reinterpret_cast<float*>(def + def_spacing)                   = row_pitch;

		if (disabled)
		{
			set_def_text_grey(row);
		}
		else
		{
			set_def_text_normal(row);
		}

		if (g_setup_component)
		{
			g_setup_component(row, row_bytes + component_data_offset);
		}
		if (g_set_normal_texture)
		{
			g_set_normal_texture(row, 0, false);
		}
		if (g_set_selected_texture)
		{
			g_set_selected_texture(row, 0);
		}
		if (g_set_animation && g_blank_graphic)
		{
			g_set_animation(row, g_blank_graphic);
		}
		if (g_set_label)
		{
			g_set_label(row, text);
		}

		row->m_can_be_focused = false;

		finalize_row(screen, row, false);
		return row;
	}

	static bool is_whole(double v)
	{
		return std::isfinite(v) && v == std::floor(v);
	}

	static void set_numbox_value_text(GUIComponent* numbox, const char* text)
	{
		if (!g_show_text || !numbox)
		{
			return;
		}
		if (void* value_tb = *reinterpret_cast<void**>(reinterpret_cast<char*>(numbox) + numbox_value_text_offset))
		{
			g_show_text(value_tb, escape_markup(text).c_str());
		}
	}

	static GUIComponent* make_numbox_row(MiscSettingsScreen* screen, const char* label, double min_v, double max_v, double step_v, double initial, bool disabled, const std::vector<std::string>* value_labels = nullptr, bool block_input = true)
	{
		if (!g_numbox_factory || !g_numbox_set_range || !g_numbox_set_value || !g_apply_data || !g_show_text)
		{
			return nullptr;
		}

		void* scr = screen;
		auto* nb  = static_cast<GUIComponent*>(g_numbox_factory("h2m", 0, "h2m::NumBox", &scr));
		if (!nb)
		{
			return nullptr;
		}
		char* nb_bytes = reinterpret_cast<char*>(nb);

		set_sso_string(nb_bytes + gui_component_name_offset, "OptionNumBox");
		if (void* value_tb = *reinterpret_cast<void**>(nb_bytes + numbox_value_text_offset))
		{
			set_sso_string(static_cast<char*>(value_tb) + gui_component_name_offset, "OptionNumBoxValueText");
		}
		if (void* left_arrow = *reinterpret_cast<void**>(nb_bytes + numbox_left_arrow_offset))
		{
			set_sso_string(static_cast<char*>(left_arrow) + gui_component_name_offset, "OptionNumBoxLeftArrow");
		}
		if (void* right_arrow = *reinterpret_cast<void**>(nb_bytes + numbox_right_arrow_offset))
		{
			set_sso_string(static_cast<char*>(right_arrow) + gui_component_name_offset, "OptionNumBoxRightArrow");
		}

		// mIsInteger picks the value-text format and the input path (one step per press instead of an analog repeat).
		const bool is_integer = is_whole(min_v) && is_whole(max_v) && is_whole(step_v);
		*reinterpret_cast<bool*>(nb_bytes + numbox_is_integer_offset) = is_integer;

		// SetRange derives its own step and overwrites mNumberStepValue, so pin ours after it. A zero step would
		// freeze the box, and SetRange never clamps the current value, which the SetNumberValue below does.
		g_numbox_set_range(nb, static_cast<float>(min_v), static_cast<float>(max_v));
		*reinterpret_cast<float*>(nb_bytes + numbox_step_offset) = static_cast<float>(step_v != 0.0 ? step_v : 1.0);

		g_apply_data(reinterpret_cast<MenuScreen*>(screen), nb);

		// ApplyDataToComponent copies OptionNumBox's own row grid, so override it.
		{
			char* def                                    = nb_bytes + component_def_offset;
			*reinterpret_cast<float*>(def + def_y)       = row_base_y;
			*reinterpret_cast<float*>(def + def_spacing) = row_pitch;
		}

		if (void* label_tb = *reinterpret_cast<void**>(nb_bytes + numbox_label_text_offset))
		{
			g_show_text(label_tb, label);
		}

		// notify=false avoids persisting the initial paint as a user edit.
		g_numbox_set_value(nb, static_cast<float>(initial), false);

		if (value_labels && !value_labels->empty())
		{
			int idx = static_cast<int>(initial);
			if (idx < 0)
			{
				idx = 0;
			}
			else if (idx >= static_cast<int>(value_labels->size()))
			{
				idx = static_cast<int>(value_labels->size()) - 1;
			}
			set_numbox_value_text(nb, (*value_labels)[idx].c_str());
		}

		if (disabled)
		{
			*reinterpret_cast<bool*>(nb_bytes + numbox_disable_input_offset) = true;
			grey_text_box(*reinterpret_cast<void**>(nb_bytes + numbox_label_text_offset));
			grey_text_box(*reinterpret_cast<void**>(nb_bytes + numbox_value_text_offset));
			if (block_input)
			{
				nb->m_is_useable = false;
				if (auto* box = *reinterpret_cast<char**>(nb_bytes + numbox_anim_offset))
				{
					*reinterpret_cast<std::uint32_t*>(box + animation_color_offset) = numbox_hover_bg_black;
				}
			}
		}

		finalize_row(screen, nb);
		nb->m_location_x = numbox_location_x;
		return nb;
	}

	// Formats a numeric setting value for display. The value is rounded to the display step's precision so scaling by 100
	// does not surface floating-point noise, then trailing zeros are trimmed.
	static std::string format_setting_display(double value, bool show_as_pct, bool is_pct, double step)
	{
		double shown           = is_pct ? value * 100.0 : value;
		const double disp_step = is_pct ? step * 100.0 : step;

		int decimals = 0;
		if (disp_step > 0.0)
		{
			double s = disp_step;
			while (decimals < 6 && std::abs(s - std::round(s)) > 1e-9)
			{
				s *= 10.0;
				++decimals;
			}
		}
		const double scale = std::pow(10.0, decimals);
		shown              = std::round(shown * scale) / scale;

		std::string out = std::to_string(shown);
		if (out.find('.') != std::string::npos)
		{
			const std::size_t last = out.find_last_not_of('0');
			out.erase((out[last] == '.') ? last : last + 1);
		}
		if (show_as_pct || is_pct)
		{
			out += "%";
		}
		return out;
	}

	// Native dragging rewrites the value text to a percentage.
	static void set_slider_value_text(GUIComponent* slider, const char* text)
	{
		if (!g_show_text || !slider)
		{
			return;
		}
		if (void* value_tb = *reinterpret_cast<void**>(reinterpret_cast<char*>(slider) + slider_value_text_offset))
		{
			g_show_text(value_tb, text);
		}
	}

	static void* row_bounded_area(GUIComponent* self, std::int32_t* out)
	{
		const int left = static_cast<int>(row_location_x + row_text_offset_x);
		out[0]         = left;
		out[1]         = static_cast<int>(self->m_location_y) - 22;
		out[2]         = static_cast<int>(row_location_x) + 22 - left;
		out[3]         = 44;
		return out;
	}

	static std::uintptr_t build_row_area_vtable(std::uintptr_t* dst, std::size_t dst_bytes, std::uintptr_t src)
	{
		std::memcpy(dst, reinterpret_cast<const void*>(src), dst_bytes);
		dst[0x98 / sizeof(std::uintptr_t)] = reinterpret_cast<std::uintptr_t>(&row_bounded_area);
		dst[0xA0 / sizeof(std::uintptr_t)] = reinterpret_cast<std::uintptr_t>(&row_bounded_area);
		return reinterpret_cast<std::uintptr_t>(dst);
	}

	static void install_wide_button_nav_rect(GUIComponent* row)
	{
		if (!g_button_vtable_patched)
		{
			const std::uintptr_t native_vtable = *reinterpret_cast<std::uintptr_t*>(row);
			g_button_vtable_patched = build_row_area_vtable(g_button_vtable_copy, sizeof(g_button_vtable_copy), native_vtable);
		}
		*reinterpret_cast<std::uintptr_t*>(row) = g_button_vtable_patched;
	}

	// Slider stores a normalized 0..1 fraction, with drags snapped in the SetFraction hook.
	static GUIComponent* make_slider_row(MiscSettingsScreen* screen, const char* label, double min_v, double max_v, double step_v, double initial, bool show_as_pct, bool is_pct, bool disabled, bool block_input = true)
	{
		if (!g_gui_component_ctor || !g_image_ctor || !g_textbox_ctor || !g_slider_defaults || !g_slider_set_fraction || !g_slider_vtable || !g_apply_data || !g_show_text)
		{
			return nullptr;
		}

		char* s = static_cast<char*>(game_alloc(slider_sizeof));
		if (!s)
		{
			return nullptr;
		}
		std::memset(s, 0, slider_sizeof);

		// Install the bounded GetArea vtable over the base GUIComponent vtable.
		g_gui_component_ctor(s, 0);
		*reinterpret_cast<std::uintptr_t*>(s) = g_slider_vtable_patched ? g_slider_vtable_patched : g_slider_vtable;

		// Defaults does not initialise mOnValueChanged or mValueTextBox.
		std::memset(s + slider_on_changed_offset, 0, 3 * sizeof(void*));
		*reinterpret_cast<void**>(s + slider_label_offset)      = nullptr;
		*reinterpret_cast<void**>(s + slider_value_text_offset) = nullptr;

		g_slider_defaults(s);
		*reinterpret_cast<void**>(s + slider_owner_offset) = screen;

		// Construct the four owned sub-components at the origin, matching the game.
		char* backing = static_cast<char*>(game_alloc(image_sizeof));
		char* fill    = static_cast<char*>(game_alloc(image_sizeof));
		char* lbl     = static_cast<char*>(game_alloc(textbox_sizeof));
		char* val     = static_cast<char*>(game_alloc(textbox_sizeof));
		if (!backing || !fill || !lbl || !val)
		{
			game_free(backing);
			game_free(fill);
			game_free(lbl);
			game_free(val);
			game_free(s);
			return nullptr;
		}
		g_image_ctor(backing, 0);
		g_image_ctor(fill, 0);
		g_textbox_ctor(lbl, 0);
		g_textbox_ctor(val, 0);
		*reinterpret_cast<void**>(s + slider_backing_offset)    = backing;
		*reinterpret_cast<void**>(s + slider_fill_offset)       = fill;
		*reinterpret_cast<void**>(s + slider_label_offset)      = lbl;
		*reinterpret_cast<void**>(s + slider_value_text_offset) = val;

		// SetParent only writes mParentContainer.
		*reinterpret_cast<void**>(s + slider_parent_offset) = reinterpret_cast<char*>(screen) + menu_screen_container_offset;

		set_sso_string(s + gui_component_name_offset, "OptionSlider");
		set_sso_string(val + gui_component_name_offset, "OptionSliderValueText");

		g_apply_data(reinterpret_cast<MenuScreen*>(screen), reinterpret_cast<GUIComponent*>(s));

		{
			char* def                                    = s + component_def_offset;
			*reinterpret_cast<float*>(def + def_y)       = row_base_y;
			*reinterpret_cast<float*>(def + def_spacing) = row_pitch;
		}

		if (void* label_tb = *reinterpret_cast<void**>(s + slider_label_offset))
		{
			g_show_text(label_tb, label);
		}

		// notify=false avoids treating the initial paint as a user edit.
		const double range = max_v - min_v;
		const float frac   = (range > 0.0) ? static_cast<float>((initial - min_v) / range) : 0.0f;
		g_slider_set_fraction(s, frac, false);
		set_slider_value_text(reinterpret_cast<GUIComponent*>(s),
		                      format_setting_display(initial, show_as_pct, is_pct, step_v).c_str());

		if (disabled)
		{
			// Native drag ignores mIsUseable, so HandleInput blocks it separately.
			auto* sc = reinterpret_cast<GUIComponent*>(s);
			if (block_input)
			{
				sc->m_is_useable = false;
			}
			grey_text_box(*reinterpret_cast<void**>(s + slider_label_offset));
			grey_text_box(*reinterpret_cast<void**>(s + slider_value_text_offset));
			grey_image(*reinterpret_cast<void**>(s + slider_backing_offset));
			grey_image(*reinterpret_cast<void**>(s + slider_fill_offset));
		}

		finalize_row(screen, reinterpret_cast<GUIComponent*>(s));
		reinterpret_cast<GUIComponent*>(s)->m_location_x = slider_location_x;
		return reinterpret_cast<GUIComponent*>(s);
	}

#pragma endregion

#pragma region Row teardown and mod list

	static void vector_erase(sgg::eastl_vector<GUIComponent*>& vec, GUIComponent* value)
	{
		for (GUIComponent** it = vec.m_begin; it != vec.m_end; ++it)
		{
			if (*it == value)
			{
				std::memmove(it, it + 1, reinterpret_cast<char*>(vec.m_end) - reinterpret_cast<char*>(it + 1));
				--vec.m_end;
				return;
			}
		}
	}

	// Our rows are not registered in the reflection helper.
	static void destroy_rows(MiscSettingsScreen* screen)
	{
		auto* menu = reinterpret_cast<MenuScreen*>(screen);

		auto unlink_and_free = [&](GUIComponent* comp, bool in_options, bool owns_subcomponents)
		{
			if (!comp)
			{
				return;
			}
			if (menu->m_mouse_over_component == comp)
			{
				menu->m_mouse_over_component = nullptr;
			}
			if (menu->m_selected_component == comp)
			{
				menu->m_selected_component = nullptr;
			}
			if (screen->m_component_focused == comp)
			{
				screen->m_component_focused = nullptr;
			}
			if (screen->m_last_option_button == comp)
			{
				screen->m_last_option_button = nullptr;
			}
			if (g_edit_component == comp)
			{
				g_edit_component = nullptr;
			}

			vector_erase(menu->m_components, comp);
			if (in_options)
			{
				vector_erase(screen->m_options, comp);
			}

			if (owns_subcomponents)
			{
				// Num-box and slider vtables free their owned sub-components.
				void** vtbl = *reinterpret_cast<void***>(comp);
				auto dtor = reinterpret_cast<void* (*)(void*, unsigned int)>(vtbl[vtable_deleting_dtor_offset / sizeof(void*)]);
				dtor(comp, 0);
			}
			else if (g_button_dtor)
			{
				g_button_dtor(comp);
			}
			game_free(comp);
		};

		for (const auto& row : g_rows)
		{
			unlink_and_free(row.component, true, row.is_stepper || row.is_enum || row.is_slider);
			unlink_and_free(row.value_component, false, false);
		}

		g_rows.clear();
	}

	static void build_mod_list(MiscSettingsScreen* screen)
	{
		std::vector<std::string> stems;
		for (const auto* cfg : toml_v2::config_file::g_config_files)
		{
			if (!cfg || cfg->m_config_file_stem_as_str.empty())
			{
				continue;
			}

			if (cfg->m_config_file_stem_as_str == "Hell2Modding-Hell2Modding-General")
			{
				continue;
			}
			if (std::find(stems.begin(), stems.end(), cfg->m_config_file_stem_as_str) == stems.end())
			{
				stems.push_back(cfg->m_config_file_stem_as_str);
			}
		}

		std::vector<std::pair<std::string, std::string>> mods;
		mods.reserve(stems.size());
		for (const auto& stem : stems)
		{
			mods.emplace_back(display_name_from_stem(stem), stem);
		}
		std::sort(mods.begin(),
		          mods.end(),
		          [](const auto& a, const auto& b)
		          {
			          return compare_display_names(a.first, b.first) < 0;
		          });

		for (const auto& [display, stem] : mods)
		{
			// Opted-out mods stay listed but cannot be opened.
			const bool opted_out = mod_opted_out(stem);
			if (auto* row = make_text_row(screen, escape_markup(display).c_str(), opted_out, /*block_input*/ false))
			{
				PanelRow pr{row, RowKind::mod_entry, stem, {}};
				pr.disabled    = opted_out;
				pr.description = opted_out ? opt_out_description(stem) : mod_description_from_stem(stem);
				g_rows.push_back(std::move(pr));
			}
		}
	}

#pragma endregion

#pragma region Value formatting, freetext editing, and commit

	static std::string key_to_display(const std::string& key)
	{
		const auto is_upper = [](char c)
		{
			return c >= 'A' && c <= 'Z';
		};
		const auto is_lower = [](char c)
		{
			return c >= 'a' && c <= 'z';
		};

		std::string out;
		out.reserve(key.size() + 8);
		for (std::size_t i = 0; i < key.size(); ++i)
		{
			const char c = key[i];
			if (c == '_')
			{
				out.push_back(' ');
				continue;
			}
			if (!out.empty() && out.back() != ' ')
			{
				const char prev           = key[i - 1];
				const bool lower_to_upper = is_lower(prev) && is_upper(c);
				const bool acronym_boundary = is_upper(prev) && is_upper(c) && (i + 1 < key.size()) && is_lower(key[i + 1]);
				if (lower_to_upper || acronym_boundary)
				{
					out.push_back(' ');
				}
			}
			out.push_back(c);
		}

		for (char& c : out)
		{
			if (c != ' ')
			{
				if (is_lower(c))
				{
					c = static_cast<char>(c - ('a' - 'A'));
				}
				break;
			}
		}
		return out;
	}

	// Creates a "caret" (|) when editing textboxes
	static std::string render_edit_display(const std::string& buf, std::size_t cursor, bool blink_on)
	{
		const char* caret     = blink_on ? "|" : " ";
		const std::size_t len = buf.size();
		if (cursor > len)
		{
			cursor = len;
		}

		const float caret_w    = 0.6f;
		const float ellipsis_w = measure_width("...");

		if (measure_width(buf) + caret_w <= value_display_max_width)
		{
			return escape_markup(buf.substr(0, cursor)) + caret + escape_markup(buf.substr(cursor));
		}

		std::size_t start = cursor;
		std::size_t end   = cursor;
		float used        = caret_w;
		for (bool grew = true; grew;)
		{
			grew = false;

			if (start > 0)
			{
				const std::size_t prev = caret_prev(buf, start);
				const float add        = measure_width(buf.substr(prev, start - prev));
				const float overhead   = (prev > 0 ? ellipsis_w : 0.0f) + (end < len ? ellipsis_w : 0.0f);
				if (used + add + overhead <= value_display_max_width)
				{
					used  += add;
					start  = prev;
					grew   = true;
				}
			}

			if (end < len)
			{
				const std::size_t next = caret_next(buf, end);
				const float add        = measure_width(buf.substr(end, next - end));
				const float overhead   = (start > 0 ? ellipsis_w : 0.0f) + (next < len ? ellipsis_w : 0.0f);
				if (used + add + overhead <= value_display_max_width)
				{
					used += add;
					end   = next;
					grew  = true;
				}
			}
		}

		std::string out;
		if (start > 0)
		{
			out += "...";
		}
		out += escape_markup(buf.substr(start, cursor - start));
		out += caret;
		out += escape_markup(buf.substr(cursor, end - cursor));
		if (end < len)
		{
			out += "...";
		}
		return out;
	}

	static bool numeric_char_ok(const std::string& buffer, std::size_t cursor, char c)
	{
		if (c >= '0' && c <= '9')
		{
			return true;
		}
		if (c == '-' || c == '+')
		{
			return cursor == 0 && (buffer.empty() || (buffer.front() != '-' && buffer.front() != '+'));
		}
		if (c == '.')
		{
			return buffer.find('.') == std::string::npos;
		}
		return false;
	}

	// Window-procedure callback: while a freetext setting is being edited, capture typed characters and caret movement
	// into the edit buffer. A mouse click anywhere submits the edit.
	static void on_wndproc(HWND, UINT msg, WPARAM wparam, LPARAM)
	{
		if (!g_editing)
		{
			return;
		}

		if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN)
		{
			g_edit_confirm = true;
			return;
		}

		if (g_edit_cursor > g_edit_buffer.size())
		{
			g_edit_cursor = g_edit_buffer.size();
		}

		if (msg == WM_KEYDOWN)
		{
			const bool ctrl = (GetKeyState(VK_CONTROL) & 0x80'00) != 0;
			switch (wparam)
			{
			case VK_BACK:
				if (g_edit_cursor > 0)
				{
					const std::size_t prev = caret_prev(g_edit_buffer, g_edit_cursor);
					g_edit_buffer.erase(prev, g_edit_cursor - prev);
					g_edit_cursor = prev;
				}
				break;
			case VK_DELETE:
				if (g_edit_cursor < g_edit_buffer.size())
				{
					const std::size_t next = caret_next(g_edit_buffer, g_edit_cursor);
					g_edit_buffer.erase(g_edit_cursor, next - g_edit_cursor);
				}
				break;
			case VK_LEFT:
				g_edit_cursor = ctrl ? caret_prev_word(g_edit_buffer, g_edit_cursor) : caret_prev(g_edit_buffer, g_edit_cursor);
				break;
			case VK_RIGHT:
				g_edit_cursor = ctrl ? caret_next_word(g_edit_buffer, g_edit_cursor) : caret_next(g_edit_buffer, g_edit_cursor);
				break;
			case VK_HOME: g_edit_cursor = 0; break;
			case VK_END:  g_edit_cursor = g_edit_buffer.size(); break;
			default:      break;
			}
			return;
		}

		if (msg == WM_CHAR)
		{
			const unsigned c = static_cast<unsigned>(wparam);
			if (c < 32 || c >= 127)
			{
				return;
			}
			const char ch = static_cast<char>(c);
			if (g_edit_numeric && !numeric_char_ok(g_edit_buffer, g_edit_cursor, ch))
			{
				return;
			}
			g_edit_buffer.insert(g_edit_cursor, 1, ch);
			++g_edit_cursor;
		}
	}

	static void ensure_wndproc_registered()
	{
		static bool registered = false;
		if (registered || !g_renderer || !g_feature_enabled)
		{
			return;
		}
		g_renderer->add_wndproc_callback(
		    [](HWND hwnd, UINT32 msg, WPARAM wparam, LPARAM lparam)
		    {
			    on_wndproc(hwnd, msg, wparam, lparam);
		    });
		registered = true;
	}

	static void enter_edit_mode(GUIComponent* value_component, toml_v2::config_file::config_entry_base* entry)
	{
		ensure_wndproc_registered();
		g_editing        = true;
		g_edit_component = value_component;
		g_edit_entry     = entry;
		g_edit_buffer    = entry ? entry->get_serialized_value() : std::string{};
		g_edit_cursor    = g_edit_buffer.size();
		g_edit_numeric   = entry && entry->type() != typeid(std::string);
		g_edit_confirm   = false;
		g_edit_cancel    = false;
	}

	static void exit_edit_mode()
	{
		g_editing        = false;
		g_edit_component = nullptr;
		g_edit_entry     = nullptr;
		g_edit_buffer.clear();
		g_edit_cursor  = 0;
		g_edit_confirm = false;
		g_edit_cancel  = false;
	}

	static std::string restart_change_key(toml_v2::config_file::config_entry_base* entry, const std::string& stem)
	{
		return stem + '\0' + entry->m_definition.m_section + '\0' + entry->m_definition.m_key;
	}

	// Captures a restart-required setting's baseline before its first modification so a later revert clears it.
	static void capture_restart_baseline(toml_v2::config_file::config_entry_base* entry)
	{
		if (!entry || !entry->m_config_file)
		{
			return;
		}
		const std::string& stem = entry->m_config_file->m_config_file_stem_as_str;
		if (!setting_requires_restart(stem, entry->m_definition.m_section, entry->m_definition.m_key))
		{
			return;
		}
		const std::string key = restart_change_key(entry, stem);
		g_restart_baselines.try_emplace(key, entry->get_serialized_value());
	}

	static std::string current_language_code()
	{
		return g_config_language ? std::string(g_config_language) : std::string();
	}

	static std::string resolve_localized(const localized_text& t)
	{
		if (t.empty())
		{
			return {};
		}
		if (t.size() == 1)
		{
			return t.begin()->second;
		}
		if (const auto it = t.find(current_language_code()); it != t.end())
		{
			return it->second;
		}
		if (const auto it = t.find("en"); it != t.end())
		{
			return it->second;
		}
		if (const auto it = t.find(""); it != t.end())
		{
			return it->second;
		}
		return t.begin()->second;
	}

	static bool g_view_has_dynamic = false;

	// Dynamic metadata marks the view so later toggles can re-evaluate it.
	static std::optional<setting_metadata> resolved_metadata(const std::string& stem, const std::string& section, const std::string& key)
	{
		auto meta = get_setting_metadata(stem, section, key);
		if (!meta)
		{
			return resolve_setting_metadata(stem, section, key);
		}
		if (meta->has_dynamic)
		{
			g_view_has_dynamic = true;
			if (auto dynamic = resolve_setting_metadata(stem, section, key))
			{
				return dynamic;
			}
		}
		return meta;
	}

	static std::string setting_display_name(const std::string& stem, const std::string& section, const std::string& key)
	{
		const auto meta = resolved_metadata(stem, section, key);
		if (meta)
		{
			if (std::string name = resolve_localized(meta->name); !name.empty())
			{
				return name;
			}
		}
		return key_to_display(key);
	}

	// Records or clears a restart-required setting change after the value has been written. If the new value equals the
	// baseline (e.g. a toggle flipped and flipped back, or a number re-typed to its original), nothing actually
	// changed and the setting is dropped from the restart list.
	static void note_change_if_restart_required(toml_v2::config_file::config_entry_base* entry, const std::string& new_value_display)
	{
		if (!entry || !entry->m_config_file)
		{
			return;
		}
		const std::string& stem = entry->m_config_file->m_config_file_stem_as_str;
		if (!setting_requires_restart(stem, entry->m_definition.m_section, entry->m_definition.m_key))
		{
			return;
		}
		const std::string key = restart_change_key(entry, stem);

		const auto baseline = g_restart_baselines.find(key);
		if (baseline != g_restart_baselines.end() && entry->get_serialized_value() == baseline->second)
		{
			g_restart_changes.erase(key);
		}
		else
		{
			const std::string line = display_name_from_stem(stem) + ": "
			                         + setting_display_name(stem, entry->m_definition.m_section, entry->m_definition.m_key) + " (" + new_value_display + ")";
			g_restart_changes[key] = line;
		}

		g_restart_required = !g_restart_changes.empty();
	}

	// Virtual-row Lua I/O uses the stored real config section, not a `group` override's view path.
	static const std::string& row_io_section(const PanelRow* row)
	{
		return !row->config_section.empty() ? row->config_section : g_view_section;
	}

	static bool commit_row_bool(PanelRow* row, bool v)
	{
		bool changed = false;
		if (row->entry)
		{
			if (row->entry->get_value_base<bool>() != v)
			{
				capture_restart_baseline(row->entry);
				row->entry->set_value_base<bool>(v);
				note_change_if_restart_required(row->entry, v ? "on" : "off");
				changed = true;
			}
		}
		else if (row->is_virtual_input)
		{
			const auto cur = get_virtual_value(row->stem, row_io_section(row), row->setting_key);
			if (!(cur.type == virtual_value::kind::boolean && cur.as_bool == v))
			{
				virtual_value nv;
				nv.type    = virtual_value::kind::boolean;
				nv.as_bool = v;
				set_virtual_value(row->stem, row_io_section(row), row->setting_key, nv);
				changed = true;
			}
		}
		if (changed && g_view_has_dynamic)
		{
			g_dynamic_refresh_settle = dynamic_refresh_settle_seconds;
		}
		return changed;
	}

	static bool commit_row_number(PanelRow* row, double v)
	{
		bool changed = false;
		if (row->entry)
		{
			if (row->entry->get_value_base<double>() != v)
			{
				capture_restart_baseline(row->entry);
				row->entry->set_value_base<double>(v);
				note_change_if_restart_required(row->entry, row->entry->get_serialized_value());
				changed = true;
			}
		}
		else if (row->is_virtual_input)
		{
			const auto cur = get_virtual_value(row->stem, row_io_section(row), row->setting_key);
			if (!(cur.type == virtual_value::kind::number && cur.as_number == v))
			{
				virtual_value nv;
				nv.type      = virtual_value::kind::number;
				nv.as_number = v;
				set_virtual_value(row->stem, row_io_section(row), row->setting_key, nv);
				changed = true;
			}
		}
		if (changed && g_view_has_dynamic)
		{
			g_dynamic_refresh_settle = dynamic_refresh_settle_seconds;
		}
		return changed;
	}

	// Virtual set() receives the config-serialized value as a string.
	static bool commit_row_serialized(PanelRow* row, const std::string& serialized, const std::string& display)
	{
		bool changed = false;
		if (row->entry)
		{
			if (row->entry->get_serialized_value() != serialized)
			{
				capture_restart_baseline(row->entry);
				row->entry->set_serialized_value(serialized);
				note_change_if_restart_required(row->entry, display);
				changed = true;
			}
		}
		else if (row->is_virtual_input)
		{
			const auto cur = get_virtual_value(row->stem, row_io_section(row), row->setting_key);
			if (!(cur.type == virtual_value::kind::string && cur.as_string == serialized))
			{
				virtual_value nv;
				nv.type      = virtual_value::kind::string;
				nv.as_string = serialized;
				set_virtual_value(row->stem, row_io_section(row), row->setting_key, nv);
				changed = true;
			}
		}
		if (changed && g_view_has_dynamic)
		{
			g_dynamic_refresh_settle = dynamic_refresh_settle_seconds;
		}
		return changed;
	}

	static void refresh_value_display(GUIComponent* value_component, const std::string& serialized)
	{
		if (value_component && g_set_label)
		{
			const std::string disp = escape_markup(truncate_value(serialized));
			g_set_label(value_component, disp.c_str());
		}
	}

	// Runs on the same frame HandleInput swallows the triggering key or click.
	static bool commit_or_cancel_edit()
	{
		if (g_edit_confirm)
		{
			if (g_edit_entry)
			{
				capture_restart_baseline(g_edit_entry);

				// Bad numeric input keeps the previous value because set_serialized_value validates before saving.
				g_edit_entry->set_serialized_value(g_edit_buffer);

				// Covers partially bounded or stepped numbers. set_serialized_value already parsed it.
				if (g_edit_entry->type() == typeid(double))
				{
					const auto meta = resolved_metadata(g_edit_entry->m_config_file->m_config_file_stem_as_str,
					                                    g_edit_entry->m_definition.m_section,
					                                    g_edit_entry->m_definition.m_key);
					if (meta && (meta->has_min || meta->has_max || meta->has_step))
					{
						double v = g_edit_entry->get_value_base<double>();

						const auto clamp_range = [&](double x)
						{
							if (meta->has_min && x < meta->min)
							{
								x = meta->min;
							}
							if (meta->has_max && x > meta->max)
							{
								x = meta->max;
							}
							return x;
						};

						v = clamp_range(v);
						if (meta->has_step && meta->step > 0.0)
						{
							const double base = meta->has_min ? meta->min : 0.0;
							v                 = base + std::round((v - base) / meta->step) * meta->step;
							v                 = clamp_range(v);
						}
						g_edit_entry->set_value_base<double>(v);
					}
				}

				note_change_if_restart_required(g_edit_entry, g_edit_entry->get_serialized_value());

				// Reflect the committed value in the right-hand display in place.
				refresh_value_display(g_edit_component, g_edit_entry->get_serialized_value());

				if (g_view_has_dynamic)
				{
					g_dynamic_refresh_settle = dynamic_refresh_settle_seconds;
				}
			}
			exit_edit_mode();
			return true;
		}
		if (g_edit_cancel)
		{
			if (g_edit_entry)
			{
				refresh_value_display(g_edit_component, g_edit_entry->get_serialized_value());
			}
			exit_edit_mode();
			return true;
		}
		return false;
	}

	static void update_edit_label()
	{
		if (g_edit_component && g_set_label)
		{
			const bool cursor_on    = ((GetTickCount64() / edit_cursor_blink_ms) % 2) == 0;
			const std::string label = render_edit_display(g_edit_buffer, g_edit_cursor, cursor_on);
			g_set_label(g_edit_component, label.c_str());
		}
	}

#pragma endregion

#pragma region Editability context and menu-path helpers

	static bool is_enabled_key(const std::string& key)
	{
		return big::string::to_lower(key) == "enabled";
	}

	static bool entry_has_description(const toml_v2::config_file::config_entry_base* entry)
	{
		return entry && !entry->m_description.m_description.empty();
	}

	static bool g_opened_in_game = false;

	// CurrentHubRoom is non-nil in the Crossroads, nil in a run.
	static bool g_in_hub = false;

	static bool g_options_screen_open = false;

	bool on_change_callbacks_enabled()
	{
		return g_options_screen_open;
	}

	static constexpr std::size_t game_screen_get_type_vtable_slot = 10;
	static constexpr int screen_type_pause                        = 0x10'00'03;

	static bool opener_indicates_in_game(void* opened_from)
	{
		if (!opened_from)
		{
			return false;
		}
		void** vtable = *reinterpret_cast<void***>(opened_from);
		auto get_type = reinterpret_cast<int (*)(void*)>(vtable[game_screen_get_type_vtable_slot]);
		return get_type(opened_from) == screen_type_pause;
	}

	static editable_context effective_editable_context(const std::optional<setting_metadata>& meta, bool is_enabled_toggle)
	{
		if (is_enabled_toggle)
		{
			return editable_context::main_menu;
		}
		if (meta && meta->restart_required)
		{
			return editable_context::main_menu;
		}
		return meta ? meta->context : editable_context::any;
	}

	static bool is_context_restricted(editable_context ctx)
	{
		switch (ctx)
		{
		case editable_context::main_menu: return g_opened_in_game;
		case editable_context::in_save:   return !g_opened_in_game;
		case editable_context::in_hub:    return !(g_opened_in_game && g_in_hub);
		default:                          return false;
		}
	}

	static std::string context_note(editable_context ctx)
	{
		switch (ctx)
		{
		case editable_context::main_menu: return "This setting can only be changed from the main menu.";
		case editable_context::in_save:   return "This setting can only be changed while a save is loaded.";
		case editable_context::in_hub:    return "This setting can only be changed while in the Crossroads.";
		default:                          return {};
		}
	}

	// Description-box text for a context-restricted row: scenario note first, then appending the row's normal description.
	static std::string note_then_description(const std::string& note, const std::string& description)
	{
		if (note.empty())
		{
			return description;
		}
		if (description.empty())
		{
			return note;
		}
		return note + "\n" + description;
	}

	// True if `cfg` has a direct config entry at (section, key), so group desc fields can defer to real children.
	static bool config_child_exists(toml_v2::config_file* cfg, const std::string& section, const std::string& key)
	{
		if (!cfg)
		{
			return false;
		}
		toml_v2::config_definition def(section, key);
		return cfg->try_get_entry(def) != nullptr;
	}

	static std::set<std::string> g_warned_group_overrides;

	static std::string menu_path_of(const std::string& config_section, const std::vector<std::string>& group)
	{
		if (group.empty())
		{
			return config_section;
		}
		std::string p = root_section;
		for (const auto& seg : group)
		{
			p.push_back('.');
			p.append(seg);
		}
		return p;
	}

	static std::vector<std::string> author_group_path(const std::string& menu_path)
	{
		std::vector<std::string> out;
		const std::string prefix = std::string(root_section) + ".";
		if (menu_path.rfind(prefix, 0) != 0)
		{
			return out;
		}
		std::string rest = menu_path.substr(prefix.size());
		while (!rest.empty())
		{
			const auto dot = rest.find('.');
			out.push_back(rest.substr(0, dot));
			rest = (dot == std::string::npos) ? std::string{} : rest.substr(dot + 1);
		}
		return out;
	}

	static const menu_group* find_author_group(const std::vector<menu_group>& tree, const std::string& menu_path)
	{
		const std::string prefix = std::string(root_section) + ".";
		if (menu_path.rfind(prefix, 0) != 0)
		{
			return nullptr;
		}
		std::string rest                     = menu_path.substr(prefix.size());
		const std::vector<menu_group>* level = &tree;
		const menu_group* found              = nullptr;
		while (!rest.empty())
		{
			const auto dot        = rest.find('.');
			const std::string seg = rest.substr(0, dot);
			found                 = nullptr;
			for (const auto& g : *level)
			{
				if (g.id == seg)
				{
					found = &g;
					break;
				}
			}
			if (!found)
			{
				return nullptr;
			}
			level = &found->children;
			rest  = (dot == std::string::npos) ? std::string{} : rest.substr(dot + 1);
		}
		return found;
	}

	// Validates `group` overrides so the panel and Reset resolve the same menu paths.
	static std::string resolve_entry_menu_path(const std::string& stem, const std::vector<menu_group>& author_groups, toml_v2::config_file* view_cfg, const std::string& csection, const std::vector<std::string>& group)
	{
		if (group.empty())
		{
			return csection;
		}
		const std::string m = menu_path_of(csection, group);
		if (find_author_group(author_groups, m))
		{
			return m;
		}
		if (view_cfg)
		{
			const std::string desc_prefix = m + ".";
			for (const auto& [k, e] : view_cfg->m_entries)
			{
				if (k.m_section == m || k.m_section.rfind(desc_prefix, 0) == 0)
				{
					return m;
				}
			}
		}
		const std::string warn_key = stem + '\0' + m;
		if (g_warned_group_overrides.insert(warn_key).second)
		{
			LOG(WARNING) << "[mod_settings] " << stem << ": `group` target '" << m << "' is neither a config section nor a category declared in configDesc `groups`; the row falls back to its config-section placement. Declare it in `groups` if it is a new menu category.";
		}
		return csection;
	}

	static bool menu_path_in_scope(const std::string& p, const std::string& scope)
	{
		return p == scope || p.rfind(scope + ".", 0) == 0;
	}

#pragma endregion

#pragma region Panel builder

	struct panel_item
	{
		bool is_group = false;
		std::string key;
		toml_v2::config_file::config_entry_base* entry = nullptr;
		std::string child_section;
		std::string config_section; // real config section for virtual I/O.
		bool is_author_group = false;
		localized_text author_name;
		localized_text author_description;
		localized_text author_disabled_description;
		bool author_disabled           = false; // already resolved if dynamic.
		editable_context group_context = editable_context::any;
		bool has_order                 = false;
		double order                   = 0.0;
		std::string sort_name;
		bool is_enabled = false;
		bool is_action  = false;
		action_info action;
		bool is_virtual          = false;
		bool virtual_interactive = false; // has a Lua set().
	};

	struct panel_contents
	{
		std::vector<panel_item> items;
		toml_v2::config_file::config_entry_base* enabled_entry = nullptr;
		toml_v2::config_file* view_cfg                         = nullptr;
		bool mod_enabled                                       = true;
	};

	static panel_contents collect_panel_items(const std::string& stem, const std::string& section)
	{
		panel_contents out;
		std::vector<panel_item>& items = out.items;

		std::map<std::string, panel_item> groups;
		toml_v2::config_file*& view_cfg  = out.view_cfg;
		const std::string section_prefix = section + ".";

		const std::vector<menu_group> author_groups = mod_menu_groups(stem);

		auto resolve_menu_path = [&](const std::string& csection, const std::vector<std::string>& group) -> std::string
		{
			return resolve_entry_menu_path(stem, author_groups, view_cfg, csection, group);
		};

		auto placement = [&](const std::string& csection, const std::vector<std::string>& group, std::string& child_out) -> int
		{
			const std::string m = resolve_menu_path(csection, group);
			if (m == section)
			{
				return 1;
			}
			if (m.rfind(section_prefix, 0) == 0)
			{
				const std::string rest = m.substr(section_prefix.size());
				child_out              = section_prefix + rest.substr(0, rest.find('.'));
				return 2;
			}
			return 0;
		};

		auto ensure_group = [&](const std::string& child_path)
		{
			if (groups.contains(child_path))
			{
				return;
			}
			panel_item g;
			g.is_group      = true;
			g.child_section = child_path;
			g.key           = child_path.substr(child_path.rfind('.') + 1);
			if (const menu_group* ag = find_author_group(author_groups, child_path))
			{
				g.is_author_group             = true;
				g.author_name                 = ag->name;
				g.author_description          = ag->description;
				g.author_disabled_description = ag->disabled_description;
				g.author_disabled             = ag->disabled;
				g.group_context               = ag->context;
				if (ag->has_order)
				{
					g.has_order = true;
					g.order     = ag->order;
				}

				// Dynamic fields are skipped at load, so re-evaluate the declaration now.
				if (ag->has_dynamic)
				{
					g_view_has_dynamic = true;
					if (const auto live = resolve_menu_group(stem, author_group_path(child_path)))
					{
						g.author_name                 = live->name;
						g.author_description          = live->description;
						g.author_disabled_description = live->disabled_description;
						g.author_disabled             = live->disabled;
						g.group_context               = live->context;
					}
				}
				g.sort_name = resolve_localized(g.author_name);
			}
			else if (const auto meta = resolved_metadata(stem, section, g.key); meta)
			{
				g.sort_name = resolve_localized(meta->name);

				// A configDesc child table can also describe its own config child of the same name.
				if (meta->has_order && !config_child_exists(view_cfg, child_path, "order"))
				{
					g.has_order = true;
					g.order     = meta->order;
				}
				if (!config_child_exists(view_cfg, child_path, "editableContext"))
				{
					g.group_context = meta->context;
				}
			}
			if (g.sort_name.empty())
			{
				g.sort_name = key_to_display(g.key);
			}
			groups.emplace(child_path, std::move(g));
		};

		for (auto* cfg : toml_v2::config_file::g_config_files)
		{
			if (!cfg || cfg->m_config_file_stem_as_str != stem || cfg != live_config_file(stem))
			{
				continue;
			}
			view_cfg = cfg;
			for (auto& [key, entry] : cfg->m_entries)
			{
				if (!entry || key.m_key == section_empty_key)
				{
					continue;
				}

				// Keys left in the .cfg that the mod no longer declares are not part of its settings.
				if (!setting_is_declared(stem, key.m_section, key.m_key))
				{
					continue;
				}

				// Undescribed keys stay hidden. The master toggle is exempt only for mods that declare their settings,
				// since Chalk re-binds stale .cfg keys with an empty description and an old "enabled" would resurface.
				const bool is_enabled_toggle = key.m_section == root_section && entry->type() == typeid(bool) && is_enabled_key(key.m_key);
				const bool desc_exempt       = is_enabled_toggle && mod_declares_settings(stem);
				if (!desc_exempt && !setting_is_described(stem, key.m_section, key.m_key)
				    && !entry_has_description(entry.get()))
				{
					continue;
				}

				// Only a toggle that is actually shown may mark the mod disabled.
				if (!out.enabled_entry && is_enabled_toggle)
				{
					out.enabled_entry = entry.get();
				}

				const auto static_meta             = get_setting_metadata(stem, key.m_section, key.m_key);
				const std::vector<std::string> grp = static_meta ? static_meta->group : std::vector<std::string>{};
				std::string child_path;
				const int place = placement(key.m_section, grp, child_path);
				if (place == 1)
				{
					panel_item it;
					it.key            = key.m_key;
					it.entry          = entry.get();
					it.config_section = key.m_section;
					it.sort_name      = setting_display_name(stem, key.m_section, key.m_key);
					if (const auto meta = resolved_metadata(stem, key.m_section, key.m_key); meta && meta->has_order)
					{
						it.has_order = true;
						it.order     = meta->order;
					}
					items.push_back(std::move(it));
				}
				else if (place == 2)
				{
					ensure_group(child_path);
				}
			}
		}

		for (auto& kv : groups)
		{
			items.push_back(std::move(kv.second));
		}

		// Actions are bucketed by menu path like settings.
		for (auto& a : get_actions(stem, ""))
		{
			std::string child_path;
			const int place = placement(a.section, a.group, child_path);
			if (place == 2)
			{
				ensure_group(child_path);
				continue;
			}
			if (place != 1)
			{
				continue;
			}
			panel_item it;
			it.is_action      = true;
			it.key            = a.key;
			it.config_section = a.section;
			it.has_order      = a.has_order;
			it.order          = a.order;
			it.sort_name      = resolve_localized(a.name);
			if (it.sort_name.empty())
			{
				it.sort_name = key_to_display(a.key);
			}
			it.action = std::move(a);
			items.push_back(std::move(it));
		}

		for (const auto& vr : get_virtual_rows(stem, ""))
		{
			std::string child_path;
			const int place = placement(vr.section, vr.group, child_path);
			if (place == 2)
			{
				ensure_group(child_path);
				continue;
			}
			if (place != 1)
			{
				continue;
			}
			if (vr.has_dynamic)
			{
				g_view_has_dynamic = true;
			}
			panel_item it;
			it.is_virtual          = true;
			it.virtual_interactive = vr.interactive;
			it.key                 = vr.key;
			it.config_section      = vr.section;
			it.has_order           = vr.has_order;
			it.order               = vr.order;
			it.sort_name           = setting_display_name(stem, vr.section, vr.key);
			items.push_back(std::move(it));
		}

		out.mod_enabled = !out.enabled_entry || out.enabled_entry->get_value_base<bool>();
		if (section == root_section && out.enabled_entry)
		{
			for (auto& it : items)
			{
				if (it.entry == out.enabled_entry)
				{
					it.is_enabled = true;
				}
			}
		}

		// Row order: pinned "enabled", authored `order`, then localized display name.
		std::stable_sort(items.begin(),
		                 items.end(),
		                 [](const panel_item& a, const panel_item& b)
		                 {
			                 if (a.is_enabled != b.is_enabled)
			                 {
				                 return a.is_enabled;
			                 }
			                 if (a.is_enabled)
			                 {
				                 return false;
			                 }
			                 if (a.has_order != b.has_order)
			                 {
				                 return a.has_order;
			                 }
			                 if (a.has_order && a.order != b.order)
			                 {
				                 return a.order < b.order;
			                 }
			                 return compare_display_names(a.sort_name, b.sort_name) < 0;
		                 });

		return out;
	}

	static void build_panel_rows(MiscSettingsScreen* screen, const std::string& stem, const std::string& section, const panel_contents& contents)
	{
		const bool mod_enabled               = contents.mod_enabled;
		toml_v2::config_file* const view_cfg = contents.view_cfg;

		for (const auto& it : contents.items)
		{
			const bool is_enabled_row = it.is_enabled;
			const bool disabled       = !is_enabled_row && !mod_enabled;

			if (it.is_action)
			{
				if (it.action.has_dynamic)
				{
					g_view_has_dynamic = true;
				}
				const bool ctx_blocked  = is_context_restricted(it.action.context);
				const bool mod_off      = disabled;
				const bool act_disabled = mod_off || it.action.disabled || ctx_blocked;
				const std::string name  = resolve_localized(it.action.name);
				const std::string label = escape_markup(name.empty() ? key_to_display(it.key) : name);

				if (auto* row = make_button_row(screen, label.c_str(), act_disabled, /*block_input*/ mod_off))
				{
					if (mod_off)
					{
						row->m_can_be_focused = false;
						*reinterpret_cast<bool*>(reinterpret_cast<char*>(row) + sgg::gui_component_button_selectable_offset) = false;
					}
					else
					{
						// Clear overlays so soft-disabled buttons do not flash clickable.
						*reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(row) + sgg::gui_component_button_under_mouse_texture_offset) = 0;
						if (g_set_selected_texture)
						{
							g_set_selected_texture(row, 0);
						}
					}
					PanelRow pr{row, RowKind::action, stem, it.key};
					pr.disabled       = act_disabled;
					pr.target_section = it.action.section;

					if (ctx_blocked)
					{
						pr.description =
						    note_then_description(context_note(it.action.context), resolve_localized(it.action.description));
					}
					else if (it.action.disabled)
					{
						const std::string ddesc = resolve_localized(it.action.disabled_description);
						pr.description          = !ddesc.empty() ? ddesc : resolve_localized(it.action.description);
					}
					else
					{
						pr.description = resolve_localized(it.action.description);
					}
					g_rows.push_back(std::move(pr));
				}
				continue;
			}

			if (it.is_virtual)
			{
				// `group` can move a virtual row, so Lua I/O uses its real config section.
				const std::string& vsection = it.config_section;
				const auto vmeta            = resolved_metadata(stem, vsection, it.key);
				const std::string vname     = vmeta ? resolve_localized(vmeta->name) : std::string{};
				const std::string vlabel    = escape_markup(!vname.empty() ? vname : key_to_display(it.key));
				const std::string vdesc     = vmeta ? resolve_localized(vmeta->description) : std::string{};

				const auto build_readonly = [&](const std::string& value_text)
				{
					if (auto* row = make_text_row(screen, vlabel.c_str(), /*disabled*/ false, /*block_input*/ false, /*no_hover_highlight*/ true))
					{
						PanelRow pr{row, RowKind::info, stem, it.key};
						pr.disabled       = true;
						pr.config_section = vsection; // real config section.
						pr.value_component = make_value_display(screen, escape_markup(value_text).c_str(), /*disabled*/ false);
						pr.description = vdesc;
						g_rows.push_back(std::move(pr));
					}
				};

				if (!it.virtual_interactive)
				{
					build_readonly(get_virtual_display(stem, vsection, it.key));
					continue;
				}

				// If get() returns nil, `type` can force a widget seeded from `default` or a fallback.
				virtual_value vv = get_virtual_value(stem, vsection, it.key);
				if (vv.type == virtual_value::kind::none && vmeta && vmeta->type != widget_type::inferred)
				{
					const std::string& dflt = vmeta->default_value;
					switch (vmeta->type)
					{
					case widget_type::boolean:
						vv.type    = virtual_value::kind::boolean;
						vv.as_bool = (dflt == "true");
						break;
					case widget_type::number:
					{
						vv.type  = virtual_value::kind::number;
						double n = vmeta->has_min ? vmeta->min : 0.0;
						if (vmeta->has_default)
						{
							try
							{
								n = std::stod(dflt);
							}
							catch (...)
							{
							}
						}
						vv.as_number = n;
						break;
					}
					case widget_type::string:
					case widget_type::enumeration:
						vv.type      = virtual_value::kind::string;
						vv.as_string = dflt;
						break;
					default: break;
					}
				}
				const bool is_enum    = vmeta && !vmeta->values.empty();
				const bool is_bool    = vv.type == virtual_value::kind::boolean;
				const bool is_number  = vv.type == virtual_value::kind::number;
				const double step     = (vmeta && vmeta->has_step) ? vmeta->step : 1.0;
				const bool is_stepper = !is_enum && is_number && vmeta && vmeta->has_min && vmeta->has_max;

				std::string vv_serialized;
				switch (vv.type)
				{
				case virtual_value::kind::boolean: vv_serialized = vv.as_bool ? "true" : "false"; break;
				case virtual_value::kind::number:  vv_serialized = std::format("{}", vv.as_number); break;
				case virtual_value::kind::string:  vv_serialized = vv.as_string; break;
				default:                           break;
				}

				const bool author_disabled = vmeta && vmeta->disabled;
				const editable_context ctx = vmeta ? vmeta->context : editable_context::any;
				const bool context_blocked = is_context_restricted(ctx);

				std::vector<std::string> enum_values;
				std::vector<std::string> enum_labels;
				int enum_index = 0;
				if (is_enum)
				{
					enum_values = vmeta->values;
					if (vmeta->labels.size() == enum_values.size())
					{
						for (const auto& lbl : vmeta->labels)
						{
							enum_labels.push_back(resolve_localized(lbl));
						}
					}
					else
					{
						enum_labels = enum_values;
					}
					for (int i = 0; i < static_cast<int>(enum_values.size()); ++i)
					{
						if (enum_values[i] == vv_serialized)
						{
							enum_index = i;
							break;
						}
					}
				}

				if (!disabled && (context_blocked || author_disabled))
				{
					std::string vtext;
					if (is_enum && enum_index >= 0 && enum_index < static_cast<int>(enum_labels.size()))
					{
						vtext = enum_labels[enum_index];
					}
					else if (is_stepper)
					{
						vtext = format_setting_display(vv.as_number, vmeta->show_as_percentage, vmeta->is_percentage, step);
					}
					else
					{
						vtext = truncate_value(vv_serialized);
					}
					if (auto* ro_row = make_text_row(screen, vlabel.c_str(), /*disabled*/ true, /*block_input*/ false))
					{
						PanelRow pr{ro_row, RowKind::setting, stem, it.key};
						pr.disabled       = true;
						pr.config_section = vsection; // real config section.
						pr.value_component = make_value_display(screen, escape_markup(vtext).c_str(), /*disabled*/ true);
						pr.description =
						    context_blocked ?
						        note_then_description(context_note(ctx), vdesc) :
						        (!resolve_localized(vmeta->disabled_description).empty() ? resolve_localized(vmeta->disabled_description) : vdesc);
						g_rows.push_back(std::move(pr));
					}
					continue;
				}

				GUIComponent* row   = nullptr;
				GUIComponent* value = nullptr;
				bool built_slider   = false;
				if (is_enum)
				{
					row = make_numbox_row(screen, vlabel.c_str(), 0.0, static_cast<double>(enum_values.size() - 1), 1.0, static_cast<double>(enum_index), disabled, &enum_labels);
				}
				else if (is_bool)
				{
					row = make_toggle_row(screen, vlabel.c_str(), vv.as_bool, disabled);
				}
				else if (is_stepper)
				{
					row = make_slider_row(screen,
					                      vlabel.c_str(),
					                      vmeta->min,
					                      vmeta->max,
					                      step,
					                      vv.as_number,
					                      vmeta->show_as_percentage,
					                      vmeta->is_percentage,
					                      disabled);
					if (row)
					{
						built_slider = true;
					}
					else
					{
						row = make_numbox_row(screen, vlabel.c_str(), vmeta->min, vmeta->max, step, vv.as_number, disabled);
					}
				}
				else
				{
					// Interactive free-text virtual rows are not supported yet.
					build_readonly(truncate_value(vv_serialized));
					continue;
				}

				if (row)
				{
					PanelRow pr{row, RowKind::setting, stem, it.key};
					pr.disabled         = disabled;
					pr.is_virtual_input = true;
					pr.config_section   = vsection; // real config section.
					pr.value_component  = value;
					pr.description      = vdesc;
					if (is_enum)
					{
						pr.is_enum     = true;
						pr.enum_values = std::move(enum_values);
						pr.enum_labels = std::move(enum_labels);
					}
					else if (is_stepper)
					{
						pr.is_slider          = built_slider;
						pr.is_stepper         = !built_slider;
						pr.stepper_min        = vmeta->min;
						pr.stepper_max        = vmeta->max;
						pr.stepper_step       = step;
						pr.show_as_percentage = vmeta->show_as_percentage;
						pr.is_percentage      = vmeta->is_percentage;
					}
					else if (is_bool)
					{
						pr.is_toggle    = true;
						pr.toggle_value = vv.as_bool; // fallback when get() is nil.
					}
					g_rows.push_back(pr);
				}
				continue;
			}

			if (it.is_group)
			{
				std::string glabel;
				std::string gdescription;
				std::string gdisabled_description;
				bool group_disabled = false;
				if (it.is_author_group)
				{
					const std::string gname = resolve_localized(it.author_name);
					glabel                  = escape_markup(!gname.empty() ? gname : key_to_display(it.key));
					gdescription            = resolve_localized(it.author_description);
					gdisabled_description   = resolve_localized(it.author_disabled_description);
					group_disabled          = it.author_disabled;
				}
				else
				{
					auto gmeta = resolved_metadata(stem, section, it.key);

					// A group's desc table can also describe config children of the same name.
					if (gmeta && view_cfg)
					{
						if (config_child_exists(view_cfg, it.child_section, "displayName"))
						{
							gmeta->name.clear();
						}
						if (config_child_exists(view_cfg, it.child_section, "description"))
						{
							gmeta->description.clear();
						}
						if (config_child_exists(view_cfg, it.child_section, "hidden"))
						{
							gmeta->hidden = false;
						}
						if (config_child_exists(view_cfg, it.child_section, "disabled"))
						{
							gmeta->disabled = false;
						}
					}

					if (gmeta && gmeta->hidden)
					{
						continue;
					}
					const std::string gname = gmeta ? resolve_localized(gmeta->name) : std::string{};
					glabel                  = escape_markup(!gname.empty() ? gname : key_to_display(it.key));
					gdescription            = gmeta ? resolve_localized(gmeta->description) : std::string{};
					gdisabled_description   = gmeta ? resolve_localized(gmeta->disabled_description) : std::string{};
					group_disabled          = gmeta && gmeta->disabled;
				}

				// Context-blocked categories are greyed and cannot be entered.
				const bool ctx_blocked = is_context_restricted(it.group_context);

				// Soft-disabled groups stay hoverable for their notes, while mod-off groups are inert.
				const bool greyed = disabled || group_disabled || ctx_blocked;
				if (auto* row = make_text_row(screen, glabel.c_str(), greyed, /*block_input*/ disabled))
				{
					PanelRow pr{row, RowKind::group, stem, {}};
					pr.disabled       = greyed;
					pr.target_section = it.child_section;

					if (ctx_blocked)
					{
						pr.description = note_then_description(context_note(it.group_context), gdescription);
					}
					else
					{
						pr.description = (group_disabled && !gdisabled_description.empty()) ? gdisabled_description : gdescription;
					}
					g_rows.push_back(std::move(pr));
				}
				continue;
			}

			const std::string& key = it.key;
			auto* entry            = it.entry;

			const auto meta = resolved_metadata(stem, entry->m_definition.m_section, entry->m_definition.m_key);
			if (meta && meta->hidden)
			{
				continue;
			}

			// Author-disabled rows are visible but read-only and greyed.
			const bool author_disabled = meta && meta->disabled;
			const std::string mname    = meta ? resolve_localized(meta->name) : std::string{};
			const std::string label    = escape_markup(!mname.empty() ? mname : key_to_display(key));

			const bool is_number  = entry->type() == typeid(double);
			const bool is_enum    = meta && !meta->values.empty();
			const bool is_stepper = !is_enum && is_number && meta && meta->has_min && meta->has_max;
			const double step     = (meta && meta->has_step) ? meta->step : 1.0;

			std::vector<std::string> enum_values;
			std::vector<std::string> enum_labels;
			int enum_index = 0;
			if (is_enum)
			{
				enum_values = meta->values;

				if (meta->labels.size() == enum_values.size())
				{
					for (const auto& lbl : meta->labels)
					{
						enum_labels.push_back(resolve_localized(lbl));
					}
				}
				else
				{
					enum_labels = enum_values;
				}
				const std::string cur = entry->get_serialized_value();
				for (int i = 0; i < static_cast<int>(enum_values.size()); ++i)
				{
					if (enum_values[i] == cur)
					{
						enum_index = i;
						break;
					}
				}
			}

			GUIComponent* row   = nullptr;
			GUIComponent* value = nullptr;
			bool built_slider   = false;
			bool built_stepper  = false;
			bool built_enum     = false;
			bool built_toggle   = false;

			const editable_context ctx = effective_editable_context(meta, is_enabled_row);
			const bool context_blocked = is_context_restricted(ctx);
			if (!disabled && (context_blocked || author_disabled))
			{
				// Greyed widgets stay focusable for their descriptions.
				GUIComponent* ro_row   = nullptr;
				GUIComponent* ro_value = nullptr;
				bool ro_is_toggle      = false;
				bool ro_is_enum        = false;
				bool ro_is_numbox      = false;
				bool ro_is_slider      = false;
				if (entry->type() == typeid(bool))
				{
					ro_row = make_toggle_row(screen, label.c_str(), entry->get_value_base<bool>(), /*disabled*/ true, /*block_input*/ false);
					ro_is_toggle = ro_row != nullptr;
				}
				else if (is_enum)
				{
					ro_row = make_numbox_row(screen, label.c_str(), 0.0, static_cast<double>(enum_values.size() - 1), 1.0, static_cast<double>(enum_index), /*disabled*/ true, &enum_labels, /*block_input*/ false);
					ro_is_enum = ro_row != nullptr;
				}
				else if (is_stepper)
				{
					ro_row = make_slider_row(screen, label.c_str(), meta->min, meta->max, step, entry->get_value_base<double>(), meta->show_as_percentage, meta->is_percentage, /*disabled*/ true, /*block_input*/ false);
					ro_is_slider = ro_row != nullptr;
					if (!ro_row)
					{
						ro_row = make_numbox_row(screen, label.c_str(), meta->min, meta->max, step, entry->get_value_base<double>(), /*disabled*/ true, nullptr, /*block_input*/ false);
						ro_is_numbox = ro_row != nullptr;
					}
				}
				if (!ro_row)
				{
					const std::string vtext = truncate_value(entry->get_serialized_value());
					ro_row = make_text_row(screen, label.c_str(), /*disabled*/ true, /*block_input*/ false);
					if (ro_row)
					{
						ro_value = make_value_display(screen, escape_markup(vtext).c_str(), /*disabled*/ true);
					}
				}

				if (ro_row)
				{
					PanelRow pr{ro_row, RowKind::setting, stem, key, entry};
					pr.disabled          = true; // blocks every edit path.
					pr.is_enabled_toggle = is_enabled_row;
					if (ro_is_slider || ro_is_numbox)
					{
						pr.is_slider          = ro_is_slider;
						pr.is_stepper         = ro_is_numbox;
						pr.stepper_min        = meta->min;
						pr.stepper_max        = meta->max;
						pr.stepper_step       = step;
						pr.show_as_percentage = meta->show_as_percentage;
						pr.is_percentage      = meta->is_percentage;
					}
					else if (ro_is_enum)
					{
						pr.is_enum     = true;
						pr.enum_values = enum_values;
						pr.enum_labels = enum_labels;
					}
					else if (ro_is_toggle)
					{
						pr.is_toggle = true;
					}
					else
					{
						pr.value_component = ro_value;
					}

					if (context_blocked)
					{
						pr.description = note_then_description(context_note(ctx), meta ? resolve_localized(meta->description) : std::string{});
					}
					else
					{
						const std::string ddesc = meta ? resolve_localized(meta->disabled_description) : std::string{};
						pr.description = !ddesc.empty() ? ddesc : (meta ? resolve_localized(meta->description) : std::string{});
					}
					g_rows.push_back(pr);
				}
				continue;
			}

			if (entry->type() == typeid(bool))
			{
				row          = make_toggle_row(screen, label.c_str(), entry->get_value_base<bool>(), disabled);
				built_toggle = row != nullptr;
			}
			else if (is_enum)
			{
				row = make_numbox_row(screen, label.c_str(), 0.0, static_cast<double>(enum_values.size() - 1), 1.0, static_cast<double>(enum_index), disabled, &enum_labels);
				built_enum = row != nullptr;
			}
			else if (is_stepper)
			{
				row = make_slider_row(screen, label.c_str(), meta->min, meta->max, step, entry->get_value_base<double>(), meta->show_as_percentage, meta->is_percentage, disabled);
				if (row)
				{
					built_slider = true;
				}
				else
				{
					row = make_numbox_row(screen, label.c_str(), meta->min, meta->max, step, entry->get_value_base<double>(), disabled);
					built_stepper = row != nullptr;
				}
			}
			else
			{
				row = make_text_row(screen, label.c_str(), disabled);
				if (row)
				{
					const std::string v = entry ? entry->get_serialized_value() : std::string{};
					value = make_value_display(screen, escape_markup(truncate_value(v)).c_str(), disabled);
				}
			}

			if (row)
			{
				PanelRow pr{row, RowKind::setting, stem, key, entry};
				pr.disabled          = disabled;
				pr.is_enabled_toggle = is_enabled_row;
				pr.value_component   = value;

				const std::string mdesc = meta ? resolve_localized(meta->description) : std::string{};
				pr.description          = !mdesc.empty() ? mdesc : entry->m_description.m_description;

				if (built_enum)
				{
					pr.is_enum     = true;
					pr.enum_values = std::move(enum_values);
					pr.enum_labels = std::move(enum_labels);
				}
				else if (built_slider || built_stepper)
				{
					pr.is_slider          = built_slider;
					pr.is_stepper         = built_stepper;
					pr.stepper_min        = meta->min;
					pr.stepper_max        = meta->max;
					pr.stepper_step       = step;
					pr.show_as_percentage = meta->show_as_percentage;
					pr.is_percentage      = meta->is_percentage;
				}
				else if (built_toggle)
				{
					pr.is_toggle = true;
				}

				g_rows.push_back(pr);
			}
		}
	}

	static void build_mod_settings(MiscSettingsScreen* screen, const std::string& stem, const std::string& section)
	{
		build_panel_rows(screen, stem, section, collect_panel_items(stem, section));
	}

#pragma endregion

#pragma region Panel sync, focus, and navigation

	// Match native category switching: on-page rows fade in, off-page rows are hidden immediately.
	static void sync_scroll_fade(MiscSettingsScreen* screen)
	{
		const std::size_t first = screen->m_page_start_index;
		const std::size_t last  = first + rows_per_page;
		for (std::size_t i = 0; i < g_rows.size(); ++i)
		{
			auto* comp = g_rows[i].component;
			if (comp && !(i >= first && i < last))
			{
				comp->m_fade_opacity = 0.0f;
				comp->m_fade_target  = 0.0f;
			}
		}
	}

	// Value displays are outside mOptions, so mirror the key row's position and fade.
	static void sync_value_columns()
	{
		for (const auto& row : g_rows)
		{
			GUIComponent* key   = row.component;
			GUIComponent* value = row.value_component;
			if (!key || !value)
			{
				continue;
			}
			value->m_location_x   = key->m_location_x;
			value->m_location_y   = key->m_location_y;
			value->m_fade_opacity = key->m_fade_opacity;
			value->m_fade_target  = key->m_fade_target;
			value->m_hidden       = key->m_hidden;
		}
	}

	static GUIComponent* active_row_component(MiscSettingsScreen* screen)
	{
		auto* menu = reinterpret_cast<MenuScreen*>(screen);
		return menu->m_mouse_over_component ? menu->m_mouse_over_component : menu->m_selected_component;
	}

	static PanelRow* find_row(GUIComponent* comp)
	{
		if (!comp)
		{
			return nullptr;
		}
		for (auto& row : g_rows)
		{
			if (row.component == comp)
			{
				return &row;
			}
		}
		return nullptr;
	}

	static std::string row_config_section_of(const PanelRow& r)
	{
		if (r.entry)
		{
			return r.entry->m_definition.m_section;
		}
		if (!r.config_section.empty())
		{
			return r.config_section;
		}
		return r.target_section;
	}

	static RowIdentity row_identity_of(const PanelRow& r)
	{
		return RowIdentity{true, r.kind, r.stem, r.target_section, r.setting_key, row_config_section_of(r)};
	}

	// Re-finds a selectable row after an instant rebuild.
	static GUIComponent* find_row_by_identity(const RowIdentity& id)
	{
		if (!id.valid)
		{
			return nullptr;
		}
		for (const auto& row : g_rows)
		{
			GUIComponent* c = row.component;
			if (c && row.kind == id.kind && row.stem == id.stem && row.target_section == id.section && row.setting_key == id.key && row_config_section_of(row) == id.config_section && !row.disabled && c->m_is_useable && !c->m_hidden)
			{
				return c;
			}
		}
		return nullptr;
	}

	// True while the user is adjusting one of our rows. If the mouse-down probe is absent, hover holds.
	static bool interacting_with_row(MiscSettingsScreen* screen, void* input)
	{
		if (screen->m_component_focused && find_row(screen->m_component_focused))
		{
			return true;
		}
		auto* menu = reinterpret_cast<MenuScreen*>(screen);
		if (!menu->m_mouse_over_component || !find_row(menu->m_mouse_over_component))
		{
			return false;
		}
		return g_mouse_button_down ? g_mouse_button_down(input) : true;
	}

	static GUIComponent* g_last_description_component = nullptr;

	static void sync_description_box(MiscSettingsScreen* screen)
	{
		if (!g_show_text || !screen->m_description_box)
		{
			return;
		}
		auto* box = screen->m_description_box;

		GUIComponent* active = active_row_component(screen);

		const std::string* description = nullptr;
		if (PanelRow* row = find_row(active))
		{
			description = &row->description;
		}
		const bool show = description && !description->empty();

		if (active != g_last_description_component)
		{
			g_last_description_component = active;

			std::string shown;
			if (show)
			{
				shown = escape_markup(*description);
				for (std::size_t pos = 0; (pos = shown.find('\n', pos)) != std::string::npos; pos += 4)
				{
					shown.replace(pos, 1, " \\n ");
				}
			}
			g_show_text(box, shown.c_str());

			// ShowText only marks lines dirty, so force layout now to avoid a first-frame jump.
			if (show && g_get_lines)
			{
				g_get_lines(box);
			}
		}

		// Native Update runs before this and re-hides the box.
		box->m_fade_opacity = show ? 1.0f : 0.0f;
		box->m_fade_target  = show ? 1.0f : 0.0f;
	}

	static std::string g_prompt_confirm_label;
	static std::string g_prompt_cancel_label;

	// The key glyph comes from the button's bound control, not the label.
	static void set_prompt_label(GUIComponent* button, std::string& cache, const char* text)
	{
		if (!button || !g_set_label || cache == text)
		{
			return;
		}
		cache.assign(text);
		g_set_label(button, text);
	}

	// Retunes bottom prompts for the Mods tab and clears caches off-tab.
	static void sync_prompts(MiscSettingsScreen* screen, bool on_mods_tab)
	{
		if (!on_mods_tab)
		{
			g_prompt_confirm_label.clear();
			g_prompt_cancel_label.clear();
			return;
		}

		auto* menu = reinterpret_cast<MenuScreen*>(screen);

		const char* cancel = g_editing ? "{CN} CANCEL" : (g_view == View::mod_settings ? "{CN} BACK" : "{CN} EXIT");
		set_prompt_label(menu->m_cancel_button, g_prompt_cancel_label, cancel);

		std::string confirm;
		if (g_editing)
		{
			confirm = "{SL} SUBMIT";
		}
		else if (PanelRow* row = find_row(active_row_component(screen)))
		{
			if (row->disabled)
			{
				confirm.clear();
			}
			else
			{
				switch (row->kind)
				{
				case RowKind::mod_entry: confirm = "{SL} SELECT"; break;
				case RowKind::group:     confirm = "{SL} SELECT"; break;
				case RowKind::setting:
					if (row->is_toggle)
					{
						confirm = "{SL} TOGGLE";
					}
					else if (row->is_enum)
					{
						confirm = "{SL} SET";
					}
					else if (row->is_slider)
					{
						confirm = "{SL} SET";
					}
					else if (row->is_stepper)
					{
						confirm = "{SL} SELECT";
					}
					else
					{
						confirm = "{SL} EDIT";
					}
					break;
				case RowKind::action: confirm = "{SL} SELECT"; break;
				}
			}
		}

		// Native OnOptionMouseOver never fires for our custom rows.
		if (menu->m_confirm_button)
		{
			if (confirm.empty())
			{
				menu->m_confirm_button->m_fade_opacity = 0.0f;
				menu->m_confirm_button->m_fade_target  = 0.0f;
			}
			else
			{
				set_prompt_label(menu->m_confirm_button, g_prompt_confirm_label, confirm.c_str());
				menu->m_confirm_button->m_hidden       = false;
				menu->m_confirm_button->m_fade_opacity = 1.0f;
				menu->m_confirm_button->m_fade_target  = 1.0f;
			}
		}

		// Hide Reset outside a single mod's settings and while editing.
		if (screen->m_defaults_button)
		{
			const bool show_reset               = (g_view == View::mod_settings) && !g_editing;
			screen->m_defaults_button->m_hidden = !show_reset;
		}
	}

	// DoShowCategory focuses rows only when the option list is already populated.
	static void focus_row(MiscSettingsScreen* screen, GUIComponent* component)
	{
		if (!g_teleport_cursor || (g_use_mouse && *g_use_mouse) || !component)
		{
			return;
		}
		g_teleport_cursor(screen, component); // next Update focuses it.
		screen->m_category_focused = false;   // hand nav from tabs to rows.
	}

	static void focus_first_row(MiscSettingsScreen* screen)
	{
		if (!g_teleport_cursor || (g_use_mouse && *g_use_mouse))
		{
			return;
		}
		for (const auto& row : g_rows)
		{
			GUIComponent* c = row.component;
			if (c && !row.disabled && c->m_is_useable && !c->m_hidden)
			{
				focus_row(screen, c);
				return;
			}
		}
	}

	// Native page scroll selects the edge row directly, ignoring mFreeFormSelectable.
	static void redirect_page_landing(MiscSettingsScreen* screen, bool going_down)
	{
		if (!g_set_mouse_over || !g_teleport_cursor || (g_use_mouse && *g_use_mouse) || g_rows.empty())
		{
			return;
		}
		const std::size_t page_start = screen->m_page_start_index;
		if (page_start >= g_rows.size())
		{
			return;
		}
		const std::size_t page_end = std::min(page_start + rows_per_page, g_rows.size());

		const auto eligible = [](const PanelRow& r)
		{
			return r.component && !r.disabled && r.component->m_is_useable && !r.component->m_hidden;
		};

		GUIComponent* target = nullptr;
		if (going_down)
		{
			for (std::size_t i = page_start; i < page_end; ++i)
			{
				if (eligible(g_rows[i]))
				{
					target = g_rows[i].component;
					break;
				}
			}
		}
		else
		{
			for (std::size_t i = page_end; i-- > page_start;)
			{
				if (eligible(g_rows[i]))
				{
					target = g_rows[i].component;
					break;
				}
			}
		}

		auto* menu = reinterpret_cast<MenuScreen*>(screen);
		if (!target || menu->m_mouse_over_component == target)
		{
			return;
		}

		g_set_mouse_over(screen, target);
		g_teleport_cursor(screen, target);
		screen->m_category_focused = false;
	}

	static GUIComponent* restore_target_row(const NavRestore& r)
	{
		for (const auto& row : g_rows)
		{
			if (!row.component)
			{
				continue;
			}
			const bool match =
			    !r.focus_stem.empty() ? (row.kind == RowKind::mod_entry && row.stem == r.focus_stem) : (!r.focus_section.empty() && row.kind == RowKind::group && row.target_section == r.focus_section);
			if (match)
			{
				return row.component;
			}
		}
		return nullptr;
	}

	static void request_back_nav()
	{
		const auto dot = g_view_section.rfind('.');
		if (dot != std::string::npos)
		{
			g_pending_view    = View::mod_settings;
			g_pending_stem    = g_view_stem;
			g_pending_section = g_view_section.substr(0, dot);
		}
		else
		{
			g_pending_view = View::mod_list;
			g_pending_stem.clear();
			g_pending_section.clear();
		}
		g_nav_pending = true;
	}

	// Bit 0x4 of a control's state is "was pressed".
	static bool control_pressed(void* input, const void* control)
	{
		if (!input || !g_input_get_state || !control)
		{
			return false;
		}
		return (g_input_get_state(input, control) & 0x4u) != 0;
	}

	static void reassert_keep_active_row(MiscSettingsScreen* screen)
	{
		if (!(g_use_mouse && *g_use_mouse))
		{
			return;
		}
		GUIComponent* keep = find_row_by_identity(g_keep_active_row);
		if (!keep)
		{
			return;
		}
		auto* menu                   = reinterpret_cast<MenuScreen*>(screen);
		menu->m_mouse_over_component = keep;
		menu->m_selected_component   = keep;

		g_prompt_confirm_label.clear();
		g_prompt_cancel_label.clear();
		g_last_description_component = nullptr;
	}

	// Calls an engine virtual by byte offset so native highlight reverts run fully.
	static void call_component_vfn(GUIComponent* comp, std::size_t vtable_byte_offset)
	{
		char* vtable = *reinterpret_cast<char**>(comp);
		void* fn     = *reinterpret_cast<void**>(vtable + vtable_byte_offset);
		reinterpret_cast<void (*)(void*)>(fn)(comp);
	}

	static void set_component_location(GUIComponent* comp, float x, float y)
	{
		char* vtable = *reinterpret_cast<char**>(comp);
		auto fn      = *reinterpret_cast<void (**)(void*, std::uint64_t)>(vtable + vtable_set_location_offset);
		std::uint32_t xb, yb;
		std::memcpy(&xb, &x, sizeof xb);
		std::memcpy(&yb, &y, sizeof yb);
		fn(comp, (static_cast<std::uint64_t>(yb) << 32) | xb);
	}

	// Sliders revert through OnMouseOff, num-boxes through OnUnselected.
	static void clear_stale_widget_highlight(MiscSettingsScreen* screen)
	{
		auto* menu            = reinterpret_cast<MenuScreen*>(screen);
		const bool mouse_mode = g_use_mouse && *g_use_mouse;
		for (const auto& row : g_rows)
		{
			if (!row.component)
			{
				continue;
			}
			char* s = reinterpret_cast<char*>(row.component);

			if (row.is_slider)
			{
				if (row.disabled || row.component != menu->m_mouse_over_component)
				{
					if (auto* label = *reinterpret_cast<char**>(s + slider_label_offset); label && *reinterpret_cast<bool*>(label + textbox_use_selected_color_off))
					{
						call_component_vfn(row.component, vtable_on_mouse_off_offset);
					}
				}

				if ((row.disabled || row.component != screen->m_component_focused) && *reinterpret_cast<bool*>(s + slider_focused_offset))
				{
					call_component_vfn(row.component, vtable_on_focus_off_offset);
				}
			}
			else if ((row.is_enum || row.is_stepper) && (mouse_mode || row.disabled))
			{
				if (row.disabled || row.component != menu->m_mouse_over_component)
				{
					if (auto* label = *reinterpret_cast<char**>(s + numbox_label_text_offset); label && *reinterpret_cast<bool*>(label + textbox_use_selected_color_off))
					{
						call_component_vfn(row.component, vtable_on_unselected_offset);
					}
				}
			}
		}
	}

	static void keep_disabled_labels_grey()
	{
		const auto grey_label = [](char* base, std::size_t tb_offset)
		{
			auto* tb = *reinterpret_cast<void**>(base + tb_offset);
			if (!tb)
			{
				return;
			}
			char* vtable = *reinterpret_cast<char**>(tb);
			auto fn      = *reinterpret_cast<void (**)(void*, std::uint32_t)>(vtable + vtable_set_text_color_offset);
			fn(tb, disabled_label_grey_packed);
		};
		for (const auto& row : g_rows)
		{
			if (!row.disabled || !row.component)
			{
				continue;
			}
			char* s = reinterpret_cast<char*>(row.component);
			if (row.is_slider)
			{
				grey_label(s, slider_label_offset);
				grey_label(s, slider_value_text_offset);
			}
			else if (row.is_enum || row.is_stepper)
			{
				grey_label(s, numbox_label_text_offset);
				grey_label(s, numbox_value_text_offset);
			}
			else if (row.is_toggle)
			{
				grey_label(s, button_label_offset);
			}
		}
	}

	// mFreeFormSelectable makes spatial nav skip disabled rows without blocking mouse hover.
	static void apply_row_freeform_selectability()
	{
		for (const auto& row : g_rows)
		{
			if (!row.disabled)
			{
				continue;
			}
			if (row.component)
			{
				*reinterpret_cast<bool*>(reinterpret_cast<char*>(row.component) + component_free_form_selectable_offset) = false;
			}
			if (row.value_component)
			{
				*reinterpret_cast<bool*>(reinterpret_cast<char*>(row.value_component) + component_free_form_selectable_offset) = false;
			}
		}
	}

	static void build_panel(MiscSettingsScreen* screen, bool instant = false)
	{
		g_last_description_component = nullptr;

		// In-place refreshes preserve the current scroll offset.
		const std::uint32_t prev_start = screen->m_page_start_index;

		// Same-view rebuilds recreate rows, so remember the keyboard/controller cursor row.
		RowKind cursor_kind = RowKind::mod_entry;
		std::string cursor_key;
		bool had_cursor = false;
		if (instant && !(g_use_mouse && *g_use_mouse))
		{
			auto* menu = reinterpret_cast<MenuScreen*>(screen);
			GUIComponent* target = screen->m_component_focused ? screen->m_component_focused : menu->m_selected_component;
			if (const PanelRow* fr = find_row(target); fr && !fr->setting_key.empty())
			{
				cursor_kind = fr->kind;
				cursor_key  = fr->setting_key;
				had_cursor  = true;
			}
		}

		destroy_rows(screen);

		// "Blank" only hashes correctly once the string-intern table is ready.
		if (!g_blank_graphic && g_hash_lookup)
		{
			HashGuid res{};
			g_hash_lookup(&res, "Blank", 5);
			g_blank_graphic = res.m_id;
		}

		g_view_has_dynamic = false;

		g_dynamic_refresh_settle = 0.0f;

		if (g_view == View::mod_settings && !g_view_stem.empty())
		{
			build_mod_settings(screen, g_view_stem, g_view_section.empty() ? root_section : g_view_section);
		}
		else
		{
			build_mod_list(screen);
		}

		// Let the engine position, paginate and drive the scrollbar and arrows.
		const bool restoring = !instant && g_has_pending_restore;

		std::uint32_t start = 0;
		if (instant || restoring)
		{
			// Clamp only when the saved offset now points past the last row.
			const std::uint32_t row_count       = static_cast<std::uint32_t>(g_rows.size());
			const std::uint32_t last_page_start = row_count > 0 ? ((row_count - 1) / rows_per_page) * rows_per_page : 0;
			const std::uint32_t desired         = instant ? prev_start : g_pending_restore.scroll_index;
			start                               = desired > last_page_start ? last_page_start : desired;
		}
		screen->m_page_start_index = start;
		screen->m_options_per_page = rows_per_page;
		if (g_update_scroll)
		{
			g_update_scroll(screen); // sets each row's mFadeTarget.
		}

		if (instant)
		{
			// Snap in-place refreshes to their final visibility so the panel does not flash.
			for (const auto& row : g_rows)
			{
				if (row.component)
				{
					row.component->m_fade_opacity = row.component->m_fade_target;
				}
			}
		}

		sync_value_columns();

		apply_row_freeform_selectability();

		// Real view changes focus the first row, while in-place refreshes keep focus.
		if (!instant)
		{
			GUIComponent* restore_focus = restoring ? restore_target_row(g_pending_restore) : nullptr;
			if (restore_focus)
			{
				focus_row(screen, restore_focus);
			}
			else
			{
				focus_first_row(screen);
			}
		}
		else if (had_cursor)
		{
			for (const auto& row : g_rows)
			{
				if (row.component && row.kind == cursor_kind && row.setting_key == cursor_key && !row.disabled
				    && row.component->m_is_useable && !row.component->m_hidden)
				{
					focus_row(screen, row.component);
					break;
				}
			}
		}

		if (instant && g_keep_active_row.valid && g_use_mouse && *g_use_mouse)
		{
			g_keep_active_frames = keep_active_frame_count;
		}
		else
		{
			g_keep_active_row.valid = false;
			g_keep_active_frames    = 0;
		}

		if (restoring)
		{
			g_has_pending_restore = false;
		}
	}

	static void apply_nav(MiscSettingsScreen* screen)
	{
		const bool instant = (g_pending_view == g_view) && (g_pending_stem == g_view_stem) && (g_pending_section == g_view_section);

		const bool drilling_in =
		    (g_view == View::mod_list && g_pending_view == View::mod_settings)
		    || (g_view == View::mod_settings && g_pending_view == View::mod_settings && g_pending_section.rfind(g_view_section + ".", 0) == 0);
		const bool backing_out =
		    (g_view == View::mod_settings && g_pending_view == View::mod_list)
		    || (g_view == View::mod_settings && g_pending_view == View::mod_settings && g_view_section.rfind(g_pending_section + ".", 0) == 0);

		if (drilling_in)
		{
			NavRestore r;
			r.scroll_index = screen->m_page_start_index;
			if (g_view == View::mod_list)
			{
				r.focus_stem = g_pending_stem;
			}
			else
			{
				r.focus_section = g_pending_section;
			}
			g_nav_stack.push_back(std::move(r));
		}
		else if (backing_out && !g_nav_stack.empty())
		{
			g_pending_restore = g_nav_stack.back();
			g_nav_stack.pop_back();
			g_has_pending_restore = true;
		}

		g_view         = g_pending_view;
		g_view_stem    = g_pending_stem;
		g_view_section = g_pending_section;
		build_panel(screen, instant);
	}

#pragma endregion

#pragma region Reset to defaults

	// Reads the serialized default from write_description for set_serialized_value.
	static std::optional<std::string> entry_default_serialized(toml_v2::config_file::config_entry_base* entry)
	{
		if (!entry)
		{
			return std::nullopt;
		}
		std::ostringstream ss;
		entry->write_description(ss);
		const std::string text          = ss.str();
		static const std::string marker = "# Default value: ";
		const auto pos                  = text.rfind(marker);
		if (pos == std::string::npos)
		{
			return std::nullopt;
		}
		return text.substr(pos + marker.size());
	}

	// Reset affects only entries whose menu path is within the current view.
	static bool reset_settings_to_defaults()
	{
		bool any_changed                            = false;
		const std::vector<menu_group> author_groups = mod_menu_groups(g_view_stem);
		toml_v2::config_file* mod_cfg               = nullptr; // for virtual-row path resolution.
		for (auto* cfg : toml_v2::config_file::g_config_files)
		{
			if (!cfg || cfg->m_config_file_stem_as_str.empty() || cfg->m_config_file_stem_as_str != g_view_stem
			    || cfg != live_config_file(g_view_stem))
			{
				continue;
			}
			if (!mod_cfg)
			{
				mod_cfg = cfg;
			}
			const std::string& guid = cfg->m_config_file_stem_as_str;
			for (auto& [def, entry] : cfg->m_entries)
			{
				if (!entry)
				{
					continue;
				}
				auto* e = entry.get();

				// Reset must not touch keys the mod no longer declares, matching what the menu shows.
				if (!setting_is_declared(guid, def.m_section, def.m_key))
				{
					continue;
				}

				const bool is_enabled_toggle = def.m_section == root_section && e->type() == typeid(bool) && is_enabled_key(def.m_key);
				const bool desc_exempt       = is_enabled_toggle && mod_declares_settings(guid);
				if (!desc_exempt && !setting_is_described(guid, def.m_section, def.m_key) && !entry_has_description(e))
				{
					continue;
				}

				const auto static_meta             = get_setting_metadata(guid, def.m_section, def.m_key);
				const std::vector<std::string> grp = static_meta ? static_meta->group : std::vector<std::string>{};
				const std::string mpath = resolve_entry_menu_path(guid, author_groups, cfg, def.m_section, grp);
				if (!menu_path_in_scope(mpath, g_view_section))
				{
					continue;
				}

				auto def_val = get_setting_default(guid, def.m_section, def.m_key);
				if (!def_val)
				{
					// Chalk mods recover defaults from the config entry itself.
					def_val = entry_default_serialized(e);
				}
				if (!def_val || e->get_serialized_value() == *def_val)
				{
					continue;
				}
				capture_restart_baseline(e);
				e->set_serialized_value(*def_val); // auto-saves + fires on_setting_changed.
				note_change_if_restart_required(e, e->get_serialized_value());
				any_changed = true;
			}
		}

		// Interactive virtual rows with a `default` are reset through set().
		for (const auto& vr : get_virtual_rows(g_view_stem, ""))
		{
			if (!vr.interactive)
			{
				continue;
			}
			const std::string mpath = resolve_entry_menu_path(g_view_stem, author_groups, mod_cfg, vr.section, vr.group);
			if (!menu_path_in_scope(mpath, g_view_section))
			{
				continue;
			}
			if (reset_virtual_row_to_default(g_view_stem, vr.section, vr.key))
			{
				any_changed = true;
			}
		}
		return any_changed;
	}

	static void perform_reset()
	{
		const bool changed = reset_settings_to_defaults();
		if (changed && g_view == View::mod_settings)
		{
			g_pending_view    = g_view;
			g_pending_stem    = g_view_stem;
			g_pending_section = g_view_section;
			g_nav_pending     = true;
		}
	}

#pragma endregion

#pragma region Native dialogs and dependency checks

	// CJK fonts draw U+00A0 as '*', so use regular spaces and a U+3000 blank.
	static bool current_language_is_cjk()
	{
		const std::string code = current_language_code();
		return code.rfind("zh", 0) == 0 || code.rfind("ja", 0) == 0 || code.rfind("ko", 0) == 0;
	}

	// Blank characters must survive ShowText's ASCII-whitespace-line trim.
	static std::string build_list_message(const std::string& intro, const std::vector<std::string>& lines, const std::string& outro)
	{
		const bool cjk          = current_language_is_cjk();
		const std::string blank = cjk ? "\xE3\x80\x80" : "\xC2\xA0"; // U+3000 or U+00A0.

		const auto spaced = [cjk](const std::string& s) -> std::string
		{
			if (cjk)
			{
				return s;
			}
			std::string out;
			out.reserve(s.size() + s.size() / 4);
			for (char c : s)
			{
				out += (c == ' ') ? std::string("\xC2\xA0") : std::string(1, c);
			}
			return out;
		};

		std::string msg  = spaced(intro);
		msg             += "\n" + blank + "\n";
		for (const auto& line : lines)
		{
			msg += spaced(line);
			msg += "\n";
		}
		msg += blank + "\n";
		msg += spaced(outro);
		msg += "\n" + blank;
		return msg;
	}

	static std::string build_restart_message()
	{
		std::vector<std::string> lines;
		lines.reserve(g_restart_changes.size());
		for (const auto& change : g_restart_changes)
		{
			lines.push_back(change.second);
		}
		return build_list_message("A restart is required because you changed these settings:", lines, "The game will now close. Please restart it to apply the changes.");
	}

	// Builds an EASTL SSO string.
	static void make_eastl_sso(char* buf, const char* text)
	{
		std::size_t n = std::strlen(text);
		if (n > 22)
		{
			n = 22;
		}
		std::memset(buf, 0, 24);
		std::memcpy(buf, text, n);
		buf[23] = static_cast<char>(23 - n);
	}

	// Forced restart skips OnExit, so SaveProfile must flush native Options settings first.
	static void flush_native_settings()
	{
		if (g_save_profile && g_active_profile)
		{
			g_save_profile(g_active_profile, false, false);
		}
	}

	// Captures the confirm button only for the forced-restart dialog.
	static bool show_message_dialog(void* screen_manager, const char* title, const std::string& message, bool confirm_closes_game)
	{
		if (screen_manager && g_message_dialog_ctor && g_add_screen)
		{
			// ScreenManager frees dialogs with the game's CRT heap.
			void* dialog = game_alloc(message_dialog_size);
			if (dialog)
			{
				std::memset(dialog, 0, message_dialog_size);

				char empty_message[24];
				make_eastl_sso(empty_message, "");
				g_message_dialog_ctor(dialog, screen_manager, empty_message);

				auto* bytes = reinterpret_cast<char*>(dialog);

				bytes[screen_removed_offset]     = 0;
				bytes[screen_visible_offset]     = 1;
				bytes[screen_block_input_offset] = 1;

				if (g_show_text)
				{
					if (auto* title_box = *reinterpret_cast<void**>(bytes + dialog_title_offset))
					{
						g_show_text(title_box, title);
					}
					if (auto* message_box = *reinterpret_cast<GUIComponent**>(bytes + dialog_message_offset))
					{
						char* handle = reinterpret_cast<char*>(message_box) + textbox_font_handle_offset;
						*reinterpret_cast<float*>(handle + font_handle_size_ratio_offset) *= restart_message_font_scale;
						*reinterpret_cast<float*>(handle + font_handle_eng_size_ratio_offset) *= restart_message_font_scale;

						const std::string shown = escape_markup(message);
						g_show_text(message_box, shown.c_str());
					}
				}

				// Remember the dialog so OnClicked can verify ownership before terminating.
				if (confirm_closes_game)
				{
					g_restart_confirm_button = *reinterpret_cast<GUIComponent**>(bytes + dialog_confirm_button_offset);
					g_restart_dialog         = dialog;
				}

				char empty_name[24];
				make_eastl_sso(empty_name, "");
				g_add_screen(screen_manager, dialog, true, empty_name);
				return true;
			}
		}

		return false;
	}

	static bool show_restart_dialog(void* screen_manager, const std::string& message)
	{
		return show_message_dialog(screen_manager, "Restart Required", message, /*confirm_closes_game*/ true);
	}

	static bool show_dependency_dialog(void* screen_manager, const std::string& message)
	{
		return show_message_dialog(screen_manager, "Cannot Disable Mod", message, /*confirm_closes_game*/ false);
	}

	static bool mod_is_enabled(const std::string& guid)
	{
		if (auto* cfg = live_config_file(guid))
		{
			for (auto& [key, entry] : cfg->m_entries)
			{
				if (!entry || key.m_section != root_section || entry->type() != typeid(bool) || !is_enabled_key(key.m_key))
				{
					continue;
				}
				// A stale toggle the menu does not show must not decide whether the mod counts as disabled,
				// otherwise every row greys out with no way to recover.
				if (!setting_is_declared(guid, key.m_section, key.m_key)
				    || (!mod_declares_settings(guid) && !setting_is_described(guid, key.m_section, key.m_key)
				        && !entry_has_description(entry.get())))
				{
					continue;
				}
				return entry->get_value_base<bool>();
			}
		}
		return true;
	}

	static std::vector<std::string> active_dependents_of(const std::string& stem)
	{
		std::vector<std::string> result;
		if (!big::g_lua_manager)
		{
			return result;
		}
		std::scoped_lock guard(big::g_lua_manager->m_module_lock);
		for (const auto& module : big::g_lua_manager->m_modules)
		{
			if (!module)
			{
				continue;
			}
			const auto& deps = module->manifest().dependencies_no_version_number;
			if (std::find(deps.begin(), deps.end(), stem) == deps.end())
			{
				continue;
			}
			if (!mod_is_enabled(module->guid()))
			{
				continue;
			}
			result.push_back(display_name_from_stem(module->guid()));
		}
		std::sort(result.begin(), result.end());
		return result;
	}

	static std::string build_dependency_message(const std::vector<std::string>& dependents)
	{
		return build_list_message("These enabled mods depend on this one:", dependents, "Disable them first to disable this mod.");
	}

#pragma endregion

#pragma region Engine hooks

	static void* hook_MiscSettingsScreen_ctor(void* self, void* screen_manager, void* opened_from, void* profile_name)
	{
		// The original ctor may build our panel via DoShowCategory.
		g_rows.clear();
		g_view = View::mod_list;
		g_view_stem.clear();
		g_view_section.clear();
		g_pending_section.clear();
		g_nav_pending = false;
		g_nav_stack.clear();
		g_has_pending_restore    = false;
		g_restart_required       = false;
		g_restart_prompt_shown   = false;
		g_restart_confirm_button = nullptr;
		g_restart_dialog         = nullptr;
		g_restart_changes.clear();
		g_restart_baselines.clear();
		g_last_description_component = nullptr;
		g_prompt_confirm_label.clear();
		g_prompt_cancel_label.clear();
		exit_edit_mode();

		// Must be set before the original ctor may build our panel.
		g_opened_in_game      = opener_indicates_in_game(opened_from);
		g_in_hub              = game_is_in_hub();
		g_options_screen_open = true;

		auto* screen = static_cast<MiscSettingsScreen*>(big::g_hooking->get_original<hook_MiscSettingsScreen_ctor>()(self, screen_manager, opened_from, profile_name));

		if (!mods_category_button(screen))
		{
			LOG(WARNING) << "[mod_settings] no reusable category button; Mods tab not installed";
			return screen;
		}

		show_mods_tab(screen);
		return screen;
	}

	static void* hook_MiscSettingsScreen_DoShowCategory(void* self, void* category_button, std::uint32_t category_flag)
	{
		auto* screen = static_cast<MiscSettingsScreen*>(self);
		const bool is_mods_tab = category_button && category_button == reinterpret_cast<void*>(screen->m_editor_options_button);

		// Tear down rows before the native category switch so mComponents stays clean.
		if (!is_mods_tab && !g_rows.empty())
		{
			destroy_rows(screen);
			exit_edit_mode();
		}

		auto* result = big::g_hooking->get_original<hook_MiscSettingsScreen_DoShowCategory>()(self, category_button, category_flag);

		show_mods_tab(screen);

		if (is_mods_tab)
		{
			g_view = View::mod_list;
			g_view_stem.clear();
			g_view_section.clear();
			g_nav_pending = false;
			g_nav_stack.clear();
			g_has_pending_restore = false;
			exit_edit_mode();
			build_panel(screen);
		}

		return result;
	}

	static void hook_GUIComponentNumBox_SetNumberValue(void* self, float value, bool notify)
	{
		big::g_hooking->get_original<hook_GUIComponentNumBox_SetNumberValue>()(self, value, notify);

		if (!notify || !self)
		{
			return;
		}

		PanelRow* row = find_row(reinterpret_cast<GUIComponent*>(self));
		if (!row || (!row->entry && !row->is_virtual_input))
		{
			return;
		}

		if (row->is_enum)
		{
			int idx = static_cast<int>(*reinterpret_cast<float*>(reinterpret_cast<char*>(self) + numbox_value_offset));
			if (idx < 0 || idx >= static_cast<int>(row->enum_values.size()))
			{
				return;
			}
			set_numbox_value_text(reinterpret_cast<GUIComponent*>(self), row->enum_labels[idx].c_str());
			commit_row_serialized(row, row->enum_values[idx], row->enum_labels[idx]);
			return;
		}

		if (!row->is_stepper)
		{
			return;
		}

		const double new_value = static_cast<double>(*reinterpret_cast<float*>(reinterpret_cast<char*>(self) + numbox_value_offset));
		commit_row_number(row, new_value);
	}

	// Leave mFraction continuous so native small deltas can accumulate before snapping on commit.
	static void hook_GUIComponentSlider_SetFraction(void* self, float fraction, bool notify)
	{
		big::g_hooking->get_original<hook_GUIComponentSlider_SetFraction>()(self, fraction, notify);

		if (!notify || !self)
		{
			return;
		}

		PanelRow* row = find_row(reinterpret_cast<GUIComponent*>(self));
		if (!row || !row->is_slider || (!row->entry && !row->is_virtual_input) || row->disabled)
		{
			return;
		}

		const double min_v  = row->stepper_min;
		const double max_v  = row->stepper_max;
		const double step_v = row->stepper_step;
		const double range  = max_v - min_v;

		const float f = *reinterpret_cast<float*>(reinterpret_cast<char*>(self) + slider_fraction_offset);
		double v      = min_v + static_cast<double>(f) * range;
		if (step_v > 0.0 && range > 0.0)
		{
			v = min_v + std::round((v - min_v) / step_v) * step_v;
		}
		if (v < min_v)
		{
			v = min_v;
		}
		else if (v > max_v)
		{
			v = max_v;
		}

		commit_row_number(row, v);

		set_slider_value_text(reinterpret_cast<GUIComponent*>(self),
		                      format_setting_display(v, row->show_as_percentage, row->is_percentage, step_v).c_str());
	}

	// Writes through SetFraction with notify so the SetFraction hook stores and repaints.
	static void step_slider_row(void* slider, PanelRow* row, int dir)
	{
		if (!g_slider_set_fraction || (!row->entry && !row->is_virtual_input))
		{
			return;
		}
		const double min_v  = row->stepper_min;
		const double max_v  = row->stepper_max;
		const double step_v = row->stepper_step > 0.0 ? row->stepper_step : 1.0;
		const double range  = max_v - min_v;
		if (range <= 0.0)
		{
			return;
		}
		const double cur = row->entry ? row->entry->get_value_base<double>() :
		                                get_virtual_value(row->stem, row_io_section(row), row->setting_key).as_number;
		const double idx = std::round((cur - min_v) / step_v);
		double v         = min_v + (idx + dir) * step_v;
		if (v < min_v)
		{
			v = min_v;
		}
		else if (v > max_v)
		{
			v = max_v;
		}
		g_slider_set_fraction(slider, static_cast<float>((v - min_v) / range), true);
	}

	// GUIComponentSlider carries no repeat fields of its own.
	static void* g_slider_repeat_component = nullptr;
	static float g_slider_repeat_timer     = 0.0f;
	static int g_slider_repeat_dir         = 0;

	// Native HandleInput slides mFraction continuously, so keyboard/controller input steps manually.
	static bool hook_GUIComponentSlider_HandleInput(void* self, void* input, float dt)
	{
		PanelRow* row = self ? find_row(reinterpret_cast<GUIComponent*>(self)) : nullptr;
		if (row && row->is_slider)
		{
			if (row->disabled)
			{
				return false; // swallow native drag.
			}
			if ((row->entry || row->is_virtual_input) && !(g_use_mouse && *g_use_mouse) && *reinterpret_cast<bool*>(reinterpret_cast<char*>(self) + slider_focused_offset))
			{
				// Was*Pressed degrades repeat to one step per press when level probes are absent.
				const bool right_down = g_input_is_right_pressed ? g_input_is_right_pressed(input) : g_input_was_right_pressed(input);
				const bool left_down = g_input_is_left_pressed ? g_input_is_left_pressed(input) : g_input_was_left_pressed(input);
				const int dir = right_down ? 1 : (left_down ? -1 : 0);

				if (self != g_slider_repeat_component)
				{
					g_slider_repeat_component = self; // focus moved, restart repeat.
					g_slider_repeat_dir       = 0;
					g_slider_repeat_timer     = 0.0f;
				}

				bool step_now = false;
				if (dir == 0)
				{
					g_slider_repeat_timer = 0.0f;
				}
				else if (dir != g_slider_repeat_dir)
				{
					step_now              = true; // fresh press steps immediately.
					g_slider_repeat_timer = slider_repeat_delay;
				}
				else
				{
					g_slider_repeat_timer -= dt;
					if (g_slider_repeat_timer <= 0.0f)
					{
						step_now              = true;
						g_slider_repeat_timer = slider_repeat_interval;
					}
				}
				g_slider_repeat_dir = dir;

				if (step_now)
				{
					step_slider_row(self, row, dir);
					return true; // claim only frames that moved the value.
				}
				return false; // still block native continuous slide.
			}
		}
		return big::g_hooking->get_original<hook_GUIComponentSlider_HandleInput>()(self, input, dt);
	}

	static bool hook_GUIComponentButton_OnClicked(GUIComponent* self, std::uint64_t location)
	{
		// Re-validate ownership so address reuse cannot trigger a forced restart.
		if (self && self == g_restart_confirm_button && g_restart_dialog && *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + sgg::gui_component_button_owner_offset) == g_restart_dialog)
		{
			big::g_hooking->get_original<hook_GUIComponentButton_OnClicked>()(self, location);
			flush_native_settings();
			TerminateProcess(GetCurrentProcess(), 0);
		}

		PanelRow matched_row;
		bool matched = false;

		if (self)
		{
			for (const auto& row : g_rows)
			{
				if (row.component == self)
				{
					matched_row = row;
					matched     = true;
					break;
				}
			}
		}

		// Stage the native toggle cue before base OnClicked plays mPressSound.
		if (matched && !matched_row.disabled && matched_row.kind == RowKind::setting && matched_row.entry
		    && matched_row.entry->type() == typeid(bool))
		{
			stage_toggle_press_sound(self, !matched_row.entry->get_value_base<bool>());
		}
		else if (matched && !matched_row.disabled && matched_row.kind == RowKind::setting && matched_row.is_virtual_input
		         && matched_row.is_toggle)
		{
			// Fall back to the last-drawn state when get() is nil.
			const auto cur = get_virtual_value(matched_row.stem, row_io_section(&matched_row), matched_row.setting_key);
			const bool cur_on = cur.type == virtual_value::kind::boolean ? cur.as_bool : matched_row.toggle_value;
			stage_toggle_press_sound(self, !cur_on);
		}

		// Disabled rows stay hoverable for notes but must not play press feedback.
		if (matched && matched_row.disabled)
		{
			return false;
		}

		const bool result = big::g_hooking->get_original<hook_GUIComponentButton_OnClicked>()(self, location);

		if (matched && !matched_row.disabled)
		{
			switch (matched_row.kind)
			{
			case RowKind::mod_entry:
				g_pending_view    = View::mod_settings;
				g_pending_stem    = matched_row.stem;
				g_pending_section = root_section;
				g_nav_pending     = true;
				break;
			case RowKind::group:
				g_pending_view    = View::mod_settings;
				g_pending_stem    = matched_row.stem;
				g_pending_section = matched_row.target_section;
				g_nav_pending     = true;
				break;
			case RowKind::setting:
			{
				auto* entry = matched_row.entry;

				if (entry && entry->type() == typeid(bool))
				{
					const bool new_value = !entry->get_value_base<bool>();

					// Block disabling a mod while enabled mods still depend on it.
					if (matched_row.is_enabled_toggle && !new_value)
					{
						const std::vector<std::string> dependents = active_dependents_of(matched_row.stem);
						if (!dependents.empty())
						{
							void* owner = *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + sgg::gui_component_button_owner_offset);
							void* screen_manager = owner ? *reinterpret_cast<void**>(reinterpret_cast<char*>(owner) + screen_manager_offset) : nullptr;
							show_dependency_dialog(screen_manager, build_dependency_message(dependents));
							break; // leave the toggle on.
						}
					}

					// Capture the baseline before the first write so a revert is "no net change".
					capture_restart_baseline(entry);

					entry->set_value_base<bool>(new_value);
					set_toggle_graphic(self, new_value);

					note_change_if_restart_required(entry, new_value ? "on" : "off");

					// Toggling bools can change greying or dynamic rows, so rebuild in place.
					if (matched_row.is_enabled_toggle || g_view_has_dynamic)
					{
						g_pending_view    = View::mod_settings;
						g_pending_stem    = matched_row.stem;
						g_pending_section = g_view_section;
						g_nav_pending     = true;
						g_keep_active_row = row_identity_of(matched_row);
					}
				}
				else if (entry)
				{
					enter_edit_mode(matched_row.value_component, entry);
				}
				else if (matched_row.is_virtual_input && matched_row.is_toggle)
				{
					// Flip through Lua set() and repaint.
					const auto cur = get_virtual_value(matched_row.stem, row_io_section(&matched_row), matched_row.setting_key);
					const bool cur_on = cur.type == virtual_value::kind::boolean ? cur.as_bool : matched_row.toggle_value;
					const bool new_value = !cur_on;
					commit_row_bool(&matched_row, new_value);
					set_toggle_graphic(self, new_value);
				}
				break;
			}
			case RowKind::action:

				// Lua callbacks may change config values or dynamic ranges.
				invoke_action(matched_row.stem, matched_row.target_section, matched_row.setting_key);
				g_pending_view    = View::mod_settings;
				g_pending_stem    = matched_row.stem;
				g_pending_section = g_view_section;
				g_nav_pending     = true;
				g_keep_active_row = row_identity_of(matched_row);
				break;
			}
		}

		return result;
	}

	static void apply_button_spacing(MiscSettingsScreen* screen)
	{
		const std::size_t first = screen->m_page_start_index;
		const std::size_t last  = first + rows_per_page;
		float extra             = 0.0f;
		for (std::size_t i = first; i < last && i < g_rows.size(); ++i)
		{
			GUIComponent* c = g_rows[i].component;
			if (!c)
			{
				continue;
			}
			float shift;
			if (g_rows[i].kind == RowKind::action)
			{
				shift  = extra + button_extra_lead;
				extra += button_extra_lead + button_extra_trail;
			}
			else
			{
				shift = extra;
			}
			if (shift != 0.0f)
			{
				set_component_location(c, c->m_location_x, c->m_location_y + shift);
			}
		}
	}

	// SearchInDirection uses the arrow's free-form eval point, so aim it at the page edge row.
	static void enable_arrow_keyboard_paging(MiscSettingsScreen* screen)
	{
		if (g_rows.empty())
		{
			return;
		}
		const std::size_t first = screen->m_page_start_index;
		if (first >= g_rows.size())
		{
			return;
		}
		const std::size_t last = std::min(first + rows_per_page, g_rows.size()) - 1;

		const auto aim = [](GUIComponent* arrow, GUIComponent* row, float row_delta_y)
		{
			if (!arrow || !row)
			{
				return;
			}
			auto* bytes = reinterpret_cast<char*>(arrow);
			*reinterpret_cast<float*>(bytes + component_free_form_offset_x_offset) = row->m_location_x - arrow->m_location_x;
			*reinterpret_cast<float*>(bytes + component_free_form_offset_y_offset) =
			    (row->m_location_y + row_delta_y) - arrow->m_location_y;
			*reinterpret_cast<bool*>(bytes + component_auto_activate_offset) = true;
		};

		aim(screen->m_down_arrow, g_rows[last].component, row_pitch);
		aim(screen->m_up_arrow, g_rows[first].component, -row_pitch);
	}

	// Runs inside MiscSettingsScreen::Update before row hit-tests.
	static void hook_MiscSettingsScreen_UpdateScrollState(void* self)
	{
		big::g_hooking->get_original<hook_MiscSettingsScreen_UpdateScrollState>()(self);

		auto* screen = static_cast<MiscSettingsScreen*>(self);
		const bool on_mods_tab = screen->m_current_category_button == reinterpret_cast<GUIComponent*>(screen->m_editor_options_button);
		if (on_mods_tab)
		{
			apply_button_spacing(screen);
			enable_arrow_keyboard_paging(screen);
		}
	}

	// RCX=this, XMM1=dt, R8=input. Rebuilds are safe after click/input iteration unwinds.
	static void* hook_MiscSettingsScreen_Update(void* self, float dt, void* input)
	{
		auto* screen = static_cast<MiscSettingsScreen*>(self);
		const bool on_mods_tab = screen->m_current_category_button == reinterpret_cast<GUIComponent*>(screen->m_editor_options_button);

		if (g_editing)
		{
			if (on_mods_tab)
			{
				update_edit_label();
			}
			else
			{
				exit_edit_mode(); // never stay in edit mode off the Mods tab.
			}
		}

		// Dynamic rows rebuild only after debounce and only once the user stops adjusting a row.
		if (g_dynamic_refresh_settle > 0.0f)
		{
			if (!on_mods_tab)
			{
				g_dynamic_refresh_settle = 0.0f;
			}
			else if (interacting_with_row(screen, input))
			{
				g_dynamic_refresh_settle = dynamic_refresh_settle_seconds; // hold until they leave the row.
			}
			else
			{
				g_dynamic_refresh_settle -= dt;
				if (g_dynamic_refresh_settle <= 0.0f)
				{
					g_dynamic_refresh_settle = 0.0f;
					if (!g_nav_pending)
					{
						g_pending_view    = g_view;
						g_pending_stem    = g_view_stem;
						g_pending_section = g_view_section;
						g_nav_pending     = true;

						if (GUIComponent* active = active_row_component(screen))
						{
							if (const PanelRow* fr = find_row(active))
							{
								g_keep_active_row = row_identity_of(*fr);
							}
						}
					}
				}
			}
		}

		if (g_nav_pending)
		{
			if (on_mods_tab)
			{
				// Leaving a mod's settings is the restart-required prompt point.
				const bool leaving_mod = (g_view == View::mod_settings) && (g_pending_view == View::mod_list);
				bool prompted          = false;
				if (leaving_mod && g_restart_required && !g_restart_prompt_shown)
				{
					g_restart_prompt_shown = true;
					void* screen_manager = *reinterpret_cast<void**>(reinterpret_cast<char*>(screen) + screen_manager_offset);
					prompted = show_restart_dialog(screen_manager, build_restart_message());
				}
				if (!prompted)
				{
					apply_nav(screen);
				}
			}
			g_nav_pending = false;
		}

		// Pin the clicked row over the native hover pass for a few frames after rebuild.
		if (g_keep_active_frames > 0 && on_mods_tab)
		{
			reassert_keep_active_row(screen);
			if (--g_keep_active_frames == 0)
			{
				g_keep_active_row.valid = false;
			}
		}

		void* result = big::g_hooking->get_original<hook_MiscSettingsScreen_Update>()(self, dt, input);

		if (on_mods_tab)
		{
			sync_scroll_fade(screen);
			sync_value_columns();
			sync_description_box(screen);
		}

		sync_prompts(screen, on_mods_tab);

		// Widgets clear greyed-label colour from mIsUseable each frame.
		if (on_mods_tab)
		{
			clear_stale_widget_highlight(screen);
			keep_disabled_labels_grey();
		}

		return result;
	}

	// Committing here also swallows a submitting mouse click.
	static bool hook_MiscSettingsScreen_HandleInput(void* self, void* input, float x)
	{
		if (g_editing)
		{
			if (g_was_key_pressed && input)
			{
				if (g_was_key_pressed(input, key_return) || g_was_key_pressed(input, key_kp_enter))
				{
					g_edit_confirm = true;
				}
				if (g_was_key_pressed(input, key_escape))
				{
					g_edit_cancel = true;
				}
			}
			commit_or_cancel_edit();
			return true;
		}

		auto* screen = static_cast<MiscSettingsScreen*>(self);
		const bool on_mods_tab = screen->m_current_category_button == reinterpret_cast<GUIComponent*>(screen->m_editor_options_button);
		if (on_mods_tab && !(g_use_mouse && *g_use_mouse) && !screen->m_component_focused)
		{
			auto* menu = reinterpret_cast<MenuScreen*>(screen);

			if (g_component_focused && control_pressed(input, g_controls_select))
			{
				PanelRow* row = find_row(menu->m_mouse_over_component);
				if (row && !row->disabled && (row->is_slider || row->is_enum))
				{
					g_component_focused(screen, menu->m_mouse_over_component);
					return true; // consume the enter press.
				}
			}

			if (g_view == View::mod_settings && !g_nav_pending && control_pressed(input, g_controls_cancel))
			{
				request_back_nav();
				return true;
			}
		}

		// Capture page changes so disabled edge-row landings can be corrected.
		const bool track_paging         = on_mods_tab && !(g_use_mouse && *g_use_mouse);
		const std::uint32_t page_before = screen->m_page_start_index;

		auto result = big::g_hooking->get_original<hook_MiscSettingsScreen_HandleInput>()(self, input, x);

		if (track_paging && screen->m_page_start_index != page_before)
		{
			redirect_page_landing(screen, screen->m_page_start_index > page_before);
		}

		return result;
	}

	// Close funnel while mScreenManager is valid.
	static void hook_MiscSettingsScreen_ExitScreen(void* self)
	{
		auto* screen = static_cast<MiscSettingsScreen*>(self);
		const bool on_mods_tab = screen->m_current_category_button == reinterpret_cast<GUIComponent*>(screen->m_editor_options_button);
		if (on_mods_tab && g_view == View::mod_settings)
		{
			request_back_nav();
			return; // veto close, apply_nav runs next Update.
		}

		if (g_restart_required && !g_restart_prompt_shown)
		{
			g_restart_prompt_shown = true;
			void* screen_manager   = *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + screen_manager_offset);
			if (show_restart_dialog(screen_manager, build_restart_message()))
			{
				return;
			}
		}

		// MenuScreen frees components through reflection, not by walking mComponents.
		g_options_screen_open    = false; // stop gating on_change on this screen.
		g_dynamic_refresh_settle = 0.0f;  // drop the pending refresh.
		destroy_rows(screen);
		exit_edit_mode();

		big::g_hooking->get_original<hook_MiscSettingsScreen_ExitScreen>()(self);
	}

	// RestoreDefaults is the handler for [I]/MenuInfo and the on-screen Reset button.
	static void hook_MiscSettingsScreen_RestoreDefaults(void* self)
	{
		auto* screen = static_cast<MiscSettingsScreen*>(self);
		if (screen->m_current_category_button == reinterpret_cast<GUIComponent*>(screen->m_editor_options_button))
		{
			if (g_view != View::mod_settings)
			{
				return; // do not play native reset in the mod list.
			}
			perform_reset();
		}

		big::g_hooking->get_original<hook_MiscSettingsScreen_RestoreDefaults>()(self);
	}

#pragma endregion

#pragma region Hook registration

	void register_hooks()
	{
		// Resolve symbols, RVAs and offsets up front against the validated Ship build.
		std::vector<const char*> missing;
		const auto require = [&](const char* name) -> gmAddress
		{
			const auto addr = big::hades2_symbol_to_address[name];
			if (!addr)
			{
				missing.push_back(name);
			}
			return addr;
		};

		const auto ctor             = require("sgg::MiscSettingsScreen::MiscSettingsScreen");
		const auto do_show_category = require("sgg::MiscSettingsScreen::DoShowCategory");
		const auto on_clicked       = require("sgg::GUIComponentButton::OnClicked");
		const auto update           = require("sgg::MiscSettingsScreen::Update");
		const auto update_scroll    = require("sgg::MiscSettingsScreen::UpdateScrollState");
		const auto handle_input     = require("sgg::MiscSettingsScreen::HandleInput");
		const auto set_number_value = require("sgg::GUIComponentNumBox::SetNumberValue");

		// Required helpers. The button ctor anchors later RVA fallbacks.
		const auto anchor = require("sgg::GUIComponentButton::GUIComponentButton");
		g_button_ctor     = anchor.as_func<void*(void*, void*)>();
		// SetText, not SetDisplayName: the latter runs the string through GameDataManager::GetTextData and swaps in
		// that entry's display name, so e.g. "Random" would render as "Fates' Whim".
		g_set_label       = require("sgg::GUIComponentButton::SetText").as_func<void(void*, const char*)>();
		g_apply_data      = require("sgg::MenuScreen::ApplyDataToComponent").as_func<void(void*, GUIComponent*)>();
		g_update_scroll   = require("sgg::MiscSettingsScreen::UpdateScrollState").as_func<void(void*)>();
		g_set_animation   = require("sgg::GUIComponentButton::SetAnimation").as_func<void(void*, std::uint32_t)>();
		g_hash_lookup     = require("sgg::HashGuid::Lookup").as_func<HashGuid*(HashGuid*, const char*, std::size_t)>();
		g_setup_component = require("sgg::ComponentData::SetupComponent").as_func<void(void*, void*)>();
		g_set_normal_texture = require("sgg::GUIComponentButton::SetNormalTexture").as_func<void(void*, std::uint32_t, bool)>();
		g_was_key_pressed  = require("sgg::InputHandler::WasKeyPressed").as_func<bool(void*, int)>();
		g_show_text        = require("sgg::GUIComponentTextBox::ShowText").as_func<void(void*, const char*)>();
		g_numbox_set_range = require("sgg::GUIComponentNumBox::SetRange").as_func<void(void*, float, float)>();
		g_numbox_set_value = set_number_value.as_func<void(void*, float, bool)>();

		g_push_back =
		    big::hades2_symbol_to_address["eastl::vector<sgg::GUIComponent *,eastl::allocator_forge>::push_back"].as_func<void(void*, GUIComponent**)>();

		// Optional helpers are null-guarded.
		g_get_lines = big::hades2_symbol_to_address["sgg::GUIComponentTextBox::GetLines"].as_func<void*(void*)>();
		g_set_selected_texture = big::hades2_symbol_to_address["sgg::GUIComponentButton::SetSelectedTexture"].as_func<void(void*, std::uint32_t)>();
		g_button_dtor = big::hades2_symbol_to_address["sgg::GUIComponentButton::~GUIComponentButton"].as_func<void(void*)>();
		g_disable = big::hades2_symbol_to_address["sgg::GUIComponentButton::Disable"].as_func<void(void*)>();

		// Slider helpers are optional, with num-box fallback for bounded numbers.
		g_gui_component_ctor = big::hades2_symbol_to_address["sgg::GUIComponent::GUIComponent"].as_func<void(void*, std::uint64_t)>();
		g_image_ctor = big::hades2_symbol_to_address["sgg::GUIComponentImage::GUIComponentImage"].as_func<void(void*, std::uint64_t)>();
		g_textbox_ctor = big::hades2_symbol_to_address["sgg::GUIComponentTextBox::GUIComponentTextBox"].as_func<void(void*, std::uint64_t)>();
		g_slider_defaults = big::hades2_symbol_to_address["sgg::GUIComponentSlider::Defaults"].as_func<void(void*)>();
		const auto slider_set_fraction = big::hades2_symbol_to_address["sgg::GUIComponentSlider::SetFraction"];
		g_slider_set_fraction          = slider_set_fraction.as_func<void(void*, float, bool)>();

		// Optional controller focus and Back/Cancel helpers.
		g_component_focused = big::hades2_symbol_to_address["sgg::MiscSettingsScreen::ComponentFocused"].as_func<void(void*, GUIComponent*)>();
		g_set_mouse_over = big::hades2_symbol_to_address["sgg::MenuScreen::SetMouseOver"].as_func<void(void*, GUIComponent*)>();
		g_input_get_state = big::hades2_symbol_to_address["sgg::InputHandler::GetState"].as_func<std::uint32_t(void*, const void*)>();
		g_mouse_button_down = big::hades2_symbol_to_address["sgg::InputHandler::IsLeftOrRightMouseButtonDown"].as_func<bool(void*)>();

		// Optional left/right press edges enable one-step slider input.
		g_input_was_left_pressed = big::hades2_symbol_to_address["sgg::InputHandler::WasLeftPressed"].as_func<bool(void*)>();
		g_input_was_right_pressed = big::hades2_symbol_to_address["sgg::InputHandler::WasRightPressed"].as_func<bool(void*)>();

		// Optional level probes enable held-direction repeat.
		g_input_is_left_pressed = big::hades2_symbol_to_address["sgg::InputHandler::IsLeftPressed"].as_func<bool(void*)>();
		g_input_is_right_pressed = big::hades2_symbol_to_address["sgg::InputHandler::IsRightPressed"].as_func<bool(void*)>();

		// Optional native-settings flush before a forced restart.
		g_save_profile = big::hades2_symbol_to_address["sgg::ProfileManager::SaveProfile"].as_func<char(void*, bool, bool)>();
		g_active_profile = big::hades2_symbol_to_address["sgg::ProfileManager::ACTIVE_PROFILE"].as<void*>();

		// Named PDB data symbols move with .data and .rdata, unlike anchor-relative RVAs.
		g_use_mouse       = big::hades2_symbol_to_address["sgg::ConfigOptions::UseMouse"].as<const bool*>();
		g_config_language = big::hades2_symbol_to_address["sgg::ConfigOptions::Language"].as<const char*>();
		g_controls_cancel = big::hades2_symbol_to_address["sgg::Controls::Cancel"].as<const void*>();
		g_controls_select = big::hades2_symbol_to_address["sgg::Controls::Select"].as<const void*>();

		// Never fall back from the game's CRT heap to H2M's CRT.
		if (HMODULE ucrt = ::GetModuleHandleW(L"ucrtbase.dll"))
		{
			g_game_aligned_malloc = reinterpret_cast<aligned_malloc_fn>(::GetProcAddress(ucrt, "_aligned_malloc"));
			g_game_aligned_free   = reinterpret_cast<aligned_free_fn>(::GetProcAddress(ucrt, "_aligned_free"));
		}
		if (!g_game_aligned_malloc || !g_game_aligned_free)
		{
			missing.push_back("ucrtbase.dll _aligned_malloc/_aligned_free (the game's CRT heap)");
		}

		// Hardcoded RVAs and struct offsets are valid only for allow-listed PDB GUIDs.
		static constexpr const char* validated_pdb_guids[] = {
		    "744ea71c-2c21-4b40-a6c486d1fa6647da", // Ship, 2026-08-04.
		};
		const bool build_validated = std::find(std::begin(validated_pdb_guids), std::end(validated_pdb_guids), big::hades2_pdb_guid) != std::end(validated_pdb_guids);

		// A GUID match plus anchor RVA match guards against PDB/exe mismatch.
		uintptr_t game_base   = 0;
		std::size_t game_size = 0;
		::module_info_helper::get_module_base_and_size(&game_base, &game_size, nullptr);
		const bool build_matches = anchor && game_base && (anchor.as<uintptr_t>() - game_base == anchor_rva);

		// push_back can be absent as a named symbol, so fall back to its RVA.
		if (!g_push_back && build_matches)
		{
			g_push_back = reinterpret_cast<push_back_fn>(anchor.as<uintptr_t>() - anchor_rva + push_back_rva);
		}
		if (!g_push_back)
		{
			missing.push_back("eastl::vector<sgg::GUIComponent *,eastl::allocator_forge>::push_back");
		}

		if (!missing.empty() || !build_matches || !build_validated)
		{
			std::string detail;
			for (const auto* name : missing)
			{
				detail += "\n    - missing symbol: ";
				detail += name;
			}
			if (!build_validated)
			{
				detail += "\n    - game build not validated for this Hell2Modding version (PDB GUID '";
				detail += big::hades2_pdb_guid.empty() ? "<unknown>" : big::hades2_pdb_guid;
				detail += "'). The game likely updated; add this GUID to validated_pdb_guids after re-validating the "
				          "engine offsets/RVAs against the new Ship build.";
			}
			if (!build_matches)
			{
				detail += "\n    - build fingerprint mismatch (button ctor not at the expected RVA; PDB/exe mismatch?)";
			}
			LOG(WARNING) << "[mod_settings] Mods options tab disabled for this game build; the in-game mod-settings "
			                "editor is skipped. The rom.mod_settings Lua config API is unaffected."
			             << detail;
			return;
		}

		// These helpers cannot be picked unambiguously by name.
		const auto anchor_base = anchor.as<uintptr_t>() - anchor_rva;
		g_message_dialog_ctor  = reinterpret_cast<message_dialog_ctor_fn>(anchor_base + message_dialog_ctor_rva);
		g_add_screen           = reinterpret_cast<add_screen_fn>(anchor_base + add_screen_rva);
		g_numbox_factory       = reinterpret_cast<numbox_factory_fn>(anchor_base + numbox_factory_rva);
		g_teleport_cursor      = reinterpret_cast<teleport_cursor_fn>(anchor_base + teleport_cursor_rva);

		// Prefer the named slider vtable, then fall back to the anchor-relative .rdata RVA.
		if (const auto slider_vt = big::hades2_symbol_to_address["??_7GUIComponentSlider@sgg@@6B@"]; slider_vt)
		{
			g_slider_vtable = slider_vt.as<uintptr_t>();
		}
		else
		{
			g_slider_vtable = anchor_base + slider_vtable_rva;
		}

		// Native slider GetArea spans the screen and hijacks hover from other rows.
		if (g_slider_vtable)
		{
			g_slider_vtable_patched = build_row_area_vtable(g_slider_vtable_copy, sizeof(g_slider_vtable_copy), g_slider_vtable);
		}

		g_feature_enabled = true;

		static auto ctor_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_ctor>(
		    "sgg::MiscSettingsScreen::MiscSettingsScreen",
		    ctor);
		static auto category_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_DoShowCategory>(
		    "sgg::MiscSettingsScreen::DoShowCategory",
		    do_show_category);

		// Global button and num-box hooks filter to our rows via find_row.
		static auto onclick_hook = hooking::detour_hook_helper::add_queue<hook_GUIComponentButton_OnClicked>(
		    "sgg::GUIComponentButton::OnClicked",
		    on_clicked);
		static auto snv_hook = hooking::detour_hook_helper::add_queue<hook_GUIComponentNumBox_SetNumberValue>(
		    "sgg::GUIComponentNumBox::SetNumberValue",
		    set_number_value);

		// Optional slider drag hook.
		if (slider_set_fraction)
		{
			static auto set_fraction_hook = hooking::detour_hook_helper::add_queue<hook_GUIComponentSlider_SetFraction>("sgg::GUIComponentSlider::SetFraction", slider_set_fraction);

			// Only override slider input when both left/right probes resolved.
			const auto slider_handle_input = big::hades2_symbol_to_address["sgg::GUIComponentSlider::HandleInput"];
			if (slider_handle_input && g_input_was_left_pressed && g_input_was_right_pressed)
			{
				static auto slider_handle_input_hook = hooking::detour_hook_helper::add_queue<hook_GUIComponentSlider_HandleInput>("sgg::GUIComponentSlider::HandleInput", slider_handle_input);
			}
		}
		static auto update_hook =
		    hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_Update>("sgg::MiscSettingsScreen::Update", update);
		static auto update_scroll_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_UpdateScrollState>("sgg::MiscSettingsScreen::UpdateScrollState", update_scroll);
		static auto handle_input_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_HandleInput>(
		    "sgg::MiscSettingsScreen::HandleInput",
		    handle_input);

		// ExitScreen is the restart-required prompt funnel.
		const auto exit_screen = big::hades2_symbol_to_address["sgg::MiscSettingsScreen::ExitScreen"];
		if (exit_screen)
		{
			static auto exit_screen_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_ExitScreen>("sgg::MiscSettingsScreen::ExitScreen", exit_screen);
		}
		else
		{
			LOG(WARNING) << "[mod_settings] sgg::MiscSettingsScreen::ExitScreen not found; the restart-required prompt "
			                "will not appear";
		}

		// Optional RestoreDefaults hook for the Reset button.
		const auto restore_defaults = big::hades2_symbol_to_address["sgg::MiscSettingsScreen::RestoreDefaults"];
		if (restore_defaults)
		{
			static auto restore_defaults_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_RestoreDefaults>("sgg::MiscSettingsScreen::RestoreDefaults", restore_defaults);
		}
		else
		{
			LOG(WARNING) << "[mod_settings] sgg::MiscSettingsScreen::RestoreDefaults not found; the Reset button will "
			                "not reset mod settings";
		}
	}

#pragma endregion

} // namespace big::mod_settings
