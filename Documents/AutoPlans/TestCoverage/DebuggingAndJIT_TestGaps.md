# DebuggingAndJIT 测试覆盖缺口分析

---

## 测试审查 (2026-04-08 13:09)

### 一、现有测试问题

#### Issue-1：后台 monitor 线程在失败路径没有 join，测试会留下悬空引用与未完成清理

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:166-305, 324-652`；`AngelscriptDebuggerSteppingTests.cpp:172-329, 348-657` |
| 问题描述 | `StartBreakpointMonitor()` / `StartStepMonitor()` 在线程池里捕获 `bMonitorReady`、`bShouldStop` 的引用；各个 `RunTest()` 在 monitor 已启动后，如果 `WaitForInvocationCompletion()`、`StartAndWaitFor*MonitorReady()` 或后续断言失败，很多路径直接 `return false`，并没有先对 `TFuture` 执行 `Get()`/`Wait()`。此时 `ON_SCOPE_EXIT` 只会把 `bMonitorShouldStop` 置真并销毁 session / client / module，后台线程却仍可能继续访问这些栈上 `TAtomic` 和已被 teardown 的调试会话。 |
| 影响 | 失败路径会把“测试失败”升级成“测试后还有线程在跑”的不稳定状态，轻则留下 socket / monitor 线程尾巴，重则出现对已析构局部变量的悬空访问，导致后续用例串扰或偶发崩溃。 |
| 修复建议 | 把 monitor 生命周期收敛成一个 RAII helper，例如 `FScopedDebuggerMonitor`：析构时固定执行 `bShouldStop = true` 并 `Future.Wait()`/`Future.Get()`；或者在每个 `RunTest()` 里对 `TFuture` 增加 `ON_SCOPE_EXIT` 清理，只要 future 已创建就统一收口，不允许失败路径直接返回。 |

#### Issue-2：两个负向断点用例把“没有 stop”当成成功，却没有排除 monitor 超时或握手失败

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume`、`Angelscript.TestModule.Debugger.Breakpoint.IgnoreInactiveBranch` |
| 行号范围 | 436-570，572-652 |
| 问题描述 | `ClearThenResume` 第二次运行和 `IgnoreInactiveBranch` 末尾都只断言 `MonitorResult.StopEnvelopes.Num() == 0`。但 `FBreakpointMonitorResult` 还携带了 `Error` 与 `bTimedOut`，其中 monitor 握手失败、连接失败、后台线程超时都可能同样产生“0 个 stop”。当前这两个负向用例没有对 `Error.IsEmpty()`、`!bTimedOut` 做任何约束，因此 monitor 本身失效时依然可能绿灯。 |
| 影响 | 这类测试不能区分“目标断点确实未命中”和“监控客户端压根没工作”。一旦调试器停止发送 `HasStopped`，或者 monitor 因连接问题未建立，负向用例会错误报告成功，失去回归价值。 |
| 修复建议 | 抽一个统一断言 helper，例如 `AssertMonitorCompletedWithoutStops()`，同时检查 `MonitorResult.Error.IsEmpty()`、`!MonitorResult.bTimedOut` 和 `StopEnvelopes.Num() == 0`；`ClearThenResume` 第二次运行还应附带断言脚本返回值仍为 `8`，避免只验证“没停下”而不验证执行路径。 |

#### Issue-3：`StepOver` 和 `StepOut` 没有验证 stop reason，步进语义退化时仍可能误报通过

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | 456-657 |
| 问题描述 | `StepIn` 会反序列化 `FStoppedMessage` 并断言首个 stop 是 `breakpoint`、第二个 stop 是 `step`；但 `StepOver` / `StepOut` 只看 callstack 行号和栈深，没有反序列化任何 `HasStopped` payload，也不检查 `Reason`。如果调试器把后续 stop 错误地报告成 `breakpoint`、`exception`，甚至重复命中原断点，只要 callstack 恰好落到同一行，这两个测试仍可能通过。 |
| 影响 | 这会放过步进协议层最关键的语义回归：前端依赖 `reason="step"` 来区分用户主动步进和普通断点命中。当前测试只能证明“停在了某一行”，不能证明“这是正确的步进行为”。 |
| 修复建议 | 复用 `StepIn` 的反序列化方式，在 `StepOver` 断言第 1 个 stop 为 `breakpoint`、第 2 个 stop 为 `step`；在 `StepOut` 断言第 1 个 stop 为 `breakpoint`，第 2/3 个 stop 都是 `step`，并同时校验 `Description`/`Text` 为空或符合预期，避免异常路径冒充步进。 |

#### Issue-4：`FAngelscriptDebuggerTestClient::Connect` 没有真正处理连接超时，Shared 基础设施会把 `EINPROGRESS` 当成成功

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | 32-73 |
| 问题描述 | `Connect()` 把 socket 设为 non-blocking 后，只要 `FSocket::Connect()` 返回 `SE_EWOULDBLOCK` 或 `SE_EINPROGRESS` 就立即返回 `true`，既不轮询 `GetConnectionState()`，也没有超时窗口。随后上层测试把这个返回值当成“已连接”，再进入 `SendStartDebugging()` / `ReceiveEnvelope()`。这意味着连接尚未建立、连接永远不会建立、或端口未监听时，错误会被延后成握手超时，而不是明确的 connect timeout。 |
| 影响 | Shared 调试器基础设施无法稳定地区分“连接阶段失败”和“协议阶段失败”，会把环境抖动包装成模糊的 handshake 问题，增加用例偶发失败和诊断成本。 |
| 修复建议 | 给 `Connect()` 增加显式连接超时，例如轮询 `Socket->GetConnectionState()` 到 `SCS_Connected`，或用 `Wait(ESocketWaitConditions::WaitForWrite, Timeout)` 再读取 socket error；超时后应关闭 socket 并返回包含 host/port/timeout 的错误信息。把这个 timeout 暴露到 `FAngelscriptDebuggerSessionConfig`，便于 smoke / breakpoint / stepping 测试统一配置。 |

### 二、需要新增的测试

#### NewTest-53：`WaitForMessageType()` 必须保留跳过的 envelope，不能把前序协议消息静默吞掉

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h` |
| 关联函数 | `FAngelscriptDebuggerTestClient::WaitForMessageType()`、`WaitForTypedMessage()`、`ReceiveEnvelope()` |
| 现有测试覆盖 | 当前 7 个 debugger 场景没有直接覆盖这些 helper；而文档里已记录的 `WaitForBreakpointAck(...)`、`BreakFiltersRoundtrip`、`DebugDatabase` 等后续测试建议，都准备复用 `WaitForTypedMessage()`。换句话说，Shared helper 的“保留非目标消息”语义目前是 0 自动化护栏。 |
| 风险评估 | 如果 `WaitForMessageType()` 继续吞掉非目标消息，后续所有依赖它等待 `SetBreakpoint` ack、`BreakFilters`、`AssetDatabase*`、`ClearDataBreakpoints` 的测试都会把先到达的 `HasStopped`/`Diagnostics`/其它响应静默丢掉，最终得到既不稳定也不可信的绿灯。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Shared.WaitForMessageTypePreservesSkippedMessages` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerClientHelperTests.cpp`，与其它 Shared helper 回归一起放 2-3 个用例，整文件控制在 300-500 行 |
| 场景描述 | 给 `FAngelscriptDebuggerTestClient` 增加 test-only fake socket / transport seam。预装两条按顺序到达的 envelope：先是 `HasStopped`，后是目标消息 `BreakFilters`。测试先调用 `WaitForMessageType(EDebugMessageType::BreakFilters, Timeout)` 拿到目标 envelope，再立即调用 `ReceiveEnvelope()` 或 `WaitForMessageType(EDebugMessageType::HasStopped, ...)` 读取之前被跳过的 `HasStopped`。 |
| 输入/前置 | 复用 `NewTest-34` 已要求的 `IDebuggerTestSocketFactory` / `FFakeDebuggerClientSocket` 注入点；用 `SerializeDebugMessageEnvelope(...)` 生成确定性的 `HasStopped` 与 `BreakFilters` payload。若最终采用 `PendingEnvelopes` 队列修复方案，测试应直接断言 fake socket 只被读取到“目标消息为止”，剩余跳过消息从 queue 回放，而不是要求 socket 再次重放同一帧。 |
| 期望行为 | 1. `WaitForMessageType(BreakFilters, ...)` 返回的必须是目标 `BreakFilters` envelope。 2. 先到达的 `HasStopped` 不得丢失；下一次读取仍能按原顺序得到它，且 payload `Reason == "breakpoint"`。 3. 整个过程中 `Client.GetLastError().IsEmpty()`。 4. 两条消息都只消费一次，不允许重复回放或顺序颠倒。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerTestClient`、新增 `IDebuggerTestSocketFactory` / `FFakeDebuggerClientSocket`、`SerializeDebugMessageEnvelope(...)` |
| 优先级 | P2 |

本轮先记录现有测试质量问题，新增测试建议将在后续分段追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-1 |
| WeakAssertion | 2 | Issue-2 |
| FlakyRisk | 1 | Issue-4 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 01:13)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮新增 `NewTest-52`，正文已记录于本文 `## 测试审查 (2026-04-10 01:11)` 段落；由于该文档历史上已存在段落顺序错位，本轮在尾部补记索引，后续继续审查时请按 `NewTest-52` 编号检索，不要只从文件尾部向上判断最新新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 01:25)

### 一、现有测试问题

本轮新增 `Issue-51`，正文已记录于本文 `## 测试审查 (2026-04-10 01:22)` 段落；由于该文档历史上已存在段落顺序错位，本轮在文件尾部补记索引，后续继续审查时请按 `Issue-51` 编号检索，不要只从文件尾部向上判断最新现有问题。

### 二、需要新增的测试

本轮新增 `NewTest-53`，正文已记录于本文 `## 测试审查 (2026-04-10 01:22)` 段落；由于该文档历史上已存在段落顺序错位，本轮在文件尾部补记索引，后续继续审查时请按 `NewTest-53` 编号检索，不要只从文件尾部向上判断最新测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-51 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 01:22)

### 一、现有测试问题

#### Issue-51：`PumpUntil` / `WaitForMessage*` 的 `Sleep(0.0f)` 忙轮询会放大共享 editor 调试用例的时序抖动

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerTestSession.cpp:102-145`；`AngelscriptDebuggerTestClient.cpp:160-208` |
| 问题描述 | `FAngelscriptDebuggerTestSession::PumpOneTick()` 在每次 `DebugServer->Tick()` 后都执行 `FPlatformProcess::Sleep(0.0f)`，`PumpUntil()` 则在整个 timeout 窗口里持续调用它。`FAngelscriptDebuggerTestClient::WaitForMessage()` / `WaitForMessageType()` 也使用同样的 `Sleep(0.0f)` 零延迟轮询。对当前 debugger 测试来说，这意味着主线程 pump、monitor 线程收包和未来复用 `WaitForMessage*` 的 helper 都在共享 editor 进程里做高频 busy-spin。只要 CI 或本地 editor 正在承压，这些零睡眠循环就会把 CPU 抢占和调度抖动放大成握手、stop、callstack 等阶段的偶发 timeout。 |
| 影响 | 当前 7 个用例虽然大多还在 45 秒窗口内能通过，但它们的稳定性被绑定到了机器负载和线程调度运气上，而不是协议本身。最坏情况下，shared production engine 的 GameThread、background monitor 和 socket poll 互相争抢时间片，会把原本可恢复的短暂延迟放大成假超时，增加“本地过、CI 偶发红”的噪声。 |
| 修复建议 | 不要继续使用 `Sleep(0.0f)` 忙轮询。1. 给 `FAngelscriptDebuggerTestSession` 和 `FAngelscriptDebuggerTestClient` 提取统一的 poll interval，例如 `0.001f` 到 `0.005f`，并在 timeout 长循环里使用它。2. `WaitForMessage()` / `WaitForMessageType()` 更稳妥的做法是改用 `Socket->Wait(...)` 或等价可读事件，再进入 `ReceiveEnvelope()`。3. 若保留轮询，至少把 poll interval 暴露到 `FAngelscriptDebuggerSessionConfig`，让 smoke / breakpoint / stepping / future transport tests 使用同一套节流参数，而不是在多个 helper 里复制裸 `Sleep(0.0f)`。 |

### 二、需要新增的测试

---

## 测试审查 (2026-04-10 01:11)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-52：`BlueprintEvent/BlueprintOverride` 的 `__Evt_Execute` 必须在 StaticJIT 下走 `ProcessEvent` custom-call，并保持 Blueprint override 结果一致

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintEvent.cpp` |
| 关联函数 | `FScriptFunctionNativeForm::BindEventFunctionExecute()`、`GenerateCustomCall_EventFunctionExecute(...)`、`__Evt_Execute(const UObject Object, const FName& Name)` |
| 现有测试覆盖 | 现有仓库里虽然有 `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintSubclassRuntimeTests.cpp` 这类 runtime 场景去验证 Blueprint child / `ProcessEvent` 基本行为，也已经在本文记录了 `NewTest-49`、`NewTest-50` 去补 delegate/multicast 的 StaticJIT custom-call，但对 `StaticJITBinds.cpp:998-1025` 注册的 `FScriptNativeEventFunctionExecute` 和 `AngelscriptBytecodes.cpp:2369-2379` 的 `GenerateCustomCall_EventFunctionExecute` 仍然是 0 场景覆盖。换句话说，`BlueprintEvent/BlueprintOverride` 这条最基础的 `ProcessEvent` JIT 桥接目前完全没有自动化护栏。 |
| 风险评估 | 一旦这条 custom-call 回归，JIT 路径就可能直接绕过 Blueprint override、找错 `UFunction` 名称、或把 `l_ParmStruct` 传给错误的 `ProcessEvent` 目标。真实项目里会表现成“VM 下 BlueprintEvent 正常，JIT 下只跑默认实现/根本不进 Blueprint override/参数错位”，属于高频且隐蔽的行为分叉。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.BlueprintEvent.ProcessEventCustomCall` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITBlueprintEventTests.cpp`，同文件集中放 `BlueprintEvent` custom-call 1-2 个用例，整文件控制在 300-500 行 |
| 场景描述 | 构造一个最小 script parent actor：`UFUNCTION(BlueprintEvent) int ComputeScore(int Input) { return Input + 10; }`，再提供 `UFUNCTION() int CallComputeScore(int Input) { return ComputeScore(Input); }`。测试侧复用 `Blueprint/AngelscriptBlueprintSubclassRuntimeTests.cpp` 的 transient Blueprint child 创建模式，为 Blueprint child 动态 override `ComputeScore`，让它返回 `Input + 100`。然后在同一个 parent script 上分别跑 VM baseline 和真正的 StaticJIT execution harness，对 Blueprint child 实例调用 `CallComputeScore(7)`。 |
| 输入/前置 | 复用 `FStaticJITExecutionHarness`、`FActorTestSpawner`、`SpawnScriptActor(...)`、`BlueprintSubclassRuntimeTest::CreateTransientBlueprintChild(...)`，并在 Shared 新增一个小型 helper，例如 `CreateTransientBlueprintIntReturnOverride(Blueprint, FunctionName, AddedValue)`，负责给 Blueprint child 挂一个最小 graph/override，让 `ComputeScore(7)` 返回 `107`。测试必须运行在非 `WITH_EDITOR + AS_SKIP_JITTED_CODE` 的真实 JIT harness 上，而不是当前 `EditorContext` 的 precompiled-structure 用例。 |
| 期望行为 | 1. VM baseline 与 JIT 路径都返回 `107`，明确证明调用结果来自 Blueprint child override，而不是 parent script 的默认 `17`。 2. JIT 路径具备有效 `CallComputeScore` entry，不允许静默退回解释执行。 3. 若 harness 支持导出生成源码，`CallComputeScore` 或其 wrapper 的输出里必须出现 `FindFunctionChecked` 与 `ProcessEvent(EventFunction, &l_ParmStruct[0])`，锁住 `GenerateCustomCall_EventFunctionExecute(...)` 的 custom-call 文本。 4. 整个过程中不出现异常、空 `UFunction`、或 Blueprint override 丢失。 |
| 使用的 Helper | `ASTEST_*` 宏、`FStaticJITExecutionHarness`、`FActorTestSpawner`、`SpawnScriptActor(...)`、`BlueprintSubclassRuntimeTest::CreateTransientBlueprintChild(...)`、新增 `CreateTransientBlueprintIntReturnOverride(...)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:59)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮新增 `NewTest-51`，正文已记录于本文较前位置；由于该文档历史上已存在段落顺序错位，本轮在尾部补记索引，后续继续审查时请按 `NewTest-51` 编号检索，不要只从文件尾部向上判断最新新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:57)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-51：`check(false, ...)` 必须每次都进入 debugger stop，不能被误做成 `ensure` 式去重或直接掉进 native break

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `ASDebugBreak()`、`FAngelscriptBinds::BindGlobalFunction("void check(bool Condition, const FString& Message)", ...)`、`FAngelscriptEngine::TryBreakpointAngelscriptDebugging()` |
| 现有测试覆盖 | 现有 7 个 debugger 用例完全不使用 `CreateBindingFixture()`；已记录的 `NewTest-8` 只覆盖 `DebugBreak()` 与 `ensure(false, ...)`，还没有任何场景把 `TriggerCheck(false, Message)` 推进到 debugger stop。也就是说，`check()` 这条显式错误路径仍然是 0 集成覆盖。 |
| 风险评估 | 如果 `check(false)` 被错误实现成“像 ensure 一样同位置只停一次”、直接落到 native `UE_DEBUG_BREAK()`、或根本不发 `HasStopped`，当前套件不会报警。真实脚本作者会直接遇到“check 不进脚本调试器”“第二次 check 不再停”“IDE 无法继续执行”这类高风险回归。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Binding.CheckBreaksEveryInvocation` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBindingTests.cpp`，与 `NewTest-8` 共用 `CreateBindingFixture()` 与 generated-function dispatch helper，整文件保持在 300-500 行 |
| 场景描述 | 使用 `CreateBindingFixture()` 编译生成 `UDebuggerBindingFixture`。在同一 debugging session 内分两轮调用 `TriggerCheck(false, "CheckA")` 与 `TriggerCheck(false, "CheckB")`；每轮都由 monitor 记录完整协议消息，在命中 stop 后请求 callstack 并发送 `Continue`，然后等待 invocation 收口。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerScriptFixture::CreateBindingFixture`、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`DispatchGeneratedVoidInvocation(...)`；monitor 复用 `NewTest-19` 已要求的 transcript/`HasContinued` 采集 helper，确保能区分“真的再次停下”和“被 helper 提前 `StopDebugging` 掩盖”。 |
| 期望行为 | 1. 第 1 次 `TriggerCheck(false, "CheckA")` 产生恰好 1 个 `HasStopped`，`Reason == "breakpoint"`，callstack 顶帧 `Source.EndsWith(Fixture.Filename)` 且 `LineNumber == Fixture.GetLine(TEXT("BindingCheckLine"))`。 2. 发送 `Continue` 后收到 1 条 `HasContinued`，首次 invocation 正常完成，不出现额外 stop。 3. 第 2 次对同一 `BindingCheckLine` 再次调用 `TriggerCheck(false, "CheckB")` 时，仍然再次产生 1 个 `HasStopped`；不得像 `ensure` 那样被位置去重吞掉。 4. 两轮 monitor 都要求 `Error.IsEmpty()`、`!bTimedOut`，从而锁住“每次都进脚本调试 stop，而不是掉进 native break 或超时”。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerScriptFixture::CreateBindingFixture`、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`DispatchGeneratedVoidInvocation(...)`、`CollectProtocolMessagesUntil(...)` 或等价 transcript helper |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:18)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-44：同一个 debugger client 必须能独立完成 `SetBreakpoint -> HasStopped -> CallStack -> Continue` 闭环

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp` |
| 关联函数 | `FAngelscriptDebugServer::PauseExecution()`、`SendCallStack()`、`HandleMessage()` 的 `SetBreakpoint` / `RequestCallStack` / `Continue` 分支、`FAngelscriptDebuggerTestClient::WaitForMessageType()` / `ReceiveEnvelope()` |
| 现有测试覆盖 | 当前 3 个 breakpoint 用例都会在主 client 之外再启动一个 monitor client；主 client 只发 `SetBreakpoint/ClearBreakpoints`，从不自己消费 `HasStopped`、`CallStack` 或 `HasContinued`。仓库里没有任何 debugger 场景验证“同一 socket 既设置断点，又在自己的连接上拿到 stop/stack 并继续执行”。 |
| 风险评估 | 如果 runtime 只把 `HasStopped` 广播给后接入 client、`CallStack` 回复只在 secondary client 上稳定、或者同一 socket 的 `Continue` 状态机有问题，现有 breakpoint 套件仍可能全绿；真实 IDE 却会直接卡在“本连接能设断点但停下后拿不到栈/继续不了”。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.SingleClient.BreakpointRoundtrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSingleClientTests.cpp`，在同一文件内放 2 个单连接场景用例，整文件控制在 300-500 行 |
| 场景描述 | 复用 `CreateBreakpointFixture()`，但不再创建第二个 monitor socket。改为在 Shared 增加一个 `RunSingleClientDebuggerWorker(...)` helper：后台线程独占同一个 `FAngelscriptDebuggerTestClient`，完成 `StartDebugging(2)`、等待 `DebugServerVersion`、等待 `HasStopped`、发送 `RequestCallStack`、等待 `CallStack`、发送 `Continue`、等待 `HasContinued`。主线程只负责 `Session.PumpUntil(...)` 与 `DispatchModuleInvocation(...)`。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture()`、`DispatchModuleInvocation(...)`；在 Shared 新增 `FSingleClientDebuggerTranscript` / `RunSingleClientDebuggerWorker(...)`，内部要保留消息顺序与 `LastError`，并在析构时统一执行 `SendStopDebugging()`、`SendDisconnect()`、`Disconnect()`。 |
| 期望行为 | 1. 同一个 client 成功完成 `StartDebugging` 握手，并收到 `DebugServerVersion == DEBUG_SERVER_VERSION`。 2. 该 client 自己收到恰好 1 个 `HasStopped`，反序列化后 `Reason == "breakpoint"`。 3. 同一个 client 请求并收到 1 个 `CallStack`，顶帧 `Source.EndsWith(Fixture.Filename)`，`LineNumber == Fixture.GetLine(TEXT("BreakpointHelperLine"))`。 4. 同一个 client 发送 `Continue` 后收到 1 个 `HasContinued`，invocation 成功完成且 `Result == 8`。 5. 整个过程中不允许出现第 2 个 `HasStopped` 或来自第二个 socket 的辅助握手。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture()`、`DispatchModuleInvocation(...)`、新增 `RunSingleClientDebuggerWorker(...)` / `FSingleClientDebuggerTranscript` |
| 优先级 | P1 |

#### NewTest-45：同一个 debugger client 必须能独立完成 `breakpoint -> StepIn -> Continue` 的步进闭环

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp` |
| 关联函数 | `FAngelscriptDebugServer::PauseExecution()`、`HandleMessage()` 的 `StepIn` / `Continue` / `RequestCallStack` 分支、`SendCallStack()` |
| 现有测试覆盖 | 当前 3 个 stepping 用例都把 `HasStopped`、`RequestCallStack`、`StepIn/Over/Out` 和最终 `Continue` 交给 `StartStepMonitor()` 的第二个 client。仓库里没有任何用例验证“发起断点的同一个 client 自己完成 step 命令并收到后续 stop”。 |
| 风险评估 | 如果 per-client step 状态只在 secondary client 上正常，或者 primary client 发 `StepIn` 时不能驱动 `ConditionBreakFrame/Function` 的正确流转，现有 StepIn/Over/Out 仍可能通过；真实前端却会直接表现成“同一连接能停下，但点击 StepIn 没反应或回不到预期栈帧”。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.SingleClient.StepInRoundtrip` |
| 测试类型 | Scenario |
| 测试文件 | 追加到新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSingleClientTests.cpp`，与 `NewTest-44` 共用单连接 worker/helper，保持单文件 300-500 行 |
| 场景描述 | 复用 `CreateSteppingFixture()`。同一个后台 worker client 先 `StartDebugging(2)`，在 `StepCallLine` 等到第 1 个 `HasStopped` 后请求 `CallStack`，然后在同一 socket 上发送 `StepIn`；等待第 2 个 `HasStopped` 后再次请求 `CallStack`，确认进入 `Inner()`；最后仍由同一 client 发送 `Continue`，直到 invocation 完成。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateSteppingFixture()`、`DispatchSteppingModuleInvocation(...)`；Shared 继续使用 `RunSingleClientDebuggerWorker(...)`，但 phase 配置要支持同一 socket 顺序执行 `WaitForStop -> RequestCallStack -> StepIn -> WaitForStop -> RequestCallStack -> Continue`。 |
| 期望行为 | 1. 第 1 个 stop 由同一个 client 收到，`Reason == "breakpoint"`，其 `CallStack` 顶帧 `LineNumber == Fixture.GetLine(TEXT("StepCallLine"))`。 2. 同一 client 发送 `StepIn` 后收到第 2 个 stop，`Reason == "step"`，其 `CallStack` 顶帧 `LineNumber == Fixture.GetLine(TEXT("StepInnerEntryLine"))`，且 `Frames.Num() == 2`。 3. 同一 client 再发送 `Continue` 后收到 1 个 `HasContinued`，invocation 成功完成且 `Result == 14`。 4. 整个过程中不允许依赖第二个 client，也不允许出现额外的第 3 个 `HasStopped`。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateSteppingFixture()`、`DispatchSteppingModuleInvocation(...)`、新增 `RunSingleClientDebuggerWorker(...)` / `FSingleClientDebuggerTranscript` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 2 | MissingScenario: 2 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:34)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `NewTest-46`、`NewTest-47`、`NewTest-48`，正文已记录于本文 `## 测试审查 (2026-04-10 00:29)` 段落；由于该文档历史上已存在段落顺序错位，继续审查时请按这些编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:33)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `NewTest-46`、`NewTest-47`、`NewTest-48`，正文已记录于本文 `## 测试审查 (2026-04-10 00:29)` 段落；由于该文档历史上已存在段落顺序错位，后续继续审查时请按这些编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |


---

## 测试审查 (2026-04-09 23:52)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-42：第二个 debugger client 在已有断点之后再发送 `StartDebugging` 时，不得清空首个 client 的 breakpoint state

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `StartDebugging` 分支、`ClearAllBreakpoints()`、`ClientsThatAreDebugging` 相关状态维护 |
| 现有测试覆盖 | 当前 7 个 debugger 用例虽然都会引入第二个 monitor client，但顺序都是“第二个 client 完成握手后，主 client 才设置 breakpoint”。也就是说，现有套件完全没有覆盖“首个 client 已经注册断点，然后第二个 client 再后接入并发送 `StartDebugging`”这个多客户端场景。 |
| 风险评估 | 如果后接入 client 的 `StartDebugging` 会重置全局 breakpoint/break option 状态，首个 IDE 已设置的断点会被静默清掉。当前 breakpoint/stepping 套件因为握手顺序刻意避开了这条路径，所以会给出假安全感；真实用户在 IDE 重连、第二个前端附加、或测试 monitor 改变连接顺序时会直接遇到“断点突然失效”。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Protocol.SecondClientStartPreservesBreakpoints` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerMultiClientTests.cpp`，使用 `ASTEST_*` 宏组织 1-2 个多客户端协议用例，整文件控制在 300-500 行 |
| 场景描述 | 使用 `CreateBreakpointFixture()`。主 client 先完成严格握手，在 `BreakpointHelperLine` 设置断点并确认 `BreakpointCount == 1`。随后第二个 client 连接到同一 `FAngelscriptDebuggerTestSession`，再发送一次 `StartDebugging` 并完成 `DebugServerVersion` 握手。握手完成后，不重新设置任何断点，直接运行 `RunScenario()`；由主 client 接收 `HasStopped`、请求 `CallStack`，再发送 `Continue` 让 invocation 完成。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture()`、`DispatchModuleInvocation(...)`、`WaitForBreakpointCount(...)`；在 Shared 增加一个严格的 `StartAdditionalDebuggerClient(Session, Client, AdapterVersion)` helper，统一处理第二个 client 的 connect + handshake + `DebugServerVersion` 断言，避免再次复制宽松握手逻辑。 |
| 期望行为 | 1. 第二个 client 完成握手后，`Session.GetDebugServer().BreakpointCount` 仍然等于 `1`，且 fixture 对应的 breakpoint key 仍存在。 2. 运行 `RunScenario()` 时，主 client 仍能收到 1 个 `HasStopped`，`Reason == "breakpoint"`，顶帧 `Source` 以 `Fixture.Filename` 结尾，`LineNumber == Fixture.GetLine(TEXT("BreakpointHelperLine"))`。 3. `Continue` 后 invocation 成功完成，`Result == 8`。 4. 在整个过程中，第二个 client 的接入不得要求重新发送 `SetBreakpoint`，也不得让 `BreakpointCount` 短暂掉到 `0`。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture()`、`DispatchModuleInvocation(...)`、`WaitForBreakpointCount(...)`、新增 `StartAdditionalDebuggerClient(...)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |



---

## 测试审查 (2026-04-09 23:54)

### 一、现有测试问题

本轮新增 `Issue-46`，正文已记录于本文 `## 测试审查 (2026-04-09 23:51)` 段落；该条目覆盖“启动 helper 失败早退、清理尚未安装就泄漏共享 debug state”的问题。

### 二、需要新增的测试

本轮新增 `NewTest-42`，正文已记录于本文 `## 测试审查 (2026-04-09 23:52)` 段落；该条目覆盖“第二个 debugger client 后接入时不得清空首个 client 已注册断点”的多客户端缺口。

尾部索引说明：由于该文档历史上已存在段落顺序错位，本轮再次在文件末尾补记索引。继续审查时请按 `Issue-46`、`NewTest-42` 编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-46 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 23:53)

### 一、现有测试问题

本轮新增 `Issue-46`，正文已记录于本文 `## 测试审查 (2026-04-09 23:51)` 段落；该条目覆盖“启动 helper 失败早退、清理尚未安装就泄漏共享 debug state”的问题。

### 二、需要新增的测试

本轮新增 `NewTest-42`，正文已记录于本文 `## 测试审查 (2026-04-09 23:52)` 段落；该条目覆盖“第二个 debugger client 后接入时不得清空首个 client 已注册断点”的多客户端缺口。

