# SubPlan: GameplayTag 测试改造

> 主 Plan：[`../Plan_BindingsTestSuiteRefactor.md`](../Plan_BindingsTestSuiteRefactor.md)  
> 共同规则与基座 API：[`README.md`](./README.md) + [`BaseAPI.md`](./BaseAPI.md)  
> 前置依赖：✅ 基座代码已就绪（`Bindings/Shared/` 5 文件已落地、金丝雀 `Bindings.SharedExample` 通过、Bindings 全量回归 134/134 绿）

## 目标文件与现状

- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp`
- 文件大小：29.95 KB（目录最大的非 TArray 文件）
- Automation ID 数：**6 个**
- 旧式 case 规模：**166 个 `if` 分支** / **106 个 `return N;`** / **18 个 `TestEqual+TestTrue`**（实地 grep 数据；目录里 case 密度最大的文件，工作量最重）
- 依赖：`UGameplayTagsManager` 全局单例、`GameplayTags.ini` 配置

### 现有 Automation ID 清单

| # | ID | 主题 |
|---|-----|------|
| 1 | `GameplayTagCompat` | FGameplayTag 单值构造、比较、IsValid |
| 2 | `GameplayTagContainerCompat` | FGameplayTagContainer 容器操作 |
| 3 | `GameplayTagContainerEmptyContracts` | 空容器边界契约 |
| 4 | `GameplayTagQueryCompat` | HasTag/HasAny/HasAll 查询 |
| 5 | `GameplayTagExactQueryCompat` | HasTagExact/HasAnyExact/HasAllExact 精确查询 |
| 6 | `GameplayTagNamespaceGlobals` | 命名空间内全局函数（如 `GameplayTags::RequestGameplayTag`） |

## Section 切分方案

| Section | 包含的旧 ID | 主题 |
|---------|-------------|------|
| `RunTagSingleSection` | `GameplayTagCompat` | 单 tag 构造 / 相等 / IsValid / GetTagName / 序列化 |
| `RunTagContainerSection` | `GameplayTagContainerCompat` | 容器 Add/Remove/Num/Contains/AppendTags/Reset/拷贝 |
| `RunTagContainerEmptySection` | `GameplayTagContainerEmptyContracts` | 空容器在所有查询/操作下的契约 |
| `RunTagQuerySection` | `GameplayTagQueryCompat` + `GameplayTagExactQueryCompat` | HasTag/HasAny/HasAll 与对应 Exact 变体 |
| `RunTagNamespaceSection` | `GameplayTagNamespaceGlobals` | 命名空间下的全局函数 |

## Profile 定义

```cpp
const FBindingsCoverageProfile GGameplayTagProfile{
    TEXT("GameplayTag"), TEXT(""), TEXT("ASGameplayTag"),
    TEXT("GameplayTag"), TEXT("GameplayTagBindings"),
};
```

## 分阶段执行计划

### Phase 0 — 前置体检

- [ ] **P0.1** Dump case 清单到 `GameplayTag_CaseInventory.md`，记录所有 `if/return`、`TestEqual`、AddExpectedError，并标注每个 case 依赖哪些预定义 Tag（如 `Test.Foo.Bar`）
  - GameplayTag 测试普遍依赖测试时存在的 tag；如旧 ID 在测试中调用 `UGameplayTagsManager::Get().AddNativeGameplayTag(...)` 注册临时 tag，需在 dump 中明确标记
- [ ] **P0.1** 📦 Git 提交：`[Docs/Plans] Docs: dump gameplay tag bindings case inventory baseline`

- [ ] **P0.2** 检查测试时 `UGameplayTagsManager` 状态共享是否会导致 Section 之间污染
  - 在 dump 文档末尾给出结论：是否需要在每个 Section 顶部 `Reset` Tag manager / 是否所有 Section 共用一组 native tag 即可
  - 这一步决定 P1 各 Section 是否需要 setup/teardown helper
- [ ] **P0.2** 📦 Git 提交：与 P0.1 合并提交

### Phase 1 — Section 实现

- [ ] **P1.1** 实现 `RunTagSingleSection`
  - 覆盖：默认构造为 EmptyTag、`FGameplayTag::RequestGameplayTag(FName)` 已注册 tag 返回有效、未注册返回 EmptyTag、`==`/`!=`、`GetTagName`、`IsValid`、`MatchesTag`/`MatchesAnyTag` 单 tag 行为、ToString 路径
  - 每个分支一个独立 case 函数 + `ExpectGlobalInt` 断言
- [ ] **P1.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild GameplayTag single-tag section`

