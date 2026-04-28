# ClassGenerator 测试覆盖缺口

---

## 测试审查 (2026-04-08 13:09)

### 一、现有测试问题

#### Issue-1：`ClassGenerator.EmptyModuleSetup` 复用生产/共享引擎且未回收临时模块，隔离与清理都不足

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp` |
| 测试名 | `Angelscript.TestModule.ClassGenerator.EmptyModuleSetup` |
| 行号范围 | 16-24, 41-56 |
| 问题描述 | `GetEngineForClassGeneratorTests()` 优先返回 `TryGetRunningProductionEngine()`，否则退回 `GetOrCreateSharedCloneEngine()`；测试随后在该引擎上用 `asGM_ALWAYS_CREATE` 新建 `Tests.ClassGenerator.EmptyModule`，但没有 `ON_SCOPE_EXIT` 或 `ASTEST_*_FRESH` 级别的回收逻辑。这样同一个用例既可能直接污染正在运行的生产引擎，也会把临时脚本模块残留在共享 clone engine 中。 |
| 影响 | 用例结果会依赖前序测试或编辑器当前状态，后续测试也可能看到这个残留 module，形成顺序相关的误报绿灯。对 “类生成器是否能在干净环境下正确 setup” 这个目标几乎没有隔离保证。 |
| 修复建议 | 改为 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` 或显式 `AcquireFreshSharedCloneEngine()`；在测试体内增加 `ON_SCOPE_EXIT` 回收 `Tests.ClassGenerator.EmptyModule`；如果必须支持生产引擎路径，也要先快照并在退出时恢复模块状态，避免直接复用 live engine。 |

#### Issue-2：`ClassGenerator.EmptyModuleSetup` 断言只覆盖 reload 标志，几乎没有验证类生成器的实际行为

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp` |
| 测试名 | `Angelscript.TestModule.ClassGenerator.EmptyModuleSetup` |
| 行号范围 | 41-56 |
| 问题描述 | 该文件唯一的用例只断言 `Generator.Setup()` 返回 `SoftReload`，并检查 `WantsFullReload/NeedsFullReload` 为 `false`。它没有验证 `Module->Classes/Structs/Enums/Delegates` 为空时生成器是否保持稳定，也没有验证 `AddModule()` 之后 generator 内部是否真的注册了该 module、是否没有产生任何 `UClass/UFunction/UProperty` 副作用。 |
| 影响 | 这个用例即使通过，也只能说明“空 module 没触发 full reload 标志”；它不能证明 `ClassGenerator` 的空输入处理是正确的，更守不住未来把空 module 错误注册为 class/struct 的回归。作为该目录唯一的 `ClassGeneratorTests` 用例，覆盖价值明显偏低。 |
| 修复建议 | 保留 reload 标志断言的同时，补充 `Module` 级别状态断言：`Classes/Structs/Enums/Delegates` 数量为 0、`ScriptModule` 仍可查询、`FindGeneratedClass`/`FindObject` 不会出现新生成类型。若要保留单文件规模，可把该用例扩成空 module smoke test，并新增专门用例覆盖“空 module + 重复 setup + discard”行为。 |

#### Issue-3：`HotReload.ModuleWatcherQueuesFileChanges` 直接操作内部队列，实际上没有测试 watcher 行为

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.ModuleWatcherQueuesFileChanges` |
| 行号范围 | 49-68, 302-327 |
| 问题描述 | 测试通过 `FAngelscriptHotReloadTestAccess::QueueFileChange()` 直接向 `Engine.FileChangesDetectedForReload` 调 `AddUnique`，然后只断言队列长度从 0 变 1。它没有经过真实 watcher 回调、没有调用 `CheckForHotReload()`，也没有验证 `QueuedFullReloadFiles`、文件名规范化、重复事件合并等任何 watcher contract。当前用例名称在测 “ModuleWatcher”，实际只是在测 `TArray::AddUnique`。 |
| 影响 | 只要内部容器仍是 `AddUnique`，测试就会一直绿；即便真实文件监听链路、相对路径解析或 queue-to-reload 转换已经坏掉，这个用例也发现不了。它会制造“watcher 已覆盖”的假象。 |
| 修复建议 | 改成真正驱动 watcher/hot reload 流程的测试：通过 helper 构造临时脚本文件，触发 `CheckForHotReload(ECompileType::SoftReloadOnly)` 或等价入口，再断言 `FileChangesDetectedForReload`、`QueuedFullReloadFiles` 与 diagnostics 的变化。若当前无法接入真实 watcher，至少把测试名改成 queue helper 行为，并单独新增 watcher integration 用例。 |

#### Issue-4：`AngelscriptScriptClassCreationTests.cpp` 单文件 694 行且混合 8 类场景，已经超出单文件单职责约束

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.*` |
| 行号范围 | 1-694 |
| 问题描述 | 该文件同时承载脚本类编译、world spawn、multi-spawn 状态隔离、Blueprint child、CDO defaults、recompile/class switch、non-actor spawn rejection、rename replacement 八类场景，总长度达到 694 行，明显超出规则里建议的 300-500 行范围。大量 helper 与场景逻辑堆叠在同一文件，使每次修改类生成器相关断言时都要通读整个文件。 |
| 影响 | 测试维护成本高，新增场景时更容易继续把不相干职责塞进同一文件；定位失败用例也更难，审查时容易遗漏断言缺口。当前文件已经出现“热重载类切换”和“脚本类创建 smoke test”耦合在一起的趋势。 |
| 修复建议 | 按职责拆分成至少三个文件：`ScriptClassCreationSmoke`（编译/空类/基础 spawn）、`ScriptClassCreationBlueprintAndDefaults`（Blueprint child/CDO/defaults）、`ScriptClassCreationReloadScenarios`（recompile/rename/non-actor rejection）。共享 helper 保留在 `Shared/`，文件内只保留对应场景的 `ASTEST_*` 用例。 |

#### Issue-5：`HotReload.FullReload.Basic` 没有验证 full reload 的核心语义，只验证了“最新类快照可用”

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.FullReload.Basic` |
| 行号范围 | 207-304 |
| 问题描述 | 用例在 structural change 后只查 `FindGeneratedClass("UFullReloadTarget")`、读取新 `Version/Mana` 属性并实例化新对象。它没有断言 `ClassV2 != ClassV1`，没有检查旧类是否被移出活跃名称，也没有验证旧实例/CDO 是否与新类正确切换。由于 `FindGeneratedClass()` 内部还会自动走 `GetMostUpToDateClass()`，这个测试实际上只观察到了“最新类能否工作”，完全看不到 full reload 的版本替换过程。 |
| 影响 | 即便 full reload 退化成错误的类复用、旧类残留或版本链断裂，只要最新查找还能拿到一个可实例化类，该用例就会继续绿灯。它无法证明 full reload 的关键 contract: 新旧类分离、旧类退役、CDO/默认值切换。 |
| 修复建议 | 补三类断言：1. 保存 `ClassV1` 并在 reload 后显式断言 `ClassV2 != ClassV1`；2. 直接按旧指针检查旧类名称已迁移出 canonical name，且 `FindObject` 不会把旧名解析回旧类；3. 在 reload 前创建旧实例和旧 CDO 快照，reload 后验证新类 CDO/新实例看到新 defaults，而旧对象不会被静默篡改。若继续用 `FindGeneratedClass()`，需要同时补一个不折叠 `GetMostUpToDateClass()` 的 helper。 |

#### Issue-6：`HotReload.PIEStructuralChangeNeedsFullReload` 名称声明的是 PIE 场景，实际只做离线 reload 分析

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.PIEStructuralChangeNeedsFullReload` |
| 行号范围 | 341-407 |
| 问题描述 | 该用例没有创建 test world、没有 spawn actor、没有进入 PIE/BeginPlay，也没有在 live actor 存在时触发 reload。它只是编译 `ScriptV1`，然后调用 `AnalyzeReloadFromMemory()` 看 `ReloadRequirement` 是否为 full reload。测试名称里的 `PIE` 与“结构变更需要 full reload”的运行时 contract 都没有被真正覆盖。 |
| 影响 | 这会给读者和 CI 造成误导，以为“PIE 下结构变更”已经有场景覆盖；实际上它只验证了分析阶段的枚举值，完全不能证明 live world/actor/PIE 环境下会正确拒绝 soft reload，也不能证明现有实例和 world 状态不会被破坏。 |
| 修复建议 | 把当前用例降名为 `AnalyzeStructuralChangeNeedsFullReload`，并新增真正的 PIE scenario：使用 `FActorTestSpawner` 创建 actor 实例与 world，上线 `BeginPlay` 后尝试 `SoftReloadOnly` 编译 structural change，断言 compile result 为 `ErrorNeedFullReload` 或明确 fallback，且原 actor/world 仍保持可用。 |

#### Issue-7：`NativeScriptHotReload` 三个 Phase 用例只追加注释重编译，几乎没有验证热重载正确性

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2A` / `Phase2B` / `Phase2C` |
| 行号范围 | 14-62, 80-256 |
| 问题描述 | `VerifyNativeScriptHotReloadInline()` 对每个脚本只做两步：full compile，然后把源码末尾追加一行注释 `// hot reload verification marker` 再走 `SoftReloadOnly` compile。整个过程既没有引入语义变化，也没有执行任何函数、读取任何属性、比较任何 `UClass/UFunction/UEnum` 状态。通过条件仅仅是 “compile wrapper 返回 handled path”。 |
| 影响 | 这些测试最多能证明“对同一文件再次编译不会立刻报错”，并不能证明 native script 热重载后行为、版本链、反射元数据或旧实例状态是正确的。Phase 名称看起来覆盖了 enum/inheritance/handles/actor lifecycle 等专题，实际上没有任何专题断言。 |
| 修复建议 | 将每个 Phase 中的脚本改成最小但有语义差异的 V1/V2 对：例如函数返回值变化、enum 新增枚举项、继承 override 行为变化、actor lifecycle counter 变化。每个用例至少补一组执行级断言：reload 前后执行函数结果不同、`UClass/UFunction/UEnum` 元数据更新、必要时验证旧实例或旧函数指针的可用性。若必须运行在 production engine，还应补清理和冲突保护。 |

#### Issue-8：`HotReloadPerformanceTests` 没有任何性能阈值，且部分用例把编译错误也当成可接受结果

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.Performance.*` |
| 行号范围 | 42-75, 78-320 |
| 问题描述 | 四个性能用例都只收集 3 个 sample，写入 `metrics.json`，然后断言 compile result 属于某个宽泛集合；没有对 `ReloadSeconds` 做任何上限、基线对比或 percentile 检查。更弱的是 `RenameWindowLatency` 明确接受 `Error`/`ErrorNeedFullReload`，`BurstChurnLatency` 也接受 `ErrorNeedFullReload` 作为 pass。换言之，这些测试即便测到的是失败流程，也照样会在 CI 里通过。 |
| 影响 | 当前 performance suite 无法阻止任何真实的性能回退；它更像“采样脚本”而不是自动化测试。对于 rename-window 与 burst churn，用例甚至不能保证自己测量的是成功路径时延，因此输出的数字和质量门禁都缺乏解释力。 |
| 修复建议 | 先为每个场景定义最低可执行 contract，再补门限断言：例如 `SoftReloadLatency`/`FullReloadLatency` 要求 `Median <= X ms`，并在必要时按 `WITH_PERF_BASELINE` 或配置文件读取门限；`RenameWindowLatency` 只允许测量明确建模成功的 rename flow，不能把普通编译错误记为 pass；`BurstChurnLatency` 应分别校验三步 compile result，再对总时延和每步时延设限。若暂时不做门禁，应把这些用例迁到 benchmark/telemetry 组，而不是自动化 correctness suite。 |

#### Issue-9：`HotReload.FullReload.EnumBasic` 没有验证 enum reload 的实际语义变化

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.FullReload.EnumBasic` |
| 行号范围 | 307-390 |
| 问题描述 | `ScriptV2` 明确新增了 `Gamma = 9`，并把 `UFullReloadEnumTarget.State` 默认值从 `Alpha` 改到 `Gamma`。但测试在 reload 后只断言 `Engine.GetEnum("EFullReloadEnumState")` 仍然有效，以及 carrier class 上还能找到 `State` property；它没有检查 `Gamma` 是否真的出现在枚举元数据里，也没有验证 CDO/新实例上的默认值已经切到 `Gamma`。 |
| 影响 | 只要 enum 没有在 reload 中彻底丢失，这个用例就会通过；即便枚举成员表没更新、默认值仍停在旧值，测试也发现不了。对于 class generator 的 enum reload contract，这个断言强度明显不够。 |
| 修复建议 | 在 reload 前后分别读取 `UEnum` 的枚举名和值，显式断言 `Gamma` 存在且值为 `9`；再创建 `UFullReloadEnumTarget` 的 CDO 或新实例，读取 `State` 属性，确认默认值从 `Alpha` 更新为 `Gamma`。若需要保持单文件规模，可将 enum metadata 断言抽到局部 helper。 |

#### Issue-10：`NativeScriptHotReload` 用例直接依赖生产引擎，测试结果受 live 编辑器状态影响

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2A` / `Phase2B` / `Phase2C` |
| 行号范围 | 14-25, 80-256 |
| 问题描述 | `VerifyNativeScriptHotReloadInline()` 强制通过 `RequireRunningProductionEngine()` 获取生产引擎，然后在该引擎上做 full compile 与 soft reload。虽然每轮有 `Engine.DiscardModule()`，但它仍共享 live engine 的包、module registry、diagnostics 与其他编辑器状态，且没有任何快照/恢复逻辑来保证进入测试前后的生产环境一致。 |
| 影响 | 用例会依赖当前编辑器会话是否已经加载同名模块、是否存在其他脚本资产、以及生产引擎当前的 diagnostics/module 状态。即便断言本身正确，也容易出现环境相关的偶发失败，或者把生产状态污染带给后续测试。 |
| 修复建议 | 若业务上必须跑 production engine，至少增加专用命名空间、状态快照和退出恢复，并在每次编译前清空相关 diagnostics/module 记录。更稳妥的做法是提供 `ASTEST_CREATE_ENGINE_FULL()` 或隔离 clone engine 版本的 native hot reload harness，把这三个 Phase 改成隔离执行。 |

### 二、需要新增的测试

#### NewTest-1：补齐 `UASStruct::GetNewestVersion` 的 full reload 版本链测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASStruct::GetNewestVersion()`；`FAngelscriptClassGenerator::CreateFullReloadStruct()` / `DoFullReloadStruct()` |
| 现有测试覆盖 | `Internals/AngelscriptStructCppOpsTests.cpp` 只检查 `BlueprintType` 元数据；当前 `ClassGenerator/`、`HotReload/`、`AngelscriptNativeScriptHotReloadTests.cpp` 对 struct version chain 为 0 命中 |
| 风险评估 | struct full reload 若没正确接上 `NewerVersion`，旧 struct 指针和新 struct 指针的切换会静默失真，现有 suite 不会报警 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASStruct.GetNewestVersionAfterFullReload` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptStructHotReloadTests.cpp` |
| 场景描述 | 编译 `FReloadedStructV1`，保存 `UASStruct* OldStruct`；对同名 `USTRUCT()` 做 structural change 触发 full reload，拿到 `UASStruct* NewStruct` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()` 编译带 `USTRUCT()` 的 v1/v2 脚本；通过 `FindObject<UScriptStruct>(FAngelscriptEngine::GetPackage(), *Name)` 定位 struct |
| 期望行为 | 断言 `OldStruct != NewStruct`；`Cast<UASStruct>(OldStruct)->GetNewestVersion() == NewStruct`；`NewStruct` 上能找到新增字段；旧 struct 仍可访问但不再是 canonical 最新版本 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH`；`CompileAnnotatedModuleFromMemory`；`FindObject<UScriptStruct>` |
| 优先级 | P0 |

#### NewTest-2：补齐 `UASStruct` 的 `CppStructOps` / `ToString` / `Equals` / `Hash` 暴露测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp` |
| 关联函数 | `UASStruct::PrepareCppStructOps()`；`UASStruct::UpdateScriptType()`；`UASStruct::GetToStringFunction()`；`FASStructOps::Identical()`；`FASStructOps::GetStructTypeHash()` |
| 现有测试覆盖 | 只有 `AngelscriptStructCppOpsTests.cpp` 的 `NotBlueprintTypeByDefault`；没有任何用例验证 `GetToStringFunction`、`STRUCT_IdenticalNative`、hash/equals 行为 |
| 风险评估 | struct 运算符或 `CppStructOps` 断链后，UE 比较/哈希/格式化会退化成错误行为，但现有测试树不会发现 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASStruct.CppStructOpsExposeToStringEqualsAndHash` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptStructHotReloadTests.cpp` |
| 场景描述 | 编译带 `opEquals(const FMyStruct& Other) const`、`uint32 Hash() const`、`FString ToString() const` 的脚本 struct，随后通过 `UASStruct` 与 `UScriptStruct` API 检查 native ops 是否就位 |
| 输入/前置 | `USTRUCT()` 脚本中定义 `Value` 字段与三种方法；编译后获取 `UASStruct* Struct`；准备两份 struct 实例并写入不同值 |
| 期望行为 | 断言 `Cast<UASStruct>(Struct)->GetToStringFunction() != nullptr`；`Struct->GetCppStructOps() != nullptr`；`Struct->StructFlags` 含 `STRUCT_IdenticalNative`；两份相同实例 `CompareScriptStruct` 为 `true`、不同实例为 `false`；`GetStructTypeHash` 返回非 0 且与字段变化一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH`；`CompileAnnotatedModuleFromMemory`；UE `UScriptStruct` API |
| 优先级 | P1 |

#### NewTest-3：补齐 full reload 后 `UASClass` 版本链与 CDO 一致性测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASClass::GetMostUpToDateClass()`；`FAngelscriptClassGenerator::DoFullReloadClass()` |
| 现有测试覆盖 | `HotReload.FullReload.Basic` 只检查最新类快照；没有任何 targeted 测试同时保存旧类指针、旧 CDO 与新类 CDO 来验证替换语义 |
| 风险评估 | 版本链断裂、旧类残留、CDO 默认值切换错误都会在现有 full reload suite 下漏报 |
| 建议测试名 | `Angelscript.TestModule.HotReload.FullReload.VersionChainAndCDOConsistency` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadVersionChainTests.cpp` |
| 场景描述 | 编译 actor class v1，保存 `OldClass`、`OldCDO` 与旧默认值；full reload 到 v2（新增属性并修改默认值）；读取 `NewClass`、`NewCDO` 与新实例 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileModuleWithResult(..., ECompileType::FullReload, ...)`；`FActorTestSpawner` 生成实例 |
| 期望行为 | 断言 `NewClass != OldClass`；`Cast<UASClass>(OldClass)->GetMostUpToDateClass() == NewClass`；`NewCDO` 和新实例读到 v2 默认值与新增属性；`OldCDO` 仍保留 v1 默认值，不会被静默篡改 |
| 使用的 Helper | `CompileModuleWithResult`；`FindGeneratedClass`；`ReadPropertyValue`；`FActorTestSpawner` |
| 优先级 | P0 |

#### NewTest-4：补齐 body-only soft reload 的 CDO 与新旧实例一致性测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::PrepareSoftReload()`；`DoSoftReload()` |
| 现有测试覆盖 | `PropertyPreserved` 只验证旧 actor 的属性值保留；没有验证 soft reload 后 CDO、旧实例、新实例三者的默认值/行为是否一致 |
| 风险评估 | soft reload 若让 CDO、新实例、旧实例看到不同默认值，现有 suite 仍可能绿灯 |
| 建议测试名 | `Angelscript.TestModule.HotReload.SoftReload.CDOAndInstanceConsistency` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadVersionChainTests.cpp` |
| 场景描述 | actor v1 定义 `Counter = 5` 与 `GetValue() { return Counter; }`；创建一个旧实例并把 `Counter` 改成 `42`；body-only soft reload 到 `GetValue() { return Counter + 100; }`；reload 后再生成一个新实例 |
| 输入/前置 | `FActorTestSpawner`；`CompileScriptModule` 或 `CompileModuleWithResult(... SoftReloadOnly ...)`；保存 reload 前 CDO 与旧实例 |
| 期望行为 | 断言 class 指针保持不变；CDO 的 `Counter` 仍为 `5`；reload 后新实例默认值仍为 `5`；旧实例保持 `42`；调用 `GetValue` 时新实例得到 `105`、旧实例得到 `142` |
| 使用的 Helper | `CompileScriptModule`；`CompileModuleWithResult`；`SpawnScriptActor`；`ExecuteGeneratedIntEventOnGameThread`；`ReadPropertyValue` |
| 优先级 | P1 |

#### NewTest-5：补齐 `FAngelscriptClassGenerator` reload delegates 广播测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` |
| 关联函数 | `FOnAngelscriptClassReload`；`FOnAngelscriptStructReload`；`FOnAngelscriptPostReload` |
| 现有测试覆盖 | 全仓 `AngelscriptTest` 对这些 delegate 为 0 命中 |
| 风险评估 | reload delegate 若不广播、广播顺序错误、old/new 指针错误，外部订阅者会静默失效，CI 目前没有任何守卫 |
| 建议测试名 | `Angelscript.TestModule.HotReload.Delegates.BroadcastOldAndNewTypes` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDelegateTests.cpp` |
| 场景描述 | 绑定 `OnClassReload`、`OnStructReload`、`OnPostReload`；编译包含一个 `UCLASS` 与一个 `USTRUCT` 的 module；做一次 full reload |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；编译前保存 delegate handle；reload 后解绑 |
| 期望行为 | 断言 `OnClassReload`、`OnStructReload` 各触发 1 次；回调里的 `Old` 与 `New` 都非空且 `Old != New`；`OnPostReload` 收到一次成功结果；广播顺序晚于新类型可查询时机 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；delegate handle 捕获 |
| 优先级 | P1 |

#### NewTest-6：补齐脚本类“空类”结构矩阵测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FinalizeClass()`；`FinalizeActorClass()` |
| 现有测试覆盖 | `ScriptClassCreation` 已覆盖“带属性”“带函数”“Blueprint child”，但没有真正的空 `UCLASS()` actor/object smoke test |
| 风险评估 | 空类是类生成器最基础的合法输入之一；若空类在 future refactor 中错误地要求属性/函数辅助路径，当前 suite 不会发现 |
| 建议测试名 | `Angelscript.TestModule.ScriptClass.EmptyActorCompilesAndSpawns` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassShapeTests.cpp` |
| 场景描述 | 编译 `UCLASS() class AEmptyScriptActor : AActor {}`；获取 generated class、CDO，并在 test world 中 spawn 一次 |
| 输入/前置 | `AcquireFreshSharedCloneEngine()` 或 `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`FActorTestSpawner` |
| 期望行为 | 断言 generated class 存在且 `IsChildOf(AActor::StaticClass())`；CDO 非空；spawn 成功；没有额外脚本属性时仍能正常进入 `BeginPlay` 与销毁流程 |
| 使用的 Helper | `CompileScriptModule`；`SpawnScriptActor`；`FActorTestSpawner` |
| 优先级 | P2 |

#### NewTest-7：补齐脚本类继承链生成测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` |
| 关联函数 | `SetScriptStaticClass()`；`FinalizeClass()`；`UASClass::GetMostUpToDateClass()` |
| 现有测试覆盖 | 现有 `ScriptClassCreation` 没有任何 Angelscript-to-Angelscript 继承用例；只有 Blueprint child 继承脚本父类 |
| 风险评估 | 父类属性继承、override 绑定、child `IsChildOf` 关系若出错，当前 targeted suite 仍为空白 |
| 建议测试名 | `Angelscript.TestModule.ScriptClass.ScriptInheritancePreservesParentPropertyAndOverride` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassShapeTests.cpp` |
| 场景描述 | 在同一 module 中定义 `ABaseScriptActor` 和 `AChildScriptActor : ABaseScriptActor`；父类带 `ParentValue` 与 `GetValue()`，子类 `BlueprintOverride` 或 script override 改写返回值 |
| 输入/前置 | 编译后分别获取 parent/child class；spawn child actor |
| 期望行为 | 断言 `ChildClass->IsChildOf(ParentClass)`；child 实例能读取继承来的 `ParentValue`；调用 `GetValue` 返回 child override 结果；父类默认值正确流入 child CDO/实例 |
| 使用的 Helper | `CompileScriptModule`；`SpawnScriptActor`；`ReadPropertyValue`；`ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 5 | Issue-5 |
| BadIsolation | 2 | Issue-10 |
| WrongHelper | 2 | Issue-6 |
| AntiPattern | 1 | Issue-4 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | NoTestForSource: 1, MissingScenario: 1 |
| P1 | 4 | NoTestForSource: 2, MissingScenario: 2 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-08 13:21)

### 一、现有测试问题

#### Issue-15：`ScriptClass.CDOHasExpectedDefaults` 没有验证 `bool` 默认值是否真正传播到实例

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.CDOHasExpectedDefaults` |
| 行号范围 | 451-487 |
| 问题描述 | 脚本类声明了 `DefaultCounter`、`bDefaultFlag`、`DefaultLabel` 三个默认值，但测试只在 CDO 上读取并断言 `bDefaultFlag`，实例侧只验证了整数和字符串默认值。也就是说，如果类生成器或 reload 路径把 `bool` 默认值从 CDO 复制到实例这一步弄坏了，当前用例依然会通过。 |
| 影响 | 该用例名和场景都在宣称“CDO defaults 会应用到 actor 实例”，但实际上只覆盖了 `int` 和 `FString` 两种属性类型；`bool` 的实例默认值回归会被误报绿灯。 |
| 修复建议 | 在 `SpawnedActor` 上补一段 `ReadPropertyValue<FBoolProperty>` 读取 `bDefaultFlag`，并增加 `TestTrue` 断言。更稳妥的做法是把三种属性都同时在 CDO 和实例上成对比对，避免只覆盖一半的 default propagation。 |

#### Issue-16：`ScriptClass.RecompileDoesNotCrashClassSwitch` 没有验证“class switch”，只验证了新实例默认值

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.RecompileDoesNotCrashClassSwitch` |
| 行号范围 | 502-577 |
| 问题描述 | 该用例保存了 `InitialClass`，重新编译后拿到 `RecompiledClass`，但断言只比较了旧实例 `GenerationValue == 1` 与新实例 `GenerationValue == 2`，再确认新增属性 `AddedAfterRecompile == 17`。它没有断言 `InitialClass != RecompiledClass`，没有验证 canonical class 查找是否切到了新类，也没有检查旧类/旧实例在 class switch 后处于什么状态。 |
| 影响 | 即使回归把“同名脚本类重编译后创建新类”退化成“原地修改旧类”，当前用例也可能继续通过，因为新实例仍可能读到更新后的默认值。测试标题里的 `ClassSwitch` contract 实际上没有被覆盖。 |
| 修复建议 | 在现有断言外补充版本切换断言：`TestTrue(InitialClass != RecompiledClass)`、`FindGeneratedClass(&Engine, TEXT("AScenarioScriptClassRecompileDoesNotCrashClassSwitch")) == RecompiledClass`，并明确检查旧类是否已退役或 `GetMostUpToDateClass()` 指向新类。若产品语义要求旧实例继续绑定旧类，也应把 `FirstGenerationActor->GetClass()` 的期望写清楚。 |

#### Issue-17：`HotReload.DiscardModule` 没有验证 discard 后旧类型查找是否真的失效

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.DiscardModule` |
| 行号范围 | 186-210 |
| 问题描述 | 用例在 discard 前验证了 `UDiscardableObject` 可见，但 `Engine.DiscardModule("DiscardA")` 后只检查 module record 被移除，并确认另一个 survivor module 还能执行。它没有再次调用 `FindGeneratedClass("UDiscardableObject")`、`FindGeneratedFunction()` 或任何对象创建路径，去验证旧类/旧函数是否真的从运行时查找表里消失。 |
| 影响 | 如果 discard 只删除了 module record，却把旧 `UClass/UFunction` 残留在反射表或 Angelscript lookup 中，当前测试仍会通过。这样无法守住“discard 真正卸载脚本产物”的核心 contract。 |
| 修复建议 | 在 discard 后补充负向断言：`FindGeneratedClass(&Engine, TEXT("UDiscardableObject")) == nullptr`，旧函数查找失败，按旧类名/函数名重新执行应失败；同时保留 survivor module 的正向断言，确保测试既验证删除，又验证旁路模块不受影响。 |

#### Issue-18：`HotReload.DiscardAndRecompile` 通过改类名绕开了同名 recompile 场景

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.DiscardAndRecompile` |
| 行号范围 | 220-296 |
| 问题描述 | 该用例先编译 `UDiscardRecompileTarget`，discard 后却重新编译成另一个公开类名 `UDiscardRecompileTargetV2`。这会绕开“同一模块、同一 public symbol 再次编译”最容易出问题的名字回收、旧反射对象清理和 canonical lookup 更新，只验证了“discard 之后还能再编译一个不同名字的类”。 |
| 影响 | 即便 discard 后旧类名仍残留、同名 recompile 会冲突，当前用例也会保持绿灯，因为它故意换了新类名来避开冲突面。测试标题里的 `Recompile` contract 因此被明显弱化。 |
| 修复建议 | 把 `ScriptV2` 改成继续生成 `UDiscardRecompileTarget`，只修改默认值或函数体；随后断言同名查找返回新类、旧类名不再残留，并验证新实例看到更新后的默认值。若还想保留“改名后可重编译”的场景，应单独拆成 rename/discard 测试，而不是混在 recompile 用例里。 |

#### Issue-19：`HotReload.FailureKeepsOldCodeAndDiagnostics` 只验证旧指针还能跑，没有验证查找表仍指向旧版本

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.FailureKeepsOldCodeAndDiagnostics` |
| 行号范围 | 464-501 |
| 问题描述 | reload 失败后，用例继续使用失败前缓存下来的 `ClassBeforeFailure`、`GetValueBeforeFailure` 和 `TestObject` 来调用旧逻辑，并据此断言“旧代码仍然有效”。但它没有重新执行 `FindGeneratedClass("UHotReloadFailureKeepsOldCode")`、`FindGeneratedFunction("GetValue")` 或 module lookup，去确认运行时查找表没有被失败 reload 部分污染。 |
| 影响 | 如果失败路径错误地替换了 lookup registry，但旧对象和旧函数指针因为仍在内存里而还能执行，当前测试会误报通过。这样会漏掉“新查找拿不到旧代码、但旧指针侥幸还能跑”的半损坏状态。 |
| 修复建议 | 在失败 reload 后重新按名字查类和函数，断言得到的仍是旧版本对象，或至少能稳定执行并返回 `5`；同时补 `Engine.GetModuleByModuleName(ModuleName)` 仍有效、diagnostics 条目只附着在失败文件而没有清空旧 module record 的断言。 |

#### Issue-20：`HotReload.SoftReload.Basic` 实际没有验证生成类方法是否完成了 soft reload

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.SoftReload.Basic` |
| 行号范围 | 89-133 |
| 问题描述 | 用例在 reload 前后都查到了 `USoftReloadTarget::GetVersion`，但没有真正执行该生成方法；它唯一执行的是 module 级全局函数 `GetSoftReloadVersion()`，并以 `1 -> 2` 作为通过条件。这样测试覆盖到的是“脚本 module 的 free function body 被替换”，而不是 “UClass 上的 generated UFunction 已经切到新实现”。 |
| 影响 | 如果 soft reload 只更新了 module 级函数，而对象方法的 thunk/绑定仍指向旧实现，当前测试仍会通过。对于类生成器最关键的 `UClass/UFunction` 热替换路径，这个用例几乎没有保护力。 |
| 修复建议 | 在 reload 前后都创建 `USoftReloadTarget` 实例并执行 `GetVersion`，或在同一实例上分别调用旧/新 `UFunction`，明确断言结果从 `1` 变成 `2`。同时保留 `ClassAfterReload == ClassV1` 的断言，形成“同一类指针 + 新方法实现”的完整 soft reload contract。 |

#### Issue-21：`HotReload.AddProperty` 被归类为 scenario，但没有任何 reload 前的 live actor/world 状态

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AddProperty` |
| 行号范围 | 183-233 |
| 问题描述 | 该用例先编译 `ClassV1`，随后立即执行 full reload，再在 reload 完成后才首次创建 actor。整个过程没有任何 reload 前的 live actor、BeginPlay 状态或 world 内对象，因此它其实只是在 UE scenario harness 里复刻了一个“reload 后新类默认值正确”的静态断言。 |
| 影响 | 如果 structural full reload 会破坏已有 actor、world 注册、旧类退役或 live instance 切换，这个用例发现不了，因为它完全绕开了最容易出问题的“已有对象在场”场景。作为 `Scenario` 组用例，它对运行时行为的覆盖明显不足。 |
| 修复建议 | 把该用例改成真正的 live scenario：在 full reload 前先 spawn `ClassV1` actor 并写入一个旧属性值，reload 后断言新类指针与旧类分离、world 仍可继续运行，再按产品语义检查旧 actor 是否保留/退役，以及新 actor 是否拥有 `NewValue = 99`。如果不打算覆盖 live world，只应把当前逻辑挪到 integration/property test，并重新命名。 |

#### Issue-22：`HotReloadPerformanceTests` 的测量 harness 没有校验基线编译成功，失败样本也会进入统计

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.Performance.*` |
| 行号范围 | 111-115, 162-166, 215-218, 289-296 |
| 问题描述 | 四个性能用例在 `Measure()` 内都直接调用 `CompileAnnotatedModuleFromMemory()` 建 baseline，再调用 `CompileModuleWithResult()` 做 reload，但没有检查这两个 API 的 `bool` 返回值。也就是说，哪怕 baseline compile 根本没成功，或者 reload wrapper 直接返回 `false`，样本依然会带着一个 `ReloadResult` 被写进统计与 `metrics.json`。 |
| 影响 | 当前 performance artifact 可能测到的是“失败路径的报错时间”而不是 reload 时间，而且失败样本会和成功样本混在一起，进一步放大 Issue-8 中“没有阈值”的问题。CI 看到的数字因此缺乏可信度。 |
| 修复建议 | 在每个 `Measure()` 中显式校验 baseline compile 和 reload compile 的 `bool` 返回值；任一步失败时应立即返回带失败标记的样本，并在外层断言中把该样本判为失败而不是继续写 metrics。若要保留失败时延数据，应单独记录 `BaselineCompileSucceeded` / `ReloadCompileSucceeded` 字段，避免把错误路径当成正常 latency。 |

### 二、需要新增的测试

