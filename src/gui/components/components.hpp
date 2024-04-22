#pragma once

namespace big
{
	class components
	{
		static void custom_text(const std::string_view, ImFont*);

	public:
		static bool nav_button(const std::string_view);
		static void icon(const std::string_view);
		static void small_text(const std::string_view);
		static void sub_title(const std::string_view);
		static void title(const std::string_view);

		static bool input_text_with_hint(const std::string_view label, const std::string_view hint, char* buf, size_t buf_size, ImGuiInputTextFlags_ flag = ImGuiInputTextFlags_None, std::function<void()> cb = nullptr);
		static bool input_text_with_hint(const std::string_view label, const std::string_view hint, std::string& buf, ImGuiInputTextFlags_ flag = ImGuiInputTextFlags_None, std::function<void()> cb = nullptr);

		static void input_text(const std::string_view label, char* buf, size_t buf_size, ImGuiInputTextFlags_ flag = ImGuiInputTextFlags_None, std::function<void()> cb = nullptr);

		static bool selectable(const std::string_view, bool);
		static bool selectable(const std::string_view, bool, ImGuiSelectableFlags);
		static void selectable(const std::string_view, bool, std::function<void()>);
		static void selectable(const std::string_view, bool, ImGuiSelectableFlags, std::function<void()>);

		static bool script_patch_checkbox(const std::string_view text, bool* option, const std::string_view tooltip = "");

		static void options_modal(std::string element_name, std::function<void()> render_elements, bool sameline = true, std::string custom_button_name = "Options");

		template<ImVec2 size = ImVec2(0, 0), ImVec4 color = ImVec4(0.24f, 0.23f, 0.29f, 1.00f)>
		static bool button(const std::string_view text)
		{
			bool status = false;

			if constexpr (color.x != 0.24f || color.y != 0.23f || color.z != 0.29f || color.w != 1.0f)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, color);
			}

			status = ImGui::Button(text.data(), size);

			if constexpr (color.x != 0.24f || color.y != 0.23f || color.z != 0.29f || color.w != 1.0f)
			{
				ImGui::PopStyleColor(1);
			}
			return status;
		}

		template<ImVec4 green = ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ImVec4 red = ImVec4(1.0f, 0.0f, 0.0f, 1.0f)>
		static void overlay_indicator(const std::string_view text, bool value)
		{
			ImGui::Text(std::format("{}: ", text).data());
			ImGui::SameLine(180);
			ImGui::TextColored(value ? green : red, value ? "Enabled" : "Disabled");
		}

		template<ImVec2 size = ImVec2(0, 0), ImVec4 color = ImVec4(0.24f, 0.23f, 0.29f, 1.00f)>
		static void button(const std::string_view text, std::function<void()> cb)
		{
			if (button<size, color>(text))
			{
				cb();
			}
		}

		template<typename PredicateFn, typename ComponentsFn>
		static void disable_unless(PredicateFn predicate_fn, ComponentsFn components_fn)
		{
			const auto result = predicate_fn();
			if (!result)
			{
				ImGui::BeginDisabled(true);
			}
			components_fn();
			if (!result)
			{
				ImGui::EndDisabled();
			}
		}
	};
} // namespace big
