## Current State

The StaticJIT tests currently validate surrounding pieces: generated source text, archive/precompiled support, helper exception mapping, debug callstack RAII, primitive conversions, and direct native bridge helpers. They do not compile generated StaticJIT C++ into `AngelscriptTest`, attach the compiled entry points to AngelScript functions, or prove that `Context->Execute()` reaches a generated AOT function instead of the VM fallback.

Generated StaticJIT source registers functions through static `FStaticJITFunction` instances and `FJITDatabase`. Editor builds define `AS_SKIP_JITTED_CODE`, so a test AOT path must explicitly make generated code visible in the test module when that is safe. `FStaticJITCompiledInfo` also assumes a single active compiled-info instance per process, so the first test closure should diagnose multi-engine/global-state problems rather than attempting a broad runtime state refactor.

## Goals

- Provide a repeatable test workflow that generates StaticJIT AOT source from committed test fixtures.
- Keep generated AOT files under `AngelscriptTest` source so UBT discovers them naturally on the next build.
- Add runtime automation tests that prove the generated C++ is registered, attached as `jitFunction`, and executed through normal AngelScript calls.
- Add multi-engine diagnostics that make current StaticJIT global-state assumptions visible.
- Keep production behavior compatible and avoid a full StaticJIT state ownership rewrite in this change.

## Non-Goals

- Do not convert all StaticJIT globals to per-engine ownership in this change.
- Do not replace the existing source-text/unit tests; the AOT tests complement them.
- Do not introduce ad hoc UBT commands or bypass the project build/test runners.
- Do not manually maintain generated `.cpp` file lists in module build rules.

## Technical Approach

1. Add a small fixture set under `AngelscriptTest/StaticJIT/AOT/Fixtures` that compiles deterministically and contains at least one function suitable for proving AOT execution.
2. Add an `AngelscriptTest` commandlet with generate and verify modes. Generate mode writes the checked-in AOT C++ artifacts and a local ignored precompiled cache. Verify mode regenerates into a temporary location and compares against checked-in source plus the local cache to catch stale or incomplete setup.
3. Place generated `.jit.cpp` / `.jit.hpp` artifacts under `AngelscriptTest/StaticJIT/AOT/Generated`. After a rebuild, UBT should compile those `.cpp` files automatically because they live inside the module source tree. Keep `StaticJITAotFixture.Cache` as a local generated prerequisite rather than a committed binary fixture.
4. Add a narrow test-support marker for AOT execution. Prefer a test-only function or counter that generated C++ can call when it actually runs, rather than inferring execution from source text.
5. Add automation tests under `Angelscript.TestModule.StaticJIT.AOT.*` that verify compiled-info registration, non-null `jitFunction`, and execution through `Context->Execute()`.
6. Add focused `Angelscript.TestModule.StaticJIT.AOT.MultiEngine.*` diagnostics that exercise two engines or two compiled-info lifetimes enough to surface singleton/global-state conflicts.

## Tradeoffs

- Checking in generated C++ creates an update workflow, but it is the only practical way to make UBT compile StaticJIT output during normal tests. The matching precompiled cache remains a local generated artifact to avoid committing binary cache data.
- A test-only execution marker is more explicit than relying on timing or source inspection, but it must remain isolated from production runtime behavior.
- Multi-engine coverage is intentionally diagnostic. A full engine-owned StaticJIT state refactor is larger and should follow only if the AOT tests show concrete failures.

## Risks

- `AS_SKIP_JITTED_CODE` can silently skip generated registration in editor builds unless the test module opt-in is explicit and validated.
- Static initialization order can make generated registration brittle if test artifacts depend on runtime state too early.
- `FStaticJITCompiledInfo` currently enforces one active compiled info per process; tests must avoid compiling multiple generated info packages simultaneously unless the purpose is a controlled diagnostic.
