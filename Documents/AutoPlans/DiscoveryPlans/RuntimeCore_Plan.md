# RuntimeCore 发现与规划

---

## 发现与方案 (2026-04-08 12:29)

### Issue-01：full engine 常规销毁路径不会刷新 ambient world context

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `922-941`; `1134-1244`; `39-45`; `35-38` |
| 问题 | `InitializeOwnedSharedState()` 会在 full engine 的正常初始化路径上稳定创建 `SharedState`（`AngelscriptEngine.cpp:922-941`），但 `Shutdown()` 只在 `!LocalSharedState.IsValid()` 时才调用 `SyncAmbientWorldContextFromCurrentEngine()`（`AngelscriptEngine.cpp:1241-1244`）。这意味着常规 owner teardown 根本不会刷新 ambient world。与此同时，subsystem 与 runtime module 的 owner 路径都先直接 `FAngelscriptEngineContextStack::Pop(...)` 再销毁 engine（`AngelscriptGameInstanceSubsystem.cpp:39-45`，`AngelscriptRuntimeModule.cpp:35-38`），中间没有任何 ambient/world-context 同步。 |
| 根因 | ambient world 刷新被错误绑定到了“没有 shared state 的少数路径”，而不是 owner engine 退出 current-engine 解析链时的通用生命周期合同；低层 `ContextStack::Pop()` 也没有封装这项同步责任。 |
| 影响 | owner engine 正常销毁后，`GAmbientWorldContext` 仍可能保留上一次脚本执行写入的对象。后续 `UAngelscriptGameInstanceSubsystem::GetCurrent()`、`TryGetCurrentWorldContextObject()` 以及依赖 ambient world 的日志/异常路径会继续基于 stale world 解析，扩大为错误 subsystem 解析或悬空 world 访问。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 ambient/world-context 刷新收口到 owner teardown 的统一 helper，而不是依赖个别调用点手工补齐。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 内新增私有 helper，例如 `RefreshAmbientWorldContextAfterDetach()`，专门处理 owner engine 脱离 current-engine 解析链后的 ambient 同步。 2. 修改 `Shutdown()`，在 owner engine 成功进入 teardown 且即将失效时无条件执行该 helper，不再以 `!LocalSharedState.IsValid()` 作为前提。 3. 将 `UAngelscriptGameInstanceSubsystem::Deinitialize()` 与 `FAngelscriptRuntimeModule::ShutdownModule()` 的裸 `Pop()` 收口到统一入口，保证 `Pop` 成功后立即刷新 ambient world，再执行 engine reset/shutdown。 4. 为 subsystem owner teardown 与 runtime-module owner teardown 各补一条自动化，先写入非空 world context，再断言 teardown 后 ambient/current world 解析恢复到空或外层 scope。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 现有少数测试夹具可能隐式依赖“teardown 后 ambient 仍保留旧 world”的错误行为，修复后需要同步更新断言。 |
| 前置依赖 | 无 |
| 验证方式 | 新增自动化覆盖 subsystem/module 两条 owner teardown 路径；在 teardown 前后断言 `FAngelscriptEngine::GetAmbientWorldContext()`、`TryGetCurrentWorldContextObject()`、`UAngelscriptGameInstanceSubsystem::GetCurrent()` 的返回值。 |

### Issue-02：初始化链路把同一段 engine bootstrap 分散复制到多个大函数里

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `819-856`; `867-919`; `1280-1324`; `1372-1423` |
| 问题 | `Initialize()` 负责 threaded orchestration（`819-856`），`PreInitialize_GameThread()` 负责基础 engine 创建（`1280-1324`），而 `InitializeForTesting()` 与 `Initialize_AnyThread()` 又各自复制了一大段几乎相同的 bootstrap：运行时 flag 传播、root package 创建、十余个 `SetEngineProperty(...)`、`SetMessageCallback(...)` 与 `SetContextCallbacks(...)`（`867-919` 对应 `1372-1423`）。当前实现把“通用初始化”“testing 差异”“production 差异”交错写在同一个文件的多个大函数里，没有单一事实来源。 |
| 根因 | 初始化过程长期靠新增语句堆叠演进，缺少按职责切开的 helper 和显式阶段模型，导致 testing/prod 两条路径只能通过手工复制保持同步。 |
| 影响 | 每次修复 engine property、callback、GC 行为或 package 生命周期时，都需要在至少两条路径里手工同步，极易再次制造分支漂移。对于 RuntimeCore 这种已经承载 threaded initialize、hot reload、coverage、debug server 的中心模块，这会持续放大后续缺陷的引入概率和审查成本。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 以“公共 bootstrap + 模式差异”重组初始化代码，把重复配置提炼成可复用 helper。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 私有区新增 3 个 helper：`ApplyRuntimeFlagsFromConfig()`、`CreateRootPackages()`、`ConfigureScriptEngineCommon()`，分别收口运行时布尔状态、package 创建和通用 `SetEngineProperty`/callback 注册。 2. 让 `InitializeForTesting()` 与 `Initialize_AnyThread()` 统一调用这组 helper，只保留各自真正不同的步骤，例如 `BindScriptTypes()`、`InitialCompile()`、precompiled data 与 hot reload 初始化。 3. 将 `Initialize()` 继续只保留线程切换与阶段编排，不再混入任何具体 bootstrap 细节，使 threaded/non-threaded 差异只体现在调度层。 4. 新增一个小型自动化，分别走 testing/prod 初始化后读取关键 engine property（如 `asEP_AUTO_GARBAGE_COLLECT`、`asEP_ALLOW_IMPLICIT_HANDLE_TYPES`、`asEP_PROPERTY_ACCESSOR_MODE`）并断言公共配置一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 重构会改变初始化调用顺序，若 helper 切分不当，可能把本应仅存在于 production 路径的副作用带入 testing 路径。 |
| 前置依赖 | 无 |
| 验证方式 | 编译通过后，运行现有 multi-engine/subsystem/runtime-core 自动化，并新增对 testing/prod 共有 engine property 的一致性断言。 |

### Issue-03：current-engine 解析与 world-context 解析形成环状依赖，职责边界不清

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` |
| 行号 | `287-295`; `718-753`; `94-113` |
| 问题 | `FAngelscriptEngine::TryGetCurrentEngine()` 在 `ContextStack` 为空时，会回退到 `UAngelscriptGameInstanceSubsystem::GetCurrent()`（`AngelscriptEngine.cpp:718-733`）；而 `GetCurrent()` 自己又只能通过 `FAngelscriptEngine::GetAmbientWorldContext()` 去反查 `World -> GameInstance -> Subsystem`（`AngelscriptGameInstanceSubsystem.cpp:94-113`）。另一方面，`AssignWorldContext()` 和 `SyncAmbientWorldContextFromCurrentEngine()` 又会先调用 `TryGetCurrentEngine()` 再写 ambient/current-engine world（`AngelscriptEngine.cpp:287-295`, `746-753`）。也就是说，engine 解析依赖 world 解析，world 解析又依赖 engine 解析来维护状态，形成了跨模块的隐式环。 |
| 根因 | RuntimeCore 没有把“当前 engine”“ambient world”“subsystem 查找”建模成三个独立职责，而是通过若干静态 helper 在 `FAngelscriptEngine` 与 `UAngelscriptGameInstanceSubsystem` 之间互相回调，靠全局状态拼出当前上下文。 |
| 影响 | 这类环状依赖让任何一侧的生命周期抖动都会污染另一侧：scope 退出、owner teardown、thread fallback 或 adopted subsystem 解析失配时，问题不会局限在单个模块，而会沿着 resolver 链条扩散成 current-engine 失效、ambient world 丢失、subsystem 查询漂移等跨模块故障。后续再补单点修复时，也会持续被这条隐式依赖链反噬。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 拆开 engine resolver、world-context storage 和 subsystem lookup，取消彼此之间的静态回跳。 |
| 具体步骤 | 1. 在 RuntimeCore 中引入单独的 resolver 边界，例如 `FAngelscriptExecutionContextResolver`，明确区分三种来源：`ScopedEngine`、`SubsystemBoundEngine`、`AmbientWorldContext`。 2. 将 `UAngelscriptGameInstanceSubsystem::GetCurrent()` 改造成显式按 `UWorld*` 或 `UGameInstance*` 查询的 API，例如 `TryGetForWorld(UWorld*)`，不再从 engine 静态 helper 里反向偷取 ambient world。 3. 将 `AssignWorldContext()` 拆成 engine-local 与 ambient 两个独立写入口，禁止一个 setter 同时修改 resolver 输入和 resolver 输出。 4. 让 `TryGetCurrentEngine()` 只消费 resolver 已经准备好的输入，不再直接触发 subsystem/world 反查；对于必须从 world 进入的调用点，由上层先显式提供 world，再调用 subsystem resolver。 5. 补充契约测试，分别验证 `ContextStack` 驱动解析、world 驱动解析、以及 teardown 后 resolver 清理，确保三条链路互不隐式反向修改。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | L |
| 风险 | 这是 resolver 边界重构，影响面会覆盖大量静态 helper 和测试夹具；若迁移分阶段进行，需要短期兼容层防止行为突变。 |
| 前置依赖 | 建议先完成 Issue-01，先把 teardown 后的 ambient 同步合同收口，再拆 resolver。 |
| 验证方式 | 增加三组回归：`ContextStack` 独立解析、`UWorld` 显式解析、teardown 后 resolver 清理；并在 multi-engine/subsystem 场景下验证 `TryGetCurrentEngine()` 不再隐式依赖 ambient world。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-01 | Defect | 立即修复，先收口 owner teardown 的 ambient/world-context 合同 |
| P1 | Issue-03 | Architecture | 在 Issue-01 之后推进，拆 resolver 边界并清理隐式依赖 |
| P2 | Issue-02 | Refactoring | 穿插执行，在修复高优先级问题前先抽出公共 bootstrap helper 也可降低后续改动风险 |

---

## 发现与方案 (2026-04-08 12:36)

### Issue-04：world context 持有模型同时依赖裸静态指针和非 GC owner，销毁后仍可能返回悬空对象

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` |
| 行号 | `85`; `268-300`; `682-690`; `746-753`; `455-456`; `59-60`; `101-113`; `5263-5267` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 A-04，并补充当前 owner 差异：`GAmbientWorldContext` 仍是文件静态 `UObject*`（`AngelscriptEngine.cpp:85`, `268-300`），`TryGetCurrentWorldContextObject()` 与 `GetAmbientWorldContext()` 会直接把这条裸指针暴露给调用方（`682-690`）。同时 `AssignWorldContext()` 还会把同一个 `UObject*` 写进 `FAngelscriptEngine::WorldContextObject`（`746-753`），但 module 级主引擎是由 `FAngelscriptRuntimeModule::OwnedPrimaryEngine` 以 `TUniquePtr<FAngelscriptEngine>` 持有（`AngelscriptRuntimeModule.h:59-60`），不是 GC 可遍历 owner。后续 `UAngelscriptGameInstanceSubsystem::GetCurrent()` 与异常打印都继续消费这些 world-context 指针（`AngelscriptGameInstanceSubsystem.cpp:101-113`, `AngelscriptEngine.cpp:5263-5267`）。 |
| 根因 | RuntimeCore 把“当前 world context”拆成两套存储：一套是完全绕过 GC 的全局裸静态指针，一套是挂在 `FAngelscriptEngine` 里的 `UPROPERTY()`；但 `FAngelscriptEngine` 既可能被 `UGameInstanceSubsystem` 以 `UPROPERTY()` 持有，也可能被 runtime module 以 `TUniquePtr` 持有，导致同一字段在不同 owner 下具备不同 GC 合同。 |
| 影响 | 一旦 world teardown、PIE 切换或临时 context object 被回收，RuntimeCore 仍可能从 ambient 路径或 module-owned engine 路径返回失效 `UObject*`。后续 subsystem 解析、`GetWorldFromContextObject()`、屏幕异常提示和任何依赖 `TryGetCurrentWorldContextObject()` 的 bind 都会建立在悬空对象上，故障形态包括错误 world 解析、非确定性崩溃和回收后访问。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 world-context 存储统一改为“默认弱引用 + 只有 GC-aware owner 才能保强引用”，消除 module/runtime 路径上的裸 `UObject*` 缓存。 |
| 具体步骤 | 1. 将 `GAmbientWorldContext` 从裸 `UObject*` 改成 `TWeakObjectPtr<UObject>`，并新增单一读入口，例如 `ResolveAmbientWorldContext()`，在所有读取点统一做 `IsValid()` 校验。 2. 将 `FAngelscriptEngine::WorldContextObject` 也改为 `TWeakObjectPtr<UObject>`；对于确实需要强持有的 subsystem owner，不再依赖 `FAngelscriptEngine` 内部 `UPROPERTY()`，而是把强引用放到 `UAngelscriptGameInstanceSubsystem` 这种 GC-aware 宿主上，再由 resolver 显式传入 engine。 3. 在 `AssignWorldContext()`/`TryGetCurrentWorldContextObject()`/`UAngelscriptGameInstanceSubsystem::GetCurrent()`/`LogAngelscriptException()` 全部改走新的 resolve helper，读不到有效对象时稳定返回 `nullptr`，禁止把失效 world 继续传给 `GetWorldFromContextObject()` 或 `PrintString()`。 4. 审查 runtime module fallback tick 路径，禁止 module-owned `TUniquePtr<FAngelscriptEngine>` 长期保存强 world context；若 tick 或 reload 需要 world，应让调用点显式提供 `UWorld*`/`UObject*`，而不是依赖 engine 内缓存。 5. 新增回归：构造临时 world-context 对象并建立 current/ambient 绑定，销毁或触发 GC 后断言 `TryGetCurrentWorldContextObject()`、`GetAmbientWorldContext()`、`UAngelscriptGameInstanceSubsystem::GetCurrent()` 均返回空且不会崩溃。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 把 world-context 从强/裸引用切成弱引用后，少数调用点可能暴露出此前被悬空指针掩盖的“没有 world 也继续运行”的旧假设，需要同步补空值分支。 |
| 前置依赖 | 建议与 Issue-01 配套推进；先收口 teardown 同步，再统一 world-context 存储语义。 |
| 验证方式 | 新增 GC/销毁回归，覆盖 ambient 路径、module-owned engine 路径和 subsystem 路径；并在异常打印与 subsystem lookup 上验证失效 world context 只会返回空，不会继续解引用。 |

### Issue-05：engine 初始化注册的全局 delegate 和 coverage hook 没有解绑，销毁后保留悬空回调

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.h` |
| 行号 | `1460-1462`; `1628-1639`; `2201-2218`; `1132-1252`; `22-28`; `43-52` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 中关于 delegate 泄漏与 coverage hook 的多条发现。`Initialize_AnyThread()` 会创建 `CodeCoverage`（`AngelscriptEngine.cpp:1460-1462`），随后向 `FCoreDelegates::OnPostEngineInit` 注册两个 `AddLambda([&](){ ... })`，并向 `FCoreDelegates::OnGetOnScreenMessages` 注册 `AddRaw(this, &FAngelscriptEngine::GetOnScreenMessages)`（`1628-1639`, `2201-2218`）。`FAngelscriptCodeCoverage::AddTestFrameworkHooks()` 还会继续把 `this` 以 `AddRaw` 方式挂到 automation controller 的 `OnTestsAvailable()` / `OnTestsComplete()`（`AngelscriptCodeCoverage.cpp:22-28`, `AngelscriptCodeCoverage.h:43-52`）。但 `Shutdown()` 从 `1132` 到 `1252` 只释放 script-engine、context、package 和 shared state，没有保存任何 `FDelegateHandle`，也没有删除 `CodeCoverage` 或解除这些注册。 |
| 根因 | RuntimeCore 的初始化阶段把“接入全局 delegate / 外部控制器”当成一次性副作用处理，没有把这些注册纳入 engine 自身的生命周期状态；结果 teardown 只回收本地资源，不回收外部事件面。 |
| 影响 | engine 一旦销毁，后续 `OnGetOnScreenMessages`、`OnPostEngineInit`、`OnCompletedInitialScan` 或 automation controller 广播都仍可能回调到已释放的 `FAngelscriptEngine` / `FAngelscriptCodeCoverage`。即使暂时不崩溃，多次创建/销毁 engine 也会叠加重复测试发现、重复 coverage hook 和重复 on-screen message 采集，形成跨实例污染。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为所有外部 delegate 注册建立显式 owner 句柄和 teardown 对称路径，禁止 fire-and-forget 式全局订阅。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中新增成员句柄，例如 `FDelegateHandle PostEngineInitCoverageHandle`、`PostEngineInitDiscoveryHandle`、`OnScreenMessagesHandle`，并把当前 `AddLambda` / `AddRaw` 改成保存句柄的注册形式。 2. 将两个 `OnPostEngineInit` lambda 改为捕获 `TWeakPtr<FAngelscriptEngineLifetimeToken>` 或等价 weak token，而不是 `[&]` 隐式引用当前实例；对于 `AssetManager->CallOrRegister_OnCompletedInitialScan(...)` 的 nested lambda，同样通过 weak token 判活，避免 engine 已销毁后继续调用 `DiscoverTests()`。 3. 扩展 `FAngelscriptCodeCoverage`：增加 `FDelegateHandle TestsAvailableHandle`、`TestsCompleteHandle` 与 `RemoveTestFrameworkHooks()`，并在 `FAngelscriptEngine::Shutdown()` 中显式调用解绑后 `delete CodeCoverage; CodeCoverage = nullptr;`。 4. 在 `Shutdown()` 的最前段先撤销所有 delegate / hook，再开始释放 script engine、package、shared state 和 lifetime token，确保 teardown 后不会再有外部事件回进来。 5. 新增回归：构造并销毁 full engine 后手动触发 `OnGetOnScreenMessages` 或 automation controller 相关广播，断言不会命中旧实例；再创建第二台 engine，断言测试发现和 coverage hook 只注册一份。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | delegate 拆句柄后会暴露出一些 late-init 路径此前其实依赖“重复注册也能凑合工作”的旧行为，尤其是 test discovery 和 coverage hook 的触发时机需要同步梳理。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 destroy/recreate full engine 的回归，配合手动广播 `OnGetOnScreenMessages` 与 automation controller 事件，验证无悬空回调、无重复回调、`CodeCoverage` 在 teardown 后被正确释放。 |

### Issue-06：hot reload checker thread 没有 owner/stop/restart 合同，错误恢复后可能永久失效

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `1618-1620`; `1658-1700`; `2109-2120`; `2194-2195`; `2729-2779`; `1132-1252`; `406-410`; `554-555` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 中 A-05、B-04、以及编译失败恢复相关发现。engine 只用 `bUseHotReloadCheckerThread` / `bWaitingForHotReloadResults` / `bHotReloadThreadStarted` 三个裸状态位描述后台 watcher（`AngelscriptEngine.h:406-410`, `554-555`）。`StartHotReloadThread()` 直接 `FRunnableThread::Create(new FAngelscriptHotReloadThread(), ...)`，既不保存线程句柄，也不保存 worker 实例（`AngelscriptEngine.cpp:1658-1700`）；线程入口又不是绑定创建它的 engine，而是在 worker thread 里重新执行 `FAngelscriptEngine::Get()`（`1680`），让后台线程依赖全局 current-engine 解析。`Shutdown()` 全段没有停止线程、等待退出或复位这些状态（`1132-1252`）。更糟的是，编译失败对话框会临时把 `bUseHotReloadCheckerThread = true` 并提前启动线程（`2109-2120`），成功后又把它恢复成旧值（`2194-2195`）；如果旧值是 `false`，worker 会退出，但 `bHotReloadThreadStarted` 已经被永久置成 `true`，后面正常启动阶段再到 `1618-1620` 时不会重新建线程。与此同时 `CheckForHotReload()` 还会持续把 `bWaitingForHotReloadResults` 置回 `true`（`2773-2778`），没有活线程时这个等待位就失去清零者。 |
| 根因 | hot reload watcher 被实现成 detached 线程加几个分散的布尔标志，而不是 engine 生命周期中的显式子对象；线程的 owner、启动原因、停止握手和 restart 语义都没有被建模。 |
| 影响 | 运行期会出现三类故障：1. shutdown 后后台线程仍可能继续访问已销毁或错误的 engine；2. 多 engine 场景下线程可能修改到并非启动者的实例；3. 编译失败恢复后，后续 hot reload 检测可能永久不再启动，甚至卡在 `bWaitingForHotReloadResults = true` 的假等待状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 hot reload watcher 改造成 engine-owned 可停止组件，显式保存线程句柄并绑定固定 owner engine。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中新增明确的线程所有权成员，例如 `TUniquePtr<FAngelscriptHotReloadThread> HotReloadWorker`、`FRunnableThread* HotReloadThreadHandle`，并提供 `StartHotReloadWatcher()` / `StopHotReloadWatcher()` 两个对称 helper。 2. 让 `FAngelscriptHotReloadThread` 构造时接收 `FAngelscriptEngine* Owner` 或 `TWeakPtr<FAngelscriptEngineLifetimeToken>`，worker 内只访问这台 owner engine，不再调用 `FAngelscriptEngine::Get()`。 3. 在 `Shutdown()` 中先调用 `StopHotReloadWatcher()`：把 stop 标志和 `bUseHotReloadCheckerThread` 关闭，唤醒并 `WaitForCompletion()`，随后释放 thread/worker 句柄，并把 `bHotReloadThreadStarted`、`bWaitingForHotReloadResults` 复位。 4. 将“编译失败对话框期间的临时轮询线程”和“正常 development-mode watcher”分成两套显式 reason，至少要保证前者退出后不会污染后者的 start gate；如果不拆两套线程，也要通过引用计数或状态枚举区分 `TemporaryDialogPolling` 与 `PersistentRuntimeWatching`。 5. 补充测试 seam：给 `FAngelscriptHotReloadTestAccess` 暴露 watcher 状态，新增回归验证 `start -> shutdown` 会清干净线程状态，以及“失败对话框先启动，再进入正常 dev-mode”时 watcher 仍可被重新建立并正确清零 `bWaitingForHotReloadResults`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | L |
| 风险 | 线程生命周期收口会改变现有 hot reload 触发时序，若 stop/start 处理不严谨，可能把原来“偶然可用”的轮询节奏改坏，因此需要 focused regression 覆盖启动失败、正常轮询和 shutdown 三种状态。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 watcher 生命周期回归，验证 shutdown 后线程句柄为空、`bHotReloadThreadStarted == false`、`bWaitingForHotReloadResults == false`；并验证 compile-error 临时 watcher 结束后，正常 dev-mode watcher 仍能重新启动并继续清空等待位。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-05 | Defect | 立即修复，先收口全局 delegate / coverage hook 的 teardown，对多实例和销毁后的 UAF 风险止血 |
| P1 | Issue-06 | Defect | 紧随其后，重建 hot reload watcher 的 owner/stop/restart 合同，避免后台线程继续污染实例状态 |
| P1 | Issue-04 | Defect | 与 Issue-01 配套推进，统一 world-context 的 GC/owner 语义，消除悬空对象解析路径 |

---

## 发现与方案 (2026-04-08 12:43)

### Issue-07：full engine 销毁时只丢失 package 指针，不撤销 root set，包级状态会跨生命周期残留

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `878-882`; `1382-1386`; `1213-1246` |
| 问题 | `InitializeForTesting()` 与 `Initialize_AnyThread()` 都用 `RF_Public | RF_Standalone | RF_MarkAsRootSet` 创建 `AngelscriptPackage` / `AssetsPackage`（`878-882`, `1382-1386`）。但 `Shutdown()` 在 owner release 路径只把两个成员置空（`1245-1246`），没有 `RemoveFromRoot()`、没有清 root flag，也没有把 package 销毁收口到 deferred clone release 分支。由于 clone wrapper 还会通过 `AdoptSharedStateFrom()` 共享这两个 package 指针，这意味着包对象被显式放进 root set 后，再也没有对称释放。 |
| 根因 | RuntimeCore 把 `UPROPERTY()` 置空误当成 package teardown，而忽略了对象创建时已经升级成 root set；同时 package 生命周期没有像 `ScriptEngine` / `PrimaryContext` 那样进入 owner shared-state release 合同。 |
| 影响 | full engine 即使走完 `Shutdown()`，`/Script/Angelscript` 与 `/Script/AngelscriptAssets` 以及挂在其下的对象仍会跨 engine epoch 存活，导致多轮 create/destroy、测试隔离和热重载后的命名空间都无法回到干净基线。长期看这会表现为内存泄漏、旧脚本对象残留，以及后续初始化误复用上一轮包级状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 package root-set 释放并入 owner final release 路径，保证 create/shutdown 对称。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 私有区新增 package teardown helper，例如 `ReleaseOwnedPackages()`，统一处理 `AngelscriptPackage` 和 `AssetsPackage` 的 `IsRooted()` / `RemoveFromRoot()` / 成员清空。 2. 将该 helper 挂到 owner 最终释放路径，而不是普通 wrapper 清理路径：优先放进 `ReleaseOwnedSharedStateResources(...)` 之后或与 `bShouldReleaseOwnedEngine` / `bPendingOwnerRelease` 同级的最终 owner release 分支，确保 clone 仍存活时不会提前撤根。 3. 对 helper 加上判空与幂等保护，避免 testing/full engine 路径和 deferred clone release 路径重复调用时再次触碰已释放 package。 4. 审查所有依赖 `GetPackage()` / `AssetsPackage` 的 teardown 后调用点，确保在 owner shutdown 之后不会再假定 package 仍可解析。 5. 在 `AngelscriptEngineIsolationTests.cpp` 增加 focused regression：创建 full engine，保存 `TWeakObjectPtr<UPackage>`，销毁 engine 后断言 package 不再 rooted，并在一次 GC 后验证弱引用失效或至少不再处于 root set。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果某些旧路径隐式依赖“engine shutdown 后 package 仍常驻”，撤根后会暴露出隐藏的 teardown 顺序问题，需要同步修正这些调用点。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 full engine create/destroy focused regression，断言 package 在 shutdown 后不再 rooted；再跑现有 multi-engine / engine isolation 回归，确认 clone defer-release 场景不会因 package 提前撤根而回归。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-07 | Defect | 尽快修复，先让 package 生命周期与 owner release 对称，避免状态跨 epoch 残留 |

---

## 发现与方案 (2026-04-08 12:44)

### Issue-08：threaded initialize 通过改写全局 `GameThreadTLD` 做跨线程传递，和 game thread 泵任务并发后会踩坏 TLS 合同

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `821-848`; `1179-1182`; `1320-1323`; `1565-1566`; `1778-1782`; `141` |
| 问题 | `PreInitialize_GameThread()` 把静态 `GameThreadTLD` 绑定到真正的 game-thread TLS（`1320-1323`），但 `Initialize()` 的 threaded 分支又在 worker `AsyncTask` 中把这根全局静态指针临时改写成 `asCThreadManager::GetLocalData()` 返回的 worker TLS，并把 `primaryContext` 从真实 game-thread TLS 搬过去（`821-840`）。主线程 meanwhile 只靠 `volatile bool bInitializationDone` 轮询完成态，并在等待期间持续执行 `ProcessThreadUntilIdle(ENamedThreads::GameThread)`（`843-847`）。同一静态入口随后还被 `Shutdown()`、`FAngelscriptGameThreadContext` 和 `Initialize_AnyThread()` 继续直接消费（`1179-1182`, `1565-1566`, `1778-1782`）。这意味着只要等待窗口内有任何 game-thread 代码再次读取 `GameThreadTLD`，它拿到的就不是 game thread 自己的 TLS。 |
| 根因 | RuntimeCore 用进程级静态 `GameThreadTLD` 承担了两个彼此冲突的职责：一边表示“game thread 的固定 TLS 入口”，一边在 threaded initialize 里把它当作跨线程 scratch pointer。与此同时，完成同步只靠 `volatile` 自旋，没有建立真正的 happens-before。 |
| 影响 | 这条初始化路径存在明确的数据竞争和 TLS 语义破坏：主线程可能在泵任务时读到 worker TLS，把本应属于 game thread 的 `primaryContext`/context acquisition 绑定到错误线程；也可能因为 `volatile` 不是同步原语而在不同平台上出现可见性问题。故障形态包括初始化窗口内的 context 污染、错误释放，以及低概率卡死或不稳定的 teardown。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 保持 `GameThreadTLD` 只代表真正的 game-thread TLS，worker 初始化改用显式局部状态传递，不再改写全局静态指针。 |
| 具体步骤 | 1. 把 threaded 初始化需要的 TLS/primary-context 输入抽成显式 helper 参数，例如让 `Initialize_AnyThread()` 接收 worker-local `asCThreadLocalData*` 与 seed context，或新增局部 RAII 封装来保存 worker 侧临时状态，而不是直接改写 `FAngelscriptEngine::GameThreadTLD`。 2. 将 `GameThreadTLD` 的赋值严格收敛到 `PreInitialize_GameThread()`，后续只允许 game-thread 路径读取/写入；worker 线程若需要 `primaryContext`，改为复制到自己的局部 TLS 变量。 3. 用真实同步原语替代 `volatile bool` 自旋，例如 `FEvent`、`TFuture`/`TPromise` 或至少 `TAtomic<bool>` 配合明确的 completion fence；若仍需主线程泵任务，也必须保证泵任务期间全局 `GameThreadTLD` 不会指向 worker 数据。 4. 审查所有直接读取 `GameThreadTLD` 的路径，给 `FAngelscriptGameThreadContext`、`Shutdown()` 等 game-thread-only 入口补上线程前置条件或显式注释，避免未来再次把 worker 状态塞进同一入口。 5. 在 `AngelscriptEngineIsolationTests.cpp` 增加 threaded initialize focused regression：强制走 threaded 路径，并通过 test seam 在初始化等待窗口读取 `GameThreadTLD`/`primaryContext`，断言其始终保持 game-thread 绑定；同时验证初始化完成后 `GameThreadTLD` 与 usable context 状态一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | L |
| 风险 | 线程化初始化的现有时序非常脆弱，重构后可能暴露出之前被全局静态入口掩盖的依赖，尤其是初始 compile / context 创建的线程归属需要一并确认。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 threaded initialize 回归，断言等待窗口内 `GameThreadTLD` 不会漂移到 worker TLS；再跑 engine isolation、multi-engine 与 hot reload 相关回归，确认 context 获取和 teardown 没有引入新串线。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-08 | Defect | 与 hot reload/多引擎修复并列优先，先收掉 threaded initialize 的 TLS/data-race 风险 |

---

## 发现与方案 (2026-04-08 12:46)

### Issue-09：`InitializeOverrideForTesting` 注入的 full engine 没有 owner 记录，`ContextStack` 和实例都无法对称回收

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 行号 | `145-151`; `27-39`; `174-182`; `776-779`; `59-65`; `339-344`; `366-370`; `392-397` |
| 问题 | `FAngelscriptRuntimeModule::InitializeAngelscript()` 在 `InitializeOverrideForTesting` 分支只把 callback 返回的 `OverrideEngine` 压入 `FAngelscriptEngineContextStack` 然后直接返回（`145-151`），没有把这台 engine 存进任何 owner 字段。后续 `ShutdownModule()` 与 `ResetInitializeStateForTesting()` 只会处理 `OwnedPrimaryEngine`（`27-39`, `174-182`），对 override engine 完全无感。当前自动化又明确通过 `CreateTestingFullEngine(...).Release()` 把 `TUniquePtr` 释放成裸指针交给 override 路径（`339-344`, `366-370`, `392-397`），因此这台 full engine 既不会被 module reset/shutdown 回收，也不会被测试辅助 `ResetToIsolatedState()` 清走，因为后者调用的 `DestroyGlobal()` 目前是空实现（`776-779`, `59-65`）。 |
| 根因 | test override 入口只建模了“把某台 engine 变成当前 engine”，没有建模“谁拥有它、谁负责 pop/shutdown/reset”。owner 生命周期仍然只围绕 `OwnedPrimaryEngine`，而 override 分支绕过了这套合同。 |
| 影响 | 每次走 override 初始化都会把 full engine 永久留在 `ContextStack` 和堆上，后续自动化即使调用 reset/shutdown 也可能继续解析到上一轮遗留的 engine。结果是测试隔离失真、生命周期串线、悬空 current-engine 解析，以及更隐蔽的“测试仍然通过，但运行在旧实例上”的假阳性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 给 testing override 建立显式 owner 字段和对称 teardown，禁止再把裸指针 fire-and-forget 地塞进 `ContextStack`。 |
| 具体步骤 | 1. 将 `InitializeOverrideForTesting` 的签名从 `TFunction<FAngelscriptEngine*()>` 改为返回显式所有权，例如 `TFunction<TUniquePtr<FAngelscriptEngine>()>`；在 `FAngelscriptRuntimeModule` 内新增专用 owner 字段（如 `OwnedOverrideEngine`），或复用 `OwnedPrimaryEngine` 但明确记录“当前是 override owner”。 2. 在 `InitializeAngelscript()` 的 override 分支中，把返回的 engine 保存到 owner 字段后再 `Push()`；禁止调用方通过 `.Release()` 交出裸指针。 3. 扩展 `ShutdownModule()` 与 `ResetInitializeStateForTesting()`，统一处理 normal owner 和 override owner：按同一顺序 `Pop -> Shutdown/Reset -> 清 owner 字段`，确保 `ContextStack` 与实例生命周期对称。 4. 处理测试辅助的遗留假设：要么让 `DestroyGlobal()` 在测试构建下真正清理当前 module-owned/override-owned engine，要么让 `ResetToIsolatedState()` 改为调用新的显式 cleanup 入口，避免继续依赖空实现。 5. 在 `AngelscriptSubsystemTests.cpp` 补两条 focused regression：一条验证 override `InitializeAngelscript()` 后执行 `ResetInitializeStateForTesting()` 会清空 `ContextStack`；另一条验证连续两次 override 初始化不会解析到前一轮 engine，也不会泄漏实例。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 预估工作量 | M |
| 风险 | 调整 override 签名会影响现有测试夹具，需要同步更新所有调用点；但这是必要代价，否则 owner 合同仍然不明确。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 override lifecycle 回归，断言 reset/shutdown 后 `FAngelscriptEngineContextStack::IsEmpty()`，并验证第二次 override 初始化解析到的是新实例；同时运行现有 `Angelscript.CppTests.Subsystem.RuntimeModule.*` 用例确认行为保持稳定。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-09 | Defect | 在继续扩充 RuntimeModule 自动化前先修，先补齐 override owner/teardown 合同，避免测试基线继续被旧实例污染 |

---

## 发现与方案 (2026-04-08 12:47)

### Issue-10：`ContextStack` 是进程级全局栈，而上下文池是 `thread_local`，跨线程 current-engine 解析模型自相矛盾

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `71`; `87`; `391-415`; `718-733`; `1680-1689`; `1812-1857`; `1890-1896` |
| 问题 | RuntimeCore 一边把 `GAngelscriptContextPool` 设计成 `thread_local`（`87`），一边却把 `GAngelscriptEngineContextStack` 实现成没有任何同步的进程级全局 `TArray`（`71`, `391-415`），而 `TryGetCurrentEngine()` 又把这份全局栈当成 current-engine 的首选来源（`718-733`）。这个全局 resolver 还被直接用于后台路径：hot reload thread 在 worker 中调用 `FAngelscriptEngine::Get()`（`1680-1689`），`FAngelscriptPooledContextBase::Init()` / 析构返回路径会在 `DesiredScriptEngine == nullptr` 或归还 global pool 时再次走 `TryGetCurrentEngine()` / `Get()`（`1812-1857`, `1890-1896`）。结果是“线程自己的 context 池”却要依赖“别的线程也可改写的 current-engine 栈”来决定该向哪台 engine 借/还 context。 |
| 根因 | `ContextStack` 同时被拿来表达“当前调用作用域”和“全局当前主引擎”，但它的存储模型既不是 `thread_local`，也不是显式 owner registry；这与线程本地 context pool、后台 watcher、以及 pooled context 的跨线程用法天然冲突。 |
| 影响 | 只要 game thread 正在切换 scope，后台线程或其他工作线程就可能通过同一份全局栈解析到错误的 engine，再把 context 从错误的 `GlobalContextPool` 借出或归还进去，或者让后台 watcher 操作到并非其 owner 的实例。即使短期不崩，这也会持续制造难定位的跨线程串线、错误 pool 归属和 current-engine 非确定性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `ContextStack` 收回“线程内执行作用域”语义，跨线程路径全部改为显式传递 owner engine。 |
| 具体步骤 | 1. 将 `GAngelscriptEngineContextStack` 改为 `thread_local` 存储，或引入等价的线程局部 execution-context 容器，明确它只服务 `FAngelscriptEngineScope` 这类线程内 RAII 作用域。 2. 停止让 subsystem/module owner 以及后台线程依赖这份栈充当全局注册表；长期 owner 应走独立 resolver/registry，后台任务则必须显式持有 `FAngelscriptEngine*` 或 `asIScriptEngine*`。 3. 审查 `FAngelscriptPooledContextBase::Init()`、析构和 hot reload/debug 线程，去掉 `DesiredScriptEngine == nullptr` 时对 `TryGetCurrentEngine()` / `Get()` 的隐式回退；要求这些路径由调用方提供明确 engine，或者在进入后台任务前先捕获 owner token。 4. 保留 `ContextStack::Push/Pop` 的顺序断言，但把它限制在单线程语义内；如果仍需跨线程诊断，新增只读快照/日志接口，不再复用同一容器作为共享状态。 5. 在 `AngelscriptEngineIsolationTests.cpp` 增加并发 focused regression：game thread 建立 scoped engine 后启动后台任务，验证 worker 默认不会读取到另一线程的 scope；再验证显式传入 owner engine 时，context 借还只命中该 owner 的 `GlobalContextPool`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | L |
| 风险 | 这是 resolver 基础设施调整，可能影响大量依赖 `TryGetCurrentEngine()` 的隐式调用点；需要分阶段迁移并在 worker 路径上先加显式 owner 参数，避免一次性改动过大。 |
| 前置依赖 | 建议与 Issue-03、Issue-06、Issue-08 统筹，先明确 resolver 与线程边界，再拆后台线程和 pooled context 的隐式回退。 |
| 验证方式 | 新增跨线程 resolver 回归，验证 worker 默认不继承其他线程的 scope；再跑 multi-engine、subsystem、hot reload focused regression，确认 context 借还和 watcher 行为不再依赖全局共享栈。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-10 | Architecture | 在修复 Issue-06/08 后立即推进，把 current-engine 解析从共享全局栈收回到线程内语义 |

---

## 发现与方案 (2026-04-08 12:53)

### Issue-11：`RuntimeModule` 复用现有 engine 时仍会二次执行完整初始化

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `156-164`; `819-857`; `1320-1324`; `1382-1423`; `1565-1640` |
| 问题 | `FAngelscriptRuntimeModule::InitializeAngelscript()` 在发现已有 `CurrentEngine` 时仍直接调用 `CurrentEngine->Initialize()`（`AngelscriptRuntimeModule.cpp:156-159`）。但 `FAngelscriptEngine::Initialize()` 本身没有任何 `Engine != nullptr` 的重入保护；它会再次执行 `PreInitialize_GameThread()` 重新 `asCreateScriptEngine()`（`AngelscriptEngine.cpp:1320-1324`），随后重新创建 `/Script/Angelscript` 与 `/Script/AngelscriptAssets` 包、重复注册 message/context callback，并重新创建 `primaryContext`、hot-reload runner 与 on-screen delegate（`1382-1423`, `1565-1640`）。 |
| 根因 | RuntimeModule 把“拿到当前 engine”误当成“拿到一个尚未 bootstrap 的 wrapper”，没有区分 adopt existing engine 与 create-and-initialize new engine 两种生命周期分支；同时 `FAngelscriptEngine::Initialize()` 缺少幂等/防重入合同。 |
| 影响 | 只要 subsystem、测试 scope 或其他入口先创建过 full engine，后续 RuntimeModule 再启动就会把同一个 wrapper 重新初始化一遍，覆盖旧 `asCScriptEngine*`、重复注册全局回调，并让旧 VM / package / context 失去正常 `Shutdown()` 收口机会。这会放大悬空回调、资源泄漏和 shared-state 串线风险。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 RuntimeModule 的“adopt existing engine”与“创建并初始化新 engine”拆成两个明确分支，并在 `FAngelscriptEngine::Initialize()` 上加防重入护栏。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 增加明确的初始化状态判断，至少以 `Engine != nullptr` 为基础建立 `IsInitialized()`/`HasScriptEngine()` 级别的实例合同。 2. 修改 `FAngelscriptRuntimeModule::InitializeAngelscript()`：若 `TryGetCurrentEngine()` 返回的实例已经完成 bootstrap，则只记录/采用该实例，不再调用 `Initialize()`；只有 `OwnedPrimaryEngine = MakeUnique<FAngelscriptEngine>()` 的自建路径才执行完整初始化。 3. 在 `FAngelscriptEngine::Initialize()` 开头加入 `checkf(Engine == nullptr, ...)` 或等价的 fail-fast/early-return，阻止未来其它入口再次重跑 bootstrap。 4. 把当前混在 `Initialize()` 里的“创建 VM/包/回调”和“共享状态收口”整理成可复用 helper，确保 adopt 分支只做 owner 同步，不触碰底层 VM。 5. 在 RuntimeModule / subsystem 自动化中新增回归：先用 subsystem 或测试 helper 创建一台已初始化 engine，再调用 `InitializeAngelscript()`，断言 `GetScriptEngine()` 指针不变、不会新增第二套 package/context，并验证 shutdown 后只发生一次 owner 释放。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | `Initialize()` 目前承担了大量副作用，直接加 fail-fast 可能暴露既有测试夹具对重复初始化的隐式依赖；需要先把 adopt 分支调通，再打开强校验。 |
| 前置依赖 | 无 |
| 验证方式 | 新增“existing engine + RuntimeModule startup”回归，验证底层 `asIScriptEngine*`、package 指针和 delegate 数量不发生二次初始化；随后跑 RuntimeCore/subsystem focused tests，确认 create/adopt/shutdown 三条路径都能对称收口。 |

### Issue-12：`GameInstanceSubsystem` 生命周期没有把 `UWorld` 同步到 engine world context

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp` |
| 行号 | `12-30`; `55-58`; `682-690`; `746-754`; `275`; `73-80`; `89-93`; `66-70`; `134-138` |
| 问题 | `UAngelscriptGameInstanceSubsystem::Initialize()` 明确拿到了 `UGameInstance` 生命周期，但整个函数只处理 `PrimaryEngine` 选择、`ContextStack::Push` 和 `OwnedEngine.Initialize()`，没有任何一处调用 `AssignWorldContext(GetGameInstance())`、`AssignWorldContext(GetWorld())` 或 `FAngelscriptEngineScope`（`AngelscriptGameInstanceSubsystem.cpp:12-30`）。与此同时，`TryGetCurrentWorldContextObject()` 只要能解析到 current engine，就直接返回该 engine 的 `WorldContextObject`（`AngelscriptEngine.cpp:682-690`; `AngelscriptEngine.h:275`），而 `AssignWorldContext()` 才是唯一会把值写进 `WorldContextObject` 的公共入口（`746-754`）。由于 subsystem owner 路径会把 `OwnedEngine` 长期压在 `ContextStack` 栈顶（`21-22`），稳态下 world 查询优先命中的就是这台尚未写入 world context 的 engine。直接受影响的调用点已经存在于运行时：`Bind_Subsystems.cpp` 的 `UGameInstanceSubsystem::Get()` / `UWorldSubsystem::Get()` 都依赖 `TryGetCurrentWorldContextObject()` 解析 world（`73-80`, `89-93`），`UnitTest.cpp` 的 `AdvanceTime` / `SetIsServer` 在取不到 world 时会直接失败（`66-70`, `134-138`）。 |
| 根因 | subsystem 集成只建模了“哪台 engine 归谁持有、谁来 tick”，没有把 `UGameInstance/UWorld` 到 `FAngelscriptEngine::WorldContextObject` 的同步作为正式生命周期步骤；结果长期 owner 状态和 world-context 状态分裂成两套互不联动的状态机。 |
| 影响 | subsystem 自有 engine 即使已经初始化完成，也可能在稳定运行状态下持续返回空 world context，使一批依赖当前 world 的核心绑定和测试辅助函数随机失效。更糟的是，这类问题只会在没有额外临时 scope 帮忙写入 world context 的场景下出现，表现为“有时能用、有时突然没有 world”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 world-context 同步提升为 `UAngelscriptGameInstanceSubsystem` 初始化/反初始化合同的一部分，而不是依赖临时 scope 偶然写入。 |
| 具体步骤 | 1. 在 `UAngelscriptGameInstanceSubsystem::Initialize()` 确定 `PrimaryEngine` 后，立即以 `GetGameInstance()` 或 `GetWorld()` 为来源调用显式 helper（例如 `PrimaryEngine->SetPersistentWorldContext(...)`，或在受控 scope 内调用 `AssignWorldContext(...)` 后沉淀到实例字段），确保 subsystem owner/adopted 两条路径都能建立基础 world context。 2. 在 `Deinitialize()` 对称清理该 subsystem 安装的 world context，避免继续保留已经失效的 `UGameInstance/UWorld`。 3. 避免继续把“长期 world context”塞进 ambient 全局指针；若需要兼容旧调用点，应先写 engine-local `WorldContextObject`，再由单独 helper 决定是否同步 ambient。 4. 为 `Bind_Subsystems`、`UnitTest` 和至少一个 world-sensitive bind（例如 `Bind_SystemTimers` 或 `Bind_UWorld`）增加真实 subsystem 初始化回归，断言不借助额外 `FAngelscriptEngineScope` 时也能稳定解析到 world。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | world context 同步时机如果处理不当，可能与现有 ambient-world 恢复逻辑互相覆盖；需要把“长期 owner context”和“临时调用 scope context”拆开，防止修复后引入新的恢复顺序错误。 |
| 前置依赖 | 建议结合 Issue-01、Issue-04 一起推进，统一 world-context 的 owner、GC 与恢复语义。 |
| 验证方式 | 新增真实 `UGameInstanceSubsystem` 初始化测试，验证 `TryGetCurrentWorldContextObject()`、`UGameInstanceSubsystem::Get()`、`UWorldSubsystem::Get()`、`AdvanceTime()` 在 subsystem 稳态下都能拿到 world；再验证 `Deinitialize()` 后 world context 被正确清空或恢复。 |