#### NewTest-8：补齐 `UASStruct` 自定义 GUID 稳定性测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp` |
| 关联函数 | `UASStruct::SetGuid(FName)`；`UASStruct::GetCustomGuid() const` |
| 现有测试覆盖 | 当前 `ClassGenerator/`、`HotReload/` 与全仓 `AngelscriptTest` 对 `SetGuid` / `GetCustomGuid` 为 0 命中；已有建议只覆盖 version chain 与 `CppStructOps` |
| 风险评估 | 如果同名脚本 struct 在 full reload 后拿到不同 GUID，或不同 struct 名误碰撞成同一 GUID，依赖 struct identity 的缓存、序列化和反射查找会静默错位，现有 suite 不会报警 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASStruct.CustomGuidStableAcrossSameNameReload` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptStructHotReloadTests.cpp` |
| 场景描述 | 编译 `FStableGuidStruct` 并记录 `GetCustomGuid()`；对同名 struct 做一次 full reload；再额外编译一个不同名字的 `FDifferentGuidStruct` 作对照 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()` / `CompileModuleWithResult(... FullReload ...)`；通过 `FindObject<UScriptStruct>` 获取 `UASStruct*` |
| 期望行为 | 断言第一次与 reload 后的 `FStableGuidStruct` 都是 `UASStruct`、`GetCustomGuid().IsValid()` 为 `true`，且同名 reload 前后 GUID 相同；`FDifferentGuidStruct` 的 GUID 与 `FStableGuidStruct` 不同 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH`；`CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`FindObject<UScriptStruct>` |
| 优先级 | P1 |

#### NewTest-9：补齐 `UASClass` hierarchy helper 的脚本/Blueprint/Native 祖先解析测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::GetFirstASClass(UClass*)`；`UASClass::GetFirstASClass(UObject*)`；`UASClass::GetFirstASOrNativeClass(UClass*)` |
| 现有测试覆盖 | `ScriptClass.BlueprintChildCompiles` 只证明 Blueprint child 能生成和执行；全仓 `AngelscriptTest` 没有任何 targeted 断言验证这些 helper 的返回值 |
| 风险评估 | 这些 helper 被构造、虚函数分派和 hot reload blueprint-child 同步直接依赖；一旦祖先解析回退，Blueprint child、native fallback 与 script override 都可能错读 runtime state，而当前 targeted suite 只能在更晚的崩溃时才被动发现 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.HierarchyHelpersResolveScriptAndNativeAncestors` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassHelperTests.cpp` |
| 场景描述 | 编译 `AScriptHierarchyHelperParent : AActor`，为它创建一个 transient Blueprint child，并分别对脚本父类、Blueprint child class、Blueprint child actor instance、以及纯 native `AActor::StaticClass()` 调用 hierarchy helper |
| 输入/前置 | `AcquireCleanSharedCloneEngine()` 或 `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`CreateTransientBlueprintChild`；`SpawnScriptActor` |
| 期望行为 | 断言 `GetFirstASClass(ScriptParentClass) == ScriptParentClass`；`GetFirstASClass(BlueprintChildClass) == ScriptParentClass`；`GetFirstASClass(BlueprintChildActor) == ScriptParentClass`；`GetFirstASOrNativeClass(BlueprintChildClass) == ScriptParentClass`；`GetFirstASOrNativeClass(AActor::StaticClass()) == AActor::StaticClass()` |
| 使用的 Helper | `CompileScriptModule`；`CreateTransientBlueprintChild`；`CompileAndValidateBlueprint`；`SpawnScriptActor` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 8 | Issue-20 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-09 11:20)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 11:16)` 的 `Issue-56`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 11:18)` 的 `NewTest-43`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-56 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 11:34)

### 一、现有测试问题

本轮末尾校正：本轮未新增现有测试问题；新增测试建议已记录在上文 `## 测试审查 (2026-04-09 11:28)`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 11:28)` 的 `NewTest-44` 与 `NewTest-45`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 23:30)

### 一、现有测试问题

本轮末尾校正：`Issue-67` 与既有 `Issue-20` 重复，`Issue-69` 与既有 `Issue-16` 重复；二者不计入本轮有效新增发现。

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 校正 | 2 | Issue-67 / Issue-69 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-10 01:27)

### 一、现有测试问题

#### Issue-77：`HotReload.Performance.BurstChurnLatency` 把三段不同 reload 合同压成一个总样本，单步回归完全不可定位

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.Performance.BurstChurnLatency` |
| 行号范围 | 240-319 |
| 问题描述 | 该用例在一个 `Measure()` 里连续执行 `SoftReloadOnly -> FullReload -> SoftReloadOnly` 三步 compile，然后只保留一个总耗时 `Elapsed` 和一个人为折叠出来的 `AggregateResult`。最终 artifact 也只写单个 `reload.burst_churn.seconds` 指标。这样 soft reload body-change、structural full reload、以及“结构变更后再次请求 soft reload”的失败/降级路径被混成了一条样本，外层断言根本看不出是哪一步变慢、是哪一步改了 contract。 |
| 影响 | 即使总时长看起来稳定，某一阶段单独回退也会被总耗时掩盖；反过来，某一步骤异常变快也可能把失败路径伪装成“性能提升”。这让 `BurstChurnLatency` 很难作为可执行门禁，更不利于定位问题究竟发生在 soft reload、full reload 还是 deferred full reload 这一步。 |
| 修复建议 | 把三步至少拆成可观察的 step-level 样本：分别记录 `StepOne/StepTwo/StepThree` 的时长与 `CompileResult`，在 artifact 中写成独立字段或三条 metric；外层断言也应逐步校验 `StepOne` 是成功 soft reload、`StepTwo` 是成功 full reload、`StepThree` 明确符合当前产品策略。若仍想保留总 burst 指标，可把它降为附加 telemetry，而不是唯一 contract。 |

### 二、需要新增的测试

#### NewTest-77：补齐 failed reload 不应广播 `OnPostReload` / `OnClassReload` / `OnFullReload` 的事件错误路径测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::OnPostReload`；`FAngelscriptClassGenerator::OnClassReload`；`FAngelscriptClassGenerator::OnFullReload` |
| 现有测试覆盖 | 当前 gap 文档已规划 success-path 的 `OnPostReload` / `OnClassReload` / `OnStructReload` / `OnEnumChanged` 事件语义，但对“compile 失败时这些 delegate 必须完全不广播”仍是 0 命中；仓库测试树里也没有任何一条现有自动化直接把 failed reload 与这些 public delegate 绑定起来 |
| 风险评估 | 如果失败 reload 仍错误广播 post/class/full reload delegate，editor UI、缓存刷新和后处理工具会把一轮未提交的坏脚本当成已经生效，造成状态错乱，而且当前 suite 不会报警 |
| 建议测试名 | `Angelscript.TestModule.HotReload.Events.FailedReloadDoesNotBroadcastReloadDelegates` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadEventTests.cpp` |
| 场景描述 | 先编译 `UFailedReloadEventTarget : UObject` 的 v1，并绑定 test-local listener 记录 `OnPostReload`、`OnClassReload`、`OnFullReload` 的触发次数与参数。随后对同一 module 发起一次 `SoftReloadOnly` broken reload，把返回类型改成 `MissingType`，稳定触发 compile error |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::SoftReloadOnly, ...)`；`FindGeneratedClass()`；test-local `FScopedReloadEventRecorder`，在析构时解绑所有 delegate |
| 期望行为 | 断言 broken reload 返回 `false`，`ReloadResult` 为 `Error` 或 `ErrorNeedFullReload`；`OnPostReloadCount == 0`、`OnClassReloadCount == 0`、`OnFullReloadCount == 0`；同时旧类仍可通过 `FindGeneratedClass()` 查到并保持旧函数行为，证明测试观察到的是“失败后无广播且旧代码仍活着”，而不是整个模块已被清空 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`FindGeneratedClass`；test-local `FScopedReloadEventRecorder` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-77 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-10 01:13)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-76：补齐 `UASClass::IsSafeForRootSet()` 的手动 root/unroot 生命周期契约测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` |
| 关联函数 | `UASClass::IsSafeForRootSet() const` |
| 现有测试覆盖 | 当前 `Plugins/Angelscript/Source/AngelscriptTest` 与 `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` 对 `IsSafeForRootSet` 都是 0 命中；现有 lifecycle 建议已覆盖 `StaticDestructor`、`RuntimeDestroyObject`、`DebugValues`，但还没有任何一条把“脚本生成类允许被 root set 安全持有”锁成公开 contract |
| 风险评估 | `UASClass` 会参与 full reload、reinstance 和工具侧生命周期管理；如果 `IsSafeForRootSet()` 回退成默认 `false`，或脚本类在手动 `AddToRoot()` 后仍不能稳定穿过 GC，依赖 root-set 保护的 class/object 生命周期会在 CI 绿灯下静默失真。虽然这是 P3 契约，但它直接关系到生成类能否安全被外部系统临时持有 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.IsSafeForRootSetSupportsManualRootingAcrossGC` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassLifecycleContractTests.cpp` |
| 场景描述 | 编译最小脚本 `URootSafeTarget : UObject`，获取 `UASClass* ScriptClass`；先断言公开 API `IsSafeForRootSet()` 返回 `true`，再对该 class 显式 `AddToRoot()`，执行一次 `CollectGarbage(RF_NoFlags, true)`，随后验证 canonical lookup 与实例化路径仍指向同一个脚本类；最后 `RemoveFromRoot()` 并确认 rooted 状态恢复 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindGeneratedClass()`；`Cast<UASClass>()`；`CollectGarbage(RF_NoFlags, true)`；必要时 `NewObject<UObject>(GetTransientPackage(), ScriptClass)` 验证 GC 后仍可实例化 |
| 期望行为 | 断言 `ScriptClass != nullptr` 且 `Cast<UASClass>(ScriptClass) != nullptr`；`AsClass->IsSafeForRootSet() == true`；`AddToRoot()` 后 `ScriptClass->IsRooted() == true`；经过一次 GC 后 `FindGeneratedClass(&Engine, TEXT("URootSafeTarget")) == ScriptClass`，并且用同一 class 仍能 `NewObject<UObject>` 成功；`RemoveFromRoot()` 后 `ScriptClass->IsRooted() == false`。这样才能把“公开 API 返回 true”与“真实 rooting 行为可用”绑定成一条测试契约，而不是只测一个内联布尔值 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`FindGeneratedClass`；`Cast<UASClass>`；`CollectGarbage`；`NewObject<UObject>` |
| 优先级 | P3 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 01:28 尾部确认)

### 一、现有测试问题

本轮尾部确认：本次有效新增现有测试问题为上文 `## 测试审查 (2026-04-10 01:24)` 的 `Issue-76` 与 `## 测试审查 (2026-04-10 01:27)` 的 `Issue-77`。

### 二、需要新增的测试

本轮尾部确认：本次有效新增测试建议为上文 `## 测试审查 (2026-04-10 01:27)` 的 `NewTest-77`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-76 |
| AntiPattern | 1 | Issue-77 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-10 01:24)

### 一、现有测试问题

#### Issue-76：`HotReload.FailureKeepsOldCodeAndDiagnostics` 只验证缓存对象仍能跑，失败回滚后新实例/CDO 是否保持旧语义仍是盲区

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.FailureKeepsOldCodeAndDiagnostics` |
| 行号范围 | 464-501 |
| 问题描述 | 用例在 broken reload 之后，只继续对 reload 前缓存下来的 `ClassBeforeFailure` / `GetValueBeforeFailure` / `TestObject` 做一次 `ExecuteGeneratedIntEventOnGameThread()`，验证旧对象还能返回 `5`。它没有在失败后重新创建对象，也没有检查 `ClassBeforeFailure->GetDefaultObject()` 或 fresh `FindGeneratedClass()` 产物上的默认值/函数语义。这样一来，只要失败回滚保住了这一个旧对象和旧 `UFunction*`，即便类默认值、后续新实例或新创建对象已经被半途污染，当前测试仍然会绿灯。 |
| 影响 | soft reload 失败最危险的回归之一不是“旧对象立刻不可调用”，而是“旧对象还活着，但 class/CDO/new instance 已经进入半回滚状态”。当前用例守不住这条 contract，会让失败恢复问题长期滞留到更晚的行为测试才暴露。 |
| 修复建议 | 在现有旧对象断言后，再补一条 fresh-object rollback 检查：失败后重新 `FindGeneratedClass()` / `FindGeneratedFunction()`，创建新的 `UHotReloadFailureKeepsOldCode` 实例并执行 `GetValue()`，要求仍返回 `5`；若脚本后续引入 defaults，也应同步检查 `GetDefaultObject()` 仍保持旧默认值。这样才能把“失败后旧代码继续生效”从缓存指针层面提升到 class/CDO/new instance 层面。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-76 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-10 01:14 位置校正)

### 一、现有测试问题

本轮末尾校正：有效新增现有测试问题为上文 `## 测试审查 (2026-04-10 01:12)` 的 `Issue-75`。该问题来自对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` 的完整文件审查，结论保持不变。

### 二、需要新增的测试

本轮末尾校正：有效新增测试建议为上文 `## 测试审查 (2026-04-10 01:13)` 的 `NewTest-76`。该建议指向当前 gap 文档此前未覆盖的 `UASClass::IsSafeForRootSet()` public API surface，结论保持不变。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-75 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:31)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-70：补齐 `DefaultComponent.Attach` 指向缺失父组件时的 fail-closed 测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::FinalizeActorClass()` |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/Component/AngelscriptComponentScenarioTests.cpp` 只覆盖 `DefaultComponent.Basic` 与 `DefaultComponent.Multiple` 的 happy path；当前 `ClassGenerator_TestGaps.md` 还没有任何一条针对 `Attach parent ... does not exist` 诊断的 targeted 建议 |
| 风险评估 | 如果 attach parent 缺失时没有稳定 fail closed，类生成会在 editor 与 runtime 之间分叉，或者把错误组件树静默发布到运行时；当前测试树只会因为 actor “还能生成”而误报绿灯 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.Component.InvalidAttachParentFailsClosed` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptComponentMetadataValidationTests.cpp` |
| 场景描述 | 编译一个最小 actor：`RootScene` 用 `UPROPERTY(DefaultComponent, RootComponent)` 声明；`Billboard` 用 `UPROPERTY(DefaultComponent, Attach = MissingParent)` 声明，其中 `MissingParent` 在类中不存在 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`FAngelscriptCompileTraceSummary Summary`；`CompileModuleWithSummary(&Engine, ECompileType::FullReload, ModuleName, TEXT("ComponentInvalidAttachParent.as"), Script, true, Summary, true)`；`FindGeneratedClass(&Engine, TEXT("AComponentInvalidAttachParent"))`；`Engine.GetModuleByModuleName(ModuleName.ToString())` |
| 期望行为 | 断言 `bCompiled == false`、`Summary.CompileResult == ECompileResult::Error`、`Summary.Diagnostics` 至少包含一条 `Attach parent MissingParent does not exist for DefaultComponent Billboard.`；同时 `FindGeneratedClass(...) == nullptr`，且 `Engine.GetModuleByModuleName(...)` 无有效 module record，确保失败轮次不会把半有效 actor class 发布到 runtime |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH`；`CompileModuleWithSummary`；`FAngelscriptCompileTraceSummary`；`FindGeneratedClass`；`Engine.GetModuleByModuleName` |
| 优先级 | P1 |

#### NewTest-71：补齐重复 `RootComponent` 声明的生成失败测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::FinalizeActorClass()` |
| 现有测试覆盖 | 现有组件场景只验证单 root 与合法 `Attach`；对 `Property ... is RootComponent, but the actor already has root component ...` 这条错误路径仍是 0 命中 |
| 风险评估 | 一旦 duplicate-root 校验回退，actor 组件树会依赖 fallback 构造顺序决定真正 root，生成结果变成不确定行为；当前测试不会主动报错 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.Component.DuplicateRootComponentFailsClosed` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptComponentMetadataValidationTests.cpp` |
| 场景描述 | 编译一个 actor，声明两个 `USceneComponent` 字段：`RootScene` 与 `OtherRoot`，二者都用 `UPROPERTY(DefaultComponent, RootComponent)` 标记 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`FAngelscriptCompileTraceSummary Summary`；`CompileModuleWithSummary(&Engine, ECompileType::FullReload, ModuleName, TEXT("ComponentDuplicateRoot.as"), Script, true, Summary, true)`；`FindGeneratedClass(&Engine, TEXT("AComponentDuplicateRoot"))`；`Engine.GetModuleByModuleName(ModuleName.ToString())` |
| 期望行为 | 断言编译失败，`Summary.Diagnostics` 包含 `Property OtherRoot is RootComponent, but the actor already has root component RootScene.`；`Summary.CompileResult == ECompileResult::Error`；生成类和 module record 都不会被发布。这样可以把 duplicate-root 约束从 editor 日志提升成自动化 contract |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH`；`CompileModuleWithSummary`；`FAngelscriptCompileTraceSummary`；`FindGeneratedClass`；`Engine.GetModuleByModuleName` |
| 优先级 | P1 |

#### NewTest-72：补齐 `OverrideComponent` 指向不存在基类组件时的失败契约测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::FinalizeActorClass()` |
| 现有测试覆盖 | 当前 `Component/AngelscriptComponentScenarioTests.cpp` 没有任何 `OverrideComponent` 场景；`ClassGenerator_TestGaps.md` 也尚未把 `OverrideComponent ... could not find component ... in base class to override.` 这条路径具体化成测试建议 |
| 风险评估 | 若 override target 名写错仍被静默接受，派生 actor 会带着错误组件布局继续进入 runtime；这类错误通常只会在更晚的构造或 attach 阶段才暴露，排查成本高 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.Component.MissingOverrideTargetFailsClosed` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptComponentMetadataValidationTests.cpp` |
| 场景描述 | 同一 module 中先声明基类 `ABaseOverrideMissing : AActor`，带 `UPROPERTY(DefaultComponent, RootComponent) UBaseRoot RootScene;`；再声明派生类 `ADerivedOverrideMissing : ABaseOverrideMissing`，并添加 `UPROPERTY(OverrideComponent = MissingScene) UDerivedRoot ReplacementRoot;`，其中 `MissingScene` 不存在于基类组件树 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`FAngelscriptCompileTraceSummary Summary`；`CompileModuleWithSummary(&Engine, ECompileType::FullReload, ModuleName, TEXT("ComponentMissingOverrideTarget.as"), Script, true, Summary, true)`；`FindGeneratedClass(&Engine, TEXT("ADerivedOverrideMissing"))`；`Engine.GetModuleByModuleName(ModuleName.ToString())` |
| 期望行为 | 断言编译失败；`Summary.Diagnostics` 包含 `OverrideComponent ADerivedOverrideMissing::ReplacementRoot could not find component MissingScene in base class to override.`；`FindGeneratedClass(...) == nullptr`；`Engine.GetModuleByModuleName(...)` 不返回 live module record。若实现选择保留基类发布，也应额外断言只有基类可见、派生类未发布，但不能出现“派生类编译成功却 override 无效”的 silent success |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH`；`CompileModuleWithSummary`；`FAngelscriptCompileTraceSummary`；`FindGeneratedClass`；`Engine.GetModuleByModuleName` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingErrorPath: 3 |
 
---

## 测试审查 (2026-04-10 01:00 位置校正)

### 一、现有测试问题

本轮末尾校正：有效新增现有测试问题为上文 `## 测试审查 (2026-04-10 00:58)` 的 `Issue-74`。该问题来自对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` 的完整文件审查，结论保持不变。

### 二、需要新增的测试

本轮末尾校正：有效新增测试建议为上文 `## 测试审查 (2026-04-10 00:58)` 的 `NewTest-75`。该建议指向当前 gap 文档此前未覆盖的 `UASClass::StaticDestructor` public API surface，结论保持不变。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-74 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 01:12)

### 一、现有测试问题

#### Issue-75：`HotReload.AnalyzeReload.FunctionSignatureChanged` 只检查分析结论，没有验证 required-path analyze 仍保持 live module 只读

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AnalyzeReload.FunctionSignatureChanged` |
| 行号范围 | 321-366 |
| 问题描述 | 用例先编译 `UReloadFunctionTarget::int ComputeValue()`，再把分析输入改成 `float ComputeValue(float Scale)`，随后只断言 `ReloadRequirement == FullReloadRequired` 与 full-reload 布尔位。它没有在 `AnalyzeReloadFromMemory()` 之后重新查询 baseline `UClass/UFunction`，也没有执行旧签名 `int ComputeValue()` 来证明 analyze 阶段仍是只读的。若 analyzer 在 required-path 上错误地提前发布了新签名、移除了旧 `UFunction`，或污染了 module record，这条用例仍会继续绿灯。 |
| 影响 | 当前 analysis suite 只在 `NoChange` 和 `SoftReloadRequirement` 两条场景上部分覆盖“只读”语义，针对 `FunctionSignatureChanged` 这种必须 full reload 的高风险路径却没有任何 live-state 观测。结果是 analyzer 即使只在 required-path 上发生副作用回归，也不会被这条测试拦住。 |
| 修复建议 | 在现有断言前保存 baseline `UClass* ClassBeforeAnalyze = FindGeneratedClass(&Engine, TEXT("UReloadFunctionTarget"))` 与旧函数执行结果；`AnalyzeReloadFromMemory()` 之后补两类断言：1. `FindGeneratedClass()` 仍返回同一个 `ClassBeforeAnalyze`，旧签名 `FindGeneratedFunction(ClassBeforeAnalyze, TEXT("ComputeValue"))` 仍存在；2. 如有可执行 helper，继续按旧签名执行并断言返回 `1`，同时确认新签名函数在真正 compile 前尚未 materialize。这样才能把 “required-path analyze 只读” 锁成 contract，而不是只锁枚举值。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-75 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-10 01:00 位置校正)

### 一、现有测试问题

本轮末尾校正：有效新增现有测试问题为上文 `## 测试审查 (2026-04-10 00:58)` 的 `Issue-74`。该问题来自对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` 的完整文件审查，结论保持不变。

### 二、需要新增的测试

本轮末尾校正：有效新增测试建议为上文 `## 测试审查 (2026-04-10 00:58)` 的 `NewTest-75`。该建议指向当前 gap 文档此前未覆盖的 `UASClass::StaticDestructor` public API surface，结论保持不变。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-74 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:58)

### 一、现有测试问题

#### Issue-74：`HotReload.SoftReload.Basic` 对 `GetVersion` 的存在性检查是非阻塞断言，函数即使丢失也可能整例继续绿灯

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.SoftReload.Basic` |
| 行号范围 | 95-96, 124-125 |
| 问题描述 | 用例在 soft reload 前后都写了 `TestNotNull(TEXT("GetVersion should exist ..."), GetVersion...)`，但没有用返回值做短路。后续断言只执行全局函数 `GetSoftReloadVersion()`，完全不使用这两个 `UFunction*`。结果是只要全局函数热更正常，即使 `USoftReloadTarget::GetVersion` 在任一阶段查找失败，这个用例仍会继续执行并可能以通过结束。 |
| 影响 | 该用例表面上像是同时守住了“类方法存在”和“全局函数 body 更新”，实际上前者是无效断言。生成类方法丢失、名字解析回退或 `UASFunction` 重绑失败，都可能被当前测试误报成绿灯。 |
| 修复建议 | 把两处存在性检查改成阻塞式写法：`if (!TestNotNull(...)) { return false; }`。同时在同一用例里创建 `USoftReloadTarget` 实例，分别执行 reload 前后的 `GetVersion()`，把“函数存在”和“函数可执行且语义更新”绑定成同一条失败路径。 |

### 二、需要新增的测试

#### NewTest-75：补齐 `UASClass::StaticDestructor` 的 public-symbol 与生命周期契约测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` |
| 关联函数 | `UASClass::StaticDestructor(const FObjectInitializer&)` |
| 现有测试覆盖 | 对整个 `Plugins/Angelscript/Source/AngelscriptTest` 和当前 gap 文档定向检索，`StaticDestructor` 都是 0 命中；当前源码检索也只命中 `ASClass.h:112` 的公开声明，仓库内没有对应定义或使用点。 |
| 风险评估 | 这是一个已经暴露在 public header 上的生命周期入口，但当前既没有实现可见性，也没有测试说明它是“故意废弃”还是“缺失的销毁 hook”。如果后续有人把它接入 `ClassDestructor` / object teardown，双重销毁、漏清理或符号缺失都会在没有回归护栏的情况下直接落到运行时或链接期。 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.StaticDestructorSymbolAndCleanupContract` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassLifecycleContractTests.cpp` |
| 场景描述 | 在测试 TU 内显式 ODR-use `&UASClass::StaticDestructor`，把 public declaration 变成链接期可见 contract；随后编译一个最小脚本 `UObject` 类，并配一个 test-local native probe 记录析构侧 cleanup 次数。若团队决定保留该 API，就在对象销毁/GC 路径中验证该 hook 与 `RuntimeDestroyObject()` 的职责边界；若团队确认这是死接口，则应先删除 public declaration，再移除本测试。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindGeneratedClass()`；`Cast<UASClass>()`；test-local `FASClassDestructorProbe`；必要时 `CollectGarbage()` 或显式销毁实例触发 teardown 路径 |
| 期望行为 | 第一层 contract 是测试模块必须成功链接，说明 `StaticDestructor` 不再是 declaration-only public API；保留该 API 的实现方案下，再断言 probe 记录到恰好一次析构侧 cleanup，且不会与 `RuntimeDestroyObject()` 形成双重调用或完全漏调。这样才能把“是否真的存在这条 hook”与“存在后如何工作”一起锁住。 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`FindGeneratedClass`；`Cast<UASClass>`；`CollectGarbage`；test-local `FASClassDestructorProbe` |
| 优先级 | P3 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-74 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:40)

### 一、现有测试问题

#### Issue-73：`HotReload.ModuleWatcherQueuesFileChanges` 在 `ASTEST_END_SHARE_FRESH` 之前直接 `return`，破坏了宏配对的显式结构

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.ModuleWatcherQueuesFileChanges` |
| 行号范围 | 302-329 |
| 问题描述 | 用例在 323-327 行直接 `return TestEqual(...)`，导致 329 行的 `ASTEST_END_SHARE_FRESH` 变成不可达代码。`AngelscriptTestMacros.h:74-78` 已明确要求把终结 `return` 放在 `ASTEST_END_*` 之后，以保持生命周期宏的 begin/end 配对在源码里清晰可见；当前写法虽然依赖 RAII 仍能跑通，但把作用域闭合藏成了死代码。 |
| 影响 | 这会让源码结构与测试规范脱节，审查者难以一眼确认生命周期作用域何时结束；后续若有人在 `ASTEST_END_SHARE_FRESH` 前后追加 cleanup 或断言，很容易把逻辑误放到永远不会执行的位置，形成隐蔽死代码。 |
| 修复建议 | 先把最后一次 `TestEqual(...)` 的结果保存到局部变量，例如 `const bool bQueueStayedDeduplicated = TestEqual(...);`，然后执行 `ASTEST_END_SHARE_FRESH`，最后在作用域外 `return bQueueStayedDeduplicated;`。同文件其他 `ASTEST_BEGIN_*` 用例也应保持同样的显式 begin/end 配对写法。 |

本轮校正：上文 `Issue-73` 与前文 `Issue-36` 重复，属于误记，不计入本轮有效新增现有测试问题。

### 二、需要新增的测试

#### NewTest-73：补齐 `ComposeOntoClass` 缺失 target 时 fail-closed 的测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 关联函数 | `UASClass::ComposeOntoClass`；`FAngelscriptClassGenerator::FinalizeClass()`；`FAngelscriptClassDesc::ComposeOntoClass` |
| 现有测试覆盖 | `ClassGenerator_Analysis.md` 已记录 `ComposeOntoClass` 是 silent no-op 且测试树 0 命中，但当前 `Documents/AutoPlans/TestCoverage/ClassGenerator_TestGaps.md` 还没有任何对应 `NewTest-*`。`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/` 与 `HotReload/` 对 `ComposeOntoClass` 仍是完全空白。 |
| 风险评估 | 如果 compose target 写错或缺失，当前生成器会把一个“表面编译成功、实际没有 compose 效果”的类静默发布到 runtime；开发者只能在更晚的行为层排查，CI 不会提前报警。 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ComposeOntoClass.MissingTargetFailsClosed` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptComposeOntoClassTests.cpp` |
| 场景描述 | 参考现有 generator-level helper，为最小 `UComposeOntoProbe : UObject` 构造 prepared `FAngelscriptModuleDesc`，但在进入 `FAngelscriptClassGenerator` 前，test-local helper 直接把唯一 `FAngelscriptClassDesc` 的 `ComposeOntoClass` 改成一个不存在的 target，例如 `UNonexistentComposeHost`。随后走 compile summary 或 generator 执行入口，而不是依赖未知脚本语法。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`FAngelscriptEngineScope`；test-local `BuildPreparedAnnotatedModulesForGenerator()` 或等价 helper，负责生成已填充 `Code`/`ScriptModule`/`Classes` 的 `FAngelscriptModuleDesc`；`FAngelscriptClassGenerator` 或带 diagnostics 的 compile summary helper；`FindGeneratedClass()`；`Engine.GetModuleByModuleName()` |
| 期望行为 | 断言本轮生成稳定失败，diagnostics 至少包含 `ComposeOntoClass` 与 `UNonexistentComposeHost`；`FindGeneratedClass(&Engine, TEXT("UComposeOntoProbe")) == nullptr`；`Engine.GetModuleByModuleName(ModuleName.ToString()).IsValid() == false`。这样才能把“compose target 缺失时必须 fail closed”锁成 contract，而不是继续接受 silent success。 |
| 使用的 Helper | `FAngelscriptClassGenerator`；test-local `BuildPreparedAnnotatedModulesForGenerator()` / compile summary helper；`FindGeneratedClass`；`Engine.GetModuleByModuleName()` |
| 优先级 | P1 |

#### NewTest-74：补齐 `ComposeOntoClass` 命中有效 target 时仍不能静默发布 no-op 类的测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 关联函数 | `UASClass::ComposeOntoClass`；`FAngelscriptClassGenerator::FinalizeClass()`；`FAngelscriptClassDesc::ComposeOntoClass` |
| 现有测试覆盖 | 当前 gap 文档和测试树都只覆盖普通 class generation / hot reload / component layout；对“`ComposeOntoClass` 指向一个真实存在的 class desc 时会发生什么”仍然是 0 命中。 |
| 风险评估 | 这条路径比缺失 target 更隐蔽：生成器会把 `UASClass::ComposeOntoClass` 镜像成非空指针，但当前没有任何 materialization/instantiation 消费它。若没有测试把这条 silent no-op 锁死，开发者会误以为 compose 已生效。 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ComposeOntoClass.ValidTargetDoesNotSilentlyPublishNoOpClass` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptComposeOntoClassTests.cpp` |
| 场景描述 | 用同一个 prepared module 构造两个最小 class desc：`UComposeOntoHost : UObject` 与 `UComposeOntoProjected : UObject`；在进入 generator 前把 `UComposeOntoProjected` 的 `ComposeOntoClass` 设为 `UComposeOntoHost`。当前阶段不要求真正实现 compose，只要求它不能以“成功发布一个普通类 + 把 `ComposeOntoClass` 填上指针”的 silent no-op 方式通过。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`FAngelscriptEngineScope`；test-local prepared-module builder；compile summary / generator 执行 helper；`FindGeneratedClass()`；必要时读取 `UASClass::ComposeOntoClass` 观察是否出现“成功但无语义”的旁路状态 |
| 期望行为 | 在当前“未完成 compose materialization owner”阶段，断言生成必须显式失败并给出 `ComposeOntoClass` unsupported/unsupported-yet 诊断；`FindGeneratedClass(&Engine, TEXT("UComposeOntoProjected")) == nullptr`，不能发布一个表面成功但行为与普通类完全相同的脚本类。若未来真正实现 compose pipeline，再把同一用例改写成正向 materialization contract，但在那之前必须先锁死“不静默成功”。 |
| 使用的 Helper | `FAngelscriptClassGenerator`；test-local prepared-module builder；compile summary helper；`FindGeneratedClass`；`Cast<UASClass>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无有效新增 | 0 | `Issue-73` 为重复误记，见本轮校正 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-09 23:25)

### 一、现有测试问题

#### Issue-69：`ScriptClass.RecompileDoesNotCrashClassSwitch` 名称宣称验证 class switch，实际没有断言新旧类切换语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.RecompileDoesNotCrashClassSwitch` |
| 行号范围 | 491-578 |
| 问题描述 | 用例先编译 `AScenarioScriptClassRecompileDoesNotCrashClassSwitch` 的 V1，再对同名类编译一个新增属性的 V2，然后只断言第一次 spawn 的 actor 读到 `GenerationValue == 1`，第二次 spawn 的 actor 读到 `GenerationValue == 2`、`AddedAfterRecompile == 17`。它没有断言 `RecompiledClass != InitialClass`，没有验证旧类是否迁出 canonical name，也没有检查 `FirstGenerationActor->GetClass()` 在重编译后是否仍保持旧类、或 `FindGeneratedClass()` 是否已经切到最新版本。当前测试真正守住的只是“连续两次 compile + spawn 不崩并能读到新默认值”，不是“class switch 正确发生”。 |
| 影响 | 即便重编译路径错误地复用了旧 `UClass`、版本链断裂，或旧 actor 被静默篡改到新类，只要第二次 spawn 还能拿到一个带新属性的新对象，这个用例仍会通过。它无法证明类生成器最关键的 contract 之一：recompile 后新旧类对象、旧实例和 canonical lookup 的关系保持一致。 |
| 修复建议 | 补三组断言：1. 直接保存并断言 `RecompiledClass != InitialClass`，同时 `FindGeneratedClass(&Engine, TEXT("AScenarioScriptClassRecompileDoesNotCrashClassSwitch")) == RecompiledClass`；2. 在 reload 后检查 `FirstGenerationActor->GetClass() == InitialClass`，确认旧实例没有被静默切到新类；3. 对 `InitialClass` 做对象名或 `GetMostUpToDateClass()` 级别的验证，明确旧类是否进入 replaced/version-chain 状态。若想继续保留“does not crash”定位，可把当前 smoke 断言保留，再单独新增更窄的 class-switch semantic 用例。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-69 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 23:24)

### 一、现有测试问题

#### Issue-68：`AngelscriptHotReloadAnalysisTests.cpp` 七个分析用例都把基线模块留在 shared engine 中，缺少退出清理

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AnalyzeReload.*` |
| 行号范围 | 49-366 |
| 问题描述 | 文件内七个用例都采用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `ASTEST_BEGIN_SHARE_CLEAN`，先用 `CompileAnnotatedModuleFromMemory()` 编译 `ReloadNoChangeMod`、`ReloadPropertyMod`、`ReloadSuperMod` 等基线模块，再调用 `AnalyzeReloadFromMemory()` 做分析；但每个用例都没有 `ON_SCOPE_EXIT`、`Engine.DiscardModule()` 或 `ResetSharedCloneEngine()`。根据 `AngelscriptTestMacros.h:108-113` 与 `AngelscriptTestUtilities.h:378-383`，`SHARE_CLEAN` 只保证入口 reset，不负责退出时回收 active modules，所以这些分析测试都把自己创建的模块残留到了 shared engine 里。 |
| 影响 | 当前文件的隔离依赖“下一条测试再次调用 `AcquireCleanSharedCloneEngine()` 先把上一个模块清掉”。一旦这些用例被单独摘出来执行、在同进程内与额外诊断代码混跑，或后续有人在 `ASTEST_END_SHARE_CLEAN` 之后追加断言，残留 module 就会直接污染后续观察。与此同时，测试本身也完全看不到 `DiscardModule()` 是否还能正常工作，模块回收回归不会在这里第一时间暴露。 |
| 修复建议 | 为每个用例显式回收自己创建的模块，最小做法是在进入测试后声明 `const FName ModuleName(...)` 并用 `ON_SCOPE_EXIT { Engine.DiscardModule(*ModuleName.ToString()); }` 兜底；如果一个用例先后创建多个 module，则逐个 discard。若希望统一治理，可把这类纯分析测试改成 `ASTEST_CREATE_ENGINE_CLONE()` / `ASTEST_BEGIN_CLONE`，让生命周期宏自动在退出时回收模块，而不是把 cleanup 责任推给下一条 `SHARE_CLEAN` 测试。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-68 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:07 续)

### 一、现有测试问题

本轮无进一步新增现有测试问题。

### 二、需要新增的测试

本轮无进一步新增测试建议；`NewTest-69` 已在同一时间戳区块中记录。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:35)

### 一、现有测试问题

本轮末尾校正：有效新增现有测试问题为上文 `## 测试审查 (2026-04-10 00:27)` 的 `Issue-72`。该问题来自对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` 的完整文件审查，结论保持不变。

### 二、需要新增的测试

本轮末尾校正：有效新增测试建议为上文 `## 测试审查 (2026-04-10 00:31)` 的 `NewTest-70`、`NewTest-71`、`NewTest-72`。三条建议分别覆盖 `DefaultComponent.Attach` 缺失父节点、重复 `RootComponent`、`OverrideComponent` 指向不存在基类组件这三条 fail-closed 错误路径，结论保持不变。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-72 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingErrorPath: 3 |

---

## 测试审查 (2026-04-10 01:00 位置校正-尾部)

### 一、现有测试问题

本轮末尾校正：有效新增现有测试问题为上文 `## 测试审查 (2026-04-10 00:58)` 的 `Issue-74`。该问题来自对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` 的完整文件审查，结论保持不变。

### 二、需要新增的测试

本轮末尾校正：有效新增测试建议为上文 `## 测试审查 (2026-04-10 00:58)` 的 `NewTest-75`。该建议指向当前 gap 文档此前未覆盖的 `UASClass::StaticDestructor` public API surface，结论保持不变。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-74 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 01:00 位置校正)

