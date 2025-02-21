---@meta mods

---@class (exact) rom.mods
---@field [Mod GUID] string # Each mod once loaded will have a key in this table, the key will be their guid string and the value their `_ENV`.

---@return table<int, string> # Table containing the order in which mods are loaded by the mod loader.
function mods.loading_order() end

-- Registers a callback that will be called once all mods are loaded. Will be called instantly if mods are already loaded and that you are just hot-reloading your mod.
---@param callback function callback that will be called once all mods are loaded. The callback function should match signature func()
function mods.on_all_mods_loaded(callback) end


