# SubPlan: Console 测试改造

> 主 Plan：[`../Plan_BindingsTestSuiteRefactor.md`](../Plan_BindingsTestSuiteRefactor.md)  
> 共同规则与基座 API：[`README.md`](./README.md) + [`BaseAPI.md`](./BaseAPI.md)  
> 前置依赖：✅ 基座代码已就绪（`Bindings/Shared/` 5 文件已落地、金丝雀 `Bindings.SharedExample` 通过、Bindings 全量回归 134/134 绿）
> 执行范围：本次合并主 Console 文件与 4 个 Console 周边小文件，保留所有既有 Automation ID。
> 当前执行状态：✅ 实现已落地并完成 fresh 验证；📦 Git 提交切点未执行，按仓库规则保留未勾选。

## 目标文件与现状

- 主实现文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp`
- 新增共享声明：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsSections.h`
- 薄壳调用文件：
  - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleCommandArgumentBindingsTests.cpp`
  - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleCommandErrorBindingsTests.cpp`
  - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleCommandLifecycleBindingsTests.cpp`
  - `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleVariableIdentityTests.cpp`
- Baseline：`Angelscript.TestModule.Bindings.Console` 前缀当前 **10/10 PASS**
- 依赖：`IConsoleManager` 全局单例，注册的 CVar/CCommand 会跨 Section / 跨 Automation ID 残留，必须用本文件 RAII 清理。

### 现有 Automation ID 清单

| # | ID | 来源文件 | 主题 |
|---|-----|----------|------|
| 1 | `ConsoleVariableCompat` | `AngelscriptConsoleBindingsTests.cpp` | FConsoleVariable 注册 / Set / Get |
| 2 | `ConsoleVariableExistingCompat` | `AngelscriptConsoleBindingsTests.cpp` | 已存在 CVar 的查询与复用 |
| 3 | `ConsoleCommandCompat` | `AngelscriptConsoleBindingsTests.cpp` | FConsoleCommand 注册 / 触发 / 卸载 |
| 4 | `ConsoleCommandReplacementCompat` | `AngelscriptConsoleBindingsTests.cpp` | 命令替换语义 |
| 5 | `ConsoleCommandSignatureCompat` | `AngelscriptConsoleBindingsTests.cpp` | 错误 handler 签名异常路径 |
| 6 | `ConsoleCommandArgumentMarshalling.EmptyArgsMarker` | `AngelscriptConsoleCommandArgumentBindingsTests.cpp` | 空参数数组转发 |
| 7 | `ConsoleCommandArgumentMarshalling.ContentAndOrder` | `AngelscriptConsoleCommandArgumentBindingsTests.cpp` | 参数内容与顺序转发 |
| 8 | `ConsoleCommandMissingHandlerCompat` | `AngelscriptConsoleCommandErrorBindingsTests.cpp` | 缺失 handler 异常路径 |
| 9 | `ConsoleCommandLifecycleOriginalReplacementUnload` | `AngelscriptConsoleCommandLifecycleBindingsTests.cpp` | 原始/替换模块卸载生命周期 |
| 10 | `ConsoleVariableExistingIdentityCompat` | `AngelscriptConsoleVariableIdentityTests.cpp` | 复用已有 CVar 时保留 identity/help/flags |

## Section 切分方案

| Section | 包含的旧 ID | 主题 |
|---------|-------------|------|
| `RunConsoleVariableTypesSection` | `ConsoleVariableCompat` | int/float/bool/string CVar 默认值、Set/Get、native final value |
| `RunConsoleVariableExistingSection` | `ConsoleVariableExistingCompat` | 复用已有 native CVar 并写回最终值 |
| `RunConsoleVariableIdentitySection` | `ConsoleVariableExistingIdentityCompat` | 已有 native CVar 的指针 identity、help、flags 保留 |
| `RunConsoleCommandBasicSection` | `ConsoleCommandCompat` | 命令注册、参数数量写回、模块卸载后 unregister |
| `RunConsoleCommandArgumentEmptySection` | `ConsoleCommandArgumentMarshalling.EmptyArgsMarker` | 空参数数组写入 `<empty>` |
| `RunConsoleCommandArgumentContentSection` | `ConsoleCommandArgumentMarshalling.ContentAndOrder` | 参数内容与顺序写入 `One|Two Words|Three=Value` |
| `RunConsoleCommandReplacementSection` | `ConsoleCommandReplacementCompat` | 替换命令覆盖原始命令，替换模块卸载后移除 |
| `RunConsoleCommandLifecycleSection` | `ConsoleCommandLifecycleOriginalReplacementUnload` | 原始模块卸载不移除替换命令，替换模块卸载移除命令 |
| `RunConsoleCommandMissingHandlerSection` | `ConsoleCommandMissingHandlerCompat` | 缺失 handler 抛异常且不注册命令 |
| `RunConsoleCommandWrongSignatureSection` | `ConsoleCommandSignatureCompat` | handler 签名错误抛异常且不注册命令 |
| `RunConsoleLeakSelfCheckSection` | 新增 `Console.LeakSelfCheck` | 验证 `as.test.console` 前缀无残留对象 |

## Profile 定义

```cpp
const FBindingsCoverageProfile GConsoleProfile{
    TEXT("Console"), TEXT(""), TEXT("ASConsole"),
    TEXT("Console"), TEXT("ConsoleBindings"),
};
```

## 分阶段执行计划

> 注：📦 Git 提交项是执行切点记录；本轮未创建 commit，因此保持未勾选。

### Phase 0 — Baseline 与清单

- [x] **P0.1** 跑 `Angelscript.TestModule.Bindings.Console` baseline
  - 在独立 worktree 内先完成 bootstrap/build，再运行 Console 前缀测试，确认改造前 10 个既有 Console 相关 ID 全绿。
- [ ] **P0.1** 📦 Git 提交：`[Tests/Bindings] Test: capture Console baseline before refactor`

- [x] **P0.2** Dump 案例清单到 `Console_CaseInventory.md`
  - 记录 5 个目标文件中的每个旧 `int Entry()` 分支、native post-check、expected error、模块名、注册对象名前缀和 cleanup 预期。
  - 清单覆盖范围为 10 个旧 ID，而不是早期草案中的 5 个主文件 ID。
- [ ] **P0.2** 📦 Git 提交：`[Docs/Plans] Docs: dump console bindings case inventory baseline`

### Phase 1 — Console 专属基座与声明

- [x] **P1.1** 新增 `AngelscriptConsoleBindingsSections.h`
  - 声明所有 `RunConsole*Section` 函数，让 4 个周边文件只保留 Automation ID 注册与薄壳调用，不再复制旧 helper。
  - header 不包含实现细节，只暴露 `FAutomationTestBase&`、`FAngelscriptEngine&`、`const FBindingsCoverageProfile&` 三参数 section 接口。
- [ ] **P1.1** 📦 Git 提交：`[Tests/Bindings] Feat: add Console section declarations`

- [x] **P1.2** 在主实现文件内增加 `FConsoleManagerScope`
  - 所有测试对象名统一走 `as.test.console.<Section>.<Guid>` 前缀。
  - Scope 负责生成并跟踪 CVar/CCommand 名，提供 native register/unregister、command execute、CVar value verify、prefix leak verify helper。
  - 析构按反向顺序 unregister 已跟踪名字；每个 Section 结束必须调用 prefix leak verify。
- [ ] **P1.2** 📦 Git 提交：`[Tests/Bindings] Feat: add ConsoleManagerScope RAII for test isolation`

### Phase 2 — 主 Console 文件改造

- [x] **P2.1** 实现 `RunConsoleVariableTypesSection`
  - 用 `FCoverageModuleScope` 构建 `ASConsole_VariableTypes`，脚本拆成独立 no-arg case 函数，不再使用 `int Entry()` 总入口。
  - 覆盖 int/float/bool/string 的默认读、Set 后读；C++ 侧继续验证 `IConsoleManager` 中最终值为 `42` / `3.25f` / `false` / `UpdatedValue`。
- [ ] **P2.1** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Console variable type section`

