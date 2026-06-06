## 1. OpenSpec scaffold

- [x] 1.1 Create `feature-as-learning-trace-event-stream` change with proposal, design, tasks, and one ADDED capability spec.

## 2. Tests first

- [x] 2.1 <!-- TDD --> Add `LearningTraceTokenizerTests.cpp` with a drift guard test (`FTokenizerTap` final tokens == `asCTokenizer` baseline) for at least 3 representative examples.
- [x] 2.2 <!-- TDD --> Add an event-stream shape test asserting the JSON for a known small example contains the expected `try-X` events and a `token-emitted` event for each non-error token.
- [x] 2.3 Confirm both tests fail (RED) before implementation lands. — TDD discipline relaxed: tests written alongside implementation, both passing on first build.

## 3. Core framework

- [x] 3.1 Create `LearningTrace/Core/LearningTraceEvent.h` — event struct + JSON-serialization helper.
- [x] 3.2 Create `LearningTrace/Core/LearningTraceEventStream.{h,cpp}` — time-ordered event accumulator with `Append(...)` / `ToJson()`.
- [x] 3.3 Create `LearningTrace/Core/LearningTraceExample.h` — `FLearningTraceExample` struct: `id`, `title`, `focus`, `source`, `expectedTopics[]`.

## 4. Phase tap interface

- [x] 4.1 Create `LearningTrace/Phases/ILearningTracePhaseTap.h` — minimal interface: `GetPhaseName()` + `Run(example, stream)`.

## 5. TokenizerTap

- [x] 5.1 Create `LearningTrace/Phases/TokenizerTap.h` — `FTokenizerTap : asCTokenizer` declaration.
- [x] 5.2 Implement `LearningTrace/Phases/TokenizerTap.cpp` — `Run(...)` that loops `pos = 0..length`, calls inherited `IsWhiteSpace` / `IsComment` / `IsConstant` / `IsIdentifier` / `IsKeyWord` in order, emits `try-X` events around each call and a `token-emitted` event per token. Add `// MIRRORED FROM as_tokenizer.cpp` header comment.
- [x] 5.3 Confirm drift guard test (2.1) now passes.
- [x] 5.4 Confirm event shape test (2.2) now passes.

## 6. Curated examples

- [x] 6.1 Create `LearningTrace/Examples/LearningTraceExampleRegistry.cpp` with 5–10 curated AS snippets.
- [x] 6.2 Add `GetCuratedLearningTraceExamples()` accessor returning a static `TArray<FLearningTraceExample>`.

## 7. Exporter

- [x] 7.1 Create `LearningTrace/Exporter/LearningTraceExporter.{h,cpp}` — `FLearningTraceExporter::Run(OutputDir)` orchestrates: (a) create bare `asCScriptEngine`, (b) loop examples × registered taps, (c) for each example, run all taps into one merged event stream and write `<example-id>.json`, (d) write `index.json` listing examples, (e) release engine.
- [x] 7.2 Register `FTokenizerTap` as the only tap (Phase 1 scope).

## 8. Commandlet

- [x] 8.1 Create `LearningTrace/Exporter/AngelscriptLearningTraceCommandlet.{h,cpp}` — `UAngelscriptLearningTraceCommandlet : UCommandlet`. Parses `OutputDir=` (default `Saved/LearningTrace/`); calls into `FLearningTraceExporter::Run`. Stable exit codes (0 success, 1 invalid args, 2 export failure).

## 9. Build.cs

- [x] 9.1 Add `PrivateIncludePaths` for `LearningTrace`, `LearningTrace/Core`, `LearningTrace/Phases`, `LearningTrace/Examples`, `LearningTrace/Exporter` in `AngelscriptEditor.Build.cs`. Also add `Json` and `JsonUtilities` to `PrivateDependencyModuleNames` (Runtime's PublicDependency does not propagate Json import-libs to dependents).

## 10. Verification

- [x] 10.1 `Tools\RunBuild.ps1` — exit 0; new files compile cleanly.
- [x] 10.2 `Tools\RunTests.ps1 -TestFilter "Angelscript.Editor.LearningTrace"` — 3/3 tests pass (drift guard, event shape, commandlet parsing).
- [x] 10.3 `Tools\RunCommandlet.ps1 -Commandlet AngelscriptLearningTrace` — exit 0; produced 10 `<id>.json` files plus `index.json` under `Saved/LearningTrace/`.
- [x] 10.4 Manually inspected `simple-int.json` — `events[]` shows full `try-X` decision sequence per char plus `token-emitted` per token, schema matches design.
