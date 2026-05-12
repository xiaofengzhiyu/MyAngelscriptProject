## Why

`UASFunction` dispatch now has positive coverage for direct optimized calls, `ProcessEvent`, direct `RuntimeCallEvent`, metadata predicates, and StaticJIT AOT `Context->Execute()`. The remaining risk is structural: tests do not prove enough of the `AllocateFunctionFor` wrapper matrix, and StaticJIT AOT tests do not prove generated JIT is reached through `UASFunction` wrappers.

## What Changes

- Expand `UASFunction` allocation coverage across representative wrapper shapes so regressions in subclass selection are caught directly.
- Add deterministic JIT-path coverage that proves generated StaticJIT AOT functions are attached to script functions and reached through `UASFunction` dispatch, not only through raw `asIScriptContext::Execute()`.
- Compare JIT and non-JIT observable behavior for ABI-sensitive shapes including primitive arguments, primitive returns, reference writeback, object return, static/world-context behavior, and virtual override boundaries.
- Keep production runtime behavior unchanged unless a failing test exposes a real defect; prefer test fixtures and narrow test-support hooks over broad runtime API changes.

## Capabilities

### New Capabilities

- `uasfunction-dispatch-matrix-and-jit-paths`: Regression coverage for `UASFunction` wrapper selection and JIT-backed dispatch paths.

### Modified Capabilities

- `uasfunction-runtime-dispatch-coverage`: Extends the existing behavior-focused ASFunction coverage with structural dispatch and JIT path assertions.
- `as-static-jit-aot-test`: Extends AOT runtime verification from global context execution into `UASFunction` dispatch.

## Impact

- `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/`: expands ASFunction dispatch/allocation and invocation matrix tests.
- `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/`: expands AOT fixtures and runtime tests for UASFunction-backed JIT execution.
- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.*`: only touched if test-only observation or a real defect fix is required.
