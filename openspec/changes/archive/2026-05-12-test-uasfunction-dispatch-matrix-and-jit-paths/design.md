## Context

`UASFunction::AllocateFunctionFor` selects specialized subclasses based on thread safety, static-ness, argument shape, return shape, reference behavior, and whether a final script function has complete JIT entries. Existing tests exercise many call results but only lightly assert this selection matrix.

StaticJIT AOT tests currently prove generated code can be compiled into the test module, registered, attached to an AngelScript function, and executed through `asIScriptContext::Execute()`. That does not prove `UASFunction_*_JIT` wrappers or the `JitFunction_ParmsEntry` path are used.

## Approach

Use two focused layers rather than one large exhaustive test:

1. `ClassGenerator` ASFunction tests cover wrapper allocation and non-AOT runtime invocation behavior.
2. `StaticJIT.AOT` tests cover generated JIT attachment and execution through UASFunction-accessible functions.

For allocation, test representative wrapper classes instead of every possible C++ branch duplicate:

- no params / void
- byte, bool, dword, qword, float, double, float-extended-to-double arguments
- reference argument
- byte, dword, float, double, float-extended-to-double, object returns
- generic fallback for multi-argument or unsupported shape
- thread-safe and static generic boundaries
- final/JIT-capable vs virtual/non-final boundaries where AOT fixtures make this observable

For JIT execution, tests must show both pointer availability and execution evidence:

- `jitFunction`, `jitFunction_Raw`, and `jitFunction_ParmsEntry` are non-null for the generated fixture function when expected.
- a test-visible generated-code counter increments when the function is invoked.
- returned values and reference/object side effects match the non-JIT/context semantics.

## Risks

- Full BPVM coverage for every wrapper shape would be large and slow. Limit BPVM additions to high-risk cases and rely on ProcessEvent/RuntimeCallEvent matrix for broad ABI coverage.
- StaticJIT AOT fixtures depend on generated source plus a local cache. Tests should fail with clear setup instructions if prerequisites are missing rather than silently falling back to non-JIT execution.
- Thread-safe JIT wrappers currently do not necessarily take the same fast JIT branch as non-thread-safe wrappers. Tests should document current behavior before changing semantics.

## Implementation Findings

This change took longer than a normal test-matrix addition because the first failures were not simple missing assertions. The ASFunction wrapper tests exposed that the StaticJIT AOT fixture path had to prove the generated JIT entry was actually attached and executed through `UASFunction`, not merely that `asIScriptContext::Execute()` could run a precompiled function.

The largest issue was generated-output verification. The AOT code path could run correctly, but `GeneratedOutputVerify` still reported stale `.jit.hpp` output because several generated reference values were process-local and changed between runs. These values included old reference arguments in `FJitRef_Function`, `FJitRef_SystemFunctionPointer`, `FJitRef_Type`, `FJitRef_GlobalVar`, and layout verification helpers such as `FJitVerifyPropertyOffset` / `FJitVerifyTypeSize`.

One real root cause was global-reference symbol instability. `ReferenceGlobalVariable` reused existing global references, but the existing-reference branch did not always write the stable generated name back to `OutName`. In that case the generator could fall back to address-shaped names such as `GREF_<hex>`, so identical source could produce different text after a fresh process or repeated engine load.

The fix has two layers:

- Runtime generation now keeps global-reference names stable by filling `OutName` when an existing global reference is reused.
- Test comparison only normalizes unavoidable old-reference parameters, while still strictly comparing generated function bodies, generated symbols, offset/size expressions, and AOT cache semantics.

Repeated-load and multi-engine behavior also mattered. `FStaticJITCompiledInfo` and related JIT reference structs previously carried transient pointer state that could survive in ways that made a second load observe stale data. The implementation now keeps immutable reference identifiers and resolves runtime pointers during finalization, while resetting per-load transient state before applying precompiled data again.

`StaticJITAotFixture.Cache` remains intentionally untracked. The test flow now treats the cache as a local generation artifact and uses defensive checks plus compiled generated source to prove the AOT path is available. This avoids committing machine-local binary cache state while still failing clearly when the generated fixture has not been prepared.
