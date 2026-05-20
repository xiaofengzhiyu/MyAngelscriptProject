# Tasks: refactor-as-engine-clone-removal

## 1. Create FAngelscriptTestEngine

- [ ] 1.1 <!-- Non-TDD --> Create `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngine.h` with struct definition inheriting `FAngelscriptEngine`. Declare `ResetModules()`, `FullReset()`, `Create()`, `GetSharedEngine()`, `DestroySharedEngine()`. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptTest -Action Compile`

- [ ] 1.2 <!-- Non-TDD --> Create `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngine.cpp` implementing `ResetModules()` (discard all modules, GC), `FullReset()` (discard modules + clear generated types), `GetSharedEngine()` (lazy singleton), `DestroySharedEngine()`. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptTest -Action Compile`

- [ ] 1.3 <!-- Non-TDD --> Change required `FAngelscriptEngine` members from `private` to `protected` to enable subclass access (Modules map, Engine pointer, SharedState pointer). Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptRuntime -Action Compile`

## 2. Migrate Test Infrastructure

- [ ] 2.1 <!-- Non-TDD --> Update `AngelscriptTestMacros.h` to route `ASTEST_CREATE_ENGINE`, `ASTEST_GET_ENGINE`, `ASTEST_RESET_ENGINE` through `FAngelscriptTestEngine` APIs. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptTest -Action Compile`

- [ ] 2.2 <!-- Non-TDD --> Update `AngelscriptTestUtilities.h`: replace `GetOrCreateSharedCloneEngine()` and `AcquireCleanSharedCloneEngine()` implementations to delegate to `FAngelscriptTestEngine::GetSharedEngine()`. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptTest -Action Compile`

- [ ] 2.3 <!-- Non-TDD --> Replace all 14 `CreateCloneFrom` call sites in test module with `FAngelscriptTestEngine::Create()` or `GetSharedEngine()` as appropriate. Verify: `powershell.exe -File Tools\RunTests.ps1 -TestPrefix "ASTest." -Label "Clone migration smoke"`

- [ ] 2.4 <!-- Non-TDD --> Update `AngelscriptTestEnginePool.h` to use `FAngelscriptTestEngine` instead of Clone-based pooling. Verify: `powershell.exe -File Tools\RunTests.ps1 -TestPrefix "ASTest." -Label "Pool migration"`

## 3. Remove Clone from Runtime

- [ ] 3.1 <!-- Non-TDD --> Delete `EAngelscriptEngineCreationMode` enum from `AngelscriptEngine.h`. Remove `CreateCloneFrom()` declarations (both overloads). Remove `CreateUncompiledWithMode()` if test-only. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptRuntime -Action Compile`

- [ ] 3.2 <!-- Non-TDD --> Remove Clone-related members from `FAngelscriptEngine`: `CreationMode`, `SourceEngine`, `InstanceId`, `bOwnsEngine`. Remove `MakeModuleName()` Clone branch. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptRuntime -Action Compile`

- [ ] 3.3 <!-- Non-TDD --> Delete `CreateCloneFrom()` implementation, `AdoptSharedStateFrom()` implementation from `AngelscriptEngine.cpp`. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptRuntime -Action Compile`

- [ ] 3.4 <!-- Non-TDD --> Simplify `Shutdown()`: remove `bPendingOwnerRelease` checks, `ActiveCloneCount` waiting logic, deferred-release branches. Direct call to `ReleaseOwnedSharedStateResources()`. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptRuntime -Action Compile`

## 4. Simplify FAngelscriptOwnedSharedState

- [ ] 4.1 <!-- Non-TDD --> Remove `ActiveParticipants`, `ActiveCloneCount`, `bPendingOwnerRelease`, `bReleased` from `FAngelscriptOwnedSharedState`. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptRuntime -Action Compile`

- [ ] 4.2 <!-- Non-TDD --> Change `TSharedPtr<FAngelscriptOwnedSharedState>` to `TUniquePtr<FAngelscriptOwnedSharedState>` in `FAngelscriptEngine`. Update all usages. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptRuntime -Action Compile`

- [ ] 4.3 <!-- Non-TDD --> Simplify `ReleaseOwnedSharedStateResources()`: remove double-release guard (`bReleased` check), remove participant tracking. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptRuntime -Action Compile`

## 5. Full Validation

- [ ] 5.1 <!-- Non-TDD --> Full build of all modules. Verify: `powershell.exe -File Tools\Build.ps1 -Target All -Action Compile`

- [ ] 5.2 <!-- Non-TDD --> Run complete test suite. Verify: `powershell.exe -File Tools\RunTests.ps1 -TestPrefix "ASTest." -Label "Full regression"`

- [ ] 5.3 <!-- Non-TDD --> Run `GlobalContainerCycleBoundedTests` to confirm no memory leaks across engine cycles. Verify: `powershell.exe -File Tools\RunTests.ps1 -TestPrefix "ASTest.GlobalContainerCycleBounded" -Label "Cycle bounds"`

- [ ] 5.4 <!-- Non-TDD --> Verify AngelscriptRuntime compiles independently without AngelscriptTest linked. Verify: `powershell.exe -File Tools\Build.ps1 -Target AngelscriptRuntime -Action Compile -Standalone`
