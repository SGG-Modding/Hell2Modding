---@meta config.config_file

-- A helper class to handle persistent data.
---@class (exact) rom.config.config_file
---@field owner_guid string # The owner GUID of this config file.
---@field config_file_path string # The file path of this config file.
---@field entries table<config.config_definition, config.config_entry> # All config entries of the config file.

-- Create a new config file at the specified config path.
---@param config_path string Full path to a file that contains settings. The file will be created as needed. It's recommended to use `.cfg` as the file extension. The mod manager will pick it up and make it show nicely inside the mod manager UI.
---@param save_on_init boolean If the config file/directory doesn't exist, create it immediately.
---@return config.config_file
function config.config_file:new(config_path, save_on_init) end

-- Create a new setting. The setting is saved to drive and loaded automatically.
--Each section and key pair can be used to add only one setting,
--trying to add a second setting will throw an exception.
---@param section string Section/category/group of the setting. Settings are grouped by this.
---@param key string Name of the setting.
---@param default_value boolean|number|string Value of the setting if the setting was not created yet.
---@param description string Simple description of the setting shown to the user.
---@return config.config_entry # new config_entry object.
function config.config_file:bind(section, key, default_value, description) end

-- Removes a setting from the config file.
---@param section string Section/category/group of the setting. Settings are grouped by this.
---@param key string Name of the setting.
function config.config_file:remove(section, key) end

-- Writes the config to disk.
function config.config_file:save() end

-- Reloads the config from disk. Unsaved changes are lost.
function config.config_file:reload() end


