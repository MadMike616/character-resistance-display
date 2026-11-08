# Hook Inventory — Character Resistance Display

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Character Resistance Display |
| Plugin ID | character-resistance-display |
| Release version | 0.1.9 |
| Inventory updated | 2026-11-08 |
| Source revision | v0.1.9 GitHub release tag |
| D2R build(s) audited | Not validated |
| D2RLoader SDK / Plugin ABI | 0.4.0 / 4 |
| Audit status | Complete |

## Scope and method

Inspected the plugin source tree, including src/plugin.cpp, src/plugin.rc, CMakeLists.txt, and the embedded-config build integration. The audit checked plugin-owned inline detours, CALL/JMP redirects, byte/NOP patches, vtable or shared-slot replacements, MinHook targets, and Win32 input/message hooks. Ordinary D2RLoader lifecycle, panel, draw, and input callbacks and native helper calls are not hook sites.

## Hook sites

| Site ID | Type | Target | Address / identifier | Operation | Guard / validation | Source reference | Notes |
|---|---|---|---|---|---|---|---|

No direct hook sites found in the audit.

## Known collision overlaps

List only overlaps supported by the current source audit. Include the other plugin and shared target. If none are known, write “None identified in the current inventory; this is not a compatibility guarantee.”

| Other plugin | Shared target | Relationship | Evidence / notes |
|---|---|---|---|

None identified in the current inventory; this is not a compatibility guarantee.

## Audit notes

The plugin declares the NativeHooks capability flag but does not install or replace a native or operating-system hook target. CheckExpectedBytes validates direct native helper calls at RVAs 0x08B2D0, 0x09A480, 0x09A5D0, 0x13ABC0, 0x0846170, 0x0856220, and 0x08562A0; these calls are not hook sites. The plugin also resolves ReadWideUnitStat by D2RCore.dll export name. The audit is source-level; no specific D2R build was validated.



