# Table: rom.inputs

## Functions (3)

### `on_key_pressed(keybind, callback)`

- **Parameters:**
  - `keybind` (string): The key binding string representing the key that, when pressed, will trigger the callback function. The format used is the one used by the vanilla game, please check the vanilla scripts using "OnKeyPressed".
  - `callback` (function): The function to be called when the specified keybind is pressed.

**Example Usage:**
```lua
rom.inputs.on_key_pressed(keybind, callback)
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


