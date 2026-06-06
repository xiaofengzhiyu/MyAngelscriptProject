## Context

`Experiment/ASRuntimeVisualizer` currently renders scenarios from `scenarios.js` and `deep-scenarios.js`. Those scenarios already model the desired page shape, but their data is manually curated. Existing learning tests expose most data needed for a real teaching trace: `asCTokenizer`, `asCParser`, `asCScriptNode`, `CompileModuleWithSummary`, `FindGeneratedClass`, `GetByteCode`, and `SetInstructionCallback`. `AngelscriptLearningTraceExport` now writes JSON with real tokens, AST nodes, compile events, bytecode functions, engine snapshots, VM events, diagnostics, and results.

## Goals / Non-Goals

**Goals:**

- Keep the page directly openable from disk.
- Auto-load generated Raw Trace data from a local `raw-trace-data.js` file.
- Keep file import as an optional replacement/debug path for another `as-learning-raw-trace.json`.
- Prioritize generated or imported real trace scenarios over hand-authored examples.
- Use exporter-provided teaching links for precise source hover and timeline behavior.
- Export Raw Trace JSON for token, AST, compile, bytecode, engine state, and VM execution phases.
- Keep the exporter deterministic and runnable from automation tests or `Tools\RunCommandlet.ps1`.

**Non-Goals:**

- Do not add npm, a bundler, or a persistent frontend server.
- Do not remove existing demo scenarios.
- Do not expand the commandlet scenario set beyond the current v1 examples.
- Do not yet build the general "input arbitrary AS code and export every learning phase" framework.
- Do not add per-parser-method or per-compiler-method instrumentation hooks.
- Do not vendor BMPrivateAccess unless implementation hits a hard blocker that cannot be solved by existing internal headers.

## Decisions

- Own the commandlet/exporter in `AngelscriptTest/Learning/Export` so teaching export tooling stays out of the reusable runtime module.
- Write Raw Trace JSON first: stable schema fields are exported from real phase objects, and visualizer hints are optional metadata layered beside the raw arrays.
- Collect tokenizer, parser, compiler, bytecode, engine, and VM data from real objects after each phase, not from intrusive hooks inside compiler control flow.
- Reuse the existing `StartAngelscriptHeaders.h` private-header pattern for `asCContext`, `asCScriptFunction`, `asCParser`, and `asCScriptNode`.
- Use a browser `FileReader` import instead of `fetch()` so `index.html` keeps working under `file://`.
- Generate `raw-trace-data.js` beside the page so the default path requires no manual import and no dev server.
- Read imported files as bytes and decode BOM/UTF-16/UTF-8 in the adapter because UE commandlet output is currently UTF-16LE JSON.
- Add an optional `visualizer` object to each exported scenario rather than changing existing raw arrays or bumping the root schema.
- Keep adapter logic in a separate `trace-adapter.js` file so `app.js` remains responsible for UI state and rendering.
- Use a two-column Workbench layout: source and timeline on the left, related inspectors on the right.
- Treat C++ exporter scenarios as the source of truth for static teaching cases; website changes only display imported data, and any future case additions must update the exporter first.
- Record the longer-term direction as a multi-pass trace framework: a user-provided AS snippet can be executed/exported multiple times across tokenizer, parser, compile, bytecode, engine, and VM phases, then merged into one teaching trace. If required data is not externally observable, later changes may expose or instrument AS engine internals.
- Default the right-side Workbench to a focused panel mode: it shows only panels related to the current timeline phase plus the notes/source-reference panel. The toolbar keeps an all-panel fallback for debugging complete trace shape.
- Export observable Engine snapshots for every generated scenario before adding private-field instrumentation. Native SDK scenarios record module/function table state plus prepared/completed context snapshots; runtime scenarios record compile summary state, and UClass scenarios also record generated class metadata.
- Add `RuntimeUFunctionExecution` as the first real UE UFunction execution teaching case. It exports one AS source snippet but runs it twice: first through `UObject::ProcessEvent` / `UASFunction::RuntimeCallEvent` with reflected params to prove the UE bridge path, then against the same `UASFunction::ScriptFunction` with a direct AS context and instruction callback to capture `vmEvents`.
- Keep `RuntimeUFunctionExecution` non-invasive for v1. It reads public/generated runtime state (`UASFunction`, reflected properties, bytecode, context results) and does not add a private runtime hook or patch AngelScript engine internals yet.

## Risks / Trade-offs

- Imported JSON can be missing hints or arrays -> the adapter will degrade to generated timeline steps and the UI will show empty states.
- Snapshot-only traces cannot show every internal parser/compiler branch. Mitigation: include phase names, source ranges, AST nodes, compile summary, bytecode, engine snapshots, and VM evidence.
- Bytecode/source mapping is not fully recoverable from raw bytecode alone -> exporter hints are required for precise teaching links.
- C++ and website can drift if cases are updated in only one side -> tests validate exported hints and adapter behavior from the same Raw Trace contract.
- The visualizer is experimental -> keep changes scoped under `Experiment/ASRuntimeVisualizer` and avoid introducing repo-wide frontend tooling.
- Observable Engine snapshots do not yet expose private AS engine internals such as full compiler work queues or VM locals. Missing fields should be recorded as future instrumentation needs instead of faking values in the page.
- The direct VM trace is a second execution on a separate UObject instance, not the same invocation as `ProcessEvent`. This keeps the bridge proof and instruction trace both real while avoiding a runtime hook in the bridge-owned context.

## Future Notes

- General snippet tracing will likely need a new commandlet mode such as `SourceFile=` or `SnippetFile=` and a merge layer that can combine several phase-specific exports for the same code.
- Some relationships, especially exact bytecode-to-source and VM-to-local-variable state, may require AS engine instrumentation rather than external observation.
