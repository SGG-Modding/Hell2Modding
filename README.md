# Hell 2 Modding

[Hades Modding Discord](https://discord.com/invite/KuMbyrN)
---

## Manual Installation

- Place the main Hell2Modding file, called `d3d12.dll`, next to the game executable called `Hades2.exe` inside the game folder.

## User Interface

- Ships with a ImGui user interface. The default key for opening the GUI is INSERT. You can change the key inside the `Hell2Modding/config/Hotkeys.cfg` file

## Creating mods

- Define a `main.lua` file in which to code your mod.

- Create a `manifest.json` file that follows the Thunderstore Version 1 Manifest format.

- Create a folder whose name follows the GUID format `TeamName-ModName`, for example: `Hell2Modding-DebugToolkit`.

- Place the `main.lua` file and the `manifest.json` file in the folder you've just created.

- Place the newly created folder in the `plugins` folder in the Hell2Modding root folder, called `Hell2Modding`, so the path to your manifest.json should be something like `Hell2Modding/plugins/Hell2Modding-DebugToolkit/manifest.json`.

- You can check the existing `examples` in that github repository if you wanna try stuff out.

Interesting folders under the root folder:

- `plugins`: Location of .lua, README, manifest.json files.
- `plugins_data`: Used for data that must persist between sessions but not be manipulated by the user.
- `config`: Used for data that must persist between sessions and that can be manipulated by the user.
