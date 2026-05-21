> **Sequencing**: This change is **blocked** on `refactor-as-remove-autoaccessor` being archived. Do not start tasks 1.1+ until that prerequisite is complete. Phase 4 (Category A renames) is the load-bearing reason — renaming `GetActorInstigator` back to `GetInstigator` would collide with the auto-property-accessor `GetInstigator()` generated for the `Instigator` UPROPERTY; the prerequisite removes that synthesis path so the rename becomes a clean addition.

## 1. Category E — strip `#if !WITH_ANGELSCRIPT_HAZE` decorative wrappers (12 sites)

- [ ] 1.1 <!-- Non-TDD --> `Bind_AActor.cpp:342` remove the `#if !WITH_ANGELSCRIPT_HAZE` wrapper around the `GetAllActorsOfClass` global function; verify with `rg -n "WITH_ANGELSCRIPT_HAZE" Plugins\Angelscript\Source\AngelscriptRuntime\Binds\Bind_AActor.cpp`.
- [ ] 1.2 <!-- Non-TDD --> `Bind_APlayerController.cpp` remove the wrappers at lines 11, 72, 104 (three blocks for `AController`, `APlayerController`, `APawn`); verify with `rg -n "WITH_ANGELSCRIPT_HAZE" Plugins\Angelscript\Source\AngelscriptRuntime\Binds\Bind_APlayerController.cpp`.
- [ ] 1.3 <!-- Non-TDD --> `Bind_FCollisionShape.cpp:34` remove the wrapper around `FCollisionShape` POD value-class registration.
- [ ] 1.4 <!-- Non-TDD --> `Bind_Subsystems.cpp:154` remove the wrapper around the `ULocalPlayerSubsystem::Get(LocalPlayer)` global function block.
- [ ] 1.5 <!-- Non-TDD --> `Bind_UGameInstance.cpp:9` remove the wrapper around `CreateInitialPlayer` / `CreateLocalPlayer` / `AddLocalPlayer` and friends.
- [ ] 1.6 <!-- Non-TDD --> `Bind_ULocalPlayer.cpp:6` remove the wrapper around `GetGameInstance` / `GetControllerId`.
- [ ] 1.7 <!-- Non-TDD --> `Bind_UObject.cpp:54, 569` remove the two wrappers around `IsSupportedForNetworking` and the `LoadObject` global function.
- [ ] 1.8 <!-- Non-TDD --> `Bind_UWorld.cpp:48` remove the wrapper around `ServerTravel` / `GetNetMode` / `GetGameState`.
- [ ] 1.9 <!-- Non-TDD --> `AngelscriptPreprocessor.cpp:2627` remove the wrapper around `Replicated` / `ReplicationCondition` / `NotReplicated` UPROPERTY specifier processing; this is the only remaining E site outside Binds.
- [ ] 1.10 <!-- TDD --> `Tools\RunBuild.ps1 -Label haze-cat-e -TimeoutMs 180000` and `Tools\RunTests.ps1 -Suite Functional -Label haze-cat-e`. Both must be green.
- [ ] 1.11 <!-- Non-TDD --> Confirm progress: `rg -n "WITH_ANGELSCRIPT_HAZE" Plugins\Angelscript\Source` should now report 9 sites (down from 21).

## 2. Category B — delete Haze-only RPC machinery (4 sites)

- [ ] 2.1 <!-- Non-TDD --> `AngelscriptClassGenerator.cpp:3501-3512` delete the entire `#if WITH_ANGELSCRIPT_HAZE` block (sets `FUNC_NetFunction`, `FUNC_DevFunction`, `HazeFunctionFlags`, `HAZEFUNC_CrumbFunction`).
- [ ] 2.2 <!-- Non-TDD --> `AngelscriptPreprocessor.cpp:1632` delete the `else if (Spec.Name == PP_NAME_NetFunction || Spec.Name == PP_NAME_CrumbFunction)` branch and any associated `PP_NAME_DevFunction` handling. Search for `PP_NAME_NetFunction`, `PP_NAME_CrumbFunction`, `PP_NAME_DevFunction` constants and remove their declarations if no longer referenced.
- [ ] 2.3 <!-- Non-TDD --> `Bind_BlueprintType.cpp:787, 1359, 1504` remove the three `#if WITH_ANGELSCRIPT_HAZE / else if (Function->HasAnyFunctionFlags(FUNC_NetFunction)) / #endif` blocks.
- [ ] 2.4 <!-- Non-TDD --> Sweep `Script/**/*.as` and `Plugins/Angelscript/Source/AngelscriptTest/**/*.cpp` for `NetFunction`, `CrumbFunction`, `DevFunction` UFUNCTION specifiers; rewrite or remove any hit. Expected: zero hits in the active codebase.
- [ ] 2.5 <!-- TDD --> `Tools\RunBuild.ps1 -Label haze-cat-b -TimeoutMs 180000` and `Tools\RunTests.ps1 -TestPrefix Angelscript.TestModule.Networking -Label haze-cat-b`. Network RPC tests must hold.

## 3. Category C and D — namespace consolidation and behavioural decisions