### Issue-13：生产 tick 边界没有建立 `FAngelscriptEngineScope`，`Tick()` 内部仍依赖隐式 current-engine

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `81-86`; `186-195`; `437-455`; `635-652`; `718-765`; `2794-2840`; `5012-5023` |
| 问题 | RuntimeCore 已经提供了标准 RAII 入口 `FAngelscriptEngineScope`，其职责就是在构造时 `Push` 当前 engine 并同步 ambient/world context，析构时再恢复（`AngelscriptEngine.cpp:437-455`; `AngelscriptEngine.h:635-652`）。但两个正式 tick 边界都没有使用它：`UAngelscriptGameInstanceSubsystem::Tick()` 与 `FAngelscriptRuntimeModule::TickFallbackPrimaryEngine()` 只是裸调用 `PrimaryEngine->Tick()` / `CurrentEngine->Tick()`（`AngelscriptGameInstanceSubsystem.cpp:81-86`; `AngelscriptRuntimeModule.cpp:186-195`）。与此同时，`FAngelscriptEngine::Tick()` 内部会进入 hot reload / debug / diagnostics 路径（`2794-2840`），而编译消息回调 `LogAngelscriptError()` 又直接通过 `FAngelscriptEngine::Get()` 反查“当前 engine”并加锁写诊断（`5012-5023`）。`Get()` 自己也明确把“缺少 `FAngelscriptEngineScope`”记录为失败原因（`718-765`）。 |
| 根因 | tick 调用边界被当成“纯成员函数”处理，但 `Tick()` 触发的副作用并不是纯实例内逻辑，它仍依赖全局 current-engine resolver；生产入口没有像 `Initialize()` 那样在外层显式建立 scope。 |
| 影响 | 只要被 tick 的 engine 不是长期残留在 `ContextStack` 栈顶，`Tick()` 内部任何走到 `FAngelscriptEngine::Get()` 的路径都可能解析到错误实例，或者在 current-engine 为空时直接命中 `checkf`。这会让 adopted engine、clone wrapper 和后续的多引擎路径把正确性建立在隐式全局状态上，而不是建立在 tick 调用本身。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“进入 `Tick()` 前必须安装 current-engine scope”固化成 RuntimeCore 的统一合同。 |
| 具体步骤 | 1. 在 `UAngelscriptGameInstanceSubsystem::Tick()` 和 `FAngelscriptRuntimeModule::TickFallbackPrimaryEngine()` 外围都建立 `FAngelscriptEngineScope ScopedTickEngine(*PrimaryEngine, GetTickableGameObjectWorld())` 或等价 helper，确保 tick 周期内 `Get()`、`TryGetCurrentWorldContextObject()` 和 diagnostics 都绑定到被 tick 的那台 engine。 2. 若不希望每帧手写 scope，抽一个共享 helper，例如 `FAngelscriptScopedTickContext`，把 engine/world 提取、push/pop 和 world-context 恢复放在一处，避免 subsystem/module 继续复制逻辑。 3. 审查所有可能在 `Tick()` 内异步触发的 callback，确认它们不再依赖 ambient world 的偶然残留；必要时把 owner engine 显式透传给 hot reload / diagnostics helper。 4. 为 subsystem adopted-engine、RuntimeModule fallback tick 和 clone/full engine 混跑场景增加回归，断言 tick 期间 `FAngelscriptEngine::Get()` 始终返回正在被 tick 的实例。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 给 tick 补 scope 会改变 current-engine/world-context 的可见性，可能暴露一批此前依赖“ambient 恰好为空/非空”的隐式行为；需要配套回归来锁定新合同。 |
| 前置依赖 | 建议与 Issue-10、Issue-12 配合推进，先明确 tick 周期内的 current-engine 与 world-context 应该来自哪一层 owner。 |
| 验证方式 | 新增 subsystem/runtime tick 回归：在没有预先手工建 scope 的情况下触发 `Tick()`，断言 `FAngelscriptEngine::Get()`、compile diagnostics 和 world-sensitive bind 都落到当前被 tick 的 engine；再跑 multi-engine focused tests，确认 adopted/clone 路径不再因为缺 scope 而串线。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-11 | Defect | 立即修复，先阻断 existing engine 被 RuntimeModule 二次初始化 |
| P1 | Issue-12 | Defect | 紧随其后，把 subsystem owner 生命周期与 world-context 建立合同收口 |
| P1 | Issue-13 | Defect | 在 world-context 合同明确后推进，为所有生产 tick 入口补齐 `FAngelscriptEngineScope` |

---

## 发现与方案 (2026-04-08 13:07)

### Issue-14：subsystem adopted 分支会把临时 `current-engine` 提升为长期 `PrimaryEngine`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `12-25`, `32-45`, `81-85`; `437-506`, `628-647`, `718-733`; `208-210`; `27-38` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `B-13`，并补充生命周期收口方案：`UAngelscriptGameInstanceSubsystem::Initialize()` 会直接把 `FAngelscriptEngine::TryGetCurrentEngine()` 的返回值写进长期成员 `PrimaryEngine`（`AngelscriptGameInstanceSubsystem.cpp:12-25`）。但 `TryGetCurrentEngine()` 的首选来源是 `ContextStack::Peek()`（`AngelscriptEngine.cpp:718-733`），而 `FAngelscriptEngineScope` 明确只是短生命周期 RAII scope，离开作用域就会 `Pop()`（`437-506`）。另一方面，clone wrapper 已经通过 `SourceLifetimeToken`/`GetSourceEngine()` 表达“source 可能失效”的合同（`628-647`; `AngelscriptEngine.h:208-210`），RuntimeModule owner 也会在 `ShutdownModule()` 中直接 `OwnedPrimaryEngine.Reset()`（`AngelscriptRuntimeModule.cpp:27-38`）。subsystem 却没有保存任何 token、owner 句柄或 adopt 合法性标记，后续 `Tick()` / `Deinitialize()` 仍会长期解引用这根裸指针（`AngelscriptGameInstanceSubsystem.cpp:32-45`, `81-85`）。 |
| 根因 | subsystem 把“当前调用现场解析到的 engine”误当成“可被 `UGameInstance` 长期拥有的主引擎”，没有区分 `FAngelscriptEngineScope` 的瞬时作用域、clone/source 的生命周期合同，以及 RuntimeModule/subsystem 自身的持久 owner。 |
| 影响 | 只要 subsystem 初始化发生在临时 `FAngelscriptEngineScope`、clone scope，或随后会被 RuntimeModule reset 的外部 owner 内，它就可能把短生命周期 wrapper 升级成长期状态。后续 `Tick()` / `Deinitialize()` 会在过期 wrapper 上继续调用 `ShouldTick()`、`Shutdown()` 和 world-context 相关逻辑，最终演变成 stale current-engine、错误 owner teardown，甚至 use-after-free。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 subsystem 的 adopt 行为限制为“显式可校验的持久 owner”，禁止直接采信 `ContextStack` 的瞬时解析结果。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 暴露只读生命周期句柄能力，例如 `GetLifetimeTokenWeak()` 或专门的 adoptable descriptor，供长期 owner 保存并在使用前校验；不要继续让外部组件直接长期保存裸 `FAngelscriptEngine*`。 2. 在 `UAngelscriptGameInstanceSubsystem::Initialize()` 新增 adopt 入口，例如 `ResolveAdoptableEngineForSubsystem()`，只接受来自持久 owner registry 的 engine；若当前解析结果仅来自 `ContextStack` scope 或 clone wrapper，则拒绝 adopt，回退到 `OwnedEngine.Initialize()`。 3. 将 `PrimaryEngine` 改为“裸指针 + 生命周期弱句柄”或等价结构，在 `Tick()` / `Deinitialize()` 前先判活；若 owner 已失效，立即清空 adopt 状态并停止继续驱动该实例。 4. 给 RuntimeModule 与 subsystem owner 路径补统一的 detach helper，在 owner reset/shutdown 时主动让下游 adopted subsystem 失效，而不是等裸指针悬空。 5. 在 `AngelscriptSubsystemTests.cpp` 或 `AngelscriptSubsystemOwnershipTests.cpp` 增加真实场景回归：在临时 `FAngelscriptEngineScope` 和 clone scope 中初始化 subsystem，再销毁 scope/source，断言 subsystem 不会继续持有失效 engine。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemOwnershipTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | adopt 合同一旦收紧，现有依赖“在 scope 内借外部 engine 初始化 subsystem”的测试夹具会失效，需要同步改成真实 owner 注册或显式注入。 |
| 前置依赖 | 建议与 Issue-03 的 resolver 拆分同步设计；若需要先止血，可先仅加 adoptable-engine 校验。 |
| 验证方式 | 新增 “scoped engine 初始化 subsystem” 与 “clone source 失效后 subsystem 持续 tick” 两条回归；同时运行现有 subsystem/runtime module 自动化，确认 owner 自建路径保持稳定。 |

### Issue-15：启动期编译错误恢复会把 hot reload checker thread 永久关停

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `1618-1620`; `1658-1699`; `2109-2195`; `2732-2778` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `A-27`，并补充具体修复路径：正式 steady-state hot reload checker 只会在 `InitialCompile()` 之后通过 `bUseHotReloadCheckerThread = bScriptDevelopmentMode && !RuntimeConfig.bIsEditor; StartHotReloadThread();` 启动（`1618-1620`）。但启动期编译失败分支会先保存旧值 `bPreviousUseHotReloadCheckerThread`，再在错误对话框里临时把 `bUseHotReloadCheckerThread = true` 并提前 `StartHotReloadThread()`（`2109-2120`）。`StartHotReloadThread()` 又把 `bHotReloadThreadStarted` 当成一次性闸门，只要置真就永不重置（`1658-1666`）；后台线程本身则在看到 `!Manager.bUseHotReloadCheckerThread` 时立即退出（`1681-1684`）。这意味着当错误对话框结束后恢复旧值 `false`（`2195`），临时线程会退出，而稍后的正式启动阶段再到 `1618-1620` 时，`bHotReloadThreadStarted` 已经阻止了重新创建。后续 `CheckForHotReload()` 仍会把 `bWaitingForHotReloadResults = true`（`2774-2778`），却再也没有存活线程把它清回 `false`（`1686-1689`）。 |
| 根因 | RuntimeCore 把“启动报错期间的临时轮询线程”和“正常运行期的长期 checker thread”复用了同一套布尔状态机，但只实现了一次性启动闸门，没有线程存活性、模式切换和重启语义。 |
| 影响 | 用户在启动报错窗口中修好脚本后，编辑器虽然能继续打开，但后续热重载检查会永久降级为失效状态：`bWaitingForHotReloadResults` 可能持续停留在等待态，新的文件变更不再被后台线程采集，最终表现为“启动恢复成功后脚本再也不会自动热重载”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将“启动期临时 watcher”和“steady-state checker”拆成显式状态机，并把线程句柄纳入可重启的生命周期管理。 |
| 具体步骤 | 1. 为 hot reload checker 引入显式线程所有权，例如 `FRunnableThread* HotReloadThreadHandle` + `TUniquePtr<FRunnable>` 或等价封装，替代单独的 `bHotReloadThreadStarted` 闸门。 2. 把启动期报错恢复模式建模成独立状态，例如 `EHotReloadWatcherMode::StartupRecovery / RuntimePolling / Stopped`，避免通过临时翻转 `bUseHotReloadCheckerThread` 复用 steady-state 语义。 3. 在错误对话框结束时，若 steady-state 仍未启用，则显式停止并回收临时 watcher，同时把“已启动”状态恢复到可再次启动；若 steady-state 需要启用，则让临时 watcher无缝转为长期模式，而不是先退出再被一次性闸门挡住。 4. 修改 `CheckForHotReload()`，只有在确认存在存活 watcher 或已经同步完成扫描时才允许把 `bWaitingForHotReloadResults` 置为 `true`；否则立即走同步 `CheckForFileChanges()` fallback，禁止永久等待。 5. 在 `AngelscriptEngineIsolationTests.cpp` 或 `AngelscriptTest/HotReload/` 下增加恢复回归：先制造启动期编译失败并进入对话框轮询，再修复脚本继续启动，随后触发第二次文件变更，断言 `bWaitingForHotReloadResults` 会被清空且热重载仍可继续工作。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 风险 | 调整线程状态机会影响现有非 editor development 模式的 hot reload 行为；若 stop/restart 次序处理不当，可能引入重复扫描或线程未退出的新问题。 |
| 前置依赖 | 无 |
| 验证方式 | 新增“启动报错修复后继续热重载”的自动化；同时跑现有 hot reload scenario tests，确认正常启动路径与编辑器内 reload 行为不退化。 |

### Issue-16：启动报错对话框的跨线程等待缺少完成信号，失败路径会卡住初始化 worker

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `819-848`; `2115-2190` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `A-28`，并补充执行方案：默认 threaded 初始化会把 `Initialize_AnyThread()` 放到 worker thread 中执行（`819-848`），因此启动期编译失败时通常也是 worker 进入错误恢复逻辑。该路径只用 `volatile bool bErrorResponseDone = false` 在 worker 与 game thread 之间同步（`2115`），随后把 `ShowErrorDialog()` 投递到 game thread，并在 worker 上循环 `while (!bErrorResponseDone) FPlatformProcess::Sleep(0.01f);`（`2184-2190`）。然而 `ShowErrorDialog()` 的失败分支在 `!bSuccess` 时直接 `RequestExit(true); return;`（`2171-2174`），完全不会写入完成位；成功分支虽然会写 `bSuccess = true` 和 `bErrorResponseDone = true`（`2143-2145`, `2176-2178`），但两者都只是跨线程裸写，没有任何原子语义或事件通知。 |
| 根因 | 启动报错恢复复用了 `Initialize()` 主路径里已经有问题的 `volatile + Sleep` 协作方式，却又额外把对话框结果和完成通知拆成两个裸共享变量，导致失败路径根本没有 completion signal，成功路径也没有可见性保证。 |
| 影响 | 这条恢复链会把“脚本编译错误后的人工修复”放进新的阻塞窗口：失败退出时 worker 会一直停在等待循环，直到进程真正被外层强退；成功修复时也可能因为缺少 happens-before 而出现 worker 迟迟看不到完成位、编辑器继续卡住的情况。结果是错误恢复路径本身成为新的启动卡死源。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用显式结果对象和一次性同步原语替代 `volatile` 自旋，确保成功/失败两条路径都必定发出完成通知。 |
| 具体步骤 | 1. 引入显式返回结果，例如 `enum class EStartupCompileDialogResult { RetrySucceeded, ExitRequested };`，并通过 `TPromise`/`TFuture`、`FEvent` 或等价同步原语在 game thread 与 worker 之间传递，而不是共享 `bSuccess` 与 `bErrorResponseDone`。 2. 将 `ShowErrorDialog()` 改为无论成功、失败、窗口关闭还是 `RequestExit`，都必须写出一个最终结果并触发 completion；禁止出现早退分支不通知等待方。 3. worker 侧改成阻塞等待结果对象，并在 `IsEngineExitRequested()` / `FPlatformMisc::RequestExit` 场景下可中断返回，去掉 `Sleep(0.01f)` 忙等。 4. 把所有 UI 状态变更限定在 game thread 内，worker 只消费最终结果，不再直接共享 `bSuccess` 这类跨线程可变状态。 5. 增加测试 seam 或注入式对话框工厂，在自动化中分别模拟“修复成功继续启动”和“用户退出”两条路径，断言等待方都能稳定结束。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 风险 | 为对话框引入 test seam 和显式结果对象会改动启动期错误恢复结构；若 UI 线程与 worker 线程职责拆分不彻底，可能留下新的双重退出路径。 |
| 前置依赖 | 无 |
| 验证方式 | 新增两条自动化：一条模拟修复成功后 worker 正常继续初始化，一条模拟退出后等待方立即结束；并观察无残留自旋线程、无超时。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-16 | Defect | 立即修复，先替换启动报错恢复的等待协议，消除失败/成功路径卡死窗口 |
| P1 | Issue-15 | Defect | 随后修复 hot reload checker 的线程状态机，保证启动恢复后仍能继续检测文件变更 |
| P1 | Issue-14 | Defect | 在启动恢复链稳定后推进，收口 subsystem adopt 生命周期，禁止长期持有临时 engine |

---

## 发现与方案 (2026-04-08 13:12)

### Issue-17：`Tick()` 已知 `GEngine` 可能为空，却仍无条件走 `HasGameWorld()` 解引用

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `2781-2792`; `2794-2829` |
| 问题 | `Tick()` 在热重载开发模式分支里先显式注释“启动期编译失败后用户点 `Try Again` 时 `GEngine` 可能为 `nullptr`”，并且确实在 `HotReloadTestRunner` 路径上加了 `GEngine != nullptr` 保护（`2799-2803`）。但同一个函数几行后又直接执行 `if (!GIsEditor || HasGameWorld())`（`2822`），而 `HasGameWorld()` 内部无条件遍历 `GEngine->GetWorldContexts()`（`2781-2792`）。也就是说，代码已经承认 `GEngine == nullptr` 是合法运行态，却在同一 tick 中继续走一条确定会解引用空 `GEngine` 的分支。 |
| 根因 | `Tick()` 只对单个子路径补了空指针保护，没有把“启动恢复期允许 `GEngine == nullptr`”提升为整个 hot reload 分支的统一前置条件；`HasGameWorld()` 也被写成了默认 `GEngine` 总是存在的 helper。 |
| 影响 | 启动报错恢复场景下，只要 engine 进入开发模式 tick 且走到热重载检查，就可能在 `HasGameWorld()` 中直接崩溃。这样会把本应用于恢复编译错误的路径再次变成致命故障，导致用户无法完成 `Try Again` 恢复流程。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `GEngine` 判空前置到整个 hot reload 决策分支，并让 `HasGameWorld()` 本身具备空值语义。 |
| 具体步骤 | 1. 将 `HasGameWorld()` 改成空安全 helper：入口先判断 `GEngine == nullptr` 并稳定返回 `false`，禁止 helper 自己隐含要求全局引擎已存在。 2. 在 `FAngelscriptEngine::Tick()` 的 `bScriptDevelopmentMode` 分支内，把 `GEngine` 判空提升为统一 gate；当 `GEngine == nullptr` 时只允许执行不依赖 world 的最小恢复逻辑，明确跳过 `HasGameWorld()` 和依赖 world context 的热重载模式判断。 3. 若启动恢复期仍需轮询脚本文件变化，新增一个不依赖 `GEngine` 的 fallback 分支，例如固定走 `CheckForHotReload(ECompileType::SoftReloadOnly)` 或仅驱动恢复所需的同步扫描。 4. 给相关注释和分支命名补充“startup recovery / no `GEngine`”语义，避免后续又在同一窗口添加新的 `GEngine` 访问。 5. 为启动编译失败恢复流程补 focused regression：模拟 `GEngine == nullptr` 下执行 `Tick()`，断言不会崩溃且仍能继续推进错误恢复所需的轮询。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果直接把 `GEngine == nullptr` 时的热重载检查全部跳过，可能让启动恢复轮询丢失一次文件变更，需要明确 fallback 行为。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 `GEngine == nullptr` 的 tick 回归，覆盖 `Try Again` 恢复窗口；并复跑 hot reload scenario tests，确认正常 editor/runtime 路径的 reload 模式选择不回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-17 | Defect | 立即修复，先消除启动恢复期 `Tick()` 的空指针崩溃路径 |

