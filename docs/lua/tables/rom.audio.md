# Table: rom.audio

## Functions (1)

### `load_bank(file_path)`

The game currently use FMod Studio `2.02.23`. You can query the version by clicking checking Properties -> Details of the game `fmodstudio.dll`.
If your sound events correcty play but nothing can be heard, make sure that the guid of the Mixer masterBus, MixerInput output and MixerMaster id matches one from the game, one known to work is the guid that can be found inside the vanilla game file GUIDS.txt, called bus:/Game
You'll want to string replace the guids in the (at minimum 2) .xml files Master, Mixer, and any Metadata/Event events files that were made before the guid setup change

- **Parameters:**
  - `file_path` (string): Path to the fmod .bank to load

- **Returns:**
  - `bool`: Returns true if bank loaded successfully.

**Example Usage:**
```lua
bool = rom.audio.load_bank(file_path)
```


