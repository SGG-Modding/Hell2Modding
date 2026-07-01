#pragma once

#include <cstring>
#include <EASTL/string.h>
#include <hades2/pdb_symbol_map.hpp>
#include <string>
#include <unordered_set>
#include <vector>

/*struct eastl_basic_string_view_char
{
	const char* mpBegin;
	size_t mnCount;
	char m_pad[7];
	char mRemainingSizeField;

	inline std::string get_text() const
	{
		__int64 mnRemainingSize  = mRemainingSizeField;
		const bool is_sso_string = (mnRemainingSize & 0x80u) == 0i64;
		if (is_sso_string)
		{
			const char* string_begin = (const char*)&mpBegin;
			const char* string_end   = (const char*)&mRemainingSizeField - mnRemainingSize;

			return std::string(string_begin, string_end);
		}
		else
		{
			return std::string(mpBegin, mpBegin + mnCount);
		}
	}
};

static_assert(offsetof(eastl_basic_string_view_char, mRemainingSizeField) == 0x17);*/

struct GUIComponentTextBox;

struct GUIComponentTextBox_Line
{
	bool mNewLine;
	bool mFormatBreak;
	float mColumnOffset;
	float mActualWidth;
	float mFixedLineSpacing;
	float mImageWidth;
	eastl::string mText;
	GUIComponentTextBox* mFormatter;
	GUIComponentTextBox* mGraftFormatter;
};

template<typename T>
struct eastl_vector
{
	T* mpBegin;
	T* mpEnd;
};

struct /*VFT*/ GUIComponentTextBox_vtbl
{
	bool(__fastcall* ShouldDraw)(void* this_);
	void(__fastcall* Clear)(void* this_);
	bool(__fastcall* HasText)(void* this_);
};

// 00000000 struct __cppobj sgg::GUIComponentTextBox : sgg::GUIComponentBox // sizeof=0x6C0
struct GUIComponentTextBox
{
	GUIComponentTextBox_vtbl* vtbl;
	char m_pad[0x5'A0];
	eastl::string mStringBuilder;
	char m_pad2[70];
	eastl_vector<GUIComponentTextBox_Line> mLines;
};

//constexpr size_t verify_offset_manually_hover  = offsetof(GUIComponentTextBox, mStringBuilder);
//constexpr size_t verify_offset_manually_hover2 = 0x5'd0;
static_assert(offsetof(GUIComponentTextBox, mStringBuilder) == 0x5'A8);
static_assert(offsetof(GUIComponentTextBox, mLines) == 0x6'08);

inline std::mutex g_GUIComponentTextBoxes_mutex;
inline std::unordered_set<GUIComponentTextBox*> g_GUIComponentTextBoxes;

struct GUIComponentButton
{
	char m_pad[0x5'80];
	GUIComponentTextBox* mTextBox;
};

static_assert(offsetof(GUIComponentButton, mTextBox) == 0x5'80);

// mTextBox/mLines moved to different struct offsets on the older debug build; the ship offsets come
// from the struct defs above (via the static_asserts), the debug ones were read from its PDB.
inline GUIComponentTextBox* gui_button_textbox(GUIComponentButton* button)
{
	const size_t offset = big::hades2_is_debug_build() ? 0x5'90 : offsetof(GUIComponentButton, mTextBox);
	return *reinterpret_cast<GUIComponentTextBox**>(reinterpret_cast<char*>(button) + offset);
}

inline eastl_vector<GUIComponentTextBox_Line>* gui_textbox_lines(GUIComponentTextBox* textbox)
{
	const size_t offset = big::hades2_is_debug_build() ? 0x6'48 : offsetof(GUIComponentTextBox, mLines);
	return reinterpret_cast<eastl_vector<GUIComponentTextBox_Line>*>(reinterpret_cast<char*>(textbox) + offset);
}

inline GUIComponentButton* g_currently_selected_gui_comp = nullptr;

// Reading a text box's lines can crash on the debug build for some reason.
// when the box or its line vector is null or points at non-text memory. Copy the
// lines under SEH so a bad read just yields fewer lines instead of taking down the game.
inline int tolk_copy_textbox_lines(GUIComponentTextBox* textbox, char lines[][256], int max_lines)
{
	__try
	{
		if (!textbox)
		{
			return 0;
		}
		auto* vec = gui_textbox_lines(textbox);
		if (!vec || !vec->mpBegin || vec->mpEnd < vec->mpBegin || vec->mpEnd - vec->mpBegin > max_lines)
		{
			return 0;
		}
		int count = 0;
		for (auto* line = vec->mpBegin; line < vec->mpEnd && count < max_lines; line++)
		{
			const size_t n = line->mText.size();
			if (n == 0 || n >= 256)
			{
				continue;
			}
			memcpy(lines[count], line->mText.c_str(), n);
			lines[count][n] = '\0';
			count++;
		}
		return count;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
}

inline std::vector<std::string> tolk_safe_textbox_lines(GUIComponentTextBox* textbox)
{
	char buf[64][256];
	const int count = tolk_copy_textbox_lines(textbox, buf, 64);
	std::vector<std::string> out;
	out.reserve(count);
	for (int k = 0; k < count; k++)
	{
		out.push_back(buf[k]);
	}
	return out;
}

namespace lua::tolk
{
	void bind(sol::table& state);
}