- [ ] 3.1 <!-- Non-TDD --> `Bind_WorldCollision.cpp:358` keep the `System` namespace branch and delete the `AsyncTrace` alternative branch (Category C).
- [ ] 3.2 <!-- Non-TDD --> `ASClass.cpp:31-35` replace the `#if !WITH_ANGELSCRIPT_HAZE / #else / #endif` `AS_ENSURE` definition with a single unconditional `#define AS_ENSURE ensureMsgf` (Category D).
- [ ] 3.3 <!-- Non-TDD --> `AngelscriptEngine.cpp:5897` delete the `#if !WITH_EDITOR && WITH_ANGELSCRIPT_HAZE` block entirely. The cooked-only Haze exception dialog logic is removed; non-Haze cooked builds already do not emit the dialog (Category D).
- [ ] 3.4 <!-- Non-TDD --> `AngelscriptDebugServer.cpp:1911` remove the `DebugSettings.bUseAngelscriptHaze = !!WITH_ANGELSCRIPT_HAZE;` assignment (Category D). Locate the `FAngelscriptDebugDatabaseSettings` struct definition (in the same file or a paired header) and remove the `bUseAngelscriptHaze` member field.
- [ ] 3.5 <!-- Non-TDD --> `AngelscriptTest/Debugger/AngelscriptDebuggerDatabaseTests.cpp:115` delete the `TestEqual("Debugger database protocol should mirror the haze integration setting", Settings->bUseAngelscriptHaze, !!WITH_ANGELSCRIPT_HAZE)` assertion entirely.
- [ ] 3.6 <!-- TDD --> `Tools\RunBuild.ps1 -Label haze-cat-cd -TimeoutMs 180000` and `Tools\RunTests.ps1 -TestPrefix Angelscript.Editor.Debugger -Label haze-cat-cd`. Debugger DB protocol tests must hold.

## 4. Category A — restore UE-original names (1 binding site, multiple callers)

> Phase 4 begins only after `refactor-as-remove-autoaccessor` is merged. Otherwise the renamed `GetInstigator()` collides with the auto-property-accessor `GetInstigator()` synthesized for the `Instigator` UPROPERTY field.

- [ ] 4.1 <!-- TDD --> `Bind_AActor.cpp:155-175` rename the bound method signature from `"APawn GetActorInstigator() const"` to `"APawn GetInstigator() const"`. Add a regression test asserting `actor.GetInstigator()` compiles in script and returns the actor's `Instigator` pawn.
- [ ] 4.2 <!-- TDD --> `Bind_AActor.cpp:155-175` rename the bound method signature from `"AController GetActorInstigatorController() const"` to `"AController GetInstigatorController() const"`. Add a regression test for the same.
- [ ] 4.3 <!-- Non-TDD --> `GetInputComponent` keeps its name (already UE-aligned); only ensure the surrounding `#if !WITH_ANGELSCRIPT_HAZE` wrapper is removed when this line is touched.
- [ ] 4.4 <!-- Non-TDD --> Repository-wide rename sweep: `rg -n "GetActorInstigator" .` and `rg -n "GetActorInstigatorController" .` and rewrite every match in `.as` scripts, inline AS string literals (`Plugins/Angelscript/Source/AngelscriptTest/**/*.cpp`), test files, and documentation. Final state: both names return zero hits.
- [ ] 4.5 <!-- TDD --> `Tools\RunTests.ps1 -TestPrefix Angelscript.TestModule.Actor -Label haze-cat-a` and `Tools\RunTests.ps1 -TestPrefix Angelscript.TestModule.Networking -Label haze-cat-a`. Both must be green.

## 5. Macro definition removal and milestone

- [ ] 5.1 <!-- Non-TDD --> `AngelscriptEngine.h:30-32` delete the `#ifndef WITH_ANGELSCRIPT_HAZE / #define WITH_ANGELSCRIPT_HAZE 0 / #endif` block.
- [ ] 5.2 <!-- Non-TDD --> Verification gate: `rg -n "WITH_ANGELSCRIPT_HAZE" .` over the entire repository must return zero matches. If any hit remains (in `Documents/`, `Reference/`, etc.), update or remove that text.
- [ ] 5.3 <!-- Non-TDD --> Add a one-line annotation at the top of `Documents/Plans/Plan_HazelightScriptFeatureParity.md`: "Completed via OpenSpec change `refactor-as-audit-remove-with-angelscript-haze` (date)."
- [ ] 5.4 <!-- Non-TDD --> Update `Documents/Knowledges/ZH/Syntax_UFUNCTION.md` `WITH_ANGELSCRIPT_HAZE` references to read "removed in `refactor-as-audit-remove-with-angelscript-haze`".
- [ ] 5.5 <!-- Non-TDD --> Append a "Recently Completed Milestones" entry to `AGENTS.md` and `AGENTS_ZH.md`.
- [ ] 5.6 <!-- TDD --> Final regression: `Tools\RunBuild.ps1 -Label haze-final -TimeoutMs 180000` and `Tools\RunTests.ps1 -Suite Functional -Label haze-final`. The catalogued 275/275 baseline must hold.
- [ ] 5.7 <!-- Non-TDD --> `openspec validate refactor-as-audit-remove-with-angelscript-haze --strict --json` exits 0.
