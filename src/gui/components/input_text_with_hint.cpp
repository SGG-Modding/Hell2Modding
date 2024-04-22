#include "gui/components/components.hpp"
#include "misc/cpp/imgui_stdlib.h"

namespace big
{
	bool components::input_text_with_hint(const std::string_view label, const std::string_view hint, char* buf, size_t buf_size, ImGuiInputTextFlags_ flag, std::function<void()> cb)
	{
		bool returned = false;
		if (returned = ImGui::InputTextWithHint(label.data(), hint.data(), buf, buf_size, flag); returned && cb)
		{
			cb();
		}

		return returned;
	}

	bool components::input_text_with_hint(const std::string_view label, const std::string_view hint, std::string& buf, ImGuiInputTextFlags_ flag, std::function<void()> cb)
	{
		bool returned = false;
		if (returned = ImGui::InputTextWithHint(label.data(), hint.data(), &buf, flag); returned && cb)
		{
			cb();
		}

		return returned;
	}
} // namespace big
