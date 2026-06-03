## Context

The current script pipeline discovers disk-backed `.as` files through `FAngelscriptEngine::AllRootPaths`. Each file is represented as an absolute filename plus a root-relative filename, and `FAngelscriptPreprocessor::FilenameToModuleName()` turns the relative filename into a dot-separated module name. This keeps project-local scripts simple, but it does not distinguish equal relative paths from different roots and it cannot describe memory-backed scripts, dynamically injected extension logic, or future mod packages.

The runtime already has several adjacent building blocks:

- `FAngelscriptEngineDependencies` abstracts project and plugin script-root discovery.
- `as-engine-extension-registry` allows optional extensions to attach or replay state against the active engine, and this change extends that observable contract for virtual script source providers.
- `as-preprocessor-context` moves preprocessing configuration into explicit value-style context.
- `as-compilation-events` provides structured read-only compile observability.

The virtual script path system should use those seams instead of introducing another global singleton or replacing the compiler pipeline wholesale.

## Goals / Non-Goals

**Goals:**

- Give every script source a canonical virtual identity before preprocessing and compilation.
- Allow disk-backed, memory-backed, plugin-backed, and future mod-backed sources to enter the same discovery pipeline.
- Keep project script module names compatible during the first migration.
- Make diagnostics, preprocessing summaries, compilation events, and hot reload queues able to report virtual paths while retaining physical paths when available.
- Provide a provider registration model that external runtime/editor/mod extensions can use later.

**Non-Goals:**

- Full mod lifecycle management: install, enable, disable, unload, dependencies, versioning, sandboxing, pak staging, or cooked distribution.
- Runtime security boundaries for untrusted script code.
- A general dynamic execution API for invoking arbitrary source snippets by user request.
- Replacing `/Script/Angelscript`, `/Script/AngelscriptAssets`, or Content Browser `/All/Angelscript` object paths.
- Removing the existing project `Script/` root or breaking current project script imports.

## Decisions

### 1. Introduce `as://` virtual paths as source identity

Each source will have a canonical virtual path:

```text
as://project/Gameplay/Enemy.as
as://plugin/Inventory/Gameplay/Item.as
as://memory/RuntimePatch/Injected.as
as://mod/MyMod/Gameplay/Patch.as
```

The normalized path uses forward slashes, requires a `.as` leaf, rejects `.` / `..` traversal segments, and keeps a stable mount prefix. A small value type should parse and format this identity instead of spreading string rules across engine, preprocessor, hot reload, and diagnostics code.

Mount identifiers such as plugin names, memory provider ids, and future mod ids must be normalized into safe module-name segments. If a provider id cannot be normalized without ambiguity, source enumeration should reject it before compilation instead of generating an unstable module name.

Alternative considered: expose only a prettier display string for current root-relative paths. That is smaller, but it does not solve cross-root identity or memory/mod sources, so it would be a temporary layer that later code would need to replace.

### 2. Represent inputs as source descriptors, not filename pairs

Add a source descriptor with fields similar to:

```text
VirtualPath
ModuleName
MountKind
MountName
ProviderId
Priority
RelativePath
PhysicalPath optional
SourceText optional/lazy
ContentHash or timestamp summary
```

`FFilenamePair` can remain as an adapter for existing call sites, but new discovery and preprocessing entry points should consume descriptors. Disk-backed sources keep a physical path. Memory-backed sources must be valid without one.

Alternative considered: extend `FFilenamePair` with virtual fields. That keeps signatures smaller initially, but it continues to encode "file" as the core abstraction and makes memory sources awkward.

### 3. Use source providers behind an engine-owned registry

The engine should own a virtual script source registry for its current compile context. Built-in providers cover project and plugin roots. Extensions can register additional providers through engine attach/replay paths supplied by the existing extension registry.

Provider behavior should be explicit:

- enumerate sources for the current engine/configuration;
- optionally read source text for memory-backed or virtual-only inputs;
- optionally map physical paths back to virtual paths for disk hot reload;
- report provider id, mount identity, and priority.
- unregister or stop contributing providers when the owning extension unregisters or when a new engine replaces the current engine.

The first implementation does not need a public mod API. It only reserves the `mod` mount kind and ordering model so a later mod change can plug into the provider surface.

### 4. Preserve project module-name compatibility, prefix non-project sources

Project scripts should keep the current module-name rule during this migration:

```text
as://project/Gameplay/Enemy.as -> Gameplay.Enemy
```

Non-project sources should use mount-prefixed names:

```text
as://plugin/Inventory/Gameplay/Enemy.as -> plugin.Inventory.Gameplay.Enemy
as://memory/RuntimePatch/Injected.as -> memory.RuntimePatch.Injected
as://mod/MyMod/Gameplay/Enemy.as -> mod.MyMod.Gameplay.Enemy
```

This gives plugin, memory, and future mod sources stable namespaces without breaking common project imports. If later work needs project modules to opt into `project.*` names, it should be a separate compatibility change.

### 5. Fail visibly on conflicting source identities

The registry should normalize all sources, then detect:

- duplicate virtual paths;
- duplicate module names;
- invalid virtual paths;
- providers that expose a source without physical path or source text;
- non-project sources trying to use reserved project-legacy module names.

The default behavior should be fail-closed with diagnostics rather than silently choosing whichever root was enumerated last. Priority should be recorded now, but replacement/override semantics for mods should be a later mod lifecycle decision.

### 6. Carry both virtual and physical paths through observability

`FAngelscriptPreprocessorSummary`, `FAngelscriptModuleDesc::FCodeSection`, compilation events, and compile diagnostics should have a virtual path field. Physical path remains available for disk-backed editor tooling and source navigation.

For disk-backed sources, the compiler may keep using physical section names where needed for existing editor integration, while diagnostics and event payloads map back to the virtual identity. For memory-backed sources, the virtual path becomes the only stable section/source name.

### 7. Keep disk hot reload physical, map it into virtual identity

The directory watcher should continue watching physical directories for built-in disk sources. When a file changes, the engine should map the physical filename to the matching source descriptor and queue reload work using virtual path plus module name. Memory providers do not gain automatic hot reload in the first version unless the provider later exposes explicit invalidation hooks.

## Risks / Trade-offs

- Plugin script module names may differ from the old root-relative behavior if plugin scripts are already in active use -> mitigate by documenting the compatibility boundary and adding duplicate-module diagnostics that explain the new prefixed name.
- Keeping project legacy module names means project and future mod namespaces are not perfectly symmetrical -> mitigate by limiting the exception to `project` and recording a possible later opt-in migration.
- Carrying both virtual and physical paths increases descriptor complexity -> mitigate by making the value types small, immutable after normalization, and covered by focused tests.
- Memory-backed sources can make source navigation impossible without provider support -> mitigate by surfacing virtual paths and allowing providers to expose optional source text/physical mapping rather than pretending every source is a disk file.
- Extension-provided providers can leak across engine instances if their ownership is not scoped -> mitigate by storing providers on `FAngelscriptEngine` and requiring extension replay to attach providers to the current engine only.
- Full mod override semantics are intentionally deferred -> mitigate by reserving `mod` paths and priority fields without defining replacement behavior until a mod lifecycle proposal exists.

## Migration Plan

1. Add virtual path and source descriptor value types with focused parsing/module-name tests.
2. Add an engine-owned source registry and provider interface with no behavior change when unused.
3. Implement built-in project/plugin disk providers and a memory provider test double.
4. Add adapter methods so existing `FindAllScriptFilenames` and `FAngelscriptPreprocessor::AddFile` keep working while new code uses descriptors.
5. Move initial compile discovery to enumerate descriptors and feed the preprocessor through a descriptor-aware entry point.
6. Add virtual path fields to preprocessing summaries, module code sections, diagnostics, and compilation events.
7. Map directory watcher physical file changes back to virtual source descriptors for reload.
8. Update documentation that currently describes script roots, relative filenames, and module names.

Rollback strategy: keep compatibility adapters until the new descriptor pipeline is fully verified. If virtual source enumeration causes a regression, the engine can temporarily route built-in disk sources through the existing `FFilenamePair` path while keeping the value types and tests in place.

## Open Questions

- Whether plugin scripts need an opt-in compatibility mode for legacy unprefixed module names should be decided after auditing current plugin script usage.
- The exact public API shape for future mod enable/disable, dependency ordering, and cooked distribution should be handled by a follow-up mod lifecycle proposal.
- Whether AS SDK section names should become virtual paths for all sources or remain physical for disk-backed sources should be decided during implementation based on debugger/source-navigation fallout.