尾部索引说明：由于该文档历史上已存在段落顺序错位，本轮在尾部补记索引。继续审查时请按 `Issue-46`、`NewTest-42` 编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-46 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 23:29)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-40：debugger fixture identity 必须在同一 engine 内保持唯一，避免同名旧模块劫持后续用例

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` |
| 关联函数 | `FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture()`、`CreateSteppingFixture()`、`Compile()`、`ExecuteIntFunction()` |
| 现有测试覆盖 | 完全无直接测试。当前文档虽然已有 `NewTest-21` 之类需要“命名 fixture”的场景建议，但没有任何 Shared 级回归直接锁住“同一 engine 下创建多个 debugger fixture 时，`ModuleName/Filename` 不得互相别名”这条 contract。 |
| 风险评估 | 如果 fixture 继续使用固定 identity，或未来补的 `Suffix/GUID` 逻辑回归，现有 debugger 套件会继续对同名旧模块和旧 breakpoint store 敏感，形成顺序相关假绿；而这种问题通常只会在更大的 breakpoint/stepping 场景里以难定位的方式暴露。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Shared.FixtureIdentity.IsolatedPerInstance` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixtureTests.cpp`，文件集中放 fixture identity / marker parser 相关 2-3 个 `ASTEST_*` 用例，控制在 300-500 行 |
| 场景描述 | 在一个 fresh clone/shared test engine 内，构造两个 breakpoint fixture 实例：A 和 B。两者使用不同的 `ModuleName/Filename` 后缀，并把脚本常量做最小差异化，例如 A 保持 `StoredValue = 5`，B 改成 `StoredValue = 9`，这样 `RunScenario()` 结果分别应为 `8` 与 `12`。先后编译 A/B 后，不销毁 A，分别通过各自的 `Filename/ModuleName` 执行 `int RunScenario()`；随后只 `DiscardModule(A.ModuleName)`，再次验证 B 仍能被解析并执行。 |
| 输入/前置 | 复用 `AcquireFreshSharedCloneEngine()` 或 `CreateIsolatedCloneEngine()`，并在 Shared 为 fixture 增加 `CreateBreakpointFixture(Suffix, StoredValueOverride)` 或等价 `CreateNamedBreakpointFixture(ModuleName, Filename, StoredValue)` helper。测试结束时统一 `DiscardModule()` 两个生成模块，避免污染其他 Shared tests。 |
| 期望行为 | 1. A/B 编译后 `Engine.GetModuleByFilenameOrModuleName(A.Filename, A.ModuleName.ToString())` 与同名 B 查询都各自有效，且 `ModuleName` 不相等。 2. 执行 A 返回 `8`，执行 B 返回 `12`，证明 lookup 没有把两个 fixture 别名到同一个旧模块。 3. 只丢弃 A 后，A 的 module lookup 失效，但 B 仍有效且继续返回 `12`。 4. 若再向 A/B 各自的 `BreakpointHelperLine` 注册断点，`DebugServer` 中也应出现两个独立 key，而不是共享同一 breakpoint store。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerScriptFixture`、`CompileModuleFromMemory`、`ExecuteIntFunction`、`Engine.GetModuleByFilenameOrModuleName()`、新增 `CreateNamedBreakpointFixture(...)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 23:51)

### 一、现有测试问题

#### Issue-46：`StartDebuggerSession` / `StartSteppingDebuggerSession` 在失败路径先返回，清理逻辑尚未安装就把共享调试状态泄漏给后续用例

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.HitLine`、`Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume`、`Angelscript.TestModule.Debugger.Breakpoint.IgnoreInactiveBranch`、`Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:19-80,324-330,438-442,574-578`；`AngelscriptDebuggerSteppingTests.cpp:17-79,350-354,458-462,559-563`；关联基础设施：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp:80-100` |
| 问题描述 | 6 个用例都是先调用 `StartDebuggerSession()` / `StartSteppingDebuggerSession()`，只有 helper 返回成功后才注册各自的 `ON_SCOPE_EXIT` 清理。两组 helper 在 `Client.Connect()` 成功后还会继续发送 `StartDebugging` 并等待 `DebugServerVersion`；如果这一阶段失败，它们会直接 `return false`。此时测试体尚未安装任何 `StopDebugging` / `Disconnect` / `DiscardModule` 清理，`FAngelscriptDebuggerTestClient` 析构只会关本地 socket，`FAngelscriptDebuggerTestSession::Shutdown()` 也不会把共享 production engine 的 `bIsDebugging`、`bIsPaused`、`BreakpointCount` 收口。结果是“启动 helper 失败”本身就可能把 editor 进程里的共享 debug session 留在脏状态。 |
| 影响 | 这会把一次本应局限在当前用例的握手/启动失败扩散成后续 5 个用例的顺序相关污染。后面的场景可能继承到残留的 debugging mode、旧客户端连接或旧断点，表现成偶发 timeout、意外 `HasStopped`，甚至让后续用例在错误前置状态下误报通过。 |
| 修复建议 | 不要把 cleanup 安装放在 helper 之后。更稳妥的做法是抽一个 `FScopedDebuggerScenario` / `FScopedDebuggerClientSession` RAII helper：创建 `Session` / `Client` 后立刻建立析构清理，无论启动阶段在哪一步失败，都统一执行 `SendStopDebugging()`、`PumpUntil(!bIsDebugging && !bIsPaused)`、`SendDisconnect()`、`Disconnect()`。如果暂时不重构，至少让 `StartDebuggerSession()` / `StartSteppingDebuggerSession()` 在 `Client.Connect()` 成功后的所有失败分支里主动收口 shared server 状态，而不是把失败路径留给对象析构的弱清理。 |

### 二、需要新增的测试

本轮先记录新增现有测试问题，新的场景缺口将在后续段落继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-46 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 23:41)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮新增 `NewTest-41`，正文已记录于本文 `## 测试审查 (2026-04-09 23:40)` 段落；该条目覆盖“`StepIn` 在非调用语句上逐行前进”的缺口。

尾部索引说明：由于该文档历史上已存在段落顺序错位，本轮在尾部补记索引，继续审查时请按 `NewTest-41` 编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 23:40)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-41：`StepIn` 在非调用语句上必须前进到下一条脚本行，而不是卡在原地或直接跑出当前函数

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `StepIn` 分支、`FAngelscriptDebugServer::ProcessScriptLine()` |
| 现有测试覆盖 | 当前只有 `Angelscript.TestModule.Debugger.Stepping.StepIn`，但它只在 `StepCallLine` 这个函数调用点上发送 `StepIn`。现有 7 个用例没有任何一个场景是在已经停到普通语句后，再验证 `StepIn` 是否仍然只前进到下一条脚本行。`CreateSteppingFixture()` 里的 `StepInnerEntryLine`/`StepInnerLine` marker 目前只被拿来验证“进入 callee”与“callee 内 step over”，没有覆盖“callee 内非调用语句 step in”。 |
| 风险评估 | 如果 `StepIn` 在非调用语句上退化成“重复停在同一行”“直接 `Continue` 到函数末尾”或“错误地跳回 caller/after-call 行”，当前套件仍会全部绿灯。真实 IDE 会表现成用户在函数体里按 `StepIn` 却没有逐行前进，属于基础步进语义缺口。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Stepping.StepInOnStatementAdvancesWithinFrame` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerStepInStatementTests.cpp`，文件只放非调用语句 `StepIn` / 相邻逐行语义 1-2 个 `ASTEST_*` 用例，控制在 300-500 行 |
| 场景描述 | 复用 `FAngelscriptDebuggerScriptFixture::CreateSteppingFixture()`，但把断点直接设在 `StepInnerEntryLine`。脚本第一次停下时，顶帧已经位于 `Inner()` 的普通赋值语句。此时发送一次 `StepIn`，预期第 2 个 stop 仍停在 `Inner()` 内部，并前进到 `StepInnerLine`；随后发送 `Continue` 让脚本自然结束。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、现有 `StartStepMonitor(...)` / `FStepMonitorPhase`，phase 序列设置为 `{ StepIn, Continue }`。断点行改为 `Fixture.GetLine(TEXT("StepInnerEntryLine"))`，并保留 callstack 采集，以便同时锁定 frame depth 与 caller frame。 |
| 期望行为 | 1. 第 1 个 stop：`Reason == "breakpoint"`，顶帧 `Source` 以 `Fixture.Filename` 结尾，`LineNumber == StepInnerEntryLine`，`Frames.Num() == 2`。 2. 第 2 个 stop：`Reason == "step"`，顶帧仍以 `Fixture.Filename` 结尾，`LineNumber == StepInnerLine`，`Frames.Num() == 2`，且 `Frames[1].LineNumber == StepCallLine`。 3. 第 2 个 stop 不得仍停在 `StepInnerEntryLine`，也不得直接跳到 `StepAfterCallLine` 或把栈深降回 `1`。 4. invocation 成功完成，`InvocationState->bSucceeded == true` 且 `InvocationState->Result == 14`。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerScriptFixture::CreateSteppingFixture()`、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、现有 `StartStepMonitor(...)` / `FStepMonitorPhase`、现有异步 invocation helper |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 23:27)

### 一、现有测试问题

#### Issue-45：断点/步进 fixture 在共享 production engine 上复用固定 `ModuleName/Filename`，会把残留模块与断点直接别名到下一条用例

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerScriptFixture.cpp:62-69,97-104`；`AngelscriptDebuggerBreakpointTests.cpp:20-23`；`AngelscriptDebuggerSteppingTests.cpp:18-21`；关联解析/索引路径：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp:372-382`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:933-939,960-966` |
| 问题描述 | `StartDebuggerSession()` / `StartSteppingDebuggerSession()` 都显式复用 `TryGetRunningProductionDebuggerEngine()` 返回的共享 production engine；与此同时，`CreateBreakpointFixture()` 和 `CreateSteppingFixture()` 每次都生成固定的 `ModuleName/Filename`，分别是 `DebuggerBreakpointFixture` / `DebuggerBreakpointFixture.as` 与 `DebuggerSteppingFixture` / `DebuggerSteppingFixture.as`。而脚本执行查找 `ExecuteIntFunction(...)`、断点注册 `HandleMessage(SetBreakpoint/ClearBreakpoints)` 都正是按 `Filename/ModuleName` 解析模块与 breakpoint store。这样一来，只要前一条用例因为已记录的 teardown / async invocation / late socket cleanup 问题留下了同名旧模块或旧 breakpoint entry，下一条用例就会继续命中同一组键，根本没有办法在标识层面区分“这是全新 fixture”还是“在复用旧状态”。 |
| 影响 | 当前 debugger 场景的顺序相关性被固定标识进一步放大。最坏情况下，后续用例会在同名旧模块上设置断点、读取旧 callstack/source、或把上一次残留的 breakpoint store 当成当前编译结果，从而出现“测试通过但其实跑的是旧脚本/旧断点”的假绿。它也让已有的 cleanup 缺陷更难定位，因为残留状态与新状态在 key 上完全重叠。 |
| 修复建议 | 让 fixture identity 默认唯一化，而不是固定字面量。最直接的做法是给 `FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture()` / `CreateSteppingFixture()` 增加 `Suffix` 或 `FixtureId` 参数，把 `ModuleName` 与 `Filename` 生成为 `DebuggerBreakpointFixture_<Id>`、`DebuggerBreakpointFixture_<Id>.as` 这类唯一值；测试体可用测试名或 `FGuid::NewGuid()` 作为输入。随后所有 `SendSetBreakpoint`、`ExecuteIntFunction`、`DiscardModule` 都沿用该实例自己的生成名，确保即便共享 production engine 上残留旧状态，也不会和新用例的 key 直接重叠。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-45 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 12:51)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-37：`StaticJITHeader` 的 primitive conversion helper 必须锁定 bit-cast 与数值转换语义

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` |
| 关联函数 | `value_as()`、`value_read()`、`value_assign_safe()`、`ConvertPrimitiveValue()` |
| 现有测试覆盖 | 完全无直接测试。当前文档里对 `StaticJITHeader.h` 的建议只覆盖 exception helper、native bridge、debug callstack 和生成输出；测试侧对 `value_as` / `value_read` / `value_assign_safe` / `ConvertPrimitiveValue` 是 0 次符号命中，而 `AngelscriptBytecodes.cpp:4731-4803,4959-5103` 已把这些 helper 用在 `asBC_RDR*` 与 `iTOf/uTOf/iTOd/uTOd` 等核心寄存器读取、原始 bit-cast 和 primitive conversion 路径上。 |
| 风险评估 | 这组 helper 是生成代码最底层的值搬运与数值转换基石，一旦 bit-cast 方向、zero-extend/truncate 语义或 `ConvertPrimitiveValue` 的 MSVC work-around 回归，JIT 路径会在 float/int/64-bit 转换上静默产出错误值，而当前所有 debugger 与 `PrecompiledData` 测试都不会报警。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.PrimitiveConversions.BitCastAndNumericParity` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITPrimitiveConversionTests.cpp`，文件只放 primitive conversion helper 方向 2-3 个 `ASTEST_*` 用例，控制在 300-500 行 |
| 场景描述 | 直接 include `StaticJIT/StaticJITHeader.h`，做纯 C++ header-level 回归。Case 1 验证 bit-cast：`value_as<float>(asDWORD(0x3FC00000))` 必须得到 `1.5f`，再用 `value_assign_safe<asDWORD>(&Bits, 1.5f)` + `value_read<asDWORD>(&Bits)` 读回同一个 `0x3FC00000`。Case 2 验证 widening/truncation：`value_assign_safe<asQWORD>(&Wide, asDWORD(0x89ABCDEF))` 后 `value_read<asQWORD>(&Wide)` 必须是 `0x0000000089ABCDEFull`，证明 helper 没有脏高位。Case 3 验证 numeric conversion：`ConvertPrimitiveValue<double, int>(-1)` 必须得到 `-1.0`，`ConvertPrimitiveValue<double, asDWORD>(0xFFFFFFFFu)` 必须得到 `4294967295.0`，直接锁住 `StaticJITHeader.h:306-312` 注释提到的“MSVC 把 `-1` 错转成 `UINT_MAX`”回归。 |
| 输入/前置 | 复用 `ASTEST_*` 宏即可，不需要 world/engine；测试文件直接 include `StaticJIT/StaticJITHeader.h` 与 `CoreMinimal.h`。对浮点断言可用 `TestEqual` 验证精确可表示值，或用 `FMath::IsNearlyEqual` 把误差固定在 `KINDA_SMALL_NUMBER` 以内。 |
| 期望行为 | 1. `value_as<float>(0x3FC00000u)` 返回 `1.5f`。 2. `value_assign_safe<asDWORD>` + `value_read<asDWORD>` 往返后，bit pattern 仍是 `0x3FC00000`。 3. `value_assign_safe<asQWORD>(..., asDWORD(0x89ABCDEF))` 读回 `0x0000000089ABCDEFull`，高 32 位保持 0。 4. `ConvertPrimitiveValue<double, int>(-1)` 返回 `-1.0`，绝不能退化成 `4294967295.0`。 5. `ConvertPrimitiveValue<double, asDWORD>(0xFFFFFFFFu)` 返回 `4294967295.0`，证明 signed/unsigned 路径没有串值。 |
| 使用的 Helper | `ASTEST_*` 宏、`value_as()`、`value_read()`、`value_assign_safe()`、`ConvertPrimitiveValue()` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:20)

### 一、现有测试问题

本轮新增 `Issue-48`，正文已记录于本文 `## 测试审查 (2026-04-10 00:17)` 段落；该条目覆盖“6 个 breakpoint/stepping happy-path 全部依赖第二个 monitor client，单连接 IDE 主路径未被现有测试锁住”的问题。

### 二、需要新增的测试

本轮新增 `NewTest-44`、`NewTest-45`，正文已记录于本文 `## 测试审查 (2026-04-10 00:18)` 段落；这两个条目分别补“同一 client 的 breakpoint 往返闭环”和“同一 client 的 `StepIn` 步进闭环”。

尾部索引说明：该文档历史上已存在段落顺序错位；本轮新增正文虽然已写入本文，但不完全位于文件尾部。后续继续审查时请按 `Issue-48`、`NewTest-44`、`NewTest-45` 编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-48 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 2 | MissingScenario: 2 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:34)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `NewTest-46`、`NewTest-47`、`NewTest-48`，正文已记录于本文 `## 测试审查 (2026-04-10 00:29)` 段落；继续审查时请按这些编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:29)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-46：混合 `Blueprint + Angelscript` 调用栈必须同时正确暴露 `(BP)` frame 与 Blueprint `this` 求值

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `FAngelscriptDebugServer::SendCallStack()`、`ResolveDebuggerFrame()`、`GetDebuggerValue()`、`GetDebuggerScope()` |
| 现有测试覆盖 | 当前 7 个 debugger 场景全部使用纯脚本 fixture：`CreateBreakpointFixture()`、`CreateSteppingFixture()` 都只在 Angelscript 栈里断住，不会产生任何 Blueprint frame。仓库历史缺口文档也还没有覆盖 `DebugServer` 里 `FBlueprintContextTracker` / `GetBlueprintCallstackFrame()` 这条混合栈分支。 |
| 风险评估 | 这段代码专门把 Blueprint frame 插入 `CallStack`，并为 Blueprint frame 单独实现了 `%this%` / 成员求值。若 frame 顺序、`(BP)` 命名、`Source = "::Outer"`、或 Blueprint frame 上的 `this` 解析回归，现有 7 个用例会全部绿灯，但真实 IDE 会直接表现成“脚本里断住后看不到 Blueprint caller”或“切到 Blueprint frame 后 Variables/Evaluate 全空”。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Blueprint.MixedCallstackAndThisScope` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBlueprintFrameTests.cpp`，放 1-2 个 Blueprint 混合栈场景，整文件控制在 300-500 行 |
| 场景描述 | 在 `Shared/` 新增 `FAngelscriptDebuggerScriptFixture::CreateBlueprintFrameFixture()`：生成一个带 `UPROPERTY() int ScriptValue = 5;` 与 `UFUNCTION(BlueprintCallable) int BreakInScript(int Input)` 的脚本类，断点打在 `BreakInScript` 内部。测试侧复用 `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintSubclassRuntimeTests.cpp` 的 transient Blueprint 创建模式，再新增 `CreateTransientBlueprintCallingScriptFunction(...)` helper，给 Blueprint child 动态生成一个简单图/函数 `CallIntoScript()`，其实现只调用脚本父类的 `BreakInScript(7)`。对这个 Blueprint 函数 `ProcessEvent`，在脚本断点停下后先请求 `CallStack`，再扫描返回帧找到首个 `Name` 以 `(BP)` 开头的 frame index，并用该 index 对 `ScriptValue` 与 `%this%` 发起 `RequestEvaluate` / `RequestVariables`。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、现有异步 invocation helper；在 `Shared/` 新增 `CreateTransientBlueprintCallingScriptFunction(...)`、`DispatchBlueprintFunctionInvocation(...)`、`FindCallstackFrameIndexByPrefix(...)`，Blueprint 创建逻辑优先参考 `Plugins/Angelscript/Source/AngelscriptTest/Blueprint/AngelscriptBlueprintSubclassRuntimeTests.cpp` 里的 `CreateTransientBlueprintChild()` / `CompileAndValidateBlueprint()`。 |
| 期望行为 | 1. `CallStack` 顶部仍包含脚本 frame，`Source.EndsWith(Fixture.Filename)`，`LineNumber == Fixture.GetLine(TEXT("BlueprintScriptBreakLine"))`。 2. 同一份 `CallStack` 里至少存在 1 个 `Name` 以 `(BP)` 开头的 frame，且其 `Source` 以 `::` 开头并指向 transient Blueprint outer/class。 3. 以该 Blueprint frame index 请求 `Evaluate("ScriptValue", FrameIndex)` 时返回值为 `5`；请求 `Variables("%this%", FrameIndex)` 时返回的成员列表也包含 `ScriptValue = 5`。 4. 脚本 invocation 最终成功完成，返回值与 VM 预期一致，不允许为了拿到 callstack 而丢失 Blueprint caller frame。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、新增 `FAngelscriptDebuggerScriptFixture::CreateBlueprintFrameFixture()`、`CreateTransientBlueprintCallingScriptFunction(...)`、`DispatchBlueprintFunctionInvocation(...)`、`FindCallstackFrameIndexByPrefix(...)` |
| 优先级 | P1 |

#### NewTest-47：`RequestEvaluate` 必须正确解析 `Namespace::Value` 与 `Array[Index]`，不能在表达式分词上静默退化

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `ParseExpression()`、`GetDebuggerValue()`、`GetDebuggerScope()` |
| 现有测试覆盖 | 已记录的 `NewTest-7` 只覆盖平面的 `%local%`、`%this%`、`%module%` 与简单变量求值；当前 7 个现有用例完全不触发 `RequestEvaluate`。文档历史里也没有任何一条建议锁住 `ParseExpression()` 对 `[]` 递归、首 token 的 `::` namespace 解析、以及 `Member[Expr]` 组合路径。 |
| 风险评估 | 这些分支一旦回归，调试器最常见的 watch expression 会直接坏掉，但仍表现成“callstack 没问题”。真实前端会遇到 `Numbers[Index]`、`MyNamespace::Value`、`%module%.Globals[1]` 之类表达式全部求值失败或求错值，而当前现有与历史建议都不会直接报警。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Evaluate.NamespaceAndSubscriptExpressions` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerEvaluateExpressionTests.cpp`，整文件放 2-3 个 evaluate edge-case 场景，控制在 300-500 行 |
| 场景描述 | 在 `Shared/` 新增 `FAngelscriptDebuggerScriptFixture::CreateEvaluateExpressionFixture()`：脚本中声明 `namespace DebuggerEval { enum EChoice { Alpha = 11, Beta = 22 } }`、模块级数组 `array<int> ModuleNumbers = {3, 5, 8};`，以及入口函数内的 `array<int> Numbers = {7, 13, 21}; int Index = 1;`，并在返回前的 `/*MARK:EvaluateExpressionLine*/` 断住。暂停后依次请求 1. `Evaluate("Numbers[Index]")`，2. `Evaluate("DebuggerEval::Beta")`，3. `Evaluate("%module%.ModuleNumbers[2]")`，必要时再对 `%local%`/`%module%` scope 做一次变量展开，确认 parser 没把 `::` 误当 frame 前缀，也没把 `[]` 里的局部变量解析丢失。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、现有断点 monitor 或单连接 transcript helper；在 `Shared/` 新增 `CreateEvaluateExpressionFixture()`，并复用 `DispatchModuleInvocation(...)`。如已有 `WaitForSingleClientStop(...)` / `CollectProtocolMessagesUntil(...)` 变体，可优先复用，避免再复制 monitor 逻辑。 |
| 期望行为 | 1. `Evaluate("Numbers[Index]")` 返回 `13`。 2. `Evaluate("DebuggerEval::Beta")` 返回 `22`，证明首 token 的 `::` namespace 解析没有被 frame-prefix 逻辑吃掉。 3. `Evaluate("%module%.ModuleNumbers[2]")` 返回 `8`，证明 `%module% + subscript` 组合能正确进入 `GetDebuggerMember("[2]")` 路径。 4. 整个过程中 `CallStack` 顶帧仍命中 `EvaluateExpressionLine`，且 `MonitorResult.Error.IsEmpty()`、`!MonitorResult.bTimedOut`。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、新增 `FAngelscriptDebuggerScriptFixture::CreateEvaluateExpressionFixture()`、`DispatchModuleInvocation(...)`、`SendRequestEvaluate`、可选 `SendRequestVariables` |
| 优先级 | P1 |

#### NewTest-48：脚本对象构造路径必须执行 `asBC_FinConstruct`，确保 JIT 下 `SCRIPT_FINISH_CONSTRUCT` 与 VM 语义一致

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h` |
| 关联函数 | `IMPL_BYTECODE_BEGIN(asBC_FinConstruct)`、`FStaticJITFunction::ScriptFinishConstruct()`、`SCRIPT_FINISH_CONSTRUCT(...)` wrapper |
| 现有测试覆盖 | 当前缺口文档已经覆盖 `FJITDatabase`、异常 helper、native bridge、primitive conversion、truthiness 与 `Pow`，但仍没有任何一条建议或现有自动化直接命中 `asBC_FinConstruct` / `ScriptFinishConstruct`。这意味着对象构造完成态在 JIT 下仍是 0 条显式护栏。 |
| 风险评估 | 只要脚本里出现值类型/脚本对象构造，JIT 就会依赖 `SCRIPT_FINISH_CONSTRUCT` 完成对象初始化与构造收尾。若这条路径回归，真实运行时会表现成“构造函数执行过但对象未完成初始化”“成员默认值错乱”或更隐蔽的生命周期异常，而现有 `PrecompiledData` 结构回归和 debugger 套件都完全看不到。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.ObjectConstruction.FinishConstructParity` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITObjectConstructionTests.cpp`，集中放对象构造方向 2-3 个 `ASTEST_*` 用例，控制在 300-500 行 |
| 场景描述 | 复用 `NewTest-3` 要求建立的非 `EditorContext` `FStaticJITExecutionHarness`。在脚本里放一个最小脚本对象/值类型，例如 `class FConstructed { int Value; FConstructed() { Value = 41; } int Read() const { return Value + 1; } } int ConstructAndRead() { FConstructed Obj(); return Obj.Read(); }`。先跑 VM 基线，再生成并执行 JIT；若 harness 支持生成源码文本，再断言目标函数输出里包含 `SCRIPT_FINISH_CONSTRUCT(`，确保没有绕过 `asBC_FinConstruct`。 |
| 输入/前置 | 需要真实执行 transpiled code 的 harness，不能跑在 `WITH_EDITOR` + `AS_SKIP_JITTED_CODE` 环境。复用 `FStaticJITExecutionHarness` 或等价 helper，并补齐 `HasJITEntry(FunctionName)`、`ExecuteIntFunctionJIT(FunctionName)`、可选 `GetGeneratedSourceForFunction(FunctionName)` 接口。 |
| 期望行为 | 1. `ConstructAndRead` 在 VM 与 JIT 下都返回 `42`。 2. 目标函数具备有效 JIT entry，而不是回退到解释执行。 3. 若可观测生成源码，源码中必须包含 `SCRIPT_FINISH_CONSTRUCT(`。 4. 再补一个无显式构造函数、只依赖默认成员初始化的子场景时，VM/JIT 结果也必须一致，避免只锁住“显式 ctor”而漏掉构造收尾本身。 |
| 使用的 Helper | `ASTEST_*` 宏、`FStaticJITExecutionHarness`、`CompileModuleFromMemory`、`FJITDatabase::Get()`、可选 `GetGeneratedSourceForFunction(...)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 3 | MissingScenario: 1, MissingEdgeCase: 1, NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:20)

### 一、现有测试问题

本轮新增 `Issue-48`，正文已记录于本文 `## 测试审查 (2026-04-10 00:17)` 段落；该条目覆盖“6 个 breakpoint/stepping happy-path 全部依赖第二个 monitor client，单连接 IDE 主路径未被现有测试锁住”的问题。

### 二、需要新增的测试

本轮新增 `NewTest-44`、`NewTest-45`，正文已记录于本文 `## 测试审查 (2026-04-10 00:18)` 段落；这两个条目分别补“同一 client 的 breakpoint 往返闭环”和“同一 client 的 `StepIn` 步进闭环”。

尾部索引说明：该文档历史上已存在段落顺序错位；本轮新增正文虽然已写入本文，但不完全位于文件尾部。后续继续审查时请按 `Issue-48`、`NewTest-44`、`NewTest-45` 编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-48 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 2 | MissingScenario: 2 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:17)

### 一、现有测试问题

#### Issue-48：6 个 breakpoint/stepping 用例把 stop/stack/continue 全部委托给第二个 monitor client，单连接 IDE 主路径根本没有被现有测试锁住

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.HitLine`、`Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume`、`Angelscript.TestModule.Debugger.Breakpoint.IgnoreInactiveBranch`、`Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:147-242,324-650`；`AngelscriptDebuggerSteppingTests.cpp:145-301,348-655`；关联运行时：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:667-698,812-817,1382-1482` |
| 问题描述 | 这 6 个用例的主 client 只负责初始握手和 `SetBreakpoint/ClearBreakpoints`；真正接收 `HasStopped`、发送 `RequestCallStack`、`Continue`、`StepIn/Over/Out` 的都是 `StartBreakpointMonitor()` / `StartStepMonitor()` 创建的第二个 background client。运行时里 `PauseExecution()` 会向所有 debugging clients 广播 `HasStopped/HasContinued`，但 `SendCallStack(FSocket* Client)` 明确只回给发起请求的那个 socket。也就是说，当前套件从未验证“同一个 IDE 连接既设置断点，又收到自己的 stop，再在同一 socket 上请求 stack 并发 continue/step”这条最常见的单连接调试主路径。只要 secondary client 还能收到广播，哪怕 primary client 的 stop 路由、callstack 请求、continue/step 会话状态已经坏掉，现有 6 个用例仍可能全部绿灯。 |
| 影响 | 现有 debugger 套件更像“多客户端广播路径”回归，而不是“单前端会话”回归。任何只影响发起断点的原始 client 的协议问题，例如 primary client 收不到 `HasStopped`、`RequestCallStack` 只对 secondary client 生效、同一 socket 的 `Continue/Step*` 状态机失配，都可能被当前用例完全漏掉；真实 IDE 却会直接表现成“断点能设但停下后本连接拿不到栈/继续不了”。 |
| 修复建议 | 不要让 happy-path 永久依赖第二个 client。Shared 里新增一个单连接 worker helper，例如 `FSingleClientDebuggerWorker` / `RunSingleClientDebuggerScenario(...)`：由同一个 `FAngelscriptDebuggerTestClient` 在后台线程上依次完成 `StartDebugging -> WaitFor HasStopped -> RequestCallStack -> Continue/Step*`，主线程只负责 `Session.PumpUntil(...)` 和触发脚本执行。至少把 `Breakpoint.HitLine` 和 `Stepping.StepIn` 改成或补成同 socket 闭环基线；保留现有双 client monitor 只作为多客户端/广播路径测试，而不是 6 个主用例的唯一 helper。 |

### 二、需要新增的测试

本轮先记录新增现有测试问题；对应的单连接补测建议将在后续段落继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-48 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 13:04)

### 一、现有测试问题

本轮新增 `Issue-42`，详见本文对应条目。

### 二、需要新增的测试

本轮新增 `NewTest-38`，详见本文对应条目。

尾部索引说明：本轮新增 `Issue-42`、`NewTest-38` 已写入本文；由于该文档历史上已存在段落顺序错位，继续审查时请按编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-42 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 13:03)

### 一、现有测试问题

#### Issue-42：`Breakpoint.HitLine` 只锁定顶帧，断点命中的 caller frame 丢失时仍可能绿灯

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.HitLine` |
| 行号范围 | 418-430 |
| 问题描述 | `CreateBreakpointFixture()` 的调用链固定是 `RunScenario() -> Helper()`，而 `HitLine` 已经开启了 `bRequestCallstack = true`。但用例在拿到 `Callstack` 后只断言 `Frames.Num() > 0`，随后只检查 `Frames[0]` 的 `Source` 和 `LineNumber`。它没有验证 callstack 至少保留 caller frame，也没有证明第 2 帧仍然属于同一个 fixture 的 `RunScenario()` 调用点。这样一来，只要调试器还能返回当前 `Helper()` 顶帧，就算 `SendCallStack()` 把 caller 截断成单帧、把第 2 帧换成错误调用者，甚至把后续 frame 顺序弄乱，这个用例依然会通过。 |
| 影响 | 当前唯一的正向断点 happy-path 用例只能证明“停在了目标行”，不能证明“停下后前端看到的调用栈仍然完整”。这会放过 breakpoint 命中后的 caller/callee 链回归，连带削弱 frame-scoped `Evaluate` / `Variables`、callstack 面板和后续多帧断言的可信度。 |
| 修复建议 | 把 `HitLine` 的 callstack 断言升级成多帧基线：1. 给 `CreateBreakpointFixture()` 增加 `BreakpointCallSiteLine` marker；2. 断言 `Callstack.Frames.Num() == 2`；3. 顶帧继续校验 `Helper()` 的 `Source/LineNumber`；4. 新增对 `Frames[1]` 的断言，要求 `Source.EndsWith(Fixture.Filename)`、`LineNumber == Fixture.GetLine(TEXT("BreakpointCallSiteLine"))`，必要时再补 `Name.Contains(TEXT("RunScenario"))`。 |

### 二、需要新增的测试

#### NewTest-38：`AngelscriptStaticJIT` 的 symbol sanitization 与去重逻辑必须锁住，避免生成非法或冲突的 C++ 标识符

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h` |
| 关联函数 | `FAngelscriptStaticJIT::SanitizeSymbolName()`、`GetUniqueSymbolName()` |
| 现有测试覆盖 | 完全无直接测试。现有文档虽然已经覆盖 `StaticJIT` 的 generated output、helper parity、precompiled fallback 等方向，但还没有任何自动化锁住 `AngelscriptStaticJIT.cpp:250-266,3706-3733` 这一组“把脚本名/模块名转成合法 C++ symbol 并做唯一化”的基础逻辑。 |
| 风险评估 | 这条路径是所有 generated symbol 的入口。一旦 `~`、`.`、`:`、`<`、`>` 等字符的替换规则回归，或 `UsedUniqueNames` 的去重后缀不再稳定，带泛型、析构函数、命名空间或同名脚本函数的模块就可能生成非法/冲突的 C++ 标识符，最终在 JIT codegen 或编译阶段直接炸掉；而当前仅有的 `PrecompiledData` 结构测试和现有 debugger 套件都不会报警。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.Symbols.SanitizeAndDeduplicate` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITSymbolTests.cpp`，文件集中放 2-3 个 symbol generation 单测，控制在 300-500 行 |
| 场景描述 | 在 `#if AS_CAN_GENERATE_JIT` 下直接实例化 `FAngelscriptStaticJIT` 做纯 C++ 单测。Case 1 验证 destructor 名称展开：输入 `~MyType`，调用 `SanitizeSymbolName()` 后应得到 `Destruct__MyType`。Case 2 验证非法字符替换：输入 `Module.Sub-Type`，输出应只剩字母/数字/下划线，且结果为 `Module_Sub_Type`。Case 3 验证去重：连续两次调用 `GetUniqueSymbolName(TEXT(\"Module.Sub\"))`，第 1 次应得到 `Module_Sub`，第 2 次应得到 `Module_Sub__1`；再用另一个会 sanitize 到相同结果的原始输入（例如 `Module/Sub`）调用一次，应继续得到 `Module_Sub__2`。 |
| 输入/前置 | 不需要 engine/world，只需 include `StaticJIT/AngelscriptStaticJIT.h`。测试文件里增加一个小 helper `ContainsOnlyIdentifierChars(const FString&)`，统一验证输出只包含 `[A-Za-z0-9_]`；整个文件用 `ASTEST_*` 宏组织，并在 `#if !AS_CAN_GENERATE_JIT` 分支下显式跳过，避免非 Windows/Linux 配置误报。 |
| 期望行为 | 1. `SanitizeSymbolName(~MyType)` 输出精确等于 `Destruct__MyType`。 2. `SanitizeSymbolName(Module.Sub-Type)` 输出精确等于 `Module_Sub_Type`，且 helper 验证字符串中不含任何非法字符。 3. 第 1 次 `GetUniqueSymbolName(\"Module.Sub\")` 返回 `Module_Sub`。 4. 第 2 次返回 `Module_Sub__1`。 5. 第 3 次使用另一个会 sanitize 成同名的输入时，返回 `Module_Sub__2`，证明去重基于 sanitize 后的最终 symbol，而不是原始输入。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptStaticJIT`、测试内新增 `ContainsOnlyIdentifierChars()` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-42 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 12:53)

### 一、现有测试问题

本轮已新增 `Issue-41`，详见本文对应条目。

### 二、需要新增的测试

本轮已新增 `NewTest-37`，详见本文对应条目。

尾部索引说明：本轮新增 `Issue-41`、`NewTest-37` 已写入本文；由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按这些编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-41 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 13:04)

### 一、现有测试问题

本轮新增 `Issue-42`，详见本文对应条目。

### 二、需要新增的测试

本轮新增 `NewTest-38`，详见本文对应条目。

尾部索引说明：本轮新增 `Issue-42`、`NewTest-38` 已写入本文；由于该文档历史上已存在段落顺序错位，继续审查时请按编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-42 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

## 测试审查 (2026-04-09 12:13)

### 一、现有测试问题

本轮已新增 `Issue-40`，详见本文对应条目。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `Issue-40` 已写入本文；由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按该编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-40 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

## 测试审查 (2026-04-09 12:13)

### 一、现有测试问题

#### Issue-40：`ClearThenResume` 首轮没有验证 stop reason 与命中位置，任意暂停都可能被误认成目标断点

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume` |
| 行号范围 | 510-518 |
| 问题描述 | 首轮执行结束后，该用例只断言 `FirstMonitorResult.StopEnvelopes.Num() > 0` 和 `FirstInvocation->bSucceeded`，既没有反序列化 `FirstMonitorResult.StopEnvelopes[0]`，也没有校验 `Reason == "breakpoint"`、`Source` 命中 `Fixture.Filename`、`LineNumber == Fixture.GetLine(TEXT("BreakpointHelperLine"))`。因此只要首轮运行期间出现任何暂停事件，例如错误行号的 breakpoint、意外 `pause`/`exception`、或残留会话带来的其它 stop，这个用例都会把“首轮确实命中了刚设置的断点”判成成功。 |
| 影响 | 该用例当前无法把 `ClearBreakpoints` 的验证建立在可靠的前提上。若首轮停下的根本不是目标断点，后半段“清除后不再停下”仍可能绿灯，导致断点命中位置回归、残留暂停状态或错误 stop reason 都被吞掉。 |
| 修复建议 | 把首轮断言升级为和 `HitLine` 一样的精确协议检查：1. 反序列化首个 `HasStopped` 为 `FStoppedMessage`，断言 `Reason == "breakpoint"`；2. 要么给首轮 monitor 开启 `bRequestCallstack = true`，要么改用可选 callstack collector，显式断言顶帧 `Source.EndsWith(Fixture.Filename)` 且 `LineNumber == BreakpointHelperLine`；3. 继续保留已记录的 `Issue-33` 返回值断言，把“停在正确位置”和“恢复后结果正确”同时锁住。 |

