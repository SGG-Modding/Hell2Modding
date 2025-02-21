---@meta config.config_entry

-- Provides access to a single setting inside of a config_file.
---@class (exact) rom.config.config_entry
---@field description string # Simple description of the setting shown to the user.



---@return boolean|number|string # Value of this setting
function config.config_entry:get() end

---@param boolean|number|string New value of this setting.
function config.config_entry:set(boolean|number|string) end


