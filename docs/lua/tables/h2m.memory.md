# Table: h2m.memory

Table containing helper functions related to process memory.

## Functions (3)

### `scan_pattern(pattern)`

Scans the specified memory pattern within the target main module and returns a pointer to the found address.

- **Parameters:**
  - `pattern` (string): byte pattern (IDA format)

- **Returns:**
  - `pointer`: A pointer to the found address.

**Example Usage:**
```lua
pointer = h2m.memory.scan_pattern(pattern)
```

### `allocate(size)`

- **Parameters:**
  - `size` (integer): The number of bytes to allocate on the heap.

- **Returns:**
  - `pointer`: A pointer to the newly allocated memory.

**Example Usage:**
```lua
pointer = h2m.memory.allocate(size)
```

### `free(ptr)`

- **Parameters:**
  - `ptr` (pointer): The pointer that must be freed.

**Example Usage:**
```lua
h2m.memory.free(ptr)
```


