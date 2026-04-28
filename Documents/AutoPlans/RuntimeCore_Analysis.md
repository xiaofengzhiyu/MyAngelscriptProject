# RuntimeCore 分析

---

## 分析 (2026-04-08 01:45)

### 发现 A-01：`GetAngelscriptExecutionFileAndLine()` 在空 `JIT debugCallStack` 分支直接解引用空指针

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 5667-5676 |
| 描述 | `tld->activeExecution != nullptr` 且 `debugCallStack == nullptr` 时，代码进入 `if (DebugStack == nullptr)` 分支后立刻读取 `DebugStack->Filename` 和 `DebugStack->LineNumber`。这条分支本身就证明 `DebugStack` 为空，因此这里是确定性的空指针解引用。 |
| 根因 | `GetAngelscriptExecutionFileAndLine()` 的空栈判断与同文件 `GetAngelscriptExecutionPosition()`（5638-5644 行）方向相反，明显是复制逻辑时把 `nullptr` 条件写反了。 |
| 影响 | 任何依赖 “当前 Angelscript 执行位置” 的日志、调试器、异常上报路径，只要命中 `activeExecution` 但未建立 `debugCallStack` 的 JIT 执行态，就会在查询位置信息时直接崩溃，错误处理路径反而扩大故障面。 |

---

## 分析 (2026-04-08 01:46)

### 发现 A-02：`InitializeOverrideForTesting` 路径把测试 engine 永久留在 `ContextStack` 且无所有权回收

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 行号 | 145-151，27-40，174-182，339-344，366-370，392-397 |
| 描述 | `FAngelscriptRuntimeModule::InitializeAngelscript()` 在 `InitializeOverrideForTesting` 分支只执行 `FAngelscriptEngineContextStack::Push(OverrideEngine)` 后直接 `return`。同文件 `ShutdownModule()` 与 `ResetInitializeStateForTesting()` 只会 `Pop`/`Reset` `OwnedPrimaryEngine`，完全看不到这个 override engine。与此同时，`AngelscriptSubsystemTests.cpp` 三个 RuntimeModule 用例都通过 `CreateTestingFullEngine(...).Release()` 把 `TUniquePtr` 释放成裸指针后交给 override 路径，因此测试创建的完整 engine 会永久滞留。 |
| 根因 | 测试注入入口绕过了 `OwnedPrimaryEngine` 的生命周期模型，只把实例挂进全局 `ContextStack`，却没有对应的 owner 字段或 teardown 分支承接回收。 |
| 影响 | 自动化测试会泄漏完整 `FAngelscriptEngine` 实例，并把过期测试 engine 长期保留为 `TryGetCurrentEngine()` 的首选结果；后续测试即使重置模块状态，也仍可能解析到上一轮注入的 engine，导致生命周期串线和隔离失真。 |

---

## 分析 (2026-04-08 01:47)

### 发现 A-03：`GetAngelscriptExecutionFileAndLine()` 在“无活动上下文”时直接返回，输出参数保持脏值

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 行号 | 5683-5690，660-666 |
| 描述 | `GetAngelscriptExecutionFileAndLine()` 在 `Context == nullptr` 或 `GetCallstackSize() == 0` 时直接 `return`，不会把 `OutFilename` / `OutLineNumber` 置为稳定的 sentinel。`Bind_UObject.cpp` 的资产元数据写入路径先声明未初始化的 `FString Filename; int LineNumber;`，调用该接口后立刻把两个值写入 `ScriptAssetFilename` / `ScriptAssetLineNumber` metadata，因此只要创建资产时不在有效脚本调用栈内，就会把旧值或未定义值写进元数据。 |
| 根因 | 该接口只在 `activeExecution` 分支定义了 `"" / -1` 兜底语义，普通 `asGetActiveContext()` 分支却把“未命中上下文”当作裸返回处理，接口契约前后不一致。 |
| 影响 | 调试和定位信息会出现伪造的文件名/行号，后续依赖这些 metadata 的定位、报错跳转或审计工具会指向错误脚本位置，排查成本被系统性放大。 |

---

## 分析 (2026-04-08 01:49)

### 发现 A-04：`GAmbientWorldContext` 以裸 `UObject*` 跨帧缓存，读取路径完全绕过 GC 有效性校验

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` |
| 行号 | 268-300，682-690，5263-5267，101-113 |
| 描述 | `GAmbientWorldContext` 是文件静态的 `UObject*`。`SetAmbientWorldContext()` 只在写入时做一次 `IsValidLowLevelFast(false)` 检查，随后 `GetAmbientWorldContext()` / `TryGetCurrentWorldContextObject()` 直接原样返回；`UAngelscriptGameInstanceSubsystem::GetCurrent()` 把它传给 `GEngine->GetWorldFromContextObject()`，`LogAngelscriptException()` 又把它传给 `UKismetSystemLibrary::PrintString()`。这条 ambient 路径既不是 `UPROPERTY()`，也不是 `TWeakObjectPtr`，读路径没有任何二次校验。 |
| 根因 | Runtime 把“当前 world context” 建模成进程级裸指针缓存，试图用一次性写时校验替代持续的对象生命周期管理；而 `FAngelscriptEngine` 内部真正受 GC 跟踪的是 `WorldContextObject`（`AngelscriptEngine.h:455-456`），ambient 缓存绕过了这层保护。 |
| 影响 | 一旦 world teardown、PIE 切换或临时 context 对象销毁发生在两次 ambient 更新之间，后续 subsystem 解析和异常打印就可能在悬空 `UObject*` 上运行，形成 use-after-free / 崩溃风险，而且这种故障会出现在报错与恢复路径，定位难度更高。 |

---

## 分析 (2026-04-08 01:49)

### 发现 D-01：RuntimeModule override 路径没有 teardown 回归测试，`ContextStack` 残留不会被自动化发现

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 行号 | 337-349，364-377，390-402，对比 158-191 |
| 描述 | `AngelscriptSubsystemTests.cpp` 三个 RuntimeModule 用例都会设置 `InitializeOverrideForTesting` 并在 `ON_SCOPE_EXIT` 中调用 `ResetInitializeStateForTesting()` / `ShutdownModule()`，但断言只覆盖“创建成功”“ticker 注册成功”“能推进 tick”，没有任何一个断言检查 cleanup 之后 `FAngelscriptEngineContextStack::IsEmpty()`、`Peek()` 是否恢复基线，也没有验证当前 engine 是否回到预期状态。与之相对，`AngelscriptEngineIsolationTests.cpp` 已经对普通 `FAngelscriptEngineScope` 路径显式断言了 `ContextStack` 入栈、嵌套恢复和最终清空。 |
| 根因 | 现有 subsystem/runtime module 测试把 override 路径当作功能注入手段，而不是新的生命周期分支；测试设计只验证“运行成功”，没有把 `ContextStack` 清理视为合同的一部分。 |
| 影响 | A-02 这类 “功能通过但 teardown 失真” 的缺陷会长期潜伏；自动化测试会给出绿色结果，却掩盖 stale current-engine、跨用例串线和测试内存泄漏问题。 |

---

## 分析 (2026-04-08 01:54)

### 发现 A-05：`StartHotReloadThread()` 启动后台线程后丢失所有权，`Shutdown()` 无法停止或等待线程退出

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1132-1251，1618-1620，1658-1700 |
| 描述 | `Initialize_AnyThread()` 在 `bScriptDevelopmentMode && !RuntimeConfig.bIsEditor` 条件下打开 `bUseHotReloadCheckerThread` 并调用 `StartHotReloadThread()`。后者直接执行 `FRunnableThread::Create(new FAngelscriptHotReloadThread(), TEXT("AngelscriptHotReload"), ...)`，既不保存返回的 `FRunnableThread*`，也不保存 `FAngelscriptHotReloadThread` 实例。线程入口在 1680 行先取 `auto& Manager = FAngelscriptEngine::Get();`，随后循环读取 `Manager.bUseHotReloadCheckerThread` 与 `Manager.bWaitingForHotReloadResults`。但 `Shutdown()` 整段清理逻辑中没有把 `bUseHotReloadCheckerThread` 置回 `false`，也没有 `Stop()`、`WaitForCompletion()`、`Kill()` 或删除线程对象。 |
| 根因 | 热重载检查线程被建模成“启动即放任运行”的一次性后台任务，生命周期没有纳入 `FAngelscriptEngine` 的 owner 状态；代码只保存了布尔标记，没有保存线程句柄与 runnable 所有权，因此 teardown 阶段根本没有可执行的回收路径。 |
| 影响 | 非 Editor 开发模式下销毁 full engine 时，后台线程仍会继续访问已经开始 `Shutdown()` 乃至已析构的 `FAngelscriptEngine` 成员，形成稳定的 use-after-free / 崩溃窗口；同时线程对象与 runnable 也会泄漏，重复创建/销毁 engine 的测试或运行时切换会累积悬挂线程。 |

---

## 分析 (2026-04-08 02:08)

### 发现 A-06：root-set `UPackage` 在 `Shutdown()` 只清空指针不退根，full engine teardown 无法真正释放包级状态

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | 878-882，1245-1251，1382-1386；289-293 |
| 描述 | `InitializeForTesting()` 和 `Initialize_AnyThread()` 都用 `RF_Public | RF_Standalone | RF_MarkAsRootSet` 创建 `AngelscriptPackage` / `AssetsPackage`。这两个包还被 `FAngelscriptEngine` 作为 `UPROPERTY()` 持有。但 `Shutdown()` 在 teardown 末尾只做 `AngelscriptPackage = nullptr; AssetsPackage = nullptr;`，整个 RuntimeCore 中看不到任何 `RemoveFromRoot()`、清除 root flag 或显式销毁逻辑。也就是说，包对象一旦创建就会继续留在 root set，engine 侧只是丢失了引用。 |
| 根因 | 生命周期实现把 `UPROPERTY()` 置空误当成完整清理，却忽略了对象是在创建时被显式提升到 root set；GC 只能追踪字段引用，不能自动撤销 `RF_MarkAsRootSet` 带来的全局保活。 |
| 影响 | full engine 即使走完 `Shutdown()` 和析构，也不能回到“无包对象”的干净基线；所有 outer 到这两个包下的脚本类、资产或临时对象都会继续被 root set 间接保活，形成跨 engine epoch 的内存泄漏和状态残留。对多轮自动化测试、重复创建/销毁 full engine 的隔离验证，以及任何依赖 teardown 后重新初始化得到干净包命名空间的路径，这都是持续污染。 |

---

## 分析 (2026-04-08 01:56)

### 发现 A-06：`Initialize()` 的 threaded 分支用 `volatile bool` 和全局 `GameThreadTLD` 做跨线程同步，存在未受保护的数据竞争

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | 820-848，141，1179-1183，1565-1566，1779-1782 |
| 描述 | `Initialize()` 的 threaded 路径在 827-840 行声明 `volatile bool bInitializationDone`，由后台 `AsyncTask` 写入、主线程 while 循环持续读取；同一个 worker 还会把静态全局 `GameThreadTLD` 从真正的 game-thread TLD 临时改写成 `asCThreadManager::GetLocalData()` 返回的 worker-thread TLD，并把 `primaryContext` 在两个线程的 TLD 之间来回搬运。与此同时，主线程在 843-847 行没有阻塞等待，而是持续 `Broadcast()` / `ProcessThreadUntilIdle()` 处理 game-thread 任务；而 `GameThreadTLD` 又是类级静态变量（`AngelscriptEngine.h:141`），后续 `Shutdown()`、`GameThreadContext` 构造等路径都会直接读它（1179-1183，1779-1782）。 |
| 根因 | 线程化初始化没有建立显式同步原语，只依赖 `volatile` 和裸静态指针来共享状态；实现假定“初始化期间不会有别的 game-thread 代码读取 `GameThreadTLD`”，但代码本身在等待阶段主动泵了 game-thread 任务，这个假定不成立。 |
| 影响 | 这条初始化路径存在标准意义上的 data race：主线程可能读到过期的 `bInitializationDone` 而异常自旋，也可能在等待期间通过其他任务读到 worker 线程的 `GameThreadTLD`，把本应绑定 game thread 的 primary context 解析到错误线程上；结果是初始化卡死、上下文错绑或后续 shutdown/执行态行为不稳定。 |

---

## 分析 (2026-04-08 01:57)

### 发现 B-01：`UAngelscriptGameInstanceSubsystem` 把 `ContextStack` 当作长期全局注册表，导致多实例下当前引擎解析串台

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 17-23, 39-45; 718-733 |
| 描述 | 当 subsystem 没有拿到现成引擎时，`Initialize()` 会把 `OwnedEngine` 先 `Push` 到 `FAngelscriptEngineContextStack`，然后直到 `Deinitialize()` 才 `Pop`。与此同时，`TryGetCurrentEngine()` 的首选分支始终是 `FAngelscriptEngineContextStack::Peek()`。这意味着 subsystem 生命周期内，栈顶永远代表“最后一个初始化的 subsystem 引擎”，而不是“当前调用现场真正激活的引擎作用域”。 |
| 根因 | `FAngelscriptEngineContextStack` 原本由 `FAngelscriptEngineScope` 以 RAII 方式短时压栈/出栈，用来表达一次调用链的当前引擎；但 subsystem 直接把它当作长期 owner 注册表使用，破坏了“栈顶即当前作用域”的语义。 |
| 影响 | 一旦同时存在多个 `UGameInstance` / PIE world / 测试 world，所有未显式建立 `FAngelscriptEngineScope` 的代码都会优先解析到最后初始化的那台引擎，出现脚本根目录、包对象、配置开关、上下文池和 world context 的跨实例串用。该问题会把本应隔离的 runtime 状态重新汇聚回一个隐式全局入口。 |

### 发现 A-11：threaded `Initialize()` 在 worker thread 改写全局 `GameThreadTLD`，同时 game thread 继续跑任务循环

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | 819-848, 1179-1182, 1779; 141 |
| 描述 | `Initialize()` 的 threaded 分支把静态全局 `GameThreadTLD` 保存到 `RealGameThreadTLD` 后，在 worker thread 内直接重写为 `asCThreadManager::GetLocalData()` 返回的本地 TLS，并把 `primaryContext` 复制过去。与此同时，game thread 没有阻塞，而是在 `while (!bInitializationDone)` 里持续执行 `OnAsyncLoadingFlushUpdate.Broadcast()` 和 `ProcessThreadUntilIdle(GameThread)`。因此初始化窗口内，任何读取 `FAngelscriptEngine::GameThreadTLD` 的路径都会看到 worker thread 的 TLS，而不是 game thread 自己的 TLS。 |
| 根因 | `GameThreadTLD` 被设计成全局静态入口，但 threaded 初始化把它当成了跨线程共享的临时 scratch pointer；实现没有锁、原子或线程隔离，也没有停止 game thread 上继续处理任务。 |
| 影响 | 运行在这段窗口内的 game-thread 代码会把 context 创建、回收和 `primaryContext` 访问误导到 worker thread 的 `asCThreadLocalData` 上，破坏 “game thread TLS” 的基本约束。后续 `Shutdown()`、`FAngelscriptPooledContextBase` 构造和 context 归还逻辑都依赖这个静态入口，错用后会把初始化期问题放大成跨线程 context 污染或错误释放。 |

### 发现 A-10：全局委托注册没有解绑，`Shutdown()` 后仍会保留指向已销毁 `FAngelscriptEngine` 的回调

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1132-1252, 1628-1638, 2201-2218 |
| 描述 | 初始化阶段直接向 `FCoreDelegates::OnPostEngineInit` 注册两个 `AddLambda([&](){ ... })`，并向 `FCoreDelegates::OnGetOnScreenMessages` 注册 `AddRaw(this, &FAngelscriptEngine::GetOnScreenMessages)`。这些回调都捕获当前引擎实例或其成员，但 `Shutdown()` 全段没有任何 `Remove`、`RemoveAll(this)` 或 `FDelegateHandle` 清理逻辑。 |
| 根因 | 生命周期设计只释放了 `DebugServer`、`StaticJIT`、`PrecompiledData`、context pool 和 shared state，却没有把全局 delegate 订阅纳入 engine owner 的资源模型；同时注册时也没有保存 handle，导致后续无法定点解绑。 |
| 影响 | 引擎销毁后，后续任意一次 `OnGetOnScreenMessages` 广播都仍可能回调到已释放的 `FAngelscriptEngine`；`OnPostEngineInit` / `AssetManager` 相关 lambda 也会继续保留对旧实例成员的引用。结果是 use-after-free、重复测试发现、重复覆盖率 hook，或者在多次创建/销毁 engine 的测试场景中累积出越来越多的悬空回调。 |

### 发现 D-03：subsystem 自动化测试绕过真实 `Initialize()`，导致生产初始化路径几乎没有覆盖

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` |
| 行号 | 52-87, 140-160, 211-417; 12-30 |
| 描述 | `AngelscriptSubsystemTests.cpp` 里的主要 helper `FAngelscriptTickBehaviorTestAccess::SetSubsystemPrimaryEngine()` 直接写入 `Subsystem.PrimaryEngine`、`bOwnsPrimaryEngine`、`bInitialized`、`ActiveTickOwners`，并手工 `Push` 到 `FAngelscriptEngineContextStack`。随后 `CreateInjectedSubsystem()` 用这个 helper 伪造出“已初始化 subsystem”，后面的 `CreatesPrimaryEngine`、`TicksPrimaryEngine`、`RuntimeFallbackDoesNotTickWhenSubsystemOwnsEngine` 等测试都建立在这条假路径上。相对地，真正的生产入口 `UAngelscriptGameInstanceSubsystem::Initialize()`（12-30 行）包含的 `TryGetCurrentEngine()` 采用分支、`OwnedEngine.Initialize()` 调用和 owner 判定逻辑都没有被这些测试覆盖。 |
| 根因 | 现有测试为了避免搭建完整 `UGameInstance`/world 生命周期，选择通过 friend access 直接篡改 subsystem 内部状态；文件中虽然定义了 `CreateSubsystemWorld()` 来创建真实 world/subsystem，但在该测试文件内没有任何调用点。 |
| 影响 | 与 subsystem 真实接线边界最相关的缺陷不会在现有自动化套件里暴露，包括 “当前引擎 adoption 是否绑错实例”、“长期 `ContextStack` 压栈是否污染全局解析”、“自有 `OwnedEngine` 创建/销毁是否对称” 等问题。当前测试通过只能说明测试 helper 自己能构造出一个看似可用的 subsystem 状态，不能证明生产初始化路径正确。 |

### 发现 C-02：运行时关闭 `asEP_AUTO_GARBAGE_COLLECT` 后没有补上任何 production 级 AngelScript GC 驱动

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 行号 | 889, 1395, 2794-2840, 1132-1252; 354-372, 399-415 |
| 描述 | 无论测试初始化还是正常初始化，engine 都显式执行 `Engine->SetEngineProperty(asEP_AUTO_GARBAGE_COLLECT, 0)`。但 `Tick()` 和 `Shutdown()` 代码里都没有任何 `ScriptEngine->GarbageCollect(...)` 调用，`rg \"GarbageCollect\\(\" Plugins/Angelscript/Source/AngelscriptRuntime` 的运行结果也显示，运行时代码里除 third-party 实现外，只有内部 GC 测试在手动驱动 AngelScript GC。与之对应，`AngelscriptGCInternalTests.cpp` 354-372、399-415 行明确证明：自引用循环对象在 `Release()` 之后，只有显式执行 full GC / detect+collect 才会被检测并销毁。 |
| 根因 | 当前实现关闭了 AngelScript 的自动循环收集器，却没有在 `FAngelscriptEngine` 生命周期里补一个周期性或 teardown 时的手动 GC 调度点。现有 `Testing/UnitTest.cpp` 里的 `ForceGarbageCollectionNow()`/`MaybeGarbageCollect()` 只驱动 UE `CollectGarbage()`，不驱动 AngelScript `GarbageCollect()`。 |
| 影响 | 脚本侧的循环引用对象在 production runtime 中没有可验证的回收入口；根据仓库内的内部 GC 测试，这类对象不会因为单纯 `Release()` 而消失。结果是运行时间越长，GC 跟踪表中的循环垃圾越积越多，直到整个 engine 销毁才可能统一释放，长期会侵蚀内存占用并放大热重载/长局游戏会话的稳定性风险。 |

---

## 分析 (2026-04-08 01:58)

### 发现 A-07：`FAngelscriptEngine` 注册了长期存活的原始委托，但 `Shutdown()` 没有任何解绑路径

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 行号 | 1132-1251，1628-1638，2201-2218；对比 349-367 |
| 描述 | 初始化阶段会注册三条和 engine 实例强绑定的长期回调：`FCoreDelegates::OnPostEngineInit.AddLambda([&]{ CodeCoverage->AddTestFrameworkHooks(); })`、`FCoreDelegates::OnGetOnScreenMessages.AddRaw(this, &FAngelscriptEngine::GetOnScreenMessages)`，以及另一条 `OnPostEngineInit` lambda，内部再通过 `AssetManager->CallOrRegister_OnCompletedInitialScan(CreateLambda([&]{ DiscoverTests(); bCompletedAssetScan = true; }))` 捕获当前 engine。`Shutdown()` 整段析构逻辑只清理 script engine / context / package / shared state，没有保存任何 `FDelegateHandle`，也没有 `Remove` / `RemoveAll(this)`。同时 `AngelscriptEngineIsolationTests.cpp:349-367` 已显式验证“多个 full engine 可以并存”，这意味着这些回调会按实例叠加。 |
| 根因 | engine 生命周期把委托注册当成初始化副作用，却没有把 delegate handle 纳入成员状态管理；`AddRaw(this, ...)` 和 `[&]` lambda 都直接捕获实例地址，但类定义中不存在对应的 handle 字段，teardown 阶段因此没有解绑抓手。 |
| 影响 | 只要 engine 被销毁而 `OnGetOnScreenMessages`、`OnPostEngineInit` 或 `OnCompletedInitialScan` 之后再次触发，回调就会落到已释放的 `FAngelscriptEngine` 上，形成 use-after-free；即使未销毁，多 full engine 并存时也会重复执行 `GetOnScreenMessages()` / `DiscoverTests()` / coverage hook 注册，把本应单实例的全局行为叠加成多份。 |

