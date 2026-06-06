## Context

`Plugins/Angelscript/Source/AngelscriptTest/Learning/` accumulated three layers of work over recent OpenSpec changes:

1. **Phase 1 — Native + Runtime trace tests** (~21 files, 2026-04 era): hand-authored teaching-trace tests under `Native/` and `Runtime/`. Pattern is "exercise an AS scenario, capture trace events, assert against a golden text." Heavy maintenance burden; teaching value low because the assertions are noisy and not student-friendly.
2. **Phase 2 — Raw-trace exporter** (`feature-as-runtime-visualizer-raw-trace`, archived 2026-06-05): added `Export/` with a 1786-line exporter and a `UCommandlet` that produces JSON consumed by `Experiment/ASRuntimeVisualizer/`. Six fixed scenarios.
3. **Phase 3 — Teaching snapshot workbench** (`feature-as-teaching-snapshot-workbench`, archived 2026-06-05): added scoped runtime instrumentation (`AngelscriptTeachingTrace.{h,cpp}`), `ClassGenerator.cpp` probe points, the `RuntimeClassMemberGeneration` exporter scenario, and an Emscripten/ImGui workbench scaffold (in `Experiment/`).

The owner reviewed Phases 1–3 and concluded the C++ approach does not produce satisfying learning artifacts. The future direction is a web-based publishing pipeline that authors learning content directly (likely Markdown/wiki + curated static JSON), rather than generating it from a fixed C++ scenario set. Continuing to maintain the C++ tooling while building the web pipeline would create a permanent "second source of truth" risk.

## Goals / Non-Goals

**Goals:**

- Remove all C++ learning-trace tooling cleanly (no orphaned headers, no dangling includes, no broken build).
- Preserve the rest of `AngelscriptTest/` untouched — Actor, Bindings, Blueprint, Component, Debugger, Delegate, GC, HotReload, Inheritance, Interface, Networking, Preprocessor, StaticJIT, Subsystem, etc., themes are unaffected.
- Record the removal in OpenSpec so the archive history reflects the deliberate de-scoping rather than silent decay.

**Non-Goals:**

- Designing or building the web-based learning content pipeline (separate future work; the user has flagged this as the next direction but specifics are TBD).
- Touching `Experiment/ASRuntimeVisualizer/` — the static page can survive as a frozen demo. Whether it is eventually deleted is a follow-up decision tracked outside this change.
- Updating `Documents/Guides/LearningTrace.md` or other doc references in this same change — bundled into a follow-up doc sweep to keep this code-only diff reviewable.
- Renaming, splitting, or refactoring any non-Learning Test module code.

## Decisions

- **Delete, don't archive.** The Learning code is uncommitted in the working tree; instead of committing-and-then-removing, we delete uncommitted files outright and `git rm` tracked siblings. The git history of `feature-as-runtime-visualizer-raw-trace` and `feature-as-teaching-snapshot-workbench` lives in the *parent* repo's `openspec/archive/` so the design intent is preserved even though the implementation never landed.
- **Three REMOVED specs, not one merged delta.** Each removed capability gets its own delta file under `specs/<capability>/spec.md`, mirroring the original capability granularity. This makes the archive history symmetric with the original additions and keeps each REMOVED record self-contained.
- **Revert `ClassGenerator.cpp` rather than delete sections manually.** The workbench probe additions are localised but interleaved with multiple methods (Analyze, CreateFullReloadClass, AddClassProperties, DoFullReloadClass). `git checkout -- <file>` against HEAD is safer and atomic.
- **Keep `Experiment/ASRuntimeVisualizer/`.** The frozen demo costs nothing on disk and may be useful as a reference for how the web pipeline could surface similar data. If the user decides to delete it, that's a one-line `rm -rf` change that doesn't need spec ceremony.
- **Don't pre-emptively touch `AngelscriptTestEngineHelper.h`'s `CompileModuleWithSummary` etc.** Those Test-side helpers were on the prior plan's "promote to Runtime" list — but with the exporter gone they have no Editor consumer; they go back to being plain Test-only utilities. Zero edits needed to them.

## Risks / Trade-offs

- **Risk:** A non-Learning test or runtime call site might transitively include `AngelscriptLearningTrace.h` or `AngelscriptTeachingTrace.h`.  
  **Mitigation:** A grep across the codebase before commit confirms the only consumers are the files being deleted. Verification step runs `Tools\RunBuild.ps1` to catch any missed reference.
- **Risk:** Future student/contributor finds the archived `feature-as-*` records and wonders why the implementation isn't there.  
  **Mitigation:** This change's archived record explains the deliberate removal; a single `grep` from `as-learning-trace-export` to its REMOVED record will surface the explanation.
- **Trade-off:** Loses real-data export for `Experiment/ASRuntimeVisualizer/`. The frozen `raw-trace-data.js` snapshot is an artifact-without-a-regenerator. Acceptable because the visualizer was already framed as an experiment, not a maintained tool, and the future web pipeline supersedes it.

## Migration Plan

1. Read the current state of the workbench probe diff in `ClassGenerator.cpp` so the revert is intentional, not accidental.
2. Delete uncommitted files in the submodule working tree (`Learning/` whole, `AngelscriptTeachingTrace.{h,cpp}`, `Shared/AngelscriptLearningTrace*`).
3. `git checkout -- AngelscriptClassGenerator.cpp` to revert tracked file to HEAD.
4. Update `AngelscriptTest/Shared/README.md` to drop the orphan reference.
5. Verify build + remaining tests.
6. Commit submodule, then parent (with new gitlink + new OpenSpec change directory).
7. Archive the change.

## Open Questions

- Should `Experiment/ASRuntimeVisualizer/` be kept as a frozen demo, deleted, or get a README note saying its data source is decommissioned? Default action in this change: do nothing (keep frozen). Surface to user before commit.
- Should `Documents/Guides/LearningTrace.md` be updated to point at the new web pipeline (once it exists), or deleted now? Deferred to a follow-up doc-sweep change.
