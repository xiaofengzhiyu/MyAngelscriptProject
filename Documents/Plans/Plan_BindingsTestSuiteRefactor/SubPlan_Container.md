# SubPlan: Container 测试改造

> 主 Plan：[`../Plan_BindingsTestSuiteRefactor.md`](../Plan_BindingsTestSuiteRefactor.md)  
> 共同规则与基座 API：[`README.md`](./README.md) + [`BaseAPI.md`](./BaseAPI.md)  
> 前置依赖：✅ 基座代码已就绪（`Bindings/Shared/` 5 文件已落地、金丝雀 `Bindings.SharedExample` 通过、Bindings 全量回归 134/134 绿）

## 目标文件与现状

- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp`
- 文件大小：21.35 KB
- Automation ID 数：**11 个**（目录最多）
- 旧式 case 规模：**88 个 `if` 分支** / **62 个 `return N;`** / **17 个 `TestEqual`**（实地 grep 数据）
- 模式：典型旧式扁平 —— 单文件 11 个 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`，每个 `RunTest` 跑一段大 `int Entry()`，靠 `if (...) return N;` 区分失败原因

### 现有 Automation ID 清单

| # | ID | 主题 |
|---|-----|------|
| 1 | `OptionalCompat` | TOptional 基本能力 |
| 2 | `OptionalGetValueUnsetError` | TOptional 异常路径 |
| 3 | `SetCompat` | TSet 基本能力 |
| 4 | `MapCompat` | TMap 基本能力 |
| 5 | `MapFindFailureAndFindOrAddRefCompat` | TMap 引用语义 |
| 6 | `ArrayForeach` | TArray foreach 语法 |
| 7 | `SetForeach` | TSet foreach 语法 |
| 8 | `SetForeachExactVisit` | TSet foreach 精确访问 |
| 9 | `MapForeach` | TMap foreach 语法 |
| 10 | `MapForeachKeyValuePairing` | TMap foreach key/value 配对 |
| 11 | `ForeachNestedArrayMap` | 嵌套容器 foreach |

> 注：现有 ID 分布于文件不同行；ID 6-11 在文件 350+ 行起，部分 ID 名末尾的 `Compat` 后缀不一致（有的带有有的没有），改造时**保留原名一字不改**。

## Section 切分方案

把 11 个 ID 归并为 **5 个 Section**，每个 ID 的 `RunTest` 调用其对应的 Section 子集。

| Section | 包含的旧 ID | 主题 |
|---------|-------------|------|
| `RunOptionalSection` | `OptionalCompat` | TOptional 构造 / 赋值 / IsSet / Get / GetValue / Reset / Copy |
| `RunOptionalErrorSection` | `OptionalGetValueUnsetError` | 未设置时 GetValue 抛异常的完整四元组（异常类型 / 消息 / 行号 / Prepare 成功） |
| `RunSetSection` | `SetCompat` | TSet 增/查/删/拷贝/Reset/Names 类型 |
| `RunMapSection` | `MapCompat` + `MapFindFailureAndFindOrAddRefCompat` | TMap 全功能 + Find 失败语义 + FindOrAdd 引用语义 |
| `RunForeachSection` | `ArrayForeach` + `SetForeach` + `SetForeachExactVisit` + `MapForeach` + `MapForeachKeyValuePairing` + `ForeachNestedArrayMap` | foreach 语法在三种容器与嵌套场景下的覆盖 |

## Profile 定义

```cpp
const FBindingsCoverageProfile GContainerProfile{
    TEXT("Container"), TEXT(""), TEXT("ASContainer"),
    TEXT("Container"), TEXT("ContainerBindings"),
};
```

## 分阶段执行计划

### Phase 0 — Case 清单 dump

- [ ] **P0.1** 把 14 段 `int Entry()` 中所有 `if (...) return N;` 与所有 `TestEqual(...)` 全部 dump 到 `Container_CaseInventory.md`（与本 SubPlan 同目录）
  - 每条记录列：旧 ID 名 / `int Entry()` 内行号 / 条件原文 / 期望返回值 / 失败语义（"成功路径"/"返回 10 表示 IsEmpty 失败" 等）
  - 这是后续改造的下界基线，新 Section 必须 100% 覆盖每一条 dump
- [ ] **P0.1** 📦 Git 提交：`[Docs/Plans] Docs: dump container bindings case inventory baseline`

