---@meta on_import

---@class (exact) rom.on_import

-- The passed function will be called before the game loads a .lua script from the game's Content/Scripts folder.
--The _ENV returned (if not nil) by the passed function gives you a way to define the _ENV of this lua script.
---@param function function signature (string file_name, current_ENV_for_this_import) return nil or _ENV
function on_import.pre(function) end

-- The passed function will be called after the game loads a .lua script from the game's Content/Scripts folder.
---@param function function signature (string file_name)
function on_import.post(function) end


