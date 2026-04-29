# SubPlan: EnhancedInput 测试改造

> 主 Plan：[`../Plan_BindingsTestSuiteRefactor.md`](../Plan_BindingsTestSuiteRefactor.md)  
> 共同规则与基座 API：[`README.md`](./README.md) + [`BaseAPI.md`](./BaseAPI.md)  
> 前置依赖：✅ 基座代码已就绪（`Bindings/Shared/` 5 文件已落地、金丝雀 `Bindings.SharedExample` 通过、Bindings 全量回归 134/134 绿）

## 目标文件与现状

- `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp`
- 文件大小：13.13 KB
- Automation ID 数：**8 个**
- 旧式 case 规模：**20 个 `if` 分支** / **21 个 `return N;`** / **9 个 `TestEqual`**（实地 grep 数据；其中 5 个 ID 是 "*Compiles" 类型，不靠 if-return 分案，所以 IfLines 反而比其他文件少）
- 依赖：`UEnhancedInputComponent` / `ULocalPlayer` / `UInputAction` / `EInputActionValueType`

### 现有 Automation ID 清单

| # | ID | 主题 |
|---|-----|------|
| 1 | `InputActionValueMulAssignCompat` | FInputActionValue 自乘 |
| 2 | `EnhancedInputComponentConstCompat` | UEnhancedInputComponent const 接口 |
| 3 | `InputDebugKeyBindingExecuteCompat` | DebugKey 绑定执行 |
| 4 | `EnhancedInput.InputActionValueConstructorsAndAxisTypes` | InputActionValue 构造与 axis 类型矩阵 |
| 5 | `EnhancedInput.InputActionValueConvertToType` | InputActionValue 类型转换 |
| 6 | `EnhancedInput.EnhancedInputComponentBindActionCompiles` | BindAction 编译路径 |
| 7 | `EnhancedInput.EnhancedInputComponentRemoveBindingCompiles` | RemoveBinding 编译路径 |
| 8 | `EnhancedInput.EnhancedInputComponentEditorDelegateFlags` | Editor delegate 标志 |

> 注意：5-8 号 ID 都带 `EnhancedInput.` 子命名空间前缀，1-3 号没有（`InputActionValue*` / `EnhancedInputComponent*` 直接挂在 `Bindings.` 下）。新 Section 改造**保留 ID 名一字不改**，不要统一前缀。

## Section 切分方案

| Section | 包含的旧 ID | 主题 |
|---------|-------------|------|
| `RunInputActionValueArithSection` | `InputActionValueMulAssignCompat` + `EnhancedInput.InputActionValueConstructorsAndAxisTypes` + `EnhancedInput.InputActionValueConvertToType` | FInputActionValue 构造、自乘、类型转换 |
| `RunEnhancedInputComponentBindingSection` | `EnhancedInputComponentConstCompat` + `InputDebugKeyBindingExecuteCompat` | UEnhancedInputComponent const 接口 + DebugKey 绑定执行 |
| `RunEnhancedInputComponentCompileSection` | `EnhancedInput.EnhancedInputComponentBindActionCompiles` + `EnhancedInput.EnhancedInputComponentRemoveBindingCompiles` | BindAction / RemoveBinding 在脚本端的编译路径 |
| `RunEnhancedInputEditorFlagSection` | `EnhancedInput.EnhancedInputComponentEditorDelegateFlags` | Editor 专属 delegate 标志 |

## Profile 定义

```cpp
const FBindingsCoverageProfile GEnhancedInputProfile{
    TEXT("EnhancedInput"), TEXT(""), TEXT("ASEnhancedInput"),
    TEXT("EnhancedInput"), TEXT("EnhancedInputBindings"),
};
```

## 分阶段执行计划

### Phase 0

- [ ] **P0.1** Dump 案例清单到 `EnhancedInput_CaseInventory.md`
  - 每个 case 标注：依赖的 UInputAction 是测试时构造还是预先存在 / 是否需要 ULocalPlayer / 是否需要 UEnhancedInputComponent NewObject
- [ ] **P0.1** 📦 Git 提交：`[Docs/Plans] Docs: dump enhanced input bindings case inventory baseline`

### Phase 1 — Section 实现

- [ ] **P1.1** 实现 `RunInputActionValueArithSection`
  - 合并 3 个旧 ID 的 case
  - 覆盖：默认构造（值为零）、从 bool/float/Vector2D/Vector3 构造（4 种 axis 类型）、`*=` 自乘、`Get<T>()` 类型转换、`GetValueType()` 返回的枚举
  - axis 矩阵每种类型一个独立 case
