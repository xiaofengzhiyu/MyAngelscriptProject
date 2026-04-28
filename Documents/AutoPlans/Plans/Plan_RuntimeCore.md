# RuntimeCore 改进计划

## 背景与目标

### 背景

`RuntimeCore` 当前最危险的问题不是“能力完全缺失”，而是 **owner 生命周期、全局状态、worker-thread 分支** 三条合同长期混在一起：`Core/AngelscriptRuntimeModule.cpp` 仍把 testing override 做成 `Push + return` 旁路，`Core/AngelscriptEngine.cpp` 仍同时持有 raw ambient world、late delegate/hook、detached hot reload thread、以及 threaded initialize 对 `GameThreadTLD` 的跨线程改写。结果是“对象还活着但 resolver 已经失真”“销毁后旧回调/旧线程还在跑”“threaded init 明明建了状态却没有正确移交”这三类问题会彼此放大。

`Documents/Plans/Plan_TestEngineIsolation.md` 已经覆盖长期的 deglobalization / engine-local state 迁移，`Documents/Plans/Plan_HazelightCapabilityGap.md` 已经覆盖 subsystem 对外能力闭环；本计划不重复这些长期路线，只记录本轮 **经五维交叉引用 + 当前源码复核后仍然成立** 的 RuntimeCore 缺陷切口，并把对应测试闭环写成可执行任务。

### 目标

- 让 runtime-module、subsystem、full engine teardown 对 `ContextStack`、world-context、delegate、watcher、threaded-init state 全部对称收口
- 让 worker-thread 路径只消费显式 owner 或 thread-safe 状态，不再通过 ambient world / global stack 猜 current engine
- 为当前最脆弱的生命周期分支补齐正式自动化：runtime module override、world-context invalidation、delegate cleanup、hot reload watcher、threaded initialize

## 范围与边界

### 范围内

- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/`
- 与 Core 生命周期强耦合的 `Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/`
- 对应 `AngelscriptTest` 主题测试文件；按 `Plugins/Angelscript/AGENTS.md` 约定，测试继续落在 `Core/`、`HotReload/`、`Subsystem/` 等主题目录，不新建扁平 catch-all 目录

### 范围外

- `Documents/Plans/Plan_TestEngineIsolation.md` 已规划的全量 static-state 去全局化
- `Documents/Plans/Plan_HazelightCapabilityGap.md` 中面向外部能力表面的 subsystem parity 扩展
- 推翻现有 `SoftReload / FullReload` 语义去重写整套热重载哲学；本轮只收口 owner、状态机和线程移交合同
- 相邻但暂缓的 owner final release 清理项，例如 rooted package 回收与 production 级 AngelScript GC 调度；这些问题已在 AutoPlans 中被识别，但本轮优先让 current-engine / world-context / async lifecycle 达到可测试基线

## 与现有活跃 Plan 的关系

- `Documents/Plans/Plan_TestEngineIsolation.md`
  - 已覆盖长期 `ContextStack -> engine-local state -> 移除 legacy fallback` 主线
  - 本文件只补充其未展开的当前缺陷：runtime-module override owner、world-context dangling、delegate/coverage teardown、hot reload watcher、threaded initialize
- `Documents/Plans/Plan_HazelightCapabilityGap.md`
  - 已把 subsystem 闭环列为能力缺口
  - 本文件只处理 subsystem 相关的 **内部 resolver / world-context / tick ownership 合同**，不重述外部能力验证
- `Documents/Plans/Plan_StatusPriorityRoadmap.md`
  - 已将 subsystem 与插件可信度列为优先事项
  - 本文件提供 RuntimeCore 级别的可执行切口和测试矩阵，作为更细的实现入口

## 分析来源

| 分析文档 | 关键发现 |
|---------|---------|
| `Documents/AutoPlans/RuntimeCore_Analysis.md` | 给出 RuntimeCore 当前 defect 基线，集中暴露 override owner、world-context、delegate/coverage、hot reload、threaded initialize 五组问题 |
| `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md` | 把上述 defect 展开成可执行修复方案，尤其补齐 owner、thread-safety、startup/shutdown 对称性 |
| `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` | 指出 runtime module override、真实 subsystem 初始化、delegate cleanup、runtime hot reload scanner、threaded initialize 分支都缺正式回归 |
| `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` | 从架构层面指出 current-engine identity、startup phase、hot reload scheduler 需要显式边界，而不是继续依赖 ambient/global state |
| `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` | 说明 D4 热重载不应推翻重做，而应在保持现有语义的前提下补 owner / observer / impact contract |
| `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` | 对比参考实现中“旧 hook 显式脱钩 / restore”“hook owner 明确化”的做法，为 RuntimeCore teardown 提供吸收方向 |

## 分阶段执行计划

### Phase 1: 先把当前生命周期真相收口为对称合同

- [ ] **P1.1** 收口 `RuntimeModule` 初始化状态机与 testing override owner
  - 当前 `InitializeAngelscript()` 把 testing override 视为一次性 `Push + return` 旁路，既不记录 owner，也不参与 `ShutdownModule()` / `ResetInitializeStateForTesting()` 的对称回收；同文件还用粘滞 `bInitializeAngelscriptCalled` 做幂等，导致 override 污染和同进程二次启动问题会互相放大
  - 本项只做 `RuntimeModule` 级生命周期修复，不重述 `Plan_TestEngineIsolation.md` 里更长期的 global-engine API 收口；目标是让 normal owner 与 override owner 共用一套 `attach -> detach -> shutdown -> reset state` helper，并把“是否已初始化”改成真实 owner 状态判断而不是裸布尔位
  - 先把 override owner 变成 module 明确持有的实例/句柄，再统一收口 `ShutdownModule()` / `ResetInitializeStateForTesting()` / 二次 `StartupModule()`；同时清理对历史 no-op reset 入口的依赖，避免测试继续靠空实现做隔离
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-02` / `D-01` 指出 override 分支只 `Push` 不回收，现有 runtime module 用例不检查 `ContextStack` teardown
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-09` / `Issue-11` / `Issue-21` 要求把 override 改成显式 owner，并恢复 module 启停对称性
    - [C] `RuntimeCore_TestGaps.md` — runtime module `InitializeAngelscript()` / override / reset 当前完全没有直接合同测试
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` L140-L151、L176-L182、L27-L38 — override 分支只 `Push(OverrideEngine)` 后直接返回，reset/shutdown 只处理 `OwnedPrimaryEngine`，当前实现没有任何 override owner 字段，也没有在正常 shutdown 后复位初始化状态
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`
- [ ] **P1.1** 📦 Git 提交：`[Runtime/Core] Fix: own runtime module override lifecycle and startup state`
- [ ] **P1.1-T** 单元测试：补齐 RuntimeModule override / reset / 二次启动生命周期合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleLifecycleTests.cpp`
  - 测试场景：
    - 正常路径：设置 override 后首次 `InitializeAngelscript()` 建立 current engine，`ResetInitializeStateForTesting()` 后 `ContextStack`、current engine 与 module 状态全部回到进入前基线
    - 边界条件：同进程执行 `StartupModule -> ShutdownModule -> StartupModule`，第二次启动仍会重新建立 owner engine 和 fallback ticker，不会被旧状态位短路
    - 错误路径：连续两次设置不同 override engine 时，第二次初始化不会解析到上一轮 engine，也不会留下额外栈项或悬空 owner
  - 测试命名：`Angelscript.TestModule.Core.RuntimeModule.OverrideLifecycleIsSymmetric`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.1-T** 📦 Git 提交：`[Test/Core] Test: cover runtime module override lifecycle`

- [ ] **P1.2** 统一 world-context 的 owner / GC / refresh 合同，并切断 current-engine 与 ambient world 的隐式回跳
  - 当前 RuntimeCore 同时保留 raw `GAmbientWorldContext`、engine-local `WorldContextObject`、以及 `TryGetCurrentEngine() -> UAngelscriptGameInstanceSubsystem::GetCurrent() -> ambient world` 这条反查链；world-context 写入、读取、teardown refresh 与 subsystem 初始化因此彼此缠绕，导致 stale object、`nullptr` world 与 resolver 串线交替出现
  - 本项不是一步到位重写完整 runtime registry，而是先把最危险的真相收口：world-context 默认走 weak/validated resolve，owner teardown 必须同步 refresh，真实 subsystem 初始化要显式建立 world-context，`AssignWorldContext()` 不能继续同时偷改 resolver 输入和输出
  - 先把 `ResolveAmbientWorldContext()`、engine-local world-context 写入口、以及 `TryGetCurrentEngine()` / `GetCurrent()` 的 world 驱动入口拆开，再决定是否继续演进到 `ArchReview` 建议的 runtime handle；本轮优先止住悬空 world 与错误 current-engine 解析
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-04` / `B-18` / `A-27` / `A-32` 指出 raw ambient pointer、subsystem 初始化不建 world-context、module owner 无 GC bridge、engine-local world-context 不做有效性清洗
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-01` / `Issue-03` / `Issue-04` / `Issue-12` / `Issue-32` 提出 teardown refresh、resolver 拆边界、weak ref 与 owner-aware world-context 写入口
    - [C] `RuntimeCore_TestGaps.md` — 缺少真实 subsystem 初始化、失效对象恢复、module-owned world-context 和 cleanup 合同回归
    - [D] `ScriptLifecycle_ArchReview.md` — 当前 engine identity 依赖 `ContextStack + ambient world + subsystem fallback`，建议引入显式 runtime handle / registry 边界
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L269-L275、L718-L753、L1241-L1246 — ambient world 仍是一次校验后的 raw pointer，`AssignWorldContext()` 仍同时写 engine-local 与 ambient，owner teardown 只在 `!LocalSharedState.IsValid()` 时才刷新 ambient；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L12-L22、L101-L113 — subsystem 初始化选择/创建 engine 后不写 world-context，而 `GetCurrent()` 仍只靠 `GetAmbientWorldContext()` 反查 world
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
- [ ] **P1.2** 📦 Git 提交：`[Runtime/Core] Fix: align world-context ownership and resolver inputs`
- [ ] **P1.2-T** 单元测试：补齐真实 subsystem / invalid world-context / teardown refresh 的正式合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptWorldContextLifecycleTests.cpp`
  - 测试场景：
    - 正常路径：真实 `UGameInstance` / `UWorld` 驱动的 subsystem 初始化后，`TryGetCurrentWorldContextObject()`、`GetAmbientWorldContext()` 与 subsystem `GetCurrent()` 都能稳定解析到同一 owner world
    - 边界条件：module-owned engine 与 subsystem-owned engine 分别 teardown 时，ambient world 会恢复到外层 scope 或稳定清空，而不是保留旧对象
    - 错误路径：previous world-context 在恢复前已失效或被 GC 回收时，所有读取入口都只返回 `nullptr`，不会继续把旧指针传给 `GetWorldFromContextObject()`
  - 测试命名：`Angelscript.TestModule.Core.WorldContext.OwnerLifecycleUsesValidatedResolve`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.2-T** 📦 Git 提交：`[Test/Core] Test: cover world-context ownership and invalidation`

- [ ] **P1.3** 把 engine 外部 hook 纳入 owner teardown，并为 late-init 补 immediate-ready 路径
  - 当前 `FAngelscriptEngine` 初始化会创建 `CodeCoverage`、注册 `OnPostEngineInit` lambda、注册 `OnGetOnScreenMessages.AddRaw(this, ...)`，并在 `AssetManager->OnCompletedInitialScan` 上挂测试发现；但 `Shutdown()` 没有保存或移除任何外部 delegate，`CodeCoverage` 自己也只注册 automation hooks 不解绑。同时 late-init 仍然等待“未来某次 `OnPostEngineInit`”，如果事件已过，就会静默错过 coverage hook 与 tests-ready
  - 本项目标不是再造一套新 boot framework，而是先把当前外部订阅全部纳入 owner teardown，并补一个“late init 时若宿主里程碑已过，立刻执行等价接线”的最小 ready path；必要时再把内部状态提升为结构化 milestone，但第一版先收住 UAF、重复 hook 与半完成状态
  - 需要把所有外部 hook 统一成显式 handle / lifetime token / remove path，避免继续依赖 `[&]` lambda 与 raw callback 在多 engine / destroy-recreate 场景下“碰巧还没炸”
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-07` / `A-10` / `A-18` / `A-25` / `C-09` 指出 delegate、coverage、late init 都没有对称 teardown 或 fallback ready path
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-05` / `Issue-28` 要求显式保存 handle、解绑外部事件，并把 tests-ready 从未来事件改成可见里程碑
    - [C] `RuntimeCore_TestGaps.md` — `D-02` / `D-14` 表明 delegate cleanup、coverage teardown、late-init readiness 当前没有正式回归
    - [E] `CrossComparison.md` — 参考实现普遍把旧 hook 显式脱钩 / restore，当前 AS 也应把 hook lifecycle 提升为 owner 合同而非一次性副作用
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1460-L1462、L1628-L1638、L2201-L2218、L1132-L1246 — 初始化会 `new CodeCoverage` 并注册 `OnPostEngineInit` / `OnGetOnScreenMessages` / `OnCompletedInitialScan`，但 `Shutdown()` 没有任何 `Remove` / `delete CodeCoverage`；`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp` L22-L28 — automation hooks 以 `AddRaw(this, ...)` 方式注册，当前也没有 remove 路径
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.h`
- [ ] **P1.3** 📦 Git 提交：`[Runtime/Core] Fix: detach runtime hooks and late-init ready path`
- [ ] **P1.3-T** 单元测试：补齐 destroy/recreate、late-init、automation hook 的 hook 生命周期合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeHookLifecycleTests.cpp`
  - 测试场景：
    - 正常路径：full engine 初始化后只注册一份 on-screen message / coverage / tests-ready hook，destroy-recreate 后新实例仍能正常工作
    - 边界条件：在 `OnPostEngineInit` 已经广播过的条件下再做 late init，coverage hook 与 test discovery 会立刻接线并进入 ready 状态，而不是留在半完成
    - 错误路径：销毁旧 engine 后手动广播 `OnGetOnScreenMessages` 或 automation 事件，不会再命中旧实例，也不会产生重复 coverage recorder
  - 测试命名：`Angelscript.TestModule.Core.RuntimeHooks.DestroyedEngineDetachesDelegates`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.3-T** 📦 Git 提交：`[Test/Core] Test: cover runtime hook teardown and late init`

### Phase 2: 再修 worker-thread 与异步分支

- [ ] **P2.1** 把 hot reload watcher 改成 engine-owned、可停止、可重启的线程服务
  - 当前 `StartHotReloadThread()` 直接 `Create(new FAngelscriptHotReloadThread())` 后丢失 thread/runnable 所有权，worker 线程内部再调用 `FAngelscriptEngine::Get()` 动态解析 owner；同时 worker 与 game thread 裸共享 `bWaitingForHotReloadResults`、文件队列和 `FileHotReloadState`，编译错误恢复还会临时借用同一个 watcher 并把 `bHotReloadThreadStarted` 卡成永久真值
  - 本项保持现有 `SoftReload / FullReload` 与 watcher 触发语义，不重做整套 D4；重点是把 watcher 改成 engine-owned 子对象，区分 temporary dialog polling 与 persistent runtime watching 两种 reason，并把 worker 产物改成受控移交而不是直接改共享容器
  - 同时补齐 runtime scanner 的删除/重命名检测，避免 non-editor 路径继续只有“新增/修改”语义；第一版即便仍由 game thread compile/commit，也必须先让 worker 生命周期与状态交换稳定下来
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-05` / `B-04` / `A-17` / `C-06` / `C-08` 指出 detached watcher、错误 owner、共享队列竞态、删除/重命名盲区、启动恢复后永久死线程
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-06` / `Issue-15` / `Issue-34` / `Issue-35` 要求 engine-owned watcher、locked handoff、restart state machine 与 delete/rename 语义
    - [C] `RuntimeCore_TestGaps.md` — 当前 runtime 热重载测试只验证 helper 直接塞队列，不覆盖真实 scanner / worker / shutdown 生命周期
    - [D] `ScriptLifecycle_ArchReview.md` — 后续 scheduler / reload request 也要求 worker 只产生 change event，不再直接隐式驱动主线程状态机
    - [E] `GapAnalysis.md` — D4 的正确方向是保持现有语义化 reload contract，再补 owner / observer / impact contract，而不是推倒重写
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1658-L1690、L2109-L2120、L2194-L2195、L2735-L2778、L2859-L2894 — watcher 当前无 thread handle、worker 通过 `Get()` 找 owner、编译错误恢复只改布尔位、worker 与主线程共用文件队列且 scanner 不产生删除事件；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L406-L419、L477-L478、L554-L555 — 共享状态仍由裸布尔与共享容器直接暴露
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
- [ ] **P2.1** 📦 Git 提交：`[Runtime/HotReload] Fix: own watcher lifecycle and synchronize scan handoff`
- [ ] **P2.1-T** 单元测试：补齐 watcher start / stop / restart / delete-rename / handoff 的正式合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadWatcherLifecycleTests.cpp`
  - 测试场景：
    - 正常路径：runtime watcher 启动后能完成一次 scan-handoff，shutdown 后线程句柄与等待状态全部清零，重建 engine 后 watcher 可再次启动
    - 边界条件：启动期编译报错先拉起 temporary watcher，修复脚本后切回 persistent watcher，后续文件修改仍能继续触发检测
    - 错误路径：删除/重命名文件与主线程消费并发时，队列不会损坏、不会永久卡在 `bWaitingForHotReloadResults`，且删除事件能被正确观测
  - 测试命名：`Angelscript.TestModule.HotReload.WatcherLifecycle.StartStopRestartIsSymmetric`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.1-T** 📦 Git 提交：`[Test/HotReload] Test: cover watcher lifecycle and scan handoff`

- [ ] **P2.2** 重构 threaded initialize 的 GT / worker phase，修复 `GameThreadTLD` 与 primary-context handoff
  - 当前 threaded initialize 直接在 worker thread 改写全局 `GameThreadTLD`，用 `volatile bool` 与主线程自旋同步；worker 还会在 `Initialize_AnyThread()` 里执行 `LoadModule()`，并把新建 `primaryContext` 留在 worker 侧临时 TLS，最终 `SharedState->PrimaryContext` 仍从真实 game-thread TLS 读到空值
  - 本项先把“必须在 game thread 上执行的事情”前移成显式 phase：bind module preload、primary-context 安装、shared-state 持久化；worker 只返回局部结果，不再直接改全局 `GameThreadTLD`
  - 这不是一步到位改造成完整 boot framework，而是先把 `AnyThread` 与 `GameThread` 的线程亲和边界变成可验证的实现合同；若后续继续吸收 `ArchReview` 的 startup participant 模型，本项产出的 phase helper 可直接复用
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-06` / `A-09` / `A-11` 指出 `volatile` 自旋、worker 改写 `GameThreadTLD`、primary-context 丢失三类初始化缺陷
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-08` / `Issue-31` / `Issue-37` 要求 GT preload bind modules、显式 handoff `primaryContext`、停止 off-thread `LoadModule`
    - [C] `RuntimeCore_TestGaps.md` — `D-02` 指出 threaded initialize 分支当前完全没有正式自动化
    - [D] `ScriptLifecycle_ArchReview.md` — startup phase / thread affinity 应显式化，不能继续让扩展方猜哪些 side effect 必须回到 game thread
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L825-L847、L917-L935、L1481-L1487、L1566-L1569 — worker 当前直接改写 `GameThreadTLD` 并靠 `volatile bool` 自旋同步，`Initialize_AnyThread()` 仍在 worker 上 `LoadModule()`，primary-context 创建后 `SharedState->PrimaryContext` 仍靠当时 TLS 快照；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L141 — `GameThreadTLD` 仍是单一静态入口
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
- [ ] **P2.2** 📦 Git 提交：`[Runtime/Core] Fix: separate threaded init phases and install primary context on game thread`
- [ ] **P2.2-T** 单元测试：补齐 threaded initialize 的 GT 亲和、usable-context 与 preload 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptThreadedInitializeTests.cpp`
  - 测试场景：
    - 正常路径：强制走 threaded initialize 后，`GameThreadTLD->primaryContext`、`SharedState->PrimaryContext` 与当前 owner engine 一致，且 bind modules 已在 game thread 预加载完成
    - 边界条件：关闭 threaded initialize 的路径与 threaded 路径在初始化完成后的可观察状态一致，不会重复触发 post-compile ready 事件
    - 错误路径：存在未预加载 bind module 或 worker 初始化失败时，会在 game thread 上给出明确失败而不是让 worker 静默 `LoadModule`；整个过程不会再把 `GameThreadTLD` 指向 worker TLS
  - 测试命名：`Angelscript.TestModule.Core.ThreadedInitialize.GameThreadAffinityAndPrimaryContextAreExplicit`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.2-T** 📦 Git 提交：`[Test/Core] Test: cover threaded initialize affinity and primary context handoff`

## 单元测试总览

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.1` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleLifecycleTests.cpp` | override attach/detach、module shutdown/restart、旧 owner 不残留 | P1 |
| `P1.2` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptWorldContextLifecycleTests.cpp` | 真实 subsystem world-context、invalid object restore、teardown refresh | P1 |
| `P1.3` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeHookLifecycleTests.cpp` | delegate cleanup、coverage teardown、late-init tests-ready | P1 |
| `P2.1` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadWatcherLifecycleTests.cpp` | watcher start/stop/restart、delete/rename、worker-main handoff | P1 |
| `P2.2` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptThreadedInitializeTests.cpp` | GT affinity、primary-context handoff、preload bind modules | P1 |

## 验收标准

1. `RuntimeModule` 的 normal owner 与 testing override 都有显式 owner 记录，`ShutdownModule()` / `ResetInitializeStateForTesting()` / 同进程二次启动后不再残留 `ContextStack` 或旧 engine。
2. world-context 在 subsystem、runtime-module 和普通 scope 三条路径上都具备一致合同：无效对象只返回 `nullptr`，owner teardown 会对称 refresh，真实 subsystem 初始化后 world-sensitive API 立即可用。
3. full engine destroy-recreate 后不会残留 `OnPostEngineInit`、`OnGetOnScreenMessages`、automation coverage hooks 或重复 recorder；late init 不会再停在 “初始化成功但 tests/coverage 未 ready” 的半状态。
4. hot reload watcher 具备显式 start / stop / restart / handoff 合同，non-editor 删除与重命名可被检测，worker 与主线程不再通过裸共享容器竞态交换状态。
5. threaded initialize 不再在 worker thread 上改写全局 `GameThreadTLD` 或调用 `LoadModule()`；初始化完成后 usable-context 与 shared-state 持久化一致，destroy-recreate 后不会跨 epoch 残留旧 `primaryContext`。

## 风险与注意事项

### 风险

1. **world-context 改成 validated/weak resolve 后，会暴露过去被 stale pointer 掩盖的空值调用点**
   - 缓解：本计划把 invalid-object、teardown refresh 与真实 subsystem 初始化测试放在同一批推进，先把 `nullptr` 合同定清楚再扩大改动面。
2. **delegate / coverage / late-init 收口后，旧测试可能依赖“重复注册也能凑合工作”的错误行为**
   - 缓解：destroy-recreate 与 late-init 回归必须先补，再改 ready path 与 hook 拆除。
3. **hot reload watcher 改成 engine-owned + synchronized handoff 后，standalone/non-editor 的时序会发生变化**
   - 缓解：保持现有 `SoftReload / FullReload` 语义不变，只收口 owner、restart 与 handoff；任何 scheduler 化都放到后续再做。
4. **threaded initialize phase 化后，部分启动耗时会回到 game thread**
   - 缓解：第一阶段只前移必须 GT 亲和的 `LoadModule` 与 primary-context 安装，避免一次性把整条初始化链都搬回主线程。

### 已知行为变化

1. `RuntimeModule` reset/shutdown 后不再允许旧 engine 继续作为 current engine 残留在 `ContextStack`。
2. `TryGetCurrentWorldContextObject()`、`GetAmbientWorldContext()` 与 subsystem `GetCurrent()` 在遇到失效 world-context 时将更倾向返回 `nullptr`，而不是继续泄漏旧对象。
3. late init 若发生在 `OnPostEngineInit` 之后，将立即补 coverage/test-discovery ready path，而不是继续等待一个不会再来的事件。
4. non-editor 启动期编译错误恢复后，watcher 将重建或复位到可继续检测的状态，不再允许 `bHotReloadThreadStarted` 永久卡死。
5. threaded initialize 将不再依赖 off-thread `GameThreadTLD` 改写与 off-thread `LoadModule`，后续任何新增 startup 扩展都必须显式声明 GT / worker 亲和边界。

---

## 本轮补充（2026-04-09）

### Phase 1 补充：把 `ContextStack`、scope 恢复和 tick 仲裁收成同一套合同

- [ ] **P1.4** 收口 `ContextStack` / `FAngelscriptEngineScope` / subsystem tick 的嵌套与 owner 仲裁
  - 当前 `ContextStack`、ambient world restore 和 tick owner 仍建立在“全局 current-engine 碰巧正确”的隐式前提上：同 engine 嵌套 scope 会提前弹空外层上下文，ambient scope 与 engine scope 叠加时会丢 world，subsystem/module tick 又在没有显式 scope 的情况下直接推进 `Engine->Tick()`；这些问题叠加后，`Deinitialize()` 栈顶错位、clone wrapper 被误 tick、`GEngine == nullptr` 的恢复分支都会把 resolver 拉到错误实例
  - 本项不重做 `Arch-SL-12` 所建议的完整 registry/runtime handle，只先把现有原语修到自洽：`Push/Pop` 必须保留深度、owner teardown 必须在 detach 成功后才能 `Shutdown()`、tick 入口必须用显式 scoped helper 绑定 current engine/world、tick 仲裁要从进程级 `ActiveTickOwners` 退到“当前 engine 是否已有 owner”的粒度
  - 实施顺序上先修 `ContextStack` 与 ambient restore 原语，再收 subsystem/runtime module 的 tick helper 与 `ShouldTick()` 资格判断，最后补 `HasGameWorld()` 的 `GEngine` 空指针防护和 `GetCurrent()`/`Deinitialize()` 的错误路径诊断，避免后续其它生命周期修复继续建立在错误上下文上
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `B-06` / `B-07` / `B-14` / `B-17` / `A-22` / `A-23` 指出同 engine 嵌套 scope 提前出栈、ambient world 不恢复、进程级 `ActiveTickOwners` 串台、生产 tick 缺 scope、`HasGameWorld()` 解引用空 `GEngine`、subsystem teardown 会留下已 shutdown engine
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-13` / `Issue-19` / `Issue-20` / `Issue-26` / `Issue-27` 已给出“tick 入口加 scope、teardown 先 detach、tick owner 按 engine 仲裁、scope 恢复对称化”的具体方案
    - [C] `RuntimeCore_TestGaps.md` — `Issue-07` / `Issue-32` / `NewTest-10` 表明核心 lifecycle 测试没有稳定断言 create-mode、`ContextStack` 隔离和真实 `GameInstanceSubsystem` 运行场景
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-12` 明确指出 current-engine 仍依赖 ambient context + 进程级 tick owner，生命周期 owner 不是一等 runtime identity
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L391-L409、L437-L512 — `ContextStack` 仍只做裸 `Push/Pop`，`Reset()` 仍未使用 `PreviousWorldContext`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L710-L724 — `FAngelscriptGameThreadScopeWorldContext` 仍以全局回滚析构直接写 ambient world；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L8-L42、L81-L118 — `ActiveTickOwners` 仍是进程级计数，`Tick()`/`Deinitialize()` 仍分别裸调 `PrimaryEngine->Tick()` 与无条件 `Shutdown()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` L186-L194 — fallback tick 仍未建立 `FAngelscriptEngineScope`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2781-L2846 — `HasGameWorld()` 直接依赖 `GEngine`，`ShouldTick()` 只判断 `Engine != nullptr`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
- [ ] **P1.4** 📦 Git 提交：`[Runtime/Core] Fix: make context scope and tick ownership symmetric`
- [ ] **P1.4-T** 单元测试：补齐 `ContextStack` / ambient restore / subsystem tick 的组合合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptContextStackTickLifecycleTests.cpp`
  - 测试场景：
    - 正常路径：同一 engine 双层 `FAngelscriptEngineScope` + 外层 `FAngelscriptGameThreadScopeWorldContext` 叠加后，内层析构仍保持外层 current-engine 与 ambient world；真实 subsystem adopt/own 两条路径的 `Tick()` 都能在无预先手工 scope 时把 `Get()` 绑定到被 tick 的 engine
    - 边界条件：两个不同 engine 分别由 subsystem 与 runtime fallback 驱动时，tick owner 只抑制对应 engine 的 fallback tick，不会全局熄火；`GEngine == nullptr` 时 `Tick()` 走恢复分支不崩溃
    - 错误路径：制造 `Deinitialize()` 前栈顶错位或 owner + 临时 scope 叠加的异常顺序，断言失效 engine 不会残留在 `ContextStack`，后续 `TryGetCurrentEngine()` 只返回外层有效实例或 `nullptr`
  - 测试命名：`Angelscript.TestModule.Core.ContextStackTick.ScopeAndOwnerContractsStaySymmetric`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.4-T** 📦 Git 提交：`[Test/Core] Test: cover context stack restore and tick ownership`

### Phase 2 补充：把 full teardown 做成真正的 engine epoch 收口

- [ ] **P2.3** 收口 full teardown 的 `ContextPool`、package epoch 与 export-only 终止分支
  - 当前 full owner 销毁只清当前线程可见的 pool 与字段引用，没把 thread-local pool registry、package root/name contract、`bForcedExit` 早退分支纳入统一 epoch cleanup；结果是旧 context、旧 package identity 和导出模式残余 watcher/回调会一起跨越 engine 周期
  - 本项的目标不是照 `Arch-SL-21` 直接重写 executor，而是先把当前 full teardown 做到可验证：`GlobalContextPool` 所有读写统一加锁并在 shutdown 前停止再入；所有 thread-local pool 都能按 `ScriptEngine` 枚举并 drain；package 必须区分 canonical owner 与 isolated full owner 的命名/退根合同；导出型 compile 在请求 exit 后立刻停止 interactive runtime 接线
  - 实施顺序上先补 `thread_local` pool registry + shutdown gate，再做 package unroot / unique naming / recreate contract，最后把 `bForcedExit` 提前返回整合到 export-only finalizer，避免 teardown 清理继续建立在“先半初始化再退出”的错误假设上
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-06` / `A-24` / `A-26` 指出 root-set package 只丢字段不退根、`GlobalContextPool` shutdown 无锁、shared-state teardown 只清当前线程 local pool
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-18` / `Issue-29` / `Issue-46` / `Issue-47` 已把全局池加锁、thread-local pool registry、unique package namespace、export-only 提前终止列为具体修复项
    - [C] `RuntimeCore_TestGaps.md` — `Issue-13` / `Issue-07` 指出现有 full-destroy 核心用例没有证明旧类/包真正回收，也没有覆盖 `FAngelscriptEngine` 关键 create/destroy 生命周期合同
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-21` 指出 context pooling 目前被锁成公共执行契约，生命周期收口缺少显式 owner/seam；在当前阶段至少应先把 pool teardown 做成可枚举、可停机的受管资源
    - [E] `CrossComparison.md` `[D4]` — 参考实现的 `state close / reload` 都伴随明确 cache teardown，当前 AS 也应把 epoch 结束时的 cache/pool/package 收口提升成显式合同
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1185-L1197、L1905-L1913 — shutdown 仍无锁遍历 `GlobalContextPool`，其余线程的 `thread_local` pool 只在线程退出时释放；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1287-L1289、L1722-L1750、L1868-L1903 — 初始化前只清当前线程 local pool，请求/归还 context 仍优先写各线程本地缓存；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L878-L881、L1382-L1386、L1245-L1246 — testing/full 初始化仍创建固定 `/Script/Angelscript*` root-set 包，shutdown 只清空指针；同文件 `rg "RemoveFromRoot|RF_MarkAsRootSet"` 仅命中 `RF_MarkAsRootSet`，当前没有 `RemoveFromRoot()` 路径；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1571-L1620 — `bForcedExit` 请求退出后仍继续创建 `HotReloadTestRunner` 并启动 watcher
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
- [ ] **P2.3** 📦 Git 提交：`[Runtime/Core] Fix: drain pooled contexts and isolate full-engine epochs`
- [ ] **P2.3-T** 单元测试：补齐 full teardown epoch cleanup、package identity 与 export-only 早退合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFullTeardownEpochTests.cpp`
  - 测试场景：
    - 正常路径：full owner shutdown 前后分别在 game thread 与 worker thread 建立 pooled context，teardown 完成后目标 `ScriptEngine` 不再残留于任何 local/global pool；重新创建 isolated full engine 时拿到新的 package identity 和干净 context 基线
    - 边界条件：canonical runtime owner 与 isolated/testing full owner 并存时，canonical 仍保持固定 package 名，isolated owner 使用唯一 package 名且彼此不共享 `AngelscriptPackage` / `AssetsPackage`；开启 export-only 模式后仍会正确请求 exit，但不会创建 `HotReloadTestRunner` 或启动 hot reload watcher
    - 错误路径：worker thread 在 owner shutdown 竞态归还 context 或旧 package 尚未 GC 时，不会把 stale context 重新挂回 retired engine，也不会让第二轮 full engine 静默复用上一轮 package/root 状态
  - 测试命名：`Angelscript.TestModule.Core.FullTeardown.EpochCleanupAndPackagesAreSymmetric`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.3-T** 📦 Git 提交：`[Test/Core] Test: cover full teardown epoch cleanup`

