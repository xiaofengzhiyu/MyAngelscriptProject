# SubPlan: File & Delegate 测试改造

> 主 Plan：[`../Plan_BindingsTestSuiteRefactor.md`](../Plan_BindingsTestSuiteRefactor.md)  
> 共同规则与基座 API：[`README.md`](./README.md) + [`BaseAPI.md`](./BaseAPI.md)  
> 前置依赖：✅ 基座代码已就绪（`Bindings/Shared/` 5 文件已落地、金丝雀 `Bindings.SharedExample` 通过、Bindings 全量回归 134/134 绿）

## 目标文件与现状

- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`
- 文件大小：23.30 KB
- Automation ID 数：**7 个**
- 旧式 case 规模：**88 个 `if` 分支** / **64 个 `return N;`** / **26 个 `TestEqual+TestTrue`**（实地 grep 数据）
- 特殊性：**双主题混居**（File 与 Delegate 完全不同主题塞在同一文件）—— 不拆文件，但用**双 Profile + 两组 Section runner** 分隔

### 现有 Automation ID 清单

| # | ID | 主题 |
|---|-----|------|
| 1 | `ScriptDelegateCompat` | FScriptDelegate 基本绑定 |
| 2 | `ScriptDelegateExecuteCompat` | FScriptDelegate 执行 / 解绑 |
| 3 | `SoftPathCompat` | FSoftObjectPath 构造 / 比较 |
| 4 | `SoftPathResolveCompat` | FSoftObjectPath 解析 / 加载 |
| 5 | `SourceMetadataCompat` | 源代码元数据访问 |
| 6 | `FileHelperCompat` | FFileHelper 读写 |
| 7 | `DelegateWithPayloadCompat` | Delegate 带 payload 的绑定与执行 |

## Section 切分方案

按主题切两组 Profile，5 个 Section：

### Delegate 组（Profile: `GDelegateProfile`）

| Section | 包含的旧 ID | 主题 |
|---------|-------------|------|
| `RunDelegateBindSection` | `ScriptDelegateCompat` | 绑定 / 解绑 / IsBound / Clear |
| `RunDelegateExecuteSection` | `ScriptDelegateExecuteCompat` | Execute / ExecuteIfBound / 返回值 |
| `RunDelegatePayloadSection` | `DelegateWithPayloadCompat` | 带 payload 的绑定（CreateUFunction with bound args） |

### File 组（Profile: `GFileProfile`）

| Section | 包含的旧 ID | 主题 |
|---------|-------------|------|
| `RunSoftPathSection` | `SoftPathCompat` + `SoftPathResolveCompat` | FSoftObjectPath 构造/比较/解析 |
| `RunFileIOSection` | `SourceMetadataCompat` + `FileHelperCompat` | FFileHelper 读写、源元数据访问 |

> 不拆文件的理由：保留单文件可比性、避免新增空文件；两个 Profile + namespace 隔离即可避免命名冲突。

## Profile 定义

```cpp
namespace {
const FBindingsCoverageProfile GDelegateProfile{
    TEXT("Delegate"), TEXT(""), TEXT("ASDelegate"),
    TEXT("Delegate"), TEXT("DelegateBindings"),
};
const FBindingsCoverageProfile GFileProfile{
    TEXT("File"), TEXT(""), TEXT("ASFile"),
    TEXT("File"), TEXT("FileBindings"),
};
}
```

## 分阶段执行计划

### Phase 0

- [ ] **P0.1** Dump 案例清单到 `FileAndDelegate_CaseInventory.md`
  - 按 Delegate / File 两栏分类
  - File 类如有依赖磁盘临时文件（`FPaths::ProjectSavedDir() / TestXxx.txt` 等），dump 中标注，确保新 Section 用同样的临时路径与清理方式
- [ ] **P0.1** 📦 Git 提交：`[Docs/Plans] Docs: dump file/delegate bindings case inventory baseline`

### Phase 1 — Delegate 组

- [ ] **P1.1** 实现 `RunDelegateBindSection`
  - 覆盖：FScriptDelegate 默认构造（IsBound=false）、BindUFunction 后 IsBound=true、Unbind 后 IsBound=false、Clear 行为
  - **关键**：脚本侧 delegate 绑定通常需要一个目标 UObject + UFunction 名；新 Section 自封装一个最小 receiver UObject（继承 AAngelscriptActor 或 UScriptObject），在 Section 顶部 `NewObject` 创建后供本 Section 内多 case 共享
- [ ] **P1.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Delegate bind section`

