## Why

`as.DumpEngineState` is documented and used as a developer-facing diagnostics entry point, but its console command is currently registered from the `AngelscriptTest` module. That makes the command depend on a test module being loaded even though the dump implementation lives in `AngelscriptRuntime`.

## What Changes

- Move the formal `as.DumpEngineState` console command registration into `AngelscriptRuntime`.
- Keep `AngelscriptTest` focused on automation coverage for the command and dump output.
- Preserve the existing command name, argument handling, and `FAngelscriptStateDump::DumpAll()` behavior.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- None. This is a module-boundary refactor of an existing diagnostic command, not a user-visible command contract change.

## Impact

- `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/`
- `Plugins/Angelscript/Source/AngelscriptTest/Dump/`
- Dump automation tests for command registration.
