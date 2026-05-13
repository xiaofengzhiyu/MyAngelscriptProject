## 1. Runtime Module Rename

- [x] 1.1 <!-- Non-TDD --> Rename `Plugins/UnrealEvent/Source/GMP/GMP.Build.cs` and its `ModuleRules` class to `UnrealEvent`.
- [x] 1.2 <!-- Non-TDD --> Update the runtime module implementation macro and build definitions so existing `GMP_API` declarations compile under the `UnrealEvent` module.
- [x] 1.3 <!-- Non-TDD --> Update `Plugins/UnrealEvent/UnrealEvent.uplugin` to declare `UnrealEvent` as the runtime module instead of `GMP`.
- [x] 1.4 <!-- Non-TDD --> Update active runtime `/Script/GMP` metadata paths and default module lookups to `/Script/UnrealEvent`, keeping compatibility redirects where needed.

## 2. Editor And Test Modules

- [x] 2.1 <!-- Non-TDD --> Add a minimal `UnrealEventEditor` module with dependencies on `Core`, `CoreUObject`, `Engine`, `UnrealEd`, and `UnrealEvent`.
- [x] 2.2 <!-- Non-TDD --> Add a minimal `UnrealEventTest` module with dependencies on `Core`, `CoreUObject`, `Engine`, `UnrealEvent`, and editor-only test dependencies when `Target.bBuildEditor`.
- [x] 2.3 <!-- Non-TDD --> Add `UnrealEventEditor` and `UnrealEventTest` to `UnrealEvent.uplugin` using editor loading semantics aligned with the Angelscript plugin.

## 3. Verification

- [x] 3.1 <!-- Non-TDD --> Validate descriptor JSON with `Get-Content -Raw Plugins\UnrealEvent\UnrealEvent.uplugin | ConvertFrom-Json`.
- [x] 3.2 <!-- Non-TDD --> Run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -Label unrealevent-module-layout -TimeoutMs 180000 -NoXGE`.
- [x] 3.3 <!-- Non-TDD --> Run `openspec validate "refactor-unrealevent-module-layout" --strict --json` and update completed task checkboxes.
