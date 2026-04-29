# SubPlan: Compat 测试改造

> 主 Plan：[`../Plan_BindingsTestSuiteRefactor.md`](../Plan_BindingsTestSuiteRefactor.md)  
> 共同规则与基座 API：[`README.md`](./README.md) + [`BaseAPI.md`](./BaseAPI.md)  
> 前置依赖：✅ 基座代码已就绪（`Bindings/Shared/` 5 文件已落地、金丝雀 `Bindings.SharedExample` 通过、Bindings 全量回归 134/134 绿）

## 目标文件与现状

- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp`
- 文件大小：12.85 KB
- Automation ID 数：**5 个**
- 旧式 case 规模：**80 个 `if` 分支** / **56 个 `return N;`** / **9 个 `TestEqual+TestTrue`**（实地 grep 数据）
- 主题：UObject Cast / EditorOnly 标志 / Timespan / DateTime 兼容性矩阵

### 现有 Automation ID 清单

| # | ID | 主题 |
|---|-----|------|
| 1 | `ObjectCastCompat` | Cast<T>(UObject) 行为 |
| 2 | `ObjectEditorOnlyCompat` | EditorOnly 标记访问 |
| 3 | `ObjectEditorOnlyParity` | EditorOnly 在脚本与 C++ 端一致性 |
| 4 | `TimespanCompat` | FTimespan 算术 / 构造 / 比较 |
| 5 | `DateTimeCompat` | FDateTime 算术 / 构造 / 比较 |

## Section 切分方案

| Section | 包含的旧 ID | 主题 |
|---------|-------------|------|
| `RunObjectCastSection` | `ObjectCastCompat` | Cast 命中 / 未命中 / nullptr 输入 |
| `RunObjectEditorOnlySection` | `ObjectEditorOnlyCompat` + `ObjectEditorOnlyParity` | EditorOnly 标记访问 + 与 C++ 端 parity 矩阵 |
| `RunTimespanSection` | `TimespanCompat` | FTimespan 全部算术与比较 |
| `RunDateTimeSection` | `DateTimeCompat` | FDateTime 全部算术与比较 |

## Profile 定义

```cpp
const FBindingsCoverageProfile GCompatProfile{
    TEXT("Compat"), TEXT(""), TEXT("ASCompat"),
    TEXT("Compat"), TEXT("CompatBindings"),
};
```

## 分阶段执行计划

### Phase 0

- [ ] **P0.1** Dump 案例清单到 `Compat_CaseInventory.md`
- [ ] **P0.1** 📦 Git 提交：`[Docs/Plans] Docs: dump compat bindings case inventory baseline`

### Phase 1 — Section 实现

- [ ] **P1.1** 实现 `RunObjectCastSection`
  - 覆盖：Cast 命中（`UTexture2D` cast 到 `UObject` 再 cast 回）/ 未命中（`AActor` cast 到 `UTexture2D` 返回 null）/ nullptr 输入返回 null / `IsValid` 配合 Cast
  - 用 `ExpectGlobalInt` 把"cast 成功"/"cast 失败/null"映射为 1/0
- [ ] **P1.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Compat ObjectCast section`

- [ ] **P1.2** 实现 `RunObjectEditorOnlySection`
  - 合并 `ObjectEditorOnlyCompat` + `ObjectEditorOnlyParity`
  - 覆盖：EditorOnly UProperty 在脚本侧访问、与 C++ 直接读取的值一致性、EditorOnly UFunction 的可调用性
- [ ] **P1.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Compat EditorOnly section`

- [ ] **P1.3** 实现 `RunTimespanSection`
  - 覆盖：默认构造 == 0、从 ticks/seconds/minutes/hours/days 构造、`+`/`-`/`*`/`/`、`==`/`!=`/`<`/`>`、ToString
  - 用 `ExpectGlobalDouble`（基座新增）做浮点比较
- [ ] **P1.3** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Compat Timespan section`

- [ ] **P1.4** 实现 `RunDateTimeSection`
  - 覆盖：FromYear/Month/Day/Hour/Min/Sec 构造、`+ FTimespan` / `- FTimespan` / `- FDateTime` / 比较、`Now`（如旧实现有，注意时区/CI 稳定性，可改用固定常量值断言）
- [ ] **P1.4** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Compat DateTime section`

### Phase 2 — 接线 + 验证

- [ ] **P2.1** 5 个 ID 接线
- [ ] **P2.1** 📦 Git 提交：`[Tests/Bindings] Refactor: wire Compat automation IDs to coverage sections`

- [ ] **P2.2** 对位 dump 打勾
- [ ] **P2.2** 📦 Git 提交：`[Docs/Plans] Docs: confirm Compat case inventory coverage`

- [ ] **P2.3** 单文件 5 ID 回归全绿
- [ ] **P2.3** 📦 Git 提交：`[Tests/Bindings] Test: compat subplan single-id regression green`

- [ ] **P2.4** Bindings 整体回归
- [ ] **P2.4** 📦 Git 提交：`[Tests/Bindings] Test: compat subplan full bindings regression`

## 验收标准

1. `AngelscriptCompatBindingsTests.cpp` 内 `grep "int Entry()"` = 0
2. `grep "return 1[0-9][0-9]"` = 0
3. `grep "BuildModule(.*\"AS"` = 0
4. 5 个原 Automation ID 全部保留且全绿
5. Timespan/DateTime 浮点比较使用 `ExpectGlobalDouble` 或显式 tolerance，不依赖 `==` 直接断言

## 风险与注意事项

### 风险

1. **`FDateTime::Now` 的 CI 不稳定**：如旧实现使用 Now 做 round-trip，新 Section 可改用固定常量构造避免时区/时钟漂移。
   - **缓解**：dump 阶段标注是否依赖 Now，新 Section 改用 `FDateTime(2026, 4, 28, 22, 30, 0)` 等固定值。

### 已知行为变化

1. EditorOnly 测试在 non-editor build 不会被运行（`EditorContext` flag 已限定），新 Section 行为一致。
