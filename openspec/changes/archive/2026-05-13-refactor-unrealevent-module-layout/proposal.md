## Why

`UnrealEvent` still exposes its active runtime module as `GMP`, which keeps the bootstrapped source tied to the upstream plugin identity. The next migration step is to make the module layout match the standalone plugin name while adding explicit editor and test module slots for later work.

## What Changes

- Rename the active runtime module from `GMP` to `UnrealEvent` without adding a `Runtime` suffix.
- Update `UnrealEvent.uplugin` so it declares `UnrealEvent`, `UnrealEventEditor`, and `UnrealEventTest` using the same broad runtime/editor/test split as the Angelscript plugin.
- Add `UnrealEventEditor` and `UnrealEventTest` module skeletons that compile and depend on the runtime module.
- Update active runtime reflection metadata from `/Script/GMP` to `/Script/UnrealEvent` where it depends on the UE module name.
- Preserve GMP-derived C++ symbol names and the `GMP` include subfolder during this step to avoid a broad API rewrite.
- Defer functional editor tooling, automation tests, and deeper GMP API renaming to later changes.

## Capabilities

### New Capabilities

- `unrealevent-module-layout`: Captures the standalone UnrealEvent module naming and runtime/editor/test module boundaries.

### Modified Capabilities

- None.

## Impact

- Plugin descriptor: `Plugins/UnrealEvent/UnrealEvent.uplugin`.
- Runtime module build and implementation entry point under `Plugins/UnrealEvent/Source`.
- New module directories: `Plugins/UnrealEvent/Source/UnrealEventEditor` and `Plugins/UnrealEvent/Source/UnrealEventTest`.
- Host build discovery through `Tools\RunBuild.ps1`.
