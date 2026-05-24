#!/usr/bin/env python3
"""One-shot helper: unwrap AngelscriptTest Shared namespaces and fix callsites."""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PLUGIN_TEST = REPO / "Plugins" / "Angelscript" / "Source" / "AngelscriptTest"
PLUGIN_GAS = REPO / "Plugins" / "AngelscriptGAS" / "Source" / "AngelscriptGASTest"
SHARED = PLUGIN_TEST / "Shared"

NAMESPACES = (
    "AngelscriptTestSupport",
    "AngelscriptTest",
    "AngelscriptTestBindings",
    "AngelscriptReflectiveAccess",
)


def unwrap_single_namespace(content: str, namespace: str) -> str:
    """Remove one `namespace X { ... }` block (outermost match) and dedent body."""
    pattern = rf"namespace {re.escape(namespace)}\s*\n\{{"
    match = re.search(pattern, content)
    if not match:
        return content

    start = match.start()
    brace_start = match.end() - 1
    depth = 0
    i = brace_start
    while i < len(content):
        if content[i] == "{":
            depth += 1
        elif content[i] == "}":
            depth -= 1
            if depth == 0:
                brace_end = i
                break
        i += 1
    else:
        raise ValueError(f"unmatched brace for namespace {namespace}")

    before = content[:start]
    inner = content[brace_start + 1 : brace_end]
    after = content[brace_end + 1 :]

    dedented_lines = []
    for line in inner.splitlines(keepends=True):
        if line.startswith("\t"):
            dedented_lines.append(line[1:])
        elif line.startswith("    "):
            dedented_lines.append(line[4:])
        else:
            dedented_lines.append(line)
    inner = "".join(dedented_lines)

    # strip leading blank line after opening brace
    if inner.startswith("\n"):
        inner = inner[1:]

    return before + inner + after


def unwrap_all_namespaces(content: str) -> str:
    changed = True
    while changed:
        changed = False
        for ns in NAMESPACES:
            new_content = unwrap_single_namespace(content, ns)
            if new_content != content:
                content = new_content
                changed = True
    return content


def strip_namespace_detail(content: str) -> str:
    """Flatten `namespace Detail { ... }` inside headers."""
    while True:
        match = re.search(r"namespace Detail\s*\n\{", content)
        if not match:
            break
        content = unwrap_single_namespace(content, "Detail")
    return content


def qualify_strip(content: str) -> str:
    for ns in NAMESPACES:
        content = content.replace(f"{ns}::", "")
    content = content.replace("Detail::TraceCase", "AngelscriptTestTraceCase")
    return content


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return path.read_text(encoding="utf-8", errors="replace")


def process_header(path: Path, extra: str | None = None) -> bool:
    text = read_text(path)
    original = text
    if extra == "execute":
        text = process_execute_header(text)
    else:
        while True:
            prev = text
            text = unwrap_all_namespaces(text)
            text = strip_namespace_detail(text)
            if text == prev:
                break
        text = qualify_strip(text)
    if text != original:
        path.write_text(text, encoding="utf-8", newline="\n")
        return True
    return False


def process_execute_header(text: str) -> str:
    """Special-case Execute.h: drop ReflectiveAccess forwarder block, merge bindings forwarders."""
    # Remove PART 3 block (ReflectiveAccess aliases in Execute.h only)
    part3_start = text.find("// PART 3 — namespace AngelscriptReflectiveAccess")
    part4_start = text.find("// PART 4 — namespace AngelscriptTestBindings")
    if part3_start != -1 and part4_start != -1:
        text = text[:part3_start] + text[part4_start:]

    while True:
        prev = text
        text = unwrap_all_namespaces(text)
        text = strip_namespace_detail(text)
        if text == prev:
            break

    text = qualify_strip(text)

    # Global legacy alias after executor struct
    if "using FASGlobalFunctionInvoker = FAngelscriptTestExecutor" not in text:
        marker = "struct FAngelscriptTestExecutor"
        idx = text.find(marker)
        if idx != -1:
            # insert after closing }; of struct - find first }; after struct start
            close_idx = text.find("};", idx)
            if close_idx != -1:
                insert_at = close_idx + 3
                text = (
                    text[:insert_at]
                    + "\n\n/** Legacy alias for FAngelscriptTestExecutor. */\nusing FASGlobalFunctionInvoker = FAngelscriptTestExecutor;\n"
                    + text[insert_at:]
                )

    # Rename TraceCase helper if still nested name
    text = text.replace("inline bool TraceCase(", "inline bool AngelscriptTestTraceCase(")

    return text


def process_callsite_file(path: Path) -> bool:
    text = read_text(path)
    original = text

    lines = text.splitlines(keepends=True)
    filtered = []
    for line in lines:
        stripped = line.strip()
        if stripped in (
            "using namespace AngelscriptTestSupport;",
            "using namespace AngelscriptTest;",
            "using namespace AngelscriptTestBindings;",
            "using namespace AngelscriptReflectiveAccess;",
        ):
            continue
        filtered.append(line)
    text = "".join(filtered)

    text = qualify_strip(text)

    if text != original:
        path.write_text(text, encoding="utf-8", newline="\n")
        return True
    return False


def main() -> int:
    headers = [
        SHARED / "AngelscriptTestEngineAcquisition.h",
        SHARED / "AngelscriptTestEngineCleanup.h",
        SHARED / "AngelscriptTestMemoryProbe.h",
        SHARED / "AngelscriptTestModuleBuilder.h",
        SHARED / "AngelscriptTestFixture.h",
        SHARED / "AngelscriptTestModuleScope.h",
        SHARED / "AngelscriptReflectiveAccess.h",
        SHARED / "AngelscriptBindingsExampleSection.h",
    ]

    execute = SHARED / "AngelscriptTestExecute.h"
    if execute.exists():
        text = read_text(execute)
        execute.write_text(process_execute_header(text), encoding="utf-8", newline="\n")
        print(f"updated {execute}")

    for h in headers:
        if h.exists() and process_header(h):
            print(f"updated {h}")

    # Utilities comments only
    util = SHARED / "AngelscriptTestUtilities.h"
    if util.exists():
        t = read_text(util)
        t2 = t.replace("AngelscriptTestSupport::", "").replace(
            "`AngelscriptTestSupport::` namespace (or at top-level for\n// `FAngelscriptTestEngineScopeAccess`).",
            "global scope (top-level `FAngelscriptTestEngineScopeAccess` and themed helpers).",
        )
        if t2 != t:
            util.write_text(t2, encoding="utf-8", newline="\n")
            print(f"updated {util}")

    extra_headers = [
        SHARED / "AngelscriptDebuggerScriptFixture.h",
        SHARED / "AngelscriptDebuggerTestSession.h",
    ]
    for h in extra_headers:
        if h.exists() and process_header(h):
            print(f"updated {h}")

    roots = [PLUGIN_TEST, PLUGIN_GAS]
    exts = {".cpp", ".h"}
    count = 0
    ns_markers = tuple(f"namespace {ns}" for ns in NAMESPACES)
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.suffix not in exts:
                continue
            raw = read_text(path)
            if any(marker in raw for marker in ns_markers):
                if path == execute:
                    continue
                if process_header(path):
                    print(f"unwrapped namespaces in {path}")
                    continue
            if process_callsite_file(path):
                count += 1
    print(f"updated {count} callsite files")

    example_tests = SHARED / "AngelscriptBindingsExampleSectionTests.cpp"
    if example_tests.exists():
        process_callsite_file(example_tests)

    return 0


if __name__ == "__main__":
    sys.exit(main())
