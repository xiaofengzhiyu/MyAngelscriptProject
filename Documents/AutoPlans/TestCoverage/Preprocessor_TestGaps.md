# Preprocessor 测试覆盖缺口

---

## 测试审查 (2026-04-08 13:10)

### 一、现有测试问题

#### Issue-1：唯一的 `import` 用例主动关闭 automatic import，默认路径完全未被审查

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.ImportParsing` |
| 行号范围 | 169-219 |
| 问题描述 | 用例在 194-195 行通过 `TGuardValue<bool> AutomaticImportGuard(Engine->bUseAutomaticImportMethod, false);` 强制关闭 automatic import，然后只验证手写 `import` 被记录并从 `Code[0].Code` 中移除。源码 `Preprocess()` 在 `AngelscriptPreprocessor.cpp:232-239` 明确把 automatic import 作为分叉条件，但现有唯一 `import` 测试把默认分支整个绕开了。 |
| 影响 | 默认配置下手写 `import` 的 warning、ignored 语义、以及 automatic import 与 manual import 的互斥合同都没有任何回归保护。当前测试即使全绿，也不能说明默认预处理路径是正确的。 |
| 修复建议 | 保留现有用例专门验证 manual import 排序，但新增一个不改 `bUseAutomaticImportMethod` 的默认路径用例，至少断言 `Preprocess()` 的返回值、`ImportedModules` 是否变化、`Code[0].Code` 是否保留/移除原始 `import`、以及诊断里是否出现 manual import warning。两个用例都应显式创建 `FAngelscriptEngineScope` 或改用 `FAngelscriptTestFixture`，避免继续依赖环境默认值。 |

#### Issue-2：`MacroDetection` 只按名字找宏，无法验证宏解析结果是否完整

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.MacroDetection` |
| 行号范围 | 120-161 |
| 问题描述 | 用例只把 `GatherMacros()` 的结果做 `ContainsByPredicate`，确认存在名为 `Mesh` 的 `Property` 宏和名为 `BeginPlay` 的 `Function` 宏；但脚本里同时提供了 `UPROPERTY(EditAnywhere, BlueprintReadWrite)`、`UFUNCTION(BlueprintOverride)`、属性类型 `UStaticMesh` 和明确的源码行号。当前断言完全不检查 `Macro.Arguments`、`SubjectType`、`FileLineNumber`、宏总数，也不检查是否错误地多解析出额外宏。 |
| 影响 | 只要预处理器还能产出两个“名字对上”的宏，这个测试就会通过；即便参数串被截断、类型解析错误、行号漂移、或把宏附着到错误 chunk，上游回归也不会被发现。 |
| 修复建议 | 把断言改成“精确结构”验证：断言总宏数恰好为 2；`Mesh` 宏的 `Type == Property`、`Arguments == "EditAnywhere, BlueprintReadWrite"`、`SubjectType == "UStaticMesh"`、`FileLineNumber` 对应属性声明；`BeginPlay` 宏的 `Type == Function`、`Arguments == "BlueprintOverride"`、`FileLineNumber` 对应函数声明。若项目允许，直接遍历 `ChunkedCode` 断言两个宏落在同一 `Class` chunk，避免 `GatherMacros()` 把 chunk 关联信息抹平。 |

#### Issue-3：`Preprocessor` 三个用例都依赖“当前正在运行的引擎”，测试环境不稳定

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.BasicParse` / `Angelscript.TestModule.Preprocessor.MacroDetection` / `Angelscript.TestModule.Preprocessor.ImportParsing` |
| 行号范围 | 13-21，74-91，120-143，169-198 |
| 问题描述 | 三个用例统一通过 `GetEngineForPreprocessorTests()` 先偷用运行中的 production engine，拿不到时再退回 `GetOrCreateSharedCloneEngine()`。这意味着测试会随外部 editor/runtime 上下文改变 `PreprocessorFlags`、settings 和 import 模式；而 shared clone 分支又不会做 reset，上一条测试残留的 engine 状态会直接复用到下一条。 |
| 影响 | 同一份脚本在不同执行环境下可能走到不同的预处理分支，导致结果依赖测试顺序和宿主环境。CI 全绿并不能证明用例是自洽的，尤其对 `automatic import`、`EDITOR` 条件和配置默认值这类上下文敏感逻辑更危险。 |
| 修复建议 | 不再回退到 production engine，改用 `FAngelscriptTestFixture` 或 `AcquireCleanSharedCloneEngine()` 固定测试引擎来源；每个用例显式建立 `FAngelscriptEngineScope`，并在需要改动 `bUseAutomaticImportMethod` 或其他配置时局部 guard 后恢复。若必须覆盖 production-like 上下文，应拆成独立 scenario 测试，而不是让基础单元测试自动复用宿主引擎。 |

#### Issue-4：`CompilerPipeline` 全文件使用 `SHARE` 引擎但从不清理，反射状态会跨用例泄漏

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.*`（8 个用例） |
| 行号范围 | 16-498 |
| 问题描述 | 8 个用例全部用 `ASTEST_CREATE_ENGINE_SHARE()` + `ASTEST_BEGIN_SHARE`。而 `AngelscriptTestMacros.h:42-47,97-104` 已经明确 `SHARE` 是“process-level singleton, reused across tests, no reset”。这些用例又都会向引擎注册 `UClass`、`UDelegateFunction`、`FAngelscriptEnumDesc` 和活跃模块，但 `ASTEST_END_SHARE` 不会丢弃模块、也不会清空共享引擎。 |
| 影响 | 任何基于 `Engine.GetDelegate()`、`FindGeneratedClass()`、`Engine.GetEnum()` 的断言都可能命中上一轮残留对象，造成假阳性；同时它完全掩盖了模块销毁、旧类型清理、重复编译覆盖等回归。作为“编译管线”测试，这种隔离方式不足以证明当前脚本真的是由本次编译产生的。 |
| 修复建议 | 默认改成 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `ASTEST_CREATE_ENGINE_CLONE()`；需要完整反射清理语义时优先用 `FULL/CLONE`。如果确实要保留 `SHARE`，则在每个用例结束前显式丢弃本次模块并清理生成类型，至少保证 `Engine.GetActiveModules()`、raw AS modules 和 generated class 查找不会跨测试复用旧状态。 |

#### Issue-5：`CompilerPipeline` 大多数用例只看反射元数据，没有证明“源码 -> 可执行字节码”真的跑通

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateEnumClassCompile` / `PropertyDefaultsCompile` / `GeneratedClassConsistency` / `EnumAvailability` / `DelegateSignatureConsistency` / `ClassLikeReflectionShape` |
| 行号范围 | 16-82，168-222，229-270，329-364，371-413，420-499 |
| 问题描述 | 这 6 个用例都以 `CompileAnnotatedModuleFromMemory()` 返回 `true` 为起点，然后只检查 `FindGeneratedClass()`、`Engine.GetDelegate()`、`Engine.GetEnum()` 或属性类型；没有任何一个去执行脚本入口、调用生成函数、或验证 `asIScriptModule` 里的函数体能运行。相比之下，同文件只有 2 个用例会真正执行 `Entry()`。 |
| 影响 | 编译阶段如果出现“元数据注册成功，但函数体/默认值初始化/生成代码没有进入可执行模块”的回归，这 6 个测试仍然会全绿。文件名叫 `CompilerPipeline`，但现有断言更接近“reflection shape smoke test”，对完整管线的保护明显不足。 |
| 修复建议 | 为每个 annotated case 增加最小可执行探针：全局 `Entry()`、类实例方法、默认值读取函数、delegate/event 触发函数，至少验证一个最终字节码路径。若不方便在同一个脚本里执行全部对象，可把当前 6 个用例拆成“反射形状”与“可执行行为”两层，并用 `ExecuteIntFunction()` 或 `ExecuteGeneratedIntEventOnGameThread()` 对应断言返回值。 |

#### Issue-6：`PropertyDefaultsCompile` 写了两个 default 场景，却只验证了 `Score`

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.PropertyDefaultsCompile` |
| 行号范围 | 168-222 |
| 问题描述 | 脚本体同时包含 `default Score = 21;` 和 `default Tags.Add(n"Alpha");` 两条默认值语句，但测试只读取 `FIntProperty* ScoreProperty` 并断言 `Score == 21`。`Tags` 数组属性既没有 `FindFProperty`，也没有检查 CDO 中是否真的追加了 `Alpha`。 |
| 影响 | 只要整数默认值路径仍然工作，这个测试就会通过；`default` 语句里对容器追加、复杂初始化或多条默认值顺序的回归完全不会被发现。当前用例表面上覆盖“property defaults”，实际上只保护了最简单的标量分支。 |
| 修复建议 | 补充 `FArrayProperty` / `FNameProperty` 断言，读取 `DefaultObject` 上的 `Tags` 容器并验证长度为 1、内容为 `Alpha`。更稳妥的做法是在脚本里增加一个 `int Entry()` 或实例方法返回 `Tags.Num()` / `Tags[0]` 的可执行探针，把“默认值写入 CDO”与“脚本运行期可见默认值”一起锁住。 |

#### Issue-7：`BasicParse` 只验证“输出里还含有函数名”，对预处理结果几乎没有约束

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.BasicParse` |
| 行号范围 | 74-113 |
| 问题描述 | 用例在 `Preprocess()` 成功后，只断言模块数为 1、模块名被规范化、代码段数为 1，以及 `Code[0].Code.Contains("ReturnSeven")`。它没有验证 `return 7;` 是否保留、`ImportedModules` 是否为空、`RelativeFilename/AbsoluteFilename` 是否正确、也没有检查 `ChunkedCode` 或最终代码能否被下游编译执行。 |
| 影响 | 只要预处理输出里还残留 `ReturnSeven` 这个名字，哪怕函数体被破坏、被重复包裹、或额外插入了错误文本，这个测试依然会通过。它对最基础的“单文件无宏脚本应产生干净 code section”合同保护很弱。 |
| 修复建议 | 至少把断言升级为：`ImportedModules.Num() == 0`、`Module.Code[0].RelativeFilename`/`AbsoluteFilename` 与 fixture 对齐、`Code[0].Code` 精确包含 `return 7;` 且不包含多余前导 directive。若希望验证与编译管线的接口契约，可在同一脚本上追加一个轻量 compile/execute 断言，确保预处理输出可被后续 builder 正常消费。 |

#### NewTest-1：循环 `import` 必须给出稳定错误链

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessImports` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 循环依赖是 import 图的核心失败路径；如果没有测试，错误链内容、失败时机和失败后的状态污染都可能回归而 CI 无法发现。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Import.CircularDependencyReportsChain` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp` |
| 场景描述 | 构造 `A.as -> import B`、`B.as -> import A`，在 manual import 模式下运行预处理。 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或 `AcquireCleanSharedCloneEngine()`；通过 `TGuardValue<bool>` 关闭 automatic import；写入两份 fixture 文件并分别 `AddFile()`。 |
| 期望行为 | `Preprocess()` 返回 `false`；`Engine.Diagnostics` 中包含 `Detected circular import`，且错误链至少列出两个模块名；失败后不得继续把模块送入编译 helper。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P0 |

#### NewTest-2：默认 automatic import 路径必须覆盖手写 `import` 的兼容语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::Preprocess` / `FAngelscriptPreprocessor::ProcessImports` |
| 现有测试覆盖 | 有测试但缺少默认 automatic import 场景 |
| 风险评估 | 当前唯一 import 用例只覆盖 manual 模式，默认配置下 manual import 的 warning/ignored 行为会长期处于盲区。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Import.AutomaticModeManualImportCompatibility` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp` |
| 场景描述 | 在不修改 `bUseAutomaticImportMethod` 的前提下，构造一个包含手写 `import Shared;` 的模块，并同时提供被导入模块。 |
| 输入/前置 | 使用 clean engine；根据当前环境默认值保持 automatic import 开启；开启诊断采集；两份 fixture 文件分别加入 preprocessor。 |
| 期望行为 | 断言 `Preprocess()` 的结果与产品合同一致；同时精确检查 `ImportedModules`、`Code[0].Code` 中的原始 `import` 文本是否被保留/移除，以及 `Engine.Diagnostics` 是否出现 manual import compatibility warning。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P0 |

#### NewTest-3：缺失分号的 `import` 必须在预处理阶段报清晰错误

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::ProcessImports` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | `import` 漏分号是高频笔误；若没有专门测试，错误很容易退化成误导性的“找不到模块”或把后续源码吞进模块名。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Import.MissingSemicolonReportsSyntax` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp` |
| 场景描述 | 构造 `import Tests.Preprocessor.Shared` 后直接换行并继续写函数定义，故意省略分号。 |
| 输入/前置 | clean engine；manual import 模式；提供被导入模块和导入方模块；预处理前清空 diagnostics。 |
| 期望行为 | `Preprocess()` 返回 `false`；诊断定位到 `import` 所在行，而不是报一个超长模块名；`ImportedModules` 不应记录畸形模块名；`Code[0].Code` 中也不应残留半截 import 污染。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P0 |

#### NewTest-4：失活分支里的未知条件不应被求值

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::ParsePreProc` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 条件编译短路错误会让本应不可达的脚本分支把整个预处理打红，是典型的错误恢复缺口。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Directives.InactiveBranchSkipsUnknownConditions` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` |
| 场景描述 | 构造 `#if EDITOR` 的活跃分支返回 `7`，在 `#else` 中嵌套 `#if UNKNOWN_FLAG` 或 `#elif UNKNOWN_FLAG`，保证未知 flag 位于不可达分支。 |
| 输入/前置 | clean engine；使用当前 editor 环境默认 `EDITOR` flag；脚本同时包含 `Entry()` 以便后续执行。 |
| 期望行为 | `Preprocess()` 和后续编译都应成功；`ExecuteIntFunction()` 返回 `7`；diagnostics 中不应出现 `Invalid preprocessor condition`；最终代码中也不应残留 dead branch 的 `UNKNOWN_FLAG` 文本。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `CompileModuleWithSummary` / `ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-5：`#include` 需要有明确且稳定的合同测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 用户能在脚本里自然写出 `#include`，但现有自动化完全不说明它是支持、忽略还是报错；这会让 include 相关回归和诊断漂移长期无人发现。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Directives.IncludeDirectiveProducesDeterministicResult` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` |
| 场景描述 | 在脚本头部写 `#include \"Shared.as\"`，并提供一个简单 `Entry()`；根据产品合同验证 include 的处理结果。 |
| 输入/前置 | clean engine；使用 `CompileModuleWithSummary` 收集预处理/编译诊断；如合同要求 include 不支持，则只需单文件脚本。 |
| 期望行为 | 必须固定成二选一的断言：若产品决定支持 include，则断言 include 文件内容被纳入并可执行；若产品决定不支持，则断言 compile/preprocess 明确失败，诊断里包含 `#include` 和具体行号，而不是把原始文本静默流入后续编译。 |
| 使用的 Helper | `CompileModuleWithSummary` |
| 优先级 | P1 |

#### NewTest-6：空脚本编译失败后不能污染后续正常编译

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `BuildModulesForSummary` / `PreprocessAndCompile` / `FAngelscriptPreprocessor::Preprocess` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 空文件是最基础的边界输入；若失败路径会残留 module/diagnostic 状态，后续正常编译就可能出现假红或假绿。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.EmptySourceFailsWithoutStateLeak` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineFailureTests.cpp` |
| 场景描述 | 先对空字符串脚本调用 `CompileModuleWithSummary`，随后在同一 clean engine 上编译一个简单返回 `42` 的正常模块。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `FAngelscriptTestFixture`；第一次输入为空脚本，第二次输入为带 `Entry()` 的最小模块。 |
| 期望行为 | 第一次编译应失败，`CompileResult == Error`、`CompiledModuleCount == 0`、diagnostics 非空；第二次编译应成功并可 `ExecuteIntFunction()` 返回 `42`，证明失败路径没有污染引擎状态。 |
| 使用的 Helper | `CompileModuleWithSummary` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-7：超长脚本文件应保持可预处理、可编译且结果稳定

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::CondenseFromChunks` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 长文件会放大 chunk 切分、字符串替换和 import/宏扫描的边界错误；没有压力型回归时，长度相关 bug 很容易只在大项目里出现。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Stress.LongSourceRemainsDeterministic` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorStressTests.cpp` |
| 场景描述 | 在测试里程序化拼接一个超长脚本：大量无宏函数 + 尾部 `Entry()` 调用最后一个函数，必要时插入少量注释和空白行。 |
| 输入/前置 | clean engine；生成长度至少数万字符的脚本文本；先直接 `Preprocess()`，再走编译执行。 |
| 期望行为 | `Preprocess()` 成功、只产出一个模块、`Code[0].Code.Len()` 大于设定阈值且不为空；随后编译成功并执行 `Entry()` 返回最后一个函数的预期值，证明长输入没有破坏 chunk 拼接或函数体顺序。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `CompileModuleWithSummary` / `ExecuteIntFunction` |
| 优先级 | P2 |

#### NewTest-8：`Helper_CommentFormat.h` 需要独立锁住注释规范化合同

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/Helper_CommentFormat.h` |
| 关联函数 | `FormatCommentForToolTip` / `IsLineSeparator` / `IsAllSameChar` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 该 helper 会直接影响 class/function/property/enum 的 tooltip 文本；没有独立测试时，注释剥离和格式化回归会在多个反射路径同时出现，但根因难以定位。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.CommentFormatting.TooltipNormalization` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorCommentFormatTests.cpp` |
| 场景描述 | 直接调用 `FormatCommentForToolTip`，覆盖 JavaDoc、C-style、CPP-style、`/*~` / `//~` 忽略块、全分隔线、以及纯 CJK 注释输入。 |
| 输入/前置 | 构造 4-6 组字符串样例，例如带 `/** ... */` 的多行注释、含 `//~` 的忽略段、仅由 `====` 组成的分隔线、以及中文 tooltip 文本。 |
| 期望行为 | 逐条断言格式化后的精确字符串：注释标记被去掉、忽略块被删除、分隔线被裁掉、内部换行保留、纯 CJK 内容不会被错误清空。 |
| 使用的 Helper | 其他（纯 Automation test，直接调用 `FormatCommentForToolTip`） |
| 优先级 | P1 |

#### NewTest-9：`#ifdef/#ifndef` 必须读取 flag 的布尔值，而不是只看 key 是否存在

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::FAngelscriptPreprocessor` / `FAngelscriptPreprocessor::ParseIntoChunks` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 条件编译最容易被脚本作者写成 `#ifdef FLAG`；若这里不测，布尔值被误当“存在性”会把整类环境隔离逻辑全部放空。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Directives.IfdefRespectsBooleanFlagValue` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` |
| 场景描述 | 在 `Preprocessor.PreprocessorFlags` 中手工加入 `MYFLAG = false`，脚本分别使用 `#ifdef MYFLAG` 与 `#ifndef MYFLAG` 选择不同 `Entry()` 返回值。 |
| 输入/前置 | clean engine；构造两个独立脚本，或一个脚本里同时验证两个分支；预处理前手工设置 `PreprocessorFlags.Add("MYFLAG", false)`。 |
| 期望行为 | `#ifdef MYFLAG` 分支必须被裁掉，`#ifndef MYFLAG` 分支必须保留；编译后执行 `Entry()` 返回来自 `#ifndef` 分支的值，且 diagnostics 为空。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` / `ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-10：annotated 编译路径需要证明生成函数真的可执行

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `CompileAnnotatedModuleFromMemory` / `ExecuteGeneratedIntEventOnGameThread` |
| 现有测试覆盖 | 有测试但缺少 annotated 模块的真实执行路径 |
| 风险评估 | 目前大多数 compiler-pipeline 用例只检查反射形状；若生成类/函数已注册但字节码不可执行，现有测试不会报红。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.AnnotatedMethodExecutes` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineExecutionTests.cpp` |
| 场景描述 | 编译一个含 `UCLASS()` 和 `UFUNCTION()` 的 annotated 模块，函数体返回固定整数；编译后定位生成类和函数，并在对象上真正调用该函数。 |
| 输入/前置 | clean engine；脚本包含 `UCLASS`、一个 `UFUNCTION int GetValue() { return 42; }`，必要时用 CDO 或新建对象调用。 |
| 期望行为 | `CompileAnnotatedModuleFromMemory()` 成功；`FindGeneratedClass()` 与 `FindGeneratedFunction()` 返回非空；`ExecuteGeneratedIntEventOnGameThread()` 返回 `true` 且结果为 `42`，证明 annotated 路径真的走到了可执行函数。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` / `FAngelscriptTestFixture` + `CompileAnnotatedModuleFromMemory` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P0 |

#### NewTest-11：语法错误后的编译失败必须留下清晰诊断且不残留旧反射对象

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `CompileModuleWithSummary` / `CompilePreparedModules` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 编译失败若没有稳定诊断或会残留旧 class/enum/delegate，对热重载和后续编译都会造成二次污染。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.SyntaxErrorFailsWithoutResidualReflection` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineFailureTests.cpp` |
| 场景描述 | 先编译一个合法 annotated 模块注册 `UBrokenCarrier`，再用同模块名提交一个缺右花括号或缺分号的语法错误版本。 |
| 输入/前置 | clean engine；第一次脚本合法，第二次脚本故意破坏语法；使用 `CompileModuleWithSummary` 捕获失败结果和 diagnostics。 |
| 期望行为 | 第二次编译失败，diagnostics 精确指向语法错误行；`FindGeneratedClass("UBrokenCarrier")` 仍指向旧的可用类或明确为空，但绝不能出现半更新对象；如果随后重新提交修复版本，应再次成功并可执行。 |
| 使用的 Helper | `CompileModuleWithSummary` + `FindGeneratedClass` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 4 | Issue-5 |
| BadIsolation | 2 | Issue-4 |
| AntiPattern | 1 | Issue-1 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 6 | MissingErrorPath: 4, MissingScenario: 2 |
| P1 | 4 | MissingScenario: 1, MissingEdgeCase: 1, MissingErrorPath: 1, NoTestForSource: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-08 13:21)

### 一、现有测试问题

#### Issue-8：`Preprocessor` 用例向真实磁盘写 fixture 但从不清理

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.BasicParse` / `Angelscript.TestModule.Preprocessor.MacroDetection` / `Angelscript.TestModule.Preprocessor.ImportParsing` |
| 行号范围 | 23-33，82-88，128-139，177-192 |
| 问题描述 | 三个用例都通过 `WriteFixtureFile()` 把脚本写到 `Saved/Automation/PreprocessorFixtures/...`，但 helper 只负责 `MakeDirectory + SaveStringToFile`，测试结束后没有删除文件，也没有为每次运行生成唯一目录。这样同一路径会长期残留在工作区里，并在多轮执行之间被复用。 |
| 影响 | 测试结果会受历史残留文件影响，尤其是 import 用例与后续基于 `Saved/Automation` 的其它 helper 共享目录时，更容易出现“读取到上次运行留下的脚本”或 fixture 污染诊断输出的问题。它还让用例依赖真实文件系统状态，增加了并发执行和失败重跑时的不稳定性。 |
| 修复建议 | 把 fixture 生命周期显式化：为每个用例创建唯一临时子目录，并在 `ON_SCOPE_EXIT` 中递归删除；至少也要为 `WriteFixtureFile()` 返回一个可清理的 absolute path 列表，并在测试结束时调用 `IFileManager::Delete`/`DeleteDirectory`。如果项目已有 `FAngelscriptTestFixture` 一类封装，优先复用而不是手写磁盘 helper。 |

#### Issue-9：`CompilerPipeline` 文件已超过 500 行且混合 8 个不同主题，违反单文件单职责约束

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.*`（8 个用例） |
| 行号范围 | 1-501 |
| 问题描述 | 当前文件总长 501 行，并把 delegate/enum/class 反射、function default、property default、module inspection、class-like property 形状等 8 个不同目标混在同一个 `.cpp`。规则明确要求单文件 300-500 行、单文件单职责；这个文件已经越界，而且后续继续补 failure/edge-case 场景时只会更快失控。 |
| 影响 | 审查和维护成本升高，新增回归测试时很容易继续把不相干场景堆进同一个文件；同时它削弱了用例分组语义，导致“执行路径测试”“反射形状测试”“失败路径测试”无法独立扩展和定位。 |
| 修复建议 | 按职责拆分为至少 3 个文件：`AngelscriptCompilerPipelineExecutionTests.cpp`（真实执行路径）、`AngelscriptCompilerPipelineReflectionTests.cpp`（delegate/enum/class/property 形状）、`AngelscriptCompilerPipelineFailureTests.cpp`（空文件/语法错误/恢复路径）。拆分后每个文件维持 300-500 行，并让测试名继续使用 `Angelscript.TestModule.Compiler.EndToEnd.*` 的子分类。 |

#### Issue-10：`CompilerPipeline` 成功路径从不清空也不检查 diagnostics，编译 warning 会被全部漏掉

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.*`（8 个用例） |
| 行号范围 | 21-51，95-145，174-193，234-253，283-298，334-348，376-390，425-453 |
| 问题描述 | 8 个用例统一以 `CompileAnnotatedModuleFromMemory()` 或 `BuildModule()` 的 `bool` 结果作为“编译成功”判据，但这些 helper 并不会像 `CompileModuleWithSummary()` 那样在调用前清空 `Engine->Diagnostics`，也不会在成功后回收并断言 diagnostics 为空。`AngelscriptTestEngineHelper.cpp:285-315` 已提供了专门的 summary helper 用于收集诊断，而当前文件完全没有使用。 |
| 影响 | 只要编译结果仍返回 handled，新增 warning、错误定位漂移、甚至共享引擎里遗留的 diagnostics 都不会让这些测试变红。对于 `Preprocessor + Compiler` 联动场景，这意味着“代码能编过去但合同已经退化”的回归会被长期放过。 |
| 修复建议 | 对成功路径改用 `CompileModuleWithSummary()`，显式断言 `bCompileSucceeded == true`、`CompileResult` 为期望值、`Diagnostics.Num() == 0`，并在需要执行字节码的场景里再补 `ExecuteIntFunction()` / `ExecuteGeneratedIntEventOnGameThread()`。至少要在每个用例开头清空 `Engine.Diagnostics`，避免共享引擎残留诊断造成假绿。 |

#### Issue-11：`WriteFixtureFile()` 忽略写盘结果，fixture 写入失败时测试可能读取旧文件假绿

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.BasicParse` / `Angelscript.TestModule.Preprocessor.MacroDetection` / `Angelscript.TestModule.Preprocessor.ImportParsing` |
| 行号范围 | 28-33，82-88，128-139，177-192 |
| 问题描述 | `WriteFixtureFile()` 调用 `FFileHelper::SaveStringToFile(Contents, *AbsolutePath);` 后直接返回路径，没有检查返回值，也没有把失败信息传回测试用例。结合固定路径和缺少清理，一旦写盘失败，后续 `Preprocessor.AddFile()` 很可能继续读取上一次运行留下的文件内容。 |
| 影响 | 这会把真实的 I/O 失败掩盖成假绿，尤其是在权限、文件占用或并发测试环境下更危险。测试失败时也很难第一时间定位到“fixture 根本没写进去”，因为断言只会看到后续预处理结果。 |
| 修复建议 | 让 `WriteFixtureFile()` 返回 `TOptional<FString>` 或 `bool + OutPath`，并在调用处先 `TestTrue("Fixture file should be written", bSaved)`；若保留当前签名，至少在 helper 内部 `ensureAlwaysMsgf` 并在失败时返回空路径，使后续 `TestNotNull/TestTrue` 能立即报出 fixture 写盘失败。 |

#### NewTest-12：已命中分支后的 `#elif` 必须短路，不能再求值未知 flag

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::ParsePreProc` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | `#if A ... #elif B ...` 是最常见的互斥配置写法；如果前一个分支已经命中后仍去求值后续 `#elif`，未注册 flag 会把本应成功的脚本直接打红。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Directives.ElifShortCircuitsAfterTakenBranch` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` |
| 场景描述 | 构造 `#if EDITOR` 返回 `7`、`#elif UNKNOWN_FLAG` 返回 `9`、`#else` 返回 `3` 的脚本，在 editor 上下文编译并执行。 |
| 输入/前置 | clean engine；使用 `CompileModuleWithSummary()` 打开预处理路径；保持 `EDITOR` 为 true，不向 `PreprocessorFlags` 注册 `UNKNOWN_FLAG`。 |
| 期望行为 | 编译成功且 diagnostics 为空；`ExecuteIntFunction()` 返回 `7`；诊断中不得出现 `Invalid preprocessor condition: UNKNOWN_FLAG`，证明后续 `#elif` 没有被求值。 |
| 使用的 Helper | `CompileModuleWithSummary` + `ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-13：`AddFile` 传入 Windows 风格相对路径时必须规范化为点分模块名

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::FilenameToModuleName` / `FAngelscriptPreprocessor::AddFile` / `FAngelscriptPreprocessor::ProcessImports` |
| 现有测试覆盖 | 有测试但只覆盖 `/` 风格路径 |
| 风险评估 | 当前工作区运行在 Windows；若 `AddFile()` 收到 `Tests\\Preprocessor\\Foo.as` 这类常见路径，模块名规范化与 `import Tests.Preprocessor.Foo;` 的查找规则会脱节，导致导入图和模块名稳定性失真。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Paths.BackslashRelativePathNormalizesModuleName` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp` |
| 场景描述 | 用 `Tests\\Preprocessor\\WinShared.as` 与 `Tests\\Preprocessor\\WinUse.as` 两个 backslash 相对路径建立 provider/importer，import 语句仍使用规范模块名 `Tests.Preprocessor.WinShared`。 |
| 输入/前置 | clean engine；manual import 模式；通过 fixture 写入两份脚本并分别 `AddFile()`；随后执行 `Preprocess()`。 |
| 期望行为 | `Preprocess()` 成功；provider 模块名应为 `Tests.Preprocessor.WinShared`，而不是保留反斜杠；importer 的 `ImportedModules` 精确包含 `Tests.Preprocessor.WinShared`，并且不会留下 `Tests\\Preprocessor\\WinShared` 形式的错误模块名。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-14：tab 分隔的 directive 必须与空格分隔语义一致

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::ReadIdentifier` / `FAngelscriptPreprocessor::ReadUntilWhitespace` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 格式化工具和手工对齐都可能把 directive 改成 tab 分隔；如果 lexer 只接受空格，条件编译和 `#restrict usage` 会从“生效”静默退化成“原文流入后续编译”。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Directives.TabSeparatedDirectiveParsing` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` |
| 场景描述 | 脚本分别使用 `#if\tEDITOR`、`#ifdef\tEDITOR`、`#restrict usage allow\tGame.*` 三种 tab 分隔写法，构造一个带 `Entry()` 的最小模块。 |
| 输入/前置 | clean engine；使用 `CompileModuleWithSummary()` 收集 diagnostics；在 editor 上下文运行，并额外读取 `ModuleDesc->UsageRestrictions`。 |
| 期望行为 | 预处理/编译成功；`ExecuteIntFunction()` 返回来自活跃分支的值；`UsageRestrictions` 成功记录 `Game.*`；最终 diagnostics 中不应出现原始 `#if`/`#restrict` 文本残留导致的语法错误。 |
| 使用的 Helper | `CompileModuleWithSummary` + `ExecuteIntFunction`，必要时配合直接读取 `FAngelscriptPreprocessor` 的 `Module` 元数据 |
| 优先级 | P1 |

