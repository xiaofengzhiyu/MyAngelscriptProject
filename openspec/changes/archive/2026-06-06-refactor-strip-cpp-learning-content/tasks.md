## 1. OpenSpec scaffold

- [x] 1.1 Cancel the obsolete `refactor-learning-trace-export-to-editor` change directory.
- [x] 1.2 Create `refactor-strip-cpp-learning-content` change with proposal, design, tasks, and three REMOVED spec files.

## 2. Pre-flight verification

- [x] 2.1 Confirm workbench probe diff in `AngelscriptClassGenerator.cpp` is bounded to additions only (no edits to other call sites).
- [x] 2.2 Confirm no non-Learning code references `AngelscriptLearningTrace.h`, `AngelscriptTeachingTrace.h`, `FAngelscriptLearningTrace`, `FAngelscriptTeachingTrace`, or `IAngelscriptTeachingTraceSink`.
- [x] 2.3 Confirm no non-Learning code references the deleted exporter API (`ExportAngelscriptLearningTrace`, `EAngelscriptLearningTraceExportScenario`, `UAngelscriptLearningTraceExportCommandlet`).

## 3. Delete C++ Learning content

- [x] 3.1 Delete `Plugins/Angelscript/Source/AngelscriptTest/Learning/` (entire tree: `Native/`, `Runtime/`, `Export/`).
- [x] 3.2 Delete `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptLearningTrace.h`.
- [x] 3.3 Delete `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptLearningTrace.cpp`.
- [x] 3.4 Delete `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptLearningTraceTests.cpp`.
- [x] 3.5 Delete `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTeachingTrace.h`.
- [x] 3.6 Delete `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTeachingTrace.cpp`.

## 4. Revert workbench probes

- [x] 4.1 `git checkout -- Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` (drop the workbench-introduced `EmitClassTeachingTraceEvent` / `EmitTeachingTraceEvent` namespace and 5 probe call sites).

## 5. Documentation cleanup (in-scope)

- [x] 5.1 Update `Plugins/Angelscript/Source/AngelscriptTest/Shared/README.md` to remove `AngelscriptLearningTrace.h` from the "Other Shared/* headers" inventory list.

## 6. Verification

- [x] 6.1 `Tools\RunBuild.ps1 -Clean` then `Tools\RunBuild.ps1` — exit code 0, no missing-include or missing-symbol errors.
- [x] 6.2 `Tools\RunTests.ps1 -TestFilter "Angelscript.TestModule"` — all remaining tests pass; no Learning-prefixed tests run (because they're deleted). Verified with Bindings smoke run (252/252) and confirmed Learning filter returns "No automation tests matched".
- [x] 6.3 `openspec status --change "refactor-strip-cpp-learning-content"` — change is well-formed and archive-ready (verified `isComplete: true`).

## 7. Decommission decision (defer to user before commit)

- [x] 7.1 Decided with user: keep `Experiment/ASRuntimeVisualizer/` as frozen archival demo. No edits to that directory in this change.