---

## 分析 (2026-04-08 01:59)

### 发现 A-09：threaded 初始化创建出的 `primaryContext` 在 worker 收尾时被直接丢弃，game-thread `usable context` 永远接不回来

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptobject.cpp` |
| 行号 | 809-817，828-839，935，1565-1566；150-156；41-60，136-150，500-516，808-820 |
| 描述 | 非 Editor 路径默认走 threaded 初始化（`ShouldInitializeThreaded()` 在 816 行返回 `!RuntimeConfig.bSkipThreadedInitialize`）。这条路径先把 `RealGameThreadTLD->primaryContext` 复制到 worker TLD（833 行），再在 `Initialize_AnyThread()` 中把新建 context 写进当前 `GameThreadTLD->primaryContext`（1566 行）。但 worker 收尾时没有把新 context 回填给真实 game-thread TLD，而是直接执行 `GameThreadTLD->primaryContext = nullptr; GameThreadTLD = RealGameThreadTLD;`（837-838 行）。随后 `InitializeOwnedSharedState()` 又从真实 game-thread TLD 读取 `SharedState->PrimaryContext`（935 行），因此记录到的仍然是 `nullptr`。换句话说，threaded 初始化确实创建了 primary context，但在切回主线程前就把它丢失了。 |
| 根因 | 实现把 `GameThreadTLD` 当成“可临时借给 worker thread 的全局别名”，却没有处理 `primaryContext` 的所有权迁移：创建发生在 worker TLD，上层持久化却从 real game-thread TLD 读取，两个步骤之间没有任何 handoff。 |
| 影响 | AngelScript runtime 的 `asGetUsableContext()` 会在无活动上下文时回退到 `tld->primaryContext`（`as_context.cpp:150-156`），而脚本对象工厂、构造和 `opAssign` 等路径都会调用它来复用 usable context（`as_scriptobject.cpp:41-60,136-150,500-516,808-820`）。threaded 初始化结束后，game thread 的 usable context 恒为 `nullptr`，这些路径只能退化为额外创建新 context，或者在依赖 primary context 可用的场景里失去预期行为；同时 worker 上创建出的 primary context 因为既没挂回 TLS 也没写入 shared state，会成为一块无法再被正常释放或复用的泄漏对象。 |

---

## 分析 (2026-04-08 02:00)

### 发现 A-10：`CodeCoverage` 对象不受任何 teardown 路径管理，开启覆盖率后每个 full engine 都会永久泄漏一套统计与回调

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp` |
| 行号 | 1132-1252，1460-1462；15-47；22-27 |
| 描述 | `Initialize_AnyThread()` 在 `FAngelscriptCodeCoverage::CoverageEnabled()` 为真时直接执行 `CodeCoverage = new FAngelscriptCodeCoverage;`。这个指针既不写入 `SharedState`，也不在 `Shutdown()` 中 `delete`，`ReleaseOwnedSharedStateResources()` 里同样没有对应分支。更进一步，`AddTestFrameworkHooks()` 会把这个 `CodeCoverage` 实例以 `AddRaw(this, ...)` 方式挂到 `AutomationController->OnTestsAvailable()` 和 `OnTestsComplete()` 上，但类本身没有析构函数或解绑逻辑。 |
| 根因 | 覆盖率统计被当成一次性可选功能对象创建出来，但没有纳入 `FAngelscriptEngine` 或 shared-state 的资源所有权模型；初始化路径负责分配和注册，teardown 路径却完全不知道它的存在。 |
| 影响 | 只要开启 code coverage，每个 full engine 生命周期都会残留一份 `FAngelscriptCodeCoverage`、它累计的 `FilesToCoverage` 统计，以及一组 AutomationController raw 回调。长期运行的编辑器会逐轮累积旧覆盖率数据和重复回调，自动化测试结束时可能对多份历史实例重复写报告；多次创建/销毁 engine 的测试环境也无法回到干净基线。 |

---

## 分析 (2026-04-08 02:00)

### 发现 C-01：`TryGetGlobalEngine` / `SetGlobalEngine` / `DestroyGlobal` 仍以公开 API 暴露，但实现已经退化成别名与空操作

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 行号 | `AngelscriptEngine.h` 461-464；`AngelscriptEngine.cpp` 736-778；测试引用见 12-24，19-24，54-63 等 |
| 描述 | 头文件仍公开声明 `TryGetGlobalEngine()`、`SetGlobalEngine()`、`DestroyGlobal()` 三个“全局引擎”入口，但实现里 `TryGetGlobalEngine()` 只是直接返回 `TryGetCurrentEngine()`，`SetGlobalEngine(FAngelscriptEngine* InEngine)` 完全忽略传入参数，只调用 `SyncAmbientWorldContextFromCurrentEngine()`，`DestroyGlobal()` 则恒定返回 `false`。与之相对，测试辅助代码仍把它们当成真实的全局状态 API：`DestroyGlobal()` 被用作重置入口，`TryGetGlobalEngine()` 被用作“当前全局主引擎”断言来源。 |
| 根因 | Runtime 从“显式 global engine” 迁移到 `ContextStack + subsystem` 解析后，没有同步收敛旧公共接口；旧 API 名称和签名被保留，但内部语义已经被掏空。 |
| 影响 | 调用方会得到一个表面可用、实则失真的契约：它既不能可靠区分“global engine”和“current scoped engine”，也不能真正执行全局 teardown。结果是测试重置和外部接入都可能在 silent no-op 上继续运行，把状态残留误判成“已经清理”。 |

---

## 分析 (2026-04-08 02:01)

### 发现 D-02：自动化测试没有覆盖 threaded initialize、热重载线程 teardown 和 delegate cleanup 这三条生命周期分支

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 测试命中集中在 `AngelscriptSubsystemTests.cpp` 92-109，329-414；实现分支见 `AngelscriptEngine.cpp` 820-848，1132-1251，1618-1638，1658-1700，2201-2218 |
| 描述 | 对 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 执行 `rg -n "ShouldInitializeThreaded|bInitializationDone|GameThreadTLD|AngelscriptHotReload|OnGetOnScreenMessages|OnPostEngineInit|CompletedInitialScan|TickFallbackPrimaryEngine|DiscoverTests"` 后，只命中 `TickFallbackPrimaryEngine` 相关测试（`AngelscriptSubsystemTests.cpp` 92-109、329-414），没有任何用例覆盖 `ShouldInitializeThreaded()` 的多线程初始化分支、`AngelscriptHotReload` 后台线程的 shutdown、`OnGetOnScreenMessages` / `OnPostEngineInit` / `OnCompletedInitialScan` 的注册与解绑，以及 `DiscoverTests()` 的延迟触发路径。 |
| 根因 | 现有 RuntimeCore 测试主要围绕 `ContextStack` 解析、clone/shared-state 和 fallback tick 行为构建，对“初始化后异步副作用是否被 teardown 正确回收”缺少专门的生命周期回归用例。 |
| 影响 | A-05、A-06、A-07 这类只会在 threaded initialize、后台线程退出或晚到 delegate broadcast 时暴露的问题不会被现有自动化发现；测试集能证明基础解析逻辑存在，但对真正容易在编辑器关闭、模块卸载或异常启动流程中失真的路径没有防线。 |

---

## 分析 (2026-04-08 02:03)

### 发现 A-08：`RuntimeModule` 持有的 `OwnedPrimaryEngine` 没有进入 GC 引用图，`WorldContextObject` 在这条路径上退化成未受跟踪的裸指针

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptRuntimeModule.h` 25-61；`AngelscriptRuntimeModule.cpp` 8，162-164；`AngelscriptEngine.h` 455-456，613-620；`AngelscriptEngine.cpp` 682-689，746-753 |
| 描述 | module 主引擎由 `FAngelscriptRuntimeModule::OwnedPrimaryEngine` 这个静态 `TUniquePtr<FAngelscriptEngine>` 持有，并在 `InitializeAngelscript()` 中直接 `MakeUnique` 创建。`FAngelscriptEngine` 内部把当前世界对象声明为 `UPROPERTY() UObject* WorldContextObject`，而 `TryGetCurrentWorldContextObject()` / `AssignWorldContext()` 都直接读写这块成员。与此同时，`FAngelscriptEngine` 的 `TStructOpsTypeTraits` 只声明了 `WithCopy = false`，当前 RuntimeCore 代码中也找不到 `FGCObject`、`AddReferencedObjects`、`AddStructReferencedObjects` 之类把 `OwnedPrimaryEngine` 接入 GC 的实现。 |
| 根因 | 已验证事实：`WorldContextObject` 只有放在可被 Unreal GC 遍历的 owner 链上时，`UPROPERTY` 才有意义；`OwnedPrimaryEngine` 当前只存在于模块级静态 `TUniquePtr` 中，没有任何反射或 `FGCObject` 桥接。推断：这意味着 module-owned engine 路径上的 `WorldContextObject` 不会被 GC 自动跟踪或在对象销毁后清空。 |
| 影响 | 只要编辑器/commandlet 主引擎曾经通过 `AssignWorldContext()` 绑定过临时 `UObject`，后续 `TryGetCurrentWorldContextObject()` 就可能从仍然“当前”的 module-owned engine 中取回悬空指针；这条风险独立于 `GAmbientWorldContext`，会直接影响依赖 current-engine world context 的 bind 和日志路径。 |

---

## 分析 (2026-04-08 02:22)

### 发现 A-12：`ShutdownModule()` 不复位 `bInitializeAngelscriptCalled`，RuntimeModule 在同进程二次启动时会永久跳过引擎初始化

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | 13-18，27-40，138-166，174-182 |
| 描述 | `StartupModule()` 在 editor/commandlet 里每次都会调用 `InitializeAngelscript()`。但 `InitializeAngelscript()` 只要看到静态标志 `bInitializeAngelscriptCalled` 为 true 就直接返回。与之相对，正式 `ShutdownModule()` 只移除 `FallbackTickHandle` 并释放 `OwnedPrimaryEngine`，完全没有把这个闸门复位；整个文件里只有测试辅助 `ResetInitializeStateForTesting()` 才会在 181 行把 `bInitializeAngelscriptCalled = false`。这意味着模块一旦在同一进程里走过一次 `StartupModule()`/`ShutdownModule()`，下一次 `StartupModule()` 会重新注册 ticker，但 `InitializeAngelscript()` 会被 140-141 行短路，新的 primary engine 根本不会再建起来。 |
| 根因 | RuntimeModule 把“初始化已发生过”建模成进程级静态 latch，却只在测试 helper 里提供复位逻辑；正式 shutdown 路径没有把这个生命周期状态纳入 teardown 合同。 |
| 影响 | 任何依赖模块卸载后重新加载的路径都会进入“模块已启动但 engine 未初始化”的半死状态：fallback ticker 仍在，`OwnedPrimaryEngine` 已经释放，而再次启动又不会重建 engine。后续所有要求 RuntimeModule 重新接管引擎生命周期的流程都会失效。 |

### 发现 B-02：`UAngelscriptGameInstanceSubsystem` 对共享 `PrimaryEngine` 没有单点 tick 仲裁，同一 engine 可被多个 subsystem 重复推进

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `AngelscriptGameInstanceSubsystem.cpp` 12-30，81-86，116-118；`AngelscriptRuntimeModule.cpp` 186-195 |
| 描述 | `UAngelscriptGameInstanceSubsystem::Initialize()` 只要发现已有 `TryGetCurrentEngine()`，就直接采用这台 engine 作为 `PrimaryEngine`，并无条件把进程级 `ActiveTickOwners` 加一。后续 `Tick()` 也不区分“我是 owner”还是“我只是 adopted existing engine”，任何 `PrimaryEngine != nullptr` 的 subsystem 都会直接执行 `PrimaryEngine->Tick(DeltaTime)`。与此同时，RuntimeModule 的 fallback ticker 只看 `HasAnyTickOwner()` 这个全局计数，因此一旦任意 subsystem 存在，模块侧单点 tick 就被完全关掉。已验证事实：这套协议里不存在任何“每个 engine 只能由一个 tick owner 推进”的判定。推断：只要同一进程里有多个 subsystem 实例采用了同一个当前 engine，它们就会在各自的 `Tick()` 中重复推进同一台 engine。 |
| 根因 | subsystem 集成边界把 tick ownership 建模成了进程级布尔语义（`ActiveTickOwners > 0`），而不是 per-engine / per-owner 协议；`Initialize()`、`Tick()`、RuntimeModule fallback 三处逻辑彼此之间没有共享“谁负责给这台 engine tick”的唯一性约束。 |
| 影响 | 同一 `FAngelscriptEngine` 的热重载检查、unit test runner、debug server 和其它 `Tick()` 副作用可能在一帧内被执行多次，造成时间基准被放大、状态机重复推进，以及多 `UGameInstance` / multi-PIE 场景下的行为失真。 |

### 发现 C-03：公开 `FAngelscriptEngine::Create()` 已退化成测试工厂别名，无法构造正式 runtime full engine

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | `AngelscriptEngine.cpp` 605-625，819-920；`AngelscriptDependencyInjectionTests.cpp` 280-287；`AngelscriptMultiEngineTests.cpp` 183-199 |
| 描述 | 头文件公开的 `FAngelscriptEngine::Create()` 按名字是通用 full-engine 工厂，但实现只是 `return CreateTestingFullEngine(...)`，而后者固定执行 `InitializeForTesting()`。`InitializeForTesting()` 与正式 `Initialize()` 不是一回事：前者只做最小初始化和 `BindScriptTypes()`，不会走 threaded initialize、`Initialize_AnyThread()`、`PostInitialize_GameThread()`、正式 script-root 发现/目录创建以及后续 runtime side effects。更关键的是，测试已经把这个退化语义固化下来：`Create.LegacyAliasSkipsProductionDirectorySetup` 明确断言 `Create()` 不会跑 production script-root setup，`MultiEngine.Create.Full` 则继续把这条 API 当“full engine” 入口使用。 |
| 根因 | Runtime 从显式 global factory 迁移到 `RuntimeModule + Subsystem` 生命周期后，旧公共工厂 API 只保留了名字，没有同步收敛或重命名；历史兼容别名被继续暴露成正式入口。 |
| 影响 | 外部调用方如果按名字把 `Create()` 当成“创建完整运行时 engine”的公共入口，会得到一台测试语义的 engine：初始化阶段缺失、行为边界与 RuntimeModule/Subsystem 不一致，而且这种错位已经被测试锁成当前 contract，后续很难通过自动化自然暴露。 |

### 发现 A-13：`InitializeAngelscript()` 复用 current engine 时直接二次调用 `Initialize()`，会重建底层 `asCScriptEngine`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptRuntimeModule.cpp` 138-159；`AngelscriptEngine.cpp` 819-857，922-927，1123-1229，1280-1323；对比 859-864 |
| 描述 | `FAngelscriptRuntimeModule::InitializeAngelscript()` 在检测到 `TryGetCurrentEngine()` 命中时，直接执行 `CurrentEngine->Initialize()`。但 `FAngelscriptEngine::Initialize()` 没有任何 `Engine != nullptr` 保护，进入后总会跑 `PreInitialize_GameThread()`，而该函数在 1320 行无条件执行 `Engine = (asCScriptEngine*)asCreateScriptEngine(...)`，也就是直接覆盖现有底层 engine 指针。对比之下，同文件 `InitializeForTesting()` 明确在 861-864 行对“已经初始化过”做了早返回，说明代码库本身已经承认这类入口需要幂等保护。 |
| 根因 | RuntimeModule 把“当前能解析到一个 engine”误当成“当前有一个尚未初始化的 wrapper”；而 `FAngelscriptEngine` 的正式初始化路径本质上是一次性构造流程，不是可重入的 attach 操作。 |
| 影响 | 对已初始化 full engine 走这条分支时，旧 `asCScriptEngine`、context pool 与相关资源会在没有先 `Shutdown()` 的情况下被新指针覆盖；已验证事实：`Shutdown()` 只会释放 `bOwnsEngine` 为真的当前 `Engine`（1136-1223），不会回头处理已经丢失句柄的旧实例。推断：如果 current engine 恰好还是 clone wrapper，`InitializeOwnedSharedState()` 又会因 `!bOwnsEngine` 直接跳过（922-927），新建出来的底层 engine 甚至不会进入 shared-state owner 链，最终在 clone `Shutdown()` 时被直接遗失。 |

---

## 分析 (2026-04-08 02:33)

### 发现 B-03：`StaticNames` / `StaticNamesByIndex` 是进程级追加式全局表，但 RuntimeCore 没有任何 teardown 复位路径

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | `AngelscriptEngine.cpp` 1236-1251，1560-1567；`AngelscriptPreprocessor.cpp` 2045-2054；`PrecompiledData.cpp` 2436-2440 |
| 描述 | `FAngelscriptEngine::StaticNames` 和 `StaticNamesByIndex` 是静态全局表。正常初始化未命中 precompiled data 时，`Initialize_AnyThread()` 只做 `StaticNames.Reserve(7000)`；预处理阶段 `GenerateStaticName()` 会持续向两张表 `Emplace/Add` 新条目。`Shutdown()` 结束时只清空当前 engine 实例字段与模块级容器，没有任何 `StaticNames.Empty()` / `StaticNamesByIndex.Empty()` 之类的复位逻辑。我已对 `Plugins/Angelscript/Source/AngelscriptRuntime` 执行 `rg -n "StaticNames\\.Empty|StaticNamesByIndex\\.Empty"`，结果为空。更进一步，`PrecompiledData.cpp` 在重新加载静态名表前明确 `check(FAngelscriptEngine::StaticNames.Num() == 0)`。 |
| 根因 | Static name 映射被实现成进程级单例缓存，但 RuntimeCore 生命周期只管理 `FAngelscriptEngine` 实例级资源，没有把这两张静态表纳入 full engine teardown 合同。 |
| 影响 | 一旦同进程里发生第二次 full engine 初始化，上一轮脚本运行留下的 static-name 索引会直接污染下一轮；若第二轮改走 precompiled-data 加载路径，`PrecompiledData.cpp` 的空表断言会直接触发。即使不崩溃，static-name 索引也不再对应当前 engine epoch 的真实脚本集合，破坏“全局状态收口完整性”。 |

---

## 分析 (2026-04-08 02:40)

### 发现 B-04：`StartHotReloadThread()` 启动的后台线程没有绑定发起它的 engine，而是在线程内重新走全局解析

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 71，391-415，718-733，1674-1700 |
| 描述 | `GAngelscriptEngineContextStack` 是文件静态全局数组，`Peek()` 永远返回当前栈顶 engine。`StartHotReloadThread()` 虽然是成员函数，但内部创建的 `FAngelscriptHotReloadThread::Run()` 没有捕获 `this`，而是在 1680 行直接执行 `auto& Manager = FAngelscriptEngine::Get();`。`Get()` 又会转到 `TryGetCurrentEngine()`，优先返回全局 `ContextStack` 栈顶，其次才是 `UAngelscriptGameInstanceSubsystem::GetCurrent()`。也就是说，热重载线程操作的不是“启动它的那台 engine”，而是“线程启动时全局解析出来的当前 engine”。 |
| 根因 | 热重载线程的 owner 边界被错误地实现成了动态全局查询，而不是在创建线程时显式捕获/持有目标 `FAngelscriptEngine`。这与 RuntimeCore 其它依赖 `FAngelscriptEngineScope` 表达瞬时作用域的设计是冲突的。 |
| 影响 | 只要同进程里存在多台 engine，或者 `ContextStack` 栈顶在后台线程运行期间发生变化，热重载线程就会读取/修改错误实例的 `bUseHotReloadCheckerThread`、`bWaitingForHotReloadResults`、`FileHotReloadState` 与 reload 结果缓存，造成跨实例热重载串线。即使不考虑 A-05 里的悬挂线程问题，这条实现本身也已经破坏了 subsystem / engine 之间的集成边界。 |

---

## 分析 (2026-04-08 02:47)

