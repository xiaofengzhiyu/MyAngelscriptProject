# Tasks: refactor-as-sdk-test-namespace-consolidation

## 1. Shared helper foundation

- [x] 1.1 Add `ExecuteScriptFunction<T>()` + `ExecuteScriptVoidFunction()` to `AngelscriptSDKTestExecutionHelpers.h`
- [x] 1.2 Add compatibility aliases in `AngelscriptSDKTestUtilities.h`
- [x] 1.3 Add shared `FTokenizerAccessor`, `FParserAccessor` (superset), `CreateSdkModule` to `AngelscriptNativeTestSupport.h`
- [x] 1.4 Add shared `FBytecodeFixture` (bOptimize flag), `CountInstructions`, `ContainsOpcode`, `FindFirstNodeOfType`
- [x] 1.5 Add shared `FMemoryBinaryStream` (superset: `Truncate` + `TruncateBy`); add `Misc/ScopeExit.h` include
- [x] 1.6 Add shared `ContainsError` + `CompileSnippet` for reference-error tests

## 2. ASSDK → SDK rename

- [x] 2.1 Rename 3 files: `AngelscriptASSDK{Function,Operator,Type}Tests.cpp` → `AngelscriptSDK*`
- [x] 2.2 Rename test classes `FAngelscriptASSDK*` → `FAngelscriptSDK*`
- [x] 2.3 Rename helper types/functions (`FASSDKBufferedOutStream`, `FASSDKBytecodeStream`, `CreateASSDKTestEngine`, `ASSDKExecuteString`, `RegisterASSDKAssert`, `GetASSDKAdapter`, `ASSDK_TEST_FAILED`) in `AngelscriptTestAdapter.h` and callers
- [x] 2.4 Collapse automation path `...AngelScriptSDK.ASSDK.*` → `...AngelScriptSDK.*`

## 3. Remove _Private namespaces (per cluster)

- [x] 3.1 Function/Operator/Type SDK tests
- [x] 3.2 CallFunc, CallingConv, Conversion, Module tests
- [x] 3.3 Tokenizer cluster (Literals, Whitespace, Operators, ReferenceTokenizer, TokenizerTests) — use shared `FTokenizerAccessor`
- [x] 3.4 Parser/ScriptNode cluster (Declarations, Errors, Expressions, Copy, Shape, SourceRange) — shared `FParserAccessor`/`CreateSdkModule`, rename divergent free funcs (`ParseDeclScript`, `ParseShapeScript`, `ParseRangeScript`)
- [x] 3.5 Bytecode cluster (Jumps, Opcodes, Optimize) — shared `FBytecodeFixture`/`ContainsOpcode`/`CountInstructions`
- [x] 3.6 Restore + ReferenceSaveLoad — shared `FMemoryBinaryStream`
- [x] 3.7 ReferenceCompilerReject + ReferenceParserError — shared `ContainsError`/`CompileSnippet`
- [x] 3.8 Remaining `_Private` files: ConfigGroup, OOP, Object, Runtime

## 4. Operator coverage expansion

- [x] 4.1 Expand `AngelscriptSDKOperatorTests` to full matrix (arithmetic, comparison, logical, bitwise, assignment, ternary, pow, opCall, opIndex, string concat, short-circuit)

## 5. Test-pattern modernization (named functions instead of Entry())

- [x] 5.1 Add shared raw-engine invoker (`FSdkFunctionInvoker`) on `asIScriptContext` — resolve-by-decl, typed args, typed return
- [x] 5.2 Convert `bool Entry()` self-check tests to per-function named calls with typed args/returns in the six behavioral SDK themes (Function, Operator, Type, Conversion, OOP, Object)
- [x] 5.3 Confirm no remaining single-aggregate-bool `Entry()` pattern in those six behavioral SDK tests
- [x] 5.4 Add CQTest class-level raw-engine fixtures (`FNativeSdkEngineFixture`, `FNativeSdkAdapterEngineFixture`) using `BEFORE_ALL()` / `AFTER_ALL()` and per-method `BEFORE_EACH()` reset hooks
- [x] 5.5 Migrate ordinary AngelScriptSDK CQTest classes to shared class-level engine lifecycle; keep per-method engines only where tests validate engine lifecycle/config isolation, bytecode save/load, stack limits, or internal `asCScriptEngine` parser/bytecode state

## 6. Broader coverage audit & expansion

