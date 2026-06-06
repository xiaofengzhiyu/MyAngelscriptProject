## REMOVED Requirements

### Requirement: Raw trace export commandlet
**Reason:** The C++ exporter and `AngelscriptLearningTraceExport` commandlet have been deleted along with the rest of `AngelscriptTest/Learning/`. The owner determined the C++ teaching artifacts were unsatisfactory; future learning content will be authored through a web-based pipeline (TBD, separate effort).
**Migration:** None — the commandlet is gone. Consumers of `Saved/Automation/AngelscriptLearningTraceExport/*.json` should treat the existing files as frozen and stop scheduling regenerations. The static snapshot embedded in `Experiment/ASRuntimeVisualizer/raw-trace-data.js` remains usable as an archival demo.

### Requirement: Raw trace schema
**Reason:** No code emits the schema anymore. The `schemaVersion: 1`, `generator`, `generatedAtUtc`, and `scenarios[]` shape are no longer maintained.
**Migration:** Any future export contract will be defined by the upcoming web-based pipeline; do not assume continuity of the v1 schema.

### Requirement: Full learning phase coverage
**Reason:** The exporter scenarios (tokenizer, parser, compile, bytecode, engine snapshots, VM execution) have been deleted as part of removing `AngelscriptTest/Learning/Export/`.
**Migration:** None — equivalent teaching coverage will be planned and re-introduced (or replaced) by the future web pipeline.

### Requirement: Scenario filtering
**Reason:** No commandlet exists to filter scenarios on; the surrounding API surface is gone.
**Migration:** None.