---

## 测试审查 (2026-04-09 12:05)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-33：`FAngelscriptDebuggerTestSession` 必须把 debug-break 全局开关恢复到进入前状态，而不是一律恢复成 enabled

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp` |
| 关联函数 | `FAngelscriptDebuggerTestSession::Initialize()`、`Shutdown()`、`AngelscriptDisableDebugBreaks()`、`AngelscriptEnableDebugBreaks()` |
| 现有测试覆盖 | 完全无直接测试。当前 7 个 debugger 用例从不设置 `FAngelscriptDebuggerSessionConfig::bDisableDebugBreaks`，也没有任何基础设施测试断言 session 生命周期是否保留进入前的 `GDebugBreaksEnabled` 状态。 |
| 风险评估 | 若不补这条回归，任何后续需要临时关闭 native debug break 的 debugger/JIT 测试都会把全局开关改成顺序相关状态，导致 `DebugBreak()`、失败的 `ensure` / `check` 在后续用例里行为漂移。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Shared.SessionPreservesDebugBreakState` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSessionInfrastructureTests.cpp`，文件集中放 session/client infrastructure 回归 2-3 个 `ASTEST_*` 用例，整文件控制在 300-500 行 |
| 场景描述 | 在 `Shared/` 增加一个只用于自动化的 `FScopedDebugBreakStateSentinel` 或 `GetAreDebugBreaksEnabledForTesting()` seam。测试分两段执行：1. 先显式 `AngelscriptEnableDebugBreaks()`，记录 sentinel 为 enabled，再创建 `FAngelscriptDebuggerTestSession` 并用 `bDisableDebugBreaks=true` 初始化/析构，断言退出后仍为 enabled；2. 再显式 `AngelscriptDisableDebugBreaks()`，重复同样的 session 生命周期，断言退出后仍保持 disabled，而不是被 helper 强行改回 enabled。 |
| 输入/前置 | 复用 `TryGetRunningProductionDebuggerEngine()` 作为 `ExistingEngine`，避免把本测试耦合到 `CreateTestingFullEngine` 端口分配；新增 `FScopedDebugBreakStateSentinel` 负责保存并恢复原始全局状态，并暴露 `IsEnabled()` / `SetEnabled(bool)` 断言入口。 |
| 期望行为 | 1. pre-enabled 分支：session 初始化期间允许把开关暂时拉成 disabled，但 `Shutdown()` 后 sentinel 仍报告 enabled。 2. pre-disabled 分支：`Shutdown()` 后 sentinel 仍报告 disabled，不得被 helper 强行改回 enabled。 3. 两个分支都不应因为 session 析构而修改 `AngelscriptDebugServer::DebugAdapterVersion` 以外的无关全局状态。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerTestSession`、`TryGetRunningProductionDebuggerEngine()`、新增 `FScopedDebugBreakStateSentinel` 或 `GetAreDebugBreaksEnabledForTesting()` |
| 优先级 | P1 |

#### NewTest-34：`FAngelscriptDebuggerTestClient::Connect` 必须把 `EINPROGRESS`/连接未完成收敛成明确的 connect-timeout 失败，而不是把错误拖到握手阶段

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h` |
| 关联函数 | `FAngelscriptDebuggerTestClient::Connect()` |
| 现有测试覆盖 | 当前 7 个 debugger 用例只覆盖“对真实 debug server 端口发起成功连接”；没有任何自动化覆盖 unavailable endpoint、`SE_EINPROGRESS` 长时间不完成、或 connect 失败后 `LastError` / socket 清理语义。 |
| 风险评估 | 如果不把这条失败路径锁住，任何本地端口冲突、server 未监听、或非阻塞 connect 长时间未完成都会被现有测试伪装成 `StartDebugging`/handshake timeout，导致 Shared helper 的诊断能力持续失真。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Shared.ClientConnectTimeoutReportsFailure` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSessionInfrastructureTests.cpp`，与 `NewTest-33` 共用 infrastructure helper，整文件保持在 300-500 行 |
| 场景描述 | 为 `FAngelscriptDebuggerTestClient` 增加一个可注入的 socket factory / fake socket seam，仅在测试构建中启用。构造一个 `FFakeDebuggerClientSocket`：第一次 `Connect()` 返回 `false` 且 `GetLastErrorCode()==SE_EINPROGRESS`，随后在整个 timeout 窗口内 `GetConnectionState()` 都维持 `SCS_NotConnected` 或 `SCS_ConnectionError`。测试调用新的 `Connect(Host, Port, TimeoutSeconds)` 或等价 timeout-aware API，验证 client 会在 deadline 内返回失败，而不是先返回 `true` 再在握手阶段超时。 |
| 输入/前置 | 在 `Shared/` 新增 `IDebuggerTestSocketFactory` / `FFakeDebuggerClientSocket`（仅测试使用），并给 `FAngelscriptDebuggerTestClient` 增加注入点或 test-only constructor；同时把 connect timeout 暴露成参数，避免继续依赖后续 `WaitForMessage()` 的超时兜底。 |
| 期望行为 | 1. `Connect(...)` 在给定 timeout 内返回 `false`。 2. `Client.GetLastError()` 明确包含 host、port 和 timeout/连接状态，而不是模糊的 handshake timeout 文案。 3. 失败后 `Client.IsConnected() == false`，内部 socket 已关闭，不会把半连接状态带入后续 `SendStartDebugging()`。 4. 若 fake socket 在 timeout 前切到 `SCS_Connected`，同一 helper 应能返回成功，证明它真的等待了连接完成，而不是单纯把 `EINPROGRESS` 判死。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerTestClient`、新增 `IDebuggerTestSocketFactory` / `FFakeDebuggerClientSocket`、新增 timeout-aware `Connect` overload |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 11:43)

### 一、现有测试问题

#### Issue-37：3 个步进用例没有锁定精确的 callstack 形状，caller/callee 链回归仍可能误报通过

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | `AngelscriptDebuggerSteppingTests.cpp:437-449, 532-550, 634-651` |
| 问题描述 | `CreateSteppingFixture()` 只有一层 `RunScenario() -> Inner()` 调用，callstack 形状其实是确定的：call-site stop 应该只有 1 帧，`StepIn` 进入 `Inner()` 后应该是 2 帧，`StepOut` 回到 caller 后应再回到 1 帧。但当前 3 个用例只检查顶帧行号和“`Frames.Num() >= 2` / `Frames.Num() < 上一次` / `两次深度相等`”这类相对条件，从不验证 `Frames[1]` 是否仍然指向 `StepCallLine`，也不锁定精确帧数。只要错误 callstack 仍然带着正确顶帧行号，例如多混入一层 runtime/internal frame、caller frame 漂到错误行、或 `StepOver` 在前后 stop 都附带同样的额外帧，这 3 个用例仍可能通过。 |
| 影响 | 现有步进回归只能证明“某个顶帧停在了预期行号附近”，不能证明前端看到的调用栈结构是正确的。这样会放过 frame-scoped `Evaluate/Variables`、callstack 面板和 step 语义依赖的 caller/callee 链回归，尤其是在后续引入跨文件和 legacy adapter 覆盖时更容易被误判成正常。 |
| 修复建议 | 把单文件 stepping fixture 的 callstack 形状断言收紧成确定值：1. `StepIn` 首个 stop 断言 `Frames.Num() == 1`，第二个 stop 断言 `Frames.Num() == 2`，且 `Frames[1].Source`/`LineNumber` 仍指向 `Fixture.Filename` / `StepCallLine`。 2. `StepOver` 两个 stop 都断言 `Frames.Num() == 1`，避免“前后都带一层脏 caller frame”也通过。 3. `StepOut` 第二个 stop 断言 `Frames.Num() == 2` 且 `Frames[1]` 仍是 caller call-site，第三个 stop 断言精确回到 `Frames.Num() == 1`。这些断言应和已记录的 `Issue-28` 一起补齐 `Source` 校验，形成完整的单文件步进基线。 |

### 二、需要新增的测试

#### NewTest-30：`StepOver` 在 callee 内部必须前进到下一条可执行行，而不是退化成 `StepOut` 或 `Continue`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `StepOver` 分支、`FAngelscriptDebugServer::ProcessScriptLine()` |
| 现有测试覆盖 | 当前只有 `Angelscript.TestModule.Debugger.Stepping.StepOver`，它只覆盖“在 caller 的函数调用点执行 `StepOver` 后跳到 after-call 行”。现有 7 个用例没有任何一个场景在已经 `StepIn` 进入 `Inner()` 后，再验证 `StepOver` 是否留在 callee 内部并前进到下一条语句；`CreateSteppingFixture()` 里的 `StepInnerLine` marker 目前完全未被使用。 |
| 风险评估 | 如果 runtime 把“callee 内部的 `StepOver`”错误实现成 `StepOut`、`Continue` 或重复停在同一行，当前测试仍会全部绿灯。真实 IDE 会表现成用户在函数体里点 `StepOver` 却直接跳回 caller 或直接跑完函数，这属于核心步进语义缺口。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Stepping.StepOverWithinCallee` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerStepOverInFunctionTests.cpp`，使用 `ASTEST_*` 宏组织 1-2 个步进用例，整文件控制在 300-500 行 |
| 场景描述 | 复用 `FAngelscriptDebuggerScriptFixture::CreateSteppingFixture()`。先在 `StepCallLine` 断住，随后依次发送 `StepIn`、`StepOver`、`Continue`。这样 stop 序列应为：1. caller call-site breakpoint；2. 进入 `Inner()` 的 `StepInnerEntryLine`；3. 仍留在 `Inner()` 内部、移动到 `StepInnerLine`；最后继续执行结束。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、现有 `StartStepMonitor(...)` / `FStepMonitorPhase`，phase 序列设置为 `{ StepIn, StepOver, Continue }`。沿用 `CreateSteppingFixture()`，不需要新增 fixture，只需要在测试体里显式使用已有的 `StepInnerLine` marker。 |
| 期望行为 | 1. 第 1 个 stop：`Reason == "breakpoint"`，`Source` 以 `DebuggerSteppingFixture.as` 结尾，`LineNumber == StepCallLine`，`Frames.Num() == 1`。 2. 第 2 个 stop：`Reason == "step"`，顶帧 `LineNumber == StepInnerEntryLine`，`Frames.Num() == 2`，`Frames[1].LineNumber == StepCallLine`。 3. 第 3 个 stop：`Reason == "step"`，顶帧仍然是 `DebuggerSteppingFixture.as`，`LineNumber == StepInnerLine`，`Frames.Num() == 2`，caller frame 仍保持 `StepCallLine`。 4. 第 3 个 stop 不得直接跳到 `StepAfterCallLine`，也不得把 stack depth 降回 `1`。 5. 最终 `InvocationState->bSucceeded == true` 且 `InvocationState->Result == 14`。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateSteppingFixture()`、现有 `StartStepMonitor(...)` / `FStepMonitorPhase` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-37 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 14:56)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `Issue-33`、`Issue-34`、`NewTest-27` 已写入本文；由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按这些编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 11:27)

### 一、现有测试问题

#### Issue-35：`FAngelscriptDebuggerTestSession` 在 fresh session 上先执行 `Shutdown()`，会无条件改写进程级 `DebugAdapterVersion`

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerTestSession.cpp:29-35, 80-99`；`AngelscriptDebuggerTestSession.h:57-59` |
| 问题描述 | `Initialize()` 一进入就先调用 `Shutdown()`，而 `Shutdown()` 不管 session 是否真的初始化过，都会执行 `AngelscriptDebugServer::DebugAdapterVersion = PreviousDebugAdapterVersion;`。`PreviousDebugAdapterVersion` 在 header 里默认是 `0`，只有 `Initialize()` 跑到第 35 行之后才会重新捕获当前全局值。这意味着 fresh session 的第一次 `Initialize()`、以及任何“构造后还没成功初始化就析构”的失败路径，都会先把进程级 `DebugAdapterVersion` 回写成默认成员值。当前 debugger 测试又全部挂到共享 production engine，上述写回不是 session 局部状态，而是会直接影响同进程里其他调试连接和后续消息序列化。 |
| 影响 | Shared session helper 会在用例启动/失败清理阶段引入隐藏的全局状态污染。若前一个调试连接仍在运行，或同进程里还有其他 debugger/client 使用非默认 adapter version，这个 helper 会在真正握手前把协议版本静默改回旧值，导致 `Variables/Evaluate` payload 分支、后续兼容断言和跨测试隔离都变得顺序相关。 |
| 修复建议 | 给 `FAngelscriptDebuggerTestSession` 增加显式的“已捕获全局快照”状态，例如 `bHasCapturedDebugAdapterVersion`。只有在成功记录 `PreviousDebugAdapterVersion` 之后，`Shutdown()` 才允许回写；`Initialize()` 也不应在 fresh session 上先执行会改全局的 `Shutdown()`，而应先 reset 本地指针/owned resources，再在需要时恢复先前快照。最小修复是把 `PreviousDebugAdapterVersion` 初始化成当前全局值并在 `Shutdown()` 中加 guard，避免未初始化 session 污染全局协议版本。 |

### 二、需要新增的测试

本轮先记录 `DebugAdapterVersion` 的 session 隔离问题，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-35 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 11:29)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-28：`FAngelscriptDebuggerTestSession` 的初始化/析构不得在握手前污染全局 `DebugAdapterVersion`

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h` |
| 关联函数 | `FAngelscriptDebuggerTestSession::Initialize()`、`Shutdown()` |
| 现有测试覆盖 | 完全无直接测试；当前 7 个 debugger 用例只把 `FAngelscriptDebuggerTestSession` 当成启动 helper 使用，从不单独断言 session 的 init/shutdown 是否会在握手前改写 `AngelscriptDebugServer::DebugAdapterVersion` |
| 风险评估 | 若这个 helper 在 fresh session、初始化失败或析构路径里把全局 adapter version 静默改回默认值，后续 `Variables/Evaluate` payload 兼容分支会变成顺序相关问题；当前所有 debugger 用例都可能在“看起来只是 session 生命周期变化”时被共享状态污染。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Shared.SessionInitializeDoesNotMutateAdapterVersion` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSessionInfrastructureTests.cpp`，文件只放 session/client shared helper 回归 1-2 个用例，控制在 300-500 行 |
| 场景描述 | 先把 `AngelscriptDebugServer::DebugAdapterVersion` 显式设为一个非默认 sentinel 值，例如 `7`。然后分两段验证：1. fresh `FAngelscriptDebuggerTestSession` 调用 `Initialize()` 之前/之后，但还没发送 `StartDebugging` 时，全局值不应变化；2. 构造一个未成功初始化的 session（例如不给 `ExistingEngine` 且不真正进入握手，或显式只走析构路径），离开作用域后全局值仍保持 sentinel。 |
| 输入/前置 | 复用 `TryGetRunningProductionDebuggerEngine()` 取得 attach 目标，但测试本身不需要真正建立 client handshake；在 `Shared/` 新增 `FScopedDebugAdapterVersionSentinel` helper，负责保存并恢复原始全局值，并提供 `SetSentinel(int32)` / `GetCurrent()` 断言入口。若需要覆盖 failure path，可结合 `FAngelscriptDebuggerSessionConfig` 构造一个会早退的初始化配置。 |
| 期望行为 | 1. sentinel 在 `Session.Initialize(...)` 返回后、`Client.SendStartDebugging(...)` 之前保持不变。 2. fresh session 的析构或显式 `Shutdown()` 不会把 sentinel 改写成 `0` 或其他旧值。 3. 只有真正发送 `StartDebugging(AdapterVersion)` 并收到 server 处理后，全局值才允许切到请求版本。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`TryGetRunningProductionDebuggerEngine()`、新增 `FScopedDebugAdapterVersionSentinel` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 12:51)

### 一、现有测试问题

#### Issue-41：`FAngelscriptDebuggerTestSession` 默认会清空全局 ensure 去重状态，Shared helper 把运行时语义改成了“每个 session 重置一次”

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerTestSession.h:18-19`；`AngelscriptDebuggerTestSession.cpp:29-40,80-85`；关联运行时语义：`Bind_Debugging.cpp:43-62` |
| 问题描述 | `FAngelscriptDebuggerSessionConfig` 默认把 `bResetSeenEnsuresOnInitialize` / `bResetSeenEnsuresOnShutdown` 都设为 `true`，而 `Initialize()` / `Shutdown()` 会直接调用 `AngelscriptForgetSeenEnsures()`。该函数会把 `Bind_Debugging.cpp` 中进程级的 `PoppedEnsures` map 整体清空；但真实运行时的 ensure 去重语义是按 `GEndPlayMapCount` 区分“同一地图生命周期内同一位置只 break 一次”，并不是“每次 debugger attach/detach 都重置”。当前 7 个 debugger 用例都复用这套 session helper 且 attach 到共享 production engine，因此每次测试都会改写 editor 进程级 ensure 状态。 |
| 影响 | Shared helper 会把 future `ensure(false)` 调试用例的前置语义改掉，也会把别的自动化或同进程 editor 状态中的 ensure 去重历史清空，导致“同一脚本位置第二次 ensure 是否还应 break”变成顺序相关行为。这样测出来的是 test harness 语义，不是产品语义。 |
| 修复建议 | 把两个 reset 开关改成 opt-in，而不是默认开启；只在专门验证 ensure/debug-break 行为的用例里显式请求清空。若确实需要在基础设施层恢复状态，应先为 `PoppedEnsures` 增加 test-only snapshot/restore seam，例如 `FScopedSeenEnsureStateSentinel`，避免用 `Empty()` 粗暴覆盖共享进程状态。 |

### 二、需要新增的测试

本轮先记录 Shared session 的新隔离问题，新增测试建议将在后续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-41 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 11:32)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-29：`FJITDatabase` 引用注册与 `Clear()` 必须保持一致，不能把 JIT lookup 状态静默留脏

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h` |
| 关联函数 | `FJITDatabase::Clear()`、`Get()`、`FStaticJITFunction::FStaticJITFunction(...)`、`FJitRef_Function::FJitRef_Function(...)`、`FJitRef_SystemFunctionPointer::FJitRef_SystemFunctionPointer(...)`、`FJitRef_Type::FJitRef_Type(...)`、`FJitRef_GlobalVar::FJitRef_GlobalVar(...)`、`FJitRef_PropertyOffset::FJitRef_PropertyOffset(...)` |
| 现有测试覆盖 | 完全无直接测试；当前缺口文档里的 `NewTest-3` / `NewTest-5` 关注的是生成输出和真实执行，尚未单独锁住 `StaticJITHeader.cpp` 这些“注册 lookup side effect + 清库”基础设施 |
| 风险评估 | 这批 helper 是生成代码和运行态绑定之间的最底层桥。如果构造器不再向 `FJITDatabase` 追加 lookup，或 `Clear()` 漏掉某个数组，JIT 生成结果就可能带着旧 lookup / 残留函数表继续运行，表现为错绑函数、错绑类型或顺序相关的 property offset 漂移，而现有自动化不会直接指出是 database 污染。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.Database.ReferenceRegistrationAndClear` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITDatabaseTests.cpp`，文件只放 database/reference-registration 方向 1-2 个用例，控制在 300-500 行 |
| 场景描述 | 先调用 `FJITDatabase::Clear()` 保证空库，再依次构造最小对象：1 个 `FStaticJITFunction`、1 个 `FJitRef_Function`、1 个 `FJitRef_SystemFunctionPointer`、1 个 `FJitRef_Type`、1 个 `FJitRef_GlobalVar`、1 个 `FJitRef_PropertyOffset`。随后直接读取 `FJITDatabase::Get()`，确认各 lookup 数组和 `Functions` map 都增加到预期数量；最后再次调用 `Clear()`，验证所有数组/map 都回到空。若当前构建启用了 `AS_JIT_VERIFY_PROPERTY_OFFSETS`，同一个文件里再补 `FJitVerifyPropertyOffset` / `FJitVerifyTypeSize` 的条件分支断言。 |
| 输入/前置 | 复用 `FAngelscriptEngineScope` 或等价 minimal engine setup 以获得可用的脚本 engine 指针；在 test file 里增加一个 `ResetJITDatabase()` helper 包装 `FJITDatabase::Clear()` 并在 `ON_SCOPE_EXIT` 再清一次，避免 database 跨测试泄漏。对于 `FJitRef_*` 的入参可使用稳定的伪地址/伪 id 字面量，只要能断言 lookup 数组追加即可，不要求真正执行 JIT。 |
| 期望行为 | 1. fresh `FJITDatabase::Clear()` 后，`Functions.Num()` 与所有 lookup array 都为 `0`。 2. 构造 `FStaticJITFunction` 后，`Functions` 恰好新增对应 `FunctionId`。 3. 构造各类 `FJitRef_*` 后，`FunctionLookups` / `SystemFunctionPointerLookups` / `TypeInfoLookups` / `GlobalVarLookups` / `PropertyOffsetLookups` 的数量各自加 `1`。 4. 再次 `Clear()` 后，这些容器全部归零，不残留前一次测试状态。 5. 若启用 offset verification，`VerifyPropertyOffsets` / `VerifyTypeSizes` / `VerifyTypeAlignments` 也会按构造次数增加并在 `Clear()` 后归零。 |
| 使用的 Helper | `FAngelscriptEngineScope`、`FJITDatabase::Clear()`、`FJITDatabase::Get()`、新增 `ResetJITDatabase()` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 12:06)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `Issue-39`、`NewTest-33`、`NewTest-34` 已写入本文；由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按这些编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 12:13)

### 一、现有测试问题

本轮已新增 `Issue-40`，详见本文对应条目。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `Issue-40` 已写入本文；由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按该编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-40 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

## 测试审查 (2026-04-09 12:04)

### 一、现有测试问题

#### Issue-39：`FAngelscriptDebuggerTestSession` 不保存进入前的 debug-break 全局状态，`bDisableDebugBreaks` 会把进程级开关错误恢复成 enabled

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.h` |
| 测试名 | `Angelscript.TestModule.Debugger.*`（当前 7 个用例未显式打开 `bDisableDebugBreaks`，但所有后续复用该 session helper 的 debugger 基础设施测试都会继承这个全局状态污染风险） |
| 行号范围 | `AngelscriptDebuggerTestSession.cpp:42-45, 87-90`；`AngelscriptDebuggerTestSession.h:17, 60`；关联全局状态定义：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp:12, 33-40` |
| 问题描述 | `FAngelscriptDebuggerSessionConfig` 只提供了 `bDisableDebugBreaks` 这个布尔开关，`Initialize()` 在它为真时直接调用 `AngelscriptDisableDebugBreaks()` 并把 `bRestoreDebugBreaksToEnabled = true`；`Shutdown()` 随后无条件调用 `AngelscriptEnableDebugBreaks()`。但运行时真实状态是 `Bind_Debugging.cpp` 里的进程级 `GDebugBreaksEnabled`。也就是说，如果进入 session 之前全局已经被其他测试或 harness 设成 disabled，这个 helper 并不会先快照旧值，而会在退出时强行恢复为 enabled。 |
| 影响 | 这是一个隐藏的进程级状态泄漏：shared session helper 会把“临时关闭 debug break 以避免 native break”错误地实现成“退出后永远打开 debug break”。后续任何依赖全局 debug-break 关闭状态的测试或工具链步骤，都会在跑过一次 `bDisableDebugBreaks=true` 的 debugger session 后变成顺序相关，导致 ensure/check/DebugBreak 行为与调用前环境不一致。 |
| 修复建议 | 不要用单个 `bRestoreDebugBreaksToEnabled` 表示“我改过状态”。应改成快照式恢复：1. 在 `Bind_Debugging` 暴露一个只读查询接口，或新增 `FScopedDebugBreakState`/`GetAreDebugBreaksEnabledForTesting()` helper；2. `Initialize()` 在真正修改前保存旧值；3. `Shutdown()` 只恢复到快照值，而不是一律 `Enable`。如果短期内不想暴露 getter，至少把 session 的 disable/restore 行为封成 RAII sentinel，并在测试端增加针对“初始 disabled 状态”的基础设施回归。 |

### 二、需要新增的测试

本轮先记录 `bDisableDebugBreaks` 的全局状态污染问题，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-39 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 11:32)

### 一、现有测试问题

#### Issue-36：`Breakpoint.HitLine` 只证明“至少停过一次”，却无法发现 `Continue` 后的重复停点

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.HitLine` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:158-163, 255-275, 357-365, 398-432` |
| 问题描述 | `HitLine` 把 monitor 配成 `MaxStopsToHandle = 1`，helper 在收到第 1 个 `HasStopped` 后就会 `SendContinue()`、递增 `StopsHandled`，随后立刻跳出循环并在退出时 `SendStopDebugging()`/`SendDisconnect()`。测试体本身也只断言 `MonitorResult.StopEnvelopes.Num() > 0`。结果是它只能证明“命中过一次断点”，却完全看不到 `Continue` 之后是否又错误地收到了第 2 个 `HasStopped`，例如 pause state 没清干净、同一断点重复触发、或 `Continue`/`HasContinued` 序列错乱。 |
| 影响 | 当前唯一的正向断点 happy-path 用例对“恢复执行后仍重复停下”不敏感。真实 IDE 会表现成点击 Continue 后又立刻回到同一停点、paused UI 清不掉或多发 stopped event，而 `Breakpoint.HitLine` 仍可能绿灯，因为 helper 已在第 1 个 stop 后主动收口。 |
| 修复建议 | 不要让 `HitLine` 用“一停就退”的 monitor。把它改成 transcript 型 collector，至少记录 `HasStopped`、`HasContinued` 到 invocation 完成为止，并显式断言“恰好 1 个 `HasStopped` + 恰好 1 个 `HasContinued` + 之后没有第 2 个 stop”。若不想重写，可直接复用已记录的 `NewTest-19` 所要求的 `CollectProtocolMessagesUntil(...)`/扩展型 `FBreakpointMonitorResult`，再把 `HitLine` 的 `Num() > 0` 升级成精确消息序列断言。 |

### 二、需要新增的测试

本轮先记录 `Breakpoint.HitLine` 的正向断言缺口，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-36 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 14:50)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-27：`TArray` 下标访问必须走 `StaticJITBinds` 的 custom native form，并保留越界保护

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` |
| 关联函数 | `FScriptFunctionNativeForm::GetNativeForm()`、`FScriptFunctionNativeForm::BindTArrayIndex()`、`FScriptNativeTArrayIndex::GenerateCustomCall()` |
| 现有测试覆盖 | 完全无直接测试；当前仓库唯一命中 `StaticJIT/` 的自动化仍然只是 `AngelscriptPrecompiledDataTests.cpp` 对 `PrecompiledData` 结构的 2 个 Editor-only 回归，测试侧对 `StaticJITBinds.cpp/.h` 依旧是 0 次命中 |
| 风险评估 | `array<T>.opIndex` 是脚本里极常见的容器路径。如果 template-instantiated method 没有通过 `GetNativeForm()` 回落到正确 native form，或 `GenerateCustomCall()` 丢了 `IsValidIndex`/`ThrowOutOfBounds`，JIT 生成代码就可能退化成错误调用、静默越界读，甚至直接生成不可编译的 C++；现有测试不会报警。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.NativeForms.TArrayIndexCustomCall` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITNativeFormTests.cpp`，单文件只放 native-form/container 方向 1-2 个用例，控制在 300-500 行 |
| 场景描述 | 编译一个最小脚本 fixture，例如 `int ReadMiddle() { array<int> Values = {11, 22, 33}; return Values[1]; }` 和 `int ReadInvalid() { array<int> Values = {11}; return Values[5]; }`。先触发 StaticJIT 生成并拿到对应函数的生成 C++ 文本，再定位 `array<int>.opIndex` 的 script function/native form 解析结果。必要时用非 Editor-only harness 执行生成后的 `ReadMiddle` / `ReadInvalid`。 |
| 输入/前置 | 复用 `FAngelscriptEngineScope`、`CompileModuleFromMemory`、`ExecuteIntFunction`；在 Shared 新增 `GenerateStaticJITSourceText(...)` 或 `RunStaticJITAndCollectOutput(...)` helper，返回目标函数的生成源码文本，并提供 `ResolveScriptFunctionByDecl(...)` 便于直接拿到 `array<int>.opIndex` 的 `asCScriptFunction*` 传给 `GetNativeForm()`。 |
| 期望行为 | 1. `GetNativeForm()` 对 fixture 中解析出的 `array<int>.opIndex` 返回非空。 2. 生成源码包含 `FArrayOperations::IsValidIndex`。 3. 生成源码包含 `OpIndex_Template_Unchecked`、`OpIndex_Stride_Unchecked` 或 `OpIndex_Unchecked` 之一，而不是退化成普通 generic call。 4. 生成源码包含 `FArrayOperations::ThrowOutOfBounds();`。 5. 若接 execution harness，`ReadMiddle()` 返回 `22`，`ReadInvalid()` 会触发“Index out of bounds.”异常而不是静默返回垃圾值。 |
| 使用的 Helper | `FAngelscriptEngineScope`、`CompileModuleFromMemory`、`ExecuteIntFunction`、新增 `GenerateStaticJITSourceText(...)` / `RunStaticJITAndCollectOutput(...)`、`ResolveScriptFunctionByDecl(...)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 14:43)

### 一、现有测试问题

#### Issue-34：`MakeUniqueDebugServerPort()` 只在 10 个端口里循环，`Shared` 自建 engine 场景存在端口冲突风险

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.*`（当前 7 个用例复用 `ExistingEngine`，但后续任何调用 `CreateTestingFullEngine` 的 debugger / StaticJIT 场景都会继承这个 helper） |
| 行号范围 | 12-20，54-57 |
| 问题描述 | `MakeUniqueDebugServerPort()` 用 `static int32 NextOffset` 在 `BasePort + ProcessBucket` 的 10 个槽位里循环分配端口，然后 `Initialize()` 在 `Config.ExistingEngine == nullptr` 时直接把这个端口塞进 `EngineConfig.DebugServerPort`。这既没有原子递增，也没有检测端口是否已被当前进程内的其他 test session 占用；同一进程里只要创建超过 10 个 session，或并行创建 session，就会重用旧端口。 |
| 影响 | 这会把未来 debugger / StaticJIT 测试的不稳定失败伪装成“连接不上 server”或“连到了错误会话”。尤其在自动化批量运行、测试重试、或还有旧 session 尚未完全释放 socket 时，端口冲突会直接削弱 Shared 基础设施的可复用性。 |
| 修复建议 | 不要手写 10 槽位轮转。更稳妥的做法是：1. 让 debug server 先 bind 到端口 `0`，再读取系统分配的实际端口；或 2. 至少把 `NextOffset` 改成跨进程/跨线程安全的单调递增计数，并在 bind 失败时重试下一个空闲端口。无论采用哪种方式，都应把“最终绑定到的实际端口”回写到 session，避免 helper 名字叫 `MakeUnique...` 却不保证唯一。 |

