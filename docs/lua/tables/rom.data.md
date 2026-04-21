# Table: rom.data

## Fields (1)

### `SJSON_DATA_DIR_NAME`

Value: "Hell2Modding-SJSON"
The canonical directory name for the SJSON data overlay.
Mods must place .sjson files in plugins_data/<mod-guid>/<SJSON_DATA_DIR_NAME>/Animations/, Text/{lang}/, etc.
Hell2Modding scans this directory at startup and injects discovered .sjson files into the engine's loading pipeline.

- Type: `string`

## Functions (18)

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

### `add_granny_file(filename, full_path)`

Registers a custom GPK file for file-redirection at runtime, allowing
hot-loading of GPK assets that were not present during initial scan.

**Example Usage:**
```lua
rom.rom.data.add_granny_file("Melinoe.gpk", "C:/path/to/plugins_data/CG3HBuilder/Melinoe.gpk")
```

- **Parameters:**
  - `filename` (string): The GPK filename (e.g. "Melinoe.gpk").
  - `full_path` (string): The full filesystem path to the GPK file.

**Example Usage:**
```lua
rom.data.add_granny_file(filename, full_path)
```

### `add_package_file(filename, full_path)`

Registers a custom PKG/PKG_MANIFEST file for file-redirection at runtime.

**Example Usage:**
```lua
rom.rom.data.add_package_file("MyMod.pkg", "C:/path/to/plugins_data/MyMod/MyMod.pkg")
```

- **Parameters:**
  - `filename` (string): The PKG or PKG_MANIFEST filename.
  - `full_path` (string): The full filesystem path to the file.

