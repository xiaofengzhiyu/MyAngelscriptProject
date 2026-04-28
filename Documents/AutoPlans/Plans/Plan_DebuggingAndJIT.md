# DebuggingAndJIT 改进计划

## 背景与目标

### 背景

`DebugServer V2` 与 `StaticJIT` 已经是插件成熟阶段的核心能力，但本轮五维分析显示这两条线同时存在“全局状态串台”“transport 与协议 contract 分裂”“JIT 可执行但不可调试”“fallback 不是安全退化而是 `check` / 静默错配”“Windows data breakpoint 后端与协议结果不一致”五类高风险问题。

已核对当前活跃 Plan，避免重复承接：

- `Documents/Plans/Plan_ASDebuggerUnitTest.md` 已覆盖 debugger 测试基础设施与通用测试分层；本计划不重复铺设通用 harness，只追加和运行时修复强绑定的回归任务。
- `Documents/Plans/Plan_StaticJITUnitTests.md` 已覆盖 StaticJIT 基础单元测试补齐；本计划不重复“测试框架扩面”，只承接当前代码中已经验证存在的 runtime correctness / fallback contract 缺口。
- `Documents/Plans/Plan_AS238JITv2Port.md` 只承接 `asIJITCompilerV2` 接口回移；本计划不重复 ThirdParty API 迁移。
- `Documents/Plans/Plan_DebugAdapter.md` 只承接 DAP / VS Code 客户端；本计划不重复 IDE 适配层建设。

因此，本计划的主线不是“继续补工具”，而是先把当前 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/` 中最容易造成行为分叉、协议错位、JIT 调试黑洞和 watchpoint 误判的 runtime 缺陷收口。

### 目标

1. 让 `DebugServer V2` 的会话状态、wire format 与 transport 行为对多 client / 弱网络 / 大包传输保持确定性。
2. 让真实 JIT / precompiled 运行态具备可连接的调试宿主，并让 `exception`、`DebugBreak`、`ensure`、`check`、`CallStack` 与源码定位在解释器 / JIT 间一致。
3. 让 `StaticJIT` 从“未覆盖 opcode 直接 `check` 或静默回退”升级为“函数级 capability / fallback / 诊断”合同，至少堵住 `POWd` / `POWdi` 与 missing entry 这类当前已知确定性分叉。
4. 让 Windows data breakpoint 同时具备正确的寄存器编程、清理闭环和协议回执，避免“请求成功但后端未安装 / 已清理但寄存器仍 armed / watchpoint 被误报成 exception”。

## 范围与边界

- **范围内**：`Debugging/`、`StaticJIT/`、`Core/AngelscriptEngine.cpp`、`Binds/Bind_Debugging.cpp` 的 runtime correctness 改造，以及与每个改进项直接绑定的 targeted regression tests。
- **范围外**：
  - DAP / VS Code / Cursor 客户端实现
  - `asIJITCompilerV2` / ThirdParty JIT interface 回移
  - 泛化测试基建扩容或 `Debugger/` 目录全量重构
  - 与本轮证据无关的 HotReload / AssetDatabase / SourceNavigation 新功能

## 分析来源

| 分析文档 | 关键发现 |
|---------|---------|
| `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` | 锁定 `DebugAdapterVersion` 全局污染、`StopDebugging` / `StartDebugging` 全局清空、`Recv()`/`Send()` transport 缺陷、JIT exception/callstack/debugbreak 断链、`StaticJIT` unsupported opcode `check`、Windows data breakpoint 寄存器与 stop 语义问题 |
| `Documents/AutoPlans/DiscoveryPlans/DebuggingAndJIT_Plan.md` | 给出 `Issue-5` / `Issue-10` / `Issue-11` / `Issue-12` / `Issue-17` / `Issue-28` / `Issue-43` / `Issue-57` / `Issue-66` / `Issue-67` / `Issue-70` 等可执行修复路径 |
| `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` | 指出 current debugger tests 无法覆盖多 client 隔离、live transport path、真实 JIT 调试链路、adapter v1/v2 payload 分支、data breakpoint 端到端语义 |
| `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` | 从架构上确认 `DebugServer` 仍把 transport/session/backend 混在单类里，且 precompiled/JIT 运行态与 debug host 当前互斥；同时指出 data breakpoint 需要 backend 抽象与 capability 化 |
| `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` | `D5` 强调当前仓库把调试协议当正式 contract 维护，不能让 live path 与 parser / session 继续分裂；`D8` 强调 fallback / exception contract 必须前置到 build/codegen/runtime 层，而不是靠 `check` 或日志兜底 |

## 影响范围

本次改进涉及以下操作（按需组合）：

- **会话状态下沉**：`server-global` 的 adapter version / pause / breakpoint / pending request 状态改为 `per-client session`
- **transport 收敛**：live socket `Recv/Send` 改为 buffered incremental framing + partial send progress
- **JIT debug target 打通**：`activeExecution/debugCallStack` 进入 `DebugServer` 的 stop / stack / source-location / debugbreak 链路
- **StaticJIT fallback 合同化**：unsupported opcode / missing JIT entry 从 `check` / 静默回退改为函数级 capability + diagnostic + safe fallback
- **watchpoint backend 硬化**：Windows `DR0-DR3` 编程、异常 handler 清理、verified/reject 回执与 stop reason 收口

按目录分组的文件清单：

- `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/`（核心改动）
  - `AngelscriptDebugServer.h/.cpp`
  - `DebugSession/AngelscriptDebugClientSession.h/.cpp`
  - `Transports/AngelscriptDebugSocketTransport.h/.cpp`
  - `Breakpoints/AngelscriptHardwareDataBreakpointBackend_Windows.h/.cpp`
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/`
  - `AngelscriptEngine.cpp`
- `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/`
  - `StaticJITHeader.h/.cpp`
  - `AngelscriptStaticJIT.cpp`
  - `AngelscriptBytecodes.cpp`
  - `PrecompiledData.cpp`
- `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/`
  - `Bind_Debugging.cpp`
- 测试侧
  - `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/`
  - `Plugins/Angelscript/Source/AngelscriptTest/Debugger/`
  - `Plugins/Angelscript/Source/AngelscriptTest/Shared/`

## 分阶段执行计划

### Phase 1：收口调试会话与 transport 基础合同

- [ ] **P1.1** 将 `DebugServer V2` 从 server-global 状态改成 per-client session，并补齐会话边界上的 transient-state reset
  - 当前实现把 `DebugAdapterVersion`、`bIsDebugging`、`BreakOptions`、`CallstackRequests`、断点表和 data breakpoint 都挂在 `FAngelscriptDebugServer` 单例上，`StartDebugging` / `StopDebugging` 直接改全局状态；这会让多 client 的 adapter version、断点、break filters 与 pending step/pause 互相污染，不满足独立 debug session 的基本约束。
  - 本项先以最小可落地方式把 `DebugAdapterVersion`、`BreakOptions`、pending callstack request、瞬时 stop state (`bBreakNextScriptLine` / `IgnoreBreak*` / `ConditionBreak*`) 收口到 `FSocket* -> session state`；`ClearAllBreakpoints()` 保持“权威断点状态清理”，`ResetTransientDebugExecutionState()` 专门负责跨会话瞬时状态复位，避免继续把断点表清空与会话重置混成一件事。
  - 本项不重复实现新的 DAP adapter，也不扩出完整 discovery host registry；目标是先让当前 `DebugServer V2` 在同进程多 client / 重连 / adapter v1+v2 并存下不再互相清空状态，并为后续 transport/JIT/data breakpoint 改造提供稳定 session 边界。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` — “`DebugAdapterVersion` 使用进程级全局状态，多个调试客户端会互相污染变量消息格式”
    - [A] `DebuggingAndJIT_Analysis.md` — “任意一个客户端发送 `StopDebugging` 都会全局关闭调试并清空其他客户端断点”
    - [A] `DebuggingAndJIT_Analysis.md` — “后接入的调试客户端会在 `StartDebugging` 时重置全局断点与 break filters”
    - [B] `DebuggingAndJIT_Plan.md` Issue-11 — “多个 socket 共享一份可变全局状态，应拆成 `FDebugClientSession`”
    - [B] `DebuggingAndJIT_Plan.md` Issue-66 — “`StartDebugging` / `StopDebugging` / 断线清理不会重置瞬时停点状态”
    - [C] `DebuggingAndJIT_TestGaps.md` — “`Initialize()/Shutdown()` 目前没有统一恢复 DebugServer 干净状态，测试隔离依赖手写 teardown”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT1 — “多前端不是按 session 隔离，最后一个 `StartDebugging` 会覆盖 `DebugAdapterVersion`、断点和 break filters”
    - [E] `CrossComparison.md` [D5] — “当前仓库把调试协议当正式 contract 维护，这条协议不应继续依赖 server-global 串台语义”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L22-L23、L433-L436 — `DebugAdapterVersion` 仍是 namespace 级全局值，`FAngelscriptVariable::operator<<` 继续按该全局值切换 `ValueAddress/ValueSize`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L897-L927 — `StartDebugging`/`StopDebugging` 直接改全局 `bIsDebugging`、`BreakOptions.Empty()`、`ClearAllBreakpoints()` 与 `CallstackRequests`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1222-L1239 — `ClearAllBreakpoints()` 仍顺带清空全部 data breakpoint，而不是按会话隔离。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/DebugSession/AngelscriptDebugClientSession.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/DebugSession/AngelscriptDebugClientSession.cpp`
- [ ] **P1.1** 📦 Git 提交：`[Debug/Session] Refactor: isolate per-client debug state and transient reset`
- [ ] **P1.1-T** 单元测试：验证多 client adapter version、断点与瞬时 stop state 不再互相污染
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSessionIsolationTests.cpp`
  - 测试场景：
    - 正常路径：client A 用 adapter v1、client B 用 adapter v2 同时 attach，请求同一 paused frame 的 `Variables` / `CallStack`，两侧 payload 结构各自稳定；A/B 各自设置的断点与 break filters 都能独立命中。
    - 边界条件：client A 在 running 态发出 `Pause` 或 `StepOver` 但尚未真正停下时结束会话；client B 重新 `StartDebugging` 后首个 safe point 不应收到伪造 `step`，同一 breakpoint 也不应因旧 `IgnoreBreak*` 被吞掉。
    - 错误路径：client A 在 paused 态 `StopDebugging` / 异常断线后，client B 仍保持活跃调试状态且断点未被清空；database-only 连接不应收到其他 session 的 stopped/continued event。
  - 测试命名：`Angelscript.TestModule.Debugger.SessionIsolation.AdapterVersionAndBreakpointsArePerClient`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.1-T** 📦 Git 提交：`[Test/Debug] Test: add debug session isolation regressions`

- [ ] **P1.2** 将 live socket 收发统一到单一 transport 路径，补齐 incremental framing、`Recv()` 失败退出、partial send 与 backpressure 语义
  - 当前 `ProcessMessages()` 仍在 live socket 上手写一套 header/body 同步读取，既不复用已经有单测的 `TryDeserializeDebugMessageEnvelope()`，也不检查 `Recv()` 失败/零字节，更没有按 `BytesReceived` 偏移写入 buffer；`TrySendingMessages()` 则把 `Client->Send()` 的布尔返回当成整包发送成功，`BytesSent < Buffer.Num()` 时仍会直接出队。这让生产 transport 与测试 parser 分裂，任何半包、短读、背压、坏包都会让协议 contract 与自动化结论脱节。
  - 本项把 transport 收敛成“每个 client 持久化 receive buffer + parser 增量消费 + send offset 进度跟踪”的唯一实现；同时把“10 秒首包年龄就断开”的启发式升级为真正的“无进展超时 + 队列上限”策略，避免数据库大包或慢客户端场景被误判成坏连接。
  - 本项只修 transport 与 framing，不在这里顺手改 DAP request/response correlation；但新 transport 必须为后续 `strict parse`、session route、JIT runtime harness 提供可测试的统一入口。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` — “收包循环对 `Recv()` 失败和零字节读取没有退出条件，半包或断线会卡死调试线程”
    - [A] `DebuggingAndJIT_Analysis.md` — “发送队列把 partial send 当作完整成功，调试消息在网络背压下会被直接截断”
    - [A] `DebuggingAndJIT_Analysis.md` — “live socket 收包路径绕过了已测试的 envelope parser，非法长度包不会被拒绝而会把连接流永久打乱”
    - [B] `DebuggingAndJIT_Plan.md` Issue-5 — “live socket 收包路径与已测试 envelope parser 分裂”
    - [B] `DebuggingAndJIT_Plan.md` Issue-10 — “发送队列把 `partial send` 当作完整成功”
    - [B] `DebuggingAndJIT_Plan.md` Issue-28 — “`Recv()` 失败或返回 0 字节时会把调试线程卡死在半包读取中”
    - [C] `DebuggingAndJIT_TestGaps.md` Issue-20 / 相关 helper 问题 — “现有测试更多验证独立 parser / 等待 helper，没有覆盖 live socket path 的消息保持与坏包处理”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT1 — “transport、消息编解码与 session/backend 仍压在同一个 Runtime 类里”
    - [E] `CrossComparison.md` [D5] — “当前仓库已把 envelope framing helper 当正式协议 contract，live path 应共享同一实现”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L756-L789 — header/body 两段 `while` 循环忽略 `Recv()` 返回值且始终从 `Datagram->GetData()` 起始位置写入；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L724-L728 — 仍按 `Queue[0].FirstTry < Now - 10` 粗暴移除客户端；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L2845-L2859 — `TrySendingMessages()` 在未校验 `BytesSent == Msg.Buffer.Num()` 的情况下直接出队。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugSocketTransport.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugSocketTransport.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp`
- [ ] **P1.2** 📦 Git 提交：`[Debug/Transport] Fix: unify live framing partial send and backpressure handling`
- [ ] **P1.2-T** 单元测试：验证 live transport 与 parser contract 一致，坏包不会卡死或错位出队
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp`
  - 测试场景：
    - 正常路径：partial header、partial body、back-to-back envelopes 与 partial send 都能在多次 `Recv()` / `Send()` 后完整复原，不丢尾字节、不乱序。
    - 边界条件：大包 / 慢客户端 / `DebugDatabase` 风格多包场景下，发送进度会刷新存活时间，不会因为固定 10 秒首包年龄被误断开。
    - 错误路径：invalid length、`Recv()==false`、`BytesRead==0`、`Send()==false`、`BytesSent==0` 都会触发 client cleanup，而不是 busy-loop、错位解析或静默丢包。
  - 测试命名：`Angelscript.CppTests.Debug.Transport.LiveSocketIncrementalFraming`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.2-T** 📦 Git 提交：`[Test/Debug] Test: add live transport framing and backpressure coverage`

### Phase 2：打通 JIT 运行态与 DebugServer 的统一调试目标

- [ ] **P2.1** 建立 JIT `activeExecution` 到 `DebugServer` 的统一 debug target，并显式支持受控的 precompiled/JIT runtime debug host
  - 当前 JIT 侧已经维护了 `FScopeJITDebugCallstack` 与 `SCRIPT_DEBUG_CALLSTACK_LINE`，但 `DebugServer` 的 `ProcessException()`、`SendCallStack()`、`TryBreakpointAngelscriptDebugging()` 和 `GetAngelscriptExecutionFileAndLine()` 仍按“只有 `asCContext` 才是合法调试上下文”的旧模型工作；同时引擎初始化又把 `DebugServer` 创建条件和 `bUsePrecompiledData` 做成互斥，导致真实 JIT 运行态根本没有稳定的 debug host。
  - 本项把 `activeExecution/debugCallStack` 升级为 `DebugServer` 的一等 debug target：JIT `exception`、`DebugBreak`、`ensure`、`check`、`CallStack`、源码定位统一通过同一 stop pipeline 输出；`GetAngelscriptExecutionFileAndLine()` 复用与 `GetAngelscriptExecutionPosition()` 一致的 JIT 路径，修掉当前空指针条件写反的问题；同时允许受控 Development/Test precompiled 运行态显式启用 `DebugServer`，让真实 JIT 路径可被端到端验证。
  - 本项不要求一步做到 JIT locals materialization 完整可用，但至少必须保证 JIT 停下时 stop event、callstack 顶帧和源码定位一致，不再退回 native break 或空栈。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` — “JIT 异常路径没有通知 `DebugServer`，调试器收不到 `exception` 停止事件”
    - [A] `DebuggingAndJIT_Analysis.md` — “`SendCallStack` 完全忽略 JIT 维护的 `debugCallStack`”
    - [A] `DebuggingAndJIT_Analysis.md` — “`GetAngelscriptExecutionFileAndLine` 的 JIT 分支把空指针判定写反”
    - [A] `DebuggingAndJIT_Analysis.md` — “JIT 执行里的 `DebugBreak` / `ensure` / `check` 无法进入 DebugServer 停止态”
    - [A] `DebuggingAndJIT_Analysis.md` — “真实加载 StaticJIT/transpiled code 的运行态不会启动 `DebugServer`”
    - [B] `DebuggingAndJIT_Plan.md` Issue-9 / Issue-17 / Issue-19 / Issue-43 / Issue-57 — 分别承接 JIT `DebugBreak`、JIT exception、源码定位、safe-point bridge 与 `CallStack` 修复
    - [C] `DebuggingAndJIT_TestGaps.md` — “当前 debugger tests 完全没有覆盖真实 JIT `DebugBreak` / `ensure` / `check` stop 链路，也没有验证 JIT debug metadata 或非 editor JIT 执行”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT42 — “调试 attach 仍是破坏性启动，JIT / precompiled 运行态缺少稳定 bootstrap policy”
    - [E] `CrossComparison.md` [D5] / [D8] — “当前 AS 的优势在于插件自己拥有 exception/stop contract，多 surface exception 不应在 JIT 路径退化为只写日志或 native break”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1453-L1455 — `DebugServer` 只在 `!bUsePrecompiledData || bScriptDevelopmentMode` 时创建；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1591-L1594 — `bStaticJITTranspiledCodeLoaded` 仍只按 `FJITDatabase::Get().Functions.Num() > 0` 判断；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L5314-L5317 — `HandleExceptionFromJIT()` 只写日志，不进入 `ProcessException()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L5667-L5677 — `GetAngelscriptExecutionFileAndLine()` 在 `DebugStack == nullptr` 分支里直接解引用；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L5718-L5731 — `TryBreakpointAngelscriptDebugging()` 仍要求 `asGetActiveContext() != nullptr`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1382-L1482 — `SendCallStack()` 仅遍历 `asGetActiveContext()`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h` L194-L216、L338-L340 与 `StaticJITHeader.cpp` L104-L161 — JIT 运行态确实维护了 `debugCallStack` 和异常入口。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`
- [ ] **P2.1** 📦 Git 提交：`[Debug/JIT] Fix: bridge activeExecution into debug server and precompiled runtime host`
- [ ] **P2.1-T** 单元测试：验证 JIT runtime 下的 `DebugBreak` / `exception` / `CallStack` / source-location 与解释器态一致
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerJITParityTests.cpp`
  - 测试场景：
    - 正常路径：在启用 precompiled + JIT 的受控运行态里触发 `DebugBreak()` 与一个稳定的 JIT 异常（如除零或空指针），都能收到 `HasStopped`，且 `CallStack` 顶帧文件/行号指向当前 JIT 函数。
    - 边界条件：`debugCallStack` 为空时 `GetAngelscriptExecutionFileAndLine()` 返回空字符串和 `-1`，不会崩溃；JIT `ensure` / `check` 仍遵守“一次一断”与日志文本契约。
    - 错误路径：在真实 precompiled/JIT 运行态 attach 前后切换 `DebugServer` 配置，未启用调试宿主时应返回明确 capability / skip；启用后不得退回 native break 或空栈。
  - 测试命名：`Angelscript.TestModule.Debugger.JITParity.DebugBreakExceptionAndCallstack`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.1-T** 📦 Git 提交：`[Test/JIT] Test: add JIT debugger parity coverage`

- [ ] **P2.2** 将 `StaticJIT` 从“unsupported opcode 直接 `check` / missing entry 静默回退”升级为函数级 capability + safe fallback contract，并修复 `POWd` / `POWdi`
  - 当前生成链路在 `CompileFunction()` 阶段把所有脚本函数无条件加入 `FunctionsToGenerate`，`GenerateCppCode()` 再对每条 bytecode 直接 `check(bImplemented)`；与之同时，多条 opcode 仍明确 `return false` 或 `check(false)`，`POWd` / `POWdi` 甚至已经写出 `Math::Pow(...)` 代码却仍返回 `false`。运行时若某个函数没有命中 `FJITDatabase::Functions`，`PrecompiledData::Create()` 又会静默退回 VM，而公开状态位只看“进程里是否有任意 JIT function”，无法知道目标函数是否实际拿到了 JIT entry。
  - 本项把这条链升级为“函数级 capability / fallback / diagnostic”：支持的函数正常生成并标记 `FullyJitted`；部分仅能走 VM/native fallback 的函数标记 `HybridFallback`；确实不支持的函数显式 `InterpreterOnly`，不再靠 `check` 或 `DO_CHECK=0` 的半成品代码兜底。第一阶段至少要修正 `POWd` / `POWdi` 的返回值和类型契约，并为 missing JIT entry / unsupported opcode 输出可回归的诊断信号。
  - 同一改动还应把 precompiled VM fallback 保留为“可调试 artifact”，至少保留 line cue 与 source-location 元数据，避免函数级 JIT miss 只能执行、不能断点/步进。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` — “`StaticJIT` 对部分函数未命中 transpiled code 的回退完全静默”
    - [A] `DebuggingAndJIT_Analysis.md` — “StaticJIT 对多类未覆盖字节码不是安全 fallback，而是在生成期直接 `check` 中断”
    - [A] `DebuggingAndJIT_Analysis.md` — “`DO_CHECK=0` 时 unsupported bytecode 不会 fallback，只会继续生成错误代码”
    - [A] `DebuggingAndJIT_Analysis.md` — “`POWd` / `POWdi` 明明生成了 JIT 代码却返回 `false`”
    - [B] `DebuggingAndJIT_Plan.md` Issue-12 — “`POWd` / `POWdi` 的 JIT handler 同时存在错误返回值和错误类型契约”
    - [B] `DebuggingAndJIT_Plan.md` Issue-6 / Issue-39 — “VM fallback 需要保留可调试 line cue，precompiled/JIT cache 需要函数级 validation/fallback”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-4 / NewTest-5 / Issue-18 — “现有自动化不知道目标函数是否拿到 JIT entry，也没有覆盖真实 codegen / debug metadata / non-editor JIT 执行”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT2 — “JIT 覆盖率需要 manifest / failure reason / function-level contract，而不是继续靠 build-time assert”
    - [E] `CrossComparison.md` [D8] — “高价值 contract 应前置到 build/codegen 阶段，不应继续靠 runtime `check` 或静默退化”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` L66-L75 — 生成模式仍把所有函数无条件加入 `FunctionsToGenerate`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` L3204-L3336 — `GenerateCppCode()` 对每条 bytecode `check(bImplemented)`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` L6047-L6054、L6398-L6517 — `asBC_CallPtr`、列表初始化相关、`POWi/u/i64/u64` 仍直接 `check(false)` / `return false`，`POWd` / `POWdi` 写了代码却返回 `false`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L527-L540 — 缺失 JIT entry 时直接 `AllocateScriptFunctionData()` 静默回退；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1591-L1594 — `bStaticJITTranspiledCodeLoaded` 仍只按 `Functions.Num() > 0` 汇总判断。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`
- [ ] **P2.2** 📦 Git 提交：`[StaticJIT] Refactor: classify unsupported functions and fix pow fallback contract`
- [ ] **P2.2-T** 单元测试：验证 `POWd/POWdi`、unsupported opcode 与 missing JIT entry 都走可预测的函数级合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITFallbackTests.cpp`
  - 测试场景：
    - 正常路径：`double ** double` 与 `double ** int` 脚本函数能成功生成 / 加载，JIT 结果与解释执行一致。
    - 边界条件：人为让目标函数缺失 JIT entry 时，运行态会给出明确 `HybridFallback/InterpreterOnly` 信号并仍可执行，不再把整个系统误报成“JIT 已加载且一切正常”。
    - 错误路径：包含 `CallPtr`、列表初始化或其它当前不支持 opcode 的函数不会触发 `check` / 产出半成品代码，而是被稳定分类为 `InterpreterOnly`，同时生成 manifest / log / counter 可供自动化断言。
  - 测试命名：`Angelscript.CppTests.StaticJIT.Fallback.UnsupportedOpcodeAndPowMatrix`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.2-T** 📦 Git 提交：`[Test/StaticJIT] Test: add fallback and pow consistency coverage`

### Phase 3：硬化 Windows data breakpoint backend 与协议结果

- [ ] **P3.1** 收口 Windows data breakpoint 的寄存器后端、即时清理闭环与 verified/reject 协议回执
  - 当前 data breakpoint 同时存在三类确定性缺陷：其一，`GetThreadContext()` 之前才设置 `ContextFlags`，`Dr7` 初始化常量也和注释声明冲突；其二，handler 内的“紧急清除”只改 active mirror，不会立即撤销当前线程寄存器，也不会在无后续脚本行时同步权威状态；其三，`SetDataBreakpoints` 把请求原样写入 `DataBreakpoints`，只把前四条复制进 `ActiveDataBreakpoints`，多出来的 watchpoint 既无 verified/reject 回执，又在 stop 阶段继续被伪装成 `Reason="exception"`。
  - 本项把 data breakpoint 收敛成“authoritative request -> backend apply -> verified/reject result -> stable stop reason / clear flow”四段式合同：先修正 `CONTEXT_DEBUG_REGISTERS` 的读写顺序与 `Dr7` 初始化；再把 handler 中的“立即撤销当前线程寄存器”与“游戏线程清空权威状态并广播 `ClearDataBreakpoints`”分成两阶段；最后为超过 4 个请求或硬件编程失败的条目返回结构化拒绝结果，并把 watchpoint 停止原因从 `exception` 拆成显式 data-breakpoint 分类。
  - 本项仍以 Windows backend 为第一落地点，但实现上应为后续 `IAngelscriptDataBreakpointBackend` / capability 化保留接口，不继续把 raw address + `DR0-DR3` 假定为唯一协议事实。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` — “`SetDataBreakpoints` 会静默丢弃第 5 个及之后的断点”
    - [A] `DebuggingAndJIT_Analysis.md` — “Windows data breakpoint 更新线程在 `GetThreadContext()` 之后才设置 `ContextFlags`”
    - [A] `DebuggingAndJIT_Analysis.md` — “`Dr7` 初始化常量把高位重新置 1”
    - [A] `DebuggingAndJIT_Analysis.md` — “异常处理器会吞掉未被本插件确认的 `EXCEPTION_SINGLE_STEP`”
    - [A] `DebuggingAndJIT_Analysis.md` — “data breakpoint 命中发生在 C++ 写入时不会立即停下，若后续没有脚本行回调则停止事件会丢失”
    - [B] `DebuggingAndJIT_Plan.md` Issue-67 — “`ClearAllAngelscriptDataBreakpointsFromHandler()` 不会真正撤销当前线程硬件断点”
    - [B] `DebuggingAndJIT_Plan.md` Issue-70 — “超过 4 个请求时没有 verified/reject 回执”
    - [C] `DebuggingAndJIT_TestGaps.md` — “当前没有任何端到端自动化证明 data breakpoint 会触发、清理并回发 `ClearDataBreakpoints` / capacity result”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT31 — “watchpoint backend 需要从 raw address + Windows-only 后端升级成可替换 capability”
    - [E] `CrossComparison.md` [D5] — “当前仓库的变量查看、表达式求值与 data breakpoint 本应共享同一条 addressable entity chain，这条链不能在 backend/协议层继续静默失真”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L236-L253 — `GetThreadContext()` 之后才设置 `Context.ContextFlags = CONTEXT_DEBUG_REGISTERS`，且 `Dr7 |= 0x001000000000` 与注释“32-63 应为 0”相矛盾；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L279-L370 — handler 在 `GClearDataBreakpoints` 分支里只改 active mirror 与 `bBreakNextScriptLine`，未立即清当前异常上下文寄存器；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L479-L545 — watchpoint flush 仍把停止原因硬编码成 `exception`，并依赖后续脚本行才广播 `ClearDataBreakpoints`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1061-L1068、L1248-L1286 — `SetDataBreakpoints` 直接覆盖权威数组，`RebuildActiveDataBreakpoints()` 只保留前四条，且 `UpdateDataBreakpoints()` 没有任何 per-item verified/reject 结果。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Breakpoints/AngelscriptHardwareDataBreakpointBackend_Windows.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Breakpoints/AngelscriptHardwareDataBreakpointBackend_Windows.cpp`
- [ ] **P3.1** 📦 Git 提交：`[Debug/DataBreakpoint] Fix: harden hardware watchpoint backend and verified results`
- [ ] **P3.1-T** 单元测试：验证 data breakpoint 的 verified/reject、trigger、force-clear 与 stop reason 全链路稳定
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointRobustnessTests.cpp`
  - 测试场景：
    - 正常路径：通过 `Evaluate` 获取 `ValueAddress/ValueSize` 后设置 `HitCount=1` 的 data breakpoint，命中时收到 1 条明确的数据断点 stop 与 1 条 `ClearDataBreakpoints` 回包；权威状态与寄存器都被清理。
    - 边界条件：一次发送 5 条 watchpoint 时，前 4 条得到 verified，最后 1 条得到 rejected；forced clear 从 handler 触发后即使没有后续脚本行也会撤销寄存器并广播清理。
    - 错误路径：`GetThreadContext()` / `SetThreadContext()` 失败或收到不属于本插件的 `EXCEPTION_SINGLE_STEP` 时，不会让无关异常被吞掉，也不会让客户端误以为 watchpoint 已安装成功。
  - 测试命名：`Angelscript.CppTests.Debug.Protocol.DataBreakpoints.VerifiedCapacityAndReject`；`Angelscript.TestModule.Debugger.DataBreakpoint.VerifiedTriggerAndForceClear`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.1-T** 📦 Git 提交：`[Test/Debug] Test: add data breakpoint robustness coverage`

## 单元测试总览

- `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 延续现有 `Angelscript.CppTests.*` 命名；`Plugins/Angelscript/Source/AngelscriptTest/` 延续现有 `Angelscript.TestModule.*` 命名，避免为了本计划打破既有测试索引与分层。

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.1` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSessionIsolationTests.cpp` | adapter v1/v2 并存、A/B 断点互不清空、旧会话 pending step 不污染新会话 | P0 |
| `P1.2` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` | partial header/body、partial send、多 envelope、坏包断开、慢客户端 backpressure | P0 |
| `P2.1` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerJITParityTests.cpp` | JIT `DebugBreak`/exception stop、JIT `CallStack`、JIT source location、不回退 native break | P0 |
| `P2.2` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITFallbackTests.cpp` | `POWd/POWdi` 正例、unsupported opcode `InterpreterOnly`、missing JIT entry fallback signal | P0 |
| `P3.1` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointRobustnessTests.cpp` | verified/reject、trigger+clear、forced clear、寄存器失败与无关 single-step | P1 |

## 验收标准

1. `DebugServer V2` 支持至少两个并存 client 在不同 adapter version / breakpoint state 下独立调试，`StartDebugging` / `StopDebugging` / `BreakOptions` 不再跨 client 清空状态。
2. live socket transport 与 `TryDeserializeDebugMessageEnvelope()` 共享同一条 parser contract；partial recv / partial send / invalid length / zero-byte read 都有自动化覆盖且不会卡死线程或截断消息。
3. 在受控 precompiled/JIT 运行态，`DebugBreak`、JIT exception、`CallStack` 与源码定位能通过 `DebugServer` 闭环；`GetAngelscriptExecutionFileAndLine()` 在 JIT 下不再崩溃或写空元数据。
4. `StaticJIT` 对 unsupported opcode、missing JIT entry 与 `POWd/POWdi` 具备稳定的函数级 capability/fallback 行为，不再依赖 `check` 或静默回退；至少有一组脚本级回归验证 JIT/VM 结果一致。
5. Windows data breakpoint 在正常命中、容量超限、forced clear 与硬件编程失败场景下都能返回明确协议结果；watchpoint 停止原因不再伪装成普通 `exception`。
6. 本计划新增测试可以单跑、组合跑，并且不会因为残留 `DebugServer` 状态、socket、pending monitor 或硬件寄存器污染后续用例。

## 风险与注意事项

### 风险

1. **会话隔离改造会同时触碰 line callback 快路径与停止状态机**
   - 若 per-session 原始状态与 runtime 并集状态切分不清，可能出现“语义正确但性能退化”或“并集正确但单 client 视角错误”的双重回归。
2. **JIT debug target 一旦真正接入 DebugServer，会暴露更多解释器前提**
   - 例如 `Variables` / `Evaluate` 目前大量依赖 `asCContext`；若只修 stop / callstack，不补最小 backend-neutral target，前端仍可能停得住但看不到值。
3. **StaticJIT fallback 合同化不能把 `check` 简单改成静默跳过**
   - 必须同时补 manifest / counter / log / test，否则会把“立即崩”退化成“长期性能退化但无人感知”。
4. **Windows data breakpoint handler 运行在异常上下文，不能做堆分配或复杂容器操作**
   - 即时清寄存器与延迟清权威状态必须分层，否则极易引入新的 re-entrancy 或 allocator 风险。

### 已知行为变化

1. **`StartDebugging` / `StopDebugging` 将不再隐式充当“全局清场”**
   - 旧客户端若依赖“第二个 attach 会顺手清空所有断点 / filters / pending step”，需要同步迁移到显式 `ClearBreakpoints` / 会话 reset。
2. **data breakpoint 的 stopped reason 与回执会变更**
   - 当前误报为 `exception`、容量超限静默失效的行为将被显式 `verified/reject` 和独立 watchpoint stop 取代；旧客户端若按 `Reason=="exception"` 做专门 UI，需要跟进协议字段。
3. **受控 Development/Test precompiled 运行态会新增 `DebugServer` 组合模式**
   - 这会让“真实 JIT + DebugServer”成为可测试配置；Shipping 默认策略仍需显式保守，不应意外开放调试端口。
4. **`StaticJIT` 可能开始把部分函数明确标记为 `InterpreterOnly` / `HybridFallback`**
   - 这是有意为之的正确性收口，取代今天“生成期断言”或“运行时静默回退”的不透明行为。

---

## 本轮补充（2026-04-09）

以下条目是在现有 `P1.1-P3.1` 基础上的增量补充，只覆盖本轮新确认且尚未纳入正文的 runtime contract 缺口；不重复 `Documents/Plans/Plan_ASDebuggerUnitTest.md` 的测试基建、不重复 `Documents/Plans/Plan_StaticJITUnitTests.md` 的纯单元扩面，也不重复 `Documents/Plans/Plan_DebugAdapter.md` 的 DAP/IDE 适配工作。

### 追加到 Phase 1：收口调试会话与 transport 基础合同

- [ ] **P1.3** 把 `ResolveDebuggerFrame` 改成显式失败合同，禁止过期 frame 请求静默回落到 frame 0
  - 当前 `RequestEvaluate` / `RequestVariables` 的 frame 解析仍把“找不到 frame”编码成合法 frame `0`，这是典型的 silent wrong-answer：前端拿到结构合法但错帧的 locals/watch 值，而不是明确失败。本项先做最小热修，优先消灭错值伪成功，再为后续更大的 `FrameId/ScopeId/ValueId` 模型留出正确边界。
  - 这一步只修当前 `Path` 协议的 fail-fast 语义，不重复 `Arch-DT15` 那套完整 snapshot/object-handle 重构；最小目标是 `Evaluate` / `Variables` 对 invalid/expired frame 返回可判别失败，不再把请求改写到顶层 frame 继续查询。
  - 同时把 `DefaultFrame` 与 `"{Frame}:Path"` 两条入口统一到同一个 `TryResolveDebuggerFrame(...)`，避免只修一个入口后另一个继续偷偷 alias 到 frame 0。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 8 — “无效的 debugger frame 会被静默重定向到 frame 0，`Evaluate`/`Variables` 结果可能落到错误栈帧”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-23 — “invalid `debugger frame` 会被静默改写成 frame 0，前端会拿到合法但错帧的结果”
    - [D] `ArchitectureReview/DebugAndToolchain_ArchReview.md` Arch-DT15 — “调试数据模型仍是 `Path DSL + 当前活动上下文`，frame/scope/value 没有显式身份合同”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L2282-L2345 — `ResolveDebuggerFrame()` 在越界时仍直接 `return 0`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L2369-L2375 — `GetDebuggerValue()` 无条件把解析结果写回 `*InOutFrame`，继续按该 frame 求值。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`
- [ ] **P1.3** 📦 Git 提交：`[Debug/Variables] Fix: reject invalid debugger frame ids instead of aliasing frame zero`
- [ ] **P1.3-T** 单元测试：验证过期/越界 frame 不再偷偷落到顶层 frame
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerInvalidFrameTests.cpp`
  - 测试场景：
    - 正常路径：在两层以上 `CallStack` 的停点处，对合法 frame id 发 `RequestEvaluate` / `RequestVariables`，仍返回目标帧 locals，而不是误伤现有合法查询。
    - 边界条件：先缓存较深 frame id，再 `StepOut` 或 `Continue` 到栈收缩后的停点；使用旧 frame id 请求时返回明确 invalid-frame 结果或可判别空响应，不会回退到 frame 0。
    - 错误路径：对负数 frame、超大 frame、`"{Frame}:Path"` 与 `DefaultFrame` 混合错误输入都不应崩溃、不应改写到顶层 frame，且同一 session 后续合法请求仍可继续工作。
  - 测试命名：`Angelscript.TestModule.Debugger.Variables.InvalidFrameDoesNotAliasTopFrame`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.3-T** 📦 Git 提交：`[Test/Debug] Test: add invalid frame regression coverage`

### 追加到 Phase 2：打通 JIT 运行态与 DebugServer 的统一调试目标

- [ ] **P2.3** 收口 `StaticJIT` 的 virtual/imported 调用目标解析，避免 direct-call 假设绕过 safe fallback
  - 当前 `FCallScriptFunction::MakeCall()`、`DevirtualizeFunction()` 与 `WriteDirectCall()` 把“生成期假设”直接固化成运行期 direct-call：virtual method 在 `bAllowComprehensiveJIT` 下被视为“总能 JIT”，imported call 则会在 codegen 时绑定到当时的 `boundFunctionId`。这些前提一旦在部分 JIT bundle、热重载或 rebind/unbind 场景下失效，结果不是安全回退，而是空 `jitFunction_Raw` 调用、旧目标继续被调用，或者 `unbound function` 异常被绕过。
  - 本项先做最小 correctness 收口，不重复 `Documents/Plans/Plan_AS238JITv2Port.md` 的接口回移，也不等待更大的 dispatch helper 重构；第一步先保证 virtual callee 缺失 raw JIT entry 时会走 VM fallback，`LookupType::BindIdArg` 永远保留运行期 `boundFunctionId` 查表语义。
  - 在此基础上，再为“为什么能 direct call / 为什么必须 dynamic call”的决策留下可诊断记录点，为后续 assumption manifest 或 dispatch ledger 准备稳定落点；本轮仍以消灭 crash 和 stale binding 为完成标准。
  - 来源：
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-1 — “JIT virtual dispatch 在 callee 缺失 JIT entry 时会直接调用空 `jitFunction_Raw`”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-2 — “`asBC_CALLBND` 在生成期静态绑定 imported function，JIT 代码不会观察运行期 rebind / unbind”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-18 / JIT harness 建议 — “现有 `StaticJIT` 自动化没有真实执行路径，virtual fallback / imported rebind 没有运行时护栏”
    - [D] `ArchitectureReview/DebugAndToolchain_ArchReview.md` Arch-DT30 — “dispatch/devirtualization assumption 被隐式写进生成过程，缺少可追踪的 assumption ledger”
    - [E] `ReferenceComparison/CrossComparison.md` [D8] — “高价值 contract 应前置到 build/codegen/runtime 层，而不是继续靠静默退化或断言兜底”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` L1171-L1194 — `MakeCall()` 在 `RealFunction == nullptr && IsFunctionAlwaysJIT(ScriptFunction)` 时仍直接走 `WriteDirectCall(Context, nullptr, nullptr)`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` L1453-L1458 — `WriteDirectCall()` 直接解引用 `CallRealFunction->jitFunction_Raw`，没有 null/fallback guard；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` L1487-L1496 — `WriteDynamicCall()` 已具备按 `boundFunctionId` 运行期查表的 imported fallback；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` L3424-L3449、L3472-L3474 — `DevirtualizeFunction()` 仍把 imported function 去虚化成当前 `BoundId`，`WriteOutputCode()` 继续强制 `bAllowDevirtualize = true` 与 `bAllowComprehensiveJIT = true`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITDispatchFallbackTests.cpp`
- [ ] **P2.3** 📦 Git 提交：`[StaticJIT] Fix: harden virtual and imported dispatch fallback contracts`
- [ ] **P2.3-T** 单元测试：验证 virtual callee 缺失 JIT entry 与 imported rebind/unbind 都按运行期真实目标执行
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITDispatchFallbackTests.cpp`
  - 测试场景：
    - 正常路径：virtual callee 已有 raw JIT entry、imported function 保持原绑定时，JIT caller 仍走 direct path 且结果与解释执行一致。
    - 边界条件：caller 已 JIT、callee 未注册到 `FJITDatabase` 或 imported function 在生成后重绑定到新实现时，JIT caller 自动回退到 VM/新绑定目标，不会继续使用旧 `BoundId` 或空 `jitFunction_Raw`。
    - 错误路径：imported function 被显式解绑时稳定抛出 `SCRIPT_UNBOUND_EXCEPTION()`；virtual callee raw entry 缺失时不会崩溃、不会 silent return，异常/返回值语义与解释执行一致。
  - 测试命名：`Angelscript.CppTests.StaticJIT.Dispatch.VirtualAndImportedFallback`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.3-T** 📦 Git 提交：`[Test/StaticJIT] Test: add virtual and imported fallback regressions`

### 追加到 Phase 3：统一 stop 过滤与 capability contract

- [ ] **P3.2** 把 `BreakFilters/BreakOptions` 从协议空壳收口成真实 capability，并让 exception/data breakpoint 遵守同一 stop filter
  - 当前 `BreakFilters` 握手、`BreakOptions` 存储与真正的 stopped-event 决策是三条分裂路径：provider 未绑定时仍会回空 `BreakFilters` 响应，`BreakOptions` 未绑定时默认 `return true`，而 exception / data breakpoint 又完全绕过 `ShouldBreakOnActiveSide()`。这会让客户端看到“协商成功、配置成功、行为无变化”的双重 silent failure。
  - 本项不重复 `Documents/Plans/Plan_DebugAdapter.md` 的 DAP 映射，而是把 server 端 contract 先修正成 truthful capability：provider 缺失时显式 unsupported；provider 存在时，line breakpoint、脚本 exception、data breakpoint 都要走同一套过滤决策。显式 `Pause` / `Step*` 这类用户控制保持不受 filter 影响。
  - 这一步也不重复现有 `P3.1` 的硬件 backend 加固；它承接 `P3.1` 对 data breakpoint stop reason/清理链路的修复，把“什么能停、什么时候该停、客户端被告知了什么能力”统一成可回归的协议面。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 66 — “`BreakFilters` / `BreakOptions` 在当前插件源码里默认是协议空壳”；发现 80 — “`BreakOptions` 只影响普通 line breakpoint，异常与 data breakpoint 完全绕过过滤器”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-68 — “`BreakOptions` 只过滤 line breakpoint，exception 和 data breakpoint 会绕过同一套 stop filter”；Issue-71 — “`BreakFilters/BreakOptions` 在当前源码里只是协议空壳”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` NewTest-11 — “`BreakFilters` 需要从可反序列化升级为内容与能力可回归”；NewTest-15 — “验证 `BreakOptions` 真正参与断点过滤，而不是只返回 filter 列表”
    - [E] `ReferenceComparison/CrossComparison.md` [D5] — “当前仓库把调试协议当正式 contract 维护，能力协商与 stopped-event 语义不能继续脱节”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1137-L1159 — `BreakOptions` 仅写入数组，`RequestBreakFilters` 无论 provider 是否存在都直接回包；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L652-L665 — `ShouldBreakOnActiveSide()` 在 delegate 未绑定时仍直接 `return true`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L440-L462、L479-L545、L602-L605 — exception 与 data breakpoint stop 仍绕过统一过滤，只剩 line breakpoint 命中时才调用 `ShouldBreakOnActiveSide()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` L48-L57 — `GetDebugCheckBreakOptions()` / `GetDebugBreakFilters()` 仍只返回未绑定的 static delegate，当前插件没有默认 provider。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`
- [ ] **P3.2** 📦 Git 提交：`[Debug/Filters] Fix: make break filter capability truthful and stop-source consistent`
- [ ] **P3.2-T** 单元测试：验证 break filter provider 能力协商真实，且 line breakpoint / exception / data breakpoint 共用同一过滤语义
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakFilterContractTests.cpp`
  - 测试场景：
    - 正常路径：绑定 provider 后，`RequestBreakFilters` 返回非空 filter 列表；在同一配置下触发 line breakpoint、脚本 exception、data breakpoint，三者都按同一 filter 决定是否停下。
    - 边界条件：provider 未绑定时，握手返回 explicit unsupported 或 capability false，而不是“空列表但看似支持”；显式 `Pause` / `Step*` 不受 filter 影响。
    - 错误路径：发送未知 filter 或在 unsupported 状态下发送 `BreakOptions` 时，会收到可判别的 diagnostic/no-op 结果，不会伪装成“配置已生效”；同一 session 后续正常断点与继续控制仍可工作。
  - 测试命名：`Angelscript.CppTests.Debug.Protocol.BreakFilters.UnsupportedCapability`; `Angelscript.TestModule.Debugger.BreakFilters.ProviderAndStopSourceConsistency`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.2-T** 📦 Git 提交：`[Test/Debug] Test: add break filter capability and stop-source coverage`

## 单元测试总览补充

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.3` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerInvalidFrameTests.cpp` | 合法 frame 保持可用、expired frame 不再 alias 到 frame 0、错误输入后 session 仍健康 | P1 |
| `P2.3` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITDispatchFallbackTests.cpp` | virtual raw-entry miss、imported rebind/unbind、JIT/VM 结果一致 | P0 |
| `P3.2` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakFilterContractTests.cpp` | provider 握手、unsupported capability、line/exception/data breakpoint 统一过滤 | P1 |

---

## 深化 (2026-04-09 00:50:11)

以下条目补足当前文档尚未覆盖的 `step state ownership`、`request/response correlation`、`precompiled/JIT activation validation` 与 `stopped payload identity` 缺口；不重复 `Documents/Plans/Plan_DebugAdapter.md` 的 DAP 适配层建设，也不重复 `Documents/Plans/Plan_StaticJITUnitTests.md` 的纯测试扩面。

### 追加到 Phase 1：收口调试会话与 transport 基础合同

- [ ] **P1.4** 给 `Continue/Step*` 建立 active-stop ownership，并修复顶层 `StepOut` 退化成“下一行即停”
  - 当前 `HandleMessage()` 收到 `StepIn/StepOver/StepOut` 就直接改写 `bBreakNextScriptLine` / `bIsPaused`，没有 paused ownership 校验；`StepOut` 在没有 caller 时又把 `ConditionBreak*` 清空，导致后续 `ProcessScriptLine()` 把它当成无条件 `step`。这会把 running 态误发的 step 与顶层 `StepOut` 一并放大成 stray stop。
  - 本项先做最小但严格的状态机收口：把 `Continue/Step*` 改成“只有 active stop owner 才能发起的 resume intent”，running 态、过期 stop generation 或旁路 session 一律显式拒绝；`StepOut` 在顶层帧上改成“运行到当前脚本退出”而不是再次逐行停下，必要时借 `ProcessScriptStackPop()`/调用完成路径兑现退出。
  - 这一步不扩成完整的 thread-aware pause controller；它是 `PauseController` 重构前的 correctness 热修，先堵住 stray step 与 top-frame `StepOut` 假停点，再给后续 controller 化保留干净语义。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 33 — “`StepIn` / `StepOver` / `StepOut` 没有 stopped-state 门槛，running 态请求会在下一条任意脚本行全局生效”
    - [A] `DebuggingAndJIT_Analysis.md` 发现 6 — “`StepOut` 在最外层脚本帧上退化成 `StepIn`，无法真正运行到返回点”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-44 — “需要把 `Step*` 从裸改全局字段改成 active-stop gated resume intent”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-3 / Issue-22 / NewTest-17 — “现有 stepping tests 没有兜住 top-frame `StepOut`、running-state 误发 step 与 stopped reason 语义”
    - [D] `ArchitectureReview/DebugAndToolchain_ArchReview.md` Arch-DT14 — “暂停控制当前仍与内嵌 socket pump 耦合，`Continue/Step*` 是直接改全局字段而不是命令队列”
    - [E] `ReferenceComparison/CrossComparison.md` [D5-Deep] — “当前仓库把 UE-owned debug contract 做成正式协议资产，`step/stop` 语义不能继续依赖隐式串行假设”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L847-L895 — `StepIn/StepOver/StepOut` 仍直接改 `bBreakNextScriptLine` / `bIsPaused`，没有 active stop gate；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L550-L575 — `ConditionBreakFrame == -1` 时 `bShouldBreak` 保持为 `true`，顶层 `StepOut` 仍会在下一条脚本行立刻再次停下。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerStepStateTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`
- [ ] **P1.4** 📦 Git 提交：`[Debug/Stepping] Fix: gate continue and step commands by active stop ownership`
- [ ] **P1.4-T** 单元测试：验证 running-state `Continue/Step*` 不再制造 stray stop，且顶层 `StepOut` 会直接完成脚本
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerStepStateTests.cpp`
  - 测试场景：
    - 正常路径：在 paused 状态下执行 `StepIn` / `StepOver` / `StepOut` 仍返回 `HasStopped(reason="step")`，caller/callee 行号与现有 stepping fixture 一致。
    - 边界条件：停在 `RunScenario()` 顶层 `StepAfterCallLine` 后发送 `StepOut`，不再收到额外 `HasStopped`，invocation 直接完成且返回 `14`。
    - 错误路径：在 running 态、过期 stop generation 或非 owner session 上发送 `Continue` / `StepOver` / `StepOut` 时，只得到 diagnostic/no-op，不会在下一条任意脚本行平白出现 `HasStopped(reason="step")`。
  - 测试命名：`Angelscript.TestModule.Debugger.Stepping.ActiveStopOwnershipAndTopFrameStepOut`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.4-T** 📦 Git 提交：`[Test/Debug] Test: add active-stop stepping regressions`

- [ ] **P1.5** 给 `RequestCallStack/Variables/Evaluate/GoToDefinition` 增加 `RequestId + Status` 元层，禁止响应漂移到下一次暂停
  - 当前 `DebugServer V2` 的 envelope 仍只有 `MessageType + Body`；`RequestCallStack` 只是把 socket 塞进 `CallstackRequests`，`Variables/Evaluate` 只回裸 payload，`GoToDefinition` 还是 fire-and-forget。结果是协议把“请求响应”和“事件推送”混在一起，单飞/FIFO 假设被硬编码进 wire contract。
  - 本项在保持现有 message type/body 兼容的前提下，追加可选 `request/response meta-layer`：至少给 `RequestCallStack`、`RequestVariables`、`RequestEvaluate`、`GoToDefinition`、`RequestDebugDatabase` 增加 `RequestId`、`IsResponse/IsEvent/IsError` 与结构化失败载荷。`RequestCallStack` 在 running 态必须立即回明确失败或空快照，不能再漂移到下一次任意 `breakpoint/exception/pause`。
  - 这一步承接 `P1.1` 的 session 边界，但不等同于 session isolation：它修的是“某条响应属于哪次请求、失败如何表达”的协议合同，而不是“谁拥有当前调试状态”的权限边界。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 4 — “`RequestCallStack` 在运行态被延迟到下一次任意暂停才响应”
    - [A] `DebuggingAndJIT_Analysis.md` 发现 69 — “协议 envelope 没有 request correlation，客户端只能靠消息类型和顺序猜归属”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-42 — “需要把 V2 升级成显式 request/response/event 模型，并给失败语义结构化出口”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-12 / Issue-17 / Issue-26 — “`WaitForMessageType()` 会吞并发消息、`DeserializeMessage<T>()` 默认接受 trailing bytes、monitor helper 会把协议写失败伪装成超时”
    - [D] `ArchitectureReview/DebugAndToolchain_ArchReview.md` Arch-DT21 — “`DebugServer V2` 没有 request correlation 与错误合同，单前端串行假设被硬编码进协议面”
    - [E] `ReferenceComparison/CrossComparison.md` [D5-Deep] — “当前仓库把带 framing 和协议测试的自有调试协议当正式 contract 维护，请求/响应/事件边界必须明确”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L86-L93 — envelope 只有 `MessageType` 和 `Body`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L52-L107 — framing 仍只写 `length + type + body`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L812-L817、L924-L927 — `RequestCallStack` 仍只把 socket 进队并在暂停态统一 flush；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1081-L1135 — `RequestVariables` / `RequestEvaluate` / `GoToDefinition` 仍回裸 payload 或 fire-and-forget，没有 `RequestId/Status/Ack`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolCorrelationTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`
- [ ] **P1.5** 📦 Git 提交：`[Debug/Protocol] Refactor: add request correlation and structured response errors`
- [ ] **P1.5-T** 单元测试：验证多请求并发、running-state `RequestCallStack` 和失败路径都能按 `RequestId` 正确归属
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolCorrelationTests.cpp`
  - 测试场景：
    - 正常路径：同一 client 交错发送两条 `RequestEvaluate` 与一条 `RequestVariables`，三条响应都带回匹配的 `RequestId`；命中存在定义的 `GoToDefinition` 时返回显式 success payload。
    - 边界条件：paused 状态下的 `RequestCallStack` 立即回当前 snapshot，`DebugDatabaseFinished` 带 `RequestId + Revision`；legacy single-flight client 在兼容模式下仍可保持现有顺序语义。
    - 错误路径：running-state `RequestCallStack`、无效 path、未知 symbol 或 unsupported capability 都返回结构化 error/unsupported，而不是空 payload、fire-and-forget 或漂移到下一次暂停。
  - 测试命名：`Angelscript.CppTests.Debug.Protocol.RequestCorrelationAndStructuredErrors`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.5-T** 📦 Git 提交：`[Test/Debug] Test: add request correlation and error-contract coverage`

### 追加到 Phase 2：打通 JIT 运行态与 DebugServer 的统一调试目标

- [ ] **P2.4** 将 `PrecompiledData` 激活前验证前置到 `StaticJIT` 装载合同，禁止 packaged/runtime 继续走 `checkf` 或旧 offset 静默回退
  - 当前 precompiled/JIT cache 入口几乎只校验 `BuildIdentifier`；真正解引用 type/function/global/property reference 与 ABI/layout 对齐时，一部分路径会 `checkf` 直接终止，另一部分又会像 `GetPropertyOffset()` 那样静默回退到编译期旧 offset。到了 `Shipping/Test`，`AS_JIT_VERIFY_PROPERTY_OFFSETS` 还会被整体关掉，恰好把真实消费态的 ABI 哨兵拿掉。
  - 本项把 cache 激活改成显式 `runtime validation -> activation/fallback` 两阶段：先用 `ValidateForCurrentRuntime(...)` 或等价 report 收集缺失引用、property/type/layout 漂移，再决定“禁用当前 bundle / 降为 `InterpreterOnly` / 继续激活”；development/debug 可以保留严格 `checkf` 作为 assert 层，但 packaged/runtime 至少要有 cheap verify 和安全回退，不能继续让坏 cache 走崩溃或旧 offset。
  - 这一步与现有 `P2.2` 的 `FullyJitted/HybridFallback/InterpreterOnly` 分类互补：`P2.2` 收口的是函数级 codegen capability；本项收口的是 cache/bundle 激活前的 runtime validity，先保证“坏包不崩、坏 offset 不偷跑”，再谈命中后到底走 JIT 还是 VM。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 46 — “布局校验一旦发现属性偏移或类型尺寸漂移就直接 `checkf`，没有安全 fallback”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-39 — “precompiled/JIT cache 入口只校验 `BuildIdentifier`，坏引用会在深层解析中 `checkf` 崩溃；`GetPropertyOffset()` 还会静默回退旧 offset”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-18 / NewTest-4 — “当前 `StaticJIT` 自动化只有结构 round-trip，没有函数级 load/fallback 与坏 cache 回退覆盖”
    - [D] `ArchitectureReview/DebugAndToolchain_ArchReview.md` Arch-DT40 — “`TEST/SHIPPING` 关闭 `AS_JIT_VERIFY_PROPERTY_OFFSETS`，而这正削弱了 precompiled/JIT 真实消费态的 ABI 哨兵”
    - [E] `ReferenceComparison/CrossComparison.md` [D8] — “高价值 contract 应前置到 build/codegen/runtime 层，而不是继续靠断言或隐式退化兜底”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h` L12-L13 — `AS_JIT_VERIFY_PROPERTY_OFFSETS` 在 `Shipping/Test` 默认关闭；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2330-L2346 — `GetPropertyOffset()` 在 `PropertyReferences.Find(...) == nullptr` 时仍回退旧 offset；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2429-L2477 — property/type/align 校验只有在宏开启时才执行，且 mismatch 仍直接 `checkf`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledValidationTests.cpp`
- [ ] **P2.4** 📦 Git 提交：`[StaticJIT/Validation] Fix: validate precompiled bundle refs and abi before activation`
- [ ] **P2.4-T** 单元测试：验证坏 cache / 布局漂移会安全禁用当前 bundle 或降回 VM，而不是崩溃或继续用旧 offset
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledValidationTests.cpp`
  - 测试场景：
    - 正常路径：cache 引用和 ABI 签名都匹配时，bundle 正常激活，恢复出的函数/属性偏移与当前 runtime 一致。
    - 边界条件：packaged-like 路径下即使 `AS_JIT_VERIFY_PROPERTY_OFFSETS` 不可用，也会执行 cheap verify，并把受影响 bundle/function 明确降级，而不是继续盲信 `PrecompiledDataGuid`。
    - 错误路径：缺失 property reference、type/layout 漂移或损坏 cache 只会生成 validation report、禁用当前 JIT/precompiled 激活并回退 VM，不会触发 `checkf`，也不会静默沿用编译期旧 offset。
  - 测试命名：`Angelscript.CppTests.StaticJIT.PrecompiledValidation.SafeFallbackOnAbiDrift`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P2.4-T** 📦 Git 提交：`[Test/StaticJIT] Test: add precompiled validation fallback regressions`

### 追加到 Phase 3：统一 stop 过滤与 capability contract

- [ ] **P3.3** 让 `HasStopped` 显式携带 `BreakpointIds` 和稳定 `StopSource`，不再把 data breakpoint 伪装成 `exception`
  - 当前 stopped payload 只有 `Reason/Description/Text` 三个字符串；line breakpoint 的常驻状态也只保留 `TSet<int32> Lines`。这意味着请求里的 `Breakpoint.Id` 在命中路径里会直接丢失，而 data breakpoint 停下又继续把 `Reason` 写成 `exception`，前端只能靠行号和文本猜来源。
  - 本项把 breakpoint identity 提升为协议一等公民：line breakpoint 权威状态至少改成 `Line -> BreakpointIds`，`FStoppedMessage` 或等价的新 stopped payload 增加 `BreakpointIds` / `StopSource`，普通 line breakpoint、data breakpoint、step、pause、exception 统一通过 stop-source builder 出包。旧客户端如需兼容，必须靠 adapter version/version gate 显式降级，不能继续让新字段被 silent trailing bytes 吞掉。
  - 这一步承接 `P3.1` 的 watchpoint backend 稳定化和 `P1.5` 的 meta-layer：前者先保证命中/清理真实可靠，后者给 payload 扩字段提供 version gate；本项再把“究竟命中了谁、为什么停下”做成正式协议字段。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 81 — “停下事件完全丢失 breakpoint id，客户端无法知道究竟命中了哪一个 line/data breakpoint”
    - [A] `DebuggingAndJIT_Analysis.md` 发现 21 — “data breakpoint 命中被错误编码成 `exception` 停止原因”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-69 — “`HasStopped` 需要显式带回 `BreakpointIds`”；Issue-52 — “data breakpoint stop reason 不应再借用 `exception`”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-17 — “Shared client 默认接受 trailing bytes，协议字段扩展若不做 strict parse 会被静默漏读”
    - [E] `ReferenceComparison/CrossComparison.md` [D5-Deep] — “当前仓库把自有调试协议当正式 contract 维护，stop payload 不能继续靠文本和行号猜来源”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L129-L140 — `FStoppedMessage` 仍只有 `Reason/Description/Text`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L695-L699 — `FFileBreakpoints` 仍只保留 `TSet<int32> Lines`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1029-L1034 — line breakpoint 命中存储仍只按行去重，不保留 id；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L533-L538 — data breakpoint 停下仍把 `Msg.Reason` 写成 `exception`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerStoppedPayloadTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`
- [ ] **P3.3** 📦 Git 提交：`[Debug/Protocol] Feat: carry breakpoint ids and stop source in stopped payload`
- [ ] **P3.3-T** 单元测试：验证 line/data breakpoint 都会返回稳定 `BreakpointIds`，且 data breakpoint 不再伪装成 `exception`
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerStoppedPayloadTests.cpp`
  - 测试场景：
    - 正常路径：命中 line breakpoint 与 data breakpoint 时，stopped payload 都返回正确的 `BreakpointIds` 和匹配的 `Reason/StopSource`。
    - 边界条件：同一行挂多个 breakpoint id 时，返回值包含稳定排序的完整 id 列表；旧 adapter version 走兼容分支时也不会把 payload 读错。
    - 错误路径：`step` / `pause` / `exception` 这类非 breakpoint stop 返回空 `BreakpointIds`；strict test client 在出现尾部字段漂移时会直接失败，而不是默默忽略新增字段。
  - 测试命名：`Angelscript.TestModule.Debugger.Protocol.StoppedPayloadBreakpointIdentity`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P3.3-T** 📦 Git 提交：`[Test/Debug] Test: add stopped-payload breakpoint identity coverage`

## 单元测试总览补充（本轮）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.4` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerStepStateTests.cpp` | active-stop ownership、running-state 无 stray step、top-frame `StepOut` 直接完成 | P0 |
| `P1.5` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolCorrelationTests.cpp` | `RequestId` 归属、structured error、`RequestCallStack` 不再漂移到下次暂停 | P1 |
| `P2.4` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledValidationTests.cpp` | 坏 cache / ABI 漂移安全禁用、old offset 不再静默回退、合法 bundle 继续激活 | P0 |
| `P3.3` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerStoppedPayloadTests.cpp` | `BreakpointIds`、data breakpoint stop reason、旧版本兼容门控 | P1 |

## 验收标准补充

1. running 态误发的 `Continue/Step*` 不会再制造 `HasStopped(reason="step")`，顶层 `StepOut` 在调试协议上表现为“调用完成”而不是“下一行再停”。
2. `RequestCallStack`、`RequestVariables`、`RequestEvaluate`、`GoToDefinition` 都有可追踪的 `RequestId + Status`；running-state 请求不会再漂移到下一次暂停。
3. line/data breakpoint 命中后的 stopped payload 能稳定返回 `BreakpointIds`；data breakpoint 不再把 `Reason` 伪装成 `exception`。
4. packaged/runtime 遇到坏 precompiled/JIT cache、缺失 property reference 或 ABI/layout 漂移时，会安全禁用当前 bundle 或降回 VM，而不是 `checkf` 终止或继续沿用旧 offset。

## 风险与注意事项补充

### 风险

1. `RequestId`、`BreakpointIds` 与新的 stopped payload 字段都属于 wire 兼容面；如果没有 version/adaptor gate，旧 client 会把新 body 当旧格式误读。
2. packaged profile 下新增 cheap verify 必须控制启动成本，避免把“安全禁用坏 bundle”又做成“每次启动重跑完整 preprocessor/JIT 分析”。

### 已知行为变化

1. `RequestCallStack` / `GoToDefinition` / `Evaluate` / `Variables` 将从“空结果或延迟回包”变成“显式 error/unsupported 响应”；旧前端若依赖 silent no-op，需要同步适配。
2. data breakpoint 停止原因会从 `exception` 切到独立 stop-source，且 stopped payload 会新增 `BreakpointIds`；旧测试 helper 若继续接受 trailing bytes，会失去对这次协议升级的护栏，必须同步切到 strict parse。

---

## 深化 (2026-04-09 01:10:02)

### 追加到 Phase 1：收口调试会话、断点身份与协议 schema

- [ ] **P1.6** 把 line breakpoint 从 `ModuleName + Line` 升级成 file/section scoped authority，并补齐 verified/reject/rebind contract
  - 前面已有 `P3.3` 计划给 stopped payload 补 `BreakpointIds`，但当前 line breakpoint 的权威存储仍停留在 `module -> line set`：用户请求的 `Filename/Id`、编译产物里的 `Section`、热重载后的 rebind 结果并不是同一份 authority。这个缺口不补，后续 `BreakpointIds` 最多只能回传“命中了哪一行”，却无法稳定回答“它原本是哪一个 file-scoped breakpoint”。
  - 这一项要把“请求意图”和“解析结果”拆开：持久层至少保留 canonical filename、requested line、resolved section/line、breakpoint id 与 pending/unbound 状态；`SetBreakpoint` 必须始终给出 verified/reject/relocated 回执，`ClearBreakpoints` 只影响目标 file/section，`ReapplyBreakpoints()` 则在 compile/reload 后按最新源码锚点重新绑定，而不是继续只靠 module name 回挂旧行号。
  - 实现上优先复用当前 `SetBreakpoint` changed/remove 回包通道，不急着发明第二套消息；重点先把 file-scoped binding record 与 runtime `SectionBreakpoints` cache 对齐，再让 duplicate reject、non-executable-line relocate、pending-unbound 和 multi-file module 隔离都落到同一份 authority。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` — “按 filename 暂存的断点在模块后来编译成功后不会重新绑定；正常命中的断点没有 verified ack；module 未解析时按 filename 存储但 reapply 只按 module name 查找”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-4 / Issue-63 — “断点仍按 `ModuleName + Line` 聚合，多文件 module 会误命中/误清除，非可执行行还能留下永远不会命中的死断点”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-9 / NewTest-6 / NewTest-21 / NewTest-23 — “现有测试只盯 `BreakpointCount`，缺少 file-scoped clear、line-adjust ack、duplicate reject 覆盖”
    - [D] `ArchitectureReview/DebugAndToolchain_ArchReview.md` Arch-DT23 — “普通断点缺少 revision-aware rebind 与稳定 source identity，热重载后最多只会重新指向同数字行”
    - [E] `ReferenceComparison/CrossComparison.md` [D5-Identity] — “source identity 应挂在可复用 metadata 上，并同时服务 editor navigation 与 debugger，而不是在 runtime 再折叠回 module-line 集合”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L695-L699 — `FFileBreakpoints` 仍只有 `Module + TSet<int32> Lines`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L589-L600 — 运行时首次看到 `Section` 仍回退到 `Breakpoints[ModuleName]` 共享 store；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L936-L968、L1037-L1057 — `Set/ClearBreakpoint` 仍按 `ModuleDesc->ModuleName` 或 filename fallback 选 key，且只在 relocated 或 reject 时回包；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1197-L1219 — `ReapplyBreakpoints()` 仍只按 module name 重新挂 `Module`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointAuthorityTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp`
- [ ] **P1.6** 📦 Git 提交：`[Debug/Breakpoints] Refactor: make line breakpoint authority file scoped with ack and rebind`
- [ ] **P1.6-T** 单元测试：验证 line breakpoint 的 file-scoped 绑定、回执与热重载重绑合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointAuthorityTests.cpp`
  - 测试场景：
    - 正常路径：在单文件 fixture 的空白行设置断点，收到 `SetBreakpoint` relocated ack，继续执行时恰好命中回包给出的 file/line。
    - 边界条件：同一 module 下两个文件共享相同行号时，清除文件 A 的断点不会删除文件 B；插入非代码行并重编译后，同一 `Breakpoint.Id` 会重绑到新 resolved line。
    - 错误路径：重复设置同一 file/line 返回 rejection/移除 ack；目标 file/section 不存在时断点进入 `PendingUnbound` 或等价拒绝状态，不会 ghost hit 到其它文件。
  - 测试命名：`Angelscript.TestModule.Debugger.BreakpointAuthority.FileScopedBindingAndAck`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.6-T** 📦 Git 提交：`[Test/Debug] Test: cover file-scoped breakpoint ack and rebind contract`

- [ ] **P1.7** 把 `HandleMessage()` 升级成带 `MessageSpec` 的 session-aware dispatch，统一入站 admission、paused gate 与出站 route policy
  - 现有 `P1.1` 只处理“状态要按 client 隔离”，但当前源码连“谁有资格发控制消息、谁有资格收 stop/event”都还没有 authoritative gate。只要 socket 连上端口，不进 `StartDebugging` 也能 `Pause/Continue/Step*`、改断点、读变量；同时 `HasStopped/HasContinued/ClearDataBreakpoints` 还会广播给所有 `Clients`。这意味着 session 数据结构即使拆开，协议入口和路由层仍然会继续串台。
  - 本项把 `HandleMessage()` 从 if/else 巨链收口成带 metadata 的 dispatch 表：每类消息显式声明 `bRequiresDebugSession`、`bRequiresPausedState`、`bStrictParse` 与 `Route`；协议入口先做 admission/reject，再进业务 handler。第一阶段不追求改 wire format，目标是先让未握手 socket、database-only client 和 expired session 不能再默默旁路控制或观测调试会话。
  - 实现上优先覆盖高风险消息：`Pause/Continue/Step*`、`StopDebugging`、`Set/ClearBreakpoint`、`Set/ClearDataBreakpoints`、`RequestCallStack/Variables/Evaluate`。`RequestDebugDatabase`、握手和 capability 查询保留为非 debugging-session 可用；所有 stop/continue/data-breakpoint-clear 事件则改由 `ToDebuggingSessions` 路由，不再借 `SendMessageToAll()` 走最宽泛广播。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` — “任何已连接 socket 都能在未握手状态下暂停、步进、改断点并读取调试数据；停止/继续/DataBreakpoint 清理事件会广播给所有已连接 socket”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-35 / Issue-55 — “入站授权与出站广播都忽略 session 成员关系；`HandleMessage()` 缺少统一 metadata 层，解析/鉴权/route 全靠分支手写”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-20 / NewTest-22 — “handshake 现有 helper 会吞掉额外 envelope，且最后一个 debugging client 断开后的 clean state 没有独立回归”
    - [D] `ArchitectureReview/DebugAndToolchain_ArchReview.md` Arch-DT8 / Arch-DT29 / Arch-DT50 — “remote attach 仍缺 admission/trust boundary，协议 public surface 把 socket/wire schema 与调试语义暴露在一起”
    - [E] `ReferenceComparison/CrossComparison.md` [D5] — “当前仓库自己拥有调试 contract，这条 contract 需要明确 session owner 和 event boundary，不能继续依赖所有连接串行配合的隐式假设”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L649-L666 — `SendMessageToAll()` 仍无条件遍历 `Clients` 广播；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L673-L698 — `PauseExecution()` 继续把 `HasStopped/HasContinued` 广播给所有连接；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L821-L926 — `Pause/Continue/Step*`、`StopDebugging`、`RequestCallStack` 等入口没有任何 `ClientsThatAreDebugging.Contains(Client)` 或 paused-state gate；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L733-L746 — client cleanup 只做集合摘除，并没有统一 route/admission contract
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugDispatchSpecTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSessionAuthorizationTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`
- [ ] **P1.7** 📦 Git 提交：`[Debug/Protocol] Refactor: add session-aware dispatch spec and route policy`
- [ ] **P1.7-T** 单元测试：验证 session admission、paused gate 和 event routing 都由同一套 message spec 驱动
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSessionAuthorizationTests.cpp`
  - 测试场景：
    - 正常路径：client A 完成 `StartDebugging`，client B 仅订阅 `RequestDebugDatabase`；A 能正常收到 `HasStopped/HasContinued` 与 callstack，B 只收到 database/diagnostics 流。
    - 边界条件：最后一个 debugging client `Disconnect` 后，server 会退出 debugging state 并清空 route membership；新的 client 重新握手后不继承旧 session 的路由残留。
    - 错误路径：未握手 socket 或 database-only client 发送 `Pause`、`SetBreakpoint`、`RequestEvaluate` 时被显式拒绝/无副作用处理；未知 message type 不再 silent no-op。
  - 测试命名：`Angelscript.TestModule.Debugger.Session.AuthorizationAndRoutePolicy`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.7-T** 📦 Git 提交：`[Test/Debug] Test: add session admission and route policy coverage`

- [ ] **P1.8** 把 `CallStack` frame 改成对称 schema，并为 BP/C++ 帧写稳定 `LineNumber` 哨兵
  - 现有 `P1.5` 处理的是请求归属，但 `CallStack` 本身的 body 仍然不对称：`ModuleName` 只有写侧没有显式 presence/version，BP/C++ frame 还把未初始化的 `LineNumber` 发上 wire。只要栈里混有多帧或 adapter version 打开了 `ModuleName`，第二帧开始就可能读错结构；即使不读错，非脚本 frame 的 line 也不是 deterministic 数据。
  - 本项把 `FAngelscriptCallFrame` 变成显式 schema：至少在当前 version gate 下无条件对称读写 `ModuleName`，并把未来扩展留给 `bHasModuleName` 或更高版本 header；同时把 BP/C++ frame 的 `LineNumber` 固定成 `-1` 或等价哨兵值，让 tests 与前端都能把“无源码行”的 frame 识别成正式合同，而不是偶然的随机栈值。
  - 实现时同步把测试客户端切到 strict parse，避免修完服务端后 client helper 继续把尾字段漂移当成功。这样后续无论再给 `CallStack` 补 `FrameId` 还是 `SnapshotId`，都不会再重演“服务端写了新字段，测试却完全没读”的问题。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` — “`CallStack` 的 `ModuleName` 字段没有 presence/version 标记；`CallStack` 协议的 `ModuleName` 只能写不能读；Blueprint/C++ 帧序列化了未初始化的 `LineNumber`”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-34 — “`CallStack` frame 序列化格式不对称，`ModuleName` 会错位污染后续帧且 BP/C++ frame 发送未初始化 `LineNumber`”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-17 / NewTest-2 / NewTest-7 — “Shared client 默认接受 trailing bytes，且现有 debugger tests 没有真正覆盖 mixed frame / multi-frame `CallStack`”
    - [D] `ArchitectureReview/DebugAndToolchain_ArchReview.md` Arch-DT21 / Arch-DT34 — “协议演进点分散，缺少 schema 层和严格错误合同”
    - [E] `ReferenceComparison/CrossComparison.md` [D5] — “调试协议是仓内正式 contract，frame schema 必须可回归、可严格解析，不能继续靠旧 client 容错吞字段”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L196-L212 — `FAngelscriptCallFrame::operator<<` 仍只在 `ModuleName.IsSet()` 时追加字段，且没有 presence bit / version 分支；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1400-L1462 — Blueprint/C++ frame 只写 `Name/Source/ModuleName`，没有给 `LineNumber` 赋稳定值；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1467-L1474 — 只有 script frame 路径显式写 `LineNumber` 和 `ModuleName`
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugCallStackProtocolTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`
- [ ] **P1.8** 📦 Git 提交：`[Debug/Protocol] Fix: make callstack frame schema symmetric and deterministic`
- [ ] **P1.8-T** 单元测试：验证 mixed-frame `CallStack` 的 wire format 对称、strict parse 可回归，且 BP/C++ frame 行号稳定
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugCallStackProtocolTests.cpp`
  - 测试场景：
    - 正常路径：script + BP/C++ 混合多帧 round-trip 后，`Name/Source/ModuleName/LineNumber` 与原始输入逐帧一致，第二帧开始不再错位。
    - 边界条件：adapter v0/v1/v2 下分别验证无 `ModuleName`、有 `ModuleName`、以及 `LineNumber=-1` 的非脚本 frame 哨兵值都能稳定读写。
    - 错误路径：出现 trailing bytes、缺失必填 frame 字段或旧 helper 未完整消费 body 时，strict parse 直接失败，而不是把后续帧错位读成合法 `CallStack`。
  - 测试命名：`Angelscript.CppTests.Debug.Protocol.CallStackFrameSchemaSymmetry`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.8-T** 📦 Git 提交：`[Test/Debug] Test: add callstack schema symmetry regressions`

- [ ] **P1.9** 收口 `Variables/Evaluate` 的被动读取合同，禁止隐式 getter 副作用并恢复 stable monitor-address 语义
  - 当前 `Variables/Evaluate` 既不是真正的 passive read，也不是真正的 structured eval：展开 scope 会自动枚举 `readonly Get*` 并执行用户 getter；getter 执行后又不检查 `Context->Execute()` 结果，异常/abort 也会继续读 return slot；另一方面，成功求值后普通路径还会把 `CurrentValue.Address` 清空，导致 V2 本该返回的 `ValueAddress/ValueSize` 在最常见场景里退化成 `0`。这条链既可能改写用户状态，又破坏了 data breakpoint 的地址闭环。
  - 本项要把求值合同拆成三层：默认 `Variables`/被动展开只做 memory-only 读取；显式 allowlist 的 getter 才允许执行；协议层对 success/error/address 也要分开建模，不能再用“空值 + 零地址”同时代表“getter 失败”“temporary expression”“legacy v1 分支”和“普通值被错误 scrub 掉地址”。
  - 实现上建议先引入 `EDebuggerEvaluationPolicy` 或等价 flags，把 `Bind_UStruct.cpp` 里自动 `Get*` 展开挂到 policy/metadata；然后重写 `GetDebuggerValueFromFunction()` 的执行契约，只在 `asEXECUTION_FINISHED` 且无异常时导出 return value；最后把 `Evaluate/Variables` 的 response 升级成带 success/error 与 monitor-address 生命周期分离的 payload，为后续 `P1.5` 的 request/response 元层提供可靠 body。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` — “watch/locals 求值会自动执行用户 getter；getter 求值完全忽略 `Execute()` 结果；`RequestEvaluate` 在成功路径上把普通值地址清成了 `0`”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-58 / Issue-62 — “`Variables/Evaluate` 会执行 getter 且失败伪装成成功；V2 `ValueAddress` 对大多数 evaluate 结果恒为 `0`”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` NewTest-7 / NewTest-10 / NewTest-20 — “叶子帧 `Evaluate/Variables` 还没有完整覆盖，`ValueAddress` 驱动 data breakpoint 的闭环与 legacy v1/v2 分支都缺回归”
    - [D] `ArchitectureReview/DebugAndToolchain_ArchReview.md` Arch-DT15 — “调试对象模型仍依赖 live context 和 `Path DSL`，对象 identity / value identity 过于脆弱”
    - [E] `ReferenceComparison/CrossComparison.md` [D5-ValueModel] — “当前 AS 的强项是 `Variables/Evaluate -> ValueAddress + ValueSize -> DataBreakpoint` 这一条统一变量实体链，修复应保留而不是继续削弱它”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp` L480-L503、L533-L540 — `GetDebuggerScope/GetDebuggerMember` 仍自动枚举 `readonly Get*` 并调用 `GetDebuggerValueFromFunction()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp` L703-L734 — `Context->Execute()` 之后没有检查执行结果或异常，仍直接读取 return slot；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1081-L1128 — `RequestVariables/RequestEvaluate` 仍只回裸 `Variables/Evaluate` payload，没有 success/error contract；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L2684-L2688 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` L727-L744 — evaluate 普通路径仍清空 `CurrentValue.Address`，`GetAddressToMonitor()` 会退回到已被 scrub 的地址
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerEvaluateContractTests.cpp`
- [ ] **P1.9** 📦 Git 提交：`[Debug/Values] Fix: separate passive reads from getter execution and preserve monitor address`
- [ ] **P1.9-T** 单元测试：验证 `Variables/Evaluate` 默认是 passive read，并且 V2 monitor address 在安全值路径上保持可用
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerEvaluateContractTests.cpp`
  - 测试场景：
    - 正常路径：稳定 local/member/global 在 adapter v2 下返回正确值与非零 `ValueAddress/ValueSize`；显式 allowlist 的 safe getter 仍可成功求值。
    - 边界条件：adapter v1 仍只返回值文本不返回地址；temporary expression 或无法稳定监视的值继续返回 `ValueAddress=0`。
    - 错误路径：自动 scope 展开不会触发带副作用 getter；getter 抛异常或 `Execute()` 未完成时返回结构化 failure/diagnostic，而不是默认值或伪成功 payload。
  - 测试命名：`Angelscript.TestModule.Debugger.Values.PassiveReadAndMonitorAddressContract`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P1.9-T** 📦 Git 提交：`[Test/Debug] Test: add evaluate and variable contract coverage`

### 新增 Phase 4：收口 StaticJIT 的进程级生命周期状态

- [ ] **P4.1** 把 `StaticJIT` 的 process-global registries 下沉到 shared-state / bundle-scoped authority，消除 multi-engine 与 multi-bundle 的残留状态
  - 现有 `P2.2`、`P2.3`、`P2.4` 已经处理函数级 fallback 与 precompiled validation，但 `StaticJIT` 仍有一整层“引擎销毁后仍留在进程里”的 authority：`StaticNames` 全局表在 precompile save/load 与 preprocessor 之间共享；`GScriptNativeForms` 长期持有 raw `asIScriptFunction*`；compiled bundle 仍只允许一个 `FStaticJITCompiledInfo` singleton；JIT mismatch 时直接 `FJITDatabase::Clear()` 又会把全局 lookups 整体清空。只要同一进程经历多轮 engine create/destroy、cache mismatch 或多 bundle 载入，这些状态就会变成非 deterministic 的交叉污染源。
  - 本项的目标不是继续扩 fallback，而是把“编译产物注册”“运行期激活”“engine/shared-state teardown”分成不同 ownership：static-name 表绑定到 shared state；native form registry 绑定到当前 engine 或 generation session；compiled bundle 用 keyed registry 而不是 single active pointer；runtime mismatch 只禁用当前 bundle/activation，不再靠 `Clear()` 破坏所有已注册 compiled lookup。
  - 实现上建议优先把 immutable compiled metadata 与 mutable activated lookups 分层：static constructors 只负责把 bundle/function facts 注册进只读 registry，真正面向本次 engine 的 `Functions/Lookups/StaticNames` 则由 activation layer 装配和清理。这样 multi-engine tests 才能稳定证明“同一进程第二次启动仍能得到与第一次一致的 JIT/precompiled contract”，而不是偶然继承上一次残留。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` — “`GScriptNativeForms` 只增不减；`FJITDatabase::Clear()` 会永久清空静态注册 compiled-function 表；`StaticNames/StaticNamesByIndex` 跨 engine 生命周期残留；第二份 transpiled bundle 会在静态初始化期断言”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-60 / Issue-61 / Issue-74 — “static-name 表、native form registry 与 compiled-info registry 都缺少 engine/bundle-scoped owner 和 teardown contract”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-18 / NewTest-3 / NewTest-4 — “现有 StaticJIT 自动化仍缺真正的运行期/多轮 load/fallback 覆盖，无法兜住多 engine 生命周期回归”
    - [D] `ArchitectureReview/DebugAndToolchain_ArchReview.md` Arch-DT18 / Arch-DT32 / Arch-DT49 — “bundle 启用/失效边界仍是全局 singleton，bytecode lowering 与运行期链接大量依赖 static ctor + global lookup patching”
    - [E] `ReferenceComparison/CrossComparison.md` [D8-ColdStart] — “冷启动与预热成本可以前置摊销，但前提是 artifact/cache owner 明确、可清理，不能继续依赖 process-global 脏状态续命”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` L578-L579 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L2048-L2053 — `StaticNames/StaticNamesByIndex` 仍是 process-global 静态表并在编译时直接追加；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2436-L2440、L2662-L2663 — load path 仍 `check(StaticNames.Num()==0)` 后往全局表追加，save path 直接序列化全局表；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` L27-L57 — `GScriptNativeForms` 仍是 file-static `TMap<asIScriptFunction*, FScriptFunctionNativeForm*>`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp` L19-L30、L38-L47 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1551-L1555、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` L24-L36 — compiled info 仍只有一个 active singleton，JIT functions 在全局 database 注册，mismatch 时直接 `Clear()` 整张 lookup 表
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineStaticJITStateTests.cpp`
- [ ] **P4.1** 📦 Git 提交：`[StaticJIT/Lifecycle] Refactor: scope static jit registries by shared state and bundle`
- [ ] **P4.1-T** 单元测试：验证 multi-engine / multi-bundle 下的 StaticJIT registry 生命周期不再依赖 process-global 残留
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineStaticJITStateTests.cpp`
  - 测试场景：
    - 正常路径：单 bundle 场景下，engine create -> destroy -> recreate 后 `StaticNames`、native form 与 compiled lookup 都能重新初始化并稳定命中同一 JIT 函数。
    - 边界条件：同一进程先后加载两个不同 bundle / 两轮不同 precompiled cache 时，只禁用当前 mismatch bundle，不污染另一份 compiled metadata 或下一轮 engine activation。
    - 错误路径：销毁旧 engine 后不会残留 stale `asIScriptFunction*`、旧 static-name 索引或被永久清空的 JIT lookup；第二轮启动不再因为 `check(StaticNames.Num()==0)` 或 single active compiled info 断言失败。
  - 测试命名：`Angelscript.CppTests.StaticJIT.Lifecycle.MultiEngineStateIsolation`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P4.1-T** 📦 Git 提交：`[Test/StaticJIT] Test: add multi-engine registry lifecycle coverage`

## 单元测试总览补充（本轮深化）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P1.6` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointAuthorityTests.cpp` | file-scoped breakpoint authority、relocated/reject ack、reload rebind、多 file 同 module 隔离 | P0 |
| `P1.7` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSessionAuthorizationTests.cpp` | session admission、route policy、database-only client 隔离、unknown/unauthorized message reject | P0 |
| `P1.8` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugCallStackProtocolTests.cpp` | mixed-frame `CallStack` round-trip、strict parse、non-script frame line sentinel | P1 |
| `P1.9` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerEvaluateContractTests.cpp` | passive read、getter failure surfacing、monitor address 保持、legacy v1/v2 行为分支 | P0 |
| `P4.1` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineStaticJITStateTests.cpp` | multi-engine create/destroy、multi-bundle activation、stale registry 清理 | P1 |

## 验收标准补充（本轮深化）

1. line breakpoint 的权威状态能唯一回答“请求来自哪个 file/section/id、当前绑定到哪一行、当前是否 unbound”，`SetBreakpoint` 对 relocate / duplicate / reject 都有稳定回执。
2. 未握手或仅 database 订阅的连接不能再发送控制消息，也不会再收到别的 session 的 `HasStopped`、`HasContinued` 或 `ClearDataBreakpoints`。
3. mixed-frame `CallStack` 在 strict parse 下保持对称；BP/C++ frame 的 `LineNumber` 为稳定哨兵值，不再出现随机行号或多帧错位。
4. `Variables/Evaluate` 默认不会因自动 getter 执行改写脚本状态；getter 失败会返回结构化 failure；stable value 在 V2 下仍能提供可继续监视的 `ValueAddress/ValueSize`。
5. 同一进程多轮 engine create/destroy、precompiled mismatch 与 multi-bundle 场景下，不会再因为 process-global StaticJIT registries 产生 stale state、全局清空或 singleton 断言。

## 风险与注意事项补充（本轮深化）

### 风险

1. `P1.6` 与 `P1.7` 都会触碰 debugger wire contract 和 authority model；如果 file-scoped breakpoint state、session route 和 existing adapter fallback 没有一起评审，容易出现“服务端语义已修但旧 client 仍按旧假设组包/解包”的阶段性兼容问题。
2. `P1.9` 关闭默认 getter 执行后，变量面板可见字段数量会下降；若 allowlist、diagnostic 和文档没有同步到位，用户会把安全修复误判成“调试器少字段/坏了”。
3. `P4.1` 会重划 StaticJIT 的 owner boundary；若 immutable compiled facts 与 per-engine activated lookups 切分不当，可能把当前的 process-global 污染换成“生命周期过短导致运行期找不到 JIT 元数据”的另一类回归。

### 已知行为变化

1. `SetBreakpoint` 将从“多数正常路径不回包”变成“所有请求都有 verified/reject/relocated 结果”；旧前端若默认只等待 changed/remove 事件，需要同步适配回执处理。
2. `HasStopped/HasContinued`、`ClearDataBreakpoints` 与后续 debug event 将只发给 active debugging sessions；依赖 database-only 连接旁听 stop event 的测试 helper 或工具会失效。
3. `CallStack` 的 non-script frame 将显式返回 `LineNumber=-1` 或等价哨兵；任何把非脚本 frame 行号当成真实源码位置的旧断言都要收紧。
4. `Variables/Evaluate` 不再默认执行所有 `readonly Get*`；未进 allowlist 的派生字段可能不再自动出现在 scope 展开里，前端需要展示 failure/unsupported 或按需触发显式 evaluate。
5. StaticJIT 在 mismatch / multi-bundle 场景下会更早、更明确地禁用当前 bundle 或局部 activation，而不是继续沿用历史全局注册表副作用；旧测试若隐式依赖这些残留状态，需要改成显式 setup/teardown。

---

## 深化 (2026-04-09 01:22)

以下条目补足当前文档尚未覆盖的 `SCRIPT_CALL_NATIVE` / native call bridge 安全合同缺口；不重复 `Documents/Plans/Plan_StaticJITUnitTests.md` 的纯覆盖扩面，也不把 `asIJITCompilerV2` 或更大的 ThirdParty API 迁移混进本轮。

### 新增 Phase 5：收口 JIT native fallback bridge 的安全合同

- [ ] **P5.1** 让 `SCRIPT_CALL_NATIVE` fallback 复用解释器的 null-object / `WorldContext` preflight 合同
  - 当前 `MakeDynamicCall()` 在 native fast-path 不可用时会落到 `SCRIPT_CALL_NATIVE(ScriptFunc, ...)`，最终进入 `FStaticJITFunction::ScriptCallNative()`；但这条 bridge 只在 `ICC_GENERIC_METHOD` 路径做了空对象检查，既没有复制解释器 `CallFunctionCaller()` 对 `ICC_THISCALL+` 的 null-object 拦截，也没有复制 `asTRAIT_USES_WORLDCONTEXT` / `BlueprintThreadSafe` 的前置保护。结果是同一个 native/system function，在解释器里会抛脚本异常，在 JIT miss fallback 里却可能把空 `this` 或无效 `WorldContext` 直接送进 native caller。
  - 本项要把“JIT miss 只影响性能，不改变安全语义”收口成正式合同：优先抽共享 preflight helper，让解释器和 `SCRIPT_CALL_NATIVE` fallback 统一执行 `WorldContext`、thread-safe、null-object 检查；若短期不适合抽 helper，至少先把解释器现有 guard 逐字对齐到 `StaticJITHeader.cpp` 的两个调用分支，并确保 JIT 路径也通过 `SetException(...)` 走脚本异常退出，而不是落到 native crash 或副作用执行。
  - 这条线不追求在本项里顺手统一所有 native call ABI；完成标准是解释器路径与 JIT fallback 对“空对象 / 无效 `WorldContext`”给出同类脚本异常、同类拒绝时机和同类无副作用结果。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` — “`SCRIPT_CALL_NATIVE` fallback 没有复用解释器的 null object / `WorldContext` 安全检查”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-30 — “JIT miss 后同一 native/system function 在解释器与 JIT fallback 下异常语义分叉”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-18 — “现有 `StaticJIT` 直连测试全部跑在 `EditorContext`，实际上没有执行任何 transpiled code”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` L990-L1008 — `MakeDynamicCall()` 仍通过 `SCRIPT_CALL_NATIVE(ScriptFunc, ...)` 进入 JIT/native fallback；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp` L183-L191 — 只有 `ICC_GENERIC_METHOD` 路径对空对象调用 `SetException(EJITException::NullPointer)`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp` L223-L230、L233-L299 — `sysFunc->caller.IsBound()` 路径仍直接把 object pointer 压入 `FunctionArgs`，且没有任何 `WorldContext` / `BlueprintThreadSafe` guard；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` L5164-L5198 — 解释器 `CallFunctionCaller()` 仍先执行 `asTRAIT_USES_WORLDCONTEXT` 检查，再对 `ICC_THISCALL+` 的空对象调用 `SetInternalException(TXT_NULL_POINTER_ACCESS)`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeFallbackPreflightTests.cpp`
- [ ] **P5.1** 📦 Git 提交：`[StaticJIT/NativeFallback] Fix: align native fallback preflight with interpreter guards`
- [ ] **P5.1-T** 单元测试：验证解释器与 `SCRIPT_CALL_NATIVE` fallback 在 null-object / `WorldContext` 上保持同一 preflight 语义
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeFallbackPreflightTests.cpp`
  - 测试场景：
    - 正常路径：同一个 native method / `WorldContext`-合法函数分别走解释器执行与强制 `SCRIPT_CALL_NATIVE` fallback，返回值与副作用一致。
    - 边界条件：`ICC_GENERIC_METHOD` 与 `ICC_THISCALL` 两条 bridge 都覆盖；`WorldContext` 可用时不误报，`BlueprintThreadSafe` 场景下合法函数仍可继续执行。
    - 错误路径：空对象调用与无效 `WorldContext` 调用在两条路径上都返回同类脚本异常 / diagnostic，不触发 native crash，也不会越过 guard 执行副作用。
  - 测试命名：`Angelscript.TestModule.Native.Fallback.PreflightParity`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.1-T** 📦 Git 提交：`[Test/Native] Test: add native fallback preflight parity coverage`

- [ ] **P5.2** 把 native call bridge 的参数槽预算显式化，并在超限时 fail-fast，移除 `FunctionArgs[32]` 写栈风险
  - 当前 `FStaticJITFunction::ScriptCallNative()` 与解释器 `asCContext::CallFunctionCaller()` 都把 native 参数封送进固定 `void* FunctionArgs[32]`；`ArgIndex` 会随着 `this`、`passFirstParamMetaData`、每个普通参数以及 `ttQuestion` 额外槽位持续增长，但两边都没有任何上界校验。与此同时，脚本函数参数描述本身是动态的，`CalculateParameterOffsets()` / `GetParamCount()` 也没有 32 槽契约，这意味着高参数量 native/system function 目前可以直接把桥接缓冲写出边界。
  - 本项先把安全基线拉正，而不是一次性追求“无限参数全支持”：第一落点是抽共享 `RequiredSlots` 计数逻辑，并让解释器/JIT fallback 在超出预算时统一走脚本异常 fail-fast，禁止继续写栈；若在同项内实现风险可控，再把固定 32 槽缓冲升级成共享动态参数缓冲，但验收下限始终是“绝不再越界写栈”。
  - 这条线的重点是 memory safety 和 bridge contract 显式化，不在本项里顺手扩更多 calling-convention 形态；现有 `ASSDK` / `NativeExecution` 主线必须先保持兼容，再考虑把超限场景从“安全拒绝”提升到“完全支持”。
  - 来源：
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-78 — “`ScriptCallNative` / `CallFunctionCaller` 用固定 `FunctionArgs[32]` 封送 native 参数，effective arg count 可直接越界写栈”
    - [C] `TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-18 — “现有 `StaticJIT` 直连测试全部跑在 `EditorContext`，实际上没有执行任何 transpiled code”
    - [E] `ReferenceComparison/CrossComparison.md` [D8] — “参考插件会把 native bridge / thunk 的 ABI 形状显式化，而不是把参数预算藏在隐式 bridge 细节里”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp` L217-L287 — JIT fallback 仍使用固定 `FunctionArgs[32]`，`ArgIndex` 随 `this`、metadata 和 `ttQuestion` 额外槽位增长，但没有任何容量判断；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` L5160-L5254 — 解释器 `CallFunctionCaller()` 复制了同样的 `FunctionArgs[32]` 与无上界写入；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp` L647-L675、L1463-L1465 — `parameterOffsets` 与 `GetParamCount()` 仍按动态参数列表计算，没有为 native bridge 提供 32 槽上限合同。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeCallBridgeBudgetTests.cpp`
- [ ] **P5.2** 📦 Git 提交：`[StaticJIT/NativeFallback] Fix: guard native call bridge slot budget`
- [ ] **P5.2-T** 单元测试：验证 native call bridge 在高参数量、metadata 与 `ttQuestion` 组合下不会再越界写栈
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeCallBridgeBudgetTests.cpp`
  - 测试场景：
    - 正常路径：现有 `CDecl` / `Generic` / `Thiscall` 与携带 metadata 的 under-budget native 调用继续通过，参数顺序和返回值不回归。
    - 边界条件：`RequiredSlots == 32` 的组合调用保持可执行；包含 `ttQuestion` 额外槽位和 `passFirstParamMetaData` 的组合仍按预算正确计数。
    - 错误路径：`RequiredSlots > 32` 时，解释器与 `SCRIPT_CALL_NATIVE` fallback 都返回显式脚本异常 / diagnostic，而不是栈破坏、参数错位或进程崩溃。
  - 测试命名：`Angelscript.TestModule.Native.Fallback.ArgSlotBudget`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P5.2-T** 📦 Git 提交：`[Test/Native] Test: add native call bridge slot-budget coverage`

### 单元测试总览补充（本轮深化）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P5.1` | `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeFallbackPreflightTests.cpp` | null-object parity、`WorldContext` parity、JIT miss fallback 无副作用拒绝 | P0 |
| `P5.2` | `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptNativeCallBridgeBudgetTests.cpp` | slot budget 计数、`ttQuestion`/metadata 组合、over-budget fail-fast | P0 |

### 验收标准补充（本轮深化）

1. 同一个 native/system function 在解释器与 `SCRIPT_CALL_NATIVE` fallback 下，对空对象和无效 `WorldContext` 的拒绝时机、异常类型与副作用边界保持一致，不再出现“解释器报脚本异常、JIT fallback 继续进 native”的分叉。
2. native call bridge 的参数预算变成显式合同；`RequiredSlots > 32` 或等价超限场景只能产生可诊断脚本异常，不能再写出 `FunctionArgs` 边界或导致参数错位执行。

### 风险与注意事项补充（本轮深化）

#### 风险

1. `P5.1` 同时触碰 `StaticJIT` bridge 和 ThirdParty 解释器 bridge；若两边异常文本、线程局部状态或 `Execution.bExceptionThrown` 映射不一致，可能把 crash 修成“结果不崩但错误文本漂移”的新回归。
2. `P5.2` 把隐式预算改成显式 fail-fast 后，少数历史上“碰巧能跑”的高参数量 native 注册可能开始被正式拒绝；这属于必要收紧，但需要提前告知测试与绑定维护者，不要再把未定义行为误当成兼容能力。

#### 已知行为变化

1. JIT miss 后调用 native instance method 或 `WorldContext` 受限函数时，运行结果会从“可能直接进 native / 触发原生崩溃”收紧为“明确脚本异常或 diagnostic”；依赖旧副作用的测试都需要重写为断言错误合同。
2. 超出 native bridge 参数预算的调用会从“未定义行为”变成“显式脚本异常”；若后续单独推进动态参数缓冲，再在 sibling 条目里把这类调用从“安全拒绝”提升为“正式支持”，不要在本轮文档里混写两个完成标准。

---

## 深化 (2026-04-09 01:33)

以下条目补足当前文档尚未明确承接的 3 个残余缺口：`RequestEvaluate` 的 body/failure wire hygiene、客户端主动 `ClearDataBreakpoints` 的 selective-clear 语义，以及 Windows watchpoint 更新线程的 duplicated handle 生命周期。它们都已经有 AutoPlans 证据与当前源码对位，但还没有在现有 `P1-P5` 中形成独立执行项。

### 新增 Phase 6：补齐剩余协议与 watchpoint 生命周期合同

- [ ] **P6.1** 把 `RequestEvaluate` 的 body-level parse 与失败 payload 变成显式合同，杜绝未初始化 frame/address 上 wire
  - 当前 `P1.3` 只修“frame 越界会 alias 到 frame 0”，`P1.5` 只修 request correlation，`P1.9` 只修 passive read/getter contract；但 `RequestEvaluate` 入口自身仍然把 `DefaultFrame` 当未初始化局部读取，并在失败时发送未初始化的 `FAngelscriptVariable`。这不是单纯的 UX 缺口，而是协议层会把随机 frame 和随机 `ValueAddress/ValueSize` 直接暴露给客户端。
  - 本项先把 `RequestEvaluate` 收口成严格 body parser：完整读取 `Path + DefaultFrame` 后立即检查 `Datagram->IsError()` / `Datagram->AtEnd()`，不允许截断 body 或尾字段继续落到 `GetDebuggerValue(...)`；同时把 `FAngelscriptVariable` 改成稳定零初始化对象，确保失败响应在 v2 下也只能返回 `0` 地址和空值，而不是未定义栈内容。
  - 这一步不重复 `P1.5` 的 `RequestId + Status` 大改；它是进入 request/error 元层之前的基础热修，先把最危险的未初始化读写与 malformed body 行为堵住，再让后续 structured error contract 建在确定的 payload 上。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 25 — “`RequestEvaluate` 读取截断消息体时会把未初始化 `DefaultFrame` 继续传入 `GetDebuggerValue(...)`”
    - [A] `DebuggingAndJIT_Analysis.md` 发现 68 — “`RequestEvaluate` 失败时，V2 响应会把未初始化栈数据当成 `ValueAddress` / `ValueSize` 发给客户端”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-22 — “`RequestEvaluate` 在截断请求和失败响应上都会把未初始化数据发到 wire”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-7 / NewTest-20 — “`RequestEvaluate`/`RequestVariables` 只有 happy-path 与 legacy 分支覆盖，没有 body-level 截断和失败响应字段归零回归”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT21 — “协议层缺少严格错误合同与字段级 schema 护栏，当前测试主要停留在成功 round-trip”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1107-L1128 — `RequestEvaluate` 仍直接读取未初始化的 `DefaultFrame`，且对 `Datagram->IsError()` / `AtEnd()` 没有任何检查；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L416-L438 — `FAngelscriptVariable` 仍未为 `ValueAddress` / `ValueSize` 提供默认值，但 v2 分支会无条件序列化这两个字段。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerEvaluateFailureTests.cpp`
- [ ] **P6.1** 📦 Git 提交：`[Debug/Protocol] Fix: harden evaluate request parsing and zero-init failure payloads`
- [ ] **P6.1-T** 单元测试：验证 `RequestEvaluate` 在完整 body、legacy/v2 分支与 malformed request 下都返回确定结果
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerEvaluateFailureTests.cpp`
  - 测试场景：
    - 正常路径：完整的 `RequestEvaluate(Path, Frame)` 在 adapter v2 下返回正确的 `Name/Value/Type`，且稳定可监视值仍保留非零 `ValueAddress/ValueSize`。
    - 边界条件：adapter v1 的同一请求仍能成功反序列化，但 `ValueAddress == 0`、`ValueSize == 0`；带额外尾字段的 body 会被明确拒绝，而不是被默默容忍。
    - 错误路径：缺失 `DefaultFrame`、求值失败或无效表达式时，不会读取未初始化 frame，也不会把随机地址写入响应；失败 payload 只能返回空值与零地址，且同一 session 后续合法请求仍可继续工作。
  - 测试命名：`Angelscript.TestModule.Debugger.Evaluate.StrictBodyAndZeroInitFailurePayload`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P6.1-T** 📦 Git 提交：`[Test/Debug] Test: add evaluate malformed-body and zero-init failure coverage`

- [ ] **P6.2** 统一 `ClearDataBreakpoints` 的入站/出站语义，空 `Ids` 才表示 clear-all，非空 `Ids` 必须 selective clear
  - 当前 `P3.1` 已承接 data breakpoint 的 backend apply、forced clear 与 verified/reject，但客户端主动 `ClearDataBreakpoints` 仍然没有权威语义：同一个消息类型在出站时会携带被移除的 `Ids`，入站时却被直接降格成 `ClearAllDataBreakpoints()`。这会让前端只想删 1 个 watchpoint 时，把其他仍应保留的监视点一并清空。
  - 本项把 `ClearDataBreakpoints` 收口成单一合同：`Ids.Num()==0` 保留 legacy clear-all，`Ids.Num()>0` 只删除命中的 `Id`；触发后自动移除路径与客户端主动删除路径必须共用同一套 `RemoveDataBreakpointsByIds(...)` helper 和寄存器刷新逻辑，避免再出现“一边 selective、一边 clear-all”的双轨实现。
  - 这一步也为后续 `P3.1` 的 requested/installed 分层留接口：当删除一个已安装项后，排队中的第 5 项应有机会补位，而不是继续让 authoritative state 与 hardware install state 永久失配。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 74 — “`ClearDataBreakpoints` 协议声明支持按 `Ids` 选择性删除，但服务端入口会无条件清空全部监视点”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-27 — “`ClearDataBreakpoints` 请求体携带 `Ids`，但服务端入口会无条件清空全部监视点”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-10 — “当前 data breakpoint 完全无自动化覆盖，也没有验证 `ClearDataBreakpoints` 回包里的 `Ids` 与后续保留状态”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT31 — “watchpoint 后端与协议应有清晰 capability/descriptor 合同，不能继续让地址与清理语义在协议层静默分叉”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L275-L290 — `FAngelscriptClearDataBreakpoints` 仍显式序列化 `Ids`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L479-L545 — 服务端在触发后自动移除路径里会构造带 `Ids` 的 selective clear；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1069-L1071 — 客户端主动发来的 `ClearDataBreakpoints` 仍直接调用 `ClearAllDataBreakpoints()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1241-L1285 — `ClearAllDataBreakpoints()` 仍会无条件 `Reset()` 权威数组并立即重写 watchpoint 状态。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointSelectiveClearTests.cpp`
- [ ] **P6.2** 📦 Git 提交：`[Debug/DataBreakpoint] Fix: honor selective clear ids instead of forcing clear-all`
- [ ] **P6.2-T** 单元测试：验证 `ClearDataBreakpoints` 的空 `Ids`/非空 `Ids` 语义稳定且不会误删其他 watchpoint
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointSelectiveClearTests.cpp`
  - 测试场景：
    - 正常路径：先安装至少 3 个 data breakpoint，只发送其中 1 个 `Id` 的 clear 请求，被指定的 watchpoint 被移除，其余 watchpoint 继续保留并能后续命中。
    - 边界条件：发送空 `Ids` 请求时仍执行 legacy clear-all；删除一个已安装项后，排队中的下一项会按当前限制规则补位，不会一直悬空。
    - 错误路径：发送未知 `Id`、重复 `Id` 或混合存在/不存在的 `Ids` 时，不会把全部 watchpoint 一并清空；同一 session 后续 `SetDataBreakpoints`/触发/自动清理链路仍然健康。
  - 测试命名：`Angelscript.TestModule.Debugger.DataBreakpoint.SelectiveClearHonorsIds`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P6.2-T** 📦 Git 提交：`[Test/Debug] Test: add selective clear ids coverage for data breakpoints`

- [ ] **P6.3** 为 Windows data breakpoint 更新线程补齐 duplicated thread handle 的 RAII 所有权，堵住长期调试下的 handle leak
  - 当前 `P3.1` 主要处理寄存器编程正确性、stop reason 与 forced clear；但 Windows backend 里还有一条独立的资源泄漏路径：`UpdateDataBreakpoints()` 每次都会 `DuplicateHandle(...)` 拿一个真实线程句柄，再交给 `FUpdateDebugRegisterThread`，而这个辅助对象只等待工作线程结束，从不关闭被复制出的 `ThreadToDebug`。
  - 本项把 duplicated handle 变成显式拥有的资源：`GetThreadAgnosticCurrentThreadHandle()` 的返回值必须进入一个唯一 RAII owner；无论 `FRunnableThread::Create(...)` 失败、`GetThreadContext()`/`SetThreadContext()` 早退还是正常完成，都必须在 `WaitForCompletion()` 之后成对 `CloseHandle()`。若后续仍保留“每次更新都现复制一次句柄”的模式，也要先把复制/释放对齐，再决定是否进一步缓存句柄。
  - 这一步不扩成更大的 multi-thread watchpoint registry；它先解决最基础的资源安全问题，避免长时间调试或频繁重建 watchpoint 时把进程句柄配额悄悄耗尽。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 49 — “`GetThreadAgnosticCurrentThreadHandle()` 每次 `DuplicateHandle(...)` 后都没有 `CloseHandle(ThreadToDebug)`”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-37 — “data breakpoint 更新线程每次都会泄漏 duplicated thread handle”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-10 — “当前 data breakpoint 完全无自动化覆盖，安装/清理生命周期回归只能靠手工调试发现”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT31 — “watchpoint backend 应从隐式 Windows 细节中抽离 capability/backend 边界，资源所有权必须清晰”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L206-L218 — `GetThreadAgnosticCurrentThreadHandle()` 仍每次 `DuplicateHandle(...)` 返回新的真实句柄；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L221-L232 — `FUpdateDebugRegisterThread` 析构函数仍只 `WaitForCompletion()`，没有任何 `CloseHandle(ThreadToDebug)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1275-L1282 — `UpdateDataBreakpoints()` 仍在每次设置/清空/重建 watchpoint 时创建一个新的 `FUpdateDebugRegisterThread`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugDataBreakpointHandleLeakTests.cpp`
- [ ] **P6.3** 📦 Git 提交：`[Debug/DataBreakpoint] Fix: close duplicated thread handles after register updates`
- [ ] **P6.3-T** 单元测试：验证 repeated watchpoint update/clear 不再泄漏 duplicated thread handle
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugDataBreakpointHandleLeakTests.cpp`
  - 测试场景：
    - 正常路径：多轮 `SetDataBreakpoints` / `ClearDataBreakpoints` / `UpdateDataBreakpoints` 后，duplicated handle 的创建与关闭次数保持配对，或进程 handle count 不再线性增长。
    - 边界条件：`FRunnableThread::Create(...)` 失败、`GetThreadContext()` 失败或 `SetThreadContext()` 失败时，duplicated handle 仍会被释放，不留下悬挂资源。
    - 错误路径：句柄关闭时机不会早于 worker 完成，避免把修复泄漏变成 `SuspendThread/GetThreadContext/SetThreadContext` 的 use-after-close；同一轮 watchpoint 安装行为仍保持可用。
  - 测试命名：`Angelscript.CppTests.Debug.DataBreakpoints.DuplicatedHandleLifecycle`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P6.3-T** 📦 Git 提交：`[Test/Debug] Test: add duplicated-handle lifecycle coverage for data breakpoint updates`

### 单元测试总览补充（本轮深化）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P6.1` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerEvaluateFailureTests.cpp` | malformed `RequestEvaluate` body、失败响应零初始化、v1/v2 payload 分支 | P0 |
| `P6.2` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointSelectiveClearTests.cpp` | selective clear、clear-all 兼容、未知 `Ids` no-op | P1 |
| `P6.3` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugDataBreakpointHandleLeakTests.cpp` | duplicated handle 配对释放、失败路径 RAII、无 use-after-close | P1 |

### 验收标准补充（本轮深化）

1. `RequestEvaluate` 在截断 body、失败求值与 adapter v1/v2 分支下都返回确定 payload，不再出现未初始化 frame、随机 `ValueAddress` 或随机 `ValueSize`。
2. `ClearDataBreakpoints` 的协议语义唯一且可回归：空 `Ids` 明确表示 clear-all，非空 `Ids` 只删除命中的 watchpoint，不会把其他监视点误清空。
3. Windows data breakpoint 的更新线程在长时间反复安装/清理场景下不再累积 duplicated thread handle，且失败路径与成功路径都能证明资源被成对释放。

### 风险与注意事项补充（本轮深化）

#### 风险

1. `P6.1` 若只做服务端静默吞错、不补明确失败值，前端仍会把“协议错误”“合法 miss”“表达式不存在”混在一起；修复时必须同时给出可判别的失败合同。
2. `P6.2` 会改变部分旧客户端对 `ClearDataBreakpoints` 的隐含假设；必须保留“空 `Ids` = clear-all”的兼容语义，否则现有前端容易把 selective clear 修复误判成回归。
3. `P6.3` 触碰 Windows 句柄和线程析构顺序；若 `CloseHandle()` 早于 worker 完成，会把泄漏修成 use-after-close，因此测试 seam 需要覆盖早退和异常路径。

#### 已知行为变化

1. `RequestEvaluate` 的 malformed body 将从“有时继续返回垃圾数据”收紧为“明确失败或零初始化响应”；依赖 silent trailing-bytes 容错的旧 helper 需要同步改成 strict parse。
2. 客户端主动发送 `ClearDataBreakpoints` 且附带 `Ids` 时，服务端行为会从“总是全清”改成“只删这些 id”；任何把这条消息当全清快捷键的旧前端，都需要改为发送空 `Ids`。
3. Windows 下频繁更新 watchpoint 后，调试进程的句柄曲线会从持续增长收紧为稳定；若历史诊断脚本曾把这种增长误当“仍有后台更新线程在工作”的信号，需要同步调整判断逻辑。

---

## 深化 (2026-04-09 01:42)

以下条目补足当前文档尚未承接的两组残余缺口：一组是 `RequestDebugDatabase` / `DebugDatabaseSettings` / `AssetDatabase` 这条既有协议面的 schema、订阅与生命周期 correctness；另一组是 `StaticJIT` 在 native form identity 与单条 bytecode codegen 上仍残留的确定性错配。它们都已有 AutoPlans 证据与当前源码对位，但还没有在现有 `P1-P6` 中形成独立执行项。

### 新增 Phase 7：收口 DebugDatabase / AssetDatabase 的协议与生命周期合同

- [ ] **P7.1** 把 `DebugDatabaseSettings` / `AssetDatabase` 从“写了 `Version` 字段”升级为真正的 versioned decode contract
  - 当前文档已覆盖 session、transport 与 stopped payload，但 `RequestDebugDatabase` 初始化链上的两条关键消息仍把 `Version` 当成注释字段：写端先发版本号，读端再无条件按“当前最新字段集”继续反序列化。只要字段有增删，旧客户端与新服务端就会直接错位，而不是像 `FindAssets` 那样用 `Ar.AtEnd()` 做尾字段兼容。
  - 本项只修当前已存在消息体的读写合同，不扩成 `Arch-DT5` 那类 delta sync / typed symbol graph 改造；目标是先把 `DebugDatabaseSettings` 与 `AssetDatabase` 收口成“旧版本可降级、未知未来版本可拒绝、当前版本可严格 round-trip”的可回归协议面。
  - 这一步也要顺手把 version/capability 来源从分散的 message struct 常量收敛到统一 codec/descriptor 入口，避免未来继续出现“字段已经演化，但旧 reader 仍假定自己读的是最新版”的 silent drift。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 29 — “`DebugDatabaseSettings` / `AssetDatabase` 写了 `Version` 字段却不按版本解码，协议升级没有兼容护栏”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-82 — “`DebugDatabaseSettings` / `AssetDatabase` 写了 `Version` 字段却没有版本化解码”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-14 — “需要覆盖 `RequestDebugDatabase` 的真实消息序列，而不只是 `DebugDatabaseSettings` struct roundtrip”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT5 / Arch-DT34 — “`DebugDatabase` / `AssetDatabase` 缺少 revision/schema 协商；版本演进点分散在 message struct 内部”
    - [E] `CrossComparison.md` [D5] — “当前 Angelscript 已把 `DebugDatabaseSettings` 当语言模式 contract，字段演化必须按正式协议治理”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L511-L516 — `FAngelscriptAssetDatabase` 仍先写 `Version` 再无条件读写 `Assets`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L544-L553 — `FAngelscriptDebugDatabaseSettings` 仍先写 `Version = 5` 再无条件读 5 个布尔位；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L525-L532 — 同文件里的 `FAngelscriptFindAssets` 已经证明当前代码库有 `Ar.AtEnd()` 兼容式写法，但这两条消息仍未采用。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolVersioningTests.cpp`
- [ ] **P7.1** 📦 Git 提交：`[Debug/Protocol] Fix: version debug database and asset database decoding`
- [ ] **P7.1-T** 单元测试：验证 `DebugDatabaseSettings` / `AssetDatabase` 在 legacy、current 与 future payload 下都保持确定解码
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolVersioningTests.cpp`
  - 测试场景：
    - 正常路径：当前 schema 的 `DebugDatabaseSettings` / `AssetDatabase` 能完整 round-trip，字段顺序与 `RequestDebugDatabase` 初始化流保持一致。
    - 边界条件：旧版本 payload 缺少尾字段时仍能按兼容规则解码；`AssetDatabase` 的历史 payload 不会把后续消息边界读坏。
    - 错误路径：未知 future version 或畸形消息体会得到显式 reject/diagnostic，而不是继续读错位字段或 silently 成功。
  - 测试命名：`Angelscript.CppTests.Debug.Protocol.VersionedDatabaseSchemas`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P7.1-T** 📦 Git 提交：`[Test/Debug] Test: add versioned debug database schema coverage`

- [ ] **P7.2** 让 `RequestDebugDatabase` 订阅变成 per-client idempotent，禁止同一 socket 重复进入 `ClientsThatWantDebugDatabase`
  - 当前实现把 `RequestDebugDatabase` 当成“每来一次就向 `TArray` 末尾再追加一份订阅”。这样同一 client 只要重发一次请求，之后每次 `BroadcastDebugDatabase()`、每次 `AssetRegistry` 事件、每次全量资产重发都会收到 N 份完全相同的数据流。问题不在 UI 重复，而在协议合同已经从“单订阅 -> 单条线性流”漂移成“重复请求次数决定后续广播倍数”。
  - 本项先收口当前协议的幂等语义：同一 `FSocket*` 对 `RequestDebugDatabase` 最多只占一个订阅位；如需显式刷新，必须走单独的 refresh 路径或先断开重连，而不是继续拿重复 `Add()` 当隐式刷新开关。
  - 这一步不扩成 `Arch-DT5` 的 revision/delta 体系，也不顺手修改 `DebugDatabaseFinished` / `AssetDatabaseFinished` 的边界消息；目标只是先让“一个 client -> 一条 database/asset 流”重新成立，避免后续任何 schema/性能优化都建立在重复订阅的脏前提上。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 50 — “同一客户端重复发送 `RequestDebugDatabase` 会被重复订阅，后续数据库与资产流按重复次数多次下发”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-50 — “`RequestDebugDatabase` 重复订阅后会重复收到整套 database/asset 流”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-14 — “当前没有任何自动化验证真实 `RequestDebugDatabase` 消息序列与完成边界”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT5 — “`DebugDatabase` / `AssetDatabase` 当前每个订阅者都会重复跑一遍全量快照，缺少 revision/backpressure 合同”
    - [E] `CrossComparison.md` [D5] — “当前仓库已把 debug database 流当正式 IDE contract 维护，不应让重复请求把同一 contract 变成倍增广播”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L587 — `ClientsThatWantDebugDatabase` 仍是允许重复元素的 `TArray<FSocket*>`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L822-L826 — `RequestDebugDatabase` 仍直接 `Add(Client)` 后立即全量发送；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1487-L1490 — `BroadcastDebugDatabase()` 仍对数组里的每个元素逐个重发；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L2103-L2147 — 资产增量与 `OnFilesLoaded()` 全量重发同样遍历这张可重复数组。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugDatabaseSubscriptionTests.cpp`
- [ ] **P7.2** 📦 Git 提交：`[Debug/Database] Fix: dedupe debug database subscriptions per client`
- [ ] **P7.2-T** 单元测试：验证重复 `RequestDebugDatabase` 不再放大后续 full-sync 与 asset-delta 流
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugDatabaseSubscriptionTests.cpp`
  - 测试场景：
    - 正常路径：同一 client 首次发送 `RequestDebugDatabase` 时，只收到 1 组 `DebugDatabaseSettings -> DebugDatabase* -> DebugDatabaseFinished -> AssetDatabaseInit -> AssetDatabase* -> AssetDatabaseFinished` 序列。
    - 边界条件：同一 client 连续发送两次 `RequestDebugDatabase` 后，后续一次 `BroadcastDebugDatabase()` 或单次 asset 更新仍只收到 1 份消息流，不会按请求次数倍增。
    - 错误路径：client 断开后重连再订阅时，不会因为旧数组残留而收到重复流；未知或已断开的 socket 不会继续留在订阅集合里吃到增量广播。
  - 测试命名：`Angelscript.TestModule.Debugger.Database.RequestDebugDatabaseIsIdempotent`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P7.2-T** 📦 Git 提交：`[Test/Debug] Test: add debug database subscription dedupe coverage`

- [ ] **P7.3** 为 `BindAssetRegistry()` 增加 delegate handle 所有权与 teardown 对称解绑，堵住 `DebugServer` 销毁后的 use-after-free
  - 现有 `RequestDebugDatabase` / `SendAssetDatabase()` 路径不是“功能缺失”，而是存在明确生命周期缺陷：首次请求资产数据库后，`BindAssetRegistry()` 会注册多条捕获裸 `this` 的 lambda；类里却只保存一个 `bAssetRegistryBound` 布尔值，析构函数也只停止 `Listener`。这让 `DebugServer` 与 `AssetRegistry` 之间形成了单向永久订阅，一旦 engine/debug server 生命周期结束，后续资产事件就会回调到已释放对象。
  - 本项先做最小 correctness 收口：把 `OnAssetAdded/Removed/Renamed/FilesLoaded` 的返回 handle 记录到 server 自己的拥有型成员里，在析构或显式 unbind 时逐一移除；同时把 `BindAssetRegistryChanges` 中的 `ClientsThatWantDebugDatabase` 广播路径改成只在 server 仍有效且订阅集合非空时运行。
  - 这一步不扩成 `Arch-DT3` 的 workspace service 拆分，也不处理 asset literal 写回/`FindAssets`/`CreateBlueprint` 的更大域拆分；目标是先把当前已经存在的 `AssetDatabase` 流从“偶发 editor 生命周期崩溃点”收口成受控的订阅/解绑对。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 31 — “`AssetRegistry` 变更回调用裸 `this` 捕获且从不解绑，`DebugServer` 销毁后会留下 use-after-free 路径”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-25 — “`BindAssetRegistry()` 用裸 `this` 注册长期 delegate，`DebugServer` 销毁后会留下 use-after-free 路径”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-14 — “当前没有真实 `RequestDebugDatabase` / `AssetDatabase` 序列自动化，相关生命周期问题没有护栏”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT3 / Arch-DT5 — “workspace/asset 流仍压在 `DebugServer` 内，同步全量快照与长期订阅都缺少独立生命周期治理”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L737-L738 — 类内仍只有 `bAssetRegistryBound` 和 `BindAssetRegistry()`，没有任何 `FDelegateHandle` 成员；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L419-L433 — 析构函数仍只停止 `Listener`，没有对 `AssetRegistry` 做对称解绑；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L2093-L2149 — `OnAssetAdded/Removed/Renamed/OnFilesLoaded` 仍全部以 `[this]` lambda 方式注册。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptAssetDatabaseLifecycleTests.cpp`
- [ ] **P7.3** 📦 Git 提交：`[Debug/AssetDatabase] Fix: unbind asset registry delegates on server teardown`
- [ ] **P7.3-T** 单元测试：验证请求过 `AssetDatabase` 的 `DebugServer` 在 teardown 后不再保留悬空回调
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptAssetDatabaseLifecycleTests.cpp`
  - 测试场景：
    - 正常路径：client 完成一次 `RequestDebugDatabase` 后，`AssetRegistry` 增量事件仍能精确下发 1 次，不丢事件也不重复广播。
    - 边界条件：在 `AssetRegistry` 尚未完成初始加载、需要走 `OnFilesLoaded()` 延迟绑定路径时，server teardown 仍会释放全部 handle，不留下延迟回调。
    - 错误路径：请求过 `AssetDatabase` 的 server 被销毁后，再触发 `OnAssetAdded/Removed/Renamed` 不会访问悬空 `this`，也不会继续向旧 socket 发送消息。
  - 测试命名：`Angelscript.TestModule.Debugger.AssetDatabase.DelegateLifecycleIsScoped`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P7.3-T** 📦 Git 提交：`[Test/Debug] Test: add asset database delegate lifecycle coverage`

### 新增 Phase 8：补齐 StaticJIT native form identity 与字节码生成残缺

- [ ] **P8.1** 把模板实例的 native form 解析从“按槽位索引”改成“按稳定签名键查找”
  - 现有 `P4.1` 已承接 native form registry 的 owner / teardown，但 identity 侧还有一个独立缺口没有被收口：template instantiation 仍按 methods/constructors 在数组中的位置去映射 base type native form，注释甚至直接承认前提是“methods are always in the same order”。这意味着只要模板基类和某个实例化类型的函数顺序发生漂移，JIT custom/native call 就会静默绑错目标。
  - 本项不重复 `P4.1` 的 registry 生命周期治理，而是专门修 native form lookup 的 canonical identity：优先按 declaration/signature hash 或等价稳定 key 建映射；找不到精确匹配时宁可明确 fallback / unsupported，也不能继续猜“同一槽位大概就是同一函数”。
  - 这一步与 `Arch-DT13 / Arch-DT26` 的长期方向一致，但本轮先做最小 correctness 修复，不要求一次落完整的 `BindingCoverageId` sidecar。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 20 — “模板类型的 native form 解析按方法顺序对齐，不按签名/名称匹配，JIT custom call 容易绑错目标”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-83 — “模板实例的 native form 解析按槽位对齐，方法顺序一变就会绑错目标”
    - [C] `DebuggingAndJIT_TestGaps.md` Issue-18 / NewTest-3 — “当前 `StaticJIT` 直连测试不执行真实 transpiled code，也没有覆盖 core codegen/native-form 回归”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT13 / Arch-DT26 — “native-form contract 缺少独立持久化身份与共同键，不能继续依赖 ambient pointer/slot position”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` L33-L37 — 注释仍明确写着 “methods are always in the same order”；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` L43-L52 — constructors/methods 仍通过 `IndexOf()` 后直接取 `templateBaseType->beh.constructors[...]` / `templateBaseType->methods[...]` 同位 native form；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` L27-L29、L55 — 最终仍回落到以 raw `asIScriptFunction*` 为 key 的 `GScriptNativeForms` 查找。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITNativeFormResolutionTests.cpp`
- [ ] **P8.1** 📦 Git 提交：`[StaticJIT/NativeForm] Fix: resolve template native forms by signature`
- [ ] **P8.1-T** 单元测试：验证模板实例 native form 的匹配不再依赖 methods/constructors 的槽位顺序
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITNativeFormResolutionTests.cpp`
  - 测试场景：
    - 正常路径：template base type 与实例化类型顺序一致时，native form 仍能命中正确目标，JIT/custom call 结果不回归。
    - 边界条件：constructors 或 methods 顺序发生漂移时，仍能按签名命中正确 native form，而不是按旧槽位误绑。
    - 错误路径：找不到 exact match 时，系统返回显式 fallback / unsupported，而不是继续复用错误 native form 产生 silent wrong-call。
  - 测试命名：`Angelscript.CppTests.StaticJIT.NativeForm.TemplateLookupUsesStableSignature`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P8.1-T** 📦 Git 提交：`[Test/StaticJIT] Test: add template native-form lookup coverage`

- [ ] **P8.2** 修正 `asBC_ClrHi` 的 `float/double` JIT codegen 文本，并补最小 syntax smoke 防止同类字符串拼接回归
  - 当前 `asBC_ClrHi` 不是“缺 fallback”的抽象问题，而是确定性的 codegen typo：`FloatRegister` 与 `DoubleRegister` 分支都生成了缺少分号的 `memcpy(...)` 语句。命中这两个寄存器态时，StaticJIT 产出的 C++ 会直接语法错误，连“回退到 VM”都谈不上。
  - 本项先修这两条错误输出，再把它们收进专门的 codegen syntax 回归。这样做的目的不是为单个 bytecode 大动框架，而是给当前仍大量依赖 `Context.Line("...")` 拼接字符串的 lowering 层补一条最低成本、但能挡住确定性语法回归的护栏。
  - 本项不重复 `P2.2` 的函数级 fallback 合同；它只处理一个已经被定位到的生成文本错误，并借机建立同类语句级 smoke。
  - 来源：
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-81 — “`asBC_ClrHi` 的 `float/double` JIT 分支漏写分号，命中时会直接生成非法 C++”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-3 — “需要为 StaticJIT core codegen 建立最小执行矩阵，证明能成功生成 / 注册 / 执行”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT2 — “StaticJIT 需要可记录、可回归的 codegen/fallback 合同，而不是把生成错误留到后续阶段偶发暴露”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` L6020-L6025 — `FloatRegister` 与 `DoubleRegister` 分支仍分别输出 `Context.Line("memcpy((void*)&l_byteRegister, (void*)&l_floatRegister, 1)")` 和 `Context.Line("memcpy((void*)&l_byteRegister, (void*)&l_doubleRegister, 1)")`，两条字符串末尾都没有分号。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITCodegenSyntaxTests.cpp`
- [ ] **P8.2** 📦 Git 提交：`[StaticJIT/Codegen] Fix: terminate ClrHi memcpy emissions`
- [ ] **P8.2-T** 单元测试：验证 `asBC_ClrHi` 在 `float/double` 寄存器态下生成的 C++ 始终是可编译语句
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITCodegenSyntaxTests.cpp`
  - 测试场景：
    - 正常路径：`FloatRegister` / `DoubleRegister` 两个分支都生成带分号的 `memcpy(...);` 语句，codegen 文本可通过最小 syntax smoke。
    - 边界条件：`ByteRegister` / `DWordRegister` 分支保持现有行为，不引入额外文本变化或寄存器状态回归。
    - 错误路径：若后续再次漏掉语句终止符或拼出非法文本，测试会在生成阶段直接失败，而不是拖到更晚的 C++ 编译阶段才暴露。
  - 测试命名：`Angelscript.CppTests.StaticJIT.Codegen.ClrHiEmitsValidMemcpyStatements`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P8.2-T** 📦 Git 提交：`[Test/StaticJIT] Test: add ClrHi codegen syntax coverage`

### 单元测试总览补充（本轮深化）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P7.1` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolVersioningTests.cpp` | database/asset schema versioning、legacy decode、future reject | P1 |
| `P7.2` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugDatabaseSubscriptionTests.cpp` | `RequestDebugDatabase` 幂等订阅、full-sync 去重、断线重连清理 | P1 |
| `P7.3` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptAssetDatabaseLifecycleTests.cpp` | `AssetRegistry` delegate 生命周期、teardown 后无悬空回调 | P0 |
| `P8.1` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITNativeFormResolutionTests.cpp` | template native-form exact match、顺序漂移、fallback/unsupported | P1 |
| `P8.2` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITCodegenSyntaxTests.cpp` | `ClrHi` float/double codegen 语法、寄存器态边界、syntax smoke | P1 |

### 验收标准补充（本轮深化）

1. `DebugDatabaseSettings` / `AssetDatabase` 对 legacy/current/future payload 的解码合同变成确定行为：旧版可降级、当前版可 round-trip、未知未来版可显式拒绝，不再 silent misparse。
2. 同一 socket 重复发送 `RequestDebugDatabase` 不会再放大后续 `DebugDatabase` / `AssetDatabase` 流；full-sync 与增量广播都保持“每 client 一份”的幂等语义。
3. 请求过 `AssetDatabase` 的 `FAngelscriptDebugServer` 在 teardown 后不再保留 `AssetRegistry` 悬空回调；后续资产事件既不会 crash，也不会向旧 socket 继续发包。
4. 模板实例的 native form 解析不再依赖 methods/constructors 的槽位顺序；找不到精确匹配时会走显式 fallback / unsupported，而不是静默绑错目标。
5. `asBC_ClrHi` 的 `float/double` JIT 分支始终生成可编译语句，并有专门的 codegen syntax 自动化兜底。

### 风险与注意事项补充（本轮深化）

#### 风险

1. `P7.1` 若把 future version 直接硬拒绝得过于激进，旧前端可能把“明确不兼容”误判成“调试器坏了”；需要同时给出可读 diagnostic，并保留当前版本与旧版本的兼容窗口。
2. `P7.2` / `P7.3` 共同触碰 `RequestDebugDatabase` 与 `AssetRegistry` 生命周期；如果去重与解绑顺序处理不当，容易把“重复消息”修成“漏首包”或“首轮 asset init 不再发送”。
3. `P8.1` 的 signature key 若 canonical 规则不稳定，会把原本可用的 native form 误判成 miss；因此 fallback 必须先做显式、可诊断，再谈更激进的 fast path。
4. `P8.2` 若只补分号、不建立 syntax smoke，当前 `Context.Line("...")` 风格的其它语句级 typo 仍会以同样方式回归。

#### 已知行为变化

1. 未来版本或畸形版本的 `DebugDatabaseSettings` / `AssetDatabase` 消息将从“有机会被错误读成当前版本”收紧为“明确 reject/diagnostic”；依赖 silent 宽松解析的旧 helper 需要同步调整。
2. 重复发送 `RequestDebugDatabase` 将不再被当成隐式 refresh；任何历史上依赖“重发一次就多来一遍全量流”的客户端，都需要改成显式刷新或重连语义。
3. 模板实例 native form 找不到 exact match 时，行为会从“可能错误复用同槽位 native form”收紧为“显式 fallback / unsupported”；依赖旧错绑副作用的脚本不会再被继续默许。

---

## 深化 (2026-04-09 01:56)

以下条目补足当前文档尚未承接的 5 个残余缺口：一组是 `Pause/Continue` 与 Windows watchpoint 仍缺真正的 pending-stop / thread-scope contract；另一组是 `StaticJIT/PrecompiledData` 在 multi-file source identity、VM fallback exception propagation 与 `int64` codegen 语法上仍有确定性 correctness 缺口。它们都已有五维分析支撑，并且已在当前源码中复核仍然存在，但尚未进入现有 `P1-P8` 的独立执行项。

### 新增 Phase 9：补齐剩余停点状态机与 StaticJIT source/call correctness

- [ ] **P9.1** 将 `Pause` / data breakpoint 的“待兑现停点”从 `bBreakNextScriptLine` 中拆出为显式 pending-stop state，禁止 running 态 `Continue` 提前吞掉挂起 stop
  - 现有 `P1.4` 已经收口了 active-stop ownership 与 top-frame `StepOut`，但 `Pause` 和 data breakpoint 仍共用 `bBreakNextScriptLine` 这一个全局布尔位。当前 `Pause` 只是在 running 态登记“下一条脚本行再停”，data breakpoint 也要等下一次 `ProcessScriptLine()` 才能真正发出 `HasStopped`；与此同时，`Continue` 仍无条件把 `bBreakNextScriptLine = false`。这意味着 stop 还没 materialize 成 paused-state 之前，就能被提前清空。
  - 本项只补“pending stop ≠ paused state”这一层状态机，不重复 `P1.4` 的 step owner gate，也不扩成完整 thread-aware controller。最小落地是引入显式 `PendingStopSource/PendingStopText` 或等价结构；`Pause` 与 data breakpoint 只登记 pending-stop，`ProcessScriptLine()` 再原子兑现成真正的 `PauseExecution()`；running 态 `Continue` 只允许结束已经进入 paused 的 stop，不再能擦除尚未兑现的挂起停点。
  - 这样可以把 `Pause` 的 `Reason="pause"`、watchpoint 的后续 stop 兑现顺序，以及“提前一条 `Continue` 不会把本应到达的 stop 蒸发掉”三件事固定为一套可回归的协议合同。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 28 — “`Pause` 只在下一条脚本行才可能停下，协议语义与 DAP `pause` 偏离”
    - [A] `DebuggingAndJIT_Analysis.md` 发现 33 — “running 态 `Continue` 会直接抹掉挂起中的 `pause` / data breakpoint stop”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-33 — “需要把 pending-stop 与 paused-state 拆开，`Continue` 不得清除尚未兑现的挂起停点”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-25 — “客户端主动 `Pause` 必须在下一条脚本行停下，并返回 `pause` reason”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT42 — “`Pause` 需要 startup pending stop 语义，而不是只靠下一次 line callback”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L828-L845 — `Pause` 仍只写 `bBreakNextScriptLine = true` 且没有独立 pending-stop；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L479-L545 — data breakpoint 命中仍先记 `TriggeredBreakpoints`，后续才在脚本行里统一 `PauseExecution()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L837-L845 — `Continue` 仍无条件清 `bBreakNextScriptLine`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L667-L695 — `PauseExecution()` 只区分“已 paused”和“未 paused”，没有 pending-stop 生命周期。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerPauseStateTests.cpp`
- [ ] **P9.1** 📦 Git 提交：`[Debug/Pause] Fix: separate pending stop from paused state`
- [ ] **P9.1-T** 单元测试：验证 `Pause` / data breakpoint 的挂起 stop 不会被 running-state `Continue` 提前吞掉
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerPauseStateTests.cpp`
  - 测试场景：
    - 正常路径：脚本在 `PauseLoopLine` 持续运行时发送 `Pause`，下一次可断脚本行稳定返回 `HasStopped(reason="pause")`，随后 `Continue` 可正常恢复执行。
    - 边界条件：watchpoint 或 `Pause` 已经登记 pending-stop，但 `HasStopped` 尚未发出前收到一条 running-state `Continue`；挂起 stop 仍会在下一次脚本行兑现，不会被静默擦除。
    - 错误路径：无可兑现 pending-stop 时发送 `Continue` 只返回 no-op/diagnostic，不会清掉后续真正应到达的 `Pause` 或 data breakpoint 停点。
  - 测试命名：`Angelscript.TestModule.Debugger.Session.PendingPauseAndDataBreakpointSurviveEarlyContinue`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.1-T** 📦 Git 提交：`[Test/Debug] Test: add pending-stop pause state coverage`

- [ ] **P9.2** 将 `PrecompiledData` 的 script section 恢复从“模块首文件”修正为“函数级真实 section”，堵住 multi-file module 的 VM fallback 串文件
  - 当前 `PrecompiledData` 虽然在每个 `scriptData` 上都写了 `scriptSectionIdx`，但它真正取值的 `GetScriptSection()` 仍然只通过 `Context.ModuleDesc->Code[0].RelativeFilename` 推导 section。结果是 multi-file module 内所有函数都会被压到同一个 `ScriptRelativeFilename` / `ScriptSectionIdx`，只要某个函数回退到 VM bytecode、抛异常或参与源码定位，文件 B 的函数就会被稳定报告成文件 A。
  - 本项要把 source identity 从“module 级单值”提升为“function 级持久化字段”：序列化时保存当前函数真实的 section/file，恢复时按函数自己的 section 建立 `scriptSectionIdx`；找不到 section 时显式返回 invalid/diagnostic，而不是继续回落到模块首文件。这样才能和现有 `P5.2` 的 file-scoped breakpoint authority、以及后续 JIT/source map 合同对齐。
  - 这一步不要求一次建完整 `JitSourceMap`，但至少要先把 multi-file precompiled fallback 的文件身份修正为可信输入，否则后续任何跨文件调试、异常定位或 source-navigation 对位都会建立在错误 filename 上。
  - 来源：
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-40 — “`PrecompiledData` 只保留模块首个 script section，multi-file module 的 VM fallback 会把源码定位串到第一份文件”
    - [C] `DebuggingAndJIT_TestGaps.md` Issue-18 — “现有 `StaticJIT` 直连测试全部跑在 `EditorContext`，实际上没有执行任何 transpiled code”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-18 — “跨文件 `StepOver` 必须留在 caller source，而不是误入 callee 文件”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT24 — “JIT / precompiled source identity 不能继续压缩成模块级锚点”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h` L639-L640 — context 仍只保存单个 `ScriptSectionIdx` 与 `ScriptRelativeFilename`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1484-L1495 — `ScriptRelativeFilename` 仍固定取 `Context.ModuleDesc->Code[0].RelativeFilename`，`GetScriptSection()` 也只据此生成 section index；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L573-L575 — 每个函数的 `scriptData->scriptSectionIdx` 仍直接继承这条 module 级单值。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledSourceSectionTests.cpp`
- [ ] **P9.2** 📦 Git 提交：`[StaticJIT/Precompiled] Fix: persist per-function script section identity`
- [ ] **P9.2-T** 单元测试：验证 multi-file module 的 precompiled fallback 会回到函数自己的 source section
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledSourceSectionTests.cpp`
  - 测试场景：
    - 正常路径：同一 module 下文件 A、文件 B 各有一个函数时，恢复后的 `scriptSectionIdx` 分别指向各自 source file，而不是共享首文件。
    - 边界条件：两个文件存在相同行号时，异常/定位仍能回到正确文件，不会因为“同号行”再次误绑到 `Code[0]`。
    - 错误路径：缺失或失配的 section 名称返回 explicit invalid/diagnostic，不再 silently 回退到模块首文件继续执行。
  - 测试命名：`Angelscript.CppTests.StaticJIT.PrecompiledData.FunctionSectionIdentityIsPerFunction`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.2-T** 📦 Git 提交：`[Test/StaticJIT] Test: add precompiled per-function section identity coverage`

- [ ] **P9.3** 在 `WriteDynamicCall()` 的 VM fallback 路径上显式桥接嵌套 `CallContext` 异常，恢复 JIT caller 与解释执行的一致异常传播
  - 当前 `WriteDynamicCall()` 为 JIT miss / fallback callee 临时构造独立 `FAngelscriptContext CallContext`，执行后只要 `m_status != asEXECUTION_FINISHED` 就直接 `ExceptionCleanupAndReturn(..., false)`。这条路径不会调用 `Throw()` / `SetException()`，也不会设置当前 `Execution.bExceptionThrown`。结果是 VM callee 在 fallback 里抛出的脚本异常，不会被外层 JIT caller 正式感知，上层控制流会把“callee 抛异常”误当成“清理后直接返回默认值”。
  - 本项要把 nested `CallContext` 的异常提升成当前 `FScriptExecution` 的正式异常：先读 `NestedContext` 的异常文本/位置，再复用单一异常入口把 `Execution.bExceptionThrown` 置真，让 direct-call parent path、`catch` 语义和后续 DebugServer exception stop 都能看到这次失败，而不是继续 silent cleanup。
  - 这一步不展开成 `Arch-DT22` 的完整 planner；它先修掉最危险的 correctness 裂缝，保证 hybrid fallback 至少在异常语义上不再比解释器更“宽松”。
  - 来源：
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-54 — “`WriteDynamicCall()` 会吞掉 VM callee 的脚本异常，外层 `Execution.bExceptionThrown` 永远不会置位”
    - [C] `DebuggingAndJIT_TestGaps.md` Issue-18 — “现有 `StaticJIT` 直连测试没有任何真正执行 transpiled code 的护栏”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT22 — “hybrid fallback 不能继续依赖每个 callsite 即席重建解释器上下文并放任语义分叉”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` L1563-L1570 — `WriteDynamicCall()` 仍在 `CallContext->m_status != asEXECUTION_FINISHED` 时直接 `ExceptionCleanupAndReturn(..., false)`，没有桥接异常；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp` L104-L160 — 真正的 JIT 异常 helper 会显式 `Execution.bExceptionThrown = true` 并转到 `HandleExceptionFromJIT()`，与当前 fallback 路径完全分裂。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`；`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITFallbackExceptionTests.cpp`
- [ ] **P9.3** 📦 Git 提交：`[StaticJIT/Fallback] Fix: propagate nested VM exceptions into JIT execution`
- [ ] **P9.3-T** 单元测试：验证 VM fallback callee 的异常会向外层 JIT caller 正确传播
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITFallbackExceptionTests.cpp`
  - 测试场景：
    - 正常路径：JIT caller 调 VM fallback callee 且 callee 正常返回时，结果与纯解释执行一致，不引入额外异常。
    - 边界条件：callee 在 fallback 中抛异常但由 caller 的脚本逻辑捕获时，JIT 与解释执行看到相同的异常文本与 `catch` 结果。
    - 错误路径：callee 的未捕获异常会把当前 `Execution.bExceptionThrown` 置真、阻止 caller 继续执行后续语句，并在有 DebugServer 时维持 `exception` 停止语义。
  - 测试命名：`Angelscript.TestModule.StaticJIT.FallbackException.VMCalleeExceptionPropagatesToJITCaller`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.3-T** 📦 Git 提交：`[Test/StaticJIT] Test: add VM-fallback exception propagation coverage`

- [ ] **P9.4** 将 Windows data breakpoint 的安装范围从“调用 `UpdateDataBreakpoints()` 的当前线程”扩成受控的 target-thread registry / replay 语义
  - 现有 `P1.3` 已经规划了 data breakpoint 的 verified/reject、stop reason 与 clear flow，但它默认的硬件编程范围仍然只是一条线程。`UpdateDataBreakpoints()` 每次都只复制一次 `GetCurrentThread()` 句柄，再把这一个 `HANDLE` 交给 `FUpdateDebugRegisterThread`。源码中没有任何“当前 debug host 的所有脚本线程集合”“新线程启动后 replay watchpoint”或“线程退出后的清理回放”。结果是 watchpoint 语义仍然建立在“恰好由当前线程写入”这一脆弱前提上。
  - 本项不要求一步做到全进程 OS 线程扫描；最小正确性目标是把 watchpoint owner 明确成“当前 debug host 下会执行脚本的线程集合”，并为这些线程做注册、replay 与 teardown。若短期只支持 game thread + 已登记的脚本 worker thread，也必须显式返回 unsupported/diagnostic，而不是继续 silent miss。
  - 这样 `ValueAddress -> SetDataBreakpoints -> 命中回停` 这条链才能真正成为受控能力，而不是“只要写入刚好发生在 armed thread 就碰巧有效”的偶发行为。
  - 来源：
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-59 — “Windows data breakpoint 只对单个 current thread 编程，跨线程写入会被静默漏掉”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-10 — “目前连一次真实 data breakpoint 命中与自动清理都没有端到端场景，更没有线程范围护栏”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT31 — “watchpoint backend 需要从 raw-address 细节里抽象出来，不能继续把本机实现偶发成功当协议事实”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1279-L1282 — `UpdateDataBreakpoints()` 仍只用 `GetThreadAgnosticCurrentThreadHandle()` 创建一次 `FUpdateDebugRegisterThread`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L223-L231 — `FUpdateDebugRegisterThread` 仍只持有单个 `HANDLE ThreadToDebug`，没有任何线程集合、owner registry 或 replay 机制。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDataBreakpointThreadScopeTests.cpp`
- [ ] **P9.4** 📦 Git 提交：`[Debug/DataBreakpoint] Fix: replay watchpoints across registered target threads`
- [ ] **P9.4-T** 单元测试：验证 watchpoint 会在已登记的目标线程集合上重放，而不是只对调用线程偶发生效
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDataBreakpointThreadScopeTests.cpp`
  - 测试场景：
    - 正常路径：game thread 或 primary script thread 写入被监视地址时，watchpoint 仍能稳定触发，不回归当前单线程正例。
    - 边界条件：第二个已登记的脚本执行线程写入同一地址时，也会触发相同 watchpoint；线程在 watchpoint 安装后启动时，replay 逻辑能补装硬件断点。
    - 错误路径：未登记或已退出的线程不会被 silent miss 掩盖；系统返回 explicit unsupported/diagnostic 或正确清理 stale registration，而不是继续假装“watchpoint 已全局生效”。
  - 测试命名：`Angelscript.TestModule.Debugger.DataBreakpoint.ThreadRegistrationAndReplay`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.4-T** 📦 Git 提交：`[Test/Debug] Test: add data-breakpoint thread scope coverage`

- [ ] **P9.5** 修正 `asBC_DIVi64` 的 `[[unlikely]]` 守卫文本，并补 `int64` 除法 codegen syntax smoke
  - 现有 `P8.2` 已经收口了 `ClrHi` 的语句终止符问题，但 `int64` 除法路径里还留着另一个同级别的 deterministic typo：`asBC_DIVi64` 生成 `if (divider == 0) [[unlikely]`，少了一个 `]`。任何包含 `int64` 除法的 StaticJIT 函数一旦进入输出阶段，都会把非法 attribute 文本带进产物文件，随后在 C++ 编译期失败。
  - 本项不扩成更大的 arithmetic opcode 审计，只先修这一条确定性坏文本，并把 `DIVi64/MODi64` 邻近路径一起收进最小 syntax smoke。目标和 `P8.2` 一样：把单字符 codegen 回归前移到自动化，而不是再等后续编译器报错才发现。
  - 来源：
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-64 — “`asBC_DIVi64` 的 `[[unlikely]]` 少一个 `]`，任何 `int64` 除法都会打坏 StaticJIT 输出”
    - [C] `DebuggingAndJIT_TestGaps.md` Issue-18 — “当前 `StaticJIT` 直连测试没有真正覆盖真实 codegen / executed path”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT2 — “StaticJIT 需要可记录、可回归的 codegen/fallback 合同，而不是把生成错误留到更晚阶段”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` L5808-L5812 — `asBC_DIVi64` 仍输出 `Context.Line("  if (divider == 0) [[unlikely]");`，attribute 文本缺少闭合 `]`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITInt64CodegenSyntaxTests.cpp`
- [ ] **P9.5** 📦 Git 提交：`[StaticJIT/Codegen] Fix: repair DIVi64 unlikely guard syntax`
- [ ] **P9.5-T** 单元测试：验证 `int64` 除法/取模相关 guard 文本始终生成合法的 `[[unlikely]]` 语法
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITInt64CodegenSyntaxTests.cpp`
  - 测试场景：
    - 正常路径：`asBC_DIVi64` 生成的 divide-by-zero guard 包含合法 `[[unlikely]]` 文本，最小 syntax smoke 可通过。
    - 边界条件：`MODi64` 与相邻 `DIVi/MODi` 守卫文本保持一致，不因修复 `DIVi64` 引入其它 attribute 漂移。
    - 错误路径：若后续再次把 `[[unlikely]]` 拼坏或遗漏 guard 文本，测试会在 codegen 级直接失败，而不是拖到更晚的 C++ 编译阶段才暴露。
  - 测试命名：`Angelscript.CppTests.StaticJIT.Codegen.DivI64EmitsValidGuardSyntax`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P9.5-T** 📦 Git 提交：`[Test/StaticJIT] Test: add int64 divide codegen syntax coverage`

### 单元测试总览补充（本轮深化）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P9.1` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerPauseStateTests.cpp` | pending-stop、`Pause` reason、early `Continue` 不吞 stop | P0 |
| `P9.2` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledSourceSectionTests.cpp` | multi-file module、per-function section identity、invalid section reject | P1 |
| `P9.3` | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITFallbackExceptionTests.cpp` | JIT caller -> VM callee 正常/捕获/未捕获异常传播 | P0 |
| `P9.4` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDataBreakpointThreadScopeTests.cpp` | watchpoint thread replay、secondary thread hit、stale registration cleanup | P1 |
| `P9.5` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITInt64CodegenSyntaxTests.cpp` | `DIVi64/MODi64` guard syntax、邻近分支一致性、syntax smoke | P1 |

### 验收标准补充（本轮深化）

1. running 态提前发送 `Continue` 不会再吞掉已经挂起的 `Pause` 或 data breakpoint stop；真正的 `HasStopped(reason="pause")` 仍会按下一次可断脚本行稳定到达。
2. multi-file module 下的 precompiled / VM fallback 源码定位会回到函数自己的 script section，而不是继续稳定串到 `Code[0]` 的首文件。
3. JIT caller 通过 `WriteDynamicCall()` 落到 VM callee 时，callee 的脚本异常会与解释执行保持一致地向外传播，不再 silent cleanup。
4. Windows data breakpoint 会对已登记的目标线程集合重放/清理，而不是继续只对调用 `UpdateDataBreakpoints()` 的单一线程偶发有效。
5. `asBC_DIVi64` 的 guard 文本始终生成合法 `[[unlikely]]` 语法，并有专门的 `int64` codegen syntax 自动化兜底。

### 风险与注意事项补充（本轮深化）

#### 风险

1. `P9.1` 若把 pending-stop 和 paused-state 的转换时机拆得不够严谨，容易把“stop 被吞掉”修成“重复 stop”或“旧 stop generation 泄漏到下一次运行”。
2. `P9.2` 触碰 precompiled function 的 source identity；若 section 持久化键选型不稳，可能把旧 cache 读成 invalid section，需要与 cache 版本/迁移策略一起评审。
3. `P9.3` 一旦异常桥接点不统一，可能出现“JIT 自身异常会置位、nested VM 异常也会置位，但文本/stack 来源不同步”的双重分叉；必须复用单一异常入口。
4. `P9.4` 若 thread registry/replay 只补安装不补 teardown，watchpoint 语义会从“单线程偶发漏停”变成“跨线程 stale handle / stale register”新问题。
5. `P9.5` 若只修 `DIVi64` 单条字符串、不补 smoke，`MODi64` 或邻近 arithmetic opcode 仍可能以同类拼写错误回归。

#### 已知行为变化

1. `Continue` 将从“可能顺手清掉尚未兑现的 stop”收紧为“只释放已进入 paused-state 的 stop”；历史上依赖这类 accidental clear 的前端时序假设需要同步调整。
2. multi-file precompiled fallback 的 callstack / exception / source-location 输出会从“总是首文件”变成“按函数真实 section”；旧 golden 文本和截图类断言需要更新。
3. nested VM callee 的异常不再 silent return；依赖旧默认值/残留寄存器副作用继续执行的脚本将被收紧为正式异常路径。
4. Windows watchpoint 的“支持范围”会从隐式单线程实现细节，变成显式 thread registry / replay 或 explicit unsupported；旧客户端若默认假设“只要设置成功就全线程有效”，需要同步改成看回执和 capability。

---

## 深化 (2026-04-09 02:09)

以下条目补足当前文档尚未承接的两组残余缺口：一组是 `DebugServer` 在 accepted-socket transport mode 与 session-level contract 上仍缺少显式初始化/前端无关 conformance harness；另一组是 `StaticJIT` 在 JIT 源码元数据、precompiled self-gate 与 reference-return codegen invariant 上还留着确定性漏洞。它们都已经在最新 `Issue-84/85/86`、`NewTest-26` 与 `Arch-DT52/53` 中形成多维证据，但尚未进入现有 `P1-P9` 的执行面。

### 新增 Phase 10：收口 DebugServer transport 初始化与 session contract 验证

- [ ] **P10.1** 在 accepted-socket 边界显式初始化 `non-blocking` / `no-delay`，禁止 `DebugServer` 继续依赖平台默认 socket 模式
  - 现有 `P1.2` 已承接 framing、partial send 与 backpressure；但 transport 在更早的 accepted-socket 边界仍然缺少显式初始化。`HandleConnectionAccepted()` 当前只记录日志并把 `ClientSocket` 入 `PendingClients`，后续 `ProcessMessages()` / `TrySendingMessages()` 就直接在这条原始 socket 上执行 `HasPendingData()`、`Recv()` 和 `Send()`。这意味着整个调试协议仍然在默认假设“accepted socket 一定已经是 non-blocking / no-delay”。
  - 本项不重复 `P1.2` 的 parser/backpressure 重写，而是补齐 transport 的第一个必经入口：新增统一的 accepted-socket 初始化 helper，把 `SetNonBlocking(true)`、`SetNoDelay(true)` 与失败时的 close/destroy 收口到同一处；无论后续 transport 继续留在 `FAngelscriptDebugServer` 还是下沉到 `Transports/`，都必须先经过这条 helper，不能再让 raw `FSocket*` 直接进入会话数组。
  - 这一步还要把“初始化失败”定义成显式拒绝，而不是把半初始化连接塞进 `PendingClients` 等后续循环再偶发报错；这样 `Issue-84`、`Issue-4` 和 `Issue-30` 才能落到同一个 transport-lifecycle 合同上，而不是各自以“blocking read”“握手超时”“disconnect 伪装 timeout”的不同症状反复出现。
  - 来源：
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-84 — “accepted client socket 未显式切换为 `non-blocking` / `no-delay`，DebugServer 传输层依赖未声明的 socket 默认值”
    - [C] `DebuggingAndJIT_TestGaps.md` Issue-4 / Issue-30 / Issue-32 — “connect timeout、disconnect liveness 与握手成功条件目前都被弱化成模糊超时或宽松 envelope 判断”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT1 — “transport、session 与 capability 仍压在同一个 Runtime 类里，transport mode 应在 ownership 边界显式收口”
    - [E] `CrossComparison.md` [D5] — “当前仓库已把 framing/protocol 当正式 contract 维护，live transport 不应继续依赖底层默认值”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L395-L399 — `HandleConnectionAccepted()` 仍只做日志 + `PendingClients.Enqueue(ClientSocket)`，没有任何 socket option 初始化；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L724-L789 — accepted client 进入主循环后立即参与 `HasPendingData()` / `Recv()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L2845-L2858 — 发送路径也仍直接在同一 raw socket 上执行 `Client->Send(...)`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugSocketTransport.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugSocketTransport.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp`
- [ ] **P10.1** 📦 Git 提交：`[Debug/Transport] Fix: initialize accepted sockets with explicit transport mode`
- [ ] **P10.1-T** 单元测试：验证 accepted client 只有在显式完成 transport mode 初始化后才会进入调试会话
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp`
  - 测试场景：
    - 正常路径：accepted socket 在进入 `PendingClients` 之前稳定执行 `SetNonBlocking(true)` 与 `SetNoDelay(true)`，后续 loopback / fake-socket transport 仍可正常收发 `StartDebugging` 与 `DebugServerVersion`。
    - 边界条件：如果只有一个 transport option 初始化失败，helper 仍会拒绝该 client，并走统一 close/destroy 路径，而不是把“半初始化 socket”留给 `ProcessMessages()`。
    - 错误路径：初始化失败的连接不会进入 `PendingClients` / `ClientsThatAreDebugging`，也不会在后续被误报成 framing 错误、握手超时或普通 disconnect。
  - 测试命名：`Angelscript.CppTests.Debug.Transport.AcceptedSocketRequiresExplicitTransportMode`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P10.1-T** 📦 Git 提交：`[Test/Debug] Test: add accepted-socket transport mode coverage`

- [ ] **P10.2** 为 `DebugServer` 增加前端无关的 `DebugSessionTranscript` conformance harness，冻结 `Start/Pause/Disconnect/CallStack/Continue` 的 session contract
  - 当前计划已经逐项补具体场景测试，但 session 语义本身仍散落在 `FAngelscriptDebugServer` 的状态机里，没有一层前端无关的 transcript contract 来冻结“哪些消息必须按什么顺序出现、哪些状态切换不允许静默降级”。这也是为什么 `Issue-28`、`Issue-30`、`Issue-32` 和 `NewTest-22/24/25` 会以“行号断言太弱”“disconnect 被伪装成 timeout”“handshake 只看 message type”“pause/disconnect 生命周期缺专门回归”的形式反复出现。
  - 本项不重复现有 scenario test 的业务脚本夹具，而是下沉一个最小 runtime seam：让 `StartDebugging -> DebugServerVersion -> HasStopped -> RequestCallStack -> HasContinued`、`Pause -> HasStopped(reason="pause") -> Continue`、`Disconnect/StopDebugging` cleanup 等会话序列可以在不依赖真实 socket 的前提下被 transcript 测试直接驱动，并作为后续 `V2` / `DAP` / 其它 bridge 共用的 golden behavior。
  - 关键目标不是再多写一组 happy-path 断言，而是把“额外 envelope”“断线后残留 state”“invalid frame 静默回落”“pause/continue 顺序漂移”等现象都收敛为 transcript 级失败。这样以后无论协议字段怎么扩，都会先撞上 session contract，而不是继续只靠 payload round-trip 或单个脚本场景偶然发现。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 3 / 4 / 8 — “`StopDebugging` 会全局清状态、`RequestCallStack` 与 stop event 无法一一对应、invalid frame 会静默回落到 frame 0”
    - [C] `DebuggingAndJIT_TestGaps.md` Issue-28 / Issue-30 / Issue-32 / NewTest-22 / NewTest-24 / NewTest-25 — “现有 stepping source 断言偏弱、disconnect 被伪装成 timeout、handshake 过宽、lifecycle/pause 场景仍缺独立护栏”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT52 — “当前测试主要停在 payload round-trip，缺少 session 级 conformance harness”
    - [E] `CrossComparison.md` [D5] — “当前 Angelscript 选择自己拥有调试协议 contract，就必须同时拥有对该 session contract 的 golden 验证”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L548-L627 — `step` / `breakpoint` 的 stopped-event 生成与 breakpoint 命中判定仍直接内嵌在运行时状态机里；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L667-L699 — `PauseExecution()` 直接负责 `HasStopped`、阻塞等待与 `HasContinued`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L897-L927 — `StartDebugging` / `StopDebugging` / `RequestCallStack` 仍在同一消息分发链里直接修改 session 状态。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugSessionTranscript.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugSessionTranscript.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugSessionConformanceTests.cpp`
- [ ] **P10.2** 📦 Git 提交：`[Debug/Conformance] Feat: freeze debug session transcript contract`
- [ ] **P10.2-T** 单元测试：用 transcript harness 冻结 `Pause`、`Disconnect`、`CallStack` 与 `Continue` 的事件序列
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugSessionConformanceTests.cpp`
  - 测试场景：
    - 正常路径：`StartDebugging -> DebugServerVersion -> breakpoint stop -> RequestCallStack -> Continue -> HasContinued` 的 transcript 稳定可回归，且 `CallStack` 请求与当前 stop 一一对应。
    - 边界条件：`Pause` 在循环脚本中触发时能产生 `reason="pause"` 的停点；最后一个 debugging client `Disconnect` 或 paused-state `StopDebugging` 后，下一次会话从干净状态重新启动，不继承旧 transcript 残留。
    - 错误路径：远端断开、握手版本错误或 invalid frame 请求会在 transcript 层返回确定性失败，而不是退化成 timeout、额外 envelope 被吞掉，或静默回落到 frame 0。
  - 测试命名：`Angelscript.CppTests.Debug.Conformance.SessionTranscriptCoversPauseDisconnectAndCallstack`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P10.2-T** 📦 Git 提交：`[Test/Debug] Test: add debug session transcript conformance coverage`

### 新增 Phase 11：补齐 JIT 源码元数据、自验证 gate 与 return invariant

- [ ] **P11.1** 把 JIT `SCRIPT_DEBUG_FILENAME` 从 `ModuleName` 改成真实 source filename，并显式区分 `SourceFilename` 与 `ModuleName`
  - 当前 `P2.1` 和 `P9.2` 已分别承接 “JIT 栈要接入 DebugServer” 与 “precompiled source identity 不能停留在模块首文件”；但 JIT debug metadata 源头本身还留着一个更底层的错误：`SCRIPT_DEBUG_FILENAME` 目前固定写成 `File.ModuleName`，随后整条 `FScopeJITDebugCallstack` 链都把这个 module 级字符串当成源码文件名继续向上游传播。
  - 本项不等待完整 `JitSourceMap` 或更大的 source-map 重构，而是先把元数据源头纠正为可信输入：为每个 JIT 函数或至少每个 source section 生成真实 filename symbol，`debugCallStack` 内部同时保存 `SourceFilename` 与 `ModuleName` 两个概念；上层 `GetStackTrace()` / `CallStack` / 后续 source navigation 都只消费真实 `SourceFilename`。如果短期拿不到精确 section，也要返回 invalid/diagnostic，而不能继续回落到 module name 伪装成“文件名”。
  - 这一步和 `P2.1` / `P9.2` 是互补关系：前两项解决“JIT 栈能不能被看见”和“precompiled fallback 会不会串首文件”，本项解决“即使 JIT 栈被看见，它回报的文件名是不是一开始就错了”。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 75 — “`GetAngelscriptExecutionFileAndLine` 的 JIT 分支当前已在文件/行号入口上失稳，JIT 下源码定位仍缺测试护栏”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-85 — “`SCRIPT_DEBUG_FILENAME` 被固定写成 `ModuleName`，JIT 栈即使接入 DebugServer 也会持续报告伪源码路径”
    - [C] `DebuggingAndJIT_TestGaps.md` Issue-28 — “现有 stepping 用例只校验行号和栈深，不校验 `Source`，错误 filename 仍可能绿灯”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT24 — “JIT / precompiled source identity 不能继续压缩成模块级锚点”
    - [E] `CrossComparison.md` [D5] — “当前仓库既然自己拥有调试 contract，就必须返回真实 source，而不是 module 级伪路径”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h` L122-L128 — `FJITFile` 仍同时保存 `Filename` 与 `ModuleName`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` L3612-L3621 — `SCRIPT_DEBUG_FILENAME` 仍用 `File.ModuleName` 生成 symbol；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h` L194-L209、L338-L340 — `FScopeJITDebugCallstack` 与 `SCRIPT_DEBUG_CALLSTACK_FRAME*` 仍把这条单一 `Filename` 继续塞进运行期 debug 栈。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITSourceMetadataTests.cpp`
- [ ] **P11.1** 📦 Git 提交：`[StaticJIT/DebugMeta] Fix: emit real source filenames for jit debug frames`
- [ ] **P11.1-T** 单元测试：验证 JIT 顶帧回报的 `Source` 来自真实脚本 section，而不是 module name
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITSourceMetadataTests.cpp`
  - 测试场景：
    - 正常路径：同一 module 下两个不同脚本文件各自产生 JIT frame 时，顶帧 `Source` 稳定回报真实文件名，而不是共享同一个 module name。
    - 边界条件：两个文件具有相同行号时，JIT `Source` 仍能区分文件身份，不会再次因为“同号行”掩盖错位。
    - 错误路径：缺失 source metadata 时返回 explicit invalid/diagnostic，而不是继续用 `ModuleName` 伪装成有效路径。
  - 测试命名：`Angelscript.CppTests.StaticJIT.Debug.SourceFilenameMatchesRealSection`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P11.1-T** 📦 Git 提交：`[Test/StaticJIT] Test: add jit source filename metadata coverage`

- [ ] **P11.2** 把 precompiled cache admission 收口成带 reject reason 的 self-gate 合同，并补齐 `BuildIdentifier` / `DataGuid` 错配回归
  - 现有 `P4.2` 已承接“runtime validation -> activation/fallback”的大面，但 cache admission 入口本身仍只有一层布尔/日志式 gate：`IsValidForCurrentBuild()` 只返回布尔值，startup path 只在 `BuildIdentifier` 或 `DataGuid` 错配时打印 warning 并丢弃/清空，不返回结构化 reject reason，也没有独立 archive/error-path 回归去锁住这些 self-gate。结果是“为什么没激活 precompiled/JIT”这件事仍然更多依赖日志字符串和人工排查。
  - 本项不扩成完整 `ToolchainReceipt`；第一阶段只把 admission 结果显式化为最小 report，例如 `BuildIdentifierMismatch`、`TranspiledGuidMismatch`、`Accepted` 三类 reason，并让 engine load path 返回可测试、可记录的 reject result。这样既能接住 `NewTest-26` 的 stale cache 场景，也能和 `D11 self-gate` 的现有 hard gate 口径对齐，而不是让未来任何工具都只能重新解析 warning 文案。
  - 这一步还要顺手补一条 deterministic archive regression：同一条 cache 在“匹配 build”“build id 错配”“guid 错配”三种 admission 结果下都给出稳定结论，不再把 `bStaticJITTranspiledCodeLoaded` 或“是否看到了某条 warning”当成唯一信号。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 5 — “函数级 JIT miss 与全局 `bStaticJITTranspiledCodeLoaded` 会静默掩盖真实激活结果”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-26 — “`BuildIdentifier` 不匹配时必须把 stale precompiled cache 判为不可用”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT53 — “docs/JIT/precompiled 等 producer 缺少统一 freshness / receipt contract”
    - [E] `CrossComparison.md` [D11-SelfGate] — “当前 Angelscript 已有 `BuildIdentifier` / `DataGuid` 级 hard gate，下一步应补 machine-readable rejection signal，而不是削弱 gate”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2642-L2645 — `IsValidForCurrentBuild()` 仍只有简单布尔判断；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1531-L1555 — startup load path 仍只在 `BuildIdentifier` / `PrecompiledDataGuid` 错配时打印 warning 并丢弃或 `FJITDatabase::Get().Clear()`，没有结构化 reject reason 可供测试或工具消费。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptPrecompiledActivationReport.h`；`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataArchiveTests.cpp`
- [ ] **P11.2** 📦 Git 提交：`[StaticJIT/Precompiled] Refactor: surface explicit precompiled reject reasons`
- [ ] **P11.2-T** 单元测试：验证 stale precompiled cache 的 admission result 在 build-id / guid 错配下都可稳定断言
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataArchiveTests.cpp`
  - 测试场景：
    - 正常路径：匹配的 `BuildIdentifier` 与 `PrecompiledDataGuid` 会返回 `Accepted`，precompiled data 与 transpiled code 都可继续激活。
    - 边界条件：仅 `BuildIdentifier` 错配时，cache 被整体拒收并返回明确 reject reason，而不是只依赖 warning 文案。
    - 错误路径：`PrecompiledDataGuid` 错配时，precompiled data/load path 会返回显式“禁用 transpiled code”原因并清空 JIT 激活，而不是让测试只能从间接副作用猜测。
  - 测试命名：`Angelscript.TestModule.StaticJIT.PrecompiledData.AdmissionRejectReasonIsDeterministic`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P11.2-T** 📦 Git 提交：`[Test/StaticJIT] Test: add precompiled admission reject coverage`

- [ ] **P11.3** 抽出 `reference-return` 返回前寄存器准备 helper，恢复 `asBC_SaveReturnValue` 与 `asBC_RET` 的单一 invariant
  - 当前 reference-return 路径已经出现前后段 contract 漂移：`asBC_SaveReturnValue` 在 `bReturnIsReference` 分支里会主动 `MaterializeValueRegister()`，而 `asBC_RET` 却只保留了一条永真式 `check(Context.ValueRegisterState != ValueRegister || Context.ValueRegisterState != Indeterminate)`。这使得 `RET` 端事实上不再验证自己拿到的是否真是可返回的 `l_valueRegister`，原本应由前后两段共同维护的 invariant 退化成了“靠前序 bytecode 侥幸准备正确”。
  - 本项不只修一条布尔表达式；更关键的是把“reference-return 之前必须拿到 materialized value register”收敛成统一 helper，让 `SaveReturnValue` 与 `RET` 共用同一入口。debug/test 构建可以在发生 late materialization 时打出明确诊断，release/codegen 产物仍保证正确生成，避免未来再次出现“前半段 materialize、后半段断言漂移”的双轨逻辑。
  - 这样做既能恢复当前 reference-return 的状态护栏，也能把这类 `EValueRegisterState` invariant 真正前移到 codegen 阶段，而不是继续等未来某条 bytecode 路径把寄存器准备弄丢后再 silent miscompile。
  - 来源：
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-86 — “`asBC_RET` 的 reference-return 校验写成永真式，JIT 返回路径已失去寄存器状态护栏”
    - [C] `DebuggingAndJIT_TestGaps.md` Issue-18 — “现有 `StaticJIT` 覆盖仍缺少真正锁住 codegen invariant 的 direct tests”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT2 — “StaticJIT 需要可记录、可回归的 codegen/fallback 合同”
    - [E] `CrossComparison.md` [D8] — “当前 Angelscript 的优势在于把 contract 前置到 build/codegen 阶段，而不是把错误拖到更晚的运行期或编译期”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` L2598-L2605 — `asBC_SaveReturnValue` 在 reference-return 路径仍会主动 `Context.MaterializeValueRegister()`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` L2694-L2702 — `asBC_RET` 的 reference-return 分支仍保留永真式 `check(...)`，随后无条件 `return ({0})l_valueRegister;`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITReferenceReturnTests.cpp`
- [ ] **P11.3** 📦 Git 提交：`[StaticJIT/RET] Fix: enforce reference-return register invariant`
- [ ] **P11.3-T** 单元测试：验证 reference-return 在正常、late-materialize 与 invariant 回归场景下都能给出确定结果
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITReferenceReturnTests.cpp`
  - 测试场景：
    - 正常路径：reference-return 函数在 value register 已准备好的情况下生成并返回正确结果，不影响现有 fast path。
    - 边界条件：当 `RET` 前寄存器状态仍是 `Indeterminate` 或需要 late materialization 时，会先走统一 helper 收口状态，再生成正确返回语句。
    - 错误路径：若未来再次把 reference-return invariant 拼坏，测试会在 codegen 级直接失败，而不是继续 silent 生成 `return l_valueRegister` 掩盖状态错误。
  - 测试命名：`Angelscript.CppTests.StaticJIT.Codegen.ReferenceReturnRegisterInvariant`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P11.3-T** 📦 Git 提交：`[Test/StaticJIT] Test: add reference-return register invariant coverage`

### 单元测试总览补充（本轮深化 2026-04-09 02:09）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P10.1` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` | accepted socket option init、partial init failure reject、transport mode explicitness | P1 |
| `P10.2` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugSessionConformanceTests.cpp` | transcript contract、pause/disconnect cleanup、invalid frame / dead-socket deterministic failure | P1 |
| `P11.1` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITSourceMetadataTests.cpp` | real source filename、same-line multi-file identity、invalid metadata reject | P1 |
| `P11.2` | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataArchiveTests.cpp` | build-id accept/reject、guid mismatch disable、deterministic admission reason | P1 |
| `P11.3` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITReferenceReturnTests.cpp` | reference-return fast path、late materialize、invariant regression guard | P1 |

### 验收标准补充（本轮深化 2026-04-09 02:09）

1. `DebugServer` 只会在 accepted socket 显式完成 `non-blocking` / `no-delay` 初始化后才把 client 纳入会话；初始化失败不会再伪装成后续 framing/handshake 问题。
2. 至少存在一组前端无关的 transcript conformance tests，能稳定冻结 `StartDebugging`、`Pause`、`Disconnect`、`RequestCallStack` 与 `Continue` 的消息序列和状态切换。
3. JIT `CallStack` / `GetStackTrace` 顶帧使用真实 source filename，而不是 `ModuleName` 伪路径；同 module 多文件场景可被自动化区分。
4. precompiled cache admission 会返回稳定的 reason-coded 结果，`BuildIdentifier` 与 `PrecompiledDataGuid` 错配都能被单元测试直接断言，不再只靠 warning 文案判断。
5. `reference-return` 的寄存器准备 invariant 已恢复为单一 helper 合同；`asBC_SaveReturnValue` 与 `asBC_RET` 不再各自维护漂移中的半套规则。

### 风险与注意事项补充（本轮深化 2026-04-09 02:09）

#### 风险

1. `P10.1` 如果只在部分 accept 路径补 `SetNonBlocking` / `SetNoDelay`，而其它 release/reconnect 路径继续旁路 helper，会让平台差异从“隐式默认值”变成“部分路径初始化、部分路径未初始化”的新分叉。
2. `P10.2` 若 transcript harness 的 seam 切得过深，容易把 runtime session 逻辑反向绑到测试实现；必须坚持“新增 sink/transport seam，不改变现有 `V2` wire format”的边界。
3. `P11.1` 若只在上层输出字符串时替换 module name，而不修正 `SCRIPT_DEBUG_FILENAME` 源头，后续 `CallStack`、异常与资产元数据仍会继续从错误字段读值。
4. `P11.2` 若 reject reason 只是新加 enum 但没有和实际 admission 分支绑定，测试可能反而锁住一层假 contract；必须让 build-id/guid 分支共享同一个权威 report 入口。
5. `P11.3` 若只把永真式改成另一条 `check`，却不收口 `SaveReturnValue` / `RET` 的共同 helper，reference-return invariant 仍可能在未来再次前后漂移。

#### 已知行为变化

1. accepted socket 初始化失败将从“偶发进入后续 handshake/transport 异常”收紧为“在 accept 边界直接拒绝连接”；依赖旧模糊超时行为的测试或外部 client 需要同步改成看初始化失败诊断。
2. session transcript 失败会比当前 scenario tests 更早、更严格地暴露额外 envelope、dead socket 或 invalid frame；一些历史上“最终也能跑通”的宽松 helper 断言会被收紧。
3. JIT 顶帧与相关源码定位输出会从 `ModuleName` 切换为真实 source filename；旧截图、golden 文本和依赖 module-name 伪路径的调试适配层需要更新。
4. stale precompiled cache 的 admission 将从“日志可见但结构化结果不可见”收紧为“带 reject reason 的显式合同”；依赖 warning 文案匹配的旧脚本或工具应改读结构化结果。
5. reference-return 路径将从“默认信任前序 bytecode 已准备好寄存器”收紧为“返回前统一 materialize/断言”；依赖旧 silent behavior 的 codegen 文本或调试日志可能出现可预期变化。

---

## 深化 (2026-04-09 02:25)

以下条目补足当前文档尚未显式承接的两组残余缺口：`DebugServer` 在 teardown、malformed admission 与 legacy raw-address watchpoint 上仍有确定性边界漏洞；`StaticJIT` 则在 fully precompiled 源码校验与 native-form registry 生命周期上还停留在“cache 自证 + process-global 副作用”。本轮不重复 `P10.1` 的 accepted-socket 初始化、`P3.1` 的 verified/reject backend、`P11.2` 的 reject reason，也不重复 `P8.1` 的 template signature lookup，只追加这些仍未落地的 lifecycle / admission / self-gate 修复。

### 新增 Phase 12：收口 DebugServer 生命周期与协议入口 admission

- [ ] **P12.1** 为 accepted client 引入统一 `ReleaseClientSocket()`，把 `Disconnect`、超时移除与 server 析构全部收口到“摘状态 + `Close()` + `DestroySocket()`”
  - `P10.1` 已经把 accepted socket 的进入路径收口成显式初始化，但退出路径仍然分散：tick loop 的 dead client 清理、显式 `Disconnect` 与 `FAngelscriptDebugServer` 析构都还停留在“关连接不销毁对象”或“只停 listener 不 drain client”的半完成状态。只要 editor 重连、自动化重复建连或 client 异常退出，这条路径就会稳定累积 socket 资源与 stale session state。
  - 本项不再新增第二套 cleanup 逻辑，而是引入单一 `ReleaseClientSocket(FSocket* Client, EDebugClientReleaseReason Reason)` 或等价 helper，统一负责：从 `PendingClients`、`Clients`、`ClientsThatWantDebugDatabase`、`ClientsThatAreDebugging`、`QueuedSends`、`CallstackRequests` 等容器摘除该指针；必要时更新 `bIsDebugging` / `bIsPaused` / `ClearAllBreakpoints()`；最后执行 `Client->Close()` 与 `ISocketSubsystem::DestroySocket(Client)`。后续 transport 若继续下沉到 `Transports/`，也必须复用同一退出 helper，不能再次分叉。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 64 — “已接受的 `FSocket*` 在异常断开和显式 `Disconnect` 路径上只 `Close()` 不 `DestroySocket()`”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-8 — “client cleanup 缺少统一 release helper，析构与断线/超时路径都会遗留未销毁 socket”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-22 / Issue-30 — “最后一个 debugging client 的 `Disconnect` cleanup 与 dead-socket 诊断都缺少独立自动化护栏”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT1 — “transport 与 session lifecycle 仍压在同一 Runtime 类里，生命周期 authority 没有单一 owner”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L585-L588、L709-L721 — server 仍以多组原始 `FSocket*` 容器持有 client；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L420-L432 — 析构函数只停止并释放 `Listener`，没有释放任何 accepted client；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L724-L747 — 断线/超时移除仍只 `Close()` 后摘容器；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1191-L1194 — `Disconnect` 分支仍只有 `Client->Close()`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp`
- [ ] **P12.1** 📦 Git 提交：`[Debug/Transport] Fix: unify client socket release and destruction`
- [ ] **P12.1-T** 单元测试：验证显式 `Disconnect`、超时移除与 server shutdown 都走同一条 socket release 路径
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp`
  - 测试场景：
    - 正常路径：client 发送 `Disconnect` 后，`Clients` / `ClientsThatAreDebugging` / `QueuedSends` 会被同步摘除，且 socket destroy 计数恰好增加 1。
    - 边界条件：发送队列超时或最后一个 debugging client 被移除时，也会复用同一 helper 清理 session state，不残留 `bIsDebugging`、`bIsPaused` 或断点状态。
    - 错误路径：server 析构时即使仍有 `PendingClients` 或活动 client，也会对每个 socket 恰好销毁一次，不再把 dead socket 伪装成后续 timeout。
  - 测试命名：`Angelscript.CppTests.Debug.Transport.ClientReleaseHelperHandlesDisconnectTimeoutAndShutdown`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P12.1-T** 📦 Git 提交：`[Test/Debug] Test: add client socket release lifecycle coverage`

- [ ] **P12.2** 把 `BreakOptions` 改成严格反序列化入口，封死 `FilterCount` / `truncated body` / `unknown filter` 这类 malformed request 面
  - 当前 `P3.2` 已经承接了 `BreakFilters/BreakOptions` 的 capability truthful 语义，但消息入口本身仍然是不设防的：`FAngelscriptBreakOptions::operator<<` 会先读 `FilterCount` 再立即 `SetNum(FilterCount)`，`HandleMessage(BreakOptions)` 又会在没有任何验证的前提下覆盖运行时 `BreakOptions`。这意味着一条合法 envelope 内的畸形 body 仍能在反序列化阶段触发超大分配、负长度断言或静默注入未知 filter。
  - 本项不重复 stop-filter 语义本身，而是补齐协议入口的 strict-read contract：新增 `TryReadBreakOptions(...)` 或等价 helper，先校验 `FilterCount >= 0`、不超过明确上限、剩余 payload 足够、filter 名称属于当前 server 已公布集合；只有整个 body 成功解析后才提交到运行时状态，并统一补 `break:any`。若解析失败，server 必须保留旧 `BreakOptions`，返回 deterministic diagnostic 或 explicit reject，而不是 silent clobber。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 57 — “`BreakOptions` 直接信任网络包里的 `FilterCount`，恶意请求可在反序列化阶段触发超大分配或断言”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-36 — “`BreakOptions` 需要先验证计数字段与 body，再分配容器”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-15 — “现有测试只验证 filter 列表存在，没有覆盖 `BreakOptions` 请求本身的严格输入边界”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT52 — “调试验证层仍停留在 payload round-trip，缺少能冻结 session/protocol boundary 的 conformance 护栏”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L466-L479 — `FAngelscriptBreakOptions::operator<<` 在 loading 侧直接 `SetNum(FilterCount)`，无上限或 body 完整性校验；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1137-L1145 — `HandleMessage(BreakOptions)` 反序列化后立即 `BreakOptions.Empty()` 并覆盖运行时 filter 集合。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakOptionsTests.cpp`
- [ ] **P12.2** 📦 Git 提交：`[Debug/Protocol] Fix: strict-read break options before mutating runtime state`
- [ ] **P12.2-T** 单元测试：验证合法 `BreakOptions` 仍生效，未知/畸形 body 会被拒绝且不会污染当前 filter 状态
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakOptionsTests.cpp`
  - 测试场景：
    - 正常路径：发送合法 `BreakOptions` 后，server 会应用已公布 filter，并自动追加 `break:any`；断点过滤行为与 provider 一致。
    - 边界条件：发送空列表或未知 filter 时，server 返回 explicit reject / no-op，现有 `BreakOptions` 保持不变，后续合法请求仍可工作。
    - 错误路径：`FilterCount = -1`、超大 `FilterCount` 或 body 截断时，server 不会崩溃、不做大分配，也不会把 runtime `BreakOptions` 改成半解析状态。
  - 测试命名：`Angelscript.TestModule.Debugger.BreakOptions.StrictParseRejectsMalformedRequests`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P12.2-T** 📦 Git 提交：`[Test/Debug] Test: add strict break-options parsing coverage`

- [ ] **P12.3** 给 legacy `SetDataBreakpoints` 增加 admission 校验，禁止任意 `Address` / `AddressSize` / `bCppBreakpoint` 直接写进硬件监视后端
  - `P3.1` 已经承接了 watchpoint backend 的 verified/reject、capacity 与 clear flow，但它默认“请求本身是可信的”。当前 `SetDataBreakpoints` 仍直接接受客户端下发的 `Address`、`AddressSize` 与 `bCppBreakpoint`，随后把这些值写进权威数组并尝试安装到 `DR0-DR3`。这让 data breakpoint 仍保留一个更靠前的漏洞面：任意 client 都可以请求监视任意地址，非法尺寸又会静默退化成 1-byte watchpoint，而 `bCppBreakpoint=true` 还能把命中升级到 `UE_DEBUG_BREAK()`。
  - 本项不一次性完成完整 symbolic watchpoint 架构，而是先把 legacy raw-address 路径收紧成“session-issued monitor target + 明确兼容门控”的最小合同：`RequestVariables/RequestEvaluate` 继续输出 addressable watch entity，但 server 需要为可监视实体建立 per-session allowlist 或 `MonitorToken`；`SetDataBreakpoints` 在写入 `DataBreakpoints` 前先调用 `ValidateLegacyDataBreakpointRequest(...)`，强制 `AddressSize` 属于 `{1,2,4,8}`、地址来自当前 session 的可监视实体、`bCppBreakpoint` 由 server 侧推导而不是客户端可写。对于 `ValueSize > 8`、stale/out-of-scope target、伪造地址与伪造 `bCppBreakpoint`，都必须返回 explicit reject。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 71 — “`SetDataBreakpoints` 直接信任客户端提供的原始地址和 `bCppBreakpoint`，可对任意进程地址安装硬件写监视”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-41 — “先封住任意地址、非法尺寸与 client-authored `bCppBreakpoint` 的 admission 入口，再继续谈 watchpoint 细节语义”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-10 — “当前 data breakpoint 自动化只覆盖 `Evaluate -> ValueAddress` 正例命中，没有非法地址/尺寸/跨 scope 拒绝场景”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT31 — “watchpoint 后端不应继续把 raw address 当作对外协议事实”
    - [E] `CrossComparison.md` [D5] 变量物化模型 — “当前 AS 的变量协议优势在于 addressable watch entity；不能再让协议入口退化回任意地址写寄存器”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L304-L323 — `FAngelscriptDataBreakpoint` 仍直接序列化 `Address`、`AddressSize`、`bCppBreakpoint`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1061-L1067 — `SetDataBreakpoints` 仍直接 `DataBreakpoints = BP.Breakpoints; UpdateDataBreakpoints();`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L160-L165、L193-L199 — `AddressSize` 只有 `1/2/4/8` 四种显式编码，其它值会静默落回默认设置；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L329-L353 — `bCppBreakpoint` 命中后仍可直接升级到 `UE_DEBUG_BREAK()`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointValidationTests.cpp`
- [ ] **P12.3** 📦 Git 提交：`[Debug/DataBreakpoint] Fix: validate legacy watchpoint admission before backend apply`
- [ ] **P12.3-T** 单元测试：验证只有当前 session 授权的 monitor target 才能进入 legacy data breakpoint 后端
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointValidationTests.cpp`
  - 测试场景：
    - 正常路径：通过 `Evaluate/Variables` 获取的当前 session monitor target 可以被接受，并继续触发既有 verified/stop/clear 流程。
    - 边界条件：`AddressSize > 8`、target 已离开作用域或 monitor token 已过期时，server 会返回 explicit reject，且不会占用硬件槽位。
    - 错误路径：客户端伪造任意地址、伪造 `bCppBreakpoint=true` 或跨 session 重放旧 target 时，server 不会安装 watchpoint，也不会触发 `UE_DEBUG_BREAK()` 或污染现有 debugging state。
  - 测试命名：`Angelscript.TestModule.Debugger.DataBreakpoint.LegacyAdmissionRejectsForgedTargets`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P12.3-T** 📦 Git 提交：`[Test/Debug] Test: add legacy data-breakpoint admission coverage`

### 新增 Phase 13：补齐 StaticJIT source gate 与 native-form registry 生命周期

- [ ] **P13.1** 把 fully precompiled `CodeHash` 从 cache-vs-cache 伪校验改成 source-backed validation，或显式标记 `SourceHashValidationUnavailable`
  - `P11.2` 已经把 precompiled admission 的 `BuildIdentifier` / `DataGuid` reject reason 显式化，但 fully precompiled 路径上那条“file content hashes are the same”注释对应的仍是 cache 对比 cache：`InitFrom()` 先把 `ModuleDesc->CodeHash` 写进 precompiled data，`GetModulesToCompile()` 又把同一个 `CodeHash` 回填到运行时 `ModuleDesc`，最后 `InitialCompile()` 里再拿两份都来自 cache 的值做“校验”。这会把没有读取当前源码的结果伪装成“已核对源码内容”。
  - 本项不再新增另一层布尔 gate，而是把内容真实性校验独立成权威入口，例如 `ValidatePrecompiledModuleSources(...)`：当源码仍可访问时，按当前 `AngelscriptPreprocessor.cpp` 使用的同一 `XXH64` 规则重算 fresh hash；只有 fresh hash 与 cache 中 `CompiledModule->CodeHash` 一致，才允许激活对应 module 的 precompiled/JIT 产物。当运行环境本来就不携带源码时，必须把状态显式标成 `SourceHashValidationUnavailable`，而不是继续沿用 misleading 注释和 cache 自证。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 61 — “fully precompiled 模式下的 `CodeHash` 校验是在拿 cache 对比 cache，当前脚本内容没有参与验证”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-76 — “fully precompiled 需要 source-backed validation，或明确承认当前没有做源码校验”
    - [C] `DebuggingAndJIT_TestGaps.md` NewTest-26 — “stale precompiled cache 的 build/self-gate 场景还缺少 archive/error-path 回归”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT53 — “Docs/StaticJIT/PrecompiledData 仍缺统一 build receipt 与 artifact freshness contract”
    - [E] `CrossComparison.md` [D11-SelfGate] — “当前 AS 的 self-gate 优势成立前提是 gate 真正基于 artifact freshness，而不是自我比较”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L1417-L1420 — `InitFrom()` 仍把 `Context.ModuleDesc->CodeHash` 原样写入 cache；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L2752-L2775 — `GetModulesToCompile()` 又把 cache 中 `Module.CodeHash` 原样回填到 `ModuleDesc->CodeHash`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` L289-L301 — 只有 preprocessor 真实读取当前 `ProcessedCode` 时才会重新计算 section/module `CodeHash`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L4284-L4290 — fully precompiled 分支仍以 `CompiledModule->CodeHash == Module->CodeHash` 作为“文件内容一致”判断。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledSourceValidationTests.cpp`
- [ ] **P13.1** 📦 Git 提交：`[StaticJIT/Precompiled] Fix: validate fully-precompiled code hash against current source`
- [ ] **P13.1-T** 单元测试：验证 fully precompiled 模式的内容校验来自当前源码，或明确返回“当前无法校验源码”
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledSourceValidationTests.cpp`
  - 测试场景：
    - 正常路径：源码未变化时，fresh source hash 与 cache 一致，module 会继续通过 fully precompiled activation，且 report 标记为 validated。
    - 边界条件：运行环境不携带源码时，系统返回 `SourceHashValidationUnavailable` 或等价状态，而不是继续声称 hash 已匹配。
    - 错误路径：修改脚本源码但保留旧 cache 后，module 会被明确拒绝或降回 fallback，不再因为 cache-vs-cache 比较而误通过。
  - 测试命名：`Angelscript.TestModule.StaticJIT.PrecompiledData.SourceBackedHashValidation`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P13.1-T** 📦 Git 提交：`[Test/StaticJIT] Test: add fully-precompiled source validation coverage`

- [ ] **P13.2** 把 `GScriptNativeForms` 从 process-global 裸指针表下沉为 engine-scoped owned registry，并补 teardown / rebind 压力回归
  - 当前 `P8.1` 关注的是 template/native-form 的 lookup key 与槽位错绑，但 registry 本身仍是 process-global static map。`StaticJITBinds.cpp` 里的 `GScriptNativeForms` 以 `asIScriptFunction* -> FScriptFunctionNativeForm*` 形式长期常驻，所有 `BindNative*` / `BindUFunction` / template bind 入口都会 `new` 派生对象后直接 `Add` 进去，却没有任何 `Reset()`、`delete` 或 engine teardown 路径。这意味着就算 `P8.1` 修好了“怎么找”，当前代码仍然没有回答“谁拥有这些对象、什么时候该销毁它们”。
  - 本项先不等待完整 `Arch-DT13` manifest/sidecar 合同，而是先修当前运行期 owner：把 registry 改成 engine-owned 或 shared-state-owned 资源，value 使用 `TUniquePtr<FScriptFunctionNativeForm>` 或等价拥有型容器，并暴露统一 `RegisterNativeForm(...)` / `FindNativeForm(...)` / `ResetNativeFormsForEngine(...)`。如果短期仍需保留静态入口，也必须把 key 扩成带 engine 作用域的 identity，禁止继续只靠 stale `asIScriptFunction*` 复用旧条目。
  - 来源：
    - [A] `DebuggingAndJIT_Analysis.md` 发现 19 — “`GScriptNativeForms` 只增不减，native form 会跨引擎生命周期泄漏并保留 stale `asIScriptFunction*` 键”
    - [B] `DiscoveryPlans/DebuggingAndJIT_Plan.md` Issue-61 — “native-form registry 需要有 owner、teardown 与重复注册治理”
    - [C] `DebuggingAndJIT_TestGaps.md` Issue-18 / 覆盖快照 — “`StaticJIT` 运行时路径没有真正执行 coverage，`StaticJITBinds.cpp` 当前仍是 0 次文件名命中”
    - [D] `DebugAndToolchain_ArchReview.md` Arch-DT13 — “JIT native-form 元数据不应继续寄生在 ambient state + process-global registry 上”
    - [E] `CrossComparison.md` [D8] 生命周期 authority — “关键运行时对象应有显式 owner 和 teardown，而不是依赖进程级 cache 自行悬挂”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` L27-L59 — `GScriptNativeForms` 仍是 file-static `TMap<asIScriptFunction*, FScriptFunctionNativeForm*>`，`GetNativeForm()` 只做 `Find`，没有任何 owner/teardown 逻辑；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` L112-L116 — `BindNativeConstructor()` 仍直接 `new FScriptNativeConstructor(...)` 后塞进全局表；同文件其余 `BindNative*` / `BindUFunction` / template bind 入口也延续同样模式。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 或 shared-state 挂载点；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITNativeFormRegistryTests.cpp`
- [ ] **P13.2** 📦 Git 提交：`[StaticJIT/Registry] Refactor: scope native-form registry to engine lifetime`
- [ ] **P13.2-T** 单元测试：验证 native-form registry 不再跨引擎轮次保留 stale 条目或线性增长
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITNativeFormRegistryTests.cpp`
  - 测试场景：
    - 正常路径：单个 engine 生命周期内注册 native form 后，lookup 仍能稳定返回正确的 owned form，不影响现有 codegen fast path。
    - 边界条件：连续创建/销毁多个 engine scope 或重复执行 bind 注册后，registry 会在轮次间正确 reset，不再单调增长。
    - 错误路径：销毁旧 engine 后重建新 engine 时，不会因为复用旧 `asIScriptFunction*` 地址而命中 stale native form；重复注册同一函数时也不会泄漏旧 value。
  - 测试命名：`Angelscript.CppTests.StaticJIT.NativeFormRegistry.EngineScopedLifetime`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P13.2-T** 📦 Git 提交：`[Test/StaticJIT] Test: add native-form registry lifetime coverage`

### 单元测试总览补充（本轮深化 2026-04-09 02:25）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P12.1` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` | `Disconnect`、timeout remove、server shutdown 的统一 socket release/destroy | P1 |
| `P12.2` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakOptionsTests.cpp` | valid filter 生效、unknown filter 保持旧状态、malformed body 拒绝 | P1 |
| `P12.3` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointValidationTests.cpp` | session-issued monitor target、unsupported size reject、forged raw address / `bCppBreakpoint` reject | P0 |
| `P13.1` | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledSourceValidationTests.cpp` | fresh source hash、source-unavailable 显式状态、stale cache reject | P1 |
| `P13.2` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITNativeFormRegistryTests.cpp` | engine-scoped registry、multi-engine reset、stale pointer 不再复用 | P1 |

### 验收标准补充（本轮深化 2026-04-09 02:25）

1. `DebugServer` 的 client teardown 只保留一条权威 release 路径；显式 `Disconnect`、发送超时移除与 server 析构都会销毁 socket 对象，并同步清理 session state。
2. `BreakOptions` 在合法输入下继续生效，但任何负数/超大计数、body 截断或未知 filter 都不会再污染当前运行时 `BreakOptions`，也不会在反序列化阶段触发异常分配。
3. legacy `SetDataBreakpoints` 只接受当前 session 授权的 monitor target；伪造地址、非法尺寸与 client-authored `bCppBreakpoint` 都会被明确拒绝。
4. fully precompiled 模式下的内容校验要么基于当前源码重算 hash，要么显式报告 `SourceHashValidationUnavailable`；不能再把 cache 自我比较写成“源码一致”。
5. native-form registry 具备明确 owner 与 teardown；重复创建/销毁 engine 后，registry 大小不会线性增长，也不会把 stale `asIScriptFunction*` 重新解释为合法 native form。

### 风险与注意事项补充（本轮深化 2026-04-09 02:25）

#### 风险

1. `P12.1` 若 `ReleaseClientSocket()` 没有一次性覆盖 `PendingClients`、`QueuedSends`、`CallstackRequests` 与 debugging-state 清理，容易把“socket 泄漏”修成“double destroy”或 “stale queue”。
2. `P12.2` 若 strict parser 只补计数字段，不和已公布的 filter capability 共享同一白名单，可能出现“合法旧客户端被误拒”或“未知 filter 仍被 silent 接受”的新漂移。
3. `P12.3` 若 admission 校验与旧协议兼容门控切得不清楚，可能把当前 loopback/legacy client 的 data breakpoint 能力一次性全部打断；需要 capability、diagnostic 与 reject reason 同步设计。
4. `P13.1` 若为重新算 hash 而重跑完整 preprocessor / compile 路径，可能抵消 fully precompiled 的启动收益；需要把真实性校验限制在最小必要的 source-backed hashing。
5. `P13.2` 若 registry owner 选得过短，仍在生成中的 JIT codegen/fallback 可能拿不到 native form；若 owner 选得过长，又会把当前 process-global 泄漏换个位置继续保留。

#### 已知行为变化

1. client 断线后的表现将从“可能留到后续 timeout/析构再清”收紧为“在 release helper 中立即摘状态并销毁 socket”；依赖旧模糊时序的测试需要改成等待显式 idle/release。
2. `BreakOptions` 的 malformed request 将从“可能 silent no-op 或异常分配”收紧为“显式 reject/diagnostic”；旧测试若默认允许未知 filter 或坏包通过，需要同步更新断言。
3. raw-address data breakpoint 将从“客户端随意填写地址/尺寸”收紧为“只有 session-issued target 才允许进入后端”；依赖旧自由地址模式的调试前端需要读取 capability/reject reason。
4. fully precompiled 激活日志和回执会新增“已做源码校验 / 当前无法做源码校验 / 源码不匹配”三分语义；旧工具若只看 build/guid gate，需要同步吸收新的 freshness 状态。
5. native-form registry 会从进程级隐式缓存收紧为 engine-scoped owned resource；任何依赖“上一轮 engine 还留着旧 registry 条目”的调试/脚本生成假设都需要改成显式重新注册。

---

## 深化 (2026-04-09 06:36:14)

本轮继续只读核对 AutoPlans 输入与当前源码后，确认现有文档仍有 3 个未承接的残余缺口：JIT 停点后的 `Variables/Evaluate` 仍无统一 execution truth；`DebugServer` 仍只有单一全局 paused state，会在并发脚本线程下吞掉后续 stop；watchpoint 仍会在同一 flush 里制造 stop storm，并把大 `HitCount` 压坏成首击触发。

说明：按规则复查 [B] 输入时，`Documents/AutoPlans/DiscoveryPlans/DebuggingAndJIT_Plan.md` 在当前工作树中不存在；以下新增条目只使用 [A]/[C]/[D]/[E] 与源码复核，不伪造 [B] 引用。

### 新增 Phase 14：补齐 JIT 变量观测与并发 stop/watchpoint 合同

- [ ] **P14.1** 让 JIT top frame 的 `Variables/Evaluate` 至少支持 `%this%` / `this.Member`，并把 deeper locals 显式降级为 capability/diagnostic，而不是 silent failure 或错帧回退
  - 当前 `P2.1` 已经承接了 JIT `stop/callstack/source-location`，但变量观测主链仍完全站在解释器 `asCContext` 上：`ResolveDebuggerFrame()` 只扫描 `asGetActiveContext()` 与 Blueprint stack，`GetDebuggerValue()` / `GetDebuggerScope()` 的 `%local%`、`%this%`、`%module%` 分支都直接调用 `Context->GetVarCount()`、`GetThisPointer()`、`GetFunction()`。与此同时，JIT 侧 `FScopeJITDebugCallstack` 只记录 `Filename/FunctionName/ThisObject/LineNumber`，没有 locals 描述表。结果是停点落在 JIT 覆盖函数时，`RequestVariables` / `RequestEvaluate` 只能 silent false、落回解释器帧，或者直接断掉 `ValueAddress -> DataBreakpoint` 这条链。
  - 本项不一次性承诺完整 JIT deep locals materialization；第一阶段先把 “JIT top frame 可观测性” 做成受控能力：新增 execution-state provider 或等价 helper，让 `CallStack`、`GetAngelscriptExecutionFileAndLine()`、`GetDebuggerValue()`、`GetDebuggerScope()` 共用同一条 JIT frame 入口；对 top-frame `%this%`、`this.Member` 和可稳定取址的成员值返回真实 `ValueAddress/ValueSize`；对当前拿不到稳定地址或 locals 表的 deeper frame，返回显式 `unsupported/diagnostic` 与 capability，而不是继续 silent empty 或错用解释器上下文。
  - 这一步要显式避免“看起来有值但其实来自错误 execution state”的假能力。若短期内只能稳定支持 JIT top-frame `this` 与成员访问，就把 `supportsJitDeepVariables=false` 收口成正式合同，并让前端/测试按该合同断言，而不是继续把空结果当成正常退化。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 39 — “`Evaluate` / `Variables` 解析链完全依赖解释器栈帧 API，JIT 覆盖函数里的 locals 与 `this` 无法进入调试协议”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` `Issue-18` / `NewTest-7` — “现有 `StaticJIT` 自动化根本不执行 transpiled code，`RequestEvaluate` / `RequestVariables` 只有解释器 happy-path 缺口，没有任何 JIT top-frame 变量回归”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT7` — “解释器栈与 JIT `Execution.debugCallStack` 需要统一 execution state provider；JIT top frame 至少要返回 `this` 或显式 unsupported”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` — “当前 AS 的变量协议价值在于 `Variables/Evaluate -> ValueAddress + ValueSize -> DataBreakpoint` 是同一条 addressable entity chain，JIT 路径不应脱链”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h` L194-L217 — `FScopeJITDebugCallstack` 仍只有 `Filename/FunctionName/ThisObject/LineNumber`，没有 locals/slot 描述；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L5634-L5644 — 引擎已经能从 `activeExecution->debugCallStack` 读取 JIT 位置，说明 JIT runtime truth 存在；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L2282-L2345 — `ResolveDebuggerFrame()` 仍只从 `asGetActiveContext()` / Blueprint stack 构造 frame；同文件 L2417-L2438、L2692-L2784 — `GetDebuggerValue()` / `GetDebuggerScope()` 仍直接依赖 `asGetActiveContext()`、`GetVarCount()`、`GetThisPointer()` 和 `GetFunction()`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/IAngelscriptExecutionStateProvider.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/InterpreterExecutionStateProvider.*`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/JitExecutionStateProvider.*`
- [ ] **P14.1** 📦 Git 提交：`[Debug/JIT] Feat: expose top-frame jit variables with explicit unsupported deep locals`
- [ ] **P14.1-T** 单元测试：验证 JIT top frame 的 `%this%` / `this.Member` 可观测，deeper locals 则返回显式 unsupported，而不是 silent 空结果或错帧值
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerJITVariableTests.cpp`
  - 测试场景：
    - 正常路径：在真实 transpiled/JIT 执行的成员函数断住后，请求 `0:%this%` 与 `0:this.MemberValue`，能返回正确值，且 adapter v2 下 `ValueAddress/ValueSize` 非零，可继续作为 monitor target。
    - 边界条件：混合 `Interpreter -> JIT` 与 `JIT -> Interpreter` 栈上，请求 JIT top frame 的 `Variables/Evaluate` 时，至少能观测 top-frame `this`；对于当前未实现的 deeper locals，返回显式 `unsupported`/diagnostic，而不是静默空列表。
    - 错误路径：当 `debugCallStack` 缺失或 JIT frame 没有稳定变量元数据时，服务端返回结构化失败，不会回退去解释器 frame 0 读取“看起来像真的”变量值。
  - 测试命名：`Angelscript.TestModule.Debugger.JITVariables.TopFrameThisWithExplicitUnsupportedLocals`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P14.1-T** 📦 Git 提交：`[Test/JIT] Test: add top-frame jit variable coverage`

- [ ] **P14.2** 把 single global paused state 升级成 thread-scoped stop ledger + pending stop queue，避免一个线程停住时吞掉其他线程的 `breakpoint/exception/debugbreak`
  - 当前 `P1.4` / `P9.1` 已经处理了单线程 stray step 与 pending-stop 被过早清空的问题，但 runtime 仍然只有一个全局 `bIsPaused`。`FStoppedMessage` 也只有 `Reason/Description/Text`，完全没有 `ThreadId` 或 `StopId`。所以只要某个线程已经进入 `PauseExecution()`，其他线程之后触发的 `ProcessException()`、`ProcessScriptLine()`，甚至 `TryBreakpointAngelscriptDebugging()` 都会因为“已经 paused”直接 early-return。用户看到的是第一个 stop 之外的 breakpoint/exception 被静默吞掉，而不是排队或显式拒绝。
  - 本项把 stop owner 从 `bool bIsPaused` 提升成最小可验证的线程级合同：`HasStopped` 至少要携带 `ThreadId` 与 `StopId`；server/pause controller 维护一个 `ActiveStop` 和 `PendingStops` 队列；非 owner thread 的 `breakpoint/exception/debugbreak` 进入 pending queue，而不是被 `if (bIsPaused) return` 丢弃；`Continue/Step*` 必须显式作用于当前 `StopId`，过期或不存在的 stop 请求返回 reject/diagnostic。
  - 第一阶段不要求一次对齐完整 DAP thread model，但至少要保证“不会再 silent lose stop”。如果短期仍保持单 active stop，也必须把其余线程的 stop 保存为 deterministic queue，并在当前 stop `Continue` 后按稳定顺序兑现，而不是继续把“谁先停住”变成其余线程的协议黑洞。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 70 — “`DebugServer` 只有一个全局暂停态，停在一个线程时会静默吞掉其他线程的 `breakpoint/exception`”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT14` — “`PauseExecution()` 依赖 GameThread 内嵌 socket pump，需要升级成 `PauseController + CommandQueue`，并明确 pause/thread ownership”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT15` — “调试对象模型与暂停快照需要稳定 frame/stop identity，避免 live context 重新解析”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L129-L139 — `FStoppedMessage` 仍只有 `Reason/Description/Text`，没有任何 thread/stop identity；同文件 L597-L614 — server 仍只有单一 `bIsPaused` 与 `PauseExecution()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L440-L445 — `ProcessException()` 在 paused 态直接 `return`；同文件 L471-L473 — `ProcessScriptLine()` 在 paused 态直接 `return`；同文件 L667-L699 — `PauseExecution()` 仍围绕全局 `bIsPaused` 阻塞并补发 `HasContinued`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L5718-L5744 — `TryBreakpointAngelscriptDebugging()` 在 `bIsPaused` 为真时直接拒绝新的脚本 stop。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/AngelscriptPauseController.*`
- [ ] **P14.2** 📦 Git 提交：`[Debug/Pause] Refactor: queue concurrent stops by thread ownership`
- [ ] **P14.2-T** 单元测试：验证并发脚本线程的 stop 不再 silent lost，`Continue/Step*` 只作用于当前 active stop
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerConcurrentStopTests.cpp`
  - 测试场景：
    - 正常路径：thread A 命中 line breakpoint 进入 paused 后，thread B 再触发 `exception` 或 `DebugBreak`，服务端不会吞掉第二个 stop，而是保留为 pending 并在 A `Continue` 后稳定兑现。
    - 边界条件：连续 2-3 个不同线程 stop 混入同一会话时，`ThreadId/StopId` 顺序稳定、可断言；`RequestCallStack` / `Variables` 只读取当前 active stop，不会串到 pending stop。
    - 错误路径：客户端对过期 `StopId` 或非 owner thread 发送 `Continue/StepOut` 时收到 explicit reject/diagnostic，不会悄悄把 pending stop 清掉或伪造 `HasContinued`。
  - 测试命名：`Angelscript.TestModule.Debugger.ConcurrentStops.ThreadScopedOwnershipAndQueue`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P14.2-T** 📦 Git 提交：`[Test/Debug] Test: add concurrent stop ownership coverage`

- [ ] **P14.3** 将 data breakpoint 的同轮 flush 合并为单次 stopped event，并把 `HitCount` 升级成 validated `int32` 合同，堵住 stop storm 与 `>127` 回绕
  - 当前 `P3.1` 已经承接了 watchpoint backend 清理与 verified/reject，`P3.3` 也承接了 stop payload 的 breakpoint identity，但 watchpoint 命中批次本身仍不正确：`ProcessScriptLine()` 会把 `TriggeredBreakpoints` 逐项 `PauseExecution()`，于是一次写操作若命中多个 watchpoint，就会在同一脚本行上制造多次 `HasStopped -> Continue -> HasContinued` 循环；同时协议结构 `FAngelscriptDataBreakpoint.HitCount` 仍是 `int8`，`CopyFrom()` / `CopyTo()` 继续在 8 位和 32 位之间来回收缩，`HitCount > 127` 会直接回绕成首击触发。
  - 本项把 “一次 flush 的 watchpoint 结果” 收口成单个调试语义：先聚合 `TriggeredBreakpointIds`、`OutOfScopeIds`、`ClearIds` 与统一文本，再只发一次 stopped event 和一次 clear flow；同时把 `HitCount` 升级为 validated `int32`，由 parser/capability 显式决定当前前端是否支持更宽计数。对旧前端或 legacy body，若请求超出兼容范围，必须返回 explicit reject/diagnostic，而不是 silent wrap 成负数再在第一次命中时触发。
  - 若 `P3.3` 已先落地，则本项直接复用 `BreakpointIds/StopSource`；若 `P3.3` 尚未落地，也至少先保证一次 flush 只会产生一次 `PauseExecution()`。重点不是多加一个字段，而是让 “watchpoint 命中/清理/继续执行” 重新变成单次、可预测、可回归的协议合同。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 51 — “同一轮 data breakpoint flush 会为每个命中项单独进入一次 `PauseExecution`，客户端必须连续继续多次才能前进”
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 52 — “`HitCount` 在协议层被压成 `int8`，超过 127 会回绕成首击触发”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` `NewTest-10` — “当前自动化只覆盖 `HitCount=1` 的单 watchpoint happy path，没有 burst hit 或高 hit-count 边界回归”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT31` — “watchpoint 能力应当是调试语义，不应继续让实现细节如 raw address/本机后端决定协议行为”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` — “`Variables/Evaluate -> ValueAddress + ValueSize -> DataBreakpoint` 本应是一条稳定链路，watchpoint 触发批次也应保持确定性”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L300-L323 — `FAngelscriptDataBreakpoint.HitCount` 仍是 `int8` 并按 `int8` 直接序列化；同文件 L373-L391 — active mirror 仍把 `HitCount` 从 `int8` 提升后又收回 `int8`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L308-L323 — handler 仍按提升后的旧 8 位值递减/触发；同文件 L533-L539 — `TriggeredBreakpoints` 仍逐项 `PauseExecution()`，一次 flush 会制造多次 stop/continue 循环。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointBurstTests.cpp`
- [ ] **P14.3** 📦 Git 提交：`[Debug/DataBreakpoint] Fix: coalesce burst stops and widen hit-count contract`
- [ ] **P14.3-T** 单元测试：验证一次 watchpoint flush 只产生一次 stop，且大 `HitCount` 不再回绕成首击触发
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointBurstTests.cpp`
  - 测试场景：
    - 正常路径：同一条写操作同时命中两个 watchpoint 时，只收到 1 个 stopped event 和 1 个 `ClearDataBreakpoints`，其中 clear payload 包含两条 `Id`；客户端只需 `Continue` 一次即可前进。
    - 边界条件：`HitCount = 128` 或更大的合法值时，watchpoint 只会在第 N 次写入真正停下，不再在第一次命中时立即触发。
    - 错误路径：legacy/不支持 wide hit-count 的客户端发送超范围 `HitCount` 时，服务端返回 explicit reject/diagnostic，不会 silent wrap 成负数，更不会制造 stop storm。
  - 测试命名：`Angelscript.TestModule.Debugger.DataBreakpoint.BurstCoalescingAndWideHitCount`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P14.3-T** 📦 Git 提交：`[Test/Debug] Test: add burst-stop and wide-hitcount data-breakpoint coverage`

### 单元测试总览补充（本轮深化 2026-04-09 06:36:14）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P14.1` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerJITVariableTests.cpp` | JIT top-frame `%this%`/`this.Member`、显式 unsupported locals、JIT 值地址可继续监视 | P1 |
| `P14.2` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerConcurrentStopTests.cpp` | thread-scoped stop ownership、pending stop queue、过期 `StopId` reject | P0 |
| `P14.3` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointBurstTests.cpp` | burst stop coalescing、wide `HitCount`、legacy reject/diagnostic | P1 |

### 验收标准补充（本轮深化 2026-04-09 06:36:14）

1. JIT top-frame 的 `RequestVariables` / `RequestEvaluate` 要么返回真实 `this` / `this.Member` 与可监视地址，要么返回显式 unsupported/diagnostic；不能再 silent empty，也不能回退到错误的解释器 frame。
2. 任一脚本线程进入 paused 后，其他线程后续触发的 `breakpoint/exception/debugbreak` 不会再被静默丢弃；至少会以 `ThreadId + StopId` 的 pending/queued stop 形式保留下来，并在前端可观测的时序中兑现。
3. 同一次 data breakpoint flush 最多只产生一轮 `HasStopped -> Continue -> HasContinued`；`HitCount > 127` 不再回绕成首击触发，legacy 不支持场景会返回显式 reject/diagnostic。

### 风险与注意事项补充（本轮深化 2026-04-09 06:36:14）

#### 风险

1. `P14.1` 若 JIT variable provider 在缺少稳定元数据时偷偷回落到解释器 `asCContext`，会制造“结构合法但值来自错误执行态”的高欺骗性回归。
2. `P14.2` 若 pending stop queue、pause controller 与现有 `Continue/Step*` 兼容层切分不清，容易把“silent lost stop”修成“重复 stop”或新的 paused-state 死锁。
3. `P14.3` 若 widening `HitCount` 只改服务端内部整数宽度，不同步做 capability/version/reject gate，旧前端可能在 wire 级别反序列化错位，或继续按旧语义误以为设置成功。

#### 已知行为变化

1. JIT 顶帧变量从“静默拿不到”收紧为“top-frame `this` 可观测，deeper locals 明确 unsupported/diagnostic”；旧前端不能再把空结果误当成正常退化。
2. paused state 将从“全局单布尔”收紧为“带 `ThreadId/StopId` 的 active stop + pending stop queue”；依赖旧全局继续/单步假设的客户端需要显式跟随 stop identity。
3. data breakpoint 的多命中同轮 flush 将从“需要连点多次 continue”收紧为“单次聚合 stop”；超大 `HitCount` 也会从 silent wrap 改成宽计数或显式 reject。

---

## 深化 (2026-04-09 06:48:01)

本轮继续只读核对 AutoPlans 输入、`Documents/Plans/` 现有活跃 Plan 与当前源码后，确认还有一组未被现有条目承接的 residual gap：`Plan_DebugAdapter.md` 已覆盖 VS Code / DAP 侧命令映射，但 runtime/server 侧的 workspace 副作用消息仍停留在“必须有 active debug client”“直接弹本地 modal”“没有任何结果回执”的旧合同。本轮只补服务端语义，不重复客户端适配层建设。

### 新增 Phase 15：收口 workspace 写回与 authoring 命令的服务端合同

- [ ] **P15.1** 把 `ReplaceAssetDefinition` / literal asset save 从“依赖 active debug client + 空 `DebugServer` 崩溃”收口成 null-safe、client-optional 的 workspace write contract
  - 现有 `P7.3` 只收口了 `AssetDatabase` 的 delegate 生命周期，明确没有处理 asset literal 写回；但源码里这条路径已经是直接 correctness 缺陷，而不是未来架构优化。当前 editor 保存字面量资产前先硬检查 `HasAnyDebugServerClients()`，没有调试客户端就弹窗返回；另一方面，runtime 真正写回时又默认 `DebugServer` 一定存在。只要运行态落到 fully precompiled / non-development 模式，这个前提就会失效，保存脚本字面量资产会直接走进空指针解引用。
  - 本项先做最小但严格的 server/runtime 合同收口：`ReplaceScriptAssetContent(...)` 必须先判断是否存在可用的 workspace sink/debug bridge，再决定“立即广播旧 alias”“记录本地 pending operation”或“返回 explicit unsupported/pending”；`OnLiteralAssetSaved()` 不能再把“没有 active debug client”当成唯一失败条件，更不能用 modal dialog 代替正式错误回执。这样 fully precompiled 与 headless/editor-without-client 场景至少会安全退化，而不是 crash 或强耦合到某个 IDE 扩展是否在线。
  - 这一步不直接展开 `Arch-DT28` 的完整 operation log / offline replay，也不要求一次拆出完整 workspace service；目标只是先把“保存 literal asset 不崩、不强绑 active debugger client、可给出正式结果”变成稳定底线，再给后续更大的 workspace 编排收敛留干净接口。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 65 — “fully precompiled 模式会让 `ReplaceAssetDefinition` 走进空 `DebugServer` 解引用，保存脚本字面量资源可直接崩溃”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT28` / `Arch-DT29` — “workspace 写回仍把 IDE extension 当作唯一落盘者，且 remote attach 与 workspace 写操作默认共享同一权限面”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` / `[D7]` — “调试 attach 不应反向约束 editor authoring；`CreateBlueprint`/asset authoring 已是正式工作流，不能继续靠 active debugger client 和本地 UI 副作用兜底”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L121-L128 — `OnLiteralAssetSaved()` 在没有任何 debug client 时仍直接弹 `Visual Studio Code extension must be running...` 并返回；同文件 L331 — literal 资产保存最终仍无条件调用 `ReplaceScriptAssetContent()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` L1453-L1455 — `DebugServer` 仍只在 `(!bUsePrecompiledData || bScriptDevelopmentMode)` 条件下创建；同文件 L2028-L2035 — `ReplaceScriptAssetContent()` 仍无空指针检查地直接 `DebugServer->SendMessageToAll(...)`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Workspace/IAngelscriptWorkspaceWriteSink.h`
- [ ] **P15.1** 📦 Git 提交：`[Debug/Workspace] Fix: make literal asset writeback null-safe and client-optional`
- [ ] **P15.1-T** 单元测试：验证 literal asset save 在无 debug client / 无 `DebugServer` / fully precompiled 场景下不再 crash，并返回正式结果
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptLiteralAssetWritebackTests.cpp`
  - 测试场景：
    - 正常路径：存在 workspace sink 或 legacy debug bridge 时，保存 literal curve 只产生 1 次写回结果与 1 次可选 alias 广播，不会重复发送，也不会阻塞正常保存流程。
    - 边界条件：fully precompiled 或 `DebugServer == nullptr` 的 editor/runtime 组合下，保存 literal curve 仍返回 explicit pending/local-only 结果，不会空指针崩溃，也不会再要求 active debug client 在线。
    - 错误路径：workspace sink 不可用或应用失败时，只返回结构化 error/diagnostic，不会再用 `FMessageDialog` 把“没有 VS Code 扩展/没有 client”当成协议错误通道。
  - 测试命名：`Angelscript.Editor.Workspace.LiteralAssetSaveIsNullSafeWithoutDebugServer`
  - 隔离方式：`FAngelscriptEngineScope` + temporary literal asset package
- [ ] **P15.1-T** 📦 Git 提交：`[Test/Workspace] Test: add literal asset writeback safety coverage`

- [ ] **P15.2** 把 `FindAssets` / `CreateBlueprint` 从 fire-and-forget 本地 UI 副作用改成 capability-gated 的 request/response contract
  - 现有 `P1.5` 已为 `GoToDefinition` / `Evaluate` / `CallStack` 规划了 `RequestId + Status` 元层，`P1.7` 也会收口 session admission；但 `FindAssets` / `CreateBlueprint` 仍完全绕过这套合同。服务端收到消息后只是广播 editor delegate，`FindAssets` 甚至直接把客户端传来的 `AssetList.Assets` 原样交给本地 popup helper，`CreateBlueprint` 在无效 class 情况下还会直接弹 modal dialog。整个流程既没有 success/failure 回包，也没有 capability/unsupported 语义，更没有对 headless / non-editor / remote 连接的结果边界。
  - 本项不重复 `Plan_DebugAdapter.md` 的客户端命令接线，而是只修 server/runtime 语义：在现有消息号兼容前提下，为 `FindAssets` / `CreateBlueprint` 增加显式 result payload 或等价 `WorkspaceCommandResult`，至少包含 `RequestId`、`Status`、`Capability`、`ErrorText` 以及 `CreateBlueprint` 的输出 object path / `FindAssets` 的 resolved asset list。对于没有 workspace service、没有 editor 支持或输入非法的情况，必须返回 explicit unsupported/error，而不是 silent no-op、广播本地 UI、或用 modal dialog 代替网络回执。
  - 这一步不强行把所有 workspace 行为一次迁成完整独立 service，也不重命名现有 `FindAssets` 消息号；重点是先让“命令有没有执行、为什么失败、客户端该怎么收敛 UI”从隐式副作用变成正式协议。后续若要继续做 `Arch-DT3` 的逻辑域拆分，可以直接在同一 result/capability 面上演进，而不用再兼容 silent side effect。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 41 — “`FindAssets` 请求当前是源码级 no-op，delegate 签名按值传递结果且服务端从不回包”
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 42 — “`CreateBlueprint` 扩展消息没有任何成功/失败回执，非法输入会在目标进程弹出模态对话框”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` `NewTest-16` — “需要为 `FindAssets` 补一条真实脚本类名解析 + 广播/结果场景测试”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT3` / `Arch-DT29` — “workspace/editor 服务与调试协议面混在一起，读写 capability 没有独立权限与结果合同”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` / `[D7]` — “当前自定义协议已被当作正式 contract 维护，而 `CreateBlueprint popup` 已是正式 authoring workflow，不能继续只靠本地 UI 副作用表达结果”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1161-L1170 — `FindAssets` 仍只是反序列化 `FAngelscriptFindAssets` 后直接 `Broadcast(AssetList.Assets, BaseClass)`，没有任何响应消息；同文件 L1172-L1189 — `CreateBlueprint` 仍只会广播 editor delegate，非法类名直接 `FMessageDialog::Open(...)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h` L19-L21 — `FAngelscriptDebugListAssets` / `FAngelscriptEditorCreateBlueprint` 仍是无返回值 delegate，天然没有 result contract；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` L396-L408 — editor 侧目前也只是把这两条 delegate 直接绑定到 popup helper，而不是绑定到可回执的 workspace service。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Workspace/IAngelscriptWorkspaceCommandService.h`
- [ ] **P15.2** 📦 Git 提交：`[Debug/Workspace] Refactor: return explicit results for find-assets and create-blueprint commands`
- [ ] **P15.2-T** 单元测试：验证 `FindAssets` / `CreateBlueprint` 在 success / unsupported / invalid-input 下都返回正式结果，而不是 silent side effect 或 modal
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerWorkspaceCommandTests.cpp`
  - 测试场景：
    - 正常路径：绑定临时 workspace command service 后，`FindAssets` 返回解析后的 asset list，`CreateBlueprint` 返回创建成功的 asset/object path，且每个 `RequestId` 都有对应 success response。
    - 边界条件：service 未绑定、non-editor/headless 或 capability 被关闭时，客户端收到 explicit unsupported/diagnostic，连接不断开，后续常规 debug 消息仍可工作。
    - 错误路径：无效 `ClassName`、空 query 或重复请求不会再弹 modal dialog，也不会 silent no-op；服务端返回结构化 error，并可断言 editor popup helper 没有被调用。
  - 测试命名：`Angelscript.TestModule.Debugger.Workspace.FindAssetsReturnsExplicitResults`；`Angelscript.TestModule.Debugger.Workspace.CreateBlueprintReturnsStatusInsteadOfModal`
  - 隔离方式：`FAngelscriptEngineScope` + temporary workspace service binding
- [ ] **P15.2-T** 📦 Git 提交：`[Test/Debug] Test: add workspace command result-contract coverage`

### 单元测试总览补充（本轮深化 2026-04-09 06:48:01）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P15.1` | `Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptLiteralAssetWritebackTests.cpp` | literal asset save null-safe、无 client/无 `DebugServer` 安全退化、正式结果回执 | P0 |
| `P15.2` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerWorkspaceCommandTests.cpp` | `FindAssets`/`CreateBlueprint` success、unsupported、invalid-input 结果合同 | P1 |

### 验收标准补充（本轮深化 2026-04-09 06:48:01）

1. 保存 script literal asset 时，不再要求 active debug client 作为唯一前置条件；fully precompiled 或 `DebugServer == nullptr` 场景下也不会 crash，并能返回明确的 local/pending/error 结果。
2. `FindAssets` / `CreateBlueprint` 不再是 silent side effect 或 modal-only 错误通道；每个请求都必须返回 success / unsupported / error 之一，并能按 `RequestId` 归属到正确客户端。
3. 非 editor / headless / capability 关闭场景下，workspace 命令会被正式拒绝但不会打断普通调试会话；旧的 debug-only 能力仍保持可用。

### 风险与注意事项补充（本轮深化 2026-04-09 06:48:01）

#### 风险

1. `P15.1` 若 local write、legacy alias 广播与未来 operation log 的先后顺序切分不清，容易把“空指针 crash”修成“重复写回”或“保存成功但外部工作区未同步”的新漂移。
2. `P15.2` 若 result payload 只补成功分支，不同步补 unsupported/error 和 capability gate，旧前端仍可能把 silent side effect 当默认行为，导致 server 已修但客户端 UI 继续悬空。

#### 已知行为变化

1. 保存 literal asset 时，“必须有 VS Code 扩展在线”会从硬阻塞 modal 收紧为正式 capability/result 合同；无 client 场景下会变成安全退化或 pending，而不是直接拒绝保存。
2. `CreateBlueprint` / `FindAssets` 的失败路径将从“本地 popup / modal / silent no-op”收紧为结构化 response；依赖旧副作用通道的客户端需要改成读取结果消息而不是等待本地 UI。

---

## 深化 (2026-04-09 06:58:48)

本轮继续只读核对 AutoPlans 输入、`Documents/Plans/` 活跃 Plan 与当前源码后，确认还有两组 residual gap 尚未被现有 `P1-P15` 显式承接：其一，`P7.1/P7.2` 已收口 `DebugDatabase` / `AssetDatabase` 的版本字段与重复订阅，但 stream 本身仍没有 authoritative `Revision` / resume / backpressure 合同；其二，`P8-P13` 已处理 `StaticJIT` correctness、lifecycle 与 source identity，但生成文件名和 unity shard 仍由 `TMap` 遍历顺序与 encounter-order 命名驱动，增量编译、diff 与外部诊断都缺稳定 join key。本轮不重复 `Documents/Plans/Plan_DebugAdapter.md` 的客户端缓存/桥接工作，也不重复 `Documents/Plans/Plan_StaticJITUnitTests.md` 的低层单元测试扩面。

### 新增 Phase 16：补齐调试数据库流合同与 StaticJIT 产物确定性

- [ ] **P16.1** 将 `DebugDatabase` / `AssetDatabase` 从“一次请求就重放完整快照”升级为带 `Revision` / `StreamId` 的 authoritative stream contract
  - 现有 `P7.1/P7.2` 只修了 schema 版本与重复订阅，却还没有回答“客户端拿到的到底是哪一版数据库、这是 full snapshot 还是 asset delta、重连后能否判断自己是否过期”。当前 server 仍在 `RequestDebugDatabase` 时立刻重发完整 `DebugDatabase`，`BroadcastDebugDatabase()` 与 `OnFilesLoaded()` 也会再次向所有订阅者推送全量或增量消息，但消息体和结束标记都没有任何 `Revision` / `StreamId` / `ChunkIndex` 元数据。对慢链路、代理转发和重连场景来说，这意味着前端永远只能把“最后收到的一串消息”猜成最新事实，而不能正式判断是否落后、是否混入旧流，或是否应该请求 resync。
  - 本项不扩成 `Arch-DT5` 的完整 symbol-graph / delta-sync 重构，也不重复 `Plan_DebugAdapter.md` 的客户端缓存策略；目标是先把 server 侧流语义收口成正式合同：为 `RequestDebugDatabase`、`DebugDatabaseSettings`、`DebugDatabaseFinished`、`AssetDatabaseInit`、`AssetDatabase`、`AssetDatabaseFinished` 引入最小可回归的 `Revision` / `StreamId` / `bIsFullSnapshot` / chunk metadata，新增 `FDebugDatabaseStreamState` 或等价 owner 统一维护当前快照 revision、缓存的 serialized full snapshot 与 per-client last-seen revision。重复请求同一 revision 时不再盲目全量重发；asset 变更改为显式 bump `AssetDatabaseRevision` 并携带 delta revision；slow client 的 full snapshot 发送则必须与 `P1.2` 的 send-progress/backpressure 语义对齐，不能再把数据库流和 transport 超时拆成两套互不感知的合同。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 58 / 63 — “同一客户端重复 `RequestDebugDatabase` 会放大全量/增量流；慢客户端在 database/asset 同步时容易被误判成断线”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` `NewTest-14` — “当前只验证 `RequestDebugDatabase` 的消息序列起点，没有任何 snapshot/delta revision 边界回归”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT5` — “`DebugDatabase` / `AssetDatabase` 采用 game-thread 全量快照推送，缺少 revision 协商与异步背压”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` — “当前调试数据库已经是正式协议 contract，不能继续让 stream 边界只靠顺序猜测”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L507-L517、L536-L553 — `FAngelscriptAssetDatabase` 与 `FAngelscriptDebugDatabaseSettings` 仍只有 payload/version，没有任何 revision/stream 字段；同文件 L585-L588 — `FAngelscriptDebugServer` 仍只保存原始订阅 socket 数组，没有 per-client stream state；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L822-L826 — `RequestDebugDatabase` 仍一律 `Add(Client)` 后立即全量 `SendDebugDatabase(Client)`；同文件 L1485-L1505 — `BroadcastDebugDatabase()` / `SendDebugDatabase()` 仍不带 revision 元数据；同文件 L2140-L2149、L2159-L2203 — `OnFilesLoaded()` 与 `SendAssetDatabase()` 仍只发送 raw asset list 和 `FEmptyMessage` 结束标记，没有 snapshot/delta boundary。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugDatabaseStreamState.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugDatabaseStreamState.cpp`
- [ ] **P16.1** 📦 Git 提交：`[Debug/Database] Refactor: add revisioned debug-database stream contract`
- [ ] **P16.1-T** 单元测试：验证 `DebugDatabase` / `AssetDatabase` 的 full snapshot、delta 与 resync 都带稳定 revision 边界
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugDatabaseRevisionTests.cpp`
  - 测试场景：
    - 正常路径：首次 `RequestDebugDatabase` 返回 1 组带同一 `StreamId + Revision` 的 `DebugDatabaseSettings -> DebugDatabase* -> DebugDatabaseFinished -> AssetDatabaseInit -> AssetDatabase* -> AssetDatabaseFinished` 序列；后续单次 asset 变更只发送带新 revision 的 delta，不重放旧 full snapshot。
    - 边界条件：同一 client 以当前 revision 重发请求时，server 返回 explicit up-to-date / no-resync 结果或等价轻量响应，而不是再推整套 full snapshot；旧 revision 重连时则会明确触发完整 resync。
    - 错误路径：客户端拿未知/过期 revision、mid-stream 断线后重连或 chunk 序号不连续时，server 返回 deterministic resync-needed/stream-reset 结果，不会把旧 snapshot 与新 delta 混成一条未标记的消息流。
  - 测试命名：`Angelscript.TestModule.Debugger.Database.RevisionedSnapshotAndDeltaContract`
  - 隔离方式：`FAngelscriptEngineScope` + temporary asset registry fixture
- [ ] **P16.1-T** 📦 Git 提交：`[Test/Debug] Test: add revisioned database-stream coverage`

- [ ] **P16.2** 将 `StaticJIT` 输出从 encounter-order 命名升级为 deterministic planner + manifest contract
  - 现有 `P8-P13` 已经修了 `native form`、fallback、source filename、自校验等 correctness 问题，但产物 identity 仍然是“遍历时顺手决定”的：模块第一次被看到就立刻塞进 `JITFiles`，`GetUniqueModuleName()` 用 `UsedUniqueNames` 和 `__%d` 后缀按遇到顺序消冲突，`WriteOutputCode()` 再直接按 `JITFiles` / `SharedHeaders` 当前遍历顺序写 `.as.jit.hpp`、shared header 与 `AngelscriptJitCode_N.jit.cpp`。这样即使脚本语义没变，只要 module 首次出现顺序、同名模块碰撞顺序或单文件行数变化稍有不同，后续 unity shard 和文件名就会整体漂移，放大编译缓存失效、diff 噪音和外部工具追踪成本。
  - 本项不扩成 `Arch-DT53` 的完整 toolchain receipt，也不重复 `P11.2/P13.1` 的 precompiled self-gate；目标是先把 `StaticJIT` 写盘前的 identity 固定下来：新增 `FJITOutputPlan` / `FJITManifestEntry` 或等价 planner，把 `JITFiles`、`SharedHeaders` 和 unity shard 在写盘前投影成按稳定 key 排序的数组；把 `GetUniqueModuleName()` 的 encounter-order `__%d` 后缀改成 deterministic slug（例如 sanitized module identity + short stable hash）；输出 `AngelscriptJitManifest.json` 记录 `module -> header -> unity shard -> content hash -> PrecompiledDataGuid` 映射，后续 clean-up、增量编译诊断和外部比较都以 manifest 为 join key，而不是继续猜 `TMap` 当前遍历顺序。
  - 来源：
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` `Issue-18` / `NewTest-3` / `NewTest-5` — “`AngelscriptStaticJIT.cpp` 的真实 generated output 与 execution path 几乎没有 direct coverage，现有回归不足以锁住产物 identity”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT45` — “`StaticJIT` 的 module/header/unity shard 仍依赖 `TMap` 遍历与 encounter-order 命名，生成结果缺少 deterministic build contract”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D8]` — “`StaticJIT` 继续扩展时应把配置项与 generated artifact 显式暴露出来，而不是让外部消费方跟随内部遍历副作用漂移”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h` L406-L407、L461-L472 — `JITFiles`、`UsedUniqueNames` 与 `SharedHeaders` 仍是 process-local 容器，没有独立 planner/manifest 层；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` L118-L137 — `GetFile()` 仍按模块首次出现顺序 `JITFiles.Add(Module, File)`；同文件 L226-L247 — `GetUniqueModuleName()` 仍通过 `__%d` 遇撞加后缀；同文件 L3515-L3523、L3526-L3579、L3582-L3677 — shared header 写盘、module header 写盘与 unity shard 切分都直接依赖 `JITFiles` / `SharedHeaders` 的遍历顺序和 `LinesInCombinedFile` 累加顺序。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptJitOutputPlanner.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptJitOutputPlanner.cpp`
- [ ] **P16.2** 📦 Git 提交：`[StaticJIT/Output] Refactor: make generated artifact naming and shard planning deterministic`
- [ ] **P16.2-T** 单元测试：验证 module/header/unity shard 规划与 `AngelscriptJitManifest.json` 在输入顺序变化下仍保持稳定
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITDeterministicOutputTests.cpp`
  - 测试场景：
    - 正常路径：同一组脚本输入连续生成两次，manifest、`.as.jit.hpp` 文件名和 `AngelscriptJitCode_N.jit.cpp` 列表 byte-identical。
    - 边界条件：人为打乱 module 创建顺序，或构造 sanitize 后同名的两个 module 时，deterministic slug、shared header 文件名与 unity shard 归属仍保持不变。
    - 错误路径：仅修改单个 module 内容时，只影响该 module 的 header、manifest entry 和其所属 unity shard；未改动模块不会因为 encounter-order 漂移而跟着改名或换 shard。
  - 测试命名：`Angelscript.CppTests.StaticJIT.Output.DeterministicManifestAndShardPlan`
  - 隔离方式：`FAngelscriptEngineScope` + temporary generated-output directory
- [ ] **P16.2-T** 📦 Git 提交：`[Test/StaticJIT] Test: add deterministic output-plan coverage`

### 单元测试总览补充（本轮深化 2026-04-09 06:58:48）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P16.1` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugDatabaseRevisionTests.cpp` | full snapshot/delta revision、resync-needed、stream reset | P1 |
| `P16.2` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITDeterministicOutputTests.cpp` | manifest 稳定性、module 顺序扰动、单模块增量不扩散 | P1 |

### 验收标准补充（本轮深化 2026-04-09 06:58:48）

1. `DebugDatabase` / `AssetDatabase` 的每一轮 full snapshot 和 delta 都能由 `StreamId + Revision` 唯一标识；客户端重连、重复请求和慢链路场景都不再只能靠消息顺序猜测“当前拿到的是哪一版数据库”。
2. `StaticJIT` 对同一输入的生成结果具备 deterministic build contract：连续两次生成或 module 注册顺序变化时，manifest、header 名称与 unity shard 归属保持稳定。
3. 修改单个脚本模块后，不相关 module 的 `.as.jit.hpp` 名称和 unity shard 不会因为遍历顺序副作用发生连锁改名；外部工具可用 `AngelscriptJitManifest.json` 稳定追踪产物归属。

### 风险与注意事项补充（本轮深化 2026-04-09 06:58:48）

#### 风险

1. `P16.1` 若只补 `Revision` 字段而不让 full snapshot、delta、finish marker 共用同一 stream owner，容易把“没有 revision”修成“有 revision 但边界仍不一致”的半合同。
2. `P16.2` 若 deterministic planner 的 key 只取 sanitize 后短名、不纳入完整 module identity，可能把当前的 encounter-order 漂移修成新的 hash collision 或错误合并。

#### 已知行为变化

1. `RequestDebugDatabase` 将从“重发一次就再来一遍完整数据库流”收紧为“按 revision 判断是 up-to-date、delta 还是需要 resync”；旧客户端若把重复请求当作隐式 refresh，需要改成显式读取 revision 结果。
2. `StaticJIT` 生成文件名与 unity shard 在首轮切到 deterministic planner 后，可能出现一次性、可预期的产物重排；之后的增量构建和 diff 噪音会显著收敛，外部比较工具也应切到 manifest 作为权威索引。

---

## 深化 (2026-04-09 07:09:54)

本轮继续只读复核 AutoPlans 输入、`Documents/Plans/` 活跃 Plan 与当前源码后，确认还有两组结构性残余缺口尚未被现有 `P1-P16` 明确承接：其一，`P1.2` / `P10.1` 已修 framing、backpressure 与 accepted-socket 初始化，但 `DebugServer` 仍把 raw socket 收发直接跑在 `PauseExecution()` / `ProcessException()` 的执行线程里；其二，`P7.1` / `P16.1` 已补 database version/revision/stream 边界，但 `DebugDatabase` payload 仍停留在 legacy mixed JSON tree，没有 typed symbol contract。另核对当前工作区时发现用户给定的 [B] 输入文件 `Documents/AutoPlans/DiscoveryPlans/DebuggingAndJIT_Plan.md` 不存在，因此本轮新增条目仅使用 [A]/[C]/[D]/[E] 交叉确认，不据此扩写任何无源码支撑的缺口。

### 新增 Phase 17：解耦 DebugServer transport IO lane 与执行停点线程

- [ ] **P17.1** 将 `DebugServer` 的 socket pump 从 `PauseExecution()` / `ProcessException()` 执行线程中剥离，改成 transport-owned queue + runtime-owned command sink
  - 现有 `P1.2` 与 `P10.1` 已经补上 partial read、invalid length、backpressure timeout 与 accepted socket 初始化，但这些仍是症状层修补：当前 runtime 线程在 `ProcessException()` 里会先 `ProcessMessages()`，进入 `PauseExecution()` 后又在 `while (bIsPaused)` 内持续 `ProcessMessages()`，`SleepForCommunicate()` 也复用同一路径。结果是只要远端 client 慢、TCP 分片多、或 database/asset 流正在回放，脚本执行线程就继续承担 socket 收发与协议解码，transport 抖动会直接反压 stop/pause 语义。
  - 本项不重复 `Plan_DebugAdapter.md` 的 DAP bridge，也不扩成 `Arch-DT50/51` 的完整 public-surface 模块拆分；目标是先把 lane 边界固定下来：transport pump 只负责 `Recv()/Send()`、framing、连接生命周期与将 envelope 投递进线程安全队列；`PauseExecution()`、`ProcessException()`、`SleepForCommunicate()` 不再直接做 raw socket IO，只消费已经解析好的命令或等待 transport event。若 `P14.2` 的 `PauseController` 已落地，则直接复用其 command queue；若尚未落地，也必须先抽出最小 `IDebugCommandSink`/`FDebugCommandQueue` seam，禁止再让 pause loop 自己轮询 socket。
  - 这样可以把“慢客户端”“坏包”“高 RTT”从 runtime correctness 中隔离出来：transport 失败只影响对应 client/session，不再把当前脚本线程拖进同步 `Recv()`/`Send()`；同时也让后续 `P10.2` transcript harness 可以在无真实 socket 的前提下冻结 stop/continue contract，而不是继续依赖 inline network side effect。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 19 / 54 / 63 / 72 — “收包循环对 `Recv()` 失败与 partial read 不稳；live path 绕过 parser；10 秒首包年龄误判断线；partial read 会覆写 buffer 开头”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` `Issue-4` / `Issue-30` / `Issue-32` — “connect timeout、disconnect liveness 与 handshake 成功条件仍被弱化成模糊超时或宽松 envelope 判断”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT46` — “`DebugServer` 的 transport 仍在脚本执行线程内阻塞收包，远程调试延迟会直接反压运行时”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` — “当前仓库既然自己拥有调试 contract，就不应继续让 transport 阻塞细节泄漏进 runtime stop 语义”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L395-L399 — `HandleConnectionAccepted()` 仍只把 raw `ClientSocket` 入 `PendingClients`，没有独立 transport owner；同文件 L435-L448 — `Tick()` 与 `ProcessException()` 仍直接同步 `ProcessMessages()`；同文件 L667-L699 — `PauseExecution()` 在 paused loop 内持续 `ProcessMessages()` 并在同一执行线程补发 `HasContinued`；同文件 L702-L709 — `SleepForCommunicate()` 仍靠同步 `ProcessMessages()` 与 `Sleep(0)` 维持通讯；同文件 L712-L797、L2845-L2859 — `ProcessMessages()` / `TrySendingMessages()` 仍在当前线程对每个 client 直接执行 `HasPendingData()`、`Recv()`、`Send()`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugTransportPump.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugTransportPump.cpp`；如需与停点控制复用，则修改 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/AngelscriptPauseController.h` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/AngelscriptPauseController.cpp`
- [ ] **P17.1** 📦 Git 提交：`[Debug/Transport] Refactor: move socket pumping off pause and exception execution lanes`
- [ ] **P17.1-T** 单元测试：验证慢 client / partial delivery / mid-pause disconnect 不再由 stop 线程直接承担 socket IO
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugTransportDispatchTests.cpp`
  - 测试场景：
    - 正常路径：fake transport 以 drip-feed 方式投递 `Continue` / `RequestCallStack` 时，paused runtime 只消费 command queue，stop/continue 语义仍稳定完成，不需要在测试线程里实际 `Recv()`。
    - 边界条件：同一 session 正在发送大体积 `DebugDatabase`/`AssetDatabase` chunk 时，`ProcessException()` 或 `PauseExecution()` 仍可在限定 tick 数内产出 `HasStopped` 与 `HasContinued`，不被大包 backpressure 直接拖慢。
    - 错误路径：transport worker 在 paused 态遇到 bad envelope 或 disconnect 时，runtime 收到的是显式 disconnect/cleanup 命令，而不是 busy-loop、同步阻塞或永远卡在 `while (bIsPaused)`。
  - 测试命名：`Angelscript.CppTests.Debug.Transport.CommandQueueDecouplesPauseLoopFromSocketIO`
  - 隔离方式：`FAngelscriptEngineScope` + fake transport pump
- [ ] **P17.1-T** 📦 Git 提交：`[Test/Debug] Test: add transport dispatch decoupling coverage`

### 新增 Phase 18：把 DebugDatabase 升级为 typed symbol contract

- [ ] **P18.1** 将 `DebugDatabase` 从 legacy mixed JSON tree 升级为 typed symbol chunk，并把现有 JSON tree 收口成兼容 adapter
  - 现有 `P7.1` / `P16.1` 已补 `Version`、`Revision`、`StreamId` 和 snapshot/delta 边界，但 payload 本身仍然是不透明的 `FString Database`：`SendDebugDatabase()` 先拼一个全局 `FJsonObject Root`，再把 type、namespace、enum、global property 与 method 混在同一棵树里；property/enum value 继续靠 tuple 位置表达语义，namespace 与 enum 继续共用 `Root->SetObjectField(TEXT(\"__\") + NS.Key, ...)` 的根节点形状。这样即使 stream 边界稳定了，consumer 仍必须理解一套历史 VS Code shape，而不是显式的 symbol schema。
  - 本项不扩成 `Arch-DT47` 的全量 authoritative symbol graph，也不重复 `Plan_DebugAdapter.md` 的前端消费逻辑；目标是先在 server 侧确立最小 typed contract：新增 `DebugSymbolChunk` 或等价消息体，为每个符号条目显式携带 `SchemaVersion`、`Revision`、`Kind`、`SymbolId`、`ContainerId` 与 payload；legacy `DebugDatabase` JSON tree 则改成由 typed entries 投影而来，在迁移期双写而不是双真相。这样 `type`、`namespace`、`enum`、`property`、`global` 的身份与层级都可以在 wire 上正式表达，而不再依赖 tuple 位置、`__namespace` 前缀和 `isEnum=true` 这类隐式约定。
  - 完成标准不是“一次替换所有 client”，而是让 server 自己先拥有一份可演进的 typed schema：未来无论是第二种 IDE、离线索引器还是 runtime diagnostics，都可以直接消费 typed chunk，而不必理解 legacy mixed tree；同时当前旧前端仍能继续吃原有 JSON tree，直到完成适配。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 29 — “`DebugDatabaseSettings` / `AssetDatabase` 写了 `Version` 字段却不按版本解码，协议升级没有兼容护栏”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` `NewTest-14` — “目前只覆盖 `RequestDebugDatabase` 的消息顺序，没有真实 payload contract 回归”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT48` — “`DebugDatabase` 仍是异构 JSON 树 + tuple payload，前端合同被历史 VS Code shape 锁死”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` — “当前仓库既然自己拥有调试 contract，`DebugDatabaseSettings`/database payload 就必须按正式协议字段治理，而不是继续靠隐式 shape”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L185-L190 — `FAngelscriptDebugDatabase` 仍只有一个 `FString Database` 字段，没有 typed entry 容器；同文件 L536-L547 — `FAngelscriptDebugDatabaseSettings` 仍只写裸 `Version` 与若干布尔位，没有 schema identity；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1507-L1517 — `SendDebugDatabase()` 仍以单一 `FJsonObject Root` 序列化后立即清空继续拼下一批；同文件 L1738-L1756 — property 仍编码成 `[type, flags?, doc?]` tuple，且保留“旧版 VS Code 假设第二项必须是 string”的兼容注释；同文件 L1919-L2041 — global function/global property/enum 仍先混入 `NSFunctions`，再统一投成 `Root->SetObjectField(TEXT("__") + NS.Key, NSDesc)` 的 root 节点，enum value 继续复用 property tuple shape。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugSymbolChunk.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugSymbolChunk.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugSymbolSerializer.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugSymbolSerializer.cpp`
- [ ] **P18.1** 📦 Git 提交：`[Debug/Database] Refactor: add typed symbol chunks alongside legacy debug database tree`
- [ ] **P18.1-T** 单元测试：验证 typed symbol chunk 具备稳定 identity，并与 legacy tree 共用同一 revision
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugDatabaseTypedSchemaTests.cpp`
  - 测试场景：
    - 正常路径：type、namespace、enum、global property、method 各自产生的 typed chunk 都带正确 `Kind/SymbolId/ContainerId/Revision`，consumer 不依赖 tuple 位置即可重建符号层级。
    - 边界条件：同一轮 `RequestDebugDatabase` 同时输出 typed chunk 与 legacy tree 时，两者 `Revision/StreamId` 一致；enum value 与 global property 同名场景不会再因为共用 property tuple shape 而歧义。
    - 错误路径：unknown `SchemaVersion`、缺失 `Kind`/`SymbolId` 或不完整 chunk 会触发 explicit reject/fallback-to-legacy，而不是被误解析成旧版 `properties` tuple 或静默吞掉。
  - 测试命名：`Angelscript.CppTests.Debug.Database.TypedSymbolChunksCarryStableIdentity`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P18.1-T** 📦 Git 提交：`[Test/Debug] Test: add typed debug-database schema coverage`

### 单元测试总览补充（本轮深化 2026-04-09 07:09:54）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P17.1` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugTransportDispatchTests.cpp` | slow client/backpressure、mid-pause disconnect、command queue 与 socket IO 解耦 | P1 |
| `P18.1` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugDatabaseTypedSchemaTests.cpp` | typed symbol identity、dual-write revision 一致性、unknown schema reject/fallback | P1 |

### 验收标准补充（本轮深化 2026-04-09 07:09:54）

1. `PauseExecution()`、`ProcessException()` 与 `SleepForCommunicate()` 不再直接承担 raw socket `Recv()/Send()`；慢 client 或坏包最多影响 transport worker / session，不再把当前脚本执行线程拖进同步 network loop。
2. `DebugDatabase` 在保留 legacy tree 兼容面的同时，新增一条带 `SchemaVersion + Revision + Kind + SymbolId + ContainerId` 的 typed symbol contract；新 consumer 不再需要依赖 `__namespace` 前缀和 property tuple 位置推断语义。
3. 同一轮 database 同步中，typed chunk 与 legacy tree 共享同一 `StreamId/Revision`；symbol identity 与 stream identity 都可被自动化独立回归。

### 风险与注意事项补充（本轮深化 2026-04-09 07:09:54）

#### 风险

1. `P17.1` 若 transport queue 与 stop/pause 状态机的 handoff 顺序不清，容易把“IO 反压 runtime”修成“`Continue/Step*` 乱序”或新的 paused-state 死锁。
2. `P18.1` 在 dual-write 迁移期同时维护 typed chunk 和 legacy tree；若 serializer 没有明确 single source of truth，最容易出现“两套 payload 都合法，但语义悄悄分叉”的双真相问题。

#### 已知行为变化

1. 调试时序将从“pause loop 里顺手 pump socket”收紧为“transport worker 产生命令、runtime 只消费命令”；依赖 inline network side effect 的测试 helper 需要改成 queue/transcript 断言。
2. `RequestDebugDatabase` 成功后，客户端可能额外收到 typed symbol chunk；旧前端应通过 version/capability gate 忽略它们，新前端则应切到 typed schema 而不是继续解析 legacy mixed tree 的 tuple 约定。

---

## 深化 (2026-04-09 07:18:55)

本轮继续只读复核 AutoPlans 输入、`Documents/Plans/` 活跃 Plan 与当前源码后，确认现有 `P1-P18` 仍未单独承接两组残余缺口：其一，Windows data breakpoint 的 `VEH` handler 仍以进程级 `GActiveDebugServer` 为唯一 owner，并会吞掉未被本插件确认的 `EXCEPTION_SINGLE_STEP`；其二，`DebugServer V2` 仍以 `0.0.0.0` 裸 TCP + 空 `RequestDebugDatabase` / 只有 `DebugAdapterVersion` 的 `StartDebugging` 暴露完整 debug/workspace 协议面。另再次核对用户指定的 [B] 输入文件后，`Documents/AutoPlans/DiscoveryPlans/DebuggingAndJIT_Plan.md` 仍不存在，因此本轮新增条目继续仅使用 [A]/[C]/[D]/[E] 交叉确认，不据此编造来源。

### 新增 Phase 19：补齐 Windows watchpoint owner 与 transport trust boundary

- [ ] **P19.1** 将 Windows data breakpoint 的异常入口从“单个全局 server 抢占 + 无条件 claim 单步异常”升级为 host-aware claim contract
  - 现有 `P3.1`、`P9.4` 和 `P14.3` 已经分别承接了寄存器编程、target-thread replay 与 stop storm，但 `VEH` 入口本身仍是错误的 authority：`DebugRegisterExceptionHandler()` 先按 `Dr6` 低位与进程级 `GActiveDebugServer` 认定“这一定是当前 DebugServer 的 watchpoint”，随后即使没有任何 `ActiveDataBreakpoint` 真正满足 `Status == Keep` 且被本插件确认命中，也会清 `Dr6` 并直接 `return EXCEPTION_CONTINUE_EXECUTION`。这会让外部 debugger、其他硬件断点或第二个 `DebugServer` 的单步异常被本插件静默吃掉。
  - 本项不重复 `P3.1` 的寄存器位修正，也不重复 `P9.4` 的 target-thread replay；目标是把 owner/claim 语义前置到 handler 本身：用最小 `DebugHostRegistry` 或等价 owner map 将 `thread/execution -> debug host` 建模出来，`VEH` 只在“已找到 owner host 且至少一个 watchpoint 被该 host 确认 claim”时才清理寄存器并返回 `EXCEPTION_CONTINUE_EXECUTION`；否则必须 `EXCEPTION_CONTINUE_SEARCH`，让外部 handler/native debugger 保持可见。析构路径也不能再把仍在用的全局 handler 一刀切拆掉，而要按注册 host 数量安全收口。
  - 这一步的重点不是立刻做完整多 IDE/多 engine discovery，而是先把 Windows watchpoint handler 从“最后创建的 server 赢”修到“owner 可追踪、foreign single-step 不被误吞、第二个 host 不会偷走第一个 host 的异常路由”。只有先收口这条 contract，后续 `EngineInstanceId`、symbolic watchpoint 或更高层的 attach routing 才不会建立在错误的异常入口上。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 18 — “Windows data breakpoint 基础设施是进程级单例，第二个 `DebugServer` 会接管前一个实例的异常处理”
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 83 — “Windows data breakpoint 异常处理器会吞掉未被本插件确认的 `EXCEPTION_SINGLE_STEP`”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT11` — “Windows hardware breakpoint 后端需要从单个 `GActiveDebugServer` 升级为按 `thread/execution` 反查 owner host 的注册表”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT31` — “watchpoint backend 目前仍直接绑到 `GActiveDebugServer`、`DR0-DR3` 与单个 vectored exception handler，后端不可替换且 owner 不可移植”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L111-L117 — `DataBreakpoint_Windows` 仍以进程级 `GActiveDebugServer` 和单个 `DegbugRegisterExceptionHandlerHandle` 维护 owner；同文件 L279-L285 — handler 只按 `EXCEPTION_SINGLE_STEP` + `Dr6` 低位决定是否进入本插件路径；同文件 L302-L305、L378-L379 — 即使没有任何 `Keep` 状态 watchpoint 被本插件确认 claim，当前路径也会继续走到 `EXCEPTION_CONTINUE_EXECUTION`；同文件 L410-L423 — 构造/析构 `FAngelscriptDebugServer` 时仍会无条件覆盖或移除整个进程的 vectored exception handler，而不是按 host 数量与 owner 注册表管理。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Breakpoints/AngelscriptDebugHostRegistry.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Breakpoints/AngelscriptDebugHostRegistry.cpp`；如 `P3.1` 已先落地，则继续修改 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Breakpoints/AngelscriptHardwareDataBreakpointBackend_Windows.h` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Breakpoints/AngelscriptHardwareDataBreakpointBackend_Windows.cpp`
- [ ] **P19.1** 📦 Git 提交：`[Debug/DataBreakpoint] Refactor: route windows single-step claims by host ownership`
- [ ] **P19.1-T** 单元测试：验证 Windows watchpoint handler 只 claim 已确认 owner 的单步异常，且多 host 不再互相偷走异常入口
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptWindowsDataBreakpointHostOwnershipTests.cpp`
  - 测试场景：
    - 正常路径：为某个 registered host 构造 1 个已安装的 watchpoint 与对应的 `EXCEPTION_SINGLE_STEP` 上下文时，handler 会把 stop 归属到正确 host，并返回 `EXCEPTION_CONTINUE_EXECUTION`。
    - 边界条件：同一进程注册两个 debug host / engine clone 后，第二个 host 的注册不会覆盖第一个 host；两侧各自的 target thread 触发 watchpoint 时都能回到各自 owner，而不是统一落到最后创建的 `DebugServer`。
    - 错误路径：`Dr6` 低位被置位但没有任何本插件 watchpoint 被确认 claim、或命中来自 foreign debugger/其他硬件断点时，handler 返回 `EXCEPTION_CONTINUE_SEARCH`，不会清空他方异常或伪造本插件 stop。
  - 测试命名：`Angelscript.CppTests.Debug.DataBreakpoint.WindowsHostRegistryClaimsOnlyOwnedSingleStep`
  - 隔离方式：`FAngelscriptEngineScope` + fake `EXCEPTION_POINTERS/CONTEXT` + host-registry seam
- [ ] **P19.1-T** 📦 Git 提交：`[Test/Debug] Test: add windows watchpoint host-ownership coverage`

- [ ] **P19.2** 为 `DebugServer V2` 增加 loopback-first 的 trust boundary，并把 debug/workspace 能力拆成显式 domain gate
  - 当前 `P1.7` 已经开始收口 session admission，`P15.2` 也把 workspace 命令改成了 result contract，但 transport 层仍完全没有 trust boundary：listener 默认 `FIPv4Address::Any`，接受连接时只记日志然后直接入队；`RequestDebugDatabase` 是空消息，`StartDebugging` 只有 `DebugAdapterVersion` 一个字段；同一 `EDebugMessageType` 里又混放了 `RequestDebugDatabase`、`StartDebugging`、`FindAssets`、`CreateBlueprint`、`ReplaceAssetDefinition` 等 debug/workspace 命令。也就是说，任意能连上端口的 peer 仍然默认看得到 debug surface，且没有一条正式路径区分 `debug.read`、`debug.control`、`workspace.read` 与 `workspace.write`。
  - 本项不重复 `P1.7` 的 session route policy，也不重复 `P15.2` 的 success/failure payload；目标是把准入条件前移到 transport/handshake：新增 `DebugTransportSettings` 或等价配置，将默认监听地址收紧为 `127.0.0.1`，只有显式开启 `bAllowRemote` 才允许非 loopback；在现有 `V2` 协议上追加最小 `Hello/Auth` 或 capability handshake，至少携带 `ClientName`、`RequestedDomains`、`Token/HMAC`，再由 server 派发 `AllowedDomains`。legacy client 仅在 loopback 且未配置 token 时保留兼容空握手。
  - 这样修的收益不是“更安全”四个字，而是把当前混在一条 socket 上的行为分层成可验证 contract：本机旧前端继续可用；remote 连接如果没有显式授权，就只能拿到 `debug.read` 或直接被拒绝；`CreateBlueprint`、`FindAssets`、`ReplaceAssetDefinition` 这类 workspace 副作用必须拿到 `workspace.write/read` capability 后才允许触发，拒绝时返回结构化 `Diagnostics`，不再依赖“连上 socket 就能弹宿主机 UI”。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 24 — “未 `StartDebugging` 的客户端也能直接驱动 `Pause/Continue/Step*`、断点与 `RequestEvaluate/Variables`”
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 42 — “`CreateBlueprint` 没有任何成功/失败回执，非法输入会在目标进程弹出模态对话框”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT29` — “`DebugServer V2` 没有 trust boundary，remote attach 与 workspace 写操作默认共享同一权限面”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` — “当前仓库把 source-level debugger transport 直接收在插件 runtime 自己维护，因此能力发现、准入和协议边界都必须由当前仓库自己定义，而不是外部生态代劳”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L395-L399 — `HandleConnectionAccepted()` 仍只记录来源地址并把 socket 入 `PendingClients`；同文件 L405-L408 — listener 仍固定绑定 `FIPv4Address::Any`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L25-L80 — 同一 `EDebugMessageType` 仍同时暴露 debug、database 与 workspace 消息；同文件 L103-L116 — `FStartDebuggingMessage` 仍只有 `DebugAdapterVersion`；同文件 L177-L183 — `FAngelscriptRequestDebugDatabase` 仍是空消息体；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L822-L827 — 收到 `RequestDebugDatabase` 后立即下发 database/diagnostics；同文件 L1161-L1188 — `FindAssets` / `CreateBlueprint` 仍直接触发本地 workspace side effect，没有任何 domain gate 或鉴权前置。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugTransportSettings.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugTransportSettings.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugHandshake.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugHandshake.cpp`
- [ ] **P19.2** 📦 Git 提交：`[Debug/Transport] Feat: add loopback-first trust boundary and domain-gated handshake`
- [ ] **P19.2-T** 单元测试：验证 legacy loopback 兼容、remote 准入与 workspace domain gate 都有稳定合同
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugTransportTrustBoundaryTests.cpp`
  - 测试场景：
    - 正常路径：server 处于默认 loopback-only 且未配置 token 时，legacy 本机 client 仍可完成 `RequestDebugDatabase` / `StartDebugging`，保持现有工作流兼容。
    - 边界条件：显式开启 remote + token 后，新 client 用正确 token 连接，只拿到被授权的 `debug.read/debug.control`，未授予 `workspace.write` 时不能触发 `CreateBlueprint` / `ReplaceAssetDefinition`。
    - 错误路径：非 loopback 或 token 错误的 peer 在握手阶段就被拒绝；未经授权发送 `FindAssets` / `CreateBlueprint` 时得到明确 `Diagnostics`/error，而不是 silent side effect 或宿主机模态框。
  - 测试命名：`Angelscript.CppTests.Debug.Transport.LoopbackLegacyHandshakeAndDomainGating`
  - 隔离方式：`FAngelscriptEngineScope` + fake client endpoint/capability config + fake workspace delegates
- [ ] **P19.2-T** 📦 Git 提交：`[Test/Debug] Test: add trust-boundary and domain-gating coverage`

### 单元测试总览补充（本轮深化 2026-04-09 07:18:55）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P19.1` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptWindowsDataBreakpointHostOwnershipTests.cpp` | host ownership、foreign single-step pass-through、multi-host handler 路由 | P1 |
| `P19.2` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugTransportTrustBoundaryTests.cpp` | legacy loopback 兼容、remote auth、workspace domain gate | P1 |

### 验收标准补充（本轮深化 2026-04-09 07:18:55）

1. Windows `VEH` handler 只会在“已找到 owner host 且至少一个 watchpoint 被本插件确认 claim”时返回 `EXCEPTION_CONTINUE_EXECUTION`；foreign single-step、其他硬件断点与外部 debugger 的异常不再被本插件吞掉。
2. 同一进程存在多个 debug host / engine clone 时，Windows data breakpoint 的异常路由不再依赖最后创建的 `FAngelscriptDebugServer*`；任一 host 析构都不会误拆其他 host 仍在使用的 handler 所有权。
3. `DebugServer` 默认只暴露 loopback listener；非 loopback 或未授权 peer 不能再无条件访问 `RequestDebugDatabase`、`StartDebugging` 与 workspace 命令，`debug.read/debug.control/workspace.read/workspace.write` 至少具备一套可回归的 capability gate。

### 风险与注意事项补充（本轮深化 2026-04-09 07:18:55）

#### 风险

1. `P19.1` 若 owner-host 解析过严，可能把本应由当前 watchpoint claim 的 single-step 错判成 foreign exception，造成 watchpoint 漏停；因此必须把 “claim success / continue-search” 两条路径都做成可单测的纯函数或 seam。
2. `P19.2` 若 legacy loopback 兼容门没有设计清楚，可能一次性打断现有 VS Code/测试 client；必须明确“loopback + no token”旧握手仍可用，而 remote/auth/domain gate 只对新配置或非 loopback 生效。

#### 已知行为变化

1. Windows 上来自其他 debugger/其他硬件断点的 `EXCEPTION_SINGLE_STEP` 将不再被 Angelscript watchpoint handler 静默吃掉；以前依赖这种错误副作用“帮忙继续执行”的行为将消失。
2. `DebugServer` 的默认暴露面将从“任何能连上端口的 peer 都看到完整协议面”收紧为“loopback-first + 按 domain 授权”；remote 调试或 workspace 写操作需要显式配置监听地址、token 或 capability。

---

## 深化 (2026-04-09 07:28:17)

本轮继续只读复核 AutoPlans 输入、`Documents/Plans/` 活跃 Plan 与当前源码后，确认现有 `P1-P19` 仍未单独承接两组结构性 residual gap：其一，`Debugging` 侧的 public compile surface 仍把 transport、`V2` wire schema 与 runtime 调试语义压在 `AngelscriptDebugServer.h` 一处；其二，`StaticJIT` 侧的 native-form / devirtualization / direct-call 决策仍停留在 ambient state 与 live branch 上，没有可落盘、可校验的 assumption contract。两者都不与 `Documents/Plans/Plan_DebugAdapter.md` 的前端桥接工作或 `Documents/Plans/Plan_StaticJITUnitTests.md` 的低层单测扩面重复；本轮只补 backend contract。另再次核对用户指定的 [B] 输入文件后，`Documents/AutoPlans/DiscoveryPlans/DebuggingAndJIT_Plan.md` 仍不存在，因此本轮新增条目仅使用 [A]/[C]/[D]/[E] 交叉确认，不据此编造来源。

### 新增 Phase 20：收窄调试 public facade，并把 StaticJIT 假设提升为 sidecar contract

- [ ] **P20.1** 抽出 transport-free `DebugRuntimeFacade`，把 `AngelscriptDebugServer.h` 从 public umbrella 收窄为组合根/兼容转发层
  - 现有 `P10.2` 已经开始冻结 session transcript，`P19.2` 也开始把握手/准入前移，但 `Debugging` 的 public compile surface 还没有被真正收窄。今天任何想复用 `CallStack/Variables/Evaluate` 语义的代码，首先就必须吞下 `Sockets.h`、`TcpListener.h`、`FArchive` message struct、`FTcpListener` 成员和 `FSocket*` client 列表；这让 transport-free 单测、实验性只读 bridge、或未来 bridge packaging 仍然绕不开 `V2/TCP` 内部细节。
  - 本项不重复 `P19.2` 的 trust boundary，也不直接展开 `Plan_DebugAdapter.md` 的前端实现；目标是先把 backend 自己的边界摆正：新增 `AngelscriptDebugRuntimeTypes` 与 `IAngelscriptDebugRuntimeFacade`，只承载 `CallFrame`、`VariableSnapshot`、`SourceLocation`、`StopReason`、`TryGetCallStack/TryGetScope/TryEvaluate/TryResolveDefinition/ApplyBreakCommand` 这类 transport-agnostic 语义。现有 `EDebugMessageType`、envelope codec、`SendMessageToClient()` 与 raw TCP accept/pump 则迁到 `Debugging/Protocol/V2/` 与 `Debugging/Transport/Tcp/`；旧 `AngelscriptDebugServer.h` 第一阶段仅作为兼容转发层与组合根保留。
  - 这样做的收益不是“换目录结构”，而是给后续 `V2`、实验性 `DAP/CDP` bridge、以及 `P10.2` transcript harness 一个真正稳定的 backend 依赖面。只要 runtime facade 成立，后续想测试 `Variables/Evaluate`、想做 headless 调试消费者，或想把 VS Code 假设外移时，就不再需要把 socket state machine 一起带进来。
  - 来源：
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT50` — “`AngelscriptDebugServer.h` 把 socket、wire schema 与调试语义一起暴露为 public surface，扩展前端前先放大编译边界”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT51` — “调试前端协议所有权仍内建在核心插件里，协议演进与 IDE 生态被迫跟随 Runtime 发版”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` — “当前仓库把调试协议当正式 API 守护，应保留 UE-owned contract，但不必继续把 transport 细节扩散到所有 consumer”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L3-L13 — public header 仍直接包含 `Sockets.h`、`TcpListener.h`、`UdpSocketReceiver.h`、`MemoryWriter.h`、`MemoryReader.h`；同文件 L25-L80 — `EDebugMessageType` 仍把 debug/database/workspace message 全部定义在同一头文件；同文件 L581-L739 — `FAngelscriptDebugServer` 仍同时持有 `FTcpListener`、`FSocket*` client 容器、pause state、`GetDebuggerValue()`/`GetDebuggerScope()`/`GoToDefinition()` 等 runtime 语义；同文件 L648-L687 — `SendMessageToAll/SendMessageToClient` 模板仍在 header 内直接序列化 envelope 并触发发送。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Public/AngelscriptDebugRuntimeTypes.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Public/IAngelscriptDebugRuntimeFacade.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/V2/AngelscriptDebugV2Messages.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/V2/AngelscriptDebugV2Envelope.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transport/Tcp/AngelscriptDebugTcpTransport.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transport/Tcp/AngelscriptDebugTcpTransport.cpp`
- [ ] **P20.1** 📦 Git 提交：`[Debug/Facade] Refactor: split runtime facade from V2 protocol and tcp transport`
- [ ] **P20.1-T** 单元测试：验证 facade-only consumer 不再依赖 socket 头，同时现有 `V2` transcript 行为保持兼容
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugRuntimeFacadeTests.cpp`
  - 测试场景：
    - 正常路径：测试 TU 只包含 `IAngelscriptDebugRuntimeFacade.h` 与 `AngelscriptDebugRuntimeTypes.h`，通过 fake runtime snapshot 调用 `TryGetCallStack`、`TryEvaluate`、`TryGetScope`，无需创建 `FSocket` 或 `FTcpListener` 即可返回稳定结果。
    - 边界条件：现有 `V2` adapter 建在 facade 之上后，`Start -> Pause -> CallStack -> Continue` transcript 仍与 `P10.2` 的 golden session contract 一致，旧 `V2` 客户端不需要立刻改 payload。
    - 错误路径：malformed/disconnected transport 只在 `Protocol/V2` 或 `Transport/Tcp` 层返回失败；facade 层只暴露结构化 `unsupported/error`，不再向 consumer 泄漏 `FSocket*`、`Recv()` 或 envelope 细节。
  - 测试命名：`Angelscript.CppTests.Debug.Facade.TransportFreeRuntimeContract`
  - 隔离方式：`FAngelscriptEngineScope` + fake runtime snapshot sink
- [ ] **P20.1-T** 📦 Git 提交：`[Test/Debug] Test: add runtime facade and transport-free conformance coverage`

- [ ] **P20.2** 将 `StaticJIT` 的 native-form / devirtualization 决策从隐式旁听与临场分支提升为可回归 `JITAssumptionManifest`
  - 现有 `P2.3`、`P5.1`、`P9.3`、`P13.2` 已分别修了 direct-call correctness、native fallback 语义、异常桥接与 registry 生命周期，但这些条目仍没有回答一个更结构性的问题：为什么某个函数今天会被视为 `final`、为什么某个 imported/script call 会走 direct-call、某个 native/template call 又是依据哪一条 bind 记录拿到了 custom form。只修 correctness 还不够；如果没有 assumption contract，未来 override 图、模板方法顺序或 imported binding 一变，JIT 仍然只能在运行时 silent downgrade 或撞进旧假设。
  - 本项的目标不是重复 `P2.3` 的 direct-call/`DynamicCall` 行为修复，而是把“决策依据”落盘成 sidecar：为 native-form 采集补 `JitBindingRecord`，为去虚化/`final`/import 绑定补 `DispatchAssumptionRecord`，至少记录 `FunctionId`、`ModuleName`、`BindKind`、`AssumptionKind`、`TargetFunctionId/BoundId`、`Reason`。生成期先把这些记录写入 `JITAssumptionManifest`；运行期若发现 override 图或 imported binding 与 manifest 不一致，只定向降级受影响函数/callsite，而不是继续盲信 live state 或整包禁用。
  - 这一步与 `P16.2` 的 deterministic output manifest 互补：`P16.2` 固定“写出什么文件”，本项固定“为什么这么编译”。只有两者同时成立，CI、日志与后续 bridge/tooling 才能真正解释“同一个函数是 direct/custom/pointer/dynamic 哪一层 authority 决定的”，而不是继续把关键决策埋在 `GetPreviousBind()`、模板方法顺序和 `FunctionsToGenerate.Find(...)` 这些临场分支里。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 20 — “模板类型的 native form 解析按方法顺序对齐，不按签名/名称匹配，JIT custom call 容易绑错目标”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT13` — “native-form 元数据仍靠 `GetPreviousBind()` 临场旁听，JIT 可编译性没有独立的持久化合同”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT30` — “dispatch/devirtualization assumption 被隐式写进生成过程，缺少可追踪的 assumption ledger”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D8]` — “current AS 的三轨 authority / fallback 命中情况应继续做成可观测数据，而不是只留在运行时猜测里”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` L27-L59 — `GScriptNativeForms` 仍是 process-global map，模板路径仍明确假设 “methods are always in the same order”；同文件 L112-L116 — native constructor 绑定仍直接 `GScriptNativeForms.Add(FAngelscriptBinds::GetPreviousBind(), ...)`，native-form 采集依赖 ambient `previous bind`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` L3415-L3439 — `DevirtualizeFunction()` 仍直接读取当前 imported binding / virtual override live state 决定 direct target；同文件 L3458-L3463 — `AnalyzeScriptFunction()` 仍在没有任何 sidecar 记录的情况下直接 `SetTrait(asTRAIT_FINAL, true)`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` L1171-L1194 — callsite 仍仅依据这些隐式前置决策在 `WriteDirectCall()` 与 `WriteDynamicCall()` 之间切换，没有原因记录、没有 selective invalidation key。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/Assumptions/AngelscriptJitAssumptionManifest.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/Assumptions/AngelscriptJitAssumptionManifest.cpp`
- [ ] **P20.2** 📦 Git 提交：`[StaticJIT/Assumption] Feat: persist native-form and dispatch assumptions with selective downgrade`
- [ ] **P20.2-T** 单元测试：验证 assumption manifest 能解释 direct/custom/dynamic 决策，并在假设失效时只定向降级受影响路径
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITAssumptionManifestTests.cpp`
  - 测试场景：
    - 正常路径：无 override 的脚本函数、稳定 imported binding 与 template/native custom-call 会写出带 `AssumptionKind/BindKind/TargetFunctionId` 的稳定记录，direct/custom path 与 manifest 一致。
    - 边界条件：新增 derived override 或调整 imported binding 后，只失效受影响的 assumption record，相关 callsite 定向退回 `DynamicCall` 或 `InterpreterOnly`；未受影响函数继续保持 direct/custom path。
    - 错误路径：缺失或 stale assumption record 时，运行时返回明确 downgrade/diagnostic，不再继续沿用旧 `final`/旧 target/旧 native-form，也不会退回整包 `check` 或全局禁用。
  - 测试命名：`Angelscript.CppTests.StaticJIT.Assumptions.ManifestAndSelectiveDowngrade`
  - 隔离方式：`FAngelscriptEngineScope` + temporary JIT assumption output root
- [ ] **P20.2-T** 📦 Git 提交：`[Test/StaticJIT] Test: add assumption manifest and selective-downgrade coverage`

### 单元测试总览补充（本轮深化 2026-04-09 07:28:17）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P20.1` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugRuntimeFacadeTests.cpp` | facade-only consumer、transport-free transcript、transport error 不外泄 | P1 |
| `P20.2` | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITAssumptionManifestTests.cpp` | assumption record 落盘、override/import 失效、selective downgrade | P1 |

### 验收标准补充（本轮深化 2026-04-09 07:28:17）

1. `Debugging` 的 public compile surface 不再要求所有 consumer 直接包含 `Sockets.h`、`TcpListener.h` 与整套 `V2` message enum；仅依赖 runtime 调试语义的测试/bridge 可以只包含 facade/types 头文件。
2. `V2` 仍作为默认 bridge 保持现有 wire format 与 transcript contract，但其 protocol/transport 实现已经与 runtime facade 分层；`Protocol/V2` 或 `Transport/Tcp` 的失败不会再以 public header 依赖的形式泄漏给所有 consumer。
3. `StaticJIT` 至少具备一份 machine-readable `JITAssumptionManifest`，能够解释某函数/调用点为何走 `direct/custom/dynamic`；当 override 图、import 绑定或 native-form 假设变化时，只受影响的函数/callsite 会被定向降级并给出原因。

### 风险与注意事项补充（本轮深化 2026-04-09 07:28:17）

#### 风险

1. `P20.1` 若 facade 粒度切得过粗，可能把今天还能共享的 `V2`/runtime helper 复制成两套逻辑；第一阶段必须坚持“旧 header 转发，新 facade 单一事实源”，避免只是换文件名不换耦合。
2. `P20.2` 若 assumption key 设计不稳，最容易出现“记录写出来了，但无法准确 join 到运行时函数/callsite”；模板实例、imported function 与 generated helper 必须先定义 canonical key，再启用 selective downgrade。

#### 已知行为变化

1. 未来新增调试 bridge/test consumer 时，将优先依赖 `DebugRuntimeFacade` 而不是直接 include `AngelscriptDebugServer.h`；旧 consumer 在双轨期仍可继续编译，但新增代码不应再把 `FSocket`/`FTcpListener` 当默认依赖。
2. `StaticJIT` 的 direct/custom path 将从“静默按 live state 决策”收紧为“先有 assumption record，再应用优化”；部分历史上看似“还能 direct-call”的路径，若缺少稳定 assumption 记录，将改为显式 downgrade 并给出诊断。

---

## 深化 (2026-04-09 07:37:05)

本轮继续只读复核用户指定输入、`Documents/Plans/` 活跃 Plan 与当前 `Debugging/`、`StaticJIT/` 源码后，确认现有 `P1-P20` 已分别承接 session、typed `DebugDatabase`、runtime facade、`JITAssumptionManifest` 等主线，但还有两组 backend residual gap 尚未被单独固化：其一，`V2` wire contract 的事实源仍散落在 inline `operator<<`、全局 `DebugAdapterVersion` 与 `HandleMessage()` 分支里；其二，`PrecompiledData` 明明持有 rich symbol，却在 minimized runtime 路径上被有条件跳过回填并随后清空，没有独立 `DebugSymbols` artifact。它们都不与 `Documents/Plans/Plan_DebugAdapter.md` 的前端 adapter 工作或 `Documents/Plans/Plan_StaticJITUnitTests.md` 的通用补测重复；本轮只补 runtime/backend contract。再次核对后，用户列出的 [B] `Documents/AutoPlans/DiscoveryPlans/DebuggingAndJIT_Plan.md` 仍不存在，因此本轮新增条目仅使用 [A]/[C]/[D]/[E] 交叉确认，不据此编造来源。

### 新增 Phase 21：冻结 wire contract，并保留 minimized runtime 的只读调试符号

- [ ] **P21.1** 将 `DebugServer V2` 的 message body contract 收敛为单一 `ProtocolDescriptor + Codec`，冻结 mixed-version / capability golden case
  - 现有 `P7.1` 已把 `DebugDatabaseSettings` / `AssetDatabase` 拉回 versioned decode，`P18.1` 也开始把 `DebugDatabase` 收敛为 typed symbol chunk，`P20.1` 则把 runtime facade 从 transport 中拆开；但这三步之后，wire-level 事实源仍然没有收口。今天 `StartDebugging`、`Variables`、`AssetDatabase`、`DebugDatabaseSettings` 乃至 envelope 本体，仍然靠 `AngelscriptDebugServer.h` 中一组 inline `operator<<`、`Ar.AtEnd()` 尾字段判断、全局 `DebugAdapterVersion` 与 `HandleMessage()` 手写分派共同维持兼容，新增字段时依旧需要人工同步多处 C++ 约定与测试 helper。
  - 本项不重复 `P20.1` 的 facade/transport 边界，也不展开 `Plan_DebugAdapter.md` 的 TypeScript adapter；目标是先让 runtime 自己拥有一个可枚举的 wire 事实源：新增 `ProtocolDescriptor` / `Schema` 统一声明 `message id`、字段顺序、`optionalSince`、`requiresCapability`、legacy alias 与默认降级策略；现有 `FArchive` body layout 和 `V2` message id 保持不变，但 body encode/decode 统一走 `Codec`，不再让 message struct 自己暗藏兼容逻辑。
  - 这样做的收益不是“再包一层序列化”，而是把 mixed-version、legacy empty request、`DEBUG_SERVER_VERSION` 演进与 `RequestDebugDatabase` 初始化序列真正冻结成单一 contract。后续无论补 `GoToDefinition` result、typed symbol chunk、还是第二种 front-end/代理，都不必再依赖“改一个字段就手补一串 `Ar.AtEnd()` 分支”的隐式流程。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 29 — “`DebugDatabaseSettings` / `AssetDatabase` 写了 `Version` 字段却不按版本解码，协议升级没有兼容护栏”
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 69 — “`Evaluate` / `Variables` / `CallStack` 响应只能靠顺序猜测归属，协议缺少显式 request contract”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-27 — “现有 debugger happy-path 把 `SendStartDebugging(2)` 硬编码为固定旧版本，不会自动覆盖当前协议版本”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` `NewTest-14` — “`RequestDebugDatabase` 只有 struct round-trip，没有真实消息序列 / payload contract 回归”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT9` / `Arch-DT34` — “`V2` 协议演进仍是手写 `FArchive` 契约，缺少独立 `schema + codec` 层”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` — “调试协议已经是仓内正式 contract，应先收敛 wire owner，再谈多 IDE bridge”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L86-L93 — envelope 仍只有 `MessageType + Body`，没有独立 message descriptor；同文件 L103-L115 — `FStartDebuggingMessage` 仍在结构体内部用 `Ar.IsSaving() || !Ar.AtEnd()` 维护兼容；同文件 L426-L437 — `FAngelscriptVariable` 仍直接读取全局 `AngelscriptDebugServer::DebugAdapterVersion` 决定是否序列化 `ValueAddress/ValueSize`；同文件 L507-L553 — `FAngelscriptAssetDatabase` 与 `FAngelscriptDebugDatabaseSettings` 仍把 `Version` 常量写在消息体内而非统一 descriptor；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L820-L846 — `HandleMessage()` 仍以巨型 `if/else` 直接承载 request admission 和 message semantics；同文件 L1499-L1515 — `SendDebugDatabase()` 仍先发 settings，再把 symbol 数据拼成 JSON 字符串发出，协议 sequencing 仍散落在业务代码里。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/V2/AngelscriptDebugProtocolDescriptor.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/V2/AngelscriptDebugProtocolDescriptor.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/V2/AngelscriptDebugProtocolSchema.json`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/V2/AngelscriptDebugProtocolCodec.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/V2/AngelscriptDebugProtocolCodec.cpp`
- [ ] **P21.1** 📦 Git 提交：`[Debug/Protocol] Refactor: drive V2 wire contract from descriptor and codec`
- [ ] **P21.1-T** 单元测试：验证 descriptor/codec 成为 `V2` 唯一 wire 事实源，并冻结 mixed-version 与 `RequestDebugDatabase` 序列
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerProtocolDescriptorTests.cpp`
  - 测试场景：
    - 正常路径：`StartDebugging`、`Variables`、`DebugDatabaseSettings`、`AssetDatabase` 与 `RequestDebugDatabase` 的当前 `DEBUG_SERVER_VERSION` payload 都能由 descriptor/codec 生成稳定 golden bytes；真实 session 抓到的消息序列与 descriptor 声明一致。
    - 边界条件：legacy empty `RequestDebugDatabase`、`DebugAdapterVersion=1/2/DEBUG_SERVER_VERSION` 与 capability 开关并存时，字段会按 `optionalSince` / `requiresCapability` 正式降级，而不是继续靠 `Ar.AtEnd()` 猜测。
    - 错误路径：truncated body、unknown optional field、unknown capability 或 descriptor 不允许的字段组合，会在 codec 层返回明确 reject/error，不会先改动 runtime state 后再靠 silent drift 暴露。
  - 测试命名：`Angelscript.TestModule.Debugger.Protocol.DescriptorGoldenCompatibility`；`Angelscript.TestModule.Debugger.Protocol.RequestDebugDatabaseFollowsDescriptorSequence`
  - 隔离方式：`FAngelscriptEngineScope` + `FAngelscriptDebuggerTestSession` + pending-queue message collector
- [ ] **P21.1-T** 📦 Git 提交：`[Test/Debug] Test: add protocol descriptor golden and mixed-version coverage`

- [ ] **P21.2** 将 `PrecompiledData` 的 rich symbol 提升为独立 `DebugSymbols` sidecar，避免 minimized runtime / optimized lane 抹掉只读调试与定位事实
  - 现有 `P2.1`、`P11.1`、`P14.1` 已分别打通 JIT stop pipeline、source filename 与 top-frame 变量可见性，但它们都默认 rich symbol 还在当前运行时里。现实上，`PrecompiledData` 虽然在缓存中保存了参数名、默认值、`VariableInfo`、`DeclaredAt` 与 `LineNumbers`，可一旦走 `bMinimizeMemoryUsage + ClearUnneededRuntimeData()`，这些对只读调试、source lookup、fallback 诊断最关键的事实就会被有条件跳过回填，随后再从 runtime 内存中清空。
  - 本项不重复 `P13.1` 的 cache 有效性、`P16.2` 的 deterministic output plan，也不替代未来更大的 symbol graph；目标是先在 `StaticJIT/PrecompiledData` 这条链上补一个最小、可 join 的只读 artifact：在同一轮 `PrecompiledScript.Cache` 生成时附带写出 `DebugSymbols` sidecar，以 `DataGuid + FunctionId` 为主键，记录 signature、parameter names/defaults、`DeclaredAt`、`LineNumbers`、`VariableInfo*` 与 source section identity；runtime 继续可以清理主缓存里的 heavyweight symbol，但需要保留一个轻量 loader/API 供只读 `DebugServer`、fallback 定位、crash symbolizer 或未来离线工具查询。
  - 这一步的完成标准不是“马上重建 line cue”或“立刻让 optimized build 支持完整 locals 调试”，而是先把今天明明已经采集过、却在启动后被抹掉的事实保存成正式 sidecar。只有 sidecar 先存在，后续 `P2.1/P14.1` 才有稳定 join key 去补 optimized lane 的定位、诊断和只读查询。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 53 — “precompiled/JIT 产物在生成期关闭了 line cue，JIT miss 后的 VM fallback 也无法提供断点和步进”
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 67 — “现有 `StaticJIT` 自动化全部运行在 `EditorContext`，真实 JIT 执行与调试元数据没有回归覆盖”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` Issue-18 — “现有 `StaticJIT` 直连测试全部跑在 `EditorContext`，不会执行任何 transpiled code，也不会验证 JIT debug metadata”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT41` — “`PrecompiledData` 已携带 rich symbol，但 optimized lane 在启动后主动抹除它们，缺少独立 `DebugSymbols` 交付物”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h` L79-L103 — `FAngelscriptPrecompiledFunction` 仍保存 `ParameterNames`、`ParameterDefaultArgs`、`VariableInfoProgramPos`、`DeclaredAt` 与 `LineNumbers`；同文件 L583-L618 — `FAngelscriptPrecompiledData` 只提供 `Save()/Load()/ClearUnneededRuntimeData()`，并明确注释 `bMinimizeMemoryUsage` “removes some debugging information”；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` L515-L521 — `Create()` 在 minimized 路径仍跳过 `parameterNames/defaultArgs` 回填；同文件 L573-L579 — 运行时重建函数时只恢复 `declaredAt/lineNumbers` 到 `scriptData`；同文件 L2481-L2548 — `ClearUnneededRuntimeData()` 仍清空 `propertyTable/methodTable/enumValues/defaultArgs/parameterNames`，说明当前没有独立 sidecar owner 去保存这些只读事实。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/DebugSymbols/AngelscriptPrecompiledDebugSymbols.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/DebugSymbols/AngelscriptPrecompiledDebugSymbols.cpp`
- [ ] **P21.2** 📦 Git 提交：`[StaticJIT/DebugSymbols] Feat: persist minimized precompiled debug symbol sidecar`
- [ ] **P21.2-T** 单元测试：验证 `DebugSymbols` sidecar 能在 minimized runtime 后继续提供只读函数符号，不再依赖常驻 `PrecompiledData`
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDebugSymbolsTests.cpp`
  - 测试场景：
    - 正常路径：生成一轮 `PrecompiledScript.Cache` 与对应 `DebugSymbols` sidecar；将 `FAngelscriptPrecompiledData` 切到 `bMinimizeMemoryUsage=true` 并执行 `ClearUnneededRuntimeData()` 后，仍能按 `DataGuid + FunctionId` 取回 signature、parameter names/defaults、`DeclaredAt` 与 `LineNumbers`。
    - 边界条件：只包含部分 `VariableInfo/LineNumbers` 的函数会生成 sparse record；loader 能显式区分 “字段本来不存在” 与 “sidecar 载入失败”，而不是把缺失数据伪装成空函数。
    - 错误路径：`DataGuid` / build identifier 不匹配、sidecar 缺失或内容损坏时，runtime 明确返回 `DebugSymbolsUnavailable/Rejected`，不会把旧 bundle 的符号误 join 到当前函数，也不会阻塞现有执行路径。
  - 测试命名：`Angelscript.TestModule.StaticJIT.DebugSymbols.SidecarSurvivesMinimizedRuntime`
  - 隔离方式：`FAngelscriptEngineScope` + temporary precompiled cache root + explicit minimize/clear cycle
- [ ] **P21.2-T** 📦 Git 提交：`[Test/StaticJIT] Test: add precompiled debug-symbol sidecar coverage`

### 单元测试总览补充（本轮深化 2026-04-09 07:37:05）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P21.1` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerProtocolDescriptorTests.cpp` | protocol descriptor golden bytes、mixed-version decode、`RequestDebugDatabase` 序列冻结 | P1 |
| `P21.2` | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDebugSymbolsTests.cpp` | minimized runtime 后的 `DebugSymbols` sidecar 查询、sparse record、mismatch reject | P1 |

### 验收标准补充（本轮深化 2026-04-09 07:37:05）

1. `DebugServer V2` 的新增/兼容字段不再分散在多个 message struct 的 inline `operator<<` 与全局 `DebugAdapterVersion` 条件里；`V2` wire contract 至少有一份 machine-readable `ProtocolDescriptor/Schema`，并由 runtime codec 与 golden test 共同守护。
2. `DEBUG_SERVER_VERSION`、legacy adapter version 与 `RequestDebugDatabase` 初始化序列都能通过 descriptor/codec 回归验证；新增字段或 capability 变动时，自动化可以在 payload/sequence 层直接报警，而不是等前端手工发现 drift。
3. optimized / minimized runtime 即使清掉 `PrecompiledData` 中的 heavyweight symbol，也能通过同轮生成的 `DebugSymbols` sidecar 按 `DataGuid + FunctionId` 取回只读函数定位事实；sidecar 缺失或错配时返回显式 unavailable/reject，不得伪造旧符号。

### 风险与注意事项补充（本轮深化 2026-04-09 07:37:05）

#### 风险

1. `P21.1` 若只新增 `ProtocolDescriptor` 文件却让旧 inline `operator<<` 继续做事实源，会形成“双真相”；第一阶段必须明确 descriptor/codec 才是唯一 authority，旧 message struct 只能保留纯数据承载或兼容壳。
2. `P21.2` 的 join key 若只依赖 `FunctionId` 而不绑定 `DataGuid/build identifier`，最容易在多 bundle 或 stale cache 场景误配符号；sidecar 必须坚持从同一轮 `PrecompiledData` 派生，并带显式 reject reason。

#### 已知行为变化

1. 协议错误路径会从“靠 `Ar.AtEnd()` 或旧 helper 静默吞掉 drift”收紧为“descriptor/codec 明确 reject/degrade”；部分以前能被旧客户端糊里糊涂读过去的 payload，未来会变成正式错误。
2. precompiled 产物目录将新增只读 `DebugSymbols` sidecar；它不改变现有脚本执行语义，但后续调试/定位工具应优先读取 sidecar，而不是假设 minimized runtime 仍保留完整 `PrecompiledData` 符号。

---

## 深化 (2026-04-09 07:45:59)

本轮继续只读复核用户指定输入、`Documents/Plans/` 活跃 Plan 与 `StaticJIT/` 源码后，确认现有 `P1-P21` 已分别承接 session、protocol、fallback、安全 sidecar 等 correctness 主线，但还有一组“真实 JIT 执行验证底座”缺口没有被单独固化：当前所有直接命中 `StaticJIT/` 的自动化仍建立在 `WITH_EDITOR` profile 上，而 `StaticJITConfig.h` 在 editor 构建无条件定义 `AS_SKIP_JITTED_CODE`，`AngelscriptStaticJIT.cpp` 输出又整体包在 `#ifndef AS_SKIP_JITTED_CODE` 下。结果是多条已经写进本计划的 `P2.1/P2.2/P5.1/P9.2/P14.1/P21.2` 虽然都要求“真实 transpiled/JIT 路径回归”，但共用的 execution/output harness 仍未作为独立执行项落地。

这不与 `Documents/Plans/Plan_StaticJITUnitTests.md` 重复：后者已明确把首轮范围限定为 `EditorContext` 下可稳定运行的结构/单元回归，不承接 non-`EditorContext` execution harness。本轮只补能支撑本计划现有 runtime correctness 条目的 JIT 执行与产物探针底座。再次核对后，用户列出的 [B] `Documents/AutoPlans/DiscoveryPlans/DebuggingAndJIT_Plan.md` 仍不存在，因此本轮新增条目仅使用 [A]/[C]/[D]/[E] 交叉确认，不据此编造来源。

### 新增 Phase 22：补齐真实 StaticJIT 执行验证底座与生成输出探针

- [ ] **P22.1** 建立 `StaticJITExecutionHarness` / non-`EditorContext` 执行 profile，让本计划里的 JIT correctness 回归真正跑到 transpiled code
  - 当前仓库已经把 `StaticJIT` 作为 plugin-owned second-stage AOT/JIT artifact 维护，但测试面仍停在结构 roundtrip 和 editor profile。只要 `WITH_EDITOR` 成立，`StaticJITConfig.h` 就会定义 `AS_SKIP_JITTED_CODE`，而生成输出本身也被 `#ifndef AS_SKIP_JITTED_CODE` 包裹；这意味着今天即使测试触发了 `WriteOutputCode()` 或 precompiled roundtrip，也无法证明任何 `jitFunction_Raw` 真的被装载并执行过。
  - 本项的目标不是重复 `Plan_StaticJITUnitTests.md` 的结构补测，而是补一个本计划必需的执行底座：新增 `FStaticJITExecutionHarness` 或等价 non-`EditorContext` / offline worker profile，统一负责最小脚本编译、`StaticJIT` 生成、precompiled/JIT 产物装载、`FJITDatabase` 命中检查，以及 “本次到底是 `TranspiledExecuted`、`HybridFallback` 还是 `SkippedByEditorProfile`” 的显式 receipt。只有这层先落地，`P2.1/P2.2/P5.1/P9.2/P14.1/P21.2` 里承诺的“真实 JIT 路径回归”才不会继续停留在计划文本。
  - 实现上先追求“有权威执行 lane”，不追求一次把所有 commandlet / target 体系重构完：若能在当前测试二进制内安全启动非 editor profile，就复用同进程 harness；若编译条件决定 `AS_SKIP_JITTED_CODE` 无法在进程内消失，就必须引入离线 worker / dedicated runtime test lane，并把 `ExecutionReceipt` 做成统一返回面，而不是继续拿 editor 结构测试冒充 execution coverage。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 67 — “现有 `StaticJIT` 自动化全部运行在 `EditorContext`，真实 JIT 执行与调试元数据没有回归覆盖”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` `Issue-18` / `NewTest-3` — “现有直连测试只覆盖 structure roundtrip；需要 `FStaticJITExecutionHarness` 让 `ExecutionMatrix` 真正执行 transpiled code”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT20` — “`StaticJIT` 当前是 AOT transpile 相位，Editor 构建用 `AS_SKIP_JITTED_CODE` 跳过生成代码的编译使用，真实 JIT 运行态与日常 debug/test profile 互斥”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D8]` — “当前 Angelscript 的差异能力之一是 plugin-owned second-stage script AOT/JIT artifact，这条能力需要真实执行面护栏，而不是只停在 artifact 存在性”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h` L8-L10 — `WITH_EDITOR` 下仍无条件 `#define AS_SKIP_JITTED_CODE`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` L99-L100 — 生成输出整体仍包在 `#ifndef AS_SKIP_JITTED_CODE` 之下；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` L3470-L3495 — `WriteOutputCode()` 仍是当前真实 transpiled artifact 生成入口，说明今天缺的不是 codegen 本身，而是能证明它“被装载并执行”的权威测试 lane。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptStaticJITExecutionHarness.h`；新增 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptStaticJITExecutionHarness.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITExecutionTests.cpp`
- [ ] **P22.1** 📦 Git 提交：`[StaticJIT/TestHarness] Feat: add non-editor execution harness for transpiled code`
- [ ] **P22.1-T** 单元测试：验证真实 JIT 执行矩阵能区分 `TranspiledExecuted`、`HybridFallback` 与 `SkippedByEditorProfile`
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITExecutionTests.cpp`
  - 测试场景：
    - 正常路径：最小脚本覆盖算术、脚本函数调用、局部变量与 `UObject` 属性访问；同一入口分别走 VM 与 harness 驱动的 transpiled lane，返回值一致，且 receipt 明确标记 `TranspiledExecuted`、目标函数在 `FJITDatabase` 中存在 entry。
    - 边界条件：其中一个函数故意走 `HybridFallback/InterpreterOnly` 合同，harness 仍能返回显式 mode 与原因，而不是把“没执行 JIT”伪装成通过。
    - 错误路径：如果当前二进制/profile 仍受 `AS_SKIP_JITTED_CODE` 约束，测试必须返回明确的 `SkippedByEditorProfile` / `ExecutionLaneUnavailable` 结果并判失败，不允许继续把结构 roundtrip 误记成 JIT execution coverage。
  - 测试命名：`Angelscript.TestModule.StaticJIT.Codegen.ExecutionMatrix`
  - 隔离方式：`FAngelscriptEngineScope` + dedicated non-`EditorContext` execution lane or offline worker receipt + temporary precompiled/JIT output root
- [ ] **P22.1-T** 📦 Git 提交：`[Test/StaticJIT] Test: add real transpiled execution matrix coverage`

- [ ] **P22.2** 为 `WriteOutputCode()` 增加 in-memory `GeneratedOutputSnapshot` seam，冻结 `SCRIPT_DEBUG_CALLSTACK_*` hook 与 `AS_SKIP_JITTED_CODE` 护栏
  - 现有 `P11.1` 已计划修正 JIT 顶帧 `SourceFilename`，`P16.2` 也计划把输出 identity 变成 deterministic planner，但两者都默认“我们看得见生成输出本身”。今天 `WriteOutputCode()` 仍只有写盘路径，没有可供测试直接读取的 snapshot seam；于是 `SCRIPT_DEBUG_CALLSTACK_FRAME*`、`SCRIPT_DEBUG_CALLSTACK_LINE(...)` 与 `#ifndef AS_SKIP_JITTED_CODE` 这些决定 JIT 调试可见性的关键 artifact，只能靠运行时偶发现象间接证明。
  - 本项先补一条比磁盘文件更稳定的探针：新增 `GenerateStaticJITSourceText(...)` 或等价 `GeneratedOutputSnapshot`，直接返回 header/shared-header/unity text 及其 active compile guards。第一阶段不要求改写整个输出器，只要求把 `WriteOutputCode()` 当前已经拥有的文本事实以 machine-readable 方式暴露出来，让测试能在不依赖磁盘路径、编译缓存和 editor profile 噪声的前提下冻结 “是否写了 debug frame 宏、line hook 与 skip guard”。
  - 这不重复 `P11.1` 的运行时 source identity，也不重复 `P16.2` 的 manifest/deterministic shard；它修的是更靠前一层的 artifact truth source。只有 snapshot seam 先存在，后续关于 `SCRIPT_DEBUG_CALLSTACK_FRAME*`、`SCRIPT_DEBUG_CALLSTACK_LINE(...)`、`SCRIPT_DEBUG_FILENAME` 和 `AS_SKIP_JITTED_CODE` 的变更才能被快速发现，而不是等真实运行态某次调试时才暴露。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 67 — “真实 JIT 执行与调试元数据没有回归覆盖”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` `NewTest-5` — “需要 `GenerateStaticJITSourceText(...)` 之类测试 seam，直接验证生成输出里的 JIT 调试元数据钩子”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT20` — “JIT debug metadata 没有进入统一工具链闭环；Editor 构建与真实 JIT profile 互斥”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D6]` / `[D8]` — “当前仓库已经拥有多类 generated artifact family；JIT output 也应成为可被测试和工具消费的正式 artifact，而不是只存在于写盘副作用里”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` L343-L343 — codegen 仍直接写入 `SCRIPT_DEBUG_CALLSTACK_LINE(...)`；同文件 L373-L380 — 每个函数头仍拼接 `SCRIPT_DEBUG_CALLSTACK_FRAME*` 宏；同文件 L3470-L3495 — `WriteOutputCode()` 仍是面向文件系统的单一输出入口，没有 in-memory snapshot seam；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h` L337-L344 — `SCRIPT_DEBUG_CALLSTACK_FRAME*` / `SCRIPT_DEBUG_CALLSTACK_LINE` 宏在开关关闭时会整体退成空定义，说明这层护栏必须被直接冻结。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/Testing/AngelscriptStaticJITGeneratedOutputSnapshot.h`；新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/Testing/AngelscriptStaticJITGeneratedOutputSnapshot.cpp`；新增 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITGeneratedOutputTests.cpp`
- [ ] **P22.2** 📦 Git 提交：`[StaticJIT/Output] Feat: expose generated-output snapshot seam for debug metadata`
- [ ] **P22.2-T** 单元测试：验证生成输出中的 debug metadata hook 与 skip guard 都有稳定 snapshot
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITGeneratedOutputTests.cpp`
  - 测试场景：
    - 正常路径：为带两处 line marker 的最小脚本生成 snapshot，断言输出同时包含 `SCRIPT_DEBUG_CALLSTACK_FRAME*` 与对应行号的 `SCRIPT_DEBUG_CALLSTACK_LINE(...)`，且函数声明/marker 对齐。
    - 边界条件：在 `AS_SKIP_JITTED_CODE` 或关闭 debug-callstack 宏的 profile 下，snapshot 仍能明确给出 active guard；frame/line hook 会按配置缺失，而不是因为写盘时序或路径问题偶然消失。
    - 错误路径：若函数头缺失 frame 宏、line hook 数量与 marker 不一致，或 guard 丢失导致 editor profile 也看似在生成可执行 JIT 输出，测试会在 snapshot 层直接失败，不再等运行态偶发现象兜底。
  - 测试命名：`Angelscript.TestModule.StaticJIT.GeneratedOutput.DebugMetadataHooks`
  - 隔离方式：`FAngelscriptEngineScope` + in-memory generated-output snapshot + optional temporary output root for parity diff
- [ ] **P22.2-T** 📦 Git 提交：`[Test/StaticJIT] Test: add generated-output snapshot coverage for debug metadata hooks`

### 单元测试总览补充（本轮深化 2026-04-09 07:45:59）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P22.1` | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITExecutionTests.cpp` | real transpiled execution matrix、explicit fallback/skip receipt、VM/JIT parity | P0 |
| `P22.2` | `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITGeneratedOutputTests.cpp` | generated-output snapshot、debug metadata hooks、`AS_SKIP_JITTED_CODE` guard | P1 |

### 验收标准补充（本轮深化 2026-04-09 07:45:59）

1. 仓库内至少存在一条权威 `StaticJITExecutionHarness` 或等价 non-`EditorContext` execution lane，能明确区分 “真正执行了 transpiled code” 与 “只做了 precompiled structure roundtrip / 被 editor profile 跳过”。
2. `StaticJIT` 生成输出至少有一份 machine-readable `GeneratedOutputSnapshot`，`SCRIPT_DEBUG_CALLSTACK_FRAME*`、`SCRIPT_DEBUG_CALLSTACK_LINE(...)` 与 `AS_SKIP_JITTED_CODE` guard 都能在 snapshot 层直接回归，不再只能依赖磁盘副作用或运行态偶发现象。
3. 现有 `StaticJIT` 相关自动化从此必须显式声明自己属于 `structure-only` 还是 `real-execution` lane；本计划中的 JIT correctness 测试不得再把 `EditorContext` 绿灯误报为真实 JIT 执行覆盖。

### 风险与注意事项补充（本轮深化 2026-04-09 07:45:59）

#### 风险

1. `P22.1` 若只是复用同一个 editor 二进制再改 runtime flag，`AS_SKIP_JITTED_CODE` 这个编译期开关不会消失，最后只会得到“看起来更复杂、实际上仍未执行 transpiled code”的假 harness；必须优先保证 execution lane 真能越过预处理守卫。
2. `P22.2` 若 snapshot seam 建在“写盘后的最终文件”而不是 `WriteOutputCode()` 生成阶段，测试仍会被路径、清理、deterministic naming 和外部写盘副作用污染，无法真正冻结 codegen truth source。

#### 已知行为变化

1. `StaticJIT` 自动化将从此明确分成 `structure-only` 与 `real-execution` 两类 lane；旧的 `EditorContext` 绿色结果不再被描述为“JIT 已执行”。
2. JIT 调试元数据回归会更早暴露在 generated-output snapshot 阶段；未来某些改动会在“还没跑到运行时”时就因为 frame/line hook 或 guard 漂移而失败，这是刻意收紧后的正确行为。

---

## 深化 (2026-04-09 07:56:16)

本轮继续只读核对用户指定输入、`Documents/Plans/` 活跃 Plan、现有输出与 `Debugging/` 源码后，确认现有 `P3.1`、`P9.1`、`P14.3` 已分别承接 watchpoint backend 正确性、pending-stop 与 burst stop，但还有一个仍未被单独固化的 residual gap：`native/C++` 写入触发的 data breakpoint 现在仍然只是把 `bBreakNextScriptLine` 置真，真正的 `HasStopped` 仍依赖后续某次 `ProcessScriptLine()`。只要命中发生在最后一次 native 写入、随后没有任何脚本行回调，stop 会直接蒸发。再次核对后，用户给定的 [B] 文件 `Documents/AutoPlans/DiscoveryPlans/DebuggingAndJIT_Plan.md` 在当前工作区仍不存在，因此本轮新增条目只使用 [A]/[D]/[E] 交叉确认，不据此编造来源。

### 新增 Phase 23：补齐 native/C++ 写入 data breakpoint 的 out-of-band 停点兑现

- [ ] **P23.1** 将 `native/C++` 来源的 data breakpoint 从“等下一条脚本行再停”改成 runtime-owned 的 out-of-band pending interrupt，避免没有后续 `ProcessScriptLine()` 时 stop 永久丢失
  - 现有 `P9.1` 已计划把 `Pause` / data breakpoint 的 pending-stop 从 `bBreakNextScriptLine` 中拆出来，但它仍默认“最终会有下一条脚本行来兑现 stop”。对 `native/C++` 写入并非如此：Windows watchpoint handler 当前只缓存 `Context` 并置 `bBreakNextScriptLine=true`，随后必须赌一次后续 `ProcessScriptLine()` 才会真正进入 `PauseExecution()`。如果命中发生在最后一个 native helper 写入、脚本已经返回、或后续根本没有 line callback，这次 data breakpoint 就会静默丢失。
  - 本项不重复 `P3.1` 的寄存器编程与 verified/reject，也不重复 `P14.3` 的 burst coalescing；它补的是“stop materialization lane”本身。最小落地是把 handler 命中的 `Id/Text/Context/thread` 收口成 `PendingExternalStop` 或等价队列，由 runtime-owned safe lane 在 `Tick()` / future pause controller 中显式 drain 并进入一次 `PauseExecution()`。`ProcessScriptLine()` 只继续消费真正来自脚本行的 pending stop；`native/C++` 来源的 watchpoint 不再要求后续脚本行存在。
  - 这一步必须与 `P9.1` 共享同一个 pending-stop authority，而不是再引入第二套并行状态机。完成标准不是“更早停下”，而是“命中 once、materialize once、clear once”：`native/C++` 写入即使后面没有任何脚本语句，也要稳定兑现 1 次 data-breakpoint stop；早到的 `Continue` 不能把它擦掉，后续同轮 `ClearDataBreakpoints` 也不能重复广播。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 84 — “data breakpoint 命中发生在 C++ 写入时不会立即停下，若后续没有脚本行回调则停止事件会彻底丢失”
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 73 — “running 态的 `Continue` 会直接抹掉挂起中的 `pause` / data breakpoint 停止事件”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT14` — “`PauseExecution()` 与停点控制不应继续绑死在嵌套 socket pump 和脚本行回调上，out-of-band stop source 需要独立 pause controller / command lane”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT31` — “watchpoint 应当是调试语义而不是本机后端副作用；后端命中需要有可演进的 trigger/apply/clear 契约”
    - [E] `Documents/AutoPlans/ReferenceComparison/CrossComparison.md` `[D5]` — “当前 AS 的优势是 `Variables/Evaluate -> ValueAddress/ValueSize -> DataBreakpoint` 的一体化 addressable entity model；这条链不能在 native-write 停点兑现阶段断掉”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L597-L598 — 当前仍只有 `bBreakNextScriptLine` 与 `bIsPaused`，没有独立的 out-of-band pending-stop 队列；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L329-L340、L368-L370 — Windows watchpoint handler 命中后仍只缓存 `Context` 并置 `bBreakNextScriptLine=true`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L435-L438 — `Tick()` 仍只做 `ProcessMessages()`，没有任何 external-stop drain；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L479-L545 — data breakpoint 的 `HasStopped/ClearDataBreakpoints` 仍只能在后续 `ProcessScriptLine()` 里 materialize；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L837-L840 — `Continue` 仍会在 running 态直接清掉 `bBreakNextScriptLine`。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；如 `P3.1` 已先拆 backend，则同步修改 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Breakpoints/AngelscriptHardwareDataBreakpointBackend_Windows.h` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Breakpoints/AngelscriptHardwareDataBreakpointBackend_Windows.cpp`
- [ ] **P23.1** 📦 Git 提交：`[Debug/DataBreakpoint] Fix: materialize native watchpoint stops without requiring next script line`
- [ ] **P23.1-T** 单元测试：验证 `native/C++` 写入触发的 data breakpoint 即使后续没有脚本行，也会稳定兑现为一次正式 stop
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointNativeWriteTests.cpp`
  - 测试场景：
    - 正常路径：在 paused session 中通过 `Evaluate/Variables` 获取一个可监视地址，随后恢复执行到一个“最后一步调用 native helper 写值并立即返回”的 fixture；即使 helper 返回后没有下一条脚本行，客户端仍收到 1 条 `HasStopped`，`StopSource/Reason` 属于 data breakpoint，随后只收到 1 条匹配 `Id` 的 `ClearDataBreakpoints`。
    - 边界条件：watchpoint 命中后、`HasStopped` 尚未 materialize 前插入一条 running-state `Continue`；pending native stop 仍会在 runtime-owned drain lane 被兑现，而不是被静默清除。若同一轮命中来自 `Context == nullptr` 的 `native/C++` 写入，顶帧可以是空脚本帧或显式 native marker，但不能直接丢 stop。
    - 错误路径：不存在后续脚本行、脚本 invocation 已结束、或 handler 命中发生在纯 `native/C++` 路径时，服务端也不会把这次 watchpoint 永久遗失；同一命中不会重复 materialize 两次，也不会因为 `Tick()`/`ProcessScriptLine()` 双重消费而重复广播 `HasStopped` / `ClearDataBreakpoints`。
  - 测试命名：`Angelscript.TestModule.Debugger.DataBreakpoint.NativeWriteMaterializesWithoutNextScriptLine`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P23.1-T** 📦 Git 提交：`[Test/Debug] Test: add native-write data-breakpoint materialization coverage`

### 单元测试总览补充（本轮深化 2026-04-09 07:56:16）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P23.1` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointNativeWriteTests.cpp` | native-write/no-next-line stop materialization、early-continue 不丢 stop、single-stop single-clear | P0 |

### 验收标准补充（本轮深化 2026-04-09 07:56:16）

1. `native/C++` 来源的 data breakpoint 不再依赖后续 `ProcessScriptLine()` 才能兑现；即使命中后脚本已返回或再无脚本行，runtime 仍会 materialize 1 次正式 stop。
2. `Continue` 不再能够在 running 态擦除尚未 materialize 的 native watchpoint stop；同一次命中只会产生 1 组 `HasStopped` / `ClearDataBreakpoints`，不会丢失也不会重复。

### 风险与注意事项补充（本轮深化 2026-04-09 07:56:16）

#### 风险

1. `P23.1` 若直接在 `VEH` 或 worker thread 中调用 `PauseExecution()`，最容易引入重入、死锁或与 `ProcessMessages()` 的执行线程冲突；必须坚持“handler 只记录命中，runtime-owned safe lane 负责兑现 stop”的边界。
2. `P23.1` 若不与 `P9.1` / `P14.3` 共享同一 pending-stop 与 coalescing authority，容易把“stop 丢失”修成“同一命中被 `Tick()` 与 `ProcessScriptLine()` 各停一次”的双停点回归。

#### 已知行为变化

1. `native/C++` 写入命中的 watchpoint 将从“有时静默消失”收紧为“即使没有下一条脚本行也会正式停下”；旧前端或测试若默认这类命中会被吞掉，需要改为接收并处理这条 stop。
2. running-state `Continue` 将不再把尚未 materialize 的 watchpoint stop 当成可清除噪声；以前依赖“提前 Continue 帮忙略过 native watchpoint”的错误副作用会消失。

---

## 深化 (2026-04-09 08:04:01)

本轮继续只读核对用户指定输入、`Documents/Plans/` 活跃 Plan、现有输出与 `Debugging/` 源码后，确认现有 `P1.7`、`P10.2`、`P14.2` 已分别承接 session admission、transcript harness 与 thread-scoped stop ownership，但还有一个尚未被单独固化的 stop-lifecycle residual gap：`PauseExecution()` 退出原因没有区分真正的 `Continue/Step*` 恢复与 `StopDebugging` / `Disconnect` / 最后一个 debugging client 断线这类会话终止，因此 paused teardown 仍会被统一补成 `HasContinued`。再次核对后，用户给定的 [B] 文件 `Documents/AutoPlans/DiscoveryPlans/DebuggingAndJIT_Plan.md` 在当前工作区仍不存在，因此本轮新增条目只使用 [A]/[C]/[D] 交叉确认，不据此编造来源。

### 新增 Phase 24：区分真正恢复执行与 paused teardown 的事件语义

- [ ] **P24.1** 让 `PauseExecution()` 只在真实 `Continue/Step*` 恢复时发送 `HasContinued`，`StopDebugging` / `Disconnect` / 会话终止不得再伪装成继续执行
  - 当前 runtime 把“离开 paused loop”错误等同于“目标继续运行”。`PauseExecution()` 在 `while (bIsPaused)` 退出后无条件广播 `HasContinued`；而让循环结束的并不只有 `Continue/Step*`，`StopDebugging` 会直接把 `bIsPaused=false`，最后一个 debugging client 断开时的 cleanup 也会直接把 `bIsPaused=false` 并清断点。结果是前端在 paused 态结束会话时，会先收到一条假的 `continued`，把“session ended”误判成“脚本恢复执行”。
  - 本项不重复 `P1.7` 的 session admission，也不替代 `P10.2` 的 transcript harness；它补的是 runtime 本身的 exit-cause authority。最小落地是为 paused state 增加 `EPauseExitCause` / `FPauseExitContext` 或等价状态，把 `Continue/Step*`、`StopDebugging`、`Disconnect`、`LastDebugClientGone`、`ServerShutdown` 明确区分。`PauseExecution()` 只在 exit cause 属于真实 resume 时发送 `HasContinued`；会话终止路径只做状态清理与 route 收口，不再伪造 resume event。
  - 实现时必须与 `P1.7` 的 active debugging-session 路由和 `P14.2` 的 stop owner/stop queue 共用同一 authority，而不是在 `PauseExecution()` 里再引入一套并行布尔位。完成标准不是“简单 suppress 一切 `HasContinued`”，而是“谁触发 resume，谁得到继续事件；谁终止会话，谁只看到 teardown，不看到 fake continue”。
  - 来源：
    - [A] `Documents/AutoPlans/DebuggingAndJIT_Analysis.md` 发现 32 — “暂停态收到 `StopDebugging` 或断线时仍会补发 `HasContinued`，停止会话被错误伪装成继续执行”
    - [C] `Documents/AutoPlans/TestCoverage/DebuggingAndJIT_TestGaps.md` `NewTest-19` — “当前只验证 `Continue` 应发出 `HasContinued`，没有任何场景区分 resume 与 teardown 的事件语义”；`NewTest-22` / `NewTest-24` — “`Disconnect` / `StopDebugging` 的 paused cleanup 仍缺少事件序列断言”
    - [D] `Documents/AutoPlans/ArchitectureReview/DebugAndToolchain_ArchReview.md` `Arch-DT14` — “`PauseExecution()` 仍是内嵌 socket pump 的 pause controller，pause/command 生命周期耦合过深”；`Arch-DT52` — “需要 transcript 级 contract 固化 `Disconnect during paused state` 等关键时序”
  - 源码验证：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` L597-L614 — 当前 paused state 仍只有 `bIsPaused`，没有任何 exit-cause/termination-reason 状态；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L667-L698 — `PauseExecution()` 在退出 `while (bIsPaused)` 后仍无条件发送 `HasContinued`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L915-L923 — `StopDebugging` 仍直接把 `bIsPaused=false`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L733-L747 — 最后一个 debugging client 断线时 cleanup 仍直接把 `bIsPaused=false` 并清空调试状态；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` L1191-L1194 — `Disconnect` 仍只 `Close()` socket，由后续 cleanup 间接结束 paused loop。
  - 涉及文件：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`；如 `P10.2` 已先落地 transcript seam，则同步更新 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugSessionConformanceTests.cpp`
- [ ] **P24.1** 📦 Git 提交：`[Debug/Lifecycle] Fix: suppress fake continued events on paused teardown`
- [ ] **P24.1-T** 单元测试：验证 paused 态 `StopDebugging` / `Disconnect` 只结束会话，不会伪造 `HasContinued`
  - 测试文件：`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerLifecycleTests.cpp`
  - 测试场景：
    - 正常路径：在断点停住后发送真正的 `Continue`，仍恰好收到 1 条 `HasContinued`，证明本项不会误伤真实 resume 语义。
    - 边界条件：在 paused 态由主 client 发送 `StopDebugging`，或让最后一个 debugging client 直接 `Disconnect`；server 会正确清掉 `bIsDebugging` / `bIsPaused` / 断点状态，但不会发送任何 `HasContinued`。若同进程还有 database-only 或旁路连接，它们也不会把 teardown 误收成 continued。
    - 错误路径：非 owner/旁路 client 断线、paused loop 中的 socket cleanup、或 server shutdown 都不会制造假的 `HasContinued`；同时真实 `Continue` 仍不会被错误 suppress，避免把“修假 continued”修成“合法 continued 丢失”。
  - 测试命名：`Angelscript.TestModule.Debugger.Session.StopDebuggingWhilePausedDoesNotEmitHasContinued`；`Angelscript.TestModule.Debugger.Session.DisconnectWhilePausedDoesNotEmitHasContinued`
  - 隔离方式：`FAngelscriptEngineScope`
- [ ] **P24.1-T** 📦 Git 提交：`[Test/Debug] Test: add paused-teardown event-sequence coverage`

### 单元测试总览补充（本轮深化 2026-04-09 08:04:01）

| 改进项 | 测试文件 | 测试场景 | 优先级 |
|--------|---------|---------|--------|
| `P24.1` | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerLifecycleTests.cpp` | paused `StopDebugging` / `Disconnect` 不再发出 fake `HasContinued`、真实 `Continue` 仍保留 resumed event | P0 |

### 验收标准补充（本轮深化 2026-04-09 08:04:01）

1. `HasContinued` 只代表真实 `Continue/Step*` 恢复，不再被 `StopDebugging`、`Disconnect`、最后一个 debugging client 离开或 server teardown 伪造。
2. paused 态结束会话后，server 会正确清理 `bIsDebugging`、`bIsPaused` 与断点状态，但 transcript 中不会再出现“先 terminate 再 fake continue”的错序。

### 风险与注意事项补充（本轮深化 2026-04-09 08:04:01）

#### 风险

1. `P24.1` 若只在 `PauseExecution()` 尾部加一个临时条件，而不把 exit cause 与 `P1.7` / `P14.2` 共用 authority，最容易出现“旁路 teardown 仍发 continued”或“真实 `Continue` 被错误 suppress”的双向回归。
2. `P24.1` 若把 paused teardown 与 session route 清理顺序处理错，可能把“fake continue”修成“paused loop 永不退出”或“断线后残留 paused/debugging state”；必须用 transcript 级测试同时锁住状态清理和事件序列。

#### 已知行为变化

1. paused 态下 `StopDebugging` / `Disconnect` 将不再额外发送 `HasContinued`；旧前端如果曾错误依赖这条 synthetic event 清 UI，需要改为按会话结束或 disconnect 自身收口状态。
2. `HasContinued` 的语义会从“任何离开 paused loop 都算 continued”收紧为“只有真实 resume command 才算 continued”；这会让 protocol transcript 更接近正式调试语义，也会让历史上被错当成 resumed 的 teardown 序列提前暴露。
