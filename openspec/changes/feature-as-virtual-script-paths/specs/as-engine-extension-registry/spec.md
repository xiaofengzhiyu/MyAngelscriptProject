## MODIFIED Requirements

### Requirement: Optional engine extensions can register with Angelscript

The runtime SHALL provide a registry for optional engine extensions to register and unregister themselves without changing runtime behavior when the registry is unused. The registry SHALL support registered extensions contributing virtual script source providers during engine attach or replay so their script sources participate in the engine's virtual script source enumeration.

#### Scenario: No extensions keeps runtime behavior unchanged

- **WHEN** Angelscript initializes and shuts down with no registered extensions
- **THEN** existing runtime behavior SHALL remain unchanged and no extension-specific work SHALL be performed

#### Scenario: Extension registers before an engine is active

- **WHEN** an extension registers while no engine is currently active
- **THEN** the registry SHALL remember the extension and attach it to the next active engine
- **AND** any virtual script source provider contributed during attach SHALL be visible to that engine's virtual script source enumeration

#### Scenario: Extension registers after an engine is already active

- **WHEN** an extension registers while an engine is already active
- **THEN** the registry SHALL attach that extension to the current engine without requiring a process restart
- **AND** any virtual script source provider contributed during attach SHALL be visible to that engine's virtual script source enumeration before the next compile that enumerates sources

### Requirement: Registered extensions can replay cached state to the current engine

The runtime SHALL expose an explicit replay path so an extension can rebind its cached state to the currently scoped engine, including any virtual script source providers that extension owns.

#### Scenario: Replay reattaches cached state

- **WHEN** a registered extension requests a replay against the current engine
- **THEN** the extension SHALL be able to reregister its engine-local bindings using its cached process-level state
- **AND** the extension SHALL be able to reregister its virtual script source providers for that engine

#### Scenario: Replay works after the active engine changes

- **WHEN** the current engine changes and a registered extension requests a replay
- **THEN** the extension SHALL be able to materialize its state on the new current engine without a process restart
- **AND** any virtual script source provider contributed by the extension SHALL attach to the new current engine rather than the previous engine

### Requirement: Unregistering an extension stops future lifecycle callbacks

The runtime SHALL stop delivering future engine attach and replay callbacks to an unregistered extension and SHALL ensure source providers owned by that extension are no longer contributed by future attach/replay operations.

#### Scenario: Unregistered extension is not replayed

- **WHEN** an extension is unregistered
- **AND** a new engine becomes current later
- **THEN** the registry SHALL not invoke that extension for the new engine
- **AND** virtual script source providers owned only by that unregistered extension SHALL NOT be contributed to the new engine through registry replay

## Testing Requirements

- Target test layer: Runtime integration under `Plugins/Angelscript/Source/AngelscriptTest/Core/` or `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/` because the behavior needs `FAngelscriptEngine` and extension attach/replay but does not require real `World` or `Actor` lifecycle.
- Expected Automation prefix: `Angelscript.TestModule.FileSystem.VirtualScriptPaths.ExtensionRegistry.*`
- Recommended helper/harness: existing engine test helpers, `FAngelscriptEngineScope`, current extension registry test utilities or a small test extension that contributes a memory-backed provider.
- Verification entry point: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.FileSystem.VirtualScriptPaths.ExtensionRegistry" -Label virtual-script-paths-extension-registry -TimeoutMs 600000`