**Example Usage:**
```lua
rom.data.add_package_file(filename, full_path)
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

### `register_sjson_file(absolute_path)`

Registers a .sjson file so the engine discovers and loads it as if it were in the game's `Content/Game/` directory.
The engine-relative path is inferred automatically: files inside `plugins_data/<mod>/<SJSON_DATA_DIR_NAME>/` map to `Content/Game/`.
For example, `plugins_data/<mod-guid>/<SJSON_DATA_DIR_NAME>/Animations/Foo.sjson` is loaded as `Content/Game/Animations/Foo.sjson`.
At startup, Hell2Modding automatically scans every mod's <SJSON_DATA_DIR_NAME> directory and registers any .sjson files found.
Use this function to dynamically register files created during the current session (e.g. a first-time install placing a file into plugins_data).

- **Parameters:**
  - `absolute_path` (string): The absolute filesystem path to a .sjson file inside a <SJSON_DATA_DIR_NAME> directory.

- **Returns:**
  - `boolean`: true if registered successfully, false if the file is a duplicate, not a .sjson, or the path does not contain <SJSON_DATA_DIR_NAME>.

**Example Usage:**
```lua
boolean = rom.data.register_sjson_file(absolute_path)
```

### `register_content_directory(absolute_base_path)`

Scans the directory recursively and registers all .sjson files found. Each file's engine path is derived from its position in the directory tree.
This is the same scan that Hell2Modding performs automatically at startup for `plugins_data/*/<SJSON_DATA_DIR_NAME>/`.

- **Parameters:**
  - `absolute_base_path` (string): Absolute path to a directory whose structure mirrors `Content/Game/` (e.g. containing `Animations/`, `Text/en/`, etc.)

**Example Usage:**
```lua
rom.data.register_content_directory(absolute_base_path)
```

### `register_file_redirect(content_relative_path, absolute_path)`

Registers a file redirect so the engine loads it from an external location instead of Content/.
Unlike register_content_file (SJSON-only), this works for any file type that the engine loads via fsAppendPathComponent (maps, etc.).
No directory convention is enforced - the caller provides both paths.

- **Parameters:**
  - `content_relative_path` (string): The path relative to Content/, e.g. "Maps/D_Hub.map_text" or "Maps/bin/D_Hub.thing_bin"
  - `absolute_path` (string): The absolute filesystem path to the actual file

- **Returns:**
  - `boolean`: true if registered, false if duplicate

**Example Usage:**
```lua
boolean = rom.data.register_file_redirect(content_relative_path, absolute_path)
```

### `register_plugin_file(filename, absolute_path)`

Registers a file for engine injection and redirect. Routes to the appropriate internal registry.
Supported extensions: .map_text, .thing_bin, .bik, .bik_atlas, .fsb, .txt.
SJSON files should use `register_sjson_file` instead.

- **Parameters:**
  - `filename` (string): The filename (e.g. "D_Boss01.map_text", "HadesBattleIdle.bik", "Zagreus.fsb")
  - `absolute_path` (string): The absolute filesystem path to the file

- **Returns:**
  - `boolean`: true if registered, false if already registered or unsupported extension

**Example Usage:**
```lua
boolean = rom.data.register_plugin_file(filename, absolute_path)
```

### `draw_set_visible(entry_name, visible)`

Toggles visibility of a model entry.  Takes effect immediately.

**Example Usage:**
```lua
rom.rom.data.draw_set_visible("HecateBattle_Mesh", false)
```

- **Parameters:**
  - `entry_name` (string): Model entry name (e.g. "HecateBattle_Mesh").
  - `visible` (boolean): true to show, false to hide.

**Example Usage:**
```lua
rom.data.draw_set_visible(entry_name, visible)
```

### `draw_dump_pool_stats()`

Logs vertex-pool and index-pool capacity and cursor usage.
Diagnostic only: useful when tuning the pool size config
against a mod load-out.

- **Returns:**
  - `number`: number of per-shader vertex buffers dumped.

**Example Usage:**
```lua
number = rom.data.draw_dump_pool_stats()
```

### `draw_populate_entry_textures(entry_name)`

Resolves the entry's mesh textures up-front.  Useful for
entries that aren't in the active scene (loaded-but-not-drawn
variants) so their first drawn frame doesn't render white.

- **Parameters:**
  - `entry_name` (string)

- **Returns:**
  - `integer`: number of textures populated.

**Example Usage:**
```lua
integer = rom.data.draw_populate_entry_textures(entry_name)
```

### `draw_set_mesh_visible(entry_name, mesh_name, visible)`

Finer-grained than draw_set_visible: toggles a single named
mesh inside an entry instead of the whole entry.

- **Parameters:**
  - `entry_name` (string): Model entry (e.g. "HecateHub_Mesh").
  - `mesh_name` (string): Mesh name inside that entry (e.g. "TorusHubMesh").
  - `visible` (boolean): true to show, false to hide.

- **Returns:**
  - `boolean`: true on success.

**Example Usage:**
```lua
boolean = rom.data.draw_set_mesh_visible(entry_name, mesh_name, visible)
```

### `draw_swap_to_variant(stock_entry, variant_entry)`

Redirects draw calls for `stock_entry` to `variant_entry`.
Use draw_populate_entry_textures on the variant first (ideally
from a safe window like the first ImGui frame): this call is a
cheap map write and is not safe to call GetTexture from.

**Example Usage:**
```lua
rom.data.draw_populate_entry_textures("HecateHub_Variant_Mesh")
rom.rom.data.draw_swap_to_variant("HecateHub_Mesh", "HecateHub_Variant_Mesh")
-- later:
rom.data.draw_restore_stock("HecateHub_Mesh")
```

- **Parameters:**
  - `stock_entry` (string): Stock entry name (e.g. "HecateHub_Mesh").
  - `variant_entry` (string): Variant entry name loaded in mModelData.

- **Returns:**
  - `boolean`: true on success.

**Example Usage:**
```lua
boolean = rom.data.draw_swap_to_variant(stock_entry, variant_entry)
```

### `draw_restore_stock(stock_entry)`

Clears any active hash remap for the given stock entry.

- **Parameters:**
  - `stock_entry` (string): Stock entry name to revert to.

- **Returns:**
  - `boolean`: true on success.

**Example Usage:**
```lua
boolean = rom.data.draw_restore_stock(stock_entry)
```


