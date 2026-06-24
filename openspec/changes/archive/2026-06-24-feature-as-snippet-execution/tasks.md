## 1. Runtime Execution

- [x] 1.1 Add failing runtime tests for statement mode, full-source mode, diagnostics, exceptions, module retention, empty input, and missing entry point.
- [x] 1.2 Implement `FAngelscriptSnippetRunner` using `/Angelscript/Memory/Immediate/` memory sources, generated statement entry points, and full-source `void Main()`.
- [x] 1.3 Map compile diagnostics and exceptions back to snippet virtual paths and user rows.
- [x] 1.4 Cover repeated snippet execution for default discard, kept modules, full-source `void Main()`, and underlying module discard global cleanup.

## 2. Invocation Surfaces

- [x] 2.1 Add failing console tests for `as.Snippet.ExecuteFile` registration, success, and read failure.
- [x] 2.2 Implement `as.Snippet.ExecuteFile <path>` on top of `FAngelscriptSnippetRunner`.
- [x] 2.3 Add failing editor menu test for the Tools -> Programming snippet entry.
- [x] 2.4 Implement the editor menu entry and Slate snippet runner window.

## 3. Documentation And Verification

- [x] 3.1 Record proposal, design, and spec deltas for snippet execution and Immediate memory paths.
- [x] 3.2 Update the test catalog with the new snippet automation coverage.
- [x] 3.3 Run build, focused runtime tests, console tests, editor menu tests, and discard cleanup regression tests.
