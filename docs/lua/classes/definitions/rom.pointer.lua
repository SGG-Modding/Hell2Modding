---@meta pointer

-- Class representing a 64-bit memory address.
---@class (exact) rom.pointer


-- Returns a memory instance, with the given address.
---@param address integer Address
---@return pointer
function pointer:new(address) end

-- Adds an offset to the current memory address and returns a new pointer object.
---@param offset integer offset
---@return pointer # new pointer object.
function pointer:add(offset) end

-- Subs an offset to the current memory address and returns a new pointer object.
---@param offset integer offset
---@return pointer # new pointer object.
function pointer:sub(offset) end

-- Rips the current memory address and returns a new pointer object.
---@param offset integer offset
---@return pointer # new pointer object.
function pointer:rip(offset) end

-- Rips (cmp instruction) the current memory address and returns a new pointer object.
---@param offset integer offset
---@return pointer # new pointer object.
function pointer:rip_cmp(offset) end

-- Retrieves the value stored at the memory address as the specified type.
---@return number # the value stored at the memory address as the specified type.
function pointer:get_byte() end

-- Retrieves the value stored at the memory address as the specified type.
---@return number # the value stored at the memory address as the specified type.
function pointer:get_word() end

-- Retrieves the value stored at the memory address as the specified type.
---@return number # the value stored at the memory address as the specified type.
function pointer:get_dword() end

-- Retrieves the value stored at the memory address as the specified type.
---@return float # the value stored at the memory address as the specified type.
function pointer:get_float() end

-- Retrieves the value stored at the memory address as the specified type.
---@return number # the value stored at the memory address as the specified type.
function pointer:get_qword() end

-- Sets the value at the memory address to the specified value of the given type.
---@param value number new value.
function pointer:set_byte(value) end

-- Sets the value at the memory address to the specified value of the given type.
---@param value number new value.
function pointer:set_word(value) end

-- Sets the value at the memory address to the specified value of the given type.
---@param value number new value.
function pointer:set_dword(value) end

-- Sets the value at the memory address to the specified value of the given type.
---@param value number new value.
function pointer:set_float(value) end

-- Sets the value at the memory address to the specified value of the given type.
---@param value number new value.
function pointer:set_qword(value) end

-- Retrieves the value stored at the memory address as the specified type.
---@return string # the value stored at the memory address as the specified type.
function pointer:get_string() end

-- Sets the value at the memory address to the specified value of the given type.
---@param value string new value.
function pointer:set_string(value) end

-- Creates a memory patch for modifying the value at the memory address with the specified value.
--The modified value is applied when you call the apply function on the lua_patch object.
--The original value is restored when you call the restore function on the lua_patch object.
---@param value number new value.
---@return lua_patch # memory patch instance for modifying the value at the memory address with the specified value. Can call apply / restore on the object.
function pointer:patch_byte(value) end

-- Creates a memory patch for modifying the value at the memory address with the specified value.
--The modified value is applied when you call the apply function on the lua_patch object.
--The original value is restored when you call the restore function on the lua_patch object.
---@param value number new value.
---@return lua_patch # memory patch instance for modifying the value at the memory address with the specified value. Can call apply / restore on the object.
function pointer:patch_word(value) end

-- Creates a memory patch for modifying the value at the memory address with the specified value.
--The modified value is applied when you call the apply function on the lua_patch object.
--The original value is restored when you call the restore function on the lua_patch object.
---@param value number new value.
---@return lua_patch # memory patch instance for modifying the value at the memory address with the specified value. Can call apply / restore on the object.
function pointer:patch_dword(value) end

-- Creates a memory patch for modifying the value at the memory address with the specified value.
--The modified value is applied when you call the apply function on the lua_patch object.
--The original value is restored when you call the restore function on the lua_patch object.
---@param value number new value.
---@return lua_patch # memory patch instance for modifying the value at the memory address with the specified value. Can call apply / restore on the object.
function pointer:patch_qword(value) end

---@return boolean # Returns true if the address is null.
function pointer:is_null() end

---@return boolean # Returns true if the address is not null.
function pointer:is_valid() end

-- Dereferences the memory address and returns a new pointer object pointing to the value at that address.
---@return pointer # A new pointer object pointing to the value at that address.
function pointer:deref() end

-- Retrieves the memory address stored in the pointer object.
---@return number # The memory address stored in the pointer object as a number.
function pointer:get_address() end


