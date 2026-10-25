# Table: rom.paths

Table containing helpers for retrieving project related IO file/folder paths.

## Functions (6)

### `config()`

Used for data that must persist between sessions and that can be manipulated by the user.

- **Returns:**
  - `string`: Returns the config folder path

**Example Usage:**
```lua
string = rom.paths.config()
```

### `plugins_data()`

Used for data that must persist between sessions but not be manipulated by the user.

- **Returns:**
  - `string`: Returns the plugins_data folder path

**Example Usage:**
```lua
string = rom.paths.plugins_data()
```

### `plugins()`

Location of .lua, README, manifest.json files.

- **Returns:**
  - `string`: Returns the plugins folder path

**Example Usage:**
```lua
string = rom.paths.plugins()
```

### `Content()`

- **Returns:**
  - `string`: Returns the GameFolder/Content folder path

**Example Usage:**
```lua
string = rom.paths.Content()
```

### `Ship()`

- **Returns:**
  - `string`: Returns the GameFolder/Ship folder path

**Example Usage:**
```lua
string = rom.paths.Ship()
```

### `GameInstall()`

- **Returns:**
  - `string`: Returns the physical folder containing the game executable on disk

**Example Usage:**
```lua
string = rom.paths.GameInstall()
```