### 发现 A-14：engine shutdown 只回收当前线程的 free-context 池，其他线程缓存的 `asCContext` 会越过引擎生命周期

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptengine.cpp` |
| 行号 | `AngelscriptEngine.cpp` 87，366-367，1185-1222，1287-1289，1905-1913；`AngelscriptEngine.h` 661；`as_scriptengine.cpp` 1157-1165 |
| 描述 | RuntimeCore 把空闲脚本 context 缓存在 `thread_local GAngelscriptContextPool` 中。无论 `ReleaseOwnedSharedStateResources()` 还是 `FAngelscriptEngine::Shutdown()`，都只会对当前线程可见的 `GAngelscriptContextPool.FreeContexts` 调用 `ReleaseContextsForScriptEngine(...)`，外加清理当前 wrapper 的 `GlobalContextPool`；代码里不存在任何“枚举所有线程本地池”的注册表。与此同时，`FAngelscriptContextPool::~FAngelscriptContextPool()` 明确要等到线程退出时才遍历 `FreeContexts` 并 `Context->Release()`。底层 AngelScript 的 `asCScriptEngine::ShutDownAndRelease()` 又在 1161-1165 行明确说明：如果对象池在 engine shutdown 之后再释放 context，会引发后续 memory access violation。 |
| 根因 | Context 池的所有权被拆成了“每线程 thread_local 池 + 每 engine wrapper 的 global 池”，但 teardown 只管理了当前线程和当前 wrapper，看不到其它仍然存活线程上的缓存 context。 |
| 影响 | 只要某个 worker thread 曾经缓存过属于该 `asCScriptEngine` 的 free context，而 engine 在该线程退出前先走了 `Shutdown()`，这些 context 就会越过引擎生命周期存活。推断：它们要么在后续线程退出时晚释放并命中 AngelScript 已注明的 memory-access-violation 风险，要么长期滞留形成跨 epoch 资源泄漏；两者都说明 RuntimeCore 的资源回收边界并不完整。 |

---

## 分析 (2026-04-08 02:55)

### 发现 D-04：自动化测试没有覆盖 ambient world context 的恢复合同，`FAngelscriptGameThreadScopeWorldContext` 完全处于未验证状态

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 行号 | `AngelscriptEngine.cpp` 437-509；`AngelscriptEngine.h` 710-724；`AngelscriptEngineIsolationTests.cpp` 218-229 |
| 描述 | RuntimeCore 明确同时维护两套 world-context 状态：engine 内部的 `WorldContextObject` 和进程级 ambient world context。`FAngelscriptEngineScope` 构造时既保存 `PreviousEngineWorldContext`，也单独保存 `PreviousWorldContext`；`FAngelscriptGameThreadScopeWorldContext` 甚至完全围绕 ambient world context 建模。但现有测试只在 `AngelscriptEngineIsolationTests.cpp:218-229` 断言 `GetCurrentWorldContextObject()` 的嵌套恢复，整个 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下对 `GetAmbientWorldContext`、`TryGetCurrentWorldContextObject`、`FAngelscriptGameThreadScopeWorldContext` 的 `rg` 检索均无命中。 |
| 根因 | 自动化测试把 world-context 恢复视为“active engine 上的一个成员字段恢复”，没有把 ambient world context 视为独立合同，更没有覆盖其与 `FAngelscriptEngineScope` / `FAngelscriptGameThreadScopeWorldContext` 交互时的嵌套行为。 |
| 影响 | 任何只影响 ambient world context 的回归都不会被当前测试发现，包括恢复顺序错误、作用域嵌套丢失、以及 subsystem 依赖 `GetAmbientWorldContext()` 解析当前 world 的路径异常。对于 RuntimeCore 当前这种“engine-local + ambient”双状态实现，这属于关键覆盖缺口。 |

---

## 分析 (2026-04-08 03:02)

### 发现 A-15：`TryGetCurrentEngine()` 的 subsystem fallback 没有线程边界保护，但它已被明确用于后台线程路径

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` |
| 行号 | `AngelscriptEngine.cpp` 718-733，1674-1690，1811-1818，1830-1836，1889-1896；`AngelscriptGameInstanceSubsystem.cpp` 94-113 |
| 描述 | `TryGetCurrentEngine()` 在 `ContextStack` 为空时，会直接调用 `UAngelscriptGameInstanceSubsystem::GetCurrent()`。而 `GetCurrent()` 内部没有任何 `IsInGameThread()` / `CanUseGameThreadData()` 保护，直接访问 `GEngine->GetWorldFromContextObject(...)`、`World->GetGameInstance()` 和 `GetSubsystem<>()`。与此同时，后台热重载线程的 `Run()` 明确在 worker thread 中调用 `FAngelscriptEngine::Get()`，而 `FAngelscriptPooledContextBase::Init()` / 析构也会在拿取或归还 context 时调用 `TryGetCurrentEngine()`。这说明 subsystem fallback 不是“只在 game thread 上偶尔会走到”的冷路径，而是已被 RuntimeCore 用在无显式线程限制的代码里。 |
| 根因 | current-engine 解析把 subsystem 查找当作全局兜底能力，但没有把它限制在 game-thread 可安全访问 `GEngine/UWorld/UGameInstance` 的上下文内；调用方也没有统一建立 `FAngelscriptEngineScope` 来避免走这条 fallback。 |
| 影响 | 只要后台线程在 `ContextStack` 为空时触发 current-engine 解析，就会越过线程边界触碰 `GEngine` 和世界对象图。推断：结果可能表现为竞态、非线程安全的 UObject 访问，或把 worker-thread 的逻辑意外耦合到某个 ambient world/subsystem 上；这会把 RuntimeCore 的“全局兜底解析”变成跨线程不稳定源。 |

---

## 分析 (2026-04-08 02:43)

### 发现 B-05：显式 `DesiredScriptEngine` 与 `AssignWorldContext()` 的全局解析脱钩，类生成调用会把 world context 写入错误 engine

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptEngine.cpp` 746-753，1763-1782；`ASClass.cpp` 1095-1116，1129-1133，1584-1587 |
| 描述 | `FAngelscriptContext` 和 `FAngelscriptGameThreadContext` 都接收显式 `DesiredScriptEngine`，并在基类初始化时按这个 engine 取/建 `asCContext`；但它们随后设置 world context 时调用的仍是 `FAngelscriptEngine::AssignWorldContext()`。该函数并不依据 `DesiredScriptEngine` 反查 owner，而是直接对 `TryGetCurrentEngine()` 命中的“当前 engine”写 `WorldContextObject`，再同步 `GAmbientWorldContext`。与此同时，`ASClass.cpp` 的构造函数执行与函数调度路径大量直接创建 `FAngelscriptContext Context(Object, Class->ConstructFunction->GetEngine())`、`FAngelscriptGameThreadContext Context(Object, RealFunction->GetEngine())`，这些调用点本身没有建立对应的 `FAngelscriptEngineScope`。结果是“脚本 VM 所属 engine”和“world context 被写入的 engine”在设计上可以分离。 |
| 根因 | RuntimeCore 把 world-context 路由绑定到了进程级 `TryGetCurrentEngine()` 全局解析，而不是绑定到已经显式传入的 `DesiredScriptEngine` / `RealFunction->GetEngine()`；ClassGenerator 侧又默认相信“函数所属 engine”足以确定执行边界，没有补一个与之配对的 `FAngelscriptEngineScope`。 |
| 影响 | multi-engine、clone 或 subsystem adoption 场景下，脚本函数完全可能在 Engine B 的 `asCScriptEngine` 上执行，却把 `WorldContextObject` 改写到 Engine A（当前栈顶或 ambient world 解析出的 subsystem engine）上。后续依赖 `TryGetCurrentWorldContextObject()` 的 bind、日志、timer、subsystem 查询都会落到错误 world，导致跨实例 world state 串线。 |

---

## 分析 (2026-04-08 02:44)

### 发现 D-05：现有自动化只验证 `EngineScope` 的 world-context 恢复，没有覆盖 `DesiredScriptEngine` 与 current-engine 失配的执行路径

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | `AngelscriptEngineIsolationTests.cpp` 194-229；`AngelscriptTestEngineHelperTests.cpp` 625-677 |
| 描述 | 当前 world-context 相关自动化只覆盖了两类 RAII 语义：`FAngelscriptEngineScope` 的嵌套恢复，以及测试 helper 的 `FAngelscriptGameThreadScopeWorldContext` 恢复。代码库中找不到任何一个测试去构造 `FAngelscriptContext(Object, DesiredScriptEngine)` 或 `FAngelscriptGameThreadContext(Object, DesiredScriptEngine)`，并在 “current engine 与 `DesiredScriptEngine` 不同” 的前提下断言 `WorldContextObject` 仍然写入正确实例。我对 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests` 和 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg -n "FAngelscriptGameThreadContext|FAngelscriptContext\\("`，命中只有生产调用点，没有对应的 world-context 回归测试。 |
| 根因 | 现有测试模型把 world-context 正确性等同于 scope helper 的恢复正确，而没有把 ClassGenerator 真实 dispatch 路径视为独立契约；multi-engine 测试也主要停留在 shared-state 和生命周期层面，没有进入“函数所属 engine 与当前解析 engine 不一致”这一边界。 |
| 影响 | B-05 这类只会在 generated call path、clone 或 subsystem adoption 组合下暴露的问题，不会被现有自动化拦截。测试集能证明 scope 类本身会恢复状态，但不能证明真正的脚本执行入口会把 world context 绑定到正确 engine。 |

---

## 分析 (2026-04-08 02:45)

### 发现 C-04：公开的 global-engine API 已退化成空壳，外部调用方无法通过该接口真正管理引擎生命周期

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp` |
| 行号 | `AngelscriptEngine.h` 461-464；`AngelscriptEngine.cpp` 736-779；`AngelscriptSubsystemTests.cpp` 352-359，380-382，405-407；`AngelscriptMultiEngineTests.cpp` 15-25；`AngelscriptDependencyInjectionTests.cpp` 8-13 |
| 描述 | RuntimeCore 仍在公共头文件暴露 `TryGetGlobalEngine()`、`SetGlobalEngine()`、`GetOrCreate()`、`DestroyGlobal()` 这组 global-engine API，但实现已经不再提供真正的“global owner”语义：`TryGetGlobalEngine()` 只是返回 `TryGetCurrentEngine()`，`SetGlobalEngine(FAngelscriptEngine* InEngine)` 直接忽略传入参数，`DestroyGlobal()` 则恒定返回 `false`。与此同时，测试代码仍把这套 API 当成生命周期控制面使用，例如 `ResetToIsolatedEngineState()` 会在 `IsInitialized()` 后调用 `DestroyGlobal()`，`SubsystemTests` 也继续把 `TryGetGlobalEngine()` 当作“模块创建出的全局主引擎”读取接口。 |
| 根因 | Runtime 从 global singleton 迁移到 `RuntimeModule + UAngelscriptGameInstanceSubsystem + ContextStack` 后，旧 API 名称被保留下来以兼容调用点，但实现没有提供等价替代，也没有在类型层面移除/废弃这些入口。结果是 API surface 还在，实际 contract 已经失效。 |
| 影响 | 任何仍按名称理解这套接口的调用方，都无法通过它们显式设置、销毁或获取一个稳定的 process-global engine，只能拿到“当前上下文碰巧解析到的 engine”。这会让外部模块、测试基座和工具代码误判生命周期状态，并把全局状态收口问题隐藏在一个看似存在、实则无效的 API 层后面。 |

---

## 分析 (2026-04-08 02:52)

### 发现 B-06：`ContextStack` 对同一 engine 的嵌套 scope 不计深度，内层析构会提前移除外层 current engine

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 391-409，437-507 |
| 描述 | `FAngelscriptEngineContextStack::Push()` 只有在栈顶不是同一个 `Engine` 时才 `Add`；因此对同一 `FAngelscriptEngine` 的第二层 `FAngelscriptEngineScope` 是 no-op。对应地，内层 scope 析构时 `FAngelscriptEngineContextStack::Pop(Engine)` 会把唯一那一层栈项直接弹掉，导致外层 scope 仍存活时 `TryGetCurrentEngine()` 已经返回 `nullptr` 或其它 engine。这里不是推断，而是由 `Push`/`Pop` 的对称性直接决定的确定性行为。 |
| 根因 | RuntimeCore 把 `ContextStack` 当成“去重后的当前 engine 列表”实现，而 `FAngelscriptEngineScope` 却把它当成支持嵌套 RAII 的真实栈使用；相同 engine 的嵌套进入次数没有被记录。 |
| 影响 | 任何在已有 `FAngelscriptEngineScope` 内再次进入同一 engine 的代码，都会在内层析构后丢失 current-engine 解析与 ambient world-context 同步。后续 `Get()`、`TryGetCurrentEngine()`、`AssignWorldContext()`、`GetPackage()` 等公共入口会在外层 scope 仍然存活时表现为“未初始化”或解析到错误实例，直接破坏 ContextStack 的核心契约。 |

### 发现 C-05：`IsInitialized()` 把“subsystem 对象存在”误报成“engine 已初始化”，会阻断运行时重新初始化

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp` |
| 行号 | `AngelscriptEngine.cpp` 676-679；`AngelscriptGameInstanceSubsystem.cpp` 32-52，66-69，94-113；`IntegrationTest.cpp` 41-48；`UnitTest.cpp` 47-54 |
| 描述 | `FAngelscriptEngine::IsInitialized()` 只要 `ContextStack` 非空，或者 `UAngelscriptGameInstanceSubsystem::GetCurrent()` 返回了 subsystem 对象，就返回 `true`。但 subsystem 自己的状态机并不是“对象存在即已初始化”：`Deinitialize()` 会把 `PrimaryEngine = nullptr`、`bInitialized = false`，`IsAllowedToTick()` 也明确要求两者都成立才算可用。`GetCurrent()` 却完全不看这些字段，只按 ambient world context 找到 `GameInstance` 后直接返回 subsystem。因此 `IsInitialized()` 在 subsystem 仍存活但已 deinitialize 的状态下会报告已初始化，和真实 engine 可用性不一致。 |
| 根因 | RuntimeCore 在公共生命周期查询里把“能解析到 subsystem 容器对象”混同为“容器内已经挂着一个可用 engine”，没有复用 subsystem 自己定义的初始化判定。 |
| 影响 | `EnsureAngelscriptIntegrationRuntimeInitialized()` 与 `EnsureAngelscriptUnitRuntimeInitialized()` 都把 `IsInitialized()` 当作是否需要调用 `InitializeAngelscript()` 的门禁。只要进入这种假阳性状态，它们就会跳过真正初始化，并把后续代码留在“外部以为 runtime 已就绪、内部其实没有 current engine”的不一致状态；随后任何依赖 `Get()`、`GetPackage()` 或脚本执行入口的路径都可能直接命中 check 或错误分支。 |

### 发现 B-07：`FAngelscriptEngineScope` 记录了进入前的 ambient world context，但退出时没有恢复它

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `AngelscriptEngine.cpp` 445-449，501-507；`AngelscriptEngine.h` 649，710-720 |
| 描述 | `FAngelscriptEngineScope` 在带 `InWorldContext` 的构造路径里把进入前的 ambient world context 保存到 `PreviousWorldContext`，随后调用 `AssignWorldContext(InWorldContext)`。但 `Reset()` 只恢复 `Engine->WorldContextObject`，然后 `Pop(Engine)` 并执行 `SyncAmbientWorldContextFromCurrentEngine()`；整个退出路径从未使用 `PreviousWorldContext`。这意味着当外层只有 `FAngelscriptGameThreadScopeWorldContext` 之类的 ambient scope、没有外层 current engine 时，内层 `FAngelscriptEngineScope` 退出后 ambient world context 会被强制同步成 `nullptr`，而不是恢复为进入前的值。对比之下，`FAngelscriptGameThreadScopeWorldContext` 的析构明确会 `AssignWorldContext(PreviousWorldContext)`。 |
| 根因 | `FAngelscriptEngineScope` 的实现把 world-context 恢复绑定到了“当前 engine 栈顶同步”，却没有处理“进入前存在 ambient context，但退出后没有 current engine”这一合法场景；保存下来的 `PreviousWorldContext` 因而变成了死状态。 |
| 影响 | 任何把 ambient world context scope 与 `FAngelscriptEngineScope` 叠加的代码，都会在内层 engine scope 退出后丢失外层 world context。随后依赖 `GetAmbientWorldContext()` / `TryGetCurrentWorldContextObject()` 的 subsystem 查询、timer、logging、`Bind_Subsystems`、`Bind_UWorld` 等路径会在外层 scope 仍然存活时解析到空 world 或错误 world，破坏上下文恢复合同。 |

### 发现 D-06：自动化测试没有覆盖 `EngineScope` 的同 engine 嵌套和 ambient-scope 叠加边界

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | `AngelscriptEngineIsolationTests.cpp` 176-191；`AngelscriptTestEngineHelperTests.cpp` 625-678 |
| 描述 | 当前针对 scope 语义的自动化只覆盖了两类场景：`AngelscriptEngineIsolationTests.cpp` 验证“不同 engine 的嵌套 scope”恢复是否正确，`AngelscriptTestEngineHelperTests.cpp` 则分别验证“单独的 world-context scope”与“单独的 engine scope”能否恢复进入前状态。仓库里找不到任何测试去验证“同一 engine 的两层 `FAngelscriptEngineScope` 嵌套”或“外层 `FAngelscriptGameThreadScopeWorldContext` + 内层 `FAngelscriptEngineScope`”这两条边界。`rg` 结果只命中现有这三个断言文本，没有对应的新边界用例。 |
| 根因 | 测试模型默认把 scope 问题拆成“engine 嵌套”和“world-context restore”两个独立主题验证，没有把它们在真实调用链里叠加后的组合行为纳入回归集。 |
| 影响 | B-06 与 B-07 这类发生在 scope 组合边界上的缺陷，即使基础 restore 测试全绿也不会暴露。当前自动化只能证明简单场景工作正常，不能证明 RuntimeCore 最关键的 context/world-context 原语在嵌套使用时仍然安全。 |

---

## 分析 (2026-04-08 03:05)

### 发现 B-08：ambient world-context scope helper 会反向污染 current engine 的 `WorldContextObject`

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptEngine.h` 710-765；`AngelscriptEngine.cpp` 746-753，1763-1783；`ASClass.cpp` 306-307，679-696 |
| 描述 | `FAngelscriptGameThreadScopeWorldContext`、`FAngelscriptContext` 和 `FAngelscriptGameThreadContext` 都只保存 `PreviousWorldContext = GetAmbientWorldContext()`，析构时统一调用 `AssignWorldContext(PreviousWorldContext)`。但 `AssignWorldContext()` 不是单纯设置 ambient world context：它还会先把 `TryGetCurrentEngine()` 命中的 `CurrentEngine->WorldContextObject` 改成同一个值。结果是这些 helper 退出时恢复的不是 engine 进入前自己的 `WorldContextObject`，而是“进入前的 ambient world context”。`ASClass.cpp` 的 JIT/raw call 入口又在真实执行路径里高频创建 `FAngelscriptGameThreadScopeWorldContext`，所以这不是死代码。 |
| 根因 | RuntimeCore 把 ambient world context 和 engine-local `WorldContextObject` 绑定到同一个 setter 上，但这三个 RAII helper 只记录了 ambient 侧旧值，没有像 `FAngelscriptEngineScope` 那样单独保存 `PreviousEngineWorldContext`。 |
| 影响 | 只要当前 engine 原本持有的 `WorldContextObject` 与 ambient world context 不同，临时 world-context scope 结束后就会把 engine 本地 world context 覆盖成错误对象。随后 `TryGetCurrentWorldContextObject()`、subsystem 解析、`Bind_UWorld`/`Bind_Subsystems`、日志与脚本调用边界都会在同一 engine 生命周期内读取到被 scope 污染后的 world context，造成跨对象或跨 world 串线。 |

### 发现 A-16：debug stack / prototype 元数据只分配不释放，调试路径会把 `asCContext` 和 `asCScriptFunction` 绑成永久泄漏源

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp` |
| 行号 | `AngelscriptEngine.cpp` 5333-5346，5417-5425，5477-5512；`as_context.cpp` 243-245，300-334；`as_scriptfunction.cpp` 410-447 |
| 描述 | `GetStack()` 首次命中时把 `new FAngelscriptDebugStack` 塞进 `asCContext::DebugFramePtr`；`GetDebugPrototype()` 首次命中时把 `new FDebugValuePrototype` 塞进 `asCScriptFunction::DebugPrototypePtr`。后续 `AngelscriptLineCallback()` 的 `WITH_AS_DEBUGVALUES` 分支会持续通过这两个缓存实例化 `Frame.Variables`。但 RuntimeCore 自己没有任何 `delete DebugFramePtr` / `delete DebugPrototypePtr` 路径；底层 `asCContext::~asCContext()` 只调用 `DetachEngine()`，`asCScriptFunction::~asCScriptFunction()` / `DestroyInternal()` 也没有清理这两个扩展指针。`FAngelscriptDebugFrame::~FAngelscriptDebugFrame()` 虽然会 `Prototype->Free(Variables)`，但前提是整个 `FAngelscriptDebugStack` 被析构，而这一步从未发生。 |
| 根因 | RuntimeCore 通过第三方类型上的裸 `void*` 扩展槽缓存调试元数据，却没有把这些扩展对象接入 AngelScript context / function 的销毁路径，也没有在 `DiscardModule()` 或 engine teardown 时补清理。 |
| 影响 | 只要调试值路径被走到，一批 `FAngelscriptDebugStack`、`FDebugValuePrototype` 以及仍挂在 frame 上的 `Variables` 就会永久附着在 context / script function 生命周期上。编辑器热重载、反复丢弃模块或重复创建 full engine 时，这些调试元数据会跨轮次累积，形成稳定的内存泄漏，并把旧脚本 epoch 的 debug 状态残留到后续运行中。 |

---

## 分析 (2026-04-08 03:16)

### 发现 C-06：non-editor 热重载的删除/重命名管线在 RuntimeCore 中实际上不可达

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp` |
| 行号 | `AngelscriptEngine.h` 406-419，479；`AngelscriptEngine.cpp` 1615-1620，1670-1671，2729-2778，2859-2895；`AngelscriptDirectoryWatcherInternal.cpp` 55-61 |
| 描述 | non-editor 开发模式下，RuntimeCore 只会启用 `StartHotReloadThread()`，并由 `CheckForFileChanges()` 轮询脚本目录。这个扫描器只遍历当前仍然存在的脚本文件，把“新增/修改”写进 `FileChangesDetectedForReload`，却从不把 `FileHotReloadState` 里已经消失的条目转成 `FileDeletionsDetectedForReload`。与此同时，`CheckForHotReload()` 明确依赖 `FileDeletionsDetectedForReload` 和 `LastFileChangeDetectedTime` 来延迟消费删除事件；而当前仓库里对 `LastFileChangeDetectedTime` 的赋值只存在于 editor directory watcher，RuntimeCore 自己的轮询路径没有任何写入点。 |
| 根因 | RuntimeCore 把“删除/重命名检测”拆成了独立队列和时间窗逻辑，但 non-editor 轮询实现只完成了“扫描现存文件”，没有完成“枚举已消失文件”和“推进最近变更时间”这两个必要步骤。 |
| 影响 | standalone/non-editor 开发模式下，删除或重命名 `.as` 文件不会触发预期的 unload / full reload 语义，旧脚本模块会继续残留在运行时；即使外部代码手工往 `FileDeletionsDetectedForReload` 填队列，rename 保护窗也不会按注释工作，因为时间戳从未在这条路径更新。结果是 hot reload 对“删除/重命名”这类高风险变更失真，已删脚本仍可能继续参与执行。 |

