# Table: rom.gpk

## Functions (2)

### `decompress_folder(input_folder_path, output_folder_path)`

- **Parameters:**
  - `input_folder_path` (string): Path to folder containing gpk compressed files.
  - `output_folder_path` (string): Path to the folder where the decompressed files will be placed. The folder is created if needed.

**Example Usage:**
```lua
rom.gpk.decompress_folder(input_folder_path, output_folder_path)
```

### `decompress_file(input_file_path, output_folder_path)`

- **Parameters:**
  - `input_file_path` (string): Path to a gpk file.
  - `output_folder_path` (string): Path to the folder where the decompressed files will be placed. The folder is created if needed.

**Example Usage:**
```lua
rom.gpk.decompress_file(input_file_path, output_folder_path)
```


