# Table: _ENV - Plugin Specific Global Table

Each mod/plugin have their own global table containing helpers, such as:
- Their own guid

- Path to their own folder inside `config`: Used for data that must persist between sessions that can be manipulated by the user.

- Path to their own folder inside `plugins_data`: Used for data that must persist between sessions but not be manipulated by the user.

- Path to their own folder inside `plugins`: Location of .lua, README, manifest.json files.

You can access other mods helpers through the `rom.mods[OTHER_MOD_GUID]` table.

**Example Usage:**

```lua
print(_ENV._PLUGIN.guid)

for n in pairs(rom.mods[_ENV._PLUGIN.guid]) do
     log.info(n)
end
```

## Fields (8)

### `_PLUGIN.guid`

Guid of the mod.

- Type: `string`

### `_PLUGIN.version`

Version of the mod.

- Type: `string`

### `_PLUGIN.dependencies`

Dependencies of the mod.

- Type: `table<string>`

### `_PLUGIN.dependencies_no_version_number`

Dependencies of the mod without the version numbers.

- Type: `table<string>`

### `_PLUGIN.config_mod_folder_path`

Path to the mod folder inside `config`

- Type: `string`

### `_PLUGIN.plugins_data_mod_folder_path`

Path to the mod folder inside `plugins_data`

- Type: `string`

### `_PLUGIN.plugins_mod_folder_path`

Path to the mod folder inside `plugins`

- Type: `string`

### `_PLUGIN.this`

- Type: `lua_module*`

