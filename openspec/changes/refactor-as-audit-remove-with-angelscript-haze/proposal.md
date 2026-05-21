# refactor-as-audit-remove-with-angelscript-haze

> **Sequencing**: This change is the **second** of a two-step cleanup. It is **blocked** on `refactor-as-remove-autoaccessor` being merged first. The auto-property-accessor system there generates a synthetic `GetInstigator()` for the `AActor::Instigator` UPROPERTY; restoring the UE-original `GetInstigator` method binding (Phase 4 of this change) would collide with that synthetic accessor. Once the prerequisite removes the auto-accessor system, the rename is collision-free. Do not start this change until the prerequisite is archived.

## Why

The `WITH_ANGELSCRIPT_HAZE` macro (defined in `AngelscriptRuntime/Core/AngelscriptEngine.h:30-32`, default value `0`) has been a compile-time fork between the original Hazelight internal path and the generic UE path for years. The fork has long settled on the generic path. The 21 conditional-compilation sites still in the codebase are now pure aging cost:

- They split readers' attention every time a binding file is opened ("which side is active?").
- One category (A) actively misnames UE APIs in the active path (e.g. `GetActorInstigator` instead of `GetInstigator`) only to dodge a clash with the auto-property accessor system. Once the prerequisite removes that system, the dodge has no purpose.
- Several categories (B, C, D) preserve Haze-only RPC features (`NetFunction` / `CrumbFunction` / `DevFunction` / `HazeFunctionFlags`) and Haze-only behaviours (cooked-build exception dialog, `devEnsure`-based `AS_ENSURE`, `bUseAngelscriptHaze` debugger protocol field) that no longer correspond to the fork's direction.
- Most sites (E, 12 of 21) just wrap a generic UE binding in `#if !WITH_ANGELSCRIPT_HAZE`. The wrapper is decorative and can be removed without behavioural change.

The audit reclassifies every site, applies the appropriate handling, and finally removes the macro definition itself.

## What Changes

The 21 use sites are categorized A through E. Each category has a uniform handling rule:

- **Restore (Category A — 1 site)**: `Bind_AActor.cpp:155-175` is renamed back to UE-original method names.
  - `GetActorInstigator` → `GetInstigator`
  - `GetActorInstigatorController` → `GetInstigatorController`
  - `GetInputComponent` (no rename — already matches UE's `AActor::InputComponent` field; only the `#if` wrapper is removed)
- **Remove (Category B — 4 sites)**: Haze-exclusive RPC machinery deleted entirely.
  - `AngelscriptClassGenerator.cpp:3501-3512` (`FUNC_NetFunction`, `FUNC_DevFunction`, `HazeFunctionFlags`, `HAZEFUNC_CrumbFunction`).
  - `AngelscriptPreprocessor.cpp:1632` (preprocessor handling of `NetFunction` / `CrumbFunction` / `DevFunction` UFUNCTION specifiers).
  - `Bind_BlueprintType.cpp:787, 1359, 1504` (three `else if (Function->HasAnyFunctionFlags(FUNC_NetFunction))` BP-event-binding branches).
- **Standardize (Category C — 1 site)**: `Bind_WorldCollision.cpp:358` keeps the `System::` namespace and drops the `AsyncTrace::` Haze-side variant.
- **Decide (Category D — 3 sites)**: behavioural choices fixed to the non-Haze defaults.
  - `ASClass.cpp:31-35`: `AS_ENSURE` is permanently `ensureMsgf` (not `devEnsure`).
  - `AngelscriptEngine.cpp:5897`: cooked-build exception dialog logic remains under a non-Haze conditional that is simplified to plain `#if !WITH_EDITOR` once the macro is gone (the `devEnsure` branch and `MessageDialog` branch were Haze-only and are removed).
  - `AngelscriptDebugServer.cpp:1911` plus `FAngelscriptDebugDatabaseSettings::bUseAngelscriptHaze`: the field is removed from the protocol struct, the assignment is deleted, and `AngelscriptDebuggerDatabaseTests.cpp:115` drops the corresponding assertion.
- **Cleanup (Category E — 12 sites)**: `#if !WITH_ANGELSCRIPT_HAZE ... #endif` wrappers are deleted, the wrapped bindings remain unchanged.
  - `Bind_AActor.cpp:342` (`GetAllActorsOfClass` global function).
  - `Bind_APlayerController.cpp:11, 72, 104` (three top-level binding blocks for `AController`, `APlayerController`, `APawn`).
  - `Bind_FCollisionShape.cpp:34` (`FCollisionShape` POD value-class registration).
  - `Bind_Subsystems.cpp:154` (`ULocalPlayerSubsystem::Get(LocalPlayer)` global function).
  - `Bind_UGameInstance.cpp:9` (`CreateInitialPlayer` / `CreateLocalPlayer` / `AddLocalPlayer` etc. methods).
  - `Bind_ULocalPlayer.cpp:6` (`GetGameInstance` / `GetControllerId`).
  - `Bind_UObject.cpp:54, 569` (`IsSupportedForNetworking`, `LoadObject` global function).
  - `Bind_UWorld.cpp:48` (`ServerTravel`, `GetNetMode`, `GetGameState`).
  - `AngelscriptPreprocessor.cpp:2627` (UPROPERTY `Replicated` / `ReplicationCondition` / `NotReplicated` specifier processing).

After all sites are handled, the macro definition itself is removed:

- **Remove macro**: `AngelscriptRuntime/Core/AngelscriptEngine.h:30-32` `#ifndef WITH_ANGELSCRIPT_HAZE / #define WITH_ANGELSCRIPT_HAZE 0 / #endif` is deleted.
- **Verify clean**: `rg -n "WITH_ANGELSCRIPT_HAZE"` against the entire repository SHALL return zero matches after this change.

## Capabilities

### New Capabilities

- `as-haze-macro-removal`: codifies the post-removal state — macro absent, UE-original names restored on Category-A sites, Haze-only RPC features absent, debugger protocol field absent, generic-UE behaviours fixed as the only path.

### Modified Capabilities

- None. (No prior capability declared `WITH_ANGELSCRIPT_HAZE` as a contract surface.)

## Impact

- **Source files modified**: 14 binding/runtime `.cpp` files plus 1 header (`AngelscriptEngine.h`) plus 1 debugger-protocol header (where `FAngelscriptDebugDatabaseSettings` lives).
- **Tests modified**: `AngelscriptTest/Debugger/AngelscriptDebuggerDatabaseTests.cpp:115` drops the `bUseAngelscriptHaze` mirror assertion.
- **Documentation**: `Documents/Plans/Plan_HazelightScriptFeatureParity.md` is in the deprecated Plans tree and does not require content edits, but a one-line "completed via OpenSpec change `refactor-as-audit-remove-with-angelscript-haze`" is added at the top of the file. `Documents/Knowledges/ZH/Syntax_UFUNCTION.md` updates the section that describes the `NetFunction` / `CrumbFunction` Haze branch to read "removed".
- **AGENTS.md / AGENTS_ZH.md**: one new "Recently Completed Milestones" entry.
- **Debugger wire format**: removing `bUseAngelscriptHaze` from `FAngelscriptDebugDatabaseSettings` is a forward-compatible protocol change. Older VSCode AngelScript extension builds reading the field default to `false`, which matches every observable runtime; no end-user breakage is expected.
- **Submodule scope**: All affected files live inside `Plugins/Angelscript/` (the submodule). This is a dual-repo change — submodule first, then host gitlink and OpenSpec artifact update.