### 一、现有测试问题

本轮末尾校正：有效新增现有测试问题为上文 `## 测试审查 (2026-04-10 00:58)` 的 `Issue-74`。该问题来自对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` 的完整文件审查，结论保持不变。

### 二、需要新增的测试

本轮末尾校正：有效新增测试建议为上文 `## 测试审查 (2026-04-10 00:58)` 的 `NewTest-75`。该建议指向当前 gap 文档此前未覆盖的 `UASClass::StaticDestructor` public API surface，结论保持不变。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-74 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:46 位置校正)

### 一、现有测试问题

本轮无有效新增现有测试问题；上文 `## 测试审查 (2026-04-10 00:40)` 中的 `Issue-73` 与既有 `Issue-36` 重复，已在原段内标注为误记。

### 二、需要新增的测试

本轮有效新增测试建议为上文 `## 测试审查 (2026-04-10 00:40)` 中的 `NewTest-73` 与 `NewTest-74`。两条建议都指向当前 gap 文档此前未覆盖的 `ComposeOntoClass` contract：一条锁 `missing target` 必须 fail-closed，另一条锁 `valid target` 在真正 materialization owner 落地前也不能静默发布 no-op 类。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无有效新增 | 0 | `Issue-73` 为重复误记 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-10 00:27)

### 一、现有测试问题

#### Issue-72：`AngelscriptHotReloadFunctionTests.cpp` 已超出单文件建议规模，并把 6 类不相干契约混在同一文件里

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.ModuleRecordTracking` / `DiscardModule` / `DiscardAndRecompile` / `ModuleWatcherQueuesFileChanges` / `AddModifyLookupFlow` / `FailureKeepsOldCodeAndDiagnostics` |
| 行号范围 | 1-507 |
| 问题描述 | 该文件当前达到 `507` 行，已经越过项目规则建议的 `300-500` 行上限；同时它把 module record 跟踪、module discard、recompile、watcher queue、lookup flow、compile failure fallback 六条不同主题塞进同一文件。前面几轮 gap 已经分别发现了 cleanup、弱断言和 helper 误用问题，但这些问题分布在多个主题之间，说明文件职责已经发散。继续在这个文件里追加 hot reload case，会让失败定位和审查成本持续升高。 |
| 影响 | 当前文件已经接近“热重载杂项桶”：单个回归需要通读 module bookkeeping、watcher queue 和 runtime fallback 三类上下文，降低维护效率，也更容易在改一类契约时误伤另一类断言。对后续覆盖补全来说，这种组织方式会持续放大重复 setup 与遗漏 cleanup 的风险。 |
| 修复建议 | 按契约拆分为至少三个文件：`AngelscriptHotReloadModuleLifecycleTests.cpp`（`ModuleRecordTracking` / `DiscardModule` / `DiscardAndRecompile`）、`AngelscriptHotReloadWatcherTests.cpp`（`ModuleWatcherQueuesFileChanges`）、`AngelscriptHotReloadFailureTests.cpp`（`AddModifyLookupFlow` / `FailureKeepsOldCodeAndDiagnostics` 或再细分 lookup 与 failure）。共享 helper 继续留在 `Shared/`，每个文件控制在 `300-500` 行内，并让测试名与文件职责一一对应。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-72 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-10 00:07)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-69：补齐 `DiscardModule()` 后 `UASStruct` 脚本身份与 `CppStructOps` 缓存清理测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h` |
| 关联函数 | `FAngelscriptEngine::DiscardModule(const TCHAR*)`；`UASStruct::UpdateScriptType()`；`UASStruct::GetToStringFunction() const` |
| 现有测试覆盖 | 当前 `ClassGenerator/HotReload` 主审文件只覆盖 class/function 的 discard 行为；新增落地的 `AngelscriptScriptStructHotReloadTests.cpp` 也只验证 full reload 版本链。对 `DiscardModule()` 中 `ScriptStruct->ScriptType = nullptr; ScriptStruct->UpdateScriptType();` 这条 struct 清理分支，仓库测试仍是 0 命中 |
| 风险评估 | 如果 discard 后 `UASStruct` 仍保留旧 `ScriptType`、`ToString`/`opEquals`/`Hash` 缓存，后续工具层、struct compare/hash、调试展示就可能继续调用已被丢弃 module 的 script function，形成 stale metadata 与运行时悬挂 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASStruct.DiscardModuleClearsScriptTypeAndNativeOps` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptStructHotReloadTests.cpp` |
| 场景描述 | 编译 `USTRUCT() struct FDiscardableStruct`，定义 `UPROPERTY() int Value = 7;`、`bool opEquals(const FDiscardableStruct& Other) const`、`uint32 Hash() const`、`FString ToString() const`；拿到 `UASStruct* Struct` 后先显式 `PrepareCppStructOps()`，再执行 `Engine.DiscardModule(ModuleName)` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindObject<UASStruct>(FAngelscriptEngine::GetPackage(), *StructName)`；`Struct->PrepareCppStructOps()`；`Engine.DiscardModule(ModuleName)` |
| 期望行为 | discard 前断言 `Struct->ScriptType != nullptr`、`Struct->GetCppStructOps() != nullptr`、`Struct->GetToStringFunction() != nullptr`，且 `Struct->StructFlags` 含 `STRUCT_IdenticalNative`；discard 返回 `true` 后，断言 `Engine.GetModuleByModuleName(ModuleName.ToString()).IsValid() == false`、`Struct->ScriptType == nullptr`、`Struct->GetToStringFunction() == nullptr`，并且 `Struct->StructFlags` 不再含 `STRUCT_IdenticalNative`。这样才能锁住 `DiscardModule()` 对缓存 `CppStructOps` 的真实收口，而不是只看 module record 被删 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`FindObject<UASStruct>`；`PrepareCppStructOps()`；`Engine.DiscardModule()` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:01)

### 一、现有测试问题

#### Issue-71：`HotReload.FullReload.Basic` 只检查 reloaded function 存在，不执行它们，守不住 full reload 后的函数重绑语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.FullReload.Basic` |
| 行号范围 | 282-301 |
| 问题描述 | 用例在 full reload 后会查 `GetVersionFunction` 和 `GetManaFunction`，但后续只通过 `VersionProperty` / `ManaProperty` 读取 `ObjV2` 的默认值，没有真正执行这两个 `UFunction`。这意味着只要新类和属性默认值已经 materialize，测试就会通过；即便 `GetVersion` / `GetMana` 的 generated thunk 仍指向 stale bytecode、`RuntimeCallEvent` 绑定错误，或者函数签名存在但运行时调用失败，当前断言也发现不了。 |
| 影响 | 该用例表面上看覆盖了 “full reload 后属性和函数都可用”，实际只守住了属性默认值。类生成器一旦在 full reload 中只重建了 reflected property/CDO，没有把 `UASFunction` 的执行路径同步到新脚本实现，CI 仍会误报绿灯。 |
| 修复建议 | 在保留现有属性断言的同时，补执行级验证：1. 用 `ExecuteGeneratedIntEventOnGameThread()` 在 `ObjV2` 上执行 `GetVersionFunction` 与 `GetManaFunction`，断言分别返回 `2` 和 `5`；2. 若希望同时锁住 CDO 语义，可再对 `ClassV2->GetDefaultObject()` 调同样的函数或直接比较函数返回值与 CDO 属性值一致；3. 如果产品语义要求旧函数壳退役，还应补对 `ClassV1` 上旧函数指针的预期说明，避免只验证“新函数能调用”，不验证旧函数是否正确退出。 |

### 二、需要新增的测试

#### NewTest-68：补齐 `UASFunction::RuntimeCallFunction()` / `DestroyArguments` 对非 POD 引用参数的析构清理测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASFunction::RuntimeCallFunction(UObject*, FFrame&, RESULT_DECL)`；`UASFunction::DestroyArguments`；`UASFunction::FinalizeArguments()` |
| 现有测试覆盖 | 当前 gap 文档已补 `FinalizeArguments()` 的 thunk 选型、`RuntimeCallEvent()` 的 world-context、`UASFunctionNativeThunk()` 的简单桥接，以及 `OptimizedCall_*` 的基础参数传递；但对 `DestroyArguments` 这条只在非 POD 引用参数上触发的析构收口路径，仓库测试树和当前 gap 文档仍是 0 命中 |
| 风险评估 | 如果 `FinalizeArguments()` 没把需要析构的 ref/value-type 参数放进 `DestroyArguments`，或 `RuntimeCallFunction()` 漏掉了析构循环，`ProcessEvent` / BPVM 路径会在每次调用后泄漏临时 script value；反过来若多析构一次，又会把引用参数写回和对象生命周期一起打坏。这类回归不会被当前只测 `int`/`float`/对象指针的用例发现 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASFunction.RuntimeCallFunctionCleansUpDestructibleReferenceArguments` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionArgumentLifetimeTests.cpp` |
| 场景描述 | 编译一个最小脚本 `USTRUCT() struct FTrackedRefArg`，带 `int Value`、默认构造函数和析构函数，并通过全局计数器 `GetTrackedRefArgCtorCount()` / `GetTrackedRefArgDtorCount()` 暴露构造与析构次数；再定义 `UCLASS() class UTrackedRefArgCarrier : UObject`，声明 `UFUNCTION() void Increment(FTrackedRefArg& Arg)`，函数体把 `Arg.Value += 1`。C++ 侧创建 carrier 对象，使用 `ProcessEvent` 或 `UASFunctionNativeThunk` 路径调用 `Increment`，确保走 `RuntimeCallFunction()` 而不是 helper 直连的 `RuntimeCallEvent()` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindGeneratedClass()`；`FindGeneratedFunction()`；`FindObject<UScriptStruct>()`；`FStructOnScope` 或 `Function->InitializeStruct()` 准备参数内存；`FindFProperty<FStructProperty>(Function, TEXT("Arg"))` 写入 `Value = 5`；`ExecuteIntFunction()` 读取 ctor/dtor 计数 |
| 期望行为 | 调用前断言 `Cast<UASFunction>(IncrementFunction) != nullptr`，并且 `Cast<UASFunction>(IncrementFunction)->DestroyArguments.Num() == 1`；`ProcessEvent` 返回后参数里的 `Arg.Value == 6`，证明 ref 写回正确；在销毁参数 struct 之前，`GetTrackedRefArgDtorCount()` 相比调用前正好增加 `1`，只对应 `RuntimeCallFunction()` 里临时 `ArgStack` 值的析构；随后再清理参数内存，dtor 总数再增加 `1`，明确区分 “调用路径内部清理” 与 “测试自己回收参数缓冲区” 两条生命周期 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`FindGeneratedClass`；`FindGeneratedFunction`；`Cast<UASFunction>`；`FStructOnScope` / `InitializeStruct`；`ProcessEvent`；`ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-71 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 23:49)

### 一、现有测试问题

#### Issue-70：`HotReloadAnalysisTests.cpp` 每个用例都重复获取一个未使用的 shared engine，引擎语义被 helper 反模式掩盖

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AnalyzeReload.NoChange` / `PropertyCountChange` / `SuperClassChange` / `SoftReloadRequirement` / `ClassAdded` / `ClassRemoved` / `FunctionSignatureChanged` |
| 行号范围 | 50-51, 95-96, 142-143, 182-183, 233-234, 278-279, 323-324 |
| 问题描述 | 七个用例在函数开头都先写 `FAngelscriptEngine& EngineOwner = ASTEST_CREATE_ENGINE_SHARE();`，紧接着又写 `FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();`，但 `EngineOwner` 后续完全没有被读过。根据 `AngelscriptTestMacros.h:44-49` 与 `Shared/AngelscriptTestUtilities.h:378-383`，`ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 本身就会通过 `GetOrCreateSharedCloneEngine()` 获取并 reset 同一个 shared clone engine。也就是说，这里并不存在“两台引擎对照”，只有一次多余的 shared-engine 获取。 |
| 影响 | 这会把测试真实的隔离模型藏起来，让读代码的人误以为 case 同时依赖一个 owner engine 和一个 clean engine。后续维护者如果按这个假象继续 copy/paste，很容易在分析测试里引入假的双引擎语义；一旦 shared helper 的初始化或持久 scope 行为变化，这段死代码也会平白增加额外 side effect 和排查成本。 |
| 修复建议 | 删除未使用的 `EngineOwner`，每个用例只保留 `FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();`。如果原意是显式表达“先拿 shared engine，再在同一实例上 reset”，就把它写成一个具名 helper 或注释，而不是留一个从不参与断言的引用变量；若未来真的需要比较 reset 前后状态，应拆成两个明确的步骤并补对应断言。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-70 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 11:51)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 11:39)` 的 `Issue-57` 与 `## 测试审查 (2026-04-09 11:41)` 的 `Issue-58`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 11:46)` 的 `NewTest-46`、`NewTest-47` 与 `## 测试审查 (2026-04-09 11:49)` 的 `NewTest-48`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-57 |
| WrongHelper | 1 | Issue-58 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingScenario: 1 |
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |
 

---

## 测试审查 (2026-04-09 11:54)

### 一、现有测试问题

#### Issue-59：`HotReload.FullReload.EnumBasic` 保存了 reload 前枚举描述却完全没有比较新旧元数据对象

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.FullReload.EnumBasic` |
| 行号范围 | 357-386 |
| 问题描述 | 用例在 reload 前显式保存了 `const TSharedPtr<FAngelscriptEnumDesc> EnumBeforeReload = Engine.GetEnum(TEXT("EFullReloadEnumState"));`，但 reload 后只再做一次 `EnumAfterReload.IsValid()`。它没有比较 `EnumAfterReload` 与 `EnumBeforeReload` 的 identity，也没有验证旧 descriptor 是否已从 canonical lookup 中退场。换言之，测试把“full reload 后枚举元数据仍存在”当成了充分条件，却完全没有锁住元数据替换/版本切换语义。 |
| 影响 | 如果 full reload 错误地复用了旧 enum descriptor、让 lookup 继续指向 stale metadata，或者在旧新 descriptor 并存时只侥幸保持 `IsValid()`，当前用例仍会绿灯。这会漏掉“枚举值表更新了部分状态，但 canonical metadata 没有完成替换”的半损坏回归。 |
| 修复建议 | 在现有语义断言外补一组元数据替换检查：保存 reload 前 descriptor 地址或关键字段快照，reload 后断言 `EnumAfterReload != EnumBeforeReload` 或至少其枚举项表/版本字段发生符合 full reload 预期的变化；如果产品语义允许旧 descriptor 存活，则应再补一个 helper 明确验证 `Engine.GetEnum("EFullReloadEnumState")` 指向最新 descriptor，而旧 descriptor 不再挂在 active module 的 canonical entry 上。 |

### 二、需要新增的测试

#### NewTest-49：补齐 `UASStruct::SetCppStructOps()` / `PrepareCppStructOps()` 的自定义 ops 保留测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp` |
| 关联函数 | `UASStruct::SetCppStructOps(ICppStructOps*)`；`UASStruct::PrepareCppStructOps()` |
| 现有测试覆盖 | 当前 gap 文档已规划 `CreateCppStructOps()` 生命周期、`UpdateScriptType()` 能力位和 `GetToStringFunction()` 行为，但全仓 `Plugins/Angelscript/Source/AngelscriptTest` 对公开的 `SetCppStructOps()` 包装器仍是 0 命中 |
| 风险评估 | 如果 `PrepareCppStructOps()` 在已有 `CppStructOps` 时仍错误覆盖外部注入的 ops，struct 的 construct/copy/destruct/equality 行为会在运行时被静默替换，现有 integration suite 很难第一时间定位到 “Prepare 覆盖了自定义 ops” 这层问题 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASStruct.PrepareCppStructOpsPreservesInjectedCustomOps` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASStructCppStructOpsTests.cpp` |
| 场景描述 | 在 test-local C++ 中定义一个最小 `FTestCppStructOps : UScriptStruct::ICppStructOps`，然后创建 transient `UASStruct`，先用 `SetCppStructOps(CustomOps)` 注入自定义 ops，再调用 `PrepareCppStructOps()` 两次 |
| 输入/前置 | `NewObject<UASStruct>(GetTransientPackage())`；test-local `FTestCppStructOps`；必要时在测试末尾手动恢复/释放注入的 ops，避免内存泄漏 |
| 期望行为 | 第一次和第二次 `PrepareCppStructOps()` 之后，`Struct->GetCppStructOps()` 都仍等于 `CustomOps`；不会被新的 `FASStructOps` 覆盖；若同文件顺手补一个对照用例，再对未注入 ops 的 `UASStruct` 调 `PrepareCppStructOps()`，应得到非空默认 ops，确保“空时创建、有值时保留”两个分支都被锁住 |
| 使用的 Helper | `NewObject<UASStruct>`；test-local `FTestCppStructOps` stub；`GetCppStructOps()` |
| 优先级 | P2 |

#### NewTest-50：补齐 `FAngelscriptClassGenerator::PerformSoftReload()` 的直接入口测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::AddModule()`；`Setup()`；`PerformSoftReload()` |
| 现有测试覆盖 | 当前 `HotReload.SoftReload.Basic`、`FunctionChange`、`AddModifyLookupFlow` 都是通过 `CompileModuleWithResult(... SoftReloadOnly ...)` 间接触发 reload；全仓对公开的 `PerformSoftReload()` 直接入口是 0 命中 |
| 风险评估 | 若 compile wrapper 仍能工作，但 generator 的直接 soft reload 入口在 prepared module 路径上漏掉 `PrepareSoftReload()` / `DoSoftReload()` 的某个步骤，现有 suite 会误以为核心 API 仍然安全，直到 editor/tooling 直接调用 generator 时才暴露 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.Generator.PerformSoftReloadAppliesPreparedBodyOnlyModule` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassGeneratorReloadApiTests.cpp` |
| 场景描述 | 先编译 `UGeneratorSoftReloadTarget` 的 v1，保存 `UClass* OldClass` 和 `GetValue()` 的 baseline 返回值；然后用 test-local helper 构造同 module 的 v2 `FAngelscriptModuleDesc`，只修改函数体、不改属性布局；把 prepared module 喂给 `FAngelscriptClassGenerator`，执行 `Setup()` 后直接调用 `PerformSoftReload()` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；复用或新增 `BuildPreparedAnnotatedModuleForGenerator` helper；`FindGeneratedClass()`；`FindGeneratedFunction()`；`NewObject<UObject>`；`ExecuteGeneratedIntEventOnGameThread()` |
| 期望行为 | 断言 `Setup()` 返回 `SoftReload`；`WantsFullReload(Module) == false`、`NeedsFullReload(Module) == false`；`PerformSoftReload()` 后 `FindGeneratedClass()` 仍返回 `OldClass`；新创建对象执行 `GetValue()` 返回 v2 结果；如果保留旧函数指针 contract，再补一条断言确认旧 `UFunction*` 的行为符合预期，而不是只能靠重新查找拿到新结果 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`BuildPreparedAnnotatedModuleForGenerator`；`FAngelscriptClassGenerator`；`ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

#### NewTest-51：补齐 `FAngelscriptClassGenerator::PerformFullReload()` 的直接入口测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` |
| 关联函数 | `FAngelscriptClassGenerator::AddModule()`；`Setup()`；`PerformFullReload()`；`UASClass::GetMostUpToDateClass()` |
| 现有测试覆盖 | 当前 `HotReload.FullReload.Basic` 与 `AddProperty` 都通过 compile wrapper 间接走 full reload；全仓对公开的 `PerformFullReload()` 直接入口同样是 0 命中 |
| 风险评估 | 如果 generator 已正确分析出 full reload，但直接执行入口没有完成 class replacement、CDO 重建或版本链更新，compile wrapper 级用例不一定能把问题定位到 `PerformFullReload()`；tooling 侧直接调用此 API 时会落入无覆盖区 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.Generator.PerformFullReloadReplacesPreparedStructuralModule` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassGeneratorReloadApiTests.cpp` |
| 场景描述 | 先编译 `UGeneratorFullReloadTarget` 的 v1，保存 `UClass* OldClass` 与 `OldCDO`；再构造同名 v2 prepared module，新增一个 `UPROPERTY()` 并更新默认值；将 prepared module 交给 `FAngelscriptClassGenerator`，执行 `Setup()` 后直接调用 `PerformFullReload()` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`BuildPreparedAnnotatedModuleForGenerator`；`FindGeneratedClass()`；`ReadPropertyValue<FIntProperty>()`；`Cast<UASClass>()` |
| 期望行为 | 断言 `Setup()` 给出 `FullReloadRequired` 或明确的结构变更 full reload 级别；`WantsFullReload(Module)` / `NeedsFullReload(Module)` 至少命中 required 语义；`PerformFullReload()` 后 `FindGeneratedClass()` 返回的新类指针不等于 `OldClass`；`Cast<UASClass>(OldClass)->GetMostUpToDateClass() == NewClass`；新 CDO 与新实例读到新增属性和新默认值，而旧 CDO 不会被静默补写 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`BuildPreparedAnnotatedModuleForGenerator`；`FAngelscriptClassGenerator`；`ReadPropertyValue`；`Cast<UASClass>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-59 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 11:39)

### 一、现有测试问题

#### Issue-57：`ScriptClass.BlueprintChildCompiles` 没有验证 spawn 出来的实例确实使用了 Blueprint child class

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.BlueprintChildCompiles` |
| 行号范围 | 375-399 |
| 问题描述 | 用例在拿到 `BlueprintClass` 后，只断言 `BlueprintClass->IsChildOf(ScriptClass)`，然后用 `SpawnScriptActor(..., BlueprintClass)` spawn 实例并检查 `BeginPlayCount == 1`。但脚本父类 `AScenarioScriptClassBlueprintChildCompiles` 自身就定义了同名属性和 `BeginPlay` override；如果 spawn/helper 回退成父脚本类实例，当前断言仍然会通过，因为父类也会把 `BeginPlayCount` 加到 `1`。 |
| 影响 | 该测试无法锁住“Blueprint child 真正作为运行时实例类型参与 spawn”这一 contract。Blueprint 编译后虽然生成了 class，但如果实例化路径错误地退回父脚本类、沿用了陈旧父类产物，现有用例仍可能绿灯，无法发现 Blueprint layer 丢失的回归。 |
| 修复建议 | 在现有断言后补充实例类型检查：`TestEqual(..., Actor->GetClass(), BlueprintClass)`，并显式 `TestTrue(..., Actor->GetClass() != ScriptClass)`；若要更稳妥，再断言 `Actor->GetClass()->ClassGeneratedBy == Blueprint.BlueprintAsset` 或等价 Blueprint-generated metadata，确保运行时实例确实来自 transient Blueprint child 而不是脚本父类。 |

### 二、需要新增的测试

本轮未新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-57 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 12:55)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 12:50)` 的 `Issue-62`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 12:50)` 的 `NewTest-57`、`NewTest-58` 与 `NewTest-59`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-62 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 13:12)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 13:09)` 的 `Issue-63`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 13:09)` 的 `NewTest-60`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-63 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 13:39)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-63：补齐 `UASFunction::bIsNoOp` / `ScriptNoOp` metadata 在 soft reload 前后的同步测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASFunction::bIsNoOp`；`FAngelscriptClassGenerator::FinalizeClass()`；`FAngelscriptClassGenerator::DoSoftReload()` |
| 现有测试覆盖 | 全仓 `Plugins/Angelscript/Source/AngelscriptTest` 与当前 `ClassGenerator_TestGaps.md` 对 `bIsNoOp`、`ScriptNoOp` 都是 0 命中；现有 hot reload 用例只看返回值、类切换或模块结果，没有任何一条直接锁住 generated `UFunction` 的 no-op 元数据 |
| 风险评估 | `bIsNoOp` 既会在初次生成时写进 `UASFunction`/`ScriptNoOp` metadata，也会在 soft reload 时走增量更新；一旦这个标记没有正确设置或清除，tick 能力判定、状态导出以及依赖该 metadata 的 editor/tooling 都会在 CI 绿灯下静默漂移 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASFunction.NoOpFlagAndMetadataTrackSoftReload` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionNoOpTests.cpp` |
| 场景描述 | 基线编译 `UNoopCarrier : UObject`，包含 `UFUNCTION() void DoNothing() {}`、`UFUNCTION() void DoWork() { Counter += 1; }` 与 `UPROPERTY() int Counter = 0;`。先保存 `DoNothing` / `DoWork` 的 generated `UASFunction*` 与 metadata 状态；随后对同一 module 做一次 body-only soft reload，把 `DoNothing()` 改成 `Counter += 2;`，其余签名保持不变 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::SoftReloadOnly, ...)`；`FindGeneratedClass()`；`FindGeneratedFunction()`；`Cast<UASFunction>()`；`NewObject<UObject>`；`ProcessEvent` 或 test-local void-call helper；`ReadPropertyValue` |
| 期望行为 | 基线阶段断言 `Cast<UASFunction>(DoNothingFunction)->bIsNoOp == true` 且 `DoNothingFunction->HasMetaData(TEXT("ScriptNoOp")) == true`；`DoWorkFunction` 则 `bIsNoOp == false` 且不带该 metadata。soft reload 后再次按名字查到的 `DoNothing` function 必须 `bIsNoOp == false`、`HasMetaData(TEXT("ScriptNoOp")) == false`；在新对象上执行 `DoNothing()` 后 `Counter == 2`，证明测试锁住的不是单纯 metadata，而是 metadata 与函数体语义一起完成了更新 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`FindGeneratedClass`；`FindGeneratedFunction`；`Cast<UASFunction>`；`NewObject<UObject>`；`ProcessEvent`/test-local void-call helper；`ReadPropertyValue` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 13:09)

### 一、现有测试问题

#### Issue-63：`HotReload.ModuleRecordTracking` 名称覆盖整个 module record，但断言实际上只触及 class 记录

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.ModuleRecordTracking` |
| 行号范围 | 82-147 |
| 问题描述 | 用例编译 `ModuleA` 和 `ModuleB` 后，只检查 `RecordA/RecordB->Classes.Num()` 与 `GetClass()`。但 `FAngelscriptModuleDesc` 还维护 `Enums`、`Delegates`，引擎侧也有 `GetEnum()` / `GetDelegate()` 与 `ActiveEnumsByName` / `ActiveDelegatesByName` 这两套查找表。当前脚本输入既没有 `UENUM` 也没有 `delegate`，断言更没有触碰这些路径；换言之，这个名为 `ModuleRecordTracking` 的用例实际只覆盖了 “class record tracking”。 |
| 影响 | 如果 module record 在 enum/delegate 维度发生回归，例如 ownership 归属错乱、active map 未同步、discard/reload 后 descriptor 残留或串到别的 module，当前用例仍会全绿。CI 会误以为“module record 跟踪已覆盖”，但真实只守住了 class 这一小块。 |
| 修复建议 | 两种修法任选其一：1. 把用例改名为 `ModuleClassRecordTracking`，明确它只测 class record；2. 更推荐把脚本扩成 class + enum + delegate 的组合 module，并补断言 `RecordA->Enums.Num()`、`RecordA->Delegates.Num()`、`RecordA->GetEnum(...)`、`Engine.GetEnum(..., &FoundInModule)`、`Engine.GetDelegate(..., &FoundInModule)` 都返回正确归属。为避免继续把 `AngelscriptHotReloadFunctionTests.cpp` 做大，最好拆到独立的 `ModuleRecord` 测试文件。 |

### 二、需要新增的测试

#### NewTest-60：补齐 module record 对 enum/delegate 产物的 tracking 与归属测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptEngine::GetModuleByModuleName()`；`FAngelscriptEngine::GetEnum()`；`FAngelscriptEngine::GetDelegate()`；`FAngelscriptModuleDesc::GetEnum()` |
| 现有测试覆盖 | 当前 `HotReload.ModuleRecordTracking` 只验证 `Classes.Num()` / `GetClass()`；`HotReload.FullReload.EnumBasic` 与现有 delegate 广播建议都没有检查 module-owned enum/delegate record 以及 `FoundInModule` 的归属结果。对 `HotReload/` 范围内的 module record enum/delegate tracking 仍是 0 命中。 |
| 风险评估 | 一旦 `ActiveEnumsByName` / `ActiveDelegatesByName` 与 module record 脱节，或 reload/discard 后 descriptor 仍挂在旧 module 上，编辑器查找、reload delegate 订阅者和后续 compile 路径都会读到错误归属；现有 suite 很难第一时间把问题定位到 module bookkeeping。 |
| 建议测试名 | `Angelscript.TestModule.HotReload.ModuleRecordTracking.EnumAndDelegateArtifacts` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadModuleRecordTests.cpp` |
| 场景描述 | 编译 `ModuleTrackedTypesA`，同时声明 `UENUM(BlueprintType) enum class ETrackedModuleAState : uint8 { Alpha, Beta }`、`delegate void FTrackedModuleASignal(int Value);` 和一个最小 `UCLASS() class UTrackedModuleA : UObject { UPROPERTY() ETrackedModuleAState State; }`；再编译 `ModuleTrackedTypesB`，声明另一组不同名字的 enum/delegate/class。随后读取 `RecordA/RecordB` 与 engine-level lookup。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`Engine.GetModuleByModuleName()`；`Engine.GetEnum(Name, &FoundInModule)`；`Engine.GetDelegate(Name, &FoundInModule)` |
| 期望行为 | 断言 `RecordA` 与 `RecordB` 都有效；`RecordA->Classes.Num() == 1`、`RecordA->Enums.Num() == 1`、`RecordA->Delegates.Num() == 1`；`RecordA->GetClass(TEXT("UTrackedModuleA")).IsValid()` 与 `RecordA->GetEnum(TEXT("ETrackedModuleAState")).IsValid()`；`RecordA->Delegates.Num() > 0` 且首个 `DelegateName == TEXT("FTrackedModuleASignal")`。随后 engine-level `GetEnum(TEXT("ETrackedModuleAState"), &FoundInModule)` 与 `GetDelegate(TEXT("FTrackedModuleASignal"), &FoundInModule)` 都返回有效结果，且 `FoundInModule->ModuleName == TEXT("ModuleTrackedTypesA")`；B 模块做同样断言。最后再补负向检查，确认 A 的名字不会误解析到 B module，反之亦然。 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`Engine.GetModuleByModuleName`；`Engine.GetEnum`；`Engine.GetDelegate` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-63 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 12:50)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-57：补齐 `UASFunction` world-context 元数据与 `RuntimeCallEvent` 参数布局测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASFunction::bIsWorldContextGenerated`；`UASFunction::WorldContextIndex`；`UASFunction::WorldContextOffsetInParms`；`UASFunction::RuntimeCallEvent(UObject*, void*)` |
| 现有测试覆盖 | `ClassGenerator_Analysis.md` 已把 `WorldContextOffsetInParms` 标成高风险运行时问题，且 D 维度已指出当前 hot reload/class generator 测试对 `WorldContext` 为 0 命中；现有 gap 文档已补线程安全 thunk 与 `_Validate` 缓存，但还没有任何一条测试专门锁住 world-context 参数布局与运行时赋值 |
| 风险评估 | 如果 `WorldContextOffsetInParms` 继续停留在 `-1`，静态脚本函数走 `RuntimeCallEvent` 时会按无效偏移读取 world context，导致 ambient world context 被错误赋值甚至越界读取；当前 CI 不会在第一时间把问题定位到 `UASFunction` 的 world-context 元数据层 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASFunction.StaticWorldContextRuntimeCallUsesValidParmOffset` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionWorldContextTests.cpp` |
| 场景描述 | 新增一个 test-local native probe helper，例如 `UAngelscriptWorldContextProbeLibrary::MatchesAmbientWorldContext(UObject* Expected)`，内部直接比较 `FAngelscriptEngine::TryGetCurrentWorldContextObject()`。随后编译最小脚本类 `UWorldContextFunctionCarrier : UObject`，声明 `UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject")) static int CheckWorldContext(UObject WorldContextObject, int Value)`，其脚本体调用 probe helper；C++ 侧再通过 `ProcessEvent` 或 `Cast<UASFunction>(Function)->RuntimeCallEvent()` 传入参数 struct 触发静态函数 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；test-local native probe helper；`CompileAnnotatedModuleFromMemory()`；`FindGeneratedClass()`；`FindGeneratedFunction()`；`Cast<UASFunction>()`；`FScopedTestWorldContextScope` 用来在调用前安装一个 `PreviousContext`，方便验证恢复逻辑；本地参数 struct 形如 `{ UObject* WorldContextObject; int32 Value; int32 ReturnValue; }` |
| 期望行为 | 断言 `UASFunction* ScriptFunction` 非空；`ScriptFunction->bIsWorldContextGenerated == false`；`ScriptFunction->WorldContextIndex == 0`；`ScriptFunction->WorldContextOffsetInParms >= 0`，且等于 `FindFProperty<FObjectProperty>(Function, TEXT("WorldContextObject"))->GetOffset_ForUFunction()`。调用静态函数时传入 `ExpectedContext` 与 `Value = 7`，返回值必须为 `7`，证明运行时拿到的 ambient world context 与参数 struct 中的 `WorldContextObject` 一致；调用结束后 `FAngelscriptEngine::GetAmbientWorldContext()` 仍恢复为 `PreviousContext`，避免只测成功赋值而不测恢复。 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`FindGeneratedClass`；`FindGeneratedFunction`；`Cast<UASFunction>`；`FScopedTestWorldContextScope`；test-local `UAngelscriptWorldContextProbeLibrary`；`ProcessEvent` 或 `RuntimeCallEvent` |
| 优先级 | P0 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 12:50)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-58：补齐 `UASClass::DefaultsFunction` 在 soft reload 删除 defaults 后的清理测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` |
| 关联函数 | `FAngelscriptClassGenerator::PerformSoftReload()`；`UASClass::DefaultsFunction`；`UpdateConstructAndDefaultsFunctions()` |
| 现有测试覆盖 | `ClassGenerator_Analysis.md` 已记录“删除 `__InitDefaults()` 后 `DefaultsFunction` 不会被清空”的实现风险，但当前 gap 文档仍没有任何 targeted test 去直接观察 `UASClass::DefaultsFunction` 的字段变化与新实例默认值行为；现有 `ScriptClass.CDOHasExpectedDefaults`、`HotReload.PropertyPreserved` 都只看 value surface，不看类上缓存的 defaults function 指针 |
| 风险评估 | 如果 soft reload 删除 defaults 语句后仍保留旧 `DefaultsFunction`，后续新对象会继续执行已从源码删除的旧默认值逻辑，形成“源码已删、运行时仍生效”的幽灵 defaults；当前 suite 很难第一时间把问题定位到缓存字段没有清理 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.DefaultsFunctionClearsAfterSoftReloadRemovesOwnDefaults` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassDefaultsFunctionTests.cpp` |
| 场景描述 | 先编译 `UDefaultsReloadTarget : UObject`，含 `UPROPERTY() int Value;` 与 `default Value = 11;`；拿到 `UASClass* ScriptClass`，确认 `DefaultsFunction != nullptr` 且新实例 `Value == 11`。随后构造同名 v2 prepared module，保留 `Value` 属性但删除所有 defaults 语句；把 v2 喂给 `FAngelscriptClassGenerator`，在 `Setup()` 后显式执行 `PerformSoftReload()`，再读取同一个 `UASClass` 的字段和新实例行为 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindGeneratedClass()`；`Cast<UASClass>()`；复用 `NewTest-50` 里建议的 prepared-module helper；`NewObject<UObject>`；`ReadPropertyValue<FIntProperty>` |
| 期望行为 | v1 阶段断言 `ScriptClass->DefaultsFunction != nullptr`、新实例 `Value == 11`；soft reload 后断言 `FindGeneratedClass()` 仍返回同一 `ScriptClass`，且 `ScriptClass->DefaultsFunction == nullptr`；新的 `UDefaultsReloadTarget` 实例 `Value == 0` 或当前无-defaults 语义下的基线值，而不是继续得到 `11`；如需防止旧指针静默残留，可额外断言 soft reload 前保存的 `OldDefaultsFunction` 不再等于类上当前字段。 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；prepared-module helper；`FAngelscriptClassGenerator`；`FindGeneratedClass`；`Cast<UASClass>`；`NewObject<UObject>`；`ReadPropertyValue` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 12:50)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-59：补齐只有带参构造函数的脚本类“失败而不崩溃”测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` |
| 关联函数 | `UpdateConstructAndDefaultsFunctions()`；`UASClass::ConstructFunction`；`UASClass::AllocScriptObject()`；`ReinitializeScriptObject()` |
| 现有测试覆盖 | `ClassGenerator_Analysis.md` 已明确记录“只有带参 ctor 的脚本类会让 `ConstructFunction` 走空指针”的实现风险，但当前 gap 文档还没有任何一条错误路径测试去锁住“应当报错而不是崩溃”的行为；现有 `ScriptClass` / `HotReload` 用例全部只使用默认无参构造类 |
| 风险评估 | 如果没有专门的 regression test，脚本作者一旦声明只带参 constructor 的 `UObject/AActor`，类生成或热重载刷新 `ConstructFunction` 时就可能直接空指针解引用；这类问题通常表现为 editor crash，而不是稳定可诊断的 compile failure |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.ParameterizedConstructorOnlyFailsGracefully` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassConstructorErrorTests.cpp` |
| 场景描述 | 编译最小脚本 `UOnlyParamCtorTarget : UObject`，只声明 `UOnlyParamCtorTarget(int InValue)`，不提供无参 ctor；通过 `CompileModuleWithSummary()` 或等价 helper 捕获 compile result 与 diagnostics，而不是直接走会把进程打崩的裸 compile 路径 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileModuleWithSummary()`；`FAngelscriptCompileTraceSummary`；必要时 `AddExpectedError` 匹配 “does not have a constructor with no arguments” 或团队确定的正式诊断文案；测试末尾确认 module 没有残留到 engine |
| 期望行为 | 断言 compile 返回 `false` 或明确的 error result，而不是崩溃/挂起；diagnostics 中包含“无无参构造函数不可用于生成 UObject wrapper”一类稳定错误；`FindGeneratedClass(&Engine, TEXT("UOnlyParamCtorTarget")) == nullptr`，`Engine.GetModuleByModuleName(ModuleName.ToString()).IsValid() == false`。如果团队后续决定支持参数化 ctor，则应把同一测试改成新的正路径 contract，但无论哪种语义都必须由自动化锁死。 |
| 使用的 Helper | `CompileModuleWithSummary`；`FAngelscriptCompileTraceSummary`；`FindGeneratedClass`；`Engine.GetModuleByModuleName`；`AddExpectedError` |
| 优先级 | P0 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 12:40)