---

## 发现与方案 (2026-04-08 13:13)

### Issue-18：owner teardown 无锁清空 `GlobalContextPool`，与正常借还路径的加锁访问形成真实数据竞争

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `397-398`; `1031-1036`; `1185-1197`; `1831-1837`; `1890-1897` |
| 问题 | `FAngelscriptEngine` 明确定义了 `GlobalContextPool` 与对应的 `GlobalContextPoolLock`（`AngelscriptEngine.h:397-398`），而正常运行时对这份池的读取和写回都严格包在 `FScopeLock` 中：`ClearModules()` 释放匹配 context 时会先锁（`1031-1036`），`FAngelscriptPooledContextBase::Init()` 从全局池取 context 时加锁（`1831-1837`），析构把 context 还回全局池时也加锁（`1890-1897`）。但 owner `Shutdown()` 在最终 teardown 路径里直接遍历 `GlobalContextPool` 并 `Empty()`（`1185-1197`），完全没有获取 `GlobalContextPoolLock`。这说明同一个容器在稳态路径被视为需要互斥保护，在销毁路径却被当成单线程私有数据处理。 |
| 根因 | `Shutdown()` 假定 owner teardown 发生时不会再有其他线程归还或借出 pooled context，但 RuntimeCore 实际存在 worker thread、hot reload、编译与调试回调等后台路径；代码也已经通过正常路径的加锁明确承认这份池是共享状态。 |
| 影响 | 当某个线程正在 `Init()`/析构 pooled context 并持锁访问 `GlobalContextPool` 时，owner 线程可能同时无锁遍历、释放并清空同一个 `TArray`。结果包括 `TArray` 内存竞争、重复 `Release()`、遗漏释放，甚至把刚归还的 `asCContext` 放进一个已经被 owner teardown 清掉的池，形成跨线程 use-after-free 和非确定性崩溃。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 `GlobalContextPool` 的所有读写都走同一把锁，并在 owner teardown 前显式阻断新的 pooled-context 归还。 |
| 具体步骤 | 1. 在 `Shutdown()` 中把 `GlobalContextPool` 的遍历和 `Empty()` 收进 `FScopeLock Lock(&GlobalContextPoolLock)`，与 `Init()`/析构/`ClearModules()` 保持同一互斥合同。 2. 不要在持锁状态下直接逐个 `Context->Release()`；应先把 `GlobalContextPool` move 到局部数组，再解锁后逐个释放，避免销毁过程中长时间占锁。 3. 在 owner teardown 进入“不可再借还 context”的阶段时设置显式 stop gate，例如 `bAcceptingPooledContexts = false` 或复用生命周期 token，让 `FAngelscriptPooledContextBase::~FAngelscriptPooledContextBase()` 检测到 owner 正在 shutdown 时直接 `Release()`，不再尝试写回全局池。 4. 审查所有访问 `GlobalContextPool` 的入口，确保没有任何无锁读写残留；把 “锁内移动、锁外释放” 提炼成 helper，避免后续再出现 teardown 特判。 5. 增加并发回归：一边让 worker 线程反复创建/归还 pooled context，一边在 owner 线程调用 `Shutdown()`，断言无崩溃、无重复释放，且 teardown 后 `GlobalContextPool` 和 `GAngelscriptContextPool` 都为空。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 增加 shutdown gate 后，少数此前依赖“teardown 期间仍可归还到池里”的路径会改成直接释放 context，可能改变释放时机；需要用回归确认不会引入重复 `Release()`。 |
| 前置依赖 | 建议与已有的 context-pool / hot reload 并发修复一起推进，但无需等待其它计划项落地。 |
| 验证方式 | 新增多线程 teardown 回归，覆盖 `Init()`、析构归还和 owner `Shutdown()` 并发执行；配合 ASan/调试断言或 UE 内存检测确认没有 `TArray` 并发破坏和重复释放。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-18 | Defect | 紧随 `Issue-17`，先统一 `GlobalContextPool` 的 teardown 锁合同，消除并发释放窗口 |

---

## 发现与方案 (2026-04-08 13:14)

### Issue-19：subsystem owner `Deinitialize()` 在 `Pop` 失败后仍继续 `Shutdown()`，会把已失效 engine 残留在 `ContextStack`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `32-45`; `399-410`; `718-723`; `1229-1251` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `A-23`，并补充具体修复方案：`UAngelscriptGameInstanceSubsystem::Deinitialize()` 在自有 engine 路径里先调用 `FAngelscriptEngineContextStack::Pop(PrimaryEngine)`，随后无论 `Pop` 是否真的成功，都会继续执行 `PrimaryEngine->Shutdown()`（`AngelscriptGameInstanceSubsystem.cpp:39-45`）。但 `Pop()` 在栈顶不匹配时只是 `ensureAlwaysMsgf(...)`，然后直接返回，不会移除任何栈项（`AngelscriptEngine.cpp:399-410`）。与此同时，`Shutdown()` 会把 wrapper 的 `Engine`、`SharedState`、`WorldContextObject` 等成员全部清空（`1229-1251`），而 `TryGetCurrentEngine()` 对 `ContextStack::Peek()` 没有任何“wrapper 是否仍有效”的过滤（`718-723`）。结果是只要 subsystem 在栈顶错位时进入 `Deinitialize()`，栈里就会留下一个已经 shutdown 的 `FAngelscriptEngine*`。 |
| 根因 | subsystem teardown 把 `ContextStack` 出栈当成“尽力而为”的 debug 断言，而不是 owner 销毁必须满足的前置条件；一旦栈合同被破坏，后续 teardown 仍继续推进，导致 resolver 状态和实例真实生命周期分离。 |
| 影响 | 后续任何走 `TryGetCurrentEngine()` / `Get()` 的路径都会优先解析到这个已失效 wrapper，而不是外层真实 engine 或空值。这样会把单次 `Pop` 失配扩大为持续性的 current-engine 污染，表现为错误 engine 解析、对空 `ScriptEngine` 的后续调用，甚至在下一次 scope/tick/world-context 操作中触发更隐蔽的崩溃。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 owner teardown 的“detach from `ContextStack`”升级为硬性成功条件，失败时先修复栈，再允许 `Shutdown()`。 |
| 具体步骤 | 1. 把 `FAngelscriptEngineContextStack::Pop()` 拆成显式返回结果的 API，例如 `bool TryPopExact(FAngelscriptEngine*)`，保留现有顺序断言，但把成功/失败传递给调用方。 2. 为 owner 销毁新增专用 helper，例如 `DetachEngineForOwnerTeardown(FAngelscriptEngine*)`：先尝试严格 `TryPopExact`；若栈顶不匹配，则记录诊断并执行受控 slow path，把目标 engine 从栈中按原顺序移除，保证待销毁 wrapper 不会继续留在 resolver 里。 3. 修改 `UAngelscriptGameInstanceSubsystem::Deinitialize()` 与 `FAngelscriptRuntimeModule::ShutdownModule()`，只有在 detach helper 完成后才允许继续 `Shutdown()` / `Reset()`；若 detach 失败且无法修复，应 fail-fast，而不是继续销毁实例。 4. 在 `TryGetCurrentEngine()` 或 `Get()` 上增加防御性过滤：若 `Peek()` 命中的 wrapper 已经没有底层 `ScriptEngine` 或生命周期 token 已失效，立即跳过并记录错误，避免单个残留条目长期污染 resolver。 5. 增加 focused regression：构造 subsystem owner engine 之上再压一个额外 scope，强制制造 `Pop` 栈顶不匹配，然后执行 `Deinitialize()`，断言 `ContextStack` 不残留失效 engine，且后续 `TryGetCurrentEngine()` 能恢复到外层实例或空值。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 引入 slow path 栈修复后，需要非常明确只在 owner teardown 使用，避免普通 `FAngelscriptEngineScope` 误用该 API 破坏 LIFO 语义。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 `Pop` 失配回归，覆盖 subsystem owner teardown 和 runtime module teardown；断言栈中不存在已 shutdown wrapper，且后续 `Get()`/`TryGetCurrentEngine()` 不再解析到失效实例。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-19 | Defect | 与 `Issue-18` 同级推进，先阻断 teardown 失败把失效 engine 残留在 resolver 的路径 |

---

## 发现与方案 (2026-04-08 13:22)

### Issue-20：`ActiveTickOwners` 把 subsystem tick 所有权做成进程级总闸，导致无关 engine 之间互相压制

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `8`; `17-29`; `32-37`; `38`; `186-197`; `2843-2845` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `B-14` / `B-15`，并补充执行方案：`UAngelscriptGameInstanceSubsystem` 用静态 `ActiveTickOwners` 统计“当前有多少 subsystem 拥有 tick”（`AngelscriptGameInstanceSubsystem.cpp:8`，`AngelscriptGameInstanceSubsystem.h:38`），而且只要 `PrimaryEngine != nullptr` 就会递增，不区分这台 engine 是 subsystem 自有、adopted 还是与 runtime module 是否为同一实例（`17-29`）。对应地，`FAngelscriptRuntimeModule::TickFallbackPrimaryEngine()` 只要看到 `HasAnyTickOwner()` 为真，就直接整段跳过 fallback tick（`186-197`）。与此同时，engine 自身的 `ShouldTick()` 只是 `return Engine != nullptr;`（`AngelscriptEngine.cpp:2843-2845`），意味着 clone/adopted wrapper 也会被视作“可推进的主引擎”。结果是任意 subsystem 只要存活，就会全局压制 runtime module 的 fallback tick，即使两边实际绑定的是不同 engine。 |
| 根因 | RuntimeCore 没有把“谁在 tick 哪台 engine”建模成 engine-local 合同，而是偷懒用一个进程级计数器表达所有 subsystem 的 tick 所有权；module 和 subsystem 两条生命周期因此共享了错误的全局闸门。 |
| 影响 | 多 world / 多 engine / clone 并存时，一个 subsystem 的存在就可能让另一台 module-owned engine 永久失去 tick 机会，后续 hot reload 轮询、开发模式检测和任何依赖 `Tick()` 推进的状态都会静默停摆。更糟的是 adopted/cloned `PrimaryEngine` 也会触发同一闸门，形成“并没有真正接管 module engine 的 subsystem，却把 module engine 的唯一 tick 路径关掉”的跨实例串扰。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 tick 所有权从“进程级有无 subsystem”改成“按 engine/lifetime token 精确仲裁”。 |
| 具体步骤 | 1. 删除 `ActiveTickOwners` 这种全局计数语义，改为在 RuntimeCore 内维护按 `FAngelscriptEngineLifetimeToken` 或 `FAngelscriptEngine*` 键控的 tick-owner registry，明确记录“这台 engine 当前是否已被 subsystem 接管”。 2. 修改 `UAngelscriptGameInstanceSubsystem::Initialize()` / `Deinitialize()`：只有在 subsystem 真正接管某台 engine 的 tick 责任时才注册/注销该 engine，而不是见到任意 `PrimaryEngine` 就全局递增。 3. 修改 `FAngelscriptRuntimeModule::TickFallbackPrimaryEngine()`，只检查“当前 module 准备 tick 的那台 engine 是否已有 subsystem owner”；无关 subsystem 不得影响这条判断。 4. 同步收紧 `ShouldTick()` 或其调用侧合同，避免 clone/adopted wrapper 仅因 `Engine != nullptr` 就被视为可安全推进的 primary engine；必要时将“可 tick”拆成 `HasBackingScriptEngine()` 与 `CanDriveRuntimeTick()` 两层判定。 5. 为 subsystem owner、adopted engine、module-owned primary engine 三种组合补 focused regression：验证 subsystem A 只会抑制 engine A 的 fallback tick，不会压制 engine B；同时验证 clone/adopted 场景不会错误关停 module-owned engine 的 tick。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | tick-owner registry 会改变 subsystem/module 现有的仲裁方式，少数默认依赖“任意 subsystem 存在就禁用 fallback tick”的测试夹具需要同步更新。 |
| 前置依赖 | 建议与 `Issue-13`、`Issue-14` 一起推进；先明确 tick 期间 current-engine 绑定，再收口 tick owner 仲裁。 |
| 验证方式 | 新增双 engine tick 仲裁回归，覆盖 subsystem 自有 engine、adopted engine 与 module-owned engine 并存；断言 runtime module 只在目标 engine 已被 subsystem 接管时才停掉 fallback tick。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-20 | Architecture | 在 `Issue-13/14` 之后立即推进，先把 subsystem/module 的 tick 所有权从全局闸门改成按 engine 仲裁 |

---

## 发现与方案 (2026-04-08 13:23)

### Issue-21：`ShutdownModule()` 不复位 `bInitializeAngelscriptCalled`，同进程二次启动会永久跳过引擎初始化

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `7`; `13-24`; `27-40`; `138-166`; `174-182` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `A-12`，并补充执行方案：module 级别用静态 `bInitializeAngelscriptCalled` 记录“是否初始化过”（`AngelscriptRuntimeModule.cpp:7`），`StartupModule()` 每次启动都会调用 `InitializeAngelscript()`（`13-24`），而该函数开头只要发现此标志为真就直接返回（`138-143`）。问题在于正常 `ShutdownModule()` 只移除 ticker 并销毁 `OwnedPrimaryEngine`（`27-40`），完全不会把 `bInitializeAngelscriptCalled` 复位为 `false`；整个文件里只有测试专用的 `ResetInitializeStateForTesting()` 会重置它（`174-182`）。这意味着只要 module 在同一进程里经历一次正常 `ShutdownModule()`，下一次 `StartupModule()` 进入 `InitializeAngelscript()` 时就会被这条陈旧状态位永久短路。 |
| 根因 | RuntimeModule 把“本次进程里曾经初始化过”错误地当成“当前 module 实例已经完成初始化”，用一个 sticky 静态标志代替了真实的 owner 生命周期状态；销毁路径与启动路径因此失去对称性。 |
| 影响 | 同进程的 module unload/reload、插件重载或任何再次触发 `StartupModule()` 的路径，都会在没有 owner engine、没有 `ContextStack` 注册、没有 fallback ticker 接线的前提下直接跳过引擎初始化。表现不是立即崩溃，而是模块看似启动成功，实际上 RuntimeCore 已处于“模块活着但 engine 没有重新建立”的半初始化状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 去掉 sticky 初始化闸门，改为按真实 owner/module 生命周期判定是否需要初始化，并保证 shutdown 对称复位。 |
| 具体步骤 | 1. 把 `bInitializeAngelscriptCalled` 从“一次置真永久有效”的粘滞标志改成真实状态机：要么删除该标志，直接依据 `OwnedPrimaryEngine.IsValid()`、override owner 是否存在、以及当前 module 是否已接线来判断；要么至少在 `ShutdownModule()` 完成 teardown 后统一复位为 `false`。 2. 将 `StartupModule()` / `InitializeAngelscript()` 的幂等保护收口到实际资源状态，而不是单独的布尔位；例如只有当 module 当前已经持有有效 owner engine 时才 early-return。 3. 把 `ShutdownModule()` 与 `ResetInitializeStateForTesting()` 复用同一套 cleanup helper，保证正常路径和测试路径都会按同一顺序执行 `RemoveTicker -> detach engine -> reset owner fields -> reset initialize state`。 4. 审查 override/testing 分支，确保即使使用 `InitializeOverrideForTesting`，module shutdown 后也不会把旧的初始化标志带到下一轮启动。 5. 在 `AngelscriptSubsystemTests.cpp` 或新增 runtime-module focused test 中补“同进程二次 `StartupModule()`”回归：第一次启动后 shutdown，再次 startup，断言新的 engine/`ContextStack`/fallback ticker 都会重新建立。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果直接删除标志而不补真实幂等判断，可能重新暴露 `Issue-11` 的重复初始化问题；因此必须同步把 early-return 条件改成“当前 owner 是否已存在”。 |
| 前置依赖 | 建议与 `Issue-11` 绑定处理，避免一边修掉“永远不再初始化”，一边留下“重复初始化同一 engine”。 |
| 验证方式 | 新增 module restart 回归：同进程执行 `StartupModule() -> ShutdownModule() -> StartupModule()`，断言第二次启动后 `InitializeAngelscript()` 不会被短路，且会重新建立可用的 primary engine 与 tick 接线。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-21 | Defect | 与 `Issue-11` 配对推进，先恢复 module 生命周期的启动/销毁对称性 |

---

## 发现与方案 (2026-04-08 13:24)

### Issue-22：多个 full engine 共用单个 `primaryContext` 槽，后创建/后销毁会把幸存 owner 的 usable context 清空

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptobject.cpp` |
| 行号 | `141`; `917`; `935`; `1566`; `354-360`; `1179-1182`; `150-155`; `48-49`; `141-142` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `A-29`，并补充执行方案：RuntimeCore 只有一根静态 `GameThreadTLD` 指针可访问当前线程的 `primaryContext` 槽（`AngelscriptEngine.h:141`）。无论 testing full engine 还是正式 full engine，初始化都会直接把这唯一槽位改写成新建 context（`AngelscriptEngine.cpp:917`, `1566`），随后 `InitializeOwnedSharedState()` 再把当下槽里的指针快照进各自 `SharedState->PrimaryContext`（`935`）。但 owner 释放时只会在“当前槽仍然等于自己”时把 `GameThreadTLD->primaryContext` 清成 `nullptr`（`354-360`），或在 shared-state 缺席路径直接释放并清空当前槽（`1179-1182`），从未恢复更早仍存活 owner 的 context。与此同时，AngelScript runtime 的 `asGetUsableContext()` 明确会在无 active context 时直接返回 `tld->primaryContext`（`as_context.cpp:150-155`），脚本对象工厂等基础路径已经依赖这一入口（`as_scriptobject.cpp:48-49`, `141-142`）。结果是多个 full engine 在同一线程并存时，后创建的 owner 会覆盖前一个 owner 的 usable context；再销毁后创建者时，线程槽被清空，而不是恢复到前一个仍存活 engine 的 context。 |
| 根因 | RuntimeCore 把 `primaryContext` 设计成线程上的单一全局槽，却允许同一线程存在多个独立 owner engine；生命周期实现只有“写当前槽”和“把自己的槽清空”，没有任何栈式保存/恢复或按 owner 恢复的机制。 |
| 影响 | 只要较新的 full engine 先退出，较旧但仍存活的 engine 就会失去 `asGetUsableContext()` 可见的 usable context。后续依赖 AngelScript 默认 usable context 的脚本对象创建、嵌套调用和部分 VM 内部 helper 会在 engine 仍存活的情况下突然退化成无上下文或重新分配 context，造成行为漂移、隐式失败或后续释放顺序异常。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `primaryContext` 从线程单槽改成可恢复的 owner-aware 状态，保证后创建 owner 退出后能恢复前一个幸存 engine 的 usable context。 |
| 具体步骤 | 1. 为 game-thread usable context 引入显式的 owner-aware 存储，例如 thread-local 栈或按 `FAngelscriptEngineLifetimeToken`/`asIScriptEngine*` 键控的 registry，而不是把所有 full engine 都压进同一个裸 `GameThreadTLD->primaryContext` 槽。 2. 在 full-engine 初始化时，记录被覆盖的前一个 usable context，并把当前 owner 的 context 作为新的顶部；在 owner 最终释放时，若当前顶部属于自己，则恢复前一个仍存活 owner 的 context，而不是直接写 `nullptr`。 3. 调整 `InitializeOwnedSharedState()` 与 `ReleaseOwnedSharedStateResources()`，让 `SharedState->PrimaryContext` 只表达“这个 owner 自己的 primary context”，不要再承担线程全局当前槽的唯一真相。 4. 审查 `Shutdown()` 中 `!LocalSharedState.IsValid()` 的 fallback 释放分支，确保它不会在多 owner 并存时误释放/误清空另一个仍存活 engine 的 usable context。 5. 在 `AngelscriptEngineIsolationTests.cpp` 增加 focused regression：同线程顺序创建两台 full engine，销毁后创建者后断言较早 owner 仍能通过 `asGetUsableContext()` 或等价执行路径拿到可用 context；再销毁最后一个 owner 时才允许线程槽归零。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 修改 usable-context 存储模型会影响 AngelScript 内部默认 context 复用路径；若恢复顺序设计错误，可能引入重复 `Release()` 或把已失效 owner 的 context 恢复回线程槽。 |
| 前置依赖 | 建议与 `Issue-08` 一并审查，统一梳理 `GameThreadTLD` 的职责边界。 |
| 验证方式 | 新增多 full-engine usable-context 回归，覆盖“后创建 owner 先销毁”和“最后一个 owner 销毁”两条路径；并通过脚本对象创建或等价 VM helper 验证 `asGetUsableContext()` 始终指向当前应当生效的 owner context。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-22 | Defect | 在 `Issue-08` 之后推进，先把 `GameThreadTLD` 上的 usable-context 生命周期收口成 owner-aware 模型 |

---

## 发现与方案 (2026-04-08 13:32)

### Issue-23：`FAngelscriptGameThreadScopeWorldContext` 既可复制又不绑定 owner engine，析构会把 world context 回滚到错误实例

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptGameThreadScopeWorldContext.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h`; `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp` |
| 行号 | `710-724`; `746-753`; `4-17`; `141-147`; `641-689`; `48-55` |
| 问题 | `FAngelscriptGameThreadScopeWorldContext` 构造时只缓存 `PreviousWorldContext = GetAmbientWorldContext()`，析构时直接执行 `AssignWorldContext(PreviousWorldContext)`（`AngelscriptEngine.h:712-720`）。但 `AssignWorldContext()` 并不是纯 ambient setter，它会先 `TryGetCurrentEngine()`，再把 `CurrentEngine->WorldContextObject` 和全局 ambient 一起改写（`AngelscriptEngine.cpp:746-753`）。与此同时，这个类型被直接绑定成 `ValueClass<FAngelscriptGameThreadScopeWorldContext>`（`Bind_FAngelscriptGameThreadScopeWorldContext.cpp:4-17`），`ValueClass<T>` 又会叠加 `asGetTypeTraits<T>()`（`AngelscriptBinds.h:141-147`），而 AngelScript trait 推导会在类型可复制时自动暴露 copy constructor / assignment（`angelscript.h:641-689`）；现有对象模型测试也已验证脚本 value type 的复制语义真实存在（`AngelscriptObjectModelTests.cpp:48-55`）。结果是这个 scope 既不会记住创建时的 owner engine，也没有禁止复制/赋值，任意副本或跨 engine 的析构都会把旧 world context 回写到“析构当下的 current engine”。 |
| 根因 | RuntimeCore 把带全局副作用的 world-context scope 设计成普通 value type，只记录 ambient world 快照，没有记录 owner engine / previous engine-local world context；绑定层又自动把复制语义暴露给脚本。 |
| 影响 | 一旦脚本或 C++ 侧发生按值传递、临时复制、容器搬运，或者 scope 生命周期跨过 engine 切换，析构就可能把旧 world context 回滚到错误 engine 的 `WorldContextObject`，同时覆盖 ambient 全局状态。直接后果包括 `TryGetCurrentWorldContextObject()`、subsystem/world resolver、timer/bind 调用和异常打印继续基于陈旧 world 运行，形成跨实例 world state 串线。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 world-context scope 改成显式 owner-aware、不可复制的 RAII 类型，禁止通过全局当前 engine 猜测回滚目标。 |
| 具体步骤 | 1. 重写 `FAngelscriptGameThreadScopeWorldContext`：保存创建时的 `FAngelscriptEngine*` 或其弱生命周期句柄、`PreviousAmbientWorldContext` 和该 engine 自己的 `PreviousEngineWorldContext`，析构时只在 owner 仍然有效且匹配时恢复对应 engine-local 字段。 2. 将 `FAngelscriptGameThreadScopeWorldContext` 显式声明为 `copy ctor/assignment = delete`，必要时只保留 move 语义；绑定层同步改成不暴露 copy/assignment trait，或直接改成不允许脚本长期持有的专用 scope helper。 3. 把构造/析构内部对 `AssignWorldContext()` 的直接调用拆开，至少区分“恢复 ambient world”和“恢复 owner engine 的 `WorldContextObject`”，避免通过析构当下的 `TryGetCurrentEngine()` 猜测目标。 4. 为脚本绑定增加 focused regression：验证复制/赋值 `FAngelscriptGameThreadScopeWorldContext` 会在编译期或绑定期被拒绝；再增加跨 engine 嵌套场景，断言内层 scope 析构不会回写外层以外的 engine。 5. 审查所有使用该类型的脚本 API，必要时用 `FAngelscriptEngineScope` 或新的 `FAngelscriptScopedWorldContext` 取代，统一到 RuntimeCore 已有的 engine-aware scope 合同。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptGameThreadScopeWorldContext.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp` |
| 预估工作量 | M |
| 风险 | 收紧复制语义后，现有脚本若把该类型当普通值对象使用会在编译期暴露出来，需要同步调整调用方式。 |
| 前置依赖 | 建议与 `Issue-03` 的 resolver 拆分一起设计；若先止血，至少先禁用 copy/assignment。 |
| 验证方式 | 新增脚本侧复制拒绝回归和跨 engine 嵌套回归，分别断言复制不可用、以及 scope 析构后 `TryGetCurrentWorldContextObject()` / ambient world 只恢复到对应 owner。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-23 | Defect | 尽快修复，先收口 world-context scope 的 owner 语义，阻断错误回滚污染全局状态 |

---

## 发现与方案 (2026-04-08 13:34)

### Issue-24：clone wrapper 丢失 `bIsInitialCompileFinished`，进入 clone scope 后会错误放宽 game-thread 安全门禁

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `242-249`; `481-488`; `628-647`; `918`; `2221`; `2848-2856`; `53-77` |
| 问题 | clone 创建路径会在 `CreateCloneFrom()` 中共享底层 engine 与 `SharedState`，然后调用 `AdoptSharedStateFrom(Source)`（`AngelscriptEngine.cpp:628-647`）。但 `AdoptSharedStateFrom()` 只复制了 `bDidInitialCompileSucceed` 与 `bCompletedAssetScan`，完全没有同步 `bIsInitialCompileFinished`（`2848-2856`）；而这个字段在 wrapper 上默认初始化为 `false`（`AngelscriptEngine.h:481-488`），只有 full engine 自己在初始化末尾才会置真（`AngelscriptEngine.cpp:918`, `2221`）。与此同时，`CanUseGameThreadData()` 会在 `TryGetCurrentEngine()` 命中当前 wrapper 时，依据 `!CurrentEngine->bIsInitialCompileFinished` 放宽非 game-thread 访问（`AngelscriptEngine.h:242-249`）；`CheckGameThreadExecution()` 也用同样条件跳过 Blueprint override 的线程检查（`ASClass.cpp:53-77`）。结果是只要当前解析到 clone wrapper，即使 source/full engine 早已完成初次编译，RuntimeCore 仍会把它当成“初始化特权期”处理。 |
| 根因 | clone 采用的是“共享 VM + 手工复制少量 wrapper 状态”的混合模型，但 `bIsInitialCompileFinished` 这种直接影响线程安全合同的生命周期事实仍被留在 wrapper 本地字段里，且没有进入 clone 投影。 |
| 影响 | clone scope 下的 `CanUseGameThreadData()`、Blueprint override 线程校验以及任何依赖“初次编译是否结束”的保护都会被系统性放宽。这样会把本应在 game thread 执行的访问误判为合法，制造只在 clone 场景下出现的线程安全假阴性，后续排查会非常困难。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“初次编译是否完成”从 wrapper 局部状态升级为 shared-state 生命周期事实，clone 只读取共享真相。 |
| 具体步骤 | 1. 将 `bIsInitialCompileFinished` 移入 `FAngelscriptOwnedSharedState`，或至少新增统一读入口（例如 `IsInitialCompileFinishedShared()`）并让 source/clone 都从同一位置读取，禁止 clone 继续依赖自己的默认字段。 2. 修改 `CreateCloneFrom()` / `AdoptSharedStateFrom()`，把所有会影响执行安全门禁的生命周期状态显式纳入同步，而不是手工挑几项布尔值复制。 3. 将 `CanUseGameThreadData()`、`CheckGameThreadExecution()` 以及其它依赖 `bIsInitialCompileFinished` 的调用点统一改走新的共享状态查询，确保 clone/source 对“初始化窗口是否仍在”得到同一答案。 4. 顺手审查 `bCompletedAssetScan` 这类同样会在 owner 运行期间继续变化的生命周期状态，评估是否也应进入 shared-state，而不是继续保留 wrapper-local snapshot。 5. 新增 focused regression：先创建已完成初次编译的 full engine，再创建 clone 并进入 clone scope，断言 `CanUseGameThreadData()` 与 `CheckGameThreadExecution()` 不会再误以为仍处于初次编译期。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 预估工作量 | M |
| 风险 | 若一次性把多个生命周期字段迁入 shared-state，clone/source 的现有测试夹具可能暴露出更多原先被 wrapper-local snapshot 掩盖的不一致，需要逐项校正断言。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 clone 生命周期回归，断言 source 初始化完成后 clone 读取到同样的 `IsInitialCompileFinished()` 结果；并在 clone scope 下验证 game-thread guard 不再被错误放宽。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-24 | Defect | 紧随 `Issue-23`，先消除 clone scope 对线程安全门禁的错误放宽 |

---

## 发现与方案 (2026-04-08 13:34)

### Issue-25：`IsInitialized()` 把“subsystem 对象存在”误判成“runtime 已可用”，与真实生命周期状态脱节

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp` |
| 行号 | `676-680`; `32-50`; `66-69`; `94-113`; `47-55`; `41-48` |
| 问题 | `FAngelscriptEngine::IsInitialized()` 只要 `ContextStack::Peek()` 非空，或者 `UAngelscriptGameInstanceSubsystem::GetCurrent()` 返回了 subsystem 对象，就会返回 `true`（`AngelscriptEngine.cpp:676-680`）。但 subsystem 自己在 `Deinitialize()` 中会明确把 `PrimaryEngine = nullptr`、`bInitialized = false`（`AngelscriptGameInstanceSubsystem.cpp:32-50`），`IsAllowedToTick()` 也只有在两者都成立时才认为 subsystem 可用（`66-69`）。`GetCurrent()` 却完全不检查这两个状态，只要能从 ambient world 找到 `GameInstance` 就直接返回 subsystem 对象（`94-113`）。同时，`EnsureAngelscriptUnitRuntimeInitialized()` 与 `EnsureAngelscriptIntegrationRuntimeInitialized()` 都把 `IsInitialized()` 当作是否需要重新 `InitializeAngelscript()` 的硬判断（`UnitTest.cpp:47-55`, `IntegrationTest.cpp:41-48`）。结果是 subsystem 对象尚存但已 deinitialize 时，RuntimeCore 会对外宣称“已初始化”，而 `TryGetCurrentEngine()` 实际上已经可能返回空。 |
| 根因 | RuntimeCore 把“找得到 subsystem UObject”与“存在可用 engine / runtime 已完成接线”混成同一个初始化合同，没有把 subsystem 的内部生命周期状态纳入公共判定。 |
| 影响 | 依赖 `IsInitialized()` 的调用点会在没有可用 engine 的情况下跳过重新初始化，随后再进入需要 current engine 的路径时才晚些时候失败。故障形态通常不是立刻崩溃，而是“初始化检查通过，但后续 `Get()` / bind / 测试入口又拿不到 engine”的半初始化状态，尤其容易污染测试和 teardown 恢复链。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把公共初始化判定收口到“是否存在可用 engine”而不是“是否找到 subsystem 对象”。 |
| 具体步骤 | 1. 修改 `FAngelscriptEngine::IsInitialized()`：优先复用 `TryGetCurrentEngine()` 或新的 `HasUsableCurrentEngine()` helper，只有当能解析到非空且有效的 engine 时才返回 `true`。 2. 为 `UAngelscriptGameInstanceSubsystem` 增加显式可用性查询，例如 `HasUsableEngine()` 或 `IsRuntimeReady()`，内部同时检查 `bInitialized`、`PrimaryEngine != nullptr` 以及必要的生命周期 token，而不是让外部拿到 UObject 就默认可用。 3. 调整 `GetCurrent()` 的职责边界：若保留“返回 subsystem UObject”语义，则禁止 `IsInitialized()` 直接用它做 readiness 判断；若希望它兼作 readiness 查询，则需要把 `bInitialized`/`PrimaryEngine` 校验并入实现。 4. 审查所有依赖 `FAngelscriptEngine::IsInitialized()` 的入口，尤其是 `UnitTest` / `IntegrationTest` 初始化 helper，确认在 subsystem 已 deinitialize 但对象未销毁时会重新走 `InitializeAngelscript()`。 5. 新增 focused regression：构造一个已创建但已 `Deinitialize()` 的 subsystem，保留 ambient world 可解析到该对象，断言 `IsInitialized()` 返回 `false`，且测试入口会重新初始化 runtime。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果有旧调用点把 `IsInitialized()` 当成“是否存在任意 subsystem 宿主”而不是“engine 是否可用”，修复后这些分支会改走重新初始化或空值处理，需要同步梳理预期。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 subsystem deinitialize 后的 readiness 回归，断言 `IsInitialized()` 与 `TryGetCurrentEngine()` 一致；再验证 `EnsureAngelscriptUnitRuntimeInitialized()` / `EnsureAngelscriptIntegrationRuntimeInitialized()` 会在该场景下重新接线。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-25 | Defect | 在高优先级生命周期修复后跟进，统一 readiness 合同，消除 false positive 初始化状态 |

