## Why

Runtime/editor work often needs to execute a small Angelscript command without creating a disk-backed `.as` file. The virtual path work already introduced `/Angelscript/Memory/Immediate/`; this change turns that reserved identity into a tested snippet execution path while keeping memory-backed sources isolated from project and plugin files.

## What Changes

- Add a runtime `FAngelscriptSnippetRunner` API that executes Angelscript source text as either wrapped statements or full source with `void Main()`.
- Assign every snippet a `/Angelscript/Memory/Immediate/Snippet_<id>[_Label].as` virtual path and isolated `Angelscript.Memory.Immediate.*` module name.
- Return structured success, result-code, virtual-path, module-name, compile diagnostic, and exception information to callers.
- Add `as.Snippet.ExecuteFile <path>` for command-line/editor-console execution from a file.
- Add an editor Tools -> Programming menu entry and Slate window for interactive snippet execution.
- Add runtime, console, and editor automation coverage for success, diagnostics, exceptions, module lifetime, and menu registration.

## Capabilities

### New Capabilities

- `as-snippet-execution`: Runtime, console, and editor support for executing memory-backed Angelscript snippets through virtual paths.

### Modified Capabilities

- `as-virtual-script-paths`: `/Angelscript/Memory/Immediate/` is no longer only reserved; it is the concrete provider path used by snippet execution.

## Impact

- Runtime API: `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSnippet.*`
- Editor UI/menu: `Plugins/Angelscript/Source/AngelscriptEditor/Core/AngelscriptEditorModule.*` and `Plugins/Angelscript/Source/AngelscriptEditor/Snippet/`
- Automation tests: `Plugins/Angelscript/Source/AngelscriptTest/Core/` and `Plugins/Angelscript/Source/AngelscriptEditor/Tests/`
- OpenSpec and test catalog documentation for the new automation prefixes.
