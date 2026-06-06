## ADDED Requirements

### Requirement: Scoped Teaching Trace Recording
The runtime SHALL expose a scoped class/member teaching trace recorder that collects events only while an explicit scope is active.

#### Scenario: Recorder inactive by default
- **WHEN** a script module is compiled without an active teaching trace scope
- **THEN** no class/member teaching events are collected

#### Scenario: Recorder active during class generation
- **WHEN** a script module containing reflected class members is compiled inside an active teaching trace scope
- **THEN** the recorder captures class, property, function, finalize, and default-object events with stable phase names

### Requirement: Class Member Generation Export
The learning trace exporter SHALL provide a `RuntimeClassMemberGeneration` scenario with a `classGenerationTrace` object containing events, snapshots, diffs, entities, and wiki references.

#### Scenario: Export includes member entities
- **WHEN** the `RuntimeClassMemberGeneration` scenario is exported
- **THEN** the JSON includes class, property, and function entities for `ALearningMemberTraceActor`, `Health`, `bStartsEnabled`, and `GetHealthValue`

#### Scenario: Export includes teaching diffs
- **WHEN** the `RuntimeClassMemberGeneration` scenario is exported
- **THEN** the JSON includes diffs describing generated property, function, and default value additions

### Requirement: Static Workbench Contract
The visualizer workbench SHALL consume the exported static JSON without requiring a live Unreal Editor process.

#### Scenario: Static HTTP visualizer loads raw trace
- **WHEN** the exported raw trace JSON is served over static HTTP
- **THEN** the workbench can render a timeline, source view, class/member tree, inspector, and raw JSON pane from `classGenerationTrace`
