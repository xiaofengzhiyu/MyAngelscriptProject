## ADDED Requirements

### Requirement: Raw Trace file import
The visualizer SHALL allow users to load `AngelscriptLearningTraceExport` Raw Trace JSON from a browser file input while keeping the page directly openable from disk.

#### Scenario: Import commandlet JSON
- **WHEN** a user selects a valid Raw Trace JSON file
- **THEN** the visualizer lists imported trace scenarios ahead of demo scenarios

#### Scenario: Reject invalid JSON
- **WHEN** a user selects a file that is not parseable Raw Trace JSON
- **THEN** the visualizer shows a non-crashing import error message

### Requirement: Raw Trace adapter
The visualizer SHALL adapt Raw Trace scenarios into the existing visualizer scenario model with source, tokens, AST nodes, compile events, bytecode instructions, engine snapshots, VM events, diagnostics, result, and timeline data.

#### Scenario: Adapter preserves real stage data
- **WHEN** a Raw Trace scenario contains token, AST, bytecode, engine, VM, and diagnostics arrays
- **THEN** the adapted scenario exposes those arrays to the relevant panels without replacing them with hand-authored demo data

#### Scenario: Adapter uses teaching hints
- **WHEN** a Raw Trace scenario contains `visualizer.timeline`
- **THEN** source hover and timeline playback use those hint references to highlight related data

### Requirement: Workbench interaction
The visualizer SHALL provide a Raw Trace first Workbench layout where source hover, source click, timeline playback, and inspector tabs show related low-level AS state.

#### Scenario: Hover source token
- **WHEN** the user hovers or focuses a source token
- **THEN** the inspector shows related token, AST, bytecode, engine, VM, diagnostics, and result data where available

#### Scenario: Play timeline
- **WHEN** the user plays or steps through the timeline
- **THEN** the source view and inspector panels update to the timeline step's referenced data

### Requirement: Exported visualizer hints
The Raw Trace exporter SHALL emit optional visualizer teaching hints for v1 learning scenarios without removing existing raw trace fields.

#### Scenario: Hints reference real exported data
- **WHEN** a v1 learning scenario is exported
- **THEN** every visualizer timeline reference points to a token, AST node, bytecode instruction, engine snapshot, VM event, diagnostic, or result field that exists in the same scenario

### Requirement: C++ owned teaching cases
The C++ learning trace exporter SHALL be the source of truth for Raw Trace teaching cases consumed by the website.

#### Scenario: Add a teaching case
- **WHEN** a new Raw Trace teaching case is needed
- **THEN** the case is added to the C++ exporter output before the website displays it

### Requirement: Future snippet tracing direction
The design record SHALL preserve the future goal of tracing a user-provided AS snippet through multiple export passes before merging the data into one teaching trace.

#### Scenario: Missing phase information
- **WHEN** a needed learning detail cannot be observed from current public/internal exporter access
- **THEN** the gap is recorded for a later engine instrumentation or AS engine exposure change rather than guessed in the website
