from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BINDINGS = ROOT / "Plugins/Angelscript/Source/AngelscriptTest/Bindings"

REPLACEMENTS = [
    ('#include "Shared/AngelscriptBindingsAssertions.h"', '#include "Shared/AngelscriptTestExecute.h"'),
    ('#include "Shared/AngelscriptBindingsModuleBuilder.h"', '#include "Shared/AngelscriptTestModuleScope.h"'),
    ('#include "Shared/AngelscriptGlobalFunctionInvoker.h"', '#include "Shared/AngelscriptTestExecute.h"'),
]

MODULE_SCOPE_MARKER = '#include "Shared/AngelscriptTestModuleScope.h"'

FCOVERAGE_REPLACEMENTS = [
    ("FCoverageModuleScope", "FScopedAngelscriptModule"),
]

def process_file(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    original = text
    for old, new in REPLACEMENTS:
        if old in text and new not in text:
            text = text.replace(old, new)
    if '#include "Shared/AngelscriptTestExecute.h"' in text and MODULE_SCOPE_MARKER not in text:
        if "FScopedAngelscriptModule" in text or "ExpectGlobal" in text or "ExecuteAndExpect" in text:
            text = text.replace(
                '#include "Shared/AngelscriptTestExecute.h"',
                '#include "Shared/AngelscriptTestExecute.h"\n#include "Shared/AngelscriptTestModuleScope.h"',
                1,
            )
    for old, new in FCOVERAGE_REPLACEMENTS:
        text = text.replace(old, new)
    if text != original:
        path.write_text(text, encoding="utf-8")
        return True
    return False

def main() -> None:
    changed = 0
    for path in BINDINGS.rglob("*"):
        if path.suffix.lower() not in {".cpp", ".h"}:
            continue
        if process_file(path):
            changed += 1
            print(path.relative_to(ROOT))
    template = ROOT / "Plugins/Angelscript/Source/AngelscriptTest/Template/Template_CQTest.cpp"
    if template.exists() and process_file(template):
        changed += 1
        print(template.relative_to(ROOT))
    print(f"updated {changed} files")

if __name__ == "__main__":
    main()
