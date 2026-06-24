# test-asscriptmodule-sdk-coverage

## Summary

Expand AngelScriptSDK coverage for raw `asIScriptModule` / `asCModule` behavior, while keeping true `asCBuilder` stage and `CompileFunction` coverage under the Builder prefix. This is a test-only change focused on the `AngelScriptSDK` native layer.

## Scope

- Rename the legacy `asbuilder.md` analysis artifact to `asscriptmodule.md`, since the contents primarily describe script module behavior.
- Move module-owned behavior to SDK-only `ScriptModule` tests for import binding, namespace lookup, multi-section compilation, and section diagnostics.
- Keep true `asCBuilder` tests for staged compilation and `CompileFunction` in `AngelscriptBuilderTests.cpp`.
- Keep the tests under `Plugins/Angelscript/Source/AngelscriptTest/AngelScriptSDK/`.
- Do not change runtime behavior or include `FAngelscriptManager`, HotReload, Blueprint, UObject, World, or Actor flows.

## Verification

- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -Label asscriptmodule-sdk-coverage-build -TimeoutMs 600000`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.AngelScriptSDK.ScriptModule" -Label asscriptmodule-sdk-coverage -TimeoutMs 600000`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.AngelScriptSDK.Builder" -Label asbuilder-sdk-coverage -TimeoutMs 600000`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.AngelScriptSDK" -Label sdk-native-after-scriptmodule -TimeoutMs 900000`
