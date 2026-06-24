## MODIFIED Requirements

### Requirement: Script sources have canonical AS virtual paths

The runtime SHALL assign supported script sources a canonical `/Angelscript/...` virtual path before preprocessing or compilation.

#### Scenario: Project disk source receives game virtual path

- **WHEN** a script is discovered under the project `Script/` root
- **THEN** its virtual path SHALL be `/Angelscript/Game/<RelativePath>.as`
- **AND** its physical filename SHALL remain available for editor tooling

#### Scenario: Plugin disk source receives plugin virtual path

- **WHEN** a script is discovered under an enabled plugin `Script/` root
- **THEN** its virtual path SHALL be `/Angelscript/Plugin/<PluginName>/<RelativePath>.as`
- **AND** its physical filename SHALL remain available for editor tooling

#### Scenario: Memory source receives memory virtual path

- **WHEN** a memory-backed source is passed to the preprocessor
- **THEN** it SHALL be valid without a physical filename
- **AND** its virtual path SHALL be under `/Angelscript/Memory/<Provider>/`

#### Scenario: Immediate memory source path hosts realtime snippets

- **WHEN** realtime snippets create memory-backed sources
- **THEN** their source identity SHALL be under `/Angelscript/Memory/Immediate/`
- **AND** the source SHALL NOT require a physical filename

#### Scenario: Invalid virtual path is rejected

- **WHEN** a source path has a non-`/Angelscript` root, URI scheme, missing `.as` leaf, empty segment, `.` segment, `..` segment, or backslash
- **THEN** virtual path parsing SHALL fail before the source is compiled
