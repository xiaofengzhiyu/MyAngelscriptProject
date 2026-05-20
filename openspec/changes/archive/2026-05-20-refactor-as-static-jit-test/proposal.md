## Why

StaticJIT already has helper, source-generation, archive, and native-bridge tests, but the suite does not prove that generated StaticJIT C++ is compiled into the test module and used by AngelScript execution. This change closes that gap with a repeatable AOT test workflow that exercises generation, rebuild integration, registration, and runtime dispatch.

## What Changes

- Add a StaticJIT AOT test capability for `AngelscriptTest`.
- Add commandlet-driven generation and verification of checked-in StaticJIT test C++ artifacts.
- Add runtime tests that prove generated StaticJIT functions are registered, attached to script functions, and executed through `Context->Execute()`.
- Add focused multi-engine diagnostics that expose StaticJIT global-state conflicts relevant to AOT test execution.
- Avoid manually listing generated `.cpp` files in `AngelscriptTest.Build.cs`; generated source remains under the module source tree so UBT discovers it on rebuild.

## Capabilities

### New Capabilities

- `as-static-jit-aot-test`: StaticJIT AOT generation and execution tests for the Angelscript test module.

### Modified Capabilities

- None.

## Impact

- `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/`: may receive small test-support hooks if required to observe compiled JIT registration or execution without changing production behavior.
- `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/`: new AOT fixtures, generated source artifacts, commandlet entry point, and automation tests.
- `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`: may receive test-only include or macro adjustments, but must not enumerate generated `.jit.cpp` files.
- `Documents/Guides/Test.md` and related test documentation: add the StaticJIT AOT workflow and validation entry points if the implementation changes standard test usage.