### 一、现有测试问题

本轮末尾校正：`Issue-61` 见上文 `## 测试审查 (2026-04-09 12:36)`。

### 二、需要新增的测试

本轮末尾校正：`NewTest-55` 与 `NewTest-56` 见上文 `## 测试审查 (2026-04-09 12:38)`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-61 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-09 12:39)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 12:36)` 的 `Issue-61`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 12:38)` 的 `NewTest-55` 与 `NewTest-56`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-61 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-09 12:38)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-55：补齐 `IsAngelscriptGenerated(const UFunction*)` 的脚本/原生函数判别测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `IsAngelscriptGenerated(const UFunction*)` |
| 现有测试覆盖 | 全仓 `Plugins/Angelscript/Source/AngelscriptTest` 与当前 `ClassGenerator_TestGaps.md` 对 `IsAngelscriptGenerated` 都是 0 命中；目前只有 `UASFunction` / `UFunction` 的间接行为测试，没有任何 helper 级回归锁住“script function 与 native function 的判别” |
| 风险评估 | 该 helper 被 DebugServer 之类的工具层直接消费；一旦它把 native `UFunction` 误判成脚本函数，或反过来把 `UASFunction` 当成 native，源码导航、调试过滤和 script/native 分流都会在 CI 绿灯下静默跑偏 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASFunction.IsAngelscriptGeneratedDistinguishesScriptAndNativeFunctions` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionIdentityTests.cpp` |
| 场景描述 | 在测试文件里声明一个最小 native `UCLASS()` carrier，带 `UFUNCTION() int NativeValue() const`；同时编译最小脚本 `UScriptGeneratedFunctionCarrier : UObject`，声明 `UFUNCTION() int ScriptValue() { return 17; }`。随后分别取得 script generated function、native function 和 `nullptr` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FindGeneratedFunction()`；test-local native carrier 的 `StaticClass()->FindFunctionByName(TEXT("NativeValue"))` |
| 期望行为 | 断言 `IsAngelscriptGenerated(ScriptFunction) == true`、`IsAngelscriptGenerated(NativeFunction) == false`、`IsAngelscriptGenerated(nullptr) == false`；再补一条 `Cast<UASFunction>(ScriptFunction) != nullptr` 作为交叉验证，防止 helper 偶然对错误类型返回 `true` |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedFunction`；`Cast<UASFunction>` |
| 优先级 | P2 |

#### NewTest-56：补齐 `FScriptStructWildcard` 对 `FInstancedStruct` 底层内存的别名语义测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Helpers.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInstancedStruct.cpp` |
| 关联函数 | `FScriptStructWildcard`；`FAngelscriptInstancedStructHelpers::GetMemory(FInstancedStruct*, const UScriptStruct*)` |
| 现有测试覆盖 | 全仓 `Plugins/Angelscript/Source/AngelscriptTest` 与当前 gap 文档对 `FScriptStructWildcard`、`FInstancedStruct` 和 `GetMemory` 都是 0 命中；`ASStruct` 现有建议已覆盖 version chain、GUID、CppStructOps 和 script identity，但还没有任何一条锁住 wildcard 内存别名 contract |
| 风险评估 | 如果 `FScriptStructWildcard` 不再准确别名 `FInstancedStruct` 的真实 payload，`Get()` / `GetMutable()` 绑定会在脚本侧读到拷贝、旧内存或错误类型的数据；这类回归通常只会在复杂 struct 交互里晚期暴露，当前 suite 完全无感知 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASStruct.ScriptStructWildcardAliasesInstancedStructMemory` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASStructWildcardTests.cpp` |
| 场景描述 | 编译最小脚本 `USTRUCT() struct FWildcardCarrier { UPROPERTY() int Value = 13; }`；在 C++ 测试里用该 `UScriptStruct` 创建 `FStructOnScope` 与 `FInstancedStruct`，然后调用 `FAngelscriptInstancedStructHelpers::GetMemory()` 取得 `FScriptStructWildcard&`，再通过反射属性对 wildcard 引用执行读写 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindObject<UScriptStruct>`；`FStructOnScope`；`FInstancedStruct`；`FindFProperty<FIntProperty>`；`FAngelscriptInstancedStructHelpers::GetMemory()` |
| 期望行为 | 断言 `InstancedStruct.IsValid() == true`、`InstancedStruct.GetScriptStruct() == WildcardCarrierStruct`；通过 `Value` 属性从 `FScriptStructWildcard&` 读取时得到 `13`；把 wildcard 视图中的 `Value` 改为 `27` 后，再从 `InstancedStruct.GetMutableMemory()` 读取同一属性得到 `27`。这能直接锁住 wildcard 视图与底层 instanced payload 是同一块 live memory，而不是副本或哨兵对象 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`FindObject<UScriptStruct>`；`FStructOnScope`；`FindFProperty`；`FAngelscriptInstancedStructHelpers::GetMemory` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-09 11:46)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-46：补齐 `FAngelscriptClassGenerator::CallPostInitFunctions()` 的 literal asset materialization 测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::CallPostInitFunctions()`；`FAngelscriptClassGenerator::PerformReload()`；literal asset 展开生成的 `Get{Name}()` / `__Init_{Name}` |
| 现有测试覆盖 | 当前 `ClassGenerator/`、`HotReload/` 以及全仓 `Plugins/Angelscript/Source/AngelscriptTest` 对 `PostInitFunctions` / `CallPostInitFunctions()` 为 0 命中；现有 `NewTest-16` 只规划 literal asset 的 reload 广播，不覆盖 initial compile 阶段的 post-init 执行 |
| 风险评估 | 如果 post-init 阶段没有真正执行 literal asset getter，或执行次数错误，模块编译仍可能成功，但 asset 会保持未 materialize / 未初始化状态，问题只会在更晚的资源访问时暴露 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.LiteralAsset.PostInitMaterializesAssetOnce` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptLiteralAssetPostInitTests.cpp` |
| 场景描述 | 预处理并编译一个最小模块：脚本包含 `asset ExampleAsset of UObject`、全局计数 `int PostInitCalls = 0`、`void __Init_ExampleAsset(UObject ExampleAsset) { PostInitCalls += 1; }`，以及两个全局函数 `int GetPostInitCalls()` 和 `int HasExampleAsset()`，后者通过 `GetExampleAsset() is null ? 0 : 1` 暴露 asset 是否存在 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileModuleFromMemory()` 或 `CompileModuleWithResult(..., ECompileType::FullReload, ...)`；`ExecuteIntFunction(&Engine, ModuleName, TEXT("int GetPostInitCalls()"), ...)`；`ExecuteIntFunction(&Engine, ModuleName, TEXT("int HasExampleAsset()"), ...)`；必要时再加 `int TouchExampleAssetAgain()`，内部再次访问 `GetExampleAsset()` |
| 期望行为 | 编译成功后立即断言 `GetPostInitCalls() == 1`；`HasExampleAsset() == 1`，证明 `CallPostInitFunctions()` 已在 compile/reload 收尾阶段完成 literal asset materialization；如果再调用一次 `TouchExampleAssetAgain()`，`GetPostInitCalls()` 仍应保持 `1`，确保 post-init getter 没有被重复执行 |
| 使用的 Helper | `CompileModuleFromMemory`；`CompileModuleWithResult`；`ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-47：补齐 `CallPostInitFunctions()` 的短函数名冲突分发测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::CallPostInitFunctions()`；`FAngelscriptModuleDesc::PostInitFunctions` |
| 现有测试覆盖 | 测试树里没有任何场景验证 `PostInitFunctions` 在存在同短名函数时是否仍能命中正确 getter；当前 gap 文档也没有覆盖这条 dispatch 风险 |
| 风险评估 | `CallPostInitFunctions()` 现在只按短函数名扫描 `globalFunctionList`。一旦模块里存在 namespaced 或其他同短名函数，literal asset 初始化可能调用到错误函数，导致 asset 没建好或初始化逻辑跑偏，而 compile 结果仍然显示成功 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.LiteralAsset.PostInitResolvesGeneratedGetterInsteadOfNameCollision` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptLiteralAssetPostInitTests.cpp` |
| 场景描述 | 构造一个含名字冲突的脚本模块：保留 `asset ExampleAsset of UObject` 与正常的 `__Init_ExampleAsset`，再额外声明一个同短名的 `namespace Shadow { UObject GetExampleAsset() { WrongCalls += 1; return nullptr; } }`，并用全局计数 `RightCalls` / `WrongCalls` 记录命中情况 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileModuleFromMemory()`；`ExecuteIntFunction()` 读取 `RightCalls`、`WrongCalls`、`HasExampleAsset()`；如需要可在脚本里额外暴露 `int GetExampleAssetState()` 来统一读取 getter 结果 |
| 期望行为 | 编译成功后断言 `RightCalls == 1`、`WrongCalls == 0`；`HasExampleAsset() == 1`。这能直接锁住 `CallPostInitFunctions()` 必须命中预处理器生成的真实 getter，而不是因为只比对短函数名去执行同名冲突函数 |
| 使用的 Helper | `CompileModuleFromMemory`；`ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 11:49)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-48：补齐 namespaced script class 的生成隔离测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 关联函数 | `FAngelscriptClassGenerator::GetUnrealName()`；`FAngelscriptClassGenerator::GetNamespacedTypeInfoForClass()`；`FAngelscriptEngine::GetClass()` / `ActiveClassesByName` |
| 现有测试覆盖 | 当前 `ClassGenerator/`、`HotReload/`、`Interface/` 与 `Shared/` 测试目录对 namespaced script class 基本是 0 命中；现有 `ScriptClassCreation` 与 hot reload 用例都只覆盖无 namespace 的脚本类型 |
| 风险评估 | 如果不同 namespace 下的同短名脚本类在类生成或 reload 索引阶段发生折叠，运行时会出现类覆盖、查找串线、旧版本链指向错误 head 等问题，而现有 suite 不会报警 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.Namespace.DistinctScriptClassesDoNotCollide` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptNamespacedClassGenerationTests.cpp` |
| 场景描述 | 在同一模块里声明两个 namespaced script class，短名相同但 namespace 不同，例如 `namespace Alpha { UCLASS() class UCollisionTarget : UObject { UFUNCTION() int GetValue() { return 1; } } }` 与 `namespace Beta { UCLASS() class UCollisionTarget : UObject { UFUNCTION() int GetValue() { return 2; } } }`；编译后直接检查 module/class 描述与生成结果 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`Engine.GetModuleByModuleName(ModuleName.ToString())`；遍历 `ModuleDesc->Classes` 按 `Namespace + ClassName` 取出两个 `FAngelscriptClassDesc`；`NewObject<UObject>`；`FindGeneratedFunction()`；`ExecuteGeneratedIntEventOnGameThread()` |
| 期望行为 | 断言模块记录里存在两个 `FAngelscriptClassDesc`，其 `Namespace` 分别为 `Alpha` / `Beta`；两条 descriptor 的 `Class` 指针都非空且彼此不同；分别实例化两个 `Class` 后执行各自 `GetValue()`，返回值应分别为 `1` 与 `2`，证明类生成与运行时调度没有因为相同短名发生覆盖。必要时再补一条断言：module active class 索引不能只剩一个 `UCollisionTarget` |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`Engine.GetModuleByModuleName`；`NewObject<UObject>`；`FindGeneratedFunction`；`ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P0 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 11:41)

### 一、现有测试问题

#### Issue-58：`HotReload.Performance.RenameWindowLatency` 实际没有建模 rename-window，只是在测重复 class 冲突路径

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.Performance.RenameWindowLatency` |
| 行号范围 | 183-236 |
| 问题描述 | 用例名和备注都声称在测“old-file removal + new-file addition”的 rename window，但实际 `Measure()` 只是先编译 `HotReloadPerformanceRenameOld.as`，再对同一个 `ModuleName` 直接编译 `HotReloadPerformanceRenameNew.as`，而且前后脚本都声明同名 `UHotReloadPerformanceRename`。这条路径没有模拟旧文件移除，也没有通过 watcher/preprocessor 让引擎观察到 rename 差异，结果主要落在“同名 class 已存在”的冲突/报错路径上。 |
| 影响 | 当前样本的耗时和 `CompileResult` 不能代表真实 rename-window 行为，只能代表一条 duplicate-class 冲突路径。即便未来真正的 rename 处理链路退化，这个用例也可能继续输出一组“看起来合法”的性能数字，误导对热重载 rename 场景的覆盖判断。 |
| 修复建议 | 把场景改成真正的 rename flow：在临时 `Saved/Automation` 目录先写入旧文件并完成基线编译，然后显式删除旧文件、创建新文件，再通过 `CheckForHotReload()` / 预处理入口 / 文件集重编译入口驱动“移除 + 新增”链路；同时断言 old file 不再参与 module、new file 成为唯一源码来源。若当前 harness 做不到这一点，应把测试名降为 `DuplicateClassConflictLatency`，避免继续以 rename-window 名义记录错误路径。 |

### 二、需要新增的测试

本轮未新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-58 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 12:56)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 12:50)` 的 `Issue-62`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 12:50)` 的 `NewTest-57`、`NewTest-58` 与 `NewTest-59`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-62 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 11:28)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-44：补齐 `UASClass` 脚本继承元数据标记测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASClass::CodeSuperClass`；`UASClass::bHasASClassParent`；`FAngelscriptClassGenerator::FinalizeClass()` |
| 现有测试覆盖 | 当前 `ScriptClass` / `HotReload` 侧只验证 `IsChildOf`、spawn、override 和 defaults；对整个 `Plugins/Angelscript/Source/AngelscriptTest` 检索，`CodeSuperClass` / `bHasASClassParent` 为 0 命中 |
| 风险评估 | 如果脚本继承链的元数据标记错了，构造链、defaults 继承、soft/full reload 父类解析都可能建立在错误的 super 信息上；现有行为型用例即便继续通过，也很难第一时间把问题定位到 `UASClass` 元数据层 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.ScriptInheritanceMetadataTracksCodeSuperAndASParentFlag` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassInheritanceMetadataTests.cpp` |
| 场景描述 | 在同一 module 中编译 `UMetadataParent : UObject` 与 `UMetadataChild : UMetadataParent`，child 额外 override 一个 `UFUNCTION()` 以确保存在真实的 Angelscript-to-Angelscript 继承链；随后分别读取 parent/child 的 `UASClass` 元数据 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindGeneratedClass()`；`Cast<UASClass>()`；必要时用 `ExecuteGeneratedIntEventOnGameThread()` 执行 child override |
| 期望行为 | 断言 `ParentASClass` 与 `ChildASClass` 都有效；`ParentASClass->bHasASClassParent == false`；`ChildASClass->bHasASClassParent == true`；`ParentASClass->CodeSuperClass == UObject::StaticClass()`；`ChildASClass->CodeSuperClass == ParentClass`；`ChildClass->GetSuperClass() == ParentClass`；如果顺手执行 override，则 child 返回值应与 metadata 指向的 script parent 关系一致，避免只测字段不测实际链路 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH`；`CompileAnnotatedModuleFromMemory`；`FindGeneratedClass`；`Cast<UASClass>`；`ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

#### NewTest-45：补齐 `FAngelscriptClassGenerator::AddModule()` 多模块 reload 判定隔离测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::AddModule()`；`FAngelscriptClassGenerator::Setup()`；`WantsFullReload()`；`NeedsFullReload()`；`GetFullReloadLines()` |
| 现有测试覆盖 | `ClassGenerator.EmptyModuleSetup` 是当前唯一直接调用 `AddModule()` / `Setup()` 的测试，但只覆盖单个空 module；`HotReloadAnalysisTests` 与 compile wrapper 场景也都是单 module 观察，当前没有任何 targeted 用例验证 generator 在同一轮同时持有 soft-reload module 与 full-reload module 时的隔离语义 |
| 风险评估 | 如果 `ModuleIndexByName`、`DataRefByName` 或 full-reload line 聚合逻辑在多 module 情况下串线，body-only module 可能被误判成 full reload，或者结构变更行号被归到错误 module；这会直接污染热重载诊断、PIE defer 行为和用户看到的错误定位 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.MultiModuleSetupSeparatesSoftAndFullReloadDecisions` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassGeneratorMultiModuleTests.cpp` |
| 场景描述 | 先分别编译 baseline `BodyOnlyMod` 与 `StructuralMod`；然后为两者准备 v2 module：前者只改函数体返回值，后者新增一个 `UPROPERTY()`。把这两个待 reload 的 `FAngelscriptModuleDesc` 一起喂给同一个 `FAngelscriptClassGenerator`，再调用 `Setup()` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；baseline 用 `CompileAnnotatedModuleFromMemory()`；新增 test-local helper `BuildPreparedAnnotatedModulesForGenerator()`，按 `Shared/AngelscriptTestEngineHelper.cpp` 里 `FAngelscriptPreprocessor` / `BuildModulesForSummary()` 的模式生成已填充 `Code` 与 `ScriptModule` 的 `FAngelscriptModuleDesc`，但不走 engine 的最终 swap-in；`FAngelscriptEngineScope` |
| 期望行为 | 断言整体 `Setup()` 结果为结构变更对应的 full reload 级别；`WantsFullReload(BodyOnlyModule) == false` 且 `NeedsFullReload(BodyOnlyModule) == false`；`WantsFullReload(StructuralModule) == true` 且 `NeedsFullReload(StructuralModule) == true`；`GetFullReloadLines(StructuralModule)` 非空并命中新增 `UPROPERTY()` 行，而 `GetFullReloadLines(BodyOnlyModule)` 为空。若同文件再补一条只读性断言，则 `Setup()` 后 body-only module 的 live baseline 结果仍保持旧值，证明判定阶段没有串改其他 module 的运行态 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；新增 test-local `BuildPreparedAnnotatedModulesForGenerator`；`FAngelscriptClassGenerator`；`GetFullReloadLines` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 11:18)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-43：补齐 `UASFunction` / `UASClass` 在 multi-section module 下的源码定位测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASFunction::GetSourceFilePath() const`；`UASFunction::GetSourceLineNumber() const`；`UASClass::GetSourceFilePath() const`；`UASClass::GetRelativeSourceFilePath() const` |
| 现有测试覆盖 | `Editor/AngelscriptSourceNavigationTests.cpp` 与 `Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` 只验证单文件脚本的正路径；当前 gap 文档虽已规划 `UASClass` 的单文件路径测试，但对同一 module 含多个 `FCodeSection` 的场景仍是 0 覆盖 |
| 风险评估 | 一旦一个 module 拆成多个源码 section，当前公开 metadata API 很容易把类/函数固定导航到 `Code[0]` 或错误行号。开发期 source navigation、报错跳转和 hot reload 排障会因此指向错误文件，而现有 suite 不会报警 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASMetadata.SourceMetadataUsesDeclaringSectionInMultiSectionModule` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASSourceMetadataTests.cpp` |
| 场景描述 | 构造一个带两个 `FCodeSection` 的 module：`Automation/MultiSection_A.as` 只放辅助声明，`Automation/MultiSection_B.as` 声明目标 `UCLASS()` 与 `UFUNCTION()`。编译后分别取得 `UASClass*` 与 `UASFunction*`，验证源码 metadata 指向真正声明它们的第二个 section，而不是首个 section |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；test-local `BuildMultiSectionModuleDesc()` helper，手工向 `FAngelscriptModuleDesc::Code` 追加两个 section；调用 engine compile entry 编译该 module；`FindGeneratedClass()`；`FindGeneratedFunction()` |
| 期望行为 | 断言目标类和函数都成功生成；`Cast<UASFunction>(Function)->GetSourceFilePath()` 等于 `MultiSection_B.as` 的绝对路径，`GetSourceLineNumber()` 等于该函数在第二个 section 内的声明行；若一并校验类级 API，则 `Cast<UASClass>(Class)->GetSourceFilePath()` / `GetRelativeSourceFilePath()` 也应落在 `MultiSection_B.as`，而不是 `MultiSection_A.as` |
| 使用的 Helper | `FAngelscriptModuleDesc` multi-section builder（新增 test-local helper，必要时可从 `Shared/AngelscriptTestEngineHelper.cpp` 的 `MakeModuleDesc()` 派生）；`FindGeneratedClass`；`FindGeneratedFunction`；`Cast<UASClass>`；`Cast<UASFunction>` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 11:16)

### 一、现有测试问题

#### Issue-56：`HotReloadPerformanceTests` 每轮采样后的 `DiscardModule` 完全不做可见断言，warmup 与 measurement 可能互相污染

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.Performance.SoftReloadLatency` / `FullReloadLatency` / `RenameWindowLatency` / `BurstChurnLatency` |
| 行号范围 | 111-117, 162-168, 215-221, 289-305 |
| 问题描述 | 四个 `Measure()` lambda 在样本结束时都直接调用 `Engine.DiscardModule(*ModuleName.ToString())`，随后立刻返回 `ReturnSample`，但从不检查 discard 的 `bool` 返回值，也不验证 `Engine.GetModuleByModuleName(...)` 已经失效。由于 `CollectHotReloadSamples()` 会连续执行 `1` 次 warmup 加 `3` 次 measurement，同一测试进程里任何一次 cleanup 失败都会把上一轮 module 状态带进下一轮采样。 |
| 影响 | 当前性能数字不只会混入错误编译路径，还可能混入“上一轮模块没清干净”的残留状态。这样得到的 `Elapsed` 既不再是独立样本，也会把 warmup 对 measurement 的污染静默吞掉，导致回归数据既不可解释，也不稳定。 |
| 修复建议 | 在每个 `Measure()` 末尾显式保存 `const bool bDiscarded = Engine.DiscardModule(...)`，并在返回样本前至少断言 `bDiscarded == true` 与 `!Engine.GetModuleByModuleName(ModuleName.ToString()).IsValid()`。如果需要把 cleanup 失败也写入 artifact，就给 `FHotReloadPerformanceSample` 增加 `bCleanupSucceeded` 字段，并在外层把失败样本判为红灯，而不是继续当作正常 latency 数据。 |

### 二、需要新增的测试

本轮未新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-56 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 00:52)

### 一、现有测试问题

#### Issue-47：`ScriptClass.CanSpawnInTestWorld` 没有验证 actor 真的进入了 test world，也没锁住 `BeginPlay` 的触发边界

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.CanSpawnInTestWorld` |
| 行号范围 | 241-257 |
| 问题描述 | 用例创建 `FActorTestSpawner` 后只调用 `SpawnScriptActor()`、`BeginPlayActor()`，最后读取 `BeginPlayObserved == 1`。它没有断言 `Actor->GetWorld() == &Spawner.GetWorld()`，也没有在调用 helper 前检查 `BeginPlayObserved == 0` 或 `!Actor->HasActorBegunPlay()`。而 `BeginPlayActor()` helper 自身在 actor 已经 begun play 时会直接跳过 `DispatchBeginPlay()`。这意味着如果 actor 被错误地生成到别的 world，或者 spawn 过程中意外提前触发了 `BeginPlay`，当前测试依然可能通过。 |
| 影响 | 该用例名字声称验证“可以在 test world 中 spawn”，实际上只锁住了“某个 actor 上最终看到 `BeginPlayObserved == 1`”。它无法守住 world 归属和显式 `BeginPlay` 触发时机这两个更关键的 scenario contract。 |
| 修复建议 | 在 spawn 后先补三类断言：1. `Actor->GetWorld() == &Spawner.GetWorld()`；2. `!Actor->HasActorBegunPlay()`；3. 读取 `BeginPlayObserved`，确认 helper 调用前为 `0`。随后再调用 `BeginPlayActor()`，并断言 `Actor->HasActorBegunPlay()` 为 `true`、`BeginPlayObserved == 1`。这样才能把 world 归属与 `BeginPlay` 生命周期一起锁住。 |

#### Issue-48：`HotReload.AnalyzeReload.SoftReloadRequirement` 只看分析结果枚举，没有验证 analyze 过程保持 live module 只读

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AnalyzeReload.SoftReloadRequirement` |
| 行号范围 | 208-225 |
| 问题描述 | 该用例先编译 `UReloadSoftRequirementTarget.GetValue() { return 1; }`，再把分析输入换成 `return 2;` 的 `ScriptV2`，但断言只覆盖 `ReloadRequirement == SoftReload` 和两个 full reload 布尔位。它没有在 `AnalyzeReloadFromMemory()` 之后重新执行旧函数，也没有检查 `FindGeneratedClass()` / `FindGeneratedFunction()` 是否仍指向 baseline 产物。若 analyzer 错误地在分析阶段就把新函数体 swap 进 live module，或者污染了 module record，这个用例仍会绿灯。 |
| 影响 | 这会让 `AnalyzeReloadFromMemory()` 最重要的 contract 之一处于盲区：分析必须是只读的，不应提前改变运行时行为。当前 suite 最多能证明 analyzer 给出了 `SoftReload` 判定，不能证明 live code 在真正 compile 前保持不变。 |
| 修复建议 | 在分析后补一段 baseline 行为回归：实例化 `UReloadSoftRequirementTarget`，执行 `GetValue()`，断言结果仍为 `1`；同时重新查 `FindGeneratedClass()` 和 `FindGeneratedFunction()`，确认对象指针与分析前一致。若 helper 成本允许，再补 `Engine.GetModuleByModuleName()` 里的 class/function 计数不变，明确锁住 analyze-only 的只读语义。 |

#### Issue-49：`NativeScriptHotReload.Phase2A` / `Phase2B` 把多个无关脚本塞进单个 automation test，失败定位和覆盖粒度都过粗

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2A` / `Phase2B` |
| 行号范围 | 27-59, 80-140, 142-237 |
| 问题描述 | `VerifyNativeScriptHotReloadInline()` 会按顺序遍历脚本数组，一旦某个脚本失败就直接 `return false`。但 `Phase2A` 同时承载 enum、inheritance、handle carrier 三个完全不同主题，`Phase2B` 又混合 tag container、system utils、actor lifecycle、namespace math 四个主题。这样 CI 只会告诉你 “Phase2A/2B 失败”，却不能直接看出是哪一个脚本 contract 回归；更严重的是，前一个子场景一旦失败，后续子场景在该轮根本不会执行。 |
| 影响 | 用例粒度过粗会掩盖真实覆盖规模，也会把后续场景静默短路掉。当前文件表面上只有 3 个 Native hot reload 用例，实际上 Phase2A/2B 已经各自塞进 3-4 个不相干能力点，既违反单用例单职责，也让失败 triage 和回归定位明显变差。 |
| 修复建议 | 把每个脚本主题拆成独立 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`，例如 `NativeScriptHotReload.EnumRoundTrip`、`InheritanceOverride`、`HandleCarrier`、`ActorLifecycle` 等；若仍想复用 helper，就让 helper 只负责“单脚本 V1/V2 pair 的 compile + reload harness”。同时将文件按 Phase 或主题拆分，保证单文件仍落在 300-500 行左右。 |

#### Issue-50：`HotReload.AddProperty` 没有验证 full reload 的版本链与 CDO 切换，只证明新类快照可用

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AddProperty` |
| 行号范围 | 195-233 |
| 问题描述 | 该用例保存了 `ClassV1`，随后显式走 `FullReload` 编译得到 `ClassV2`，但断言只覆盖“reload 后能 spawn 新 actor，`ExistingValue == 1`，`NewValue == 99`”。它没有断言 `ClassV2 != ClassV1`，没有检查 `FindGeneratedClass()` 是否已切到新类，也没有比较 old/new CDO。若 full reload 退化成在旧类壳上原地塞入新属性，或者旧 canonical class 仍残留在 active lookup 中，当前测试依旧可能通过。 |
| 影响 | 这条 scenario 用例名义上在覆盖结构性变更后的 full reload，但实际没有锁住版本链替换和 CDO 切换这两个最关键 contract。它只能证明“最新查找结果上的新实例读到了两个属性”，不能证明类生成器真的完成了安全的类替换。 |
| 修复建议 | 在现有断言外补四组检查：1. `ClassV2 != ClassV1`；2. `FindGeneratedClass(&Engine, TEXT("AScenarioHotReloadAddProperty")) == ClassV2`；3. 若产品语义保留版本链，断言 `Cast<UASClass>(ClassV1)->GetMostUpToDateClass() == ClassV2`；4. 同时读取 `ClassV1->GetDefaultObject()` 与 `ClassV2->GetDefaultObject()`，确认旧 CDO 不会被静默补出 `NewValue`，新 CDO 才拥有 `NewValue = 99`。 |

### 二、需要新增的测试

#### NewTest-30：补齐 `UASFunction::FinalizeArguments()` / 专用 thunk 选择的参数分派测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASFunction::FinalizeArguments()`；`UASFunction::RuntimeCallEvent(UObject*, void*)`；`UASFunction_NoParams` / `UASFunction_DWordArg` / `UASFunction_DoubleArg` / `UASFunction_ReferenceArg` / `UASFunction_ObjectReturn` 及对应 `_JIT` 子类 |
| 现有测试覆盖 | 对整个 `Plugins/Angelscript/Source/AngelscriptTest` 定向检索，`FinalizeArguments`、`UASFunction_NoParams`、`UASFunction_DWordArg`、`UASFunction_DoubleArg`、`UASFunction_ReferenceArg`、`UASFunction_ObjectReturn` 全部 0 命中；当前测试树只通过 shared helper 间接调用 `RuntimeCallEvent`，没有任何 targeted 断言验证函数壳形态选择 |
| 风险评估 | 若参数/返回值签名与 `UASFunction_*` thunk 选择错配，运行时 `ProcessEvent` 会在特定签名下读错参数布局或走错 JIT/native 调度，现有 suite 很难在第一时间把问题定位到函数壳分派层 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASFunction.FinalizeArgumentsSelectsExpectedThunkShape` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionDispatchShapeTests.cpp` |
| 场景描述 | 编译一个最小 `UDispatchShapeCarrier : UObject`，声明五个脚本函数：`uint8 NoArgs()`、`int AddOne(int Value)`、`double Scale(double Value)`、`void Bump(int& Value)`、`UObject GetSelf()`。编译后分别查询五个 generated `UFunction`，再用 `ProcessEvent` 或 test-local params struct 调用它们 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FindGeneratedClass()`；`FindGeneratedFunction()`；test-local `CallNoArgs` / `CallAddOne` / `CallScale` / `CallBump` / `CallGetSelf` params struct；一个 `ExpectFunctionClassMatchesAnyOf()` helper，允许 non-JIT 与 JIT 两种合法壳 |
| 期望行为 | 断言 `NoArgs` 的 concrete class 是 `UASFunction_NoParams` 或 `UASFunction_NoParams_JIT`；`AddOne` 是 `UASFunction_DWordArg` 或 `_JIT`；`Scale` 是 `UASFunction_DoubleArg` 或 `_JIT`；`Bump` 是 `UASFunction_ReferenceArg` 或 `_JIT`；`GetSelf` 是 `UASFunction_ObjectReturn` 或 `_JIT`。随后执行每个函数，分别验证返回值/引用写回正确，证明 `FinalizeArguments()` 选出的 thunk 与真实参数布局一致 |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedClass`；`FindGeneratedFunction`；`ProcessEvent`；test-local `ExpectFunctionClassMatchesAnyOf` |
| 优先级 | P1 |

