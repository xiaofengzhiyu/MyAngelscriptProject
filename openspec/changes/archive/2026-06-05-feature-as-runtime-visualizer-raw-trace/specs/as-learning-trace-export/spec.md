## ADDED Requirements

### Requirement: Raw trace export commandlet
The system SHALL provide an `AngelscriptLearningTraceExport` commandlet that writes Raw Trace JSON for AS learning scenarios.

#### Scenario: Default commandlet export
- **WHEN** the commandlet runs without explicit output arguments
- **THEN** it writes `as-learning-raw-trace.json` under the default learning trace export directory

#### Scenario: Custom output path
- **WHEN** the commandlet receives `OutputDir=` and `OutputFile=` arguments
- **THEN** it writes the JSON file to the requested location

### Requirement: Raw trace schema
The exported JSON SHALL include schema metadata and per-scenario arrays for tokens, AST nodes, compile events, bytecode functions, engine snapshots, VM events, and diagnostics.

#### Scenario: JSON schema round trip
- **WHEN** the exported JSON is parsed after a commandlet or helper run
- **THEN** it contains `schemaVersion`, `generator`, `generatedAtUtc`, and a non-empty `scenarios` array

### Requirement: Full learning phase coverage
The exporter SHALL cover tokenization, parsing/AST, compilation, bytecode inspection, engine state snapshots, and VM execution for fixed v1 teaching scenarios.

#### Scenario: Native VM scenario
- **WHEN** the `NativeDoubleAfterIncrement` scenario is exported
- **THEN** the scenario contains non-empty token, AST, bytecode, and VM event arrays and records a return value of `42`

#### Scenario: UE class generation scenario
- **WHEN** the `RuntimeUClassGeneration` scenario is exported
- **THEN** the scenario contains compile summary data and an engine snapshot describing the generated class, properties, and functions

#### Scenario: UE UFunction VM execution scenario
- **WHEN** the `RuntimeUFunctionExecution` scenario is exported
- **THEN** the scenario contains ProcessEvent bridge results, backing ScriptFunction bytecode, runtime context snapshots, VM events, and bridge/VM return values of `42`

### Requirement: Scenario filtering
The commandlet SHALL support exporting either all v1 scenarios or one named scenario.

#### Scenario: Single scenario export
- **WHEN** the commandlet receives `Scenario=NativeNestedCall`
- **THEN** the exported JSON contains exactly that scenario

#### Scenario: Invalid scenario
- **WHEN** the commandlet receives an unknown `Scenario=` value
- **THEN** the commandlet returns a non-zero invalid-arguments exit code