#### NewTest-15：失活分支中的 `#restrict usage` 不能泄漏到模块元数据

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 如果 dead branch 中的 restriction 仍被写入 `UsageRestrictions`，模块会在错误配置下被限制或放行，产生“源码没编进来但元数据还在生效”的漂移。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.RestrictUsage.InactiveBranchIgnored` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` |
| 场景描述 | 构造 `#if !EDITOR` 分支内写 `#restrict usage disallow Runtime.*`，活跃分支只保留一个简单 `Entry()`；在 editor 上下文执行预处理。 |
| 输入/前置 | clean engine；直接调用 `FAngelscriptPreprocessor`；保持 `EDITOR == true`；脚本保存到 fixture 后 `AddFile()` 并 `Preprocess()`。 |
| 期望行为 | `Preprocess()` 成功；目标模块的 `UsageRestrictions.Num() == 0`；`Code[0].Code` 中不残留 dead branch 的 restriction 文本；随后编译并执行 `Entry()` 成功，证明 restriction 没有从不可达分支泄漏出来。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor`，必要时再走 `CompileModuleWithSummary` / `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-8 |
| AntiPattern | 1 | Issue-9 |
| WeakAssertion | 1 | Issue-10 |
| FlakyRisk | 1 | Issue-11 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingErrorPath: 1 |
| P1 | 3 | MissingEdgeCase: 2, MissingErrorPath: 1 |

---

## 测试审查 (2026-04-08 13:38)

### 一、现有测试问题

#### Issue-12：`Preprocessor` 三个成功用例都不检查 diagnostics，warning/行号漂移会被静默放过

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.BasicParse` / `MacroDetection` / `ImportParsing` |
| 行号范围 | 74-112，120-161，169-219 |
| 问题描述 | 三个用例都只把 `Preprocessor.Preprocess()` 的 `bool` 返回值当成成功判据，然后直接检查 `Module`/`Macro` 结果；它们既没有在执行前清空 `Engine->Diagnostics`，也没有在成功后断言 diagnostics 为空或包含预期 warning。源码 `FAngelscriptPreprocessor` 明确通过 `FileWideWarning` / `LineWarning` / `LineError` 把诊断写入 `FAngelscriptEngine::Get().ScriptCompileError(...)`，因此“预处理成功但产生 warning、行号错位或额外诊断”的回归不会让现有 3 个 happy-path 用例报红。 |
| 影响 | 当前文件无法保护预处理器最关键的诊断合同：成功路径是否干净、warning 是否只在预期场景出现、错误行号是否稳定。即便 `BasicParse` 或 `MacroDetection` 开始产生 warning，CI 仍会显示全绿。 |
| 修复建议 | 每个用例进入预处理前先清空 `Engine->Diagnostics` / `LastEmittedDiagnostics`，成功后显式断言 diagnostics 为空；`ImportParsing` 再按模式断言是否应出现特定 warning。若继续直接驱动 `FAngelscriptPreprocessor`，至少补一个小型 helper 统一收集并比对诊断；更稳妥的做法是让 preprocessor 测试也复用 `CompileModuleWithSummary` 风格的 summary 结构，而不是只看 `bool`。 |

#### Issue-13：`DelegateSignatureConsistency` 名称写的是“签名一致性”，实际却没有断言任何签名细节

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateSignatureConsistency` |
| 行号范围 | 371-410 |
| 问题描述 | 该用例编译了 `delegate void FCompilerSingleCastSignature(int Value);` 和 `event void FCompilerMultiCastSignature(UClass TypeValue, FString Label);`，但断言只覆盖 `bIsMulticast` 和 `Function != nullptr`。运行时代码明明为 delegate 暴露了 `FAngelscriptDelegateDesc::Signature`，其中包含 `ReturnType` 和 `Arguments`；当前测试却既不检查 `Signature` 是否有效，也不检查参数个数、参数类型、返回类型或生成的 `UDelegateFunction` 参数布局。 |
| 影响 | 只要 delegate 名称还能注册、且能生成某个 `UDelegateFunction`，这个“签名一致性”测试就会通过；如果参数类型顺序、返回类型或 `Signature` 描述符在编译/预处理链路中发生漂移，CI 不会给出任何信号。 |
| 修复建议 | 在现有用例内把断言升级为签名级验证：`SingleCast->Signature` 和 `MultiCast->Signature` 必须有效；`ReturnType` 应为 `void`；参数数量分别为 1 和 2；参数类型分别匹配 `int`、`UClass`、`FString`，必要时同时检查参数名 `Value` / `TypeValue` / `Label`。对 `SingleCast->Function` 和 `MultiCast->Function` 也应补 `FindFProperty`/参数计数断言，确保 Angelscript 签名与 Unreal 反射签名同步。 |

#### Issue-14：`CompilerPipeline` 使用会吞掉 `ECompileResult` 的 helper，无法分辨 `FullyHandled` 与 `PartiallyHandled`

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.*`（8 个用例） |
| 行号范围 | 21-51，95-145，174-193，234-253，283-298，334-348，376-390，425-453 |
| 问题描述 | 7 个 annotated 用例统一走 `CompileAnnotatedModuleFromMemory()`，该 helper 在 `AngelscriptTestEngineHelper.cpp:358-364` 内部直接调用 `PreprocessAndCompile(..., ECompileType::FullReload)` 并只返回 `bool`；而 `PreprocessAndCompile()` / `CompilePreparedModules()` 把 `ECompileResult::FullyHandled` 和 `PartiallyHandled` 都视为成功。`ModuleFunctionInspection` 用的 `BuildModule()` 也只在 `Error` / `ErrorNeedFullReload` 时才失败，`PartiallyHandled` 同样会被吞掉。结果是测试看不到本次编译究竟是完全处理、部分处理，还是已经退化成“需要更强 reload”的边界状态。 |
| 影响 | 编译管线一旦从“稳定 fully handled”退化到“仅 partially handled”，这些用例仍会全绿，导致 reload 语义、编译阶段稳定性和 helper 合同的回归长期不可见。对于名为 `CompilerPipeline` 的测试文件，这会直接削弱它对编译结果等级的保护。 |
| 修复建议 | 对所有成功路径改用 `CompileModuleWithSummary()`，显式断言 `CompileType`、`CompileResult`、`ModuleDescCount`、`CompiledModuleCount` 和 `bUsedPreprocessor`。若某个场景本来就允许 `PartiallyHandled`，应把这一点写成明确断言；如果预期是完整编译，则必须锁定 `CompileResult == FullyHandled`。需要分析 reload 语义的场景则改用 `AnalyzeReloadFromMemory()`，不要继续用 bool-only helper 抹平状态差异。 |

### 二、需要新增的测试

本轮新增测试建议已记录为 `NewTest-67` / `NewTest-68` / `NewTest-69`；为避免重复，不在此处再次抄写正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-32 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 2, MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 12:15)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-34`；为遵守“只追加不覆盖”，此处仅在文件尾部补索引，不重复抄写正文。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-70` / `NewTest-71` / `NewTest-72`；为避免重复，此处不重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-34 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |
 
---
 
## 测试审查 (2026-04-09 12:27)
 
### 一、现有测试问题
 
本轮未新增现有测试质量问题。
 
### 二、需要新增的测试
 
本轮新增正文已记录为 `NewTest-73` / `NewTest-74` / `NewTest-75`；为避免重复，此处不重复正文。
 
### 本轮汇总
 
**现有测试问题统计**
 
| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |
 
**新增测试建议统计**
 
| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingEdgeCase: 3 |

## 测试审查 (2026-04-09 12:27)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-73` / `NewTest-74` / `NewTest-75`；为避免重复，此处不重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingEdgeCase: 3 |

---

## 测试审查 (2026-04-09 12:27)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-73` / `NewTest-74` / `NewTest-75`；为避免重复，此处不重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingEdgeCase: 3 |

---

## 测试审查 (2026-04-09 12:27)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-73` / `NewTest-74` / `NewTest-75`；为避免重复，此处不重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingEdgeCase: 3 |

---


## 测试审查 (2026-04-09 12:27)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-73：string `default` 中的 comment marker 与 escaped quote 不能被 `StripCommentsFromLine` 误剪裁

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessDefaults` / `FAngelscriptPreprocessor::StripCommentsFromLine` |
| 现有测试覆盖 | `PropertyDefaultsCompile` 只验证 `int` 与 `FName` default；`NewTest-20` 只规划覆盖 default 语句尾随的真实 line/block comment，仍未覆盖 string literal 内部的 `//`、`/* */` 和 `\\\"` |
| 风险评估 | `StripCommentsFromLine()` 目前遇到每个 `\"` 都会翻转 `bInString`，没有处理 escaped quote；一旦默认值字符串里出现 `\\\"//` 或 `/*literal*/`，预处理器就可能把字符串后半截当成注释吞掉，导致 CDO 默认值静默损坏或生成非法 defaults code |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.StringDefaultPreservesCommentMarkersInsideLiteral` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelinePropertyDefaultTests.cpp` |
| 场景描述 | 编译一个最小 annotated 类 `UCompilerStringDefaultCarrier`，声明 `UPROPERTY() FString Message;` 与 `UPROPERTY() FString BlockText;`，并写 `default Message = "He said \\\"//not a comment\\\"";`、`default BlockText = "/*literal*/";`；类内再提供 `UFUNCTION() int VerifyDefaults()`，只有两个属性值都与原始字面量完全一致时返回 `42` |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)` 编译；编译后用 `FindGeneratedClass()`、`FindFProperty<FStrProperty>()`、`FindGeneratedFunction()` 和 `ExecuteGeneratedIntEventOnGameThread()` 读取 CDO 与执行校验函数 |
| 期望行为 | 编译成功且 `CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；`Message` 精确等于 `He said \"//not a comment\"`，`BlockText` 精确等于 `/*literal*/`；`VerifyDefaults()` 返回 `42`；processed/defaults code 不得因为 comment stripper 误判而丢失字符串后半段或引入语法错误 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `FindGeneratedClass` + `FindFProperty<FStrProperty>` + `FindGeneratedFunction` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

#### NewTest-74：`for (...) : ...` 的 regex rewrite 必须跳过 string literal 与注释里的伪循环文本

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::PostProcessRangeBasedFor` |
| 现有测试覆盖 | `NewTest-22` 只规划验证真正的 block/single-line range-based `for` 能被正确改写；当前目标集没有任何用例把 `for (const int Value : Values)` 这段文本放进 string literal 或 comment 中做伪命中对照 |
| 风险评估 | `PostProcessRangeBasedFor()` 直接在整段 `ProcessedCode` 上跑正则；若它把字符串或注释里的文本也当成真实循环改写，脚本可能在不报预处理错误的情况下篡改用户字符串、注释内容，甚至生成额外 `_Iterator` 代码导致编译失败 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.RangeBasedForRewriteSkipsStringAndCommentLiterals` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineRangeForTests.cpp` |
| 场景描述 | 构造单文件脚本：保留一个真实的 `for (const int Value : Values)` 求和循环，同时声明 `FString LoopText = "for (const int Value : Values)";`，并在旁边放一条 `// for (const int Value : Values)` 注释；先直接预处理检查 rewrite 结果，再完整编译执行 `Entry()` 返回 `42` |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；第一阶段直接 `AddFile()` + `Preprocess()` 读取 `Code[0].Code`，第二阶段使用 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 编译并 `ExecuteIntFunction()` 运行 `Entry()` |
| 期望行为 | 预处理成功且 diagnostics 为空；`Code[0].Code` 中只允许真实循环被改写成 `_Iterator` / `Proceed()` 形式，字符串字面量里的 `for (const int Value : Values)` 必须原样保留；`_Iterator.CanProceed` 只应出现 1 次；后续编译成功且 `Entry()` 返回 `42`，证明 rewrite 没有污染伪循环文本或把 comment/string 当成真实语句处理 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` + `CompileModuleWithSummary` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-75：`asset ... of ...` 后处理不能误命中 string literal 或注释中的伪 asset 语法

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::PostProcessLiteralAssets` |
| 现有测试覆盖 | `NewTest-21` 只规划验证真实 `asset PreviewAsset of UObject` 会生成 getter 与 `PostInitFunctions`；当前目标集没有任何 decoy 场景验证 regex 是否会误改写字符串或注释里的同形文本 |
| 风险评估 | `PostProcessLiteralAssets()` 直接对整段 `ProcessedCode` 做 regex 替换；如果它把 `"asset FakeAsset of UObject"` 或 `// asset FakeAsset of UObject` 也当成真实声明，预处理器会静默生成多余 getter、污染 `PostInitFunctions`，甚至破坏用户字符串常量 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.LiteralAssets.SkipStringAndCommentDecoys` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorLiteralAssetTests.cpp` |
| 场景描述 | 构造一个最小脚本，同时包含 1 条真实 `asset RealAsset of UObject`、1 个字符串字面量 `FString AssetText = "asset FakeAsset of UObject";`，以及 1 条 `// asset CommentAsset of UObject` 注释；直接运行预处理并检查 module 输出 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；执行 `AddFile()` + `Preprocess()` 后读取目标模块的 `Code[0].Code` 与 `PostInitFunctions` |
| 期望行为 | `Preprocess()` 成功且 diagnostics 为空；`PostInitFunctions` 只能包含 `GetRealAsset`，不得出现 `GetFakeAsset` 或 `GetCommentAsset`；processed code 必须生成 `__Asset_RealAsset` / `GetRealAsset()`，同时字符串字面量中的 `asset FakeAsset of UObject` 文本应原样保留，不允许被替换成 getter 模板；comment decoy 也不能生成任何额外 asset 初始化代码 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

---

## 测试审查 (2026-04-09 12:15)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-34`；为遵守“只追加不覆盖”，此处仅在文件尾部补索引，不重复抄写正文。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-70` / `NewTest-71` / `NewTest-72`；为避免重复，此处不重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-34 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 12:15)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-34`；为遵守“只追加不覆盖”，此处仅在文件尾部补索引，不重复抄写正文。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-70` / `NewTest-71` / `NewTest-72`；为避免重复，此处不重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-34 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 12:11)

### 一、现有测试问题

#### Issue-34：annotated compiler 用例把 fixture 写到固定 `Saved/Automation` 路径且从不清理

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateEnumClassCompile` / `FunctionDefaultsAndClassLikeCompile` / `PropertyDefaultsCompile` / `GeneratedClassConsistency` / `EnumAvailability` / `DelegateSignatureConsistency` / `ClassLikeReflectionShape` |
| 行号范围 | 21-24，95-98，174-177，234-237，334-337，376-379，425-428 |
| 问题描述 | 这 7 个 annotated compile 用例都调用 `CompileAnnotatedModuleFromMemory()`，并传入固定文件名如 `CompilerDelegateEnumClassCompile.as`、`CompilerPropertyDefaultsCompile.as`。底层 `PreprocessAndCompile()` 在 `AngelscriptTestEngineHelper.cpp:170-186` 会把脚本直接写到 `Saved/Automation/<Filename>`，成功后既不删除文件，也不为当前执行生成唯一子目录。结果是这些用例会把临时脚本长期留在工作区，并在重跑、并发 shard 或同名回归测试复用时共享同一磁盘路径。 |
| 影响 | 预处理器读到的 fixture 来源不再是“本次用例独占输入”，而是受上一次残留文件和并发覆盖影响。即使内存里的脚本文本没变，固定写盘路径也会放大磁盘缓存、残留 diagnostics 关联文件、以及并发覆盖导致的偶发红灯风险。 |
| 修复建议 | 把 annotated compile 场景迁移到 `FAngelscriptTestFixture` 或给 `CompileAnnotatedModuleFromMemory()` 增加“唯一临时目录 + `ON_SCOPE_EXIT` 删除文件”的封装；至少把文件名扩成 `Saved/Automation/<TestName>/<Guid>.as` 并在测试结束时清理，避免继续复用固定路径。 |

### 二、需要新增的测试

#### NewTest-70：`FilenameToModuleName` 只能剥离末尾扩展名，不能吞掉中间路径段里的 `.as`

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::FilenameToModuleName` |
| 现有测试覆盖 | 当前目标集只有 `NewTest-13` 计划覆盖 Windows 反斜杠路径；没有任何用例验证路径中间出现 `.as` 目录名时的模块名规范化 |
| 风险评估 | 只要 `FilenameToModuleName()` 继续对整条路径做 `Replace(".as", "")`，`Tests/Foo.as/Bar.as` 与 `Tests/Foo/Bar.as` 就会坍缩成同一个模块名，import、热重载和 diagnostics 都会落到错误模块上 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Paths.FilenameToModuleNameOnlyStripsTerminalExtension` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPathTests.cpp` |
| 场景描述 | 直接实例化 `FAngelscriptPreprocessor`，分别对 `Tests/Foo.as/Bar.as`、`Tests/Foo/Bar.as` 和 `Tests/Foo.as/Baz.asset.as` 调用 `FilenameToModuleName()`，比较规范化结果 |
| 输入/前置 | 无需引擎 world；仅需一个普通 `FAngelscriptPreprocessor` 实例，直接调用 public helper |
| 期望行为 | `FilenameToModuleName(TEXT("Tests/Foo.as/Bar.as"))` 必须精确返回 `Tests.Foo.as.Bar`；`FilenameToModuleName(TEXT("Tests/Foo/Bar.as"))` 必须返回 `Tests.Foo.Bar`；两者不得相等；额外断言 `Tests/Foo.as/Baz.asset.as` 结果为 `Tests.Foo.as.Baz.asset`，证明函数只移除最终扩展名 |
| 使用的 Helper | 其他（纯 Automation test，直接调用 `FAngelscriptPreprocessor::FilenameToModuleName`） |
| 优先级 | P1 |

#### NewTest-71：`EDITORONLY_DATA` 条件块必须像 `EDITOR` 一样登记到 `EditorOnlyBlockLines`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks`（`UpdateEditorBlockLines` 闭包） |
| 现有测试覆盖 | `NewTest-28` 只计划覆盖 `#if EDITOR` 的行号记录；当前目标集没有任何 `EDITORONLY_DATA` fixture |
| 风险评估 | 预处理器初始化时已经注册 `EDITORONLY_DATA` flag，也允许它作为 editor-only macro 条件；如果 `EditorOnlyBlockLines` 不同步记录该分支，builder 看到的 editor-only 行号会比真实源码缺一块，后续 editor-only 诊断将静默漂移 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Directives.EditorOnlyDataBlockLinesRecorded` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` |
| 场景描述 | 构造单文件脚本：前半段用 `#if EDITORONLY_DATA ... #endif` 包住一个最小 helper 函数，后半段保留普通 `int Entry() { return 7; }`；先直接运行预处理，再按需要走一次轻量编译 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件 fixture；执行前清空 diagnostics，并保持 `EDITORONLY_DATA` 使用当前环境默认值 |
| 期望行为 | `Preprocess()` 成功且 diagnostics 为空；目标模块的 `EditorOnlyBlockLines.Num() == 1`，并且起止行精确覆盖 `#if EDITORONLY_DATA` 到对应 `#endif` 的那一段；`Code[0].Code` 中不应残留原始 directive 文本；若追加 compile/execute，对照的 `Entry()` 仍应返回 `7` |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor`，必要时配合 `CompileModuleWithSummary` / `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-72：带非法标识符字符的模块名必须经 `MakeIdentifier` 规范化后再生成 statics class

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::GetOrCreateStaticsClass` / `FAngelscriptPreprocessor::MakeIdentifier` |
| 现有测试覆盖 | `NewTest-33` 只计划验证普通模块名下的 global `UFUNCTION()` statics class；当前目标集没有任何用例把模块路径命名成 `Foo-Bar`、`Foo+Bar` 这类需要 sanitize 的形式 |
| 风险评估 | 如果 `MakeIdentifier()` 回归，global `UFUNCTION()` 所依赖的 statics class 名称会直接变成非法 C++/script 标识符，问题通常只在带特殊字符的真实文件名落地时才暴露 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.GlobalUFunctionSanitizesModuleNameForStaticsClass` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineGlobalUFunctionTests.cpp` |
| 场景描述 | 使用相对路径 `Tests/Compiler/Module-With-Dash.as` 编译一个仅包含全局 `UFUNCTION(BlueprintCallable) int GetValue() { return 42; }` 的 annotated 脚本，然后查找自动生成的 statics class |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., ECompileType::FullReload, TEXT("ModuleWithDash"), TEXT("Tests/Compiler/Module-With-Dash.as"), Script, true, Summary, false)` 编译；编译后读取 module desc 与 generated class |
| 期望行为 | 编译成功且 diagnostics 为空；module desc 中存在一个 `bIsStaticsClass == true` 的类描述，类名必须精确等于 `Module_Tests_Compiler_Module_With_DashStatics`；不得出现带 `-` 的原始字符；同时 `DisplayName` metadata 应保持 `Module-With-Dash`，证明 sanitize 只影响 identifier，不污染用户可见名字 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + 直接读取 `FAngelscriptModuleDesc` / `FindGeneratedClass` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-34 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

#### NewTest-67：direct preprocessor 必须精确锁住 `UINTERFACE` 描述采集与方法声明规范化

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::DetectClasses` / `FAngelscriptPreprocessor::AnalyzeClasses` |
| 现有测试覆盖 | 当前目标集完全无覆盖；仓库 `Interface/` 目录有高层 declare/implement 场景，但没有 direct preprocessor 用例去读取 `FAngelscriptClassDesc::bIsInterface`、`SuperClass`、`ImplementedInterfaces` 和 `InterfaceMethodDeclarations` |
| 风险评估 | 如果 interface chunk 切分、默认 `UInterface` 基类填充、script interface 继承保留或 `float/double -> float32/float64` 的方法声明规范化回归，现有 preprocessor 用例和大多数高层 scenario 都难以第一时间把问题定位到预处理阶段 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Interface.DescriptorAndMethodNormalization` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorInterfaceTests.cpp` |
| 场景描述 | 单文件脚本声明两个接口：`UINTERFACE() interface UIAuditBase { void TakeDamage(float Amount); }` 与 `UINTERFACE() interface UIAuditChild : UIAuditBase { double QueryValue(double Amount); }`，直接运行 `FAngelscriptPreprocessor` |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入唯一 fixture；执行前清空 diagnostics；`Preprocess()` 后读取 `GetModulesToCompile()` 的唯一模块与其 `Classes` 描述 |
| 期望行为 | `Preprocess()` 成功且 diagnostics 为空；模块内恰好生成两个 `FAngelscriptClassDesc`；`UIAuditBase` 与 `UIAuditChild` 都满足 `bIsInterface == true`、`bAbstract == true`；`UIAuditBase.SuperClass == "UInterface"`，`UIAuditChild.SuperClass == "UIAuditBase"`；`UIAuditBase.InterfaceMethodDeclarations` 精确等于 `["void TakeDamage(float32 Amount)"]`，`UIAuditChild.InterfaceMethodDeclarations` 精确等于 `["float64 QueryValue(float64 Amount)"]`，证明接口方法声明在预处理阶段已被正确规范化并保存 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-68：`Compiler.EndToEnd` 需要最小 `UINTERFACE` 从 annotated 源码到可调用实现的 round-trip

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::AnalyzeClasses` / interface class generation path |
| 现有测试覆盖 | 当前目标集完全无覆盖；仓库 `Interface/` 套件覆盖的是更高层行为和 Actor 场景，不是当前 `Compiler.EndToEnd` 这一层的最小 annotated compile trace |
| 风险评估 | 如果 interface 的 annotated compile 只在 class generation 或 generated `UFunction` 落地阶段退化，现有 compiler pipeline 套件不会报红，问题会被拖到更重的 scenario 测试里才暴露 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.InterfaceAnnotatedRoundTrip` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineInterfaceTests.cpp` |
| 场景描述 | 编译一个最小 annotated 脚本：`UINTERFACE() interface UICompilerProbe { int ComputeValue(); }` 与 `UCLASS() class UCompilerProbeCarrier : UObject, UICompilerProbe { UFUNCTION() int ComputeValue() { return 7; } }`，随后读取生成的 interface/class 并调用实现函数 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；统一通过 `CompileModuleWithSummary(&Engine, ECompileType::FullReload, TEXT("CompilerInterfaceRoundTrip"), TEXT("Tests/Compiler/CompilerInterfaceRoundTrip.as"), Script, true, Summary, false)` 编译；编译后用 `FindGeneratedClass()` 获取 `UICompilerProbe` 与 `UCompilerProbeCarrier`，再用 `FindGeneratedFunction()` 和 `ExecuteGeneratedIntEventOnGameThread()` 调用默认对象上的 `ComputeValue` |
| 期望行为 | `bCompileSucceeded == true`、`CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；生成的 `UICompilerProbe` 具备 `CLASS_Interface`；`UCompilerProbeCarrier->ImplementsInterface(UICompilerProbe)` 为真；`ComputeValue` 函数存在且在默认对象上执行结果为 `7`，证明接口声明、实现类落地和最终调用三段链路全部打通 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `FindGeneratedClass` + `FindGeneratedFunction` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

#### NewTest-69：interface 继承非接口基类时必须在预处理阶段稳定报错

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ResolveSuperClass` |
| 现有测试覆盖 | 当前目标集和仓库现有自动化都未发现任何用例触发 `Interface %s cannot inherit from non-interface class %s.` 这条 diagnostics 分支 |
| 风险评估 | 这条防线如果失效，非法 interface 继承会带着错误基类继续进入 class generation/compile，最终报错位置后移且更难定位；对脚本作者来说会直接退化成误导性的后续编译错误 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Interface.InvalidBaseReportsDiagnostic` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorInterfaceTests.cpp` |
| 场景描述 | 在同一测试文件里做两个子场景：1）`UINTERFACE() interface UBadNativeInterface : UObject { void Ping(); }`；2）先声明 `UCLASS() class UConcreteBase : UObject {}`，再声明 `UINTERFACE() interface UBadScriptInterface : UConcreteBase { void Ping(); }` |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；每个子场景都通过 `FAngelscriptTestFixture` 写入单文件脚本；执行前清空 diagnostics；直接调用 `FAngelscriptPreprocessor` |
| 期望行为 | 两个子场景都应 `Preprocess()` 返回 `false`；diagnostics 分别精确包含 `Interface UBadNativeInterface cannot inherit from non-interface class UObject.` 与 `Interface UBadScriptInterface cannot inherit from non-interface class UConcreteBase.`；`Diagnostic.Row` 必须落在对应 interface 声明行；`GetModulesToCompile()` 不得继续输出可消费的合法 interface/class 描述 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-32 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 2, MissingErrorPath: 1 |

