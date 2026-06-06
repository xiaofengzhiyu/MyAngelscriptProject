## Why

The AS runtime visualizer is still driven by hand-authored teaching data. It needs one coherent feature path that first exports real Raw Trace JSON from tokenizer, parser, compiler, bytecode, engine, and VM stages, then consumes that trace directly so the teaching animation reflects real execution data.

## What Changes

- Add an `AngelscriptLearningTraceExport` commandlet in the `AngelscriptTest` module.
- Add a reusable learning trace exporter that emits Raw Trace JSON, including token stream, AST tree, compile summary, bytecode, engine snapshots, VM instruction events, diagnostics, and results for fixed teaching scenarios.
- Add visualizer-oriented teaching hints to the Raw Trace exporter without replacing the existing raw arrays.
- Update `Experiment/ASRuntimeVisualizer` into a Raw Trace first workbench with file import, source hover, timeline playback, and inspector tabs.
- Keep hand-authored scenarios as fallback examples, but make imported commandlet traces the primary workflow.
- Add exporter, commandlet, and frontend adapter tests for the trace-to-visualizer contract.
- Do not add compiler/parser instrumentation hooks or vendor BMPrivateAccess in v1; export snapshot data from observable states and existing internal headers.

## Capabilities

### New Capabilities

- `as-learning-trace-export`: Export real AngelScript learning traces as Raw Trace JSON from automation tests and commandlet runs.
- `as-runtime-visualizer-raw-trace`: Consume commandlet Raw Trace JSON in the experimental AS runtime visualizer and expose precise code-to-stage teaching links.

### Modified Capabilities

- None.

## Impact

- Affects `Plugins/Angelscript/Source/AngelscriptTest/Learning/Export/`.
- Affects `Experiment/ASRuntimeVisualizer/`.
- Adds JSON output under `Saved/Automation/AngelscriptLearningTraceExport` by default.
- Adds a commandlet runnable through `Tools\RunCommandlet.ps1 -Commandlet AngelscriptLearningTraceExport`.
- Adds no frontend package manager, bundler, or dev-server dependency.
