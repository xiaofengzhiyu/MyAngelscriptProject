## 1. OpenSpec

- [x] 1.1 Record proposal, design, spec, and task checklist

## 2. Tests First

- [x] 2.1 Add exporter tests for visualizer hints and reference validity
- [x] 2.2 Add frontend adapter tests for Raw Trace conversion and invalid input
- [x] 2.3 Run focused tests to observe expected failures

## 3. Exporter Hints

- [x] 3.1 Add `visualizer.sourceRefs` and `visualizer.timeline` JSON helpers
- [x] 3.2 Add teaching timeline hints for native scenarios
- [x] 3.3 Add teaching timeline hints for runtime class and diagnostics scenarios

## 4. Frontend Adapter

- [x] 4.1 Add `trace-adapter.js` Raw Trace conversion module
- [x] 4.2 Add file import state and imported scenario priority
- [x] 4.3 Add invalid import and missing-hint empty states

## 5. Workbench UI

- [x] 5.1 Update HTML controls for import, source selector, filters, and inspector tabs
- [x] 5.2 Update rendering so source hover drives inspector details
- [x] 5.3 Update styles and README for Raw Trace first workflow
- [x] 5.4 Generate and auto-load embedded Raw Trace data

## 6. Verification

- [x] 6.1 Pass frontend adapter tests
- [x] 6.2 Pass `Angelscript.TestModule.Learning.Export`
- [x] 6.3 Pass commandlet smoke and inspect imported JSON shape
- [x] 6.4 Run focused build verification

## 7. Engine Snapshots And Layout Focus

- [x] 7.1 Add observable engine snapshots for every generated scenario
- [x] 7.2 Add engine-linked teaching timeline steps for native and runtime scenarios
- [x] 7.3 Add focused analysis panel mode with all-panel fallback
- [x] 7.4 Re-run focused build, tests, commandlet, and embedded data generation

## 8. UE UFunction VM Execution

- [x] 8.1 Add RED coverage for `RuntimeUFunctionExecution` exporter and embedded page data
- [x] 8.2 Add commandlet scenario parsing and All export inclusion
- [x] 8.3 Export ProcessEvent bridge results plus direct backing ScriptFunction VM trace
- [x] 8.4 Add engine snapshots and teaching timeline links for bridge/context/VM phases
- [x] 8.5 Regenerate embedded Raw Trace data and rerun focused verification

## 9. Learning Trace Export Commandlet

- [x] 9.1 Add `AngelscriptLearningTraceExport` commandlet and reusable exporter helpers
- [x] 9.2 Export token stream, AST, compile summary, bytecode, engine snapshots, VM events, diagnostics, and results
- [x] 9.3 Implement `Scenario=`, `OutputDir=`, `OutputFile=`, and `Detail=` parsing with stable exit codes
- [x] 9.4 Cover exporter schema, scenario contents, commandlet parsing, JSON writing, and diagnostics
