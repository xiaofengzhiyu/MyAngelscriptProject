## 1. OpenSpec And Baseline

- [x] 1.1 <!-- Non-TDD --> Validate the new change structure with `openspec validate "test-uasfunction-dispatch-matrix-and-jit-paths" --strict --json`.
- [x] 1.2 <!-- Non-TDD --> Run baseline ASFunction and StaticJIT AOT prefixes before implementation to capture current behavior: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.ClassGenerator.ASFunction" -Label uasfunction-matrix-baseline -TimeoutMs 600000` and `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.StaticJIT.AOT" -Label uasfunction-jit-baseline -TimeoutMs 600000`.

## 2. ASFunction Dispatch Matrix

- [x] 2.1 <!-- TDD --> Extend `AngelscriptASFunctionDispatchTests.cpp` with failing allocation assertions for representative non-thread-safe wrapper shapes: no-param, byte/bool/dword/qword/float/double/float-extended arg, reference arg, byte/dword/float/double/float-extended/object return, and generic fallback.
- [x] 2.2 <!-- TDD --> Add dispatch-boundary assertions for thread-safe, static, and virtual/non-final functions so tests document when generic wrappers are required instead of specialized non-virtual wrappers.
- [x] 2.3 <!-- Non-TDD --> Run Phase 2 verification: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.ClassGenerator.ASFunction" -Label uasfunction-dispatch-matrix -TimeoutMs 600000`.

## 3. StaticJIT UASFunction Paths

- [x] 3.1 <!-- TDD --> Extend the StaticJIT AOT fixture with UASFunction-accessible class methods covering at least one primitive argument, one primitive return, one reference writeback, one object return, and one static/world-context-sensitive function.
- [x] 3.2 <!-- TDD --> Add failing AOT tests proving the target functions have non-null JIT entries and that invocation through `UASFunction::RuntimeCallEvent` reaches generated code via a test-visible marker.
- [x] 3.3 <!-- Non-TDD --> Regenerate StaticJIT AOT artifacts through the commandlet workflow and rebuild so generated source participates in `AngelscriptTest`.
- [x] 3.4 <!-- Non-TDD --> Run Phase 3 verification: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.StaticJIT.AOT" -Label uasfunction-jit-paths -TimeoutMs 600000`.

## 4. Final Verification

- [x] 4.1 <!-- Non-TDD --> Run focused final tests: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.ClassGenerator.ASFunction" -Label uasfunction-matrix-final -TimeoutMs 600000` and `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.StaticJIT.AOT" -Label uasfunction-jit-final -TimeoutMs 600000`.
- [x] 4.2 <!-- Non-TDD --> Run build and OpenSpec validation: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1 -Label uasfunction-dispatch-jit -TimeoutMs 180000` and `openspec validate "test-uasfunction-dispatch-matrix-and-jit-paths" --strict --json`.

## 5. StaticJIT Reference Regression Coverage

- [x] 5.1 <!-- TDD --> Add focused precompiled-data regression tests for stable global reference names and repeated-load runtime cache reset.
- [x] 5.2 <!-- Non-TDD --> Add source comments documenting the two StaticJIT reference pitfalls near the relevant code paths.
- [x] 5.3 <!-- Non-TDD --> Run focused StaticJIT precompiled-data verification.