### 二、需要新增的测试

本轮先记录 `Shared` 端口分配 helper 的稳定性风险，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-34 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 14:36)

### 一、现有测试问题

#### Issue-33：`ClearThenResume` 两轮执行都不校验返回值，无法证明清断点前后控制流保持正确

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume` |
| 行号范围 | 497-518，550-568 |
| 问题描述 | 该用例首轮只在 513-518 断言“收到了 stop”且 `FirstInvocation->bSucceeded`，第二轮只在 566-567 断言“没有 stop”且 `SecondInvocation->bSucceeded`。但 `CreateBreakpointFixture()` 的 `RunScenario()` 正常返回值固定是 `8`。如果断点恢复后 PC 落错、脚本重复/漏执行 `Helper()`，或者 `ClearBreakpoints` 之后运行到了错误分支，只要脚本没有抛异常，这个用例仍会绿灯，因为它从头到尾都不检查 `FirstInvocation->Result` / `SecondInvocation->Result`。 |
| 影响 | 当前 `ClearThenResume` 只能部分证明“debugger 状态变化”，不能证明“脚本在两轮运行里都保持正确结果”。这会放过 resume 后控制流漂移、第二轮错误分支执行、以及断点清理后重复运行语义异常等回归。 |
| 修复建议 | 两轮 invocation 都补 `TestEqual(..., Result, 8)`，并把期望值抽到 fixture helper，例如 `Fixture.ExpectedReturnValue()`。首轮应在命中断点并继续后校验返回值仍为 `8`；第二轮应在“无 stop”之外再校验返回值仍为 `8`，把“断点命中语义”和“脚本执行语义”同时锁住。 |

### 二、需要新增的测试

本轮先记录 `ClearThenResume` 的返回值断言缺口，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-33 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 01:44)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮已新增 `NewTest-26`、`Issue-31`、`Issue-32`。由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按编号检索这些条目，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 14:58)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `Issue-33`、`Issue-34`、`NewTest-27` 已写入本文；由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按这些编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 01:42)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-26：`BuildIdentifier` 不匹配时必须把 stale precompiled cache 判为不可用

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FAngelscriptPrecompiledData::InitFromActiveScript()`、`Save()`、`Load()`、`IsValidForCurrentBuild()`，以及 `AngelscriptEngine.cpp:1512-1542` 的 precompiled cache discard 分支 |
| 现有测试覆盖 | 当前唯一的 `AngelscriptPrecompiledDataTests.cpp` 两个用例只手工构造 `asCModule` / `asCObjectType`，覆盖 high-bit flag roundtrip 与 module diff；完全不调用 `InitFromActiveScript()`、`Save()`、`Load()`、`IsValidForCurrentBuild()`，也不验证 build 配置不匹配时是否丢弃 cache |
| 风险评估 | 这条错误路径决定引擎启动时会不会错误接受旧 build 产物。若 `BuildIdentifier` 校验回归，Development/Test/Shipping 之间的 stale cache 可能被静默复用，最终表现为加载错误脚本元数据、JIT 映射错位，或启动期随机崩溃，而现有 StaticJIT 自动化完全不会报警。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.PrecompiledData.BuildIdentifierValidation` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataArchiveTests.cpp`，与后续 archive/error-path 用例共文件，控制在 300-500 行 |
| 场景描述 | 编译一个最小脚本 module，调用 `FAngelscriptPrecompiledData::InitFromActiveScript()` 生成 precompiled snapshot，并保存到临时 `.Cache` 文件。随后新建第二个 `FAngelscriptPrecompiledData` 实例执行 `Load()`，先验证正常 roundtrip；再把 `Loaded.BuildIdentifier` 改成一个明确错误的值（例如 `-1` 或 `GetCurrentBuildIdentifier() + 100`），模拟跨 build stale cache，最后走与 `AngelscriptEngine.cpp` 相同的 discard 判定。 |
| 输入/前置 | 复用 `CompileModuleFromMemory` / `FAngelscriptEngineScope`；在 Shared 新增 `FScopedTempPrecompiledCacheFile` helper 负责创建、清理临时 cache 文件，并提供 `SaveAndReloadPrecompiledData(...)` 便捷入口。若项目已有日志捕获 helper，可顺手复用以断言 warning 文案。 |
| 期望行为 | 1. 正常 roundtrip 后 `Loaded.IsValidForCurrentBuild()` 为 `true`。 2. 篡改 `BuildIdentifier` 后 `Loaded.IsValidForCurrentBuild()` 为 `false`。 3. 按 `AngelscriptEngine.cpp` 的启动分支处理 stale data 时，测试中的 `PrecompiledData` 指针会被丢弃/置空，不再进入后续 precompiled 使用路径。 4. 若接入日志捕获，应能看到“different build configuration / Discarding all precompiled data”类 warning。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptEngineScope`、`CompileModuleFromMemory`、`FAngelscriptPrecompiledData`、新增 `FScopedTempPrecompiledCacheFile` / `SaveAndReloadPrecompiledData(...)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingErrorPath: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 01:43)

### 一、现有测试问题

#### Issue-32：breakpoint / stepping 启动 helper 把“收到了 `DebugServerVersion` 信封”直接当成功，握手断言仍然过弱

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:60-80`；`AngelscriptDebuggerSteppingTests.cpp:58-79` |
| 问题描述 | `StartDebuggerSession()` / `StartSteppingDebuggerSession()` 在等到 `MessageType == DebugServerVersion` 后就直接返回成功，既不反序列化 `FDebugServerVersionMessage`，也不像 `Smoke.Handshake` 那样断言 `Session.GetDebugServer().bIsDebugging` 已进入 debugging 状态。结果是只要 server 还能发出一个同类型 envelope，即使 payload 版本错误、握手状态没有真正切到 debugging，甚至主 client 的 `StartDebugging` 只是被后续 monitor client 间接补救，6 个场景用例仍会把启动阶段视为成功。 |
| 影响 | 这会让 breakpoint / stepping 套件对握手回归过于宽松，尤其在只跑这些主题用例而不跑 smoke 时，错误会被推迟到“后面某个 stop 没到”才暴露，诊断信息更差，也削弱了这些场景测试自身的独立可信度。 |
| 修复建议 | 让两个启动 helper 和 `Smoke.Handshake` 使用同一套严格握手断言：1. 反序列化 `FDebugServerVersionMessage` 并检查 `DebugServerVersion == DEBUG_SERVER_VERSION`；2. 断言 `Session.GetDebugServer().bIsDebugging == true`；3. 若继续保留独立 helper，可抽一个 `AssertDebuggerHandshake(...)` 公共函数，统一校验 envelope 类型、payload 版本和 server debugging state，避免 smoke 严格、breakpoint/stepping 宽松的分叉。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-32 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 01:41)

### 一、现有测试问题

#### Issue-31：`CreateBreakpointFixture()` 提供了两个无效的 eval path，Shared helper 会误导后续 debugger 表达式测试

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.*`（未来复用 `CreateBreakpointFixture()` 做 evaluate/variables 时会直接受影响） |
| 行号范围 | 62-95 |
| 问题描述 | `CreateBreakpointFixture()` 生成的是两个 free function：`Helper()` 和 `RunScenario()`，没有任何类实例或成员字段。但它在 `EvalPaths` 里仍注册了 `ThisStoredValuePath -> "0:this.StoredValue"` 与 `ThisScopePath -> "0:%this%"`。这两个路径在该 fixture 的断点上下文里天然无效；真正存在的是 `StoredValue` / `LocalValue` 这类 local scope 变量。任何后续测试如果相信 fixture 暴露的 path 并据此调用 `RequestEvaluate` / `RequestVariables`，都会把 helper 自己的错误元数据误诊成 debugger 回归。 |
| 影响 | Shared fixture 本应降低写测试的成本，但这里反而埋入了“路径一定解析失败”的假数据。后续补条件断点、表达式求值或变量展开用例时，很容易复用这两个 path，最终得到脆弱的假失败，降低缺口补全效率。 |
| 修复建议 | 把这两个 path 从 `CreateBreakpointFixture()` 中删除，或改成与 free-function 场景一致的有效表达式，例如 `StoredValuePath -> "0:StoredValue"`、`LocalScopePath -> "0:%local%"`。更稳妥的做法是在 `FAngelscriptDebuggerScriptFixture` 层加一个自检 helper，例如 `ValidateEvalPathsForFixture()`，至少在测试启动时断言 `%this%` 只出现在类方法 fixture 中，避免再把无效路径静默带入后续 case。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:58)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-22：最后一个调试 client 的 `Disconnect` 必须清理 debugging state 与断点

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `Disconnect` 分支、`FAngelscriptDebugServer::Tick()` 中移除断开 client 的 cleanup 路径 |
| 现有测试覆盖 | 当前 7 个 debugger 用例只在 teardown 里把 `SendDisconnect()` 放在 `SendStopDebugging()` 之后调用，而且从不对 `Disconnect` 之后的 server 状态做断言；没有任何测试直接覆盖“最后一个 debugging client 先断开、再由 server 自动退出 debugging 并清断点” |
| 风险评估 | 如果 `Disconnect` 只关 socket 不清 `ClientsThatAreDebugging` / `bIsDebugging` / `Breakpoints`，IDE 异常退出后 server 会永久卡在 debugging 模式或残留旧断点，后续会话会收到脏 stop、残留断点或顺序相关失败。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Session.DisconnectClearsDebugState` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerLifecycleTests.cpp`，文件仅放 lifecycle 方向 1-2 个 `ASTEST_*` 用例，控制在 300-500 行 |
| 场景描述 | 复用 `CreateBreakpointFixture()`。进入 debugging 后先在 `BreakpointHelperLine` 注册一个断点并确认 `BreakpointCount == 1`，随后不发送 `StopDebugging`，直接 `SendDisconnect()` 并在本地 `Client.Disconnect()`，强制 server 走 `Disconnect -> Tick cleanup`。然后使用第二个 client 重新连接并再次 `StartDebugging()`。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`WaitForBreakpointCount(...)`；新增一个 `WaitForDebugServerIdle(Session)` helper，统一等待 `!bIsDebugging && BreakpointCount == 0`，避免把 cleanup 逻辑散在测试体里。 |
| 期望行为 | 1. 断开后 `Session.GetDebugServer().bIsDebugging` 最终变为 `false`。 2. `Session.GetDebugServer().BreakpointCount == 0`，证明最后一个 client 的移除路径确实触发了断点清理。 3. 第二个 client 能成功重新握手并收到 `DebugServerVersion`，且重新进入 debugging 前后没有额外残留消息。 4. 第二个会话在不重新下断点的情况下运行 `RunScenario()` 时不产生 `HasStopped`，返回值仍为 `8`。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture`、`WaitForBreakpointCount(...)`、新增 `WaitForDebugServerIdle(...)`、现有 `DispatchModuleInvocation(...)` / breakpoint monitor 的“不自动 StopDebugging”变体 |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 01:00)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-23：重复设置同一断点时必须返回 rejection ack，且不能重复计数

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `SetBreakpoint` 分支里“`Active->Lines.Contains(CodeLine)` / `CodeLine == -1` 时回发 `LineNumber = -1`”路径 |
| 现有测试覆盖 | 当前 3 个断点用例都只对同一行发送 1 次 `SetBreakpoint`，既没有设置 `Breakpoint.Id`，也没有等待 `SetBreakpoint` 回包；已记录的 `NewTest-6` 只覆盖“非代码行被挪到最近可执行行”的 ack，不覆盖“重复设置同一断点被拒绝”的分支 |
| 风险评估 | 如果 server 对重复断点重复计数、错误回包，或把第二次请求静默吞掉，IDE 会出现 ghost breakpoint、断点面板数量漂移，甚至同一行被重复 stop；现有套件不会发现。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Breakpoint.DuplicateSetReturnsRemovalAck` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointProtocolTests.cpp`，与 `NewTest-6` 共用 protocol helper，整文件保持在 300-500 行并按 `ASTEST_*` 风格拆成 2 个独立用例 |
| 场景描述 | 基于 `CreateBreakpointFixture()`，先发送第 1 个 `SetBreakpoint` 到 `BreakpointHelperLine`，显式设置 `Id = 201` 并确认 `BreakpointCount == 1`。随后对同一 `Filename` / `ModuleName` / `LineNumber` 再发送第 2 个 `SetBreakpoint`，`Id = 202`，等待 `SetBreakpoint` ack，再运行脚本一次。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`WaitForBreakpointCount(...)`；直接复用 `NewTest-6` 已要求增加的 `WaitForBreakpointAck(Timeout)` helper；执行阶段沿用现有 `FBreakpointMonitorConfig`，但要断言 `Error.IsEmpty()`、`!bTimedOut`。 |
| 期望行为 | 1. 第二次请求返回的 ack 存在且 `Id == 202`。 2. ack `Filename` 保持原始 fixture 文件名，`LineNumber == -1`，明确告诉前端该断点应被移除。 3. `Session.GetDebugServer().BreakpointCount` 在第 2 次请求后仍为 `1`，不会重复累加。 4. 运行 `RunScenario()` 时只收到 1 个 `HasStopped`，`Reason == "breakpoint"`，顶帧文件/行号命中 `BreakpointHelperLine`，最终返回值仍为 `8`。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture`、`WaitForBreakpointCount(...)`、`WaitForBreakpointAck(...)`、现有 `DispatchModuleInvocation(...)` 与 breakpoint monitor |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingEdgeCase: 1 |
| P2 | 0 | - |
| P3 | 0 | - |
 
---
 
## 测试审查 (2026-04-09 00:31)
 
### 一、现有测试问题
 
本轮无新增现有测试问题。
 
### 二、需要新增的测试
 
本轮无新增测试建议。
 
尾部索引说明：`2026-04-09 00:27`（`Issue-24`、`Issue-25`）已写入本文；由于文档历史段落存在时间戳乱序，继续审查时请按该时间戳检索本轮新增，不要只从文件尾部向上判断最新发现。
 
### 本轮汇总
 
**现有测试问题统计**
 
| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |
 
**新增测试建议统计**
 
| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:29)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：`2026-04-09 00:27`（`Issue-24`、`Issue-25`）已写入本文；由于文档历史段落存在时间戳乱序，继续审查时请按该时间戳检索本轮新增，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:43)

### 一、现有测试问题

#### Issue-27：现有 debugger happy-path 全部把 `StartDebugging` 的 adapter version 硬编码成 `2`，不会自动覆盖当前协议版本

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerSmokeTests.cpp:49-49`；`AngelscriptDebuggerBreakpointTests.cpp:49-50, 193-193`；`AngelscriptDebuggerSteppingTests.cpp:47-50, 198-198` |
| 问题描述 | 现有 7 个 debugger 用例以及两个 monitor helper 全都调用 `SendStartDebugging(2)`。而 runtime 在 `FAngelscriptDebugServer::HandleMessage(StartDebugging)` 中会把 `AngelscriptDebugServer::DebugAdapterVersion` 直接设为客户端传入值，再按该版本走后续序列化兼容分支。结果是这些 happy-path 测试长期固定在 adapter v2 行为上，即使未来 `DEBUG_SERVER_VERSION` 前进，现有主线回归也不会自动覆盖新版本协商出来的协议路径。 |
| 影响 | 这会让“当前协议版本”的回归保护失真。只要服务器继续兼容 v2，breakpoint/stepping/smoke 主路径就可能全部绿灯，同时把仅在新 adapter version 下才会暴露的 payload 变化、字段新增或默认行为差异完全漏掉。 |
| 修复建议 | 把共享启动 helper 的默认 adapter version 改成 `DEBUG_SERVER_VERSION` 或 `FAngelscriptDebuggerSessionConfig::RequestedDebugAdapterVersion`，让 happy-path 始终跟随当前协议版本；旧版本兼容验证则保留为单独的 legacy tests，例如已记录的 `NewTest-20`。同时在 `Smoke.Handshake`、`StartDebuggerSession()`、`StartSteppingDebuggerSession()` 成功握手后补一条断言，验证 `Session.GetDebugServer().DebugAdapterVersion` 等于本次请求值，避免 helper 悄悄落回旧分支。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-27 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:43)

### 一、现有测试问题

#### Issue-26：两个 monitor helper 忽略 `Continue` / `Step*` / `RequestCallStack` 的发送结果，协议写失败会被伪装成超时或 stop 数量不匹配

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.HitLine`、`Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume`、`Angelscript.TestModule.Debugger.Breakpoint.IgnoreInactiveBranch`、`Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:237-257`；`AngelscriptDebuggerSteppingTests.cpp:242-283` |
| 问题描述 | `StartBreakpointMonitor()` 在命中 `HasStopped` 后，只把 `SendRequestCallStack()` 放在 `if (...)` 里，若发送失败就直接跳过 callstack 请求；`SendContinue()` 也完全忽略返回值。`StartStepMonitor()` 同样对 `SendRequestCallStack()`、`SendContinue()`、`SendStepIn()`、`SendStepOver()`、`SendStepOut()` 全部只发不查。这样一旦 monitor socket 在暂停阶段断开、写缓冲塞满、或 server 已提前关闭连接，helper 不会立刻记录 `Result.Error`，而是把故障延后成“没有 callstack”“后续 stop 数量不对”或整轮 invocation timeout。 |
| 影响 | 这会把最关键的协议写失败原因吞掉，导致 breakpoint/stepping 用例输出和真实故障脱节。调试器回归时，测试日志往往只会看到模糊的 `Stops.Num()` 不匹配、`Callstack.IsSet()==false` 或 invocation 超时，而看不到是 monitor 命令根本没发出去。 |
| 修复建议 | 把 monitor 的主动发送动作统一收敛到一个 `SendOrFail(Result, MonitorClient, Context, Lambda)` helper：对 `RequestCallStack`、`Continue`、`StepIn`、`StepOver`、`StepOut` 逐一检查返回值；失败时立刻写入 `Result.Error = FString::Printf(TEXT(\"%s failed: %s\"), Context, *MonitorClient.GetLastError())`，并终止当前 monitor 循环。上层用例随后统一先断言 `Result.Error.IsEmpty()`，不要让发送失败退化成后置超时。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-26 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:27)

### 一、现有测试问题

#### Issue-24：`StartStepMonitor()` 只在“零次 stop”时标记超时，步进序列中途卡住会被伪装成普通断言失败

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | 172-301，419-455，527-657 |
| 问题描述 | `StartStepMonitor()` 在循环结束后只有 `StopsHandled == 0` 且到达 `MonitorEnd` 时才写 `Result.bTimedOut = true`（293-296）。这意味着一旦测试已经收到第 1 个 `HasStopped`，但后续 `StepIn` / `StepOver` / `StepOut` 没有再返回预期 stop，helper 会直接带着 `Error.IsEmpty()`、`bTimedOut == false` 退出；上层 3 个用例最终只看到 `Stops.Num()` 不等于 2/3，无法知道真实原因是“第二段步进超时”，还是“协议发回了错误 stop 序列”。 |
| 影响 | 步进语义回归时，测试输出会退化成模糊的 stop 数量不匹配，掩盖真正的 timeout/挂起问题；Shared monitor 也因此不能稳定区分“完全没停下”和“停下后继续命令失效”两类故障。 |
| 修复建议 | 把超时判定改成与期望 phase 数量绑定：只要 `StopsHandled < Phases.Num()` 且到达 `MonitorEnd`，就写入 `Result.bTimedOut = true` 并补充 `Result.Error = FString::Printf(TEXT(\"Timed out after %d/%d stops\"), StopsHandled, Phases.Num())`。上层 `StepIn/StepOver/StepOut` 统一先断言 `!MonitorResult.bTimedOut`、`MonitorResult.Error.IsEmpty()`，再比较 `Stops.Num()` 和逐 stop 语义。 |

#### Issue-25：`IgnoreInactiveBranch` 没有断言返回值，实际上无法证明脚本真的留在活跃分支

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.IgnoreInactiveBranch` |
| 行号范围 | 572-650 |
| 问题描述 | 这个用例的目标是验证“把断点设在 `BreakpointInactiveBranchLine` 上时，因为 `StartValue > 0`，脚本不会走到 inactive branch”。但当前结尾只检查 `MonitorResult.StopEnvelopes.Num() == 0` 和 `InvocationState->bSucceeded`（649-650），没有验证 `RunScenario()` 的实际返回值。根据 fixture，活跃分支返回 `Helper(3) == 8`，而一旦误走到 inactive branch 会返回 `Helper(-1) == 4`。也就是说，当前测试并没有真正证明“分支没有执行”，只证明“没有停下且没有抛错”。 |
| 影响 | 如果未来断点注册失效、行映射漂移、或控制流回归导致脚本意外落入 inactive branch，但调试器恰好又没有在该行停下，这个用例仍可能绿灯，无法完成它宣称的“分支未执行”验证。 |
| 修复建议 | 在现有 `bSucceeded` 断言后补 `TestEqual(TEXT(\"Debugger.Breakpoint.IgnoreInactiveBranch should keep the active-branch return value\"), InvocationState->Result, 8)`；更稳妥的做法是把 fixture 预期值抽成 `ExpectedActiveBranchResult`，让测试同时表达“无 stop”与“返回值仍为活跃分支结果”这两个条件。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-24 |
| WeakAssertion | 1 | Issue-25 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:22)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：`2026-04-09 00:17`（`Issue-22`）、`2026-04-09 00:19`（`NewTest-21`）与 `2026-04-09 00:21`（`Issue-23`）均已写入本文；继续审查时请按这些时间戳检索，避免只从文件尾部向上阅读时遗漏本轮新增。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:30)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：`2026-04-09 00:27`（`Issue-24`、`Issue-25`）已写入本文；由于文档历史段落存在时间戳乱序，继续审查时请按该时间戳检索本轮新增，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:19)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-21：`ClearBreakpoints` 只清目标模块/文件，不能误删其他断点

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `ClearBreakpoints` 分支、`FAngelscriptClearBreakpoints` |
| 现有测试覆盖 | 有 `Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume`，但该用例已被 `Issue-15` 证明会被 monitor 自己的 `StopDebugging` 旁路，而且整个仓库没有任何场景同时放置两个独立断点后再验证“只清其中一个” |
| 风险评估 | 如果 `ClearBreakpoints` 误按错误 key 清空、错误地把其他 module/file 的断点也删掉，或者只清空 `BreakpointCount` 没清具体 `Lines`，当前套件完全不会报错；真实 IDE 会表现为“删除一个文件的断点顺带清掉别的断点”或“UI 显示删掉了但另一模块仍会停下”。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Breakpoint.ClearTargetPreservesOtherBreakpoints` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointClearTests.cpp` |
| 场景描述 | 构造两个独立 breakpoint fixture，使用不同 `ModuleName`/`Filename`。分别在两个 fixture 的 `BreakpointHelperLine` 设置断点，确认两条断点都注册成功后，只对 fixture A 发送一次 `ClearBreakpoints`。随后先运行 fixture A，再运行 fixture B。 |
| 输入/前置 | 在 Shared 增加 `CreateNamedBreakpointFixture(ModuleName, Filename)` 或等价 clone helper；复用现有 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`WaitForSpecificBreakpoint(...)`；同时需要一个不在退出时自动 `SendStopDebugging()` 的 monitor 变体，避免再次落入 `Issue-15`。 |
| 期望行为 | 1. 清除后 `DebugServer.Breakpoints` 中 fixture A 对应条目为空，而 fixture B 的 `BreakpointHelperLine` 仍存在。 2. 运行 fixture A 时 `StopEnvelopes.Num() == 0`、`Error.IsEmpty()`、`!bTimedOut`，返回值仍为 `8`。 3. 运行 fixture B 时恰好收到 1 个 `HasStopped`，`Reason == "breakpoint"`，顶帧 `Source`/`LineNumber` 命中 fixture B 的 helper marker。 4. 整个过程中 `Session.GetDebugServer().BreakpointCount` 从 `2 -> 1`，不会错误归零。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture`、`WaitForSpecificBreakpoint(...)`、新增 `CreateNamedBreakpointFixture(...)`、调整后的 `StartBreakpointMonitor(...)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:21)

### 一、现有测试问题

#### Issue-23：两个 monitor helper 对 `CallStack` 响应使用硬编码 10 秒等待，超时后也不会显式报错

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.HitLine`、`Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:237-251`；`AngelscriptDebuggerSteppingTests.cpp:253-265` |
| 问题描述 | `StartBreakpointMonitor()` 和 `StartStepMonitor()` 在收到 `HasStopped` 后，如果 `bRequestCallstack` 为真，就固定轮询 10 秒等待 `EDebugMessageType::CallStack`。这段等待既不复用 `Session.GetDefaultTimeoutSeconds()` / `Config.TimeoutSeconds`，也不会在超时后写入 `Result.Error` 或 `bTimedOut`；helper 只是静默继续执行，留给上层一个 `Callstack.IsSet() == false` 的结果。 |
| 影响 | 这会把“`CallStack` 没回来”伪装成普通断言失败，掩盖真实的协议超时；同时还引入与 session timeout 脱节的第二套硬编码时钟。环境稍慢时，当前用例可能在 10 秒 callstack 子等待里失败；协议回归时，测试日志也只会看到“没有 callstack”，而看不到明确的超时原因。 |
| 修复建议 | 把 callstack 等待抽成统一 helper，例如 `WaitForRequiredMessageType(EDebugMessageType::CallStack, TimeoutSeconds)`：timeout 使用 monitor/config 传入的总窗口或独立可配置值；若超时，立即写入 `Result.Error = "Timed out waiting for CallStack after HasStopped"` 并终止本次 monitor。上层正向用例再统一断言 `Result.Error.IsEmpty()`，不要依赖 `Callstack.IsSet()` 单独推断协议健康状态。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-23 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:17)

### 一、现有测试问题

#### Issue-22：`StepOut` 从未校验首个 stop 是否真的停在 caller 的断点行

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | 629-652 |
| 问题描述 | `StepOut` 在 `MonitorResult.Stops.Num() == 3` 之后，只检查第 2 个 stop（已经 `StepIn` 进 `Inner()`）和第 3 个 stop（`StepOut` 回到 after-call 行）的 callstack；`MonitorResult.Stops[0]` 从未被反序列化，也没有任何 `Reason`、`Callstack`、`LineNumber` 或 `Source` 断言。也就是说，这个用例并没有证明“首个 stop 确实是设在 `StepCallLine` 的 breakpoint”，只证明后两个阶段看起来像一次 `StepIn`/`StepOut`。 |
| 影响 | 如果初始断点漂移到错误行、残留断点先命中、或首个 stop 其实来自其他原因，当前 `StepOut` 仍可能通过。这样会把“StepOut 从正确起点开始工作”退化成“某条三段 stop 序列看起来合理”，不足以支撑单文件 `step out` 已覆盖的结论。 |
| 修复建议 | 在 `StepOut` 中补齐和 `StepIn`/`StepOver` 同级的基线断言：1. 反序列化 `Stops[0].StopEnvelope` 并断言 `Reason == "breakpoint"`；2. 断言 `Stops[0].Callstack.IsSet()` 且 `Frames.Num() >= 1`；3. 顶帧 `Source` 以 `Fixture.Filename` 结尾、`LineNumber == Fixture.GetLine(TEXT("StepCallLine"))`；4. 可选再断言首个 stop 的 frame depth 为 1，明确它确实停在 caller 顶层 call-site。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-22 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:08)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

补充索引说明：`2026-04-09 00:04` 这一轮新增了 `Issue-21`；`2026-04-09 00:06` 这一轮新增了 `NewTest-19`、`NewTest-20`。继续人工审查时请按这两个时间戳检索本文，避免按文件尾部阅读时遗漏。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:06)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-19：断点恢复后必须发出 `HasContinued`，否则前端无法可靠清除 paused 状态

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 关联函数 | `FAngelscriptDebugServer::PauseExecution()`、`FAngelscriptDebugServer::HandleMessage()` 的 `Continue` 分支、`EDebugMessageType::HasContinued` |
| 现有测试覆盖 | `Debugger/` 现有 7 个用例都会发送 `Continue` 或 `Step*`，但 monitor 只消费 `HasStopped` / `CallStack`，从不校验 `HasContinued`。仓库里唯一和 `Pause` 靠近的自动化只是 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` 对空 body envelope 的 transport roundtrip，并不是调试场景覆盖。 |
| 风险评估 | 若 `PauseExecution()` 不再发送 `HasContinued`，或 `Continue` 只清了 `bIsPaused` 没有广播恢复事件，现有 breakpoint/stepping 套件依然会绿灯，但真实 IDE 前端会卡在 paused UI、状态栏不同步，属于高频用户路径的协议漏测。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Protocol.ContinueEmitsHasContinued` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerResumeProtocolTests.cpp` |
| 场景描述 | 复用 `CreateBreakpointFixture()`，在 `BreakpointHelperLine` 断住。收到首个 `HasStopped` 后，由 monitor 发送一次 `Continue`，并继续监听直到脚本 invocation 完成。 |
| 输入/前置 | 在 Shared 新增 `CollectProtocolMessagesUntil(...)` 或扩展现有 `FBreakpointMonitorResult`，允许同一个 monitor 同时记录 `HasStopped`、`HasContinued` 与可选 `CallStack`，避免像当前 helper 那样只盯单一消息类型。 |
| 期望行为 | 1. 首个 stop 反序列化后 `Reason == "breakpoint"`，顶帧文件/行号命中 `BreakpointHelperLine`。 2. monitor 在发送 `Continue` 后，恰好收到 1 条 `HasContinued`，且它出现在 invocation 完成之前。 3. `HasContinued` 之后不再出现第 2 个 `HasStopped`。 4. invocation 成功完成且返回值仍为 `8`。 |
| 使用的 Helper | `FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture`、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、现有异步 invocation helper，外加新增 `CollectProtocolMessagesUntil(...)` |
| 优先级 | P1 |

#### NewTest-20：用 adapter version 1 跑一次 evaluate/variables，补齐 legacy payload 分支

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `FStartDebuggingMessage`、`FAngelscriptVariable::operator<<`、`FAngelscriptDebugServer::HandleMessage()` 的 `StartDebugging` / `RequestEvaluate` / `RequestVariables` 分支 |
| 现有测试覆盖 | 现有 7 个 debugger 用例都硬编码 `SendStartDebugging(2)`；已记录的 `NewTest-7`、`NewTest-10` 也都基于 adapter v2。也就是说 `FAngelscriptVariable` 在 `DebugAdapterVersion < 2` 时“不序列化 `ValueAddress` / `ValueSize`”这条兼容分支目前是零场景覆盖。 |
| 风险评估 | 一旦 legacy adapter 分支退化，旧版 IDE/调试器会在变量面板或 hover evaluate 上直接读错 payload 长度，表现为变量列表空白、反序列化失败或后续消息错位；而当前套件因为只跑 v2，完全不会报错。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Evaluation.AdapterV1LegacyPayload` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerEvaluationTests.cpp` |
| 场景描述 | 复用 `CreateCallstackFixture()` 与 `DispatchGeneratedIntInvocation(...)`，但把启动握手改成 `StartDebugging(1)`。在 `CallstackLeafLine` 断住后，请求一次 `Evaluate(LeafCombinedPath)` 和一次 `Variables(0:%local%)`。 |
| 输入/前置 | 复用 `NewTest-7` 需要的 generated-object invocation helper；在 Shared 把 `StartDebuggerSession(...)` 参数化为 `AdapterVersion`，避免再把 version 写死为 `2`。 |
| 期望行为 | 1. `Evaluate` 回包能被成功反序列化，`Name`/`Value`/`Type` 分别等于 `Combined`、`16`、`int`。 2. `Variables` 回包至少包含 `LocalValue = 4` 与 `Combined = 16`。 3. 在 adapter v1 会话里，所有返回项的 `ValueAddress == 0` 且 `ValueSize == 0`，证明 payload 走的是 legacy 结构而不是错误混入 v2 字段。 4. invocation 最终成功完成并返回 `16`。 |
| 使用的 Helper | `FAngelscriptDebuggerScriptFixture::CreateCallstackFixture`、`DispatchGeneratedIntInvocation(...)`、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`SendRequestEvaluate`、`SendRequestVariables`、新增参数化 `StartDebuggerSession(AdapterVersion)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 2 | MissingScenario: 1, MissingEdgeCase: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:04)

### 一、现有测试问题

#### Issue-21：`Smoke.Handshake` 仍使用 5 秒默认 session timeout，和其余 debugger 用例的 45 秒窗口不一致

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake` |
| 行号范围 | 18-29，55-132 |
| 问题描述 | `Smoke.Handshake` 只设置了 `SessionConfig.ExistingEngine` 就直接 `Session.Initialize(SessionConfig)`，没有像 `StartDebuggerSession()` / `StartSteppingDebuggerSession()` 那样把 `SessionConfig.DefaultTimeoutSeconds` 提升到 `45.0f`。`FAngelscriptDebuggerSessionConfig` 和 `FAngelscriptDebuggerTestSession` 的默认值都是 `5.0f`，因此这个冒烟用例在共享 production engine 上完成 `DebugServerVersion`、`BreakFilters`、`StopDebugging` 等整套交互时，使用的是明显更短的超时窗口。 |
| 影响 | 这会让 smoke 成为 debugger 套件里最容易因编辑器负载、socket 抖动或诊断消息延迟而误报失败的用例。结果是同一套 handshake 在 breakpoint/stepping 测试里能过，在 smoke 里却因为 5 秒窗口过短而随机超时，降低“调试器是否可启动”的信号质量。 |
| 修复建议 | 让 smoke 复用统一的启动 helper，或至少显式加上 `SessionConfig.DefaultTimeoutSeconds = 45.0f`，把 session timeout 与 breakpoint/stepping 套件对齐。更稳妥的做法是把 debugger 测试统一改成一个 `CreateProductionDebuggerSessionConfig()` helper，由它集中设置 `ExistingEngine`、timeout 和清理策略，避免 smoke 再次漂移。 |

