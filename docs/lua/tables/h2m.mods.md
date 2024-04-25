# Table: h2m.mods

## Fields (1)

### `[Mod GUID]`

Each mod once loaded will have a key in this table, the key will be their guid string and the value their `_ENV`.

- Type: `string`

## Functions (1)

### `on_all_mods_loaded(callback)`

Registers a callback that will be called once all mods are loaded. Will be called instantly if mods are already loaded and that you are just hot-reloading your mod.

- **Parameters:**
  - `callback` (function): callback that will be called once all mods are loaded. The callback function should match signature func()

**Example Usage:**
```lua
h2m.mods.on_all_mods_loaded(callback)
```


