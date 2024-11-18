# Table: rom.memory

Table containing helper functions related to process memory.

## Functions (5)

### `scan_pattern(pattern)`

Scans the specified memory pattern within the target main module and returns a pointer to the found address.

- **Parameters:**
  - `pattern` (string): byte pattern (IDA format)

- **Returns:**
  - `pointer`: A pointer to the found address.

**Example Usage:**
```lua
pointer = rom.memory.scan_pattern(pattern)
```

### `allocate(size)`

- **Parameters:**
  - `size` (integer): The number of bytes to allocate on the heap.

- **Returns:**
  - `pointer`: A pointer to the newly allocated memory.

**Example Usage:**
```lua
pointer = rom.memory.allocate(size)
```

### `free(ptr)`

- **Parameters:**
  - `ptr` (pointer): The pointer that must be freed.

**Example Usage:**
```lua
rom.memory.free(ptr)
```

### `dynamic_hook(hook_name, return_type, param_types, target_func_ptr, pre_callback, post_callback)`

**Example Usage:**
```lua
local ptr = memory.scan_pattern("some ida sig")
rom.memory.dynamic_hook("test_hook", "float", {"const char*"}, ptr,
function(ret_val, str)

     --str:set("replaced str")
     ret_val:set(69.69)
     log.info("pre callback from lua", ret_val:get(), str:get())

     -- false for skipping the original function call
     return false
end,
function(ret_val, str)
     log.info("post callback from lua 1", ret_val:get(), str:get())
     ret_val:set(79.69)
     log.info("post callback from lua 2", ret_val:get(), str:get())
end)
```

- **Parameters:**
  - `hook_name` (string): The name of the hook.
  - `return_type` (string): Type of the return value of the detoured function.
  - `param_types` (table<string>): Types of the parameters of the detoured function.
  - `target_func_ptr` (memory.pointer): The pointer to the function to detour.
  - `pre_callback` (function): The function that will be called before the original function is about to be called. The callback must match the following signature: ( return_value (value_wrapper), arg1 (value_wrapper), arg2 (value_wrapper), ... ) -> Returns true or false (boolean) depending on whether you want the original function to be called.
  - `post_callback` (function): The function that will be called after the original function is called (or just after the pre callback is called, if the original function was skipped). The callback must match the following signature: ( return_value (value_wrapper), arg1 (value_wrapper), arg2 (value_wrapper), ... ) -> void

**Example Usage:**
```lua
rom.memory.dynamic_hook(hook_name, return_type, param_types, target_func_ptr, pre_callback, post_callback)
```

### `dynamic_call(return_type, param_types, target_func_ptr)`

**Example Usage:**
```lua
local ptr = memory.scan_pattern("some ida sig")
local func_to_call_test_global_name = rom.memory.dynamic_call("bool", {"const char*", "float", "double", "void*", "int8_t", "int64_t"}, ptr)
local call_res_test = _G[func_to_call_test_global_name]("yepi", 69.025, 420.69, 57005, 126, 1195861093)
log.info("call_res_test: ", call_res_test)
```

- **Parameters:**
  - `return_type` (string): Type of the return value of the function to call.
  - `param_types` (table<string>): Types of the parameters of the function to call.
  - `target_func_ptr` (memory.pointer): The pointer to the function to call.

**Example Usage:**
```lua
rom.memory.dynamic_call(return_type, param_types, target_func_ptr)
```