#### NewTest-16：manual `import` 必须显式锁住 provider-first 的拓扑排序合同

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessImports` |
| 现有测试覆盖 | 有测试但只验证单跳 import 被记录，未验证多级依赖排序 |
| 风险评估 | `ProcessImports()` 的核心职责是把导入图排序成可编译顺序；如果没有 provider-first 合同测试，未来改动很容易把多级 import 退化成“依赖记录正确但模块顺序错误”。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Import.TopologicalOrderRespectsDependencyChain` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp` |
| 场景描述 | 构造 `Base.as`、`Shared.as(import Base)`、`Consumer.as(import Shared)` 三个模块，并故意按 `Consumer -> Shared -> Base` 的逆序 `AddFile()`。 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或 `AcquireCleanSharedCloneEngine()`；manual import 模式；三份 fixture 文件都加入 preprocessor。 |
| 期望行为 | `Preprocess()` 成功；`GetModulesToCompile()` 返回 3 个模块且顺序严格为 `Base`、`Shared`、`Consumer`；`Shared.ImportedModules` 只含 `Base`，`Consumer.ImportedModules` 只含 `Shared`；两个 import 语句都已从 `Code[0].Code` 中移除。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P0 |

#### NewTest-17：重复 `import` 不能把同一依赖写入两次

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessImports` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 同一模块被重复写入 `ImportedModules` 会污染后续依赖图、哈希和编译导入顺序；没有边界测试时，这种 silent corruption 很难在 CI 中暴露。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Import.DuplicateStatementsDeduplicateDependency` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp` |
| 场景描述 | provider 模块定义 `SharedValue()`；consumer 模块连续写两条相同的 `import Tests.Preprocessor.Shared;`，随后在 `Entry()` 中调用 provider。 |
| 输入/前置 | clean engine；manual import 模式；两份 fixture 文件；预处理成功后继续走一次轻量编译执行。 |
| 期望行为 | `Preprocess()` 成功；consumer 的 `ImportedModules.Num() == 1` 且唯一值为 `Tests.Preprocessor.Shared`；两条 import 文本都从 processed code 中移除；下游编译成功并执行 `Entry()` 返回 provider 的值。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor`，随后配合 `CompileModuleWithSummary` / `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-18：delegate 元数据需要真正锁住参数与返回类型，而不只是“有对象”

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessDelegates` |
| 现有测试覆盖 | 有测试但只验证 delegate 存在与 multicast 标志 |
| 风险评估 | delegate 名称能注册并不代表签名是对的；如果参数类型、参数顺序或返回类型漂移，蓝图/反射绑定会在运行时才暴露，代价远高于编译期失败。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateSignatureMetadataRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineDelegateTests.cpp` |
| 场景描述 | 编译一个同时声明 single-cast delegate 和 event delegate 的 annotated 模块，编译后同时检查 `FAngelscriptDelegateDesc::Signature` 与生成的 `UDelegateFunction`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `FAngelscriptTestFixture`；脚本包含 `delegate void FSingle(int Value);` 与 `event void FMulti(UClass TypeValue, FString Label);`。 |
| 期望行为 | 编译成功且 diagnostics 为空；`Engine.GetDelegate()` 返回的两个 desc 都带有效 `Signature`；single-cast 的返回类型为 `void`、参数数为 1、参数类型为 `int`；multicast 的参数数为 2、参数类型依次为 `UClass` 与 `FString`；生成的 `UDelegateFunction` 也能找到同名参数属性并与 desc 一致。 |
| 使用的 Helper | `CompileModuleWithSummary` + `Engine.GetDelegate` + `FindFProperty` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-13 |
| WrongHelper | 1 | Issue-14 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingScenario: 1 |
| P1 | 2 | MissingEdgeCase: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-08 13:50)

### 一、现有测试问题

#### Issue-15：`DelegateEnumClassCompile` 名义上覆盖 delegate/enum/class，实际上完全没验证 enum 结果

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateEnumClassCompile` |
| 行号范围 | 21-78 |
| 问题描述 | 用例脚本在 26-32 行显式声明了 `UENUM(BlueprintType) enum class ECompilerTransferState : uint16`，测试名也写着 `DelegateEnumClassCompile`，但断言只检查了 delegate 注册和 `UCompilerTransferObject` 反射，完全没有调用 `Engine.GetEnum(TEXT("ECompilerTransferState"))` 或验证 `ValueNames/EnumValues`。也就是说，这个用例里的“enum”部分目前只是编译输入噪音。 |
| 影响 | 只要 delegate/class 路径继续工作，即使 `ProcessMacros`、enum 描述生成或 `UENUM(BlueprintType)` 元数据注册发生回归，这个用例仍然会全绿，无法对测试名宣称覆盖的 enum 路径提供任何保护。 |
| 修复建议 | 在当前用例内补充 enum 级断言，至少验证 `Engine.GetEnum(TEXT("ECompilerTransferState"))` 有效、`ValueNames` 顺序为 `Alpha/Beta/Gamma`、`EnumValues` 为 `0/4/5`，并补一个 `EnumDesc->Enum != nullptr` 或 Blueprint metadata 断言，确保“delegate + enum + class”三条管线都被真正检查。若不想让单个用例继续承担三个主题，应把 enum 断言拆到独立的 `Angelscript.TestModule.Compiler.EndToEnd.EnumRoundTrip`。 |

### 二、需要新增的测试

#### NewTest-19：异步 `AddFile` 读取路径必须与同步路径产出完全一致

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::AddFile` / `FAngelscriptPreprocessor::PerformAsynchronousLoads` / `FAngelscriptPreprocessor::Preprocess` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 当前预处理器既支持同步读盘也支持异步读盘，但目标测试集中没有任何用例锁住两条路径的一致性。一旦异步回调、等待时机或 cleanup 有回归，就可能出现“同步编得过、异步偶发空文件/半截源码”的 nondeterministic 失败。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.AsyncLoad.AsyncMatchesSynchronousPreprocess` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorAsyncLoadTests.cpp` |
| 场景描述 | 用同一份 fixture 脚本分别走同步 `AddFile(..., false)` 与异步 `AddFile(..., true)`，脚本包含 manual import、一个简单函数和至少一个宏，确保会经过 chunk/import/macro 流程。 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或 clean clone engine；关闭 automatic import；先写入 provider 与 consumer 两个 fixture 文件；构造两个独立 `FAngelscriptPreprocessor` 实例，一个同步加载、一个异步加载。 |
| 期望行为 | 两次 `Preprocess()` 都成功；`GetModulesToCompile()` 的模块数、模块名顺序、`ImportedModules`、`Code.Num()` 和每段 `Code`/`CodeHash` 完全一致；diagnostics 为空，证明异步读盘没有引入竞态或空内容。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-20：带行尾注释的 `default` 语句必须继续被正确提取并执行

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessDefaults` / `FAngelscriptPreprocessor::StripCommentsFromLine` |
| 现有测试覆盖 | 有测试但只覆盖无注释的 `default` 语句 |
| 风险评估 | `PropertyDefaultsCompile` 目前只证明最干净的 `default Score = 21;` 能工作；一旦真实脚本在同一行附带 `//` 或 `/* ... */` 注释，defaults 提取可能把注释一起塞进生成代码或把有效语句截断，回归会在运行时才暴露。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.PropertyDefaultsIgnoreTrailingComments` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelinePropertyDefaultTests.cpp` |
| 场景描述 | 定义一个带 `int Score` 和 `TArray<FName> Tags` 的 annotated 类，在两条 `default` 语句后分别追加 `// line comment` 和 `/* block comment */`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `FAngelscriptTestFixture`；通过 `CompileModuleWithSummary()` 编译；随后读取 generated class 的 CDO。 |
| 期望行为 | 编译成功且 diagnostics 为空；`Score` 的默认值为 `21`；`Tags` 长度为 `1` 且唯一元素为 `Alpha`；processed/compiled 结果中不得因为注释残留而出现语法错误。 |
| 使用的 Helper | `CompileModuleWithSummary` + `FindGeneratedClass` + `FindFProperty` |
| 优先级 | P1 |

#### NewTest-21：`asset ... of ...` 必须生成 getter 并登记 `PostInitFunctions`

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::PostProcessLiteralAssets` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | literal asset 语法会同时改写源码和写入 `Module->PostInitFunctions`；如果这里只改了一半，脚本可能“能编译但资源 getter/后初始化缺失”，属于非常典型的预处理后处理回归。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.LiteralAssets.GenerateGetterAndPostInitRegistration` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorLiteralAssetTests.cpp` |
| 场景描述 | 构造一个最小脚本，头部包含 `asset PreviewAsset of UObject`，并保留一个简单 `Entry()` 以确保模块仍有普通代码。直接运行预处理，不依赖额外 runtime 资源。 |
| 输入/前置 | 使用 clean engine；fixture 文件单模块即可；通过 `FAngelscriptPreprocessor` 直接 `AddFile()` + `Preprocess()`。 |
| 期望行为 | `Preprocess()` 成功；模块 `Code[0].Code` 不再包含原始 `asset PreviewAsset of UObject`；替换后的源码应包含 `UObject __Asset_PreviewAsset;`、`UObject GetPreviewAsset() property`、`__CreateLiteralAsset(UObject, "PreviewAsset")` 和 `__PostLiteralAssetSetup(__Asset_PreviewAsset, "PreviewAsset")`；`Module->PostInitFunctions` 精确包含 `GetPreviewAsset` 一项。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-22：range-based `for` 的 block 与 single-line 形式都必须被正确改写并可执行

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::PostProcessRangeBasedFor` |
| 现有测试覆盖 | 当前 Preprocessor/Compiler 测试集无覆盖；仓库其他示例仅间接编译过 range-based for |
| 风险评估 | 该后处理依赖正则重写源码，并且对 `{ ... }` 与单行 `for (...) Statement;` 走不同分支；如果其中一个分支回归，普通 parse/macro/import 测试完全不会报红，只会在运行脚本时暴露。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.RangeBasedForRewriteSupportsBlockAndSingleLine` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineControlFlowTests.cpp` |
| 场景描述 | 构造一个包含 `TArray<int>` 的脚本，在同一个 `Entry()` 里先用 block 形式累加，再用 single-line 形式累加，最终返回编码后的结果，确保两条 rewrite 路径都被执行。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `FAngelscriptTestFixture`；通过 `CompileModuleWithSummary()` 编译含 `for (const int Value : Values) { ... }` 与 `for (const int Value : Values) Sum += Value;` 的最小脚本。 |
| 期望行为 | 编译成功且 diagnostics 为空；`ExecuteIntFunction()` 返回例如 `4242`（前两位代表 block sum，后两位代表 single-line sum），证明两种 range-based for 都被正确改写并执行；若需要更强证据，可额外断言预处理后的代码包含 `_Iterator.CanProceed` / `_Iterator.Proceed()`。 |
| 使用的 Helper | `CompileModuleWithSummary` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-15 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 4 | NoTestForSource: 2, MissingEdgeCase: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-08 13:55)

### 一、现有测试问题

---

## 测试审查 (2026-04-08 17:31)

### 一、现有测试问题

#### Issue-16：`FunctionDefaultsAndClassLikeCompile` 把默认参数执行与 class-like 反射混在同一用例，失败信号含混且与专用用例重复

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.FunctionDefaultsAndClassLikeCompile` |
| 行号范围 | 95-156 |
| 问题描述 | 同一个用例在 100-108 行通过 `Entry()` 验证 `SumWithDefault()` 的默认参数执行路径，同时又在 110-131 行塞入 `UClass`、`TSubclassOf<AActor>`、`TSoftClassPtr<AActor>` 三种 class-like 签名，并在 148-156 行只做 `FindGeneratedFunction()` 存在性检查。更关键的是，文件后半段已经有 `ClassLikeReflectionShape`（425-495 行）专门负责这三种签名的反射形状断言。结果是当前用例把两个不相干主题绑在一起，还和专用用例形成重复覆盖。 |
| 影响 | 一旦失败，无法快速判断是默认参数执行、annotated 编译，还是 class-like 反射路径出了问题；重复脚本片段和重复 smoke 断言也会抬高维护成本，削弱单个失败对根因的定位能力。 |
| 修复建议 | 把该用例拆成两个聚焦测试：保留一个只验证默认参数执行合同的 `Angelscript.TestModule.Compiler.EndToEnd.FunctionDefaultsCompileAndExecute`，把 class-like 片段移出；class-like 反射留给现有 `ClassLikeReflectionShape`，或另建一个 compile smoke 并明确它只负责“能编译”。若暂时不拆文件，至少让测试名与断言主题一致，并统一改用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary()`，避免在复合用例里继续叠加共享状态与 bool-only helper。 |

### 二、需要新增的测试

#### NewTest-23：注释规范化需要做 class/property/function/enum 的端到端 metadata 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/Helper_CommentFormat.h` |
| 关联函数 | `FAngelscriptPreprocessor::DetectClasses` / `FAngelscriptPreprocessor::ProcessFunctionMacro` / `FAngelscriptPreprocessor::ProcessPropertyMacro` / `FAngelscriptPreprocessor::DetectEnum` / `FormatCommentForToolTip` |
| 现有测试覆盖 | `MacroDetection` 只验证宏存在，不含任何注释 fixture；当前 8 个 compiler pipeline 用例也没有一个读取 `ToolTip` 或 `Documentation` metadata |
| 风险评估 | 即使 `Helper_CommentFormat.h` 的纯函数测试补齐，comment 到反射 metadata 的接线仍可能回归而不被发现；编辑器 tooltip、enum 文档和 Blueprint 元数据会在无明显编译错误的情况下静默变坏。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.CommentMetadataRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineCommentMetadataTests.cpp` |
| 场景描述 | 编译一个 annotated 模块，在 class、`UPROPERTY`、`UFUNCTION`、`UENUM` 和 enum value 前分别放置 JavaDoc / C-style / C++ 注释，并混入 `/*~ ... */` 或 `//~ ...` 的 ignored comment。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary()` 编译单模块脚本；随后读取 `GeneratedClass`、`GeneratedFunction`、属性 metadata 与 `Engine.GetEnum()` 返回的 `EnumDesc`。 |
| 期望行为 | 编译成功且 diagnostics 为空；`GeneratedClass->GetMetaData(TEXT("ToolTip"))`、属性 `GetMetaData(TEXT("ToolTip"))`、函数 `GetMetaData(TEXT("ToolTip"))` 都精确等于规范化后的文本；`EnumDesc->Documentation` 与 `EnumDesc->Meta[(ToolTip, Index)]` 也应匹配规范化结果；ignored comment 文本不得出现在任何 metadata 中。 |
| 使用的 Helper | `CompileModuleWithSummary` + `FindGeneratedClass` + `FindGeneratedFunction` + `FindFProperty` + `Engine.GetEnum` |
| 优先级 | P2 |

#### NewTest-24：format string 预处理改写需要从源码一直验证到运行结果

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::GenerateFormatString` / `FAngelscriptPreprocessor::ParseFormatExpression` |
| 现有测试覆盖 | 当前 Preprocessor/Compiler 目标测试集完全无覆盖；`Plugins/Angelscript/Source/AngelscriptTest/Examples/AngelscriptScriptExampleFormatStringTest.cpp` 只验证 compile smoke，不验证 rewrite 后的具体行为 |
| 风险评估 | `f"..."` 的 rewrite 一旦在 escaped braces、`=` 展开或 format specifier 上退化，脚本仍可能成功编译，但运行结果 silently 错误；这类问题不会被现有目标测试发现。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.FormatStringRewriteProducesExpectedOutput` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineFormatStringTests.cpp` |
| 场景描述 | 在同一个 `Entry()` 中构造四类 format string：escaped braces、普通表达式插值、`{Expr =}`、`{Expr =:Specifier}`/`{Expr :Specifier}`，再用精确字符串比较把各分支结果编码成整数返回。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary()` 编译带 `Entry()` 的最小模块；脚本内例如比较 `f\"{{Alpha}}\" == \"{Alpha}\"`、`f\"{21 =}\" == \"21 = 21\"`、`f\"{255 :#06x}\" == \"0x00ff\"`、`f\"{Value =:.1f}\" == \"Value = 12.3\"`，全部命中时返回 `1111`。 |
| 期望行为 | 编译成功且 diagnostics 为空；`ExecuteIntFunction()` 返回 `1111`；任一 rewrite 分支出错都必须让返回值掉位，从而直接暴露 escaped brace、`=` 展开或 specifier 处理的回归。 |
| 使用的 Helper | `CompileModuleWithSummary` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-16 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 01:05)

### 一、现有测试问题

#### Issue-26：`CompilerPipeline` 8 个用例完全缺席 `USTRUCT` annotated 管线，现有全绿并不能证明 struct 预处理分支可用

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.*`（8 个用例） |
| 行号范围 | 11-499 |
| 问题描述 | 8 个用例里出现的 annotated 输入只有 `UCLASS(...)` 和 `UENUM(...)`，完全没有任何 `USTRUCT(...)` 场景；而目标源码里 `CompileModuleWithResult()` 明确把 `Script.Contains(TEXT("USTRUCT("))` 视为 annotated 预处理入口，`FAngelscriptPreprocessor::AnalyzeStructs()` 在 `AngelscriptPreprocessor.cpp:1320-1332` 还有独立错误路径，`ProcessPropertyMacro()` 在 `2395-2448` 也专门为 `ClassDesc->bIsStruct` 走 `DefaultPropertyEditSpecifierForStructs` 分支。也就是说，当前 `CompilerPipeline` 套件把 annotated compile 简化成“类和枚举能过”，struct 这条同级分支完全没有被现有断言触达。 |
| 影响 | 只要 `UCLASS/UENUM` 继续工作，`USTRUCT` 的继承限制、struct 属性默认 edit specifier、以及 script struct 的反射生成都可以已经回归而仍保持这 8 个用例全绿。对于名为 `Compiler.EndToEnd` 的套件，这会直接高估 annotated 编译管线的真实覆盖范围。 |
| 修复建议 | 至少补一条 dedicated `USTRUCT` end-to-end 用例进入当前主题，最好再配一条 direct preprocessor 用例把 `AnalyzeStructs()` 的错误分支单独锁住。命名可拆成 `Angelscript.TestModule.Compiler.EndToEnd.Struct...` 与 `Angelscript.TestModule.Preprocessor.Struct...`，并放入新的 `AngelscriptCompilerPipelineStructTests.cpp` / `AngelscriptPreprocessorStructTests.cpp`，避免继续把 struct 分支埋在 class/enum smoke 里。 |

### 二、需要新增的测试

#### NewTest-42：`USTRUCT` 继承错误必须在预处理阶段被直接拒绝

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::AnalyzeStructs` |
| 现有测试覆盖 | 当前目标集没有任何 `USTRUCT(...)` fixture；仓库里唯一 struct 相关自动化 `Angelscript.TestModule.Internals.StructCppOps.NotBlueprintTypeByDefault` 只编译 plain `struct FScopeConstructStruct`，不经过 `USTRUCT` 预处理路径，也不覆盖继承报错 |
| 风险评估 | 如果 `AnalyzeStructs()` 的 `Structs may not inherit from anything` 保护失效，annotated script struct 会带着非法基类继续进入后续 class generator/compile，错误定位会后移且更难追踪 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Structs.InheritanceRejected` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorStructTests.cpp` |
| 场景描述 | 构造 `USTRUCT() struct FDerivedStruct : FBaseStruct { UPROPERTY() int Value; }` 的最小脚本，只触发 struct 继承检查，不再混入 class/enum/import 场景 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；执行前清空 diagnostics |
| 期望行为 | `Preprocess()` 返回 `false`；diagnostics 精确包含 `Error parsing script struct FDerivedStruct. Structs may not inherit from anything.`；`Diagnostic.Row` 等于 struct 声明行；`GetModulesToCompile()` 不得留下可继续编译的有效 struct 描述 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P0 |

#### NewTest-43：struct `UPROPERTY()` 的默认 edit specifier 必须走 `DefaultPropertyEditSpecifierForStructs` 而不是 class 默认值

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessPropertyMacro` |
| 现有测试覆盖 | 当前目标集和仓库现有自动化都没有任何用例改写 `DefaultPropertyEditSpecifierForStructs` 并读取 struct property descriptor；现有 `MacroDetection` 只覆盖 class property 宏存在性 |
| 风险评估 | 一旦 `ClassDesc->bIsStruct ? DefaultPropertyEditSpecifierForStructs : DefaultPropertyEditSpecifier` 这条分支退化成总是走 class 默认值，script struct 在编辑器中的可编辑性会静默改变，而当前目标测试不会给出任何信号 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Structs.DefaultPropertySpecifierUsesStructSettings` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorStructTests.cpp` |
| 场景描述 | 临时把 `DefaultPropertyEditSpecifier` 设为 `NotEditable`、`DefaultPropertyEditSpecifierForStructs` 设为 `EditDefaultsOnly`，同一脚本里同时声明一个 `USTRUCT()` 和一个 `UCLASS()`，两者都只有不带显式 edit specifier 的 `UPROPERTY()` |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `GetMutableDefault<UAngelscriptSettings>()` 保存并恢复两个 setting；预处理后从 `GetModulesToCompile()` 中分别定位 struct/class 的 `FAngelscriptPropertyDesc` |
| 期望行为 | `Preprocess()` 成功且 diagnostics 为空；struct 属性描述应满足 `bEditableOnDefaults == true`、`bEditableOnInstance == false`；class 属性描述应保持 `bEditableOnDefaults == false`、`bEditableOnInstance == false`；两者都应保留正确 `PropertyName`，证明命中的是 struct-specific 分支而不是别的宏解析问题 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + `GetMutableDefault<UAngelscriptSettings>()` |
| 优先级 | P1 |

#### NewTest-44：annotated `USTRUCT` 需要一条真正从源码到 `UScriptStruct` 的 end-to-end 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::AnalyzeStructs` / `FAngelscriptEngine::CompileModules` |
| 现有测试覆盖 | 当前目标集 8 个 compiler pipeline 用例没有任何 `USTRUCT()`；仓库中的 `AngelscriptStructCppOpsTests.cpp` 只覆盖 plain `struct`，不经过 annotated preprocessor 管线 |
| 风险评估 | 如果 annotated struct 在预处理后无法生成 backing `UScriptStruct`、属性反射丢失，或脚本运行期访问 struct 字段失败，现有 `Compiler.EndToEnd.*` 仍会全绿，无法代表 annotated 类型全链路真的可用 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.AnnotatedStructRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineStructTests.cpp` |
| 场景描述 | 编译一个最小 annotated 脚本：`USTRUCT() struct FAnnotatedCarrier { UPROPERTY() int Value = 7; } int Entry() { FAnnotatedCarrier Carrier; return Carrier.Value; }`，随后同时检查脚本执行结果和 backing `UScriptStruct` |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)` 编译；编译后按 `AngelscriptStructCppOpsTests.cpp` 的现有模式从 `FAngelscriptEngine::GetPackage()` 查找 `UScriptStruct`（名称去掉前导 `F`） |
| 期望行为 | 编译成功，`bCompileSucceeded == true`、`CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；`ExecuteIntFunction()` 返回 `7`；`FindObject<UScriptStruct>(FAngelscriptEngine::GetPackage(), TEXT("AnnotatedCarrier"))` 非空；该 struct 上存在名为 `Value` 的 `FIntProperty`，证明 annotated struct 的预处理、反射生成和运行时访问三段链路全部打通 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `ExecuteIntFunction` + `FindObject<UScriptStruct>` |
| 优先级 | P0 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-26 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | MissingErrorPath: 1, MissingScenario: 1 |
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-08 23:37)

### 一、现有测试问题

#### Issue-17：`ImportParsing` 没有断言 provider-first 排序，manual import 的核心合同仍然漏检

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.ImportParsing` |
| 行号范围 | 199-218 |
| 问题描述 | 用例在 199-218 行只检查 `Preprocess()` 成功、`Modules.Num() == 2`、importing module 记录了 `ImportedModules`，以及原始 `import` 文本被移除；但 `ProcessImports()` 的首要职责是把模块重新排序成 provider-first 的可编译顺序，而当前测试从头到尾没有断言 `GetModulesToCompile()` 的返回顺序。即使回归后 `UsesImport` 仍出现在 `Shared` 之前，这个用例也会继续通过。 |
| 影响 | manual import 排序一旦退化成“依赖记录正确但模块顺序错误”，预处理专用测试不会给出任何信号；当前 3 个 preprocessor 用例里，最贴近 `ProcessImports()` 主合同的断言反而缺席。 |
| 修复建议 | 在现有用例内直接锁住顺序：断言 `Modules[0].Get().ModuleName == "Tests.Preprocessor.Shared"`、`Modules[1].Get().ModuleName == "Tests.Preprocessor.UsesImport"`，或至少断言 provider 的索引小于 importer。若担心单跳场景过弱，可同步追加一次轻量 compile/execute，证明排序后的模块列表确实能被下游按当前顺序消费。 |

#### Issue-18：8 个 `CompilerPipeline` 用例都重复声明两次 `ASTEST_CREATE_ENGINE_SHARE()`，属于明显的 helper 使用反模式

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.*`（8 个用例） |
| 行号范围 | 18-20，92-94，171-173，231-233，280-282，331-333，373-375，422-424 |
| 问题描述 | 每个用例都先写 `FAngelscriptEngine& EngineOwner = ASTEST_CREATE_ENGINE_SHARE();`，紧接着再写一行 `FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();`，随后 `ASTEST_BEGIN_SHARE` 只作用在 `Engine` 上。`AngelscriptTestMacros.h:42-46,97-104` 已经把 `ASTEST_CREATE_ENGINE_SHARE()` 定义为单个 process-level singleton，按规范只需要一份 `Engine` 变量；当前额外的 `EngineOwner` 从未被使用，只是在源里制造“似乎创建了两个测试引擎”的假象。 |
| 影响 | 这类 copy-paste setup 会掩盖真实的生命周期语义，后续一旦有人把该文件迁移到 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` / `ASTEST_CREATE_ENGINE_CLONE()`，双重调用就可能变成双重 reset 或双实例创建，直接把测试行为改坏。当前写法也让代码审查者更难看清 `ASTEST_BEGIN_SHARE` 究竟绑定的是哪一个引擎。 |
| 修复建议 | 把每个用例的开头收敛成一行明确的 helper 选择，例如 `FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();` 或在确实保留 `SHARE` 时只保留单个 `Engine` 变量。删除未使用的 `EngineOwner`，并让引擎创建宏与 `ASTEST_BEGIN_*`/`ASTEST_END_*` 配对关系在源码中保持一一对应。 |

#### Issue-19：`GeneratedClassConsistency` 与 `DelegateEnumClassCompile` 的 class 断言高度重复，8 个用例的独立覆盖被高估

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateEnumClassCompile` / `Angelscript.TestModule.Compiler.EndToEnd.GeneratedClassConsistency` |
| 行号范围 | 21-49，70-78，234-266 |
| 问题描述 | `DelegateEnumClassCompile` 里的 class 片段已经声明并验证了 `UCLASS(Abstract, BlueprintType) class UCompilerTransferObject : UObject { UPROPERTY() int Score; UFUNCTION() int GetScore() { return Score; } }`；而 `GeneratedClassConsistency` 又单独编译一个几乎同构的 `UCompilerConsistencyCarrier`，断言仍然是 `abstract flag + Score property + GetScore function` 三件事。两者的差异只剩类名与是否顺带塞了 delegate/enum 输入，真正新增的 class pipeline 证据极少。 |
| 影响 | 这会让“8 个 compiler pipeline 用例”看起来覆盖面比实际更广，但独立路径并没有相应增加；与此同时，像失败恢复、真实执行、复杂 metadata 这类更缺的场景却继续没有名额。 |
| 修复建议 | 保留一个用例承担最基础的 generated-class smoke；把另一个重写成当前文件真正缺失的路径，例如生成类方法的真实执行、reload/重新编译一致性、或更复杂的 class metadata round-trip。若短期不改脚本内容，至少在注释或测试名中明确它们是“基础 smoke + 组合 smoke”，避免误导后续覆盖评估。 |

### 二、需要新增的测试

#### NewTest-25：macro specifier 字符串里出现 `)` 仍必须完整解析并写入 metadata

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::FindScopeCloseBracket` / `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::ProcessClassMacro` |
| 现有测试覆盖 | 当前 Preprocessor/Compiler 目标测试集无覆盖；`MacroDetection` 只验证干净宏声明，`CommentMetadataRoundTrip` 关注注释规范化而非 macro 参数括号匹配 |
| 风险评估 | metadata 文本里合法出现 `)` 时，预处理器可能把字符串里的右括号误当成 macro 结束位置，导致后续 specifier 被截断或错绑；这类回归通常还能编译通过，但反射 metadata 会静默损坏。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.MacroMetadataStringsWithClosingParen` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineMetadataSpecifierTests.cpp` |
| 场景描述 | 编译一个 annotated 模块，在 `UCLASS`、`UFUNCTION`、`UENUM` 和 `UMETA` 中放入包含 `)` 的字符串参数，例如 `DisplayName="Do (Test)"`、`ToolTip="Accepts ) in text"`、`DisplayName="Alpha ) Value"`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary()` 走预处理+编译完整链路；随后读取生成类、生成函数和 `Engine.GetEnum()` 返回的 enum metadata。 |
| 期望行为 | 编译成功且 diagnostics 为空；类、函数和 enum/value 的 metadata 精确保留完整字符串，不允许出现被 `)` 截断、后半段丢失或 metadata 缺项；如 `GeneratedClass->GetMetaData(TEXT("DisplayName")) == "Do (Test)"`、enum value tooltip 精确等于 `Alpha ) Value`。 |
| 使用的 Helper | `CompileModuleWithSummary` + `FindGeneratedClass` + `FindGeneratedFunction` + `Engine.GetEnum` |
| 优先级 | P1 |

#### NewTest-26：delegate/event 声明在 `)` 与 `;` 之间带块注释时仍必须注册并可执行

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::FindSemicolonDirectlyAfter` / `FAngelscriptPreprocessor::ProcessDelegates` |
| 现有测试覆盖 | 当前 Preprocessor/Compiler 目标测试集无覆盖；仓库其他 `Delegate/` 与 `Examples/` 用例只覆盖无注释的干净声明 |
| 风险评估 | 只要有人在 delegate/event 签名后加一段 `/* comment */`，预处理器就可能完全漏掉该声明，导致 wrapper struct、`Module->Delegates` 和运行时调用路径一起消失；现有目标测试不会报红。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateDeclarationWithTrailingBlockCommentExecutes` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineDelegateExecutionTests.cpp` |
| 场景描述 | 编译一个 annotated 模块，声明 `delegate void FCommentSingle(int Value) /* note */;` 和 `event void FCommentMulti(int Value) /* note */;`；在脚本类里通过 `BindUFunction`/`AddUFunction` 绑定本类 `UFUNCTION`，再分别 `Execute` 和 `Broadcast`，最终返回写入的 `Score`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary()` 编译；随后 `FindGeneratedClass()` + `FindGeneratedFunction()` 定位返回 `int` 的脚本方法，并在新建对象上调用。 |
| 期望行为 | 编译成功且 diagnostics 为空；`Engine.GetDelegate(TEXT("FCommentSingle"))` 与 `Engine.GetDelegate(TEXT("FCommentMulti"))` 都有效；脚本方法执行后分别返回 single-cast 与 multicast 写入的目标值，证明带块注释的声明仍被 `ProcessDelegates()` 正确识别、生成并执行。 |
| 使用的 Helper | `CompileModuleWithSummary` + `FindGeneratedClass` + `FindGeneratedFunction` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-17 |
| AntiPattern | 2 | Issue-18 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingEdgeCase: 2 |

---

## 测试审查 (2026-04-08 23:45)

### 一、现有测试问题

#### Issue-20：`EnumAvailability` 只锁住计数和一个显式枚举值，隐式递增与 `UEnum` 挂接仍处于盲区

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.EnumAvailability` |
| 行号范围 | 334-360 |
| 问题描述 | 用例脚本声明了 `UENUM(BlueprintType) enum class ECompilerAvailabilityState : uint16 { Alpha, Beta = 4, Gamma }`，但断言只检查 `Engine.GetEnum()` 非空、`ValueNames.Num() == 3` 和 `EnumValues[1] == 4`。它没有验证 `ValueNames` 顺序、`Alpha == 0`、显式赋值后的隐式递增 `Gamma == 5`，也没有检查 `EnumDesc->Enum` 是否真的挂接到生成的 `UEnum`。 |
| 影响 | 只要枚举总数还是 3 且 `Beta` 恰好保持 4，这个用例就会继续通过；一旦 name/value 对应错位、显式值后的隐式递增回归，或反射层只剩描述对象没有真正的 `UEnum`，当前测试都不会报红。 |
| 修复建议 | 把断言升级成完整 round-trip：精确比较 `ValueNames` 为 `Alpha/Beta/Gamma`、`EnumValues` 为 `0/4/5`，并补 `TestNotNull(TEXT(\"Compiled enum should materialize a UEnum\"), EnumDesc->Enum)`。如果该用例仍承担 availability smoke，至少再加一个 `BlueprintType`/底层类型断言，避免继续把“枚举数量正确”误当成“枚举管线正确”。 |

