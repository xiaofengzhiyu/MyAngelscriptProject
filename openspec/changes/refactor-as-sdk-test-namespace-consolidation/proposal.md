## Why

The `AngelscriptTest` module's `AngelScriptSDK/` directory accumulated heavy boilerplate: every test file wrapped its file-local helpers in a verbose, uniquely-named `_Private` namespace, and near-identical helper types (`Execute*Entry`, `FParserAccessor`, `FTokenizerAccessor`, `FBytecodeFixture`, `FMemoryBinaryStream`, `CreateModule`, `ContainsOpcode`, etc.) were copy-pasted across dozens of files. The `ASSDK` prefix also duplicated the `AS`/`SDK` concept in file names, test class names, and the `Angelscript.TestModule.AngelScriptSDK.ASSDK.*` automation path. This raises maintenance cost and obscures the actual test intent.

A naive fix (blanket-replacing `_Private` namespaces with anonymous `namespace {}`) breaks compilation: the module builds under **Unity Build**, which concatenates multiple `.cpp` into one translation unit, so identically-named symbols in different files' anonymous namespaces collide (`error C2011` redefinition). The unique `_Private` names were load-bearing. The correct fix is to genuinely share the common helpers and keep only truly file-local, collision-free symbols anonymous.

Beyond the boilerplate, the SDK tests have two substantive quality gaps. First, **test pattern**: many tests compile a script with a single `bool Entry()` that performs all assertions internally and returns one aggregate bool — so a red test reveals nothing about which case failed, and the C++↔AS argument/return path is never exercised. The project already demonstrates the better pattern in `Template_GlobalFunctions.cpp` (resolve a named function, pass typed args, assert the typed return). Second, **coverage is thin** — operators are the clearest example (many operator forms untested), but the gap spans multiple themes.

## What Changes

- Consolidate duplicated SDK test helpers into the shared `AngelscriptNativeTestSupport.h` (and `AngelscriptSDKTestExecutionHelpers.h`) so all SDK tests reference one definition:
  - `ExecuteScriptFunction<T>()` template + `ExecuteScriptVoidFunction()` replacing per-file `Execute*Entry` helpers.
  - Shared `FTokenizerAccessor`, `FParserAccessor` (superset of per-file snippet helpers), `CreateSdkModule`, `FBytecodeFixture` (parameterized with `bOptimize`), `CountInstructions`, `ContainsOpcode`, `FindFirstNodeOfType`, and `FMemoryBinaryStream` (superset with both `Truncate` and `TruncateBy`).
