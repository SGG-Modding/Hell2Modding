#include "mod_settings.hpp"

#include "sgg_gui.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <gui/renderer.hpp>
#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <malloc.h>
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
	static constexpr std::size_t def_spacing = 0x1'5C; // mSpacing (float) row pitch, read by UpdateScrollState

	using ctor_fn            = void* (*)(void* button, void* owner_screen);
	using push_back_fn       = void (*)(void* vector, GUIComponent** value);
	using apply_data_fn      = void (*)(void* menu_screen, GUIComponent* component);
	using set_label_fn       = void (*)(void* button, const char* text);
	using update_scroll_fn   = void (*)(void* misc_settings_screen);
	using set_animation_fn   = void (*)(void* button, std::uint32_t graphic_id);
	using setup_component_fn = void (*)(void* component, void* component_data);
	using set_texture_fn     = void (*)(void* button, std::uint32_t graphic_id, bool reset);
	using set_sel_texture_fn = void (*)(void* button, std::uint32_t graphic_id);
	using disable_fn         = void (*)(void* button);
	using was_key_pressed_fn = bool (*)(void* input_handler, int keyboard_button_id);
	using dtor_fn            = void (*)(void* button);

	// sgg::HashGuid is a 32-bit interned-string id in its first field.
	struct HashGuid
	{
		std::uint32_t m_id;
	};

	using hash_lookup_fn = HashGuid* (*)(HashGuid * out, const char* str, std::size_t len);

	static ctor_fn g_button_ctor                     = nullptr;
	static push_back_fn g_push_back                  = nullptr;
	static apply_data_fn g_apply_data                = nullptr;
	static set_label_fn g_set_label                  = nullptr;
	static update_scroll_fn g_update_scroll          = nullptr;
	static set_animation_fn g_set_animation          = nullptr;
	static hash_lookup_fn g_hash_lookup              = nullptr;
	static setup_component_fn g_setup_component      = nullptr;
	static set_texture_fn g_set_normal_texture       = nullptr;
	static set_sel_texture_fn g_set_selected_texture = nullptr;
	static dtor_fn g_button_dtor                     = nullptr;
	static disable_fn g_disable                      = nullptr;
	static was_key_pressed_fn g_was_key_pressed      = nullptr;

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
	static constexpr float row_location_x        = 1560.0f; // component X (right pane), like OptionToggleButton
	static constexpr float row_text_offset_x     = -900.0f; // left-justify the label to the option-name column
	static constexpr float button_center_x       = 1130.0f; // centered action button X (clear of the scrollbar)
	static constexpr float row_base_y            = 315.0f;  // first row's Y (aligns with the tab column)
	static constexpr float row_pitch             = 54.0f;   // vertical distance between rows
	static constexpr std::uint32_t rows_per_page = 8;

	// What a panel row represents, so a click can be routed to the right action.
	enum class RowKind
	{
		mod_entry, // opens that mod's settings
		back,      // returns to the mod list
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
	};

	static std::vector<PanelRow> g_rows;

	// Which view the Mods panel is currently showing, plus a deferred navigation request
	// that a click sets and the Update hook applies at a safe point (outside input/click
	// iteration, where mutating the component vectors is safe).
	enum class View
	{
		mod_list,
		mod_settings,
	};

	static View g_view = View::mod_list;
	static std::string g_view_stem; // mod whose settings are shown (mod_settings view)
	static bool g_nav_pending  = false;
	static View g_pending_view = View::mod_list;
	static std::string g_pending_stem;

	// Freetext edit state (number/string settings). A click enters edit mode; typed input
	// is captured in the window procedure and applied on the game thread in the Update hook.
	static bool g_editing                                        = false;
	static GUIComponent* g_edit_component                        = nullptr;
	static toml_v2::config_file::config_entry_base* g_edit_entry = nullptr;
	static std::string g_edit_key;
	static std::string g_edit_buffer;
	static bool g_edit_numeric = false; // restrict input to a numeric literal
	static bool g_edit_confirm = false;
	static bool g_edit_cancel  = false;

	// Turns a config-file stem ("AuthorName-ModName") into a display name: drops the
	// author (up to the first '-') and shows the mod name with '_' replaced by spaces.
	// "SGG_Modding-Chalk" -> "Chalk"; "NikkelM-Zagreus_Journey" -> "Zagreus Journey".
	static std::string display_name_from_stem(const std::string& stem)
	{
		const auto dash  = stem.find('-');
		std::string name = (dash == std::string::npos) ? stem : stem.substr(dash + 1);
		std::replace(name.begin(), name.end(), '_', ' ');
		return name;
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
	static void finalize_row(MiscSettingsScreen* screen, GUIComponent* row)
	{
		GUIComponent* value = row;
		auto* menu          = reinterpret_cast<MenuScreen*>(screen);
		g_push_back(&menu->m_components, &value);
		g_push_back(&screen->m_options, &value);

		row->m_location_x   = row_location_x;
		row->m_fade_opacity = 0.0f;
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

		for (const auto& row : g_rows)
		{
			GUIComponent* comp = row.component;
			if (!comp)
			{
				continue;
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

			vector_erase(menu->m_components, comp);
			vector_erase(screen->m_options, comp);

			if (g_button_dtor)
			{
				g_button_dtor(comp);
			}
			_aligned_free(comp);
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
			if (auto* row = make_text_row(screen, display.c_str()))
			{
				g_rows.push_back({row, RowKind::mod_entry, stem, {}});
			}
		}
	}

	// Setting key as a display string (underscores become spaces).
	static std::string key_to_display(const std::string& key)
	{
		std::string display = key;
		std::replace(display.begin(), display.end(), '_', ' ');
		return display;
	}

	// "Key : value" label for a non-toggle setting row.
	static std::string setting_label(const std::string& key, toml_v2::config_file::config_entry_base* entry)
	{
		return key_to_display(key) + " : " + (entry ? entry->get_serialized_value() : std::string{});
	}

	// Accepts a character into a numeric edit buffer only if the result stays a plausible
	// numeric literal: an optional leading sign, digits, at most one decimal point.
	static bool numeric_char_ok(const std::string& buffer, char c)
	{
		if (c >= '0' && c <= '9')
		{
			return true;
		}
		if (c == '-' || c == '+')
		{
			return buffer.empty(); // sign only as the first character
		}
		if (c == '.')
		{
			return buffer.find('.') == std::string::npos; // a single decimal point
		}
		return false;
	}

	// Window-procedure callback: while a freetext setting is being edited, capture typed
	// characters into the edit buffer. Runs on the game's message-pump thread (same thread
	// as Update). Printable characters arrive via WM_CHAR; Backspace via WM_KEYDOWN; a mouse
	// click anywhere commits the edit (Enter/Escape are read from the game input in the
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

		if (msg == WM_KEYDOWN)
		{
			// Only editing keys (Backspace) are handled here; Enter/Escape come from HandleInput.
			if (wparam == VK_BACK && !g_edit_buffer.empty())
			{
				g_edit_buffer.pop_back();
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
			if (g_edit_numeric && !numeric_char_ok(g_edit_buffer, ch))
			{
				return;
			}
			g_edit_buffer.push_back(ch);
		}
	}

	// Registers on_wndproc with the framework's window hook the first time it is needed.
	static void ensure_wndproc_registered()
	{
		static bool registered = false;
		if (registered || !g_renderer)
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

	static void enter_edit_mode(GUIComponent* component, toml_v2::config_file::config_entry_base* entry, const std::string& key)
	{
		ensure_wndproc_registered();
		g_editing        = true;
		g_edit_component = component;
		g_edit_entry     = entry;
		g_edit_key       = key;
		g_edit_buffer    = entry ? entry->get_serialized_value() : std::string{};
		g_edit_numeric   = entry && entry->type() != typeid(std::string);
		g_edit_confirm   = false;
		g_edit_cancel    = false;
	}

	static void exit_edit_mode()
	{
		g_editing        = false;
		g_edit_component = nullptr;
		g_edit_entry     = nullptr;
		g_edit_confirm   = false;
		g_edit_cancel    = false;
	}

	// Requests an in-place rebuild of the current settings view (to reflect a committed or
	// reverted edit) on the next Update.
	static void request_settings_rebuild()
	{
		g_pending_view = g_view;
		g_pending_stem = g_view_stem;
		g_nav_pending  = true;
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
				// set_serialized_value validates (e.g. numbers) and only stores/saves a
				// valid value, so bad input for a number simply keeps the old value.
				g_edit_entry->set_serialized_value(g_edit_buffer);
			}
			exit_edit_mode();
			request_settings_rebuild();
			return true;
		}
		if (g_edit_cancel)
		{
			exit_edit_mode();
			request_settings_rebuild();
			return true;
		}
		return false;
	}

	// Live-updates the edited row's label with a trailing cursor. Called from Update while
	// editing is still active.
	static void update_edit_label()
	{
		if (g_edit_component && g_set_label)
		{
			const std::string label = key_to_display(g_edit_key) + " : " + g_edit_buffer + "|";
			g_set_label(g_edit_component, label.c_str());
		}
	}

	// True if `key` is the mod's master enable switch ("enabled", any case).
	static bool is_enabled_key(const std::string& key)
	{
		return big::string::to_lower(key) == "enabled";
	}

	// Level 2: a Back row followed by one row per config entry belonging to `stem`. Boolean
	// entries render as native toggle rows; other types render as "key : value" text rows.
	// A boolean "enabled" entry (if present) is pinned to the top; when it is off, every
	// other setting is greyed out and made non-interactable.
	static void build_mod_settings(MiscSettingsScreen* screen, const std::string& stem)
	{
		if (auto* row = make_text_row(screen, "< Back"))
		{
			g_rows.push_back({row, RowKind::back, stem, {}});
		}

		// Gather this mod's entries, keeping map order, and locate the master "enabled" one.
		std::vector<std::pair<std::string, toml_v2::config_file::config_entry_base*>> entries; // (key, entry)
		toml_v2::config_file::config_entry_base* enabled_entry = nullptr;
		for (auto* cfg : toml_v2::config_file::g_config_files)
		{
			if (!cfg || cfg->m_config_file_stem_as_str != stem)
			{
				continue;
			}
			for (auto& [key, entry] : cfg->m_entries)
			{
				if (!entry)
				{
					continue;
				}
				entries.emplace_back(key.m_key, entry.get());
				if (!enabled_entry && entry->type() == typeid(bool) && is_enabled_key(key.m_key))
				{
					enabled_entry = entry.get();
				}
			}
		}

		// Pin the enabled entry to the top; the rest keep their order.
		std::stable_sort(entries.begin(),
		                 entries.end(),
		                 [&](const auto& a, const auto& b)
		                 {
			                 return (a.second == enabled_entry) && (b.second != enabled_entry);
		                 });

		const bool mod_enabled = !enabled_entry || enabled_entry->get_value_base<bool>();

		for (const auto& [key, entry] : entries)
		{
			const bool is_enabled_row = (entry == enabled_entry);
			const bool disabled       = !is_enabled_row && !mod_enabled;

			GUIComponent* row = nullptr;
			if (entry->type() == typeid(bool))
			{
				row = make_toggle_row(screen, key_to_display(key).c_str(), entry->get_value_base<bool>(), disabled);
			}
			else
			{
				row = make_text_row(screen, setting_label(key, entry).c_str(), disabled);
			}

			if (row)
			{
				PanelRow pr{row, RowKind::setting, stem, key, entry};
				pr.disabled          = disabled;
				pr.is_enabled_toggle = is_enabled_row;
				g_rows.push_back(pr);
			}
		}
	}

	static void build_panel(MiscSettingsScreen* screen, bool instant = false)
	{
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
			build_mod_settings(screen, g_view_stem);
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
			g_update_scroll(screen);
		}

		// For an in-place refresh (e.g. toggling the mod's "enabled" switch, which only
		// changes greying) snap each row straight to its final visibility so the panel does
		// not flash a fade-out/in. UpdateScrollState set the on-page rows' fade target to 1
		// and off-page rows' to 0, so copying target->opacity gives the settled look at once.
		if (instant)
		{
			for (const auto& row : g_rows)
			{
				if (row.component)
				{
					row.component->m_fade_opacity = row.component->m_fade_target;
				}
			}
		}
	}

	// Applies a queued navigation (mod list <-> a mod's settings) by rebuilding the panel.
	// Called from the Update hook, i.e. outside click/input iteration, where mutating the
	// component vectors is safe. A rebuild that stays on the same view/mod (e.g. after
	// toggling "enabled") is applied instantly to avoid a fade flash; a real view change
	// keeps the fade-in.
	static void apply_nav(MiscSettingsScreen* screen)
	{
		const bool instant = (g_pending_view == g_view) && (g_pending_stem == g_view_stem);

		g_view      = g_pending_view;
		g_view_stem = g_pending_stem;
		build_panel(screen, instant);
	}

	static void* hook_MiscSettingsScreen_ctor(void* self, void* screen_manager, void* opened_from, void* profile_name)
	{
		// Reset state BEFORE running the original ctor: the original ctor immediately shows
		// the last-viewed category, and if that is the Mods tab it builds our panel via
		// DoShowCategory. Clearing g_rows after the original would wipe those fresh rows.
		g_rows.clear();
		g_view = View::mod_list;
		g_view_stem.clear();
		g_nav_pending = false;
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
			g_nav_pending = false;
			exit_edit_mode();
			build_panel(screen);
		}

		return result;
	}

	// Button-click hook. GUIComponentButton overrides GUIComponent::OnClicked (vtable slot
	// +0x100, the engine's terminal-click), so this is where our button rows' clicks land.
	// For our rows the engine returns false (they have no bound activate function) but still
	// plays the press sound, so we must match the row regardless of the return value. The
	// actual panel rebuild is deferred to the Update hook, where mutating the component
	// vectors is safe (this runs mid input iteration).
	static bool hook_GUIComponentButton_OnClicked(GUIComponent* self, std::uint64_t location)
	{
		RowKind kind = RowKind::mod_entry;
		std::string stem;
		std::string setting_key;
		toml_v2::config_file::config_entry_base* entry = nullptr;
		bool matched                                   = false;
		bool disabled                                  = false;
		bool is_enabled_toggle                         = false;

		if (self)
		{
			for (const auto& row : g_rows)
			{
				if (row.component == self)
				{
					kind              = row.kind;
					stem              = row.stem;
					setting_key       = row.setting_key;
					entry             = row.entry;
					disabled          = row.disabled;
					is_enabled_toggle = row.is_enabled_toggle;
					matched           = true;
					break;
				}
			}
		}

		const bool result = big::g_hooking->get_original<hook_GUIComponentButton_OnClicked>()(self, location);

		if (matched && !disabled)
		{
			switch (kind)
			{
			case RowKind::mod_entry:
				g_pending_view = View::mod_settings;
				g_pending_stem = stem;
				g_nav_pending  = true;
				break;
			case RowKind::back:
				g_pending_view = View::mod_list;
				g_pending_stem.clear();
				g_nav_pending = true;
				break;
			case RowKind::setting:
				// Boolean settings toggle in place; other types open a freetext editor.
				if (entry && entry->type() == typeid(bool))
				{
					const bool new_value = !entry->get_value_base<bool>();
					entry->set_value_base<bool>(new_value);
					set_toggle_graphic(self, new_value);

					// Toggling the mod's master "enabled" switch changes which other rows
					// are greyed out, so rebuild the settings view on the next Update.
					if (is_enabled_toggle)
					{
						g_pending_view = View::mod_settings;
						g_pending_stem = stem;
						g_nav_pending  = true;
					}
				}
				else if (entry)
				{
					enter_edit_mode(self, entry, setting_key);
				}
				break;
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

		return big::g_hooking->get_original<hook_MiscSettingsScreen_Update>()(self, dt, input);
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

	void register_hooks()
	{
		const auto ctor             = big::hades2_symbol_to_address["sgg::MiscSettingsScreen::MiscSettingsScreen"];
		const auto do_show_category = big::hades2_symbol_to_address["sgg::MiscSettingsScreen::DoShowCategory"];

		if (!ctor || !do_show_category)
		{
			LOG(WARNING) << "[mod_settings] MiscSettingsScreen symbols not found; Mods options tab unavailable";
			return;
		}

		g_set_label = big::hades2_symbol_to_address["sgg::GUIComponentButton::SetDisplayName"].as_func<void(void*, const char*)>();
		g_button_ctor = big::hades2_symbol_to_address["sgg::GUIComponentButton::GUIComponentButton"].as_func<void*(void*, void*)>();
		g_apply_data = big::hades2_symbol_to_address["sgg::MenuScreen::ApplyDataToComponent"].as_func<void(void*, GUIComponent*)>();
		g_update_scroll = big::hades2_symbol_to_address["sgg::MiscSettingsScreen::UpdateScrollState"].as_func<void(void*)>();
		g_set_animation = big::hades2_symbol_to_address["sgg::GUIComponentButton::SetAnimation"].as_func<void(void*, std::uint32_t)>();
		g_hash_lookup = big::hades2_symbol_to_address["sgg::HashGuid::Lookup"].as_func<HashGuid*(HashGuid*, const char*, std::size_t)>();
		g_setup_component = big::hades2_symbol_to_address["sgg::ComponentData::SetupComponent"].as_func<void(void*, void*)>();
		g_set_normal_texture = big::hades2_symbol_to_address["sgg::GUIComponentButton::SetNormalTexture"].as_func<void(void*, std::uint32_t, bool)>();
		g_set_selected_texture = big::hades2_symbol_to_address["sgg::GUIComponentButton::SetSelectedTexture"].as_func<void(void*, std::uint32_t)>();
		g_button_dtor = big::hades2_symbol_to_address["sgg::GUIComponentButton::~GUIComponentButton"].as_func<void(void*)>();
		g_disable = big::hades2_symbol_to_address["sgg::GUIComponentButton::Disable"].as_func<void(void*)>();
		g_was_key_pressed = big::hades2_symbol_to_address["sgg::InputHandler::WasKeyPressed"].as_func<bool(void*, int)>();
		g_push_back =
		    big::hades2_symbol_to_address["eastl::vector<sgg::GUIComponent *,eastl::allocator_forge>::push_back"].as_func<void(void*, GUIComponent**)>();

		if (!g_push_back)
		{
			const auto anchor = big::hades2_symbol_to_address["sgg::GUIComponentButton::GUIComponentButton"];
			if (anchor)
			{
				g_push_back = reinterpret_cast<push_back_fn>(anchor.as<uintptr_t>() - 0x11'5c'70 + 0x14'1e'd0);
			}
		}

		if (!g_button_ctor || !g_push_back || !g_apply_data || !g_set_label || !g_setup_component)
		{
			LOG(WARNING) << "[mod_settings] engine row helpers unresolved (ctor=" << (g_button_ctor != nullptr) << " push_back=" << (g_push_back != nullptr) << " apply=" << (g_apply_data != nullptr) << " label=" << (g_set_label != nullptr) << " setup=" << (g_setup_component != nullptr) << ")";
		}

		static auto ctor_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_ctor>(
		    "sgg::MiscSettingsScreen::MiscSettingsScreen",
		    ctor);
		static auto category_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_DoShowCategory>(
		    "sgg::MiscSettingsScreen::DoShowCategory",
		    do_show_category);

		const auto on_clicked = big::hades2_symbol_to_address["sgg::GUIComponentButton::OnClicked"];
		if (on_clicked)
		{
			static auto onclick_hook = hooking::detour_hook_helper::add_queue<hook_GUIComponentButton_OnClicked>(
			    "sgg::GUIComponentButton::OnClicked",
			    on_clicked);
		}
		else
		{
			LOG(WARNING)
			    << "[mod_settings] sgg::GUIComponentButton::OnClicked not found; mod rows will not be clickable";
		}

		const auto update = big::hades2_symbol_to_address["sgg::MiscSettingsScreen::Update"];
		if (update)
		{
			static auto update_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_Update>(
			    "sgg::MiscSettingsScreen::Update",
			    update);
		}
		else
		{
			LOG(WARNING)
			    << "[mod_settings] sgg::MiscSettingsScreen::Update not found; mod-row navigation is unavailable";
		}

		const auto handle_input = big::hades2_symbol_to_address["sgg::MiscSettingsScreen::HandleInput"];
		if (handle_input)
		{
			static auto handle_input_hook = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_HandleInput>("sgg::MiscSettingsScreen::HandleInput", handle_input);
		}
		else
		{
			LOG(WARNING) << "[mod_settings] sgg::MiscSettingsScreen::HandleInput not found; freetext editing may not "
			                "block menu nav";
		}
	}
} // namespace big::mod_settings
