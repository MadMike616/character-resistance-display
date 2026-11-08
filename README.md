# Character Resistance Display

Character Resistance Display is a Windows x64 D2RLoader plugin for D2RLoader
PluginSDK 0.4.0 / Plugin ABI 4. It reads Physical Resistance (stat 36) and
Magic Resistance (stat 37) from the player and mercenary units. It displays
player values on the character sheet and the mercenary's own values on the
desktop HD hireling panel. Physical Resistance displays up to 50%; Magic
Resistance displays up to 75%. Values at or above their cap are capped and
shown in gold. Resistance values have no hover tooltips.

Release 0.1.9 SHA-256 (`character-resistance-display.dll`):
`0003ECD2CA90E940A62B8077B3E70B44B3743449AED7884F933311371B3F49CB`.
This DLL was rebuilt from the 0.1.9 source with optimized MSVC Release flags (/O2 /Ob2 /DNDEBUG /MD) and contains no CodeView/PDB debug data.

## Install

1. Put `character-resistance-display.dll` in
   `<game>\mods\<mod-name>\d2rloader\plugins\`.
2. Copy `character-resistance-display.toml` to
   `<game>\mods\<mod-name>\d2rloader\config\`.
3. The repository includes modified character-sheet background sprites under
   `assets\data\hd\global\ui\panel\character_sheet\`. Copy the contents of
   `assets` into `<game>\mods\<mod-name>\<mod-name>.mpq\` so the sprite files
   land under `data\hd\global\ui\panel\character_sheet\`.
4. Merge the widget objects from `layout-snippets\characterstatspanelhd.json`
   into the `children` array of
   `<game>\mods\<mod-name>\<mod-name>.mpq\data\global\ui\layouts\characterstatspanelhd.json`.
   The mercenary widgets are in
   `layout-snippets\hirelinginventorypanelhd.json`; merge those objects into
   the `children` array of
   `<game>\mods\<mod-name>\<mod-name>.mpq\data\global\ui\layouts\hirelinginventorypanelhd.json`.
5. Restart D2RLoader after changing the TOML or layout files.

The desktop character-sheet HD sprite is included. The low-end character
sprite is included for completeness, but the plugin does not add text widgets
to low-resolution layouts. The mercenary and controller sprite artwork is not
included: users who want those rows must provide their own modified sprites.
For the desktop HD mercenary panel, provide
`<game>\mods\<mod-name>\<mod-name>.mpq\data\hd\global\ui\panel\hireling\hirelingpanel.sprite`
and apply the hireling layout snippet. Controller character-sheet support is
partial and disabled by default. To use it, supply a modified sprite such as
`CharacterSheet_BG.sprite` under
`<game>\mods\<mod-name>\<mod-name>.mpq\data\hd\global\ui\controller\panel\charactersheet\v2\`,
merge `layout-snippets\controller-characterstatspanelhd.json` into the
`<game>\mods\<mod-name>\<mod-name>.mpq\data\global\ui\layouts\controller\characterstatspanelhd.json`,
and enable the desired controller rows in the TOML. Without matching sprite
slots, text may overlap or be hard to read.

The low-resolution character-sheet and hireling layouts do not currently have
matching resistance text widgets. Mercenary values are supported on the
desktop HD hireling panel only.

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
`[mercenary.magic]`. Restart D2RLoader after editing the TOML.

## Compatibility

The plugin is mod-scoped and client-only. It doesn't change character stats.
It reads player and mercenary units only when the relevant function signatures
match; if a read is unavailable, that panel shows `--`. On loaders whose
WidgetService predates `setText` or `setWidgetRect`, it uses a
signature-checked compatibility path for those operations. If the native UI
fingerprints do not match, it leaves those operations untouched and logs the
reason. The plugin was built with SDK 0.4.0 / ABI 4; use a D2RLoader build that
supports ABI 4 and the widget/thread services it requires.

No specific game build is declared as in-game tested for this release. The
plugin validates its native function signatures at runtime and does not apply
an explicit game-version gate.

## Build from source

Build on Windows with Visual Studio 2022 C++ tools, Windows SDK, and CMake
3.29 or newer. Clone the [official D2RLoader PluginSDK](https://github.com/D2RLoader/PluginSDK)
at version 0.4.0 into a
separate directory, then configure this project with
`D2RLPLUGIN_SDK_SOURCE_DIR` pointing to its 0.4.0 CMake source directory:

```powershell
cmake -S . -B build -DD2RLPLUGIN_SDK_SOURCE_DIR="<path-to-PluginSDK-0.4.0>"
cmake --build build --config Release
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







