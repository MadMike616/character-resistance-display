# Hook Inventory — Character Resistance Display

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Character Resistance Display |
| Plugin ID | character-resistance-display |
| Release version | 1.1.10 |
| Inventory updated | 2026-11-27 |
| Source revision | v1.1.10 release source |
| D2R build(s) audited | Not validated |
| D2RLoader SDK / Plugin ABI | 0.4.0 / 4 |
| Audit status | Complete |

## Scope and method

Inspected src/plugin.cpp, src/loose_ui_setup.cpp, src/i_hate_tomls_client.h, and the resource build. The audit checked plugin-owned inline detours, CALL/JMP redirects, byte/NOP patches, vtable or shared-slot replacements, MinHook targets, and Win32 input/message hooks. Ordinary D2RLoader lifecycle and widget callbacks and native helper calls are not hook sites.

## Hook sites

| Site ID | Type | Target | Address / identifier | Operation | Guard / validation | Source reference | Notes |
|---|---|---|---|---|---|---|---|

No direct hook sites found in the audit.

## Known collision overlaps

| Other plugin | Shared target | Relationship | Evidence / notes |
|---|---|---|---|

None identified in the current inventory; this is not a compatibility guarantee.

## Audit notes

No hook-site changes from v0.1.9. The plugin declares NativeHooks, validates and calls native helpers at RVAs 0x08B2D0, 0x09A480, 0x09A5D0, 0x13ABC0, 0x0846170, 0x0856220, and 0x08562A0, and resolves ReadWideUnitStat by D2RCore.dll export name. It does not install or replace a hook. The shared installation mutex and optional settings service do not intercept native code.

Release 1.1.10 source review: TOML installation completion and configuration mirroring add no native hook, patch, vtable, or OS input-hook targets. Hook-site changes since the previous release: none. Startup completion was checked in the testing environment; the existing native validation limits above still apply.
