# Table: _ENV - Plugin Specific Global Table

Each mod/plugin have their own global table containing helpers, such as:
- Their own guid

- Path to their own folder inside `config`: Used for data that must persist between sessions that can be manipulated by the user.

- Path to their own folder inside `plugins_data`: Used for data that must persist between sessions but not be manipulated by the user.

- Path to their own folder inside `plugins`: Location of .lua, README, manifest.json files.

You can access other mods helpers through the `mods[OTHER_MOD_GUID]` table.

**Example Usage:**

```lua
print(_ENV["!guid"])

for n in pairs(h2m.mods[_ENV["!guid"]]) do
     log.info(n)
end
```

## Fields (5)

### `!guid`

Guid of the mod.

- Type: `string`

### `!config_mod_folder_path`

Path to the mod folder inside `config`

- Type: `string`

### `!plugins_data_mod_folder_path`

Path to the mod folder inside `plugins_data`

- Type: `string`

### `!plugins_mod_folder_path`

Path to the mod folder inside `plugins`

- Type: `string`

### `!this`

- Type: `lua_module*`