## 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.4` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptContextStackTickLifecycleTests.cpp` | 同 engine 嵌套 scope、ambient restore、subsystem/runtime tick 仲裁、栈顶错位 deinitialize | P1 |
| `P2.3` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptFullTeardownEpochTests.cpp` | 跨线程 context-pool teardown、package reclaim/unique identity、export-only early exit | P1 |

## 验收标准补充

1. `ContextStack` 与 `FAngelscriptEngineScope` 在同 engine 嵌套、ambient scope 叠加、subsystem/module tick 和 owner teardown 错误路径下都保持对称；production `Tick()` 不再依赖外部预先残留的 current-engine。
2. full owner teardown 会同时收口 `GlobalContextPool`、各线程 `thread_local` context pool、package root/name 状态和 export-only 初始化尾项；下一轮 full engine 重建不会静默复用上一轮的 stale context、package 或 watcher/delegate。

## 风险与注意事项补充

### 风险

1. 把 tick 与 owner teardown 都收口到显式 scope / detach helper 后，会暴露一批历史调用点对 ambient current-engine 的隐式依赖；需要先补 focused lifecycle 回归，再让更大范围用例跟进。
2. `thread_local` pool registry、package naming policy 与 export-only 早退会同时触碰热路径、测试路径和工具命令路径；实现时必须坚持“锁内摘取、锁外释放”和“canonical owner / isolated owner 分流”两条边界，避免把新合同引入为全局性能或兼容性回归。

### 已知行为变化

1. subsystem 与 runtime fallback tick 将只推进与当前 owner 合法绑定的 engine；clone wrapper 或无 owner wrapper 不再因为 `Engine != nullptr` 就被当成可安全 tick 的 primary engine。
2. isolated/testing full engine 的 package 不再默认复用固定 `/Script/Angelscript*` 身份；export-only 初始化在请求退出后也不会继续接线 hot reload watcher、test runner 或其它长期运行时设施。

---

## 本轮补充（2026-04-09 第二轮）

### Phase 3 补充：把 full/test/clone 的生命周期真相与测试入口语义定清

- [ ] **P3.1** 收口 `full / testing / clone` 的 lifecycle state、ready milestone 与后续同步合同
  - 当前 RuntimeCore 把 `ScriptsCompiled`、`SharedStateReady`、`AssetScanReady`、`RuntimeReady` 四类事实压缩进 `bIsInitialCompileFinished`、`bCompletedAssetScan` 和无参 `OnInitialCompileFinished` 三个松散信号里；结果是 full owner、testing owner、clone wrapper 和 late-init 路径都会对“runtime 是否真的 ready”给出互相矛盾的答案
  - 本项不重复 `Plan_TestEngineIsolation.md` 已规划的 static-flag 去全局化，而是先补 authoritative lifecycle contract：把 ready truth 收口成结构化状态或 report，明确 `Bootstrapped`、`ScriptsCompiled`、`SharedStateReady`、`AssetScanReady`、`RuntimeReady` 的顺序和 owner；clone 必须继承 source 的可见生命周期，并在 source 后续推进 asset-scan/tests-ready 时同步可观测状态
  - 实施上先新增 shared/authoritative lifecycle 读入口与新 ready event，再让 `CanUseGameThreadData()`、legacy `OnInitialCompileFinished`、clone adopt、late-init test discovery 门禁改读同一份真相；testing path 继续保留兼容 facade，但不能再伪装成“脚本已编译完成”的 runtime-ready owner
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `B-21` / `B-22` 指出 clone 漏同步 `bIsInitialCompileFinished` 与 `bCompletedAssetScan`，source 后续 ready 变化不会回流到 clone wrapper
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-24` / `Issue-28` 要求把 clone lifecycle state 改成共享真相，并把 late init / asset-scan / tests-ready 从未来事件等待改成显式 readiness 合同
    - [C] `RuntimeCore_TestGaps.md` — `NewTest-01`、`Issue-12`、`Issue-22` 表明当前自动化只看 create-mode smoke 或耗时，不断言 clone/source 生命周期状态与 fallback contract
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-50` / `Arch-SL-51` 明确指出 `OnInitialCompileFinished` 早于 `SharedState` 提交，且单一 `bIsInitialCompileFinished` 已无法表达 full/test/clone 的真实生命周期
    - [E] `GapAnalysis.md` `D9` — 参考对比已指出 `CreateTestingFullEngine()->InitializeForTesting()` 只是 minimal bootstrap，测试 profile 不能继续与 runtime-ready owner 混为一谈
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L852-L856、L922-L941、L1653-L1655 — `Initialize()` 仍先 `PostInitialize_GameThread()` 广播 `OnInitialCompileFinished`，后续才提交 `SharedState`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L628-L647、L2207-L2209、L2848-L2856 — clone 仍靠 `AdoptSharedStateFrom()` 快照复制 wrapper 状态，`bCompletedAssetScan` 只在 source 自身回调里置真；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L242-L247、L481-L488 — game-thread guard 与公开查询仍直接依赖 wrapper-local `bIsInitialCompileFinished`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`
- [ ] **P3.1** 📦 Git 提交：`[Runtime/Core] Refactor: formalize runtime-ready milestones and shared lifecycle state`
- [ ] **P3.1-T** 单元测试：补齐 full/test/clone 的 ready 顺序、clone 同步与 late-init readiness 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeLifecycleStateTests.cpp`
  - 测试场景：
    - 正常路径：full owner 初始化按顺序推进 `ScriptsCompiled -> SharedStateReady -> RuntimeReady`，clone 在 source ready 后创建时能立即继承 source 的 compiled/ready 状态与共享句柄
    - 边界条件：testing/minimal profile 只进入 `Bootstrapped` 或等价兼容态，不再被新 API 识别为 `ScriptsCompiled`；clone 若创建在 asset-scan 之前，source 后续完成扫描后 clone 也能观察到 `AssetScanReady/TestsReady`
    - 错误路径：进入 clone scope 后 `CanUseGameThreadData()`、`CheckGameThreadExecution()` 与 late-init discover/coverage 门禁不再因为 wrapper-local 默认值或已错过的 `OnPostEngineInit` 而放宽/卡死
  - 测试命名：`Angelscript.TestModule.Core.Lifecycle.ReadyMilestonesAndCloneProjectionStayConsistent`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.1-T** 📦 Git 提交：`[Test/Core] Test: cover runtime-ready milestones and clone lifecycle state`

- [ ] **P3.2** 收口 `Create()` / `CreateForTesting()` / `Testing-Full` 的公开语义，显式区分 minimal bootstrap 与 production-parity owner
  - 当前 public factory surface 仍把三类完全不同的东西塞进近似命名里：`Create()` 直接别名 `CreateTestingFullEngine()`，`CreateForTesting(Clone)` 又会在没有 current engine 时静默 fallback 成 full；同时 core tests 仍会把 `Testing-Full` 产物当成 production-like full engine 的健康信号
  - 本项不重复 `Plan_AngelscriptEngineBindAndFileWatchValidation.md` 已在做的 startup-bind 观测，而是先把入口语义定清：保留兼容层但弃用误导性的 `Create()`，为测试引擎显式区分 `MinimalBootstrap` 与 `ProductionParity` profile，并要求所有 helper / fixture / 文档 / 核心回归测试显式断言 creation mode 与 profile
  - 实施上优先新增 profile-aware helper 与 diagnostics，把 “current-engine 内 clone、无 current-engine fallback full、module/subsystem owner、production-parity full owner” 四类入口各自命名；之后迁移 `CreateDestroy`、performance startup 和 type-metadata core tests，禁止继续用 minimal testing engine 冒充正式 runtime owner
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `C-03` 指出 `Create()` 已退化成 `CreateTestingFullEngine()` 别名，名字表达的 public full-engine 语义与真实实现不符
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-48` 要求明确退役失真的 `Create()`，强制调用方在 testing full 与正式 owner lifecycle 之间做显式选择
    - [C] `RuntimeCore_TestGaps.md` — `NewTest-01`、`Issue-12`、`Issue-15`、`Issue-22` 指出现有 engine core/performance 用例没有断言 `CreateForTesting` 的 create-mode、source-engine 与 fallback full 合同
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-51` 要求 future API 区分 `Bootstrapped`、`ScriptsCompiled`、`RuntimeReady`，testing env 不能继续与 full owner 共用一套含混入口
    - [E] `GapAnalysis.md` `D9` — 参考对比已确认 `Testing-Full` 初始化与 production-like bind graph 不是同一个 owner，测试基础设施需要单独的 production-parity profile
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L127-L136 — public API 仍并列暴露 `Create()`、`CreateTestingFullEngine()` 与默认 `CreateForTesting(Clone)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L605-L607、L615-L625 — `Create()` 仍直接返回 `CreateTestingFullEngine()`，后者固定走 `InitializeForTesting()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L654-L666 — `CreateForTesting(Clone)` 在没有 current engine 时仍静默回退为 full
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp`
- [ ] **P3.2** 📦 Git 提交：`[Runtime/Core] Refactor: split testing factory profiles from runtime owners`
- [ ] **P3.2-T** 单元测试：补齐 factory profile、clone/full fallback 与 legacy 兼容面的显式合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineFactoryProfileTests.cpp`
  - 测试场景：
    - 正常路径：在显式 `FAngelscriptEngineScope` 内调用 `CreateForTesting(..., Clone)` 必须得到 clone，并断言 `GetCreationMode()`、`OwnsEngine()`、`GetSourceEngine()`、`GetScriptEngine()` 与 source/full owner 的关系正确；production-parity helper 则走完整 runtime owner profile
    - 边界条件：无 current engine 时 `CreateForTesting(..., Clone)` 必须显式暴露 fallback full 与其 profile，不再由测试名或耗时隐式猜测；minimal bootstrap 与 production-parity profile 的差异应能被统一查询 API 观察到
    - 错误路径：legacy `Create()` / 旧 `CreateFullTestEngine()` 路径必须给出 deprecation 或显式兼容提示，且依赖 production-parity 合同的测试若误用 minimal profile，应立即失败而不是默默通过
  - 测试命名：`Angelscript.TestModule.Core.Lifecycle.FactoryProfilesAndFallbackModesAreExplicit`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.2-T** 📦 Git 提交：`[Test/Core] Test: make engine factory profiles and fallback modes explicit`

## 单元测试总览补充（二）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P3.1` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeLifecycleStateTests.cpp` | ready milestone 顺序、clone 生命周期投影、late-init/asset-scan readiness | P1 |
| `P3.2` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineFactoryProfileTests.cpp` | minimal bootstrap vs production-parity profile、scoped clone、fallback full、legacy factory 兼容 | P1 |

## 验收标准补充（二）

3. RuntimeCore 能显式区分 `Bootstrapped`、`ScriptsCompiled`、`SharedStateReady`、`AssetScanReady`、`RuntimeReady`；clone 与 testing path 不再依赖 wrapper-local 脏布尔值伪装 ready，late-init 和 asset-scan 进度能稳定传播到共享 runtime 视图。
4. `Create()` / `CreateTestingFullEngine()` / `CreateForTesting()` 的用途与 profile 明确可观测；production-parity 测试入口、minimal bootstrap 测试入口、scoped clone 与 no-current fallback full 四条路径都拥有稳定断言，不再由 helper 名字或耗时侧面猜测。

## 风险与注意事项补充（二）

### 风险

1. 一旦把 ready milestone 与 profile 结构化，现有依赖 `OnInitialCompileFinished`、`bIsInitialCompileFinished` 或 `Create()` 名字语义的测试/工具会集中暴露；需要保留兼容 facade，同时分批迁移监听者与 helper。
2. testing helper 从“单一 full engine”拆成 minimal bootstrap / production-parity 后，会触及现有 core/performance/debugger 测试与文档示例；迁移时必须先补 focused contract tests，再逐步替换旧 helper，避免一次性制造大面积假红。

### 已知行为变化

3. legacy `OnInitialCompileFinished` 将不再被视为“runtime-ready”唯一真相；任何需要 `SharedState`、`PrimaryContext`、`DebugServer` 或 tests-ready 的调用点，都必须改订阅新的 ready milestone 或显式查询新的 lifecycle state。
4. `Create()` 与旧 `Testing-Full` helper 不再适合作为 production owner 的代称；需要 production-parity 行为的测试与工具必须使用显式 profile/owner 入口，而不是继续依赖 minimal bootstrap 的历史兼容层。

---

## 深化 (2026-04-09 第三轮)

本轮只追加当前文档尚未单独展开的 5 个切口：`clone` 的执行上下文投影、`clone` 的元数据投影、subsystem adopt 生命周期令牌、`AssetManager` 缺席时的 tests-ready barrier，以及 engine-owned settings snapshot。它们都来自本轮五维输入的新增交叉确认，不重复 `P1.4 / P3.1 / P3.2` 已覆盖的 tick 基线、ready milestone 与 factory profile 语义。

### Phase 4: 深化 clone / subsystem / settings 的 owner 合同

- [ ] **P4.1** 补齐 clone 对 current-context 与 debug/line-callback 能力的共享投影
  - `P3.1` 已经把 clone 的 ready milestone 提上日程，但当前 clone wrapper 仍只共享 `SharedState`，没有共享 current-context 与 debug capability；只要 `TryGetCurrentEngine()` 落到 clone，`ShouldUseEditorScriptsForCurrentContext()`、`ShouldUseAutomaticImportMethodForCurrentContext()`、`IsScriptDevelopmentModeForCurrentContext()`、`TryGetCurrentWorldContextObject()` 与 `UpdateLineCallbackState()` 就会从 clone 自己的默认字段或空 `DebugServer` 读取状态，静默把 source runtime 的 editor/import/debug 语义降级成 `false/nullptr`
  - 本项要把“共享同一 VM 的执行上下文能力”从 wrapper-local 状态提升为 shared-state 或显式 owner registry：clone/source 对 world-context、editor scripts、automatic imports、debug/coverage line callback 的可见结果必须一致，不再允许某个空 clone wrapper 把全局 line-callback static flag 重算成关闭
  - 第一阶段先做最小 shared projection：`CreateCloneFrom()` / `AdoptSharedStateFrom()` 明确同步 runtime flags 与 debug capability，再让 current-context helper 和 `UpdateLineCallbackState()` 全部转读共享事实；第二阶段再把 `DebugServer` / coverage 这种“谁请求 line callback”收口成聚合重算入口，消除 clone/source 最后写入赢
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `B-11` 指出 clone wrapper 没有继承共享 `DebugServer` 能力，当前 engine 解析落到 clone 时调试状态会失真；`B-16` 指出 clone 没有投影 source 的上下文状态，current-context API 在 clone scope 下会系统性读错；`D-11` 指出这两类 clone 投影缺口当前没有直接回归保护
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-39` 要求把 context/debug callback 从 `Get()` 反查改成按来源 owner 路由；`Issue-40` 要求把 line callback 决策从单 wrapper 的局部状态升级为 shared/global 聚合状态
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-16` 明确指出 clone 现在只是共享 VM 的半投影视图，不适合继续把 wrapper-local 状态当成 runtime 真相
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L628-L647、L682-L714、L5429-L5460 — clone 创建后只共享 `SharedState` 并调用最小 `AdoptSharedStateFrom()`；current-context helper 直接读取当前 wrapper 的局部字段；`UpdateLineCallbackState()` 也只看当前 wrapper 的 `DebugServer` / `CodeCoverage` 后直接覆盖进程级 `asCContext` static flag
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`
- [ ] **P4.1** 📦 Git 提交：`[Runtime/Core] Fix: align clone context and debug projection with shared runtime`
- [ ] **P4.1-T** 单元测试：补齐 clone 的 current-context / debug / line-callback 投影合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCloneContextDebugProjectionTests.cpp`
  - 测试场景：
    - 正常路径：source full engine 完成初始化并开启 editor/import/debug 相关能力后创建 clone，在 clone scope 下 `ShouldUseEditorScriptsForCurrentContext()`、`ShouldUseAutomaticImportMethodForCurrentContext()`、`IsScriptDevelopmentModeForCurrentContext()` 与 world-context 读取结果与 source 保持一致
    - 边界条件：clone 创建发生在 source 已连接 debug server 之后和之前两种时机时，后续 debug start/stop 或 coverage 开关变化都能同步反映到 clone 视图，不会要求重建 clone
    - 错误路径：source 与 clone 交替成为 current engine 时，global line-callback 开关不会被空 clone wrapper 误关；source runtime 仍在调试/coverage 中时，clone scope 不会把 `asCContext` static flag 重算成关闭
  - 测试命名：`Angelscript.TestModule.Core.Clone.ContextAndDebugProjectionStayAligned`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.1-T** 📦 Git 提交：`[Test/Core] Test: cover clone context and debug projection`

- [ ] **P4.2** 把 clone 的模块/类型索引提升为共享元数据注册表，而不是继续依赖空白 wrapper 缓存
  - 当前 clone 虽然与 source 共享同一底层 `asIScriptEngine`，但 `AdoptSharedStateFrom()` 完全不投影 `ActiveModules`、`ModulesByScriptModule`、`ActiveClassesByName`、`ActiveEnumsByName`、`ActiveDelegatesByName`；结果是 `GetModule/GetClass/GetEnum/GetDelegate/GetActiveModules/DiscoverTests` 在 clone 视角下天然失真，现有 multi-engine 自动化只能靠 `TrackNamedModule()` 手工往 clone 塞索引才能继续验证
  - 本项的目标不是把 clone 直接升级成独立 runtime，而是先把“共享 VM 已知元数据”定义成 shared-state 正式合同：source/clone 至少要看见同一批 authoritative module/type registry，再允许 wrapper 叠加自己的 alias/overlay；只有这样，后续 hot reload、test discovery、debugger metadata 与 clone 查询接口才会给出一致结果
  - 先把 authoritative registry 收进 shared-state，再把 `GetActiveModules()`、`GetModuleByFilename()`、`GetClass()`、`GetEnum()`、`GetDelegate()`、`DiscoverTests()` 全部改成 `overlay -> shared registry` 解析；执行时同步删弱 `TrackNamedModule()` 这种测试专用后门，避免生产缺陷继续被夹具遮蔽
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `B-20` 指出 clone wrapper 不会继承 source 的模块索引与类型索引；`D-19` 指出当前自动化依赖 `TrackNamedModule()` 手工补 clone 索引，真实模块投影合同没有回归保护
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-41` 要求把共享 VM 的 authoritative metadata registry 提升到 shared-state，而不是继续让 clone 从空本地 map 起步
    - [C] `RuntimeCore_TestGaps.md` — `Issue-27` 与 `NewTest-01` 表明现有 clone 相关测试只锁 creation-mode / 性能侧信号，没有直接证明 clone 在不借助测试后门时也能解析 source 元数据
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-16` 明确指出 clone 当前不共享完整 live module graph，不能继续把“共享 VM + 空 wrapper 账本”当作正式 runtime 视图
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L313-L323、L385-L391 — `GetActiveModules()` 与各类查询都只读 wrapper-local `ActiveModules` / `Active*ByName`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2232-L2249、L2848-L2856 — `DiscoverTests()` 只遍历 `GetActiveModules()`，而 `AdoptSharedStateFrom()` 仍只复制 package/root/少量布尔状态，不处理任何 module/type 索引
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
- [ ] **P4.2** 📦 Git 提交：`[Runtime/Core] Refactor: share clone metadata registry with source runtime`
- [ ] **P4.2-T** 单元测试：补齐 clone 的 shared metadata projection 与无后门查询合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCloneMetadataProjectionTests.cpp`
  - 测试场景：
    - 正常路径：source full engine 先加载模块并暴露类/枚举/委托后创建 clone，在不调用 `TrackNamedModule()` 的前提下，clone 也能解析同一批 `GetModule/GetClass/GetEnum/GetDelegate` 结果
    - 边界条件：clone 创建在 source 后续追加模块或完成一次增量编译之前时，source 更新 authoritative registry 后 clone 视图会同步跟进，而不是冻结在创建瞬间
    - 错误路径：source 丢弃或热更某个模块后，clone 不会继续返回 stale module/type descriptor，`DiscoverTests()` 也不会保留已被 source 移除的测试集合
  - 测试命名：`Angelscript.TestModule.Core.Clone.SharedMetadataRegistryMatchesSource`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.2-T** 📦 Git 提交：`[Test/Core] Test: cover clone metadata projection without TrackNamedModule`

- [ ] **P4.3** 给 subsystem adopt 外部 engine 引入生命周期令牌与 owner-aware detach，禁止长期持有临时 wrapper 裸指针
  - `P1.4` 已经把 tick 仲裁和 `ContextStack` 对称性列为主线，但 subsystem 仍没有 adopted-engine 的有效性合同：`Initialize()` 直接把 `TryGetCurrentEngine()` 的返回值写进长期 `PrimaryEngine`，之后 `Tick()` / `Deinitialize()` 都继续解引用这根裸指针；一旦 adopt 发生在临时 `FAngelscriptEngineScope`、clone scope 或 runtime-module owner 后续 reset 的窗口内，subsystem 就会把短生命周期 wrapper 升级成 `UGameInstance` 级长期状态
  - 本项要把 adopted 与 owned 两条路径彻底拆开：adopt 只接受带有效 lifetime token 的长期 owner，`Tick()` / `Deinitialize()` 先判 token 再解引用；若外部 owner 已失效，subsystem 只做 detach/清空状态，绝不再调用外部 owner 的 `Shutdown()`
  - 先在 `FAngelscriptEngine` 暴露只读 lifetime token，再把 subsystem 的裸 `PrimaryEngine*` 扩成“指针 + 弱生命周期句柄”组合；随后补统一 `ResolvePrimaryEngine()` / `DetachAdoptedEngine()` helper，把 scope/clone/module owner 失效的 cleanup 收到一处
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `B-13` 指出 subsystem 会把临时 `current-engine` wrapper 固化成长期裸指针；`D-10` 指出“真实 subsystem 初始化 + scoped/clone engine 生命周期”当前没有正式回归
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-14` 与 `Issue-51` 都要求把 adopted 分支改成 token-guarded owner 合同，区分 adopt detach 与 owned shutdown
    - [C] `RuntimeCore_TestGaps.md` — `NewTest-10` 要求把 `Subsystem/` 目录升级为真实 `GameInstanceSubsystem` 生命周期场景，覆盖 adopt/own/tick/deinitialize 正向合同
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-12` 强调 engine identity 与 tick owner 不能继续依赖 ambient/global state，长期 owner 必须围绕显式 runtime identity 建模
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L17-L29、L34-L45、L81-L85 — subsystem adopt 分支直接保存 `PrimaryEngine` 裸指针，后续继续 `Tick()` / `Shutdown()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L210、L451-L452 — engine 已有 `SourceLifetimeToken` / `LifetimeToken` 机制，但 subsystem 当前没有消费；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` L27-L38 — module owner reset 仍会直接销毁外部 engine
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
- [ ] **P4.3** 📦 Git 提交：`[Runtime/Core] Fix: guard subsystem adopted engine lifetime with tokens`
- [ ] **P4.3-T** 单元测试：补齐 subsystem adopt 外部 engine 的判活、tick 与 detach 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemAdoptedEngineLifetimeTests.cpp`
  - 测试场景：
    - 正常路径：subsystem adopt 长生命周期 owner engine 后，`Tick()` 只推进该 engine，`Deinitialize()` 只 detach 而不关闭外部 owner
    - 边界条件：同一 subsystem 连续跨多帧 tick 且 owner 仍然有效时，`ResolvePrimaryEngine()` 始终返回同一实例，world-context / current-engine 解析不会漂移到别的 wrapper
    - 错误路径：subsystem 初始化发生在临时 `FAngelscriptEngineScope` 或 clone/source 后续失效的窗口时，后续 `Tick()` / `Deinitialize()` 会自动失效并停止解引用旧 engine，不会对外部 owner 误调用 `Shutdown()`
  - 测试命名：`Angelscript.TestModule.Subsystem.AdoptedEngine.LifetimeGuardsTickAndDetach`
  - 隔离方式：`FAngelscriptEngineScope（配合真实 UGameInstance/UWorld fixture）`
- [ ] **P4.3-T** 📦 Git 提交：`[Test/Subsystem] Test: cover adopted engine lifetime and detach semantics`

