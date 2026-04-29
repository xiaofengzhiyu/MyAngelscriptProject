# Plan_BindingsTestSuiteRefactor 伴侣目录

主 Plan：[`../Plan_BindingsTestSuiteRefactor.md`](../Plan_BindingsTestSuiteRefactor.md)

本目录承载主 Plan 的可分发子 Plan、基座 API 规约与 case 清单 dump。每个子 Plan 都是一份**自闭环、可独立分发执行**的小 Plan，遵循根目录 `Plan.md` 的全部规则（任务编号 + checkbox + 紧跟 📦 提交 + 来龙去脉描述）。

## 角色分工

- **主 Plan 拥有者（基座 owner）**：✅ 已完成基座实现（`Bindings/Shared/` 5 文件已落地、金丝雀 `Bindings.SharedExample` 通过、Bindings 全量回归 134/134 绿）。后续负责审 PR / 派单 / 归档。
- **子 Plan 执行者**：每份 SubPlan 由独立执行者完成，只关心一个/一组目标文件的改造，不接触基座代码。开始前必须先读 [`BaseAPI.md`](./BaseAPI.md) 了解可用 helper。

## 派单表

> 推荐派单顺序、复杂度评估、第一波收尾决策点：见 [`Dispatch.md`](./Dispatch.md)。

## 子 Plan 列表（高密度 8 文件，实地 grep 数据）

| # | SubPlan | 目标文件 | 旧 ID 数 | 旧 `if` 分支 | 估算复杂度 |
|---|---------|----------|----------|---------------|------------|
| 1 | [SubPlan_Container.md](./SubPlan_Container.md) | `AngelscriptContainerBindingsTests.cpp` | 11 | 88 | ★★★★★（ID 最多） |
| 2 | [SubPlan_GameplayTag.md](./SubPlan_GameplayTag.md) | `AngelscriptGameplayTagBindingsTests.cpp` | 6 | **166** | ★★★★★（case 密度最大） |
| 3 | [SubPlan_FileAndDelegate.md](./SubPlan_FileAndDelegate.md) | `AngelscriptFileAndDelegateBindingsTests.cpp` | 7 | 88 | ★★★★（双主题混居，需双 Profile） |
| 4 | [SubPlan_Class.md](./SubPlan_Class.md) | `AngelscriptClassBindingsTests.cpp` | 7 | 83 | ★★★★（涉及 reflective 边界） |
| 5 | [SubPlan_Compat.md](./SubPlan_Compat.md) | `AngelscriptCompatBindingsTests.cpp` | 5 | 80 | ★★★（句柄类，主题相对独立） |
| 6 | [SubPlan_Console.md](./SubPlan_Console.md) | `AngelscriptConsoleBindingsTests.cpp` | 5 | 35 | ★★★（涉及 IConsoleManager 全局副作用） |
| 7 | [SubPlan_EnhancedInput.md](./SubPlan_EnhancedInput.md) | `AngelscriptEnhancedInputBindingsTests.cpp` | 8 | 20 | ★★★★（含 5 个 *Compiles 类型 ID） |
| 8 | [SubPlan_UserWidget.md](./SubPlan_UserWidget.md) | `AngelscriptUserWidgetBindingsTests.cpp` | 2 | 39 | ★★（结构最简，**推荐上手项**） |

## 执行流水

```
[基座 owner]                                            [子 Plan 执行者]
  ✅ 实现 Bindings/Shared/ (5 文件)                        │
  ✅ 金丝雀 Bindings.SharedExample 通过                    │
  ✅ BaseAPI.md 已落锁                                     │
  ✅ Bindings 全量回归 134/134 绿                          │
  ───────────────────── 派单 ──────────────────────────►  第一波：UserWidget（基座二次验证）
                                                          ↓ 反馈
                                                    （基座按需补 helper）
                                                          ↓
                                                          第二波：4 份并行
                                                          （Compat/Console/Class/EnhancedInput）
                                                          ↓
                                                          第三波：FileAndDelegate + Container
                                                          ↓
                                                          第四波：GameplayTag（最重）
```

详细派单计划与决策点：[`Dispatch.md`](./Dispatch.md)

## 共同规则（所有子 Plan 必须遵守）

1. **覆盖面只增不减**：开始前先把目标文件的所有 `if (xxx) return N;` 与 `TestEqual(...)` dump 到 `<SubPlanTopic>_CaseInventory.md`（与 SubPlan 同目录），改造完成后逐项打勾。
2. **禁用 `int Entry()` + return code 模式**：所有新 case 用 `FASGlobalFunctionInvoker` 或基座提供的 `ExpectGlobalInt*` helper，每个 case 一次小调用 + 立即一次 `TestEqual`/`TestTrue`，失败信号必须包含 case 名 + 实际/期望值。
3. **保留所有 Automation ID**：旧 ID 不删，但其 `RunTest` 改为"创建 Profile + 调用对应 Section 子集"模式。如多个旧 ID 共享同一 Theme，可在文件内拆为多个 Section 函数，每个 ID 调用对应子集。
4. **模块名走基座**：禁止文件内裸写 `BuildModule(..., "ASXxx", ...)`，统一使用 `BuildCoverageModule(Profile, SectionName, Source)`。
5. **share-clean + ResetSharedCloneEngine**：沿用 TArray 文件的引擎隔离模式（参考 `AngelscriptTArrayBindingsTests.cpp` 第 2255-2272 行）。
6. **提交粒度**：每个任务编号一个 commit，遵循 `Documents/Rules/GitCommitRule_ZH.md`。
7. **每个子 Plan 完工后**：在自己 SubPlan 顶部补 `归档状态 / 归档日期 / 完成判断 / 结果摘要`，但**不移动文件位置**（仍在伴侣目录），由主 Plan owner 在所有子 Plan 完工后统一归档主 Plan。

## 验收基线（所有子 Plan 共用）

- 改造后的目标文件 `grep "return 1[0-9][0-9]"` 命中数为 **0**
- 改造后的目标文件 `grep "int Entry()"` 命中数为 **0**
- 改造后的目标文件不再出现 `BuildModule(..., "AS\w+",` 裸字符串（必须经基座包装）
- 对应 `Angelscript.TestModule.Bindings.<原 ID 名>` 测试全绿
- 该文件断言总数 ≥ 改造前 baseline 数（dump 时记录）
- 全量 `Angelscript.TestModule.Bindings.*` 不引入新失败

## 索引文件

- [`BaseAPI.md`](./BaseAPI.md) — 基座 API 规约（基座 owner 实现完成后会落锁；执行者据此抄写）
- 各 `<SubPlanTopic>_CaseInventory.md` — 由对应 SubPlan 执行者在第一步产出
