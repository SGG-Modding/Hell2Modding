# Table: rom.game

## Functions (1)

### `LoadPackages(args)`

**Example Usage:**
```lua
local package_path = rom.path.combine(_PLUGIN.plugins_data_mod_folder_path, _PLUGIN.guid)
-- Example package_path: "C:/Program Files (x86)/Steam/steamapps/common/Hades II/Ship/ReturnOfModding/plugins_data/AuthorName-ModName/AuthorName-ModName"
rom.rom.game.LoadPackages{Name = package_path}
```

- **Parameters:**
  - `args` (table<string, string>): Table contains string key `Name` and its associated `string` value. Associated value should be a full path to the package to load, without the extension. The filename of the .pkg and the .pkg_manifest files should contains the guid of the owning mod. Example `AuthorName-ModName-MyMainPackage`

**Example Usage:**
```lua
rom.game.LoadPackages(args)
```