### 发现 A-17：热重载轮询线程与 game thread 裸共享 reload 队列和状态位，存在确定性的并发数据竞争

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptEngine.h` 406-410；`AngelscriptEngine.cpp` 1092，1674-1690，2729-2778，2859-2895 |
| 描述 | `FAngelscriptHotReloadThread::Run()` 会在后台线程里直接执行 `Manager.CheckForFileChanges()`，并写 `Manager.bWaitingForHotReloadResults = false`。同时，game thread 的 `CheckForHotReload()` 每 tick 都会读取/写入同一个 `bWaitingForHotReloadResults`，并直接 `Append` / `Empty` `FileChangesDetectedForReload`、`FileDeletionsDetectedForReload`、`QueuedFullReloadFiles`。更糟的是，后台线程在 `CheckForFileChanges()` 中读写 `FileHotReloadState`，而主线程的 `DiscardModule()` 又会在 1092 行删除同一个 `TMap` 中的条目。整条路径里看不到 `FCriticalSection`、原子容器或消息转移；`bWaitingForHotReloadResults` 只是 `volatile bool`，无法为 `TArray` / `TMap` 提供并发安全。 |
| 根因 | RuntimeCore 把原本的单线程热重载队列直接暴露给后台轮询线程复用，只加了一个 `volatile` 标志位作为“完成通知”，却没有为共享容器和状态建立任何同步协议。 |
| 影响 | 这不是风格问题，而是标准意义上的 data race：热重载文件队列可能被重复消费、丢失或在 `Append/Empty/Add/Remove` 交错时损坏；`FileHotReloadState` 也可能在扫描与模块丢弃并发时出现未定义行为。结果既包括静默漏 reload，也包括难复现的崩溃和热重载状态错乱。 |

### 发现 B-09：`UpdateLineCallbackState()` 把 engine-local 调试状态写进进程级静态开关，multi-engine 场景下谁最后更新谁生效

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 行号 | `AngelscriptEngine.cpp` 1132-1251，5429-5460；`AngelscriptDebugServer.cpp` 375，495，573，646，741，835-922，1285；`AngelscriptEngineIsolationTests.cpp` 349-367 |
| 描述 | `UpdateLineCallbackState()` 依据当前 `FAngelscriptEngine` 实例上的 `DebugServer` / `CodeCoverage` 状态，最终直接覆盖第三方全局静态 `asCContext::CanEverRunLineCallback` 和 `asCContext::ShouldAlwaysRunLineCallback`。代码库同时又明确支持多个 full engine 并存。更危险的是，`AngelscriptDebugServer.cpp` 里更新这两个开关时并不一致：有的路径调用 `OwnerEngine->UpdateLineCallbackState()`，有的路径直接走 `FAngelscriptEngine::Get().UpdateLineCallbackState()`，把“哪个 engine 应该决定全局开关”再次交给 current-engine 解析。`Shutdown()` 里也没有任何重新计算或清零这两个静态位的逻辑。 |
| 根因 | line callback 启停被设计成 engine-local 能力，但实现选择把结果写回 AngelScript 进程级静态变量；同时调试入口没有统一坚持 owner-engine 更新协议。 |
| 影响 | 只要同进程里存在多台 engine，最后一次调用 `UpdateLineCallbackState()` 的实例就会替所有 engine 决定 line callback 行为。结果可能是 Engine B 关闭了 Engine A 正在使用的断点/coverage 回调，也可能是已经销毁的 engine 留下“始终跑 line callback”的全局副作用，直接破坏 multi-engine 隔离和调试正确性。 |

### 发现 D-07：现有 hot reload 自动化绕开了 RuntimeCore 自己的轮询扫描器，删除/重命名与并发状态没有回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp` |
| 行号 | `AngelscriptHotReloadFunctionTests.cpp` 49-58；`AngelscriptDirectoryWatcherTests.cpp` 194-222 |
| 描述 | Runtime hot reload 测试辅助 `FAngelscriptHotReloadTestAccess::QueueFileChange()` 直接把条目塞进 `Engine.FileChangesDetectedForReload`，只验证“队列里已经有一个改动”后的消费行为；它不触发 `CheckForFileChanges()`，也不覆盖 `FileHotReloadState`、`LastFileChangeDetectedTime`、`bWaitingForHotReloadResults` 这些 RuntimeCore 自己的扫描/协调状态。相反，当前真正覆盖“删除/重命名”语义的自动化都在 editor 侧 `AngelscriptDirectoryWatcherTests.cpp`，验证的是 directory watcher 入队而不是 non-editor 轮询线程。已验证事实：我对 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests` 和 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg -n "CheckForFileChanges|LastFileChangeDetectedTime|FileHotReloadState|bWaitingForHotReloadResults"`，没有命中任何运行时测试。 |
| 根因 | 测试体系把 hot reload 拆成了“editor 生产者测试”和“runtime 消费者测试”，但 RuntimeCore 自己那条 non-editor 轮询生产者路径没有对应回归用例。 |
| 影响 | C-06 和 A-17 这类只会出现在 RuntimeCore 扫描器/后台线程里的缺陷，不会被现有自动化拦截。测试可以证明 editor watcher 会正确排队，也可以证明手工塞进队列的变更能被消费，但不能证明 standalone/non-editor 的真实 hot reload 流程可用。 |

---

## 分析 (2026-04-08 03:28)

### 发现 A-18：`CodeCoverage` 生命周期未纳入 engine teardown，自动化控制器会保留悬空回调

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp` |
| 行号 | `AngelscriptEngine.cpp` 335-376，1160-1233，1460-1462，1628-1633；`AngelscriptCodeCoverage.cpp` 22-28 |
| 描述 | `Initialize_AnyThread()` 在 1460-1462 行按需 `new FAngelscriptCodeCoverage`，随后通过 `OnPostEngineInit` 在 1628-1633 行调用 `CodeCoverage->AddTestFrameworkHooks()`。而 `AddTestFrameworkHooks()` 自己又把 `this` 以 `AddRaw` 形式注册到 `AutomationController->OnTestsAvailable()` 和 `OnTestsComplete()`。但 RuntimeCore 的两条释放路径都没有处理这个对象：`ReleaseOwnedSharedStateResources()` 只删除 `DebugServer`、`StaticJIT`、`PrecompiledData`、`PrimaryContext` 和 `ScriptEngine`，`Shutdown()` 也只显式清理 `DebugServer` / `StaticJIT` / `PrecompiledData` 成员并把字段置空，看不到任何 `delete CodeCoverage` 或委托解绑。 |
| 根因 | `CodeCoverage` 被当成了和调试器同级的运行期服务来创建，但没有被接入 `FAngelscriptOwnedSharedState` 的 owner 资源模型，teardown 也没有保存和撤销 AutomationController delegate handle。 |
| 影响 | 一旦 full engine 被销毁，`AutomationController` 仍会在后续测试刷新或结束时回调已失效的 `FAngelscriptCodeCoverage` 实例，形成稳定的 use-after-free 崩溃窗口；即使暂时没触发回调，对象本身和其覆盖率映射也会跨 engine epoch 泄漏，污染后续测试运行。 |

---

## 分析 (2026-04-08 03:31)

### 发现 B-10：`StaticNames` / `StaticNamesByIndex` 是跨 epoch 泄漏的进程级状态，和 precompiled-data 装载前提直接冲突

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | `AngelscriptEngine.cpp` 69-70，1236-1248；`AngelscriptPreprocessor.cpp` 2045-2054；`PrecompiledData.cpp` 2436-2440 |
| 描述 | `FAngelscriptEngine::StaticNames` 和 `StaticNamesByIndex` 在 69-70 行定义为进程级静态表。预处理器每次遇到 `GenerateStaticName()` 都会把新名字写进这两个全局容器，并把索引编码进脚本产物。可是 `Shutdown()` 结束时只清理实例级模块、包和 root path，完全没有清空这两个静态表。与此同时，precompiled-data 装载在 2436 行明确 `check(FAngelscriptEngine::StaticNames.Num() == 0)`，然后才把 archive 里的 static-name 表恢复到运行时。也就是说，RuntimeCore 一边把 static names 设计成“跨实例常驻”，一边又要求 precompiled-data 恢复时它必须为空。 |
| 根因 | static-name 索引既被用作脚本编译期的全局 intern 表，又被 precompiled-data 视为每个 script-engine epoch 独享的可重建状态；生命周期模型没有在 full engine teardown 时统一收口这组进程级缓存。 |
| 影响 | 只要同一进程里先跑过一次走预处理器的 engine epoch，再进入依赖 precompiled-data 恢复 static names 的 epoch，就会直接命中 `check(StaticNames.Num() == 0)`；即使没有走到该断言，旧 epoch 残留的 `StaticNamesByIndex` 也会继续污染新 epoch 的 name-to-index 分配，破坏“full engine teardown 后回到干净脚本命名状态”的契约。 |

---

## 分析 (2026-04-08 03:35)

### 发现 B-11：clone wrapper 没有继承共享 `DebugServer` 能力，当前 engine 解析落到 clone 时调试状态会失真

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp` |
| 行号 | `AngelscriptEngine.cpp` 629-650，2848-2856，5434-5443；`AngelscriptDebugServer.cpp` 495，835-922；`AngelscriptDebuggerTestSession.cpp` 48-75 |
| 描述 | `CreateCloneFrom()` 明确让 clone 共享 `Source.SharedState`，但 `AdoptSharedStateFrom()` 只复制 `Engine`、`ConfigSettings`、包对象和 root paths，没有把 owner wrapper 上的 `DebugServer` 指针同步到 clone。结果是 clone wrapper 虽然和 source 指向同一个底层 `asIScriptEngine`，其 `DebugServer` 成员却始终为 `nullptr`。这会直接影响调试状态计算：`UpdateLineCallbackState()` 只看当前 wrapper 的 `DebugServer` 成员，而 `AngelscriptDebugServer.cpp` 在 `Continue/StepIn/StepOut/StartDebugging/StopDebugging` 等消息处理里又多次通过 `FAngelscriptEngine::Get().UpdateLineCallbackState()` 重新计算全局 line-callback 开关。只要当前 engine 解析碰巧落到 clone scope，上述重算就会把“正在调试”误判成“没有调试器”。测试工具也暴露出同样契约：`FAngelscriptDebuggerTestSession::Initialize()` 在拿到 `ExistingEngine` 后直接读取 `Engine->DebugServer`，如果传入的是 clone wrapper 会直接判定调试服务器不存在。 |
| 根因 | multi-engine 设计把底层 script engine 与共享状态做了分离，但调试能力仍挂在 wrapper-local 裸成员上；clone 继承过程没有为这些 owner 级运行时服务建立等价视图。 |
| 影响 | clone engine 参与的调试、单步、断点和任何依赖 `Engine->DebugServer` 的工具链都会出现假阴性：调试服务器其实仍在运行，但 clone 视角下会被当成不存在，并可能把 line callback 全局开关错误关闭。结果是 multi-engine/clone 场景下调试行为不稳定，工具和运行时对“当前 engine 是否可调试”的判断彼此矛盾。 |

---

## 分析 (2026-04-08 03:42)

### 发现 C-07：shared-state 访问器与 `EnsureSharedStateCreated()` 只在 `WITH_DEV_AUTOMATION_TESTS` 下定义，正式运行时代码却无条件依赖它们

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` |
| 行号 | `AngelscriptEngine.h` 564-569；`AngelscriptEngine.cpp` 912，945-1024，1491；`AngelscriptType.cpp` 35-45；`AngelscriptBinds.cpp` 23-33 |
| 描述 | 已验证事实：`GetTypeDatabase()`、`GetBindState()`、`GetToStringList()`、`GetBindDatabase()` 和 `EnsureSharedStateCreated()` 在 header 中是无条件声明的，但 `.cpp` 里的定义整体位于 `#if WITH_DEV_AUTOMATION_TESTS` 到 `#endif` 之间。与此同时，RuntimeCore 正式代码并没有同样的宏保护：`InitializeForTesting()` 和 `Initialize_AnyThread()` 都直接调用 `EnsureSharedStateCreated()`，`AngelscriptType.cpp` / `AngelscriptBinds.cpp` 也无条件通过这些访问器读取当前 engine 的 shared state。推断：当 `WITH_DEV_AUTOMATION_TESTS=0` 的 target 编译这组源文件时，这些调用点仍会生成符号引用，但实现会被预处理剔除，导致配置相关的链接失败或被迫退回并非预期的 fallback 路径。 |
| 根因 | RuntimeCore 把“shared-state 的正式运行时实现”错误地放进了测试宏边界内，导致 API 声明、调用方和实现的编译条件不一致。 |
| 影响 | 这不是单纯的测试代码杂糅，而是 build-configuration 契约断裂：只要目标配置关闭 dev automation tests，shared-state 初始化和类型/绑定数据库访问就可能在链接阶段直接失效，RuntimeCore 的不同构建配置无法保证行为一致。 |

---

## 分析 (2026-04-08 03:45)

### 发现 A-19：并行编译诊断 callback 既不绑定来源 engine，又用进程级静态去重状态跨 engine 共享

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `AngelscriptEngine.cpp` 910，1422，3212-3237，5012-5036；`AngelscriptEngine.h` 523 |
| 描述 | `InitializeForTesting()` 和 `Initialize_AnyThread()` 都把底层 `asCScriptEngine` 的 message callback 注册成 `SetMessageCallback(asFUNCTION(LogAngelscriptError), 0, asCALL_CDECL)`，没有把 owner engine 通过 `DataPtr` 传进去。`LogAngelscriptError()` 内部随后直接用 `FAngelscriptEngine::Get()` 解析“当前 engine”，并把诊断写入该 wrapper 的 `Diagnostics` / `CompilationLock`。同一个函数又把 `PreviousSection` / `PreviousType` 定义成进程级 `static` 局部变量，用来跨消息去重 section 头。与此同时，`CompileModules()` 在 3212-3237 行明确通过 `ParallelFor` 并行执行 `BuildParallelParseScripts()`，而 `CompilationLock` 只是每个 `FAngelscriptEngine` 实例自己的成员锁。已验证事实：这意味着 callback 的 owner 归属依赖全局 current-engine 解析，而不是触发消息的 script engine；`PreviousSection` / `PreviousType` 还会在不同 engine 使用不同实例锁时被并发读写。 |
| 根因 | RuntimeCore 把 AngelScript 的全局 message callback 当成“总能从 `FAngelscriptEngine::Get()` 反查到正确 wrapper”的接口来用，同时又把诊断去重状态放进函数级静态变量，导致 owner 绑定和并发同步都脱离了真正的脚本引擎实例。 |
| 影响 | multi-engine 或并行编译时，诊断可能被记到错误 engine 的 `Diagnostics` 映射里，section 头去重状态也会在不同 engine 之间互相污染；更严重的是，`FString PreviousSection` 的并发读写只受各自实例锁保护，锁域并不共享，属于实际的数据竞争。推断：如果某个 worker 线程进入 callback 时没有可解析的 current engine，`FAngelscriptEngine::Get()` 还会直接命中 `checkf`，把编译诊断路径升级成崩溃路径。 |

---

## 分析 (2026-04-08 03:47)

### 发现 B-12：按 `asCContext` 触发的调试/覆盖率 callback 没有绑定 context owner，而是重新走全局 current-engine 解析

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1704-1711，5300-5310，5463-5562 |
| 描述 | RuntimeCore 在 `CreateContext()` 中给每个 `asCContext` 安装了 `LogAngelscriptException`、`AngelscriptLineCallback` 和 `AngelscriptStackPopCallback`。这些 callback 触发时都已经拿到了来源 `Context`，但实现里没有根据 `Context->GetEngine()` 或其它 owner 映射回触发事件的 wrapper，而是统一调用 `FAngelscriptEngine::Get()`：异常路径用它取 `DebugServer`（5305-5309），line callback 用它取 `DebugServer` 和 `CodeCoverage`（5475，5534-5549），stack-pop callback 也用它取 `DebugServer`（5560-5562）。已验证事实：这些事件的路由目标取决于“此刻全局 current engine 是谁”，而不是“哪个 `asCContext` 触发了事件”。 |
| 根因 | RuntimeCore 把 context-local 的调试/覆盖率事件错误地建模成进程级 current-engine 事件，缺少 `asIScriptEngine* -> FAngelscriptEngine wrapper` 的稳定 owner 绑定层。 |
| 影响 | 在 multi-engine、clone scope 或任何 current-engine 解析与实际执行 context 不一致的场景下，异常、断点单步、stack-pop 和覆盖率命中都可能被投递到错误 wrapper，或者被错误地静默丢弃。由于这些 callback 正是调试与观测入口，错误路由会直接破坏 debugger 行为、coverage 统计和异常处理的一致性。 |

---

## 分析 (2026-04-08 03:48)

### 发现 D-08：现有自动化没有覆盖“callback 来源 engine 与 current-engine 不一致”这条多引擎边界

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 行号 | `AngelscriptDebuggerTestSession.cpp` 67-71；`AngelscriptTestEngineHelper.cpp` 101-107，156-157；`AngelscriptMultiEngineTests.cpp` 385-397；`AngelscriptEngineIsolationTests.cpp` 318-340 |
| 描述 | 已验证事实：现有 debugger session helper 在初始化后立刻建立 `FAngelscriptEngineScope(*Engine)`，然后直接读取 `Engine->DebugServer`；编译 helper 也基本都在单个 `FAngelscriptEngineScope(*Engine)` 内调用 `CompileModules()`。multi-engine 测试虽然会创建 source/clone 并切换 scope，但断言集中在 shared-state、生存期和 current-engine 解析本身，没有任何用例去故意制造“一个 engine/context 触发 callback，另一个 engine 处于 current-engine 位置”的场景。我对 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests` 和 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg -n "ProcessException|ProcessScriptLine|ProcessScriptStackPop|BuildParallelParseScripts|CompilationLock|PreviousSection|PreviousType"`，没有命中任何回归测试。 |
| 根因 | 测试体系目前只验证了多引擎的静态 ownership 与 scope 解析语义，却没有把 AngelScript callback 的 owner 路由视为独立合同来验证。 |
| 影响 | A-19 和 B-12 这类“编译/调试 callback 在多引擎下落到错误 wrapper”的缺陷不会被现有自动化拦截；测试可以证明 engine scope 切换有效，也可以证明 debugger 在单 engine 下工作，但不能证明并行编译、clone scope 或 callback 错绑场景是正确的。 |

---

## 分析 (2026-04-08 03:57)

