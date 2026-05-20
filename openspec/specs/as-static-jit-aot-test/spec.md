# as-static-jit-aot-test Specification

## Purpose
TBD - created by archiving change refactor-as-static-jit-test. Update Purpose after archive.
## Requirements
### Requirement: StaticJIT AOT source generation is reproducible

The test module SHALL provide a documented commandlet workflow that generates StaticJIT C++ artifacts from committed test fixtures and detects when checked-in generated artifacts are stale.

#### Scenario: Generate checked-in AOT source

- WHEN the StaticJIT AOT generation commandlet runs in generate mode
- THEN it writes deterministic generated StaticJIT C++ artifacts under the Angelscript test module source tree
- AND it writes the matching precompiled cache as a local ignored test prerequisite
- AND the generated artifacts correspond to the committed test fixtures

#### Scenario: Detect stale generated source

- WHEN the StaticJIT AOT generation commandlet runs in verify mode after fixtures or generation logic changed
- THEN it compares regenerated output with the checked-in generated artifacts
- AND it semantically compares the local precompiled cache with regenerated cache output
- AND it reports a failure if the generated output is stale or the local cache is missing or stale

### Requirement: Generated StaticJIT code participates in the test build

The generated StaticJIT `.cpp` artifacts SHALL live inside the `AngelscriptTest` module source tree so the normal Unreal build discovers and compiles them after generation.

#### Scenario: Rebuild after generation compiles generated source

- WHEN generated StaticJIT C++ artifacts exist under the test module source tree
- THEN the standard project build compiles those generated `.cpp` files without manually listing them in `AngelscriptTest.Build.cs`

### Requirement: Runtime tests prove AOT registration and dispatch

The StaticJIT AOT test suite SHALL prove that generated code is registered, attached to the target AngelScript function, and executed through the normal script execution path.

#### Scenario: Generated function is registered

- WHEN the StaticJIT AOT tests run after the generated code has been built
- THEN the generated fixture function is present in the StaticJIT registration database

#### Scenario: AngelScript function has a JIT entry point

- WHEN the fixture module is loaded for AOT execution
- THEN the target script function has a non-null `jitFunction`

#### Scenario: Normal execution reaches generated code

- WHEN the target fixture function is invoked through `Context->Execute()`
- THEN a test-visible marker proves the generated StaticJIT entry point ran
- AND the function result matches the expected AngelScript behavior

### Requirement: Multi-engine StaticJIT constraints are visible

The StaticJIT AOT test suite SHALL include focused diagnostics for current global-state constraints rather than hiding them behind source-generation-only checks.

#### Scenario: Multiple engines expose compiled-info constraints

- WHEN two test engines attempt to use StaticJIT AOT state in the same process
- THEN the test result documents whether the current implementation supports the sequence or reports the specific singleton/global-state constraint

