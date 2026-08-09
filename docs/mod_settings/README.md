# In-game mod settings - IDE schema & hints

Hell2Modding renders each mod's config file as a tab in the game's Options screen. Mods declare how
their settings look and read/write their values through a `config.lua` that returns two tables:

- `config` - the config keys and their default values.
- `configDesc` - the description/metadata for each setting (labels, help text, ranges, enums, ...).

Loading the `config.lua` also writes the mod's `.cfg`: it is created from the declared defaults on first run, and
rewritten on later runs to pick up newly added keys and descriptions, keeping any values already saved there.

> **Only keys with a `configDesc` entry are shown.** A key present in `config` but absent from `configDesc`
> is treated as internal state and is not displayed in the menu.

## VS Code type hints

To get schema validation and type hints to show in VS Code when you edit your `config.lua`, follow these steps:

1. Install the [Lua extension](https://marketplace.visualstudio.com/items?itemName=sumneko.lua) for VS Code.
2. In the extension's settings, add the folder containing the `config_schema.lua` from this repository to the `workspace.library` array.
3. Annotate the `configDesc` table with `---@type mod_settings.config_desc`.

## Field reference

Hover any field in the editor for its documentation. The available fields on a **setting** description are
below. Two other kinds of `configDesc` entry have their own fields and sections: **action buttons** (an
`action` function - see [Action buttons](#action-buttons)) and **virtual rows** (`virtual = true` with a
`text` or `get`/`set` callback and no config value - see [Virtual rows](#virtual-rows)).

| Field | Type | Purpose |
| --- | --- | --- |
| `displayName` | string \| localization table \| callback | Row label (defaults to a prettified key). Keep it to ~35 characters so it leaves room for the value shown to its right. |
| `description` | string \| localization table \| callback | Help text shown in the description box at the bottom of the screen while the row is highlighted. Keep it to ~450 characters. |
| `min`/`max` | number \| callback | Numeric bounds. If both are present the input will turn into a slider. |
| `step` | number \| callback | Slider/number step size (default 1). Will clamp user input automatically. |
| `values` | array \| callback | Enum: the values stored in the `.cfg` file. If present, the input will turn into a selector. |
| `labels` | array of (string \| localization table) \| callback | Display labels to show instead of the underlying `values` in the mod menu. Keep each to ~20 characters. |
| `order` | number \| callback | Sort key for custom ordering config entries in the menu, lowest first. Rows carrying an `order` are listed above those without one. When omitted, rows are sorted alphabetically by their `displayName`. |
| `hidden` | boolean | Hide the setting from the menu entirely. Static only - use `disabled` for rows that change state while the menu is open. |
| `disabled` | boolean \| callback | Grey the setting out (read-only) while true. Updates live while the menu is open. |
| `disabledDescription` | string \| localization table \| callback | Description shown in place of `description` while the setting is greyed by its own `disabled` field, to explain why. Falls back to `description` when omitted. |
| `restartRequired` | boolean | Force the user to restart the game after existing the mod menu if this setting was changed. |
| `editableContext` | `"any"` \| `"mainMenu"` \| `"inSave"` \| `"inHub"` | Restrict where the row can be edited: `"any"` (default), `"mainMenu"` (only from the main menu), `"inSave"` (only while a save is loaded), or `"inHub"` (only in the Crossroads). Outside of the allowed context the row shows as disabled. Restrict this if the mod or game would break if the setting is edited in the wrong context. Can also be set on a whole menu category, which restricts everything inside it. The "enabled" setting and any `restartRequired` settings are always treated as `"mainMenu"`. |
| `showAsPercentage` | boolean | Append "%" to the value. Usually used for min/max restricted number fields. |
| `isPercentage` | boolean | Show a 0..x value as 0..x00 *and* append "%". You don't need `showAsPercentage` when using this. |
| `onChanged` | `fun(key, new_value)` | Called after the setting is changed through the menu. |

Try to avoid naming your config keys after any of the reserved fields above.

## Menu grouping (`group` and `groups`)

The in-game menu layout can be **decoupled** from your config file structure. `configDesc` must still mirror the config
(`config.debugging.logLevel` is described at `configDesc.debugging.logLevel`), but where each row *appears* in the menu
can be independent:

- By default a row appears under its **config section** - so a nested config nests in the menu automatically.
- Add a **`group`** property to any entry (setting, action, or virtual row) to move it into a different menu
  category. It is a string for a single level, or an array for a nested path. This works for flat *and* nested config
  keys, and doesn't change where the value is stored in the .cfg file.
- Declare menu categories that do **not** exist as config sections in a top-level **`groups`** table (keyed by the id
  used in a `group`), each with an optional `displayName`, `description`, `order`, `disabled`,
  `disabledDescription`, `editableContext`, and further nested `groups`.

This lets you keep a flat config but present any grouping you like, or re-nest an already-nested config another way.

## Dynamic fields (functions)

Most fields can also be dynamically resolved through a function call, which is evaluated when the menu
is opened and refreshed (after any other setting is changed). This lets a setting react to the live game
state or to other settings. The function runs in your mod's environment, so it can read your `config`, 
and call functions in your `mod` or the `game` namespace.

Examples:

```lua
revive_count = {
  displayName = "Allowed Revives",
  min = 2,
  -- Max could be dependent on internal mod state
  max = function() return mod.CalcNumAllowedRevives() end,
  -- Perhaps mod.CalcNumAllowedRevives() accesses the game's GameState, in which case it would error when called in the Main Menu
  editableContext = "inSave",
},
revive_chance = {
  displayName = "Chance to automatically revive",
  description = "After dying without any Death Defiance left, you have a chance to automatically respawn at the start of the encounter."
  min = 0,
  max = 100,
  -- Row is greyed/disabled unless another config value is toggled on
  disabled = function() return not mod.config.easy_mode end,
  disabledDescription = "Enable \"Easy Mode\" above to change this.",
},
```

## Action buttons

A `configDesc` entry with an `action` function (and a key that has NO config value) renders as a button
that runs the callback when pressed, instead of editing a setting. It supports `displayName`, `description`,
`disabledDescription`, `order`, `editableContext`, and `disabled`.

```lua
apply_scaling = {
  action = function() mod.ApplyEasyModeScaling() end,
  displayName = "Apply Easy Mode Scaling",
  description = "Apply the scaling values above to the current save file.",
  editableContext = "inSave",
  disabled = function() return not mod.HasUnappliedEasyModeScaling() end,
  disabledDescription = "Change a scaling value above to enable this.",
},
```

## Virtual rows

A **virtual row** is a menu row that is not backed by a `config` value - its value comes from Lua callbacks.
Declare it as a `configDesc` entry whose key has no matching `config` value, marked `virtual = true`.

A virtual row is either **read-only** or **interactive**:

- **Read-only:** give it a `text` field - a string, or a function returning a string/number/boolean - for
  the value to show.
- **Interactive:** give it `get` (reads the current value) and `set` (writes the edited value). The widget is
  inferred from `get()`'s value and the metadata, exactly like a config setting is inferred from its config
  value: a **boolean** is a toggle, a **number** with `min`+`max` is a slider (otherwise a freetext field),
  and any type with a `values` list is an **enum selector**.

Interactive rows also support `disabled`, `disabledDescription`, `editableContext`, `showAsPercentage`/
`isPercentage`, and (for enums) `labels` - the same as config settings. `get`/`set`/`text` and the metadata
fields may be functions, re-evaluated live.

Two extra fields help interactive rows that have no `.cfg` backing:

- **`type`** - force the widget kind (`"boolean"`, `"number"`, `"string"`, or `"enum"`) when `get()` can
  return `nil` at build time and so cannot be inferred.
- **`default`** - the value the menu's **Reset** button restores the row to, applied through its `set()` callback.
  A virtual row without a `default` is left untouched by Reset.

```lua
-- Not chosen yet, so get() returns nil until the player picks one
mod.EasyModePreset = nil
local configDesc = {
  preset = {
    virtual = true,
    displayName = "Difficulty Preset",
    type = "enum",
    values = { "off", "balanced", "max" },
    default = "balanced",
    get = function() return mod.EasyModePreset end,
    set = function(v) mod.EasyModePreset = v end,
  },
}
```

## Reacting to changes (`onChanged`)

Give a setting an `onChanged` function to react when the player changes it through the options menu. Use it to
apply the new value to the live game, and/or to update **other rows'** dynamic fields.
It receives the setting's key and the new value:

```lua
local configDesc = {
  run_difficulty = {
    displayName = "Run difficulty",
    min = 0, max = 100,
    onChanged = function(key, newValue)
      if game.CurrentRun then 
        mod.ApplyNewRunDifficulty(newValue)
      end
    end,
  },
}
```

The callback fires AFTER the new value is stored and the `.cfg` is saved, so reading the setting back
(directly or via your `config` proxy) returns the new value. Note:

- It is **not called for other config writes** (e.g. from imgui or the config file) - only for edits made through
  this menu.
- Re-writing the same value is a no-op and does not fire, so an `onChanged` that writes another setting
  cannot loop.
- Errors thrown in the callback are logged and do not propagate into the game.

## Localization tables

Any `displayName`, `description`, or `labels` entry may be a table keyed by the game's language
codes (`en`, `de`, `el`, `es`, `fr`, `it`, `ja`, `ko`, `pl`, `pt-BR`, `ru`, `tr`, `uk`, `zh-CN`,
`zh-TW`). The menu resolves it to the currently set language, falling back to English.