---

## 发现与方案 (2026-04-08 13:43)

### Issue-26：`ContextStack` 对同一 engine 的嵌套 `FAngelscriptEngineScope` 不计深度，内层析构会提前解除外层作用域

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `391-409`; `437-507`; `718-723`; `746-753` |
| 问题 | `FAngelscriptEngineContextStack::Push()` 只有在栈顶不是同一个 `Engine` 时才会 `Add()`（`391-395`），因此对同一台 engine 的第二层 `FAngelscriptEngineScope` 是 no-op。对应地，内层 scope 析构时 `Reset()` 仍会无条件执行 `FAngelscriptEngineContextStack::Pop(Engine)`（`491-507`），而 `Pop()` 会把唯一那一层栈项直接弹掉（`399-409`）。这会让外层 scope 仍然存活时，`TryGetCurrentEngine()` 已经读不到当前 engine（`718-723`）；后续若再调用 `AssignWorldContext()`，写入目标也会跟着漂移（`746-753`）。 |
| 根因 | RuntimeCore 把 `ContextStack` 实现成“去重后的当前 engine 列表”，但 `FAngelscriptEngineScope` 的进入/退出语义却要求它是支持嵌套的真实 LIFO 栈；同一 engine 的进入次数没有被记录。 |
| 影响 | 只要代码在已有 `FAngelscriptEngineScope` 内再次进入同一 engine，内层析构就会提前打断外层的 current-engine 合同。之后 `Get()`、`TryGetCurrentEngine()`、`AssignWorldContext()`、`GetPackage()` 等公共入口都会在外层 scope 仍然有效时表现为“未初始化”或落到错误实例，属于 RuntimeCore 基础上下文原语的确定性错误。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `ContextStack` 改回真正的嵌套作用域栈，保证每次 `Push` 都有一条对称的 `Pop`。 |
| 具体步骤 | 1. 修改 `FAngelscriptEngineContextStack::Push()`，不要再对“栈顶等于同一 engine”做去重；要么始终压入重复项，要么改成显式 `{ Engine, Depth }` 栈节点，但必须保留每次进入的深度信息。 2. 保持 `Pop()` 的严格 LIFO 检查，并将其返回值改为可判断成功/失败的形式，便于 owner teardown 与 scope 析构在异常栈状态下采取补救路径。 3. 审查所有长期 owner 使用点，尤其是 subsystem/module 的 `Push()` 与临时 `FAngelscriptEngineScope` 叠加场景，确认“owner push + 同 engine 临时 scope + 临时 pop”后，owner 栈项仍然留在栈顶。 4. 为 `FAngelscriptEngineScope::Reset()` 增加 focused regression：同一 engine 建两层 scope，内层析构后断言 `TryGetCurrentEngine()` 仍返回外层 engine；再覆盖 subsystem owner 或 module owner 栈项上叠加同 engine scope 的场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | S |
| 风险 | 一旦去掉去重，少数测试或调试日志里对 `stackDepth` 的旧预期会改变，需要同步更新断言和诊断输出。 |
| 前置依赖 | 无 |
| 验证方式 | 新增“同 engine 双层 scope”与“长期 owner + 同 engine 临时 scope”两组回归；分别断言内层析构后 `TryGetCurrentEngine()`、`Get()`、`TryGetCurrentWorldContextObject()` 仍绑定外层作用域。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-26 | Defect | 尽快修复，先把 `ContextStack` 恢复成真正的嵌套栈，避免后续所有 scope 修复建立在错误原语上 |

---

## 发现与方案 (2026-04-08 13:48)

### Issue-27：`FAngelscriptEngineScope` 退出时不会恢复进入前的 ambient world context，叠加 ambient scope 后会直接丢上下文

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `445-449`; `501-507`; `710-720` |
| 问题 | `FAngelscriptEngineScope` 在带 `InWorldContext` 的构造路径里会先把进入前的 ambient world 缓存在 `PreviousWorldContext`，再调用 `AssignWorldContext(InWorldContext)`（`445-449`）。但 `Reset()` 退出时只恢复 `Engine->WorldContextObject`，然后 `Pop(Engine)` 并直接 `SyncAmbientWorldContextFromCurrentEngine()`（`501-507`）；整个退出路径从未使用 `PreviousWorldContext`。对比之下，`FAngelscriptGameThreadScopeWorldContext` 的析构会显式 `AssignWorldContext(PreviousWorldContext)`（`710-720`）。这意味着当外层只有 ambient world scope、没有外层 current engine 时，内层 `FAngelscriptEngineScope` 退出后 ambient 会被强制同步成 `nullptr`，而不是回到进入前的 world。 |
| 根因 | `FAngelscriptEngineScope` 的恢复逻辑只考虑了“退出后由新的 current engine 决定 ambient”的路径，没有覆盖“退出后没有 current engine，但进入前确实有 ambient world”的合法场景；保存下来的 `PreviousWorldContext` 因而成为死状态。 |
| 影响 | 任何把 ambient world scope 与 `FAngelscriptEngineScope` 叠加使用的调用链，都会在内层 engine scope 退出后丢失外层 world context。随后 `GetAmbientWorldContext()`、`TryGetCurrentWorldContextObject()`、subsystem/world resolver、timer bind 和异常/日志路径都会在外层 scope 仍然存活时错误返回空 world 或错误 world。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 `FAngelscriptEngineScope` 的退出恢复拆成“恢复 owner engine-local world”和“恢复 ambient world”两个显式步骤，不再只依赖当前栈顶同步。 |
| 具体步骤 | 1. 保留 `PreviousEngineWorldContext` 与 `PreviousWorldContext` 两份快照，但在 `Reset()` 中分别恢复：先把当前 engine 的 `WorldContextObject` 还原为 `PreviousEngineWorldContext`，再依据弹栈后的状态决定 ambient 应恢复到哪里。 2. 调整退出顺序：`Pop(Engine)` 之后，如果仍有新的 current engine，则用它同步 ambient；如果已经没有 current engine，则显式把 ambient 恢复为 `PreviousWorldContext`，而不是无条件清空。 3. 为避免再次走错写入口，将“恢复 ambient”从 `SyncAmbientWorldContextFromCurrentEngine()` 中拆出可复用 helper，例如 `RestoreAmbientWorldContext(UObject* PreviousAmbient)`，让 `FAngelscriptEngineScope` 与 `FAngelscriptGameThreadScopeWorldContext` 共享同一套恢复合同。 4. 增加 focused regression：外层建立 `FAngelscriptGameThreadScopeWorldContext`，内层建立 `FAngelscriptEngineScope`，断言内层析构后 ambient world 仍等于外层 world；同时再覆盖“外层另有 engine scope”和“完全无外层 scope”两条路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果现有调用点误依赖“内层 engine scope 退出后 ambient 被清空”的旧行为，修复后会暴露出这些路径此前遗漏的 world-context 清理问题。 |
| 前置依赖 | 建议先完成 `Issue-26`，先确保同 engine 嵌套的 push/pop 深度正确，再验证 ambient 恢复顺序。 |
| 验证方式 | 新增 ambient-scope + engine-scope 叠加回归，分别断言内层退出后 `GetAmbientWorldContext()`、`TryGetCurrentWorldContextObject()` 与相关 world-sensitive bind 仍指向外层 world。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-27 | Defect | 在 `Issue-26` 之后立即处理，保证 scope 退出时 world-context 能对称恢复 |

---

## 发现与方案 (2026-04-08 13:49)

### Issue-28：late init 只等待未来 `OnPostEngineInit`，事件已过或 `AssetManager` 缺席时会把测试发现链路卡在半完成状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp` |
| 行号 | `1628-1633`; `2201-2218`; `2478-2490`; `4143-4153`; `47-55`; `41-48`; `22-28` |
| 问题 | RuntimeCore 仍然保留按需初始化入口：`EnsureAngelscriptUnitRuntimeInitialized()` 与 `EnsureAngelscriptIntegrationRuntimeInitialized()` 在 runtime 未就绪时都会调用 `FAngelscriptRuntimeModule::InitializeAngelscript()`（`UnitTest.cpp:47-55`, `IntegrationTest.cpp:41-48`）。但 `Initialize_AnyThread()` 对后续接线只做“注册未来回调”：coverage 只在 `FCoreDelegates::OnPostEngineInit` 中才调用 `CodeCoverage->AddTestFrameworkHooks()`（`1628-1633`, `AngelscriptCodeCoverage.cpp:22-28`），测试发现也只在另一条 `OnPostEngineInit` lambda 中才会执行（`2201-2218`）。更糟的是，`AssetManager == nullptr` fallback 只做 `DiscoverTests()`，却没有推进任何 ready 状态（`2213-2217`）；而后续 hot-reload 测试准备和 post-compile 增量测试发现都把 `bCompletedAssetScan` 当硬门槛（`2478-2490`, `4143-4153`）。结果是 late init 场景或 `AssetManager` fallback 场景下，engine 会停在“初始化成功，但 coverage/test discovery readiness 永远不完整”的状态。 |
| 根因 | RuntimeCore 把 `OnPostEngineInit` 当成唯一的后续接线时机，却没有提供“事件已经发生时立即补齐”的同步路径；同时又把“AssetManager 初始扫描完成”和“测试发现已准备就绪”压缩进同一个 `bCompletedAssetScan` 布尔状态，导致 fallback 只能执行行为，不能推进生命周期。 |
| 影响 | late init engine 会静默缺失 coverage hook、hot-reload 单测准备和 post-compile 测试再发现能力。表面上 `InitializeAngelscript()` 返回成功，但相关功能会长期退化，而且当前日志只提示 `Asset Manager was not ready...`，不会明确指出 runtime 已卡在半完成状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 post-engine-init 后续接线改为“立即执行或延后执行”的统一 helper，并把测试发现 ready 状态从 asset-scan 事实中拆开。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中新增统一 helper，例如 `FinalizePostEngineInitHooks()` / `FinalizeTestDiscoveryBootstrap()`，内部负责 coverage hook、`DiscoverTests()` 以及相关 readiness 状态推进；禁止继续把这些步骤散落在匿名 `OnPostEngineInit` lambda 里。 2. 初始化时先判断当前进程是否已经具备立即执行条件：如果 automation controller / `UAssetManager` 已可用，则直接调用 helper；只有在依赖尚未就绪时才注册 `OnPostEngineInit` 或 `OnCompletedInitialScan` 的延后回调。 3. 将“AssetManager 初始扫描完成”与“测试发现/热重载测试已可用”拆成两个显式状态，例如保留 `bCompletedAssetScan` 表示事实状态，新增 `bTestDiscoveryReady` 作为功能闸门；`AssetManager == nullptr` fallback 在执行 `DiscoverTests()` 后必须推进后者，不能继续让热重载测试准备永久卡在 `false`。 4. 修改 `PrepareTests(...)` 和 post-compile 增量发现的门禁，让它们检查新的 readiness 状态，而不是继续把 `bCompletedAssetScan` 当作全部前提。 5. 为 late init 和 fallback 两条路径补 focused regression：先在 runtime 未初始化后调用 `EnsureAngelscriptUnitRuntimeInitialized()` / `EnsureAngelscriptIntegrationRuntimeInitialized()`，断言 discovery/hook 会立即补齐；再构造 `AssetManager == nullptr` fallback，断言热重载测试准备与 post-compile 测试发现仍可继续工作。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCodeCoverageTests.cpp` |
| 预估工作量 | M |
| 风险 | readiness 状态一旦拆分，部分旧调用点会暴露出此前把 `bCompletedAssetScan` 当“万能已就绪”标志的隐含假设，需要同步梳理所有门禁。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 late-init 与 `AssetManager` fallback 回归，断言 `CodeCoverage->AddTestFrameworkHooks()`、`DiscoverTests()`、`PrepareTests(...)` 与 post-compile 测试发现都会在两条路径上完成接线，不再依赖未来某次 `OnPostEngineInit`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-28 | Defect | 在核心 scope 合同收口后推进，补齐 late-init 与测试发现的后置接线状态机 |

---

## 发现与方案 (2026-04-08 14:00)

### Issue-29：shared-state teardown 只清当前线程的 `thread_local` context pool，其他线程缓存的 context 会跨 engine epoch 残留

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `87`; `195-215`; `364-368`; `1185-1227`; `1722-1745`; `1801-1903`; `1905-1913`; `654-661` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `A-26`，并补充执行方案：`GAngelscriptContextPool` 被定义为 `thread_local`（`AngelscriptEngine.cpp:87`），`AngelscriptRequestContext()` / `AngelscriptReturnContext()` 又始终优先把空闲 context 留在当前线程的 `LocalPool.FreeContexts`（`1722-1745`，`1801-1903`）。但 owner teardown 不论走 `ReleaseOwnedSharedStateResources()` 还是 `Shutdown()`，都只能对“当前线程”那份 `GAngelscriptContextPool.FreeContexts` 调用 `ReleaseContextsForScriptEngine(...)`（`364-368`, `1185-1227`），随后马上执行 `ShutDownAndRelease()`。其它线程的本地池既没有全局注册表，也没有在 teardown 时被遍历；它们只会在 `FAngelscriptContextPool::~FAngelscriptContextPool()` 的线程退出阶段才逐个 `Release()`（`1905-1913`）。 |
| 根因 | RuntimeCore 只为 engine 维护了实例级 `GlobalContextPool`，却没有为跨线程 `thread_local` pool 建立 owner registry 与统一回收协议，导致 engine 生命周期只能清理当前线程视角里的 cached context。 |
| 影响 | worker thread 只要曾经执行过脚本并把 `asCContext` 归还到本线程本地池，这些 context 就会越过 full engine 的 `Shutdown()` 继续存活。后果包括跨 engine epoch 的 context 泄漏，以及线程晚退出时才对已释放 script engine 关联对象执行 `Release()`；若后续 engine epoch 复用了旧地址，`TryTakeContextFromPool()` 还可能误复用上一轮遗留 context。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为每个 `thread_local` context pool 建立可枚举的全局注册表，并在 owner teardown 前统一清空所有线程上属于该 script engine 的 cached context。 |
| 具体步骤 | 1. 给 `FAngelscriptContextPool` 增加构造/析构注册逻辑：每个线程本地池创建时把自身登记到一个进程级 registry，析构时注销；registry 使用 `FRWLock` 或 `FCriticalSection` 保护。 2. 在 `FAngelscriptContextPool` 内补一把针对 `FreeContexts` 的细粒度锁，新增 `ReleaseContextsForScriptEngineOnPool(...)` / `TakeContextFromPool(...)` 这类 helper，禁止 owner teardown 与 worker thread 同时无锁读写同一线程池。 3. 新增统一 helper，例如 `ReleaseThreadLocalContextsForScriptEngine(asIScriptEngine*)`，先从 registry 抓取所有存活 pool 的快照，再逐个清掉与目标 engine 匹配的 cached context。 4. 在 `ReleaseOwnedSharedStateResources()` 与 `Shutdown()` 真正执行 `ShutDownAndRelease()` 之前调用该 helper；`PreInitialize_GameThread()` 启动新 full engine 前也应走同一入口，避免旧 epoch context 混入新 VM。 5. 增加并发回归：让 worker thread 先借出并归还 context，再由 owner thread 触发 `Shutdown()`，断言 teardown 完成后所有线程池里都不再保留该 script engine 的 context，且后续新 engine 初始化不会复用旧 epoch context。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 给 thread-local pool 补锁和全局注册会触碰 context 借还热路径，若实现不当会引入额外争用或死锁，需要坚持“锁内摘链表、锁外 Release”原则。 |
| 前置依赖 | 无 |
| 验证方式 | 新增跨线程 teardown 回归，覆盖 “worker 线程归还 context -> owner shutdown -> 新 engine 重建” 序列；并在测试里统计各线程池中针对目标 script engine 的 context 数量归零。 |

---

## 发现与方案 (2026-04-08 16:39)

### Issue-30：clone wrapper 不投影 runtime flags，clone scope 下的 current-context 语义会偏离 source engine

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` |
| 行号 | `2848-2857`; `149-154`, `546`; `303-323`, `692-714`; `40-67`, `232-236`, `485-488`; `977-980`, `1072-1072` |
| 问题 | `CreateCloneFrom()` 最终只让 `AdoptSharedStateFrom()` 复制 `ConfigSettings`、package、root path、`bDidInitialCompileSucceed` 和 `bCompletedAssetScan`（`AngelscriptEngine.cpp:2848-2857`），但 clone wrapper 并没有同步 `bSimulateCooked`、`bTestErrors`、`bForcePreprocessEditorCode`、`bUseEditorScripts`、`bUseAutomaticImportMethod`、`bScriptDevelopmentMode` 这些 wrapper-local runtime flags（`AngelscriptEngine.h:149-154`, `546`）。与此同时，`IsSimulatingCookedForCurrentContext()`、`IsTestingErrorsForCurrentContext()`、`IsForcingPreprocessEditorCodeForCurrentContext()`、`ShouldUseEditorScriptsForCurrentContext()`、`ShouldUseAutomaticImportMethodForCurrentContext()`、`IsScriptDevelopmentModeForCurrentContext()` 全部直接读取 current engine wrapper 上的这些字段（`AngelscriptEngine.cpp:303-323`, `692-714`）。下游预处理和绑定代码又把这些 helper 当正式合同使用，例如 `FAngelscriptPreprocessor` 用它们决定 `EDITOR`/`EDITORONLY_DATA`/自动 import 行为（`AngelscriptPreprocessor.cpp:40-67`, `232-236`, `485-488`），`Bind_BlueprintType` 用它过滤 editor-only class/property（`Bind_BlueprintType.cpp:977-980`, `1072`）。 |
| 根因 | clone 目前只被当成“共享底层 `asIScriptEngine` 的轻量 wrapper”，但 RuntimeCore 的大量 current-context 判断仍挂在 wrapper-local flags 上；`AdoptSharedStateFrom()` 没有把这些语义状态一并投影过来。 |
| 影响 | 同一底层 VM 在 source scope 和 clone scope 下会得出不同的 cooked/editor/import/development 判断。结果不是单纯的观测差异，而是会直接改变预处理结果、manual import 处理方式以及 editor-only bind 暴露面，导致 clone 参与的编译、热重载或测试辅助行为与 source engine 失配。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 clone 建立完整的 runtime-context 投影合同，禁止 current-context helper 继续读取未同步的 wrapper-local 默认值。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 内新增专门的 clone 投影 helper，例如 `AdoptRuntimeFlagsFrom(const FAngelscriptEngine& Source)`，统一复制 `bSimulateCooked`、`bTestErrors`、`bForcePreprocessEditorCode`、`bUseEditorScripts`、`bUseAutomaticImportMethod`、`bScriptDevelopmentMode` 以及后续所有 current-context 语义字段。 2. 让 `CreateCloneFrom()` 在 `AdoptSharedStateFrom()` 之外强制调用该 helper，并对新增字段采用单一入口维护，避免以后再出现“新增 helper 但忘记同步 clone”的分支漂移。 3. 对于本质应随 shared engine 走的语义，进一步评估是否应该下沉到 `SharedState` 或显式 resolver 输入，而不是继续挂在 wrapper-local 字段上；至少要把 current-context helper 改成只读取“已投影状态”，不直接依赖未初始化的 clone 默认值。 4. 为 clone 增加 focused regression：以 `bSimulateCooked=true`、`bForcePreprocessEditorCode=true`、`bDevelopmentMode=true`、`bAutomaticImports` 开/关等组合创建 source full engine，再创建 clone，分别在 clone scope 下断言上述 current-context helper 与 source scope 完全一致。 5. 增加一条集成回归，直接在 clone scope 构造 `FAngelscriptPreprocessor` 并验证 `EDITOR`/`EDITORONLY_DATA`/automatic import 行为与 source scope 一致，同时验证 editor-only bind 过滤不再分叉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接复制字段而不梳理哪些状态应当 shared、哪些应当 per-wrapper，可能把本来应隔离的本地状态也一并固化到 clone；需要先明确 current-context helper 的正式语义边界。 |
| 前置依赖 | 建议结合 `Issue-24`、`Issue-28`、`Issue-29` 一起审视 clone/source 的状态投影，避免只修一个布尔值又留下其它 helper 漏洞。 |
| 验证方式 | 新增 clone scope 回归，覆盖 `IsSimulatingCookedForCurrentContext()`、`IsTestingErrorsForCurrentContext()`、`IsForcingPreprocessEditorCodeForCurrentContext()`、`ShouldUseEditorScriptsForCurrentContext()`、`ShouldUseAutomaticImportMethodForCurrentContext()`、`IsScriptDevelopmentModeForCurrentContext()`；再在 clone scope 下直接跑预处理与 editor-only bind 过滤断言，确认结果与 source scope 一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-30 | Defect | 在现有 clone 生命周期问题之后立即处理，先把 current-context helper 从 clone 默认值上收回来 |

---

## 发现与方案 (2026-04-08 16:41)

### Issue-31：non-editor threaded 初始化在 worker thread 上加载 bind modules，会被 UE `ModuleManager` 直接拒绝

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Engine/Source/Runtime/Core/Private/Modules/ModuleManager.cpp` |
| 行号 | `825-840`; `1481-1488`; `157-161`; `914-928`; `1011-1016` |
| 问题 | `FAngelscriptEngine::Initialize()` 在 `ShouldInitializeThreaded()` 为真时把 `Initialize_AnyThread()` 投到 `AnyHiPriThreadHiPriTask`（`AngelscriptEngine.cpp:825-840`）。而 `Initialize_AnyThread()` 又会遍历 `FAngelscriptBinds::GetBindModuleNames()`，并直接调用 `FModuleManager::Get().LoadModule(FName(ModuleName), ELoadModuleFlags::LogFailures)`（`AngelscriptEngine.cpp:1481-1488`）。UE 自己的 `FModuleManager` 明确规定：模块如果尚未加载且当前不在 game thread，就返回 `EModuleLoadResult::NotLoadedByGameThread`（`ModuleManager.cpp:914-928`），并在实现里明确注释 “doing LoadModule off GameThread should not be done”（`157-161`, `1011-1016`）。RuntimeCore 这里既没有把模块预加载到 game thread，也没有检查任何失败结果。 |
| 根因 | 初始化编排把“必须在 game thread 完成的模块加载”混进了 `Initialize_AnyThread()`，默认 non-editor threaded 路径因此把一段有线程前置条件的工作搬到了 worker thread。 |
| 影响 | 只要某个 auto-generated bind module 在这之前还没被主线程加载，packaged / non-editor 的默认 threaded 初始化就会静默丢掉该模块。最终表现为绑定缺失、脚本 API 集合不完整，而且问题只在某些启动顺序或部署形态出现，回归稳定性很差。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 bind module 的真正 `LoadModule` 阶段前移到 game thread，只允许 worker thread 消费已加载结果。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中新增明确的 game-thread 阶段 helper，例如 `PreloadBindModules_GameThread()`，专门负责遍历 `FAngelscriptBinds::GetBindModuleNames()` 并在 game thread 上完成 `LoadModule` / `LoadModuleChecked`。 2. 调整 `Initialize()` 时序：`PreInitialize_GameThread()` 之后、切到 worker thread 之前先调用该 helper；`Initialize_AnyThread()` 内只保留对已加载 bind module 的消费逻辑，禁止继续执行真实 DLL/module 启动。 3. 为模块加载补失败显式化：若 game-thread 预加载失败，立刻记录诊断并中止后续绑定初始化，而不是让 worker thread 静默跳过。 4. 若某些调用点只需要读取已加载模块实例，把 off-thread `LoadModule` 改成 `GetModule` / `GetModuleChecked` 或者显式使用预加载阶段产出的结果缓存，彻底收掉非法线程路径。 5. 增加 non-editor threaded focused regression：构造一个启动前未预加载的 bind module，断言初始化期间会在 game thread 完成加载；再断言 worker thread 路径不再触发 `NotLoadedByGameThread` 失败。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果简单把整个 bind 初始化都搬回 game thread，可能抵消 threaded 初始化想保留的并行收益；需要只前移真正的 `LoadModule`，保留后续可并行部分。 |
| 前置依赖 | 建议与 `Issue-08` 一并处理；两者都属于 threaded initialize 的阶段边界错误，分开修会反复改初始化编排。 |
| 验证方式 | 在 non-editor threaded 初始化下新增回归，断言 bind module 预加载发生在 game thread，且最终绑定完整可用；同时打开 `LogModuleManager` 验证不再出现 `NotLoadedByGameThread` 失败。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-31 | Defect | 与 `Issue-08` 配对推进，先把 threaded initialize 的非法模块加载收回到 game thread |

---

## 发现与方案 (2026-04-08 16:42)

### Issue-32：`AssignWorldContext()` 只清洗 ambient 指针，不清洗 engine-local `WorldContextObject`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp` |
| 行号 | `268-276`; `682-689`; `746-753`; `169-174`, `352-352`; `73-89` |
| 问题 | `SetAmbientWorldContext()` 会在写 ambient 前用 `IsValidLowLevelFast(false)` 把失效对象清成 `nullptr`（`AngelscriptEngine.cpp:268-276`），但 `AssignWorldContext()` 先把原始 `NewWorldContext` 直接写进 `CurrentEngine->WorldContextObject`，之后才调用 `SetAmbientWorldContext(NewWorldContext)`（`746-753`）。这让同一次写入出现了分裂状态：ambient 可能已经被纠正为 `nullptr`，engine-local `WorldContextObject` 却仍保留原始失效指针。更糟的是，`TryGetCurrentWorldContextObject()` 在存在 current engine 时会优先返回 `WorldContextObject`（`682-689`），下游 bind 又直接把它传给 `GEngine->GetWorldFromContextObject(...)` 或 `UGameplayStatics::*`（`Bind_AActor.cpp:169-174`, `352`; `Bind_Subsystems.cpp:73-89`）。 |
| 根因 | RuntimeCore 把 world-context 校验放在 ambient 写入口，而不是放在统一 setter / getter 边界；engine-local 存储和 ambient 存储因此使用了两套不一致的有效性合同。 |
| 影响 | 只要恢复路径、GC 或 world teardown 把一个已失效 `UObject*` 再次喂给 `AssignWorldContext()`，当前 engine 仍会继续向外暴露悬空 world context。后续 actor bind、subsystem bind、world 解析和任何依赖 `TryGetCurrentWorldContextObject()` 的路径都会在已经无效的对象上继续工作，形成错误 world 解析乃至崩溃。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 world-context 有效性校验前移到统一入口，保证 engine-local 与 ambient 永远写入同一份“已清洗值”。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中新增单一 helper，例如 `SanitizeWorldContext(UObject*)` 或 `ResolveValidWorldContext(UObject*)`，统一处理 `nullptr` / `IsValidLowLevelFast(false)` 校验。 2. 修改 `AssignWorldContext()`，先对输入做 sanitize，再把同一个 sanitized 值同时写入 `WorldContextObject` 和 ambient，禁止继续出现“engine-local 保留原始值、ambient 已清空”的分裂。 3. 作为防御性兜底，修改 `GetCurrentWorldContextObject()` / `TryGetCurrentWorldContextObject()`，在返回 engine-local world context 前再次做有效性判定；一旦发现失效对象，立即清空本地缓存并返回 `nullptr`。 4. 审查所有保存 `PreviousWorldContext` 的 scope/helper（`FAngelscriptGameThreadScopeWorldContext`、`FAngelscriptContext`、`FAngelscriptGameThreadContext`、`FAngelscriptEngineScope`），确保恢复路径全部经过新的 sanitize helper，而不是直接回写旧裸指针。 5. 增加 focused regression：构造一个 world-context 对象，建立 current/ambient 绑定后让对象失效，再触发恢复与读取路径，断言 `TryGetCurrentWorldContextObject()`、`Bind_AActor` 和 subsystem bind 都稳定得到 `nullptr`/空 world，而不是继续解引用旧对象。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelperTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果只在 getter 上做兜底而不统一 setter，仍可能留下不同调用点各自缓存旧指针的边角问题；必须优先修 setter，再补 getter 防御。 |
| 前置依赖 | 建议与 `Issue-04`、`Issue-27` 一起推进；前者统一 world-context 的 owner/GC 语义，后者收口 scope 恢复顺序。 |
| 验证方式 | 新增 GC/失效对象恢复回归，覆盖 `AssignWorldContext()`、`TryGetCurrentWorldContextObject()`、`Bind_AActor`、`Bind_Subsystems`；断言所有路径在对象失效后只返回空，不再继续向 `GetWorldFromContextObject()` 传递旧指针。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-32 | Defect | 与 `Issue-04` 配套推进，先统一 world-context 的校验入口，阻断悬空对象继续外泄 |

---

## 发现与方案 (2026-04-08 16:46)

### Issue-33：`TryGetCurrentEngine()` 会在 worker thread 上回退到 subsystem/world 解析

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `718-733`; `94-113`; `53-77`; `1674-1690`; `1795-1837`; `1889-1897` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 `A-15`，并补充可执行方案：`TryGetCurrentEngine()` 在 `ContextStack` 为空时会直接调用 `UAngelscriptGameInstanceSubsystem::GetCurrent()`（`AngelscriptEngine.cpp:718-733`），而后者内部没有任何线程边界保护，直接访问 `GEngine->GetWorldFromContextObject(...)`、`World->GetGameInstance()` 与 `GetSubsystem<>()`（`AngelscriptGameInstanceSubsystem.cpp:94-113`）。这条 fallback 已经被明确用于后台路径：`CheckGameThreadExecution()` 会在判断 “是否非法 off-thread Blueprint 调用” 之前先执行 `TryGetCurrentEngine()`（`ASClass.cpp:53-77`）；hot reload worker 线程在 `Run()` 里调用 `FAngelscriptEngine::Get()`（`AngelscriptEngine.cpp:1674-1690`）；`FAngelscriptPooledContextBase::Init()` 与析构在借还 context 时也会再次走 `TryGetCurrentEngine()`（`1795-1837`, `1889-1897`）。 |
| 根因 | current-engine resolver 把 `ContextStack` 解析、subsystem/world 解析和 ambient fallback 混在同一个 thread-agnostic helper 中，没有把 `UWorld`/`UGameInstanceSubsystem` 访问限制在 game thread。 |
| 影响 | 只要 worker thread 上的解析命中 “`ContextStack` 为空” 分支，RuntimeCore 就会在后台线程触碰 `GEngine` 与 world/subsystem 对象图。结果包括非线程安全的 UObject 访问、current-engine 解析漂移到错误 world，以及把本应只依赖显式 owner 的后台逻辑隐式耦合到 ambient world。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 拆开 thread-safe 的 engine 解析与 game-thread-only 的 subsystem fallback，禁止后台路径再隐式走 world/subsystem 反查。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 内拆分 resolver 入口，例如保留 `TryGetCurrentEngine()` 给 game thread 使用，同时新增只消费 `ContextStack`/显式 owner 的 `TryGetCurrentEngine_ThreadSafe()`，或等价地在现有实现中加入 `if (!IsInGameThread()) return FAngelscriptEngineContextStack::Peek();` 这类硬门禁。 2. 将 `UAngelscriptGameInstanceSubsystem::GetCurrent()` 明确标记为 game-thread-only API；若保留静态入口，应在实现前段加入 `check(IsInGameThread())` 或显式 `ensureMsgf`，避免后台线程静默触碰 UObject 图。 3. 审查所有后台调用点，尤其是 `FAngelscriptHotReloadThread::Run()`、`FAngelscriptPooledContextBase::Init()`/析构、`CheckGameThreadExecution()`，把它们改成显式携带 `FAngelscriptEngine*` 或 `asIScriptEngine*`，不再依赖 subsystem fallback 补 current engine。 4. 对必须允许 “无 current engine 也能工作” 的后台路径，补空值分支或显式失败日志，禁止通过 ambient world 偷拿 subsystem 继续执行。 5. 增加并发回归：在 worker thread 上构造 `ContextStack` 为空、但 ambient world 可解析到 subsystem 的场景，断言 `TryGetCurrentEngine()` 不再访问 `UAngelscriptGameInstanceSubsystem::GetCurrent()`；再验证显式传入 owner engine 的后台路径仍能正确借还 context。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 收紧 fallback 后，会暴露出一批此前依赖 ambient world 的后台调用点；这些点需要补显式 owner 参数或空值处理，否则会从“偶然工作”变成显式失败。 |
| 前置依赖 | 建议结合 `Issue-10` 一起推进；前者先收口 `ContextStack` 的线程语义，后者再切掉 off-thread 的 subsystem fallback。 |
| 验证方式 | 新增 worker-thread focused regression，覆盖 `CheckGameThreadExecution()`、context 借还与 hot reload worker 三条路径；并在日志或断言中确认后台线程不再进入 `UAngelscriptGameInstanceSubsystem::GetCurrent()`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-33 | Defect | 与 `Issue-10` 配对推进，先切掉 worker thread 上的 subsystem/world fallback |