- [x] **P2.2** 实现 `RunConsoleVariableExistingSection`
  - Native 预注册 int CVar 初始值 `7`，脚本构造 `FConsoleVariable(Name, 99, ...)` 时必须复用旧对象。
  - 覆盖脚本读到 `7`、写到 `21`，C++ 侧验证 native final value 为 `21`。
- [ ] **P2.2** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Console existing variable section`

- [x] **P2.3** 实现 `RunConsoleCommandBasicSection`
  - 脚本声明 `const FConsoleCommand Command(Name, n"OnCommand")`，handler 签名固定为 `void OnCommand(const TArray<FString>& Args)`。
  - C++ 侧执行命令并传入 3 个参数，验证 output CVar 写入 `3`；销毁 module 后验证 command 不存在。
- [ ] **P2.3** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Console command basic section`

- [x] **P2.4** 实现 `RunConsoleCommandReplacementSection`
  - 先构建 original module 写入 marker `11`，再构建 replacement module 写入 marker `22`。
  - 执行命令后必须观察 replacement marker；销毁 replacement module 后 command 必须不存在。
- [ ] **P2.4** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Console command replacement section`

- [x] **P2.5** 实现 `RunConsoleCommandWrongSignatureSection`
  - 保留 expected error：`Global function for console command must have signature`、新模块名、脚本调用行。
  - 使用 `ExecuteFunctionExpectingScriptException` 验证 Prepare 成功、执行异常、异常文本匹配、行号 > 0，并验证 command 未注册。
- [ ] **P2.5** 📦 Git 提交：`[Tests/Bindings] Refactor: rebuild Console wrong-signature section`

### Phase 3 — 周边 Console 文件合并

- [x] **P3.1** 实现变量 identity Section 并改薄壳
  - `RunConsoleVariableIdentitySection` 保留 pointer identity、help 文本、persistent flags、`ECVF_Cheat` 与 final value `21` 验证。
  - `AngelscriptConsoleVariableIdentityTests.cpp` 只保留 include、Automation ID 和调用该 Section 的 `RunTest`。
- [ ] **P3.1** 📦 Git 提交：`[Tests/Bindings] Refactor: merge Console variable identity section`

- [x] **P3.2** 实现参数转发 Sections 并改薄壳
  - `RunConsoleCommandArgumentEmptySection` 覆盖空参数输出 `<empty>`。
  - `RunConsoleCommandArgumentContentSection` 覆盖参数输出 `One|Two Words|Three=Value`。
  - `AngelscriptConsoleCommandArgumentBindingsTests.cpp` 只保留两个 Automation ID 的薄壳调用。
- [ ] **P3.2** 📦 Git 提交：`[Tests/Bindings] Refactor: merge Console command argument sections`

- [x] **P3.3** 实现缺失 handler Section 并改薄壳
  - 保留 expected error：`Could not find global function 'MissingHandler' to bind as console command.`、新模块名、脚本调用行。
  - 验证异常路径不留下 native command。
- [ ] **P3.3** 📦 Git 提交：`[Tests/Bindings] Refactor: merge Console missing-handler section`

- [x] **P3.4** 实现 lifecycle Section 并改薄壳
  - 保留 original marker `11`、replacement marker `22`、original unload 后 replacement 仍可执行、replacement unload 后 command 缺失的完整生命周期。
  - `AngelscriptConsoleCommandLifecycleBindingsTests.cpp` 只保留 Automation ID 薄壳调用。
- [ ] **P3.4** 📦 Git 提交：`[Tests/Bindings] Refactor: merge Console command lifecycle section`

### Phase 4 — 接线、清单 closure 与验证

- [x] **P4.1** 10 个旧 ID 接线，并新增 `Console.LeakSelfCheck`
  - 所有 ID 保留原路径；新增自检 ID 只检查 `as.test.console` 前缀残留数为 0。
  - 主文件和 4 个薄壳文件内 `int Entry()` / 裸 `BuildModule` 必须全部清零。
- [ ] **P4.1** 📦 Git 提交：`[Tests/Bindings] Refactor: wire Console automation IDs to coverage sections`

- [x] **P4.2** 对位 `Console_CaseInventory.md` 打勾
  - 每条旧脚本分支、native post-check、expected error、cleanup 检查都有新 Section 对应项。
- [ ] **P4.2** 📦 Git 提交：`[Docs/Plans] Docs: confirm Console case inventory coverage`

- [x] **P4.3** 单主题回归
  - 命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Bindings.Console" -Label console-refactor -TimeoutMs 600000`
  - 期望：11/11 PASS（10 个旧 ID + 1 个新增 leak self-check）。
  - 实测：`console-final`，11/11 PASS，failed=0。
