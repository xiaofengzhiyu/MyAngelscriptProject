# SubPlan: UserWidget 测试改造

> 主 Plan：[`../Plan_BindingsTestSuiteRefactor.md`](../Plan_BindingsTestSuiteRefactor.md)  
> 共同规则与基座 API：[`README.md`](./README.md) + [`BaseAPI.md`](./BaseAPI.md)  
> 前置依赖：✅ 基座代码已就绪（`Bindings/Shared/` 5 文件已落地、金丝雀 `Bindings.SharedExample` 通过、Bindings 全量回归 134/134 绿）
> 推荐作为**上手项**：本 SubPlan 结构最简（仅 2 个 ID），适合第一次接触新基座的执行者

## 目标文件与现状

- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUserWidgetBindingsTests.cpp`
- 文件大小：15.30 KB
- Automation ID 数：**2 个**
- 旧式 case 规模：**39 个 `if` 分支** / **19 个 `return N;`** / **9 个 `TestEqual+TestTrue`**（实地 grep 数据）
- 依赖：`UUserWidget` / `UWidgetTree` / `UPanelWidget`

### 现有 Automation ID 清单

| # | ID | 主题 |
|---|-----|------|
| 1 | `UserWidgetTreeCompat` | UWidgetTree 遍历 / Find / RootWidget 访问 |
| 2 | `UserWidgetTreeErrorPaths` | UWidgetTree 异常路径（空树 / 无效 widget 等） |

> 注：旧文件 15KB 但仅 2 个 ID，说明每段 `int Entry()` 都很长（200+ 行）。改造后预计行数下降 30~50%（取消大脚本 + 拆 case 后整体更紧凑）。

## Section 切分方案

| Section | 包含的旧 ID | 主题 |
|---------|-------------|------|
| `RunWidgetTreeBasicSection` | `UserWidgetTreeCompat` | RootWidget 访问 / FindWidget / ForEachWidget / 添加/移除子 widget / GetParent |
| `RunWidgetTreeErrorSection` | `UserWidgetTreeErrorPaths` | 空树查询 / 无效 widget 操作 / 重复添加 / 越界访问 |

## Profile 定义

```cpp
const FBindingsCoverageProfile GUserWidgetProfile{
    TEXT("UserWidget"), TEXT(""), TEXT("ASUserWidget"),
    TEXT("UserWidget"), TEXT("UserWidgetBindings"),
};
```

## 分阶段执行计划

### Phase 0

- [ ] **P0.1** Dump 案例清单到 `UserWidget_CaseInventory.md`
  - 标注每个 case 是否需要 NewObject UUserWidget / UPanelWidget / UTextBlock 等
  - 标注异常路径的 expected error 文本（如有 AddExpectedError 调用）
- [ ] **P0.1** 📦 Git 提交：`[Docs/Plans] Docs: dump user widget bindings case inventory baseline`

### Phase 1 — Section 实现

- [ ] **P1.1** 实现 `RunWidgetTreeBasicSection`
  - 覆盖：UUserWidget 创建 + 设置 RootWidget、FindWidget by name 命中/未命中、ForEachWidget 遍历计数、添加子 widget 后 GetChildAt 验证、GetParent 一致性
  - widget 创建用基座的 `FCoverageModuleScope` 之外的 helper：在 Section 顶部 `NewObject<UUserWidget>(GetTransientPackage())`，AddToRoot，ON_SCOPE_EXIT 中 RemoveFromRoot
  - 把 widget 操作结果（如 child count、widget name 哈希）作为 case 函数返回值
- [ ] **P1.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild UserWidget tree basic section`

- [ ] **P1.2** 实现 `RunWidgetTreeErrorSection`
  - 覆盖：空 WidgetTree 上调用查询返回 nullptr / 默认值；对未加入 tree 的 widget 调用 GetParent 返回 nullptr；对同一 widget 重复 AddChild 的语义；越界访问 GetChildAt 的容错
  - 必要时使用 `ExecuteFunctionExpectingScriptException`（如有真实抛异常路径）
- [ ] **P1.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild UserWidget tree error section`

### Phase 2 — 接线 + 验证

- [ ] **P2.1** 2 个 ID 接线
- [ ] **P2.1** 📦 Git 提交：`[Tests/Bindings] Refactor: wire UserWidget automation IDs to coverage sections`

- [ ] **P2.2** 对位 dump 打勾
- [ ] **P2.2** 📦 Git 提交：`[Docs/Plans] Docs: confirm UserWidget case inventory coverage`

- [ ] **P2.3** 单文件 2 ID 回归全绿
- [ ] **P2.3** 📦 Git 提交：`[Tests/Bindings] Test: user widget subplan single-id regression green`

- [ ] **P2.4** Bindings 整体回归
- [ ] **P2.4** 📦 Git 提交：`[Tests/Bindings] Test: user widget subplan full bindings regression`

## 验收标准

1. `AngelscriptUserWidgetBindingsTests.cpp` 内 `grep "int Entry()"` = 0
2. `grep "return 1[0-9][0-9]"` = 0
3. `grep "BuildModule(.*\"AS"` = 0
4. 2 个原 Automation ID 全部保留且全绿
5. UUserWidget / 子 widget 在 Section 末尾被正确释放（AddToRoot/RemoveFromRoot 配对，无 GC leak）
6. 改造后文件总行数较 baseline 下降 ≥ 30%（拆 case 后冗余样板减少）

## 风险与注意事项

### 风险

1. **UUserWidget 在 share-clean engine 下的 GC 行为**：widget 树持有大量子对象引用，share-clean 复位时如果 root set 没清理可能 leak。
   - **缓解**：每个 case 函数自包含创建-验证-销毁，不在多个 case 之间共享 widget 实例。
2. **`UWidgetTree::ForEachWidget` 的脚本端可用性**：旧实现是怎么遍历的？
   - **缓解**：dump 阶段记录旧实现遍历路径；如脚本侧没有直接 ForEachWidget，可改为递归 GetChildAt。

### 已知行为变化

1. 改造后单测节点 GC 压力下降（原大段 `int Entry()` 一次创建大量 widget，新模式拆为多个小 case 各自创建少量）。
2. 文件行数下降是预期效果（不再有 200 行的 `int Entry()` 字面量）。
