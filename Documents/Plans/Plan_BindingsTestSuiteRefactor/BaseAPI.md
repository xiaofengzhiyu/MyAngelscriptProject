# Bindings/Shared/ 基座 API 规约

> **状态：✅ 已锁定（2026-04-29）**
> **基座代码**：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/Shared/`（5 文件）
> **commit hash**：`6a572b2`（基座代码）/ `a3c4d74`（Plan 文档）
> **金丝雀验证**：`Angelscript.TestModule.Bindings.SharedExample` 已通过（Bindings 全量回归 134/134 通过）

本文档以**实际落地的基座代码**为准。子 Plan 执行者可以照抄本页所有签名与示例，编译/运行行为已端到端验证。

## 文件落点

```
Plugins/Angelscript/Source/AngelscriptTest/Bindings/Shared/
  AngelscriptBindingsCoverage.h              // FBindingsCoverageProfile + MakeCoverageModuleName + FormatCaseLabel
  AngelscriptBindingsModuleBuilder.h         // FCoverageModuleScope (RAII)
  AngelscriptBindingsAssertions.h            // ExpectGlobalInt / Bool / Double / IntAtLeast / Ints + ExecuteFunctionExpectingScriptException
  AngelscriptBindingsExampleSection.h        // 50 行示例 Section（执行者抄改起点）
  AngelscriptBindingsExampleSectionTests.cpp // 注册 Bindings.SharedExample，端到端自检
  README.md
```

## 命名空间

```cpp
using namespace AngelscriptTestBindings;
```

> 注意：实际命名空间是 `AngelscriptTestBindings`（不是早期草案中的 `AngelscriptTest::Bindings::Shared`）。原因：缩短 `using` 声明，避免每个测试文件多写两段 `using namespace`。

## 核心类型

### `FBindingsCoverageProfile`

```cpp
struct FBindingsCoverageProfile
{
    const TCHAR* Theme        = TEXT(""); // e.g. TEXT("Container")    — 出现在 case 标签
    const TCHAR* Variant      = TEXT(""); // e.g. TEXT("ConstRef") / TEXT("") — 用于多变体场景，空字符串表示无变体
    const TCHAR* ModulePrefix = TEXT(""); // e.g. TEXT("ASContainer")   — AS 模块名前缀，必须 AS 开头
    const TCHAR* CasePrefix   = TEXT(""); // e.g. TEXT("Container")     — case 标签的人类可读前缀
    const TCHAR* LogCategory  = TEXT(""); // e.g. TEXT("ContainerBindings") — 预留，目前仅信息性
};
```

### 模块名生成

```cpp
inline FString MakeCoverageModuleName(const FBindingsCoverageProfile& Profile, const TCHAR* SectionName);
//   无 Variant：<ModulePrefix>_<SectionName>      例：ASContainer_Optional
//   有 Variant：<ModulePrefix>_<Variant>_<SectionName>  例：ASContainer_ConstRef_Foreach
```

### Case 标签格式化

```cpp
inline FString FormatCaseLabel(const FBindingsCoverageProfile& Profile, const TCHAR* CaseLabel);
//   无 Variant："[<CasePrefix>] <CaseLabel>"      例："[Container] Empty Optional should not be set"
//   有 Variant："[<CasePrefix>/<Variant>] <CaseLabel>"
```

## RAII 模块构建（首选）

```cpp
struct FCoverageModuleScope
{
    FCoverageModuleScope(
        FAutomationTestBase& Test, FAngelscriptEngine& Engine,
        const FBindingsCoverageProfile& Profile,
        const TCHAR* SectionName,
        const FString& Source);

    ~FCoverageModuleScope();   // 自动 Engine.DiscardModule(ModuleName)