### 二、需要新增的测试

#### NewTest-27：`bTreatAsDeleted` 必须把已删除脚本稳定转换为空模块描述

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::AddFile` / `FAngelscriptPreprocessor::Preprocess` |
| 现有测试覆盖 | 当前 3 个 preprocessor 用例都只添加真实存在的 fixture 文件；8 个 compiler pipeline 用例也只覆盖非删除路径，`bTreatAsDeleted` 分支完全无测试 |
| 风险评估 | `AddFile(..., bTreatAsDeleted=true)` 是热重载删除脚本时会走到的关键入口；如果这里回归成仍去读盘、残留旧源码或产生非空 code section，删除脚本后的 hot reload 很容易继续保留陈旧模块，而现有目标测试不会给出任何信号。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.DeletedFile.TreatAsDeletedProducesEmptyModule` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDeletedFileTests.cpp` |
| 场景描述 | 先在磁盘写入一个真实的 `.as` 文件，再调用 `Preprocessor.AddFile(RelativePath, AbsolutePath, false, true)`，验证 deleted 标志会覆盖磁盘上的实际内容。 |
| 输入/前置 | 使用 clean engine 和唯一 fixture 目录；脚本文件内容可包含一个简单 `int Entry() { return 7; }`，随后以 `bTreatAsDeleted=true` 加入同一路径。 |
| 期望行为 | `Preprocess()` 成功；`GetModulesToCompile()` 返回 1 个模块；模块名仍按相对路径规范化；`Code.Num() == 1` 且 `Code[0].Code.IsEmpty()`、`Code[0].CodeHash == 0`；`ImportedModules`、`PostInitFunctions` 和宏相关数据都为空，证明删除路径不会再把旧源码送入后续编译。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-28：`#if EDITOR` 需要锁住 `EditorOnlyBlockLines` 的精确行号映射

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` |
| 现有测试覆盖 | 当前目标测试只覆盖 `#if/#elif/#restrict` 的代码裁剪与诊断语义，没有任何用例读取 `Module->EditorOnlyBlockLines` |
| 风险评估 | `ParseIntoChunks()` 在 3111-3122 行把 `#if EDITOR` 行号范围写入 `Module->EditorOnlyBlockLines`，而 `AngelscriptEngine.cpp:4355` 会把这份数据继续交给 script builder；如果起止行漂移、嵌套块漏记或把非 editor 分支也记录进去，编译仍可能成功，但 editor-only 诊断与行映射会静默失真。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Directives.EditorOnlyBlockLinesRecorded` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` |
| 场景描述 | 构造两个独立的 `#if EDITOR ... #endif` 代码块，中间穿插普通函数；直接运行预处理并读取模块描述里的 `EditorOnlyBlockLines`。 |
| 输入/前置 | 使用 clean engine；脚本文本固定行号，例如第一段 editor block 在 1-3 行、第二段在 6-8 行；保持 editor 上下文的默认 `EDITOR` flag。 |
| 期望行为 | `Preprocess()` 成功；目标模块的 `EditorOnlyBlockLines.Num() == 2`，且两段范围精确等于 `(1, 3)` 与 `(6, 8)`；`Code[0].Code` 中不应残留 `#if`/`#endif` 文本。必要时再追加一次轻量 compile/execute，验证普通 `Entry()` 仍可返回预期值。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-20 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |


### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:21)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-90` / `NewTest-91`；为避免重复，此处不重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |

---

## 测试审查 (2026-04-10 00:21)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-90：single-cast delegate 未绑定时的 `ExecuteIfBound()` 必须返回稳定默认值

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `GetReturnInit` / `FAngelscriptPreprocessor::ProcessDelegates` |
| 现有测试覆盖 | 当前目标集只验证 delegate 元数据存在性；仓库其他测试也仅覆盖 `ExecuteIfBound()` 方法暴露或示例脚本可编译，没有任何自动化真正执行未绑定 single-cast delegate 的 `ExecuteIfBound()` 并检查返回值 |
| 风险评估 | `ProcessDelegates()` 在 675-705 行会为有返回值的 delegate 生成 `__ReturnValue` 初始化和 `ExecuteIfBound()` 早退逻辑；若 `GetReturnInit()` 映射、返回值 push 方式或 early-return 分支回归，脚本会在“未绑定但允许静默返回”的常见路径上得到未初始化值或错误异常，而当前 Preprocessor/Compiler 套件不会报红 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateExecuteIfBoundReturnsDefaultValue` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineDelegateRuntimeTests.cpp` |
| 场景描述 | 编译一个最小脚本模块，声明 `delegate int FRuntimeValueDelegate();`，在 `Entry()` 中创建未绑定实例并直接返回 `Delegate.ExecuteIfBound()`；为避免只锁住 `int`，可在同文件追加 sibling case，声明 `delegate bool FRuntimeBoolDelegate();` 并断言默认返回 `false` |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(&Engine, ECompileType::FullReload, TEXT("CompilerDelegateExecuteIfBoundReturnsDefaultValue"), TEXT("Tests/Compiler/CompilerDelegateExecuteIfBoundReturnsDefaultValue.as"), Script, true, Summary, false)` 走真实 preprocessor + compile 链路；随后用 `ExecuteIntFunction()` 执行 `int Entry()`，若追加 `bool` 子场景则用现有 bool execute helper 或包一层 `int` 适配函数 |
| 期望行为 | 编译成功且 `bCompileSucceeded == true`、`CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；执行 `Entry()` 时不得抛出 `Executing unbound delegate.`，返回值必须精确等于 `0`；若追加 `bool` 子场景，则对应入口必须返回 `0/false`。这组断言要证明 `GetReturnInit()` 生成的默认值和 `ExecuteIfBound()` 的 unbound 早退逻辑都正确落到了最终字节码 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-91：single-cast delegate 未绑定时的 `Execute()` 必须抛出稳定运行时异常

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessDelegates` |
| 现有测试覆盖 | 当前目标集没有任何用例执行 unbound single-cast delegate 的 `Execute()`；仓库其他测试也未校验 `Throw("Executing unbound delegate.")` 这条生成分支的异常字符串与行号 |
| 风险评估 | `ProcessDelegates()` 在 694-699 行为 `Execute()` 生成了显式的 unbound 错误路径。若这段模板改坏，用户会从“稳定的脚本异常”退化成静默返回、错误文本漂移或上下文崩溃；现有 Preprocessor/Compiler 套件无法第一时间指出问题出在 delegate wrapper 生成 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateExecuteReportsUnboundRuntimeError` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineDelegateRuntimeTests.cpp` |
| 场景描述 | 编译一个最小脚本模块，声明 `delegate int FRuntimeValueDelegate();`，在 `Entry()` 中创建未绑定实例并直接 `return Delegate.Execute();`。执行阶段不走普通 `ExecuteIntFunction()`，而是显式拿到 `Entry()` 对应 `asIScriptFunction` 并验证脚本异常 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 编译；随后用本地 exception helper 执行 `Entry()`，实现方式可直接复用 `Bindings/AngelscriptArrayEdgeBindingsTests.cpp` 中 `ExecuteFunctionExpectingScriptException` 的模式，读取 `asIScriptContext` 的 `GetExceptionString()` / `GetExceptionLineNumber()` |
| 期望行为 | 编译成功且 `CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；执行 `Entry()` 时必须得到 `asEXECUTION_EXCEPTION`，异常字符串精确等于 `Executing unbound delegate.`，异常行号固定落在 `Delegate.Execute()` 那一行；不得退化成返回 `0` 的静默成功，也不得出现空异常字符串 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + 其他（本地 exception helper，模式参照 `Bindings/AngelscriptArrayEdgeBindingsTests.cpp`） |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |

### 本轮汇总（对应 2026-04-09 01:12 轮次）

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 4 | MissingErrorPath: 3, MissingScenario: 1 |
| P2 | 2 | MissingErrorPath: 1, NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 01:31) 续

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | NoTestForSource: 1, MissingErrorPath: 2 |
| P2 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 01:31)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-52：`ReplicationCondition` 必须从 property specifier 一直落到 generated `FProperty`

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessPropertyMacro` / `AngelscriptClassGenerator::AddClassProperties` |
| 现有测试覆盖 | 仓库自动化中未发现 `ReplicationCondition` 关键字；当前目标集只覆盖 `ReplicatedUsing` / `BlueprintGetter` / `BlueprintSetter`，没有任何用例检查 `SetBlueprintReplicationCondition()` 的正向映射 |
| 风险评估 | 如果 `OwnerOnly`、`SkipReplay` 等条件在预处理或 class generation 阶段被映射错、退化成 `COND_None`，脚本类仍可能成功编译，但网络复制行为会静默漂移，现有 Preprocessor/Compiler 套件不会报红 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.PropertyReplicationConditionRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineReplicationPropertyTests.cpp` |
| 场景描述 | 编译一个最小 annotated actor 或 object 类，声明两条 `UPROPERTY(Replicated, ReplicationCondition=...)`：一条使用 `OwnerOnly`，一条使用 `SkipReplay`，并保留一个简单 `Entry()` 返回固定值，确保完整编译后还能执行 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)` 编译；编译后用 `FindGeneratedClass()` + `FindFProperty<FProperty>()` 读取两个属性；如需更强证据，可再从 `UASClass::GetLifetimeScriptReplicationList()` 或 `GetBlueprintReplicationCondition()` 读取实际 replication condition |
| 期望行为 | 编译成功且 `CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；`ExecuteIntFunction()` 返回固定值（例如 `42`）；两个生成属性都带 `CPF_Net`；`OwnerOnlyValue->GetBlueprintReplicationCondition() == COND_OwnerOnly`，`SkipReplayValue->GetBlueprintReplicationCondition() == COND_SkipReplay`，证明 preprocessor 的枚举映射和 class generator 的落地结果都正确 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `ExecuteIntFunction` + `FindGeneratedClass` + `FindFProperty` |
| 优先级 | P1 |

#### NewTest-53：`ShowOnActor` 必须同时覆盖非法用法报错与合法组件语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessPropertyMacro` |
| 现有测试覆盖 | 仓库自动化中未发现 `ShowOnActor` 断言；当前仅有 `Examples/AngelscriptScriptExamplePropertySpecifiersTest.cpp` 展示脚本写法，没有任何自动化验证 `ShowOnActor` 的错误消息或 property desc 结果 |
| 风险评估 | `ShowOnActor` 会直接改写 `bEditableOnDefaults`、`bEditableOnInstance`、`EditInline` 和 default-component 相关语义；如果这里开始静默接受错误用法，或合法场景不再把组件暴露到 actor details，现有目标测试不会给出信号 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Properties.ShowOnActorRequiresDefaultComponent` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorComponentSpecifierTests.cpp` |
| 场景描述 | 采用 table-driven 两个子场景：1. `AActor` 类里声明 `UPROPERTY(ShowOnActor) int PlainValue;`，只触发非法组合；2. `AActor` 类里声明 `UPROPERTY(DefaultComponent, ShowOnActor, RootComponent) USceneComponent RootScene;`，验证合法组件路径的 property desc 语义 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；每个子场景独立构造 `FAngelscriptPreprocessor` 并在执行前清空 diagnostics |
| 期望行为 | 非法子场景的 `Preprocess()` 必须返回 `false`，diagnostics 精确包含 `ShowOnActor can only be used on default components in actors`，且 `Diagnostic.Row` 指向目标 `UPROPERTY` 行；合法子场景的 `Preprocess()` 必须成功且 diagnostics 为空，`FAngelscriptPropertyDesc` 需要满足 `bInstancedReference == true`、`bEditableOnDefaults == true`、`bEditableOnInstance == true`、`bBlueprintReadable == true`、`bBlueprintWritable == false`，并且 `Meta` 同时包含 `DefaultComponent=True`、`RootComponent` 与 `EditInline=true` |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-54：`BindWidget` 必须把属性收敛为只读 Blueprint-visible 元数据

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessPropertyMacro` / `AngelscriptClassGenerator::AddClassProperties` |
| 现有测试覆盖 | 仓库自动化中未发现 `BindWidget` 断言；`Examples/AngelscriptScriptExampleWidgetUmgTest.cpp` 只有示例脚本，没有任何自动化验证生成属性的 metadata、Blueprint 可见性和编辑性 |
| 风险评估 | `BindWidget` 分支会同时改写 `Meta`、Blueprint 读写权限和编辑标志；如果这里退化成可写、可编辑，或 metadata 没有落到生成属性，UI 绑定问题只会在 Widget 蓝图联调阶段暴露 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.BindWidgetPropertyFlagsRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineWidgetMetadataTests.cpp` |
| 场景描述 | 编译一个最小 annotated 类，声明 `UPROPERTY(BindWidget) UObject MainText;` 和一个 `Entry()` 返回固定值；编译后读取生成属性的 metadata 与 flags |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)` 编译；编译后用 `FindGeneratedClass()` + `FindFProperty<FProperty>()` 获取 `MainText` |
| 期望行为 | 编译成功且 `CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；`ExecuteIntFunction()` 返回固定值（例如 `42`）；`MainText` 属性必须满足 `GetMetaData(TEXT("BindWidget"))` 存在、`HasAnyPropertyFlags(CPF_BlueprintVisible)` 为真、`HasAnyPropertyFlags(CPF_BlueprintReadOnly)` 为真、`HasAnyPropertyFlags(CPF_Edit)` 为假，证明 `BindWidget` 在 preprocessor 和 class generator 两端都把属性收敛为只读 Blueprint-visible 绑定点 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `ExecuteIntFunction` + `FindGeneratedClass` + `FindFProperty` |
| 优先级 | P2 |

#### NewTest-55：同步 `AddFile()` 遇到缺失脚本文件时必须稳定退化为空模块

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::AddFile` / `FAngelscriptPreprocessor::Preprocess` |
| 现有测试覆盖 | 当前目标集只覆盖真实存在文件、`bTreatAsDeleted=true` 的显式删除路径，以及异步读盘一致性建议；没有任何用例触发 `LoadFileToString()` 失败后“Treating file as deleted”的同步 fallback 分支 |
| 风险评估 | 如果缺失文件路径开始残留旧源码、产生半截 `RawCode`，或与显式删除路径语义不一致，脚本被删/改名后的热重载会继续携带陈旧内容，而现有 Preprocessor/Compiler 套件不会给出信号 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.FileLoad.MissingFileProducesEmptyModule` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorFileLoadTests.cpp` |
| 场景描述 | 构造一个确定不存在的 `AbsoluteFilename`，调用 `Preprocessor.AddFile(RelativePath, MissingAbsolutePath, false, false)`，随后直接运行预处理并检查模块描述；如需更强证据，可先创建再删除同名文件，证明 fallback 依赖的是当前磁盘状态而非旧内容 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；测试前确认目标绝对路径不存在；只向 preprocessor 添加这一份缺失文件，避免其他模块干扰结果 |
| 期望行为 | `Preprocess()` 成功；`GetModulesToCompile()` 返回 1 个模块；模块名按相对路径规范化；`Code.Num() == 1` 且 `Code[0].Code.IsEmpty()`、`Code[0].CodeHash == 0`；`ImportedModules`、`Delegates`、`Classes` 和 `Enums` 都为空，证明缺失文件与显式 delete 路径一样不会把陈旧源码继续送入后续编译 |
| 使用的 Helper | `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P2 |

#### NewTest-56：未知 `ReplicationCondition` 必须在预处理阶段给出精确 property 级诊断

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessPropertyMacro` |
| 现有测试覆盖 | 当前目标集已计划覆盖 `ReplicatedUsing` / `BlueprintGetter` / `BlueprintSetter` 的空值错误，但没有任何用例触发 `Unknown ReplicationCondition %s on property ...` 这条专属诊断分支；仓库自动化搜索也未发现 `ReplicationCondition` 断言 |
| 风险评估 | 如果未知 replication condition 被静默接受、错误文本漂移，或行号不再指向目标属性，网络复制配置错误会延迟到 class generation/runtime 才暴露，定位成本明显升高 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Properties.UnknownReplicationConditionReportsDiagnostic` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPropertyMacroErrorTests.cpp` |
| 场景描述 | 构造一个最小 `UCLASS()`，在其中声明 `UPROPERTY(Replicated, ReplicationCondition=DefinitelyUnknown) int TrackedValue;`，只触发这一条 property-specifier 错误 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；执行前清空 diagnostics；属性名固定为 `TrackedValue` 以便精确匹配消息文本 |
| 期望行为 | `Preprocess()` 返回 `false`；diagnostics 精确包含 `Unknown ReplicationCondition DefinitelyUnknown on property UBadReplicationCarrier::TrackedValue.`；`Diagnostic.Row` 指向目标 `UPROPERTY` 行；`GetModulesToCompile()` 不应留下可继续消费的半合法 replication property descriptor |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

---

## 测试审查 (2026-04-09 01:12)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-46：RPC `UFUNCTION` 的 `Server/Client/WithValidation` 约束必须有精确错误回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessFunctionMacro` |
| 现有测试覆盖 | 当前目标集只覆盖 `BlueprintEvent/BlueprintOverride` 相关非法组合；1603-1636 行的 RPC 校验分支完全没有被现有 11 个用例触发 |
| 风险评估 | 如果 `Server` 缺 `WithValidation` 或孤立 `WithValidation` 被静默放行，错误会延后到更后面的编译/运行期才暴露；这类网络函数合同一旦回归，现有 preprocessor/compiler 套件不会给出第一时间诊断 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.FunctionMacros.RpcValidationSpecifiersReportDiagnostics` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorFunctionMacroErrorTests.cpp` |
| 场景描述 | 用 table-driven 子场景至少覆盖两份最小脚本：1. `UFUNCTION(Server)` 成员函数缺少 `WithValidation`；2. `UFUNCTION(WithValidation)` 成员函数未同时声明 `Server` 或 `Client`。两份脚本都放在最小 `UCLASS()` 容器里，直接运行预处理 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；每个子场景执行前清空 diagnostics，并保持 automatic import 默认值不变，避免引入无关 warning |
| 期望行为 | 两个子场景都应 `Preprocess()` 返回 `false`；diagnostics 精确包含 `UFUNCTION() MissingValidation is marked as Server but does not have the WithValidation property specified!` 与 `UFUNCTION() OrphanValidation has the WithValidation property without the Server or Client property!`；`Diagnostic.Row` 必须落在对应 `UFUNCTION` 行；`GetModulesToCompile()` 不得留下可继续编译的合法 wrapper |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-47：`UPROPERTY` 的回调类 specifier 缺参和 unknown specifier 必须直接报 macro 诊断

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessPropertyMacro` |
| 现有测试覆盖 | 当前目标集只验证 `UPROPERTY(EditAnywhere, BlueprintReadWrite)` 被采集，以及默认 blueprint access setting 的正向分支；2579-2742 行的 `ReplicatedUsing` / `BlueprintSetter` / `BlueprintGetter` 错误与 unknown property specifier 完全无覆盖 |
| 风险评估 | 这些错误路径一旦退化成静默放行，属性元数据会带着半合法配置继续流入 class generator 和 Blueprint 绑定层，最终故障会在更晚阶段才出现，定位成本明显上升 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.PropertyMacros.InvalidCallbackSpecifiersReportDiagnostics` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPropertyMacroErrorTests.cpp` |
| 场景描述 | 用 table-driven 子场景覆盖至少四份最小脚本：1. `UPROPERTY(ReplicatedUsing)`；2. `UPROPERTY(BlueprintSetter)`；3. `UPROPERTY(BlueprintGetter)`；4. `UPROPERTY(DefinitelyUnknownSpecifier)`。四个子场景都放在同一个最小 `UCLASS()` 壳内，只触发一个目标错误 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；每个子场景通过 `FAngelscriptTestFixture` 写入单文件脚本；执行前清空 diagnostics；属性名建议固定为 `TrackedValue`，便于精确比对消息文本 |
| 期望行为 | 四个子场景都应 `Preprocess()` 返回 `false`；diagnostics 分别精确包含 `No function specified for ReplicatedUsing on property UBadPropertyCarrier::TrackedValue.`、`No function specified for BlueprintSetter on property UBadPropertyCarrier::TrackedValue.`、`No function specified for BlueprintGetter on property UBadPropertyCarrier::TrackedValue.`、`Unknown property specifier DefinitelyUnknownSpecifier on property UBadPropertyCarrier::TrackedValue.`；`Diagnostic.Row` 必须等于目标 `UPROPERTY` 行，且 `GetModulesToCompile()` 不应留下可继续消费的半合法 property descriptor |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-48：`ReplicatedUsing` 与 `BlueprintGetter/Setter` 必须贯通到 generated property metadata

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessPropertyMacro` / `FAngelscriptClassGenerator::GenerateClass` |
| 现有测试覆盖 | 当前目标集没有任何用例把 `ReplicatedUsing`、`BlueprintGetter`、`BlueprintSetter` 的正向 metadata 从脚本一路验证到 generated `FProperty`；现有 property 相关覆盖只停留在默认值和可见性设置 |
| 风险评估 | 只测错误路径不能证明正向元数据仍会落到 `FProperty`；如果 preprocessor 记录了 metadata 但 class generator 丢失了 `RepNotifyFunc`、`BlueprintGetter` 或 `BlueprintSetter`，现有 compiler tests 仍会保持全绿 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.PropertyCallbackMetadataRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelinePropertyMetadataTests.cpp` |
| 场景描述 | 编译一个最小 annotated 类 `UPropertyCallbackCarrier`，声明 `UPROPERTY(ReplicatedUsing=OnRep_TrackedValue, BlueprintGetter=GetTrackedValue, BlueprintSetter=SetTrackedValue)` 的 `int TrackedValue;`，并同时提供三个对应 `UFUNCTION()`。脚本再提供一个最小 `Entry()` 返回固定值，确保完整编译后还能执行 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)` 编译；编译后用 `FindGeneratedClass()` + `FindFProperty<FProperty>()` 获取 `TrackedValue`，再用 `ExecuteIntFunction()` 跑 `Entry()` |
| 期望行为 | 编译成功且 `Diagnostics.Num() == 0`；`ExecuteIntFunction()` 返回固定值（例如 `42`）；生成属性必须同时满足 `HasAnyPropertyFlags(CPF_Net)`、`HasAnyPropertyFlags(CPF_RepNotify)`、`RepNotifyFunc == TEXT("OnRep_TrackedValue")`；`GetMetaData(TEXT("BlueprintGetter")) == "GetTrackedValue"`、`GetMetaData(TEXT("BlueprintSetter")) == "SetTrackedValue"`；对应三个生成 `UFunction` 也必须存在，证明 preprocessor 记录与 class generator 落地两端都被锁住 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `FindGeneratedClass` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-49：`UCLASS` / `UENUM` 的 unknown specifier 必须在预处理阶段被拒绝

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessClassMacro` / `FAngelscriptPreprocessor::DetectEnum` |
| 现有测试覆盖 | 当前目标集多次使用合法 `UCLASS(Abstract, BlueprintType)` 和 `UENUM(BlueprintType)`，但 2348-2352 / 2851-2855 行的 unknown specifier 拒绝分支完全没有自动化覆盖 |
| 风险评估 | class/enum specifier 一旦开始被静默吞掉，脚本表面上可能仍能继续编译到更后阶段，真正的 metadata 或类型语义会悄悄漂移；现有 preprocessor/compiler 套件不会第一时间把错误定位到 specifier 解析层 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.TypeMacros.UnknownSpecifiersReportDiagnostics` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTypeMacroErrorTests.cpp` |
| 场景描述 | 用 table-driven 子场景覆盖两份最小脚本：1. `UCLASS(DefinitelyUnknownSpecifier) class UBadClassCarrier : UObject {}`；2. `UENUM(DefinitelyUnknownSpecifier) enum class EBadEnum : uint8 { Alpha }`。两个脚本都只保留最小可解析结构，避免引入其他错误 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；执行前清空 diagnostics |
| 期望行为 | 两个子场景都应 `Preprocess()` 返回 `false`；diagnostics 精确包含 `Unknown class specifier DefinitelyUnknownSpecifier on class UBadClassCarrier.` 与 `Unknown enum specifier DefinitelyUnknownSpecifier on enum EBadEnum.`；`Diagnostic.Row` 必须等于对应宏行；`GetModulesToCompile()` 不得继续输出带半合法 metadata 的 class/enum desc |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P2 |

#### NewTest-50：`CannotDeriveAngelscript` native metadata 必须阻止脚本继承

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ResolveSuperClass` |
| 现有测试覆盖 | 当前目标集虽然多次验证普通 `UObject` 继承成功，但 2930-2939 行针对 native class `CannotDeriveAngelscript` metadata 的拒绝分支没有任何覆盖，仓库内也未找到现成测试基类使用该 metadata |
| 风险评估 | 如果这条 guard 回归，脚本将能错误继承本应禁止的 native 类型；问题不会在现有 compiler pipeline 套件里暴露，因为它们只覆盖普通可继承基类 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Inheritance.CannotDeriveMetadataRejectsNativeBase` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorInheritanceGuardTests.cpp`，并新增一个小型 native helper 头/源，例如 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/Shared/AngelscriptPreprocessorInheritanceTestTypes.h/.cpp` |
| 场景描述 | 在 helper 类型里声明 `UCLASS(meta=(CannotDeriveAngelscript)) class UAngelscriptNoDeriveBase : UObject`；脚本侧编写 `UCLASS() class UBadDerived : UAngelscriptNoDeriveBase {}`，直接运行预处理或完整 annotated compile |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；保证 helper 类型已被 test module 编译进反射系统；脚本通过 `FAngelscriptTestFixture` 写入单文件模块；执行前清空 diagnostics |
| 期望行为 | `Preprocess()` 应返回 `false`；diagnostics 精确包含 `Class UBadDerived cannot inherit from C++ class UAngelscriptNoDeriveBase which specifies CannotDeriveAngelscript meta`；`Diagnostic.Row` 等于 class 声明行；`GetModulesToCompile()` 不得继续输出可编译的 `UBadDerived` class desc |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + test-module native helper type |
| 优先级 | P2 |

#### NewTest-51：property callback 的签名校验必须在 compile 阶段给出稳定诊断

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptEngine::VerifyRepFunc` / `FAngelscriptEngine::VerifyBlueprintSetFunc` / `FAngelscriptEngine::VerifyBlueprintGetFunc` |
| 现有测试覆盖 | 当前目标集没有任何 compile-failure 用例去触发 `ReplicatedUsing`、`BlueprintSetter`、`BlueprintGetter` 对应回调函数的签名验证；现有 compiler pipeline 只检查成功注册后的 class/enum/delegate 形状 |
| 风险评估 | 如果 compile 阶段开始错误接受错误签名，preprocessor 侧 metadata 看起来仍然正确，但生成类在网络复制和 Blueprint 访问时会留下延迟爆炸点；这类回归不会被当前 8 个 compiler 用例捕获 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.PropertyCallbackSignatureValidationReportsDiagnostics` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelinePropertyMetadataTests.cpp` |
| 场景描述 | 用 table-driven 子场景至少覆盖三份 annotated 脚本：1. `ReplicatedUsing=OnRep_TrackedValue` 但 `OnRep_TrackedValue` 带两个参数；2. `BlueprintSetter=SetTrackedValue` 但 setter 参数类型与属性类型不匹配；3. `BlueprintGetter=GetTrackedValue` 但 getter 未标 `BlueprintPure` 或带参数 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；统一走 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)`；每个子场景都声明同名属性 `TrackedValue`，以便精确匹配 diagnostics |
| 期望行为 | 三个子场景都应 `bCompileSucceeded == false`；diagnostics 分别精确包含 `can not have more than 1 argument.`、`takes an argument of type`、以及 `needs to be marked as BlueprintPure.` 或 `should not take any arguments.` 中对应文本；所有 diagnostics 的 row 都应指向 property 或回调函数声明的稳定行号；`FindGeneratedClass(TEXT("UPropertyCallbackCarrier"))` 必须为空，避免留下半生成类型 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `FindGeneratedClass` |
| 优先级 | P1 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 00:02)

### 一、现有测试问题

#### Issue-21：`ModuleFunctionInspection` 用名字回退 helper 冒充声明检查，默认参数元数据实际上没有被锁住

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.ModuleFunctionInspection` |
| 行号范围 | 283-317 |
| 问题描述 | 用例在 303-309 行通过 `AngelscriptTestSupport::GetFunctionByDecl(*this, *Module, TEXT("int SumWithDefault(int, int)"))` 查找函数，并把“找到函数 + `GetParamCount() == 2`”当成 inspection 结果；但 `AngelscriptTestUtilities.h:600-628` 里的 helper 在 declaration 精确匹配失败后会退回 `GetFunctionByName()`/按名字遍历，这意味着只要模块里还存在一个同名函数，测试就可能继续通过。更关键的是，`SumWithDefault` 真正敏感的合同是参数名与 default arg 文本，而当前断言既不调用 `asIScriptFunction::GetParam(..., &ParamName, &ParamDefaultArg)`，也不比较任何默认值元数据。 |
| 影响 | 如果编译器把声明改成别的返回类型、补出同名重载、丢失参数名，或把 `21/21` 的默认参数文本编坏成别的表达式，这个“ModuleFunctionInspection” 仍可能保持全绿；结果是 8 个 compiler pipeline 用例里唯一声称做函数 inspection 的用例，实际上没有对 declaration/default-arg round-trip 提供可靠保护。 |
| 修复建议 | 在该用例里停用名字回退 helper：先直接调用 `Module->GetFunctionByDecl("int SumWithDefault(int, int)")` 并断言非空，再用 `GetParam(0/1, ..., &ParamName, &ParamDefaultArg)` 精确检查参数名和默认值文本，例如第 0/1 个参数名分别为 `Value`/`Extra`，default arg 分别为 `21`/`21`。执行断言可以保留，但应和 declaration 元数据断言并列，避免继续把“运行结果为 42”误当成“函数声明也正确”。 |

