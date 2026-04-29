# SubPlan: Console 测试改造

> 主 Plan：[`../Plan_BindingsTestSuiteRefactor.md`](../Plan_BindingsTestSuiteRefactor.md)  
> 共同规则与基座 API：[`README.md`](./README.md) + [`BaseAPI.md`](./BaseAPI.md)  
> 前置依赖：✅ 基座代码已就绪（`Bindings/Shared/` 5 文件已落地、金丝雀 `Bindings.SharedExample` 通过、Bindings 全量回归 134/134 绿）

## 目标文件与现状

- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp`
- 文件大小：16.19 KB
- Automation ID 数：**5 个**
- 旧式 case 规模：**35 个 `if` 分支** / **16 个 `return N;`** / **11 个 `TestEqual+TestTrue`**（实地 grep 数据）
- 依赖：`IConsoleManager` 全局单例（注册的 CVar/CCommand 会跨 Section 残留）

### 现有 Automation ID 清单

| # | ID | 主题 |
|---|-----|------|
| 1 | `ConsoleVariableCompat` | FConsoleVariable 注册 / Set / Get |
| 2 | `ConsoleVariableExistingCompat` | 已存在 CVar 的查询 |
| 3 | `ConsoleCommandCompat` | FConsoleCommand 注册 / 触发 |
| 4 | `ConsoleCommandReplacementCompat` | 命令替换语义 |
| 5 | `ConsoleCommandSignatureCompat` | 命令签名（参数类型）兼容性 |

## Section 切分方案

| Section | 包含的旧 ID | 主题 |
|---------|-------------|------|
| `RunConsoleVariableSection` | `ConsoleVariableCompat` + `ConsoleVariableExistingCompat` | CVar 注册 / 查询 / Set / Unregister |
| `RunConsoleCommandSection` | `ConsoleCommandCompat` | CCommand 基础注册 / 触发 / Unregister |
| `RunConsoleCommandReplacementSection` | `ConsoleCommandReplacementCompat` | CCommand 替换 / 还原原始命令 |
| `RunConsoleCommandSignatureSection` | `ConsoleCommandSignatureCompat` | CCommand 不同签名（无参 / 字符串数组 / 输出设备） |

## Profile 定义

```cpp
const FBindingsCoverageProfile GConsoleProfile{
    TEXT("Console"), TEXT(""), TEXT("ASConsole"),
    TEXT("Console"), TEXT("ConsoleBindings"),
};
```

## 分阶段执行计划

### Phase 0

- [ ] **P0.1** Dump 案例清单到 `Console_CaseInventory.md`
  - 重点记录每个 case 注册的 CVar/CCommand 名（如 `Test.Foo.Bar`）以及是否在 case 末尾 Unregister
  - 一并标注是否有 `AddExpectedError` 用于异常路径
- [ ] **P0.1** 📦 Git 提交：`[Docs/Plans] Docs: dump console bindings case inventory baseline`

### Phase 1 — Section 实现

- [ ] **P1.1** 实现 `RunConsoleVariableSection`
  - 合并 `ConsoleVariableCompat` + `ConsoleVariableExistingCompat`
  - 覆盖：注册新 CVar、Set/Get（int/float/bool/string）、查询已存在 CVar（如 `r.Vsync`）、Unregister 后查询返回 null
  - **关键**：每个 case 注册的 CVar 必须用本 Section 唯一前缀（如 `as.test.console.var.<caseName>`），且 case 函数返回前自行 Unregister，避免污染下一个 case
- [ ] **P1.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Console variable section`

- [ ] **P1.2** 实现 `RunConsoleCommandSection`
  - 覆盖：注册 CCommand、用 `IConsoleManager::ProcessUserConsoleInput` 触发、验证 receiver 状态、Unregister 后再触发不生效
  - receiver 状态用脚本侧"被调用次数"全局计数器，case 函数返回该计数
- [ ] **P1.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Console command section`

- [ ] **P1.3** 实现 `RunConsoleCommandReplacementSection`
  - 覆盖：替换已有命令 → 触发 → 还原 → 再触发原始命令
- [ ] **P1.3** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Console command replacement section`

- [ ] **P1.4** 实现 `RunConsoleCommandSignatureSection`
  - 覆盖三种签名：无参（`FConsoleCommandDelegate`）、字符串数组（`FConsoleCommandWithArgsDelegate`）、世界 + 输出设备（`FConsoleCommandWithWorldArgsAndOutputDeviceDelegate`）
  - 每种签名一个独立 case 函数
- [ ] **P1.4** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Console command signature section`

### Phase 2 — Section 隔离与接线

- [ ] **P2.1** 增加 `FConsoleManagerScope` 局部 RAII（仅本文件，不上抛基座）
  - 析构时把本 Section 内 `IConsoleManager::RegisterConsoleVariable` / `RegisterConsoleCommand` 注册的所有名字 Unregister
  - 通过包装注册函数 + 维护 `TArray<FString> RegisteredNames` 实现
  - 这一层是 Console 主题专属，不强求基座覆盖；如基座后续有同类需求可由 owner 上提
- [ ] **P2.1** 📦 Git 提交：`[Tests/Bindings] Feat: add ConsoleManagerScope RAII for test isolation`

- [ ] **P2.2** 5 个 ID 接线
- [ ] **P2.2** 📦 Git 提交：`[Tests/Bindings] Refactor: wire Console automation IDs to coverage sections`

### Phase 3 — 验证

- [ ] **P3.1** 对位 dump 打勾，特别确认每个 case 都有 Unregister
- [ ] **P3.1** 📦 Git 提交：`[Docs/Plans] Docs: confirm Console case inventory coverage`

- [ ] **P3.2** 单文件 5 ID 回归全绿
- [ ] **P3.2** 📦 Git 提交：`[Tests/Bindings] Test: console subplan single-id regression green`

- [ ] **P3.3** Bindings 整体回归（重点关注：本 SubPlan 完成后再跑一次，确认没有残留 CVar/CCommand 影响其它 Bindings 测试）
- [ ] **P3.3** 📦 Git 提交：`[Tests/Bindings] Test: console subplan full bindings regression`

## 验收标准

1. `AngelscriptConsoleBindingsTests.cpp` 内 `grep "int Entry()"` = 0
2. `grep "return 1[0-9][0-9]"` = 0
3. `grep "BuildModule(.*\"AS"` = 0
4. 5 个原 Automation ID 全部保留且全绿
5. 测试运行后 `IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(TEXT("as.test.console"), ...)` 命中数 = 0（本 SubPlan 完工后增加一个 P3.x 自检 case 验证）
6. `FConsoleManagerScope` 本文件 RAII 已实现并被所有 Section 使用

## 风险与注意事项

### 风险

1. **CVar/CCommand 全局污染**：Console 主题的最大风险，跨 Section / 跨测试都会残留。
   - **缓解**：P2.1 强制引入本文件 RAII；CaseInventory 阶段确认每个 case 都用唯一前缀。
2. **`ConsoleCommandReplacementCompat` 替换的命令如果原始命令依赖运行时状态**：还原可能失败。
   - **缓解**：选择无副作用的命令做替换实验（如自定义 `as.test.replacement.target`）。

### 已知行为变化

1. 引入 `FConsoleManagerScope` 后，旧实现中可能存在的"漏 Unregister"会被新 Section 自动清理；如发现这导致后续测试行为变化，说明旧测试存在隐式依赖，需要在受影响测试中显式注册依赖的 CVar。
