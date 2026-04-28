# 测试基础设施改进计划

## 背景与目标

### 背景

`AngelscriptTest` 已经具备较大规模的自动化覆盖，但本轮五维输入反复指向的不是“单个功能没测到”，而是**测试基础设施本身仍在泄漏状态、混淆 owner、弱化诊断**。当前最集中的问题有五类：

1. `SHARE` 路径只安装 `FAngelscriptEngineScope`，不会回收具名模块，导致 compile-and-execute 测试在同一进程内持续积累脚本模块、生成类和 diagnostics。
2. `FullDestroy` 核心用例通过 `DiscardSavedStack()` 直接丢弃进入测试前的 context stack，full-engine 独占期与 outer scope 恢复没有 owner 边界。
3. `BindConfig` 套件同时修改全局 bind registry、bind state、静态 recorder 和 `Angelscript` 日志级别，却缺少统一的 snapshot/restore fixture。
4. `CompileScriptModule()`、`GetFunctionByDecl()`、`FindFunctionByDecl()` 与执行 helper 之间存在重复 scope、隐式名字回退和“只走慢路径”的 contract 偏差，导致 helper 语义与真实运行路径脱节。
5. debugger 测试仍依赖外部 debuggable production engine，monitor/client 对连接、ready、callstack 和 stop reason 的契约不完整，失败路径容易退化成模糊 timeout 或后台线程残留。

如果不先收口这些基础设施缺陷，再继续推进 `Documents/Plans/Plan_TestCoverageExpansion.md` 中的大量新增测试，新增 coverage 仍会建在不稳定的 fixture 之上，难以成为可靠守门网。

### 目标

1. 让 shared / full-destroy / bind-config / debugger 这四条高频测试路径在退出后都能恢复进入前基线，不再留下模块、context stack、bind state、静态 recorder、日志级别或 monitor 线程残留。
2. 固定公共 helper 的 owner contract：谁负责压栈、谁允许 declaration fallback、谁负责 dispatch 路径选择，避免 helper 通过隐式语义制造假绿或排障噪音。
3. 为每条基础设施改动补齐对应的 `Angelscript.TestModule.*` 自测，让后续任何 helper 演进都先撞到 fixture regression，而不是先污染业务测试。
4. 明确与现有活跃 Plan 的边界：本 Plan 只处理 `AngelscriptTest` 的 helper / fixture / cleanup / diagnostics / operator surface，不重复测试命名、suite 入口、engine-local runtime state 或大范围功能 coverage 扩展。

## 范围与边界

### 范围内

- `Plugins/Angelscript/Source/AngelscriptTest/Shared/`
- `Plugins/Angelscript/Source/AngelscriptTest/Core/`
- `Plugins/Angelscript/Source/AngelscriptTest/Compiler/`
- `Plugins/Angelscript/Source/AngelscriptTest/Debugger/`
- 为测试 fixture 必要新增的 runtime seam，例如 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.*`
- `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` / `TESTING_GUIDE_ZH.md` 等测试基础设施文档

### 范围外

- `Documents/Plans/Plan_TestModuleStandardization.md` 已覆盖的命名、目录、suite 入口规范化
- `Documents/Plans/Plan_TestSystemNormalization.md` 已覆盖的执行脚本、测试层级与文档标准化
- `Documents/Plans/Plan_TestEngineIsolation.md` 已覆盖的 runtime engine-local state 主线
- `Documents/Plans/Plan_TestCoverageExpansion.md` 已覆盖的功能域新增测试扩张
- 单纯针对某个 runtime/editor 功能的业务修复；本 Plan 只处理测试基础设施 owner 与 contract

### 源码验证边界

本 Plan 的问题纳入以 `Plugins/Angelscript/Source/AngelscriptTest/` 下实际源码为准；即使实施时需要补充 runtime seam，也必须先在测试源码中确认当前缺陷仍然存在。

## 分析来源

### 直接转化为本轮条目的输入

| 分析文档 | 关键发现 |
|---------|---------|
| `Documents/AutoPlans/RuntimeCore_Analysis.md` | `D-23` 指出测试 helper 只覆盖 `RuntimeCallEvent` 慢路径；`D-01` 指出 `ContextStack` teardown 合同缺少回归保护。 |
| `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` | `Issue-1`、`Issue-4`、`Issue-52`、`Issue-62`、`Issue-64`、`Issue-65`、`Issue-66` 直接给出 full-destroy、shared cleanup、debugger、自定义 bind fixture 的缺陷与修复方向。 |
| `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` | `Issue-02`、`Issue-14`、`Issue-17`、`Issue-59` 与 `Issue-13` 指出 production/shared engine 污染、bind-state 清空、verbosity 泄漏与 full-destroy cleanup 断言不足。 |
| `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` | `Issue-4` 和 `Issue-21` 指出 `CompilerPipeline` 的 shared 污染与 `ModuleFunctionInspection` 对 declaration 的错误 helper 依赖。 |
| `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` | `Issue-1` / `Issue-10` 证明“生产或共享 engine + 临时模块不回收”并非局部问题，而是跨目录重复出现的基础设施模式。 |
| `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` | `Issue-1`、`Issue-3`、`Issue-4`、`Issue-19`、`Issue-22`、`Issue-23` 共同指向 debugger monitor/client 的生命周期与诊断合同薄弱。 |
| `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` | `Issue-22` / `Issue-23` 说明 `BindConfig` 现有覆盖只抽查理想化样本，未把真实 FunctionLibrary 的 class-map 与 metadata 行为钉成合同。 |
| `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` | `Arch-MS-48` 提醒 runtime-capable fixtures 与 `Editor` host type 错位；本轮不单独立项，但作为 Phase 完成后的后续结构方向保留。 |
| `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` | `D9` 明确当前优势是 `repo-owned runner + machine-readable artifact + 轻量内存脚本夹具`，因此应补强 cleanup/fixture 契约，而不是放弃现有轻夹具路线。 |

### 已读取并用于优先级筛查的输入

| 分析文档 | 本轮处理方式 |
|---------|---------|
| `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` | 已读取；当前问题更多是单文件 cleanup / assertion 强化，优先级低于通用 fixture 泄漏。 |
| `Documents/AutoPlans/TestCoverage/LanguageFeatures_TestGaps.md` | 已读取；主要是语言用例断言不足，适合在基础设施稳定后并入 coverage 扩展。 |
| `Documents/AutoPlans/TestCoverage/EditorAndTools_TestGaps.md` | 已读取；当前更偏 editor-specific seam 与越界保护，不先于 shared/debugger/bind state 问题。 |

## 分阶段执行计划

### Phase 1：先切断跨测试污染与全局状态泄漏

- [ ] **P1.1** 为 shared compile 路径引入 tracked module cleanup fixture
  - 当前 `ASTEST_BEGIN_SHARE` 只负责安装 `FAngelscriptEngineScope`，不会回收具名模块；`CompilerPipeline`、`AngelscriptFunctionTests` 等文件因此把 shared singleton 当成长期 module registry 使用。本项要把“共享只读”与“共享但自动 cleanup”拆开，让 compile-and-execute 用例退出后恢复干净模块基线。
  - 先在 `Shared` 层增加 tracked share fixture 或等价宏，统一记录 `BuildModule()`、`CompileAnnotatedModuleFromMemory()`、plain compile helper 成功生成的模块名；再迁移 `Compiler` / `Angelscript` 目录里最典型的 shared compile 路径，删除双重 `ASTEST_CREATE_ENGINE_SHARE()` 与无 owner 的裸 module 生命周期。
  - 这项改动只收口 fixture contract，不改已有 runner/suite 规则；迁移时要保留一条显式“只读共享”入口，避免误伤确实只做 type lookup、不写模块状态的测试。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-4`：`ASTEST_BEGIN_SHARE` 族宏不会回收测试模块，导致 shared 用例在同一进程内持续累积状态"
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`Issue-4`：`CompilerPipeline` 全文件使用 `SHARE` 引擎但从不清理"
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-02`：`EngineParityTests` 直接污染共享 production engine，既绕过 `FAngelscriptEngineScope` 也不清理临时模块"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-1`：`ClassGenerator.EmptyModuleSetup` 复用生产/共享引擎且未回收临时模块"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9`：应保持 repo-owned runner 与轻量内存脚本夹具，但要补强 cleanup contract"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` L97-L104 仅创建 `FAngelscriptEngineScope`、没有任何 `DiscardModule()`；`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` L18-L24、L92-L95、L171-L174、L231-L234 等 8 处都在 `ASTEST_BEGIN_SHARE` 下编译具名模块；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` L24-L34、L47-L57、L70-L80 在 shared singleton 上直接 `ASTEST_COMPILE_RUN_INT(...)`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`
- [ ] **P1.1** 📦 Git 提交：`[TestInfrastructure] Refactor: add tracked shared module cleanup fixture`

- [ ] **P1.1-T** 单元测试：为 tracked share fixture 建立 shared-cleanup 自测
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedModuleFixtureTests.cpp`
  - 测试场景：1. 同一模块名连续两次 compile-run 时，第二次仍从空 module 基线开始；2. fixture 退出后 `GetModuleByModuleName()` 与 raw AS module lookup 都返回空；3. annotated compile 退出后旧 `GeneratedClass`/delegate 壳对象不再可解析。
  - 测试命名：`Angelscript.TestModule.Shared.Modules.TrackedShareCleansCompiledModule`、`Angelscript.TestModule.Shared.Modules.TrackedShareCleansAnnotatedModule`
- [ ] **P1.1-T** 📦 Git 提交：`[TestInfrastructure] Test: verify tracked shared module cleanup`

- [ ] **P1.2** 以 owner-aware fixture 重写 full-destroy 核心用例，禁止测试体手动丢弃外层 context stack
  - 当前 `FCoreTestContextStackGuard` 会在构造时清空整个 `ContextStack`，而 3 条 full-destroy 用例随后调用 `DiscardSavedStack()`，等价于直接抹掉进入测试前的 ambient engine 解析。本项要把“独占 full-engine epoch”与“是否恢复 outer scope”拆成明确 fixture 语义。
  - 实施时应新增专门的 full-destroy isolation fixture：进入时保存 stack snapshot 与 shared/global engine baseline，离开时只销毁 test-owned engine，并恢复测试前已存在的 outer current engine；同时把 annotated recreate 的 cleanup 断言升级成弱引用和 generated symbol 失效合同。
  - 迁移后应删除 `AngelscriptEngineCoreTests.cpp` 内三份复制粘贴的 setup/teardown 模板代码，让 full-engine 生命周期只通过 fixture 进入，避免继续在测试体里直接调用 `DiscardSavedStack()`。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-1`：Full-destroy 核心用例会丢弃进入测试前的引擎上下文栈"
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — "`D-01`：teardown 后没有回归检查 `ContextStack` 是否恢复基线，残留不会被自动化发现"
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-13`：`FullDestroyAllowsAnnotatedRecreate` 只验证第二轮能重新生成类，没有证明第一轮类/包已被真正回收"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` L19-L24 定义 `FCoreTestContextStackGuard` 并暴露 `DiscardSavedStack()`；L158-L165、L194-L201、L266-L273 的三条 full-destroy 用例都会在清理 shared/global engine 后调用 `ContextGuard.DiscardSavedStack()`；L282-L287 只在第二轮 annotated compile 前重新建 scope，没有任何旧类弱引用验证。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`
- [ ] **P1.2** 📦 Git 提交：`[TestInfrastructure] Refactor: add owner-aware full destroy isolation fixture`

- [ ] **P1.2-T** 单元测试：补齐 outer scope 恢复与 annotated artifact 回收回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFullDestroyFixtureTests.cpp`
  - 测试场景：1. 进入 full-destroy fixture 前先建立 outer `FAngelscriptEngineScope`，退出后 `TryGetCurrentEngine()` 必须恢复到 outer engine；2. fixture 只销毁 test-owned full/shared engine，不误清 foreign outer scope；3. annotated recreate 第一轮生成的 `UClass`/outer package 弱引用在 teardown 后失效。
  - 测试命名：`Angelscript.TestModule.Shared.FullDestroy.RestoresOuterCurrentEngine`、`Angelscript.TestModule.Shared.FullDestroy.KeepsForeignOuterScope`、`Angelscript.TestModule.Shared.FullDestroy.CleansAnnotatedArtifacts`
- [ ] **P1.2-T** 📦 Git 提交：`[TestInfrastructure] Test: verify full destroy isolation fixture`

- [ ] **P1.3** 为 `BindConfig` 建立 scoped mutation fixture，统一收口 bind 注册、bind state、recorder 与 log verbosity
  - `AngelscriptBindConfigTests.cpp` 当前同时向全局 bind registry 追加 GUID bind、直接 `ResetBindState()` 清空进程级缓存、把 recorder 记到静态 `TMap`，并在 helper 内无条件把 `Angelscript` 日志级别改成 `Fatal/Log`。这些全局改动都没有统一 restore contract，导致 bind 相关测试在同一 Editor 进程内越跑越脏。
  - 本项需要把 mutation-heavy 路径拆成几类 scoped fixture：临时 bind 注册、bind-state snapshot/restore、scoped recorder、scoped verbosity override。随后将 `BindConfig` 前半组 disabled-bind 测试与后半组覆盖/metadata 测试迁移到统一 fixture，并把真实 FunctionLibrary 代表合同从“大而全”的 megafile 中拆到 dedicated 测试文件。
  - 目标不是一次性重做全部 bind tests，而是先让“修改全局 bind 状态”的测试具备可逆边界，再顺手把 `FunctionLibrary` 的 class-map / metadata 代表样本补到 fixture 自测里，避免继续只测理想化 coverage library。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-64` / `Issue-65` / `Issue-66`：动态 bind 注册、`ResetBindState()` 和 `FBindExecutionRecorder` 都缺少 scoped owner"
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-17`：前半组 `BindConfig` 用例持续向全局 bind registry 注入 GUID bind；`Issue-14`：多条用例直接清空全局 bind state；`Issue-59`：`ExecuteIsolatedBinds` 把日志级别硬切到 `Fatal/Log` 却不恢复"
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — "`Issue-22` / `Issue-23`：`BindConfig` 只抽查理想化样本，没有覆盖真实 FunctionLibrary 的 class-map 与 world-context metadata"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` L55-L77 的 `FBindExecutionRecorder` 只有 `Reset/Increment/Get`、没有删除接口；L118-L123 的 `ExecuteIsolatedBinds()` 固定执行 `Fatal -> CallBinds -> Log`；L272-L304、L334-L364、L372-L416 通过 `FAngelscriptBinds::FBind` 注册唯一 bind 并依赖 GUID 名称；L501-L505、L557-L560、L612-L615、L655-L658、L714-L717、L762-L765、L810-L813 反复 `ResetBindState()`；L512-L517 只抽查引擎原生 callable，未覆盖 FunctionLibrary class-map。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P1.3** 📦 Git 提交：`[TestInfrastructure] Refactor: scope bind registration and bind state mutations`

- [ ] **P1.3-T** 单元测试：为 bind mutation fixture 建立 restore 合同与 FunctionLibrary 代表样本
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryTests.cpp`
  - 测试场景：1. fixture 退出后 `GetAllRegisteredBindNames()` 数量恢复基线，旧测试 bind 名不会泄漏到下一条用例；2. `ClassFuncMaps` / class DB / `PreviouslyBound*` 等 bind-state 快照在退出后恢复；3. recorder 容器大小与 `Angelscript` 日志级别恢复进入前值；4. 代表性 FunctionLibrary 条目如 `USceneComponent::GetRelativeLocation`、`UWidget::GetRenderTransform`、`ULevelStreaming::GetShouldBeVisibleInEditor` 进入正确宿主 class map，且 metadata/world-context trait 符合预期。
  - 测试命名：`Angelscript.TestModule.Engine.BindConfig.Fixture.RestoresRegisteredBinds`、`Angelscript.TestModule.Engine.BindConfig.Fixture.RestoresBindState`、`Angelscript.TestModule.Engine.BindConfig.Fixture.RestoresRecorderAndVerbosity`、`Angelscript.TestModule.Engine.BindConfig.FunctionLibraryMetadata.RepresentativeEntries`
- [ ] **P1.3-T** 📦 Git 提交：`[TestInfrastructure] Test: verify bind fixture restore contracts`

### Phase 2：统一 helper 语义并补齐 debugger operator contract

- [ ] **P2.1** 收敛 `CompileScriptModule()` / declaration resolver / 执行 helper 的 owner contract
  - 当前 scenario helper、declaration helper 与执行 helper 在三个方向上都存在隐式语义：`CompileScriptModule()` 无条件重复压入 `FAngelscriptEngineScope`；`GetFunctionByDecl()` / `FindFunctionByDecl()` 在 declaration 失败后自动退到同名函数；执行 helper 则只走 `RuntimeCallEvent` 慢路径，没有显式覆盖 optimized dispatch contract。
  - 本项需要把 helper contract 拆开：1. compile orchestration 与 current-engine scope 明确只由一层 owner 管理；2. declaration lookup 分为 exact-only 与 explicit fallback 两类入口，诊断里输出可用 declaration；3. 执行 helper 公开慢路径与 optimized-path 的测试入口，而不是让业务测试默认只覆盖 `RuntimeCallEvent`。
  - 迁移时优先处理 `CompileScriptModule()` 及其直接驱动的 scenario 入口，再收口 `ModuleFunctionInspection` 所依赖的 declaration lookup；optimized dispatch 可先通过 shared helper 暴露测试 harness，不要求业务测试立即全量切换。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-62`：`CompileScriptModule()` 在公共 helper 层重复压入同一 `FAngelscriptEngineScope`"
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`Issue-21`：`ModuleFunctionInspection` 用名字回退 helper 冒充声明检查"
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — "`D-23`：现有自动化调用脚本函数只走 `RuntimeCallEvent`，没有覆盖 `OptimizedCall_*` 的线程合同"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h` L22-L31 的 `CompileScriptModule()` 一进入就构造 `FAngelscriptEngineScope EngineScope(Engine)`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` L596-L620 的 `GetFunctionByDecl()` 在 declaration 失败后回退到 `GetFunctionByName()`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` L230-L257 的 `FindFunctionByDecl()` 复制了同样的名字回退逻辑，L441-L460 对 `UASFunction` 固定走 `RuntimeCallEvent`；`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` L283-L309 的 `ModuleFunctionInspection` 正在使用这套 helper。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioHelperTests.cpp`（新建）
- [ ] **P2.1** 📦 Git 提交：`[TestInfrastructure] Refactor: unify scenario helper and declaration contracts`

- [ ] **P2.1-T** 单元测试：为 exact decl、scope 深度与 dispatch contract 建立 helper regression
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioHelperTests.cpp`
  - 测试场景：1. exact declaration lookup 在只有同名重载时必须失败，并输出可用 declarations；2. explicit fallback helper 仅在调用方声明允许时按名字回退；3. outer `ASTEST_BEGIN_SHARE_CLEAN` 与 outer explicit `FAngelscriptEngineScope` 两种场景下，`CompileScriptModule()` 只增加一层 stack；4. 同一脚本函数通过 slow-path 与 optimized-path harness 执行时，线程边界与返回值合同一致。
  - 测试命名：`Angelscript.TestModule.Shared.Helpers.ResolveFunctionByDeclarationExact`、`Angelscript.TestModule.Shared.Helpers.ResolveFunctionByDeclarationExplicitFallback`、`Angelscript.TestModule.Shared.Helpers.CompileScriptModuleDoesNotDoublePushScope`、`Angelscript.TestModule.Shared.Helpers.DispatchContractsExposeOptimizedPath`
- [ ] **P2.1-T** 📦 Git 提交：`[TestInfrastructure] Test: verify scenario helper contracts`

- [ ] **P2.2** 硬化 debugger monitor/test-client 基础设施，并让默认 debugger 回归可自举
  - debugger 测试当前有两层问题叠在一起：一是 `Smoke` / `Breakpoint` / `Stepping` 默认依赖外部 debuggable production engine，不具备自举能力；二是 monitor/client 对 connect timeout、ready 判定、callstack 等待、stop reason 与失败路径 join 没有统一 contract，导致负向场景容易把“监控自己坏了”误报成“目标逻辑没命中”。
  - 本项要引入 test-owned debugger fixture 与 RAII monitor owner：默认路径自建可调试 engine、端口和 session；`Connect()` 提供显式 timeout；ready gate 必须在宣布 ready 前确认 future 未提前失败；monitor 结果统一暴露 `Error` / `bTimedOut` / `HasStopped` / `HasContinued` / callstack 信息；`StepOver` / `StepOut` 补齐 stop reason 与首个 caller breakpoint 合同。
  - 这样可以保持当前 `repo-owned runner + 轻量 fixture` 路线，而不是把 debugger 回归退回到环境偶然满足时才可运行的“半手工 smoke”。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-52`：debugger 自测与 smoke/breakpoint/stepping 用例直接依赖外部 debuggable production engine，测试套件无法自举"
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-1` / `Issue-4` / `Issue-19` / `Issue-22` / `Issue-23`：monitor 生命周期、连接、ready、stop reason 与 callstack timeout helper 合同薄弱"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9`：当前优势是 repo-owned runner + 轻量 fixture，宜补强 operator contract，而非继续依赖外部环境偶然满足"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` L32-L73 的 `Connect()` 把 `SE_EWOULDBLOCK` / `SE_EINPROGRESS` 直接当作成功、没有等待连接建立；L160-L217 的等待 helper 只在超时后设置统一错误，不区分 callstack 专项失败。`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` L166-L277 的 monitor 线程失败路径会先 `bMonitorReady = true` 再返回，L563-L566 与 L647-L649 的负向断言只看 `StopEnvelopes.Num() == 0`。`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` L172-L303 的 step monitor 同样以布尔值宣布 ready，L521-L549 与 L623-L651 的 `StepOver` / `StepOut` 只检查 callstack 行号和栈深，不检查 `Reason`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`
- [ ] **P2.2** 📦 Git 提交：`[DebuggerTest] Refactor: harden debugger monitor and client fixtures`

- [ ] **P2.2-T** 单元测试：为 owned debugger fixture、monitor lifecycle 与 stop reason 建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerMonitorFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`
  - 测试场景：1. 无外部 production debugger engine 时，owned fixture 仍可自建并完成 handshake；2. connect timeout 与 ready gate 提前失败会给出明确错误，不再伪装成 monitor ready；3. 负向 breakpoint 场景除 `0 stop` 外，还必须满足 `Error.IsEmpty()` 且 `!bTimedOut`；4. `StepOver` / `StepOut` 校验 `Reason`、首个 caller breakpoint 行和 callstack frame depth。
  - 测试命名：`Angelscript.TestModule.Debugger.Monitor.OwnedFixtureSelfBootstraps`、`Angelscript.TestModule.Debugger.Monitor.ConnectTimeoutReportsFailure`、`Angelscript.TestModule.Debugger.Monitor.ReadyGateRejectsEarlyFailure`、`Angelscript.TestModule.Debugger.Breakpoint.ClearThenResumeRequiresHealthyMonitor`、`Angelscript.TestModule.Debugger.Stepping.StepOverReasonContract`、`Angelscript.TestModule.Debugger.Stepping.StepOutCallerBreakpointContract`
- [ ] **P2.2-T** 📦 Git 提交：`[DebuggerTest] Test: verify monitor lifecycle and stop reason contracts`

## 单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.1` shared tracked cleanup | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedModuleFixtureTests.cpp` | 同名模块重复 compile-run、annotated symbol cleanup、module lookup 归零 | P0 |
| `P1.2` full-destroy isolation | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFullDestroyFixtureTests.cpp` | outer current engine 恢复、foreign scope 保留、annotated artifact 弱引用失效 | P0 |
| `P1.3` bind mutation fixture | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigFunctionLibraryTests.cpp` | bind 注册/状态/recorder/verbosity 恢复、FunctionLibrary class-map 与 metadata 样本 | P0 |
| `P2.1` helper contract 收口 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioHelperTests.cpp` | exact decl、explicit fallback、stack depth、slow/optimized dispatch contract | P1 |
| `P2.2` debugger monitor/operator contract | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerMonitorFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` | owned fixture 自举、connect timeout、ready gate、negative no-stop、stop reason | P1 |

## 验收标准

1. `SHARE` compile 路径新增 tracked cleanup 后，代表性 `Compiler` / `Angelscript` compile-run 用例在同一进程内连续执行两轮，模块数、generated symbol lookup 与执行结果都保持稳定。
2. full-destroy 核心用例退出后，如果进入前存在 outer `FAngelscriptEngineScope`，`TryGetCurrentEngine()` 必须恢复到原 engine；annotated recreate 还必须显式证明第一轮类与 package 已被回收。
3. `BindConfig` mutation-heavy 用例退出后，bind 注册数量、bind-state 快照、recorder 容器大小与 `Angelscript` 日志级别都恢复为进入前值，不再依赖 GUID 名称或二次清空掩盖泄漏。
4. `CompileScriptModule()`、declaration resolver 与执行 helper 的 contract 被显式拆开：outer scope 下 stack 深度不再虚增，exact decl 与 fallback 行为可区分，slow/optimized dispatch 均有回归入口。
5. debugger 默认回归不再要求外部 debuggable production engine；negative breakpoint 与 step 用例能明确区分 connect/handshake/timeout/stop reason 的不同失败模式。
6. 本 Plan 落地后，不需要重复改动 `Plan_TestModuleStandardization`、`Plan_TestSystemNormalization`、`Plan_TestEngineIsolation` 或 `Plan_TestCoverageExpansion` 才能解释上述行为变化，说明边界已经收口清楚。

## 风险与注意事项

### 风险

1. **shared cleanup 语义切换风险**：少数历史用例可能隐式依赖“前一断言留下的同名模块仍可见”。迁移时必须先区分“只读共享”和“共享但自动 cleanup”两种入口，避免把旧偶然行为继续写成 contract。
2. **bind-state snapshot 覆盖不完整风险**：如果 test-only seam 漏掉 `PreviouslyBound*`、class DB 或排序缓存，fixture 退出后仍可能恢复成“半旧半新”的 bind state。实现时需要把 `FAngelscriptBindState` 视为整体快照。
3. **debugger fixture 端口与 teardown 风险**：自建 owned debugger fixture 会引入端口分配、session shutdown 和 socket wait；若 RAII monitor 没有统一 `Wait/Get`，很容易把新 helper 也做成 flaky 源。
4. **helper contract 收紧暴露历史假覆盖风险**：exact declaration 与 single-scope 规则落地后，现有依赖名字回退或隐式重复压栈的业务测试会被迫显式声明自己依赖的 contract，短期内可能带来一批真实红灯。
5. **结构性 follow-up 暂缓风险**：`Arch-MS-48` 指出的 runtime-capable fixtures / `Editor` host type 错位仍然存在。本轮先收口 correctness 与 cleanup；若 Phase 1/2 完成后仍受 host type 限制，再单独立项处理 `AngelscriptTestFixtures` 模块拆分，不在本 Plan 内硬塞架构搬迁。

### 已知行为变化

1. `SHARE` 不再默认等于“共享模块状态也可跨测试复用”；compile-and-execute 用例会优先迁移到 tracked share 或 clean share 入口。
2. full-destroy 测试不再允许通过 `DiscardSavedStack()` 直接抹掉外层 `ContextStack`；outer current engine 恢复将成为 fixture 自带合同。
3. `BindConfig` 相关测试不再假定日志级别最终一定回到 `Log`，而是恢复进入前 verbosity；这会改变部分失败路径的日志噪音基线。
4. `GetFunctionByDecl()` / `FindFunctionByDecl()` 的隐式名字回退会被拆成显式 API；依赖“同名即可”的旧用例需要改为显式 fallback helper。
5. debugger 默认 smoke / breakpoint / stepping 回归将从“附着外部 production engine”转成“默认自建 owned fixture，环境附着场景单独标注”，失败信息也会从模糊 timeout 变成具体 connect / ready / stop reason 错误。

---

## 深化 (2026-04-09)

### 与活跃 Plan 的去重边界

- `Documents/Plans/Plan_TestEngineIsolation.md` 已承接 `FAngelscriptTestFixture`、clone/full containment 与 engine-local state 主线；以下补充只处理 `AngelscriptTest` 内部 helper / fixture / support owner，不重复 engine-isolation 主线。
- `Documents/Plans/Plan_TestModuleStandardization.md` 与 `Documents/Plans/Plan_TestSystemNormalization.md` 已承接命名、目录和文档规范；以下补充不再重复纯命名整改，只收口 owner、cleanup、host type 与 white-box support surface。

### Phase 1 补充：把 shared reset 从“尽量清理”收紧成可证明的 teardown contract

- [ ] **P1.4** 强化 `ResetSharedCloneEngine()` 的 generated symbol cleanup，并把 helper 自测切到显式 fresh baseline
  - 当前 shared reset 只会丢弃 active/raw modules，再遍历 detached `UASClass` 去 root/standalone；同一文件的 debug 统计已经单独记录 `DetachedASFunctions`，但 reset 主路径并不会清理这些 `UASFunction` 壳对象。与此同时，`ResetSharedEngineReleasesGeneratedComponentClasses` 在 `HostActor`/`Component` 仍活着时直接执行 reset，最基础的 helper 自测也普遍复用 ambient shared engine，只靠单个 `DiscardModule()` 自证“自己没污染”。
  - 这一项要把 shared reset contract 拆成三层：1. reset 本体显式处理 detached `UASFunction` 与 generated symbol 残留；2. reset 前新增 live generated instance guard，禁止在 world/component 仍引用 generated class/function 时静默 `CollectGarbage()`；3. helper 自测统一改走 `FAngelscriptEngineHelperSelfTestFixture` 一类 fresh-baseline 入口，进入前后都检查 active/raw modules 与 target generated symbols，而不是继续借用 ambient shared singleton。
  - 具体落点优先保持在 `Shared`：把 `ResetSharedCloneEngine()` 的生成对象清理、live-instance 探针与 baseline probe 封成同一套 helper，再迁移 `CompileModuleFromMemory`、`ExecuteIntFunction`、`GeneratedSymbolLookup`、`FailedAnnotatedModuleDoesNotPolluteLaterCompiles` 与 `ResetSharedEngineReleasesGeneratedComponentClasses` 这批 guard-value 最高的 regression。避免在业务测试里继续散落“先跑 world，再手动 reset shared engine”的 teardown 顺序。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-2`：`ResetSharedCloneEngine()` 未回收 detached `UASFunction`；debug 统计与真正 cleanup contract 已分叉"
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-67` / `Issue-68`：shared reset 仍会在 live generated component/world 场景执行，helper 自测继续复用脏 shared engine"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-1`：`ClassGenerator.EmptyModuleSetup` 直接复用 production/shared engine 且没有 clean baseline"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：当前路线的优势是 repo-owned runner + 轻量内存脚本夹具，应补强 cleanup contract，而不是继续容忍 ambient shared state"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` L207-L269 的 `ResetSharedCloneEngine()` 只清理 module 与 detached `UASClass`，没有任何 `UASFunction` 处理；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` L391-L418 在 `HostActor` / `Component` 仍存活时直接调用 `ResetSharedCloneEngine(Engine)`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` L23-L27、L46-L50、L161-L200 的基础 helper regression 仍统一从 `GetOrCreateSharedCloneEngine()` 起步，只做单模块 discard。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P1.4** 📦 Git 提交：`[TestInfrastructure] Refactor: harden shared reset and helper self-test baselines`

- [ ] **P1.4-T** 单元测试：为 shared reset 的 function cleanup、live-instance guard 与 fresh baseline 建立自测
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedResetFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`
  - 测试场景：1. 编译带 generated function 的 annotated 模块后执行 reset，旧 `UASFunction` 壳对象与 `FindGeneratedFunction()` 都必须归零；2. world/component 仍存活时尝试 reset，应给出稳定 failure contract 或明确诊断，而不是继续 silent GC；3. helper 自测 fixture 进入前后 `ActiveModules`、raw AS modules、target generated symbol 查找都恢复到基线。
  - 测试命名：`Angelscript.TestModule.Shared.Reset.CleansDetachedGeneratedFunctions`、`Angelscript.TestModule.Shared.Reset.DetectsLiveGeneratedInstances`、`Angelscript.TestModule.Shared.EngineHelper.SelfTestsUseFreshBaseline`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.4-T** 📦 Git 提交：`[TestInfrastructure] Test: verify shared reset cleanup and fresh baselines`

### Phase 2 补充：把 file-backed fixture 与当前引擎 owner 收口成统一入口

- [ ] **P2.3** 为 `Preprocessor` / `SourceNavigation` 建立 `file-backed fixture + explicit engine scope` 组合入口
  - 当前 `Preprocessor` 三条基础用例都会先偷用 production/shared engine，再把脚本写进固定的 `Saved/Automation/PreprocessorFixtures/...`，但既没有 `FAngelscriptEngineScope`，也没有唯一目录和 teardown；`SourceNavigation.Functions` 则在 production-like engine 上固定使用 `RuntimeFunctionNavigationTest` / `RuntimeFunctionNavigationTest.as`，退出时只 `DiscardModule()`，不删磁盘脚本。两组用例都在“需要真实文件”的前提下，把 engine owner、module owner 与 file owner 拆成了互不绑定的临时变量。
  - 本项要新增一套共享 `FAngelscriptFileBackedTestFixture`：构造时统一生成唯一 `ModuleName` / `RelativeScriptFilename` / `Saved/Automation` 子目录，并立即建立 `FAngelscriptEngineScope`；析构时统一恢复被 guard 的 engine 配置、丢弃 module、删除脚本文件和目录。这样 disk-backed 测试不再各自手写 `WriteFixtureFile()`、`TGuardValue<bool>`、`DiscardModule()` 与 `DeleteDirectory`。
  - 迁移顺序优先从最薄弱的两组用例开始：`Preprocessor` 用例把 `TGuardValue<bool> AutomaticImportGuard` 挪进 scope 内，并用 fixture 回收 `Saved/Automation/PreprocessorFixtures`；`SourceNavigation.Functions` 则改成由 fixture 派发唯一模块名与脚本路径，断言 source metadata 时不再依赖固定文件名或历史残留文件。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`Issue-3` / `Issue-23`：`Preprocessor` 复用 production/shared engine 且未建立 `FAngelscriptEngineScope`，`TGuardValue` 不保证 `Preprocess()` 读到同一台 engine"
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`Issue-8`：`WriteFixtureFile()` 向 `Saved/Automation/PreprocessorFixtures` 写真实脚本却从不清理"
    - [C] `Documents/AutoPlans/TestCoverage/EditorAndTools_TestGaps.md` — "`Issue-20` / `Issue-24`：`SourceNavigation.Functions` 落盘临时脚本却不删文件，还在 production-like engine 上复用固定模块名/文件名"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：当前测试 operator surface 的优势在 repo-owned runner + 轻量夹具；对必须落盘的场景，应补 deterministic owner，而不是继续把历史残留留给下一轮运行"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` L13-L20 通过 `GetEngineForPreprocessorTests()` 偷用 production/shared engine，L28-L33 的 `WriteFixtureFile()` 只 `MakeDirectory + SaveStringToFile`，L76-L199 三条用例都未建立 `FAngelscriptEngineScope`；`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` L23-L29 直接拿 production-like engine，L41-L46 固定 `RuntimeFunctionNavigationTest` / `RuntimeFunctionNavigationTest.as`，teardown 只有 `DiscardModule()`、没有任何磁盘清理。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedTestFixture.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedTestFixture.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`
- [ ] **P2.3** 📦 Git 提交：`[TestInfrastructure] Refactor: add file-backed fixture with scoped engine ownership`