### Phase 1 — Section 实现

- [ ] **P1.1** 实现 `RunOptionalSection`
  - 把旧 `OptionalCompat` 内 `int Entry()` 中每个 `if/return` 拆为独立 case 函数（脚本侧），每个返回 int
  - 例：`int EchoEmpty_IsSet()` → `0`、`int EchoEmpty_GetFallback()` → `7`、`int EchoSet_IsSet()` → `1` 等
  - 在 C++ 侧用 `ExpectGlobalInt` 一行一个 case 调用 + 断言；CaseLabel 必须人类可读，例如 `TEXT("Empty TOptional should report IsSet()=false")`
  - `Empty.GetValue()`/`Empty == Copy`/`OptionalName.Get(Fallback)` 等所有路径全覆盖
- [ ] **P1.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Container OptionalCompat as section`

- [ ] **P1.2** 实现 `RunOptionalErrorSection`
  - 沿用基座 `ExecuteFunctionExpectingScriptException`
  - 验证脚本：`int Trigger() { TOptional<int> Empty; return Empty.GetValue(); }`
  - 期望异常子串：`TEXT("GetValue() called on Optional when not set")`
  - 同时验证 Prepare 成功、ExceptionLine > 0（与现有断言对位）
  - **重要**：保留旧实现里 `UE_SET_LOG_VERBOSITY(Angelscript, Fatal)` + `ON_SCOPE_EXIT` 复位的处理；如基座未提供日志降级 RAII，本 Section 内联实现并加 TODO 上抛基座
- [ ] **P1.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Container OptionalGetValueUnsetError as exception section`

- [ ] **P1.3** 实现 `RunSetSection`
  - 旧 `SetCompat` 的 9 个 `if/return` 拆为独立 case
  - `TSet<int>` 与 `TSet<FName>` 两个元素类型都覆盖
  - Add 重复元素去重（`Empty.Add(4); Empty.Add(4); Num()==1`）、Contains/Remove/Reset/Copy 全覆盖
- [ ] **P1.3** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Container SetCompat as section`

- [ ] **P1.4** 实现 `RunMapSection`
  - 合并 `MapCompat` 与 `MapFindFailureAndFindOrAddRefCompat` 两个旧 ID 的所有 case
  - 覆盖：Add / 重复 key Add 覆盖（`Add("Alpha",4); Add("Alpha",7);` 后 Find 得 7）/ Contains / Find（成功+失败两路）/ FindOrAdd（已存在不覆盖；不存在用默认值；返回引用可被赋值修改）/ Remove / Reset / 拷贝
  - `TMap<FName, int>` 与 `TMap<FName, FName>` 两种 value 类型都覆盖
  - **特别注意**：`FindOrAdd` 返回引用语义在脚本侧通过 `int& Gamma = Values.FindOrAdd(...); Gamma = 33;` 体现，新 case 函数需要保留这种语义验证（可以用 `int Echo_FindOrAddRefAssignable()` 函数封装并在内部断言）
- [ ] **P1.4** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Container Map sections (compat + find-failure)`

- [ ] **P1.5** 实现 `RunForeachSection`
  - 合并 6 个 foreach 旧 ID 的所有 case
  - **TArray foreach**：`foreach(Value, Index : Values)` 求 Sum 与 IndexSum
  - **TSet foreach**：`foreach(Value : Set)` 求 Sum；以及 `SetForeachExactVisit` 验证每个元素恰被访问一次（用 visit 计数器）
  - **TMap foreach**：`foreach(Value : Map)` 与 `foreach(Value, Key : Map)` 两种语法；`MapForeachKeyValuePairing` 验证 key 与 value 的配对正确性（构造 `{"A":1,"B":2,"C":3}` 后断言每个 key 对应的 value）
  - **嵌套**：`ForeachNestedArrayMap` 在 `TArray<TMap<FName,int>>` 上做嵌套 foreach
  - 每个语法/容器组合一个独立 case 函数，CaseLabel 写明覆盖意图
