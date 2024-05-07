# Table: rom.tolk

## Functions (5)

### `silence()`

Silences the screen reader.

**Example Usage:**
```lua
rom.tolk.silence()
```

### `output(str)`

Outputs text through the current screen reader driver.

- **Parameters:**
  - `str` (The text to output.)

**Example Usage:**
```lua
rom.tolk.output(str)
```

### `screen_read()`

Feeds to tolk the text from all currently visible game gui components.

**Example Usage:**
```lua
rom.tolk.screen_read()
```

### `get_lines_from_thing(thing_id)`

- **Parameters:**
  - `thing_id` (Id of the sgg): :Thing.

- **Returns:**
  - `table<int, string>`: Returns the lines inside a lua table

**Example Usage:**
```lua
table<int, string> = rom.tolk.get_lines_from_thing(thing_id)
```

### `on_button_hover(function)`

**Example Usage:**

```lua
rom.tolk.on_button_hover(function(lines)
     tolk.silence()
     for i = 1, #lines do
         tolk.output(lines[i])
     end
end)
```

- **Parameters:**
  - `function` (Function called when a button is hovered. The function must match signature): (table of string) -> returns nothing

**Example Usage:**
```lua
rom.tolk.on_button_hover(function)
```


