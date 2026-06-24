## Context

The virtual path architecture separates disk project sources (`/Angelscript/Game/...`), plugin sources (`/Angelscript/Plugin/...`), and memory-backed sources (`/Angelscript/Memory/<Provider>/...`). Before this change, `/Angelscript/Memory/Immediate/` was reserved for future realtime snippets but had no execution API. The snippet runner needs to reuse the normal preprocessor/compiler path so diagnostics, module identity, bindings, and editor behavior remain consistent with regular AS code.

## Goals / Non-Goals

**Goals:**
- Execute command-style AS statements at runtime/editor time without creating physical source files.
- Support full-source snippets for callers that need explicit declarations and `void Main()`.
- Preserve virtual path diagnostics and memory-source module isolation.
- Keep the editor UI thin and route all execution through one runtime API.

**Non-Goals:**
- Typed return values or parameter binding beyond `void Main()`.
- Persistent snippet asset management.
- Shipping-build runtime code injection.
- Replacing normal disk-backed project or plugin script compilation.

## Decisions

- Use `/Angelscript/Memory/Immediate/` as the provider path because the backing type is memory and the lifetime is immediate. A separate `/Snippet` root would mix use case with source-kind identity.
- Compile snippets through `FAngelscriptPreprocessor::AddSource()` and `FAngelscriptEngine::CompileModules()` instead of raw `asIScriptModule::CompileFunction()`. This preserves include processing, diagnostics, module registration, and bindings.
- Use a zero-argument `void` entry point. Statement mode wraps caller text in a generated unique entry point so retained snippets cannot collide on a global `Main`; full-source mode requires the caller to provide `void Main()`.
- Discard snippet modules by default after execution, with an explicit keep-for-debugging option. Discarding also removes the module's script globals/types from AngelScript engine availability so later snippets can reuse full-source global names safely.
- Make the editor window a thin Slate wrapper over `FAngelscriptSnippetRunner`. The console command and editor UI therefore share the same virtual path and diagnostic behavior.

## Risks / Trade-offs

- Snippet execution can run arbitrary editor/runtime AS code -> disabled in shipping and exposed as an explicit editor/console tool rather than automatic background execution.
- Diagnostics from wrapped statement mode have generated wrapper lines -> result diagnostics include user-facing row mapping.
- Repeated snippets can create many module names if retained -> modules are discarded by default and only kept when the caller opts in for debugging. Statement snippets also use generated entry point names to keep retained modules isolated.
- Editor UI coverage cannot exercise live Slate interaction in headless NullRHI -> menu registration and action dispatch are tested, while runtime execution semantics are covered by core tests.