- Remove all `AngelscriptTest_*_Private` namespaces from `AngelScriptSDK/*.cpp`. Truly file-local helpers move to anonymous namespaces; same-named-but-divergent free functions (e.g. `ParseScript` variants with different `Verify` signatures) are renamed file-uniquely (e.g. `ParseDeclScript`, `ParseShapeScript`, `ParseRangeScript`).
- Rename `ASSDK` → `SDK` across the directory: file names (`AngelscriptASSDK*Tests.cpp` → `AngelscriptSDK*Tests.cpp`), test class names (`FAngelscriptASSDK*` → `FAngelscriptSDK*`), helper types/functions (`FASSDKBufferedOutStream`, `FASSDKBytecodeStream`, `CreateASSDKTestEngine`, `ASSDKExecuteString`, etc.), and automation IDs (`...AngelScriptSDK.ASSDK.*` → `...AngelScriptSDK.*`).
- Expand `AngelscriptSDKOperatorTests` coverage from a partial set to a full operator matrix (arithmetic, comparison, logical, bitwise, assignment/compound, ternary, pow, opCall, opIndex, string concatenation, short-circuit).
- **Modernize the test pattern away from monolithic `bool Entry()` self-checks.** Today many SDK tests compile a script whose single `Entry()` function does all the work and returns one bool, then C++ only asserts that bool. This is low-value: a failure says nothing about *which* case broke, and arguments/return values are never exercised across the C++/AS boundary. Following the `Template_GlobalFunctions.cpp` example, tests SHALL instead call **specific named AS functions with typed arguments and assert typed return values**, one behavior per call. Because SDK tests run on the raw `asIScriptEngine`/`asCScriptEngine` (below the UE `FAngelscriptEngine` wrapper that `FASGlobalFunctionInvoker` requires), add a shared raw-engine invoker helper (e.g. `FSdkFunctionInvoker` / `CallSdkFunction<R>(...)`) that mirrors the template's ergonomics on `asIScriptContext` (resolve-by-decl, `AddArg`, typed return).
- **Raise overall coverage, not only operators.** A gap audit of the functional/runtime themes (each currently only 3–7 test methods) revealed three systemic problems and a prioritized gap list:
  - **"Compile-only" false coverage** — `AngelscriptObjectTests` (all 3), `AngelscriptOOPTests` (mixin + inheritance), `Type.Auto`, `Conversion.ImplicitValueType`, `CallingConv.Thiscall` only assert that the module compiles and the function resolves; they never execute or assert behavior. These will be upgraded to actually run and assert.
  - **Name/implementation mismatch** — `Runtime.Suspend` does not suspend; `OOP.InheritedInterfaceMethod` tests plain inheritance, not interface implementation. Fix the test to match its name (or rename).
  - **Dead scaffolding** — `AngelscriptObjectTests` defines `ReturnObjectValue`/`ReturnObjectRef`/`CFloatWrapper` operator helpers that nothing calls; restore the intended object-return / operator-overload coverage.
  - **High-priority new coverage** (core language semantics with zero/false runtime coverage): `array<T>`/`TArray<T>` runtime (length/index/insert/remove/out-of-bounds); script-side OOP polymorphism (base-handle → overridden method, `override`/`final`/`abstract`, script class implementing a script interface, multiple interfaces); property accessors (`get`/`set`); handle reference counting and `cast<T>()` up/down-cast + null-handle deref; object lifetime runtime (destructor call count/order, object as return value); function `&in` params + real overload resolution (with ambiguity negative cases); true Suspend/Resume; runtime exception varieties (array OOB, null handle, abort); script-class operator overloads (`opAdd`/`opEquals`/`opCmp`/`opAssign`).
  - **Medium/low-priority** items recorded in `tasks.md` §6 (module `import`/`shared`, integer widths/overflow, `opConv`/`opImplConv` execution, composite-type native arg round-trips, Thiscall execution fork-crash investigation, global init order, dictionary/`TMap`, GC incremental/weakref). The disabled `AngelscriptStringUtilTests.cpp` (`#if 0`, blocked on a `RegisterStringFactory` API difference between the 2.33 fork and 2.38) is a tracked prerequisite, not silently dropped.
- **Constraint:** existing passing behavior is preserved or strengthened; this is a structural/naming refactor plus additive, more granular coverage — not a silent behavior change. Where a test currently only compiles, upgrading it to execute MAY surface a real fork bug; such cases are recorded as explicit expected-fail or follow-up items rather than hidden.
- **Audit trail:** a follow-up note (`sdk-test-audit.md`) records the source-level `AngelScriptSDK/` inventory, the CQTest class-level fixture boundary, the intentional direct-engine exceptions, and the naming guidance for future helper additions so the continuation work stays traceable.

## Capabilities

### New Capabilities
<!-- No new product/spec capability: this is internal test-code structure. -->
- None.

### Modified Capabilities
<!-- No spec-level requirement changes; test internals only. -->
- None.

## Impact

- Code: `Plugins/Angelscript/Source/AngelscriptTest/AngelScriptSDK/*.cpp` (~50 files) and `AngelscriptNativeTestSupport.h`, `AngelscriptTestAdapter.h`, plus new `AngelscriptSDKTestExecutionHelpers.h` / `AngelscriptSDKTestUtilities.h`.
- Build/Test: validated only via `Tools\RunBuild.ps1` then `Tools\RunTestSuite.ps1`. Must compile cleanly under Unity Build before the change is considered done.
- Automation IDs: consumers filtering on `Angelscript.TestModule.AngelScriptSDK.ASSDK.*` must switch to `Angelscript.TestModule.AngelScriptSDK.*`.
- Risk: Unity Build symbol collisions; mitigated by sharing identical helpers and renaming divergent ones. See `Documents/...` and the project memory note on the Unity Build constraint.
