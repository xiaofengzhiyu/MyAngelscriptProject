## ADDED Requirements

### Requirement: Script sources have canonical virtual paths

The runtime SHALL assign every script source a canonical virtual path before preprocessing or compilation begins.

#### Scenario: Project script source receives project virtual path

- **WHEN** the engine discovers a disk-backed script under the project `Script/` root
- **THEN** the source SHALL have a virtual path under `as://project/`
- **AND** the virtual path SHALL contain the normalized path relative to the project script root
- **AND** the source SHALL retain its physical path for editor tooling

#### Scenario: Plugin script source receives plugin virtual path

- **WHEN** the engine discovers a disk-backed script under an enabled plugin `Script/` root
- **THEN** the source SHALL have a virtual path under `as://plugin/<PluginName>/`
- **AND** the virtual path SHALL contain the normalized path relative to that plugin script root
- **AND** the source SHALL retain its physical path for editor tooling

#### Scenario: Memory source receives memory virtual path

- **WHEN** a registered source provider exposes an in-memory script source without a physical file
- **THEN** the source SHALL have a virtual path under `as://memory/<ProviderId>/`
- **AND** the source SHALL be valid for preprocessing and compilation without a physical path

#### Scenario: Invalid virtual path is rejected

- **WHEN** a source provider exposes a virtual path with a missing mount, missing `.as` leaf, backtracking segment, empty path segment, or unsupported scheme
- **THEN** the runtime SHALL reject that source before compilation
- **AND** the diagnostic SHALL identify the provider and invalid virtual path

### Requirement: Virtual paths map to stable module names

The runtime SHALL derive module names from canonical virtual paths using deterministic rules that preserve project-script compatibility and namespace non-project sources.

#### Scenario: Project module name remains compatible

- **WHEN** a project source has virtual path `as://project/Gameplay/Enemy.as`
- **THEN** its default module name SHALL be `Gameplay.Enemy`
- **AND** existing project imports that refer to `Gameplay.Enemy` SHALL continue to resolve

#### Scenario: Mount identifiers are normalized for module names

- **WHEN** a virtual path contains a plugin, memory provider, or mod identifier
- **THEN** the runtime SHALL normalize that identifier into a valid module-name segment before deriving the module name
- **AND** the runtime SHALL reject the source if the identifier cannot be normalized without ambiguity

#### Scenario: Plugin module name is namespaced

- **WHEN** a plugin source has virtual path `as://plugin/Inventory/Gameplay/Enemy.as`
- **THEN** its default module name SHALL be `plugin.Inventory.Gameplay.Enemy`

#### Scenario: Memory module name is namespaced

- **WHEN** a memory source has virtual path `as://memory/RuntimePatch/Injected.as`
- **THEN** its default module name SHALL be `memory.RuntimePatch.Injected`

#### Scenario: Mod namespace is reserved

- **WHEN** a future mod provider exposes a source with virtual path `as://mod/MyMod/Gameplay/Patch.as`
- **THEN** its default module name SHALL be `mod.MyMod.Gameplay.Patch`
- **AND** the runtime SHALL treat the `mod` mount kind as reserved even before full mod lifecycle support exists

### Requirement: Source providers feed script discovery

The runtime SHALL discover scripts through source providers so built-in disk roots and extension-provided sources use one source enumeration model.

#### Scenario: Built-in providers preserve existing disk discovery

- **WHEN** no external source providers are registered
- **THEN** project and enabled-plugin disk scripts SHALL still be discovered from the same physical roots used by the current script-root discovery behavior
- **AND** existing project-only discovery behavior SHALL remain available for compatibility paths that explicitly request only the project script root

#### Scenario: External provider contributes source

- **WHEN** an engine extension registers a virtual script source provider before compilation
- **THEN** the provider's valid sources SHALL be included in the engine's source enumeration
- **AND** the provider SHALL NOT need to modify `FAngelscriptEngine::DiscoverScriptRoots()` directly

#### Scenario: No providers preserves runtime behavior

- **WHEN** only built-in providers are active and all discovered sources are project scripts
- **THEN** preprocessing and compilation SHALL behave equivalently to the existing project-script flow except for the additional virtual path metadata

### Requirement: Conflicting source identities fail visibly

The runtime SHALL detect conflicting virtual script identities before compilation and SHALL NOT silently choose between ambiguous sources.

