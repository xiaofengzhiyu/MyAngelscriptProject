## Why

UE 5.8 changes build defaults and several engine APIs used by the Angelscript plugin, causing the project to fail before it can produce a usable editor build. This change records and fixes the compatibility work needed to restore a clean UE 5.8 build baseline.

## What Changes

- Update project targets and local build configuration to use the UE 5.8 toolchain and include order.
- Adapt Angelscript runtime, editor, gameplay tag, GAS, and test code for UE 5.8 API and signature changes.
- Triage UE 5.8 deprecation warnings that will become future compile failures, fixing the ones close to touched code where practical.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- None. This is a build compatibility change and should not alter script-facing behavior or requirements.

## Impact

- `AgentConfig.ini` local engine path.
- `Source/*.Target.cs` build settings and include order.
- `Plugins/Angelscript/` runtime, editor, and test code affected by UE 5.8 API changes.
- Optional extension plugins under `Plugins/AngelscriptGameplayTags/` and `Plugins/AngelscriptGAS/` if their tests or modules block the editor build.
