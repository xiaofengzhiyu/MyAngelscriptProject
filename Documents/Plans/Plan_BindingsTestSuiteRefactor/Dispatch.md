# 子 Plan 派单表

> 给主 Plan owner 用：决定**给谁、什么时候、什么顺序**派发哪份 SubPlan。

## 状态总览（2026-04-29）

- ✅ **基座已就绪**：`Bindings/Shared/` 5 个文件已落地，金丝雀 `Angelscript.TestModule.Bindings.SharedExample` 通过，Bindings 全量回归 134/134 绿。
- ✅ **BaseAPI 已落锁**：见 [`BaseAPI.md`](./BaseAPI.md)。
- 🟢 **8 份 SubPlan 全部可分发**。

## 推荐派单顺序

按"复杂度递增 + 主题相对独立"的顺序，先派最简单的，让基座在真实改造中得到二次验证。

### 第一波（基座二次验证，建议串行 1 份）

> 让一个谨慎的执行者先做，把任何"基座没料到的边角"暴露出来，反馈回来再批量派后面 7 份。

| 优先级 | SubPlan | 理由 |
|--------|---------|------|
| **🥇 第 1 份** | [`SubPlan_UserWidget.md`](./SubPlan_UserWidget.md) | 仅 2 个 ID，结构最简；UWidget 是 UObject 子类，能验证基座对 UObject 句柄场景的覆盖；39 个 if-case 规模适中 |

**第一波收尾决策**：执行者完成后回看 `BaseAPI.md` 与基座代码，判断是否需要补：
- 基座是否要新增 `ExpectGlobalObject` 等 UObject 返回值的便利 helper？
- `FCoverageModuleScope` 是否要支持"多模块组合"用法？
- 任何反复手写的样板，应当上抛基座。

### 第二波（并行可派 4 份，主题独立）

> 第一波给基座加完补丁后启动。这 4 份主题完全不重叠，并行不会冲突。

| 优先级 | SubPlan | 复杂度 | 派单要点 |
|--------|---------|--------|----------|
| 🥈 第 2 份 | [`SubPlan_Compat.md`](./SubPlan_Compat.md) | ★★★ 80 if / 5 ID | UObject Cast + EditorOnly + Timespan/DateTime；浮点比较是看点，能验证 `ExpectGlobalDouble` |
| 🥈 第 3 份 | [`SubPlan_Console.md`](./SubPlan_Console.md) | ★★★ 35 if / 5 ID | 含本地 RAII (`FConsoleManagerScope`)；CVar 全局污染是看点，可能上抛基座 |
| 🥈 第 4 份 | [`SubPlan_Class.md`](./SubPlan_Class.md) | ★★★★ 83 if / 7 ID | 含 reflective 边界 + 1 条异常路径（`TSubclassOfRejectsUnrelatedClass`）+ 注解 ASClass 编译，能验证 `ExecuteFunctionExpectingScriptException` |
| 🥈 第 5 份 | [`SubPlan_EnhancedInput.md`](./SubPlan_EnhancedInput.md) | ★★★★ 20 if / 8 ID | 含 5 个 "*Compiles" 类型 ID；ID 命名空间前缀差异（部分有 `EnhancedInput.` 子层级）需保留 |

**并行风险**：第 4 份的注解 ASClass 编译路径与第 3 份的 CVar 注册都涉及共享引擎状态，但跑测试时按 ID 隔离运行，互不影响。

### 第三波（高密度 + 双 Profile，建议派给经验丰富的执行者）

| 优先级 | SubPlan | 复杂度 | 派单要点 |
|--------|---------|--------|----------|
| 🥉 第 6 份 | [`SubPlan_FileAndDelegate.md`](./SubPlan_FileAndDelegate.md) | ★★★★ 88 if / 7 ID | 双主题混居，**首次落地双 Profile 模式**；Delegate receiver UObject 生命周期 + 临时文件清理是两个看点 |
| 🥉 第 7 份 | [`SubPlan_Container.md`](./SubPlan_Container.md) | ★★★★★ 88 if / 11 ID | 目录里 ID 最多；含 1 条异常路径 + foreach 在三种容器与嵌套场景下的覆盖 |

