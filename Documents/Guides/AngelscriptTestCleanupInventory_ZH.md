# AngelscriptTest 测试模块清理清单

> 生成日期：2026-05-25  
> 背景：Bindings P1/P2/P5 整理已完成（见 `Plugins/Angelscript/Source/AngelscriptTest/Shared/README.md` § Bindings Execute migration）。本文档记录**后续可清理项**，按优先级与主题分类。  
> 决策规则与参考形态：`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`。

---

## 1. TArray `CoverageProfile` 是否多余？

### 结论（简短）

| 项 | 是否多余 | 说明 |
|----|----------|------|
| **两个不同的 Profile 配置**（`TArray` vs `TArraySyntax`） | **否** | 两套模块前缀、trace 函数名、`bBracketArraySyntax` 不同，必须区分 |
| `TArrayBindingsCoverageProfile()` / `TArraySyntaxCompatCoverageProfile()` **工厂函数** | **形式可简化** | 可改为具名 `constexpr` 常量，不必每次 `()` 调用 |
| 各 `.cpp` 里的 `static const ... = XxxCoverageProfile()` | **略冗余** | 可直接 `#include` helper 后使用 `TArrayBindingsProfile` / `TArraySyntaxCompatProfile` 全局常量 |
| `FArraySyntaxCoverageProfile::LogCategory` | **是（死字段）** | 全模块无读取，可删 |
| `TArrayBindingsFormatCoverageText(Profile, Text)` | **基本是空操作** | 当前 `return Text;`，未用 `CasePrefix` 做文案前缀；要么实现前缀，要么删掉 `Profile` 参数 |

### 两个 Profile 的实际差异

```cpp
// TArray 标准语法
CasePrefix = "TArray"
ModulePrefix = "ASTArray"
TraceFunctionDecl = "void TraceTArrayCase(...)"
bBracketArraySyntax = false   // TArray<int>

// Syntax compat
CasePrefix = "TArraySyntax"
ModulePrefix = "ASTArraySyntaxCompat"
TraceFunctionDecl = "void TraceSyntaxCase(...)"
bBracketArraySyntax = true    // int[]
```

因此：**不是「两个函数写重复配置」的问题，而是「用函数返回静态配置」可以写得更直白」。**

### 建议目标形态（低优先级 cosmetic）

在 `AngelscriptTArrayBindingsTestHelpers.h` 中：

```cpp
inline constexpr FArraySyntaxCoverageProfile TArrayBindingsProfile{
    TEXT("TArray"), TEXT("ASTArray"), TEXT("void TraceTArrayCase(const FString&in)"), false};

inline constexpr FArraySyntaxCoverageProfile TArraySyntaxCompatProfile{
    TEXT("TArraySyntax"), TEXT("ASTArraySyntaxCompat"), TEXT("void TraceSyntaxCase(const FString&in)"), true};
```

- 删除 `TArrayBindingsCoverageProfile()` / `TArraySyntaxCompatCoverageProfile()`
- 两测试 `.cpp` 删除文件内 `static const ... = ...()`，统一用上述常量
- 可选：结构体重命名为 `FTArrayBindingsTestProfile`（去掉历史名 `SyntaxCoverage`）

**风险**：低；改后跑 `Angelscript.TestModule.Bindings.Container.TArray` 前缀即可。

---

## 2. Bindings 主题 — 建议清理（P3）

### 2.1 过期文件头注释（高价值、零行为风险）

迁移后仍写「保留 `ExecuteValueFunction`」等，易误导后续维护：

| 文件 | 问题 |
|------|------|
| `AngelscriptMathBindingsTests.cpp` | 头注释仍提 `ExecuteValueFunction` / Private namespace |
| `AngelscriptMathOrientationBindingsTests.cpp` | 同上 |
| `AngelscriptAssetRegistryBindingsTests.cpp` | 仍写 `ExecuteFunctionExpectingException` |

**改法**：改为「结构体返回用 `FAngelscriptTestExecutor::ExecuteAndExtractStruct` + `AngelscriptMathBindingsTestCompare.h`」。

### 2.2 `AngelscriptTArrayBindingsTestHelpers.h` 内死代码

| 项 | 动作 |
|----|------|
| `LogCategory` 成员 | 删除，或接入 `UE_LOG`（若确实需要分类日志） |
| `TArrayBindingsFormatCoverageText` | 删除并内联 `Text`，或实现 `FString::Printf(TEXT("[%s] %s"), Profile.CasePrefix, *Text)` |

### 2.3 TArray 测试 `.cpp` 结构（可选）

- `RunTArray*Section(..., const FArraySyntaxCoverageProfile& Profile)`：单文件内 Profile 恒为 `TArrayProfile`，可去掉参数、闭包使用文件级常量，减少数百处 `Profile` 传参。
- `AngelscriptTArraySyntaxCompatBindingsTests.cpp`：大量重复传 `TArraySyntaxCompatProfile`，同上。

### 2.4 仍含 `*_Private` 的 Bindings 文件（保留 vs 再抽 helper）

**应保留 Private（有专用逻辑）**：

