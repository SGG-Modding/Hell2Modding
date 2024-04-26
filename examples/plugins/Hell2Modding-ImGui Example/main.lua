local example_bool = true
h2m.gui.add_to_menu_bar(function()
    if h2m.ImGui.BeginMenu("Ayo") then

        local new_value, clicked = h2m.ImGui.Checkbox("Checkbox Example", example_bool)
        if clicked then
            example_bool = new_value
        end

        h2m.ImGui.EndMenu()
    end
end)

h2m.gui.add_imgui(function()
    if h2m.ImGui.Begin("ImGui Mod Example") then
        if h2m.ImGui.Button("Example Button") then
            h2m.log.warning("yes")
        end 
    end
    h2m.ImGui.End()
end)
