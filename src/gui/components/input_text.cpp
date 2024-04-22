#include "gui/components/components.hpp"

namespace big
{
	void components::input_text(const std::string_view label, char* buf, size_t buf_size, ImGuiInputTextFlags_ flag, std::function<void()> cb)
	{
		if (ImGui::InputText(label.data(), buf, buf_size, flag))
		{
			if (cb)
			{
				cb();
			}
		}
	}
} // namespace big
