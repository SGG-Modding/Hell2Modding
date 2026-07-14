# In-game mod settings - IDE schema & hints

Hell2Modding renders each mod's config file as a tab in the game's Options screen. Mods declare how
their settings look and read/write their values through a `config.lua` that returns two tables:

- `config` - the default values (and the live values once loaded).
- `configDesc` - the description/metadata for each setting (labels, help text, ranges, enums, ...).

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
| `display_name` | string \| localization table | Row label (defaults to a prettified key). |
| `description` | string \| localization table | Help text in the description box. Keep each line ~35 chars to leave space for free-text input strings. |
| `min`/`max` | number | Numeric bounds. If both are present the input will turn into a slider (such as for volume control). |
| `step` | number | Slider/number step size (default 1). Will clamp user input automatically. |
| `values` | array | Enum: the values stored in the `.cfg` file. If present, the input will turn into a cycler (such as for the selected display). |
| `labels` | array of (string \| localization table) | Display labels parallel to `values`, only used in the in-game mod menu. |
| `order` | number | Sort key for custom ordering config entries in the menu, lower first. |
| `hidden` | boolean | Hide the setting from the menu entirely. |
| `freetext` | boolean | Force a bounded number to be a free-text entry instead of a slider. |
| `restart_required` | boolean | Force the user to restart the game when this setting is changed. |
| `editable_context` | `"any"` \| `"main_menu"` \| `"in_save"` | If this setting can be changed only in the main menu, only in a save, or in both. When the current context does not match, the row is shown read-only with a note. The "enabled" setting and any `restart_required` settings are always treated as `"main_menu"`. Defaults to `"any"`. |
| `show_as_percentage` | boolean | Append "%" to the value. |
| `is_percentage` | boolean | Show a 0..x value as 0..x00 *and* append "%". |

## Localization tables

Any `display_name`, `description`, or `labels` entry may be a table keyed by the game's language folder
codes (`en`, `de`, `el`, `es`, `fr`, `it`, `ja`, `ko`, `pl`, `pt-BR`, `ru`, `tr`, `uk`, `zh-CN`,
`zh-TW`). The menu resolves it to the current game language, falling back to English.