### 发现 A-20：`bGeneratePrecompiledData` 和 `GScriptNativeForms` 形成跨 engine epoch 残留的进程级 StaticJIT 状态

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` |
| 行号 | `AngelscriptEngine.cpp` 76，872，1425，1132-1251；`StaticJITBinds.cpp` 27，112-116，156-160，199-203 |
| 描述 | RuntimeCore 把 `bGeneratePrecompiledData` 定义成进程级静态变量，并在 `InitializeForTesting()` / `Initialize_AnyThread()` 中直接用当前 engine 的 `RuntimeConfig` 覆盖它。`StaticJITBinds.cpp` 又把 `GScriptNativeForms` 定义成进程级静态 `TMap<asIScriptFunction*, FScriptFunctionNativeForm*>`，所有 `BindNative*()` 入口都在 `bGeneratePrecompiledData` 为真时往里面 `Add(new ...)`。但 `Shutdown()` 全段只清理 engine 实例字段、shared state、context pool 和包对象，看不到任何 `bGeneratePrecompiledData` 复位、`GScriptNativeForms.Empty()` 或释放这些 `new FScriptNative*` 对象的路径。 |
| 根因 | “是否生成 precompiled/static-jit 元数据” 本应属于单个 script-engine epoch 的配置，却被建模成进程级静态开关；而依赖该开关构建出来的 native-form 索引表同样挂成了进程级静态缓存，没有纳入 full engine teardown。 |
| 影响 | 已验证事实是：每次启用 `bGeneratePrecompiledData` 的 full engine 初始化都会向全局 `GScriptNativeForms` 追加新堆对象，而且这些条目在 engine 销毁后继续保留，无法回到干净基线。推断：由于键是已销毁 script engine 里的 `asIScriptFunction*`，后续 engine epoch 一旦复用到相同地址，就可能命中旧 native form，把 StaticJIT 调用翻译到错误签名；即使不发生地址复用，这张表和相关堆对象也会持续泄漏。 |

### 发现 A-21：`GAngelscriptEngineContextStack` 以无锁 `TArray` 被 game thread 和 worker thread 共享，current-engine 解析存在真实数据竞争

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 71，391-415，718-733，1680-1689，1811-1836，1889-1896 |
| 描述 | `GAngelscriptEngineContextStack` 在 71 行定义成文件静态 `TArray<FAngelscriptEngine*>`。`Push()` / `Pop()` / `Peek()` 直接对它做 `Add`、`Last`、`Pop`，没有任何锁或原子保护。与此同时，`TryGetCurrentEngine()` 把 `Peek()` 作为首选解析入口；这条解析不仅发生在 game-thread scope 内，后台 hot reload 线程在 1680 行直接调用 `FAngelscriptEngine::Get()`，`FAngelscriptPooledContextBase::Init()` / 析构在 1815、1831、1890 行也会通过 `TryGetCurrentEngine()` 解析当前 engine 来选择 script engine 和 global context pool。也就是说，同一个无锁 `TArray` 同时承担了 game-thread `FAngelscriptEngineScope` 的写路径和 worker-thread 资源路径的读路径。 |
| 根因 | RuntimeCore 把 `ContextStack` 既当作“单线程作用域栈”，又当作“跨线程 current-engine 注册表”复用，但没有把容器访问纳入任何并发协议。 |
| 影响 | 这不是单纯“可能解析错 engine”的逻辑问题，而是标准意义上的未定义行为：worker thread 读取 `Peek()/Last()` 时，game thread 可能正好在 `Add/Pop` 修改同一块 `TArray` 存储。结果既可能表现为读取到错误栈顶，也可能在扩容/缩容交错时直接触发容器损坏、崩溃，或者把 context 错误归还到另一台 engine 的 global pool，进一步污染后续执行。 |

### 发现 D-09：现有自动化没有覆盖 `ContextStack` 的 worker-thread 解析边界，无法拦截并发 current-engine 回归

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 行号 | `AngelscriptEngineIsolationTests.cpp` 158-191；`AngelscriptSubsystemTests.cpp` 76 |
| 描述 | 已验证事实：现有 `ContextStack` 回归测试只覆盖单线程 push/pop 语义。`FAngelscriptContextStackScopedResolutionTest` 仅在同一个测试线程里创建嵌套 `FAngelscriptEngineScope` 并断言 `Peek()` 恢复；`AngelscriptSubsystemTests.cpp` 对 hot-reload 相关状态的唯一接触只是手工把 `Engine.bWaitingForHotReloadResults = false` 设成探针初值。我对 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests` 和 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg -n "HotReloadThread|FRunnable|CurrentEngine.*GlobalContextPool|bWaitingForHotReloadResults"`，唯一命中就是这条手工赋值，看不到任何测试真正启动 worker thread、并发读 `TryGetCurrentEngine()`，或验证 `ContextStack` 在异步解析下的行为。 |
| 根因 | 测试体系把 `ContextStack` 当作纯粹的 RAII 栈语义来验证，没有把它已经承担的 worker-thread current-engine 解析职责视为独立合同。 |
| 影响 | A-21 这类“容器本身发生并发读写”的缺陷不会被当前自动化发现。测试可以证明单线程嵌套 scope 恢复正常，但不能证明 hot reload、异步 context 池或其他 worker-thread 路径在读取 `TryGetCurrentEngine()` 时是安全的。 |

---

## 分析 (2026-04-08 04:12)

### 发现 B-13：`UAngelscriptGameInstanceSubsystem` 会把临时 `current-engine` wrapper 固化成长期裸指针，绕过 `FAngelscriptEngine` 的生命周期令牌

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `AngelscriptGameInstanceSubsystem.cpp` 17-23，34-45，81-85；`AngelscriptEngine.cpp` 437-457，628-647，718-727；`AngelscriptEngine.h` 208-210，449-452；`AngelscriptRuntimeModule.cpp` 35-38 |
| 描述 | `UAngelscriptGameInstanceSubsystem::Initialize()` 在 17-23 行直接把 `FAngelscriptEngine::TryGetCurrentEngine()` 的返回值保存到成员 `PrimaryEngine`。而 `TryGetCurrentEngine()` 在 718-727 行优先取 `FAngelscriptEngineContextStack::Peek()`，这个“current engine”本身由 `FAngelscriptEngineScope` 在 437-457 行用 RAII 临时压栈。RuntimeCore 还明确支持 clone wrapper，并为它们单独设计了 `SourceLifetimeToken` / `GetSourceEngine()` 这套生命周期保护（628-647，208-210，449-452）。但 subsystem 没有复用任何这类保护：它把 `PrimaryEngine` 作为裸 `FAngelscriptEngine*` 长期保存，并在后续 `Tick()` / `Deinitialize()` 中继续直接解引用（81-85，34-45）。与此同时，RuntimeModule 的 owner engine 在 `ShutdownModule()` 里会直接 `OwnedPrimaryEngine.Reset()`（35-38），外部 owner 消失时 subsystem 也收不到任何失效通知。 |
| 根因 | subsystem 集成边界把“当前 engine 解析”误当成稳定依赖注入来源，但 `TryGetCurrentEngine()` 实际表达的是瞬时调用作用域；RAII scope 和 clone wrapper 都有自己的生命周期语义，subsystem 却只保留了一个无法校验存活性的裸指针。 |
| 影响 | 只要 subsystem 初始化发生在临时 `FAngelscriptEngineScope` 内，或者命中了随后会被释放的外部 owner engine，它就可能把短生命周期 wrapper 提升成 `UGameInstance` 级长期状态。推断：一旦该 wrapper 后续析构，subsystem 的 `Tick()` / `Deinitialize()` 就会对悬空 `PrimaryEngine` 解引用，形成 use-after-free，或者把本应随作用域消失的 clone/module engine 错当成长期主引擎继续推进。 |

---

## 分析 (2026-04-08 04:13)

### 发现 D-10：现有自动化没有组合验证“真实 subsystem 初始化 + scoped/clone engine 生命周期”，B-13 会稳定漏检

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | `AngelscriptSubsystemTests.cpp` 80-87，113-137，140-160，163-333；`AngelscriptMultiEngineTests.cpp` 253-279，403-422 |
| 描述 | subsystem 测试文件虽然定义了会走真实 `GameInstance->GetSubsystem<UAngelscriptGameInstanceSubsystem>()` 的 `CreateSubsystemWorld()` helper（113-137），但该 helper 在文件内没有任何调用点；实际所有 subsystem 行为测试都走 `CreateInjectedSubsystem()`，再由 `SetSubsystemPrimaryEngine()` 直接把外部 `FAngelscriptEngine*` 塞进 `Subsystem.PrimaryEngine`（80-87，140-160，163-333）。与之相对，multi-engine 测试已经证明了两条关键前提：`CreateForTesting()` 会在 scoped source engine 下返回 clone（253-279），而 source owner 释放后 clone 的 `GetSourceEngine()` 会失效（403-422）。当前测试套件没有任何用例把这两类前提组合起来，也没有任何测试让真实 subsystem 在 `FAngelscriptEngineScope`/clone 环境中初始化后再观察 owner 销毁。 |
| 根因 | 测试划分把 subsystem 行为和 multi-engine 生命周期分成了两套互不组合的用例：前者通过 friend 注入绕开真实初始化，后者只验证 clone 自身合同，没有覆盖 subsystem 持有外部 engine 指针后的长期行为。 |
| 影响 | B-13 这类“subsystem 在初始化时捕获了短生命周期 engine wrapper”缺陷不会被现有自动化拦截。当前测试最多能证明 clone 生命周期自己成立，也能证明手工注入后的 subsystem 能 tick，但不能证明真实 subsystem 初始化面对 scoped/clone engine 时不会留下悬空 `PrimaryEngine`。 |

---

## 分析 (2026-04-08 10:24)

### 发现 B-14：`ActiveTickOwners` 是进程级总开关，任意 subsystem 都会压制其他 engine 的 fallback tick

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `AngelscriptGameInstanceSubsystem.cpp` 8，17-28，36，116-118；`AngelscriptRuntimeModule.cpp` 186-194 |
| 描述 | `UAngelscriptGameInstanceSubsystem` 用静态 `int32 ActiveTickOwners` 记录“是否存在任何 subsystem tick owner”。每个 subsystem 只要拿到 `PrimaryEngine`，就在 `Initialize()` 中执行 `++ActiveTickOwners`，在 `Deinitialize()` 中再做全局减一；这个计数既不区分 `UGameInstance`，也不区分实际绑定的 `FAngelscriptEngine`。与此同时，`FAngelscriptRuntimeModule::TickFallbackPrimaryEngine()` 只看 `!UAngelscriptGameInstanceSubsystem::HasAnyTickOwner()` 这一全局条件，就决定是否推进当前解析到的 engine。结果是任意一个 world/subsystem 进入运行态后，runtime module 对其他 engine 的 fallback tick 会被整体熄火。 |
| 根因 | RuntimeCore 把“哪个 engine 由哪个 owner 负责 tick”压缩成了一个进程级布尔语义：subsystem 侧只维护全局 owner 数量，runtime module 侧只查询“是否有人在 tick”，而不是“当前 engine 是否已经有自己的 tick owner”。 |
| 影响 | 多 world / 多 engine 并存时，tick 仲裁会串台：例如某个 `UGameInstance` 的 subsystem 挂起后，编辑器或 runtime module 持有的另一台 primary engine 将不再收到 fallback tick，导致热重载轮询、调试 server tick 和任何依赖 `FAngelscriptEngine::Tick()` 的生命周期推进停摆。这个问题不会表现为重复 tick，而是更隐蔽的“无关 engine 被错误静默”。 |

---

## 分析 (2026-04-08 10:26)

### 发现 B-15：`ShouldTick()` 只检查底层 `asIScriptEngine` 是否存在，clone wrapper 会被误判为可推进的 primary engine

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 行号 | `AngelscriptEngine.cpp` 628-647，1377-1462，1612-1619，2794-2848，2848-2856；`AngelscriptGameInstanceSubsystem.cpp` 17-28，81-85；`AngelscriptSubsystemTests.cpp` 72-76，167-174，243-272 |
| 描述 | clone 创建路径会把 `bOwnsEngine` 设为 `false`，并通过 `AdoptSharedStateFrom()` 只复制 `Engine`、包对象、root path 和少量编译状态；它不会继承 source wrapper 上的 `bScriptDevelopmentMode`、`bUseHotReloadCheckerThread`、`HotReloadTestRunner`、`DebugServer`、`CodeCoverage` 或 `WorldContextObject`。但 `ShouldTick()` 的判断却只有 `return Engine != nullptr;`，因此任何 clone 只要共享到底层 `asIScriptEngine`，就会被 `UAngelscriptGameInstanceSubsystem::Tick()` 当成“可推进的主引擎”直接调用 `Tick()`。现有自动化甚至显式暴露了这个缺口：`SubsystemTicksPrimaryEngine` 不是验证 clone 原生可 tick，而是先在 `PrepareTickProbe()` 里手工把 clone 的 `bScriptDevelopmentMode` 和 `bUseHotReloadCheckerThread` 改成可运行状态，再调用 `Subsystem->Tick()`。 |
| 根因 | RuntimeCore 把“wrapper 是否连接到底层 script engine”和“wrapper 是否拥有完整 tick 生命周期能力”混成了同一个条件；clone 设计只共享 shared state，却没有为 tick 期服务建立 owner/participant 的能力边界。 |
| 影响 | 一旦 subsystem 或其他集成边界把 clone wrapper 采纳为 `PrimaryEngine`，系统就会把一个不具备完整 runtime 服务的轻量包装层当成真正的主引擎推进。结果不是稳定崩溃，而是更难发现的功能静默缺失：hot reload 轮询、debug server、coverage hook 和依赖当前 wrapper 状态的 tick 逻辑都可能不再运行，而调用方仍会误以为“引擎正在正常 tick”。 |

---

## 分析 (2026-04-08 10:27)

### 发现 B-16：clone wrapper 没有投影 source 的上下文状态，`current-context` API 在 clone scope 下会系统性读错

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp` |
| 行号 | `AngelscriptEngine.cpp` 628-647，682-714，1377-1428，2848-2856；`AngelscriptPreprocessor.cpp` 40-41，232，485；`Bind_AActor.cpp` 169，201，234，352，402，408，446；`Bind_Subsystems.cpp` 46，73，89；`UnitTest.cpp` 66，134 |
| 描述 | `CreateCloneFrom()` 生成 clone 后，`AdoptSharedStateFrom()` 只把底层 `Engine`、包对象、root path 和少量编译状态复制到新 wrapper，完全没有同步 `WorldContextObject`、`bUseEditorScripts`、`bUseAutomaticImportMethod` 或 `bScriptDevelopmentMode`。但 RuntimeCore 的一组全局查询 API 恰恰直接读取“当前 wrapper”的这些字段：`TryGetCurrentWorldContextObject()`、`ShouldUseEditorScriptsForCurrentContext()`、`ShouldUseAutomaticImportMethodForCurrentContext()`、`IsScriptDevelopmentModeForCurrentContext()` 都只看 `TryGetCurrentEngine()` 返回的 wrapper。结果是只要 current engine 解析落到 clone，这些 API 就会从 clone 的默认字段里读出 `nullptr/false`，而不是 source engine 的真实运行时状态。下游消费者很多，而且都直接依赖这些查询结果，包括 preprocessor 的 `EDITOR` / automatic import 判定、`Bind_AActor` / `Bind_Subsystems` 的 world 解析，以及 unit test 运行期的 world 获取。 |
| 根因 | multi-engine 设计只共享了底层 script engine 和少量结构化资源，却没有定义“wrapper-local 运行时上下文”在 clone 上应当如何镜像 source；而 current-context API 又把 wrapper 字段直接暴露成全局事实。 |
| 影响 | clone scope 不只是调试能力失真，还会把整个运行时上下文读错：预处理条件可能退化成非 `EDITOR`，automatic imports 可能被错误关闭，依赖 `TryGetCurrentWorldContextObject()` 的 actor/subsystem/unit-test 路径会拿到空 world context。推断：这会让 clone 相关问题表现成散落在绑定、预处理和测试执行中的随机功能缺失，而根源都在 RuntimeCore 对 clone 上下文投影的不完整。 |

---

## 分析 (2026-04-08 10:28)

### 发现 D-11：现有自动化没有验证多 engine tick 仲裁和 clone 上下文投影，B-14/B-15/B-16 均处于无回归保护状态

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 行号 | `AngelscriptSubsystemTests.cpp` 61-85，113-160，167-174，243-272，307-327，412；`AngelscriptMultiEngineTests.cpp` 214-222，310-311，343，375-397，410-421，658，765-766；`AngelscriptEngineIsolationTests.cpp` 61，166-201，333 |
| 描述 | subsystem 测试没有任何一例创建两个同时存活的 subsystem/engine 去验证 `HasAnyTickOwner()` 对不同 engine 的影响；相反，它们统一走 `CreateInjectedSubsystem()` 直接注入裸 `FAngelscriptEngine*`，并在 helper 里手工把 `Subsystem.ActiveTickOwners = 1`。更关键的是，`SubsystemTicksPrimaryEngine` 和 fallback tick 相关用例在真正断言前都先调用 `PrepareTickProbe()`，主动把 clone 的 `bScriptDevelopmentMode` / `bUseHotReloadCheckerThread` 改成可运行状态。与此同时，multi-engine 与 isolation 测试虽然大量创建 clone、切换 scope、验证 source 链接和模块隔离，却没有任何断言触及 `TryGetCurrentWorldContextObject()`、`ShouldUseEditorScriptsForCurrentContext()`、`ShouldUseAutomaticImportMethodForCurrentContext()` 或 clone 的原生 `ShouldTick()`/`Tick()` 契约。 |
| 根因 | 测试套件把 subsystem tick、multi-engine 生命周期和 current-context 读取拆成了互不相交的场景，并且在 subsystem 用例里用 friend helper 直接补写 clone 私有状态，绕开了 RuntimeCore 真正的集成边界。 |
| 影响 | B-14 的跨 engine tick 串台、B-15 的 clone 假 tick、B-16 的 clone 上下文投影错误都不会被当前自动化拦截。现有测试可以证明“手工修补后的 clone 能被 tick”“clone 生命周期不会立刻崩”，但不能证明生产代码在真实多 engine / 多 world / clone scope 组合下的行为正确。 |

---

## 分析 (2026-04-08 10:37)

### 发现 A-22：`Tick()` 已知 `GEngine == nullptr`，却仍无条件进入 `HasGameWorld()` 解引用空 `GEngine`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 2781-2792，2797-2829 |
| 描述 | `FAngelscriptEngine::Tick()` 在 2799-2801 行明确注释“initial compile fails 后用户点 Try Again 时 `GEngine` 会是 `nullptr`”，并且只在执行 `HotReloadTestRunner` 时做了 `if (GEngine != nullptr)` 防护。但同一 tick 的后半段在 2822 行仍然无条件调用 `HasGameWorld()`；而 `HasGameWorld()` 本身在 2783 行直接遍历 `GEngine->GetWorldContexts()`，没有任何空指针检查。也就是说，这条代码已经承认存在 `GEngine == nullptr` 的运行场景，却在同一个控制流里继续对 `GEngine` 做确定性解引用。 |
| 根因 | `Tick()` 只把 `GEngine == nullptr` 当成“跳过 unit test runner”的局部特例处理，没有把同一前置条件统一提升为整条 hot reload 分支的保护条件；`HasGameWorld()` 也被实现成默认依赖全局 `GEngine` 存在的 helper。 |
| 影响 | 当初始编译失败后的恢复流程再次驱动 runtime tick 时，这条已知异常路径不会优雅降级到“仅跳过测试”，而是会在 hot reload 模式判定阶段直接崩溃。结果是用户点击 Try Again 试图恢复脚本环境，RuntimeCore 却在恢复路径里先因为空 `GEngine` 访问失败，导致生命周期恢复入口本身不可用。 |

---

## 分析 (2026-04-08 10:45)

### 发现 A-23：subsystem teardown 把 `Pop()` 失败当成非阻断 `ensure`，会把已 shutdown 的 engine 残留在 `ContextStack`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptGameInstanceSubsystem.cpp` 39-49；`AngelscriptEngine.cpp` 399-410，718-733，1229-1251 |
| 描述 | `UAngelscriptGameInstanceSubsystem::Deinitialize()` 在自有引擎路径里先调用 `FAngelscriptEngineContextStack::Pop(PrimaryEngine)`，随后无论 `Pop()` 是否真的成功，都会继续执行 `PrimaryEngine->Shutdown()`。但 `FAngelscriptEngineContextStack::Pop()` 在栈顶不匹配时只做 `ensureAlwaysMsgf(...)`，然后直接返回，不会移除任何栈项。与此同时，`Shutdown()` 会把该 wrapper 的 `Engine`、`StaticJIT`、`PrecompiledData`、`WorldContextObject` 等成员全部清空，却不会触碰 `GAngelscriptEngineContextStack`。因此只要 subsystem 在栈顶不是自己时进入 `Deinitialize()`，`ContextStack` 就会留下一个已经 shutdown 的 `FAngelscriptEngine*`。 |
| 根因 | subsystem 生命周期实现假定 `Pop(PrimaryEngine)` 必然成功，把 `ContextStack` 一致性错误降级成日志级 `ensure`；但后续 teardown 没有根据 `Pop()` 是否成功调整流程，也没有做二次清理或失效保护。 |
| 影响 | 这是一个比“pop 顺序错”更严重的状态腐坏：后续 `TryGetCurrentEngine()` 仍优先返回 `Peek()`，因此一旦更外层 scope 退栈，系统就可能重新解析到这台已 shutdown 的 wrapper。已验证事实是，这个 wrapper 此时 `Engine == nullptr`，会让 `GetPackage()`、`ShouldTick()`、context 池归还和 world-context 查询读取到失效状态；推断：如果 subsystem 对象随后被销毁，`ContextStack` 里还会进一步退化成悬空裸指针，演变为 use-after-free。 |

---

## 分析 (2026-04-08 10:47)

### 发现 D-12：自动化没有覆盖 `GEngine == nullptr` 的 tick 恢复路径，也没有覆盖 subsystem 在栈顶不匹配时的 deinitialize

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` |
| 行号 | `AngelscriptSubsystemTests.cpp` 239-273，276-300，303-332，388-416；`AngelscriptEngineIsolationTests.cpp` 174-191；`AngelscriptHotReloadFunctionTests.cpp` 66-68 |
| 描述 | 已验证事实：现有 runtime-core 测试只覆盖“正常前置条件下的 tick/deinitialize”。`SubsystemTicksPrimaryEngine` 与 `RuntimeFallbackTicksGlobalEngineWithoutSubsystemOwner` 只在有效 `PrimaryEngine` 上调用 `Tick()`；`SubsystemDeinitializeDestroysPrimaryEngine` 也只是直接 `SetSubsystemPrimaryEngine()` 后立刻 `Deinitialize()`，没有在 subsystem 自有 engine 上叠加任何额外 scope 或栈顶错位。另一方面，hot-reload 测试只通过 helper 直接调用 `Engine.CheckForHotReload(...)`，没有任何一条测试真正进入 `FAngelscriptEngine::Tick()` 并构造 `GEngine == nullptr` 的恢复场景。我对 `Tests`/`AngelscriptTest` 执行 `rg -n "HasGameWorld|GEngine == nullptr|pop order mismatch|ContextStack.*Deinitialize|Deinitialize\\(\\).*scope"`，没有命中针对这些边界的回归用例。 |
| 根因 | 测试套件把 RuntimeCore 的生命周期验证集中在“绿路”行为：engine 能创建、能 tick、scope 能恢复、subsystem 能清理；但没有把 hot-reload 恢复失败态和 subsystem teardown 期间的 `ContextStack` 一致性当成独立合同来建模。 |
| 影响 | A-22 和 A-23 都处于无回归保护状态。现有测试可以证明常规调用路径可工作，却不能证明 “Try Again 后 `GEngine` 缺失” 与 “deinitialize 时 stack top 已被别的 scope 占用” 这两条更接近真实故障恢复/关停边界的路径是安全的。 |

---

## 分析 (2026-04-08 11:03)

### 发现 A-24：`Shutdown()` 无锁清空 `GlobalContextPool`，与正常归还路径的加锁访问形成真实数据竞争

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptEngine.h` 396-398；`AngelscriptEngine.cpp` 1190-1197，1831-1843，1889-1896 |
| 描述 | `FAngelscriptEngine` 明确为 `GlobalContextPool` 配了专用 `FCriticalSection GlobalContextPoolLock`。正常运行时，无论是从全局池取 context，还是在 `FAngelscriptPooledContextBase::~FAngelscriptPooledContextBase()` 里把 context 归还到全局池，代码都会先 `FScopeLock` 再读写 `GlobalContextPool`。但 `FAngelscriptEngine::Shutdown()` 在 1190-1197 行直接无锁遍历 `GlobalContextPool`、逐个 `Release()` 然后 `Empty()`，完全绕过了这把锁。 |
| 根因 | 生命周期收口没有沿用运行时已经建立的并发协议：平时把 `GlobalContextPool` 当成受锁保护的共享容器，teardown 阶段却把它当成单线程私有数组直接销毁。 |
| 影响 | 只要有任何线程在 engine teardown 期间归还 pooled context，这个 `TArray` 就会在 `Push` 与 `for/Empty` 之间并发读写，导致 context 丢失、重复 `Release()`、容器损坏甚至崩溃。由于 RuntimeCore 允许 worker thread 取还 context，这条竞态会直接破坏 engine shutdown 的稳定性。 |