---

## 发现与方案 (2026-04-08 16:47)

### Issue-34：hot reload 轮询线程与 game thread 裸共享 reload 队列和状态容器

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `406-410`; `418-419`; `477-479`; `1092-1112`; `1686-1689`; `2735-2777`; `2859-2894`; `4170-4186` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 `A-17`，并补充实施边界：后台 hot reload 线程会在 `Run()` 中执行 `Manager.CheckForFileChanges()` 并把 `bWaitingForHotReloadResults` 写回 `false`（`AngelscriptEngine.cpp:1686-1689`）；同一时间，game thread 的 `CheckForHotReload()` 又会读写同一个 `bWaitingForHotReloadResults`，并直接 `Append/Empty` `FileChangesDetectedForReload`、`FileDeletionsDetectedForReload`、`QueuedFullReloadFiles`（`2735-2777`）。后台扫描本身还会修改 `FileHotReloadState` 与 `FileChangesDetectedForReload`（`2859-2894`），而主线程的 `DiscardModule()` 和 reload 结果处理会并发修改 `FileHotReloadState`、`FileChangesDetectedForReload`、`FileDeletionsDetectedForReload`、`QueuedFullReloadFiles`（`1092-1112`, `4170-4186`）。头文件里这些共享状态只有 `volatile bool bWaitingForHotReloadResults`，没有任何与 hot reload 队列对应的锁或原子协议（`AngelscriptEngine.h:406-410`, `418-419`, `477-479`）。 |
| 根因 | RuntimeCore 复用了原本单线程的 hot reload 队列与状态容器，但只加了一个 `volatile` 完成位，没有把共享 `TArray` / `TSet` / `TMap` 纳入同步模型。 |
| 影响 | 这是标准意义上的并发数据竞争，不是单纯的时序抖动。结果包括 file change 丢失、重复消费、`FileHotReloadState` 损坏、删除/重命名状态错乱，以及容器在 `Add/Remove/Append/Empty` 交错时触发非确定性崩溃。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 hot reload 轮询线程改成“产生快照并移交给主线程”的单向通道，彻底停止两线程直接共享可变容器。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中引入专用同步边界，例如 `FCriticalSection HotReloadStateLock` 配合 `FHotReloadScanResult`，把 worker 线程扫描出的 `ChangedFiles`、`DeletedFiles`、`UpdatedFileState` 先写入局部结果，再一次性 swap 到共享槽。 2. 将 `bWaitingForHotReloadResults` 从 `volatile bool` 改成 `TAtomic<bool>` 或 `FEvent`/`TPromise` 驱动的完成信号，只负责发布 “本轮扫描是否结束”，不再承担容器同步语义。 3. 修改 `CheckForFileChanges()`：worker 线程只生成局部 `TArray/TMap`，在锁内做最小化状态交换；不要继续在扫描过程中直接改写 `FileHotReloadState` 与 `FileChangesDetectedForReload`。 4. 修改 game-thread 消费路径：`CheckForHotReload()`、`DiscardModule()`、reload 结果处理统一在锁内摘取/更新共享状态，然后在锁外执行编译和文件遍历，避免长时间持锁。 5. 若 `QueuedFullReloadFiles` 本质上只应由 game thread 维护，则把它从 worker 线程可见状态中剥离，明确限定只有主线程能读写；worker 线程只上报文件变化，不触碰后续 reload 队列。 6. 增加并发回归：启动 real hot reload worker，让 worker 扫描与主线程 `CheckForHotReload()`、`DiscardModule()` 并发运行，断言结果稳定且容器不再发生重复/丢失；必要时配合 TSAN/UE 并发断言或调试日志验证。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptHotReloadTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | hot reload 流程对时序敏感，如果直接给现有容器粗暴加大锁，可能把扫描和编译串成大临界区，拖慢 tick 或制造死锁；必须坚持“锁内交换快照、锁外做重活”的策略。 |
| 前置依赖 | 建议与 `Issue-15` 一起处理；前者先收口 hot reload thread 的生命周期状态机，后者再收口它与主线程之间的数据交换协议。 |
| 验证方式 | 新增 runtime hot reload 并发回归，覆盖 “worker 扫描 -> 主线程消费 -> 模块丢弃/再排队” 序列；并验证 `FileChangesDetectedForReload`、`FileDeletionsDetectedForReload`、`QueuedFullReloadFiles` 和 `FileHotReloadState` 在并发场景下没有丢项、重复项或崩溃。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-34 | Defect | 与 `Issue-15` 同步推进，先把 hot reload worker 和主线程之间的共享状态改成受控移交 |

---

## 发现与方案 (2026-04-08 16:48)

### Issue-35：non-editor 热重载轮询永远检测不到脚本删除/重命名

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp` |
| 行号 | `406-419`; `477-479`; `1615-1620`; `1670-1671`; `2749-2755`; `2859-2894`; `55-61` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 `C-06`，并补充方案细化：non-editor 开发模式下，RuntimeCore 只会启用 `StartHotReloadThread()` 与 `CheckForFileChanges()` 轮询当前仍存在的脚本文件（`AngelscriptEngine.cpp:1615-1620`, `1670-1671`, `2859-2894`）。这条轮询路径只会向 `FileChangesDetectedForReload` 写入“新增/修改”的文件，完全不会把已经从磁盘消失的条目转成 `FileDeletionsDetectedForReload`；但 `CheckForHotReload()` 明确又依赖 `FileDeletionsDetectedForReload` 和 `LastFileChangeDetectedTime` 来处理删除/重命名窗口（`2749-2755`）。更直接的对比证据是：当前仓库里对 `LastFileChangeDetectedTime` 的赋值和 `FileDeletionsDetectedForReload.AddUnique(...)` 只出现在 editor directory watcher（`AngelscriptDirectoryWatcherInternal.cpp:55-61`），RuntimeCore 自己的 non-editor 轮询实现没有任何等价逻辑。 |
| 根因 | RuntimeCore 把删除/重命名语义建模成独立队列和时间窗，但 non-editor 扫描器只实现了“枚举当前存在的文件”，没有实现“比对并产出已消失文件”与“推进最近变更时间”。 |
| 影响 | standalone/non-editor 开发模式下，删除或重命名 `.as` 文件不会触发预期的 unload/full reload。结果是旧模块和旧符号可能继续残留在运行时，rename 保护窗也不会按注释生效，因为这条路径的 `LastFileChangeDetectedTime` 永远不更新。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 non-editor 扫描器真正维护“上次已知文件集合”，从差集里产出删除/重命名事件，并同步更新时间窗。 |
| 具体步骤 | 1. 扩展 `CheckForFileChanges()`：扫描完当前 `Filenames` 后，把 `FileHotReloadState` 中已不存在于本轮结果里的条目收集为 `DeletedFiles`，将其加入 `FileDeletionsDetectedForReload`，并从 `FileHotReloadState` 中移除。 2. 当检测到新增、修改或删除时，都统一更新 `LastFileChangeDetectedTime = FPlatformTime::Seconds()`，让 `CheckForHotReload()` 的 rename/delay 窗口在 non-editor 路径上也成立。 3. 若需要更准确地区分 rename 与 delete，可在 worker 扫描结果里保留“一轮内删除 + 一轮内新增”的组合视图，并复用现有 `0.2s` 时间窗进行合并，而不是只靠 editor watcher 独占这套语义。 4. 审查 `DiscardModule()` 和 reload 成功/失败后对 `FileHotReloadState`、`FileDeletionsDetectedForReload` 的清理逻辑，确保删除事件被消费后不会在下一轮再次误报。 5. 为 non-editor 热重载增加 focused regression：构造已加载脚本后删除文件、重命名文件、以及目录移除场景，断言 `FileDeletionsDetectedForReload` 和 `LastFileChangeDetectedTime` 会被 RuntimeCore 轮询器正确推进，并最终触发预期 reload。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptHotReloadTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 删除/重命名检测引入差集计算后，如果与 `Issue-34` 的并发同步协议不同步收口，可能把新的删除事件再次暴露给 worker/game-thread 共享竞态；建议两者合并设计。 |
| 前置依赖 | 建议与 `Issue-34` 配套推进；先确定 hot reload 扫描结果的线程移交方式，再补删除/重命名语义。 |
| 验证方式 | 新增 non-editor hot reload 回归，覆盖脚本删除、脚本重命名和脚本目录删除；断言 `FileDeletionsDetectedForReload`、`LastFileChangeDetectedTime` 和最终 reload 结果都与 editor watcher 路径一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-35 | Defect | 在 `Issue-34` 的同步模型确定后立即补齐，恢复 non-editor 删除/重命名热重载语义 |

---

## 发现与方案 (2026-04-08 23:45)

### Issue-36：显式 `DesiredScriptEngine` 取到的 VM 与 world-context 写回目标脱钩

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `746-753`; `1763-1783`; `1095-1129`; `1584-1690` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 `B-05`，并补充可执行方案：`FAngelscriptContext` 与 `FAngelscriptGameThreadContext` 明明已经接收显式 `DesiredScriptEngine`，基类也按这个 VM 借/建 `asCContext`（`1763-1783`），但 world-context 写入口仍然调用 `AssignWorldContext()`。而 `AssignWorldContext()` 并不会依据 `DesiredScriptEngine` 或调用点 owner 决定目标，只会把值写回 `TryGetCurrentEngine()` 命中的“当前 wrapper”以及 ambient world（`746-753`）。`ASClass.cpp` 的构造、defaults 和函数调度路径又大量直接创建 `FAngelscriptContext Context(Object, Class->ConstructFunction->GetEngine())` / `FAngelscriptGameThreadContext Context(Object, RealFunction->GetEngine())`（`1095-1129`, `1584-1690`）。这意味着脚本实际执行所在的 VM 与 `WorldContextObject` 被修改的 wrapper 在实现上可以是两台不同的 engine。 |
| 根因 | RuntimeCore 把 world-context 路由绑定到了全局 current-engine resolver，而不是绑定到调用点已经显式提供的执行 owner；`DesiredScriptEngine` 只参与 context 池选择，不参与 world-context 所属实例判定。 |
| 影响 | 多 engine、adopted engine、clone scope 或临时 `FAngelscriptEngineScope` 交错时，某条脚本调用可能在 engine B 的 VM 上执行，却把 `WorldContextObject` 写进 engine A。后续 `TryGetCurrentWorldContextObject()`、`Bind_AActor`/`Bind_Subsystems`、异常打印和任何依赖当前 world 的逻辑都会读取到错误 wrapper 的上下文，形成跨实例串线和非确定性 world 解析。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 world-context 写回目标从“全局 current engine”改成“调用点显式 owner engine”，禁止 `DesiredScriptEngine` 与 wrapper 状态再分离。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中新增显式入口，例如 `AssignWorldContextToEngine(FAngelscriptEngine& TargetEngine, UObject* NewWorldContext)`，把 engine-local `WorldContextObject` 与 ambient world 的更新集中到一个不依赖 `TryGetCurrentEngine()` 的 helper。 2. 扩展 `FAngelscriptContext` / `FAngelscriptGameThreadContext` 构造签名，新增显式 `FAngelscriptEngine* OwnerEngine` 或等价 owner token；当调用方已经知道执行 owner 时，world-context 的设置与恢复必须走该 owner，而不是走 resolver。 3. 修改 `ASClass.cpp` 的构造、defaults 和函数调用入口，让它们在拿到 `RealFunction->GetEngine()` 之外，还显式建立对应 wrapper 的 `FAngelscriptEngineScope`，或直接把 owner engine 透传给新的 context 构造函数；若当前调用点拿不到 owner wrapper，就先补一层从 `UASFunction`/class metadata 到 wrapper 的显式映射，而不是继续偷用全局 current engine。 4. 在旧的 `AssignWorldContext()` 上增加防御性校验：当调用方传入了显式 `DesiredScriptEngine`/owner 信息却与 `TryGetCurrentEngine()` 不一致时，立即 `ensureMsgf` 并拒绝写入，避免继续静默污染另一台 wrapper。 5. 新增 focused regression：构造“current engine = A，但 `FAngelscriptContext` 明确使用 B 的 script engine/owner”的场景，断言 world-context 只会写入 B；同时覆盖 `FAngelscriptGameThreadContext` 与 `ASClass` 调度入口，确保 A 的 `WorldContextObject` 保持不变。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只改 RuntimeCore helper 而不改 `ASClass` 等主要调用点，旧入口仍会继续通过全局 resolver 写错 engine；必须沿调用链把 owner 显式透传到底。 |
| 前置依赖 | 建议结合 `Issue-03` 的 resolver 拆分一起推进；若先止血，至少先为显式 `DesiredScriptEngine` 场景加 owner-aware 写入口与断言。 |
| 验证方式 | 新增 multi-engine/world-context 回归，分别覆盖 `FAngelscriptContext`、`FAngelscriptGameThreadContext` 和 `ASClass` 调度入口；断言执行 VM 与被修改的 wrapper 始终一致，`TryGetCurrentWorldContextObject()` 不再跨实例串线。 |

### Issue-37：threaded 初始化创建出的 `primaryContext` 在收尾时被丢弃，game-thread usable-context 永远接不回来

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptobject.cpp` |
| 行号 | `821-840`; `935`; `1566`; `150-156`; `41-60`; `137-150`; `501-516` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 `A-09`，并补充执行方案：默认 non-editor threaded 初始化会在 worker thread 中把 `GameThreadTLD` 临时改成 worker-local TLS，`Initialize_AnyThread()` 再把新建 context 写进这份临时 TLS 的 `primaryContext`（`821-840`, `1566`）。但 worker 收尾时只是把临时槽清成 `nullptr` 并恢复 `GameThreadTLD = RealGameThreadTLD`，没有把新建的 `primaryContext` 回填给真实 game-thread TLS；随后 `InitializeOwnedSharedState()` 又只会从真实 `GameThreadTLD->primaryContext` 读取 `SharedState->PrimaryContext`，因此记录到的仍是空指针（`935`）。与此同时，AngelScript 的 `asGetUsableContext()` 在无 active context 时明确直接返回 `tld->primaryContext`（`150-156`），脚本对象工厂和对象操作路径也都先依赖这一入口（`41-60`, `137-150`, `501-516`）。 |
| 根因 | threaded 初始化只借用了 worker TLS 来创建 context，却没有建立一个显式的 handoff 阶段把“新 owner 的 primary context”交还给真实 game-thread TLS；`primaryContext` 的创建、持久化和可见性因此落在了三套彼此脱节的状态上。 |
| 影响 | 初始化结束后，底层 VM 虽然已经创建了 primary context，但 game thread 的 `asGetUsableContext()` 仍然只能读到 `nullptr`。后续脚本对象构造、嵌套调用和任何依赖 usable-context 快路径的运行时逻辑都会退化成额外请求新 context，甚至在请求失败时直接静默返回；同时 worker 上创建出的那份 primary context 既没挂回真实 TLS，也没被 `SharedState` 正确接管，形成可复用性和回收语义都错误的悬空资源。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 threaded 初始化生成的 primary context 显式移交回真实 game-thread TLS，并让 `SharedState` 与 `asGetUsableContext()` 读取同一份 owner 真相。 |
| 具体步骤 | 1. 重构 `Initialize()` 的 threaded 分支：worker 线程不要再直接改写全局 `GameThreadTLD`，而是用局部变量保存 `WorkerTLD` 与 `CreatedPrimaryContext`，把初始化结果作为显式输出传回主线程。 2. 在 worker 完成后、`PostInitialize_GameThread()` 之前，由 game thread 执行单一 handoff helper，例如 `InstallPrimaryContext_GameThread(asCContext* CreatedPrimaryContext)`，同时更新 `RealGameThreadTLD->primaryContext` 与 owner 的 `SharedState->PrimaryContext`。 3. 若 threaded 初始化中途失败或被取消，必须在 game thread 或 owner release 路径上显式释放 `CreatedPrimaryContext`，禁止把“未安装到 TLS 的 primary context”留给后续 shutdown 猜测处理。 4. 将 `InitializeOwnedSharedState()` 改为优先接受显式传入的 owner primary context，而不是再次从当下的 `GameThreadTLD` 快照读取；这样可避免未来再因 TLS 时序问题把空值写进 shared state。 5. 新增 focused regression：强制走 threaded 初始化，断言初始化完成后 `GameThreadTLD->primaryContext`、`SharedState->PrimaryContext` 和 `asGetUsableContext()` 全部非空且指向同一 owner engine；再覆盖一次 destroy/recreate，确保 handoff 后的 primary context 能被正确释放和复装。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只把 context 回填到 TLS、但不同时修正 `SharedState->PrimaryContext` 的来源，owner teardown 仍可能在 shared state 里持有空值或旧值，留下后续释放顺序问题。 |
| 前置依赖 | 建议与 `Issue-08` 一起推进；前者先收口 `GameThreadTLD` 的线程职责，这一条再补齐 primary-context handoff。 |
| 验证方式 | 新增 threaded initialize/usabe-context 回归，断言初始化后 `asGetUsableContext()` 与 `SharedState->PrimaryContext` 可见且一致；并在 shutdown 后验证该 primary context 被对称释放，不会跨 engine epoch 残留。 |

### Issue-38：`StaticNames` / `StaticNamesByIndex` 是跨 engine epoch 残留的进程级全局表

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | `69-70`; `1236-1250`; `1560-1562`; `2045-2054`; `2436-2441` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 `B-10`，并补充执行方案：`FAngelscriptEngine::StaticNames` 与 `StaticNamesByIndex` 在 RuntimeCore 中是进程级静态全局表（`69-70`）。预处理阶段每次遇到新名字都会向这两张表 `Emplace/Add` 新条目（`2045-2054`），而正常初始化在未命中 precompiled-data 时甚至只会额外 `Reserve(7000)`，不会做任何清空（`1560-1562`）。相对地，owner teardown 的 `Shutdown()` 只清实例级模块、root path 和 package 指针（`1236-1250`），没有任何复位这两张静态表的路径。更直接的冲突证据是，precompiled-data 恢复静态名表前显式要求 `StaticNames.Num() == 0`（`2436-2441`）。 |
| 根因 | RuntimeCore 把 static-name 索引同时当成“当前 engine epoch 的运行时状态”和“进程级常驻缓存”使用，但生命周期实现只回收实例成员，没有给这份全局表建立 epoch 结束时的清理合同。 |
| 影响 | 同进程第二次 full engine 初始化时，上一轮脚本运行留下的 static-name 索引会继续污染新 epoch；一旦下一轮改走 precompiled-data 恢复路径，就会直接命中 `check(StaticNames.Num() == 0)`。即使暂时没崩溃，旧 epoch 残留索引也会让新一轮 `GenerateStaticName()` 的编号与当前脚本集合脱节，破坏 RuntimeCore 的全局状态基线。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 static-name 注册表显式建模为“owner engine epoch 资源”，在最后一个参与者释放时统一清空。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 内新增统一 helper，例如 `ResetStaticNameRegistry()`，一次性清空 `StaticNames` 与 `StaticNamesByIndex`，并收口所有与 static-name epoch 相关的状态复位。 2. 将该 helper 挂到 owner 最终释放路径，而不是普通 wrapper 清理路径：优先放进 `ReleaseOwnedSharedStateResources(...)` 或 `bPendingOwnerRelease && ActiveParticipants == 0` 的最终 owner release 分支，确保 clone 仍存活时不会提前清空仍在使用的 static-name 索引。 3. 在 full-engine 启动前增加防御性前置条件，例如 `PreInitialize_GameThread()` 或 precompiled-data 恢复入口上显式校验“若当前没有活动 owner，则 static-name 表必须为空”；一旦不为空，优先走明确的 reset helper，而不是依赖 `check` 在更深层崩溃。 4. 审查所有写入静态名表的路径，保证新增逻辑只在当前 engine epoch 有效时发生，避免后台 hot reload 或失败恢复在 owner teardown 后继续向旧表追加条目。 5. 新增 focused regression：第一轮 full engine 通过预处理写入 static names 后完整 shutdown；第二轮切到 precompiled-data 路径，断言不会再命中 `check(StaticNames.Num() == 0)`，且新的 static-name 编号从干净基线开始。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果在 clone 或 deferred shared-state release 期间过早清空 static-name 表，会让仍存活的 wrapper 失去与已编译脚本一致的索引映射；必须把 reset 时机绑定到“最后一个参与者释放”。 |
| 前置依赖 | 建议与 `Issue-29`、`Issue-30` 同步审视所有跨 epoch 的进程级静态状态，避免只清 static names 却留下其他全局缓存继续污染新 owner。 |
| 验证方式 | 新增跨 epoch 回归：一轮预处理生成 static names，shutdown 后再走 precompiled-data 恢复；断言 `StaticNames`/`StaticNamesByIndex` 在第二轮启动前已清空，且新旧 epoch 不再共享编号状态。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-37 | Defect | 先修复 threaded initialize 的 primary-context handoff，恢复 game-thread usable-context 基线 |
| P1 | Issue-36 | Defect | 紧随其后收口显式 owner 的 world-context 写回，阻断跨实例上下文串线 |
| P1 | Issue-38 | Defect | 在 owner teardown 路径补齐 static-name 全局表清理，恢复跨 epoch 干净基线 |

---

## 发现与方案 (2026-04-08 23:58)

### Issue-39：context/debug callback 以 `Get()` 反查当前 wrapper，导致多 engine 下调试、coverage 与 diagnostics 路由到错误实例

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `5012-5023`; `5300-5310`; `5429-5562`; `835-922`; `1285` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `B-12`，并补充执行方案：`LogAngelscriptError()`、`LogAngelscriptException()`、`AngelscriptLineCallback()`、`AngelscriptStackPopCallback()` 明明已经拿到了 `asSMessageInfo*` 或 `asCContext*`，但仍统一通过 `FAngelscriptEngine::Get()` 反查“当前 wrapper”后再读 `CompilationLock`、`DebugServer`、`CodeCoverage` 与模块索引（`5012-5023`, `5300-5310`, `5429-5562`）。另一方面，`FAngelscriptDebugServer` 自己在 `Continue/StepIn/StepOut/StartDebugging/StopDebugging` 与 data-breakpoint 更新后，也多次直接调用 `FAngelscriptEngine::Get().UpdateLineCallbackState()`（`835-922`, `1285`），而不是回到其 `OwnerEngine`。这意味着 callback 的目标实例取决于“此刻 `ContextStack`/ambient 解析到了谁”，而不是“哪个 script engine/context 触发了事件”。 |
| 根因 | RuntimeCore 没有建立从 `asIScriptEngine*` / `asCContext*` 到 owner wrapper 的显式映射，导致 callback 层偷用了全局 current-engine resolver；同时编译诊断去重还额外依赖进程级 `static PreviousSection/PreviousType`，把本应 engine-local 的状态再一次做成了全局。 |
| 影响 | multi-engine、clone scope、并行编译或调试器消息交错时，异常、line callback、stack-pop、coverage 命中和 diagnostics 都可能落到错误 wrapper：A 引擎触发的脚本事件会污染 B 引擎的 `Diagnostics`、错误关闭/开启全局 line callback，或让 coverage/模块查找在错误实例上返回空结果。由于故障表象依赖当下 `Get()` 解析结果，问题会呈现强烈的非确定性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 callback 建立基于 `Context->GetEngine()` / `OwnerEngine` 的显式 owner 路由，彻底移除对 `FAngelscriptEngine::Get()` 的隐式依赖。 |
| 具体步骤 | 1. 在 `FAngelscriptOwnedSharedState` 或等价 registry 中建立 `asIScriptEngine* -> owner wrapper` 的显式映射 helper，例如 `ResolveWrapperForScriptEngine(asIScriptEngine*)` / `ResolveWrapperForContext(asIScriptContext*)`，并保证 clone/source 对同一底层 VM 的 owner 解析合同明确。 2. 修改 `LogAngelscriptError()`、`LogAngelscriptException()`、`AngelscriptLineCallback()`、`AngelscriptStackPopCallback()`：优先根据 `Message->section` 对应 VM、`Context->GetEngine()` 或传入 owner 解析目标 engine，不再调用 `FAngelscriptEngine::Get()`。 3. 将 `FAngelscriptDebugServer.cpp` 中所有 `FAngelscriptEngine::Get().UpdateLineCallbackState()` 替换为 `if (FAngelscriptEngine* OwnerEngine = GetOwnerEngine()) OwnerEngine->UpdateLineCallbackState();`，并对 owner 已失效场景补空值保护。 4. 把 `LogAngelscriptError()` 的 `PreviousSection` / `PreviousType` 从进程级 `static` 改为 engine-local diagnostics 状态，例如挂到 `FAngelscriptEngine::FDiagnostics` 或新的 `FCompilationDiagnosticState` 中，避免不同 engine 共享去重历史。 5. 新增 focused regression：构造 source/clone 或两台 full engine 并行编译/触发 line callback 的场景，断言 diagnostics、coverage 与 debug callback 只落到来源 engine；再验证调试器 `Continue/Step*` 消息不会因为当前 scope 在另一台 engine 上而改错 callback 状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 预估工作量 | L |
| 风险 | 如果 owner 解析只返回“某个共享 VM 的任意 wrapper”，仍会把 clone/source 差异保留到 callback 层；需要先定义清楚 callback 应命中 shared-state owner 还是当前活动 wrapper。 |
| 前置依赖 | 建议结合 `Issue-03` 的 resolver 拆分一起推进；至少要先确定 shared VM 与 wrapper 的 owner 映射规则。 |
| 验证方式 | 新增 multi-engine callback 回归，覆盖 diagnostics、exception、line callback 与 stack-pop；断言来源 `Context->GetEngine()` 对应的 wrapper 收到事件，且另一台 engine 的 `Diagnostics`、`DebugServer`、`CodeCoverage` 状态不被污染。 |

