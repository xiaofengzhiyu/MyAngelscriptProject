## Context

The runtime already has a stable source identity model: `FAngelscriptVirtualPath` and `FAngelscriptSource` describe project, plugin, and memory-backed scripts in canonical `/Angelscript/...` form. The remaining gap is the access layer underneath that model. Today, source discovery still recurses through `IFileManager`, source loading still happens inside the preprocessor for disk-backed inputs, and hot-reload state still leans on legacy relative filenames.

This change is about narrowing that gap without reopening source identity decisions. The goal is to give Angelscript one explicit place to discover sources, load text, and query source state, so the runtime can treat disk, memory, and future generated sources consistently.

## Goals / Non-Goals

**Goals:**
- Centralize source discovery, source-text loading, and source-state queries behind a small provider boundary.
- Keep existing virtual-path rules, module-name compatibility, and `FAngelscriptSource` behavior intact.
- Make script discovery and reload behavior easier to test without writing ad hoc disk fixtures.
- Reduce reliance on legacy relative-path keys in hot-reload bookkeeping.

**Non-Goals:**
- Do not redesign `/Angelscript/...` virtual path naming.
- Do not add a full general-purpose virtual file system layer.
- Do not rewrite async loading behavior as part of this change.
- Do not add a runtime snippet execution API; snippet support stays a consumer of the new source pipeline.

## Decisions

1. **Use a source provider abstraction instead of a generic VFS.**
   The runtime already thinks in terms of `FAngelscriptSource`, not raw file handles. A provider that returns sources and source state matches the compilation pipeline better than a broad file-system facade. A full VFS would be heavier, would duplicate UE file APIs, and would obscure the actual unit we need to test.

2. **Keep `FAngelscriptSource` as the unit of exchange.**
   Discovery should produce source descriptors, preprocessing should consume them, and hot reload should reason about the same descriptors. This keeps disk-backed and memory-backed sources on one path and avoids splitting the system into “files” versus “sources”.

3. **Provide a disk-backed default implementation, not a new storage model.**
   The default provider should wrap the existing UE file APIs and current engine dependencies so production behavior stays stable. This change is about ownership of the boundary, not a new persistence layer.

4. **Preserve legacy filename entry points as adapters.**
   `FindAllScriptFilenames()` and existing filename-based hot-reload queues should continue to work during migration, but they should stop being the primary architectural boundary. This reduces risk and keeps existing tests and tools working while the provider becomes the preferred path.

5. **Key reload state by canonical source identity and compare content state when needed.**
   Relative filenames are not strong enough once the runtime has multiple source kinds and multiple roots. Virtual path identity is the stable key; timestamps can remain as a quick signal, but content state should be consulted before queuing unnecessary reload work.

## Risks / Trade-offs

- [Risk] The provider adds one more abstraction layer. → Mitigation: keep the interface narrow and align it with the existing `FAngelscriptSource` model.
- [Risk] Content-state checks can add extra file reads during reload scans. → Mitigation: only perform deeper checks after a state change signal, not on every source every time.
- [Risk] Partial migration could leave engine, preprocessor, and hot reload using mixed source-key semantics for a while. → Mitigation: keep compatibility adapters in place until the new boundary covers the full pipeline.
- [Risk] Tests may still rely on disk fixtures if the provider contract is too coupled to UE file APIs. → Mitigation: add a fake provider path in tests so discovery and reload logic can run without real disk writes.

## Migration Plan

1. Introduce the provider contract and a disk-backed default implementation.
2. Route discovery through the provider while keeping filename adapters alive.
3. Route preprocessor loading through the provider for disk-backed sources and keep memory sources inline.
4. Rework hot-reload bookkeeping to use canonical source identity and content-state checks.
5. Add focused regression tests around discovery, compatibility adapters, and false-positive reload suppression.

## Open Questions

None for v1. The scope is intentionally limited to a source-provider boundary and hot-reload state cleanup.