#### NewTest-31：补齐 `OverrideConstructingObject` 对 `GetConstructingASObject()` 的优先级覆盖测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::OverrideConstructingObject`；`UASClass::GetConstructingASObject()` |
| 现有测试覆盖 | 全仓 `AngelscriptTest` 对 `OverrideConstructingObject` 为 0 命中；当前 gap 文档虽已规划 `GetConstructingASObject()` 的普通构造期测试，但还没有任何建议专门验证 override 分支优先于线程上下文分支 |
| 风险评估 | 一旦 override 优先级失效，依赖该钩子的构造桥接或特殊实例化路径会在构造期间读到错误对象，表现为 ctor/defaults 访问串到别的实例，现有自动化不会给出直接定位 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.OverrideConstructingObjectTakesPrecedence` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassConstructionContextTests.cpp` |
| 场景描述 | 复用 `NewTest-20` 里建议的 `Shared/AngelscriptConstructionContextProbe`。先编译一个最小脚本 `UObject`，其构造阶段会调用 probe 记录 `UASClass::GetConstructingASObject()`。第一次实例化时不设置 override，确认 probe 记录到真实脚本实例；第二次实例化前把 `UASClass::OverrideConstructingObject` 临时设成 `OverrideDummy`，再创建对象 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`NewObject<UObject>`；`Shared/AngelscriptConstructionContextProbe`；test-local `FScopedOverrideConstructingObject`，负责保存并恢复旧的 `UASClass::OverrideConstructingObject` |
| 期望行为 | 基线实例化时 probe 记录到真实脚本实例；override 生效时，probe 记录到 `OverrideDummy` 而不是新建实例；scope 退出后 `UASClass::OverrideConstructingObject == nullptr`，再次实例化又恢复记录真实实例。这样可以明确锁住 “override 分支优先于 thread-context 分支，且退出后能恢复” 的 contract |
| 使用的 Helper | `CompileScriptModule`；`NewObject<UObject>`；`Shared/AngelscriptConstructionContextProbe`；test-local `FScopedOverrideConstructingObject` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-50 |
| AntiPattern | 1 | Issue-49 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 02:04)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-42：补齐 `CallInterfaceMethod()` 的生产桥接分发测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `CallInterfaceMethod(asIScriptGeneric*)` |
| 现有测试覆盖 | 当前 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceNativeTests.cpp:23-45` 只定义并绑定了测试私有的 `TestCallInterfaceMethod` 克隆实现；全仓没有任何自动化直接绑定生产函数 `CallInterfaceMethod`，因此 production callback 与测试副本一旦分叉，现有 suite 不会报警 |
| 风险评估 | `CallInterfaceMethod()` 是 native interface 调 script/reflective `UFunction` 的公共桥接点；如果它对 `UserData`、`FindFunction` 或参数转发的处理回退，所有依赖 generic interface dispatch 的调用都可能静默失效，而当前测试会继续因为“测试副本仍正确”而绿灯 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.Interface.CallInterfaceMethodDispatchesToImplementingUFunction` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceDispatchBridgeTests.cpp` |
| 场景描述 | 复用现有 native interface fixture，但绑定时显式使用生产 `CallInterfaceMethod`；编译一个实现 `UAngelscriptNativeParentInterface` 的脚本类，并提供一个脚本 helper 函数通过接口类型调用 `GetNativeValue()` 与 `AdjustNativeValue(int Delta, int& Value)` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`FAngelscriptBinds::ReferenceClass`；`FAngelscriptEngine::RegisterInterfaceMethodSignature()`；`Binds.GenericMethod(..., CallInterfaceMethod, Signature)`；`CompileScriptModule`；`ExecuteIntFunction` 或等价 helper 调用脚本接口桥接函数 |
| 期望行为 | 断言 helper 通过接口调用 `GetNativeValue()` 时返回脚本实现值，例如 `55`；通过接口调用 `AdjustNativeValue(5, Value)` 后脚本返回值为 `15`，证明 ref/out 参数也经过了生产桥接；若再补 `SetNativeMarker(FName)` 场景，则应能在对象属性上观察到 marker 更新。这样才能锁住 `CallInterfaceMethod -> FindFunction -> InvokeReflectiveUFunctionFromGenericCall` 整条真实生产链，而不是继续依赖测试克隆 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH`；现有 native interface test fixture；`FAngelscriptBinds`；`CompileScriptModule`；`ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 01:57)

### 一、现有测试问题

#### Issue-55：`HotReload.PropertyPreserved` 只验证旧实例保值，没有守住 CDO 与新实例的一致性

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.PropertyPreserved` |
| 行号范围 | 103-146 |
| 问题描述 | 用例在 soft reload 前把 live actor 的 `Counter` 改成 `42`，reload 后只验证这个旧实例仍保持 `42`，以及 `GetValue()` 变成 `142`。它没有检查 `ClassAfterReload->GetDefaultObject()` 上的 `Counter` 仍为脚本默认值 `0`，也没有再 spawn 一个新 actor 去验证新实例不会继承旧实例被保留的运行时值。当前断言只能证明“旧实例状态被保留”，不能证明 `PrepareSoftReload()` / `Reinstance` 没有把保留值误写回 CDO 或污染后续实例。 |
| 影响 | 如果 soft reload 把 live actor 的运行时值错误复制进新 CDO，或把 preserved state 泄漏到后续 spawn，新实例行为会悄悄偏离脚本默认值，而这个场景用例仍然会绿灯。对用户最关心的 CDO 一致性和版本切换后默认值稳定性，目前没有任何护栏。 |
| 修复建议 | 在现有断言后补两组检查：1. 读取 `ClassAfterReload->GetDefaultObject()` 的 `Counter`，断言仍为 `0`；2. 用同一 `Spawner` 再 spawn 一个新 actor，断言其 `Counter == 0` 且执行 `GetValue()` 返回 `100`，同时保留旧 actor 的 `142` 断言。这样才能区分“旧实例状态保留”与“新默认值/C DO 未污染”两个 contract。 |

### 二、需要新增的测试

本轮未新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-55 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 02:02)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-41：补齐 `UASStruct::CreateCppStructOps()` 的 construct/copy/destruct 生命周期测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp` |
| 关联函数 | `UASStruct::CreateCppStructOps()`；`UASStruct::PrepareCppStructOps()`；`FASStructOps::Construct()`；`FASStructOps::Copy()`；`FASStructOps::Destruct()` |
| 现有测试覆盖 | 当前 `ClassGenerator/`、`HotReload/` 与全仓 `Plugins/Angelscript/Source/AngelscriptTest` 对 `CreateCppStructOps`、`UScriptStruct::InitializeStruct`、`CopyScriptStruct`、`DestroyStruct` 在 script struct 场景都是 0 命中；唯一相关覆盖仍只有 `Internals/AngelscriptStructCppOpsTests.cpp` 的 `NotBlueprintTypeByDefault` metadata 断言 |
| 风险评估 | 如果 script struct 的 `CppStructOps` 只把 equals/hash 挂上去，但构造、复制、析构语义已经失真，现有 suite 完全发现不了；后续最可能以默认值丢失、拷贝后脏数据或析构期资源泄漏的形式晚期暴露 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASStruct.CppStructOpsConstructCopyAndDestructThroughUScriptStruct` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptStructCppLifecycleTests.cpp` |
| 场景描述 | 编译一个脚本 `USTRUCT()`，在默认构造、复制构造、析构里分别增加全局计数器；随后通过 `UASStruct/UScriptStruct` API 对两块原始内存依次调用 `InitializeStruct`、写入字段、`CopyScriptStruct`、`DestroyStruct` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindObject<UScriptStruct>()` / `Cast<UASStruct>()`；`UScriptStruct::InitializeStruct` / `CopyScriptStruct` / `DestroyStruct`；用脚本 free function 暴露 `GetCtorCount()` / `GetCopyCount()` / `GetDtorCount()` 读取全局计数 |
| 期望行为 | 断言 `Cast<UASStruct>(Struct)` 非空且 `Struct->GetCppStructOps() != nullptr`；两次 `InitializeStruct` 后 ctor 计数为 `2`；把源缓冲区 `Value` 设为 `13` 后执行 `CopyScriptStruct`，目标缓冲区 `Value == 13` 且 copy 计数为 `1`；依次销毁两块缓冲区后 dtor 计数为 `2`。这样才能真正锁住 `CreateCppStructOps()` 暴露给 UE 的生命周期语义，而不是只看 metadata/能力位 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_FRESH`；`CompileAnnotatedModuleFromMemory`；`FindObject<UScriptStruct>`；`UScriptStruct::InitializeStruct/CopyScriptStruct/DestroyStruct`；`ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 00:35)

### 一、现有测试问题

#### Issue-43：`ScriptClass.CompilesToUClass` 没有验证产物真的是 script-generated `UClass`

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.CompilesToUClass` |
| 行号范围 | 155-202 |
| 问题描述 | 这个用例在 `CompileScriptModule()` 返回后，只断言 `ScriptClass->IsChildOf(AActor::StaticClass())`，随后直接 spawn actor 并读取 `SpawnMarker` 默认值。它没有检查 `FindGeneratedClass(&Engine, TEXT("AScenarioScriptClassCompilesToUClass")) == ScriptClass`，也没有验证返回类型是 `UASClass` 或者该类确实登记在当前 module record 里。换言之，测试名强调的是“编译成 generated UClass”，断言却主要覆盖了“一个 actor class 能被 spawn 并带属性默认值”。 |
| 影响 | 如果类生成器未来退化成返回错误的旧类、lookup 命中了同名缓存类，或者 helper 内部悄悄回退到非预期类对象，这个测试仍可能通过。它会把“基础生成成功”与“运行时 spawn smoke test”混成一件事，难以及时发现 `UClass` 注册/查找层面的回归。 |
| 修复建议 | 在现有 spawn 断言前补一组生成层断言：`FindGeneratedClass(&Engine, TEXT("AScenarioScriptClassCompilesToUClass")) == ScriptClass`、`Cast<UASClass>(ScriptClass) != nullptr`、`Engine.GetModuleByModuleName(ModuleName.ToString())->GetClass(TEXT("AScenarioScriptClassCompilesToUClass")).IsValid()`。如果想保持 smoke 用例单一职责，也可以把“生成为 script class”拆成独立用例，把当前用例降名为 `CompilesAndSpawnsActor`。 |

#### Issue-44：`HotReload.AnalyzeReload.NoChange` 只看返回枚举，没有验证分析过程是否保持只读

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AnalyzeReload.NoChange` |
| 行号范围 | 48-88 |
| 问题描述 | 用例先编译 `UReloadNoChangeTarget`，然后调用 `AnalyzeReloadFromMemory()`，最后只断言 `ReloadRequirement/bWantsFullReload/bNeedsFullReload` 三个返回值。它没有在分析前后保存并比较 `FindGeneratedClass("UReloadNoChangeTarget")`、module record 或可执行函数结果，因而完全没验证 `AnalyzeReloadFromMemory()` 是否保持只读，不会偷偷替换 class/function、污染 module 状态或改变查找表。 |
| 影响 | reload analyzer 本应是“分析而不 materialize”的入口；如果未来重构把某些预处理或 diff 路径做成了有副作用的 mutate，这个测试仍会绿灯，因为布尔返回值依旧可能正确。这样会让“分析阶段意外修改 live state”的回归长时间潜伏。 |
| 修复建议 | 在分析前保存 `UClass* ClassBeforeAnalyze`、`UFunction* GetValueBeforeAnalyze` 和 module record；分析后补断言 `FindGeneratedClass(&Engine, TEXT("UReloadNoChangeTarget")) == ClassBeforeAnalyze`、`FindGeneratedFunction(ClassBeforeAnalyze, TEXT("GetValue")) == GetValueBeforeAnalyze`、旧函数执行结果仍为 `10`，并确认 `Engine.GetModuleByModuleName(TEXT("ReloadNoChangeMod"))` 仍只包含同一类记录。这样才能把“无变化分析”真正锁成只读 contract。 |

#### Issue-45：`HotReload.FailureKeepsOldCodeAndDiagnostics` 把诊断文案和出现次数写死，稳定性受编译器措辞影响

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.FailureKeepsOldCodeAndDiagnostics` |
| 行号范围 | 420-423, 489-493 |
| 问题描述 | 用例在入口硬编码了 4 条 `AddExpectedError()`：既要求文件名前缀 `HotReloadFailureKeepsOldCode.as:` 出现 2 次，也要求 `"Identifier 'MissingType' is not a data type in global namespace"` 和 `"Identifier 'MissingType' is not a data type"` 各出现固定次数。这里测试真正想守住的是“reload 失败后保留旧代码并产出 diagnostics”，但实现上却把 AngelScript/UE 诊断的具体措辞、去重策略和报错折叠次数一起钉死。 |
| 影响 | 只要编译器升级、diagnostics 去重规则调整，或者同一错误被合并成 1 条/拆成 3 条，该测试就会因为日志文本漂移而失败，即使“失败后保留旧代码”的核心行为完全正确。这会把本应稳定的回归测试变成依赖文案细节的脆弱测试。 |
| 修复建议 | 预期错误应只保留稳定 contract：例如文件名前缀 + 一条高层错误 `"Hot reload failed due to script compile errors. Keeping all old script code."`。具体编译器诊断数量改成通过 `GetDiagnosticsCount()` 或扫描 diagnostics 容器断言“至少有一条 MissingType 相关诊断”，不要把 wording/count 写死。若必须验证关键文本，也应容许 `Contains` + 区间计数，而不是两个近似文案各 1 次。 |

#### Issue-46：`NativeScriptHotReload` 连 baseline full compile 的结果语义都没有锁定

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2A` / `Phase2B` / `Phase2C` |
| 行号范围 | 14-18, 35-41, 45-55 |
| 问题描述 | `VerifyNativeScriptHotReloadInline()` 为每个脚本都声明了 `InitialCompileResult`，但 baseline full compile 后从不检查这个结果值；它只看 `CompileModuleWithResult(...)` 的布尔返回是否为 true。也就是说，只要 wrapper 没有直接返回 false，哪怕 baseline compile 已经落在异常的 handled path、带着 unexpected partial state，测试也会继续进入 soft reload 阶段。 |
| 影响 | 这会把“热重载验证”建立在一个未被确认的初始状态上。若 baseline compile 已经不是预期的干净 full compile，后面的 soft reload 结果就失去解释力，CI 也无法区分“初始编译就异常”与“reload 阶段异常”。 |
| 修复建议 | 在 baseline compile 后显式断言 `InitialCompileResult == ECompileResult::FullyHandled || InitialCompileResult == ECompileResult::PartiallyHandled`，更理想的是 fresh module 场景直接要求 `FullyHandled`。随后再补至少一个产物级检查，例如 `FindGeneratedClass` / `FindGeneratedFunction` / `Engine.GetEnum()` 命中，确保 reload 不是在空 baseline 上继续跑。 |

### 二、需要新增的测试

#### NewTest-27：补齐 `UASClass::DefaultComponents` / `OverrideComponents` 的编译期布局元数据测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::FinalizeActorClass()`；`UASClass::DefaultComponents`；`UASClass::OverrideComponents` |
| 现有测试覆盖 | `Component/AngelscriptComponentScenarioTests.cpp` 只验证 runtime 创建出来的 root/attach 结果；当前 `ClassGenerator/`、`HotReload/` 和本轮 gap 记录都没有任何用例直接检查 `UASClass` 上保存的 `DefaultComponents/OverrideComponents` 元数据 |
| 风险评估 | 组件树运行时看起来能生成，不代表编译期布局数组正确。若 `DefaultComponents` / `OverrideComponents` 漏项、重排或记录了错误的 `Attach`/`bIsRoot`，后续 soft reload、验证器和实例化路径都会基于错误 plan 继续运行，现有 scenario 测试很难第一时间定位到元数据层。 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.DefaultComponentMetadataCapturesRootAndAttachLayout` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassComponentMetadataTests.cpp` |
| 场景描述 | 编译一个 actor：`RootScene` 标记 `UPROPERTY(DefaultComponent, RootComponent)`，`Billboard` 标记 `UPROPERTY(DefaultComponent, Attach = RootScene)`，再额外定义一个 `OverrideComponent` 场景；拿到生成的 `UASClass` 后直接检查元数据数组，而不是只 spawn actor 看结果 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule()`；`FindGeneratedClass()`；`Cast<UASClass>()`；必要时 `FindGeneratedClass()` 获取组件类 |
| 期望行为 | 断言 `Cast<UASClass>(ActorClass)` 有效；`DefaultComponents.Num() == 2`；其中一项 `ComponentName == "RootScene"` 且 `bIsRoot == true`、`Attach.IsNone()`；另一项 `ComponentName == "Billboard"` 且 `bIsRoot == false`、`Attach == "RootScene"`；若加入 override 场景，再断言 `OverrideComponents.Num() == 1`、`OverrideComponentName` 与 `VariableName` 正确，避免只验证 runtime hierarchy 而忽略编译期 layout plan |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedClass`；`Cast<UASClass>`；test-local `FindDefaultComponentEntryByName` |
| 优先级 | P1 |

#### NewTest-28：补齐带默认组件脚本类的 soft reload 元数据稳定性测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::DoSoftReload()`；`FinalizeActorClass()`；`UASClass::DefaultComponents`；`UASClass::OverrideComponents` |
| 现有测试覆盖 | 当前 hot reload 场景没有任何一个 module 同时带 `DefaultComponent`/`Attach` 元数据并走 `SoftReloadOnly`；全仓针对 `DefaultComponents` repeated reload 的 targeted 断言为 0 |
| 风险评估 | 组件布局数组是 hot reload 高风险区。若 soft reload 后重复追加旧条目、丢失 root 标记或 attach 名称漂移，live actor/component hierarchy 会在后续迭代中逐步污染，现有 suite 只看最终 spawn 结果，很可能在回归早期完全看不出来。 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.SoftReloadPreservesDefaultComponentMetadataWithoutDuplication` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassComponentMetadataTests.cpp` |
| 场景描述 | v1 编译一个带 `RootScene` + `Billboard` 默认组件和 `GetVersion() { return 1; }` 的 actor；保存 `UASClass* ClassV1` 与 `DefaultComponents/OverrideComponents` 快照；v2 只改函数体 `GetVersion() { return 2; }` 走 `SoftReloadOnly`，然后重新读取同一个 `UASClass` 的组件元数据 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule()` / `CompileModuleWithResult()`；`FindGeneratedClass()`；`Cast<UASClass>()`；必要时再 spawn 一个 actor 验证 runtime 没被破坏 |
| 期望行为 | 断言 reload 结果走 handled soft path；`ClassAfterReload == ClassV1`；`DefaultComponents.Num()` 与 v1 一致，不会从 `2` 变成 `4`；`RootScene` 项仍是唯一 root，`Billboard.Attach == "RootScene"`；如果存在 `OverrideComponents`，数量和 `OverrideComponentName` 也保持稳定；可额外断言 reload 后新 spawn actor 的 root/attach 结构仍与元数据一致 |
| 使用的 Helper | `CompileScriptModule`；`CompileModuleWithResult`；`FindGeneratedClass`；`Cast<UASClass>`；test-local `SnapshotComponentLayoutMetadata` |
| 优先级 | P1 |

#### NewTest-29：补齐 `UASClass` tick 设置继承与 `ReceiveTick` 激活规则测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::InitClassTickSettings()`；`UASClass::bCanEverTick`；`UASClass::bStartWithTickEnabled` |
| 现有测试覆盖 | 测试树里有大量 actor/component tick 行为场景，但对 `UASClass` 自身的 `bCanEverTick` / `bStartWithTickEnabled` 没有任何直接断言；gap 文档当前也没有对应建议 |
| 风险评估 | class generator 若把 tick 能力缓存错了，运行时症状通常会表现成“为什么这个 actor 不 tick/多 tick”，但很难快速定位到类生成阶段。缺少元数据级测试会让 tick 开关继承、`ReceiveTick` 激活和后续 reload 回归长期依赖行为层偶然暴露。 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.TickSettingsEnableChildTickWhenReceiveTickIsImplemented` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassTickSettingsTests.cpp` |
| 场景描述 | 编译 `AScriptTickParent : AActor` 不声明 tick；再编译 `AScriptTickChild : AScriptTickParent`，只实现 `UFUNCTION(BlueprintOverride) void ReceiveTick(float DeltaTime)`；拿到 parent/child `UASClass` 与 child CDO |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule()`；`FindGeneratedClass()`；`Cast<UASClass>()`；`GetDefaultObject<AActor>()` |
| 期望行为 | 断言 parent/child 都是 `UASClass`；`ParentASClass->bCanEverTick == false`；`ChildASClass->bCanEverTick == true` 且 `ChildASClass->bStartWithTickEnabled == true`；`ChildClass->GetDefaultObject<AActor>()->PrimaryActorTick.bCanEverTick == true`，确保缓存字段与 CDO tick 配置一致，而不是只在行为测试里间接观察 |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedClass`；`Cast<UASClass>`；`GetDefaultObject<AActor>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-46 |
| FlakyRisk | 1 | Issue-45 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | NoTestForSource: 2, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 00:32)

### 一、现有测试问题

#### Issue-41：`HotReload.Performance.BurstChurnLatency` 用 `-1` 接受无限次 expected error，会把重复告警回归全部吞掉

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.Performance.BurstChurnLatency` |
| 行号范围 | 240-318 |
| 问题描述 | 用例在入口调用 `AddExpectedErrorPlain(..., -1)`，表示 `"Full Reload is required..."` 这条告警可以出现无限次。由于该场景本身会连续执行 soft/full/soft 三步 compile，这个无上限 expected-error 会把“本来只该出现 1 次或 2 次”的异常放大、重复刷屏、甚至循环告警全部吞掉，测试最终仍可能通过。 |
| 影响 | 一旦 burst churn 路径开始反复触发 deferred full reload 告警，当前测试不会给出任何次数级信号，CI 只会看到绿灯和一份时延 artifact。这样既守不住回归，也让性能数据失去解释力，因为样本里可能混入了大量本不该出现的错误路径。 |
| 修复建议 | 把 expected error 数量收紧成与测量步数一致的上界，并在 `Measure()` 内把 `StepOne/StepTwo/StepThree` 的错误次数单独计数。更稳妥的做法是显式断言只有需要 deferred full reload 的那一步会产生该告警，其余步骤为 0 次，避免 `-1` 把任何数量的重复告警都默认为合理。 |

#### Issue-42：`HotReload.Performance.RenameWindowLatency` 把 expected-error 次数硬编码成 `4`，与采样循环强耦合

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.Performance.RenameWindowLatency` |
| 行号范围 | 183-236 |
| 问题描述 | 用例入口把 `"Cannot declare class UHotReloadPerformanceRename..."` 的 expected-error 次数写死为 `4`。这个数字并不是业务 contract，而是当前 `CollectHotReloadSamples()` 的 `1` 次 warmup 加 `3` 次 measurement 的偶然实现细节。一旦后续调整采样次数、移除 warmup、或在某次失败后提前退出，这条断言要么误报失败，要么继续掩盖真正的 rename-window 行为变化。 |
| 影响 | 性能 harness 与断言计数绑死，会让维护者难以单独调整采样策略。未来如果仅为了稳定性能统计而改 `WarmupRuns` / `MeasurementRuns`，这里就会出现无意义的测试噪音，反过来又鼓励开发者保留脆弱的魔法数字。 |
| 修复建议 | 把 expected-error 计数改为根据 `WarmupRuns + MeasurementRuns` 动态计算，或更直接地在 `Measure()` 内记录每轮是否命中了该诊断，再在外层按 sample 数量断言总次数。若 rename-window 场景最终不应依赖这条冲突错误，也应先修正场景建模，再移除这类基于魔法数字的 expected-error。 |

### 二、需要新增的测试

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-41 |
| AntiPattern | 1 | Issue-42 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | 无 |

---

## 测试审查 (2026-04-09 00:19)

### 一、现有测试问题

#### Issue-36：`HotReload.ModuleWatcherQueuesFileChanges` 在 `ASTEST_END_SHARE_FRESH` 之前直接 `return`，破坏了生命周期宏配对约定

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.ModuleWatcherQueuesFileChanges` |
| 行号范围 | 302-329 |
| 问题描述 | 用例在 `ASTEST_BEGIN_SHARE_FRESH` 打开作用域后，最后一个断言直接 `return TestEqual(...)`，导致 `ASTEST_END_SHARE_FRESH` 永远不可达。`AngelscriptTestMacros.h` 已明确要求“把终结 `return` 放在 `ASTEST_END_*` 之后”，这样生命周期配对才在源码中保持显式；当前写法虽然依赖 C++ 栈展开仍会释放 `FAngelscriptEngineScope`，但宏边界被隐藏，读者无法从源码直接确认生命周期是否完整闭合。 |
| 影响 | 这会把测试的资源生命周期语义埋进隐式 RAII，而不是项目统一约定的 `BEGIN/END` 结构，增加审查和后续维护成本。未来若有人在 `ASTEST_END_SHARE_FRESH` 前后补逻辑，也很容易误以为该逻辑会执行，形成静默死代码。 |
| 修复建议 | 改成先把最后一次 `TestEqual(...)` 结果写入 `bPassed`，然后落到 `ASTEST_END_SHARE_FRESH` 之后再 `return bPassed;`。同类模式应在 `ClassGenerator/` 与 `HotReload/` 目录里统一清理，保证所有 `ASTEST_BEGIN_*` / `ASTEST_END_*` 在源码层面成对出现。 |

#### Issue-37：`HotReload.PIEStructuralChangeNeedsFullReload` 同样在 `ASTEST_END_SHARE_FRESH` 之前直接返回，源码中的生命周期闭合不可见

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.PIEStructuralChangeNeedsFullReload` |
| 行号范围 | 395-406 |
| 问题描述 | 该用例在完成两条 `TestTrue` 断言后，直接 `return TestTrue(...)`，把 `ASTEST_END_SHARE_FRESH` 留成不可达代码。虽然已有 `ON_SCOPE_EXIT` 会处理模块回收，但 `ASTEST_BEGIN_SHARE_FRESH` 打开的 engine-scope 生命周期仍然没有按项目约定在源码中显式闭合。 |
| 影响 | 和 Issue-36 一样，这种写法会削弱测试宏的可读性和一致性。尤其这个用例本身名字已经与实际覆盖不符，再叠加不可达的 `ASTEST_END_*`，后续审查者更难快速判断它到底依赖了哪些生命周期语义。 |
| 修复建议 | 采用与其他 `bPassed` 风格用例一致的结构：先保存最终断言结果，再经过 `ASTEST_END_SHARE_FRESH`，最后返回。顺手把同文件内所有 scenario 用例统一成同一种宏配对风格，避免部分用例是“显式闭合”，部分用例靠早退隐藏闭合。 |

#### Issue-38：`VerifyNativeScriptHotReloadInline` 从不校验 initial compile 的 `ECompileResult`，基线阶段可能已偏离预期路径

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2A` / `Phase2B` / `Phase2C` |
| 行号范围 | 35-55 |
| 问题描述 | `VerifyNativeScriptHotReloadInline()` 在 baseline 阶段只断言 `CompileModuleWithResult(..., ECompileType::FullReload, ..., InitialCompileResult)` 返回 `true`，但从未检查 `InitialCompileResult` 是否真的是 `FullyHandled`/`PartiallyHandled` 等成功态。也就是说，哪怕初始 full compile 走了意外的降级路径、产生了需要人工 full reload 的状态，测试也会继续执行后续 soft reload 并把问题归因到第二步。 |
| 影响 | 这会让 Phase2A/B/C 的失败定位变得不可靠，因为测试没有先锁住“初始脚本处于健康可运行基线”。一旦 baseline compile 语义漂移，后续 reload 断言就失去参照物，CI 只能告诉你“第二步没过”，却不能指出第一步其实已经偏离 contract。 |
| 修复建议 | 在 initial compile 后立即补显式断言：`InitialCompileResult == ECompileResult::FullyHandled || InitialCompileResult == ECompileResult::PartiallyHandled`。如果某些脚本预期只能通过更保守路径编译，应把该预期写成专门的场景，而不是让通用 helper 静默接受所有初始 compile 结果。 |

#### Issue-39：`HotReloadPerformanceTests` 四个采样 lambda 都不校验 baseline compile 成功，计时样本可能从错误初态起步

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.Performance.*` |
| 行号范围 | 111-117, 162-168, 215-221, 289-305 |
| 问题描述 | 四个性能采样 lambda 在开始计时前都直接调用 `CompileAnnotatedModuleFromMemory(..., ScriptV1)`，但完全忽略返回值，也不检查 baseline module 是否真的编译成功。随后它们立刻测量 reload compile 并把 `Elapsed` 写进样本。这样一来，只要 baseline compile 因环境、脚本或前置状态失败，整个样本测到的就不再是“reload latency”，而是“错误初态上的一次 compile wrapper 时间”。 |
| 影响 | 性能样本缺乏有效起点，artifact 中的时延数字可能混入冷启动失败、module 未建立或残留状态干扰时间。即使未来补上阈值，门禁也会建立在不可信样本上。 |
| 修复建议 | 在每个 lambda 里先显式断言 baseline compile 成功，再开始计时；推荐把 baseline 结果也记录到 `FHotReloadPerformanceSample`，例如增加 `bBaselineCompiled` 或 `BaselineCompileResult`。如果 baseline 未达成功态，应直接让该 measurement 样本失败，而不是继续产出时延数字。 |

#### Issue-40：三条 `AnalyzeReload.*` “required” 用例仍只断言 `bWantsFullReload || bNeedsFullReload`，没有把必需态布尔语义锁死

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AnalyzeReload.SuperClassChange` / `ClassRemoved` / `FunctionSignatureChanged` |
| 行号范围 | 163-175, 304-316, 354-366 |
| 问题描述 | 这三条用例都把 `ReloadRequirement` 精确断言成 `FullReloadRequired`，但对应布尔位仍然只写成 `bWantsFullReload || bNeedsFullReload`。如果 analyzer 未来错误地把“必须 full reload”的结果降成 `bWantsFullReload = true, bNeedsFullReload = false`，这些用例依旧会通过。也就是说，它们没有真正验证枚举值与布尔位之间的一致性。 |
| 影响 | 上层调用点不一定只消费 `ReloadRequirement`；有些 UI、命令行和 compile wrapper 会直接看 `bNeedsFullReload` 决定是否阻止 soft reload。当前断言留下了一个明显回归缝隙，会让“枚举是 required、布尔却不是 need”的不一致状态静默进入主流程。 |
| 修复建议 | 对这三条 required 场景统一补成强断言：`bNeedsFullReload == true`，并根据产品语义明确 `bWantsFullReload` 是否允许同时为 `true`。如果期望 required 场景只设置 need、不设置 wants，就应显式断言 `bWantsFullReload == false`，避免 future refactor 把两个布尔位混成同义字段。 |

### 二、需要新增的测试

#### NewTest-24：补齐 `UASClass::RuntimeAddReferencedObjects()` / `ReferenceSchema` 的 script-only 引用 GC 保活测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASClass::RuntimeAddReferencedObjects(UObject*, FReferenceCollector&)`；`FAngelscriptClassGenerator::DetectAngelscriptReferences()`；`UASClass::ReferenceSchema` |
| 现有测试覆盖 | 对整个 `Plugins/Angelscript/Source/AngelscriptTest` 定向检索，`RuntimeAddReferencedObjects`、`ReferenceSchema`、`EmitReferenceInfo`、`HasReferences(` 全部 0 命中；当前 GC 场景只验证空 actor/component/world teardown，不验证 script-only 引用字段 |
| 风险评估 | script 字段若不映射成 `UPROPERTY`，但又需要引用 `UObject`，一旦 GC schema 或 `RuntimeAddReferencedObjects` 断链，就会在对象仍被脚本持有时被提前回收；这是直接的内存安全与生命周期正确性风险 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.RuntimeAddReferencedObjectsKeepsScriptOnlyObjectReferenceAlive` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassReferenceSchemaTests.cpp` |
| 场景描述 | 编译 `UReferenceSchemaHolder : UObject`，其中声明一个非 `UPROPERTY` 的 `UObject HiddenRef = nullptr;`，并提供 `UFUNCTION() void Store(UObject InValue)` 与 `UFUNCTION() UObject GetStored() const`。创建 holder 与一个 transient target object，通过 `ProcessEvent` 调 `Store()` 把 target 存进脚本字段，然后释放 C++ 强引用并触发 `CollectGarbage()` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FindGeneratedClass()`；`FindGeneratedFunction()`；test-local `StoreParams` / `GetStoredParams` 结构体；`TWeakObjectPtr<UObject>`；`CollectGarbage(RF_NoFlags, true)` |
| 期望行为 | GC 前断言 `GetStored()` 返回 target；GC 后若 holder 仍活着，则 `WeakTarget.IsValid()` 仍为 `true`，且再次调用 `GetStored()` 仍返回同一对象；如果脚本再把 `HiddenRef = nullptr` 并二次 GC，`WeakTarget` 应失效，证明保活来自 script-only reference 而不是测试残留强引用 |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedClass`；`FindGeneratedFunction`；`ProcessEvent`；`CollectGarbage` |
| 优先级 | P0 |

#### NewTest-25：补齐 `FAngelscriptAdditionalCompileChecks` 的 compile/reload hook 调用测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptAdditionalCompileChecks.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 关联函数 | `FAngelscriptAdditionalCompileChecks::ScriptCompileAdditionalChecks()`；`FAngelscriptAdditionalCompileChecks::PostReloadAdditionalChecks()`；`FAngelscriptEngine::AdditionalCompileChecks` |
| 现有测试覆盖 | 对整个 `Plugins/Angelscript/Source/AngelscriptTest` 定向检索，`AdditionalCompileChecks`、`ScriptCompileAdditionalChecks`、`PostReloadAdditionalChecks` 全部 0 命中 |
| 风险评估 | 游戏模块若依赖这个 hook 做额外脚本校验或 reload 后收尾，一旦 generator 不再调用它们，项目级规则会静默失效，CI 现状不会报警 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.AdditionalCompileChecks.InvokeCompileAndPostReloadHooks` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptAdditionalCompileChecksTests.cpp` |
| 场景描述 | 在测试文件里定义一个 recorder 型 `FTestAdditionalCompileChecks : FAngelscriptAdditionalCompileChecks`，记录 `CompileCheckCount`、`PostReloadCount`、`LastModuleName`、`LastClassName`、`bLastFullReload`。把它注册到 `Engine.AdditionalCompileChecks`，key 选脚本基类的 native code parent，例如 `UObject::StaticClass()`；先编译 `UAdditionalChecksTarget : UObject` 的 v1，再 full reload 到带新增属性的 v2 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()` / `CompileModuleWithResult()`；`ON_SCOPE_EXIT` 移除 `Engine.AdditionalCompileChecks` 里的测试 hook；必要时在 recorder 内保存 `TWeakPtr` 或普通计数器 |
| 期望行为 | v1 compile 后断言 `CompileCheckCount == 1`、`LastModuleName` 与 `LastClassName` 正确；full reload 后断言 `CompileCheckCount == 2`、`PostReloadCount == 1`、`bLastFullReload == true`；如果再补负路径子用例，让 recorder 返回 `false`，compile 应进入 error 状态并留下 diagnostics，而不是静默继续生成类 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`Engine.AdditionalCompileChecks`；test-local `FTestAdditionalCompileChecks` |
| 优先级 | P1 |

#### NewTest-26：补齐 `UASFunction::AllocateFunctionFor()` 的线程安全/优化分派子类选择测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASFunction::AllocateFunctionFor(UClass*, FName, TSharedPtr<FAngelscriptFunctionDesc>)` |
| 现有测试覆盖 | 对整个 `Plugins/Angelscript/Source/AngelscriptTest` 定向检索，`AllocateFunctionFor`、`BlueprintThreadSafe`、`NotBlueprintThreadSafe`、`UASFunction_NotThreadSafe`、`UASFunction_JIT` 全部 0 命中 |
| 风险评估 | 如果生成器给函数分配了错误的 `UASFunction` 子类，运行时会走错 dispatch 路径，轻则线程安全语义漂移，重则 JIT/反射调用不一致；当前 hot reload suite 不会直接暴露这个问题 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASFunction.AllocateFunctionForSelectsCorrectThreadSafeDispatchSubclass` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionDispatchTests.cpp` |
| 场景描述 | 编译两组最小脚本类，函数签名都为 `UFUNCTION() int GetValue() { return 1; }`。第一组保持默认非线程安全语义；第二组通过 class 或 function metadata 显式标成 `BlueprintThreadSafe`。分别查出 `GetValue` 对应的 generated `UFunction` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FindGeneratedClass()`；`FindGeneratedFunction()`；`Cast<UASFunction>()`；必要时通过 `Function->GetClass()->GetName()` 判定 `_JIT` / 非 `_JIT` 变体 |
| 期望行为 | 对默认非线程安全版本，断言 `GetValue` 的 generated function 是 `UASFunction_DWordReturn` 或 `UASFunction_DWordReturn_JIT` 家族；对 `BlueprintThreadSafe` 版本，断言它不再落到 `DWordReturn` / `NotThreadSafe` 家族，而是 `UASFunction` 或 `UASFunction_JIT` 通用线程安全路径；两组函数都应可正常执行返回 `1`，避免只测类名不测行为 |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedClass`；`FindGeneratedFunction`；`ExecuteGeneratedIntEventOnGameThread`；`Cast<UASFunction>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-40 |
| AntiPattern | 2 | Issue-37 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-08 13:37)

### 一、现有测试问题

#### Issue-23：`HotReload.SoftReload.PreservesOtherModules` 只验证 free function，无法证明反射产物没有被旁路污染

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.SoftReload.PreservesOtherModules` |
| 行号范围 | 139-205 |
| 问题描述 | 用例把模块 A 和 B 都建成只有一个全局函数的脚本模块，然后在 soft reload A 之后仅重新执行 `GetValueA()` 和 `GetValueB()`。它没有让模块 B 生成任何 `UClass`/`UFunction`/`UEnum`，也没有保存或比对 `Engine.GetModuleByModuleName("SoftPreserveB")`、`FindGeneratedClass()`、`FindGeneratedFunction()` 等反射级状态。因此它验证到的只是“另一个 module 的 free function 还能调用”，而不是 “class generator / hot reload 没有破坏旁路 module 的 reflected artifacts”。 |
| 影响 | 如果 soft reload A 时错误污染了模块 B 的 `UClass`、`UFunction`、enum metadata 或 module record，只要模块 B 的全局函数仍能运行，这个用例就会继续绿灯。它会把真正高风险的跨模块反射污染误报成“已覆盖”。 |
| 修复建议 | 把模块 B 改成至少包含一个 `UCLASS()` 或 `UENUM()` 的 reflected module，reload 前保存 `ModuleRecordB`、`UClass*`、`UFunction*` 或 enum metadata，reload 后断言这些对象仍可按原名查到且关键指针未变化；同时创建模块 B 的对象并执行其 generated method，确认跨模块 soft reload 不会破坏 reflected state。free function 断言可以保留，但只能作为附加 smoke check。 |

