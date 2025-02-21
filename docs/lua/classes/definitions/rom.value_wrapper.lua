---@meta value_wrapper

-- Class for wrapping parameters and return value of functions, used mostly by the dynamic_hook system.
---@class (exact) rom.value_wrapper




-- Get the value currently contained by the wrapper.
---@return any # The current value.
function value_wrapper:get() end

-- Set the new value contained by the wrapper.
---@param new_value any The new value.
function value_wrapper:set(new_value) end


