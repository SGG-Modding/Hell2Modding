---@meta mod_settings

---@class (exact) rom.mod_settings

-- Loads a mod's `config.lua` and registers its settings under the Mods tab of the in-game Options
-- menu, returning a live read/write proxy over the config. Also manages the mod's `.cfg` file,
-- setting default values for new options and loading values saved to it by users. When using this,
-- your mod does not need to depend on or use `Chalk`.
---@param configFilePath string Path, relative to the mod's folder, of the `config.lua` that returns `config` and `configDesc`.
---@return table # A live read/write proxy over the mod's config. Index it to read a setting and assign to write one.
function mod_settings.load(configFilePath) end

-- Excludes the calling mod from the in-game mod settings menu: it stays listed but will be greyed out and
-- cannot be opened. Use it when the mod should not be edited in-game. Works with Chalk or rom.mod_settings.load.
---@param description? string A plain string or a localization table `{ en = "...", de = "..." }` shown in place of the generic note when the mod's disabled row is hovered.
function mod_settings.opt_out(description) end
