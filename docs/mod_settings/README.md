# In-game mod settings - IDE schema & hints

Hell2Modding renders each mod's config file as a tab in the game's Options screen. Mods declare how
their settings look and read/write their values through a `config.lua` that returns two tables:

- `config` - the default values (and the live values once loaded).
- `configDesc` - the description/metadata for each setting (labels, help text, ranges, enums, ...).

> **Only keys with a `configDesc` entry are shown.** A key present in `config` but absent from `configDesc`
> is treated as internal state and is not displayed in the menu (a group whose keys are all undescribed
> produces no row at all). A `configDesc` entry can be either a metadata table or a plain description string -
> either counts as "described". The one exception is the mod's master `enabled` toggle, which is always shown
> so the mod stays toggleable even when it is not described. Reset to defaults likewise only affects the
> keys the menu shows.

This folder ships [LuaCATS](https://luals.github.io/wiki/annotations/) definitions
([`config_schema.lua`](./config_schema.lua)) so that VS Code gives you **autocomplete** and **hover
documentation** while you write `configDesc`, plus **field type checking** on settings you annotate
directly (see below).

## Enabling it in VS Code

1. Install the [Lua extension](https://marketplace.visualstudio.com/items?itemName=sumneko.lua) for VS Code.
2. In the extension's settings, add the folder containing the `config_schema.lua` to the `workspace.library` array.
3. Annotate the `configDesc` table with `---@type mod_settings.config_desc`.

## Field reference

Hover any field in the editor for its documentation. The available fields on a setting description are:

| Field | Type | Purpose |
| --- | --- | --- |
| `displayName` | string \| localization table \| callback | Row label (defaults to a prettified key). |
| `description` | string \| localization table \| callback | Help text in the description box. Keep each line ~35 chars to leave space for free-text input strings. |
| `min`/`max` | number \| callback | Numeric bounds. If both are present the input will turn into a slider (such as for volume control). |
| `step` | number \| callback | Slider/number step size (default 1). Will clamp user input automatically. |
| `values` | array \| callback | Enum: the values stored in the `.cfg` file. If present, the input will turn into a cycler (such as for the selected display). |
| `labels` | array of (string \| localization table) \| callback | Display labels parallel to `values`, only used in the in-game mod menu. |
| `order` | number \| callback | Sort key for custom ordering config entries in the menu, lower first. |
| `hidden` | boolean | Hide the setting from the menu entirely. Static only - use `disabled` for a condition that changes while the menu is open. |
| `disabled` | boolean \| callback | Grey the setting out (read-only) while true. Updates live while the menu is open. See below. |
| `freetext` | boolean | Force a bounded number to be a free-text entry instead of a slider. |
| `restartRequired` | boolean | Force the user to restart the game when this setting is changed. |
| `editableContext` | `"any"` \| `"mainMenu"` \| `"inSave"` | If this setting can be changed only in the main menu, only in a save, or in both. When the current context does not match, the row is shown read-only with a note. The "enabled" setting and any `restartRequired` settings are always treated as `"mainMenu"`. Defaults to `"any"`. |
| `showAsPercentage` | boolean | Append "%" to the value. |
| `isPercentage` | boolean | Show a 0..x value as 0..x00 *and* append "%". |
| `onChange` | `fun(key, new_value)` | Called after the setting is changed in the in-game menu. Use it to apply the change to the loaded run. See below. |

## Dynamic fields (functions)

Most fields can also be dynamically resolved through a function call, which is evaluated when the menu
is opened and refreshed (after any other setting is changed). This lets a setting react to the live game
state or to other settings. The following may be a **function** returning the value instead of a
literal: `displayName`, `description`, `min`, `max`, `step`, `values`, `labels`, `order`, and
`disabled`. The function runs in your mod's environment, so it can read your `config`, and call functions
in your `mod` or the `game` namespace.

Examples:

```lua
biome_count = {
  displayName = "Number of Regions",
  min = 2,
  max = function() return mod.MaxAllowedBiomeCount end, -- 8 or 12, resolved live
},
meta_reward_fix_chance_cap = {
  displayName = "Meta Reward Chance Cap",
  min = 30, max = 90,
  disabled = function() return not mod.config.meta_reward_fix end, -- greyed unless the fix toggle is on
},
```

Use `disabled` (greys the row in place) for a condition that changes while the menu is open. `hidden` is
static only - it is evaluated only when the menu builds, and cannot be changed dynamically.

## Action buttons

A `configDesc` entry with an `action` function (and a key that has NO config value) renders as a button
that runs the callback when pressed, instead of editing a setting. It supports `displayName`, `description`,
`order`, `editableContext`, and `disabled` (grey the button live, e.g. until a value has changed).

```lua
apply_scaling = {
  action = function() mod.ApplyLateBiomeScaling() end,
  displayName = "Apply Late Biome Scaling",
  description = "Apply the scaling values above to the current run.",
  editableContext = "inSave", -- greyed unless a save is loaded
},
```

## Reacting to changes (`onChange`)

Give a setting an `onChange` function to e.g. apply its new value to the live game when the player
changes it in the in-game options menu. It receives the setting's key and the new value:

```lua
local configDesc = {
  hermes_shrine_chance = {
    displayName = "Hermes Shrine Chance",
    min = 0, max = 100,
    editableContext = "inSave",
    onChange = function(key, new_value)
      mod.ApplyHermesShrineChance(new_value) -- re-apply the value to the live run
    end,
  },
}
```

The callback fires AFTER the new value is stored and the `.cfg` is saved, so reading the setting back
(directly or via your `config` proxy) returns the new value. It runs only for an edit made through the
in-game options menu, so:

- It is **never called in the main menu** - there is no loaded run to apply to, and Lua game-data edits
  are discarded when a save loads.
- It is **not called for other config writes** (e.g. from imgui or the config file).
- Re-writing the same value is a no-op and does not fire, so an `onChange` that writes another setting
  cannot loop.
- Errors thrown in the callback are logged and do not propagate into the game.

## Localization tables

Any `displayName`, `description`, or `labels` entry may be a table keyed by the game's language folder
codes (`en`, `de`, `el`, `es`, `fr`, `it`, `ja`, `ko`, `pl`, `pt-BR`, `ru`, `tr`, `uk`, `zh-CN`,
`zh-TW`). The menu resolves it to the current game language, falling back to English.
