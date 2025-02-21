---@meta memory

-- Table containing helper functions related to process memory.
---@class (exact) rom.memory

-- Scans the specified memory pattern within the target main module and returns a pointer to the found address.
---@param pattern string byte pattern (IDA format)
---@return pointer # A pointer to the found address.
function memory.scan_pattern(pattern) end

---@param size integer The number of bytes to allocate on the heap.
---@return pointer # A pointer to the newly allocated memory.
function memory.allocate(size) end

---@param ptr pointer The pointer that must be freed.
function memory.free(ptr) end

-- **Example Usage:**
--```lua
--local ptr = memory.scan_pattern("some ida sig")
---- Check the implementation of the asmjit::TypeId get_type_id function if you are unsure what to use for return type / parameters types
--rom.memory.dynamic_hook("test_hook", "float", {"const char*"}, ptr,
--function(ret_val, str)
--
--     --str:set("replaced str")
--     ret_val:set(69.69)
--     log.info("pre callback from lua", ret_val:get(), str:get())
--
--     -- false for skipping the original function call
--     return false
--end,
--function(ret_val, str)
--     log.info("post callback from lua 1", ret_val:get(), str:get())
--     ret_val:set(79.69)
--     log.info("post callback from lua 2", ret_val:get(), str:get())
--end)
--```
---@param hook_name string The name of the hook.
---@param return_type string Type of the return value of the detoured function.
---@param param_types table<string> Types of the parameters of the detoured function.
---@param target_func_ptr memory.pointer The pointer to the function to detour.
---@param pre_callback function or nil Optional. The function that will be called before the original function is about to be called. The callback must match the following signature: ( return_value (value_wrapper), arg1 (value_wrapper), arg2 (value_wrapper), ... ) -> Returns true or false (boolean) depending on whether you want the original function to be called.
---@param post_callback function or nil Optional. The function that will be called after the original function is called (or just after the pre callback is called, if the original function was skipped). The callback must match the following signature: ( return_value (value_wrapper), arg1 (value_wrapper), arg2 (value_wrapper), ... ) -> void
function memory.dynamic_hook(hook_name, return_type, param_types, target_func_ptr, pre_callback, post_callback) end

-- **Example Usage:**
--```lua
--local ptr = memory.scan_pattern("some ida sig")
--gm.dynamic_hook_mid("test_hook", {"rax", "rcx", "[rcx+rdx*4+11]"}, {"int", "RValue*", "int"}, 0, ptr, function(args)
--     log.info("trigger", args[1]:get(), args[2].value, args[3]:set(1))
--end)
--```
--But scan_pattern may be affected by the other hooks.
---@param hook_name string The name of the hook.
---@param param_captures_targets table<string> Addresses of the parameters which you want to capture.
---@param param_captures_types table<string> Types of the parameters which you want to capture.
---@param stack_restore_offset int An offset used to restore stack, only need when you want to customize the jump location.
---@param target_func_ptr memory.pointer The pointer to the function to detour.
---@param mid_callback function The function that will be called when the program reaches the position. The callback must match the following signature: ( args (can be a value_wrapper, or a lua usertype directly, depending if you used `add_type_info_from_string` through some c++ code and exposed it to the lua vm) ) -> Returns memory.pointer if you want to customize the jump location. Be careful when customizing the jump location, you need to restore the registers and the stack before the jump.
---@return add(246) # 
function memory.dynamic_hook_mid(hook_name, param_captures_targets, param_captures_types, stack_restore_offset, target_func_ptr, mid_callback) end

-- **Example Usage:**
--```lua
---- the sig in this example leads to an implementation of memcpy_s
--local ptr = memory.scan_pattern("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 49 8B D9 49 8B F0 48 8B FA")
--if ptr:is_valid() then
--     local dest_size = 8
--     local dest_ptr = memory.allocate(dest_size)
--     dest_ptr:set_qword(0)
--
--     local src_size = 8
--     local src_ptr = memory.allocate(src_size)
--     src_ptr:set_qword(123)
--
--     -- Check the implementation of the asmjit::TypeId get_type_id function if you are unsure what to use for return type / parameters types
--     local func_to_call_test_global_name = rom.memory.dynamic_call("int", {"void*", "uint64_t", "void*", "uint64_t"}, ptr)
--     -- print zero.
--     log.info(dest_ptr:get_qword())
--     -- note: don't pass memory.pointer objects directly when you call the function, but use get_address() instead.
--     local call_res_test = _G[func_to_call_test_global_name](dest_ptr:get_address(), dest_size, src_ptr:get_address(), src_size)
--     -- print 123.
--     log.info(dest_ptr:get_qword())
--end
--```
---@param return_type string Type of the return value of the function to call.
---@param param_types table<string> Types of the parameters of the function to call.
---@param target_func_ptr memory.pointer The pointer to the function to call.
---@return string # Key name of the function that you can now call from lua.
function memory.dynamic_call(return_type, param_types, target_func_ptr) end

-- **Example Usage:**
--```lua
--memory.dynamic_hook("test", "RValue*", {"CInstance*","CInstance*","RValue*","int","RValue**"},
--ptr, function (ret_val, skill, player, result, arg_num, args_ptr)
--     log.info(rom.memory.resolve_pointer_to_type(memory.get_usertype_pointer(skill), "YYObjectBase*").skill_id)
--     log.info(rom.memory.resolve_pointer_to_type(args_ptr:deref():get_address(), "RValue*").value)
--     log.info(rom.memory.resolve_pointer_to_type(args_ptr:add(8):deref():get_address(), "RValue*").value)
--end)
--```
---@param target_address number The object target as a lua number.
---@param target_type string Target type (must be a pointer type).
---@return lua usertype. # 
function memory.resolve_pointer_to_type(target_address, target_type) end

---@param usertype_object any_usertype A lua usertype instance.
---@return number # The object address as a lua number.
function memory.get_usertype_pointer(usertype_object) end


