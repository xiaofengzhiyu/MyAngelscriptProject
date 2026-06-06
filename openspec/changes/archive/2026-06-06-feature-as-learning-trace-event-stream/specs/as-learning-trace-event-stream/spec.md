## ADDED Requirements

### Requirement: Phase-tap framework

The system SHALL provide a `LearningTrace/` subsystem under `AngelscriptEditor/` that defines a phase-tap interface, an event-stream container, and an example metadata struct, so that future phase taps (Parser, Builder, ClassGen, VM) can be added by writing only new files under `Phases/` and registering them with the exporter.

#### Scenario: Phase-tap interface is implementable in isolation

- **WHEN** a new phase tap is added (e.g. `FParserTap`)
- **THEN** the new tap implements `ILearningTracePhaseTap`, lives in its own file under `LearningTrace/Phases/`, and registers with the exporter without modifying `Core/` or existing taps

#### Scenario: Event stream supports time-ordered append

- **WHEN** a phase tap emits multiple events for one example
- **THEN** the events appear in `events[]` in the order they were emitted, with monotonically increasing `seq` numbers starting at 0

### Requirement: Tokenizer phase tap

The system SHALL provide an `FTokenizerTap` that subclasses `asCTokenizer`, reuses its protected `IsWhiteSpace` / `IsComment` / `IsConstant` / `IsIdentifier` / `IsKeyWord` helpers without copying their bodies, and emits per-decision events as the source is scanned.

#### Scenario: Decision events emitted for each token

- **WHEN** `FTokenizerTap` runs on a non-empty AS source
- **THEN** for each emitted token the event stream contains, in order, at least one `try-X` event for each helper that was called and exactly one `token-emitted` event with the resolved token's `tokenType`, `class`, `offset`, `length`, and `text`

#### Scenario: Final tokens match unmodified asCTokenizer

- **WHEN** `FTokenizerTap` and unmodified `asCTokenizer` are both run on the same AS source from the curated example registry
- **THEN** the resulting final token sequences are identical (same `tokenType`, `tokenClass`, `offset`, `length` for every token)

### Requirement: Curated example registry

The system SHALL ship a curated set of 5–10 AS snippets covering the interesting tokenizer decision branches (whitespace, comments, numeric constants in multiple radices, string constants, keywords vs identifiers, edge cases).

#### Scenario: Registry exposes static example list

- **WHEN** the exporter requests the curated example list
- **THEN** the registry returns a non-empty `TArray<FLearningTraceExample>` where each entry has a non-empty `id`, `title`, `focus`, and `source`

### Requirement: JSON output

The system SHALL write one JSON file per curated example to a configurable output directory, plus an `index.json` listing all examples.

#### Scenario: Per-example JSON file

- **WHEN** the exporter runs successfully on the curated set
- **THEN** the output directory contains one `<example-id>.json` file per example, each with a top-level `{ schemaVersion, generator, generatedAtUtc, example, events[] }` shape

#### Scenario: Index file lists examples

- **WHEN** the exporter runs successfully
- **THEN** the output directory contains an `index.json` whose `examples[]` array lists every example's `id`, `title`, and relative JSON filename

### Requirement: Commandlet entry point

The system SHALL provide `UAngelscriptLearningTraceCommandlet` runnable via `Tools\RunCommandlet.ps1` that drives the exporter end-to-end with stable exit codes.

#### Scenario: Default commandlet run

- **WHEN** the commandlet is invoked without explicit arguments
- **THEN** it writes JSON output under `Saved/LearningTrace/` and exits with code 0

#### Scenario: Custom output directory

- **WHEN** the commandlet receives `OutputDir=` argument
- **THEN** it writes to the requested directory and exits with code 0

#### Scenario: Invalid arguments produce non-zero exit

- **WHEN** the commandlet receives an unrecognized argument format
- **THEN** it logs an error and exits with a non-zero code