### Issue-40：`UpdateLineCallbackState()` 用单个 wrapper 的局部调试状态驱动进程级静态开关，clone/multi-engine 下谁最后写谁生效

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h` |
| 行号 | `628-647`; `1455`; `2848-2856`; `5429-5460`; `67-68`; `1038-1047`; `835-922`; `1285` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `B-09` 与 `B-11`，并补充实施边界：clone 创建后只共享底层 `Engine`/`SharedState`，`AdoptSharedStateFrom()` 不会把 source wrapper 上的 `DebugServer`、`CodeCoverage` 等调试状态投影给 clone（`628-647`, `2848-2856`），所以 clone 视角下这些字段天然是空。与此同时，`UpdateLineCallbackState()` 却只检查“当前这个 wrapper”的 `DebugServer`/`CodeCoverage`，然后直接覆盖第三方进程级静态开关 `asCContext::CanEverRunLineCallback` 与 `asCContext::ShouldAlwaysRunLineCallback`（`5429-5460`）；AngelScript VM 自身又在执行期间直接消费这两个全局静态位（`as_context.cpp:67-68`, `1038-1047`）。再叠加 `FAngelscriptDebugServer.cpp` 中多处调试器消息处理会重新触发 `UpdateLineCallbackState()`（`835-922`, `1285`），结果变成谁最后更新、全进程就听谁。 |
| 根因 | RuntimeCore 把“是否需要 line callback”建模成 wrapper-local 状态，但底层 VM 提供的却是进程级全局开关；同时 clone/source 共享的是一台 VM，却没有共享统一的调试/coverage 能力描述。 |
| 影响 | 只要多台 full engine、clone scope 或调试器消息交错存在，最后一次执行 `UpdateLineCallbackState()` 的 wrapper 就会替整台进程决定 line callback 行为。表现为 clone scope 可能把 source 正在使用的断点/coverage callback 关掉，或已销毁/未调试的 wrapper 把全局开关留在错误状态，直接破坏多 engine 隔离和调试正确性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 line-callback 决策从 wrapper-local 字段升级为 shared-state / 进程级聚合状态，并把 clone/source 的调试能力统一投影到同一个事实源。 |
| 具体步骤 | 1. 在 `FAngelscriptOwnedSharedState` 中新增统一调试能力描述，例如 `bDebugCallbacksRequested`、`bCoverageCallbacksRequested`、`ActiveDebuggerOwners`，让共享同一 VM 的 source/clone 都读写同一份状态，而不是各自看本地 `DebugServer`/`CodeCoverage` 指针。 2. 将 `UpdateLineCallbackState()` 重构为两层：第一层只根据 shared-state/owner registry 计算某台 VM 是否需要 line callback；第二层统一调用新的进程级 helper，例如 `RecomputeGlobalLineCallbackState()`，把所有仍存活 VM 的需求聚合后再写入 `asCContext::CanEverRunLineCallback` / `ShouldAlwaysRunLineCallback`。 3. 对 clone wrapper 明确暴露 shared debug capability 读入口，例如 `GetDebugServerShared()` 或 `HasSharedDebugger()`，禁止工具链继续把 `wrapper.DebugServer != nullptr` 当作唯一真相。 4. 在 owner shutdown、clone detach、调试开始/停止、coverage 开关变化后，统一走“更新 shared-state -> 重新聚合全局 static flags”的顺序，避免任何单个 wrapper 直接覆盖最终结果。 5. 新增 focused regression：source engine 开启调试后进入 clone scope，再触发 `UpdateLineCallbackState()` / `StartDebugging` / `StopDebugging` / coverage 切换，断言全局 line callback 开关仍与共享 VM 的真实需求一致；同时覆盖 owner shutdown 后全局 static flags 会被重新计算而不是残留旧值。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 预估工作量 | L |
| 风险 | 如果只把 `DebugServer` 指针同步给 clone、却不改全局 static 开关的聚合模型，仍然会保留“最后一次写入赢”问题；必须一起收口 shared capability 与全局重算。 |
| 前置依赖 | 建议与 `Issue-39` 配套推进；先确定 callback owner 路由，再统一 line-callback 状态来源。 |
| 验证方式 | 新增 clone/source 调试与 coverage 回归，断言 `asCContext::CanEverRunLineCallback`、`ShouldAlwaysRunLineCallback` 在多 wrapper 场景下始终反映全局真实需求，而不是当前 scope 恰好落到的那台 wrapper。 |

### Issue-41：clone wrapper 没有投影共享 VM 的模块/类型索引，`GetModule/GetClass/DiscoverTests` 在 clone 视角下天然失真

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | `311-335`; `384-390`; `628-647`; `2232-2249`; `2848-2856`; `2999-3045`; `4775-4875`; `42-49`; `320-323`; `351-354` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `B-23` 与 `D-19`，并补充执行方案：clone 创建时只共享底层 `Engine`/`SharedState`，`AdoptSharedStateFrom()` 仅复制 package、root path 和少量布尔状态（`628-647`, `2848-2856`），完全不会复制 `ActiveModules`、`ModulesByScriptModule`、`ActiveClassesByName`、`ActiveEnumsByName`、`ActiveDelegatesByName`（`384-390`）。但 wrapper 级查询 API 全部只读这些本地索引：`GetModule()` / `GetActiveModules()` / `GetModuleByFilename()` 依赖 `ActiveModules` / `ModulesByScriptModule`（`311-335`, `2999-3045`），`GetClass()` / `GetEnum()` / `GetDelegate()` 也只扫描本地缓存或本地模块集（`4775-4875`），`DiscoverTests()` 进一步遍历 `GetActiveModules()`（`2232-2249`）。更直接的证据是，多 engine 测试专门提供 `TrackNamedModule()` 后门，手工把测试模块塞进 clone 的本地索引后才继续验证（`42-49`, `320-323`, `351-354`）。 |
| 根因 | RuntimeCore 没有把“共享 VM 的脚本元数据”和“wrapper 自己的解析视图”划清边界：底层 `asIScriptEngine` 被共享了，但 wrapper 级索引仍是完全私有且空白的快照，导致 clone 在元数据层面既不像 source，也没有自己的正式投影合同。 |
| 影响 | clone 视角下会系统性丢失模块、类、枚举、委托和测试发现能力。问题不是统一崩溃，而是脚本执行看起来还能工作，但 `GetModule/GetClass/GetEnum/GetDelegate/GetModuleByFilename/DiscoverTests` 等 RuntimeCore API 会静默返回空或不完整结果，进一步破坏 hot reload、测试发现和工具链的一致性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将共享 VM 的元数据索引提升为 shared-state 正式合同，再在 wrapper 层叠加最小化的本地别名/overlay，取消 clone 的空白视图。 |
| 具体步骤 | 1. 在 `FAngelscriptOwnedSharedState` 中新增 authoritative metadata registry，例如 `SharedModulesByName`、`SharedModulesByScriptModule`、`SharedClassesByName`、`SharedEnumsByName`、`SharedDelegatesByName`；source/clone 对同一底层 VM 统一读这套注册表。 2. 将现有 wrapper-local `ActiveModules` / `ModulesByScriptModule` / `Active*ByName` 重构成 overlay：仅保存 wrapper 特有的模块名别名、临时热重载视图或 clone 私有模块，而不是承担唯一真相。 3. 修改 `CreateCloneFrom()` / `AdoptSharedStateFrom()`：clone 创建时不再从空本地表起步，而是自动接线到 shared metadata registry；对确实需要 clone-local 名称隔离的模块，增加显式 alias 层把 `MakeModuleName()` 结果映射到共享模块描述符。 4. 修改 `GetModule/GetActiveModules/GetModuleByFilename/GetClass/GetEnum/GetDelegate/DiscoverTests`，统一按“overlay -> shared registry”顺序解析，确保 clone 至少能看见共享 VM 已存在的脚本元数据。 5. 审查模块编译、discard、hot reload 和测试发现路径：所有会改变脚本元数据的地方都必须先更新 shared registry，再同步各 wrapper overlay，避免 source/clone 继续分叉。 6. 删除或弱化 `AngelscriptMultiEngineTests.cpp` 里的 `TrackNamedModule()` 测试后门，新增真实 clone 合同回归：不做任何手工回填时，clone 也应能解析 source 已加载模块及其类/枚举/委托。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | L |
| 风险 | clone 目前通过 `MakeModuleName()` 允许同名模块在不同 wrapper 下并存；如果直接把所有索引简单共享，会打破这层隔离。解决时必须先区分“共享元数据真相”和“wrapper 名称别名/overlay”，不能粗暴合表。 |
| 前置依赖 | 无；但若同时推进 `Issue-03` 的 resolver 拆分，建议一起明确 clone/source 的共享状态边界。 |
| 验证方式 | 新增 clone metadata 回归：source 先加载模块并暴露类/枚举/委托，再创建 clone，断言 clone 在不调用 `TrackNamedModule()` 的情况下即可解析同一批元数据；同时覆盖模块 discard/hot reload 后 source/clone 视图保持一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-39 | Defect | 优先修复 callback owner 路由，先阻断跨实例调试、coverage 与 diagnostics 污染 |
| P1 | Issue-40 | Architecture | 在 Issue-39 之后统一 line callback 的 shared/global 状态模型，消除 clone 与多 engine 的最后写入赢 |
| P1 | Issue-41 | Architecture | 并行推进 clone 元数据投影，恢复 `GetModule/GetClass/DiscoverTests` 等 wrapper API 的正式合同 |

---

## 发现与方案 (2026-04-09 00:09)

### Issue-42：shared-state 运行期访问器被错误包进 `WITH_DEV_AUTOMATION_TESTS`，目标配置一变就失去正式实现

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp` |
| 行号 | `564-569`; `944-999`; `912`; `1491`; `23-32`; `35-44`; `14-23` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 `C-07`，并补充实施方案：`FAngelscriptEngine` 在头文件里把 `GetTypeDatabase()`、`GetBindState()`、`GetToStringList()`、`GetBindDatabase()`、`EnsureSharedStateCreated()` 作为无条件公开的运行期成员暴露出来（`AngelscriptEngine.h:564-569`），但这些函数的正式定义却整体被包在 `#if WITH_DEV_AUTOMATION_TESTS` 里（`AngelscriptEngine.cpp:944-999`）。与此同时，正式运行时代码没有相同宏保护：`InitializeForTesting()` 与 `Initialize_AnyThread()` 都直接调用 `EnsureSharedStateCreated()`（`AngelscriptEngine.cpp:912`, `1491`），`AngelscriptBinds.cpp` / `AngelscriptType.cpp` / `AngelscriptBindDatabase.cpp` 也在无宏保护路径中直接依赖这些访问器（`23-32`, `35-44`, `14-23`）。 |
| 根因 | RuntimeCore 把“shared-state 的正式运行期入口”和“测试辅助 API”放进了同一段预处理边界，导致测试构建偶然可用，而非测试目标的真实编译合同被破坏。 |
| 影响 | 一旦 `WITH_DEV_AUTOMATION_TESTS=0` 的 target 编译到这组调用点，运行期访问器就会失去实现，形成目标配置相关的链接/集成风险。即使暂时只在测试构建中使用，这种宏边界失真也会让 RuntimeCore 的 shared-state 初始化合同无法在正式目标上被可靠验证。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 shared-state 运行期访问器从测试宏中拆出，测试宏只保留真正的 test-only helper。 |
| 具体步骤 | 1. 把 `GetTypeDatabase()`、`GetBindState()`、`GetToStringList()`、`GetBindDatabase()`、`EnsureSharedStateCreated()` 的定义移出 `#if WITH_DEV_AUTOMATION_TESTS`，恢复为正式运行期实现。 2. 将 `GetActiveParticipantsForTesting()`、`GetActiveCloneCountForTesting()`、`GetLocalPooledContextCountForTesting()`、`GetToStringEntryCountForTesting()`、`GetBindDatabaseForTesting()`、`SetUseEditorScriptsForTesting()`、`SetAutomaticImportMethodForTesting()` 保持在测试宏内，避免再把生产代码与测试 seam 混在一起。 3. 将 `EnsureSharedStateCreated()` 改名或拆分为更明确的运行期 helper，例如 `EnsureSharedRuntimeStateCreated()`，并让 `InitializeForTesting()`、`Initialize_AnyThread()`、bind/type database 获取路径统一走同一入口。 4. 审查 `AngelscriptBinds.cpp`、`AngelscriptType.cpp`、`AngelscriptBindDatabase.cpp` 的 fallback 逻辑，确保 `Legacy*` 单例只作为“当前没有 engine”时的退路，而不是继续掩盖 shared-state 初始化缺失。 5. 在构建验证中增加一个 `WITH_DEV_AUTOMATION_TESTS=0` 的编译检查，确保 RuntimeCore 不再依赖测试宏才能链接通过。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Documents/Guides/Build.md` |
| 预估工作量 | M |
| 风险 | 如果只把定义移出宏、却不重新梳理 `Legacy*` fallback 语义，可能继续保留“当前 engine 丢了也静默退回全局单例”的旧行为。 |
| 前置依赖 | 无 |
| 验证方式 | 编译至少一个 `WITH_DEV_AUTOMATION_TESTS=0` 的目标；随后运行现有 bind/type database 相关自动化，确认测试构建与非测试构建都能走同一套 shared-state 访问实现。 |

### Issue-43：`bGeneratePrecompiledData` 与 `GScriptNativeForms` 作为进程级静态状态跨 engine epoch 泄漏

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` |
| 行号 | `76`; `872`; `1425-1434`; `1132-1252`; `27-40`; `112-116`; `156-160`; `199-203` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 `A-20`，并补充执行方案：`FAngelscriptEngine::bGeneratePrecompiledData` 是进程级静态布尔（`AngelscriptEngine.cpp:76`），并在 testing/full 初始化里反复被当前 owner 的 `RuntimeConfig` 覆盖（`872`, `1425-1434`）。与此同时，`StaticJITBinds.cpp` 维护了进程级静态表 `GScriptNativeForms`（`27-40`），多个 `BindNative*()` 入口都会在 `bGeneratePrecompiledData` 为真时向其中 `Add(new FScriptNative...)`（`112-116`, `156-160`, `199-203`）。但 `Shutdown()` 从头到尾只清实例字段与 shared state（`1132-1252`），没有任何 `bGeneratePrecompiledData` 复位、`GScriptNativeForms` 清空或堆对象释放逻辑。 |
| 根因 | StaticJIT 的 native-form 注册表仍按“进程级单例缓存”设计，而 RuntimeCore 已经演进到支持多 full engine / clone / 多 epoch 的 owner 生命周期；两者之间没有建立 epoch 结束时的回收合同。 |
| 影响 | 每次启用 `bGeneratePrecompiledData` 的 owner 初始化都会把新的 native form 永久留在进程里，既造成稳定内存泄漏，也让键为旧 `asIScriptFunction*` 的映射跨 epoch 残留。后续 engine epoch 一旦复用到底层函数地址，就可能命中上一轮残留的 native form，把 StaticJIT 代码生成与当前 VM 状态错配。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 StaticJIT native-form 注册表改成 owner-aware 资源，并在 owner teardown 时对称释放。 |
| 具体步骤 | 1. 将 `bGeneratePrecompiledData` 从进程级静态开关降级为 owner/shared-state 级事实，优先挂到 `FAngelscriptOwnedSharedState` 或 `FAngelscriptStaticJIT`，禁止不同 engine epoch 通过同一个静态位相互覆盖。 2. 在 `StaticJITBinds.cpp` 中为 `GScriptNativeForms` 增加显式回收入口，例如 `ReleaseNativeFormsForScriptEngine(asIScriptEngine*)` 或 `ResetNativeFormsRegistry()`，逐个 `delete` 表中的 `FScriptFunctionNativeForm` 派生对象后再移除条目。 3. 将 native-form 注册表从“只按 `asIScriptFunction*` 键控”升级为带 owner/engine 边界的注册结构，至少保证 teardown 时能精确清理属于某一台 `asIScriptEngine` 的条目。 4. 在 `ReleaseOwnedSharedStateResources(...)` 与 owner 最终释放路径中，在 `ShutDownAndRelease()` 前先执行 native-form 回收；对于 deferred owner release，也必须在最后一个参与者退出时走同一套清理逻辑。 5. 增加 focused regression：第一轮 full engine 开启 `bGeneratePrecompiledData` 并触发若干 `BindNative*()`，shutdown 后断言 registry 已清空；第二轮重新初始化时，再断言不会读到上一轮残留条目。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果简单全量清空 registry，而没有区分仍存活的 shared VM / clone 参与者，会在 deferred owner release 场景提前删掉还在使用的 native form。 |
| 前置依赖 | 建议结合 `Issue-38` 一起审视所有跨 epoch 静态状态，但可独立推进。 |
| 验证方式 | 新增两轮 full-engine 回归：第一轮生成 precompiled/native-form 数据并 shutdown，第二轮重新初始化后断言 registry 不残留旧条目；同时在内存分析或日志中确认 teardown 后不再保留 `FScriptNative*` 堆对象。 |

### Issue-44：公开的 global-engine API 已经退化成别名与空操作，但仍被当作生命周期控制面使用

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 行号 | `461-464`; `736-779`; `31-38`; `54-64`; `233-234` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 `C-01`，并补充架构收口方案：公共头文件仍暴露 `TryGetGlobalEngine()`、`SetGlobalEngine()`、`GetOrCreate()`、`DestroyGlobal()` 这组“全局引擎” API（`AngelscriptEngine.h:461-464`），但实现已经不再提供真正的 global owner 语义：`TryGetGlobalEngine()` 只是返回 `TryGetCurrentEngine()`，`SetGlobalEngine()` 完全忽略 `InEngine` 参数，`GetOrCreate()` 直接 `checkf` 声明自己已废弃，`DestroyGlobal()` 恒定返回 `false`（`AngelscriptEngine.cpp:736-779`）。调用方却仍把它们当成生命周期控制面使用，例如测试工具用 `DestroyGlobal()` 做重置入口（`AngelscriptTestUtilities.h:31-38`），subsystem 测试用 `TryGetGlobalEngine()` 断言“当前全局主引擎”是否正确（`AngelscriptSubsystemTests.cpp:54-64`, `233-234`）。 |
| 根因 | RuntimeCore 在从旧的 global-engine 模型迁移到 `ContextStack + subsystem fallback` 解析后，没有同步删除或废弃公共 API，导致“旧职责名义”与“新实现语义”长期并存。 |
| 影响 | 后续调用方会继续围绕一套失真的 API 设计生命周期逻辑：有的调用得到的是 current-engine 别名，有的调用是 no-op，还有的调用在运行时直接 `checkf`。这不仅放大测试隔离假阳性，也让 RuntimeCore 的职责边界继续模糊，未来新增代码更容易把 resolver、owner 和 cleanup 三种概念混在一起。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 明确退役旧 global-engine API，并将调用方迁移到显式 owner / resolver 接口。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.h` 上为 `TryGetGlobalEngine()`、`SetGlobalEngine()`、`GetOrCreate()`、`DestroyGlobal()` 增加明确的 `UE_DEPRECATED` 或等价注释，说明它们不再表达 global owner 语义。 2. 选择单一方向完成收口：优先建议彻底删除这组 API 的生命周期职责，把 owner 创建/销毁统一交给 `FAngelscriptRuntimeModule`、`UAngelscriptGameInstanceSubsystem` 和显式 cleanup helper，而不是恢复一个新的隐式 global owner。 3. 迁移测试与工具调用点：把 `DestroyGlobal()` 的使用改为显式的 module/subsystem 测试 cleanup 入口，把“当前 engine”断言改为 `TryGetCurrentEngine()` 或新的 owner-aware 查询，避免继续混用“global”措辞。 4. 对短期必须保留的兼容入口，至少在实现中增加 `ensureMsgf` / 详细日志，明确提示调用方该 API 已失真，不允许继续承担真实生命周期控制。 5. 更新相关文档与测试辅助注释，消除“global engine”这套历史命名对后续维护者的误导。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Documents/Guides/Test.md` |
| 预估工作量 | M |
| 风险 | 迁移过程中如果同时保留旧名和新名，却不限制旧名调用，调用方仍可能继续走兼容层而不是完成收口。 |
| 前置依赖 | 无 |
| 验证方式 | 编译运行现有依赖这组 API 的测试，确认它们已经迁移到显式 cleanup / current-engine 查询；再检查公开头文件和文档中不再把这组入口描述成可用的 global lifecycle API。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-42 | Defect | 先修复 shared-state 访问器的宏边界，恢复 RuntimeCore 在非测试目标上的正式编译合同 |
| P1 | Issue-43 | Defect | 随后收口 StaticJIT 的进程级静态状态，阻断跨 engine epoch 的 native-form 泄漏与错配 |
| P2 | Issue-44 | Architecture | 在高优先级缺陷止血后清理 public API 语义，移除失真的 global-engine 生命周期入口 |

---

## 发现与方案 (2026-04-09 00:27)

### Issue-45：threaded 初始化仍把 `UObject` 反射绑定和全局 delegate 接线留在 worker thread

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`; `../UERelease/Engine/Source/Runtime/Core/Public/Delegates/Delegate.h`; `../UERelease/Engine/Source/Runtime/Core/Public/Delegates/MulticastDelegateBase.h` |
| 行号 | `809-848`, `1382-1495`, `1628-1640`; `716-724`, `1034-1047`; `286-294`; `213-217`; `27-42` |
| 问题 | `ShouldInitializeThreaded()` 在 non-editor 下默认返回 true（`AngelscriptEngine.cpp:809-816`），`Initialize()` 随后把整段 `Initialize_AnyThread()` 投到 worker thread 执行（`825-848`）。但这段 worker 代码并不只是做纯 AngelScript VM bootstrap：它会 `NewObject<UPackage>` 创建 `/Script/Angelscript` 包（`1382-1386`），执行 `BindScriptTypes()`（`1491-1495`），而实际 bind 代码又会直接 `FindObject<UClass>`、遍历 `TObjectRange<UClass>`（`Bind_BlueprintType.cpp:716-724`, `1034-1047`; `Bind_AActor.cpp:286-294`）。更进一步，worker 末尾还会直接把 lambda/raw 回调注册到 `FCoreDelegates::OnPostEngineInit` 与 `OnGetOnScreenMessages`（`1628-1640`）；而 UE 默认 `DECLARE_MULTICAST_DELEGATE` 走的仍是非 thread-safe 的 multicast delegate 基类（`Delegate.h:213-217`; `MulticastDelegateBase.h:27-42`）。这说明除已单独追踪的 `LoadModule` 问题外，threaded 初始化本身还在 worker thread 上执行大量 UObject/全局 delegate 侧效应。 |
| 根因 | RuntimeCore 目前只按函数名把阶段粗分成 `PreInitialize_GameThread()` 与 `Initialize_AnyThread()`，没有把真正的 thread-affinity 合同细化到“纯 VM 工作”“UObject/反射工作”“全局 delegate 接线”三个层次。结果是只要逻辑仍留在 `Initialize_AnyThread()`，就会被默认 non-editor threaded 路径整体搬到 worker thread。 |
| 影响 | 即使后续修掉 `LoadModule`，non-editor 启动仍会在 worker thread 里触碰 UObject 图、反射枚举和全局 delegate 容器。故障形态不是单点崩溃，而是启动期不稳定：绑定阶段可能读取到不一致的类型视图，delegate 注册与广播之间可能出现竞态，最终表现为 packaged/commandlet 环境下偶发初始化失败、缺绑定或难以复现的线程亲和性问题。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 threaded 初始化拆成显式的 game-thread 接线阶段和 worker 纯 VM 阶段，禁止 `Initialize_AnyThread()` 再直接碰 UObject/全局 delegate。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中新增显式阶段 helper，例如 `InitializeUObjectState_GameThread()`、`BindScriptTypes_GameThread()`、`RegisterRuntimeDelegates_GameThread()`，把 `NewObject<UPackage>`、`BindScriptTypes()` 和 `FCoreDelegates::*` 注册整体迁出 `Initialize_AnyThread()`。 2. 将 `Initialize_AnyThread()` 收敛为纯 AngelScript/IO/编译阶段，只保留不依赖 UObject 图和非 thread-safe 全局容器的逻辑；若某段 bind 逻辑既做反射扫描又做纯 VM 注册，就继续向下拆分成 game-thread gather + worker consume 两步。 3. 在新 helper 前加显式线程断言，例如 `check(IsInGameThread())`，并在 `Initialize()` 中把调用时序改为 `PreInitialize_GameThread -> UObject/bind/delegate game-thread 阶段 -> worker compile/bootstrap -> PostInitialize_GameThread`。 4. 审查 `Bind_BlueprintType`、`Bind_AActor` 以及其他 `Bind_*` 实现，凡是使用 `TObjectRange`、`FindObject`、`GetDefault`、`UClass` 遍历或全局 delegate 注册的路径，都统一归到 game-thread 阶段，不允许继续通过 `BindScriptTypes()` 被 worker 间接执行。 5. 增加 forced-threaded 回归：强制 non-editor threaded 初始化，并通过 test seam 记录 package 创建、bind 扫描与 delegate 注册所在线程，断言这些步骤都发生在 game thread，而 worker 只负责 compile/VM 初始化。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | L |
| 风险 | 如果简单把整个 `BindScriptTypes()` 全搬回 game thread，可能显著拉长 non-editor 启动时间；需要拆出真正有线程前置条件的 UObject/反射部分，保留纯 VM 部分的并行收益。 |
| 前置依赖 | 建议与已记录的 `Issue-31` 一起处理，但不依赖其先完成。 |
| 验证方式 | 强制 threaded 初始化并记录线程 ID；确认 package 创建、`BindScriptTypes()` 中的反射遍历和 `FCoreDelegates` 注册全部发生在 game thread，且 packaged/non-editor 启动不再出现 worker-thread bind/接线。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-45 | Defect | 与 `Issue-31` 成组推进，先把 threaded 初始化里的 UObject/delegate 接线移回 game thread |

---

## 发现与方案 (2026-04-09 00:27)

### Issue-46：固定 `/Script/Angelscript*` 包名让多 full engine 共享同一 package 身份，破坏实例隔离

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`; `../UERelease/Engine/Source/Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp` |
| 行号 | `878-882`, `1382-1386`; `477-515`; `3655-3708` |
| 问题 | full engine 的 testing 与 production 初始化都会无条件创建同名包 `/Script/Angelscript` 与 `/Script/AngelscriptAssets`（`AngelscriptEngine.cpp:878-882`, `1382-1386`）。与此同时，RuntimeCore 现有自动化明确允许同一进程同时存在两台 full owner（`AngelscriptMultiEngineTests.cpp:477-515`）。UE 对显式命名 `NewObject` 的底层分配逻辑会先 `StaticFindObjectFastInternal` 查现有对象；若同名对象已存在，就直接在原地址上“replace existing object without affecting the original's address or index”（`UObjectGlobals.cpp:3655-3708`）。这意味着第二台 full engine 不是拿到自己的 package namespace，而是在同一个全局 package 身份上继续重构/复用。 |
| 根因 | RuntimeCore 一方面把 full engine 设计成可并存的独立 owner，另一方面又把 package namespace 写死成全局常量，没有把“canonical runtime package”与“测试/隔离 owner package”区分开。结果 owner 隔离只停留在 VM/shared-state 层，UObject outer/package 层却仍是单例。 |
| 影响 | 多 full engine 场景下，类 outer、资产 outer、package flag、root 状态和基于 package 的任何缓存都会在 owner 之间串线。更严重的是，这个问题会放大已经记录的 package 生命周期缺陷：即使后续补上 `RemoveFromRoot()`，固定包名仍会让第二个 owner 复用第一个 owner 的 package 身份；若再叠加 threaded 初始化，worker thread 甚至可能去重构一个已存在的 package。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 明确 package namespace 策略：production canonical owner 继续单例，测试/隔离 full engine 使用唯一 package 名称，不再共享全局 `/Script/Angelscript*` 身份。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中新增 package naming helper，例如 `MakeRuntimePackageNames()`，根据 `CreationMode`、`InstanceId` 和是否为 production singleton 生成包名。 2. 对 module/subsystem 的正式 owner 保留 canonical 包名 `/Script/Angelscript` 与 `/Script/AngelscriptAssets`，但在创建前增加显式单例保护：若当前进程已经存在 canonical owner，则拒绝第二次创建，避免无声重构。 3. 对 `CreateTestingFullEngine()`、隔离测试和任何允许多 full owner 并存的路径，改用 owner-scoped 唯一包名，例如 `/Script/Angelscript_Full_<InstanceId>`，并把相关 helper/断言同步更新为读取实例自己的 package。 4. 审查 `GetPackage()`、`GetAssetPackage()`、`MakeModuleName()`、script asset 生成与任何依赖 package 路径的代码，确保它们不再把“所有 full engine 都共享同一 canonical 包”当作隐含前提。 5. 增加 focused regression：同进程创建两台 full engine，断言两者 `AngelscriptPackage`/`AssetsPackage` 指针与名字都不同；同时覆盖 production canonical 路径，断言第二个 canonical owner 会被明确拒绝而不是静默重构。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 若直接给所有路径都换成唯一包名，可能破坏依赖 canonical `/Script/Angelscript` 的运行时约定和外部工具；必须区分 production singleton 与测试/隔离 owner，不能一刀切。 |
| 前置依赖 | 建议与已记录的 `Issue-06` 一起评审 package 生命周期，但可先独立收口命名策略。 |
| 验证方式 | 新增 multi-full-engine package 隔离回归，断言两台 full owner 的 package identity 不再相同；再覆盖 canonical production owner 重复创建场景，确认会显式失败而不是复用旧 package。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-46 | Architecture | 紧随 package 生命周期修复评审，先收口 full owner 的 package namespace 合同 |

---

## 发现与方案 (2026-04-09 00:27)

### Issue-47：`bForcedExit` 路径请求退出后仍继续安装运行期 watcher 与全局回调

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `1571-1640`; `1658-1700`; `1132-1151` |
| 问题 | `Initialize_AnyThread()` 在 static JIT 导出或 `bGeneratePrecompiledData` 打开时，会先写输出文件并把 `bForcedExit = true`（`1571-1589`），随后仅调用 `FPlatformMisc::RequestExitWithStatus(false, 0)`（`1605-1609`），却没有提前返回。函数之后仍继续分配 `HotReloadTestRunner`（`1611-1613`）、按 development mode 决定是否启动 hot reload checker thread（`1615-1620`, `1658-1700`），再向 `OnPostEngineInit` / `OnGetOnScreenMessages` 注册回调并更新 line-callback 状态（`1627-1640`）。与此同时，`Shutdown()` 的清理仍是面向正常运行时设计，只在进入 teardown 时删除 `HotReloadTestRunner`（`1147-1151`），并未把“导出后立即退出”视为一条独立的最小化生命周期。 |
| 根因 | RuntimeCore 没有把“导出产物后退出进程”的 batch 模式和“完成 bootstrap 后进入长期运行”的 interactive 模式拆成两个终止条件；`RequestExitWithStatus()` 被当作 side effect，而不是初始化流程的真正终点。 |
| 影响 | 预编译数据/JIT 导出命令即使已经决定退出，仍会短暂进入一段半初始化运行期：可能启动 watcher、注册全局 delegate、修改 line-callback 全局状态，然后立刻又进入退出/销毁。这会放大已知的 teardown 风险，让导出型命令把不必要的 runtime side effect 带进退出窗口，表现为额外日志噪音、退出时的无意义线程/回调活动，甚至把真正的导出失败淹没在后续 teardown 噪声里。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 export-only 模式建立显式终止分支，请求退出后立即结束初始化，不再接线长期运行时设施。 |
| 具体步骤 | 1. 在 `Initialize_AnyThread()` 中提炼 helper，例如 `HandleForcedExitAfterCompilation()`，在 `RequestExitWithStatus()` 之前完成导出模式必需的状态持久化，随后立刻 `return`，禁止继续执行 hot reload/debug/diagnostics 接线。 2. 将 `HotReloadTestRunner` 创建、`StartHotReloadThread()`、`OnPostEngineInit`/`OnGetOnScreenMessages` 注册、`UpdateLineCallbackState()` 收口到单独的 `FinalizeInteractiveRuntime()` helper，只在 `!bForcedExit` 时调用。 3. 审查 `PostInitialize_GameThread()` 与 `InitializeOwnedSharedState()`，确认 export-only 提前返回后不会再对外宣称 runtime 已进入可交互状态；必要时增加专门的 `bExitRequestedAfterCompile` 状态，避免后续调用把这次初始化误判成成功运行。 4. 为导出模式补最小 cleanup：如果在设置 `bForcedExit` 前已经分配了临时对象，应在 return 前按 export-only 语义回收，而不是依赖正常 `Shutdown()` 去猜测。 5. 增加 focused regression：开启 `bGeneratePrecompiledData` 或 static JIT 输出模式，断言初始化后不会创建 `HotReloadTestRunner`、不会启动 hot reload thread、不会注册 `OnGetOnScreenMessages`，且退出请求仍然被正确发出。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果过早 return，却没有保留导出模式真正需要的统计/持久化步骤，可能导致产物写出完成但某些尾部状态没有刷新；必须先明确 export-only 的最小成功合同。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 export-mode 回归，验证请求退出后不再注册 watcher/delegate；同时检查导出的 `PrecompiledScript.Cache`/JIT 输出仍完整生成。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-47 | Defect | 在高优先级生命周期修复后处理，先把 export-only 初始化与 interactive runtime 接线分离 |

---

## 发现与方案 (2026-04-09 00:36)

### Issue-48：`Create()` 仍暴露成通用工厂，但实际固定走 testing lifecycle

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp` |
| 行号 | `605-625`; `183-199`; `280-287` |
| 问题 | 承接 `RuntimeCore_Analysis.md` 的 `C-04`，当前公开工厂 `FAngelscriptEngine::Create()` 仍按“通用 full-engine 入口”暴露，但实现只是直接转发到 `CreateTestingFullEngine()`，后者固定执行 `InitializeForTesting()`（`AngelscriptEngine.cpp:605-625`）。测试也已经把这层失真固化成 contract：`MultiEngine.Create.Full` 继续把 `Create()` 当 full-engine 构造入口使用（`AngelscriptMultiEngineTests.cpp:183-199`），而 `Create.LegacyAliasSkipsProductionDirectorySetup` 甚至明确断言它不会走 production script-root/setup 路径（`AngelscriptDependencyInjectionTests.cpp:280-287`）。 |
| 根因 | RuntimeCore 从旧的“直接构造 engine”模式迁移到 `RuntimeModule + Subsystem` owner lifecycle 后，没有同步退役旧工厂 API；`Create()` 这个名字仍保留 production 语义，但实现已经退化成 test-only alias。 |
| 影响 | 任何按 API 名称理解而调用 `Create()` 的代码，都会拿到一台 testing 语义的 engine，而不是经过正式 owner/bootstrap 合同的 runtime engine。这样会继续模糊“测试构造”“生产 owner”“正式初始化”三条边界，并让后续调用方在不知情的情况下绕开 RuntimeModule/subsystem 生命周期。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 明确退役失真的 `Create()`，强制调用方在“test-only full engine”与“正式 owner lifecycle”之间做显式选择。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.h`/`.cpp` 上把 `Create()` 标记为 deprecated compatibility entry，并在注释与日志里明确它等价于 `CreateTestingFullEngine()`，不是 production runtime 工厂。 2. 审查现有调用点，优先把测试和工具代码全部改成显式调用 `CreateTestingFullEngine()` 或 `CreateForTesting(..., EAngelscriptEngineCreationMode::Full)`，不再使用含混的 `Create()` 名称。 3. 对 production 路径给出唯一正规入口：owner 创建只能走 `FAngelscriptRuntimeModule::InitializeAngelscript()` 或 `UAngelscriptGameInstanceSubsystem::Initialize()`；如果确实需要无 host 的正式 full-engine 构造，应新增名字明确的新 API，例如 `CreateStandaloneOwner()`，并让它走真实 `Initialize()` 而不是 testing path。 4. 更新相关测试断言与 helper 命名，把“legacy alias”语义压缩到兼容层，避免新增用例继续把 `Create()` 当正式 API。 5. 在完成调用点迁移后，评估删除 `Create()` 或将其限制到测试构建，彻底消除 public lifecycle API 的歧义。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDependencyInjectionTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/TESTING_GUIDE.md` |
| 预估工作量 | M |
| 风险 | 如果直接把 `Create()` 改成真实 production 初始化，现有依赖 testing 语义的自动化会整体漂移；因此需要先迁移调用点，再决定删除还是保留兼容层。 |
| 前置依赖 | 无 |
| 验证方式 | 更新相关测试后，确认所有 test-only 调用点都已显式使用 testing API；同时增加一条 focused regression，断言生产 owner 仍只通过 RuntimeModule/subsystem 建立，而 `Create()` 不再被误认为正式 runtime 工厂。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-48 | Architecture | 在高优先级生命周期缺陷止血后执行，先收口公开工厂语义 |

---

## 发现与方案 (2026-04-09 00:37)

### Issue-49：Blueprint library namespace 规则被做成进程级静态状态，multi-owner 场景会互相污染

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | `78-80`; `145-147`; `1291-1308`; `123-164`; `483-515` |
| 问题 | `FAngelscriptEngine` 把 `bUseScriptNameForBlueprintLibraryNamespaces`、`BlueprintLibraryNamespacePrefixesToStrip`、`BlueprintLibraryNamespaceSuffixesToStrip` 定义成进程级静态字段（`AngelscriptEngine.cpp:78-80`, `AngelscriptEngine.h:145-147`）。每次 full-owner 初始化进入 `PreInitialize_GameThread()` 时，又会从 `UAngelscriptSettings` 重新覆盖并排序这三项静态配置（`AngelscriptEngine.cpp:1291-1308`）。后续 namespace 计算却直接从这些静态字段读取，而不是从某台具体 engine 读取（`Helper_FunctionSignature.h:123-164`）。与此同时，RuntimeCore 自己的测试已经明确允许同一进程同时存在两台 full owner（`AngelscriptMultiEngineTests.cpp:483-515`）。这意味着第二台 full owner 初始化后，会无声改写第一台 owner 后续 bind/signature 计算使用的 namespace 规则。 |
| 根因 | RuntimeCore 把本应属于 engine 初始化快照的一组绑定策略，错误地建模成了进程级全局可变状态；multi-owner 能力已经存在，但这组配置没有 owner 边界。 |
| 影响 | multi-owner、隔离测试或未来的 host/tooling 场景下，先创建的 engine 无法保持自己的 namespace policy。后续任何 reflective bind、热重载、Blueprint callable signature 生成都会读取“最近一次初始化的 owner 配置”，导致 engine 之间的脚本命名空间规则串线，破坏 multi-engine 隔离合同。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 namespace policy 从进程级静态变量下沉到 engine-owned snapshot，并要求 helper 显式消费 owner 配置。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 内引入实例级配置快照，例如 `FBlueprintNamespacePolicy` 或更通用的 `FAngelscriptEffectiveSettings`，至少包含这三项 namespace 规则。 2. 修改 `PreInitialize_GameThread()`：从 `UAngelscriptSettings` 读取后写入当前 engine 的实例快照，不再覆盖进程级静态字段；如果 clone 需要共享 source policy，则在 `AdoptSharedStateFrom()` 或 clone 构造阶段显式复制这份快照。 3. 重构 `Helper_FunctionSignature::GetScriptNamespaceForClass(...)` 及相关 bind 调用点，使其接收显式的 engine/policy 引用，而不是直接读取 `FAngelscriptEngine` 的静态变量。 4. 为兼容旧调用点，可短期保留静态 accessor，但内部必须委托到 `TryGetCurrentEngine()` 的实例快照，并在没有 current engine 时记录明确日志，禁止再把静态数组当作真实配置源。 5. 新增 multi-owner focused regression：构造两台 full owner，并通过测试 seam 为它们安装不同的 namespace policy，断言第二台 owner 初始化后不会改变第一台 owner 后续 signature 生成结果。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 预估工作量 | M |
| 风险 | helper 签名改造会波及多个 bind 调用点；如果短期仍保留静态兼容层，但不限制新代码继续读取静态字段，污染问题会反复出现。 |
| 前置依赖 | 无 |
| 验证方式 | 新增双 full-owner namespace policy 回归，验证 owner A 初始化后的 namespace 结果不会被 owner B 覆盖；同时回归现有 bind config 测试，确认单 owner 行为保持不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-49 | Architecture | 在多 owner 生命周期修复期间一并处理，先把进程级 namespace 配置收回到 owner 边界 |

---

## 发现与方案 (2026-04-09 00:40)

### Issue-50：runtime settings 同时存在 snapshot 与 live-singleton 两套读取语义，owner 生命周期内会出现配置漂移

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp` |
| 行号 | `1291-1308`; `1931-1941`; `2851`; `5566-5580`; `687-694`; `610-626` |
| 问题 | RuntimeCore 对 `UAngelscriptSettings` 的读取语义前后不一致。初始化阶段 `PreInitialize_GameThread()` 会把部分设置快照到当前 engine 或静态状态里，例如 `bUseAutomaticImportMethod` 和 namespace policy（`AngelscriptEngine.cpp:1291-1308`）；clone 创建时也只是把 `ConfigSettings` 指针直接复制过去（`2851`）。但运行期很多路径又继续直接读取 `ConfigSettings` 或 `UAngelscriptSettings::Get()`：`CollectDisabledBindNames()` 每次动态读 `DisabledBindNames`（`1931-1941`），`AngelscriptLoopDetectionCallback()` 每次 line callback 都动态读 `EditorMaximumScriptExecutionTime`（`5566-5580`），`Bind_BlueprintType.cpp` 运行时再读 `StaticClassDeprecation`（`687-694`），`Bind_Primitives.cpp` 甚至直接从默认 settings 决定 primitive type 名称（`610-626`）。结果是同一台 engine 在生命周期内同时依赖“初始化时快照值”和“当前全局单例值”。 |
| 根因 | RuntimeCore 没有定义统一的 engine-effective settings 边界，导致一部分设置被当成 owner 初始化快照，另一部分设置又被当成全局实时配置直接读取；clone/multi-owner 路径因此只能共享同一个 mutable default object，而不是拥有自己的稳定配置视图。 |
| 影响 | 这会让 engine 的行为在不重建 owner 的情况下发生局部漂移，而且漂移还是不一致的：有些逻辑继续使用旧快照，有些逻辑已经切到新全局值。对 multi-owner 支持来说，这意味着 owner A 初始化完成后，owner B 或 editor settings 的后续变化仍可能改写 A 的 bind、loop detection 或 type naming 语义，破坏“每台 engine 拥有确定配置边界”的合同。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 建立单一的 engine-effective settings snapshot，禁止 RuntimeCore 在运行期直接混读 `UAngelscriptSettings` 单例。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中新增实例级 `FAngelscriptEffectiveSettings`（或等价命名），统一收纳 RuntimeCore 在运行期会消费的 settings：automatic imports、namespace policy、disabled binds、static-class mode、float/double policy、loop timeout 等。 2. 在 `PreInitialize_GameThread()` 中一次性从 `UAngelscriptSettings` 填充这份 snapshot，并明确哪些字段属于 clone 共享、哪些属于 full-owner 独立；clone 创建时复制或共享这份快照，而不是只复制 `ConfigSettings` 指针。 3. 将 `CollectDisabledBindNames()`、`AngelscriptLoopDetectionCallback()`、`Bind_BlueprintType.cpp`、`Bind_Primitives.cpp` 等路径改为只读取当前 engine 的 `EffectiveSettings`，不再直接调用 `UAngelscriptSettings::Get()` / `GetDefault<>()`。 4. 对确实需要热更新的少数字段，建立显式的 settings-reload 流程，例如 `ApplyUpdatedSettings(const FAngelscriptEffectiveSettings&)`，由 owner 主动触发并统一更新，而不是让调用点各自偷读单例。 5. 新增 focused regression：先创建 owner A 并记录关键 settings 行为，再修改默认 settings 或初始化 owner B，断言 owner A 的 disabled binds、loop timeout 和 bind/type naming 语义保持稳定；同时验证显式 reload 路径能按预期更新。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 预估工作量 | L |
| 风险 | 把 live singleton 读取改成 snapshot 后，少数当前依赖“改设置立即生效”的编辑器行为会改变；需要显式区分哪些字段允许热更新，哪些字段必须重建 engine 才生效。 |
| 前置依赖 | 建议结合 `Issue-49` 一并推进，先把 namespace policy 从静态态移入 snapshot，再扩展到其余 settings。 |
| 验证方式 | 新增 settings 漂移回归：owner A 初始化后修改默认 settings 并创建 owner B，断言 owner A 行为不变；再覆盖显式 reload 路径，确认只有被允许热更新的字段会发生改变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-50 | Architecture | 紧接 `Issue-49` 评审，把 runtime settings 合同统一成 owner snapshot |

