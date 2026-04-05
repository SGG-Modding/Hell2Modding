---@meta tethers

---@class (exact) rom.tethers

-- Creates a tether constraint between two game objects.
---@param source_id integer The Thing Id of the object to be constrained.
---@param target_id integer The Thing Id of the anchor object.
---@param distance number Maximum distance before the tether pulls the source back.
---@param retract_speed number optional. Retraction speed when beyond distance. Default 0.
---@param elasticity number optional. Elasticity coefficient for spring tethers. Default 0.
---@param track_z_ratio number optional. Z-axis tracking ratio (0.0-1.0). Default 0.
---@return boolean # Whether the tether was created successfully.
function tethers.add(source_id, target_id, distance, retract_speed, elasticity, track_z_ratio) end

-- Removes a specific tether constraint between two game objects.
---@param source_id integer The Thing Id of the constrained object.
---@param target_id integer The Thing Id of the anchor object.
---@return boolean # Whether the tether was removed.
function tethers.remove(source_id, target_id) end

-- Removes all tether constraints involving the specified object.
---@param thing_id integer The Thing Id to remove all tethers from.
function tethers.remove_all(thing_id) end


