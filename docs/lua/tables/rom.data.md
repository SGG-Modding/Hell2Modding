# Table: rom.data

## Functions (2)

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