### 发现 B-17：subsystem/runtime ticker 直接调用 `Engine->Tick()`，却没有建立 `FAngelscriptEngineScope` 来固定 tick 期间的 current-engine

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptGameInstanceSubsystem.cpp` 17-23，81-85；`AngelscriptRuntimeModule.cpp` 186-194；`AngelscriptEngine.cpp` 821，910，1422，2794-2828，5012-5019 |
| 描述 | RuntimeCore 自己已经把 `FAngelscriptEngineScope` 当成生命周期操作的标准入口，`FAngelscriptEngine::Initialize()` 一开始就在 821 行建立 `ScopedInitializingEngine(*this)`。但两个生产 tick 边界都没有这么做：`UAngelscriptGameInstanceSubsystem::Tick()` 和 `FAngelscriptRuntimeModule::TickFallbackPrimaryEngine()` 只是直接执行 `PrimaryEngine->Tick()` / `CurrentEngine->Tick()`。这在 subsystem 采用外部 engine 的分支尤其危险，因为 17-23 行只是把 `TryGetCurrentEngine()` 的结果保存到 `PrimaryEngine`，并不会像自建引擎那样长期 `Push` 到 `ContextStack`。与此同时，`FAngelscriptEngine::Tick()` 会进入 `CheckForHotReload()`，而编译消息 callback 又是在 910/1422 行以 `SetMessageCallback(..., 0, ...)` 注册、在 5012-5019 行通过 `FAngelscriptEngine::Get()` 反查“当前 engine”写诊断。也就是说，tick 路径本身依赖 current-engine 已经被外层正确建立。 |
| 根因 | subsystem/runtime ticker 把 `Tick()` 当成纯成员函数调用处理，但 `Tick()` 触发的 hot-reload / compile / diagnostics 逻辑并不是 wrapper-local 的，它们仍通过全局 current-engine 解析回到 `FAngelscriptEngine::Get()`。 |
| 影响 | 只要 engine 不是靠长期 `ContextStack` 残留“碰巧”保持 current，例如 subsystem 采用了一个外部 full/clone wrapper，tick 期间触发的编译诊断、热重载副作用和任何依赖 `Get()` 的运行时逻辑就会落到错误 engine，或者在没有 current-engine 时直接命中 check。这个边界错误把 subsystem 集成正确性建立在隐式全局状态上，而不是建立在 `Tick()` 调用本身。 |

### 发现 D-13：现有 subsystem/runtime-module 自动化用手工 `GlobalScope` 和 override engine 运行测试，生产 `Tick()` / `Initialize()` 边界基本未被覆盖

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `AngelscriptSubsystemTests.cpp` 180，223，250，315，339-343，351，366-370，392-397，405；`AngelscriptRuntimeModule.cpp` 145-149，158-164 |
| 描述 | subsystem 相关用例在真正调用 `Subsystem->Tick()` 或验证 `Get()` 之前，都会先手工建立 `FAngelscriptEngineScope GlobalScope(*Subsystem->GetEngine())`。与此同时，三个 RuntimeModule 用例又统一通过 `SetInitializeOverride([] { return FAngelscriptEngine::CreateTestingFullEngine(...).Release(); })` 把 `InitializeAngelscript()` 导向 145-149 行的测试 override 分支，导致生产分支 158-164 行的 `CurrentEngine->Initialize()` / `OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>(); OwnedPrimaryEngine->Initialize();` 根本没有被这些测试执行。 |
| 根因 | 现有自动化为了快速得到一个“可用 engine”，选择在测试夹具里先补 current-engine scope，再用 override 注入一个 testing engine；这样可以绕开真实 startup/tick 边界的复杂依赖，但也把最关键的集成合同一起绕开了。 |
| 影响 | B-17 这类“生产 ticker 没有建立 `FAngelscriptEngineScope`”的问题不会在现有自动化里出现，因为测试已经提前把 scope 建好了；同理，RuntimeModule 的真实初始化分支和它触发的重入/所有权问题也不会被这些测试发现。当前绿色结果只能证明测试夹具下的修正版路径可工作，不能证明生产生命周期路径正确。 |

---

## 分析 (2026-04-08 11:15)

### 发现 A-25：`CodeCoverage` 生命周期没有纳入 `Shutdown()`，自动化回调会在进程内持续累积

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `AngelscriptEngine.cpp` 1132-1251，1460-1462，1628-1632；`AngelscriptCodeCoverage.cpp` 22-28；`AngelscriptEngine.h` 604-606 |
| 描述 | RuntimeCore 在初始化时通过 `CodeCoverage = new FAngelscriptCodeCoverage` 创建 coverage recorder，并在 `OnPostEngineInit` 里调用 `CodeCoverage->AddTestFrameworkHooks()`。`AddTestFrameworkHooks()` 又把 `this` 以 `AddRaw` 方式注册到 `AutomationController->OnTestsAvailable()` 和 `OnTestsComplete()`。但 `FAngelscriptEngine::Shutdown()` 整段清理逻辑完全没有 `delete CodeCoverage`、没有把成员清空，也没有任何 `Remove`/`RemoveAll` 去解绑这两个 automation delegate。对整个源码树执行 `rg -n "delete CodeCoverage|CodeCoverage = nullptr|OnTestsAvailable\\(\\).*Remove|OnTestsComplete\\(\\).*Remove"` 也只命中注册点，没有命中释放点。 |
| 根因 | Coverage recorder 被当成“初始化后常驻的附属对象”创建，却没有进入 `FAngelscriptEngine` 的 owner 资源模型；其自动化钩子注册在 `CodeCoverage` 自己内部完成，而 engine 侧既不保存 delegate handle，也没有 teardown 分支回收这个对象。 |
| 影响 | 每次创建并销毁 full engine，都会泄漏一个 `FAngelscriptCodeCoverage` 实例并把新的 automation 回调永久留在进程里。后续测试运行会同时触发历史 recorder 的 `OnTestsStarting()` / `OnTestsStopping()`，导致 coverage 统计和报告写出重复、陈旧或跨 engine epoch 混杂，长期运行的编辑器会不断累积这类悬挂 recorder。 |

---

## 分析 (2026-04-08 11:15)

### 发现 A-26：shared-state teardown 只清当前线程的 `thread_local ContextPool`，其他线程缓存的 context 会跨 engine epoch 残留

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `AngelscriptEngine.cpp` 87，364-368，1722-1745，1801-1903，1905-1913；`AngelscriptEngine.h` 654-661 |
| 描述 | RuntimeCore 把空闲 script context 分成两层池：每个线程各自持有一个 `thread_local FAngelscriptContextPool GAngelscriptContextPool`，而 `AngelscriptRequestContext()` / `AngelscriptReturnContext()` 总是优先从当前线程的 `LocalPool.FreeContexts` 取还 context。真正释放 shared state 时，`ReleaseOwnedSharedStateResources()` 在 364-368 行只对“当前线程的” `GAngelscriptContextPool.FreeContexts` 调用 `ReleaseContextsForScriptEngine(...)`，随后立刻执行 `SharedState->ScriptEngine->ShutDownAndRelease()`。源码里没有任何全局注册表能枚举其他线程的 `thread_local` 池；`FAngelscriptContextPool::~FAngelscriptContextPool()` 还明确注明它只能等线程退出时再遍历 `FreeContexts`。 |
| 根因 | 生命周期设计只把 context pool 的 engine 归属建模到了“当前线程 + engine 级全局池”，却遗漏了其余长期存活线程的 `thread_local` 缓存；teardown 时没有机制广播或收口这些线程本地池。 |
| 影响 | full engine/shared state 即使完成 `Shutdown()`，也不能保证所有 context 真正随 script engine 一起释放。只要 worker thread 曾经跑过脚本并把 context 归还到本线程本地池，这些 context 就会一直滞留到线程退出，形成跨 engine epoch 的内存泄漏；推断：如果后续 script engine 恰好复用相同地址，`TryTakeContextFromPool()` 还可能把旧 epoch 的 context 误判成可复用对象，进一步污染新的运行时周期。 |

---

## 分析 (2026-04-08 11:16)

### 发现 D-14：现有 `CodeCoverage` / `ContextPool` 测试都没有覆盖真实 teardown 边界，A-25 与 A-26 没有回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 行号 | `AngelscriptCodeCoverageTests.cpp` 17-82；`AngelscriptMultiEngineTests.cpp` 425-475；`AngelscriptEngineIsolationTests.cpp` 565-585，595-640 |
| 描述 | `AngelscriptCodeCoverageTests.cpp` 的 integration test 只是在当前运行中的 production engine 上栈内创建一个 `FAngelscriptCodeCoverage Coverage`，然后手工调用 `MapExecutableLines()`、`StartRecording()`、`HitLine()`、`StopRecordingAndWriteReport()`；整个用例完全不经过 `FAngelscriptEngine::Initialize_AnyThread()` 里的 `CodeCoverage = new ...` 和 `AddTestFrameworkHooks()`，也不验证 engine teardown 后 automation delegate 是否被解绑。另一方面，`DeferredSharedStateReleasePurgesLocalContextPool` 与 `FullEngineCreateClearsThreadLocalPool` 虽然覆盖了 local pool purge，但它们都只在当前测试线程里 `SeedContext`、读取 `GetLocalPooledContextCount(...)`，没有任何 `AsyncTask` / worker thread 参与，也没有构造“别的线程先缓存 context，再由 game thread shutdown shared state”的场景。 |
| 根因 | 现有自动化更偏向 helper 级和单线程行为验证：coverage 用例测试的是 recorder 算法本身，context-pool 用例测试的是当前线程可见的池状态；两者都没有把 RuntimeCore 真正的生命周期收口路径建模成测试合同。 |
| 影响 | A-25 的 leaked coverage recorder / automation hook accumulation 与 A-26 的跨线程 `thread_local` pool 残留都不会被现有测试拦截。当前测试通过只能说明 “单线程下 helper 行为正确”，不能证明 production 初始化和 teardown 已经把 runtime 全局状态收干净。 |

---

## 分析 (2026-04-08 11:17)

### 发现 A-27：RuntimeModule 自有 engine 没有 GC bridge，`FAngelscriptEngine` 内部的 `UObject*` 跟踪只在 subsystem owner 路径完整

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptGameInstanceSubsystem.h` 38-40；`AngelscriptRuntimeModule.h` 58-61；`AngelscriptRuntimeModule.cpp` 138-165；`AngelscriptEngine.h` 289-293，455-458，557-558；`AngelscriptEngine.cpp` 682-690，746-753 |
| 描述 | 已验证事实：subsystem 自有路径把 `OwnedEngine` 声明成 `UPROPERTY() FAngelscriptEngine`，而 RuntimeModule 的全局主引擎却是 `static TUniquePtr<FAngelscriptEngine> OwnedPrimaryEngine`。`FAngelscriptEngine` 自身确实给 `AngelscriptPackage`、`AssetsPackage`、`WorldContextObject`、`ConfigSettings` 打了 `UPROPERTY()`；`AssignWorldContext()` 还会持续把 `NewWorldContext` 写进当前 engine 的 `WorldContextObject`，`TryGetCurrentWorldContextObject()` 再原样返回它。与此同时，我对 `AngelscriptRuntimeModule.*` 执行 `rg -n "FGCObject|AddReferencedObjects"` 没有命中任何 GC bridge。 |
| 根因 | 推断：RuntimeCore 只在 subsystem owner 路径上把 `FAngelscriptEngine` 放进了 UE 反射/GC 可见的宿主对象里；module singleton owner 路径直接用 `TUniquePtr` 保存 engine，没有额外的 `FGCObject` 或 `AddReferencedObjects` 把这些 `UObject*` 暴露给 GC。 |
| 影响 | module-owned full engine 的 `WorldContextObject` / `ConfigSettings` 等对象引用在 GC 看来是“不可见的私有裸指针”。一旦运行时主引擎长期驻留在 RuntimeModule 而不是 subsystem，中间经过 GC 或 world teardown 后，`TryGetCurrentWorldContextObject()` 继续返回的就可能是已经失效的对象地址；这会把 world 解析、日志打印和依赖 current world context 的绑定调用建立在未被 GC 保护的状态上。 |

---

## 分析 (2026-04-08 11:28)

### 发现 B-18：`UAngelscriptGameInstanceSubsystem` 从未把 `UGameInstance/UWorld` 绑定进 `PrimaryEngine`，导致 subsystem owner 稳态下缺失 world context

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp` |
| 行号 | `AngelscriptGameInstanceSubsystem.cpp` 12-30，81-85；`AngelscriptEngine.cpp` 682-689，718-733，746-753，1763-1783；`Bind_Subsystems.cpp` 67-94；`UnitTest.cpp` 60-70，128-139 |
| 描述 | `UAngelscriptGameInstanceSubsystem::Initialize()` 在两条路径里都只处理 “拿哪台 engine” 和 “谁来 tick” ：要么采用 `TryGetCurrentEngine()`，要么 `Push(&OwnedEngine)` 后执行 `OwnedEngine.Initialize()`。这段代码既没有把 `GetGameInstance()` 或 `GetWorld()` 传给 `FAngelscriptEngineScope`，也没有调用 `AssignWorldContext()`。与此同时，`TryGetCurrentWorldContextObject()` 的实现是“只要存在 current engine，就直接返回该 engine 的 `WorldContextObject`”，只有没有 current engine 时才退回 `GAmbientWorldContext`。由于 subsystem owner 路径会把 `OwnedEngine` 长期留在 `ContextStack`，所以这条 fallback 在稳态下根本不会生效；而真正会写 `WorldContextObject` 的只有按次进入的 `FAngelscriptContext` / `FAngelscriptGameThreadContext`。结果是 subsystem 明明已经拿到了 `UGameInstance` 边界，却没有把它沉淀成 engine 的基础运行上下文。 |
| 根因 | subsystem 集成只建模了 owner/tick 边界，没有建模 world-context 边界：`PrimaryEngine` 的长期所有权和 `WorldContextObject` 的生命周期被拆成了两套互不联动的状态，后者只能依赖临时调用栈上的 RAII helper 偶然写入。 |
| 影响 | 这会把一批 world-sensitive API 变成“只有先经过某次显式 world scope 才偶然可用”。已验证事实是，`Bind_Subsystems.cpp` 的 `UGameInstanceSubsystem::Get()` / `UWorldSubsystem::Get()` 直接用 `TryGetCurrentWorldContextObject()` 取 world，`UnitTest.cpp` 的 `AdvanceTime` / `SetIsServer` 也在取不到 world 时直接 `Fail("...has no world")`。因此 subsystem 自有 engine 即使已经初始化完成，只要当前帧之前没有别的路径临时写过 world context，这些基础运行时入口就会稳定返回 `nullptr` 或直接失败。 |

### 发现 D-15：自动化没有验证“真实 subsystem 初始化后会建立 world context”，B-18 没有回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 行号 | `AngelscriptSubsystemTests.cpp` 80-87，113-160，163-333；`AngelscriptTestEngineHelperTests.cpp` 627-677 |
| 描述 | `AngelscriptSubsystemTests.cpp` 虽然定义了会走真实 `UWorld -> UGameInstance -> GetSubsystem<UAngelscriptGameInstanceSubsystem>()` 的 `CreateSubsystemWorld()` helper（113-137），但我对该文件执行 `rg -n "CreateSubsystemWorld\\("` 只命中这一处定义，没有任何调用点。实际所有 subsystem 用例都走 `CreateInjectedSubsystem()` + `SetSubsystemPrimaryEngine()`，直接写 `PrimaryEngine`、`bInitialized`、`ActiveTickOwners` 并手工 `Push` 到 `ContextStack`。另一边，对 `Tests` / `AngelscriptTest` 执行 `rg -n "TryGetCurrentWorldContextObject\\(|GetAmbientWorldContext\\("`，命中的 world-context 测试只在 `AngelscriptTestEngineHelperTests.cpp` 里验证独立 scope helper 的恢复语义，并没有任何一条断言“真实 subsystem 初始化后 `TryGetCurrentWorldContextObject()` 已经可用”或 “subsystem 相关 `Get()` 绑定能解析到 world”。 |
| 根因 | 现有自动化把 subsystem 当成一个可注入裸 engine 的 tick 宿主来测试，而不是把它视为 `UGameInstance/UWorld` 到 `FAngelscriptEngine` 的正式集成边界；于是最关键的 world-context 建立合同被整套测试夹具绕开了。 |
| 影响 | B-18 这类“engine 已初始化但 world-sensitive API 仍无 world” 的缺陷不会被当前测试发现。测试可以继续证明 injected helper、scope helper 和 tick 探针可工作，却不能证明真实 `GameInstanceSubsystem` 启动后，`Bind_Subsystems`、unit-test world helper 以及其它依赖 `TryGetCurrentWorldContextObject()` 的运行时入口已经具备生产可用的 world context。 |

---

## 分析 (2026-04-08 11:30)

### 发现 D-16：自动化没有覆盖 `RuntimeModule` 的二次 `StartupModule()`，`A-12` 缺少直接回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `AngelscriptSubsystemTests.cpp` 335-416；`AngelscriptRuntimeModule.cpp` 13-18，27-40，138-166 |
| 描述 | 已验证事实：现有 RuntimeModule 相关测试只覆盖“一次启动”或“直接调用一次 `InitializeAngelscript()`”。`FAngelscriptRuntimeModuleStartupRegistersFallbackTickerTest` 在 380 行调用一次 `StartupModule()`，384 行立刻 `ShutdownModule()`，随后只断言 ticker handle 被清掉；`FAngelscriptRuntimeModuleInitializeCreatesGlobalEngineTest` 与 `FAngelscriptRuntimeFallbackTicksGlobalEngineWithoutSubsystemOwnerTest` 也都在测试前显式调用 `ResetInitializeStateForTesting()`，然后只执行一次初始化。没有任何测试会在同一进程内走出 `StartupModule() -> ShutdownModule() -> StartupModule()` 序列，再验证第二次启动是否重新创建主引擎。 |
| 根因 | 测试夹具默认把 `ResetInitializeStateForTesting()` 当成清理基线，这会把 `bInitializeAngelscriptCalled` 的真实生命周期问题掩掉；测试关注点停留在“首次启动能否注册 ticker / 建出 engine”，没有把模块重入当成正式合同。 |
| 影响 | `A-12` 这类“`ShutdownModule()` 不复位 `bInitializeAngelscriptCalled`，导致同进程二次启动永久跳过引擎初始化”的缺陷不会被当前自动化直接拦截。现有绿色结果只能证明首轮 startup 正常，不能证明模块 reload、插件重启或测试进程内的二次启动仍然可用。 |

---

## 分析 (2026-04-08 11:45)

### 发现 C-08：启动期编译报错后，`bHotReloadThreadStarted` 会把非 editor 热重载检查线程永久卡死

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1568-1620，1658-1700，2109-2120，2171-2195，2729-2778 |
| 描述 | `Initialize_AnyThread()` 先在 1568-1569 行执行 `InitialCompile()`，之后才在 1618-1620 行根据 `bScriptDevelopmentMode && !RuntimeConfig.bIsEditor` 正式启用 `bUseHotReloadCheckerThread` 并调用 `StartHotReloadThread()`。但编译失败分支会在 2109-2120 行提前把 `bUseHotReloadCheckerThread = true` 并启动线程，用于错误对话框里的轮询；`StartHotReloadThread()` 又在 1664-1666 行把 `bHotReloadThreadStarted` 设成一次性闸门。等用户修好脚本后，2195 行把 `bUseHotReloadCheckerThread` 恢复成进入错误分支前的旧值。对默认的非 editor threaded 初始化来说，这个旧值仍然是 `false`，后台线程会在 1681-1684 行看到标志关闭后直接退出；随后正常启动阶段再到 1618-1620 行时，`bHotReloadThreadStarted` 已经是 `true`，`StartHotReloadThread()` 不会再创建新线程。更糟的是，`CheckForHotReload()` 在 2774-2778 行会把 `bWaitingForHotReloadResults` 重新置为 `true`，而 2735-2740 行又要求后台线程把它清回去；没有活线程后，这个等待位将永久卡住。 |
| 根因 | RuntimeCore 把“线程曾经启动过”和“线程当前仍然存活”混成了同一个布尔状态；启动期错误对话框临时借用了热重载线程，却没有在恢复原模式时同步重置 `bHotReloadThreadStarted` 或重建线程。 |
| 影响 | 在 standalone / non-editor 开发模式下，只要初次编译报错并通过对话框修复一次，后续整条热重载检测管线就会静默失效：文件变化不再被后台线程扫描，`CheckForHotReload()` 还会在第一次等待后永久早返回。结果是脚本修改无法再触发 reload，而且故障表现为“系统安静地不工作”，很难从日志外观直接定位。 |

### 发现 A-28：启动期编译错误恢复重复使用 `volatile` 自旋等待，默认 threaded 初始化下存在确定性的跨线程死等窗口

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 825-848，2115-2191 |
| 描述 | `Initialize()` 的默认 threaded 路径会在 825-848 行把 `Initialize_AnyThread()` 放到 worker thread 执行，因此 `InitialCompile()` 的编译失败分支通常也运行在非 game thread。该分支在 2115 行声明 `volatile bool bErrorResponseDone = false`，随后 2188 行把 `ShowErrorDialog()` 投递到 game thread，并在 worker thread 上用 `while (!bErrorResponseDone) FPlatformProcess::Sleep(0.01f);` 忙等。与此同时，`ShowErrorDialog()` 会在 game-thread modal tick 中修改外层 `bSuccess`（2143-2145），并只在成功路径的 2176-2178 行把 `bErrorResponseDone = true`；失败路径 2171-2174 行直接 `RequestExit(true); return;`，连完成位都不写。整条恢复链既没有 `FEvent`、`TFuture`、原子变量，也没有任何 happens-before 保证。 |
| 根因 | 启动期错误恢复沿用了和 threaded 初始化主路径相同的“`volatile` + Sleep`”协作模式，但这一次还把 UI 结果 `bSuccess` 和完成位 `bErrorResponseDone` 一起跨线程裸共享，甚至在失败分支漏掉了完成通知。 |
| 影响 | 这条路径不是理论上的代码味道，而是实际的启动稳定性风险：成功修复脚本后，worker thread 可能一直看不到 game thread 写入的完成位而卡在自旋里；失败退出时，又会在进程真正退出前保持一个确定性的无限等待窗口。结果是“修好脚本后启动仍卡住”或“报错退出前线程长时间挂死”，把本该用于恢复的错误处理路径变成新的阻塞源。 |

