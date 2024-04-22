local example_bool = true
LuaExt.gui.add_to_menu_bar(function()
    local new_value, clicked = LuaExt.ImGui.Checkbox("Checkbox Example", example_bool)
    if clicked then
        example_bool = new_value
    end
end)

LuaExt.gui.add_imgui(function()
    if LuaExt.ImGui.Begin("ImGui Mod Example") then
        if LuaExt.ImGui.Button("Example Button") then
            print("yeaap")
        end

    end
    LuaExt.ImGui.End()
end)

-- LuaExt.gui.add_imgui(function()
--     print("hello there!")
-- end)

print(ScreenData)