# as-script-source-provider Specification

## Purpose
TBD - created by archiving change refactor-as-script-source-provider. Update Purpose after archive.
## Requirements
### Requirement: Script discovery uses a source provider boundary
The runtime SHALL obtain Angelscript compilation inputs through a source provider boundary that returns `FAngelscriptSource` descriptors rather than relying on scattered direct file-system recursion.

#### Scenario: Project and plugin roots are surfaced as source descriptors
- **WHEN** the runtime scans the project `Script/` root and enabled plugin `Script/` roots
- **THEN** it SHALL produce source descriptors with canonical virtual paths and physical filenames where they exist
- **AND** plugin descriptors SHALL preserve their plugin mount names

#### Scenario: Existing filename discovery remains compatible
- **WHEN** existing code calls filename-based discovery entry points during migration
- **THEN** the runtime SHALL continue to return the same observable script files
- **AND** the legacy path SHALL remain a compatibility adapter rather than the primary architecture boundary

### Requirement: Source loading respects source kind
The runtime SHALL load source text through the provider only for sources that do not already carry inline source text.

#### Scenario: Disk-backed source loads through the provider
- **WHEN** a descriptor has a physical filename but no inline source text
- **THEN** the runtime SHALL obtain the source text through the provider boundary
- **AND** the resulting preprocessing metadata SHALL remain attached to the same virtual path

#### Scenario: Memory-backed source bypasses disk IO
- **WHEN** a descriptor already carries inline source text
- **THEN** the runtime SHALL use that text directly
- **AND** it SHALL NOT attempt to read the source from disk

### Requirement: Hot reload tracks canonical source identity
The runtime SHALL key hot-reload bookkeeping on canonical source identity and SHALL consult source state before queueing reload work.

#### Scenario: Relative filename collisions do not merge distinct sources
- **WHEN** two sources share the same relative filename but have different canonical virtual paths
- **THEN** the runtime SHALL keep their reload state separate

#### Scenario: Timestamp-only churn does not force reload
- **WHEN** a source state change is detected but the loaded source content hash is unchanged
- **THEN** the runtime SHALL update stored state
- **AND** it SHALL NOT queue the source for hot reload

### Requirement: Legacy queue shapes remain compatible during migration
The runtime SHALL preserve existing filename-based queue shapes and adapters while the provider-based path is adopted.

#### Scenario: Legacy hot-reload callers still compile
- **WHEN** existing code passes filename pairs into hot-reload entry points
- **THEN** the runtime SHALL continue to accept those adapters
- **AND** the compiled modules SHALL observe the same virtual-path metadata as the provider-backed path