---

## 发现与方案 (2026-04-09 00:44)

### Issue-51：`UAngelscriptGameInstanceSubsystem` adopt 外部 engine 时只保存裸指针，生命周期失效后仍会继续解引用

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `12-29`; `34-50`; `81-85`; `208-210`; `451-452`; `637-638`; `35-38`; `176-179` |
| 问题 | `UAngelscriptGameInstanceSubsystem::Initialize()` 在 adopt 分支直接把 `FAngelscriptEngine::TryGetCurrentEngine()` 的返回值存进成员 `PrimaryEngine`（`AngelscriptGameInstanceSubsystem.cpp:17-18`），随后整个 subsystem 生命周期都通过这个裸指针执行 `Tick()` 和 `Shutdown()`（`81-85`, `34-45`）。但 RuntimeCore 自己已经证明“跨 owner 持有 engine 需要生命周期令牌”：clone 通过 `SourceLifetimeToken` 和 `GetSourceEngine()` 判活（`AngelscriptEngine.h:208-210`, `451-452`; `AngelscriptEngine.cpp:637-638`）。subsystem 这里没有任何等价保护。外部 owner 消失时也不会通知 subsystem，例如 runtime module 在 `ShutdownModule()` / `ResetInitializeStateForTesting()` 里会直接 `OwnedPrimaryEngine.Reset()`（`AngelscriptRuntimeModule.cpp:35-38`, `176-179`）。 |
| 根因 | subsystem 把“当前解析到的 engine”当成可长期保存的稳定 owner，而不是一次解析结果；实现没有复用 RuntimeCore 已有的 lifetime-token 模式，也没有为 adopted engine 建立失效回调或弱引用语义。 |
| 影响 | 只要 subsystem 初始化命中的是临时 scope、module-owned engine 或其他外部 owner，后续 owner teardown 后 `PrimaryEngine` 就会变成悬空指针。之后 subsystem 的 `Tick()`、`Deinitialize()` 仍会继续调用 `ShouldTick()` / `Shutdown()`，故障形态包括 use-after-free、错误关闭外部 owner、以及把短生命周期 wrapper 错当成 `UGameInstance` 级主引擎长期推进。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 adopted engine 引入显式生命周期句柄，subsystem 不再长期保存无校验裸指针。 |
| 具体步骤 | 1. 在 `UAngelscriptGameInstanceSubsystem` 中新增 adopted-engine 生命周期状态，例如 `TWeakPtr<FAngelscriptEngineLifetimeToken> AdoptedEngineLifetimeToken`，并在 adopt 分支同步保存 `PrimaryEngine` 与对应 token；若当前 engine 无法提供有效 token，则拒绝 adopt 并改走 `OwnedEngine` 创建路径。 2. 在 `FAngelscriptEngine` 上补一个公开只读入口，例如 `GetLifetimeToken()` 或 `IsLifetimeValid()`，避免 subsystem 直接依赖 friend 访问内部字段。 3. 将 subsystem 的所有解引用点统一改成 `ResolvePrimaryEngine()` helper：先检查 `PrimaryEngine != nullptr`，再验证 adopted token 仍有效；失效时立即把 `PrimaryEngine`、`bOwnsPrimaryEngine`、tick owner 计数和相关 resolver 状态清空，并返回 `nullptr`。 4. 在 `Deinitialize()` 中区分“owned engine teardown”和“adopted engine detach”：只有 `bOwnsPrimaryEngine == true` 时才允许 `Shutdown()`；adopted 路径只做解绑，不得关闭外部 owner。 5. 新增两条 focused regression：一条让 subsystem adopt runtime-module owner 后先销毁 module，再断言 subsystem `Tick()`/`Deinitialize()` 不会再解引用旧 engine；另一条让 subsystem 处于临时 `FAngelscriptEngineScope` 中 adopt clone/full wrapper，scope 结束后断言 subsystem 会自动失效或回退到自有 owner。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接让 adopted engine 失效后回退到 `OwnedEngine`，可能改变现有“静默继续使用外部 engine”的测试假设；需要先把 detach 语义和 owner 切换语义拆清楚。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 adopted-engine lifecycle 回归，覆盖 module-owned owner 被销毁和临时 scope 结束两条路径；断言 subsystem 后续 `Tick()`、`GetEngine()`、`Deinitialize()` 都不会继续解引用失效 engine。 |

### Issue-52：`ActiveTickOwners` 作为进程级总开关，会让一个 subsystem 压制其他 engine 的 fallback tick

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 行号 | `8`; `26-29`; `34-37`; `116-118`; `186-197`; `61`; `234`; `299` |
| 问题 | `UAngelscriptGameInstanceSubsystem` 用静态 `int32 ActiveTickOwners` 统计“当前是否存在任何 tick owner”（`AngelscriptGameInstanceSubsystem.cpp:8`, `26-29`, `34-37`），`HasAnyTickOwner()` 也只是返回这个全局计数是否大于 0（`116-118`）。runtime module 的 fallback tick 决策完全依赖这一全局布尔门（`AngelscriptRuntimeModule.cpp:186-197`）：只要任意 subsystem 存活，模块就不再 tick 当前解析到的 engine。现有测试也只验证这个总开关本身会翻转（`AngelscriptSubsystemTests.cpp:61`, `234`, `299`），没有区分具体是哪台 engine 被某个 subsystem 拥有。 |
| 根因 | RuntimeCore 把“某台 engine 是否已经由 subsystem 承担 tick 责任”简化成了“进程内是否存在任何 subsystem tick owner”，导致 owner 关系按进程聚合，而不是按 engine 或 world 边界建模。 |
| 影响 | multi-owner 场景下，只要 world A 的 subsystem 拿到一台 engine，world B 或 runtime module 自己维护的另一台 engine 的 fallback tick 也会被一并熄火。结果不是重复 tick，而是错误地不 tick：后者的热重载检查、定时轮询和其他依赖 `ShouldTick()` 的运行期维护都会被无关 subsystem 阻断。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 tick ownership 从进程级总开关改成按 engine 解析的注册表，只屏蔽被 subsystem 真正接管的那台 engine。 |
| 具体步骤 | 1. 去掉 `ActiveTickOwners` 这一静态全局计数，改为维护 engine 级 registry，例如 `TMap<TWeakPtr<FAngelscriptEngineLifetimeToken>, int32>` 或等价的 owner 集合，键必须能标识具体 engine 且能随生命周期失效。 2. 在 subsystem `Initialize()`/`Deinitialize()` 中注册和注销自己接管的那台 engine；adopted/owned 两条路径都要走同一套 registry helper，避免状态分叉。 3. 将 `HasAnyTickOwner()` 替换成显式查询，例如 `HasTickOwnerForEngine(const FAngelscriptEngine&)` 或 `IsEngineTickOwned(...)`，runtime module 在 `TickFallbackPrimaryEngine()` 中先解析当前 engine，再只对该 engine 做 owner 判断。 4. 若当前 engine 已失效或 registry 中存在脏记录，查询 helper 必须在返回前顺带清理失效 token，避免 owner 计数永久卡住。 5. 新增 multi-owner 回归：让 subsystem 接管 engine A，同时让 runtime module fallback 面向 engine B，断言 A 不会被重复 tick，但 B 仍会继续 tick；再覆盖 subsystem deinit 后 registry 自动恢复的路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 registry 键仍然使用裸 `FAngelscriptEngine*`，会把当前的生命周期问题重新带进新设计；engine 级 gate 必须和 Issue-51 的 lifetime 句柄一起落地。 |
| 前置依赖 | 建议结合 Issue-51 一起推进，先为 adopted engine 建好可判活的 token，再改 tick ownership registry。 |
| 验证方式 | 新增多 owner tick 回归，验证 subsystem owner 只屏蔽自己的 engine；同时回归现有 fallback tick 用例，确认单 owner 场景行为不变。 |

### Issue-53：`AssetManager` 缺席的初始化 fallback 不会完成 `bCompletedAssetScan`，后续测试发现/热重载能力被永久降级

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `2198-2218`; `2481-2489`; `4143-4153` |
| 问题 | `OnPostEngineInit` 回调里，只有 `UAssetManager::GetIfInitialized()` 成功时，`OnCompletedInitialScan` lambda 才会同时执行 `DiscoverTests()` 和 `bCompletedAssetScan = true`（`AngelscriptEngine.cpp:2201-2210`）。如果 `AssetManager == nullptr`，代码只记录 warning 后直接 `DiscoverTests()`（`2213-2217`），却没有任何地方再把 `bCompletedAssetScan` 置真。后续两条功能路径又都把这个布尔值当成硬门槛：hot reload test 准备要求 `GEngine && bCompletedAssetScan && HotReloadTestRunner != nullptr`（`2481-2489`），post-compile 增量测试发现也要求 `IsInitialCompileFinished() && bCompletedAssetScan`（`4143-4153`）。 |
| 根因 | RuntimeCore 把“AssetManager 初始扫描完成”与“测试发现 barrier 已满足”混成了同一个布尔状态，但 fallback 分支实际上已经选择了“不再等待 AssetManager、直接开始发现测试”，却没有同步完成这个 barrier。 |
| 影响 | 一旦命中 fallback，当前 engine 表面上完成了首次测试发现，但运行期会永久认为“资产扫描尚未完成”。结果是后续热重载测试准备和 post-compile 测试增量发现都被静默跳过，形成初始化成功但能力被永久降级的状态机分裂。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“测试发现 barrier 完成”收口成统一状态迁移，确保 AssetManager 和 fallback 两条分支都能走到同一终态。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 内新增统一 helper，例如 `MarkInitialTestDiscoveryReady(EInitialScanSource Source)`，由它负责执行 `DiscoverTests()` 后更新 readiness 状态与日志，不再让两个分支分别手工写状态。 2. 将当前 `bCompletedAssetScan` 升级为更准确的状态表示：最小改法是在 fallback 分支也置 `bCompletedAssetScan = true`；更稳妥的做法是改成枚举或双布尔，区分 `CompletedViaAssetManager` 和 `CompletedWithoutAssetManager`，但对外统一暴露 `IsInitialTestDiscoveryReady()`。 3. 把 `2481-2489` 和 `4143-4153` 的硬编码 `bCompletedAssetScan` 判断改成统一 helper，避免未来再出现分支只做发现、不做状态迁移的漂移。 4. 若 `AssetManager` 之后才初始化，评估是否需要补一次重新发现；若不需要，也要在 helper 中明确记录“fallback 已满足 barrier，不再等待二次扫描”，避免后续调用点继续猜测。 5. 新增 focused regression：模拟 `AssetManager == nullptr` 的初始化路径，断言首次 `DiscoverTests()` 后 readiness 状态已完成，后续 hot reload test 准备和 post-compile 增量测试发现不再被永久阻断。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果简单把 fallback 直接等同于真实 asset scan 完成，可能掩盖少数确实依赖 `UPrimaryDataAsset` 扫描结果的测试发现差异；因此更推荐引入显式 readiness helper，把“已允许继续”和“确实经过 AssetManager”区分开。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 `AssetManager` 缺席回归，断言 fallback 路径下 `IsInitialTestDiscoveryReady()` 为真，并验证 hot reload test 准备与增量测试发现仍会执行。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-51 | Defect | 立即修复，先为 adopted engine 建立生命周期句柄，阻断 subsystem 对失效 engine 的继续解引用 |
| P1 | Issue-52 | Architecture | 在 Issue-51 后推进，把 tick ownership 从进程级总开关收口到 engine 级 registry |
| P2 | Issue-53 | Defect | 可并行处理，统一初始化阶段的 test-discovery readiness 状态机 |

---

## 发现与方案 (2026-04-09 01:00)

### Issue-54：`InitializeAngelscript()` 在 owner 真正建立前就提交初始化状态，失败后无法重试

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `138-166`; `1605-1608`; `2100-2105`; `2171-2174` |
| 问题 | `FAngelscriptRuntimeModule::InitializeAngelscript()` 在真正创建/接管 owner 之前就先把 `bInitializeAngelscriptCalled` 置为 `true`（`AngelscriptRuntimeModule.cpp:143`）。随后 testing override 分支即使返回 `nullptr` 也会直接 `return`（`145-151`）；自建 owner 分支则会先 `Push(OwnedPrimaryEngine.Get())`，再调用 `OwnedPrimaryEngine->Initialize()`（`162-164`）。但 `FAngelscriptEngine::Initialize()` / `Initialize_AnyThread()` 内部存在显式退出路径，例如导出模式 `RequestExitWithStatus(false, 0)`（`AngelscriptEngine.cpp:1605-1608`）以及编译失败 fatal path 的 `RequestExit(true)`（`2100-2105`, `2171-2174`），调用方拿不到任何成功/失败结果，也没有回滚 `bInitializeAngelscriptCalled` 或弹出刚压入的 owner。 |
| 根因 | RuntimeModule 把“开始尝试初始化”与“已经拥有可用 runtime”混成了同一个布尔状态，初始化流程缺少事务边界和失败回滚。 |
| 影响 | 一旦 override 返回空、初始化过程中请求退出，或后续为 `Initialize()` 增加新的 fail-fast 路径，module 会落入“`bInitializeAngelscriptCalled == true`，但没有可重试 owner” 的卡死状态；若自建 owner 路径已先 `Push()`，`ContextStack` 还会短暂暴露半初始化 engine，后续再次调用 `InitializeAngelscript()` 也会被直接短路。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 RuntimeModule 初始化改成显式事务：只有 owner 建立成功后才提交状态，失败则完整回滚。 |
| 具体步骤 | 1. 将 `InitializeAngelscript()` 重构为“准备阶段 + 提交阶段”：先在局部变量中创建 candidate owner/override owner，初始化成功后再写回 `OwnedPrimaryEngine` 并设置 `bInitializeAngelscriptCalled = true`。 2. 为 `FAngelscriptEngine::Initialize()` 增加显式返回结果，例如 `EAngelscriptInitializeResult::Succeeded / ExitRequested / Failed`，禁止继续用 `void` 让上层猜测是否成功。 3. 在 RuntimeModule 中为 `Push()` 建立 scope guard，只有当初始化结果为 `Succeeded` 时才保留栈项；否则自动 `Pop()` 并清空局部 owner。 4. testing override 分支若返回 `nullptr` 或未初始化 engine，必须保持 `bInitializeAngelscriptCalled = false`，并记录明确错误日志，允许调用方修正后重试。 5. 新增 focused regression：一条让 `InitializeOverrideForTesting` 返回 `nullptr`，断言 module 可再次初始化；另一条注入可控的 `Initialize()` 失败结果，断言 `ContextStack` 不残留半初始化 engine，且第二次调用会真正重试。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 给 `Initialize()` 增加返回结果会触及现有调用方；若短期兼容层仍保留旧 `void` 入口，容易再次把失败结果吞掉。 |
| 前置依赖 | 无 |
| 验证方式 | 新增“override 返回空后可重试”和“初始化失败后 stack/flag 回滚”回归；同时复跑现有 RuntimeModule 启动/关闭测试，确认正常成功路径不变。 |

### Issue-55：`UAngelscriptGameInstanceSubsystem::Initialize()` 先暴露已初始化状态，再执行 owner bootstrap

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `12-30`; `1605-1608`; `2100-2105`; `2171-2174` |
| 问题 | `UAngelscriptGameInstanceSubsystem::Initialize()` 一进入就把 `bInitialized = true`（`AngelscriptGameInstanceSubsystem.cpp:16`）。当没有现成 engine 时，它还会先把 `PrimaryEngine = &OwnedEngine`，再 `Push(PrimaryEngine)`，之后才调用 `OwnedEngine.Initialize()`（`18-23`）；函数末尾又会依据 `PrimaryEngine != nullptr` 递增全局 `ActiveTickOwners`（`26-29`）。但 `OwnedEngine.Initialize()` 没有成功/失败返回值，而其内部同样包含 `RequestExitWithStatus(false, 0)` 与 `RequestExit(true)` 这类退出路径（`AngelscriptEngine.cpp:1605-1608`, `2100-2105`, `2171-2174`）。也就是说，subsystem 会先对外宣称“已初始化”，再去做可能中止流程的 bootstrap。 |
| 根因 | subsystem 生命周期实现把成员字段写入顺序当成无害细节，没有把“owner 已可用”定义成 bootstrap 成功后的提交动作。 |
| 影响 | 只要 `OwnedEngine.Initialize()` 走到退出/失败路径，subsystem 仍会保留 `bInitialized`、`PrimaryEngine`、`ContextStack` 栈项以及 `ActiveTickOwners` 这些已提交状态。随后 `IsAllowedToTick()`、`HasAnyTickOwner()`、`IsInitialized()` 等上层判断都可能把这台半初始化 owner 当成正式 runtime，阻止后续重建并污染全局 tick/解析状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 subsystem 初始化改成延迟提交：先完成 owner bootstrap，再一次性写入 `bInitialized`、owner 标志和 tick-owner 状态。 |
| 具体步骤 | 1. 在 `Initialize()` 中把 `bInitialized = true`、`bOwnsPrimaryEngine = true`、`++ActiveTickOwners` 全部后移到 bootstrap 成功之后；在进入自建 owner 路径前只保留局部临时指针。 2. 为 `Push(PrimaryEngine)` 加 scope guard，若 `OwnedEngine.Initialize()` 未返回 `Succeeded`，立刻 `Pop()` 并把 `PrimaryEngine` 还原为空。 3. 将 adopted 路径与 owned 路径统一改成消费 `FAngelscriptEngine::Initialize()` 的显式结果；只有 `Succeeded` 才允许把 subsystem 对外标记为 ready。 4. 在 `Deinitialize()` 中增加防御分支：若 subsystem 处于“bootstrap 未完成但对象仍存在”的中间态，优先做状态清理而不是假定 owner 已完整建立。 5. 新增 focused regression：注入一个会返回失败/退出结果的 `OwnedEngine.Initialize()` seam，断言 `Initialize()` 返回后 `bInitialized == false`、`PrimaryEngine == nullptr`、`ContextStack` 恢复为空，且 `ActiveTickOwners` 不会被污染。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 需要为 subsystem 测试引入真实失败注入 seam；如果继续只靠当前的 injected helper 直接改字段，无法验证新合同。 |
| 前置依赖 | 建议与 Issue-54 一起推进，共用 `FAngelscriptEngine::Initialize()` 的结果枚举。 |
| 验证方式 | 新增 subsystem bootstrap failure 回归，验证 `Initialize()` 失败不会留下 tick owner、当前 engine 或 owner 标志；随后复跑现有 subsystem tick/deinitialize 用例。 |

### Issue-56：fatal compile-exit 路径仍继续发布“初始化完成”状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp` |
| 行号 | `819-856`; `1653-1655`; `2100-2105`; `2201-2221`; `5-15` |
| 问题 | `InitialCompile()` 在 commandlet 或 `bExitOnError` 场景下，只要编译失败就直接 `RequestExit(true)`（`AngelscriptEngine.cpp:2100-2105`），但函数没有在这里提前结束；后面仍继续注册 `OnPostEngineInit` 测试发现回调（`2201-2218`），并把 `bDidInitialCompileSucceed = bSuccess`、`bIsInitialCompileFinished = true`（`2220-2221`）。更上层的 `Initialize()` 也无条件执行 `PostInitialize_GameThread()` 与 `InitializeOwnedSharedState()`（`819-856`），于是 `GetOnInitialCompileFinished().Broadcast()` 仍会发生（`1653-1655`）。同时 `UAngelscriptTestCommandlet::Main()` 会继续读取当前 engine 并基于这些状态决定后续流程（`AngelscriptTestCommandlet.cpp:5-15`）。 |
| 根因 | RuntimeCore 把“请求进程退出”当成副作用，而不是初始化状态机的终止结果；fatal exit path 没有阻止后续 ready-state 发布。 |
| 影响 | 在 `RequestExit` 真正结束进程之前，RuntimeCore 已经对外发布了“初次编译已结束”和 `OnInitialCompileFinished`，并把 shared state 挂到当前 engine 上。任何监听该广播或读取 readiness 的代码，都会在一个本应终止的 engine 上继续工作，形成 fatal compile 失败后的假就绪状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 fatal compile-exit 建模成显式初始化结果，阻止 post-initialize、shared-state 发布和 ready 广播继续执行。 |
| 具体步骤 | 1. 让 `InitialCompile()` 或 `Initialize_AnyThread()` 返回显式结果枚举，例如 `Succeeded / RecoverableFailure / ExitRequested`，不要只靠 `bSuccess` 和 `RequestExit()` 侧效应。 2. 在 `Initialize()` 中根据该结果决定是否执行 `PostInitialize_GameThread()` 与 `InitializeOwnedSharedState()`；`ExitRequested` 路径必须直接结束初始化，不得再广播 `OnInitialCompileFinished`。 3. 将 `bIsInitialCompileFinished` 细分为至少两个状态：`CompileCompleted` 与 `RuntimeReady`，fatal exit 路径只允许记录失败结果，不允许对外宣称 runtime ready。 4. 把 `OnPostEngineInit` 测试发现注册也移动到“仅 interactive runtime 才允许”的 finalize helper，避免 fatal path 继续安装无意义回调。 5. 新增 commandlet/exit-on-error 回归：构造初次编译失败场景，断言 `OnInitialCompileFinished` 不会广播、`SharedState` 不会创建、`IsInitialized()`/runtime ready helper 不会被置为可用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 若现有某些工具把 `bIsInitialCompileFinished` 当成“无论成功失败都结束了编译”的信号，拆分 ready/failed 状态后需要同步更新调用点判断。 |
| 前置依赖 | 建议与 Issue-54/55 共享同一套 `EAngelscriptInitializeResult` 状态机。 |
| 验证方式 | 新增 fatal compile failure 回归，断言 exit 请求后不会广播 `OnInitialCompileFinished`、不会创建 shared state，也不会把 runtime 标记成 ready；再验证正常成功路径仍会广播一次。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-54 | Defect | 立即修复，先把 RuntimeModule 初始化改成可回滚事务，避免失败后永久卡死 |
| P1 | Issue-55 | Defect | 紧接 Issue-54，收口 subsystem bootstrap 的延迟提交与失败回滚 |
| P1 | Issue-56 | Defect | 在统一初始化结果枚举后推进，阻止 fatal compile-exit 继续发布 ready 状态 |

---

## 发现与方案 (2026-04-09 01:04)

### Issue-57：多个 `subsystem` 可同时推进同一台 adopted engine，tick ownership 没有唯一性约束

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `12-29`; `81-118`; `186-197` |
| 问题 | `UAngelscriptGameInstanceSubsystem::Initialize()` 只要解析到现成 `PrimaryEngine` 就会无条件采用，并在 `PrimaryEngine != nullptr` 时递增全局 `ActiveTickOwners`（`AngelscriptGameInstanceSubsystem.cpp:16-29`）。随后 `Tick()` 与 `IsAllowedToTick()` 只检查 `bInitialized` 和 `PrimaryEngine != nullptr`，任何 adopted subsystem 都会直接执行 `PrimaryEngine->Tick(DeltaTime)`（`66-86`）；`HasAnyTickOwner()` 也只是返回全局计数（`116-118`）。与此同时，RuntimeModule 的 fallback 只负责“有没有任意 subsystem 在 tick”，并不负责“某台 engine 是否已经被唯一接管”（`AngelscriptRuntimeModule.cpp:186-197`）。已验证事实是，这套代码里不存在任何按 engine 判定“当前 tick owner 是否已经被某个 subsystem 占用”的逻辑。推断：只要多个 `UGameInstanceSubsystem` 在同一进程内 adopt 到同一个 current engine，它们就会在各自 `Tick()` 中重复推进同一台 engine。 |
| 根因 | RuntimeCore 把 tick ownership 建模成了进程级开关和裸 `PrimaryEngine` 指针，而不是 per-engine 的唯一 owner 协议；subsystem 生命周期只记录“我拿到了一台 engine”，没有记录“我是否是这台 engine 的唯一驱动者”。 |
| 影响 | 同一台 engine 的 `Tick()` 副作用会被按 subsystem 个数重复执行，包括 hot reload 轮询、测试 runner、debug server 与其它基于帧推进的状态机。问题表象不会稳定崩溃，而是时间步长被放大、状态被重复推进，尤其会污染 multi-PIE、多 `GameInstance` 和测试夹具中的生命周期判断。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 tick ownership 从“任意 subsystem 见到 engine 就可 tick”改成“每台 engine 只能被一个显式 owner 驱动”。 |
| 具体步骤 | 1. 在 RuntimeCore 内引入 per-engine tick ownership registry，键使用 `FAngelscriptEngineLifetimeToken` 或等价可判活标识，值记录唯一 owner（推荐保存 `TWeakObjectPtr<UAngelscriptGameInstanceSubsystem>` 或显式 owner handle）。 2. 在 `UAngelscriptGameInstanceSubsystem::Initialize()` 中把“采用 `PrimaryEngine`”与“取得 tick ownership”拆开：adopt 分支先解析 engine，再调用 `TryClaimTickOwnership(PrimaryEngine, this)`；若 claim 失败，该 subsystem 仍可保留 engine 引用，但必须进入 `bDrivesPrimaryEngineTick == false` 的 observer 状态。 3. 将 `IsAllowedToTick()`、`Tick()` 和 `HasAnyTickOwner()` 统一改成依赖新的 ownership registry 或显式 `bDrivesPrimaryEngineTick`，禁止仅凭 `PrimaryEngine != nullptr` 就推进 runtime。 4. 在 `Deinitialize()` 中只为真正持有 tick ownership 的 subsystem 释放 claim，避免 adopted observer 误减全局计数或错误影响其它 owner。 5. 补充 focused regression：构造两个 `GameInstanceSubsystem` adopt 同一台 engine，断言只有一个 subsystem 会实际调用 `PrimaryEngine->Tick()`；再验证 owner 释放后，另一个 subsystem 可以重新 claim 并继续驱动。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果简单用进程级单例 owner 代替 per-engine owner，只会把当前问题换成另一种全局抑制；claim 逻辑必须严格绑定到具体 engine。 |
| 前置依赖 | 建议结合 `Issue-51` / `Issue-52` 的 lifetime token 与 engine 级 tick registry 一起推进，避免新 ownership 表继续使用裸指针。 |
| 验证方式 | 新增 multi-subsystem tick 回归，统计同一帧内 engine tick 次数；并复跑现有 subsystem/runtime fallback 用例，确认“同一 engine 只被 tick 一次”与“无关 engine 不受影响”同时成立。 |

