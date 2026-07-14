---@meta mod_settings

---@class (exact) rom.mod_settings

-- Loads a mod's config.lua and registers its settings under the Mods tab of the in-game Options
-- menu, returning a live read/write proxy over the config. When using this, you do not need to depend on `Chalk`.
---@param config_lua string Path, relative to the mod's folder, of the config.lua that returns `config, configDesc`.
---@return table # A live read/write proxy over the mod's config; index it to read a setting and assign to write one.
function mod_settings.load(config_lua) end

-- Excludes the calling mod from the in-game mod settings menu: it stays listed but will begreyed out and
-- cannot be opened, with a note pointing the player to the mod's own description. Use it when the mod
-- should not be edited in-game. Works with Chalk or rom.mod_settings.load.
function mod_settings.opt_out() end
