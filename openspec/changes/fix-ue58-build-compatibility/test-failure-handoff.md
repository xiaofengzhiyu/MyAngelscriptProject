# UE 5.8 Test Failure Handoff

## Current Baseline

- Engine root: `C:\Program Files\Epic Games\UE_5.8`
- Latest focused build: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -Label ue58-bindings-fixes-build -TimeoutMs 600000`
- Build result: passed with `FinalExitCode: 0`.
- C++ deprecation state: no remaining `warning C4996` diagnostics in the verified editor build.
- Remaining non-C++ warning: UE reports `StructUtils` plugin deprecation. Do not remove it in this compile-only pass because `FInstancedStruct` is still part of the public Angelscript runtime/script surface.

## Root Cause: Generated Function Tables

UE 5.8 no longer compiles custom UHT exporter outputs into the current C++ action graph just because the files exist under the module UHT output directory.

Observed before the fix:

- UHT generated `AS_FunctionTable_*.gen.cpp` files under `Plugins\Angelscript\Intermediate\Build\Win64\UnrealEditor\Inc\AngelscriptRuntime\UHT`.
- The generated summary reported `5835` entries.
- `Angelscript.TestModule.Engine.BindConfig` still saw empty or incomplete class maps because the custom `.gen.cpp` shards were not linked into `UnrealEditor-AngelscriptRuntime.dll`.

Implemented fix:

- Runtime generated table shards now use `.gen.cpp` names and exporter filters cover `AS_FunctionTable_*.gen.cpp`.
- `AngelscriptRuntime.Build.cs` uses `ModuleRules.FilesToGenerate` to emit wrapper `.cpp` files under the module intermediate directory.
- Each wrapper includes a generated shard through `UE_INLINE_GENERATED_CPP_BY_NAME(...)`, guarded by `__has_include`.
- UBT compiles those wrapper `.cpp` files as normal module sources, so the generated static registration code links reliably in UE 5.8.

Validation evidence:

- `Angelscript.TestModule.Engine.BindConfig`: `21/21` passed.
- `Angelscript.TestModule.Engine.GeneratedFunctionTable`: `11/11` passed.
- `Angelscript.TestModule.Engine`: `97/97` passed.
- Runtime logs report `[UHT] Registered 5835 generated BlueprintCallable entries across 29 shard(s)`, including `EnhancedInput shard 1/1`.

## Other UE 5.8 Fixes

- `AngelscriptUHTTool.ubtplugin.csproj` no longer pins `net8.0`; UE 5.8 injects its bundled SDK target.
- Removed obsolete `UhtExporterOptions.CompileOutput` usage.
- Updated generated-output tests from `AS_FunctionTable_*.cpp` to `AS_FunctionTable_*.gen.cpp`.
- Adjusted duplicate ensure-count expectations for UE 5.8 logging channels:
  - `Preprocessor.Basic.PreprocessIsSingleUse`
  - `Bindings.UserWidget.UserWidgetTreeErrorPaths`
- Removed stale `Angelscript.TestModule.Learning` suite entries from fast suite definitions.
- Fixed `FString::ApplyFormat` octal and uppercase scientific/general formatting so it no longer uses UE 5.8 unsupported varargs specifiers `%o`, `%E`, and `%G`.

## Focused Validation Runs

| Prefix | Result | Label |
|---|---:|---|
| `Angelscript.TestModule.Engine.BindConfig` | `21/21` passed | `ue58-bindconfig-after-table-wrapper` |
| `Angelscript.TestModule.Preprocessor` | `60/60` passed | `ue58-preprocessor-after-table-wrapper` |
| `Angelscript.TestModule.Engine.GeneratedFunctionTable` | `11/11` passed | `ue58-generated-function-table-tests-after-rebuild` |
| `Angelscript.TestModule.Engine` | `97/97` passed | `ue58-engine-after-generated-table-test-fix` |
| `Angelscript.TestModule.Actor` | `50/50` passed | `ue58-actor-after-table-wrapper` |
| `Angelscript.TestModule.Bindings.FString.FAngelscriptFStringBindingsTest.ApplyFormat` | `1/1` passed | `ue58-fstring-applyformat-fixed` |
| `Angelscript.TestModule.Bindings.UserWidget.FAngelscriptUserWidgetBindingsTest.UserWidgetTreeErrorPaths` | `1/1` passed | `ue58-userwidget-errorpaths-fixed` |
| `Angelscript.TestModule.Bindings` | `258/258` passed | `ue58-bindings-after-fixes` |
| `Angelscript.TestModule.FunctionLibraries` | `23/23` passed | `ue58-functionlibraries-after-table-wrapper` |
| `Angelscript.TestModule.Functional` | `104/104` passed | `ue58-functional-after-table-wrapper` |

## Final Verification

Full fast suite was run after the final UHT props and wrapper changes:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTestSuiteFast.ps1 -LabelPrefix ue58-all-fast-post-props -TimeoutMs 900000 -ContinueOnFail
```

Result:

- Summary: `Saved\Tests\ue58-all-fast-post-props_20260618_133427\ParallelSuiteSummary.json`
- Shards: `36`
- Aggregated tests: `1893 pass / 0 fail / 1893 total`