### 二、需要新增的测试

本轮先记录新增现有测试问题，新增测试建议将在后续分段追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-21 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 23:55)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

补充索引说明：`2026-04-08 23:53` 这一轮已写入文档，包含 `Issue-20`；阅读顺序请以时间戳为准继续向后查看。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 23:53)

### 一、现有测试问题

#### Issue-20：启动阶段的手写收包循环会直接吞掉非目标消息，现有 handshake 断言无法发现协议污染

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerSmokeTests.cpp:55-123`；`AngelscriptDebuggerBreakpointTests.cpp:41-72`；`AngelscriptDebuggerSteppingTests.cpp:39-71` |
| 问题描述 | 这 3 处启动/握手代码都在 `Session.PumpUntil(...)` 里直接调用 `Client.ReceiveEnvelope()`，然后只在 `Envelope->MessageType == DebugServerVersion`（smoke 里后半段是 `BreakFilters`）时才记录结果；任何其它 envelope 都被当场丢弃，没有入队、没有报错、也没有断言“这条消息本不该出现”。因此如果 debug server 因残留状态或协议回归在 handshake 阶段额外发出了 `HasStopped`、`Diagnostics`、`SetBreakpoint` ack、`ClearDataBreakpoints` 等消息，现有 7 个用例仍可能继续绿灯，只要目标消息最终也到了。 |
| 影响 | 这些测试名义上在验证“干净的 handshake / startup contract”，实际却容忍启动阶段的协议污染。它会掩盖 session 泄漏、意外 stopped event、或 server 在 `StartDebugging` 后发送了额外 side-channel 消息的问题；后续真实客户端则可能把这些额外消息当成当前会话的一部分并发生状态错乱。 |
| 修复建议 | 把启动阶段改成“消费并验证完整消息序列”，不要再手写丢消息循环。最小修复是抽一个 `WaitForExclusiveMessageType(...)` helper：内部复用 pending-queue 方案，若在 handshake 阶段收到除允许集合外的任何 envelope，立即 `AddError` 并失败。对 `Smoke.Handshake`，建议显式断言 `StartDebugging` 后首个响应就是 `DebugServerVersion`，`RequestBreakFilters` 后首个相关响应就是 `BreakFilters`，且过程中没有额外消息残留。 |

### 二、需要新增的测试

本轮先记录 handshake 断言边界问题，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-20 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 23:38)

### 一、现有测试问题

#### Issue-17：`DeserializeMessage<T>()` 默认接受 trailing bytes，Shared client 会把协议漂移误判成反序列化成功

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Angelscript.TestModule.Debugger.Breakpoint.HitLine`、`Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | `AngelscriptDebuggerTestClient.h:43-54`；使用点：`AngelscriptDebuggerSmokeTests.cpp:81-123`、`AngelscriptDebuggerBreakpointTests.cpp:410-423`、`AngelscriptDebuggerSteppingTests.cpp:424-449, 532-551, 634-652` |
| 问题描述 | `FAngelscriptDebuggerTestClient::DeserializeMessage<T>()` 只检查 `Reader.IsError()`，没有验证 `Reader.AtEnd()`。这意味着消息体只要前半段仍能按旧结构读出来，尾部新增字段会被直接忽略，helper 仍返回 `IsSet()`。当前 smoke / breakpoint / stepping 用例都把这个 helper 当作“payload 与预期结构完全一致”的证据，因此像 `CallStack` 这类正在演进的消息，即使测试端没有真正消费完整 body，也可能继续绿灯。 |
| 影响 | Shared 基础设施会把协议字段漂移、可选字段读取缺失、尾部兼容错误静默吞掉，降低现有 7 个 debugger 用例对 wire format 回归的敏感度。结果是“测试通过，但真实客户端解析错位”的问题会被系统性漏检。 |
| 修复建议 | 把 `DeserializeMessage<T>()` 改成 strict 模式：`Reader << Value` 后同时要求 `!Reader.IsError()` 且 `Reader.AtEnd()`；若需要兼容“允许尾部扩展”的历史消息，单独提供显式的 `DeserializeMessageAllowTrailingBytes<T>()`，由调用点按协议版本选择。对 `Smoke.Handshake`、`HitLine` 和 `Step*` 这类集成测试，默认应使用 strict helper，确保 payload 结构变化会直接让测试失败。 |

---

## 测试审查 (2026-04-08 14:22)

### 一、现有测试问题

#### Issue-16：test client 使用 non-blocking socket，却没有处理 `would block`/部分发送，协议消息发送存在随机失败窗口

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | 48-74，97-129 |
| 问题描述 | `Connect()` 在创建 socket 后立刻 `SetNonBlocking(true)`；但 `SendRawEnvelope()` 的循环里一旦 `Socket->Send(...)` 返回 `false`，就直接报错退出，既不读取 `ISocketSubsystem::GetLastErrorCode()`，也不对 `SE_EWOULDBLOCK`/`SE_EINPROGRESS` 做等待重试。对 non-blocking socket 来说，连接刚建立、发送窗口暂满、或编辑器线程负载较高时都可能出现暂时不可写；当前实现会把这种瞬时 backpressure 当成协议失败。 |
| 影响 | 现有 7 个用例所有控制消息都走这条路径，包括 `StartDebugging`、`SetBreakpoint`、`Continue`、`Step*`、`StopDebugging`。一旦出现暂时不可写，测试会偶发报错为“发送消息失败”，而不是稳定重试到 session timeout；后续新增 `RequestDebugDatabase` 这类更重的协议测试时风险更高。 |
| 修复建议 | 要么把 test client socket 改回 blocking，并把 timeout 收敛到外层 `PumpUntil`；要么保留 non-blocking，但在 `SendRawEnvelope()` 里处理 `SE_EWOULDBLOCK`/`SE_EINPROGRESS`，通过 `Socket->Wait(ESocketWaitConditions::WaitForWrite, Timeout)` 或轮询 `GetConnectionState()` 后重试，同时正确处理 `BytesSent < RemainingBytes` 的部分发送。建议把 send timeout 也暴露到 `FAngelscriptDebuggerSessionConfig`，与 connect/receive timeout 统一。 |

### 二、需要新增的测试

本轮先记录 Shared socket 发送风险，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-16 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 13:44)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

补充索引说明：`2026-04-08 13:42` 这一轮已写入文档，包含 `NewTest-10` 至 `NewTest-13`；阅读顺序请以时间戳为准继续向后查看。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

## 测试审查 (2026-04-08 13:44)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

补充索引说明：`2026-04-08 13:42` 这一轮已写入文档，包含 `NewTest-10` 至 `NewTest-13`；阅读顺序请以时间戳为准继续向后查看。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 13:42)

### 一、现有测试问题

本轮未新增现有测试质量问题，继续补 debugger 协议缺口与 `StaticJIT/` 剩余无覆盖文件。

### 二、需要新增的测试

#### NewTest-10：用 adapter v2 的 `ValueAddress` 驱动一次真正的数据断点命中与自动清理

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `FAngelscriptDataBreakpoint`、`FAngelscriptClearDataBreakpoints`、`FAngelscriptDebugServer::ProcessScriptLine()`、`FAngelscriptDebugServer::HandleMessage()` 的 `SetDataBreakpoints` / `ClearDataBreakpoints` 分支 |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | data breakpoint 是 debugger 的核心协议之一。当前没有任何自动化证明“地址/大小来自 evaluate 后能正确触发 watchpoint”、“触发后会发 `ClearDataBreakpoints` 回包”、“hit count 达到后 authoritative state 会被清空”。这类回归只能靠手工调试发现。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.DataBreakpoint.LocalValueHitCount` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointTests.cpp` |
| 场景描述 | 复用 `CreateSteppingFixture()`，先在 `StepAfterCallLine` 断住，发送 `RequestEvaluate(Fixture.GetEvalPath(TEXT("ResultPath")))` 读取局部变量 `Result` 的 `ValueAddress/ValueSize`，随后注册 `Id = 11`、`HitCount = 1` 的 data breakpoint，并继续执行 `Result += 1`。 |
| 输入/前置 | session 必须用 adapter version 2；在 Shared 为 test client 增加 `SendSetDataBreakpoints(...)` / `WaitForClearDataBreakpoints(...)` convenience helper，或直接复用 `SendTypedMessage(EDebugMessageType::SetDataBreakpoints, ...)` 与 `WaitForTypedMessage<FAngelscriptClearDataBreakpoints>(...)`。 |
| 期望行为 | 1. evaluate 回包 `ValueAddress != 0` 且 `ValueSize > 0`。 2. 继续执行后收到 1 个 `HasStopped`，`Reason == "exception"`，`Text` 包含 `Data breakpoint (Result) triggered!`。 3. 紧接着收到 `ClearDataBreakpoints` 回包，`Ids` 仅含 `11`。 4. 同一 session 再次运行脚本时不再因该 watchpoint 停下，且最终返回值仍为 `14`。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateSteppingFixture`、`SendRequestEvaluate`、新增 `SendSetDataBreakpoints(...)` / `WaitForClearDataBreakpoints(...)` |
| 优先级 | P0 |

#### NewTest-11：把 `BreakFilters` 从“能反序列化”升级为“内容正确且可回归”

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `RequestBreakFilters` 分支、`FAngelscriptRuntimeModule::GetDebugBreakFilters()` |
| 现有测试覆盖 | 有 `Smoke.Handshake`，但只验证收到了 `BreakFilters` 并完成反序列化，没有任何 payload 内容断言 |
| 风险评估 | 一旦 delegate 绑定丢失、filter/title 配对错位、或请求过程意外改变 debugging 状态，当前 smoke 仍会绿灯。IDE 前端会直接表现为“过滤器列表为空”或“标题与 filter key 对不上”。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Smoke.BreakFiltersRoundtrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeProtocolTests.cpp` |
| 场景描述 | 在测试开始前暂时绑定 `FAngelscriptRuntimeModule::GetDebugBreakFilters()`，返回一个固定 map，例如 `{ break:ensure -> Ensure, break:script -> Script }`；进入 debugging 后发送 `RequestBreakFilters`。 |
| 输入/前置 | 在 Shared 新增 `FScopedDebugBreakFiltersBinding`，负责保存并恢复原 delegate；测试仍复用现有 handshake/session/client 流程。 |
| 期望行为 | 1. `BreakFilters.Filters.Num() == 2` 且 `FilterTitles.Num() == 2`。 2. 按 pair 组装后的结果集精确等于 `{("break:ensure","Ensure"), ("break:script","Script")}`，不依赖 map 遍历顺序。 3. 请求前后 `Session.GetDebugServer().bIsDebugging` 仍为 `true`，说明协议查询没有副作用。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、新增 `FScopedDebugBreakFiltersBinding`、`WaitForTypedMessage<FAngelscriptBreakFilters>` |
| 优先级 | P2 |

#### NewTest-12：验证 `FormatAngelscriptCallstack()` 绑定返回的是可读的脚本栈，而不是空字符串

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `BindGlobalFunction("FString FormatAngelscriptCallstack()", ...)`、`FAngelscriptEngine::FormatAngelscriptCallstack()` |
| 现有测试覆盖 | 完全无测试；`CreateBindingFixture()` 中的 `FormatCurrentCallstack()` marker 也从未被现有 7 个 debugger 用例使用 |
| 风险评估 | 一旦绑定丢失、格式化入口返回空串、或脚本文件/函数名没有被带入，`ensure` / `check` / debug diagnostics 的可读性会直接下降，而当前自动化没有任何信号。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Binding.FormatCallstackString` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBindingCallstackTests.cpp` |
| 场景描述 | 扩展 `CreateBindingFixture()`，增加一个 wrapper `UFUNCTION() FString TriggerFormattedCallstack() { return FormatCurrentCallstack(); }`。测试通过生成类实例调用该 wrapper，读取返回的 `FString`。 |
| 输入/前置 | 在 Shared 新增 `DispatchGeneratedStringInvocation(...)` helper，负责在 GameThread 上调用返回 `FString` 的 `UFUNCTION`；fixture 继续使用 `CompileAnnotatedModuleFromMemory`。 |
| 期望行为 | 1. 返回字符串非空。 2. 文本包含 `TriggerFormattedCallstack` 与 `FormatCurrentCallstack` 两个函数名。 3. 文本包含 `DebuggerBindingFixture.as`，证明源码文件信息被格式化进结果。 |
| 使用的 Helper | `FAngelscriptDebuggerScriptFixture::CreateBindingFixture`、`FindGeneratedClass` / `FindGeneratedFunction`、新增 `DispatchGeneratedStringInvocation(...)` |
| 优先级 | P1 |

#### NewTest-13：为 `TPrecompiledAllocator` 建一个最小的 grow/copy/move 单测，补掉 `PrecompiledDataAllocator.h` 的直接覆盖

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledDataAllocator.h` |
| 关联函数 | `TPrecompiledAllocator<>::ForElementType::ResizeAllocation()`、`MoveToEmpty()`、`GetAllocation()` |
| 现有测试覆盖 | 完全无直接测试；当前只是在 `PrecompiledData.h` include 链里被顺带编译 |
| 风险评估 | 这个 allocator 承载了几乎所有 `FAngelscriptPrecompiled*` 容器。若 grow 时 copy 长度、对齐或 `MoveToEmpty()` 语义出错，会在 precompiled load 阶段造成静默内存破坏，而现有 `PrecompiledData` 两个回归用例完全测不到。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.PrecompiledAllocator.ResizeAndMove` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledAllocatorTests.cpp` |
| 场景描述 | 用 `TPrecompiledAllocator<>::ForElementType<int32>` 先分配 2 个元素写入固定值，再 grow 到 4 个元素，最后把 allocator `MoveToEmpty()` 到另一个实例。 |
| 输入/前置 | 用 `FMemMark Mark(GScriptPreallocatedMemStack)` 包裹测试，确保 mem-stack 状态在退出时恢复；无需脚本引擎，保持纯 C++ unit test。 |
| 期望行为 | 1. grow 后前两个元素值仍等于初始写入值。 2. `GetAllocation()` 非空且满足 `alignof(int32)`。 3. `MoveToEmpty()` 后源 allocator `GetAllocation() == nullptr`，目标 allocator 仍能读出原值。 |
| 使用的 Helper | `FMemMark`、`GScriptPreallocatedMemStack`、`TPrecompiledAllocator<>::ForElementType<int32>` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 2 | MissingScenario: 1, NoTestForSource: 1 |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 13:16)

### 一、现有测试问题

本轮未新增现有测试质量问题，继续补场景缺口与 `StaticJIT/` 覆盖映射。

### 二、需要新增的测试

补充覆盖结论：
`Debugger/` 现有 7 个用例里，断点方向已覆盖“设置断点 / 命中断点 / 删除断点”，但没有条件断点；步进方向已覆盖单文件 `step in / step over / step out`，但没有跨文件步进；`Smoke.Handshake` 实际验证了 handshake、`DebugServerVersion`、`BreakFilters` 和 `StopDebugging` 状态切换，不只是“调试器能启动”。

`StaticJIT/` 目录共 14 个文件。当前直接命中自动化测试的只有 `PrecompiledData.cpp` / `PrecompiledData.h`，且仅限 `AngelscriptPrecompiledDataTests.cpp` 里的 2 个 C++ tests，覆盖点只有 high-bit flag roundtrip 和 module diff。`PrecompiledDataAllocator.h` 只是随 include 编译进入，没有独立行为断言；其余 12 个文件未发现任何直接测试符号引用，属于高风险缺口。

#### NewTest-1：条件断点只在表达式为真时停下

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `FAngelscriptBreakpoint`、`FAngelscriptDebugServer::HandleMessage()` 的 `SetBreakpoint` 分支 |
| 现有测试覆盖 | 有测试但缺少条件断点场景；当前 3 个断点用例只发送无条件 line breakpoint，`FAngelscriptBreakpoint` 消息体也没有 condition 字段 |
| 风险评估 | 条件断点是 DAP 常见能力。当前既没有测试，也没有 Shared helper 入口；后续一旦补协议，很容易在“条件为假仍停下 / 条件为真不生效 / 删除条件断点后状态残留”这些路径上回归。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Breakpoint.ConditionExpression` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerConditionalBreakpointTests.cpp` |
| 场景描述 | 为 `CreateBreakpointFixture()` 增加一个可控分支入口，向同一行设置条件断点，例如 `Input > 0`。第一次以 `Input = 3` 运行应命中断点；第二次以 `Input = -1` 运行应直接完成，不产生 `HasStopped`。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`；在 Shared 新增 `SendSetConditionalBreakpoint(...)` helper，或扩展 `FAngelscriptBreakpoint` 测试协议体以承载条件表达式。 |
| 期望行为 | 1. 正向运行只收到 1 个 `HasStopped`，`Reason == "breakpoint"`，callstack 顶帧文件/行号等于条件断点所在行。 2. 反向运行 `StopEnvelopes.Num() == 0`，且 `Error.IsEmpty()`、`!bTimedOut`。 3. 两次运行结果值都与脚本预期一致。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture`，外加新增 `SendSetConditionalBreakpoint(...)` |
| 优先级 | P1 |

#### NewTest-2：跨文件 `step in / step out` 必须同时切换 `Source` 和 `LineNumber`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `StepIn` / `StepOver` / `StepOut` 分支，`FAngelscriptDebugServer::ProcessScriptLine()` |
| 现有测试覆盖 | 有测试但仅覆盖单文件 fixture；3 个步进用例都在 `DebuggerSteppingFixture.as` 内部跳转，没有任何 `Source` 切换断言 |
| 风险评估 | 一旦 section/file 映射错误，当前测试仍可能通过，因为它们只看行号和栈深；真实 IDE 前端则会表现为“步进进入了错误文件”或“StepOut 回到错误 section”。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Stepping.CrossFileTransition` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerCrossFileSteppingTests.cpp` |
| 场景描述 | 构造一个两文件 fixture：文件 A 的 `RunScenario()` 调用文件 B 的 `Inner() `。在 A 的 call-site 断住后依次发送 `StepIn`、`StepOut`、`Continue`。 |
| 输入/前置 | 在 Shared 新增 `CompileModuleSectionsFromMemory(...)` 或 `FAngelscriptDebuggerMultiFileFixture`，允许同一 module 挂两个 source file 和各自 line marker。 |
| 期望行为 | 1. 第 1 个 stop：`Reason == "breakpoint"`，顶帧 `Source` 以文件 A 结尾。 2. `StepIn` 后第 2 个 stop：`Reason == "step"`，顶帧 `Source` 切到文件 B，`LineNumber` 命中文件 B 的入口 marker。 3. `StepOut` 后第 3 个 stop：`Reason == "step"`，顶帧 `Source` 回到文件 A 的 after-call marker。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、新增 `FAngelscriptDebuggerMultiFileFixture` / `CompileModuleSectionsFromMemory(...)` |
| 优先级 | P1 |

#### NewTest-3：为 StaticJIT core codegen 建立最小执行矩阵

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHelperFunctions.h` |
| 关联函数 | `FAngelscriptStaticJIT::GenerateCppCode()`、`FAngelscriptStaticJIT::WriteOutputCode()`、核心 bytecode `Implement(...)` 路径 |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 这是 `StaticJIT/` 的核心实现区，当前没有任何自动化证明“能成功生成 / 能注册到 JIT database / 执行结果与 VM 一致”。一旦 codegen、helper bind 或 bytecode support 回归，只能靠运行时偶发现象发现。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.Codegen.ExecutionMatrix` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITExecutionTests.cpp` |
| 场景描述 | 用一个小脚本覆盖算术、函数调用、局部变量、if/else 和 UObject 属性访问五类最基础路径，先得到 VM 结果，再生成/加载 StaticJIT 产物并执行同一入口。 |
| 输入/前置 | 在 Shared 新增 `FStaticJITExecutionHarness`：负责 `CreateForTesting`、脚本编译、触发 StaticJIT 生成、装载 JIT database，并提供“执行 VM / 执行 JIT”两个入口。 |
| 期望行为 | 1. `GenerateCppCode()` / `WriteOutputCode()` 完成且不触发断言。 2. 目标函数在 `FJITDatabase` 中存在可执行 entry。 3. JIT 执行返回值与 VM 执行完全一致。 4. 至少校验一个 UObject 属性访问和一次脚本函数调用结果，避免只测纯算术。 |
| 使用的 Helper | `FAngelscriptEngineScope`、`CompileModuleFromMemory`、`ExecuteIntFunction`、新增 `FStaticJITExecutionHarness` |
| 优先级 | P0 |

#### NewTest-4：补上 PrecompiledData 的函数级 load/fallback 覆盖，而不是只测 class flag roundtrip

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledDataAllocator.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StringInArchive.h` |
| 关联函数 | `FAngelscriptPrecompiledFunction::Create()`、预编译 archive read/write 路径 |
| 现有测试覆盖 | 有测试但只覆盖 class flag roundtrip 和 module diff；没有任何函数级 JIT entry 命中/缺失测试 |
| 风险评估 | 当前唯一的 StaticJIT 自动化根本不知道“某个函数到底有没有拿到 JIT entry”，也不知道 archive/load/fallback 是否保持可执行。函数级 regressions 会被高位 flag 测试完全漏掉。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.PrecompiledData.FunctionEntryFallback` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataFunctionTests.cpp` |
| 场景描述 | 构造两个脚本函数：函数 A 在 `FJITDatabase` 中有 entry，函数 B 故意没有 entry。加载 precompiled data 后分别执行两者，验证 A 走 JIT，B 安全回退 VM。 |
| 输入/前置 | 复用 `FStaticJITExecutionHarness`；需要 helper 支持注入一个最小 `FJITDatabase`，并能在加载前清空/恢复 database。 |
| 期望行为 | 1. 函数 A 创建后 `jitFunction_Raw != nullptr`，执行结果正确。 2. 函数 B 创建后 `jitFunction_Raw == nullptr`，但仍能通过 VM 返回预期结果。 3. archive 反序列化过程中没有丢失函数元数据。 |
| 使用的 Helper | 新增 `FStaticJITExecutionHarness`、`FAngelscriptPrecompiledData`、`CompileModuleFromMemory` |
| 优先级 | P0 |

#### NewTest-5：验证生成输出里的 JIT 调试元数据钩子，而不是只依赖运行时偶发现象

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h` |
| 关联函数 | `FScopeJITDebugCallstack`、`SCRIPT_DEBUG_CALLSTACK_FRAME*`、`SCRIPT_DEBUG_CALLSTACK_LINE` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | JIT debug metadata 是调试器、callstack 和源码定位的基础。但当前既没有运行时验证，也没有生成输出级验证，任何宏展开或 config 条件错误都会直接漏进主线。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.GeneratedOutput.DebugMetadataHooks` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITGeneratedOutputTests.cpp` |
| 场景描述 | 让 `FAngelscriptStaticJIT` 为一个两行脚本生成 C++ 输出，检查生成文本中是否写入 `SCRIPT_DEBUG_CALLSTACK_FRAME*` 和正确的 `SCRIPT_DEBUG_CALLSTACK_LINE(...)`；再在禁用调试元数据的配置下生成一次，确认这些钩子被移除。 |
| 输入/前置 | 在 Shared 新增 `GenerateStaticJITSourceText(...)` 测试 seam，直接返回 `WriteOutputCode()` 产物字符串，避免把“输出到磁盘”作为测试前提。 |
| 期望行为 | 1. 开启 debug metadata 时，生成文本包含 frame 宏和至少两个 line 更新点，行号与脚本 marker 对齐。 2. 关闭对应 config 时，上述宏不应出现在输出文本里。 3. 同一脚本在两种配置下都能完成生成。 |
| 使用的 Helper | 新增 `GenerateStaticJITSourceText(...)`、`CompileModuleFromMemory`、line marker 解析 helper |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |
| P1 | 3 | MissingScenario: 2, NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 13:14)

### 一、现有测试问题

#### Issue-5：两个核心调试器测试文件都超过 500 行，且把复杂 monitor/helper 内联在测试文件里

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:1-655`；`AngelscriptDebuggerSteppingTests.cpp:1-659` |
| 问题描述 | 两个文件都把 session 初始化、异步 invocation、background monitor、握手循环和用例本体混在同一个 `.cpp`。`AngelscriptDebuggerBreakpointTests.cpp` 实际 655 行，`AngelscriptDebuggerSteppingTests.cpp` 659 行，已经超过规则里“单文件 300-500 行、单文件单职责”的上限；同时两份文件各自复制了 `Start*DebuggerSession`、`WaitFor*BreakpointCount`、`Start*Monitor` 等近似逻辑。 |
| 影响 | 文件过大且 helper 复制，会让后续修一次 monitor 行为就要同步改多处；阅读和维护成本显著上升，也更容易让 breakpoint/stepping 用例在细节上继续分叉。 |
| 修复建议 | 把 monitor/session/async invocation 抽到 `Plugins/Angelscript/Source/AngelscriptTest/Shared/`，例如新增 `AngelscriptDebuggerMonitor.h/.cpp`；然后按职责拆成 `BreakpointLifecycleTests.cpp`、`BreakpointNegativeTests.cpp`、`SteppingBasicTests.cpp`、`SteppingEdgeCaseTests.cpp` 这类 300-500 行文件，保留每个文件只覆盖一类场景。 |

#### Issue-6：`FAngelscriptDebuggerTestSession::Shutdown()` 没有接管 DebugServer 状态清理，隔离性依赖每个用例手写 teardown

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | 29-100 |
| 问题描述 | `Initialize()` 在当前调试器测试里优先复用 `TryGetRunningProductionDebuggerEngine()` 返回的共享 production engine；但 `Shutdown()` 只恢复 `AngelscriptDebugServer::DebugAdapterVersion`、reset `GlobalScope` 和本地指针，并没有对 `DebugServer` 执行 `StopDebugging`、断点清空、socket/drain 或等待 server 退出。结果是 session 自身并不拥有“恢复到干净调试状态”的能力，所有隔离都依赖各个测试在 `ON_SCOPE_EXIT` 里手动发送 `StopDebugging` / `Disconnect` / `DiscardModule`。 |
| 影响 | 只要某个用例在手写 teardown 前失败，或后台 monitor 尚未完全退出，下一条用例就可能继承上一个用例遗留的 `bIsDebugging`、breakpoints 或连接状态，形成顺序相关失败。 |
| 修复建议 | 在 `FAngelscriptDebuggerTestSession` 增加显式 cleanup 责任：1. 跟踪由 session 创建/注册的 test clients；2. `Shutdown()` 时统一请求 `StopDebugging` 并 `PumpUntil(!bIsDebugging)`；3. 清理断点与临时模块，必要时增加 `ResetDebugServerStateForTests()` helper。这样测试体只声明需要的资源，不再各自拼装 teardown。 |

### 二、需要新增的测试

本轮继续聚焦现有测试质量问题，新增测试建议将在后续分段追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-5 |
| BadIsolation | 1 | Issue-6 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |
---

## 测试审查 (2026-04-08 13:23)

### 一、现有测试问题

#### Issue-7：3 个步进用例只断言执行成功，不校验最终返回值，无法证明步进后继续执行的语义正确

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | 437-453，532-554，634-656 |
| 问题描述 | `CreateSteppingFixture()` 的 `RunScenario()` 固定应返回 `14`，但 3 个用例在完成全部 stop 断言后都只检查 `InvocationState->bSucceeded`，没有任何 `InvocationState->Result` 断言。这样即使调试器在 `Continue`/`StepOver`/`StepOut` 之后从错误的 PC 继续、漏执行 `Result += 1`，或者重复执行 `Inner()`，只要脚本没有抛异常，测试仍会绿灯。 |
| 影响 | 当前步进测试只能证明“停在过预期行号”，不能证明“从该停点恢复后仍完成了正确的剩余控制流”。这会放过 resume 语义错误和重复执行/漏执行类回归。 |
| 修复建议 | 对 3 个用例统一补上 `TestEqual(..., InvocationState->Result, 14)`；同时把期望值提取成 fixture helper，例如 `Fixture.ExpectedReturnValue()`，避免后续修改脚本常量时测试断言分叉。 |

#### Issue-8：`ClearThenResume` 的首轮基线只检查“发生过 stop”，没有证明命中的就是目标断点

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume` |
| 行号范围 | 468-518 |
| 问题描述 | 该用例首轮运行只断言 `FirstMonitorResult.StopEnvelopes.Num() > 0` 和 `FirstInvocation->bSucceeded`，既不反序列化 `FStoppedMessage` 检查 `Reason == "breakpoint"`，也不请求 callstack 验证顶帧确实停在 `BreakpointHelperLine`。在 `DebugServer` 里，异常、残留断点甚至其他客户端带来的 stop 都会同样表现为“至少收到一个 `HasStopped`”。 |
| 影响 | 这个用例的“先命中、再清除、再不中断”前半段缺少可信基线。若首轮停下的其实不是刚设置的断点，测试仍会继续执行并在第二轮给出绿灯，从而掩盖断点命中语义回归或前序状态泄漏。 |
| 修复建议 | 首轮 monitor 改为 `bRequestCallstack = true`，并补齐与 `HitLine` 一致的断言：`StopMessage.IsSet()`、`StopMessage->Reason == "breakpoint"`、顶帧 `Source` 以 `Fixture.Filename` 结尾且 `LineNumber == Fixture.GetLine(TEXT("BreakpointHelperLine"))`。这样第二轮“清除后不中断”才建立在可信的首轮命中事实上。 |

#### Issue-9：6 个设置断点的用例把“注册成功”退化成全局 `BreakpointCount`，无法证明是目标文件/目标行生效

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:74-99, 374-380, 486-492, 622-628`；`AngelscriptDebuggerSteppingTests.cpp:73-98, 390-396, 498-504, 600-606` |
| 问题描述 | 两个文件都通过 `WaitForBreakpointCount()` / `WaitForSteppingBreakpointCount()` 轮询 `Session.GetDebugServer().BreakpointCount == ExpectedCount` 来判断断点已注册。这个计数是 `DebugServer` 的全局总数，不包含 filename、module、line 维度；而测试又挂接到 `TryGetRunningProductionDebuggerEngine()` 返回的共享 production engine。结果是“其他客户端遗留断点”“断点被注册到错误文件/错误行但总数仍增加”“当前请求被忽略但旧断点刚好让总数满足条件”都会让等待条件提前通过。 |
| 影响 | 现有用例对“设置断点成功”的判断过于宽泛，无法稳定支撑用户要求里的“已覆盖设置断点”结论。它们更多是在证明“服务器里某处存在 N 个断点”，而不是“这次请求让目标断点进入了正确状态”。 |
| 修复建议 | 新增一个 Shared helper，例如 `WaitForSpecificBreakpoint(Test, Session, Filename, ModuleName, Line)`：直接检查 `DebugServer.Breakpoints` 中 canonical filename/module 对应的 `Lines` 集合；同时把测试发送的 `FAngelscriptBreakpoint.Id` 设成稳定值，在需要验证行号调整/拒绝时消费 `SetBreakpoint` 回包。保留 `BreakpointCount` 只能作为附加诊断，不应作为唯一断言。 |

