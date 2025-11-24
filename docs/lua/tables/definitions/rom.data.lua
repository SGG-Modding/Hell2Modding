---@meta data

---@class (exact) rom.data

---@param hash_guid integer Hash value.
---@return string # Returns the string corresponding to the provided hash value.
function data.get_string_from_hash_guid(hash_guid) end

---@param str string String value.
---@return number # Returns the hash guid corresponding to the provided string value.
function data.get_hash_guid_from_string(str) end

-- Returns the override list for the given HashGuid. If LoadPackage(hash_guid)
--is called, each HashGuid in this list will be loaded instead.
---@param hash_guid number The HashGuid to look up.
---@return table<number> # A table of HashGuid values that replace the input.
function data.load_package_overrides_get(hash_guid) end

-- Defines an override list for a given HashGuid. When LoadPackage(hash_guid)
--is called, it will instead load each HashGuid listed in the override table.
--
--**Example Usage:**
--```lua
--local gui_hash = rom.data.get_hash_guid_from_string("GUI")
--local some_custom_hash = rom.data.get_hash_guid_from_string("NikkelM-ColouredBiomeMap")
--rom.rom.data.load_package_overrides_set(gui_hash, {gui_hash, some_custom_hash})
--```
---@param hash_guid number The original HashGuid that will be replaced.
---@param hash_guid_table_override table<number> List of HashGuid values that should be used instead.
function data.load_package_overrides_set(hash_guid, hash_guid_table_override) end

---@param function function Function called when game data file is read. The function must match signature: (string (file_path_being_read), string (file_content_buffer)) -> returns nothing (nil) or the new file buffer (string)
---@param file_path_being_read string optional. Use only if you want your lua function to be called for a given file_path.
function data.on_sjson_read_as_string(function, file_path_being_read) end

function data.reload_game_data() end


