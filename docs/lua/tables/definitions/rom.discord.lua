---@meta discord

-- Discord Rich Presence transport
--Validation and service errors are logged and return false
--A return value of true means the request was accepted locally, not necessarily delivered to Discord
---@class (exact) rom.discord

-- Starts presence or updates its main-menu fallback
---@param application_id string Nonzero decimal Discord application ID
---@param menu_activity rom.discord.activity Initial activity and main-menu fallback, with an omitted timestamp defaulting to the current time
---@return boolean # true if accepted, false if validation or startup fails
function discord.start(application_id, menu_activity) end

-- Queues the latest activity, suppressing duplicates and waiting only for any outstanding command's acknowledgement
---@param value rom.discord.activity Replacement activity, with an omitted timestamp inheriting from the current activity or defaulting to the current time if no activity exists
---@return boolean # true if queued, false if validation fails
function discord.set_activity(value) end