#### Issue-24：`HotReload.AddModifyLookupFlow` 没有真正验证 lookup flow，只验证了“按新名字再查一次能跑”

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AddModifyLookupFlow` |
| 行号范围 | 332-413 |
| 问题描述 | 用例在 body-only soft reload 后重新 `FindGeneratedClass("UHotReloadModifyLookupFlow")` 和 `FindGeneratedFunction("GetValue")`，然后在新查到的对象上执行一次返回值断言。它没有比较 `ClassBeforeReload` 与 `ClassAfterReload` 是否仍为同一个 `UClass`，没有约束 reload 前缓存下来的 `UFunction*` 在 reload 后的行为，也没有在 `Engine.DiscardModule()` 后验证 `FindGeneratedClass/FindGeneratedFunction` 已经失效。也就是说，标题里的 add/modify/lookup/discard 四段 contract，真正被验证到的只有“修改后按名字再查得到一个可执行函数”。 |
| 影响 | 如果 soft reload 错误地替换了 `UClass`、留下了 stale lookup entry，或者 discard 之后 class/function 仍残留在查找表里，这个用例都可能保持通过。它对 lookup registry 的保护力明显低于测试名宣称的范围。 |
| 修复建议 | reload 前保存 `ClassBeforeReload`、`GetValueBeforeReload` 与 module record；reload 后补 `TestEqual(ClassAfterReload, ClassBeforeReload)` 以守住 soft reload 复用语义，并根据产品 contract 断言旧 `UFunction*` 是否仍可调用或必须被新查找替换。随后在 `DiscardModule()` 后增加负向断言：`FindGeneratedClass(&Engine, TEXT("UHotReloadModifyLookupFlow")) == nullptr`、`FindGeneratedFunction(ClassAfterReload, TEXT("GetValue")) == nullptr` 或等价失败路径，同时确认 module record 已移除。 |

#### Issue-25：`HotReload.ModuleWatcherQueuesFileChanges` 把绝对路径硬编码到 `J:/...`，带入了明显的平台与工作区假设

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.ModuleWatcherQueuesFileChanges` |
| 行号范围 | 307-310 |
| 问题描述 | 用例直接把 `FilenamePair.AbsoluteFilename` 写死成 `J:/UnrealEngine/Temp/UE-Angelscript/Saved/Automation/WatcherTest.as`，相对路径也写成固定字符串 `Automation/WatcherTest.as`。这不是从 `FPaths::ProjectSavedDir()` 或 test workspace 计算出来的路径，而是把当前开发机盘符、目录布局和文件规范化结果编码进了测试。即使该用例目前没真正访问文件系统，这个绝对路径仍是它声明中的核心输入。 |
| 影响 | 一旦 hot reload queue 对绝对路径规范化、盘符大小写、工程根目录迁移或不同工作区布局更敏感，这个测试就会继续在固定样例上通过，却无法代表真实工程路径。它还降低了测试可移植性，让用例语义依赖当前仓库恰好位于 `J:` 盘这一隐含前提。 |
| 修复建议 | 用 `FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/WatcherTest.as"))` 构造绝对路径，并在断言前用 `FPaths::MakeStandardFilename()` 做标准化；如果要验证 relative path，也应从同一 helper 推导而不是手写字符串。这样既能保留 queue semantics，又不会把本机目录结构硬编码进测试。 |

#### Issue-26：`ScriptClass.NonUClassTypeCannotSpawn` 的名字和实际场景不一致，根本没有覆盖“non-UClass type”

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.NonUClassTypeCannotSpawn` |
| 行号范围 | 581-624 |
| 问题描述 | 当前脚本源码实际声明的是 `UCLASS() class UScenarioScriptClassNonUClassTypeCannotSpawn : UObject`，也就是一个完全合法的 generated `UClass`。用例随后验证它不是 `AActor` 子类，并且 `SpawnActor` 会失败。换言之，这个测试覆盖到的是“`UObject`-derived script class 不能当 actor spawn”，而不是测试名宣称的 “non-UClass type cannot spawn”。它没有涉及 `USTRUCT`、`UENUM`、interface 或任何真正不会生成 `UClass` 的脚本形态。 |
| 影响 | 读者会误以为“非 UClass 类型的负路径”已经有自动化，实际上当前 suite 只覆盖了 actor-spawn rejection。真正的 non-UClass 生成/查找负路径仍然是空白，这会掩盖类生成器对 struct/interface 等类型的错误回归。 |
| 修复建议 | 二选一修正：1. 保留当前脚本与断言，但把用例重命名为 `Angelscript.TestModule.ScriptClass.ObjectClassCannotSpawnAsActor`，明确它测试的是 `UObject` 类不能进入 `SpawnActor`；2. 如果要保持现有测试名，就改用 `USTRUCT()`/interface 等不会生成 `UClass` 的脚本输入，并断言 `CompileScriptModule` 不会返回 `UClass`、`FindGeneratedClass()` 结果为空。最好将两种场景拆成两个用例，各自守住自己的 contract。 |

### 二、需要新增的测试

#### NewTest-10：补齐 `FAngelscriptClassGenerator::GetFullReloadLines` 与 `Setup()` 决策测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::Setup()`；`FAngelscriptClassGenerator::GetFullReloadLines()`；`WantsFullReload()`；`NeedsFullReload()` |
| 现有测试覆盖 | `ClassGeneratorTests.cpp` 只有 `EmptyModuleSetup`，仓库内对 `GetFullReloadLines()` 为 0 命中；当前没有任何用例直接验证 generator 把结构性变更映射到具体源码行 |
| 风险评估 | 一旦 full reload 行号收集退化，编辑器诊断和自动修复提示会失真；当前 CI 只能知道“要不要 full reload”，不知道 generator 标出来的行是否对 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.SetupReportsStructuralReloadLines` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptClassGeneratorDecisionTests.cpp` |
| 场景描述 | 先编译 `UDecisionLineTarget` 的 v1；再用 test-local helper 构造带 `ExtraValue` 新增 `UPROPERTY()` 的 v2 `FAngelscriptModuleDesc`，手动喂给 `FAngelscriptClassGenerator` 调 `Setup()` 与 `GetFullReloadLines()` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；baseline 用 `CompileAnnotatedModuleFromMemory()`；测试内复制 `AngelscriptTestEngineHelper.cpp` 的轻量 `MakeModuleDesc` 模式构造 v2 `FAngelscriptModuleDesc`；`FAngelscriptEngineScope` |
| 期望行为 | 断言 `Setup()` 返回 `FullReloadRequired` 或明确的结构性 full reload 结果；`WantsFullReload(Module)` 与 `NeedsFullReload(Module)` 为 `true`；`GetFullReloadLines()` 返回非空并包含新增 `UPROPERTY()` 所在源码行，而不会把仅函数体改动的行混入 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；test-local `MakeModuleDesc`；`FAngelscriptClassGenerator` |
| 优先级 | P1 |

#### NewTest-11：补齐 `UASClass::IsDeveloperOnly()` 的嵌套 `.Editor.` 模块判定测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::IsDeveloperOnly() const` |
| 现有测试覆盖 | 全仓 `AngelscriptTest` 只有 bind map 注册测试触及 `IsDeveloperOnly` 名字；当前 `ClassGenerator/`、`HotReload/` 没有任何用例直接创建脚本类并断言返回值 |
| 风险评估 | 编辑器专用模块若被误判成非 developer-only，会直接影响 `FinalizeActorClass()` 的 editor-only component 校验；当前没有 targeted test 能尽早发现 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.IsDeveloperOnlyRecognizesNestedEditorModuleNames` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassMetadataTests.cpp` |
| 场景描述 | 分别编译模块名为 `Game.Tools.Editor.Visualizers` 与 `Game.Tools.Runtime.Visualizers` 的两个最小 `UCLASS()`；拿到生成的 `UASClass` 后直接查询 `IsDeveloperOnly()` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindGeneratedClass()`；`Cast<UASClass>()` |
| 期望行为 | 断言 `Game.Tools.Editor.Visualizers` 里的 `UASClass` 返回 `true`，runtime 对照模块返回 `false`；如再补一条 `Editor.` 前缀模块，可同时验证前缀与嵌套命名两种合法输入都被识别 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`FindGeneratedClass`；`Cast<UASClass>` |
| 优先级 | P1 |

#### NewTest-12：补齐 `UASClass::IsFunctionImplementedInScript()` 的失效态测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::IsFunctionImplementedInScript(FName) const`；`UASFunction::GetSourceFilePath() const`；`UASFunction::GetSourceLineNumber() const` |
| 现有测试覆盖 | `Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` 只验证正路径 `ComputeValue` 返回 `true`；当前 `ClassGenerator/`、`HotReload/` 没有任何用例在 `DiscardModule()` 或 reload 后重查失效态 |
| 风险评估 | 模块卸载或类删除后，脚本函数壳子若仍被误报为“已实现”，工具链和 runtime fallback 会基于错误能力信息继续工作，CI 目前无感知 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.IsFunctionImplementedInScriptTurnsFalseAfterDiscard` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassMetadataTests.cpp` |
| 场景描述 | 编译一个带 `ComputeValue()` 的脚本类，缓存 `UASClass*` 与 `UASFunction*`；确认正路径后执行 `Engine.DiscardModule(ModuleName)`，再在缓存对象上查询实现态和源码 metadata |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindGeneratedClass()`；`FindGeneratedFunction()`；`Engine.DiscardModule()` |
| 期望行为 | discard 前断言 `IsFunctionImplementedInScript(TEXT("ComputeValue")) == true`、`UASFunction::GetSourceFilePath()` 非空、`GetSourceLineNumber() > 0`；discard 后断言同一 `UASClass` 上 `IsFunctionImplementedInScript(TEXT("ComputeValue")) == false`，并且缓存 `UASFunction` 的 `GetSourceFilePath()` 为空或 `GetSourceLineNumber() == -1`，避免 stale script binding 被继续当成 live 实现 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`FindGeneratedClass`；`FindGeneratedFunction`；`Cast<UASClass>`；`Cast<UASFunction>` |
| 优先级 | P1 |

#### NewTest-13：补齐 enum/full reload 事件广播测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::OnEnumChanged`；`OnFullReload`；`OnPostReload` |
| 现有测试覆盖 | 当前 gap 文档已有建议只覆盖 `OnClassReload` / `OnStructReload` / `OnPostReload`；仓库测试树对 `OnEnumChanged`、`OnFullReload` 没有任何 targeted 命中 |
| 风险评估 | enum full reload 若没有正确广播，编辑器缓存、UI 刷新和依赖工具会静默不同步；现在没有任何自动化能第一时间发现 |
| 建议测试名 | `Angelscript.TestModule.HotReload.ReloadDelegates.BroadcastEnumChangeAndFullReload` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDelegateTests.cpp` |
| 场景描述 | 基线编译 `EHotReloadEventState { Alpha, Beta = 4 }` 与一个 enum carrier class；绑定 `OnEnumChanged`、`OnFullReload`、`OnPostReload`；随后 full reload 到带 `Gamma = 9` 的 v2 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::FullReload, ...)`；delegate handle 计数器；`Engine.GetEnum()` |
| 期望行为 | 断言 full reload compile 成功；`OnFullReload` 触发 1 次；`OnEnumChanged` 触发 1 次且回调里的 `OldNames` 仍是旧成员表、reload 后 `Engine.GetEnum("EHotReloadEventState")` 能看到 `Gamma = 9`；`OnPostReload` 收到一次 `true`，并且广播发生时新 enum/carrier class 已可查询 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；delegate handle 捕获；`Engine.GetEnum` |
| 优先级 | P1 |

#### NewTest-14：补齐 `OnDelegateReload` 签名替换广播测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::OnDelegateReload` |
| 现有测试覆盖 | 全仓 delegate 测试只验证 delegate 元数据和调用桥接；当前 `ClassGenerator/`、`HotReload/` 对 `OnDelegateReload` 为 0 命中 |
| 风险评估 | delegate 签名 full reload 若没有正确广播，依赖旧 `UDelegateFunction` 切换的编辑器和运行时缓存会静默滞后，当前没有自动化保护 |
| 建议测试名 | `Angelscript.TestModule.HotReload.ReloadDelegates.BroadcastDelegateSignatureSwap` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDelegateTests.cpp` |
| 场景描述 | 基线编译 `delegate void FHotReloadSignal(int Value);` 与持有该 delegate 属性的 `UCLASS`；绑定 `OnDelegateReload`；随后 full reload 到 `delegate void FHotReloadSignal(int Value, const FString& Label);` 的 v2 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::FullReload, ...)`；`Engine.GetDelegate(TEXT("FHotReloadSignal"))`；delegate handle 捕获 |
| 期望行为 | 断言 full reload compile 成功；`OnDelegateReload` 触发 1 次，回调里的 old/new `UDelegateFunction*` 都非空且 `Old != New`；reload 后新的 delegate metadata 仍可查询，`SignatureFunction` 或 `UDelegateFunction` 上能看到新增的 `Label` 参数 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`Engine.GetDelegate`；delegate handle 捕获 |
| 优先级 | P1 |

---

## 测试审查 (2026-04-08 23:46)

### 一、现有测试问题

#### Issue-27：`HotReload.AnalyzeReload.PropertyCountChange` 把“必须 full reload”的结构变更写成了宽松断言

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AnalyzeReload.PropertyCountChange` |
| 行号范围 | 123-135 |
| 问题描述 | 用例在新增 `UPROPERTY() int ExtraValue;` 后，只断言 `bWantsFullReload || bNeedsFullReload` 为真，并接受 `ReloadRequirement` 为 `FullReloadRequired` 或 `FullReloadSuggested` 任一结果。可源码里 `FAngelscriptClassGenerator::Analyze()` 对“新增需要真实 `FProperty` 的属性”会把 `ReloadReq` 提升到 `FullReloadRequired`，并记录对应行号。也就是说，这个测试把一个应当是强约束的 contract 写成了“建议或必须都行”。 |
| 影响 | 如果未来回归把新增 `UPROPERTY()` 错误降级成 `FullReloadSuggested`，analysis suite 仍会绿灯，CI 无法及时发现结构性属性变更被误路由到 `SoftReloadOnly`/可部分处理路径。 |
| 修复建议 | 把断言收紧为 `ReloadRequirement == FAngelscriptClassGenerator::FullReloadRequired`，同时要求 `bNeedsFullReload == true`；如果保留 `AnalyzeReloadFromMemory()` helper，还应补一条针对 `GetFullReloadLines()` 或 compile summary 的断言，确保结构变更被标记到新增属性行。 |

#### Issue-28：`AngelscriptHotReloadAnalysisTests.cpp` 七个 analysis 用例都依赖启动时 reset，却没有在退出时清理模块

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AnalyzeReload.*` |
| 行号范围 | 50-88, 95-135, 142-175, 182-226, 233-271, 278-316, 323-366 |
| 问题描述 | 七个用例统一使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` + `ASTEST_BEGIN_SHARE_CLEAN`，但该生命周期宏只建立 `FAngelscriptEngineScope`，不会像 `FULL/CLONE` 那样自动 `DiscardModule()`。这些测试在体内先后创建了 `ReloadNoChangeMod`、`ReloadPropertyMod`、`ReloadSuperMod` 等模块，却没有任何 `ON_SCOPE_EXIT` 或显式 `Engine.DiscardModule()` 回收，实际是在依赖下一条测试启动时的 reset 来兜底。 |
| 影响 | 单独运行某个 analysis 用例、在中途失败后调试、或后续插入非 `SHARE_CLEAN` 测试时，shared engine 会保留这些分析模块和 diagnostics。这样会让测试结果依赖执行顺序，也会把 analysis helper 产物泄漏给后续用例。 |
| 修复建议 | 对每个用例增加 `ON_SCOPE_EXIT`，显式 `DiscardModule(ModuleName)`；如果一个用例会创建多个模块，就集中在退出时逐个回收。更稳妥的做法是把 analysis suite 切到 `ASTEST_CREATE_ENGINE_FULL()`/`ASTEST_CREATE_ENGINE_CLONE()`，让生命周期宏自动清掉 active modules，而不是依赖下一条测试的前置 reset。 |

#### Issue-29：`HotReload.ModuleRecordTracking` 对 active module 数量只做“至少两个”的宽松判断，守不住泄漏回归

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.ModuleRecordTracking` |
| 行号范围 | 125-147 |
| 问题描述 | 用例在 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` 的 fresh engine 上编译 `ModuleA`、`ModuleB` 后，只断言 `Engine.GetActiveModules().Num() >= 2`。它没有验证 active module 集合是否正好增加了这两个模块，也没有排除旧模块泄漏、重复注册或额外脏记录。后面的断言只关心 `GetModuleByModuleName("ModuleA/B")` 能查到，无法发现“除了 A/B 之外还多出脏模块”的情况。 |
| 影响 | 一旦 shared-engine reset 退化、module record 没有完全清理，测试仍可能通过，因为 A/B 仍然存在，且 `>= 2` 会把额外残留全部吞掉。它因此守不住 module tracking 最关键的“没有意外活跃模块”这条隔离 contract。 |
| 修复建议 | 在编译前记录 baseline active module 数或 baseline module 名集合，编译后断言结果恰好是 baseline + 2，并显式验证新增项只有 `ModuleA`、`ModuleB`。若产品允许框架级常驻模块，也应把这些常驻模块列入白名单，而不是用 `>= 2` 放过所有额外状态。 |

#### Issue-30：`HotReload.FunctionChange` 没有验证 soft reload 是否保持了同一个 live actor class

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.FunctionChange` |
| 行号范围 | 294-335 |
| 问题描述 | 用例在 reload 前缓存了 `ClassV1` 和 `GetValueBeforeReload`，但 reload 后只重新 `FindGeneratedClass()` / `FindGeneratedFunction()`，然后在旧 actor 上执行新查到的函数并断言返回 `2`。它没有验证 `ClassAfterReload == ClassV1`，也没有检查 `Actor->GetClass()` 在 reload 前后是否保持同一个 `UClass`。对一个宣称覆盖“soft reload on same actor instance”的 scenario 来说，这意味着它并没有真正守住“没有发生 class switch / reinstance”的前提。 |
| 影响 | 如果未来回归把 body-only 函数改动错误地升级成 full reload 或 actor class switch，只要 `FindGeneratedClass()` 还能返回一个可执行的新函数，当前用例就可能继续通过，或者只在更晚的对象调用阶段以不透明方式失败。这样会掩盖最重要的 live actor 绑定语义。 |
| 修复建议 | 在现有断言外补 `TestEqual(ClassAfterReload, ClassV1)`、`TestEqual(Actor->GetClass(), ClassV1)`，必要时再断言缓存的 `GetValueBeforeReload` 在同一 actor 上也遵循预期 contract。这样才能把“同一 actor + 同一 UClass + 新函数体”这条 soft reload 语义写完整。 |

### 二、需要新增的测试

#### NewTest-15：补齐 `OnEnumCreated` 首次创建广播测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::OnEnumCreated` |
| 现有测试覆盖 | 当前 gap 文档只规划了 `OnEnumChanged` / `OnFullReload` / `OnDelegateReload`；全仓 `AngelscriptTest` 对 `OnEnumCreated` 为 0 命中 |
| 风险评估 | 新增 enum 若没有正确广播，依赖枚举注册时机的编辑器缓存、面板刷新和辅助工具会静默缺失通知，现有自动化不会报警 |
| 建议测试名 | `Angelscript.TestModule.HotReload.ReloadDelegates.BroadcastEnumCreatedOnFirstCompile` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadDelegateTests.cpp` |
| 场景描述 | 先用一个最小 warmup module 确保 engine 已完成初始 compile；随后绑定 `OnEnumCreated`，编译一个首次声明 `UENUM(BlueprintType) enum class EHotReloadCreatedState : uint8 { Alpha, Beta };` 的模块 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；delegate handle 计数器；`Engine.GetEnum(TEXT("EHotReloadCreatedState"))` |
| 期望行为 | 断言目标模块 compile 成功；`OnEnumCreated` 触发 1 次，回调里的 `UEnum*` 名称为 `EHotReloadCreatedState`；`Engine.GetEnum()` 返回有效枚举描述，且不会误触发 `OnEnumChanged` |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；delegate handle 捕获；`Engine.GetEnum` |
| 优先级 | P2 |

#### NewTest-16：补齐 `OnLiteralAssetReload` 的旧对象/新对象替换广播测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::OnLiteralAssetReload`；`__CreateLiteralAsset(UClass, const FString&)` |
| 现有测试覆盖 | 全仓 `AngelscriptTest` 对 `OnLiteralAssetReload` 和 literal asset reload 语法都是 0 命中 |
| 风险评估 | literal asset 在 class full reload 后如果没有正确广播 old/new object 替换，依赖 asset identity 迁移的编辑器和运行时观察者会静默失步，当前 CI 完全无感知 |
| 建议测试名 | `Angelscript.TestModule.HotReload.LiteralAsset.BroadcastsReloadedObjectReplacement` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadLiteralAssetTests.cpp` |
| 场景描述 | v1 脚本声明 `UCLASS() class ULiteralReloadAsset : UObject` 与 `asset ExampleAsset of ULiteralReloadAsset`；绑定 `OnLiteralAssetReload`；随后 full reload 到带新增 `UPROPERTY() int ExtraValue = 2;` 的 v2，使旧 asset class 带上 `CLASS_NewerVersionExists` 并触发 literal asset 重建 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::FullReload, ...)`；delegate handle 捕获；`FindObject<UObject>(FAngelscriptEngine::Get().AssetsPackage, TEXT("ExampleAsset"))` |
| 期望行为 | 断言 v1/v2 compile 都成功；`OnLiteralAssetReload` 触发 1 次；回调里的 old/new `UObject*` 都非空且 `Old != New`；旧对象名被改成 `REPLACED_ASSET_*` 或离开 canonical 名称；新对象仍以 `ExampleAsset` 可查到，并且新类上存在 `ExtraValue` 默认值 `2` |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；delegate handle 捕获；`FindObject<UObject>`；`ReadPropertyValue` |
| 优先级 | P1 |

#### NewTest-17：补齐 `UASClass` 源码路径 metadata 的公开 API 测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::GetSourceFilePath() const`；`UASClass::GetRelativeSourceFilePath() const` |
| 现有测试覆盖 | `Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` 与 `Editor/AngelscriptSourceNavigationTests.cpp` 只覆盖 `UASFunction` 的绝对路径/行号正路径；`UASClass` 两个路径 getter 在全仓没有 targeted test |
| 风险评估 | 一旦类级源码定位 metadata 回退，编辑器导航、错误提示和调试工具会把类跳转到错误文件，当前 suite 不会第一时间发现 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.SourcePathMetadataExposesAbsoluteAndRelativeFilenames` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassMetadataTests.cpp` |
| 场景描述 | 用相对文件名 `Automation/SourceMetadataTarget.as` 编译一个最小 `UCLASS()` 脚本类，并获取生成的 `UASClass*`；同时取一个同类 `UASFunction*` 作为对照 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindGeneratedClass()`；`FindGeneratedFunction()`；`FPaths::Combine(FPaths::ProjectSavedDir(), TEXT(\"Automation\"), TEXT(\"SourceMetadataTarget.as\"))` |
| 期望行为 | 断言 `Cast<UASClass>(Class)->GetSourceFilePath()` 返回保存到 `Saved/Automation` 下的绝对路径；`GetRelativeSourceFilePath()` 返回 `Automation/SourceMetadataTarget.as`；对照 `UASFunction::GetSourceFilePath()` 仍指向同一脚本文件，避免类级和函数级 metadata 分叉 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`FindGeneratedClass`；`FindGeneratedFunction`；`Cast<UASClass>`；`Cast<UASFunction>` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-27 |
| MissingCleanup | 1 | Issue-28 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |
| P2 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-08 23:54)

### 一、现有测试问题

#### Issue-31：`ScriptClass.RenameReplacesOldClass` 没有验证旧 canonical 名称和 module 记录是否真正退场

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.RenameReplacesOldClass` |
| 行号范围 | 639-688 |
| 问题描述 | 用例在 rename 后只验证了 `AScenarioScriptClassRenameNew` 能按新名字查到、`OldClass != NewClass`，以及旧类对象名发生变化。它没有对 `FindGeneratedClass(&Engine, TEXT("AScenarioScriptClassRenameOld"))` 做负向断言，也没有检查 module record 里是否还残留旧类描述。也就是说，只要新类成功生成、旧指针被改名，测试就会通过；即便旧 canonical 名仍可解析、module 同时暴露 old/new 两个 class entry，这个用例也发现不了。 |
| 影响 | rename/full reload 最关键的 contract 之一是 “旧名字彻底退场，新名字成为唯一 active entry”。当前断言强度不足，会漏掉旧类 lookup 残留、module record 污染和双注册问题；这类回归会直接破坏后续 `FindGeneratedClass()`、hot reload 替换以及版本链清理。 |
| 修复建议 | 在现有断言外补三类检查：1. `FindGeneratedClass(&Engine, TEXT("AScenarioScriptClassRenameOld")) == nullptr` 或等价失败路径；2. `Engine.GetModuleByModuleName(ModuleName.ToString())` 仍有效且只包含新类名，不再包含 `AScenarioScriptClassRenameOld`；3. 若产品语义要求旧类走版本链，补 `Cast<UASClass>(OldClass)->GetMostUpToDateClass() == NewClass`。这样才能把 rename replacement 的 lookup cleanup 和版本切换一起守住。 |

#### Issue-32：`NativeScriptHotReload.Phase2C` 直接读取工作区 fixture 文件，测试语义会随外部脚本漂移

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2C` |
| 行号范围 | 241-255 |
| 问题描述 | `Phase2C` 不像 `Phase2A/2B` 那样在测试文件里内联源码，而是直接从 `Script/Tests/Test_ExampleActorFixture.as` 读取当前工作区文件内容，再交给 `VerifyNativeScriptHotReloadInline()` 做 compile-only reload。这个用例没有钉住 fixture 里必须包含的类型、函数或行为契约，因此任何对 `Test_ExampleActorFixture.as` 的无关修改，都会在不改测试代码的情况下改变该测试实际覆盖的脚本形态。 |
| 影响 | 这会让测试覆盖范围和失败原因随外部 fixture 漂移，CI 绿灯不再稳定代表固定 contract。更糟的是，当前 harness 本来就只断言“重新编译成功”，一旦 fixture 被简化或换成别的内容，`Phase2C` 可能继续通过，但已不再覆盖原本想守住的 native actor fixture 场景。 |
| 修复建议 | 把 `Phase2C` 改成最小自包含脚本，至少把需要验证的 actor fixture 结构内联到测试里；如果必须复用仓库 fixture，就在读取后先断言关键 marker 存在，例如目标 `UCLASS` 名、核心 `UFUNCTION` 或属性名，再在 reload 前后执行这些行为级断言，避免 fixture 漂移把测试语义悄悄改掉。 |

### 二、需要新增的测试

#### NewTest-18：补齐 `UASClass::GetLifetimeScriptReplicationList()` 的脚本属性收集测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::GetLifetimeScriptReplicationList(TArray<FLifetimeProperty>& OutLifetimeProps) const` |
| 现有测试覆盖 | 全仓 `AngelscriptTest` 对该 API 为 0 命中；`Examples/AngelscriptScriptExampleCoverageTests.cpp` 只检查 actor 是否开启 replication，没有验证 `FLifetimeProperty` 列表内容 |
| 风险评估 | script `UPROPERTY(Replicated)` 或继承链上的 replicated 字段若没有进入 lifetime props，运行时网络复制会静默失效，当前 targeted suite 不会第一时间报警 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.LifetimeScriptReplicationListIncludesInheritedReplicatedProperties` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassReplicationTests.cpp` |
| 场景描述 | 在同一 module 中定义 `AReplicationParent` 与 `AReplicationChild : AReplicationParent`；父类和子类各声明一个 `UPROPERTY(Replicated)` 字段；编译后直接对 child `UASClass` 调用 `GetLifetimeScriptReplicationList()` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FindGeneratedClass()`；`FindFProperty<FProperty>()`；一个把 `RepIndex` 反查到属性名的 test-local helper |
| 期望行为 | 断言 child class 是 `UASClass`；`OutLifetimeProps` 至少包含父类 `ParentValue` 与子类 `ChildValue` 两个 `RepIndex`，且没有重复条目；如果再补一条 `RepNotify` 字段，对应 property 也必须出现在列表中 |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedClass`；`FindFProperty`；test-local `CollectReplicatedPropertyNamesFromLifetimeProps` |
| 优先级 | P1 |

#### NewTest-19：补齐 `FScopeSetDefaultConstructorOuter` / `GetDefaultConstructorOuter()` 的嵌套恢复测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::FScopeSetDefaultConstructorOuter`；`UASClass::GetDefaultConstructorOuter()` |
| 现有测试覆盖 | 全仓 `AngelscriptTest` 对 `FScopeSetDefaultConstructorOuter` 和 `GetDefaultConstructorOuter()` 为 0 命中 |
| 风险评估 | 构造 outer 作用域若不能正确嵌套恢复，script object 分配会把对象落到错误 outer 或长期保留脏线程局部状态；当前没有任何自动化锁住这条 API 契约 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.DefaultConstructorOuterScopeRestoresPreviousOuter` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassConstructionContextTests.cpp` |
| 场景描述 | 创建两个 transient outer 对象 `OuterA` / `OuterB`；在无 scope、单层 scope、双层嵌套 scope 三种状态下调用 `GetDefaultConstructorOuter()` |
| 输入/前置 | `NewObject<UObject>(GetTransientPackage())`；无需脚本编译；直接构造 `UASClass::FScopeSetDefaultConstructorOuter` |
| 期望行为 | baseline 断言 `GetDefaultConstructorOuter() == GetTransientPackage()`；进入 `OuterA` scope 后返回 `OuterA`；进入嵌套 `OuterB` scope 后返回 `OuterB`；内层析构后恢复到 `OuterA`；外层析构后恢复到 `GetTransientPackage()` |
| 使用的 Helper | `NewObject<UObject>`；`UASClass::FScopeSetDefaultConstructorOuter` |
| 优先级 | P2 |

#### NewTest-20：补齐 `UASClass::GetConstructingASObject()` 的脚本构造期上下文测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::GetConstructingASObject()`；`UASClass::AllocScriptObject()`；`UASClass::FinishConstructObject()` |
| 现有测试覆盖 | 全仓 `AngelscriptTest` 对 `GetConstructingASObject()` 为 0 命中；`DiscoveryPlans/ClassGenerator_Plan.md` 已指出该 API 与构造栈共享状态相关，但当前没有 targeted regression |
| 风险评估 | 如果构造期当前对象指针错乱，script ctor/defaults 会读取错误实例或跨对象串栈；这类问题通常只在运行时偶发崩溃或状态污染时才暴露 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.GetConstructingASObjectReportsCurrentScriptInstance` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassConstructionContextTests.cpp` |
| 场景描述 | 增加一个 test-local native probe helper，把 `UASClass::GetConstructingASObject()` 的返回值记录到静态指针；编译一个脚本 `UObject`，在构造函数或默认值阶段调用该 probe；随后实例化该脚本对象 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`NewObject<UObject>`；新建 `Shared/AngelscriptConstructionContextProbe.h/.cpp` 提供 `CaptureConstructingObject()` 与 `ResetCapturedObject()` |
| 期望行为 | 断言实例化成功；构造期间 probe 记录到的对象指针与最终返回的脚本实例相同；构造完成后再次调用 `UASClass::GetConstructingASObject()` 返回 `nullptr`；如果同一文件再补嵌套构造场景，外层对象不应被内层覆盖成错误实例 |
| 使用的 Helper | `CompileScriptModule`；`NewObject<UObject>`；`Shared/AngelscriptConstructionContextProbe` |
| 优先级 | P1 |

#### NewTest-21：补齐 `UASClass::RuntimeDestroyObject()` 的脚本析构回调测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::RuntimeDestroyObject(UObject* Object)` |
| 现有测试覆盖 | 全仓 `AngelscriptTest` 对 `RuntimeDestroyObject()` 为 0 命中；`ClassGenerator_Analysis.md` 与 `DiscoveryPlans/ClassGenerator_Plan.md` 都已指出析构路径是高风险区，但没有对应 targeted automation |
| 风险评估 | script 对象销毁时若没有进入 `RuntimeDestroyObject()`，脚本析构、debug cleanup 和资源解绑都会静默丢失；这类问题通常只会在 reload/GC 后以泄漏或悬空状态出现 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.RuntimeDestroyObjectInvokesScriptDestructorOnGC` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassLifecycleTests.cpp` |
| 场景描述 | 提供一个 test-local native lifecycle probe，暴露 `RecordDestructorCall()` 计数器；编译一个脚本 `UObject`，在析构函数或等价销毁路径中调用 probe；创建实例后释放强引用并触发 `CollectGarbage()` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`NewObject<UObject>`；`CollectGarbage(RF_NoFlags, true)`；新建 `Shared/AngelscriptLifecycleProbe.h/.cpp` |
| 期望行为 | 断言脚本实例创建成功；GC 前析构计数为 0；GC 后 probe 计数增加 1；若同文件再补 full reload 变体，旧实例在 reload 后被回收时也必须触发同一析构计数，避免旧对象跳过脚本销毁 |
| 使用的 Helper | `CompileScriptModule`；`NewObject<UObject>`；`CollectGarbage`；`Shared/AngelscriptLifecycleProbe` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-31 |
| AntiPattern | 1 | Issue-32 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | NoTestForSource: 3 |

---

## 测试审查 (2026-04-09 01:34)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-37：补齐 `UASClass::StaticComponentConstructor()` 的脚本 component 构造与 defaults 测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::StaticComponentConstructor(const FObjectInitializer&)`；`ExecuteConstructFunction()`；`ExecuteDefaultsFunctions()` |
| 现有测试覆盖 | 全仓现有 component 相关用例主要覆盖 `Component.BeginPlay`、`Component.Tick`、`Component.ReceiveEndPlay`、`Component.ActorOwner` 以及 `DefaultComponent.Basic/Multiple`；没有任何 targeted 断言直接锁住脚本 `UActorComponent` 在 `NewObject<UActorComponent>` 路径上会执行构造函数与 default statements，且只执行一次 |
| 风险评估 | 如果 `StaticComponentConstructor()` 漏跑脚本构造函数、重复执行 defaults，或 direct component instantiation 路径与 actor 默认组件路径语义分叉，当前 ClassGenerator/HotReload review 范围不会第一时间报警 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.StaticComponentConstructorAppliesScriptConstructorAndDefaultsOnce` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassComponentConstructionTests.cpp` |
| 场景描述 | 编译最小脚本 `UActorComponent`，定义 `CtorCount`、`DefaultValue`、`DefaultLabel` 三个属性，并在脚本构造函数里执行 `CtorCount += 1`、在 defaults 中写入 `DefaultValue = 9` 与 `DefaultLabel = "ComponentDefaults"`；创建一个 host actor 后通过 `NewObject<UActorComponent>(HostActor, ComponentClass)` 实例化两个组件 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FActorTestSpawner` 或最小 host actor helper；`NewObject<UActorComponent>`；`ReadPropertyValue<FIntProperty>` / `ReadPropertyValue<FStrProperty>` |
| 期望行为 | 断言生成类是 `UASClass` 且派生自 `UActorComponent`；component CDO 上 `DefaultValue == 9`、`DefaultLabel == "ComponentDefaults"`；两个实例上的 `CtorCount` 都各自为 `1`，不是跨实例累积；两个实例上的 `DefaultValue`/`DefaultLabel` 都等于 defaults 设定值，证明 `StaticComponentConstructor()` 在 direct component 路径上既执行构造，也正确应用 defaults，且没有重复执行 |
| 使用的 Helper | `CompileScriptModule`；`NewObject<UActorComponent>`；`FActorTestSpawner`；`ReadPropertyValue`；`Cast<UASClass>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 01:50)

