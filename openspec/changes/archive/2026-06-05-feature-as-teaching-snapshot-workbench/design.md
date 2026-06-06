## Context

The existing learning export produces static JSON for native AngelScript compilation, runtime compile diagnostics, UClass generation, and UFunction execution. It already carries tokens, AST nodes, compile events, engine snapshots, VM events, result metadata, and visualizer hints, but class generation remains too coarse for teaching member-level state changes.

The desired direction is a pure Dear ImGui workbench served by static HTTP. Wiki integration is expected later, so the exported trace needs stable IDs and wiki references instead of UI-specific state.

## Goals / Non-Goals

**Goals:**
- Record class/member generation events only when a scoped recorder is active.
- Export a class/member scenario that explains UCLASS, UPROPERTY, UFUNCTION, defaults, and generated reflection metadata.
- Use stable entity IDs such as `class:ALearningMemberTraceActor`, `property:ALearningMemberTraceActor.Health`, and `function:ALearningMemberTraceActor.GetHealthValue`.
- Keep the UI data contract static JSON so the same artifact can feed the current visualizer, an ImGui WASM workbench, or later wiki pages.

**Non-Goals:**
- Replacing the existing JavaScript raw trace visualizer.
- Building a complete wiki shell in this change.
- Making runtime teaching trace always-on or adding persistent runtime storage.
- Requiring Emscripten to be installed on every development machine before the exporter can be validated.

## Decisions

- Scoped recorder over global recording: the runtime exposes a small active-recorder stack and class generator probes short-circuit when no recorder is active. This keeps normal runtime compilation free of teaching trace allocation.
- JSON contract over UI-specific state: `classGenerationTrace` contains `events`, `snapshots`, `diffs`, `entities`, and `wikiRefs`, allowing different visualizers to derive their own presentation.
- Member-focused scenario over generic runtime class scenario: `RuntimeClassMemberGeneration` uses one Actor with two properties, defaults, and one reflected function so the timeline can show a complete but compact state transition.
- Static HTTP ImGui scaffold over `file://` delivery: Emscripten output and fetched JSON behave like the future docs/wiki host and avoid local file security edge cases.

## Risks / Trade-offs

- [Risk] Class generator probes could add overhead to normal compiles -> Mitigation: every emit path checks for an active scoped recorder before constructing event payloads.
- [Risk] Stable entity IDs could drift if class names change -> Mitigation: tests assert the exported class/property/function IDs for the teaching scenario.
- [Risk] ImGui WASM requires Emscripten and Ninja on the local machine -> Mitigation: keep the static scaffold under `Experiment/`, document `emsdk`/Dear ImGui setup, and verify C++/JSON behavior independently from the browser artifact.
- [Risk] The trace may still be too detailed for teaching -> Mitigation: keep raw `events` plus curated `snapshots` and `diffs` so the UI can choose summary or deep views.
