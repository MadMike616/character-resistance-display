# Character Resistance Display

Character Resistance Display is a Windows x64 D2RLoader plugin for D2RLoader
PluginSDK 0.4.0 / Plugin ABI 4. It reads Physical Resistance (stat 36) and
Magic Resistance (stat 37) from the player and mercenary units. It displays
player values on the character sheet and the mercenary's own values on the
desktop HD hireling panel. Physical Resistance displays up to 50%; Magic
Resistance displays up to 75%. Values at or above their cap are capped and
shown in gold. Resistance values have no hover tooltips.

Download the DLL from the [GitHub Releases page](https://github.com/MadMike616/character-resistance-display/releases/latest).
The release notes provide the DLL's SHA-256 checksum.

## Install

1. Put `character-resistance-display.dll` in
   `<game>\mods\<mod-name>\d2rloader\plugins\`.
2. If a layout is missing from the unpacked mod's loose data folder, use
   **Ladik's Casc Viewer** to extract the original JSON from the game data
   version your mod uses. Put it under
   `<game>\mods\<mod-name>\d2rloader\plugins\basefiles-and-backups\basefiles\`,
   keeping the path relative to `data`. For example:
   `basefiles\global\ui\layouts\characterstatspanelhd.json`.
3. Launch D2RLoader with the mod. Setup merges the resistance widgets into
   the loose layout, creating it from the original if needed. It installs
   the bundled HD desktop sprite if missing. During desktop layout setup,
   it also replaces a loose desktop sprite that matches the original in
   `basefiles`. Other existing artwork is kept.
4. After setup writes files, exit and relaunch D2RLoader with the same mod
   to load them.

The paths below are relative to your mod's loose `data` folder or to `basefiles`:

| Panel | Layout JSON | Background sprite |
| --- | --- | --- |
| Desktop HD | `global\ui\layouts\characterstatspanelhd.json` | `hd\global\ui\panel\character_sheet\character_background.sprite` (bundled) |
| Desktop HD mercenary | `global\ui\layouts\hirelinginventorypanelhd.json` | `hd\global\ui\panel\hireling\hirelingpanel.sprite` (supply your own) |
| Controller HD (partial, off by default) | `global\ui\layouts\controller\characterstatspanelhd.json` | `hd\global\ui\controller\panel\charactersheet\v2\CharacterSheet_BG.sprite` (supply your own) |

For mercenary and controller rows, put your modified sprite in loose data
or in `basefiles` and enable the desired rows before launch. Stock artwork
for those panels has no slots for the new values. Low-resolution layouts
are unsupported.

The DLL embeds its default settings and automatically creates
`d2rloader/config/character-resistance-display.toml` on first launch.
After closing the game, edit that generated file to change options, then
launch again. Keep your existing configuration when upgrading.

### Backups and manual installation

Setup uses the shared mod install lock. It saves existing files before
changing them under
`d2rloader\plugins\basefiles-and-backups\backups\character-resistance-display\`
in dated folders, using paths relative to the mod folder. Layouts are
reformatted when widgets are added; the original JSON is kept in the backup
when setup changes an existing loose layout. Each backup has a
`created-files.txt` listing newly added files. To undo setup, close
D2RLoader, remove the plugin, restore the originals, and review the listed
files before removing them.

For manual installation, merge the corresponding objects in
`layout-snippets` into the layout's `children` array and install a compatible
sprite. Automatic setup only writes to unpacked mods.

## TOML settings

All on/off switches are grouped at the top of the TOML under `[enabled]`.
Each resistance on each supported panel can be enabled independently:

```toml
[enabled]
character_physical = true
character_magic = true
controller_character_physical = false
controller_character_magic = false
mercenary_physical = true
mercenary_magic = true
```

Each row has one `[panel.resistance]` table with `x` and `y` coordinates.
That point moves the label and value together; their spacing and text-box
sizes stay paired. Coordinates use the corresponding D2R layout's local
coordinate space. The sections are `[desktop.physical]`, `[desktop.magic]`,
`[controller.physical]`, `[controller.magic]`, `[mercenary.physical]`, and
`[mercenary.magic]`.

With the optional **I Hate TOMLs** plugin installed, open its Character
Resistance Display page to change visibility and positions live for panels
that already have their UI files installed. Changes are saved to the TOML.
Restart D2RLoader after editing the TOML by hand or enabling a panel that
still needs first-launch setup.

## Compatibility

The plugin is mod-scoped and client-only. It doesn't change character stats.
It reads player and mercenary units only when the relevant function signatures
match; if a read is unavailable, that panel shows `--`. On loaders whose
WidgetService predates `setText` or `setWidgetRect`, it uses a
signature-checked compatibility path for those operations. If the native UI
fingerprints do not match, it leaves those operations untouched and logs the
reason. The plugin was built with SDK 0.4.0 / ABI 4; use a D2RLoader build that
supports ABI 4 and the widget/thread services it requires.

The plugin validates its native function signatures at runtime.

## Build from source

Build on Windows with Visual Studio 2022 C++ tools, Windows SDK, and CMake
3.29 or newer. Clone the [official D2RLoader PluginSDK](https://github.com/D2RLoader/PluginSDK)
at version 0.4.0 into a
separate directory, then configure this project with
`D2RLPLUGIN_SDK_SOURCE_DIR` pointing to its 0.4.0 CMake source directory:

```powershell
cmake -S . -B ../build-character-resistance-display -DD2RLPLUGIN_SDK_SOURCE_DIR="<path-to-PluginSDK-0.4.0>"
cmake --build ../build-character-resistance-display --config Release
```

The resulting plugin DLL is written to the build output directory.

## License and assets

The plugin source code is MIT licensed. The modified sprite files are derived
from Diablo II: Resurrected game assets and are not covered by the MIT license;
Blizzard Entertainment retains ownership of the underlying game assets. Use
them only with a legally obtained copy of the game. See `ASSET_NOTICES.md`.

## Manual verification

Open the character sheet and confirm each enabled player value appears in its
sprite slot. Open the hireling panel and confirm each enabled mercenary value
follows the mercenary's resistance, not the player's. Change an `x`/`y` pair
and confirm its label and value move together. Disable each row in `[enabled]`
in turn and confirm only that row disappears. Equip or remove an item that
changes resistance and confirm the corresponding panel refreshes. Save and
quit to character select, enter a character again without restarting
D2RLoader, and confirm both panels update. A genuine zero resistance displays
as `0%`.


## Installation completion

After successful first-run setup, the plugin records a completion token in its TOML configuration. Later launches skip that completed setup and preserve existing files and settings. Failed setup remains retryable.

Completion is recorded separately in `[installation.desktop]`, `[installation.mercenary]`, and `[installation.controller]`. Enabling a previously disabled panel runs setup for that panel only.

To rerun setup intentionally, close the game and set `complete = false` in the relevant token section (`[installation.desktop]` by default).
