# TestInfrastructure 发现与方案

---

## 发现与方案 (2026-04-08 12:25)

### Issue-1：Full-destroy 核心用例会丢弃进入测试前的引擎上下文栈

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 行号 | 19-24, 158-170, 194-207, 266-279 |
| 问题 | `FCoreTestContextStackGuard` 在构造时通过 `FAngelscriptEngineContextStack::SnapshotAndClear()` 清空进入测试前的整条 context stack，但 3 个 full-destroy 用例在销毁 shared/global engine 后都会调用 `ContextGuard.DiscardSavedStack()`，导致析构阶段不再恢复原始栈；同一段 setup/teardown 还会无条件调用 `DestroyGlobalEngine()`。这使 `Engine.LastFullDestroyClearsTypeState`、`Engine.FullDestroyAllowsCleanRecreate`、`Engine.FullDestroyAllowsAnnotatedRecreate` 不只是隔离自己，而是永久抹掉进入测试前的 ambient engine 上下文。 |
| 根因 | 当前实现把“为了独占 full-engine epoch 暂时清空上下文”与“销毁旧测试引擎”混在同一个局部模式里，但没有区分 outer scope 是否属于当前测试，也没有提供可恢复的 owner-aware fixture。 |
| 影响 | 后续测试如果依赖进入该用例前已存在的 `CurrentEngine` / ambient world context，会在执行顺序变化时出现上下文错乱、空 engine、错误 engine 复用等不稳定结果；同时这 3 个测试的 cleanup 逻辑只能通过复制粘贴维持一致，后续很容易继续漂移。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 full-engine 独占期的上下文隔离提炼成可恢复、可判定所有权的 fixture，禁止测试体内直接 `DiscardSavedStack()`。 |
| 具体步骤 | 1. 在 `Shared` 层新增专用 fixture（例如 `FFullEngineEpochIsolationFixture`），统一保存 `SnapshotAndClear()` 的结果、进入测试前的 shared/global engine 指针与是否由当前测试创建。 2. 让 fixture 只销毁 test-owned shared/global engine；如果进入测试前存在 outer scoped engine，则在析构时恢复原始 stack，而不是 `Reset()` 掉快照。 3. 将 `FAngelscriptFullDestroyClearsTypeStateTest`、`FAngelscriptFullDestroyAllowsCleanRecreateTest`、`FAngelscriptFullDestroyAllowsAnnotatedRecreateTest` 的重复 setup/teardown 改为调用 fixture，删除文件内的 `FCoreTestContextStackGuard` 和 3 份重复的 `DestroySharedTestEngine()` / `DestroyGlobalEngine()` 模板代码。 4. 在 helper 自测中补一条回归：先建立 outer `FAngelscriptEngineScope`，再执行 full-destroy fixture，断言退出后 `TryGetCurrentEngine()` 恢复为 outer engine。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 fixture 错误地恢复了已经被当前测试销毁的 engine 指针，会把 stale context 再次压回栈中；需要通过 owner 标记或实例地址对比避免恢复已销毁对象。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 helper 回归，验证 outer current engine 在 full-destroy 流程后被恢复。 2. 单独运行 `Angelscript.TestModule.Engine.LastFullDestroyClearsTypeState`、`Angelscript.TestModule.Engine.FullDestroyAllowsCleanRecreate`、`Angelscript.TestModule.Engine.FullDestroyAllowsAnnotatedRecreate`。 3. 将上述 3 个用例与一个依赖 ambient current engine 的 helper 测试串行运行，确认顺序互换后结果一致。 |

### Issue-2：`ResetSharedCloneEngine()` 未回收 detached `UASFunction`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 行号 | 207-266, 313-374; 1062-1065; 405-426 |
| 问题 | `ResetSharedCloneEngine()` 只遍历 detached `UASClass` 并移除 root / `RF_Standalone`，没有对 detached `UASFunction` 做任何回收；但同文件的 `LogSharedEngineDebugState()` 已经显式统计 `DetachedASFunctions`。运行时在 `DiscardModule()` 时会把 `UASFunction::ScriptFunction` 和 `ValidateFunction` 置空，意味着 reset 后完全可能留下已经脱离脚本实现的 `UASFunction` 壳对象，而 `Bind_UObject.cpp` 仍为这些对象暴露 `GetSourceFilePath()` / `GetSourceLineNumber()` / `GetScriptFunctionDeclaration()` 访问路径。 |
| 根因 | shared reset 的实现只覆盖了“生成类残留”这一个历史问题，没有把 generated function 残留纳入同一 cleanup contract；debug 统计与真正 cleanup 逻辑已经出现分叉。 |
| 影响 | shared-engine 在 `AcquireCleanSharedCloneEngine()` / `DestroySharedTestEngine()` 后仍可能保留失效的 `UASFunction` UObject，导致后续测试通过反射或名称查找拿到 stale function shell，出现脚本声明信息错误、函数覆盖判断失真、测试顺序相关的反射污染。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 扩展 shared reset，使 detached `UASFunction` 与 detached `UASClass` 使用同一级别的清理契约，并补齐针对函数残留的回归测试。 |
| 具体步骤 | 1. 在 `ResetSharedCloneEngine()` 中新增 `TObjectIterator<UASFunction>` 清理段：对 `ScriptFunction == nullptr` 的对象清空 `ValidateFunction`，移除 root，清除 `RF_Standalone`，必要时补 `MarkAsGarbage()` 前的保护检查，再统一 `CollectGarbage(RF_NoFlags, true)`。 2. 保留并复用 `LogSharedEngineDebugState()` 的 detached-function 统计，在 reset 前后分别记录 `DetachedASFunctions`，将其变成可验证的 cleanup 结果，而不是仅用于日志观测。 3. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 新增 `ResetSharedEngineReleasesGeneratedFunctions` 回归：编译带 `UFUNCTION` 的 annotated 模块，确认 reset 前能找到 `UASFunction`，reset 后相同名字的 `UASFunction` 数量归零，且 `FindGeneratedFunction()` 不再返回旧函数。 4. 把现有 `ResetSharedEngineReleasesGeneratedComponentClasses` 保留为类级回归，与新的函数级回归一起覆盖 reset contract。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | M |
| 风险 | `UASFunction` 可能被其它生成对象短暂引用；如果在引用仍有效时过早清理，可能引入悬空 `ValidateFunction` 或反射崩溃，需要以 `ScriptFunction == nullptr` 且所属模块已 discard 为准做保守清理。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行新增的 `Shared.EngineHelper.ResetSharedEngineReleasesGeneratedFunctions`。 2. 回归现有 `Shared.EngineHelper.ResetSharedEngineReleasesGeneratedComponentClasses` 与 `Shared.EngineHelper.GeneratedSymbolLookup`。 3. 串行运行一条生成 `UFUNCTION` 的绑定测试和一条使用 clean shared engine 的反射测试，确认顺序互换后结果一致。 |

### Issue-3：函数声明解析逻辑在两个 helper 中重复实现且诊断不一致

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | 230-271, 391-395; 596-652 |
| 问题 | `AngelscriptTestEngineHelper.cpp` 中的 `FindFunctionByDecl()` 与 `AngelscriptTestUtilities.h` 中的 `GetFunctionByDecl()` 各自复制了一套“先按 declaration 查找，再回退到函数名，再遍历模块函数列表”的解析逻辑。两者当前已经出现诊断分叉：`GetFunctionByDecl()` 会把 `available functions` 写入 `AddError()`，而 `FindFunctionByDecl()` 失败后只让 `ExecuteIntFunction()` 输出一条简短 warning。 |
| 根因 | helper 体系同时服务“automation test 断言路径”和“通用执行 helper 路径”，但没有抽象出共享的 declaration resolver，导致相同语义被在 header / cpp 两处各自实现。 |
| 影响 | 后续一旦要支持更复杂的 declaration 形式、改进 fallback 规则或增强错误信息，必须同步修改两处代码，否则同一个脚本模块会在不同 helper 路径下表现不一致；当前已经让失败诊断质量依赖调用入口，增加定位 flaky 编译/执行失败的成本。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 提取单一的 declaration resolver，并把“查找结果”和“诊断信息”分离，供 test/assert helper 与 runtime helper 共同复用。 |
| 具体步骤 | 1. 在 `Shared` 层新增统一解析函数（例如 `ResolveFunctionByDecl(asIScriptModule&, const FString&, FAngelscriptFunctionLookupDiagnostics*)`），集中处理 declaration 直查、函数名回退和模块函数枚举。 2. 让 `GetFunctionByDecl()` 只负责把统一 resolver 的诊断转成 `FAutomationTestBase::AddError()`；让 `ExecuteIntFunction()` 在 resolver 失败时复用同一份 `available declarations` 信息写日志。 3. 删除 `AngelscriptTestEngineHelper.cpp` 私有 `FindFunctionByDecl()` 的重复实现，避免 header/cpp 两份解析规则继续漂移。 4. 在 helper 自测中新增 declaration lookup 回归，覆盖“精确声明命中”“按函数名回退命中”“查找失败时返回 available declarations”三条路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果抽取时改变了当前 fallback 顺序，可能影响依赖“函数名模糊匹配”的老测试；需要通过回归锁定现有行为，再在单一实现上演进。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 resolver 单测，验证 3 条查找路径。 2. 回归 `Shared.EngineHelper.ExecuteIntFunction` 与任何依赖 `BuildModule()+GetFunctionByDecl()` 的基础执行测试。 3. 人工检查失败日志，确认 `ExecuteIntFunction` 在查找失败时也能输出候选 declaration 列表。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-1 | Defect | 先修复，先把 full-destroy 用例改成 owner-aware fixture，避免继续破坏 outer context |
| P1 | Issue-2 | Defect | 紧随其后，补齐 shared reset 的 `UASFunction` 清理与回归，切断函数级状态泄漏 |
| P2 | Issue-3 | Refactoring | 在前两条 cleanup contract 稳定后执行，收敛 helper 解析逻辑与失败诊断 |

---

## 发现与方案 (2026-04-08 12:33)

### Issue-4：`ASTEST_BEGIN_SHARE` 族宏不会回收测试模块，导致 shared 用例在同一进程内持续累积状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 行号 | 97-122, 160-192; 535-594; 17-26, 38-47; 24-35, 47-58, 70-81, 122-134, 146-157; 18-79, 92-159, 171-219, 231-260 |
| 问题 | `ASTEST_BEGIN_SHARE` / `ASTEST_END_SHARE` 只包了一层 `FAngelscriptEngineScope`，没有像 `ASTEST_BEGIN_FULL` / `ASTEST_BEGIN_CLONE` 一样在退出时枚举并 `DiscardModule()`；而 `ASTEST_COMPILE_RUN_INT()`、`BuildModule()`、`CompileAnnotatedModuleFromMemory()` 会把具名模块长期注册到 shared engine。当前大量 shared 用例直接在这种生命周期下编译固定模块名，例如 `ASCoreCreateCompileExecute`、`ASFunctionDefaultArguments`、`CompilerDelegateEnumClassCompile`，但测试末尾没有任何 module cleanup。 |
| 根因 | 宏体系把“共享 engine 复用”与“共享模块状态”绑定到一起，默认路径没有提供按测试回收模块的能力，导致业务测试只能依赖唯一模块名和进程重启来规避冲突。 |
| 影响 | 同一批测试在一次进程内重复执行时，第二次开始不再是“首次编译”，而是对旧模块做 reload / 覆盖；shared engine 中残留的类、枚举、delegate 和 diagnostics 也会改变后续测试的初始状态，形成顺序相关和重跑不稳定。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 shared 路径补上“按测试追踪并回收模块”的 fixture/宏，禁止继续在 `ASTEST_BEGIN_SHARE` 下裸编译具名模块。 |
| 具体步骤 | 1. 在 `Shared` 层新增 module-tracking fixture（例如 `FAngelscriptSharedModuleFixture`），内部记录本测试通过 `BuildModule()` / `CompileModuleFromMemory()` / `CompileAnnotatedModuleFromMemory()` 成功生成的模块名。 2. 为宏体系增加 cleanup 版本，例如 `ASTEST_BEGIN_SHARE_TRACKED` / `ASTEST_END_SHARE_TRACKED`，在 `END` 阶段按记录顺序统一 `Engine.DiscardModule()`，必要时再调用 `ResetSharedCloneEngine()` 处理生成 UObject 残留。 3. 先迁移当前最典型的 compile-and-execute 文件：`AngelscriptCoreExecutionTests.cpp`、`AngelscriptFunctionTests.cpp`、`AngelscriptCompilerPipelineTests.cpp`，把裸用 `ASTEST_BEGIN_SHARE` 的具名编译路径改成 tracked fixture；纯只读测试才允许保留原 `SHARE` 宏。 4. 新增回归：同一 automation test 连续两次对同一个模块名执行 compile-run，第二次仍应处于干净初始状态，并且 shared engine 退出时 `GetModuleByModuleName()` 返回空。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果一刀切地在所有 shared 用例末尾 reset engine，可能破坏少数确实依赖跨断言共享模块状态的测试；需要先把“只读共享”和“共享但自动 cleanup”两类路径拆开。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 shared-module cleanup 回归，连续两次编译同名模块并断言结果一致。 2. 回归 `Angelscript.TestModule.Angelscript.Core.*`、`Angelscript.TestModule.Angelscript.Functions.*`、`Angelscript.TestModule.Compiler.EndToEnd.*`。 3. 在 shared helper 日志中确认每个 tracked 测试退出后 `ActiveModules` 回到 0。 |

### Issue-5：`SharedClone` 辅助层实际创建 Full engine，导致引擎模式选择语义失真

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_MIGRATION_GUIDE.txt`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | 158-200, 378-433, 727-747; 42-63, 97-122; 22-29; 112-136, 206-212; 232-299, 680-730 |
| 问题 | `GetOrCreateSharedCloneEngine()` / `AcquireCleanSharedCloneEngine()` / `ETestEngineMode::SharedClone` 的命名都声明这是 shared clone 语义，但实现实际走的是 `CreateIsolatedFullEngine()`。与之相对，运行时测试明确约束：`CreateForTesting()` 在已有 source engine 时应得到 `Clone`，clone 不拥有 script engine 且不会 replay startup binds；只有 fallback/full 路径才拥有 engine 并重放 bind 初始化。现在 TestInfrastructure 的 shared 主路径把这两种模式混成了一个 API。 |
| 根因 | 早期为了保证 shared 测试始终有可用 epoch，helper 直接固化成 persistent full owner；后续又在 API、宏和 fixture 层继续沿用 `SharedClone` 命名，没有把“共享 owner”与“clone view”拆成两个明确概念。 |
| 影响 | 测试作者会在以为自己选择了 clone 的情况下，实际跑在 full-epoch 上，看到启动 bind、类型初始化、生命周期 ownership 等 full-only 行为；这会掩盖真正 clone 模式的缺陷，也让“Full vs Clone”选择失去规划价值。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 shared full owner 与 shared clone view 明确拆分，消除 `SharedClone` 这个名实不符的公共入口。 |
| 具体步骤 | 1. 先把现有实现按真实语义重命名为 `GetOrCreateSharedFullEngine()` / `AcquireCleanSharedFullEngine()` / `AcquireFreshSharedFullEngine()`，并同步更新 `ETestEngineMode`，避免继续把 full helper 暴露成 clone。 2. 在 `Shared` 层新增真正的 clone helper：持有一个内部 `SharedFullSourceEngine` 作为 source，再通过 `CreateForTesting(..., Clone)` 或 `CreateCloneFrom()` 产生 test-visible clone；该 helper 必须暴露 `GetCreationMode()==Clone`、`OwnsEngine()==false`、`GetSourceEngine()!=nullptr`。 3. 更新 `ASTEST_CREATE_ENGINE_SHARE*` 的宏选择：需要 full-epoch 的测试改成 `*_FULL_SHARED` 或直接 `ASTEST_CREATE_ENGINE_FULL()`；需要 clone 语义的测试改走新的 shared clone helper。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 增加模式回归，显式断言 shared full 与 shared clone 两条路径的 `CreationMode`、`OwnsEngine`、startup bind observation 和 `GetSourceEngine()`。 5. 补充 `TESTING_GUIDE.md` / 宏迁移文档，把“复用 shared engine”与“选择 clone/full”分开说明，避免后续继续在错误语义上扩散。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_MIGRATION_GUIDE.txt` |
| 预估工作量 | L |
| 风险 | shared clone 需要额外维护 hidden source full engine；如果 source 与 clone 的所有权/销毁顺序设计不清，会重新引入 full owner 先销毁、clone 悬挂的问题。 |
| 前置依赖 | 建议先完成 Issue-4 的 shared-module cleanup，避免在语义切换时继续叠加历史残留模块。 |
| 验证方式 | 1. 新增 helper 测试，断言 shared full 与 shared clone 的 `CreationMode`/`OwnsEngine`/`GetSourceEngine`。 2. 回归 `AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` 中与 clone/full contract 相关的用例。 3. 随机抽取 2-3 个原 `ASTEST_CREATE_ENGINE_SHARE()` 用例迁移后重跑，确认 bind 执行次数与预期模式一致。 |

### Issue-6：coverage 结果缓存不会随测试轮次或 module discard 收缩，导致报告混入历史文件

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp` |
| 行号 | 22-65, 67-83, 156-199; 20-33, 55-72; 1173-1209; 159-176, 236-253, 309-326, 374-391 |
| 问题 | `FAngelscriptCodeCoverage::StartRecording()` 只调用 `ResetHits()` 把已有行命中归零，但 `FilesToCoverage` 从未在新 test run 开始时清空，也没有在 module `DiscardModule()` 后移除对应文件。`MapExecutableLines()` 只会 `Add()` 新文件；`WriteReportHtml()` / `WriteCoverageSummaries()` 却遍历整张 `FilesToCoverage`。与此同时，state dump 的 `DumpCodeCoverage()` 只导出 active modules 的 coverage。结果是：像 `AngelscriptScriptExampleCoverageTests.cpp` 这种会在测试末尾 `Engine.DiscardModule()` 的 coverage 用例，虽然模块已退出 active set，其旧文件仍会残留在 HTML/JSON summary 中，和 dump CSV 产生不一致。 |
| 根因 | coverage 子系统把“本轮命中数据重置”与“本轮文件集合重建”分离了，但只实现了前者，没有为 module 生命周期提供对应的 prune/clear 入口。 |
| 影响 | 多轮 automation 或顺序不同的 coverage 用例会把历史文件混入当前报告，出现 0-hit ghost files、覆盖率分母膨胀、HTML/JSON 与 `CodeCoverage.csv` 不一致；如果 CI 依赖 summary 报告，结论会被历史残留污染。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 coverage 报告只反映“当前测试轮次仍然有效的模块集合”，并统一 summary 与 dump 的数据源。 |
| 具体步骤 | 1. 在 `FAngelscriptCodeCoverage` 中新增 `ResetForNewRun()`，在 `StartRecording()` 时不仅 `ResetHits()`，还清空 `FilesToCoverage`，确保每轮 automation 从空集合重新 `MapExecutableLines()`。 2. 如果需要支持同一轮中途 `DiscardModule()` 的回收，则在 `FAngelscriptEngine::DiscardModule()` 或 coverage 管理器中新增按 `RelativeFilename` 的 prune 接口，把已丢弃模块的 coverage 条目移除。 3. 将 `WriteCoverageSummaries()` 的输入收敛到“active modules 可解析到的 coverage 集合”，与 `DumpCodeCoverage()` 共享同一层筛选逻辑，避免 HTML/JSON 与 CSV 各自遍历不同数据源。 4. 新增回归：先编译并丢弃模块 A，再开始新一轮 recording 只运行模块 B，断言 summary 中不再出现 A，且 `CodeCoverage.csv`、top-level JSON、HTML summary 的文件集合一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果简单在 `StartRecording()` 清空映射，却没有保证测试执行期间重新触发所有 `MapExecutableLines()`，可能导致正在运行的模块完全没有 coverage 条目；需要用回归覆盖“新一轮 compile 后自动重新建图”。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 为 `AngelscriptCodeCoverageTests.cpp` 增加跨两轮 recording 的回归，验证旧文件不会残留。 2. 运行 `Angelscript.TestModule.ScriptExamples.Coverage.*` 后检查 HTML/JSON/CSV 三种输出的文件列表一致。 3. 在同一进程内连续执行两次 coverage 用例，确认第二次报告不会包含第一次独有的模块文件。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-4 | Defect | 先处理 shared 宏的 module cleanup，先把最直接的测试状态泄漏切断 |
| P1 | Issue-5 | Architecture | 随后拆分 shared full / shared clone 语义，修正错误的引擎模式选择 |
| P2 | Issue-6 | Defect | 在前两项稳定后修 coverage 缓存生命周期，确保报告和 dump 一致 |

---

## 发现与方案 (2026-04-08 12:43)

### Issue-7：`AcquireFreshSharedCloneEngine()` 会把任意当前 scoped engine 当成 legacy global 并执行 reset

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | 410-432; 736-779; 287-312, 361-418 |
| 问题 | `AcquireFreshSharedCloneEngine()` 会先调用 `DestroyStrayLegacyGlobalTestEngine()`；后者通过 `FAngelscriptTestEngineScopeAccess::GetGlobalEngine()` 取回“legacy global”，再直接对该指针执行 `ResetSharedCloneEngine()`。但运行时里 `TryGetGlobalEngine()` 只是 `TryGetCurrentEngine()` 的别名，而 `DestroyGlobal()` 已经是 no-op。也就是说，只要当前上下文栈里存在任何 scoped engine，这条 fresh-shared 路径就会对那个当前 engine 做 shared reset，不要求它来自 shared storage。`GetSharedTestEngineAliasesSharedCloneTest` 还把这种“current == legacy global”行为固化成了 helper 契约。 |
| 根因 | TestInfrastructure 仍然沿用“legacy global engine”清理分支，但 Runtime 已经把 global 语义退化为 current-engine alias；helper 层没有按所有权区分 shared engine、outer scoped engine 和真正的 runtime-owned engine。 |
| 影响 | 在 outer `FAngelscriptEngineScope` 内调用 `AcquireFreshSharedCloneEngine()` 时，外层 engine 的模块、raw script module、generated class/function 都会被 `ResetSharedCloneEngine()` 清掉，随后 `DestroyGlobalEngine()` 还不会真正销毁它，形成“误 reset 但未退出作用域”的上下文混乱；这会直接制造顺序相关的 flaky 行为。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 去掉基于 legacy-global alias 的盲目 reset，只允许 fresh-shared 路径销毁 helper 自己拥有的 shared engine。 |
| 具体步骤 | 1. 在 `AngelscriptTestUtilities.h` 新增显式所有权判断（例如 `TryGetOwnedSharedTestEngine()` / `IsSharedTestEngine(FAngelscriptEngine*)`），`DestroyStrayLegacyGlobalTestEngine()` 只在指针等于 shared storage 或未来明确的 legacy owner 时才允许 reset。 2. 将 `AcquireFreshSharedCloneEngine()` 改为只执行 `DestroySharedTestEngine()` + `AcquireClean...`，不要再对 `TryGetCurrentEngine()` 做别名式清理。 3. 删除或弃用 `FAngelscriptTestEngineScopeAccess::GetGlobalEngine()` / `DestroyGlobalEngine()` 在测试层的“cleanup API”角色，把需要 current-engine 检查的地方改成显式命名的 `GetCurrentEngine()` 分支，避免继续把 current 与 global 混用。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 增加回归：先创建 isolated clone/full engine 并编译模块，在 outer `FAngelscriptEngineScope` 仍然存活时调用 `AcquireFreshSharedCloneEngine()`，断言 outer engine 的 `GetModuleByModuleName()` 和 raw `GetModule(..., asGM_ONLY_IF_EXISTS)` 仍然可见，离开 inner fresh-shared 流程后 current engine 恢复为 outer engine。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果有旧测试暗中依赖“fresh-shared 顺手清空当前 engine”的副作用，收紧所有权后这些测试会暴露出真实的缺少 cleanup 问题；需要通过回归逐个补显式 teardown。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 outer-scope 回归，验证 `AcquireFreshSharedCloneEngine()` 不会清掉外层 engine 的模块与 current context。 2. 回归 `Shared.EngineHelper.GetResetSharedTestEngineResetsSharedState`、`Shared.EngineHelper.ResetSharedEngineReleasesGeneratedComponentClasses`、`Engine.FullDestroyAllowsCleanRecreate`。 3. 串行运行一个 isolated-engine 测试后接一个 fresh-shared 测试，确认前者结果不因后者的 helper cleanup 改变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-7 | Defect | 先修 helper 所有权判断，避免 fresh-shared 路径继续误 reset 外层 engine |

---

## 发现与方案 (2026-04-08 12:44)

### Issue-8：`AcquireProductionLikeEngine()` 会把 ambient test engine 误判为 production engine

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 行号 | 107-123, 179-199, 453-459; 676-679, 718-723; 447-466; 109-124; 20-28 |
| 问题 | `TryGetRunningProductionEngine()` 在没有 subsystem-attached engine 时，只要 `FAngelscriptEngine::IsInitialized()` 为 true 就直接返回 `&FAngelscriptEngine::Get()`；而 `IsInitialized()` 只要求 context stack 非空，`GetOrCreateSharedCloneEngine()` 又会建立一个进程级 persistent `FAngelscriptEngineScope`。结果是：任何残留在 context stack 里的 shared/current test engine 都会被 `AcquireProductionLikeEngine()` 立即当成 production engine 返回。`ProductionHelperRejectsMissingProductionTest` 还把这条 fallback 行为写成了预期，而 `DumpAll` 与 `SourceNavigation` 测试都依赖这个 helper。 |
| 根因 | TestInfrastructure 把“当前存在可用 engine”与“当前有 production-owned engine”混成了同一概念，使用 runtime 的 current-engine 解析逻辑替代了真正的 runtime ownership 识别。 |
| 影响 | `Dump`、`Editor.SourceNavigation` 等本应针对 production-like/full-runtime 场景的测试，可能在前一个 shared 测试留下的 engine 上运行，继承其模块、诊断、bind 状态和 world context；测试结果将依赖执行顺序，而不是只依赖当前测试搭建的环境。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 “production engine” 的判定收紧为显式 runtime owner，不再接受 ambient current/shared test engine 作为 production-like fallback。 |
| 具体步骤 | 1. 重写 `TryGetRunningProductionEngine()`：优先返回 `UAngelscriptGameInstanceSubsystem` 挂接的 engine；如果不存在，则返回 `nullptr`，不要再用 `IsInitialized()` + `Get()` 把 ambient current engine 兜底成 production。 2. 在 `AcquireProductionLikeEngine()` 中保留“无生产引擎时创建 fresh full engine”的分支，并把这个分支作为默认 fallback；只有明确传入允许复用 ambient engine 的专用 helper 时，才可走当前上下文。 3. 更新 `ProductionHelperRejectsMissingProductionTest`，改为断言“无 subsystem / runtime owner 时返回 null”，并新增回归：先创建 shared test engine，再调用 `AcquireProductionLikeEngine()`，应得到一个与 shared engine 不同的 `OwnedEngine`。 4. 回归 `Dump/AngelscriptDumpTests.cpp` 与 `Editor/AngelscriptSourceNavigationTests.cpp`，确认它们在 shared test engine 已存在的情况下仍然走 fresh full engine 或真实 subsystem engine。 5. 在 `TESTING_GUIDE.md` 明确区分 `current engine`、`shared test engine`、`production engine` 三种来源，禁止继续把 `Get()` fallback 写进 production helper 语义。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 行为收紧后，一些旧测试会从“复用残留 shared engine”变成“创建 fresh full engine”，执行时间会上升，且可能暴露之前被 shared 状态掩盖的初始化缺口。 |
| 前置依赖 | 建议先完成 Issue-7，先去掉 legacy-global cleanup 对 current engine 的误伤，再收紧 production probe。 |
| 验证方式 | 1. 新增回归：shared engine 已存在时 `AcquireProductionLikeEngine()` 仍返回独立 owned full engine。 2. 串行运行一个 shared compile 测试后接 `Angelscript.TestModule.Dump.DumpAll.*` 和 `Angelscript.TestModule.Editor.SourceNavigation.Functions`，确认两者不复用 shared engine。 3. 检查 resolved engine 的 `GetInstanceId()` / 指针地址与 shared storage 不同，并确认 dump/source-navigation 结果在顺序互换后保持一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-8 | Architecture | 在 Issue-7 后立刻收紧 production probe，切断 dump/editor 测试对 ambient shared engine 的依赖 |

---

## 发现与方案 (2026-04-08 12:45)

### Issue-9：compile-trace helper 会泄漏 diagnostics 与日志配置，污染后续测试观测结果

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp` |
| 行号 | 89-112, 285-316, 319-324; 680-720; 730-745 |
| 问题 | `CompileModuleWithSummary()` 在进入前会直接 `Empty()` 当前 engine 的 `Diagnostics`、`LastEmittedDiagnostics` 并把 `bDiagnosticsDirty` 置回 `false`；它随后常被直接喂给 shared engine 使用。与此同时，`CompilePreparedModules()` 和 `AnalyzeReloadFromMemory()` 在抑制编译错误日志时都用 `UE_SET_LOG_VERBOSITY(Angelscript, Fatal)`，退出时再硬编码写回 `Log`，而不是恢复调用前的原始 verbosity。`AngelscriptStateDump::DumpDiagnostics()` 又会直接导出 `Engine.Diagnostics`。这意味着 compile-trace helper 不只是“读取编译结果”，还会永久改写同一个 engine 的诊断缓存和全局日志观测配置。 |
| 根因 | helper 设计把“为了收集本次 summary 暂时压制日志/清空 diagnostics”实现成了对共享运行态的原地修改，但没有为这些观测性操作建立 scope-based snapshot/restore。 |
| 影响 | 先运行 compile-summary / reload-analysis 测试，再运行依赖 diagnostics 或日志级别的测试时，后者看到的是被清空或被改成 `Log` 的状态，而不是进入 helper 前的真实环境；`Diagnostics.csv`、调试日志和失败排查信息都会受到顺序污染。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 compile-trace helper 改成纯观测型辅助：临时覆盖的 diagnostics 与日志级别必须在 helper 退出时恢复。 |
| 具体步骤 | 1. 在 `Shared` 层新增两个小型 scope guard：一个保存并恢复 `Angelscript` 日志类别当前 verbosity，另一个保存并恢复 `Engine.Diagnostics` / `LastEmittedDiagnostics` / `bDiagnosticsDirty`。 2. 让 `CompilePreparedModules()` 与 `AnalyzeReloadFromMemory()` 使用日志 scope guard，而不是固定把类别改成 `Fatal` 后再写死恢复到 `Log`。 3. 让 `CompileModuleWithSummary()` 在收集完 `OutSummary.Diagnostics` 后恢复进入函数前的 diagnostics 快照；如果某些调用者确实需要保留新 diagnostics，则显式增加参数（例如 `bPreserveDiagnosticsMutation`），默认值保持 `false`。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 增加两条回归：先在 shared engine 中制造一条已知 diagnostics，再调用 `CompileModuleWithSummary()`，断言 helper 返回后旧 diagnostics 仍在；再在测试入口设置非 `Log` 的 Angelscript verbosity，调用 `AnalyzeReloadFromMemory()` 或 `CompileModuleWithSummary(..., true)` 后断言 verbosity 恢复原值。 5. 用 `DumpDiagnostics()` 作为端到端校验，确认 compile-trace helper 执行前后的 `Diagnostics.csv` 文件集合与消息内容一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp` |
| 预估工作量 | M |
| 风险 | 如果有测试目前依赖 helper 顺手清空 diagnostics 或把日志恢复到 `Log`，修复后会暴露这些测试对隐藏副作用的耦合；需要把这类需求改成显式 setup。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 diagnostics-snapshot 与 log-verbosity 回归，验证 helper 退出后状态恢复。 2. 串行运行 `Shared.EngineHelper.CompileSummary*`、`Inheritance.*Analyze*`、`Dump.DumpAll.Summary`，确认顺序互换后 diagnostics 导出一致。 3. 人工检查一条故意失败的 compile-summary 用例，确认 suppressed log 只在 helper 作用域内生效。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-9 | Defect | 在 engine 生命周期两项问题稳定后处理，收敛 compile-trace helper 的观测状态泄漏 |

---

## 发现与方案 (2026-04-08 12:54)

### Issue-10：Parity 用例把 raw snippet module 留在 production engine 内，污染后续生产态测试

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | 19-27, 192-227, 245-261, 272-312, 554-570, 581-613, 670-694; 503-549 |
| 问题 | `GetProductionEngineForParity()` 直接返回当前 production/ambient engine，随后多个 parity 用例在这个长生命周期 engine 上调用 `Engine->GetScriptEngine()->GetModule(..., asGM_ALWAYS_CREATE)` 和 `CompileFunction()`，但函数结束前没有任何 `Engine.DiscardModule()` 或 `ScriptEngine->DiscardModule()`。`ResetSharedEngineDiscardsRawModulesTest` 已经证明 raw `asIScriptModule` 不会被 helper 自动回收，必须显式 reset/Discard 才会消失。也就是说 `CollisionProfileParity`、`CollisionQueryParamsParity`、`WorldCollisionParity`、`RuntimeCurveLinearColorParity`、`HitResultParity`、`StartupBindRegistryParity` 等 snippet module 会长期留在 production engine 里。 |
| 根因 | parity 测试沿用了“直接拿 production engine 做 smoke compile”的写法，但没有给 raw module 提供配套的 teardown fixture；raw script module 生命周期被错误地当成了局部栈对象。 |
| 影响 | 后续依赖 production-like engine 的 `Dump`、`Editor.SourceNavigation`、coverage 或其它 parity 测试会继承前面遗留的 raw module，导致 `GetModuleCount()`、state dump、diagnostics 与绑定可见性受到顺序污染；同一批测试重复运行时也不是干净起点。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 production-like raw snippet 编译建立统一的 auto-discard fixture，禁止 parity 测试继续裸用 `GetScriptEngine()->GetModule()` 而不回收。 |
| 具体步骤 | 1. 在 `Core` 或 `Shared` 层新增 raw snippet helper（例如 `FScopedRawSnippetModule`），统一封装 `GetModule(..., asGM_ALWAYS_CREATE)`、`CompileFunction()`、可选执行与析构时的 `ScriptEngine->DiscardModule()`/`DeleteDiscardedModules()`。 2. 迁移 `AngelscriptEngineParityTests.cpp` 中当前直接操作 raw module 的用例，让每个测试通过 helper 创建并自动回收 `CollisionProfileParity`、`WorldCollisionParity`、`StartupBindRegistryParity` 等模块。 3. 如果某些 parity 用例只需要“能编译”而不需要保留 module，可直接改用 `BuildModule()` 或新的 `CompileRawSnippet()` 包装，并把 module 名纳入 tracked cleanup。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 新增 production-engine 版本的回归：先记录 `GetScriptEngine()->GetModuleCount()`，运行一次 parity raw snippet helper，退出后断言 module count 恢复且 `GetModule(..., asGM_ONLY_IF_EXISTS)` 返回空。 5. 对 `Dump` 或另一条 production-like 测试做串行验证，确认在 parity 测试之后运行时不会看到这些 helper 模块。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 helper 在析构时无差别删除同名 module，而测试环境里恰好存在真实 production module，同名冲突会误删有效模块；需要限定 helper 只管理测试前不存在、且由 helper 自己创建的 raw module 名称。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增回归，验证 parity raw snippet 退出后 `GetModuleCount()` 回到初始值。 2. 运行 `Angelscript.TestModule.Parity.*` 后紧接 `Angelscript.TestModule.Dump.*`，确认 dump 中不再出现 parity helper module。 3. 同一进程内重复运行一条 parity 用例两次，确认第二次不会命中上轮残留 module。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-10 | Defect | 先处理 production-engine raw module cleanup，避免 parity 结果继续污染后续生产态测试 |

---

## 发现与方案 (2026-04-08 12:55)

### Issue-11：annotated 测试普遍把 `DiscardModule()` 当成完整 teardown，导致生成类/函数壳对象跨测试残留

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp` |
| 行号 | 1045-1067; 34-40, 82-95; 161-205, 355-445; 22-47; 35-39, 57-59; 40-93 |
| 问题 | Runtime 的 `DiscardModule()` 只会把 `UASClass::ScriptTypePtr`、`ConstructFunction`、`DefaultsFunction` 和 `UASFunction::ScriptFunction`/`ValidateFunction` 置空，并不会移除 root、清理 `RF_Standalone` 或回收对应 UObject。`ResetSharedEngineReleasesGeneratedComponentClassesTest` 已经证明，真正让生成类对象消失还需要额外的 `ResetSharedCloneEngine()` + `CollectGarbage()`。但当前大量 annotated 测试仍把单次 `Engine.DiscardModule()` 当成完整 teardown：例如 shared helper 测试、`SourceNavigation`、script example compile 支撑以及多条 full 宏测试都只做 module discard。 |
| 根因 | 测试层缺少一个“annotated module 退出时做对象级 cleanup”的统一 fixture，导致调用者沿用 plain-module 的 teardown 习惯，把 module-level discard 错当成 generated UObject cleanup。 |
| 影响 | 生成类/函数壳对象会在 shared engine、production engine，甚至 full 宏背后的 thread-local engine 中继续存活，污染后续 `FindGeneratedClass()`/`FindGeneratedFunction()`、source navigation、反射查询与覆盖率统计；测试顺序一变，就可能读到上一条用例留下的 detached symbol。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 annotated 测试建立专门的 fixture，把“discard module”与“generated UObject cleanup”绑定成一个不可分割的 teardown 合约。 |
| 具体步骤 | 1. 在 `Shared` 层新增 annotated test fixture（例如 `FScopedAnnotatedTestModule`），记录 engine 来源与 module 名，析构时至少执行 `Engine.DiscardModule()`；当 engine 是 shared helper 所有或测试自建 full engine 时，继续调用 `ResetSharedCloneEngine()` 或销毁 owned engine 以回收 detached `UASClass`/`UASFunction`。 2. 对 `GetOrCreateSharedCloneEngine()` / `AcquireProductionLikeEngine()` 路径下的 annotated 测试进行分类：shared compile/helper 测试改为 fresh shared + reset teardown；只需要生成类验证的 editor/example 用例优先切到 owned full engine，避免把 detach 壳对象留在真实 production engine。 3. 将 `AngelscriptTestEngineHelperTests.cpp` 中只做 `DiscardModule()` 的 annotated 用例、`AngelscriptSourceNavigationTests.cpp`、`AngelscriptScriptExampleTestSupport.cpp` 和 `AngelscriptBlueprintCallableReflectiveFallbackTests.cpp` 迁移到新 fixture。 4. 新增回归：先运行一个 annotated 编译测试并退出 fixture，再遍历 `TObjectIterator<UASClass>` / `TObjectIterator<UASFunction>`，断言指定类名/函数名在 teardown 后数量归零；随后再运行另一条 `FindGeneratedClass()` 测试，确认不会命中旧壳对象。 5. 文档中明确 plain module 与 annotated module 的 teardown 差异，禁止继续在 annotated 场景里只写 `DiscardModule()`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp` |
| 预估工作量 | L |
| 风险 | 如果对真实 production engine 直接做 shared-style reset，可能清掉当前进程里其它 production module 或 subsystem 状态；需要优先把 annotated production-like 用例迁移到 owned full engine，再在 fixture 中按 engine ownership 选择 teardown 策略。 |
| 前置依赖 | 建议先完成 Issue-2，使 shared reset 具备回收 detached `UASFunction` 的能力，否则新 fixture 只能清掉类壳对象，函数壳对象仍会残留。 |
| 验证方式 | 1. 新增 annotated teardown 回归，验证 fixture 退出后目标 `UASClass`/`UASFunction` 数量归零。 2. 串行运行 `Shared.EngineHelper.GeneratedSymbolLookup`、`Editor.SourceNavigation.Functions`、`ScriptExamples.Coverage.*`，确认顺序互换后不会命中上一条测试的 generated symbol。 3. 人工检查 `FindGeneratedClass()`/`FindGeneratedFunction()` 在 teardown 后返回空，而不是返回 detached 壳对象。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-11 | Defect | 在 shared reset 补强后立刻统一 annotated teardown，切断 generated symbol 残留 |

---

## 发现与方案 (2026-04-08 12:56)

### Issue-12：`FAngelscriptTestFixture` 处于“定义了但无人使用”的状态，engine 生命周期规则无法集中收敛

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | 734-809; 158-172, 194-208, 266-280; 19-27, 192-227; 22-47; 289-323, 447-500 |
| 问题 | `FAngelscriptTestFixture` 已经提供 `SharedClone` / `IsolatedFull` / `ProductionLike` 三种 engine 入口，但在整个 `AngelscriptTest` 目录里并没有实际调用点；我执行 `rg -n "FAngelscriptTestFixture"` 时只命中 `AngelscriptTestUtilities.h` 自身的定义。与此同时，各测试文件仍在手写自己的 engine 获取和 teardown：`EngineCoreTests.cpp` 手工清 context stack 与 shared/global engine，`EngineParityTests.cpp` 自定义 `GetProductionEngineForParity()`，`SourceNavigationTests.cpp` 手工 `AcquireProductionLikeEngine()` + `DiscardModule()`，helper 自测也各自写 `DestroySharedTestEngine()` / `ON_SCOPE_EXIT`。 |
| 根因 | fixture 只收敛了“拿到某种 engine”的入口，没有继续吸纳 module tracking、annotated teardown、production-engine 识别和 raw-module cleanup，因此测试作者仍然倾向于直接调用底层 helper；久而久之，fixture 变成了悬置抽象。 |
| 影响 | 前面几轮已经发现的共享 engine 泄漏、production probe 漂移、annotated cleanup 不一致、raw module 残留等问题，都会在不同测试文件里重复出现，因为修复无法通过一个统一 fixture 向外传播；后续新增测试也会继续复制错误模式。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `FAngelscriptTestFixture` 升级为测试基础设施的唯一入口，吸纳 module cleanup 与 ownership 规则，然后逐步迁移手写 lifecycle 代码。 |
| 具体步骤 | 1. 扩展 `FAngelscriptTestFixture`：加入显式的 module cleanup 策略枚举（如 `None` / `TrackedModules` / `AnnotatedModuleFixture` / `RawModuleFixture`），并把 `AcquireProductionLikeEngine`、shared reset、owned full engine 销毁统一封装到析构路径。 2. 在 fixture 内部提供 `BuildTrackedModule()`、`CompileAnnotatedTrackedModule()`、`CreateRawSnippetModule()` 等 API，把 module 名记录到 fixture 自己的 cleanup 列表，禁止调用方再手写 `ON_SCOPE_EXIT` + `DiscardModule()`。 3. 先迁移最容易复发问题的几组测试：`Core/AngelscriptEngineCoreTests.cpp`、`Core/AngelscriptEngineParityTests.cpp`、`Editor/AngelscriptSourceNavigationTests.cpp`、`Shared/AngelscriptTestEngineHelperTests.cpp`。 4. 迁移后将底层裸 helper 标记为“仅 fixture 内部使用”或在文档里明确为高级 API，避免新增测试继续绕开 fixture。 5. 为 fixture 本身补一组自测，覆盖 shared/full/production-like 三种模式下的 current-engine restore、module cleanup、annotated teardown 和 raw-module teardown。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md` |
| 预估工作量 | L |
| 风险 | 如果一次性迁移过多测试文件，容易把历史上依赖隐藏副作用的用例一起打破；应先从最小代表集开始，边迁移边补 fixture 自测，确认行为契约后再批量推广。 |
| 前置依赖 | 建议先完成 Issue-10 和 Issue-11 的 teardown 契约设计，把 raw module 与 annotated module 的清理规则固化到 fixture，再做迁移。 |
| 验证方式 | 1. fixture 自测通过，并覆盖三种 engine mode 的 setup/teardown。 2. 迁移后的 `Core` / `Editor` / `Shared` 代表性测试在顺序互换后结果一致。 3. 再次执行 `rg -n "FAngelscriptTestFixture"`，确认已出现真实测试调用点，而不再只有定义。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-12 | Architecture | 在前两条 cleanup 缺陷收敛后推进 fixture 化迁移，防止相同问题反复扩散 |

---

## 发现与方案 (2026-04-08 13:03)

### Issue-13：`TickWorld()` helper 在一次 world tick 后又手工重放 actor/component tick，导致场景测试基线偏离真实运行时

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h`, `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorLifecycleTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptScriptSpawnedActorOverrideTests.cpp` |
| 行号 | 46-70; 149-157; 199-207; 282-303; 343-397 |
| 问题 | `TickWorld()` 每轮先执行 `World.Tick(ELevelTick::LEVELTICK_All, DeltaTime)`，随后又遍历全部 actor 调 `Actor->Tick(DeltaTime)`，再遍历已注册组件调 `Component->TickComponent(...)`。这不是“辅助推进 world”，而是把同一轮 tick 的 actor/component 生命周期额外重放一遍。多个场景测试已经开始迁就这种偏差：`Actor.Tick`、`Component.Tick`、coverage component 测试都只断言 `TickCount >= 5`；`ScriptActor.TickRunsNTimes` 甚至要靠 `World.TimeSeconds` 去滤掉同一 world time 下的重复 tick，说明 helper 当前行为与“每次调用只产生一次逻辑 tick”的真实语义不一致。 |
| 根因 | 测试 helper 试图手工补齐脚本对象的 tick 驱动，但没有把 UE world 自身已经负责 actor/component tick 这一层排除掉，结果形成“world tick + 手工 replay tick”的双重驱动。 |
| 影响 | 依赖 tick 次数、tick 顺序或副作用累计的场景测试会系统性高估执行次数，隐藏真实的重复调用/重入缺陷；未来一旦把 helper 改正，这些用 `>=` 或 `LogicalTickCount` 规避双 tick 的测试会成批翻转，暴露当前基线并不等于真实运行时。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `TickWorld()` 收敛为“只推进 world 一次”的单一职责 helper，并将额外的手工 actor/component tick 分拆为显式 opt-in 的诊断辅助。 |
| 具体步骤 | 1. 将 `AngelscriptScenarioTestUtils::TickWorld()` 改为只保留 `World.Tick(...)`，删除内部对 `Actor->Tick()` 和 `Component->TickComponent()` 的手工重放。 2. 如果少数测试确实需要脱离 world 驱动、单独触发 actor/component tick，则新增显式 helper（例如 `ReplayActorTicksForDiagnostics()`），默认场景测试不得调用。 3. 回归并修正依赖双 tick 的断言：把 `ActorLifecycleTests.cpp`、`ComponentScenarioTests.cpp`、`AngelscriptScriptExampleCoverageTests.cpp` 中的 `TickCount >= 5` 改成精确等于请求 tick 次数；删除 `ScriptSpawnedActorOverrideTests.cpp` 中依赖 `World.TimeSeconds` 去重的 workaround，直接断言原始 `TickCount` 或等价计数与 `ScriptActorScenarioTickCount` 一致。 4. 在 `Shared/AngelscriptScenarioTestUtils` 新增 helper 自测或代表性场景回归，覆盖 actor tick、component tick、destroy/end-play 后单轮 world tick 这三类路径，确保 helper 修正后仍能驱动需要的生命周期。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h`, `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorLifecycleTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptScriptSpawnedActorOverrideTests.cpp` |
| 预估工作量 | M |
| 风险 | 个别测试可能一直依赖双 tick 来“补齐”某些脚本回调；修复后这些测试会失败，但这是暴露 helper 偏差后的必要清理，不能再通过放宽断言继续掩盖。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行 `Angelscript.TestModule.Actor.Tick`、`Angelscript.TestModule.Component.Tick`、`Angelscript.TestModule.ScriptExamples.Coverage.Component`、`Angelscript.TestModule.ScriptActor.TickRunsNTimes`，确认 tick 次数精确等于请求次数。 2. 串行运行 `ReceiveEndPlay` / `ReceiveDestroyed` 类测试，确认 world 单轮 tick 仍能完成销毁路径而不需要额外 replay。 3. 随机抽取一个 blueprint/runtime 场景测试使用同一 helper 重跑，确认不再需要 `>=` 或 `LogicalTickCount` 一类补偿逻辑。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-13 | Defect | 先修正场景 helper 的 tick 语义，再回归所有依赖手工 world tick 的 actor/component/coverage 场景测试 |

---

## 发现与方案 (2026-04-08 13:04)

### Issue-14：debugger 入口 helper 会把带 `DebugServer` 的 shared Full test engine 误判为 production debugger engine

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | 125-149, 158-186; 78; 1455; 20-25; 20-26; 22-28; 48-68; 469-500 |
| 问题 | `TryGetRunningProductionDebuggerEngine()` 在找不到 subsystem-attached engine 后，会直接 `SnapshotAndClear()` 整个 context stack，并返回最后一个 `DebugServer != nullptr` 的 engine。与此同时，shared test engine 路径实际调用 `CreateIsolatedFullEngine()`，而 runtime 默认 `DebugServerPort = 27099`，full engine 初始化时会创建 `FAngelscriptDebugServer`。结果是：只要进程里残留了一个 shared/full test engine，debugger smoke/stepping/breakpoint 测试就可能把它当成“production debugger engine”并通过 `SessionConfig.ExistingEngine` 直接附着；`FAngelscriptDebuggerTestSession` 也会无条件复用这个外部 engine，而不是创建自己的隔离 debugger engine。现有 helper 自测只验证“真实 debuggable production engine 存在时优先选它”，没有覆盖“没有 subsystem owner 时必须拒绝 shared test engine”的负向场景。 |
| 根因 | debugger helper 把“有 DebugServer 的 engine”与“production-owned debugger target”混为一谈，仍然依赖 ambient context stack 兜底，而 shared test engine 恰好又是带调试服务器的 full owner。 |
| 影响 | debugger 测试会顺序相关地附着到前一条测试遗留的 shared/full test engine，继承其模块、断点、调试状态和 diagnostics；当真实 production engine 不存在时，测试仍可能错误通过，掩盖 subsystem/debugger 初始化缺口，或者在错误 engine 上修改断点状态，反向污染后续测试。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 取消 debugger helper 对 ambient context stack 的兜底识别，显式区分“真实 production debugger engine”与“为测试临时创建的 owned debugger engine”。 |
| 具体步骤 | 1. 将 `TryGetRunningProductionDebuggerEngine()` 收紧为只接受 `UAngelscriptGameInstanceSubsystem` 挂接且 `DebugServer != nullptr` 的 engine；删除对 `SnapshotAndClear()` 后扫描 context stack 的 fallback。 2. 在 `Shared` 层新增 `AcquireDebuggerLikeEngine()` 或扩展 `FAngelscriptDebuggerSessionConfig`：当没有真实 production debugger engine 时，由 session 自己创建一个带唯一 `DebugServerPort` 的 owned full engine，而不是复用 ambient shared engine。 3. 更新 `AngelscriptDebuggerSmokeTests.cpp`、`AngelscriptDebuggerSteppingTests.cpp`、`AngelscriptDebuggerBreakpointTests.cpp`：如果测试目标是“生产引擎上的 debugger”，就显式要求 subsystem-owned engine；如果目标只是协议/断点/step 行为，则改用新的 owned debugger fixture，避免再通过 `TryGetRunningProductionDebuggerEngine()` 偷拿环境中的测试引擎。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 增加负向回归：先创建 shared engine，再确保没有 subsystem-owned production engine，断言 `TryGetRunningProductionDebuggerEngine()` 返回空；再增加 owned-debugger fixture 回归，验证它不会与 shared engine 复用同一实例。 5. 在 `TESTING_GUIDE.md` 补充 debugger 测试约束，明确 shared/full test engine 不是 production debugger target。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 收紧 helper 后，当前依赖 ambient debugger engine 的测试可能立即暴露出没有初始化 subsystem 或没有显式创建 owned debugger engine 的问题；但这正是要把环境依赖从隐式改成显式。 |
| 前置依赖 | 建议先完成 Issue-8，统一 production helper 对“ambient current engine 不是 production engine”的判定规则。 |
| 验证方式 | 1. 新增负向回归：仅创建 shared full test engine 时，`TryGetRunningProductionDebuggerEngine()` 必须返回空。 2. 运行 `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*`，确认它们只会绑定 subsystem-owned engine 或新的 owned debugger fixture。 3. 在 debugger 测试前后串行插入一条 shared engine 测试，确认 debugger 会话的 engine 指针、端口和模块状态不再与 shared storage 复用。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-14 | Defect | 在 production/helper 语义收紧后尽快处理，避免 debugger 测试继续附着到错误引擎 |

---

## 发现与方案 (2026-04-08 13:05)

### Issue-15：shared engine 上的 raw `asIScriptModule` 编译路径没有任何自动回收，导致 direct-compile 测试跨用例残留模块

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | 99-101; 79-134; 114-134; 48-71; 503-549 |
| 问题 | `ASTEST_BEGIN_SHARE` 只建立 `FAngelscriptEngineScope`，完全不负责回收模块。`EngineCoreTests.cpp` 的 `CompileSnippet` / `ExecuteSnippet`、`AngelscriptMiscTests.cpp` 的 `DuplicateFunction`、`AngelscriptOperatorTests.cpp` 的 `GetSet` 都直接在 shared engine 上走 `ScriptEngine->GetModule(..., asGM_ALWAYS_CREATE)` 的 raw `asIScriptModule` 路径，测试体结束前没有 `DiscardModule()` / `DeleteDiscardedModules()` / `ResetSharedCloneEngine()`。而 helper 自测 `ResetSharedEngineDiscardsRawModulesTest` 已明确证明这类 direct script-engine module 不会进入 `Engine.GetModuleByModuleName()` 的 tracked 模块集合，且只有显式 `ResetSharedCloneEngine()` 后才会从 `asGM_ONLY_IF_EXISTS` 查询中消失。也就是说，当前多条 shared raw-compile 测试在一次进程内都会把 raw module 留在共享引擎里。 |
| 根因 | 现有测试基础设施只给 tracked Angelscript module 和 production raw snippet 设计了 cleanup 思路，没有为 shared engine 上的 raw `asIScriptModule` 提供统一的 RAII/fixture。 |
| 影响 | 重跑或顺序交换 raw-compile 测试时，shared engine 会继承前一条测试遗留的 raw module 名称、脚本 section 和编译诊断；由于这些模块不在 helper 的 tracked 集合中，测试作者很容易误以为 `ASTEST_END_SHARE` 已经完成 teardown，实际却把状态泄漏到了下一条 shared 用例。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 shared raw `asIScriptModule` 路径补上统一的 auto-discard fixture，并禁止测试直接在 shared engine 上裸用 `GetModule(..., asGM_ALWAYS_CREATE)`。 |
| 具体步骤 | 1. 在 `Shared` 层新增 raw module RAII（可与 Issue-10 共用底层实现，例如 `FScopedRawSnippetModule` / `CreateTrackedRawModule()`），统一封装 `GetModule(..., asGM_ALWAYS_CREATE)`、可选 `CompileFunction()` / `Build()`，以及析构时的 `DiscardModule()` + `DeleteDiscardedModules()`。 2. 为 shared 场景增加对应 helper 或宏（例如 `ASTEST_CREATE_RAW_MODULE_SHARE()`），让 raw module 名自动登记到当前 fixture，而不是散落在测试体里手写字符串。 3. 迁移当前已验证的泄漏点：`AngelscriptEngineCoreTests.cpp` 的 `CompileSnippet` / `ExecuteSnippet`，`AngelscriptMiscTests.cpp` 的 `DuplicateFunction`，`AngelscriptOperatorTests.cpp` 的 `GetSet`。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 增加回归：连续两次在 shared engine 上创建同名 raw module，第一次退出 fixture 后断言 `asGM_ONLY_IF_EXISTS` 返回空，第二次应从干净状态重新创建且 raw module 数量不会累积。 5. 文档中明确 shared raw module 与 tracked `BuildModule()` 是两套 cleanup 合约，不能依赖 `ASTEST_BEGIN_SHARE` 的裸 scope 自动清理。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 如果 fixture 在 shared engine 上按名字无差别丢弃 raw module，可能误伤确实希望跨断言复用的少数 raw 测试；需要把“共享但需保留”与“共享且 auto-discard”拆成显式 API，而不是继续让默认行为含糊。 |
| 前置依赖 | 建议与 Issue-10 合并设计同一个 raw-module cleanup 抽象，避免 shared/prod 两条路径再各自复制一份逻辑。 |
| 验证方式 | 1. 运行新增 shared raw-module 回归，验证 fixture 退出后 `asGM_ONLY_IF_EXISTS` 返回空。 2. 回归 `Angelscript.TestModule.Engine.CompileSnippet`、`Angelscript.TestModule.Engine.ExecuteSnippet`、`Angelscript.TestModule.Angelscript.Misc.DuplicateFunction`、`Angelscript.TestModule.Angelscript.Operators.GetSet`，确认顺序互换和重复执行时结果一致。 3. 在 helper 日志中记录 raw module count，确认 shared raw tests 退出后不会继续累计。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-15 | Defect | 在 shared module cleanup 主线下优先补齐 raw-module fixture，切断 direct-compile 路径的残留状态 |

---

## 发现与方案 (2026-04-08 13:14)

### Issue-16：engine helper 自测把 ambient 状态当作契约，导致 shared/production 语义回归不可靠

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | 268-312, 447-467; 107-123, 179-199 |
| 问题 | helper 自测当前把“进程里已经存在什么 engine”写成了测试前置条件，而不是显式构造基线。`Shared.EngineHelper.SharedTestEngineNeverSilentlyAttachesToProductionEngine` 断言调用 `GetOrCreateSharedCloneEngine()` 之后 `CurrentEngine` 不应变化（`AngelscriptTestEngineHelperTests.cpp:268-285`），但真实 helper 在首次创建 shared engine 时会建立 persistent `FAngelscriptEngineScope`（`AngelscriptTestUtilities.h:179-199`），必然把 shared engine 压入 context stack。紧接着，`GetSharedTestEngineAliasesSharedCloneEngine` 又在显式 `DestroySharedTestEngine()` 后断言重建 shared engine 会把自己安装成 current/global alias（`AngelscriptTestEngineHelperTests.cpp:287-312`）。`ProductionHelperRejectsMissingProductionEngine` 还接受“只要 `IsInitialized()` 为 true，就把 ambient current engine 当成 production engine”（`AngelscriptTestEngineHelperTests.cpp:447-467`），与 `TryGetRunningProductionEngine()` 的 fallback 实现绑定在一起（`AngelscriptTestUtilities.h:107-123`）。这些测试组合起来形成了互相矛盾、且依赖执行顺序的回归网。 |
| 根因 | helper 自测没有使用显式 fixture 固定“无 shared engine / 有 shared engine / 有 subsystem-owned production engine”三种起始状态，而是直接读取当前进程状态并把它当作预期；结果是测试在验证环境，而不是验证契约。 |
| 影响 | 这些回归会在 shared engine 已经被前序测试创建时“真空通过”，却无法覆盖首次创建路径和无 production owner 路径；一旦 helper 语义继续漂移，回归可能仍然全绿，导致真正的隔离缺陷长期躲在测试网后面。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 helper 语义回归拆成显式 baseline fixture，禁止在同一个测试里读取 ambient 进程状态后再决定预期。 |
| 具体步骤 | 1. 在 `Shared` 层新增最小状态 fixture，例如 `FAngelscriptEngineProbeFixture`，显式提供 `WithoutSharedEngine()`、`WithSharedEngine()`、`WithSubsystemOwnedProductionEngine()` 三种 arrange 入口，并在析构时调用 `DestroySharedAndStrayGlobalTestEngine()` 恢复基线。 2. 将 `SharedTestEngineNeverSilentlyAttachesToProductionEngine` 拆成两个独立测试：一条覆盖“首次创建 shared engine 会建立 current-engine scope，但不得伪装成 subsystem-owned production engine”；另一条覆盖“已有 shared engine 时重复获取不得替换为新实例”。 3. 将 `GetSharedTestEngineAliasesSharedCloneEngine` 改成只验证别名关系与 shared-instance 复用，不再顺带承担 current/global 语义验证；current/global 语义单独由 fixture 化测试覆盖。 4. 将 `ProductionHelperRejectsMissingProductionEngine` 改成显式负向回归：在无 subsystem owner 且仅存在 shared/current test engine 时，`TryGetRunningProductionEngine()` 必须返回空；如果需要测试 subsystem 场景，就在同一文件内单独创建带 `UAngelscriptGameInstanceSubsystem` 的 production fixture。 5. 在 `TESTING_GUIDE.md` 增补“helper contract tests must build their own engine baseline”规则，避免后续继续写成 ambient-state assertions。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 拆分后会立即暴露当前 helper 的名实不符行为，导致若干旧回归翻转；但这是把“环境依赖”从测试契约里剥离的必要步骤，不能再靠模糊前置状态维持表面通过。 |
| 前置依赖 | 建议先同步 Issue-7、Issue-8 的 helper 语义收紧方案，避免新回归继续围绕旧的 current/global fallback 设计。 |
| 验证方式 | 1. 新增 fixture 化回归，分别在“无 shared / 有 shared / 有 subsystem owner”三种基线下运行，确认结果不受执行顺序影响。 2. 将 `Shared.EngineHelper.*` 相关测试重复执行两轮，确认第二轮不会因为首轮残留 shared scope 而改变结果。 3. 人工检查 `Session Frontend` 中的失败信息，确认每条回归都明确指出是哪种 baseline 被破坏。 |

### Issue-17：`ExecuteGeneratedIntEventOnGameThread()` 的布尔返回值无法表示脚本调用失败

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 行号 | 434-485; 1947-1954, 2605-2644; 141-142, 300-301, 330-331; 223-226, 361-364; 82, 203, 267 |
| 问题 | `ExecuteGeneratedIntEventOnGameThread()` 只有在 `Object == nullptr` 或 `Function == nullptr` 时才返回 `false`；一旦进入 `Invoke()`，无论 `UASFunction::RuntimeCallEvent()` 是否因为线程检查失败、脚本异常或返回值被置零而早退，helper 最终都会返回 `true`（`AngelscriptTestEngineHelper.cpp:434-485`）。运行时的 `UASFunction` 实现本身在异常路径下只是记录日志并把返回值改成 `0`，并不会把失败状态反馈给 caller（例如 `UASFunction_DWordReturn::RuntimeCallEvent()` 在 `ASClass.cpp:2605-2644`）。但 `HotReloadScenarioTests`、`CoverageTests`、`NativeEngineBindingsTests` 等大量测试都把这个布尔值直接当成“反射调用成功”的断言入口。也就是说，当前 helper 的布尔返回更像“已尝试调度到 game thread”，而不是“脚本执行成功”。 |
| 根因 | helper 设计时只封装了“切到 game thread 并执行”这一步，没有把 runtime 里的脚本执行状态向上传播；测试层因此误把一个调度状态当成了执行结果。 |
| 影响 | 当脚本函数抛异常、`CheckGameThreadExecution()` 拒绝执行或返回值被 fallback 成默认值时，测试可能仍然报告“调用成功”，只靠后续数值断言偶然兜底；一旦预期值恰好也是默认值，或测试只检查 `TestTrue(...)`，就会形成假阳性，削弱对 reflected-call 路径的回归能力。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“调度成功”和“脚本执行成功”拆成显式结果结构，测试 helper 必须返回真实的 invocation status。 |
| 具体步骤 | 1. 在 runtime 或 test helper 层新增结构化结果，例如 `FAngelscriptReflectedCallResult`，至少包含 `bDispatched`、`bExecuted`、`bScriptException`、`ExceptionMessage` 和 `ReturnValue`。 2. 为 `UASFunction` 增加测试可用的状态接口（例如 `RuntimeCallEventForTesting()` 或 `TryRuntimeCallEvent()`）：在 `WITH_DEV_AUTOMATION_TESTS` 下捕获 `CheckGameThreadExecution` 失败、`Context->m_status == asEXECUTION_EXCEPTION` 等状态，并把它们回传给调用方，而不是只写日志。 3. 将 `ExecuteGeneratedIntEventOnGameThread()` 改为在 game-thread lambda 中填充结果结构；对非 `UASFunction` 的原生 `ProcessEvent` 路径至少保留 `bDispatched=true`，并在文档里说明只有 script-generated `UFunction` 能提供完整执行状态。 4. 批量迁移调用点：把 `HotReloadScenarioTests.cpp`、`AngelscriptScriptExampleCoverageTests.cpp`、`AngelscriptNativeEngineBindingsTests.cpp` 等文件从 `TestTrue(..., ExecuteGeneratedIntEventOnGameThread(...))` 改成先断言 `bExecuted`，再断言 `ReturnValue`，必要时把 `ExceptionMessage` 打到 `AddError()`。 5. 在 helper 自测中新增负向回归：构造一个会抛异常的 generated `UFUNCTION`，断言新 helper 返回 `bExecuted=false`、`bScriptException=true`，且不再把异常路径伪装成成功。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 调整 helper 签名会波及较多调用点；如果一次性全量替换，容易引入机械性改错，需要先提供兼容包装层，再逐批迁移测试。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增“脚本异常时返回失败状态”的 helper 回归，并确认日志中包含具体异常文本。 2. 回归 `HotReload`, `Examples.Coverage`, `Bindings.NativeEngine` 中依赖 reflected-call helper 的代表性用例，确认它们改为断言真实执行状态。 3. 人工制造一个返回值默认也是 `0` 的异常脚本，确认旧 helper 会误判成功而新 helper 会明确失败。 |

### Issue-18：`AngelscriptTestUtilities.h` 直接耦合 runtime 与 AngelScript 私有内部，破坏测试基础设施边界

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 19-21, 218-243, 278-355, 571-572; 99, 154, 220, 296-298; 571-575; 1001-1023 |
| 问题 | `AngelscriptTestUtilities.h` 作为整个测试模块广泛包含的公共 helper 头，直接 `#include "source/as_context.h"`, `source/as_module.h`, `source/as_scriptengine.h`，并在 inline 逻辑里把 `Engine.GetScriptEngine()` 强转为 `asCScriptEngine*`，读取 `scriptFunctions.GetLength()`、`freeScriptFunctionIds.GetLength()` 等内部容器，还在多个 helper 中直接修改 `Engine.bUseAutomaticImportMethod` 与 diagnostics 内部字段（`AngelscriptTestUtilities.h:571-572`，`AngelscriptTestEngineHelper.cpp:99,154,220,296-298`）。与此同时，runtime 自己已经提供了 `GetToStringEntryCountForTesting()`、`GetBindDatabaseForTesting()`、`SetUseEditorScriptsForTesting()`、`SetAutomaticImportMethodForTesting()` 这类受控测试 seam（`AngelscriptEngine.h:571-575`, `AngelscriptEngine.cpp:1001-1023`）。也就是说，当前测试基础设施没有通过 runtime-owned testing API 观察行为，而是把私有实现细节扩散到了 test module 的公共边界。 |
| 根因 | 为了快速做 shared-engine 调试和 compile helper，测试层直接把 runtime/AngelScript internal types 拉进 header inline 实现，没有再回头收敛成 runtime 提供的 test-only façade。 |
| 影响 | 任何 AngelScript 升级、runtime 内部容器改名、shared state 重构，都会先打碎 test helper 编译边界，并迫使 `AngelscriptTest` 大面积跟着私有实现改动；同时公共 header 泄漏这些依赖后，所有包含它的测试 TU 都被绑死到 internal layout，模块边界形同虚设。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 internal probe 下沉回 runtime 的 test-only façade，测试模块只依赖稳定的 `WITH_DEV_AUTOMATION_TESTS` seam，不再直接包含 `source/` 私有头。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 中新增受控的 test-only 查询接口，例如 `GetRawModuleNamesForTesting()`、`GetDetachedGeneratedSymbolStatsForTesting()`、`ResetDiagnosticsForTesting()`，统一包裹当前对 `asCScriptEngine`、`Diagnostics`、`bUseAutomaticImportMethod` 的直接访问。 2. 将 `AngelscriptTestUtilities.h` 中依赖 `asCScriptEngine` 的实现移到 runtime `.cpp` 或新的 runtime testing helper 中，让测试模块通过前向声明或轻量 header 调用这些 seam；删除 `source/as_*` 私有 include。 3. 把 `AngelscriptTestEngineHelper.cpp` 中直接改写 `Engine.bUseAutomaticImportMethod` 的位置改成调用 `SetAutomaticImportMethodForTesting()`，并为 diagnostics 清空/恢复补对应的 runtime test API，而不是直接摸内部字段。 4. 将 `LogSharedEngineDebugState()` 这类调试辅助改造成返回结构化统计，而不是在 test header 中直接遍历 internal containers；日志格式化放回 test module，数据采集放在 runtime seam。 5. 新增一组 seam 自测，确认 test façade 暴露的 raw-module count、detached symbol stats、automatic-import toggle 与当前实现一致，然后再逐步删掉 test module 中的 internal cast。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | L |
| 风险 | 如果 façade 设计得过于贴近当前内部布局，只是把 `reinterpret_cast` 挪了位置，边界问题仍然存在；需要以“暴露测试需要的行为数据”而不是“转发内部指针”为原则。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 删除 `AngelscriptTestUtilities.h` 对 `source/as_*` 头的包含后，确认 `AngelscriptTest` 仍可编译。 2. 运行 shared-engine helper 自测，确认 raw-module 统计、generated-symbol cleanup 与 automatic-import 控制行为保持一致。 3. 对 runtime seam 新增单测，确保 façade 返回的数据与现有 internal probe 一致后再移除旧实现。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-17 | Defect | 先修复 reflected-call helper 的成功/失败信号，避免大量场景测试继续把异常路径误判为成功 |
| P1 | Issue-16 | Defect | 随后重写 helper 自测 baseline，确保 shared/production 语义回归不再依赖 ambient 状态 |
| P1 | Issue-18 | Architecture | 在前两条契约稳定后下沉 internal probe 到 runtime test seam，收紧测试基础设施与 runtime 的模块边界 |

---

## 发现与方案 (2026-04-08 13:24)

### Issue-19：`ASTEST_CREATE_ENGINE_FULL/CLONE` 的 `thread_local` engine 不会在 `ASTEST_END_*` 后销毁，导致隐藏 owner 跨测试存活

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp` |
| 行号 | 34-40, 57-63, 82-95, 126-139; 615-625, 1132-1227; 521-545; 158-172, 224-225, 328-329; 82-119 |
| 问题 | `ASTEST_CREATE_ENGINE_FULL()` 和 `ASTEST_CREATE_ENGINE_CLONE()` 都把 engine 放进 `static thread_local TUniquePtr`；但 `ASTEST_BEGIN_FULL/CLONE` 的退出逻辑只负责 `DiscardModule()`，没有 `Reset()` 这份 `thread_local` storage。Runtime 侧只有在 `FAngelscriptEngine::~FAngelscriptEngine()` / `Shutdown()` 时才会释放 full owner、递减 clone participant，并在“最后一个 full owner 销毁”时清空全局 type/shared state（`AngelscriptMultiEngineTests.cpp:521-545` 已显式验证这一契约）。这意味着任何使用 `ASTEST_CREATE_ENGINE_FULL()` 的测试在 `ASTEST_END_FULL` 之后，engine 对象仍然活到同线程下一次宏调用或线程退出；`Core` 中的 full-destroy 用例却只清 `shared/global` helper，并没有办法触及这份宏私有的 `thread_local` owner。 |
| 根因 | 宏体系把“模块级 teardown”与“engine 对象生命周期结束”拆开了，但 `FULL/CLONE` 创建宏选择了跨测试持久化的 `thread_local` storage，又没有在 lifecycle 宏里配对释放该 storage。 |
| 影响 | full-owner 的 type metadata、debug server、owned shared state 以及 clone participant 计数都可能跨测试残留，形成隐藏的顺序依赖；`LastFullDestroyClearsTypeState` / `FullDestroyAllows*Recreate` 这类验证“最后一个 full owner 退出后应清零”的核心回归，在前面跑过 `ASTEST_CREATE_ENGINE_FULL()` 用例时会不再从干净基线开始。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 保留现有宏调用形态，但把 `FULL/CLONE` 宏私有 storage 的销毁绑定到 `ASTEST_END_FULL/CLONE`，让 engine 生命周期真正止于测试体。 |
| 具体步骤 | 1. 在 `Shared` 层为宏增加显式 storage access/reset helper，例如 `GetThreadLocalFullMacroEngineStorage()`、`ResetThreadLocalFullMacroEngineStorage()`、`GetThreadLocalCloneMacroEngineStorage()`，避免 `ASTEST_CREATE_ENGINE_*` 把 `thread_local TUniquePtr` 隐藏在匿名 lambda 内，导致 `BEGIN/END` 无法回收。 2. 更新 `ASTEST_CREATE_ENGINE_FULL()` / `ASTEST_CREATE_ENGINE_CLONE()` 改为通过上述 helper 创建 engine；在 `ASTEST_BEGIN_FULL` / `ASTEST_BEGIN_CLONE` 的 `ON_SCOPE_EXIT` 中，先按现有逻辑 `DiscardModule()`，随后调用对应的 `ResetThreadLocal*Storage()`，确保 owner/clone 在测试离开时立即析构。 3. 对 `ASTEST_BEGIN_FULL` 增加一条宏回归：测试体内创建 full engine 并确认 `FAngelscriptType::GetTypes().Num() > 0`，离开 `ASTEST_END_FULL` 后立刻断言 type count 回到 0。 4. 对 `ASTEST_BEGIN_CLONE` 增加一条回归：在 source full engine 存在时创建 clone 宏 engine，离开 `ASTEST_END_CLONE` 后断言 source engine 的 `GetActiveCloneCountForTesting()` 回到进入前的值，避免 clone participant 残留。 5. 更新 `TESTING_GUIDE.md` / `README_MACROS.md`，把 `FULL/CLONE` 的生命周期说明从“auto-discard modules”补充为“auto-discard modules + auto-destroy macro-owned engine”，避免后续测试继续建立在错误假设上。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md` |
| 预估工作量 | M |
| 风险 | 如果少数旧测试暗中依赖“full engine 在测试结束后继续存活到下一条用例”的副作用，修复后会暴露这些测试对隐藏 owner 的耦合；但这是必须显式化的坏依赖。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行新增的 `Validation` 宏生命周期回归，验证 `ASTEST_END_FULL` 后 type count 归零、`ASTEST_END_CLONE` 后 clone count 恢复。 2. 串行运行一条 `ASTEST_CREATE_ENGINE_FULL()` 的编译测试后接 `Angelscript.TestModule.Engine.LastFullDestroyClearsTypeState`，确认后者不再受前序 full 宏测试影响。 3. 重复执行 `Angelscript.TestModule.Validation.GlobalBindingsMacro` 两轮，确认第二轮不继承第一轮留下的 full owner。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-19 | Defect | 优先修宏私有 engine 的真实销毁时机，先切断 hidden full/clone owner 的跨测试残留 |

---

## 发现与方案 (2026-04-08 13:27)

### Issue-20：`ClassGenerator.EmptyModuleSetup` 在 long-lived engine 上创建 raw module 却没有任何 teardown

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | 16-24, 32-56; 503-549; 179-204 |
| 问题 | `GetEngineForClassGeneratorTests()` 会优先复用 production engine，否则退回进程级 shared engine；`FAngelscriptClassGeneratorEmptyModuleSetupTest` 随后直接在这个 long-lived engine 上调用 `GetScriptEngine()->GetModule("Tests.ClassGenerator.EmptyModule", asGM_ALWAYS_CREATE)` 构造 raw `asCModule`，但函数结束前没有任何 `DiscardModule()`、`DeleteDiscardedModules()` 或 shared reset。`ResetSharedEngineDiscardsRawModulesTest` 已经证明 direct script-engine module 不会进入 tracked module 集合，必须显式 reset/Discard 才会从 `asGM_ONLY_IF_EXISTS` 查询中消失。也就是说，这个 class-generator setup 用例会把 `Tests.ClassGenerator.EmptyModule` 留在它借来的 shared / production engine 里。 |
| 根因 | class-generator 空模块测试为了就地构造 `FAngelscriptModuleDesc::ScriptModule`，直接借用了长生命周期 engine 和 raw `asIScriptModule` API，但没有把 raw module 生命周期封装成局部 fixture。 |
| 影响 | 重复执行 `EmptyModuleSetup` 或在它之后运行依赖 raw module 数、state dump、class generator reload 前置状态的测试时，起始环境不再干净；如果该测试附着的是 production engine，污染范围会扩大到后续 `Dump` / `Editor` / parity 一类生产态辅助测试。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 `EmptyModuleSetup` 改用 owned engine + raw-module RAII，禁止继续把 class-generator scaffold 留在 shared / production engine 中。 |
| 具体步骤 | 1. 删除 `GetEngineForClassGeneratorTests()` 对 production/shared engine 的复用逻辑；对 `FAngelscriptClassGeneratorEmptyModuleSetupTest` 直接使用 `CreateFullTestEngine()` 或 `FAngelscriptTestFixture(..., ETestEngineMode::IsolatedFull)`，把空模块脚手架限定在 test-owned engine 内。 2. 为 raw empty module 增加局部 RAII（可以复用 Issue-10 / Issue-15 计划中的 `FScopedRawSnippetModule` 设计）：构造时执行 `GetModule(..., asGM_ALWAYS_CREATE)`，析构时统一 `ScriptEngine->DiscardModule("Tests.ClassGenerator.EmptyModule")` 并 `DeleteDiscardedModules()`。 3. 将当前 `MakeShared<FAngelscriptModuleDesc>()` + 手填 `ScriptModule` 的逻辑改为通过该 RAII/helper 创建，避免今后其它 class-generator 测试继续复制同一模式。 4. 在 `ClassGeneratorTests.cpp` 新增回归：同一 automation test 中连续两次执行 empty-module setup，第一次 fixture 退出后断言 `GetScriptEngine()->GetModule("Tests.ClassGenerator.EmptyModule", asGM_ONLY_IF_EXISTS)` 返回空，第二次应从干净状态重新创建。 5. 如果后续确实需要 production-attached class-generator 覆盖，应单独建立 `ProductionLike` fixture，并要求每个 raw module 都走显式 auto-discard helper，而不是复用当前裸 API。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果有后续 class-generator 测试偷偷依赖这个空 raw module 在 engine 中长期存在，补 teardown 后会暴露这种隐式依赖；应把这类共享前置状态改成显式 fixture，而不是继续借旧残留。 |
| 前置依赖 | 建议与 Issue-10 / Issue-15 共享同一套 raw-module RAII 设计，避免 class-generator 再引入第三份 direct-module cleanup 实现。 |
| 验证方式 | 1. 新增 `EmptyModuleSetup` 重跑回归，确认第一次 teardown 后 `asGM_ONLY_IF_EXISTS` 返回空。 2. 串行运行 `Angelscript.TestModule.ClassGenerator.EmptyModuleSetup` 后接 `Angelscript.TestModule.Dump.DumpAll.Summary` 或另一条 raw-module 计数测试，确认输出中不再出现 `Tests.ClassGenerator.EmptyModule`。 3. 重复执行该用例两轮，确认第二轮不会命中首轮残留 raw module。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-20 | Defect | 在 raw-module cleanup 主线下尽快修复，先把 class-generator 空模块脚手架从 shared / production engine 中剥离出来 |

---

## 发现与方案 (2026-04-08 13:37)

### Issue-21：`CreateIsolatedCloneEngine()` / `ASTEST_CREATE_ENGINE_CLONE()` 会在无 source engine 时静默退化为 Full

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | 169-176; 57-63; 17, 152-162; 118-125, 246-253, 269-274; 554-557, 591-592, 648-649, 749-750; 654-666; 290-296 |
| 问题 | `CreateIsolatedCloneEngine()` 直接调用 `FAngelscriptEngine::CreateForTesting(..., Clone)`，但 runtime 的 `CreateForTesting()` 只有在 `TryGetCurrentEngine()` 非空时才真正 `CreateCloneFrom()`；否则会回退到 `CreateTestingFullEngine()`。runtime 自测已经明确验证“无 source engine 时 fallback 到 Full”。与此同时，`ASTEST_CREATE_ENGINE_CLONE()`、`TESTING_GUIDE.md` 以及多条测试（如 `Restore.*`、`Shared.EngineHelper.*`）都把它当成稳定的“isolated clone”入口。结果是：没有 ambient current engine 时，这些测试实际创建的是 Full owner；如果前序测试留下 shared/current engine，它们又会克隆那个残留 engine。 |
| 根因 | TestInfrastructure 把“请求 Clone mode”实现成了“从 ambient current engine 猜 source，否则偷偷退回 Full”，但 helper API、宏命名和文档都没有把这个条件暴露出来，也没有在调用点验证最终 creation mode。 |
| 影响 | 依赖 clone 语义的测试会在不同执行顺序下观察到两套完全不同的生命周期：一套是 Full owner，包含 startup bind replay、type metadata owner、global state；另一套是共享 source 的真正 Clone。结果既会掩盖 clone 路径缺陷，也会把 full-owner 副作用误带进本应轻量隔离的测试，形成顺序相关的不稳定结果。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 废弃“靠 ambient current engine 推断 clone source”的 helper 语义，把 clone 创建改成显式 source 驱动，并让宏/文档/验证一起收敛到真实 contract。 |
| 具体步骤 | 1. 在 `Shared/AngelscriptTestUtilities.h` 中把当前 `CreateIsolatedCloneEngine()` 拆成两个显式入口：`CreateCloneFromTestSource(FAngelscriptEngine& Source)` 和 `CreateIsolatedFullEngine()`；不要再保留“请求 Clone 但允许 silent fallback 到 Full”的公共 helper。 2. 对确实只想拿到“单个独立 engine”的测试，统一改用 `CreateIsolatedFullEngine()` / `CreateFullTestEngine()`；对确实要验证 clone 语义的测试，先显式创建 source full engine，再调用 `CreateCloneFromTestSource()`。 3. 更新 `ASTEST_CREATE_ENGINE_CLONE()`：如果没有显式 source fixture，就直接 `checkf` 失败并提示调用方先建立 source full engine；不要再返回 disguised Full engine。 4. 迁移当前已验证的误用调用点，例如 `Internals/AngelscriptRestoreTests.cpp` 和 `Shared/AngelscriptTestEngineHelperTests.cpp`，把“isolated clone”改成“owned full”或“source+clone pair”，并在这些测试里额外断言 `GetCreationMode()`、`OwnsEngine()`、`GetSourceEngine()`。 5. 同步修正 `TESTING_GUIDE.md`、`README_MACROS.md`、`MACRO_MIGRATION_GUIDE.txt` 中对 `ASTEST_CREATE_ENGINE_CLONE()` 的描述，明确 clone 必须依赖显式 source，不再把它写成无条件可用的廉价隔离模式。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/MACRO_MIGRATION_GUIDE.txt`, `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp` |
| 预估工作量 | M |
| 风险 | 行为收紧后，少数把 `CreateIsolatedCloneEngine()` 当作“廉价 Full engine”使用的旧测试会立即暴露出来；但这类依赖本身就是当前顺序不稳定的根源，必须显式改成 Full 或显式 source+clone。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增回归：在没有 current/source engine 时，请求 clone helper 应直接失败或被调用点显式改成 Full helper，而不再返回 `GetCreationMode()==Full` 的伪 clone。 2. 新增 source+clone 回归，断言 clone 路径的 `GetCreationMode()==Clone`、`OwnsEngine()==false`、`GetSourceEngine()!=nullptr`。 3. 回归 `Angelscript.TestModule.Internals.Restore.*` 与 `Angelscript.TestModule.Shared.EngineHelper.*`，确认它们在顺序互换后不再依赖 ambient current/shared engine。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-21 | Defect | 先修 helper/macro 的 clone 真实语义，避免后续测试继续在伪 clone 和真 clone 之间漂移 |

---

## 发现与方案 (2026-04-08 13:38)

### Issue-22：`TryGetRunningProductionSubsystem()` 会从任意 world context 抢一个 subsystem，production-like 测试缺少稳定绑定目标

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorInteractionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp` |
| 行号 | 46-78, 107-133; 94-113; 391-392; 161-164, 238-241, 311-314; 76-78, 138-140; 30, 74, 121, 163, 211 |
| 问题 | runtime 自己的 `UAngelscriptGameInstanceSubsystem::GetCurrent()` 只会基于 ambient world context 解析当前 subsystem；但测试 helper 的 `TryGetRunningProductionSubsystem()` 在 `GetCurrent()` 失败后，会直接遍历 `GEngine->GetWorldContexts()` 并返回遇到的第一个 `UAngelscriptGameInstanceSubsystem`。与此同时，`Shared.EngineHelper.ResetSharedEngineReleasesGeneratedComponentClasses`、`ScriptExamples.Coverage.*`、`ActorInteraction.*`、`SubsystemScenario.*` 等大量测试都会初始化 game subsystem/world。这样一来，只要进程里还挂着多个 scenario/test world，`RequireRunningProductionEngine()` / `AcquireProductionLikeEngine()` 就可能绑定到“前一条测试遗留的第一个 subsystem”，而不是当前测试刚创建的那个。 |
| 根因 | TestInfrastructure 把“当前测试需要的 production-like subsystem”实现成了全局扫描，并绕过了 runtime 已经提供的 world-context 解析语义；helper 没有要求调用方提供明确的 world/subsystem 身份。 |
| 影响 | coverage、dump、editor/source-navigation 一类 production-like 测试即使先创建了自己的 world，也仍可能编译到另一个测试世界附着的 engine 上，继承错误的模块、world context 与 tick owner；这会让同一条测试在不同执行顺序下连绑定对象都不一致。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 去掉“扫描全部 world context 抢第一个 subsystem”的 fallback，把 production-like engine 解析改成显式 world/subsystem 绑定。 |
| 具体步骤 | 1. 将 `TryGetRunningProductionSubsystem()` 收紧为只返回 `UAngelscriptGameInstanceSubsystem::GetCurrent()`；如果当前没有 ambient world context，就返回 `nullptr`，不要再遍历 `GEngine->GetWorldContexts()`。 2. 在 `Shared/AngelscriptTestUtilities.h` 新增显式入口，例如 `TryGetProductionSubsystemForWorld(UWorld&)` / `RequireProductionEngineForWorld(FAutomationTestBase&, UWorld&, const TCHAR*)`，让调用方基于自己创建的 world 获取对应 subsystem engine。 3. 更新 `AngelscriptScriptExampleCoverageTests.cpp`、`AngelscriptTestEngineHelperTests.cpp`、以及其它 production-like caller：在初始化 world/spawner 后，显式保存当前测试创建的 `UWorld` 或 `UAngelscriptGameInstanceSubsystem*`，后续只通过新的显式 helper 取 engine，不再调用全局扫描版 probe。 4. 在 `AcquireProductionLikeEngine()` 中保留“无显式 production subsystem 时创建 owned full engine”的 fallback，但只在 caller 没有 world/subsystem 目标时使用；不要再把进程里任意活着的 subsystem 当成默认 production engine。 5. 增加双世界负向回归：先创建 world A 和 world B，各自初始化 subsystem，切换 ambient world 或显式传入 world B 后，断言 helper 返回的是 B 的 engine；在无 ambient world 且未传 world 参数时，断言 helper 返回 `nullptr`/owned full fallback，而不是 world A 的残留 subsystem。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp` |
| 预估工作量 | M |
| 风险 | 收紧后，部分旧测试会失去“顺手捡到别的 world 上 subsystem engine”的隐藏 fallback，需要补显式 world/subsystem fixture；但这是把 production-like 目标固定下来的必要代价。 |
| 前置依赖 | 建议在 Issue-8 的 production-engine 判定收紧方案基础上一起实施，避免“ambient current engine 误判”和“任意 subsystem 误选”两条路径相互掩盖。 |
| 验证方式 | 1. 新增双世界回归，验证 helper 不再返回 world 列表中的第一个 subsystem。 2. 串行运行一条会初始化 game subsystem 的 scenario/actor 测试后，再运行 `ScriptExamples.Coverage.*`、`Editor.SourceNavigation.Functions` 或 `DumpAll`，确认这些 production-like 测试仍然绑定到自己显式指定的 world/subsystem。 3. 记录 resolved engine 的 `InstanceId` 与 world/subsystem 所属关系，确认顺序互换后保持一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-22 | Architecture | 在 production-like helper 主线上尽快收紧 subsystem 选择，先消除 world-context 级别的误绑定 |

---

## 发现与方案 (2026-04-08 13:40)

### Issue-23：macro/helper 层几乎没有验证 engine mode contract，导致 clone 语义漂移长期未被 `AngelscriptTest` 捕获

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | 62-80; 17, 147-163; 554-560, 591-596, 648-650, 749-753; 118-125, 246-253, 269-274; 72-77; 220-223, 247, 290-296 |
| 问题 | TestInfrastructure 自己提供了 `ASTEST_CREATE_ENGINE_CLONE()`、`CreateIsolatedCloneEngine()` 等公共入口，并在 `TESTING_GUIDE.md` 中把 clone 描述成稳定的轻量隔离模式；但 `Validation/AngelscriptMacroValidationTests.cpp` 只验证 `FULL`、`SHARE_CLEAN`、`SHARE_FRESH` 和 lifecycle end placement，完全没有 clone 宏回归。`Shared/AngelscriptTestEngineHelperTests.cpp` 与 `Internals/AngelscriptRestoreTests.cpp` 虽然频繁创建“isolated clone” engine，却只检查非空和业务结果，没有断言 `GetCreationMode()`、`OwnsEngine()`、`GetSourceEngine()`。在 `AngelscriptTest` 目录里，我能定位到的 creation-mode 断言只有 `AngelscriptCoreExecutionTests.cpp:76-77`，而它也只验证“不是未初始化枚举值”，并不校验 clone/full contract。真正的精确 contract 只存在于 runtime 的 `AngelscriptMultiEngineTests.cpp`。这使得 test macro/helper 层可以和 runtime 语义漂移，而 `AngelscriptTest` 本身不会报错。 |
| 根因 | 测试基础设施把宏/helper 视为“方便调用的薄包装”，长期只验证脚本执行结果，没有把 engine ownership / source / creation mode 视为需要自测的公共 API contract。 |
| 影响 | 一旦 helper 命名、文档和 runtime 真实语义不一致，新测试会继续复制错误假设，直到更底层的 runtime 测试或顺序相关失败把问题暴露出来；这会显著降低 TestInfrastructure 对自己公共入口的防回归能力。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 engine mode/ownership/source contract 提升为 TestInfrastructure 的一等测试目标，让宏、helper、文档和 runtime contract 在同一组回归里对齐。 |
| 具体步骤 | 1. 在 `Validation/AngelscriptMacroValidationTests.cpp` 新增专门的 mode-contract 回归，至少覆盖：`ASTEST_CREATE_ENGINE_FULL()` 返回 `Full + OwnsEngine==true`；clone helper 在显式 source 存在时返回 `Clone + OwnsEngine==false + GetSourceEngine()!=nullptr`；无 source 时走显式失败或显式 Full helper，而不是 silent fallback。 2. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 的代表性 helper 测试里加入前置 contract 断言，尤其是所有创建“isolated clone” engine 的测试，在继续执行业务断言前先验证 engine mode/source 是否符合预期。 3. 在 `Internals/AngelscriptRestoreTests.cpp` 这类直接依赖 engine 类型的测试中补显式断言，防止未来再把 Full owner 当成 clone fixture 使用。 4. 更新 `TESTING_GUIDE.md`，把 engine 入口 contract 写成表格化规则（`CreationMode` / `OwnsEngine` / `RequiresSource` / `ExpectedFailureWithoutSource`），并在文档中直接引用对应 validation test 名称，形成“文档声明必须有回归佐证”的约束。 5. 在 review/checklist 中增加一次简单静态扫描：新增使用 `ASTEST_CREATE_ENGINE_CLONE()` 或 clone helper 的测试时，必须伴随至少一条 contract assertion 或复用统一 fixture，避免业务断言再次替代基础设施 contract 断言。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptRestoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | S |
| 风险 | 这些新增断言会立即把当前已经漂移的调用点打红；但这正是该覆盖层应该承担的职责，风险主要是暴露现存问题，而不是引入新行为。 |
| 前置依赖 | 建议在 Issue-21 的 clone helper 语义修正后落地，否则新增 contract test 只会稳定复现当前错误行为。 |
| 验证方式 | 1. 运行新增的 `Validation` engine-mode 回归，确认 full/clone/source contract 均被显式断言。 2. 回归 `Shared.EngineHelper.*` 与 `Internals.Restore.*` 中使用 clone helper 的代表性测试，确认它们在业务断言前先验证 engine mode。 3. 再次搜索 `ASTEST_CREATE_ENGINE_CLONE()` / clone helper 的使用点，确认新增调用路径不再只做“非空 + 业务结果”式的间接验证。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-23 | Architecture | 在 Issue-21 修复后立刻补 contract coverage，防止同类语义漂移再次逃逸 |

---

## 发现与方案 (2026-04-08 13:51)

### Issue-24：preprocessed compile helper 忽略 `ModuleName`，导致带路径文件名的 annotated/reload 测试清理错模块

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 165-227, 275-280, 319-364; 86-99; 29-39, 56-57; 202-209, 217-232; 1026-1043 |
| 问题 | `CompileAnnotatedModuleFromMemory()`、`AnalyzeReloadFromMemory()` 以及 `CompileModuleWithResult()` 的 preprocessor 分支都把 `ModuleName` 参数丢掉，直接调用只接收 `Filename` 的 `PreprocessAndCompile()`。runtime preprocessor 又会用 `FilenameToModuleName(Filename)` 把文件名转换成真实模块名。结果是：一旦 `Filename` 含目录或绝对路径，真实模块名就不再等于调用方传入的 `ModuleName`。当前代码里已经出现两类现成错配。其一，`RunScriptExampleCompileTest()` 把 `ModuleName` 设为文件 basename，却把 `VirtualFileName` 设为 `ScriptExamples/<ExampleFileName>`，cleanup 仍执行 `Engine.DiscardModule(*ModuleName.ToString())`。其二，`RuntimeSourceMetadataBindingsTest` 直接把绝对 `ScriptPath` 传给 `CompileAnnotatedModuleFromMemory()`；脚本断言 `Type.GetScriptModuleName().Contains("RuntimeSourceMetadataBindingsTest")` 而不是等于该名字，已经在绕开 path-derived module name。与此同时，`FAngelscriptEngine::DiscardModule()` 在模块名不匹配时会直接返回 `false`。 |
| 根因 | helper API 试图用同一组 `ModuleName + Filename + Script` 签名覆盖 plain compile 与 preprocessed compile，但只有 plain 分支通过 `MakeModuleDesc()` 真的使用了 `ModuleName`；preprocessed 分支把“模块身份”完全交给了 `Filename`。 |
| 影响 | 任何使用带目录/绝对路径文件名的 annotated/full-reload helper，都会在 teardown、lookup、trace 与 diagnostics 上同时出现身份错位：`DiscardModule(RequestedModuleName)` 可能失败并遗留模块；`GetScriptModuleName()` 与调用方记录的模块名不一致；后续测试会继承错误模块名的残留状态，形成顺序相关失败。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 preprocessed compile 路径的模块身份改成“显式请求值优先”，并让 helper 把实际编译出的模块名返回给调用方，停止继续猜测。 |
| 具体步骤 | 1. 在 `Shared/AngelscriptTestEngineHelper.cpp` 为 preprocessed 路径新增显式模块名收敛逻辑，例如 `PreprocessAndCompileWithRequestedModuleName(FAngelscriptEngine*, ECompileType, FName RequestedModuleName, FString Filename, FString Script, ...)`：`Preprocessor.GetModulesToCompile()` 之后，把入口文件对应的 root `FAngelscriptModuleDesc->ModuleName` 强制改为 `RequestedModuleName.ToString()`，不要再让 root module name 仅由 `FilenameToModuleName()` 决定。 2. 同步更新 `CompileAnnotatedModuleFromMemory()`、`AnalyzeReloadFromMemory()`、`CompileModuleWithResult()` 的 annotated/full-reload 分支全部走这条新路径；`CompileModuleWithSummary()` 也要在 `bUsePreprocessor=true` 时复用同一规则，确保 `OutSummary.ModuleNames` 与调用方请求一致。 3. 对已有 path-based caller 做收口：`Examples/AngelscriptScriptExampleTestSupport.cpp` 和 `Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` 改为依赖 helper 返回的实际模块名或统一后的 requested module name 做 `DiscardModule()`；把 `Contains("RuntimeSourceMetadataBindingsTest")` 这类 path-workaround 断言改成精确等值断言。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 新增两条回归：一条用 `VirtualFileName = "ScriptExamples/Foo.as"` + `ModuleName = "Foo"` 编译 annotated 脚本，断言 `GetModuleByModuleName("Foo")` 可见且 `DiscardModule("Foo")` 返回 `true`；另一条用绝对路径文件名编译 annotated 脚本，断言 `GetScriptModuleName()` 精确等于请求的模块名，不再泄露绝对路径。 5. 若需要保留“由文件路径导出模块名”的能力，则新增独立 API（例如 `CompileAnnotatedModuleUsingFilenameIdentity()`），把当前行为从默认 helper 中拆出去，避免继续污染大多数测试路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果少数旧测试其实依赖 path-derived module name（例如把路径信息当成脚本模块名的一部分），收紧后会暴露这些隐式约定；需要在迁移时把这类需求挪到显式 filename-identity API，而不是留在默认 helper 中。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行新增的 module-identity 回归，确认虚拟路径和绝对路径两种 annotated compile 都能用请求的 `ModuleName` 成功查找与 `DiscardModule()`。 2. 回归 `Angelscript.TestModule.Bindings.SourceMetadata`、`Angelscript.TestModule.Editor.SourceNavigation.Functions`、以及任何调用 `RunScriptExampleCompileTest()` 的 example 编译测试，确认 teardown 后不会再残留 path-derived module。 3. 再次检查 `Type.GetScriptModuleName()` / compile summary `ModuleNames`，确认它们不再暴露 `ScriptExamples/...` 或绝对磁盘路径。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-24 | Defect | 先修 helper 的模块身份 contract，再继续处理依赖 path-based filename 的 annotated/trace 测试 |

---

## 发现与方案 (2026-04-08 13:54)

### Issue-25：preprocessed helper 只创建 `Saved/Automation` 根目录，嵌套虚拟文件名依赖外部残留目录才能成功写盘

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp` |
| 行号 | 54-58, 179-185; 56-57 |
| 问题 | 两条 preprocessed 写盘路径都只调用了 `MakeDirectory(*AutomationDirectory / *TempDir, true)`，但真正写入的 `AbsoluteFilename` 可能带子目录：`BuildModulesForSummary()` 用 `Saved/Automation/<Filename>`，`PreprocessAndCompile()` 也用 `Saved/Automation/<Filename>`。当 `Filename` 是 `ScriptExamples/Example_Actor.as` 这类嵌套相对路径时，helper 并不会创建 `Saved/Automation/ScriptExamples/`，却直接 `SaveStringToFile()` 到该路径。`RunScriptExampleCompileTest()` 当前正稳定传入 `VirtualFileName = "ScriptExamples/<ExampleFileName>"`。仓库内也没有其它地方创建 `Saved/Automation/ScriptExamples` 子目录。 |
| 根因 | helper 把“automation 根目录存在”误当成“目标文件父目录存在”，目录创建只停在一级，没有按最终 `AbsoluteFilename` 的父路径递归建目录。 |
| 影响 | example compile 支撑和任何未来传入嵌套虚拟文件名的 annotated/summary helper，都可能因为机器上是否残留目标子目录而出现顺序相关的成功/失败；一旦某轮清理掉 `Saved/Automation/ScriptExamples`，同一测试就会在写盘阶段提前失败。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 所有 preprocessed 写盘入口都必须按最终目标文件的父目录递归建目录，而不是只建 `Saved/Automation` 根目录。 |
| 具体步骤 | 1. 在 `Shared/AngelscriptTestEngineHelper.cpp` 中把 `BuildModulesForSummary()` 与 `PreprocessAndCompile()` 的目录创建改为 `IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsoluteFilename), true)`；不要继续只创建 `AutomationDirectory` / `TempDir`。 2. 在写盘失败分支补上下文更完整的错误信息，把 `AbsoluteFilename` 和父目录一起写入日志或 summary diagnostics，避免后续再把目录缺失误判成编译错误。 3. 为 `RunScriptExampleCompileTest()` 新增负向回归：测试开始前显式删除 `Saved/Automation/ScriptExamples` 目录，然后调用一个最小 example compile，用例应仍能成功写盘与编译。 4. 为 `CompileModuleWithSummary()` 新增嵌套文件名回归，使用 `Filename = "Nested/CompileSummaryAnnotated.as"` + `bUsePreprocessor=true`，断言 summary compile 成功且 `AbsoluteFilenames` 指向新建的嵌套路径。 5. 把这条目录创建规则写入 helper 注释或 `TESTING_GUIDE.md`，明确允许调用方传相对虚拟路径，但 helper 必须负责创建其父目录。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | S |
| 风险 | 如果有旧测试在断言写盘失败消息或依赖某个固定不存在目录的错误路径，修复后这些断言需要同步更新；但这类断言本身也在锁定错误行为。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行新增的 nested-path 写盘回归，确认预先删除 `Saved/Automation/ScriptExamples` 后 example compile 仍能通过。 2. 回归全部 `Angelscript.TestModule.ScriptExamples.*Compile` 用例，确认它们不再依赖本地残留目录。 3. 人工检查 `Saved/Automation` 下新建的嵌套父目录，确认 helper 只创建需要的目录且不会在写盘前失败。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-25 | Defect | 与 Issue-24 一起处理 preprocessed helper，先去掉 path-based 写盘的环境依赖 |

---

## 发现与方案 (2026-04-08 13:55)

### Issue-26：compile summary 与 learning trace 从不校验实际 `ModuleNames`，导致模块身份漂移长期逃逸

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningReloadAndClassAnalysisTests.cpp` |
| 行号 | 18-28; 680-744; 137-205, 219-240; 75-84; 75-84 |
| 问题 | `FAngelscriptCompileTraceSummary` 已经暴露了 `ModuleNames`，这本来就是 helper 对“实际编译模块身份”的唯一结构化输出。但当前自测 `CompileSummaryPlainModuleTest` / `CompileSummaryDiagnosticCaptureTest` 只检查 `bUsedPreprocessor`、`ModuleDescCount`、`CompiledModuleCount` 和 `Diagnostics.Num()`，完全不校验 `Summary.ModuleNames`。learning trace 也在重复这个盲区：`LearningCompilerTraceTests`、`LearningHotReloadDecisionTraceTests`、`LearningReloadAndClassAnalysisTests` 都只把 scenario/requested `ModuleName` 和 `Filename` 写进 trace，而不是记录 helper 实际返回的 `ModuleNames`。结果是，只要 compile helper 把模块名从 `RequestedModuleName` 漂移到 `FilenameToModuleName(Filename)`，现有 trace/self-test 仍然会全部通过，并把错误的“请求值”写成事实。 |
| 根因 | TestInfrastructure 在 compile-trace 层只把 helper 当作“成功/失败 + diagnostics”采样器，没有把“实际编译出的模块身份”视为需要验证和记录的一等 contract。 |
| 影响 | Module identity 相关的回归可以长期潜伏而不被 `Shared.EngineHelper.*` 或 `Learning.*` 覆盖层发现；trace 文档会向后续排障者输出错误模块名，进一步放大 cleanup、lookup、source-navigation 类问题的定位成本。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“请求模块名”和“实际编译模块名”明确区分，并在 self-test 与 learning trace 中都强制校验/记录两者。 |
| 具体步骤 | 1. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 扩展 `CompileSummaryPlainModuleTest` 与 `CompileSummaryDiagnosticCaptureTest`：增加 `TestEqual` / `TestTrue`，显式断言 `Summary.ModuleNames` 至少包含预期 root module name；同时新增一个 nested-path preprocessor summary 回归，验证 `ModuleNames` 不会被路径意外改写。 2. 更新 `LearningCompilerTraceTests.cpp`，在 `TraceCompilerScenario()` 或调用点中同时记录 `RequestedModuleName` 与 `ResolvedModuleNames = FString::Join(OutSummary.ModuleNames, ",")`，避免 trace 继续把请求值当成真实编译结果。 3. 对 `LearningHotReloadDecisionTraceTests.cpp` 和 `LearningReloadAndClassAnalysisTests.cpp` 增加轻量 summary/identity 采样：在 baseline compile 或 analyze 后读取实际模块名，并把 requested/resolved 两组值同时输出到 trace。 4. 在 compile helper 文档或注释里补 contract：`ModuleNames` 是诊断和 cleanup 必须消费的事实来源；`ModuleName` 参数只是请求值，不应再被 trace/test 单独当成结果。 5. 将新增 identity 断言纳入 review checklist：任何新增 `CompileModuleWithSummary()`/learning trace 场景，必须至少断言一次 `ResolvedModuleNames`，否则视为 coverage 缺口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningReloadAndClassAnalysisTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h` |
| 预估工作量 | S |
| 风险 | 新增 identity 断言会让部分 trace/self-test 立刻暴露当前 helper 的错位行为；但这属于暴露既有缺陷，不是新增不兼容设计。 |
| 前置依赖 | 建议与 Issue-24 同步推进，否则 identity coverage 只会稳定复现当前错误 contract。 |
| 验证方式 | 1. 运行扩展后的 `Shared.EngineHelper.CompileSummary*` 回归，确认 `Summary.ModuleNames` 被显式断言。 2. 检查 `Learning.*` trace 输出，确认同时包含 `RequestedModuleName` 与 `ResolvedModuleNames`，且两者在 path-based 场景下不会再被混淆。 3. 人工制造一个 path-based annotated compile 场景，确认 trace/self-test 会在模块身份漂移时直接失败，而不是静默通过。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-26 | Architecture | 在 Issue-24 修 helper contract 后立刻补 module identity coverage，防止同类问题再次逃逸 |

---

## 发现与方案 (2026-04-08 14:06)

### Issue-27：`Engine.CreateDestroy` 核心烟雾测试会在 ambient engine 存在时退化为 clone 路径

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 58-72; 179-199; 654-666 |
| 问题 | `FAngelscriptTestModuleLifecycleTest` 先调用 `DestroySharedTestEngine()`，随后直接使用默认模式的 `FAngelscriptEngine::CreateForTesting(Config, Dependencies)`。runtime 实现里这条 API 只要发现 `TryGetCurrentEngine()` 非空就会 `CreateCloneFrom(*CurrentEngine, ...)`，否则才 fallback 到 `CreateTestingFullEngine()`。而 `GetOrCreateSharedCloneEngine()` 又会建立一个进程级 persistent `FAngelscriptEngineScope`，把 shared engine 留在 context stack 里。结果是：只要前序测试留下任何 ambient current engine，这条名为 `Engine.CreateDestroy` 的核心生命周期测试就会从“创建独立测试引擎”退化成“克隆现有 engine 然后析构”，且当前断言只检查“非空 + 指针 reset”，完全看不出模式漂移。 |
| 根因 | 核心生命周期测试使用了依赖 ambient context 的 convenience API，却没有先建立显式隔离基线，也没有断言 `CreationMode` / `OwnsEngine` / `SourceEngine` 等真实契约。 |
| 影响 | 这条测试会随执行顺序在 Full 与 Clone 之间漂移，导致真正的 full-owner 创建/销毁回归可能被假阳性掩盖；同时测试标题与实际覆盖语义不一致，会误导后续维护者继续复用错误入口。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `Engine.CreateDestroy` 改成显式的 full-owner 生命周期测试，并把 clone 创建验证拆成独立用例。 |
| 具体步骤 | 1. 在 `Core/AngelscriptEngineCoreTests.cpp` 中将 `FAngelscriptTestModuleLifecycleTest` 改为使用显式 owner 入口：优先复用 Issue-1 计划中的 full-epoch isolation fixture，或至少调用 `CreateTestingFullEngine()` / `CreateForTesting(..., EAngelscriptEngineCreationMode::Full)`，不要再走默认 `CreateForTesting()`。 2. 在该测试内补充契约断言：`GetCreationMode()==Full`、`OwnsEngine()==true`、`GetSourceEngine()==nullptr`，并在销毁后补一条状态断言，确认如果它是最后一个 full owner，相关全局状态确实回收。 3. 新增独立 clone 生命周期测试，例如 `Engine.CreateCloneFromScopedSource`：先显式创建 source full engine，再调用 clone 入口，断言 `Clone + OwnsEngine()==false + GetSourceEngine()==Source`，把 clone 覆盖从 `CreateDestroy` 中拆出去。 4. 在 helper 自测中增加顺序相关回归：先建立 shared/current engine，再运行新的 `Engine.CreateDestroy` 测试逻辑，断言得到的仍是独立 full owner，而不是 ambient engine 的 clone。 5. 更新 `TESTING_GUIDE.md` 或 core 测试注释，明确“默认 `CreateForTesting()` 不是隔离生命周期 smoke test 入口”，防止后续再把 convenience API 用到 lifecycle 断言里。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | S |
| 风险 | 如果现有部分调用点其实依赖 `CreateForTesting()` 的 clone fallback，收紧后会立即暴露这些隐式假设；但这正是把 lifecycle 测试从 ambient-context 假阳性中剥离出来所必须承担的改动。 |
| 前置依赖 | 建议与 Issue-21/Issue-23 一起收口 engine mode contract，避免新断言继续接受伪 clone 行为。 |
| 验证方式 | 1. 新增/更新 `Angelscript.TestModule.Engine.CreateDestroy`，显式断言 Full mode contract。 2. 增加一条“outer shared engine 已存在”负向回归，确认 `CreateDestroy` 仍创建独立 full owner。 3. 单独运行新的 clone 生命周期测试，确认 clone 语义由独立用例覆盖且不再混入 `CreateDestroy`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-27 | Defect | 在 engine mode contract 收紧时一并修复，先去掉 core lifecycle smoke test 的假阳性来源 |

---

## 发现与方案 (2026-04-08 14:08)

### Issue-28：两个 `FullDestroyAllows*Recreate` 用例都没有验证“同名符号跨 epoch 重建”

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 行号 | 210-260; 282-351 |
| 问题 | `FAngelscriptFullDestroyAllowsCleanRecreateTest` 的第一轮 full engine 只检查 `FAngelscriptType::GetTypes().Num() > 0`，直到第二轮 engine 才第一次编译 `RecreateCoreSnippet` 并执行 `int Entry()`，也就是说它从未验证“第一轮已经存在的模块能否在 full destroy 后用同名重新创建”。`FAngelscriptFullDestroyAllowsAnnotatedRecreateTest` 更明显：第一轮编译的是 `RecreateAnnotatedActorA` / `ARecreateAnnotatedActorA`，第二轮改成 `RecreateAnnotatedActorB` / `ARecreateAnnotatedActorB`。测试标题写的是 `Allows...Recreate`，但实际覆盖的只是“第二个 epoch 能再编译一个新名字的脚本类型”，而不是最容易残留的同名 module / class / generated function 复建路径。 |
| 根因 | 当前用例把“full destroy 后还能继续编译任何脚本”误当成了“full destroy 后可重新创建同名脚本资产”，为了规避名字冲突反而避开了真正需要验证的冲突点。 |
| 影响 | 如果 full destroy 后仍残留旧 module identity、旧 `UClass` 路径或 generated function shell，这两条核心回归仍会通过，因为第二轮根本没有复用同名符号；真正的 stale-symbol / stale-package / stale-reflection 回归会长期逃逸。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把两个 recreate 用例改成“同一模块名、同一脚本类型名跨两个 full epoch 重新创建”，并在第二轮编译前显式断言旧符号已经清空。 |
| 具体步骤 | 1. 修改 `FAngelscriptFullDestroyAllowsCleanRecreateTest`：在第一轮 engine 中先编译并执行 `RecreateCoreSnippet`，记录结果后销毁 full owner；第二轮再用相同 `ModuleName` 和相同 `int Entry()` 重新编译并执行，确认不是“第一次创建”而是真正的 recreate。 2. 修改 `FAngelscriptFullDestroyAllowsAnnotatedRecreateTest`：两轮都使用同一个 `ModuleName`（例如 `RecreateAnnotatedActor`）和同一个类名（例如 `ARecreateAnnotatedActor`），第一轮销毁前显式 `DiscardModule()` + `CollectGarbage()`，第二轮编译前先断言 `FindGeneratedClass()` 已返回空，随后再编译同名类型并验证能重新解析。 3. 把 inline script 构造提取为小型 fixture/helper（例如 `CompileAnnotatedActorByName(Engine, ModuleName, ClassName, ValueLiteral)`），避免继续在测试体里复制 `A/B` 两套几乎相同的脚本文本。 4. 在第二轮回归中额外校验 reflection identity：重新获得的 `UClass` 路径应与旧名字一致，但对象实例/地址必须来自新的 epoch，防止“旧壳对象复用”被误判为成功。 5. 将 `LastFullDestroyClearsTypeState`、`FullDestroyAllowsCleanRecreate`、`FullDestroyAllowsAnnotatedRecreate` 三条测试收敛为统一 full-epoch fixture 下的 staged cases，减少后续再把“继续能编译新名字”误写成“recreate”测试的概率。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦改成同名复建，当前 runtime/test helper 里残留的 module identity 或 generated UObject 清理问题会被立刻打红；但这正是这两条“recreate”回归应当承担的职责。 |
| 前置依赖 | 建议与 Issue-1、Issue-2、Issue-24 同步推进，先把 full-destroy context cleanup、generated function cleanup 和 module identity contract 收紧，再用同名复建回归锁定行为。 |
| 验证方式 | 1. 更新后单独运行 `Angelscript.TestModule.Engine.FullDestroyAllowsCleanRecreate` 与 `Angelscript.TestModule.Engine.FullDestroyAllowsAnnotatedRecreate`，确认两轮都使用相同 module/class 名称。 2. 在 annotated 用例中增加断言：第二轮编译前 `FindGeneratedClass("ARecreateAnnotatedActor") == nullptr`，编译后再次可见。 3. 将这两条用例与 `ResetSharedEngineReleasesGeneratedFunctions`/`ResetSharedEngineReleasesGeneratedComponentClasses` 串行运行，确认顺序互换后同名复建结果一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-28 | Defect | 在 full-destroy cleanup contract 收紧后立即补强，避免 “recreate” 回归继续漏测同名符号残留 |

---

## 发现与方案 (2026-04-08 14:08)

### Issue-29：`ExecuteGeneratedIntEventOnGameThread()` 在非 game thread 路径上会无限等待，单次卡住即可挂死整轮 automation

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp` |
| 行号 | 434-485; 142, 301, 331; 226, 364 |
| 问题 | `ExecuteGeneratedIntEventOnGameThread()` 在 `!IsInGameThread()` 时会从池中取一个 `FEvent`，把 lambda 投递到 `ENamedThreads::GameThread`，随后直接执行 `CompletedEvent->Wait()`，没有任何超时、取消或失败出口。只要 game thread 因 hot reload、world teardown、latents 或退出流程没有及时执行这段 lambda，调用方就会永远卡在等待上。我的本轮扫描里，这个 helper 在 `AngelscriptTest` 模块中有 20 个调用点，代表性调用集中在 hot reload 和 coverage 路径，而这些调用点都把它直接包在 `TestTrue(...)` 里，预期是“失败一个断言”，实际却可能把整轮 automation 挂死。 |
| 根因 | helper 把“跨线程切回 game thread 执行脚本反射调用”实现成了无条件同步桥接，假设投递一定会完成，却没有把调度失败、game-thread 停摆或测试退出视为需要处理的状态。 |
| 影响 | 一旦这条路径卡住，后续 cleanup、GC、module discard 和其它测试都不会执行，整轮发现结果会停在一条挂起用例上；由于 helper 没有 timeout diagnostics，排障者也拿不到“卡在哪个对象/函数/测试场景”的直接证据。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 reflected-call helper 从“无限等待的同步桥”改成“有超时、有失败原因的受控调度”，让卡住的调用降级为可诊断的测试失败。 |
| 具体步骤 | 1. 在 `Shared/AngelscriptTestEngineHelper.cpp` 中为 `ExecuteGeneratedIntEventOnGameThread()` 引入受控等待：使用固定 deadline（例如 5s，可通过测试常量集中管理）轮询 `CompletedEvent->Wait(TimeoutSliceMs)`，同时检查 `IsEngineExitRequested()` / automation 终止条件；超时后返回 `false` 并把 `Object`、`Function`、当前线程信息写入日志。 2. 用 `ON_SCOPE_EXIT` 包住 pooled event 的归还，确保无论成功、超时还是早退都能 `ReturnSynchEventToPool()`，避免把 event 池状态也拖脏。 3. 将异步 lambda 中捕获的 `Object` / `Function` 改成 `TWeakObjectPtr` 或等价的可校验句柄，game thread 执行前先验证对象仍然有效；无效时直接记录失败原因并触发 event，而不是继续解引用悬空指针。 4. 为 helper 增加 automation-only dispatch seam（例如可注入的 `DispatchToGameThreadForTesting`），在 `Shared/AngelscriptTestEngineHelperTests.cpp` 新增一条负向回归：注入“永不执行回调”的 dispatcher，断言 helper 在超时后返回 `false` 且给出明确 diagnostics，而不是永久挂起。 5. 更新现有调用点的断言消息模板，让它们在 helper 返回 `false` 时能够附带 timeout / invalid-object 原因，避免 20 个调用点继续只输出泛化的“should execute on the game thread”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp` |
| 预估工作量 | M |
| 风险 | 超时值如果设置过短，重负载机器上可能把慢但正确的 reflected call 误判为失败；需要先以代表性 hot reload/coverage 场景测一轮，选一个只拦截“卡死”而不拦截“正常慢”的保守阈值。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 helper 负向回归，注入不执行的 dispatcher，确认 helper 在 deadline 后返回 `false` 而不是挂起。 2. 回归至少一条 hot reload 调用点和一条 coverage 调用点，确认正常路径仍返回成功且结果值正确。 3. 从非 game thread 主动触发一次 reflected call，验证日志里能看到 object/function/timeout diagnostics，且测试进程不会被无限阻塞。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-29 | Defect | 与 Issue-17 一起处理 reflected-call helper，先把“挂死整轮测试”降级成可诊断失败 |

---

## 发现与方案 (2026-04-08 16:37)

### Issue-30：`Engine.CompileSnippet` / `Engine.ExecuteSnippet` 核心烟雾测试建立在 shared 缓存引擎上，无法验证冷启动 engine 路径

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 行号 | 75-153; 42-46, 97-104; 179-200; 13-18, 41-64 |
| 问题 | `FAngelscriptTestModuleCompileSnippetTest` 与 `FAngelscriptTestModuleExecuteSnippetTest` 这两条 `Angelscript.TestModule.Engine.*` 核心烟雾测试，都通过 `ASTEST_CREATE_ENGINE_SHARE()` + `ASTEST_BEGIN_SHARE` 进入 shared engine 路径。宏定义和 `TESTING_GUIDE.md` 又明确把 `SHARE` 标成“lightweight compile-and-execute, no isolation needed”，而把 `FULL` 标成“engine core self-tests”。helper 实现里，`ASTEST_CREATE_ENGINE_SHARE()` 最终落到 `GetOrCreateSharedCloneEngine()`，返回的是进程级缓存 engine，并维持一条 persistent `FAngelscriptEngineScope`。这意味着这两条 core smoke test 实际并不是在 fresh engine 上验证“创建后即可编译/执行”，而是在一个可能已被前序测试预热、复用、污染过的 shared epoch 上运行。 |
| 根因 | core 测试名称表达的是 engine 基础能力 smoke test，但实现直接复用了共享缓存 helper，没有把“验证 engine 冷启动 contract”和“验证 shared compile/execute convenience path”拆成两类独立测试。 |
| 影响 | 即使 fresh engine 的初始化、bind replay、首轮 context 建立或 teardown contract 已经回归，这两条用例仍可能因为 shared engine 之前已被创建并保持 current scope 而继续通过；同时它们把 order-dependent 共享状态引入了 `Engine.*` 基线，削弱了该测试组作为基础设施守门用例的价值。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 engine-core smoke test 与 shared-engine convenience test 拆开：前者强制走 fresh owner engine，后者留给 helper/shared 测试覆盖。 |
| 具体步骤 | 1. 将 `AngelscriptEngineCoreTests.cpp` 中的 `Engine.CompileSnippet` 与 `Engine.ExecuteSnippet` 改为显式 fresh owner 路径，优先使用 `CreateFullTestEngine()` / `ASTEST_CREATE_ENGINE_FULL()`，不要再从 `ASTEST_CREATE_ENGINE_SHARE()` 取缓存 engine。 2. 在这两条 core 测试里补充 contract 断言：`GetCreationMode()==Full`、`OwnsEngine()==true`、`GetSourceEngine()==nullptr`，把“真的在独立 engine 上运行”固定成测试前提。 3. 保留 shared compile/execute 行为，但迁移到 `Shared/AngelscriptTestEngineHelperTests.cpp` 或新的 shared smoke 测试里，明确命名成 `Shared.Engine...`，不要继续挂在 `Engine.*` 核心分组下。 4. 将 raw snippet 编译部分与 Issue-15 的 raw-module RAII 合并治理，避免 core 测试在迁移 engine 模式时继续手写 `GetModule(..., asGM_ALWAYS_CREATE)` + 手工 `Release()`。 5. 更新 `TESTING_GUIDE.md` 和 `README_MACROS.md` 的示例，把 `Engine.*` / core smoke test 统一指向 `FULL` 或 direct owner helper，shared 宏只留给明确声明“no isolation needed”的轻量路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md` |
| 预估工作量 | M |
| 风险 | fresh full engine 的创建成本高于 shared 路径，迁移后这两条 smoke test 的运行时间会增加；需要把 shared-path 覆盖单独保留，避免为了隔离把“缓存 engine 行为”完全测丢。 |
| 前置依赖 | 建议与 Issue-15 一起实施，统一收掉 raw snippet module 的 cleanup。 |
| 验证方式 | 1. 在 shared engine 已存在和不存在两种前置状态下分别运行 `Angelscript.TestModule.Engine.CompileSnippet`、`Angelscript.TestModule.Engine.ExecuteSnippet`，结果应保持一致。 2. 新增一条顺序回归：先创建并污染 shared engine，再运行新的 core smoke test，断言其 `CreationMode` 仍为 `Full` 且不复用 shared instance。 3. 回归 `Shared.EngineHelper.ExecuteIntFunction` 或新的 shared smoke 测试，确认 shared convenience path 仍有独立覆盖。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-30 | Architecture | 在 raw-module cleanup 收口时同步调整，把 core smoke test 从 shared helper 中拆出 |

---

## 发现与方案 (2026-04-08 16:39)

### Issue-31：`CompileModuleWithResult()` 用脚本文本启发式决定编译管线，hot-reload / subsystem / interface 测试无法显式声明 helper 契约

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 行号 | 275-283; 32-48; 105-106; 77-84; 327-390 |
| 问题 | `CompileModuleWithResult()` 并不让调用方显式指定“plain compile 还是 preprocessed/annotated compile”，而是直接根据 `Script.Contains("UCLASS(")` / `USTRUCT(` / `UENUM(` 的字符串命中来决定走 `PreprocessAndCompile()` 还是 `CompileModuleInternal()`。与此同时，这个统一入口已经被 native hot-reload、普通 hot-reload、subsystem scenario、interface hot-reload 等多类测试广泛复用。`InterfaceAdvancedTests.cpp` 的 hot-reload 脚本还明确包含 `UINTERFACE()`，但 helper 的判定条件里完全没有 `UINTERFACE`。也就是说，调用点源码本身并不能说明自己到底在走哪条编译管线；真正的 helper 行为要等脚本文本内容解析后才能知道。 |
| 根因 | helper 把“脚本是否需要 preprocessor / generated-symbol compile path”的决定隐藏在一个内容启发式里，没有把它建模成显式 API 契约；测试作者传了 `CompileType`，却没有机会声明“输入属于 plain 还是 annotated/preprocessed”。 |
| 影响 | hot-reload 与 scenario 测试本来就在故意修改脚本内容；一旦某次脚本演化增删了 `UCLASS/UENUM/USTRUCT`，同一个测试会在不改调用点的情况下静默切换 helper 基础设施路径，导致“被测行为变化”和“测试工具路径变化”混在一起。推断：若后续出现仅包含 `UINTERFACE()` 的脚本并继续走该 wrapper，它会被误判为 plain compile，进一步放大这类隐性漂移。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 取消内容猜测，把 compile-pipeline 选择提升为显式 API：调用方必须声明自己要测 plain 还是 preprocessed/annotated 输入。 |
| 具体步骤 | 1. 将 `CompileModuleWithResult()` 拆成显式入口，例如 `CompilePlainModuleWithResult()` 与 `CompilePreprocessedModuleWithResult()`，或新增 `EScriptCompileInputKind { Plain, Preprocessed }` 参数；不要再在 helper 内根据 `Script.Contains(...)` 猜路径。 2. 让当前的 `CompileModuleWithResult()` 退化为兼容层：短期内保留，但内部改成 `checkf/ensureMsgf` 警告并调用显式入口，便于逐步迁移而不是继续扩散。 3. 先迁移高风险调用面：`AngelscriptNativeScriptHotReloadTests.cpp`、`HotReload/AngelscriptHotReload*Tests.cpp`、`Subsystem/AngelscriptSubsystemScenarioTests.cpp`、`Interface/AngelscriptInterfaceAdvancedTests.cpp`，让每条测试在调用点就能看出自己测的是 plain 还是 preprocessed contract。 4. 为 interface/annotation 变体补 helper 自测：至少覆盖 `UINTERFACE()` 输入、`UCLASS()` 输入，以及“同一 hot-reload 测试的 V1/V2 输入 annotation 形态不同但 helper 入口保持不变”的场景，保证测试作者改的是脚本，不是暗中改 helper 路径。 5. 文档中把 `CompileType` 与 `InputKind` 分开说明：`CompileType` 决定 soft/full reload 语义，`InputKind` 决定 plain/preprocessed 管线，防止未来继续把两者混成一个维度。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 迁移显式入口时，部分现有测试会暴露出自己其实一直跑在与作者预期不同的 helper 路径上，短期内可能带来一批“测试需要重新选择 plain/preprocessed 入口”的修正工作。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 在 helper 自测中新增 `InputKind` 回归：`UINTERFACE`、`UCLASS`、plain script 三类输入都命中预期编译入口。 2. 回归一条 native hot-reload、一条 interface hot-reload 和一条 subsystem scenario 测试，确认迁移后 `CompileType` 结果不变，但 helper 路径由调用点显式决定。 3. 对旧兼容层做一次仓库扫描，确认高风险目录不再直接调用启发式版本的 `CompileModuleWithResult()`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-31 | Architecture | 在 preprocessed-path 模块名修复之后紧接处理，先把 helper 的编译入口契约显式化 |

---

## 发现与方案 (2026-04-08 16:41)

### Issue-32：shared 测试里对同一 engine 重复嵌套 `FAngelscriptEngineScope`，把 current-engine 栈深度人为放大

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | 79-84, 111-116; 110-114; 47-50; 168-171; 99-104; 179-200 |
| 问题 | `ASTEST_CREATE_ENGINE_SHARE()` 取得 shared engine 时，`GetOrCreateSharedCloneEngine()` 已经为该实例建立 persistent `FAngelscriptEngineScope`。随后 `ASTEST_BEGIN_SHARE` 又会再压入一层 `_AutoEngineScope`。但当前仓库里至少还有 4 处 shared 测试在宏作用域内部再手写一层同引擎 `FAngelscriptEngineScope`：`Engine.CompileSnippet`、`Engine.ExecuteSnippet`、`Misc.DuplicateFunction`、`Operators.GetSet`、`Type.FloatDebuggerFormatting`。结果是同一个 shared engine 在这些测试体内会同时以 persistent scope、宏 scope、手写 scope 的形式重复压栈。 |
| 根因 | 宏文档只说明 `ASTEST_BEGIN_SHARE` 会建立 engine scope，但没有把“在宏体内不要再对同一 engine 手写 `FAngelscriptEngineScope`”收敛成明确约束；已有测试又沿用了宏迁移前的老写法。 |
| 影响 | current-engine 栈深度不再只由 fixture 生命周期决定，而是被单个测试体内部实现细节放大；凡是依赖 `TryGetCurrentEngine()`、`IsInitialized()`、context snapshot/restore 或 shared/global cleanup 的 helper，都可能因为多余层级而观察到与作者预期不同的 ambient 状态，增加顺序相关和排障复杂度。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 统一约束“宏负责同引擎 scope，测试体只在需要不同 world context 时额外建 scope”，并清理现有重复压栈点。 |
| 具体步骤 | 1. 逐个删除已验证的重复 scope：`AngelscriptEngineCoreTests.cpp`、`AngelscriptMiscTests.cpp`、`AngelscriptOperatorTests.cpp`、`AngelscriptTypeTests.cpp` 中那些位于 `ASTEST_BEGIN_SHARE` 内、且没有额外 `WorldContextObject` 参数的 `FAngelscriptEngineScope`。 2. 如果某条测试确实需要临时切换 world context，就改成显式的 `FAngelscriptEngineScope(Engine, SomeObject)` 或单独的 `FScopedTestWorldContextScope`，不要再无差别复用 `FAngelscriptEngineScope(Engine)`。 3. 在 `TESTING_GUIDE.md` / `README_MACROS.md` 加一条硬规则：`ASTEST_BEGIN_*` 已经建立同引擎 scope，除非附带不同 world context，否则测试体内禁止再次对同一 engine 调用 `FAngelscriptEngineScope(Engine)`。 4. 在 `Validation/AngelscriptMacroValidationTests.cpp` 增加回归，记录 `ASTEST_BEGIN_SHARE` 进入前后的 context stack 深度；再构造一个带 world context 的显式 scope 用例，验证只有“不同上下文”场景才允许额外层级。 5. 对依赖 shared/current 语义的 helper 测试做一次 focused 回归，确认去掉重复 scope 后 `TryGetCurrentEngine()`、snapshot/restore 和 shared cleanup 的行为更稳定，而不是继续依赖多层同引擎压栈。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptOperatorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md` |
| 预估工作量 | S |
| 风险 | 少数测试如果曾经无意依赖“多压一层同引擎 scope”来延迟 current-engine 恢复，去掉后会暴露出真实的 helper 假设错误；这类测试需要改成显式 fixture，而不是保留重复 scope。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 回归 `Angelscript.TestModule.Engine.CompileSnippet`、`Angelscript.TestModule.Engine.ExecuteSnippet`、`Angelscript.TestModule.Angelscript.Misc.DuplicateFunction`、`Angelscript.TestModule.Angelscript.Operators.GetSet`、`Angelscript.TestModule.Angelscript.Type.FloatDebuggerFormatting`。 2. 新增 macro validation 回归，验证 `ASTEST_BEGIN_SHARE` 自身只增加一层 engine scope。 3. 结合 shared helper 测试重跑 `CompileRestoresOuterCurrentEngine` 与 `NestedCurrentEngineScopeRestoresPreviousEngine`，确认去掉重复 scope 后 current-engine 恢复次序仍正确。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-32 | Defect | 在 shared/current helper 收口时顺手清理，先把重复压栈点从测试体里移除 |

---

## 发现与方案 (2026-04-08 16:50)

### Issue-33：场景 helper 的 ambient-engine 重载在缺少 `FAngelscriptEngineScope` 时直接 `checkf`，会把单条测试失败放大成整轮 automation 崩溃

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h`, `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorInteractionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp` |
| 行号 | 15-18, 75-77, 90-92; 305-307; 296-306 |
| 问题 | `AngelscriptScenarioTestUtils::RequireCurrentEngine()` 不是返回可诊断失败，而是对 `FAngelscriptEngine::TryGetCurrentEngine()` 直接执行 `checkf(CurrentEngine != nullptr)`。随后 `TickWorld(UWorld&, ...)` 与 `BeginPlayActor(AActor&)` 两个 ambient-engine 重载都无条件调用它。这意味着一旦调用点缺少外层 `FAngelscriptEngineScope`，或者前序测试把 current-engine 栈弄乱，场景 helper 不会给当前测试 `AddError()`，而是直接触发 fatal assert。当前模块内已经有多处真实调用依赖这种 ambient overload，例如 `AngelscriptActorInteractionTests.cpp:305-307` 与 `AngelscriptInterfaceAdvancedTests.cpp:296-306`。 |
| 根因 | 场景 helper 把“当前引擎必须已由外部建立”编码成了 runtime assert，而不是测试基础设施应有的显式前置条件和可恢复错误返回。 |
| 影响 | 只要出现一次状态泄漏、宏作用域漏配对或世界/引擎上下文错位，本应失败一条 case 的问题就会升级成整轮 automation 中断；同时崩溃栈只会停在 `checkf`，无法直接告诉维护者是哪条测试忘了建 scope、哪个 helper 重置了 current engine。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 取消场景 helper 的 fatal ambient-engine 断言，改成显式解析 current engine 的可诊断测试 API，并优先推动调用点传入显式 `Engine`。 |
| 具体步骤 | 1. 在 `Shared/AngelscriptScenarioTestUtils.h` 中把 `RequireCurrentEngine()` 改成测试友好的解析入口，例如 `TryRequireCurrentEngine(FAutomationTestBase&, const TCHAR* Context, FAngelscriptEngine*& OutEngine)`；当 `TryGetCurrentEngine()==nullptr` 时，通过 `AddError()` 报告缺少 `FAngelscriptEngineScope`，返回 `false`，不要再 `checkf`。 2. 将 `TickWorld(UWorld&, ...)` 与 `BeginPlayActor(AActor&)` 改成返回 `bool` 或 `FAngelscriptEngine*` 解析结果驱动的薄包装；调用失败时立即把错误回传给当前测试，而不是崩溃进程。 3. 优先迁移当前最脆弱的 ambient 调用点：`Actor/AngelscriptActorInteractionTests.cpp`、`Interface/AngelscriptInterfaceAdvancedTests.cpp`，把 `BeginPlayActor(*Actor)` / `TickWorld(World, ...)` 改成显式传入 `Engine` 的重载，减少对 ambient current engine 的隐藏依赖。 4. 在 `Shared` 层新增负向回归：故意在无 `FAngelscriptEngineScope` 的情况下调用 ambient overload，断言 helper 返回失败并输出包含测试名/操作名的 diagnostics，而不是触发 assert。 5. 在 `TESTING_GUIDE.md` 增加硬规则：scenario helper 默认应接收显式 `Engine`；ambient overload 仅作为兼容层存在，并且只能以“可失败、不崩溃”的方式解析 current engine。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Actor/AngelscriptActorInteractionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceAdvancedTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 把 assert 改成测试失败后，一些当前被 fatal 中断掩盖的调用点会开始稳定报红；但这正是把“基础设施崩溃”降级为“可定位的用例失败”所需要暴露出的真实问题。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 helper 负向回归，在没有 current engine 的情况下调用 ambient `TickWorld`/`BeginPlayActor`，确认只产生测试失败与明确 diagnostics。 2. 回归 `Angelscript.TestModule.Actor.Interaction.*` 与 `Angelscript.TestModule.Interface.Advanced.*` 的代表性场景，确认显式 `Engine` 版 helper 运行结果不变。 3. 串行在一条故意破坏 current-engine scope 的测试后再运行场景 helper 回归，确认进程不会因 `checkf` 中断。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-33 | Defect | 先把 fatal assert 降级成可诊断失败，再继续处理场景 helper 的 context 泄漏与 cleanup 问题 |

---

## 发现与方案 (2026-04-08 16:53)

### Issue-34：world-progression helper 在 `World.Tick()` 期间没有安装 `WorldContextObject`，世界相关绑定会读取到空或陈旧上下文

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h`, `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_WorldTick.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_SystemTimers.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp` |
| 行号 | 46-52; 18-23; 113-114; 17-46; 69-94 |
| 问题 | `AngelscriptScenarioTestUtils::TickWorld()` 在推进世界时只创建了 `FAngelscriptEngineScope WorldScope(Engine)`，没有把 `UWorld` 本身作为 `WorldContextObject` 传入；`Template/Template_WorldTick.cpp` 里的示例 helper 更是直接裸调 `World.Tick(...)`。与此同时，runtime 多个世界相关绑定明确通过 `FAngelscriptEngine::TryGetCurrentWorldContextObject()` 解析世界，例如 timer handle API（`Bind_SystemTimers.cpp:17-46`）和 `UGameInstanceSubsystem` / `UWorldSubsystem` 获取（`Bind_Subsystems.cpp:69-94`）。这意味着凡是在 `World.Tick()` 推进期间执行、且依赖 world context 的脚本逻辑，都可能看到空上下文或前一个 actor/component scope 残留的旧上下文。当前学习测试已经把 `TickWorld(Engine, Spawner.GetWorld(), ...)` 用在 timer 场景上（`LearningTimerAndLatentTraceTests.cpp:113-114`），说明这个 helper 正处在 world-sensitive 路径的主干上。 |
| 根因 | 测试基础设施把“推进世界”实现成了“只有 current engine、没有当前 world context 的 tick”，并且模板代码继续强化了这个错误模式。这个问题独立于 Issue-13 的双 tick：即使删掉手工 actor/component 重放，`World.Tick()` 本身仍然缺少正确的 world context。 |
| 影响 | world-driven 场景测试对 ambient world context 的正确性没有基线保证；一旦脚本在 tick 期间调用 timer/subsystem/world API，结果就可能依赖前一个 scope 恰好留下了什么对象。推断：timer、latent、subsystem、world lookup 相关测试会因此出现“同一脚本在单独运行与串行运行时结果不同”的顺序敏感现象。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 统一把 world progression helper 改成显式 world-scoped 执行，并清理模板中继续传播错误模式的本地实现。 |
| 具体步骤 | 1. 在 `Shared/AngelscriptScenarioTestUtils.h` 中把 `TickWorld(FAngelscriptEngine&, UWorld&, ...)` 的 world-scope 改成 `FAngelscriptEngineScope WorldScope(Engine, &World)` 或等价的 world-context 入口，确保 `World.Tick()` 期间 `TryGetCurrentWorldContextObject()` 精确指向当前 `UWorld`。 2. 同步更新 `Template/Template_WorldTick.cpp`：删除本地 `TickWorld(UWorld&, ...)` 实现，改为复用 shared helper 或至少在模板中建立显式 `FAngelscriptEngineScope(Engine, &World)`，避免继续向新测试传播“裸调 `World.Tick`”示例。 3. 在 `LearningTimerAndLatentTraceTests.cpp`、`ComponentHierarchyTraceTests.cpp` 等 world-driven 场景上补断言：world tick 期间调用 timer/subsystem/world API 时，应解析到 `Spawner.GetWorld()` 对应的上下文，而不是 `nullptr` 或前一 actor/component。 4. 新增 helper 回归：在一个 tick 驱动的脚本中同时调用 `UGameInstanceSubsystem::Get()`、`UWorldSubsystem::Get()` 或 `World` 绑定，断言它们在 `TickWorld()` 内能稳定返回当前测试世界的对象。 5. 将这条规则写入 `TESTING_GUIDE.md`：凡是“推进世界”的 helper，必须同时安装 current engine 和 current world context；actor/component scope 只能覆盖对象级回调，不能替代 world tick 自身的上下文。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h`, `Plugins/Angelscript/Source/AngelscriptTest/Template/Template_WorldTick.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningComponentHierarchyTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 修正后，少数当前依赖“上一层 actor/component scope 恰好留下 world context”的测试会暴露出真实上下文假设错误；这些测试需要改成显式 world/actor scope，而不是继续利用残留上下文。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 `TickWorld` helper 回归，在 world tick 内调用 world/subsystem/timer 相关脚本 API，确认解析到的 world 等于 `Spawner.GetWorld()`。 2. 回归 `Angelscript.TestModule.Learning.Runtime.TimerAndLatent` 与至少一条 actor/component scenario，确认修正后结果在单独运行和串行运行时一致。 3. 检查模板示例，确认仓库内不再存在直接裸调 `World.Tick(...)` 的测试模板实现。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-34 | Defect | 在修复 scenario helper 崩溃路径后立即处理，先把 world tick 的 ambient context 固定正确 |

---

## 发现与方案 (2026-04-08 16:56)

### Issue-35：`ResetSharedCloneEngine()` 用全局 `TObjectIterator<UASClass>` 做 cleanup，shared reset 会越界影响其它 engine 的生成类状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 行号 | 246-268, 276-375; 487-502; 357-444, 552-586; 22-28 |
| 问题 | `ResetSharedCloneEngine()` 在回收 shared engine 生成类时，直接全局遍历 `TObjectIterator<UASClass>`，只要发现 `ScriptTypePtr == nullptr` 就 `RemoveFromRoot()` 并清 `RF_Standalone`，完全不检查这些对象是否属于当前 shared engine 的 package。`LogSharedEngineDebugState()` 和 `ResetSharedEngineReleasesGeneratedComponentClassesTest` 也沿用同样的全局扫描思路，后者甚至按类名全局断言 reset 后数量必须为 0。与此相对，`FindGeneratedClass()` 查找 generated class 时明确先取 `Engine->GetPackageInstance()`，只在该 package 内找类，说明 test helper 其余部分默认模型其实是“generated symbol 归属于具体 engine package”。当前模块里又确实存在 shared engine 与其它 engine 并存的场景，例如 `CompileRestoresScopedGlobalEngineTest` 同时持有 shared engine 和 isolated engine，`SourceNavigationTests` 还会单独申请 production-like engine。 |
| 根因 | shared reset 把“回收本 engine 的 generated symbols”实现成了“回收进程里所有看起来 detached 的 `UASClass`”，缺少 package/owner 过滤；相关回归也因此把 shared cleanup 锁成了全局语义。 |
| 影响 | 只要进程里同时存在 shared engine 与 isolated/production-like engine，shared reset 就可能误清理后者留下的 detached class 壳对象，或者在统计时把别的 engine 的对象也算进 shared cleanup 结果，导致顺序相关失败和误导性的 debug 诊断。推断：一旦两个 engine 生成了同名类，当前按名称全局计数的 helper 自测还会把“别的 engine 仍然正常持有的类”误判为 shared reset 泄漏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 shared cleanup/debug/assert 全部收紧到当前 engine 的 package 或明确 owner 标识，禁止 shared reset 继续扫描全进程 generated class。 |
| 具体步骤 | 1. 在 `Shared/AngelscriptTestUtilities.h` 为 generated-symbol cleanup 增加 owner 过滤：遍历 `UASClass` / `UASFunction` 时，只处理 `GetOuterMost()==Engine.GetPackageInstance()`、`ClassGeneratedBy`/outer 链指向当前 engine package，或其它 runtime 可提供的明确 owner 标识；不要再对所有 detached 对象执行 unroot/clear flags。 2. 把 `LogSharedEngineDebugState()` 的统计改成 package-scoped 版本，并在日志里同时输出“当前 engine package 内数量”与“全局数量”，避免调试时把别的 engine 的对象误算成 shared 污染。 3. 更新 `ResetSharedEngineReleasesGeneratedComponentClassesTest`：先构造 shared engine 和第二个独立 engine，各自生成同名或不同名类；执行 shared reset 后，只断言 shared package 内对象被清掉，而另一 engine package 内对象保持可见。 4. 在 `FindGeneratedClass()` / cleanup helper 之间提取统一的 package 解析逻辑，确保“查找”和“清理”使用同一所有权边界，而不是一个按 package、一个按全局。 5. 对 `AcquireProductionLikeEngine()` 调用者做串行回归，例如 `Editor/AngelscriptSourceNavigationTests.cpp` 之前先运行 shared reset，确认 production-like engine 里的 generated symbols 不会被 shared cleanup 误伤。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Editor/AngelscriptSourceNavigationTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 runtime 当前没有稳定的“generated symbol 属于哪个 engine”查询 seam，需要先补 owner/package metadata；否则仅靠 `OuterMost` 过滤可能漏掉个别历史遗留对象。 |
| 前置依赖 | 建议与已有的 runtime testing seam 收口一起推进，优先复用 engine/package 所有权查询接口。 |
| 验证方式 | 1. 新增双-engine 回归：shared engine reset 之后，shared package 内 generated class 归零，而第二个 engine package 内同名/异名 class 仍保留。 2. 回归 `Shared.EngineHelper.ResetSharedEngineReleasesGeneratedComponentClasses`，确认断言从“全局 0 个同名类”改成“shared package 内 0 个”。 3. 在 `SourceNavigation` 或其它 production-like 测试前后插入 shared reset，确认 production-like engine 的 generated symbol 查找结果不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-35 | Architecture | 在收口 shared cleanup/annotated teardown 时一并处理，先把 reset 边界从“全局”缩回“当前 engine” |
---

## 发现与方案 (2026-04-08 17:06)

### Issue-36：`Engine.ExecuteSnippet` 在失败路径泄漏 `asIScriptFunction` 句柄

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 行号 | 125-149 |
| 问题 | `FAngelscriptTestModuleExecuteSnippetTest::RunTest()` 在 `Module->CompileFunction()` 成功后拿到 `asIScriptFunction* Function`，但如果后续 `TestNotNull(..., Function)` 或 `TestNotNull(..., Context)` 失败，就会直接 `return false`，没有执行 `Function->Release()`。当前只有 happy path 末尾才释放 `Function`。这让核心 smoke test 的失败路径把脚本函数句柄留在 raw `asIScriptModule` 上，和同文件仍未统一治理的 raw-module 生命周期叠加后，会把一次断言失败放大成后续测试的额外状态污染。 |
| 根因 | 测试体手写了多段 early return，却没有用 RAII 或 `ON_SCOPE_EXIT` 统一管理 `asIScriptFunction` / `asIScriptContext` 的释放；资源释放只覆盖了成功路径。 |
| 影响 | 当 `CreateContext()` 失败、函数为空或未来在两者之间插入新的失败断言时，这条测试不会只是单次失败，而会把 leaked function handle 留到模块清理阶段之后才被动回收；在顺序敏感的 raw-module 场景里，这会增加上下文池、discard 时机和诊断输出的噪声。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 snippet smoke test 的 raw function/context 生命周期改成统一 RAII，彻底消除 early-return 泄漏。 |
| 具体步骤 | 1. 在 `Core/AngelscriptEngineCoreTests.cpp` 中为 `Function` 与 `Context` 增加 `ON_SCOPE_EXIT` 或提取小型 RAII 包装，保证从 `CompileFunction()` 成功开始，任何 `return false` 都会执行 `Function->Release()`；`Context` 同理。 2. 将 `CompileSnippet` / `ExecuteSnippet` 两条 raw-snippet 核心测试统一迁移到现有规划中的 raw module helper（可与已记录的 raw-module RAII 合并实现），让 module、function、context 三层资源都走同一个 teardown contract。 3. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 增加失败路径回归：注入 `CreateContext()==nullptr` 或模拟 function lookup 失败，断言 helper/测试退出后 raw module 中不会残留额外 function 引用，且下一次同名 snippet 编译仍可从干净状态开始。 4. 若后续继续保留手写 raw snippet 测试，则在 `TESTING_GUIDE.md` 明确要求 raw `asIScriptFunction*` / `asIScriptContext*` 一律使用 scope cleanup，不允许把 `Release()` 写在 happy path 尾部。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | S |
| 风险 | 如果简单地把 `Function->Release()` 重复放在多个失败分支，后续再次重构时仍可能漏掉新分支；因此应优先用 RAII，而不是继续复制释放语句。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增失败路径回归，强制 `CreateContext()` 失败后确认没有 residual function handle。 2. 回归 `Angelscript.TestModule.Engine.ExecuteSnippet` 与同文件的 `Engine.CompileSnippet`。 3. 连续两次运行同名 raw snippet 回归，确认第一次失败后第二次结果不受残留引用影响。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-36 | Defect | 在 raw-module 生命周期收口时一并处理，先把失败路径资源释放补齐 |
---

## 发现与方案 (2026-04-08 17:09)

### Issue-37：`CompileModuleWithSummary()` 在 preprocessor 失败时会丢失 diagnostics，summary/learning trace 看不到真实错误

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp` |
| 行号 | 30-47, 50-77, 300-305; 718-744; 183-230 |
| 问题 | `CompileModuleWithSummary()` 只会对 `OutSummary.AbsoluteFilenames` 里的文件调用 `CollectCompileTraceDiagnostics()`。但 `BuildModulesForSummary()` 在 `bUsePreprocessor=true` 且 `Preprocessor.Preprocess()` 失败时，会直接 `return false`，此时还没有把入口 `AbsoluteFilename` 写入 `OutAbsoluteFilenames`。结果是：只要错误发生在 preprocessor 阶段，`OutSummary.Diagnostics` 就会稳定为空，调用方只能看到 `CompileResult=Error`。当前自测和 learning trace 只覆盖“预处理成功、编译报错”的 broken annotated 场景，都会断言 `Diagnostics.Num() > 0`，但没有一条回归覆盖真正的 preprocess failure。 |
| 根因 | summary helper 把“收集 diagnostics 的文件列表”建立在 `Preprocess()` 成功之后，导致 preprocessor 失败这条最早的错误路径没有任何可收集的 filename 基线；同时现有测试覆盖只打到了 compile-stage error，没有覆盖 preprocess-stage error。 |
| 影响 | 当脚本导入缺失、预处理指令非法或入口文件本身在 preprocess 阶段报错时，`CompileModuleWithSummary()`、`LearningCompilerTraceTests` 以及任何依赖 `FAngelscriptCompileTraceSummary` 的规划/诊断工具都会把真实错误降格成“失败但没有诊断”，直接削弱缺陷定位能力，并让后续 trace/plan 把最关键的错误上下文丢掉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 summary helper 在进入 preprocessor 前就固定诊断采集基线，并补一条真正的 preprocess-failure 回归。 |
| 具体步骤 | 1. 在 `BuildModulesForSummary()` 的 preprocessed 分支中，写盘成功后立即把入口 `AbsoluteFilename` 加入 `OutAbsoluteFilenames`，不要等 `Preprocess()` 成功后才建立诊断列表。 2. 如果 `FAngelscriptPreprocessor` 还能暴露 imported file 列表或错误文件路径，补充一个专用接口把这些路径一并塞进 `OutAbsoluteFilenames`，保证 imported-file failure 也能被 summary 捕获。 3. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 新增 `CompileSummaryPreprocessDiagnosticCapture` 回归：构造一个 `import` 缺失或非法预处理指令的最小脚本，断言 `bCompileSucceeded==false`、`CompileResult==Error` 且 `Summary.Diagnostics.Num() > 0`，并校验 `Summary.Diagnostics[0].Section` 指向入口文件或失败 import。 4. 在 `LearningCompilerTraceTests.cpp` 增加一条 preprocess-failure scenario，把 trace 中的 `DiagnosticsCount` 和具体 diagnostics code block 固定下来，避免学习 trace 继续只覆盖 compile-stage error。 5. 将 compile-summary helper 的 contract 注释补充为“`Diagnostics` 同时覆盖 preprocess 与 compile 两阶段”，防止后续调用方继续把空 diagnostics 当作正常失败形态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果简单把入口文件强行加入 diagnostics 列表，但 preprocessor 实际把错误记在 import 文件上，仍可能只拿到部分 diagnostics；因此最好同时补一条 preprocessor 失败文件枚举能力。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行新增的 preprocess-failure summary 回归，确认 `Summary.Diagnostics` 不再为空。 2. 回归现有 `Shared.EngineHelper.CompileSummaryDiagnosticCapture`，确保 compile-stage error 的 diagnostics 仍保持不变。 3. 检查 `Learning.CompilerPipeline` trace 输出，确认 preprocess failure 场景能看到具体错误信息，而不是只有 `CompileResult=Error`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-37 | Defect | 在 compile-summary helper 收口时立即补上，先把 preprocess diagnostics 链路打通 |
---

## 发现与方案 (2026-04-08 17:10)

### Issue-38：`DumpAll` 测试把 `CodeCoverage.csv` 固定视为 `Skipped`，导致 coverage 导出集成路径没有任何回归保护

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp` |
| 行号 | 45-95, 109-145; 1173-1219; 13-83 |
| 问题 | `Dump` 侧测试在 `GetExpectedSummaryStatus()` 中把 `CodeCoverage.csv` 的期望状态硬编码成 `Skipped`，因此 `FAngelscriptStateDumpSummaryTest` 永远不会验证成功导出的 coverage 表。可 runtime 的 `DumpCodeCoverage()` 明确存在成功路径：当 `Engine.CodeCoverage != nullptr` 时会遍历 active modules 的命中数据并真正写出 `CodeCoverage.csv`。同时 runtime 还有独立的 `AngelscriptCodeCoverageTests0`，已经证明 recorder 本身可以产出 HTML/coverage 文件。也就是说，仓库既有“coverage 能工作”的底层测试，也有“DumpAll 会导出 coverage 表”的产品路径，但中间的集成层完全没有 automation 覆盖。 |
| 根因 | TestInfrastructure 的 dump 用例默认依赖 `AcquireProductionLikeEngine()` 返回的普通 engine，没有显式安装或驱动 `CodeCoverage` recorder，于是测试作者直接把 summary contract 固化成 `Skipped`，绕过了成功路径验证。 |
| 影响 | 任何针对 `DumpCodeCoverage()` 的回归，例如 active-module 过滤错误、CSV 列格式漂移、coverage recorder 挂接失效、Issue-6 修复后引入的新筛选 bug，都不会在 `DumpAll` 测试中暴露；最终 CI 只能证明“没 recorder 时会跳过”，却无法证明“有 recorder 时导出正确”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `DumpAll` 增加一个最小可控的 coverage-enabled fixture，显式跑通 `CodeCoverage.csv = Success` 的集成路径。 |
| 具体步骤 | 1. 在 `Dump/AngelscriptDumpTests.cpp` 新增专门用例，例如 `DumpAll.CodeCoverage`：创建 test-owned full engine，附加一个 test-local `FAngelscriptCodeCoverage`（仅在 `WITH_AS_COVERAGE` 下编译），并通过最小模块 compile + execute 或直接 `MapExecutableLines()/HitLine()` 产生至少一条命中数据。 2. 复用 `FAngelscriptStateDump::DumpAll()` 或直接 `DumpCodeCoverage()`，断言 `DumpSummary.csv` 中 `CodeCoverage.csv` 的状态从 `Skipped` 变成 `Success`，且生成的 `CodeCoverage.csv` 至少包含一行非零命中。 3. 保留现有 summary/end-to-end 用例作为“无 recorder 时应跳过”的路径，但把 `CodeCoverage.csv` 的断言拆成两类：baseline dump 仍允许 `Skipped`，coverage-enabled dump 必须是 `Success`。 4. 将 runtime 已有的 `AngelscriptCodeCoverageTests0` 中可复用的 recorder 驱动逻辑抽到共享 helper，避免 dump 测试再自己手写一份 coverage 命中构造。 5. 在 `TESTING_GUIDE.md` 或 dump test 注释里写清楚：coverage dump 是可选能力，但 test suite 必须同时覆盖“skip contract”和“success contract”两条路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 该回归只在 `WITH_AS_COVERAGE` 构建下有效；需要为不支持 coverage 的配置保留条件编译和 skip 语义，避免在无 coverage 平台上制造假失败。 |
| 前置依赖 | 建议与 Issue-6 的 coverage 生命周期修复配套推进，否则 success-path dump 可能先稳定复现历史 ghost-file 问题。 |
| 验证方式 | 1. 新增 `DumpAll.CodeCoverage` 回归，在 `WITH_AS_COVERAGE` 下确认 `DumpSummary.csv` 对 `CodeCoverage.csv` 的状态为 `Success`。 2. 检查生成的 `CodeCoverage.csv`，确认至少包含一条 `HitCount > 0` 的记录。 3. 同时回归现有 `DumpAll.Summary`，确认无 recorder 基线场景仍然保持 `Skipped`，两条 contract 不互相污染。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-38 | Architecture | 在 coverage 生命周期修复后立即补集成回归，先把 dump success-path 纳入测试网 |

---

## 发现与方案 (2026-04-08 17:22)

### Issue-39：coverage 示例测试直接依赖 `Documents/Plans` 下的脚本文件，测试输入不受模块自身管理

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp` |
| 行号 | 25-61, 159-178, 236-255, 309-328, 374-393 |
| 问题 | `GetCoverageExampleAbsolutePath()` 把传入路径直接拼到 `FPaths::ProjectDir()` 下，4 条 coverage 示例测试随后都从 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/*.as` 读取脚本，再通过 `CompileCoverageExample()` 编译执行。这说明测试输入文件不在 `AngelscriptTest` 模块自己的源码/fixture 目录里，而是挂在规划文档目录下，由项目根路径和文档布局隐式决定。 |
| 根因 | coverage 示例测试把“规划文档中的示例脚本”直接当成运行时 fixture 使用，没有建立插件内的受控 test-data 目录，也没有提供把文档样例同步为测试资产的独立流程。 |
| 影响 | 只要 `Documents/Plans` 被裁剪、重命名、迁移到别的分支结构，或者文档编辑者修改示例文本但并未同步更新测试预期，`ScriptExamples.Coverage.*` 就会因为非运行时代码改动而失败；测试也无法脱离当前项目目录结构单独复用，破坏了 test fixture 的可移植性和可审计性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 coverage 示例脚本迁移到 `AngelscriptTest` 自有的 fixture 目录，并把文档示例与测试资产分离管理。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptTest` 或插件专用 `TestData` 目录下建立稳定的 coverage fixture 位置，例如 `Plugins/Angelscript/TestData/ScriptExamples/Coverage/`，把 `Example_Coverage_Actor.as`、`Example_Coverage_Component.as`、`Example_Coverage_UObject.as`、`Example_Coverage_PropertySpecifiers.as` 迁入该目录。 2. 将 `GetCoverageExampleAbsolutePath()` 改为基于插件/模块根目录解析 fixture 路径，不再依赖 `ProjectDir()/Documents/Plans/...`。 3. 如果规划文档仍需要保留这些示例，增加一个显式同步脚本或说明文档，把“文档示例”和“测试 fixture”定义成单向同步关系，而不是让自动化测试直接读取文档目录。 4. 为 coverage helper 增加回归：断言所有 coverage fixture 都位于插件 test-data 根目录下，并在测试消息中输出实际 fixture 路径，避免未来再次回退到 `Documents/Plans`。 5. 回归 4 条 `ScriptExamples.Coverage.*` 测试，确认迁移后行为与当前断言保持一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/TestData/ScriptExamples/Coverage/*`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 如果直接移动文件而不保留同步或引用说明，现有文档链接和人工查阅路径会失效；需要在文档侧保留引用说明或同步脚本，避免文档与 fixture 内容再次分叉。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 删除或临时重命名 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage` 后运行 `Angelscript.TestModule.ScriptExamples.Coverage.*`，确认测试仍能从插件 fixture 目录通过。 2. 检查日志中的 fixture 绝对路径，确认均落在插件 test-data 根目录。 3. 对迁移后的 `.as` 文件做一次内容比对，确认运行期断言结果与迁移前一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-39 | Architecture | 先把 coverage fixture 从 `Documents/Plans` 脱钩，再继续扩展 coverage 示例测试 |

---

## 发现与方案 (2026-04-08 17:23)

### Issue-40：`ModuleWatcherQueuesFileChanges` 用硬编码绝对路径构造队列输入，hot-reload 路径契约与当前工程脱节

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | 302-327; 535-541 |
| 问题 | `FAngelscriptModuleWatcherQueuesFileChangesTest` 直接把 `AbsoluteFilename` 写死为 `J:/UnrealEngine/Temp/UE-Angelscript/Saved/Automation/WatcherTest.as`，而测试基础设施生成 automation 脚本路径时统一使用的是当前工程的 `FPaths::ProjectSavedDir()/Automation/...`。这条 watcher 回归并没有从当前工程路径派生队列输入，而是把另一份工作区的绝对路径当作契约常量。 |
| 根因 | hot-reload watcher 测试缺少统一的“automation filename pair”构造 helper，测试作者直接把当时机器上的样例路径内联进了回归。 |
| 影响 | 这条测试只能证明“相同字符串会去重”，却无法证明 watcher 在当前工程的真实 `Saved/Automation` 路径下工作正常；一旦后续对路径规范化、盘符大小写、工程根目录切换或 automation 根目录做修改，这条回归仍可能通过，从而放过真实的路径映射问题。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用当前工程路径动态生成 watcher 测试输入，并把 `FFilenamePair` 构造收敛到共享 helper。 |
| 具体步骤 | 1. 在 `Shared` 层新增统一 helper，例如 `MakeAutomationFilenamePair(const FString& RelativeFilename)`，内部使用 `FPaths::ProjectSavedDir()` 生成绝对路径，并同步给出 `Automation/<Name>.as` 形式的相对路径。 2. 将 `FAngelscriptModuleWatcherQueuesFileChangesTest` 改为调用该 helper 构造 `WatcherTest.as` 的 `FFilenamePair`，删除硬编码的 `J:/UnrealEngine/Temp/UE-Angelscript/...` 字符串。 3. 补一条路径契约断言：验证生成的 `AbsoluteFilename` 以当前 `ProjectSavedDir()` 为前缀，`RelativeFilename` 保持 `Automation/...` 约定，确保测试覆盖真实工程布局。 4. 若 hot-reload 队列内部存在路径规范化逻辑，再新增一条大小写/分隔符归一化回归，避免测试继续只覆盖“完全相同字符串”这一条最弱路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果 watcher 真实依赖的是更早阶段写入的原始绝对路径格式，收敛 helper 后可能暴露已有规范化缺陷；这属于需要被测试显式捕获的真实问题，而不是回避理由。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行 `Angelscript.TestModule.HotReload.ModuleWatcherQueuesFileChanges`，确认在当前工程根目录下仍通过。 2. 新增断言检查 `FilenamePair.AbsoluteFilename` 以 `FPaths::ProjectSavedDir()` 开头，而不是历史工作区路径。 3. 如果补了规范化回归，分别用大小写不同或反斜杠/斜杠混用的路径入队，确认队列计数仍保持去重。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-40 | Defect | 在 hot-reload watcher 回归附近立即收口，先把路径契约从机器私有常量改成工程实际路径 |

---

## 发现与方案 (2026-04-08 17:25)

### Issue-41：startup performance 的“fallback full”基线重置依赖 no-op `DestroyGlobalEngine()`，测量结果可能实际来自 clone 路径

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 42-69, 130-195; 36-39; 654-667, 676-679, 776-779 |
| 问题 | `ResetPerformanceEngineState()` 先 `DestroySharedTestEngine()`，随后在 `FAngelscriptEngine::IsInitialized()` 为真时调用 `FAngelscriptTestEngineScopeAccess::DestroyGlobalEngine()`。但 test access 只是转发到 runtime 的 `FAngelscriptEngine::DestroyGlobal()`，而该函数当前直接 `return false`，不会清理任何 current/global engine。与此同时，`MeasureCreateForTestingFallbackStartup()` 依赖 `CreateForTesting(..., Clone)` 在“没有 current engine”时 fallback 到 Full；runtime 实现里只要 `TryGetCurrentEngine()` 非空就会走 `CreateCloneFrom()`。这意味着性能测试名义上在测 “CreateForTestingFallbackFull”，实际却可能因为残留 ambient engine 而测到 clone 路径。 |
| 根因 | performance 基准复用了已经失效的 legacy-global cleanup 语义，把“DestroyGlobalEngine 已经清空上下文”当成前置条件，但 runtime 早已把 `DestroyGlobal()` 退化成 no-op。 |
| 影响 | `Angelscript.TestModule.Core.Performance.Startup.CreateForTestingFallbackFull` 的采样结果会受前序测试或残留 current engine 影响，可能把 clone 启动时间记录成 full fallback 基线；同文件其它采样也缺少 reset 后状态断言，导致性能回归数据不可比较。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 performance 基准切到显式可验证的引擎基线 fixture，禁止继续依赖 `DestroyGlobalEngine()`。 |
| 具体步骤 | 1. 用显式 baseline fixture 重写 `ResetPerformanceEngineState()`，优先复用 Issue-1 / Issue-19 中规划的 full-epoch isolation 与 thread-local engine reset helper，确保重置后 `TryGetCurrentEngine()==nullptr` 且 shared storage 为空。 2. 删除 `DestroyGlobalEngine()` 在 performance 测试中的使用；若确实需要清空当前栈，改为显式 snapshot/restore 或新的 test-owned cleanup seam，而不是调用 no-op API。 3. 在 `MeasureCreateForTestingFallbackStartup()` 中补 contract 断言：创建出的 engine 必须满足 `CreationMode==Full`、`OwnsEngine()==true`、`GetSourceEngine()==nullptr`；在 `MeasureCreateForTestingCloneStartup()` 中同步断言 clone contract，避免性能测试继续只记录时间不验证路径。 4. 在 `CollectStartupSamples()` 的 warmup 和 measurement 循环之间增加一次 reset 后断言，确保每轮采样都从相同 engine baseline 开始。 5. 若需要保留 legacy-global 访问器，至少在 `TESTING_GUIDE.md` 和性能测试注释中明确其不可用于状态清理，只能用于只读探测。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 一旦加上 creation-mode 断言，当前隐藏的 ambient-engine 污染可能会立刻把基准跑红；这是需要尽早暴露并修复的真实问题，不应再用“只采样时间”掩盖。 |
| 前置依赖 | 建议先完成 Issue-19 中的 thread-local macro engine 显式 reset seam，否则仍可能残留宏私有 owner 干扰性能基线。 |
| 验证方式 | 1. 运行 `Angelscript.TestModule.Core.Performance.Startup.CreateForTestingFallbackFull`，确认新增断言稳定报告 `CreationMode==Full`。 2. 在该测试前串行插入一条会留下 current engine 的用例，确认 baseline fixture 仍能清空上下文并得到 full fallback。 3. 检查新生成的性能工件，确认 fallback/full 与 clone 两条测试的 bind replay 指标继续符合预期。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-41 | Defect | 在继续使用 startup performance 基线前先修复，否则性能数据本身不可信 |

---

## 发现与方案 (2026-04-08 17:44)

### Issue-42：`BuildModule()` 会删除同名前缀脚本文件，破坏仍然存活模块的源码文件生命周期

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/CoverageReportGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 535-578; 96-126; 1497-1545; 25-36; 1091-1116 |
| 问题 | `BuildModule()` 虽然给每次写盘生成了带 `FGuid` 的 `UniqueFilename`，但在写新文件前会先扫描并删除 `Saved/Automation` 下所有 `RequestedModuleName*.as` 文件。`FAngelscriptBuilderRebuildModuleTest` 同一条测试里就连续两次用 `"BuilderRebuild"` 调 `BuildModule()`，第二次调用会删除第一次编译对应的脚本文件，而 `ModuleV1`/`FunctionV1` 在此时仍然存活。与此同时，runtime 会把 `Module->Code[0].AbsoluteFilename` 持久保存在 generated class/function、coverage 报告和 hot-reload 路径状态里；一旦旧文件被删，这些仍然活着的对象就会指向已经不存在的源码。 |
| 根因 | test helper 只把磁盘脚本当成“编译前临时输入”，没有把它建模成与模块实例同生命周期的 test asset；为了清理旧文件采用了全局前缀删除，而不是由当前测试精确回收自己创建的文件。 |
| 影响 | source navigation、`UASClass::GetSourceFilePath()` / `UASFunction::GetSourceFilePath()`、coverage HTML 生成、hot-reload 诊断等依赖 `AbsoluteFilename` 的功能会在模块仍然存活时读到缺失文件；同名模块重编译或不同测试重用同名模块时，还会出现跨测试互删源码文件的顺序相关失败。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 test-generated script file 建立显式所有权和生命周期，禁止 `BuildModule()` 继续做全局前缀删除。 |
| 具体步骤 | 1. 在 `Shared` 层新增 `FAngelscriptTestSourceFileFixture` 或等价结构，负责为当前测试分配独立子目录或 manifest，并记录每个编译产生的 `AbsoluteFilename`。 2. 重写 `BuildModule()`：保留唯一文件名，但删除 `FindFiles(... RequestedModuleName*.as)` + 全量 `Delete()` 逻辑；改为把创建出的文件登记到 fixture，并在 fixture 析构时只删除本测试自己生成、且对应模块已经 teardown 的文件。 3. 对需要“同名模块重建”的测试（至少 `Internals/AngelscriptBuilderTests.cpp` 的 `BuilderRebuildModule`）改用新 fixture，新增断言验证第一次编译生成的 `AbsoluteFilename` 在第二次编译后仍然存在，直到模块/fixture 退出才被删除。 4. 若 helper 需要支持 source-file introspection，再让 fixture 暴露 `GetSourceFiles()`，供 `SourceNavigation` / coverage / dump 回归直接校验文件仍可读，而不是依赖全局 `Saved/Automation` 扫描。 5. 将 `BuildModule()` 与其它写盘 helper 的“源码文件由 fixture 负责回收”规则写入 `TESTING_GUIDE.md`，避免未来再在 helper 内部做 wildcard 删除。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 如果 fixture 在模块真正 discard 前过早删除脚本文件，仍会留下悬空 `AbsoluteFilename`；因此文件回收必须绑定到模块 teardown 或 fixture 析构顺序，而不是仅绑定到“下一次写盘”。 |
| 前置依赖 | 建议与 Issue-4 / Issue-11 的 tracked-module teardown 一起推进，这样文件回收可以与模块回收使用同一所有权边界。 |
| 验证方式 | 1. 新增 `BuilderRebuild` 回归：两次同名 `BuildModule()` 之间断言 `ModuleV1->Code[0].AbsoluteFilename` 仍可读。 2. 运行 `Angelscript.TestModule.Internals.Builder.RebuildModule`、`Angelscript.TestModule.Shared.EngineHelper.GeneratedSymbolLookup`、`Angelscript.TestModule.Dump.DumpAll.*`，确认 source-path 相关功能在重编译后仍稳定。 3. 在 fixture 退出后检查其登记的文件全部被清理，而未登记的 `Saved/Automation` 文件不受影响。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-42 | Defect | 先修复 test script file 生命周期，再继续扩展依赖 source-path 的 coverage / dump / navigation 回归 |

---

## 发现与方案 (2026-04-08 17:46)

### Issue-43：共享 compile helper 默认关闭 automatic imports，导致大部分测试不覆盖产品默认编译模式

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` |
| 行号 | 61-63; 905-907, 1415-1416; 511-527, 571-572; 99-100, 154-155, 220-221 |
| 问题 | runtime 默认配置把 `bAutomaticImports` 设为 `true`，engine 初始化时也会据此打开 `asEP_AUTOMATIC_IMPORTS`。但测试基础设施里，`BuildModule()`、`CompilePreparedModules()`、`CompileModuleInternal()`、`PreprocessAndCompile()` 全都会在编译前用 `TGuardValue<bool>(Engine.bUseAutomaticImportMethod, false)` 加 `FScopedAutomaticImportsOverride(... SetEngineProperty(asEP_AUTOMATIC_IMPORTS, 0))` 强制关闭 automatic imports。本轮对 `BuildModule`、`CompileModuleFromMemory`、`CompileAnnotatedModuleFromMemory`、`CompileModuleWithSummary`、`AnalyzeReloadFromMemory` 的仓库扫描共命中 216 处调用点，说明当前绝大多数 compile/reload 测试默认跑的都不是产品默认配置。 |
| 根因 | compile helper 把“为了减少测试噪声临时关闭 automatic imports”做成了隐式默认行为，而不是由调用方显式声明的 compile policy；结果 helper 的 convenience path 直接替代了产品真实配置。 |
| 影响 | automatic imports 相关的编译、预处理、reload、调试设置和诊断行为在 `AngelscriptTest` 中被系统性欠覆盖；测试即使通过，也只能证明“关闭 automatic imports 后能工作”，不能证明默认配置下的产品路径稳定。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 automatic-import 策略从 helper 隐式副作用改成显式参数，并为默认产品配置建立独立回归。 |
| 具体步骤 | 1. 在 `Shared` compile helper 层新增显式策略枚举，例如 `ETestAutomaticImportMode { RespectEngineDefault, ForceDisabled, ForceEnabled }`，让 `BuildModule()`、`CompileModuleFromMemory()`、`CompileAnnotatedModuleFromMemory()`、`CompileModuleWithSummary()`、`AnalyzeReloadFromMemory()` 都必须接收或继承该策略。 2. 默认值改为 `RespectEngineDefault`，只有少数确实需要 legacy/manual-import 语义的测试才显式传 `ForceDisabled`；内部实现改用 runtime 现成的 `SetAutomaticImportMethodForTesting()` seam，而不是直接改写内部字段。 3. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 新增两组 contract 回归：一组验证 `RespectEngineDefault` 会跟随 runtime 默认 `bAutomaticImports=true` 打开 engine property；另一组验证 `ForceDisabled` 只影响当前 helper 调用，并在退出后恢复原设置。 4. 针对依赖 import 行为的目录补产品默认回归，至少覆盖 `Preprocessor/AngelscriptPreprocessorTests.cpp`、`Compiler/AngelscriptCompilerPipelineTests.cpp`、`HotReload/AngelscriptHotReload*Tests.cpp` 中各一条代表性用例，确认默认 automatic imports 路径能在当前测试基础设施下稳定通过。 5. 在 `TESTING_GUIDE.md` 中把 compile policy 明确写成表格：调用方需要同时选择 engine mode、input kind 和 automatic-import mode，避免 future test 再通过 helper 默认值无意绕开产品配置。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | L |
| 风险 | 一旦把默认值切回 `RespectEngineDefault`，现有大量测试可能暴露出隐藏的 import-path 依赖；需要先用显式策略参数保留旧行为，再逐目录迁移，避免一次性把整套测试网打红。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 helper contract 回归，验证 `RespectEngineDefault` 与 `ForceDisabled` 两种策略。 2. 回归 `Angelscript.TestModule.Preprocessor.*`、`Angelscript.TestModule.Compiler.EndToEnd.*`、`Angelscript.TestModule.HotReload.*` 中至少各一条代表性测试，确认默认 automatic imports 路径可运行。 3. 重新执行一次 `rg` 调用点扫描，确认高风险目录不再默认落在隐式 `ForceDisabled` 路径上。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-43 | Architecture | 在继续扩展编译/热重载回归前先显式化 compile policy，避免测试网长期偏离产品默认配置 |

---

## 发现与方案 (2026-04-08 17:49)

### Issue-44：`Builder.RebuildModule` 名义上测试重建，实际只重复走了两次 `Initial` compile

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` |
| 行号 | 535-573; 353-365; 96-139 |
| 问题 | `FAngelscriptBuilderRebuildModuleTest` 通过两次调用 `BuildModule("BuilderRebuild", ...)` 来验证“重建”。但 `BuildModule()` 内部把编译类型硬编码为 `Engine.CompileModules(ECompileType::Initial, ...)`，并不会进入 reload 语义。与之相对，当前基础设施里真正表达 reload 语义的 helper 是 `CompileModuleFromMemory()` 的 `SoftReloadOnly` 和 `CompileAnnotatedModuleFromMemory()` 的 `FullReload`。因此这条名为 `RebuildModule` 的回归并没有覆盖 `SoftReloadOnly` / `FullReload` 路径，也不会触发 `PartiallyHandled`、`ErrorNeedFullReload` 或 reload-requirement 相关分支。 |
| 根因 | 测试名称和 helper 语义脱节；`BuildModule()` 被当成“通用编译入口”复用，但它其实是固定 `Initial` compile 的 convenience helper，不能代表重建/重载 contract。 |
| 影响 | `Internals.Builder.RebuildModule` 给出了“builder 重建已被覆盖”的假象，实际却没有保护 reload 决策、旧模块替换和 full-reload requirement 这些关键路径；相关回归一旦退化，当前测试不会报错。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 builder 重建测试改成显式 compile-type 驱动，分别覆盖 initial、soft reload 和 full reload contract。 |
| 具体步骤 | 1. 在 `Shared` 层新增显式 helper，例如 `CompileModuleForTest(FAutomationTestBase&, FAngelscriptEngine&, ECompileType, FName ModuleName, FString Filename, FString Script, ECompileResult* OutCompileResult)`，不要再让 `BuildModule()` 承担 reload 语义。 2. 保留 `BuildModule()` 作为“initial compile convenience path”，但在命名或注释中明确它只覆盖 `ECompileType::Initial`。 3. 将 `FAngelscriptBuilderRebuildModuleTest` 拆成至少两条回归：一条 `Initial -> SoftReloadOnly` 的 plain rebuild，断言第二次编译后执行结果更新且 `CompileResult` 符合 soft reload 预期；另一条 annotated/full-reload 场景，断言需要 full reload 时能得到 `PartiallyHandled` 或 `ErrorNeedFullReload` 之类的明确结果。 4. 如果 builder 层还需要保留“同名模块两次 initial compile”的行为覆盖，把现有测试重命名成 `RecreateModuleWithInitialCompile`，避免继续把 recreate 冒充 rebuild。 5. 在 `TESTING_GUIDE.md` 中补充“helper name 与 compile type 必须一一对应”的约束，并要求重建/热重载测试必须显式记录 `ECompileType` 与期望 `ECompileResult`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 把假“重建”测试改成真实 reload contract 后，现有 builder 行为缺陷可能会立刻暴露；需要按 plain 和 annotated 两类输入分开迁移，避免一次性把所有 reload 测试都绑到同一 helper 改动上。 |
| 前置依赖 | 建议与 Issue-43 的 compile policy 显式化一起推进，这样 compile type 和 automatic-import mode 可以在同一套 helper 参数中声明。 |
| 验证方式 | 1. 新增 `Initial -> SoftReloadOnly` builder 回归，确认第二次编译真正走 reload 路径并返回期望 `ECompileResult`。 2. 新增 annotated/full-reload builder 回归，覆盖 `PartiallyHandled` / `ErrorNeedFullReload` 路径。 3. 重新运行 `Angelscript.TestModule.Internals.Builder.*`，确认测试名称与实际 compile type 契约一一对应。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-44 | Defect | 在 builder / reload 类回归扩展前修正这条假覆盖用例，避免继续把 initial compile 误当重建保护网 |

---

## 发现与方案 (2026-04-08 17:56)

### Issue-45：non-preprocessor compile helper 会给模块挂上不存在的源码路径

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/CoverageReportGenerator.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptHandleTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptInheritanceTests.cpp` |
| 行号 | 115-163；1497-1545；280-284，405-417；25-33；202-217；55-79，123-155；24-49，91-116 |
| 问题 | `MakeModuleDesc()` 会为 relative `Filename` 直接合成 `Saved/Automation/<Filename>`，absolute `Filename` 则原样写进 `Section.AbsoluteFilename`；但 `CompileModuleInternal()` 随后直接把 `Section.Code` 送进 `Engine->CompileModules()`，全过程没有把脚本文本写到这些路径。结果是 `CompileModuleFromMemory()` / plain `CompileModuleWithResult()` 产出的模块携带了看起来像真实磁盘文件、实际并不存在的 `AbsoluteFilename`。runtime 后续又把这个字段当真使用：`UASClass::GetSourceFilePath()` / `UASFunction::GetSourceFilePath()` 直接返回 `Module->Code[0].AbsoluteFilename`，`Bind_UObject.cpp` 把它暴露给脚本，coverage 报告生成器还会按该路径 `LoadFileToString()`。仓库里至少有 43 处 `CompileModuleFromMemory()` 调用点，说明这不是局部问题；相反，`RuntimeSourceMetadataBindingsTest` 已经只能先 `SaveStringToFile()` 再调 helper，手工绕过这条假路径契约。 |
| 根因 | 当前 helper 把“内存中提供脚本文本”和“模块拥有可回溯的 source file identity”混成了一个 API：它伪造文件名元数据，却没有同时承担文件落盘与生命周期管理。 |
| 影响 | 任何依赖 source path 的测试或运行时能力都会在 plain helper 路径上得到 phantom file：`GetSourceFilePath()` 可能返回不存在的文件，coverage HTML 会读取空内容，diagnostics / dump / hot-reload 也会记录一个无法打开的路径。更严重的是，当前大量 plain compile 测试实际上没有覆盖真实 file-backed contract，只覆盖了“带假路径的内存编译”这一条偏离产品语义的测试路径。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“memory compile”与“file-backed compile”显式分离；凡是 helper 接收 `Filename` 并把它写进 `ModuleDesc`，就必须真正物化该文件或改用明确的 `memory://` 身份。 |
| 具体步骤 | 1. 在 `Shared` 层复用或扩展 Issue-42 规划的 source-file fixture，新增统一入口（例如 `MaterializeTestSourceFile(FString Filename, FString Script)`），负责创建父目录、写盘并登记 cleanup。 2. 重写 `MakeModuleDesc()` / `CompileModuleInternal()` / plain `CompileModuleWithResult()`：当调用方传入 relative 或 absolute `Filename` 时，先通过 fixture 把 `Script` 写到该路径，再生成 `ModuleDesc`；不要继续把未写盘的路径塞进 `AbsoluteFilename`。 3. 若确实需要纯内存编译，再新增单独 helper（例如 `CompileModuleInMemoryWithoutFile()`），把 `RelativeFilename` / `AbsoluteFilename` 标成 `memory://<ModuleName>` 或等价 sentinel，并在注释与文档中明确该入口不能用于 source metadata / coverage / hot-reload / dump 类测试。 4. 先迁移当前明显依赖假路径的调用点：`AngelscriptHandleTests.cpp`、`AngelscriptInheritanceTests.cpp`、`AngelscriptFunctionTests.cpp` 里的 `NegativeCompileIsolation` / relative-filename compile，统一走新 fixture，而不是继续传不存在的磁盘路径。 5. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 新增 file-backed contract 回归：一条用 relative filename 编译 plain module，断言 `Module->Code[0].AbsoluteFilename` 存在且文件内容与输入脚本一致；另一条用 absolute filename 编译 plain module，断言 helper 会更新该文件而不是只记录路径。 6. 在 `TESTING_GUIDE.md` 中补一张表，明确 `CompileModuleFromMemory` 系 helper 默认是否落盘、何时该选 file-backed helper、何时可选 memory-only helper。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptHandleTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptInheritanceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 一旦把 plain helper 切到真实 file-backed 语义，现有测试里依赖“路径存在但文件不存在”的隐式假设会被暴露；同时写盘会引入额外 cleanup 责任，需要与 Issue-42 的 source-file fixture 一起收口，避免把问题从“假路径”变成“真实文件泄漏”。 |
| 前置依赖 | 建议与 Issue-42 一起推进，共用 source-file fixture，避免为 plain helper 再发明第二套文件清理机制。 |
| 验证方式 | 1. 新增 helper contract 回归，验证 plain compile 后 `AbsoluteFilename` 对应文件真实存在且内容正确。 2. 回归 `Angelscript.TestModule.Bindings.SourceMetadata`、`Angelscript.TestModule.Editor.SourceNavigation.Functions` 与任一 coverage 导出测试，确认读取到的源码路径可以实际打开。 3. 对 `AngelscriptHandleTests.cpp` / `AngelscriptInheritanceTests.cpp` 中迁移后的负向 compile 用例做串行运行，确认文件存在性与 diagnostics 路径在重跑后保持稳定。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-45 | Defect | 在继续扩展 source metadata / coverage / dump 回归前先修复，先把 plain compile helper 的 fake file contract 收口 |

---

## 发现与方案 (2026-04-08 17:59)

### Issue-46：expected-compile-failure 用例手写 `Fatal -> Log` 日志切换，持续泄漏全局 verbosity

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptHandleTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptInheritanceTests.cpp` |
| 行号 | 145-154，195-204；97-107，173-183，202-212；26-37，94-105；63-73，130-140，161-171 |
| 问题 | 多个“预期编译失败”的测试都在测试体里手写同一模式：`UE_SET_LOG_VERBOSITY(Angelscript, Fatal)`，随后调用 `CompileModuleWithResult()`，再无条件 `UE_SET_LOG_VERBOSITY(Angelscript, Log)`。我对 `Plugins/Angelscript/Source/AngelscriptTest` 的扫描共命中 17 处 `Fatal` 切换。这个模式不会恢复进入测试前的真实 verbosity；只要上一条测试、命令行或调试会话把 `Angelscript` 类别调成 `Verbose` / `VeryVerbose`，这些负向用例一跑完就会永久降回 `Log`。由于实现分散在多个文件里，任何修复都必须逐处收口。 |
| 根因 | 测试基础设施缺少统一的“静默预期失败编译”fixture，导致调用方把日志抑制、compile result 断言和错误期望都散落到各自测试体里，用最短路径复制了一个带全局副作用的实现。 |
| 影响 | 后续依赖高 verbosity 的 trace、diagnostics 或 dump 测试会看到被前序负向用例改写后的日志级别，结果继续受执行顺序影响；同时 17 处重复样板也放大了维护成本，后续若要统一调整 compile-failure contract、写盘策略或 diagnostics 断言，极易出现修一半漏一半。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 提供统一的 expected-compile-failure helper：内部 snapshot/restore 当前 verbosity，并把失败断言、`ECompileResult` 校验和脚本文件准备收敛到一处。 |
| 具体步骤 | 1. 在 `Shared` 层新增 RAII guard（例如 `FScopedAngelscriptLogVerbosityOverride`），进入时保存 `Angelscript` 类别当前 verbosity，退出时恢复原值；禁止测试文件继续直接写 `Fatal` / `Log` 配对。 2. 基于该 guard 再增加统一 helper（例如 `ExpectCompileFailure(FAutomationTestBase&, FAngelscriptEngine&, ECompileType, FName ModuleName, FString Filename, FString Script, ECompileResult ExpectedResult)`），封装日志抑制、`CompileModuleWithResult()` 调用、`bCompiled==false` 与 `OutCompileResult==ExpectedResult` 的断言。 3. 先迁移已确认的 4 个高重复目录：`AngelscriptCoreExecutionTests.cpp`、`AngelscriptFunctionTests.cpp`、`AngelscriptHandleTests.cpp`、`AngelscriptInheritanceTests.cpp`；迁移后删除各文件中的裸 `UE_SET_LOG_VERBOSITY(Angelscript, Fatal/Log)`。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 新增回归：先把 `Angelscript` 类别调到非 `Log` 级别，再调用新 helper，断言 helper 退出后 verbosity 恢复为进入前的值。 5. 若 Issue-45 的 file-backed source fixture 一并落地，让 `ExpectCompileFailure` 同时接管脚本文本写盘，避免负向编译测试继续传假路径并各自维护 `NegativeCompileIsolation`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptCoreExecutionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptHandleTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptInheritanceTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果某些现有测试实际上依赖“helper 结束后日志级别被重置成 `Log`”，迁移后会暴露这类隐藏耦合；但这正是需要显式化的历史副作用，不应继续保留。 |
| 前置依赖 | 无；若与 Issue-45 联动实现，可一起把负向 compile 的假路径问题收口。 |
| 验证方式 | 1. 新增 verbosity-restore 自测，验证新 helper 前后日志级别一致。 2. 回归 `Angelscript.TestModule.Angelscript.Core.*`、`Angelscript.TestModule.Angelscript.Functions.*`、`Angelscript.TestModule.Angelscript.Handles.*`、`Angelscript.TestModule.Angelscript.Inheritance.*` 中所有 expected-failure 用例，确认结果不变。 3. 在一条将 `Angelscript` 类别提升到 `Verbose` 的 trace 测试后串行运行这些负向用例，再继续跑 diagnostics / dump 类测试，确认后者仍能看到原始 verbosity。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-46 | Defect | 在 source-file contract 收口后处理，顺手把 expected-failure 样板与日志状态泄漏一起统一掉 |

---

## 发现与方案 (2026-04-08 18:01)

### Issue-47：`CompileAnnotatedModuleFromMemory()` 在 absolute-path 分支实际编译磁盘文件，而不是传入的 `Script`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 行号 | 33-35；165-206，358-365；202-217 |
| 问题 | helper 公开的是 `CompileAnnotatedModuleFromMemory()` / `AnalyzeReloadFromMemory()` 这类 “from memory” API，但 `PreprocessAndCompile()` 在 `Filename` 为 absolute path 时只做 `AbsoluteFilename = Filename`，不会把传入的 `Script` 写到磁盘，然后立刻让 preprocessor 从该路径读文件。也就是说，absolute-path 分支真正被编译的是磁盘上现有内容，而不是调用方传进来的字符串。当前仓库里唯一的 absolute-path 调用点 `RuntimeSourceMetadataBindingsTest` 已经被迫先 `SaveStringToFile(Script, *ScriptPath)`，再调 `CompileAnnotatedModuleFromMemory(...)`，说明调用方必须手工同步磁盘文件，helper 才不会编译错内容。 |
| 根因 | `PreprocessAndCompile()` 把 “relative path 代表临时内存脚本，需要落盘” 和 “absolute path 代表现成磁盘脚本，无需使用 `Script` 参数” 混在同一个实现里，但 API 名称与签名仍宣称输入主体是内存中的 `Script`。 |
| 影响 | 只要调用方传 absolute path 却忘了预先写盘，helper 就会静默编译 stale file 或空文件，测试可能根本没有覆盖自己声明的脚本文本；同时 absolute-path 调用者还得重复手写目录创建、写盘和 cleanup，继续把 file-backed contract 散落到业务测试里。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 “from memory” 与 “from disk” 拆成显式 API；只要 helper 名称带 `FromMemory`，就必须保证无论 relative 还是 absolute path 都编译传入的 `Script`。 |
| 具体步骤 | 1. 重构 `PreprocessAndCompile()`：统一先解析出目标 `AbsoluteFilename`，创建其父目录，然后始终把 `Script` 写到该路径，再调用 preprocessor；不要让 absolute-path 分支绕过 `SaveStringToFile()`。 2. 如果确实需要“从现有磁盘文件编译”能力，新增单独 API（例如 `CompileAnnotatedModuleFromDisk()` / `AnalyzeReloadFromDisk()`），把当前 absolute-path 读取磁盘的语义移到新接口，避免继续污染 `FromMemory` 合约。 3. 更新 `Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`：若保留 `FromMemory` 语义，就删除手工 `SaveStringToFile()`，改由 helper 自己写盘；若该测试必须验证“脚本文件先存在于磁盘”，则改调新的 `FromDisk` helper，让调用点语义明确。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 增加回归：先把某个 absolute path 写入旧脚本 A，再直接调用 `CompileAnnotatedModuleFromMemory(..., AbsolutePath, ScriptB)`，断言最终生成的 class/function 来自 `ScriptB` 而不是磁盘上的 A。 5. 在 `TESTING_GUIDE.md` 中明确 `Filename` 只负责 source identity，不应改变 `Script` 参数是否生效；disk-backed 场景必须走显式的 `FromDisk` 接口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | S |
| 风险 | 一旦把 `FromMemory` 语义收紧，现有依赖“helper 读取磁盘现成文件”的少数调用点需要显式迁移到 `FromDisk` API；但这类迁移是必要的，因为当前行为本身就与函数名冲突。 |
| 前置依赖 | 无；若与 Issue-45 的 file-backed source fixture 联动，可复用同一套写盘与 cleanup 机制。 |
| 验证方式 | 1. 新增 absolute-path 回归，验证 `CompileAnnotatedModuleFromMemory()` 实际编译的是传入 `Script`。 2. 回归 `Angelscript.TestModule.Bindings.SourceMetadata`，确认不再需要手工写盘也能得到正确 source path 与 source line。 3. 若新增 `FromDisk` API，再补一条磁盘优先回归，确认只有该接口会读取磁盘已有内容。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-47 | Defect | 在 compile helper API 收口时一并修复，避免 future caller 再误以为 absolute-path 仍属于 from-memory 语义 |

---

## 发现与方案 (2026-04-08 18:15)

### Issue-48：`RunScriptExampleCompileTest()` 会把依赖脚本拼进主文件，example compile 回归绕过真实多文件契约

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.h`<br>`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExamplePropertySpecifiersTest.cpp` |
| 行号 | 7-12; 42-57; 9-88 |
| 问题 | `FScriptExampleSource` 明确暴露了 `DependencyFileName` / `DependencyScriptText` 两个字段，但 `RunScriptExampleCompileTest()` 在检测到依赖后，并没有把依赖文件单独物化或交给 preprocessor，而是直接把依赖文本追加到 `CombinedScriptCode`，最后只以一个 `VirtualFileName = "ScriptExamples/<ExampleFileName>"` 调 `CompileAnnotatedModuleFromMemory()`。当前 `PropertySpecifiers` example 正在提供 `Example_Enum.as` 作为依赖文件，但编译路径实际上从头到尾只处理一份合成后的单文件脚本。 |
| 根因 | example compile 支撑把“多文件 example”简化成了“单文件字符串拼接”，导致 `DependencyFileName` 退化成只做空指针校验的死字段，helper 自身没有能力表达真实的多文件输入。 |
| 影响 | script example 回归无法覆盖真实的 dependency file identity、multi-file diagnostics、source path/source line 归属以及 cleanup 规则；只要 preprocessor / batch compile / automatic import 对跨文件符号的处理退化，`PropertySpecifiers` 这类带依赖的 example 仍可能继续通过，因为依赖内容已经被内联到主文件里。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 example compile 支撑改成真正的多文件 fixture，禁止继续通过字符串拼接伪装 dependency compile。 |
| 具体步骤 | 1. 在 `Examples` 或 `Shared` 层新增专用 helper（例如 `CompileExampleModuleSetFromMemory()`），输入主文件 + 依赖文件列表，分别物化每个 `Filename` 对应的脚本文本，再把这些文件一起交给 preprocessor / compile pipeline，而不是合并成 `CombinedScriptCode`。 2. 修改 `RunScriptExampleCompileTest()`：当 `DependencyScriptText != nullptr` 时，必须同时消费 `DependencyFileName`，把主文件和依赖文件都登记到 source-file fixture，并在 teardown 中统一清理，不再只 `DiscardModule(ModuleName)`。 3. 先迁移已存在的多文件 example：`AngelscriptScriptExamplePropertySpecifiersTest.cpp` 应通过真实的 `Example_PropertySpecifiers.as` + `Example_Enum.as` 两文件编译通过，而不是依赖内联后的单文件。 4. 在 `Examples` 或 `Shared` 自测中增加回归：构造一个主文件引用依赖枚举/类型的最小 example，断言编译成功时依赖文件路径真实出现在 compile diagnostics/source metadata 中；同时移除依赖文件后该回归必须失败，确保测试真在覆盖多文件契约。 5. 更新 `AngelscriptScriptExampleTestSupport.h` 注释或 `TESTING_GUIDE.md`，把 `DependencyFileName` 明确成真实编译输入，而不是“可选附加文本”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.h`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExamplePropertySpecifiersTest.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 一旦切到真实多文件编译，部分 example 可能暴露出对 automatic imports、真实依赖声明或 cleanup 的隐藏依赖；但这些正是当前拼接实现掩盖掉的真实产品契约。 |
| 前置依赖 | 建议结合 Issue-24、Issue-25、Issue-45 一起推进，复用 preprocessed file identity 与 source-file fixture 的收口。 |
| 验证方式 | 1. 新增多文件 example 回归，验证 `DependencyFileName` 不再是死字段。 2. 回归 `Angelscript.TestModule.ScriptExamples.PropertySpecifiers` 与其它 `ScriptExamples.*` compile 用例，确认单文件 example 行为不变、多文件 example 改为真实依赖编译。 3. 人工检查依赖文件相关 diagnostics/source metadata，确认不再全部归属到主文件 `ScriptExamples/<ExampleFileName>`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-48 | Defect | 在 script example 回归继续扩展前先修复，避免多文件 example 覆盖率继续建立在单文件拼接假象上 |

---

## 发现与方案 (2026-04-08 18:22)

### Issue-49：`DestroySharedTestEngine()` / `AcquireFreshSharedCloneEngine()` 在仍有 live clone 时直接拆 shared owner，触发 deferred shutdown 并把旧 shared state 带进后续测试

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | 169-199，392-432；629-650，1133-1225；368-475；552-557，787-788 |
| 问题 | `GetOrCreateSharedCloneEngine()` 首次创建的是带 persistent scope 的 Full engine，不是真正的 clone；在该 scope 仍是 current engine 时，`CreateIsolatedCloneEngine()` 会经由 `CreateForTesting(..., Clone)` 走 `CreateCloneFrom()`，把 `ActiveCloneCount` 加到 shared owner 的 `SharedState` 上。与此同时，`DestroySharedTestEngine()` 与 `AcquireFreshSharedCloneEngine()` 无论 clone 依赖数是多少，都会直接 `ResetSharedCloneEngine()` 后 `SharedEngineStorage.Reset()`。runtime 的 `Shutdown()` 已明确把“Full owner 仍有 clone dependents”视为拒绝关闭的路径：它会记录 `Rejecting Full engine shutdown while Clone instances still reference shared state.`，设置 `bPendingOwnerRelease=true`，并一直把 shared resources 留到最后一个 clone 析构才真正释放。runtime 自测也已验证：source owner 析构后，clone 仍会继续持有 shared type metadata、script engine 和 pooled context。 |
| 根因 | test helper 的 shared teardown 只考虑“把当前 shared engine 清空并重建”，没有把 runtime 的 owner/clone 生命周期契约纳入 cleanup 设计；helper 也缺少一条专门覆盖“shared owner 仍有 live clone”时 fresh/destroy 行为的回归。 |
| 影响 | 已验证事实：helper 自测当前已经能构造出“shared engine 为 current，然后再创建 isolated clone”的前置条件。推断：一旦某条测试在 clone 仍然存活时调用 `DestroySharedTestEngine()`、`AcquireFreshSharedCloneEngine()` 或间接走到 `AcquireProductionLikeEngine()`，shared helper 就会提前丢掉 owner 指针，而旧 shared state 仍继续存活；随后新一轮 shared full engine 又会被创建，导致同一进程短时间内并存两套 full epoch，触发意外错误日志、残留 type/context pool、以及顺序相关的 flaky 结果。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 shared teardown/fresh 路径补上 clone-dependent guard；只要 shared owner 仍有 live clone，就拒绝回收或轮换 shared epoch，而不是继续依赖 runtime 的 deferred-shutdown 错误路径兜底。 |
| 具体步骤 | 1. 在 `Shared/AngelscriptTestUtilities.h` 增加显式探针，例如 `GetSharedEngineActiveCloneCount()` / `HasLiveSharedCloneDependents()`，直接读取 shared owner 的 `GetActiveCloneCountForTesting()`，并把 instance id、clone count 一起写进 helper 诊断日志。 2. 把 `DestroySharedTestEngine()` 重构成可返回状态的 API（例如 `TryDestroySharedTestEngine(FString* OutReason)`）：当 live clone count > 0 时，不再调用 `ResetSharedCloneEngine()`、不再 `SharedEngineStorage.Reset()`，而是返回失败原因并保留现有 shared owner，避免制造 deferred shutdown。 3. 相应改造 `DestroySharedAndStrayGlobalTestEngine()`、`AcquireFreshSharedCloneEngine()`、`AcquireProductionLikeEngine()`：这些入口必须显式处理“shared owner 仍被 clone 引用”的失败状态。对必须拿 fresh epoch 的调用点，改成创建 `CreateFullTestEngine()` / `ETestEngineMode::IsolatedFull` 这种 test-owned owner；对必须复用 shared owner 的调用点，则先释放 clone 再重试。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 新增回归：先获取 shared engine，再创建 dependent clone，随后调用新的 destroy/fresh helper，断言不会触发 `Rejecting Full engine shutdown...`，不会新建第二个 shared full engine；等 clone 释放后再次 fresh acquire，才允许 shared owner 被真正轮换。 5. 在 `TESTING_GUIDE.md` 和宏文档里补一条硬规则：任何会调用 fresh/destroy shared helper 的测试，都不得在外层保留由 shared owner 派生的 clone；需要跨 setup 保留 clone 的场景必须改用 isolated full source engine。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` |
| 预估工作量 | M |
| 风险 | 行为收紧后，少数当前“误打误撞可用”的测试会暴露出自己在 shared owner 仍有 dependent clone 时就尝试 fresh reset 的隐式耦合，需要同步迁移到 isolated full fixture 或显式缩短 clone 生命周期。 |
| 前置依赖 | 建议与 Issue-5、Issue-12 联动：shared full / shared clone 的命名和 clone fallback 语义越清晰，这条 guard 越容易落地且不易再次被绕过。 |
| 验证方式 | 1. 新增 shared-owner-with-live-clone 回归，验证 destroy/fresh helper 会拒绝轮换 shared epoch，而不是触发 runtime 的 rejection log。 2. 回归 `Angelscript.CppTests.MultiEngine.CloneKeepsSharedStateAlive`、`Angelscript.CppTests.MultiEngine.DestroyingSourceWhileCloneAliveIsRejected`、`Angelscript.CppTests.MultiEngine.DeferredSharedStateReleasePurgesLocalContextPool`，确认 runtime clone 生命周期契约未被破坏。 3. 串行运行一条创建 shared-backed clone 的 helper 测试后接 `AcquireFreshSharedCloneEngine()` 调用者，确认不再出现意外错误日志，且 fresh/shared 指针只会在 clone 释放后发生轮换。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-49 | Defect | 在继续使用 fresh/destroy shared helper 扩展更多测试前先修复，先把 shared owner 与 clone dependents 的生命周期契约收口 |

---

## 发现与方案 (2026-04-08 18:25)

### Issue-50：`ScriptExamples.Coverage.*` 四条测试复制同一套 production bootstrap，`PropertySpecifiers` 还把多类断言塞进单个超长测试体

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp` |
| 行号 | 159-178，236-255，309-328，374-484 |
| 问题 | 4 条 `ScriptExamples.Coverage.*` 用例都在测试体里重复同一段 bootstrap：`DestroySharedTestEngine()`、创建 `FActorTestSpawner`、`InitializeGameSubsystems()`、`RequireRunningProductionEngine()`、建立 `FAngelscriptEngineScope`、定义 `ModuleName`、`ON_SCOPE_EXIT` 执行 `DiscardModule()`，然后才开始各自的示例断言。与此同时，`FAngelscriptScriptExampleCoveragePropertySpecifiersTest::RunTest()` 一条函数里连续校验 property lookup、metadata、edit flags、component hierarchy 与 spawned actor 行为，单个测试体跨越 110+ 行。 |
| 根因 | coverage example suite 已经抽出了一些小 helper（如 `CompileCoverageExample()`、`CreateCoverageRuntimeComponent()`、`RequireProperty()`），但最关键的 engine/world/module arrange/teardown 仍散落在每条测试里，没有上升成可复用 fixture；测试作者因此继续在业务测试函数里复制完整 bootstrap，并把多维断言堆叠到同一 case。 |
| 影响 | 任何关于 production-engine 解析、shared cleanup、module teardown 的修复都要在 4 个测试体里同步修改，极易出现一半迁移一半遗留；`PropertySpecifiers` 这种巨型测试一旦失败，只能得到一条笼统的 case 失败结果，难以区分是 metadata、edit flag、还是 component hierarchy 回归，降低了 coverage suite 作为示例回归守门网的可维护性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 提取统一的 coverage example fixture，复用 production world/engine/module 生命周期；把 `PropertySpecifiers` 拆成更小、更聚焦的测试用例。 |
| 具体步骤 | 1. 在 `Examples/AngelscriptScriptExampleCoverageTests.cpp` 的匿名命名空间，或提升到 `Shared` 层，新增 `FScriptExampleCoverageFixture`：构造时负责 `DestroySharedTestEngine()`、创建 `FActorTestSpawner`、初始化 subsystem、获取当前测试 world 对应的 production engine、建立 `FAngelscriptEngineScope`；析构时统一 `DiscardModule()`。 2. 给 fixture 提供小型入口，如 `CompileExample(FName ModuleName, const TCHAR* RelativePath, FName GeneratedClassName)`、`SpawnActor(UClass*)`、`GetWorld()`，把 4 条测试公共的 arrange 代码从 `RunTest()` 中移走，只留下各自断言。 3. 将 `FAngelscriptScriptExampleCoveragePropertySpecifiersTest` 至少拆成 3 条自动化测试：一条只校验 property metadata / edit flags，一条只校验 generated component classes 与 hierarchy，一条只校验 actor spawn + BeginPlay 后的 runtime 状态；共享同一 fixture，但每条测试只关注一个回归面。 4. 对 `Actor` / `Component` / `UObject` 三条 coverage 测试，保留现有断言内容，但统一改用 fixture 提供的 arrange 入口，删除文件内重复的 `DestroySharedTestEngine()` / `RequireRunningProductionEngine()` / `ON_SCOPE_EXIT` 模板代码。 5. 在 `TESTING_GUIDE.md` 或该文件头部注释中补充规则：script example coverage tests 必须通过 fixture 获取 production engine 和 module cleanup，不再允许在业务用例里手写 bootstrap。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 拆分 `PropertySpecifiers` 后会新增更多 automation case，运行时间与日志条目会略增；但换来的是更清晰的失败定位和更低的生命周期样板复制成本。 |
| 前置依赖 | 建议在 Issue-29、Issue-49 收口 production/shared engine 选择语义后同步推进，避免先把错误的 bootstrap 行为封装进新 fixture。 |
| 验证方式 | 1. 回归 `Angelscript.TestModule.ScriptExamples.Coverage.*`，确认 4 条现有示例测试在切到 fixture 后结果保持一致。 2. 人工触发一类 property metadata 回归，确认失败只落在对应拆分后的单条测试，而不是整个 `PropertySpecifiers` 大用例全红。 3. 检查文件内重复 bootstrap 片段数量，确认 `DestroySharedTestEngine()` / `RequireRunningProductionEngine()` / module cleanup 样板只保留在 fixture 一处。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-50 | Refactoring | 在 shared/production engine 生命周期契约稳定后推进，把 coverage example suite 收敛到统一 fixture 并拆小超长用例 |

---

## 发现与方案 (2026-04-08 18:40)

### Issue-51：`LifecycleEndPlacement` 校验只检查 `ASTEST_END_*` 前一条非空行，无法发现被 `}` 包裹的 early return

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp` |
| 行号 | 13-56 |
| 问题 | `CollectTerminalReturnBeforeLifecycleEndLocations()` 在命中 `ASTEST_END_*` 后，只向上找第一条非空行，并且仅当该行以 `return ` 开头、以 `;` 结尾时才报错。这样会漏掉最常见的违规形态：`if (...) { return false; }`、`return SomeBool && OtherCheck;` 后面再跟一个 `}` 才到 `ASTEST_END_*`。在这些情况下，离 `ASTEST_END_*` 最近的非空行通常是 `}`，当前校验会直接 `break`，导致真正的 terminal return 被静默放过。 |
| 根因 | 现有实现采用“上一条非空文本行”启发式，没有跟踪当前宏作用域内的 brace depth，也没有定位“结束宏之前最后一个可执行语句”。 |
| 影响 | `Angelscript.TestModule.Validation.LifecycleEndPlacement` 可能在违规代码已经进入仓库后仍然保持绿色，后续开发者会误以为 `ASTEST_END_*` 的显式配对规则仍被守住；一旦真实 early return 绕过生命周期尾部，宏层 cleanup/readability 约束就会继续漂移。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 lifecycle-end 校验从“上一条非空行匹配”升级为“扫描当前宏作用域内最后一个可执行 return”的结构化检查。 |
| 具体步骤 | 1. 重写 `CollectTerminalReturnBeforeLifecycleEndLocations()`：遇到 `ASTEST_END_*` 后，继续向上扫描当前宏作用域，跟踪 `{`/`}` 深度，而不是在第一条非空行处停止。 2. 将“违规”定义为：在与 `ASTEST_END_*` 对应的生命周期块内，存在任何位于尾部之前的 `return ...;`，无论它是否被单行 `if`、额外花括号或条件块包裹。 3. 为 validation 文件新增最小样例源，至少覆盖三种场景：直接 `return`、`if (...) { return ...; }`、合法的 `ASTEST_END_*` 后返回；让扫描逻辑对前两者报错、对后一者放行。 4. 若继续用文本扫描而不是语法分析，至少要先剥离行尾注释并忽略纯 `}` / `);` / `else` 行，避免它们提前终止回溯。 5. 在 `TESTING_GUIDE.md` 或 `README_MACROS.md` 中补充说明：validation 现在会检查整个生命周期块，不再只是检查 `ASTEST_END_*` 的上一行。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md` |
| 预估工作量 | S |
| 风险 | 如果实现成过度宽松的文本回溯，可能把注释中的 `return` 或 lambda 内局部 return 误报为违规；需要通过样例覆盖把 scope 边界和注释过滤锁死。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 为 validation 扫描新增合成样例，验证 `if (...) { return false; }` 这种当前漏报模式会被抓到。 2. 回归 `Angelscript.TestModule.Validation.LifecycleEndPlacement`，确认现有合法用例不误报。 3. 人工在一份临时测试文件里插入带花括号包裹的 terminal return，确认 validation 会直接报出具体文件和行号。 |

### Issue-52：debugger 自测与 smoke/breakpoint/stepping 用例直接依赖外部 debuggable production engine，测试套件无法自举

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`<br>`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 行号 | 469-500; 16-26; 19-30; 17-28 |
| 问题 | `ProductionDebuggerHelperPrefersDebuggableEngineOverScopedTestEngine`、`Debugger.Smoke.Handshake`、`Debugger.Breakpoint.*` 和 `Debugger.Stepping.*` 都先调用 `TryGetRunningProductionDebuggerEngine()`，然后在返回空指针时直接 `TestNotNull(..., ExistingEngine)` 失败。也就是说，这些 automation test 自己并不会创建带 `DebugServer` 的 engine，而是假定 editor automation 进程里已经存在一个可附着的 production debugger engine。 |
| 根因 | 当前 debugger 测试把“如果已有 production debugger engine，应优先附着它”与“测试本身如何建立 debuggable engine 前置条件”混成了一件事，导致 session fixture 完全依赖外部运行环境。 |
| 影响 | 这组 debugger 测试在本地、CI 或无现成 subsystem-owned engine 的 editor automation 进程里会直接失败，结果取决于运行顺序和环境而不是代码本身；同时一旦按已记录方案收紧 `TryGetRunningProductionDebuggerEngine()`，这些测试会更频繁地暴露出“没有自建前置状态”的问题。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 debugger automation 拆成“自建 owned debugger fixture 的协议/行为回归”和“显式环境前置的 production-attach 回归”两条路径，默认测试必须可自举。 |
| 具体步骤 | 1. 在 `Shared` 层新增 `FAngelscriptOwnedDebuggerFixture` 或等价 helper：创建 test-owned full engine、配置唯一 `DebugServerPort`、建立 `FAngelscriptEngineScope`，并在析构时负责 session shutdown、module cleanup 与 engine 销毁。 2. 将 `AngelscriptDebuggerSmokeTests.cpp`、`AngelscriptDebuggerBreakpointTests.cpp`、`AngelscriptDebuggerSteppingTests.cpp` 的默认路径改为使用该 owned fixture，而不是把 `SessionConfig.ExistingEngine` 直接绑到 `TryGetRunningProductionDebuggerEngine()`。 3. 保留一条单独的 environment-specific 回归来验证“有真实 production debugger engine 时 helper 会优先选择它”，但这条测试必须先显式判断环境是否存在目标 engine；若不存在，则改成 `AddWarning`/skip 语义，而不是普通失败。 4. 将 `Shared/AngelscriptTestEngineHelperTests.cpp:469-500` 的 helper 自测改成两步：先用 owned debugger fixture 构造一个明确可调试的 engine，再验证 shared test engine 不会抢占它；不要继续把外部 editor 状态当作测试前提。 5. 在 `TESTING_GUIDE.md` 或 debugger fixture 注释中写明：debugger automation 默认必须自建 engine，只有明确标注为 production-attach 的测试才允许依赖外部 engine。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 自建 debugger fixture 会增加端口分配和 session teardown 复杂度；若 fixture 没有统一回收 debug server，容易把端口占用和残留 session 变成新的 flaky 来源。 |
| 前置依赖 | 建议结合已记录的 debugger-engine 选择收口一起做，避免新 fixture 仍然误复用 ambient shared engine。 |
| 验证方式 | 1. 在没有现成 production debugger engine 的 editor automation 进程里直接运行 `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Breakpoint.*`、`Stepping.*`，确认它们仍可通过或被显式 skip，而不是因为空 `ExistingEngine` 失败。 2. 新增 helper 自测，验证 owned debugger fixture 与 shared test engine 指针不同，且 `TryGetRunningProductionDebuggerEngine()` 不再是唯一前提。 3. 在同一轮 automation 中先跑 shared engine 用例再跑 debugger 用例，确认 debugger session 不再依赖前序是否碰巧留下 debuggable engine。 |

### Issue-53：`Learning.Runtime.CompilerPipeline` 把 annotated generated class 的残留可见性固化成通过条件，阻碍 annotated teardown 收口

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp` |
| 行号 | 117-127, 161-180, 207-224 |
| 问题 | `FAngelscriptLearningCompilerTraceTest` 在 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 上执行 annotated compile，只在 `ON_SCOPE_EXIT` 中对 4 个模块名调用 `Engine.DiscardModule(...)`，没有 reset shared engine，也没有使用 annotated fixture。随后该测试立刻调用 `FindGeneratedClass(&Engine, TEXT("ULearningCompilerTraceCarrier"))`，并把“generated class 仍然可见”写成 trace 文案和 `TestNotNull` 通过条件。也就是说，这条 learning trace 当前把 annotated compile 后的 generated symbol 残留当成了期望行为。 |
| 根因 | learning trace 想展示“annotated compile 会生成 Unreal class”，但没有把“编译期可见”与“测试退出后必须 cleanup”拆成两个阶段，最终把中间态泄漏直接固化成了最终断言。 |
| 影响 | 一旦按既有规划为 annotated 模块引入正确的 fixture/reset teardown，这条 learning trace 会反过来阻止修复，因为它当前显式要求 generated class 在 shared test 退出前后继续可见；同时它也继续把 detached `UASClass`/`UASFunction` 残留留给后续 shared 用例，放大顺序相关污染。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 learning trace 拆成“fixture 内观察 annotated compile 产物”和“fixture 外验证 cleanup”两个阶段，不再把 leaked symbol 可见性当成成功标准。 |
| 具体步骤 | 1. 把 `LearningCompilerTraceTest` 的 annotated 场景迁移到显式 annotated fixture：在 fixture 生命周期内记录 `CompileResult`、`ModuleNames`、`FindGeneratedClass()` 命中情况，并把这些信息写入 learning trace。 2. fixture 析构时执行与 annotated helper 一致的 teardown（至少 `DiscardModule()` + shared reset 或 owned full engine 销毁），然后在 fixture 外新增一步 trace/断言：`FindGeneratedClass(&Engine, TEXT("ULearningCompilerTraceCarrier"))` 必须返回空。 3. 将当前 `GeneratedClassVisibility` 文案改成两条事件：一条描述“compile 阶段生成类可被发现”，另一条描述“teardown 后生成类已被回收”，避免 trace 继续传播错误 contract。 4. 如果 learning trace 确实需要给后续 runtime 步骤消费这个 class，就把消费动作放进同一个 fixture 生命周期内完成；不要依赖测试结束后 shared engine 里继续挂着 generated class。 5. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 或 learning 目录补一条对应回归，明确 annotated learning fixture 不得把 generated symbol 泄漏到下一条 shared 用例。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 如果 learning trace 当前被其它人当成“生成类可跨测试继续可见”的示例，修复后会暴露这层误解；但这类跨测试残留本身就不应成为基础设施 contract。 |
| 前置依赖 | 建议结合已记录的 annotated teardown 收口一起推进，避免同一批调用点重复迁移。 |
| 验证方式 | 1. 运行 `Angelscript.TestModule.Learning.Runtime.CompilerPipeline`，确认 trace 同时记录“compile 阶段可见”和“teardown 后不可见”两条事件。 2. 在该用例后串行运行 `Shared.EngineHelper.GeneratedSymbolLookup` 或其它 annotated shared 测试，确认不会再命中 `ULearningCompilerTraceCarrier` 的旧壳对象。 3. 人工检查 trace 文本，确认不再出现 “generated class visible for later runtime tests” 这种把泄漏写成正向契约的描述。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-52 | Architecture | 先把 debugger 测试改成可自举 fixture，避免整组 automation 继续依赖外部 editor 状态 |
| P1 | Issue-53 | Architecture | 紧随其后，收掉 learning trace 中把 annotated 残留写成成功条件的反向契约 |
| P2 | Issue-51 | Defect | 在宏 validation 收口时一并修复，补上 lifecycle end placement 的漏报路径 |

---

## 发现与方案 (2026-04-08 18:56)

### Issue-54：`FAngelscriptEngineScope` 带 `WorldContextObject` 的退出路径会丢掉进入前的 ambient world context

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | 287-295, 437-453, 491-512; 648-651; 645-677 |
| 问题 | `FAngelscriptEngineScope` 在带 `InWorldContext` 构造时会保存 `PreviousWorldContext = GAmbientWorldContext`，但 `Reset()` 退出时完全没有使用这个字段，而是在 `Pop(Engine)` 之后无条件调用 `SyncAmbientWorldContextFromCurrentEngine()`。`SyncAmbientWorldContextFromCurrentEngine()` 在当前栈已空时会直接把 ambient world context 置空。也就是说，只要进入 scope 前 ambient world context 非空、退出后又没有外层 current engine，这条 scope 就会把进入前的 ambient context 丢掉。现有自测 `EngineScopeRestoresWorldContextAndCurrentEngine` 只覆盖 `PreviousWorldContext` 为空的基线，因此这条回归当前不会暴露。 |
| 根因 | runtime 在 `FAngelscriptEngineScope` 中同时维护了 `PreviousEngineWorldContext` 和 `PreviousWorldContext` 两份状态，但退出路径只恢复 engine 内部字段，没有恢复“无 current engine 时应回退到哪个 ambient world context”的外层语义。 |
| 影响 | 任何测试或 helper 只要在已有 ambient world context 的环境里短暂进入 `FAngelscriptEngineScope(Engine, SomeObject)`，退出后都可能把 ambient world context 直接清成 `nullptr`；随后依赖 `GetAmbientWorldContext()` / `TryGetCurrentWorldContextObject()` 的 subsystem、timer、world bind 会在顺序变化时解析到空上下文，形成难以复现的 world-context flaky。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 修正 `FAngelscriptEngineScope` 的 world-context 恢复逻辑：有外层 engine 时同步外层 engine，没有外层 engine 时恢复进入前的 ambient world context。 |
| 具体步骤 | 1. 在 `FAngelscriptEngineScope::Reset()` 中保留现有 `Engine->WorldContextObject = PreviousEngineWorldContext`，但在 `Pop(Engine)` 后改成分支恢复：若 `FAngelscriptEngine::TryGetCurrentEngine()!=nullptr`，继续 `SyncAmbientWorldContextFromCurrentEngine()`；若当前已无 engine 且 `bChangedWorldContext==true`，则显式把 ambient world context 恢复为 `PreviousWorldContext`，不要直接清空。 2. 为避免直接依赖匿名静态函数，给 runtime 增加一个小型恢复 seam，例如 `RestoreAmbientWorldContextForTesting(UObject*)` 或复用 `AssignWorldContext()` 的“无 current engine 时只写 ambient”路径，但必须保证不会把 `PreviousWorldContext` 错写回已经切换掉的 engine。 3. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 增加非空基线回归：先用 `FScopedTestWorldContextScope` 或等价手段安装一个 `PreviousWorldContext`，再进入 `FAngelscriptEngineScope(*Engine, DummyContext)`，退出后断言 `GetAmbientWorldContext()` 恢复为原对象而不是 `nullptr`。 4. 再补一条嵌套回归：outer 只有 ambient world context、没有 current engine，inner 为 `FAngelscriptEngineScope(*Engine, DummyContext)`；离开 inner 后应恢复 outer ambient，随后离开 outer scope 才回到空。 5. 检查并回归所有依赖 world-context 恢复的 helper 测试，至少覆盖 `EngineScopeRestoresWorldContextAndCurrentEngine` 与 world/subsystem 绑定相关的代表性场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果恢复分支写错时机，可能把旧 ambient world context 覆盖到仍然活着的外层 engine 上；需要以“当前是否仍有 active engine”为条件区分 ambient-only 与 engine-backed 两条恢复路径。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增非空 `PreviousWorldContext` 回归，确认 `FAngelscriptEngineScope(..., DummyContext)` 退出后 ambient world context 恢复到进入前对象。 2. 回归 `Angelscript.TestModule.Shared.EngineHelper.EngineScopeRestoresWorldContextAndCurrentEngine`，确认现有空基线行为不回退。 3. 串行运行一条安装 ambient world context 的 helper 测试后接一条依赖 `RequireRunningProductionEngine()` / subsystem world 的测试，确认后者不再因前者把 ambient world context 清空而失败。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-54 | Defect | 先修复 world-context 恢复，再扩展相关 helper 回归，避免更多世界相关测试继续建立在错误恢复语义上 |

---

## 发现与方案 (2026-04-08 18:57)

### Issue-55：`FAngelscriptGameThreadScopeWorldContext` 会把进入前的 world context 写回“退出时的当前 engine”，而不是写回进入时被修改的对象

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | 710-724; 746-753, 718-739; 93-104; 434-485; 625-643 |
| 问题 | `FAngelscriptGameThreadScopeWorldContext` 构造时只保存 `PreviousWorldContext`，析构时直接调用 `FAngelscriptEngine::AssignWorldContext(PreviousWorldContext)`。而 `AssignWorldContext()` 的实现是：如果当前存在 engine，就把 `CurrentEngine->WorldContextObject` 直接写成传入对象，再同步 ambient world context。也就是说，这个 scope 根本没有记住“进入时是哪台 engine 被改过”；如果 scope 生命周期内 current engine 发生切换，或进入时没有 engine、退出时却有 engine，析构就会把旧 world context 写回错误的 engine。当前测试只覆盖“没有 current engine 的纯 ambient world-context scope”，没有覆盖 engine handoff 场景。 |
| 根因 | world-context-only scope 采用了 ambient-only 的最小实现，但底层恢复 API 却隐式带有“修改当前 engine 的 `WorldContextObject`”副作用，导致 scope 的恢复目标取决于退出时的上下文，而不是进入时的所有权。 |
| 影响 | 一旦测试 helper、debugger session 或未来脚本绑定在 `FScopedTestWorldContextScope` 生命周期内切换了 current engine，退出时就可能把前一个 world context 注入到一台无关 engine 的 `WorldContextObject` 上；后续 `TryGetCurrentWorldContextObject()`、timer/subsystem/world bind 解析到的上下文将取决于嵌套顺序，而不是显式传入的 world/object。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 world-context-only scope 记录进入时的 engine/ambient 状态，并在退出时按进入时的所有权恢复，而不是借助 `AssignWorldContext()` 去猜“当前是谁”。 |
| 具体步骤 | 1. 重构 `FAngelscriptGameThreadScopeWorldContext`：构造时同时保存 `PreviousCurrentEngine = FAngelscriptEngine::TryGetCurrentEngine()`、`PreviousWorldContext = FAngelscriptEngine::GetAmbientWorldContext()`，以及 `PreviousEngineWorldContext = PreviousCurrentEngine ? PreviousCurrentEngine->GetCurrentWorldContextObject() : nullptr`。 2. 析构时分三步恢复：先判断进入时是否存在 `PreviousCurrentEngine`；若存在，则直接恢复那台 engine 的 `WorldContextObject = PreviousEngineWorldContext`；随后根据当前栈状态设置 ambient world context，优先恢复仍然活着的 outer/current engine，否则恢复 `PreviousWorldContext`。不要再直接调用会修改“当前 engine”的 `AssignWorldContext()`。 3. 若该 runtime 类型不适合承担复杂恢复语义，就在 `Shared` 层新增测试专用 `FScopedTestWorldContextFixture`，并将 `FScopedTestWorldContextScope` 改为使用新 fixture；同时限制旧的 `FAngelscriptGameThreadScopeWorldContext` 只用于无 current engine 的场景。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 增加嵌套回归：进入 scope 时没有 engine，scope 内创建一个 `FAngelscriptEngineScope(*Engine)` 后退出 world-context scope，断言 `Engine->GetCurrentWorldContextObject()` 不会被错误写成旧 ambient world。 5. 再补一条“entry engine A / exit engine B”回归：在 world-context scope 内切换 current engine，退出后断言恢复的是 entry engine A 的状态，engine B 的 `WorldContextObject` 不被污染。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | M |
| 风险 | runtime 里也绑定暴露了 `FAngelscriptGameThreadScopeWorldContext`；如果直接收紧语义或加断言，可能影响少量依赖旧行为的脚本/测试，需要先用回归把“进入时所有权决定恢复目标”的新 contract 固定下来。 |
| 前置依赖 | 建议先完成 Issue-54，统一 `FAngelscriptEngineScope` 与 world-context-only scope 的恢复语义。 |
| 验证方式 | 1. 新增 “entry 无 engine / exit 有 engine” 与 “entry engine A / exit engine B” 两条回归，确认退出后不会把旧 world context 写进错误 engine。 2. 回归 `Angelscript.TestModule.Shared.EngineHelper.WorldContextScopeRestoresPreviousContext`，确认原有无-engine 场景继续通过。 3. 在 `ExecuteGeneratedIntEventOnGameThread` 的 `Engine == nullptr` 路径前后插入 world-context 断言，确认 helper 不会因为内部 engine handoff 污染其它测试的 `WorldContextObject`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-55 | Defect | 在 world-context 恢复链路继续扩散之前修复，先把 world-only scope 从“退出时猜当前 engine”改成 owner-aware 恢复 |

---

## 发现与方案 (2026-04-08 18:58)

### Issue-56：world-context 自测只覆盖空基线 happy path，`FAngelscriptEngineScope`/`FScopedTestWorldContextScope` 的恢复矩阵没有守门回归

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | 625-677; 93-104; 437-453, 491-512; 710-724 |
| 问题 | 当前与 world-context 恢复相关的 helper 自测只有两条：`WorldContextScopeRestoresPreviousContext` 和 `EngineScopeRestoresWorldContextAndCurrentEngine`。前者只验证“无 current engine、`PreviousWorldContext` 为 `nullptr`”的纯 ambient 场景；后者只验证“创建 isolated engine + `DummyContext` + 退出后恢复到空”的简单路径。它们都没有覆盖非空 `PreviousWorldContext`、entry/exit engine 变化、outer ambient + inner engine scope 嵌套、outer engine A + inner engine B 切换这几类关键状态组合。结果是，Issue-54 和 Issue-55 这种 world-context 恢复缺陷可以长期存在而测试仍保持绿色。 |
| 根因 | TestInfrastructure 为 engine/world-context scope 建了“存在回归测试”的表象，但测试设计只挑了最容易通过的空基线路径，没有把恢复语义拆成状态矩阵，也没有把 helper 与 runtime scope 的所有权约束固化成 contract tests。 |
| 影响 | world-context 恢复逻辑的回归目前缺少直接守门网；后续无论是修复 Issue-54/55，还是继续重构 `FAngelscriptEngineScope` / `FScopedTestWorldContextScope`，都容易在另一个状态组合上再次破坏恢复语义而不被 CI 发现。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把现有零散的 happy-path 测试收敛为 owner-aware 的 world-context 恢复矩阵，覆盖 ambient-only、engine-backed 和 engine-handoff 三类状态。 |
| 具体步骤 | 1. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 提取小型测试夹具，例如 `FWorldContextRestoreMatrixFixture`，统一创建 `DummyContextA` / `DummyContextB`、可选 outer engine、以及读取 `GetAmbientWorldContext()` / `TryGetCurrentWorldContextObject()` / `Engine->GetCurrentWorldContextObject()` 的断言 helper。 2. 基于该 fixture 至少补四条 matrix 回归：`AmbientOnly_NonNullPreviousContext_RestoredAfterEngineScope`、`AmbientOnly_NonNullPreviousContext_RestoredAfterWorldContextScope`、`EntryEngineA_ExitEngineB_DoesNotPolluteEngineB`、`OuterAmbient_NoCurrentEngine_AfterInnerEngineScope_RestoresAmbient`。 3. 将现有 `WorldContextScopeRestoresPreviousContext` 与 `EngineScopeRestoresWorldContextAndCurrentEngine` 保留为最小 smoke tests，但它们的核心断言迁移到 matrix fixture，避免继续只覆盖空基线。 4. 在 `TESTING_GUIDE.md` 写明 world-context contract checklist：新增或修改 `FAngelscriptEngineScope` / `FScopedTestWorldContextScope` / `FAngelscriptGameThreadScopeWorldContext` 时，必须同时覆盖“previous ambient non-null”“entry/exit engine 不同”“outer engine/ambient 嵌套”三项。 5. 若后续将 Issue-54/55 修复拆到不同提交，先落测试矩阵并让其在当前实现下稳定重现，再据此推动实现修复，避免再次回到只靠手工推理验证恢复语义。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | S |
| 风险 | matrix 回归会暴露当前实现里更多历史缺口，短期内可能让 world-context 相关测试从 2 条增长到 6-8 条；但这是把恢复 contract 显式化所必须付出的成本。 |
| 前置依赖 | 无；建议与 Issue-54、Issue-55 同步推进，先写失败回归再改实现。 |
| 验证方式 | 1. 新增 matrix 回归后，在当前实现上应至少复现非空 ambient / engine handoff 的失败。 2. 修复 Issue-54、Issue-55 后重新运行 `Angelscript.TestModule.Shared.EngineHelper.WorldContextScopeRestoresPreviousContext`、`EngineScopeRestoresWorldContextAndCurrentEngine` 以及新增 matrix，用例应全部转绿。 3. 人工在 future 变更里修改 scope 恢复顺序，确认 matrix 回归会第一时间报出具体失配场景，而不是只剩下泛化的空指针/顺序 flaky。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-54 | Defect | 先修复 `FAngelscriptEngineScope` 对非空 ambient world context 的丢失问题 |
| P1 | Issue-55 | Defect | 随后收口 `FAngelscriptGameThreadScopeWorldContext` 的 owner-aware 恢复，避免错写其它 engine |
| P2 | Issue-56 | Architecture | 与前两条同步补 matrix 回归，把 world-context 恢复 contract 固化进测试基础设施 |

---

## 发现与方案 (2026-04-08 19:04)

### Issue-57：`CompileModuleWithSummary()` 的 preprocessor 路径没有绑定目标 engine scope，导致 flags/diagnostics 依赖 ambient engine

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | 50-77, 285-316; 38-60, 212-214, 4249-4294; 718-765; 710-744 |
| 问题 | `CompileModuleWithSummary()` 会先调用 `BuildModulesForSummary()`，而 `BuildModulesForSummary()` 在 `bUsePreprocessor == true` 时直接构造 `FAngelscriptPreprocessor` 并执行 `Preprocess()`，整个阶段没有建立 `FAngelscriptEngineScope(*Engine)`。但 preprocessor 构造和报错都显式依赖当前 engine：构造函数用 `ShouldUseEditorScriptsForCurrentContext()` / `IsSimulatingCookedForCurrentContext()` 初始化 flags，`Preprocess()` 用 `FAngelscriptEngine::Get().ConfigSettings` 取配置，`LineError()` / `MacroError()` 又通过 `FAngelscriptEngine::Get().ScriptCompileError(...)` 写 diagnostics。`FAngelscriptEngine::Get()` 在没有 current engine 时会直接 `checkf`，有 outer current engine 时则把数据写到外层 engine 上。现有 `CompileSummaryDiagnosticCapture` 自测只在 shared persistent scope 下运行，正好掩盖了这条 ambient-state 依赖。 |
| 根因 | summary helper 把“准备待编译 modules”当成与 engine 无关的预处理步骤，但 runtime preprocessor 的实现并不是纯函数，而是显式读取 current-engine flags/config 并把诊断写回当前 engine。 |
| 影响 | 对非 current-engine 的 isolated/prod-like engine 调用 `CompileModuleWithSummary()` 时，preprocessor 可能读取错误的 editor/cooked flag 和 `ConfigSettings`，并把 diagnostics 污染到外层 ambient engine；如果当时没有 current engine，helper 还可能在 `FAngelscriptEngine::Get()` 处直接断言。learning trace、hot-reload analysis 和后续任何想在隔离 engine 上采集 compile summary 的测试，都会因此变成顺序相关或环境相关。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 summary helper 的 preprocessor 阶段显式绑定目标 engine，并逐步消除 preprocessor 对 ambient current-engine 的隐式依赖。 |
| 具体步骤 | 1. 将 `BuildModulesForSummary()` 改为接收 `FAngelscriptEngine&`，并在 `FAngelscriptPreprocessor` 构造与 `Preprocess()` 调用外层包裹 `FAngelscriptEngineScope SummaryScope(Engine)`；不要再让 summary 的 prepare 阶段裸跑在 ambient context 上。 2. 进一步把 runtime preprocessor 的 engine 依赖显式化：优先新增 `FAngelscriptPreprocessor(FAngelscriptEngine&)` 或等价 seam，把 flags/config/diagnostics sink 从 `FAngelscriptEngine::Get()` 收敛到传入的目标 engine，避免 future helper 再次踩到 current-engine 隐式依赖。 3. 调整 `CompileModuleWithSummary()` 的 diagnostics 收集顺序：即便 `BuildModulesForSummary()` / `Preprocess()` 提前失败，也要保证 `OutSummary.AbsoluteFilenames` 至少包含当前输入文件，以便失败 summary 能稳定带回对应文件诊断。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 新增两条回归：一条先建立 outer shared current engine，再对独立 isolated engine 调 `CompileModuleWithSummary(..., true)` 并制造 `#if UNKNOWN_FLAG` 之类的 preprocessor 错误，断言 summary diagnostics 只落在 isolated engine 且 outer engine 诊断不变；另一条在无 outer current engine 的基线上调用同样路径，断言 helper 返回失败 summary 而不是触发 `FAngelscriptEngine::Get()` 断言。 5. 回归 `Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp` 与 `HotReload/AngelscriptHotReloadAnalysisTests.cpp` 中使用 summary/analyze helper 的代表性场景，确认它们在 shared engine 存在与不存在两种顺序下都得到一致 diagnostics。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只在 helper 外层补一个 `EngineScope`，但不收口 preprocessor 内部对 `FAngelscriptEngine::Get()` 的依赖，未来其它 helper 仍可能在类似路径上复发；需要至少留出显式 engine seam，避免问题继续靠调用约定维持。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 isolated-engine preprocessor failure 回归，确认 diagnostics 进入目标 engine summary，而不是外层 shared/current engine。 2. 在没有 outer current engine 的情况下运行新的 summary 回归，确认 helper 返回 `CompileResult=Error` 和非空 diagnostics，而不是触发断言。 3. 串行运行一条 shared compile 测试后接 `Learning.CompilerTrace` / `HotReload.Analysis` 代表性用例，再交换顺序重跑，确认 summary 结果与 diagnostics 文件集合一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-57 | Defect | 优先修复 summary helper 的 target-engine scope 绑定，再扩展基于 compile trace 的隔离测试 |

---

## 发现与方案 (2026-04-08 23:42)

### Issue-58：coverage report writer 无法可靠生成嵌套路径产物，子目录失败还会被静默吞掉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/CoverageReportGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp` |
| 行号 | 25-40, 199-257; 178, 255, 328, 393; 65-76 |
| 问题 | `WriteFileCoverageReportHtml()` 直接把输出文件拼成 `OutputDir/<RelativeFilename>.as.html` 并立刻 `SaveStringToFile()`，`WriteCoverageSummaryHtml()` 也直接递归到 `OutputDir/<Child>/index.html` 写盘，但两个路径都没有先创建父目录。更糟的是，`WriteCoverageSummaryHtml()` 在 210-211 行递归子节点后完全忽略返回值，子目录写盘失败不会向上传播。与此同时，coverage 示例测试明确使用了带多级目录的相对路径，例如 `Documents/Plans/Plan_ScriptExamplesExpansion/Coverage/Example_Coverage_Actor.as`。这意味着一旦打开 code coverage，leaf html 和子目录 `index.html` 会尝试写到多级目录下；目录不存在时子路径写盘失败，但顶层 summary 仍可能继续生成，看起来像“报告成功了”。 |
| 根因 | coverage 产物生成器把 `RelativeFilename` 当成了平面文件名处理，既没有建立输出目录树，也没有把递归写盘视为必须成功的 contract。 |
| 影响 | 对带嵌套路径的脚本模块，`Saved/CodeCoverage` 下会缺失 `.as.html` 文件或子目录 `index.html`；调用方只能从日志里看到零散 `Failed writing ...`，而顶层索引/JSON 仍可能存在，形成不完整但表面成功的 coverage 报告。对 `ScriptExamples.Coverage.*` 这类深路径脚本尤其明显。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 coverage report writer 对每个目标文件显式创建父目录，并把递归失败变成整体失败，而不是继续输出残缺报告。 |
| 具体步骤 | 1. 在 `WriteFileCoverageReportHtml()` 里于首次 `SaveStringToFile()` 前调用 `IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputFile), true)`，确保 `OutputDir/<RelativeFilename>` 的父目录存在。 2. 在 `WriteCoverageSummaryHtml()` 中，对 `OutputFile = OutputDir/index.html` 同样先创建目录；对子节点递归改成 `bOk &= WriteCoverageSummaryHtml(...)` 或等价累计返回值，不能再丢弃子节点失败。 3. 让 `FAngelscriptCodeCoverage::StopRecordingAndWriteReport()` 检查 `WriteReportHtml()` 和 `WriteCoverageSummaries()` 的返回值，并在失败时输出一条聚合错误，避免只留下分散日志。 4. 在 `AngelscriptCodeCoverageTests.cpp` 新增确定性的 nested-path 回归：构造 `RelativeFilename = "Nested/Deeper/Test.as"` 的 `FLineCoverage` / `FCoverageNode`，断言会同时生成 `Nested/Deeper/Test.as.html`、`Nested/index.html`、`Nested/Deeper/index.html`。 5. 再加一条负向回归验证递归失败传播：通过注入不可写目录或删除父目录后直接调用 writer，断言顶层返回 `false`，而不是只留下部分产物。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/CoverageReportGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只修 leaf html 不修 summary 递归，仍会留下“文件页存在但目录索引缺失”的半修状态；需要把目录创建与失败传播一起收口。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行新增 nested-path coverage 回归，确认多级目录下的 `.as.html` 与 `index.html` 都被生成。 2. 在启用 coverage 的前提下运行 `Angelscript.TestModule.ScriptExamples.Coverage.*`，检查 `Saved/CodeCoverage/Documents/Plans/...` 下叶子页和目录索引都存在。 3. 人工制造不可写输出目录，确认 writer 返回失败且顶层不会再伪装成成功。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-58 | Defect | 先修复 coverage 报告写盘链路，避免当前 artifact 在嵌套路径下继续输出残缺结果 |

---

## 发现与方案 (2026-04-08 23:42)

### Issue-59：coverage recorder 在同名脚本重编译时会清空既有 hit counts，hot-reload 覆盖率被“最后一次 compile”覆盖

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 行号 | 75-83, 107-126; 84-106; 367-380; 289-296 |
| 问题 | `MapExecutableLines()` 只按 `Module.Code[0].RelativeFilename` 建立 coverage entry，并在每次重建映射时无条件执行 `HitCounts.Empty()`。`HitLine()` 之后也只回写到同一个文件名 key。与此同时，hot-reload 测试明确会在同一条 automation run 内对同一个文件名重复编译，例如 `SoftReloadMod.as` 先 `CompileAnnotatedModuleFromMemory()` 再 `CompileModuleWithResult()`，`HotReloadModifyLookupFlow.as` 先编译 V1 再 soft reload V2，`HotReloadPerformanceBurst.as` 更是在同一次性能测试里连续编译 V1/V2/V3/V2。已验证事实：同名文件 remap 时旧 hit map 会被清空。推断：如果这些测试在启用 code coverage 的同一轮里执行，重编译前记录到的命中会被后一次 `MapExecutableLines()` 擦掉，最终报告只剩最后一次 compile 对应的命中。 |
| 根因 | coverage recorder 把“同一路径文件在一个 test run 内只会 map 一次”当成默认前提，没有为 hot reload / repeated compile 场景定义稳定的累积或版本化 contract。 |
| 影响 | hot-reload、reload-analysis、performance burst 等测试会系统性低报 coverage；覆盖率结果不再代表“整轮自动化执行过哪些代码”，而是偏向最后一次 remap 的脚本版本。顺序不同或是否发生 reload，会直接改变同一文件的最终 hit 数。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为同名脚本的 repeated compile 定义显式 coverage contract，避免当前无声的 “last compile wins” 覆盖行为。 |
| 具体步骤 | 1. 在 `FAngelscriptCodeCoverage` 中把当前单一 `RelativeFilename -> FLineCoverage` 模型升级为“可重映射”的结构：至少额外记录 `CodeHash` 或 compile generation，区分同一文件名的不同版本。 2. 若产品目标是“整轮运行聚合 coverage”，则在 remap 时基于旧 map 和新 map 做保守合并：保留仍存在的行号 hit count，新出现的行号从 0 开始，删除的旧行号移到历史 generation 而不是直接丢弃。 3. 若产品目标是“每个脚本版本独立 coverage”，则将 writer 输出改成带 generation/hash 的叶子文件名，并在 summary 中把同一路径的多代版本都列出来；无论选哪条，都要把 contract 写进 `AngelscriptCodeCoverage.h` 注释。 4. 在 `AngelscriptCodeCoverageTests.cpp` 新增 deterministic 回归：构造同一 `RelativeFilename` 的 V1/V2 两份 module desc，先 map+hit V1，再 remap V2，断言 V1 的命中不会被静默归零；对应地校验聚合模式或 versioned mode 的输出。 5. 再加一条端到端回归，复用现有 hot-reload 样式输入（至少 `SoftReloadMod.as` 或 `HotReloadPerformanceBurst.as`），在启用 coverage 的 engine 上跑一次重编译流程，确认最终 report 会稳定反映 pre-reload 与 post-reload 的执行，而不是只留下最后一步。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/CoverageReportGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp` |
| 预估工作量 | L |
| 风险 | 如果直接把旧 hit count 生硬地并回新版本行号，可能把已删除/重排的代码错误归到新源码上；因此必须先选定“聚合”还是“版本化” contract，再实现对应的数据结构与报告格式。 |
| 前置依赖 | 建议先完成 Issue-58，先确保 writer 能稳定输出多级路径与多版本产物。 |
| 验证方式 | 1. 新增 repeated-compile coverage 回归，确认 remap 后旧 hit 不会无声丢失。 2. 在启用 coverage 的前提下运行 `Angelscript.TestModule.HotReload.*` 中至少一条 soft reload 和一条 burst 用例，检查相同文件名的报告不再只反映最后一步 compile。 3. 人工对比同一测试在“仅执行 V1”与“执行 V1 后再 reload V2”两种顺序下的 coverage 输出，确认前者命中不会在后者中凭空消失。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-59 | Defect | 在 coverage writer 稳定后优先收口 remap contract，避免 hot-reload 路径继续系统性低报覆盖率 |

---

## 发现与方案 (2026-04-08 23:42)

### Issue-60：`AngelscriptCodeCoverageTests` 仍是环境依赖型 smoke test，缺少 nested-path 与 repeated-compile 的确定性守门回归

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/CoverageReportGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 行号 | 17-28, 31-76, 103-181; 25-40, 199-257; 178, 255, 328, 393; 289-296 |
| 问题 | 当前 `FAngelscriptCodeCoverageTests0` 直接依赖 `FAngelscriptEngine::Get()` 和 `Manager.GetActiveModules()` 作为输入；如果找不到合适模块，它会在 52-55 行直接 `AddInfo(...)` 后返回 `true`。即使运行成功，它也只检查根 `index.html` 与每个 module 的 leaf html 是否存在，并没有显式构造嵌套相对路径、子目录 summary、或同名文件 repeated compile 的场景。其余 `Tests1-4` 也都只覆盖 `FLineCoverage` 计数和 `FCoverageNode` 汇总数学，没有覆盖真正的 writer/recorder contract。与此同时，仓库里已经有两类真实调用面：coverage 示例测试使用 `Documents/Plans/...` 这种深路径脚本，hot-reload 性能测试会对同一个 `HotReloadPerformanceBurst.as` 连续编译多次。也就是说，coverage 基础设施的高风险路径已经存在于仓库里，但没有任何确定性回归专门守住它们。 |
| 根因 | 现有 coverage 测试偏向“拿当前进程里碰巧活着的模块做 smoke test”，没有建立 test-owned fixture 去显式控制输入文件路径、compile 次数和预期产物。 |
| 影响 | 像 Issue-58 的目录写盘失败、Issue-59 的 remap 覆盖行为这类回归，可以在 coverage 相关自动化测试全部为绿的情况下长期存在；测试要么环境依赖地通过，要么因为无活跃模块直接自跳过，无法充当稳定守门网。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 coverage 自测从 ambient smoke test 改成 fixture 驱动的 contract suite，显式覆盖 writer、summary 和 remap 三类关键路径。 |
| 具体步骤 | 1. 在 `AngelscriptCodeCoverageTests.cpp` 新增 test-owned fixture，直接构造 `FLineCoverage` / `FCoverageNode` 或最小 `FAngelscriptModuleDesc`，不要再依赖 ambient `Manager.GetActiveModules()` 才能测试 writer。 2. 将现有 integration test 拆成至少三条确定性回归：`NestedPathWritesLeafAndIndexes`、`RepeatedCompileDoesNotDropPriorCoverage`、`MissingSourceFileFailsLoudlyOrProducesExpectedError`；每条测试自己创建 temp output dir 和最小输入，不允许再通过“无模块时返回 true”自跳过。 3. 保留现有 `LineCoverageTest*` 和 `ComputeCoverageTest` 作为纯算法单测，但把 `Tests0` 改成真正的 end-to-end contract test，明确断言叶子 html、目录 index、top-level json 以及 repeated-compile 后的输出结构。 4. 在 `AngelscriptTest` 侧补一个 bridge regression：启用 coverage 后运行一条 `ScriptExamples.Coverage.*` 或 `HotReloadPerformanceBurst` 的最小场景，再调用 coverage writer，验证 runtime contract 与高层测试输入保持一致。 5. 在 `TESTING_GUIDE.md` 或 runtime test 注释中写明：coverage 相关改动必须同时更新 unit-level writer tests 和至少一条 end-to-end scenario test，禁止继续只靠 ambient active-module smoke test。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/CoverageReportGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 如果端到端回归直接复用大型 scenario/test world，测试会重新变慢且不稳定；需要把 fixture 控制在最小输入，尽量在 runtime test 层模拟 writer/recorder contract，只保留 1 条轻量级 bridge test。 |
| 前置依赖 | 建议与 Issue-58、Issue-59 同步推进，先写失败回归，再落实现修复。 |
| 验证方式 | 1. 新增 contract suite 后，在当前实现上应能稳定复现 nested-path 或 repeated-compile 的至少一类失败。 2. 修复后重新运行 `Angelscript.CppTests.AngelscriptCodeCoverage.*`，不再依赖 ambient modules，且不会因“无模块”直接跳过关键断言。 3. 额外运行 1 条最小 `ScriptExamples.Coverage` 或 hot-reload bridge test，确认 runtime coverage contract 与高层调用面一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-58 | Defect | 先修 coverage writer 的目录创建与失败传播，确保 artifact 不是残缺成功 |
| P1 | Issue-59 | Defect | 随后定义 repeated-compile 的 coverage contract，避免 hot-reload 路径继续低报覆盖率 |
| P2 | Issue-60 | Architecture | 与前两条同步补确定性 coverage 回归，把 writer/remap contract 固化进测试网 |

---

## 发现与方案 (2026-04-08 23:59)

### Issue-61：production-world 场景测试会在 live actor/component 仍存活时提前 `DiscardModule()`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 行号 | 161-176, 184-190, 239-253, 267-282, 377-390, 399-405; 357-371, 397-403, 423-437, 471-477; 157-188 |
| 问题 | `ScriptExamples.Coverage.*` 与 `DefaultComponent.*` 这类 production-world 场景，都先声明 `FActorTestSpawner Spawner`，后声明 `ON_SCOPE_EXIT { Engine.DiscardModule(...); }`，随后再生成 actor/component 并驱动 `BeginPlay`/`Tick`。C++ 局部对象按逆序析构，因此这些用例会先执行 `DiscardModule()`，再让 `Spawner` 离开作用域；也就是说，module teardown 发生时，world 里的 script actor / component 仍然活着。仓库里同样依赖 `Spawner` 的 `ClassGenerator/AngelscriptScriptClassCreationTests.cpp:157-188` 则采用了相反顺序：先注册 `ON_SCOPE_EXIT`，后创建 `Spawner`，从而保证 world teardown 先发生。 |
| 根因 | 测试模块没有提供统一的 “world/actor teardown must precede module teardown” fixture，调用点只能手写 `Spawner` 与 `ON_SCOPE_EXIT` 的声明顺序；一旦顺序写反，cleanup contract 就会悄悄颠倒。 |
| 影响 | world 销毁、component unregister、actor cleanup 会在 module 已经 `DiscardModule()` 之后发生；这会把 runtime 对象 teardown 暴露给 detached `UASClass` / `UASFunction` 壳对象，放大为顺序相关的 cleanup 失败、generated symbol 残留或后续测试命中 stale class/function。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 production-world 场景的 world 生命周期与 module 生命周期绑定到同一个 fixture，强制先销毁 `Spawner`/world，再执行 `DiscardModule()`。 |
| 具体步骤 | 1. 在 `Examples`/`Component` 公共层新增 `FProductionWorldScriptModuleFixture` 或等价 fixture，内部同时持有 `FAngelscriptEngineScope`、`ModuleName` 与可选 `FActorTestSpawner`。 2. 让 fixture 的析构顺序显式化：先释放或离开 `Spawner`/world 作用域，再执行 `Engine.DiscardModule()`；annotated 模块还要按 Issue-11 的方向补 generated UObject cleanup。 3. 迁移 `AngelscriptScriptExampleCoverageTests.cpp` 与 `AngelscriptComponentScenarioTests.cpp` 中当前把 `Spawner` 声明在 `ON_SCOPE_EXIT` 之前的场景；对暂时不引入 fixture 的函数，至少改成内层 `{ FActorTestSpawner Spawner; ... }` 块包住 runtime world，让 `ON_SCOPE_EXIT` 位于外层。 4. 参照 `ClassGenerator/AngelscriptScriptClassCreationTests.cpp` 已存在的安全顺序，把“先注册 module cleanup、后创建 `Spawner`”固化成模块级约定，而不是靠调用者记忆。 5. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 或 scenario 自测里新增 teardown-order 回归：编译 annotated actor/component，spawn 后直接离开 fixture，断言 world 已先释放，再检查 `DiscardModule()` 后不会留下同模块的 live generated instance。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 预估工作量 | M |
| 风险 | 如果 fixture 过早销毁 `Spawner`，测试体后半段对 actor/component 的断言会失效；需要把 world teardown 严格放在业务断言之后、module cleanup 之前。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 回归 `Angelscript.TestModule.ScriptExamples.Coverage.*` 与 `Angelscript.TestModule.DefaultComponent.*`，确认迁移后功能断言不变。 2. 新增 teardown-order 回归，验证离开测试作用域后不存在 “module 已 discard 但 world 仍持有 live script instance” 的窗口。 3. 串行交错运行一条 production-world 场景测试和一条 `FindGeneratedClass()`/`FindGeneratedFunction()` 测试，确认后者不再命中前者 teardown 留下的 stale symbol。 |

### Issue-62：`CompileScriptModule()` 在公共 helper 层重复压入同一 `FAngelscriptEngineScope`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp` |
| 行号 | 22-35; 165-177, 358-364; 94-103; 170-178 |
| 问题 | `CompileScriptModule()` 在 30 行无条件创建 `FAngelscriptEngineScope EngineScope(Engine)`，随后立刻调用 `CompileAnnotatedModuleFromMemory()`；而后者又在 `PreprocessAndCompile()` 的 176 行再次创建 `FAngelscriptEngineScope(*Engine)`。这意味着 scenario helper 自己就把同一 engine scope 叠了两层。调用点若本来已经在 `ASTEST_BEGIN_SHARE_CLEAN` 或显式 `FAngelscriptEngineScope` 中，例如 `ComponentScenarioTests.cpp:94-103` 与 `ScriptExampleCoverageTests.cpp:170-178`，一次 annotated compile 实际会把同一 engine 连续压栈 2 到 3 次。我执行 `rg -n "CompileScriptModule\\(" Plugins/Angelscript/Source/AngelscriptTest`，当前共有 89 个调用点。 |
| 根因 | scenario helper 与 compile helper 都把“安装 current engine”当成自己的职责，但两层 API 没有约定谁才是 scope owner，导致公共入口把同一语义重复实现。 |
| 影响 | current-engine 栈深度不再代表真实 fixture/engine ownership，而是被 helper 内部实现细节放大；任何依赖 `TryGetCurrentEngine()`、context snapshot/restore、debugger/production probe 或 world-context 恢复的测试，都可能在排障时看到多余的重复 frame，增加 context 泄漏与恢复问题的分析复杂度。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 明确 compile helper 的 scope ownership，只保留一层 `FAngelscriptEngineScope`，禁止公共 helper 再对已经 scoped 的 engine 做重复压栈。 |
| 具体步骤 | 1. 选定单一 ownership 规则：要么 `CompileScriptModule()` 负责安装 scope、`CompileAnnotatedModuleFromMemory()` 提供 `AlreadyScoped` 版本；要么反过来，删除 `CompileScriptModule()` 内部的 `FAngelscriptEngineScope`，完全依赖底层 compile helper。 2. 优先推荐后者：把 `Shared/AngelscriptScenarioTestUtils.h` 的 `CompileScriptModule()` 改成纯 orchestration helper，只负责调用 compile、查找 generated class、组织断言，不再管理 current-engine scope。 3. 为少数确实需要 compile 前后包住同一 outer scope 的调用点，新增显式命名 API（例如 `CompileScriptModuleWithinExistingScope()`），不要继续复用隐式双 scope 的通用入口。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 或 validation 层新增 stack-depth 回归：记录进入 compile 前的 context stack 深度，在 outer `ASTEST_BEGIN_SHARE_CLEAN` 与 outer explicit `FAngelscriptEngineScope` 两种前置下调用 `CompileScriptModule()`，断言 helper 只增加一层而不是两层。 5. 仓库内批量迁移 89 个 `CompileScriptModule()` 调用点后，再把“helper 不得对已 scoped engine 重复 `FAngelscriptEngineScope(Engine)`”写入 `TESTING_GUIDE.md` / `Shared/AngelscriptScenarioTestUtils.h` 注释，避免类似叠层再次扩散。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptScenarioTestUtils.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 如果直接删掉外层 scope，却遗漏了某些 compile 路径在进入 preprocessor 前仍依赖 current engine 的事实，可能暴露已有的 ambient-engine 依赖；需要与 compile helper 自测一起推进，确保只删重复层，不删最后一层。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 stack-depth 回归，验证 `CompileScriptModule()` 在已有 outer scope 的场景下只额外压栈一层。 2. 回归 `Angelscript.TestModule.Component.*`、`ScriptExamples.Coverage.*` 以及任意一条 `CompileScriptModule()` 驱动的 `Interface`/`Actor` 场景测试，确认编译与 generated class 查找结果不变。 3. 再次运行 `rg -n "CompileScriptModule\\("` 的代表性调用点抽样检查，确认不再出现 helper 内部的重复同引擎 scope。 |

### Issue-63：`AngelscriptTest` 模块 shutdown 没有接入任何 test-engine cleanup，缺少最终泄漏兜底

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | 9-16; 81-90, 392-427 |
| 问题 | `AngelscriptTest` 模块当前在 `StartupModule()` / `ShutdownModule()` 中只写日志。与此同时，测试基础设施自己维护了进程级 static shared storage：`GetSharedTestEngineStorage()` 与 `GetSharedTestEngineScopeStorage()` 保存 shared engine 与 persistent scope；`DestroySharedTestEngine()`、`DestroySharedAndStrayGlobalTestEngine()` 则实现了带 `PreReset/PostReset` 日志的显式 teardown。也就是说，模块里已经存在“如何安全收尾测试 engine”的正式 API，但模块 shutdown 完全没有调用它。 |
| 根因 | 测试模块把 shared engine cleanup 视为“每条测试自己负责”的局部动作，没有把 module 生命周期当成最终 ownership boundary，也没有在模块收尾阶段集中检查残留状态。 |
| 影响 | 一旦 automation run 中断、测试早退、模块被热重载，当前唯一的 controlled cleanup 路径就不会执行；shared engine 的 reset 日志、残留 module/symbol 统计以及 stray-global 清理都不会在模块结束时得到最后一次兜底，问题只能留到隐式 static 析构或下一轮测试里以顺序相关方式暴露。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 module shutdown 升级为测试基础设施的最终 cleanup fence，显式清 shared/stray engine，并记录无法收尾的残留状态。 |
| 具体步骤 | 1. 在 `FAngelscriptTestModule::ShutdownModule()` 中调用统一的 teardown 入口，例如 `AngelscriptTestSupport::DestroySharedAndStrayGlobalTestEngine()`；不要继续只写一条 “module shut down” 日志。 2. 在进入 teardown 前补一条结构化泄漏日志：输出当前 shared engine 是否存在、instance id、active modules、detached symbol 统计，便于定位是哪个测试留下了残留。 3. 将 teardown 入口改造成可返回状态的 API；如果因为 live clone 或 ownership 不明而无法安全销毁，shutdown 日志必须明确报告失败原因，而不是静默跳过。 4. 在 Issue-19 暴露 macro-owned `thread_local` storage seam 后，shutdown fence 继续补上 `FULL/CLONE` 宏私有 owner 的集中回收；在那之前至少先收 shared helper 这条主链。 5. 新增一条 module-lifecycle regression：先显式创建 shared engine 并注册模块，再调用新的 module teardown helper，断言 shared storage、persistent scope 与 stray current/global alias 均被清空。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/AngelscriptTestModule.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | S |
| 风险 | 若 ownership 判断仍然沿用当前的 current/global 混用语义，shutdown fence 可能误清理真实 production engine；因此需要与已有 ownership 修正方向一起落地，至少先限制为 shared helper 自己拥有的 engine。 |
| 前置依赖 | 建议先完成 Issue-7 的 shared ownership 判断；要覆盖宏私有 `thread_local` owner 还需 Issue-19 的 storage seam。 |
| 验证方式 | 1. 新增 module-teardown 回归，验证创建 shared engine 后执行 shutdown helper 会清空 shared storage 与 persistent scope。 2. 人工或自动检查 shutdown 日志，确认会输出残留 engine/module/symbol 的清理前状态，而不是只有一条通用日志。 3. 在同一 editor 进程内先运行一批 shared/helper 测试，再触发模块卸载或 teardown helper，确认下一轮测试不再继承上一轮残留的 shared engine。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-61 | Defect | 先修 production-world 场景的 teardown 顺序，避免 live actor/component 在 module 已 discard 后才退出 |
| P2 | Issue-62 | Architecture | 随后收敛 `CompileScriptModule()` 的重复 scope ownership，降低 89 个 scenario 调用点的 context 噪音 |
| P2 | Issue-63 | Architecture | 最后补 module shutdown cleanup fence，把 shared test state 的最终收尾从“靠每条测试自觉”升级为模块级兜底 |

---

## 发现与方案 (2026-04-09 00:13)

### Issue-64：`BindConfig` 测试动态注册的 bind 永久残留在全局注册表中

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` |
| 行号 | 79-82, 272-304, 332-364, 370-417, 422-471; 438-469; 151-154, 161-183 |
| 问题 | `AngelscriptBindConfigTests.cpp` 多条用例会在测试体内构造局部 `FAngelscriptBinds::FBind`，并通过 `MakeUniqueBindTestName()` 为每次运行生成新的 GUID 名称。运行时侧 `FBind` 只有构造函数，没有析构或注销逻辑；`RegisterBinds()` 只是把条目持续 `Add()` 到全局 `GetBindArray()`，`GetAllRegisteredBindNames()` / `GetBindInfoList()` 之后会一直枚举这些历史条目。这意味着测试新增的 bind 在测试结束后不会移除，而是永久污染本进程的 bind 注册表。 |
| 根因 | 测试基础设施缺少“临时 bind 注册”的 owner 模型；当前只能调用产品级 `RegisterBinds()` 追加全局状态，却没有任何 test-only snapshot / truncate / unregister seam。 |
| 影响 | 重复执行 `BindConfig` 用例或在同一 Editor 进程内多轮运行自动化后，bind 名列表、bind info 顺序、state dump 统计以及基于 `GetAllRegisteredBindNames()` 构造的 disabled 集合都会不断膨胀；为规避冲突，测试只能继续依赖 GUID 命名，这会进一步放大全局状态泄漏和排障成本。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为测试层补上可回收的 bind-registration fixture，禁止 `BindConfig` 测试直接向产品级全局 bind 数组无限追加。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime/Core/AngelscriptBinds.*` 增加 test-only seam，例如 `GetRegisteredBindCountForTesting()` + `TrimRegisteredBindsForTesting(int32 BaselineCount)`，或返回可释放 handle 的 `RegisterBindsForTesting()` / `UnregisterBindsForTesting()`。 2. 在 `AngelscriptTest` 侧新增 `FScopedTestBindRegistration` fixture：构造时记录当前 bind 数量，注册测试用 bind；析构时回滚到进入前的注册表长度。 3. 将 `AngelscriptBindConfigTests.cpp` 中当前直接声明局部 `FAngelscriptBinds::FBind` 的路径改为使用 fixture，并把 `MakeUniqueBindTestName()` 收敛为稳定、可读的测试名，避免继续用 GUID 逃避历史残留。 4. 在 `Core` 或 `Shared` 层新增回归：记录 `GetAllRegisteredBindNames().Num()`，进入/退出 fixture 后断言数量恢复基线，且旧测试名不会在下一条测试里继续出现。 5. 将这条约束写入 `TESTING_GUIDE.md` / `TESTING_GUIDE_ZH.md`：测试若需要临时 bind，必须走 scoped fixture，不允许直接构造裸 `FAngelscriptBinds::FBind`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md` |
| 预估工作量 | M |
| 风险 | 直接截断全局 bind 数组如果没有严格按进入时长度回滚，可能误删产品启动时已注册的真实 bind；需要把回滚基线绑定到 fixture 构造时的确切长度，并限制该 seam 只在测试编译条件下可见。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 scoped-bind 回归，验证 fixture 退出后 `GetAllRegisteredBindNames()` 恢复到进入前数量。 2. 在同一进程内连续两次运行 `BindConfig` 代表性测试，确认第二次不会因为第一次残留的 GUID bind 改变 bind info 列表。 3. 对 `as.DumpEngineState` 或 bind dump 相关输出做抽样检查，确认测试 bind 不再长期残留到后续导出。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-64 | Defect | 先收口临时 bind 注册泄漏，否则后续任何 bind 相关回归都会继续在被污染的全局注册表上运行 |

---

## 发现与方案 (2026-04-09 00:14)

### Issue-65：`BindConfig` 覆盖测试通过 `ResetBindState()` 直接抹掉全局 bind state，却不恢复进入测试前的运行态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` |
| 行号 | 493-552, 555-585, 604-703, 706-799, 802-847; 118-131; 186-189 |
| 问题 | `GeneratedBlueprintCallableEntriesPopulateClassMaps`、`AddFunctionEntryPreservesFirstRegistration`、`FunctionLevelScriptMethodUsesFirstParameterAsMixin`、`CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait`、`OverloadedExportedFunctionsCanRecoverDirectBind`、`InlineDefinitionFunctionsCanRecoverDirectBind`、`InlineOutRefFunctionsCanRecoverDirectBind` 这 7 条测试都会在进入时调用 `FAngelscriptBinds::ResetBindState()`，退出时再调用一次同样的 reset。运行时实现表明，这个 API 会把整个 `FAngelscriptBindState` 直接替换成默认值，包含 `ClassFuncMaps`、`RuntimeClassDB`、`EditorClassDB`、`SkipBinds` 等全部全局缓存。也就是说，这些测试不是“局部清理自己的修改”，而是在进出测试时两次清空全局 bind state，却从不恢复进入测试前的真实内容。 |
| 根因 | 覆盖测试需要在干净 bind-state 基线上验证重建流程，但当前基础设施没有 bind-state snapshot / restore fixture，调用方只能直接使用产品级 destructive reset。 |
| 影响 | 只要测试开始前已经存在 production engine、shared engine 或任何依赖 `GetClassFuncMaps()` / class DB 的运行态，这些测试就会把它们的 bind-state 镜像清空并长期留空；后续反射绑定、source navigation、dump、editor/runtime bridge 相关测试会在同一进程里看到被抹掉的全局状态，形成明显的顺序依赖。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用 bind-state snapshot/restore fixture 替代裸 `ResetBindState()`，把“干净基线”限制在测试自有作用域内。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime/Core/AngelscriptBinds.*` 新增 test-only seam，例如 `CaptureBindStateForTesting()` / `RestoreBindStateForTesting(const FAngelscriptBindState&)`，或提供可复制的 snapshot 结构，显式覆盖 `ClassFuncMaps`、class DB、skip sets 与 `PreviouslyBound*` 字段。 2. 在 `AngelscriptTest` 侧新增 `FScopedBindStateIsolationFixture`：构造时捕获旧 state，并根据需要执行一次 `ResetBindState()` 作为测试基线；析构时恢复进入前快照，而不是再次清空。 3. 将 `AngelscriptBindConfigTests.cpp` 中当前 7 处 `ResetBindState()` + `ON_SCOPE_EXIT{ ResetBindState(); ... }` 样板全部迁移到该 fixture，同时把 shared/global engine cleanup 收敛到同一 fixture，避免再手写散落的 teardown。 4. 新增回归：先人为向 `ClassFuncMaps` / `RuntimeClassDB` 注入一条可识别状态，运行 fixture 驱动的 bind-config 覆盖测试后，断言退出后原状态仍存在。 5. 在 `TESTING_GUIDE.md` / `TESTING_GUIDE_ZH.md` 明确规定：`ResetBindState()` 只能在 bind-state isolation fixture 内部调用，测试文件不得直接使用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md` |
| 预估工作量 | M |
| 风险 | 如果 snapshot/restore 只覆盖 map 容器而遗漏内部辅助字段，例如 `PreviouslyBoundFunction`，恢复后的 bind 行为仍可能与进入前不一致；需要把 `FAngelscriptBindState` 全量纳入快照。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 bind-state restore 回归，验证 fixture 退出后 `ClassFuncMaps` 和 class DB 恢复为进入前内容。 2. 在同一进程内先运行一条依赖现有 bind map 的测试，再运行 `BindConfig` 覆盖测试并回到前者，确认结果不再受中间 reset 影响。 3. 抽样回归 `GeneratedBlueprintCallableEntriesPopulateClassMaps`、`CallableWithoutWorldContextKeepsHiddenWorldContextButClearsTrait` 与一条 source-navigation / reflective binding 测试，确认顺序互换后仍稳定。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-65 | Defect | 与 Issue-64 紧邻处理，先把 bind-state 恢复语义收口，否则 bind 注册回滚后仍会留下被清空的全局缓存 |

---

## 发现与方案 (2026-04-09 00:15)

### Issue-66：`FBindExecutionRecorder` 用 GUID key 的静态计数表永不清理，长期会话中持续积累无主观测状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 行号 | 55-77, 79-82, 272-304, 332-364, 370-417 |
| 问题 | `FBindExecutionRecorder` 把计数保存在进程级静态 `TMap<FName, int32>` 中，API 只有 `Reset/Increment/Get`，没有任何 `Remove/Clear`。同文件又通过 `MakeUniqueBindTestName()` 为 `CounterKey` 生成每轮都不同的 GUID 名称。结果是每次执行 `GlobalDisabledBindNames`、`EngineDisabledBindNames`、`UnnamedBindBackwardCompatibility` 等测试，都会向静态计数表新增一批永不回收的新 key。 |
| 根因 | 计数 recorder 设计成了“全局按名字查表”，但没有和测试生命周期绑定，也没有和临时 bind fixture 共用同一个 teardown 合约。 |
| 影响 | 在长时间运行的 Editor 会话或多轮自动化里，静态计数表会不断膨胀；如果某条遗留 bind 在后续轮次被再次触发，还会继续修改早已失去所有者的历史 counter，导致观测层本身成为新的噪音源。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用 scoped counter fixture 替代全局静态计数表，让 bind 观测状态随测试退出一起回收。 |
| 具体步骤 | 1. 在 `AngelscriptBindConfigTests.cpp` 提取 `FScopedBindExecutionCounter`：构造时创建计数对象，提供 `Increment()` / `Get()`，析构时显式移除或释放底层存储。 2. 让测试 bind lambda 捕获该 scoped counter 的句柄，而不是捕获 `FName CounterKey` 再回查全局 `TMap`。 3. 若受 Issue-64 影响仍需要名字索引，至少为 `FBindExecutionRecorder` 增加 `Remove(FName)`，并在每条测试 `ON_SCOPE_EXIT` 中清理 `CounterKey`；同时禁止继续用 GUID key 作为长期存储主键。 4. 在 `BindConfig` 自测中新增 recorder-cleanup 回归：记录计数容器大小，运行一个最小 bind recorder 场景后断言大小恢复基线。 5. 将 recorder 生命周期与未来的 `FScopedTestBindRegistration` 合并，确保 bind 与计数器由同一个 fixture 管理。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果简单把计数器改成局部对象，但 bind 注册仍是永久的，后续误触发旧 bind 可能访问失效引用；因此要么使用弱句柄，要么与 Issue-64 的 scoped bind 注册一起落地。 |
| 前置依赖 | 建议与 Issue-64 同步处理 |
| 验证方式 | 1. 新增 recorder-cleanup 回归，验证测试退出后计数容器大小恢复。 2. 在同一进程内重复运行 `BindConfig.GlobalDisabledBindNames` / `BindConfig.EngineDisabledBindNames` 两轮，确认计数器不会无限新增历史 key。 3. 人工检查日志或调试器，确认 fixture 退出后旧 counter 不再被后续 bind 误更新。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-66 | Defect | 在 Issue-64 完成后一起收口，把 bind 观测层也从全局静态表改成 scoped fixture |

---

## 发现与方案 (2026-04-09 00:25)

### Issue-67：`ResetSharedEngineReleasesGeneratedComponentClasses` 在 live component/world 仍存活时执行 shared reset

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | 355-444; 207-269 |
| 问题 | `FAngelscriptTestEngineHelperResetSharedEngineReleasesGeneratedComponentClassesTest` 在 `FActorTestSpawner Spawner`、`HostActor` 和已注册的 generated `UActorComponent` 仍然活着时，直接调用 `AngelscriptTestSupport::ResetSharedCloneEngine(Engine)`。而 `ResetSharedCloneEngine()` 会立刻 `DiscardModule()`、遍历 detached `UASClass` 清 root / `RF_Standalone`，然后 `CollectGarbage(RF_NoFlags, true)`。这意味着测试正在“世界里还有 live script instance”时强行回收其 class/module 归属的 shared engine 状态。 |
| 根因 | 该回归把“验证 shared reset 后 generated class 会消失”和“驱动 component 在 world 中完成 BeginPlay/Tick”塞进同一个作用域，没有先销毁 world fixture，再执行 engine reset。 |
| 影响 | reset 期间 world 仍持有 generated component/class 引用，可能出现悬空 `UClass`/`UFunction` 指针、GC 次序相关的偶发失败，或者把一次 helper 回归扩大成后续 scenario 测试的状态污染；同时当前 `MatchingClasses == 0` 断言也无法区分“真的安全清理”与“live object 仍在错误引用旧 class”两种状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 world/live-object 生命周期与 shared reset 生命周期拆开，先销毁 runtime fixture，再验证 generated symbol cleanup。 |
| 具体步骤 | 1. 将 `FActorTestSpawner`、`HostActor`、`Component` 包进内层作用域或提取专用 fixture（例如 `FGeneratedComponentWorldFixture`），确保离开该作用域后 world、actor、component 已完成 unregister/destroy。 2. 仅在 world fixture 完全销毁后调用 `ResetSharedCloneEngine(Engine)`；随后再执行 `TObjectIterator<UASClass>` 计数，避免在 live component 仍引用 class 时做 cleanup 断言。 3. 为这个 helper 回归补一条 teardown 顺序断言：reset 前记录 component 已销毁或 owner world 已退出，禁止未来再次在 live instance 场景下直接 reset shared engine。 4. 如果测试基础设施需要显式支持“仍有 live generated instance 时尝试 reset”的场景，则在 `ResetSharedCloneEngine()` 侧新增检测/日志，发现当前 engine package 仍有 live actor/component 使用 generated class 时直接报错，而不是静默 GC。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 若仅通过缩小作用域修复测试而不补 helper 侧 guard，未来其它测试仍可能重现同类“live world + reset”问题；因此建议至少补日志或 contract 断言。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 回归 `Angelscript.TestModule.Shared.EngineHelper.ResetSharedEngineReleasesGeneratedComponentClasses`，确认 reset 前 world fixture 已销毁，且结果稳定。 2. 串行运行该用例后再跑 `ScriptExamples.Coverage.*`、`Component.*` 或任意 `FindGeneratedClass()` 场景，确认不再出现 stale generated symbol。 3. 若新增 helper guard，再补负向回归：在 live component 未销毁时尝试 reset，应得到明确失败日志而不是继续 GC。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-67 | Defect | 先修这条 helper 回归的 teardown 顺序，避免 shared reset 在 live generated component 仍被 world 持有时执行 |

---

## 发现与方案 (2026-04-09 00:26)

### Issue-68：基础 helper 自测复用脏 shared engine，无法稳定验证 helper 自身的隔离契约

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | 21-70, 159-265; 179-199 |
| 问题 | `CompileModuleFromMemory`、`ExecuteIntFunction`、`GeneratedSymbolLookup`、`FailedAnnotatedModuleDoesNotPolluteLaterCompiles` 这 4 条最基础的 helper regression 都直接调用 `GetOrCreateSharedCloneEngine()`。而该 helper 返回的是进程级复用 engine，并维护 persistent scope；这些测试退出时最多只 `DiscardModule()` 1-2 个模块名，没有在进入前建立 clean baseline，也没有在退出后做 shared reset。结果是，这些“验证 helper 是否正确工作”的回归本身就依赖前序测试没有留下 raw module、detached generated symbol、diagnostics 或其它 shared state。 |
| 根因 | helper 自测把 convenience API 当成测试基线，而不是把“干净 shared engine / fresh engine”显式纳入 arrange 阶段；测试名字是 contract test，但实现仍然沿用业务测试式的共享引擎复用。 |
| 影响 | 这些回归会出现两类失真：一类是前序污染导致假失败，问题看起来像 helper regression，实际只是 inherited shared state；另一类是 helper 真正泄漏状态，但因为进入测试前 shared engine 已经不干净，当前断言无法证明污染来自本测试。像 `FailedAnnotatedModuleDoesNotPolluteLaterCompiles` 这种名字本就声称“无污染”的用例，在当前写法下并不能提供可信证据。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 helper 自测建立专用 baseline fixture，统一以 fresh/clean engine 启动，并在测试前后检查 shared state。 |
| 具体步骤 | 1. 在 `Shared` 层新增 `FAngelscriptEngineHelperSelfTestFixture` 或等价 helper：构造时执行 `DestroySharedTestEngine()` + `AcquireFreshSharedCloneEngine()`，析构时统一 `ResetSharedCloneEngine()` 或 `DestroySharedTestEngine()`。 2. 将 `CompileModuleFromMemory`、`ExecuteIntFunction`、`GeneratedSymbolLookup`、`FailedAnnotatedModuleDoesNotPolluteLaterCompiles` 迁移到该 fixture，禁止继续直接从 `GetOrCreateSharedCloneEngine()` 取 ambient shared engine。 3. 在 fixture 中加入基线断言/探针，例如进入测试前 `Engine.GetActiveModules().Num() == 0`、raw module count 为 0、目标 generated symbol 不存在；退出后再次验证恢复。 4. 对 annotated helper 自测，不再只 `DiscardModule()`，而是使用 reset/fresh teardown，确保 generated class/function cleanup 被真正覆盖，而不是依赖前序状态碰巧干净。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 若简单把所有 helper 自测一刀切到 `AcquireFreshSharedCloneEngine()`，会重新碰到已记录的 shared-owner/clone 语义问题；因此 baseline fixture 需要和 shared ownership 规则一起落地，至少保证不会在 live clone 场景下强行 fresh。 |
| 前置依赖 | 建议结合 Issue-49 的 shared-owner/live-clone 处理一起实施 |
| 验证方式 | 1. 回归 `Angelscript.TestModule.Shared.EngineHelper.CompileModuleFromMemory`、`ExecuteIntFunction`、`GeneratedSymbolLookup`、`FailedAnnotatedModuleDoesNotPolluteLaterCompiles`，确认单独运行与串行运行结果一致。 2. 在这些测试前人工插入一条会污染 shared engine 的 annotated/raw-module 用例，确认迁移后它们仍从干净基线开始。 3. 为新 fixture 增加自测，验证进入前后 active modules、raw modules 和 target generated symbols 都恢复到基线。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-68 | Defect | 在 shared cleanup/ownership 规则稳定后，尽快把基础 helper 自测切到显式 clean baseline，恢复它们的护栏价值 |

---

## 发现与方案 (2026-04-09 00:27)

### Issue-69：源码中已经存在真实的 `ASTEST_END_*` 配对违规样本，宏生命周期规则开始失效

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp` |
| 行号 | 74-78; 234-245; 182-199; 212-214, 300-302 |
| 问题 | 宏注释已经明确规定“把 terminal `return` 放在 `ASTEST_END_*` 之后”，但本轮人工核对至少确认了 4 处真实违规：`LearningCompilerTraceTests.cpp` 和 `LearningFileSystemAndModuleTraceTests.cpp` 都在 multi-line `return ...` 之后才写 `ASTEST_END_SHARE_CLEAN`；`BlueprintImpactTests.cpp` 中两条 `RunTest()` 直接在 `ASTEST_END_SHARE_CLEAN` 前 `return TestTrue(...)` / `return TestFalse(...)`。这些文件当前依赖 C++ 的 RAII 仍会在 `return` 时析构作用域对象，但源码层面的 begin/end 配对已经不再显式。 |
| 根因 | 宏迁移后的“显式闭合再 return”规则没有被实际调用点持续遵守；同时已有 validation 只覆盖了部分简单形态，未及时阻止这些具体违规落库。 |
| 影响 | 生命周期宏在源码中的可读性和审查价值被削弱，后续维护者更难判断 cleanup scope 的真实边界；一旦有人在 `return` 与 `ASTEST_END_*` 之间插入语句、注释或新的 cleanup 代码，就会进一步制造误读和样板扩散。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先修正已确认的违规文件，再把“显式闭合后 return”固化进 validation 与宏示例。 |
| 具体步骤 | 1. 对已确认的 4 处样本逐个重写成统一模式：先把最终结果存入局部变量（例如 `const bool bAllChecksPassed = ...;`），随后写 `ASTEST_END_*`，最后在宏外 `return bAllChecksPassed;`。 2. 优先处理 `LearningCompilerTraceTests.cpp`、`LearningFileSystemAndModuleTraceTests.cpp`、`BlueprintImpactTests.cpp` 这三份文件，因为它们现在都是用户可读的示例/trace 路径，最容易向后续新增测试传播错误样板。 3. 将这些修正样本加入 `Validation/AngelscriptMacroValidationTests.cpp` 或示例文档，作为“合法 end placement”的正例；同时与已记录的 Issue-51 一起加强扫描，确保未来不会再把 wrapped/multi-line return 漏过去。 4. 在 `README_MACROS.md` / `README_MACROS_ZH.md` 中增加一段最小合法模板，避免开发者继续从旧文件复制“return 在 END 前”的写法。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintImpactTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS_ZH.md` |
| 预估工作量 | S |
| 风险 | 单独只改源码而不补 validation，会继续允许旧模式从其它文件复制扩散；单独只改 validation 而不先修当前违规文件，又会立刻把现有仓库打红。两者需要同批落地。 |
| 前置依赖 | 建议与 Issue-51 一起实施 |
| 验证方式 | 1. 回归 `Angelscript.TestModule.Learning.Runtime.CompilerPipeline`、`Learning.Runtime.FileSystemAndModuleResolution`、`Blueprint.Impact.*`，确认行为不变。 2. 运行 `Angelscript.TestModule.Validation.LifecycleEndPlacement`，确认修正后的样本不再违规。 3. 对这三份文件做一次静态复查，确认最终 `return` 已移动到 `ASTEST_END_*` 之后。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-69 | Refactoring | 与 Issue-51 配套处理，先收敛已落库的真实违规样本，再收紧 validator，阻止样板继续扩散 |

---

## 发现与方案 (2026-04-09 00:41)

### Issue-70：测试 cleanup 普遍忽略 `DiscardModule()` 失败，module teardown 失效会被静默吞掉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h` |
| 行号 | 1026-1043; 121-126; 368-370; 173-175; 24-26; 207-240; 85-90, 129-134 |
| 问题 | runtime 侧 `FAngelscriptEngine::DiscardModule()` 在 `Engine == nullptr` 或底层 `Engine->DiscardModule()` 返回 `< 0` 时会直接返回 `false`。但测试层大量 cleanup 调用都把这个返回值直接丢弃：`LearningCompilerTraceTests.cpp`、`ComponentScenarioTests.cpp`、`ScriptExampleCoverageTests.cpp`、`AngelscriptTestEngineHelperTests.cpp` 的 `ON_SCOPE_EXIT` 都只是裸调用；`ResetSharedCloneEngine()` 和 `ASTEST_BEGIN_FULL/CLONE` 的自动 cleanup 也没有检查成功与否。结果是模块名错配、重复 discard、tracked/raw module 状态异常都会被静默吞掉，测试表面继续通过。仓库里其实已经存在正确范式，例如 `HotReload/AngelscriptHotReloadFunctionTests.cpp:196-210` 与 `Angelscript/AngelscriptExecutionTests.cpp:466-472` 会显式断言 discard 第一次成功、第二次失败，但这种 contract 没有扩散到基础设施。 |
| 根因 | `DiscardModule()` 被当成 best-effort 清理语句使用，测试基础设施没有提供统一的 checked cleanup helper，也没有区分“必须成功的 tracked module teardown”和“允许不存在的 best-effort 清理”两种语义。 |
| 影响 | cleanup 失败后，tracked module、generated class/function 壳对象、compile diagnostics 与 raw script module 可能继续残留到后续测试；同时像 Issue-22 这类 module identity 漂移问题会被长期掩盖，因为 teardown 失败不会让当前用例立即变红。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为测试层建立统一的 checked module-cleanup helper，把 `DiscardModule()` 从“裸调用”升级为“有语义的 teardown contract”。 |
| 具体步骤 | 1. 在 `Shared` 层新增统一 helper，例如 `DiscardTrackedModuleChecked(FAutomationTestBase&, FAngelscriptEngine&, const TCHAR* ModuleName, const TCHAR* Context)` 与 `DiscardTrackedModuleBestEffort(...)`；前者在返回 `false` 时调用 `TestTrue`/`AddError` 报失败，后者只用于显式允许模块不存在的场景。 2. 将 `ASTEST_BEGIN_FULL`、`ASTEST_BEGIN_CLONE`、`ResetSharedCloneEngine()`、`DestroySharedAndStrayGlobalTestEngine()` 内部对 tracked module 的 cleanup 全部迁移到该 helper，禁止继续在基础设施里直接裸写 `Engine.DiscardModule(...)`。 3. 批量迁移当前代表性调用点：`Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`、`Component/AngelscriptComponentScenarioTests.cpp`、`Examples/AngelscriptScriptExampleCoverageTests.cpp`、`Shared/AngelscriptTestEngineHelperTests.cpp`、`Examples/AngelscriptScriptExampleTestSupport.cpp`；其中 production-world 与 annotated 路径优先切到 checked 版本，因为它们最容易把 teardown 失败扩散到后续测试。 4. 新增两条回归：一条故意对同一模块做二次 discard，验证 checked helper 会把第二次失败暴露成测试断言；另一条构造 requested module name 与实际 module name 不一致的 path-based compile，验证 cleanup 会明确报错，而不是静默通过。 5. 在 `TESTING_GUIDE.md` / `TESTING_GUIDE_ZH.md` 中写明规则：测试体内禁止直接裸调用 `Engine.DiscardModule(...)` 做 cleanup，必须使用 checked helper；只有显式说明“允许模块不存在”的场景才能使用 `BestEffort` 变体。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningCompilerTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleCoverageTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleTestSupport.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md` |
| 预估工作量 | M |
| 风险 | 这次迁移会把一批原本被吞掉的 cleanup 失败立即暴露出来，短期内可能导致多条测试转红；因此应先从 shared/helper 自测和代表性 production-world 场景落地，再逐步扩展到全仓库。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 discard-contract 回归，验证首次 discard 成功、二次 discard 失败会被 checked helper 明确捕获。 2. 回归 `Angelscript.TestModule.Shared.EngineHelper.*`、`Learning.Runtime.CompilerPipeline`、`Component.DefaultComponent.*`、`ScriptExamples.Coverage.*`，确认 cleanup 失败时会直接报错。 3. 对 `rg -n "\\.DiscardModule\\(" Plugins/Angelscript/Source/AngelscriptTest` 做抽样复查，确认基础设施与代表性业务测试已从裸调用迁移到 checked helper。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-70 | Defect | 先把 cleanup failure 从静默吞掉改成显式失败，否则后续所有 teardown 修复都缺少可靠护栏 |

---

## 发现与方案 (2026-04-09 00:44)

### Issue-71：`Native` 执行测试在 `CreateEngineAndBuildModule()` 失败路径泄漏 standalone script engine

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeExecutionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeTestSupport.h` |
| 行号 | 37-58, 62-75, 94-107, 128-141, 170-183, 213-226; 121-165, 191-194 |
| 问题 | `CreateEngineAndBuildModule()` 先调用 `CreateNativeEngine()` 创建 standalone `asIScriptEngine*`，再调用 `BuildNativeModule()`。如果第二步失败，helper 只会 `AddInfo` 后 `return false`，并不会执行 `DestroyNativeEngine()`。而 5 条 `Native.Execute.*` 测试都是在 helper 成功返回之后才注册 `ON_SCOPE_EXIT { DestroyNativeEngine(ScriptEngine); }`，因此一旦模块编译失败、helper 直接返回，engine 就没有任何 owner 负责释放。 |
| 根因 | `Native` 测试基础设施把 engine ownership 暴露为裸指针出参，却把销毁责任留给调用方在 helper 返回后再补 `ON_SCOPE_EXIT`；这种设计天然覆盖不到 helper 内部的失败路径。 |
| 影响 | 一旦某条 `Native.Execute.*` 测试因为脚本编译失败、message callback 初始化后续调整或测试脚本文本损坏而提前失败，就会把 standalone script engine 留在进程里；长时间 automation 会话中这会累积 native engine、module 与 message collector 相关资源，同时让 failure-path 本身变得不可重复和难排查。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用 owning fixture 接管 `Native` 测试的 engine/module 生命周期，把创建、编译、销毁封装成单一 RAII 合约。 |
| 具体步骤 | 1. 在 `Native` 层新增 `FNativeExecutionFixture` 或等价 RAII 结构：构造时调用 `CreateNativeEngine()`，并提供 `BuildModule()` / `RequireFunctionByDecl()`；析构时无条件调用 `DestroyNativeEngine()`。 2. 把 `CreateEngineAndBuildModule()` 改成返回 fixture 或在 helper 内部就地持有一个 `TUniquePtr` 风格 owner；若 `BuildNativeModule()` 失败，helper 在返回 `false` 前必须立即销毁刚创建的 engine，并把 `OutScriptEngine` / `OutModule` 置空。 3. 迁移 5 条 `Native.Execute.*` 测试到 fixture，删除文件内重复的 `FNativeMessageCollector`、裸 `asIScriptEngine*`、裸 `ON_SCOPE_EXIT` 样板；调用点只保留业务相关的 function lookup、arg setup 与 return-value 断言。 4. 新增负向回归：传入一个故意编译失败的 native script，断言 helper 返回失败后 fixture 已释放 engine，且第二次用相同模块名重新创建不会继承第一次的残留状态。 5. 若后续还需要 `asIScriptContext*` 的 failure-path 收口，可在同一 fixture 下继续补 `FScopedNativeContext`，统一处理 `CreateContext()`、`Prepare()` 和 `Release()`，避免 native 测试再次回到手写 resource cleanup。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeExecutionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeTestSupport.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | S |
| 风险 | 如果只在 helper 失败分支补 `DestroyNativeEngine()`，但不把 ownership 收口到 fixture，后续新增 native 测试仍会复制同样的裸指针模式；因此最好同批完成最小 RAII 封装，而不是只打一个局部补丁。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 native-build-failure 回归，确认 `CreateEngineAndBuildModule()` 失败后不会留下 live engine。 2. 回归 `Angelscript.TestModule.Native.Execute.*` 全部 5 条测试，确认迁移后行为不变。 3. 在同一进程内先跑负向 native compile，再跑一条成功的 `Native.Execute.ReturnValue`，确认第二条不会继承第一次失败留下的状态。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-71 | Defect | 在 shared/full cleanup 主线之外并行收口 `Native` failure-path ownership，避免一旦 native 测试转红就开始泄漏 standalone engine |

---

## 发现与方案 (2026-04-09 00:58)

### Issue-72：`SHARE_CLEAN` / `SHARE_FRESH` 只有入场 reset，没有退场 reset，分析类用例会把 shared engine 留脏

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp` |
| 行号 | 106-122; 48-366; 122-168 |
| 问题 | `ASTEST_BEGIN_SHARE_CLEAN` / `ASTEST_BEGIN_SHARE_FRESH` 在宏体内只建立 `FAngelscriptEngineScope`，不像 `FULL` / `CLONE` 那样带任何退出时 cleanup。`HotReloadAnalysisTests.cpp` 的 7 条 `AnalyzeReload.*` 用例从 50、95、142、182、233、278、323 行开始都使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`，随后编译 annotated baseline 再调用 `AnalyzeReloadFromMemory()`，但整文件没有对应的 `ON_SCOPE_EXIT` 或 `ResetSharedCloneEngine()`；`Validation/AngelscriptMacroValidationTests.cpp:126-166` 的 `SharedCleanMacro` / `SharedFreshMacro` 也只是 compile-run 后直接退出。宏名里的 `CLEAN` / `FRESH` 很容易让调用点误以为它们提供整条测试生命周期的隔离，实际只是在进入前清一次。 |
| 根因 | 当前宏体系把“进入测试前拿到干净 shared engine”与“退出测试后恢复干净基线”混成同一个命名，但实现只覆盖了前者；validation 也只检查 compile/run 成功，没有检查退出后的 shared state。 |
| 影响 | 这些测试退出后会把 tracked module、generated class/function、diagnostics 等状态继续留在 shared engine 中，后续若接的是 `ASTEST_BEGIN_SHARE`、`GetOrCreateSharedCloneEngine()` 或任何不先 reset 的 helper，就会继承前序分析测试残留，形成顺序相关和重跑不稳定；同时宏 validation 会继续把这种半隔离语义误报为“通过”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“pre-clean convenience”与“整个测试生命周期隔离”拆成两个明确 contract，并让分析/validation 用例统一迁移到带退场 reset 的 contract。 |
| 具体步骤 | 1. 在 `Shared` 层新增对称生命周期入口，例如 `FAngelscriptSharedIsolatedScope` 或 `ASTEST_BEGIN_SHARE_ISOLATED_CLEAN` / `ASTEST_END_SHARE_ISOLATED_CLEAN`，内部统一包 `FAngelscriptEngineScope` + `ON_SCOPE_EXIT { ResetSharedCloneEngine(Engine); }`。 2. 保留现有 `ASTEST_CREATE_ENGINE_SHARE_CLEAN/FRESH` 作为“进入前 reset”的低层 API，但在 `README_MACROS*.md` / `TESTING_GUIDE*.md` 中把语义改写为 `PreClean` / `PreFresh`，避免继续把它们当成完整隔离宏。 3. 先迁移已确认的泄漏调用点：`HotReload/AngelscriptHotReloadAnalysisTests.cpp` 的 7 条 `AnalyzeReload.*` 测试，以及 `Validation/AngelscriptMacroValidationTests.cpp` 的 `SharedCleanMacro` / `SharedFreshMacro`。 4. 在 validation 或 helper 自测里新增 postcondition 回归：测试体内编译一个具名模块，离开 `ASTEST_END_SHARE_*` 后立即断言 `Engine.GetActiveModules().Num()==0`，并且 `GetModule(..., asGM_ONLY_IF_EXISTS)` 返回空。 5. 对仓库内其余 `SHARE_CLEAN` / `SHARE_FRESH` 调用点做一次审计：凡是退出时没有显式 `ResetSharedCloneEngine()` / `DestroySharedTestEngine()` 的，统一迁移到新宏或补对称 teardown。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Validation/AngelscriptMacroValidationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS.md`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/README_MACROS_ZH.md`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE_ZH.md` |
| 预估工作量 | M |
| 风险 | 如果直接给现有 `SHARE_CLEAN/FRESH` 宏硬塞退场 reset，可能会打破少数确实想在 `ASTEST_END_*` 之后继续读取 shared 状态的旧测试；因此更稳妥的落地方式是新增对称宏并逐步迁移，再把旧语义显式标成 pre-clean。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行 `Angelscript.TestModule.HotReload.AnalyzeReload.*` 与 `Angelscript.TestModule.Validation.SharedCleanMacro` / `SharedFreshMacro`，确认迁移后结果不依赖执行顺序。 2. 新增 macro postcondition 回归，验证 `ASTEST_END_SHARE_*` 后 tracked/raw module 都被清空。 3. 串行运行一条 `AnalyzeReload.*` 测试后紧接一条 `ASTEST_BEGIN_SHARE` 的轻量 compile-run 测试，确认后者不再继承前者残留模块。 |

### Issue-73：reload-analysis 回归在两个文件中复制了 10 份 baseline/analyze/assert 样板，cleanup 语义已经开始漂移

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Inheritance/AngelscriptInheritanceScenarioTests.cpp` |
| 行号 | 48-366; 39-206 |
| 问题 | 本轮对这两份文件扫描，`CompileAnnotatedModuleFromMemory()` + `AnalyzeReloadFromMemory()` 的重复骨架共命中 10 组：`HotReloadAnalysisTests.cpp` 7 组、`InheritanceScenarioTests.cpp` 3 组。它们都在做同一套流程：获取 shared engine、编译 baseline、分析 updated script、断言 `ReloadRequirement` / `bWantsFullReload` / `bNeedsFullReload`。但重复实现已经开始漂移：`HotReloadAnalysisTests.cpp` 每条测试都保留了未使用的 `EngineOwner = ASTEST_CREATE_ENGINE_SHARE()`，而 `InheritanceScenarioTests.cpp` 走的是显式 `ON_SCOPE_EXIT { Engine.DiscardModule(...); ResetSharedCloneEngine(Engine); }`。相同 contract 的回归现在需要靠复制 10 份手写样板维持，任何 cleanup、engine-mode 或错误信息策略调整都必须人工同步。 |
| 根因 | 目前没有一个面向 reload-analysis 的小型 fixture 或 data-driven runner，把“输入脚本对 + 预期 reload requirement”与“如何搭建 engine、如何 cleanup、如何断言 flags”分离开。 |
| 影响 | Issue-72 这类 shared teardown 修复落地时，需要同时修改 10 条测试体，遗漏一处就会留下新的顺序污染；未来如果 `AnalyzeReloadFromMemory()` 的返回 contract 或 diagnostics 形态变化，也容易让 HotReload 和 Inheritance 两组回归出现不同的 arrange/cleanup 行为，增加维护成本并削弱 coverage 一致性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 提取统一的 reload-analysis scenario runner，保留现有测试名，但把脚本输入、期望结果和 cleanup 策略收敛到共享 fixture。 |
| 具体步骤 | 1. 在 `Shared` 层新增 `FAngelscriptReloadAnalysisScenario` / `RunReloadAnalysisScenario(...)` 辅助体，字段至少包含 `ModuleName`、`Filename`、`BaselineScript`、`UpdatedScript`、`ExpectedRequirement`、`ExpectedWantsFullReload`、`ExpectedNeedsFullReload`、`ExpectedAnalyzeSuccess`。 2. 让 runner 内部统一负责 engine 获取、baseline compile、`AnalyzeReloadFromMemory()` 调用、flag 断言以及 teardown；shared engine 路径直接复用 Issue-72 规划的对称 cleanup contract。 3. 将 `HotReload/AngelscriptHotReloadAnalysisTests.cpp` 的 7 条测试改成“声明 scenario 常量 + 调 runner”的薄包装，删除未使用的 `EngineOwner` 和重复的 local flag 初始化。 4. 将 `Inheritance/AngelscriptInheritanceScenarioTests.cpp` 的 3 条测试同步迁移到同一 runner，只保留各自不同的脚本输入与预期结果，避免再单独维护第二套 cleanup 模式。 5. 若 learning trace 也需要同类矩阵，可让 `LearningHotReloadDecisionTraceTests.cpp` / `LearningReloadAndClassAnalysisTests.cpp` 读取同一 scenario 结构生成 trace，避免 HotReload、Inheritance、Learning 三套样板继续分叉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptReloadAnalysisTestUtils.h`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Inheritance/AngelscriptInheritanceScenarioTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningReloadAndClassAnalysisTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果把所有回归合并成一个大循环测试，会丢失现有细粒度测试名与失败定位信息；因此应保留每个 automation test 的独立注册名，只把内部执行逻辑抽到共享 runner。 |
| 前置依赖 | 建议与 Issue-72 的 shared cleanup contract 一起实施 |
| 验证方式 | 1. 回归 `Angelscript.TestModule.HotReload.AnalyzeReload.*` 与 `Angelscript.TestModule.Inheritance.*`，确认测试名不变、断言结果不变。 2. 变更一次 runner 内部 cleanup 或 flag 初始化后重跑全部 10 条测试，确认 HotReload 与 Inheritance 两组行为保持一致。 3. 对 `rg -n "CompileAnnotatedModuleFromMemory\\(|AnalyzeReloadFromMemory\\("` 在这两份文件中复查，确认重复骨架显著减少，只剩 scenario 数据与少量包装代码。 |

### Issue-74：多处 Native / ASSDK 执行 helper 在确认成功前就读取返回值，失败路径会产出不可信结果

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKCallingConvTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKCompilerTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKConversionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKModuleTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKObjectTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKOOPTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKOperatorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKRuntimeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKTypeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeRegistrationTests.cpp` |
| 行号 | 35-44; 27-44; 36-45; 102-127 |
| 问题 | 多个 native/ASSDK helper 采用相同模式：先 `const int ExecuteResult = PrepareAndExecute(Context, Function);`，随后立刻把 `Context->GetReturnByte()` / `GetReturnDWord()` / `GetReturnDouble()` 读进 `OutValue`，最后才用 `TestEqual(..., ExecuteResult, asEXECUTION_FINISHED)` 判断是否成功。`AngelscriptASSDKCallingConvTests.cpp:35-44`、`AngelscriptASSDKCompilerTests.cpp:27-44`、`AngelscriptASSDKConversionTests.cpp:36-45` 都是这种顺序；本轮对 11 个 native/ASSDK 文件扫描，命中了 14 处 `OutValue = Context->GetReturn*()` 样板。只有 `AngelscriptNativeRegistrationTests.cpp:108-127` 在失败时补了异常信息，其余 helper 在失败路径既读了不可信返回寄存器，也没有统一输出异常上下文。 |
| 根因 | native 层缺少一个统一的 “execute + verify + decode return value” helper，调用点只能各自手写 `PrepareAndExecute()`、`GetReturn*()` 和 diagnostics 顺序，导致 decode 时机与失败日志策略漂移。 |
| 影响 | 一旦 prepare 失败、执行异常或未来增加新的失败分支，`OutValue` 就可能被写成陈旧/未定义的返回寄存器内容；失败日志也会因 helper 不一致而缺少异常行号、异常字符串和 message callback 诊断，增加 native 测试定位成本。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 `NativeTestSupport` 中集中实现 checked execute helper，只允许在 `asEXECUTION_FINISHED` 后解码返回值，并统一附带异常/diagnostics 输出。 |
| 具体步骤 | 1. 在 `Native/AngelscriptNativeTestSupport.h` 新增通用执行辅助，例如 `ExecuteNativeFunctionChecked(FAutomationTestBase&, asIScriptEngine&, asIScriptFunction&, FNativeMessageCollector*, FNativeExecutionResult&)`，内部统一完成 `CreateContext()`、`PrepareAndExecute()`、异常字符串/行号收集和 `Context->Release()`。 2. 在该 helper 上层提供少量 typed decode 包装，例如 `ExecuteNativeBoolChecked(...)`、`ExecuteNativeIntChecked(...)`、`ExecuteNativeDoubleChecked(...)`；只有 `ExecuteResult == asEXECUTION_FINISHED` 时才读取 `GetReturn*()`，否则保留 `OutValue` 原值并返回 `false`。 3. 优先迁移当前 11 个文件中的匿名 namespace helper 与 registration helper，删除分散的 `PrepareAndExecute()` + `GetReturn*()` 顺序代码。 4. 把 `AngelscriptNativeRegistrationTests.cpp` 当前已有的 exception 信息采集并入新 helper，让 ASSDK/native 两条测试线共享同一 diagnostics 格式，而不是一边有异常日志、一边只有 `false`。 5. 新增负向回归：构造一条运行时抛异常或 prepare 失败的 native script，断言 helper 返回 `false`、`OutValue` 维持调用前值，且日志包含异常行号或 message callback 文本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeTestSupport.h`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKCallingConvTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKCompilerTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKConversionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKModuleTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKObjectTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKOOPTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKOperatorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKRuntimeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKTypeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeRegistrationTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接把 helper 改成泛型模板并一次性迁移全部 native 文件，容易把返回值类型推导、日志文案和现有断言文本一起打散；更稳妥的方式是先提供少数 typed wrapper，再分批迁移调用点。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 native execution negative 回归，验证失败时 `OutValue` 不被污染且 diagnostics 可见。 2. 回归 `Angelscript.TestModule.Native.Register.*` 与一组 ASSDK execute/compiler 测试，确认成功路径行为不变。 3. 对 `rg -n "OutValue = .*GetReturn"` 在上述 11 个文件做复查，确认 decode-return 样板已收敛到 shared helper。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-72 | Defect | 先修复 shared clean/fresh 的退场隔离 contract，再处理所有依赖它的分析类测试 |
| P2 | Issue-74 | Defect | 紧随其后收口 native/ASSDK 执行 helper，避免 failure-path 继续读脏返回值 |
| P2 | Issue-73 | Refactoring | 在 cleanup contract 稳定后提取统一 reload-analysis runner，压缩 10 份重复样板 |

---

## 发现与方案 (2026-04-09 01:11)

### Issue-75：debugger test client 把 non-blocking `Connect()` 的 “进行中” 误判成已连接，握手阶段存在时序型 flaky

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 行号 | 32-73, 92-129; 26-53; 19-58; 17-55 |
| 问题 | `FAngelscriptDebuggerTestClient::Connect()` 把 socket 设成 `NonBlocking`，随后如果 `Socket->Connect()` 返回 `SE_EWOULDBLOCK` 或 `SE_EINPROGRESS`，就直接把这次调用当作成功并返回 `true`。但 `Smoke.Handshake`、`Breakpoint`、`Stepping` 三组测试都在 `Client.Connect(...)` 成功后立即调用 `SendStartDebugging()` 或进入依赖即时收发的握手流程，没有任何 “等待 socket 真正进入 `SCS_Connected`” 的阶段。这意味着当前 contract 实际是“connect 已发起”而不是“client 已连上 debug server”。 |
| 根因 | debugger test client 采用了 non-blocking socket，但测试基础设施没有补连接完成探测步骤；`Connect()` 的返回语义与调用方假设的“立即可收发”不一致。 |
| 影响 | 在本地负载高、端口刚建立、或后续按既有计划改成自建 debugger engine/随机端口后，`SendStartDebugging()`、`ReceiveEnvelope()` 可能偶发命中“socket 仍在连接中”窗口，表现为握手超时、首包发送失败或 `IsConnected()` 偶发为假，形成典型的时序型 flaky。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 debugger client 的连接 contract 收紧为“只有完成 TCP 握手后才返回成功”，并给调用方显式暴露等待连接完成的超时诊断。 |
| 具体步骤 | 1. 在 `FAngelscriptDebuggerTestClient` 中新增 `WaitForConnected(float TimeoutSeconds)` 或直接把 `Connect()` 改成两阶段实现：先发起 non-blocking connect，再用 `Wait(ESocketWaitConditions::WaitForWrite, Timeout)` / `GetConnectionState()` 轮询直到进入 `SCS_Connected`，超时或 error code 异常时返回 `false` 并设置 `LastError`。 2. 在等待完成后补一次 socket error/connection state 校验，禁止继续把 `SE_EWOULDBLOCK` / `SE_EINPROGRESS` 当最终成功态缓存下来。 3. 更新 `AngelscriptDebuggerSmokeTests.cpp`、`AngelscriptDebuggerBreakpointTests.cpp`、`AngelscriptDebuggerSteppingTests.cpp`：把“connect client”断言改成“connect + wait until connected”的单一 helper 调用，后续 `SendStartDebugging()` 不再假设 `Connect()` 已完成握手。 4. 在 `Shared` 层新增一条 deterministic 回归：注入一个延迟 accept/handshake 的 test server 或 test seam，断言 helper 会等待到 `SCS_Connected` 后才返回 `true`；再补一条超时回归，断言失败时 `LastError` 包含 host、port 和 timeout。 5. 若后续继续保留 non-blocking 模式，把连接完成超时常量收敛到 debugger fixture/session 配置中，避免 smoke、breakpoint、stepping 各自维护不同等待策略。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接把 `Connect()` 改成阻塞等待而不统一 timeout，某些失联场景会把 debugger 测试从“偶发失败”变成“长时间卡住”；需要把超时和错误信息一起收口。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 client-connect contract 回归，验证 `Connect()` 返回成功时 `IsConnected()==true`。 2. 回归 `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Breakpoint.*`、`Stepping.*`，确认首条 `SendStartDebugging()` 不再依赖竞态窗口。 3. 在同一进程内连续创建多次 debugger session/client，确认连接建立结果稳定，不再出现“connect 成功但首包发送失败”的间歇性问题。 |

### Issue-76：`DrainPendingMessages()` 会清空真实 transport 错误，断点回归会把协议故障误诊成业务失败

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 行号 | 131-158, 223-238, 324-362; 520-545 |
| 问题 | `ReceiveEnvelope()` 在两类场景下都会返回空 `TOptional`：一类是“当前没有完整 envelope”，另一类是 `AppendReceivedData()`/`ConsumeEnvelope()` 失败并把 `LastError` 设成 socket 或反序列化错误。`DrainPendingMessages()` 随后把这两类空结果混为一谈，直接 `break` 出循环并无条件 `LastError.Reset()`。`DebuggerBreakpoint.ClearThenResume` 在第二轮运行前正好调用了 `Client.DrainPendingMessages()` 清空队列，但没有检查是否刚刚吞掉了一次协议错误。结果是：真实的 transport/解析故障会在这里被抹掉，后面只留下 “没收到 stop event / breakpoint count 不对” 之类二次症状。 |
| 根因 | debugger client 当前没有区分 “queue is empty” 与 “receive failed” 两种结束条件，drain API 也没有把错误状态回传给调用方。 |
| 影响 | 一旦 debug server 发出半包、socket 接收失败或 envelope 解析异常，相关断点测试会先在 `DrainPendingMessages()` 处静默清错，再在后续等待断点/消息时以完全不同的断言失败，显著增加 flaky 排障成本，也削弱 transport 层回归价值。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 debugger client 的 drain/receive API 改为显式区分“无消息”和“接收失败”，禁止再在清队列辅助函数里吞掉 `LastError`。 |
| 具体步骤 | 1. 为 `ReceiveEnvelope()` / `DrainPendingMessages()` 引入结构化返回值，例如 `EDebuggerReceiveStatus { NoMessage, Received, Error }` 或 `FDrainPendingMessagesResult { Messages, bHadError, ErrorMessage }`，不要继续用空 `TOptional` 同时表达“暂时无消息”和“发生错误”。 2. 删除 `DrainPendingMessages()` 末尾的无条件 `LastError.Reset()`；只有在明确判定为 `NoMessage` 时才允许清空瞬时状态，真实 error 必须保留下来供调用方读取。 3. 更新 `DebuggerBreakpointTests.cpp:534` 及其它未来调用点：清队列后必须显式断言 `!Result.bHadError`，若失败则立即 `AddError(Result.ErrorMessage)` 并中止当前测试，而不是继续进入第二轮断点流程。 4. 在 debugger client 自测或 breakpoint 辅助回归中新增一条负向场景：向 `ReceiveBuffer` 注入截断 envelope 或模拟 `Recv()` 失败，断言 `DrainPendingMessages()` 返回 error，且 `LastError` 不会被清掉。 5. 如果仍希望保留 “best effort drain” 语义，则新增单独命名的 `DrainPendingMessagesBestEffort()`，并限制只能用于显式容忍 transport 噪声的诊断路径，普通断点/步进回归必须使用 checked 版本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果直接改成“任何 drain error 都让测试失败”，会暴露出一批此前被吞掉的 transport 问题；但这些失败本来就是真实问题，关键是要把错误信息收口到 transport 层而不是后置断言。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 debugger client negative 回归，验证 malformed envelope / recv failure 会从 `DrainPendingMessages()` 向上传播。 2. 回归 `Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume`，确认 transport 故障会在清队列阶段直接暴露，不再拖到第二轮断点断言。 3. 人工检查失败输出，确认错误信息优先指向 socket/deserialize 问题，而不是泛化的 “did not hit breakpoint”。 |

### Issue-77：debugger client 在 non-blocking socket 上把瞬时写阻塞当成 fatal send failure，监控线程会把 transport 抖动放大成超时型 flaky

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 行号 | 54-55, 97-129; 49-53, 90-93; 40-58, 189-214, 237-275; 38-56, 191-221, 253-300 |
| 问题 | `FAngelscriptDebuggerTestClient::Connect()` 明确把 socket 设成 `NonBlocking`，但 `SendRawEnvelope()` 仍按“阻塞式一次发完”的 contract 实现：循环里只要 `Socket->Send(...)` 返回 `false`，或者 `BytesSent <= 0`，就立刻 `SetError(...)` 并失败退出，没有读取 socket error code，也没有 `Wait(WaitForWrite, Timeout)` 或任何可写重试逻辑。上层测试又广泛依赖这条路径发送控制消息：`Smoke.Handshake` 直接发送 `StartDebugging` 和 `RequestBreakFilters`；`Breakpoint` / `Stepping` 的 monitor 线程在握手和 stop 处理中持续发送 `StartDebugging`、`RequestCallStack`、`Continue`、`StepIn/Over/Out`、`StopDebugging`、`Disconnect`，其中不少调用点连返回值都没有检查。结果是 client 无法区分“真正断连”和“socket 暂时不可写”，monitor helper 也会把 send-side 抖动折叠成笼统的 handshake timeout 或漏停断点。 |
| 根因 | debugger client 复用了 non-blocking connect，但 send contract 仍停留在 blocking socket 假设；上层 monitor/session helper 也没有把 send failure 建模成一等错误，而是继续轮询收包或在 cleanup 中直接忽略返回值。 |
| 影响 | 一旦连接建立后的短窗口、线程调度抖动或发送背压让 socket 暂时不可写，`StartDebugging` / `Continue` / `Step*` / `RequestCallStack` 就会被错误报成 fatal send failure，或者在 monitor 循环里被掩盖成 “timed out waiting for DebugServerVersion” / “没有收到 HasStopped” 之类二次症状，形成新的 debugger automation flaky，并显著拉高排障成本。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 统一 debugger client 的 socket contract：要么在连接完成后切回 blocking send，要么为 `SendRawEnvelope()` 补全 non-blocking 可写等待与错误分级，并要求所有 monitor/session helper 显式处理 send failure。 |
| 具体步骤 | 1. 在 `FAngelscriptDebuggerTestClient` 中明确 send 模式：若测试客户端不需要真正的 non-blocking write，最直接的方案是在 connect 完成后切回 blocking，仅保留 receive 侧轮询；若必须保留 non-blocking，则 `SendRawEnvelope()` 必须在 `Send()==false` 或 `BytesSent==0` 时读取 `ISocketSubsystem::GetLastErrorCode()`，对 `SE_EWOULDBLOCK` / `SE_EINPROGRESS` 走 `Wait(ESocketWaitConditions::WaitForWrite, Timeout)` 后重试，只把真实终止性错误视为失败。 2. 给 send path 加入结构化诊断，至少在 `LastError` 中包含 `MessageType`、已发送字节数、socket error code 和当前连接状态，避免再只留下 “failed to send” 的笼统文本。 3. 更新 `AngelscriptDebuggerSmokeTests.cpp`、`AngelscriptDebuggerBreakpointTests.cpp`、`AngelscriptDebuggerSteppingTests.cpp` 中所有 control-message 调用点：握手循环里一旦 `SendStartDebugging()` 返回终止性失败，应立即中止并透出 `LastError`，不要继续等到 receive timeout；`SendRequestCallStack()`、`SendContinue()`、`SendStepIn/Over/Out()`、`SendStopDebugging()`、`SendDisconnect()` 也必须改成 checked helper 或至少在 monitor result 中记录失败原因。 4. 为 shared debugger helper 新增 send-contract 回归，构造一个 test seam 或 fake socket，让第一次 `Send()` 报 `would-block`、第二次成功，验证 client 会等待并重试而不是立刻失败；再补一条永久 send error 回归，确认上层结果直接暴露 send-side 错误而不是握手超时。 5. 将 cleanup 阶段的 `StopDebugging` / `Disconnect` 收敛到统一 helper，区分“必须成功的协议步骤”和“best-effort 断开”，防止 monitor 线程继续无声吞掉收尾 send failure。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerClientContractTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接把所有 send 都改成长时间等待可写，可能把现在的 flaky 从“偶发失败”变成“偶发卡死”；因此必须和统一 timeout、错误分级、cleanup 语义一起落地，而不是只在 `SendRawEnvelope()` 里盲目加 sleep/retry。 |
| 前置依赖 | 建议与 Issue-75 同批实施，以统一 debugger client 的 connect/send 生命周期 contract |
| 验证方式 | 1. 新增 send-contract 回归，验证首次 `would-block` 不会让 `SendStartDebugging()` 直接失败。 2. 回归 `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Breakpoint.*`、`Stepping.*`，确认失败时优先暴露 send-side 错误，不再退化成笼统 timeout。 3. 在高频连续 attach/continue/step 的本地循环中抽样运行 debugger 测试，确认 send path 不再出现 “did not send any bytes” 或被掩盖成 handshake timeout 的间歇性失败。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-75 | Defect | 先修正 `Connect()` 的完成语义，消除“连接发起即成功”的基础竞态 |
| P1 | Issue-77 | Defect | 紧接着收口 non-blocking send contract 与 monitor send failure 传播，避免控制消息路径继续制造 flaky |
| P2 | Issue-76 | Defect | 在 connect/send contract 稳定后补齐 drain error 传播，把 transport 故障重新定位回真正失败点 |

---

## 发现与方案 (2026-04-09 01:34)

### Issue-78：`ResetSharedCloneEngine()` 不清理 diagnostics 缓存，`SHARE_CLEAN/FRESH` 之后仍会继承上一轮编译错误

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | 207-269, 378-432, 487-508; 4464-4467, 4926-4933; 718-745; 198-265 |
| 问题 | `ResetSharedCloneEngine()` 当前只做 module discard、raw `asIScriptModule` discard、detached `UASClass` cleanup 和 `CollectGarbage()`，没有清 `Engine.Diagnostics`、`LastEmittedDiagnostics` 或 `bDiagnosticsDirty`。runtime 侧 `ScriptCompileError()` 会把失败信息持续追加到 `Engine.Diagnostics`，而现成的 `ResetDiagnostics()` 也只清 `Diagnostics`。与此同时，`AcquireCleanSharedCloneEngine()`、`AcquireFreshSharedCloneEngine()` 和 `DestroySharedTestEngine()` 都把 `ResetSharedCloneEngine()` 当成 shared baseline 的核心清理入口；`DumpDiagnostics()` 又会直接把 `Engine.Diagnostics` 全量导出到 `Diagnostics.csv`。已验证事实：`Shared.EngineHelper.FailedAnnotatedModuleDoesNotPolluteLaterCompiles` 会先在 shared engine 上制造一次 broken annotated compile，再继续在同一 engine 上编译恢复模块，但测试只 `DiscardModule()`，没有清 diagnostics 缓存。 |
| 根因 | shared reset 的实现只覆盖了“模块/UObject 生命周期”，没有把 diagnostics 这类 engine-level compile cache 纳入 reset contract；runtime 现有 `ResetDiagnostics()` 也不足以恢复 `LastEmittedDiagnostics` 和 dirty 标记。 |
| 影响 | `SHARE_CLEAN` / `SHARE_FRESH` 名义上提供干净基线，实际仍可能把上一条失败编译的 diagnostics 带到下一条测试；后续 `ReportCompileDiagnostics()`、debugger diagnostics 推送和 `DumpAll` 的 `Diagnostics.csv` 都会看到历史错误，形成顺序相关的假失败或假污染。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 diagnostics 清理提升为 runtime-owned testing seam，并把它纳入 shared reset 的标准后置条件。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 增加完整的 diagnostics reset seam，例如 `ResetDiagnosticsForTesting()`，统一清空 `Diagnostics`、`LastEmittedDiagnostics` 并把 `bDiagnosticsDirty` 复位；不要继续让 test module 只知道 `ResetDiagnostics()` 这一半套接口。 2. 在 `ResetSharedCloneEngine()` 中于 module/raw-module cleanup 之后调用新的 diagnostics reset seam，使 `AcquireCleanSharedCloneEngine()`、`AcquireFreshSharedCloneEngine()` 和 `DestroySharedTestEngine()` 都获得同一份 engine-level 清理语义。 3. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 新增回归：先在 shared engine 上运行一次必然失败的 annotated compile，确认 `Engine.Diagnostics.Num() > 0`；随后调用 `AcquireCleanSharedCloneEngine()` 或 `ResetSharedCloneEngine()`，断言 `Diagnostics`、`LastEmittedDiagnostics` 与 dirty 标记全部回到空基线。 4. 再补一条 dump 回归：在制造失败 diagnostics 后执行 shared clean，再调用 `FAngelscriptStateDump::DumpDiagnostics()` 或 `DumpAll`，断言导出的 `Diagnostics.csv` 不再包含上轮失败文件。 5. 将 shared baseline 规则写入 `TESTING_GUIDE.md`：凡是宣称 `clean/fresh` 的 helper，必须同时恢复 modules、generated symbols、diagnostics 和 reload bookkeeping，而不是只清模块。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 如果直接在 test module 里硬清 runtime 内部字段，会进一步加深模块越层耦合；应优先把完整 reset seam 下沉到 runtime，再让 test helper 调用它。 |
| 前置依赖 | 建议结合已记录的 Issue-18 一并收口，避免继续扩大对 runtime internal field 的直接访问面。 |
| 验证方式 | 1. 新增 shared-diagnostics-reset 回归，验证失败编译后执行 `AcquireCleanSharedCloneEngine()` 会把 diagnostics 全部清空。 2. 串行运行 `Shared.EngineHelper.FailedAnnotatedModuleDoesNotPolluteLaterCompiles` 后接 `Dump.DumpAll.Summary` 或新的 dump diagnostics 回归，确认第二条测试看不到第一条的错误缓存。 3. 在同一进程内重复运行一条 broken compile 测试两轮，确认第二轮的 diagnostics 只包含本轮输入，而不是首轮残留。 |

### Issue-79：`ResetSharedCloneEngine()` 不清空 hot-reload bookkeeping，`SHARE_CLEAN` 仍会继承上一轮 reload 失败和队列状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp` |
| 行号 | 207-269, 378-432; 401-419, 477-479; 2270-2280, 2992-2994, 4048-4052, 4168-4180; 49-63; 60-88, 118-135; 54-72 |
| 问题 | shared reset 目前不会清 `FileHotReloadState`、`FileChangesDetectedForReload`、`FileDeletionsDetectedForReload`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles` 或 `LastFileChangeDetectedTime`。runtime 侧在 reload 失败时会把文件追加到 `PreviouslyFailedReloadFiles` / `QueuedFullReloadFiles`，下次 reload 前又会把这些旧文件重新并入 `FileList`；只要 `PreviouslyFailedReloadFiles.Num() > 0`，还会继续产出 “ANGELSCRIPT HOT-RELOAD FAILED -- KEEPING OLD CODE” 的消息。与此同时，测试模块已经直接暴露并使用这些状态：`FAngelscriptHotReloadTestAccess` 提供 `QueueFileChange()`、`GetQueuedFileChangeCount()`、`GetQueuedFullReloadCount()`；`HotReloadAnalysisTests.cpp` 和 `LearningHotReloadDecisionTraceTests.cpp` 则把 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` / `ResetSharedCloneEngine()` 当成 reload 分析前的干净基线。已验证事实：`ResetSharedCloneEngine()` 只清模块和 generated class，不碰上述任何 reload bookkeeping。 |
| 根因 | shared reset 把“清理 module graph”误当成“恢复 engine baseline”；但 hot-reload bookkeeping 的一部分是在 compile error 路径和 file-queue 路径写入的，未必能通过 `DiscardModule()` 间接回收。 |
| 影响 | 失败过一次的 reload 文件会被后续 `AnalyzeReloadFromMemory()` / `CheckForHotReload()` 自动重放；同一 shared engine 上的第二条 hot-reload 分析测试即使先走 `SHARE_CLEAN`，也可能继承上一轮的 `PreviouslyFailedReloadFiles` / `QueuedFullReloadFiles` / file-change 队列，出现顺序相关的分析结果漂移、额外错误消息或队列计数不归零。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 hot-reload bookkeeping 也纳入 shared baseline reset，并通过 hot-reload contract 回归显式锁住“clean means no pending reload history”。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 增加 test-only reset seam，例如 `ResetHotReloadStateForTesting()`，统一清空 `FileHotReloadState`、`FileChangesDetectedForReload`、`FileDeletionsDetectedForReload`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles` 和 `LastFileChangeDetectedTime`。 2. 在 `ResetSharedCloneEngine()` 中调用该 seam，使 `AcquireCleanSharedCloneEngine()`、`AcquireFreshSharedCloneEngine()` 与 `DestroySharedTestEngine()` 不再只清模块。 3. 在 `HotReload/AngelscriptHotReloadFunctionTests.cpp` 新增 shared-reset 回归：先通过 `FAngelscriptHotReloadTestAccess::QueueFileChange()` 注入一条 pending file change，再制造一次 `ErrorNeedFullReload` 或 `Error` 路径，把 `QueuedFullReloadFiles` / `PreviouslyFailedReloadFiles` 填满；执行 `ResetSharedCloneEngine()` 后断言三个队列计数都回到 0。 4. 在 `HotReload/AngelscriptHotReloadAnalysisTests.cpp` 或 `LearningHotReloadDecisionTraceTests.cpp` 增加顺序回归：先运行一条必然留下 reload failure history 的分析，再通过 `SHARE_CLEAN` 启动第二条 unrelated analysis，断言第二条的结果不再消费第一条遗留文件。 5. 将 `TESTING_GUIDE.md` 中 `SHARE_CLEAN/FRESH` 的说明补成“同时清 module、generated symbols、diagnostics、hot-reload bookkeeping”，避免 future test 继续把 clean 理解成仅清模块。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 如果 reset seam 直接清空 runtime 的 hot-reload 私有结构，但没有和真实 owner 生命周期对齐，可能掩盖某些产品级 reload 调试路径；应限制该 seam 仅在 test-owned/shared baseline 场景下调用，并保留 production engine 现有行为。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 hot-reload-reset 回归，验证 `ResetSharedCloneEngine()` 后 `GetQueuedFileChangeCount()`、`GetQueuedFullReloadCount()` 和 `PreviouslyFailedReloadFiles` 相关探针都回到 0。 2. 串行运行一条故意失败的 reload 分析测试后接一条 unrelated `AnalyzeReload.*` 或 learning trace，确认第二条结果不受第一条残留文件影响。 3. 人工检查或自动断言失败消息，确认 `SHARE_CLEAN` 之后不会继续冒出上一轮留下的 “ANGELSCRIPT HOT-RELOAD FAILED” 状态。 |

### Issue-80：`DumpAll` 回归只验证“文件存在”，没有受控内容契约，无法拦截 shared/prod 污染

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp` |
| 行号 | 45-96, 109-125, 202-247 |
| 问题 | 当前 `DumpAll` 两条 automation test 都建立在 ambient `production-like engine` 上：`RunDumpAll()` 先调用 `AcquireProductionLikeEngine()`，随后直接执行 `FAngelscriptStateDump::DumpAll()`。`DumpAll.EndToEnd` 只断言 27 个 CSV 文件“存在”；`DumpAll.Summary` 只断言每个表在 `DumpSummary.csv` 里有一行、状态匹配、并且行数 `>= 0`。也就是说，测试并不构造任何受控输入，也不验证 `Modules.csv`、`Diagnostics.csv`、`HotReloadState.csv` 等内容是否对应当前测试刚刚安排的状态，更不会验证这些表里没有前序测试遗留的脏数据。 |
| 根因 | dump 测试目前只把自己定位成 exporter wiring smoke，没有建立 test-owned engine fixture 和最小内容 contract；因此它更像“导出命令有没有返回文件”，而不是“导出的状态是否正确且隔离”。 |
| 影响 | 即使 shared/prod engine 里残留了旧模块、旧 diagnostics、旧 hot-reload 队列，`DumpAll.*` 也大概率仍然是绿色；这让 dump 入口无法对前两条状态泄漏形成回归保护，也让 `as.DumpEngineState` 这条关键交付入口缺少可信的内容级验证。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 保留现有存在性 smoke，但新增 test-owned dump fixture 和内容契约回归，显式验证最小可控状态。 |
| 具体步骤 | 1. 在 `Dump/AngelscriptDumpTests.cpp` 新增 test-owned fixture，例如 `FDumpContractFixture`：创建 owned full engine，显式编译一个已知模块、制造一条已知 diagnostics，并按需要注入一条已知 hot-reload queue/file-change 状态。 2. 新增 `DumpAll.ContentContract` 或等价测试，解析 `Modules.csv`、`Diagnostics.csv`、`DumpSummary.csv`，断言至少包含当前 fixture 刚写入的模块名/文件名/diagnostic message，且行数与 fixture 安排一致；不要再只看“非负”。 3. 如果按 Issue-79 增加了 hot-reload reset seam，再补一条 `HotReloadState.csv` contract：fixture 注入一条 pending file change，断言 dump 中只出现该路径，不包含历史文件。 4. 现有 `DumpAll.EndToEnd` 与 `DumpAll.Summary` 保留为 smoke，但名称或注释中明确它们只验证 exporter baseline；真正的内容正确性由新 contract 测试覆盖。 5. 在 `TESTING_GUIDE.md` 或 dump 测试注释中写明：dump/regression 变更至少要同时维护一条 “smoke exists” 和一条 “content contract” 回归，避免未来继续只有文件存在性检查。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 如果内容契约直接依赖 ambient production engine，测试会继续顺序相关；必须使用 owned fixture 或显式 baseline，不能把更多内容断言堆到现有 ambient smoke 上。 |
| 前置依赖 | 建议先完成 Issue-78 和 Issue-79 的 shared baseline reset，否则新 contract 只会稳定暴露已有脏状态，而难以区分是 dump 错还是输入不干净。 |
| 验证方式 | 1. 新增 dump content-contract 回归，验证 `Modules.csv`、`Diagnostics.csv` 和 `DumpSummary.csv` 至少对 fixture 安排的状态给出精确匹配。 2. 在 contract 测试前串行插入一条会制造 diagnostics 或 reload history 的 shared 测试，确认 owned dump fixture 仍只导出本测试状态。 3. 保留并回归现有 `DumpAll.EndToEnd` / `DumpAll.Summary`，确认 smoke 和 content contract 两条测试各自稳定。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-78 | Defect | 先补 shared diagnostics reset seam，修正 `clean/fresh` 仍继承旧编译错误的问题 |
| P1 | Issue-79 | Defect | 紧接着清空 hot-reload bookkeeping，把 reload 分析和 file-change 队列恢复成真正的干净基线 |
| P2 | Issue-80 | Architecture | 在前两条 reset contract 落地后补 dump content-contract 回归，让 central dump 入口真正能拦截状态泄漏 |

---

## 发现与方案 (2026-04-09 01:44)

### Issue-81：warning 回归按整个 `Engine.Diagnostics` 做全局模糊匹配，旧 warning 可让当前测试误报通过

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestMacros.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 13-27, 125-145; 34-40, 82-94; 4926-4933 |
| 问题 | `ContainsWarningDiagnostic()` 会遍历整个 `Engine.Diagnostics`，只按消息子串 `Contains(Needle)` 判断命中；`FAngelscriptControlFlowNotInitializedTest` 直接用它断言 `"may not be initialized"` warning 存在。与此同时，runtime 的 `ScriptCompileError()` 会把 warning 持续追加到 `Diagnostics`，而 `ASTEST_BEGIN_FULL` 的退出逻辑只 `DiscardModule()`，不会清 diagnostics；`ASTEST_CREATE_ENGINE_FULL()` 还把 full engine 放进 `static thread_local TUniquePtr`，使同线程前序 full 用例留下的 warning 能继续存活到后续测试。结果是，只要同一个 full engine 上曾出现过相同 warning 文本，`ControlFlow.NotInitialized` 就可能在当前输入没有产出目标 warning 时仍然通过。 |
| 根因 | warning 断言没有绑定到“本次编译对应的 source file/module”，而是把整台 engine 的 diagnostics map 当成单次测试结果；宏层 full engine 又不是测试末尾立即销毁，放大了这个误报窗口。 |
| 影响 | warning 类回归会从“验证当前编译行为”退化成“验证这个 engine 历史上出现过类似 warning”；一旦后续再新增 warning 断言测试，顺序相关的假通过会继续扩散，削弱测试对编译器回归的拦截能力。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 warning 断言改成 filename-scoped contract，只读取当前模块对应文件的 diagnostics。 |
| 具体步骤 | 1. 在 runtime 或 shared helper 层新增只读测试 seam，例如 `GetDiagnosticsForFileForTesting(const FString& AbsoluteFilename)` / `ContainsDiagnosticForFileForTesting(...)`，避免每个测试继续手写全局 map 扫描。 2. 将 `FAngelscriptControlFlowNotInitializedTest` 改为先通过 `Engine.GetModuleByModuleName(TEXT("ASControlFlowNotInitialized"))` 取到当前 `ModuleDesc`，再使用 `ModuleDesc->Code[0].AbsoluteFilename` 对应的 file-scoped helper 断言 warning；删除 `ContainsWarningDiagnostic()` 这种跨文件搜索。 3. 对需要验证 warning 的用例，在编译前显式建立 diagnostics 基线：优先使用新的 snapshot/reset seam，至少保证测试读取的是“本次编译后、当前文件”的 diagnostics，而不是整个 engine 历史。 4. 在 `Shared/AngelscriptTestEngineHelperTests.cpp` 或 `AngelscriptControlFlowTests.cpp` 新增负向回归：先在同一个 full engine 上编译一个会产生相同 warning 文本的 unrelated 脚本，再编译目标脚本；断言 helper 只会报告目标文件自己的 warning，旧文件 warning 不能让当前测试通过。 5. 若后续仍需保留 diagnostics 遍历逻辑，把它收敛到单一 helper，并在注释中写明“禁止直接遍历 `Engine.Diagnostics` 做跨文件 contains 断言”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptControlFlowTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 预估工作量 | S |
| 风险 | 如果 helper 继续依赖不稳定的虚拟路径或 path-derived module name，改成 file-scoped 后可能出现假阴性；实现时必须使用 `ModuleDesc->Code[0].AbsoluteFilename` 这类真实编译输入路径。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 warning-file-scope 回归，先注入 unrelated warning，再验证 `ControlFlow.NotInitialized` 只认可当前文件的 warning。 2. 回归 `Angelscript.TestModule.Angelscript.ControlFlow.NotInitialized`，确认在顺序互换后结果一致。 3. 若新增 runtime seam，补一条 seam 自测，验证按 filename 查询时不会返回其它文件的 diagnostics。 |

### Issue-82：hot-reload 函数回归在 live generated `UObject` 仍存活时执行 `DiscardModule`/shared reset

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | 338-342, 398-412, 429-432, 476-501; 207-269 |
| 问题 | `FAngelscriptHotReloadModifyLookupFlowTest` 在 `NewObject<UObject>(..., ClassAfterReload)` 创建 generated object 后，仍持有 `TestObject` 和 `GetValueFunction`，就直接执行 `Engine.DiscardModule(*ModuleName.ToString())`；`FAngelscriptHotReloadFailureKeepsOldCodeTest` 则在 `ON_SCOPE_EXIT` 里先 `DiscardModule()`，再 `ResetSharedInitializedTestEngine(Engine)`，而 `TestObject` 是从 generated class 实例化的 `UObject`，直到函数退出前都没有显式销毁或标记回收。`ResetSharedCloneEngine()` 本身会 discard module、清 detached generated symbol 并立刻 `CollectGarbage()`。这意味着两条 hot-reload 回归都在 generated `UObject` 仍然存活时回收其 class/module 所属的 shared engine 状态。 |
| 根因 | 这些测试把“验证运行时对象行为”和“回收 generated module/engine baseline”放在同一层作用域里，但没有给 generated object 定义先销毁、后 reset 的 teardown 顺序；`UObject*` 局部指针离开作用域也不会主动销毁底层对象。 |
| 影响 | 测试可能在 object 仍引用 generated class/function 时就让 module descriptor、generated symbol 和 shared engine baseline 进入 cleanup，形成 stale class/function 引用、GC 顺序相关问题，以及后续 generated-class/hot-reload 测试的顺序污染；这类问题不会稳定复现，最容易演化成 flaky。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 hot-reload generated object 引入显式 fixture，把 teardown 顺序固定为“释放实例 → 验证已回收 → discard/reset engine”。 |
| 具体步骤 | 1. 在 `HotReload` 或 `Shared` 层新增小型 fixture（例如 `FGeneratedHotReloadObjectFixture`），集中持有 `FAngelscriptEngine&`、`ModuleName`、`TWeakObjectPtr<UObject>` 和可选 `UFunction*`；fixture 的析构必须先把 test-owned object 标记为可回收并触发一次 `CollectGarbage(RF_NoFlags, true)`，随后再执行 `DiscardModuleChecked` / `ResetSharedInitializedTestEngine`。 2. 迁移 `FAngelscriptHotReloadModifyLookupFlowTest`：去掉测试体内的裸 `Engine.DiscardModule()`，把 `TestObject`、`GetValueFunction` 和执行断言包进 fixture 或内层作用域，在 object 明确释放后再做 module cleanup。 3. 迁移 `FAngelscriptHotReloadFailureKeepsOldCodeTest`：把当前 `ON_SCOPE_EXIT` 改为调用 fixture teardown，不再在 live `TestObject` 仍存在时直接 reset shared engine。 4. 在 teardown 前增加显式 postcondition：通过 `TWeakObjectPtr` 或 `TObjectIterator<UObject>` 断言该 generated class 的测试实例已不可达/数量归零，然后才允许 `ResetSharedInitializedTestEngine` 继续执行。 5. 若测试基础设施需要继续支持“object 仍活着时尝试 reset”的场景，则在 `ResetSharedCloneEngine()` 侧新增检测并报错，禁止继续静默 GC。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果简单地对任意 generated object 调 `MarkAsGarbage()`，可能掩盖真实 world-owned 生命周期问题；fixture 只能处理这些测试自己在 `GetTransientPackage()` 下创建、且没有外部 owner 的 test-owned object。 |
| 前置依赖 | 建议复用已记录的 checked cleanup 方案，避免 `DiscardModule()` 失败继续被静默吞掉。 |
| 验证方式 | 1. 为 `ModifyLookupFlow` / `FailureKeepsOldCode` 增加 teardown-order 回归，断言 generated object 已失效后才发生 `DiscardModule`/reset。 2. 串行运行这两条 hot-reload 测试后接一条 generated-class 查找或 shared reset 回归，确认后者不再看到前者遗留实例。 3. 回归 `Shared.EngineHelper.ResetSharedEngineReleasesGeneratedComponentClasses`，确认新的对象 teardown 顺序与现有 component 回归保持一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-82 | Defect | 先修 hot-reload generated object 的 teardown 顺序，避免 shared reset 在 live 实例仍存活时运行 |
| P2 | Issue-81 | Defect | 随后收紧 warning 断言到 filename-scoped diagnostics，消除编译 warning 回归的假通过 |

---

## 发现与方案 (2026-04-09 02:04)

### Issue-83：`Preprocessor` fixture 写盘失败会静默回退到旧文件内容

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 23-33, 82-88, 128-139, 177-192; 116-137 |
| 问题 | `WriteFixtureFile()` 只调用 `FFileHelper::SaveStringToFile(Contents, *AbsolutePath)`，但完全忽略返回值，然后无条件把固定路径返回给调用方。3 个 `Preprocessor` 用例都复用固定文件名，如 `Tests/Preprocessor/BasicModule.as`、`MacroActor.as`、`Shared.as`、`UsesImport.as`。runtime 侧 `FAngelscriptPreprocessor::AddFile()` 随后会直接从该路径 `LoadFileToString()`；如果写盘失败但旧文件仍留在磁盘上，测试会继续读取上一次运行留下的内容，而不是本次构造的脚本。 |
| 根因 | fixture helper 把“生成测试输入文件”设计成 fire-and-forget 工具，没有把写盘成功作为测试前置条件，也没有为每次运行提供独立目录或内容回读校验。 |
| 影响 | 只要遇到临时文件锁、只读目录、杀毒占用或其它 I/O 异常，`Preprocessor.BasicParse`、`MacroDetection`、`ImportParsing` 就可能在错误输入上继续执行，并以旧文件内容产生假通过或误导性失败，破坏测试对真实预处理行为的验证价值。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 fixture 写盘升级为 checked helper，并为每次测试使用独立输出目录，彻底切断“写失败后读取旧文件”的回退路径。 |
| 具体步骤 | 1. 将 `WriteFixtureFile()` 改为返回 `bool`，签名调整为 `bool WriteFixtureFile(const FString& RelativeScriptPath, const FString& Contents, FString& OutAbsolutePath)`，在 `MakeDirectory()` 和 `SaveStringToFile()` 任一步失败时立即返回 `false` 并让调用测试 `AddError`。 2. 为 `Preprocessor` 测试引入 per-test root，例如 `Saved/Automation/PreprocessorFixtures/<TestName or Guid>/...`，不要继续把所有运行写到同一个 `PreprocessorFixtures` 目录。 3. 在 `BasicParse`、`MacroDetection`、`ImportParsing` 三个用例里，把当前直接接收路径的写法改成“先 `TestTrue(WriteFixtureFile(...))`，成功后再 `Preprocessor.AddFile(...)`”。 4. 可选但建议补一条 lightweight 回读断言：保存后立刻 `LoadFileToString()` 校验文件内容与写入文本一致，再进入 `Preprocess()`，这样能尽早暴露编码或截断问题。 5. 为新 helper 增加失败路径回归，例如传入不可写目录或注入 test seam，让测试确认写盘失败时用例会立即失败，而不是继续读取历史文件。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | S |
| 风险 | 如果只改成 checked save，但仍保留共享根目录，历史文件仍可能在其它并行/交错测试间被复用；因此写盘检查和独立目录应同批落地。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 回归 `Angelscript.TestModule.Preprocessor.BasicParse`、`Angelscript.TestModule.Preprocessor.MacroDetection`、`Angelscript.TestModule.Preprocessor.ImportParsing`。 2. 新增负向回归，模拟 `WriteFixtureFile()` 失败后确认测试直接报错，不会进入 `Preprocess()`。 3. 在同一进程内连续两次运行 `ImportParsing`，第二次修改输入脚本内容后断言结果来自新文件，而不是首轮残留内容。 |

### Issue-84：`Preprocessor` 测试没有显式绑定 `current engine`，读取和修改的可能不是同一台引擎

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | 13-20, 74-112, 120-161, 169-219; 40-41, 59-67, 214, 232, 4234-4294; 179-199, 453-473 |
| 问题 | `GetEngineForPreprocessorTests()` 只返回一个 `FAngelscriptEngine*`，但没有像其它 helper 一样建立 `FAngelscriptEngineScope`。随后 `FAngelscriptPreprocessor` 在构造和 `Preprocess()` 期间却反复读取 `FAngelscriptEngine::Get()` / `ShouldUseAutomaticImportMethodForCurrentContext()` / `ShouldUseEditorScriptsForCurrentContext()`，错误上报也写回当前 engine 的 diagnostics。更具体地，`ImportParsing` 测试在第 195 行直接用 `TGuardValue<bool>(Engine->bUseAutomaticImportMethod, false)` 修改拿到的 `Engine`，而 `Preprocess()` 第 232 行读取的却是“当前 scope 顶部 engine”的 automatic-import 状态。只要两者不是同一实例，测试改的是 A，引擎真正用的是 B。 |
| 根因 | 该测试文件复用了一个“拿 engine 指针即可使用”的轻量模式，但 preprocessor 实现本身是显式依赖 ambient `current engine` 的；测试入口没有把“拿到的 engine”和“current engine scope”绑定成同一个 fixture。 |
| 影响 | 当进程里已存在其它 `FAngelscriptEngineScope` 时，`Preprocessor.BasicParse`、`MacroDetection`、`ImportParsing` 可能读取错误的 editor/cooked/import 配置，并把 diagnostics 写进外层 engine；这会导致顺序相关的假失败、假通过，以及定位时看到错误 engine 上的编译消息。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `Preprocessor` 测试引入显式的 scoped-engine fixture，保证“测试修改的 engine”与“runtime 读取的 current engine”永远是同一个对象。 |
| 具体步骤 | 1. 用小型 fixture 替换 `GetEngineForPreprocessorTests()`，例如 `FScopedPreprocessorTestEngine`：内部要么通过 `AcquireProductionLikeEngine()` 获取带 scope 的 production-like engine，要么获取 shared engine 后再显式持有本地 `FAngelscriptEngineScope`，并在析构时恢复上下文。 2. 将三个 `RunTest()` 全部改成通过 fixture 取 `FAngelscriptEngine& Engine`，禁止继续只拿裸指针。 3. 把 `ImportParsing` 中对 `Engine->bUseAutomaticImportMethod` 的直接字段写入改成 runtime 已提供的 `SetAutomaticImportMethodForTesting(false)`，并在退出时恢复原值；`bUseEditorScripts` 同理优先走 `SetUseEditorScriptsForTesting()`。 4. 新增负向回归：先建立一个 unrelated outer `FAngelscriptEngineScope`，再运行 `ImportParsing` 或最小化 helper，断言 `Preprocessor` 使用的 `ConfigSettings`、automatic-import 决策和 diagnostics 都来自 fixture engine，而不是 outer engine。 5. 在 `TESTING_GUIDE.md` 补一条硬规则：凡调用 `FAngelscriptPreprocessor` 的测试，必须显式持有 `FAngelscriptEngineScope` 或使用封装好的 fixture，不得只依赖 ambient current engine。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 如果简单把 shared persistent scope 当成 fixture 使用，而不恢复 outer scope，仍可能把其它测试的 ambient context 覆盖掉；因此需要真正的本地 scope owner，而不是继续依赖全局 persistent scope 副作用。 |
| 前置依赖 | 建议与 Issue-83 同批完成，这样新 fixture 可以同时接管目录和 engine scope 两类前置条件。 |
| 验证方式 | 1. 新增 scope-mismatch 回归，先压入 outer engine，再运行 `Preprocessor.ImportParsing`，确认 diagnostics 和 automatic-import 决策只落在 fixture engine 上。 2. 回归 `Angelscript.TestModule.Preprocessor.BasicParse`、`MacroDetection`、`ImportParsing`，确认顺序互换后结果一致。 3. 若引入 `SetAutomaticImportMethodForTesting()` 路径，补一条 helper 自测确认测试结束后原始 engine 配置被恢复。 |

### Issue-85：`DumpAll.Summary` 用例的 CSV 解析器不支持合法的引号/换行字段

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptStateDump.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptCSVWriter.h` |
| 行号 | 127-145, 175-199, 219-247; 808-837; 65-95 |
| 问题 | `ParseDumpSummary()` 先用 `ParseIntoArrayLines()` 切行，再用 `ParseIntoArray(..., TEXT(\",\"), false)` 直接按逗号分列。这个解析逻辑与同文件前面的 `FAngelscriptCSVWriterEscapingTest` 相矛盾：`FCSVWriter::EscapeField()` 明确支持带逗号、双引号、`\n`、`\r` 的合法 CSV 字段，而 `DumpSummary.csv` 又会把 `ErrorMessage` 原样作为第 4 列写出。结果是，一旦某个 summary row 的 `ErrorMessage` 含逗号、引号或换行，`ParseDumpSummary()` 就会把单条记录拆成多列或多行，轻则读错 `RowCount/Status`，重则直接丢失整行。 |
| 根因 | 测试文件为 `DumpSummary.csv` 自己实现了一套“按行 + 按逗号切分”的简化 parser，但没有复用 `FCSVWriter` 的 escaping contract，也没有提供对应的 quoted-field reader。 |
| 影响 | `DumpAll.Summary` 无法可靠验证 failure-path 或 partial-export 路径下的真实 summary 内容；未来只要 `ErrorMessage` 变得更接近真实用户输入或底层错误文本，测试就可能在 exporter 正确的情况下误报失败，或者把错误行解析错位后继续误报通过。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用 quote-aware CSV reader 替换 `ParseDumpSummary()`，并把 `Summary` 自测扩展到特殊字符错误信息。 |
| 具体步骤 | 1. 在 `Dump/AngelscriptDumpTests.cpp` 内提取一个最小 CSV parser helper，至少正确处理双引号转义、逗号分隔和跨行字段；或者更好地，把 parser 放到 `Dump` 共享 helper 中，避免未来再次手写 `ParseIntoArrayLines()+ParseIntoArray()`。 2. 将 `ParseDumpSummary()` 的返回类型从 `TPair<int32, FString>` 升级为结构体，显式保存 `RowCount`、`Status`、`ErrorMessage`，让 parser 不再靠“忽略第 4 列”侥幸工作。 3. 为 `DumpSummary` parser 新增直接单测：用 `FCSVWriter` 生成一份包含 `Comma,Value`、`Quote \"Here\"`、`Line1\\nLine2` 的 summary 文本，再断言 parser 能无损还原各列。 4. 将 `FAngelscriptStateDumpSummaryTest` 扩展为至少验证一条含非空 `ErrorMessage` 的 row，不要只看 `Status` 和 `RowCount`；这样 parser 回归会在 `Dump` 测试层被直接拦截。 5. 在 dump 测试注释或 `TESTING_GUIDE.md` 中写明：凡解析 plugin 导出的 CSV，必须使用支持 quoted field 的解析 helper，不允许再用裸 `ParseIntoArrayLines()` 处理通用 CSV。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Dump/AngelscriptDumpTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Dump/AngelscriptCSVWriter.h`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | S |
| 风险 | 如果 parser 只修 `DumpSummary.csv`，而不沉淀为共享 helper，后续其它 dump CSV 回归仍可能继续复制错误的解析方式；应尽量把 quote-aware 解析抽成复用点。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 parser 单测，覆盖逗号、引号、换行三类字段。 2. 回归 `Angelscript.TestModule.Dump.CSVWriter.SpecialCharacters` 与 `Angelscript.TestModule.Dump.DumpAll.Summary`，确认 writer 和 reader 对同一 CSV 契约一致。 3. 构造至少一条带非空 `ErrorMessage` 的 summary 输入，断言 `RowCount`、`Status`、`ErrorMessage` 全部精确还原。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-84 | Defect | 先把 `Preprocessor` 测试改成 scoped-engine fixture，切断 current-engine 与测试目标 engine 错位的问题 |
| P2 | Issue-83 | Defect | 随后收紧 `Preprocessor` fixture 的写盘契约和独立目录，避免旧文件内容污染本轮输入 |
| P2 | Issue-85 | Defect | 最后修复 `DumpSummary` 的 CSV reader，使 dump 回归在 failure-path 上也能稳定解析合法输出 |
