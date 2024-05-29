# Table: rom.inputs

## Functions (3)

### `on_key_pressed([1], [2], Name)`

The parameters must be inside a table. Check how the vanilla game does it through `OnKeyPressed`.
For every possible keys, please refer to [this map](https://github.com/SGG-Modding/Hell2Modding/blob/6d1cb8ed8870a401ac1cefd599bf2ae3a270d949/src/lua_extensions/bindings/hades/inputs.cpp#L204-L298)
**Example Usage:**
rom.inputs.on_key_pressed{"Ctrl X", Name = "Testing key 2", function()
     print("hello there")
end}

- **Parameters:**
  - `[1]` (string): The key binding string representing the keys that, when pressed, will trigger the callback function. The format used is the one used by the vanilla game, please check the vanilla scripts using "OnKeyPressed".
  - `[2]` (function): The function to be called when the specified keybind is pressed.
  - `Name` (string): Optional. The name linked to this keybind, used in the GUI to help the user know what it corresponds to.

**Example Usage:**
```lua
rom.inputs.on_key_pressed([1], [2], Name)
```

### `let_game_input_go_through_gui_layer(new_value)`

Allows game input to be processed even when the GUI layer is active. This is useful for scenarios where you need the game to remain responsive to player actions or on key presses callbacks despite overlay interfaces.

- **Parameters:**
  - `new_value` (bool): Optional. Set the backing field to the passed new value.

**Example Usage:**
```lua
rom.inputs.let_game_input_go_through_gui_layer(new_value)
```

### `enable_vanilla_debug_keybinds(new_value)`

Enables the default debug key bindings used in the vanilla game.

- **Parameters:**
  - `new_value` (bool): Optional. Set the backing field to the passed new value.

**Example Usage:**
```lua
rom.inputs.enable_vanilla_debug_keybinds(new_value)
```