### 一、现有测试问题

#### Issue-54：`ScriptClass.RenameReplacesOldClass` 绕开了 suite 统一的 fresh-engine helper，隔离级别低于同文件其他 7 个用例

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.RenameReplacesOldClass` |
| 行号范围 | 24-27, 630-636 |
| 问题描述 | 同文件前面专门定义了 `AcquireFreshScriptClassEngine()`，其中先调用 `DestroySharedAndStrayGlobalTestEngine()`，再拿 `AcquireCleanSharedCloneEngine()`；前 7 个 `ScriptClass` 用例也都统一走这条 fresh 路径。唯独 `RenameReplacesOldClass` 直接改用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`，并在退出时只做 `ResetSharedInitializedTestEngine(Engine)`。这意味着它不会像同组兄弟用例那样在进入测试前主动清掉 stray legacy/global engine，只依赖 shared clone reset。 |
| 影响 | rename/full reload 场景对 canonical 名称、`REPLACED_*` 残留和 detached class 状态最敏感；一旦前序测试或编辑器会话留下 stray global engine / old script objects，这条用例就更容易出现顺序相关结果。它会把“rename 替换逻辑回归”和“测试入口隔离不一致”混在一起，降低结论可信度。 |
| 修复建议 | 把该用例与同文件其他 7 条保持一致，直接切回 `ScriptClassCreationTest::AcquireFreshScriptClassEngine()`，退出时使用与其配套的 `ResetSharedCloneEngine(Engine)` 流程。若业务上必须保留 `SHARE_CLEAN`，至少要在进入测试前显式调用 `DestroySharedAndStrayGlobalTestEngine()`，并补一条 baseline 断言确认当前没有 stray global/shared modules 再开始 rename 场景。 |

### 二、需要新增的测试

#### NewTest-39：补齐 `AnalyzeEnums()` 的 enum value-only 变更分析测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::AnalyzeEnums()`；`FAngelscriptClassGenerator::Setup()` |
| 现有测试覆盖 | 当前 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` 只覆盖 class/property/function 维度；`HotReload.FullReload.EnumBasic` 只验证显式 `FullReload` 后的运行时结果。对 “enum 名字不变、底层整数值变化” 的分析决策目前是 0 命中。 |
| 风险评估 | 如果 `AnalyzeEnums()` 再次把 value-only 变更漏判成 `SoftReload`，CI 不会在分析层第一时间报警，问题只会拖到运行时 enum 映射或资产解释错乱时才暴露。 |
| 建议测试名 | `Angelscript.TestModule.HotReload.AnalyzeReload.EnumValueChange` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 场景描述 | 基线脚本声明 `UENUM(BlueprintType) enum class EReloadAnalysisState : uint16 { Alpha = 1, Beta = 4 }`，并加一个最小 carrier `UCLASS` 持有该 enum；v2 保持枚举成员名不变，只把 `Beta` 的值改成 `7`。先 full compile v1，再对 v2 调 `AnalyzeReloadFromMemory()`。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；显式 `ON_SCOPE_EXIT` 回收模块；`CompileAnnotatedModuleFromMemory()`；`AnalyzeReloadFromMemory()` |
| 期望行为 | 断言分析成功；`ReloadRequirement == FAngelscriptClassGenerator::FullReloadSuggested`；`bWantsFullReload == true`；`bNeedsFullReload == false`。如果后续 helper 暴露行号，也应补 `GetFullReloadLines()` 命中新枚举定义行，避免只锁住枚举值不锁住诊断来源。 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`AnalyzeReloadFromMemory`；`Engine.DiscardModule()` |
| 优先级 | P1 |

#### NewTest-40：补齐 delegate 签名变更的 hot reload 分析测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::Analyze(FModuleData&, FDelegateData&)`；`FAngelscriptClassGenerator::Setup()`；`ShouldFullReload(FDelegateData&)` |
| 现有测试覆盖 | 当前 gap 文档里只有 `OnDelegateReload` 的广播建议，没有任何 analysis 层用例去验证 “existing delegate 改签名时必须阻止 soft reload”。`AngelscriptHotReloadAnalysisTests.cpp` 对 delegate 维度是完全空白。 |
| 风险评估 | delegate 签名若被错误降级成可 soft reload，执行层就可能继续沿用旧 `UDelegateFunction` 壳；这类问题会直接污染 Blueprint/反射调用签名，而现有 analysis suite 不会给出早期信号。 |
| 建议测试名 | `Angelscript.TestModule.HotReload.AnalyzeReload.DelegateSignatureChange` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 场景描述 | 基线脚本声明 `delegate void FReloadAnalysisSignal(int Value);`，并让一个最小 `UCLASS` 持有该 delegate 属性；v2 把签名改成 `delegate void FReloadAnalysisSignal(int Value, int Tag);`。先 full compile v1，再对 v2 调 `AnalyzeReloadFromMemory()`。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；显式 `ON_SCOPE_EXIT` 回收模块；`CompileAnnotatedModuleFromMemory()`；`AnalyzeReloadFromMemory()` |
| 期望行为 | 断言分析成功；`ReloadRequirement == FAngelscriptClassGenerator::FullReloadRequired`；`bWantsFullReload == true`；`bNeedsFullReload == true`。如后续暴露 full-reload line 信息，应进一步断言命中 delegate 声明行，避免把“需要 full reload”与“定位到正确签名变更位置”分离。 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`AnalyzeReloadFromMemory`；`Engine.DiscardModule()` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-54 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |

---

## 测试审查 (2026-04-09 01:31)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-35：补齐脚本类“仅函数无属性”结构矩阵测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::FinalizeClass()`；`FAngelscriptClassGenerator::FinalizeObjectClass()`；`UASFunction::AllocateFunctionFor()` |
| 现有测试覆盖 | 当前 `ScriptClassCreation` 已覆盖“带属性 actor”“Blueprint child”“CDO 默认值”“脚本类重编译/改名”，也已规划空类与脚本继承补测，但还没有任何 targeted 用例验证“仅声明 `UFUNCTION()`、不含 `UPROPERTY()`/继承特例”的最小脚本类能稳定生成并执行 |
| 风险评估 | 如果类生成器未来错误依赖属性布局或默认值路径，function-only script class 可能编译通过却不生成可执行 `UFunction`；当前 suite 会把这类结构回归完全漏掉 |
| 建议测试名 | `Angelscript.TestModule.ScriptClass.FunctionOnlyClassCompilesAndExecutes` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassStructureTests.cpp` |
| 场景描述 | 编译最小脚本 `UCLASS() class UFunctionOnlyScriptClass : UObject`，仅声明 `UFUNCTION() int GetValue() { return 17; }`；随后实例化对象并执行该 generated function |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FindGeneratedFunction()`；`NewObject<UObject>`；`ExecuteGeneratedIntEventOnGameThread()` |
| 期望行为 | 断言生成结果是 `UASClass`；`FindGeneratedFunction(Class, TEXT("GetValue"))` 非空；`NewObject<UObject>(GetTransientPackage(), Class)` 成功；执行 `GetValue()` 返回 `17`；并补一个负向断言，确认类上不存在测试脚本未声明的用户属性，避免误把“带属性路径”当成 function-only 覆盖 |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedFunction`；`NewObject<UObject>`；`ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 01:33)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-36：补齐 `UASStruct::UpdateScriptType()` 在移除 `opEquals/Hash` 后的能力位清理测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp` |
| 关联函数 | `UASStruct::UpdateScriptType()`；`UASStruct::PrepareCppStructOps()`；`UASStruct::GetToStringFunction()` |
| 现有测试覆盖 | 当前 gap 文档已规划 “有 `opEquals/Hash/ToString` 时 `CppStructOps` 能暴露能力”，但全仓 `Plugins/Angelscript/Source/AngelscriptTest` 对 `STRUCT_IdenticalNative`、`CPF_HasGetValueTypeHash`、`GetCppStructOps()` 相关断言为 0 命中，完全没有用例覆盖 reload 后删除这些方法的回收路径 |
| 风险评估 | 如果 struct reload 后 `Equals/Hash` 已从脚本中删除，但 `StructFlags` 或 `CppStructOps` 能力位仍残留旧状态，UE 比较/哈希会继续沿用过期能力，当前 suite 不会报警 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASStruct.UpdateScriptTypeClearsIdenticalAndHashCapabilitiesAfterReload` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptStructHotReloadTests.cpp` |
| 场景描述 | 先编译 `FReloadableCapabilityStructV1`，定义 `bool opEquals(...) const`、`uint32 Hash() const`、`FString ToString() const`；拿到 `UASStruct* StructV1` 并触发一次能力断言。随后对同名 struct 做 full reload，v2 只保留字段和 `ToString()`，删除 `opEquals/Hash` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::FullReload, ...)`；`FindObject<UScriptStruct>`；`Cast<UASStruct>` |
| 期望行为 | v1 阶段断言 `Struct->GetCppStructOps() != nullptr`、`Struct->StructFlags` 含 `STRUCT_IdenticalNative`、`Struct->GetToStringFunction() != nullptr`，且 `Struct->StructFlags`/property flags 暴露哈希能力；reload 到 v2 后，断言 `Cast<UASStruct>(OldStruct)->GetNewestVersion() == StructV2`，`StructV2->GetToStringFunction() != nullptr` 但 `STRUCT_IdenticalNative` 已被清掉，相关哈希能力位不再残留旧值，避免只验证“有能力时能开”而不验证“删除后能关” |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`FindObject<UScriptStruct>`；`Cast<UASStruct>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 00:07)

### 一、现有测试问题

#### Issue-33：`ScriptClass.MultiSpawnKeepsStateIsolation` 没有验证实例写入后是否污染 CDO 和后续 spawn

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.MultiSpawnKeepsStateIsolation` |
| 行号范围 | 291-323 |
| 问题描述 | 用例先连续 spawn `FirstActor` 和 `SecondActor`，然后才把 `FirstActor.LocalState` 改成 `11`，最后断言第二个已存在实例仍为默认值 `3`。这只能证明两个已经构造完成的实例不是同一块内存；如果类生成器把实例写入错误地回写到 CDO、spawn template 或后续实例初始化基线，当前测试依然会通过，因为 `SecondActor` 是在污染发生前创建的。 |
| 影响 | 一旦实例修改回流到 CDO 或默认模板，真正受影响的通常是“后续新建实例”和 `GetDefaultObject()`，而不是已经 spawn 完成的第二个 actor。当前用例会把这类默认值污染、构造基线串写问题误报成绿灯。 |
| 修复建议 | 在现有断言外补两步：1. 修改 `FirstActor` 后立即读取 `ScriptClass->GetDefaultObject()` 上的 `LocalState`，要求仍为 `3`；2. 再 spawn 一个 `ThirdActor`，断言其 `LocalState` 仍为 `3`。这样才能把“实例隔离”“CDO 不被污染”“后续 spawn 不串值”三条 contract 一次锁住。 |

#### Issue-34：`ScriptClass.BlueprintChildCompiles` 用一个过弱的 helper 判定 Blueprint 编译成功

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptScriptClassCreationTests.cpp` |
| 测试名 | `Angelscript.TestModule.ScriptClass.BlueprintChildCompiles` |
| 行号范围 | 71-75, 363-373 |
| 问题描述 | `CompileAndValidateBlueprint()` 只调用 `FKismetEditorUtilities::CompileBlueprint(&Blueprint)`，随后用 `Blueprint.GeneratedClass.Get() != nullptr` 作为“编译成功”的唯一条件。这个判断过弱：一旦 Blueprint 重新编译失败但保留了上一轮的 `GeneratedClass`，helper 仍会返回成功，`BlueprintChildCompiles` 后面的 spawn/BeginPlay 断言也可能继续落在旧 class 上。 |
| 影响 | 该用例看起来在验证“脚本父类可被 Blueprint child 正常编译并继承”，但实际上只锁住了“某个 generated class 指针仍然存在”。Blueprint 真实编译失败、状态退化到 error/dirty、或沿用陈旧生成类时，测试可能继续绿灯。 |
| 修复建议 | 把 helper 收紧成真正的 Blueprint compile 验证：编译后同时断言 `Blueprint.Status != BS_Error`、`Blueprint.GeneratedClass != nullptr`、`Blueprint.GeneratedClass->ClassGeneratedBy == &Blueprint`，必要时再检查 `Blueprint.GeneratedClass->GetSuperClass() == ParentClass`。如果想保持 helper 通用性，可让 `CompileAndValidateBlueprint()` 返回这些更强的结果，而不是只看一个非空指针。 |

#### Issue-35：`HotReload.AnalyzeReload.ClassAdded` 没有把 “suggested” 与 “required” 的布尔语义锁死

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AnalyzeReload.ClassAdded` |
| 行号范围 | 259-270 |
| 问题描述 | 该用例对新增 `UNewReloadTarget` 的断言只有两层：`ReloadRequirement == FullReloadSuggested`，以及 `bWantsFullReload || bNeedsFullReload`。这意味着只要枚举值仍是 `FullReloadSuggested`，即便 analyzer 错把 `bNeedsFullReload` 也置成 `true`，或者把 “建议”/“必须” 两个布尔位同时拉高，测试依然通过。 |
| 影响 | `AnalyzeReloadFromMemory()` 的结果往往会被上层 UI、命令行和 compile wrapper 同时消费；这些调用点未必都只看 `ReloadRequirement` 枚举。如果布尔语义漂移，用户可能被错误告知“必须 full reload”，或者自动化分支走到更保守的路径，而这条测试发现不了。 |
| 修复建议 | 把断言补完整为：`ReloadRequirement == FAngelscriptClassGenerator::FullReloadSuggested`、`bWantsFullReload == true`、`bNeedsFullReload == false`。如果 helper 能返回 line diagnostics，再补一条“新增类声明所在行被标进 full reload 建议列表”的断言，避免 future refactor 把建议原因也丢掉。 |

### 二、需要新增的测试

#### NewTest-22：补齐 `UASClass::GetContainerSize()` / `ScriptPropertyOffset` 的布局元数据测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASClass::GetContainerSize() const`；`UASClass::ScriptPropertyOffset` |
| 现有测试覆盖 | 当前 `ClassGenerator/`、`HotReload/` 只验证属性值和类切换结果；全仓实际测试源码对 `GetContainerSize()` 与 `ScriptPropertyOffset` 都是 0 命中 |
| 风险评估 | 这两个布局元数据直接决定脚本对象尾部存储、hot reload 清零区间和默认组件变量偏移；一旦回归，现有 suite 只会在更晚的构造/析构或热重载异常里被动暴露 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.ContainerSizeAndScriptPropertyOffsetFollowInheritanceLayout` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassLayoutTests.cpp` |
| 场景描述 | 编译 `UCLASS() class ULayoutParent : UObject`，含一个 `int ParentValue`；再编译 `UCLASS() class ULayoutChild : ULayoutParent`，新增 `double ChildValue` 与 `bool bChildFlag`。拿到 parent/child 的 `UASClass*` 并读取其布局字段 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule` 或 `CompileAnnotatedModuleFromMemory()`；`FindGeneratedClass()`；`Cast<UASClass>()` |
| 期望行为 | 断言 `ParentASClass`、`ChildASClass` 都有效且 `ChildClass->IsChildOf(ParentClass)`；`ParentASClass->GetContainerSize() > 0`；`ChildASClass->GetContainerSize() > ParentASClass->GetContainerSize()`；`ChildASClass->ScriptPropertyOffset == ParentASClass->GetPropertiesSize()`；`ChildASClass->GetContainerSize() >= ChildASClass->GetPropertiesSize()`；child CDO 仍能同时读到 `ParentValue` 与 `ChildValue` 默认值，证明布局元数据和反射壳一致 |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedClass`；`Cast<UASClass>`；`ReadPropertyValue` |
| 优先级 | P1 |

#### NewTest-23：补齐 `UASFunction::GetRuntimeValidateFunction()` 的 `_Validate` 缓存测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASFunction::GetRuntimeValidateFunction()` |
| 现有测试覆盖 | 当前 `ClassGenerator/`、`HotReload/` 范围内没有任何 RPC `WithValidation` 场景；全仓实际测试源码对 `GetRuntimeValidateFunction()` 为 0 命中 |
| 风险评估 | 如果 `_Validate` 函数没有被缓存到 generated `UASFunction`，运行时 RPC 校验会退化成绕过验证或在调用时拿到空指针；这类问题通常只会在网络路径上晚期暴露 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASFunction.NetValidateCachesValidateFunction` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionMetadataTests.cpp` |
| 场景描述 | 编译一个最小 `AActor` 脚本类，声明 `UFUNCTION(Server, Reliable, WithValidation) void Server_SetValue(int Value)`，并提供 `UFUNCTION() bool Server_SetValue_Validate(int Value)`；随后查询主 RPC 函数与 `_Validate` 函数 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FindGeneratedClass()`；`FindGeneratedFunction()`；`Cast<UASFunction>()` |
| 期望行为 | 断言主 RPC 函数存在且 `FunctionFlags` 含 `FUNC_Net` 与 `FUNC_NetValidate`；`Cast<UASFunction>(ServerFunction)->GetRuntimeValidateFunction()` 非空；返回值与 `FindGeneratedFunction(Class, TEXT("Server_SetValue_Validate"))` 相同；`_Validate` 函数返回类型为 `bool` 且参数签名与主 RPC 一致，避免只缓存了名字没缓存到正确函数 |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedClass`；`FindGeneratedFunction`；`Cast<UASFunction>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-34 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-09 01:06)

### 一、现有测试问题

#### Issue-51：`HotReload.AddModifyLookupFlow` 在断言路径里手动 `DiscardModule`，与 `ON_SCOPE_EXIT` 形成双重清理

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.AddModifyLookupFlow` |
| 行号范围 | 338-341, 411-413 |
| 问题描述 | 用例已经在 `ON_SCOPE_EXIT` 中注册了 `Engine.DiscardModule(*ModuleName.ToString())`，但在主断言路径末尾又手动调用了一次 `Engine.DiscardModule(*ModuleName.ToString())`，随后只检查 `GetModuleByModuleName(...)` 是否失效。这样测试实际上对同一 module 走了两次 discard：第一次作为业务断言，第二次作为隐藏的退出清理，而且第二次的返回值与副作用都没有被显式验证。 |
| 影响 | 退出路径语义会变得模糊：如果第一次 discard 已经足够，第二次只是噪声；如果第二次会产生日志/诊断，它会在测试绿灯时被静默吞掉。更关键的是，这条用例把“discard 成功”与“lookup 已清空”混成了一件事，没有稳定锁住真实 cleanup contract。 |
| 修复建议 | 只保留一条 cleanup 路径。若要把 discard 作为用例断言，就显式保存 `const bool bDiscarded = Engine.DiscardModule(...)` 并 `TestTrue` 其返回值，同时让 `ON_SCOPE_EXIT` 受一个 `bModuleDiscarded` 标志保护，避免二次调用；若只关心退出清理，则删掉手动 discard，把最终断言改成其他 reload/lookup 行为。 |

#### Issue-52：`HotReloadPerformanceTests` 的 `metrics.json` 只记录时延，不记录每个样本的 `CompileResult`

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.Performance.*` |
| 行号范围 | 36-75, 227-236, 311-319 |
| 问题描述 | `FHotReloadPerformanceSample` 明明同时保存了 `ReloadSeconds` 和 `CompileResult`，但 `WriteHotReloadMetrics()` 在序列化时只把 `ReloadSeconds` 抽成 `Durations` 写入 `metrics.json`。对于已经允许 `Error` / `ErrorNeedFullReload` 通过的 `RenameWindowLatency`、`BurstChurnLatency` 来说，最终产物完全看不出某个时延样本对应的是成功 reload、降级路径还是错误路径。 |
| 影响 | 一旦某次回归把“成功路径时延”变成“失败路径更快”，当前 artifact 反而会把更小的秒数当成正常性能数据保存下来，后续人工回看也无法追溯样本质量。结果是性能数据既不能做门禁，也很难做事后解释。 |
| 修复建议 | 扩展 `WritePerformanceMetricsArtifact` 的调用数据，把每个 sample 的 `CompileResult` 一并写进 artifact，至少补 `handled_count`、`error_count`、`error_need_full_reload_count` 和逐样本状态；若当前 artifact schema 不支持，先在 `Notes` 或旁路 JSON 里追加结构化状态信息，并让性能用例在写 metrics 前先过滤掉非成功路径样本。 |

#### Issue-53：`VerifyNativeScriptHotReloadInline` 从不验证每轮 `DiscardModule` 是否成功，production engine 清理结果完全不可见

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2A` / `Phase2B` / `Phase2C` |
| 行号范围 | 27-33, 59-61 |
| 问题描述 | `VerifyNativeScriptHotReloadInline()` 在每个 inline script 迭代里只放了一个 `ON_SCOPE_EXIT { Engine.DiscardModule(*ModuleName.ToString()); };`，但无论 initial compile、soft reload 还是本轮退出，都没有任何地方检查这次 discard 的返回值或后续 module lookup。由于这些用例运行在 `RequireRunningProductionEngine()` 上，一旦某轮 discard 没有真正把 module 清干净，后续脚本和后续测试会直接继承这份 live 状态。 |
| 影响 | 当前 helper 看起来在做“每脚本独立 hot reload 验证”，实际却没有把每轮 cleanup contract 锁住。只要 `DiscardModule` 在 production engine 上偶发失败、留下 diagnostics 或 module record，Phase 内后续脚本可能受到污染，而测试日志仍然只显示 compile/reload 通过。 |
| 修复建议 | 把 cleanup 从纯 `ON_SCOPE_EXIT` 改成可断言的显式步骤：在每轮末尾调用 `const bool bDiscarded = Engine.DiscardModule(...)`，随后 `TestTrue` 它为 `true`，并再断言 `Engine.GetModuleByModuleName(ModuleName.ToString()).IsValid() == false`。如果仍要保留 scope guard，至少让它只做兜底，并在循环体内先完成可见的 cleanup 断言。 |

### 二、需要新增的测试

#### NewTest-32：补齐 `UASClass::StaticObjectConstructor()` 的脚本构造函数与 defaults 应用测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::StaticObjectConstructor(const FObjectInitializer&)`；`UASClass::AllocScriptObject()`；`ExecuteConstructFunction()`；`ExecuteDefaultsFunctions()` |
| 现有测试覆盖 | 当前 `ClassGenerator/` 与 `HotReload/` 只有 `ScriptClass.NonUClassTypeCannotSpawn`、`DiscardAndRecompile`、`FullReload.Basic` 等用例会 `NewObject<UObject>`，但都只验证对象能创建或属性默认值，不验证 plain `UObject` 脚本类的构造函数与 default statements 是否各执行一次 |
| 风险评估 | 如果 `StaticObjectConstructor()` 漏跑脚本构造函数、漏跑 defaults，或在 `NewObject<UObject>` 路径上重复执行，当前 suite 只会把它当成“对象能实例化”而放过，最终以默认值错乱或重复副作用的形式晚期暴露 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.StaticObjectConstructorAppliesScriptConstructorAndDefaultsOnce` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassObjectConstructionTests.cpp` |
| 场景描述 | 编译一个最小脚本 `UObject`：构造函数里 `CtorCount += 1`，默认语句里设置 `DefaultValue = 7` 与 `DefaultLabel = "ObjectDefaults"`；随后分别读取 CDO 和两个 `NewObject<UObject>` 实例上的这些属性 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`NewObject<UObject>(GetTransientPackage(), Class)`；`ReadPropertyValue<FIntProperty>` / `ReadPropertyValue<FStrProperty>` |
| 期望行为 | 断言 generated class 是 `UASClass`；CDO 上 `DefaultValue == 7`、`DefaultLabel == "ObjectDefaults"`；每个实例上 `CtorCount == 1`、`DefaultValue == 7`、`DefaultLabel == "ObjectDefaults"`；第二个实例的 `CtorCount` 仍为 `1` 而不是累计值，确保 `StaticObjectConstructor()` 没有把脚本构造状态泄漏到后续实例 |
| 使用的 Helper | `CompileScriptModule`；`NewObject<UObject>`；`ReadPropertyValue`；`Cast<UASClass>` |
| 优先级 | P1 |

#### NewTest-33：补齐 `UASFunctionNativeThunk()` / `RuntimeCallFunction()` 的 `ProcessEvent` 桥接测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASFunctionNativeThunk(UObject*, FFrame&, RESULT_DECL)`；`UASFunction::RuntimeCallFunction(UObject*, FFrame&, RESULT_DECL)` |
| 现有测试覆盖 | 全仓 `Plugins/Angelscript/Source/AngelscriptTest` 对 `UASFunctionNativeThunk` 和 `RuntimeCallFunction` 都是 0 命中；现有 helper `ExecuteGeneratedIntEventOnGameThread()` 在脚本函数路径只直接调用 `RuntimeCallEvent()`，没有经过 `ProcessEvent`/native thunk |
| 风险评估 | 一旦 BPVM/`ProcessEvent` 桥接断掉，脚本 `UFUNCTION` 可能在原生或 Blueprint 调用链上失效，但当前 hot reload 与 class generator suite 仍会因为 `RuntimeCallEvent()` 直连路径正常而误报绿灯 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASFunction.ProcessEventDispatchesThroughNativeThunk` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionProcessEventTests.cpp` |
| 场景描述 | 编译脚本 `UObject`，声明 `UFUNCTION() int AddTen(int Input)` 与 `UFUNCTION() void SetStoredValue(int Input)`；实例化对象后，使用 `FStructOnScope` 或等价反射参数缓冲区，通过 `Object->ProcessEvent()` 调用这两个生成函数 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FindGeneratedFunction()`；`NewObject<UObject>`；`FStructOnScope`；`FindFProperty<FIntProperty>` |
| 期望行为 | 断言 `Cast<UASFunction>(AddTenFunction)` 与 `Cast<UASFunction>(SetStoredValueFunction)` 都非空；向 `AddTen` 传入 `5` 后 `ProcessEvent` 返回 `15`；向 `SetStoredValue` 传入 `17` 后对象上的 `StoredValue == 17`。这能直接锁住 `UASFunctionNativeThunk -> RuntimeCallFunction` 的 BPVM/native 调用桥，而不是只验证 `RuntimeCallEvent()` 快捷路径 |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedFunction`；`NewObject<UObject>`；`FStructOnScope`；`ReadPropertyValue` |
| 优先级 | P1 |

#### NewTest-34：补齐 `UASFunction::OptimizedCall_*` 直接调用包装器测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASFunction::OptimizedCall(UObject*)`；`OptimizedCall_ByteReturn(UObject*)`；`OptimizedCall_FloatArg(UObject*, float)`；`OptimizedCall_DoubleArg(UObject*, double)`；`OptimizedCall_RefArg(UObject*, void*)`；`OptimizedCall_RefArg_ByteReturn(UObject*, void*)` |
| 现有测试覆盖 | 全仓 `Plugins/Angelscript/Source/AngelscriptTest` 对这些 `OptimizedCall_*` 包装器为 0 命中；已有 `NewTest-30` 只规划 `FinalizeArguments()` 与派生 `UASFunction_*` 子类选择，还没有任何建议直接验证公开优化调用入口的参数/返回值语义 |
| 风险评估 | 如果公开的优化包装器与 `RuntimeCallEvent()`/真实脚本语义发生偏差，使用这些 fast path 的运行时代码会直接读错返回值或写坏引用参数，而当前 suite 没有任何 targeted regression 能在第一时间报警 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASFunction.OptimizedCallWrappersPreserveArgumentsAndReturnValues` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionOptimizedCallTests.cpp` |
| 场景描述 | 编译 `UOptimizedCallTarget`，提供五类脚本函数：无参 `Ping()`、`uint8 GetByteCode()`、`void StoreFloat(float InValue)`、`void StoreDouble(double InValue)`、`void BumpRef(int& Value)`、`uint8 BumpRefAndReturn(int& Value)`；实例化对象后直接对各自的 `UASFunction*` 调用对应 `OptimizedCall_*` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FindGeneratedFunction()`；`Cast<UASFunction>()`；`NewObject<UObject>`；test-local `int32 RefValue` 变量；`ReadPropertyValue` |
| 期望行为 | `OptimizedCall()` 让对象上的 `PingCount` 从 `0` 变 `1`；`OptimizedCall_ByteReturn()` 返回脚本定义的 `7`；`OptimizedCall_FloatArg(1.25f)` 后 `LastFloat` 为 `1.25f`；`OptimizedCall_DoubleArg(2.5)` 后 `LastDouble` 为 `2.5`；`OptimizedCall_RefArg()` 与 `OptimizedCall_RefArg_ByteReturn()` 都会按脚本逻辑修改传入的 `RefValue`，并把对象上的观测属性与返回值同步更新。必要时可再对照一次 `RuntimeCallEvent()` 结果，确认优化包装器和通用路径语义一致 |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedFunction`；`Cast<UASFunction>`；`NewObject<UObject>`；`ReadPropertyValue` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-53 |
| AntiPattern | 2 | Issue-52 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | NoTestForSource: 3 |

---

## 测试审查 (2026-04-09 01:36)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-38：补齐 `UASClass::StaticActorConstructor()` 的脚本 actor 构造与 defaults 测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASClass::StaticActorConstructor(const FObjectInitializer&)`；`ExecuteConstructFunction()`；`ExecuteDefaultsFunctions()` |
| 现有测试覆盖 | 当前 `ScriptClass.CompilesToUClass`、`CanSpawnInTestWorld`、`CDOHasExpectedDefaults` 与 `HotReload.PropertyPreserved` 都会创建 actor 或读取 actor 默认值，但没有任何 targeted 用例把“脚本 actor 构造函数会执行、defaults 会应用、且每个实例只执行一次”锁成独立 contract |
| 风险评估 | 如果 `StaticActorConstructor()` 在 actor spawn 路径上跳过脚本构造函数、重复应用 defaults，或把上一个实例的构造副作用泄漏到下一个实例，现有 suite 多半只会在更晚的行为差异里被动暴露 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.StaticActorConstructorAppliesScriptConstructorAndDefaultsOnce` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassActorConstructionTests.cpp` |
| 场景描述 | 编译最小脚本 `AActor`，声明 `CtorCount`、`DefaultValue`、`DefaultLabel` 三个属性；在脚本构造函数里执行 `CtorCount += 1`，在 defaults 中写入 `DefaultValue = 11` 与 `DefaultLabel = "ActorDefaults"`；随后在同一 test world 内连续 spawn 两个 actor |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FActorTestSpawner`；`SpawnScriptActor`；`ReadPropertyValue<FIntProperty>` / `ReadPropertyValue<FStrProperty>` |
| 期望行为 | 断言生成类是 `UASClass` 且派生自 `AActor`；CDO 上 `DefaultValue == 11`、`DefaultLabel == "ActorDefaults"`；两个实例上的 `CtorCount` 都各自为 `1`；两个实例上的 `DefaultValue`/`DefaultLabel` 都继承 defaults 值；第二个实例不会继承第一个实例的构造副作用，证明 `StaticActorConstructor()` 在 spawn 路径上构造与 defaults 语义稳定 |
| 使用的 Helper | `CompileScriptModule`；`FActorTestSpawner`；`SpawnScriptActor`；`ReadPropertyValue`；`Cast<UASClass>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 02:06)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 01:57)` 的 `Issue-55`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 02:02)` 的 `NewTest-41` 与 `## 测试审查 (2026-04-09 02:04)` 的 `NewTest-42`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-55 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

---

## 测试审查 (2026-04-09 11:21)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 11:16)` 的 `Issue-56`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 11:18)` 的 `NewTest-43`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-56 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 11:36)

### 一、现有测试问题

本轮末尾校正：本轮未新增现有测试问题；新增测试建议已记录在上文 `## 测试审查 (2026-04-09 11:28)`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 11:28)` 的 `NewTest-44` 与 `NewTest-45`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 11:53)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 11:39)` 的 `Issue-57` 与 `## 测试审查 (2026-04-09 11:41)` 的 `Issue-58`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 11:46)` 的 `NewTest-46`、`NewTest-47` 与 `## 测试审查 (2026-04-09 11:49)` 的 `NewTest-48`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-57 |
| WrongHelper | 1 | Issue-58 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingScenario: 1 |
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |

---

## 测试审查 (2026-04-09 12:02)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 11:54)` 的 `Issue-59`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 11:54)` 的 `NewTest-49`、`NewTest-50` 与 `NewTest-51`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-59 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 12:06)

### 一、现有测试问题

