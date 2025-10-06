---@meta gpk

---@class (exact) rom.gpk

---@param input_folder_path string Path to folder containing gpk compressed files.
---@param output_folder_path string Path to the folder where the decompressed files will be placed. The folder is created if needed.
function gpk.decompress_folder(input_folder_path, output_folder_path) end

---@param input_file_path string Path to a gpk file.
---@param output_folder_path string Path to the folder where the decompressed files will be placed. The folder is created if needed.
function gpk.decompress_file(input_file_path, output_folder_path) end