- [ ] **P2.3-T** 单元测试：为 file-backed fixture 的 scope/cleanup contract 建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`
  - 测试场景：1. fixture 进入时把目标 engine 压入 current-engine scope，退出后恢复 `bUseAutomaticImportMethod` 与当前 engine；2. 连续两次创建 file-backed fixture 时，`ModuleName` / `RelativeScriptFilename` 不冲突，且旧脚本文件已从 `Saved/Automation` 清掉；3. `SourceNavigation.Functions` 仍能保留正确 `SourceFilePath` / line number，但不再依赖固定脚本名与残留文件。
  - 测试命名：`Angelscript.TestModule.Shared.FileFixture.RestoresCurrentEngineAndImportMode`、`Angelscript.TestModule.Shared.FileFixture.CleansSavedAutomationScripts`、`Angelscript.TestModule.Editor.SourceNavigation.Functions.UsesUniqueCleanedScriptFixture`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.3-T** 📦 Git 提交：`[TestInfrastructure] Test: verify file-backed fixture scope and cleanup`

### Phase 3：把 runtime-capable fixture 与 white-box support 从 editor test shell 中分层

- [ ] **P3.1** 拆出 `runtime-capable fixtures + test-support façade`，收口 `AngelscriptTest` 的 accidental public surface
  - 当前 `AngelscriptTest.Build.cs` 把整个模块根目录公开给外部，并把 `AngelscriptRuntime` 置于 public deps；`Shared/AngelscriptTestUtilities.h` 又直接 include `Preprocessor/AngelscriptPreprocessor.h`、`ClassGenerator/ASClass.h` 与 raw `source/as_*` internals。与此同时，`UAngelscriptNativeScriptTestObject`、`UAngelscriptNativeParent/ChildInterface`、`UAngelscriptUhtCoverageTestLibrary` 这类其实可以在 runtime/headless 侧复用的 fixture 类型都仍塞在同一个 editor test shell 里。
  - 本项不重复 `Plan_TestEngineIsolation` 的 engine-local fixture API，只处理 owner/host 边界：新增 `AngelscriptTestFixtures` 承接 runtime-capable UHT/native fixture 类型，新增 `AngelscriptTestSupport` 或 `AngelscriptRuntimeInternalAccess` 承接 white-box façade；现有 `AngelscriptTest` 保留 editor automation/cases，并改为 private 依赖这些新 owner，而不是继续把整棵测试树与 runtime internals 一并公开。
  - 第一阶段优先迁最典型的三类资产：1. `Shared/AngelscriptNativeScriptTestObject.h` 的 runtime object fixture；2. `Shared/AngelscriptNativeInterfaceTestTypes.h` 的 interface fixture；3. `Core/AngelscriptUhtCoverageTestTypes.h` 的 UHT coverage library。并同步把 `Shared/AngelscriptTestUtilities.h` 这种“测试模块共享头”改为 include 受控 façade，而不是直接穿透到 `Preprocessor`、`ClassGenerator` 与 `source/as_*`。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-43`：`AngelscriptTest` 更像 automation harness，却仍按可复用库模块公开依赖；white-box access 直接穿透 runtime internals"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-48`：`AngelscriptTest` 被固定为 `Editor` 模块，但大量 fixture 实际上是 runtime-capable"
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`Issue-23`：`Preprocessor` 用例拿到了 `Engine*` 却没有稳定 current-engine owner，暴露出 shared support API 仍缺统一入口"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Deep` / `D9-Operator`：当前 AS 已有 repo-owned runner；参考 `UnLuaTestSuite` 一类 opt-in test plugin / leaf owner 模式，更适合把 runtime-capable fixture 从 editor shell 中解耦"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs` L12-L31 公开整个模块根与 `AngelscriptRuntime` public dep；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` L15-L21 直接 include `Preprocessor/AngelscriptPreprocessor.h`、`ClassGenerator/ASClass.h` 与 raw `source/as_*` internals；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeScriptTestObject.h` L17-L27、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeInterfaceTestTypes.h` L8-L42、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h` L9-L38 说明 runtime-capable fixture 类型确实仍位于 `AngelscriptTest` 模块内部。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeScriptTestObject.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeInterfaceTestTypes.h`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h`、`Plugins/Angelscript/Angelscript.uplugin`，以及新增的 `Plugins/Angelscript/Source/AngelscriptTestFixtures/*` / `Plugins/Angelscript/Source/AngelscriptTestSupport/*`
- [ ] **P3.1** 📦 Git 提交：`[TestInfrastructure] Refactor: split runtime-capable fixtures from editor test shell`

- [ ] **P3.1-T** 单元测试：为 fixture/support owner 拆分后的可见性与 metadata 保真建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTestFixtures/Private/Tests/AngelscriptFixtureSupportContractTests.cpp`
  - 测试场景：1. `UAngelscriptNativeScriptTestObject`、`UAngelscriptNativeParentInterface` / `UAngelscriptNativeChildInterface` 在迁移后仍能被 UHT、反射与脚本 fixture 正确发现；2. `UAngelscriptUhtCoverageTestLibrary` 的 `InternalCallableWithOverride`、`RequiresWorldContext`、`CallableWithoutWorldContext` metadata 与 script-visible trait 保持不变；3. editor-side regression 继续只经由 support façade 访问 white-box capability，不再要求普通 consumer 直接 include raw `source/as_*` 或 `ClassGenerator/*`。
  - 测试命名：`Angelscript.TestModule.Fixtures.NativeObject.UhtVisibility`、`Angelscript.TestModule.Fixtures.NativeInterface.UhtVisibility`、`Angelscript.TestModule.Fixtures.CoverageLibrary.MetadataParity`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.1-T** 📦 Git 提交：`[TestInfrastructure] Test: verify fixture support owner contracts`

### 本轮补充测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.4` shared reset hardening | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedResetFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` | detached `UASFunction` cleanup、live generated instance guard、helper self-test fresh baseline | P1 |
| `P2.3` file-backed fixture contract | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` | current-engine scope restore、unique script path、saved file cleanup、source-navigation metadata 保真 | P1 |
| `P3.1` fixture/support owner split | `Plugins/Angelscript/Source/AngelscriptTestFixtures/Private/Tests/AngelscriptFixtureSupportContractTests.cpp` | fixture type UHT/反射可见性、coverage library metadata parity、support façade 稳定入口 | P2 |

### 本轮补充风险

1. **shared reset 收紧后会暴露历史假设**：一批旧 regression 可能隐式依赖“reset 时 world 里仍可挂着 generated instance”或“旧 `UASFunction` 壳对象还能被查到”；新增 guard 会把这些历史偶然行为直接打红。
2. **file-backed fixture 改成唯一脚本名后会改变字面路径断言**：少数测试如果把固定文件名当成 contract，需要同步改成“由 fixture 返回的 path/line”而不是硬编码 `RuntimeFunctionNavigationTest.as`。
3. **fixture/support owner 拆分会牵动 UHT 与 include 路径**：若没有先加 forwarding headers 和最小 façade，`AngelscriptTest`、editor tests 与未来外部 harness 都可能在第一次迁移时同时报编译错误。

---

## 深化 (2026-04-09 第二轮)

### 本轮深化边界

- `P1.4` 已覆盖 shared reset 的 generated symbol cleanup、live-instance guard 与 fresh baseline；以下 `P1.6` 只补 owner/package 边界，不重复前一轮 cleanup 本体。
- `P2.2` 已覆盖 debugger fixture 自举、monitor lifecycle、ready gate 与 stop reason；以下 `P2.5` 只补 protocol version contract，避免与现有 monitor 收口条目重复。
- `P2.3` 已覆盖 file-backed fixture、唯一脚本路径与 current-engine scope；以下 `P2.4` 只补 preprocessed helper 自身的 module identity 漂移与 trace/self-test 盲区。

### Phase 1 追加：把 teardown failure 与 shared reset owner 漂移从“静默”改成“可证明”

- [ ] **P1.5** 为测试层建立 `checked cleanup` / `best-effort cleanup` 双轨 contract，禁止 fixture 与宏继续静默吞掉 `DiscardModule()` 失败
  - 当前 `ASTEST_BEGIN_FULL`、`ASTEST_BEGIN_CLONE`、`ResetSharedCloneEngine()` 以及多处 `ON_SCOPE_EXIT` 都把 `Engine.DiscardModule(...)` 当成 best-effort 语句使用；一旦 helper 因模块名漂移、重复 discard 或 raw/module state 错位返回 `false`，用例仍会继续通过，直接掩盖 teardown 失效和跨用例污染。
  - 这一项要在 `Shared` 层引入两类明确语义的 helper：1. `checked cleanup` 用于 test-owned module，失败时立即把当前用例打红并输出 module/filename/owner 诊断；2. `best-effort cleanup` 仅用于显式允许“模块可能不存在”的场景，调用点必须在代码上声明这是可容忍分支。随后把 `ASTEST_BEGIN_FULL/CLONE`、shared reset、learning trace、自测 helper 和典型 scenario/example teardown 全部迁到统一入口，停止继续裸调 `DiscardModule()`。
  - 迁移顺序先收口基础设施，再迁代表性业务测试：先改宏和 `Shared` helper，让 teardown failure 能被框架检测；再替换 `LearningCompilerTrace`、`ComponentScenario`、`ScriptExampleCoverage` 这类当前已经暴露裸 discard 的文件，避免“Plan 写了、实际主线 helper 仍在吞错”的半收口状态。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-70`：测试 cleanup 普遍忽略 `DiscardModule()` 失败，module teardown 失效会被静默吞掉"
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-02`：多组 parity 测试在共享/production engine 上创建临时模块却没有稳定 teardown，要求至少有显式 scope 与 discard 合同"
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`Issue-4`：`CompilerPipeline` 长期复用 shared engine 且不清理，说明 teardown 失败/缺失一旦发生就会持续累积状态"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：当前优势在 repo-owned runner + 轻量 fixture，必须把 operator contract 做成 deterministic cleanup，而不是继续依赖 best-effort 语义"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1026-L1043 的 `FAngelscriptEngine::DiscardModule()` 明确会在失败时返回 `false`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` L82-L92、L126-L136 的 `ASTEST_BEGIN_FULL/CLONE` 直接裸调 `Engine.DiscardModule(...)`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` L207-L240 的 `ResetSharedCloneEngine()` 同样忽略返回值；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` L23-L27、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp` L121-L127、`Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp` L366-L371、`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp` L171-L176 的代表性 teardown 都仍是无检查 discard。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`
- [ ] **P1.5** 📦 Git 提交：`[TestInfrastructure] Refactor: add checked module cleanup contracts`

- [ ] **P1.5-T** 单元测试：为 checked cleanup / best-effort cleanup 的失败语义建立自测
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptModuleCleanupContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`
  - 测试场景：1. test-owned 模块第一次 `checked discard` 成功、第二次重复 discard 必须返回明确失败并带出模块名诊断；2. `ASTEST_BEGIN_FULL` / `ASTEST_BEGIN_CLONE` 驱动的自动 teardown 在模块身份错配时必须让测试失败，而不是静默继续；3. `best-effort cleanup` 只在显式“模块可能不存在”的 helper 中允许通过，且不会被主 fixture 默认采用。
  - 测试命名：`Angelscript.TestModule.Shared.Cleanup.CheckedDiscardFailsOnDuplicateTeardown`、`Angelscript.TestModule.Shared.Cleanup.FullFixtureReportsDiscardFailure`、`Angelscript.TestModule.Shared.Cleanup.BestEffortDiscardMustBeExplicit`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.5-T** 📦 Git 提交：`[TestInfrastructure] Test: verify checked module cleanup contracts`

- [ ] **P1.6** 把 `ResetSharedCloneEngine()` / debug probe / helper 自测的 generated-symbol 边界从“全局扫描”收回当前 engine package
  - 当前 shared reset 主路径与 debug 统计都用 `TObjectIterator<UASClass>` 全局遍历，只要 `ScriptTypePtr == nullptr` 就清 root/standalone；helper 自测还会在 reset 后按类名全局计数并要求数量为 `0`。这等价于把“shared engine cleanup”做成了“清理整个进程里所有看起来 detached 的脚本类”，会越界影响其它 isolated/production-like engine。
  - 这一项要把 owner contract 改成与现有查找 helper 一致的 engine-local 语义：reset、debug probe 与断言都必须优先按 `Engine->GetPackageInstance()` 或等价 owner 标识过滤对象；只有当前 shared engine 生成的 class/function/symbol 才能被本次 reset 清理。测试层要显式构造“shared engine + foreign engine 并存”的场景，防止 owner 边界再次回退成全局语义。
  - 迁移时优先收口 `ResetSharedCloneEngine()`、`LogSharedEngineDebugState()`、`ResetSharedEngineReleasesGeneratedComponentClasses` 及其配套 helper 自测；不要继续用“按类名全局计数归零”这种断言锁死错误 contract，而应改成“当前 engine 查不到、foreign engine 仍能查到”。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-35`：`ResetSharedCloneEngine()` 用全局 `TObjectIterator<UASClass>` 做 cleanup，shared reset 会越界影响其它 engine 的生成类状态"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-1`：`ClassGenerator.EmptyModuleSetup` 复用 production/shared engine 且没有 clean baseline，说明当前测试进程里确实存在多 engine 并存与互相污染的风险"
    - [C] `Documents/AutoPlans/TestCoverage/EditorAndTools_TestGaps.md` — "`Issue-24`：`SourceNavigation.Functions` 会单独拿 production-like engine，进一步放大 shared reset 扫描全进程对象的越界面"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：现有测试基础设施应保持 deterministic owner；shared reset 必须围绕当前 engine 组织，而不是继续依赖全局副作用"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` L246-L268 的 `ResetSharedCloneEngine()` 与 L285-L309 的 `LogSharedEngineDebugState()` 都在全局 `TObjectIterator<UASClass>` 上工作；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` L487-L515 的 `FindGeneratedClass()` 却已经明确按 `Engine->GetPackageInstance()` 查找，说明 helper 其余部分默认模型本就是 engine-local；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` L418-L444 仍通过全局按名计数断言 shared reset 后类数量必须为 `0`；`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` L22-L29 也证明同一测试进程内确实会再拿一台 production-like engine。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`
- [ ] **P1.6** 📦 Git 提交：`[TestInfrastructure] Refactor: scope shared reset cleanup to engine-owned packages`

- [ ] **P1.6-T** 单元测试：为 shared reset 的 owner/package 边界补齐回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedResetOwnerBoundaryTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`
  - 测试场景：1. shared engine 与 isolated/production-like engine 同时各自产生同名或不同名 generated class 时，shared reset 只清 shared engine package 下的 detached symbols；2. `LogSharedEngineDebugState()` 与配套计数 helper 只统计当前 engine package，不再把 foreign engine 的 `UASClass` 算进 shared reset 诊断；3. `ResetSharedEngineReleasesGeneratedComponentClasses` 改成 engine-local 断言，即 shared engine 侧 `FindGeneratedClass(...) == nullptr`，而 foreign engine 侧同名类仍保持可见。
  - 测试命名：`Angelscript.TestModule.Shared.Reset.CleansOnlyCurrentEnginePackage`、`Angelscript.TestModule.Shared.Reset.DebugProbeIgnoresForeignEngineSymbols`、`Angelscript.TestModule.Shared.Reset.ComponentCleanupDoesNotTouchForeignEngine`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.6-T** 📦 Git 提交：`[TestInfrastructure] Test: verify shared reset owner boundaries`

### Phase 2 追加：把 module identity 与 debugger protocol version 收口到 helper contract

- [ ] **P2.4** 把 preprocessed compile helper 的 module identity 从“由 filename 隐式决定”改成显式 contract，并让 trace/self-test 校验 `ResolvedModuleNames`
  - 当前 `CompileModuleWithResult()`、`AnalyzeReloadFromMemory()`、`CompileAnnotatedModuleFromMemory()` 都声明接收 `ModuleName`，但 annotated/preprocessed 分支会把它直接丢掉，转去调用只接收 `Filename` 的 `PreprocessAndCompile()`；后续执行 helper 又重新混用 `Filename` 和 `ModuleName` 查模块。这使得调用方以为自己控制的是 `ModuleName`，实际 preprocessor 仍按 `FilenameToModuleName(Filename)` 决定真实身份。
  - 这一项要把 helper contract 收口成二选一且对调用方可见：1. 要么 preprocessed 路径显式接受 `ModuleName` 并把它灌入 module desc/build output；2. 要么 helper 返回 `ResolvedModuleNames` / `PrimaryResolvedModuleName`，调用方和 cleanup/trace 全部改用真实解析结果。无论选哪条路，`CompileModuleWithSummary()`、learning trace 和 helper 自测都必须显式断言模块身份，不能继续只测 `CompiledModuleCount`、`Diagnostics.Num()`。
  - 迁移时优先覆盖 path-based/nested filename 场景，因为这正是 identity 漂移最容易被掩盖的位置；`SourceNavigation`、annotated compile、reload analysis、compile summary 与 learning trace 要统一记录 `RequestedModuleName` 与 `ResolvedModuleNames`，避免再把“请求值”误写成事实。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-24`：preprocessed compile helper 忽略 `ModuleName`，导致带路径文件名的 annotated/reload 测试清理错模块"
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-26`：compile summary 与 learning trace 从不校验实际 `ModuleNames`，导致模块身份漂移长期逃逸"
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`Issue-23`：`Preprocessor` 用例已经暴露 helper 拿到 `Engine*` 却没有稳定 current-engine owner，说明 preprocessed 路径的 owner/identity contract 仍不明确"
    - [C] `Documents/AutoPlans/TestCoverage/EditorAndTools_TestGaps.md` — "`Issue-24`：`SourceNavigation.Functions` 复用固定模块名/文件名，在 production-like engine 上更容易把 module identity 漂移与 cleanup 错配放大"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：file-backed 与 memory-backed fixture 都应围绕 deterministic owner/identity 运作，不能继续让 helper 自己猜测模块身份"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h` L31-L36 的公开 API 全都接收 `ModuleName`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` L165-L227 的 `PreprocessAndCompile()` 完全不接收该参数，L275-L282、L319-L365 的 annotated/reload 分支直接把 `ModuleName` 丢掉，L380-L385 又在执行阶段混用 `Filename` 与 `ModuleName` 查模块；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` L680-L744 的 compile summary 自测仍只检查计数与 diagnostics，不验证 `Summary.ModuleNames`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp` L88-L95 与 `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp` L75-L84 只记录请求的 `ModuleName/Filename`，没有记录 helper 实际解析出的模块身份。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningReloadAndClassAnalysisTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`
- [ ] **P2.4** 📦 Git 提交：`[TestInfrastructure] Refactor: make preprocessed module identity explicit`

- [ ] **P2.4-T** 单元测试：为 path-based annotated/reload helper 的 module identity 与 trace coverage 建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPreprocessedModuleIdentityTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`
  - 测试场景：1. 使用带目录的虚拟文件名进行 `CompileAnnotatedModuleFromMemory()` 时，helper 仍能稳定返回并清理同一模块身份，不再出现 `ModuleName` 与 `FilenameToModuleName(Filename)` 错配；2. `AnalyzeReloadFromMemory()` 与 `CompileModuleWithResult()` 在 preprocessed 分支会把 `ResolvedModuleNames` 暴露给调用方，trace/self-test 对 identity mismatch 直接打红；3. `CompileModuleWithSummary()` 与 learning trace 同时记录 `RequestedModuleName`、`ResolvedModuleNames` 与最终 teardown 目标，防止 future regression 继续只靠 diagnostics 数量蒙混过关。
  - 测试命名：`Angelscript.TestModule.Shared.EngineHelper.AnnotatedCompileUsesResolvedModuleIdentity`、`Angelscript.TestModule.Shared.EngineHelper.ReloadAnalysisReportsResolvedModuleIdentity`、`Angelscript.TestModule.Shared.EngineHelper.CompileSummaryAssertsResolvedModuleNames`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.4-T** 📦 Git 提交：`[TestInfrastructure] Test: verify preprocessed module identity contracts`

- [ ] **P2.5** 把 debugger happy-path 的 adapter version 从硬编码 `2` 收口到 session config，并在 smoke/breakpoint/stepping 上断言实际协商版本
  - 当前 debugger 主线 happy-path 全部直接 `SendStartDebugging(2)`；与此同时，session 已经会保存/恢复 `PreviousDebugAdapterVersion`，runtime debug server 也明确把客户端传入值写入 `AngelscriptDebugServer::DebugAdapterVersion`。这说明“协议版本是测试夹具的可控输入”已经存在，只是测试层没有把它建成统一 contract。
  - 这一项要在 `FAngelscriptDebuggerSessionConfig` 中显式加入 `RequestedDebugAdapterVersion`（默认取 `DEBUG_SERVER_VERSION`），并让 `Smoke`、`StartDebuggerSession()`、`StartSteppingDebuggerSession()` 以及 monitor helper 统一从 config 读值，不再内嵌 magic number。握手后还要新增断言，确认 `Session.GetDebugServer().DebugAdapterVersion` 和 `DebugServerVersion` envelope 与本次请求值/当前 server version 一致；旧协议兼容验证则保留为单独 legacy tests。
  - 这项属于对 `P2.2` 的后续补强：前一轮解决了“能否自举与稳定等待”，本轮解决“自举后到底覆盖的是哪个协议版本”。只有把 protocol version 纳入默认 happy-path，未来 `DEBUG_SERVER_VERSION` 前进时 smoke/breakpoint/stepping 才会自动跟上，而不是继续稳定跑在旧 adapter 分支上。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-27`：现有 debugger happy-path 全部把 `StartDebugging` 的 adapter version 硬编码成 `2`，不会自动覆盖当前协议版本"
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-52`：debugger 自测应改成 owned fixture + 显式 contract；协议版本也应成为 session fixture 的一部分，而不是外部偶然状态"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：继续保留 repo-owned debugger runner 的前提，是 handshake/operator contract 必须足够明确且可配置"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h` L12-L20 的 `FAngelscriptDebuggerSessionConfig` 目前没有任何 requested version 字段；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` L38-L53、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` L19-L24、L40-L52、L189-L203、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` L17-L21、L38-L50、L191-L205 都把 `SendStartDebugging(2)` 写死在 happy-path/helper 中；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L15、L103-L116 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L897-L907 说明 server 会直接采纳客户端传入的 adapter version；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp` L35-L36、L93-L99 也已具备保存/恢复全局 `DebugAdapterVersion` 的基础设施。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`
- [ ] **P2.5** 📦 Git 提交：`[DebuggerTest] Refactor: make protocol version part of debugger session config`

- [ ] **P2.5-T** 单元测试：为 debugger handshake 的 protocol version contract 建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerProtocolVersionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`
  - 测试场景：1. 默认 session config 不显式覆盖时，happy-path 使用 `DEBUG_SERVER_VERSION`，并在握手后断言 `Session.GetDebugServer().DebugAdapterVersion == DEBUG_SERVER_VERSION`；2. 显式请求 legacy adapter version 时，shared start helper 与 monitor helper 都会发送同一请求值，且 runtime/server 端能观察到一致版本；3. smoke/breakpoint/stepping 不再直接依赖魔法数字 `2`，未来版本前进时主线回归会自动覆盖新 adapter 分支。
  - 测试命名：`Angelscript.TestModule.Debugger.Protocol.DefaultHandshakeUsesCurrentAdapterVersion`、`Angelscript.TestModule.Debugger.Protocol.HandshakeRespectsRequestedAdapterVersion`、`Angelscript.TestModule.Debugger.Protocol.MonitorUsesConfiguredAdapterVersion`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.5-T** 📦 Git 提交：`[DebuggerTest] Test: verify debugger protocol version contracts`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.5` checked cleanup contract | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptModuleCleanupContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` | checked discard fail-fast、full/clone auto-teardown surfaced failure、best-effort cleanup 显式化 | P1 |
| `P1.6` shared reset owner boundary | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedResetOwnerBoundaryTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` | current-engine package cleanup、foreign engine symbol 保留、debug probe engine-local 统计 | P1 |
| `P2.4` preprocessed module identity | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPreprocessedModuleIdentityTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp` | path-based annotated compile identity、reload analysis resolved names、compile summary/trace identity coverage | P1 |
| `P2.5` debugger protocol version | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerProtocolVersionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` | default adapter version 跟随 `DEBUG_SERVER_VERSION`、requested version override、monitor/helper 一致性 | P1 |

### 本轮追加风险

1. **checked cleanup 会把历史假绿灯直接打红**：一旦 module identity 仍有漂移、重复 discard 或 raw-module 残留，原本静默吞掉的 teardown 失败会立刻变成 hard failure，短期内会增加红灯数量。
2. **shared reset 改成 engine-local 后会改写现有“全局归零”断言**：少数 helper 自测如果把“全进程同名类都消失”误当成 contract，需要同步改成 owner-aware 断言，否则会把 foreign engine 的正常对象误判为泄漏。
3. **preprocessed identity 显式化会逼迫调用方停止拿 filename 当事实**：`SourceNavigation`、learning trace、reload analysis 等路径如果继续只记录请求值或固定脚本名，会在 helper 收紧后暴露出旧 trace/cleanup 写法的偏差。
4. **debugger protocol config 化会暴露 hidden v2 assumption**：现有 smoke/breakpoint/stepping 如果暗中依赖 adapter v2 payload 形态，默认切到 `DEBUG_SERVER_VERSION` 后会更早暴露版本兼容问题，但这正是主线回归需要承担的职责。

---

## 深化 (2026-04-09 第三轮)

### 本轮深化边界

- `P1.5` / `P1.6` 已经覆盖 checked cleanup 与 shared reset 的 engine-local owner；以下 `P1.7`-`P1.9` 只继续收 shared owner 销毁、module shutdown 兜底和 `SHARE_CLEAN/FRESH` 的对称退场，不重复前两轮 cleanup 本体。
- `P2.4` 已经处理 preprocessed module identity；以下 `P2.6` 只补 `FromMemory` / `FromDisk` 的事实输入源 contract，不再重述 `ResolvedModuleNames` trace 覆盖。
- `P2.5` 已把 debugger adapter version 收进 session config；以下 `P2.7` 只补 connect/ready/send/timeout 的 strict protocol contract，避免和 protocol version 条目重复。
- 本轮不重复 `Plan_TestEngineIsolation.md` 的 engine-family 迁移，也不重复 `Plan_ScriptExamplesExpansion.md` 的示例资产整理；这里只记录会直接影响测试基础设施稳定性的 helper / macro / lifecycle contract。

### Phase 1 追加：把 shared owner 的销毁边界与最终兜底收口成可证明 contract

- [ ] **P1.7** 为 shared owner 增加 live-clone guard，禁止 `DestroySharedTestEngine()` / `AcquireFreshSharedCloneEngine()` 在 dependent clone 仍存活时直接轮换 shared owner
  - 当前 shared helper 明确允许“shared owner 常驻 + isolated clone 并存”：`GetOrCreateSharedCloneEngine()` 会创建带 persistent scope 的 shared full engine，而 `CreateIsolatedCloneEngine()` 则始终以 `Clone` mode 创建隔离 engine；helper 自测里也已经有“先拿 shared engine、再创建 clone”的现实样例。但 `DestroySharedTestEngine()` / `AcquireFreshSharedCloneEngine()` 仍旧无条件 `ResetSharedCloneEngine()` 然后直接 `Reset()` shared storage，一旦 clone 仍依赖该 shared state，就会把 runtime 的 deferred shutdown 风险包装成看似成功的 fresh baseline。
  - 本项要把 shared owner 销毁从“裸动作”改成显式 contract：先暴露 shared owner 的 active-clone probe，再把 destroy/fresh helper 改造成 `Try*` 或状态返回式 API。对 live clone 未释放的情况，helper 必须返回明确失败原因与当前 instance id，而不是继续 reset / recreate shared owner。只有在 clone 数归零后，fresh helper 才能真正轮换 shared epoch；需要跨 setup 保留 clone 的测试则必须改用 isolated full owner，而不是继续借 shared owner 做 source。
  - 文档与 helper 自测也要同步改成 owner-aware 语义：`AcquireFreshSharedCloneEngine()` 不再被描述为“永远可拿的新基线”，而是“仅在 shared owner 无 live clone 依赖时可轮换”。这会直接降低 `ClassGenerator` 一类推荐采用 fresh shared baseline 的用例再次把 shared owner 当成 disposable sandbox 的风险。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-49`：`DestroySharedTestEngine()` / `AcquireFreshSharedCloneEngine()` 在仍有 live clone 时直接拆 shared owner，触发 deferred shutdown 并把旧 shared state 带进后续测试"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-1`：`ClassGenerator.EmptyModuleSetup` 已把 `AcquireFreshSharedCloneEngine()` / `ASTEST_CREATE_ENGINE_SHARE_FRESH()` 当成推荐 clean baseline，说明 fresh helper 本身必须先具备真实 owner guard"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：当前测试基础设施的优势在 repo-owned runner + 轻量 fixture，但前提是 owner/fixture contract 必须 deterministic"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` L169-L176 的 `CreateIsolatedCloneEngine()` 固定创建 `Clone` mode engine，L179-L199 的 `GetOrCreateSharedCloneEngine()` 保持 shared owner + persistent scope，L392-L432 的 `DestroySharedTestEngine()` / `AcquireFreshSharedCloneEngine()` 却没有任何 dependent check 就直接 reset + reset storage；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` L555-L556 与 L787-L788 已存在“shared engine 与 isolated clone 同时存活”的 helper 自测样例。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md`
- [ ] **P1.7** 📦 Git 提交：`[TestInfrastructure] Refactor: guard shared owner teardown against live clones`

- [ ] **P1.7-T** 单元测试：为 shared owner/live clone guard 建立 helper 自测
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedOwnerCloneGuardTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`
  - 测试场景：1. 先获取 shared owner，再创建 dependent clone，此时调用新的 `TryDestroySharedTestEngine()` / `TryAcquireFreshSharedCloneEngine()` 必须返回明确 blocker，而不能轮换 shared instance；2. clone 释放后再次 fresh acquire，必须得到新的 shared epoch，并且旧 shared owner 的 modules/symbols 已清空；3. diagnostic 要输出 shared instance id、active clone count 与失败原因，避免 future regression 再退回 silent reset。
  - 测试命名：`Angelscript.TestModule.Shared.Owner.RejectsFreshWhileCloneAlive`、`Angelscript.TestModule.Shared.Owner.RotatesEpochAfterCloneRelease`、`Angelscript.TestModule.Shared.Owner.ReportsActiveCloneBlocker`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.7-T** 📦 Git 提交：`[TestInfrastructure] Test: verify shared owner live-clone guards`

- [ ] **P1.8** 把 `FAngelscriptTestModule::ShutdownModule()` 升级为 shared/stray engine 的最终 cleanup fence
  - 当前 `AngelscriptTest` 模块生命周期基本上是空壳：`StartupModule()` / `ShutdownModule()` 只写日志，而 shared helper 已经在同模块内维护了 static shared storage、persistent scope，以及 `DestroySharedAndStrayGlobalTestEngine()` 这一条统一 teardown 主链。换句话说，模块 owner 已经握有正式 cleanup API，却没有把它接到自身生命周期上，导致测试中断、automation 早退或模块热重载时没有任何最后一道兜底。
  - 本项要把 module shutdown 明确建模成 final cleanup fence：在 `ShutdownModule()` 中调用统一 teardown helper，并在调用前先打印结构化泄漏快照，至少包括 shared owner 是否存在、instance id、active modules、detached symbol 统计以及 active clone count。若因为 `P1.7` 的 live-clone guard 或 ownership 不明而无法安全销毁，shutdown fence 必须把失败原因打到日志与测试断言里，而不是继续输出一条通用 “module shut down”。
  - 这项不是在做 `Plan_TestModuleStandardization` 的模块拆分，而是先补齐现有 owner 最后一道清理边界。只要 `AngelscriptTest` 仍然随主插件默认加载，`ShutdownModule()` 就必须承担 shared/helper 残留的最终回收责任，否则所有上层 cleanup contract 都还缺少一个“测试提前退出时谁来兜底”的答案。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-63`：`AngelscriptTest` 模块 shutdown 没有接入任何 test-engine cleanup，缺少最终泄漏兜底"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-43` / `Arch-MS-48`：`AngelscriptTest` 作为默认加载的 editor module 存在，但 `StartupModule()` / `ShutdownModule()` 只记录日志，真实测试 owner 反而在 runtime/testing 与 commandlet"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：repo-owned runner 的优势成立前提是生命周期 owner 足够明确，不能把最终 cleanup 继续交给顺序相关的 helper 习惯"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp` L9-L16 的 `StartupModule()` / `ShutdownModule()` 目前只有日志；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` L423-L426 已经提供 `DestroySharedAndStrayGlobalTestEngine()` 统一 teardown 入口，但模块生命周期并未调用它。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`
- [ ] **P1.8** 📦 Git 提交：`[TestInfrastructure] Refactor: add module shutdown cleanup fence`

- [ ] **P1.8-T** 单元测试：为 module shutdown cleanup fence 建立 lifecycle 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestModuleLifecycleTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`
  - 测试场景：1. 显式创建 shared owner、注册模块并制造 stray current/global alias 后，执行新的 shutdown cleanup helper，必须清空 shared storage、persistent scope 与 stray alias；2. 若存在 live clone blocker，shutdown fence 不能误清 shared owner，而要返回明确失败原因；3. cleanup 前日志应包含 instance id、active modules 与 clone count，保证失败时可定位残留来源。
  - 测试命名：`Angelscript.TestModule.Shared.ModuleShutdown.CleansSharedAndStrayStorage`、`Angelscript.TestModule.Shared.ModuleShutdown.ReportsLiveCloneBlocker`、`Angelscript.TestModule.Shared.ModuleShutdown.EmitsLeakSnapshot`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.8-T** 📦 Git 提交：`[TestInfrastructure] Test: verify module shutdown cleanup fence`

- [ ] **P1.9** 为 `SHARE_CLEAN` / `SHARE_FRESH` 补齐对称退场 cleanup，停止把“只入场 reset 一次”伪装成完整生命周期隔离
  - 当前 `ASTEST_BEGIN_SHARE_CLEAN` / `ASTEST_BEGIN_SHARE_FRESH` 的宏体只建立 `FAngelscriptEngineScope`，没有任何 `ON_SCOPE_EXIT` 或 `ResetSharedCloneEngine()`；但调用方很容易把名字里的 `CLEAN` / `FRESH` 理解成“整条测试生命周期隔离”。`HotReload` 的 7 条 `AnalyzeReload.*` 用例已经把它们当成分析场景的默认基线：先 compile baseline，再 `AnalyzeReloadFromMemory()`，最后直接结束测试体，实际仍依赖下一条测试开头的 pre-clean 兜底。
  - 本项要把语义拆清楚并对称化：要么新增真正对称的 `shared isolated scope` 宏/fixture，在退场时统一执行 `ResetSharedCloneEngine()`；要么明确把现有宏改名/改文档成 `PreClean` / `PreFresh` 低层入口，然后把需要整条生命周期隔离的调用点迁到新宏。无论采用哪条实现线，`HotReloadAnalysis` 和 macro validation 都必须新增 postcondition 回归，证明退出测试体后 shared engine 上不再遗留分析模块与 diagnostics。
  - 这一项直接影响分析类场景的顺序稳定性，也会降低 `Issue-73` 一类 reload-analysis runner 重构的成本。如果没有先把 shared clean/fresh 的退场 contract 收紧，后续抽统一 runner 只会把同一个脏 shared baseline 复制得更彻底。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-72`：`SHARE_CLEAN` / `SHARE_FRESH` 只有入场 reset，没有退场 reset，分析类用例会把 shared engine 留脏"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-27`：七个 `AnalyzeReload.*` 用例统一使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`，但宏只建立 scope、不做退场 cleanup，shared modules 会依赖下一条测试启动时的 reset"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：轻量 fixture 的前提是 lifecycle contract 简单且 deterministic，不能继续依赖“下一条测试帮我清理”"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` L108-L121 的 `ASTEST_BEGIN_SHARE_CLEAN` / `ASTEST_BEGIN_SHARE_FRESH` 只有 `FAngelscriptEngineScope`；`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` L50-L88 的首条 `AnalyzeReload` 用例已经展示“compile baseline + analyze + 直接 `ASTEST_END_SHARE_CLEAN`”模式，且同文件 L95-L366 的其余 6 条用例沿用同一模式；`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp` L126-L166 的 `SharedCleanMacro` / `SharedFreshMacro` 也只验证 compile-run 成功，没有任何退场后状态断言。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md`
- [ ] **P1.9** 📦 Git 提交：`[TestInfrastructure] Refactor: add symmetric share-clean lifecycle cleanup`

- [ ] **P1.9-T** 单元测试：为 `SHARE_CLEAN` / `SHARE_FRESH` 的退场对称性建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedLifecycleIsolationTests.cpp`
  - 测试场景：1. 在 `SHARE_CLEAN` / `SHARE_FRESH` 测试体内编译具名模块，离开 `ASTEST_END_*` 后必须确认 shared engine 不再保留该模块；2. `AnalyzeReload.*` 退出后不得把 baseline/analyze 产物留在 shared engine；3. validation 要区分“仅 pre-clean”与“完整 isolated lifecycle”两类入口，防止 future caller 再误用旧 contract。
  - 测试命名：`Angelscript.TestModule.Shared.Macros.ShareClean.PostCleanupRemovesModules`、`Angelscript.TestModule.Shared.Macros.ShareFresh.PostCleanupRemovesModules`、`Angelscript.TestModule.HotReload.AnalyzeReload.ShareLifecycleLeavesNoSharedState`
  - 隔离方式：`FAngelscriptEngineScope` + 对称 `ResetSharedCloneEngine()`
- [ ] **P1.9-T** 📦 Git 提交：`[TestInfrastructure] Test: verify symmetric share-clean lifecycle cleanup`

### Phase 2 追加：把 file-backed helper 与 debugger monitor 的隐式协议收口成显式 contract

- [ ] **P2.6** 拆分 `CompileAnnotatedModuleFromMemory()` 与显式 `FromDisk` helper，停止让 absolute-path 分支静默改成“编译磁盘旧文件”
  - 当前 helper API 名称和签名宣称输入主体是内存中的 `Script`，但 `PreprocessAndCompile()` 在 absolute-path 分支里只是把 `AbsoluteFilename = Filename`，并不把传入脚本文本写回磁盘。结果是调用方一旦传 absolute path，真正参与编译的就变成路径上已有的文件内容，而不是这次传入的 `Script`。`RuntimeSourceMetadataBindingsTest` 之所以先手工 `SaveStringToFile()`，本质上就是在为这个 helper contract 漏洞补丁。
  - 本项要把“脚本真实来源”从隐式行为改成显式 API：凡是名称带 `FromMemory` 的 helper，无论 `Filename` 是否 absolute path，都必须保证最终编译的是传入的 `Script`；如果确实需要复用现成磁盘文件，则新增 `CompileAnnotatedModuleFromDisk()` / `AnalyzeReloadFromDisk()` 之类入口，把“从磁盘读取”语义从 `FromMemory` 系列剥离出来。随后所有 file-backed 调用点改成按真实意图选择接口，而不是再手写 `SaveStringToFile()` + `FromMemory` 混合路径。
  - 这项与 `P2.4` 的 module identity 收口互补：`P2.4` 处理“模块名是谁”，`P2.6` 处理“编译的脚本文本到底是谁”。只有这两个 contract 同时成立，后续 source-navigation、runtime metadata 和 disk-backed regression 才不会继续出现“路径像是 file-backed，实际覆盖的是另一份脚本文本”的假绿灯。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-47`：`CompileAnnotatedModuleFromMemory()` 在 absolute-path 分支实际编译磁盘文件，而不是传入的 `Script`"
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-21` / `NewTest-17`：当前所谓 `CompileFromDisk` 其实是手动读文件再走 `CompileModuleFromMemory`，仓库仍缺真正的 disk-backed compile contract"
    - [C] `Documents/AutoPlans/TestCoverage/EditorAndTools_TestGaps.md` — "`Issue-20` / `Issue-24`：`SourceNavigation.Functions` 依赖固定脚本名与 file-backed helper 产物，却没有把真实 source-of-truth contract 说清楚"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：轻量内存脚本夹具是当前基础设施优势，但前提是 memory-backed 与 disk-backed 入口边界清晰"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` L179-L196 的 absolute-path 分支只做 `AbsoluteFilename = Filename`，没有写入传入的 `Script`；同文件 L358-L364 的 `CompileAnnotatedModuleFromMemory()` 仍直接走 `PreprocessAndCompile()`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` L202-L218 的 `RuntimeSourceMetadataBindingsTest` 目前必须手工写 `ScriptPath` 后再调用 `CompileAnnotatedModuleFromMemory(...)`，L229-L232 又只通过 `Contains("RuntimeSourceMetadataBindingsTest")` 侧面证明 module/path 关系。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`
- [ ] **P2.6** 📦 Git 提交：`[TestInfrastructure] Refactor: split memory and disk compile contracts`

- [ ] **P2.6-T** 单元测试：为 `FromMemory` / `FromDisk` helper 的输入源 contract 建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedCompileContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`
  - 测试场景：1. absolute-path 下调用 `CompileAnnotatedModuleFromMemory(..., ScriptB)` 时，即使磁盘上预先存在旧脚本 A，最终生成结果也必须来自 `ScriptB`；2. 显式 `FromDisk` helper 必须真正读取磁盘当前内容，而不是调用方先手工 `LoadFileToString()` 再转给内存编译；3. source-metadata / navigation 相关测试要改成按真实意图选择 memory-backed 或 disk-backed helper，并保持 `SourceFilePath` 与 module metadata 断言稳定。
  - 测试命名：`Angelscript.TestModule.Shared.EngineHelper.FromMemoryAbsolutePathUsesProvidedScript`、`Angelscript.TestModule.Shared.EngineHelper.FromDiskAbsolutePathReadsCurrentFile`、`Angelscript.TestModule.Bindings.SourceMetadata.UsesExplicitFileBackedCompileContract`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.6-T** 📦 Git 提交：`[TestInfrastructure] Test: verify memory and disk compile contracts`

- [ ] **P2.7** 把 debugger session/monitor 收口为 strict protocol helper，显式处理 connect timeout、ready 语义、send 失败与中途 timeout
  - 现有 debugger 基础设施仍停留在“能跑 happy path”的松散轮询层：client 侧 `Connect()` 只要拿到 `SE_EWOULDBLOCK` / `SE_EINPROGRESS` 就立刻宣称连接成功；session 默认 timeout 仍是 5 秒，smoke 又没有统一覆盖；两个 monitor helper 在命中 `HasStopped` 后对 `RequestCallStack`、`Continue`、`Step*` 只发不查，callstack 子等待还硬编码 10 秒；步进 monitor 甚至只会在“零次 stop”时把超时标成 timeout，中途卡住会退化成普通 stop 数量不匹配。整体上，connect 失败、握手异常、写失败和中途卡住都可能被伪装成更晚、更模糊的失败形态。
  - 本项要在 `Shared` 层建立一条统一 strict protocol contract：`Connect()` 必须等待到真正 connected 或明确 timeout；smoke/breakpoint/stepping 要复用同一份 production debugger session config；monitor 启动状态要从“线程已启动”改成“已完成握手且 ready”；主动发送动作统一走 `SendOrFail` helper，发送失败时立刻记录 `Result.Error` 并终止 monitor；`CallStack` 与 phase progress 的等待要使用可配置 timeout，并在中途卡住时把 `bTimedOut` / `Result.Error` 写成明确诊断。
  - 这项不重复 `P2.5` 的 adapter version 配置化，而是把“协议是否正确完成了这轮连接/握手/暂停/继续/步进”变成基础设施层面的显式约束。只有在 ready/read/write/timeout contract 都被收紧后，后续 debugger 业务场景补测才不会继续把 transport/monitor 自身的问题误判成断点或步进行为回归。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-20` / `Issue-21`：smoke handshake 仍是手写收包循环 + 5 秒默认 timeout，协议污染和慢环境都容易被误报"
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-23` / `Issue-24` / `Issue-26`：两个 monitor helper 对 callstack 等待、step phase timeout 与 send failure 都缺少显式 contract"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：repo-owned debugger runner 的价值建立在 helper contract 足够清晰、可诊断，而不是只要最后有 stop 就算通过"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` L32-L73 的 `Connect()` 在 `SE_EWOULDBLOCK` / `SE_EINPROGRESS` 时直接返回成功，没有连接超时；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h` L12-L20 的默认 timeout 仍是 `5.0f`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` L18-L21、L55-L73 说明 smoke 没有统一 timeout helper，且仍使用手写 envelope 轮询；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` L237-L257、L268-L270 对 `RequestCallStack` / `Continue` 发送结果和 callstack timeout 都没有显式错误；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` L252-L295 对 `RequestCallStack` / `Step*` 同样只发不查，而且只在 `StopsHandled == 0` 时把 timeout 标成 `bTimedOut=true`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`
- [ ] **P2.7** 📦 Git 提交：`[DebuggerTest] Refactor: harden monitor ready and timeout contracts`

- [ ] **P2.7-T** 单元测试：为 debugger strict protocol helper 建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerMonitorProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`
  - 测试场景：1. debugger client 在端口未监听或连接迟迟未建立时，必须报告 connect timeout，而不是把失败拖延到后续握手；2. monitor ready 只有在完成 `DebugServerVersion` 握手后才算成功，若连接失败、send 失败或 callstack timeout，helper 必须返回明确 `Result.Error`；3. 步进序列收到部分 stop 后再卡住时，`bTimedOut` 与错误信息必须反映“中途 timeout”，而不是退化成模糊的 stop 数量不匹配。
  - 测试命名：`Angelscript.TestModule.Debugger.Protocol.ConnectTimeoutIsExplicit`、`Angelscript.TestModule.Debugger.Protocol.MonitorReadyRequiresSuccessfulHandshake`、`Angelscript.TestModule.Debugger.Protocol.StepMonitorReportsMidSequenceTimeout`
  - 隔离方式：`FAngelscriptDebuggerTestSession`
- [ ] **P2.7-T** 📦 Git 提交：`[DebuggerTest] Test: verify monitor ready and timeout contracts`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.7` shared owner/live clone guard | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedOwnerCloneGuardTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` | blocker-aware destroy/fresh、clone release 后 epoch 轮换、shared owner 诊断日志 | P1 |
| `P1.8` module shutdown cleanup fence | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestModuleLifecycleTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` | module teardown 清空 shared/stray storage、live clone blocker 报告、cleanup snapshot | P1 |
| `P1.9` symmetric share-clean lifecycle | `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedLifecycleIsolationTests.cpp` | `SHARE_CLEAN/FRESH` 退场后无残留模块、analysis fixture 不再依赖下一条测试 reset | P1 |
| `P2.6` memory vs disk compile contract | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedCompileContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` | absolute-path `FromMemory` 仍编译传入脚本、显式 `FromDisk` 真实读取磁盘、source metadata 保真 | P1 |
| `P2.7` debugger strict protocol helper | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerMonitorProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` | explicit connect timeout、ready 仅在握手成功后成立、send failure / mid-sequence timeout 明确报错 | P1 |

### 本轮追加风险

1. **shared owner guard 会让一批“靠 fresh helper 偷换共享 epoch”的旧测试立刻暴露**：一旦某些用例仍在外层保留 clone，再调用 fresh/destroy helper，它们会从隐式通过变成显式 blocker。
2. **module shutdown fence 会把过去依赖“进程结束自然清理”的残留直接摊开**：短期内可能看到更多 shutdown 日志与 teardown 红灯，但这正是 infrastructure owner 应该承担的可诊断性成本。
3. **`SHARE_CLEAN/FRESH` 退场对称化会改变分析类测试的历史假设**：少数 `HotReload` / validation 用例如果一直默认“下一条测试开头会帮我 reset”，迁移后需要补 postcondition 或改用新的 isolated shared fixture。
4. **memory/disk compile contract 拆分会迫使 file-backed 调用点停止混用 helper**：一些现有测试可能需要在“我想验证 source metadata”与“我想验证 runtime 从磁盘读取”之间作明确选择，不能继续一条用例同时承担两种语义。
5. **debugger strict protocol helper 会把 transport / monitor 自身的问题更早打红**：过去被包装成 stop 数量不匹配或 callstack 缺失的故障，会改成 connect timeout、send failure 或 mid-sequence timeout 的明确失败，短期内会提高红灯可见度。 

---

## 深化 (2026-04-09 第四轮)

### 本轮深化边界

- 本轮只追加当前 `Plan_TestInfrastructure.md` 仍未覆盖的四类 contract 缺口：`TickWorld/BeginPlay` 场景 helper、world-context 恢复矩阵、`DiscardModule()` teardown 护栏、以及 reflected-call helper 的结果/超时语义。
- 不重复 `Documents/Plans/Plan_TestEngineIsolation.md` 中的 engine-local state 主线，也不把 `Documents/Plans/Plan_TestCoverageExpansion.md` 里的业务功能补测重新抄成测试基础设施条目；这里只处理会系统性影响多组测试可靠性的公共夹具与 helper 契约。

### Phase 1 追加：把 world-sensitive helper 与 teardown 护栏补成可直接守门的基础设施 contract

- [ ] **P1.10** 把 `TickWorld()` / `BeginPlayActor()` 从“ambient helper + replay tick”收口为显式 `Engine + WorldContext` contract
  - 当前 `AngelscriptScenarioTestUtils` 同时存在三种会系统性放大顺序依赖的行为：`RequireCurrentEngine()` 在缺少外层 scope 时直接 `checkf`；`TickWorld(Engine, World, ...)` 只安装 current engine、不安装 `WorldContextObject`；同一个 helper 在 `World.Tick()` 后又手工 replay actor/component tick。再叠加 `Template_WorldTick.cpp` 仍保留裸 `World.Tick()` 模板，等于把“ambient current engine”“上一层残留 world context”“双 tick”三种错误样板继续向新测试传播。
  - 本项要把场景 helper 切成明确 contract：`TickWorld(FAngelscriptEngine&, UWorld&, ...)` 只负责一次 world progression，并在 tick 期间显式建立 `FAngelscriptEngineScope(Engine, &World)`；如果确实需要诊断型 actor/component replay，则拆到单独 opt-in helper。`BeginPlayActor` / `TickWorld` 的 ambient overload 保留兼容层，但必须改成返回失败并输出 diagnostics，而不是再用 `checkf` 把单条测试失败放大成整轮 automation 崩溃。
  - 迁移时应优先收口 `Template/Template_WorldTick.cpp` 与 `Learning` 里的 world-driven 样例，再把依赖 `timer/subsystem/world` 查询的场景切到显式 `Engine + World` 调用；这样可以先消掉模板错误示范，再让世界相关测试真正建立在稳定上下文而不是残留状态上。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-13`：`TickWorld()` 在 `World.Tick()` 后又手工 replay actor/component tick，场景基线偏离真实运行时"
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-33` / `Issue-34`：ambient overload 用 `checkf` 解析 current engine，且 world tick 期间没有安装 `WorldContextObject`"
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — "`D-04` / `B-07`：ambient world context 与 engine-local world context 的恢复合同当前没有稳定守门，world-sensitive helper 不显式传 context 时很容易串线"
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — "`NewTest-28`：`Bind_UWorld.cpp` 仍缺 `world context / globals` 直接回归，说明测试基础设施必须先保证 `TickWorld` 提供稳定上下文"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：repo-owned 轻量 fixture 的前提是 helper contract deterministic，而不是依赖 ambient 状态与顺序偶然成立"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h` L15-L19 的 `RequireCurrentEngine()` 直接 `checkf`；同文件 L46-L72 的 `TickWorld()` 先做 `World.Tick(...)`，随后手工 `Actor->Tick()` / `Component->TickComponent()`，而 `WorldScope` 只传 `Engine` 没有传 `&World`；`Plugins/Angelscript/Source/AngelscriptTest/Template/Template_WorldTick.cpp` L18-L24 仍保留裸 `World.Tick(...)` 模板，L41-L50 则继续通过 ambient helper 调 `BeginPlay` 和本地 `TickWorld()`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp` L101-L114 已把该 helper 用在 timer 路径上。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h`、`Plugins/Angelscript/Source/AngelscriptTest/Template/Template_WorldTick.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningComponentHierarchyTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P1.10** 📦 Git 提交：`[TestInfrastructure] Refactor: harden scenario world helper contracts`

- [ ] **P1.10-T** 单元测试：为 world-sensitive scenario helper 建立显式 `Engine + WorldContext` 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioHelperContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Template/Template_WorldTick.cpp`
  - 测试场景：1. `TickWorld()` 内脚本读取 `__WorldContext()` / `GetCurrentWorld()` 时必须解析到当前测试 world；2. 单次 `TickWorld(..., NumTicks=N)` 只能产生 N 次 actor/component tick，不再允许 helper 自己 replay；3. 在无 outer `FAngelscriptEngineScope` 的情况下调用 ambient overload，helper 必须返回失败并输出 diagnostics，而不是触发 fatal assert。
  - 测试命名：`Angelscript.TestModule.Shared.ScenarioHelpers.TickWorldInstallsWorldContext`、`Angelscript.TestModule.Shared.ScenarioHelpers.TickWorldDoesNotReplayActorComponentTicks`、`Angelscript.TestModule.Shared.ScenarioHelpers.AmbientOverloadsReportMissingEngine`
  - 隔离方式：`FAngelscriptEngineScope` + `FActorTestSpawner`
- [ ] **P1.10-T** 📦 Git 提交：`[TestInfrastructure] Test: verify scenario world helper contracts`

- [ ] **P1.11** 为 world-context 恢复补齐 matrix 自测，并把 `FScopedTestWorldContextScope` 收口成 owner-aware contract 门面
  - 当前仓库里与 world-context 恢复直接相关的 helper 自测只有两条 happy path：一条验证“无 current engine 时 ambient world context 能恢复到进入前值”，另一条验证“isolated engine scope 退出后 world context 恢复到空基线”。这两条 smoke 都没有覆盖非空 `PreviousWorldContext`、entry engine 与 exit engine 不同、outer ambient + inner engine scope 嵌套、或 world-context-only scope 在中途切换 current engine 的情况，因此 `Issue-54/55` 这类恢复错误即使存在，CI 也很难及时报警。
  - 本项要先在 `Shared` 层建立 `FWorldContextRestoreMatrixFixture` 一类的矩阵夹具，把 `DummyContextA/B`、outer ambient、entry engine A / exit engine B 这些组合都固化成可复用断言；随后再把 `FScopedTestWorldContextScope` 明确建模成 owner-aware 门面，要么记录 entry owner 并在退出时按进入时所有权恢复，要么在文档中限制其只适用于无 current engine 的场景，避免 helper 名字看似“scope”，实际却依赖退出时当前栈猜 owner。
  - 执行顺序应先补 matrix regression，让当前实现把非空 ambient / engine handoff 的失败稳定复现出来，再根据失败面决定 runtime seam 调整范围。这样本 Plan 负责先把守门网补上，不会再让 world-context contract 继续停留在“靠人脑推理”的状态。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — "`D-04`：自动化没有覆盖 ambient world context 的恢复合同，`FAngelscriptGameThreadScopeWorldContext` 基本处于未验证状态"
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — "`B-07` / `B-08`：`FAngelscriptEngineScope` 与 world-context-only scope 的恢复目标存在 owner 脱节，ambient / engine-local world context 可能互相污染"
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-54` / `Issue-55` / `Issue-56`：`FAngelscriptEngineScope` 会丢失进入前 ambient world context，`FAngelscriptGameThreadScopeWorldContext` 会把旧 context 写回错误 engine，现有自测只覆盖空基线"
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — "`NewTest-28`：`Bind_UWorld.cpp` 仍需要 world context / globals 直接回归，说明测试基础设施必须先把 context 恢复矩阵锁成基础前提"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` L625-L642 的 `WorldContextScopeRestoresPreviousContext` 只覆盖“无 current engine + 进入前 ambient 直接恢复”路径；同文件 L645-L677 的 `EngineScopeRestoresWorldContextAndCurrentEngine` 只覆盖“isolated engine + `PreviousWorldContext == nullptr`”场景，当前没有任何 entry/exit engine handoff、non-null ambient 或 outer ambient + inner scope 的 matrix 守门用例。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
- [ ] **P1.11** 📦 Git 提交：`[TestInfrastructure] Refactor: add world-context restore matrix coverage`

- [ ] **P1.11-T** 单元测试：为 ambient/current-engine handoff 补齐 world-context 恢复矩阵
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptWorldContextRestoreMatrixTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`
  - 测试场景：1. non-null ambient world context 进入 `FAngelscriptEngineScope(*Engine, DummyContext)` 后，退出必须恢复 outer ambient；2. world-context-only scope 在 entry 无 engine、exit 有 engine 的情况下不能把旧 ambient world 写进新 engine；3. entry engine A / exit engine B 时只能恢复 A 的状态，不得污染 B；4. outer ambient + inner engine scope 嵌套离开后，`GetAmbientWorldContext()` 与 `TryGetCurrentWorldContextObject()` 都必须回到进入前矩阵状态。
  - 测试命名：`Angelscript.TestModule.Shared.WorldContext.AmbientRestoredAfterEngineScope`、`Angelscript.TestModule.Shared.WorldContext.WorldOnlyScopeDoesNotPolluteExitEngine`、`Angelscript.TestModule.Shared.WorldContext.EntryEngineRestoreDoesNotPolluteOtherEngine`、`Angelscript.TestModule.Shared.WorldContext.OuterAmbientRestoredAfterInnerScope`
  - 隔离方式：`FAngelscriptEngineScope` + `FScopedTestWorldContextScope`
- [ ] **P1.11-T** 📦 Git 提交：`[TestInfrastructure] Test: verify world-context restore matrix`

- [ ] **P1.12** 承接 `P1.5` 的 checked-cleanup contract，分批把 debugger / file-backed / production-like 调用点迁到可观测 teardown guard
  - `P1.5` 已经负责定义 “checked cleanup / best-effort cleanup” 的底层 contract；本项不重复抽象本身，而是把当前最容易继续污染全局状态的高风险调用点迁到新 guard：world scenario、debugger、file-backed bindings、source navigation 与 file-system。尤其是 debugger 与 file-backed 测试，它们的 teardown 往往不是单个 `DiscardModule()`，而是 `StopDebugging + Disconnect + DiscardModule + Delete(File) + CollectGarbage()` 的组合收尾，如果没有统一 guard，就很难看出究竟哪一步失败了。
  - 迁移目标是让这些组合 teardown 具备同一套可观测 contract：module 清理要校验返回值与 post-lookup，磁盘脚本要校验删除结果，debugger 收尾要区分“必须成功的协议步骤”和“best-effort 断开”，production-like 热重载则要避免“双重 discard + 隐式兜底”继续把真实失败形态藏在 `ON_SCOPE_EXIT` 后面。
  - 这样可以把 `P1.5` 的基础规则真正落到最脆弱的业务调用点，而不是只停留在 `Shared` 层抽象。否则 checked-cleanup contract 写在 helper 里，真正最需要它的 debugger/file-backed 场景仍然沿用旧的静默 teardown，实际收益会被打折。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-51` / `Issue-53`：一处双重 `DiscardModule()`、另一处 production hot-reload 每轮 cleanup 完全不校验 discard 成功与 lookup 失效"
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-31`：file-system 测试只删磁盘目录，不清 shared engine 里的模块和 filename 索引"
    - [C] `Documents/AutoPlans/TestCoverage/EditorAndTools_TestGaps.md` — "`Issue-20`：`SourceNavigation.Functions` teardown 只丢模块不删脚本文件"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：轻量 operator surface 只有在 cleanup 失败可见、owner 边界明确时才值得长期复用"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorInteractionTests.cpp` L45-L49 的 `ON_SCOPE_EXIT` 直接 `DiscardModule()` 后接 `ResetSharedCloneEngine()`，没有任何返回值检查；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` L336-L343 把 `SendStopDebugging()`、`SendDisconnect()`、`Disconnect()`、`DiscardModule()` 和 `CollectGarbage()` 串成 best-effort 退场；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` L205-L209 既不校验两个 `DiscardModule()` 的结果，也不校验 `Delete(*ScriptPath)` 是否真正成功。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptCleanupGuards.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptCleanupGuards.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorInteractionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P1.12** 📦 Git 提交：`[TestInfrastructure] Refactor: migrate high-risk teardowns to cleanup guards`

- [ ] **P1.12-T** 单元测试：为高风险组合 teardown 建立 guard 迁移回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptCleanupContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`
  - 测试场景：1. module 存在时 guard 必须断言 `DiscardModule()` 成功且后续 lookup 为空；2. file-backed teardown 必须同时验证 module 已移除、脚本文件已删除；3. debugger teardown 若 `StopDebugging` / `Disconnect` / `DiscardModule` 任一关键步骤失败，当前测试必须直接暴露明确失败原因，而不是静默进入 `CollectGarbage()`。
  - 测试命名：`Angelscript.TestModule.Shared.Cleanup.DiscardModuleGuardRemovesTrackedModule`、`Angelscript.TestModule.Shared.Cleanup.FileBackedGuardRemovesModuleAndScriptFile`、`Angelscript.TestModule.Debugger.Cleanup.TeardownGuardReportsProtocolOrModuleFailure`
  - 隔离方式：`FAngelscriptEngineScope` + file-backed fixture + debugger fixture
- [ ] **P1.12-T** 📦 Git 提交：`[TestInfrastructure] Test: verify cleanup guard migration on high-risk teardowns`

### Phase 2 追加：把 reflected-call helper 的结果语义与真实调用桥接补成可观测 contract

- [ ] **P2.8** 把 `ExecuteGeneratedIntEventOnGameThread()` 升级为 structured reflected-call helper，并为 `ProcessEvent` / timeout / script-exception 建立直接回归
  - 现有 reflected-call helper 只有一个 `bool`：`Object`/`Function` 非空就几乎必然返回 `true`，即使脚本函数内部抛异常、`RuntimeCallEvent()` 早退、或异步投递到 game thread 后永远等不到回调，也不会把失败状态反馈给调用方。更糟的是，helper 在脚本函数路径直接调用 `UASFunction::RuntimeCallEvent()`，根本没有覆盖 generated `UFunction -> ProcessEvent -> native thunk` 这条真实桥接路径。
  - 本项要把 helper 改成结构化结果，例如 `FAngelscriptReflectedCallResult`，至少显式区分 `bDispatched`、`bExecuted`、`bTimedOut`、`bScriptException`、`FailureReason` 与 `ReturnValue`；off-thread 路径必须有 deadline、可注入 dispatcher seam 和 invalid-object 防护，保证最坏情况也只是可诊断失败，而不是无限挂死。与此同时，针对 generated `UASFunction` 必须新增一条 direct `ProcessEvent` 回归，确认 test helper 不再只覆盖 `RuntimeCallEvent` 快捷路径。
  - 这一项与现有 `P2.1` 的边界是明确分开的：`P2.1` 继续处理 general scenario helper / declaration resolver / optimized dispatch 入口；`P2.8` 只负责当前高频 reflected-call helper 的成功语义、超时语义与真实桥接覆盖，把大量业务测试共享的“执行 generated UFUNCTION”入口先变成可信 contract。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-17`：`ExecuteGeneratedIntEventOnGameThread()` 的布尔返回值无法表示脚本调用失败"
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-29`：helper 在非 game thread 路径上会无限等待，单次卡住即可挂死整轮 automation"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`NewTest-33`：当前测试树对 `UASFunctionNativeThunk()` / `RuntimeCallFunction()` 为 0 命中，而 helper 在脚本函数路径只直接调用 `RuntimeCallEvent()`"
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — "`D-23`：现有自动化调用脚本函数主要停留在 `RuntimeCallEvent` 慢路径，真实桥接/优化路径缺少直接保护"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：operator helper 的价值取决于失败形态是否可观测、可诊断，而不是单靠 happy-path 绿灯"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` L434-L484 中 helper 在 game thread 路径调用完 `Invoke()` 后直接返回 `true`，off-thread 路径则 `CompletedEvent->Wait()` 无超时；同文件 L446-L460 的脚本函数分支始终走 `RuntimeCallEvent()` 而不是 `ProcessEvent()`；`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` L141-L143 与 `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp` L223-L226 都把这个 `bool` 直接当成“函数执行成功”的断言入口。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptReflectedCallHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionProcessEventTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P2.8** 📦 Git 提交：`[TestInfrastructure] Refactor: make reflected call helper observable and bounded`

- [ ] **P2.8-T** 单元测试：为 reflected-call helper 的结果语义、超时语义与真实桥接建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptReflectedCallHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionProcessEventTests.cpp`
  - 测试场景：1. 构造会抛脚本异常的 generated `UFUNCTION`，helper 必须返回 `bExecuted=false`、`bScriptException=true` 并携带异常 diagnostics；2. 注入“永不触发 game-thread 回调”的 dispatcher seam，helper 必须在 deadline 到达后返回 timeout，而不是挂死；3. 对 generated `UASFunction` 走一次真实 `ProcessEvent()`，确认 native thunk/`RuntimeCallFunction()` 路径返回值正确；4. 对 native `UFunction` 路径保留一条 smoke，确认结构化结果不会破坏现有原生反射调用。
  - 测试命名：`Angelscript.TestModule.Shared.ReflectedCall.ScriptExceptionIsObservable`、`Angelscript.TestModule.Shared.ReflectedCall.GameThreadDispatchTimeoutIsReported`、`Angelscript.TestModule.ClassGenerator.ASFunction.ProcessEventDispatchesThroughNativeThunk`、`Angelscript.TestModule.Shared.ReflectedCall.NativeProcessEventPathStillSucceeds`
  - 隔离方式：`FAngelscriptEngineScope` + 可注入 dispatcher seam
- [ ] **P2.8-T** 📦 Git 提交：`[TestInfrastructure] Test: verify reflected call helper contracts`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.10` scenario world helper contract | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioHelperContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Template/Template_WorldTick.cpp` | `TickWorld` 安装 world context、禁止双 tick、ambient overload 缺少 engine 时返回 diagnostics 而不是崩溃 | P1 |
| `P1.11` world-context restore matrix | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptWorldContextRestoreMatrixTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` | non-null ambient 恢复、entry/exit engine handoff、不污染其它 engine、outer ambient + inner scope 嵌套恢复 | P1 |
| `P1.12` teardown cleanup guards | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptCleanupContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` | `DiscardModule()` 成功与 lookup 清空绑定、cleanup failure 显式报错、file-backed module 与磁盘夹具一起删除 | P1 |
| `P2.8` reflected-call helper contract | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptReflectedCallHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionProcessEventTests.cpp` | script exception 可观测、game-thread timeout 有上界、generated `UFunction` 通过 `ProcessEvent/native thunk` 执行、native `UFunction` 路径不回退 | P1 |

### 本轮追加风险

1. **world helper 收口后会立刻暴露依赖双 tick 或 ambient 残留的旧测试**：部分 scenario / learning / template 用例可能需要把“>=N 次”弱断言收紧成精确 tick 计数，并显式传入 `Engine + World`。
2. **world-context matrix 先落地后，短期内大概率会把 runtime 恢复缺陷直接打红**：这不是噪音，而是把当前只靠顺序偶然成立的恢复语义真正纳入守门网。
3. **cleanup failure guard 会提高历史测试的红灯可见度**：过去被 `ON_SCOPE_EXIT` 静默吞掉的 `DiscardModule()` / 文件删除失败会变成显式错误，需要分批迁移高风险目录而不是一次性全仓替换。
4. **structured reflected-call helper 会改变失败形态**：部分高层测试不再只拿到一个泛化 `false` 或超时挂死，而会收到更具体的 `timeout` / `script exception` / `invalid object` 诊断，断言文案需要同步更新。 

---

## 深化 (2026-04-09 第五轮)

### 本轮深化边界

- 本轮只补两类当前文档尚未单独收口的 false-green 来源：`expected-failure` 用例没有稳定的 diagnostics/rollback contract，以及 `ClassGenerator/HotReload/Debugger` 的高价值 regression 仍停留在弱断言。
- 不重复前文 `P2.2` / `P2.7` 已覆盖的 debugger transport/monitor 生命周期，也不把 `Plan_TestCoverageExpansion.md` 中大批业务功能补测平移到这里；这里只处理可被多个测试目录复用的断言与负向 contract helper。

### Phase 1 追加：把 expected-failure 用例从“任意失败都算过”收口成可迁移的 diagnostics contract

- [ ] **P1.13** 为 compile-fail / error-path 测试建立 `expected-failure contract` helper，统一校验失败原因、source fragment 与失败后无残留 artifact
  - 当前负向测试在两个方向上同时失真：`SubsystemScenarioTests` 只断言 `bCompiled == false` 和 `CompileResult == Error`，把任意编译失败都当成“当前不支持”；`ConsoleCommandSignatureCompat` 又把 `Line 8 | Col 2` 这种脚本文本布局写死在 `AddExpectedError(...)` 里，导致纯格式调整也会制造无关红灯。两类问题本质上都来自同一个缺口: 仓库里还没有一条共享的 `expected-failure` contract，来明确“失败必须因为什么原因失败、失败后哪些 artifact 不得留下、哪些 source 片段是稳定 contract、哪些只是噪音”。
  - 本项要在 `Shared` 层抽一套统一 helper，例如 `FAngelscriptExpectedFailureContract` 或等价 API，至少收口四个维度：1. 用 `CompileModuleWithSummary(...)` 或等价入口抓到结构化 diagnostics；2. 支持稳定的 `MessageFragments` / `ModuleName` / 可选 `SourceFragment` 匹配，而不是继续把行列号硬编码成 API 契约；3. 自动追加 `Engine.GetModuleByFilenameOrModuleName(...)`、`FindGeneratedClass(...)`、可选 callable lookup 的 rollback 断言，证明失败后没有半注册 module/class/command 残留；4. 对 error-path 用例提供统一报告文案，让后续 negative tests 不再各自手搓 `AddExpectedError + ExecuteResult != asEXECUTION_FINISHED` 样板。
  - 第一批迁移只挑两类代表性调用点：`Subsystem/AngelscriptSubsystemScenarioTests.cpp` 用它把“当前分支不支持 subsystem script generation”钉成精确 diagnostics + 无 generated artifact 合同；`Bindings/AngelscriptConsoleBindingsTests.cpp` 用它把 `ConsoleCommandSignatureCompat` 从脆弱的行号匹配改成“稳定错误文本 + module/source fragment + command 未注册”三联断言。这样既能立刻减少 false green，也能给后续 Bind negative path、language negative path 提供统一写法。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-42` / 相关弱断言条目：`SubsystemScenarioTests` 只断言 compile error，不验证失败原因；`Issue-46`：失败后也不检查 module/class rollback，任何无关错误都可能被误判成预期 unsupported"
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — "`Issue-62`：`ConsoleCommandSignatureCompat` 把 `int Entry() | Line 8 | Col 2` 写死进 `AddExpectedError(...)`，脚本排版调整就会造成无关红灯"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Batch` / `D9-Operator`：当前测试优势在 repo-owned runner + machine-readable artifact，负向用例也应产出 deterministic failure contract，而不是依赖模糊 `Error` 或脆弱文本布局"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` L77-L107、L124-L149、L166-L197、L214-L244 统一只做 `CompileModuleWithResult(...)` + `bCompiled == false` + `CompileResult == ECompileResult::Error`，没有任何 diagnostics 命中或 `FindGeneratedClass(...)`/module rollback 断言；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` L450-L452 把 `Global function for console command must have signature`、module 名与 `int Entry() | Line 8 | Col 2` 三条文本一起硬编码，L500-L503 也只断言执行失败与 command 未注册，未区分稳定契约和布局噪音。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptExpectedFailureContracts.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptExpectedFailureContracts.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P1.13** 📦 Git 提交：`[TestInfrastructure] Refactor: add expected-failure diagnostic and rollback contracts`

- [ ] **P1.13-T** 单元测试：为 expected-failure helper 建立稳定 diagnostics 与 rollback 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptExpectedFailureContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp`
  - 测试场景：1. helper 能稳定匹配错误主文本、module/source fragment，但不会因为脚本空行或缩进变化而要求固定 `Line/Col`；2. `WorldSubsystem` / `GameInstanceSubsystem` 当前 unsupported 路径除 `CompileResult == Error` 外，还必须命中目标 diagnostics，并在失败后保证 `Engine.GetModuleByFilenameOrModuleName(...)` 为空、`FindGeneratedClass(...)` 为空；3. `ConsoleCommandSignatureCompat` 失败时必须命中稳定的 signature 文本与模块片段，同时确认 `IConsoleManager` 中没有残留 command 注册。
  - 测试命名：`Angelscript.TestModule.Shared.ExpectedFailure.MatchesReasonWithoutLayoutFragility`、`Angelscript.TestModule.Shared.ExpectedFailure.LeavesNoGeneratedArtifactsAfterCompileGuard`、`Angelscript.TestModule.Bindings.ConsoleCommandSignatureCompat.UsesStableDiagnosticContract`
  - 隔离方式：`FAngelscriptEngineScope` + `CompileModuleWithSummary(...)` / `AddExpectedError(...)`
- [ ] **P1.13-T** 📦 Git 提交：`[TestInfrastructure] Test: verify expected-failure diagnostic and rollback contracts`

### Phase 2 追加：把高价值 regression 的弱断言收口成 exact-outcome helper，而不是继续依赖模糊成功信号

- [ ] **P2.9** 为 stateful regression 建立 `exact outcome` assertion helper，优先替换 `ClassGenerator` / `HotReload` / `Debugger` 中的弱断言样板
  - 当前最危险的一批假绿灯不是“没写测试”，而是“写了测试但断言只够证明它大概没崩”：`ClassGenerator.EmptyModuleSetup` 只看 reload flags，几乎不验证 empty module 是否保持 artifact-free；`HotReload.SoftReload.Basic` 只执行 module 级 `GetSoftReloadVersion()`，没有验证同一个 `USoftReloadTarget` 的 generated `GetVersion()` 是否真的切到新实现；`Debugger.Breakpoint.ClearThenResume` 第二轮只要求 `StopEnvelopes.Num() == 0`，`StepOver` / `StepOut` 只看 callstack 行号和栈深，不检查 stop reason、monitor 健康状态或继续执行后的返回值。它们共同说明当前仓库缺少一层面向 regression 的共享断言门面，来表达“应该精确发生什么、绝不能只用 `>= 1` 或 `0 stop` 这种模糊信号过关”。
  - 本项要在 `Shared` 层补一组 exact-outcome helper，而不是全仓一次性机械改断言。第一类 helper 面向 stateful engine/module regression，例如 `AssertModuleHasNoGeneratedArtifacts(...)`、`AssertGeneratedMethodReturns(...)`，服务 `ClassGenerator.EmptyModuleSetup` 和 `HotReload.SoftReload.Basic`；第二类 helper 面向 debugger regression，例如 `AssertMonitorHealthyWithoutStops(...)`、`AssertStopReasonSequence(...)`，在不重复 transport lifecycle 重构的前提下，把业务回归的 postcondition 从“没停下/停在某行”提升到“monitor 健康 + 原因正确 + 执行路径仍正确”。
  - 落地顺序只做代表样本，不追求一轮扫全仓：先把 `ClassGeneratorTests.cpp` 的 empty-module smoke 升级为“无 generated class/struct/enum/delegate 副作用 + backing module 可查询”的完整 contract；把 `HotReload.SoftReload.Basic` 升级为“同一 `UClass` 指针上 `GetVersion()` 的行为从 1 变 2”；再把 `Debugger.Breakpoint.ClearThenResume`、`Debugger.IgnoreInactiveBranch`、`Stepping.StepOver`、`Stepping.StepOut` 迁到 exact outcome helpers，统一要求 `Error.IsEmpty()`、`!bTimedOut`、expected return value 与 `Reason` 序列同时成立。这样能把 false green 最集中的三组 regression 先收口，而不和前文 `P2.2` / `P2.7` 的 monitor/client 生命周期改造打架。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-2`：`ClassGenerator.EmptyModuleSetup` 只覆盖 reload flag；`Issue-20`：`HotReload.SoftReload.Basic` 没有验证生成类方法是否完成 soft reload"
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-2`：`ClearThenResume` / `IgnoreInactiveBranch` 只看 `0 stop`，monitor 失效也可能绿灯；`Issue-3`：`StepOver` / `StepOut` 不检查 stop reason"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Deep` / `D9-Operator`：当前 Angelscript 适合补更稳定的行为规范回归，而不是继续堆手写样板；repo-owned runner + 轻量 fixture 的价值建立在 deterministic assertion contract 之上"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp` L41-L56 只断言 `ReloadRequirement` / `WantsFullReload` / `NeedsFullReload`，没有任何 empty module 副作用检查；`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` L98-L132 只执行 `GetSoftReloadVersion()`，从未调用 generated `USoftReloadTarget::GetVersion()`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` L513-L518、L566-L567 仍以“有 stop/无 stop + `bSucceeded`”作为关键断言；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` L527-L553 与 L629-L655 仅校验 callstack 行号/帧深，没有对 `Reason` 做任何约束。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptRegressionAssertions.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptRegressionAssertions.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P2.9** 📦 Git 提交：`[TestInfrastructure] Refactor: add exact-outcome assertions for stateful regressions`

- [ ] **P2.9-T** 单元测试：为 exact-outcome helper 与首批迁移用例建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptRegressionAssertionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`
  - 测试场景：1. `EmptyModuleSetup` 除 reload flag 外，还必须证明 `Classes/Structs/Enums/Delegates` 为空、`FindGeneratedClass(...)` 不会冒出新类型；2. `SoftReload.Basic` 在 `ClassAfterReload == ClassV1` 前提下，实例方法 `GetVersion()` 的执行结果必须从 `1` 切到 `2`；3. `ClearThenResume` / `IgnoreInactiveBranch` 必须同时满足 `Error.IsEmpty()`、`!bTimedOut`、`StopEnvelopes.Num() == 0` 和正确返回值；4. `StepOver` / `StepOut` 必须显式断言 `Reason` 序列、caller/callee frame 语义与最终执行成功。
  - 测试命名：`Angelscript.TestModule.Shared.Assertions.ExactOutcomeHelpersRejectWeakSignals`、`Angelscript.TestModule.ClassGenerator.EmptyModuleSetup.AssertsNoGeneratedArtifacts`、`Angelscript.TestModule.HotReload.SoftReload.Basic.GeneratedMethodChangesOnSameClass`、`Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume.RequiresHealthyNoStopContract`、`Angelscript.TestModule.Debugger.Stepping.StepOverReasonSequenceContract`、`Angelscript.TestModule.Debugger.Stepping.StepOutReasonSequenceContract`
  - 隔离方式：`FAngelscriptEngineScope` + shared debugger fixture
- [ ] **P2.9-T** 📦 Git 提交：`[TestInfrastructure] Test: verify exact-outcome assertion contracts`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.13` expected-failure contract | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptExpectedFailureContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` | 失败原因精确匹配、source fragment 稳定匹配、失败后无 module/class/command 残留 | P0 |
| `P2.9` exact-outcome assertions | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptRegressionAssertionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` | empty module 无副作用、generated method 真正切换、negative breakpoint 同时校验 monitor 健康与返回值、step reason 序列正确 | P1 |

### 本轮追加风险

1. **expected-failure contract 收紧后会先打红一批“过去任何 error 都算预期”的旧用例**：这不是噪音，而是把真实失败原因从“某处出错了”收口到“因正确原因失败”。
2. **exact-outcome assertion 会暴露历史测试里依赖模糊信号的部分**：例如 `0 stop`、`>= 1`、只看 global free function 结果的用例，在迁移后可能立刻暴露 monitor 健康、generated method thunk 或 rollback contract 的真实缺陷。
3. **console/source fragment 匹配若定义过窄，仍可能把可读性改动误判成回归**：helper 设计时必须明确“稳定 message fragment”与“可选 source fragment”的边界，禁止再把 `Line/Col` 这类布局细节默认升级成硬契约。

---

## 深化 (2026-04-09 第六轮)

### 本轮深化边界

- 本轮只追加 3 类当前文档尚未单独落地的基础设施 contract：shared baseline 的 engine-level reset、`DumpAll` 的 content contract、以及 debugger `Shared` helper 的 checked contract。
- 不重复前文 `P2.7` 已覆盖的 connect/ready/timeout 主线，也不扩写 `Documents/Plans/Plan_ASDebuggerUnitTest.md` 的 pause/step/breakpoint 场景矩阵；本轮只处理会系统性影响多目录测试稳定性的公共夹具与 helper 语义。

### Phase 1 追加：把 shared baseline 从“清 module”补成“恢复 engine-level state”

- [ ] **P1.14** 把 `ResetSharedCloneEngine()` 升级为完整 shared baseline reset，统一清 `diagnostics` 与 hot-reload bookkeeping
  - 当前 `ResetSharedCloneEngine()` 已经负责 active/raw module 与 detached `UASClass` 清理，但仍完全不管 `Diagnostics` / `LastEmittedDiagnostics` / dirty 标记，也不碰 `FileChangesDetectedForReload`、`QueuedFullReloadFiles`、`PreviouslyFailedReloadFiles` 等 hot-reload bookkeeping。结果是 `AcquireCleanSharedCloneEngine()` / `AcquireFreshSharedCloneEngine()` 名义上的 `clean/fresh`，只在 module graph 层成立；上一轮失败 compile 和 reload failure history 仍会被下一条 shared case 继承。
  - 本项要把 shared baseline 明确定义成 runtime-owned、可复用的 engine-level reset：优先增加 `ResetDiagnosticsForTesting()` 与 `ResetHotReloadStateForTesting()` 之类 seam，再由 `ResetSharedCloneEngine()` 统一调用；不要继续在 test module 零碎直改 runtime 字段。落地顺序先收口 runtime seam 与 shared helper，再补 helper 自测、`HotReload` analysis 与 learning trace 的 postcondition，让 `SHARE_CLEAN/FRESH` 真正覆盖 module、generated symbol、diagnostics、reload history 四类状态。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-78`：shared reset 不清 diagnostics 缓存，broken compile 会污染后续 shared case"
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-79`：shared reset 不清 hot-reload bookkeeping，`SHARE_CLEAN` 仍会继承上一轮 reload 失败和队列状态"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-28`：`AnalyzeReload.*` 七个 analysis 用例依赖 `SHARE_CLEAN` 基线，却没有退出清理，说明 suite 已把 shared clean 当成 analysis baseline"
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-02`：共享 production/shared engine 污染会放大顺序依赖，测试基础设施需要 deterministic baseline"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：repo-owned runner + 内存脚本夹具的价值建立在稳定 isolate/reset contract，而不是只保证文件能跑完"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` L207-L269 的 `ResetSharedCloneEngine()` 只清 active/raw module、detached `UASClass` 与 `CollectGarbage()`，没有任何 diagnostics 或 reload state reset；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` L198-L265 的 `FailedAnnotatedModuleDoesNotPolluteLaterCompiles` 只验证 broken compile 后还能恢复编译，不验证 diagnostics baseline；`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` L49-L80 已直接暴露 `GetQueuedFileChangeCount()`、`GetQueuedFullReloadCount()` 与 `GetDiagnosticsCount()` 探针，但没有 shared-baseline contract；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp` L54-L72 在 `RunDecisionScenario()` 开头直接把 `ResetSharedCloneEngine(Engine)` 当成干净起点。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P1.14** 📦 Git 提交：`[TestInfrastructure] Refactor: complete shared baseline reset for diagnostics and hot-reload state`

- [ ] **P1.14-T** 单元测试：为 shared baseline reset 建立 diagnostics + reload history 双 contract 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedBaselineResetTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp`
  - 测试场景：1. 在 shared engine 上先制造一次 failed annotated compile，再执行 `AcquireCleanSharedCloneEngine()` 或 `ResetSharedCloneEngine()`，断言 `Diagnostics`、`LastEmittedDiagnostics` 与 dirty 标记全部回到空基线；2. 先注入 pending file change，再制造一次 reload failure history，执行 shared reset 后断言 `FileChangesDetectedForReload`、`QueuedFullReloadFiles`、`PreviouslyFailedReloadFiles` 与相关时间戳全部归零；3. 先跑一条会留下 reload failure history 的 analysis，再用 `SHARE_CLEAN` 启动一条 unrelated analysis/learning trace，断言第二条结果只反映本轮脚本，不再消费上一轮残留文件。
  - 测试命名：`Angelscript.TestModule.Shared.EngineReset.CleanSharedClearsDiagnosticsCaches`、`Angelscript.TestModule.HotReload.SharedReset.ClearsReloadQueuesAndFailureHistory`、`Angelscript.TestModule.Learning.HotReloadDecisionTrace.ShareCleanDoesNotReplayPreviousReloadState`
  - 隔离方式：`ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `FAngelscriptEngineScope`
- [ ] **P1.14-T** 📦 Git 提交：`[TestInfrastructure] Test: verify shared baseline reset contracts`

- [ ] **P1.15** 为 `DumpAll` 增加 test-owned content contract，显式核对 `Modules.csv` / `Diagnostics.csv` / `HotReloadState.csv`
  - 当前 `DumpAll.EndToEnd` 与 `DumpAll.Summary` 都建立在 ambient `AcquireProductionLikeEngine()` 之上，且只验证“27 个 CSV 文件存在”与 `DumpSummary.csv` 中每张表“状态正确、行数非负”。这等于只证明 exporter wiring 存在，完全没有证明导出的 `Modules.csv`、`Diagnostics.csv`、`HotReloadState.csv` 是否对应当前测试刚布置的输入，更无法拦截 shared/prod 污染。
  - 本项要新增 test-owned dump fixture，而不是继续把更多内容断言堆到 ambient smoke 上：优先用 owned full engine 显式编译一个已知 module、制造一条已知 diagnostics、注入一条已知 pending reload/file-change，再解析 `Modules.csv`、`Diagnostics.csv`、`HotReloadState.csv` 与 `DumpSummary.csv` 做精确对账。现有 `EndToEnd/Summary` 保留为 exporter smoke，但需要在命名或注释里明确它们不是内容正确性的主 guardrail。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-80`：`DumpAll` 回归只验证文件存在，没有受控内容契约，无法拦截 shared/prod 污染"
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-02`：共享 production engine 测试会继承残留模块和状态，单靠 ambient engine 无法保证隔离"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-10`：production engine 上的 hot reload regression 会受 live editor state 影响，说明 dump contract 也不能继续依赖 live/ambient engine"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：当前 AS 的优势是 repo-owned runner + per-run artifacts，dump 产物也应由 test-owned fixture 提供 deterministic 内容契约"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp` L109-L125 的 `RunDumpAll()` 先 `AcquireProductionLikeEngine()`，随后直接执行 `FAngelscriptStateDump::DumpAll()`；同文件 L202-L217 的 `DumpAll.EndToEnd` 只检查每个 CSV 文件存在；L219-L250 的 `DumpAll.Summary` 只检查 `DumpSummary.csv` 行存在、状态匹配和行数 `>= 0`，没有任何 `Modules.csv` / `Diagnostics.csv` / `HotReloadState.csv` 内容断言。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P1.15** 📦 Git 提交：`[TestInfrastructure] Refactor: add test-owned dump content contracts`

- [ ] **P1.15-T** 单元测试：为 `DumpAll` 建立 deterministic content contract 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpContentContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`
  - 测试场景：1. 用 owned full engine 编译一个已知 module 后执行 `DumpAll()`，断言 `Modules.csv` 中恰好出现该 module 与对应 filename；2. 在同一 owned fixture 内制造一条已知 diagnostics，断言 `Diagnostics.csv` 命中目标 filename 与稳定 message fragment，且 `DumpSummary.csv` 中对应行数与明细文件一致；3. 向 fixture 注入一条 pending file change 或 reload history，断言 `HotReloadState.csv` 只出现本轮路径，不包含历史文件；4. 在 contract 测试前串行插入一条会污染 shared engine 的用例，确认 owned dump fixture 导出的内容仍只包含本测试安排的状态。
  - 测试命名：`Angelscript.TestModule.Dump.DumpAll.ContentContract_ExportsOwnedModuleOnly`、`Angelscript.TestModule.Dump.DumpAll.ContentContract_ExportsOwnedDiagnosticsOnly`、`Angelscript.TestModule.Dump.DumpAll.ContentContract_ExportsOwnedHotReloadStateOnly`
  - 隔离方式：`ASTEST_CREATE_ENGINE_FULL()` + test-owned dump fixture
- [ ] **P1.15-T** 📦 Git 提交：`[TestInfrastructure] Test: verify dump content contracts`

### Phase 2 追加：把 debugger `Shared` helper 从“方便使用”收口成 checked contract

- [ ] **P2.10** 把 debugger `Shared` fixture / receive helper 收口为 checked contract，禁止 blind drain、silent disconnect 和 invalid eval metadata
  - 当前 debugger 共享基础设施仍有 3 个会系统性污染后续用例的 helper 缺口：`ReceiveEnvelope()` 用空 `TOptional` 同时表达“没消息”和“接收失败”；`DrainPendingMessages()` 在退出前无条件 `LastError.Reset()`；`CreateBreakpointFixture()` 明明只生成 free-function 场景，却暴露了 `%this%` / `this.StoredValue` 这类天然无效的 eval path。这样 future evaluate/variables/clear-resume regression 很容易先被 helper 自己误导，再在业务断言里表现成模糊超时或 expression failure。
  - 本项明确限定在 `Shared` 层 helper contract，不重复 `P2.7` 已记录的 connect/monitor ready/timeout 主线，也不扩写 `Plan_ASDebuggerUnitTest.md` 的业务场景矩阵。这里要补的是 checked receive/drain 结果、socket liveness 检测，以及 fixture metadata 自检，例如 `ValidateEvalPathsForFixture()`；上层 `ClearThenResume` 等调用点必须显式断言“第二轮开始前无 pending message、无 transport error”，而不是继续 blind drain。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-76`：`DrainPendingMessages()` 会清空真实 transport 错误，断点回归会把协议故障误诊成业务失败"
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-29`：`ClearThenResume` 依赖 blind drain，第二轮前缺少干净前提检查"
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-30`：`ReceiveEnvelope` 不检测远端断开，真实 disconnect 会被伪装成普通 timeout"
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-31`：`CreateBreakpointFixture()` 暴露无效 eval path，会误导后续 evaluate/variables 测试"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-02`：test 层应依赖受控 white-box test API，而不是继续依赖隐式 helper contract"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Deep` / `D9-Operator`：测试 authority 的强项来自稳定 helper/fixture owner，而不是在业务用例里手搓协议细节"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` L131-L158 的 `ReceiveEnvelope()` 用空 `TOptional` 同时表达 no-message 与 error，L223-L238 的 `DrainPendingMessages()` 无条件 `LastError.Reset()`，L324-L351 的 `AppendReceivedData()` 只依赖 `HasPendingData()` 而不检查 socket liveness；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` L520-L545 的 `ClearThenResume` 在第二轮前直接 `Client.DrainPendingMessages()`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp` L62-L95 的 `CreateBreakpointFixture()` 暴露 `ThisStoredValuePath` 与 `ThisScopePath` 两个无效 path；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.h` L16-L30 目前只有 `GetEvalPath()`，没有任何 fixture metadata 自检 API。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P2.10** 📦 Git 提交：`[DebuggerTest] Refactor: add checked receive and fixture metadata contracts`

- [ ] **P2.10-T** 单元测试：为 debugger shared helper 建立 checked receive / drain / fixture metadata 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerClientContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`
  - 测试场景：1. 向 client 注入截断 envelope 或模拟 `Recv()` 失败时，checked drain 必须返回 error 并保留 `LastError`，不能被当成“队列已空”；2. 当 socket 已断开且本地缓冲区为空时，`ReceiveEnvelope()` / `WaitForMessageType()` 必须返回显式 disconnect error，而不是普通 timeout；3. `CreateBreakpointFixture()` 的 free-function 场景不得再暴露 `%this%` / `this.StoredValue`，或 fixture 自检必须在测试启动时明确拒绝这类路径；4. `ClearThenResume` 第二轮开始前必须显式断言 pending message 为零且无 transport error。
  - 测试命名：`Angelscript.TestModule.Debugger.Client.DrainReportsTransportError`、`Angelscript.TestModule.Debugger.Client.ReceiveEnvelopeReportsPeerDisconnect`、`Angelscript.TestModule.Debugger.Fixture.ValidateEvalPathsForBreakpointFixture`、`Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume.RequiresCleanProtocolState`
  - 隔离方式：`FAngelscriptDebuggerTestSession` + shared debugger fixture self-check
- [ ] **P2.10-T** 📦 Git 提交：`[DebuggerTest] Test: verify shared debugger helper contracts`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.14` shared baseline reset | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSharedBaselineResetTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp` | clean/fresh 同时清空 diagnostics、reload queue 与 failure history；第二条 analysis 不再消费前一轮残留 | P0 |
| `P1.15` dump content contract | `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpContentContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp` | owned module/diagnostics/hot-reload state 精确导出；ambient/shared 污染不会进入本轮 dump | P1 |
| `P2.10` debugger shared helper contract | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerClientContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` | checked drain、显式 disconnect、fixture eval path 自检、第二轮前 protocol state 干净 | P1 |

### 本轮追加风险

1. **shared baseline reset 一旦补齐 engine-level state，会先打红一批历史上“靠 clean 名字假定自己已隔离”的用例**：这些红灯不是新增噪音，而是当前 diagnostics/reload residue 被第一次真正纳入 guardrail。
2. **dump content contract 会迫使 dump 测试停止依赖 ambient production-like engine 作为数据源**：短期内一部分只会读 `DumpSummary.csv` 的旧断言可能需要迁到 owned fixture 或重命名为 smoke。
3. **checked debugger helper 会把 transport 和 fixture metadata 问题更早打到 Shared 层**：过去表现为“第二轮没停下”“evaluate 失败”“timeout”的模糊症状，会更早变成 drain error、peer disconnect 或 invalid eval path 的明确失败形态。

---

## 深化 (2026-04-09 第七轮)

### 本轮深化边界

- 本轮只追加 3 个当前文档尚未单独落地的基础设施 contract：warning 成功路径的 `diagnostics` 归属、hot-reload live generated object 的 teardown 顺序、以及 debugger non-blocking send 的 checked contract。
- 不重复前文 `P1.13` 已覆盖的 expected-failure negative diagnostics，也不把 `P1.14` 扩写成全仓 diagnostics 重构；本轮只处理“编译成功但伴随 warning”的 file-scoped/delta contract。
- 不重复前文 `P2.10` 已覆盖的 checked receive/drain，也不重写 `Documents/Plans/Plan_ASDebuggerUnitTest.md` 中 debugger client/session 的初始搭建；本轮只补 send-side transport contract 与调用点 checked send。
- 不重复 `Documents/Plans/Plan_TestCoverageExpansion.md` 中的业务场景补测；本轮条目仍限定为可被多目录复用的 test infrastructure owner/helper contract。

### Phase 1 追加：把 warning success-path 与 hot-reload teardown 都收口为 test-owned contract

- [ ] **P1.16** 为 warning-bearing success path 建立 `file-scoped diagnostics delta` helper，禁止测试继续扫描整台 engine 的历史 warning
  - 当前 warning 断言仍停留在“整台 engine 历史上出现过类似文本就算通过”的粗粒度阶段：`ControlFlow.NotInitialized` 直接遍历 `Engine.Diagnostics` 做 `Contains("may not be initialized")`，而 `ASTEST_CREATE_ENGINE_FULL()` / `ASTEST_BEGIN_FULL` 只复用 `thread_local` full engine 并在退出时丢模块、不清 diagnostics。结果是 warning contract 没有绑定到“这次编译、这个文件”，而是绑定到“这个线程上的 full engine 曾经见过什么 warning”。
  - 这类问题已经不只出现在单条 `ControlFlow` 用例。`Preprocessor` 三条 success-path 用例同样直接拿 ambient engine、只看 `Preprocess()` 的 `bool` 和模块结构，不建立 diagnostics 基线，也不在成功后确认“当前文件没有额外 warning”。如果继续在 shared/full long-lived engine 上新增 warning 回归，顺序相关的假绿会继续扩散。
  - 本项要在 `Shared` 层提供统一的 warning contract 门面：进入编译前记录 diagnostics baseline，编译后只读取“当前 file/module 新增的 diagnostics”，并把 warning/assertion 绑定到真实 `AbsoluteFilename` 或当前 `ModuleDesc->Code[0].AbsoluteFilename`。优先迁移 `ControlFlow.NotInitialized` 与 `Preprocessor` success-path；其他 warning-bearing 用例随后跟进，不再允许直接遍历 `Engine.Diagnostics` 做全局模糊匹配。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-81`：warning 回归按整个 `Engine.Diagnostics` 做全局模糊匹配，旧 warning 可让当前测试误报通过"
    - [C] `Documents/AutoPlans/TestCoverage/LanguageFeatures_TestGaps.md` — "`Issue-85`：`ControlFlow.NotInitialized` 用全局 diagnostics 模糊匹配 warning，未绑定到当前模块"
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`Issue-10` / `Issue-12`：`Preprocessor` success-path 从不清空也不检查 diagnostics，warning/行号漂移会被静默放过"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：repo-owned runner + machine-readable artifact 的价值建立在 deterministic diagnostics contract，而不是依赖环境历史状态"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` L13-L24 的 `ContainsWarningDiagnostic()` 遍历整个 `Engine.Diagnostics`，L124-L140 的 `ControlFlow.NotInitialized` 在 `ASTEST_CREATE_ENGINE_FULL()` 上直接用它判定 warning；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` L34-L40、L82-L94 说明 full engine 是 `thread_local` 复用，退出时只 `DiscardModule()`；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` L13-L28 的 helper 继续拿 ambient engine / 固定落盘脚本，L76-L199 三条 success-path 都只看 `Preprocess()` 和 module 结构，没有任何 diagnostics baseline 或成功后空 diagnostics 断言。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P1.16** 📦 Git 提交：`[TestInfrastructure] Refactor: add file-scoped diagnostics delta contracts`

- [ ] **P1.16-T** 单元测试：为 warning success-path 建立 file-scoped diagnostics/delta 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDiagnosticsContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`
  - 测试场景：1. 在同一台 full/shared engine 上先编译一条 unrelated warning 脚本，再运行 `ControlFlow.NotInitialized`，断言 helper 只消费目标文件新增 warning，前序 warning 不能让当前测试通过；2. `ControlFlow.NotInitialized` 除 warning 文本外，还要锁定目标文件名、变量名或非零行列号，避免仅靠子串命中；3. `Preprocessor` 的 success-path 在进入前建立 diagnostics baseline，成功后要么显式断言当前文件无新增 diagnostics，要么只命中该 case 预期的 manual-import warning，而不是吃到历史 warning。
  - 测试命名：`Angelscript.TestModule.Shared.Diagnostics.FileScopedDeltaIgnoresPreviousWarnings`、`Angelscript.TestModule.Angelscript.ControlFlow.NotInitialized_UsesCurrentFileDiagnosticsOnly`、`Angelscript.TestModule.Preprocessor.Diagnostics.SuccessPathsConsumeCurrentFileDeltaOnly`
  - 隔离方式：`ASTEST_CREATE_ENGINE_FULL()` / `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + file-scoped diagnostics contract helper
- [ ] **P1.16-T** 📦 Git 提交：`[TestInfrastructure] Test: verify file-scoped diagnostics delta contracts`

- [ ] **P1.17** 为 `HotReload` 函数回归引入 test-owned generated object teardown fixture，禁止 live object 仍存活时 `DiscardModule()` / `ResetSharedInitializedTestEngine()`
  - 当前 `HotReload` 函数回归把“验证 generated object 行为”和“回收 module/shared baseline”放在同一作用域里，但没有先释放 test-owned object。`AddModifyLookupFlow` 与 `FailureKeepsOldCodeAndDiagnostics` 都在外层 `ON_SCOPE_EXIT` 里直接 `DiscardModule()` + `ResetSharedInitializedTestEngine()`，而 `TestObject` 直到函数退出才离开作用域；其中前者还在主断言路径里手动再 `DiscardModule()` 一次，进一步放大 teardown 顺序与 owner 不清的问题。
  - 这已经不是单条业务断言缺口，而是 fixture contract 缺口。当前用例没有 `TWeakObjectPtr`、没有“先释放对象再 cleanup”的 postcondition，也没有在 cleanup 前确认 generated class/function lookup 是否应继续可见。结果是 live `UObject`、旧 `UFunction*`、module discard 和 shared reset 的先后顺序全靠当前栈帧偶然成立，最容易演化成 flaky 或顺序污染。
  - 本项要把 hot-reload generated object 收口到 test-owned fixture：集中持有 `Engine`、`ModuleName`、`TWeakObjectPtr<UObject>`、可选 `UFunction*` 与 cleanup guard；析构顺序固定为“释放/失活 test-owned object → `CollectGarbage()`/验证弱引用失效 → `DiscardModuleChecked` → shared reset”。优先迁移 `AddModifyLookupFlow` 与 `FailureKeepsOldCodeAndDiagnostics` 两条代表用例，不再允许在 live generated object 仍存活时直接 reset shared engine。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-82`：hot-reload 函数回归在 live generated `UObject` 仍存活时执行 `DiscardModule`/shared reset"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-19`：`FailureKeepsOldCodeAndDiagnostics` 只验证旧指针还能跑，没有验证查找表仍指向旧版本"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-24` / `Issue-51`：`AddModifyLookupFlow` 没有验证 discard 后 lookup 失效，还在主断言路径里与 `ON_SCOPE_EXIT` 形成双重 cleanup"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：轻量 fixture 的价值建立在 deterministic teardown owner，而不是让业务测试自己手搓对象生命周期"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` L338-L341 的 `AddModifyLookupFlow` 在外层 `ON_SCOPE_EXIT` 里直接 `DiscardModule()` + `ResetSharedInitializedTestEngine()`，L398-L411 期间 `TestObject` 仍存活且主路径又手动执行一次 `Engine.DiscardModule(...)`；同文件 L429-L432 的 `FailureKeepsOldCodeAndDiagnostics` 也在 `ON_SCOPE_EXIT` 中直接 cleanup，L476-L496 继续在 live `TestObject` 上执行旧函数并读取 diagnostics，却没有任何“对象先释放、再 cleanup”的 guard 或弱引用 postcondition。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGeneratedObjectFixture.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGeneratedObjectFixture.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptCleanupContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P1.17** 📦 Git 提交：`[TestInfrastructure] Refactor: enforce generated object teardown order in hot-reload tests`

- [ ] **P1.17-T** 单元测试：为 generated object fixture 建立 teardown-order 与 lookup-cleanup 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGeneratedObjectFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`
  - 测试场景：1. fixture 在 object 仍存活时不得静默执行 `DiscardModule` / shared reset，必须返回明确失败或 guard 诊断；2. 当 object 已释放且弱引用失效后，fixture 才允许执行 `DiscardModuleChecked`，并断言 module lookup 与 generated class/function lookup 一并失效；3. `FailureKeepsOldCodeAndDiagnostics` 在 broken reload 后仍可验证旧代码行为，但退出顺序必须先释放旧对象，再 reset shared engine，不再把 live object 带进下一条测试。
  - 测试命名：`Angelscript.TestModule.Shared.GeneratedObjectFixture.RequiresReleasedObjectsBeforeCleanup`、`Angelscript.TestModule.HotReload.GeneratedObjectFixture.DiscardClearsLookupAfterObjectRelease`、`Angelscript.TestModule.HotReload.GeneratedObjectFixture.FailureFallbackDoesNotLeakLiveObjectIntoReset`
  - 隔离方式：`ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + generated object teardown fixture + `TWeakObjectPtr`
- [ ] **P1.17-T** 📦 Git 提交：`[TestInfrastructure] Test: verify generated object teardown order`

### Phase 2 追加：把 debugger send-side contract 从“尽量发出去”收口成 checked transport

- [ ] **P2.11** 收口 debugger `SendRawEnvelope()` 的 non-blocking send contract，并把 smoke/breakpoint/stepping 迁到 checked send
  - 当前 debugger client 已明确把 socket 设为 non-blocking，但 send path 仍按“阻塞式一次发完”编码：`SendRawEnvelope()` 只要 `Send()` 返回 `false` 或 `BytesSent <= 0` 就立即失败，既不读取 socket error code，也不等待 `WaitForWrite` 或处理 partial send。与此同时，上层 smoke/breakpoint/stepping 还有大量 control-message 调用点没有检查返回值，导致 send-side 抖动被后置成 handshake timeout、漏停断点或 cleanup 噪声。
  - 这项与前文 `P2.10` 的边界是明确分开的：`P2.10` 已经处理 receive/drain/error propagation，本项只处理 send-side transport contract，以及调用点何时必须 checked send、何时可 best-effort disconnect。`Documents/Plans/Plan_ASDebuggerUnitTest.md` 已覆盖 debugger test client/session 的初始搭建，因此本项不重复脚手架，而是硬化现有 helper 的发送语义。
  - 实施时需要统一三层合同：1. `FAngelscriptDebuggerTestClient` 决定 send 是 blocking 还是 non-blocking retry，并在 `LastError` 中带上 `MessageType`、error code、connection state 与已发送字节数；2. smoke 与 monitor helper 统一改为 checked send，终止性 send failure 直接透出，而不是继续等待 receive timeout；3. cleanup helper 区分“必须成功的协议步骤”和“仅断开链路的 best-effort 步骤”，禁止 monitor 线程继续无声吞掉 `StopDebugging` / `Disconnect` 的 send failure。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-77`：debugger client 在 non-blocking socket 上把瞬时写阻塞当成 fatal send failure，监控线程会把 transport 抖动放大成超时型 flaky"
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-16`：test client 使用 non-blocking socket，却没有处理 `would block`/部分发送"
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-32`：breakpoint / stepping 启动 helper 只把收到 `DebugServerVersion` 当成功，send-side failure 仍可能被后置成宽松握手结果"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Deep` / `D9-Operator`：测试 authority 的优势来自协议 contract 在仓内可诊断、可复现，而不是把 transport 抖动折叠成业务层 timeout"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` L54-L63 在 connect 后直接 `SetNonBlocking(true)`，L97-L127 的 `SendRawEnvelope()` 遇到 `Send()==false` 或 `BytesSent<=0` 就立刻报错，没有 `GetLastErrorCode()`、`WaitForWrite` 或 partial-send retry；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` L43-L47 与 L79-L83 只对部分 send 做 checked，但 L33-L39 的 cleanup 仍无条件发送 `StopDebugging` / `Disconnect`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` L49-L49、L193-L193、L239-L257、L273-L274 的 monitor/send 调用点多处直接发送 `StartDebugging`、`RequestCallStack`、`Continue`、`StopDebugging`、`Disconnect`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` L47-L47、L198-L198、L244-L282、L298-L299 同样在 step monitor 中直接发送 `Continue` / `StepIn/Over/Out` / `StopDebugging` / `Disconnect`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerClientContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P2.11** 📦 Git 提交：`[DebuggerTest] Refactor: harden non-blocking send contracts`

- [ ] **P2.11-T** 单元测试：为 debugger send-side contract 建立 would-block / fatal-error / cleanup 传播回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerClientContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`
  - 测试场景：1. 构造一次 first-send `would block` / partial-send 场景，断言 client 会等待并重试，而不是立刻报 fatal send failure；2. 构造永久 send error 场景时，smoke/monitor 必须直接暴露 send-side error，不能退化成 `DebugServerVersion` timeout 或 “did not hit breakpoint”；3. cleanup 阶段若 `StopDebugging` 属于必须成功步骤，失败必须显式报错；`Disconnect` 若被定义为 best-effort，则要有清晰的 helper 命名和调用点约束。
  - 测试命名：`Angelscript.TestModule.Debugger.Client.SendRawEnvelope_RetriesWouldBlock`、`Angelscript.TestModule.Debugger.Smoke.Handshake_CheckedSendReportsTransportFailureBeforeTimeout`、`Angelscript.TestModule.Debugger.Breakpoint.MonitorCheckedSendPropagatesRequestCallStackFailure`、`Angelscript.TestModule.Debugger.Stepping.Cleanup.CheckedStopDebuggingReportsSendFailure`
  - 隔离方式：`FAngelscriptDebuggerTestSession` + debugger client contract seam/fake socket
- [ ] **P2.11-T** 📦 Git 提交：`[DebuggerTest] Test: verify non-blocking send contracts`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.16` file-scoped diagnostics delta | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDiagnosticsContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` | 当前文件 warning delta、历史 warning 不再假绿、success-path diagnostics 不再吃到 ambient state | P1 |
| `P1.17` generated object teardown order | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGeneratedObjectFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` | live object 时禁止 cleanup、object release 后 lookup/discard 一致失效、broken reload 不再把旧对象带进 shared reset | P1 |
| `P2.11` debugger send-side contract | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerClientContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` | would-block retry、fatal send 早暴露、cleanup checked send/best-effort disconnect 分层 | P1 |

### 本轮追加风险

1. **file-scoped diagnostics delta 会先打红一批依赖 ambient diagnostics 历史的旧 warning 用例**：这些失败说明旧测试一直在吃历史 warning，不是本轮新引入的噪音。
2. **generated object teardown fixture 会暴露 hot-reload 测试里“对象还活着就先 reset/discard”的旧写法**：短期内部分用例需要显式引入弱引用和 GC postcondition，不能再依赖栈帧退出顺序碰巧正确。
3. **checked send contract 会把 debugger flaky 更早定位到 transport 层**：过去表现为 handshake timeout、二轮 breakpoint 没命中或 cleanup 噪声的症状，会提前变成 `would block`、partial send 或 terminal send error 的明确失败形态。

---

## 深化 (2026-04-09 第八轮)

### 本轮深化边界

- 本轮只补 `file-backed fixture` 的 deterministic contract、`Preprocessor` 的 scoped current-engine owner，以及 `ASTEST` 生命周期 validator 的漏网形态。
- 不重复 `Documents/Plans/Plan_TestModuleStandardization.md` / `Documents/Plans/Plan_TestSystemNormalization.md` 已覆盖的目录命名、suite 拆分和入口收口。
- 不重复 `Documents/Plans/Plan_TestEngineIsolation.md` 的 clone/full/shared engine 总体隔离路线；这里只补 `Preprocessor` 当前测试仍缺失的本地 `FAngelscriptEngineScope` 子合同。
- 不重写前文已经覆盖的 shared baseline reset、warning delta、debugger helper 主线；本轮条目都限定为当前文档里尚未单独落地的 test fixture / validator contract。

### Phase 1 追加：把 file-backed fixture 的前置条件收口成 deterministic contract

- [ ] **P1.18** 为 `file-backed fixture` 建立 `checked write + unique-root` contract，禁止旧脚本内容回退
  - 当前 `Preprocessor` fixture 把脚本统一写到 `Saved/Automation/PreprocessorFixtures` 固定目录，并直接忽略 `FFileHelper::SaveStringToFile(...)` 的返回值。只要写盘失败、权限不足或旧文件残留，本轮测试就可能继续读取上次内容而假绿；这已经不是单条业务用例的缺口，而是 shared fixture owner 缺少写盘前置条件和 artifact owner 的问题。
  - `Editor` 侧的 `SourceNavigation.Functions` 也落在同一类问题上：它使用固定模块名 `RuntimeFunctionNavigationTest` 和固定文件名 `RuntimeFunctionNavigationTest.as`，teardown 只 `DiscardModule()` 不删除磁盘脚本。这样一来，production-like engine 上的模块缓存、磁盘脚本和同名 fixture 会发生碰撞，造成“导航功能通过”与“本轮输入真实生效”之间脱节。
  - 本项要在 `Shared` 层新增统一的 `file-backed fixture` owner：1. 写盘必须 checked，失败时直接终止测试，不能继续进入 `Preprocess()` / compile path；2. 每轮分配唯一 root、唯一 module/file identity，避免跨用例复用固定文件名；3. teardown 统一清理脚本 artifact 与目录。`Preprocessor` 与 `SourceNavigation` 迁移到同一套 helper 后，后续 file-backed 场景才能建立真正 deterministic 的输入前提。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-83`：`Preprocessor` fixture 写盘失败会静默回退到旧文件内容"
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`Issue-11`：`WriteFixtureFile()` 忽略写盘结果，fixture 写入失败时测试可能读取旧文件假绿"
    - [C] `Documents/AutoPlans/TestCoverage/EditorAndTools_TestGaps.md` — "`Issue-20`：`SourceNavigation.Functions` 落盘临时脚本但 teardown 不删文件"
    - [C] `Documents/AutoPlans/TestCoverage/EditorAndTools_TestGaps.md` — "`Issue-24`：`SourceNavigation.Functions` 复用 production-like engine 却使用固定模块名/文件名，存在共享状态碰撞"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：test authority 应由仓内 owner 持有输入与 artifact，而不是依赖环境里遗留的脚本文件与固定入口名"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` L23-L33 的 `GetPreprocessorFixtureRoot()` / `WriteFixtureFile()` 把脚本固定写到 `Saved/Automation/PreprocessorFixtures`，且 L32 直接忽略 `SaveStringToFile()` 返回值；同文件 L82-L88、L128-L139、L177-L192 三条用例持续复用固定相对路径。`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` L41-L46 使用固定 `RuntimeFunctionNavigationTest.as` 和固定模块名，teardown 只 `Engine.DiscardModule(...)`，没有删除磁盘脚本。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedFixture.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedFixture.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedFixtureContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P1.18** 📦 Git 提交：`[TestInfrastructure] Refactor: add checked file-backed fixture ownership`

- [ ] **P1.18-T** 单元测试：为 `file-backed fixture` 建立 checked write / unique-root / cleanup 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedFixtureContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`
  - 测试场景：1. 注入一次写盘失败，断言 fixture 必须在进入 `Preprocess()` 或 compile path 之前显式失败，不能静默读取旧文件；2. 同一逻辑用例连续两轮运行时，第二轮修改脚本内容后必须命中新文件，不能回退到第一轮残留内容；3. `SourceNavigation.Functions` 退出后磁盘脚本和临时目录必须被清理，且 module/file identity 不再固定复用。
  - 测试命名：`Angelscript.TestModule.Shared.FileBackedFixture.CheckedWriteFailsBeforeCompile`、`Angelscript.TestModule.Preprocessor.FileBackedFixture.UniqueRootPreventsStaleContentFallback`、`Angelscript.TestModule.Editor.SourceNavigation.FileBackedFixture.CleansScriptArtifact`
  - 隔离方式：shared `file-backed fixture` owner + per-run unique `Saved/Automation` 子目录
- [ ] **P1.18-T** 📦 Git 提交：`[TestInfrastructure] Test: verify file-backed fixture contracts`

- [ ] **P1.19** 为 `Preprocessor` fixture 绑定 `scoped current-engine` contract，禁止裸 `Engine*` 与 ambient current-engine 脱节
  - 当前 `Preprocessor` 测试虽然先取到 `FAngelscriptEngine*`，但 `GetEngineForPreprocessorTests()` 只返回裸指针，不建立本地 `FAngelscriptEngineScope`。与此同时，`FAngelscriptPreprocessor` 构造和 `Preprocess()` 执行过程会多次读取 `FAngelscriptEngine::ShouldUseEditorScriptsForCurrentContext()`、`FAngelscriptEngine::ShouldUseAutomaticImportMethodForCurrentContext()` 与 `FAngelscriptEngine::Get().ConfigSettings`，诊断也统一经 `FAngelscriptEngine::Get().ScriptCompileError(...)` 回写。也就是说，测试拿在手里的 `Engine*` 与真正生效的 current-engine 现在并没有被绑定到同一个 owner 上。
  - 这会直接放大 `ImportParsing` 的 helper 风险。当前实现通过 `TGuardValue<bool> AutomaticImportGuard(Engine->bUseAutomaticImportMethod, false);` 修改目标 engine 字段，但如果 outer scope 里存在另一台 current-engine，实际的 automatic-import 决策和 diagnostics 归属仍可能落到 ambient engine 上。对比之下，`AcquireProductionLikeEngine()` 已经证明 production-like 场景应该把 `Engine` 与 `FAngelscriptEngineScope` 一起打包成 fixture owner。
  - 本项要新增 `Preprocessor` 专用 fixture：集中持有 `Engine`、`FAngelscriptEngineScope`、以及可恢复的 testing config，使 `Preprocessor` 所读写的 editor-script / automatic-import / diagnostics 始终绑定同一台 engine。优先迁移 `BasicParse`、`MacroDetection`、`ImportParsing` 三条现有回归；后续所有 file-backed preprocessor 测试都不再允许直接返回裸 `Engine*`。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-84`：`Preprocessor` 测试没有显式绑定 `current engine`，读取和修改的可能不是同一台引擎"
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`Issue-23`：三个 `Preprocessor` 用例拿到了 `Engine*` 却没有建立 `FAngelscriptEngineScope`，实际 current-engine 不受该指针约束"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-02`：test 层需要受控的 test surface / helper owner，而不是继续依赖 ambient runtime/helper contract"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：deterministic test operator 的前提是 fixture owner 持有执行上下文，而不是让环境里的 outer scope 决定本轮行为"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` L13-L20 的 `GetEngineForPreprocessorTests()` 只返回裸 `Engine*`，L194-L196 直接对该指针做 `AutomaticImportGuard`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L40-L41、L214、L232、L485 与 L4234-L4294 在构造、预处理和 diagnostics 写回阶段都读取 current-engine；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L692-L704 显示这些决策依赖 `TryGetCurrentEngine()`，L1015-L1022 提供了 testing 开关写入入口；而 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` L440-L458 的 `FResolvedProductionLikeEngine` / `AcquireProductionLikeEngine()` 已经证明 production-like helper 应同时拥有 `Engine` 与 `EngineScope`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPreprocessorFixture.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPreprocessorFixture.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPreprocessorFixtureContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P1.19** 📦 Git 提交：`[TestInfrastructure] Refactor: scope current-engine for preprocessor fixtures`

- [ ] **P1.19-T** 单元测试：为 `Preprocessor` fixture 建立 current-engine scope / diagnostics 归属回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPreprocessorFixtureContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`
  - 测试场景：1. 先压入一台带相反 automatic-import / editor-script 设置的 outer engine，再运行 `Preprocessor` fixture，断言导入策略只受 fixture engine 控制；2. 构造一条会产生 warning/error 的预处理脚本，断言 diagnostics 只能写回 fixture engine，而不是 outer ambient engine；3. fixture 退出后原始 current-engine 与 testing config 必须恢复，不能把本轮设置泄漏给下一条测试。
  - 测试命名：`Angelscript.TestModule.Preprocessor.Fixture.EngineScopeControlsAutomaticImport`、`Angelscript.TestModule.Preprocessor.Fixture.DiagnosticsRouteToScopedEngine`、`Angelscript.TestModule.Preprocessor.Fixture.RestoresOuterCurrentEngineAfterScope`
  - 隔离方式：local `FAngelscriptEngineScope` owner + scoped testing config restore
- [ ] **P1.19-T** 📦 Git 提交：`[TestInfrastructure] Test: verify preprocessor current-engine fixture contract`

### Phase 2 追加：把 `ASTEST` lifecycle validation 从“样例约定”升级成可拦截多行 `return` 的 guardrail

- [ ] **P2.12** 硬化 `ASTEST lifecycle validation`，拦截 `multi-line terminal return` 并清理现存违规样本
  - 项目文档已经明确要求把终止 `return` 放在 `ASTEST_END_*` 之后，但现有 validator 只检查 `ASTEST_END_*` 前一条非空行是否以 `return ...;` 结束。这意味着 single-line 之外的多行 `return`、带 lambda/body 的 `return TestTrue(...)`，以及更复杂的终止表达式都可能从校验器下方漏过去，生命周期规则实际上正在失去“仓内自动守卫”的作用。
  - 真实违规样本已经落库，而且不止两条 discovery case。`HotReload.ModuleWatcherQueuesFileChanges` 与 `HotReload.PIEStructuralChangeNeedsFullReload` 仍在 `ASTEST_END_SHARE_FRESH` 之前直接 `return`；`LearningCompilerTrace`、`LearningFileSystemAndModuleTrace`、`BlueprintImpact` 则使用 multi-line `return`，现有 validator 因为只看上一行而无法拦截。这说明问题已经从“个别样例写法不一致”升级成“validator contract 无法覆盖仓内当前语法形态”。
  - 本项要把 validator 升级为 block-aware / statement-aware 扫描：识别同一函数体内在 `ASTEST_END_*` 之前结束的 terminal `return` 语句，而不是只看相邻单行；同时迁移现存违规样本为 `bPassed` / `FinalResult` 风格，确保生命周期闭合重新在源码层面可见。只有这样，`ASTEST` 宏规则才不会继续退化成“文档写了但仓内守不住”的软约定。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-69`：源码中已经存在真实的 `ASTEST_END_*` 配对违规样本，宏生命周期规则开始失效"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-36`：`HotReload.ModuleWatcherQueuesFileChanges` 在 `ASTEST_END_SHARE_FRESH` 之前直接 `return`"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-37`：`HotReload.PIEStructuralChangeNeedsFullReload` 同样在 `ASTEST_END_SHARE_FRESH` 之前直接返回"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Deep` / `D9-Operator`：测试优势来自仓内可执行 spec/guardrail，而不是只靠 reviewer 记忆宏使用约定"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` L74-L78 已明确要求 terminal `return` 放在 `ASTEST_END_*` 之后；但 `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp` L13-L59 的 `CollectTerminalReturnBeforeLifecycleEndLocations()` 只检查 `ASTEST_END_*` 前一条非空行，L170-L191 的 repository validator 因而只能拦截最窄的一种形态。当前真实违规仍存在于 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` L323-L329、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` L401-L406、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp` L234-L245、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp` L182-L199，以及 `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp` L212-L214、L300-L302、L372-L377。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`、`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P2.12** 📦 Git 提交：`[TestInfrastructure] Refactor: harden ASTEST lifecycle validation`

- [ ] **P2.12-T** 单元测试：为 `ASTEST lifecycle` validator 建立 single-line / multi-line / 仓内样本回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp`
  - 测试场景：1. validator 必须继续拦截 single-line `return TestTrue(...);` 直接出现在 `ASTEST_END_*` 之前的旧形态；2. validator 必须新增拦截 multi-line `return`、lambda/body 跨行的终止表达式；3. 仓内现有 `HotReload`、`Learning`、`BlueprintImpact` 样本迁移后，repository-level validation 需要保持全绿，防止同类写法再次落库。
  - 测试命名：`Angelscript.TestModule.Validation.LifecycleEndPlacement.DetectsSingleLineReturnBeforeEnd`、`Angelscript.TestModule.Validation.LifecycleEndPlacement.DetectsMultiLineReturnBeforeEnd`、`Angelscript.TestModule.Validation.LifecycleEndPlacement.RepositorySamplesRemainExplicitlyClosed`
  - 隔离方式：repository source scan + migrated sample regression
- [ ] **P2.12-T** 📦 Git 提交：`[TestInfrastructure] Test: verify ASTEST lifecycle validation guardrail`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.18` file-backed fixture contract | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedFixtureContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` | checked write 失败前置报错、unique root 避免旧内容回退、磁盘脚本与临时目录清理 | P1 |
| `P1.19` preprocessor current-engine scope | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPreprocessorFixtureContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` | outer ambient engine 不再劫持 automatic import、diagnostics 只回写 fixture engine、scope/config 正确恢复 | P1 |
| `P2.12` ASTEST lifecycle validation | `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp` | single-line / multi-line terminal return 检测、仓内样本迁移后 repository validator 持续守卫 | P2 |

### 本轮追加风险

1. **`file-backed fixture` 一旦真正要求 checked write 和 per-run unique root，会先暴露一批历史用例对 `Saved/Automation` 固定文件名的隐性依赖**：这些失败说明旧测试一直把磁盘残留当成正常前提，不是新增噪音。
2. **`Preprocessor` current-engine scope 收紧后，会把过去“拿到一台 `Engine*`，实际却由 outer current-engine 决定行为”的偶然成功全部打红**：短期内需要同步清理依赖 ambient scope 的旧 helper，不能只在单条用例上补 `TGuardValue`。
3. **`ASTEST` validator 一旦覆盖 multi-line terminal return，短期内会同时打到 `HotReload`、`Learning`、`BlueprintImpact` 多个目录**：这是仓内 guardrail 第一次真正覆盖现有写法，先红后绿是预期演进，不应为了维持当前绿灯而保留无效校验器。

---

## 深化 (2026-04-09 02:21:58)

### Phase 1 追加：把 engine mode 与宏私有 owner 从“文档描述”收口成可执行 contract

- [ ] **P1.20** 把 `FULL/CLONE/CreateForTesting` 的 engine-mode contract 从 guide 文本升级为宏/fixture guardrail
  - 当前 `ASTEST_CREATE_ENGINE_FULL()` / `ASTEST_CREATE_ENGINE_CLONE()` 把 owner 藏在 `static thread_local TUniquePtr` 里，`ASTEST_END_FULL/CLONE` 却只 discard modules、不回收宏私有 engine；与此同时，`Engine.CreateDestroy` 仍直接使用默认 `CreateForTesting()`，既不清 `ContextStack`，也不检查 `GetCreationMode()` / `OwnsEngine()` / `GetSourceEngine()`。结果是 `TESTING_GUIDE` 里宣称的 “fresh isolated Full” / “lightweight Clone” 语义，在 `AngelscriptTest` 自己的宏、validation 与 lifecycle smoke 中都没有被正式锁住。
  - 本项只处理 `AngelscriptTest` 层的 owner contract，不重复 `Documents/Plans/Plan_TestEngineIsolation.md` 的 runtime engine-local 主线：1. 为 FULL/CLONE 宏暴露显式 storage seam，并让 `ASTEST_END_FULL/CLONE` 在 module cleanup 后立刻销毁 macro-owned engine；2. 新增 focused lifecycle mode tests，分别覆盖“无 current-engine 时 fallback Full”“有 scoped source 时得到 Clone”“默认 `CreateForTesting()` 不得再充当 lifecycle smoke”三条边界；3. 把 clone/full mode 校验补进 validation 与宏文档，避免后续继续依赖口头约定。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-19`：`ASTEST_CREATE_ENGINE_FULL/CLONE` 的 `thread_local` engine 不会在 `ASTEST_END_*` 后销毁"
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-21` / `Issue-23` / `Issue-27`：clone/full 命名、默认 `CreateForTesting()` 与 macro/helper 层的 engine mode contract 长期未被 `AngelscriptTest` 捕获"
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`NewTest-01` 与 `Engine.CreateDestroy` gap 都要求 focused creation-mode lifecycle tests，当前 smoke 会随 ambient current-engine 漂移"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：当前仓库优势是 repo-owned runner + suite fanout，因此测试入口语义必须由可执行 contract 守住，而不是停留在 guide 文本"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` L34-L63 把 FULL/CLONE owner 藏在 `static thread_local TUniquePtr` 里，L82-L95 与 L126-L139 的 `ASTEST_END_FULL/CLONE` 只 discard module、不 reset storage；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` L60-L71 的 `Engine.CreateDestroy` 直接调用默认 `CreateForTesting()` 且只断言非空和 `Reset()`；`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp` L82-L167 只验证 FULL/SHARE_CLEAN/SHARE_FRESH pair，完全没有 clone/full mode 校验；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` L554-L565 与 L787-L805 虽频繁创建 isolated clone，但只断 compile/module 结果，不锁 `GetCreationMode()/OwnsEngine()/GetSourceEngine()`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineLifecycleModeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS_ZH.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P1.20** 📦 Git 提交：`[TestInfrastructure] Refactor: enforce engine mode and macro owner contracts`

- [ ] **P1.20-T** 单元测试：为 full/clone macro owner 与 lifecycle mode 建立 deterministic 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineLifecycleModeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`
  - 测试场景：1. `ASTEST_BEGIN_FULL` 退出后，macro-owned full engine 必须已析构，type/module/current-engine 基线恢复；2. `ASTEST_BEGIN_CLONE` 退出后，source engine 的 active clone count 恢复到进入前值，clone owner 不得残留在线程本地 storage；3. 在 scoped source engine 存在时，`CreateForTesting(..., Clone)` 必须得到 `OwnsEngine()==false` 且 `GetSourceEngine()==Source` 的 clone；4. 在 `TryGetCurrentEngine()==nullptr` 时，默认 `CreateForTesting()` 必须返回 full owner，而不是随 ambient 状态漂移。
  - 测试命名：`Angelscript.TestModule.Engine.Lifecycle.CreateForTestingUsesScopedSourceOrFallsBackToFull`、`Angelscript.TestModule.Validation.EngineMode.FullMacroDestroysOwnedEngineAtEnd`、`Angelscript.TestModule.Validation.EngineMode.CloneMacroRestoresSourceCloneCount`
  - 隔离方式：`FCoreTestContextStackGuard` + explicit macro-owned storage reset seam
- [ ] **P1.20-T** 📦 Git 提交：`[TestInfrastructure] Test: verify engine mode and macro owner contracts`

### Phase 2 追加：把 `NativeScriptHotReload` 的 phase bucket 改成单主题 scenario harness

- [ ] **P2.13** 收敛 `VerifyNativeScriptHotReloadInline()` 为 single-scenario harness，并拆解 `Phase2A/B/C`
  - 当前 helper 强制依赖 `RequireRunningProductionEngine()`，对每个脚本只做 “full compile + 在源码末尾追加注释后 soft reload compile” 两步，没有任何行为断言、产物断言或 checked discard；而 `Phase2A`、`Phase2B` 又把 3 到 4 个无关主题塞进单个 automation test，第一条子场景失败后后续场景会被直接短路，CI 只能看到粗粒度的 `Phase2A/Phase2B` 红灯。
  - 本项要把 native hot reload 收口成 test-owned、单主题、可 triage 的 scenario harness：1. helper 只负责一个 `V1/V2` 脚本对，显式记录 baseline / reload 的 `ECompileResult`、checked discard 结果和必要的 pre/post assertion callback；2. `Phase2A/B` 拆成按主题命名的独立 automation tests，至少分离 enum、inheritance、handle carrier、actor lifecycle 等 contract；3. `Phase2C` 改成最小自包含脚本，或在继续读取 fixture 时先锁住 marker/行为契约，避免测试语义随 `Script/Tests/Test_ExampleActorFixture.as` 漂移。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-5` / `Issue-38`：Native hot reload 目前只验证 compile wrapper，连 initial compile 的 `ECompileResult` 都不检查"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`Issue-49` / `Issue-53` / `Issue-32`：Phase bucket 过粗、discard success 不可见、`Phase2C` 直接读取外部 fixture 导致覆盖语义漂移"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9`：当前测试优势来自 repo-owned runner + 轻量脚本夹具，hot-reload regression 也应保持单场景、可定位、可批执行的组织方式"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` L19-L25 强制使用 production engine；L27-L58 的 `VerifyNativeScriptHotReloadInline()` 只做 compile/reload compile、从不检查 `InitialCompileResult`，且 `ON_SCOPE_EXIT` 里的 `DiscardModule()` 没有任何返回值断言；L80-L139 的 `Phase2A` 同时承载 enum/inheritance/handle carrier 三个主题；L142-L237 的 `Phase2B` 又混合 tag/system utils/actor lifecycle/math；L239-L255 的 `Phase2C` 直接读取 `Script/Tests/Test_ExampleActorFixture.as`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeHotReloadScenario.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptNativeHotReloadScenario.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTypeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadObjectTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P2.13** 📦 Git 提交：`[HotReloadTest] Refactor: split native hot-reload phase buckets into scenario contracts`

- [ ] **P2.13-T** 单元测试：为 native hot-reload scenario harness 建立单主题与 checked-cleanup 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTypeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadObjectTests.cpp`
  - 测试场景：1. enum / inheritance / handle carrier / actor lifecycle 各自独立成单主题用例，失败时不会再短路其它主题；2. baseline compile 必须显式断言 `InitialCompileResult` 处于成功态，再进入 reload 阶段；3. 每轮结束都要验证 `DiscardModule()` 成功且 lookup 已清空；4. `Phase2C` 若继续复用仓库 fixture，必须先锁定关键 marker，并在 reload 前后验证目标 actor fixture contract 没有随外部脚本漂移。
  - 测试命名：`Angelscript.TestModule.Angelscript.NativeScriptHotReload.EnumRoundTrip`、`Angelscript.TestModule.Angelscript.NativeScriptHotReload.InheritanceOverride`、`Angelscript.TestModule.Angelscript.NativeScriptHotReload.HandleCarrier`、`Angelscript.TestModule.Angelscript.NativeScriptHotReload.ActorLifecycle`、`Angelscript.TestModule.Angelscript.NativeScriptHotReload.ExampleActorFixtureContract`
  - 隔离方式：single-scenario hot-reload harness + checked discard contract
- [ ] **P2.13-T** 📦 Git 提交：`[HotReloadTest] Test: verify native hot-reload scenario contracts`

### Phase 3 追加：让 `FAngelscriptTestFixture` 真正成为 canonical fixture owner

- [ ] **P3.2** 把 `FAngelscriptTestFixture` 从“只定义不使用”的 façade 收口为首选测试入口
  - `FAngelscriptTestFixture` 已经定义了 `SharedClone` / `IsolatedFull` / `ProductionLike` 三种 mode，但当前 `AngelscriptTest` 目录没有任何实际调用点；代表性文件仍各自手写 engine 获取、scope、raw snippet、production-like fallback 与 teardown。这样一来，前几轮已经识别出的 shared pollution、annotated cleanup、production-like 误判和 module tracking 规则，都无法通过一个统一 fixture 向外传播。
  - 本项不重复 `P3.1` 的模块拆层，只聚焦当前仓内可立即收口的 fixture contract：1. 扩展 `FAngelscriptTestFixture`，吸纳 tracked module、annotated module、raw snippet 与 production-like artifact cleanup 策略；2. 先迁移 `EngineCore`、`EngineParity`、`SourceNavigation` 与 helper 自测这四组最常复发手写 lifecycle 的文件；3. 把裸 `AcquireProductionLikeEngine()` / `CreateForTesting()` / `ON_SCOPE_EXIT + DiscardModule()` 组合降级成高级 API，只允许 fixture 或少量明确声明所有权的测试继续直连底层 helper。
  - 来源：
    - [B] `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` — "`Issue-12`：`FAngelscriptTestFixture` 处于“定义了但无人使用”的状态，engine 生命周期规则无法集中收敛"
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "跨 lifecycle、commandlet、GAS 与 debugger 的新增测试建议反复把 `FAngelscriptTestFixture` 作为首选 helper，说明缺口已经是跨模块共性，而不是单文件偏好"
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — "大量 world/context/function-library 场景都把 `FAngelscriptTestFixture + FScopedTestWorldContextScope` 视为推荐入口，现状却没有真实使用样板"
    - [C] `Documents/AutoPlans/TestCoverage/EditorAndTools_TestGaps.md` — "editor module lifecycle、content browser、prompt、blueprint impact 等新测试都以 `FAngelscriptTestFixture` 为建议 helper，但当前 editor 测试仍在手写 production-like engine 流程"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-48`：runtime-capable fixtures 需要明确 owner 边界，而不是继续被 editor-only harness 与分散 helper 隐式承接"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` L734-L809 已定义 `FAngelscriptTestFixture`；但 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` L60-L71 仍直接用默认 `CreateForTesting()` 写 lifecycle smoke；`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` L22-L47 继续手写 `AcquireProductionLikeEngine()` + fixed module/file + `ON_SCOPE_EXIT` discard；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` L19-L27 与 L192-L227 仍手写 production engine raw module 流程；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` L554-L565 与 L787-L805 反复手写 shared/isolated engine 组合。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestFixtureContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P3.2** 📦 Git 提交：`[TestInfrastructure] Refactor: adopt FAngelscriptTestFixture as canonical test entrypoint`

- [ ] **P3.2-T** 单元测试：为 canonical fixture 建立 mode-aware self-test，并验证首批迁移不再复制手写 lifecycle
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestFixtureContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`
  - 测试场景：1. `SharedClone` / `IsolatedFull` / `ProductionLike` 三种 mode 都要显式验证 current-engine restore、module cleanup 与 owner 语义；2. fixture 需要支持 tracked module、annotated module 与 raw snippet 三类 cleanup contract，而不是继续依赖调用方手写 `ON_SCOPE_EXIT`；3. 首批迁移后的 `EngineCore` / `Parity` / `SourceNavigation` 回归在串行运行时不再继承前序 module/file artifact。
  - 测试命名：`Angelscript.TestModule.Shared.Fixture.SharedCloneTracksAndCleansModules`、`Angelscript.TestModule.Shared.Fixture.IsolatedFullRestoresCurrentEngine`、`Angelscript.TestModule.Shared.Fixture.ProductionLikeKeepsArtifactsTestOwned`
  - 隔离方式：canonical `FAngelscriptTestFixture` + fixture self-test before representative file migration
- [ ] **P3.2-T** 📦 Git 提交：`[TestInfrastructure] Test: verify canonical fixture contracts`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.20` engine mode / macro owner contract | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineLifecycleModeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp` | FULL/CLONE macro-owned engine 会在 `ASTEST_END_*` 后销毁；默认 `CreateForTesting()` 的 full/clone 分支不再随 ambient current-engine 漂移 | P1 |
| `P2.13` native hot-reload scenario harness | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTypeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadObjectTests.cpp` | 单主题 hot-reload contract、baseline compile result 显式校验、checked discard、Phase2C fixture contract 固定化 | P2 |
| `P3.2` canonical `FAngelscriptTestFixture` | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestFixtureContractTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` | 三种 mode 的 current-engine/module/artifact cleanup contract，以及首批迁移文件不再复制手写 lifecycle | P1 |

### 本轮追加风险

1. **engine-mode contract 一旦收紧，短期内会直接打红一批历史上依赖 `thread_local` owner 常驻或默认 `CreateForTesting()` fallback 的测试**：这不是新问题，而是把过去被 ambient current-engine 和宏私有 storage 掩盖的假阳性提前暴露出来。
2. **把 `FAngelscriptTestFixture` 升级为 canonical fixture 需要与 tracked module / annotated teardown / production-like artifact cleanup 一起推进**：如果只迁移调用点、不先补齐 fixture contract，会把旧的手写泄漏模式换一种包装继续保留。
3. **`NativeScriptHotReload` 从 phase bucket 拆成单主题 scenario 后，测试数量会上升且失败会更细**：短期看起来像“回归变多了”，实质上是把过去被单个 `Phase2A/2B` 红灯掩盖的多条真实 contract 分离出来，属于可解释性提升而不是噪音增加。

---

## 深化 (2026-04-09 06:35:58)

- `Documents/Plans/Plan_TestCoverageExpansion.md` 继续承接 subsystem 生命周期与 generated function table 的业务覆盖扩张；本轮只补测试宿主、artifact 定位与 scenario harness owner，不重复更大的功能矩阵。
- `Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md` 已承接 function-table / startup bind 的业务主线；以下 `P2.14` 只处理 `AngelscriptTest` 如何稳定定位和解析已有 UHT artifact。

### Phase 2 追加：把 `GeneratedFunctionTable` 报表测试从“硬编码路径 + 裸文本解析”收口成 artifact contract

- [ ] **P2.14** 为 `GeneratedFunctionTable` 建立 `artifact resolver + structured parser` 统一入口，并拆出独立报表测试文件
  - 当前 6 条报表类用例把 UHT 生成目录硬编码成 `Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT`，同时把 `Summary.json`、`ModuleSummary.csv`、`SkippedEntries.csv`、`SkippedReasonSummary.csv` 混在同一个大文件里用裸文本 helper 解析。结果是“当前 host/target 布局刚好匹配”和“报表语义正确”被绑成了同一件事，任何平台切换、CSV quoting 或旧产物残留都会制造与真实功能无关的红灯或假绿。
  - 本项要在 `Shared` 层新增 `GeneratedArtifactLocator`/`GeneratedFunctionTableArtifactSnapshot` 一类统一入口：1. 按当前工作区实际布局解析 UHT 输出根，不再写死 `Win64/UnrealEditor`；2. 对 summary/json/csv 做结构化读取，而不是继续依赖 `ParseIntoArray(TEXT(\",\"), false)`；3. 把 6 条 artifact-driven 用例迁到独立 `ArtifactTests` 文件，与仍需 runtime `ClassFuncMaps` 的 map/runtime smoke 分层，避免一个文件同时承担 runtime map、磁盘 artifact 与字符串搜索三种职责。
  - 这项不扩写 function-table 的业务覆盖面，而是先把“读到的是哪一轮产物”“CSV/JSON 是不是按语义解析”“磁盘 artifact 与 runtime representative entry 是否至少存在独立参照”收成基础设施合同；只有这层稳定后，后续 `Plan_TestCoverageExpansion.md` 里的 function-table 细化断言才不会继续建立在脆弱 file-path 和文本 split 之上。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-68`：6 条报表测试把 UHT 目录硬编码成 `Win64/UnrealEditor`；`Issue-12` / `Issue-55`：CSV 用例用逗号硬拆列；`Issue-36` / `Issue-61`：`SummaryOutput` 缺少独立参照与结构字段断言；`Issue-39`：direct-binding 文本 smoke 没有交叉验证 runtime entry"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9` / `D6`：当前仓库的差异化优势是 repo-owned machine-readable artifact 与 freshness contract，因此测试本身也应按 artifact owner 建立稳定的 resolver / parser，而不是把平台路径和文本布局写死在 case 里"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` L244-L247、L461-L464、L596-L599、L671-L674、L708-L711、L754-L757 六处都把生成目录写死为 `Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT`；L510 通过 `CountGeneratedBindingRegistrations(GeneratedDirectory)` 继续用同一目录下的 `AS_FunctionTable_*.cpp` 给 `Summary.json` 自证；L690 与 L734 用 `ParseIntoArray(TEXT(\",\"), false)` 直接拆 CSV；L648-L665 与 L759-L775 只在磁盘 artifact 上搜 `RunBehaviorTree` / `ReportPerceptionEvent` 文本，没有核对最终 runtime `FFuncEntry`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableArtifactTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGeneratedArtifactLocator.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGeneratedArtifactLocator.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P2.14** 📦 Git 提交：`[TestInfrastructure] Refactor: normalize generated artifact resolution and parsing`

- [ ] **P2.14-T** 单元测试：为 generated artifact resolver / parser 建立跨布局与结构化报表回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableArtifactTests.cpp`
  - 测试场景：1. resolver 能在当前构建布局下找到真实 UHT 目录，缺失产物时输出显式前置条件诊断，而不是继续暴露 `Win64/UnrealEditor` 固定路径；2. `Summary.json`、`ModuleSummary.csv` 与 `Entries.csv` 至少在 `totalGeneratedEntries`、`moduleCount`、`totalShardCount`、per-module `moduleName/editorOnly/shardCount` 上互相对账；3. quoted CSV 或带逗号的 `FailureReason` 仍能被结构化 parser 正确读取，不再因为列数变化假红；4. `RunBehaviorTree`、`ReportPerceptionEvent` 的 artifact 文本 smoke 会再交叉验证 runtime `FFuncEntry` 仍是 direct path，而不是只看磁盘宏文本。
  - 测试命名：`Angelscript.TestModule.Engine.GeneratedFunctionTable.Artifacts.ResolveGeneratedRootFromCurrentBuild`、`Angelscript.TestModule.Engine.GeneratedFunctionTable.Artifacts.SummaryCrossChecksEntriesAndModuleCsv`、`Angelscript.TestModule.Engine.GeneratedFunctionTable.Artifacts.SkippedCsvParsesQuotedFailureReasons`、`Angelscript.TestModule.Engine.GeneratedFunctionTable.Artifacts.DirectBindingArtifactMatchesRuntimeEntry`
  - 隔离方式：shared generated-artifact locator + structured CSV/JSON parser；direct-path 交叉验证场景额外要求 runtime 已初始化
- [ ] **P2.14-T** 📦 Git 提交：`[TestInfrastructure] Test: verify generated artifact resolver contracts`

### Phase 3 追加：把 `SubsystemScenarioTests` 从 compile-failure smoke 与 runtime harness 分层

- [ ] **P3.3** 拆分 `SubsystemScenarioTests` 的 compile-guard 与 runtime harness owner，停止 dead fixture 伪装成 scenario 基础
  - 当前 `SubsystemScenarioTests.cpp` 文件头部保留了 `FActorTestSpawner`、`UGameInstance`、`UWorld`、`TestGameInstance` 与 `UAngelscriptNativeScriptTestObject` 相关 include 和 helper，看起来像已经拥有 world/game-instance 级 scenario 基础；但四个 automation test 实际只做 “编译失败即通过” 的 unsupported smoke，从未创建 spawner、从未读取 native recorder、也从未推进任何 world/subsystem 生命周期。这样既误导后续维护者继续往 compile-failure 文件里堆场景夹具，也让 `Subsystem/` 目录看起来像“已有 runtime harness，只差补断言”。
  - 本项不重复 `Plan_TestCoverageExpansion.md` 已列出的完整 subsystem 正向矩阵，只做两件基础设施工作：1. 把当前 unsupported compile-failure 合同迁到专用 `AngelscriptSubsystemCompileFailureTests.cpp`，继续通过 `expected-failure` contract 精确守住 diagnostics 与 rollback；2. 把真正需要 `FActorTestSpawner` / `UGameInstance` / `UWorld` / native recorder 的 runtime harness 抽成独立 `AngelscriptSubsystemRuntimeHarnessTests.cpp` 或 `Shared` helper，并至少做一条最小 harness self-test，证明这些夹具真的会被使用和清理，而不是继续把未使用脚手架留在 smoke 文件里。
  - 这样处理后，当前分支即使 subsystem script generation 仍不支持，`Subsystem/` 目录也会明确区分 “unsupported compile guard” 与 “future runtime carrier”。后续当 `Plan_TestCoverageExpansion.md` 开始补 `Initialize/Tick/Deinitialize/GetCurrent` 的正向回归时，可以直接落在已存在的 runtime harness 上，而不是重新从一份被 smoke 文件污染的脚手架里拆 owner。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-06`：`SubsystemScenarioTests` 只有 compile-failure、没有正向 scenario；`Issue-42` / `Issue-38`：失败原因与失败后残留都没有被正式合同锁住；`Issue-71`：scenario fixture 死代码长期留在 compile-failure 文件里"
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — "`D-01` / `D-11` 相关 subsystem 分析：现有 subsystem 测试大量依赖 fake injected helper，`CreateSubsystemWorld()` 这类真实 world/subsystem carrier 没有真正进入回归；测试基础设施需要把 runtime harness owner 从 friend 注入和死 helper 中解耦出来"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` L8-L15 include 了 `ActorTestSpawner`、`Engine/GameInstance`、`Engine/World`、`TestGameInstance` 与 native recorder 相关头；L26-L42 定义了 `SubsystemScenarioDeltaTime`、`InitializeSubsystemScenarioSpawner(...)`、`GetScenarioNativeRecorder(...)` 三个 scenario helper；但 L66-L245 的四条测试都只执行 `CompileModuleWithResult(...)`、`TestFalse(bCompiled)` 与 `TestEqual(CompileResult, Error)`，没有任何 world/game-instance/spawner/recorder 使用。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemCompileFailureTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemRuntimeHarnessTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSubsystemScenarioHarness.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptSubsystemScenarioHarness.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P3.3** 📦 Git 提交：`[TestInfrastructure] Refactor: split subsystem compile guards from runtime harness`

- [ ] **P3.3-T** 单元测试：为 subsystem compile-guard / runtime harness 分层建立自测
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemCompileFailureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemRuntimeHarnessTests.cpp`
  - 测试场景：1. compile-failure 文件继续显式断言 unsupported diagnostics，并验证失败后没有 module/class/entry 残留；2. runtime harness self-test 必须真实创建 `UGameInstance` / `UWorld` / `FActorTestSpawner` 与 native recorder，至少推进一次受控 tick/teardown，证明夹具真的被执行和回收；3. 当脚本 subsystem 仍不支持时，runtime harness 测试要输出明确的 not-supported guard 或 harness-precondition 结果，而不是把未使用 helper 静默塞回 compile-failure smoke。
  - 测试命名：`Angelscript.TestModule.Subsystem.CompileFailure.UnsupportedScriptsLeaveNoArtifacts`、`Angelscript.TestModule.Subsystem.RuntimeHarness.BootstrapsWorldAndRecorder`、`Angelscript.TestModule.Subsystem.RuntimeHarness.TearsDownWorldAndRecorderCleanly`
  - 隔离方式：`FAngelscriptTestFixture` + `FActorTestSpawner` + explicit `UGameInstance/UWorld` harness owner
- [ ] **P3.3-T** 📦 Git 提交：`[TestInfrastructure] Test: verify subsystem harness ownership split`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P2.14` generated artifact resolver / parser | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableArtifactTests.cpp` | 真实 UHT root 解析、summary/module/entries 对账、quoted CSV 正确解析、artifact 文本与 runtime direct entry 交叉验证 | P1 |
| `P3.3` subsystem harness ownership split | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemCompileFailureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemRuntimeHarnessTests.cpp` | compile-failure contract 留在专用文件、runtime harness 真实创建 world/game-instance/spawner/recorder、unsupported 分支不再隐藏 dead helper | P1 |

### 本轮追加风险

1. **generated-artifact resolver 一旦改为查找当前真实 UHT 目录，部分靠旧 `Win64/UnrealEditor` 残留产物“碰巧通过”的环境会先转红**：这是把错误前置成明确的 build-layout/precondition failure，不是引入新的功能回归。
2. **把 `SubsystemScenarioTests` 拆成 compile guard 与 runtime harness 后，会更直接暴露 `Subsystem/` 目录当前几乎没有正向 scenario coverage**：短期看上去像“新增了空白”，实质上是把过去被 dead helper 伪装掉的 coverage 真空显式化，便于后续由 `Plan_TestCoverageExpansion.md` 承接。
3. **`GeneratedFunctionTable` 的结构化 parser 如果开始正确处理 quoted CSV，会打掉一批依赖“failure reason 永远不含逗号”的脆弱断言习惯**：这是 artifact 语义从文本布局耦合收回到字段合同的必要收紧。 

---

## 深化 (2026-04-09 06:42:48)

- `Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md` 已承接性能测试建设、指标定义与 `Saved/Automation/AngelscriptPerformance/<RunId>/` 产物路径；以下只补 `AngelscriptTest` 现有 performance helper / artifact 的 owner contract，不重复更大的性能基线、stress 波次与文档主线。

### Phase 1 追加：把 performance artifact 从“固定 `RunId` + 只看文件存在”收口成当前轮 test-owned contract

- [ ] **P1.21** 为 `WritePerformanceMetricsArtifact()` 建立 checked-save / unique-run / structured-snapshot contract，禁止旧 `metrics.json` 继续伪装成本轮结果
  - 当前 performance artifact 路径已经稳定，但 owner contract 仍然过松：writer 只调用一次 `FFileHelper::SaveStringToFile(...)` 就直接返回路径，不检查目录创建与写盘是否成功；`ArtifactGeneration` 与 4 条 startup performance 用例又都复用固定 `RunId`，并且只靠 `FileExists`、`LoadFileToString` 和子串匹配确认成功。这样一来，只要当前轮写盘失败但旧目录里还留着 `metrics.json`，测试就能继续从历史文件里“读到成功”。
  - 本项要在 `Shared` 层补出一条真正的 artifact owner contract：1. `WritePerformanceMetricsArtifact()` 返回结构化结果，而不是裸路径，至少显式携带 `bSaved`、`RunDirectory`、`MetricsPath`、本轮唯一 `nonce`；2. 写入前统一清理或分配唯一 run 目录，保证本轮一定读到新文件；3. `Core` 层补一个 `PerformanceArtifactSnapshot` 解析入口，对 `run_id`、`test_group`、`metrics[*].name/median/samples`、`notes` 与 `nonce` 做结构化读取，不再继续用字符串包含判断 schema 正确性。
  - 这项只收口“当前轮 artifact 是否真的由当前测试写出且结构可消费”，不改 `Plan_AngelscriptEngineBindAndFileWatchValidation.md` 已定义的指标项、产物根路径和性能基线策略；也不把 `metrics.json` 混成 repo runner 的 `Summary.json` 替代物，而是让它作为 performance 子域 artifact 拥有与当前仓库总体 runner contract 一致的 owner 强度。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-20`：`PerformanceArtifactGeneration` 只做字符串包含检查，没有验证 `metrics.json` 的 JSON 结构；`Issue-57`：所有 performance 用例都复用固定 `RunId`，writer 也不检查 `SaveStringToFile(...)` 成功与否，旧 artifact 会制造假绿"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D8-Observability`：当前仓库的稀缺优势是 script-visible scope 之外还会落 `metrics.json`；`D9`：测试结果/产物 owner 已经被收口成 machine-readable contract，因此 performance artifact 也不应继续停留在“文件存在即可”的松散层"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceTestUtils.h` L40-L79 的 `WritePerformanceMetricsArtifact()` 只构造 JSON 并调用 `FFileHelper::SaveStringToFile(Output, *MetricsPath)`，没有检查返回值；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPerformanceArtifactTests.cpp` L17-L28 把 `RunId` 固定为 `P3_4_PerformanceArtifactGeneration`，并且仅通过 `FileExists`、`LoadFileToString`、`Contents.Contains(...)` 判断成功；`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp` L164-L195 的 4 条 startup performance 用例同样把 `RunId` 写死为 `P3_1_StartupPerformance_*`，没有任何本轮唯一标识或旧目录清理。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceTestUtils.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceArtifactSnapshot.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceArtifactSnapshot.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPerformanceArtifactTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp`
- [ ] **P1.21** 📦 Git 提交：`[TestInfrastructure] Refactor: harden performance artifact ownership and parsing`

- [ ] **P1.21-T** 单元测试：为 performance artifact writer / parser 建立“当前轮写入”回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPerformanceArtifactTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceArtifactHelperTests.cpp`（新建）
  - 测试场景：1. `ArtifactGeneration` 必须解析 `metrics.json` 的真实 JSON 结构，精确断言 `run_id`、`test_group`、metric 名称、`median`、sample 数组、`notes` 与本轮唯一 `nonce`；2. 预先放入同 `RunId` 的旧 `metrics.json` 后，本轮写入仍必须读到新 `nonce`，证明测试没有继续吃历史残留文件；3. 当 writer seam 模拟 `MakeDirectory` 或 `SaveStringToFile` 失败时，helper 要返回显式失败结果，不能再给调用方一个“看起来可读”的旧路径。
  - 测试命名：`Angelscript.TestModule.Core.Performance.Artifacts.ParsesStructuredMetricsJson`、`Angelscript.TestModule.Core.Performance.Artifacts.RejectsStaleMetricsFromPreviousRun`、`Angelscript.TestModule.Shared.Performance.Artifacts.WriterFailureIsReported`
  - 隔离方式：unique `RunId + nonce` 的 test-owned metrics root；writer failure 通过可注入 seam 模拟，避免依赖宿主文件系统权限偶现
- [ ] **P1.21-T** 📦 Git 提交：`[TestInfrastructure] Test: verify performance artifact ownership contracts`

### Phase 2 追加：把 startup performance 用例从“只测耗时 + 断言不崩”升级成语义化测量 contract

- [ ] **P2.15** 扩展 startup performance sample，显式记录 creation mode / source identity / startup bind pass，并移除 `check()` 式硬失败 helper
  - 当前 `Startup.Full`、`Startup.Clone`、`Startup.CreateForTestingFallbackFull`、`Startup.CreateForTestingClone` 看起来像在保护 4 条启动路径，但实际样本只携带 3 个时长字段；测试既不核对 `FAngelscriptBindExecutionSnapshot::InvocationCount` / `ExecutedBindNames`，也不核对 `GetCreationMode()` / `GetSourceEngine()` / `OwnsEngine()`，而且一旦创建 engine 失败还会直接 `check()` 打断整批自动化。这样会把“路径语义是否正确”“样本是否有效”和“artifact 是否写出来”混成一个模糊成功信号。
  - 本项要把 performance sample 改成 semantic sample：1. 在样本里补齐 `InvocationCount`、`ExecutedBindCount`、creation mode、`OwnsEngine`、是否命中了预期 source engine 等字段；2. `CollectStartupSamples()` 在 warmup / measurement 前后显式建立并恢复 `ContextStack` 基线，避免 `CreateForTestingFallbackFull` 因前序 current-engine 污染偷偷退成 clone；3. `Measure*` helper 改为返回带 `bValid` / `FailureReason` 的结果结构，`RunTest()` 用 `TestTrue` / `AddError` 报告失败，而不是继续依赖 `check()`。
  - 第一批语义断言应当直接落在现有 4 条 performance 测试上：`Startup.Full` 要证明 startup bind pass 的确执行过；`Startup.Clone` / `CreateForTestingClone` 要证明没有 replay bind 且 source identity 正确；`CreateForTestingFallbackFull` 要证明在清空 current-engine 后确实创建成 full / owner engine。只有在这些合同成立后，`metrics.json` 才有资格作为基线 artifact 被继续消费。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-21`：`CreateForTestingFallbackFull` 没有清 `ContextStack`，可能在脏 current-engine 下偷偷测到 clone；`Issue-22`：`Startup.Full` / `CreateForTestingFallbackFull` 只写 performance artifact，没有断言 full startup 语义；`Issue-43`：4 个 `Measure*` helper 都用 `check()`，回归时会直接打断整批自动化"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D8-Observability`：当前仓库的 performance 优势不只是有 timing，而是 `metrics.json` 可以作为自动化基线消费；因此采样本身必须先守住 path semantics，再谈 artifact 价值"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp` L42-L68 的 `ResetPerformanceEngineState()` / `CollectStartupSamples()` 只销毁 shared/global engine，没有任何 `ContextStack` 清理；L73-L97 的 `ValidateAndWriteStartupMetrics()` 只把 3 个 timing 字段写进 `metrics.json` 并断言文件存在；L101-L155 的 4 个 `Measure*` helper 全部使用 `check(Engine.IsValid())` / `check(SourceEngine.IsValid())` / `check(CloneEngine.IsValid())`；L163-L195 的 4 条 `RunTest()` 中，除 clone 场景对两项时长做 `== 0.0` 断言外，没有任何 `InvocationCount`、creation mode、source identity 或 owner 断言。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceTestUtils.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceStartupHelperTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`
- [ ] **P2.15** 📦 Git 提交：`[TestInfrastructure] Refactor: make startup performance helpers semantic and non-fatal`

- [ ] **P2.15-T** 单元测试：为 startup performance semantic sample 与 failure reporting 建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceStartupHelperTests.cpp`（新建）
  - 测试场景：1. `Startup.Full` 每个样本都要显式断言 `InvocationCount == 1`、`ExecutedBindCount > 0`，并且 creation mode 为 full；2. `Startup.Clone` / `CreateForTestingClone` 必须同时断言 `InvocationCount == 0`、没有 replay startup bind、`GetSourceEngine()` 指向预期 source engine；3. `CreateForTestingFallbackFull` 要在清空 current-engine 后证明 creation mode 为 full 且 `OwnsEngine == true`；4. helper 自测要通过可注入 factory seam 模拟 engine/source/clone 创建失败，验证测试只记录结构化 failure diagnostics，不再触发 `check()` 终止进程。
  - 测试命名：`Angelscript.TestModule.Core.Performance.Startup.FullValidatesStartupBindPass`、`Angelscript.TestModule.Core.Performance.Startup.CloneValidatesSourceIdentityAndNoBindReplay`、`Angelscript.TestModule.Core.Performance.Startup.CreateForTestingFallbackValidatesFullMode`、`Angelscript.TestModule.Shared.Performance.Startup.HelperReportsCreationFailureWithoutFatalAssert`
  - 隔离方式：`ContextStack` baseline guard + unique run id；failure path 通过 helper seam 注入，不依赖真实 engine 创建崩溃
- [ ] **P2.15-T** 📦 Git 提交：`[TestInfrastructure] Test: verify startup performance semantic contracts`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.21` performance artifact owner contract | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptPerformanceArtifactTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceArtifactHelperTests.cpp` | JSON schema 精确解析、旧 `metrics.json` 不能伪装成本轮结果、writer save failure 显式上报 | P1 |
| `P2.15` startup performance semantic sample | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptPerformanceStartupHelperTests.cpp` | full/clone/fallback create mode 与 startup bind pass 合同、source identity 合同、creation failure 非 fatal diagnostics | P1 |

### 本轮追加风险

1. **performance artifact 一旦改成 unique `RunId + nonce` 或先清旧目录，任何依赖固定 `P3_1_*` 目录反复读取“最新一次”结果的本地脚本都会先失效**：需要同步确认是否存在仓外消费方，避免把正确的 owner 收紧误判成工具回归。
2. **把 performance helper 的 `check()` 改成结构化测试失败后，单次 run 可能会暴露出更多后续失败**：这不是噪声变多，而是从“第一条 fatal assert 提前终止”转成“多条 contract 都能留下可定位证据”。
3. **为 startup performance 加上 creation mode / source identity / `ContextStack` 断言后，短期内更容易打到前序 current-engine 污染和 ambient scope 残留**：这会让某些过去只看 timing 的用例从“稳定绿灯”变成“稳定暴露真实隔离问题”，属于必要的收紧而非新增回归。

---

## 深化 (2026-04-09 06:54:39)

- `Documents/Plans/Plan_TestCoverageExpansion.md` 已覆盖功能域新增测试；以下 `P2.16` 只收口“通过条件必须显式表达”的基础设施合同，`P3.4` 只处理文件职责拆分与共享 harness，不重复业务 coverage 扩张。
- `Documents/Plans/Plan_TestModuleStandardization.md` 已覆盖目录命名、Automation 前缀与 suite 入口；以下不再重复目录/文件命名规则，只补 file-level case carrier 与仓内 guardrail。

### Phase 2 追加：把低信号绿灯从“断言副作用”收口成显式 outcome / compile-only contract

- [ ] **P2.16** 为 lightweight smoke 与 compile-only 用例建立 `explicit outcome` guardrail，禁止 `TestTrue(..., true)` 与 `TestEqual(...); return true;`
  - 当前 `AngelscriptTest` 里已经出现成片的低信号通过模式：一类是在执行或编译后只做 `TestEqual(...)`，随后无条件 `return true;`；另一类是名称看起来在验证行为，但测试体只做到“能编译 / 能拿到 symbol”后再执行 `TestTrue(..., true)`。这会把失败传播从当前 `RunTest()` 的显式返回路径，退化成“依赖 UE 断言副作用刚好记错”的软绿灯。
  - 本项只处理测试基础设施合同，不代替具体功能域补测：1. 新增 repository-level `AssertionSignal` validation，专门拦截 `constant-true assertion`、`tail return true after assertion`、以及“行为名义但 compile-only”三类样式；2. 迁移第一批已确认违规的代表用例，让 `NeverVisited`、`Constructor`、`Memory.Construction`、`ObjectPtrCompat`、`Core.CreateCompileExecute` 一类 case 要么返回显式 `bPassed`，要么升级成真正执行/诊断断言；3. 对当前因运行时 blocker 只能 compile-only 的场景，强制落显式 `CompileOnly` contract，而不是继续用空断言伪装成行为回归。
  - 这样处理后，`Angelscript.TestModule.*` 的“绿灯”才重新表示“当前文件真的把失败纳入返回路径”，而不是“最后一条断言只是在 Automation 日志里留了个副作用”。这与 `P2.9 exact-outcome` 不冲突：`P2.9` 处理 stateful regression 的高价值 postcondition，本项处理更底层的测试通过条件表达。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/LanguageFeatures_TestGaps.md` — "`Issue-1`：`ControlFlow.NeverVisited` 只有 `TestTrue(..., true)`；`Issue-2`：`Functions.Constructor` 只编译模块、不执行构造语义；`Issue-6`：`Memory.Construction` 也是空断言测试"
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — "`Issue-72`：`ObjectPtrCompat` / `SoftObjectPtrCompat` 在尾部 `TestEqual(...)` 后直接 `return true;`，最终通过条件没有进入显式返回路径"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：repo-owned 测试优势成立的前提，是 case carrier 与 pass/fail contract 都 deterministic，而不是靠日志副作用或模糊 smoke 结果撑住绿灯"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp` L98-L116 的 `NeverVisited` 只 `ASTEST_BUILD_MODULE` + `GetFunctionByDecl(...)`，随后 `TestTrue(..., true)` 并 `return true;`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp` L114-L136 的 `Constructor` 只编译 `ConstructorCarrier` 后直接 `return true;`；`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptMemoryTests.cpp` L51-L65 的 `Construction` / `FreeUnused` 仍保留 `TestTrue(..., true)` 与无条件 `return true;`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` L78-L81、L189-L192 在 `TestEqual(Result, 1)` 后直接 `return true;`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp` L17-L28、L38-L49 也是 `TestEqual(...)` 后直接 `return true;` 的同类样板。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptAssertionSignalValidationTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptMemoryTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P2.16** 📦 Git 提交：`[TestInfrastructure] Refactor: enforce explicit assertion outcome contracts`

- [ ] **P2.16-T** 单元测试：为 `AssertionSignal` guardrail 与首批迁移样本建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptAssertionSignalValidationTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp`
  - 测试场景：1. validator 必须拦截 synthetic sample 中的 `TestTrue(..., true)`、`TestEqual(...); return true;` 与“行为名义但 compile-only”三类低信号样式；2. `ControlFlow.NeverVisited` 迁移后要么显式验证 diagnostics/执行结果，要么改成受约束的 `CompileOnly` contract，不再允许空断言；3. `Functions.Constructor`、`ObjectPtrCompat`、`SoftObjectPtrCompat` 的尾部结果必须通过 `bPassed` 或等价显式 contract 传播，不能再靠 Automation 断言副作用决定最终绿灯。
  - 测试命名：`Angelscript.TestModule.Validation.AssertionSignal.RejectsConstantTrueAssertions`、`Angelscript.TestModule.Validation.AssertionSignal.RejectsTailReturnTrueAfterAssertions`、`Angelscript.TestModule.Validation.AssertionSignal.RepositoryTargetsUseExplicitOutcome`
  - 隔离方式：validator 测试使用 repo-local source snippet / repository target scan；迁移后的代表运行时 case 继续使用各自现有 `ASTEST_*` 引擎隔离
- [ ] **P2.16-T** 📦 Git 提交：`[TestInfrastructure] Test: verify assertion signal guardrail`

### Phase 3 追加：按职责拆分超长测试 megafile，并把重复 setup 收口成共享 harness

- [ ] **P3.4** 拆分 `InterfaceAdvancedTests.cpp` 与 `ExecutionTests.cpp` 的混合 carrier，避免继续把多主题回归堆进单文件
  - 当前两个代表性文件都已经越过“单文件单职责”的阈值，而且重复 setup 模式已经肉眼可见：`InterfaceAdvancedTests.cpp` 同时承载继承、缺方法、GC、hot reload、dispatch 等 9 个主题，并在文件内反复复制 `CompileScriptModule + FActorTestSpawner + ResetSharedCloneEngine`；`ExecutionTests.cpp` 则把参数封送、context 生命周期、nested call、range 求值等不同问题压进同一文件，并多次复制 `BuildModule -> GetFunctionByDecl -> CreateContext -> Prepare -> Execute -> Release`。
  - 本项要先做 file-level carrier 收敛，再谈继续补 coverage：1. 把 `InterfaceAdvancedTests.cpp` 按 `Hierarchy` / `Lifecycle` / `Validation` 三类拆开，并抽出共享的 interface scenario harness，统一承接 compile/spawn/reset 责任；2. 把 `ExecutionTests.cpp` 至少拆成 `ArgumentMarshalling` 与 `ContextLifecycle` 两组，让“参数传递问题”和“context 状态问题”不再共用一个 megafile；3. 为拆分后的组织增加 repository-level validation，阻止这两条家族线重新涨回 500 行以上或再次把多个主题混回一个 carrier。
  - 这里不重复 `Plan_TestModuleStandardization.md` 的目录命名主线，也不提前做 `Arch-MS-09` 的整插件搬迁；目标只是先把已经进入 `AngelscriptTest` 的高频回归文件压回可维护尺度，让后续 coverage 补充有明确落点。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/LanguageFeatures_TestGaps.md` — "`Issue-3`：`InterfaceAdvancedTests.cpp` 803 行，混合 inherited interface / GC / hot reload / dispatch 等 9 个场景，且反复复制 `CompileScriptModule`、`FActorTestSpawner`、`ResetSharedCloneEngine`；`Issue-39`：`ExecutionTests.cpp` 533 行，把参数封送、context 生命周期、模块管理问题压在同一文件里"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-09`：`AngelscriptTest` 已经同时承载 regression、example、learning、validation 多种 surface，说明回归代码本身更需要明确的局部 owner，而不是继续放任单文件混装多个 carrier"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：回归 case 装在什么载体里本身就是测试基础设施 contract；carrier 越模糊，repo-owned runner 的可维护性越差"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` 当前共 `803` 行，且 L74、L159、L219、L324、L426、L488、L601、L689 重复 `ResetSharedCloneEngine(Engine)`，L77、L222、L264、L369、L429、L491、L604、L692 重复 `CompileScriptModule(...)`，L123、L289、L460、L562、L647、L775 重复 `FActorTestSpawner`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` 当前共 `533` 行，且 L19-L47、L75-L113、L129-L168、L184-L225、L255-L315、L400-L438、L488-L527 多次重复 `BuildModule(...) -> GetFunctionByDecl(...) -> Engine.CreateContext() -> Context->Prepare(...) -> Context->Execute() -> Context->Release()` 样板。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceHierarchyTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceLifecycleTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceValidationTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptInterfaceScenarioHarness.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptInterfaceScenarioHarness.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionArgumentTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionContextLifecycleTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptExecutionContextHarness.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptExecutionContextHarness.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptTestOrganizationValidationTests.cpp`（新建）
- [ ] **P3.4** 📦 Git 提交：`[TestInfrastructure] Refactor: split oversized interface and execution test carriers`

- [ ] **P3.4-T** 单元测试：为拆分后的 file-level carrier 与共享 harness 建立组织回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptTestOrganizationValidationTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptInterfaceScenarioHarnessTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptExecutionContextHarnessTests.cpp`（新建）
  - 测试场景：1. repository validator 必须确认 `InterfaceAdvancedTests.cpp` / `ExecutionTests.cpp` 不再作为 500+ 行 megafile 承载多主题回归，且拆分后的目标文件存在；2. interface harness 自测要覆盖一次最小 `CompileScriptModule + Spawn + ResetSharedCloneEngine` 流程，证明共享 harness 真能承接原文件复制的 setup/teardown；3. execution harness 自测要覆盖一次 `BuildModule + GetFunctionByDecl + CreateContext + Prepare + Execute + Release` 的 happy path 与 cleanup，确保文件拆分后不是把重复样板换个文件继续复制。
  - 测试命名：`Angelscript.TestModule.Validation.Organization.InterfaceAndExecutionFilesStayScoped`、`Angelscript.TestModule.Shared.InterfaceHarness.BasicCompileSpawnReset`、`Angelscript.TestModule.Shared.ExecutionHarness.BasicPrepareExecuteRelease`
  - 隔离方式：organization validator 走 repository-local file scan；两个 harness 自测分别复用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` 与 isolated context lifecycle
- [ ] **P3.4-T** 📦 Git 提交：`[TestInfrastructure] Test: verify test carrier decomposition guardrail`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P2.16` assertion signal / compile-only contract | `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptAssertionSignalValidationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` | 拦截 `TestTrue(..., true)`、拦截 `TestEqual(...); return true;`、强制 compile-only / 显式 `bPassed` contract | P1 |
| `P3.4` megafile carrier decomposition | `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptTestOrganizationValidationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptInterfaceScenarioHarnessTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptExecutionContextHarnessTests.cpp` | `Interface` / `Execution` 文件职责拆分、共享 harness 真正接管重复 setup、仓内 guardrail 阻止 megafile 回潮 | P2 |

### 本轮追加风险

1. **`AssertionSignal` validator 一旦直接扫描全仓，短期内会把大量历史 `return true` / 空断言样板一起打红**：实施时应先锁定本轮已源码验证的代表文件并分批扩面，否则 guardrail 本身会先被历史债务淹没。
2. **把 compile-only case 改成显式 `CompileOnly` 或真实执行断言后，部分测试名称与历史统计口径会发生变化**：这属于修正测试语义，不应再把“能编译”继续包装成“已验证运行时行为”。
3. **拆分 `Interface` / `Execution` megafile 会改变 blame 与文件路径，但 automation 名称不应变化**：执行时要明确“迁文件不迁 test id”，避免把组织重构误演成 discover/filter 行为变更。

---

## 深化 (2026-04-09 07:03:57)

- `Documents/Plans/Plan_TestEngineIsolation.md` 已承接 `primaryContext`、clone 生命周期与多 full-engine 可见性主线；本轮只追加 `AngelscriptTest` 侧仍缺的 fast-path 入口与 test-control-plane owner，不重复已有 runtime 隔离计划。
- `P3.1` 已关注 runtime-capable fixture/support façade 拆分；以下 `P3.5` 只处理 discovery / coverage / commandlet 的控制面 authority，不重复 fixture owner 主线。

### Phase 2 追加：把 direct fast-path 入口纳入仓内 regression surface

- [ ] **P2.17** 为 `UASFunction::OptimizedCall_*` 建立 direct-call harness，停止让 fast-path 继续躲在 `RuntimeCallEvent()` 后面
  - 当前 `AngelscriptTest` 的执行夹具只覆盖两类入口：`ExecuteGeneratedIntEventOnGameThread()` 在 `UASFunction` 分支硬编码 `RuntimeCallEvent()`，`AngelscriptExecutionTests.cpp` 则只走 `asIScriptContext::Prepare/Execute()`。这意味着 production 里同样暴露给脚本函数的 `OptimizedCall_*` 公开包装器仍然没有任何 test-owned carrier，`P2.1` 虽然准备统一 helper contract，但还没有把“直接调用 fast-path wrapper”本身建成独立回归面。
  - 本项应新增专门的 `OptimizedCall` harness，而不是继续把快路径塞回慢路径 helper：1. 共享 compile/spawn helper 负责拿到具体 `UASFunction_*`；2. harness 直接调用 `OptimizedCall()`、`OptimizedCall_ByteReturn()`、`OptimizedCall_FloatArg()`、`OptimizedCall_DoubleArg()`、`OptimizedCall_RefArg()`、`OptimizedCall_RefArg_ByteReturn()`；3. 同一脚本场景再跑一次 `RuntimeCallEvent()` 对照，防止未来 wrapper 参数封送、返回值或对象副作用与慢路径悄悄分叉。
  - 这条深化不重复 `Plan_TestCoverageExpansion.md` 的功能面扩张，也不和 `P2.1` 的 owner contract 打架：`P2.1` 负责把 helper 暴露出 fast-path 入口，本项负责把入口本身钉成仓内 regression。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — "`A-33`：`OptimizedCall_*` 快路径绕过 `CheckGameThreadExecution()`，线程边界风险没有直接守门；`D-23`：现有自动化只走 `RuntimeCallEvent()`，没有任何 fast-path 回归"
    - [C] `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` — "`NewTest-34`：全仓 `Plugins/Angelscript/Source/AngelscriptTest` 对 `OptimizedCall_*` 为 0 命中，建议新增 `AngelscriptASFunctionOptimizedCallTests.cpp` 直接验证参数/返回值语义"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：repo-owned test operator surface 必须覆盖真实公开入口，而不是只验证 wrapper 后面的慢路径"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` L434-L460 在 `UASFunction` 分支只调用 `RuntimeCallEvent()`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptExecutionTests.cpp` L29-L47、L85-L105、L139-L160、L194-L217 也都只走 `GetFunctionByDecl(...) -> Prepare() -> Execute()`；当前对 `Plugins/Angelscript/Source/AngelscriptTest/` 执行 `rg -n "OptimizedCall_"` 仍为 0 命中，说明 fast-path wrapper 仍未进入测试模块的直接回归面。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionOptimizedCallTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptASFunctionOptimizedCallHarness.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptASFunctionOptimizedCallHarness.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P2.17** 📦 Git 提交：`[TestInfrastructure] Feat: add direct optimized call regression harness`

- [ ] **P2.17-T** 单元测试：为 fast-path direct-call 与 slow-path parity 建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionOptimizedCallTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptASFunctionOptimizedCallHarnessTests.cpp`（新建）
  - 测试场景：1. `UOptimizedCallTarget` 上的 `Ping()`、`GetByteCode()`、`StoreFloat(float)`、`StoreDouble(double)`、`BumpRef(int&)`、`BumpRefAndReturn(int&)` 六类脚本函数分别通过对应 `OptimizedCall_*` wrapper 直接调用，参数、返回值和对象观测属性都必须精确匹配脚本定义；2. 同一函数再走一次 `RuntimeCallEvent()` 对照，确认 fast-path 与慢路径的最终结果一致；3. harness 在拿到错误的 `UFunction` 形状或错误 wrapper 类型时，必须输出明确 diagnostics，而不是静默退回 `RuntimeCallEvent()`。
  - 测试命名：`Angelscript.TestModule.ClassGenerator.ASFunction.OptimizedCallWrappersPreserveArgumentsAndReturnValues`、`Angelscript.TestModule.ClassGenerator.ASFunction.OptimizedCallWrappersMatchRuntimeCallEvent`、`Angelscript.TestModule.Shared.ASFunctionOptimizedCallHarness.RejectsUnexpectedFunctionShape`
  - 隔离方式：`ASTEST_CREATE_ENGINE_SHARE_FRESH()` + test-owned `UOptimizedCallTarget`；parity 场景同一轮内分别执行 fast-path 与 slow-path，避免跨测试共享状态污染
- [ ] **P2.17-T** 📦 Git 提交：`[TestInfrastructure] Test: verify optimized call wrapper contracts`

### Phase 3 追加：把 test-control-plane owner 从 runtime 拉回显式 framework seam

- [ ] **P3.5** 为 `CodeCoverage` / test discovery / commandlet 建立 `IAngelscriptTestServices` seam，停止让测试控制面继续漂在 `AngelscriptRuntime`
  - 当前 `AngelscriptTest` 更像 case carrier 而不是控制面 owner：`Build.cs` 只是把 runtime/editor 依赖叠上来，模块 `StartupModule()` / `ShutdownModule()` 只记日志，`DumpTests` 也只是把 `CodeCoverage.csv` 当成一个预期 `Skipped` 的表项检查。与此同时，分析已明确指出 `UAngelscriptTestSettings`、`CodeCoverage` hook、test discovery 和 test commandlet 仍直接挂在 `AngelscriptRuntime` 上，导致 teardown、late-init 和 artifact owner 都没有单一 authority。
  - 本项应先引入最小 `IAngelscriptTestServices` seam 或等价 `AngelscriptTestFramework` owner，再逐步迁移 `Testing/AngelscriptTestSettings.*`、`Testing/DiscoverTests.*`、`Testing/UnitTest.*`、`Testing/IntegrationTest.*`、`CodeCoverage/*` 与 `Core/AngelscriptTestCommandlet.*`。`AngelscriptRuntime` 只查询“有没有 test service / coverage service”，不再直接 new recorder 或直接读 test settings；`AngelscriptTest` 则退回为纯 automation case + fixture/UHT coverage 类型宿主。
  - 迁移时必须把兼容层作为一等任务：保留现有 config 节、automation 名称、commandlet 入口和 repo runner 的 artifact 路径，通过 redirect / adapter 把旧 `UAngelscriptTestSettings` 映射到新 owner，避免把控制面重构演成现有 CI 或脚本入口的破坏性改名。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — "`A-10` / `A-18` / `A-25`：`CodeCoverage` 与相关 delegate hook 未纳入 engine teardown，runtime 当前会累积悬空回调和历史 recorder"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-23`：test / coverage / commandlet authority 被拆在 `AngelscriptRuntime` 与 `AngelscriptTest` 两侧，建议引入 `IAngelscriptTestServices` / `AngelscriptTestFramework` 作为单一 owner"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator` / `D9-ArtifactOwner`：当前仓库的优势是 repo-owned runner 与 machine-readable summary，测试控制面不应继续停留在 runtime 私有角落"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs` L23-L49 仅把 `AngelscriptRuntime` 设为公共依赖、editor 依赖按构建条件叠加，没有独立 framework owner；`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp` L9-L16 的 `StartupModule()` / `ShutdownModule()` 仅记录日志；`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp` L69-L92 只把 `CodeCoverage.csv` 视为 `Skipped` 摘要项，没有任何 coverage hook / discovery / commandlet owner 的生命周期合同测试。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTestSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/DiscoverTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTest.Build.cs`、`Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp`、`Plugins/Angelscript/Source/AngelscriptTestFramework/`（新建）
- [ ] **P3.5** 📦 Git 提交：`[TestInfrastructure] Refactor: extract test control plane into framework seam`

- [ ] **P3.5-T** 单元测试：为 test-control-plane seam 与 coverage owner 建立 contract 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptTestControlPlaneTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCoverageLifecycleTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`
  - 测试场景：1. framework seam 必须保留 legacy config surface，旧配置项仍能驱动 discovery / coverage 开关，且 automation 名称不变化；2. 创建并销毁 full-engine test service 后，coverage hook 的注册计数或等价生命周期观测必须回到进入前基线，不再留下重复 hook / 悬空 recorder；3. test commandlet 与 discovery 入口迁到新 owner 后，repo runner 仍能拿到同样的 machine-readable artifact 路径和退出语义，`DumpSummary` 对 `CodeCoverage` 的状态也要随新 owner 从单纯 `Skipped` smoke 升级为真实 owner contract。
  - 测试命名：`Angelscript.TestModule.Core.TestControlPlane.LegacyConfigRedirectPreservesAutomationSurface`、`Angelscript.TestModule.Core.CoverageLifecycle.ReleasesHooksOnShutdown`、`Angelscript.TestModule.Core.TestControlPlane.CommandletAndDiscoveryRunThroughFrameworkOwner`
  - 隔离方式：优先使用可注入 service test double / hook counter seam 与 unique artifact root；避免再把控制面回归写成仅依赖日志文本的 smoke
- [ ] **P3.5-T** 📦 Git 提交：`[TestInfrastructure] Test: verify test control plane owner contracts`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P2.17` optimized call direct-path harness | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionOptimizedCallTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptASFunctionOptimizedCallHarnessTests.cpp` | direct `OptimizedCall_*` 参数/返回值合同、与 `RuntimeCallEvent()` 的 parity、错误 wrapper 形状诊断 | P1 |
| `P3.5` test-control-plane framework seam | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptTestControlPlaneTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCoverageLifecycleTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp` | legacy config redirect、coverage hook teardown、framework-owned commandlet/discovery artifact 合同 | P2 |

### 本轮追加风险

1. **`OptimizedCall_*` direct-path 回归一旦补上，可能立刻把 `A-33` 这类真实 runtime 快路径问题打红**：这是预期中的“暴露真实入口”，不应再用慢路径 helper 假绿去掩盖。
2. **把测试控制面从 runtime 抽到 `TestFramework` seam 会同时触碰 config、commandlet、artifact 和 automation 名称**：必须用 redirect / adapter 保持外部入口不变，否则仓内 runner 与现有文档会先被结构重构打断。
3. **coverage 生命周期如果没有额外的 service seam 或 hook counter，可观察性会继续不足**：届时测试很容易再次退化成“看日志/看 dump 表”的弱 smoke，无法真正证明 teardown 已恢复基线。

---

## 深化 (2026-04-09 07:13:26)

- 本轮复核时未在工作区定位到 `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md`；以下新增条目只基于 [A] / [C] / [D] / [E] 与 `Plugins/Angelscript/Source/AngelscriptTest/` 实码核验，不补造 [B] 来源。
- 重叠检查已覆盖 `Documents/Plans/Plan_TestEngineIsolation.md`、`Documents/Plans/Plan_TestCoverageExpansion.md`、`Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md` 与 `Documents/Plans/Plan_GlobalVariableAndCVarParity.md`；本轮只补 `AngelscriptTest` 自身的 guardrail / fixture contract，不重复 runtime 主线、功能面覆盖扩张或 Editor 私有行为计划。

### Phase 1 追加：继续切断隐藏前置与路径耦合

- [ ] **P1.22** 为 `SourceMetadata` / `SourceNavigation` 引入 `anchor-based file-backed fixture`，停止把固定脚本路径和手写行号当 contract
  - 承接 `P2.3` / `P2.6` 已规划的 file-backed compile owner，本项只补“正向 source metadata 断言如何生成期望值”的薄弱层，不重复 `FromMemory` / `FromDisk` contract 本身。当前 `SourceMetadataCompat` 和 `SourceNavigation.Functions` 都在验证 `GetSourceFilePath()` / `GetSourceLineNumber()`，但一个把脚本固定写到 `Script/Automation/RuntimeSourceMetadataBindingsTest.as`，另一个在 production-like engine 下固定使用 `RuntimeFunctionNavigationTest` / `RuntimeFunctionNavigationTest.as`，并把 `6` 直接写死成期望行号；测试成功与否仍被脚本排版、旧文件残留和同名模块碰撞绑在一起。
  - 具体实现应在 `Shared/` 层新增统一 fixture：1. 为每次用例生成唯一 `ModuleName`、`RelativeScriptFilename` 和落盘路径；2. 用 marker/comment token 或等价 anchor 从脚本文本动态计算 declaration 所在行号；3. 把“实际 path + 期望 line + cleanup 句柄”回传给测试代码，避免继续在脚本里手写魔法数字或硬编码固定工程路径。
  - 迁移目标优先锁定 `Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` 与 `Editor/AngelscriptSourceNavigationTests.cpp` 两个正向 metadata 载体：前者负责 `UClass/UFunction` source metadata，后者负责 `UASFunction` 导航 contract。两边都应改成“fixture 生成 path/line，测试只校验绑定结果与 fixture 返回值一致”。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — "`Issue-63`：`SourceMetadataCompat` 把脚本函数行号硬编码成 `6`；`Issue-84`：同一用例把源码写到固定工程路径，存在并发/重入污染"
    - [C] `Documents/AutoPlans/TestCoverage/EditorAndTools_TestGaps.md` — "`Issue-20` / `Issue-24`：`SourceNavigation.Functions` 落盘后只丢模块不删文件，且在 production-like engine 上复用固定 module/file 名"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：repo-owned runner 既要定义标准跑法，也要拥有隔离目录与稳定夹具边界"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` L202-L209 把脚本固定写到 `Script/Automation/RuntimeSourceMetadataBindingsTest.as`，L243 直接断言 `Func.GetSourceLineNumber() != 6`；`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` L41-L50 固定 `RuntimeFunctionNavigationTest` / `RuntimeFunctionNavigationTest.as`，L43-L46 teardown 只 `DiscardModule()`，L75-L76 又把 `SourceFilePath` / `SourceLineNumber` 当成固定字面值断言。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedSourceMetadataFixture.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedSourceMetadataFixture.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`
- [ ] **P1.22** 📦 Git 提交：`[TestInfrastructure] Refactor: add anchor-based source metadata fixture`

- [ ] **P1.22-T** 单元测试：为 `anchor-based file-backed fixture` 建立唯一性、行号解析与 cleanup 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedSourceMetadataFixtureTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`
  - 测试场景：1. 同一进程内连续创建两次 fixture 时，`ModuleName`、`RelativeScriptFilename` 与实际磁盘路径都必须唯一，且不会互相命中旧模块；2. 在脚本文本前插入空行或注释后，anchor 解析出的期望行号会同步变化，但 `SourceMetadataCompat` / `SourceNavigation.Functions` 仍保持绿色；3. fixture 退出后脚本文件被删除，且只丢弃本轮生成的 owned module，不误删外部同名状态。
  - 测试命名：`Angelscript.TestModule.Shared.FileBackedSourceMetadataFixture.GeneratesUniqueOwnedPathAndModule`、`Angelscript.TestModule.Shared.FileBackedSourceMetadataFixture.ComputesExpectedLineFromAnchor`、`Angelscript.TestModule.Shared.FileBackedSourceMetadataFixture.CleansOwnedFileAndModule`
  - 隔离方式：`FAngelscriptEngineScope` + fixture 自带 unique path lease；禁止再依赖固定 `Script/Automation` 文件名或共享 production module slot
- [ ] **P1.22-T** 📦 Git 提交：`[TestInfrastructure] Test: verify anchor-based source metadata fixture`

- [ ] **P1.23** 为 AngelScript GC internal tests 引入 `unique probe-type + isolated engine fixture`，切断引擎级 object type 注册前置
  - 这条线不重复 runtime 侧“是否要补 production GC driver”的主修复，只收口测试载体本身。当前仓内真正直接驱动 AngelScript `GarbageCollect()` 的，是 `AngelscriptGCInternalTests.cpp` 里的两条 internal tests；既然这两条用例就是 GC regression 的主要守门网，就不能再让它们依赖共享引擎上残留的 `GCProbeObject` 注册状态。
  - 实现上应把 `RegisterGCProbeType()` 升级成 test-owned fixture：1. type 名称按测试实例生成唯一后缀，不再硬编码全局 `GCProbeObject`；2. 即便遇到“同名 type 已存在”，也要显式验证 GC behaviours 与当前 `GGCProbeScriptEngine` 是否匹配，而不是直接 `return true`；3. `ManualCycleCollection` 与 `CycleDetection` 迁到 `FAngelscriptTestFixture` 的 isolated mode、`ASTEST_CREATE_ENGINE_SHARE_FRESH()` 或等价独占引擎入口，确保 object type 注册不跨用例复用。
  - 迁移后应把 probe-type ownership 从测试函数体里抽离出来，让 GC tests 自己只表达“创建循环对象 -> release -> detect/collect -> 统计回归”，不再夹带共享类型注册的历史前置。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — "`C-02`：runtime 关闭 `asEP_AUTO_GARBAGE_COLLECT` 后，仓内只有 internal GC tests 在手动驱动 `GarbageCollect()`，说明这组用例本身就是 GC regression 的主载体"
    - [C] `Documents/AutoPlans/TestCoverage/LanguageFeatures_TestGaps.md` — "`Issue-65`：`RegisterGCProbeType()` 用固定名字复用 `GCProbeObject`，会把旧注册状态当成当前测试前置"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9` 的 build participation 结论：验证代码既然已经进入插件 build graph，就应具备稳定、可复跑的 fixture owner，而不是依赖运行时私有历史状态"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` L199-L228 先 `GetTypeInfoByName("GCProbeObject")`，命中后直接 `return true`，随后所有 behaviours 仍固定注册在字符串字面值 `GCProbeObject` 上；同文件 L345-L349 与 L380-L384 的两条核心 GC 用例仍在 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 下直接调用这套 helper，说明 object type 注册状态仍可跨测试共享。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGCProbeFixture.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGCProbeFixture.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp`
- [ ] **P1.23** 📦 Git 提交：`[TestInfrastructure] Refactor: isolate GC probe type registration per test`

- [ ] **P1.23-T** 单元测试：为 GC probe fixture 建立唯一 type 名、behaviour 校验与 cycle regression 自测
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGCProbeFixtureTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp`
  - 测试场景：1. 同一进程内连续两次创建 probe fixture 时，生成的 type 名称不同且都能拿到完整 GC behaviours；2. fixture 遇到 foreign/stale probe type 时必须报明确失败，而不是静默吃掉旧注册；3. `ManualCycleCollection` 与 `CycleDetection` 迁移后仍能在 isolated engine 下完成 `detect + collect`，并保证 `FGCProbeObject::LiveCount == 0`。
  - 测试命名：`Angelscript.TestModule.Shared.GCProbeFixture.GeneratesUniqueTypePerRun`、`Angelscript.TestModule.Shared.GCProbeFixture.RejectsStaleBehaviourRegistration`、`Angelscript.TestModule.Internals.GC.IsolatedProbeFixturePreservesCycleCollectionContracts`
  - 隔离方式：优先使用 isolated full/fresh engine；禁止继续在 `SHARE_CLEAN` 上复用引擎级 object type 注册面
- [ ] **P1.23-T** 📦 Git 提交：`[TestInfrastructure] Test: verify GC probe fixture isolation`

### Phase 2 追加：把隐式 macro 约束升级为仓内 guardrail

- [ ] **P2.18** 为 shared compile carrier 增加 `macro-owner validator`，拦截双重 `ASTEST_CREATE_ENGINE_SHARE()` 与无 owner 的固定 module slot
  - `P1.1` / `P1.20` 已经在 helper 与 macro contract 上规划了 cleanup / owner 语义，但当前仓内仍缺一条可执行 guardrail 去阻止旧反模式再次长回来。`CompilerPipeline` 的 8 个 end-to-end 用例至今仍重复声明两次 `ASTEST_CREATE_ENGINE_SHARE()`；`AngelscriptEngineBindingsTests.cpp` 则继续在 `ASTEST_BEGIN_SHARE` 下用固定 module 名 `BuildModule(...)`，把共享 singleton 当成长期 module registry 使用。单靠人工 code review，无法持续拦住这类模式回流。
  - 本项应新增 repository-level validator：扫描 `RunTest()` 体内的 engine create 宏与 compile helper 使用方式，至少覆盖两条规则。第一，单个 `ASTEST_BEGIN_*` / `ASTEST_END_*` 生命周期内只允许一个 engine-create owner，禁止 `EngineOwner + Engine` 这种双重 create 样板。第二，`ASTEST_BEGIN_SHARE*` 下若调用 `BuildModule()` / `CompileAnnotatedModuleFromMemory()` / 同类 compile helper，就必须走 tracked-share lease、unique module helper 或显式 owned cleanup contract，不能继续裸用固定 module 名。
  - 第一批迁移点只锁定目前已源码验证的两个载体：`Compiler/AngelscriptCompilerPipelineTests.cpp` 和 `Bindings/AngelscriptEngineBindingsTests.cpp`。这样 guardrail 一落地就能立刻覆盖仓内现存真违规样本，而不是停留在抽象规则说明。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`Issue-18`：8 个 `CompilerPipeline` 用例重复声明两次 `ASTEST_CREATE_ENGINE_SHARE()`"
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — "`Issue-14`：`AngelscriptEngineBindingsTests.cpp` 的 5 个用例在共享引擎里编译模块却不做任何模块清理"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-58`：规则仍停留在人工评审提示，没有进入可执行 guardrail"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9` 的 build participation / operator 结论：验证载体既然已进入插件 build graph，就应让仓内 validator 在编译期或自动化期直接失败，而不是继续依赖人工记忆"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` L18-L20、L92-L94、L171-L173、L231-L233、L280-L282、L331-L333、L373-L375、L422-L424 全部重复声明 `EngineOwner` 与 `Engine` 两次 `ASTEST_CREATE_ENGINE_SHARE()`；`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` L35-L40、L95-L100、L164-L169、L228-L233、L318-L323 在 `ASTEST_BEGIN_SHARE` 下直接 `BuildModule(...)`，模块名分别固定为 `ASBindingValueTypes`、`ASFNameArrayCompat` 等，源码中没有任何 test-owned cleanup lease。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroOwnershipValidationTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P2.18** 📦 Git 提交：`[TestInfrastructure] Refactor: add macro ownership validation guardrail`

- [ ] **P2.18-T** 单元测试：为 macro-owner validator 建立负样本、正样本与仓内样本回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroOwnershipValidationTests.cpp`（新建）
  - 测试场景：1. validator 必须拒绝单个 `RunTest()` 里重复两次 `ASTEST_CREATE_ENGINE_SHARE()` 的样本；2. validator 必须拒绝 `ASTEST_BEGIN_SHARE` 下直接 `BuildModule()` 且未声明 owned module lease/cleanup 的样本；3. 只读 shared helper 或已迁到 tracked-share fixture 的正样本必须通过，避免把 read-only `SHARE` 全部打红。
  - 测试命名：`Angelscript.TestModule.Validation.MacroOwnership.RejectsDuplicateSharedEngineCreate`、`Angelscript.TestModule.Validation.MacroOwnership.RejectsSharedCompileWithoutOwnedModuleLease`、`Angelscript.TestModule.Validation.MacroOwnership.AllowsReadOnlyOrTrackedSharedTests`
  - 隔离方式：validator 仅扫描 `Plugins/Angelscript/Source/AngelscriptTest/` 仓内源文件；匹配范围限定在 `RunTest()` 体与 macro 生命周期块，避免注释/模板文本造成假阳性
- [ ] **P2.18-T** 📦 Git 提交：`[TestInfrastructure] Test: verify macro ownership validation guardrail`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.22` anchor-based source metadata fixture | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptFileBackedSourceMetadataFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` | 唯一 module/path lease、anchor 行号解析、owned file/module cleanup | P1 |
| `P1.23` GC probe isolated fixture | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGCProbeFixtureTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` | 唯一 probe type、stale behaviour 拒绝、cycle collect regression | P1 |
| `P2.18` macro ownership validator | `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroOwnershipValidationTests.cpp` | 双重 create 拒绝、shared compile owner 校验、正样本白名单 | P2 |

### 本轮追加风险

1. **`anchor-based` line 解析如果直接靠脆弱字符串匹配，仍可能把脚本重排误判成 fixture 失效**：实现时应优先用显式 marker/comment token，而不是继续把 declaration 文本硬编码成搜索关键字。
2. **GC probe 若只改成唯一 type 名但仍留在共享引擎上运行，会把 object type 注册表越积越多**：因此必须和 isolated/fresh engine mode 一起落地，不能只修名字不修 owner。
3. **macro validator 如果只做裸 `regex` 扫描，容易把注释、示例字符串或 helper 定义误判成真实违规**：匹配范围必须限制在 `RunTest()` 体与 `ASTEST_BEGIN_*` 生命周期块，并允许 tracked-share / read-only shared 的显式白名单。

---

## 深化 (2026-04-09 07:21:36)

- 本轮继续复核后，`Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` 仍未在工作区出现；以下新增条目只引用已可读取的 [C] / [D] / [E] 输入，并全部重新对 `Plugins/Angelscript/Source/AngelscriptTest/` 实码做了行号核验。
- 与 `Documents/Plans/Plan_TestCoverageExpansion.md`、`Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md`、`Documents/Plans/Plan_UFunctionReflectiveFallbackBinding.md` 的重叠复查结论不变：本轮只补测试 guardrail / helper owner / artifact 语义，不把 file-watch 功能验证或 broader binding coverage 扩张重复写回本 Plan。

### Phase 1 追加：把 `asIScriptEngine` 全局 namespace 从手写恢复收口成 test-owned guardrail

- [ ] **P1.24** 为 `SetDefaultNamespace()` 建立 scoped namespace guard 与 repository validator，禁止测试体裸改 script-engine 全局 namespace
  - 当前仓内已经不止一条测试直接修改 `asIScriptEngine` 的默认 namespace，但没有统一 owner。`NativeStaticClassNamespace` 明确先切到 `AActor` 再在尾部手写恢复，且恢复结果没有并入最终 pass/fail；`Misc.DuplicateFunction` 与 `Operators.GetSet` 则直接把 namespace 硬重置为空字符串，默认假定进入测试前 global namespace 已经是空；`ASSDK enum type test` 在失败分支手写 `SetDefaultNamespace("")`，但对 restore 本身没有任何断言。它们共同指向的是同一种基础设施缺口：测试可以随手改 script-engine 全局状态，却没有可复用的 snapshot/restore owner，也没有仓内 guardrail 防止未来继续散落裸调用。
  - 本项应新增一个只服务测试层的 `FScopedScriptEngineNamespace` 或等价 helper：构造时快照 `GetDefaultNamespace()`，切入测试需要的 namespace；析构时无条件恢复上一个值，并把 restore 失败提升成明确 diagnostics。第一批迁移点至少覆盖 `Bindings/AngelscriptClassBindingsTests.cpp`、`Angelscript/AngelscriptMiscTests.cpp`、`Angelscript/AngelscriptOperatorTests.cpp` 和 `Native/AngelscriptASSDKTypeTests.cpp`，把这些文件从“手写 `SetDefaultNamespace()` + 假定空基线”改成共享 guard。
  - 仅有 helper 还不够，需要一条 repository-level validator 把规则变成可执行契约：`RunTest()` 中除 guard 实现文件和少数明确白名单外，不再允许直接出现裸 `SetDefaultNamespace(`。这样才能把 [C] 暴露的单点 false green 收口成全仓 guardrail，而不是再留下一批“知道要恢复，但忘了把恢复算进结果”的变体。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — "`Issue-1`：`NativeStaticClassNamespace` 把 `SetDefaultNamespace(\"AActor\")` 的恢复失败排除在最终结果之外，说明测试层当前没有稳定的 namespace owner contract"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-58`：分层/规则仍停留在人工评审提示，没有进入可执行 guardrail；测试基础设施里的全局状态约束也应转成仓内 validator"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：当前仓库的差异化优势是 repo-owned runner + 轻量夹具，因此全局状态 cleanup 也应由 repo-owned contract 收口，而不是继续依赖调用方记忆"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` L433-L446 先 `SetDefaultNamespace("AActor")`，尾部只 `TestTrue(... SetDefaultNamespace("") >= 0)` 且最终仅返回 `bHasFunction`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` L120-L123 在 raw compile-fail helper 里直接 `SetDefaultNamespace("")` 并继续执行，没有恢复前态；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp` L57-L59 同样直接重置为空 namespace；`Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKTypeTests.cpp` L395-L407 手写 `foo -> ""` 切换，但只在失败路径里裸 restore，没有统一 snapshot/restore owner。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScriptEngineStateGuards.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScriptEngineStateGuards.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptScriptEngineStateValidationTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKTypeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P1.24** 📦 Git 提交：`[TestInfrastructure] Refactor: scope script engine namespace mutations`

- [ ] **P1.24-T** 单元测试：为 namespace guard 与裸调用 validator 建立自测
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScriptEngineStateGuardTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptScriptEngineStateValidationTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp`
  - 测试场景：1. 先把 `ScriptEngine` 的默认 namespace 置为非空值，再进入 `FScopedScriptEngineNamespace("AActor")`，退出后必须恢复到进入前的原 namespace，而不是一律重置为空字符串；2. guard 嵌套与早退路径都必须恢复上一层 snapshot，并把 restore 失败显式上报到 `RunTest()` 结果；3. repository validator 必须拒绝 `RunTest()` 里的裸 `SetDefaultNamespace(`，但允许 guard 实现文件与迁移后的正样本通过；4. `NativeStaticClassNamespace` 迁移后必须把“恢复成功”纳入最终 `bPassed`，不再允许 `StaticClass` 查找成功就误报绿灯。
  - 测试命名：`Angelscript.TestModule.Shared.ScriptEngineState.NamespaceGuardRestoresPreviousNamespace`、`Angelscript.TestModule.Shared.ScriptEngineState.NamespaceGuardRestoresAfterEarlyExit`、`Angelscript.TestModule.Validation.ScriptEngineState.RejectsBareSetDefaultNamespace`、`Angelscript.TestModule.Bindings.NativeStaticClassNamespace.RestoreFailureWouldFailTest`
  - 隔离方式：`FAngelscriptEngineScope` + guard 自带 previous-namespace snapshot；validator 仅扫描 `Plugins/Angelscript/Source/AngelscriptTest/`，并对白名单实现文件做精确豁免
- [ ] **P1.24-T** 📦 Git 提交：`[TestInfrastructure] Test: verify script engine namespace guard`

### Phase 2 追加：把 generated artifact 的 skipped-reason 汇总从“总和 smoke”收口成按桶语义合同

- [ ] **P2.19** 深化 `P2.14` 的 generated artifact parser，补齐 `SkippedReasonSummary` 与 `SkippedEntries` 的 `FailureReason -> Count` 精确对账
  - `P2.14` 已经计划解决 generated-artifact 的路径解析、quoted CSV 和结构化 parser，本项只补其中仍然裸奔的一块语义空洞：当前 `SkippedReasonSummaryCsvOutput` 只把 summary 里的 `SkippedCount` 全部求和，然后和 `SkippedEntries.csv` 的行数做一次总和比较。这样即便生成器把多个 `FailureReason` 错并到同一个桶、把 count 记到错误 reason、或统一改坏 reason 文本，只要总和保持一致，测试仍会稳定绿灯。
  - 因此这里不再新增另一套 artifact 载体，而是要求沿用 `P2.14` 的结构化 CSV parser，把 `SkippedEntries.csv` 和 `SkippedReasonSummary.csv` 都 materialize 成 `TMap<FString, int32>` 或等价 typed rows，再做 key 集合与每个 bucket count 的逐项比对。quoted reason、带逗号的 reason 和 summary 缺失 bucket 也都必须进入同一条语义合同，而不是拆回若干字符串 contains smoke。
  - 这项深化能把 generated function table 报表从“格式能读”提升到“统计语义正确”。它既不重复 `P2.14` 的 resolver/path 主线，也不把 `GeneratedFunctionTable` 扩成新的业务 coverage 面，只是在现有 artifact parser 上补足 CI/Agent 真正会消费的聚合语义。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-55`：`SkippedCsvOutput` / `SkippedReasonSummaryCsvOutput` 仍用裸逗号 split，quoted `FailureReason` 会误报；`Issue-73`：`SkippedReasonSummaryCsvOutput` 只校验总和，没有逐个 `FailureReason` 分桶对账"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-ArtifactOwner` / `D9-Operator`：当前仓库的测试价值在 repo-owned machine-readable artifact，因此聚合报表必须保护可消费语义，而不只是保护文本存在"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` L671-L703 的 `SkippedCsvOutput` 仍在逐行 `ParseIntoArray(TEXT(\",\"), false)` 并强断言 4 列；L706-L749 的 `SkippedReasonSummaryCsvOutput` 同样裸 split summary 行，L730-L748 只累计 `SummedSkippedCount` 并和 `SkippedLines.Num() - 1` 比较，没有任何 per-reason bucket parity 断言。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableArtifactTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGeneratedArtifactParsers.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGeneratedArtifactParsers.cpp`（新建）
- [ ] **P2.19** 📦 Git 提交：`[TestInfrastructure] Refactor: add skipped-reason bucket parity to generated artifacts`

- [ ] **P2.19-T** 单元测试：为 `SkippedReasonSummary` 的按桶语义建立结构化回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableArtifactTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGeneratedArtifactParserTests.cpp`（新建）
  - 测试场景：1. 真实 artifact 路径下，`SkippedEntries.csv` 与 `SkippedReasonSummary.csv` 解析成的 `FailureReason -> Count` map 必须 key 集合完全一致，且每个 bucket count 精确相等；2. parser 必须正确处理带逗号、引号和转义的 synthetic `FailureReason` 样本，不再把合法 quoted CSV 误拆成多列；3. 当 summary 故意缺失 bucket、合并错误 bucket 或 count 漂移时，parser/helper 要返回明确 mismatch diagnostics，而不是只给“总和不等”或 silent pass。
  - 测试命名：`Angelscript.TestModule.Engine.GeneratedFunctionTable.Artifacts.SkippedReasonSummaryMatchesEntryBuckets`、`Angelscript.TestModule.Engine.GeneratedFunctionTable.Artifacts.ParsesQuotedFailureReasonBuckets`、`Angelscript.TestModule.Engine.GeneratedFunctionTable.Artifacts.ReportsReasonBucketMismatch`
  - 隔离方式：真实 artifact 回归复用 `P2.14` 的 generated-artifact locator；synthetic parser 测试直接喂内存 CSV 文本，不依赖宿主 UHT 输出布局
- [ ] **P2.19-T** 📦 Git 提交：`[TestInfrastructure] Test: verify skipped-reason summary bucket parity`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.24` script-engine namespace guard | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScriptEngineStateGuardTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptScriptEngineStateValidationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` | previous namespace snapshot/restore、早退恢复、裸 `SetDefaultNamespace` validator、恢复失败并入最终结果 | P1 |
| `P2.19` skipped-reason bucket parity | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableArtifactTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptGeneratedArtifactParserTests.cpp` | `FailureReason -> Count` 精确对账、quoted CSV 解析、bucket mismatch diagnostics | P1 |

### 本轮追加风险

1. **namespace validator 一旦落地，会先暴露一批历史测试对“默认 namespace 本来就是空”的隐含假设**：这不是新增约束，而是把 process-global script-engine 状态的顺序依赖显式化；需要用 guard + 少量白名单过渡，而不是把规则回退掉。
2. **`SkippedReasonSummary` 改成按桶精确对账后，任何 summary 文本规范化差异都会被更早打红**：这属于把 machine-readable artifact 的真实语义前置成回归合同，短期可能增加失败数，但能显著降低“总和没变就假绿”的风险。

---

## 深化 (2026-04-09 07:28:54)

- 本轮复查 `Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md`、`Documents/Plans/Plan_TestCoverageExpansion.md` 与 `Documents/Plans/Plan_TestEngineIsolation.md` 后，继续把范围限制在 `AngelscriptTest` 自身的 bind assertion / helper contract；不重复 runtime bind 业务修复、file-watch 主线或更大范围的功能覆盖扩张。
- `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` 仍未出现在当前工作区；以下新增条目只引用已可读取的 [C] / [E] 输入，并重新对 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` 做了源码核验。

### Phase 1 追加：把 `BindConfig` 的 direct-bind 与 startup 观测从“条目存在”收口成行为/诊断合同

- [ ] **P1.25** 深化 `P1.3` 的 `BindConfig` 断言：把 “recover direct bind” 从 registry smoke 升级为 `direct-not-fallback + runtime behavior` 合同
  - 当前三条 `CanRecoverDirectBind` 用例只证明 `FFuncEntry` 可调度，却没有证明它真的留在 direct bind 主路径。对当前 Angelscript 而言，direct bind 和 reflective fallback 是两条不同 authority：前者是 build/UHT 生成后落到 VM-native 调用面，后者是 runtime copy-back 兜底。如果测试名写的是 “RecoverDirectBind”，断言就不能继续把 reflective fallback 也算绿灯。
  - 本项应把 `OverloadedExportedFunctionsCanRecoverDirectBind`、`InlineDefinitionFunctionsCanRecoverDirectBind`、`InlineOutRefFunctionsCanRecoverDirectBind` 从单一 megafile 的 map-entry smoke 拆成两层合同。第一层是结构合同：明确要求 `IsFunctionEntryBound(*Entry)` 之外，还要断言 `!Entry->bReflectiveFallbackBound`。第二层是行为合同：为 `URuntimeFloatCurveMixinLibrary::GetNumKeys` / `GetTimeRange` / `GetTimeRange_Double` 建立最小执行回归，真正创建 `FRuntimeFloatCurve`、填入 key，再验证返回值与 out-ref 回填，不再只看条目存在。
  - 这项深化顺带落实 `FunctionLibraries_TestGaps.md` 对 megafile 拆分的要求：把 direct-bind 行为测试移到独立文件，例如 `AngelscriptBindConfigDirectBindBehaviorTests.cpp`，而原 `AngelscriptBindConfigTests.cpp` 只保留 bind-config 基建与 registry-level 合同。这样后续再补 representative FunctionLibrary 行为时，不会继续把运行时语义埋进 800 行以上的大文件。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-37`：三条 `BindConfig` “recover direct bind” 用例目前只看 `IsFunctionEntryBound(*Entry)`，会把 reflective fallback 误判成 direct bind 恢复"
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — "`Issue-7`：`RuntimeFloatCurve` 相关用例只检查 direct-bind 注册，不验证返回值和 out-ref 语义；`Issue-24`：`AngelscriptBindConfigTests.cpp` 已膨胀成 700+ 行，FunctionLibrary 行为断言被埋没"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D2` / `D8`：当前 Angelscript 已形成 `FunctionTable + direct bind + reflective fallback` 三轨 authority，且 reflective fallback 的 out/ref copy-back 语义与 generated direct path 明确不同，测试必须把两条路径区分开"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` L175-L179 的 `IsFunctionEntryBound()` 只检查 `FuncPtr.IsBound()` 与 `Entry.Caller.IsBound()`；L727-L751、L773-L799、L821-L847 三条 `CanRecoverDirectBind` 用例最终都只返回 `IsFunctionEntryBound(*Entry)`，没有任何 `bReflectiveFallbackBound` 断言，也没有构造 `FRuntimeFloatCurve` 或执行脚本来验证 `GetNumKeys` / `GetTimeRange` 的实际行为。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigDirectBindBehaviorTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`
- [ ] **P1.25** 📦 Git 提交：`[TestInfrastructure] Refactor: harden direct bind recovery contracts`

- [ ] **P1.25-T** 单元测试：为 `BindConfig` 的 direct-bind 恢复建立 “非 fallback + 真实执行” 回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigDirectBindBehaviorTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
  - 测试场景：1. overload、inline definition 和 inline out-ref 三条 representative entry 都必须同时满足 `IsFunctionEntryBound(*Entry)` 与 `!Entry->bReflectiveFallbackBound`；2. 构造含两个 key 的 `FRuntimeFloatCurve`，脚本或 helper 调用 `GetNumKeys()` 时必须返回 `2`；3. `GetTimeRange(out Min, out Max)` 和 `GetTimeRange_Double(out Min, out Max)` 必须把最小/最大时间正确写回，且 double overload 不得静默丢精度或交换参数顺序；4. 若故意选取只能走 reflective fallback 的负样本，测试 diagnostics 必须明确区分 “条目可调度” 与 “不是 direct bind”。
  - 测试命名：`Angelscript.TestModule.Engine.BindConfig.DirectBind.OverloadStaysOffReflectiveFallback`、`Angelscript.TestModule.Engine.BindConfig.DirectBind.RuntimeFloatCurveGetNumKeysReturnsActualCount`、`Angelscript.TestModule.Engine.BindConfig.DirectBind.RuntimeFloatCurveGetTimeRangeCopiesOutValues`、`Angelscript.TestModule.Engine.BindConfig.DirectBind.RuntimeFloatCurveDoubleRangeKeepsPrecision`
  - 隔离方式：独占 full engine + `FAngelscriptEngineScope`；沿用 `P1.3` 的 bind-state snapshot/restore contract，避免 direct-bind 行为测试再把全局 registry 留脏
- [ ] **P1.25-T** 📦 Git 提交：`[TestInfrastructure] Test: verify direct bind recovery behavior`

- [ ] **P1.26** 深化 `P1.3` 的 startup bind 观测 helper：移除 `check()` 式 fatal 失败，改为可注入、可报告的结构化 observation contract
  - `StartupBindInfoPreservesOrder` 和 `StartupPathMergesDisabledBindNames` 本应是 bind 启动路径的定点回归，但当前它们把 `ObserveStartupBindPass()` 当黑盒 helper 调用；一旦 helper 里的 `CreateTestingFullEngine()` 失败，就不是单测失败，而是整个 automation 批次被 `check()` 直接打断。这与当前仓库强调的 “repo-owned test module + commandlet batch contract + 显式 failure reason” 是反方向。
  - 本项需要把 `ObserveStartupBindPass()` 升级成显式结果结构，例如 `FAngelscriptBindObservationResult { bool bValid; FString FailureReason; FAngelscriptBindExecutionSnapshot Snapshot; }` 或等价 `TOptional` + error channel；helper 内部不再使用 `check()`，而是把 engine 创建失败、snapshot 缺失、invocation count 异常等信息返回给 `RunTest()`。同时给 helper 预留可注入 factory seam，允许测试显式构造 “engine 创建失败” 的负路径，而不必依赖真实崩溃或全局状态污染。
  - 迁移后，`StartupBindInfoPreservesOrder` 与 `StartupPathMergesDisabledBindNames` 应先断言 observation 结果有效，再继续检查 bind order / disabled merge；若 observation 失败，也要留下本用例上下文和具体原因，而不是在 helper 内提前终止。这样本项不是扩张新的 bind 业务覆盖，而是把现有 startup-path regression 从 fatal assert 收口成可批量执行的诊断合同。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`Issue-45`：`ObserveStartupBindPass` 在 helper 里用 `check()`，会把 bind 启动回归升级成整批崩溃"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D6`：当前 Angelscript 的优势之一是 failure reason 显式；`D9`：脚本 discover protocol 与 commandlet exit contract 已闭环，因此测试 helper 不应再把可预期回归升级成进程级 fatal"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` L125-L146 的 `ObserveStartupBindPass()` 直接 `CreateTestingFullEngine(...)` 后 `check(Engine.IsValid())`，没有任何 error channel；L420-L451 与 L454-L490 的两条 startup bind 用例都直接接收返回的 snapshot，只对 invocation/order/disabled set 做断言，没有先验证 observation 自身是否有效或输出失败原因。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigStartupObservationTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptBindConfigTestFixtures.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptBindConfigTestFixtures.cpp`（新建）
- [ ] **P1.26** 📦 Git 提交：`[TestInfrastructure] Refactor: replace fatal startup bind observation helper`

- [ ] **P1.26-T** 单元测试：为 startup bind observation helper 建立成功路径与失败注入回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigStartupObservationTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
  - 测试场景：1. 注入 failing engine factory 时，helper 必须返回 `bValid == false` 与明确 `FailureReason`，并允许 `RunTest()` 继续报告常规自动化失败而不是触发 fatal assert；2. 正常 factory 下，helper 仍能返回 startup snapshot，`StartupBindInfoPreservesOrder` 与 `StartupPathMergesDisabledBindNames` 的既有 order / disabled-set 断言继续成立；3. 当 observation 结果无效时，调用方测试名与失败原因必须同时出现在 diagnostics 中，避免只剩下模糊的 helper-level 报错。
  - 测试命名：`Angelscript.TestModule.Engine.BindConfig.StartupObservation.EngineCreationFailureReportsDiagnostic`、`Angelscript.TestModule.Engine.BindConfig.StartupObservation.ValidSnapshotPreservesOrderAssertions`、`Angelscript.TestModule.Engine.BindConfig.StartupObservation.InvalidResultKeepsCallerContext`
  - 隔离方式：独立 full engine + 可注入 factory seam；失败注入走测试专用 seam，不污染真实 `FAngelscriptEngine::CreateTestingFullEngine()` 全局路径
- [ ] **P1.26-T** 📦 Git 提交：`[TestInfrastructure] Test: verify startup bind observation diagnostics`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.25` direct-bind behavior contract | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigDirectBindBehaviorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` | `direct-not-fallback` 断言、`RuntimeFloatCurve` 实际执行、double out-ref 精度 | P1 |
| `P1.26` startup bind observation diagnostics | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigStartupObservationTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` | helper 失败注入、结构化 `FailureReason`、caller context 保留 | P1 |

### 本轮追加风险

1. **把 `CanRecoverDirectBind` 改成 `!bReflectiveFallbackBound` 后，可能会立刻暴露当前某些 signature 实际仍依赖 reflective fallback 的历史事实**：这不是新回归，而是把 “可调度” 与 “仍在 direct bind 主路径” 两个合同正式拆开；执行时需要保留少量明确的 fallback 负样本，避免把兜底路径误当产品 bug。
2. **`ObserveStartupBindPass()` 从 `check()` 改成结构化失败后，同一批次里会看到更多后续单测继续执行并留下证据**：短期看像是“红灯变多”，本质上是从 fatal 中断改成可诊断批执行，符合 `AngelscriptTest` 作为 commandlet / batch regression carrier 的定位。

---

## 深化 (2026-04-09 08:46:00)

- 已复查 `Documents/Plans/Plan_ASDebuggerUnitTest.md` 与 `Documents/Plans/Plan_TestCoverageExpansion.md`：前者覆盖 debugger 能力面 rollout，后者覆盖业务域新增测试。本轮只深化当前 `Plan_TestInfrastructure.md` 已承接的 debugger helper / operator guardrail，不重复 callstack、variables、data-breakpoint 等更宽功能面扩张。
- `Documents/AutoPlans/DiscoveryPlans/TestInfrastructure_Plan.md` 仍未在当前工作区出现；以下条目仅引用已可读取的 [C] / [E] 输入，并已重新对 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 与 `Shared/` 做源码核验。

### Phase 2 追加：把 debugger helper 的“收消息”和“看 callstack”从 lucky path 收口成显式协议合同

- [ ] **P2.20** 深化 `P2.7` 的 debugger transport helper：用 lossless pending-queue collector 取代 `WaitForMessageType()` 的有损轮询，并把 `RequestDebugDatabase` / handshake 序列纳入协议合同
  - 当前 debugger helper 仍把“等到目标消息”为唯一成功条件：`WaitForMessageType()` 会把所有非目标 envelope 当场吞掉；smoke、breakpoint、stepping 的握手代码也都在 `Session.PumpUntil(...)` 里手写 `ReceiveEnvelope()` 过滤，只要目标消息最终到达，途中多出来的 `HasStopped`、`Diagnostics`、database chunk 或其它 side-channel message 都不会留下证据。这会把真正的协议污染伪装成“偶然还能继续绿”的 lucky path。
  - 本项不重复 `P2.7` 已覆盖的 connect timeout / ready gate / mid-sequence timeout，而是补 helper 语义缺口：1. 给 `FAngelscriptDebuggerTestClient` 增加 pending queue，任何非当前等待目标的 envelope 都必须保留；2. 抽 `WaitForExclusiveMessageType(...)`、`CollectMessagesUntil(...)` 或等价 helper，把“允许哪些消息出现、哪些必须失败、剩余消息是否清空”变成可复用 contract；3. 在现有 smoke 之外补一条 `RequestDebugDatabase` 真实序列回归，验证 `DebugDatabaseSettings -> DebugDatabase* -> DebugDatabaseFinished -> AssetDatabaseInit -> AssetDatabase* -> AssetDatabaseFinished` 的消息顺序和完整性。
  - 这样做的目标不是再扩一层 debugger 功能测试，而是把 operator-facing protocol helper 从“收到想看的那条消息就算过”提升到“消息流本身可验证、可诊断、不会吞包”。只有先收口这层，后续任何 database、evaluate、variables 或 adapter 版本测试才不会继续建立在会丢消息的 transport helper 上。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-12`：`WaitForMessageType()` 是有损 helper，会把所有非目标类型协议消息直接吞掉"
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-20`：启动阶段的手写收包循环会吞掉非目标消息；`NewTest-14`：`RequestDebugDatabase` 仍缺完整消息序列回归"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Batch` / `D9-Operator`：当前 AS 的优势在 repo-owned batch / runner 与明确 operator contract，测试 helper 不应再靠吞消息维持偶然通过"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` L183-L220 的 `WaitForMessageType()` 在 `Envelope->MessageType != ExpectedType` 时只记录最后一个类型并继续轮询，未做任何 pending 保留；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` L55-L74 与 L96-L115 在 handshake 中直接 `ReceiveEnvelope()` 后只保留 `DebugServerVersion` / `BreakFilters`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` L60-L80 与 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` L58-L79 也沿用同一类手写过滤循环，说明“启动阶段只看目标消息”的模式仍是仓内主线。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerMessageCollector.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerMessageCollector.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerProtocolSequenceTests.cpp`（新建）
- [ ] **P2.20** 📦 Git 提交：`[DebuggerTest] Refactor: make debugger message collection lossless and sequence-aware`

- [ ] **P2.20-T** 单元测试：为 lossless collector 与 database/handshake sequence 建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerMessageCollectorTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerProtocolSequenceTests.cpp`（新建）
  - 测试场景：1. synthetic mixed-envelope 样本里，等待 `DebugServerVersion` 时收到的非目标 envelope 必须保留在 pending queue，后续 collector 能按原顺序继续取出，不能被静默丢弃；2. `Smoke.Handshake` 必须显式断言 `StartDebugging` 后首个相关响应是 `DebugServerVersion`，`RequestBreakFilters` 后首个相关响应是 `BreakFilters`，且阶段结束后无意外消息残留；3. `RequestDebugDatabase` 必须完整观测到 `DebugDatabaseSettings`、至少一条 `DebugDatabase`、`DebugDatabaseFinished`、`AssetDatabaseInit`、`AssetDatabaseFinished`，并校验顺序不倒退、不漏段。
  - 测试命名：`Angelscript.TestModule.Debugger.Protocol.PendingQueuePreservesUnexpectedMessages`、`Angelscript.TestModule.Debugger.Protocol.HandshakeRejectsUnexpectedStartupMessages`、`Angelscript.TestModule.Debugger.Database.RequestDebugDatabaseSequence`
  - 隔离方式：synthetic collector 测试直接喂内存 envelope 队列；真实序列测试继续走 `FAngelscriptDebuggerTestSession`，但 message ordering 统一由 collector helper 管理
- [ ] **P2.20-T** 📦 Git 提交：`[DebuggerTest] Test: verify lossless debugger message collection`

- [ ] **P2.21** 深化 `P2.9` 的 debugger exact-outcome：给 stepping 回归补 `top-frame guard + source ownership + initial breakpoint` 合同
  - 当前 `StepIn` / `StepOver` / `StepOut` 仍停留在“callstack 存在就直接看 `Frames[0].LineNumber`”的 lucky path。三个用例都在 `Callstack.IsSet()` 之后直接索引 `Frames[0]`；`StepOut` 甚至完全不校验首个 stop 是否真的停在 caller 的 `StepCallLine` 断点，只检查第 2、3 个 stop 看起来像 `StepIn -> StepOut`。这意味着一旦 debug server 返回空 frame、错误 source 文件、或首个 stop 来自残留断点，当前测试可能直接数组越界，或者继续把错误 source map 当成正确结果。
  - 本项不重复 `P2.9` 已规划的 `Reason` 序列和返回值合同，而是补齐它缺的“frame 是否存在、source 是否属于当前 fixture、StepOut 是否从正确起点开始”三层 guardrail。优先做法是在 `Shared` 层抽出 `AssertStopHasTopFrame(...)`、`AssertStopMatchesFixtureLine(...)`、`AssertStopSourceBelongsToFixture(...)` 一类 helper，再把 `Stepping` 三条用例与已有 `Breakpoint.HitLine` 的更强断言风格对齐，避免继续在每条 case 里手写不完整的 `Callstack.IsSet()` + `Frames[0]` 模式。
  - 这项深化只收口当前 stepping fixture 的基础断言，不重复 `Plan_ASDebuggerUnitTest.md` 里更宽的 callstack / variables 新 suite。目标是让现有三条已存在的 high-value regression 先变成“显式失败且定位清楚”的合同，而不是继续因为测试代码本身越界或只看行号而掩盖 source mapping 退化。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-11`：3 个步进用例只检查 `Callstack.IsSet()` 就直接索引 `Frames[0]`，协议退化时会崩成数组越界"
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` — "`Issue-22`：`StepOut` 从未校验首个 stop 是否真的停在 caller 的断点行；`Issue-28`：3 个步进用例只校验行号和栈深，不校验 `Source`"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：repo-owned debugger runner 的价值在于把用户可见的 source/line contract 稳定锁死，而不是只数 stop 数和行号"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` L437-L449、L532-L550、L634-L651 都在 `Callstack.IsSet()` 后直接读取 `Frames[0]`，且只比较 `LineNumber` / `Frames.Num()`；同文件 L629-L651 的 `StepOut` 只检查第 2、3 个 stop，完全没有校验 `Stops[0]` 的 `Reason`、`Source` 或 `LineNumber`；相比之下，`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` L423-L430 已经在同一套 fixture 下显式断言 `Frames.Num() > 0` 与 `Source.EndsWith(Fixture.Filename)`，说明更强的 top-frame/source 合同在现有 debugger 测试中已经有先例但尚未推广到 stepping。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerAssertionHelpers.h`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerAssertionHelpers.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`
- [ ] **P2.21** 📦 Git 提交：`[DebuggerTest] Refactor: harden stepping callstack and source assertions`

- [ ] **P2.21-T** 单元测试：为 stepping 的 top-frame/source guardrail 建立回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerAssertionHelperTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`
  - 测试场景：1. helper 在 `Callstack.IsSet()` 但 `Frames.Num() == 0` 的 synthetic stop 上必须返回显式失败诊断，而不是让调用方继续索引 `Frames[0]`；2. `StepIn` / `StepOver` / `StepOut` 都必须验证顶帧 `Source.EndsWith(Fixture.Filename)`，避免“行号对但文件错”的 source mapping 假绿；3. `StepOut` 额外断言首个 stop 的 `Reason == "breakpoint"`、`LineNumber == Fixture.GetLine(TEXT("StepCallLine"))`、且 caller 顶帧存在，证明整个 `StepIn -> StepOut` 序列从正确断点起步。
  - 测试命名：`Angelscript.TestModule.Debugger.Assertions.RejectsEmptyTopFrame`、`Angelscript.TestModule.Debugger.Stepping.StepIn.VerifiesTopFrameSource`、`Angelscript.TestModule.Debugger.Stepping.StepOut.ValidatesInitialBreakpointStop`
  - 隔离方式：synthetic helper 测试直接构造 stop/callstack 数据；真实 stepping 回归继续走 `FAngelscriptDebuggerTestSession + FAngelscriptDebuggerScriptFixture`
- [ ] **P2.21-T** 📦 Git 提交：`[DebuggerTest] Test: verify stepping top-frame and source contracts`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P2.20` debugger lossless collector | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerMessageCollectorTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerProtocolSequenceTests.cpp` | pending queue 保留非目标消息、handshake 独占消息序列、`RequestDebugDatabase` 完整顺序 | P1 |
| `P2.21` stepping top-frame/source guardrail | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerAssertionHelperTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` | empty frame 显式失败、`Source.EndsWith(Fixture.Filename)`、`StepOut` 首个 stop baseline | P1 |

### 本轮追加风险

1. **lossless pending queue 一旦落地，会把过去被 `WaitForMessageType()` 和手写 handshake 循环吞掉的启动污染直接暴露出来**：短期内 smoke / breakpoint / stepping 可能先因为“多收到一条消息”变红，但这属于把真实协议噪音前置成可诊断信号，而不是新增不稳定性。
2. **给 stepping 回归补 `Frames.Num() > 0` 与 `Source` 合同后，现有 debug server 若偶发返回空 frame 或 stale source path，会更早在测试层显性失败**：这是期望中的 fail-fast 行为，能把“测试代码越界”改成“协议回归有明确错误面”，但会让当前部分模糊绿灯先转成真实红灯。

---

## 深化 (2026-04-09 07:48:33)

- 已复查 `Documents/Plans/Plan_ScriptExamplesExpansion.md` 与 `Documents/Plans/Plan_TestCoverageExpansion.md`：前者承接示例资产与公开入口，后者承接更宽功能面覆盖；以下只补 `AngelscriptTest` 内部的 example helper / file-discovery contract，不重复资产落盘或大范围功能扩张。

### Phase 2 追加：把公开示例与脚本发现从 compile-smoke 收口成真实 regression contract

- [ ] **P2.22** 把 `ScriptExample` helper 拆成显式 compile-smoke 与语义验证双车道，停止让 `WidgetUMG` 这类 runtime-sensitive 示例继续借 `RunScriptExampleCompileTest()` 假装有行为覆盖
  - 当前 `Examples` 测试的公共 helper 只有一条 compile-only 路：它总是 `AcquireCleanSharedCloneEngine()` + `CompileAnnotatedModuleFromMemory()`，完成后直接返回 `bCompiled`。这对纯语法示例足够，但对 `WidgetUMG` 这类脚本里同时声明 `UPROPERTY(BindWidget)` 与 `WidgetBlueprint::CreateWidget(...)` 的示例，会把 runtime 行为、world-context 和 metadata contract 全部留空。
  - 本项只收口测试基础设施语义，不重复 `Plan_ScriptExamplesExpansion.md` 的资产落盘主线：1. 把现有 helper 命名改成显式 `CompileSmoke` 语义，避免 compile-only case 再被误读成行为覆盖；2. 为 runtime-sensitive 示例补专用 harness 或直接迁到 dedicated `Bindings/` / `Compiler/` 用例，分别守 `CreateWidget` 运行时 contract 与 `BindWidget` metadata round-trip；3. `Examples/` 目录保留“示例可编译”职责，但不再承担函数库/metadata 的唯一守门。
  - 这样处理后，`ScriptExamples` 仍保留 D9 强调的“公开示例挂自动化”的优势，但示例 smoke 与真正的 API 语义会分层：示例测试负责 public example carrier，`Bindings/Compiler` 负责 runtime/metadata regression。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — "`Issue-6` / `NewTest-6`：`WidgetUMG` 目前只做 compile smoke，`CreateWidget` 的运行时 contract 与 null-path 完全未验证"
    - [C] `Documents/AutoPlans/TestCoverage/Preprocessor_TestGaps.md` — "`NewTest-54`：`BindWidget` 当前没有 metadata/flag round-trip 自动化，示例脚本并不能替代编译后属性断言"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-09`：`AngelscriptTest` 同时承载 regression / example / learning / validation；可推断 example carrier 必须明确区分 compile smoke 与真正的行为回归"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9`：当前仓库把 public/staged example 直接挂到 automation gate，这条优势只有在示例 helper 不再夸大覆盖范围时才成立"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp` L16-L17、L35-L39、L56-L59 的 `RunScriptExampleCompileTest()` 只创建 shared clone、编译虚拟文件并返回 `bCompiled`；`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleWidgetUmgTest.cpp` L22、L47、L59 的示例脚本同时含 `UPROPERTY(BindWidget)` 和 `WidgetBlueprint::CreateWidget(...)`，但测试入口仍只是 `RunScriptExampleCompileTest(...)`；`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExamplePropertySpecifiersTest.cpp` L95 也沿用同一 compile-only helper，说明当前 Example carrier 仍缺“compile smoke 与语义回归”分层。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.h`、`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleWidgetUmgTest.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWidgetFunctionLibraryTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineWidgetMetadataTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupportTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P2.22** 📦 Git 提交：`[TestInfrastructure] Refactor: split script example compile smoke from widget runtime contracts`

- [ ] **P2.22-T** 单元测试：为 `ScriptExamples` helper 的 smoke/semantic 分层补齐自测与 `WidgetUMG` 真实 contract
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupportTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWidgetFunctionLibraryTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineWidgetMetadataTests.cpp`（新建）
  - 测试场景：1. compile-smoke helper 只断言示例可编译与 cleanup 成功，不再暗示 runtime 执行；2. `WidgetBlueprint::CreateWidget` 在最小 world + `APlayerController` 下返回非空 widget、类匹配且 `GetOwningPlayer()` 正确；3. `WidgetClass == null` 时返回可诊断失败，不静默成功；4. `BindWidget` round-trip 后生成属性必须保留 `BindWidget` metadata、`CPF_BlueprintVisible` 与 `CPF_BlueprintReadOnly`，且不得回落成可编辑属性。
  - 测试命名：`Angelscript.TestModule.ScriptExamples.Support.CompileSmokeDoesNotPretendRuntimeCoverage`、`Angelscript.TestModule.FunctionLibraries.Widget.CreateWidgetRuntimeContract`、`Angelscript.TestModule.FunctionLibraries.Widget.CreateWidgetRejectsNullClass`、`Angelscript.TestModule.Compiler.WidgetMetadata.BindWidgetPropertyFlagsRoundTrip`
  - 隔离方式：compile-smoke 自测使用 shared clone + checked cleanup；runtime 与 metadata 场景使用 `FAngelscriptTestFixture` 或等价 `Engine + WorldContext` 夹具，避免继续依赖 Example compile helper 的 ambient 假设
- [ ] **P2.22-T** 📦 Git 提交：`[TestInfrastructure] Test: verify script example widget runtime and metadata contracts`

- [ ] **P2.23** 为 `FileSystem` discovery/skip-rule 夹具补齐 `editor-scripts enabled` 保留分支，停止让 `Examples/Dev/Editor` 目录只在“被过滤掉”这半边有回归
  - 当前 `FileSystem` 夹具对同一类目录只守住了 half-contract：`Discovery` 测试在 `bUseEditorScripts = true` 时只验证普通脚本枚举，`SkipRules` 测试虽然写入了 `Examples/Dev/Editor` 三类文件，却只在 `bUseEditorScripts = false` 分支断言它们被过滤。结果是“这些目录在 editor-scripts enabled 时必须被保留”这一半完全没有 regression fence。
  - 本项不重复 `Plan_ScriptExamplesExpansion.md` 的示例资产主线，只补现有 file-backed helper 的 gate 语义：把 discovery/skip-rule 共享根目录与 relative-path 收集逻辑抽成专用 helper，再明确拆成 `disabled -> skip` 与 `enabled -> keep` 两条对称用例。这样将来 `Script/Examples` / `Dev` / `Editor` 目录真正落资产后，不会因为 skip-rule 回归而被悄悄踢出测试视野。
  - 这条 guardrail 也与 D9 的“公开示例必须挂自动化”相一致：如果 `FindAllScriptFilenames()` 在 editor-scripts 打开时仍把 `Examples` 类目录过滤掉，公开示例虽然存在，却不会真正进入 regression gate。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — "`NewTest-26`：`FindAllScriptFilenames()` 缺少 `editor-scripts enabled` 下保留 `Examples/Dev/Editor` 的直接回归"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-09`：`Examples` 与 regression 共享同一测试模块；可推断脚本发现分支若不对称守卫，example carrier 会变成名义上挂自动化、实际上可被 discovery 漂移静默踢出"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9`：当前仓库强调 public example/staged example 必须进入 regression gate；skip-rule 的 keep-branch 若无测试，这条 operator contract 就不完整"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` L254-L263 的 `Discovery` 仅在 `bUseEditorScripts = true` 时验证 `RootScript`/`Game/*` 三条普通路径；同文件 L281-L292 写入了 `Examples/ExampleOnly.as`、`Dev/DevOnly.as`、`Editor/EditorOnly.as`，但 L298-L309 的 `SkipRules` 只在 `bUseEditorScripts = false` 下断言最终只剩 `Gameplay/Main.as`，完全没有对应的 keep-branch 断言。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemDiscoveryTests.cpp`（新建，可选，用于拆出 discovery/skip-rule 夹具）、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P2.23** 📦 Git 提交：`[TestInfrastructure] Refactor: make file discovery skip-rule coverage symmetric`

- [ ] **P2.23-T** 单元测试：为 `FindAllScriptFilenames()` 建立 `disabled-skip` / `enabled-keep` 成对回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemDiscoveryTests.cpp`（新建，或在现有 `AngelscriptFileSystemTests.cpp` 拆分后承接）
  - 测试场景：1. `bUseEditorScripts = false` 时仍只保留 `Gameplay/Main.as`；2. `bUseEditorScripts = true` 时必须同时枚举 `Gameplay/Main.as`、`Examples/ExampleOnly.as`、`Dev/DevOnly.as`、`Editor/EditorOnly.as`；3. 两条用例都要在退出时恢复 `AllRootPaths` 并清空临时目录，避免文件系统夹具把旧 root 泄漏给后续测试。
  - 测试命名：`Angelscript.TestModule.FileSystem.SkipRules.EditorScriptsDisabledSkipsExamplesDevAndEditor`、`Angelscript.TestModule.FileSystem.SkipRules.EditorScriptsEnabledKeepsExamplesDevAndEditor`
  - 隔离方式：`ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `TGuardValue<bool>` 守护 `bUseEditorScripts` + `ON_SCOPE_EXIT` 恢复 `AllRootPaths` 与清理临时根目录
- [ ] **P2.23-T** 📦 Git 提交：`[TestInfrastructure] Test: verify file discovery skip-rule keep and skip branches`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P2.22` script example smoke/semantic split | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupportTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWidgetFunctionLibraryTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineWidgetMetadataTests.cpp` | compile-smoke 语义显式化、`CreateWidget` 正/负路径、`BindWidget` metadata round-trip | P1 |
| `P2.23` file discovery keep/skip symmetry | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemDiscoveryTests.cpp` | `editor-scripts disabled` skip branch、`editor-scripts enabled` keep branch、root/临时目录恢复 | P1 |

### 本轮追加风险

1. **把 `WidgetUMG` 从 compile-only 误绿收口成 runtime + metadata 双 contract 后，短期内可能先暴露真实 `WidgetBlueprint::CreateWidget` 或 `BindWidget` 绑定问题**：这不是新增不稳定性，而是把过去被示例 compile smoke 吞掉的语义回归前置成可诊断失败。
2. **给 `FindAllScriptFilenames()` 补齐 keep-branch 后，`Examples/Dev/Editor` 目录一旦被 skip-rule 误删会立即从“静默漏测”变成显式红灯**：这会让公开示例与 editor-only 脚本目录真正受 regression gate 保护，但也意味着后续目录规则改动需要同步更新测试矩阵，不能再靠偶然发现。

---

## 深化 (2026-04-09 07:57:08)

- 已复查 `Documents/Plans/Plan_TestCoverageExpansion.md`：它继续承接更宽的 bind / function-library 覆盖扩张；以下只补 `AngelscriptTest` 里仍会误导覆盖口径的现有 carrier，不重复更大的功能矩阵。

### Phase 2 追加：把 Bindings 目录里“借名覆盖”的 smoke carrier 收口成 truthful test surface

- [ ] **P2.24** 深化 `P2.16`：把 `MathExtendedCompat` 从“大而全 smoke 名字”收口成 utility/random smoke，并补 dedicated deterministic math carrier
  - 当前 `MathExtendedCompat` 的名字和目录位置都在暗示“Math function library 已有行为回归”，但真实脚本只覆盖随机/utility 片段，而且仍通过随机非零断言与单个 `Result == 1` 汇总结果过关。本项要尽量保持现有 automation 入口兼容，但把真实覆盖面说清楚：保留旧用例只负责 random/utility smoke，同时新增 `AngelscriptMathFunctionLibraryTests.cpp` 承接 deterministic math contract。
  - 实施上优先拆成两个新 carrier：1. shortest-path / transform rotation / `TInterpTo` / `MoveTowards`；2. `SinCos` / `Modf` / `LineBoxIntersection`。旧 `FAngelscriptMathExtendedBindingsTest` 只保留 `RandHelper`、`VRand` / `VRandCone` 等随机/通用 helper，并把断言改成稳定不变量而不是“非零”。若不改 automation id，则至少在文件注释与测试指南里明确它不代表整份 `AngelscriptMathLibrary`。
  - 这项深化不重复 `Plan_TestCoverageExpansion.md` 对整个 function library 面的系统扩张，只处理“现有 carrier 名称与真实守护范围不一致”这条测试基础设施问题，避免后续 coverage 继续叠在误导性 smoke file 上。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/FunctionLibraries_TestGaps.md` — "`Issue-14`：`MathExtendedCompat` 借名覆盖 `AngelscriptMathLibrary`；`Issue-16`：随机 helper 依赖非零断言会引入噪声；`NewTest-23` / `NewTest-27`：shortest-path / transform / `SinCos` / `Modf` / `LineBoxIntersection` 当前都缺 dedicated carrier"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-25`：测试职责已经很多，但仍被压在单一 `AngelscriptTest` owner 下；单个 carrier 更需要保持单职责，否则目录级分层会继续失真"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：repo-owned runner 的价值在于 operator 能从测试 surface 读出真实保护面，而不是被宽名字 smoke case 误导"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` L12-L14 仍把用例命名为 `Angelscript.TestModule.Bindings.MathExtendedCompat`；L33-L154 的脚本体只覆盖 `RandHelper`、`IsPowerOfTwo`、`VRand`、`VRandCone`、`ClampAngle`、`CubicInterp`、`LinePlaneIntersection` 等另一批 helper，没有任何 `Wrap` / `SinCos` / `Modf` / shortest-path / `TransformRotation` 入口；L46-L96 继续把 `VRand`、`VRandCone`、`RandomRotator(false)` 写成“非零即通过”的随机断言；L154 仍只用 `Result == 1` 汇总整条语义。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md`
- [ ] **P2.24** 📦 Git 提交：`[TestInfrastructure] Refactor: split MathExtendedCompat smoke from deterministic math carriers`

- [ ] **P2.24-T** 单元测试：为 math carrier truthfulness 补 deterministic contract，并收紧旧 smoke 的随机不变量
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`（新建）、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp`
  - 测试场景：1. `LerpShortestPath` / `RInterpShortestPathTo` / `RInterpConstantShortestPathTo` 在 `170 -> -170` 上必须走短弧而不是长弧；2. `TransformRotation` / `InverseTransformRotation` 必须 round-trip；3. `TInterpTo` 零速度直接命中 target，`MoveTowards` 同时覆盖小步长与夹到目标；4. `SinCos` / `Modf` float/double 版本要精确对齐 native reference，且 out-ref 语义正确；5. `LineBoxIntersection` 覆盖 hit / miss / 起点在盒内三条路径；6. 旧 `MathExtendedCompat` 只保留 `VRand` 单位长度、`VRandCone` 夹角、`RandomRotator(false)` 的 `Roll == 0` 与合法范围等稳定不变量，不再把“接近零”当失败。
  - 测试命名：`Angelscript.TestModule.FunctionLibraries.MathShortestPathAndTransformSemantics`、`Angelscript.TestModule.FunctionLibraries.MathTrigDecomposeAndBoxIntersection`、`Angelscript.TestModule.Bindings.MathExtendedCompat.RandomHelpersUseStableInvariants`
  - 隔离方式：`ASTEST_CREATE_ENGINE_SHARE_CLEAN` + native reference 对照；旧 smoke 留在 shared-clean 路径，新 deterministic carrier 用独立模块名避免继续复用 `ASMathExtendedCompat`
- [ ] **P2.24-T** 📦 Git 提交：`[TestInfrastructure] Test: verify deterministic math carriers and stable random smoke`

- [ ] **P2.25** 深化 `P2.16`：把 `GlobalVariableCompat` 从单脚本混合 smoke 拆成 core-default globals 与 commandlet globals 两条 carrier
  - 当前 `GlobalVariableCompat` 用一个 `Entry()` 同时覆盖 `CollisionProfile`、`FComponentQueryParams`、`FGameplayTag::EmptyTag`、`FGameplayTagContainer::EmptyContainer`、`FGameplayTagQuery::EmptyQuery`，最终仍只看 `Result == 1`。这既让失败定位退化成错误码 scavenger hunt，也让 `Bind_CoreGlobals.cpp` 里更敏感的 commandlet globals 完全处于无保护状态。
  - 本项要把“全局常量默认值”和“当前进程 commandlet 状态”拆成两条显式 contract：保留 `GlobalVariableCompat` 只验证 deterministic default globals，并将每个 global 单独映射为可诊断断言；同时新增 `GlobalCommandletGlobalsCompat`，在 C++ 侧读取 `IsRunningCommandlet()`、`IsRunningCookCommandlet()`、`IsRunningDLCCookCommandlet()` 与 `GetRunningCommandletClass()` 的原生值后注入脚本，固定 script globals 必须与当前进程一致。
  - 这里不重复 `Plan_TestCoverageExpansion.md` 的 bind surface 扩张主线，也不把整个 `Bind_CoreGlobals.cpp` 一次性铺满；先把现有最具误导性的单文件 smoke 变成可解释的 two-lane carrier，后续更宽的 global API 才有清晰落点。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/BindSystem_TestGaps.md` — "`Issue-8`：`GlobalVariableCompat` 用单个混合 smoke 代替真正的 global coverage；`NewTest-1`：`Bind_CoreGlobals.cpp` 的 4 个 commandlet globals 仍没有任何测试"
    - [D] `Documents/AutoPlans/ArchitectureReview/ModuleStructure_ArchReview.md` — "`Arch-MS-25`：测试层级仍大量依赖目录/命名约定；像 `GlobalBindings` 这种宽名字 case 若不拆 carrier，会继续让分层口径失真"
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — "`D9-Operator`：repo-owned runner 的 operator surface 应能直观看出正在守什么 contract；用一个 `Result == 1` smoke 代表整组 globals 会削弱这一点"
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGlobalBindingsTests.cpp` L10-L13 仍只有 `Angelscript.TestModule.Bindings.GlobalVariableCompat` 一条 automation；L25-L42 的单个 `Entry()` 仍把 `CollisionProfile::BlockAllDynamic`、`DefaultComponentQueryParams`、`FGameplayTag::EmptyTag`、`EmptyContainer`、`EmptyQuery` 混在一起验证；整文件没有任何 `IsRunningCommandlet` / `IsRunningCookCommandlet` / `IsRunningDLCCookCommandlet` / `GetRunningCommandletClass` 命中；L62 仍只以 `Result == 1` 作为总断言。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGlobalBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCommandletGlobalBindingsTests.cpp`（新建，可选；若不新建则在原文件拆分为两组明确 case）、`Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`
- [ ] **P2.25** 📦 Git 提交：`[TestInfrastructure] Refactor: split core globals and commandlet globals carriers`

- [ ] **P2.25-T** 单元测试：为 default globals 与 commandlet globals 建立双车道回归
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGlobalBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCommandletGlobalBindingsTests.cpp`（新建，可选）
  - 测试场景：1. `CollisionProfile::BlockAllDynamic`、`DefaultComponentQueryParams`、`EmptyTag`、`EmptyContainer`、`EmptyQuery` 分别有独立断言，任何一项回归都能直接定位；2. 脚本侧 `IsRunningCommandlet()` / `IsRunningCookCommandlet()` / `IsRunningDLCCookCommandlet()` 必须精确等于当前进程原生值；3. `GetRunningCommandletClass()` 在有运行中 commandlet 时返回同一 `UClass*` / 名称，在无 commandlet 时显式返回 `null`；4. commandlet globals 用例不得依赖硬编码环境假设，而是由 C++ 基线驱动脚本期望值。
  - 测试命名：`Angelscript.TestModule.Bindings.GlobalConstantsCompat`、`Angelscript.TestModule.Bindings.GlobalCommandletGlobalsCompat`
  - 隔离方式：`ASTEST_CREATE_ENGINE_SHARE_CLEAN` + C++ 侧缓存当前进程 global state；default globals 与 commandlet globals 使用不同模块名，避免继续塞进同一个 `Entry()` 返回码脚本
- [ ] **P2.25-T** 📦 Git 提交：`[TestInfrastructure] Test: verify core globals and commandlet globals separately`

### 本轮追加测试矩阵

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P2.24` truthful math carriers | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathFunctionLibraryTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` | shortest-path / transform round-trip、`SinCos` / `Modf` / `LineBoxIntersection` 精确对照、旧 random smoke 改为稳定不变量 | P1 |
| `P2.25` global carrier split | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGlobalBindingsTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCommandletGlobalBindingsTests.cpp` | deterministic default globals、process commandlet globals 与 native 基线精确对齐 | P1 |

### 本轮追加风险

1. **把 `MathExtendedCompat` 从宽名字 smoke 收口成 deterministic carriers 后，短期统计看起来会像“新增了 math 测试、缩小了旧 compat 测试覆盖”**：这是把原本借名覆盖的口径纠正为真实守护面，不是覆盖回退。
2. **新增 `GlobalCommandletGlobalsCompat` 会把当前进程是否处于 commandlet、cook、DLC cook 等状态显式暴露到回归中**：如果某些执行入口对这些全局量的期望值并不稳定，测试会先把环境假设打成红灯；这正是需要尽早固定的 operator contract，而不是应继续隐藏的偶然性。
