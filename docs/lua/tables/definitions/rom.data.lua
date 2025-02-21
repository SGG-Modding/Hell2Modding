---@meta data

---@class (exact) rom.data

---@param function function Function called when game data file is read. The function must match signature: (string (file_path_being_read), string (file_content_buffer)) -> returns nothing (nil) or the new file buffer (string)
---@param file_path_being_read string optional. Use only if you want your lua function to be called for a given file_path.
function data.on_sjson_read_as_string(function, file_path_being_read) end

function data.reload_game_data() end

---@param hash_guid integer Hash value.
---@return string # Returns the string corresponding to the provided hash value.
function data.get_string_from_hash_guid(hash_guid) end


