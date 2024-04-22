#include "gui/components/components.hpp"

namespace big
{
	bool components::selectable(const std::string_view text, bool selected)
	{
		return ImGui::Selectable(text.data(), selected);
	}

	bool components::selectable(const std::string_view text, bool selected, ImGuiSelectableFlags flag)
	{
		return ImGui::Selectable(text.data(), selected, flag);
	}

	void components::selectable(const std::string_view text, bool selected, std::function<void()> cb)
	{
		if (components::selectable(text.data(), selected))
		{
			cb();
		}
	}

	void components::selectable(const std::string_view text, bool selected, ImGuiSelectableFlags flag, std::function<void()> cb)
	{
		if (components::selectable(text.data(), selected, flag))
		{
			cb();
		}
	}
} // namespace big