### 6a. Fix systemic problems
- [x] 6.1 Audit each SDK theme for coverage gaps (done — see audit summary in proposal §What Changes)
- [ ] 6.2 Upgrade "compile-only" false-coverage tests to execute + assert: `AngelscriptObjectTests` (3), `AngelscriptOOPTests` (mixin, inheritance), `Type.Auto`, `Conversion.ImplicitValueType`
- [ ] 6.3 Fix name/impl mismatches: `Runtime.Suspend` (make it actually suspend/resume), `OOP.InheritedInterfaceMethod` (test interface impl or rename)
- [ ] 6.4 Restore dead scaffolding in `AngelscriptObjectTests` (`ReturnObjectValue`/`ReturnObjectRef`/`CFloatWrapper`) into real object-return / operator tests

### 6b. High-priority new coverage
- [ ] 6.5 `array<T>` / `TArray<T>` runtime: construct, length, index read/write, insertLast/removeAt, out-of-bounds exception
- [ ] 6.6 OOP polymorphism: base-handle → overridden method, `override`/`final`/`abstract`, script class implements script interface, multiple interfaces, super/base-ctor chain
- [ ] 6.7 Property accessors (`get`/`set`) runtime
- [ ] 6.8 Handle reference counting (`@` AddRef/Release count), null-handle deref exception, `cast<T>()` up/down-cast
- [ ] 6.9 Object lifetime runtime: destructor call count + order, object as return value
- [~] 6.10 Functions: `&in` param ✅, type-based overloads ✅, recursion ✅ (added); still TODO: overload ambiguity negative cases, funcdef/function-pointer args
- [~] 6.11 Runtime: exception varieties ✅ (divide/modulo-by-zero + exception string/line/function details). NOTE: true Suspend/Resume/Abort + line callbacks are NOT testable — this fork has no SetLineCallback and SetInstructionCallback is an unimplemented stub (see memory angelscript-sdk-bare-engine-limits). Marked done within engine capability.
- [ ] 6.12 Script-class operator overloads: `opAdd`/`opEquals`/`opCmp`/`opAssign` (bare engine can't instantiate script classes; compile+resolve or UE-wrapper engine)

### 6c. Medium-priority new coverage
- [x] 6.13 Module multi-section build ✅, enumerate functions ✅, recompile-after-discard ✅ (import/`shared` deferred — cross-module needs more setup)
- [~] 6.14 Integer full-width round-trip ✅, overflow/wrap ✅, enum↔int ✅ (added to Type tests); still TODO: `opConv`/`opImplConv` execution
- [~] 6.15 Native context param coverage: int64 wide return ✅, mixed int+double args ✅ (added to CallFunc); object/handle/string/array round-trip still TODO (bare-engine limits apply)
- [ ] 6.16 Thiscall execution — investigate the fork crash (possible real bug; record expected-fail or fix)
- [~] 6.17 VariableScope for-init scope ✅ + deep shadowing ✅ (added); global var init order + object-typed global lifetime still TODO

### 6d. Low-priority / prerequisites
- [ ] 6.18 String runtime: resolve `RegisterStringFactory` 2.33↔2.38 API difference (prerequisite), then re-enable `AngelscriptStringUtilTests.cpp` (`#if 0`) — methods/index/concat/compare/parse
- [ ] 6.19 Dictionary / `TMap` runtime; deep-scope shadowing; GC incremental (`asGC_ONE_STEP`)/weakref/script-driven cycle; TypeRegistry nested templates
- [ ] 6.20 Update `Documents/Guides/TestCatalog.md` scale + verification snapshot
- [x] 6.21 Record the SDK audit context, source-level test inventory, and naming rationale in OpenSpec (`sdk-test-audit.md`)

## 7. Verification

- [x] 7.1 `Tools\RunBuild.ps1` compiles clean under Unity Build (0 errors) — Result: Succeeded, FinalExitCode 0 (latest run `Saved\Build\build\20260617_204058_413_ff9e4af1`)
- [x] 7.2 `Tools\RunTests.ps1` — AngelScriptSDK tests pass — 342/342 PASS, 0 failed (RunTests prefix `Angelscript.TestModule.AngelScriptSDK`, latest report `Saved\Tests\Angelscript.TestModule.AngelScriptSDK\20260617_204107_119_1c6dfb52\Report\index.json`)
- [ ] 7.3 Remove scratch artifacts (`refactor_private_namespaces.sh`, `REFACTORING_*.md`) before finalizing
- [ ] 7.4 Commit submodule + parent pointer with accurate (non-broken-build) state
