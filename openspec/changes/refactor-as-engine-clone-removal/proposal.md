# refactor-as-engine-clone-removal

## Why

`FAngelscriptEngine` contains a Clone mechanism (`EAngelscriptEngineCreationMode::Clone`) that allows creating lightweight engine copies sharing `FAngelscriptOwnedSharedState`. Investigation reveals:

- Clone has **zero production callers** (including `CreateUncompiledWithMode`, which is only called from test code).
- The Clone coordination logic (`ActiveParticipants`, `ActiveCloneCount`, `bPendingOwnerRelease`) adds unnecessary complexity to the runtime shutdown path.
- Test-specific concerns (module reset, engine pooling, transient engine creation) have leaked into `AngelscriptRuntime`, making the runtime harder to reason about independently.
- Most tests already use a persistent shared engine with module-level reset, not true Clone semantics.

Removing Clone and extracting test logic into a dedicated `FAngelscriptTestEngine` subclass simplifies the runtime while preserving test performance.

## What Changes

- **New**: `FAngelscriptTestEngine` struct in `AngelscriptTest` module, inheriting `FAngelscriptEngine`.
- **Remove**: `EAngelscriptEngineCreationMode` enum, `CreateCloneFrom()`, `CreateUncompiledWithMode()`, Clone-related members from `FAngelscriptEngine`.
- **Simplify**: `FAngelscriptOwnedSharedState` drops reference-counting fields; ownership changes from `TSharedPtr` to `TUniquePtr`.
- **Simplify**: `Shutdown()` no longer performs deferred-release coordination.
- **Migrate**: 14 test-side `CreateCloneFrom` call sites adopt `FAngelscriptTestEngine` APIs.

## Capabilities

### New Capabilities

- **FAngelscriptTestEngine**: Test-module-owned engine subclass providing `ResetModules()`, `FullReset()`, shared-engine singleton, and test helper methods (compile-from-memory, execute functions).

### Modified Capabilities

- **FAngelscriptEngine**: Simplified to Full-mode-only; no Clone branching in `MakeModuleName`, `Shutdown`, or initialization paths.
- **FAngelscriptOwnedSharedState**: Reduced to pure value aggregate with no lifecycle coordination.

## Impact

- **AngelscriptRuntime**: `AngelscriptEngine.h/.cpp` modified (removal of Clone API surface and SharedState simplification).
- **AngelscriptTest**: New files `AngelscriptTestEngine.h/.cpp`; updates to `AngelscriptTestMacros.h`, `AngelscriptTestUtilities.h`, `AngelscriptTestEngineHelper.h`, `AngelscriptTestEnginePool.h`.
- **Test consumers**: All 463 test files compile and pass without regression; macro interface remains stable.
- **Build rules**: No new module dependencies (AngelscriptTest already depends on AngelscriptRuntime).