### 二、需要新增的测试

补充覆盖映射：按 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` + `Plugins/Angelscript/Source/AngelscriptTest/` 的测试侧精确检索，`StaticJIT/` 14 个文件里只有 `PrecompiledData.h` 在 `AngelscriptPrecompiledDataTests.cpp` 中出现 1 次直接 include；`PrecompiledData.cpp` 的行为仅被该测试经 header API 间接触发，其余 `AngelscriptBytecodes.cpp/.h`、`AngelscriptStaticJIT.cpp/.h`、`PrecompiledDataAllocator.h`、`StaticJITBinds.cpp/.h`、`StaticJITConfig.h`、`StaticJITHeader.cpp/.h`、`StaticJITHelperFunctions.h`、`StringInArchive.h` 在测试侧都是 0 次文件名命中。

#### NewTest-6：验证 `SetBreakpoint` 对“非可执行行”返回调整后的实际落点

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `SetBreakpoint` 分支 |
| 现有测试覆盖 | 有测试但只覆盖“直接把断点下在 marker 行”；没有任何用例设置 `Breakpoint.Id`，也没有验证服务器对非代码行的 `SetBreakpoint` 回包 |
| 风险评估 | 一旦服务器把断点挪到错误行、静默拒绝、或 filename canonization 后回包错位，现有 6 个用例仍可能因为 `BreakpointCount` 满足而通过，IDE 前端却会显示错误的断点位置。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Breakpoint.NearestExecutableLineAck` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointProtocolTests.cpp` |
| 场景描述 | 基于 `CreateBreakpointFixture()`，把断点下在 `BreakpointHelperLine - 1` 的空白行，并显式设置 `Breakpoint.Id = 101`。发送 `SetBreakpoint` 后等待同类型回包，再执行脚本。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient` 和现有 breakpoint monitor；新增一个小 helper `WaitForBreakpointAck(Timeout)`，内部使用 `WaitForTypedMessage<FAngelscriptBreakpoint>(EDebugMessageType::SetBreakpoint, ...)`。 |
| 期望行为 | 1. 回包存在且 `Id == 101`。 2. 回包 `Filename` 仍是原始 fixture 文件名。 3. 回包 `LineNumber == Fixture.GetLine(TEXT("BreakpointHelperLine"))`。 4. 运行脚本后只收到 1 个 `HasStopped`，`Reason == "breakpoint"`，顶帧行号等于回包行号。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient::WaitForTypedMessage`、现有 `FBreakpointMonitorConfig`，断言沿用 `ASTEST_*` 风格包装现有 `TestTrue/TestEqual` 检查 |
| 优先级 | P1 |

#### NewTest-7：在叶子帧同时校验 `CallStack`、`RequestEvaluate` 与 `RequestVariables`

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `SendCallStack()`、`GetDebuggerValue()`、`GetDebuggerScope()`、`HandleMessage()` 的 `RequestCallStack` / `RequestEvaluate` / `RequestVariables` 分支 |
| 现有测试覆盖 | `HitLine` / `Step*` 只验证 callstack 顶帧行号；`RequestEvaluate`、`RequestVariables` 在 debugger 测试里完全零覆盖，`CreateCallstackFixture()` 与 `GetEvalPath()` 也完全未被使用 |
| 风险评估 | 变量面板和 hover evaluate 是调试器的核心功能。当前没有任何自动化证明 frame 解析、`%local%` / `%module%` / `%this%` scope 展开、以及 adapter v2 的变量值序列化是正确的。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Evaluation.ScopeValues` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerEvaluationTests.cpp` |
| 场景描述 | 使用 `CreateCallstackFixture()` 编译一个含 `Entry -> Middle -> Leaf` 的类脚本，在 `CallstackLeafLine` 断住后依次请求 callstack、evaluate、variables。 |
| 输入/前置 | 在 Shared 新增 `DispatchGeneratedIntInvocation(...)` helper：根据 fixture 的 `GeneratedClassName` / `EntryFunctionName` 创建对象并在 GameThread 调用 `Entry()`；monitor 在收到 `HasStopped` 后先发 `RequestCallStack`，再对 `Fixture.GetEvalPath(TEXT("LeafLocalValuePath"))`、`LeafCombinedPath`、`ThisMemberValuePath`、`ModuleGlobalCounterPath` 发送 `RequestEvaluate`，最后对 `0:%local%` 发送 `RequestVariables`。 |
| 期望行为 | 1. `CallStack` 至少有 3 帧，前三帧行号分别等于 `CallstackLeafLine`、`CallstackMiddleLine`、`CallstackEntryLine`。 2. `RequestEvaluate` 返回的值分别为 `4`、`16`、`5`、`7`。 3. `RequestVariables("0:%local%")` 返回的变量列表至少包含 `LocalValue` 和 `Combined`，且值分别为 `4`、`16`。 4. 在 adapter version 2 下，`this.MemberValue` 与 `%module%.GlobalCounter` 的 `ValueAddress` 不为 `0`。 |
| 使用的 Helper | `FAngelscriptDebuggerScriptFixture::CreateCallstackFixture`、`FAngelscriptDebuggerScriptFixture::GetEvalPath`、`FAngelscriptDebuggerTestClient::SendRequestCallStack`、`SendRequestEvaluate`、`SendRequestVariables`、新增 `DispatchGeneratedIntInvocation(...)` |
| 优先级 | P0 |

#### NewTest-8：验证 `DebugBreak()` 与 `ensure(false, ...)` 会走脚本调试 stop，并保留 ensure 的“一次一断”语义

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `ASDebugBreak()`、`ASShouldBreakEnsure()`、`FAngelscriptEngine::TryBreakpointAngelscriptDebugging()` |
| 现有测试覆盖 | debugger 套件完全没有使用 `CreateBindingFixture()`；`DebugBreak()`、`ensure()`、`check()` 与 debugger stop 的集成路径是零覆盖 |
| 风险评估 | 这条链路一旦回归，脚本作者显式调用 `DebugBreak()` 或命中 `ensure(false)` 时就可能直接掉进 native break，或者完全不会产生 `HasStopped`，而现有自动化不会给出任何信号。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Binding.DebugBreakAndEnsure` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBindingTests.cpp` |
| 场景描述 | 使用 `CreateBindingFixture()` 生成类脚本，分三阶段执行：1. 调用 `TriggerDebugBreak()`；2. 调用 `TriggerEnsure(false, "Once")`；3. 在同一 session 内再次调用 `TriggerEnsure(false, "Repeat")`。 |
| 输入/前置 | 在 Shared 新增 `DispatchGeneratedVoidInvocation(...)` 与 `DispatchGeneratedBoolInvocation(...)`，用于对生成类的 `UFUNCTION` 做异步调用；每个阶段都复用现有 monitor，并在 stop 时请求 callstack。 |
| 期望行为 | 1. `TriggerDebugBreak()` 产生 1 个 `HasStopped`，`Reason == "breakpoint"`，顶帧行号等于 `BindingDebugBreakLine`。 2. `TriggerEnsure(false, "Once")` 产生 1 个 `HasStopped`，顶帧行号等于 `BindingEnsureLine`，继续执行后函数返回 `false`。 3. 第二次 `TriggerEnsure(false, "Repeat")` 仍返回 `false`，但因为 `ASShouldBreakEnsure()` 已记住该位置，本轮 `StopEnvelopes.Num() == 0`。 |
| 使用的 Helper | `FAngelscriptDebuggerScriptFixture::CreateBindingFixture`、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、新增 `DispatchGeneratedVoidInvocation(...)` / `DispatchGeneratedBoolInvocation(...)` |
| 优先级 | P1 |

#### NewTest-9：为 `StringInArchive` 建立 UTF-8 与连续读取 roundtrip 单测

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StringInArchive.h` |
| 关联函数 | `FStringInArchive::AssignAsUTF8()`、`FStringInArchive::UnrealString_UTF8()`、`operator<<(FArchive&, FStringInArchive&)`、`FMemoryReaderWithPtr::GetCurrentPtr()` |
| 现有测试覆盖 | 完全无测试；测试侧对 `StringInArchive.h` 是 0 次引用 |
| 风险评估 | 该结构负责 precompiled/StaticJIT 路径里的字符串序列化视图。若长度、零结尾或 reader seek 偏移出错，后续 namespace / type / function 名称都会在 load 阶段静默损坏。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.StringInArchive.RoundtripUtf8` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITArchiveTests.cpp` |
| 场景描述 | 用 `FStringInArchive` 依次写入一个空字符串和一个通过 `AssignAsUTF8()` 设置的多字节字符串，再用同一份 buffer 通过 `FMemoryReaderWithPtr` 连续读回两个对象。 |
| 输入/前置 | 直接在 C++ 测试里构造 `TArray<uint8>`、`FMemoryWriter`、`FMemoryReaderWithPtr`，字符串样例建议使用既含 ASCII 又含多字节字符的值，例如 `TEXT("JIT_é_中")`。 |
| 期望行为 | 1. 第一个读回对象 `Len() == 0` 且 `UnrealString()` 为空。 2. 第二个读回对象 `UnrealString_UTF8()` 与原始 `FString` 完全一致。 3. 连续读取后 reader 没有错位，说明 `operator<<` 的 `(Length + 1)` seek 偏移正确。 4. 额外补一组 copy/move 断言，确认从 loaded view 复制/移动后读取内容仍一致。 |
| 使用的 Helper | `FStringInArchive`、`FMemoryReaderWithPtr`、`FMemoryWriter`，断言使用 `ASTEST_*` 风格的值比较宏 |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-9 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 2 | MissingEdgeCase: 1, NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 13:37)

### 一、现有测试问题

#### Issue-10：`Smoke.Handshake` 的 teardown 只等 1 秒且忽略结果，失败路径会把共享 debug session 留在 debugging 状态

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake` |
| 行号范围 | 38-46 |
| 问题描述 | `ON_SCOPE_EXIT` 里发送 `StopDebugging` 后，只调用 `Session.PumpUntil(..., 1.0f)` 等待 1 秒，而且完全忽略返回值，随后立刻 `SendDisconnect()` / `Disconnect()`。和 breakpoint/stepping 用例统一使用 `Session.GetDefaultTimeoutSeconds()` 的做法不同，这里既没有沿用 session timeout，也没有在 teardown 失败时报错。 |
| 影响 | 当前 smoke 用例复用 `TryGetRunningProductionDebuggerEngine()` 返回的共享 engine。若编辑器负载较高或 socket teardown 稍慢，测试退出时可能仍保持 `bIsDebugging == true`，把 debug 状态泄漏给后续 debugger 用例，形成顺序相关失败。 |
| 修复建议 | 把 cleanup 改成显式断言：`const bool bStopped = Session.PumpUntil(..., Session.GetDefaultTimeoutSeconds()); TestTrue(..., bStopped);`；若 stop 仍失败，至少在 `AddError` 后再断开 client。更稳妥的做法是把这段 teardown 下沉到 `FAngelscriptDebuggerTestSession::Shutdown()` 的统一清理路径。 |

#### Issue-11：3 个步进用例只检查 `Callstack.IsSet()` 就直接索引 `Frames[0]`，协议退化时会崩成数组越界而不是测试失败

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | 437-449，532-548，634-650 |
| 问题描述 | 这 3 个用例在 `TestTrue(... Callstack.IsSet())` 之后就直接访问 `Callstack->Frames[0]`。其中 `StepIn` 第 1 个 stop、`StepOver` 两个 stop、`StepOut` 第 3 个 stop 都没有先验证 `Frames.Num() > 0`；一旦 debug server 回了空 callstack、裁剪帧、或反序列化后帧数组为空，测试会触发数组越界/`check`，而不是给出“callstack 为空”的清晰断言。 |
| 影响 | 这类失败会把真实的协议回归伪装成测试代码崩溃，降低定位效率；同时也说明当前断言链没有完整覆盖“收到 callstack 且 callstack 至少包含顶帧”这个最基本前提。 |
| 修复建议 | 在每次访问 `Frames[0]` 前先补 `TestTrue(TEXT(\"... should contain at least one frame\"), Callstack->Frames.Num() > 0)`，失败时立即 `return false`；对需要比较栈深的路径继续保留 `>= 2` 断言。顺手补 `Source.EndsWith(Fixture.Filename)`，避免只有行号没有文件身份的弱断言。 |

#### Issue-12：`WaitForMessageType()` 是有损 helper，会把所有“非目标类型”的协议消息直接吞掉

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.*`（通过 Shared helper 间接受影响） |
| 行号范围 | 183-218 |
| 问题描述 | `WaitForMessageType()` 循环调用 `ReceiveEnvelope()`；如果收到的 envelope 不是 `ExpectedType`，它只记录 `LastMessageType`，然后继续等待，完全没有把这条消息回放到队列里。对 debugger 这种多路复用协议来说，`Diagnostics`、`SetBreakpoint` ack、`HasStopped`、`AssetDatabase*` 等都可能与目标消息交错到达；当前 helper 一旦被调用，就会无声丢失这些消息。 |
| 影响 | 这会让 Shared 基础设施无法安全地组合多个协议断言。未来新增 `SetBreakpoint` 回包、evaluate、asset database 或 data breakpoint 测试时，很容易因为 helper 先吞掉了别的消息而出现“目标消息超时”或后续断言缺消息的伪失败。 |
| 修复建议 | 给 `FAngelscriptDebuggerTestClient` 增加 `PendingEnvelopes` 队列：`ReceiveEnvelope()` 先消费队列再读 socket，`WaitForMessageType()` 把不匹配的 envelope 放回队列；或者改成 `WaitForMessageType(..., TArray<FAngelscriptDebugMessageEnvelope>* OutSkippedMessages)`，由调用方决定如何处理非目标消息。 |

### 二、需要新增的测试

本轮先追加现有测试/Shared 基础设施的新问题，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-10 |
| WeakAssertion | 1 | Issue-11 |
| WrongHelper | 1 | Issue-12 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 13:46)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

补充索引说明：`2026-04-08 13:42` 这一轮已写入文档，包含 `NewTest-10` 至 `NewTest-13`；阅读顺序请以时间戳为准继续向后查看。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 13:56)

### 一、现有测试问题

#### Issue-13：`MakeFixture()` 的 marker 解析误用 `FindChar`，一旦 marker 前出现 `/` 就可能卡死 Shared fixture

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.*`（通过 Shared fixture 间接受影响） |
| 行号范围 | 19-45 |
| 问题描述 | `MakeFixture()` 用 `while (Line.FindChar(TEXT('/'), MarkerStart))` 扫描 `/*MARK:...*/`，但 `FString::FindChar` 只会返回“整行第一个 `/`”的位置，不会从 `MarkerStart` 继续向后搜索。当前代码在发现第一个 `/` 不是 marker 起点时只做 `MarkerStart += 1` 然后继续循环，下一轮又会拿回同一个 `/`。这意味着只要某一行在 marker 前出现 `//`、`/*comment*/`、路径字面量或除法表达式，fixture 构建就可能进入死循环。现在 4 个内置 fixture 恰好没有这种行，所以问题被掩盖。 |
| 影响 | 这是 Shared helper 的潜在挂死点。后续一旦按现有建议扩展 `CreateCallstackFixture()` / `CreateBindingFixture()`，或给现有脚本加注释、路径、除法表达式，测试会在编译前卡住，表现成难定位的超时。 |
| 修复建议 | 不要再用 `FindChar` 做前向扫描，改为 `Line.Find(TEXT("/*MARK:"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart)` 维护显式 `SearchStart`；每次移除 marker 后把 `SearchStart` 更新到移除位置，确保能安全处理“marker 前有普通 `/`”和“一行多个 marker”两种情况。最好补一个 `AngelscriptDebuggerScriptFixtureTests.cpp` 单测，输入含 `// helper` 与 `Value / 2 /*MARK:Line*/` 的脚本行，断言 `GetLine()` 返回正确且不会卡死。 |

### 二、需要新增的测试

本轮先记录 Shared fixture 新问题，新增测试建议将在后续分段追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-13 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 13:58)

### 一、现有测试问题

#### Issue-14：脚本 invocation 超时后没有任何取消或排空，失败路径可能把挂起的 GameThread 执行泄漏到后续用例

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:111-145, 392-396, 504-508, 557-560, 640-643`；`AngelscriptDebuggerSteppingTests.cpp:110-143, 407-410, 515-518, 617-620` |
| 问题描述 | 两个文件都用 `AsyncTask(ENamedThreads::GameThread, ...)` 发起脚本调用，再靠 `WaitForInvocationCompletion()` / `WaitForSteppingInvocationCompletion()` 轮询 `bCompleted`。但一旦等待超时，测试只会直接 `return false`，最多把 `bMonitorShouldStop` 置真；没有任何逻辑去确认 GameThread 里的 `ExecuteIntFunction(...)` 已经退出，也没有取消、排空或二次 `PumpUntil` 等待 invocation 收口。随后 `ON_SCOPE_EXIT` 立刻执行 `Client.SendStopDebugging()`、`Engine.DiscardModule(...)` 和 `CollectGarbage(...)`。 |
| 影响 | 当失败发生在“脚本已进入 debugger pause 或尚未完全结束”时，当前用例会把挂起的 invocation 留在共享 production engine 上。后续 debugger 用例可能接收到迟到的 stop、访问已被 `DiscardModule` 的模块，或者单纯因为前一个用例残留执行而出现顺序相关失败。 |
| 修复建议 | 把脚本调用也纳入 RAII cleanup：为异步 invocation 增加 `FScopedInvocationDrain`，析构时先发送 `StopDebugging`/`Continue` 解除 pause，再 `Session.PumpUntil([&] { return InvocationState->bCompleted.Load(); }, Session.GetDefaultTimeoutSeconds())`；如果仍未完成，至少 `AddError` 标记“invocation leaked”。更稳妥的做法是让 `Dispatch*Invocation` 返回可等待句柄，并在所有失败路径统一调用 `WaitForInvocationDrain(...)`，禁止直接在 invocation 未收口时 `return false`。 |

### 二、需要新增的测试

本轮先记录 invocation 清理问题，新增测试建议将在后续分段追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-14 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 14:04)

### 一、现有测试问题

本轮未新增现有测试质量问题，继续补 debugger 协议空白。

### 二、需要新增的测试

#### NewTest-14：覆盖 `RequestDebugDatabase` 的真实消息序列，而不只是 `DebugDatabaseSettings` struct roundtrip

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `RequestDebugDatabase` 分支、`SendDebugDatabase()`、`SendAssetDatabase()`、`FAngelscriptDebugDatabaseSettings`、`FAngelscriptDebugDatabase`、`FAngelscriptAssetDatabase` |
| 现有测试覆盖 | 只有 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` 里的 `Angelscript.CppTests.Debug.Protocol.DatabaseSettings.RoundTrip` 覆盖了 struct 序列化；没有任何场景测试验证 debug server 的实际发送顺序与 payload |
| 风险评估 | `RequestDebugDatabase` 是 IDE 初始化的关键协议。若 server 不再发送 `DebugDatabaseSettings`、`DebugDatabaseFinished`、`AssetDatabaseInit/Finished`，或 asset chunk 结构被破坏，前端会直接表现为“数据库加载卡死/资产面板空白”，当前自动化完全无信号。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Database.RequestDebugDatabaseSequence` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDatabaseTests.cpp` |
| 场景描述 | 建立 debugger session 并完成 `StartDebugging` 后，发送 `RequestDebugDatabase`。用带 pending-queue 的 message collector 收集直到 `AssetDatabaseFinished`，记录完整消息序列。 |
| 输入/前置 | 在 Shared 增加 `SendRequestDebugDatabase()`，以及 `CollectMessagesUntil(EDebugMessageType TerminalType, float TimeoutSeconds, TArray<FAngelscriptDebugMessageEnvelope>& OutMessages)`；该 helper 必须基于 Issue-12 的 pending queue 方案实现，避免吞消息。 |
| 期望行为 | 1. 消息序列中先出现 1 条 `DebugDatabaseSettings`，其字段精确等于当前运行时配置：`bAutomaticImports == FAngelscriptEngine::Get().ShouldUseAutomaticImportMethod()`，其余 4 个布尔值等于 `UAngelscriptSettings` / `WITH_ANGELSCRIPT_HAZE` 当前值。 2. 至少收到 1 条 `DebugDatabase`，且 `Database` 字符串非空、能被解析成 JSON object。 3. `DebugDatabaseFinished` 必须出现在任何 `AssetDatabase*` 之前。 4. `AssetDatabaseInit` 与 `AssetDatabaseFinished` 都必须出现；中间所有 `AssetDatabase` payload 的 `Assets.Num()` 都是偶数，保持“路径/类名”配对。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、新增 `SendRequestDebugDatabase()`、新增 `CollectMessagesUntil(...)`、`FJsonSerializer` |
| 优先级 | P1 |

#### NewTest-15：验证 `BreakOptions` 真正参与断点过滤，而不是只返回可选 filter 列表

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `BreakOptions` 分支、`FAngelscriptDebugServer::ShouldBreakOnActiveSide()`、`FAngelscriptRuntimeModule::GetDebugCheckBreakOptions()` |
| 现有测试覆盖 | 有 `Smoke.Handshake` 与 `NewTest-11` 对应的 `BreakFilters` 方向，但没有任何测试发送 `BreakOptions` 或验证 filter 真能抑制/放行 breakpoint |
| 风险评估 | 当前 UI 即使能拿到 filter 列表，也完全可能在 server 侧被忽略。若 `BreakOptions` 解析失效、遗漏 `break:any` 兜底、或 delegate 参数错误，最终表现是“筛选器面板存在但没有效果”。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Breakpoint.BreakOptionsGateStop` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakOptionsTests.cpp` |
| 场景描述 | 复用 `CreateBreakpointFixture()`，先绑定 `FAngelscriptRuntimeModule::GetDebugCheckBreakOptions()`，让它只在 `BreakOptions` 包含 `break:test` 时返回 `true`。同一条脚本断点分两轮执行：第一轮发送 `BreakOptions = ["break:other"]`，第二轮发送 `BreakOptions = ["break:test"]`。 |
| 输入/前置 | 在 Shared 新增 `FScopedDebugBreakOptionsBinding` 与 `SendBreakOptions(const TArray<FString>& Filters)` helper；两轮都复用现有 breakpoint monitor，并打开 `bRequestCallstack = true`。 |
| 期望行为 | 1. 第一轮 monitor `StopEnvelopes.Num() == 0`、`Error.IsEmpty()`、`!bTimedOut`，脚本返回值仍为 `8`。 2. 第二轮恰好收到 1 个 `HasStopped`，`Reason == "breakpoint"`。 3. 第二轮顶帧文件仍以 `DebuggerBreakpointFixture.as` 结尾，行号等于 `BreakpointHelperLine`。 4. 绑定 delegate 实际收到的 options 集合包含调用方发来的 filter，并且 server 自动追加的 `break:any` 也在集合内。 |
| 使用的 Helper | `FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture`、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、新增 `SendBreakOptions(...)`、新增 `FScopedDebugBreakOptionsBinding` |
| 优先级 | P1 |

#### NewTest-16：为 `FindAssets` 补一条“脚本类名解析 + multicast 广播”场景测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `FindAssets` 分支、`FAngelscriptRuntimeModule::GetDebugListAssets()` |
| 现有测试覆盖 | 完全无测试；测试侧没有任何 `FindAssets`、`FAngelscriptFindAssets` 或 `GetDebugListAssets()` 的命中 |
| 风险评估 | 这条分支一旦回归，编辑器侧的“根据脚本类列出资产”能力会直接退化成无响应或 class 解析为空，而且因为它只是 delegate 广播，手工回归也很容易漏掉参数是否被正确转发。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Database.FindAssetsBroadcast` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDatabaseTests.cpp` |
| 场景描述 | 使用 `CreateBindingFixture()` 编译生成 `UDebuggerBindingFixture`，然后绑定 `FAngelscriptRuntimeModule::GetDebugListAssets()` 捕获回调参数。发送 `FindAssets` 消息，请求资产数组 `["/Game/Test/Foo", "/Game/Test/Bar"]` 和 `ClassName = "UDebuggerBindingFixture"`。 |
| 输入/前置 | 在 Shared 新增 `SendFindAssets(const TArray<FString>& Assets, const FString& ClassName)` helper；增加 `FScopedDebugListAssetsBinding`，负责注册并在析构时恢复 multicast 订阅。 |
| 期望行为 | 1. delegate 恰好触发 1 次。 2. 捕获到的 asset array 精确等于发送值，不丢顺序。 3. `BaseClass != nullptr`，且 `BaseClass->GetName() == "UDebuggerBindingFixture"`。 4. 再补一组缺省类名子场景时，`ClassName = "Missing.Script.Class"` 应该仍广播原 asset array，但 `BaseClass == nullptr`，证明错误路径不会吞请求。 |
| 使用的 Helper | `FAngelscriptDebuggerScriptFixture::CreateBindingFixture`、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、新增 `SendFindAssets(...)`、新增 `FScopedDebugListAssetsBinding` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 2 | NoTestForSource: 1, MissingScenario: 1 |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 14:20)

### 一、现有测试问题

#### Issue-15：`ClearThenResume` 的核心断言被 monitor 自己的 `StopDebugging` 旁路了，当前并没有真正验证 `ClearBreakpoints`

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume` |
| 行号范围 | 166-279，497-545，520-530；`AngelscriptDebugServer.cpp:897-923, 1222-1239` |
| 问题描述 | `StartBreakpointMonitor()` 在退出前固定执行 `MonitorClient.SendStopDebugging()`。而 runtime 的 `FAngelscriptDebugServer::HandleMessage(StopDebugging)` 会立即 `bIsDebugging = false` 并调用 `ClearAllBreakpoints()`。`ClearThenResume` 在首轮 invocation 结束后先 `FirstMonitorFuture.Get()`，也就是先等 monitor 把 `StopDebugging` 发完，再发送 `Client.SendClearBreakpoints(ClearBreakpoints)` 并等待 `BreakpointCount == 0`。结果是“断点已被清除”的前提，早在 monitor teardown 时就已经由 `StopDebugging` 全局完成；当前用例并不能区分“`ClearBreakpoints` 生效”与“monitor 先把整个 debug state 重置了”。 |
| 影响 | 这个用例名义上覆盖“删除断点后继续执行”，实际上验证的是“经历一次 `StopDebugging/StartDebugging` 周期后第二次运行不会停下”。如果 `ClearBreakpoints` 分支回归、只清错文件、或根本不处理消息，当前测试仍可能继续绿灯。 |
| 修复建议 | 把 monitor 和“控制调试会话生命周期”的职责分离。最直接的修复是给 `StartBreakpointMonitor()` 增加 `bSendStopDebuggingOnExit` 开关，并在 `ClearThenResume` 的首轮 monitor 中关闭它，只保留 `SendDisconnect()`；这样首轮结束后 server 仍保持 debugging 状态，随后再发送 `ClearBreakpoints` 才是真正的被测动作。更稳妥的做法是把 monitor 改成只负责 `Continue`/`RequestCallStack`，所有 `StartDebugging`/`StopDebugging` 都只由主 client 控制，并补一个断言确认 `Session.GetDebugServer().bIsDebugging` 在 `ClearBreakpoints` 前后始终为 `true`。 |

### 二、需要新增的测试

本轮先记录 `ClearThenResume` 的测试失真问题，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-15 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 23:43)

### 一、现有测试问题

#### Issue-18：现有 `StaticJIT` 直连测试全部跑在 `EditorContext`，实际上没有执行任何 transpiled code

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 测试名 | `Angelscript.CppTests.StaticJIT.PrecompiledData.EditorOnlyFlagRoundtrip`、`Angelscript.CppTests.StaticJIT.ModuleDiff.HighBitFlags` |
| 行号范围 | `AngelscriptPrecompiledDataTests.cpp:13-21`；关联配置：`StaticJITConfig.h:8-10` |
| 问题描述 | 当前仓库里唯一直接命中 `StaticJIT` 目录的自动化是 `AngelscriptPrecompiledDataTests.cpp` 这 2 个 `EditorContext` 用例；但 `StaticJITConfig.h` 在 `WITH_EDITOR` 下无条件定义 `AS_SKIP_JITTED_CODE`。结果是这些测试虽然 include 了 `PrecompiledData.h` 并验证了 precompiled 结构 roundtrip / module diff，却不会真正执行 transpiled function，也不会覆盖 `AngelscriptBytecodes.cpp`、`AngelscriptStaticJIT.cpp`、`StaticJITBinds.cpp`、`StaticJITHeader.cpp` 等运行时路径。 |
| 影响 | 现有自动化会给出“StaticJIT 相关测试通过”的表面信号，但这实际上只证明了 precompiled data 的少量结构行为，没有验证真实 JIT 代码是否可生成、可加载、可执行，也没有验证 JIT 调试元数据、native bind 与 fallback 语义。对用户要求里的“14 个 `StaticJIT/` 文件是否有覆盖”而言，这属于明显的高风险误导。 |
| 修复建议 | 保留这 2 个用例作为 `PrecompiledData` 结构回归，但不要再把它们当成 `StaticJIT` 运行时覆盖。新增一个明确的 non-`EditorContext` harness 或离线执行 harness，让 `NewTest-3` 这类 `ExecutionMatrix` 真正跑到 transpiled code；若短期内做不到，至少在测试命名/文档里把当前覆盖描述为“precompiled structure only”，避免误报为 JIT execution coverage。 |

### 二、需要新增的测试

#### NewTest-17：顶层帧执行 `StepOut` 时应直接完成脚本，而不是再次逐行停下

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `StepOut` 分支、`FAngelscriptDebugServer::ProcessScriptLine()` |
| 现有测试覆盖 | 有 `Angelscript.TestModule.Debugger.Stepping.StepOut`，但只覆盖“当前帧有 caller”的 happy path；没有任何用例在 `RunScenario()` 顶层帧上发送 `StepOut` |
| 风险评估 | `StepOut` 是核心调试命令。若顶层帧上的 `StepOut` 退化成“下一行继续停下”，当前套件会完全漏报，前端则会表现成 `StepOut` 像 `StepIn` 一样卡在入口函数里，直接破坏步进语义。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Stepping.StepOutTopFrameCompletes` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerStepOutEdgeTests.cpp` |
| 场景描述 | 复用 `CreateSteppingFixture()`，但把断点设在 `StepAfterCallLine`，确保首次停下时已经回到 `RunScenario()` 顶层帧。收到首个 `HasStopped` 后发送一次 `StepOut`，然后等待 invocation 自然完成。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateSteppingFixture`；在 Shared 抽一个 `StartActionThenExpectCompletionMonitor(...)` 或等价 helper，支持“首个 stop 后发送 `StepOut`，随后直到 invocation 完成期间必须没有额外 `HasStopped`”。 |
| 期望行为 | 1. 首个 stop 的 `Reason == "breakpoint"`，顶帧 `LineNumber == Fixture.GetLine(TEXT("StepAfterCallLine"))`，且 `Callstack.Frames.Num() == 1`。 2. 发送 `StepOut` 后不再收到第 2 个 `HasStopped`，同时 `MonitorResult.Error.IsEmpty()` 且 `!MonitorResult.bTimedOut`。 3. invocation 成功完成，`InvocationState->Result == 14`。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateSteppingFixture`、新增 `StartActionThenExpectCompletionMonitor(...)` |
| 优先级 | P0 |

#### NewTest-18：跨文件 `StepOver` 必须留在 caller source，而不是误入 callee 文件

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `StepOver` 分支、`FAngelscriptDebugServer::ProcessScriptLine()` |
| 现有测试覆盖 | 有 `Angelscript.TestModule.Debugger.Stepping.StepOver`，但只在 `DebuggerSteppingFixture.as` 单文件内验证；当前文档已补了跨文件 `StepIn` / `StepOut` 建议，仍缺“跨文件 `StepOver` 不应进入 callee source”这一条独立语义 |
| 风险评估 | 如果 `StepOver` 的 frame/source 条件判断只在单文件场景成立，现有套件仍会通过；真实前端会表现为“按了 StepOver 却跳进了另一个脚本文件”，这比单纯行号错误更难排查。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Stepping.CrossFileStepOverStaysInCaller` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerCrossFileSteppingTests.cpp` |
| 场景描述 | 基于两文件 fixture：文件 A 的 `RunScenario()` 调用文件 B 的 `Inner()`。在文件 A 的 call-site 断住后发送 `StepOver`，随后再 `Continue` 让脚本结束。 |
| 输入/前置 | 复用已建议的 `FAngelscriptDebuggerMultiFileFixture` / `CompileModuleSectionsFromMemory(...)`；step monitor 继续使用现有 `FStepMonitorPhase` 模式，但每个 stop 都要请求 callstack。 |
| 期望行为 | 1. 第 1 个 stop：`Reason == "breakpoint"`，顶帧 `Source` 以文件 A 结尾，`LineNumber` 命中文件 A 的 call-site marker。 2. `StepOver` 后第 2 个 stop：`Reason == "step"`，顶帧 `Source` 仍然以文件 A 结尾，`LineNumber` 命中文件 A 的 after-call marker。 3. 第 2 个 stop 的顶帧不得切到文件 B，且 frame depth 与第 1 个 stop 相同。 4. invocation 成功完成，返回值与 VM 预期一致。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、新增 `FAngelscriptDebuggerMultiFileFixture` / `CompileModuleSectionsFromMemory(...)`、现有 `StartStepMonitor(...)` |
| 优先级 | P1 |

补充覆盖快照：按 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` + `Plugins/Angelscript/Source/AngelscriptTest/` 对 `StaticJIT/` 14 个文件做 `rg --fixed-strings <filename>` 精确检索，当前直接文件名命中如下。

