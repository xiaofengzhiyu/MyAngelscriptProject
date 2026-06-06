## REMOVED Requirements

### Requirement: Scoped Teaching Trace Recording
**Reason:** The scoped recorder (`FAngelscriptTeachingTrace`, `IAngelscriptTeachingTraceSink`, `FAngelscriptTeachingTraceScope`, `FAngelscriptTeachingTraceRecorder`) and the class/member generation probes inside `AngelscriptClassGenerator.cpp` have been deleted along with the rest of the C++ learning tooling.
**Migration:** None — the runtime instrumentation is gone. Future teaching workflows will not depend on this recorder.

### Requirement: Class Member Generation Export
**Reason:** The `RuntimeClassMemberGeneration` scenario lived inside the deleted exporter; the `classGenerationTrace` payload is no longer produced anywhere.
**Migration:** None.

### Requirement: Static Workbench Contract
**Reason:** With no exporter and no maintained JSON contract, the static workbench has no source data to consume. The Emscripten/ImGui workbench scaffold under `Experiment/` is left untouched but unmaintained for now; whether it is also removed is a separate follow-up decision.
**Migration:** None.
