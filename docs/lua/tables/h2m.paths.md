# Table: h2m.paths

Table containing helpers for retrieving Hell2Modding related IO file/folder paths.

## Functions (2)

### `config()`

Used for data that must persist between sessions and that can be manipulated by the user.

- **Returns:**
  - `string`: Returns the Hell2Modding/config folder path

**Example Usage:**
```lua
string = h2m.paths.config()
```

### `plugins_data()`

Used for data that must persist between sessions but not be manipulated by the user.

- **Returns:**
  - `string`: Returns the Hell2Modding/plugins_data folder path

**Example Usage:**
```lua
string = h2m.paths.plugins_data()
```

