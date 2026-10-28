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
  - `string`: Returns the engine-visible Content directory used for game assets and SJSON hook paths, resolving on Microsoft Store to the virtual WindowsApps package Content directory reported by the engine, while rom.paths.GameInstall() should be used for physical filesystem access or the physical executable directory

**Example Usage:**
```lua
string = rom.paths.Content()
```

### `Ship()`

- **Returns:**
  - `string`: Returns the engine-visible directory containing Hades2.exe, including the virtual WindowsApps package directory on Microsoft Store, while rom.paths.GameInstall() returns the physical directory

**Example Usage:**
```lua
string = rom.paths.Ship()
```

### `GameInstall()`

- **Returns:**
  - `string`: Returns the physical directory containing Hades2.exe, including the mutable Xbox/Gaming Services install directory on Microsoft Store, and matches rom.paths.Ship() on Steam and Epic

**Example Usage:**
```lua
string = rom.paths.GameInstall()
```


