# Table: h2m

## Functions (2)

### `on_pre_import(function)`

The passed function will be called before the game loads a .lua script from the game's Content/Scripts folder.
The _ENV returned (if not nil) by the passed function gives you a way to define the _ENV of this lua script.

- **Parameters:**
  - `function` (signature (string file_name, current_ENV_for_this_import) return nil or _ENV)

**Example Usage:**
```lua
h2m.on_pre_import(function)
```

### `on_pre_import(function)`

The passed function will be called after the game loads a .lua script from the game's Content/Scripts folder.

- **Parameters:**
  - `function` (signature (string file_name))

**Example Usage:**
```lua
h2m.on_pre_import(function)
```


