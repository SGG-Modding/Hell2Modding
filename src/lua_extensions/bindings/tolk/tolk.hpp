#pragma once

#include <EASTL/string.h>

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

struct GUIComponentTextBox
{
	GUIComponentTextBox_vtbl* vtbl;
	char m_pad[0x6'A2];
	eastl::string mStringBuilder;
	char m_pad2[70];
	eastl_vector<GUIComponentTextBox_Line> mLines;
};

static_assert(offsetof(GUIComponentTextBox, mStringBuilder) == 0x6'B0);
static_assert(offsetof(GUIComponentTextBox, mLines) == 0x7'10);

inline std::mutex g_GUIComponentTextBoxes_mutex;
inline std::unordered_set<GUIComponentTextBox*> g_GUIComponentTextBoxes;

inline GUIComponentTextBox* g_currently_selected_gui_comp = nullptr;

namespace lua::tolk
{
	void bind(sol::table& state);
}