- [ ] **P4.3** 📦 Git 提交：`[Tests/Bindings] Test: console subplan single-prefix regression green`

- [x] **P4.4** Bindings 整体回归
  - 命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\RunTests.ps1 -TestPrefix "Angelscript.TestModule.Bindings" -Label console-bindings -TimeoutMs 900000`
  - 期望：不引入新失败。
  - 实测：`console-bindings`，succeeded=135，succeededWithWarnings=3，failed=0，notRun=0。
- [ ] **P4.4** 📦 Git 提交：`[Tests/Bindings] Test: console subplan full bindings regression`

## 验收标准

1. 5 个 Console 相关测试源文件内 `grep "int Entry()"` = 0。
2. 5 个 Console 相关测试源文件内 `grep "BuildModule("` = 0（全部经 `FCoverageModuleScope`）。
3. 10 个原 Automation ID 全部保留且全绿。
4. 新增 `Angelscript.TestModule.Bindings.Console.LeakSelfCheck` 全绿。
5. 测试运行后 `IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(TEXT("as.test.console"), ...)` 命中数 = 0。
6. `FConsoleManagerScope` 本文件 RAII 已实现并被所有 Section 使用。
7. `ConsoleCommandSignatureCompat` 仍是错误签名异常路径；正向参数覆盖由两个 `ConsoleCommandArgumentMarshalling.*` ID 承担。

## 风险与注意事项

### 风险

1. **CVar/CCommand 全局污染**：Console 主题的最大风险，跨 Section / 跨测试都会残留。
   - **缓解**：所有新测试名使用 `as.test.console` 前缀，所有注册名都交给 `FConsoleManagerScope` 追踪并清理，新增 leak self-check 做终局验证。
2. **命令替换与 unload 顺序敏感**：`FScriptConsoleCommand` 析构只在当前 registered object 仍等于自身 command 指针时 unregister。
   - **缓解**：replacement / lifecycle Section 明确覆盖“卸载 original 不移除 replacement、卸载 replacement 才移除 command”。
3. **expected error 行号变化**：从 `int Entry()` 改为独立函数后，日志中的行号文本会变化。
   - **缓解**：保留关键错误文本和模块名 expected error；行号 expected error 只匹配新函数声明和稳定子串，避免硬编码旧 `int Entry()`。

### 已知行为变化

1. 旧实现中可能漏清理或依赖 `Angelscript.Test.*` 随机前缀；新实现统一改为 `as.test.console.*` 并在 Section 退出时强制清理。
2. 4 个周边 Console 文件不再拥有私有 helper 和裸 `BuildModule` 实现，只作为 Automation ID 注册与 Section 调用薄壳存在。
3. 按仓库规则，本次实现不自动创建 git commit；提交项保留在文档中作为执行粒度记录。
