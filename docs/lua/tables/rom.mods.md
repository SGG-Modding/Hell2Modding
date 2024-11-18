# Table: rom.mods

## Fields (1)

### `[Mod GUID]`

Each mod once loaded will have a key in this table, the key will be their guid string and the value their `_ENV`.

- Type: `string`

## Functions (2)

### `loading_order()`

- **Returns:**
  - `table<int, string>`: Table containing the order in which mods are loaded by the mod loader.

**Example Usage:**
```lua
table<int, string> = rom.mods.loading_order()
```

### `on_all_mods_loaded(callback)`

Registers a callback that will be called once all mods are loaded. Will be called instantly if mods are already loaded and that you are just hot-reloading your mod.

- **Parameters:**
  - `callback` (function): callback that will be called once all mods are loaded. The callback function should match signature func()

**Example Usage:**
```lua
rom.mods.on_all_mods_loaded(callback)
```


