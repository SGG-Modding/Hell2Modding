# Class: rom.config.config_entry

Provides access to a single setting inside of a config_file.

## Fields (1)

### `description`

Simple description of the setting shown to the user.

- Type: `string`

## Functions (2)

### `get()`

- **Returns:**
  - `boolean|number|string`: Value of this setting

**Example Usage:**
```lua
boolean|number|string = rom.config.config_entry:get()
```

### `set(boolean|number|string)`

- **Parameters:**
  - `boolean|number|string` (New value of this setting.)

**Example Usage:**
```lua
rom.config.config_entry:set(boolean|number|string)
```


