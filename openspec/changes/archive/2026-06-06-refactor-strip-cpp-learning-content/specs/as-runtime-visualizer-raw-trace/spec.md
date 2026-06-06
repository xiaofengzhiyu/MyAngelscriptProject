## REMOVED Requirements

### Requirement: Raw Trace file import
**Reason:** With the C++ exporter deleted, there is no canonical Raw Trace JSON producer. `Experiment/ASRuntimeVisualizer/` may keep a frozen sample `raw-trace-data.js` for archival reference, but the round-trip "import a freshly-generated commandlet JSON file" workflow is no longer a maintained capability.
**Migration:** None — the visualizer experiment is decoupled from any active export contract. Treat any imported JSON as one-off rather than the start of a maintained feed.

### Requirement: Raw Trace adapter
**Reason:** The adapter file `Experiment/ASRuntimeVisualizer/trace-adapter.js` may persist physically, but it no longer represents a contract between an upstream exporter and the page; it is frozen sample-driving code.
**Migration:** None.

### Requirement: Workbench interaction
**Reason:** Same as above — the page may still load a static `raw-trace-data.js` snapshot, but its source hover / timeline / inspector behavior is no longer a tracked capability with maintained source data.
**Migration:** None.

### Requirement: Exported visualizer hints
**Reason:** The exporter no longer exists; nothing emits `visualizer.timeline` hints.
**Migration:** None.

### Requirement: C++ owned teaching cases
**Reason:** Explicitly inverted by this change. The C++ exporter is no longer the source of truth — it has been deleted.
**Migration:** Future teaching cases will be authored in the upcoming web pipeline (TBD). Do not add new teaching cases to deleted C++ tooling.

### Requirement: Future snippet tracing direction
**Reason:** The deferred multi-pass snippet-tracing roadmap captured by this requirement is abandoned along with the rest of the C++ tooling.
**Migration:** None.