### 二、需要新增的测试

#### NewTest-29：全局函数默认参数需要锁住 `GetParam()` 暴露的名字与 defaultArg 文本

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 关联函数 | `FAngelscriptPreprocessor::Preprocess` / `asIScriptFunction::GetParam` |
| 现有测试覆盖 | 当前目标集只有 `ModuleFunctionInspection` 和 `FunctionDefaultsAndClassLikeCompile` 会把默认参数跑到 `42`，但都不检查参数名或 defaultArg 元数据 |
| 风险评估 | 默认参数文本不仅影响运行时省参调用，还会进入 overload resolution、reload diff 与调试/文档输出；如果只测执行结果，不测 `GetParam()` 暴露的声明元数据，这条链路一旦回归就会长期静默。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.FunctionDefaultMetadataRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineFunctionDefaultTests.cpp` |
| 场景描述 | 编译一个最小模块，声明 `int SumWithDefaults(int Required, int Value = 21, int Extra = 7)`，同时保留 `Entry()` 用省参方式调用它。编译后既检查运行结果，也直接检查 `asIScriptFunction` 的参数名与默认值元数据。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `FAngelscriptTestFixture`；优先走 `CompileModuleWithSummary(..., bUsePreprocessor=false)` 收集 diagnostics；从 `Engine->GetModuleByModuleName()` 拿到 `ScriptModule` 后直接调用原生 `GetFunctionByDecl()` 与 `GetParam()`。 |
| 期望行为 | 编译成功且 diagnostics 为空；`Entry()` 执行结果为 `42`；`SumWithDefaults` 精确按声明查找成功；第 0 个参数名为 `Required` 且 defaultArg 为 `nullptr`，第 1/2 个参数名为 `Value`/`Extra` 且 defaultArg 分别精确等于 `21`/`7`；`GetParamCount()` 恰好为 3，不允许同名重载或名字回退掩盖声明漂移。 |
| 使用的 Helper | `CompileModuleWithSummary` + `ExecuteIntFunction` + 原生 `asIScriptFunction::GetParam` |
| 优先级 | P1 |

#### NewTest-30：class-like 签名需要从 generated `UFUNCTION` 一直跑到真实执行结果

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessFunctionMacro` / `FAngelscriptPreprocessor::Preprocess` |
| 现有测试覆盖 | 当前目标集只有 `ClassLikeReflectionShape` 验证 `FClassProperty` / `FSoftClassProperty` 形状；仓库其他 `Bindings/AngelscriptClassBindingsTests.cpp` 覆盖了脚本语法，但没有经过 generated class pipeline |
| 风险评估 | 反射形状正确并不代表生成的 `UFUNCTION` 能把 `UClass`、`TSubclassOf<AActor>`、`TSoftClassPtr<AActor>` 正确编译成可执行字节码；一旦 marshalling、return path 或 generated wrapper 回归，现有 compiler suite 仍会全绿。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.ClassLikeMethodExecutionRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineClassLikeExecutionTests.cpp` |
| 场景描述 | 编译一个 annotated `UObject` 类，保留现有 `EchoPlainClass` / `EchoActorClass` / `EchoSoftActorClass` 三个方法，再新增一个 `UFUNCTION() int VerifyRoundTrip()`，在脚本内部用 `AActor::StaticClass()`、`ACameraActor::StaticClass()` 和 `TSoftClassPtr<AActor>(...)` 逐项调用这些方法并返回编码结果。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 编译 annotated 模块；随后 `FindGeneratedClass()`、`FindGeneratedFunction()`，再用 `NewObject<UObject>(GetTransientPackage(), GeneratedClass)` + `ExecuteGeneratedIntEventOnGameThread()` 执行 `VerifyRoundTrip`。 |
| 期望行为 | 编译成功且 diagnostics 为空；generated class 与 `VerifyRoundTrip` 函数都存在；运行结果精确等于成功码（例如 `1`），并在脚本内部同时验证 `EchoPlainClass(AActor::StaticClass()) == AActor::StaticClass()`、`EchoActorClass(ACameraActor::StaticClass()) == ACameraActor::StaticClass()`、`EchoSoftActorClass(TSoftClassPtr<AActor>(AActor::StaticClass())).Get() == AActor::StaticClass()`。任一 marshalling/return path 退化都必须落到不同返回码。 |
| 使用的 Helper | `CompileModuleWithSummary` + `FindGeneratedClass` + `FindGeneratedFunction` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-21 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-09 00:13)

### 一、现有测试问题

#### Issue-22：`ImportParsing` 只用 `Contains` 检查依赖，无法发现重复或脏 import 记录

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.ImportParsing` |
| 行号范围 | 204-219 |
| 问题描述 | 用例在 216-218 行只检查 `ImportingModule->ImportedModules.Contains(TEXT("Tests.Preprocessor.Shared"))`，然后确认源码里的 `import` 文本被移除；它没有断言 `ImportedModules.Num()`，也没有限制依赖列表只能包含这一项。这样即使 `ProcessImports()` 因回归把同一个模块写入多次，或顺手残留了其他无关依赖，当前测试也会继续通过。 |
| 影响 | manual import 的核心产物是“精确且可编译的依赖清单”。如果依赖列表出现重复项或脏数据，后续编译、热重载依赖图和诊断排序都可能被污染，但该用例仍会给出绿灯。 |
| 修复建议 | 把断言升级成精确集合验证：`ImportedModules.Num() == 1` 且唯一值等于 `Tests.Preprocessor.Shared`；同时补 provider 模块 `ImportedModules.Num() == 0`。如果该用例继续承担 manual import smoke，还应联动现有 `TopologicalOrderRespectsDependencyChain` 场景，把“依赖集合正确”和“排序正确”分开锁住。 |

### 二、需要新增的测试

#### NewTest-31：public preprocess hook 必须按固定阶段顺序触发并暴露正确状态

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::OnProcessChunks` / `FAngelscriptPreprocessor::OnPostProcessCode` / `FAngelscriptPreprocessor::PostProcessLiteralAssets` |
| 现有测试覆盖 | 仓库 `Learning.Runtime.Preprocessor` 只观察到两个 hook 都会触发；当前 `Preprocessor/` 与 `Compiler/` 目标套件没有锁定触发顺序，也没有验证 hook 时点看到的数据阶段 |
| 风险评估 | 这两个 delegate 是公开扩展点；一旦广播顺序漂移、漏触发，或把 `OnProcessChunks` 提前/延后到错误阶段，依赖 hook 的扩展逻辑会静默拿到错误状态，现有目标套件不会报红。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Hooks.ProcessAndPostProcessFireInOrder` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorHookTests.cpp` |
| 场景描述 | 为 `OnProcessChunks` 与 `OnPostProcessCode` 注册两个 lambda，预处理一个包含 `asset PreviewAsset of UObject` 的最小模块，用 asset literal 区分 pre/post 阶段的可见状态。 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或 clean engine；通过 fixture 写入单模块脚本；在 `ON_SCOPE_EXIT` 中移除 hook；脚本同时保留一个简单 `Entry()` 以保证模块有普通代码。 |
| 期望行为 | `Preprocess()` 成功；事件顺序精确为 `OnProcessChunks` 后 `OnPostProcessCode`；在 `OnProcessChunks` 回调里目标模块 `PostInitFunctions.Num() == 0`，且 `ProcessedCode` 仍为空或尚未去掉原始 `asset PreviewAsset of UObject`；在 `OnPostProcessCode` 回调里 `PostInitFunctions` 精确包含 `GetPreviewAsset`，并且 `ProcessedCode` 已不再包含原始 asset literal 文本。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` + `ON_SCOPE_EXIT` |
| 优先级 | P2 |

#### NewTest-32：`BlueprintEvent` wrapper 需要从预处理改写一直跑到真实执行结果

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessFunctionMacro` / `FAngelscriptPreprocessor::GenerateBlueprintEventWrapper` / `FAngelscriptPreprocessor::ExtractArgumentList` / `FAngelscriptPreprocessor::ExtractReturnType` |
| 现有测试覆盖 | 当前 `Preprocessor/Compiler` 目标集只在 `MacroDetection` 里看到了 `UFUNCTION(BlueprintOverride)` 宏名，未覆盖 wrapper 生成；仓库其他 `Actor/Blueprint` 套件只在更高层间接覆盖事件调用行为 |
| 风险评估 | `GenerateBlueprintEventWrapper()` 一旦把参数列表、返回值引用、`_Implementation` 改名或 `__Evt_Execute` 调用生成错了，现有目标套件不会报红；高层行为测试即使存在，也很难把回归定位回预处理器改写阶段。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.BlueprintEventWrapperExecutesImplementation` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineBlueprintEventWrapperTests.cpp` |
| 场景描述 | 编译一个 annotated `UObject` 类，声明 `UFUNCTION(BlueprintEvent) int Compute(int Value) { return Value + 21; }` 和 `UFUNCTION() int Entry() { return Compute(21); }`，随后通过生成类实例执行 `Entry()`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 编译；随后 `FindGeneratedClass()`、`FindGeneratedFunction()` 定位 `Entry`，在 `NewObject<UObject>(GetTransientPackage(), GeneratedClass)` 上执行。 |
| 期望行为 | 编译成功且 diagnostics 为空；generated class 存在，`Entry` 和事件入口 `Compute` 都能在反射中找到；执行 `Entry()` 返回 `42`，证明预处理器生成的 wrapper 正确把参数 `21` 推入事件调用，并把 `Compute_Implementation` 的返回值传回脚本。若项目允许读取预处理结果，可再补一条断言：processed code 中出现 `__Evt_Execute(this, __STATIC_NAME(` 与 `Compute_Implementation`。 |
| 使用的 Helper | `CompileModuleWithSummary` + `FindGeneratedClass` + `FindGeneratedFunction` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

#### NewTest-33：global `UFUNCTION()` 必须生成 statics class 并保持可调用

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessFunctionMacro` / `FAngelscriptPreprocessor::GetOrCreateStaticsClass` / `FAngelscriptPreprocessor::MakeIdentifier` |
| 现有测试覆盖 | 当前 `Preprocessor/Compiler` 目标集完全没有覆盖 global `UFUNCTION()`；现有 8 个 compiler pipeline 用例只处理类成员 `UFUNCTION()` 或普通全局函数 |
| 风险评估 | 这条路径负责把全局 `UFUNCTION()` 收拢进自动生成的 statics class；如果 statics class 名称规范化、函数挂接或 `bIsStatic` 标记退化，脚本仍可能部分编译通过，但 Blueprint/反射层将找不到对应函数。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.GlobalUFunctionCreatesStaticsClass` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineGlobalUFunctionTests.cpp` |
| 场景描述 | 编译一个 annotated 模块，在全局作用域声明 `UFUNCTION(BlueprintCallable) int GetGlobalValue() { return 42; }`，随后定位自动生成的 statics class 和对应 `UFunction`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 编译；根据模块名规范化结果计算预期 statics class 名称，或直接从 module desc 的 `Classes` 中查找 `bIsStaticsClass` 对象。 |
| 期望行为 | 编译成功且 diagnostics 为空；模块 desc 中存在且仅存在一个 `bIsStaticsClass == true` 的类描述；该类暴露 `GetGlobalValue`，并且反射层能找到同名 `UFunction`；若项目允许执行 statics class 方法，则再补一次调用断言返回 `42`，证明“全局 `UFUNCTION` -> statics class -> 可执行函数”链路完整。 |
| 使用的 Helper | `CompileModuleWithSummary` + 直接读取 `FAngelscriptModuleDesc` / `FindGeneratedFunction`，必要时配合 `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-22 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 1, NoTestForSource: 1 |
 

| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 00:28)

### 一、现有测试问题

#### Issue-23：`Preprocessor` 三个用例拿到了 `Engine*` 却没有建立 `FAngelscriptEngineScope`，实际当前引擎并不受该指针约束

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.BasicParse` / `MacroDetection` / `ImportParsing` |
| 行号范围 | 74-219 |
| 问题描述 | 三个用例都先通过 `GetEngineForPreprocessorTests()` 取得一个 `FAngelscriptEngine*`，但全文件从未建立 `FAngelscriptEngineScope`。这让测试里拿到的 `Engine` 与预处理阶段真正查询的“当前引擎”脱节：`FAngelscriptPreprocessor::Preprocess()` 在 214 行读取 `FAngelscriptEngine::Get().ConfigSettings`，在 232 行调用 `ShouldUseAutomaticImportMethodForCurrentContext()`，而 `FileWideError/LineError/LineWarning` 又都写入 `FAngelscriptEngine::Get().ScriptCompileError(...)`。`FAngelscriptEngine::TryGetCurrentEngine()` 明确只看 `FAngelscriptEngineContextStack::Peek()` 或当前 `GameInstanceSubsystem` 附着的引擎；因此 `ImportParsing` 在 195 行对 `Engine->bUseAutomaticImportMethod` 做的 `TGuardValue`，并不能保证 `Preprocess()` 看到的就是这台引擎。 |
| 影响 | 这些用例即使显式修改了 `Engine` 配置，也可能没有真正控制预处理器使用的上下文；在无 production engine/无 subsystem 的测试环境里，`ShouldUseAutomaticImportMethodForCurrentContext()` 甚至会直接退成 `false`。结果是 manual import、配置读取和 diagnostics 路由都可能意外走到别的引擎或空上下文，测试绿色也不能证明目标引擎上的行为正确。 |
| 修复建议 | 把 helper 收敛成“取引擎 + 立 scope”的固定模式：每个用例进入主体后立即构造 `FAngelscriptEngineScope ScopedEngine(*Engine);`，再调用 `Preprocessor.Preprocess()`；或者直接改用已经内置 scope 语义的 `FAngelscriptTestFixture` / `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 驱动预处理。`ImportParsing` 的 `TGuardValue<bool>` 也必须放在 scope 之内，确保 `ShouldUseAutomaticImportMethodForCurrentContext()` 读取到的确实是被 guard 的那台引擎。 |

### 二、需要新增的测试

#### NewTest-34：结构性 directive 错误必须给出稳定行号并在失败后停止继续预处理

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` |
| 现有测试覆盖 | 当前目标集覆盖了条件编译的成功裁剪、未知条件短路和 `#restrict usage`，但没有任何用例触发 `Invalid #elif` / `Invalid #else` / `Invalid #endif` / `missing #endif` 这四条结构性错误分支 |
| 风险评估 | directive 结构错误是预处理器最基础的错误恢复路径；如果这几条分支的诊断文本、行号或停止时机回归，用户会在明显写错脚本时得到误导性报错，现有套件不会给任何信号。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Directives.StructuralErrorsReportStableDiagnostics` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveErrorTests.cpp` |
| 场景描述 | 用一个 table-driven automation test 跑四个子场景：孤立 `#elif EDITOR`、孤立 `#else`、孤立 `#endif`、以及缺少闭合 `#endif` 的 `#if EDITOR`。每个子场景都构造最小脚本文本并直接执行预处理。 |
| 输入/前置 | 使用 `AcquireCleanSharedCloneEngine()` + `FAngelscriptEngineScope`；通过 fixture 为每个子场景写入单文件脚本；每次执行前清空 `Engine.Diagnostics` / `LastEmittedDiagnostics`。 |
| 期望行为 | 四个子场景都应 `Preprocess()` 返回 `false`；diagnostics 恰好包含对应错误文本之一：`Invalid #elif, no matching #if found.` / `Invalid #else, no matching #if found.` / `Invalid #endif, no matching #if found.` / `Preceding preprocessor #if/#ifdef/#else was not closed, missing #endif.`；`Diagnostic.Row` 精确等于出错 directive 所在行；`GetModulesToCompile()` 不得产生可编译的有效代码段。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-35：`UFUNCTION` / `UPROPERTY` 置于未声明 flag 的条件编译块中时必须在预处理阶段拒绝

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` |
| 现有测试覆盖 | 当前目标集只验证了合法条件里的普通代码裁剪，没有任何用例触发 3618-3633 行对 `UFUNCTION` / `UPROPERTY` 条件编译位置的限制 |
| 风险评估 | 这条规则直接决定 annotated 成员能否出现在条件编译块里；如果回归成静默放行，后续类生成会在更晚阶段以更难懂的方式失败，或者产生与配置不一致的反射结果。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.FunctionMacros.RejectUnsupportedConditionalPlacement` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorFunctionMacroErrorTests.cpp` |
| 场景描述 | 编写一个 annotated `UObject` 类，在 `#if UNKNOWN_FLAG` 块内分别声明一个 `UFUNCTION()` 和一个 `UPROPERTY()`；再用第二个子场景对照 `#if EDITOR` 下的同类声明，证明规则不是“所有条件编译都禁止”。 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；不要向 `PreprocessorFlags` 注入 `UNKNOWN_FLAG`；fixture 脚本包含最小 `UCLASS()` 载体类，避免其他语法因素干扰结果。 |
| 期望行为 | `UNKNOWN_FLAG` 子场景的 `Preprocess()` 必须返回 `false`，diagnostics 精确包含 `Cannot put a UPROPERTY or UFUNCTION inside preprocessor conditions other than EDITOR or flags declared in configuration.`，并定位到宏声明行；`EDITOR` 对照子场景应 `Preprocess()` 成功且 `Macro.bEditorOnly == true`，证明测试同时锁住“非法条件报错”和“合法 editor-only 条件放行”两侧合同。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接读取 `FAngelscriptPreprocessor::Files[*].ChunkedCode` |
| 优先级 | P1 |

#### NewTest-36：非法 `UFUNCTION` specifier 组合必须在预处理阶段产出精确 macro 诊断

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessFunctionMacro` |
| 现有测试覆盖 | 当前目标集只覆盖合法 `UFUNCTION(BlueprintOverride)` 宏被识别，以及合法 `BlueprintEvent` wrapper 的正向生成；1500-1719 行大量非法 specifier 组合完全没有回归保护 |
| 风险评估 | 一旦这些 macro 诊断回归成静默放行、错误文本漂移或行号错位，用户会在更后面的类生成/编译阶段才看到二次故障，问题定位成本会明显上升。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.FunctionMacros.InvalidSpecifiersReportDiagnostics` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorFunctionMacroErrorTests.cpp` |
| 场景描述 | 使用 table-driven 子场景覆盖至少三种脚本：1. 全局 `UFUNCTION(BlueprintEvent) int BadGlobalEvent()`；2. 类成员 `UFUNCTION(BlueprintEvent, BlueprintOverride) int Conflict()`；3. 类成员 `UFUNCTION(DefinitelyUnknownSpecifier) void Unknown()`。 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；每个子场景都构造最小可解析脚本，只保留一个触发目标错误的函数声明；执行前清空 diagnostics。 |
| 期望行为 | 三个子场景都应 `Preprocess()` 返回 `false`；diagnostics 分别精确包含 `Global UFUNCTION() BadGlobalEvent may not be marked BlueprintEvent.`、`UFUNCTION() Conflict cannot be both BlueprintEvent and BlueprintOverride.`、`Unknown function specifier DefinitelyUnknownSpecifier on method UBadCarrier::Unknown.`；`Diagnostic.Row` 必须等于对应 `UFUNCTION` 所在行，且 `GetModulesToCompile()` 不应留下可继续编译的合法函数包装结果。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-23 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingErrorPath: 3 |

---

## 测试审查 (2026-04-09 00:38)

### 一、现有测试问题

#### Issue-24：`MacroDetection` 只覆盖 `Property/Function` 两类宏，`Class/Enum/EnumValue/EnumMeta` 四条预处理路径完全未被直接审查

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.MacroDetection` |
| 行号范围 | 128-161 |
| 问题描述 | `FAngelscriptPreprocessor::EMacroType` 在 `AngelscriptPreprocessor.h:32-41` 明确定义了 `Property`、`Function`、`Class`、`Enum`、`EnumValue`、`EnumMeta` 六类宏，但当前 `MacroDetection` fixture 只声明了 `UPROPERTY(EditAnywhere, BlueprintReadWrite)` 和 `UFUNCTION(BlueprintOverride)`，随后也只按名字查 `Mesh`/`BeginPlay` 两项。也就是说，`UCLASS/UENUM/UMETA` 与 enum value 注释元数据这四条宏采集路径，当前 direct preprocessor 套件完全没有任何断言。 |
| 影响 | 只要 property/function 宏还在工作，`DetectClasses`、`DetectEnum`、`ProcessClassMacro` 对 class/enum 级宏的解析回归就不会被这个“MacroDetection” 用例发现；即便 compiler 侧后续还能勉强生成类型，也无法证明预处理阶段的 `Type`、`Arguments`、`SubjectIndex`、`Comment` 等基础记录仍然正确。 |
| 修复建议 | 保留当前用例专门检查 property/function 结构，同时新增一个 direct preprocessor 宏形状用例，显式覆盖 `UCLASS(Abstract, BlueprintType)`、`UENUM(BlueprintType)`、`UMETA(DisplayName=\"...\")` 和 enum value 注释，逐项断言 `FMacro.Type`、`Arguments`、`Name`、`SubjectIndex`、`FileLineNumber` 与 chunk 归属。这样可以把“宏被采集”与“后续编译还能跑通”拆开验证，避免继续把四类未审查宏路径留给更高层 compile smoke 间接碰运气。 |

### 二、需要新增的测试

#### NewTest-37：direct preprocessor 套件需要单独锁住 `Class/Enum/EnumValue/EnumMeta` 宏形状

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::DetectEnum` / `FAngelscriptPreprocessor::ProcessClassMacro` |
| 现有测试覆盖 | `MacroDetection` 只覆盖 `Property` 和 `Function`；当前目标集没有任何 direct preprocessor 用例读取 `EMacroType::Class`、`Enum`、`EnumValue`、`EnumMeta` |
| 风险评估 | class/enum 宏解析一旦回归，compiler 层可能只留下模糊的后续故障，无法证明预处理阶段到底是“没采到宏”还是“采到了但后续处理坏了”；这会让 `UCLASS/UENUM/UMETA` 边界缺陷长期缺少定位精度。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Macros.ClassEnumMetaShapes` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorMacroShapeTests.cpp` |
| 场景描述 | 构造一个最小 annotated 脚本，同时包含 `UCLASS(Abstract, BlueprintType)` 的 `UObject` 类、`UENUM(BlueprintType)` 枚举、一个带注释的 enum value，以及一个 `UMETA(DisplayName=\"Beta Friendly\")`。直接运行 `FAngelscriptPreprocessor` 并从 `ChunkedCode` 收集宏记录。 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 fixture 写入单文件脚本；脚本中让 enum 的第一个值前带注释，第二个值前带 `UMETA`，避免 property/function 宏干扰统计。 |
| 期望行为 | `Preprocess()` 成功且 diagnostics 为空；收集到的宏集合至少精确包含 4 条目标记录：1. `Type == Class`、`Name == UMacroCarrier`、`Arguments == \"Abstract, BlueprintType\"`；2. `Type == Enum`、`Name == EMacroState`、`Arguments == \"BlueprintType\"`；3. `Type == EnumValue`、`SubjectIndex == 0` 且 `Comment` 包含第一个 enum value 的注释；4. `Type == EnumMeta`、`SubjectIndex == 1` 且 `Arguments` 包含 `DisplayName=\"Beta Friendly\"`。四条记录都必须带稳定 `FileLineNumber`，并且 class/enum 宏落在各自对应 chunk。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-38：成功重编译必须替换旧模块字节码和旧 generated class，而不是继续命中陈旧结果

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `CompileModuleWithSummary` / `FAngelscriptPreprocessor::Preprocess` / `FAngelscriptEngine::CompileModules` |
| 现有测试覆盖 | 当前目标集只有“首次成功编译”和“失败后恢复”建议，没有任何用例锁住“同模块名、同文件名再次成功编译后应替换旧结果”的正向更新路径 |
| 风险评估 | 如果二次编译后仍执行旧 bytecode、保留旧 generated class，或在 `ActiveModules` 里残留重复模块，当前 8 个 compiler pipeline 用例都会继续全绿；这会把最常见的编辑循环问题留到人工联调阶段才暴露。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.SuccessfulRecompileReplacesStaleOutputs` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineRecompileTests.cpp` |
| 场景描述 | 在同一 clean engine 上，先用模块名/文件名固定的 annotated 脚本 `V1` 编译 `URecompileCarrier`，其中全局 `Entry()` 与实例方法 `GetValue()` 都返回 `7`；随后以完全相同的模块名/文件名提交 `V2`，把两处返回值都改成 `42`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；两次都走 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)`；每次编译后都调用 `FindGeneratedClass()`、`FindGeneratedFunction()`，并在新建对象上通过 `ExecuteGeneratedIntEventOnGameThread()` 执行 `GetValue()`。 |
| 期望行为 | 两次编译都 `bCompileSucceeded == true`、`CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；第一次 `ExecuteIntFunction()` 与 `GetValue()` 都返回 `7`，第二次都返回 `42`；第二次编译后 `Engine.GetActiveModules()` 中目标模块名只出现一次，证明没有留下旧模块副本；如果保留第一次拿到的 `UFunction*`/`UClass*` 指针做对比，则第二次查找到的对象必须是最新可执行版本，而不是继续命中旧结果。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `ExecuteIntFunction` + `FindGeneratedClass` + `FindGeneratedFunction` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

#### NewTest-39：`BlueprintEvent` wrapper 需要同时锁住 specialized 与 generic push-argument 路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::GenerateBlueprintEventWrapper` / `FAngelscriptPreprocessor::GetPushArgumentSuffix` / `FAngelscriptPreprocessor::ExtractArgumentList` |
| 现有测试覆盖 | `NewTest-32` 只覆盖简单 `int` 参数的 wrapper 执行；当前目标集没有任何场景同时命中 `const FString&` 的 specialized push 和 `TSubclassOf<AActor>` 这类模板参数的 generic push |
| 风险评估 | 如果 `GetPushArgumentSuffix()` 或 wrapper 生成逻辑把 specialized/generic 路径选错，`BlueprintEvent` 仍可能在最简单的 POD 参数上通过，但一到字符串、模板类参数就会在运行期静默错参或返回错误值，当前套件无法第一时间定位到 wrapper 生成层。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.BlueprintEventWrapperUsesMixedPushPaths` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineBlueprintEventWrapperTests.cpp` |
| 场景描述 | 编译一个 annotated `UObject` 类，声明 `UFUNCTION(BlueprintEvent) int EvaluateMixedPush(const FString& Label, TSubclassOf<AActor> TypeValue) { return TypeValue == AActor::StaticClass() ? 42 : 0; }`，再提供 `UFUNCTION() int Entry() { return EvaluateMixedPush(\"Alpha\", AActor::StaticClass()); }`。在编译前先直接跑一次 `FAngelscriptPreprocessor`，检查生成代码里的 push 调用。 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；先用 `FAngelscriptPreprocessor` 读取同一份脚本并拿到 `Module.Code[0].Code`；随后再用 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 编译，并在生成类对象上执行 `Entry()`。 |
| 期望行为 | 预处理成功且 diagnostics 为空；processed code 明确包含 `__Evt_PushArgumentRef__FString(`，同时对 `TSubclassOf<AActor>` 参数生成 generic `__Evt_PushArgument(` 而不是错误的 specialized suffix；随后编译成功，`CompileResult == FullyHandled`，执行 `Entry()` 返回 `42`，证明 mixed push 路径既生成正确也能真正把模板类参数传到事件实现体。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` + `CompileModuleWithSummary` + `FindGeneratedClass` + `FindGeneratedFunction` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-24 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 3 |

---

## 测试审查 (2026-04-09 00:51)

### 一、现有测试问题

#### Issue-25：两个 `UCLASS(..., BlueprintType)` smoke 用例都没有验证 `BlueprintType` specifier 的实际效果

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateEnumClassCompile` / `Angelscript.TestModule.Compiler.EndToEnd.GeneratedClassConsistency` |
| 行号范围 | 26-78，239-266 |
| 问题描述 | 两个用例都把脚本类声明成 `UCLASS(Abstract, BlueprintType)`，但断言只检查了 `CLASS_Abstract`、属性存在和函数存在。对应源码里，`ProcessClassMacro()` 会在 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:2342-2345` 把 `BlueprintType` 写入 `ClassDesc->Meta`，而 `AngelscriptClassGenerator` 又会在 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3316-3319` 把这条语义继续写到生成 `UClass` 的 metadata。当前两个 smoke 都没有读取 `GeneratedClass->GetBoolMetaData(TEXT("BlueprintType"))`，也没有在预处理阶段检查 `ClassDesc->Meta`，所以显式 `BlueprintType` specifier 是否被识别和传递完全处于盲区。 |
| 影响 | 如果 class specifier 解析退化成“脚本仍能编译，但显式 `BlueprintType` 声明被忽略或漂移”，当前 compiler suite 仍可能全绿；Blueprint 变量可见性和编辑器类型暴露问题会延迟到人工联调才暴露。 |
| 修复建议 | 至少在现有 smoke 中补一条 `GeneratedClass->GetBoolMetaData(TEXT("BlueprintType"))` 断言，明确 generated class 的 metadata 仍为 `true`。更稳妥的做法是补一个 direct preprocessor + compile 组合用例：先断言 `Module->Classes[*]->Meta` 中只有显式 `UCLASS(BlueprintType)` 的类带 `BlueprintType=true`，再断言对应 generated `UClass` 也保留该 metadata，把 specifier 解析与下游传播一起锁住。 |

### 二、需要新增的测试

#### NewTest-40：namespaced annotated class 必须生成可用的 namespace-scoped `StaticClass()` helper

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::DetectClasses` / `FAngelscriptPreprocessor::AnalyzeClasses` |
| 现有测试覆盖 | 当前目标集没有任何 namespace fixture；仓库里的 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` 只覆盖普通脚本 namespace 解析，不经过 annotated class + generated helper 这条预处理链 |
| 风险评估 | 普通 `namespace Foo { ... }` 脚本即使继续工作，也不能说明 namespaced `UCLASS` 的 `ClassDesc->Namespace`、生成的 `__StaticType_*` 全局变量和嵌套 `StaticClass()` wrapper 仍然正确；一旦这里回归，`Gameplay::UNamespaceCarrier::StaticClass()` 这类写法会在无现成目标测试保护的情况下失效。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.NamespacedAnnotatedClassStaticHelperRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineNamespaceTests.cpp` |
| 场景描述 | 构造 `namespace Gameplay { UCLASS() class UNamespaceCarrier : UObject { UFUNCTION() int GetValue() { return 42; } } } int Entry() { return Gameplay::UNamespaceCarrier::StaticClass() != nullptr ? 42 : 0; }`。先对同一脚本直接跑 `FAngelscriptPreprocessor` 检查生成代码，再走完整编译并执行 `Entry()`。 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过唯一 fixture 文件保存脚本；第一阶段直接 `AddFile()` + `Preprocess()` 读取 module/code section，第二阶段使用 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 编译同一脚本。 |
| 期望行为 | 预处理成功且 diagnostics 为空；processed/generated code 明确包含 `namespace Gameplay`、`__StaticType_UNamespaceCarrier`，以及 `namespace UNamespaceCarrier { UClass StaticClass() ... }` 的 helper 片段；随后编译成功，`CompileResult == FullyHandled`、`FindGeneratedClass(TEXT("UNamespaceCarrier"))` 非空，`ExecuteIntFunction()` 返回 `42`，证明 namespace 记录和 helper 生成一路跑到了可执行字节码。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` + `CompileModuleWithSummary` + `ExecuteIntFunction` + `FindGeneratedClass` |
| 优先级 | P1 |

#### NewTest-41：`UCLASS(BlueprintType)` 必须在 preprocessor class desc 上留下可区分的 metadata

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessClassMacro` |
| 现有测试覆盖 | 当前目标集虽然多次把类声明成 `UCLASS(..., BlueprintType)`，但只检查 compile smoke；`NewTest-37` 计划覆盖 macro 记录形状，仍不会验证 `ClassDesc->Meta` 是否真的因为该 specifier 被写成 `BlueprintType=true` |
| 风险评估 | 如果 `ProcessClassMacro()` 开始忽略 `BlueprintType`、把它误当未知 specifier，或错误地把 metadata 写到所有类上，现有目标测试都不会直接暴露；后续即使 generated `UClass` 仍因默认行为带上 metadata，也会掩盖 preprocessor specifier 解析已经退化的事实。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.ClassSpecifiers.BlueprintTypeMetadataPropagation` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorClassSpecifierTests.cpp` |
| 场景描述 | 在同一脚本里声明两个最小类：`UCLASS(BlueprintType) class UBlueprintCarrier : UObject {}` 和 `UCLASS() class UPlainCarrier : UObject {}`。直接运行预处理并读取 `Module->Classes` / `Chunk.ClassDesc`。 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 fixture 写入单文件脚本；执行 `AddFile()` + `Preprocess()` 后，从 `GetModulesToCompile()` 返回的 module desc 中按类名定位两个 `FAngelscriptClassDesc`。 |
| 期望行为 | `Preprocess()` 成功且 diagnostics 为空；`UBlueprintCarrier` 的 `Meta` 精确包含 `BlueprintType=true`；`UPlainCarrier` 的 `Meta` 不得含有同名键；同时两个类都应保持正确的 `ClassName` 与 `SuperClass == "UObject"`，避免把失败归因到别的 class 解析问题。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-25 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 01:09)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-45：未显式标注 Blueprint 访问级别的 `UPROPERTY()` 必须遵守 `DefaultPropertyBlueprintSpecifier`

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessPropertyMacro` |
| 现有测试覆盖 | 当前目标集只验证显式 `UPROPERTY(EditAnywhere, BlueprintReadWrite)` 能被识别；仓库内 examples 也主要覆盖显式 `BlueprintReadOnly/BlueprintReadWrite`，没有任何自动化改写 `DefaultPropertyBlueprintSpecifier` 后读取 `FAngelscriptPropertyDesc::bBlueprintReadable/bBlueprintWritable` |
| 风险评估 | 如果默认蓝图访问分支退化成总是 `BlueprintReadWrite`、总是 hidden，或忽略 settings 值，脚本属性在 Blueprint 中的可见性会静默漂移，而当前 preprocessor/compiler 套件不会报红 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Properties.DefaultBlueprintAccessUsesSettings` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorPropertyDefaultSpecifierTests.cpp` |
| 场景描述 | 临时把 `DefaultPropertyBlueprintSpecifier` 设为 `BlueprintReadOnly`，脚本里声明 `UCLASS() class UBlueprintAccessCarrier : UObject { UPROPERTY() int ImplicitAccess; UPROPERTY(BlueprintReadWrite) int ExplicitAccess; }`，直接运行预处理并读取两个属性描述 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `GetMutableDefault<UAngelscriptSettings>()` 保存并恢复 setting；用 `FAngelscriptTestFixture` 写入单文件脚本；执行前清空 diagnostics |
| 期望行为 | `Preprocess()` 成功且 diagnostics 为空；`ImplicitAccess` 的属性描述必须满足 `bBlueprintReadable == true`、`bBlueprintWritable == false`；`ExplicitAccess` 必须保持 `bBlueprintReadable == true`、`bBlueprintWritable == true`，证明默认 setting 与显式 specifier 两侧合同都被锁住 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + `GetMutableDefault<UAngelscriptSettings>()` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

---

## 测试审查 (2026-04-09 01:12) 续

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 4 | MissingErrorPath: 3, MissingScenario: 1 |
| P2 | 2 | MissingErrorPath: 1, NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 01:31) 续

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | NoTestForSource: 1, MissingErrorPath: 2 |
| P2 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 11:08)