#### Issue-60：`HotReload.FunctionChange` 只观察函数返回值，没有任何 live actor 生命周期观测点

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.FunctionChange` |
| 行号范围 | 250-335 |
| 问题描述 | `ScriptV1` / `ScriptV2` 都只定义了一个 `GetValue()`，测试在 reload 前后唯一观察点就是同一个 `Actor` 上 `GetValue()` 的返回值从 `1` 变成 `2`。脚本里没有任何 `UPROPERTY` 计数器、`BeginPlay` / ctor 副作用或 world 归属观测，因此它完全看不出 soft reload 过程中是否错误地重跑了 lifecycle、重建了 live actor，或者把实例状态悄悄重置后再继续返回新函数值。 |
| 影响 | 只要 reload 后还能在某个可调用对象上拿到 `2`，该用例就会通过；即便 soft reload 误触发 `BeginPlay`、替换了实例，或清空了 actor 上已有状态，CI 也不会报警。这让“live actor 上函数体热更新是否稳定”这条 contract 仍然缺少真实保护。 |
| 修复建议 | 把场景脚本扩成带状态观测的 live actor：增加 `UPROPERTY()` 的 `BeginPlayCount` / `PersistentCounter`，在 reload 前显式写入状态并执行一次 `BeginPlay`。reload 后除验证 `GetValue()` 返回 `2` 外，再断言 `Actor` 指针和 world 未变化、`BeginPlayCount` 没有从 `1` 变成 `2`、`PersistentCounter` 仍保留 reload 前值。若产品语义要求 class 指针保持不变，也应一并断言 `Actor->GetClass() == ClassAfterReload` 或等价版本链关系。 |

### 二、需要新增的测试

#### NewTest-52：补齐 live actor soft reload 不重放 `BeginPlay` / 不重置实例状态的场景测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::DoSoftReload()`；`UASClass::StaticActorConstructor(const FObjectInitializer&)` |
| 现有测试覆盖 | 当前 `HotReload.PropertyPreserved` 只检查旧实例属性保值，`HotReload.FunctionChange` 只检查返回值变化；没有任何用例锁住 soft reload 不会对已 `BeginPlay` 的 live actor 重放 lifecycle 或清空实例状态 |
| 风险评估 | 如果 soft reload 误重建 actor、重跑 `BeginPlay` 或把实例值回滚到 defaults，现有 scenario suite 仍可能继续绿灯，直到用户在 PIE 中遇到状态重复初始化或行为抖动 |
| 建议测试名 | `Angelscript.TestModule.HotReload.SoftReload.DoesNotReplayBeginPlayOnLiveActor` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadLifecycleTests.cpp` |
| 场景描述 | 编译 `AHotReloadLifecycleTarget`，声明 `UPROPERTY() int BeginPlayCount = 0;`、`UPROPERTY() int PersistentCounter = 0;`，在 `BeginPlay()` 中 `BeginPlayCount += 1`，`GetValue()` 初版返回 `PersistentCounter`，v2 改成返回 `PersistentCounter + 1`。spawn actor、显式 `BeginPlay`、把 `PersistentCounter` 设为 `41` 后执行 `SoftReloadOnly` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule`；`FActorTestSpawner`；`SpawnScriptActor`；`BeginPlayActor`；`CompileModuleWithResult(..., ECompileType::SoftReloadOnly, ...)`；`ReadPropertyValue`；`ExecuteGeneratedIntEventOnGameThread` |
| 期望行为 | reload 前断言 `BeginPlayCount == 1`、`PersistentCounter == 41`、`!Actor->IsPendingKill()`；reload 后断言 `Actor` 指针和 `Actor->GetWorld()` 不变、`Actor->HasActorBegunPlay()` 仍为 `true`、`BeginPlayCount` 仍为 `1`、`PersistentCounter` 仍为 `41`，并且 `GetValue()` 返回 `42`。必要时再断言 `Actor->GetClass()` 仍等于 reload 后 lookup 到的最新 class，避免只守住数值不守住 live class continuity |
| 使用的 Helper | `CompileScriptModule`；`FActorTestSpawner`；`SpawnScriptActor`；`BeginPlayActor`；`CompileModuleWithResult`；`ReadPropertyValue`；`ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

#### NewTest-53：补齐 `UASClass` 派生字段与 `UClass` 基类脚本身份位同步测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASClass::bIsScriptClass`；`UASClass::ScriptTypePtr`；`FAngelscriptClassGenerator::DoSoftReload()` |
| 现有测试覆盖 | 现有 `ScriptClass.CompilesToUClass`、`HotReload.SoftReload.Basic`、`HotReload.FunctionChange` 只验证行为或 `Cast<UASClass>` 是否成功；全仓 `Plugins/Angelscript/Source/AngelscriptTest` 对 `bIsScriptClass`、`ScriptTypePtr` 以及 `static_cast<UClass*>(AsClass)` 视角下的同名字段都是 0 命中 |
| 风险评估 | `ClassGenerator_Analysis.md` 已指出 `UASClass` 与 `UClass` 双份脚本身份字段存在分裂风险。若派生类视角有值、基类视角仍是空值，引擎级初始化、析构和复制路径都会在 CI 绿灯下静默读错状态 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.ScriptIdentityFieldsStayInSyncWithUClassBase` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASGeneratedTypeIdentityTests.cpp` |
| 场景描述 | 编译 `UIdentitySyncTarget : UObject`，读取 `UASClass* AsClass = Cast<UASClass>(GeneratedClass)`，同时保存 `UClass* BaseClassView = static_cast<UClass*>(AsClass)`；随后做一次 body-only soft reload，把 `GetValue()` 从返回 `1` 改到返回 `2`，再次读取同一类对象的两套字段 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileScriptModule` 或 `CompileAnnotatedModuleFromMemory`；`FindGeneratedClass`；`Cast<UASClass>`；`CompileModuleWithResult(..., ECompileType::SoftReloadOnly, ...)`；`ExecuteGeneratedIntEventOnGameThread` |
| 期望行为 | 初次编译后断言 `AsClass != nullptr`、`AsClass->bIsScriptClass == true`、`AsClass->ScriptTypePtr != nullptr`，并且 `BaseClassView->bIsScriptClass == true`、`BaseClassView->ScriptTypePtr == AsClass->ScriptTypePtr`。soft reload 后再断言类实例仍为同一指针，且基类/派生类两套 `bIsScriptClass` 与 `ScriptTypePtr` 仍同步非空；最后执行 `GetValue()` 得到 `2`，证明同步字段对应的是 live class 而不是 stale metadata |
| 使用的 Helper | `CompileScriptModule`；`FindGeneratedClass`；`Cast<UASClass>`；`CompileModuleWithResult`；`ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P0 |

#### NewTest-54：补齐 `UASStruct` 脚本身份字段在 full reload 前后的初始化与清理测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASStruct::bIsScriptStruct`；`UASStruct::ScriptType`；`UASStruct::GetNewestVersion()`；`FAngelscriptClassGenerator::DoFullReloadStruct()` |
| 现有测试覆盖 | 当前 gap 文档已规划 `GetNewestVersion()`、`SetGuid()`、`CppStructOps`、`UpdateScriptType()`，但全仓 `Plugins/Angelscript/Source/AngelscriptTest` 对公开字段 `bIsScriptStruct` 与 `ScriptType` 本身仍是 0 命中 |
| 风险评估 | 如果新 struct 没有把脚本身份字段初始化完整，或 replaced struct 在 full reload 后仍残留旧 `ScriptType`，struct compare/hash/ctor/dtor 相关路径会继续拿到 stale type 信息，而现有 suite 很难把问题定位到“身份字段未同步”这一层 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASStruct.ScriptIdentityFieldsTrackFullReloadLifecycle` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASGeneratedTypeIdentityTests.cpp` |
| 场景描述 | 先编译 `FStructIdentityTarget`，保存 `UASStruct* StructV1`；再对同名 `USTRUCT()` 做 structural change 触发 full reload，得到 `UASStruct* StructV2`。测试专门观察 reload 前后两代 struct 的脚本身份字段，而不是只看新字段是否存在 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::FullReload, ...)`；`FindObject<UScriptStruct>`；`Cast<UASStruct>` |
| 期望行为 | v1 阶段断言 `StructV1->bIsScriptStruct == true`、`StructV1->ScriptType != nullptr`；reload 后断言 `StructV2 != StructV1`、`StructV2->bIsScriptStruct == true`、`StructV2->ScriptType != nullptr`、`StructV1->GetNewestVersion() == StructV2`，并且旧 `StructV1->ScriptType == nullptr`，避免旧版本继续保留可调用的 stale script type 指针 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`FindObject<UScriptStruct>`；`Cast<UASStruct>` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-60 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 2 | MissingScenario: 1, NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 12:36)

### 一、现有测试问题

#### Issue-61：`VerifyNativeScriptHotReloadInline` 直接用文件基名做 module name，在 production engine 上存在 live module 名称碰撞风险

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 测试名 | `Angelscript.TestModule.Angelscript.NativeScriptHotReload.Phase2A` / `Phase2B` / `Phase2C` |
| 行号范围 | 29-33, 241-255 |
| 问题描述 | 公共 helper 在每轮里直接执行 `const FName ModuleName(*FPaths::GetBaseFilename(Filename));`，随后在 production engine 上对该 module name 做 compile / soft reload / discard。这样测试 module 没有任何 `Tests.` 前缀、group 前缀或随机后缀来避开 live 状态。尤其 `Phase2C` 传入的是实际工作区路径 `Script/Tests/Test_ExampleActorFixture.as`，helper 最终会把 module name 固定成 `Test_ExampleActorFixture`；如果 production engine 已经加载过同名脚本模块，测试实际上会对 live module 做热重载与清理，而不是只操作 test-owned module。 |
| 影响 | 这会把 native hot reload 用例变成带名称碰撞风险的共享状态测试：一条测试可能覆写或丢弃编辑器当前会话里的真实 module，后续用例也可能继承前一个同名 module 的残留结果。即使 compile 断言本身通过，CI 也无法证明验证的是独立的 test module，而不是偶然命中的 live module。 |
| 修复建议 | helper 内不要再直接使用文件基名，改为构造 test-owned module name，例如 `Tests.NativeHotReload.<GroupLabel>.<Index>.<Guid>`；若仍需保留源文件名供 diagnostics 使用，只把它作为 filename 参数，不作为 module identity。`Phase2C` 还应避免把真实 fixture 的 basename 直接映射成 live module 名，必要时先在 clone/fresh engine 上执行，进一步切断与 production session 的碰撞面。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-61 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 12:50)

### 一、现有测试问题

#### Issue-62：`HotReloadPerformanceTests` 从不验证基线编译是否成功，latency 样本可能建立在坏前置状态上

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPerformanceTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.Performance.SoftReloadLatency` / `FullReloadLatency` / `RenameWindowLatency` / `BurstChurnLatency` |
| 行号范围 | 78-99, 120-141, 165-186, 213-250 |
| 问题描述 | 四个 `Measure()` lambda 都先调用一次 `CompileAnnotatedModuleFromMemory(&Engine, ModuleName, ..., ScriptV1)` 建立 baseline，但完全忽略了这个调用的 `bool` 返回值。`BurstChurnLatency` 对三次 `CompileModuleWithResult(...)` 也同样只看输出枚举，不看 wrapper 自身是否返回 `false`。这意味着一旦 baseline compile 因上轮残留、预处理失败或脚本错误没有成功建立，后续样本仍会继续计时并写入 artifact。 |
| 影响 | 当前 performance suite 记录到的时延并不保证对应“从有效 v1 基线出发的 reload 成本”。最坏情况下，样本测量的是失败恢复路径或空 baseline 路径，但 CI 仍只按最终 `CompileResult` 的宽松集合给出绿灯，导致 metrics 缺乏解释力，也会掩盖 warmup/measurement 之间的真实污染来源。 |
| 修复建议 | 在每个 `Measure()` 里先保存 `const bool bBaselineCompiled = CompileAnnotatedModuleFromMemory(...)` 并立刻断言为 `true`；`BurstChurnLatency` 还应分别保存 `bStepOneCompiled` / `bStepTwoCompiled` / `bStepThreeCompiled`，把 wrapper 返回值与 `ECompileResult` 一起纳入样本结构。只有 baseline 和各步 compile 都成功进入预期路径时，才允许把该样本记为有效 latency 数据；否则应直接红灯或把样本标成无效而不是继续写入正常指标。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-62 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 12:57)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 12:50)` 的 `Issue-62`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 12:50)` 的 `NewTest-57`、`NewTest-58` 与 `NewTest-59`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-62 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |
| P1 | 1 | MissingErrorPath: 1 |

---

## 测试审查 (2026-04-09 13:15)

### 一、现有测试问题

本轮末尾校正：新增现有测试问题见上文 `## 测试审查 (2026-04-09 13:09)` 的 `Issue-63`。

### 二、需要新增的测试

本轮末尾校正：新增测试建议见上文 `## 测试审查 (2026-04-09 13:09)` 的 `NewTest-60`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-63 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 13:18)

### 一、现有测试问题

#### Issue-64：`AngelscriptHotReloadPropertyTests.cpp` 前三个用例都依赖下一次 `AcquireFresh` 才清模块，退出时没有自证 cleanup 完成

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.SoftReload.Basic` / `SoftReload.PreservesOtherModules` / `FullReload.Basic` |
| 行号范围 | 36-136, 139-205, 207-305 |
| 问题描述 | 这三个用例都使用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` + `ASTEST_BEGIN_SHARE_FRESH`，但测试体内没有任何 `ON_SCOPE_EXIT` 或显式 `Engine.DiscardModule()`；对照 `AngelscriptTestUtilities.h:429-432` 与 `AngelscriptTestMacros.h:117-121`，`AcquireFreshSharedCloneEngine()` 只在入口执行 `DestroySharedAndStrayGlobalTestEngine()` / `AcquireCleanSharedCloneEngine()`，`ASTEST_BEGIN/END_SHARE_FRESH` 也只建立 `FAngelscriptEngineScope`，不会像 `ASTEST_BEGIN_FULL/CLONE` 那样在退出时自动丢弃 active modules。结果是 `SoftReloadMod`、`SoftPreserveA`、`SoftPreserveB`、`FullReloadMod` 都会在用例返回后继续留在 shared engine 中，只能等下一条 fresh 测试再次 reset。 |
| 影响 | 这些用例把 cleanup contract 外包给“下一条测试的前置 reset”，一旦同进程内有人复用该 shared engine 做额外诊断、追加断言或把用例单独摘出来执行，残留 module 就会直接污染后续观察结果。更重要的是，测试本身看不到 `DiscardModule()` 是否成功，无法在 CI 中第一时间暴露 module teardown 回归。 |
| 修复建议 | 为每个用例补显式 cleanup：在进入测试后定义 `const FName ModuleName(...)` 并用 `ON_SCOPE_EXIT` 调 `const bool bDiscarded = Engine.DiscardModule(*ModuleName.ToString())`；多 module 场景则逐个回收 `SoftPreserveA` / `SoftPreserveB`。若需要把 cleanup 也纳入断言，可在退出前先 `TestTrue` `bDiscarded` 与 `!Engine.GetModuleByModuleName(...).IsValid()`，再让 scope guard 只做兜底。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-64 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 13:19)

### 一、现有测试问题

#### Issue-65：`AngelscriptHotReloadFunctionTests.cpp` 多个 shared-fresh 用例没有把自己创建的模块清干净

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.ModuleRecordTracking` / `DiscardModule` / `DiscardAndRecompile` |
| 行号范围 | 82-150, 153-214, 216-300 |
| 问题描述 | 这三个用例都使用 `ASTEST_CREATE_ENGINE_SHARE_FRESH()` + `ASTEST_BEGIN_SHARE_FRESH`，但退出时没有完整 cleanup。`ModuleRecordTracking` 编译 `ModuleA` 与 `ModuleB` 后直接返回；`DiscardModule` 只丢弃 `DiscardA`，却把 `SurvivorB` 留在 shared engine 中；`DiscardAndRecompile` 先 discard 一次旧 `DiscardRecompileMod`，重编译出新 module 后又直接返回。根据 `AngelscriptTestMacros.h:117-121` 与 `AngelscriptTestUtilities.h:429-432`，`SHARE_FRESH` 作用域本身不会自动 discard active modules，因此这些残留只能依赖下一条 fresh 测试的入口 reset 清理。 |
| 影响 | 一旦这些用例被单独执行、在同一进程中追加更多断言，或与非 fresh 的调试代码混跑，残留 module 就会直接污染后续 engine 状态。与此同时，测试也完全观察不到 “模块 teardown 是否成功” 这条 contract，相关回归只能等下一条用例间接暴露。 |
| 修复建议 | 为每个用例显式列出并回收自己创建的 module：`ModuleRecordTracking` 在 `ON_SCOPE_EXIT` 中 discard `ModuleA` / `ModuleB`；`DiscardModule` 额外 discard `SurvivorB`；`DiscardAndRecompile` 在重编译后的退出路径再 discard 一次当前 `DiscardRecompileMod`。若希望把 cleanup 结果也纳入断言，则在退出前补 `TestTrue(Engine.DiscardModule(...))` 与 `!Engine.GetModuleByModuleName(...).IsValid()`，并用 scope guard 只做兜底。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-65 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 13:25)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-61：补齐 `CreateDebugValuePrototype` / `UASClass::DebugValues` 的 debug-build 生命周期测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDebugValue.h` |
| 关联函数 | `FAngelscriptClassGenerator::CreateDebugValuePrototype()`；`UASClass::DebugValues`；`UASClass::StaticObjectConstructor(const FObjectInitializer&)`；`UASClass::RuntimeDestroyObject(UObject*)` |
| 现有测试覆盖 | `ClassGenerator_Analysis.md` 的发现 72 已明确指出 `WITH_AS_DEBUGVALUES`、`DebugValues.Instantiate`、`DebugValues.Free` 在测试树里都是 0 命中；当前 gap 文档虽已补 `IsDeveloperOnly()`，但对 debug-value 生命周期仍没有任何具体测试建议 |
| 风险评估 | debug build 下脚本对象构造会走 `Object->Debug = Class->DebugValues.Instantiate(Object)`，销毁又会进入 `DebugValues.Free(Object->Debug)`。如果 prototype 没生成、构造期没实例化，或销毁期路径失效，调试版编辑器会在对象检查/析构时静默丢调试数据甚至崩溃，而现有 CI 完全无感知 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.DebugValues.InstantiateAndDestroyOnScriptObjectLifecycle` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassDebugValueTests.cpp` |
| 场景描述 | 仅在 `#if WITH_AS_DEBUGVALUES` 下运行：编译一个最小 `UCLASS() class UDebugValueCarrier : UObject`，声明至少一个脚本字段（如 `int Counter = 3;` 与 `FString Label = "Dbg";`）以驱动 `CreateDebugValuePrototype()`；拿到 `UASClass* ScriptClass` 后创建两个对象，显式覆盖一次“构造 -> 运行 -> 销毁 -> 再构造”链路 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`FindGeneratedClass()`；`Cast<UASClass>()`；`NewObject<UObject>(GetTransientPackage(), ScriptClass)`；必要时 `CollectGarbage()` 或直接调用 `ScriptClass->RuntimeDestroyObject(Object)` 触发销毁路径；测试源码需包含能读取 `Object->Debug` 的头路径 |
| 期望行为 | 首次 compile 后断言 `Cast<UASClass>(ScriptClass) != nullptr`；第一个对象创建成功且 `ObjectOne->Debug != nullptr`；执行一次销毁路径后测试进程不崩溃，再创建第二个对象时 `ObjectTwo->Debug != nullptr` 且对象可正常读取脚本默认值 `Counter == 3`、`Label == "Dbg"`。若团队希望把 free 路径也做成可见断言，可在 test-local probe 中记录 `RuntimeDestroyObject()` 前后对象销毁次数，但最低限度必须把 instantiate/free 这条 debug-only 生命周期链跑进自动化。非 debug build 则以 `AddInfo("WITH_AS_DEBUGVALUES disabled")` 早退，避免把无效配置误记成覆盖 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`FindGeneratedClass`；`Cast<UASClass>`；`NewObject<UObject>`；`CollectGarbage` 或 `RuntimeDestroyObject`；`#if WITH_AS_DEBUGVALUES` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-09 13:26)

### 一、现有测试问题

#### Issue-66：`HotReload.ModuleWatcherQueuesFileChanges` 往 shared engine 写入 file-change queue 后从不清理

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.ModuleWatcherQueuesFileChanges` |
| 行号范围 | 302-330 |
| 问题描述 | 用例通过 `FAngelscriptHotReloadTestAccess::QueueFileChange()` 直接把 `FilenamePair` 推进 `Engine.FileChangesDetectedForReload`，并在第三个断言后立即 `return`。它既没有调用 `Engine.CheckForHotReload()` 消耗队列，也没有在退出前手动清空 `FileChangesDetectedForReload` / `QueuedFullReloadFiles`。结合 `ASTEST_BEGIN_SHARE_FRESH` 只建立 `FAngelscriptEngineScope`、不负责退出清理的语义，这条测试把 watcher queue 的残留完全留给下一次 `AcquireFreshSharedCloneEngine()` 的入口 reset 处理。 |
| 影响 | 一旦后续调试代码、额外断言或非 fresh 测试在同一 shared engine 上继续运行，就会读到上一个 watcher 用例留下的伪造 file-change 事件，进而把 queue 长度、full-reload 决策或 diagnostics 统计污染掉。当前自动化也看不到“队列清理是否成功”，无法在 CI 中及时发现 watcher state reset 回归。 |
| 修复建议 | 如果该用例继续保留 queue-level 形态，应在退出前显式清空或消费队列，并把结果写成断言：例如补 `FAngelscriptHotReloadTestAccess::CheckForHotReload(Engine, ECompileType::SoftReloadOnly)` 后验证 `GetQueuedFileChangeCount(Engine) == 0`，或新增 test access helper 清空 `FileChangesDetectedForReload/QueuedFullReloadFiles`。同时保留 `ON_SCOPE_EXIT` 兜底，避免早退路径把队列残留到 shared engine。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-66 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 13:27)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-62：补齐 repeated soft reload 下 `ReferenceSchema` 不重复累积的测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h` |
| 关联函数 | `FAngelscriptClassGenerator::DetectAngelscriptReferences()`；`FAngelscriptClassGenerator::DoSoftReload()`；`UASClass::ReferenceSchema`；`UASClass::RuntimeAddReferencedObjects(UObject*, FReferenceCollector&)` |
| 现有测试覆盖 | 当前 gap 文档的 `NewTest-24` 只覆盖“一次性 script-only 引用能否保活”；对 `ClassGenerator_Analysis.md` 发现 67/68 指出的 repeated soft reload schema 重建问题，现有文档和测试树都还没有 targeted 用例 |
| 风险评估 | 如果 `DetectAngelscriptReferences()` 在每次 `SoftReloadOnly` 后把旧 schema 追加一遍，`ReferenceSchema` 成员数会线性增长，并把 stale offset 一直留在 GC 扫描路径里。现有 happy-path reload 测试仍可能继续绿灯，但脚本对象会在多次热更后出现重复保活、错误保活甚至扫描过期内存 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASClass.ReferenceSchema.DoesNotDuplicateAcrossRepeatedSoftReload` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASClassReferenceSchemaTests.cpp` |
| 场景描述 | 编译 `UReferenceSchemaReloadHolder : UObject`，包含一个非 `UPROPERTY` 的 `UObject HiddenRef = nullptr;`，以及 `Store(UObject InValue)` / `GetStored()` / `GetVersion()` 三个函数；保存 `UASClass* ClassV1` 与 `InitialMemberCount = ClassV1->ReferenceSchema.Get().NumMembers()`。随后对同一 module 连续做两次 body-only soft reload，只改 `GetVersion()` 的返回值 `1 -> 2 -> 3`，确保走 `SoftReloadOnly` 而不改变 `HiddenRef` 布局 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::SoftReloadOnly, ...)`；`FindGeneratedClass()`；`Cast<UASClass>()`；`ProcessEvent` 或已有 helper 调 `Store/GetStored`；`CollectGarbage()` |
| 期望行为 | 初次编译后断言 `ClassV1 != nullptr` 且 `InitialMemberCount > 0`；第一次和第二次 soft reload 都返回 handled path，且 `FindGeneratedClass()` 仍返回同一个 `UASClass*`；`ClassAfterReload->ReferenceSchema.Get().NumMembers()` 在两次 soft reload 后都等于 `InitialMemberCount`，不会变成 `2N/3N`；在第二次 reload 后创建对象、把 transient target 塞进 `HiddenRef` 并触发 GC，`GetStored()` 仍返回该 target，证明 schema 不仅数量稳定，实际引用追踪也没有被 repeated soft reload 破坏 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`FindGeneratedClass`；`Cast<UASClass>`；`ProcessEvent`/现有执行 helper；`CollectGarbage` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 13:42)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-64：补齐 `UASFunction::bIsNoOp` 在 soft reload 中从非空函数切换为空函数的增量更新测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASFunction::bIsNoOp`；`FAngelscriptClassGenerator::DoSoftReload()`；`FAngelscriptClassGenerator::InitClassTickSettings()` |
| 现有测试覆盖 | 当前 gap 文档新补的 `NewTest-63` 只覆盖 “empty -> non-empty” 时清除 no-op 标记；对 `AngelscriptClassGenerator.cpp:4262-4268` 里 “non-empty -> no-op” 的反向分支，现有测试树与 gap 文档都还是 0 命中 |
| 风险评估 | 如果 soft reload 时不能把已有 `UFunction` 正确切回 no-op，运行时会保留 stale metadata，editor/tick 决策也会继续把一个空函数当成有实际行为的实现。该问题不会被现有返回值类 smoke test 提前暴露，因为它们通常只观察“新函数还能调用”，不观察生成函数元数据是否收敛到空实现 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASFunction.NoOpFlagAndMetadataCanBeAddedOnSoftReload` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASFunctionNoOpTests.cpp` |
| 场景描述 | 基线编译 `UNoopReloadCarrier : UObject`，声明 `UPROPERTY() int Counter = 0;` 与 `UFUNCTION() void Update() { Counter += 1; }`。保存 `Update` 的 generated `UASFunction*` 后，对同一 module 做 body-only soft reload，把 `Update()` 改成空函数体 `{}`；签名、类结构和属性布局保持不变，确保仍走 `SoftReloadOnly` |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::SoftReloadOnly, ...)`；`FindGeneratedClass()`；`FindGeneratedFunction()`；`Cast<UASFunction>()`；`NewObject<UObject>`；`ProcessEvent` 或 test-local void-call helper；`ReadPropertyValue` |
| 期望行为 | 基线阶段断言 `UpdateFunction->bIsNoOp == false` 且 `HasMetaData(TEXT("ScriptNoOp")) == false`；soft reload 后再次按名字查到的 `Update` function 必须 `bIsNoOp == true` 且 `HasMetaData(TEXT("ScriptNoOp")) == true`。在新对象上执行 `Update()` 后 `Counter` 仍为 `0`，证明生成器不仅补上了 no-op 标记，还把运行时语义同步收敛到了空函数体 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`FindGeneratedClass`；`FindGeneratedFunction`；`Cast<UASFunction>`；`NewObject<UObject>`；`ProcessEvent`/test-local void-call helper；`ReadPropertyValue` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-09 23:22)

### 一、现有测试问题

#### Issue-67：`HotReload.SoftReload.Basic` 只验证全局函数热更，实际上没有证明生成类方法被正确 soft reload

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadPropertyTests.cpp` |
| 测试名 | `Angelscript.TestModule.HotReload.SoftReload.Basic` |
| 行号范围 | 38-136 |
| 问题描述 | 脚本主体同时定义了 `USoftReloadTarget::GetVersion()` 和全局函数 `GetSoftReloadVersion()`，其中 V2 对类方法做的是 `return Version + 1;`，对全局函数做的是 `return 2;`。但测试在 reload 前后只执行 `ExecuteIntFunction(..., "int GetSoftReloadVersion()", ...)`，并未调用 95-96、124-125 行查到的 `GetVersion` 函数，也没有实例化 `USoftReloadTarget` 来验证 `GetVersion()` 的返回值是否从 `1` 变成 `2`。换言之，这个用例真正守住的只有“全局函数 body 变更能生效”，而不是“生成类上的 `UASFunction` 已被 soft reload 到新实现”。 |
| 影响 | 只要全局函数更新成功，这个用例就会绿灯；即便 `SoftReloadFunction()` 没有把 `USoftReloadTarget::GetVersion` 重新绑定到新脚本实现、旧 `UFunction` 仍指向 stale bytecode，当前测试也发现不了。这会把类方法热更回归伪装成已覆盖。 |
| 修复建议 | 在保留现有全局函数断言的同时，补一条对象级执行链：reload 前创建 `USoftReloadTarget` 实例并执行 `GetVersion()` 断言返回 `1`；reload 后在同一实例和新实例上分别执行 `GetVersion()`，断言都返回 `2`。若希望同时守住 function identity，可额外比较 `GetVersionAfterReload` 是否仍可从 `ClassAfterReload` 正常查到，并明确说明旧/新 `UFunction*` 的预期是否应相同。 |

### 二、需要新增的测试

本轮未新增需要新增的测试。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-67 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| 无新增 | 0 | - |

---

## 测试审查 (2026-04-09 23:37)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-65：补齐 `UASFunction` 源码行号在 soft reload 后随新脚本位置更新的测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 关联函数 | `UASFunction::GetSourceFilePath() const`；`UASFunction::GetSourceLineNumber() const`；`FAngelscriptClassGenerator::DoSoftReload()` |
| 现有测试覆盖 | `Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` 与 `Editor/AngelscriptSourceNavigationTests.cpp` 只验证首次生成后的正向 source metadata；当前 gap 文档也只覆盖 multi-section 定位与 discard 后失效态，还没有任何一条测试锁住“同一 generated function 经 soft reload 后，源码行号会更新到新声明位置” |
| 风险评估 | 如果 soft reload 只替换函数体而没有刷新 `declaredAt` / source metadata，编辑器跳转、错误定位和调试面板会继续指向旧行号；这类回归在行为测试里通常不会暴露，但对热重载排障成本影响很大 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASFunction.SourceLineMetadataUpdatesAfterSoftReload` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASSourceMetadataTests.cpp` |
| 场景描述 | 编译 `USourceLineReloadTarget : UObject` 的 v1，令 `GetValue()` 声明位于固定行号；保存 `UASFunction* GetValueV1` 的 `GetSourceFilePath()` 与 `GetSourceLineNumber()`。随后对同一 module 做 body-only soft reload，在函数前插入若干空行/注释并把返回值改成 `2`，确保脚本函数的声明行向后移动 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::SoftReloadOnly, ...)`；`FindGeneratedClass()`；`FindGeneratedFunction()`；`Cast<UASFunction>()`；`NewObject<UObject>`；`ExecuteGeneratedIntEventOnGameThread()` |
| 期望行为 | 基线阶段断言 `GetValueV1->GetSourceFilePath()` 指向目标脚本文件，`GetSourceLineNumber()` 等于 v1 中 `GetValue` 的声明行；soft reload 后再次按名字查到的 `UASFunction* GetValueV2` 仍可执行且返回 `2`，同时 `GetSourceFilePath()` 保持同一文件、`GetSourceLineNumber()` 更新为 v2 的新声明行，并且新行号严格大于旧行号。若产品语义要求 soft reload 复用同一 `UFunction` 外壳，也可额外断言 `GetValueV2 == GetValueV1` 时 metadata 仍然完成更新 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`FindGeneratedClass`；`FindGeneratedFunction`；`Cast<UASFunction>`；`ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P2 |

#### NewTest-66：补齐 `UASStruct::GetToStringFunction()` 在 full reload 删除 `ToString()` 后清空的测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASStruct.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `UASStruct::GetToStringFunction() const`；`UASStruct::UpdateScriptType()`；`FAngelscriptClassGenerator::DoFullReloadStruct()` |
| 现有测试覆盖 | 当前 gap 文档已规划 `GetToStringFunction()` 的正向暴露和 `opEquals/Hash` 删除后的能力位清理，但还没有任何一条建议专门验证 `ToString()` 本身在 reload 后被移除时，`GetToStringFunction()` 会从非空回落到 `nullptr` |
| 风险评估 | 如果 `UpdateScriptType()` 没把旧的 `ToStrFunction` 缓存清掉，工具层、调试输出和依赖 `ToString` 的 struct 展示会继续拿到 stale script function；现有正向测试即使通过，也发现不了“方法已删但缓存还活着”的回归 |
| 建议测试名 | `Angelscript.TestModule.ClassGenerator.ASStruct.GetToStringFunctionClearsAfterFullReloadRemovesToString` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/AngelscriptASStructCppStructOpsTests.cpp` |
| 场景描述 | 基线编译 `FReloadableToStringStruct`，定义 `int Value` 与 `FString ToString() const { return "V1"; }`；保存 `UASStruct* StructV1` 并确认 `GetToStringFunction()` 非空。随后对同名 struct 做 full reload，v2 删除 `ToString()`，并额外新增一个字段以确保走 struct full reload 路径 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::FullReload, ...)`；`FindObject<UScriptStruct>`；`Cast<UASStruct>` |
| 期望行为 | v1 阶段断言 `StructV1->GetToStringFunction() != nullptr`；reload 后断言 `StructV2 != StructV1`、`Cast<UASStruct>(StructV1)->GetNewestVersion() == StructV2`、`StructV2->GetToStringFunction() == nullptr`；同时 `StructV2` 上新增字段存在，证明测试观察到的不是编译失败或旧 struct 回退，而是真正的 full reload 后 API 缓存被正确清空 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`FindObject<UScriptStruct>`；`Cast<UASStruct>` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P2 | 2 | MissingScenario: 1, MissingEdgeCase: 1 |

---

## 测试审查 (2026-04-09 23:48)

### 一、现有测试问题

本轮未新增现有测试问题。

### 二、需要新增的测试

#### NewTest-67：补齐 `OnPostReload` bool 负载区分 soft/full reload 的事件语义测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 关联函数 | `FAngelscriptClassGenerator::OnPostReload`；`FAngelscriptClassGenerator::PerformSoftReload()`；`FAngelscriptClassGenerator::PerformFullReload()` |
| 现有测试覆盖 | 当前 `ClassGenerator/`、`HotReload/` 与全仓 `Plugins/Angelscript/Source/AngelscriptTest` 对 `OnPostReload` 的 bool 负载语义是 0 命中；源码在 `AngelscriptClassGenerator.h:12` 只声明 `DECLARE_MULTICAST_DELEGATE_OneParam(FOnAngelscriptPostReload, bool)`，而真正广播点 `AngelscriptClassGenerator.cpp:2395,2469` 传入的是 `bIsDoingFullReload`，不是 compile success/failure。现有 hot reload 测试只观察 compile result、类查询和事件是否触发，没有一条把这个 mode flag 锁成 contract。 |
| 风险评估 | 如果后续重构把 `OnPostReload` 的参数误改成“成功/失败”、固定常量，或 soft/full reload 两条路径广播出相同值，依赖该 delegate 做 UI 刷新、工具分支和后处理的调用点会静默走错分支，而当前测试树不会报警。 |
| 建议测试名 | `Angelscript.TestModule.HotReload.Events.PostReloadModeFlagMatchesReloadPath` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadEventTests.cpp` |
| 场景描述 | 先编译 `UPostReloadModeTarget` 的 v1。绑定 `FAngelscriptClassGenerator::OnPostReload`，记录每次广播收到的 bool 与广播时 `FindGeneratedClass()` 是否已经能查到最新类。随后先对同一 module 做一次 body-only soft reload，再做一次 structural full reload。 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_SHARE_FRESH()`；`CompileAnnotatedModuleFromMemory()`；`CompileModuleWithResult(..., ECompileType::SoftReloadOnly, ...)`；`CompileModuleWithResult(..., ECompileType::FullReload, ...)`；`FindGeneratedClass()`；test-local RAII 订阅器，负责在退出时解绑 `OnPostReload`。soft reload 的 v2 只改 `GetValue()` 返回体；full reload 的 v3 新增一个 `UPROPERTY()` 以稳定走 full reload。 |
| 期望行为 | soft reload 阶段断言 compile wrapper 成功、回调触发 1 次且收到的 bool 为 `false`；full reload 阶段再次断言回调触发 1 次且收到的 bool 为 `true`。两次回调触发时都应已经能通过 `FindGeneratedClass()` 查询到当前 canonical 类；full reload 后还应能查到新增属性，证明该 bool 确实表达 reload 路径而不是“事件早/晚”或“是否成功”。 |
| 使用的 Helper | `CompileAnnotatedModuleFromMemory`；`CompileModuleWithResult`；`FindGeneratedClass`；test-local `FScopedPostReloadListener` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 00:07 收尾)

### 一、现有测试问题

本轮无进一步新增现有测试问题。

### 二、需要新增的测试

本轮无进一步新增测试建议；本轮新增内容为前文同时间段写入的 `NewTest-69`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

---

## 测试审查 (2026-04-10 00:35)

### 一、现有测试问题

本轮末尾校正：有效新增现有测试问题为上文 `## 测试审查 (2026-04-10 00:27)` 的 `Issue-72`。该问题来自对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` 的完整文件审查，结论保持不变。

### 二、需要新增的测试

本轮末尾校正：有效新增测试建议为上文 `## 测试审查 (2026-04-10 00:31)` 的 `NewTest-70`、`NewTest-71`、`NewTest-72`。三条建议分别覆盖 `DefaultComponent.Attach` 缺失父节点、重复 `RootComponent`、`OverrideComponent` 指向不存在基类组件这三条 fail-closed 错误路径，结论保持不变。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-72 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingErrorPath: 3 |

---

## 测试审查 (2026-04-10 01:00 尾部确认)

### 一、现有测试问题

本轮尾部确认：有效新增现有测试问题为上文 `## 测试审查 (2026-04-10 00:58)` 的 `Issue-74`。

### 二、需要新增的测试

本轮尾部确认：有效新增测试建议为上文 `## 测试审查 (2026-04-10 00:58)` 的 `NewTest-75`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-74 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 01:15 尾部校正)

### 一、现有测试问题

本轮尾部校正：有效新增现有测试问题为上文 `## 测试审查 (2026-04-10 01:12)` 的 `Issue-75`。该问题来自对 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadAnalysisTests.cpp` 的完整文件审查，结论保持不变。

### 二、需要新增的测试

本轮尾部校正：有效新增测试建议为上文 `## 测试审查 (2026-04-10 01:13)` 的 `NewTest-76`。该建议指向当前 gap 文档此前未覆盖的 `UASClass::IsSafeForRootSet()` public API surface，结论保持不变。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-75 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P3 | 1 | NoTestForSource: 1 |

---

## 测试审查 (2026-04-10 01:29 尾部确认)

### 一、现有测试问题

本轮尾部确认：本次有效新增现有测试问题为上文 `## 测试审查 (2026-04-10 01:24)` 的 `Issue-76` 与 `## 测试审查 (2026-04-10 01:27)` 的 `Issue-77`。

### 二、需要新增的测试

本轮尾部确认：本次有效新增测试建议为上文 `## 测试审查 (2026-04-10 01:27)` 的 `NewTest-77`。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-76 |
| AntiPattern | 1 | Issue-77 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingErrorPath: 1 |
