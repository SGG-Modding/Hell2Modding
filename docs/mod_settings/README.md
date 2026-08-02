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

Hover any field in the editor for its documentation. The available fields on a **setting** description are
below. Two other kinds of `configDesc` entry have their own fields and sections: **action buttons** (an
`action` function - see [Action buttons](#action-buttons)) and **virtual rows** (`virtual = true` with a
`text` or `get`/`set` callback and no config value - see [Virtual rows](#virtual-rows)).

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
| `disabledDescription` | string \| localization table \| callback | Description shown in place of `description` while the setting is greyed by its own `disabled` field, to explain why. Falls back to `description` when omitted. Not used for context-restricted or mod-disabled rows. |
| `freetext` | boolean | Force a bounded number to be a free-text entry instead of a slider. |
| `restartRequired` | boolean | Force the user to restart the game when this setting is changed. |
| `editableContext` | `"any"` \| `"mainMenu"` \| `"inSave"` \| `"inHub"` | Restrict when this setting can be changed: `"any"` (default), `"mainMenu"` (only from the main menu), `"inSave"` (only while a save is loaded - both in the Crossroads and mid-run), or `"inHub"` (only while in the Crossroads). When the current context does not match, the row is shown read-only with a note. The "enabled" setting and any `restartRequired` settings are always treated as `"mainMenu"`. |
| `showAsPercentage` | boolean | Append "%" to the value. |
| `isPercentage` | boolean | Show a 0..x value as 0..x00 *and* append "%". |
| `onChange` | `fun(key, new_value)` | Called after the setting is changed in the in-game menu. Use it to apply the change to the loaded run. See below. |

## Config keys named like reserved fields

If you happen to name a config key after one of the reserved fields above, the menu will still render them correctly,
but it is highly recommended to **not** use reserved field names as config keys to prevent confusion and potential edge
case breakage.

## Menu grouping (`group` and `groups`)

The in-game menu layout can be **decoupled** from your config file structure. `configDesc` must still mirror the config
(`config.debugging.logLevel` is described at `configDesc.debugging.logLevel`), but where each row *appears* in the menu
is independent:

- By default a row appears under its **config section** - so a nested config nests in the menu automatically.
- Add a **`group`** property to any entry (setting, action, or virtual row) to move it into a different menu
  category. It is a string for a single level, or an array for a nested path. This works for flat *and* nested config
  keys, and doesn't change where the value is stored in the .cfg file.
- Declare menu categories that do **not** exist as config sections in a top-level **`groups`** table (keyed by the id
  used in a `group`), each with an optional `displayName`, `description`, `order`, and nested `groups`.

This lets you keep a flat config but present any grouping you like, or re-nest an already-nested config another way.

## Dynamic fields (functions)

Most fields can also be dynamically resolved through a function call, which is evaluated when the menu
is opened and refreshed (after any other setting is changed). This lets a setting react to the live game
state or to other settings. The following may be a **function** returning the value instead of a
literal: `displayName`, `description`, `disabledDescription`, `min`, `max`, `step`, `values`, `labels`, `order`, and
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
  disabledDescription = "Enable \"Fix Meta Reward Count\" above to change this.", -- shown while greyed
},
```

Use `disabled` (greys the row in place) for a condition that changes while the menu is open. `hidden` is
static only - it is evaluated only when the menu builds, and cannot be changed dynamically. Pair `disabled`
with `disabledDescription` to explain why the row is greyed: while the row is disabled by its own `disabled`
field, the description box shows `disabledDescription` instead of the normal `description` (falling back to
`description` if you omit it). A greyed row still highlights on mouse hover so the note is readable. This does
not apply to context-restricted rows (which show their own "change it in X" note) or while the whole mod is
disabled.

## Action buttons

A `configDesc` entry with an `action` function (and a key that has NO config value) renders as a button
that runs the callback when pressed, instead of editing a setting. It supports `displayName`, `description`,
`disabledDescription`, `order`, `editableContext`, and `disabled` (grey the button live, e.g. until a value
has changed).

```lua
apply_scaling = {
  action = function() mod.ApplyLateBiomeScaling() end,
  displayName = "Apply Late Biome Scaling",
  description = "Apply the scaling values above to the current run.",
  editableContext = "inSave", -- greyed unless a save is loaded
  disabled = function() return not mod.HasUnappliedScaling() end, -- greyed until a value changes
  disabledDescription = "Change a scaling value above to enable this.", -- shown while greyed
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
  value: a **boolean** is a toggle, a **number** with `min`+`max` is a slider (otherwise a number box), and any
  type with a `values` list is an **enum picker**.

Interactive rows also support `disabled`, `disabledDescription`, `editableContext`, `showAsPercentage`/
`isPercentage`, and (for enums) `labels` - the same as config settings. `get`/`set`/`text` and the metadata
fields may be functions, re-evaluated live.

Two extra fields help interactive rows that have no `.cfg` backing:

- **`type`** - force the widget kind (`"boolean"`, `"number"`, `"string"`, or `"enum"`) when `get()` can
  return `nil` at build time and so cannot be inferred. Only needed then; `"enum"` still requires `values`.
- **`default`** - the value the menu **Reset** restores the row to, applied through its `set()` callback.
  Config-backed settings recover their own default automatically; a virtual row without a `default` is left
  untouched by Reset.

```lua
local preset = nil -- not chosen yet, so get() returns nil until the player picks one
local configDesc = {
  preset = {
    virtual = true, displayName = "Preset",
    type = "enum", values = { "off", "balanced", "max" }, default = "balanced",
    get = function() return preset end,
    set = function(v) preset = v end,
  },
}
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
