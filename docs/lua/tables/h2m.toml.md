## Usage

### Decoding

```lua
local tomlStr = [[
a = 1275892
b = 'Hello, World!'
c = true
d = 124.2548

[e]
f = [ 1, 2, 3, '4', 5.142 ]
g = 1979-05-27
h = 07:32:00
i = 1979-05-27T07:32:00-07:00
]]

-- Decode from string
local succeeded, table = pcall(toml.decode, tomlStr)

-- Decode from file
succeeded, table = pcall(toml.decodeFromFile, "configuration.toml")

if succeeded then
-- Use `table`.
else
-- Error details are in `table`.
end
```

#### Decoding Options

##### `temporalTypesAsUserData`

-   `temporalTypesAsUserData = true`: The userdata types `toml.Date`, `toml.Time`, and `toml.DateTime` are used to represent TOML date and time types.

-   `temporalTypesAsUserData = false`: Lua tables are used to represent TOML date and time types.

> The default value is `true`

##### `formattedIntsAsUserData`

-   `formattedIntsAsUserData = true`: The userdata type `toml.Int` is used to represent integers in octal, binary, or hexadecimal format.
-   `formattedIntsAsUserData = false`: Integers in octal, binary, or hexadecimal format will be represented in decimal.

> The default value is `false`

```lua
local tomlStr = [[
date = 1979-05-27
time = 07:32:00
datetime = 1979-05-27T07:32:00-07:00

hexadecimal = 0x16C3
binary = 0b110110011011
octal = 0x169F
]]

local table1 = toml.decode(tomlStr, { temporalTypesAsUserData = true, formattedIntsAsUserData = true })
local table2 = toml.decode(tomlStr, { temporalTypesAsUserData = false, formattedIntsAsUserData = false })
```

### Encoding

```lua
-- Inline tables: https://toml.io/en/v1.0.0#inline-table
local inlineTable = {
	a = 1275892,
	b = "Hello, World!",
	c = true,
	d = 124.2548,
}

-- Make the table inline.
setmetatable(inlineTable, { inline = true })

local table = {

	e = {
		f = { 1, 2, 3, "4", 5.142 },
		g = toml.Date.new(1979,   05,     27),
		--                year   month   day

		h = toml.Time.new( 7,     32,      0,        0),
		--                hour   minute  second   nanoSecond

		i = toml.DateTime.new(
			toml.Date.new(1979, 05, 27),
			toml.Time.new(7, 32, 0, 0),

			toml.TimeOffset.new(  -7,     0)
			--                   hour   minute
		)
	},
	inlineTable = inlineTable
}

-- Encode to string
local succeeded, documentOrErrorMessage = pcall(toml.encode, table)

-- Encode to file, this will **append** to the file.
succeeded, documentOrErrorMessage = pcall(toml.encodeToFile, table, "configuration.toml")

-- Encode to file, this will **overwrite** the file.
succeeded, documentOrErrorMessage = pcall(toml.encodeToFile, table, { file = "configuration.toml", overwrite = true })

if succeeded then
	-- Successfully encoded to string / wrote to file
	print(tomlDocumentOrErrorMessage)
else
-- Error occurred
	print(tomlDocumentOrErrorMessage)
end

--[[
inlineTable = { a = 1275892, b = "Hello, World!", c = true, d = 124.2548 }

[e]
f = [ 1, 2, 3, "4", 5.1420000000000003 ]
g = 1979-05-27
h = 07:32:00
i = 1979-05-27T07:32:00-07:00
--]]
```

### Error Handling

```lua
local tomlStr = [[
a = 1275892
b = 'Hello, World!'
c = true
d = 124. # <-- ERROR: "Expected decimal digit"

[e]
f = [ 1, 2, 3, '4', 5.142 ]
g = 1979-05-27
h = 07:32:00
i = 1979-05-27T07:32:00-07:00
]]

local succeeded, table = pcall(toml.decode, tomlStr)

if succeeded then
	-- Use decoded table.
else
	-- Error details are in `table`.
end
```

### Inline Tables

