# Table: rom.mod_settings

## Functions (2)

### `load(configFilePath)`

Loads a mod's `config.lua` and registers its settings under the Mods tab of the in-game Options menu, returning a 
live read/write proxy over the config. Also manages the mod's `.cfg` file, setting default values for new options
and loading values saved to it by users. When using this, your mod does not need to depend on or use `Chalk`.

- **Parameters:**
  - `configFilePath` (string): Path, relative to the mod's folder, of the `config.lua` that returns `config` and `configDesc`.

- **Returns:**
  - `table`: A live read/write proxy over the mod's config. Index it to read a setting and assign to write one.

**Example Usage:**
```lua
config = rom.mod_settings.load("config.lua")
```

### `opt_out(description)`

Excludes the calling mod from the in-game mod settings menu: it stays listed but will be greyed out and
cannot be opened. Use it when the mod should not be edited in-game. Works with Chalk or rom.mod_settings.load.

- **Parameters:**
  - `description` (string): Optional. A plain string or a localization table `{ en = "...", de = "..." }` shown in place of the generic note when the mod's disabled row is hovered.

**Example Usage:**
```lua
rom.mod_settings.opt_out("Please use the imgui menu to configure this mod (opens with \"Insert\" by default).")
```