### 发现 C-09：按需晚初始化的 engine 会错过 `OnPostEngineInit`，导致 coverage hook 与 test discovery 静默失效

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp` |
| 行号 | `AngelscriptEngine.cpp` 1628-1633，2201-2218，2481-2489；`AngelscriptCodeCoverage.cpp` 22-28；`UnitTest.cpp` 47-54；`IntegrationTest.cpp` 41-48；`LaunchEngineLoop.cpp` 4004-4007，4766-4769 |
| 描述 | RuntimeCore 把两项后续接线都绑定到了未来的 `FCoreDelegates::OnPostEngineInit`：1628-1633 行只有在该回调触发时才会执行 `CodeCoverage->AddTestFrameworkHooks()`，2201-2218 行也只有在该回调触发时才会执行 `DiscoverTests()` 并把 `bCompletedAssetScan = true`。但 UE 引擎自身在 `LaunchEngineLoop.cpp` 4004-4007 与 4766-4769 行只在 engine startup 期间广播 `OnPostEngineInit`。与此同时，`EnsureAngelscriptUnitRuntimeInitialized()` 和 `EnsureAngelscriptIntegrationRuntimeInitialized()` 仍保留了运行中按需调用 `FAngelscriptRuntimeModule::InitializeAngelscript()` 的入口（47-54，41-48）。如果这类 late init 发生在 `OnPostEngineInit` 已经广播之后，当前实现没有任何同步 fallback 去立即执行 `AddTestFrameworkHooks()`、`DiscoverTests()` 或补置 `bCompletedAssetScan`。 |
| 根因 | RuntimeCore 把 coverage / test discovery 的完成条件硬绑到一次性的 engine 生命周期事件，却没有区分“早期启动初始化”和“运行中按需初始化”两种合法入口，也没有在 late-init 场景下补一条立即执行路径。 |
| 影响 | 晚初始化出来的 engine 会静默失去两类关键功能：`FAngelscriptCodeCoverage` 永远不会接上 `OnTestsAvailable/OnTestsComplete`，而 `DiscoverTests()`/`bCompletedAssetScan` 也不会完成，直接使 2481-2489 行的 hot-reload unit-test 准备条件长期为假。结果是 coverage 和测试发现/热重载测试能力在 on-demand 初始化场景下表面“初始化成功”，实则功能缺失，而且当前实现没有任何显式告警来提示这类退化。 |

---

## 分析 (2026-04-08 11:59)

### 发现 A-29：多个 full engine 共享单个 `primaryContext` 槽，后创建/后销毁会把幸存 owner 的 usable context 清空

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_thread.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptobject.cpp` |
| 行号 | `AngelscriptEngine.cpp` 354-361，917-919，934-935；`as_thread.h` 52-56；`as_context.cpp` 150-155；`as_scriptobject.cpp` 48-63，141-156，506-519，813-826 |
| 描述 | `CreateTestingFullEngine()` 走 `InitializeForTesting()` 时，每个 full engine 都会直接执行 `GameThreadTLD->primaryContext = CreateContext()`，随后把这个线程单槽写进各自 `SharedState->PrimaryContext`。但 `asCThreadLocalData` 只有一个 `primaryContext` 字段；RuntimeCore 并没有为“同一线程上第二个 full engine 覆盖了第一个 owner 的 primary context”建立栈式保存/恢复。之后当后创建的 owner 销毁时，`ReleaseOwnedSharedStateResources()` 只会在“当前槽正好等于自己”时把 `GameThreadTLD->primaryContext` 置空，不会把仍然存活的旧 owner 的 `SharedState->PrimaryContext` 恢复回线程 TLS。结果是多 full-engine 并存后，只要较新的 owner 先退出，较旧但仍存活的 owner 就会失去 usable context。 |
| 根因 | RuntimeCore 把 AngelScript 的线程级 `primaryContext` 当成了每个 full engine 都能独占设置的 owner 状态，但底层实现实际上只有一个线程单槽；create path 只会覆盖，teardown path 只会清空匹配值，没有“上一个 owner 是谁”的恢复模型。 |
| 影响 | `asGetUsableContext()` 只看当前线程的 `activeContext/primaryContext`，而脚本对象 factory、析构和 `opAssign` 等路径都会先依赖它复用上下文。幸存 owner 的 `primaryContext` 被清空后，这些路径会系统性退化到 `engine->RequestContext()` 慢路径；一旦请求失败，third-party 代码在这些分支里直接 `return 0` / `return`，连错误上报都只留了 TODO。多引擎隔离在表面上仍可继续运行，但对象构造/析构/赋值已经不再具备稳定的 usable-context 基线。 |

### 发现 D-17：现有多 full-engine 自动化没有守住 `primaryContext` / `asGetUsableContext()`，A-29 没有直接回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 行号 | `AngelscriptMultiEngineTests.cpp` 483-518；`AngelscriptEngineIsolationTests.cpp` 376-409，892-919 |
| 描述 | 已验证事实：现有多 full-engine 测试确实覆盖了“允许第二个 full owner 共存”“`RequestContext()` 不串 engine”“销毁一个 owner 后另一个 owner 的 `TypeDatabase` 仍存活”这几条合同，但没有任何一条断言 `GameThreadTLD->primaryContext` 或 `asGetUsableContext()`。我对 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests` 和 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg -n "asGetUsableContext|primaryContext|GetUsableContext"`，结果均无命中。换句话说，现有自动化完全没有覆盖“创建两个 full engine，再销毁后创建的那个之后，幸存 owner 还能否保住 usable context”这条边界。 |
| 根因 | 测试设计把多引擎隔离主要建模成“类型数据库隔离”和“显式 `RequestContext()` 不串线”，但 RuntimeCore 实际还依赖 AngelScript 的线程级 `primaryContext` 作为隐式执行基线；这条隐式状态没有进入任何回归断言。 |
| 影响 | A-29 这类不会立刻让 `RequestContext()` 失败、但会悄悄破坏 usable-context 快路径的回归可以长期潜伏。测试继续是绿色，只能说明显式请求上下文和类型库隔离还在，不能说明脚本对象构造、析构、`opAssign` 等依赖 `asGetUsableContext()` 的真实运行路径在多 full-engine 生命周期下仍然正确。 |

### 发现 A-30：`FAngelscriptGameThreadScopeWorldContext` 的公开绑定绕过了 `BlueprintThreadSafe` 的 `WorldContext` 崩溃保护

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptGameThreadScopeWorldContext.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` |
| 行号 | `AngelscriptEngine.h` 710-721；`Bind_FAngelscriptGameThreadScopeWorldContext.cpp` 4-17；`ASClass.cpp` 1924-1937，1947-1953；`as_context.cpp` 5165-5171 |
| 描述 | RuntimeCore 在 AngelScript 调用系统函数前已经显式检查：只要目标函数带 `asTRAIT_USES_WORLDCONTEXT`，并且当前处于 `BlueprintThreadSafe` 执行态，就直接抛出 `"Calling a function that requires WorldContext from a BlueprintThreadSafe function is not allowed and can crash!"`。与此同时，thread-safe 生成路径会通过 `UASFunction::RuntimeCallFunction/Event()` 把 `GIsInAngelscriptThreadSafeFunction` 置为 `true`。但 `FAngelscriptGameThreadScopeWorldContext` 的构造/析构只是无条件调用 `FAngelscriptEngine::AssignWorldContext(...)`，而该 helper 又被 `Bind_FAngelscriptGameThreadScopeWorldContext.cpp` 公开绑定成任意脚本都可实例化的 value class；整个入口没有 `IsInGameThread()`、没有 `GIsInAngelscriptThreadSafeFunction` 检查，也不会触发 `asTRAIT_USES_WORLDCONTEXT` 这道保护。也就是说，RuntimeCore 一边声明“thread-safe 函数碰 world context 会 crash”，一边又公开暴露了一个可以直接改写 world context 的旁路。 |
| 根因 | `BlueprintThreadSafe` 的保护只落在“带 `USES_WORLDCONTEXT` trait 的系统函数调用”这一层，而没有把直接操纵 ambient/current engine world context 的 helper 也纳入同一条线程边界策略；绑定层把这个 helper 当作普通 value class 暴露后，策略出现了明显逃逸口。 |
| 影响 | 脚本只要在 `BlueprintThreadSafe` 函数里显式构造 `FAngelscriptGameThreadScopeWorldContext`，就能在不触发现有保护的前提下改写 `GAmbientWorldContext` 和 `CurrentEngine->WorldContextObject`。随后同一执行链再调用依赖 world context 的逻辑时，RuntimeCore 的“禁止并提示崩溃风险”合同就被绕空了；最坏情况下这会把本应在入口即被拒绝的 off-thread world 访问，升级成真正的崩溃或全局 world-context 污染。 |

---

## 分析 (2026-04-08 12:13)

### 发现 B-19：`GetCurrent()` 只能靠 ambient world 解析 subsystem，导致 adopted engine 在离开 scope 后重新解析失效

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | `AngelscriptGameInstanceSubsystem.cpp` 17-23，94-113；`AngelscriptEngine.cpp` 491-506，682-689，718-733；`AngelscriptTestUtilities.h` 46-68，107-119 |
| 描述 | `UAngelscriptGameInstanceSubsystem::Initialize()` 的 adopted 分支只是把 `TryGetCurrentEngine()` 命中的 engine 记到 `PrimaryEngine`，并不会像 owner 分支那样长期 `Push` 到 `ContextStack`。之后 RuntimeCore 重新解析当前 subsystem 时，`GetCurrent()` 只会把 `FAngelscriptEngine::GetAmbientWorldContext()` 交给 `GEngine->GetWorldFromContextObject()`；若 ambient world 为空，就直接返回 `nullptr`。但 `FAngelscriptEngineScope::Reset()` 在恢复 world context 时，会先把 `Engine->WorldContextObject` 还原成 `PreviousEngineWorldContext`，再 `Pop` 当前 scope；一旦外层没有继续存活的 current engine，ambient world 会被同步回 `nullptr`。这意味着 adopted subsystem 挂着的 `PrimaryEngine` 即使仍然存活且保留了 `WorldContextObject`，离开 scope 后也可能因为 ambient 被清空而无法再被 `GetCurrent()`/`TryGetCurrentEngine()` 找回。更直接的证据是，测试辅助 `TryGetRunningProductionSubsystem()` 已经不得不在 `GetCurrent()` 失败后，额外遍历 `GEngine->GetWorldContexts()` 再次查找 `GameInstanceSubsystem`。 |
| 根因 | subsystem 集成把“重新发现当前 production subsystem”硬绑定到了 ambient world context，而不是绑定到 subsystem 自身持有的 `PrimaryEngine` / `GameInstance` 边界；`WorldContextObject` 的持久状态与 ambient world 的瞬时状态被拆成两套解析源，但 `GetCurrent()` 只认后者。 |
| 影响 | 这会让 adopted engine 呈现“对象还活着，但核心查询说它不存在”的分裂状态。`TryGetCurrentEngine()`、`ShouldUseEditorScriptsForCurrentContext()`、`ShouldUseAutomaticImportMethodForCurrentContext()`、`IsScriptDevelopmentModeForCurrentContext()` 乃至依赖 `GetCurrent()` 的生产 helper，都可能在离开一层临时 scope 后突然退化为 `nullptr/false`。测试辅助已经用 world-scan fallback 绕过这个问题，而 RuntimeCore 自身没有同等级补救，说明 subsystem 集成边界并未真正收口。 |

### 发现 C-10：JIT 执行态的 `DebugBreak()` 无法走调试服务器暂停路径，最终会退化成原生 `UE_DEBUG_BREAK()`

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h` |
| 行号 | `Bind_Debugging.cpp` 14-28；`AngelscriptEngine.cpp` 4912-4920，5632-5648，5718-5745；`StaticJITHeader.h` 203-216 |
| 描述 | `Bind_Debugging.cpp` 的 `ASDebugBreak()` 会先调用 `FAngelscriptEngine::TryBreakpointAngelscriptDebugging()`，只有返回 `false` 时才执行 `UE_DEBUG_BREAK()`。但 `TryBreakpointAngelscriptDebugging()` 内部只接受 `asGetActiveContext()`，拿不到上下文就直接 `return false`。与此同时，RuntimeCore 自己已经证明 JIT 执行态并不总是通过 `asGetActiveContext()` 表达：`Throw()` 在 4914-4919 行优先看 `tld->activeExecution`，`GetAngelscriptExecutionPosition()` 也先走 `activeExecution->debugCallStack`；`StaticJITHeader.h` 203-216 行还把 `FScopeJITDebugCallstack` 明确挂在 `Execution.debugCallStack` 上。也就是说，JIT 路径已经有独立的执行态和调试栈，但断点接线没有接过去。 |
| 根因 | 调试断点的实现仍然只面向传统 AngelScript bytecode context，未把 RuntimeCore 自己扩展出来的 `activeExecution` / JIT debug stack 纳入同一套调试协议。 |
| 影响 | 在 JIT 执行的脚本里调用 `DebugBreak()`、`ensure()` 触发 `ASDebugBreak()` 时，即使调试服务器已经附着，RuntimeCore 也不会进入 `PauseExecution()`，而是直接退化到原生 `UE_DEBUG_BREAK()`。结果是 JIT 调试体验与非 JIT 路径不一致：远程调试器收不到标准的 script breakpoint 停止事件，开发者反而会被拉进宿主进程的原生断点，调试链路在最需要定位脚本问题的地方失真。 |

### 发现 D-18：自动化没有覆盖“ambient world 为空但 subsystem 仍存活”的重新解析边界，`B-19` 缺少直接回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 行号 | `AngelscriptTestEngineHelperTests.cpp` 447-467；`AngelscriptTestUtilities.h` 46-68，107-119；`AngelscriptSubsystemTests.cpp` 113-137 |
| 描述 | 当前唯一接近 production-subsystem 解析的 helper 测试 `FAngelscriptTestEngineHelperProductionHelperRejectsMissingProductionTest`，只是读取一次 `TryGetRunningProductionEngine()`，然后根据当下 `UAngelscriptGameInstanceSubsystem::GetCurrent()` / `FAngelscriptEngine::IsInitialized()` 的结果做静态断言；它没有构造“subsystem 仍然存活，但 ambient world 已被 scope 恢复成空”的场景。另一方面，`TryGetRunningProductionSubsystem()` 本身已经包含一条对 `UAngelscriptGameInstanceSubsystem::GetCurrent()` 失败后的 fallback：显式遍历 `GEngine->GetWorldContexts()` 再找 `GameInstance->GetSubsystem<>()`。我对 `Tests` 与 `AngelscriptTest` 执行 `rg` 后，除了这条 helper 自己的实现外，没有找到任何测试主动清空 ambient world、再断言 core `GetCurrent()` / `TryGetCurrentEngine()` 是否仍能解析到存活的 subsystem。`AngelscriptSubsystemTests.cpp` 虽然定义了真实 world helper `CreateSubsystemWorld()`，但前文已经验证它在该测试文件里没有调用点。 |
| 根因 | 现有自动化把 subsystem 解析问题交给测试辅助函数兜底，却没有把“core resolver 在 ambient 缺席时是否仍然正确”定义成正式合同；真实 world/subsystem 场景也被测试夹具长期绕开。 |
| 影响 | `B-19` 这种“helper 能靠 world-scan 兜住，但 RuntimeCore 自己的 `GetCurrent()`/`TryGetCurrentEngine()` 仍会失效”的问题不会被当前自动化直接拦截。测试继续为绿，只能证明测试辅助足够健壮，不能证明生产解析入口已经可靠。 |

---

## 分析 (2026-04-08 12:28)

### 发现 C-11：`AssetManager` 缺席 fallback 只调用 `DiscoverTests()`，却永远不把 `bCompletedAssetScan` 置真

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 2203-2216，2481-2489，4143-4153 |
| 描述 | `OnPostEngineInit` 回调里，`AssetManager != nullptr` 分支会在 `OnCompletedInitialScan` 中同时执行 `DiscoverTests()` 和 `bCompletedAssetScan = true`。但 `AssetManager == nullptr` fallback 只记录 warning 并直接 `DiscoverTests()`，没有任何地方再补 `bCompletedAssetScan = true`。同一 wrapper 后续两条功能路径又都把 `bCompletedAssetScan` 当硬门槛：`PrepareTests(...)` 需要 `GEngine && bCompletedAssetScan`，post-compile 增量测试发现也要求 `IsInitialCompileFinished() && bCompletedAssetScan`。当前文件里 `bCompletedAssetScan = true` 只有 2209 一处写入，因此 fallback 一旦命中，该标志会永久停留在 `false`。 |
| 根因 | RuntimeCore 把“AssetManager 初始扫描完成”和“测试发现已经可用”混成一个布尔状态，但 fallback 分支只补了前者的行为表现（立即 `DiscoverTests()`），没有同步推进这条全局生命周期状态。 |
| 影响 | 日志会提示 “Tests are discovered without completing an initial asset scan”，但 hot reload test queue 和后续增量测试发现实际仍被永久关闭，形成 silent degradation：初次发现看似成功，后续脚本热重载与测试再发现却不再工作，而且没有额外告警指出是 `bCompletedAssetScan` 卡在 `false`。 |

### 发现 B-20：clone wrapper 共享底层 `asIScriptEngine`，却不会继承 source wrapper 的模块索引与类型索引

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | `AngelscriptEngine.h` 311-326，385-391；`AngelscriptEngine.cpp` 2232-2249，2848-2856，2999-3055，4775-4880；`AngelscriptMultiEngineTests.cpp` 351-354 |
| 描述 | clone 创建路径通过 `AdoptSharedStateFrom()` 只复制 `Engine`、`ConfigSettings`、包对象、root path 以及两个布尔状态，并没有复制 `ActiveModules`、`ModulesByScriptModule`、`ActiveClassesByName`、`ActiveEnumsByName`、`ActiveDelegatesByName`。但 wrapper 级查询 API 完全建立在这些索引上：`GetModule()` / `GetActiveModules()` 直接查 `ActiveModules`，`GetModule(asIScriptModule*)` 查 `ModulesByScriptModule`，`GetClass()` / `GetEnum()` / `GetDelegate()` 也只扫描这些容器；`DiscoverTests()` 进一步遍历 `GetActiveModules()` 进行测试发现。也就是说，clone 虽然指向同一个底层 VM，却在 wrapper 视角上天然“看不见” source 已加载模块。测试代码本身已经暴露出这条断层：`MultiEngine.CloneDestroyDoesNotAffectPrimary` 在 clone 上创建 raw script module 后，还要额外调用 `TrackNamedModule()` 手工把描述符塞进 `ActiveModules` / `ModulesByScriptModule`。 |
| 根因 | shared-state 设计只共享了 VM 和少量配置快照，却没有把 RuntimeCore 另一半同样关键的 wrapper 元数据层纳入共享或同步模型；clone 因而变成“底层 engine 共用，管理索引各自为空”的半投影对象。 |
| 影响 | 任何在 clone scope 中依赖 wrapper 元数据的 RuntimeCore 功能都会退化，包括模块名/文件名解析、类/枚举/委托查找、测试发现，以及后续基于 `GetActiveModules()` 的 hot reload 辅助逻辑。表现不会是统一崩溃，而是 clone 表面“能执行脚本”，但一批依赖模块索引的高层能力系统性返回空结果；这类分层断裂比显式失败更难定位。 |

---

## 分析 (2026-04-08 12:29)

