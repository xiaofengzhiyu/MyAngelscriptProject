## Why

Angelscript currently treats script identity as a physical root plus root-relative filename, then derives module names from that relative filename. That model works for project-local scripts, but it does not provide a stable namespace for plugin scripts, memory-backed sources, dynamic extension logic, or future mod packages.

This change introduces a virtual script path layer so every script source has a stable, user-visible identity before preprocessing, compilation, diagnostics, hot reload, and future mod mounting build on top of it.

## What Changes

- Add a virtual script source model that represents script inputs by `as://` virtual paths, source provider identity, optional physical path, relative path, priority, and derived module name.
- Add built-in virtual mounts for project scripts, plugin scripts, memory-backed sources, and reserved mod paths.
- Preserve project-script compatibility by keeping existing project module names available during the initial migration.
- Route script discovery through source providers while keeping existing physical `Script/` root discovery as a compatibility provider.
- Allow registered engine extensions to contribute virtual script source providers through the existing extension attach/replay lifecycle.
- Surface virtual paths in compile diagnostics, preprocessing summaries, compilation events, and hot reload mapping without removing physical paths needed by editor tooling.
- Reserve mod namespace and ordering semantics for a later mod lifecycle change; this proposal does not implement full mod install, unload, sandboxing, pak staging, or dependency resolution.

## Capabilities

### New Capabilities

- `as-virtual-script-paths`: Defines stable virtual script paths, source provider behavior, source conflict handling, diagnostics identity, and compatibility rules for project, plugin, memory, and future mod script sources.

### Modified Capabilities

- `as-engine-extension-registry`: Extends registered engine extension behavior so extensions can attach, replay, and unregister virtual script source providers for the current engine.

## Impact

- Runtime script discovery and preprocessing: `FAngelscriptEngine::DiscoverScriptRoots`, `FindAllScriptFilenames`, `FAngelscriptPreprocessor::AddFile`, and module-name derivation need to consume virtual source descriptors rather than only `FFilenamePair`.
- Runtime extension APIs: the existing engine extension registry becomes the preferred integration point for external script source providers.
- Compilation observability: preprocessing summaries, compilation events, diagnostics, and debug/source navigation paths need to carry virtual path identity while retaining physical paths when available.
- Hot reload and editor integration: directory watcher input remains physical for disk-backed sources, but reload queues and user-facing diagnostics need virtual path mapping.
- Tests and documentation: add runtime integration coverage under the file-system/preprocessor themes, update docs that describe script roots and module naming, and keep validation on the standard build/test runners.
