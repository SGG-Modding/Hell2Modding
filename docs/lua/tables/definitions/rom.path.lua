---@meta path

-- Table containing helpers for manipulating file or directory paths
---@class (exact) rom.path

-- Combines strings into a path.
---@param path string Any amount of string is accepted.
---@return string # Returns the combined path
function path.combine(path) end

-- Retrieves the parent directory of the specified path, including both absolute and relative paths.
---@param path string The path for which to retrieve the parent directory.
---@return string # Returns the parent path
function path.get_parent(path) end

---@param root_path string The path to the directory to search.
---@return string table # Returns the names of subdirectories under the given root_path
function path.get_directories(root_path) end

---@param root_path string The path to the directory to search.
---@return string table # Returns the names of all the files under the given root_path
function path.get_files(root_path) end

---@param path string The path for which to retrieve the filename.
---@return string # Returns the filename identified by the path.
function path.filename(path) end

---@param path string The path for which to retrieve the stem.
---@return string # Returns the stem of the filename identified by the path (i.e. the filename without the final extension).
function path.stem(path) end

---@param path string The path to the new directory to create.
---@return boolean # true if a directory was newly created for the directory p resolves to, false otherwise.
function path.create_directory(path) end

---@param path string The path to check.
---@return boolean # true if the path exists, false otherwise.
function path.exists(path) end


