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

	// GUIComponent::mName, an eastl::string ApplyDataToComponent uses to look up the matching sjson template.
	static constexpr std::size_t gui_component_name_offset = 0x4'88;

	// Retuning mDef then re-running ComponentData::SetupComponent re-applies the template - this is how a plain button
	// is converted into a key-rebind style text row.
	static constexpr std::size_t component_data_offset = 0x88; // GUIComponent::mData (sgg::ComponentData).
	static constexpr std::size_t component_def_offset  = 0xA8; // mData(0x88) + ComponentData::mDef(0x20).


	// Field offsets inside sgg::ComponentDataDef (relative to component_def_offset).
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
	// Each sgg::SoundCue is 0x10 bytes (pOwner @0, mName HashGuid @8). The base OnClicked plays mPressSound, while the
	// native ToggleOptionValueChanged (which our toggle path replaces) plays the toggle cues - so we copy the matching
	// one into mPressSound to reproduce the sound.
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

	// GUIComponent::Update moves mFadeOpacity toward mFadeTarget by dt * mFadeSpeed, so this drives the fade timing.
	// Applied to every row so all row types fade at one uniform speed.
	static constexpr float row_fade_speed = 10.0f;

	// sgg::MessageDialog, the single-button box the game uses in the MAIN MENU for save/file errors.
	static constexpr std::size_t message_dialog_size          = 0x2'F0; // sizeof sgg::MessageDialog
	static constexpr std::size_t screen_manager_offset        = 0x48;   // sgg::GameScreen::mScreenManager
	static constexpr std::size_t screen_removed_offset        = 0x21;   // sgg::GameScreen::mRemoved (bool)
	static constexpr std::size_t screen_visible_offset        = 0x22;   // sgg::GameScreen::mIsVisible (bool)
	static constexpr std::size_t screen_block_input_offset    = 0x24;   // sgg::GameScreen::mBlockLowerInput (bool)
	static constexpr std::size_t dialog_title_offset          = 0x1'88; // sgg::MenuScreen::mTitleText
	static constexpr std::size_t dialog_confirm_button_offset = 0x1'A0; // sgg::MenuScreen::mConfirmButton
	static constexpr std::size_t dialog_message_offset        = 0x2'B0; // sgg::MessageDialog::mMessageText

	// The MessageDialog.sjson MessageText template renders at FontSize 26, too large for the multi-line body. Scaling
	// the font handle's ratios shrinks it.
	static constexpr std::size_t textbox_font_handle_offset        = 0x6'A4; // GUIComponentTextBox::mFontHandle
	static constexpr std::size_t font_handle_size_ratio_offset     = 0x0C;   // sgg::FontHandle::mFontSizeRatio
	static constexpr std::size_t font_handle_eng_size_ratio_offset = 0x10;   // sgg::FontHandle::mEnglishFontSizeRatio
	static constexpr float restart_message_font_scale              = 0.75f;  // ~26 -> ~19.5

	// Module-relative RVAs for overloaded functions the PDB map cannot disambiguate. Resolved from
	// anchor_runtime - anchor_rva + target_rva. AddScreen's 4-arg overload appends so the dialog draws on top.
	static constexpr std::uintptr_t anchor_rva              = 0x11'5C'70; // GUIComponentButton::GUIComponentButton
	static constexpr std::uintptr_t message_dialog_ctor_rva = 0x16'EE'60; // sgg::MessageDialog::MessageDialog
	static constexpr std::uintptr_t add_screen_rva          = 0x14'7D'D0; // sgg::ScreenManager::AddScreen

	// tf_new_internal<sgg::GUIComponentNumBox, sgg::MiscSettingsScreen*>: the game's own factory, which allocates the
	// num-box and builds its 5 sub-components. A template instantiation, so resolved by RVA off the anchor.
	static constexpr std::uintptr_t numbox_factory_rva = 0x17'A5'30;

	// eastl::vector<GUIComponent*>::push_back, used only as a fallback when the named PDB symbol is missing (it is
	// sometimes emitted inline). Resolved off the same button-ctor anchor.
	static constexpr std::uintptr_t push_back_rva = 0x14'1E'D0;

	// sgg::MenuScreen::TeleportCursorTo(this, GUIComponent*) - the 2-arg overload that drops the controller/keyboard
	// free-form cursor onto a component.
	static constexpr std::uintptr_t teleport_cursor_rva = 0x14'03'A0;

	// The config/control globals (ConfigOptions::UseMouse/Language, Controls::Cancel/Select) live in .data/.rdata,
	// which a game update can grow and shift independently of .text, so they are resolved by name rather than by an
	// anchor-relative RVA.

	// sgg::GUIComponentNumBox, sizeof 0x5D0. Derives directly from GUIComponent, not GUIComponentButton.
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

	// sgg::GUIComponentButton box-graphic scaling. GUIComponentButton::Draw pushes only a uniform scale into it, so a
	// non-uniform (wider) box needs the anim's own def mScaleX plus mScaleModifierOnlyX, which the anim draw path honours.
	static constexpr std::size_t button_anim_offset = 0x5'70; // GUIComponentButton::mAnim (GUIComponentAnimation*)
	// GUIComponentButton::GetArea reads GUIComponentButton::mLabel location, not the button's.
	static constexpr std::size_t button_label_offset = 0x5'80; // GUIComponentButton::mLabel (GUIComponentTextBox*)

	static constexpr std::size_t anim_scale_modifier_only_x_offset = 0x5'42; // mScaleModifierOnlyX (bool)
	// component_def_scale_* offsets are from the component base.
	static constexpr std::size_t component_def_scale_x_offset = 0x1'14; // mData.mDef.mScaleX (float)

	static constexpr std::size_t component_def_scale_y_offset = 0x1'18; // mData.mDef.mScaleY (float)

	// FreeFormSelectOffset is added to a component's location when the spatial keyboard/controller nav
	// (SearchInDirection) evaluates it as a candidate. Used to place the scroll arrows' eval point where the next or
	// previous row would be, so nav reaches an arrow at a page edge (see enable_arrow_keyboard_paging).
	static constexpr std::size_t component_free_form_offset_x_offset = 0x1'54;  // mFreeFormSelectOffsetX (float)
	static constexpr std::size_t component_free_form_offset_y_offset = 0x1'58;  // mFreeFormSelectOffsetY (float)
	static constexpr std::size_t component_auto_activate_offset      = 0x00'BC; // mAutoActivateWithGamepad (bool)

	// SearchInDirection skips a candidate whose mFreeFormSelectable is false before it even calls IsSelectable, while
	// mouse hover does not read it - so clearing it makes UP/DOWN nav jump a row that the mouse can still hover.
	static constexpr std::size_t component_free_form_selectable_offset = 0x00'B1; // mData.mDef.mFreeFormSelectable (bool)

	// The Button_Secondary sprite's native atlas width in px. The box draws at native * mScale * mScaleX.
	static constexpr float button_graphic_native_width = 350.0f;

	// Approximate label capacity of the box at its native width, in measure_width glyph units. A longer label stretches
	// the box just enough to fit, so short buttons keep the clean native box and only long ones distort.
	static constexpr float button_label_capacity = 15.0f;
	static constexpr float button_label_padding  = 2.0f;

	// sgg::GUIComponentSlider, the audio-volume drag bar. DoShowCategory hand-builds it, so make_slider_row does too.
	// The vtable is preferred by name; this RVA is a .rdata fallback and must be refreshed when the build changes.
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

	// GUIComponentSlider has no Draw-time highlight gate (unlike GUIComponentButton, whose Draw re-derives it from
	// mForceSelected/owner->mSelectedComponent), so the focus look tracks its own mFocused bool.
	static constexpr std::size_t slider_focused_offset          = 0x5'48;  // GUIComponentSlider::mFocused (bool)
	static constexpr std::size_t textbox_use_selected_color_off = 0x5'52;  // GUIComponentTextBox::mUseSelectedTextColor
	static constexpr std::size_t vtable_on_mouse_off_offset     = 0x00'60; // GUIComponent::OnMouseOff slot
	static constexpr std::size_t vtable_on_unselected_offset    = 0x00'88; // GUIComponent::OnUnselected slot
	static constexpr std::size_t vtable_on_focus_off_offset     = 0x1'18;  // GUIComponent::OnFocusOff slot
	static constexpr std::size_t vtable_set_location_offset = 0x1'80; // GUIComponent::SetLocation slot (moves the component and its children)

	// Greying a slider/num-box: the button-style def greying does not reach their separate label/value text boxes or
	// bar/arrow graphics, so each is greyed directly. A text box renders mDisabledText only when its def carries a
	// non-negative disabled colour, so that is written explicitly; the value box is flagged too, since Draw never
	// touches it. An image tints from mColor every frame, so mColorTarget is written as well or a lerp undoes it.
	static constexpr std::size_t textbox_use_disabled_color_off = 0x5'53; // GUIComponentTextBox::mUseDisabledTextColor
	// Greying the normal and selected colours too keeps the label grey in every state, matching set_def_text_grey.
	static constexpr std::size_t textbox_text_red            = 0x1'B4;  // mData.mDef.mTextRed (float)
	static constexpr std::size_t textbox_selected_text_red   = 0x1'D0;  // mData.mDef.mSelectedTextRed (float)
	static constexpr std::size_t textbox_disabled_text_red   = 0x1'E8;  // mData.mDef.mDisabledTextRed (float)
	static constexpr std::size_t textbox_disabled_text_green = 0x1'EC;  // mDisabledTextGreen (float)
	static constexpr std::size_t textbox_disabled_text_blue  = 0x1'F0;  // mDisabledTextBlue (float)
	static constexpr std::size_t textbox_disabled_text_alpha = 0x1'F4;  // mDisabledTextAlpha (float)
	static constexpr std::size_t image_color_offset          = 0x5'44;  // GUIComponentImage::mColor (packed RGBA)
	static constexpr std::size_t image_color_target_offset   = 0x00'78; // mColorTarget (packed RGBA)
	static constexpr std::size_t button_graphic_color_offset = 0x5'5C; // GUIComponentButton::mButtonColor - the colour Draw paints the toggle graphic with
	static constexpr std::size_t component_color_target_offset = 0x00'78; // GUIComponent::mColorTarget (Update eases mButtonColor toward this)
	static constexpr std::size_t def_sel_red = 0xFC; // ComponentDataDef::mSelectedRed - set <0 to disable the selected-colour override in Draw/On(Un)Selected
	static constexpr float disabled_text_grey            = 0.22f; // matches set_def_text_grey (toggle/text rows)
	static constexpr std::uint32_t disabled_graphic_grey = 0xFF'66'66'66; // opaque 0.4 grey (packed A,B,G,R)
	// The template caches a bright colour at build time and greying the def alone does not update it, so a still-
	// selectable greyed label stays bright - SetTextColor re-applies the grey, as UpdateButtonStates does.
	static constexpr std::size_t vtable_set_text_color_offset = 0x1'60;
	static constexpr std::uint32_t disabled_label_grey_packed = 0xFF'38'38'38;

	// NumBox::OnSelected turns the box black by writing the selected colour into the animation's own mColor.
	static constexpr std::size_t animation_color_offset  = 0x5'58;        // GUIComponentAnimation::mColor (packed ARGB)
	static constexpr std::uint32_t numbox_hover_bg_black = 0xFF'00'00'00; // the num-box's hovered/selected box colour

	// Called with flags = 0 it destructs and frees owned sub-components without the final operator delete, so the block
	// itself is freed separately (see game_free).
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

	// GUIComponent-derived constructors take the initial location as a Vec2 passed by value (packed into a single.
	// 64-bit register). 0 is the origin. Used to hand-build a slider and its sub-components.
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

	// sgg::HashGuid is a 32-bit interned-string id in its first field.
	struct HashGuid
	{
		std::uint32_t m_id;
	};

	using hash_lookup_fn = HashGuid* (*)(HashGuid * out, const char* str, std::size_t len);

	// sgg::ProfileManager::SaveProfile(eastl::string* profileName, bool showSpinner, bool async): serializes the active
	// profile (language, audio volumes, resolution/window/VSync/graphics, and all gameplay/interface/accessibility
	// toggles) to disk. Called synchronous (async=false) to guarantee the write completes before we force a restart.
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
	static std::uintptr_t g_slider_vtable               = 0; // runtime
	// A patched copy of the slider vtable (built in set_up_hooks) whose GetArea/GetScreenArea slots return a one-row
	// hit rect (see row_bounded_area), replacing the native ones that union the slider's sub-components into a
	// screen-spanning rect. 128 slots comfortably covers the class's virtual table.
	static constexpr std::size_t slider_vtable_slot_count = 128;
	// The highest slot we override or copy through is SetLocation at +0x180, so keep the buffer big enough for it.
	static_assert(0x1'80 / sizeof(std::uintptr_t) < slider_vtable_slot_count, "vtable copy buffer too small for the highest patched slot");
	static std::uintptr_t g_slider_vtable_copy[slider_vtable_slot_count] = {};
	static std::uintptr_t g_slider_vtable_patched                        = 0; // runtime

	// A patched copy of the GUIComponentButton vtable (built lazily in install_wide_button_nav_rect from the first action
	// button's vtable) whose GetArea/GetScreenArea slots return the same wide one-row rect (row_bounded_area), so a
	// centre-column action button is reachable by the vertical spatial nav. Every other button row keeps the native
	// vtable.
	static std::uintptr_t g_button_vtable_copy[slider_vtable_slot_count] = {};
	static std::uintptr_t g_button_vtable_patched                        = 0; // runtime
	static teleport_cursor_fn g_teleport_cursor = nullptr; // drops the controller cursor on a row (initial focus)
	static set_mouse_over_fn g_set_mouse_over   = nullptr; // MenuScreen::SetMouseOver (highlight + select a row)
	static const bool* g_use_mouse              = nullptr; // sgg::ConfigOptions::UseMouse (false in controller mode)
	static const char* g_config_language        = nullptr; // sgg::ConfigOptions::Language

	static component_focused_fn g_component_focused = nullptr; // focuses a row so it receives stick input + green
	static input_get_state_fn g_input_get_state     = nullptr; // reads a remappable control's per-frame state
	static mouse_button_down_fn g_mouse_button_down = nullptr; // true while a mouse button is held (active drag detect)
	static input_dir_pressed_fn g_input_was_left_pressed  = nullptr; // left/decrease press edge (dpad, arrow, stick)
	static input_dir_pressed_fn g_input_was_right_pressed = nullptr; // right/increase press edge
	static const void* g_controls_cancel  = nullptr; // &sgg::Controls::Cancel (controller B/keyboard Esc)
	static const void* g_controls_select  = nullptr; // &sgg::Controls::Select (controller A/Enter)
	static save_profile_fn g_save_profile = nullptr; // sgg::ProfileManager::SaveProfile (flush native settings)
	static void* g_active_profile         = nullptr; // &sgg::ProfileManager::ACTIVE_PROFILE

	// Set true by register_hooks only once every engine symbol, RVA and offset the Mods tab needs has resolved for the
	// running game build. While false no hooks are installed and the tab is absent. It also gates process-global side
	// effects (the wndproc callback) as a safety net.
	static bool g_feature_enabled = false;

	// sgg::KeyboardButtonId values used for edit confirm/cancel (validated in the PDB).
	static constexpr int key_escape   = 0;
	static constexpr int key_kp_enter = 113;
	static constexpr int key_return   = 127;

	// Hash of the game's "Blank" (empty) graphic, resolved once, used to hide a row's button background so it renders
	// as a plain text label.
	static std::uint32_t g_blank_graphic = 0;

	// Panel layout, in native 1080p menu coordinates. The engine's UpdateScrollState pass positions each on-page row at Y
	// = (index - pageStart) * row_pitch + row_base_y + ScreenCenterOffsetY, and X = the row's own location.
	static constexpr float row_location_x      = 1560.0f; // component X (right pane), like OptionToggleButton
	static constexpr float row_text_offset_x   = -900.0f; // left-justify the label to the option-name column
	static constexpr float value_text_offset_x = 15.0f; // right-justify the value, right edge aligns with the toggle's
	static constexpr float numbox_location_x   = 1365.0f; // native OptionNumBox X (box + arrows clear the scrollbar)
	static constexpr float slider_location_x   = 1330.0f; // native OptionSlider X (bar + value clear the scrollbar
	// template's label offset puts the name in the option-name column).
	static constexpr float button_center_x       = 1130.0f; // centered action button X (clear of the scrollbar)
	static constexpr float row_base_y            = 300.0f;  // first row's Y - matches the vanilla option templates
	static constexpr float row_pitch             = 45.0f;   // vertical distance between rows (vanilla Spacing = 45)
	static constexpr std::uint32_t rows_per_page = 10;      // vanilla ItemsPerPage = 10

	// Action-button rows use the taller Button_Secondary box, which crowds the neighbouring setting rows on the uniform
	// row pitch. apply_button_spacing nudges each action button down by this lead and shifts the rows below it by
	// lead+trail, giving the buttons vertical breathing room (applied through the engine's own SetLocation, so it is
	// drift-free - see apply_button_spacing).
	static constexpr float button_extra_lead  = 14.0f;
	static constexpr float button_extra_trail = 14.0f;

	// Config sections. Both rom.mod_settings.load and Chalk bind a mod's settings under the root "config" section.
	// Nested groups are dot-separated child sections (e.g. "config.biome_pool").
	static const std::string root_section = "config";

	// Chalk writes a placeholder entry with this key per section so empty groups persist. Skip it.
	static constexpr const char* section_empty_key = "...";

	// Approximate visual width budget for the right-column value (freetext + its edit caret), in "width units" where a
	// typical medium glyph is 1.0 The menu font is variable-width, so a raw character count looks inconsistent (a run of
	// 'W' is far wider than a run of 'i') budgeting by summed glyph weight keeps the shown value a consistent WIDTH so it
	// does not run left into the key label.
	static constexpr float value_display_max_width = 30.0f;

	// Edit-cursor blink half-period (ms): the "|" shows for this long, then hides.
	static constexpr std::uint64_t edit_cursor_blink_ms = 500;

	// What a panel row represents, so a click can be routed to the right action.
	enum class RowKind
	{
		mod_entry, // opens that mod's settings
		group,     // opens a nested config group (a child section)
		setting,   // edits one config entry
		action,    // a button that runs an action (e.g. Apply/Reset)
		info,      // a read-only virtual row (value from a Lua get/text callback, no config entry)
	};

	struct PanelRow
	{
		GUIComponent* component = nullptr;
		RowKind kind            = RowKind::mod_entry;
		std::string stem;        // owning mod's config-file stem
		std::string setting_key; // config entry key (setting rows only)

		// The bound config entry (setting rows only) valid for the config file's lifetime, which spans the whole menu
		// session.
		toml_v2::config_file::config_entry_base* entry = nullptr;

		bool disabled          = false; // greyed & non-interactable (mod disabled)
		bool is_enabled_toggle = false; // the mod's master "enabled" toggle
		bool is_virtual_input  = false; // an interactive virtual row (value via Lua get/set, not a config entry)
		bool is_toggle         = false; // a boolean toggle row (config bool, or an interactive virtual bool)

		// The on/off state a virtual toggle was drawn with. A virtual toggle computes its flip from get(), but get()
		// may return nil (the mod's value is not set yet, the case `type` covers) - the flip then falls back to this
		// last-drawn state so the first click still works. Only meaningful for an interactive virtual bool row.
		bool toggle_value = false;

		// Author-provided description shown at the bottom of the screen while this row is highlighted (setting rows
		// only empty for navigation rows).
		std::string description;

		// Right-column value display for a non-bool setting row (paired with `component`, the left-column key). Not in
		// mOptions positioned to follow `component` each frame.
		GUIComponent* value_component = nullptr;

		// Bounded number setting (metadata has both min and max). Rendered as a native slider (drag bar) spanning
		// [stepper_min, stepper_max] and snapped to stepper_step.
		bool is_slider      = false;
		bool is_stepper     = false;
		double stepper_min  = 0.0;
		double stepper_max  = 0.0;
		double stepper_step = 1.0;

		// Number-display options (slider value text): is_percentage shows a 0..1 value as 0..100 and appends "%"
		// show_as_percentage only appends "%".
		bool show_as_percentage = false;
		bool is_percentage      = false;

		// Enum cycler (metadata has `values`). Rendered as a native number box over the index 0..labels-1 whose value
		// text is overridden to the label (like the game's own enum options) `enum_values` are the serialized config
		// values, `enum_labels` the parallel display strings both indexed by the box's current integer value.
		bool is_enum = false;
		std::vector<std::string> enum_values;
		std::vector<std::string> enum_labels;

		// Group rows (RowKind::group) only: the child config section this row drills into.
		std::string target_section;

		// The entry's REAL config section (for virtual-row Lua I/O: get/set/text). A `group` override can place a row on a
		// menu page whose path differs from the entry's config section, so runtime commits must use this, not the view path.
		std::string config_section;
	};

	static std::vector<PanelRow> g_rows;

	// Set when a restart-required setting is changed this menu session (e.g. toggling the "enabled" switch of an
	// sjson-backed mod). On options-menu close we warn + close the game.
	static bool g_restart_required = false;

	// The restart-causing changes this session, keyed by "<stem>\0<section>\0<key>" so re-editing the same setting
	// overwrites its line rather than adding a duplicate. Values are the human-readable lines listed in the restart
	// popup, e.g. "MyMod: Enabled (on)".
	static std::map<std::string, std::string> g_restart_changes;

	// Baseline serialized value (as of this menu session's open) for each restart-required setting that was touched,
	// keyed identically to g_restart_changes. Used to drop a setting from the restart list when it is changed back to
	// its baseline (no net change -> no restart needed).
	static std::map<std::string, std::string> g_restart_baselines;

	// The native restart message box's (only) button clicking it closes the game (restart).
	static GUIComponent* g_restart_confirm_button = nullptr;

	// The restart message box itself (owner of g_restart_confirm_button). A genuine restart button's owner is this dialog
	// any rebuilt row's owner is the options screen, so it will not match.
	static void* g_restart_dialog = nullptr;

	// True once the restart prompt has been shown this menu session (so closing again proceeds).
	static bool g_restart_prompt_shown = false;

	// Which view the Mods panel is currently showing, plus a deferred navigation request that a click sets and the
	// Update hook applies at a safe point (outside input/click iteration, where mutating the component vectors is
	// safe).
	enum class View
	{
		mod_list,
		mod_settings,
	};

	static View g_view = View::mod_list;
	static std::string g_view_stem;    // mod whose settings are shown (mod_settings view)
	static std::string g_view_section; // config section shown within that mod (mod_settings view)
	static bool g_nav_pending  = false;
	static View g_pending_view = View::mod_list;
	static std::string g_pending_stem;
	static std::string g_pending_section;

	// Identifies a panel row by its stable fields (kind + owning mod + section + key) so it can be matched to the
	// equivalent freshly built row after a rebuild frees every component.
	struct RowIdentity
	{
		bool valid = false;
		RowKind kind;
		std::string stem;
		std::string section;
		std::string key;
		std::string config_section; // real config section, to distinguish same-named keys grouped onto one page
	};

	// The clicked row to hold as hovered/selected across a click-triggered instant rebuild, captured in the OnClicked
	// hook. A rebuild frees every component and the native hover pass re-resolves the cursor a frame later, so this is
	// re-asserted for a few frames (see reassert_keep_active_row) to steady the prompt, description and highlight.
	static RowIdentity g_keep_active_row;

	// Frames to re-assert the clicked row as hovered/selected after a click-triggered instant rebuild. The native hover
	// pass runs the frame after the rebuild and can transiently resolve the stationary cursor to a neighbouring row (or
	// clear the bottom-prompt label), so the prompt, description and highlight blink for a frame unless we hold them.
	static constexpr int keep_active_frame_count = 3;
	static int g_keep_active_frames              = 0;

	// Seconds of input quiet after a numeric setting (slider/number-box) changes before the view is rebuilt to
	// re-evaluate its dynamic (Lua-function) rows - e.g. an apply button's dynamic `disabled`.
	static constexpr float dynamic_refresh_settle_seconds = 0.15f;

	// Time left on that debounce (0 = nothing pending). A slider fires its change hook every frame while dragged and a
	// rebuild frees the dragged row, so we wait for a short quiet gap and rebuild once the drag settles. Re-armed on
	// every change ticked down in the Update hook.
	static float g_dynamic_refresh_settle = 0.0f;

	// Navigation restore stack: one entry per drill-in level. Each records the parent view's scroll offset and which row
	// was drilled through, so backing out restores that scroll and re-selects that row instead of snapping to the top.
	// focus_stem identifies a mod row, focus_section a group row by its target section.
	struct NavRestore
	{
		std::uint32_t scroll_index = 0;
		std::string focus_stem;
		std::string focus_section;
	};

	static std::vector<NavRestore> g_nav_stack;
	static NavRestore g_pending_restore;
	static bool g_has_pending_restore = false;

	// Freetext edit state (number/string settings). A click enters edit mode typed input is captured in the window
	// procedure and applied on the game thread in the Update hook.
	static bool g_editing                                        = false;
	static GUIComponent* g_edit_component                        = nullptr;
	static toml_v2::config_file::config_entry_base* g_edit_entry = nullptr;

	static std::string g_edit_buffer;
	static std::size_t g_edit_cursor = 0;     // caret position as a byte index into g_edit_buffer.
	static bool g_edit_numeric       = false; // restrict input to a numeric literal.
	static bool g_edit_confirm       = false;
	static bool g_edit_cancel        = false;

	// Turns a config-file stem ("AuthorName-ModName") into a display name: drops the author (up to the first '-') and runs
	// the mod name through key_to_display, so '_' becomes a space and camelCase/PascalCase word boundaries are split - the
	// same friendly-name logic used for setting keys "SGG_Modding-Chalk" -> "Chalk". "NikkelM-Zagreus_Journey" -> "Zagreus
	// Journey" "zerp-DreamDiveTweaks" -> "Dream Dive Tweaks".
	static std::string key_to_display(const std::string& key); // shared friendly-name logic, defined below

#pragma endregion

#pragma region Mod identity, text, and Mods-tab helpers

	static std::string display_name_from_stem(const std::string& stem)
	{
		const auto dash        = stem.find('-');
		const std::string name = (dash == std::string::npos) ? stem : stem.substr(dash + 1);
		return key_to_display(name);
	}

	// The mod's Thunderstore manifest description, shown in the description box while its row in the mod list is
	// highlighted. Empty when no loaded module matches the stem.
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

	// Shown in the description box in place of the mod description when a mod opted out of the in-game settings menu
	// (rom.mod_settings.opt_out()), explaining why its row is greyed and where to configure it instead.
	static std::string opt_out_note()
	{
		return "This mod opted out of the in-game settings menu. See the mod's own description for how "
		       "to configure it, if applicable.";
	}

	static std::string resolve_localized(const localized_text& t); // defined below

	// The description shown for an opted-out mod's greyed row: the author's own opt_out(description) if they supplied
	// one (resolved to the current game language), otherwise the generic opt_out_note().
	static std::string opt_out_description(const std::string& stem)
	{
		const std::string custom = resolve_localized(mod_opt_out_description(stem));
		return !custom.empty() ? custom : opt_out_note();
	}

	// Escapes the characters GUIComponentTextBox::Parse treats as markup, so arbitrary user text renders verbatim.
	// The parser reads '\' as an escape lead that consumes the following word ("D:\Program..." -> "D: ...") and '[' ']'
	// as inline-tag delimiters whose contents are dropped ("[deprecated] x" -> " x"). Backslash must be escaped first.
	// '{' and '@' are also markup leads but have no literal escape and do not eat surrounding characters, so are left.
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
		const std::size_t n = std::min(a.size(), b.size());
		for (std::size_t i = 0; i < n; ++i)
		{
			unsigned char ca = static_cast<unsigned char>(a[i]);
			unsigned char cb = static_cast<unsigned char>(b[i]);
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
		}
		if (a.size() == b.size())
		{
			return 0;
		}
		return a.size() < b.size() ? -1 : 1;
	}

	// --- Text metrics + caret helpers (byte indices into a string UTF-8 aware) --- Approximate width of a single byte in
	// the value font, in the same units as value_display_max_width (medium glyph.
	static float glyph_weight(unsigned char c)
	{
		if (c >= 0xC0)
		{
			return 1.0f; // UTF-8 lead byte: count the codepoint once
		}
		if (c >= 0x80)
		{
			return 0.0f; // UTF-8 continuation byte
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
		case 'r':  return 0.5f; // narrow glyphs
		case 'm':
		case 'w':
		case 'M':
		case 'W':
		case '@':
		case '%':  return 1.5f; // wide glyphs
		default:   return 1.0f;   // medium (digits, most letters)
		}
	}

	// Summed approximate visual width of a string (see glyph_weight).
	static float measure_width(const std::string& s)
	{
		float w = 0.0f;
		for (char c : s)
		{
			w += glyph_weight(static_cast<unsigned char>(c));
		}
		return w;
	}

	// True for a "word" byte: ASCII alphanumeric, underscore, or any UTF-8 byte (>=0x80, so non-ASCII letters count as
	// word characters). Used for Ctrl+Left/Right word skip.
	static bool is_word_byte(char c)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		return (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u == '_' || u >= 0x80;
	}

	// Caret one codepoint to the left (skips UTF-8 continuation bytes so a multibyte char moves as a unit).
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

	// Caret one codepoint to the right.
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

	// Caret to the start of the current/previous word (Ctrl+Left): skip any non-word bytes to the left, then the run of
	// word bytes.
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

	// Caret to the start of the next word (Ctrl+Right): skip the current run of word bytes, then the following non-word
	// bytes.
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

	// Caps an over-wide value string for the right-aligned value column so it does not run left into the option's key
	// label. Fits by summed glyph WIDTH, not character count, so wide/narrow text shows a consistent visual width.
	static std::string truncate_value(const std::string& text)
	{
		if (measure_width(text) <= value_display_max_width)
		{
			return text;
		}
		const float avail = value_display_max_width - measure_width("..."); // leave room for the prefix
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

		// Point the button's localization id at "Mods" so the engine's own label pipeline resolves it.
		// GUIComponentButton::UseDefaultText re-derive "Mods" natively - including after a language change, which re-runs
		// that derivation and would otherwise revert the tab to "Editor" "Mods" has no text-data entry, so the lookup misses
		// and the engine renders the raw key ("Mods") verbatim in every language.
		if (g_hash_lookup)
		{
			HashGuid id{};
			g_hash_lookup(&id, "Mods", 4);
			*reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(button) + sgg::gui_component_button_display_name_id_offset) = id.m_id;
		}

		// Apply the label now for the initial display: the original constructor already rendered the native "Editor"
		// text from the native Editor id, and UseDefaultText only re-derives on the next localization pass. Subsequent
		// language changes are handled by the id above, not here.
		if (g_set_label)
		{
			g_set_label(button, "Mods");
		}
	}

