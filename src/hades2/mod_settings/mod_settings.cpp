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
	using sgg::GUIComponent;
	using sgg::MenuScreen;
	using sgg::MiscSettingsScreen;
	using sgg::Vec2;

	// Hades II's in-game options menu is the native C++ screen sgg::MiscSettingsScreen.
	// Its category tabs include several non-user categories (Editor, Debug, ...) that are
	// created but hidden; the "Editor" one is reused as the "Mods" tab.
	//
	// Option rows are native GUIComponentButtons built here. A freshly constructed
	// component is invisible because it has no visual data; MenuScreen::ApplyDataToComponent
	// applies the screen's SJSON template whose name matches the component's mName, which
	// is what makes it render. So each row is named after an existing template
	// ("CategoryOptionsButton"), has that template applied, is given its label, and is
	// linked into mComponents (drawn) and mOptions (freed/unlinked on category switch).

	// GUIComponent::mName lives at this offset; it is an eastl::string used by
	// ApplyDataToComponent to look up the matching template.
	static constexpr std::size_t gui_component_name_offset = 0x4'88;

	// Each GUIComponent embeds an sgg::ComponentData (mData) whose mDef (sgg::ComponentDataDef)
	// drives its visuals/layout. Retuning mDef then re-running ComponentData::SetupComponent
	// re-applies the template - this is how a plain button is converted into a key-rebind
	// style text row. Offsets validated against the Ship Hades2.pdb.
	static constexpr std::size_t component_data_offset = 0x88; // GUIComponent::mData (sgg::ComponentData)
	static constexpr std::size_t component_def_offset  = 0xA8; // mData(0x88) + ComponentData::mDef(0x20)

	// Field offsets inside sgg::ComponentDataDef (relative to component_def_offset).
	static constexpr std::size_t def_use_text_area      = 0x05;   // mUseTextArea        (bool)
	static constexpr std::size_t def_add_text_area      = 0x06;   // mAddTextArea        (bool)
	static constexpr std::size_t def_y                  = 0x20;   // mY  (float)  row Y, read by UpdateScrollState
	static constexpr std::size_t def_offset_y           = 0x2C;   // mOffsetY (float) template vertical offset
	static constexpr std::size_t def_scale              = 0x34;   // mScale  (float) uniform component scale
	static constexpr std::size_t def_text_offset_x      = 0x50;   // mTextOffsetX        (float)
	static constexpr std::size_t def_width              = 0x74;   // mWidth  (float -> mCustomWidth)
	static constexpr std::size_t def_height             = 0x78;   // mHeight (float -> mCustomHeight)
	static constexpr std::size_t def_graphic            = 0x80;   // mGraphic            (HashGuid)
	static constexpr std::size_t def_selected_graphic   = 0x84;   // mSelectedGraphic    (HashGuid)
	static constexpr std::size_t def_alternate_graphic  = 0x88;   // mAlternateGraphic   (HashGuid)
	static constexpr std::size_t def_add_color          = 0x0D;   // mAddColor           (bool)
	static constexpr std::size_t def_red                = 0xEC;   // mRed  button tint   (float)
	static constexpr std::size_t def_green              = 0xF0;   // mGreen button tint  (float)
	static constexpr std::size_t def_blue               = 0xF4;   // mBlue  button tint  (float)
	static constexpr std::size_t def_text_justification = 0xEA;   // mTextJustification  (sgg::Justification: LEFT=0)
	static constexpr std::size_t def_text_red           = 0x1'0C; // mTextRed            (float)
	static constexpr std::size_t def_text_green         = 0x1'10; // mTextGreen          (float)
	static constexpr std::size_t def_text_blue          = 0x1'14; // mTextBlue           (float)
	static constexpr std::size_t def_sel_text_red       = 0x1'28; // mSelectedTextRed    (float)
	static constexpr std::size_t def_sel_text_green     = 0x1'2C; // mSelectedTextGreen  (float)
	static constexpr std::size_t def_sel_text_blue      = 0x1'30; // mSelectedTextBlue   (float)
	static constexpr std::size_t def_spacing    = 0x1'5C; // mSpacing (float) row pitch, read by UpdateScrollState
	static constexpr std::size_t def_fade_speed = 0x2'1C; // mFadeSpeed (float) opacity ease rate (component +0x2C4)

	// Opacity ease rate applied to every row so all row types fade at one uniform speed. The native
	// OptionToggleButton / OptionNumBox templates use 10.0; CategoryOptionsButton (our text/value/
	// group rows) declares none, so we set it explicitly. GUIComponent::Update moves mFadeOpacity
	// toward mFadeTarget by dt * mFadeSpeed each frame, so this drives the fade timing.
	static constexpr float row_fade_speed = 10.0f;

	// Native sgg::MessageDialog (the single-button message box the game shows in the MAIN MENU for
	// save/file errors, ShellText SaveErrorPC/FileAccessErrorPC). Unlike the Lua screen system it
	// does not need a loaded save, so it works when mods are toggled in the main menu. Offsets +
	// RVAs DIA-validated against the current Ship Hades2.pdb.
	static constexpr std::size_t message_dialog_size          = 0x2'F0; // sizeof sgg::MessageDialog
	static constexpr std::size_t screen_manager_offset        = 0x48;   // sgg::GameScreen::mScreenManager
	static constexpr std::size_t screen_removed_offset        = 0x21;   // sgg::GameScreen::mRemoved (bool)
	static constexpr std::size_t screen_visible_offset        = 0x22;   // sgg::GameScreen::mIsVisible (bool)
	static constexpr std::size_t screen_block_input_offset    = 0x24;   // sgg::GameScreen::mBlockLowerInput (bool)
	static constexpr std::size_t dialog_title_offset          = 0x1'88; // sgg::MenuScreen::mTitleText
	static constexpr std::size_t dialog_confirm_button_offset = 0x1'A0; // sgg::MenuScreen::mConfirmButton
	static constexpr std::size_t dialog_message_offset        = 0x2'B0; // sgg::MessageDialog::mMessageText

	// The MessageDialog.sjson MessageText template renders at FontSize 26, which is larger than we
	// want for the multi-line body. The rendered size is driven by GUIComponentTextBox::mFontHandle
	// (@0x6A4); scaling its mFontSizeRatio (@+0x0C) / mEnglishFontSizeRatio (@+0x10) shrinks it. The
	// def's mFontSize is ignored once the sjson template is loaded, so we scale the live handle.
	static constexpr std::size_t textbox_font_handle_offset        = 0x6'A4; // GUIComponentTextBox::mFontHandle
	static constexpr std::size_t font_handle_size_ratio_offset     = 0x0C;   // sgg::FontHandle::mFontSizeRatio
	static constexpr std::size_t font_handle_eng_size_ratio_offset = 0x10;   // sgg::FontHandle::mEnglishFontSizeRatio
	static constexpr float restart_message_font_scale              = 0.75f;  // ~26 -> ~19.5

	// Module-relative RVAs (current Ship build) for the overloaded functions that cannot be picked
	// by name from the PDB symbol map. Resolved at runtime relative to the button-ctor anchor:
	// anchor_runtime - anchor_rva + target_rva. AddScreen has three overloads; the 4-arg one
	// inserts at the END of the screen list (drawn on top), unlike the 2-arg one which front-inserts
	// (drawn under the full-screen options menu = invisible).
	static constexpr std::uintptr_t anchor_rva = 0x11'5C'70; // sgg::GUIComponentButton::GUIComponentButton
	static constexpr std::uintptr_t message_dialog_ctor_rva = 0x16'EE'60; // sgg::MessageDialog::MessageDialog(this,sm,eastl::string*)
	static constexpr std::uintptr_t add_screen_rva = 0x14'7D'D0; // sgg::ScreenManager::AddScreen(this,screen,bool,eastl::string*)
	// tf_new_internal<sgg::GUIComponentNumBox, sgg::MiscSettingsScreen*>: the game's own factory that
	// allocates a GUIComponentNumBox, sets its vtable and builds its 5 sub-components (box graphic,
	// label, value text, left/right arrows). Template instantiation, so resolved by RVA off the anchor.
	static constexpr std::uintptr_t numbox_factory_rva = 0x17'A5'30;

	// eastl::vector<GUIComponent*>::push_back, used only as a fallback when the named PDB symbol is
	// missing (it is sometimes emitted inline). Resolved off the same button-ctor anchor.
	static constexpr std::uintptr_t push_back_rva = 0x14'1E'D0;

	// sgg::GUIComponentNumBox field offsets (DIA-validated on the current Ship build). sizeof 0x5D0;
	// derives directly from GUIComponent (not GUIComponentButton).
	static constexpr std::size_t numbox_value_offset = 0x5'40;      // mNumberValue      (float)
	static constexpr std::size_t numbox_step_offset  = 0x5'44;      // mNumberStepValue  (float)
	static constexpr std::size_t numbox_min_offset   = 0x5'48;      // mNumberMin        (float)
	static constexpr std::size_t numbox_max_offset   = 0x5'4C;      // mNumberMax        (float)
	static constexpr std::size_t numbox_is_integer_offset = 0x5'50; // mIsInteger        (bool: discrete + integer display)
	static constexpr std::size_t numbox_disable_input_offset = 0x5'63; // mDisableInput   (bool: HandleInput early-out)
	static constexpr std::size_t numbox_value_text_offset    = 0x5'B0; // mValueTextBox     (GUIComponentTextBox*)
	static constexpr std::size_t numbox_left_arrow_offset    = 0x5'98; // mLeftArrow        (GUIComponentAnimation*)
	static constexpr std::size_t numbox_right_arrow_offset   = 0x5'A0; // mRightArrow       (GUIComponentAnimation*)
	static constexpr std::size_t numbox_label_text_offset = 0x5'A8; // mTextBox          (GUIComponentTextBox*, the label)
	static constexpr std::size_t numbox_sizeof = 0x5'D0;
	// Scalar deleting destructor slot in the GUIComponent vtable. Called with flags=0 it destructs
	// and frees any owned sub-components without the final operator delete, so we then _aligned_free.
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

	// sgg::HashGuid is a 32-bit interned-string id in its first field.
	struct HashGuid
	{
		std::uint32_t m_id;
	};

	using hash_lookup_fn = HashGuid* (*)(HashGuid * out, const char* str, std::size_t len);

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

	// Set true by register_hooks only once every engine symbol, RVA and offset the Mods tab needs has
	// resolved for the running game build. While false no hooks are installed and the tab is absent;
	// it also gates process-global side effects (the wndproc callback) as a safety net.
	static bool g_feature_enabled = false;

	// sgg::KeyboardButtonId values used for edit confirm/cancel (validated in the PDB).
	static constexpr int key_escape   = 0;
	static constexpr int key_kp_enter = 113;
	static constexpr int key_return   = 127;

	// Hash of the game's "Blank" (empty) graphic, resolved once, used to hide a row's
	// button background so it renders as a plain text label.
	static std::uint32_t g_blank_graphic = 0;

	// Panel layout, in native 1080p menu coordinates. The engine's UpdateScrollState pass
	// positions each on-page row at Y = (index - pageStart) * row_pitch + row_base_y +
	// ScreenCenterOffsetY, and X = the row's own location. Rows mirror the key-rebind
	// ControlButton layout: the component is anchored to the right pane and its text is
	// left-justified via a negative text offset, matching the native option-name column.
	static constexpr float row_location_x      = 1560.0f; // component X (right pane), like OptionToggleButton
	static constexpr float row_text_offset_x   = -900.0f; // left-justify the label to the option-name column
	static constexpr float value_text_offset_x = 15.0f; // right-justify the value; right edge aligns with the toggle's
	static constexpr float numbox_location_x   = 1365.0f; // native OptionNumBox X (box + arrows clear the scrollbar)
	static constexpr float button_center_x     = 1130.0f; // centered action button X (clear of the scrollbar)
	static constexpr float row_base_y          = 300.0f;  // first row's Y - matches the vanilla option templates
	static constexpr float row_pitch           = 45.0f;   // vertical distance between rows (vanilla Spacing = 45)
	static constexpr std::uint32_t rows_per_page = 10;    // vanilla ItemsPerPage = 10

	// Config sections. Both rom.mod_settings.load and Chalk bind a mod's settings under the root
	// "config" section; nested groups are dot-separated child sections (e.g. "config.biome_pool").
	static const std::string root_section = "config";
	// Chalk writes a placeholder entry with this key per section so empty groups persist; skip it.
	static constexpr const char* section_empty_key = "...";

	// Approximate visual width budget for the right-column value (freetext + its edit caret), in
	// "width units" where a typical medium glyph is 1.0. The menu font is variable-width, so a raw
	// character count looks inconsistent (a run of 'W' is far wider than a run of 'i'); budgeting by
	// summed glyph weight keeps the shown value a consistent WIDTH so it does not run left into the
	// key label. ~30 units is roughly 30 average glyphs wide.
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
	};

	struct PanelRow
	{
		GUIComponent* component = nullptr;
		RowKind kind            = RowKind::mod_entry;
		std::string stem;        // owning mod's config-file stem
		std::string setting_key; // config entry key (setting rows only)

		// The bound config entry (setting rows only); valid for the config file's lifetime,
		// which spans the whole menu session.
		toml_v2::config_file::config_entry_base* entry = nullptr;

		bool disabled          = false; // greyed & non-interactable (mod disabled)
		bool is_enabled_toggle = false; // the mod's master "enabled" toggle

		// Author-provided description shown at the bottom of the screen while this row is
		// highlighted (setting rows only; empty for navigation rows).
		std::string description;

		// Right-column value display for a non-bool setting row (paired with `component`, the
		// left-column key). Not in mOptions; positioned to follow `component` each frame.
		GUIComponent* value_component = nullptr;

		// Numeric stepper (bounded number setting: metadata has both min and max). Left/right
		// adjusts the value by `stepper_step`, clamped to [stepper_min, stepper_max]; a mouse
		// click increments and wraps. When false, a numeric setting uses the freetext editor.
		bool is_stepper     = false;
		double stepper_min  = 0.0;
		double stepper_max  = 0.0;
		double stepper_step = 1.0;

		// Enum cycler (metadata has `values`). Rendered as a native number box over the index
		// 0..labels-1 whose value text is overridden to the label (like the game's own enum
		// options). `enum_values` are the serialized config values, `enum_labels` the parallel
		// display strings; both indexed by the box's current integer value.
		bool is_enum = false;
		std::vector<std::string> enum_values;
		std::vector<std::string> enum_labels;

		// Group rows (RowKind::group) only: the child config section this row drills into.
		std::string target_section;
	};

	static std::vector<PanelRow> g_rows;

	// Set when a restart-required setting is changed this menu session (e.g. toggling the
	// "enabled" switch of an sjson-backed mod). On options-menu close we warn + close the game.
	static bool g_restart_required = false;

	// The restart-causing changes this session, keyed by "<stem>\0<section>\0<key>" so re-editing
	// the same setting overwrites its line rather than adding a duplicate. Values are the
	// human-readable lines listed in the restart popup, e.g. "MyMod: Enabled (on)".
	static std::map<std::string, std::string> g_restart_changes;

	// Baseline serialized value (as of this menu session's open) for each restart-required setting
	// that was touched, keyed identically to g_restart_changes. Used to drop a setting from the
	// restart list when it is changed back to its baseline (no net change -> no restart needed).
	static std::map<std::string, std::string> g_restart_baselines;

	// The native restart message box's (only) button; clicking it closes the game (restart).
	static GUIComponent* g_restart_confirm_button = nullptr;

	// True once the restart prompt has been shown this menu session (so closing again proceeds).
	static bool g_restart_prompt_shown = false;

	// Which view the Mods panel is currently showing, plus a deferred navigation request
	// that a click sets and the Update hook applies at a safe point (outside input/click
	// iteration, where mutating the component vectors is safe).
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
	static bool g_nav_reset_to_top = false; // Reset action: force a top (non-instant) rebuild next apply_nav

	// Freetext edit state (number/string settings). A click enters edit mode; typed input
	// is captured in the window procedure and applied on the game thread in the Update hook.
	static bool g_editing                                        = false;
	static GUIComponent* g_edit_component                        = nullptr;
	static toml_v2::config_file::config_entry_base* g_edit_entry = nullptr;

	static std::string g_edit_buffer;
	static std::size_t g_edit_cursor = 0;     // caret position as a byte index into g_edit_buffer
	static bool g_edit_numeric       = false; // restrict input to a numeric literal
	static bool g_edit_confirm       = false;
	static bool g_edit_cancel        = false;

	// Turns a config-file stem ("AuthorName-ModName") into a display name: drops the author (up to
	// the first '-') and runs the mod name through key_to_display, so '_' becomes a space and
	// camelCase / PascalCase word boundaries are split - the same friendly-name logic used for
	// setting keys. "SGG_Modding-Chalk" -> "Chalk"; "NikkelM-Zagreus_Journey" -> "Zagreus Journey";
	// "zerp-DreamDiveTweaks" -> "Dream Dive Tweaks".
	static std::string key_to_display(const std::string& key); // shared friendly-name logic, defined below

	static std::string display_name_from_stem(const std::string& stem)
	{
		const auto dash        = stem.find('-');
		const std::string name = (dash == std::string::npos) ? stem : stem.substr(dash + 1);
		return key_to_display(name);
	}

	// The mod's Thunderstore manifest description, shown in the description box while its row in the
	// mod list is highlighted. Empty when no loaded module matches the stem.
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

	// Escapes the characters the game's text parser (GUIComponentTextBox::Parse) treats as markup,
	// so arbitrary user text - config values (e.g. Windows paths with '\'), display names and
	// descriptions - renders verbatim instead of being mangled. The parser reads '\' as an escape
	// lead that consumes the following word ("D:\Program..." -> "D: ...") and '[' ']' as inline-tag
	// delimiters whose contents are dropped ("[deprecated] x" -> " x"). A leading backslash makes
	// each literal (\\ -> \, \[ -> [, \] -> ]); backslash MUST be escaped first. ('{' and '@' are
	// also markup leads but have no literal escape in the parser, so are left as-is - they are rare
	// in config text and, unlike '\'/'[', do not silently eat surrounding characters.)
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

	// --- Text metrics + caret helpers (byte indices into a string; UTF-8 aware) ---

	// Approximate width of a single byte in the value font, in the same units as
	// value_display_max_width (medium glyph = 1.0). The menu font (P22UndergroundSCMedium) is
	// variable-width; these rough classes are enough to fit values by visual width instead of raw
	// character count (exact pixel measurement is intentionally avoided - it would need the engine's
	// SpriteFont globals). A UTF-8 lead byte counts once as a medium glyph; continuation bytes add 0.
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

	// True for a "word" byte: ASCII alphanumeric, underscore, or any UTF-8 byte (>=0x80, so non-ASCII
	// letters count as word characters). Used for Ctrl+Left/Right word skip.
	static bool is_word_byte(char c)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		return (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u == '_' || u >= 0x80;
	}

	// Caret one codepoint to the left (skips UTF-8 continuation bytes so a multibyte char moves as
	// a unit).
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

	// Caret to the start of the current/previous word (Ctrl+Left): skip any non-word bytes to the
	// left, then the run of word bytes.
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

	// Caret to the start of the next word (Ctrl+Right): skip the current run of word bytes, then the
	// following non-word bytes.
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

	// Caps an over-wide value string for the right-aligned value column so it does not run left into
	// the option's key label. Keeps the TAIL with a leading ellipsis (most informative for a path,
	// and where the append/backspace edit caret sits). Fits by summed glyph WIDTH, not character
	// count, so wide/narrow text shows a consistent visual width. Operates on the logical (pre-escape)
	// string; escape the result afterwards.
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

		if (g_set_label)
		{
			g_set_label(button, "Mods");
		}
	}

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

	static GUIComponent* create_button(MiscSettingsScreen* screen)
	{
		if (!g_button_ctor || !g_push_back || !g_apply_data)
		{
			return nullptr;
		}

		auto* row = static_cast<GUIComponent*>(_aligned_malloc(sgg::gui_component_button_size, 8));
		if (!row)
		{
			return nullptr;
		}

		g_button_ctor(row, screen);
		*reinterpret_cast<void**>(reinterpret_cast<char*>(row) + sgg::gui_component_button_owner_offset) = screen;
		return row;
	}

	// Links a finished row into the drawn/hit-tested (mComponents) and paged (mOptions)
	// vectors, sets its X, and starts it transparent. UpdateScrollState only fades in and
	// repositions on-page rows, so off-page rows must start invisible to avoid flashing
	// stacked at the top.
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

	// Shows the on or off toggle graphic for a toggle row. The OptionToggleButton template
	// stores both graphic hashes in the row's def (mGraphic = on, mAlternateGraphic = off);
	// pick one and set it as the drawn texture.
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

	// Dims a row's def text colours (both normal and selected) so a disabled row reads as
	// greyed out and does not recolour on hover. Must be applied before SetupComponent so
	// the change reaches the text box.
	static void set_def_text_grey(GUIComponent* row)
	{
		char* def                                           = reinterpret_cast<char*>(row) + component_def_offset;
		constexpr float grey                                = 0.22f;
		*reinterpret_cast<float*>(def + def_text_red)       = grey;
		*reinterpret_cast<float*>(def + def_text_green)     = grey;
		*reinterpret_cast<float*>(def + def_text_blue)      = grey;
		*reinterpret_cast<float*>(def + def_sel_text_red)   = grey;
		*reinterpret_cast<float*>(def + def_sel_text_green) = grey;
		*reinterpret_cast<float*>(def + def_sel_text_blue)  = grey;
	}

	// Sets a row's normal text colour to the native settings-option grey (0.55) used by the
	// game's own OptionToggleButton / OptionNumBox rows, so plain-text (key/value) rows built on
	// the CategoryOptionsButton template (whose own text is a darker 0.35) match the toggle rows
	// instead of reading as brighter full white. The selected colour is left as the template's
	// (the same green highlight both templates use) so hover still highlights. Must run before
	// SetupComponent to reach the text box.
	static void set_def_text_normal(GUIComponent* row)
	{
		char* def                                       = reinterpret_cast<char*>(row) + component_def_offset;
		constexpr float option_grey                     = 0.55f; // matches MiscSettingsScreen.sjson option rows
		*reinterpret_cast<float*>(def + def_text_red)   = option_grey;
		*reinterpret_cast<float*>(def + def_text_green) = option_grey;
		*reinterpret_cast<float*>(def + def_text_blue)  = option_grey;
	}

	// A plain left-justified text row (mod names, Back, and non-toggle settings). Applies a
	// template for a valid font/colours, then retunes the row's own def into the key-rebind
	// "ControlButton" style - no background graphic, left text, and a text-area hit region
	// that hugs the label - and clears any leftover textures. Disabled rows are greyed and
	// made non-interactable.
	static GUIComponent* make_text_row(MiscSettingsScreen* screen, const char* label, bool disabled = false)
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
		*reinterpret_cast<std::uint32_t*>(def + def_selected_graphic)  = 0; // no highlight box (text recolours instead)
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
			set_def_text_normal(row);
		}

		if (g_setup_component)
		{
			g_setup_component(row, row_bytes + component_data_offset);
		}

		// SetupComponent applies our zeroed graphic fields but does not actively tear down
		// the normal/selected textures a prior template already set. Clear them explicitly.
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

		if (disabled && g_disable)
		{
			g_disable(row);
		}

		finalize_row(screen, row);
		return row;
	}

	// A toggle row (boolean setting): a left-justified label plus the native on/off toggle
	// switch graphic on the right. The OptionToggleButton template already supplies the
	// toggle graphic, left-justified text and text area; we only realign it to our row grid
	// (mY/mSpacing, read directly by UpdateScrollState) and choose the on/off graphic.
	// Disabled rows are greyed and made non-interactable.
	static GUIComponent* make_toggle_row(MiscSettingsScreen* screen, const char* label, bool is_on, bool disabled = false)
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

		// Greying needs a SetupComponent pass to reach the text box and button colour; the
		// toggle graphic is re-chosen afterwards so the pass does not revert it. The button
		// tint is switched from additive to a multiplicative dim so the toggle graphic reads
		// as greyed rather than full brightness.
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

		if (disabled && g_disable)
		{
			g_disable(row);
		}

		finalize_row(screen, row);
		return row;
	}

	// A centered native button row (for actions like Apply/Reset), using the
	// CategoryOptionsButton template unchanged so it keeps its Button_Secondary box graphic
	// and centered label - visually distinct from the plain-text setting rows. Only the row
	// grid position (mY/mSpacing) is overridden.
	static GUIComponent* make_button_row(MiscSettingsScreen* screen, const char* label, bool disabled = false)
	{
		auto* row = create_button(screen);
		if (!row)
		{
			return nullptr;
		}
		auto* row_bytes = reinterpret_cast<char*>(row);

		set_sso_string(row_bytes + gui_component_name_offset, "CategoryOptionsButton");
		g_apply_data(reinterpret_cast<MenuScreen*>(screen), row);

		char* def                                     = row_bytes + component_def_offset;
		*reinterpret_cast<float*>(def + def_y)        = row_base_y;
		*reinterpret_cast<float*>(def + def_spacing)  = row_pitch;
		*reinterpret_cast<float*>(def + def_offset_y) = 0.0f;  // drop the template's built-in vertical offset
		*reinterpret_cast<float*>(def + def_scale)    = 0.85f; // shrink slightly for top/bottom breathing room
		// Size the hit-test rect to cover the whole visible (scaled) button; the template's
		// 280x40 was smaller than the Button_Secondary graphic, which cut off hover top/bottom.
		*reinterpret_cast<float*>(def + def_width)  = 340.0f;
		*reinterpret_cast<float*>(def + def_height) = 58.0f;

		if (disabled)
		{
			set_def_text_grey(row);
		}

		if (g_setup_component)
		{
			g_setup_component(row, row_bytes + component_data_offset);
		}

		if (g_set_label)
		{
			g_set_label(row, label);
		}

		if (disabled && g_disable)
		{
			g_disable(row);
		}

		finalize_row(screen, row);

		// Centre the button in the content pane (finalize_row anchors rows at the right-hand
		// option column, which would put the button over the scrollbar).
		row->m_location_x = button_center_x;
		return row;
	}

	// A right-justified, non-interactive value label for the right column of a key/value
	// setting row (paired with a left-column key row). It is NOT added to mOptions: the
	// engine's scroll pass lays out only mOptions rows by index and would stack a second
	// per-row entry, so instead the value follows its key row each frame (sync_value_columns).
	// It shares the key's component X anchor but uses RIGHT justification, so the value sits in
	// the right column while the key stays left.
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

		row->m_can_be_focused = false; // never interactive; the empty hit area blocks hover/click

		finalize_row(screen, row, false); // drawn (mComponents) but not paged (mOptions)
		return row;
	}

	// True for a finite whole number (used to pick integer vs float num-box display/stepping).
	static bool is_whole(double v)
	{
		return std::isfinite(v) && v == std::floor(v);
	}

	// Overrides a num-box's centered value text (its mValueTextBox) with an enum option label. The
	// label is escaped so paths/brackets in the option text render verbatim (see escape_markup).
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

	// Builds a native sgg::GUIComponentNumBox stepper row - identical to the game's own FPS-limit /
	// graphics-quality options (boxed value flanked by Arrow_Left/Arrow_Right, left/right + arrow-click
	// stepping, keyboard + controller). The game's factory allocates it, sets the correct vtable and
	// builds all five sub-components (box graphic, label, value text, both arrows), which are also
	// freed automatically when the row vectors are torn down - so no manual cleanup is needed. Value
	// edits are persisted by the SetNumberValue hook (filtered to our rows). Returns the num-box
	// component (not a GUIComponentButton, so it never routes through the OnClicked hook). When
	// `value_labels` is non-null the box is an enum cycler: it steps the integer index and its value
	// text is overridden to the matching label instead of the raw number.
	static GUIComponent* make_numbox_row(MiscSettingsScreen* screen, const char* label, double min_v, double max_v, double step_v, double initial, bool disabled, const std::vector<std::string>* value_labels = nullptr)
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

		// Name the box and its sub-components so ApplyDataToComponent applies the matching sjson
		// templates (its virtual ApplyDataToName routes each def by the sub-component's mName).
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

		// Integer box when the bounds and step are all whole (shows "3" not "3.0" and uses the discrete
		// single-step path); otherwise a float box (decimals + analog repeat). Set the flag BEFORE
		// SetRange, whose auto-step derives from it, then pin our own step.
		const bool is_integer = is_whole(min_v) && is_whole(max_v) && is_whole(step_v);
		*reinterpret_cast<bool*>(nb_bytes + numbox_is_integer_offset) = is_integer;

		g_numbox_set_range(nb, static_cast<float>(min_v), static_cast<float>(max_v));
		*reinterpret_cast<float*>(nb_bytes + numbox_step_offset) = static_cast<float>(step_v != 0.0 ? step_v : 1.0);

		g_apply_data(reinterpret_cast<MenuScreen*>(screen), nb);

		// ApplyDataToComponent copies the OptionNumBox template's own row grid (Y=300, Spacing=45)
		// into the component; override it to our grid so the box lines up with the other rows instead
		// of drawing on the previous one. def_y/def_spacing alias the component's baseY(+0xC8) and
		// pitch(+0x204) that UpdateScrollState reads (def sits at component+0xA8).
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

		// Paint the starting value; notify=false so the SetNumberValue hook does not persist it.
		g_numbox_set_value(nb, static_cast<float>(initial), false);

		// Enum cycler: replace the raw index the box just painted with the option's label.
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
		}

		finalize_row(screen, nb);
		nb->m_location_x = numbox_location_x; // override finalize_row's default so box + arrows clear the scrollbar
		return nb;
	}

	// Removes the first pointer equal to `value` from an eastl vector by shifting the tail
	// down in place - the same unlink the engine's DoShowCategory performs. No-op if not
	// present; the backing storage is left owned by the vector.
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

	// Tears down every custom row we currently own: clears any screen pointer that still
	// references a row (so the engine cannot dereference it after free), unlinks it from
	// the drawn/hit-tested mComponents and the paged mOptions, then destroys and frees it.
	// Our rows are not registered in the reflection helper, so the engine never frees them
	// and never double-frees here. Safe to call when g_rows is empty or already unlinked.
	static void destroy_rows(MiscSettingsScreen* screen)
	{
		auto* menu = reinterpret_cast<MenuScreen*>(screen);

		auto unlink_and_free = [&](GUIComponent* comp, bool in_options, bool is_numbox)
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

			if (is_numbox)
			{
				// GUIComponentNumBox is not a GUIComponentButton; destruct it through its own vtable
				// so its five sub-components (box/label/value/arrows) are freed too. flags=0 destructs
				// without the final operator delete, so we still _aligned_free the block ourselves.
				void** vtbl = *reinterpret_cast<void***>(comp);
				auto dtor = reinterpret_cast<void* (*)(void*, unsigned int)>(vtbl[vtable_deleting_dtor_offset / sizeof(void*)]);
				dtor(comp, 0);
			}
			else if (g_button_dtor)
			{
				g_button_dtor(comp);
			}
			_aligned_free(comp);
		};

		for (const auto& row : g_rows)
		{
			unlink_and_free(row.component, true, row.is_stepper || row.is_enum);
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
			          return a.first < b.first;
		          });

		for (const auto& [display, stem] : mods)
		{
			if (auto* row = make_text_row(screen, escape_markup(display).c_str()))
			{
				PanelRow pr{row, RowKind::mod_entry, stem, {}};
				pr.description = mod_description_from_stem(stem);
				g_rows.push_back(std::move(pr));
			}
		}
	}

	// Turns an identifier into a friendly display string: underscores become spaces, and camelCase /
	// PascalCase word boundaries are split ("z_ThisConfigKey" -> "z This Config Key"). An acronym run
	// splits before its final capital when that capital starts a lowercase word ("HTTPServer" ->
	// "HTTP Server"). Used for both setting keys and mod names (via display_name_from_stem). Authors
	// can override this entirely with `display_name`.
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
		return out;
	}

	// Renders the edit buffer with a caret marker at `cursor`, windowed by visual WIDTH so the caret
	// stays visible and the whole string fits the value column (value_display_max_width) without
	// running into the key label. The window grows outward from the caret (both sides) filling the
	// budget by summed glyph width, reserving space for the caret and for whichever ellipses are
	// actually shown. The caret is a blinking "|"/" "; hidden text is marked with a leading/trailing
	// ellipsis. Each shown buffer segment is markup-escaped (a path may contain '\'); the caret and
	// ellipses are literal.
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

		// Grow a window [start, end) outward from the caret, one codepoint at a time, alternating
		// left then right, while it still fits the budget (accounting for the ellipses each side will
		// need). Left grows first each round so a right-aligned field shows preceding context.
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

	// Accepts a character into a numeric edit buffer only if the result stays a plausible
	// numeric literal: an optional leading sign (only at the front), digits, at most one decimal
	// point. `cursor` is where the character would be inserted.
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
			return buffer.find('.') == std::string::npos; // a single decimal point
		}
		return false;
	}

	// Window-procedure callback: while a freetext setting is being edited, capture typed characters
	// and caret movement into the edit buffer. Runs on the game's message-pump thread (same thread
	// as Update). Printable characters arrive via WM_CHAR (inserted at the caret); Backspace/Delete,
	// arrow movement (with Ctrl for word skip), Home/End via WM_KEYDOWN; a mouse click anywhere
	// commits the edit (Enter/Escape are read from the game input in the HandleInput hook, which
	// also blocks the menu from reacting).
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

	// Registers on_wndproc with the framework's window hook the first time it is needed.
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

	// Replaces each ASCII space with a non-breaking space (U+00A0, UTF-8 0xC2 0xA0). The message
	// textbox auto-wraps at breakable spaces (computed at the template font size, before our font
	// scaling), which would split a single logical line; non-breaking spaces keep it on one line.
	static std::string to_non_breaking(const std::string& text)
	{
		std::string out;
		out.reserve(text.size() + text.size() / 4);
		for (char c : text)
		{
			if (c == ' ')
			{
				out += "\xC2\xA0";
			}
			else
			{
				out += c;
			}
		}
		return out;
	}

	// Composite key ("<stem>\0<section>\0<key>") uniquely identifying a config entry across mods.
	static std::string restart_change_key(toml_v2::config_file::config_entry_base* entry, const std::string& stem)
	{
		return stem + '\0' + entry->m_definition.m_section + '\0' + entry->m_definition.m_key;
	}

	// Captures a restart-required setting's baseline (its value as of this menu session's open)
	// BEFORE it is first modified, so a later change back to this value can be recognised as "no
	// net change". Called just before the value is written. No-op for non-restart-required settings
	// and after the first capture for a given setting.
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

	// Records or clears a restart-required setting change after the value has been written. If the
	// new value equals the session baseline (e.g. a toggle flipped and flipped back, or a number
	// re-typed to its original), nothing actually changed, so the setting is dropped from the
	// restart list; otherwise it is listed. `new_value_display` is the value shown in the popup.
	// g_restart_required stays set as long as any real change remains.
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
			// Reverted to the session baseline: no net change, so it no longer needs a restart.
			g_restart_changes.erase(key);
		}
		else
		{
			// Keep each mod/setting/value entry on one line (see to_non_breaking).
			const std::string line = display_name_from_stem(stem) + ": " + key_to_display(entry->m_definition.m_key) + " (" + new_value_display + ")";
			g_restart_changes[key] = to_non_breaking(line);
		}

		g_restart_required = !g_restart_changes.empty();
	}

	// Refreshes a freetext row's right-column value display to show `serialized`, formatted exactly
	// as build_mod_settings renders it (width-truncated with a leading ellipsis, then markup-escaped).
	// Used to reflect a committed or cancelled edit in place, without a panel rebuild.
	static void refresh_value_display(GUIComponent* value_component, const std::string& serialized)
	{
		if (value_component && g_set_label)
		{
			const std::string disp = escape_markup(truncate_value(serialized));
			g_set_label(value_component, disp.c_str());
		}
	}

	// Commits or cancels a pending edit. Called from the HandleInput hook so it runs on the
	// same frame the triggering key/click is swallowed (HandleInput returns true that
	// frame), which prevents a submitting mouse click from also activating the row it lands
	// on. Returns true if the edit ended this call.
	static bool commit_or_cancel_edit()
	{
		if (g_edit_confirm)
		{
			if (g_edit_entry)
			{
				// Capture the session baseline before the first write so a later revert to it
				// is recognised as "no net change".
				capture_restart_baseline(g_edit_entry);

				// set_serialized_value validates (e.g. numbers) and only stores/saves a
				// valid value, so bad input for a number simply keeps the old value.
				g_edit_entry->set_serialized_value(g_edit_buffer);

				// Clamp/snap a bounded number typed via freetext to match what the stepper would
				// produce: keep it within [min, max] and, if a step is declared, snap to the nearest
				// grid point min + k*step. (The native stepper enforces both; freetext does it on
				// commit.) set_serialized_value above already parsed/validated the number.
				if (g_edit_entry->type() == typeid(double))
				{
					const auto meta = get_setting_metadata(g_edit_entry->m_config_file->m_config_file_stem_as_str,
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

				// Reflect the committed value in the right-hand display in place. Do NOT rebuild the
				// panel here: a rebuild frees and recreates every row, which snaps the visible page
				// back to the top while the scrollbar keeps the scrolled position, so the rows and
				// the scrollbar desync until the next manual scroll. Only this one value changed, so
				// just update its label (the native number-box rows persist the same in-place way).
				refresh_value_display(g_edit_component, g_edit_entry->get_serialized_value());
			}
			exit_edit_mode();
			return true;
		}
		if (g_edit_cancel)
		{
			// Restore the display to the unchanged value (the live caret label was transient); no
			// rebuild, for the same scroll-preservation reason as the commit path above.
			if (g_edit_entry)
			{
				refresh_value_display(g_edit_component, g_edit_entry->get_serialized_value());
			}
			exit_edit_mode();
			return true;
		}
		return false;
	}

	// Live-updates the edited value display (right column) with a movable, blinking caret. Called
	// from Update while editing is active; g_edit_component is the row's value component.
	static void update_edit_label()
	{
		if (g_edit_component && g_set_label)
		{
			const bool cursor_on    = ((GetTickCount64() / edit_cursor_blink_ms) % 2) == 0;
			const std::string label = render_edit_display(g_edit_buffer, g_edit_cursor, cursor_on);
			g_set_label(g_edit_component, label.c_str());
		}
	}

	// True if `key` is the mod's master enable switch ("enabled", any case).
	static bool is_enabled_key(const std::string& key)
	{
		return big::string::to_lower(key) == "enabled";
	}

	// Level 2: the leaf settings and nested groups inside config section `section` of mod `stem`.
	// Leaf entries render as setting rows (bool -> toggle, enum/bounded number -> num box, else a
	// freetext value); each direct child section renders as a group row that drills into it. At the
	// root section a boolean "enabled" entry (if present) is pinned to the top; when it is off, every
	// other row is greyed out and made non-interactable.
	static void build_mod_settings(MiscSettingsScreen* screen, const std::string& stem, const std::string& section)
	{
		// A menu item is either a leaf setting directly in `section`, or a direct child group (a
		// nested sub-section such as "config.biome_pool" while viewing "config").
		struct panel_item
		{
			bool is_group = false;
			std::string key;                                          // leaf key, or the group's last path segment
			toml_v2::config_file::config_entry_base* entry = nullptr; // leaf only
			std::string child_section;                                // group only (full "config.x.y" path)
			bool has_order  = false;
			double order    = 0.0;
			int appearance  = INT_MAX; // config.lua source rank (fallback order)
			bool is_enabled = false;   // the mod's master "enabled" toggle (root section only)
		};

		std::vector<panel_item> items;
		std::map<std::string, panel_item> groups; // child section path -> group item (keeps its min appearance)
		toml_v2::config_file::config_entry_base* enabled_entry = nullptr;
		const std::string section_prefix                       = section + ".";

		for (auto* cfg : toml_v2::config_file::g_config_files)
		{
			if (!cfg || cfg->m_config_file_stem_as_str != stem)
			{
				continue;
			}
			for (auto& [key, entry] : cfg->m_entries)
			{
				if (!entry || key.m_key == section_empty_key)
				{
					continue;
				}

				// The mod's master switch lives in the root section; track it whatever section is
				// being shown, so nested rows are greyed when the mod is disabled.
				if (!enabled_entry && key.m_section == root_section && entry->type() == typeid(bool) && is_enabled_key(key.m_key))
				{
					enabled_entry = entry.get();
				}

				if (key.m_section == section)
				{
					panel_item it;
					it.key        = key.m_key;
					it.entry      = entry.get();
					it.appearance = get_setting_appearance_order(stem, key.m_section, key.m_key);
					if (const auto meta = get_setting_metadata(stem, key.m_section, key.m_key); meta && meta->has_order)
					{
						it.has_order = true;
						it.order     = meta->order;
					}
					items.push_back(std::move(it));
				}
				else if (key.m_section.rfind(section_prefix, 0) == 0)
				{
					// A descendant section: the direct child under `section` is the first path segment
					// after the prefix. Collapse its whole subtree into one group row, ranked by its
					// earliest-defined descendant.
					const std::string rest       = key.m_section.substr(section_prefix.size());
					const std::string child      = rest.substr(0, rest.find('.'));
					const std::string child_path = section_prefix + child;
					const int app                = get_setting_appearance_order(stem, key.m_section, key.m_key);
					const auto git               = groups.find(child_path);
					if (git == groups.end())
					{
						panel_item g;
						g.is_group      = true;
						g.key           = child;
						g.child_section = child_path;
						g.appearance    = app;
						if (const auto meta = get_setting_metadata(stem, section, child); meta && meta->has_order)
						{
							g.has_order = true;
							g.order     = meta->order;
						}
						groups.emplace(child_path, std::move(g));
					}
					else if (app < git->second.appearance)
					{
						git->second.appearance = app;
					}
				}
			}
		}

		for (auto& kv : groups)
		{
			items.push_back(std::move(kv.second));
		}

		const bool mod_enabled = !enabled_entry || enabled_entry->get_value_base<bool>();
		if (section == root_section && enabled_entry)
		{
			for (auto& it : items)
			{
				if (it.entry == enabled_entry)
				{
					it.is_enabled = true;
				}
			}
		}

		// Row order: the master "enabled" toggle is pinned to the top; then rows with an author
		// `order` (ascending); then the rest. Ties and absent order fall back to config.lua source
		// order (a group's rank is its earliest-defined descendant's).
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
			                 if (!a.has_order && a.is_group != b.is_group)
			                 {
				                 return a.is_group; // with no explicit order, groups are pinned above settings
			                 }
			                 return a.appearance < b.appearance; // equal/absent order -> config.lua source order
		                 });

		for (const auto& it : items)
		{
			const bool is_enabled_row = it.is_enabled;
			const bool disabled       = !is_enabled_row && !mod_enabled;

			// A nested group drills into its child section when clicked/activated.
			if (it.is_group)
			{
				const auto gmeta = get_setting_metadata(stem, section, it.key);
				if (gmeta && gmeta->hidden)
				{
					continue;
				}
				const std::string glabel = escape_markup((gmeta && !gmeta->name.empty()) ? gmeta->name : key_to_display(it.key));
				if (auto* row = make_text_row(screen, glabel.c_str(), disabled))
				{
					PanelRow pr{row, RowKind::group, stem, {}};
					pr.disabled       = disabled;
					pr.target_section = it.child_section;
					if (gmeta)
					{
						pr.description = gmeta->description;
					}
					g_rows.push_back(std::move(pr));
				}
				continue;
			}

			const std::string& key = it.key;
			auto* entry            = it.entry;

			// Author metadata (if any) can rename the row, hide it, and (later) pick its widget.
			const auto meta = get_setting_metadata(stem, entry->m_definition.m_section, entry->m_definition.m_key);
			if (meta && meta->hidden)
			{
				continue;
			}
			const std::string label = escape_markup((meta && !meta->name.empty()) ? meta->name : key_to_display(key));

			// An enum (metadata `values`) renders as a native number box cycling its label list; a
			// numeric setting with author-declared min AND max renders as a native number box over
			// its range (like the FPS-limit option) UNLESS the author set `freetext` (e.g. for a very
			// large range better typed than stepped); other numbers stay freetext-editable with a
			// plain right-column value label.
			const bool is_number  = entry->type() == typeid(double);
			const bool is_enum    = meta && !meta->values.empty();
			const bool is_stepper = !is_enum && is_number && meta && meta->has_min && meta->has_max && !meta->freetext;
			const double step     = (meta && meta->has_step) ? meta->step : 1.0;

			// Enum option lists (serialized values + parallel labels), resolved once so the widget and
			// the PanelRow share them. The current value maps to its index, defaulting to 0.
			std::vector<std::string> enum_values;
			std::vector<std::string> enum_labels;
			int enum_index = 0;
			if (is_enum)
			{
				enum_values           = meta->values;
				enum_labels           = (meta->labels.size() == enum_values.size()) ? meta->labels : enum_values;
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
			if (entry->type() == typeid(bool))
			{
				row = make_toggle_row(screen, label.c_str(), entry->get_value_base<bool>(), disabled);
			}
			else if (is_enum)
			{
				row = make_numbox_row(screen, label.c_str(), 0.0, static_cast<double>(enum_values.size() - 1), 1.0, static_cast<double>(enum_index), disabled, &enum_labels);
			}
			else if (is_stepper)
			{
				row = make_numbox_row(screen, label.c_str(), meta->min, meta->max, step, entry->get_value_base<double>(), disabled);
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
				// Prefer the author's metadata description; fall back to the .cfg comment text.
				pr.description = (meta && !meta->description.empty()) ? meta->description : entry->m_description.m_description;

				if (is_enum)
				{
					pr.is_enum     = true;
					pr.enum_values = std::move(enum_values);
					pr.enum_labels = std::move(enum_labels);
				}
				else if (is_stepper)
				{
					pr.is_stepper   = true;
					pr.stepper_min  = meta->min;
					pr.stepper_max  = meta->max;
					pr.stepper_step = step;
				}

				g_rows.push_back(pr);
			}
		}
	}

	// Matches the native category-switch transition: the incoming page fades in and there is no
	// fade-out crossover. Native UpdateScrollState sets each on-page row's mFadeTarget to 1 and each
	// off-page row's to 0, and GUIComponent::Update (driven by MenuScreen::Update, which the original
	// runs before this) eases mFadeOpacity toward the target at dt * mFadeSpeed - so on-page rows are
	// left entirely to the native ease. We only force off-page rows fully transparent so a row leaving
	// the page vanishes at once instead of fading out on top of the incoming page. Rows are in
	// m_options / g_rows order, so row i is on the current page when start <= i < start + rows_per_page.
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

	// Value displays are not in mOptions, so the engine's scroll pass does not lay them out.
	// Mirror each value component onto its key row's current position and fade so the right
	// column tracks scrolling and fade-in/out.
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

	// The component the user is currently on: the mouse-over one (mouse) takes priority, else the
	// selected one (keyboard/controller). These are MenuScreen fields (flat struct view).
	static GUIComponent* active_row_component(MiscSettingsScreen* screen)
	{
		auto* menu = reinterpret_cast<MenuScreen*>(screen);
		return menu->m_mouse_over_component ? menu->m_mouse_over_component : menu->m_selected_component;
	}

	// Finds the PanelRow whose left-column component is `comp`, or nullptr. Valid until the next
	// panel rebuild (deferred to Update), so callers within a single input/update pass may keep it.
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

	// The component whose description was last written to the description box, so the box is only
	// updated when the highlighted row changes (not every frame). Reset when the panel rebuilds.
	static GUIComponent* g_last_description_component = nullptr;

	// Shows the highlighted row's author description in the screen's native description box
	// (MiscSettingsScreen::mDescriptionBox @ 0x460). The highlighted component is the mouse-over
	// one (mouse) or the selected one (keyboard/controller); if it is one of our rows, its
	// description is shown as raw text, otherwise the box is cleared.
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
			// Escape markup so paths/brackets in the description render verbatim (see escape_markup).
			const std::string shown = show ? escape_markup(*description) : std::string{};
			g_show_text(box, shown.c_str());

			// ShowText only marks the lines dirty; the layout (and text height, which the box's
			// justification uses to place the text) is otherwise recomputed lazily at draw time,
			// so the first visible frame would render at a stale position and visibly jump. Force
			// the line rebuild now so the first shown frame is already laid out.
			if (show && g_get_lines)
			{
				g_get_lines(box);
			}
		}

		// Re-apply the fade every frame: the native Update runs before this and re-hides the box on
		// the Mods tab (it does not use mDescriptionBox here), so a one-time set would fade back out.
		box->m_fade_opacity = show ? 1.0f : 0.0f;
		box->m_fade_target  = show ? 1.0f : 0.0f;
	}

	// Last label we wrote to each bottom-prompt button, so SetDisplayName is only called when the
	// label actually changes (avoids re-laying out the text every frame). Cleared when we leave the
	// Mods tab so the native labels take back over and re-entering re-applies ours.
	static std::string g_prompt_confirm_label;
	static std::string g_prompt_cancel_label;

	// Sets a bottom-prompt button's label (GUIComponentButton::SetDisplayName) only when it changes
	// from what we last set. The key glyph is driven by the button's bound control, not the label, so
	// it stays correct (Enter for Confirm, Esc for Cancel) regardless of the text.
	static void set_prompt_label(GUIComponent* button, std::string& cache, const char* text)
	{
		if (!button || !g_set_label || cache == text)
		{
			return;
		}
		cache.assign(text);
		g_set_label(button, text);
	}

	// Retunes the options screen's bottom button prompts for the Mods tab per context, and hides the
	// native Reset prompt where it must not apply. Called every frame from the Update hook (after the
	// original, which sets the native prompts on focus/hover/category events). Off the Mods tab it
	// only clears our caches and leaves the native prompts untouched.
	static void sync_prompts(MiscSettingsScreen* screen, bool on_mods_tab)
	{
		if (!on_mods_tab)
		{
			g_prompt_confirm_label.clear();
			g_prompt_cancel_label.clear();
			return;
		}

		auto* menu = reinterpret_cast<MenuScreen*>(screen);

		// The native prompt strings embed a glyph token that the text box expands to the device-
		// appropriate key icon: "{CN}" = the Cancel control (Esc / B), "{SL}" = the Select/Confirm
		// control (Enter / A). We prepend the same token to our custom labels so the icon is kept
		// (a raw string with no token renders text only). Labels are upper-case to match the game.
		// Cancel (Esc): "CANCEL" while editing a field; "BACK" inside a mod's settings (Esc returns to
		// the mod list, see the ExitScreen hook); "EXIT" at the mod list (closes the options screen).
		const char* cancel = g_editing ? "{CN} CANCEL" : (g_view == View::mod_settings ? "{CN} BACK" : "{CN} EXIT");
		set_prompt_label(menu->m_cancel_button, g_prompt_cancel_label, cancel);

		// Confirm (Enter): "SUBMIT" while editing; otherwise a verb matching the highlighted row.
		std::string confirm;
		if (g_editing)
		{
			confirm = "{SL} SUBMIT";
		}
		else if (PanelRow* row = find_row(active_row_component(screen)))
		{
			switch (row->kind)
			{
			case RowKind::mod_entry: confirm = "{SL} SELECT"; break;
			case RowKind::group:     confirm = "{SL} SELECT"; break;
			case RowKind::setting:
				if (row->entry && row->entry->type() == typeid(bool))
				{
					confirm = "{SL} TOGGLE";
				}
				else if (row->is_enum)
				{
					confirm = "{SL} SET";
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

		// Drive the Confirm prompt's visibility ourselves: native only fades it in (OnOptionMouseOver)
		// for its OWN option rows, which never fires for our custom rows. Show it with its glyph
		// whenever we have a hint, hide it when we don't. mFadeOpacity is the field the draw gate
		// reads; native Update rewrites mHidden each frame, so both are set here (after the original Update).
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

		// Reset prompt: shown only inside a single mod's settings (resets that mod) and not while
		// editing. It is hidden in the mod list/overview so users cannot reset every mod's config by
		// accident (the RestoreDefaults hook also swallows the shortcut there).
		if (screen->m_defaults_button)
		{
			const bool show_reset               = (g_view == View::mod_settings) && !g_editing;
			screen->m_defaults_button->m_hidden = !show_reset;
		}
	}

	static void build_panel(MiscSettingsScreen* screen, bool instant = false)
	{
		// A rebuild frees and recreates the row components, so the cached highlighted-row pointer
		// is stale; force the description box to refresh next frame.
		g_last_description_component = nullptr;

		// Preserve the current scroll offset across an in-place refresh (same view/mod, e.g.
		// after committing a setting edit or toggling "enabled") so confirming a setting on a
		// lower page does not jump back to the top. A real view change (instant == false)
		// starts at the top.
		const std::uint32_t prev_start = screen->m_page_start_index;

		// Remove any rows from a previous view/visit before building the new set.
		destroy_rows(screen);

		// Resolve the blank graphic lazily: the string-intern table is not ready at hook
		// registration time, so "Blank" only hashes correctly once the game is running.
		if (!g_blank_graphic && g_hash_lookup)
		{
			HashGuid res{};
			g_hash_lookup(&res, "Blank", 5);
			g_blank_graphic = res.m_id;
		}

		if (g_view == View::mod_settings && !g_view_stem.empty())
		{
			build_mod_settings(screen, g_view_stem, g_view_section.empty() ? root_section : g_view_section);
		}
		else
		{
			build_mod_list(screen);
		}

		// Let the engine position, paginate and drive the scrollbar/arrows for the rows.
		std::uint32_t start = 0;
		if (instant)
		{
			// Clamp the preserved offset in case the row count shrank (e.g. a row became
			// hidden), keeping a full page in view where possible.
			const std::uint32_t row_count = static_cast<std::uint32_t>(g_rows.size());
			const std::uint32_t max_start = row_count > rows_per_page ? row_count - rows_per_page : 0;
			start                         = prev_start > max_start ? max_start : prev_start;
		}
		screen->m_page_start_index = start;
		screen->m_options_per_page = rows_per_page;
		if (g_update_scroll)
		{
			g_update_scroll(screen); // sets each row's mFadeTarget: 1 on-page, 0 off-page
		}

		if (instant)
		{
			// In-place refresh (e.g. toggling the mod's "enabled" switch, which only changes greying):
			// snap each row straight to its final visibility so the panel does not flash a fade.
			for (const auto& row : g_rows)
			{
				if (row.component)
				{
					row.component->m_fade_opacity = row.component->m_fade_target;
				}
			}
		}
		// A view change leaves the freshly built rows at mFadeOpacity 0 (finalize_row); the native
		// ease (GUIComponent::Update) then fades the on-page rows in toward mFadeTarget == 1, matching
		// the game's own category-switch transition. Off-page rows are held transparent in
		// sync_scroll_fade.

		// Value displays are not laid out by the scroll pass; place them on their key rows now.
		sync_value_columns();
	}

	// Applies a queued navigation (mod list <-> a mod's settings) by rebuilding the panel.
	// Called from the Update hook, i.e. outside click/input iteration, where mutating the
	// component vectors is safe. A rebuild that stays on the same view/mod (e.g. after
	// toggling "enabled") is applied instantly to avoid a fade flash; a real view change
	// keeps the fade-in.
	static void apply_nav(MiscSettingsScreen* screen)
	{
		// A Reset forces a top (non-instant) rebuild even though the view is unchanged, so the
		// restored rows and the scrollbar stay in sync - an in-place rebuild that preserves a
		// scrolled position would leave the stale page-1 rows visible (see the scroll-model notes).
		const bool instant = !g_nav_reset_to_top && (g_pending_view == g_view) && (g_pending_stem == g_view_stem) && (g_pending_section == g_view_section);
		g_nav_reset_to_top = false;

		g_view         = g_pending_view;
		g_view_stem    = g_pending_stem;
		g_view_section = g_pending_section;
		build_panel(screen, instant);
	}

	// Restores the current mod's config entries (g_view_stem) to their config.lua defaults (see
	// get_setting_default), saving each change and flagging any restart-required ones. Only ever
	// resets the one mod whose settings are open - never every mod - so it is called only from the
	// mod-settings view. Only settings bound via rom.mod_settings.load carry a captured default;
	// anything else (raw rom.config or big::config) is left untouched. Returns true if any value
	// actually changed.
	static bool reset_settings_to_defaults()
	{
		bool any_changed = false;
		for (auto* cfg : toml_v2::config_file::g_config_files)
		{
			if (!cfg || cfg->m_config_file_stem_as_str.empty() || cfg->m_config_file_stem_as_str != g_view_stem)
			{
				continue;
			}
			const std::string& guid = cfg->m_config_file_stem_as_str;
			for (auto& [def, entry] : cfg->m_entries)
			{
				if (!entry)
				{
					continue;
				}
				auto* e = entry.get();

				const auto def_val = get_setting_default(guid, def.m_section, def.m_key);
				if (!def_val || e->get_serialized_value() == *def_val)
				{
					continue;
				}
				capture_restart_baseline(e);
				e->set_serialized_value(*def_val); // auto-saves + fires on_setting_changed
				note_change_if_restart_required(e, e->get_serialized_value());
				any_changed = true;
			}
		}
		return any_changed;
	}

	// Handles a Reset activation on the Mods tab: restores the in-scope settings to their config.lua
	// defaults, then (in a mod's settings view, where the changed values are on screen) queues a top
	// rebuild so the widgets show the restored values. Safe to call from input/click context because
	// the rebuild is deferred to the Update hook.
	static void perform_reset()
	{
		const bool changed = reset_settings_to_defaults();
		if (changed && g_view == View::mod_settings)
		{
			g_pending_view     = g_view;
			g_pending_stem     = g_view_stem;
			g_pending_section  = g_view_section;
			g_nav_pending      = true;
			g_nav_reset_to_top = true;
		}
	}

	// Builds the restart-popup body text from the changes collected this session. Blank lines are a
	// single non-breaking space (U+00A0): ShowText trims ASCII-whitespace-only lines (so "\n\n" and
	// "\n \n" collapse) but keeps an nbsp line. A sacrificial trailing nbsp line is appended because
	// the formatter also trims the LAST whitespace-only line, which would otherwise merge the blank
	// before the outro into it. Intro/outro and each change entry are non-breaking so the
	// width-greedy formatter keeps each on one line.
	static std::string build_restart_message()
	{
		const std::string blank = "\xC2\xA0"; // nbsp: a whitespace line ShowText will not trim

		std::string msg  = to_non_breaking("A restart is required because you changed these settings:");
		msg             += "\n" + blank + "\n";
		for (const auto& change : g_restart_changes)
		{
			msg += change.second;
			msg += "\n";
		}
		msg += blank + "\n";
		msg += to_non_breaking("The game will now close. Please restart it to apply the changes.");
		msg += "\n" + blank; // sacrificial trailing blank so the one above the outro survives
		return msg;
	}

	// Builds an empty EASTL SSO string (24-byte layout) in `buf` (>=24 bytes). Passed to the
	// dialog ctor (message) and AddScreen (name); the real message is applied afterwards via
	// ShowText. Layout: bytes[0..]=chars, byte[23]=remaining-capacity marker (23 - length).
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

	// Shows the native single-button "restart required" message box (sgg::MessageDialog, the same
	// box the game uses in the main menu for save/file errors). `message` is shown as the body
	// text. Its only button closes the game (handled in the OnClicked hook) - a restart-required
	// change must not be cancellable, since cancelling would have to undo the change. Returns true
	// if the native dialog was shown; otherwise falls back to a MessageBox (OK closes the game).
	static bool show_restart_dialog(void* screen_manager, const std::string& message)
	{
		if (screen_manager && g_message_dialog_ctor && g_add_screen)
		{
			void* dialog = _aligned_malloc(message_dialog_size, 8);
			if (dialog)
			{
				std::memset(dialog, 0, message_dialog_size);

				// The ctor builds every component (single button + text) and loads
				// GUI/MessageDialog.sjson. Pass an empty message; the real (multi-line) text is
				// applied below via ShowText so it need not be an eastl heap string.
				char empty_message[24];
				make_eastl_sso(empty_message, "");
				g_message_dialog_ctor(dialog, screen_manager, empty_message);

				auto* bytes = reinterpret_cast<char*>(dialog);

				// Ensure the dialog is visible and modal over the options screen.
				bytes[screen_removed_offset]     = 0;
				bytes[screen_visible_offset]     = 1;
				bytes[screen_block_input_offset] = 1;

				// Set the title + body (raw text; the body carries the restart-causing settings).
				if (g_show_text)
				{
					if (auto* title_box = *reinterpret_cast<void**>(bytes + dialog_title_offset))
					{
						g_show_text(title_box, "Restart Required");
					}
					if (auto* message_box = *reinterpret_cast<GUIComponent**>(bytes + dialog_message_offset))
					{
						// Shrink the body font: the sjson template renders at size 26; scale the
						// live font handle's size ratios down before ShowText lays out the lines
						// (the def's mFontSize is ignored once the template is loaded).
						char* handle = reinterpret_cast<char*>(message_box) + textbox_font_handle_offset;
						*reinterpret_cast<float*>(handle + font_handle_size_ratio_offset) *= restart_message_font_scale;
						*reinterpret_cast<float*>(handle + font_handle_eng_size_ratio_offset) *= restart_message_font_scale;
						// Escape markup so a path value (e.g. hadesGameFolder) with '\' or brackets in
						// the changed-settings list renders verbatim (see escape_markup).
						const std::string shown = escape_markup(message);
						g_show_text(message_box, shown.c_str());
					}
				}

				// Capture the confirm button so the OnClicked hook closes the game on press.
				g_restart_confirm_button = *reinterpret_cast<GUIComponent**>(bytes + dialog_confirm_button_offset);

				// Add at the END of the screen list so it draws on top of the options menu.
				char empty_name[24];
				make_eastl_sso(empty_name, "");
				g_add_screen(screen_manager, dialog, true, empty_name);
				return true;
			}
		}

		MessageBoxW(nullptr, L"A changed mod setting requires a restart. The game will now close - please restart it.", L"Hell2Modding - Restart Required", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
		TerminateProcess(GetCurrentProcess(), 0);
		return false;
	}

	static void* hook_MiscSettingsScreen_ctor(void* self, void* screen_manager, void* opened_from, void* profile_name)
	{
		// Reset state BEFORE running the original ctor: the original ctor immediately shows
		// the last-viewed category, and if that is the Mods tab it builds our panel via
		// DoShowCategory. Clearing g_rows after the original would wipe those fresh rows.
		g_rows.clear();
		g_view = View::mod_list;
		g_view_stem.clear();
		g_view_section.clear();
		g_pending_section.clear();
		g_nav_pending            = false;
		g_nav_reset_to_top       = false;
		g_restart_required       = false;
		g_restart_prompt_shown   = false;
		g_restart_confirm_button = nullptr;
		g_restart_changes.clear();
		g_restart_baselines.clear();
		g_last_description_component = nullptr;
		g_prompt_confirm_label.clear();
		g_prompt_cancel_label.clear();
		exit_edit_mode();

		// The engine constructor returns `this`; forward it unchanged.
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

		auto* result = big::g_hooking->get_original<hook_MiscSettingsScreen_DoShowCategory>()(self, category_button, category_flag);

		show_mods_tab(screen);

		if (is_mods_tab)
		{
			// Entering the tab always starts at the mod list; drill-down happens in-place
			// via the Update hook, not by re-entering the category.
			g_view = View::mod_list;
			g_view_stem.clear();
			g_view_section.clear();
			g_nav_pending = false;
			exit_edit_mode();
			build_panel(screen);
		}

		return result;
	}

	// Value-change hook for our native number-box rows. GUIComponentNumBox::SetNumberValue is called
	// (with notify=true) on every user step - left/right, arrow click, keyboard or controller. We run
	// the original first (it clamps to [min,max], refreshes the value text, updates arrow visibility),
	// then, if `this` is one of our rows, persist the post-clamp value to the config entry and run the
	// restart-required tracking. `notify` is false only for our own initial paint in make_numbox_row,
	// so filtering on it keeps that from being recorded as a change. This fires for native settings
	// num-boxes too, hence the `find_row` filter.
	static void hook_GUIComponentNumBox_SetNumberValue(void* self, float value, bool notify)
	{
		big::g_hooking->get_original<hook_GUIComponentNumBox_SetNumberValue>()(self, value, notify);

		if (!notify || !self)
		{
			return;
		}

		PanelRow* row = find_row(reinterpret_cast<GUIComponent*>(self));
		if (!row || !row->entry)
		{
			return;
		}

		// Enum cycler: the box tracks the option index; persist the matching serialized value and
		// replace the raw index the original just painted with the option's label.
		if (row->is_enum)
		{
			int idx = static_cast<int>(*reinterpret_cast<float*>(reinterpret_cast<char*>(self) + numbox_value_offset));
			if (idx < 0 || idx >= static_cast<int>(row->enum_values.size()))
			{
				return;
			}
			set_numbox_value_text(reinterpret_cast<GUIComponent*>(self), row->enum_labels[idx].c_str());

			const std::string& serialized = row->enum_values[idx];
			if (row->entry->get_serialized_value() != serialized)
			{
				capture_restart_baseline(row->entry);
				row->entry->set_serialized_value(serialized); // auto-saves via on_setting_changed
				note_change_if_restart_required(row->entry, row->enum_labels[idx]);
			}
			return;
		}

		if (!row->is_stepper)
		{
			return;
		}

		const double new_value = static_cast<double>(*reinterpret_cast<float*>(reinterpret_cast<char*>(self) + numbox_value_offset));
		if (row->entry->get_value_base<double>() == new_value)
		{
			return;
		}

		capture_restart_baseline(row->entry);
		row->entry->set_value_base<double>(new_value); // auto-saves via on_setting_changed
		note_change_if_restart_required(row->entry, row->entry->get_serialized_value());
	}

	// Button-click hook. GUIComponentButton overrides GUIComponent::OnClicked (vtable slot
	// +0x100, the engine's terminal-click), so this is where our button rows' clicks land.
	// For our rows the engine returns false (they have no bound activate function) but still
	// plays the press sound, so we must match the row regardless of the return value. The
	// actual panel rebuild is deferred to the Update hook, where mutating the component
	// vectors is safe (this runs mid input iteration).
	static bool hook_GUIComponentButton_OnClicked(GUIComponent* self, std::uint64_t location)
	{
		// Clicking the restart message box's button closes the game (forced restart).
		if (self && self == g_restart_confirm_button)
		{
			big::g_hooking->get_original<hook_GUIComponentButton_OnClicked>()(self, location);
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
				// Boolean settings toggle in place; other types open a freetext editor. Number-box
				// (stepper) rows are GUIComponentNumBox, not buttons, so their clicks never reach
				// this hook - the num-box handles its own arrow clicks and left/right natively.
				if (entry && entry->type() == typeid(bool))
				{
					// Capture the session baseline before the first write so a later revert to
					// it (toggling off then on again) is recognised as "no net change".
					capture_restart_baseline(entry);

					const bool new_value = !entry->get_value_base<bool>();
					entry->set_value_base<bool>(new_value);
					set_toggle_graphic(self, new_value);

					// If the author declared this setting restart-required, flag/clear the
					// restart and record the change so the popup can list what forced it.
					note_change_if_restart_required(entry, new_value ? "on" : "off");

					// Toggling the mod's master "enabled" switch changes which other rows
					// are greyed out, so rebuild the settings view on the next Update.
					if (matched_row.is_enabled_toggle)
					{
						g_pending_view    = View::mod_settings;
						g_pending_stem    = matched_row.stem;
						g_pending_section = g_view_section;
						g_nav_pending     = true;
					}
				}
				else if (entry)
				{
					enter_edit_mode(matched_row.value_component, entry);
				}
				break;
			}
			case RowKind::action:
				// TODO: dispatch the action row's callback.
				break;
			}
		}

		return result;
	}

	// Per-frame screen update: RCX=this, XMM1=dt (float), R8=input. We apply any queued
	// navigation here because the click/input iteration has fully unwound by now, so
	// tearing down and rebuilding the component vectors is safe. We rebuild before the
	// original runs so this frame lays out and hover-resolves the new rows.
	static void* hook_MiscSettingsScreen_Update(void* self, float dt, void* input)
	{
		auto* screen = static_cast<MiscSettingsScreen*>(self);
		const bool on_mods_tab = screen->m_current_category_button == reinterpret_cast<GUIComponent*>(screen->m_editor_options_button);

		// Freetext editing: refresh the edited row's live label. Confirm/cancel is handled in
		// the HandleInput hook so the submitting key/click is swallowed on the same frame.
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

		if (g_nav_pending)
		{
			// Only act while this screen is actually showing the Mods tab.
			if (on_mods_tab)
			{
				apply_nav(screen);
			}
			g_nav_pending = false;
		}

		void* result = big::g_hooking->get_original<hook_MiscSettingsScreen_Update>()(self, dt, input);

		// The original just laid out the key rows for this frame; mirror the value columns
		// onto them so the right column tracks scrolling and fade, and show the highlighted
		// row's description in the native description box.
		if (on_mods_tab)
		{
			sync_scroll_fade(screen);
			sync_value_columns();
			sync_description_box(screen);
		}

		// Retune the bottom prompt buttons per context (off the Mods tab this only clears our
		// caches and leaves the native prompts alone).
		sync_prompts(screen, on_mods_tab);

		return result;
	}

	// While a freetext setting is being edited, read Enter (confirm) and Escape (cancel)
	// from the game's own per-frame input, commit/cancel here, then swallow the screen's
	// input handling entirely so menu navigation and the Escape-to-close do not react.
	// Committing here (rather than in Update) is important: HandleInput returns true this
	// frame, so a submitting mouse click is swallowed and cannot also activate the row it
	// lands on. Returning true without calling the original bypasses the whole close chain
	// (the base MenuScreen::HandleInput is only reached via this function's tail-call).
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

		return big::g_hooking->get_original<hook_MiscSettingsScreen_HandleInput>()(self, input, x);
	}

	// Close funnel for the options screen: every way the user dismisses it (Escape key, controller
	// B, or clicking the on-screen "Exit" button) converges here (MiscSettingsScreen::ExitScreen,
	// vtable slot 7), before any fade/teardown and while mScreenManager is valid. If a restart is
	// required, show the native message box and DO NOT run the original (veto the close): the box
	// is modal over the still-open options screen and its button closes the game. A restart-required
	// change must not be cancellable (that would require undoing the change), so the restart is
	// forced. If the native dialog cannot be shown, the MessageBox fallback closes the game anyway.
	static void hook_MiscSettingsScreen_ExitScreen(void* self)
	{
		// Inside a mod's settings, Esc / controller B / the on-screen Back button steps up one level:
		// a nested group returns to its parent section, and the root returns to the mod list. Only the
		// mod-list view actually closes the options screen.
		auto* screen = static_cast<MiscSettingsScreen*>(self);
		const bool on_mods_tab = screen->m_current_category_button == reinterpret_cast<GUIComponent*>(screen->m_editor_options_button);
		if (on_mods_tab && g_view == View::mod_settings)
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
			return; // veto the close; apply_nav applies the new view next Update
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

		big::g_hooking->get_original<hook_MiscSettingsScreen_ExitScreen>()(self);
	}

	// Reset choke-point: sgg::MiscSettingsScreen::RestoreDefaults (virtual slot 21) is the single
	// handler for both the [I]/MenuInfo control and a mouse click on the on-screen Reset button. On
	// our Mods tab the native reset is a no-op (our rows' mDataValue is not a ConfigOptionsField key).
	// Inside a single mod's settings we run our own reset of that mod's config and still call the
	// original for the native confirm animation + sound and glyph refresh (on our tab it touches no
	// real game settings). In the mod list/overview we swallow it entirely: Reset is intentionally
	// unavailable there (its prompt is hidden too) so users can't reset every mod's config by mistake.
	static void hook_MiscSettingsScreen_RestoreDefaults(void* self)
	{
		auto* screen = static_cast<MiscSettingsScreen*>(self);
		if (screen->m_current_category_button == reinterpret_cast<GUIComponent*>(screen->m_editor_options_button))
		{
			if (g_view != View::mod_settings)
			{
				return; // reset is unavailable in the mod list; do nothing (and do not play the native reset)
			}
			perform_reset();
		}

		big::g_hooking->get_original<hook_MiscSettingsScreen_RestoreDefaults>()(self);
	}

	void register_hooks()
	{
		// Resolve every engine symbol, RVA and offset the Mods tab depends on up front. The symbol map
		// is built from the game's live PDB, so if the game updates and a required function moved or was
		// renamed it resolves to null here; likewise the hardcoded RVAs and struct offsets this feature
		// was reverse-engineered against only match one specific Ship build. If anything required is
		// missing we log exactly what and install NO hooks, so the tab is cleanly skipped instead of
		// crashing the game. The rom.mod_settings Lua config API is wired separately (bind_config_api)
		// and keeps working regardless, so mods can still author and read their config.
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
		const auto handle_input     = require("sgg::MiscSettingsScreen::HandleInput");
		const auto set_number_value = require("sgg::GUIComponentNumBox::SetNumberValue");

		// Engine helpers called while building and editing rows. A null call here would crash, so every
		// one is required. The button ctor doubles as the RVA anchor for the templated/overloaded
		// helpers resolved further down.
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

		// Optional helpers: every call site is null-guarded, so their absence only degrades a visual or
		// teardown detail (never crashes) and must not gate the feature.
		g_get_lines = big::hades2_symbol_to_address["sgg::GUIComponentTextBox::GetLines"].as_func<void*(void*)>();
		g_set_selected_texture = big::hades2_symbol_to_address["sgg::GUIComponentButton::SetSelectedTexture"].as_func<void(void*, std::uint32_t)>();
		g_button_dtor = big::hades2_symbol_to_address["sgg::GUIComponentButton::~GUIComponentButton"].as_func<void(void*)>();
		g_disable = big::hades2_symbol_to_address["sgg::GUIComponentButton::Disable"].as_func<void(void*)>();

		// The num-box factory (a template instantiation) and the restart-dialog ctor / AddScreen
		// overloads cannot be picked by name from the PDB, so they are addressed by hardcoded RVA off
		// the button-ctor anchor. Those RVAs - and every struct offset this feature uses - are valid
		// only for the Ship build they were captured from. Fingerprint that build by checking the
		// anchor sits at its known module RVA (game base taken from the live process). A mismatch means
		// the game changed and our RVAs/offsets can no longer be trusted, so disable the whole tab.
		uintptr_t game_base   = 0;
		std::size_t game_size = 0;
		::module_info_helper::get_module_base_and_size(&game_base, &game_size, nullptr);
		const bool build_matches = anchor && game_base && (anchor.as<uintptr_t>() - game_base == anchor_rva);

		// push_back is a named PDB symbol but is occasionally emitted inline; fall back to its RVA.
		if (!g_push_back && build_matches)
		{
			g_push_back = reinterpret_cast<push_back_fn>(anchor.as<uintptr_t>() - anchor_rva + push_back_rva);
		}
		if (!g_push_back)
		{
			missing.push_back("eastl::vector<sgg::GUIComponent *,eastl::allocator_forge>::push_back");
		}

		if (!missing.empty() || !build_matches)
		{
			std::string detail;
			for (const auto* name : missing)
			{
				detail += "\n    - missing symbol: ";
				detail += name;
			}
			if (!build_matches)
			{
				detail += "\n    - build fingerprint mismatch (button ctor not at the expected RVA; game updated?)";
			}
			LOG(WARNING) << "[mod_settings] Mods options tab disabled for this game build; the in-game mod-settings "
			                "editor is skipped. The rom.mod_settings Lua config API is unaffected."
			             << detail;
			return;
		}

		// Build verified and every required symbol resolved: derive the RVA-relative helpers and hook.
		const auto anchor_base = anchor.as<uintptr_t>() - anchor_rva;
		g_message_dialog_ctor  = reinterpret_cast<message_dialog_ctor_fn>(anchor_base + message_dialog_ctor_rva);
		g_add_screen           = reinterpret_cast<add_screen_fn>(anchor_base + add_screen_rva);
		g_numbox_factory       = reinterpret_cast<numbox_factory_fn>(anchor_base + numbox_factory_rva);

		g_feature_enabled = true;

		static auto ctor_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_ctor>(
		    "sgg::MiscSettingsScreen::MiscSettingsScreen",
		    ctor);
		static auto category_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_DoShowCategory>(
		    "sgg::MiscSettingsScreen::DoShowCategory",
		    do_show_category);

		// All required by the checks above, so install unconditionally. OnClicked and SetNumberValue are
		// global (they fire for every button / num-box in the game); their callbacks filter to our rows
		// via find_row, so installing them is a no-op for the rest of the game's UI.
		static auto onclick_hook = hooking::detour_hook_helper::add_queue<hook_GUIComponentButton_OnClicked>(
		    "sgg::GUIComponentButton::OnClicked",
		    on_clicked);
		static auto snv_hook = hooking::detour_hook_helper::add_queue<hook_GUIComponentNumBox_SetNumberValue>(
		    "sgg::GUIComponentNumBox::SetNumberValue",
		    set_number_value);
		static auto update_hook =
		    hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_Update>("sgg::MiscSettingsScreen::Update", update);
		static auto handle_input_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_HandleInput>(
		    "sgg::MiscSettingsScreen::HandleInput",
		    handle_input);

		// Every close path (Escape key, controller B, clicking the on-screen Exit button) funnels
		// through ExitScreen, so this is where the restart-required prompt is triggered.
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

		// Optional: the on-screen "Reset" button ([I]/MenuInfo control or mouse) funnels through
		// RestoreDefaults. Without it the Mods tab still works; Reset just won't restore mod defaults.
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
} // namespace big::mod_settings