- [ ] **P1.5** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Container foreach sections`

### Phase 2 — Automation ID 接线

- [ ] **P2.1** 把 11 个 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 的 `RunTest` 全部改为"建 Profile + 调对应 Section"
  - 每个 ID 的 RunTest 体不超过 10 行（创建 Engine / `ASTEST_BEGIN_SHARE_CLEAN` / `ON_SCOPE_EXIT ResetSharedCloneEngine` / 调用 Section / `ASTEST_END_SHARE_CLEAN` / return）
  - 删除文件内所有 `BuildModule(..., "ASXxx", ...)` 裸字符串、所有 `int Entry()` 字符串字面量、所有 `ExecuteIntFunction + TestEqual(Result, 1)` 套路
  - 保留 11 个 ID 名一字不改
- [ ] **P2.1** 📦 Git 提交：`[Tests/Bindings] Refactor: wire Container automation IDs to coverage sections`

### Phase 3 — 验证与回归

- [ ] **P3.1** 与 P0 dump 逐项对位
  - 对照 `Container_CaseInventory.md`，把每条旧 case 在新 Section 中找到对应 `ExpectGlobal*` 调用，打勾
  - 任何漏项必须补回；如果某个旧 case 在新结构中合并或拆分，在 dump 文档对应行写明合并/拆分关系
- [ ] **P3.1** 📦 Git 提交：`[Docs/Plans] Docs: confirm Container case inventory coverage`

- [ ] **P3.2** 单文件回归
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings.OptionalCompat`
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings.OptionalGetValueUnsetError`
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings.SetCompat`
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings.MapCompat`
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings.MapFindFailureAndFindOrAddRefCompat`
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings.ArrayForeach`
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings.SetForeach`
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings.SetForeachExactVisit`
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings.MapForeach`
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings.MapForeachKeyValuePairing`
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings.ForeachNestedArrayMap`
  - 11 个 ID 全绿
- [ ] **P3.2** 📦 Git 提交：`[Tests/Bindings] Test: container subplan single-id regression green`

- [ ] **P3.3** Bindings 整体回归
  - 跑 `Tools\RunTests.ps1 -Prefix Angelscript.TestModule.Bindings`
  - 整体 0 新增失败、断言数较 P0 baseline 不下降
- [ ] **P3.3** 📦 Git 提交：`[Tests/Bindings] Test: container subplan full bindings regression`

## 验收标准

1. `AngelscriptContainerBindingsTests.cpp` 内 `grep "int Entry()"` 命中 = 0
2. `grep "return 1[0-9][0-9]"` 命中 = 0（不再用 return code 区分失败）
3. `grep "BuildModule(.*\"AS"` 命中 = 0（必须经基座包装）
4. 11 个原 Automation ID 全部保留且全绿
5. `Container_CaseInventory.md` 中每条旧 case 都在 dump 文档对应行打勾，且能在新 Section 中找到对应 `ExpectGlobal*` 调用
6. 改造后该文件断言总数 ≥ baseline（dump 时记录的 `if/return` 数 + `TestEqual` 数之和）
7. 全量 `Angelscript.TestModule.Bindings.*` 相对 baseline 0 新增失败

## 风险与注意事项

### 风险

1. **Foreach 语法在 share-clean 引擎下的注册时机**：foreach 是引擎特性，不是普通 binding，确认基座 `BuildCoverageModule` 没有抑制 foreach 注册路径。
   - **缓解**：P1.5 先实现一个最简 `TArray<int>` foreach Sum，单独运行验证基座支持，再扩展到 Set/Map/嵌套。
2. **`MapFindFailureAndFindOrAddRefCompat` 的引用语义**：脚本侧 `int& Ref = Map.FindOrAdd(...); Ref = X;` 改写为函数后，需要保证基座 invoker 不会让脚本端的引用语义失效。
   - **缓解**：把"FindOrAdd 返回引用 + 赋值修改 + 后续 Find 验证修改生效"作为单个 case 函数的整体（脚本侧自封装），C++ 侧只看最终 int 返回，不试图跨函数边界传引用。

### 已知行为变化

1. `OptionalGetValueUnsetError` 旧实现使用 `UE_SET_LOG_VERBOSITY(Angelscript, Fatal)` 抑制日志，新实现需保留同等抑制（要么基座提供 RAII，要么 P1.2 内联），否则 CI 日志会被 GetValue 异常刷屏。
2. 改造后断言数预计从 11 个 `TestEqual(Result, 1)` + 4 个异常断言 ≈ **15 个**，扩大到约 **60+ 个**（每个旧 `if` 一个 ExpectGlobal*），属预期内增长。
