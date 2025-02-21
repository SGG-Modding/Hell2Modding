---@meta gui

---@class (exact) rom.gui

-- Registers a function that will be called under your dedicated space in the imgui main menu bar.
--**Example Usage:**
--```lua
--rom.gui.add_to_menu_bar(function()
--   if ImGui.BeginMenu("Ayo") then
--       if ImGui.Button("Label") then
--         log.info("hi")
--       end
--       ImGui.EndMenu()
--   end
--end)
--```
---@param imgui_rendering function Function that will be called under your dedicated space in the imgui main menu bar.
function gui.add_to_menu_bar(imgui_rendering) end

-- Registers a function that will be called every rendering frame, regardless of the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.
--**Example Usage:**
--```lua
--rom.gui.add_always_draw_imgui(function()
--   if ImGui.Begin("My Custom Window") then
--       if ImGui.Button("Label") then
--         log.info("hi")
--       end
--
--   end
--   ImGui.End()
--end)
--```
---@param imgui_rendering function Function that will be called every rendering frame, regardless of the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.
function gui.add_always_draw_imgui(imgui_rendering) end

-- Registers a function that will be called every rendering frame, only if the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.
--**Example Usage:**
--```lua
--rom.gui.add_imgui(function()
--   if ImGui.Begin("My Custom Window") then
--       if ImGui.Button("Label") then
--         log.info("hi")
--       end
--
--   end
--   ImGui.End()
--end)
--```
---@param imgui_rendering function Function that will be called every rendering frame, only if the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.
function gui.add_imgui(imgui_rendering) end

---@return boolean # Returns true if the GUI is open.
function gui.is_open() end

-- Opens or closes the GUI.
function gui.toggle() end


