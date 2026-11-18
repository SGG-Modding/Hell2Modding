# Class: rom.discord.activity

Table describing a Discord activity in UTF-8

## Fields (5)

### `details`

Activity description, at most 128 bytes

- Type: `string`

### `large_image`

Artwork key uploaded to the Discord application, at most 32 bytes

- Type: `string`

### `large_text`

Large image hover text, at most 128 bytes

- Type: `string`

### `state`

Optional secondary status, at most 128 bytes

- Type: `string|nil`

### `start_timestamp`

Optional positive 64-bit Unix timestamp in seconds for the elapsed gameplay timer

- Type: `integer|nil`