- [ ] **P1.2** 实现 `RunDelegateExecuteSection`
  - 覆盖：Execute 后 receiver 状态符合预期、ExecuteIfBound 在未绑定时返回 false 不执行、绑定后再 Unbind 验证不再触发、Execute 返回值（如 delegate 有返回值类型）
  - 把 receiver 的"被调用次数"作为 case 函数返回值，用 `ExpectGlobalInt` 断言
- [ ] **P1.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Delegate execute section`

- [ ] **P1.3** 实现 `RunDelegatePayloadSection`
  - 覆盖：带 payload 的 delegate 绑定（脚本侧 `CreateUFunction(target, name)` 带额外参数）、payload 在 execute 时被正确传入
- [ ] **P1.3** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Delegate payload section`

### Phase 2 — File 组

- [ ] **P2.1** 实现 `RunSoftPathSection`
  - 合并 `SoftPathCompat` 与 `SoftPathResolveCompat`
  - 覆盖：默认构造空、从字符串构造、`==`/`!=`、`ToString`、`IsValid`、`TryLoad`/`ResolveObject`（如能在 EditorContext 下命中已有资产则正路径，否则 nullptr）
- [ ] **P2.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild File SoftPath section`

- [ ] **P2.2** 实现 `RunFileIOSection`
  - 合并 `FileHelperCompat` 与 `SourceMetadataCompat`
  - 覆盖：写文件 → 读回比较、读不存在文件返回失败、`IFileManager` 删除/存在性检查；源元数据查询（如 `GetCurrentScriptFilePath` / `GetCurrentScriptLineNumber` 之类，旧实现里有什么就保什么）
  - **必须**：所有写入路径用 `FPaths::ProjectSavedDir() / "ASTestTemp" / <unique-name>`，Section 顶部清理目录、Section 末尾删除目录（`ON_SCOPE_EXIT`），避免污染开发机
- [ ] **P2.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild File IO section`

### Phase 3 — Automation ID 接线

- [ ] **P3.1** 7 个 ID 的 `RunTest` 全部改写为"对应 Profile + 对应 Section 子集"
  - Delegate 组 3 个 ID 用 `GDelegateProfile`
  - File 组 4 个 ID 用 `GFileProfile`
  - 删除所有旧裸 `BuildModule`、`int Entry()`、`ExecuteIntFunction + TestEqual(Result, 1)`
- [ ] **P3.1** 📦 Git 提交：`[Tests/Bindings] Refactor: wire File/Delegate automation IDs to coverage sections`

### Phase 4 — 验证

- [ ] **P4.1** 对位 dump 打勾
- [ ] **P4.1** 📦 Git 提交：`[Docs/Plans] Docs: confirm File/Delegate case inventory coverage`

- [ ] **P4.2** 单文件 7 ID 回归全绿
- [ ] **P4.2** 📦 Git 提交：`[Tests/Bindings] Test: file/delegate subplan single-id regression green`

- [ ] **P4.3** Bindings 整体回归
- [ ] **P4.3** 📦 Git 提交：`[Tests/Bindings] Test: file/delegate subplan full bindings regression`

## 验收标准

1. `AngelscriptFileAndDelegateBindingsTests.cpp` 内 `grep "int Entry()"` = 0
2. `grep "return 1[0-9][0-9]"` = 0
3. `grep "BuildModule(.*\"AS"` = 0
4. 7 个原 Automation ID 全部保留且全绿
5. 文件内同时存在两个 Profile 实例（`GDelegateProfile` / `GFileProfile`），且每个 ID 准确使用对应 Profile
6. File 组测试运行后磁盘无残留临时文件（`FPaths::ProjectSavedDir() / "ASTestTemp"` 应被清理或不存在）

## 风险与注意事项

### 风险

1. **磁盘 IO 在 CI 上路径权限**：FileHelper 写入路径如选错可能没有写权限。
   - **缓解**：固定使用 `FPaths::ProjectSavedDir()` 而非 `FPaths::EngineSavedDir()` 或绝对路径。
2. **Delegate receiver 的生命周期**：`NewObject` 出来的 receiver 如果没 root 或没引用，可能在 Section 内被 GC。
   - **缓解**：Section 顶部 `AddToRoot` 创建的 receiver，`ON_SCOPE_EXIT` 中 `RemoveFromRoot`。

### 已知行为变化

1. 双 Profile 模式是本 SubPlan 首次落地，可能暴露基座对多 Profile 共存的隐藏假设；如发现，反馈到主 Plan owner 修基座。
2. 临时文件清理在新 Section 是显式 `ON_SCOPE_EXIT`，旧实现可能依赖测试 framework 的 teardown，行为更可控。
