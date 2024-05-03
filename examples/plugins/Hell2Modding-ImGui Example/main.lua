local example_bool = true
rom.gui.add_to_menu_bar(function()
    if rom.ImGui.BeginMenu("Ayo") then

        local new_value, clicked = rom.ImGui.Checkbox("Checkbox Example", example_bool)
        if clicked then
            example_bool = new_value
        end

        rom.ImGui.EndMenu()
    end
end)

rom.gui.add_imgui(function()
    if rom.ImGui.Begin("ImGui Mod Example") then
        if rom.ImGui.Button("Example Button") then
            rom.log.warning("yes")
        end 
    end
    rom.ImGui.End()
end)
