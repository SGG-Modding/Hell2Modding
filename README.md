# Hell 2 Modding

[Hades Modding Discord](https://discord.com/invite/KuMbyrN)
---

## Mod Manager Installation

- Until ReturnOfModding integration is merged into the main r2modman version, [use the fork available here.](https://github.com/xiaoxiao921/r2modmanPlus/releases/)

## Manual Installation

- Place the main Hell2Modding file, called `d3d12.dll`, next to the game executable called `Hades2.exe` in the game's `Ship` folder.

- To uninstall the mod loader or revert to a vanilla experience without mods, you can simply rename or delete the `d3d12.dll` file.

## User Interface

- Hell2Modding comes with an ImGui user interface. The default key to open the user interface is `INSERT`. You can change this key in the `ReturnOfModding/config/Hell2Modding-Hell2Modding-Hotkeys.cfg` file. If the file isn't there, it will appear once you've launched the game at least once with Hell2Modding installed. There's also a small onboarding window that will appear in-game if the mod loader has launched successfully, where you can also edit the keybind.

## Creating mods

- [Checkout the template created by the modding community](https://github.com/SGG-Modding/Hades2ModTemplate)

## Folder convention:

- `plugins`: Location of .lua, README, manifest.json files.
- `plugins_data`: Used for data that must persist between sessions but not be manipulated by the user.
- `config`: Used for data that must persist between sessions and can be manipulated by the user.
