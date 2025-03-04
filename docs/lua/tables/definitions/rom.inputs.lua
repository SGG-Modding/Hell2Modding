---@meta inputs

---@class (exact) rom.inputs

-- The parameters must be inside a table. Check how the vanilla game does it through `OnKeyPressed`.
--For every possible keys, please refer to [this map](https://github.com/SGG-Modding/Hell2Modding/blob/6d1cb8ed8870a401ac1cefd599bf2ae3a270d949/src/lua_extensions/bindings/hades/inputs.cpp#L204-L298)
--
--**Example Usage:**
--```lua
--local handle = rom.inputs.on_key_pressed{"Ctrl X", Name = "Testing key 2", function()
--     print("hello there")
--end}
--```
---@param [1] string The key binding string representing the keys that, when pressed, will trigger the callback function. The format used is the one used by the vanilla game, please check the vanilla scripts using "OnKeyPressed".
---@param [2] function The function to be called when the specified keybind is pressed.
---@param Name string Optional. The name linked to this keybind, used in the GUI to help the user know what it corresponds to.
---@return table # Returns a handle to use, in case you want to remove this specific keybind.
function inputs.on_key_pressed([1], [2], Name) end

-- Remove a keybind previously added through `on_key_pressed`.
---@param handle table The handle that was returned to you from the on_key_pressed call.
function inputs.remove_on_key_pressed(handle) end

-- Allows game input to be processed even when the GUI layer is active. This is useful for scenarios where you need the game to remain responsive to player actions or on key presses callbacks despite overlay interfaces.
---@param new_value bool Optional. Set the backing field to the passed new value.
function inputs.let_game_input_go_through_gui_layer(new_value) end

-- Enables the default debug key bindings used in the vanilla game.
---@param new_value bool Optional. Set the backing field to the passed new value.
function inputs.enable_vanilla_debug_keybinds(new_value) end