#### Scenario: Duplicate virtual path is rejected

- **WHEN** two providers expose sources with the same canonical virtual path
- **THEN** source enumeration SHALL fail before preprocessing
- **AND** the diagnostic SHALL identify both providers and the duplicate virtual path

#### Scenario: Duplicate module name is rejected

- **WHEN** two distinct virtual sources resolve to the same module name
- **THEN** source enumeration SHALL fail before preprocessing
- **AND** the diagnostic SHALL identify both virtual paths and the duplicate module name

#### Scenario: Override priority does not imply replacement

- **WHEN** two sources differ only by provider priority but conflict on virtual path or module name
- **THEN** the runtime SHALL still report a conflict
- **AND** it SHALL NOT replace one source with the other until a later mod lifecycle capability defines override semantics

### Requirement: Compilation observability includes virtual paths

The runtime SHALL expose virtual path identity in user-visible compile observability while retaining physical path data when it exists.

#### Scenario: Preprocessor summary includes virtual path

- **WHEN** preprocessing summarizes a source file
- **THEN** the summary SHALL include the source virtual path
- **AND** disk-backed sources SHALL also report their physical path

#### Scenario: Compilation event includes virtual path

- **WHEN** a compilation event describes a source file, code section, module, diagnostic, or reload input
- **THEN** the event payload SHALL include the relevant virtual path when one is known
- **AND** the event payload SHALL remain read-only and value-style

#### Scenario: Compile diagnostic reports virtual path

- **WHEN** compilation emits an error, warning, or info diagnostic for a virtual script source
- **THEN** the diagnostic payload SHALL include the virtual path
- **AND** disk-backed diagnostics SHALL retain enough physical path information for editor source navigation when available

### Requirement: Hot reload maps physical changes to virtual sources

The runtime SHALL keep disk-backed hot reload driven by physical file changes while mapping those changes to virtual source identities before module reload decisions are made.

#### Scenario: Disk project file change maps to virtual path

- **WHEN** the directory watcher reports a change to a physical project script file
- **THEN** the reload queue SHALL identify the matching `as://project/` source
- **AND** the reload decision SHALL use the source's module name and virtual path

#### Scenario: Disk plugin file change maps to virtual path

- **WHEN** the directory watcher reports a change to a physical plugin script file
- **THEN** the reload queue SHALL identify the matching `as://plugin/<PluginName>/` source
- **AND** the reload decision SHALL use the source's module name and virtual path

#### Scenario: Memory source has no implicit disk hot reload

- **WHEN** a memory-backed source has no physical path
- **THEN** disk directory watcher changes SHALL NOT implicitly reload that source
- **AND** any memory-source invalidation behavior SHALL require an explicit provider-facing mechanism outside this requirement

## Testing Requirements

- Target test layer: Runtime integration under `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/` and `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/` for source identity, source discovery, preprocessing, module lookup, and diagnostics; Editor tests under `Plugins/Angelscript/Source/AngelscriptEditor/Tests/` for directory watcher and hot-reload physical-to-virtual mapping.
- Expected Automation prefix: `Angelscript.TestModule.FileSystem.VirtualScriptPaths.*` for source discovery, conflict, and module-name coverage; `Angelscript.TestModule.Preprocessor.VirtualScriptPaths.*` for preprocessing summary and diagnostic identity coverage; `Angelscript.Editor.VirtualScriptPaths.*` for editor directory watcher and hot-reload mapping coverage.
- Recommended helper/harness: existing `AngelscriptTestEngineHelper.*`, `FAngelscriptEngineScope`, file-system test helpers near `AngelscriptFileSystemTests.cpp`, script-root discovery dependency injection patterns near `AngelscriptScriptRootDiscoveryTests.cpp`, preprocessor tests near existing `Preprocessor` themed coverage, and directory watcher helpers near `AngelscriptDirectoryWatcherRootResolutionTests.cpp`.
- Verification entry points:
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.FileSystem.VirtualScriptPaths" -Label virtual-script-paths-filesystem -TimeoutMs 600000`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Preprocessor.VirtualScriptPaths" -Label virtual-script-paths-preprocessor -TimeoutMs 600000`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.Editor.VirtualScriptPaths" -Label virtual-script-paths-editor -TimeoutMs 600000`
