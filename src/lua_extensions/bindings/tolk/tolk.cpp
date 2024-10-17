#include "tolk.hpp"

#include "Tolk.h"

#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <lua_extensions/bindings/hades/hades_ida.hpp>
#include <lua_extensions/lua_module_ext.hpp>
#include <memory/gm_address.hpp>
#include <string/string.hpp>

namespace lua::tolk
{
	static std::string wstring_to_utf8(const std::wstring& wstr)
	{
		int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (utf8_length == 0)
		{
			// Failed to get UTF-8 length
			// You might want to handle this error case appropriately
			return "";
		}

		std::string utf8_str(utf8_length, 0);
		WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8_str[0], utf8_length, nullptr, nullptr);

		// Remove the null-terminator that WideCharToMultiByte added
		utf8_str.pop_back();

		return utf8_str;
	}

	static std::wstring utf8_to_wstring(const std::string& utf8_str)
	{
		int wstr_length = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, nullptr, 0);
		if (wstr_length == 0)
		{
			// Failed to get wide string length
			// You might want to handle this error case appropriately
			return L"";
		}

		std::wstring wstr(wstr_length, 0);
		MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, &wstr[0], wstr_length);

		// Remove the null-terminator that MultiByteToWideChar added
		wstr.pop_back();

		return wstr;
	}

	// Lua API: Function
	// Table: tolk
	// Name: silence
	// Silences the screen reader.
	static void silence()
	{
		if (Tolk_IsLoaded())
		{
			Tolk_Silence();
		}
	}

	// Lua API: Function
	// Table: tolk
	// Name: output
	// Param: str: string: The text to output.
	// Outputs text through the current screen reader driver.
	static void output(const std::string& str)
	{
		if (Tolk_IsLoaded())
		{
			Tolk_Output(utf8_to_wstring(str).c_str());
		}
	}

	// Lua API: Function
	// Table: tolk
	// Name: screen_read
	// Feeds to tolk the text from all currently visible game gui components.
	static void screen_read()
	{
		std::vector<std::pair<GUIComponentTextBox*, Vectormath::Vector2>> sorted_components;

		static auto gui_comp_get_location = big::hades2_symbol_to_address["sgg::GUIComponent::GetLocation"].as_func<Vectormath::Vector2*(GUIComponentTextBox*, Vectormath::Vector2*)>();
		for (auto* gui_comp : g_GUIComponentTextBoxes)
		{
			Vectormath::Vector2 loc;
			gui_comp_get_location(gui_comp, &loc);
			sorted_components.push_back({gui_comp, loc});
		}

		std::sort(sorted_components.begin(),
		          sorted_components.end(),
		          [](std::pair<GUIComponentTextBox*, Vectormath::Vector2>& a, std::pair<GUIComponentTextBox*, Vectormath::Vector2>& b)
		          {
			          if (std::abs(a.second.mY - b.second.mY) < 5)
			          {
				          return a.second.mX < b.second.mX;
			          }
			          else
			          {
				          return a.second.mY < b.second.mY;
			          }
		          });

		std::unordered_set<std::string> already_printed;
		std::stringstream text_buffer;
		for (auto& [gui_comp, gui_comp_loc] : sorted_components)
		{
			if (gui_comp->vtbl->ShouldDraw(gui_comp))
			{
				for (auto i = gui_comp->mLines.mpBegin; i < gui_comp->mLines.mpEnd; i++)
				{
					if (i->mText.size() && !already_printed.contains(i->mText.c_str()))
					{
						already_printed.insert(i->mText.c_str());
						//LOG(INFO) << text << " (" << gui_comp_loc.mX << ", " << gui_comp_loc.mY << ")";
						text_buffer << i->mText.c_str() << "\n";
					}
				}
			}
		}
		if (text_buffer.str().size())
		{
			LOG(INFO) << text_buffer.str();

			const auto text_buffer_wide = utf8_to_wstring(text_buffer.str());
			if (!Tolk_Output(text_buffer_wide.c_str()))
			{
				LOG(ERROR) << "Failed to output to tolk";
			}
		}
	}

	static void* sgg_world_ptr = nullptr;

	namespace sgg
	{
		// These offsets can easily be found again
		// if they ever change by looking at the body of
		// sgg::GUIComponentTextBox::Draw(sgg::GUIComponentTextBox *this)
		struct TextComponent
		{
			char m_pad[0x30];

			eastl::vector<GUIComponentTextBox*> mTextBoxes;
		};

		static_assert(offsetof(TextComponent, mTextBoxes) == 0x30);

		struct Thing
		{
			char m_pad[0xF8];
			TextComponent* pText;
		};

		static_assert(offsetof(Thing, pText) == 0xF8);

	} // namespace sgg

	// Lua API: Function
	// Table: tolk
	// Name: get_lines_from_thing
	// Param: thing_id: integer: Id of the sgg::Thing.
	// Returns: table<int, string>: Returns the lines inside a lua table
	static std::vector<std::string> get_lines_from_thing(int thing_id)
	{
		std::vector<std::string> res;

		static auto get_active_thing = big::hades2_symbol_to_address["sgg::World::GetActiveThing"].as_func<sgg::Thing*(void* this_, int id)>();
		sgg::Thing* active_thing = get_active_thing(sgg_world_ptr, thing_id);
		if (active_thing && active_thing->pText)
		{
			for (auto i = active_thing->pText->mTextBoxes.begin(); i != active_thing->pText->mTextBoxes.end(); i++)
			{
				for (auto j = (*i)->mLines.mpBegin; j < (*i)->mLines.mpEnd; j++)
				{
					res.push_back(j->mText.c_str());
				}
			}
		}

		return res;
	}

	// Lua API: Function
	// Table: tolk
	// Name: on_button_hover
	// Param: function: function: Function called when a button is hovered. The function must match signature: (table of string) -> returns nothing
	// **Example Usage:**
	//
	// ```lua
	// tolk.on_button_hover(function(lines)
	//     tolk.silence()
	//     for i = 1, #lines do
	//         tolk.output(lines[i])
	//     end
	// end)
	// ```
	static void on_button_hover(sol::protected_function f, sol::this_environment env)
	{
		auto mod = (big::lua_module_ext*)big::lua_module::this_from(env);
		if (mod)
		{
			mod->m_data_ext.m_on_button_hover.push_back(f);
		}
	}

	static void* hook_GetActiveThing(void* this_, int thing_id)
	{
		sgg_world_ptr = this_;

		const auto res = big::g_hooking->get_original<hook_GetActiveThing>()(this_, thing_id);

		return res;
	}

	void bind(sol::table& state)
	{
		if (!Tolk_IsLoaded())
		{
			Tolk_TrySAPI(true);

			Tolk_Load();

			const wchar_t* name = Tolk_DetectScreenReader();
			if (name)
			{
				LOG(INFO) << "The active screen reader driver is: " << wstring_to_utf8(name);
			}
			else
			{
				LOG(INFO) << "None of the supported screen readers is running";
			}
		}

		auto ns = state.create_named("tolk");
		ns.set_function("screen_read", screen_read);
		ns.set_function("silence", silence);
		ns.set_function("output", output);
		ns.set_function("on_button_hover", on_button_hover);

		static auto GetActiveThing_hook_obj =
		    big::hooking::detour_hook_helper::add_now<hook_GetActiveThing>("hook_GetActiveThing", big::hades2_symbol_to_address["GetActiveThing"]);

		ns.set_function("get_lines_from_thing", get_lines_from_thing);
	}
} // namespace lua::tolk