### Issue-58：adopted `subsystem` 离开临时 scope 后无法被 core resolver 重新发现

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 行号 | `17-23`; `94-113`; `491-506`; `718-733`; `46-68` |
| 问题 | adopted 路径下，`UAngelscriptGameInstanceSubsystem::Initialize()` 只是把 `TryGetCurrentEngine()` 命中的 engine 保存到 `PrimaryEngine`，并不会像 owned 路径那样长期 `Push` 到 `ContextStack`（`AngelscriptGameInstanceSubsystem.cpp:17-23`）。之后 RuntimeCore 再次解析当前 subsystem 时，`TryGetCurrentEngine()` 只能在 `ContextStack` 为空后回退到 `UAngelscriptGameInstanceSubsystem::GetCurrent()`（`AngelscriptEngine.cpp:718-733`），而 `GetCurrent()` 又只依赖 `GetAmbientWorldContext()` 反查 `World -> GameInstance -> Subsystem`（`AngelscriptGameInstanceSubsystem.cpp:94-113`）。问题在于 `FAngelscriptEngineScope::Reset()` 退出时会先弹栈，再把 ambient world 同步到“弹栈后的 current engine”；若外层没有继续存活的 current engine，ambient 就会被清空（`AngelscriptEngine.cpp:491-506`）。这意味着 adopted subsystem 明明仍持有有效 `PrimaryEngine`，离开临时 scope 后却可能因为 ambient 丢失而完全无法再被 core resolver 找回。更直接的证据是，测试辅助 `TryGetRunningProductionSubsystem()` 已经不得不在 `GetCurrent()` 失败后额外遍历 `GEngine->GetWorldContexts()` 做二次兜底（`AngelscriptTestUtilities.h:46-68`）。 |
| 根因 | RuntimeCore 把“重新发现当前 subsystem”绑定到了 ambient world 这一瞬时全局状态，而不是绑定到 subsystem 自身稳定持有的 `UGameInstance` / `PrimaryEngine` 边界；adopted owner 的持久状态与 resolver 的输入源因此脱节。 |
| 影响 | adopted engine 会出现“对象还活着，但 resolver 说当前没有 subsystem/engine”的分裂状态。随后 `TryGetCurrentEngine()`、`ShouldUseEditorScriptsForCurrentContext()`、`ShouldUseAutomaticImportMethodForCurrentContext()`、`IsScriptDevelopmentModeForCurrentContext()` 等入口都可能在离开一层临时 scope 后错误退化为 `nullptr/false`，把生产路径变成高度依赖 ambient 偶然残留的非确定性行为。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 adopted subsystem 的重新解析改走稳定宿主边界，而不是继续把 ambient world 当唯一入口。 |
| 具体步骤 | 1. 为 `UAngelscriptGameInstanceSubsystem` 增加显式稳定查询入口，例如 `TryGetForGameInstance(UGameInstance*)` / `TryGetForWorld(UWorld*)`，并把 `GetCurrent()` 改成薄封装：只负责从 ambient world 推导 `UGameInstance*`，真正的 subsystem 解析交给显式 API。 2. 在 `FAngelscriptEngine` resolver 层新增 adopted-subsystem 查询 helper，例如 `TryResolveEngineFromSubsystemHost()`，优先消费显式 `UWorld*`/`UGameInstance*` 或 subsystem 自身登记的宿主信息，不再把 ambient world 当成唯一真相。 3. 在 subsystem `Initialize()` 的 adopted 路径中，除保存 `PrimaryEngine` 外，还要把 `UGameInstance` 或可反查 `UWorld` 的宿主信息显式沉淀下来，供后续 resolver 直接命中。 4. 将 `TryGetCurrentEngine()` 中的 subsystem fallback 重构为“两段式”：game thread 上先解析稳定宿主，再取 subsystem engine；当 ambient 不可用但存在已登记的 adopted subsystem 时，仍能返回对应 engine。 5. 新增 focused regression：在临时 `FAngelscriptEngineScope` 内初始化 adopted subsystem，离开 scope 后清空 ambient world，再断言 `GetCurrent()`/`TryGetCurrentEngine()` 仍可通过稳定宿主重新解析到同一 subsystem/engine。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果继续让 `GetCurrent()` 同时承担“ambient 入口”和“稳定宿主查询”两种职责，容易再次把瞬时状态和持久状态混回一个函数；需要先拆接口边界。 |
| 前置依赖 | 建议结合 `Issue-03` 的 resolver 拆分一起实施；否则容易在旧环状依赖上再补一层特殊分支。 |
| 验证方式 | 新增 adopted-subsystem 重新解析回归，覆盖“scope 结束后 ambient 被清空”“subsystem 仍存活且持有 engine”场景；并验证测试辅助不再需要 world-scan fallback 才能找到 production subsystem。 |

### Issue-59：`GetAngelscriptExecutionFileAndLine()` 同时存在空指针崩溃和脏输出合同

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 行号 | `5665-5698`; `659-666` |
| 问题 | `FAngelscriptEngine::GetAngelscriptExecutionFileAndLine()` 的 JIT 分支在 `DebugStack == nullptr` 时反而直接读取 `DebugStack->Filename` 与 `DebugStack->LineNumber`（`AngelscriptEngine.cpp:5671-5676`），这是确定性的空指针解引用。与此同时，非 JIT 分支若 `asGetActiveContext() == nullptr` 或 `GetCallstackSize() == 0`，函数会直接 `return`，完全不初始化 `OutFilename` / `OutLineNumber`（`5685-5690`）。这不是孤立 helper：`Bind_UObject.cpp` 在创建 redirector metadata 时先声明未初始化的 `FString Filename; int LineNumber;`，随后直接调用该接口并把结果写入 `ScriptAssetFilename` / `ScriptAssetLineNumber`（`659-666`）。 |
| 根因 | 该 helper 没有定义统一的“无可用执行位置信息时返回什么”合同：JIT 路径把 `nullptr` 判断写反了，普通 context 路径又把“无上下文”处理成裸返回，导致同一 API 同时违反内存安全和输出稳定性。 |
| 影响 | 命中 `activeExecution` 且缺少 `debugCallStack` 时会直接崩溃；命中“无 active context / 空 callstack”时则会把调用方的旧值或未定义值继续向下传播。`Bind_UObject` 已经把这类输出写进资产 metadata，后续定位、跳转和异常追踪会得到错误脚本位置。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把输出 sentinel 在函数入口统一初始化，再分别修正 JIT 与普通 context 两条分支的返回合同。 |
| 具体步骤 | 1. 在 `GetAngelscriptExecutionFileAndLine()` 入口立即执行 `OutFilename = TEXT(\"\"); OutLineNumber = -1;`，把“无可用位置信息”的默认值显式化。 2. 修正 JIT 分支条件：只有 `DebugStack != nullptr` 时才读取 `Filename/LineNumber`；若为空则保留 sentinel 直接返回，禁止继续解引用。 3. 在普通 context 分支保留现有 early-return 结构，但它们现在必须返回已初始化的 sentinel，而不是把调用方输出留成脏值；同时对 `Filename == nullptr` 的情况补一层防御。 4. 审查所有调用点，尤其是 `Bind_UObject.cpp` 这种先声明局部变量再调用 helper 的路径，确认它们接受 `\"\" / -1` 作为稳定合同，而不是继续假定一定有有效位置。 5. 新增 focused regression：一条覆盖 `activeExecution` 存在但 `debugCallStack == nullptr` 的 JIT 路径，断言返回 `\"\" / -1` 且不崩溃；另一条覆盖无 active context 的普通路径，断言 metadata 写入稳定 sentinel。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果现有少数调用点把“空字符串/`-1`”误当成有效位置信息，需要同步修正断言或 UI 展示；但这比继续传播未定义值可控得多。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 helper focused regression，并执行一条资产 metadata 相关回归，确认 `ScriptAssetFilename` / `ScriptAssetLineNumber` 在无执行上下文时稳定写入 sentinel，而不是崩溃或残留旧值。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-58 | Architecture | 优先处理，先把 adopted subsystem 的稳定 resolver 边界收口，避免存活对象被 core 查询“找丢” |
| P1 | Issue-57 | Defect | 在 Issue-58 后推进，建立 per-engine tick ownership 唯一性，阻断同一 engine 被重复推进 |
| P1 | Issue-59 | Defect | 可并行快速修复，先止住 `GetAngelscriptExecutionFileAndLine()` 的崩溃与脏输出合同 |

---

## 发现与方案 (2026-04-09 01:26)

### Issue-60：threaded `Initialize()` 会把半初始化 engine 过早暴露给全局 resolver

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `437-455`; `676-807`; `819-856`; `1280-1324`; `1382-1491` |
| 问题 | `FAngelscriptEngine::Initialize()` 一进入就创建 `FAngelscriptEngineScope ScopedInitializingEngine(*this)`（`819-823`），而 `FAngelscriptEngineScope` 构造函数会立刻把当前 wrapper `Push` 进 `ContextStack`（`437-455`）。与此同时，`IsInitialized()` 只要发现 `ContextStack::Peek()` 非空就返回 `true`（`676-680`），`GetPackage()` / `GetScriptRootDirectory()` / `ShouldUseEditorScriptsForCurrentContext()` 等公共入口也都会直接基于 `TryGetCurrentEngine()` 读取当前 wrapper（`692-807`）。问题在于 threaded 路径随后会在 worker thread 里慢慢完成真正 bootstrap，而 game thread 在等待期间还会持续 `OnAsyncLoadingFlushUpdate.Broadcast()` 和 `ProcessThreadUntilIdle(...)`（`825-848`）。这时 package、root path、shared state 等关键状态仍未就绪，它们要到 `Initialize_AnyThread()` 中后段才建立（`1382-1491`）。也就是说，初始化窗口内任何被 pump 到 game thread 的任务，都可能把这台半初始化 engine 当成“当前且已初始化”的正式 runtime。 |
| 根因 | RuntimeCore 复用了面向外部的 `ContextStack`/`TryGetCurrentEngine()` 作为内部 bootstrap scope，却没有区分“正在初始化”与“已经 ready”的状态边界；worker 初始化与 game-thread 泵任务并存后，这个私有过渡态被直接暴露成公共真相。 |
| 影响 | 初始化窗口内的 game-thread 任务会得到假阳性 readiness：`IsInitialized()` 提前返回 `true`，`GetPackage()` 可能返回 `nullptr`，`GetScriptRootDirectory()` 可能拿到空 root，依赖 current-engine 的诊断、bind 或测试辅助也会读到不完整状态。故障形态不是单点崩溃，而是启动期行为不稳定和难以复现的“看起来已初始化、实际还没 ready”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把初始化中的私有 scope 与对外可见的 current-engine/readiness 状态拆开，未 ready 前不得进入公共 resolver。 |
| 具体步骤 | 1. 将 `Initialize()` 开头的 `FAngelscriptEngineScope ScopedInitializingEngine(*this)` 改成私有 bootstrap guard，或新增 `FScopedInitializingEngine` 这类只服务内部调用的 helper；它不得写入公共 `ContextStack`。 2. 为 `FAngelscriptEngine` 增加显式初始化状态，例如 `EAngelscriptInitializeState::Uninitialized / Initializing / Ready / Failed`，并让 `IsInitialized()`、`TryGetCurrentEngine()`、`GetPackage()` 只对 `Ready` owner 生效。 3. 若初始化内部某些步骤确实需要当前 engine，上层应显式传 `this` 或使用私有 helper，不再依赖 `TryGetCurrentEngine()` 的全局回退。 4. threaded 等待循环若仍需 `ProcessThreadUntilIdle(...)`，必须保证被 pump 的任务在 ready 前看不到这台 engine；必要时把相关 ready-sensitive 工作延后到 `PostInitialize_GameThread()` 之后。 5. 增加 focused regression：在 threaded 初始化等待窗口向 game thread 注入一个会调用 `IsInitialized()` / `TryGetCurrentEngine()` / `GetPackage()` 的任务，断言提交前它们不会把初始化中的 engine 暴露为 ready。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接让 `TryGetCurrentEngine()` 在 `Initializing` 阶段返回空，少数初始化内部 helper 可能暴露出对全局 resolver 的隐式依赖，需要同步改成显式参数传递。 |
| 前置依赖 | 建议与 `Issue-08` / `Issue-54` 统筹，统一收口 threaded initialize 的状态机与失败提交边界。 |
| 验证方式 | 新增 threaded initialize focused regression，覆盖等待窗口内的 ready 查询；随后复跑现有 runtime-module / subsystem / multi-engine 用例，确认初始化完成后 current-engine 仍能正常建立。 |

### Issue-61：`FAngelscriptGameThreadScopeWorldContext` 的公开绑定绕过了 `BlueprintThreadSafe` 的 `WorldContext` 保护

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptGameThreadScopeWorldContext.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` |
| 行号 | `AngelscriptEngine.h` `710-724`; `AngelscriptEngine.cpp` `746-753`; `Bind_FAngelscriptGameThreadScopeWorldContext.cpp` `4-17`; `ASClass.cpp` `1931-1953`; `as_context.cpp` `5165-5175` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 `A-30`，并补充实施边界：`FAngelscriptGameThreadScopeWorldContext` 构造/析构只会无条件调用 `FAngelscriptEngine::AssignWorldContext(...)`（`AngelscriptEngine.h:712-720`），而 `AssignWorldContext()` 又会直接改写 current engine 的 `WorldContextObject` 与 ambient world（`AngelscriptEngine.cpp:746-753`）。这个 helper 还被 `Bind_FAngelscriptGameThreadScopeWorldContext.cpp` 作为普通 `ValueClass` 公开绑定给脚本（`4-17`）。与此同时，RuntimeCore 对真正带 `asTRAIT_USES_WORLDCONTEXT` 的系统函数已经建立了 `BlueprintThreadSafe` 保护：线程安全函数执行时 `UASFunction::RuntimeCallFunction/Event()` 会把 `GIsInAngelscriptThreadSafeFunction` 置为 `true`（`ASClass.cpp:1931-1953`），第三方 AngelScript 在系统函数调用前会检查该标志并直接抛错（`as_context.cpp:5165-5175`）。但 `FAngelscriptGameThreadScopeWorldContext` 既不是带 trait 的系统函数，也没有任何线程/线程安全检查，因此它成了一个可以直接改写 world context 的旁路。 |
| 根因 | RuntimeCore 只把 `BlueprintThreadSafe` 限制落实到了“调用带 `USES_WORLDCONTEXT` trait 的系统函数”这一层，却没有把直接写 ambient/current-engine world context 的 helper 纳入同一套线程边界策略；绑定层又把该 helper 暴露成了任意脚本都能构造的普通值类型。 |
| 影响 | 线程安全脚本函数可以在不触发现有保护的前提下改写 `GAmbientWorldContext` 和 `WorldContextObject`。这会把本应在入口即被拒绝的 world-context 访问，升级成静默污染全局上下文，后续再调用依赖 world 的逻辑时才以错误 world、线程违规或崩溃的形式暴露。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `FAngelscriptGameThreadScopeWorldContext` 明确定义为 game-thread-only / non-thread-safe helper，禁止脚本通过公开绑定绕过现有 `WorldContext` 保护。 |
| 具体步骤 | 1. 在 `FAngelscriptGameThreadScopeWorldContext` 的构造/析构，以及更底层的 `AssignWorldContext()`/`SetAmbientWorldContext()` 入口增加统一前置校验：至少要求 `IsInGameThread()`，并在 editor 下同步拒绝 `GIsInAngelscriptThreadSafeFunction == true` 的调用。 2. 调整 `Bind_FAngelscriptGameThreadScopeWorldContext.cpp`：优先移除该类型的通用脚本绑定，或改成只供受控 native glue 使用的内部 helper，不再让任意脚本直接 `new` 出这个 scope。 3. 若确实需要脚本侧 API，则改成显式函数入口并挂上与 `WorldContext` 等价的线程约束，在失败时复用已有异常文案，保证 contract 一致。 4. 审查所有当前通过该 helper 改写 world context 的脚本/绑定路径，确认它们要么已经在 game thread，要么改为显式 non-thread-safe 调度后再进入。 5. 新增 focused regression：在线程安全脚本函数里尝试构造该 scope，断言会稳定抛出/失败且 `GAmbientWorldContext`、`TryGetCurrentWorldContextObject()` 保持不变。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptGameThreadScopeWorldContext.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptObjectModelTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果仓库里已有脚本把该类型当作合法公开 API 使用，直接移除绑定会暴露兼容性问题；需要先以日志/断言确认调用面，再决定是彻底隐藏还是转成受限入口。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 `BlueprintThreadSafe` 违规回归，覆盖“构造失败不改写 ambient/current world”“普通 non-thread-safe 路径仍可正常恢复 world context”两条合同。 |

### Issue-62：`OptimizedCall_*` 快路径绕过了 Blueprint override 的线程边界保护

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | `ASClass.cpp` `53-77`; `1561-1735`; `1971-2033`; `AngelscriptEngine.cpp` `1778-1783`; `AngelscriptEngine.h` `242-250`; `758-768` |
| 问题 | 对应 `RuntimeCore_Analysis.md` 的 `A-33`，并补充实施边界：慢路径 `RuntimeCallFunction/Event` 在进入执行前会统一调用 `CheckGameThreadExecution()`（`ASClass.cpp:1971-2033`），从而拒绝 default statement 或非 game-thread 的 Blueprint override 调用（`53-77`）。但多组 `OptimizedCall_*` 快路径完全没有这道检查（`1561-1735`）：它们要么直接执行 raw JIT thunk，要么立即创建 `FAngelscriptGameThreadContext Context(Object, RealFunction->GetEngine())`。而 `FAngelscriptGameThreadContext` 自己又只是无条件使用 `GameThreadTLD` 并改写 world context（`AngelscriptEngine.cpp:1778-1783`），没有任何 `IsInGameThread()` 防护；`CanUseGameThreadData()` 也只是一个独立 helper，并未被这些快路径统一消费（`AngelscriptEngine.h:242-250`）。结果是同一类 Blueprint override 一旦走到 optimized dispatch，就能直接绕过慢路径上已经建立好的线程边界。 |
| 根因 | 线程安全合同被散落在部分调度入口上，而不是收口到所有 override 都必须经过的共享执行原语；当 dispatch 选择优化分支时，核心的 game-thread 校验自然被跳过。 |
| 影响 | 这是 correctness/safety 级别的问题：off-thread Blueprint override 在 optimized path 上会直接推进脚本执行、读写 `GameThreadTLD`、改写 world context，最坏情况下触发跨线程脚本执行和宿主崩溃。更糟的是，现有测试 helper 只走慢路径，导致这条生产分支可以长期绿灯潜伏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把线程边界检查前移到所有 override dispatch 的公共入口，raw JIT 和解释执行快路径都不得绕过。 |
| 具体步骤 | 1. 在 `ASClass.cpp` 提炼统一 helper，例如 `ValidateBlueprintOverrideThread(UASFunction* Function, UObject* Object)` 或 `PrepareOverrideExecution(...)`，让 `RuntimeCall*` 与所有 `OptimizedCall_*` 都先经过同一条线程边界检查。 2. 对 raw JIT 分支同样执行这条检查，不能因为“不创建 `FAngelscriptGameThreadContext`”就跳过；若线程不合法，必须在进入 JIT thunk 之前直接返回/报错。 3. 在 `FAngelscriptGameThreadContext` 构造函数中再补一层防御性 `checkf/ensureMsgf(IsInGameThread() || <明确允许条件>)`，把它从“假定调用方已校验”改成“helper 自身也拒绝非法线程”。 4. 审查所有 `OptimizedCall_*` 变体和未来新增 fast path，统一改用共享 helper，避免继续靠人工复制 `CheckGameThreadExecution()`。 5. 新增 focused regression：构造会命中 `OptimizedCall_*` 的 Blueprint override，在非 game thread 或 default statement 环境下调用，断言不会进入 raw JIT / `FAngelscriptGameThreadContext`，同时现有慢路径合同保持不变。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` |
| 预估工作量 | M |
| 风险 | 如果只在 `FAngelscriptGameThreadContext` 上补校验、却漏掉 raw JIT 直跳分支，仍会留下半条绕过路径；修复必须覆盖所有 optimized 入口。 |
| 前置依赖 | 建议与 `Issue-61` 一起评审，统一收口 RuntimeCore 的 game-thread/world-context 保护面。 |
| 验证方式 | 新增 optimized dispatch 线程回归，分别覆盖 raw JIT 分支和 `FAngelscriptGameThreadContext` 分支；再复跑现有 `RuntimeCallEvent` 路径测试，确认合法 game-thread 调用没有回归。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-62 | Defect | 立即修复，先封住 optimized dispatch 对线程边界的绕过路径 |
| P1 | Issue-61 | Defect | 紧随其后，收掉 `BlueprintThreadSafe` 下可直接改写 world context 的公开旁路 |
| P1 | Issue-60 | Defect | 与 threaded initialize 状态机修复一并推进，避免初始化窗口暴露半初始化 engine |

---

## 发现与方案 (2026-04-09 01:37)

### Issue-63：RuntimeCore 关闭 AngelScript 自动 GC 后没有任何生产级回收驱动

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`; `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp` |
| 行号 | `889`; `1395`; `2794-2845`; `1132-1252`; `218-231`; `250-254`; `361-415` |
| 问题 | `InitializeForTesting()` 与 `Initialize_AnyThread()` 都显式执行 `Engine->SetEngineProperty(asEP_AUTO_GARBAGE_COLLECT, 0)`（`AngelscriptEngine.cpp:889`, `1395`），但正式运行期的 `Tick()` 与 `Shutdown()` 全段都没有任何 `ScriptEngine->GarbageCollect(...)` 或等价驱动（`2794-2845`, `1132-1252`）。当前仓库里唯一明确跑 AngelScript GC 的地方只出现在内部 GC 测试 `RunFullGarbageCollection()` / `GarbageCollect(asGC_FULL_CYCLE, 1)`（`AngelscriptGCInternalTests.cpp:250-254`, `361-415`）；而 `UnitTest.cpp` 的 `MaybeGarbageCollect()` 调的是 UE 的 `CollectGarbage(...)`，不是 AngelScript GC（`218-231`）。这意味着 RuntimeCore 在生产路径上关闭了 AngelScript 自动 GC，却没有补上任何稳定的手动回收节奏。 |
| 根因 | RuntimeCore 把“关闭 AngelScript 自动 GC”当成初始化配置的一部分落地了，却没有同步建立 owner 级 GC 调度合同；测试通过内部 helper 手动补了脚本 GC，但正式 runtime tick / teardown 从未接管这项责任。 |
| 影响 | 运行期产生的循环引用脚本对象、delegate/function 等 GC 管理对象会在长时间会话里持续积压，直到进程退出或测试专用 helper 人工介入才可能被清理。结果是内存/对象数量单调增长，热重载与多轮脚本执行期间的旧对象残留时间被无限拉长，也让当前 GC 内部测试覆盖到的行为与真实运行时语义脱节。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 owner engine 建立显式的 AngelScript GC 驱动策略，把“禁用自动 GC”与“何时手动跑 GC”绑定成同一份生命周期合同。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 内新增统一 helper，例如 `TickScriptGarbageCollection(float DeltaTime)` 或 `RunScriptGarbageCollectionStep(EGarbageCollectionTrigger Trigger)`，集中封装 `ScriptEngine->GarbageCollect(...)` 调用与节流策略。 2. 在 `Tick()` 中为正式 owner 路径加入周期性脚本 GC 驱动，优先从轻量步进开始，例如按时间窗或对象统计执行 `asGC_ONE_STEP`，并在 development/test 模式下允许切换成更激进的 full cycle。 3. 在 owner 最终释放前的 `Shutdown()` / `ReleaseOwnedSharedStateResources(...)` 中增加一次受控的 drain/final full cycle，确保 engine teardown 前尽可能回收仍在 AngelScript GC 中排队的对象，而不是完全依赖 `ShutDownAndRelease()` 的底层兜底。 4. 将 GC 触发策略挂进配置或 settings，至少暴露“是否启用手动 GC”“每多少 tick/秒执行一步”“teardown 是否强制 full cycle”三个开关，避免未来再次只有测试知道如何驱动脚本 GC。 5. 补两类回归：一类在正式 runtime tick 路径下构造循环引用脚本对象，断言多次 tick 后 `GetGCStatistics()` 的 `TotalDestroyed` 增长；另一类在 shutdown 前制造 GC 对象，断言 teardown 会完成最后一轮回收而不是把对象完整留到进程结束。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptGCInternalTests.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接在每帧执行 full cycle，可能引入明显的帧时间尖峰；需要先用步进式策略落地，再通过统计数据调节阈值。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 runtime tick GC 回归和 shutdown drain 回归；同时在运行中读取 `GetGCStatistics()`，确认 `CurrentSize` 不再只增不减，`TotalDestroyed` 会随 tick/teardown 增长。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-63 | Defect | 尽快补上运行期 AngelScript GC 驱动，避免当前“只在测试里有人回收”的状态继续放大内存与对象残留 |

---

## 发现与方案 (2026-04-09 01:37)

### Issue-64：JIT 执行态的 `this` 查询与 `DebugBreak()` 仍停留在 `activeContext` 语义

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h` |
| 行号 | `5101-5125`; `5635-5647`; `5700-5745`; `14-28`; `194-217`; `144-147`; `262-285` |
| 问题 | RuntimeCore 已经把 JIT 执行态的 callstack/position 查询迁到了 `tld->activeExecution->debugCallStack`：`GetAngelscriptCallstack()` 会遍历 `Execution->debugCallStack` 组装 frame（`AngelscriptEngine.cpp:5101-5125`），`GetAngelscriptExecutionPosition()` 也优先读同一条 JIT 调试栈（`5635-5647`）。同时 `FScopeJITDebugCallstack` 明确缓存了 `ThisObject`（`StaticJITHeader.h:194-217`），而 `FScriptExecution` 在执行期会把 `tld->activeContext` 置空、只保留 `activeExecution`（`as_context.h:262-285`）。但 `GetAngelscriptExecutionThisObject()` 与 `TryBreakpointAngelscriptDebugging()` 仍直接调用 `asGetActiveContext()`（`AngelscriptEngine.cpp:5700-5745`），而该 API 只返回 `tld->activeContext`（`as_context.cpp:144-147`）。结果是 JIT 执行时，这两个 helper 在 RuntimeCore 已拥有完整 `activeExecution` / `debugCallStack` 信息的前提下，仍稳定拿到 `nullptr`。更直接的后果已经在绑定层暴露：`ASDebugBreak()` 先尝试 `TryBreakpointAngelscriptDebugging()`，失败后立即走 `UE_DEBUG_BREAK()`（`Bind_Debugging.cpp:14-28`）。 |
| 根因 | RuntimeCore 对 JIT 执行态的适配只完成了 file/line/callstack 这一半，`this` 查询与调试暂停仍沿用旧的 bytecode `activeContext` 语义，没有收口到同一条 execution-state resolver。 |
| 影响 | JIT/StaticJIT 路径下，`GetAngelscriptExecutionThisObject()` 会稳定丢失当前 `this`，依赖它的调试、日志和对象定位工具全部退化成假阴性；而脚本侧 `DebugBreak()` 在 JIT 帧里无法进入调试服务器的暂停路径，会直接落到原生 `UE_DEBUG_BREAK()`，把本应可调试的脚本断点升级成宿主级中断。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 建立统一的 execution-state resolver，让 JIT 与 bytecode 路径共用同一套“当前 frame / 当前 this / 当前可暂停上下文”读取合同。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 内新增私有 helper，例如 `ResolveCurrentExecutionState()`，统一返回当前线程的 `activeExecution`、`activeContext`、顶层 `FScopeJITDebugCallstack` 和可用 frame 数，不再让各个 helper 自己猜是走 JIT 还是 bytecode。 2. 用该 helper 重写 `GetAngelscriptExecutionThisObject()`：JIT 路径优先从 `debugCallStack->ThisObject` 取值，普通 bytecode 路径再回退到 `Context->GetThisPointer(...)`，并统一做 `asOBJ_REF` / `UObject` 判定。 3. 用同一 helper 重写 `TryBreakpointAngelscriptDebugging()`：只要当前线程存在 `activeExecution` 或 `activeContext`，都允许构造 `FStoppedMessage` 并进入 `DebugServer->PauseExecution(...)`；禁止继续把 “`asGetActiveContext()==nullptr`” 当成 JIT 帧下不可调试的判据。 4. 审查 `Bind_Debugging.cpp` 与其它调试入口，把 “pause 失败就直接 `UE_DEBUG_BREAK()`” 改成仅在 resolver 明确判定“当前不在任何脚本执行态”时才触发 native break；JIT 帧下应优先暂停脚本调试器。 5. 新增 focused regression：一条覆盖 JIT 执行态下 `GetAngelscriptExecutionThisObject()` 能返回 `ThisObject`；另一条覆盖脚本侧 `DebugBreak()` 在 JIT 帧里会命中调试服务器 pause，而不是直接走 `UE_DEBUG_BREAK()`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerProtocolTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只补 `GetAngelscriptExecutionThisObject()` 而不统一调试暂停入口，仍会保留一半 JIT 调试失真；修复必须以共享 resolver 为中心，而不是点修单个 helper。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 JIT execution-state 回归，验证 `GetAngelscriptCallstack()`、`GetAngelscriptExecutionPosition()`、`GetAngelscriptExecutionThisObject()`、`DebugBreak()` 四条接口在同一 JIT 帧下给出一致结果，并确认调试服务器能收到 pause。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-64 | Defect | 在继续扩展 JIT/StaticJIT 调试功能前先统一 execution-state resolver，避免现有调试接口继续分裂 |

---

## 发现与方案 (2026-04-09 01:37)

### Issue-65：调试值缓存挂在 `asCContext` / `asCScriptFunction` 上后没有任何释放路径

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`; `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp` |
| 行号 | `5330-5347`; `5417-5425`; `5477-5514`; `243-245`; `300-340`; `410-447` |
| 问题 | `GetStack()` 首次命中时会把 `new FAngelscriptDebugStack` 挂到 `asCContext::DebugFramePtr`（`AngelscriptEngine.cpp:5330-5335`），`GetDebugPrototype()` 首次命中时则把 `new FDebugValuePrototype` 挂到 `asCScriptFunction::DebugPrototypePtr`（`5338-5417`）。后续 line callback 会持续复用这两个缓存来实例化 debug variables（`5477-5514`），`FAngelscriptDebugFrame::~FAngelscriptDebugFrame()` 也只会释放 frame 上的 `Variables`，不会回收 `DebugFramePtr`/`DebugPrototypePtr` 本体（`5421-5425`）。另一方面，第三方 AngelScript 的 `asCContext::~asCContext()` 只调用 `DetachEngine()`（`as_context.cpp:243-245`），`DetachEngine()` 只清理 stack blocks 和 user data（`300-340`）；`asCScriptFunction::~asCScriptFunction()` / `DestroyInternal()` 也没有任何对 `DebugPrototypePtr` 的释放（`as_scriptfunction.cpp:410-447`）。也就是说，RuntimeCore 新增的两类调试缓存有分配点，却没有任何对称销毁点。 |
| 根因 | RuntimeCore 复用了第三方类型上的扩展指针存调试元数据，但没有把这些自定义对象接进 AngelScript context/function 的析构链，也没有在 module discard 或 owner teardown 时补一层集中清理。 |
| 影响 | 只要 `WITH_AS_DEBUGVALUES` 路径被触发，`FAngelscriptDebugStack` 与 `FDebugValuePrototype` 就会跟着 `asCContext` / `asCScriptFunction` 一起长期滞留。重复调试、热重载、discard module 或多轮 full-engine create/destroy 后，这些缓存会稳定累积，既造成内存泄漏，也把旧脚本 epoch 的调试元数据残留到新一轮运行里。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `DebugFramePtr` 和 `DebugPrototypePtr` 建立显式 owner 清理路径，禁止继续依赖第三方默认析构去猜测这些 RuntimeCore 扩展对象。 |
| 具体步骤 | 1. 在 RuntimeCore 内新增集中清理 helper，例如 `DestroyDebugStack(asCContext&)` 与 `DestroyDebugPrototype(asCScriptFunction&)`，内部负责 `delete` 扩展对象并把裸指针清回 `nullptr`。 2. 将这两个 helper 接入正式生命周期：优先在 `ReleaseContextsForScriptEngine(...)` / `ResetContextForPooling(...)` 清理 `DebugFramePtr`，在 module discard 和 owner 最终 teardown 遍历待释放 `asCScriptFunction` 时清理 `DebugPrototypePtr`。 3. 若第三方 fork 可接受改动，更稳妥的做法是在 `asCContext::~asCContext()` 与 `asCScriptFunction::DestroyInternal()` 添加受控的扩展清理钩子；否则就在 RuntimeCore 自己的 release helper 中保证所有 context/function 在真正 `Release()` 前先完成清理。 4. 审查 `AngelscriptLineCallback()` 的缓存策略，确保重复进入同一 frame 时只复用仍然有效的 prototype；一旦 module/function 已 discard，必须拒绝继续访问旧 prototype 缓存。 5. 新增 focused regression：触发 debug-values 路径后执行 module discard 或 engine shutdown，断言扩展缓存数量会回到基线，且下一轮重新加载脚本时不会复用旧 prototype/stack 对象。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerValueTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接在第三方析构函数里释放而没有先梳理 RuntimeCore 何时仍在读这些缓存，可能引入 use-after-free；需要先把缓存 owner 边界和回调时序画清楚。 |
| 前置依赖 | 无 |
| 验证方式 | 新增 debug-values teardown 回归，分别覆盖 `DiscardModule()` 和 engine shutdown；用计数器或内存快照确认 `FAngelscriptDebugStack` / `FDebugValuePrototype` 数量不会跨 engine epoch 单调增长。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-65 | Defect | 在高优先级生命周期问题之后处理，尽快补齐调试缓存的释放链，避免长会话持续积压 |
