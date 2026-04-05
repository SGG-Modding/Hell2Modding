# Table: rom.tethers

## Functions (3)

### `add(source_id, target_id, distance, retract_speed, elasticity, track_z_ratio)`

Creates a tether constraint between two game objects.

- **Parameters:**
  - `source_id` (integer): The Thing Id of the object to be constrained.
  - `target_id` (integer): The Thing Id of the anchor object.
  - `distance` (number): Maximum distance before the tether pulls the source back.
  - `retract_speed` (number): optional. Retraction speed when beyond distance. Default 0.
  - `elasticity` (number): optional. Elasticity coefficient for spring tethers. Default 0.
  - `track_z_ratio` (number): optional. Z-axis tracking ratio (0.0-1.0). Default 0.

- **Returns:**
  - `boolean`: Whether the tether was created successfully.

**Example Usage:**
```lua
boolean = rom.tethers.add(source_id, target_id, distance, retract_speed, elasticity, track_z_ratio)
```

### `remove(source_id, target_id)`

Removes a specific tether constraint between two game objects.

- **Parameters:**
  - `source_id` (integer): The Thing Id of the constrained object.
  - `target_id` (integer): The Thing Id of the anchor object.

- **Returns:**
  - `boolean`: Whether the tether was removed.

**Example Usage:**
```lua
boolean = rom.tethers.remove(source_id, target_id)
```

### `remove_all(thing_id)`

Removes all tether constraints involving the specified object.

- **Parameters:**
  - `thing_id` (integer): The Thing Id to remove all tethers from.

**Example Usage:**
```lua
rom.tethers.remove_all(thing_id)
```