#pragma endregion

#pragma region Native row construction and styling

	// Writes an in-place EASTL short-string (SSO, up to 22 chars) into a component field.
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
		bytes[0x17] = static_cast<char>(0x17 - n); // SSO: remaining = capacity(23) - length
	}

	// GUI objects are allocated and freed through the GAME's CRT, never H2M's: H2M is /MT while the game is /MD against
	// ucrtbase, and the engine frees anything it owns (removed screens, a slider's sub-components, tf_new_internal
	// blocks) with ucrtbase's _aligned_free. Crossing the boundary either way hands a heap a block it never owned.
	// The deleting destructor is always called with flags = 0 for the same reason: flags = 1 routes to operator delete
	// -> free() on an _aligned_malloc block, which the engine never does either.
	using aligned_malloc_fn = void*(__cdecl*)(std::size_t, std::size_t);
	using aligned_free_fn   = void(__cdecl*)(void*);

	static aligned_malloc_fn g_game_aligned_malloc = nullptr;
	static aligned_free_fn g_game_aligned_free     = nullptr;

	static void* game_alloc(std::size_t size)
	{
		return g_game_aligned_malloc ? g_game_aligned_malloc(size, 8) : nullptr; // alignment 8 matches the engine
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

	// Links a finished row into the drawn/hit-tested (mComponents) and paged (mOptions) vectors, sets its X, and starts
	// it transparent UpdateScrollState only fades in and repositions on-page rows, so off-page rows must start
	// invisible to avoid flashing stacked at the top.
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

		// Uniform opacity ease rate so every row type fades at the same native speed (see row_fade_speed).
		*reinterpret_cast<float*>(reinterpret_cast<char*>(row) + component_def_offset + def_fade_speed) = row_fade_speed;
	}

	// Shows the on or off toggle graphic for a toggle row. The OptionToggleButton template stores both graphic hashes
	// in the row's def (mGraphic = on, mAlternateGraphic = off) pick one and set it as the drawn texture.
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

	// Reproduces the vanilla toggle click sound. A native toggle plays its cue from ToggleOptionValueChanged, which our
	// toggle path replaces, and the base OnClicked only plays mPressSound (unset in the toggle template) - so a toggle
	// would be silent. Copying the cue for the value the click will produce into mPressSound lets the native audio path
	// play it with the correct swap handling.
	static void stage_toggle_press_sound(GUIComponent* row, bool new_value)
	{
		char* def             = reinterpret_cast<char*>(row) + component_def_offset;
		const std::size_t src = new_value ? def_toggle_on_sound : def_toggle_off_sound;
		std::memcpy(def + def_press_sound, def + src, sound_cue_size);
	}

	// Dims a row's def text colours (both normal and selected) so a disabled row reads as greyed out and does not
	// recolour on hover. Must be applied before SetupComponent so the change reaches the text box.
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

	// Greys a child GUIComponentTextBox (a slider/num-box label or value box) by giving it a disabled text colour and
	// flagging it to use that colour. Grey text colour matches set_def_text_grey so every disabled row reads the same.
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

		// Also grey the normal and selected text colours (red/green/blue triples). A still-selectable greyed row keeps
		// mIsUseable=1, so Slider/NumBox Draw clears mUseDisabledTextColor and the label falls back to the normal colour (and
		// the selected colour on hover) - greying both keeps it greyed in every state, with no hover highlight.
		for (const std::size_t base : {textbox_text_red, textbox_selected_text_red})
		{
			*reinterpret_cast<float*>(b + base + 0x0) = disabled_text_grey;
			*reinterpret_cast<float*>(b + base + 0x4) = disabled_text_grey;
			*reinterpret_cast<float*>(b + base + 0x8) = disabled_text_grey;
		}
	}

	// Dims a GUIComponentImage (a slider's bar backing/fill) to the disabled grey. Image::Draw tints from mColor each
	// frame and neither Slider::Draw nor Slider::Update recolour the bar, so writing mColor (plus mColorTarget so the
	// per-frame lerp does not pull it back) sticks.
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

	// Greys a disabled toggle's on/off ring so it reads greyed from frame one. The ring is a bare texture that Draw
	// paints with mButtonColor, which starts black and only eases toward mColorTarget on hover - so an untouched
	// disabled toggle would show black. Setting both to the grey (Update sees them equal and never eases) plus
	// mSelectedRed < 0 to skip the selected-colour override keeps it greyed at rest and through hover.
	static void grey_toggle_graphic(GUIComponent* row)
	{
		char* b                                                              = reinterpret_cast<char*>(row);
		*reinterpret_cast<std::uint32_t*>(b + button_graphic_color_offset)   = disabled_graphic_grey;
		*reinterpret_cast<std::uint32_t*>(b + component_color_target_offset) = disabled_graphic_grey;
		*reinterpret_cast<float*>(b + component_def_offset + def_sel_red)    = -1.0f;
	}

	// Sets a row's normal text colour to the native settings-option grey (0.55) used by the game's own.
	// OptionToggleButton/OptionNumBox rows, so plain-text (key/value) rows built on the CategoryOptionsButton template
	// (whose own text is a darker 0.35) match the toggle rows instead of reading as brighter full white. Must run before
	// SetupComponent to reach the text box.
	static void set_def_text_normal(GUIComponent* row, bool also_selected = false)
	{
		char* def                                       = reinterpret_cast<char*>(row) + component_def_offset;
		constexpr float option_grey                     = 0.55f; // matches MiscSettingsScreen.sjson option rows
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

	// A plain left-justified text row (mod names, Back, and non-toggle settings). Disabled rows are greyed. By default
	// they are also hard-disabled (non-selectable).
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
		*reinterpret_cast<std::uint8_t*>(def + def_add_text_area)      = 1; // hit area follows the text
		*reinterpret_cast<std::uint8_t*>(def + def_use_text_area)      = 0; // (union with the empty graphic area)
		*reinterpret_cast<std::uint32_t*>(def + def_graphic)           = 0; // no button background
		*reinterpret_cast<std::uint32_t*>(def + def_selected_graphic)  = 0; // no
		*reinterpret_cast<std::uint32_t*>(def + def_alternate_graphic) = 0;
		*reinterpret_cast<float*>(def + def_width)                     = 0.0f; // let the text drive the area
		*reinterpret_cast<float*>(def + def_height)                    = 0.0f;
		*reinterpret_cast<std::uint8_t*>(def + def_text_justification) = 0; // sgg::Justification::LEFT
		*reinterpret_cast<float*>(def + def_text_offset_x)             = row_text_offset_x;
		*reinterpret_cast<float*>(def + def_y)                         = row_base_y; // read by UpdateScrollState
		*reinterpret_cast<float*>(def + def_spacing)                   = row_pitch;  // read by UpdateScrollState

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

		// SetupComponent applies our zeroed graphic fields but does not actively tear down the normal/selected textures
		// a prior template already set. Clear them explicitly.
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

	// A toggle row (boolean setting): a left-justified label plus the native on/off toggle switch graphic on the right.
	// The OptionToggleButton template already supplies the toggle graphic, left-justified text and text area we only
	// realign it to our row grid (mY/mSpacing, read directly by UpdateScrollState) and choose the on/off graphic. Disabled
	// rows grey their ring (grey_toggle_graphic) and label from frame one.
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

		// Greying needs a SetupComponent pass to reach the text box and button colour. The toggle graphic is re-chosen
		// afterwards so the pass does not revert it. The button tint is switched from additive to a multiplicative dim
		// so the toggle graphic reads as greyed rather than full brightness.
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
			// Grey the on/off ring from frame one (it has no colour of its own and would otherwise stay black until a
			// hover eases it grey - see grey_toggle_graphic).
			grey_toggle_graphic(reinterpret_cast<GUIComponent*>(row));

			if (block_input && g_disable)
			{
				// Whole-mod-off toggle: also drop mIsUseable via Disable so nav and hover skip the row. A
				// context/author-disabled toggle keeps mIsUseable so it stays mouse-hoverable for its note.
				g_disable(row);
			}
		}

		finalize_row(screen, row);
		return row;
	}

	// A centered native button row (for actions like Apply/Reset), using the CategoryOptionsButton template unchanged so
	// it keeps its Button_Secondary box graphic and centered label - visually distinct from the plain-text setting rows.
	// Disabled rows are greyed by default they are also hard-disabled (non-selectable).
	static void install_wide_button_nav_rect(GUIComponent* row); // defined below (near row_bounded_area)

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

		// Stretch the box only enough to fit a label wider than the native box (see button_label_*), so short labels
		// keep the clean native box. Drawn box width = native * mScale * box_scale_x.
		const float box_scale_x = std::max(1.0f, (measure_width(label) + button_label_padding) / button_label_capacity);

		constexpr float button_scale = 0.8f;

		char* def                                     = row_bytes + component_def_offset;
		*reinterpret_cast<float*>(def + def_y)        = row_base_y;
		*reinterpret_cast<float*>(def + def_spacing)  = row_pitch;
		*reinterpret_cast<float*>(def + def_offset_y) = 0.0f;         // drop the template's built-in vertical offset
		*reinterpret_cast<float*>(def + def_scale)    = button_scale; // shrink slightly for top/bottom breathing room

		// The hover/click rect is GetArea = mCustomWidth * mScale@0x38 * mScaleX@0x114. The drawn box already reflects
		// button_scale and mScaleX@0x114 carries box_scale_x below, so mCustomWidth is the plain native width.
		*reinterpret_cast<float*>(def + def_width)  = button_graphic_native_width;
		*reinterpret_cast<float*>(def + def_height) = 58.0f;

		// Momentary selection: the CategoryOptionsButton template keeps a button selected (its highlight lit) after a
		// mouse-off - correct for the category tabs, but an action button should not stay lit like a selected tab once
		// clicked. mDeselectOnMouseOff makes the highlight clear when the cursor leaves (the highlight still shows while
		// hovered), so the action button reads as momentary.
		*reinterpret_cast<bool*>(def + def_deselect_on_mouse_off) = true;

		if (disabled)
		{
			set_def_text_grey(row);
		}

		if (g_setup_component)
		{
			g_setup_component(row, row_bytes + component_data_offset);
		}

		// SetupComponent copied the button def (with the native mCustomWidth used for the hit rect) into the child
		// label, whose own def mWidth drives where the text wraps. For a stretched box widen the label's copy to the
		// full visible width so a long label stays on one line instead of wrapping at the native width.
		if (auto* label_box = *reinterpret_cast<char**>(row_bytes + button_label_offset))
		{
			*reinterpret_cast<float*>(label_box + component_def_offset + def_width) = button_graphic_native_width * box_scale_x;
		}

		if (g_set_label)
		{
			g_set_label(row, label);
		}

		// Widen the box graphic to box_scale_x. The box is a single-frame animation reached via mAnim enabling
		// mScaleModifierOnlyX makes GUIComponentAnimation::Draw honour the anim's own def mScaleX (horizontal-only), which
		// the button otherwise leaves at a uniform scale.
		if (box_scale_x > 1.0f)
		{
			// component_def_scale_* offsets are from the component base
			*reinterpret_cast<float*>(row_bytes + component_def_scale_x_offset) = box_scale_x;
			*reinterpret_cast<float*>(row_bytes + component_def_scale_y_offset) = 1.0f;

			if (void* anim = *reinterpret_cast<void**>(row_bytes + button_anim_offset); anim)
			{
				char* anim_bytes = reinterpret_cast<char*>(anim);
				*reinterpret_cast<bool*>(anim_bytes + anim_scale_modifier_only_x_offset) = true;
				// component_def_scale_* offsets are from the component base
				*reinterpret_cast<float*>(anim_bytes + component_def_scale_x_offset) = box_scale_x;
				*reinterpret_cast<float*>(anim_bytes + component_def_scale_y_offset) = 1.0f;
			}
		}

		if (disabled && block_input && g_disable)
		{
			g_disable(row);
		}

		// The CategoryOptionsButton template is shared with the top category tabs (paged by bumpers, not the vertical
		// nav), so it leaves mFreeFormSelectable unset and SearchInDirection skips it. Opt an enabled action button in,
		// and give it a wide nav rect too, since its native GetArea is a narrow rect at the centred label.
		if (!disabled)
		{
			*reinterpret_cast<bool*>(row_bytes + component_free_form_selectable_offset) = true;
			install_wide_button_nav_rect(row);
		}

		finalize_row(screen, row);

		// Centre the button in the content pane (finalize_row anchors rows at the right-hand option column, which would
		// put the button over the scrollbar).
		row->m_location_x = button_center_x;
		return row;
	}

	// A right-justified, non-interactive value label for the right column of a key/value setting row (paired with a
	// left-column key row). It shares the key's component X anchor but uses RIGHT justification, so the value sits in the
	// right column while the key stays left.
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
		*reinterpret_cast<std::uint8_t*>(def + def_add_text_area)      = 0; // display only: no hit area
		*reinterpret_cast<std::uint8_t*>(def + def_use_text_area)      = 0;
		*reinterpret_cast<std::uint32_t*>(def + def_graphic)           = 0;
		*reinterpret_cast<std::uint32_t*>(def + def_selected_graphic)  = 0;
		*reinterpret_cast<std::uint32_t*>(def + def_alternate_graphic) = 0;
		*reinterpret_cast<float*>(def + def_width)                     = 0.0f;
		*reinterpret_cast<float*>(def + def_height)                    = 0.0f;
		*reinterpret_cast<std::uint8_t*>(def + def_text_justification) = 1; // sgg::Justification::RIGHT
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

		row->m_can_be_focused = false; // never interactive, the empty hit area blocks hover/click

		finalize_row(screen, row, false); // drawn (mComponents) but not paged (mOptions)
		return row;
	}

	// True for a finite whole number (used to pick integer vs float num-box display/stepping).
	static bool is_whole(double v)
	{
		return std::isfinite(v) && v == std::floor(v);
	}

	// Overrides a num-box's centered value text (its mValueTextBox) with an enum option label. The label is escaped so
	// paths/brackets in the option text render verbatim (see escape_markup).
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

	// A native num-box stepper row, as used by the game's own FPS-limit and graphics-quality options. The game's factory
	// allocates it and builds all five sub-components, which the row teardown frees with it. Value edits are persisted
	// by the SetNumberValue hook. Not a GUIComponentButton, so it never routes through the OnClicked hook.
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

		// Name the box and its sub-components so ApplyDataToComponent applies the matching sjson templates (its
		// virtual ApplyDataToName routes each def by the sub-component's mName).
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

		// Integer box when the bounds and step are all whole (shows "3" not "3.0" and uses the discrete single-step
		// path) otherwise a float box (decimals + analog repeat). Set the flag BEFORE SetRange, whose auto-step.
		// Derives from it, then pin our own step.
		const bool is_integer = is_whole(min_v) && is_whole(max_v) && is_whole(step_v);
		*reinterpret_cast<bool*>(nb_bytes + numbox_is_integer_offset) = is_integer;

		g_numbox_set_range(nb, static_cast<float>(min_v), static_cast<float>(max_v));
		*reinterpret_cast<float*>(nb_bytes + numbox_step_offset) = static_cast<float>(step_v != 0.0 ? step_v : 1.0);

		g_apply_data(reinterpret_cast<MenuScreen*>(screen), nb);

		// ApplyDataToComponent copies the OptionNumBox template's own row grid (Y=300, Spacing=45) into the component
		// override it to our grid so the box lines up with the other rows instead of drawing on the previous one
		// def_y/def_spacing alias the component's baseY(+0xC8) and pitch(+0x204) that UpdateScrollState reads (def sits at
		// component+0xA8).
		{
			char* def                                    = nb_bytes + component_def_offset;
			*reinterpret_cast<float*>(def + def_y)       = row_base_y;
			*reinterpret_cast<float*>(def + def_spacing) = row_pitch;
		}

		// The label lives in the num-box's own left text box (raw text, like our other rows).
		if (void* label_tb = *reinterpret_cast<void**>(nb_bytes + numbox_label_text_offset))
		{
			g_show_text(label_tb, label);
		}

		// Paint the starting value notify=false so the SetNumberValue hook does not persist it.
		g_numbox_set_value(nb, static_cast<float>(initial), false);

		// Enum cycler: replace the raw index. The box just painted with the option's label.
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
			// mDisableInput is the num-box's own input gate, blocking arrow-clicks and keyboard stepping alike;
			// mIsUseable does NOT gate num-box input. When block_input is set (the whole-mod-off case) clear mIsUseable
			// too so nav and hover skip the row entirely.
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
		nb->m_location_x = numbox_location_x; // override finalize_row's default so box + arrows clear the scrollbar
		return nb;
	}

	// Formats a numeric setting value for display. The value is rounded to the display step's precision so scaling by 100
	// does not surface floating-point noise, then trailing zeros are trimmed ("53", "0.5", "50%").
	static std::string format_setting_display(double value, bool show_as_pct, bool is_pct, double step)
	{
		double shown           = is_pct ? value * 100.0 : value;
		const double disp_step = is_pct ? step * 100.0 : step;

		// Decimal places implied by the display step (0.01 -> 2, 1 -> 0), capped for sanity.
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

		std::string out = std::to_string(shown); // fixed 6-decimal form, e.g. "53.000000"
		if (out.find('.') != std::string::npos)
		{
			const std::size_t last = out.find_last_not_of('0');
			out.erase((out[last] == '.') ? last : last + 1); // drop trailing zeros (and a bare '.')
		}
		if (show_as_pct || is_pct)
		{
			out += "%";
		}
		return out;
	}

	// Sets the slider's right-hand value text (mValueTextBox). The native drag handler rewrites this to a percentage on
	// every change, so we re-apply the setting's real value after each user edit.
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

	// Row-sized hit rect for rows whose native GetArea is unsuitable, installed via a patched vtable on the GetArea and
	// GetScreenArea slots. Two rows need it: sliders (whose GetArea unions their sub-components into a near
	// screen-spanning rect that steals hover from every other row) and centred action buttons (whose GetArea is a narrow
	// rect at the button centre that a vertical nav ray never crosses). Slider dragging runs through HandleInput, not
	// GetArea, so it is unaffected.
	static void* row_bounded_area(GUIComponent* self, std::int32_t* out)
	{
		const int left = static_cast<int>(row_location_x + row_text_offset_x); // option-name column start (~660)
		out[0]         = left;
		out[1]         = static_cast<int>(self->m_location_y) - 22;
		out[2]         = static_cast<int>(row_location_x) + 22 - left; // out to the value column (~922 wide)
		out[3]         = 44;                                           // one row tall, under row_pitch so no overlap
		return out;
	}

	// Copies vtable `src` into `dst` and redirects the GetArea (+0x98) and GetScreenArea (+0xA0) slots to
	// row_bounded_area, so the row hit- and nav-tests as one option-column row. The copy is byte-identical otherwise,
	// so every other virtual (ctor/dtor/Draw/HandleInput/...) behaves as native. Returns dst as a vtable pointer.
	static std::uintptr_t build_row_area_vtable(std::uintptr_t* dst, std::size_t dst_bytes, std::uintptr_t src)
	{
		std::memcpy(dst, reinterpret_cast<const void*>(src), dst_bytes);
		dst[0x98 / sizeof(std::uintptr_t)] = reinterpret_cast<std::uintptr_t>(&row_bounded_area);
		dst[0xA0 / sizeof(std::uintptr_t)] = reinterpret_cast<std::uintptr_t>(&row_bounded_area);
		return reinterpret_cast<std::uintptr_t>(dst);
	}

	// Installs the patched button vtable (see build_row_area_vtable) on `row`, building the copy lazily from the row's
	// current native vtable on first use. A centre-column action button's native GetArea is a narrow rect at the button
	// centre that the vertical nav ray never crosses. The wide rect makes it reachable like any setting row.
	static void install_wide_button_nav_rect(GUIComponent* row)
	{
		if (!g_button_vtable_patched)
		{
			const std::uintptr_t native_vtable = *reinterpret_cast<std::uintptr_t*>(row);
			g_button_vtable_patched = build_row_area_vtable(g_button_vtable_copy, sizeof(g_button_vtable_copy), native_vtable);
		}
		*reinterpret_cast<std::uintptr_t*>(row) = g_button_vtable_patched;
	}

	// A native slider row (the volume-style drag bar) for a bounded numeric setting. The slider stores a normalized
	// 0..1 fraction, so [min,max] is mapped onto it and drags are snapped to `step` in the SetFraction hook. The engine
	// exposes no factory for this type, so this replicates what DoShowCategory does for the volume rows.
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

		// Base GUIComponent constructor (location passed by value. 0 = origin, overridden below by
		// ApplyDataToComponent./finalize_row), then install the patched slider vtable (bounded GetArea) over the base
		// one, falling back to the unpatched native vtable if the copy was not built.
		g_gui_component_ctor(s, 0);
		*reinterpret_cast<std::uintptr_t*>(s) = g_slider_vtable_patched ? g_slider_vtable_patched : g_slider_vtable;

		// Defaults does not initialise mOnValueChanged or mValueTextBox, so zero them (the block is freshly malloc'd)
		// before Defaults runs and before anything reads them.
		std::memset(s + slider_on_changed_offset, 0, 3 * sizeof(void*));
		*reinterpret_cast<void**>(s + slider_label_offset)      = nullptr;
		*reinterpret_cast<void**>(s + slider_value_text_offset) = nullptr;

		g_slider_defaults(s);
		*reinterpret_cast<void**>(s + slider_owner_offset) = screen;

		// Four owned sub-components, each allocated then constructed at the origin (as the game does): two images (bar
		// background + fill) and two text boxes (left label + right value).
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

		// Parent container, matching DoShowCategory SetParent is a plain setter (writes mParentContainer), so a direct
		// write is equivalent and avoids a vtable call. SetParent writes. GUIComponent::GUIComponent::mParentContainer.
		*reinterpret_cast<void**>(s + slider_parent_offset) = reinterpret_cast<char*>(screen) + menu_screen_container_offset;

		// Name the slider and its value box so ApplyDataToComponent applies the OptionSlider/OptionSliderValueText
		// templates (bar graphics, colours, FadeSpeed and the label styling).
		set_sso_string(s + gui_component_name_offset, "OptionSlider");
		set_sso_string(val + gui_component_name_offset, "OptionSliderValueText");

		g_apply_data(reinterpret_cast<MenuScreen*>(screen), reinterpret_cast<GUIComponent*>(s));

		// Override the template's row grid (Y=300, Spacing=45) so the bar lines up with the other rows.
		{
			char* def                                    = s + component_def_offset;
			*reinterpret_cast<float*>(def + def_y)       = row_base_y;
			*reinterpret_cast<float*>(def + def_spacing) = row_pitch;
		}

		if (void* label_tb = *reinterpret_cast<void**>(s + slider_label_offset))
		{
			g_show_text(label_tb, label);
		}

		// Paint the starting value: map [min,max] -> 0..1 and set the fraction without notifying (so the SetFraction
		// hook does not treat it as a user edit), then show the real value (not a percentage).
		const double range = max_v - min_v;
		const float frac   = (range > 0.0) ? static_cast<float>((initial - min_v) / range) : 0.0f;
		g_slider_set_fraction(s, frac, false);
		set_slider_value_text(reinterpret_cast<GUIComponent*>(s),
		                      format_setting_display(initial, show_as_pct, is_pct, step_v).c_str());

		if (disabled)
		{
			// Grey every visible part explicitly: the value box and both bar images, plus the label (which Slider::Draw would
			// otherwise only grey off mIsUseable). Mouse-drag is separately blocked in the HandleInput hook (the native drag
			// path ignores mIsUseable).
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
		reinterpret_cast<GUIComponent*>(s)->m_location_x = slider_location_x; // override finalize_row's default
		return reinterpret_cast<GUIComponent*>(s);
	}