- [ ] **P1.2** 实现 `RunTagContainerSection`
  - 覆盖：默认构造空容器、AddTag/AddTagFast/AppendTags、RemoveTag/RemoveTags、Num、Contains、Reset、拷贝/赋值/相等
  - 注意脚本侧 `FGameplayTagContainer Container; Container.AddTag(Tag);` 的 by-value 行为
- [ ] **P1.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild GameplayTag container section`

- [ ] **P1.3** 实现 `RunTagContainerEmptySection`
  - 覆盖：空容器调用 `HasTag`/`HasAny`/`HasAll`/`HasTagExact` 等查询的契约（应返回 false 而不是 crash），`First`/`Last` 在空容器上的行为，`IsEmpty` 的 true 路径
  - 这些契约容易回归，每条独立 case
- [ ] **P1.3** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild GameplayTag empty-contracts section`

- [ ] **P1.4** 实现 `RunTagQuerySection`
  - 把 `GameplayTagQueryCompat` 与 `GameplayTagExactQueryCompat` 合并为一个 Section（主题对称：HasXxx vs HasXxxExact）
  - 覆盖矩阵：`HasTag` / `HasAnyTag` / `HasAllTags` / `HasTagExact` / `HasAnyTagExact` / `HasAllTagsExact`，每个 op × {完全匹配 / 前缀匹配 / 不匹配} 三种数据形态 = 18 个 case
- [ ] **P1.4** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild GameplayTag query sections (compat + exact)`

- [ ] **P1.5** 实现 `RunTagNamespaceSection`
  - 覆盖：`GameplayTags::RequestGameplayTag(FName, bool ErrorIfNotFound)` 等命名空间函数；ErrorIfNotFound=true 走异常路径用 `ExecuteFunctionExpectingScriptException` + `AddExpectedError`
  - ErrorIfNotFound=false 路径返回 EmptyTag
- [ ] **P1.5** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild GameplayTag namespace globals section`

### Phase 2 — Automation ID 接线

- [ ] **P2.1** 6 个 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 的 `RunTest` 改写为"建 Profile + 调对应 Section"
  - 删除所有裸 `BuildModule(..., "ASXxx", ...)`、所有 `int Entry()` 字面量、所有 `ExecuteIntFunction + TestEqual(Result, 1)` 套路
  - 保留 6 个 ID 名一字不改
  - 如旧实现中有 `AddExpectedError(TEXT("ASGameplayTagXxx"), ...)`，迁移到新模块名 `ASGameplayTag_<SectionName>` 并保留 expected error 注册
- [ ] **P2.1** 📦 Git 提交：`[Tests/Bindings] Refactor: wire GameplayTag automation IDs to coverage sections`

### Phase 3 — 验证与回归

- [ ] **P3.1** 对位 `GameplayTag_CaseInventory.md`，逐项打勾
- [ ] **P3.1** 📦 Git 提交：`[Docs/Plans] Docs: confirm GameplayTag case inventory coverage`

- [ ] **P3.2** 单文件 6 个 ID 回归全绿
- [ ] **P3.2** 📦 Git 提交：`[Tests/Bindings] Test: gameplay tag subplan single-id regression green`

- [ ] **P3.3** Bindings 整体回归
- [ ] **P3.3** 📦 Git 提交：`[Tests/Bindings] Test: gameplay tag subplan full bindings regression`

## 验收标准

1. `AngelscriptGameplayTagBindingsTests.cpp` 内 `grep "int Entry()"` = 0
2. `grep "return 1[0-9][0-9]"` = 0
3. `grep "BuildModule(.*\"AS"` = 0
4. 6 个原 Automation ID 全部保留且全绿
5. `GameplayTag_CaseInventory.md` 全部打勾
6. `UGameplayTagsManager` 在 Section 前后状态一致（不留临时 native tag 污染后续测试）

## 风险与注意事项

### 风险

1. **Tag manager 全局副作用**：测试可能会注册 native tag，跨 Section 残留。
   - **缓解**：P0.2 已要求评估；如确认有残留，P1 各 Section 在 ModuleScope 析构后做一次 manager 状态校验断言。
2. **测试依赖的 tag 来源**：旧测试可能在 `Config/DefaultGameplayTags.ini` 或 `Bindings/AngelscriptGameplayTagFixture.cpp` 中预置 tag。
   - **缓解**：dump 阶段一并记录依赖来源；新 Section 只引用同一组 tag，不重复注册。

### 已知行为变化

1. `RequestGameplayTag` ErrorIfNotFound=true 路径在新 Section 改用 `ExecuteFunctionExpectingScriptException`，仍需 `AddExpectedError` 注册 expected error 文本（基座如未提供，本 SubPlan 内联 `AddExpectedError`）。
2. 断言数预计从约 30 个增长到 60+ 个。
