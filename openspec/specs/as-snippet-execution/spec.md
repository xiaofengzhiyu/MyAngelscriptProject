# as-snippet-execution Specification

## Purpose
TBD - created by archiving change feature-as-snippet-execution. Update Purpose after archive.
## Requirements
### Requirement: Snippets execute through Immediate memory virtual paths

The runtime SHALL provide a non-shipping snippet execution API that compiles and executes memory-backed Angelscript source under `/Angelscript/Memory/Immediate/`.

#### Scenario: Statement snippet executes through generated entry point

- **WHEN** a caller executes a statement-mode snippet
- **THEN** the runtime SHALL wrap the source in a generated zero-argument `void` entry point
- **AND** assign a virtual path under `/Angelscript/Memory/Immediate/`
- **AND** execute the generated entry point

#### Scenario: Full-source snippet executes explicit Main

- **WHEN** a caller executes a full-source snippet containing `void Main()`
- **THEN** the runtime SHALL compile the source as provided
- **AND** execute the explicit `void Main()` entry point

#### Scenario: Empty snippet is rejected

- **WHEN** a caller executes an empty or whitespace-only snippet
- **THEN** the runtime SHALL reject the request before preprocessing or compilation

### Requirement: Snippet results expose structured diagnostics

The runtime SHALL return structured snippet results containing success state, result code, virtual path, module name, compile diagnostics, and execution exception details.

#### Scenario: Compile diagnostic reports virtual path

- **WHEN** snippet compilation fails
- **THEN** the result SHALL include diagnostics associated with the snippet virtual path
- **AND** statement-mode diagnostics SHALL include user-facing line numbers adjusted for the generated wrapper

#### Scenario: Execution exception reports exception details

- **WHEN** the snippet entry point throws an Angelscript exception
- **THEN** the result SHALL report an execution-exception result code
- **AND** include exception text, source section, and user-facing line number

### Requirement: Snippet module lifetime is isolated by default

Snippet execution SHALL use isolated memory-source module names and SHALL discard compiled snippet modules by default after execution.

#### Scenario: Successful snippet discards module by default

- **WHEN** a snippet executes successfully with default lifetime options
- **THEN** the runtime SHALL discard the compiled snippet module after execution

#### Scenario: Caller keeps module for debugging

- **WHEN** a caller requests the snippet module be kept for debugging
- **THEN** the runtime SHALL leave the compiled snippet module active after execution

#### Scenario: Repeated snippets remain isolated

- **WHEN** a caller executes repeated snippets with the same label
- **THEN** each snippet SHALL use an isolated module name
- **AND** statement-mode snippets SHALL use isolated generated entry point declarations
- **AND** repeated default-lifetime and kept-for-debugging snippets SHALL execute without duplicate global-function diagnostics

### Requirement: Snippets are available through console and editor surfaces

The plugin SHALL expose user-facing editor/developer invocation surfaces that route through the runtime snippet execution API.

#### Scenario: Console command executes snippet file

- **WHEN** the user runs `as.Snippet.ExecuteFile <path>`
- **THEN** the command SHALL load the file as a statement-mode snippet
- **AND** report success or failure through the console output device

#### Scenario: Editor menu opens snippet runner

- **WHEN** the editor Tools -> Programming menu is registered
- **THEN** it SHALL include a `Run Angelscript Snippet` entry
- **AND** activating that entry SHALL open the snippet runner window
