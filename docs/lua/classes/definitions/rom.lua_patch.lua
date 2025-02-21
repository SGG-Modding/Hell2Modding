---@meta lua_patch

-- Class representing a in-memory patch.
---@class (exact) rom.lua_patch




-- Apply the modified value.
function lua_patch:apply() end

-- Restore the original value.
function lua_patch:restore() end


