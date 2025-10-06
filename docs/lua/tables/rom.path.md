# Table: rom.path

Table containing helpers for manipulating file or directory paths

## Functions (9)

### `combine(path)`

Combines strings into a path.

- **Parameters:**
  - `path` (string): Any amount of string is accepted.

- **Returns:**
  - `string`: Returns the combined path

**Example Usage:**
```lua
string = rom.path.combine(path)
```

### `get_parent(path)`

Retrieves the parent directory of the specified path, including both absolute and relative paths.

- **Parameters:**
  - `path` (string): The path for which to retrieve the parent directory.

- **Returns:**
  - `string`: Returns the parent path

**Example Usage:**
```lua
string = rom.path.get_parent(path)
```

### `get_directories(root_path)`

- **Parameters:**
  - `root_path` (string): The path to the directory to search.

- **Returns:**
  - `string table`: Returns the names of subdirectories under the given root_path

**Example Usage:**
```lua
string table = rom.path.get_directories(root_path)
```

### `get_files(root_path)`

- **Parameters:**
  - `root_path` (string): The path to the directory to search.

- **Returns:**
  - `string table`: Returns the names of all the files under the given root_path

**Example Usage:**
```lua
string table = rom.path.get_files(root_path)
```

### `filename(path)`

- **Parameters:**
  - `path` (string): The path for which to retrieve the filename.

- **Returns:**
  - `string`: Returns the filename identified by the path.

**Example Usage:**
```lua
string = rom.path.filename(path)
```

### `stem(path)`

- **Parameters:**
  - `path` (string): The path for which to retrieve the stem.

- **Returns:**
  - `string`: Returns the stem of the filename identified by the path (i.e. the filename without the final extension).

**Example Usage:**
```lua
string = rom.path.stem(path)
```

### `create_directory(path)`

- **Parameters:**
  - `path` (string): The path to the new directory to create.

- **Returns:**
  - `boolean`: true if a directory was newly created for the directory p resolves to, false otherwise.

**Example Usage:**
```lua
boolean = rom.path.create_directory(path)
```

### `exists(path)`

- **Parameters:**
  - `path` (string): The path to check.

- **Returns:**
  - `boolean`: true if the path exists, false otherwise.

**Example Usage:**
```lua
boolean = rom.path.exists(path)
```

### `add_file_watcher(path, callback)`

Registers a callback that will be called when a file changes.
**Example Usage:**
```lua
rom.path.add_file_watcher(_ENV["!config_mod_folder_path"], function (file_name, timestamp)
		log.info(file_name, timestamp)
end)
```

- **Parameters:**
  - `path` (string): The path to add file watcher.
  - `callback` (function): callback that match signature function ( file_name, timestamp ).

**Example Usage:**
```lua
rom.path.add_file_watcher(path, callback)
```