| 文件 | Private 内容 |
|------|----------------|
| `AngelscriptReflectiveFallbackCacheTests.cpp` | GameplayTag 反射前缀、缓存场景 |
| `AngelscriptMathAndPlatformBindingsTests.cpp` | `FormatScriptFloatLiteral` 等 |
| `AngelscriptTextFormattingBindingsTests.cpp` | 文本格式化 |
| `AngelscriptWorldCollisionBindingsTests.cpp` | 碰撞体搭建、常量、Functional 世界 |
| `AngelscriptCollisionParamsBindingsTests.cpp` | `CopyIgnoredIds`、模块名常量 |
| `AngelscriptAssetRegistryBindingsTests.cpp` | `GetAssetRegistryChecked`、资产路径断言 |
| `AngelscriptScriptFunctionLibraryTests.cpp` | 热重载模块名、字符串全局 Execute |
| `AngelscriptWorldFunctionLibraryTests.cpp` | 模块名常量（仅 2 个 `constexpr`） |
| `AngelscriptWorldCollisionFunctionLibraryComponentTests.cpp` | 组件/世界搭建 |
| `AngelscriptWorldCollisionAsyncBindingsTests.cpp` | Async + Functional |

**可评估再整理（P3）**：

| 文件 | 建议 |
|------|------|
| `AngelscriptDataTableBindingsTests.cpp` | 对照 P1，是否仍有私有 `Execute*` 可换全局 API |
| `AngelscriptWorldFunctionLibraryTests.cpp` | 仅常量的 Private 可改为文件顶部 `namespace { constexpr ... }` 或匿名 namespace |
| `AngelscriptAssetRegistryBindingsTests.cpp` | Private helper 可迁 `AngelscriptAssetRegistryBindingsTestHelpers.h`（Bindings 内） |

### 2.5 Console 簇（P4，低优先级）

- 已符合 `*Sections.h` 范式；仅需确认 include 已为 `AngelscriptTestModuleScope.h` / `AngelscriptTestExecute.h`（Bindings 批量脚本未覆盖 Console 子文件时手动 spot-check）。

---

## 3. Syntax / Functional — P5 延续（Bindings 外）

Bindings 已清零转发 shim；以下仍 `#include "Shared/AngelscriptBindingsAssertions.h"`（约 **25** 个 Syntax `.cpp` + **10** 个 Functional Interface/Inheritance + `Template_CQTest.cpp`）。

**改法**：复用 `Tools/Diagnostics/UpdateBindingsTestIncludes.py`，扩展 glob 到 `Syntax/`、`Functional/`、`Template/`（或复制脚本为 `UpdateTestModuleIncludes.py`）。

**验证**：`rg 'AngelscriptBindingsAssertions\.h' Plugins/Angelscript/Source/AngelscriptTest --glob '*.cpp'` 目标为 0（除 Shared 转发头自身）。

---

## 4. Shared 层 — 长期项（非紧急）

| 项 | 说明 |
|----|------|
| 转发 shim 退役 | `AngelscriptBindingsAssertions.h` 等可保留至 Syntax/Functional 迁移完成，再评估 deprecation 注释 |
| `AngelscriptTestUtilities.h` 伞头 | README 已说明 305 TU 依赖；新代码应直接 include 子头 |
| 物理分目录 | `Shared/Engine|Execute|Module/` — 等 API 稳定后单独 OpenSpec |
| 全模块 `AngelscriptTest_*_Private` | 约 **178** 文件（HotReload / Learning / Compiler / SDK 等），**不要**与 Bindings 整理混做 |

---

## 5. 父仓库 `Tools/Diagnostics/` 临时脚本

| 文件 | 建议 |
|------|------|
| `UpdateBindingsTestIncludes.py` | **可保留** — Syntax/Functional 批量改 include 时复用 |
| `strip_tarray_private.py` | **删除或不提交** — TArray 迁移一次性脚本 |
| `migrate_syntax_compat.py` | **删除或不提交** |
| `MigrateTArrayBindingsTests.py` | **删除或不提交** |

---

## 6. 建议实施顺序

```mermaid
flowchart LR
  A[P3 过期注释 + TArray 死字段]
  B[P3 MathAndPlatform / ReflectiveFallback helper]
  C[P5 Syntax include 批量]
  D[TArray Profile 常量化 cosmetic]
  A --> B
  B --> C
  C --> D
```

1. **Quick win**：修正 Math/AssetRegistry 文件头注释；删 `LogCategory` / 简化 `FormatCoverageText`（+ TArray 测试前缀）。
2. **P3 Bindings**：按需抽 `MathAndPlatform` / `ReflectiveFallback` helper。
3. **P5 扩展**：Syntax + Functional include 迁移。
4. **TArray cosmetic**：Profile 改 `constexpr` 常量、去掉 `Run*Section` 的 Profile 参数（纯可读性）。

---

## 7. 验证命令（改后）

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunBuild.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Bindings.Container.TArray"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Bindings.Math"
```

静态检查：

```powershell
rg "ExecuteValueFunction" Plugins/Angelscript/Source/AngelscriptTest/Bindings
rg "AngelscriptBindingsAssertions\.h" Plugins/Angelscript/Source/AngelscriptTest --glob "*.cpp"
```

---

## 8. 变更记录

| 日期 | 说明 |
|------|------|
| 2026-05-25 | 初版：Bindings 整理后续清理项；TArray Profile 设计说明 |