    bool                IsValid() const;
    asIScriptModule&    GetModule() const;        // 调用前必须 IsValid()
    const FString&      GetModuleName() const;    // 用于 AddExpectedError 等手工注册
};
```

**用法**（这是每个 Section 的标准开头）：

```cpp
static bool RunOptionalSection(FAutomationTestBase& Test, FAngelscriptEngine& Engine,
    const FBindingsCoverageProfile& Profile)
{
    FCoverageModuleScope ModuleScope(Test, Engine, Profile, TEXT("Optional"), TEXT(R"(
        int EchoEmpty()         { TOptional<int> O; return O.IsSet() ? 1 : 0; }
        int EchoEmptyFallback() { TOptional<int> O; return O.Get(7); }
    )"));
    if (!ModuleScope.IsValid()) return false;
    asIScriptModule& Module = ModuleScope.GetModule();
    // ... 后续断言
}
```

## 断言 Helper（一行一 case）

每个 helper 自动带 `Test.AddInfo(label)`，所以失败时日志能精确定位到 case 名 + 函数声明。

```cpp
// 标量断言
bool ExpectGlobalInt(
    FAutomationTestBase& Test, FAngelscriptEngine& Engine,
    asIScriptModule& Module, const FBindingsCoverageProfile& Profile,
    const TCHAR* FunctionDecl,   // 例：TEXT("int EchoEmpty()")
    const TCHAR* CaseLabel,      // 例：TEXT("Empty Optional should not be set")
    int32 Expected);

bool ExpectGlobalBool   (..., bool Expected);                // 内部转 0/1 走 ExpectGlobalInt
bool ExpectGlobalIntAtLeast (..., int32 Minimum);
bool ExpectGlobalDouble (..., double Expected, double Tolerance = 1e-6);

// 批量
struct FExpectedGlobalInt {
    const TCHAR* FunctionDecl;
    const TCHAR* CaseLabel;
    int32 Expected;
};
bool ExpectGlobalInts(..., TArrayView<const FExpectedGlobalInt> Cases);

// 异常路径（Prepare 成功 / Execute 抛异常 / 消息非空 / 消息包含子串 / 行号 > 0 五点联合校验）
bool ExecuteFunctionExpectingScriptException(
    FAutomationTestBase& Test, FAngelscriptEngine& Engine,
    asIScriptModule& Module, const FBindingsCoverageProfile& Profile,
    const TCHAR* FunctionDecl,
    const TCHAR* CaseLabel,
    const FString& ExpectedExceptionContains);
```

## 复杂场景：直接用 `FASGlobalFunctionInvoker`

`Expect*` helper 仅覆盖"无参 + 单标量返回"。需要多参数、struct return、out-ref、UObject 句柄等场景，直接用 `AngelscriptReflectiveAccess::FASGlobalFunctionInvoker`（位于 `AngelscriptTest/Shared/AngelscriptGlobalFunctionInvoker.h`）。完整参数矩阵示例见 `Template/Template_GlobalFunctions.cpp`。

## 主入口模板（标准 RunTest 形态）

```cpp
#include "Shared/AngelscriptBindingsCoverage.h"
#include "Shared/AngelscriptBindingsModuleBuilder.h"
#include "Shared/AngelscriptBindingsAssertions.h"
#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptTestBindings;

namespace {
const FBindingsCoverageProfile GContainerProfile{
    TEXT("Container"), TEXT(""), TEXT("ASContainer"),
    TEXT("Container"), TEXT("ContainerBindings"),
};

// === Section 函数前向声明 ===
bool RunOptionalSection      (FAutomationTestBase&, FAngelscriptEngine&, const FBindingsCoverageProfile&);
bool RunOptionalErrorSection (FAutomationTestBase&, FAngelscriptEngine&, const FBindingsCoverageProfile&);
// ...
}

// === Automation ID 注册 ===
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAngelscriptOptionalBindingsTest,
    "Angelscript.TestModule.Bindings.OptionalCompat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptOptionalBindingsTest::RunTest(const FString&)
{
    bool bPassed = true;
    FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
    ASTEST_BEGIN_SHARE_CLEAN
    ON_SCOPE_EXIT { ResetSharedCloneEngine(Engine); };
    bPassed &= RunOptionalSection(*this, Engine, GContainerProfile);
    ASTEST_END_SHARE_CLEAN
    return bPassed;
}

// 其他 ID 类似（每个 ID 调用其关心的 Section 子集）
// === Section 实现 ===
namespace {
bool RunOptionalSection(FAutomationTestBase& Test, FAngelscriptEngine& Engine,
    const FBindingsCoverageProfile& Profile)
{
    FCoverageModuleScope ModuleScope(Test, Engine, Profile, TEXT("Optional"), TEXT(R"(
        int EchoEmpty()         { TOptional<int> O; return O.IsSet() ? 1 : 0; }
        int EchoEmptyFallback() { TOptional<int> O; return O.Get(7); }
        int EchoSet()           { TOptional<int> O(42); return O.IsSet() ? 1 : 0; }
        int EchoSetValue()      { TOptional<int> O(42); return O.GetValue(); }
    )"));
    if (!ModuleScope.IsValid()) return false;
    auto& Module = ModuleScope.GetModule();

    bool bPassed = true;
    bPassed &= ExpectGlobalInt(Test, Engine, Module, Profile,
        TEXT("int EchoEmpty()"), TEXT("Empty Optional should not be set"), 0);
    bPassed &= ExpectGlobalInt(Test, Engine, Module, Profile,
        TEXT("int EchoEmptyFallback()"), TEXT("Empty Get fallback should return default"), 7);
    bPassed &= ExpectGlobalBool(Test, Engine, Module, Profile,
        TEXT("int EchoSet()"), TEXT("Optional with value should be set"), true);
    bPassed &= ExpectGlobalInt(Test, Engine, Module, Profile,
        TEXT("int EchoSetValue()"), TEXT("GetValue should return the set value"), 42);
    return bPassed;
}
}

#endif
```

## 关键约定

1. **模块名永远走基座**：禁止文件内裸写 `BuildModule(..., "ASXxx", ...)`。
2. **case 函数返回 int**：脚本侧每个 case 函数返回 0/1 或小整数，C++ 侧 `ExpectGlobalInt` 断言。
3. **case 标签写"行为"不写"函数名"**：`TEXT("Empty Optional should not be set")` 而非 `TEXT("EchoEmpty")`，因为函数声明已被 invoker 自动包含在失败信息里。
4. **`FCoverageModuleScope` 析构会自动 `DiscardModule`**：不需要再写 `ON_SCOPE_EXIT { Engine.DiscardModule(...) };`。
5. **share-clean 引擎**：`ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `ResetSharedCloneEngine(Engine)`，与 TArray 文件一致。
6. **`AddExpectedError` 由 SubPlan 自己注册**：基座不接管。旧文件中的注册必须迁移到新模块名 `MakeCoverageModuleName(Profile, SectionName)`。

## 自检命令

```powershell
Tools\RunTests.ps1 -TestPrefix Angelscript.TestModule.Bindings.SharedExample -Label canary -TimeoutMs 600000
```

绿色即基座可用。

## TimeoutMs 上限提醒

`Tools\RunTests.ps1` 的 `-TimeoutMs` **上限是 900000**（15 分钟）。超过会立即返回 exit code 1 且不创建任何输出目录。SubPlan 中所有命令示例已对齐这个上限。