#pragma endregion

#pragma region Row teardown and mod list

	// Removes the first pointer equal to `value` from an eastl vector by shifting the tail down in place - the same
	// unlink the engine's DoShowCategory performs. No-op if not present. The backing storage is left owned by the
	// vector.
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

	// Tears down every custom row we currently own: clears any screen pointer that still references a row (so the engine
	// cannot dereference it after free), unlinks it from the drawn/hit-tested mComponents and the paged mOptions, then
	// destroys and frees it. Our rows are not registered in the reflection helper, so the engine never frees them and
	// never double-frees here.
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
				// The num-box and slider are not GUIComponentButtons destruct through the component's own vtable so its owned
				// sub-components (num-box: box/label/value/arrows slider: background/fill/label/value) are freed too flags=0
				// destructs without the final operator delete, so we still free the block ourselves (see game_free).
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

	// Level 1: one row per installed mod (config-file stem), friendly display name, sorted.
	static void build_mod_list(MiscSettingsScreen* screen)
	{
		std::vector<std::string> stems;
		for (const auto* cfg : toml_v2::config_file::g_config_files)
		{
			if (!cfg || cfg->m_config_file_stem_as_str.empty())
			{
				continue;
			}

			// H2M's own framework config is not a mod the user configures here.
			if (cfg->m_config_file_stem_as_str == "Hell2Modding-Hell2Modding-General")
			{
				continue;
			}
			if (std::find(stems.begin(), stems.end(), cfg->m_config_file_stem_as_str) == stems.end())
			{
				stems.push_back(cfg->m_config_file_stem_as_str);
			}
		}

		std::vector<std::pair<std::string, std::string>> mods; // (display name, stem)
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
			// A mod that called rom.mod_settings.opt_out() is still listed (dropping it would look like a missing mod), but its
			// row is greyed and cannot be opened, and its description is a note pointing back to the mod's own description. The
			// drilldown is blocked by the disabled flag in the click handler.
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

	// Turns an identifier into a friendly display string: underscores become spaces, and camelCase/PascalCase word
	// boundaries are split ("z_ThisConfigKey" -> "z. The first letter is capitalized ("enabled" -> "Enabled").
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

		// Capitalize the first letter so a key/mod name with no author display_name still reads as a proper title
		// ("enabled" -> "Enabled").
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

	// Renders the edit buffer with a caret marker at `cursor`, windowed by visual WIDTH so the caret stays visible and the
	// whole string fits the value column (value_display_max_width) without running into the key label.
	static std::string render_edit_display(const std::string& buf, std::size_t cursor, bool blink_on)
	{
		const char* caret     = blink_on ? "|" : " ";
		const std::size_t len = buf.size();
		if (cursor > len)
		{
			cursor = len;
		}

		const float caret_w    = 0.6f; // reserve a little for the caret glyph
		const float ellipsis_w = measure_width("...");

		// Whole buffer (plus caret) fits: no truncation.
		if (measure_width(buf) + caret_w <= value_display_max_width)
		{
			return escape_markup(buf.substr(0, cursor)) + caret + escape_markup(buf.substr(cursor));
		}

		// Grow a window [start, end) outward from the caret, one codepoint at a time, alternating left then right,
		// while it still fits the budget (accounting for the ellipses each side will need). Left grows first each round
		// so a right-aligned field shows preceding context.
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

	// Accepts a character into a numeric edit buffer only if the result stays a plausible numeric literal: an optional
	// leading sign (only at the front), digits, at most one decimal point `cursor` is where the character would be
	// inserted.
	static bool numeric_char_ok(const std::string& buffer, std::size_t cursor, char c)
	{
		if (c >= '0' && c <= '9')
		{
			return true;
		}
		if (c == '-' || c == '+')
		{
			// A sign is valid only inserted at the very front, and only if no sign is there already.
			return cursor == 0 && (buffer.empty() || (buffer.front() != '-' && buffer.front() != '+'));
		}
		if (c == '.')
		{
			return buffer.find('.') == std::string::npos; // a single decimal point.
		}
		return false;
	}

	// Window-procedure callback: while a freetext setting is being edited, capture typed characters and caret movement
	// into the edit buffer. A mouse click anywhere commits the edit (Enter/Escape are read from the game input in the
	// HandleInput hook, which also blocks the menu from reacting).
	static void on_wndproc(HWND, UINT msg, WPARAM wparam, LPARAM)
	{
		if (!g_editing)
		{
			return;
		}

		if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN)
		{
			// Clicking away from the edited row submits the current value, like Enter.
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
			if (c < 32 || c >= 127) // control chars handled via WM_KEYDOWN
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

	// Registers on_wndproc with the framework's window hook once editing needs typed input.
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

	// Enters freetext edit on `value_component` (the row's right-column value display).
	static void enter_edit_mode(GUIComponent* value_component, toml_v2::config_file::config_entry_base* entry)
	{
		ensure_wndproc_registered();
		g_editing        = true;
		g_edit_component = value_component;
		g_edit_entry     = entry;
		g_edit_buffer    = entry ? entry->get_serialized_value() : std::string{};
		g_edit_cursor    = g_edit_buffer.size(); // caret starts at the end
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

	// Composite key ("<stem>\0<section>\0<key>") uniquely identifying a config entry across mods.
	static std::string restart_change_key(toml_v2::config_file::config_entry_base* entry, const std::string& stem)
	{
		return stem + '\0' + entry->m_definition.m_section + '\0' + entry->m_definition.m_key;
	}

	// Captures a restart-required setting's baseline (its value as of this menu session's open) BEFORE It is first
	// modified, so a later change back to this value can be recognised as "no net change". Called just before the value
	// is written. No-op for non-restart-required settings and after the first capture for a given setting.
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

	// The current game display-language folder code (e.g. "en", "zh-TW"), or "" if unavailable. Read from
	// sgg::ConfigOptions::Language (an eastl SSO string whose code chars sit at offset 0, null-terminated).
	static std::string current_language_code()
	{
		return g_config_language ? std::string(g_config_language) : std::string();
	}

	// Resolves a localized string to the current game language: the entry for the current language code, then English,
	// then the unlocalized value (empty key), then any entry. Resolution happens here (render time), so re-entering the
	// tab after a language change picks up the new language.
	static std::string resolve_localized(const localized_text& t)
	{
		if (t.empty())
		{
			return {};
		}
		if (t.size() == 1)
		{
			return t.begin()->second; // one entry (a plain value, or the only language provided)
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

	// True if the current settings view has any row with a dynamic (Lua-function) description field. Recomputed each
	// build (see build_panel). Consulted when a bool toggle changes so the panel is rebuilt in place to re-evaluate
	// dynamic disabled/ranges/options against the new value.
	static bool g_view_has_dynamic = false;

	// A setting's metadata with any dynamic (Lua-function) description fields evaluated against the current game state.
	// Records that the view has a dynamic row so a later toggle can rebuild to re-evaluate it.
	static std::optional<setting_metadata> resolved_metadata(const std::string& stem, const std::string& section, const std::string& key)
	{
		auto meta = get_setting_metadata(stem, section, key);
		if (!meta)
		{
			// Not a config-backed setting registered at load (e.g. a virtual row, whose metadata lives only in the Lua
			// descs registry). Resolve it straight from there. Returns nullopt when there is no rich description there
			// either (a Chalk setting, or a plain-string desc), exactly as before.
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

	// The friendly display name for a setting: the author's `display_name` override when provided, otherwise the
	// prettified key. Mirrors how the setting rows are labelled.
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
	// session baseline (e.g. a toggle flipped and flipped back, or a number re-typed to its original), nothing actually
	// changed, so the setting is dropped from the restart list otherwise.
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
			// Matches the session baseline, so it has no net restart requirement
			g_restart_changes.erase(key);
		}
		else
		{
			// Stored plain word-wrapped with regular spaces for the dialog in build_restart_message.
			const std::string line = display_name_from_stem(stem) + ": "
			                         + setting_display_name(stem, entry->m_definition.m_section, entry->m_definition.m_key) + " (" + new_value_display + ")";
			g_restart_changes[key] = line;
		}

		g_restart_required = !g_restart_changes.empty();
	}

	// The config section to use for a row's virtual-row Lua I/O (get/set/text). A `group` override can place a row on a
	// menu page whose path differs from where its configDesc lives, so runtime lookups use the row's stored real
	// config section, falling back to the current view path for rows built before that field was set.
	static const std::string& row_io_section(const PanelRow* row)
	{
		return !row->config_section.empty() ? row->config_section : g_view_section;
	}

	// Commit helpers for an edited row: they write the config entry (with restart-required tracking) when the row is
	// config-backed, or call the interactive virtual row's Lua set() callback when it is virtual. Each returns true if
	// the value actually changed, and arms the dynamic live-refresh so dependent rows re-evaluate.
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

	// `serialized` is the config-serialized value (also the enum option's stored value). A config entry parses it back.
	// A virtual set() receives it as a string (virtual enum options are matched/passed as strings).
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

	// Refreshes a freetext row's right-column value display to show `serialized`, formatted exactly as
	// build_mod_settings renders it (width-truncated with a leading ellipsis, then markup-escaped). Used to reflect a
	// committed or cancelled edit in place, without a panel rebuild.
	static void refresh_value_display(GUIComponent* value_component, const std::string& serialized)
	{
		if (value_component && g_set_label)
		{
			const std::string disp = escape_markup(truncate_value(serialized));
			g_set_label(value_component, disp.c_str());
		}
	}

	// Commits or cancels a pending edit. Called from the HandleInput hook so it runs on the same frame the triggering
	// key/click is swallowed (HandleInput returns true that frame), which prevents a submitting mouse click from also
	// activating the row it lands on. Returns true if the edit ended this call.
	static bool commit_or_cancel_edit()
	{
		if (g_edit_confirm)
		{
			if (g_edit_entry)
			{
				// Capture the session baseline before the first write so a later revert is recognised as "no net
				// change".
				capture_restart_baseline(g_edit_entry);

				// set_serialized_value validates (e.g. numbers) and only stores/saves a valid value, so bad input for a
				// number simply keeps the previous value.
				g_edit_entry->set_serialized_value(g_edit_buffer);

				// Clamp/snap a bounded freetext number to the stepper grid: [min, max] and min + k*step.
				// set_serialized_value above already parsed/validated the number.
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
							v                 = clamp_range(v); // snapping may overshoot a bound
						}
						g_edit_entry->set_value_base<double>(v);
					}
				}

				// If the author declared this setting restart-required, flag/clear the restart.
				note_change_if_restart_required(g_edit_entry, g_edit_entry->get_serialized_value());

				// Reflect the committed value in the right-hand display in place. Do NOT rebuild the panel here: a rebuild frees
				// and recreates every row, which snaps the visible page back to the top while the scrollbar keeps the scrolled
				// position, so the rows and the scrollbar desync until the next manual scroll.
				refresh_value_display(g_edit_component, g_edit_entry->get_serialized_value());

				// Other rows may still key off this value (e.g. an apply button's dynamic `disabled`), so a dynamic
				// view re-evaluates its function rows shortly after (see g_dynamic_refresh_settle).
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
			// Restore the display to the unchanged value (the live caret label was transient) no rebuild, for the same
			// scroll-preservation reason as the commit path above.
			if (g_edit_entry)
			{
				refresh_value_display(g_edit_component, g_edit_entry->get_serialized_value());
			}
			exit_edit_mode();
			return true;
		}
		return false;
	}

	// Live-updates the edited value display (right column) with a movable, blinking caret. Called from Update while
	// editing is active g_edit_component is the row's value component.
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

	// True if `key` is the mod's master enable switch ("enabled", any case).
	static bool is_enabled_key(const std::string& key)
	{
		return big::string::to_lower(key) == "enabled";
	}

	// True if a config entry carries an author-written description string. Our own loader also writes the description here
	// for string/`description`-field descs, and additionally records metadata-only descs in g_described_keys, so the two
	// checks together recognize every configDesc form as "described".
	static bool entry_has_description(const toml_v2::config_file::config_entry_base* entry)
	{
		return entry && !entry->m_description.m_description.empty();
	}

	// True when the options screen was opened during gameplay (a save is loaded), false when opened from the main menu.
	// Captured from the MiscSettingsScreen constructor's "opened from" argument (see hook_MiscSettingsScreen_ctor).
	// Used to grey out context-restricted setting rows.
	static bool g_opened_in_game = false;

	// True when the game global `CurrentHubRoom` is non-nil, i.e. the player is in the hub (the Crossroads) rather than in
	// a run. Captured once in the ctor (see hook_MiscSettingsScreen_ctor) via game_is_in_hub() - the context cannot change
	// while the pause screen is open.
	static bool g_in_hub = false;


	// True while a native options screen is open. Set in the ctor, cleared when it closes in ExitScreen.
	static bool g_options_screen_open = false;

	// True while a setting change should notify its mod through an on_change callback: an options screen is currently
	// open. Fires for any edit made through our options menu, in the main menu or in a save (so a callback can also
	// drive other rows' dynamic min/max/values/disabled), but not from a mod's own config write outside the menu.
	// Callbacks must guard live-run access (game.CurrentRun and GameState may be absent in the main menu).
	bool on_change_callbacks_enabled()
	{
		return g_options_screen_open;
	}

	// The MiscSettingsScreen ctor's "opened from" argument is the opening screen (sgg::MenuScreen*): a MainMenuScreen when
	// opened from the main menu, a PauseScreen when opened in-game (the only two call sites in the engine).
	// GameScreen::GetType (virtual, vtable slot 10 - a `mov eax,imm ret` stub, so calling.
	static constexpr std::size_t game_screen_get_type_vtable_slot = 10;
	static constexpr int screen_type_pause                        = 0x10'00'03; // sgg::ScreenType::Pause

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

	// The context in which a setting may actually be changed. Authors declare it, but the master "enabled" toggle and
	// any restart_required setting are. Forced to main_menu because neither can take effect on the live save.
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

	// True when a setting cannot be changed in the current screen context (main-menu vs in-game vs in-hub), so its row
	// is shown read-only with an explanatory note instead of an editable widget.
	static bool is_context_restricted(editable_context ctx)
	{
		switch (ctx)
		{
		case editable_context::main_menu: return g_opened_in_game;  // main-menu-only, greyed while in a save.
		case editable_context::in_save:   return !g_opened_in_game; // in-save-only, greyed at the main menu.
		case editable_context::in_hub:    return !(g_opened_in_game && g_in_hub); // hub-only, greyed at menu/mid-run.
		default:                          return false;                                                    // any.
		}
	}

	// The note shown in the description box for a row that is read-only because of its editable context. Empty for
	// `any` (never restricted).
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

	// Description-box text for a context-restricted (editableContext-blocked) row: the scenario note on the first line(s),
	// then the row's normal description below it, so the box explains BOTH why the row is read-only here and what it does.
	// The break is a '\n', handled like the restart dialog's build_list_message.
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

	// True if the mod's config file `cfg` has a direct config entry at (section, key). Used so a group defers a
	// group-consumed desc field (displayName/description/order/hidden) to a real config child of the same name:
	// a group's desc table doubles as its children's descriptions, so such a field belongs to the child, not the group.
	static bool config_child_exists(toml_v2::config_file* cfg, const std::string& section, const std::string& key)
	{
		if (!cfg)
		{
			return false;
		}
		toml_v2::config_definition def(section, key);
		return cfg->try_get_entry(def) != nullptr;
	}

	// Menu paths from a `group` override that resolved to neither a config section nor a declared category, already
	// warned about (keyed "<stem>\0<path>"), so the per-frame rebuild logs each bad target only once.
	static std::set<std::string> g_warned_group_overrides;

	// The menu path an entry appears at: its `group` override (resolved to a root_section-rooted dotted path) if it has
	// one, else the entry's own config section. Author-group segments and config-section names share this path space,
	// so navigation, bucketing and RowIdentity all keep using dotted-string paths.
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

	// Finds an author-declared menu group (configDesc `groups`) by its full menu path (walking the tree by the segments
	// after root_section), or nullptr if the path names no author group (e.g. it is a config section instead).
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

	// Resolves an entry's menu path: its `group` override (validated against author groups and the mod's config
	// sections) else its config section. A `group` naming neither is logged once and falls back to the config-section
	// placement, so a typo leaves the row where its value lives. Shared by the panel builder and Reset so both agree.
	static std::string resolve_entry_menu_path(const std::string& stem, const std::vector<menu_group>& author_groups, toml_v2::config_file* view_cfg, const std::string& csection, const std::vector<std::string>& group)
	{
		if (group.empty())
		{
			return csection;
		}
		const std::string m = menu_path_of(csection, group);
		if (find_author_group(author_groups, m))
		{
			return m; // a declared author group (the common case)
		}
		if (view_cfg) // or an existing config section the row is being merged into
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

	// True if menu path `p` lies within the current view scope `scope`: the scope page itself or any of its descendant
	// subgroups. Used to limit Reset to the drilled-in group (and its subgroups) rather than the whole mod.
	static bool menu_path_in_scope(const std::string& p, const std::string& scope)
	{
		return p == scope || p.rfind(scope + ".", 0) == 0;
	}

#pragma endregion

#pragma region Panel builder

	// Level 2: the leaf settings and nested groups inside config section `section` of mod `stem`. Leaf entries render as
	// setting rows (bool -> toggle, enum/bounded number -> num box, else a freetext value).
	// A menu item is either a leaf setting directly in `section`, or a direct child group (a nested sub-section
	// such as "config.biome_pool" while viewing "config").
	struct panel_item
	{
		bool is_group = false;
		std::string key;                                          // leaf key, or the group's last path segment
		toml_v2::config_file::config_entry_base* entry = nullptr; // leaf only
		std::string child_section;                                // group only (full menu path, e.g. "config.x.y")
		std::string config_section; // the entry's REAL config section (for virtual I/O - group: its parent config section)
		bool is_author_group = false;      // group only: declared in configDesc `groups` (not a config section)
		localized_text author_name;        // author-group display name (is_author_group only)
		localized_text author_description; // author-group description (is_author_group only)
		bool has_order  = false;
		double order    = 0.0;
		std::string sort_name;            // resolved display name, the alphabetical fallback sort key
		bool is_enabled = false;          // the mod's master "enabled" toggle (root section only)
		bool is_action  = false;          // a config.lua action button (runs a Lua callback, no config value)
		action_info action;               // valid when is_action
		bool is_virtual          = false; // a config.lua virtual row (Lua get/text/set, no config value)
		bool virtual_interactive = false; // the virtual row has a `set` (an editable get/set widget)
	};

	// One page of the Mods tab before any native widget exists: the rows in display order, plus what the row builder
	// needs to know about the mod as a whole.
	struct panel_contents
	{
		std::vector<panel_item> items;
		toml_v2::config_file::config_entry_base* enabled_entry = nullptr; // the mod's master "enabled" toggle
		toml_v2::config_file* view_cfg                         = nullptr; // this mod's config file (for child lookups)
		bool mod_enabled                                       = true;
	};

	// Collects every row belonging on page `section` of mod `stem` - settings, child groups, action buttons and virtual
	// rows - and sorts them into display order.
	static panel_contents collect_panel_items(const std::string& stem, const std::string& section)
	{
		panel_contents out;
		std::vector<panel_item>& items = out.items;

		std::map<std::string, panel_item> groups;        // child menu path -> group item
		toml_v2::config_file*& view_cfg  = out.view_cfg; // this mod's config file (for child lookups)
		const std::string section_prefix = section + ".";

		// The categories a per-entry `group` can target that do not exist as config sections.
		const std::vector<menu_group> author_groups = mod_menu_groups(stem);

		// Delegates to the shared resolver so the panel and Reset agree on placement.
		auto resolve_menu_path = [&](const std::string& csection, const std::vector<std::string>& group) -> std::string
		{
			return resolve_entry_menu_path(stem, author_groups, view_cfg, csection, group);
		};

		// Where an entry sits relative to the current view: 0 = not on this page, 1 = a direct row here, 2 = inside a
		// child group (its full menu path returned in child_out).
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

		// Creates the child group row at `child_path` (no-op if it already exists). A group declared in configDesc
		// `groups` takes its name/order/description from there; otherwise it is config-derived and resolved in the
		// render. Its sort name mirrors the label the render picks, so the alphabetical fallback matches what is shown.
		auto ensure_group = [&](const std::string& child_path)
		{
			if (groups.contains(child_path))
			{
				return;
			}
			panel_item g;
			g.is_group      = true;
			g.child_section = child_path;
			g.key           = child_path.substr(child_path.rfind('.') + 1); // the child's last path segment
			if (const menu_group* ag = find_author_group(author_groups, child_path))
			{
				g.is_author_group    = true;
				g.author_name        = ag->name;
				g.author_description = ag->description;
				g.sort_name          = resolve_localized(ag->name);
				if (ag->has_order)
				{
					g.has_order = true;
					g.order     = ag->order;
				}
			}
			else if (const auto meta = resolved_metadata(stem, section, g.key); meta)
			{
				g.sort_name = resolve_localized(meta->name);

				// Config-derived group: its metadata is configDesc.<section>.<child>, resolved here for order and again
				// in the render for name/description. Defers to a real config child named "order".
				if (meta->has_order && !config_child_exists(view_cfg, child_path, "order"))
				{
					g.has_order = true;
					g.order     = meta->order;
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
			if (!cfg || cfg->m_config_file_stem_as_str != stem)
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

				// Tracked whatever section is shown, so nested rows are greyed when the mod is disabled.
				if (!out.enabled_entry && key.m_section == root_section && entry->type() == typeid(bool) && is_enabled_key(key.m_key))
				{
					out.enabled_entry = entry.get();
				}

				// Undescribed keys are a mod's internal bookkeeping, so they stay off the page. The master "enabled"
				// toggle is the exception, always shown so the mod stays toggleable even if undescribed.
				const bool is_enabled_toggle = key.m_section == root_section && entry->type() == typeid(bool) && is_enabled_key(key.m_key);
				if (!is_enabled_toggle && !setting_is_described(stem, key.m_section, key.m_key)
				    && !entry_has_description(entry.get()))
				{
					continue;
				}

				// `group` is a static field, so the cheap (no-Lua) stored metadata is enough to place the entry.
				// Everything else still uses its real config section.
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

		// Collected across ALL config sections (empty section = all) and bucketed by menu path, so an action moved with
		// `group` lands on its target page like any setting.
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

		// Interleaved with the settings by `order`/source rank. A dynamic field on a row that lands on THIS page makes
		// an edit re-run this build, for live refresh.
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

		// Row order: the master "enabled" toggle is pinned to the top, then rows carrying an authored `order` (ascending),
		// then everything else alphabetically by its displayed name. Sorting on the display name (not the config key)
		// keeps the list alphabetical in whatever language is active, matching what the player actually reads.
		std::stable_sort(items.begin(),
		                 items.end(),
		                 [](const panel_item& a, const panel_item& b)
		                 {
			                 if (a.is_enabled != b.is_enabled)
			                 {
				                 return a.is_enabled; // enabled toggle first
			                 }
			                 if (a.is_enabled)
			                 {
				                 return false; // only one enabled entry exists
			                 }
			                 if (a.has_order != b.has_order)
			                 {
				                 return a.has_order; // ordered rows before unordered ones
			                 }
			                 if (a.has_order && a.order != b.order)
			                 {
				                 return a.order < b.order;
			                 }
			                 return compare_display_names(a.sort_name, b.sort_name) < 0;
		                 });

		return out;
	}

	// Builds the native widget for each collected row, in order, appending to g_rows.
	static void build_panel_rows(MiscSettingsScreen* screen, const std::string& stem, const std::string& section, const panel_contents& contents)
	{
		const bool mod_enabled               = contents.mod_enabled;
		toml_v2::config_file* const view_cfg = contents.view_cfg;

		for (const auto& it : contents.items)
		{
			const bool is_enabled_row = it.is_enabled;
			const bool disabled       = !is_enabled_row && !mod_enabled;

			// An action button runs a Lua callback (config.lua `action`). It edits no config value. It is greyed and
			// inert when the mod is disabled, when the author marked it `disabled`, or when its editable_context does
			// not match the current screen (main-menu vs in-save).
			if (it.is_action)
			{
				if (it.action.has_dynamic)
				{
					g_view_has_dynamic = true;
				}
				const bool ctx_blocked  = is_context_restricted(it.action.context);
				const bool mod_off      = disabled; // the whole mod is disabled
				const bool act_disabled = mod_off || it.action.disabled || ctx_blocked;
				const std::string name  = resolve_localized(it.action.name);
				const std::string label = escape_markup(name.empty() ? key_to_display(it.key) : name);

				// A mod-off action is hard-disabled (block_input). An author-disabled or context-blocked action is only
				// greyed (block_input=false), so it stays focusable/hoverable to show its note - clicks are still
				// blocked by pr.disabled in the OnClicked hook. This mirrors context-restricted settings.
				if (auto* row = make_button_row(screen, label.c_str(), act_disabled, /*block_input*/ mod_off))
				{
					// A mod-off action button is fully inert: clearing mSelectable makes MenuScreen::SetMouseOver skip
					// it so it never highlights or takes the selection, and m_can_be_focused = false blocks keyboard
					// focus too. A soft-disabled action keeps both so it can be highlighted (mouse) to read its note.
					if (mod_off)
					{
						row->m_can_be_focused = false;
						*reinterpret_cast<bool*>(reinterpret_cast<char*>(row) + sgg::gui_component_button_selectable_offset) = false;
					}
					else
					{
						// Soft-disabled (author-disabled or context-blocked) but kept useable so it can be highlighted
						// to show its note. Clear the hover + selection overlays so it does not flash a clickable
						// glow on mouse-over (the grey label already signals it is disabled, like a text row).
						*reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(row) + sgg::gui_component_button_under_mouse_texture_offset) = 0;
						if (g_set_selected_texture)
						{
							g_set_selected_texture(row, 0);
						}
					}
					PanelRow pr{row, RowKind::action, stem, it.key};
					pr.disabled       = act_disabled;
					pr.target_section = it.action.section; // the section the action's callback lives in.

					// A context mismatch shows the scenario note first, then the normal description below it. An
					// author-disabled action shows its disabledDescription (falling back to the normal description) so
					// the author can explain why it is greyed.
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

			// A virtual row's value comes from Lua callbacks, not a config entry. There is no `hidden`: with no backing
			// state, an author omits one by not declaring it.
			if (it.is_virtual)
			{
				// A `group` override can move a virtual row onto a page whose path differs from its config section, so
				// all its Lua I/O (metadata/display/get) uses the row's real config section, not the view path.
				const std::string& vsection = it.config_section;
				const auto vmeta            = resolved_metadata(stem, vsection, it.key);
				const std::string vname     = vmeta ? resolve_localized(vmeta->name) : std::string{};
				const std::string vlabel    = escape_markup(!vname.empty() ? vname : key_to_display(it.key));
				const std::string vdesc     = vmeta ? resolve_localized(vmeta->description) : std::string{};

				// A read-only virtual row, or an interactive row with no widget, becomes key + value text. mIsUseable
				// stays on so the mouse can still resolve it for the description.
				const auto build_readonly = [&](const std::string& value_text)
				{
					if (auto* row = make_text_row(screen, vlabel.c_str(), /*disabled*/ false, /*block_input*/ false, /*no_hover_highlight*/ true))
					{
						PanelRow pr{row, RowKind::info, stem, it.key};
						pr.disabled       = true;
						pr.config_section = vsection;
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

				// Interactive row. If get() returns nil, `type` can force a widget. Seed it from `default` or a
				// fallback so it still builds.
				virtual_value vv = get_virtual_value(stem, vsection, it.key);
				if (vv.type == virtual_value::kind::none && vmeta && vmeta->type != widget_type::inferred)
				{
					const std::string& dflt = vmeta->default_value; // empty when no default declared
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
				const bool is_enum   = vmeta && !vmeta->values.empty();
				const bool is_bool   = vv.type == virtual_value::kind::boolean;
				const bool is_number = vv.type == virtual_value::kind::number;
				const double step    = (vmeta && vmeta->has_step) ? vmeta->step : 1.0;
				const bool is_stepper = !is_enum && is_number && vmeta && vmeta->has_min && vmeta->has_max && !vmeta->freetext;

				// The current value serialized the same way config values/enum options are, for enum matching and the
				// read-only fallback.
				std::string vv_serialized;
				switch (vv.type)
				{
				case virtual_value::kind::boolean: vv_serialized = vv.as_bool ? "true" : "false"; break;
				case virtual_value::kind::number:  vv_serialized = std::format("{}", vv.as_number); break;
				case virtual_value::kind::string:  vv_serialized = vv.as_string; break;
				default:                           break;
				}

				// The whole mod being disabled greys the widget (native look). Author `disabled` or an editableContext
				// mismatch render the value read-only with a note, like a config setting.
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

				// Read-only presentation for a context/author-disabled interactive row (unless the mod is fully off,
				// whose native greying covers it below).
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
						pr.config_section = vsection;
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
					// get() returned a string with no `values`, or nil: interactive free-text virtual rows are not
					// supported yet, so show the current value read-only rather than an uneditable input.
					build_readonly(truncate_value(vv_serialized));
					continue;
				}

				if (row)
				{
					PanelRow pr{row, RowKind::setting, stem, it.key};
					pr.disabled         = disabled;
					pr.is_virtual_input = true;
					pr.config_section = vsection; // real config section for runtime get/set (may differ from view path)
					pr.value_component = value;
					pr.description     = vdesc;
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
						pr.toggle_value = vv.as_bool; // last-drawn state, for the flip fallback when get() is nil
					}
					g_rows.push_back(pr);
				}
				continue;
			}

			// A nested group drills into its child menu path when activated. A config-derived group takes its name and
			// description from its configDesc entry; an author group carries its own, captured during collection.
			if (it.is_group)
			{
				std::string glabel;
				std::string gdescription;
				if (it.is_author_group)
				{
					const std::string gname = resolve_localized(it.author_name);
					glabel                  = escape_markup(!gname.empty() ? gname : key_to_display(it.key));
					gdescription            = resolve_localized(it.author_description);
				}
				else
				{
					auto gmeta = resolved_metadata(stem, section, it.key);

					// A group's desc table doubles as its children's descriptions. If displayName/description/hidden is
					// one of the group's own config children, defer to that child.
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
					}

					if (gmeta && gmeta->hidden)
					{
						continue;
					}
					const std::string gname = gmeta ? resolve_localized(gmeta->name) : std::string{};
					glabel                  = escape_markup(!gname.empty() ? gname : key_to_display(it.key));
					gdescription            = gmeta ? resolve_localized(gmeta->description) : std::string{};
				}

				if (auto* row = make_text_row(screen, glabel.c_str(), disabled))
				{
					PanelRow pr{row, RowKind::group, stem, {}};
					pr.disabled       = disabled;
					pr.target_section = it.child_section;
					pr.description    = gdescription;
					g_rows.push_back(std::move(pr));
				}
				continue;
			}

			const std::string& key = it.key;
			auto* entry            = it.entry;

			// Author metadata (if any) can rename the row, hide it, and (later) pick its widget.
			const auto meta = resolved_metadata(stem, entry->m_definition.m_section, entry->m_definition.m_key);
			if (meta && meta->hidden)
			{
				continue;
			}

			// Author-`disabled` keeps the row visible but read-only and greyed. Distinct from the whole-mod-off greying,
			// which keeps the native widgets.
			const bool author_disabled = meta && meta->disabled;
			const std::string mname    = meta ? resolve_localized(meta->name) : std::string{};
			const std::string label    = escape_markup(!mname.empty() ? mname : key_to_display(key));

			// An enum cycles its label list in a num-box; a bounded number gets a slider unless the author set `freetext`
			// (better for a very large range). Everything else is a freetext-editable right-column value.
			const bool is_number  = entry->type() == typeid(double);
			const bool is_enum    = meta && !meta->values.empty();
			const bool is_stepper = !is_enum && is_number && meta && meta->has_min && meta->has_max && !meta->freetext;
			const double step     = (meta && meta->has_step) ? meta->step : 1.0;

			// Enum option lists (serialized values + parallel labels), resolved once so the widget and the PanelRow
			// share them. The current value maps to its index, defaulting to 0.
			std::vector<std::string> enum_values;
			std::vector<std::string> enum_labels;
			int enum_index = 0;
			if (is_enum)
			{
				enum_values = meta->values;

				// Labels parallel the values when the author supplied a full set (each resolved to the current
				// language) otherwise the raw values double as their own labels.
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
			bool built_stepper  = false; // num-box fallback used when the slider could not be built
			bool built_enum     = false;
			bool built_toggle   = false;

			// A context-blocked or author-disabled setting still takes focus, so the description box can explain why it
			// is unavailable. Edits are blocked by pr.disabled in the row handlers.
			const editable_context ctx = effective_editable_context(meta, is_enabled_row);
			const bool context_blocked = is_context_restricted(ctx);
			if (!disabled && (context_blocked || author_disabled))
			{
				// Greyed but still visible: keep the real widget focusable so the description can explain why it is
				// unavailable. Edits are blocked by pr.disabled.
				GUIComponent* ro_row   = nullptr;
				GUIComponent* ro_value = nullptr;
				bool ro_is_toggle      = false;
				bool ro_is_enum        = false; // real enum cycler (carries values/labels)
				bool ro_is_numbox      = false; // numeric num-box (stepper fallback when the slider cannot be built)
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
					// Plain string (or a widget that could not be built): greyed key + value text row.
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
					pr.disabled          = true; // blocks every edit path (click/slider/num-box) via the row handlers
					pr.is_enabled_toggle = is_enabled_row;
					if (ro_is_slider || ro_is_numbox)
					{
						pr.is_slider          = ro_is_slider; // slider drag bar, or ...
						pr.is_stepper         = ro_is_numbox; // ... num-box stepper fallback (shares the revert path)
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

					// A context mismatch shows the scenario note first, then the normal description below it. An
					// author-disabled row shows its disabledDescription (falling back to the normal description) so the
					// author can explain why it is greyed.
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
				// Bounded number: a slider (drag bar) like the audio-volume rows, snapped to step. Fall back to a
				// number-box stepper if the slider cannot be built on this game build.
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
				// Left-aligned key + right-aligned value (two components), like a keybind row.
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

				// Prefer the author's metadata description (resolved to the current language), else fall back to the
				// .cfg comment text.
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

	// One page of a mod's settings: collect what belongs on it, then build a native row for each.
	static void build_mod_settings(MiscSettingsScreen* screen, const std::string& stem, const std::string& section)
	{
		build_panel_rows(screen, stem, section, collect_panel_items(stem, section));
	}

#pragma endregion

#pragma region Panel sync, focus, and navigation

	// Matches the native category-switch transition: the incoming page fades in, with no fade-out crossover. Native
	// UpdateScrollState sets each on-page row's mFadeTarget to 1 and off-page rows to 0, and GUIComponent::Update eases
	// toward it - so on-page rows are left entirely to the native ease. Rows are in m_options/g_rows order.
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

	// Value displays are not in mOptions, so the engine's scroll pass does not lay them out. Mirror each value
	// component onto its key row's current position and fade so the right column tracks scrolling and fade-in/out.
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

	// The component the user is currently on: the mouse-over one (mouse) takes priority, else the selected one
	// (keyboard/controller). These are MenuScreen fields (flat struct view).
	static GUIComponent* active_row_component(MiscSettingsScreen* screen)
	{
		auto* menu = reinterpret_cast<MenuScreen*>(screen);
		return menu->m_mouse_over_component ? menu->m_mouse_over_component : menu->m_selected_component;
	}

	// Finds the PanelRow whose left-column component is `comp`, or nullptr. Valid until the next panel rebuild
	// (deferred to Update), so callers within a single input/update pass may keep it.
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

	// Builds a stable identity for a row so it can be re-found after a rebuild recreates the components.
	// A row's real config section, used to tell same-named keys apart when a `group` override moves them onto one page.
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

	// The freshly built row matching a captured identity, or null if it is gone or is no longer selectable. Used to put
	// the hover and selection back on the equivalent new row after an instant rebuild.
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

	// True while the user is still actively adjusting one of our rows: the entered component (keyboard or controller
	// adjusting a slider/enum), or a mouse drag (a mouse button held over one of our rows). If the mouse-down probe is
	// unavailable, any hover holds instead, so a drag is never interrupted.
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

	// The component whose description was last written to the description box. The box is only updated when the
	// highlighted row changes (not every frame). Reset when the panel rebuilds.
	static GUIComponent* g_last_description_component = nullptr;

	// Shows the highlighted row's author description in the screen's native description box
	// (MiscSettingsScreen::mDescriptionBox @ 0x460). Otherwise the box is cleared.
	static void sync_description_box(MiscSettingsScreen* screen)
	{
		if (!g_show_text || !screen->m_description_box)
		{
			return;
		}
		auto* box = screen->m_description_box;

		GUIComponent* active = active_row_component(screen);

		// Resolve the highlighted row's description (cheap linear scan over the few visible rows).
		const std::string* description = nullptr;
		if (PanelRow* row = find_row(active))
		{
			description = &row->description;
		}
		const bool show = description && !description->empty();

		// Rebuild the text only when the highlighted row changes (ShowText re-lays out the lines).
		if (active != g_last_description_component)
		{
			g_last_description_component = active;

			// Escape markup so paths/brackets in the description render verbatim (see escape_markup), then turn any embedded
			// newline into the box's hard-break escape. GUIComponentTextBox::Parse strips a raw 0x0A but honors the escape "\n"
			// (backslash + n) as a wrap-independent hard break (via ParseEscapeSequence), so a context note stays on its own
			// line above the description while each part still word-wraps.
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

			// ShowText only marks the lines dirty. The layout (and text height, which the box's justification uses to
			// place the text) is otherwise recomputed lazily at draw time, so the first visible frame would render at a
			// stale position and visibly jump. Force the line rebuild now so the first shown frame is already laid out.
			if (show && g_get_lines)
			{
				g_get_lines(box);
			}
		}

		// Re-apply the fade every frame: the native Update runs before this and re-hides. The box on the Mods tab (it
		// does not use mDescriptionBox here), so a one-time set would fade back out.
		box->m_fade_opacity = show ? 1.0f : 0.0f;
		box->m_fade_target  = show ? 1.0f : 0.0f;
	}

	// Last label we wrote to each bottom-prompt button, so SetDisplayName is only called when the label actually
	// changes (avoids re-laying out the text every frame). Cleared when we leave the Mods tab so the native labels take
	// back over and re-entering re-applies ours.
	static std::string g_prompt_confirm_label;
	static std::string g_prompt_cancel_label;

	// Sets a bottom-prompt button's label (GUIComponentButton::SetDisplayName) only when it changes from what we last
	// set. The key glyph is driven by the button's bound control, not the label, so it stays correct (Enter for
	// Confirm, Esc for Cancel) regardless of the text.
	static void set_prompt_label(GUIComponent* button, std::string& cache, const char* text)
	{
		if (!button || !g_set_label || cache == text)
		{
			return;
		}
		cache.assign(text);
		g_set_label(button, text);
	}

	// Retunes the options screen's bottom button prompts for the Mods tab per context, and hides the native Reset prompt
	// where it must not apply. Off the Mods tab it only clears our caches and leaves the native prompts untouched.
	static void sync_prompts(MiscSettingsScreen* screen, bool on_mods_tab)
	{
		if (!on_mods_tab)
		{
			g_prompt_confirm_label.clear();
			g_prompt_cancel_label.clear();
			return;
		}

		auto* menu = reinterpret_cast<MenuScreen*>(screen);

		// The native prompt strings embed a glyph token that the text box expands to the device- appropriate key icon:.
		// Labels are upper-case to match the game Cancel (Esc): "CANCEL" while editing a field "BACK" inside a mod's settings
		// (Esc returns to the mod list, see the ExitScreen hook) "EXIT" at the mod list (closes the options screen).
		const char* cancel = g_editing ? "{CN} CANCEL" : (g_view == View::mod_settings ? "{CN} BACK" : "{CN} EXIT");
		set_prompt_label(menu->m_cancel_button, g_prompt_cancel_label, cancel);

		// Confirm (Enter): "SUBMIT" while editing otherwise a verb matching the highlighted row.
		std::string confirm;
		if (g_editing)
		{
			confirm = "{SL} SUBMIT";
		}
		else if (PanelRow* row = find_row(active_row_component(screen)))
		{
			if (row->disabled)
			{
				// A greyed, non-interactable row (e.g. an opted-out mod) has no confirm action, so show no confirm
				// prompt for it.
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
						confirm = "{SL} SET"; // matches the base-game volume sliders' prompt
					}
					else if (row->is_stepper)
					{
						confirm = "{SL} SELECT";
					}
					else
					{
						confirm = "{SL} EDIT"; // freetext value
					}
					break;
				case RowKind::action: confirm = "{SL} SELECT"; break;
				}
			}
		}

		// Drive the Confirm prompt's visibility ourselves: native only fades it in (OnOptionMouseOver) for its OWN option
		// rows, which never fires for our custom rows Show it with its glyph whenever we have a hint, hide it when we don't
		// mFadeOpacity is the field the draw gate reads native Update rewrites mHidden each frame, so both are set here
		// (after the original Update).
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

		// Reset prompt: shown only inside a single mod's settings (resets that mod) and not while editing. It is hidden
		// in the mod list/overview so users cannot reset every mod's config by accident (the. RestoreDefaults hook also
		// swallows the shortcut there).
		if (screen->m_defaults_button)
		{
			const bool show_reset               = (g_view == View::mod_settings) && !g_editing;
			screen->m_defaults_button->m_hidden = !show_reset;
		}
	}

	// Focuses the first selectable row so the controller/keyboard cursor lands on it, as a native category does. The
	// engine's DoShowCategory only does this when the option list is already populated, and our rows are appended
	// afterwards - so without this the screen stays in tab-navigation mode and the stick never reaches the rows until
	// the tab is selected a second time.
	static void focus_row(MiscSettingsScreen* screen, GUIComponent* component)
	{
		if (!g_teleport_cursor || (g_use_mouse && *g_use_mouse) || !component)
		{
			return;
		}
		g_teleport_cursor(screen, component); // drop the cursor on the row, next Update focuses it
		screen->m_category_focused = false;   // hand navigation from the tab bar to the option rows
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

	// After a native page scroll the engine selects the new page's edge row directly, without consulting
	// mFreeFormSelectable, so redirect it to the first selectable row instead. Falls back to the native edge selection
	// when every row on the page is disabled. Mouse mode is untouched, since the pointer drives hover itself.
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
		const std::size_t page_end = std::min(page_start + rows_per_page, g_rows.size()); // exclusive

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

		// No eligible row on this page (all disabled): keep the native edge selection.
		auto* menu = reinterpret_cast<MenuScreen*>(screen);
		if (!target || menu->m_mouse_over_component == target)
		{
			return;
		}

		g_set_mouse_over(screen, target);  // remove the highlight from the edge row and place it on the eligible one
		g_teleport_cursor(screen, target); // the free-form cursor follows so the next press moves from here
		screen->m_category_focused = false;
	}

	// The row a pending back-navigation should re-focus: the mod_entry row of the mod that was open (focus_stem set),
	// or the group row that drills into the section that was open (focus_section set). Exactly one of the two fields is
	// set per restore. Returns nullptr if that row is not in the freshly built view (e.g. it was removed since).
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

	// Queues a one-level back navigation inside a mod's settings: a nested group returns to its parent section, and the
	// root returns to the mod list. Applied next Update via apply_nav.
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

	// True if a remappable control (e.g. Back/Cancel = controller B + keyboard Esc, or Select = controller A + Enter)
	// was pressed this frame Bit 0x4 of the control's state is "was pressed" (edge, not held).
	static bool control_pressed(void* input, const void* control)
	{
		if (!input || !g_input_get_state || !control)
		{
			return false;
		}
		return (g_input_get_state(input, control) & 0x4u) != 0;
	}

	// Holds the clicked row as moused-over and selected, and re-applies our prompt and description, for a few frames
	// after a click-triggered rebuild. The native hover pass runs later in HandleInput and over freshly laid-out rows can
	// transiently resolve the stationary cursor to a neighbour, which would blink the highlight and prompt.
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

		// Clear the prompt caches and the last-description marker so this frame's sync re-applies our label and text,
		// overriding a native clear on the rebuild frame.
		g_prompt_confirm_label.clear();
		g_prompt_cancel_label.clear();
		g_last_description_component = nullptr;
	}

	// Calls a no-argument GUIComponent virtual (by byte offset into the vtable) on a component. Used to invoke the
	// engine's own OnMouseOff/OnFocusOff so their full revert (text-colour flag plus the fill-texture swap) runs.
	static void call_component_vfn(GUIComponent* comp, std::size_t vtable_byte_offset)
	{
		char* vtable = *reinterpret_cast<char**>(comp);
		void* fn     = *reinterpret_cast<void**>(vtable + vtable_byte_offset);
		reinterpret_cast<void (*)(void*)>(fn)(comp);
	}

	// Moves a row (and its owned child components) to an absolute location via the engine's own SetLocation (GUIComponent
	// vtable slot +0x180) - the same call UpdateScrollState uses to lay rows on the grid. Going through SetLocation
	// (rather than writing m_location_y directly) keeps a row's children - a slider's bar/label, a button's label - in
	// step, avoiding the per-frame drift a raw location write causes.
	static void set_component_location(GUIComponent* comp, float x, float y)
	{
		char* vtable = *reinterpret_cast<char**>(comp);
		auto fn      = *reinterpret_cast<void (**)(void*, std::uint64_t)>(vtable + vtable_set_location_offset);
		std::uint32_t xb, yb;
		std::memcpy(&xb, &x, sizeof xb);
		std::memcpy(&yb, &y, sizeof yb);
		fn(comp, (static_cast<std::uint64_t>(yb) << 32) | xb);
	}

	// Reverts a stale highlight left on the wrong slider or num-box row. The two differ in which handler sets the look: a
	// slider's is set by OnMouseOver and reverted by OnMouseOff, a num-box's by OnSelected and reverted by OnUnselected
	// (its OnMouseOff is an inherited no-op). The num-box look also fires under keyboard/controller nav, so its revert is
	// gated to mouse mode to avoid clearing a genuine gamepad selection. Both carry a focus look reverted by OnFocusOff.
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
				// Moused-over look lives on the left label textbox. Revert it unless this row is the live mouse-over -
				// but a disabled (greyed, still-selectable) row is reverted even while hovered, so its bar/label never
				// light up: it must read as greyed no matter the cursor.
				if (row.disabled || row.component != menu->m_mouse_over_component)
				{
					if (auto* label = *reinterpret_cast<char**>(s + slider_label_offset); label && *reinterpret_cast<bool*>(label + textbox_use_selected_color_off))
					{
						call_component_vfn(row.component, vtable_on_mouse_off_offset);
					}
				}

				// Focused look lives on mFocused/the value textbox. Revert it unless this row is the focused option (a
				// disabled row is never focused, so it is always reverted here).
				if ((row.disabled || row.component != screen->m_component_focused) && *reinterpret_cast<bool*>(s + slider_focused_offset))
				{
					call_component_vfn(row.component, vtable_on_focus_off_offset);
				}
			}
			else if ((row.is_enum || row.is_stepper) && (mouse_mode || row.disabled))
			{
				// Num-box selected look (black box + green label) set by OnSelected, on the label textbox mUseSelected
				// flag. Revert via OnUnselected (not OnMouseOff, a no-op here) unless it is the live mouse-over - a
				// disabled (greyed, still-selectable) row is reverted even while hovered so it stays greyed.
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

	// Keeps a greyed-but-still-selectable widget row's label (and value) text greyed. We re-apply the grey through the
	// text box's own SetTextColor each frame (the same call the engine's UpdateButtonStates uses to grey a still-hoverable
	// option), which writes that cached mTextColor directly. The selected-colour def is greyed too (grey_text_box) so a
	// hover that briefly sets the selected flag stays grey.
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

	// Makes the spatial nav skip disabled rows so UP/DOWN jumps to the next interactable one, while leaving mouse hover
	// alone so a greyed row can still be rested on to read its description. mFreeFormSelectable is the only gate
	// SearchInDirection checks before IsSelectable, and one UpdateMouseOver never reads. Reapplied after every build,
	// since the row objects are recreated each time.
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
		// A rebuild frees and recreates the row components, so the cached highlighted-row pointer is stale force the
		// description box to refresh next frame.
		g_last_description_component = nullptr;

		// Preserve the current scroll offset across an in-place refresh (same view/mod, e.g. after committing a setting
		// edit or toggling "enabled") so confirming a setting on a lower page does not jump back to the top. A real
		// view change (instant == false) starts at the top.
		const std::uint32_t prev_start = screen->m_page_start_index;

		// On an instant (same-view) rebuild the highlighted row is freed and recreated, so remember it to put the
		// keyboard/controller cursor back afterwards (mouse uses hover, so this is gated to non-mouse mode). Only
		// setting/action rows (those with a key) are tracked.
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

		// Remove any rows from a previous view/visit before building the new set.
		destroy_rows(screen);

		// Resolve the blank graphic lazily: the string-intern table is not ready at hook registration time, so "Blank"
		// only hashes correctly once the game is running.
		if (!g_blank_graphic && g_hash_lookup)
		{
			HashGuid res{};
			g_hash_lookup(&res, "Blank", 5);
			g_blank_graphic = res.m_id;
		}

		// Recomputed during the build: true if any row in the new view has a dynamic (Lua-function) field, so a bool
		// toggle should trigger an in-place rebuild to re-evaluate it live.
		g_view_has_dynamic = false;

		// This rebuild supersedes any pending numeric-change refresh, so cancel its debounce.
		g_dynamic_refresh_settle = 0.0f;

		if (g_view == View::mod_settings && !g_view_stem.empty())
		{
			build_mod_settings(screen, g_view_stem, g_view_section.empty() ? root_section : g_view_section);
		}
		else
		{
			build_mod_list(screen);
		}

		// Let the engine position, paginate and drive the scrollbar/arrows for the rows. If a restore is pending from the
		// back-nav, restore that view's saved scroll offset so the user lands where they were.
		const bool restoring = !instant && g_has_pending_restore;

		std::uint32_t start = 0;
		if (instant || restoring)
		{
			// Restore the exact offset the view had. Only clamp when it now points past the last row (the row count
			// shrank, e.g. a row became hidden), and then to the first index of the last page - so a partial final page
			// (fewer than rows_per_page rows) keeps its own offset instead of being pulled up into a full page of rows.
			const std::uint32_t row_count       = static_cast<std::uint32_t>(g_rows.size());
			const std::uint32_t last_page_start = row_count > 0 ? ((row_count - 1) / rows_per_page) * rows_per_page : 0;
			const std::uint32_t desired         = instant ? prev_start : g_pending_restore.scroll_index;
			start                               = desired > last_page_start ? last_page_start : desired;
		}
		screen->m_page_start_index = start;
		screen->m_options_per_page = rows_per_page;
		if (g_update_scroll)
		{
			g_update_scroll(screen); // sets each row's mFadeTarget: 1 on-page, 0 off-page
		}

		if (instant)
		{
			// In-place refresh (e.g. toggling the mod's "enabled" switch, which only changes greying): snap each row
			// straight to its final visibility so the panel does not flash a fade.
			for (const auto& row : g_rows)
			{
				if (row.component)
				{
					row.component->m_fade_opacity = row.component->m_fade_target;
				}
			}
		}

		// A view change leaves the freshly built rows at mFadeOpacity 0 (finalize_row). The native ease
		// (GUIComponent::Update) then fades the on-page rows in toward mFadeTarget == 1, matching the game's own
		// category-switch transition.
		sync_value_columns();

		// Take the disabled/greyed rows out of the keyboard/controller nav so the cursor only lands on interactable
		// ones (mouse hover is unaffected).
		apply_row_freeform_selectability();

		// On a real view change (tab entry, drilling in, going back), drop the cursor on the first row so it highlights
		// immediately like a native category. Skipped on in-place refreshes so committing an edit or toggling "enabled" does
		// not yank focus back to the top.
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
			// Put the keyboard/controller cursor back on the equivalent new row so an instant rebuild (a toggle, an
			// action, or the deferred numeric refresh) does not drop it. No-op for mouse.
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

		// Arm a short re-assert window after a click-triggered instant rebuild (a toggle or an action). The Update hook
		// re-asserts the clicked row over these frames (see reassert_keep_active_row).
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

	// Applies a queued navigation (mod list <-> a mod's settings) by rebuilding the panel. A rebuild that stays on the
	// same view/mod (e.g. after toggling "enabled") is applied instantly to avoid a fade flash.
	static void apply_nav(MiscSettingsScreen* screen)
	{
		// A rebuild that stays on the same view/mod/section (a setting edit, an "enabled" toggle, or a Reset) is
		// applied instantly, which preserves the current scroll page instead of snapping back to the top.
		const bool instant = (g_pending_view == g_view) && (g_pending_stem == g_view_stem) && (g_pending_section == g_view_section);

		// Maintain the restore stack. A same-view rebuild (instant:.
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
				r.focus_stem = g_pending_stem; // the mod being opened
			}
			else
			{
				r.focus_section = g_pending_section; // the child section being opened (a group row's target)
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

	// The serialized default of a config entry, read from the entry itself via the public write_description (whose last
	// output line is "#. The serialized form uses the same converter as get_serialized_value, so it round-trips through
	// set_serialized_value.
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

	// Restores config entries to their defaults, but only those whose MENU path lies within the current view, so a Reset
	// inside a group leaves siblings and parents untouched. At the mod root that is every described entry. Defaults come
	// from the config.lua value captured by rom.mod_settings.load when available, else the entry's own stored default.
	static bool reset_settings_to_defaults()
	{
		bool any_changed                            = false;
		const std::vector<menu_group> author_groups = mod_menu_groups(g_view_stem);
		toml_v2::config_file* mod_cfg = nullptr; // any config file of this mod, for virtual-row path resolution.
		for (auto* cfg : toml_v2::config_file::g_config_files)
		{
			if (!cfg || cfg->m_config_file_stem_as_str.empty() || cfg->m_config_file_stem_as_str != g_view_stem)
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

				// Reset only the settings the menu actually shows: described keys (from our loader or a Chalk
				// plain-string description) plus the always-shown master "enabled" toggle. Hidden (undescribed) keys
				// are the mod's internal state, so a menu Reset leaves them untouched.
				const bool is_enabled_toggle = def.m_section == root_section && e->type() == typeid(bool) && is_enabled_key(def.m_key);
				if (!is_enabled_toggle && !setting_is_described(guid, def.m_section, def.m_key) && !entry_has_description(e))
				{
					continue;
				}

				// Skip entries outside the current menu group. The `group` override is a static field, so the cheap
				// stored metadata gives the placement.
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
					// Not bound via rom.mod_settings.load (e.g. a Chalk mod): recover the default from the config entry
					// itself.
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

		// Interactive virtual rows are not config entries, so restore any in scope that declare a `default` through
		// their set() callback here (read-only rows and rows without a default are left untouched).
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

	// Handles a Reset activation on the Mods tab: restores the in-scope settings to their config.lua defaults, then (in a
	// mod's settings view, where the changed values are on screen) queues an in-place rebuild so the widgets show the
	// restored values. Safe to call from input/click context because the rebuild is deferred to the Update hook.
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

	// True when the game's current display language uses a CJK font (zh-CN, zh-TW, ja, ko). Those fonts have no glyph for
	// the non-breaking space U+00A0 and draw a visible '*' instead, so the restart message uses regular spaces and a
	// U+3000 blank for them.
	static bool current_language_is_cjk()
	{
		const std::string code = current_language_code();
		return code.rfind("zh", 0) == 0 || code.rfind("ja", 0) == 0 || code.rfind("ko", 0) == 0;
	}

	// Builds a locale-aware popup body: an intro line, a blank line, one line per list entry, a blank line, then an outro
	// line (plus a sacrificial trailing blank). Both blank characters survive ShowText's ASCII-whitespace-line trim.
	// Shared by the restart-required and dependency-block dialogs.
	static std::string build_list_message(const std::string& intro, const std::vector<std::string>& lines, const std::string& outro)
	{
		const bool cjk          = current_language_is_cjk();
		const std::string blank = cjk ? "\xE3\x80\x80" : "\xC2\xA0"; // U+3000 (CJK) or U+00A0 (other)

		const auto spaced = [cjk](const std::string& s) -> std::string
		{
			if (cjk)
			{
				return s; // regular spaces render in the CJK font, the entries fit without non-breaking
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

	// Builds the restart-popup body text from the changes collected this session.
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

	// Builds an empty EASTL SSO string (24-byte layout) in `buf` (>=24 bytes). Passed to the dialog ctor (message) and
	// AddScreen (name). The real message is applied afterwards via ShowText. Layout: bytes[0..]=chars,
	// byte[23]=remaining-capacity marker (23 - length).
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

	// Persists the game's native Options settings (language, audio volumes, resolution/window/graphics, and all
	// gameplay/interface/accessibility toggles) to disk. The engine normally does this only when the options screen
	// finishes closing (MiscSettingsScreen::OnExit -> ProfileManager::SaveProfile), which never runs when we force a
	// restart. SaveProfile's synchronous path (async=false, no save spinner) so the files are written before we exit.
	static void flush_native_settings()
	{
		if (g_save_profile && g_active_profile)
		{
			g_save_profile(g_active_profile, false, false);
		}
	}

	// Shows the native single-button message box, modal over the options screen. When confirm_closes_game is set the
	// confirm button is captured so the OnClicked hook closes the game on press (a forced restart, which must not be
	// cancellable); otherwise the button keeps its native dismiss behaviour.
	static bool show_message_dialog(void* screen_manager, const char* title, const std::string& message, bool confirm_closes_game)
	{
		if (screen_manager && g_message_dialog_ctor && g_add_screen)
		{
			// The ScreenManager takes ownership and frees this with ucrtbase's _aligned_free, so it must come from the
			// game's heap (see game_alloc).
			void* dialog = game_alloc(message_dialog_size);
			if (dialog)
			{
				std::memset(dialog, 0, message_dialog_size);

				// The ctor builds every component (single button + text) and loads GUI/MessageDialog.sjson. Pass an
				// empty message. The real (multi-line) text is applied below via ShowText so it need not be an eastl
				// heap string.
				char empty_message[24];
				make_eastl_sso(empty_message, "");
				g_message_dialog_ctor(dialog, screen_manager, empty_message);

				auto* bytes = reinterpret_cast<char*>(dialog);

				// Ensure the dialog is visible and modal over the options screen.
				bytes[screen_removed_offset]     = 0;
				bytes[screen_visible_offset]     = 1;
				bytes[screen_block_input_offset] = 1;

				// Set the title + body (raw text. The body carries the list of settings/mods).
				if (g_show_text)
				{
					if (auto* title_box = *reinterpret_cast<void**>(bytes + dialog_title_offset))
					{
						g_show_text(title_box, title);
					}
					if (auto* message_box = *reinterpret_cast<GUIComponent**>(bytes + dialog_message_offset))
					{
						// Shrink the body font: the sjson template renders at size 26 scale the live font handle's size
						// ratios down before ShowText lays out the lines (the def's mFontSize is ignored once the
						// template is loaded).
						char* handle = reinterpret_cast<char*>(message_box) + textbox_font_handle_offset;
						*reinterpret_cast<float*>(handle + font_handle_size_ratio_offset) *= restart_message_font_scale;
						*reinterpret_cast<float*>(handle + font_handle_eng_size_ratio_offset) *= restart_message_font_scale;

						// Escape markup so a path value (e.g. hadesGameFolder) with '\' or brackets in the listed lines
						// renders verbatim (see escape_markup).
						const std::string shown = escape_markup(message);
						g_show_text(message_box, shown.c_str());
					}
				}

				// Capture the confirm button only when it should close the game. Otherwise the native confirm behaviour
				// dismisses the dialog. Remember the dialog so the OnClicked hook can confirm the clicked button still
				// belongs to it before terminating.
				if (confirm_closes_game)
				{
					g_restart_confirm_button = *reinterpret_cast<GUIComponent**>(bytes + dialog_confirm_button_offset);
					g_restart_dialog         = dialog;
				}

				// Add at the END of the screen list so it draws on top of the options menu.
				char empty_name[24];
				make_eastl_sso(empty_name, "");
				g_add_screen(screen_manager, dialog, true, empty_name);
				return true;
			}
		}

		return false;
	}

	// The "restart required" prompt: its only button closes the game (a restart-required change must not be
	// cancellable, since cancelling would have to undo the change).
	static bool show_restart_dialog(void* screen_manager, const std::string& message)
	{
		return show_message_dialog(screen_manager, "Restart Required", message, /*confirm_closes_game*/ true);
	}

	// The "can't disable this mod" prompt: purely informational, so its button just dismisses the dialog and returns
	// the player to the options screen with the mod left enabled.
	static bool show_dependency_dialog(void* screen_manager, const std::string& message)
	{
		return show_message_dialog(screen_manager, "Cannot Disable Mod", message, /*confirm_closes_game*/ false);
	}

	// True if the mod with config-file stem/guid `guid` is currently enabled: the value of its master "enabled"
	// root-section toggle, or true when it has no such toggle (a mod with no enable switch is always active). Reads the
	// live config value, so it reflects any change made this menu session.
	static bool mod_is_enabled(const std::string& guid)
	{
		for (auto* cfg : toml_v2::config_file::g_config_files)
		{
			if (!cfg || cfg->m_config_file_stem_as_str != guid)
			{
				continue;
			}
			for (auto& [key, entry] : cfg->m_entries)
			{
				if (entry && key.m_section == root_section && entry->type() == typeid(bool) && is_enabled_key(key.m_key))
				{
					return entry->get_value_base<bool>();
				}
			}
		}
		return true;
	}

	// Display names of the currently-enabled loaded mods that declare `stem` as a dependency (via their Thunderstore
	// manifest, which lists dependency guids in dependencies_no_version_number). A dependent that is itself disabled is
	// skipped -.
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

	// Body text for the dependency-block popup: lists the enabled mods depending on the one the player tried to disable,
	// and tells them how to proceed. The blocked mod is identified by the dialog title and the toggle the player just
	// clicked, so it is not repeated here.
	static std::string build_dependency_message(const std::vector<std::string>& dependents)
	{
		return build_list_message("These enabled mods depend on this one:", dependents, "Disable them first to disable this mod.");
	}

#pragma endregion

#pragma region Engine hooks

	static void* hook_MiscSettingsScreen_ctor(void* self, void* screen_manager, void* opened_from, void* profile_name)
	{
		// Reset state BEFORE running the original ctor: the original ctor immediately shows the last-viewed category,
		// and if that is the Mods tab it builds our panel via DoShowCategory. Clearing g_rows after the original would
		// wipe those fresh rows.
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

		// Record whether the screen was opened during gameplay (a save loaded) or from the main menu, so context-restricted
		// rows can be greyed. Must be set before the original ctor runs, which shows the last-viewed category and may build
		// our panel via DoShowCategory.
		g_opened_in_game      = opener_indicates_in_game(opened_from);
		g_in_hub              = game_is_in_hub();
		g_options_screen_open = true;

		// The engine constructor returns `this` forward it unchanged
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

		// Leaving the Mods tab for another category: tear our rows down FIRST, before the native category switch runs. They
		// would then linger in mComponents on the other category - re-localized by a language change and walked by the native
		// layout - which can corrupt unrelated widgets (e.g. a category button's label). Doing our own teardown here keeps
		// mComponents clean for the native code re-entering the tab rebuilds.
		if (!is_mods_tab && !g_rows.empty())
		{
			destroy_rows(screen);
			exit_edit_mode();
		}

		auto* result = big::g_hooking->get_original<hook_MiscSettingsScreen_DoShowCategory>()(self, category_button, category_flag);

		show_mods_tab(screen);

		if (is_mods_tab)
		{
			// Entering the tab always starts at the mod list drill-down happens in-place via the Update hook, not by
			// re-entering the category.
			g_view = View::mod_list;
			g_view_stem.clear();
			g_view_section.clear();
			g_nav_pending = false;
			g_nav_stack.clear(); // a fresh tab entry starts at the top of the mod list.
			g_has_pending_restore = false;
			exit_edit_mode();
			build_panel(screen);
		}

		return result;
	}

	// Value-change hook for our native number-box rows. GUIComponentNumBox::SetNumberValue is called (with notify=true) on
	// every user step - left/right, arrow click, keyboard or controller. This fires for native settings num-boxes too,
	// hence the `find_row` filter.
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

		// Enum cycler: The box tracks the option index persist the matching serialized value and replace the raw index
		// the original just painted with the option's label.
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

	// Persists a user drag or adjust on our slider rows. Fires for the native audio sliders too, hence the find_row
	// filter. mFraction is deliberately left continuous rather than snapped: the native adjust accumulates a small
	// per-frame delta into it, so re-snapping each frame would discard any delta below half a step and a partial stick
	// deflection would never move the slider.
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

		// Continuous post-clamp fraction the original just wrote, mapped to the value and snapped to step.
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

		// Restore the real value in place of the percentage the original wrote (applying the setting's own
		// percentage-display options).
		set_slider_value_text(reinterpret_cast<GUIComponent*>(self),
		                      format_setting_display(v, row->show_as_percentage, row->is_percentage, step_v).c_str());
	}

	// Moves a slider row one grid step (dir -1 or +1) from its current snapped value, clamped to [min, max], and writes
	// the exact grid fraction through SetFraction with notify so the SetFraction hook stores the value and repaints the
	// value text. The stored value is already on the grid, so rounding the current index is just a safety net.
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

	// Discrete keyboard/controller stepping for our slider rows, and a disabled-row guard. The native
	// The native HandleInput slides mFraction continuously behind a dead-zone, so a small tap can land back on the same
	// snapped value. Under keyboard/controller we bypass it and move exactly one step per left/right press edge, gated
	// on the slider's own mFocused so only the entered slider reacts.
	static bool hook_GUIComponentSlider_HandleInput(void* self, void* input, float dt)
	{
		PanelRow* row = self ? find_row(reinterpret_cast<GUIComponent*>(self)) : nullptr;
		if (row && row->is_slider)
		{
			if (row->disabled)
			{
				return false; // greyed slider: swallow so the native mouse-drag/slide never adjusts it
			}
			if ((row->entry || row->is_virtual_input) && !(g_use_mouse && *g_use_mouse) && *reinterpret_cast<bool*>(reinterpret_cast<char*>(self) + slider_focused_offset))
			{
				if (g_input_was_right_pressed(input))
				{
					step_slider_row(self, row, 1);
				}
				else if (g_input_was_left_pressed(input))
				{
					step_slider_row(self, row, -1);
				}
				return true; // own the focused slider's input so the native continuous slide never runs
			}
		}
		return big::g_hooking->get_original<hook_GUIComponentSlider_HandleInput>()(self, input, dt);
	}

	// Button-click hook GUIComponentButton overrides. GUIComponent::OnClicked (vtable slot +0x100, the engine's
	// terminal-click), so this is where our button rows' clicks land.
	static bool hook_GUIComponentButton_OnClicked(GUIComponent* self, std::uint64_t location)
	{
		// Clicking the restart message box's button closes the game (forced restart). Re-validate the button's owner is
		// still the restart dialog so a rebuilt row that happened to reuse the freed button's address (if the dialog
		// were ever dismissed without confirming) cannot trigger it.
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

		// A boolean toggle flips in our own code below rather than through the native toggle handler that plays the
		// click sound, so stage the matching toggle cue as the press sound before the base OnClicked runs (it plays
		// mPressSound). Predict the value the click produces (the flip of the current one) to pick the on/off cue.
		if (matched && !matched_row.disabled && matched_row.kind == RowKind::setting && matched_row.entry
		    && matched_row.entry->type() == typeid(bool))
		{
			stage_toggle_press_sound(self, !matched_row.entry->get_value_base<bool>());
		}
		else if (matched && !matched_row.disabled && matched_row.kind == RowKind::setting && matched_row.is_virtual_input
		         && matched_row.is_toggle)
		{
			// Predict the flipped state for the press cue. get() drives the flip, but may be nil (value not set yet),
			// so fall back to the row's last-drawn state - matching the flip below.
			const auto cur = get_virtual_value(matched_row.stem, row_io_section(&matched_row), matched_row.setting_key);
			const bool cur_on = cur.type == virtual_value::kind::boolean ? cur.as_bool : matched_row.toggle_value;
			stage_toggle_press_sound(self, !cur_on);
		}

		// A matched but disabled row (a greyed action button or a context-restricted setting that stays selectable so its
		// note still shows on hover) must not react to a click. The base GUIComponent::OnClicked plays mPressSound and swaps
		// the button's pressed graphic even though the row has no usable activate, so calling it would sound and visually
		// "press" a control the user cannot use.
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

				// Boolean settings toggle in place. Other types open a freetext editor. Number-box rows are
				// GUIComponentNumBox, so their clicks never reach this hook.
				if (entry && entry->type() == typeid(bool))
				{
					const bool new_value = !entry->get_value_base<bool>();

					// Block disabling a mod that other enabled mods still depend on: turning the mod's master "enabled"
					// switch off would break them. Leave the toggle on and show an informational popup listing the
					// dependents (its button just dismisses the popup).
					if (matched_row.is_enabled_toggle && !new_value)
					{
						const std::vector<std::string> dependents = active_dependents_of(matched_row.stem);
						if (!dependents.empty())
						{
							void* owner = *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + sgg::gui_component_button_owner_offset);
							void* screen_manager = owner ? *reinterpret_cast<void**>(reinterpret_cast<char*>(owner) + screen_manager_offset) : nullptr;
							show_dependency_dialog(screen_manager, build_dependency_message(dependents));
							break; // do not disable, the toggle stays on
						}
					}

					// Capture the session baseline before the first write so a later revert to it (toggling off then on
					// again) is recognised as "no net change".
					capture_restart_baseline(entry);

					entry->set_value_base<bool>(new_value);
					set_toggle_graphic(self, new_value);

					// If the author declared this setting restart-required, flag/clear the restart and record the
					// change for the popup.
					note_change_if_restart_required(entry, new_value ? "on" : "off");

					// Toggling "enabled" changes greying. Toggling any bool in a dynamic view may change
					// disabled/hidden/range. Rebuild in place next Update, preserving scroll.
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
					// Interactive virtual boolean: flip through Lua set() and repaint. After set(), get() returns the
					// stored value for later clicks.
					const auto cur = get_virtual_value(matched_row.stem, row_io_section(&matched_row), matched_row.setting_key);
					const bool cur_on = cur.type == virtual_value::kind::boolean ? cur.as_bool : matched_row.toggle_value;
					const bool new_value = !cur_on;
					commit_row_bool(&matched_row, new_value);
					set_toggle_graphic(self, new_value);
				}
				break;
			}
			case RowKind::action:

				// Run the author's Lua callback, then rebuild the current view: the callback may have changed config
				// values (e.g. a "Reset" button) or dynamic ranges, so the rows need to re-read them. Mirrors the
				// master-toggle rebuild path.
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

	// Restores vertical breathing room around action-button rows, whose taller Button_Secondary box would otherwise
	// crowd neighbouring setting rows on the uniform grid. Runs after the native UpdateScrollState has laid out the
	// page. The shift goes through the engine's own SetLocation so each row's child components follow - a raw
	// m_location_y write leaves them behind, which is what caused the earlier slider-bar drift.
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

	// Points the native scroll arrows at the keyboard/controller nav so it can page. The spatial search
	// (SearchInDirection) walks a ray from the selected row in the pressed direction and picks the nearest selectable
	// component whose eval point (location + mFreeFormSelectOffset) is close to the ray. Off the last/first page the arrow
	// is hidden and unselectable, so this is inert there.
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

		// Down arrow aims one row below the last visible row. Up arrow one row above the first visible row.
		aim(screen->m_down_arrow, g_rows[last].component, row_pitch);
		aim(screen->m_up_arrow, g_rows[first].component, -row_pitch);
	}

	// Detour on the native scroll pass. We hook it to give the action-button rows their vertical breathing room
	// (apply_button_spacing) and to re-aim the scroll arrows' keyboard-nav eval points at the new page edges after each
	// layout (see enable_arrow_keyboard_paging), inside MiscSettingsScreen::Update before the row hit-test.
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

	// Per-frame screen update: RCX=this, XMM1=dt (float), R8=input. We apply any queued navigation here because the
	// click/input iteration has fully unwound by now, so tearing down and rebuilding the component vectors is safe. We
	// rebuild before the original runs so this frame lays out and hover-resolves the new rows.
	static void* hook_MiscSettingsScreen_Update(void* self, float dt, void* input)
	{
		auto* screen = static_cast<MiscSettingsScreen*>(self);
		const bool on_mods_tab = screen->m_current_category_button == reinterpret_cast<GUIComponent*>(screen->m_editor_options_button);

		// Freetext editing: refresh the edited row's live label Confirm/cancel is handled in the HandleInput hook so
		// the submitting key/click is swallowed on the same frame.
		if (g_editing)
		{
			if (on_mods_tab)
			{
				update_edit_label();
			}
			else
			{
				exit_edit_mode(); // safety: never stay in edit mode off the Mods tab
			}
		}

		// An edit in a view with dynamic (function) rows re-evaluates them, which frees and recreates every row. That is
		// deferred twice over: a debounce absorbs the per-frame slider hook and coalesces bursts (each commit re-arms
		// it), and the rebuild is HELD while the user is still adjusting a row, since otherwise it would free the
		// focused slider mid-adjust or interrupt a drag. build_panel clears the timer so its own rebuild cancels any
		// pending one, and the !g_nav_pending guard folds this into a rebuild already queued by an instant path.
		if (g_dynamic_refresh_settle > 0.0f)
		{
			if (!on_mods_tab)
			{
				g_dynamic_refresh_settle = 0.0f;
			}
			else if (interacting_with_row(screen, input))
			{
				g_dynamic_refresh_settle = dynamic_refresh_settle_seconds; // hold until they leave the row
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

						// Pin the edited row so the rebuild keeps focus on it. Keyboard/controller focus is restored by
						// build_panel's cursor tracking.
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
			// Only act while this screen is actually showing the Mods tab.
			if (on_mods_tab)
			{
				// Stepping from a mod's settings back to the mod overview is the "done configuring this mod" point: if a
				// restart-required setting changed this session, show the restart prompt now (it forces the restart) and stay on
				// the current view under it, rather than returning to the overview.
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

		// For a few frames after a click-triggered rebuild, pin the clicked row as hovered/selected and re-apply our
		// prompt/description over the native hover pass, which settles over the new layout a frame later and would otherwise
		// blink the prompt, description or highlight onto a neighbouring row. Runs before the original Update (which reads
		// mMouseOverComponent for the description) so this frame is already correct.
		if (g_keep_active_frames > 0 && on_mods_tab)
		{
			reassert_keep_active_row(screen);
			if (--g_keep_active_frames == 0)
			{
				g_keep_active_row.valid = false;
			}
		}

		void* result = big::g_hooking->get_original<hook_MiscSettingsScreen_Update>()(self, dt, input);

		// The original just laid out the key rows for this frame mirror the value columns onto them so the right column
		// tracks scrolling and fade, and show the highlighted row's description in the native description box.
		if (on_mods_tab)
		{
			sync_scroll_fade(screen);
			sync_value_columns();
			sync_description_box(screen);
		}

		// Retune the bottom prompt buttons per context (off the Mods tab this only clears our caches and leaves the
		// native prompts alone).
		sync_prompts(screen, on_mods_tab);

		// Revert any slider/num-box highlight left stranded on the wrong row by a rebuild or the hover re-assert, and
		// re-assert the greyed-label colour flag on disabled-but-selectable widget rows (the widgets clear it each
		// frame from mIsUseable).
		if (on_mods_tab)
		{
			clear_stale_widget_highlight(screen);
			keep_disabled_labels_grey();
		}

		return result;
	}

	// While a freetext setting is being edited, read Enter and Escape from the game's own per-frame input, commit or
	// cancel here, then swallow the screen's input handling so menu navigation and Escape-to-close do not react.
	// Committing here rather than in Update matters: returning true this frame also swallows a submitting mouse click,
	// so it cannot activate the row it lands on.
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

			// Select enters a slider/enum row (so the stick adjusts it) toggles and buttons are left to the native
			// component pass.
			if (g_component_focused && control_pressed(input, g_controls_select))
			{
				PanelRow* row = find_row(menu->m_mouse_over_component);
				if (row && !row->disabled && (row->is_slider || row->is_enum))
				{
					g_component_focused(screen, menu->m_mouse_over_component);
					return true; // consume the enter press
				}
			}

			// Back/Cancel inside a mod's settings steps back one level instead of returning to the tab bar. In the mod
			// list it is left to the native handler. The restart prompt is shown by apply_nav.
			if (g_view == View::mod_settings && !g_nav_pending && control_pressed(input, g_controls_cancel))
			{
				request_back_nav();
				return true;
			}
		}

		// The native handler runs the keyboard/controller nav, including the on-screen scroll arrow's auto-activate at a page
		// edge, which pages via ScrollDown/ScrollUp and selects the new page's edge row. Capture the page index across the
		// call so we can correct that landing when it falls on a disabled row (see redirect_page_landing).
		const bool track_paging         = on_mods_tab && !(g_use_mouse && *g_use_mouse);
		const std::uint32_t page_before = screen->m_page_start_index;

		auto result = big::g_hooking->get_original<hook_MiscSettingsScreen_HandleInput>()(self, input, x);

		if (track_paging && screen->m_page_start_index != page_before)
		{
			redirect_page_landing(screen, screen->m_page_start_index > page_before);
		}

		return result;
	}

	// Close funnel: every way the user dismisses the screen converges here, before any fade/teardown and while
	// mScreenManager is valid. If a restart is required, show the message box and veto the close - the box is modal over
	// the still-open screen and its button closes the game, since a restart-required change cannot be cancelled.
	static void hook_MiscSettingsScreen_ExitScreen(void* self)
	{
		// Inside a mod's settings, Esc/controller B/the on-screen Back button steps up one level: a nested group
		// returns to its parent section, and the root returns to the mod list. Only the mod-list view actually closes
		// the options screen.
		auto* screen = static_cast<MiscSettingsScreen*>(self);
		const bool on_mods_tab = screen->m_current_category_button == reinterpret_cast<GUIComponent*>(screen->m_editor_options_button);
		if (on_mods_tab && g_view == View::mod_settings)
		{
			request_back_nav();
			return; // veto the close, apply_nav applies the new view next Update
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

		// The screen is really closing now. Tear our rows down first: the engine frees a MenuScreen's components through its
		// reflection helper (which our rows are deliberately not registered in), not by walking mComponents, so on close it
		// would neither free nor double-free them - they would just leak destroy_rows is a no-op when g_rows is already empty
		// (e.g. closing off the Mods tab).
		g_options_screen_open    = false; // stop gating on_change on this now-closing screen.
		g_dynamic_refresh_settle = 0.0f;  // drop any pending numeric-change refresh for the closing screen.
		destroy_rows(screen);
		exit_edit_mode();

		big::g_hooking->get_original<hook_MiscSettingsScreen_ExitScreen>()(self);
	}

	// Reset choke-point: sgg::MiscSettingsScreen::RestoreDefaults (virtual slot 21) is the single handler for both the
	// [I]/MenuInfo control and a mouse click on the on-screen Reset button. On our Mods tab the native reset is a no-op
	// (our rows' mDataValue is not a ConfigOptionsField key).
	static void hook_MiscSettingsScreen_RestoreDefaults(void* self)
	{
		auto* screen = static_cast<MiscSettingsScreen*>(self);
		if (screen->m_current_category_button == reinterpret_cast<GUIComponent*>(screen->m_editor_options_button))
		{
			if (g_view != View::mod_settings)
			{
				return; // reset is unavailable in the mod list, do nothing (and do not play the native reset)
			}
			perform_reset();
		}

		big::g_hooking->get_original<hook_MiscSettingsScreen_RestoreDefaults>()(self);
	}

#pragma endregion

#pragma region Hook registration

	void register_hooks()
	{
		// Resolve every engine symbol, RVA and offset the Mods tab depends on up front. The symbol map is built from the
		// game's live PDB, so if the game updates and a required function moved or was renamed it resolves to null here
		// likewise the hardcoded RVAs and struct offsets this feature was reverse-engineered against only match one specific
		// Ship build.
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

		// Functions we hook (installed below, once everything checks out).
		const auto ctor             = require("sgg::MiscSettingsScreen::MiscSettingsScreen");
		const auto do_show_category = require("sgg::MiscSettingsScreen::DoShowCategory");
		const auto on_clicked       = require("sgg::GUIComponentButton::OnClicked");
		const auto update           = require("sgg::MiscSettingsScreen::Update");
		const auto update_scroll    = require("sgg::MiscSettingsScreen::UpdateScrollState");
		const auto handle_input     = require("sgg::MiscSettingsScreen::HandleInput");
		const auto set_number_value = require("sgg::GUIComponentNumBox::SetNumberValue");

		// Engine helpers called while building and editing rows. A null call here would crash, so every one is
		// required. The button ctor doubles as the RVA anchor for the templated/overloaded helpers resolved further
		// down.
		const auto anchor = require("sgg::GUIComponentButton::GUIComponentButton");
		g_button_ctor     = anchor.as_func<void*(void*, void*)>();
		g_set_label       = require("sgg::GUIComponentButton::SetDisplayName").as_func<void(void*, const char*)>();
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

		// Optional helpers: every call site is null-guarded, so their absence only degrades a visual or teardown detail
		// (never crashes) and must not gate the feature.
		g_get_lines = big::hades2_symbol_to_address["sgg::GUIComponentTextBox::GetLines"].as_func<void*(void*)>();
		g_set_selected_texture = big::hades2_symbol_to_address["sgg::GUIComponentButton::SetSelectedTexture"].as_func<void(void*, std::uint32_t)>();
		g_button_dtor = big::hades2_symbol_to_address["sgg::GUIComponentButton::~GUIComponentButton"].as_func<void(void*)>();
		g_disable = big::hades2_symbol_to_address["sgg::GUIComponentButton::Disable"].as_func<void(void*)>();

		// Slider construction + drag hook (optional: if any is missing, bounded numbers fall back to the number-box stepper).
		// SetFraction is both the initial set and the drag hook (installed below). The slider vtable is resolved by name (RVA
		// fallback) once the build is verified.
		g_gui_component_ctor = big::hades2_symbol_to_address["sgg::GUIComponent::GUIComponent"].as_func<void(void*, std::uint64_t)>();
		g_image_ctor = big::hades2_symbol_to_address["sgg::GUIComponentImage::GUIComponentImage"].as_func<void(void*, std::uint64_t)>();
		g_textbox_ctor = big::hades2_symbol_to_address["sgg::GUIComponentTextBox::GUIComponentTextBox"].as_func<void(void*, std::uint64_t)>();
		g_slider_defaults = big::hades2_symbol_to_address["sgg::GUIComponentSlider::Defaults"].as_func<void(void*)>();
		const auto slider_set_fraction = big::hades2_symbol_to_address["sgg::GUIComponentSlider::SetFraction"];
		g_slider_set_fraction          = slider_set_fraction.as_func<void(void*, float, bool)>();

		// Controller focus: ComponentFocused makes a row the focused option, and GetState reads Back/Cancel for
		// drilldown back-nav. Both are optional by-name lookups.
		g_component_focused = big::hades2_symbol_to_address["sgg::MiscSettingsScreen::ComponentFocused"].as_func<void(void*, GUIComponent*)>();
		g_set_mouse_over = big::hades2_symbol_to_address["sgg::MenuScreen::SetMouseOver"].as_func<void(void*, GUIComponent*)>();
		g_input_get_state = big::hades2_symbol_to_address["sgg::InputHandler::GetState"].as_func<std::uint32_t(void*, const void*)>();
		g_mouse_button_down = big::hades2_symbol_to_address["sgg::InputHandler::IsLeftOrRightMouseButtonDown"].as_func<bool(void*)>();

		// Left/right press edges (dpad, arrow keys and a left-stick flick fold into these), read to move our discrete
		// slider rows one step per press instead of the native continuous slide. Optional - without them the sliders
		// keep the native continuous keyboard/controller behaviour.
		g_input_was_left_pressed = big::hades2_symbol_to_address["sgg::InputHandler::WasLeftPressed"].as_func<bool(void*)>();
		g_input_was_right_pressed = big::hades2_symbol_to_address["sgg::InputHandler::WasRightPressed"].as_func<bool(void*)>();

		// Native-settings flush before a forced restart. SaveProfile persists language, volumes, graphics and gameplay
		// toggles. Optional - missing symbols only mean those native edits may wait for a normal save.
		g_save_profile = big::hades2_symbol_to_address["sgg::ProfileManager::SaveProfile"].as_func<char(void*, bool, bool)>();
		g_active_profile = big::hades2_symbol_to_address["sgg::ProfileManager::ACTIVE_PROFILE"].as<void*>();

		// Config/control GLOBALS, resolved by name (update-proof - they are named PDB data symbols that move with
		// .data/.rdata across updates, so an anchor-relative RVA cannot be trusted). None crash.
		g_use_mouse       = big::hades2_symbol_to_address["sgg::ConfigOptions::UseMouse"].as<const bool*>();
		g_config_language = big::hades2_symbol_to_address["sgg::ConfigOptions::Language"].as<const char*>();
		g_controls_cancel = big::hades2_symbol_to_address["sgg::Controls::Cancel"].as<const void*>();
		g_controls_select = big::hades2_symbol_to_address["sgg::Controls::Select"].as<const void*>();

		// The game's CRT heap (see game_alloc). Missing means disable, never fall back to H2M's own CRT.
		if (HMODULE ucrt = ::GetModuleHandleW(L"ucrtbase.dll"))
		{
			g_game_aligned_malloc = reinterpret_cast<aligned_malloc_fn>(::GetProcAddress(ucrt, "_aligned_malloc"));
			g_game_aligned_free   = reinterpret_cast<aligned_free_fn>(::GetProcAddress(ucrt, "_aligned_free"));
		}
		if (!g_game_aligned_malloc || !g_game_aligned_free)
		{
			missing.push_back("ucrtbase.dll _aligned_malloc/_aligned_free (the game's CRT heap)");
		}

		// The hardcoded RVAs and struct offsets above are valid only for the Ship build they were captured against, and
		// unlike the name-resolved symbols they do NOT auto-adapt - a game update could move them and crash the options
		// screen. So gate the menu on the exact build via its PDB GUID: after an update the GUID no longer matches and
		// the tab is cleanly skipped (the rom.mod_settings Lua API is unaffected) until Hell2Modding is updated.
		static constexpr const char* validated_pdb_guid = "744ea71c-2c21-4b40-a6c486d1fa6647da";
		const bool build_validated                      = big::hades2_pdb_guid == validated_pdb_guid;

		// Secondary sanity check on top of the GUID allow-list: the anchor (button ctor) must sit at its known module
		// RVA. A matching GUID already implies this, so a failure here means the PDB and the loaded exe disagree (e.g.
		// a mismatched/hand-swapped PDB), which would make every RVA/offset untrustworthy.
		uintptr_t game_base   = 0;
		std::size_t game_size = 0;
		::module_info_helper::get_module_base_and_size(&game_base, &game_size, nullptr);
		const bool build_matches = anchor && game_base && (anchor.as<uintptr_t>() - game_base == anchor_rva);

		// push_back is a named PDB symbol but is occasionally emitted inline fall back to its RVA
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
				detail += "'). The game likely updated; update validated_pdb_guid to this GUID after re-validating the "
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

		// Build verified and every required symbol resolved: derive the remaining anchor-relative helpers and hook.
		// These are .text functions that cannot be picked unambiguously by name, plus TeleportCursorTo.
		const auto anchor_base = anchor.as<uintptr_t>() - anchor_rva;
		g_message_dialog_ctor  = reinterpret_cast<message_dialog_ctor_fn>(anchor_base + message_dialog_ctor_rva);
		g_add_screen           = reinterpret_cast<add_screen_fn>(anchor_base + add_screen_rva);
		g_numbox_factory       = reinterpret_cast<numbox_factory_fn>(anchor_base + numbox_factory_rva);
		g_teleport_cursor      = reinterpret_cast<teleport_cursor_fn>(anchor_base + teleport_cursor_rva);

		// Slider vtable: prefer the named public symbol (update-proof), fall back to the anchor-relative RVA (which
		// lives in .rdata and shifts on updates) only if the vtable is absent from the symbol map.
		if (const auto slider_vt = big::hades2_symbol_to_address["??_7GUIComponentSlider@sgg@@6B@"]; slider_vt)
		{
			g_slider_vtable = slider_vt.as<uintptr_t>();
		}
		else
		{
			g_slider_vtable = anchor_base + slider_vtable_rva;
		}

		// Build a patched copy of the slider vtable whose GetArea/GetScreenArea slots return a one-row hit rect (see
		// build_row_area_vtable). The native slider GetArea unions the slider's sub-components into a screen-spanning
		// rectangle that, through the nearest-anchor hover tiebreak, hijacks mouse hover (and keyboard nav) from other rows
		// on a mixed page.
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

		// All required by the checks above, so install unconditionally. OnClicked and SetNumberValue are global (they
		// fire for every button/num-box in the game). Their callbacks filter to our rows via find_row, so installing
		// them is a no-op for the rest of the game's UI.
		static auto onclick_hook = hooking::detour_hook_helper::add_queue<hook_GUIComponentButton_OnClicked>(
		    "sgg::GUIComponentButton::OnClicked",
		    on_clicked);
		static auto snv_hook = hooking::detour_hook_helper::add_queue<hook_GUIComponentNumBox_SetNumberValue>(
		    "sgg::GUIComponentNumBox::SetNumberValue",
		    set_number_value);

		// Optional: persists user drags on our slider rows (filtered to our rows via find_row, so it is a no-op for the
		// native audio sliders). If absent, bounded numbers render as the number-box stepper.
		if (slider_set_fraction)
		{
			static auto set_fraction_hook = hooking::detour_hook_helper::add_queue<hook_GUIComponentSlider_SetFraction>("sgg::GUIComponentSlider::SetFraction", slider_set_fraction);

			// Discrete keyboard/controller stepping needs the left/right edge probes. Without them our slider rows keep
			// the native continuous slide, so only install the input override when both resolved.
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

		// Every close path (Escape key, controller B, clicking the on-screen Exit button) funnels through ExitScreen,
		// so this is where the restart-required prompt is triggered.
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

		// Optional: the on-screen "Reset" button ([I]/MenuInfo control or mouse) funnels through RestoreDefaults.
		// Without it the Mods tab still works. Reset just won't restore mod defaults.
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
