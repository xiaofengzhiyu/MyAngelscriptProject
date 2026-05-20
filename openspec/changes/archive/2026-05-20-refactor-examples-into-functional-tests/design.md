## Context

`Plugins/Angelscript/Source/AngelscriptTest/Examples/` contains 21 `AngelscriptScriptExample*Test.cpp` files plus `AngelscriptScriptExampleTestSupport.*`. Most files compile inline snippets through `RunScriptExampleCompileTest()` and register under `Angelscript.TestModule.ScriptExamples.*`. The layer originally made sense while example scripts were being restored, but `Script/Examples/**` now owns the example asset surface and the test module already has richer functional themes.

The main exception is `AngelscriptScriptExampleCoverageTests.cpp`, which performs runtime assertions for actor defaults, component lifecycle, UObject methods, and property metadata. Several compile-only examples also point at behaviors that may still be worth retaining, such as timers, construction script, overlaps, delegates, widget binding, and actor movement. Those should become functional behavior tests instead of staying as example smoke tests.

Current documentation and automation still expose `ScriptExamples.*` as an active layer through `Config/DefaultEngine.ini`, `Documents/Guides/Test.md`, `Documents/Guides/TestConventions.md`, `Documents/Guides/TestCatalog.md`, and technical-debt notes.

## Goals / Non-Goals

**Goals:**

- Retire the `Examples/` test directory and `Angelscript.TestModule.ScriptExamples.*` automation prefix.
- Preserve useful Examples-derived behavior through focused functional assertions in the existing test hierarchy.
- Classify each existing Examples test before deletion so coverage loss is intentional and reviewable.
- Prefer existing `Functional/<Theme>/` directories and established helpers over a new broad replacement layer.
- Remove stale automation group and documentation references to Examples as a current test layer.

**Non-Goals:**

- This change will not remove, rewrite, or reorganize `Script/Examples/**`.
- This change will not migrate every example one-to-one.
- This change will not create a new `Functional/Examples` catch-all directory.
- This change will not revive legacy `Documents/Plans/**` as the active planning system.
- This change will not attempt a broad CQTest conversion outside the behavior needed to retire Examples.

## Decisions

### Decision: Classify before deleting

Implementation starts with an inventory table for every file in `Plugins/Angelscript/Source/AngelscriptTest/Examples/`. Each file is marked as one of:

- `Absorb`: behavior remains valuable and needs functional coverage before deletion.
- `DeleteAsCovered`: compile-only or duplicate content already covered by `Script/Examples`, runtime integration, bindings, syntax, or functional tests.
- `DeleteAfterDependencyMove`: helper or shared inline source needed only until a dependent Examples test is absorbed.

Rationale: the purpose is to remove a low-value layer, not silently shrink coverage. The classification table gives reviewers a concrete artifact to challenge.

Alternative considered: delete all compile-only Examples immediately. That is faster but risky because some compile-only examples encode missing behavior intent that should be turned into assertions first.

### Decision: Absorb into functional themes, not a replacement Examples layer

Examples-derived runtime behavior should land where the behavior belongs: actor lifecycle, component lifecycle, delegate invocation, widget binding, property metadata, UObject/object model, function behavior, or specific `Functional/<Theme>/` directories. New directories are allowed only when the behavior has a real theme that does not already exist.

Rationale: `Examples/` was organized by documentation samples, while the mature test suite is organized by behavior and theme. Keeping a new examples-shaped folder would preserve the same ownership ambiguity.

Alternative considered: move `Examples/` files into `Functional/Examples/`. That would make the directory name nicer but would not solve the duplicate layer problem.

### Decision: Keep compile-only examples out of the functional suite unless they expose a gap

Pure syntax or API compilation examples, such as simple arrays, maps, structs, enums, access specifiers, format strings, mixin methods, or basic functions, should be deleted when existing syntax, bindings, or runtime integration tests already cover the behavior. If a compile-only example reveals a missing assertion, the implementation should add the assertion to the correct theme instead of preserving the original snippet wholesale.

Rationale: the user goal is to supplement functional tests. A pile of migrated compile-only tests would keep maintenance cost without improving behavior confidence.

### Decision: Treat `AngelscriptScriptExampleCoverageTests.cpp` as the main migration source

The coverage tests contain the strongest behavior signal and should be split into functional destinations rather than deleted as examples. Their file-backed legacy coverage assets under `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/` may be used as temporary source material during migration, but the final tests should not depend on legacy plan directories unless the implementation explicitly documents why that dependency remains valuable.

Rationale: tests should not keep active coverage anchored to deprecated plan assets when the behavior can be expressed in current functional fixtures.

### Decision: Retire public test entry points in the same change

After functional coverage is in place, the implementation removes the `AngelscriptExamples` automation group and updates current test docs so users run functional/theme prefixes instead.

Rationale: leaving group and documentation aliases behind would make the removed layer appear supported.

## Risks / Trade-offs

- **Coverage loss from over-deletion** -> Require a per-file classification table and targeted functional tests before removing `Absorb` files.
- **Functional suite bloat** -> Add assertions only where they cover user-observable UE behavior or a known gap; delete compile-only examples that duplicate established coverage.
- **Misplaced tests** -> Prefer existing theme directories and helpers; add a new functional theme only with a clear behavior owner.
- **Stale docs or config** -> Include `rg` verification for `ScriptExamples`, `AngelscriptExamples`, and `AngelscriptScriptExampleTestSupport`.
- **Legacy plan asset dependency remains hidden** -> Either inline/relocate the useful fixture script into the functional test style or explicitly verify and document any retained legacy file dependency.

## Migration Plan

1. Inventory current Examples tests and classify each as `Absorb`, `DeleteAsCovered`, or `DeleteAfterDependencyMove`.
2. Add or extend functional tests for every `Absorb` item using the existing theme helpers.
3. Remove the corresponding Examples tests after the functional tests pass.
4. Delete `AngelscriptScriptExampleTestSupport.*` and the empty `Examples/` directory.
5. Remove the `AngelscriptExamples` automation group and update current test docs.
6. Run targeted functional prefixes, the functional suite, a standard build, and strict OpenSpec validation.

Rollback is straightforward before archive: restore the deleted Examples files and `AngelscriptExamples` group from git, then keep any newly useful functional tests as additive coverage.

## Open Questions

- Should the implementation retain any file-backed coverage fixture from `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/`, or should those snippets be re-expressed directly in functional test sources?
- Should timer examples land under `Functional/Actor/` because they need world ticking, or under a new `Functional/System/` theme because the API being validated is `System::SetTimer`?
- Should behavior-tree and character-input examples be absorbed in this pass only if existing harnesses make them cheap, or classified as covered/deferred if they would require substantial new infrastructure?