| StaticJIT 文件 | 测试侧文件名命中次数 | 备注 |
|------|------|------|
| `AngelscriptBytecodes.cpp` | 0 | 测试侧 0 次文件名命中 |
| `AngelscriptBytecodes.h` | 0 | 测试侧 0 次文件名命中 |
| `AngelscriptStaticJIT.cpp` | 0 | 测试侧 0 次文件名命中 |
| `AngelscriptStaticJIT.h` | 0 | 测试侧 0 次文件名命中 |
| `PrecompiledData.cpp` | 0 | 行为仅通过 `PrecompiledData.h` 间接触发 |
| `PrecompiledData.h` | 1 | `AngelscriptPrecompiledDataTests.cpp` 直接 include |
| `PrecompiledDataAllocator.h` | 0 | 测试侧 0 次文件名命中 |
| `StaticJITBinds.cpp` | 0 | 测试侧 0 次文件名命中 |
| `StaticJITBinds.h` | 0 | 测试侧 0 次文件名命中 |
| `StaticJITConfig.h` | 0 | 测试侧 0 次文件名命中 |
| `StaticJITHeader.cpp` | 0 | 测试侧 0 次文件名命中 |
| `StaticJITHeader.h` | 0 | 测试侧 0 次文件名命中 |
| `StaticJITHelperFunctions.h` | 0 | 测试侧 0 次文件名命中 |
| `StringInArchive.h` | 0 | 测试侧 0 次文件名命中 |

额外高风险说明：唯一直接命中的 `PrecompiledData.h` 仍然只被 `EditorContext` 用例消费，而 `StaticJITConfig.h:8-10` 在 `WITH_EDITOR` 下定义 `AS_SKIP_JITTED_CODE`；因此当前这张快照最多只能说明“有少量 precompiled structure 覆盖”，不能说明 14 个文件里的真实 transpiled execution 路径被自动化执行过。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-18 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingEdgeCase: 1 |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-08 23:52)

### 一、现有测试问题

#### Issue-19：`StartAndWaitFor*MonitorReady()` 把“monitor 已失败退出”误判成 ready，启动错误会被延后成更模糊的用例失败

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:166-216, 281-305`；`AngelscriptDebuggerSteppingTests.cpp:172-221, 305-329` |
| 问题描述 | `StartBreakpointMonitor()` / `StartStepMonitor()` 在两类失败路径都会先把 `bMonitorReady = true` 再返回：1. `MonitorClient.Connect(...)` 失败时；2. 握手超时、没有拿到 `DebugServerVersion` 时。随后 `StartAndWaitForMonitorReady()` / `StartAndWaitForStepMonitorReady()` 只等待这个布尔值，就把 helper 判定为成功，完全不检查 future 是否已经带着 `Result.Error` 提前结束。结果是调用方会继续发送断点、启动脚本 invocation，把“monitor 根本没起来”延后成后面的 invocation timeout、`StopEnvelopes.Num() == 0` 或更模糊的断言失败。 |
| 影响 | 这让 Shared monitor helper 的契约本身失真：`ready` 实际上只是“线程已经退出或进入监听循环”，不是“已成功完成握手并准备收 stop”。正向用例会浪费整轮 session timeout 才暴露 monitor 启动失败；负向用例则更容易落入已记录的“0 个 stop 也算成功”误报。 |
| 修复建议 | 把 monitor 启动状态拆成显式成功/失败信号，而不是复用单个 `bMonitorReady`。最直接的做法是新增 `TAtomic<EAsyncMonitorStartState>` 或 `TPromise<TOptional<FString>> StartupResult`：只有在收到 `DebugServerVersion` 后才标记 `Ready`，连接失败/握手超时则标记 `Failed` 并附带错误。`StartAndWaitFor*MonitorReady()` 在等待结束后必须分支检查启动状态，失败时立刻 `AddError(Result.Error)` 并返回 `false`，不要再让测试继续跑到后面的 invocation/stop 断言。 |

### 二、需要新增的测试

本轮先记录 monitor 启动 helper 的契约问题，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-19 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:10)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：`2026-04-09 00:04`（`Issue-21`）、`2026-04-09 00:06`（`NewTest-19`、`NewTest-20`）与 `2026-04-09 00:08`（索引补充）均已写入本文；继续审查时请按这些时间戳检索，避免只从文件尾部向上阅读时遗漏本轮新增。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:23)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：`2026-04-09 00:17`（`Issue-22`）、`2026-04-09 00:19`（`NewTest-21`）与 `2026-04-09 00:21`（`Issue-23`）均已写入本文；继续审查时请按这些时间戳检索，避免只从文件尾部向上阅读时遗漏本轮新增。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:32)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：`2026-04-09 00:27`（`Issue-24`、`Issue-25`）已写入本文；由于文档历史段落存在时间戳乱序，继续审查时请按该时间戳检索本轮新增，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 00:44)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：`2026-04-09 00:43`（`Issue-26`、`Issue-27`）已写入本文；由于文档历史段落存在时间戳乱序，继续审查时请按该时间戳检索本轮新增，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 01:02)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：`2026-04-09 00:58`（`NewTest-22`）与 `2026-04-09 01:00`（`NewTest-23`）已写入本文；由于文档历史段落存在时间戳乱序，继续审查时请按这两个时间戳检索本轮新增，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 14:11)

### 一、现有测试问题

#### Issue-28：3 个步进用例只校验行号和栈深，不校验 `Source`，源码定位回归仍可能误报通过

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | 437-449，532-550，634-651 |
| 问题描述 | 3 个步进用例在拿到 `CallStack` 后，都只断言 `Frames[0].LineNumber` 和部分 `Frames.Num()`，从未校验任一 stop 的 `Frames[0].Source` 是否仍然指向 `DebuggerSteppingFixture.as`。这意味着只要错误的 stop 恰好落在相同行号，或 debug server 把源码映射到了错误文件但保留了正确行号，当前断言仍可能全部通过。现有 `StepIn`/`StepOver`/`StepOut` 事实上只证明了“某个行号序列成立”，没有证明“IDE 会跳到正确脚本文件”。 |
| 影响 | 当前单文件步进回归对源码定位异常不敏感。真实前端会表现为步进后跳到错误文件、错误 section，或者显示 stale source path；而现有 3 个 happy-path 用例仍可能绿灯，削弱它们对 stepping/source mapping 的保护力度。 |
| 修复建议 | 在现有 `CallStack` 断言旁补齐 `TestTrue(TEXT(\"... source should stay in the stepping fixture\"), MonitorResult.Stops[i].Callstack->Frames[0].Source.EndsWith(Fixture.Filename))`。`StepIn` 至少校验第 1、2 个 stop 的 `Source`，`StepOver` 校验两个 stop 都仍在 `Fixture.Filename`，`StepOut` 校验第 2、3 个 stop 在返回前后都来自预期文件；这样单文件场景先把“文件对不对”锁死，再由已记录的跨文件新增用例覆盖 source 切换语义。 |

### 二、需要新增的测试

本轮先记录现有步进断言的源码定位问题，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-28 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 14:14)

### 一、现有测试问题

#### Issue-29：`ClearThenResume` 用 `DrainPendingMessages()` 静默丢弃第二轮前的协议残留，Shared helper 还会把真实错误清零

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:520-545`；`AngelscriptDebuggerTestClient.cpp:223-238` |
| 问题描述 | `ClearThenResume` 在发送 `ClearBreakpoints` 并等待 `BreakpointCount == 0` 后，直接调用 `Client.DrainPendingMessages()` 再开始第二轮监控。这个 helper 会把所有已到达但未消费的 envelope 无条件吞掉，而且一旦 `ReceiveEnvelope()` 因 socket/反序列化错误返回空值，它在退出前还会执行 `LastError.Reset()`。结果是第二轮之前若出现意外 `SetBreakpoint` 回包、残留 `HasContinued`、脏 `Diagnostics`，甚至协议解析错误，当前测试都会静默继续，完全失去“第二轮开始前连接是干净的”这个前提检查。 |
| 影响 | `ClearThenResume` 现在不仅无法证明 `ClearBreakpoints` 自身生效，还会把第一轮和清理阶段遗留的协议污染主动抹掉。这样一来，即使 server 在 `StopDebugging`/`ClearBreakpoints` 周期里发出了错误消息或把 socket 状态弄脏，第二轮也可能继续绿灯，调试价值进一步下降。 |
| 修复建议 | 不要在主路径里使用“盲清空” helper。把 `DrainPendingMessages()` 改成显式返回错误状态的 collector，例如 `CollectPendingMessages(...)`：1. 保留并上抛 `LastError`；2. 返回完整消息列表供调用方断言。`ClearThenResume` 应改成只允许明确白名单消息，最好直接断言第二轮开始前 `PendingMessages.Num() == 0`；若确实需要忽略某类 envelope，也应把忽略条件写在测试体里，而不是由 Shared helper 无声吞掉。 |

### 二、需要新增的测试

本轮先记录 `DrainPendingMessages()` 的 helper 失真问题，新增测试建议将在后续分段继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-29 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 14:18)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-24：`StopDebugging` 在已暂停会话里必须同时清掉 pause state 与断点

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `StopDebugging` 分支、`FAngelscriptDebugServer::ClearAllBreakpoints()` |
| 现有测试覆盖 | `Smoke.Handshake` 只验证了“空会话下发送 `StopDebugging` 后 `bIsDebugging` 变 false”；3 个 breakpoint/3 个 stepping 用例虽然都会在 teardown 或 monitor 退出时发送 `StopDebugging`，但从不在“命中断点后仍处于 paused 状态”时断言 `bIsPaused`、`BreakpointCount` 和后续运行结果。因此当前没有任何用例覆盖 `StopDebugging` 的真实 cleanup 语义。 |
| 风险评估 | 如果 `StopDebugging` 只退出 debugging 模式却没清 `bIsPaused` 或残留断点，IDE 在一次停止调试后会继续收到旧断点命中，甚至把下一次运行卡在 paused 状态；现有 7 个 debugger 用例不会发现。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Session.StopDebuggingClearsPausedState` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerLifecycleTests.cpp`，与 `NewTest-22` 共用 lifecycle/session helper，整文件控制在 300-500 行 |
| 场景描述 | 复用 `CreateBreakpointFixture()`。进入 debugging 后，在 `BreakpointHelperLine` 设置断点，并启动一个“不自动 `Continue`、不自动 `StopDebugging`”的 monitor 变体，确保脚本第一次命中断点后仍停在 paused 状态。确认首个 stop 到达后，由主 client 显式发送 `StopDebugging`，等待 server 完成状态收口；随后不重新设置任何断点，再次运行同一脚本。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture`、`DispatchModuleInvocation(...)`；Shared 需要新增 `StartBreakpointMonitor(..., bSendContinueOnStop=false, bSendStopDebuggingOnExit=false)` 或等价 `WaitForSingleStopWithoutContinue(...)` helper，避免再次落入 `Issue-15` 的 helper 旁路。 |
| 期望行为 | 1. 首个 `HasStopped` 能正常反序列化，`Reason == "breakpoint"`，顶帧 `Source`/`LineNumber` 命中 `BreakpointHelperLine`。 2. 发送 `StopDebugging` 后，`Session.GetDebugServer().bIsDebugging == false`、`Session.GetDebugServer().bIsPaused == false`、`Session.GetDebugServer().BreakpointCount == 0`。 3. 第二次运行 `RunScenario()` 时 monitor 不再收到任何 `HasStopped`，且 `Error.IsEmpty()`、`!bTimedOut`。 4. 第二次 invocation 成功完成，`Result == 8`。 |
| 使用的 Helper | `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、`FAngelscriptDebuggerScriptFixture::CreateBreakpointFixture`、`DispatchModuleInvocation(...)`、`WaitForSpecificBreakpoint(...)`、新增 `WaitForSingleStopWithoutContinue(...)` / monitor 变体 |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 01:29)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-25：客户端主动 `Pause` 必须在下一条脚本行停下，并返回 `pause` reason

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 关联函数 | `FAngelscriptDebugServer::HandleMessage()` 的 `Pause` 分支、`FAngelscriptDebugServer::ProcessScriptLine()` 的 `bBreakNextScriptLine` 路径、`FAngelscriptDebugServer::PauseExecution()` |
| 现有测试覆盖 | 现有 7 个 debugger 场景用例从不发送 `Pause`；仓库里唯一碰到 `EDebugMessageType::Pause` 的自动化只有 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` 对空 body envelope 的 transport roundtrip，它不覆盖“脚本运行中收到 `Pause` 后是否真的停下、stop reason 是什么、恢复后能否继续执行” |
| 风险评估 | `Pause` 是调试器核心公开动作，但当前没有任何场景测试锁住它的运行时语义。如果 `Pause` 不能在下一条脚本行可靠停下、把用户主动暂停错误报告成 `step`/`breakpoint`、或恢复后无法继续执行，IDE 的 Pause 按钮会直接失真，而现有 7 个用例仍会全部绿灯。 |
| 建议测试名 | `Angelscript.TestModule.Debugger.Session.PauseStopsAtNextScriptLine` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerPauseTests.cpp`，文件只放 pause/resume 方向 1-2 个 `ASTEST_*` 用例，控制在 300-500 行 |
| 场景描述 | 在 `Shared/` 新增 `FAngelscriptDebuggerScriptFixture::CreatePauseFixture()`：入口函数先执行一条可断住的 `PauseReadyLine`，随后进入带 marker 的长循环 `PauseLoopLine`，保证 `Continue` 后仍有足够执行窗口。测试先在 `PauseReadyLine` 设置普通断点；monitor 收到第 1 个 `HasStopped` 后发送 `Continue`，并继续记录协议消息。主 client 在收到对应 `HasContinued` 后立即发送 `Pause`，等待第 2 个 `HasStopped`。 |
| 输入/前置 | 复用 `FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、现有 `DispatchModuleInvocation(...)`；Shared 复用 `NewTest-19` 已要求增加的 `CollectProtocolMessagesUntil(...)`/协议采集 helper，同时新增 `CreatePauseFixture()`，避免用现有短脚本造成“Pause 还没发出脚本就已结束”的时序噪声。 |
| 期望行为 | 1. 第 1 个 stop 反序列化后 `Reason == "breakpoint"`，顶帧 `Source`/`LineNumber` 命中 `PauseReadyLine`。 2. monitor 发送 `Continue` 后收到 1 条 `HasContinued`。 3. 主 client 发送 `Pause` 后，第 2 个 stop 在 timeout 内到达，`Reason == "pause"`，顶帧 `Source` 仍以 fixture 文件名结尾，`LineNumber` 命中 `PauseLoopLine`。 4. 第 2 个 stop 后再发送 `Continue`，invocation 能成功完成，`MonitorResult.Error.IsEmpty()`、`!MonitorResult.bTimedOut`，且返回值等于 fixture 预期。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptDebuggerTestSession`、`FAngelscriptDebuggerTestClient`、新增 `FAngelscriptDebuggerScriptFixture::CreatePauseFixture()`、`CollectProtocolMessagesUntil(...)`、现有 `DispatchModuleInvocation(...)` |
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
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |
---

## 测试审查 (2026-04-09 23:34)

### 一、现有测试问题

本轮新增 `Issue-44`、`Issue-45`，详见本文对应条目。

### 二、需要新增的测试

本轮新增 `NewTest-40`，详见本文对应条目。

尾部索引说明：本轮新增 `Issue-44`、`Issue-45`、`NewTest-40` 已写入本文；由于该文档历史上已存在段落顺序错位，继续审查时请按编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-44 |
| BadIsolation | 1 | Issue-45 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 23:31)

### 一、现有测试问题

本轮新增 `Issue-44`、`Issue-45`，详见本文对应条目。

### 二、需要新增的测试

本轮新增 `NewTest-40`，详见本文对应条目。

尾部索引说明：本轮新增 `Issue-44`、`Issue-45`、`NewTest-40` 已写入本文；由于该文档历史上已存在段落顺序错位，继续审查时请按编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-44 |
| BadIsolation | 1 | Issue-45 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 23:22)

### 一、现有测试问题

#### Issue-44：6 个断点/步进用例只看 `BreakpointCount`，没有验证 `SetBreakpoint` 协议回执

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.HitLine`、`Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume`、`Angelscript.TestModule.Debugger.Breakpoint.IgnoreInactiveBranch`、`Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:370-380,482-492,618-628`；`AngelscriptDebuggerSteppingTests.cpp:386-396,494-504,596-606`；关联运行时语义：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:955-1057` |
| 问题描述 | 这 6 个用例在 `Client.SendSetBreakpoint(...)` 后，唯一的注册成功证据都是 `WaitForBreakpointCount(..., 1)`。它们既不读取任何 `EDebugMessageType::SetBreakpoint` 回执，也不验证服务端是否把请求行号重映射到最近可执行行、是否按 `Id` 回传 changed/remove ack、或者是否因为重复断点而把 `LineNumber` 退成 `-1`。而 runtime 的 `HandleMessage(SetBreakpoint)` 明确包含这些协议分支：当 `CodeLine != WantedLine && BP.Id != -1` 时会回发修正行号；当断点不可设置或重复且 `BP.Id != -1` 时会回发 `LineNumber = -1`。当前场景测试发送的 `FAngelscriptBreakpoint` 还全部保留默认 `Id = -1`，等于主动绕开了协议回执面，只剩服务器内部计数这一个弱信号。 |
| 影响 | 现有 happy-path 只能证明“server 内部某处有一个断点计数”，不能证明“客户端请求的那个断点被按正确的行号和 `Id` 接受”。一旦断点重映射、重复断点处理、无效行拒绝或 `Id` 回写语义回归，IDE 端看到的将是错行、悬空断点或丢失 ack，而这 6 个用例仍可能继续绿灯。 |
| 修复建议 | 抽一个统一 helper，例如 `SetBreakpointAndAssertAck(...)`：1. 调用前给每个请求设置显式 `Id`；2. 注册后先 drain/等待 `SetBreakpoint` 相关 envelope，而不是只看 `BreakpointCount`；3. 若收到 ack，断言 `Id` 与请求一致，且 `LineNumber` 等于预期命中行或显式 `-1`；4. 再补一个 server-side 精确断言，确认 `Session.GetDebugServer()` 中保存的就是该 fixture 的目标行，而不是“任意一个断点计数为 1”。这样 `Breakpoint` 与 `Stepping` 套件都能真正锁住 set-breakpoint contract，而不是只锁住内部计数。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-44 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 01:35)

### 一、现有测试问题

#### Issue-30：`ReceiveEnvelope` 不检测远端已断开，Shared client 会把真实断线伪装成普通超时

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.*`（通过 `FAngelscriptDebuggerTestClient` 间接受影响） |
| 行号范围 | 131-180，324-351 |
| 问题描述 | `ReceiveEnvelope()` 只调用 `AppendReceivedData()` 和 `ConsumeEnvelope()`；而 `AppendReceivedData()` 仅在 `HasPendingData()` 为真时才 `Recv()`，从不检查 `Socket->GetConnectionState()`。结果是当 debug server 提前关闭连接、monitor 在 paused 状态被踢掉、或 socket 进入 `SCS_ConnectionError` 时，只要本地缓冲区为空，`ReceiveEnvelope()` 仍然返回“没有 envelope，也没有 error”。随后 `WaitForMessage()` / `WaitForMessageType()` 就会一直空转到 timeout，把真实的 peer disconnect 记成“等待消息超时”。 |
| 影响 | 现有 smoke / breakpoint / stepping 以及后续所有 debugger 协议测试，在 server 先断开或连接半关闭时都拿不到准确诊断，只会看到模糊的 timeout。这样既放大排障成本，也会把“连接已死”的硬失败误判成“消息稍晚一点到”的偶发抖动。 |
| 修复建议 | 在 `ReceiveEnvelope()` 或 `AppendReceivedData()` 增加显式连接状态检查：当 `Socket != nullptr` 且 `GetConnectionState()` 落到 `SCS_NotConnected` / `SCS_ConnectionError` 时，若当前 `ReceiveBuffer` 也拼不出完整 envelope，应立即 `SetError(...)`，把“远端主动断开”“连接错误”“本端尚未连接完成”区分开。更稳妥的做法是新增 `CheckSocketLiveness()` helper，供 `ReceiveEnvelope()`、`WaitForMessage()`、`WaitForMessageType()` 统一复用，并在错误信息中附带最近一次目标消息类型与 timeout，避免继续把 disconnect 伪装成 timeout。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-30 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 01:45)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮已新增 `NewTest-26`、`Issue-31`、`Issue-32`。由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按编号检索这些条目，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 15:01)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `Issue-33`、`Issue-34`、`NewTest-27` 已写入本文；由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按这些编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 11:46)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-31：`FScopeJITDebugCallstack` 必须正确 push/pop `Execution.debugCallStack`，不能泄漏或串错父子帧

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h` |
| 关联函数 | `FScopeJITDebugCallstack::FScopeJITDebugCallstack(...)`、`~FScopeJITDebugCallstack()` |
| 现有测试覆盖 | 完全无直接测试。当前文档里的 `NewTest-5` 只要求检查生成输出是否写出 `SCRIPT_DEBUG_CALLSTACK_*` 宏，但没有任何自动化真正构造 `FScopeJITDebugCallstack` 并验证 `Execution.debugCallStack` 在嵌套作用域中的 push/pop 语义。 |
| 风险评估 | 这条 RAII 链是 JIT 调试 callstack、异常栈和源码定位的运行时基础。如果构造/析构不再正确恢复 `PrevFrame`，或把 `Filename`/`FunctionName`/`LineNumber` 挂到错误节点，真实前端会看到 stale frame、串栈或作用域退出后仍残留旧帧；当前所有 debugger / StaticJIT 自动化都不会报警。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.DebugCallstack.ScopePushPop` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITDebugCallstackTests.cpp`，使用 `ASTEST_*` 宏组织 1-2 个 JIT debug-callstack 单测，整文件控制在 300-500 行 |
| 场景描述 | 在最小 testing engine scope 下获取 `asCThreadLocalData*`，构造一个本地 `FScriptExecution Execution(tld)`。先创建 outer `FScopeJITDebugCallstack(Execution, "Outer.as", "Outer", 11, OuterThis)`，断言 `Execution.debugCallStack` 指向 outer；随后在其内部再创建 inner `FScopeJITDebugCallstack(Execution, "Inner.as", "Inner", 27, InnerThis)`，断言当前顶帧切到 inner，且 `PrevFrame` 指向 outer。离开 inner 作用域后应恢复到 outer，离开 outer 后应恢复为 `nullptr`。 |
| 输入/前置 | 复用 `FAngelscriptEngineScope` 或等价 minimal engine helper，确保当前线程存在有效 `asCThreadLocalData`；测试中使用稳定的 sentinel 指针值作为 `ThisObject`，例如两个不同的 dummy object / 地址常量，便于断言 frame 字段没有串值。 |
| 期望行为 | 1. outer scope 期间，`Execution.debugCallStack` 非空且顶帧 `Filename == "Outer.as"`、`FunctionName == "Outer"`、`LineNumber == 11`、`ThisObject == OuterThis`。 2. inner scope 期间，`Execution.debugCallStack` 切到 inner，顶帧字段更新为 inner 值，且 `static_cast<FScopeJITDebugCallstack*>(Execution.debugCallStack)->PrevFrame` 仍指向 outer。 3. inner 析构后，`Execution.debugCallStack` 恢复成 outer；outer 析构后，`Execution.debugCallStack == nullptr`。 4. 整个过程中 `Execution.bExceptionThrown` 保持 `false`，证明 callstack helper 本身没有副作用。 |
| 使用的 Helper | `ASTEST_*` 宏、`FAngelscriptEngineScope`、`FScriptExecution`、`FScopeJITDebugCallstack` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 11:54)

### 一、现有测试问题

#### Issue-38：3 个步进用例在最后一次动作后立刻收口并 `StopDebugging`，会把“恢复后又多停一次”的回归直接遮掉

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | `AngelscriptDebuggerSteppingTests.cpp:172-300, 375-423, 483-530, 584-632` |
| 问题描述 | `StartStepMonitor()` 每次在处理完最后一个 configured phase 后都会立刻 `break`（287-290），随后在退出路径固定执行 `MonitorClient.SendStopDebugging()`（298）。3 个步进用例本身也都是在 `MonitorResult.Stops.Num() == 2/3` 后直接判定通过（419-422, 527-530, 629-632）。结果是只要第 1/2/3 个 stop 前缀看起来对，monitor 就会在最后一次 `StepIn` / `StepOver` / `Continue` / `StepOut` 发出后马上停止观察并主动把整个调试会话关掉，完全看不到“恢复后又错误地产生第 3/4 个 `HasStopped`”“`Continue` 后没有 `HasContinued` 就再次停下”这类尾段回归。 |
| 影响 | 当前 3 个步进 happy-path 只能证明“预期前缀 stop 序列存在”，不能证明“最后一次步进/继续之后脚本真的顺利跑完且没有额外 stopped event”。真实 IDE 会表现成最后一步后 UI 又被重新拉回 paused、step 命令收尾不干净、或 `Continue`/`StepOut` 之后多发一次停点，而现有测试仍可能绿灯，因为 helper 已提前结束监控并用 `StopDebugging` 抹平现场。 |
| 修复建议 | 把步进 monitor 改成 transcript 型 collector，而不是“处理完 phase 就退出”。最小修复是增加 `bKeepCollectingAfterLastAction` / `ExpectedFinalState` 配置：在发出最后一个 `Continue`/`StepOut` 后继续监听到 invocation 完成，并显式断言“之后没有新的 `HasStopped`，且恰好收到 1 条 `HasContinued`”。若暂时不重构 helper，至少为 `StepIn`/`StepOver`/`StepOut` 新增一个 `CollectMessagesUntilInvocationComplete(...)` 变体，禁止 monitor 在被测动作刚发出后立即 `SendStopDebugging()`。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-38 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 11:57)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-32：`FName` 等值比较必须在真实 transpiled execution 下保持与 VM 一致，补上 `StaticJITHelperFunctions.h` 的直接行为覆盖

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHelperFunctions.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FName.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` |
| 关联函数 | `FStaticJITHelperFunctions::FName_Equals`、`Bind_FName.cpp` 中的 `FName_.Method("bool opEquals(const FName& Other) const", ...)` |
| 现有测试覆盖 | 当前仓库里确实有普通 binding 测试会在 VM/解释执行路径上比较 `FName`，但 `StaticJIT/` 侧对 `StaticJITHelperFunctions.h` 仍是 0 次直接文件命中，也没有任何测试要求“目标函数必须进入 `FJITDatabase` 并在 transpiled execution 下走完 `FName == FName` 这条 helper 路径” |
| 风险评估 | 一旦 `FUNC_TRIVIAL` 到 JIT native call 的映射回归、helper 签名/导出变化、或生成代码在 `FName` 比较上静默退回错误路径，现有普通 binding 测试仍会全部绿灯，因为它们只证明 VM/解释执行没坏。真实启用 StaticJIT 的项目则可能在最常见的 `FName` 比较上出现编译失败、结果错误或仅 JIT 模式失配。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.HelperFunctions.FNameEqualsParity` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITFNameTests.cpp`，文件只放 `FName` / trivial-helper 方向 1-2 个 `ASTEST_*` 用例，控制在 300-500 行 |
| 场景描述 | 使用非 `EditorContext` 的 `FStaticJITExecutionHarness` 或等价 harness 编译一个最小脚本模块，例如 `int CheckSame() { return FName("Alpha") == FName("Alpha") ? 1 : 0; }` 与 `int CheckDifferent() { return FName("Alpha") == FName("Beta") ? 1 : 0; }`。先走 VM 记录基线结果，再生成并加载 StaticJIT 产物执行同一入口。 |
| 输入/前置 | 复用 `NewTest-3` 已要求建立的 `FStaticJITExecutionHarness`：它必须能显式断言目标函数已经注册到 `FJITDatabase::Get().Functions`，避免测试在 VM fallback 上假绿。若 harness 已支持收集生成源码，可顺手暴露 `GetGeneratedSourceForFunction(...)` 供可选文本断言使用。 |
| 期望行为 | 1. `CheckSame` 在 VM 与 JIT 下都返回 `1`。 2. `CheckDifferent` 在 VM 与 JIT 下都返回 `0`。 3. harness 能证明两个目标函数都拿到了 JIT entry，而不是退回解释执行。 4. 若生成源码可观测，源码中应出现 `FStaticJITHelperFunctions::FName_Equals` 或等价 direct trivial-native 调用，证明不是绕回 generic fallback。 |
| 使用的 Helper | `ASTEST_*` 宏、`FStaticJITExecutionHarness`、`CompileModuleFromMemory`、`ExecuteIntFunction`、可选 `GetGeneratedSourceForFunction(...)` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 12:07)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `Issue-39`、`NewTest-33`、`NewTest-34` 已写入本文；由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按这些编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 12:13)

### 一、现有测试问题

本轮已新增 `Issue-40`，详见本文对应条目。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `Issue-40` 已写入本文；由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按该编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-40 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |
---

