## Why

The current raw trace export can show compilation, bytecode, VM events, and coarse runtime snapshots, but it does not teach how script class members become Unreal-facing UClass, UProperty, UFunction, and default object state. The next visualizer needs a stable teaching data contract that can later be embedded into wiki pages without coupling the runtime to a specific UI shell.

## What Changes

- Add a scoped runtime teaching trace recorder that is inactive by default and can collect class/member generation events only inside an explicit recording scope.
- Instrument the runtime class/member generation path with stable phase names and entity identifiers for class, property, function, argument, return type, finalize, and default-object steps.
- Extend the learning trace export with a `RuntimeClassMemberGeneration` scenario containing `classGenerationTrace` data: events, snapshots, diffs, entities, and wiki references.
- Add visualizer hints for class/member teaching steps so the existing static raw trace viewer and the future ImGui workbench can render the same JSON contract.
- Scaffold a static HTTP Dear ImGui/Emscripten workbench experiment for loading the exported JSON and presenting timeline, source, class/member tree, inspector, and raw JSON panes.

## Capabilities

### New Capabilities
- `as-teaching-snapshot-workbench`: Scoped class/member generation teaching trace export and static visualizer workbench contract.

### Modified Capabilities

## Impact

- Runtime module: low-overhead optional teaching trace interfaces and class generator probes.
- Test module: learning trace exporter scenario, commandlet scenario parsing, and automation coverage.
- Experiment directory: ignored static ImGui workbench scaffold and existing static visualizer adapter support.
- Documentation/OpenSpec: records the data contract and verification expectations for future wiki integration.
