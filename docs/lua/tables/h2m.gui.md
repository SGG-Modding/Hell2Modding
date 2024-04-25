# Table: h2m.gui

## Functions (4)

### `is_open()`

- **Returns:**
  - `bool`: Returns true if the GUI is open.

**Example Usage:**
```lua
bool = h2m.gui.is_open()
```

### `add_to_menu_bar(imgui_rendering)`

Registers a function that will be called under your dedicated space in the imgui main menu bar.
**Example Usage:**
```lua
h2m.gui.add_to_menu_bar(function()
   if h2m.ImGui.BeginMenu("Ayo") then
       if h2m.ImGui.Button("Label") then
         h2m.log.info("hi")
       end
       h2m.ImGui.EndMenu()
   end
end)
```

- **Parameters:**
  - `imgui_rendering` (function): Function that will be called under your dedicated space in the imgui main menu bar.

**Example Usage:**
```lua
h2m.gui.add_to_menu_bar(imgui_rendering)
```

### `add_always_draw_imgui(imgui_rendering)`

Registers a function that will be called every rendering frame, regardless of the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.
**Example Usage:**
```lua
h2m.gui.add_always_draw_imgui(function()
   if h2m.ImGui.Begin("My Custom Window") then
       if h2m.ImGui.Button("Label") then
         h2m.log.info("hi")
       end

   end
   h2m.ImGui.End()
end)
```

- **Parameters:**
  - `imgui_rendering` (function): Function that will be called every rendering frame, regardless of the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.

**Example Usage:**
```lua
h2m.gui.add_always_draw_imgui(imgui_rendering)
```

### `add_imgui(imgui_rendering)`

Registers a function that will be called every rendering frame, only if the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.
**Example Usage:**
```lua
h2m.gui.add_imgui(function()
   if h2m.ImGui.Begin("My Custom Window") then
       if h2m.ImGui.Button("Label") then
         h2m.log.info("hi")
       end

   end
   h2m.ImGui.End()
end)
```

- **Parameters:**
  - `imgui_rendering` (function): Function that will be called every rendering frame, only if the gui is in its open state. You can call ImGui functions in it, please check the ImGui.md documentation file for more info.

**Example Usage:**
```lua
h2m.gui.add_imgui(imgui_rendering)
```