### 发现 D-19：multi-engine 自动化用 `TrackNamedModule()` 手工补 clone 索引，`CreateCloneFrom()` 的真实模块投影合同没有回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | 32-49，322-354 |
| 描述 | `AngelscriptMultiEngineTests.cpp` 为测试专门暴露了 `TrackNamedModule()` helper，直接往 `Engine.ActiveModules` 和 `Engine.ModulesByScriptModule` 塞测试描述符。现有 clone 相关用例也确实依赖这条后门：`CloneDestroyDoesNotAffectPrimary`、双 clone 相关用例都会先用底层 `Engine.Engine->GetModule(...)` 创建 raw script module，再调用 `TrackNamedModule()` 补齐 wrapper 索引。也就是说，自动化验证的是“手工回填后的 clone 能否继续工作”，而不是 `CreateCloneFrom()` 本身是否已经把 source wrapper 的模块/类型索引正确投影到 clone。 |
| 根因 | 测试夹具把 clone 的模块元数据当成可随手注入的布置状态，而不是 clone 生命周期必须天然满足的合同；一旦引入 `TrackNamedModule()`，真正的生产创建路径就被测试绕开了。 |
| 影响 | `B-20` 这类“底层 VM 共享但 wrapper 模块索引为空”的回归不会被当前 multi-engine 套件直接拦截。测试继续为绿，只能证明 friend helper 足够强，不能证明 RuntimeCore 的 clone 创建边界已经完整。 |

---

## 分析 (2026-04-08 12:39)

### 发现 B-21：clone wrapper 漏同步 `bIsInitialCompileFinished`，进入 clone scope 后会被误判为“仍在初次编译期”

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptEngine.cpp` 2848-2856；`AngelscriptEngine.h` 242-249，481-488；`ASClass.cpp` 53-75 |
| 描述 | `AdoptSharedStateFrom()` 在 clone 创建时只复制 `Engine`、包对象、`bDidInitialCompileSucceed` 和 `bCompletedAssetScan`，没有复制 `bIsInitialCompileFinished`。而 `FAngelscriptEngine` 头文件把这个字段默认初始化为 `false`。结果是 clone wrapper 明明共享了一台已经完成初次编译的底层 `asIScriptEngine`，其本地状态却会永久保留“初次编译尚未完成”。这不是孤立标志：`CanUseGameThreadData()` 直接用 `!CurrentEngine->bIsInitialCompileFinished` 放宽 game-thread 限制，`ASClass.cpp` 的 `CheckGameThreadExecution()` 也会在该标志为 `false` 时跳过对非 game-thread Blueprint 调用的保护。只要 `TryGetCurrentEngine()` 在 clone scope 下解析到 clone wrapper，这些判断就会读到错误生命周期状态。 |
| 根因 | clone 方案只共享了底层 VM 和部分结果状态，却漏掉了同样属于运行时生命周期合同的 `bIsInitialCompileFinished`；wrapper 级“启动阶段是否已经结束”因此没有随 shared state 一起投影到 clone。 |
| 影响 | clone scope 下的一批运行时保护会系统性退化到“初始化特权模式”：off-thread 代码可能被错误允许访问 game-thread 数据，Blueprint override 的线程检查也可能被绕过。表现不是 clone 无法工作，而是它在边界检查上比 source engine 更宽松，导致真实线程问题只在非 clone 场景下暴露，调试和隔离结论都会失真。 |

---

## 分析 (2026-04-08 12:41)

### 发现 A-31：`FAngelscriptGameThreadScopeWorldContext` 是可复制的 script value type，但析构会回滚全局 world context

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptGameThreadScopeWorldContext.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp` |
| 行号 | `AngelscriptEngine.h` 710-724；`Bind_FAngelscriptGameThreadScopeWorldContext.cpp` 4-17；`AngelscriptBinds.h` 141-147；`angelscript.h` 641-689；`AngelscriptObjectModelTests.cpp` 48-55 |
| 描述 | `FAngelscriptGameThreadScopeWorldContext` 只保存一个 `PreviousWorldContext`，并在析构时调用 `AssignWorldContext(PreviousWorldContext)` 回滚全局状态；但它不像 `FAngelscriptEngineScope` 那样删除 copy/assignment，也没有 move-only 保护。更关键的是，这个类型被直接绑定成 `ValueClass<FAngelscriptGameThreadScopeWorldContext>`，而 `ValueClass` 会叠加 `asGetTypeTraits<T>()`；后者会为“可复制且非 trivial”的 class 自动声明 assignment 与 copy constructor trait。现有 `AngelscriptObjectModelTests` 还明确证明了脚本 value type 的复制是正常语言行为。也就是说，这个带析构副作用的 RAII scope 已被公开成可复制的脚本值类型。 |
| 根因 | RuntimeCore 把“作用域回滚 helper”当成普通 value class 暴露，却没有像 `FAngelscriptEngineScope` 一样把复制语义禁掉；绑定层又根据 C++ traits 自动把 copy/assignment 能力暴露给 AngelScript。 |
| 影响 | 一旦脚本或 C++ 侧发生按值传递、临时复制、赋值或容器搬运，多个实例就会共享同一份 `PreviousWorldContext` 快照，并在不同析构点反复调用 `AssignWorldContext(...)`。结果不是简单的多析构，而是 ambient world context 和 current engine 的 `WorldContextObject` 都可能被旧快照回滚，形成作用域错乱、嵌套恢复顺序被破坏，甚至把后建立的合法 world context 覆盖成陈旧值。 |

---

## 分析 (2026-04-08 12:42)

### 发现 D-20：现有自动化没有覆盖 clone 的 `bIsInitialCompileFinished` 合同，`B-21` 缺少直接回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 行号 | 838-878 |
| 描述 | 当前与 engine-local flag 最接近的回归测试 `FAngelscriptEngineLocalFlagsIsolationTest` 只创建了两台 `CreateTestingFullEngine()` full engine，并断言 `bSimulateCooked` / `bTestErrors` 以及对应的 current-context 查询是否按实例隔离。测试里没有任何 clone。与此同时，我对 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests` 和 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg -n "IsInitialCompileFinished|CanUseGameThreadData|CheckGameThreadExecution"`，没有找到任何命中。也就是说，现有自动化完全没有断言 “clone scope 下 `IsInitialCompileFinished()` 是否继承 source engine” 或 “启动完成后 clone 仍会维持正确的 game-thread guard”。 |
| 根因 | multi-engine 测试把 clone 合同主要建模成“共享 VM、source 生命周期、模块索引和 bind 观测”，却没有把 wrapper 级启动状态也当成 clone 必须投影的正式合同。 |
| 影响 | `B-21` 这类 “clone 仍能工作，但线程/生命周期边界判断已经退化” 的回归不会被当前测试直接发现。测试继续为绿，只能证明 clone 的共享引擎语义还在，不能证明 clone scope 下的安全门禁与 source engine 保持一致。 |

---

## 分析 (2026-04-08 13:23)

### 发现 B-22：`AdoptSharedStateFrom()` 只快照复制 `bCompletedAssetScan`，clone 不会跟随后续 asset-scan 完成

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptEngine.h` 481-484；`AngelscriptEngine.cpp` 2201-2220，2481-2489，2848-2856，4143-4153 |
| 描述 | `bCompletedAssetScan` 是 wrapper 本地布尔值，source full engine 只会在 `OnPostEngineInit -> OnCompletedInitialScan` 这条异步回调里把它置为 `true`。但 clone 创建路径的 `AdoptSharedStateFrom()` 只是把当前瞬间的 `Source.bCompletedAssetScan` 复制一次，并没有把这个标志放进 `SharedState` 或建立后续同步。与此同时，RuntimeCore 后续两条测试发现路径都直接读当前 wrapper 自己的 `bCompletedAssetScan`：hot reload test 准备在 2481-2489 行要求该标志为真，post-compile 增量测试发现也在 4143-4153 行用它做硬门槛。也就是说，clone 若诞生在 initial asset scan 完成之前，就会把“尚未扫描完”永久冻结在自己身上。 |
| 根因 | multi-engine 设计只共享了底层 VM 和部分编译结果，却把 `bCompletedAssetScan` 这种会在 engine 启动后异步推进的生命周期状态保留成 wrapper-local 快照；clone 之后没有任何回流机制把 source 的后续状态变化同步过来。 |
| 影响 | clone scope 下触发的增量编译、hot reload test 准备和测试再发现会被静默降级成“永远认为 asset scan 未完成”。表现不是立即崩溃，而是 source engine 侧明明已经完成扫描并可发现测试，clone 侧仍会持续跳过这些能力，导致 multi-engine 工具链和测试辅助对同一台底层引擎给出彼此矛盾的生命周期判断。 |

### 发现 C-12：`GetAngelscriptExecutionThisObject()` 没有接入 JIT `debugCallStack`，JIT 执行态下会稳定丢失当前 `this`

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h` |
| 行号 | `AngelscriptEngine.cpp` 5101-5125，5635-5648，5668-5696，5700-5715；`StaticJITHeader.h` 194-217，338-339；`as_context.cpp` 144-162；`as_context.h` 262-279 |
| 描述 | RuntimeCore 已经为 JIT 执行态建立了完整的 `debugCallStack` 数据：`FScopeJITDebugCallstack` 明确保存 `ThisObject`，`SCRIPT_DEBUG_CALLSTACK_FRAME_UOBJECT` 还会把 `l_This` 写进去；`GetStackTrace()`、`GetAngelscriptExecutionPosition()`、`GetAngelscriptExecutionFileAndLine()` 也都已经优先消费 `tld->activeExecution->debugCallStack`。但 `GetAngelscriptExecutionThisObject()` 仍然只调用 `asGetActiveContext()` 再读 `Context->GetThisPointer()`。第三方 AngelScript 实现又明确表明：`asGetActiveContext()` 只返回 `tld->activeContext`，而 `FScriptExecution` 在 JIT 执行期间会把 `tld->activeContext` 置为 `nullptr`。因此，这个 API 在 JIT 执行态下无法读取到 RuntimeCore 自己已经维护好的 `ThisObject`，会直接返回 `nullptr`。 |
| 根因 | “当前执行位置”这一组辅助接口只完成了一半的 JIT 适配：file/line/callstack 已切到 `activeExecution` 模型，但 `this-object` 查询仍停留在旧的 bytecode context 语义，没有复用同一条 JIT 调试栈。 |
| 影响 | 任何依赖该接口恢复当前脚本对象的调试、日志或工具链代码，在 JIT/transpiled 路径上都会稳定拿到空结果，即使 RuntimeCore 明明已经拥有正确的 `ThisObject`。这会让 JIT 场景下的“当前脚本对象是谁”查询退化成假阴性，进一步放大 script/native 调试体验的不一致。 |

### 发现 D-21：自动化没有覆盖 clone 的 asset-scan 状态同步，也没有覆盖 JIT `this-object` 查询接口

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `AngelscriptMultiEngineTests.cpp` 214-222，310-311，343，375-432，658，692-723，765-766；`AngelscriptSubsystemTests.cpp` 167-245，307-327；`AngelscriptPrecompiledDataTests.cpp` 27-77 |
| 描述 | 我对 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests` 与 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg -n "bCompletedAssetScan|GetAngelscriptExecutionThisObject|GetAngelscriptExecutionPosition|GetAngelscriptExecutionFileAndLine"`，结果为 0 命中，说明现有自动化没有任何一条断言直接覆盖这组生命周期/执行态接口。与此同时，对同一批测试目录执行 `rg -n "CreateCloneFrom\\(|CreateForTesting\\(.*Clone"`，可以确认 clone 创建在 `AngelscriptMultiEngineTests.cpp`、`AngelscriptSubsystemTests.cpp`、`AngelscriptPrecompiledDataTests.cpp` 中被大量使用，但这些用例只验证 shared-state、生存期和少量 wrapper 行为，没有任何场景构造“clone 创建后 source 才完成 asset scan”，也没有任何场景验证 JIT 执行态下 `GetAngelscriptExecutionThisObject()` 是否还能返回对象。 |
| 根因 | 当前测试套件虽然已经广泛引入 clone，但验证重点仍停留在 shared VM、生存期与模块索引；对异步生命周期标志同步和 JIT 辅助接口一致性，没有被定义成正式测试合同。 |
| 影响 | `B-22` 与 `C-12` 这类“同一底层 engine 在不同 wrapper / 不同执行态上读取到不同生命周期事实”的问题不会被现有自动化直接拦截。测试继续为绿，只能证明 clone 和 JIT 基础设施大体可运行，不能证明它们对外暴露的 runtime 辅助接口仍然一致。 |

---

## 分析 (2026-04-08 13:40)

### 发现 C-13：非 Editor 默认 threaded 初始化会在 worker thread 上加载 bind modules，未预加载模块会被 `FModuleManager` 直接拒绝

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Private/Modules/ModuleManager.cpp` |
| 行号 | `AngelscriptEngine.cpp` 825-840，1466-1490；`ModuleManager.cpp` 157-161，914-928，1004-1016 |
| 描述 | `FAngelscriptEngine::Initialize()` 在非 Editor 下默认走 threaded 路径，把 `Initialize_AnyThread()` 投到 `AnyHiPriThreadHiPriTask`。而 `Initialize_AnyThread()` 在 1481-1488 行遍历 `FAngelscriptBinds::GetBindModuleNames()`，对每个名字直接执行 `FModuleManager::Get().LoadModule(...)`。UE 自身的 `FModuleManager::GetOrLoadModule()` 明确规定：如果模块尚未加载且当前不在 game thread，就返回 `EModuleLoadResult::NotLoadedByGameThread` 并直接失败（914-928 行）；`WarnIfItWasntSafeToLoadHere()` 与 `LoadModuleWithFailureReason()` 的注释还明确写着“outside the main thread / doing LoadModule off GameThread should not be done”（157-161，1004-1016 行）。RuntimeCore 这里既没有检查返回值，也没有回到 game thread 重试。 |
| 根因 | 初始化流程把“扫描 bind module 名单”和“真正加载模块 DLL / startup code”混在 `Initialize_AnyThread()` 里执行，默认 non-editor 生命周期因而把一段必须由 game thread 完成的模块加载逻辑搬到了 worker thread。 |
| 影响 | 只要某个 auto-generated bind module 在这之前还没被主线程加载，它在 packaged/non-editor 默认初始化路径上就会被 `FModuleManager` 拒绝，后续绑定缺失却没有补救分支。结果是同一批 bind modules 在 editor 或预加载场景能工作，在默认 runtime threaded init 场景却可能静默失效，形成只在非 editor 环境出现的 API 缺口。 |

---

## 分析 (2026-04-08 13:56)

### 发现 A-32：`AssignWorldContext()` 只清洗 ambient 指针，未清洗 `WorldContextObject`，会把无效 `UObject*` 固化进当前 engine

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp` |
| 行号 | `AngelscriptEngine.cpp` 268-276，682-689，746-752，1768-1782；`AngelscriptEngine.h` 714-720，750，764；`Bind_AActor.cpp` 169-234，352-446；`Bind_Subsystems.cpp` 73-89；`UnitTest.cpp` 66，134 |
| 描述 | `FAngelscriptEngine::AssignWorldContext()` 先把 `NewWorldContext` 原样写入 `CurrentEngine->WorldContextObject`，然后才调用 `SetAmbientWorldContext(NewWorldContext)`。但 `SetAmbientWorldContext()` 自己会在 268-276 行用 `IsValidLowLevelFast(false)` 把无效对象清成 `nullptr`。也就是说，同一个输入指针会出现“ambient 已清空，但 engine-local `WorldContextObject` 仍保留原始值”的分裂状态。更糟的是，RuntimeCore 里有多条恢复路径会把先前缓存的 ambient 指针重新喂回 `AssignWorldContext()`：`FAngelscriptGameThreadScopeWorldContext`、`FAngelscriptContext`、`FAngelscriptGameThreadContext` 都保存 `PreviousWorldContext`，析构时直接 `AssignWorldContext(PreviousWorldContext)`。而 `TryGetCurrentWorldContextObject()` 在存在 current engine 时又会优先返回 `WorldContextObject`，根本不会复用已经被清洗过的 ambient 值。 |
| 根因 | RuntimeCore 把 world-context 统一入口拆成了“两阶段写入”：engine-local 字段先写，ambient 缓存后校验；校验逻辑没有前移到 setter 入口，也没有在 `TryGetCurrentWorldContextObject()` 读取 engine-local 字段时补做有效性检查。 |
| 影响 | 一旦恢复路径拿到的是过期 `UObject*`，ambient 侧表面上会被纠正成 `nullptr`，但当前 engine 仍会继续通过 `TryGetCurrentWorldContextObject()` 向外暴露悬空指针。下游调用点很多，包括 `Bind_AActor` 的 `UGameplayStatics::*` 查询、`Bind_Subsystems` 的 `GetWorldFromContextObject()` 以及 `UnitTest` 的世界解析。结果是 world 解析会在已失效对象上继续运行，形成 use-after-free / 崩溃风险，而且故障表象会和 ambient 状态不一致，进一步放大排查难度。 |

---

## 分析 (2026-04-08 14:00)

### 发现 D-22：world-context 自动化只验证“活对象恢复”，没有任何回归测试覆盖失效对象恢复路径

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 行号 | `AngelscriptTestEngineHelperTests.cpp` 625-677；`AngelscriptEngineIsolationTests.cpp` 219-229 |
| 描述 | 现有 world-context 相关自动化只断言正常的 live-object 恢复语义。`FAngelscriptTestEngineHelperWorldContextScopeRestoreTest` 和 `FAngelscriptTestEngineHelperEngineScopeWorldContextRestoreTest` 都是 `NewObject` 一个 `DummyContext`，验证 scope 进入/退出前后 `GetAmbientWorldContext()` 或 `TryGetCurrentWorldContextObject()` 是否等于同一个仍然存活的对象。`AngelscriptEngineIsolationTests.cpp` 的 world-context 用例也只检查嵌套 scope 期间 `PrimaryEngine->GetCurrentWorldContextObject()` 的切换与清空。我又对这两份测试文件执行 `rg -n "CollectGarbage|DestroyWorld|ConditionalBeginDestroy|MarkAsGarbage|IsValidLowLevelFast"`，结果为 0 命中，说明当前没有任何一条回归测试构造“previous world context 在恢复前已经失效”的场景。 |
| 根因 | 当前测试设计把 world-context helper 当成纯粹的栈恢复工具验证，没有把 GC / world teardown / 失效对象恢复纳入正式合同，因此 `AssignWorldContext()` 的有效性边界从未被自动化触发。 |
| 影响 | `A-32` 这类“恢复路径把失效 `UObject*` 再写回 engine-local 状态”的问题不会被现有测试直接发现。自动化保持绿色只能证明 live-object scope 正常，不能证明 RuntimeCore 在对象销毁、GC 或 world 切换后的恢复路径仍然安全。 |

---

## 分析 (2026-04-08 14:03)

### 发现 A-33：`OptimizedCall_*` 快路径绕过 `CheckGameThreadExecution()`，会跳过 Blueprint override 的线程边界保护

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `ASClass.cpp` 53-75，1561-1731，1978-2032；`AngelscriptEngine.cpp` 1778-1783 |
| 描述 | `ASClass.cpp` 已经把非 game-thread Blueprint override 调用定义为非法：`CheckGameThreadExecution()` 会在 53-75 行拒绝 “default statement 或其他线程” 上的调用。慢路径也确实执行了这条合同，`UASFunction_NoParams::RuntimeCallFunction/Event` 等入口在 1978-2032 行先 `if (!CheckGameThreadExecution(this)) return;`，之后才构造 `FAngelscriptGameThreadContext`。但几组快路径 `OptimizedCall_*` 完全没有这道检查：`OptimizedCall_ByteReturn`、`OptimizedCall_FloatArg`、`OptimizedCall_DoubleArg`、`OptimizedCall_RefArg_ByteReturn`、`OptimizedCall_RefArg` 在 1561-1731 行要么直接执行 raw JIT thunk，要么立刻创建 `FAngelscriptGameThreadContext Context(Object, RealFunction->GetEngine())`。而 `FAngelscriptGameThreadContext` 自己在 `AngelscriptEngine.cpp` 1778-1783 行也只是无条件拿 `GameThreadTLD` 建 context，没有任何 `IsInGameThread()` 防护。 |
| 根因 | 线程边界保护被放在了部分调度入口上，而不是放在 `FAngelscriptGameThreadContext` 或统一的 override 调度层上；一旦调用走到 optimized thunk，快路径就绕过了慢路径上的 `CheckGameThreadExecution()` 合同。 |
| 影响 | 这会让同一类 Blueprint override 在“是否走 optimized call”上表现出不同的线程安全语义。off-thread 调用若命中快路径，就可能直接使用 `GameThreadTLD`、执行 raw JIT thunk 或推进脚本上下文，绕过 RuntimeCore 明确声明的 game-thread 限制，形成跨线程脚本执行、错误 world-context 写入乃至崩溃风险。 |

---

## 分析 (2026-04-08 14:04)

### 发现 D-23：现有自动化调用脚本函数只走 `RuntimeCallEvent`，没有任何回归测试覆盖 `OptimizedCall_*` 的线程合同

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests`；`Plugins/Angelscript/Source/AngelscriptTest` |
| 行号 | `AngelscriptTestEngineHelper.cpp` 448，460 |
| 描述 | 当前测试 helper 在执行脚本函数时，只要 `Function` 是 `UASFunction`，就固定调用 `ScriptFunction->RuntimeCallEvent(Object, &OutResult)`。我又对 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests` 与 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg -n "OptimizedCall_|CheckGameThreadExecution|RuntimeCallEvent\\("`，结果只有 helper 本身的两处 `RuntimeCallEvent` 调用命中，没有任何 `OptimizedCall_*` 或 `CheckGameThreadExecution` 相关断言。也就是说，现有自动化验证的是慢路径调度合同，而不是生产里同样会被走到的 optimized thunk。 |
| 根因 | 测试基础设施把脚本函数调用统一收口到 `RuntimeCallEvent`，没有为 optimized dispatch 建立单独的执行入口或断言，因此快路径线程边界被系统性跳过。 |
| 影响 | `A-33` 这类“慢路径安全、快路径失守”的回归不会被当前测试套件直接拦住。自动化继续为绿，只能证明 helper 驱动的慢路径仍然工作，不能证明 Blueprint override 的真实 optimized 调度在异步线程、default statement 或其他边界场景下仍然遵守同一套线程合同。 |
