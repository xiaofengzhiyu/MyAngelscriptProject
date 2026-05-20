## Examples Test Classification

This classification is the review gate before retiring `Plugins/Angelscript/Source/AngelscriptTest/Examples/`.

| File | Classification | Functional destination / rationale |
| --- | --- | --- |
| `AngelscriptScriptExampleCoverageTests.cpp` | `Absorb` | Split actor defaults/UFUNCTION coverage into `Functional/Actor`, component lifecycle/owner coverage into `Functional/Component`, UObject defaults/UFUNCTION coverage into `Functional/Objects`, and property metadata/default component hierarchy into `Functional/Property` plus `Functional/Component`. |
| `AngelscriptScriptExampleActorTest.cpp` | `Absorb` | Actor defaults, tags, `BeginPlay`, script-only method, and `BlueprintEvent` behavior are covered by `Functional/Actor` and `Actor.ScriptOverride`; add any missing default/tag assertion before deletion. |
| `AngelscriptScriptExampleConstructionScriptTest.cpp` | `Absorb` | `Functional/Actor/AngelscriptActorLifecycleTests.cpp` already covers construction script execution and recomputation; keep as target for any missing default-component style assertion. |
| `AngelscriptScriptExampleMovingObjectTest.cpp` | `Absorb` | Movement, default root/attached components, tick-driven state, and vector mutation belong in `Functional/Actor` and `Functional/Component`. |
| `AngelscriptScriptExampleOverlapsTest.cpp` | `Absorb` | Actor overlap overrides are already in `Functional/Actor/AngelscriptActorInteractionTests.cpp`; add component `OnComponentBeginOverlap.AddUFunction()` behavior. |
| `AngelscriptScriptExampleDelegatesTest.cpp` | `Absorb` | Delegate properties, `AddUFunction`, broadcasting, unbind/clear, and signature mismatch are already covered under `Functional/Delegate`; no example-shaped test should remain. |
| `AngelscriptScriptExampleTimersTest.cpp` | `Absorb` | Timer handle pause/unpause/clear behavior is already covered in `Functional/Actor/AngelscriptActorTimerRuntimeBehaviorTests.cpp`; no separate compile-only test should remain. |
| `AngelscriptScriptExampleWidgetUmgTest.cpp` | `Absorb` | `BindWidget` metadata/type coverage exists in `Functional/Widget`; add widget lifecycle signature coverage where feasible. |
| `AngelscriptScriptExamplePropertySpecifiersTest.cpp` | `Absorb` | Metadata and property flags belong in `Functional/Property`; default components/root/attach hierarchy belongs in `Functional/Component`. |
| `AngelscriptScriptExampleBehaviorTreeNodesTest.cpp` | `DeleteAsCovered` | Current example is compile-only and would require new AI/BT execution infrastructure to become behavior coverage; no new infrastructure is in scope for this refactor. |
| `AngelscriptScriptExampleCharacterInputTest.cpp` | `DeleteAsCovered` | Current example is compile-only and duplicates input binding syntax already exercised by script/class generation surfaces; real input dispatch coverage needs separate input harness work. |
| `AngelscriptScriptExampleAccessSpecifiersTest.cpp` | `DeleteAsCovered` | Covered by syntax/access-specifier tests; example adds compile-only duplication. |
| `AngelscriptScriptExampleArrayTest.cpp` | `DeleteAsCovered` | Covered by container/bindings tests; example adds compile-only duplication. |
| `AngelscriptScriptExampleMapTest.cpp` | `DeleteAsCovered` | Covered by container/bindings tests; example adds compile-only duplication. |
| `AngelscriptScriptExampleEnumTest.cpp` | `DeleteAsCovered` | Covered by enum/syntax/class-generation tests; example adds compile-only duplication. |
| `AngelscriptScriptExampleFormatStringTest.cpp` | `DeleteAsCovered` | Covered by string interpolation/formatting and text/string binding tests; example adds compile-only duplication. |
| `AngelscriptScriptExampleMixinMethodsTest.cpp` | `DeleteAsCovered` | Covered by actor/function mixin and bindings tests; example adds compile-only duplication. |
| `AngelscriptScriptExampleFunctionsTest.cpp` | `DeleteAsCovered` | Covered by function, UFUNCTION, and script override tests; example adds compile-only duplication. |
| `AngelscriptScriptExampleFunctionSpecifiersTest.cpp` | `DeleteAsCovered` | Covered by syntax/UFUNCTION and networking tests; example adds compile-only duplication. |
| `AngelscriptScriptExampleMathTest.cpp` | `DeleteAsCovered` | Covered by math module and bindings tests; example adds compile-only duplication. |
| `AngelscriptScriptExampleStructTest.cpp` | `DeleteAsCovered` | Covered by struct/type declaration and object model tests; example adds compile-only duplication. |
| `AngelscriptScriptExampleTestSupport.cpp` | `DeleteAfterDependencyMove` | Remove after all `ScriptExamples.*` tests are deleted. |
| `AngelscriptScriptExampleTestSupport.h` | `DeleteAfterDependencyMove` | Remove after all `ScriptExamples.*` tests are deleted. |