### 一、现有测试问题

#### Issue-27：`GeneratedClassConsistency` 只编译一次，根本没有验证任何“一致性”

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.GeneratedClassConsistency` |
| 行号范围 | 224-269 |
| 问题描述 | 用例名声明要验证“GeneratedClassConsistency”，但实现只做了一次 `CompileAnnotatedModuleFromMemory()`，随后断言类存在、`CLASS_Abstract` 仍在、`Score` 属性和 `GetScore` 函数存在。测试里既没有第二次编译同模块，也没有比较 reload 前后的 class/function/property 身份、flags 或执行结果，因此它实际上只是单次 smoke test。 |
| 影响 | 一旦生成类在二次编译、同名替换或 reload 后出现不一致，这个用例仍会保持全绿，给人一种“已经覆盖一致性”的错误信号。当前命名会误导后续审查者高估这条回归保护。 |
| 修复建议 | 二选一处理：1）把用例重命名成更准确的 `GeneratedClassSmoke`，承认它只做单次反射冒烟；2）更推荐把它改成真正的一致性测试，在 clean engine 中连续编译同一模块两次，第二次修改实现或 specifier，然后断言旧类不再被 `FindGeneratedClass()` 命中、`GetMostUpToDateClass()` 指向新类、关键属性/函数仍可查找，并补一条可执行断言证明 reload 后使用的是新字节码。 |

#### Issue-28：preprocessor fixture 使用固定磁盘路径，重跑或并发执行时会互相覆盖

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.BasicParse` / `Angelscript.TestModule.Preprocessor.MacroDetection` / `Angelscript.TestModule.Preprocessor.ImportParsing` |
| 行号范围 | 23-33，82-89，128-139，177-192 |
| 问题描述 | 三个用例都通过 `WriteFixtureFile()` 把脚本写到 `Saved/Automation/PreprocessorFixtures/Tests/Preprocessor/*.as` 下的固定文件名，例如 `BasicModule.as`、`MacroActor.as`、`Shared.as`、`UsesImport.as`。这些文件名在不同运行之间完全复用，且没有任何 run id、GUID 或测试专属子目录隔离。 |
| 影响 | 一旦自动化重试、并行 worker、或本地两次运行交叠，同名 fixture 会被彼此覆盖，导致一个测试读取到另一轮的脚本内容。结果可能表现为偶发假红、假绿，或者把与当前提交无关的旧脚本当成当前输入。 |
| 修复建议 | 给 `GetPreprocessorFixtureRoot()` 增加每轮唯一子目录，或在 `WriteFixtureFile()` 中把文件名改成 `<TestName>_<Guid>.as`；同时让 `AddFile()` 使用对应的唯一相对路径，保证并发运行互不影响。若项目已有 `FAngelscriptTestFixture` 风格 helper，可把 fixture 创建/销毁统一收口到 shared helper。 |

#### Issue-29：`ImportParsing` 直接访问 `Code[0]`，回归时更可能崩溃而不是给出可诊断失败

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.ImportParsing` |
| 行号范围 | 204-219 |
| 问题描述 | 用例在确认 `ImportingModule` 非空后，直接读取 `ImportingModule->Code[0].Code` 去断言 `import` 语句已移除，但整个测试没有先验证 `Code.Num() > 0`，也没有检查 code section 的 `RelativeFilename/AbsoluteFilename` 是否成立。若预处理回归把 importing module 变成零 code section，此处会直接发生越界访问。 |
| 影响 | 这会把本应表现为清晰断言失败的回归，升级成测试崩溃或未定义行为，既降低定位效率，也让 CI 很难从日志里看出真正破坏的是“importing module 没有产出代码段”而不是测试框架本身。 |
| 修复建议 | 在访问 `Code[0]` 之前先断言 `ImportingModule->Code.Num() == 1`，再检查 `RelativeFilename`、`AbsoluteFilename` 与 importing fixture 一致，最后再验证 `Code[0].Code` 中 `import` 被删除。若希望更稳妥，可先把 `Code[0]` 绑定为局部引用并复用它做后续多条精确断言。 |

### 二、需要新增的测试

#### NewTest-57：`Preprocess()` 与 `AddFile()` 的 single-use 合同必须被锁住

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::AddFile` / `FAngelscriptPreprocessor::Preprocess` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 当前 public API 明确通过 `ensureMsgf` 约束“预处理器实例只能 preprocess 一次，且 preprocess 后不能再追加文件”。若没有测试，这个合同未来可能退化成静默重复执行、重复 module 产出，或在 late add 时悄悄修改已生成结果。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Api.PreprocessIsSingleUse` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorApiContractTests.cpp` |
| 场景描述 | 先创建一个最小 fixture 并成功调用一次 `Preprocess()`；随后尝试对同一个 `FAngelscriptPreprocessor` 再调用 `AddFile()` 追加第二个脚本，并再次执行 `Preprocess()`。 |
| 输入/前置 | 使用 `FAngelscriptTestFixture` 或 clean engine；写入 `First.as` 和 `Second.as` 两个最小脚本；如测试框架允许，使用 `AddExpectedErrorPlain` 匹配 `Cannot add files after preprocessing is done.` 与 `Can only preprocess once.` 的 ensure 文本。 |
| 期望行为 | 第一次 `Preprocess()` 返回 `true`，`GetModulesToCompile()` 只包含 `First` 模块；晚于 preprocess 的 `AddFile()` 不应让 `Second` 出现在模块列表中；第二次 `Preprocess()` 返回 `false`；模块数量、模块名和首轮产出的 code section 内容保持不变。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-58：异步零字节文件分支必须与同步读取路径产出一致

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::AddFile` / `FAngelscriptPreprocessor::PerformAsynchronousLoads` / `FAngelscriptPreprocessor::Preprocess` |
| 现有测试覆盖 | 有异步 happy path 建议，但零字节文件分支完全无测试 |
| 风险评估 | `PerformAsynchronousLoads()` 对 `Request->GetSizeResults() <= 0` 有单独分支，会把 `RawCode` 置空并结束异步状态。若这条边界没有回归保护，异步读取空文件时可能出现卡死、脏状态，或与同步空文件路径产出不一致。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Api.AsyncZeroByteFileMatchesSyncPath` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorAsyncTests.cpp` |
| 场景描述 | 创建一个真实的零字节 `.as` 文件，分别用同步 `AddFile(..., false)` 和异步 `AddFile(..., true)` 送入两个独立的 `FAngelscriptPreprocessor`，比较两条路径的最终模块描述。 |
| 输入/前置 | 使用 clean engine；在 `Saved/Automation` 下创建空文件 `ZeroByte.as`；两个 preprocessor 均使用相同的 `RelativeFilename`/`AbsoluteFilename`；执行前清空 diagnostics。 |
| 期望行为 | 两次 `Preprocess()` 都应完成且返回值一致；`GetModulesToCompile()` 的模块数量、模块名、`Code.Num()`、`ImportedModules` 和 code hash 完全一致；diagnostics 不应因为异步空文件分支而多出额外错误；测试结束后删除零字节 fixture。 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-27 |
| FlakyRisk | 1 | Issue-28 |
| AntiPattern | 1 | Issue-29 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 1, MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 11:31)

### 一、现有测试问题

#### Issue-30：`ModuleFunctionInspection` 用了绕开 preprocessor 的 helper，不能代表完整编译管线

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.ModuleFunctionInspection` |
| 行号范围 | 283-317 |
| 问题描述 | 用例通过 `AngelscriptTestSupport::BuildModule(...)` 构建模块，再检查 `GetFunctionByDecl()`、`GetParamCount()` 和 `Entry()` 运行结果。但 `BuildModule()` 在 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp:137-163` 里直接走 `MakeModuleDesc()` + `Engine->CompileModules()`，只把原始脚本文本塞进 `FAngelscriptModuleDesc::Code`，完全不会调用 `FAngelscriptPreprocessor::AddFile()` / `Preprocess()`。这意味着该用例虽然名字在 `CompilerPipeline` 套件里，看起来像“源码到模块再到字节码”的 inspection，实际却绕开了 preprocessor、module desc 生成和文件名规范化这整段链路。 |
| 影响 | 当前 8 个 compiler pipeline 用例里，真正执行到字节码的只剩 `FunctionDefaultsAndClassLikeCompile` 还经过 annotated preprocessor 路径；`ModuleFunctionInspection` 即使全绿，也只能说明 raw `CompileModules()` 能执行一段无注解脚本，不能证明 plain source 经 preprocessor 后仍能被完整编译和运行。这样会高估“从源码到可执行字节码”这条端到端覆盖的实际强度。 |
| 修复建议 | 把该用例改成显式的 preprocessor round-trip：优先使用 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 或直接 `FAngelscriptPreprocessor.AddFile() + Preprocess() + Engine->CompileModules()`，随后再做 `GetFunctionByDecl()`、默认参数元数据和 `Entry()` 执行断言。若确实需要保留 raw compile helper，应把它重命名到更准确的 raw compiler/engine smoke 文件中，避免继续作为完整 pipeline 覆盖被统计。 |

### 二、需要新增的测试

#### NewTest-59：普通无注解脚本也必须覆盖 preprocessor 到字节码的完整 round-trip

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::Preprocess` / `FAngelscriptEngine::CompileModules` |
| 现有测试覆盖 | `BasicParse` 只停在 `GetModulesToCompile()`；`ModuleFunctionInspection` 会执行字节码，但使用 `BuildModule()` 绕开了 preprocessor 和文件名到模块名的转换 |
| 风险评估 | 只要 raw `CompileModules()` 还能跑通，当前目标集就可能误判“plain source 全流程正常”；一旦 plain script 经 preprocessor 后出现 code section、模块名规范化或 code hash 相关回归，现有 11 个目标用例都不会准确报红 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.PlainSourcePreprocessorRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineExecutionTests.cpp` |
| 场景描述 | 构造一个没有任何 `UCLASS/USTRUCT/UENUM` 的最小脚本，例如 `int Add(int A = 20, int B = 22) { return A + B; } int Entry() { return Add(); }`，强制它走 `bUsePreprocessor=true` 的编译 helper，然后执行 `Entry()` |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(&Engine, ECompileType::SoftReloadOnly, ..., TEXT(\"Tests/Compiler/PlainSourceRoundTrip.as\"), Script, true, Summary, false)` 编译；执行前清空 diagnostics，并记录 `Summary.ModuleNames` / `Summary.AbsoluteFilenames` |
| 期望行为 | `bCompileSucceeded == true`、`CompileResult == FullyHandled`、`bUsedPreprocessor == true`、`ModuleDescCount == 1`、`Diagnostics.Num() == 0`；`Summary.ModuleNames[0]` 必须等于 `Tests.Compiler.PlainSourceRoundTrip`；随后 `ExecuteIntFunction()` 返回 `42`，证明普通脚本也确实走完了 preprocessor、module desc 生成和最终字节码执行三段链路 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-60：带逗号和转义引号的 specifier 字符串必须完整保留到反射 metadata

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `ParseSpecifier` / `ParseSpecifiers` / `FAngelscriptPreprocessor::ProcessClassMacro` / `FAngelscriptPreprocessor::ProcessPropertyMacro` / `FAngelscriptPreprocessor::ProcessFunctionMacro` |
| 现有测试覆盖 | 当前目标集只覆盖 `Abstract`、`BlueprintType`、`EditAnywhere`、`BlueprintReadWrite` 这类简单 specifier；没有任何用例让 metadata 字符串同时包含逗号或 `\\\"` 转义引号 |
| 风险评估 | 这条 helper 链负责把 macro 参数拆成 metadata/value；如果带逗号或转义引号的字符串被错误切分，脚本通常仍能继续编译，但 `DisplayName`、`ToolTip`、`Keywords` 等元数据会静默丢失、截断或串位，现有目标套件无法定位到 specifier parser 已经退化 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.SpecifierStringMetadataRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineSpecifierMetadataTests.cpp` |
| 场景描述 | 编译一个最小 annotated 类：`UCLASS(meta=(DisplayName=\"Alpha, Beta\", ToolTip=\"He said \\\"Hi\\\"\")) class USpecifierCarrier : UObject { UPROPERTY(meta=(DisplayName=\"Count, Total\", ToolTip=\"Quoted \\\"Value\\\"\")) int Count; UFUNCTION(meta=(DisplayName=\"Call, Verify\", ToolTip=\"Escaped \\\"quote\\\"\")) int Compute() { return 7; } } int Entry() { return 7; }`，随后读取生成类、属性和函数 metadata |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)` 编译；编译后用 `FindGeneratedClass()`、`FindFProperty<FProperty>()`、`FindGeneratedFunction()` 定位 `USpecifierCarrier`、`Count` 和 `Compute`；再执行全局 `Entry()` 确认脚本仍可运行 |
| 期望行为 | 编译成功且 `CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；`GeneratedClass->GetMetaData(TEXT(\"DisplayName\")) == \"Alpha, Beta\"`、`GeneratedClass->GetMetaData(TEXT(\"ToolTip\")) == \"He said \\\"Hi\\\"\"`；属性和函数上的 `DisplayName/ToolTip` 也必须逐项精确匹配原字符串，不允许被逗号拆开、被转义引号截断或丢掉后半段；最后 `ExecuteIntFunction()` 返回 `7`，证明 metadata 复杂字符串没有破坏整体编译执行路径 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `FindGeneratedClass` + `FindGeneratedFunction` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-30 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 1, NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 11:40)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-61：declared function import 必须覆盖从源码到绑定执行的完整链路

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::Preprocess` / `FAngelscriptEngine::CheckFunctionImportsForNewModules` / `FAngelscriptEngine::ResolveAllDeclaredImports` |
| 现有测试覆盖 | 当前目标集只有 `ImportParsing` 验证 module import 文本处理，`ModuleFunctionInspection` 只编译单模块 raw script；仓库里唯一触及 declared import 的是 `Internals/AngelscriptBuilderTests.cpp`，它直接手动 `BindImportedFunction()`，没有经过 preprocessor、多模块 `CompileModules()`、swap-in 与最终执行 |
| 风险评估 | 如果 declared import 在真实编译链路里没有被校验、没有在 swap-in 后重新绑定，脚本会表现为“单模块 smoke 都是绿的，但跨模块调用一运行就炸”；这正是 preprocessor/compiler 套件应该兜住的端到端合同 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DeclaredFunctionImportRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineImportTests.cpp` |
| 场景描述 | 构造两个真实脚本文件：provider 模块定义 `int SharedValue() { return 77; }`；consumer 模块声明 `import int SharedValue() from "Tests.Compiler.ImportSource"; int Entry() { return SharedValue(); }`。先走 preprocessor，再把两个 `FAngelscriptModuleDesc` 一次性交给 `Engine->CompileModules()`，最后执行 consumer 的 `Entry()` |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `FAngelscriptTestFixture` + `FAngelscriptEngineScope`；显式关闭 automatic import，确保走 `CheckFunctionImportsForNewModules()` / `ResolveAllDeclaredImports()` 这条合同；保留唯一 fixture 路径，编译前清空 diagnostics |
| 期望行为 | `Preprocess()` 成功且模块顺序为 provider 在前、consumer 在后；`CompileModules()` 返回成功且 diagnostics 为空；consumer 模块 `GetImportedFunctionCount() == 1`，其 source module 等于 `Tests.Compiler.ImportSource`；最终 `ExecuteIntFunction()` 执行 `int Entry()` 返回 `77`，证明 declared import 已在真实管线中完成绑定而不是只停留在 builder 层 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` + `FAngelscriptEngine::CompileModules` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 11:41)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-62：declared function import 的缺失模块与签名不匹配必须给出稳定诊断

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::CheckFunctionImportsForNewModules` |
| 现有测试覆盖 | 当前目标集没有任何 multi-module compile 测试触发 declared import 校验；`Internals/AngelscriptBuilderTests.cpp` 只验证成功绑定，完全没有覆盖 `could not find module` 与 `could not find function with this signature` 这两条错误分支 |
| 风险评估 | 一旦 declared import 的错误信息退化成泛化编译失败、错误行号漂移，或错误模块仍被 swap-in，脚本作者会在跨模块调用失败时得到极差的定位体验，而当前 preprocessor/compiler 套件不会报红 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DeclaredFunctionImportErrorsReportPreciseDiagnostics` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineImportTests.cpp` |
| 场景描述 | 在同一测试文件里做两个子场景：1）consumer 声明 `import int SharedValue() from "Tests.Compiler.MissingSource";`，provider 模块不存在；2）provider 存在，但只提供 `int SharedValue(int Extra)`，故意与 consumer 的 `import int SharedValue() ...` 签名不匹配 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `AcquireFreshSharedCloneEngine()`；显式关闭 automatic import；每个子场景都通过 `FAngelscriptPreprocessor` 预处理多个脚本，再调用 `Engine->CompileModules()`；编译前清空 diagnostics，并记录 consumer 模块的 absolute filename |
| 期望行为 | 两个子场景都应编译失败且 consumer 不可执行；missing-module 场景的 diagnostics 必须包含 `could not find module Tests.Compiler.MissingSource to import from`；signature-mismatch 场景必须包含 `could not find function with this signature in module Tests.Compiler.ImportSource`；两条诊断都应挂在 consumer 模块上，且行号固定为导入声明对应的第 1 行或该 fixture 中实际 import 行 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` + `FAngelscriptEngine::CompileModules` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 11:43)

### 一、现有测试问题

#### Issue-31：`Compiler.EndToEnd` 全套 helper 都会强制关闭 automatic import，默认编译配置根本没被覆盖

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateEnumClassCompile` / `FunctionDefaultsAndClassLikeCompile` / `PropertyDefaultsCompile` / `GeneratedClassConsistency` / `ModuleFunctionInspection` / `EnumAvailability` / `DelegateSignatureConsistency` / `ClassLikeReflectionShape` |
| 行号范围 | 21-49，95-132，174-191，234-251，283-297，334-346，376-388，425-451 |
| 问题描述 | 这 8 个用例虽然名字都叫 `Compiler.EndToEnd`，但实际调用的 helper 会统一把 automatic import 关闭：`CompilePreparedModules()` 在 `AngelscriptTestEngineHelper.cpp:97-100`、`CompileModuleInternal()` 在 `152-155`、`PreprocessAndCompile()` 在 `218-220` 都执行 `TGuardValue<bool> AutomaticImportGuard(Engine->bUseAutomaticImportMethod, false);` 与 `FScopedAutomaticImportsOverride`。因此不管当前引擎默认配置是什么，这些测试跑的都是“强制 manual import”的特化路径，而不是产品默认编译配置。 |
| 影响 | 只要 manual-mode 编译还正常，`Compiler.EndToEnd` 全绿也不能说明默认 automatic import 配置下的源码到字节码流程正确。任何只在默认模式下出现的跨模块绑定、诊断、module dependency 或 reload 回归，都会被这一层 helper override 静默遮掉。 |
| 修复建议 | 把“验证默认编译配置”的用例与“验证 manual import 兼容路径”的用例拆开：前者不要再复用会硬改 `bUseAutomaticImportMethod` 的 helper，改用直接 `FAngelscriptPreprocessor + Engine->CompileModules()`，或给现有 helper 增加显式参数以保留当前 import mode；后者再继续保留 manual-mode guard，并把测试名改得更准确，避免把非默认配置的 smoke 统计成完整 end-to-end 覆盖。 |

### 二、需要新增的测试

本轮未新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无 | 0 | 无 |

## 测试审查 (2026-04-09 11:44)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-63：provider reload 后必须重新绑定所有 active declared imports

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::ResolveAllDeclaredImports` / `FAngelscriptEngine::ResolveDeclaredImports` |
| 现有测试覆盖 | `NewTest-61` 只建议覆盖首次编译的成功绑定；当前目标集和仓库现有测试都没有任何场景验证“只 reload provider，不重编 consumer”时 active consumer 模块会不会被重新绑定到新函数 |
| 风险评估 | 如果 provider 更新后 consumer 仍然绑定旧函数句柄，跨模块调用会长期执行陈旧 bytecode，或者在旧模块被丢弃后变成 stale import。这个问题只会在真实 reload 编辑循环里暴露，单次 compile smoke 捕不到 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DeclaredFunctionImportRebindsAfterProviderReload` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineImportTests.cpp` |
| 场景描述 | 第一步编译 provider v1：`int SharedValue() { return 1; }` 和 consumer：`import int SharedValue() from "Tests.Compiler.ImportSource"; int Entry() { return SharedValue(); }`，执行 consumer 返回 `1`；第二步只重编同名 provider v2：`int SharedValue() { return 2; }`，不重编 consumer，再次执行 consumer 的 `Entry()` |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `AcquireFreshSharedCloneEngine()`；显式关闭 automatic import；两轮都通过 `FAngelscriptPreprocessor` + `Engine->CompileModules()` 走真实多模块编译链路；两轮编译前后都清空并检查 diagnostics |
| 期望行为 | 首轮编译成功且 consumer `Entry()` 返回 `1`；第二轮只编 provider 也必须成功，且无需重编 consumer 就能让同一个 `Entry()` 返回 `2`；整个过程 diagnostics 为空，证明 `ResolveAllDeclaredImports()` 已把 active consumer 模块从旧 provider 函数重绑到新 provider 函数 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` + `FAngelscriptEngine::CompileModules` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 11:52)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-64：`n"..."` name literal 必须锁住 `GenerateStaticName` 的 rewrite 与运行时一致性

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::GenerateStaticName` / `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::ProcessReplacements` |
| 现有测试覆盖 | `PropertyDefaultsCompile` 虽然脚本里写了 `default Tags.Add(n"Alpha");`，但现有断言只检查 `Score == 21`，没有验证 `Tags`，也没有任何 direct preprocessor/compile round-trip 去检查 `n"..."` 是否真的被改写成 `__STATIC_NAME(...)` |
| 风险评估 | `n"..."` 是脚本层显式语法；如果 `GenerateStaticName`、replace 范围或静态名去重回归，测试仍可能因为其他默认值路径正常而全绿，最终把 name literal 退化留到运行时才暴露 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Literals.NameLiteralRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorLiteralTests.cpp` |
| 场景描述 | 构造最小脚本：`FName A = n"Alpha"; FName B = n"Alpha"; FName C = n"Beta"; int Entry() { return A == B && A != C ? 42 : 0; }`。先直接运行 `FAngelscriptPreprocessor` 检查 processed code，再走完整编译并执行 `Entry()` |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；第一阶段直接 `AddFile()` + `Preprocess()` 读取 `Code[0].Code`，第二阶段使用 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 编译同一脚本 |
| 期望行为 | 预处理成功且 diagnostics 为空；`Code[0].Code` 不得再包含原始 `n"Alpha"` / `n"Beta"` 文本，而必须包含 `__STATIC_NAME(`；两次 `Alpha` 应被改写成相同的 static-name 索引，`Beta` 必须使用不同索引；随后编译成功且 `CompileResult == FullyHandled`、`Diagnostics.Num() == 0`，`ExecuteIntFunction()` 返回 `42`，证明 rewrite 与最终字节码执行一致 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` + `CompileModuleWithSummary` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-65：非法 `namespace` 声明必须在预处理阶段报错，而不是静默污染后续 chunk 的 `Namespace`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` |
| 现有测试覆盖 | 当前目标集没有任何 namespace fixture；已规划的 `NewTest-40` 只覆盖合法 namespaced annotated class 的成功路径，仍未覆盖 `namespace Foo` 缺少 `{` 时的错误恢复 |
| 风险评估 | 现有实现会在看到 `namespace` 关键字时直接压栈；如果缺少 `{` 仍不报错，后续 top-level class/function 会被错误打上伪造 namespace，属于典型“预处理成功但语义已漂移”的假绿路径 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Namespace.InvalidDeclarationReportsSyntax` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorNamespaceTests.cpp` |
| 场景描述 | 构造一个明显错误的脚本：`namespace Gameplay` 下一行直接声明 `UCLASS() class UBrokenNamespaceCarrier : UObject {}`，不写 `{}` block；再保留一个普通 `int Entry() { return 7; }`，观察 preprocessor 是否把后续声明错误地吸进 `Gameplay` 命名空间 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；执行前清空 diagnostics；直接调用 `FAngelscriptPreprocessor`，必要时再读取 `ChunkedCode` / `GetModulesToCompile()` |
| 期望行为 | `Preprocess()` 必须返回 `false`；diagnostics 需要给出稳定的 namespace 语法错误，而不是把错误后移到后续编译阶段；`ChunkedCode` 中后续 class/global chunk 不得携带 `Gameplay` 的 `Namespace`，`GetModulesToCompile()` 也不应继续输出可消费的 `UBrokenNamespaceCarrier` 描述，避免错误声明静默污染整个文件的 namespace 语义 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-66：`#restrict usage` 后紧跟 inline comment 时，pattern 不能把注释尾巴一起吞进去

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::ReadUntilWhitespace` / `FAngelscriptPreprocessor::KillRawLine` |
| 现有测试覆盖 | 当前目标集只规划了 `#restrict usage` 的正常路径、dead-branch 泄漏和 tab 分隔写法；没有任何用例覆盖 `Game.*//temporary` 这种 comment 紧贴 pattern 的词法边界 |
| 风险评估 | 如果 `ReadUntilWhitespace()` 把 `//temporary` 一起吞进 `Pattern`，模块限制会在没有显式编译错误的情况下静默失效，属于很难从普通 compile smoke 中发现的 metadata 漂移 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Directives.RestrictUsageInlineCommentNormalization` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` |
| 场景描述 | 构造最小脚本：首行写 `#restrict usage allow Game.*//temporary-note`，随后保留 `int Entry() { return 7; }`。直接预处理并在需要时继续编译执行，验证 comment 不会污染 restriction pattern |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；先直接调用 `FAngelscriptPreprocessor` 读取 `Module->UsageRestrictions`，再使用 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 作为对照确认其余链路仍然可执行 |
| 期望行为 | `Preprocess()` 成功且 diagnostics 为空；`UsageRestrictions.Num() == 1`，唯一 restriction 的 `Pattern` 必须精确等于 `Game.*`，不能包含 `//temporary-note`；processed code 也不应残留脏的 restriction 文本；随后编译成功并执行 `Entry()` 返回 `7`，证明 comment 归一化没有破坏整条 directive 管线 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` + `CompileModuleWithSummary` + `ExecuteIntFunction` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 12:01)

### 一、现有测试问题

#### Issue-32：`CompilerPipeline` 8 个用例完全缺席 `UINTERFACE` annotated 管线，`EndToEnd` 覆盖被高估

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateEnumClassCompile` / `FunctionDefaultsAndClassLikeCompile` / `PropertyDefaultsCompile` / `GeneratedClassConsistency` / `ModuleFunctionInspection` / `EnumAvailability` / `DelegateSignatureConsistency` / `ClassLikeReflectionShape` |
| 行号范围 | 16-499 |
| 问题描述 | 当前 8 个 compiler 用例只覆盖 `UCLASS`、`UENUM`、delegate、property default 和 class-like parameter 形状，没有任何一个脚本 fixture 声明 `UINTERFACE()` 或 `interface`。而源码里 interface 是独立预处理/生成分支：`DetectClasses()` 在 `AngelscriptPreprocessor.cpp:736-782` 为 `EChunkType::Interface` 设置 `bIsInterface` 和默认 `UInterface` 基类，`AnalyzeClasses()` 在 `949-1161` 还会做 interface inheritance 保留、方法声明规范化和 interface method stub 注册。也就是说，这个名为 `Compiler.EndToEnd` 的目标集对 interface annotated 管线实际上是空白；即便仓库其他 `Interface/` 套件覆盖了更高层行为，也没有在这里提供最小的 pipeline 回归锚点。 |
| 影响 | 一旦 interface 的预处理 chunk 切分、`UInterface` 基类解析、方法声明规范化或 wrapper/stub 注册回归，当前 compiler pipeline 仍可能全绿，导致“annotated source -> module desc -> reflected interface -> 可执行实现类”这条关键路径被误判为已覆盖。 |
| 修复建议 | 在当前主题补一条最小 `Compiler.EndToEnd.Interface...` 用例，使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)` 驱动真实 annotated 编译；断言 generated `UInterface` 和实现类同时存在、实现类 `ImplementsInterface()` 为真，并执行一个最小脚本探针验证 interface 方法确实可调。若担心文件继续膨胀，应把 interface 场景拆到新的 `AngelscriptCompilerPipelineInterfaceTests.cpp`。 |

#### Issue-33：`Preprocessor` 3 个 direct 用例完全没有触达 `interface` chunk，接口声明采集路径处于盲区

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.BasicParse` / `MacroDetection` / `ImportParsing` |
| 行号范围 | 74-219 |
| 问题描述 | 现有 3 个用例的 fixture 分别是普通函数、普通 `class AMacroActor : AActor` 和 manual `import`，没有任何一个声明 `UINTERFACE()` / `interface`。但 `FAngelscriptPreprocessor` 对接口有单独分支：`DetectClasses()` 在 `AngelscriptPreprocessor.cpp:736-782` 为 `EChunkType::Interface` 设置 `bIsInterface`、`bAbstract` 和默认 `SuperClass = "UInterface"`，`AnalyzeClasses()` 在 `949-1161` 还会保留 script interface 继承、提取 `InterfaceMethodDeclarations` 并注册 stub。当前 direct preprocessor 套件完全没有读取这些字段，也没有断言 interface 方法签名是否被规范化。 |
| 影响 | 只要普通 class/property/function/import 仍然工作，接口声明切块、接口方法收集、默认基类填充和 script-interface 继承处理的回归都不会被当前 3 个 preprocessor 用例发现；这会把一整条独立预处理分支留给更高层 scenario 测试兜底，定位成本更高。 |
| 修复建议 | 新增 dedicated `AngelscriptPreprocessorInterfaceTests.cpp`，直接驱动 `FAngelscriptPreprocessor` 读取最小 `UINTERFACE()` fixture；至少断言 `Module->Classes.Num() == 1`、`ClassDesc->bIsInterface == true`、`ClassDesc->bAbstract == true`、`ClassDesc->SuperClass == "UInterface"`、`ImplementedInterfaces` 与 `InterfaceMethodDeclarations` 精确匹配源码。对 interface inheritance 再补一个子场景验证 `interface UIChild : UIBase` 不会被错误剥掉继承关系。 |

### 二、需要新增的测试

本轮新增测试建议已记录为 `NewTest-67` / `NewTest-68` / `NewTest-69`；为避免重复，不在此处再次抄写正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-32 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingScenario: 2, MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 12:15)

### 一、现有测试问题

本轮新增正文已记录为 `Issue-34`；为遵守“只追加不覆盖”，此处仅在文件尾部补索引，不重复抄写正文。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-70` / `NewTest-71` / `NewTest-72`；为避免重复，此处不重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-34 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 12:27)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-73` / `NewTest-74` / `NewTest-75`；为避免重复，此处不重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingEdgeCase: 3 |

---

## 测试审查 (2026-04-09 12:39)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-76：字符串字面量中的 `#if/#else` 文本不能触发 directive lexer

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` |
| 现有测试覆盖 | 当前目标集没有任何 fixture 在字符串字面量中包含 `#if` / `#ifdef` / `#else` 文本；已有 directive 建议只覆盖真实预处理指令和 dead branch，不覆盖 string literal 词法边界 |
| 风险评估 | `ParseIntoChunks()` 目前在进入字符串状态机前就处理 `#...` 指令；如果字符串中的 `#if` 被误当成真实 directive，预处理器会改写 `IfDefStack` 并把该行后半段抹空，直接破坏合法源码 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Directives.StringLiteralDoesNotTriggerDirectiveLexer` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` |
| 场景描述 | 构造最小脚本：`FString BuildMarker() { return "debug #if RELEASE #else keep"; } int Entry() { return BuildMarker() == "debug #if RELEASE #else keep" ? 42 : 0; }`。先直接执行预处理，再走完整编译执行 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；第一阶段直接 `AddFile()` + `Preprocess()` 读取 `Code[0].Code`，第二阶段使用 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 编译同一脚本 |
| 期望行为 | `Preprocess()` 成功且 diagnostics 为空；processed code 必须仍然包含完整字符串字面量 `debug #if RELEASE #else keep`，不得出现被 `KillRawLine()` 截断的残缺文本；随后编译成功且 `ExecuteIntFunction()` 返回 `42`，证明字符串内容没有触发真实条件编译分支 |
| 使用的 Helper | `FAngelscriptTestFixture` + 直接调用 `FAngelscriptPreprocessor` + `CompileModuleWithSummary` + `ExecuteIntFunction` |
| 优先级 | P0 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 12:40)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-77：`import` 语句尾随 block comment 时，模块名必须先规范化再做解析

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::ProcessImports` |
| 现有测试覆盖 | `AngelscriptPreprocessorTests.cpp` 里的 `ImportParsing` 只覆盖没有任何尾随注释的干净 `import Tests.Preprocessor.Shared;`；当前目标集没有任何用例验证 `import` 词法阶段会不会把注释尾巴一起塞进 `ImportedModules` |
| 风险评估 | 现有实现会从 `import` 后一路读到 `;`，却不会像 defaults 那样剥离注释；一旦用户写出 `import Foo.Bar /* shared helpers */;`，预处理器就可能把注释文本一并写进模块名，最终退化成误导性的“找不到模块”错误 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Import.TrailingBlockCommentDoesNotPolluteModuleName` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportTests.cpp` |
| 场景描述 | 构造 provider 模块 `Tests.Preprocessor.Shared` 返回 `11`，consumer 模块使用 `import Tests.Preprocessor.Shared /* shared helpers */; int Entry() { return SharedValue(); }`。先直接预处理，再按真实模块顺序编译执行 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入两份脚本；显式关闭 automatic import；第一阶段直接调用 `FAngelscriptPreprocessor`，第二阶段把 `GetModulesToCompile()` 结果交给 `Engine->CompileModules()` 并执行 consumer 的 `Entry()` |
| 期望行为 | `Preprocess()` 成功且 diagnostics 为空；consumer 的 `ImportedModules` 必须精确只包含 `Tests.Preprocessor.Shared`，不得包含 `/* shared helpers */`；processed code 中原始 `import` 语句应被完整移除；随后编译成功且 `Entry()` 返回 `11`，证明 comment 归一化没有破坏 import 解析和后续绑定 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` + `FAngelscriptEngine::CompileModules` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 23:48)