- [ ] **P4.4** 把 `AssetManager` 缺席分支的 tests-ready barrier 从“只做发现不迁移状态”收口成统一 readiness helper
  - `P1.3` 已经识别 late-init / delegate cleanup 问题，但当前还有一条单独漏口：`OnPostEngineInit` 在 `AssetManager` 存在时会同时 `DiscoverTests()` 并置 `bCompletedAssetScan = true`，而 `AssetManager == nullptr` 时只做 `DiscoverTests()`；后续 hot reload test 准备和 post-compile 测试再发现又都把 `bCompletedAssetScan` 当成硬门槛，最终形成“首次发现成功，但后续永远认为 barrier 未完成”的 silent degradation
  - 本项不重做整套 boot coordinator，只先把“测试发现 barrier 已满足”从 `AssetManager` 扫描事实中拆出来：无论来自真实 `OnCompletedInitialScan` 还是 fallback 分支，都必须进入同一 ready 终态，再由调用方按需区分“真实经过 AssetManager”与“fallback 直接放行”
  - 先新增统一 helper 例如 `MarkInitialTestDiscoveryReady(...)` / `IsInitialTestDiscoveryReady()`，把 `DiscoverTests()` + readiness 迁移收口到一处；再把 hot reload test 准备与 post-compile 增量发现统一改读新 helper，不再硬编码 `bCompletedAssetScan`
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `C-11` 指出 `AssetManager` 缺席 fallback 只调用 `DiscoverTests()`，却永远不把 `bCompletedAssetScan` 置真
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-53` 要求把 tests-ready barrier 从 `AssetManager` 扫描事实中拆开，避免 fallback 分支永久降级
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-40` 要求把 `ScriptsReady`、`AssetScanReady`、`TestsReady` 变成显式里程碑，而不是继续依赖单个内部布尔
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2201-L2217、L2481-L2489、L4143-L4153 — `AssetManager` 正常路径会在回调里同时发现测试并置真 `bCompletedAssetScan`，fallback 分支只调用 `DiscoverTests()`；后续 hot reload test 准备和 post-compile 增量发现仍直接把 `bCompletedAssetScan` 当成硬门槛
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
- [ ] **P4.4** 📦 Git 提交：`[Runtime/Core] Fix: mark initial test discovery ready without AssetManager`
- [ ] **P4.4-T** 单元测试：补齐 `AssetManager` 缺席时的 initial discovery barrier 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptInitialTestDiscoveryReadyTests.cpp`
  - 测试场景：
    - 正常路径：`AssetManager` 可用时，initial discovery 完成后 tests-ready barrier 为真，hot reload test 准备与 post-compile 再发现都能继续运行
    - 边界条件：`AssetManager == nullptr` 的 fallback 分支也会进入统一 ready 状态；之后即使 `AssetManager` 再初始化，也不会把 barrier 回退成未完成或重复污染测试发现
    - 错误路径：fallback 分支不再出现“日志提示已发现测试，但 `bCompletedAssetScan`/ready helper 仍为假，导致后续热更测试与增量发现永久被阻断”的静默降级
  - 测试命名：`Angelscript.TestModule.Core.Startup.AssetManagerFallbackCompletesInitialDiscoveryBarrier`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.4-T** 📦 Git 提交：`[Test/Core] Test: cover asset-manager fallback discovery readiness`

- [ ] **P4.5** 把 namespace policy 与 runtime settings 收口成 engine-owned snapshot，禁止多 owner 生命周期内继续混读全局单例
  - 当前 RuntimeCore 仍保留两套相互打架的 settings 语义：一方面 `PreInitialize_GameThread()` 会把 namespace policy 写进进程级静态变量，另一方面 `CollectDisabledBindNames()` 又继续从 `ConfigSettings` 或默认对象读取 live settings，`AngelscriptLoopDetectionCallback()` 甚至每次都直接读 `UAngelscriptSettings::Get()`；这意味着 owner A 初始化完成后，owner B 或 editor settings 的后续变化仍可能回写 A 的 namespace / bind / timeout 语义
  - 本项不是一次性完成所有 compile-profile 架构重写，而是先把当前最危险的全局状态拉成 `FAngelscriptEffectiveSettings` 快照：owner 初始化时一次性固化 namespace、automatic imports、disabled binds、loop timeout 等运行期读取项；clone 只读 source snapshot；确需热更新的字段必须通过显式 `ApplyUpdatedSettings(...)` 流程进入，而不是继续让调用点偷读默认单例
  - 先把 namespace policy 从静态态移到 owner snapshot，再把 `CollectDisabledBindNames()`、loop detection 和其余 Core 级 settings 读取点全部改成只读 snapshot；后续再让 `Helper_FunctionSignature`、`Bind_BlueprintType.cpp`、`Bind_Primitives.cpp` 等外围消费者改接同一份 owner settings
  - 来源：
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-49` 指出 Blueprint library namespace 规则仍是进程级静态状态；`Issue-50` 指出 runtime settings 同时存在 snapshot 与 live-singleton 两套读取语义，会在多 owner 生命周期内发生配置漂移
    - [C] `RuntimeCore_TestGaps.md` — `NewTest-36` 已把 namespace-strip、debugger blacklist 与默认 timeout 定位为应该被正式锁定的 settings 合同
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-13` 要求把 compile/runtime profile 显式化，禁止预处理和运行期继续依赖 ambient engine 与默认 settings 单例
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L145-L147 — namespace policy 仍是进程级静态变量；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1291-L1308、L1928-L1941、L5568-L5580 — owner 初始化时直接覆盖静态 namespace 列表，`CollectDisabledBindNames()` 继续从 live settings 读取，loop detection 每次直接读取 `UAngelscriptSettings::Get().EditorMaximumScriptExecutionTime`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Primitives.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/Helper_FunctionSignature.h`
- [ ] **P4.5** 📦 Git 提交：`[Runtime/Core] Refactor: snapshot effective settings per engine owner`
- [ ] **P4.5-T** 单元测试：补齐 owner-level settings snapshot 与显式 reload 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineEffectiveSettingsTests.cpp`
  - 测试场景：
    - 正常路径：owner A 初始化后记录 namespace policy、disabled binds 与 loop timeout；后续常规运行中这些结果保持稳定，不随外部默认对象变化而漂移
    - 边界条件：clone 继承 source 的 effective settings，而新建 owner B 可以拥有不同 snapshot；两台 owner 在同进程内并存时 namespace/bind/timeout 语义彼此隔离
    - 错误路径：修改 `UAngelscriptSettings` 默认对象后，owner A 不会被动改变；只有显式 `ApplyUpdatedSettings(...)` 或等价 reload 入口触发后，允许热更新的字段才发生可预期迁移
  - 测试命名：`Angelscript.TestModule.Core.Settings.OwnerSnapshotPreventsCrossEngineDrift`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.5-T** 📦 Git 提交：`[Test/Core] Test: cover engine effective settings snapshots`

## 单元测试总览补充（三）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P4.1` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCloneContextDebugProjectionTests.cpp` | clone scope 的 current-context flags、debug capability 与 global line-callback 聚合 | P1 |
| `P4.2` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCloneMetadataProjectionTests.cpp` | 无 `TrackNamedModule()` 的 clone 模块/类/枚举/委托投影与 source 同步 | P1 |
| `P4.3` | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemAdoptedEngineLifetimeTests.cpp` | adopt owner 判活、tick / detach 分流、scope/clone 失效后的安全清空 | P1 |
| `P4.4` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptInitialTestDiscoveryReadyTests.cpp` | `AssetManager` 可用与缺席两条路径下的 tests-ready barrier 与增量发现 | P1 |
| `P4.5` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineEffectiveSettingsTests.cpp` | namespace policy、disabled binds、loop timeout 的 owner snapshot 与显式 reload | P1 |

## 验收标准补充（三）

5. clone 在 current-context、debug/line-callback 与 metadata 三层都不再依赖空白 wrapper 默认值：不调用 `TrackNamedModule()` 也能解析 source 元数据，且 clone/source 交替成为 current engine 时不会互相关掉 line callback 或读错 world/editor/import 状态。
6. subsystem adopt 外部 engine 后会持有可判活的生命周期令牌；外部 owner reset、scope 退出或 clone/source 失效后，subsystem 后续 `Tick()` / `Deinitialize()` 只会 detach/清空，不会再解引用或关闭失效 engine。
7. `AssetManager` 缺席时，initial test discovery 仍能进入统一 ready barrier；owner 初始化后的 namespace policy、disabled binds 与 loop timeout 具备稳定 snapshot，不会被其他 owner 或默认 settings 的后续变化被动改写。

## 风险与注意事项补充（三）

### 风险

1. clone 的 current-context/debug/metadata 一旦改为 shared projection，会暴露一批历史测试或工具对“clone 是轻量空 wrapper”的隐式依赖；执行时必须先补 focused contract tests，再逐步移除 `TrackNamedModule()` 与 wrapper-local 假设。
2. settings snapshot 收口后，部分 editor 调整将不再即时影响已经存活的 owner；如果某些工作流确实需要 live reload，必须补显式 `ApplyUpdatedSettings(...)` 路径，而不是偷偷保留单例回读。
3. subsystem adopt 改成 token-guarded 后，少量以前“恰好能工作”的临时 scope adopt 场景会被拒绝或自动失效；这是正确收紧，但需要同步更新测试夹具与 helper 预期。

### 已知行为变化

5. clone 将开始稳定暴露 source 的 debug/context/metadata 真相；任何依赖 clone 默认 `false/null/empty map` 的旧断言都需要改成显式区分“shared projection”与“wrapper overlay”。
6. `AssetManager` 缺席时不再意味着 tests-ready barrier 永久关闭；headless 或 fallback 初始化路径下，hot reload test 准备与 post-compile test discovery 可能更早进入可运行状态。
7. owner 初始化完成后，namespace policy、disabled binds、loop timeout 等 runtime settings 将默认保持快照语义；修改 `UAngelscriptSettings` 默认对象不会立刻改写既有 owner，除非显式触发 settings reload。

---

## 本轮补充（2026-04-09 第四轮）

本轮只追加当前文档尚未单列的 5 个切口：初始化失败事务、production 级 AngelScript GC 驱动、`FAngelscriptGameThreadScopeWorldContext` 安全收口、worker-thread current-engine resolver 边界、`StaticNames` 跨 epoch 复位。它们分别补上 `P3.1` 尚未展开的失败回滚、`P2.3` 未覆盖的脚本 GC 与静态名表残留、`P1.4` 尚未封死的 scope copy/thread 逃逸口，以及 `P2.1/P2.2` 之后仍然存在的 off-thread resolver 旁路。

### Phase 5: 补失败路径、执行态与跨 epoch 的剩余全局状态

- [ ] **P5.1** 把 `RuntimeModule` / `Subsystem` / fatal compile-exit 收口成可回滚的初始化事务
  - `P3.1` 已经把 ready milestone 的静态语义写清，但当前真正危险的还是失败路径：module 和 subsystem 都会在 owner 真正可用前先提交 `bInitializeAngelscriptCalled`、`bInitialized`、`PrimaryEngine`、`ActiveTickOwners` 或 `ContextStack` 栈项，fatal compile-exit 还会在请求退出后继续广播 `OnInitialCompileFinished`。这会把“初始化尝试开始了”误报成“runtime 已 ready”
  - 本项不重复 `P1.1` 的 override owner 和 `P3.1` 的 milestone 归并，而是把它们补成同一套 failure-aware contract：`FAngelscriptEngine::Initialize()` 必须返回显式结果；module/subsystem 只在 `Succeeded` 时提交 owner、ready 和 tick-owner 状态；`ExitRequested/Failed` 路径必须完整回滚并允许重试
  - 先引入统一的 `EAngelscriptInitializeResult`（或等价结果对象），再把 `InitializeAngelscript()`、`UAngelscriptGameInstanceSubsystem::Initialize()` 与 `InitialCompile()`/`PostInitialize_GameThread()`/`InitializeOwnedSharedState()` 串成“prepare -> initialize -> commit/rollback”事务；其中 `OnInitialCompileFinished` 与 shared-state 发布都必须挂到 commit 之后，而不是继续作为 `RequestExit()` 之后的尾随副作用
  - 来源：
    - [B] `RuntimeCore_Plan.md` — `Issue-54` / `Issue-55` / `Issue-56` 指出 module 与 subsystem 都会在 bootstrap 前提交初始化状态，fatal compile-exit 还会继续发布 ready 广播
    - [C] `RuntimeCore_TestGaps.md` — `NewTest-22` 说明 runtime module 初始化入口、testing override 与 reset 仍无直接合同测试；`NewTest-10` 要求 `Subsystem/` 升级为真实 `GameInstanceSubsystem` 生命周期场景，而不是 injected helper 假状态
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-50` 指出当前唯一启动完成信号是无参 `OnInitialCompileFinished`，且广播早于 shared state ready，扩展方无法区分“编译结束”“runtime ready”“请求退出”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` L138-L166、L174-L182 — `InitializeAngelscript()` 先置 `bInitializeAngelscriptCalled` 再尝试 override/owned owner，reset 也只复位 `OwnedPrimaryEngine`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L12-L30 — subsystem 一进入就置 `bInitialized=true`，owned 路径先 `Push()` 再 `Initialize()` 并递增 `ActiveTickOwners`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1605-L1608、L1653-L1655、L2100-L2105、L2201-L2221 — engine 在 `RequestExit*()` 后仍会广播 `OnInitialCompileFinished`、注册 tests-ready 相关回调并把 `bIsInitialCompileFinished` 置真
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptTestCommandlet.cpp`
- [ ] **P5.1** 📦 Git 提交：`[Runtime/Core] Fix: make initialization transactional and rollback on failure`
- [ ] **P5.1-T** 单元测试：补齐初始化失败回滚、ready 发布与重试合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptInitializationTransactionTests.cpp`
  - 测试场景：
    - 正常路径：`RuntimeModule` 与真实 `GameInstanceSubsystem` 成功初始化后，只广播一次 ready，`ContextStack`、`PrimaryEngine`、`ActiveTickOwners` 与 shared-state 全部进入一致终态
    - 边界条件：`InitializeOverrideForTesting()` 返回 `nullptr` 或受控 `ExitRequested` 结果时，module/subsystem 不提交 owner 状态，修正输入后可再次成功初始化
    - 错误路径：初始编译 fatal 退出时，不广播 `OnInitialCompileFinished`、不创建 shared-state、`ContextStack` 不残留半初始化 engine，`ActiveTickOwners` 也不会被污染
  - 测试命名：`Angelscript.TestModule.Core.Initialize.TransactionPublishesReadyOnlyOnSuccess`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.1-T** 📦 Git 提交：`[Test/Core] Test: cover initialization transactions and rollback`

- [ ] **P5.2** 给 runtime owner 增加 production 级 AngelScript GC 驱动，而不是只在内部测试里手动调用
  - 当前运行时在 testing/full 初始化两条路径都显式关闭 `asEP_AUTO_GARBAGE_COLLECT`，但 `Tick()` 与 `Shutdown()` 没有任何生产级 `GarbageCollect(...)` 调度点；这意味着 RuntimeCore 真实运行时只会靠测试或外部手工 helper 才能回收 AngelScript 循环图。`P2.3` 解决的是 context/package epoch 清理，不会替代脚本对象图的循环回收
  - 本项目标不是把整套 GC 策略过度复杂化，而是先建立最小、可验证的 owner-level contract：运行期在可控 cadence 下驱动 detect/collect，owner teardown 再做一次 final flush；同时把现有 GC 场景测试从“UE 弱指针最终失效”升级成“script 引用先保活、清空后才回收”的双阶段合同
  - 实施上优先把 GC 调度收口成 engine 内 helper，例如 `MaybeRunRuntimeGarbageCollection()` / `FlushRuntimeGarbageCollectionOnShutdown()`；再让 `Tick()` 与 `Shutdown()` 使用同一套节流/策略读取入口，避免未来继续只有测试 helper 才能触发 AngelScript GC
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `C-02` 指出 runtime 明确关闭 `asEP_AUTO_GARBAGE_COLLECT`，但运行时代码里没有任何 production 级 `GarbageCollect(...)` 驱动
    - [C] `RuntimeCore_TestGaps.md` — `Issue-03` / `Issue-67` 说明现有 `GCScenarioTests` 只验证 UE 对象最终失效，无法证明 GC 真正负责回收；`NewTest-39` 明确要求补“脚本字段先保活、清空后再回收”的双阶段合同
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L889、L1395 — testing/full 初始化都执行 `SetEngineProperty(asEP_AUTO_GARBAGE_COLLECT, 0)`；同文件 L2794-L2841、L1132-L1252 — `Tick()` 与 `Shutdown()` 只处理 hot reload、debug、context/package/engine 释放，当前 `Core/` 下没有 runtime-side `GarbageCollect(` 调用
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/UnitTest.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptGCScenarioTests.cpp`
- [ ] **P5.2** 📦 Git 提交：`[Runtime/Core] Fix: drive Angelscript GC during runtime and final teardown`
- [ ] **P5.2-T** 单元测试：补齐 runtime 手动 GC cadence 与双阶段回收合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptRuntimeManualGCTests.cpp`
  - 测试场景：
    - 正常路径：脚本字段持有对象时，runtime GC 周期执行后对象仍被保活；清空字段并再次驱动 GC 后对象被回收
    - 边界条件：world teardown 与 runtime GC 分离验证，确保离开 scope 后、真正调用 runtime GC 之前仍能观察到未回收状态，避免把 teardown 误当 GC 成功
    - 错误路径：owner shutdown 时即使存在待回收脚本循环，也会执行 final flush 并安全结束；不会因为 `Engine == nullptr` 或 teardown 顺序而跳过脚本 GC
  - 测试命名：`Angelscript.TestModule.GC.Runtime.ManualCollectorRunsDuringTickAndTeardown`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.2-T** 📦 Git 提交：`[Test/GC] Test: cover runtime manual GC cadence and teardown flush`

- [ ] **P5.3** 把 `FAngelscriptGameThreadScopeWorldContext` 收口成 engine-aware、move-only、受线程边界保护的 helper
  - `P1.4` 已把 ambient restore 与 tick scope 的主线补齐，但当前还留着一个明显逃逸口：`FAngelscriptGameThreadScopeWorldContext` 只缓存一份 `PreviousWorldContext`，析构直接 `AssignWorldContext(...)`，没有 owner engine 身份，也没有禁掉 copy/assignment；同时它还能绕过现有 `USES_WORLDCONTEXT` 的 `BlueprintThreadSafe` 防护
  - 本项不重写 world-context 总体 owner 语义，而是先把这个公开 helper 从“带全局副作用的普通 value type”收成受控 scope：记录 owner engine / previous engine-local world-context，禁止 copy/assignment，只允许 move 或直接禁止脚本长期持有；并在构造入口同步执行 game-thread / thread-safe guard，避免脚本通过它绕开 world-context 安全边界
  - 具体实现上，禁止再让析构依赖“析构当下的 `TryGetCurrentEngine()` 猜目标”；恢复 ambient 与恢复 owner engine-local world-context 必须拆成两步，且只在 owner 仍然有效时发生。绑定层则应同步撤掉 copy trait 或直接把它改成不可复制的专用 scope 暴露
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-30` 指出该 helper 可绕过 `BlueprintThreadSafe` world-context 保护；`A-31` 指出它作为可复制 value type 暴露给脚本，但析构会回滚全局 world-context
    - [B] `RuntimeCore_Plan.md` — `Issue-23` 已明确要求把 `FAngelscriptGameThreadScopeWorldContext` 改成 owner-aware、non-copyable scope，并拆分 ambient/engine-local 恢复入口
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L710-L724 — 该类型当前只存 `PreviousWorldContext`，且头文件没有删除 copy ctor / assignment；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L746-L753、L1778-L1782 — `AssignWorldContext()` 会同时改写 current engine 的 `WorldContextObject` 与 ambient，全局恢复目标取决于析构当下的 current engine，而 `FAngelscriptGameThreadContext` 也仍无条件调用这条写入口
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptGameThreadScopeWorldContext.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`
- [ ] **P5.3** 📦 Git 提交：`[Runtime/Core] Fix: harden world-context scope ownership and copy semantics`
- [ ] **P5.3-T** 单元测试：补齐 world-context scope 的 owner、copy 与 thread-safe 边界合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptWorldContextScopeSafetyTests.cpp`
  - 测试场景：
    - 正常路径：单层与跨 engine 嵌套 scope 退出后，ambient world 与 owner engine-local world-context 都恢复到进入前状态，不会误写到当前其它 engine
    - 边界条件：`FAngelscriptGameThreadScopeWorldContext` 只能 move/局部使用，copy/assignment 在编译期或绑定期被拒绝，脚本侧也不会再把它当普通 value type 复制
    - 错误路径：在 `BlueprintThreadSafe` 或非 game-thread 执行态中尝试构造该 scope 时，会被显式拒绝且不改写 ambient/current-engine world-context
  - 测试命名：`Angelscript.TestModule.Core.WorldContextScope.IsMoveOnlyAndThreadSafeGuarded`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.3-T** 📦 Git 提交：`[Test/Core] Test: cover world-context scope safety and ownership`

- [ ] **P5.4** 拆分 worker-thread current-engine resolver，禁止后台路径继续偷走 subsystem/world fallback
  - `P2.1` 与 `P2.2` 已经开始收 watcher owner 与 threaded init phase，但 steady-state 里还有一条更底层的旁路：`TryGetCurrentEngine()` 只要 `ContextStack` 为空，就会回退到 `UAngelscriptGameInstanceSubsystem::GetCurrent()`；而 hot reload worker、pooled context 借还和其它后台路径都还在直接调用它
  - 本项先把 resolver 边界做硬：game-thread resolver 允许走 subsystem/world，worker-thread resolver 只允许消费显式 owner 或 `ContextStack`；凡是后台路径想继续工作，都必须带 owner 或接受空值，而不是继续在后台线程触碰 `GEngine`、`UWorld` 和 `UGameInstanceSubsystem`
  - 先补 `TryGetCurrentEngine_ThreadSafe()`（或等价 gate）和 `GetCurrent()` 的 game-thread-only 保护，再审查 `FAngelscriptHotReloadThread::Run()`、`FAngelscriptPooledContextBase::Init()` / 析构等调用点，去掉对 ambient world 的隐式依赖。这样后续 thread-affinity 扩展才不需要继续围绕“当前恰好在哪个 world”猜 owner
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-15` 指出 `TryGetCurrentEngine()` 的 subsystem fallback 没有线程边界保护，却已被 hot reload worker、context 池借还和 `CheckGameThreadExecution()` 这些后台路径直接使用
    - [B] `RuntimeCore_Plan.md` — `Issue-33` 给出了拆分 `TryGetCurrentEngine()` / `TryGetCurrentEngine_ThreadSafe()`、把 `GetCurrent()` 限定为 game-thread-only 的具体方案
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-50` 指出当前启动/扩展模型缺少 first-class thread-affinity contract，side-effectful 扩展只能猜测哪些动作必须回到 game thread
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L718-L733 — `TryGetCurrentEngine()` 当前在 `ContextStack` 为空时直接走 `UAngelscriptGameInstanceSubsystem::GetCurrent()`；同文件 L1674-L1690、L1815-L1818、L1831-L1837、L1890-L1897 — hot reload worker、pooled context 初始化与归还都仍通过 `TryGetCurrentEngine()` 在后台线程解析 current engine
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`
- [ ] **P5.4** 📦 Git 提交：`[Runtime/Core] Fix: split worker-safe current-engine resolution from subsystem fallback`
- [ ] **P5.4-T** 单元测试：补齐 worker-thread current-engine 解析与 subsystem fallback 边界合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCurrentEngineThreadBoundaryTests.cpp`
  - 测试场景：
    - 正常路径：game thread 上仍可通过有效 world/subsystem 解析到 attached engine，行为与现有入口保持一致
    - 边界条件：worker thread 携带显式 `FAngelscriptEngineScope` 或 owner 参数时，thread-safe resolver 仍能解析到对应 engine，不依赖 ambient world
    - 错误路径：worker thread 在 `ContextStack` 为空但 ambient world 可解析到 subsystem 的情况下，只返回 `nullptr` 或显式 owner，不会再访问 `UAngelscriptGameInstanceSubsystem::GetCurrent()`
  - 测试命名：`Angelscript.TestModule.Core.Resolver.WorkerThreadDoesNotUseSubsystemFallback`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.4-T** 📦 Git 提交：`[Test/Core] Test: cover worker-safe current-engine resolution`

- [ ] **P5.5** 把 `StaticNames` / `StaticNamesByIndex` 从进程级残留收回到 owner epoch 结束点
  - `P2.3` 已经开始清理 package 与 context epoch，但脚本命名面还残留一块更隐蔽的跨 epoch 状态：`StaticNames` 与 `StaticNamesByIndex` 仍是进程级静态表，初始化只 `Reserve()`，而 owner teardown 从不复位。这会让第二轮 full engine 或 precompiled replay 在同进程里继续吃到上一轮脚本名索引
  - 本项目标不是一次性重构整个 precompiled artifact 体系，而是先把 static-name 注册表明确定义成 owner epoch 资源：最后一个 owner/clone 释放时统一 reset；启动新 owner 前可显式验证“当前若无活动 owner，则 static-name 表必须为空”
  - 先补统一 `ResetStaticNameRegistry()` 与 owner-final-release 挂点，再让 full-engine 启动前与 precompiled-data 恢复入口复用同一套防御性前置校验。这样后续要做 mixed-mode artifact replay 或 module residency 时，至少不会继续被上一轮进程残留污染 baseline
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `B-03` / `B-10` 指出 `StaticNames` / `StaticNamesByIndex` 是进程级追加式全局表，但 RuntimeCore 没有任何 teardown 复位路径，且与 precompiled-data 的空表前提直接冲突
    - [B] `RuntimeCore_Plan.md` — `Issue-38` 已给出把 static-name 注册表建模成 owner epoch 资源、在最后一个参与者释放时统一清空的执行方案
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-10` / `Arch-SL-17` 都指出 source/artifact/live state 仍缠在同一生命周期里，precompiled artifact replay 需要更清晰的 epoch 边界和 cache identity
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L69-L70 — `StaticNames` 与 `StaticNamesByIndex` 当前是进程级静态表；同文件 L1560-L1562 — 初始化未命中 precompiled-data 时只 `Reserve(7000)`；同文件 L1236-L1250 — `Shutdown()` 只清实例级模块、root path、package 与 shared-state 字段，没有任何 static-name registry reset
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`
- [ ] **P5.5** 📦 Git 提交：`[Runtime/Core] Fix: reset static-name registry at owner epoch boundaries`
- [ ] **P5.5-T** 单元测试：补齐 `StaticNames` 跨 epoch 复位与 precompiled replay 前置条件合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStaticNameEpochTests.cpp`
  - 测试场景：
    - 正常路径：第一轮 full engine 通过预处理生成 static names，完整 shutdown 后第二轮 owner 启动从空 registry 基线开始，不继承旧编号
    - 边界条件：存在 clone/shared-state 延迟释放时，static-name registry 只在最后一个参与者退出后才清空，不会过早破坏仍存活视图
    - 错误路径：先跑 source-preprocess epoch、再跑依赖 precompiled replay 的 epoch 时，不再因为旧 static-name 残留而触发错误前置条件或读到错位索引
  - 测试命名：`Angelscript.TestModule.Core.StaticNames.RegistryResetsPerOwnerEpoch`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.5-T** 📦 Git 提交：`[Test/Core] Test: cover static-name epoch reset and precompiled replay guard`

## 单元测试总览补充（四）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.1` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptInitializationTransactionTests.cpp` | module/subsystem 成功初始化、override/exit requested 回滚、fatal compile-exit 不发布 ready | P1 |
| `P5.2` | `Plugins/Angelscript/Source/AngelscriptTest/GC/AngelscriptRuntimeManualGCTests.cpp` | runtime GC cadence、脚本字段双阶段保活/回收、shutdown final flush | P1 |
| `P5.3` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptWorldContextScopeSafetyTests.cpp` | scope owner restore、copy/assignment 禁用、`BlueprintThreadSafe`/非 GT 拒绝 | P1 |
| `P5.4` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptCurrentEngineThreadBoundaryTests.cpp` | game-thread resolver 保持现状、worker-thread 只消费显式 owner、off-thread 不再走 subsystem fallback | P1 |
| `P5.5` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStaticNameEpochTests.cpp` | static-name registry 复位、clone 延迟释放边界、precompiled replay 前置条件 | P1 |

## 验收标准补充（四）

8. `RuntimeModule`、`Subsystem` 与 fatal compile-exit 不再把“开始初始化”误报成“runtime ready”：失败/退出路径不会残留 `ContextStack`、`ActiveTickOwners`、shared-state 或 ready 广播，且修正输入后允许重试。
9. runtime 在关闭 `asEP_AUTO_GARBAGE_COLLECT` 的前提下，仍具备 production 级 AngelScript GC 调度；脚本对象图可以在“字段保活 -> 清空字段 -> 手动 GC”三步下稳定表现，不再只靠测试私有 helper 才能回收。
10. world-context scope、worker-thread resolver 与 static-name registry 都具备明确 owner 边界：scope 不可复制且不再绕开 thread-safe 保护；后台线程不会再偷走 subsystem/world fallback；full owner epoch 结束后 static-name 注册表回到干净基线。

## 风险与注意事项补充（四）

### 风险

1. 初始化事务化会把一批当前“失败后仍半初始化可用”的隐式调用点暴露出来；如果某些工具仍把 `OnInitialCompileFinished` 当成“一切都好了”的信号，切换到结果枚举后需要同步修正。
2. production 级 AngelScript GC 一旦 cadence 过于激进，可能放大 editor/dev 环境下的 tick 成本；第一阶段应优先做可观测、可调节的节流，而不是在每帧做 full cycle。
3. 收紧 worker-thread resolver 与 world-context scope 后，少数此前依赖 ambient world 或可复制 scope 的路径会从“偶然工作”变成显式失败；这是正确收口，但需要同步更新 helper、测试夹具和错误文案。
4. `StaticNames` 的 reset 时机必须晚于最后一个 shared participant 退出；如果把它挂错到普通 wrapper shutdown，会直接破坏 clone/shared-state 仍在使用的 name-index 视图。

### 已知行为变化

8. 初始化失败或 `RequestExit` 场景下，`OnInitialCompileFinished`、`bIsInitialCompileFinished` 与 shared-state 不再提前发布；任何监听方都必须区分“compile 完成”“runtime ready”“exit requested”三种结果。
9. `FAngelscriptGameThreadScopeWorldContext` 将不再是可自由复制的普通 value helper；脚本或 C++ 侧若仍试图复制/跨线程持有它，会收到更早、更明确的拒绝。
10. worker-thread 上 `TryGetCurrentEngine()` 的旧 ambient/subsystem fallback 将被切断；后台路径若未显式携带 owner engine，今后会得到 `nullptr` 或显式失败，而不是继续静默解析到某个 world。

---

## 深化 (2026-04-09 01:21:19)

本轮不重述 `P1.4` 的泛化 tick 合同或 `P4.3` 的 adopted-engine 判活；只把当前文件尚未拆成独立执行项的两条 subsystem 稳态缺口补齐：`稳定重发现` 与 `唯一驱动 claim`。

### Phase 5 深化：把 adopted subsystem 从“能判活”推进到“可重发现、可仲裁”

- [ ] **P5.6** 给 adopted subsystem 建立稳定宿主解析链，切断 `GetCurrent()` 对 ambient world 的单点依赖
  - `P4.3` 已经准备把 adopted `PrimaryEngine` 从裸指针升级成带 token 的引用，但当前 resolver 仍只有 `ambient world -> GameInstance -> subsystem` 这一条回路：一旦临时 `FAngelscriptEngineScope` 退出并清掉 ambient，subsystem 明明还活着、`PrimaryEngine` 也还在，却会被 `GetCurrent()` / `TryGetCurrentEngine()` 整体“找丢”
  - 本项只补 subsystem 稳态 discoverability，不重做 `Arch-SL-12` 的完整 runtime registry：先把 `UAngelscriptGameInstanceSubsystem` 的稳定宿主信息显式沉淀为 `UGameInstance` / `UWorld` 级查询入口，再让 `TryGetCurrentEngine()` 的 fallback 先解析宿主、再取 attached engine，而不是继续把 ambient world 当成 adopted owner 的唯一真相
  - 实施顺序上先加 `TryGetForGameInstance(...)` / `TryGetForWorld(...)` 这类显式 API 和最小宿主缓存，再把 `GetCurrent()` 降成薄封装；最后清理 `TryGetCurrentEngine()`、`ShouldUseEditorScriptsForCurrentContext()`、`IsScriptDevelopmentModeForCurrentContext()` 这类 current-context 入口对 ambient 丢失的脆弱依赖
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `B-19` 指出 adopted subsystem 离开临时 scope 后会因 ambient world 被清空而无法被 `GetCurrent()` / `TryGetCurrentEngine()` 重新发现
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-58` 要求把 subsystem 重新解析改为 `TryGetForGameInstance` / `TryResolveEngineFromSubsystemHost()` 这类稳定宿主边界
    - [C] `RuntimeCore_TestGaps.md` — `NewTest-10` 指出 `Subsystem/` 目录缺少真实 `GameInstanceSubsystem` adopt/own/tick 正向场景，当前最核心的 lifecycle 合同没有正向保护
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-12` 明确建议引入 `engine registry + runtime handle`，让 owner、tick 和上下文解析围绕 `OwningGameInstance` 运转，而不是围绕 ambient 全局状态运转
    - [E] `CrossComparison.md` `[D1]` — 参考实现把 runtime identity 绑定到 `GameInstance` / env-group / runtime-owned registry，而不是继续依赖 ambient state + heuristic current-engine 解析
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L17-L23、L94-L113 — adopted 分支当前只缓存 `PrimaryEngine`，`GetCurrent()` 仍只靠 `GetAmbientWorldContext()` 反查 `World -> GameInstance -> Subsystem`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L491-L507、L718-L733 — `FAngelscriptEngineScope::Reset()` 退出时会同步 ambient 到弹栈后的 current engine，而 `TryGetCurrentEngine()` 在栈空时仍只会回退到 `GetCurrent()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h` L36-L43 — subsystem 私有状态里目前只有 `OwnedEngine`、`PrimaryEngine`、`bOwnsPrimaryEngine` 与 `bInitialized`，没有任何稳定宿主字段或显式查询入口
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
- [ ] **P5.6** 📦 Git 提交：`[Runtime/Subsystem] Fix: resolve adopted subsystem by stable host instead of ambient world`
- [ ] **P5.6-T** 单元测试：补齐 adopted subsystem 离开临时 scope 后的稳定重发现合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemStableResolverTests.cpp`
  - 测试场景：
    - 正常路径：在临时 `FAngelscriptEngineScope` 内初始化 adopted subsystem，scope 退出并清空 ambient world 后，只要 `UGameInstance` / `UWorld` 仍存活，`GetCurrent()` 与 `TryGetCurrentEngine()` 仍能解析回同一 subsystem/engine
    - 边界条件：同进程同时存在 adopted subsystem 与 owned subsystem 时，显式 `GameInstance` / `World` 查询只返回对应宿主的 subsystem，不会被另一个 runtime 或外层 scope 串线
    - 错误路径：宿主 `GameInstance` 已销毁、lifetime token 失效或 owner 已 detach 时，稳定查询会返回 `nullptr` 并清空旧解析结果，不会回退到陈旧 `PrimaryEngine`
  - 测试命名：`Angelscript.TestModule.Subsystem.Resolver.AdoptedSubsystemUsesStableHostLookup`
  - 隔离方式：`FAngelscriptEngineScope（配合真实 UGameInstance/UWorld fixture）`
- [ ] **P5.6-T** 📦 Git 提交：`[Test/Subsystem] Test: cover adopted subsystem stable host resolution`

- [ ] **P5.7** 把 subsystem tick ownership 收口成 engine-unique claim，并把非 owner adopted subsystem 降为 observer
  - `P1.4` 已经把“不要用进程级 `ActiveTickOwners` 压制无关 engine”写成总方向，但当前文件还没把“同一台 engine 只能有一个 subsystem 负责驱动”拆成独立合同：只要多个 subsystem 都 adopt 到同一 `PrimaryEngine`，现代码就会让它们全部进入 `Tick()`，形成重复推进；而 runtime module fallback 又只认全局 `HasAnyTickOwner()`，既不能表达“谁拥有这台 engine”，也不能表达“谁只是观察者”
  - 本项的目标不是再造复杂 scheduler，而是先建立最小唯一性：subsystem adopt engine 后必须显式 `TryClaimTickOwnership(Engine, Subsystem)`；claim 失败的 adopted subsystem 只能保留只读 `PrimaryEngine` 引用和 resolver 能力，不能继续 `Tick()` 该 engine，也不能继续用全局 owner 计数压制其它 engine 的 fallback lane
  - 实施上先引入 per-engine claim registry 与 `bDrivesPrimaryEngineTick` 之类的显式状态，再让 `IsAllowedToTick()`、`Tick()`、`Deinitialize()` 与 runtime module fallback 统一改读新 claim；最后补 claim 释放和 observer 转 owner 的迁移路径，避免 owner 退出后剩余 subsystem 永远失去驱动资格
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `B-14` / `D-11` 指出 `ActiveTickOwners` 仍是进程级总闸，现有自动化也没有任何 multi-engine / multi-owner tick 仲裁回归
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-57` 明确要求引入 per-engine tick ownership registry，把 adopted subsystem 区分成 driver 与 observer 两种状态
    - [C] `RuntimeCore_TestGaps.md` — `NewTest-10` 只要求补 adopt/own/tick 正向场景，说明当前 `Subsystem/` 目录还没有“多个 subsystem / 多个 owner / 同一 engine 仲裁”这类正式合同测试
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-12` 明确要求把 `TickOwnerCount` 绑定到 runtime handle，而不是继续用一个进程级布尔门控 editor/runtime fallback tick
    - [E] `CrossComparison.md` `[D1]` — 参考实现把 owner 与执行 env 绑定到 runtime-specific identity，再由 selector/locator 决定谁驱动哪一个 runtime，而不是让多个 subsystem 共享一个全局 tick owner 语义
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L8-L29、L66-L86、L116-L118 — subsystem 仍以静态 `ActiveTickOwners` 记全局 owner 数，`PrimaryEngine != nullptr` 就递增，`Tick()` 也只要 `PrimaryEngine->ShouldTick()` 就直接推进；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` L186-L197 — fallback tick 仍只看 `HasAnyTickOwner()` 全局结果； `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h` L40-L43 — 当前没有任何 per-engine claim / driver-or-observer 状态
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
- [ ] **P5.7** 📦 Git 提交：`[Runtime/Subsystem] Fix: claim unique tick ownership per engine`
- [ ] **P5.7-T** 单元测试：补齐同一 engine 的唯一 tick driver 与 observer 回退合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemTickOwnershipTests.cpp`
  - 测试场景：
    - 正常路径：两个 subsystem adopt 同一台 engine 时，只有成功 claim 的那个 subsystem 会实际调用 `Tick()`；另一个 subsystem 保留 resolver/读状态能力，但不会重复推进同一台 engine
    - 边界条件：subsystem A 驱动 engine A、runtime module fallback 驱动 engine B 时，A 的 claim 只抑制 A，不会全局熄火 B；owner 释放后 observer 可以重新 claim 并继续驱动
    - 错误路径：claim owner 宿主销毁、lifetime token 失效或 `Deinitialize()` 提前退出时，registry 会清掉脏 claim，`HasAnyTickOwner()` / engine-specific query 不会永久卡死，也不会留下 double-tick
  - 测试命名：`Angelscript.TestModule.Subsystem.TickOwnership.OnlyOneSubsystemDrivesAnEngine`
  - 隔离方式：`FAngelscriptEngineScope（配合真实 UGameInstance/UWorld fixture）`
- [ ] **P5.7-T** 📦 Git 提交：`[Test/Subsystem] Test: cover unique subsystem tick ownership`

## 单元测试总览补充（五）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.6` | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemStableResolverTests.cpp` | adopted subsystem 离开临时 scope 后的稳定宿主解析、multi-GameInstance 边界、宿主失效后的安全清空 | P1 |
| `P5.7` | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemTickOwnershipTests.cpp` | 同一 engine 的唯一 tick driver、observer 回退、无关 engine fallback 不再被全局抑制 | P1 |

## 验收标准补充（五）

11. adopted subsystem 在临时 `FAngelscriptEngineScope` 退出并清空 ambient world 后，只要其 `UGameInstance` / `UWorld` 宿主仍有效，`GetCurrent()` 与 `TryGetCurrentEngine()` 仍能稳定解析回同一 runtime；宿主或 lifetime token 失效后，查询会明确返回空而不是回落到陈旧 `PrimaryEngine`。
12. 任意时刻同一台 `FAngelscriptEngine` 至多只有一个 subsystem tick driver；其它 adopted subsystem 只能作为 observer 存活，不再重复推进同一 engine，也不再用全局 owner 语义压制无关 engine 的 fallback tick。

## 风险与注意事项补充（五）

### 风险

5. 一旦 `GetCurrent()` 增加稳定宿主查询，少数当前依赖“ambient world 一旦清空就视为 subsystem 不存在”的 helper 可能暴露出错误假设；测试夹具和 editor/runtime 诊断需要同步改成区分“ambient 不可用”和“宿主不可用”。
6. 引入 driver/observer 分流后，部分历史调用点可能仍把 `PrimaryEngine != nullptr` 误当成“这个 subsystem 一定会 tick”；如果不同时收紧 `IsAllowedToTick()` / `Tick()` / fallback 判断，容易出现 half-migrated 状态。

### 已知行为变化

11. `UAngelscriptGameInstanceSubsystem::GetCurrent()` / `FAngelscriptEngine::TryGetCurrentEngine()` 在 ambient world 已清空时，仍可能通过稳定宿主解析回 adopted subsystem；这类命中今后不再是“偶然残留”，而是正式 lifecycle 合同的一部分。
12. 多个 subsystem 可以同时 adopt 同一台 engine，但只有 claim owner 会驱动 `Tick()`；其余 subsystem 变成 observer，直到 owner 释放 claim 才允许重新接管。

---

## 深化 (2026-04-09 01:34:25)

本轮不重写 `P2.2`、`P5.3` 与 `P5.5` 的既有主线，只补三个当前源码仍存在、但现有 Plan 尚未单列的 RuntimeCore 缺口：启动恢复/初始化窗口的未 ready 暴露、shared-state 正式实现仍被测试宏包住、以及 `BlueprintThreadSafe` 仍可经 world-context helper 旁路改写上下文。

### Phase 2 深化：把启动恢复与 threaded initialize 的未 ready 窗口收口成正式合同

- [ ] **P2.4** 收口 startup recovery / threaded initialize 的“未 ready 不可见”窗口
  - 当前 `Initialize()` 一开始就通过 `FAngelscriptEngineScope` 把 wrapper 推进公共 `ContextStack`，随后 threaded 路径在 worker thread 慢慢完成 bootstrap，而 game thread 在等待窗口里继续 `ProcessThreadUntilIdle(...)`；同一窗口内，`Tick()` 还明确承认 `GEngine` 可能为 `nullptr`，却仍会走 `HasGameWorld()` 解引用。这两条分支本质上暴露的是同一个问题：RuntimeCore 没有把“已经开始初始化”和“已经可以被 public resolver / tick 逻辑消费”区分开
  - 本项不重做 `Arch-SL-40/50` 的完整 startup phase 体系，也不重复 `P2.2` 的 `primaryContext` handoff 细节；目标只收口当前最危险的窗口合同：初始化中的 engine 在 commit 前不得进入公共 resolver，startup recovery 在 `GEngine == nullptr` 时也必须有稳定的 no-crash 行为
  - 实施上先把 `Initialize()` 开头的 public `FAngelscriptEngineScope` 改成 private bootstrap guard，或等价地延后 `ContextStack::Push()` 到 `PostInitialize_GameThread()` / ready commit 之后；再让 `IsInitialized()`、`TryGetCurrentEngine()`、`GetPackage()` 之类 public 入口显式区分 `Initializing` 与 `Ready`；最后把 `HasGameWorld()` 变成空安全 helper，并为 `GEngine == nullptr` 的恢复窗口补最小可轮询分支，而不是继续把 world-dependent 热重载决策跑进空指针解引用
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-22` 指出 `Tick()` 已知 `GEngine == nullptr` 仍继续调用 `HasGameWorld()`；`B-17` / `D-13` 指出 production `Tick()` 与初始化边界依赖 ambient current-engine，而现有自动化又用测试 scope 绕过了这条真实合同
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-17` 明确要求把 startup recovery 的 `GEngine == nullptr` 提升为统一 gate；`Issue-60` 明确要求 threaded `Initialize()` 在 ready 前不得把半初始化 engine 暴露给公共 resolver
    - [C] `RuntimeCore_TestGaps.md` — `Issue-22` 指出 `Startup.Full` / `CreateForTestingFallbackFull` 只写性能 artifact，没有断言 full startup 语义或 creation-mode/owner 合同
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-40` / `Arch-SL-50` 都要求把 startup thread affinity 与 ready milestone 提升为显式 contract，而不是继续让外部在“初始化差不多结束”窗口里猜测 runtime 是否已经可见
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L437-L455、L819-L856、L676-L680、L796-L806、L2781-L2829 — `Initialize()` 当前一进入就 `FAngelscriptEngineScope ScopedInitializingEngine(*this)`，threaded 等待窗口仍持续 pump game-thread 任务；`IsInitialized()` 只看 `ContextStack::Peek()` / subsystem 是否存在；`GetPackage()` 仍直接从 `TryGetCurrentEngine()` 取当前 wrapper；`Tick()` 明确注释 `GEngine` 可能为空，但 `HasGameWorld()` 仍无判空直接遍历 `GEngine->GetWorldContexts()`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
- [ ] **P2.4** 📦 Git 提交：`[Runtime/Core] Fix: keep initializing engines invisible until ready commit`
- [ ] **P2.4-T** 单元测试：补齐 startup recovery / threaded initialize 的 ready-window 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupReadyWindowTests.cpp`
  - 测试场景：
    - 正常路径：threaded initialize 完成 commit 后，`TryGetCurrentEngine()`、`IsInitialized()` 与 `GetPackage()` 才对外表现为 ready，且随后 `Tick()` 在有效 `GEngine` 下仍按既有 world-based reload 语义工作
    - 边界条件：startup recovery 期间 `GEngine == nullptr` 时执行 `Tick()` 不会触发 `HasGameWorld()` 崩溃，并仍能保留最小的 retry / scan 能力
    - 错误路径：在 threaded initialize 等待窗口内注入 game-thread 回调，ready 提交前这些回调不会把 initializing wrapper 解析成 current engine，也不会误报 `IsInitialized() == true`
  - 测试命名：`Angelscript.TestModule.Core.Startup.InitializingEngineStaysInvisibleUntilReady`
  - 隔离方式：`FAngelscriptEngineScope（仅在 ready 后校验阶段启用；初始化窗口通过 dedicated startup seam 观测）`
- [ ] **P2.4-T** 📦 Git 提交：`[Test/Core] Test: cover startup recovery and ready-window visibility`

### Phase 5 深化：把 shared-state 与 world-context 的防护面补成正式 runtime contract

- [ ] **P5.8** 把 shared-state 正式访问器移出 `WITH_DEV_AUTOMATION_TESTS`，收口 runtime / test 双轨实现
  - `FAngelscriptEngine.h` 当前把 `GetTypeDatabase()`、`GetBindState()`、`GetToStringList()`、`GetBindDatabase()`、`EnsureSharedStateCreated()` 作为无条件公开的运行期入口暴露，但 `AngelscriptEngine.cpp` 仍把它们的定义整体包在 `#if WITH_DEV_AUTOMATION_TESTS` 中；与此同时，bind/type/bind-database 路径却在无宏保护的正式代码里继续直接依赖这些访问器。现在之所以没有在所有目标上立刻炸掉，更多是构建配置偶然遮住了问题，而不是合同成立
  - 本项不是要顺手重做整套 bind/type registry 架构，而是先把当前最基础的 shared-state 访问合同修正成“所有 target 同一套正式实现”；测试宏只保留真正的 test-only 计数器和 override seam，不能再承载运行期能力本身
  - 实施上先把五个正式访问器移出 `WITH_DEV_AUTOMATION_TESTS`，并把 `GetActiveParticipantsForTesting()`、`GetLocalPooledContextCountForTesting()`、`SetUseEditorScriptsForTesting()` 这类纯测试 seam 留在宏内；再统一 `InitializeForTesting()`、`Initialize_AnyThread()` 与 bind/type database helper 对 shared-state 的入口，最后审查 `LegacyTypeDatabase` / `LegacyBindState` / `LegacyBindDatabase` 的 fallback，只允许它们在“当前没有 engine”时充当显式 legacy bucket，不再掩盖缺失的 shared-state 初始化
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `C-07` 指出 shared-state 访问器与 `EnsureSharedStateCreated()` 只在 `WITH_DEV_AUTOMATION_TESTS` 下定义，但正式运行时代码仍无宏保护地依赖它们
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-42` 明确要求把 shared-state 正式访问器从测试宏中拆出，并恢复 RuntimeCore 在非测试目标上的正式编译合同
    - [E] `GapAnalysis.md` — `Environment-owned registry graph` / `[19]` 强调参考实现会在 runtime owner 初始化时一次建齐 registry graph，测试与工具直接消费 owner graph，而不是继续依赖 test-only seam 或猜“哪些 registry 已 replay”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L564-L569 — shared-state 访问器当前是无条件声明；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L944-L999 — 这五个正式定义仍被整体包在 `#if WITH_DEV_AUTOMATION_TESTS` 下；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` L23-L33、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` L35-L45、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp` L14-L24 — bind/type/bind-database 运行期 helper 仍无条件调用这些访问器，并在失败时静默退回 legacy singleton
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`、`Documents/Guides/Build.md`
- [ ] **P5.8** 📦 Git 提交：`[Runtime/Core] Fix: remove test-only macro boundary from shared-state accessors`
- [ ] **P5.8-T** 单元测试：补齐 shared-state 访问器与 owner graph 的正式合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptSharedStateAccessContractTests.cpp`
  - 测试场景：
    - 正常路径：full owner 初始化后，`GetTypeDatabase()`、`GetBindState()`、`GetBindDatabase()` 与 bind/type helper 看到的是同一份已建立的 shared-state
    - 边界条件：clone 或第二参与者接入同一 shared-state 时，访问器继续返回 owner graph，而不是掉回测试专用 shadow 实现或空指针
    - 错误路径：当前没有 engine 时，bind/type helper 只回退到显式 legacy bucket，不会隐式创建新的 shared-state，也不会因为 accessors 缺实现而改变行为
  - 测试命名：`Angelscript.TestModule.Core.SharedState.RuntimeAccessorsMatchOwnerGraph`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.8-T** 📦 Git 提交：`[Test/Core] Test: cover runtime shared-state accessor contracts`

- [ ] **P5.9** 封住 `BlueprintThreadSafe` 对 `FAngelscriptGameThreadScopeWorldContext` 的 world-context 旁路
  - 当前 RuntimeCore 已经对带 `USES_WORLDCONTEXT` trait 的系统函数建立了 `BlueprintThreadSafe` 保护，但 `FAngelscriptGameThreadScopeWorldContext` 仍可以绕过这层 contract：它的构造/析构只会无条件 `AssignWorldContext(...)`，而该 helper 又被绑定层以普通 `ValueClass` 形式完整公开给脚本。这意味着脚本无需触发现有 trait 检查，就能直接改写 ambient world 和 current engine 的 `WorldContextObject`
  - 本项与 `P5.3` 的区别是：`P5.3` 关注 C++ scope 自身的 owner/copy 语义，本项只补“这个 helper 是否允许在公开脚本面被任意、跨线程、`BlueprintThreadSafe` 地调用”这条防护面；目标是把 world-context 改写入口重新收回到明确的 game-thread / non-thread-safe contract 下
  - 实施上先在 `FAngelscriptGameThreadScopeWorldContext` 构造/析构，以及更底层的 `AssignWorldContext()` / `SetAmbientWorldContext()` 入口增加统一的 game-thread / thread-safe 检查；再决定绑定层是完全隐藏该类型，还是改成带显式约束的受控 API；最后把现有调用点中依赖该 helper 的路径补成显式 non-thread-safe 调度，而不是继续靠 value class 直接写全局上下文
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-30` 指出 `FAngelscriptGameThreadScopeWorldContext` 的公开绑定绕过了 `BlueprintThreadSafe` 的 `WorldContext` 崩溃保护
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-61` 明确要求把该 helper 定义为 game-thread-only / non-thread-safe helper，并阻止脚本通过公开绑定直接改写 ambient/current world context
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-40` 强调 startup / runtime 扩展需要显式 thread-affinity contract，side-effectful helper 不应再靠“当前大概在 game thread”这种隐式前提存活
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L710-L724、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L746-L753 — `FAngelscriptGameThreadScopeWorldContext` 构造/析构当前仍无条件 `AssignWorldContext(...)`，而 `AssignWorldContext()` 会同时写 current engine 的 `WorldContextObject` 与 ambient world；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptGameThreadScopeWorldContext.cpp` L4-L17 — 该 helper 仍以普通 `ValueClass` 公开绑定给脚本
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptGameThreadScopeWorldContext.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`
- [ ] **P5.9** 📦 Git 提交：`[Runtime/Core] Fix: block world-context scope bypass in BlueprintThreadSafe paths`
- [ ] **P5.9-T** 单元测试：补齐 `BlueprintThreadSafe` / game-thread world-context 写入口的拒绝合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptWorldContextThreadGuardTests.cpp`
  - 测试场景：
    - 正常路径：在 game thread 且明确 non-thread-safe 的调用路径里，world-context scope 仍能成功进入并对称恢复 ambient/current world context
    - 边界条件：已有 ambient world 或 `FAngelscriptEngineScope` 叠加时，允许的 game-thread 路径仍保持正确恢复顺序，不会因为新增 guard 破坏合法嵌套
    - 错误路径：在 `BlueprintThreadSafe` 函数或 off-game-thread 路径里尝试构造该 helper 时，调用会稳定失败/抛错，且 `GAmbientWorldContext` 与 `TryGetCurrentWorldContextObject()` 保持进入前状态
  - 测试命名：`Angelscript.TestModule.Core.WorldContext.BlueprintThreadSafeCannotMutateAmbientWorld`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.9-T** 📦 Git 提交：`[Test/Core] Test: cover world-context thread guard and bypass rejection`

## 单元测试总览补充（六）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P2.4` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupReadyWindowTests.cpp` | startup recovery 下 `GEngine == nullptr` 不崩溃、threaded initialize 等待窗口不提前暴露 ready、ready 后再发布 package/current-engine | P1 |
| `P5.8` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptSharedStateAccessContractTests.cpp` | shared-state 正式访问器与 bind/type helper 共用同一 owner graph、clone/多参与者边界、无 engine 时的 legacy fallback | P1 |
| `P5.9` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptWorldContextThreadGuardTests.cpp` | game-thread 正常 world-context scope、嵌套恢复边界、`BlueprintThreadSafe`/off-thread 拒绝写上下文 | P1 |

## 验收标准补充（六）

13. startup recovery 与 threaded initialize 的等待窗口不再把半初始化 engine 暴露给公共 resolver：`GEngine == nullptr` 时 `Tick()` 不崩溃，ready commit 前 `TryGetCurrentEngine()` / `IsInitialized()` 不会把 initializing wrapper 误报为正式 runtime。
14. shared-state 访问器在测试目标与非测试目标上共用同一套正式实现；bind/type/bind-database 路径不再依赖 `WITH_DEV_AUTOMATION_TESTS` 才能链接/运行，legacy fallback 也只剩显式的“无当前 engine”语义。
15. `BlueprintThreadSafe` 与 off-game-thread 路径不能再通过 `FAngelscriptGameThreadScopeWorldContext` 直接改写 ambient/current world context；合法 game-thread 路径保持可用且恢复顺序稳定。

## 风险与注意事项补充（六）

### 风险

7. 一旦把 initializing engine 从公共 resolver 中隐藏出来，少数初始化内部 helper 若仍偷偷依赖 `TryGetCurrentEngine()`，会从“偶然可用”变成显式失败；执行时必须把这类路径同步改成显式 owner / private bootstrap helper。
8. 把 shared-state 正式访问器移出测试宏后，`LegacyTypeDatabase` / `LegacyBindState` / `LegacyBindDatabase` 的历史兜底路径会更容易暴露出“当前没有 engine 却还在默默工作”的旧假设；如果不顺手收紧 fallback 语义，会留下 half-migrated 状态。
9. 若仓库内已有脚本直接实例化 `FAngelscriptGameThreadScopeWorldContext`，收紧或隐藏公开绑定会立刻暴露兼容性回归；需要先用日志/断言确认调用面，再决定是完全隐藏还是转成受限 API。

### 已知行为变化

13. threaded initialize 开始后到 ready commit 之前，`TryGetCurrentEngine()` / `IsInitialized()` / 依赖 current-engine 的 package 查询会更严格地返回“未 ready”；这是刻意收紧，不再允许半初始化窗口冒充正式 runtime。
14. 非测试目标会开始编译并使用与测试目标相同的 shared-state 正式访问器；真正的 test-only helper 仍保留在 `WITH_DEV_AUTOMATION_TESTS` 下，但 production code 不能再借测试宏获得能力实现。
15. `FAngelscriptGameThreadScopeWorldContext` 在脚本面的可见性和可调用条件会被收紧：`BlueprintThreadSafe` 或 off-game-thread 路径下要么无法构造，要么会明确抛错，而不是继续静默改写全局 world context。

---

## 深化 (2026-04-09 01:44:24)

本轮不重复 `P2.2`、`P5.2` 与 `P5.9` 的既有主线，只补 3 个当前源码仍存在、但现有 Plan 尚未单列的 RuntimeCore 缺口：threaded init 里残留的 `GameThread` 亲和工作、`StaticJIT/precompiled` 的跨 epoch 静态状态、以及 optimized dispatch 仍可借无保护的 `GameThreadContext` 绕过线程边界。

### Phase 5 深化：把剩余的 thread-affinity 与 epoch-state 漏口收口成正式合同

- [ ] **P5.10** 把 threaded 初始化里的 `UObject`/反射绑定与全局 delegate 接线前移到 `GameThread` commit phase
  - `P2.2` 已经准备收口 `LoadModule()`、`GameThreadTLD` 与 `primaryContext` handoff，但当前 threaded init 仍把一批明确带 `GameThread` 亲和性的工作塞在 `Initialize_AnyThread()`：创建 `/Script/Angelscript*` 包、执行 `BindScriptTypes()`、以及向 `FCoreDelegates` 注册长期回调。只修 `LoadModule()` 还不够，worker 线程仍会继续触碰 `UObject` 图和非 thread-safe delegate 容器
  - 本项不重复 `Plan_AngelscriptEngineBindAndFileWatchValidation.md` 的 startup 观测矩阵，而是先把 RuntimeCore 的线程合同做实：worker 只负责纯 VM/bootstrap 计算，任何 `UObject`、反射枚举、package materialization、coverage/debug hook 注册都必须进入显式 `GameThread` commit phase；这样后续 startup bind、late-init 与 recovery 测试才有稳定地基
  - 实施上先拆出 `Initialize_AnyThread()` 里的 `CreatePackages_GameThread()`、`BindScriptTypes_GameThread()`、`FinalizeRuntimeHooks_GameThread()` 或等价 helper，再让 threaded path 在 worker 产出纯 bootstrap 结果后回到 `GameThread` 执行 commit；同时把当前 startup/performance smoke 升级成能断言 creation mode、bind invocation 与 hook 注册时序的合同测试，而不是只看 artifact 或总量
  - 来源：
    - [B] `RuntimeCore_Plan.md` — `Issue-45` 明确指出 threaded init 仍把 `NewObject<UPackage>`、`BindScriptTypes()` 与 `OnPostEngineInit`/`OnGetOnScreenMessages` 接线留在 worker thread
    - [C] `RuntimeCore_TestGaps.md` — `Issue-22` / `Issue-29` 说明当前 `Startup.Full`、`CreateForTestingFallbackFull` 与 `StartupBindRegistrySmoke` 只看 artifact、数量或同一 registry 的自洽，无法证明 full startup 语义和 startup bind surface 在正确线程上提交
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-40` 要求把 startup 扩展改成显式 `phase + thread affinity` 协议，而不是继续依赖无参 `OnInitialCompileFinished` 让调用方自己猜哪些副作用必须回到 `GameThread`
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L819-L848 — `Initialize()` 当前仍把整段 `Initialize_AnyThread()` 投到 `AnyHiPriThread`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1382-L1495 — worker 路径仍直接创建 `/Script/Angelscript*` 包、覆盖 `bGeneratePrecompiledData` 并调用 `BindScriptTypes()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1628-L1640 — 同一路径末尾仍直接注册 `OnPostEngineInit` / `OnGetOnScreenMessages` 并更新 line-callback 状态
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEnginePerformanceTests.cpp`
- [ ] **P5.10** 📦 Git 提交：`[Runtime/Core] Fix: move GT-affine startup work out of threaded init`
- [ ] **P5.10-T** 单元测试：补齐 threaded initialize 的 `GameThread` commit 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptThreadedInitializeAffinityTests.cpp`
  - 测试场景：
    - 正常路径：强制 threaded initialize 时，startup bind pass、package 创建与 runtime hook 注册都只在 `GameThread` commit 后变为可见，且 bind invocation / executed bind 列表与 full startup 语义一致
    - 边界条件：关闭 threaded initialize 后，single-thread 启动仍保持现有 bind/hook 行为，不因为 phase 拆分改变 creation mode、bind 次数或 ready 顺序
    - 错误路径：threaded init 在 commit 前失败或请求退出时，不会留下 worker 创建的 package、已注册 delegate 或半完成 bind surface；重试后仍可成功启动
  - 测试命名：`Angelscript.TestModule.Core.Startup.ThreadedInitializeCommitsGTOnlyWorkOnGameThread`
  - 隔离方式：`FAngelscriptEngineScope（仅在 commit 后验证阶段启用；初始化窗口通过 dedicated startup seam 观测）`
- [ ] **P5.10-T** 📦 Git 提交：`[Test/Core] Test: cover threaded initialize game-thread commit affinity`

- [ ] **P5.11** 把 `StaticJIT` / precompiled native-form 状态收回 owner epoch，禁止跨 full-engine 周期残留
  - `Plan_TestEngineIsolation.md` 已经把“静态标志 engine-local 化”列为长期主线，但当前 RuntimeCore 还有一条更具体、且现有 Plan 尚未单列的缺口：`bGeneratePrecompiledData` 仍是进程级静态开关，而 `StaticJIT` 的 `GScriptNativeForms` 仍按进程级静态表累积 native form；owner shutdown 并不会把这两者与 full-engine epoch 对齐
  - 本项不重做整个 artifact/build gate 体系，而是先补最小可执行合同：precompiled/native-form 相关静态状态必须显式归属某个 owner/shared-state epoch，最后一个参与者退出时统一释放；下一轮 full engine 启动前必须回到干净基线，不能继续吃上一轮残留的 `asIScriptFunction* -> native form` 映射
  - 实施上先把 `bGeneratePrecompiledData` 从进程级静态态收进 owner/shared-state，再给 `StaticJIT` 增加 `ReleaseNativeFormsForScriptEngine(...)` 或等价 registry reset 入口；最后把现有 full-destroy/recreate 测试升级成“两轮都显式 teardown 并检查 registry 基线”的形式，避免第二个 epoch 的泄漏继续躲在测试返回后的析构里
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-20` 指出 `bGeneratePrecompiledData` 与 `GScriptNativeForms` 仍是跨 engine epoch 的进程级 StaticJIT 状态，shutdown 没有任何复位或释放路径
    - [B] `RuntimeCore_Plan.md` — `Issue-43` 已把 native-form registry 改成 owner-aware 资源、在 owner-final-release 时对称释放列为明确修复方案
    - [C] `RuntimeCore_TestGaps.md` — `Issue-65` / `Issue-66` 说明现有 full-destroy/recreate 只验证第一轮 cleanup，没有任何断言保护第二个 epoch 的 teardown，因此跨 epoch 静态残留可以长期漏检
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L76-L80 — `bGeneratePrecompiledData` 仍与其它 runtime policy 一起作为进程级静态状态存在；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1425-L1447 — 初始化仍直接用当前 owner 的 `RuntimeConfig` 覆盖静态 `bGeneratePrecompiledData` 并据此创建 `PrecompiledData`/`StaticJIT`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1132-L1252 — `Shutdown()` 仍只清实例字段，没有任何 static precompiled/native-form reset；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` L27-L28、L112-L116、L156-L160、L199-L203 — `GScriptNativeForms` 仍是进程级静态 `TMap`，`BindNative*()` 在 `bGeneratePrecompiledData` 为真时继续 `new` native form 并常驻其中
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`
- [ ] **P5.11** 📦 Git 提交：`[Runtime/Core] Fix: reset StaticJIT native-form state per owner epoch`
- [ ] **P5.11-T** 单元测试：补齐 `StaticJIT/precompiled` 跨 epoch 清理合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStaticJITEpochTests.cpp`
  - 测试场景：
    - 正常路径：第一轮 full owner 开启 precompiled/native-form 生成后显式 shutdown，第二轮 owner 启动从空 registry 基线重新生成，不会读到上一轮条目
    - 边界条件：存在 clone/shared participant 时，native-form registry 只在最后一个参与者退出后才清空，不会过早破坏仍存活的 shared VM
    - 错误路径：precompiled/export-only 流程在中途失败或请求退出后，不会把半注册的 native form 或静态 `bGeneratePrecompiledData` 残留到下一轮 owner epoch
  - 测试命名：`Angelscript.TestModule.Core.StaticJIT.NativeFormsResetPerOwnerEpoch`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.11-T** 📦 Git 提交：`[Test/Core] Test: cover StaticJIT native-form epoch reset`

- [ ] **P5.12** 把 `FAngelscriptGameThreadContext` 收口成强制 thread-affinity 的公共执行原语，封住 `OptimizedCall_*` 快路径旁路
  - `P5.3` / `P5.9` 已经覆盖 world-context scope helper 的 owner 与公开绑定问题，但当前还有一条未单列的执行层缺口：`FAngelscriptGameThreadContext` 本身并不验证 `GameThread` 亲和性，optimized dispatch 仍可直接构造它或直接跳 raw JIT，从而绕过慢路径上已有的 `CheckGameThreadExecution()` 合同
  - 本项不重做整套调度体系，而是先把“进入 Blueprint override 执行前必须经过统一线程边界检查”落成公共原语：`FAngelscriptGameThreadContext` 自身要拒绝非法线程，optimized/raw JIT/解释执行三条分支必须共用同一条 guard，而不是继续让慢路径安全、快路径失守
  - 实施上先在 `Core/` 侧补 `PrepareBlueprintOverrideExecution(...)` 或等价 helper，把 `CanUseGameThreadData()` 从“无人调用的静态提示”提升为强制 gate；再回到 `ASClass.cpp` 把所有 `OptimizedCall_*` 和 raw JIT 分支接入同一 helper，最后补一组真正命中 optimized dispatch 的线程合同回归，避免测试继续只走 `RuntimeCallEvent`
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-33` 指出 `OptimizedCall_*` 会跳过 `CheckGameThreadExecution()`；同文 `D-23` 指出当前自动化只走慢路径，optimized dispatch 完全没有线程合同回归
    - [B] `RuntimeCore_Plan.md` — `Issue-62` 已明确要求把线程边界检查前移到所有 override dispatch 公共入口，并在 `FAngelscriptGameThreadContext` 上补防御性 `GameThread` 校验
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-40` 强调启动与运行期扩展都需要显式 thread-affinity contract，不能继续依赖“调用方自己知道哪些副作用必须在 `GameThread`”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L242-L249 — `CanUseGameThreadData()` 当前只是独立 helper，没有被 `FAngelscriptGameThreadContext` 强制消费；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L758-L768、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1778-L1782 — `FAngelscriptGameThreadContext` 构造/析构仍无条件绑定 `GameThreadTLD` 并改写 world context；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L53-L77、L1561-L1738 — 慢路径仍显式 `CheckGameThreadExecution()`，但 `OptimizedCall_*` 仍可直接走 raw JIT 或直接构造 `FAngelscriptGameThreadContext`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp`
- [ ] **P5.12** 📦 Git 提交：`[Runtime/Core] Fix: unify thread guard for optimized Blueprint override dispatch`
- [ ] **P5.12-T** 单元测试：补齐 optimized dispatch 的线程边界合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptOptimizedDispatchThreadGuardTests.cpp`
  - 测试场景：
    - 正常路径：在 `GameThread` 上命中 optimized dispatch 时，fast path 与慢路径都能正常执行 Blueprint override，且 world-context 进出对称
    - 边界条件：关闭 raw JIT 或强制走解释执行时，optimized 与非 optimized 入口共享同一条线程 guard，不会因为 dispatch 形态不同而改变合同
    - 错误路径：off-game-thread 或 default-statement 场景命中 `OptimizedCall_*` 时，不会执行 raw JIT、不会构造可写 world-context 的 `FAngelscriptGameThreadContext`，并给出与慢路径一致的拒绝结果
  - 测试命名：`Angelscript.TestModule.Core.ThreadAffinity.OptimizedDispatchMatchesSlowPathGuard`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.12-T** 📦 Git 提交：`[Test/Core] Test: cover optimized dispatch thread guard parity`

## 单元测试总览补充（七）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.10` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptThreadedInitializeAffinityTests.cpp` | threaded init 下的 `GameThread` commit、single-thread 兼容、失败前不泄漏 package/delegate | P1 |
| `P5.11` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStaticJITEpochTests.cpp` | native-form registry 跨 epoch reset、clone/shared participant 边界、export-only 失败回滚 | P1 |
| `P5.12` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptOptimizedDispatchThreadGuardTests.cpp` | optimized/slow path 线程 guard 一致性、raw JIT 边界、off-thread 拒绝 | P0 |

## 验收标准补充（七）

16. threaded initialize 不再在 worker thread 上执行 `UObject`/反射/package/delegate 副作用；这些工作只会在显式 `GameThread` commit phase 中提交，失败/退出前不会对外泄漏半完成 startup 状态。
17. `StaticJIT/precompiled` 相关静态状态具备 owner epoch 边界：full owner 退出后 native-form registry 与 `bGeneratePrecompiledData` 不再跨轮残留，下一轮 full engine 从干净基线启动。
18. optimized Blueprint override dispatch 与慢路径共享同一套线程边界合同；off-thread/default-statement 场景不再因命中 `OptimizedCall_*` 或 raw JIT 而绕过 `GameThread` 保护。

## 风险与注意事项补充（七）

### 风险

10. 把 `BindScriptTypes()`、package 创建和 delegate 接线从 worker 移回 `GameThread` commit 后，startup 时长与现有 telemetry 可能出现明显变化；执行时必须同步修正性能基线和 startup observation helper，避免把时序收口误判成纯性能回退。
11. `StaticJIT` registry 若只做“全量清空”而不感知 shared participant / clone 仍在使用的 VM，会把当前的跨 epoch 泄漏修成“仍在服役的 native form 被提前删掉”；清理挂点必须绑定最后一个 owner/shared participant 释放点。
12. 一旦 optimized/raw JIT 入口开始统一执行线程 guard，仓库内少数此前“偶然在后台线程也能跑”的 Blueprint override 调用会暴露成显式失败；需要同步升级错误文案、测试夹具和调用面，避免把安全收口误判成新回归。

### 已知行为变化

16. threaded startup 完成前，`BindScriptTypes()`、package materialization 和 runtime hook 注册不再在 worker thread 上“悄悄发生”；相关观测点会更明确地集中到 `GameThread` commit 之后。
17. 启用 precompiled/static JIT 输出的 owner 在退出后会主动清理 native-form 静态状态；下一轮 owner 不再继承上一轮 `StaticJIT` registry 的残留条目或静态开关。
18. 命中 `OptimizedCall_*` 的 Blueprint override 今后与慢路径一样遵守线程边界；off-thread/default-statement 调用会更早、更一致地失败，而不是继续因为 fast path 形态不同出现静默旁路。

---

## 深化 (2026-04-09 01:54:53)

本轮不重复 `P4.1` 的 callback owner/line-callback 聚合、`P5.10` 的 threaded startup 亲和性、以及 `P5.12` 的 optimized dispatch 线程保护；只补 3 个当前源码仍存在、但现有 Plan 尚未单列的 RuntimeCore 调试执行态缺口：执行位置 helper 的 sentinel 合同、JIT `this`/`DebugBreak()` 的 execution-state resolver、以及 debug-value 缓存的 teardown 释放。

### Phase 5 深化：把调试执行态与定位 helper 收成 owner-aware 生命周期合同

- [ ] **P5.13** 收口 `GetAngelscriptExecutionFileAndLine()` 的 sentinel 合同，修复空 `debugCallStack` 崩溃与脏输出传播
  - 当前 RuntimeCore 的执行位置 helper 仍同时违反内存安全和输出稳定性：JIT 分支把 `DebugStack == nullptr` 的条件写反了，空栈时反而直接解引用；普通 bytecode 分支在无 active context 或空 callstack 时又直接返回，导致调用方继续拿未初始化输出向下传播
  - 本项不重做整套 debugger metadata，只先把“没有可用执行位置时返回什么”固定成正式合同：函数入口统一产出 `"" / -1` sentinel，JIT 与 bytecode 两条分支都只在拿到有效 frame 时覆盖它；任何调用点都不再需要猜测 helper 是否一定成功
  - 先修 `Core/AngelscriptEngine.cpp` 里的 JIT/bytecode 两条返回路径，再审查会立刻消费结果的调用面，尤其是资产 redirector metadata 写入；后续若要把位置 helper 扩展到更多调试入口，也必须复用同一 sentinel 语义，而不是继续让“无上下文”走裸返回
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-01` / `A-03` 指出 `GetAngelscriptExecutionFileAndLine()` 在空 `JIT debugCallStack` 分支会确定性崩溃，且在无活动上下文时会保留脏输出
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-59` 已明确要求把 file/line helper 改成“入口先初始化 sentinel，再分别修正 JIT 与普通 context 分支”的正式合同
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L5665-L5697 — JIT 分支当前在 `DebugStack == nullptr` 时仍直接读取 `DebugStack->Filename/LineNumber`，普通分支在 `Context == nullptr` 或 `GetCallstackSize() == 0` 时直接 `return`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` L659-L666 — 调用方仍把该 helper 的返回直接写入 `ScriptAssetFilename/ScriptAssetLineNumber` metadata
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`
- [ ] **P5.13** 📦 Git 提交：`[Runtime/Debug] Fix: stabilize execution file-line sentinel contract`
- [ ] **P5.13-T** 单元测试：补齐执行位置 helper 在 JIT 空栈与无上下文下的 sentinel 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptExecutionLocationContractTests.cpp`
  - 测试场景：
    - 正常路径：存在有效执行 frame 时，`GetAngelscriptExecutionFileAndLine()` 返回当前脚本文件与行号，调用方 metadata 仍能拿到真实位置
    - 边界条件：`activeExecution` 存在但 `debugCallStack == nullptr` 时，helper 返回 `"" / -1` 而不是崩溃，且重复调用结果稳定
    - 错误路径：无 active context 或 callstack 为空时，helper 仍返回 `"" / -1`，`Bind_UObject` 相关 metadata 写入不会残留旧值或未初始化值
  - 测试命名：`Angelscript.TestModule.Core.Debug.ExecutionFileAndLineReturnsStableSentinel`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.13-T** 📦 Git 提交：`[Test/Core] Test: cover execution file-line sentinel contract`

- [ ] **P5.14** 统一 JIT `this` 查询与 `DebugBreak()` 的 execution-state resolver，禁止继续停留在 `activeContext` 语义
  - RuntimeCore 已经把 JIT 执行态的 callstack 与 position helper 迁到了 `tld->activeExecution->debugCallStack`，但 `GetAngelscriptExecutionThisObject()` 与 `TryBreakpointAngelscriptDebugging()` 仍只读 `asGetActiveContext()`；结果是同一 JIT 帧里 file/line/callstack 看得到，`this` 和 pause 却系统性丢失
  - 本项不重做 `DebugServer V2` 协议，也不重复 `P4.1` 的 clone 调试 owner 投影；只先把“当前执行态”收成一条公共 resolver，让 bytecode/JIT 两条路径都能返回同一份 `current frame / current this / current pause target`
  - 实施上先在 `Core/AngelscriptEngine.cpp` 提炼统一 execution-state helper，再让 `GetAngelscriptExecutionThisObject()`、`TryBreakpointAngelscriptDebugging()` 和 `Bind_Debugging.cpp` 的 fallback 逻辑全部改接这条 helper；只有当 resolver 明确判断“当前不在任何脚本执行态”时，`DebugBreak()` 才允许继续走原生 `UE_DEBUG_BREAK()`
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `C-10` / `C-12` 指出 JIT 执行态的 `DebugBreak()` 无法进入调试服务器暂停路径，`GetAngelscriptExecutionThisObject()` 也没有接入 `activeExecution->debugCallStack`；同文 `D-21` 说明这些接口当前没有直接自动化保护
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-64` 已明确要求新增统一 execution-state resolver，让 `this` 查询、callstack/position 与 pause 共享同一套 JIT/bytecode 读取合同
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L5101-L5125、L5632-L5647 — callstack 与 execution position 已优先读取 `tld->activeExecution->debugCallStack`；同文件 L5700-L5745 — `GetAngelscriptExecutionThisObject()` 和 `TryBreakpointAngelscriptDebugging()` 仍只依赖 `asGetActiveContext()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp` L14-L28 — `ASDebugBreak()` 在 pause 失败时仍直接落到 `UE_DEBUG_BREAK()`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`
- [ ] **P5.14** 📦 Git 提交：`[Runtime/Debug] Fix: resolve JIT execution state for this-object and debug break`
- [ ] **P5.14-T** 单元测试：补齐 JIT 执行态的 `this` 查询与 `DebugBreak()` pause 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptJITExecutionStateTests.cpp`
  - 测试场景：
    - 正常路径：JIT 执行帧内 `GetAngelscriptCallstack()`、`GetAngelscriptExecutionPosition()`、`GetAngelscriptExecutionThisObject()` 返回同一帧事实，`DebugBreak()` 能命中调试服务器 pause
    - 边界条件：同一 helper 在 bytecode 与 JIT 两条路径都返回一致语义，不会因为执行后端不同而出现 `this`/pause 分叉
    - 错误路径：当前线程不在任何脚本执行态时，resolver 明确返回空，`DebugBreak()` 才允许退化为原生 break，而不是把 JIT 帧误判成“无上下文”
  - 测试命名：`Angelscript.TestModule.Core.Debug.JITExecutionStateUsesUnifiedResolver`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.14-T** 📦 Git 提交：`[Test/Core] Test: cover JIT execution-state resolver`

- [ ] **P5.15** 为 `DebugFramePtr` / `DebugPrototypePtr` 建立显式 teardown 路径，阻止 debug-value 缓存跨 module/engine epoch 残留
  - 当前 debug-value 路径在 `GetStack()` / `GetDebugPrototype()` 中向 `asCContext::DebugFramePtr` 与 `asCScriptFunction::DebugPrototypePtr` 挂入堆对象，但正式释放路径只会 `Context->Release()`、`DiscardModule()` 或 `Shutdown()`，没有任何对称 delete/reset；调试值一旦被走到，就会把旧 epoch 的缓存永久挂在 context / function 上
  - 本项不重做整个 debug-value 展示模型，只先收口生命周期：context 归还/销毁前要清理 `DebugFramePtr`，module discard 或 owner 最终释放前要清理 `DebugPrototypePtr`，确保 `WITH_AS_DEBUGVALUES` 不再把调试缓存变成跨轮次泄漏源
  - 优先在 RuntimeCore 自己的 release helper 中建立集中清理入口，例如 `DestroyDebugStack(...)` / `DestroyDebugPrototype(...)`；若第三方 fork 可接受，再评估把这类清理钩子下沉到 `asCContext` / `asCScriptFunction` 析构阶段，但第一步先把仓内 owner 生命周期补成闭环
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-16` 指出 `GetStack()` 与 `GetDebugPrototype()` 只分配不释放，调试路径会把 `asCContext` / `asCScriptFunction` 绑成永久泄漏源
    - [B] `DiscoveryPlans/RuntimeCore_Plan.md` — `Issue-65` 已给出“在 RuntimeCore release helper 中清理 `DebugFramePtr` / `DebugPrototypePtr`，并在 module discard / engine shutdown 回归中验证”的具体修复方向
    - [C] `RuntimeCore_TestGaps.md` — `NewTest-41` / `NewTest-38` 说明当前调试值基础设施与 debugger auto-evaluate 仍缺直接单元测试，debug-value 路径整体处于低覆盖状态
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L5330-L5417 — `GetStack()` 与 `GetDebugPrototype()` 当前仍分别 `new FAngelscriptDebugStack` / `new FDebugValuePrototype` 并挂到 `DebugFramePtr` / `DebugPrototypePtr`；同文件 L195-L215、L1026-L1124、L1132-L1252 — context release、module discard 与 engine shutdown 路径只做 `Release()` 或清空模块索引，没有任何对应的 debug cache 销毁逻辑
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDebugValue.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`
- [ ] **P5.15** 📦 Git 提交：`[Runtime/Debug] Fix: release debug-value caches on discard and shutdown`
- [ ] **P5.15-T** 单元测试：补齐 debug-value 缓存在 discard/shutdown 下的回收合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDebugValueLifetimeTests.cpp`
  - 测试场景：
    - 正常路径：命中 debug-value 路径后执行一次 module discard 或 owner shutdown，`DebugFramePtr` / `DebugPrototypePtr` 对应缓存会回到基线，下一轮重新加载可重新创建
    - 边界条件：同一 module/function 被重复调试、重复 discard 或多轮 create/destroy 时，缓存只在需要时复用，不会跨 epoch 单调增长
    - 错误路径：模块已 discard 或 owner 已 shutdown 后，旧 prototype/stack 不会再被下一轮 debug-value 访问复用，也不会把 stale pointer 留给新 epoch
  - 测试命名：`Angelscript.TestModule.Core.Debug.DebugValueCachesReleaseOnDiscardAndShutdown`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.15-T** 📦 Git 提交：`[Test/Core] Test: cover debug-value cache teardown`

## 单元测试总览补充（八）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.13` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptExecutionLocationContractTests.cpp` | JIT 空 `debugCallStack` sentinel、无上下文 sentinel、metadata 调用点稳定性 | P1 |
| `P5.14` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptJITExecutionStateTests.cpp` | JIT `this` 查询、pause 路径、JIT/bytecode 语义一致性 | P1 |
| `P5.15` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptDebugValueLifetimeTests.cpp` | debug-value cache 的 discard/shutdown 回收、多轮 epoch 基线恢复 | P1 |

## 验收标准补充（八）

19. `GetAngelscriptExecutionFileAndLine()` 在 JIT 空栈、无 active context 或空 callstack 下都会稳定返回 `"" / -1`，不再崩溃，也不会把脏输出继续写入资产 metadata。
20. JIT 执行态下 `GetAngelscriptExecutionThisObject()`、`GetAngelscriptExecutionPosition()`、`GetAngelscriptCallstack()` 与 `DebugBreak()` 共用同一条 execution-state resolver；JIT 帧内断点会优先暂停脚本调试器，而不是直接落到宿主级 `UE_DEBUG_BREAK()`。
21. `WITH_AS_DEBUGVALUES` 路径创建的 `DebugFramePtr` / `DebugPrototypePtr` 缓存在 module discard、context 释放与 owner shutdown 后都具备对称清理；多轮 full-engine epoch 不再累计旧调试缓存。

## 风险与注意事项补充（八）

### 风险

13. 一旦 `GetAngelscriptExecutionFileAndLine()` 明确返回 sentinel，少数历史调用点若仍把“空字符串/`-1` 不可能出现”当成前提，会在修复后暴露为断言或文案回归；需要同步收紧调用面而不是再把 helper 改回脏输出。
14. 把 JIT `DebugBreak()` 改成优先 pause 调试服务器后，现有依赖“直接打进原生断点”的本地调试习惯会改变；如果测试夹具或工具链默认期待 `UE_DEBUG_BREAK()`，需要先改成显式区分“脚本 pause”与“无脚本执行态时的 native break”。
15. debug-value cache 若只在部分 release 路径清理，反而容易形成“有些 context/function 清理了、有些没清理”的半迁移状态；实现时必须覆盖 context pool 回收、module discard 和 owner shutdown 三条正式生命周期。

### 已知行为变化

19. 无可用脚本执行位置时，`GetAngelscriptExecutionFileAndLine()` 以及依赖它的 metadata/日志路径将更稳定地看到 `"" / -1`，而不是偶然残留旧值或直接崩溃。
20. JIT 帧中的 `DebugBreak()` 将优先产出脚本调试器 pause 事件；只有 resolver 明确判定“当前不在任何脚本执行态”时才会继续退化为原生 `UE_DEBUG_BREAK()`。
21. 命中过 debug-value 路径的 context/function 在 discard 或 shutdown 后将主动释放调试缓存；下一轮 engine/module 重建不会再继承上一轮的 debug stack / prototype 残留。

---

## 深化 (2026-04-09 02:08:44)

本轮不重复 `Plan_TestEngineIsolation.md` 已承接的全仓 deglobalization 主线，也不重写 `P1.2`、`P2.4`、`P3.2`、`P5.9` 的既有条目；只补 4 个当前源码仍存在、但 `Plan_RuntimeCore.md` 尚未单列成执行项的 RuntimeCore 缺口：fake global-engine 生命周期表面、startup error dialog 的 completion 协议、显式 `DesiredScriptEngine` 的 owner/world-context 路由，以及 testing/prod bootstrap 的 duplicated core。

### Phase 5 深化：把 owner contract、startup completion 与 bootstrap 单一事实来源补齐

- [ ] **P5.16** 收口 RuntimeCore 对 fake global-engine 生命周期表面的依赖，只保留可诊断 shim
  - 当前 `TryGetGlobalEngine()` / `SetGlobalEngine()` / `DestroyGlobal()` / `GetOrCreate()` 仍以 public surface 暴露，但实现已经分别退化成 current-engine redirect、忽略参数的 ambient sync、恒定 `false` 和 deprecated `checkf`。更危险的是，RuntimeCore 自带的测试辅助仍把这些入口当成真实 lifecycle seam，在 `DestroyGlobal()` 上做 reset，在 `TryGetGlobalEngine()` 上做“当前全局主引擎”断言
  - 本项不重开 `Plan_TestEngineIsolation.md` 里的全仓 API 删除主线，只把 RuntimeCore 范围内仍依赖这组假入口的 helper、测试与断言迁到显式 owner/current-engine seam，并把遗留 shim 收紧成“只读诊断入口”，避免继续向新代码暗示存在一条可写的 global-engine 生命周期通道
  - 实施上先把 `AngelscriptTestUtilities`、`AngelscriptSubsystemTests` 和同类调用面改成 `FAngelscriptEngineScope`、`TryGetCurrentEngine()`、专用 destroy helper 或等价 owner-aware seam；再为 legacy global API 增加更强的弃用诊断与文档注释，明确它们不能控制 owner state；最后把 RuntimeCore 内部与测试文案里的 “global engine” 语义改成 `current engine` / `owner engine`，防止断言继续把 redirect 行为误写成生命周期真相
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `C-01` 指出 `TryGetGlobalEngine` / `SetGlobalEngine` / `DestroyGlobal` 已退化成别名与空操作，但测试辅助仍把它们当成真实全局状态 API
    - [B] `RuntimeCore_Plan.md` — `Issue-44` 要求移除失真的 public global-engine 生命周期入口，至少先把调用面迁离 fake contract
    - [E] `GapAnalysis.md` — 参考对比把 “runtime-owned registry + 可回收 owner” 列为可吸收模式，强调 owner 应显式可观测，而不是继续挂在误导性的 ambient/public 入口上
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L461-L464 — global-engine 相关入口仍以 public API 暴露；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L736-L779 — `TryGetGlobalEngine()` 直接 redirect 到 `TryGetCurrentEngine()`，`SetGlobalEngine()` 忽略参数，`DestroyGlobal()` 恒定返回 `false`，`GetOrCreate()` 只保留 deprecated `checkf`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptSubsystemTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptLegacyGlobalApiContractTests.cpp`
- [ ] **P5.16** 📦 Git 提交：`[Runtime/Core] Refactor: stop using fake global-engine lifecycle shims`
- [ ] **P5.16-T** 单元测试：补齐 legacy global-engine shim 的只读合同与迁移后调用面
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptLegacyGlobalApiContractTests.cpp`
  - 测试场景：
    - 正常路径：在 `FAngelscriptEngineScope` 内，legacy 读取入口与 `TryGetCurrentEngine()` 返回同一实例，但 helper/test 调用面已经不再依赖 `DestroyGlobal()` 做 cleanup
    - 边界条件：无 current engine 时，legacy 读取入口返回空值且不改写 ambient world/context；迁移后的 helper 仍可通过 owner-aware seam 正确复位状态
    - 错误路径：显式调用 `SetGlobalEngine()` / `DestroyGlobal()` 不会改变当前 owner，也不会让测试误以为发生了 teardown；如保留诊断，需断言会给出明确 deprecated/unsupported 信号
  - 测试命名：`Angelscript.TestModule.Core.Lifecycle.LegacyGlobalEngineShimsStayReadOnly`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.16-T** 📦 Git 提交：`[Test/Core] Test: cover legacy global-engine shim contract`

- [ ] **P5.17** 用显式结果对象与一次性同步原语替换 startup error dialog 的 `volatile` 自旋等待
  - `P2.4` 已经把 threaded initialize 的 ready window 和半初始化曝光列入主线，但启动编译报错恢复仍单独保留了一条更危险的死等支线：worker thread 把 `ShowErrorDialog()` 投给 game thread 后，只能靠 `volatile bool bErrorResponseDone` 自旋等待，而失败分支根本不写 completion
  - 本项不重复 `P2.2` 对 worker/TLD handoff 的拆分，只补“启动错误恢复必须有 completion signal”这条合同：成功、退出、窗口关闭与请求退出四条路径都必须生成唯一最终结果，等待方只消费结果对象，不再共享 `bSuccess` / `bErrorResponseDone` 这种裸状态
  - 实施上先引入 `EStartupCompileDialogResult` + `TPromise/TFuture`、`FEvent` 或等价 one-shot completion primitive，把 `ShowErrorDialog()` 改成无论成功还是退出都必定完成一次；再让 worker 等待结果对象而不是 `Sleep(0.01f)`；最后给对话框工厂或恢复入口补 test seam，确保自动化能分别模拟“修复成功继续启动”和“用户退出/请求退出”两条路径
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `A-28` 指出默认 threaded 初始化下，startup error dialog 在 worker/game-thread 之间使用 `volatile + Sleep` 忙等，且失败分支漏写完成位
    - [B] `RuntimeCore_Plan.md` — `Issue-16` 给出了“显式结果对象 + 一次性同步原语 + 双路径 completion” 的具体修复方向
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-50` / `Arch-SL-51` 强调 ready/started 状态必须建立在可判定的 lifecycle milestone 上，而不是靠裸布尔和早广播猜测
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L819-L848 — threaded `Initialize()` 当前仍用 `volatile bool bInitializationDone` + game-thread pump 等待 worker；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L2115-L2191 — startup error dialog 继续用 `volatile bool bErrorResponseDone`，失败路径 `RequestExit(true); return;` 前没有写 completion，worker 则在 `while (!bErrorResponseDone)` 中持续自旋
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupErrorDialogTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp`
- [ ] **P5.17** 📦 Git 提交：`[Runtime/Core] Fix: replace startup error-dialog spin wait with explicit completion`
- [ ] **P5.17-T** 单元测试：补齐 startup error dialog 在成功/退出两条路径上的 completion 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupErrorDialogTests.cpp`
  - 测试场景：
    - 正常路径：模拟脚本修复成功后，对话框结果会唤醒等待方，worker 能继续初始化且不会残留自旋线程
    - 边界条件：对话框在 game thread 执行、结果在 worker 消费时，只完成一次且不会重复广播或重复退出
    - 错误路径：模拟用户退出、窗口关闭或 `RequestExit` 路径时，等待方也会立即结束，不会因漏写 completion 卡住 startup worker
  - 测试命名：`Angelscript.TestModule.Core.Startup.ErrorDialogAlwaysSignalsCompletion`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.17-T** 📦 Git 提交：`[Test/Core] Test: cover startup error-dialog completion signaling`

- [ ] **P5.18** 把 `DesiredScriptEngine` 的 world-context 写回目标从 current-engine resolver 改成显式执行 owner
  - `P1.2` 已经把 ambient world / owner teardown 的底面问题列为主线，`P5.9` 也在收口 `AssignWorldContext()` 的线程保护，但当前仍缺一条更具体的执行层合同：`FAngelscriptContext` / `FAngelscriptGameThreadContext` 已经显式接收 `DesiredScriptEngine`，world-context 写回却依旧走 `TryGetCurrentEngine()` 命中的当前 wrapper
  - 这会让 “VM 所属 engine” 与 “world-context 被改写的 wrapper” 在设计上脱钩。ClassGenerator 的构造、defaults 与 `OptimizedCall_*` 路径大量直接构造 `FAngelscriptContext(Object, Function->GetEngine())` / `FAngelscriptGameThreadContext(Object, RealFunction->GetEngine())`，却没有同步给出 owner wrapper，因此 multi-engine/clone/adopted-engine 场景下很容易把 world-context 写进错误实例
  - 实施上先新增 `AssignWorldContextToEngine(FAngelscriptEngine& TargetEngine, UObject* NewWorldContext)` 或等价 helper，把 engine-local `WorldContextObject` 与 ambient 写回绑定到显式 owner；再扩展 `FAngelscriptContext` / `FAngelscriptGameThreadContext` 构造，要求调用点传入 owner wrapper 或等价 owner token；最后改造 `ASClass.cpp` 的 dispatch/construct/defaults 入口与静态 helper，使 “脚本在哪台 VM 上执行” 与 “world-context 写到哪台 wrapper” 使用同一份 owner 真相，并在 mismatch 时 `ensure` 拒绝静默污染
  - 来源：
    - [A] `RuntimeCore_Analysis.md` — `B-05` 指出显式 `DesiredScriptEngine` 与 `AssignWorldContext()` 的全局解析脱钩，类生成调用会把 world context 写入错误 engine
    - [B] `RuntimeCore_Plan.md` — `Issue-36` 给出了“显式 owner helper + context 构造透传 owner + mismatch 防御性校验”的执行方案
    - [C] `RuntimeCore_TestGaps.md` — `NewTest-59` 指出 current-context 静态 helper 只有 scope/ambient 恢复用例，没有覆盖 “current engine 与显式执行 owner 不一致” 的执行边界
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-12` 强调 engine identity 仍依赖 ambient context 与进程级 owner，建议把 runtime owner 提升成一等解析真相
    - [E] `GapAnalysis.md` — 参考对比将 “bind/type owner 显式化为可回收 registry” 作为多 engine 隔离基础，反面说明不能继续让 owner 隐藏在 ambient/current resolver 后面
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L746-L753 — `AssignWorldContext()` 仍只对 `TryGetCurrentEngine()` 命中的 wrapper 写 `WorldContextObject`，再同步 ambient；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1763-L1783 — `FAngelscriptContext` / `FAngelscriptGameThreadContext` 在收到显式 `DesiredScriptEngine` 后仍调用 `AssignWorldContext()`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` L1095-L1116、L1584-L1690 — 构造、defaults 与函数调度路径大量直接传入 `Function->GetEngine()`，但没有同步透传 wrapper owner
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptWorldContextOwnerRoutingTests.cpp`
- [ ] **P5.18** 📦 Git 提交：`[Runtime/Core] Fix: route world-context writes through explicit execution owner`
- [ ] **P5.18-T** 单元测试：补齐 `DesiredScriptEngine` / owner wrapper 失配时的 world-context 路由合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptWorldContextOwnerRoutingTests.cpp`
  - 测试场景：
    - 正常路径：`current engine = A`、显式 owner/`DesiredScriptEngine = B` 时，`FAngelscriptContext` / `FAngelscriptGameThreadContext` 只会把 `WorldContextObject` 写入 B，`TryGetCurrentWorldContextObject()` 与相关 static helper 能看到 B 的上下文
    - 边界条件：嵌套 scope、ambient fallback 与 current-context static helper 并存时，outer/inner owner 的 world 与 flag 切换保持对称，不会因 `DesiredScriptEngine` 路由修复而打破 `NewTest-59` 期望的 scoped/ambient 合同
    - 错误路径：显式 owner 与 resolver 命中的 current engine 不一致且无法建立有效映射时，会给出明确诊断并拒绝改写错误 wrapper，而不是继续静默污染另一台 engine
  - 测试命名：`Angelscript.TestModule.Core.WorldContext.DesiredScriptEngineWritesToExplicitOwner`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.18-T** 📦 Git 提交：`[Test/Core] Test: cover desired-engine world-context owner routing`

- [ ] **P5.19** 抽出 testing/prod 共用的 engine bootstrap 核心，避免 `InitializeForTesting()` 与 `Initialize_AnyThread()` 继续手工复制
  - `P3.2` 已经把 `Create()` / `CreateTestingFullEngine()` / `CreateForTesting()` 的 public 语义漂移列入主线，但当前更底层的重复仍在初始化实现内部：runtime flag 传播、root package 创建、`SetEngineProperty(...)`、message/context callback 注册被复制到 `InitializeForTesting()` 与 `Initialize_AnyThread()` 两条大路径里
  - 这不仅是可维护性问题，还会直接放大 profile drift。参考对比已经指出 testing-full 和 production-like init 的 owner 语义并不等价；如果继续靠两段复制代码维持同步，后续任何 engine property、callback、bind replay、precompiled gate 的修补都可能再一次只落一边
  - 实施上先抽 `ApplyRuntimeFlagsFromConfig()`、`CreateRootPackages()`、`ConfigureScriptEngineCommon()` 或等价 helper，保证 testing/prod 共用同一份 bootstrap 单一事实来源；再把 `InitializeForTesting()` 和 `Initialize_AnyThread()` 各自剩下的差异限定为 `BindScriptTypes()`、`InitialCompile()`、precompiled/hot-reload 与 shared-state orchestration；最后补一组 parity tests，锁住公共 property/callback/package baseline，同时允许 mode-specific 差异保持显式可测
  - 来源：
    - [B] `RuntimeCore_Plan.md` — `Issue-02` 明确指出初始化链把同一段 bootstrap 分散复制到多个大函数里，缺少公共 helper 与显式阶段模型
    - [D] `ScriptLifecycle_ArchReview.md` — `Arch-SL-01` / `Arch-SL-03` 要求 startup 形成可编排的 phase pipeline，并把全量启动与其它 owner/surface 的 orchestration 分层
    - [E] `GapAnalysis.md` — 参考对比指出 `Initialize()` 与 `InitializeForTesting()` 的 owner 语义存在实质差异，继续把 minimal bootstrap 当 production parity 证据会掩盖 profile drift；`CrossComparison.md` 进一步提醒应避免落入 “只共享一半核心、另一半仍靠 duplicated orchestration” 的 `Partial shared core` 中间态
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L819-L920 — `Initialize()` 负责 threaded orchestration，而 `InitializeForTesting()` 独自复制 runtime flag、package 与 engine property/callback 配置；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1280-L1425 — `PreInitialize_GameThread()` 与 `Initialize_AnyThread()` 再次拆散 engine 创建和通用 bootstrap，`Initialize_AnyThread()` 继续复制与 testing 路径高度相似的 property/callback 设置
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineBootstrapParityTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineCoreTests.cpp`
- [ ] **P5.19** 📦 Git 提交：`[Runtime/Core] Refactor: extract shared engine bootstrap for testing and production`
- [ ] **P5.19-T** 单元测试：补齐 testing/prod bootstrap 公共基线与 mode-specific 差异的 parity 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineBootstrapParityTests.cpp`
  - 测试场景：
    - 正常路径：`InitializeForTesting()` 与 production-like `Initialize()` 在共享 bootstrap 段上的 runtime flag、root package、关键 `SetEngineProperty(...)`、message/context callback 完全一致
    - 边界条件：testing/prod 仍保留显式差异，例如 `BindScriptTypes()`、`InitialCompile()`、precompiled/hot-reload 与 shared-state orchestration，不会因抽公共 helper 而互相污染
    - 错误路径：后续若只在其中一条路径修改 engine property、callback 或 package 初始化，parity test 能直接报出 drift，而不是继续让 smoke 测试侧面猜测
  - 测试命名：`Angelscript.TestModule.Core.Startup.CommonBootstrapStaysInSyncAcrossProfiles`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.19-T** 📦 Git 提交：`[Test/Core] Test: cover testing-production bootstrap parity`

## 单元测试总览补充（九）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.16` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptLegacyGlobalApiContractTests.cpp` | legacy global shim 只读合同、helper 迁移后 cleanup、deprecated 误用诊断 | P1 |
| `P5.17` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupErrorDialogTests.cpp` | startup error dialog 成功/退出 completion、game-thread UI 与 worker 消费分离、无自旋挂死 | P0 |
| `P5.18` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptWorldContextOwnerRoutingTests.cpp` | `DesiredScriptEngine` 显式 owner 路由、nested scope/ambient helper 边界、wrong-owner 拒绝写回 | P0 |
| `P5.19` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineBootstrapParityTests.cpp` | testing/prod 共用 bootstrap 基线、mode-specific 差异保持显式、后续 drift 报警 | P1 |

## 验收标准补充（九）

22. RuntimeCore 自身与其测试夹具不再把 `TryGetGlobalEngine()` / `SetGlobalEngine()` / `DestroyGlobal()` / `GetOrCreate()` 当成 lifecycle control seam；legacy global API 至多保留为只读/可诊断 shim，不再制造“已经 teardown” 的假象。
23. startup compile-error recovery 在成功、退出、窗口关闭和请求退出四条路径上都具备一次性 completion signal；等待方不再依赖 `volatile + Sleep` 忙等，也不会在失败路径确定性挂死。
24. 当 `DesiredScriptEngine` / 显式执行 owner 与 current engine 不一致时，world-context 的设置与恢复仍只会作用在正确 owner wrapper；错误 owner 不会再被静默改写。
25. `InitializeForTesting()` 与 production-like `Initialize()` 共用同一份 bootstrap 单一事实来源；关键 runtime flag、root package、engine property 和 callback 注册的公共基线有 parity test 锁定，后续漂移会被直接捕获。

## 风险与注意事项补充（九）

### 风险

16. 如果在迁移 helper/test 调用面之前就简单删除 legacy global API，仍残留的 RuntimeCore 调用点会从 silent no-op 变成大面积编译/运行失败；执行顺序必须是“先迁消费面，再收紧 shim”。
17. startup error dialog 改成显式结果对象后，最容易引入的新回归是 double-completion 或 double-exit；需要把 “谁写最终结果、谁负责退出” 压成单一 owner，而不是让 UI 线程和 worker 都能终结流程。
18. `DesiredScriptEngine` 修复的关键不是单纯多传一个参数，而是建立稳定的 `script engine -> wrapper owner` 真相来源；若 owner 映射仍有空洞，修复后会把过去的串线从“静默成功”升级为显式失败，需要同步补 diagnostic。
19. 抽公共 bootstrap helper 时，最容易把 production-only 或 testing-only 的副作用拉错路径；必须先通过 parity test 固定公共段，再把 mode-specific 差异显式列出，而不是再次复制出第三份初始化分支。

### 已知行为变化

22. RuntimeCore 测试与 helper 里的“global engine”文案和断言会被收紧为 `current engine` / `owner engine` 语义；误把 redirect/no-op shim 当 lifecycle 控制面的旧写法将不再被接受。
23. 启动编译报错后的等待行为会从“worker 自旋直到看见布尔位”变成“等待显式结果对象”；退出路径会更早、更确定地结束，而不是继续悬在自旋循环里。
24. `FAngelscriptContext` / `FAngelscriptGameThreadContext` 在传入显式 owner/`DesiredScriptEngine` 时，会优先改写该 owner 的 `WorldContextObject`；依赖 ambient/current resolver 偶然写到另一台 engine 的旧行为将被视为错误。
25. testing/prod 初始化今后会共享同一份核心 bootstrap 配置；如果某个改动只想影响其中一条路径，必须显式落在 mode-specific 差异段，而不是再往复制块里插语句。

---

## 深化 (2026-04-09 06:39:53)

> 说明：本轮复核时，用户指定的 `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md` 在仓库中不存在；以下 [B] 暂引用同批 `Documents/AutoPlans/2026-04-07/RuntimeCore/01_Proposals.md` 中可回链到同一源码问题的 proposal，且只读不写。

### Phase 5 深化：补齐 owner / snapshot / callback 尾项

- [ ] **P5.20** 把 test-only `SnapshotAndClear()/RestoreSnapshot()` 接入 ambient-world 同步合同
  - `P1.2` 与 `P5.3` 已经开始收口 live world-context 与 public scope helper，但当前 dev-automation 快照入口仍只搬运 `ContextStack`，不会同步 `GAmbientWorldContext`。这会让测试与 helper 继续看到“栈已清空/恢复，但 ambient 仍停在旧值”的不可能状态，掩盖 world-context 真相。
  - 本项只补 testing seam，不重复 `P5.3` 对 public helper move-only / owner-aware 的主线：把 `SnapshotAndClear()`、`RestoreSnapshot()` 和相关 guard 接到现有 `SyncAmbientWorldContextFromCurrentEngine()` / validated world-context helper，同步修正 automation 场景下的 ambient state。
  - 实施上先在 `FAngelscriptEngineContextStack::SnapshotAndClear()` 与 `RestoreSnapshot()` 里统一调用 ambient sync，再把依赖这两个 helper 的 engine isolation / test helper 用例改成同时断言 stack 与 ambient；必要时补一个只供测试 guard 使用的最小 helper，避免测试继续直接偷写进程级 global。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `A-04` / `D-04` / `A-32` / `D-22` 指出 ambient world 是裸全局状态，且自动化当前既不校验 snapshot seam 是否同步 ambient，也不覆盖失效恢复路径。
    - [B] `Documents/AutoPlans/2026-04-07/RuntimeCore/01_Proposals.md` — `方案 P2` 已明确把 “`SnapshotAndClear()` / `RestoreSnapshot()` 只清 `ContextStack`、不同步 ambient” 列成单独修复点，并给出 `SyncAmbientWorldContextFromCurrentEngine()` 接线方向。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `NewTest-59` 只锁 current-context helper 的 scoped/ambient 正常合同，仍没有直接覆盖 `SnapshotAndClear()` / `RestoreSnapshot()` 对 ambient 的同步语义。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L423-L434 — `SnapshotAndClear()` / `RestoreSnapshot()` 当前只搬运 `GAngelscriptEngineContextStack`；同文件 L268-L300 — ambient world 的正规同步入口已经集中在 `SetAmbientWorldContext()` / `SyncAmbientWorldContextFromCurrentEngine()`，但测试快照入口没有调用；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L710-L724 — `FAngelscriptGameThreadScopeWorldContext` 仍把 ambient 当作全局单例来恢复，因此 snapshot seam 一旦不同步会放大后续恢复歧义。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`
- [ ] **P5.20** 📦 Git 提交：`[Runtime/Core] Fix: sync ambient world state in context-stack snapshots`
- [ ] **P5.20-T** 单元测试：补齐 stack snapshot 与 ambient world 同步 regression
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp`
  - 测试场景：
    - 正常路径：在 `FAngelscriptEngineScope` + `FAngelscriptGameThreadScopeWorldContext` 下调用 `SnapshotAndClear()` 后，`FAngelscriptEngineContextStack::IsEmpty()` 与 `GetAmbientWorldContext()` 同时回到空基线；`RestoreSnapshot()` 后两者同时恢复。
    - 边界条件：outer/inner 两层 engine scope 嵌套时，对 inner snapshot/restore 只能恢复 inner 当前值；退出 inner 后 outer 的 current-engine 与 ambient world 仍保持对称。
    - 错误路径：对空快照或重复 restore 运行 guard 时，ambient world 不会残留进入前旧值，也不会在 stack 已空时继续暴露陈旧 world context。
  - 测试命名：`Angelscript.CppTests.Engine.Isolation.SnapshotRestoreSyncsAmbientWorldContext`
  - 隔离方式：`FCoreTestContextStackGuard` + `FAngelscriptEngineScope`
- [ ] **P5.20-T** 📦 Git 提交：`[Test/Core] Test: cover context-stack snapshot ambient sync`

- [ ] **P5.21** 把 `RuntimeModule` 的 existing-current-engine 分支收口成 adopt-only / rollback 合同
  - `P1.1` 已经在收 runtime-module 的 override owner 与初始化状态机，但还有一条分支尚未单列：`InitializeAngelscript()` 看到已有 current engine 时仍直接 `CurrentEngine->Initialize()`。这会把外部 owner 已经发布的引擎重新走一遍完整 bootstrap，并把 runtime/testing 两条入口的“是否允许重复初始化”合同继续撕开。
  - 本项不是重述 `P1.1` 的整个 state machine，而是把剩余的 current-engine 旁路补齐：module 只能 adopt 已 ready 的 current engine；若看到的是半初始化 engine，则必须 fail-fast 或 rollback，而不是先把 `bInitializeAngelscriptCalled` 锁死再静默重入。
  - 实施上先把 `bInitializeAngelscriptCalled` 的提交时机从函数入口后移到 adopt/create 成功之后，再把 current-engine 分支改成 `ready-or-fail`；最后补一条直接命中“已有 current engine”分支的 regression，确认不会替换 `asIScriptEngine`、不会多压一层 `ContextStack`，也不会把失败的半初始化 owner 永久卡成“已初始化过”。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `A-13` 指出 `InitializeAngelscript()` 当前会在已有 current engine 时二次调用 `Initialize()`，与 runtime owner 语义冲突。
    - [B] `Documents/AutoPlans/2026-04-07/RuntimeCore/01_Proposals.md` — `方案 P13` 已把这条分支归类为 reentrant initialize，并明确建议改成 adopt-only / fail-fast。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `NewTest-22` / `NewTest-65` / `NewTest-66` 当前只覆盖 override、fallback tick 与 shutdown，仍没有任何一条测试先建立 current engine 再调用 `InitializeAngelscript()`。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` L138-L165 — module 入口当前先置 `bInitializeAngelscriptCalled = true`，随后在 current-engine 分支直接 `CurrentEngine->Initialize()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L820-L857 — `Initialize()` 每次都会进入完整 `PreInitialize_GameThread()` / `Initialize_AnyThread()` / `PostInitialize_GameThread()` / `InitializeOwnedSharedState()`；同文件 L859-L864 — `InitializeForTesting()` 却在 `Engine != nullptr` 时直接返回，说明 runtime/testing 合同已经分叉。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptRuntimeModuleTests.cpp`
- [ ] **P5.21** 📦 Git 提交：`[Runtime/Core] Fix: make runtime module adopt existing ready engine`
- [ ] **P5.21-T** 单元测试：补齐 current-engine adopt-only 分支与 rollback 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptRuntimeModuleTests.cpp`
  - 测试场景：
    - 正常路径：先发布一台已完成初始化的 full engine，再调用 `InitializeAngelscript()`，断言 module 只 adopt 当前实例，不会替换 `GetScriptEngine()` 指针。
    - 边界条件：连续两次调用 `InitializeAngelscript()` 命中同一 ready engine 时，不会额外压栈、不会重复注册 fallback tick，也不会修改已存在 owner 的 runtime state。
    - 错误路径：当前可见 engine 仍处于半初始化或 `GetScriptEngine()==nullptr` 时，module 会给出明确诊断并 rollback `bInitializeAngelscriptCalled`，修正输入后允许重试。
  - 测试命名：`Angelscript.CppTests.RuntimeModule.Initialize.AdoptsExistingCurrentEngine`
  - 隔离方式：`FCoreTestContextStackGuard` + `FAngelscriptEngineScope`
- [ ] **P5.21-T** 📦 Git 提交：`[Test/Core] Test: cover runtime module current-engine adopt path`

- [ ] **P5.22** 把 compile/debug/coverage callback 从 ambient `Get()` 反查改成 explicit owner + request-local diagnostics
  - 当前 `P4.1` 已经在补 clone/source 的 line-callback 聚合状态，但更底层的 callback 入口仍然是错的：`LogAngelscriptError()`、`AngelscriptLineCallback()`、`AngelscriptStackPopCallback()`、`LogAngelscriptException(asIScriptContext*)` 都继续走 `FAngelscriptEngine::Get()`，而 compile diagnostics 还叠加了进程级 `static PreviousSection/PreviousType` 去重。只要 callback 来源 context/engine 与 current-engine 不同，diagnostics、debug server、coverage 命中就会落到错误 wrapper。
  - 本项不重做 `DebugServer V2` 协议，也不重复 `P4.1` 对 clone 投影的上层收口；目标是先把所有 callback 入口改成 owner-aware：compile diagnostics 走 request-local / engine-local sink，line/stack/exception callback 则按 `asIScriptContext*` 或 `asIScriptEngine*` 明确解析 owner，而不是继续依赖 ambient current-engine。
  - 实施上先在 `LogAngelscriptError()` 上消掉 `PreviousSection/PreviousType` 这组进程级静态去重，把 section/type 去重挪进 compile request 或 engine-local diagnostics batch；再抽 `ResolveOwnerEngineFromContext(...)` / `ResolveDiagnosticsSink(...)` 一类 helper，让 line/stack pop/exception/coverage 都按 callback 来源 owner 路由；最后把 `DebugServer`、`CodeCoverage` 与 diagnostics flush 改成“找不到 owner 时安全丢弃或显式告警”，不再去污染另一台 current engine。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `A-19` / `B-12` 指出并行编译 diagnostics callback 没有绑定来源 engine，且 line/debug/coverage callback 都是重新走全局 current-engine 解析。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-58` 明确要求把 compile worker 的共享状态改成 request-local artifact + 串行 merge，diagnostics 不应继续写 request-global / engine-global 静态状态。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — `构建资产与 live 协议分层` / `runtime-owned registry + 可观测 bind plan` 强调 runtime protocol owner 应显式可路由，而不是继续落在 ambient state + heuristic 上。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L5012-L5034、L5077-L5084 — `LogAngelscriptError()` 当前用 `FAngelscriptEngine::Get()` 拿 manager，并以 `static PreviousSection/PreviousType` 在进程级做去重；同文件 L5429-L5562 — `UpdateLineCallbackState()`、`AngelscriptLineCallback()`、`AngelscriptStackPopCallback()` 仍直接取 current engine 的 `DebugServer` / `CodeCoverage`；同文件 L5244-L5309 — exception 路径也仍通过 `FAngelscriptEngine::Get()` 处理 debugger watch 与 `ProcessException()`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/CodeCoverage/AngelscriptCodeCoverage.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCallbackOwnerRoutingTests.cpp`
- [ ] **P5.22** 📦 Git 提交：`[Runtime/Core] Fix: route callbacks and diagnostics by explicit owner`
- [ ] **P5.22-T** 单元测试：补齐 callback owner 路由与 request-local diagnostics regression
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCallbackOwnerRoutingTests.cpp`
  - 测试场景：
    - 正常路径：engine A 触发 compile diagnostics 时，即使 engine B 暂时位于 current-engine 位置，diagnostics 也只写入 A 的 request/owner sink，B 不会收到 section/type 去重污染。
    - 边界条件：engine A 的 script context 触发 `ProcessScriptLine()` / `ProcessScriptStackPop()` 时，clone 或另一台 full owner 成为 current engine 也不会把 debug server / coverage 命中改路由到错误 wrapper。
    - 错误路径：owner engine 已释放或 callback 无法解析到合法 owner 时，入口会安全返回或给出明确 diagnostic，不会继续修改另一台引擎的 diagnostics、debug state 或 coverage 统计。
  - 测试命名：`Angelscript.CppTests.Engine.Callbacks.RouteByContextOwnerAndRequest`
  - 隔离方式：`FCoreTestContextStackGuard` + `FAngelscriptEngineScope`
- [ ] **P5.22-T** 📦 Git 提交：`[Test/Core] Test: cover callback owner routing and request-local diagnostics`

- [ ] **P5.23** 给多 full-owner 生命周期建立可恢复的 `primaryContext` 归属链
  - `P2.2` 已经在收 threaded initialize 的 `primaryContext` handoff，但当前还留着另一条没有单列的全局状态：无论 testing 还是 full initialize，都直接覆写唯一的 `GameThreadTLD->primaryContext`，`SharedState` 只是快照当前指针；后创建 owner 销毁后不会恢复前一个 owner，只会把 usable-context 槽清空。
  - 这会让多 full owner 表面上仍能共存，但 `asGetUsableContext()`、脚本对象 factory/析构/`opAssign` 等真实运行路径失去稳定 baseline。它不是简单的性能退化，而是 owner 生命周期没有“上一个 primary context 是谁”的恢复模型。
  - 实施上先在 owner/full shared-state 上引入 `PrimaryContextLease` 或等价字段，记录安装到 `GameThreadTLD->primaryContext` 前的前序 context 及其 owner epoch；再把 `InitializeForTesting()`、`Initialize_AnyThread()`、`InitializeOwnedSharedState()` 与 teardown 路径统一改成 `InstallPrimaryContextForOwner()` / `RestorePreviousPrimaryContextForOwner()`；最后补 `asGetUsableContext()` regression，保证销毁后创建的 owner 之后，幸存 owner 仍能恢复自己的 usable-context。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `A-29` / `D-17` 指出多个 full engine 共享单个 `primaryContext` 槽，且现有自动化完全没有守住 `asGetUsableContext()` / `primaryContext` 合同。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `Issue-40` 已明确指出 `CreateDestroy` 只检查裸指针 reset，没有覆盖 `FAngelscriptEngine` 的关键生命周期合同，这正好漏掉了 `primaryContext` / usable-context 基线。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-50` / `Arch-SL-51` 强调 `SharedStateReady` 必须暴露稳定 `PrimaryContext`，且当前 lifecycle state 已经不足以表达 full/test/clone 的真实 ready 语义。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L141 — `GameThreadTLD` 仍是单一静态线程槽；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L917-L919、L1565-L1569 — testing/full 初始化都直接覆写 `GameThreadTLD->primaryContext`；同文件 L934-L935 — `InitializeOwnedSharedState()` 只是快照当前槽值进 `SharedState->PrimaryContext`；同文件 L1179-L1182、L354-L361 — shutdown / shared-state release 只会 release 并清空匹配值，没有恢复前序 owner 的模型。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrimaryContextLifecycleTests.cpp`
- [ ] **P5.23** 📦 Git 提交：`[Runtime/Core] Fix: restore previous primary-context owner across full-engine lifetimes`
- [ ] **P5.23-T** 单元测试：补齐 multi-full-owner `primaryContext` / `asGetUsableContext()` 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrimaryContextLifecycleTests.cpp`
  - 测试场景：
    - 正常路径：先创建 full owner A，再创建 full owner B；销毁 B 后，`GameThreadTLD->primaryContext` 与 `asGetUsableContext()` 会恢复指向 A 的 usable context。
    - 边界条件：销毁最后一个 full owner 时，`primaryContext` 槽会清空；随后新 owner 启动会得到新的 primary context，而不是复用已释放实例。
    - 错误路径：销毁 clone wrapper 或 testing owner 时，不会错误 release/清空仍属于幸存 full owner 的 primary context；相关 factory / `opAssign` 路径仍能走 usable-context 快路径。
  - 测试命名：`Angelscript.CppTests.Engine.Lifecycle.PrimaryContextRestoresPreviousOwner`
  - 隔离方式：`FCoreTestContextStackGuard` + `FAngelscriptEngineScope`
- [ ] **P5.23-T** 📦 Git 提交：`[Test/Core] Test: cover primary-context restoration across full owners`

## 单元测试总览补充（十）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.20` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptEngineIsolationTests.cpp` | `SnapshotAndClear()` / `RestoreSnapshot()` 同步 ambient world、nested scope 恢复、空快照不残留旧值 | P1 |
| `P5.21` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptRuntimeModuleTests.cpp` | current-engine adopt-only、重复调用不重入、半初始化 owner rollback | P0 |
| `P5.22` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptCallbackOwnerRoutingTests.cpp` | compile diagnostics owner 路由、line/stack/exception callback 不串 wrapper、owner 缺失安全失败 | P0 |
| `P5.23` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrimaryContextLifecycleTests.cpp` | 多 full owner `primaryContext` 恢复、最后 owner 清槽、clone/testing teardown 不误清 usable context | P1 |

## 验收标准补充（十）

26. `FAngelscriptEngineContextStack::SnapshotAndClear()` / `RestoreSnapshot()` 不再只改 stack：ambient world 会与 current-engine 解析结果保持同步，测试 guard 不会制造 impossible mixed state。
27. `FAngelscriptRuntimeModule::InitializeAngelscript()` 在已有 current engine 时只 adopt ready owner；它不会再次执行 `Initialize()`，也不会因半初始化 owner 把 module 永久卡成“已初始化过”。
28. compile diagnostics、debug server、coverage line-hit 与 exception callback 都按 explicit owner/request-local sink 路由；多 engine / clone /并行编译场景下，不会再因为 current-engine 变化把 callback 结果写进错误 wrapper。
29. 多 full-owner 生命周期下，`primaryContext` / `asGetUsableContext()` 具备“后创建先销毁后恢复前序 owner”的稳定合同；销毁最后 owner 时才清空线程槽。

## 风险与注意事项补充（十）

### 风险

20. `SnapshotAndClear()` 开始同步 ambient world 后，少数历史测试若依赖“清栈但保留 ambient”的隐式行为会立即转红；这些用例需要改成显式断言合法 baseline，而不是继续吃脏状态。
21. adopt-only current-engine 分支一旦 fail-fast，会暴露那些过去依赖 “RuntimeModule 顺手帮我把 current engine 初始化完” 的路径；必须同时给出 rollback / retry 合同，否则会把旧脏状态从“静默重入”换成“永久卡死”。
22. callback owner 路由的难点不在改几处 `Get()`，而在建立稳定的 `context/script engine -> owner runtime` 解析真相；如果 owner registry 不完整，修复会把过去的 silent pollution 升级成显式 missing-owner failure。
23. `primaryContext` 恢复若只靠裸指针比较而没有 owner epoch / refcount 保护，最容易把已释放 context 恢复回 `GameThreadTLD`；实现时必须把“谁安装、谁恢复、谁最终 release”压成单一合同。

### 已知行为变化

26. dev-automation 的 stack snapshot helper 今后会同时刷新 ambient world；依赖 “SnapshotAndClear() 只清 stack、不动 ambient” 的旧测试基线将不再成立。
27. `InitializeAngelscript()` 不再“帮忙”重入初始化已发布的 current engine；调用方必须先发布 ready owner，或显式处理 fail-fast / retry 结果。
28. 编译诊断的 section/type 去重会从进程级静态状态收紧为 request-local / owner-local 行为；不同 engine 的 diagnostics 输出顺序与折叠边界会因此更稳定，也可能与旧日志快照不同。
29. 销毁较新的 full owner 后，幸存 owner 的 usable-context 会被恢复，而不是继续留空；多 owner 生命周期下的 `asGetUsableContext()` 结果将更接近真实 owner 链，而不是“最后一次覆写留下的状态”。

---

## 深化 (2026-04-09 06:51:35)

本轮只补一条当前文档尚未单列、且不与 `Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md` 的 directory-watcher seam 重复的 RuntimeCore 缺口：threaded-init 期间的 `OnAsyncLoadingFlushUpdate` 仍只是全局 progress pump，没有 engine-owned startup flush phase / async-attach candidate ledger。它与 `P2.2` 的 thread-affinity、`P3.1` 的 ready milestone 直接相邻，但当前源码里还没有单独的 owner 合同和测试闸门。

### Phase 5 深化：把 threaded-init 的 global flush progress pump 收口成 owner-aware startup flush phase

- [ ] **P5.24** 把 `OnAsyncLoadingFlushUpdate` 从 threaded-init 的无差别 progress pump 收口成显式 startup flush phase，并为 async-loaded attach 引入 engine-owned candidate ledger
  - 当前 `Initialize()` 在 worker thread 暂时改写 `GameThreadTLD` 的同时，让 game thread 在 `while (!bInitializationDone)` 循环里反复 `Broadcast(OnAsyncLoadingFlushUpdate)`；但 engine 自身并没有任何 `PendingAsyncAttachCandidates` / flush handle / readiness gate。结果是 flush delegate 的语义仍停留在“帮初始化窗口泵消息”，而不是“owner 已 ready、现在可以安全 drain 某批候选对象/回调”。
  - 本项不重做 object hook 哲学，也不去复制 `sluaunreal` 的 override queue；目标只是把 RuntimeCore 自己的生命周期合同补完整：`Core` 侧显式区分 `StartupProgressPump` 与 `StartupFlushReady`，并给 future async-loaded attach / postload bind 预留 engine-owned candidate ledger。global flush 若仍要保留，只能作为 compatibility wrapper，不能继续在 half-initialized window 内裸广播。
  - 实施上先在 `AngelscriptEngine.h/.cpp` 新增最小 `FAsyncAttachCandidate`（或等价 `FStartupFlushCandidate`）与 `DrainStartupFlushCandidates_GameThread()`；`Initialize()` 的 threaded wait loop 不再直接广播全局 flush，而是只处理 engine-owned 候选 drain 或受 phase gate 控制的兼容广播；`Shutdown()`、初始化失败回滚和 `RequestExit*()` 统一清空未消费候选，避免半初始化 owner 在 teardown 后仍可能触发 attach。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `A-11` 指出 threaded `Initialize()` 会在 worker thread 改写全局 `GameThreadTLD` 的同时，让 game thread 持续跑 `OnAsyncLoadingFlushUpdate.Broadcast()` 和任务循环，启动窗口内任何依赖 flush 的侧路都缺显式 owner / seam。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-40` / `Arch-SL-41` 要求把 startup phase、thread affinity 与 readiness milestone 显式化，而不是继续让扩展在粗粒度广播和硬编码 core 时序上自行猜测“现在是否安全”。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — `[D3] 异步加载完成后的对象接管 safe point` 明确指出当前 Angelscript 只有 threaded-init 中的 `OnAsyncLoadingFlushUpdate` progress pump，没有 `UnrealCSharp` / `UnLua` / `sluaunreal` 那样的插件自有 candidate ledger / flush-drain owner，建议先吸收最小的 engine-owned queue + flush fence 模型。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L827-L845 — threaded `Initialize()` 当前在 `volatile bool bInitializationDone` + `AsyncTask(...)` 等待循环里直接 `FCoreDelegates::OnAsyncLoadingFlushUpdate.Broadcast()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L406-L419 — engine owner 目前只显式持有 `bUseHotReloadCheckerThread`、`FileHotReloadState`、`bWaitingForHotReloadResults`、`HotReloadTestRunner`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles`，没有任何 async-loaded candidate ledger、flush handle 或 startup-flush readiness 状态。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
- [ ] **P5.24** 📦 Git 提交：`[Runtime/Core] Refactor: replace threaded startup flush pump with owner-aware startup flush phase`
- [ ] **P5.24-T** 单元测试：补齐 startup flush phase / async-attach candidate ledger 的生命周期合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupFlushPhaseTests.cpp`
  - 测试场景：
    - 正常路径：强制 threaded initialize 时，pending startup / async-attach candidate 只会在 `GameThread` 且 runtime 进入显式 ready phase 后 drain 一次；drain 前后 current-engine、`WorldContextObject` 与 shared-state 不会被半初始化窗口提前污染。
    - 边界条件：等待循环多次迭代或重复触发 flush pump 时，同一 candidate 不会被重复 attach；object 仍处于 `RF_NeedPostLoad` / `NeedInitialization` 等未 ready 条件时，会继续留在 engine-owned ledger，等下一次合法 flush 再处理。
    - 错误路径：初始化失败、`RequestExit*()` 或 `Shutdown()` 先发生时，未消费 candidate 会被统一丢弃；之后即使再有 stray flush / broadcast，也不会命中已 teardown owner 或产生悬空 attach。
  - 测试命名：`Angelscript.TestModule.Core.StartupFlush.CandidatesDrainOnlyAfterReadyPhase`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.24-T** 📦 Git 提交：`[Test/Core] Test: cover owner-aware startup flush phase and candidate drain`

## 单元测试总览补充（十一）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.24` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupFlushPhaseTests.cpp` | threaded-init ready phase、pending candidate drain、shutdown / exit 丢弃未消费候选 | P1 |

## 验收标准补充（十一）

30. threaded `Initialize()` 不再在 `GameThreadTLD` 尚由 worker thread 临时接管的窗口里裸广播全局 `OnAsyncLoadingFlushUpdate`；startup flush 只能通过 engine-owned phase / candidate ledger 在显式 ready 条件下执行。
31. 初始化失败、`RequestExit*()` 或 `Shutdown()` 后不会残留未消费的 startup / async-attach candidate，也不会在 teardown 后再触发 attach 或恢复半初始化 owner。

## 风险与注意事项补充（十一）

### 风险

24. 若现有 editor/tooling helper 曾把 threaded startup 期间的 `OnAsyncLoadingFlushUpdate` 误当成“现在就可以执行 attach work”的信号，收口为显式 phase 后会暴露出一批“候选永远不 drain”或“调用时机过早”的历史假设；需要同步迁移这些消费者，或保留有明确 gate 的兼容包装层。
25. candidate ledger 如果只保存裸 `UObject*` 而不在每次 drain 时重新校验有效性、`RF_NeedPostLoad` 和 `NeedInitialization`，很容易把当前的 progress-pump 问题换成“teardown 后 attach 旧对象”这一类更隐蔽的悬空恢复缺陷。

### 已知行为变化

30. threaded startup 期间的 `OnAsyncLoadingFlushUpdate` 不再代表“任意外部 attach work 都可以立刻执行”；只有通过 startup flush phase 或 engine-owned candidate ledger 注册的工作，才会在 owner ready 后被 drain。
31. async-loaded / postload 对象在 runtime ready 之前会先留在 engine-owned ledger，而不是继续搭车每轮等待循环的 progress pump；其 attach 时机会比今天更晚，但语义会更稳定。

---

## 深化 (2026-04-09 07:01:18)

本轮不重写 `P1.2` 的 world-context validated resolve 主线，也不重复 `Plan_TestEngineIsolation.md` 已经建立的 subsystem by-value `UPROPERTY() FAngelscriptEngine OwnedEngine` carrier；只补当前文档尚未单列的一条 carrier 缺口：`FAngelscriptRuntimeModule` 仍用静态 `TUniquePtr<FAngelscriptEngine>` 持有 module-owned owner，而 `FAngelscriptEngine` 内部多处 `UObject*` 成员继续依赖 `USTRUCT + UPROPERTY` 的表面语义。只要该 owner 不在 `UObject`/`FGCObject`/`AddStructReferencedObjects` 可见链上，这套“看起来可追踪”的字段在 module path 上仍会退化成裸指针合同。

### Phase 5 深化：把 module-owned runtime carrier 的 `UObject` 成员收口成显式 reference policy

- [ ] **P5.25** 收口 `RuntimeModule` owner 的 GC 可见性，移除 `FAngelscriptEngine` 在 module carrier 下对 `UPROPERTY` 的假保护
  - `P1.2` 已经在收 world-context validated resolve，但当前还缺 carrier 层的一条硬约束：`FAngelscriptEngine` 是 `USTRUCT()`，`WorldContextObject`、`AngelscriptPackage`、`AssetsPackage`、`ConfigSettings` 都写成了 `UPROPERTY()`；只有当这个 struct 真的挂在可被 UE GC 枚举的 owner 链上时，这些字段才有追踪意义。subsystem 的 `UPROPERTY() FAngelscriptEngine OwnedEngine` 满足这一点，`RuntimeModule` 的 `static TUniquePtr<FAngelscriptEngine> OwnedPrimaryEngine` 则完全不满足。
  - 本项不是把 `RuntimeModule` 退回全局 singleton，也不是重复 `P1.2` 的 resolver 修复；目标是把 `FAngelscriptEngine` 在不同 carrier 下的 `UObject` 生命周期语义收紧成一套显式 policy：`WorldContextObject` 改成 weak/validated slot，不再假装靠 `UPROPERTY` 被 module path 自动追踪；确实需要强持有的对象则通过 `FGCObject`、`AddReferencedObjects` 或等价 owner collector 进入显式引用图，而不是继续依赖“struct 上写了 `UPROPERTY()`”。
  - 实施上先区分 `WorldContextObject` 与 package/settings 的 reference policy：`WorldContextObject` 统一改成 weak 读取 + validated resolve；`AngelscriptPackage`、`AssetsPackage`、`ConfigSettings` 等真正需要 owner 生命周期托管的对象，则新增 `FAngelscriptEngine::AddReferencedObjects(...)` 或 module-local referencer helper，由 subsystem carrier 与 runtime-module carrier 共用；最后清理直接裸读 `GetCurrentWorldContextObject()` 的路径，确保 GC 后只会得到 `nullptr` 或显式 owner-managed 引用，而不会读到“看似 UPROPERTY、实际未被遍历”的悬空值。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `A-08` / `A-27` 指出 module-owned `OwnedPrimaryEngine` 不在 GC 引用图里，而 `WorldContextObject` 仍被当作受追踪成员使用；一旦 editor/commandlet 主引擎绑定过临时 world context，module path 上的 current-engine helper 仍可能读到悬空指针。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `NewTest-59` 目前只计划覆盖 current-context helper 的 scope/ambient 正常合同，`NewTest-66` 只覆盖 `ShutdownModule()` 的 owned-engine/ticker 幂等；module-owned owner 在 `CollectGarbage()` 之后的 world-context 可见性与 teardown 后清零合同仍没有任何直接回归。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-12` 要求 runtime identity/owner 数据落到显式 handle/registry，而不是继续依赖 ambient/global heuristics；module-owned carrier 把 `UObject` 状态藏在 GC 不可见的 `TUniquePtr` 里，正是 owner 不可见的一种表现。
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` — `[D8] 生命周期 authority` 明确指出当前 Angelscript 的结构优势在于脚本 reachability 归 UE GC schema 或显式 retainers 管，而不是 VM 外侧的隐式 bridge state；module-owned `TUniquePtr` carrier 让 `WorldContextObject` 脱离了这条 authority。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h` L25-L31、L59-L61 — `FAngelscriptRuntimeModule` 只是 `FDefaultModuleImpl`，`OwnedPrimaryEngine` 以静态 `TUniquePtr` 持有；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` L35-L39、L162-L164 — module path 仅在 `ShutdownModule()` / `InitializeAngelscript()` 里 reset/make unique，没有任何引用收集桥；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h` L38-L40 — 对比之下 subsystem carrier 是 `UPROPERTY() FAngelscriptEngine OwnedEngine` by-value；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L118-L121、L455-L456、L613-L619 — `FAngelscriptEngine` 虽是 `USTRUCT()` 且 `WorldContextObject` 仍写成 `UPROPERTY()`，但 `TStructOpsTypeTraits` 只声明 `WithCopy = false`，没有 `WithAddStructReferencedObjects` 或等价引用收集；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L682-L689、L746-L753、L1251 — current-world helper 直接返回/写入 `WorldContextObject`，shutdown 只裸置空成员，本轮 `Core/` 检索也未命中任何 `AddReferencedObjects` / `FGCObject` 实现。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`
- [ ] **P5.25** 📦 Git 提交：`[Runtime/Core] Refactor: make module-owned engine UObject references explicit`
- [ ] **P5.25-T** 单元测试：补齐 module-owned owner 的 world-context GC 可见性与 teardown 清零合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleReferencePolicyTests.cpp`
  - 测试场景：
    - 正常路径：让 `InitializeAngelscript()` 在无 current engine 条件下创建 `OwnedPrimaryEngine`，给它绑定一个仍有外部强引用的 dummy world-context object；执行一次 `CollectGarbage()` 后，`TryGetCurrentWorldContextObject()` 仍返回该对象，证明合法 owner/validated path 不被误清空。
    - 边界条件：把同一 world-context object 的外部强引用释放后再次 `CollectGarbage()`；module-owned path 只能返回 `nullptr` 或等价 invalid result，不能继续吐出 stale pointer；随后重新绑定新的 world-context object 时，current-engine helper 与 ambient sync 会完整切到新对象。
    - 错误路径：在 `ShutdownModule()` / `ResetInitializeStateForTesting()` 之后再执行 `CollectGarbage()` 或 helper 查询，不会再看到刚才的 world-context、不会访问已释放 `OwnedPrimaryEngine`，重复 shutdown/reset 也不会把旧 `UObject` 留在 current/ambient resolver 上。
  - 测试命名：`Angelscript.TestModule.Core.RuntimeModule.WorldContextReferencePolicyIsExplicit`
  - 隔离方式：`FCoreTestContextStackGuard` + `FAngelscriptEngineScope` + `CollectGarbage(RF_NoFlags, true)`
- [ ] **P5.25-T** 📦 Git 提交：`[Test/Core] Test: cover runtime module world-context reference policy`

## 单元测试总览补充（十二）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.25` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleReferencePolicyTests.cpp` | module-owned engine world-context 在 GC 前后只暴露合法引用；shutdown/reset 后不残留旧对象 | P1 |

## 验收标准补充（十二）

32. `RuntimeModule` 自建 owner 路径上的 `WorldContextObject`、package 与 settings 不再依赖 “`USTRUCT` 字段写了 `UPROPERTY()`” 这种 carrier-sensitive 假设；弱引用与强引用的 owner policy 显式可见，GC 后 current-world helper 只能返回合法对象或 `nullptr`。
33. `ShutdownModule()` / `ResetInitializeStateForTesting()` 之后，module-owned engine 不会继续通过 current-engine / ambient world helper 暴露旧 `UObject`；`CollectGarbage()` 也不会把 module carrier 留在半有效状态。

## 风险与注意事项补充（十二）

### 风险

26. 一旦把 `WorldContextObject` 从“看似强持有”收紧成 explicit weak/collector policy，少数历史调用点若偷偷依赖 module path 顺便保活临时 `UObject`，会从“偶然可用”变成显式 `nullptr`；这些调用点必须同步补 owner 或改成真实外部持有。
27. 若强引用收集器设计过宽，把 `WorldContextObject` 与本应弱化的 transient context 一起塞进 `AddReferencedObjects`，会把当前悬空指针问题换成“runtime module 意外保活 world/object”这一类更难察觉的泄漏。

### 已知行为变化

32. `RuntimeModule` 路径下的 world-context 可见性将更严格：对象没有外部合法 owner 或显式 collector 时，GC 后 helper 会返回 `nullptr`，而不是继续给出旧指针。
33. `FAngelscriptEngine` 在不同 carrier（subsystem by-value、module `TUniquePtr`、testing clone）下的 `UObject` 生命周期语义会被统一到显式 reference policy；后续新增字段不能再仅靠 `UPROPERTY()` 修饰符默认推断“已经可被 GC 看见”。

---

## 深化 (2026-04-09 07:10:18)

本轮不重复 `Documents/Plans/Plan_TestEngineIsolation.md` 已完成的 deglobalization 背景，也不重写 `P5.4` 已经单列的 worker fallback 边界；只把当前源码仍存在、且本文件尚未单列的一条并发 ownership 缺口拆出来：`FAngelscriptEngineContextStack` 仍是进程级、无锁、跨线程共享的 `TArray`，而 subsystem owned-engine、module/runtime scope 与 pooled-context / hot reload worker 仍会同时读写或读取它。只切断 worker 的 subsystem/world fallback 还不够，只要 `Push()/Pop()/Peek()` 继续共享这块容器，current-engine 解析就仍然建立在未定义的跨线程读写上。

### Phase 5 深化：把 `ContextStack` 从跨线程共享容器降级为 game-thread scoped override lane

- [ ] **P5.26** 收口 `FAngelscriptEngineContextStack` 的线程所有权，禁止 worker 与长期 owner 继续共享同一条 `TArray`
  - `P5.4` 只解决了 “worker thread 不再走 subsystem/world fallback” 这条 resolver 语义，但当前更底层的问题还在：`ContextStack` 自己仍是进程级 `TArray<FAngelscriptEngine*>`，`Push/Pop/Peek/SnapshotAndClear()` 全部无锁；`FAngelscriptEngineScope`、subsystem 自建 owner 与 runtime/module 启停在 game thread 上改它，hot reload worker 与 pooled-context 借还又在后台线程读它。只要这个容器继续跨线程共享，就算 worker 不再回退 ambient world，current-engine 解析仍可能在 `Add/Pop/Last()` 与 `Peek()` 之间撞上未定义行为。
  - 本项不重做 `Arch-SL-12` 的完整 `runtime handle + registry`，而是把当前容器 ownership 先收硬：`ContextStack` 只允许 game-thread scoped override 使用，module/subsystem 这类长期 owner 不再依赖它做常驻注册；worker 若要解析 owner，只能拿显式 engine/handle、thread-local override 或 thread-safe snapshot，而不是继续直接读共享 `GAngelscriptEngineContextStack`。
  - 实施上先给 `FAngelscriptEngineContextStack::Push/Pop/Peek/IsEmpty/SnapshotAndClear/RestoreSnapshot` 加 thread-affinity contract，并把 worker 路径改接显式 owner；再把 subsystem/module 的“长期 owner 可见性”从 `Push()` 常驻迁回显式宿主查询或 owner token，避免同一容器同时承担 “临时 scope override” 和 “长期 runtime registry” 两种互相冲突的职责。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `A-21` 指出 `GAngelscriptEngineContextStack` 仍是无锁 `TArray`，game thread 与 worker thread 会并发共享 current-engine 容器；同文 `B-01` 说明 subsystem 仍把 `ContextStack` 当长期全局注册表，容器职责已从 scoped override 滑成 runtime ownership 真相。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-12` 明确要求把 engine identity/tick/context 解析从 ambient state 收口到显式 runtime identity，并把 `FAngelscriptEngineContextStack` 降级为 scoped override 机制，而不是继续承载长期 owner。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — `Explicit provider owner before more automation` 强调在扩展更多自动化与多 runtime 行为前，必须先把 “谁生成、谁注册、谁消费” 的 owner 真相显式化；当前 `ContextStack` 同时承担短时 override 与长期 owner 可见性，正是 runtime identity 仍不显式的直接表现。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L391-L415、L423-L433 — `Push/Pop/Peek/SnapshotAndClear/RestoreSnapshot` 当前直接读写 `GAngelscriptEngineContextStack`，没有锁、没有 thread-affinity 检查；同文件 L437-L507 — `FAngelscriptEngineScope` 构造/析构直接操作该全局栈；同文件 L718-L733、L1813-L1837、L1890-L1897 — `TryGetCurrentEngine()`、pooled-context 借还仍在后台路径读取 current engine；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L17-L29、L39-L45 — subsystem owned-engine 仍通过 `Push()/Pop()` 把长期 owner 注册到同一容器。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
- [ ] **P5.26** 📦 Git 提交：`[Runtime/Core] Refactor: make ContextStack game-thread scoped and owner visibility explicit`
- [ ] **P5.26-T** 单元测试：补齐 `ContextStack` 线程所有权与 worker 解析合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptContextStackThreadOwnershipTests.cpp`
  - 测试场景：
    - 正常路径：game thread 上嵌套 `FAngelscriptEngineScope` 仍可通过 `ContextStack` 解析 current engine；worker thread 若携带显式 owner/override，则 thread-safe resolver 只命中该 owner，不依赖 game-thread 栈内容。
    - 边界条件：真实 subsystem 自建 `OwnedEngine` 后，game-thread resolver 仍能通过显式宿主或 owner token 找回同一 runtime；后台线程在 `ContextStack` 正发生 push/pop 时不会再直接读共享栈，也不会因为 subsystem/module 的长期 owner 可见性而碰到 `Last()` / `Pop()` 竞态。
    - 错误路径：non-game-thread 直接调用 `FAngelscriptEngineContextStack::Push/Pop/Peek/SnapshotAndClear` 或等价共享栈入口时，会命中明确的 thread-affinity 诊断或失败路径，而不是静默访问共享 `TArray`；无显式 owner 的 worker 解析只返回 `nullptr` 或显式失败。
  - 测试命名：`Angelscript.TestModule.Core.ContextStack.GameThreadOwnsSharedStack`
  - 隔离方式：`FAngelscriptEngineScope` + `FCoreTestContextStackGuard` + 显式 worker-thread harness
- [ ] **P5.26-T** 📦 Git 提交：`[Test/Core] Test: cover ContextStack thread ownership and worker-safe resolution`

## 单元测试总览补充（十三）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.26` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptContextStackThreadOwnershipTests.cpp` | game-thread scoped stack、worker 显式 owner 解析、non-game-thread 共享栈拒绝访问 | P0 |

## 验收标准补充（十三）

34. `FAngelscriptEngineContextStack` 不再作为跨线程共享 current-engine 容器使用：game-thread scoped override 与 worker-thread owner 解析有明确边界，后台路径不再直接读写共享 `TArray`。
35. subsystem/module 的长期 owner 可见性不再依赖 `ContextStack::Push()` 常驻注册；`ContextStack` 回到 scoped override 语义后，多线程 current-engine 解析不会再建立在 `Add/Last/Pop` 的竞态上。

## 风险与注意事项补充（十三）

### 风险

28. 如果只给 `ContextStack` 补锁而不拆 “临时 scope override” 与 “长期 owner 可见性” 两种职责，会把当前 data race 修成更难发现的锁顺序/死锁问题，且 worker 仍会继续读到错误的 runtime identity。
29. subsystem/module 从 `Push()` 常驻迁出后，少数历史 helper 若仍把 “栈里有 engine” 当成 owner discoverability 前提，短期会从“偶然可用”变成显式 `nullptr`；执行时必须同步补宿主查询或 owner token。

### 已知行为变化

34. `ContextStack` 今后只表达 scoped current-engine override，不再承担 subsystem/module 的长期 owner 注册；离开 scope 后还能否重新解析 runtime，将由宿主或 owner token 合同而不是全局栈残留决定。
35. worker thread 上没有显式 owner 时，current-engine 相关 helper 会更早返回 `nullptr` 或诊断失败；这属于刻意收紧，不再允许后台路径“借用” game-thread 共享栈碰巧解析到某台 engine。

---

## 深化 (2026-04-09 07:18:35)

本轮未在仓库中读取到 `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md`；以下深化仅基于当前实际存在的 `[A]`、`[D]`、`[E]` 输入与 `Core/` 源码复核追加，不补写不存在的 `[B]` 引用。

本轮不重复 `P2.1` 的 watcher 同步、`P5.12` 的 optimized-call 线程门禁或 `P5.15` 的 debug-value 清理；只补当前文档尚未单列的一条 reload correctness 缺口：hot reload / swap-in 仍默认把“free pool 已清”当成“旧 generation 已经安全退场”，并且 compile 成功后只有无参 `PostCompile` 广播，没有 module-level retire/commit observer。只要执行层仍允许 `PushState()` 复用 active context，这个假设就不成立。

### Phase 5 深化：把 old-module reload 从“立即 discard”收口成可观测的 retire barrier

- [ ] **P5.27** 为 old-module reload 增加 in-flight execution retire barrier，并补 module-level `PrepareRetire/Retired/ReloadCommitted` observer
  - 当前 `CompileModules()` 在 `bShouldSwapInModules` 成立后，会直接遍历 `DiscardedModules` 调 `Engine->DiscardModule(...)`，随后立刻 `DeleteDiscardedModules()`；同一实现末尾只广播无参 `FAngelscriptRuntimeModule::GetPostCompile()`。与此同时，执行层只要发现当前线程已有 `asEXECUTION_ACTIVE` 且 engine 匹配，就会直接 `PushState()` 复用 active context。也就是说，RuntimeCore 现在既没有“旧 generation 仍有 in-flight frame”这条显式账本，也没有告诉外部“哪一个模块刚进入 retire/commit”的结构化事件，reload correctness 仍依赖隐式时序。
  - 本项不重做 `D4` 热重载哲学，也不替换现有 `ReloadRequirement`；目标是把最危险的生命周期空洞补上：old module 先进入 `Retiring` / `WaitingForInFlight`，只有 barrier 确认无活动 lease 后才真正 `DiscardModule()` / `DeleteDiscardedModules()`；同时 runtime module 暴露最小 module-level observer，把 `ModuleName`、`GenerationId`、`Reason`、`bWillBeReplaced` 和 `Result` 带给 `HotReloadTestRunner`、debug/cache owner 与后续 teardown consumer，而不是继续只给一个无参 `PostCompile`。
  - 实施上先在 `Core/AngelscriptEngine.h/.cpp` 引入轻量 `FAngelscriptModuleExecutionLease` / `FAngelscriptRetireBarrier` / `FAngelscriptModuleReloadReport`，把 `FAngelscriptPooledContextBase::Init()` 的 nested `PushState()` 和 compile swap-in 尾段接入同一条 generation 账本；再把 `DiscardModule()` 从“立即清空一切”改成“barrier 已通过时的最终物理回收”；最后在 `Core/AngelscriptRuntimeModule.h/.cpp` 增加 module-level retire/commit delegate，替代当前只有 compile-global 广播的状态。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — 现有分析已确认 `DiscardModule()` 是 runtime 状态与 `FileHotReloadState` 的破坏性清理中心，同时 debug-value 清理也只能被动寄望于 `DiscardModule()` / engine teardown，说明当前 reload 缺少统一的 retire phase 与可扩展清理挂点。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-57` 明确指出执行层允许 `PushState()` 复用 active context，但 swap-in 后仍立即 `DiscardModule/DeleteDiscardedModules()`；`Arch-SL-52` / `Arch-SL-56` 同时指出 runtime 只有 compile-global delegate，没有 module-level lifecycle / deactivation contract。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — D4 补充指出当前缺的是 `execution epoch` 与 `ReloadComplete/Rebind` observer，而不是重写 hot reload 框架；`CrossComparison.md` 中 puerts 的 `NotifyRebind` 对比也说明 reload 完成后需要显式 consumer contract，而不是让旧 generation 在静默 discard 后直接消失。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1026-L1036 — `DiscardModule()` 进入后只先释放 free pool，没有任何 active/in-flight barrier；L1797-L1806 — 当前线程已有活动 context 时会直接 `PushState()` 复用执行帧；L4015-L4025 — swap-in 尾段会立刻对旧模块执行 `Engine->DiscardModule()` 和 `DeleteDiscardedModules()`；L4138-L4140 — compile 成功后只有无参 `PostCompile.Broadcast()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h` L14-L18、L37-L42 — 对外只有 compile-global / class-collection 级 delegate，没有 module retire/commit observer。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
- [ ] **P5.27** 📦 Git 提交：`[Runtime/Core] Refactor: defer old-module discard behind retire barrier`
- [ ] **P5.27-T** 单元测试：补齐 reload retire barrier 与 module-level observer 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptModuleRetireBarrierTests.cpp`
  - 测试场景：
    - 正常路径：编译 `Game.Reload.Target` 第一代模块，保持一次可控脚本执行处于 active/nested 状态时触发同模块 reload；断言新 generation 已成为 current module，但旧 generation 在执行退出前不会被 `DiscardModule()`，并且会按顺序收到 `PrepareRetire -> ReloadCommitted -> Retired` 事件。
    - 边界条件：同一 generation 发生二层 `PushState()` 嵌套执行时，第一次 drain 只把旧 generation 保持在 `WaitingForInFlight`，直到最内层退出后才真正 retire；期间 `GetModule()` / active lookup 始终只暴露新 generation。
    - 错误路径：reload 结果为 `Error` / `ErrorNeedFullReload`，或旧 generation 仍未满足 barrier 条件时，不得误发 `ReloadCommitted` / `Retired` 成功事件，也不能把 current module 切到半清理状态。
  - 测试命名：`Angelscript.TestModule.HotReload.ExecutionLease.ReloadDefersDiscardUntilFramesRetire`
  - 隔离方式：`FAngelscriptEngineScope` + clean full engine fixture + 显式 hot-reload harness
- [ ] **P5.27-T** 📦 Git 提交：`[Test/HotReload] Test: cover reload retire barrier and observer contract`

## 单元测试总览补充（十四）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.27` | `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptModuleRetireBarrierTests.cpp` | reload 时旧 generation 延迟退场、nested `PushState()` barrier、module observer 顺序与失败分支 | P0 |

## 验收标准补充（十四）

36. hot reload / swap-in 不再在存在 in-flight execution 的同一 tick 内立即 `DiscardModule()` 旧 generation；只有 retire barrier 确认无活动 lease 后，旧模块才会真正物理回收。
37. RuntimeCore 对外暴露最小 module-level retire/commit observer，consumer 能拿到 `ModuleName` / `GenerationId` / `Reason` / `Result`；现有无参 `PostCompile` 仅保留为兼容 facade，不再是唯一 lifecycle 信号。

## 风险与注意事项补充（十四）

### 风险

30. retire barrier 如果只跟踪 free pool 而不把 nested `PushState()` / active context reuse 纳入 lease 统计，会把当前“立即 discard”问题换成“永远等待不到零活动帧”的假 barrier，导致旧 generation 无法退场。
31. 若 module-level observer 在 destructive cleanup 之后才广播，consumer 看到的将只剩空 `ScriptModule` / 空 `UASFunction`；事件时序必须发生在 `DiscardModule()` 之前，否则 contract 本身没有诊断价值。

### 已知行为变化

36. reload 成功后，旧 module generation 可能短暂处于 `Retiring` / `WaitingForInFlight`，而不是像今天一样在同一提交尾部立刻消失；这属于刻意引入的显式生命周期窗口。
37. 外部 tooling / debug / hot-reload consumer 后续应优先订阅 module-level observer，而不是继续把无参 `PostCompile` 当成“所有模块都已完全换代”的唯一信号。

---

## 深化 (2026-04-09 07:25:38)

本轮不重写 `P1.2` 的 world-context validated resolve 主线，也不重复 `P5.1` 的通用初始化事务或 `P5.6` 的 stable-host 重发现；只补当前源码仍存在、但现有 Plan 尚未单列的一条 subsystem startup seam：`UAngelscriptGameInstanceSubsystem::Initialize()` 仍会在宿主 world-context 尚未沉淀、owner 是否真正 ready 尚未确认前，先提交 `bInitialized`、`PrimaryEngine` 与 `ActiveTickOwners`。这让真实 subsystem 启动继续停留在“先占位、后碰运气补 world”的隐式合同上。

### Phase 5 深化：把 subsystem startup 从“先发布 owner”收口成 host-aware commit

- [ ] **P5.28** 把 `UAngelscriptGameInstanceSubsystem::Initialize()` 改成 `capture host -> initialize/adopt engine -> commit world-context/tick owner` 的原子流程
  - `P1.2` 已经把“真实 subsystem 初始化要显式建立 world-context”列为方向，`P5.6` 也开始补 adopted subsystem 的稳定宿主解析；但当前 subsystem 启动入口本身仍没有独立 commit seam：一进函数就先写 `bInitialized = true`，随后要么直接 adopt ambient current engine，要么 `Push(&OwnedEngine)` 后立即执行 `OwnedEngine.Initialize()`，最后无条件递增 `ActiveTickOwners`。与此同时，这条路径没有沉淀 `GetGameInstance()/GetWorld()` 对应的 host world-context，也没有在失败/退出时回滚这些已发布状态。
  - 本项不重做完整 runtime handle/registry，只先把 subsystem 入口修到自洽：初始化前先捕获宿主 `UGameInstance`/`UWorld`，用它建立或验证 owner engine 的初始 world-context；只有当 owner engine ready、host 有效、tick claim 可提交时，才发布 `PrimaryEngine`、`bOwnsPrimaryEngine`、`bInitialized` 与 `ActiveTickOwners`。若 adopt/owned path 任一阶段失败或宿主无效，则必须 detach/rollback，不允许留下半初始化 subsystem 或空 world-context 的“假 ready” owner。
  - 实施上先给 `Initialize()` 引入局部 candidate/transaction state，拆开“拿到 engine 指针”和“把 subsystem 宣布为已初始化”两个阶段；再把 owned path 的 `Push()/Initialize()` 包进 scoped rollback，确保失败时自动清掉 `ContextStack` 与 tick-owner 预占位；最后把真实测试夹具从 `CreateInjectedSubsystem()` 迁回 `CreateSubsystemWorld()`，让 world-context、host 解析和 rollback 都走生产入口，而不是继续靠 friend access 伪造 `bInitialized/PrimaryEngine/ActiveTickOwners`。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `B-18` 指出真实 subsystem 初始化后并不会建立基础 world-context；`D-03` 指出当前自动化大量绕过真实 `Initialize()`；`D-15` 进一步说明仓库里没有任何一条回归直接验证“真实 subsystem 初始化后 world-sensitive API 已可用”。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `NewTest-10` 已明确要求把 `Subsystem/` 从“编译失败 smoke / injected helper”升级为真实 `GameInstanceSubsystem` 生命周期场景；`NewTest-59` 也说明 current-context 静态 helper 仍缺真实 world-context 入口保护。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-12` 明确要求 runtime identity 至少围绕 `OwningGameInstance`、`TickOwnerCount` 与 `WorldContextObject` 建模，而不是继续依赖 ambient state + 进程级 tick owner。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L16-L28、L101-L113 — `Initialize()` 当前一进入就置 `bInitialized = true`，随后 adopt/owned 两条路径都只处理 engine 与 tick owner，`GetCurrent()` 仍只靠 `GetAmbientWorldContext()` 反查 `World -> GameInstance -> Subsystem`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L682-L684、L718-L733、L746-L753 — `TryGetCurrentWorldContextObject()` 只要 current engine 存在就优先返回其 `WorldContextObject`，`TryGetCurrentEngine()` 在栈空时仍回退到 `GetCurrent()`，而 `AssignWorldContext()` 仍只会把 world-context 写回“当前解析到的 engine”，不会在 subsystem startup 中自动沉淀宿主 world。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`
- [ ] **P5.28** 📦 Git 提交：`[Runtime/Subsystem] Fix: commit subsystem startup only after host world-context is ready`
- [ ] **P5.28-T** 单元测试：补齐真实 subsystem startup 的 host/world-context commit 与 rollback 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemInitializationContractTests.cpp`
  - 测试场景：
    - 正常路径：通过真实 `UWorld -> UGameInstance -> GetSubsystem<UAngelscriptGameInstanceSubsystem>()` 触发启动后，不借助额外 `FAngelscriptEngineScope` 或 injected helper，`GetEngine()`、`TryGetCurrentWorldContextObject()`、`GetCurrent()` 与依赖 world 的 subsystem helper 都能立即命中同一宿主 world。
    - 边界条件：当前进程里先存在一台已初始化 engine，再让真实 subsystem 走 adopt path；只有在宿主 world-context 验证通过后才会提交 tick owner，且 `ActiveTickOwners` 只增加一次，不会因重复进入或临时 scope 嵌套产生额外 owner。
    - 错误路径：构造“宿主 world 无效 / 初始化中断 / owned path 需要 rollback”的场景后，`bInitialized`、`PrimaryEngine`、`ActiveTickOwners` 与 `ContextStack` 都保持进入前基线；修正输入后再次初始化可以成功，不会被上一轮半初始化状态卡死。
  - 测试命名：`Angelscript.TestModule.Subsystem.Initialization.HostWorldContextCommitsAtomically`
  - 隔离方式：`CreateSubsystemWorld()` + `FAngelscriptEngineScope`（仅用于构造 adopt 边界）+ `FCoreTestContextStackGuard`
- [ ] **P5.28-T** 📦 Git 提交：`[Test/Subsystem] Test: cover real subsystem startup commit and rollback`

## 单元测试总览补充（十五）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.28` | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemInitializationContractTests.cpp` | 真实 subsystem 启动后的 host world-context 建立、adopt path 单次提交、失败回滚不污染 `ContextStack/ActiveTickOwners` | P1 |

## 验收标准补充（十五）

38. 真实 `GameInstanceSubsystem` 启动不再把“开始初始化”误报成“已经 ready”：只有宿主 world-context、owner engine 与 tick owner 全部提交成功后，`bInitialized`、`PrimaryEngine` 与 `ActiveTickOwners` 才会对外可见。
39. 不借助 injected helper 或额外 world scope 的真实 subsystem 生命周期场景下，`TryGetCurrentWorldContextObject()`、`UAngelscriptGameInstanceSubsystem::GetCurrent()` 与依赖 world 的基础运行时入口都能在启动后立即解析到宿主 world；失败路径不会残留半初始化 subsystem 状态。

## 风险与注意事项补充（十五）

### 风险

32. 如果只把 `bInitialized` 的写入时机后移，却不把 `PrimaryEngine`、`ActiveTickOwners` 与 `ContextStack` 一起纳入同一事务，最终只会把今天的“半初始化 owner”问题拆成更多隐式中间态，测试反而更难稳定复现。
33. 真实 `CreateSubsystemWorld()` 场景一旦取代 injected helper 成为主回归入口，部分依赖 friend access 直接改内部字段的老测试会集中转红；执行时需要同步区分“保留低层 access shim”与“继续用假路径证明生产合同”这两种完全不同的测试目的。

### 已知行为变化

38. subsystem 在宿主 world/context 尚未 ready 时，会更早返回未初始化或失败结果，而不是像今天一样先暴露 `bInitialized/PrimaryEngine` 再等待后续路径碰巧补 world-context。
39. `Subsystem/` 主题测试后续将更多依赖真实 `CreateSubsystemWorld()` / `GetSubsystem<>()` 生命周期夹具；继续直接写 `PrimaryEngine`、`bInitialized`、`ActiveTickOwners` 的 injected helper 只应保留给低层故障注入，不再作为主合同证明路径。

---

## 深化 (2026-04-09 07:33:01)

本轮继续未在仓库中读取到 `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md`；以下深化继续只引用 `[A]`、`[C]`、`[D]`。

本轮不重写 `P3.1` 的 lifecycle milestone、`P4.3` 的 adopted-engine lifetime token、也不重复 `P5.7` 的唯一 tick owner 主线；只把三者共同遗漏的一条判定层 seam 单列出来：`FAngelscriptEngine::ShouldTick()` 仍把“共享到底层 `asIScriptEngine` 的 wrapper”与“真正能驱动 hot reload/debug/coverage/world-context 的 runtime owner”混为一谈。只要 subsystem 或 fallback lane 采纳的是 clone / bootstrap-only wrapper，当前代码就会在看似合法的 `PrimaryEngine` 上静默丢失完整 runtime service。

### Phase 5 深化：把 tick 资格从 `Engine != nullptr` 收口成显式 runtime capability

- [ ] **P5.29** 收口 `ShouldTick()` / subsystem primary-engine claim 的 runtime capability 合同，禁止 clone 与 bootstrap wrapper 被当成可驱动主 runtime
  - `P3.1` 已经准备把 lifecycle state 从单一 `bIsInitialCompileFinished` 抽出来，`P4.3` 也在收 adopt engine 的 lifetime token，`P5.7` 则处理唯一 tick owner；但当前还缺一个单独的判定层：`ShouldTick()` 只看 `Engine != nullptr`，subsystem `Initialize()` 只要拿到任意 current engine 就递增 `ActiveTickOwners` 并在 `Tick()` 中直接推进。对 clone / bootstrap-only wrapper 来说，这会把“能共享底层 VM”误报成“具备完整 runtime service”。
  - 本项不重做整个 runtime handle 体系，也不和 `P4.3` 重复 lifetime token 细节；目标是先把 “wrapper existence != runtime tick capability” 变成正式合同：引入 `CanTickRuntime()` / `HasTickCapability()` 之类显式判定，至少综合 `CreationMode`、owner 角色、shared-state/readiness、必要服务就绪度；`UAngelscriptGameInstanceSubsystem::Initialize()`、`Tick()` 和 runtime-module fallback lane 只对具备 capability 的 owner 建 tick claim，其余 wrapper 一律降为 observer 或直接拒绝 adopt 为 primary tick driver。
  - 实施上先在 `Core/AngelscriptEngine.h/.cpp` 为 clone / bootstrapped / compiled runtime 定义明确 tick capability；再把 `UAngelscriptGameInstanceSubsystem::Initialize()` 从“有 current engine 就 claim owner”改成“candidate 验证通过后才提交 `PrimaryEngine`/`ActiveTickOwners`”；最后清理测试里依赖 `PrepareTickProbe()` 或手工改 `bScriptDevelopmentMode` / `bUseHotReloadCheckerThread` 让 clone 假装可 tick 的路径，改成显式证明“不能 tick clone，但 fallback/real owner 仍能继续前进”。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `B-15` 明确指出 `ShouldTick()` 只检查 `Engine != nullptr`，clone wrapper 会被误判为可推进 primary engine；`B-16` 进一步说明 clone wrapper 不投影 source 的 world/editor/debug 状态，current-context helper 在 clone scope 下系统性读错。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `NewTest-10` 只要求真实 subsystem adopt/own/tick 正向场景，当前还没有任何一条回归直接证明 clone/bootstrap wrapper 不能 claim primary tick；`NewTest-65` 也只覆盖 fallback tick 的全局 owner 门控，没有覆盖 wrapper capability 判定。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-50` 明确指出 `ShouldTick()` 只靠 `Engine != nullptr` 无法表达 `Bootstrapped / ScriptsCompiled / RuntimeReady` 的差别，建议把 `bCanTick`/`CanTickRuntime()` 提升为 lifecycle state 的正式字段；`Arch-SL-12` 同时要求 tick owner 围绕显式 runtime identity，而不是围绕 ambient current engine。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L628-L647 — `CreateCloneFrom()` 当前显式创建 `CreationMode = Clone` 且 `bOwnsEngine = false` 的 wrapper；同文件 L2843-L2856 — `ShouldTick()` 仍只返回 `Engine != nullptr`，`AdoptSharedStateFrom()` 只复制 `Engine/ConfigSettings/package/root-path/compile flags`，没有任何 tick capability 或 runtime-service 判定；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L16-L29、L81-L85 — subsystem 初始化拿到任意 `TryGetCurrentEngine()` 就立即提交 `PrimaryEngine` 并递增 `ActiveTickOwners`，后续 `Tick()` 只看 `PrimaryEngine->ShouldTick()` 就直接推进。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`
- [ ] **P5.29** 📦 Git 提交：`[Runtime/Core] Fix: gate primary ticking on explicit runtime capability`
- [ ] **P5.29-T** 单元测试：补齐 clone/bootstrap wrapper 的 tick capability 与 fallback handoff 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemTickCapabilityTests.cpp`
  - 测试场景：
    - 正常路径：真实 subsystem adopt 一个 full/compiled owner 或自建 owned engine 后，只有具备 runtime capability 的 owner 才会 claim `ActiveTickOwners` 并在 `Tick()` 中推进 runtime。
    - 边界条件：在外层 scope 中提供 clone wrapper 作为 current engine 时，subsystem 可以解析到该 wrapper，但不会把它当成 primary tick driver；editor fallback lane 仍能在无真实 world owner 时接管可 tick 的 full owner。
    - 错误路径：bootstrap-only/testing wrapper 或手工只补 `bScriptDevelopmentMode` / `bUseHotReloadCheckerThread` 的 clone probe 都不能再骗过 `ShouldTick()`；不会出现 `ActiveTickOwners` 已占用、但 hot reload/debug/coverage 实际没被驱动的 silent degradation。
  - 测试命名：`Angelscript.TestModule.Subsystem.TickCapability.CloneOrBootstrapWrapperCannotDrivePrimaryRuntime`
  - 隔离方式：`CreateSubsystemWorld()` + `FAngelscriptEngineScope` + `FCoreTestContextStackGuard` + runtime-module fallback tick access shim
- [ ] **P5.29-T** 📦 Git 提交：`[Test/Subsystem] Test: cover primary tick capability and fallback handoff`

## 单元测试总览补充（十六）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.29` | `Plugins/Angelscript/Source/AngelscriptTest/Subsystem/AngelscriptSubsystemTickCapabilityTests.cpp` | full owner 正向 tick、clone/bootstrap wrapper 拒绝 claim、fallback handoff 仍可推进真正 runtime | P1 |

## 验收标准补充（十六）

40. `FAngelscriptEngine::ShouldTick()` 不再把“底层 `asIScriptEngine` 指针存在”误当成 runtime 可推进条件；clone、bootstrap-only 或缺少必要服务的 wrapper 不能再被 subsystem / fallback lane 当成 primary tick driver。
41. 当 subsystem 或 runtime-module 遇到“不具备 tick capability 的 wrapper”时，要么拒绝 claim `ActiveTickOwners`，要么明确降级为 observer；不能再出现 owner 已被占用、但 hot reload/debug/coverage/world-context 实际未被驱动的静默退化。

## 风险与注意事项补充（十六）

### 风险

34. 如果直接把 `ShouldTick()` 绑定到旧的 `bIsInitialCompileFinished`，会把当前 testing/bootstrap 路径里的历史语义再次硬编码进新 gate；实现时必须优先落显式 capability，而不是继续复用失真的 legacy bool。
35. subsystem adopt/fallback handoff 一旦开始区分 capability，现有靠 `PrepareTickProbe()`、手工改 clone 字段或人工占用 `ActiveTickOwners` 的测试夹具会集中转红；需要同步把这些测试改成“显式证明不能 tick clone，而不是硬塞 clone 进正向路径”。

### 已知行为变化

40. clone wrapper 与 bootstrap-only/testing wrapper 之后即使能共享底层 VM、能被 current-engine resolver 解析出来，也不再默认具备 `Tick()` 能力；只有显式 full/runtime-ready owner 才能驱动主 runtime 前进。
41. editor fallback tick 与 subsystem owner 之间的交接会更严格依赖 runtime capability，而不是只看“有没有某个 engine 指针”或“全局 `ActiveTickOwners` 是否非零”；少数历史 helper 若隐式依赖 clone 也会被 tick，需要改成显式 full owner fixture。

---

## 深化 (2026-04-09 07:43:56)

本轮继续未在仓库中读取到 `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md`；以下深化仅引用当前实际存在的 `[A]`、`[C]`、`[D]`、`[E]` 输入，并只基于 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 的实际源码追加。

本轮不重写 `P2.4` 的 startup ready window、`P5.10` 的 threaded startup 亲和性，或 `P5.28` 的真实 subsystem 启动事务；只补两条当前源码仍存在、但现有 Plan 尚未单列成执行项的 RuntimeCore seam：其一，`IsInitialized()` 仍把“能解析到 subsystem 容器对象”误报成 runtime ready；其二，startup bind provider 仍是“裸字符串名单 + fire-and-forget `LoadModule()`”，缺少显式失败语义与 bind-surface 合同。

### Phase 5 深化：把 runtime ready gate 与 startup bind provider 合同收硬

- [ ] **P5.30** 把 `IsInitialized()` 从“subsystem 容器存在”收口成“存在可用 owner engine”的 readiness gate
  - `P2.4` 已经开始收紧“初始化中 wrapper 不得冒充 ready”，`P5.28` 也在把真实 subsystem 启动改成原子提交；但当前还留着一条更细的假阳性 seam：`FAngelscriptEngine::IsInitialized()` 只要 `ContextStack` 非空，或者 `UAngelscriptGameInstanceSubsystem::GetCurrent()` 能返回一个 subsystem 对象，就直接返回 `true`。这会把“subsystem 还活着，但 `PrimaryEngine` 已清空、`bInitialized == false`”误报成 runtime 已就绪。
  - 本项不重做完整 runtime handle/registry，只先把现有 readiness gate 收到自洽：`IsInitialized()`、`TryGetCurrentEngine()` 的 subsystem 分支，以及依赖它们的 runtime-init helper 都只能认“subsystem 持有一个真实可用 owner engine”，不能再把容器对象存在性当成 ready 事实。
  - 实施上先在 `UAngelscriptGameInstanceSubsystem` 引入显式 `HasReadyPrimaryEngine()` 或等价 helper，统一复用 `bInitialized && PrimaryEngine != nullptr` 这条真实语义；再把 `FAngelscriptEngine::IsInitialized()` 与相关 init gate 改为读该 helper，而不是直接把 `GetCurrent() != nullptr` 当成初始化完成；最后补一组“deinitialize 后仍能解析到 subsystem shell”的回归，锁住“可以重新初始化，但不会误报已就绪”这条合同。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `C-05` 指出 `IsInitialized()` 当前把“subsystem 对象存在”误报成“engine 已初始化”，会让后续 runtime-init helper 跳过真正初始化。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `NewTest-10` 只要求真实 subsystem adopt/own/tick/deinitialize 正向合同，`Issue-40` 也指出当前 `CreateDestroy` 类用例没有守住核心 lifecycle gate；现有测试没有直接覆盖“deinitialized subsystem shell 不应算 ready”。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-12` / `Arch-SL-50` / `Arch-SL-51` 都要求 runtime identity 与 ready milestone围绕显式 owner/ready phase，而不是围绕 ambient object 或 legacy bool。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L676-L679 — `IsInitialized()` 当前直接用 `ContextStack::Peek()` 或 `UAngelscriptGameInstanceSubsystem::GetCurrent() != nullptr` 判定 ready；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp` L32-L50、L66-L68、L94-L113 — `Deinitialize()` 会把 `PrimaryEngine = nullptr`、`bInitialized = false`，`IsAllowedToTick()` 也明确要求两者同时成立，但 `GetCurrent()` 仍只按 ambient world 找到 subsystem 并返回对象本身。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.h`
- [ ] **P5.30** 📦 Git 提交：`[Runtime/Core] Fix: require a ready owner engine for runtime initialization gates`
- [ ] **P5.30-T** 单元测试：补齐 deinitialized subsystem shell 不应算 runtime ready 的 gate 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeInitializationGuardTests.cpp`
  - 测试场景：
    - 正常路径：真实 subsystem 成功 adopt/own 一台 ready engine 后，`IsInitialized()` 与相关 init gate 返回 `true`，不会重复初始化或错误创建新 owner。
    - 边界条件：调用 `Deinitialize()` 后 subsystem 对象仍可经 ambient/world 路径找到，但此时 `IsInitialized()` 必须返回 `false`；修正输入后再次初始化可以成功，不会被上一轮 shell 假阳性卡死。
    - 错误路径：ambient world 能解析到 subsystem，但其 `bInitialized == false` 或 `PrimaryEngine == nullptr` 时，runtime gate 只会报告未 ready，不会继续把 `GetCurrent()` 命中的容器对象误判为 current engine。
  - 测试命名：`Angelscript.TestModule.Core.RuntimeInitialization.DeinitializedSubsystemShellDoesNotCountAsReady`
  - 隔离方式：`CreateSubsystemWorld()` + `FCoreTestContextStackGuard`
- [ ] **P5.30-T** 📦 Git 提交：`[Test/Core] Test: cover runtime initialization gate against deinitialized subsystem shells`

- [ ] **P5.31** 把 startup bind provider 从“裸名单 + 直接 `LoadModule()`”收口成可诊断的 preload transaction
  - `P5.10` 已经把 threaded startup 里 package / bind / delegate 的 `GameThread` 提交边界列成主线，但当前还有一条更底层的 correctness seam 没有单列：runtime 仍把 `BindModules.Cache` 当原始字符串数组读回，再逐个 `FModuleManager::Get().LoadModule(...)`，随后立刻 `BindScriptTypes()`；`LoadModule()` 的失败结果既不回传、也不形成显式 startup report。
  - 这会让 startup bind surface 落在一种危险的“自洽但不完整”状态：只要模块加载被线程亲和性、缺失模块或名单漂移静默拒绝，后面的 registry/self-consistency smoke 仍可能绿灯，因为它们看到的只是“当前已经注册进去的东西彼此一致”，而不是“应该存在的 provider 都已成功 preload 并参与 bind pass”。
  - 本项不直接跳到 `Arch-SL-34` 的 provider-lazy architecture，只先把现有 eager/all-providers 路线收成显式事务：读取名单、规范化 provider、在 `GameThread` 上 preload、产出成功/失败报告，然后只有在报告满足 contract 时才执行 `BindScriptTypes()`；失败分支必须在 bind pass 前就给出明确 startup 结果，而不是继续靠日志与后续 smoke 猜测。
  - 实施上先给当前 raw string list 包一层最小 `BindProviderPreloadReport`，至少记录 `Requested/Loaded/Failed` provider 集；再把 `LoadModule()` 改成带结果检查与显式聚合的 preload helper，禁止直接 fire-and-forget；最后补一组 startup bind regression，锁住“provider 全部 preload 成功后才开始 bind pass”和“失败时不留下半注册 registry”这两条合同，为后续 manifest/source/scope 演进留出落点。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `C-13` 指出非 Editor 默认 threaded 初始化仍会在 worker path 上 `LoadModule()`，并且当前实现没有检查返回值或显式重试/失败语义。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `Issue-22` / `Issue-29` 指出 `Startup.Full` / `CreateForTestingFallbackFull` / `StartupBindRegistrySmoke` 只看 artifact 或 registry 自洽，抓不住关键 bind provider 缺失、错误线程提交或启动 bind surface 不完整。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-34` 明确指出当前 startup 会先整批读 `BindModules.Cache`、`LoadModule()`、再 `BindScriptTypes()`，建议把 provider 发现/登记与真正激活拆层。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — `BindModules.Cache` 仍只是裸字符串数组，runtime 读回后只能 `LoadModule(ModuleName)`，没有 source/scope/phase，也没有显式 preload 合同。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1477-L1495 — runtime 当前先 `LoadBindModules(...)`，再遍历 `GetBindModuleNames()` 直接 `LoadModule(...)`，随后无条件 `BindScriptTypes()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` L594-L602 — `LoadBindModules()` 当前只把模块名字符串数组读入内存，没有 phase/source/requiredness 等结构化信息。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`
- [ ] **P5.31** 📦 Git 提交：`[Runtime/Core] Fix: make startup bind-provider preload explicit and fail-fast`
- [ ] **P5.31-T** 单元测试：补齐 startup bind provider preload 与 bind-surface 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupBindProviderContractTests.cpp`
  - 测试场景：
    - 正常路径：full startup 先完成 provider preload，再执行一次 startup bind pass；测试能同时看到非空 `LoadedProviders` 与一次性 `ExecutedBindNames`，证明 bind surface 来自显式 preload 成功结果。
    - 边界条件：provider 已经处于已加载状态或名单里存在重复/空项时，preload report 会去重并保持 bind pass 只执行一次，不会因为重复输入多次重放 startup bind。
    - 错误路径：名单里的 provider 缺失、加载失败或线程亲和性不满足时，startup 会给出显式失败/降级结果并阻止 `BindScriptTypes()` 留下半注册 registry；`StartupBindRegistrySmoke` 一类自洽断言不再能掩盖关键 bind surface 缺失。
  - 测试命名：`Angelscript.TestModule.Core.Startup.BindProvidersPreloadExplicitlyBeforeBindPass`
  - 隔离方式：`FAngelscriptBindExecutionObservation` + isolated full-startup fixture
- [ ] **P5.31-T** 📦 Git 提交：`[Test/Core] Test: cover startup bind-provider preload and failure signaling`

## 单元测试总览补充（十七）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.30` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeInitializationGuardTests.cpp` | subsystem shell 存活但 `PrimaryEngine` 已清空时不误报 ready、可重新初始化、ambient world 不再造成假阳性 | P1 |
| `P5.31` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptStartupBindProviderContractTests.cpp` | provider preload 成功后再执行 bind pass、重复输入不重复重放、加载失败不留半注册 bind surface | P1 |

## 验收标准补充（十七）

42. `FAngelscriptEngine::IsInitialized()` 不再把“能解析到 subsystem 对象”误当成 runtime ready；只有存在真实可用 owner engine 时，runtime-init gate 才会返回已初始化。
43. startup bind surface 具备显式 preload contract：provider 全部完成 preload 并通过结果校验后才允许执行 `BindScriptTypes()`；provider 缺失或加载失败不会再被 registry 自洽或性能 artifact 静默掩盖。

## 风险与注意事项补充（十七）

### 风险

36. 若只修改 `IsInitialized()` 而不把 subsystem readiness helper、ambient fallback 与重新初始化入口统一到同一判定源，最终会把今天的假阳性拆成更多“有的入口能重试、有的入口仍被挡住”的分叉合同。
37. startup bind provider 一旦从 fire-and-forget 改成显式 preload report，部分历史测试与工具会从“日志里偶尔能看出来”升级成“立即 fail-fast”；执行时必须同步区分“真的缺 provider”与“名单 schema 本身需要补字段”这两类错误。

### 已知行为变化

42. deinitialized 的 subsystem shell 之后即使仍能通过 ambient/world 解析到对象，也不会再让 runtime gate 报告“已初始化”；需要 runtime 的调用方必须先重新建立 ready owner。
43. startup 若遇到 bind provider 缺失、重复输入或加载失败，会更早暴露显式 preload 结果，而不是继续进入 `BindScriptTypes()` 后再靠后续 smoke、日志或空 registry 自洽去猜问题。

---

## 深化 (2026-04-09 07:53:58)

本轮继续未在仓库中读取到 `Documents/AutoPlans/DiscoveryPlans/RuntimeCore_Plan.md`；以下新增只引用当前实际存在的 `[C]`、`[D]`、`[E]` 输入，并基于 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 的实际源码复核后追加。

本轮不重复 `P5.31` 的 preload transaction，也不与 `Documents/Plans/Plan_OpportunityIndex.md` 中 `Plan_BindShardConsolidation` 的结构性收口撞车；只补当前 runtime startup contract 还没单列的一条 seam：`BindModules.Cache` 仍和 `Binds.Cache` / `PrecompiledScript*.Cache` 走不同 authority，且 artifact schema 仍是裸模块名数组，导致启动诊断、兼容 fallback 与后续 provider 分层都没有统一账本。

### Phase 5 深化：把 bind-module artifact 从隐式路径 + raw list 收口成 single-authority manifest contract

- [ ] **P5.32** 统一 bind-module artifact 的 authority，并把 raw `BindModules.Cache` 升级为 dual-read 的 structured manifest
  - `P5.31` 已开始把 startup bind provider 从 fire-and-forget load 收成 preload transaction，但它默认拿到的仍只是“某个路径读回的一串模块名”。当前 runtime 其余 script artifacts 都以 `GetScriptRootDirectory()` 为 authority，只有 bind-module 清单仍绕过这条根并直接去 plugin base dir 读取；再叠加 `LoadFileToStringArray()` 的 raw schema，startup 即使 fail-fast 也说不清“读的是哪份清单、来自哪个 producer、是否属于 runtime/editor/legacy fallback”。
  - 本项不是去停用 `BindModules.Cache`，也不提前做 `Plan_BindShardConsolidation` 的 shard 合并；目标只是在保持现有 bind shard 主线可运行的前提下，把“读哪份 bind-module 清单、怎么兼容旧格式、清单里最少要有哪些字段”收口成 runtime 可诊断 contract。
  - 实施上先在 `Core/` 新增 `ResolveBindModuleManifest()` / `LoadBindModuleManifest()` 或等价 helper，让 runtime 对 bind-module artifact 采用与 `Binds.Cache` / `PrecompiledScript*.Cache` 一致的 authority；manifest 首版至少记录 `ModuleName`、`SourceKind`、`Scope`、`bRequired`、`ManifestVersion`、`GeneratedAtUtc`，并在无 manifest 时 dual-read legacy raw list。完成后再让 `P5.31` 的 preload transaction 消费这份 resolved artifact plan，而不是继续直接吞 `TArray<FString>`。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `Issue-29` 指出 `StartupBindRegistrySmoke` 只比较同一 registry 的两个视图与 compile smoke，抓不住关键 bind 缺失、旧清单污染或 startup bind surface authority 漂移。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-34` 明确指出宿主 bind provider 仍在 runtime bootstrap 阶段整批激活，provider discovery/activation 缺少 manifest 与增量激活边界；继续使用 raw list 会把 `P5.31` 的 preload 改造卡在“只有模块名、没有 provider 身份”的半成品状态。
    - [E] `Documents/AutoPlans/ReferenceComparison/GapAnalysis.md` — D1/D11 补充分析已确认 `BindModules.Cache` 仍是 raw module-name list，缺少 `source/scope/phase` 字段；同时 bind-module artifact 与其他 runtime artifacts 的 root authority 仍分裂，这会直接削弱 delivery baseline 与外部 provider 的可诊断性。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L781-L793 — `GetScriptRootDirectory()` 当前就是 runtime 其它 script artifacts 的 authority；同文件 L1469-L1477、L1504-L1529 — `Binds.Cache` 与 `PrecompiledScript*.Cache` 都走 `GetScriptRootDirectory()`，唯独 bind-module 清单改为 `FindPlugin("Angelscript")->GetBaseDir() / "BindModules.Cache"`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` L583-L601 — `SaveBindModules()` / `LoadBindModules()` 仍只用 `SaveStringArrayToFile()` / `LoadFileToStringArray()` 读写裸字符串数组，schema 中没有任何 producer/source/scope/version 信息。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`
- [ ] **P5.32** 📦 Git 提交：`[Runtime/Core] Refactor: unify bind-module artifact authority and structured manifest`
- [ ] **P5.32-T** 单元测试：补齐 bind-module artifact authority、legacy fallback 与 schema fail-fast 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindModuleArtifactContractTests.cpp`
  - 测试场景：
    - 正常路径：script-root authority 下存在 structured bind-module manifest 时，startup 读取到的 artifact plan 与 `Binds.Cache` / `PrecompiledScript*.Cache` 使用同一 authority，preload/bind 只消费 manifest 里的 provider 集。
    - 边界条件：只有 legacy raw `BindModules.Cache` 时，runtime 会经 explicit legacy fallback 成功读取并去重；同时对外暴露“命中了 legacy source / raw schema”的稳定诊断，而不是静默改走另一条 root。
    - 错误路径：manifest version 不支持、required provider 缺字段，或 script-root manifest 与 legacy raw list 相互冲突时，startup 会在 `LoadModule()` / `BindScriptTypes()` 前显式失败，不再继续绑定陈旧 provider 集。
  - 测试命名：`Angelscript.TestModule.Core.Startup.BindModuleArtifactUsesSingleAuthorityAndStructuredManifest`
  - 隔离方式：`isolated full engine + temporary script-root fixture + explicit bind-manifest probe`
- [ ] **P5.32-T** 📦 Git 提交：`[Test/Core] Test: cover bind-module artifact authority and manifest contract`

## 单元测试总览补充（十八）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.32` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindModuleArtifactContractTests.cpp` | single authority manifest、legacy raw fallback、schema/version fail-fast | P1 |

## 验收标准补充（十八）

44. bind-module artifact 不再绕开 `GetScriptRootDirectory()` 所代表的 runtime authority；`Binds.Cache`、`PrecompiledScript*.Cache` 与 bind-module manifest 共享同一条权威路径解析 contract。
45. structured bind-module manifest 至少能表达 `ModuleName / SourceKind / Scope / bRequired / ManifestVersion`；legacy raw `BindModules.Cache` 只作为显式 fallback 存在，authority/schema 冲突会在 `LoadModule()` / `BindScriptTypes()` 前 fail-fast。

## 风险与注意事项补充（十八）

### 风险

38. 如果直接把 runtime 读取 authority 硬切到新 manifest，而不做 dual-read legacy fallback，现有 editor 生成或历史仓库里的 `BindModules.Cache` 会在下一次启动时整体失效；第一阶段必须先做兼容读，再逐步收紧写口。
39. manifest 如果只补路径而不补 `SourceKind / Scope / bRequired` 等最小 schema，`P5.31` 的 preload transaction 仍然无法区分“真正缺 provider”与“读错清单 / 读到 editor-only provider”，最终会把现在的静默错误换成更早但仍不可诊断的 fail-fast。

### 已知行为变化

44. runtime 启动后会更明确地报告 bind-module artifact 的实际命中 authority 和 schema 来源；历史上“恰好从 plugin base dir 读到旧清单也能工作”的路径会从隐式成功变成显式 legacy fallback。
45. 当 bind-module manifest 缺字段、版本不兼容或与 legacy raw list 互相冲突时，startup 会更早失败，而不是继续进入 `LoadModule()` / `BindScriptTypes()` 后再由空 registry 或缺 bind surface 间接暴露问题。

---

## 深化 (2026-04-09 08:02:34)

本轮不重写 `P1.1` 的 testing override owner，也不重复 `P5.21` 的 existing-current-engine adopt-only 分支；只补真实 `StartupModule() -> ShutdownModule() -> StartupModule()` 路径里仍未被单列的初始化闩锁问题。当前源码确认 `bInitializeAngelscriptCalled` 仍只在 `InitializeAngelscript()` 入口置真，`ShutdownModule()` 从不复位，这让真实模块 restart 与 testing-only `ResetInitializeStateForTesting()` 继续分叉。

### Phase 5 深化：把 RuntimeModule restart/retry 从 testing reset 旁路收成正式生命周期合同

- [ ] **P5.33** 收口 `RuntimeModule` 的 startup/shutdown/restart 闩锁，让真实模块重启与 testing reset 共用一套提交/回滚语义
  - `P1.1` 已经在处理 override owner 与 module 初始化状态机，`P5.21` 也在收 current-engine adopt-only；但当前仍有一条真实宿主路径没有被单列：`StartupModule()` 通过 `InitializeAngelscript()` 进入初始化后，会立刻把 `bInitializeAngelscriptCalled` 置为 `true`，而 `ShutdownModule()` 只移除 ticker 与 `OwnedPrimaryEngine`，完全不复位这把闩锁。结果是 editor/commandlet 同进程 restart 时第二次 `StartupModule()` 会静默跳过真正初始化，只有 testing 专用 `ResetInitializeStateForTesting()` 才能恢复。
  - 本项不引入新的 global owner，也不与 `Documents/Plans/Plan_AngelscriptEngineBindAndFileWatchValidation.md` 的启动 bind 验证重复；目标只把 module 自己的 restart/retry 合同收成单一真相：真实 `StartupModule()`、显式 `ShutdownModule()`、以及失败后重试都必须走同一套 `begin -> commit -> rollback -> reset` helper，而不是继续让 testing reset 拥有生产路径没有的恢复能力。
  - 实施上先在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` / `.h` 抽出 `CommitInitializeState()` / `RollbackInitializeState()` 或等价 helper，把 `bInitializeAngelscriptCalled` 的提交时机从 `InitializeAngelscript()` 入口后移到 adopt/create 成功之后；再让 `ShutdownModule()`、失败分支和 `ResetInitializeStateForTesting()` 统一调用同一个 reset helper，保证 `OwnedPrimaryEngine`、`ContextStack`、`FallbackTickHandle` 与初始化闩锁同步复位；最后补 `StartupModule()` 真实 restart 回归，锁住“第一次启动、显式关闭、第二次启动仍可重新建立 runtime owner”这条合同。
  - 来源：
    - [A] `Documents/AutoPlans/RuntimeCore_Analysis.md` — `A-12` 指出 `ShutdownModule()` 不复位 `bInitializeAngelscriptCalled`，同进程二次启动会永久跳过初始化；`A-13` 进一步表明 current-engine 分支仍可能重入 `Initialize()`，说明 module 入口仍缺少明确的提交/回滚边界。
    - [C] `Documents/AutoPlans/TestCoverage/RuntimeCore_TestGaps.md` — `Issue-22` 说明当前 `Startup.Full` 只验证性能 artifact，不验证真实 startup 语义；`NewTest-66` 明确指出 `ShutdownModule()` 的 owned-engine 与 ticker 清理合同没有直接回归，因此 restart 语义更没有正式保护。
    - [D] `Documents/AutoPlans/ArchitectureReview/ScriptLifecycle_ArchReview.md` — `Arch-SL-41` 指出启动恢复策略和 readiness milestone 仍硬编码在 core，缺少显式 boot/retry policy；`Arch-SL-50` 强调 startup observer 只有在 runtime handle 稳定后才应视为 ready，真实 restart 必须具备对称的 reset 与重新提交。
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` L13-L24 — `StartupModule()` 在 editor/commandlet 直接调用 `InitializeAngelscript()` 并注册 fallback ticker；同文件 L27-L40 — `ShutdownModule()` 只移除 `FallbackTickHandle` 并重置 `OwnedPrimaryEngine`，没有任何 `bInitializeAngelscriptCalled = false`；同文件 L138-L166 — `InitializeAngelscript()` 当前在任何分支前就把 `bInitializeAngelscriptCalled` 置真；同文件 L174-L182 — 只有 testing 专用 `ResetInitializeStateForTesting()` 会复位该布尔并清理 override。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleTests.cpp`
- [ ] **P5.33** 📦 Git 提交：`[Runtime/Core] Fix: make runtime module startup-shutdown restartable`
- [ ] **P5.33-T** 单元测试：补齐真实 `StartupModule()` restart 与 init-latch rollback 合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleTests.cpp`
  - 测试场景：
    - 正常路径：在无 current engine 基线下执行 `StartupModule() -> ShutdownModule() -> StartupModule()`，第二次启动后仍会重新建立可解析的 runtime owner，`TryGetCurrentEngine()` 不会卡在第一次的已释放状态。
    - 边界条件：第一次启动创建 `OwnedPrimaryEngine` 并注册 fallback ticker 后，`ShutdownModule()` 会同步清掉 ticker、owner 与 init latch；随后第二次启动只生成一份新 owner，不会重复注册多份 ticker，也不会残留上一轮栈项。
    - 错误路径：向 `ContextStack` 预置一台未 ready 的 current engine 或制造 adopt 失败分支后，`InitializeAngelscript()` 会回滚 init latch 并允许修正输入后重试，而不是永久停在“已初始化过”。
  - 测试命名：`Angelscript.TestModule.Core.RuntimeModule.StartupShutdownStartupRestoresInitializeLatch`
  - 隔离方式：`FCoreTestContextStackGuard` + `FAngelscriptRuntimeModuleTickTestAccess`
- [ ] **P5.33-T** 📦 Git 提交：`[Test/Core] Test: cover runtime module restart and initialize-latch rollback`

## 单元测试总览补充（十九）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.33` | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptRuntimeModuleTests.cpp` | `StartupModule -> ShutdownModule -> StartupModule` restart、ticker/owner/init-latch 同步复位、adopt 失败后可重试 | P1 |

## 验收标准补充（十九）

46. `FAngelscriptRuntimeModule` 在真实 `StartupModule()` / `ShutdownModule()` 循环下具备可重试性：显式 shutdown 后，第二次 startup 会重新建立 runtime owner，而不是被旧 `bInitializeAngelscriptCalled` 闩锁永久短路。
47. module 初始化闩锁只在 adopt/create 真正成功后提交；任一失败或半初始化分支都会同步回滚 `OwnedPrimaryEngine`、`ContextStack` 和 init latch，不再需要依赖 testing-only `ResetInitializeStateForTesting()` 才能恢复。

## 风险与注意事项补充（十九）

### 风险

40. 如果只在 `ShutdownModule()` 末尾补 `bInitializeAngelscriptCalled = false`，而不把 `InitializeAngelscript()` 的提交时机后移到成功路径，真实结果只会从“永远跳过第二次启动”变成“失败后进入半初始化 + 允许重试”的更隐蔽分叉状态。
41. `StartupModule()` 的 restart 回归一旦开始直接驱动真实 module 生命周期，现有依赖 singleton-like 启动假设的测试夹具可能会暴露出额外残留状态；实现时必须把 `FallbackTickHandle`、`OwnedPrimaryEngine` 与 `ContextStack` 一起纳入同一 reset helper，否则很容易修掉 latch 但保留旧栈项或旧 ticker。

### 已知行为变化

46. 真实 `ShutdownModule()` 之后，module 将明确回到“未初始化”基线；后续同进程再次 `StartupModule()` 会重新走初始化，而不是继续沿用“本进程只允许一次 startup”的隐式短路行为。
47. module adopt/create 失败时，调用方会更早看到明确 rollback/重试语义；原先那种“第一次失败后只能靠 testing reset 解锁”的隐式行为将不再保留。
