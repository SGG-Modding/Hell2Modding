# Class: rom.config.config_file

A helper class to handle persistent data.

## Fields (3)

### `owner_guid`

The owner GUID of this config file.

- Type: `string`

### `config_file_path`

The file path of this config file.

- Type: `string`

### `entries`

All config entries of the config file.

- Type: `table<config.config_definition, config.config_entry>`

## Constructors (1)

### `new(config_path, save_on_init)`

Create a new config file at the specified config path.

- **Parameters:**
  - `config_path` (string): Full path to a file that contains settings. The file will be created as needed. It's recommended to use `.cfg` as the file extension. The mod manager will pick it up and make it show nicely inside the mod manager UI.
  - `save_on_init` (boolean): If the config file/directory doesn't exist, create it immediately.

**Example Usage:**
```lua
myInstance = config.config_file:new(config_path, save_on_init)
```

## Functions (4)

### `bind(section, key, default_value, description)`

Create a new setting. The setting is saved to drive and loaded automatically.
Each section and key pair can be used to add only one setting,
trying to add a second setting will throw an exception.

- **Parameters:**
  - `section` (string): Section/category/group of the setting. Settings are grouped by this.
  - `key` (string): Name of the setting.
  - `default_value` (boolean|number|string): Value of the setting if the setting was not created yet.
  - `description` (string): Simple description of the setting shown to the user.

- **Returns:**
  - `config.config_entry`: new config_entry object.

**Example Usage:**
```lua
config.config_entry = rom.config.config_file:bind(section, key, default_value, description)
```

### `remove(section, key)`

Removes a setting from the config file.

- **Parameters:**
  - `section` (string): Section/category/group of the setting. Settings are grouped by this.
  - `key` (string): Name of the setting.

**Example Usage:**
```lua
rom.config.config_file:remove(section, key)
```

### `save()`

Writes the config to disk.

**Example Usage:**
```lua
rom.config.config_file:save()
```

### `reload()`

Reloads the config from disk. Unsaved changes are lost.

**Example Usage:**
```lua
rom.config.config_file:reload()
```