### 一、现有测试问题

#### Issue-38：compiler pipeline 只检查 `UFunction` 存在性，完全没有锁住 `BlueprintCallable/BlueprintPure` 函数标志

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateEnumClassCompile` / `FunctionDefaultsAndClassLikeCompile` / `GeneratedClassConsistency` / `ClassLikeReflectionShape` |
| 行号范围 | 71-78，149-156，258-266，464-495 |
| 问题描述 | 这 4 个用例都会在生成类上查 `GetScore`、`EchoPlainClass`、`EchoActorClass`、`EchoSoftActorClass` 等 `UFunction`，但断言只停在“函数存在”或“返回/参数 property 形状正确”。源码 `FAngelscriptPreprocessor::ProcessFunctionMacro()` 在 `AngelscriptPreprocessor.cpp:1466-1496,1518,1551,1595,1671` 明确根据 `bDefaultFunctionBlueprintCallable`、`BlueprintCallable`、`NotBlueprintCallable`、`BlueprintPure`、网络 specifier 等分支决定 `FunctionDesc->bBlueprintCallable`/`bBlueprintPure`；当前套件却没有任何一条断言去读 `UFunction::FunctionFlags` 或对应描述符字段。 |
| 影响 | 只要函数对象还能生成，这几条测试就会继续全绿；一旦预处理器把默认 callable 语义、`BlueprintPure => BlueprintCallable` 联动、或 `NotBlueprintCallable` 覆盖逻辑改坏，Blueprint/反射可见性会静默漂移，而当前 compiler pipeline 不会给出信号。 |
| 修复建议 | 在现有 generated-function 断言后补充函数标志检查，而不是只看存在性：对默认 `UFUNCTION()` 场景显式断言当前合同下是否带 `FUNC_BlueprintCallable`；对未来 dedicated setting 测试再分别断言 `BlueprintPure` 同时带 `FUNC_BlueprintCallable | FUNC_BlueprintPure`、`NotBlueprintCallable` 明确移除 callable 标志。若这些断言会让当前综合用例继续膨胀，应把函数标志主题拆到新的 `AngelscriptCompilerPipelineFunctionFlagTests.cpp`。 |

### 二、需要新增的测试

本轮未新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-38 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无 | 0 | 无 |


---

## 测试审查 (2026-04-09 23:49)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-86：`UFUNCTION()` 默认 Blueprint 暴露策略必须遵守 `bDefaultFunctionBlueprintCallable`，且显式 specifier 要能稳定覆盖默认值

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessFunctionMacro` |
| 现有测试覆盖 | 当前目标集只覆盖 `BlueprintEvent/BlueprintOverride` 错误与 wrapper 路径，以及若干 generated `UFunction` 的存在性/参数形状；没有任何用例改写 `bDefaultFunctionBlueprintCallable`，也没有断言 `BlueprintCallable`、`NotBlueprintCallable`、`BlueprintPure` 对最终 `UFunction::FunctionFlags` 的影响 |
| 风险评估 | 这条分支决定脚本函数是否对 Blueprint/反射层可见。若默认 callable 策略或显式覆盖关系回归，编译仍然会成功，但用户暴露给 Blueprint 的 API 集合会静默漂移，现有套件无法第一时间定位到 `ProcessFunctionMacro()`。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.FunctionBlueprintCallableDefaultsAndOverrides` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineFunctionFlagTests.cpp` |
| 场景描述 | 用 table-driven 方式跑两个子场景。子场景 A：临时把 `bDefaultFunctionBlueprintCallable=false`，编译 `UCLASS()` 载体类，包含 `UFUNCTION() int ImplicitDefault()`、`UFUNCTION(BlueprintCallable) int ExplicitCallable()`、`UFUNCTION(BlueprintPure) int PureValue()`。子场景 B：临时把 `bDefaultFunctionBlueprintCallable=true`，编译另一载体类，包含 `UFUNCTION() int ImplicitCallable()` 与 `UFUNCTION(NotBlueprintCallable) int HiddenValue()`。两轮都走完整 annotated compile。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 clean engine + `FAngelscriptEngineScope`；通过 `GetMutableDefault<UAngelscriptSettings>()` 保存并恢复 `bDefaultFunctionBlueprintCallable`；使用 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)` 编译；随后通过 `FindGeneratedClass()` / `FindGeneratedFunction()` 读取生成 `UFunction`。 |
| 期望行为 | 两个子场景都应 `bCompileSucceeded == true`、`CompileResult == FullyHandled`、`Diagnostics.Num() == 0`。子场景 A 中：`ImplicitDefault` 不得带 `FUNC_BlueprintCallable`；`ExplicitCallable` 必须带 `FUNC_BlueprintCallable` 且不带 `FUNC_BlueprintPure`；`PureValue` 必须同时带 `FUNC_BlueprintCallable | FUNC_BlueprintPure`。子场景 B 中：`ImplicitCallable` 必须带 `FUNC_BlueprintCallable`；`HiddenValue` 不得带 `FUNC_BlueprintCallable`。必要时再追加一次 direct preprocessor 断言，确认对应 `FAngelscriptFunctionDesc::bBlueprintCallable/bBlueprintPure` 与生成 `UFunction` flags 一致。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `GetMutableDefault<UAngelscriptSettings>()` + `CompileModuleWithSummary` + `FindGeneratedClass` + `FindGeneratedFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:05)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-87：未知父类必须在预处理阶段给出稳定诊断并阻止继续生成 class desc

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ResolveSuperClass` |
| 现有测试覆盖 | 当前目标集覆盖了普通 `UObject`/`AActor` 继承成功、interface 继承非 interface 报错、struct 非法继承报错，但 3006-3015 行的 `Class %s has an unknown super type %s.` 分支完全没有自动化覆盖 |
| 风险评估 | 一旦 unknown-super 诊断文本、行号或 early-out 时机回归，脚本类可能带着无效父类继续流入后续 class generator/compile；问题会从“预处理立即定位”退化成更晚阶段的二次错误，定位成本明显上升 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Classes.UnknownSuperTypeReportsDiagnostic` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorClassErrorTests.cpp` |
| 场景描述 | 构造最小 annotated 脚本：`UCLASS() class UUnknownSuperCarrier : UMissingBaseType {}`。直接运行 `FAngelscriptPreprocessor`，只聚焦 class super-type 解析失败路径，不掺杂其他宏或函数体 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；执行前清空 `Engine.Diagnostics` / `LastEmittedDiagnostics`；直接调用 `Preprocessor.AddFile()` + `Preprocess()` |
| 期望行为 | `Preprocess()` 必须返回 `false`；diagnostics 精确包含 `Class UUnknownSuperCarrier has an unknown super type UMissingBaseType.`，且 `Diagnostic.Row` 固定在 class 声明行；`GetModulesToCompile()` 不得留下可继续消费的 `UUnknownSuperCarrier` class desc 或可编译 code section，避免半合法类型漏到后续编译 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-88：复合 `#if` 条件必须稳定报“不支持的 condition”诊断，而不是静默误判

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ReadIdentifier` / `FAngelscriptPreprocessor::ParsePreProc` |
| 现有测试覆盖 | 当前目标集覆盖了单标识符 `#if EDITOR`、`#if !EDITOR`、unknown flag、`#elif` short-circuit 和结构性 directive 错误，但没有任何用例触发 `ParsePreProc()` 对 `EDITOR && TEST` 这类复合表达式的拒绝路径 |
| 风险评估 | 真实脚本里很容易写出 `#if EDITOR && TEST` 或 `#if EDITOR || RELEASE`。如果没有专门回归保护，这类不支持语法可能被误判成别的 flag 名、报错行号漂移，甚至在未来改动中被错误吞掉造成分支静默错判 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Directives.CompoundConditionReportsUnsupportedSyntax` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorDirectiveTests.cpp` |
| 场景描述 | 构造最小脚本：`#if EDITOR && TEST` 下放一个简单 `int Entry() { return 7; }`，随后 `#endif` 结束。直接运行预处理，专门验证 compound condition 的诊断合同 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；执行前清空 `Engine.Diagnostics` / `LastEmittedDiagnostics`；直接调用 `FAngelscriptPreprocessor` |
| 期望行为 | `Preprocess()` 必须返回 `false`；diagnostics 精确包含 `Invalid preprocessor condition: EDITOR && TEST`，且 `Diagnostic.Row` 固定在 `#if` 所在行；`GetModulesToCompile()` 不得输出可继续编译的合法模块，避免 unsupported syntax 被当成普通 dead branch 静默吞掉 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-89：`CallInEditor` 与 `Exec` 必须稳定落到生成 `UFunction` 的 metadata/flags

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessFunctionMacro` |
| 现有测试覆盖 | 当前目标集只覆盖 `BlueprintCallable`、`BlueprintPure`、`BlueprintEvent/Override`、网络 specifier 与若干函数存在性检查；`CallInEditor`、`Exec` 两条正向分支完全没有自动化覆盖 |
| 风险评估 | 一旦 `ProcessFunctionMacro()` 不再把 `CallInEditor` 写进 metadata，或漏掉 `Exec` 对应的函数标志，脚本函数仍可能正常编译，但编辑器按钮暴露与 console command 入口会静默失效；现有 compiler pipeline 套件无法把问题定位到 function specifier 解析层 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.FunctionEditorAndExecSpecifiersPropagate` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineFunctionMetadataTests.cpp` |
| 场景描述 | 编译最小 annotated 类：`UCLASS() class UFunctionMetadataCarrier : UObject { UFUNCTION(CallInEditor) int RunEditorAction() { return 7; } UFUNCTION(Exec) int RunConsoleAction() { return 8; } }`。编译后读取生成类上的两个 `UFunction`，并执行最小函数体确认 specifier 不会破坏可执行路径 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)` 编译；随后用 `FindGeneratedClass()`、`FindGeneratedFunction()` 和 `ExecuteGeneratedIntEventOnGameThread()` 读取并执行默认对象上的函数 |
| 期望行为 | 编译成功且 `bCompileSucceeded == true`、`CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；`RunEditorAction` 的 `GetMetaData(TEXT("CallInEditor"))` 必须等于 `true`；`RunConsoleAction` 必须带 `FUNC_Exec`；两个函数都存在且在默认对象上执行时分别返回 `7` 与 `8`，证明 function specifier 传播与生成字节码两端都正确 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `FindGeneratedClass` + `FindGeneratedFunction` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |
| P2 | 1 | NoTestForSource: 1 |
---

## 测试审查 (2026-04-10 00:22)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

本轮新增正文已记录为 `NewTest-90` / `NewTest-91`；为避免重复，此处不重复正文。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |

---

## 测试审查 (2026-04-10 01:05)

### 一、现有测试问题

#### Issue-39：annotated compiler pipeline 用例完全不读取 metadata，注释与文档链路仍是盲区

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateEnumClassCompile` / `GeneratedClassConsistency` / `EnumAvailability` / `ClassLikeReflectionShape` |
| 行号范围 | 21-79，234-267，334-360，425-495 |
| 问题描述 | 这些用例都在编译带 `UENUM` / `UCLASS` / `UPROPERTY` / `UFUNCTION` 的 annotated 脚本，但断言只停在“类型存在”“flag/shape 正确”这一层，没有任何一条去读取 `UClass/UFunction/FProperty` 的 metadata 或 `FAngelscriptEnumDesc::Documentation`。而源码 `AngelscriptPreprocessor.cpp:770-772,1473-1475,2460-2461,2817-2825` 明确把 comment 规范化结果写进 `ToolTip` / enum 文档元数据。当前 suite 即使全绿，也不能证明 `Helper_CommentFormat.h` 与 comment-to-metadata 接线仍然正常。 |
| 影响 | 只要反射对象还能生成，这批“compiler pipeline” 用例就会继续通过；一旦注释被错误清洗、`ToolTip` 丢失、enum 文档没有挂到 desc/meta，编辑器 tooltip 和文档显示会静默退化，而现有套件不会给出任何信号。 |
| 修复建议 | 不要继续把 metadata 路径留给间接覆盖。最小修复是给现有 annotated fixture 增加前置 comment，并在断言里显式读取 `GeneratedClass->GetMetaData(TEXT("ToolTip"))`、属性/函数 `GetMetaData(TEXT("ToolTip"))`、`EnumDesc->Documentation` 与 enum value metadata。若不想让当前 smoke 用例继续膨胀，应拆出专门的 `AngelscriptCompilerPipelineMetadataTests.cpp`，把“对象存在”和“metadata 正确”分层验证。 |

### 二、需要新增的测试

#### NewTest-92：CPP-style tooltip 中的 URL 与路径分隔符必须保留正文里的 `//`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/Helper_CommentFormat.h` |
| 关联函数 | `FormatCommentForToolTip` |
| 现有测试覆盖 | 当前目标集没有任何自动化直接喂入带 `https://`、UNC/path 或正文 `//` 文本的 CPP-style comment；现有 2 个测试文件也完全不触达 `Helper_CommentFormat.h` |
| 风险评估 | `FormatCommentForToolTip()` 在 97-101 行对 CPP-style comment 直接做 `Replace("///", "//").Replace("//", "")`。若没有专门回归，tooltip 里的 URL、网络路径或示例代码会被静默改写成缺失分隔符的脏文本，问题只会在编辑器文档显示时暴露 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.CommentFormatting.CppStylePreservesInnerSlashes` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptCommentFormattingTests.cpp` |
| 场景描述 | 直接调用 `FormatCommentForToolTip`，输入两组 CPP-style 注释字符串，例如 `// Visit https://example.com/docs//v2` 与 `/// Mirror //server/share`，专门验证“去掉 comment marker”不会顺手删除正文里的 `//` |
| 输入/前置 | 纯 Automation test；无需引擎或 fixture 文件；把原始 comment 字符串直接传入 helper |
| 期望行为 | 第一组输出必须精确等于 `Visit https://example.com/docs//v2`；第二组输出必须精确等于 `Mirror //server/share`。测试不接受 `https:example.com`、`server/share` 这类正文分隔符被吞掉的结果 |
| 使用的 Helper | 其他（纯 Automation test，直接调用 `FormatCommentForToolTip`） |
| 优先级 | P2 |

#### NewTest-93：`(cpptext)` 占位符与 tab 缩进归一化必须有独立回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/Helper_CommentFormat.h` |
| 关联函数 | `FormatCommentForToolTip` |
| 现有测试覆盖 | 当前目标集没有任何用例命中 103-115 行的 `(cpptext)` 特判与 `ConvertTabsToSpaces(8)` 路径；`Preprocessor` / `Compiler` 现有 10 个测试文件中也没有读取 comment helper 的精确格式化结果 |
| 风险评估 | 如果 `(cpptext)` 占位符不再被去掉，或 tab 缩进归一化/统一去缩进退化，编辑器里显示的 tooltip 会残留 parser 噪音和错位缩进；这类问题通常不会影响编译成功，因此很容易长期潜伏 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.CommentFormatting.CppTextPlaceholderAndTabNormalization` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptCommentFormattingTests.cpp` |
| 场景描述 | 在同一个 helper test 中覆盖两个子场景。子场景 A：输入 `// (cpptext)\n// Real tooltip line`，验证占位符被删除后只留下真实 tooltip。子场景 B：输入带 tab 缩进的 JavaDoc/C-style 多行注释，例如 `/**\n\tTitle\n\tDetail\n*/`，验证 tab 会被归一化后再统一去除公共缩进 |
| 输入/前置 | 纯 Automation test；直接传入原始 comment 字符串；每个子场景单独比较 helper 返回值 |
| 期望行为 | 子场景 A 的输出必须精确等于 `Real tooltip line`，不得残留 `(cpptext)` 或多余空行；子场景 B 的输出必须精确等于 `Title\nDetail`，不允许保留 tab 字符、8-space 残留缩进或额外首尾空白 |
| 使用的 Helper | 其他（纯 Automation test，直接调用 `FormatCommentForToolTip`） |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-39 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 2 | MissingEdgeCase: 1, NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:44)

### 一、现有测试问题

