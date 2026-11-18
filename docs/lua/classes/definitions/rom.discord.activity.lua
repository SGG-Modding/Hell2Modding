---@meta discord.activity

-- Table describing a Discord activity in UTF-8
---@class (exact) rom.discord.activity
---@field details string # Activity description, at most 128 bytes
---@field large_image string # Artwork key uploaded to the Discord application, at most 32 bytes
---@field large_text string # Large image hover text, at most 128 bytes
---@field state string|nil # Optional secondary status, at most 128 bytes
---@field start_timestamp integer|nil # Optional positive 64-bit Unix timestamp in seconds for the elapsed gameplay timer



