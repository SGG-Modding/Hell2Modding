# Class: rom.config.config_entry

Provides access to a single setting inside of a config_file.

## Functions (2)

### `get()`

- **Returns:**
  - `val`: bool or double or string. Value of this setting

**Example Usage:**
```lua
val = rom.config.config_entry:get()
```

### `set(new_value)`

- **Parameters:**
  - `new_value` (bool or double or string): New value of this setting.

**Example Usage:**
```lua
rom.config.config_entry:set(new_value)
```