## 测试审查 (2026-04-09 12:27)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-35：`StaticJITHeader` 的异常 helper 必须把 `bExceptionThrown` 和错误文本映射到正确分支

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 关联函数 | `FStaticJITFunction::SetException()`、`SetNullPointerException()`、`SetDivByZeroException()`、`SetOverflowException()`、`SetUnboundFunctionException()`、`SetOutOfBoundsException()`、`SetSwitchValueInvalidException()`、`SetUnknownException()` |
| 现有测试覆盖 | 完全无直接测试。当前文档已覆盖 `FJITDatabase` 注册、`FScopeJITDebugCallstack` push/pop 和 `FName_Equals` helper，但 `StaticJITHeader.cpp:104-161` 这一组异常桥接函数仍然没有任何自动化命中。 |
| 风险评估 | 这些 helper 是 transpiled execution 把 native/JIT 故障翻译成脚本异常的统一出口。若枚举分支映射错文案、wrapper 忘记置 `Execution.bExceptionThrown`、或后续重构把某个 helper 指向错误消息，JIT 运行时会在最关键的错误路径上静默失真，而现有 `PrecompiledData` 结构测试和 debugger 场景都不会报警。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.ExceptionHelpers.MapExpectedErrors` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITExceptionTests.cpp`，文件只放 exception-helper 方向 2-3 个 `ASTEST_*` 用例，控制在 300-500 行 |
| 场景描述 | 在最小 testing engine scope 下构造 `FScriptExecution Execution(FAngelscriptEngine::GameThreadTLD)`。测试主体用表驱动遍历 `EJITException::NullPointer / Div0 / Overflow / UnboundFunction / OutOfBounds / Unknown`，分别调用 `FStaticJITFunction::SetException(Execution, EnumValue)`；随后再单独调用 `SetNullPointerException()`、`SetDivByZeroException()`、`SetOverflowException()`、`SetUnboundFunctionException()`、`SetOutOfBoundsException()`、`SetSwitchValueInvalidException()`、`SetUnknownException()`，验证 wrapper 与总入口的行为一致。每个 case 前使用 `AddExpectedErrorPlain(...)` 或等价 log-capture helper 注册预期日志，case 间把 `Execution.bExceptionThrown` 重置为 `false`。 |
| 输入/前置 | 复用 `ASTEST_CREATE_ENGINE_FULL()` / `ASTEST_BEGIN_FULL`，确保 `FAngelscriptEngine::GameThreadTLD` 已初始化；测试文件直接 include `StaticJIT/StaticJITHeader.h` 与 `source/as_context.h` 以访问 `FScriptExecution`。若日志断言需要避免全局串扰，可在同文件补一个轻量 `FScopedExpectedAngelscriptError` helper 包装 `AddExpectedErrorPlain(...)`。 |
| 期望行为 | 1. 每次调用 helper 后 `Execution.bExceptionThrown == true`。 2. `SetException(EJITException::NullPointer/Div0/Overflow/UnboundFunction/OutOfBounds/Unknown)` 分别产生日志文本 `TXT_NULL_POINTER_ACCESS`、`TXT_DIVIDE_BY_ZERO`、`TXT_DIVIDE_OVERFLOW`、`TXT_UNBOUND_FUNCTION`、`Index out of bounds.`、`Unknown exception.`。 3. 专用 wrapper 的日志文本与对应 enum 分支完全一致。 4. `SetSwitchValueInvalidException()` 单独产生日志 `Invalid enum value passed to switch`，避免被 `Unknown` 分支吞掉。 |
| 使用的 Helper | `ASTEST_*` 宏、`FScriptExecution`、`FStaticJITFunction`、`FAutomationTestBase::AddExpectedErrorPlain` 或新增 `FScopedExpectedAngelscriptError` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 12:28)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-36：`ScriptCallNative` 的 generic bridge 必须恢复 `activeFunction` 并拦截空 `this`

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_callfunc.h` |
| 关联函数 | `FStaticJITFunction::ScriptCallNative()` |
| 现有测试覆盖 | 完全无直接测试。当前 `StaticJIT` 缺口文档已经覆盖 database 注册、debug callstack、helper parity 和 generated output，但 `StaticJITHeader.cpp:169-307` 这条 native bridge 仍是 0 次文件级行为断言。 |
| 风险评估 | 这是 transpiled code 调用 generic/native system function 的统一入口。若 bridge 忘记恢复 `tld->activeFunction`，调试 callstack、异常归属和 `asGetActiveFunction()` 相关逻辑都会串到错误函数；若 generic method 的空 `this` 没被拦截，JIT 路径会把应有的脚本异常退化成崩溃或未定义行为。现有 `Debugger/` 7 个用例和 `PrecompiledData` 结构测试都无法发现这类回归。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.NativeBridge.GenericCallRestoresState` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITNativeBridgeTests.cpp`，文件集中放 `ScriptCallNative` / `FScopeInformSystemFunction` 相邻桥接回归 2-3 个 `ASTEST_*` 用例，控制在 300-500 行 |
| 场景描述 | 仿照 `AngelscriptPrecompiledDataTests.cpp` 直接 include internal AngelScript headers，手工构造最小 `asCScriptFunction` + `asSSystemFunctionInterface`。Case 1 使用 `ICC_GENERIC_FUNC`，把 `sysFunc->func` 指向一个测试回调 `GenericProbe(asIScriptGeneric*)`；回调里记录 `asGetActiveFunction()`，并写入固定返回值。调用 `FStaticJITFunction::ScriptCallNative(...)` 后，验证 callback 观察到的 active function 正是传入的 `descr`，且返回值被正确写回 `valueRegister`。Case 2 使用 `ICC_GENERIC_METHOD`，把栈上的 object pointer 置空；调用 bridge 后不应进入 probe callback，而应走 `SetException(EJITException::NullPointer)`。 |
| 输入/前置 | 复用 `ASTEST_CREATE_ENGINE_FULL()` / `ASTEST_BEGIN_FULL` 保证 `FAngelscriptEngine::GameThreadTLD` 可用；测试文件 include `Core/angelscript.h`、`source/as_context.h`、`source/as_callfunc.h`、`source/as_scriptfunction.h`。为减少样板，可在同文件加一个轻量 `FScopedFakeSystemFunction` helper，负责分配/回收 `asCScriptFunction`、`asSSystemFunctionInterface` 和用于 `l_sp` 的小栈缓冲。 |
| 期望行为 | 1. `ICC_GENERIC_FUNC` case 中，probe callback 观察到的 `asGetActiveFunction()` 等于传入 `descr`，返回后 `Execution.tld->activeFunction` 恢复成调用前 sentinel。 2. 同一 case 中，`Execution.bExceptionThrown == false`，且 `*valueRegister` 等于 probe 写入的固定返回值。 3. `ICC_GENERIC_METHOD` + null `this` case 中，probe callback 不会被调用，`Execution.bExceptionThrown == true`，并记录 `TXT_NULL_POINTER_ACCESS` 预期错误。 4. 两个 case 结束后 `Execution.tld->activeFunction` 都恢复为进入前 sentinel，证明 bridge 不会把 active function 泄漏到后续调用。 |
| 使用的 Helper | `ASTEST_*` 宏、`FScriptExecution`、`FStaticJITFunction::ScriptCallNative`、`asGetActiveFunction()`、`FAutomationTestBase::AddExpectedErrorPlain`、新增 `FScopedFakeSystemFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 12:54)

### 一、现有测试问题

本轮已新增 `Issue-41`，详见本文对应条目。

### 二、需要新增的测试

本轮已新增 `NewTest-37`，详见本文对应条目。

尾部索引说明：本轮新增 `Issue-41`、`NewTest-37` 已写入本文；由于文档历史段落存在插入顺序不完全按时间排序的情况，继续审查时请按这些编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 1 | Issue-41 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |
---

## 测试审查 (2026-04-09 13:12)

### 一、现有测试问题

#### Issue-43：6 个 breakpoint/stepping 用例的 teardown 只发 `StopDebugging`/`Disconnect`，却没有再 pump server，实际 cleanup 未被验证

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.HitLine`、`Angelscript.TestModule.Debugger.Breakpoint.ClearThenResume`、`Angelscript.TestModule.Debugger.Breakpoint.IgnoreInactiveBranch`、`Angelscript.TestModule.Debugger.Stepping.StepIn`、`Angelscript.TestModule.Debugger.Stepping.StepOver`、`Angelscript.TestModule.Debugger.Stepping.StepOut` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:336-344, 448-456, 584-592`；`AngelscriptDebuggerSteppingTests.cpp:360-368, 468-476, 570-576`；关联基础设施：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestSession.cpp:102-149` |
| 问题描述 | 这 6 个用例在 `ON_SCOPE_EXIT` 里都会执行 `Client.SendStopDebugging()`、`Client.SendDisconnect()`、`Client.Disconnect()`，然后立刻 `Engine.DiscardModule(...)` 和 `CollectGarbage(...)`。但 `FAngelscriptDebuggerTestSession` 只有在 `PumpOneTick()` / `PumpUntil()` 时才会驱动 `DebugServer->Tick()` 处理 socket 消息；teardown 本身没有任何额外 pump。也就是说，这些 cleanup control message 在退出路径上只是“发出去了”，并没有被测试验证为“已经被 server 消费并把 `bIsDebugging` / `bIsPaused` / `BreakpointCount` 收口”。在当前实现优先复用 `TryGetRunningProductionDebuggerEngine()` 共享 production engine 的前提下，scope exit 后立刻销毁模块和 session 指针，完全可能把尚未处理的 debug state 留给后续用例。 |
| 影响 | 这会把清理成功与否变成时序偶然事件，而不是确定行为。表现上既可能让后续用例继承上一个用例残留的 debugging/pause/breakpoint 状态，也可能在模块已经 `DiscardModule()` 后才让 server 读到迟到的 `StopDebugging` / `Disconnect`，形成顺序相关失败和诊断噪音。 |
| 修复建议 | 把 debugger 场景 teardown 收敛成 Shared helper，例如 `WaitForDebugServerIdle(Session, Client)`：1. 发送 `StopDebugging` 后必须 `Session.PumpUntil([&Session]() { return !Session.GetDebugServer().bIsDebugging && !Session.GetDebugServer().bIsPaused && Session.GetDebugServer().BreakpointCount == 0; }, Session.GetDefaultTimeoutSeconds())`；2. 再发送 `Disconnect` 并继续 pump 至少一轮，确保 server 消费断连；3. 只有在 cleanup 成功后才 `DiscardModule()` / `CollectGarbage()`。这样 6 个用例都复用同一套可断言的 teardown，而不是 fire-and-forget。 |

### 二、需要新增的测试

本轮无新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-43 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |
---

## 测试审查 (2026-04-09 13:13)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-39：`asBC_POWd / asBC_POWdi` 必须在 JIT 下成功生成并保持与 VM 一致，不能在 `check(bImplemented)` 前崩掉

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` |
| 关联函数 | `IMPL_BYTECODE_BEGIN(asBC_POWd)`、`IMPL_BYTECODE_BEGIN(asBC_POWdi)`、`FAngelscriptStaticJIT::GenerateCppCode()` |
| 现有测试覆盖 | 完全无直接自动化。当前仓库里唯一直接命中 `StaticJIT/` 的测试仍是 `AngelscriptPrecompiledDataTests.cpp` 那 2 个 structure regression；测试侧对 `Pow(` / `asBC_POWd` / `asBC_POWdi` 是 0 次符号命中。更关键的是，`AngelscriptBytecodes.cpp:6470-6498` 明明已经写出了 `Math::Pow(...)` 代码，却仍然 `return false`，而 `AngelscriptStaticJIT.cpp:3330-3336` 会立刻 `check(bImplemented)`，这条高风险路径目前没有任何护栏。 |
| 风险评估 | 只要脚本函数里出现 `double Pow(double,double)` 或 `double Pow(double,int)`，JIT codegen 就可能在生成阶段直接命中 `check`，或者静默退回错误路径。现有 debugger 套件和 `PrecompiledData` 结构测试都不会发现这类 bytecode 级回归。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.Bytecodes.DoublePowParity` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITPowTests.cpp`，文件集中放 `POWd / POWdi` 方向 2-3 个 `ASTEST_*` 用例，控制在 300-500 行 |
| 场景描述 | 复用 `NewTest-3` 已要求建立的非 `EditorContext` `FStaticJITExecutionHarness`。在内存脚本里放两个最小入口：`int PowDoubleMatches() { return Pow(2.0, 3.0) == 8.0 ? 1 : 0; }` 和 `int PowDoubleIntMatches() { return Pow(3.0, 2) == 9.0 ? 1 : 0; }`。先跑 VM 基线，再生成并加载 StaticJIT 产物执行同一函数。若 harness 支持收集生成源码，再补一个文本断言，确保输出里确实出现 `Math::Pow(`，而不是退化成 generic fallback。 |
| 输入/前置 | 需要一个真正执行 transpiled code 的 harness，不能跑在 `WITH_EDITOR` + `AS_SKIP_JITTED_CODE` 环境。复用 `FStaticJITExecutionHarness` 或等价 helper，并补齐 `HasJITEntry(FunctionName)` / `ExecuteIntFunctionJIT(FunctionName)` / 可选 `GetGeneratedSourceForFunction(FunctionName)` 接口，确保测试能区分“拿到了 JIT entry”和“只是 VM fallback 假绿”。 |
| 期望行为 | 1. 生成 `PowDoubleMatches` / `PowDoubleIntMatches` 的 JIT 产物时不触发 `check(bImplemented)`。 2. 两个函数在 `FJITDatabase` 里都有有效 entry。 3. VM 与 JIT 执行结果都返回 `1`，证明 `POWd` 和 `POWdi` 语义一致。 4. 若可观测生成源码，源码中应包含 `Math::Pow(`，锁住 `AngelscriptBytecodes.cpp:6479,6494` 的实现文本，不允许“写了代码却仍然被标记未实现”这种回归再次溜过。 |
| 使用的 Helper | `ASTEST_*` 宏、`FStaticJITExecutionHarness`、`CompileModuleFromMemory`、`FJITDatabase::Get()`、可选 `GetGeneratedSourceForFunction(...)` |
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
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |
---

## 测试审查 (2026-04-09 23:36)

### 一、现有测试问题

本轮新增 `Issue-44`、`Issue-45`，详见本文对应条目。

### 二、需要新增的测试

本轮新增 `NewTest-40`，详见本文对应条目。

尾部索引说明：本轮新增 `Issue-44`、`Issue-45`、`NewTest-40` 已写入本文；由于该文档历史上已存在段落顺序错位，继续审查时请按编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-44 |
| BadIsolation | 1 | Issue-45 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |
 
---

## 测试审查 (2026-04-09 23:42)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮新增 `NewTest-41`，正文已记录于本文 `## 测试审查 (2026-04-09 23:40)` 段落；该条目覆盖“`StepIn` 在非调用语句上逐行前进”的缺口。

尾部索引说明：由于该文档历史上已存在段落顺序错位，本轮在尾部补记索引，继续审查时请按 `NewTest-41` 编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-09 23:55)

### 一、现有测试问题

本轮新增 `Issue-46`，正文已记录于本文 `## 测试审查 (2026-04-09 23:51)` 段落；该条目覆盖“启动 helper 失败早退、清理尚未安装就泄漏共享 debug state”的问题。

### 二、需要新增的测试

本轮新增 `NewTest-42`，正文已记录于本文 `## 测试审查 (2026-04-09 23:52)` 段落；该条目覆盖“第二个 debugger client 后接入时不得清空首个 client 已注册断点”的多客户端缺口。

尾部索引说明：由于该文档历史上已存在段落顺序错位，本轮以文件末尾补记索引。继续审查时请按 `Issue-46`、`NewTest-42` 编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-46 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | MissingScenario: 1 |
| P2 | 0 | - |
| P3 | 0 | - |
---

## 测试审查 (2026-04-10 00:02)

### 一、现有测试问题

#### Issue-47：`Smoke.Handshake` 的 teardown 被 `Client.IsConnected()` 门控，连接掉线后会直接跳过 `StopDebugging`/`Disconnect`

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake` |
| 行号范围 | `AngelscriptDebuggerSmokeTests.cpp:31-47`；关联基础设施：`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp:29-63, 65-81` |
| 问题描述 | 冒烟用例的 `ON_SCOPE_EXIT` 只有在 `Client.IsConnected()` 为真时才发送 `StopDebugging`、`Disconnect` 并关闭本地 socket。问题在于当前 Shared client 的 `Connect()` 是宽松成功语义：non-blocking `Connect()` 遇到 `SE_EWOULDBLOCK` / `SE_EINPROGRESS` 也会直接返回 `true`。这样一来，只要握手中途连接掉线、socket 状态提前变成 `SCS_ConnectionError`，或者一开始就是“半连接但未真正建立”，scope exit 就会整段跳过，既不尝试把 server 拉回非 debugging 状态，也不显式执行本地 `Disconnect()`。 |
| 影响 | 这个门控把冒烟测试的失败路径变成“只析构本地 client，不清理共享 debug session”。一旦 `StartDebugging` 已经被 server 处理，而随后连接状态在 teardown 前掉成非 `SCS_Connected`，`bIsDebugging` 和相关会话状态就可能残留到后续 debugger 用例，继续放大已有的共享 production engine 串扰风险。 |
| 修复建议 | 不要把 cleanup 建立在 `IsConnected()` 上。更稳妥的做法是：1. 只要 `Client` 持有 socket，就总是执行 `Client.Disconnect()`；2. `StopDebugging` / `SendDisconnect` 应基于“是否曾成功进入 handshake/debugging state”决定，而不是基于瞬时连接状态；3. 抽一个 `TeardownDebuggerClient(Session, Client, bMayHaveStartedDebugging)` Shared helper，统一负责 `SendStopDebugging()`、`PumpUntil(!bIsDebugging && !bIsPaused)`、`SendDisconnect()`、`Disconnect()`，避免 `Smoke.Handshake` 单独走这条更脆弱的分支。 |

### 二、需要新增的测试

本轮先记录新增现有测试问题，新的场景缺口将在后续段落继续追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| MissingCleanup | 1 | Issue-47 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:04)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

#### NewTest-43：浮点/双精度 truthiness 分支必须先 materialize `l_valueRegister`，避免 `JZ/TZ` 路径静默误编译

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` |
| 关联函数 | `FStaticJITContext::MaterializeValueRegister()`、`GetValueRegisterForUnsignedComparisonMaybeMaterialize()`、`IMPL_BYTECODE_BEGIN(asBC_JZ)`、`IMPL_BYTECODE_BEGIN(asBC_JNZ)`、`IMPL_BYTECODE_BEGIN(asBC_TZ)`、`IMPL_BYTECODE_BEGIN(asBC_TNZ)` |
| 现有测试覆盖 | 完全无直接测试。当前缺口文档虽然已经覆盖 `GenerateCppCode()` 总体通路、`PrimitiveConversions`、`Pow`、`FName_Equals` 和 generated output metadata，但没有任何一个用例锁住 `AngelscriptStaticJIT.cpp:2533-2610` 这条“float/double register 在进入 truthiness/zero-check 分支前必须 materialize 成 `l_valueRegister`”的生成逻辑；`AngelscriptBytecodes.cpp:2793-2822, 2938-2961` 对 `JZ/JNZ/TZ/TNZ` 的实现因此仍是 0 场景覆盖。 |
| 风险评估 | 这条路径一旦回归，JIT 会在最基础的 `if (FloatValue)` / `if (!DoubleValue)` 分支上静默生成错误代码，表现为 0/非 0 判断反转、`-0.0`/bit-pattern 处理异常，或直接丢失 `value_extend_safe` / `value_as_safe` 这层转换。现有 `PrecompiledData` 结构测试和 debugger 套件都不会发现这类 codegen 级行为错误。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.Bytecodes.FloatDoubleTruthinessMaterialization` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITTruthinessTests.cpp`，文件集中放 truthiness/zero-branch 方向 2-3 个 `ASTEST_*` 用例，控制在 300-500 行 |
| 场景描述 | 复用 `NewTest-3` 已要求建立的非 `EditorContext` `FStaticJITExecutionHarness`，编译一个最小脚本模块，至少包含：`int FloatTruthy() { float Value = 1.5f; return Value ? 1 : 0; }`、`int FloatFalsy() { float Value = 0.0f; return Value ? 1 : 0; }`、`int DoubleFalsy() { double Value = 0.0; return Value ? 1 : 0; }`。先跑 VM 基线，再生成并加载 StaticJIT 产物执行同一入口；若 harness 支持收集生成源码，再分别检查 `FloatTruthy/FloatFalsy` 的输出里是否出现 `value_extend_safe<asQWORD>(l_floatRegister)`，`DoubleFalsy` 的输出里是否出现 `value_as_safe<asQWORD>(l_doubleRegister)`，并确认这些转换出现在 `if(... == 0)` / `if(... != 0)` 分支判断之前。 |
| 输入/前置 | 需要真实执行 transpiled code 的 harness，不能跑在 `WITH_EDITOR` + `AS_SKIP_JITTED_CODE` 环境。复用 `FStaticJITExecutionHarness` 或等价 helper，并补齐 `HasJITEntry(FunctionName)`、`ExecuteIntFunctionJIT(FunctionName)`、可选 `GetGeneratedSourceForFunction(FunctionName)` 接口，确保测试能同时断言“拿到了 JIT entry”和“生成文本确实走了 materialize 分支”。 |
| 期望行为 | 1. `FloatTruthy` 在 VM 与 JIT 下都返回 `1`。 2. `FloatFalsy` 与 `DoubleFalsy` 在 VM 与 JIT 下都返回 `0`。 3. 三个函数都具备有效 JIT entry，而不是退回解释执行。 4. 若可观测生成源码，float case 必须包含 `value_extend_safe<asQWORD>(l_floatRegister)`，double case 必须包含 `value_as_safe<asQWORD>(l_doubleRegister)`，且这些语句出现在 `JZ/JNZ/TZ/TNZ` 对应的 zero-check 之前。 |
| 使用的 Helper | `ASTEST_*` 宏、`FStaticJITExecutionHarness`、`CompileModuleFromMemory`、`FJITDatabase::Get()`、可选 `GetGeneratedSourceForFunction(...)` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:20)

### 一、现有测试问题

本轮新增 `Issue-48`，正文已记录于本文 `## 测试审查 (2026-04-10 00:17)` 段落；该条目覆盖“6 个 breakpoint/stepping happy-path 全部依赖第二个 monitor client，单连接 IDE 主路径未被现有测试锁住”的问题。

### 二、需要新增的测试

本轮新增 `NewTest-44`、`NewTest-45`，正文已记录于本文 `## 测试审查 (2026-04-10 00:18)` 段落；这两个条目分别补“同一 client 的 breakpoint 往返闭环”和“同一 client 的 `StepIn` 步进闭环”。

尾部索引说明：该文档历史上已存在段落顺序错位；本轮新增正文虽然已写入本文，但不完全位于文件尾部。后续继续审查时请按 `Issue-48`、`NewTest-44`、`NewTest-45` 编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WrongHelper | 1 | Issue-48 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 2 | MissingScenario: 2 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:34)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮无新增测试建议。

尾部索引说明：本轮新增 `NewTest-46`、`NewTest-47`、`NewTest-48`，正文已记录于本文 `## 测试审查 (2026-04-10 00:29)` 段落；继续审查时请按这些编号检索，不要只从文件尾部向上判断最新发现。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |
---

## 测试审查 (2026-04-10 00:44)

### 一、现有测试问题

#### Issue-49：`FAngelscriptDebuggerTestClient::Connect` 忽略 `SetNonBlocking` / `SetNoDelay` 返回值，socket 选项失败会被静默带入后续握手

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Smoke.Handshake`、`Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerTestClient.cpp:48-55` |
| 问题描述 | `Connect()` 创建 socket 后直接调用 `Socket->SetNonBlocking(true)` 与 `Socket->SetNoDelay(true)`，但两个返回值都被忽略。当前 Shared client 的后续实现却默认依赖这两个选项已经生效：连接阶段把 `SE_EWOULDBLOCK` / `SE_EINPROGRESS` 视为非阻塞连接中的中间态，发送/接收阶段也按 non-blocking socket 的语义做轮询与超时收口。若某个平台、CI 环境或底层 socket 后端拒绝设置这些选项，helper 仍会继续返回成功，把“socket 运行模式不符合预期”的根因静默包装成更晚的握手超时、发送失败或随机卡顿。 |
| 影响 | 这会让 Shared debugger client 的行为继续依赖平台默认值而不是测试代码自己的契约。最坏情况下，测试会在阻塞 socket 上表现成长时间挂起；即使没有完全阻塞，`no-delay` 未生效也会放大小包往返延迟，并与文档里已记录的 connect-timeout / partial-send 问题叠加成更难定位的时序抖动。 |
| 修复建议 | 在 `Connect()` 里把 socket 选项初始化收敛成显式成功条件：1. 对 `SetNonBlocking(true)`、`SetNoDelay(true)` 分别检查返回值；2. 任一步失败都读取 `ISocketSubsystem::GetLastErrorCode()`，拼出包含 host/port/option 名称的错误信息；3. 失败时立即 `Disconnect()` 并返回 `false`，不要继续进入握手；4. 若后续要引入 fake socket / 注入式测试 seam，也应把这两个 option 调用纳入可断言的 transport 初始化 helper，而不是散落在 `Connect()` 主体中静默忽略。 |

### 二、需要新增的测试

#### NewTest-49：`Delegate.Execute` 的 custom-call 必须在 StaticJIT 下走 `ProcessDelegate`，并与 VM 回调结果一致

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` |
| 关联函数 | `FScriptFunctionNativeForm::BindDelegateExecute()`、`GenerateCustomCall_DelegateExecute(...)` |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/Delegate/AngelscriptDelegateScenarioTests.cpp` 只验证常规运行时里 `OnHealthChanged.Execute(...)` 能调到绑定的 native callback；测试侧对 `StaticJITBinds.cpp/.h` 仍是 `0` 次文件名命中，也没有任何自动化断言 JIT 生成的 `Delegate.Execute` 会走 `ProcessDelegate<UObject>` custom-call。 |
| 风险评估 | `Delegate.Execute` 是脚本里高频使用的单播委托入口。如果 StaticJIT 在这个 native form 上退化成 generic call、参数清理遗漏、或根本拿不到 custom-call，真实 JIT 执行会出现“VM 正常、JIT 不回调/回调参数错位/生成源码不可编译”的高频回归，而当前仓库不会报警。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.Delegate.ExecuteCustomCall` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITDelegateExecuteTests.cpp`，同文件放 `Delegate.Execute` 与 `event.Broadcast` 两个 custom-call 集成用例，整文件控制在 300-500 行 |
| 场景描述 | 复用 `Delegate/AngelscriptDelegateScenarioTests.cpp` 的 unicast 场景脚本：声明 `delegate void FOnHealthChanged(int32 NewHealth, const FString& Label);`，脚本类里持有 `FOnHealthChanged OnHealthChanged`，`TriggerHealthChanged()` 内部执行 `OnHealthChanged.Execute(NewHealth, Label)`。测试先走 VM baseline：spawn actor、从 C++ 侧把 native receiver 的 `RecordIntString` UFunction 绑到 delegate、调用 `TriggerHealthChanged(77, "Unicast")`，记录回调次数和值。随后在同一脚本上走真正的 StaticJIT execution harness，再执行同一个 trigger。若 harness 支持导出生成源码，再额外检查目标函数生成文本。 |
| 输入/前置 | 复用 `FActorTestSpawner`、`InitializeDelegateScenarioSpawner(...)`、`SpawnScriptActor(...)`、`FindFProperty<FDelegateProperty>(...)`、`UAngelscriptNativeScriptTestObject`；新增 `FStaticJITExecutionHarness` 或等价 helper，至少提供 `CompileForJIT(...)`、`ExecuteUFunctionJIT(UObject*, FName, void* Params)`、可选 `GetGeneratedSourceForFunction(...)`。 |
| 期望行为 | 1. VM baseline 与 JIT 路径都能把 native receiver 的计数从 `0` 提升到 `77`，且 label 保持 `"Unicast"`。 2. JIT 路径不能比 VM 多触发或少触发回调，委托参数顺序必须一致。 3. 若可读取生成源码，`TriggerHealthChanged` 的输出里必须出现 `ProcessDelegate<UObject>(&l_ParmStruct[0])`，且不能退化成普通 generic fallback。 4. JIT 执行结束后不应留下异常或脏 delegate state。 |
| 使用的 Helper | `ASTEST_*` 宏、`FActorTestSpawner`、`SpawnScriptActor(...)`、`FDelegateProperty`、`UAngelscriptNativeScriptTestObject`、新增 `FStaticJITExecutionHarness` |
| 优先级 | P1 |

#### NewTest-50：`event.Broadcast` 的 custom-call 必须在 StaticJIT 下走 `ProcessMulticastDelegate`，并保持脚本副作用一致

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp` |
| 关联函数 | `FScriptFunctionNativeForm::BindMulticastExecute()`、`GenerateCustomCall_MulticastExecute(...)` |
| 现有测试覆盖 | 现有 `Delegate/AngelscriptDelegateScenarioTests.cpp` 只覆盖解释器/常规运行时里 `OnDamaged.Broadcast(...)` 能触发脚本 handler；测试侧没有任何一条用例把该路径推进到 StaticJIT，也没有对 `ProcessMulticastDelegate<UObject>` 生成文本做断言。 |
| 风险评估 | multicast/event 广播是脚本事件分发的核心路径。如果 JIT custom-call 丢掉 `CleanupPropertiesInParm(...)`、参数结构布局错位，或退化成错误调用，真实游戏逻辑会表现成“广播不触发”“只触发部分 handler”或“带 `FString` 参数时崩溃/脏内存”，但当前测试套件看不到。 |
| 建议测试名 | `Angelscript.TestModule.StaticJIT.Event.BroadcastCustomCall` |
| 测试类型 | Integration |
| 测试文件 | 追加到新建 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptStaticJITDelegateExecuteTests.cpp`，与 `NewTest-49` 共用 actor/spawner/JIT harness helper，保持单文件 300-500 行 |
| 场景描述 | 复用 `Delegate/AngelscriptDelegateScenarioTests.cpp` 的 multicast 场景脚本：声明 `event void FOnDamaged(int32 NewHealth, const FString& Label);`，脚本类中 `BeginPlay()` 先 `AddUFunction(this, n"HandleDamaged")`，`TriggerBroadcast()` 内部执行 `OnDamaged.Broadcast(NewHealth, Label)`，`HandleDamaged()` 把 `EventTriggerCount += 1`。测试先跑 VM baseline，确认 `TriggerBroadcast(33, "Multicast")` 让 `EventTriggerCount` 从 `0` 变 `1`；随后走 JIT harness 对同一 trigger 做一次真实 JIT 执行，并比较副作用。若 harness 支持导出源码，再校验生成文本。 |
| 输入/前置 | 复用 `FActorTestSpawner`、`InitializeDelegateScenarioSpawner(...)`、`SpawnScriptActor(...)`、`ReadPropertyValue<FIntProperty>(...)`；共用 `FStaticJITExecutionHarness`。为避免旧状态串扰，JIT 和 VM 两轮都要各自创建 fresh actor 或在执行前重置 `EventTriggerCount`。 |
| 期望行为 | 1. VM baseline 与 JIT 路径都会把 `EventTriggerCount` 精确从 `0` 增加到 `1`，不会漏触发或重复触发。 2. 带 `FString` 参数的广播在 JIT 下保持正确，不出现异常、空字符串或内存污染。 3. 若可读取生成源码，`TriggerBroadcast` 的输出里必须出现 `ProcessMulticastDelegate<UObject>(&l_ParmStruct[0])`。 4. JIT 路径执行后再次读取脚本属性，`EventTriggerCount` 与 VM 结果一致。 |
| 使用的 Helper | `ASTEST_*` 宏、`FActorTestSpawner`、`SpawnScriptActor(...)`、`ReadPropertyValue<FIntProperty>(...)`、新增 `FStaticJITExecutionHarness` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-49 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 2 | NoTestForSource: 2 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 00:56)

### 一、现有测试问题

#### Issue-50：两个 monitor helper 从不消费 `ReceiveEnvelope()` 的 `LastError`，真实断线/反序列化错误会被伪装成 timeout 或“0 个 stop”

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 测试名 | `Angelscript.TestModule.Debugger.Breakpoint.*`、`Angelscript.TestModule.Debugger.Stepping.*` |
| 行号范围 | `AngelscriptDebuggerBreakpointTests.cpp:189-206, 224-250`；`AngelscriptDebuggerSteppingTests.cpp:194-211, 228-265` |
| 问题描述 | `StartBreakpointMonitor()` 与 `StartStepMonitor()` 在握手循环、stop 监听循环、callstack 等待循环里都直接调用 `MonitorClient.ReceiveEnvelope()`；一旦返回空值，只会 `Sleep` 后继续下一轮。问题在于 `FAngelscriptDebuggerTestClient::ReceiveEnvelope()` 遇到远端断线、协议解析失败或 socket read error 时会把原因写进 `LastError`，但这两个 monitor helper 从头到尾都不检查 `MonitorClient.GetLastError()`。结果是 monitor socket 中途掉线、收到坏包、或 callstack 请求阶段出错时，helper 不会立刻失败，而是继续空转到 timeout，最后只留下泛化的 `bTimedOut` / `StopEnvelopes.Num() == 0` / `"timed out waiting for DebugServerVersion"` 文案。 |
| 影响 | 这会把最关键的 transport 级失败信号降格成模糊超时，直接放大现有 debugger 套件的诊断成本。更糟的是，负向断点用例本来就存在“0 个 stop 即通过”的弱断言；一旦 monitor 因真实 socket 错误提前失效，当前 helper 会把根因吞掉，进一步增加假绿概率。 |
| 修复建议 | 把 monitor 的所有接收循环统一收敛成 `ReceiveEnvelopeOrFail(...)` helper：1. 每次 `ReceiveEnvelope()` 返回空值后立刻检查 `MonitorClient.GetLastError()`；2. 若非空，立刻写入 `Result.Error = FString::Printf(TEXT(\"%s failed: %s\"), Context, *MonitorClient.GetLastError())` 并退出循环；3. callstack 等待阶段也要把“超时没收到 `CallStack`”和“socket/read 失败”区分开，不能继续复用同一个 timeout 文案；4. 上层用例在断言 stop 数量前先统一断言 `Result.Error.IsEmpty()`，避免 transport error 再次退化成“没有 stop”。 |

### 二、需要新增的测试

本轮先记录新增现有测试问题，新的测试建议在继续完成 Shared/StaticJIT 审查后再追加。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-50 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 01:14)

### 一、现有测试问题

本轮无新增现有测试问题。

### 二、需要新增的测试

本轮新增 `NewTest-52`，正文已记录于本文 `## 测试审查 (2026-04-10 01:11)` 段落；由于该文档历史上已存在段落顺序错位，本轮在文件尾部补记索引，后续继续审查时请按 `NewTest-52` 编号检索，不要只从文件尾部向上判断最新新增测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| 无新增 | 0 | - |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 0 | - |
| P3 | 0 | - |

---

## 测试审查 (2026-04-10 01:26)

### 一、现有测试问题

本轮新增 `Issue-51`，正文已记录于本文 `## 测试审查 (2026-04-10 01:22)` 段落；由于该文档历史上已存在段落顺序错位，本轮在文件尾部补记索引，后续继续审查时请按 `Issue-51` 编号检索，不要只从文件尾部向上判断最新现有问题。

### 二、需要新增的测试

本轮新增 `NewTest-53`，正文已记录于本文 `## 测试审查 (2026-04-10 01:22)` 段落；由于该文档历史上已存在段落顺序错位，本轮在文件尾部补记索引，后续继续审查时请按 `NewTest-53` 编号检索，不要只从文件尾部向上判断最新测试建议。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| FlakyRisk | 1 | Issue-51 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 0 | - |
| P1 | 0 | - |
| P2 | 1 | NoTestForSource: 1 |
| P3 | 0 | - |
