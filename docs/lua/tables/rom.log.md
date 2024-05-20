# Table: rom.log

Table containing functions for printing to console / log file.

## Functions (5)

### `info(args)`

Logs an informational message.

- **Parameters:**
  - `args` (any)

**Example Usage:**
```lua
rom.log.info(args)
```

### `warning(args)`

Logs a warning message.

- **Parameters:**
  - `args` (any)

**Example Usage:**
```lua
rom.log.warning(args)
```

### `debug(args)`

Logs a debug message.

- **Parameters:**
  - `args` (any)

**Example Usage:**
```lua
rom.log.debug(args)
```

### `error(args)`

Logs an error message.

- **Parameters:**
  - `args` (any)

**Example Usage:**
```lua
rom.log.error(args)
```

### `refresh_filters()`

Refresh the log filters (Console and File) from the config file.

**Example Usage:**
```lua
rom.log.refresh_filters()
```


