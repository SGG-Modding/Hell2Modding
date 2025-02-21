---@meta tolk

---@class (exact) rom.tolk

-- Silences the screen reader.
function tolk.silence() end

-- Outputs text through the current screen reader driver.
---@param str string The text to output.
function tolk.output(str) end

-- Feeds to tolk the text from all currently visible game gui components.
function tolk.screen_read() end

---@param thing_id integer Id of the sgg::Thing.
---@return table<int, string> # Returns the lines inside a lua table
function tolk.get_lines_from_thing(thing_id) end

-- **Example Usage:**
--
--```lua
--rom.tolk.on_button_hover(function(lines)
--     tolk.silence()
--     for i = 1, #lines do
--         tolk.output(lines[i])
--     end
--end)
--```
---@param function function Function called when a button is hovered. The function must match signature: (table of string) -> returns nothing
function tolk.on_button_hover(function) end


