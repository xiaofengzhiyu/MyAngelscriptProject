## ADDED Requirements

### Requirement: UASFunction wrapper allocation is covered by representative matrix tests

The ASFunction test suite SHALL assert the generated `UASFunction` subclass selected for representative function shapes rather than only validating call results.

#### Scenario: Specialized non-thread-safe wrapper shapes are selected

- WHEN script class functions are generated for no-param, single primitive argument, reference argument, primitive return, and object return shapes
- THEN each generated function uses the expected specialized `UASFunction` wrapper class for its reflected ABI shape
- AND each wrapper can still execute the script behavior it represents

#### Scenario: Generic boundaries stay generic

- WHEN a generated function is thread-safe, static, virtual/non-final, multi-argument, or otherwise unsupported by a specialized wrapper
- THEN the generated function uses the appropriate generic `UASFunction` wrapper
- AND the test documents why the specialized non-virtual wrapper is not expected

### Requirement: StaticJIT AOT verifies UASFunction-backed JIT execution

The StaticJIT AOT tests SHALL prove generated JIT entries are reached through `UASFunction` dispatch for representative reflected script methods.

#### Scenario: UASFunction targets expose JIT entries

- WHEN the AOT fixture is loaded from generated precompiled data
- THEN the target script methods have non-null JIT entry pointers required by UASFunction dispatch
- AND the functions remain discoverable through the generated Unreal class/function metadata

#### Scenario: RuntimeCallEvent reaches generated JIT code

- WHEN a JIT-capable generated `UASFunction` is invoked through reflected parameter memory
- THEN a test-visible generated-code marker proves StaticJIT generated code ran
- AND primitive return values, primitive arguments, reference writeback, object return identity, and static/world-context-sensitive behavior match expected script semantics

### Requirement: JIT and non-JIT dispatch boundaries are explicit

Tests SHALL make the current JIT dispatch limitations visible so later runtime refactors can distinguish intentional behavior from missing coverage.

#### Scenario: Virtual override is not bypassed by JIT-capable paths

- WHEN a parent generated function is invoked on a child script object that overrides it
- THEN dispatch resolves the child override rather than blindly calling the parent raw JIT entry

#### Scenario: Thread-safe JIT behavior is documented

- WHEN a thread-safe generated function has JIT entries available
- THEN the test records whether the current wrapper uses generic context behavior or a direct JIT branch
- AND the observable result remains correct

## Testing Requirements

- Target test layers: Runtime Integration under `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/` and `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/`.
- Expected Automation prefixes: `Angelscript.TestModule.ClassGenerator.ASFunction` and `Angelscript.TestModule.StaticJIT.AOT`.
- Verification entry points:
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.ClassGenerator.ASFunction" -Label uasfunction-matrix-final -TimeoutMs 600000`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.StaticJIT.AOT" -Label uasfunction-jit-final -TimeoutMs 600000`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -Label uasfunction-dispatch-jit -TimeoutMs 180000`
