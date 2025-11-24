# Table: rom.data

## Functions (6)

### `get_string_from_hash_guid(hash_guid)`

- **Parameters:**
  - `hash_guid` (integer): Hash value.

- **Returns:**
  - `string`: Returns the string corresponding to the provided hash value.

**Example Usage:**
```lua
string = rom.data.get_string_from_hash_guid(hash_guid)
```

### `get_hash_guid_from_string(str)`

- **Parameters:**
  - `str` (string): String value.

- **Returns:**
  - `number`: Returns the hash guid corresponding to the provided string value.

**Example Usage:**
```lua
number = rom.data.get_hash_guid_from_string(str)
```

### `load_package_overrides_get(hash_guid)`

Returns the override list for the given HashGuid. If LoadPackage(hash_guid)
is called, each HashGuid in this list will be loaded instead.

- **Parameters:**
  - `hash_guid` (number): The HashGuid to look up.

- **Returns:**
  - `table<number>`: A table of HashGuid values that replace the input.

**Example Usage:**
```lua
table<number> = rom.data.load_package_overrides_get(hash_guid)
```

### `load_package_overrides_set(hash_guid, hash_guid_table_override)`

Defines an override list for a given HashGuid. When LoadPackage(hash_guid)
is called, it will instead load each HashGuid listed in the override table.

**Example Usage:**
```lua
local gui_hash = rom.data.get_hash_guid_from_string("GUI")
local some_custom_hash = rom.data.get_hash_guid_from_string("NikkelM-ColouredBiomeMap")
rom.rom.data.load_package_overrides_set(gui_hash, {gui_hash, some_custom_hash})
```

- **Parameters:**
  - `hash_guid` (number): The original HashGuid that will be replaced.
  - `hash_guid_table_override` (table<number>): List of HashGuid values that should be used instead.

**Example Usage:**
```lua
rom.data.load_package_overrides_set(hash_guid, hash_guid_table_override)
```

### `on_sjson_read_as_string(function, file_path_being_read)`

- **Parameters:**
  - `function` (function): Function called when game data file is read. The function must match signature: (string (file_path_being_read), string (file_content_buffer)) -> returns nothing (nil) or the new file buffer (string)
  - `file_path_being_read` (string): optional. Use only if you want your lua function to be called for a given file_path.

**Example Usage:**
```lua
rom.data.on_sjson_read_as_string(function, file_path_being_read)
```

### `reload_game_data()`

**Example Usage:**
```lua
rom.data.reload_game_data()
```