### 第四波（最重，单独派给资深执行者）

| 优先级 | SubPlan | 复杂度 | 派单要点 |
|--------|---------|--------|----------|
| 🏁 第 8 份 | [`SubPlan_GameplayTag.md`](./SubPlan_GameplayTag.md) | ★★★★★ **166 if** / 6 ID | **目录里 case 密度最大的文件**；查询矩阵（HasTag × Exact × {完全/前缀/不匹配}）≈ 18 个 case；依赖 GameplayTagsManager 全局状态 |

## 实地数据（执行者可直接抄进 P0 dump 的下界估算）

| SubPlan | Automation ID | `if` 分支 | `return N;` | `TestEqual+TestTrue` | 文件大小 |
|---------|---------------|-----------|-------------|----------------------|----------|
| Container | 11 | 88 | 62 | 17 | 21.4 KB |
| GameplayTag | 6 | **166** | 106 | 18 | 30.0 KB |
| FileAndDelegate | 7 | 88 | 64 | 26 | 23.3 KB |
| Class | 7 | 83 | 46 | 20 | 20.9 KB |
| Compat | 5 | 80 | 56 | 9 | 12.9 KB |
| Console | 5 | 35 | 16 | 11 | 16.2 KB |
| EnhancedInput | 8 | 20 | 21 | 9 | 13.1 KB |
| UserWidget | 2 | 39 | 19 | 9 | 15.3 KB |

**总计**：51 个 Automation ID / 599 个 `if` 分支待重写为独立 case 函数 / 119 个 `TestEqual+TestTrue` 待替换为 `ExpectGlobal*`。

## 派单时给执行者的话术

每次派单建议附带：

> 你接到的是 `SubPlan_<Topic>.md`。开始前请：
>
> 1. 读 `Documents/Plans/Plan_BindingsTestSuiteRefactor/README.md` 了解整体规则。
> 2. 读 `BaseAPI.md` 了解基座 API（已落锁，可直接抄）。
> 3. 在改任何代码前先跑金丝雀确认本地基座可用：
>    ```powershell
>    Tools\RunTests.ps1 -TestPrefix Angelscript.TestModule.Bindings.SharedExample -Label canary -TimeoutMs 600000
>    ```
> 4. 跑你即将改造的文件的 baseline 拿到现有断言数（用于改造后下界对比）：
>    ```powershell
>    Tools\RunTests.ps1 -TestPrefix Angelscript.TestModule.Bindings.<原 ID 名> -Label baseline -TimeoutMs 600000
>    ```
> 5. 然后按 SubPlan 的 Phase 顺序执行，每个任务编号一个 commit。
>
> **TimeoutMs 上限是 900000**，写命令时不要超过。

## 验收交回时检查清单

执行者交回 SubPlan 时，主 Plan owner 需要确认：

- [ ] 该文件 `grep "int Entry()"` 命中 = 0
- [ ] 该文件 `grep "return 1[0-9][0-9]"` 命中 = 0
- [ ] 该文件 `grep 'BuildModule(.*"AS'` 命中 = 0
- [ ] `<Topic>_CaseInventory.md` 已就位且每条都打勾
- [ ] 该文件涉及的所有原 Automation ID 都跑过且全绿
- [ ] Bindings 全量回归相对 baseline 0 新增失败
- [ ] PR 中 commit 数与 SubPlan 任务编号数一致（每个 P 编号一 commit）

## 所有 SubPlan 完工后

主 Plan owner 负责：

1. 更新 `Plan_OpportunityIndex.md` 第二节"测试增强"，登记本 Plan 完成。
2. 把主 Plan `Plan_BindingsTestSuiteRefactor.md` 顶部加 `归档状态 / 归档日期 / 完成判断 / 结果摘要`。
3. 移入 `Documents/Plans/Archives/`（伴侣目录可一并移入或保留作为子 Plan 归档）。
4. 同步 `Archives/README.md`。