#### Issue-40：`MacroDetection` 只停在 `GatherMacros()` 结果，没有验证宏是否真正进入类描述符流水线

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.MacroDetection` |
| 行号范围 | 141-161 |
| 问题描述 | 用例在 `Preprocess()` 成功后，只调用 `GatherMacros(Preprocessor)` 从 `ChunkedCode[*].Macros` 收集词法级宏记录，并用两个 `ContainsByPredicate` 检查 `Mesh` 与 `BeginPlay` 是否出现。它没有读取 `GetModulesToCompile()`，也没有检查 `Module->Classes`、`FAngelscriptClassDesc::Properties`、`Methods` 等由 `Preprocess()` 后半段 `DetectClasses()` / `AnalyzeClasses()` / `ProcessMacros()` 生成的结果。换言之，只要 chunk 里还残留两个目标宏名，这个测试就会通过，即便后续把宏消费进类描述符的逻辑已经退化。 |
| 影响 | 当前用例更像“macro lexing smoke”，而不是预处理器宏管线的质量锚点。若 `ProcessPropertyMacro()` / `ProcessFunctionMacro()` 开始漏建 property/function desc、把宏挂到错误 class、或在 `AnalyzeClasses()` 后丢失类上下文，CI 仍会显示全绿。 |
| 修复建议 | 保留现有 `GatherMacros()` 断言作为词法层 smoke，但追加第二层结果断言：读取 `GetModulesToCompile()`，精确断言只有一个目标模块、一个目标类描述符，并且该类描述符内存在名为 `Mesh` 的 property desc 与名为 `BeginPlay` 的 function/method desc，必要时再检查 `BlueprintOverride` 或对应 metadata。若不想把一个用例塞成两层职责，建议拆成 `MacroLexing` 与 `MacroDescriptorRoundTrip` 两个测试。 |

### 二、需要新增的测试

#### NewTest-94：hot reload 批次内的重复 class 名必须稳定报冲突诊断

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::DetectClasses` / `FAngelscriptPreprocessor::IsPreprocessingModule` |
| 现有测试覆盖 | 当前目标集只覆盖唯一类名的 happy path，没有任何用例在“已有 active module + 当前 hot reload 批次重编多个模块”场景下触发重复 class 名冲突 |
| 风险评估 | reflected class 名冲突是 class generator / reload 流水线的基础防线。若没有专门回归，重复类名可能在热重载时静默落到错误模块、覆盖旧类或留下二义性诊断，而现有 `Preprocessor` 与 `Compiler` 套件都不会给出信号。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Classes.DuplicateClassNameAcrossHotReloadBatchReportsConflict` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorClassHierarchyTests.cpp` |
| 场景描述 | 第一步先编译 `Tests.Preprocessor.First`，让引擎里已有 `UCLASS() class UDuplicateCarrier : UObject {}`。第二步模拟 hot reload：当前批次同时预处理更新版 `First.as` 和新的 `Second.as`，两者都声明同名 `UDuplicateCarrier`。 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；用 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)` 或等价 helper 先种下 active module；随后通过 `FAngelscriptTestFixture` 写入 `First.as` 与 `Second.as`，直接驱动 `FAngelscriptPreprocessor`；执行前清空 diagnostics。 |
| 期望行为 | 第二步 `Preprocess()` 必须返回 `false`；diagnostics 精确包含 `Cannot declare class UDuplicateCarrier in module Tests.Preprocessor.Second. A class with this name already exists in module Tests.Preprocessor.First.`；`Diagnostic.Row` 固定在第二个声明所在行；`GetModulesToCompile()` 不得留下两个同名且可继续消费的 class desc。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + `CompileModuleWithSummary` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

#### NewTest-95：script class 继承另一份 script class 必须走通预处理到可执行字节码

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ResolveSuperClass` / `FAngelscriptPreprocessor::GetClassDescFor` |
| 现有测试覆盖 | 当前目标集只覆盖 `UObject` / `AActor` 等 code class 继承成功，另有 interface 继承与错误路径建议；没有任何一个 `Preprocessor` 或 `Compiler` 用例验证“script class 继承 script class”这条正向分支 |
| 风险评估 | 如果 `GetClassDescFor()` 或 `ResolveSuperClass()` 在脚本层级解析上退化，child class 可能仍然被生成，但会挂到错误的 `SuperClass` / `CodeSuperClass`，最终在反射层或运行期才暴露层级错乱；现有 suite 对这条公共能力没有直接保护。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.ScriptSuperclassRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineClassHierarchyTests.cpp` |
| 场景描述 | 构造两份脚本。`Base.as`：`UCLASS() class UHierarchyBase : UObject { UFUNCTION() int GetBaseValue() { return 7; } }`。`Child.as`：`UCLASS() class UHierarchyChild : UHierarchyBase { UFUNCTION() int GetDerivedValue() { return GetBaseValue(); } }`。先直接预处理，再把两个 module desc 交给真实 `CompileModules()`，最后执行 child 方法。 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入两份脚本并按 `Base -> Child` 顺序 `AddFile()`；编译前后都清空并检查 diagnostics；编译完成后通过 `FindGeneratedClass` / `FindGeneratedFunction` / `ExecuteGeneratedIntEventOnGameThread` 读取并执行生成类。 |
| 期望行为 | `Preprocess()` 必须成功且 diagnostics 为空；`GetModulesToCompile()` 中 child 的 `ClassDesc->SuperClass` 精确等于 `UHierarchyBase`；`CompileModules()` 成功后，`GeneratedChild->GetSuperClass() == GeneratedBase`；在 child 默认对象上执行 `GetDerivedValue()` 必须返回 `7`，证明 script-super 解析、生成类继承关系和最终字节码执行三段链路全部打通。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` + `FAngelscriptEngine::CompileModules` + `FindGeneratedClass` + `FindGeneratedFunction` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-40 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 1, NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:53)

### 一、现有测试问题

本轮未新增现有测试质量问题。

### 二、需要新增的测试

#### NewTest-96：`n"..."` / `f"..."` 只能在独立 token 起点触发 rewrite

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::GenerateStaticName` / `FAngelscriptPreprocessor::GenerateFormatString` |
| 现有测试覆盖 | 当前目标集已规划 `NewTest-24` 覆盖合法 `f"..."`，`NewTest-64` 覆盖合法 `n"..."`；但 2 个现有测试文件和已记录建议里都没有“prefixed literal 紧贴标识符时不应被预处理器吞走”的负向场景 |
| 风险评估 | `ParseIntoChunks()` 在 `AngelscriptPreprocessor.cpp:3749-3775` 只判断当前字符是 `n`/`f` 且下一个字符是 `"`，没有校验前导 token 边界。若没有专项回归，`Actionn"Tag"`、`Valuef"{123}"` 这类本应保留给后续脚本编译器报词法错误的文本，可能在预处理阶段被静默改写成 `__STATIC_NAME(...)` 或 format rewrite，导致错误位置、错误文本和真实语义一起漂移。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Literals.PrefixedLiteralsRequireTokenBoundary` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorLiteralTests.cpp` |
| 场景描述 | 设计两个子场景。子场景 A：脚本正文包含 `void Probe() { Actionn"Tag"; }`。子场景 B：脚本正文包含 `void Probe() { Valuef"{123}"; }`。两者都只验证 malformed token 不会被 preprocessor 当成合法 `n"..."` / `f"..."` 重写。 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入单文件脚本；第一阶段直接 `AddFile()` + `Preprocess()` 读取 `Code[0].Code`；第二阶段使用 `CompileModuleWithSummary(..., bUsePreprocessor=true)` 收集最终编译诊断。 |
| 期望行为 | 两个子场景的 `Preprocess()` 都必须成功，但 `Code[0].Code` 必须保留原始 `Actionn"Tag"` / `Valuef"{123}"` 文本，且不得出现对应位置被替换成 `__STATIC_NAME(` 或 format helper 代码的结果；随后编译必须失败，`Summary.Diagnostics` 要把错误稳定定位在原始 malformed token 所在行，而不是 preprocessor 生成代码行。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` + `CompileModuleWithSummary` |
| 优先级 | P1 |

#### NewTest-97：同一 chunk 中相邻的 `n"..."` 与 `f"..."` rewrite 必须保持位置稳定

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ParseIntoChunks` / `FAngelscriptPreprocessor::ProcessReplacements` / `FAngelscriptPreprocessor::GenerateStaticName` / `FAngelscriptPreprocessor::GenerateFormatString` |
| 现有测试覆盖 | `NewTest-24` 与 `NewTest-64` 都是单一 rewrite 类型；现有目标集没有任何场景把多个 `n"..."` 与 `f"..."` 放在同一 code chunk、同一函数甚至同一行上验证 replacement 顺序 |
| 风险评估 | `ProcessReplacements()` 在 `AngelscriptPreprocessor.cpp:3943-3980` 依赖 source offset 排序后回放替换。若多个 prefixed literal 相邻出现时位置调整有回归，后一个 replacement 很容易覆盖错误范围、留下半截原文，或把前一个 rewrite 产物再次切坏；这类问题单独的 `n"..."` / `f"..."` happy path 用例捕不到。 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.MixedLiteralRewritesRemainStable` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineLiteralRewriteTests.cpp` |
| 场景描述 | 构造一个最小模块，把多个 rewrite 放在同一函数内并尽量相邻书写，例如：`FName Alpha = n"Alpha"; int Value = 21; FString Msg = f"{Value =}"; FName Beta = n"Beta"; int Entry() { return Alpha == n"Alpha" && Alpha != Beta && Msg == "Value = 21" ? 42 : 0; }`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 clean engine + `FAngelscriptEngineScope`；第一阶段直接运行 `FAngelscriptPreprocessor` 检查 processed code；第二阶段使用 `CompileModuleWithSummary(..., ECompileType::FullReload, bUsePreprocessor=true)` 编译并 `ExecuteIntFunction()` 执行 `Entry()`。 |
| 期望行为 | `Preprocess()` 必须成功且 diagnostics 为空；processed code 中原始 `n"Alpha"`、`n"Beta"`、`f"{Value =}"` 文本都必须被完整移除，不能留下半截 token；随后编译成功且 `CompileResult == FullyHandled`、`Diagnostics.Num() == 0`，`ExecuteIntFunction()` 返回 `42`，证明多种 rewrite 共存时 replacement 顺序没有破坏最终字节码。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + 直接调用 `FAngelscriptPreprocessor` + `CompileModuleWithSummary` + `ExecuteIntFunction` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无 | 0 | 无 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:56)

### 一、现有测试问题

#### Issue-41：多个 compiler 用例用带前缀回退的 `FindGeneratedClass()` 做存在性判断，会掩盖生成类命名错误

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateEnumClassCompile` / `FunctionDefaultsAndClassLikeCompile` / `PropertyDefaultsCompile` / `GeneratedClassConsistency` / `ClassLikeReflectionShape` |
| 行号范围 | 70-78，148-156，198-217，258-266，458-495 |
| 问题描述 | 这些用例都把 `AngelscriptTestSupport::FindGeneratedClass(&Engine, TEXT("U..."))` 当成“生成类存在”的主断言。但 helper 在 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp:500-519` 里先查完整类名，查不到后又会自动把前导 `U`/`A` 去掉再查一次。也就是说，只要包里存在 `CompilerTransferObject` 这类 stripped name，对应断言仍可能通过，测试无法证明生成类确实按预期名称落成 `UCompilerTransferObject` / `UCompilerDefaultsCarrier` 等目标名字。 |
| 影响 | 生成类命名、前缀保留、包内对象名规范化一旦回归，这几条编译管线测试仍可能保持全绿；后续出现 Blueprint 查找、反射命名或 reload 替换异常时，当前 suite 给出的信号会明显偏弱。 |
| 修复建议 | 在这些用例里把“存在性”与“latest-class 追踪”拆开：先直接对 `Engine.GetPackageInstance()` 调用 `FindObject<UClass>(Package, TEXT("UCompilerTransferObject"))` 这类精确查找，并断言 `GetName()` 与脚本声明完全一致；只有在明确要验证 `GetMostUpToDateClass()` 时才再调用 `FindGeneratedClass()`。若未来保留 helper，也应增加一个不带前缀回退的 `FindGeneratedClassExact()`，供 pipeline 测试使用。 |

#### Issue-42：多条 generated `UFUNCTION` 断言只按名字找函数，签名漂移仍可能假绿

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.DelegateEnumClassCompile` / `FunctionDefaultsAndClassLikeCompile` / `GeneratedClassConsistency` |
| 行号范围 | 76-78，148-156，264-266 |
| 问题描述 | 这 3 个用例把 `GetScore`、`EchoPlainClass`、`EchoActorClass`、`EchoSoftActorClass` 的验证都收敛成 `AngelscriptTestSupport::FindGeneratedFunction(GeneratedClass, TEXT(\"...\")) != nullptr`。但 helper 在 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp:525-531` 本质只是 `OwnerClass->FindFunctionByName(FuncName)`，它既不检查返回类型、参数列表，也不检查 `FunctionFlags`。因此只要类上还挂着同名 `UFunction`，哪怕签名已经从 `int GetScore()` 漂移成别的返回类型/参数形状，这几条断言依然会通过。 |
| 影响 | 现有 compiler suite 对“生成了一个同名函数”和“生成了正确的 `UFUNCTION`”没有清晰区分，函数包装签名回归、参数 marshalling 漂移或 flags 异常都可能被这几条 smoke 断言漏掉。 |
| 修复建议 | 对每个目标函数至少补一层精确签名断言：`GetScore` 需要验证 `GetReturnProperty()` 为 `FIntProperty` 且没有输入参数；`EchoPlainClass` / `EchoActorClass` / `EchoSoftActorClass` 需要显式读取返回与参数 property 的具体类型、`MetaClass` 和必要的 flag，而不是只看函数存在。若用例职责过重，建议把“函数存在”留在组合 smoke，把签名断言拆到专门的 `AngelscriptCompilerPipelineGeneratedFunctionTests.cpp`。 |

### 二、需要新增的测试

#### NewTest-98：generated class 必须以脚本声明名精确落成包内 `UClass`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::DetectClasses` |
| 现有测试覆盖 | 当前 compiler 用例只通过带前缀回退的 `FindGeneratedClass()` 取类对象，没有任何一条断言包内真实对象名必须精确等于脚本声明的 `U/A` 前缀类名 |
| 风险评估 | 若生成类开始以 stripped name、错误 sanitize 结果或别名对象落地，现有套件仍可能依赖 helper 回退继续全绿，真实反射查找和 Blueprint 命名却会悄悄退化 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.GeneratedClassExactNameLookup` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineNamingTests.cpp` |
| 场景描述 | 编译一个最小 annotated 模块：`UCLASS() class UExactNameCarrier : UObject { UFUNCTION() int GetValue() { return 42; } }`，编译后直接对引擎包做精确对象查找，而不是只走 helper |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., ECompileType::FullReload, TEXT("CompilerGeneratedClassExactNameLookup"), TEXT("Tests/Compiler/CompilerGeneratedClassExactNameLookup.as"), Script, true, Summary, false)` 编译；随后取 `UPackage* Package = Engine.GetPackageInstance()` |
| 期望行为 | `bCompileSucceeded == true`、`CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；`FindObject<UClass>(Package, TEXT("UExactNameCarrier"))` 必须非空，且 `GetName()` 精确等于 `UExactNameCarrier`；`FindObject<UClass>(Package, TEXT("ExactNameCarrier"))` 必须为空；`FindGeneratedClass(&Engine, TEXT("UExactNameCarrier"))` 返回的对象必须与精确查找结果同一指针；最后执行 `GetValue()` 返回 `42`，证明精确命名对象也是可执行对象 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `FindObject<UClass>` + `FindGeneratedFunction` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

#### NewTest-99：generated `UFUNCTION` 需要锁住普通零参返回与 raw `UClass` 参数的精确签名

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptPreprocessor::ProcessFunctionMacro` |
| 现有测试覆盖 | 当前目标集对 generated 成员函数要么只看 `FindGeneratedFunction() != nullptr`，要么只验证 `TSubclassOf` / `TSoftClassPtr` 的 property 形状；没有单独锁住 `int GetScore()` 这种普通零参函数和裸 `UClass` 参数/返回的精确 `UFunction` 签名 |
| 风险评估 | 一旦函数包装把零参方法错误生成为带额外参数的签名，或把裸 `UClass` 错误收窄成别的 class-like 约束，当前 suite 仍可能只凭“函数存在”继续通过，真实调用与反射签名会在更晚阶段才暴露问题 |
| 建议测试名 | `Angelscript.TestModule.Compiler.EndToEnd.GeneratedFunctionSignatureExactShape` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineGeneratedFunctionTests.cpp` |
| 场景描述 | 编译最小 annotated 类：`UCLASS() class UGeneratedFunctionSignatureCarrier : UObject { UPROPERTY() int Score; UFUNCTION() int GetScore() { return Score; } UFUNCTION() UClass EchoPlainClass(UClass Value) { return Value; } }`，随后直接读取生成 `UFunction` 的反射形状 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `CompileModuleWithSummary(..., ECompileType::FullReload, TEXT("CompilerGeneratedFunctionSignatureExactShape"), TEXT("Tests/Compiler/CompilerGeneratedFunctionSignatureExactShape.as"), Script, true, Summary, false)` 编译；编译后用精确类查找或 `FindGeneratedClass()` 获取 `UGeneratedFunctionSignatureCarrier` |
| 期望行为 | `bCompileSucceeded == true`、`CompileResult == FullyHandled`、`Diagnostics.Num() == 0`；`GetScore` 必须存在，`GetReturnProperty()` 为 `FIntProperty`，且 `FindFProperty<FProperty>(GetScore, TEXT("Value")) == nullptr`，证明没有平白生成输入参数；`EchoPlainClass` 的返回与参数都必须是 `FClassProperty`，并且两个 `MetaClass` 都精确等于 `UObject::StaticClass()`，从而锁住裸 `UClass` 路径而不是 `TSubclassOf` 式收窄；必要时再对默认对象执行最小探针函数确认签名正确的函数也能正常运行 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `CompileModuleWithSummary` + `FindGeneratedClass` + `FindGeneratedFunction` + `FindFProperty` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-41 |
| WeakAssertion | 1 | Issue-42 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 01:05)

### 一、现有测试问题

#### Issue-43：ImportParsing 背后的 automatic-import warning 合同在当前源码控制流里不可达

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp |
| 测试名 | Angelscript.TestModule.Preprocessor.ImportParsing |
| 行号范围 | 169-218 |
| 问题描述 | 当前唯一的 import 用例只覆盖手动 import happy path；而源码交叉校验显示，FAngelscriptPreprocessor::Preprocess() 只有在 !FAngelscriptEngine::ShouldUseAutomaticImportMethodForCurrentContext() 时才会进入 ProcessImports()（AngelscriptPreprocessor.cpp:223-229），但 manual-import warning 与 WarnOnManualImportStatements 的读取却写在 ProcessImports() 内部（AngelscriptPreprocessor.cpp:485-489）。在稳定的 engine context 下，这意味着“automatic imports are active, import statements will be ignored.” 这条 warning/config 分支实际上没有可执行入口。现有测试文件因此不仅没覆盖该合同，而且会让人误以为只要补一个 automatic-mode 用例就能验证 warning 行为。 |
| 影响 | 当前 import 覆盖被高估：WarnOnManualImportStatements 可能长期处于死代码状态而无人发现；后续文档或测试若继续按“automatic mode 会 strip 并 warning”来设计，会不断追逐一个当前实现根本到不了的分支。 |
| 修复建议 | 先明确产品合同，再对实现与测试一起收口：如果 automatic mode 下手写 import 的目标是“忽略并按设置决定是否 warning”，就把 strip/warning 逻辑移到 automatic path 真正会执行的位置，再新增 setting on/off 场景测试；如果 automatic mode 下根本不打算处理这类语句，就删除这段不可达 warning 分支，并同步修正文档与覆盖计划，避免继续把它当成现有能力。 |
#### Issue-44：ModuleFunctionInspection 已拿到精确 sIScriptModule*，执行阶段却回退到按模块名二次查找

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp |
| 测试名 | Angelscript.TestModule.Compiler.EndToEnd.ModuleFunctionInspection |
| 行号范围 | 283-317 |
| 问题描述 | 用例在 283-309 行已经通过 AngelscriptTestSupport::BuildModule(...) 拿到了当前编译产物的精确 sIScriptModule* Module，并在这份模块上检查了 SumWithDefault；但到了执行断言，代码又改成 AngelscriptTestSupport::ExecuteIntFunction(&Engine, TEXT("CompilerModuleFunctionInspection"), TEXT("int Entry()"), Result)（311-317 行）。这个 helper 会在 AngelscriptTestEngineHelper.cpp:367-391 里按模块名重新查 ModuleDesc，再按声明重新找函数，而不是执行刚才 inspection 过的那份 Module。仓库里其实已经有 AngelscriptTestSupport::ExecuteIntFunction(*this, Engine, asIScriptFunction&, OutValue) 这种“直接执行精确函数指针”的 helper（AngelscriptTestUtilities.h:667-695）。 |
| 影响 | 当前文件中唯一真正查看 sIScriptModule 函数形状的用例，没有把“被检查的模块”和“被执行的字节码”绑定到同一个对象上。只要引擎级模块查找还能返回一个同名模块，执行断言就可能与前面的 inspection 脱节，从而削弱该用例对 module identity/stale lookup 问题的保护。 |
| 修复建议 | 在 BuildModule() 返回后，直接从当前 Module 上精确获取 Entry 的 sIScriptFunction*，并改用 AngelscriptTestSupport::ExecuteIntFunction(*this, Engine, *EntryFunction, Result) 执行；这样 inspection 与 execution 会绑定到同一份编译产物。若必须保留按模块名执行，也至少要额外断言重新查到的 ScriptModule 指针与 BuildModule() 返回的 Module 完全相同。 |

### 二、需要新增的测试

#### NewTest-100：automatic import 模式下的 manual-import warning 必须受 WarnOnManualImportStatements 控制

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp；Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h |
| 关联函数 | FAngelscriptPreprocessor::Preprocess / FAngelscriptPreprocessor::ProcessImports |
| 现有测试覆盖 | 当前目标集只有 manual import 的 happy path，没有任何用例显式切换 UAngelscriptSettings::bWarnOnManualImportStatements；因此 warning setting 的 on/off 合同完全没有被自动化锁住。 |
| 风险评估 | 如果 automatic-import 兼容语义继续漂移，用户会在两种坏结果之间摇摆而没有回归信号：要么手写 import 被静默保留到后续编译阶段，要么 warning 数量/行号/开关行为与配置不一致。 |
| 建议测试名 | Angelscript.TestModule.Preprocessor.Import.AutomaticWarningRespectsConfig |
| 测试类型 | Scenario |
| 测试文件 | 新建 Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportModeTests.cpp |
| 场景描述 | 用 table-driven 两个子场景覆盖同一对 fixture 文件。provider：int SharedValue() { return 11; }。consumer：import Tests.Preprocessor.Shared; int Entry() { return SharedValue(); }。子场景 A 打开 WarnOnManualImportStatements；子场景 B 关闭该 setting；两者都保持 automatic import 为当前引擎默认开启状态。 |
| 输入/前置 | 使用 clean engine + FAngelscriptEngineScope；通过 FAngelscriptTestFixture 写入两份脚本；用 GetMutableDefault<UAngelscriptSettings>() 保存并恢复 WarnOnManualImportStatements；每个子场景执行前清空 diagnostics，并显式断言当前 context 下 ShouldUseAutomaticImportMethodForCurrentContext() 为 	rue，避免误跑到 manual path。 |
| 期望行为 | 两个子场景都必须 Preprocess() 成功，并且 consumer 的 processed code 不再包含原始 import Tests.Preprocessor.Shared; 文本。子场景 A：diagnostics 恰好出现 1 条 warning，消息精确包含 Automatic imports are active, import statements will be ignored.，且 Diagnostic.Row 等于 import 所在行。子场景 B：diagnostics 必须为空。两个子场景里 ImportedModules 都应精确只包含 Tests.Preprocessor.Shared，证明 warning 开关只影响诊断，不改变依赖解析结果。 |
| 使用的 Helper | FAngelscriptTestFixture + FAngelscriptEngineScope + GetMutableDefault<UAngelscriptSettings>() + 直接调用 FAngelscriptPreprocessor |
| 优先级 | P1 |
---

## 测试审查 (2026-04-10 01:09)

### 一、现有测试问题

说明：上一段 `2026-04-10 01:05` 的新增条目在 shell 写入时发生 markdown 反引号转义污染，出现不可读控制字符。本段仅做可读修正，不新增新的问题编号。

#### Issue-43（修正版）：`ImportParsing` 背后的 automatic-import warning 合同在当前源码控制流里不可达

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 测试名 | `Angelscript.TestModule.Preprocessor.ImportParsing` |
| 行号范围 | 169-218 |
| 问题描述 | 当前唯一的 `import` 用例只覆盖手动 import happy path；而源码交叉校验显示，`FAngelscriptPreprocessor::Preprocess()` 只有在 `!FAngelscriptEngine::ShouldUseAutomaticImportMethodForCurrentContext()` 时才会进入 `ProcessImports()`（`AngelscriptPreprocessor.cpp:223-229`），但 manual-import warning 与 `bWarnOnManualImportStatements` 的读取却写在 `ProcessImports()` 内部（`AngelscriptPreprocessor.cpp:485-489`）。在稳定的 engine context 下，这意味着 `Automatic imports are active, import statements will be ignored.` 这条 warning/config 分支实际上没有可执行入口。现有测试文件因此不仅没覆盖该合同，而且会让人误以为只要补一个 automatic-mode 用例就能验证 warning 行为。 |
| 影响 | 当前 import 覆盖被高估：`bWarnOnManualImportStatements` 可能长期处于死代码状态而无人发现；后续文档或测试若继续按“automatic mode 会 strip 并 warning”来设计，会不断追逐一个当前实现根本到不了的分支。 |
| 修复建议 | 先明确产品合同，再对实现与测试一起收口：如果 automatic mode 下手写 `import` 的目标是“忽略并按设置决定是否 warning”，就把 strip/warning 逻辑移到 automatic path 真正会执行的位置，再新增 setting on/off 场景测试；如果 automatic mode 下根本不打算处理这类语句，就删除这段不可达 warning 分支，并同步修正文档与覆盖计划，避免继续把它当成现有能力。 |

#### Issue-44（修正版）：`ModuleFunctionInspection` 已拿到精确 `asIScriptModule*`，执行阶段却回退到按模块名二次查找

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 测试名 | `Angelscript.TestModule.Compiler.EndToEnd.ModuleFunctionInspection` |
| 行号范围 | 283-317 |
| 问题描述 | 用例在 283-309 行已经通过 `AngelscriptTestSupport::BuildModule(...)` 拿到了当前编译产物的精确 `asIScriptModule* Module`，并在这份模块上检查了 `SumWithDefault`；但到了执行断言，代码又改成 `AngelscriptTestSupport::ExecuteIntFunction(&Engine, TEXT("CompilerModuleFunctionInspection"), TEXT("int Entry()"), Result)`（311-317 行）。这个 helper 会在 `AngelscriptTestEngineHelper.cpp:367-391` 里按模块名重新查 `ModuleDesc`，再按声明重新找函数，而不是执行刚才 inspection 过的那份 `Module`。仓库里其实已经有 `AngelscriptTestSupport::ExecuteIntFunction(*this, Engine, asIScriptFunction&, OutValue)` 这种“直接执行精确函数指针”的 helper（`AngelscriptTestUtilities.h:667-695`）。 |
| 影响 | 当前文件中唯一真正查看 `asIScriptModule` 函数形状的用例，没有把“被检查的模块”和“被执行的字节码”绑定到同一个对象上。只要引擎级模块查找还能返回一个同名模块，执行断言就可能与前面的 inspection 脱节，从而削弱该用例对 module identity/stale lookup 问题的保护。 |
| 修复建议 | 在 `BuildModule()` 返回后，直接从当前 `Module` 上精确获取 `Entry` 的 `asIScriptFunction*`，并改用 `AngelscriptTestSupport::ExecuteIntFunction(*this, Engine, *EntryFunction, Result)` 执行；这样 inspection 与 execution 会绑定到同一份编译产物。若必须保留按模块名执行，也至少要额外断言重新查到的 `ScriptModule` 指针与 `BuildModule()` 返回的 `Module` 完全相同。 |

### 二、需要新增的测试

#### NewTest-100（修正版）：automatic import 模式下的 manual-import warning 必须受 `bWarnOnManualImportStatements` 控制

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` |
| 关联函数 | `FAngelscriptPreprocessor::Preprocess` / `FAngelscriptPreprocessor::ProcessImports` |
| 现有测试覆盖 | 当前目标集只有 manual import 的 happy path，没有任何用例显式切换 `UAngelscriptSettings::bWarnOnManualImportStatements`；因此 warning setting 的 on/off 合同完全没有被自动化锁住。 |
| 风险评估 | 如果 automatic-import 兼容语义继续漂移，用户会在两种坏结果之间摇摆而没有回归信号：要么手写 `import` 被静默保留到后续编译阶段，要么 warning 数量、行号、开关行为与配置不一致。 |
| 建议测试名 | `Angelscript.TestModule.Preprocessor.Import.AutomaticWarningRespectsConfig` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorImportModeTests.cpp` |
| 场景描述 | 用 table-driven 两个子场景覆盖同一对 fixture 文件。provider：`int SharedValue() { return 11; }`。consumer：`import Tests.Preprocessor.Shared; int Entry() { return SharedValue(); }`。子场景 A 打开 `bWarnOnManualImportStatements`；子场景 B 关闭该 setting；两者都保持 automatic import 为当前引擎默认开启状态。 |
| 输入/前置 | 使用 clean engine + `FAngelscriptEngineScope`；通过 `FAngelscriptTestFixture` 写入两份脚本；用 `GetMutableDefault<UAngelscriptSettings>()` 保存并恢复 `bWarnOnManualImportStatements`；每个子场景执行前清空 diagnostics，并显式断言当前 context 下 `ShouldUseAutomaticImportMethodForCurrentContext()` 为 `true`，避免误跑到 manual path。 |
| 期望行为 | 两个子场景都必须 `Preprocess()` 成功，并且 consumer 的 processed code 不再包含原始 `import Tests.Preprocessor.Shared;` 文本。子场景 A：diagnostics 恰好出现 1 条 warning，消息精确包含 `Automatic imports are active, import statements will be ignored.`，且 `Diagnostic.Row` 等于 `import` 所在行。子场景 B：diagnostics 必须为空。两个子场景里 `ImportedModules` 都应精确只包含 `Tests.Preprocessor.Shared`，证明 warning 开关只影响诊断，不改变依赖解析结果。 |
| 使用的 Helper | `FAngelscriptTestFixture` + `FAngelscriptEngineScope` + `GetMutableDefault<UAngelscriptSettings>()` + 直接调用 `FAngelscriptPreprocessor` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-43 |
| WrongHelper | 1 | Issue-44 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
