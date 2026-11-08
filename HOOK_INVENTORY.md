# Hook Inventory

Plugin: Character Resistance Display (`character-resistance-display`)
Release: 0.1.8 (2026-11-08)
SDK / ABI: D2RLoader PluginSDK 0.4.0 / Plugin ABI 4

## Installed hook sites

This release installs no inline hooks, byte patches, vtable hooks, or OS input
hooks. There are no hook-site changes in this release. The source declares the
`PluginFlags::NativeHooks` flag, but does not stage or commit a mutation through
the SDK mutation service.

## Signature-guarded native call targets

The plugin calls these D2R.exe functions after validating the listed leading
bytes with `CheckExpectedBytes`. These are direct-call targets, not installed
hook sites:

| Function | RVA | Validated bytes | Purpose |
| --- | ---: | ---: | --- |
| `GetLocalDataContext` | `0x08B2D0` | 32 | Resolve active local-player context |
| `GetLocalPlayer` | `0x09A480` | 32 | Resolve player unit |
| `GetUnit` | `0x09A5D0` | 6 | Resolve mercenary unit |
| `GetPet` / mercenary ID reader | `0x13ABC0` | 12 | Read the mercenary unit ID |
| `FindTopLevelPanel` | `0x0846170` | 22 | Find character/hireling panel |
| `FindChildWidgetByName` | `0x0856220` | 32 | Find added resistance widgets |
| `GetWidgetRect` | `0x08562A0` | 32 | Validate and update native widget rect |

The plugin also resolves `ReadWideUnitStat` from `D2RCore.dll` by export name.
The legacy native UI path uses structure offsets `0x70` for widget rectangles
and `0x88` for text-buffer pointers; these are data-field offsets, not hook
targets. The compatibility path is enabled only when its expected function
signatures match.

No prior public release exists for comparison. Recheck these call targets and
the validated byte signatures during the next release preparation.