- [ ] **P1.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild EnhancedInput action value arith section`

- [ ] **P1.2** 实现 `RunEnhancedInputComponentBindingSection`
  - 合并 `EnhancedInputComponentConstCompat` + `InputDebugKeyBindingExecuteCompat`
  - 覆盖：const UEnhancedInputComponent 上的查询接口（GetActionEventBindings 等返回 const 引用）、DebugKey 绑定 → 触发 → receiver 状态正确
  - DebugKey 触发可能需要 `IConsoleManager::ProcessUserConsoleInput` 或 `EnhancedInputSubsystem::InjectInputForAction`，按旧实现照抄
- [ ] **P1.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild EnhancedInput component binding section`

- [ ] **P1.3** 实现 `RunEnhancedInputComponentCompileSection`
  - 合并 `BindActionCompiles` + `RemoveBindingCompiles`
  - 覆盖："只验证编译路径"的两个 case：脚本侧调用 `Component.BindAction(Action, ETriggerEvent::Triggered, this, n"Handler")` 与 `Component.RemoveBinding(Handle)` 能够编译并不抛运行时异常
  - 这类 "compiles" 测试关注脚本绑定可用性，不强校验运行时副作用，case 函数返回 1 表示编译路径走通即可
- [ ] **P1.3** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild EnhancedInput component compile section`

- [ ] **P1.4** 实现 `RunEnhancedInputEditorFlagSection`
  - 覆盖：Editor 下 delegate 标志位（如 `bExecuteWhenPaused` / `bConsumeInput` 等）的脚本端读写一致性
- [ ] **P1.4** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild EnhancedInput editor flag section`

### Phase 2 — 接线 + 验证

- [ ] **P2.1** 8 个 ID 接线（注意 ID 命名空间前缀差异：1-3 号无 `EnhancedInput.`，4-8 号有）
- [ ] **P2.1** 📦 Git 提交：`[Tests/Bindings] Refactor: wire EnhancedInput automation IDs to coverage sections`

- [ ] **P2.2** 对位 dump 打勾
- [ ] **P2.2** 📦 Git 提交：`[Docs/Plans] Docs: confirm EnhancedInput case inventory coverage`

- [ ] **P2.3** 单文件 8 ID 回归全绿
- [ ] **P2.3** 📦 Git 提交：`[Tests/Bindings] Test: enhanced input subplan single-id regression green`

- [ ] **P2.4** Bindings 整体回归
- [ ] **P2.4** 📦 Git 提交：`[Tests/Bindings] Test: enhanced input subplan full bindings regression`

## 验收标准

1. `AngelscriptEnhancedInputBindingsTests.cpp` 内 `grep "int Entry()"` = 0
2. `grep "return 1[0-9][0-9]"` = 0
3. `grep "BuildModule(.*\"AS"` = 0
4. 8 个原 Automation ID 全部保留且全绿（注意保留 5-8 号的 `EnhancedInput.` 子命名空间前缀）
5. UEnhancedInputComponent / UInputAction / ULocalPlayer 等动态创建的 UObject 在 Section 末尾被正确释放（AddToRoot/RemoveFromRoot 配对）

## 风险与注意事项

### 风险

1. **`InjectInputForAction` 在测试 World 下的可用性**：DebugKey 绑定执行测试可能依赖完整的输入子系统初始化。
   - **缓解**：dump 阶段确认旧实现是怎么触发执行的；如发现旧实现已有 workaround，照搬即可。
2. **`UEnhancedInputComponent` NewObject 需要 outer**：share-clean engine 下 outer 的选择可能影响 GC 行为。
   - **缓解**：用 `GetTransientPackage()` 作为 outer，AddToRoot 后在 ON_SCOPE_EXIT 中 RemoveFromRoot。

### 已知行为变化

1. 8 个 ID 中 4-8 号带 `EnhancedInput.` 子命名空间前缀，新代码必须保留这个层级（不能统一拍平），否则会破坏 `RunTestSuite.ps1` 中可能存在的子前缀匹配。
2. "Compiles" 类 case 行为：旧实现可能只验证脚本编译通过；新 Section 也仅做"调用一次返回 1"，不扩展为运行时副作用断言（保持原意图）。
