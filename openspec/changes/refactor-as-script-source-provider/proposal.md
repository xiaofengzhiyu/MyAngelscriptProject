## Why

Angelscript now has stable virtual source identity under `/Angelscript/...`, but the runtime still discovers files, reads script text, and decides reloads through scattered `IFileManager` / `FFileHelper` calls. That makes the source pipeline hard to test, keeps hot reload keyed to legacy filename state, and makes future memory-backed or generated script sources more expensive to support.

## What Changes

- Introduce a small script source provider boundary that owns discovery, source-text loading, and source-state queries for Angelscript compilation inputs.
- Keep `/Angelscript/...` virtual paths and current module-name compatibility intact; this change does not rename source identity rules.
- Route engine discovery, preprocessing source loading, and hot-reload change detection through the provider boundary instead of ad hoc file-system calls.
- Preserve existing filename-based entry points as compatibility adapters while new code consumes `FAngelscriptSource`-style descriptors.
- Improve hot-reload accuracy by keying reload state on canonical source identity and source content state instead of only legacy relative filenames and timestamps.

## Capabilities

### New Capabilities
- `as-script-source-provider`: Script discovery, source loading, compatibility adapters, and hot-reload state management for descriptor-backed Angelscript sources.

### Modified Capabilities
- None

## Impact

- Runtime source discovery and preprocessing in `Plugins/Angelscript/Source/AngelscriptRuntime/Core/`.
- Hot-reload bookkeeping and reload queue assembly in `FAngelscriptEngine`.
- Preprocessor source loading paths in `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/`.
- Runtime and editor tests that currently depend on disk fixtures or filename-only reload behavior.
