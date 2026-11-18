# Table: rom.discord

Discord Rich Presence transport
Validation and service errors are logged and return false
A return value of true means the request was accepted locally, not necessarily delivered to Discord

## Functions (2)

### `start(application_id, menu_activity)`

Starts presence or updates its main-menu fallback

- **Parameters:**
  - `application_id` (string): Nonzero decimal Discord application ID
  - `menu_activity` (rom.discord.activity): Initial activity and main-menu fallback, with an omitted timestamp defaulting to the current time

- **Returns:**
  - `boolean`: true if accepted, false if validation or startup fails

**Example Usage:**
```lua
boolean = rom.discord.start(application_id, menu_activity)
```

### `set_activity(value)`

Queues the latest activity, suppressing duplicates and waiting only for any outstanding command's acknowledgement

- **Parameters:**
  - `value` (rom.discord.activity): Replacement activity, with an omitted timestamp inheriting from the current activity or defaulting to the current time if no activity exists

- **Returns:**
  - `boolean`: true if queued, false if validation fails

**Example Usage:**
```lua
boolean = rom.discord.set_activity(value)
```