Use `setmetatable(myTable, { inline = true })` to create an [inline table](https://toml.io/en/v1.0.0#inline-table).

### TOML Conversion

```lua
local tomlStr = [[
a = 1275892
b = 'Hello, World!'
c = true
d = 124.2548

[e]
f = [ 1, 2, 3, '4', 5.142 ]
g = 1979-05-27
h = 07:32:00
i = 1979-05-27T07:32:00-07:00
]]
```

#### JSON

```lua
-- Convert from a string
local json = toml.toJSON(tomlStr)

-- or from a table
json = toml.toJSON(toml.decode(tomlStr))

print(json)
```

#### YAML

```lua
local yaml = toml.toYAML(tomlStr)
yaml = toml.toYAML(toml.decode(tomlStr))
print(yaml)
```

### Output Formatting

#### Formatting Integers

```lua
local normalIntegers = {
	int1 = 2582
	int2 = 3483
	int3 = 5971
}
print(toml.encode(normalIntegers))
--[[
int1 = 2582
int2 = 3483
int3 = 5791
--]]

local formattedIntegers = {
	int1 = toml.Int.new(2582, toml.formatting.int.octal),
	int2 = toml.Int.new(3483, toml.formatting.int.binary),
	int3 = toml.Int.new(5791, toml.formatting.int.hexadecimal)
}

print(toml.encode(formattedIntegers))
--[[
int1 = 0o5026
int2 = 0b110110011011
int3 = 0x169F
--]]

-- Use `int` and `flags` properties to assign and retrieve flags and integers.
local int = formattedIntegers.int1.int
local flags = formattedIntegers.int1.flags

formattedIntegers.int1.int = 5827
formattedIntegers.int1.flags = toml.formatting.int.hexadecimal

print(toml.encode(formattedIntegers))
--[[
int1 = 0x16C3
int2 = 0b110110011011
int3 = 0x169F
--]]
```

#### Formatting TOML, JSON, or YAML

`toml.encode`, `toml.encodeToFile`, `toml.toJSON`, and `toml.toYAML` all take an optional second parameter: a table containing keys that disable or enable different formatting options.
Passing an empty table removes all options, while not providing a table will use the default options.

```lua
{
	--- Dates and times will be emitted as quoted strings.
	quoteDatesAndTimes = false,

	--- Infinities and NaNs will be emitted as quoted strings.
	quoteInfinitesAndNaNs = false,

	--- Strings will be emitted as single-quoted literal strings where possible.
	allowLiteralStrings = false,

	--- Strings containing newlines will be emitted as triple-quoted 'multi-line' strings where possible.
	allowMultiLineStrings = false,

	--- Allow real tab characters in string literals (as opposed to the escaped form `\t`).
	allowRealTabsInStrings = false,

	--- Allow non-ASCII characters in strings (as opposed to their escaped form, e.g. `\u00DA`).
	allow_unicode_strings = true,

	--- Allow integers with `toml.formatting.int.binary` to be emitted as binary.
	allowBinaryIntegers = true,

	--- Allow integers with `toml.formatting.int.octal` to be emitted as octal.
	allowOctalIntegers = true,

	--- Allow integers with `toml.formatting.int.hexadecimal` to be emitted as hexadecimal.
	allowHexadecimalIntegers = true,

	--- Apply indentation to tables nested within other tables/arrays.
	indentSubTables = true,

	--- Apply indentation to array elements when the array is forced to wrap over multiple lines.
	indentArrayElements = true,

	--- Combination of `indentSubTables` and `indentArrayElements`.
	indentation = true,

	--- Emit floating-point values with relaxed (human-friendly) precision.
	---
	--- Warning: Setting this flag may cause serialized documents to no longer round-
	--- trip correctly since floats might have a less precise value upon being written out
	--- than they did when being read in. Use this flag at your own risk.
	relaxedFloatPrecision = false,

	--- Avoids the use of whitespace around key-value pairs.
	terseKeyValuePairs = false
}
```

### Date and Time

(Creating Date, Time, and DateTime is shown in [the encoding section](#encoding))

```lua
	record Date
		year: number
		month: number
		day: number

		new: function(year: number, month: number, day: number): Date
	end

	record Time
		hour: number
		minute: number
		second: number
		nanoSecond: number

		new: function (
			hour: number,
			minute: number,
			second: number,
			nanoSecond: number
		): Time
	end

	record TimeOffset
		minutes: number

		new: function (hours: number, minutes: number): TimeOffset
	end

	record DateTime
		date: Date
		time: Time
		TimeOffset: nil | TimeOffset

		new: function(date: Date, time: Time): DateTime
		new: function(date: Date, time: Time, timeOffset: TimeOffset): DateTime
	end
```

> The comments for the options are from [the tomlplusplus documentation](https://marzer.github.io/tomlplusplus/namespacetoml.html#a2102aa80bc57783d96180f36e1f64f6a)