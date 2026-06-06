## Why

The previous C++ learning-trace tooling (snapshot-style, 6 fixed scenarios, ad-hoc fields) produced output that was hard to teach from. After deleting it, we want to rebuild on a cleaner contract: a comprehensive, time-ordered event stream that captures the *decision process* of each AS pipeline phase — not just the final state. This is the data the future web animation layer will consume.

This change establishes the framework and ships the first phase tap (Tokenizer) walking through a curated example set.

## What Changes

- Add a `LearningTrace/` subsystem under `AngelscriptEditor/` with a phase-tap framework: `ILearningTracePhaseTap` interface, `FLearningTraceEvent` data shape, `FLearningTraceEventStream` accumulator, `FLearningTraceExample` curated-snippet metadata.
- Add `FTokenizerTap` — subclass of `asCTokenizer` that reuses the original protected `IsWhiteSpace` / `IsComment` / `IsConstant` / `IsIdentifier` / `IsKeyWord` helpers (no logic copy) and emits per-decision `try-X` and `token-emitted` events while iterating a source.
- Add `LearningTraceExampleRegistry` populated with 5–10 curated AS snippets covering tokenizer decision branches.
- Add `FLearningTraceExporter` that drives examples × taps and writes one JSON file per example.
- Add `UAngelscriptLearningTraceCommandlet` UCommandlet entry point.
- Add Editor automation tests, including a *drift guard* test that asserts `FTokenizerTap`'s final tokens equal `asCTokenizer`'s on every example.

## Capabilities

### New Capabilities

- `as-learning-trace-event-stream`: Comprehensive AS pipeline trace exporter producing time-ordered event streams suitable for downstream teaching animation. Initial scope = Tokenizer phase; framework supports adding Parser/Builder/ClassGen/VM phase taps in later changes without touching Core or Exporter.

### Modified Capabilities

None.

## Impact

- Adds `Plugins/Angelscript/Source/AngelscriptEditor/LearningTrace/` (new subsystem, ~9 new files plus 1 test file).
- One Build.cs delta on `AngelscriptEditor.Build.cs` (PrivateIncludePaths only).
- New commandlet `AngelscriptLearningTrace` runnable via `Tools\RunCommandlet.ps1`.
- JSON output written under `Saved/LearningTrace/` by default.
- Does NOT touch: Runtime module, Test module, `Experiment/ASRuntimeVisualizer/`, any deleted v1 tooling.
- Does NOT add new module dependencies (Json/JsonUtilities propagate transitively from `AngelscriptRuntime`).
