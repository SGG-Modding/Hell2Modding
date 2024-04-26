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

## Mod Manager Integration

If you'd like to integrate Hell2Modding into your mod manager, here are the specifications:

Hell2Modding is injected into the game process using DLL hijacking (more precisely, it hijacks the Windows dynamic linked library `d3d12.dll`), which is the same technique used by other bootstrappers such as [UnityDoorstop](https://github.com/NeighTools/UnityDoorstop).

The root folder used by Hell2Modding (which will then be used to load mods from this folder) can be defined in several ways:

- Setting the process environment variable: `hell_2_modding_root_folder <CUSTOM_PATH>`

- Command line argument when launching the game executable: `--hell_2_modding_root_folder <CUSTOM_PATH>`

- If the process environment variable is not defined, the command line arguments are checked. If neither is defined, the Hell2Modding folder is placed in the game folder, next to the game executable.

Interesting folders under the root folder:

- `plugins`: Location of .lua, README, manifest.json files.
- `plugins_data`: Used for data that must persist between sessions but not be manipulated by the user.
- `config`: Used for data that must persist between sessions and that can be manipulated by the user.
