# DebuggingAndJIT 迭代分析

---

## 分析 (2026-04-08 02:28)

### 发现 1：`DebugAdapterVersion` 使用进程级全局状态，多个调试客户端会互相污染变量消息格式

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.h:20-23, 416-438`；`AngelscriptDebugServer.cpp:897-913` |
| 描述 | `StartDebugging` 收到客户端握手后，把 `Msg.DebugAdapterVersion` 写入全局 `AngelscriptDebugServer::DebugAdapterVersion`。随后 `FAngelscriptVariable::operator<<` 依据这个全局值决定是否序列化 `ValueAddress` 和 `ValueSize`。这意味着只要第二个客户端以不同版本接入，所有后续 `Variables` 消息的二进制布局都会被最后一次握手覆盖，而不是按目标客户端分别编码。 |
| 根因 | 协议版本被建模成进程级共享状态，而不是 `FSocket*` 级别的连接状态；消息序列化层又直接读取这个共享变量决定字段集合。 |
| 影响 | 多客户端并存时会出现协议自相矛盾：旧客户端可能收到额外字段，新客户端也可能丢失 `ValueAddress`/`ValueSize`。结果是 watch/variables 解析错位，严重时直接让调试会话失去可用性，也不满足 DAP 场景里“每个 debug session 独立协商能力”的基本要求。 |

### 发现 2：收包循环对 `Recv()` 失败和零字节读取没有退出条件，半包或断线会卡死调试线程

| 项目 | 内容 |
|------|------|
| 维度 | A / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:756-789` |
| 描述 | `ProcessMessages()` 在读取包头和包体时使用两个 `while (BytesReceived < ...)` 循环反复调用 `Client->Recv(...)`，但既不检查返回值，也不处理 `BytesRead == 0`。如果对端在发出部分数据后断开，或者底层 socket 暂时不给数据，这两个循环都会一直自旋，永远不离开当前消息处理。 |
| 根因 | 调试协议实现假设 `Recv()` 每次都会前进到完整消息，没有为 TCP 半包、对端断开和非阻塞读取失败建立失败路径。 |
| 影响 | 调试器只要发送截断包或在消息中途断开，就可能把 `ProcessMessages()` 卡死在游戏线程上；随后 `PauseExecution()`、line callback 和正常运行都会被拖住，属于可由坏客户端触发的拒绝服务路径。 |

### 发现 3：任意一个客户端发送 `StopDebugging` 都会全局关闭调试并清空其他客户端断点

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:724-747, 897-923` |
| 描述 | `StartDebugging` 把每个连接加入 `ClientsThatAreDebugging`，`ProcessMessages()` 处理断线时也只有在该数组清空后才会把 `bIsDebugging` 设为 `false`。但显式 `StopDebugging` 分支不做同样判断，直接执行 `bIsDebugging = false`、`bIsPaused = false` 和 `ClearAllBreakpoints()`，然后才移除当前 `Client`。结果是一个客户端停止调试时，会把其他仍连接的调试客户端一起踢出活动状态，并清空它们的断点。 |
| 根因 | 会话生命周期同时维护了“全局状态”和“客户端集合”两套语义，但 `StopDebugging` 分支没有遵守集合驱动的停机条件。 |
| 影响 | 多客户端场景下，任何一个调试器都可以非预期地终止其他会话；即使客户端只是正常关闭自身窗口，也会造成剩余调试器丢失断点、停止接收 line callback 和继续/暂停控制，协议完整性被破坏。 |

### 发现 4：`RequestCallStack` 在运行态被延迟到“下一次任意暂停”才响应，请求与停止事件无法一一对应

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:812-817, 924-927` |
| 描述 | 收到 `RequestCallStack` 时，代码只是把 `Client` 追加到 `CallstackRequests`。真正发送 `CallStack` 的逻辑只在 `bIsPaused` 时统一 flush 整个数组。这样如果客户端在运行态发起请求，当前请求不会立即得到“不可用”或空响应，而是被保留到下一个任意 breakpoint/exception/pause 才发送。 |
| 根因 | 协议层把 `stackTrace` 类请求实现成“延迟广播队列”，没有把请求和当前 stop state 绑定，也没有 request id 或错误响应路径。 |
| 影响 | 调试前端会把某一次用户操作错误地关联到之后的另一次停止事件，出现“点了当前帧调用栈却收到下一次停点的栈”的现象；这不符合 DAP 中 `stackTrace` 依附当前暂停线程状态的基本语义，也会让步进调试结果变得不可信。 |

### 发现 5：`StaticJIT` 对“部分函数未命中 transpiled code”的回退完全静默，现有状态位和测试都无法发现覆盖退化

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `PrecompiledData.cpp:527-540`；`AngelscriptEngine.cpp:77, 1591-1595`；`AngelscriptPrecompiledDataTests.cpp:13-21` |
| 描述 | `FAngelscriptPrecompiledFunction::Create()` 只要在 `FJITDatabase::Get().Functions` 里找不到当前 `Id`，就直接走 `AllocateScriptFunctionData()` 恢复 bytecode 解释执行，没有日志、计数器或断言说明该函数未命中 transpiled code。与此同时，运行时唯一公开的状态位 `bStaticJITTranspiledCodeLoaded` 只检查 `Functions.Num() > 0`，只要数据库里存在任意一个函数就会报告“已加载 transpiled code”。当前 `AngelscriptPrecompiledDataTests.cpp` 也只有两个 round-trip / diff 用例，没有任何测试验证“目标函数实际拿到了 JIT entry”。 |
| 根因 | JIT 可用性在实现上是“函数级查表”，但对外暴露和测试的口径却退化成“进程里是否存在任意 transpiled function”。 |
| 影响 | 一旦生成链路或打包流程漏掉部分函数，系统会悄悄退回解释执行，既不会告警，也不会让自动化失败。结果是 JIT 覆盖范围可以在没有任何可见信号的情况下持续收缩，性能和行为差异只能在运行时偶发暴露。 |

---

## 分析 (2026-04-08 02:37)

### 发现 6：`StepOut` 在最外层脚本帧上退化成 `StepIn`，无法真正运行到返回点

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:877-895, 548-575` |
| 描述 | `HandleMessage()` 处理 `StepOut` 时，无论当前是否存在调用者帧，都会先把 `bBreakNextScriptLine = true`、`bIsPaused = false`。当 `Context->GetCallstackSize() < 2` 时，它只把 `ConditionBreakFrame` 和 `ConditionBreakFunction` 清空。随后下一次 `ProcessScriptLine()` 进入 `bBreakNextScriptLine` 分支时，因为 `ConditionBreakFrame == -1`，条件检查被完全跳过，`bShouldBreak` 保持为 `true`，服务器会在“下一条脚本行”立刻再次暂停。 |
| 根因 | `StepOut` 复用了“下一行即停”的通用步进开关，但没有为“当前已在最外层帧，没有 caller 可退”建立单独语义。空条件被当成“立即允许停下”，而不是“继续执行直到脚本离开当前上下文”。 |
| 影响 | 用户在顶层脚本或入口函数里执行 `step out` 时，不会回到调用方或结束当前脚本，而是像 `step in` 一样逐行停下。该行为与 DAP/常见调试器对 `stepOut` 的预期不一致，会直接误导前端的步进控制。 |

### 发现 7：JIT 异常路径没有通知 `DebugServer`，调试器收不到 `exception` 停止事件

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `StaticJITHeader.cpp:104-161`；`AngelscriptEngine.cpp:5300-5317`；`AngelscriptDebugServer.cpp:440-462` |
| 描述 | 所有 JIT 运行时异常辅助函数最终都调用 `FAngelscriptEngine::HandleExceptionFromJIT()`。但这个函数只执行 `LogAngelscriptException(ExceptionString)`，没有像 `LogAngelscriptException(asIScriptContext*)` 那样在 game thread 上调用 `DebugServer->ProcessException(Context)`。`DebugServer` 里唯一会发送 `HasStopped(reason=\"exception\")` 的入口正是 `ProcessException()`。结果是解释执行异常会触发调试停点，JIT 异常则只写日志，不会向调试客户端发任何停止消息。 |
| 根因 | JIT 和解释器维护了两条异常上报链路，但只有解释器链路接入了调试服务器；JIT 链路没有桥接到调试协议层。 |
| 影响 | 一旦脚本函数被 JIT 覆盖，诸如空指针、除零、越界、unbound function 等运行时错误将不会在调试器中停下，前端既看不到 `StoppedEvent(reason=\"exception\")`，也无法自动同步调用栈，导致“同一脚本在解释模式能断住、JIT 模式却直接漏停”的行为分叉。 |

### 发现 8：无效的 debugger frame 会被静默重定向到 frame 0，`Evaluate`/`Variables` 结果可能落到错误栈帧

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:2282-2345, 2369-2375, 1081-1128` |
| 描述 | `ResolveDebuggerFrame()` 构造当前可见帧列表后，如果 `DebuggerFrame` 越界，直接返回 `0`。`GetDebuggerValue()` 会把这个返回值写回 `*InOutFrame`，而 `RequestEvaluate` / `RequestVariables` 之后继续按这个 frame 查询变量。结果是当前端持有过期 frame id、或者在步进后栈深变化时，请求不会得到错误响应，也不会返回空，而是静默读取最顶层 frame 的值。 |
| 根因 | 帧解析函数把“找不到目标 frame”编码成合法 frame index `0`，协议层又没有 request id / error channel 来表达 `invalid frameId`。 |
| 影响 | 调试前端会把错误帧上的局部变量误当成目标帧数据，用户看到的 watch / hover / variables 面板可能与实际选中帧不一致。由于返回包仍是结构合法的 `Evaluate`/`Variables` 消息，这种错配很难被前端识别，只会表现为“偶发看错值”。 |

### 发现 9：`SendCallStack` 完全忽略 JIT 维护的 `debugCallStack`，JIT 覆盖后的帧不会进入调试协议

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:1382-1482`；`StaticJITHeader.h:193-218, 337-340`；`AngelscriptStaticJIT.cpp:337-344, 373-380`；`AngelscriptEngine.cpp:5087-5126` |
| 描述 | JIT 生成代码在函数入口创建 `FScopeJITDebugCallstack`，并通过 `SCRIPT_DEBUG_CALLSTACK_LINE` 持续更新行号；引擎的 `GetStackTrace()` 也会优先遍历 `tld->activeExecution->debugCallStack` 以输出这些 JIT 帧。相比之下，`FAngelscriptDebugServer::SendCallStack()` 只读取 `asGetActiveContext()`、`Context->GetFunction()` 和 Blueprint 栈，从头到尾没有访问 `activeExecution` 或 `debugCallStack`。因此只要当前执行落在 JIT 覆盖函数里，调试协议返回的 `CallStack` 就不可能包含这部分帧。 |
| 根因 | JIT 调试元数据被接入了日志/诊断栈格式化，但没有同步接入 DebugServer 的 `stackTrace` 序列化路径。 |
| 影响 | 在启用 StaticJIT 的函数上，前端看到的调用栈会缺失正在运行的 JIT 帧，表现为停在异常或断点时栈顶部与真实执行位置不一致，进而破坏 frame 选择、变量查看和 step 语义。 |

### 发现 10：`SetDataBreakpoints` 会静默丢弃第 5 个及之后的断点，协议层没有任何确认或拒绝反馈

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:713-716`；`AngelscriptDebugServer.cpp:1061-1068, 1248-1286`；`AngelscriptDebugProtocolTests.cpp:174-214` |
| 描述 | `SetDataBreakpoints` 直接把客户端发来的整个列表赋给 `DataBreakpoints`，随后 `RebuildActiveDataBreakpoints()` 只复制 `min(DataBreakpoints.Num(), DATA_BREAKPOINT_HARDWARE_LIMIT)`，而 `DATA_BREAKPOINT_HARDWARE_LIMIT` 被硬编码为 `4`。多出来的断点既不会返回错误，也不会通过任何 ack/verified 状态告诉客户端它们已经失效。现有测试只验证两个 data breakpoint 的 round-trip 序列化，没有覆盖容量上限或服务器确认语义。 |
| 根因 | 运行时实现受硬件 debug register 数量限制，但协议面向客户端暴露的是“可变长列表”，且没有能力协商或逐项反馈机制。 |
| 影响 | 调试前端会误以为自己成功设置了全部 data breakpoint，实际只有前四个可能触发。超过上限的监视点会静默失效，导致调试结论依赖断点顺序，明显偏离 DAP `setDataBreakpoints` 应逐项返回验证结果的预期。 |

---

## 分析 (2026-04-08 02:46)

### 发现 11：发送队列把 partial send 当作完整成功，调试消息在网络背压下会被直接截断

| 项目 | 内容 |
|------|------|
| 维度 | A / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:2845-2859, 52-61`；`AngelscriptDebugServer.h:703-709`；`AngelscriptDebugTransportTests.cpp:43-231` |
| 描述 | `TrySendingMessages()` 对队首消息调用一次 `Client->Send()` 后，只要返回值是 `true` 就立刻 `Queue.RemoveAt(0)`。实现里既不检查 `BytesSent == Msg.Buffer.Num()`，也不保存剩余偏移，下次不会补发未送出的尾部字节。由于 envelope 格式在 `SerializeDebugMessageEnvelope()` 中按 `[int32 length][uint8 type][body]` 组织，只要 socket 在拥塞或非阻塞场景下发生 partial send，客户端收到的就是一个被截断的协议帧。现有 transport tests 只覆盖内存 buffer 的序列化/反序列化和截断入站包，没有任何用例验证真实发送端的 partial send 重试。 |
| 根因 | 出站队列只记录整包 `Buffer` 和首次发送时间，没有“已发送字节数”状态；实现把 `FSocket::Send()` 的布尔成功误当成“整包写完”。 |
| 影响 | 一旦调试数据库、调用栈或变量包在发送过程中被部分写出，客户端将拿到损坏 envelope，后续解析要么直接报错，要么把剩余字节错位到下一帧。结果是协议完整性在网络背压下不成立，且自动化测试无法提前暴露该问题。 |

### 发现 12：StaticJIT 对多类未覆盖字节码不是安全 fallback，而是在生成期直接 `check` 中断

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `AngelscriptStaticJIT.cpp:66-75, 3330-3337, 3491-3497`；`AngelscriptBytecodes.h:25-28`；`AngelscriptBytecodes.cpp:6050-6054, 6400-6429, 6433-6517`；`AngelscriptPrecompiledDataTests.cpp:14-63` |
| 描述 | `CompileFunction()` 在生成模式下把每个脚本函数无条件加入 `FunctionsToGenerate`。后续 `WriteOutputCode()` 对整个集合逐个调用 `GenerateCppCode()`，而 codegen 主循环对每条 bytecode 执行 `check(bImplemented)`。但当前实现里仍有多类指令明确返回 `false` 或先 `check(false)` 再返回 `false`，包括 `asBC_CallPtr`（函数指针调用）、`asBC_SetListSize` / `asBC_PshListElmnt` / `asBC_SetListType`（列表初始化相关）、以及 `asBC_POWi/u/i64/u64`（integer Pow）。注释虽然声称 `asBC_CallPtr` “revert to the VM”，实际代码路径并没有函数级跳过或解释器 fallback；一旦这些指令进入待生成函数，就会在生成期命中断言。现有 StaticJIT tests 只有 precompiled-data round-trip / diff 两个小用例，没有任何脚本级用例覆盖这些特性。 |
| 根因 | JIT 覆盖策略是“先把所有函数加入生成队列，再要求每条字节码都必须实现”，但未实现指令没有被预扫描、降级或从 JIT 集合中剔除。 |
| 影响 | 只要项目脚本使用 function pointer、列表初始化或 integer `Pow` 等已知未覆盖能力，生成 precompiled/JIT 输出就可能在构建期或预编译阶段直接中断，而不是按用户预期安全退回 VM。这使得 JIT 覆盖范围既不完整，也缺少可验证的 fallback 安全性。 |

### 发现 13：后接入的调试客户端会在 `StartDebugging` 时重置全局断点与 break filters

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 行号 | `AngelscriptDebugServer.cpp:897-913, 1137-1146, 1222-1239`；`AngelscriptDebugServer.h:603-605, 718-723` |
| 描述 | `HandleMessage()` 处理 `StartDebugging` 时，在把新客户端加入 `ClientsThatAreDebugging` 之前，先执行 `BreakOptions.Empty()` 和 `ClearAllBreakpoints()`。`ClearAllBreakpoints()` 又会把脚本断点表、section 缓存和 data breakpoint 一并清空。因为这些状态都挂在 `FAngelscriptDebugServer` 的全局成员上，而不是 `FSocket*` 会话上，所以第二个客户端只要正常发起握手，就会无条件重置已有调试会话已经设置好的断点和 break filters。 |
| 根因 | DebugServer 把“开始调试某个连接”和“初始化整台服务器的调试状态”混成同一条路径，缺少按客户端隔离的会话状态。 |
| 影响 | 多客户端场景下，后接入的 IDE 会让先接入的 IDE 瞬间丢失全部断点、data breakpoint 和 break filter，导致停点行为突然改变。这和 DAP/debug session 应独立维护 `setBreakpoints` / `setExceptionBreakpoints` 状态的基本预期不一致。 |

### 发现 14：`CallstackRequests` 不在断线或 `StopDebugging` 时清理，下一次暂停会向 stale socket 重新排队发送

| 项目 | 内容 |
|------|------|
| 维度 | A / C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 行号 | `AngelscriptDebugServer.cpp:724-747, 812-816, 915-923, 924-927, 1382-1482`；`AngelscriptDebugServer.h:709-721` |
| 描述 | 收到 `RequestCallStack` 时，服务器只是 `CallstackRequests.Add(Client)`。客户端断线时的清理逻辑只会把它从 `Clients`、`ClientsThatAreDebugging`、`ClientsThatWantDebugDatabase` 和 `QueuedSends` 里移除，没有同步清掉 `CallstackRequests`；`StopDebugging` 分支也没有做这件事。结果是如果客户端在运行态请求过 call stack 后又先断开，数组里会留下 stale `FSocket*`。下一次任意 pause 时，`ProcessMessages()` 仍会遍历这个 stale 指针调用 `SendCallStack()`，而 `SendMessageToClient()` 又会通过 `QueuedSends.FindOrAdd(Client)` 给这个已脱离 `Clients` 管理的 socket 重新创建发送队列。 |
| 根因 | `CallstackRequests` 被实现成单独的原始指针队列，但生命周期管理没有和客户端主集合绑定，也没有去重与移除逻辑。 |
| 影响 | 调试器频繁在运行态请求调用栈并断开时，会留下无法再被正常清理的待发项和发送队列，导致无效发送、额外内存保留，甚至让后续暂停阶段反复操作已关闭连接，降低调试服务器稳定性。 |

### 发现 15：`HasStopped` / `HasContinued` / `ClearDataBreakpoints` 被广播给所有已连接客户端，非调试会话也会收到别人的事件

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 行号 | `AngelscriptDebugServer.cpp:541-544, 667-699, 822-826`；`AngelscriptDebugServer.h:587-588, 649-668` |
| 描述 | `RequestDebugDatabase` 会把客户端加入 `ClientsThatWantDebugDatabase`，说明系统已经区分了“只想拿 database 的连接”和“真正处于调试中的连接”。但 `PauseExecution()` 发送 `HasStopped` / `HasContinued` 时调用的是 `SendMessageToAll()`，其实现直接遍历 `Clients` 而不是 `ClientsThatAreDebugging`；data breakpoint 自动失效时发送的 `ClearDataBreakpoints` 也是同样的全播。因此，只要某个工具连到 debug server 上请求 database，它就会收到其他调试会话的 stop/continue/clear 事件。 |
| 根因 | 事件派发层只有“所有连接”与“单一连接”两种发送粒度，没有把调试控制事件限定到活动 debug session 集合。 |
| 影响 | 多工具并存时，纯观测型连接会被注入与自身无关的暂停/继续事件，导致协议语义串台；如果前端把这些消息当成当前 session 状态，界面会错误跳转到别人的停点。这也说明当前实现不满足 DAP 式的 session 隔离。 |

---

## 分析 (2026-04-08 02:58)

### 发现 16：`CallStack` 的 `ModuleName` 扩展字段没有 presence/version 标记，`FAngelscriptCallFrame` 自身无法对称反序列化

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:196-214`；`AngelscriptDebugServer.cpp:1435-1474`；`AngelscriptDebugProtocolTests.cpp:40-76` |
| 描述 | `SendCallStack()` 在 `DebugAdapterVersion >= 1` 时会给 `FAngelscriptCallFrame::ModuleName` 赋值并发送到 wire 上。但 `FAngelscriptCallFrame::operator<<` 只在 `Frame.ModuleName.IsSet()` 时额外写一个 `FString`，消息里没有任何布尔位、长度前缀分支或版本参数来声明“后面还跟着 ModuleName”。更关键的是，同一个 `operator<<` 在加载侧不会探测 `Ar.AtEnd()`，默认构造出来的 `Frame.ModuleName` 也是 unset，因此它根本不会读取这个字段。结果是这个结构体本身不是对称序列化格式，发送端一旦写出 `ModuleName`，同实现的接收端就无法按相同规则把它读回来。当前 `AngelscriptDebugProtocolTests.cpp` 只注册了 `StartDebugging`、`DebugServerVersion`、`Breakpoint`、`Variables`、`DataBreakpoints`、`BreakFilters` 和 `DatabaseSettings` round-trip，用例里没有任何 `CallStack` 覆盖。 |
| 根因 | `CallStack` 协议把可选字段建模成 `TOptional<FString>` 的“是否已赋值”，却没有把这个可选性编码进消息格式，也没有像 `Breakpoint` / `StartDebugging` 那样使用 `Ar.AtEnd()` 或版本门控来兼容读写两端。 |
| 影响 | 这使 `CallStack` 的协议演进依赖外部客户端“事先知道还要多读一个字符串”，而不是依赖消息本身自描述；任何复用仓库内同一套 serializer 的接收端都会在 `DebugAdapterVersion >= 1` 时错误解析帧数据。由于仓库内没有 round-trip 测试，这个扩展字段的兼容性回归目前不会被自动化捕获。 |

### 发现 17：调试测试客户端把“存在未消费尾字节”的 payload 当作成功反序列化，协议漂移会被自动化静默吞掉

| 项目 | 内容 |
|------|------|
| 维度 | D / C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 行号 | `AngelscriptDebuggerTestClient.h:43-54`；`AngelscriptDebuggerSteppingTests.cpp:253-262`；`AngelscriptDebuggerBreakpointTests.cpp:239-248` |
| 描述 | `FAngelscriptDebuggerTestClient::DeserializeMessage<T>()` 把 envelope body 交给 `FMemoryReader` 后，只检查 `Reader.IsError()`，完全不验证 `Reader.AtEnd()` 或“是否把整个 payload 消费完”。这意味着消息结构只要前半段还能按旧格式读出来，尾部多出来的字段会被直接忽略，函数仍返回成功。`Stepping` 和 `Breakpoint` 集成测试在读取 `CallStack` 时都依赖这个 helper，因此像 `FAngelscriptCallFrame::ModuleName` 这类尾部扩展字段即使没有被测试端真正读掉，自动化也不会报错。 |
| 根因 | 测试辅助层把“没有触发 reader error”误当成“payload 与期望结构完全一致”，缺少对 trailing bytes 的显式断言。 |
| 影响 | 调试协议新增字段、字段顺序漂移或可选字段读取缺失时，现有自动化很容易继续通过，导致协议兼容性问题只能在真实客户端上暴露。对 `CallStack`、`Variables` 等演进中的消息来说，这直接削弱了 DAP 兼容回归测试的可信度。 |

### 发现 18：Windows data breakpoint 基础设施是进程级单例，第二个 `DebugServer` 会接管前一个实例的异常处理

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:111-117, 281-286, 402-416, 419-424`；`AngelscriptEngine.cpp:1453-1455`；`AngelscriptMultiEngineTests.cpp:309-311, 765-768` |
| 描述 | Windows data breakpoint 路径把活动调试服务器保存在 `DataBreakpoint_Windows::GActiveDebugServer` 这个进程级 `TAtomic<FAngelscriptDebugServer*>` 里，并把 vectored exception handler 句柄保存在同一命名空间的静态变量中。`FAngelscriptDebugServer` 构造时会先 `RemoveVectoredExceptionHandler` 旧句柄，再 `AddVectoredExceptionHandler` 并把 `GActiveDebugServer` 覆盖成 `this`；析构时又无条件把全局指针清空。与此同时，引擎初始化路径在满足条件时会直接 `new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort)`，而代码库本身又明确支持同进程存在多个 engine/clone 实例。结果是只要同一进程里出现第二个 debug server，data breakpoint 的异常处理所有权就会从第一个实例被抢走。 |
| 根因 | data breakpoint 的异常入口被建模成“整个进程只有一个活动调试服务器”，没有把 handler 所属关系与 `FAngelscriptEngine` 或具体线程/连接绑定。 |
| 影响 | 在多引擎、测试引擎或并行调试环境下，先创建的 `DebugServer` 会突然失去 data breakpoint 能力，异常事件可能被路由到错误的 engine，或者在旧实例析构时把仍在使用的全局 handler 一并拆掉。该实现与仓库已经存在的 multi-engine 生命周期模型不兼容。 |

### 发现 19：`StaticJITBinds` 的 `GScriptNativeForms` 只增不减，native form 会跨引擎生命周期永久泄漏并保留 stale `asIScriptFunction*` 键

| 项目 | 内容 |
|------|------|
| 维度 | A / B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` |
| 行号 | `StaticJITBinds.cpp:27, 55-60, 116, 160, 203, 334, 372, 405, 443, 534, 596, 707, 781, 864, 905, 935, 965, 995, 1025` |
| 描述 | `StaticJITBinds.cpp` 把所有 native form 注册在进程级静态 `TMap<asIScriptFunction*, FScriptFunctionNativeForm*> GScriptNativeForms` 中。`FScriptFunctionNativeForm::GetNativeForm()` 只做 `Find`，而各类 `BindNative*`/`BindCustom*` 入口持续用 `new ...` 后 `GScriptNativeForms.Add(...)` 填表。源码里没有任何对应的 `delete`、`Remove`、`Reset` 或 `Empty` 路径来释放这些对象，也没有在 engine 销毁、bind 重新注册或 precompiled-data 生成结束时清空表。 |
| 根因 | JIT native form 注册表被设计成进程全局缓存，但生命周期没有和 `FAngelscriptEngine`、bind 数据库或一次生成会话绑定；表值还是裸指针，缺少所有权管理。 |
| 影响 | 每次 bind 注册都会永久泄漏一批 `FScriptFunctionNativeForm`。更严重的是，map key 是原始 `asIScriptFunction*`，旧 engine 销毁后这些键会变成 stale 指针；后续如果分配器复用相同地址，JIT 可能把过期 native form 错绑到新的脚本函数上，造成难以定位的错误 codegen。即使地址不复用，长期运行的 editor / automation 进程也会持续积累泄漏。 |

### 发现 20：模板类型的 native form 解析按“方法顺序”对齐，不按签名/名称匹配，JIT custom call 容易绑错目标

| 项目 | 内容 |
|------|------|
| 维度 | C / B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `StaticJITBinds.cpp:31-52`；`AngelscriptPrecompiledDataTests.cpp:14-63` |
| 描述 | `FScriptFunctionNativeForm::GetNativeForm()` 在处理 template instantiation 时，不是根据函数声明、参数列表或 function id 去找 template base type 上对应的 native form，而是先在当前 `objectType->methods` / `beh.constructors` 里找到“当前函数位于第几个槽位”，再把同一个索引拿去读 `templateBaseType->methods` / `beh.constructors`。源码注释直接写明了前提条件：`"Note that this has the assumption that the methods are always in the same order!"`。这意味着只要模板基类与某个实例化类型的 methods/constructors 顺序出现差异，JIT 就会返回错误的 native form。当前 StaticJIT 自动化仍只有两个 precompiled-data round-trip / diff 用例，没有任何模板容器或模板方法 native-call 覆盖。 |
| 根因 | native form 复用逻辑为了避免按签名重建映射，采用了“模板基类和实例化类型的方法槽位完全同构”的脆弱假设。 |
| 影响 | 一旦方法顺序被新增 bind、条件编译或模板特化打乱，JIT 生成的 custom/native call 可能调用到错误的构造函数、析构函数或成员方法，表现为错误代码执行而不是显式失败。这类问题很难在运行时直接定位，而且现有测试不会提前发现。 |

---

## 分析 (2026-04-08 03:07)

### 发现 21：data breakpoint 触发后被伪装成 `exception` 停止事件，停止原因不符合调试协议语义

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` |
| 行号 | `AngelscriptDebugServer.cpp:503-538`；`AngelscriptDebugProtocolTests.cpp:174-213` |
| 描述 | `ProcessScriptLine()` 在处理 `Breakpoint.bTriggered` 时，会构造 `FStoppedMessage`，把 `Msg.Text` 写成 data breakpoint 提示文本，但 `Msg.Reason` 被硬编码为 `TEXT("exception")`，随后调用 `PauseExecution(&Msg)`。因此客户端在 wire 上收到的是“异常停下”，而不是专门的数据断点停止原因。仓库内唯一与 data breakpoint 相关的测试只有 `AngelscriptDebugProtocolTests.cpp` 中的 round-trip 序列化用例，`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下没有任何运行时 data breakpoint 场景去校验 `HasStopped.Reason`。 |
| 根因 | data breakpoint 运行时复用了异常停止消息通道，只复用了 `Text` 字段描述触发信息，没有为停止原因建立独立枚举或协议映射。 |
| 影响 | 前端如果按停止原因驱动 UI 或后续请求，会把 data breakpoint 错判成脚本异常，无法正确呈现“数据监视点触发”的语义。这既偏离调试器常见的 stopped-reason 约定，也让自动化无法在协议回归时发现该问题。 |

### 发现 22：调试控制消息没有会话门槛，任何已连接 socket 都能在未握手状态下暂停、继续和改写断点

| 项目 | 内容 |
|------|------|
| 维度 | A / B / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:575-588, 712-721`；`AngelscriptDebugServer.cpp:822-923, 928-1128` |
| 描述 | `FAngelscriptDebugServer` 同时维护了 `Clients`、`ClientsThatWantDebugDatabase` 和 `ClientsThatAreDebugging` 三个集合，但 `HandleMessage()` 在处理 `Pause`、`Continue`、`StepIn/Over/Out`、`ClearBreakpoints`、`SetBreakpoint`、`SetDataBreakpoints`、`RequestVariables`、`RequestEvaluate` 等分支时，完全不检查 `Client` 是否已进入 `ClientsThatAreDebugging`。也就是说，只要 TCP 连接被接受并进入 `Clients`，该连接即使从未发送 `StartDebugging`，依然可以直接驱动全局调试状态和断点表。现有 debugger 集成测试全部在发送这些控制消息前先调用 `SendStartDebugging(2)`，没有任何负向用例验证“未握手客户端不能控制会话”。 |
| 根因 | 协议实现把“连接已建立”和“调试会话已建立”混为一谈，虽然定义了 `ClientsThatAreDebugging`，但没有把它用于消息授权或作用域限制。 |
| 影响 | 任意观测型工具、半初始化客户端甚至误连端口的进程，都可以在不完成握手的情况下暂停脚本、恢复执行、插入/清除断点或读取变量，直接破坏调试会话隔离。这既是调试协议完整性缺陷，也让多工具并存时的行为不可预测。 |

### 发现 23：Windows data breakpoint 编程路径仍按完整列表写 `Dr7`，第 5 个断点开始会污染保留位而不是安全截断

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:134-169, 247-250, 1275-1282`；`AngelscriptDebugProtocolTests.cpp:174-213` |
| 描述 | 虽然 `RebuildActiveDataBreakpoints()` 会把活动 data breakpoint 限制到 `DATA_BREAKPOINT_HARDWARE_LIMIT == 4`，但 `UpdateDataBreakpoints()` 在 Windows 上启动 `FUpdateDebugRegisterThread` 时传入的仍然是完整的 `DataBreakpoints` 数组，而不是裁剪后的 `ActiveDataBreakpoints`。`ApplyBreakpointsToThreadContext(const TArray<FAngelscriptDataBreakpoint>&)` 又直接按 `for (RegisterToModify = 0; RegisterToModify < Breakpoints.Num(); ++RegisterToModify)` 迭代，并对每个索引都执行 `Context.Dr7 |= ... << (16 + RegisterToModify * 4)`。同文件稍后自己的注释明确要求 `Dr7` 的 `32-63` 位应为 `0`。因此当客户端提交第 5 个断点时，代码会开始向 `Dr7` 的保留位写入数据，而不是像协议层表面表现的那样“只保留前四个”。现有 data breakpoint 测试仍只有 `AngelscriptDebugProtocolTests.cpp` 的序列化 round-trip，没有任何 Windows 运行时寄存器编程覆盖。 |
| 根因 | 运行时同时维护了“协议可见的完整断点列表”和“硬件可编程的活动断点列表”，但真正写入 CPU debug registers 的路径错误地使用了前者。 |
| 影响 | 一旦客户端发送超过 4 个 data breakpoint，结果不只是后续断点失效，还可能把无效位写进 `Dr7`，触发 `SetThreadContext` 失败、硬件断点行为异常或平台相关的不稳定表现。这说明当前实现并没有做到安全 fallback，而是在容量溢出时进入未定义寄存器配置。 |

### 发现 24：JIT 属性偏移引用缺失时会静默回退到旧 offset，Shipping/Test 构建默认没有校验保护

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `PrecompiledData.cpp:2330-2359, 2429-2458`；`StaticJITConfig.h:12-13`；`AngelscriptPrecompiledDataTests.cpp:13-123` |
| 描述 | JIT 运行时在 `UpdateReferenceMap()` 中遍历 `Database.PropertyOffsetLookups`，并把每个占位符更新为 `GetPropertyOffset(...)` 的返回值。`GetPropertyOffset()` 一旦发现 `PropertyReferences.Find(Reference.OldReference)` 返回空，就不会报错或拒绝加载，而是直接从 `Reference.OldReference` 里解出编译期旧 offset，缓存后继续返回。与此同时，编译期/运行期 offset 一致性校验仅在 `AS_JIT_VERIFY_PROPERTY_OFFSETS` 打开时才执行，而该宏默认配置是 `!UE_BUILD_SHIPPING && !UE_BUILD_TEST`，意味着 Shipping/Test 构建默认没有这层检查。仓库内 `AngelscriptPrecompiledDataTests.cpp` 只有 editor-only flag 与 module diff 两个用例，没有任何属性偏移漂移或缺失引用场景。 |
| 根因 | 属性引用修补链路把“引用缺失”视为可容忍情况，并选择继续使用旧布局数据；校验机制又被做成非 Shipping/Test 默认启用的调试辅助宏。 |
| 影响 | 只要 precompiled data 丢失了某个属性引用，或者加载到与编译期布局不兼容的脚本/类型集合，JIT 代码就可能继续按过期 byte offset 读写对象内存。这不是安全 fallback，而是把布局不一致直接降级成潜在内存破坏；当前自动化也不会在发布路径提前发现。 |

### 发现 25：`RequestEvaluate` 对截断消息体没有任何校验，缺失 `DefaultFrame` 时会把未初始化栈值当成 frame 使用

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:1107-1128, 2353-2375`；`AngelscriptDebugTransportTests.cpp:18-231` |
| 描述 | `HandleMessage()` 处理 `RequestEvaluate` 时，先读取 `FString Path`，再直接执行 `*Datagram << DefaultFrame;`，但整个分支从未检查 `Datagram->IsError()` 或 body 是否已经读完。`DefaultFrame` 还是一个未初始化的局部变量；如果客户端发来只包含表达式字符串、不包含 frame 整数的截断消息体，第二次读取失败后，这个未初始化值仍会传给 `GetDebuggerValue(Path, Value, &DefaultFrame)`。后续 `GetDebuggerValue()` 又会把该值当成调试 frame 继续解析。现有 `AngelscriptDebugTransportTests.cpp` 只覆盖 envelope 级别的截断/空包/非法长度，没有任何“消息体字段缺失”的负向测试。 |
| 根因 | 协议实现把 envelope 完整性校验和消息体反序列化校验割裂开了；进入 `HandleMessage()` 后，代码默认假设 body 字段总是完整且合法。 |
| 影响 | 恶意或损坏客户端可以用一个结构合法但字段不完整的 `RequestEvaluate` 包，把服务器带入未定义 frame 解析路径，轻则返回错误栈帧的值，重则触发不稳定行为。由于自动化只验证了 transport 层，当前回归测试无法发现这类 body-level 协议损坏。 |

---

## 分析 (2026-04-08 03:19)

### 发现 26：`PrecompiledData` 重绑函数/类型/全局引用时以 `checkf`/空指针崩溃收场，过期 JIT 数据没有安全 fallback

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `PrecompiledData.cpp:1711-1841, 1953-2153, 2227-2287, 2381-2427`；`AngelscriptPrecompiledDataTests.cpp:13-123` |
| 描述 | `PrepareToFinalizePrecompiledModules()` 会把 `FJITDatabase` 里的 `FunctionLookups`、`SystemFunctionPointerLookups`、`TypeInfoLookups` 和 `GlobalVarLookups` 全部回填到当前运行时对象。这个过程依赖 `GetTypeInfo()`、`GetFunction()` 和 `GetGlobalVariable()` 做名字/签名重绑定，但这些函数在找不到目标时都走硬失败路径：`GetTypeInfo()` 对缺失类型和缺失模块直接 `checkf`；`GetFunction()` 对缺失方法、全局函数和导入声明也直接 `checkf`；`GetGlobalVariable()` 甚至在 `Engine->GetModule(*Ref.Module, false)` 可能返回空的情况下直接访问 `Module->scriptGlobals`。代码里没有任何“该引用失效则放弃 JIT、退回 bytecode 解释执行”的分支。 |
| 根因 | StaticJIT/PrecompiledData 的外部引用修补把“编译期引用与运行时环境完全一致”当作前提条件，而不是把它视为需要验证的输入；一旦运行时脚本、模块或绑定签名发生漂移，加载路径选择断言终止，而不是降级。 |
| 影响 | 只要 precompiled/JIT 数据比当前脚本或绑定稍旧，例如函数改名、模块未加载、类型移动或全局变量消失，加载阶段就可能直接断言失败或空指针崩溃。这说明当前 JIT fallback 只覆盖“函数没生成”的情况，不覆盖“引用过期”的实际发布场景；而现有 `AngelscriptPrecompiledDataTests.cpp` 只有 flag roundtrip 和 module diff 两个回归用例，完全没有覆盖这类兼容性失配。 |

### 发现 27：按 filename 暂存的断点在模块后来编译成功后不会重新绑定，热重载场景会静默失效

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:589-600, 955-1035, 1197-1219`；`AngelscriptEngine.cpp:2499-2502`；`AngelscriptDebuggerBreakpointTests.cpp:346-380, 458-492, 594-628` |
| 描述 | `SetBreakpoint()` 在 `GetModuleByFilenameOrModuleName()` 找不到 module 时，会按注释所说退回到 `Key = BP.Filename`，把断点暂存在 `Breakpoints` 里，并直接把 `WantedLine` 计入断点表。但随后真正用于重新挂断点的 `ReapplyBreakpoints()` 只会对每个 key 调用 `GetModuleByModuleName(BreakpointElem.Key)`，完全不会再按 filename 解析。运行时命中路径 `ProcessScriptLine()` 也只会用当前执行模块的 `baseModuleName` 去 `Breakpoints.FindOrAdd(ModuleName)`，不会回查 filename key。引擎在编译完成后确实会调用一次 `DebugServer->ReapplyBreakpoints()`，但对这些 filename key 没有任何修复效果。 |
| 根因 | 断点存储层混用了“module name key”和“canonical filename key”两套索引，但重新绑定和实际命中都只认识 module name，没有把 filename fallback 闭环到后续解析阶段。 |
| 影响 | 用户如果在脚本尚未编译、模块暂时不可见或热重载重建过程中先下断点，服务器表面上会接受这个断点并增加 `BreakpointCount`，但模块真正出现后该断点仍不会命中，属于静默失效。现有 `BreakpointTests` 每次都是先编译 fixture、确认 module 已解析，再发送 `SetBreakpoint`，因此完全没有覆盖这种“先设断点后编译/重载”的真实工作流。 |

### 发现 28：`Pause` 请求并不会立即打断目标，只是在“下一条脚本行”才可能停下，协议语义与 DAP `pause` 明显偏离

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` |
| 行号 | `AngelscriptDebugServer.cpp:465-628, 667-700, 828-835`；`AngelscriptDebuggerTestClient.h:68-80`；`AngelscriptDebugTransportTests.cpp:199-230` |
| 描述 | `HandleMessage()` 收到 `Pause` 后只做三件事：设置 `bBreakNextScriptLine = true`、清空 step condition、调用 `UpdateLineCallbackState()`。它既不会立刻把 `bIsPaused` 设为 `true`，也不会直接调用 `PauseExecution()` 发送 `HasStopped(reason=\"pause\")`。真正能消费这个标志的地方只有 `ProcessScriptLine()`，而那必须等脚本再次进入 line callback。也就是说，若目标当前卡在 native/C++、阻塞等待、异常前后边界或已经没有后续脚本行，`Pause` 请求不会产生任何即时停止事件。 |
| 根因 | DebugServer 把 `Pause` 实现成“安排下一次 line callback 停下”，而不是“异步中断当前调试目标”；协议层没有单独的 interrupt 机制，只能借脚本行回调来完成暂停。 |
| 影响 | 前端发起 `pause` 时，用户预期是当前目标尽快停住并收到 `StoppedEvent(reason=\"pause\")`，但这里的实际语义是“如果之后还有脚本行再说”。这会让 pause 在长时间 native 执行或无后续脚本行时表现为挂起/无响应，明显不符合 DAP 的暂停语义。测试层也没有补位：`AngelscriptDebuggerTestClient` 甚至没有 `SendPause()` helper，现有自动化只有 transport 层验证 `Pause` envelope 能编码解码，没有任何集成测试验证 pause 行为。 |

### 发现 29：`DebugDatabaseSettings` / `AssetDatabase` 写了 `Version` 字段却不按版本解码，协议升级没有兼容护栏

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:507-554`；`AngelscriptDebugServer.cpp:1493-1505`；`AngelscriptDebugProtocolTests.cpp:230-245`；`AngelscriptDebuggerSmokeTests.cpp:49-123` |
| 描述 | `FAngelscriptAssetDatabase::operator<<` 和 `FAngelscriptDebugDatabaseSettings::operator<<` 都先写入一个整型 `Version`，但读端随后并不会根据该版本号决定还要不要继续读取后续字段。`AssetDatabase` 永远直接读 `Assets`，`DebugDatabaseSettings` 永远直接读 5 个布尔位；只有 `FAngelscriptFindAssets` 对 `ClassName` 做了 `Ar.AtEnd()` 检查，说明同一文件里其实已经存在兼容式写法。与此同时，`SendDebugDatabase()` 每次处理 `RequestDebugDatabase` 都先下发 `DebugDatabaseSettings`，因此这个不兼容布局是 debug database 握手路径上的实际协议面。 |
| 根因 | 消息格式虽然引入了 `Version` 字段，但实现把它当成注释式元数据，而不是解码条件；协议演进没有统一的 presence/version 分支约定。 |
| 影响 | 只要未来对 `DebugDatabaseSettings` 或 `AssetDatabase` 增删字段，旧客户端和新服务器之间就会在首个结构变化处直接反序列化错位，而不是像 `StartDebugging`、`Breakpoint`、`FindAssets` 那样优雅跳过未知尾字段。现有测试也没有覆盖这类兼容性：`DebugProtocolTests` 只做同版本 round-trip，`SmokeHandshake` 甚至没有请求 `DebugDatabase`，所以真正的版本漂移要到真实客户端升级后才会暴露。 |

---

## 分析 (2026-04-08 03:26)

### 发现 30：`StaticJIT` 把“bytecode 未实现”托付给 `check`，`DO_CHECK=0` 时不会 fallback，只会继续生成错误代码

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`UERelease/Engine/Source/Runtime/Core/Public/Misc/AssertionMacros.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `AngelscriptStaticJIT.cpp:3335-3336`；`AngelscriptBytecodes.cpp:6047-6055, 6059-6069, 6433-6517`；`AssertionMacros.h:224-258, 312-315`；`AngelscriptPrecompiledDataTests.cpp:14-63` |
| 描述 | `GenerateCppCode()` 对每条 bytecode 的唯一失败处理是 `bool bImplemented = Bytecode.Implement(Context); check(bImplemented);`。它没有根据 `bImplemented == false` 把当前函数移出 `FunctionsToGenerate`，也没有退回解释执行。与此同时，多个明确未覆盖的 bytecode 只是返回 `false` 或更糟地 `check(false); return true;`，例如 `asBC_CallPtr`、`asBC_FuncPtr`、`asBC_POWi/u/i64/u64`。引擎侧 `AssertionMacros.h` 又明确说明当 `DO_CHECK=0` 时，`check(expr)` 只展开成 `CA_ASSUME(expr)`，不会执行任何运行时中止逻辑。结果是在 `DO_CHECK` 关闭的构建里，这些“未实现”路径不会触发安全 fallback，而是继续把半成品或空实现代码写进 JIT 输出。 |
| 根因 | `StaticJIT` 没有真正的“unsupported bytecode -> 放弃 JIT 该函数”控制流；实现把 `check` 当成了功能性护栏，而不是调试断言。 |
| 影响 | 当前所谓 fallback 只在带 `DO_CHECK` 的构建里表现成断言失败；到了 `DO_CHECK=0` 的构建，JIT 会继续产出与原始 bytecode 语义不一致的代码，既不回退 VM，也不显式报错。这直接破坏了 fallback 安全性。现有 `AngelscriptPrecompiledDataTests.cpp` 仍只有 precompiled-data 的两个结构用例，没有任何脚本级 JIT 回归去覆盖这些 unsupported bytecode。 |

### 发现 31：`AssetRegistry` 变更回调用裸 `this` 捕获且从不解绑，`DebugServer` 销毁后会留下 use-after-free 路径

| 项目 | 内容 |
|------|------|
| 维度 | A / B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:419-433, 2078-2155, 2159-2205`；`AngelscriptDebugServer.h:737-738`；`AngelscriptEngine.cpp:335-338, 1160-1163, 1453-1455` |
| 描述 | `SendAssetDatabase()` 首次发送资产数据库时会调用 `BindAssetRegistry()`，后者通过 `OnAssetAdded().AddLambda([this] ...)`、`OnAssetRemoved().AddLambda([this] ...)`、`OnAssetRenamed().AddLambda([this] ...)` 和 `OnFilesLoaded().AddLambda([this, BindAssetRegistryChanges] ...)` 把多个捕获裸 `this` 的回调注册到 `AssetRegistry`。类里只记录了一个布尔值 `bAssetRegistryBound`，没有保存任何 `FDelegateHandle`，析构函数也只停止 `Listener`，完全没有解绑这些 delegate。与此同时，`FAngelscriptEngine` 在共享状态重置和 owned engine 释放时都会直接 `delete DebugServer`。这意味着只要某个调试会话请求过 `AssetDatabase`，后续 engine/debug server 生命周期结束后，`AssetRegistry` 仍可能回调到已经释放的 `FAngelscriptDebugServer`。 |
| 根因 | 事件订阅生命周期被设计成“注册一次永久有效”，但 `DebugServer` 本身并不是进程单例；实现既没有 delegate handle 管理，也没有在析构时做对称解绑。 |
| 影响 | 编辑器里脚本引擎重建、测试会话收尾或 shared state 切换后，只要资产注册表再发生增删改名事件，就可能命中悬空 `this` 指针，触发 use-after-free。仓库内也没有任何 `AssetDatabase` / `RequestDebugDatabase` 自动化去覆盖这条路径，意味着该缺陷只能在真实 editor 生命周期里偶发暴露。 |

### 发现 32：暂停态收到 `StopDebugging` 或断线时仍会补发 `HasContinued`，停止会话被错误伪装成继续执行

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:667-699, 733-741, 915-922`；`AngelscriptDebuggerSmokeTests.cpp:49-132` |
| 描述 | `PauseExecution()` 在进入暂停后会一直 `while (bIsPaused)` 轮询消息，退出循环后无条件执行 `SendMessageToAll(EDebugMessageType::HasContinued, ContinueMsg)`。但让这个循环结束的并不只有 `Continue`：`HandleMessage()` 的 `StopDebugging` 分支会直接把 `bIsPaused = false`，断线处理在最后一个调试客户端离开时也会把 `bIsPaused = false`。因此如果目标当前正停在 breakpoint/step/exception，上层发送 `StopDebugging` 或直接断开 socket，调试器仍会先看到一个 `HasContinued`，仿佛目标恢复了运行。 |
| 根因 | 暂停循环只用一个共享布尔位表示“离开暂停态”，没有区分离开原因是 `continue`、`terminate` 还是 `disconnect`；事件发射层因此把所有退出都映射成 `continued`。 |
| 影响 | 调试前端会把“结束会话”误判成“目标继续运行”，导致生命周期状态错乱，后续 UI 和请求时机都可能出错。这在 DAP 语义上尤其危险，因为 `continued` 通常意味着线程仍受当前会话控制。现有 `SmokeHandshake` 只验证 `StopDebugging` 后 `bIsDebugging` 变成 `false`，没有任何事件序列断言来发现这个问题。 |

### 发现 33：`StepIn` / `StepOver` / `StepOut` 没有 stopped-state 门槛，running 态请求会在下一条任意脚本行全局生效

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:847-895, 465-575`；`AngelscriptDebuggerSteppingTests.cpp:237-283, 370-453, 478-553, 584-655` |
| 描述 | `HandleMessage()` 处理 `StepIn` / `StepOver` / `StepOut` 时，只会直接设置 `bBreakNextScriptLine = true`、清空或重设 step condition，然后调用 `UpdateLineCallbackState()`；整个分支完全不检查 `bIsPaused`，也不验证当前请求是否对应某个已停止线程。随后 `ProcessScriptLine()` 只要看到 `bBreakNextScriptLine`，就在下一次满足条件的 line callback 上进入 `step` 停止。这意味着前端若在 running 态、过期 stop state，甚至还没收到任何 `HasStopped` 时误发 step 请求，服务器也会把它当成有效控制，在下一条任意脚本行全局触发一次步进停点；当 `asGetActiveContext()` 恰好为空时，`StepOver` / `StepOut` 还会退化成清空条件后的“下一行即停”。 |
| 根因 | DebugServer 把 step 请求实现成对全局 line-callback 标志位的直接写入，而不是“只允许针对当前 stopped state 的线程执行 step”的状态机。 |
| 影响 | 这与 DAP 对 `next` / `stepIn` / `stepOut` 必须建立在 stopped thread 上的语义不一致，也会让丢包、重放或时序偏差变成真实行为偏差。现有 stepping tests 全部只在收到 `HasStopped` 后才发送 step 消息，缺少任何 running-state 的负向覆盖，因此该缺陷不会被自动化发现。 |

---

## 分析 (2026-04-08 03:41)

### 发现 34：`FJITDatabase::Clear()` 会永久清空静态注册的 compiled-function 表，后续同进程 engine 无法再命中 StaticJIT

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | `StaticJITHeader.cpp:38-46`；`AngelscriptStaticJIT.cpp:24-37, 41-45`；`AngelscriptEngine.cpp:1551-1557, 1600-1603`；`PrecompiledData.cpp:527-540`；`AngelscriptMultiEngineTests.cpp:98-105, 202-223, 302-332` |
| 描述 | 编译进游戏二进制的 JIT 入口只在 `FStaticJITFunction` 的静态构造函数里向进程级单例 `FJITDatabase::Get().Functions` 注册一次。运行时加载 precompiled data 时，`FAngelscriptPrecompiledFunction::Create()` 依赖这张表按 `Id` 查找 `VMEntry/ParmsEntry/RawFunction`；找不到就静默回退解释执行。问题在于首个 engine 初始化结束时，`FAngelscriptEngine::Initialize()` 无条件执行 `FJITDatabase::Clear()`，把 `Functions` 和各种 lookup 表全部清空，而源码里没有任何后续重建 `Functions` 的路径。仓库同时又明确支持同进程反复创建 full engine 和 clone engine。 |
| 根因 | StaticJIT compiled metadata 被设计成进程级静态注册，但生命周期管理把它当成“一次初始化后即可销毁的临时加载缓存”；清理逻辑没有区分“可重建的 lookup 修补表”和“不可重建的 compiled-function 注册表”。 |
| 影响 | 首次 engine 启动后，当前 engine 已经拿到的函数指针还能继续工作，但之后同一进程里新建的 engine / test engine / clone 再加载同一份 precompiled data 时，`Functions` 已经为空，所有 JIT 查表都会退回解释执行。结果是 StaticJIT 覆盖范围依赖“这是不是本进程第一次初始化 engine”，既破坏多 engine 隔离，也让后续会话的性能/行为与首次会话分叉；现有 multi-engine tests 只验证 engine 生命周期和模块隔离，没有任何用例校验第二个 engine 仍能命中同一批 JIT entry。 |

### 发现 35：停止/继续/DataBreakpoint 清理事件会广播给所有已连接 socket，旁路客户端也会收到别人的调试会话事件

| 项目 | 内容 |
|------|------|
| 维度 | A / B / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:583-588, 628-645`；`AngelscriptDebugServer.cpp:543, 673-680, 698, 716-720, 822-826`；`AngelscriptDebuggerSmokeTests.cpp:28-123`；`AngelscriptDebugProtocolTests.cpp:23-31` |
| 描述 | `FAngelscriptDebugServer` 明确区分了 `Clients`、`ClientsThatWantDebugDatabase` 和 `ClientsThatAreDebugging` 三个集合，但 `SendMessageToAll()` 的广播目标始终是最宽泛的 `Clients`。`PauseExecution()` 发送 `HasStopped`/`HasContinued`、`ProcessScriptLine()` 发送 `ClearDataBreakpoints` 时都走这条广播路径。与此同时，任何连接一旦被 `PendingClients` 接受就进入 `Clients`，而 `RequestDebugDatabase` 甚至会在未 `StartDebugging` 的情况下把该 socket 注册为数据库订阅者并立刻返回数据。因此，只要有第二个仅做诊断/数据库同步的客户端连上端口，它也会被动收到别的调试会话的 stop/continue/data-breakpoint 事件。 |
| 根因 | 服务器虽然保存了“谁在真正调试”的客户端集合，但事件发射层没有使用该集合做广播范围约束，导致“连接存在”被错误等同于“属于当前 debug session”。 |
| 影响 | 这会把单个调试会话的生命周期事件泄漏给无关客户端，破坏 session 隔离；前端若仅想订阅 debug database 或做旁路诊断，也会被迫消费并误处理 `stopped` / `continued` / `clearDataBreakpoints` 事件。现有 smoke/protocol tests 只覆盖单客户端握手和纯序列化 round-trip，没有任何“双客户端中一个只请求数据库、另一个实际调试”的隔离用例，因此该问题当前不会被自动化捕获。 |

### 发现 36：`RequestEvaluate` 在返回前主动清空普通值的地址，V2 协议里的 `ValueAddress` 对大多数 evaluate 结果恒为 `0`

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:1107-1128, 2680-2687`；`AngelscriptType.h:624-737`；`AngelscriptDebuggerTestClient.h:78`；`AngelscriptDebuggerTestClient.cpp:302-309`；`AngelscriptDebugProtocolTests.cpp:130-170` |
| 描述 | `RequestEvaluate` 分支在 `GetDebuggerValue(Path, Value, &DefaultFrame)` 成功后，会把 `Value.GetAddressToMonitor()` 和 `Value.GetAddressToMonitorValueSize()` 填进返回包，表面上看支持 V2 的 monitorable value。问题是 `GetDebuggerValue()` 在 `OutInnerValues == nullptr` 的常规 evaluate 路径末尾，会无条件执行 `CurrentValue.Address = nullptr; CurrentValue.ClearLiteral();`。而 `FDebuggerValue::GetAddressToMonitor()` 对未显式设置 `AddressToMonitor`、也不是 temporary value 的普通局部变量/成员/全局变量，最终就是返回 `Address`。因此这些最常见的 evaluate 结果在返回前已经被去地址化，wire 上的 `ValueAddress` 会变成 `0`；只有少数 binder 手工设置过 `AddressToMonitor` 或 temporary value 保留了 `NonTemporaryAddress` 的类型还能幸免。 |
| 根因 | `GetDebuggerValue()` 把“避免把内部地址长期暴露给调用方”与“给 `Evaluate` 响应导出 monitorable address”混在同一收尾逻辑里；协议层没有为 evaluate 专门保留只读 monitor address 的独立字段生命周期。 |
| 影响 | 调试前端即使使用了 V2 协议，也无法从大多数 hover/watch/evaluate 结果里拿到有效的 `ValueAddress`，从而不能基于这些结果继续设置 data breakpoint 或做基于地址的增量调试操作。仓库里虽然有 `SendRequestEvaluate()` helper，但我对 `Debugger`/`Runtime/Tests` 做 grep 后，唯一命中只有 helper 自身声明与实现；现有自动化只验证 `FAngelscriptVariable` 的纯序列化 round-trip，没有任何集成测试检查 evaluate 返回的真实 `ValueAddress`。 |

### 发现 37：`StaticNames` / `StaticNamesByIndex` 是跨 engine 生命周期残留的进程级全局表，precompiled/JIT 二次加载会直接撞上空表断言

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 行号 | `AngelscriptEngine.cpp:69-70, 1562`；`AngelscriptEngine.h:578-583`；`AngelscriptPreprocessor.cpp:2048-2053`；`PrecompiledData.cpp:2436-2440, 2662-2663`；`AngelscriptMultiEngineTests.cpp:521-545` |
| 描述 | `FAngelscriptEngine::StaticNames` 和 `StaticNamesByIndex` 被定义成进程级静态全局表。源码里我对 `StaticNames|StaticNamesByIndex` 做全仓 grep 后，唯一写入点是 preprocessor 的 `GenerateStaticName()` 追加新条目，以及 precompiled data 的 save/load 路径读写它们；没有任何 `Empty()` / `Reset()` / teardown 清理逻辑。与此同时，`PrepareToFinalizePrecompiledModules()` 在加载 precompiled data 时显式 `check(FAngelscriptEngine::StaticNames.Num() == 0)`，假定这张表在导入前是空的，然后直接把缓存中的名字重新 append 进去。仓库的 multi-engine tests 又明确支持 full engine 销毁后继续在同进程里重建 engine。 |
| 根因 | Static-name 索引表承担了 preprocessor、precompiled data 和运行时 `FName` 绑定三方共享状态，却被实现成进程级静态容器，没有与 engine/shared-state 生命周期绑定，也没有在 engine 销毁时恢复到“未初始化”状态。 |
| 影响 | 只要同进程里先跑过一次脚本编译，再创建一个需要加载 precompiled data 的 engine，会因为残留的 `StaticNames` 直接触发 `check(Num() == 0)`；即使没有命中断言，旧表项也会让新的静态名索引继承上一轮 engine 的残留状态，导致二次会话的 static-name 编码依赖历史进程状态。现有 `SingleFullDestroyResetsGlobalState` 只验证 `FAngelscriptType::GetTypes()` 被清空，没有任何测试覆盖 `StaticNames`/`StaticNamesByIndex` 是否同步复位。 |

---

## 分析 (2026-04-08 03:53)

### 发现 38：`StaticJIT` 只维护 `debugCallStack` 行号，不接入解释器 line callback，JIT 覆盖函数里的断点/步进语义会整体失效

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `StaticJITHeader.h:337-344`；`AngelscriptStaticJIT.cpp:337-343, 374-380`；`AngelscriptEngine.cpp:5429-5460, 5463-5536`；`AngelscriptDebuggerBreakpointTests.cpp:309-429`；`AngelscriptDebuggerSteppingTests.cpp:370-655`；`AngelscriptPrecompiledDataTests.cpp:13-63` |
| 描述 | `StaticJIT` 生成代码时只插入 `SCRIPT_DEBUG_CALLSTACK_FRAME*` 和 `SCRIPT_DEBUG_CALLSTACK_LINE`，它们仅修改 `Execution.debugCallStack` 上的函数名/行号。相对地，`DebugServer` 的 line breakpoint、`step in/out/over`、以及 data breakpoint 触发后的落点清理，全都挂在 `FAngelscriptEngine::UpdateLineCallbackState()` 驱动的 `asCContext` line callback 上，最后统一进入 `AngelscriptLineCallback()` -> `DebugServer->ProcessScriptLine(Context)`。我对 `StaticJIT/` 做符号检索后，没有找到任何 JIT 代码去调用 `ProcessScriptLine`、`ProcessScriptStackPop` 或解释器 line callback 的桥接路径。基于这组已验证事实，可以直接推断：一旦脚本函数被 JIT 覆盖，DebugServer 在该函数内部就收不到逐行停点事件，line breakpoint 与 step 语义会退化为“只能在解释器帧上工作”。 |
| 根因 | JIT 调试插桩只补了“日志/调用栈可见性”最小集，没有把解释器时代承担断点与步进语义的 line-callback 协议重新桥接到 native JIT 执行路径。 |
| 影响 | 开启 `StaticJIT` 后，断点命中、`pause` 后落到下一脚本行、以及 `step in/out/over` 的核心语义会对 JIT 覆盖函数失真或直接失效，用户看到的是“同一脚本解释执行可调，JIT 后断不住/步不动”。测试层目前也没有兜底：现有 debugger 集成测试都只对运行时 `Fixture.Compile(Engine)` 的解释执行脚本做断点与步进校验，而 `StaticJIT` 只有两个 precompiled-data 结构测试，完全没有“JIT 覆盖函数仍可调试”的回归。 |

### 发现 39：`Evaluate` / `Variables` 解析链完全依赖解释器栈帧 API，JIT 覆盖函数里的 locals 与 `this` 无法进入调试协议

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h` |
| 行号 | `StaticJITHeader.h:193-218`；`AngelscriptDebugServer.cpp:2282-2345, 2348-2689, 2692-2842`；`AngelscriptDebuggerTestClient.h:76-80`；`AngelscriptDebuggerTestClient.cpp:296-321` |
| 描述 | `StaticJIT` 为调试保留下来的运行时元数据只有 `FScopeJITDebugCallstack` 这条单链表，字段里只有 `Filename`、`FunctionName`、`ThisObject`、`LineNumber` 和前驱指针，没有 locals、成员地址表或 frame id。相对地，`DebugServer` 的 `ResolveDebuggerFrame()`、`GetDebuggerValue()`、`GetDebuggerScope()` 全部只从 `asGetActiveContext()` 和 Blueprint stack 构造 frame 视图，局部变量解析依赖 `Context->GetVarCount/GetVarName/GetAddressOfVar`，`this` 解析依赖 `Context->GetThisTypeId/GetThisPointer`，module scope 依赖 `Context->GetFunction(Frame)`。源码里没有任何分支会读取 `Execution.debugCallStack` 或 `FScopeJITDebugCallstack::ThisObject` 来补齐 JIT 帧变量。基于这些已验证事实，可以直接推断：只要停点落在 JIT 覆盖函数里，watch/hover/locals 面板就没有可用的数据来源。 |
| 根因 | JIT 调试设计只记录了最薄的一层“栈顶文本信息”，但 `Evaluate` / `Variables` 协议仍完整绑定在解释器上下文 API 上，没有建立 JIT frame 到 debugger frame/value model 的映射。 |
| 影响 | 即便未来补上 JIT frame 的 callstack 展示，当前实现也仍然无法对这些帧做 locals、成员和 `this` 观察，前端会表现为 JIT 函数“能停但看不到变量”或直接落回错误帧。测试同样为空白：仓库里只有 `SendRequestEvaluate()` / `SendRequestVariables()` helper，本轮我对 `AngelscriptTest/Debugger` 和 `AngelscriptRuntime/Tests` 做 grep 没有找到任何对应调用，说明没有自动化覆盖 JIT 停点下的变量求值。 |

### 发现 40：`SetBreakpoint` 只有“改行号/删除”时才回包，正常命中的断点没有 verified ack

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:1028-1058`；`AngelscriptDebuggerTestClient.cpp:312-321`；`AngelscriptDebuggerBreakpointTests.cpp:370-380`；`AngelscriptDebuggerSteppingTests.cpp:387-396, 495-504, 597-606`；`AngelscriptDebugProtocolTests.cpp:49-118` |
| 描述 | `HandleMessage(SetBreakpoint)` 在 `CodeLine != WantedLine && BP.Id != -1` 时会回发一个“改行号后的断点”，在 `CodeLine == -1 && BP.Id != -1` 时会回发一个 `LineNumber = -1` 的删除通知；但最常见的成功路径 `CodeLine == WantedLine` 只把断点加入 `Active->Lines`，随后直接返回，没有任何 `SendMessageToClient()`。这意味着协议层从来不会对“断点已原样接受”给出正向确认。现有测试也没有按 wire 语义验证它：测试客户端 `SendSetBreakpoint()` 只是发包，断点/步进集成测试随后都改为轮询 `Session.GetDebugServer().BreakpointCount`，`AngelscriptDebugProtocolTests.cpp` 也只做结构 round-trip，没有任何 verified/ack 断言。 |
| 根因 | 当前协议把 `SetBreakpoint` 同时承担了“客户端请求”与“服务器异动通知”两种角色，但实现只覆盖了需要更正客户端状态的少数分支，没有给正常成功路径定义显式回执。 |
| 影响 | 客户端无法区分“断点已被服务器原样接受”和“消息丢失/时序落后/断点仍未绑定”，只能依赖后续是否真的命中来反推结果。这不符合 DAP `setBreakpoints` 应返回每个 breakpoint verified 状态的基本期望，也让自动化无法从协议层及时发现注册失败。 |

### 发现 41：`FindAssets` 请求当前是源码级 no-op，delegate 签名按值传递结果且服务端从不回包

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `AngelscriptDebugServer.h:68, 520-533`；`AngelscriptDebugServer.cpp:1161-1170`；`AngelscriptRuntimeModule.h:19, 45`；`AngelscriptRuntimeModule.cpp:120-123` |
| 描述 | 协议里 `FindAssets` 只有一个消息类型，负载结构 `FAngelscriptFindAssets` 也包含 `Assets` 结果数组，看起来像“请求后返回命中结果”。但服务端收到消息后只是反序列化 `AssetList`，解析 `ClassName`，然后调用 `FAngelscriptRuntimeModule::GetDebugListAssets().Broadcast(AssetList.Assets, BaseClass)`；整个分支没有任何 `SendMessageToClient()`。更关键的是 `FAngelscriptDebugListAssets` 被声明成 `DECLARE_MULTICAST_DELEGATE_TwoParams(..., TArray<FString>, UASClass*)`，第一个参数是按值传递，不是引用或返回值，所以监听器即使往 `Assets` 里追加结果，也只会修改一份临时副本，调用返回后全部丢失。 |
| 根因 | 这条扩展消息把“资产搜索请求”和“资产搜索结果”混成了同一个结构，但实现同时缺了两块关键能力：没有服务端回包路径，delegate 也没有任何可把结果带回调用点的签名。 |
| 影响 | 当前 `FindAssets` 功能在协议层根本不可用，客户端不可能通过这条消息拿到搜索结果；无论监听器是否真的找到了资产，结果都会在 `Broadcast()` 返回时消失。仓库里也没有相应用例覆盖它：本轮对 `AngelscriptRuntime/Tests` 与 `AngelscriptTest` 的 grep 没有找到任何 `FindAssets` 测试或测试客户端 helper。 |

### 发现 42：`CreateBlueprint` 扩展消息没有任何成功/失败回执，非法输入会在目标进程弹出模态对话框

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` |
| 行号 | `AngelscriptDebugServer.h:74, 557-564`；`AngelscriptDebugServer.cpp:1172-1189`；`AngelscriptRuntimeModule.h:20-21, 46-47`；`AngelscriptRuntimeModule.cpp:126-135` |
| 描述 | 协议枚举里只有一个 `CreateBlueprint` 消息类型，没有任何 `...Result` / `...Failed` 对应项。服务端处理该消息时，成功路径只是 `Broadcast()` 一个 `FAngelscriptEditorCreateBlueprint` multicast delegate，失败路径则直接执行 `FMessageDialog::Open(...)` 弹出模态框提示“class 不存在”；整个分支从头到尾都没有 `SendMessageToClient()`。此外，相关 editor delegate 也只有“广播创建请求”和“查询默认路径”两个本地回调，没有把创建结果、资产路径或错误文本重新编码回 socket 的通道。 |
| 根因 | 这条扩展协议被实现成“远端命令本地编辑器做事”，而不是“请求-响应”语义；错误处理因此退化成目标进程 UI，而非可序列化的协议消息。 |
| 影响 | 远端客户端无法知道 Blueprint 到底是否创建成功、创建到了哪里、还是因为类名错误而失败；自动化或 headless 场景下，非法输入甚至会把目标进程卡在模态对话框上。仓库里对 `AngelscriptRuntime/Tests`、`AngelscriptTest/Debugger` 和 `AngelscriptTest/Shared` 的 grep 也没有任何 `CreateBlueprint` 调试协议测试，因此这个分支目前完全靠人工交互兜底。 |

---

## 分析 (2026-04-08 03:57)

### 发现 43：`CallStack` 协议的 `ModuleName` 字段只能写不能读，多帧调用栈在 V1/V2 适配器下会发生反序列化错位

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:196-223`；`AngelscriptDebugServer.cpp:1382-1482`；`AngelscriptDebuggerBreakpointTests.cpp:239-247, 423-431`；`AngelscriptDebuggerSteppingTests.cpp:253-261` |
| 描述 | `FAngelscriptCallFrame::operator<<` 写入 `Name`、`Source`、`LineNumber` 后，只在 `Frame.ModuleName.IsSet()` 为真时才序列化 `ModuleName`，但加载路径没有使用 `Ar.IsSaving() || !Ar.AtEnd()` 之类的条件读取逻辑。`FAngelscriptCallStack` 反序列化时数组元素都是默认构造的 frame，`ModuleName` 初始为 unset，因此读取端永远不会消费这个字段。与此同时，`SendCallStack()` 在 `DebugAdapterVersion >= 1` 时会为 script frame 写入真实 `ModuleName`，并为 BP/C++ frame 写入空字符串模块名。结果不是单纯“丢失模块名”而已；只要调用栈里存在第二帧，前一帧遗留的 `ModuleName` 字节就会被下一帧的 `Name`/`Source`/`LineNumber` 当成新帧内容继续读取，整个消息从第二帧开始错位。 |
| 根因 | `FAngelscriptCallFrame` 把“可选字段是否存在”的判断绑在了对象当前的 `TOptional` 状态上，却没有为 wire format 编码 presence bit，也没有在加载时根据剩余字节显式消费可选字段。 |
| 影响 | 在启用 V1/V2 适配器时，多帧 `CallStack` 结果不再可信：客户端要么完全收不到正确的 `ModuleName`，要么从第二帧开始把字段串位，进而影响 frame 展示、source 归属和后续按模块定位脚本源码的逻辑。这一问题当前没有被自动化捕获；现有 breakpoint/stepping 测试虽然会请求 `CallStack`，但只断言顶层 frame 的 `Source`/`LineNumber`，没有任何 round-trip 或多帧断言覆盖 `ModuleName`。 |

### 发现 44：任何已连接 socket 都能在未加入 debug session 的情况下控制暂停/步进/断点并读取调试数据

| 项目 | 内容 |
|------|------|
| 维度 | A / B / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:719-720, 733-745, 820-1193`；`AngelscriptDebuggerSmokeTests.cpp:49-88`；`AngelscriptDebuggerBreakpointTests.cpp:44-54, 193-213`；`AngelscriptDebuggerSteppingTests.cpp:47-52, 198-218` |
| 描述 | 新连接一旦被 `PendingClients` 接受就直接进入 `Clients`。`HandleMessage()` 中除 `RequestDebugDatabase` 会把 socket 加入 `ClientsThatWantDebugDatabase`、`StartDebugging` 会把 socket 加入 `ClientsThatAreDebugging` 外，其余控制/读取消息没有任何会话成员校验：`Pause`、`Continue`、`StepIn/Over/Out`、`StopDebugging`、`SetBreakpoint`、`SetDataBreakpoints`、`RequestVariables`、`RequestEvaluate`、`GoToDefinition`、`BreakOptions`、`FindAssets`、`CreateBlueprint`、`Disconnect` 都是对任意已连接客户端直接生效。源码里唯一显式使用 `ClientsThatAreDebugging` 的地方只有断线清理和 ping 广播，而不是权限判断。 |
| 根因 | DebugServer 把 socket 接入层、数据库订阅层和实际 debug session 层拆成了三个集合，但消息调度层没有基于这些集合做授权/隔离，导致“连接存在”被错误当成“允许驱动当前会话”。 |
| 影响 | 旁路客户端哪怕从未发送 `StartDebugging`，也能恢复或终止别人的暂停态、插入/清空断点、修改 break filter，甚至在会话暂停时读取 `Evaluate`/`Variables` 数据。这既破坏 DAP/debug session 的基本隔离，也给调试端口暴露出一个“只要能连上就能操控会话”的攻击面。当前自动化没有覆盖这种场景；现有 smoke/breakpoint/stepping 测试全部在握手后再发控制消息，没有任何“仅连接或只订阅 debug database 的客户端尝试发控制消息”的负向用例。 |

### 发现 45：断点在 module 未解析时按 filename 存储，但后续 reapply 和命中路径只按 module name 查找，导致这类断点永远不会重新绑定

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:589-599, 936-968, 1197-1215`；`AngelscriptEngine.cpp:2502, 3007-3026`；`AngelscriptDebuggerBreakpointTests.cpp:372-388, 484-500, 620-636`；`AngelscriptDebuggerSteppingTests.cpp:388-404, 496-512, 598-614` |
| 描述 | `SetBreakpoint()` / `ClearBreakpoints()` 在 `GetModuleByFilenameOrModuleName()` 找不到 module 时，会把 breakpoint store 的 key 退化成 canonized `Filename`。但 `ReapplyBreakpoints()` 重新绑定时只调用 `GetModuleByModuleName(BreakpointElem.Key)`，默认把 map key 当成 module name 使用；如果 key 实际是 filename，这一步必然拿不到 module。更糟的是运行期 `ProcessScriptLine()` 构造 `SectionBreakpoints` 缓存时，也只用 `Context->m_currentFunction->module->baseModuleName` 去 `Breakpoints.FindOrAdd(ModuleName)`，于是 filename-keyed store 不会被命中路径复用，反而会额外创建一个空的 module-name store。 |
| 根因 | 断点索引的主键语义在不同阶段不一致：设置阶段允许 `filename` fallback，而重绑和运行期命中阶段都假设主键恒为 `module name`。 |
| 影响 | 任何在 module 尚未可解析时设置的断点，例如脚本刚被丢弃后重编、hot reload 过渡态或调试器先于 module 注册时下发的断点，都会长期停留在“客户端认为已设置、服务端永远不会命中”的悬空状态。这正好击中用户最难排查的一类 breakpoint correctness 问题，而现有 breakpoint/stepping 测试全都在 module 已明确存在的 fixture 上设置断点，没有覆盖“先设置后重绑”场景。 |

### 发现 46：`StaticJIT` 的布局校验一旦发现属性偏移或类型尺寸漂移就直接 `checkf` 终止，没有按函数或按 bundle 安全回退到 VM

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `StaticJITConfig.h:12-13`；`StaticJITHeader.cpp:89-100`；`PrecompiledData.cpp:2443-2476`；`AngelscriptPrecompiledDataTests.cpp:13-63` |
| 描述 | `AS_JIT_VERIFY_PROPERTY_OFFSETS` 在默认配置下对所有非 `Shipping`/`Test` 构建启用。JIT 生成出来的全局 `FJitVerifyPropertyOffset` / `FJitVerifyTypeSize` 静态对象会把“编译期看到的属性偏移、类型大小、对齐”注册进 `FJITDatabase`。随后 `PrepareToFinalizePrecompiledModules()` 在加载 precompiled data 时逐项执行 `checkf(Property->byteOffset == Element.Value)`、`checkf(TypeInfo->size == Element.Value)`、`checkf(TypeInfo->alignment == Element.Value)`。这里没有任何“仅禁用该函数 JIT entry”“清空当前 bundle 的 JIT 并退回 bytecode”或“记录告警继续启动”的路径；一旦 ABI 与生成时不一致，结果就是硬断言。 |
| 根因 | 这套校验把“JIT 可执行性验证”实现成 fatal assertion，而不是 fallback gate。系统只在 `PrecompiledDataGuid` 不匹配时整体关闭 transpiled code，却没有为更细粒度的布局漂移提供降级通道。 |
| 影响 | 当脚本对应的 C++ 绑定类型发生布局变化、编译选项改变对象对齐，或者同一 precompiled/JIT 产物被拿到 ABI 不同的开发构建里时，启动流程不会安全地回退到解释执行，而是直接在加载 precompiled data 阶段崩溃。这与“fallback 安全性”的目标相反。测试层也没有保护网：`AngelscriptPrecompiledDataTests.cpp` 目前只有 editor-only flag roundtrip 和 module diff 两个结构测试，没有任何“属性偏移/类型尺寸不匹配时应如何处理”的回归。 |

### 发现 47：`StaticJIT` 编译信息被建模成进程级唯一单例，第二份 transpiled bundle 不是协商回退而是直接在静态初始化期断言

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `StaticJITHeader.cpp:19-35`；`AngelscriptStaticJIT.cpp:3684-3692`；`AngelscriptEngine.cpp:1550-1556` |
| 描述 | `FStaticJITCompiledInfo` 通过 `GetActiveCompiledInfo()` 持有一个进程级静态指针，构造函数里显式 `checkf(ActiveInfo == nullptr, "Only one angelscript static JIT info can be compiled in!")`，然后把当前实例注册成唯一活动项。与此同时，`WriteOutputCode()` 每次生成 transpiled 代码都会额外写出一个 `AngelscriptJitInfo.jit.cpp`，其中包含 `AS_FORCE_LINK static const FStaticJITCompiledInfo JitInfo(FGuid(...))`。引擎加载 precompiled data 时也只会读取这一份全局 `CompiledInfo`，并拿它与当前 cache 的 `DataGuid` 做单次比较。基于这组已验证事实，可以直接推断：运行时架构只接受“一进程一份编译进来的 transpiled bundle”，不存在第二份 bundle 的匹配、屏蔽或逐 bundle fallback 机制。 |
| 根因 | JIT 元数据被设计成进程级单例，而不是可枚举的 bundle 列表；加载侧也只有“一份 GUID 对比一份 precompiled cache”的模型。 |
| 影响 | 一旦同一进程需要装入第二份独立生成的 transpiled code，结果不是优雅地忽略不匹配 bundle 或按 GUID 关闭其中一份 JIT，而是在静态初始化期直接命中 `checkf`。这让 `StaticJIT` 无法自然扩展到多 bundle / 多来源脚本资产的场景，也说明当前 fallback 语义只覆盖“单 bundle GUID 不匹配”，并不覆盖“存在多份 bundle”的架构演进。 |

### 去重说明

本轮追加的 `发现 43` 与文档中更早的 `发现 16` 指向同一 `CallStack.ModuleName` 序列化问题。由于本任务规则要求只追加、不回写历史内容，这里保留该条目，但后续阅读与去重应以前面的 `发现 16` 为准。 

---

## 分析 (2026-04-08 10:26)

### 发现 48：`SendCallStack` 对 Blueprint/C++ 帧序列化未初始化的 `LineNumber`，非脚本帧行号会变成随机值

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:196-213`；`AngelscriptDebugServer.cpp:1400-1476`；`AngelscriptDebuggerBreakpointTests.cpp:239-247, 423-431`；`AngelscriptDebuggerSteppingTests.cpp:253-261` |
| 描述 | `FAngelscriptCallFrame` 里的 `LineNumber` 没有默认初始化，但 `operator<<` 总会无条件序列化它。`SendCallStack()` 在 Blueprint 帧分支和 `asFUNC_SYSTEM` 的 C++ 帧分支里只填写 `Name`、`Source`、`ModuleName`，从未给 `Frame.LineNumber` 赋值；只有脚本帧分支才会调用 `Context->GetLineNumber(...)`。因此只要调用栈里混入 BP/C++ 帧，协议就会把栈上残留值当成行号发给客户端。 |
| 根因 | `CallStack` wire format 把 `LineNumber` 设计成必填字段，但 `SendCallStack()` 只在 script frame 路径设置该字段，非脚本帧路径遗漏了默认值或显式 sentinel。 |
| 影响 | 调试前端在展示跨脚本/BP/C++ 混合调用栈时，会收到不可信的行号，轻则 source hyperlink 跳到错误位置，重则让 frame 选择和后续源码导航建立在随机数据上。现有 breakpoint/stepping 测试虽然会请求 `CallStack`，但只断言顶层脚本帧信息，没有任何用例覆盖带 BP/C++ 帧时的 `LineNumber` 合法性。 |

### 发现 49：data breakpoint 更新线程每次都会泄漏一个 duplicated thread handle

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:206-218, 221-232, 1241-1282`；`AngelscriptDebugProtocolTests.cpp:174-214` |
| 描述 | `GetThreadAgnosticCurrentThreadHandle()` 每次调用都通过 `DuplicateHandle(...)` 生成一个新的 `HANDLE` 并返回。`UpdateDataBreakpoints()` 在每次设置、清空或重建 data breakpoint 时都会用这个句柄构造 `FUpdateDebugRegisterThread`。但 `FUpdateDebugRegisterThread` 的析构函数只 `WaitForCompletion()`，整个类型里没有任何 `CloseHandle(ThreadToDebug)`，也没有别的清理路径释放这个 duplicated handle。 |
| 根因 | 更新 debug registers 的辅助线程承担了“等待线程退出”的职责，但没有承担被复制线程句柄的所有权回收；`UpdateDataBreakpoints()` 又把这条路径放在所有 data breakpoint 生命周期事件上反复调用。 |
| 影响 | 频繁设置/移除 data breakpoint，或脚本变量离开作用域触发 `UpdateDataBreakpoints()` 时，进程会持续累积未关闭的内核线程句柄。长时间调试后这会变成稳定的 handle leak，最差情况下会耗尽句柄配额并让后续调试基础设施失败。现有测试只有 `FAngelscriptDataBreakpoints` 的纯序列化 round-trip，没有任何真实 data breakpoint 生命周期测试能发现该泄漏。 |

### 发现 50：`GoToDefinition` 协议只有请求没有结果通道，服务端把导航变成了本地 UI 副作用

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 行号 | `AngelscriptDebugServer.h:25-80, 453-463`；`AngelscriptDebugServer.cpp:1130-1135, 1288-1375`；`AngelscriptDebuggerTestClient.h:68-80`；`AngelscriptDebuggerTestClient.cpp:250-322` |
| 描述 | 协议枚举里只有一个 `GoToDefinition` 消息类型，负载也只包含 `TypeName` 和 `SymbolName`，没有任何 `...Result`/`...Failed` 对应结构。服务端收到后只是执行 `GoToDefinition(GoTo)`，内部调用 `FSourceCodeNavigation::NavigateToFunction/Property/Class` 在目标进程本地打开源码，然后直接返回；整个链路没有 `SendMessageToClient()`。测试客户端同样没有 `SendGoToDefinition()` helper，也没有等待结果消息的 API，说明当前协议从设计上就不存在“把定义位置返回给请求方”的路径。 |
| 根因 | 该扩展功能被实现成“远端命令编辑器本地导航”的 side-effect，而不是标准的请求-响应协议。消息模型缺少 location/range 结果结构，也缺少失败回执。 |
| 影响 | 调试客户端无法拿到定义位置并在自身 UI 中导航，只能依赖 debug target 机器本地弹出源码窗口。这让远端 IDE、自动化测试和 headless 场景都无法可靠使用该能力，也偏离了现代调试/编辑协议对 definition lookup 的基本请求-响应语义。 |

### 发现 51：同一轮 data breakpoint flush 会为每个命中项单独进入一次 `PauseExecution`，客户端必须连续继续多次才能前进

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:479-545, 667-699`；`AngelscriptDebugProtocolTests.cpp:174-214` |
| 描述 | `ProcessScriptLine()` 在处理 data breakpoint 触发时，先把所有命中的 `InfoText` 收集进 `TriggeredBreakpoints`，随后对数组逐项执行 `PauseExecution(&Msg)`。`PauseExecution()` 会立刻发送一次 `HasStopped`，阻塞在 `while (bIsPaused)` 里等待客户端继续，再补发一次 `HasContinued` 后才返回。结果是如果同一轮 flush 里有两个及以上触发项，例如同一条写操作命中了多个 watchpoint，或一次栈弹出让多个局部变量同时 out-of-scope，服务器会在还没执行下一条脚本指令前连续制造多次 stop/continue 循环。 |
| 根因 | data breakpoint flush 以“每条触发文案单独暂停一次”的方式实现，而不是把同一条脚本线上的多命中压缩成单个 stop event 或一个聚合消息。 |
| 影响 | 调试前端会表现成“按一次 continue 仍然停在原地，还要继续多次才能走”的 stop storm，严重破坏 step/continue 语义，也让命中顺序影响用户操作次数。现有测试只覆盖 data breakpoint 消息 round-trip，没有任何运行态测试验证多命中时只产生一次暂停。 |

### 发现 52：data breakpoint 的 `HitCount` 在协议层被压成 `int8`，超过 127 的命中条件会回绕成“立即触发”

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:300-327, 330-395, 402-410`；`AngelscriptDebugServer.cpp:308-321`；`AngelscriptDebugProtocolTests.cpp:174-214` |
| 描述 | 协议结构 `FAngelscriptDataBreakpoint` 把 `HitCount` 定义成 `int8`，序列化时也按 `int8` 直接写入。运行时 `FAngelscriptActiveDataBreakpoint` 虽然把它扩成 `TAtomic<int32>`，但 `CopyFrom()` 只是把这个 8 位值原样提升。触发路径随后按 `PreviousHitCount > 0` 决定是否递减等待，否则立即把断点标记为已触发。这样一来，只要客户端设置的 hit count 超过 `127`，它在协议层就会发生有符号回绕，服务端会把它当成 `<= 0` 的值，在第一次命中时直接触发。 |
| 根因 | 命中计数在 wire format 中被建模成 8 位有符号整数，而不是和运行时逻辑一致的更宽整数；协议层没有范围校验或拒绝路径。 |
| 影响 | 高命中条件的 watchpoint 无法按用户设定工作，表现为“本该第 N 次再停，结果第一次就停了”。现有测试只验证 `HitCount = 3` 和 `1` 的 round-trip，没有任何边界值或溢出测试，所以该问题会一直隐藏在大于 127 的实际调试场景里。 |

---

## 分析 (2026-04-08 10:43)

### 发现 53：precompiled/JIT 产物在生成期关闭了 line cue，JIT miss 后的 VM fallback 也无法提供断点和步进

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_bytecode.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `AngelscriptEngine.cpp:1433-1446, 5429-5460`；`PrecompiledData.cpp:403-409, 538-547, 585-592`；`as_bytecode.cpp:1827-1844`；`as_context.cpp:2411-2424`；`AngelscriptPrecompiledDataTests.cpp:14-63` |
| 描述 | 生成 precompiled data 时，`Initialize_AnyThread()` 在创建 `StaticJIT` 后显式执行 `Engine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, 1)`。AngelScript bytecode 生成器随后会把每个 `Line()` 生成的 `asBC_LINE` 指令大小设为 `0`，注释明确说明“without line cues these instructions will be removed”，也就不会生成运行期触发 line callback 的 `asBC_SUSPEND` 停点。`FAngelscriptPrecompiledFunction::InitFrom()` 又把这份已去 line cue 的 `scriptData->byteCode` 原样 memcpy 进 `ByteCode` 缓存；等到运行时某个函数没有命中 `FJITDatabase::Functions` 时，`Create()` 会走 `AllocateScriptFunctionData()` + memcpy 把缓存 bytecode 恢复成 VM fallback，`Process()` 只做引用修复，不会重建 line cue。与此同时，DebugServer 的断点/步进链路完全依赖 `UpdateLineCallbackState()` 打开 `asCContext::m_lineCallback`，而 `as_context.cpp` 里真正调用它的运行期分支只在 `case asBC_SUSPEND`。因此只要某个函数从 precompiled cache 回退到 VM，它实际执行的仍是“没有 line cue 的 bytecode”，DebugServer 不会在函数内部收到逐行回调。 |
| 根因 | 生成 precompiled/JIT 产物时把“为减小 bytecode 体积而关闭 line cue”的构建选项和“未来运行时的安全 VM fallback”复用了同一份缓存格式，导致 fallback bytecode 从生成之初就缺少调试所需的行挂钩。 |
| 影响 | 当前 fallback 只保证“还能执行”，并不保证“还能被调试”。一旦某个函数因为 JIT 覆盖缺口、development mode、bundle 不匹配等原因落回 VM，line breakpoint、step in/over/out、按行 pause 都会在该函数内部失效，前端只能看到函数外层或结束时的停点。这和 fallback 安全性的目标相反。测试层同样没有护栏：仓库里与 precompiled/JIT 直接相关的自动化只有 `AngelscriptPrecompiledDataTests.cpp` 的两个结构用例，没有任何“从 precompiled cache 回退到 VM 后仍能断点/步进”的场景。 |

### 发现 54：live socket 收包路径绕过了已测试的 envelope parser，非法长度包不会被拒绝而会把连接流永久打乱

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:64-108, 756-794`；`AngelscriptDebugTransportTests.cpp:153-197` |
| 描述 | 仓库已经实现了 `TryDeserializeDebugMessageEnvelope()`，它会在缓冲区内保留 partial envelope，并对 `MessageLength <= 0 || > 1024 * 1024` 直接返回错误；对应的 transport tests 也专门覆盖了 `TruncatedEnvelope` 与 `InvalidLength`。但 `FAngelscriptDebugServer::ProcessMessages()` 并没有用这条经过测试的解析路径，而是在 live socket 上手写了另一套 framing：先读 4 字节 `PacketSize`，若 `PacketSize <= 0 || > 1024 * 1024` 仅执行 `break` 退出当前 `while (HasPendingData)`，既不关闭连接，也不清空剩余缓冲，更不会回退到 `TryDeserializeDebugMessageEnvelope()`。这意味着一个客户端只要发送“非法长度头 + 任意后续字节”，服务器就会先消费掉头部 4 字节，再把后续 payload 残留在 socket 缓冲里；下一轮读取时，这些残留字节会被当成新的 `PacketSize` 继续解释，整个 TCP 流从此失去边界同步。 |
| 根因 | transport 层存在两套独立实现：测试覆盖的是 buffer-based envelope parser，生产收包路径却复制了一份简化版逻辑，且没有同步 parser 的错误处理和缓冲策略。 |
| 影响 | 真实调试连接上，invalid-length/truncated 包的行为与单测结论不一致。坏客户端不需要触发死循环，只靠一次非法头部就能让该连接后续所有消息进入错位解析或静默丢弃状态，服务器还不会给出明确错误。由于当前自动化完全建立在 `TryDeserializeDebugMessageEnvelope()` 上，而不是 `ProcessMessages()` 的 live socket 路径，这类协议健壮性回归不会被测试发现。 |

### 发现 55：watch/locals 求值会自动执行用户 getter，单纯展开变量面板就可能触发脚本副作用

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:1081-1128, 2692-2820`；`AngelscriptType.cpp:639-714`；`AngelscriptSettings.h:203-219`；`Bind_BlueprintType.cpp:418-445, 520-540, 572-576`；`Bind_UStruct.cpp:480-503, 519-540, 606-612`；`AngelscriptDebuggerTestClient.cpp:296-309` |
| 描述 | `RequestVariables` / `RequestEvaluate` 最终都会走到 `GetDebuggerValue()` / `GetDebuggerScope()`。对象与结构体 scope 枚举时，不只读取真实字段，还会扫描所有 zero-arg、`IsReadOnly()` 且名字以 `Get` 开头的方法，并通过 `GetDebuggerValueFromFunction()` 自动求值后把结果作为 `Foo$` 成员塞进 scope。更关键的是 `GetDebuggerValueFromFunction()` 不是纯反射读取，而是显式 `PrepareAngelscriptContextWithLog(...)` 后执行 `Context->Execute()` 真跑一次脚本/native getter。显式成员求值路径同样支持 `Member()`、`Member$` 和自动拼出的 `GetMember`。配置层虽然提供了 `DebuggerBlacklistAutomaticFunctionEvaluation`，但默认的无条件黑名单是空集合，只有 `...WithoutWorldContext` 里硬编码了 4 个 `AActor` getter，因此绝大多数 getter 会在调试观察时被自动执行。 |
| 根因 | 调试器把“便捷展示 getter 派生属性”实现成了真实函数执行，并且采用默认允许、靠 blacklist 事后屏蔽的策略，而不是默认只读字段、显式请求才执行函数。 |
| 影响 | 这破坏了调试观察应尽量无副作用的基本预期。用户只要展开 `this`、locals 或 watch，就可能触发脚本/native getter 内的昂贵计算、日志、计时器访问、世界查询，甚至任何 getter 自身包含的状态修改与异常路径。由于仓库里的 `RequestEvaluate` / `RequestVariables` 在测试树中只出现于 test client helper 定义，当前没有任何自动化用例覆盖“getter 求值不会带来副作用或异常”的场景，所以这类调试期行为偏差完全靠人工规避。 |

### 发现 56：getter 求值完全忽略 `Execute()` 结果，异常或中止会被伪装成“成功返回了一个值”

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/angelscript.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptTest.cpp` |
| 行号 | `AngelscriptType.cpp:704-735`；`AngelscriptDebugServer.cpp:440-445`；`angelscript.h:298-305, 974`；`AngelscriptTest.cpp:364-367` |
| 描述 | `GetDebuggerValueFromFunction()` 在 `PrepareAngelscriptContextWithLog(...)` 成功后直接调用 `Context->Execute()`，但完全不检查返回的执行状态；紧接着无论脚本是 `asEXECUTION_FINISHED`、`asEXECUTION_EXCEPTION`、`asEXECUTION_ABORTED` 还是 `asEXECUTION_ERROR`，它都会继续读取返回值寄存器并尝试格式化成 `FDebuggerValue`。仓库里别的执行路径并不是这样处理的，例如 `AngelscriptTest.cpp` 会显式 `switch (Execute())` 并区分 `EXCEPTION` / `ERROR`。更糟的是，如果 getter 在调试暂停态里抛异常，`DebugServer::ProcessException()` 发现 `bIsPaused` 为真后会立刻 `return`，不会向客户端发新的异常停止事件。结果是自动 getter 求值失败时，前端既拿不到一个明确错误，也不会看到新的 exception stop，而是可能收到一个基于默认初始化或残留返回寄存器拼出来的“正常值”。 |
| 根因 | 调试求值路径把 getter 执行当成普通辅助步骤，遗漏了对 `asIScriptContext::Execute()` 结果的状态机处理，同时暂停态异常处理又被 `ProcessException()` 的早退逻辑吞掉。 |
| 影响 | 调试器会在最不可信的时候给出最像真的结果：getter 实际已经异常或被中止，watch/evaluate 面板却可能显示一个默认值、旧值或空值，让用户误以为求值成功。这不仅掩盖真实异常，也会让自动 getter 求值与显式运行脚本的语义分叉。当前仓库没有任何 `RequestEvaluate` / `RequestVariables` 集成测试覆盖 getter 抛异常或返回失败状态时的协议行为，因此该问题不会被回归测试捕获。 |

---

## 分析 (2026-04-08 10:58)

### 发现 57：`BreakOptions` 直接信任网络包里的 `FilterCount`，恶意请求可在反序列化阶段触发超大分配或断言

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:466-480`；`AngelscriptDebugServer.cpp:777-778, 1137-1145`；`AngelscriptDebugProtocolTests.cpp:217-245`；`AngelscriptDebuggerSmokeTests.cpp:90-123` |
| 描述 | `HandleMessage()` 处理 `BreakOptions` 时，会把客户端 datagram 直接反序列化为 `FAngelscriptBreakOptions`。该结构的 `operator<<` 先从 archive 读一个 `int32 FilterCount`，随后在加载分支立即执行 `Msg.Filters.SetNum(FilterCount)`，没有任何 `FilterCount < 0`、上限值、剩余 payload 长度或“是否属于已公布 filter 列表”的校验。`ProcessMessages()` 虽然把整包长度限制在 `<= 1MB`，但这个限制并不能约束 `FilterCount` 本身；攻击者仍可在一个合法长度的包里写入畸形计数，让服务器先按该计数扩容，再去读实际并不存在的字符串数组。 |
| 根因 | 调试协议的手写反序列化把来自 socket 的计数字段当成可信输入，先分配容器再验证内容；`BreakOptions` 路径也没有复用任何带边界检查的解析助手或白名单过滤。 |
| 影响 | 任意能连上调试端口的客户端都可以用一条畸形 `BreakOptions` 消息把服务器带进超大内存分配、负长度断言或后续反序列化崩溃路径，形成明确的拒绝服务面。当前自动化没有覆盖这条真实接收路径：协议测试只验证 `BreakFilters`/`DatabaseSettings` 等同版本 round-trip，smoke test 也只请求 `BreakFilters`，没有任何 malformed `BreakOptions` 负向用例。 |

### 发现 58：同一客户端重复发送 `RequestDebugDatabase` 会被重复订阅，后续数据库与资产流会按重复次数多次下发

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:822-826, 1485-1490, 2037-2049, 2095-2147, 2173-2203` |
| 描述 | `HandleMessage(RequestDebugDatabase)` 每收到一次请求就执行一次 `ClientsThatWantDebugDatabase.Add(Client)`，没有去重或“已订阅”检查。之后 `BroadcastDebugDatabase()`、`OnAssetAdded/Removed/Renamed()`、`OnFilesLoaded()` 都直接遍历这个 `TArray`，并对每个元素分别调用 `SendDebugDatabase()` 或 `SendAssetDatabase()`。而这两个发送函数本身都会下发完整的 `DebugDatabaseFinished` / `AssetDatabaseInit` / `AssetDatabaseFinished` 流程。结果是同一 socket 只要因为重连逻辑、超时重试或前端误重发而重复请求一次 database，就会在后续每次全量同步和每次资产变更中收到 N 份重复消息。 |
| 根因 | debug database 订阅状态被建模成允许重复元素的 `TArray<FSocket*>`，但请求语义实际更接近幂等的“打开订阅”；实现没有使用 `AddUnique`、set 语义或 request token。 |
| 影响 | 客户端会看到重复的 `AssetDatabaseInit/Finished` 边界和重复资产条目，若前端把这些消息当成一条线性流处理，就可能造成资产列表抖动、重复增量应用或不必要的全量重建。仓库里对 `AngelscriptRuntime/Tests` 与 `AngelscriptTest` 的检索没有发现任何 `RequestDebugDatabase` / `AssetDatabase` 集成测试，因此这种重复订阅行为目前没有自动化护栏。 |

### 发现 59：precompiled cache 装载只检查 `BuildIdentifier`，引用损坏或半兼容缓存会在后续 `checkf` 中崩溃而不是安全回退

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `AngelscriptEngine.cpp:1513-1556, 3821-3824`；`PrecompiledData.h:587-618, 651-660`；`PrecompiledData.cpp:1732-1805, 1868-1869, 1972-2142, 2180-2250, 2642-2644, 2678-2689`；`AngelscriptPrecompiledDataTests.cpp:13-63` |
| 描述 | 引擎在 `bUsePrecompiledData` 路径下只要发现 cache 文件存在，就会创建 `FAngelscriptPrecompiledData` 并直接 `Load(Filename)`。`Load()` 本身只是 `LoadFileToArray()` 后执行一次 `Reader << *this`；入口处唯一显式有效性检查是后面的 `IsValidForCurrentBuild()`，它只比较 `BuildIdentifier`。但后续真正使用这些数据时，`GetTypeInfo()`、`GetTypeId()`、`GetFunction()`、`GetFunctionId()`、`GetGlobalVariable()` 等引用恢复函数遇到任何“引用不存在”“模块/类型/函数找不到”的情况都会直接 `checkf`。这意味着一个 build id 相同、但内容被截断、局部损坏、旧缓存残留了过期引用，或与当前脚本资产只部分兼容的 cache，并不会在装载阶段被判定为“不可用然后回退”，而是会在模块实例化或 `PrepareToFinalizePrecompiledModules()` 的引用解析中直接终止。 |
| 根因 | precompiled data 的入口校验口径过窄，只验证了构建配置，没有验证 archive 是否完整、引用表是否自洽、或关键符号是否仍可解析；错误处理依赖大量 fatal `checkf`，而不是“标记 cache 无效并退回普通编译/VM”的降级路径。 |
| 影响 | 当前 fallback 安全性只覆盖了“文件不存在”或“build configuration 不匹配”这类最外层情况，无法覆盖更常见的 cache 损坏、半更新或引用漂移。结果是在 packaged/precompiled 场景里，问题缓存不是被忽略，而是可能在启动或首轮模块恢复时直接触发断言。测试层也没有针对这类入口做保护：`AngelscriptPrecompiledDataTests.cpp` 目前只有两个结构性回归用例，没有任何损坏文件、截断 archive 或缺失引用时应回退的负向测试。 |

### 发现 60：`ClearAllAngelscriptDataBreakpointsFromHandler()` 只改 active 状态不重写 debug registers，应急清除后硬件断点仍可能继续触发

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:318-340, 356-391, 479-545, 1241-1282` |
| 描述 | `ClearAllAngelscriptDataBreakpointsFromHandler()` 只是把全局标志 `GClearDataBreakpoints` 设为 `true`。真正进入 `DebugRegisterExceptionHandler()` 后，这条分支只会把 `ActiveDataBreakpoints[i].bTriggered` 置回 `false`、把 `Status` 改成 `Remove_ReachedHitCount`，然后直接把 `bBreakNextScriptLine` 清成 `false`。问题在于本轮异常里真正写回 CPU `Dr0-Dr7` 的 `ApplyBreakpointsToThreadContext(...)` 已经在更早的 `if (bBreakpointTriggered)` 分支执行完毕；`GClearDataBreakpoints` 分支之后没有第二次重写 `ExceptionInfo->ContextRecord`，也没有调用 `UpdateDataBreakpoints()`。随后常规清理链 `ProcessScriptLine()` 又明确要求 `DataBreakpoints.Num() > 0 && bBreakNextScriptLine` 才会先 `SyncActiveDataBreakpointsToAuthoritativeState()`、再发送 `ClearDataBreakpoints` 并 `UpdateDataBreakpoints()`，而这里恰好把 `bBreakNextScriptLine` 关掉了。结果是这个“立即窗口里用来退出 spamming breakpoint”的应急入口，只修改了 active 数组里的软件状态，却没有保证当前线程的硬件 debug registers 被真正清空，也没有把删除状态同步回权威 `DataBreakpoints`。 |
| 根因 | handler 内的应急清除逻辑只覆盖了 `ActiveDataBreakpoints` 内存状态，没有和正常 data breakpoint 生命周期复用同一条“同步权威数组 -> 重建 active 集 -> 重写 debug registers -> 向客户端广播清除”的闭环；同时它还主动关闭了触发后唯一会做这件事的 `bBreakNextScriptLine` 路径。 |
| 影响 | 这条紧急逃生口在最需要可靠的时候反而可能失效：用户以为自己已经从 debugger immediate window 清掉了正在 spam 的 data breakpoint，但线程上的硬件断点位仍可能继续工作，后续写入继续触发 `EXCEPTION_SINGLE_STEP`，而客户端也收不到对应的 `ClearDataBreakpoints` 状态同步。仓库里对 `Runtime/Tests` 和 `AngelscriptTest` 的检索没有发现任何 `ClearAllAngelscriptDataBreakpointsFromHandler` 或 `GClearDataBreakpoints` 覆盖，因此这个 Windows 专属清除路径目前完全没有自动化保护。 |

### 发现 61：fully precompiled 模式下的 `CodeHash` 校验是在拿 cache 对比 cache，磁盘脚本内容根本没有参与验证

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `AngelscriptEngine.cpp:2046-2052, 4283-4303`；`PrecompiledData.cpp:1417-1421, 2752-2929`；`AngelscriptPreprocessor.cpp:295-301`；`AngelscriptPrecompiledDataTests.cpp:13-63` |
| 描述 | fully precompiled 路径下，引擎在 `InitializeModules` 早期一旦命中 `PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode`，就直接 `bUsedPrecompiledDataForPreprocessor = true`，并用 `PrecompiledData->GetModulesToCompile()` 取代正常 preprocessor。`GetModulesToCompile()` 重新构造每个 `FAngelscriptModuleDesc` 时，会把 cache 里保存的 `Module.CodeHash` 原样赋回 `ModuleDesc->CodeHash`，但这一整条路径没有重新读取脚本文件，也没有像 preprocessor 那样根据 `File.ProcessedCode` 重新计算 hash。全仓 grep 显示，`Module->CodeHash` 来自真实源码内容的计算只发生在 `AngelscriptPreprocessor.cpp:295-301`。更关键的是，cache 生成期 `FAngelscriptPrecompiledModule::InitFrom()` 又会把当时的 `Context.ModuleDesc->CodeHash` 存进 precompiled data。结果到了后面的 `CompileModule_Properties_Stage1()`，代码虽然写着“Check if file content hashes are the same or not”，实际比较的却是 `CompiledModule->CodeHash == Module->CodeHash`，而这两个值在 fully precompiled 模式下都来自同一份 cache。 |
| 根因 | precompiled data 不只缓存了 bytecode，还缓存并回放了 preprocessor 产出的模块描述；但引擎仍保留了一个看似针对“当前磁盘源码”的 `CodeHash` 守卫，实际上这个守卫在 fully precompiled 分支里没有任何来自磁盘或当前脚本处理结果的输入，因此退化成了自我比较。 |
| 影响 | 这意味着只要旧的 `PrecompiledScript*.Cache` 还在，且外层 `BuildIdentifier` 没把它拦下，非 development 模式下的引擎就可能完全绕过当前脚本文件，继续实例化旧 cache 里的模块描述和脚本代码；源码已经修改、删除或新增的情况不会被这条 `CodeHash` 检查发现。现有 `AngelscriptPrecompiledDataTests.cpp` 仍只有结构 round-trip / module diff 两个小用例，没有任何“磁盘脚本已变但 fully precompiled 模式应拒绝旧 cache”的集成测试，所以这条最关键的真实性校验目前没有自动化护栏。 |

---

## 分析 (2026-04-08 11:22)

### 发现 62：JIT 的 `SCRIPT_CALL_NATIVE` fallback 没有复用解释器的 null object / WorldContext 安全检查，语义上不是安全 fallback

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:164-168, 988-1009`；`StaticJITHeader.h:350-351`；`StaticJITHeader.cpp:169-302`；`as_context.cpp:5152-5272`；`AngelscriptPrecompiledDataTests.cpp:13-63` |
| 描述 | `FScriptSystemCall::MakeCall()` 在不能走 fully native/custom/generic 路径时，会进入 `MakeDynamicCall()`，并通过 `SCRIPT_CALL_NATIVE(ScriptFunc, ...)` 调 `FStaticJITFunction::ScriptCallNative()`。这段代码注释写的是“fall back to calling the script VM for the system call”，但实际实现没有复用解释器的 `asCContext::CallFunctionCaller()` 语义：解释器版本在 `callConv >= ICC_THISCALL` 时会对 object pointer 做空指针检查，并在 `WITH_EDITOR` 下对 `asTRAIT_USES_WORLDCONTEXT` 执行 `BlueprintThreadSafe` / invalid world context 防护；JIT 版本的 `ScriptCallNative()` 既没有这两段 WorldContext 检查，也没有在 `sysFunc->callConv >= ICC_THISCALL` 的 caller path 上验证 `FunctionArgs[ArgIndex] != nullptr`。结果是同一类 system/native 调用，一旦落到 JIT 的 dynamic-call fallback，就会绕过解释器本来会抛出的脚本异常，直接把空 `this` 或无效 WorldContext 传进 native 代码。 |
| 根因 | StaticJIT 把“fallback 到 VM 语义”实现成了自写的 `ScriptCallNative()` bridge，而不是复用 AngelScript 解释器现有的 `CallFunctionCaller()`；复制出来的桥接代码遗漏了关键安全检查。 |
| 影响 | 这不是单纯的调试语义偏差，而是明确的安全/行为分叉：解释执行下本应得到 `TXT_NULL_POINTER_ACCESS` 或 WorldContext 错误的调用，在 JIT fallback 路径里可能直接进入 native method，轻则行为与解释器不一致，重则把 null `this` 传给 C++ 成员函数导致崩溃。由于仓库里和 StaticJIT 直接相关的自动化仍只有 `AngelscriptPrecompiledDataTests.cpp` 的两个结构用例，没有任何“JIT 覆盖函数经 dynamic native fallback 调用 null object / invalid WorldContext 时应抛脚本异常”的回归测试，这条 fallback 安全性缺口当前完全没有护栏。 |

### 发现 63：出站队列把“10 秒内未发出的首包”直接判成断线，慢客户端在全量 database/asset 同步时会被误踢

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:724-747, 1493-1517, 2159-2204, 2845-2859`；`AngelscriptDebugTransportTests.cpp:43-231`；`AngelscriptDebuggerSmokeTests.cpp:55-132` |
| 描述 | `TrySendingMessages()` 对队首消息第一次尝试发送时把 `FirstTry` 记成当前时间；只要后续 `Send()` 持续返回 `false`，消息就一直留在队首。`ProcessMessages()` 下一轮扫描客户端时，又把“`Queue[0].FirstTry < Now - 10s`”与 `GetConnectionState() != SCS_Connected` 并列当成移除条件，直接 `Close()` 并踢掉客户端。问题在于服务端本身会主动发送大批量消息：`SendDebugDatabase()` 会序列化整个 debug database JSON 并多次 `SendMessageToClient()`，`SendAssetDatabase()` 也会按 50 个 asset 一批循环下发 `AssetDatabase`。因此只要客户端读包稍慢、网络暂时背压、或者 database/asset 初始化阶段连续几秒都处于 `EWOULDBLOCK`，当前实现就会把“还活着但暂时发不出去”误判成必须断开的坏连接。 |
| 根因 | 发送队列只有一个固定 10 秒的首包年龄阈值，没有区分永久断线和可恢复的 socket backpressure；同时 database/asset 同步使用的也是同一条无流控的发送路径。 |
| 影响 | 调试器在最需要稳定长连接的时候反而容易被误踢，尤其是首次 `RequestDebugDatabase`、asset registry 全量同步或弱网络远端调试场景。客户端看到的表象会是随机掉线、database 初始化中途终止、后续 breakpoint/variables/stackTrace 请求全部失效。当前自动化没有覆盖这条真实行为链：`AngelscriptDebugTransportTests.cpp` 只测 envelope 序列化与入站解析，`AngelscriptDebuggerSmokeTests.cpp` 只验证单客户端握手，不包含任何“慢读客户端 + 大量出站消息”或 backpressure 负向用例。 |

### 发现 64：DebugServer 对已接受的 `FSocket*` 只做 `Close()` 不做 `DestroySocket()`，连接生命周期会累积泄漏 socket 资源

| 项目 | 内容 |
|------|------|
| 维度 | A / B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:395-399, 419-432, 724-747, 1191-1194`；`AngelscriptDebuggerTestClient.cpp:76-87` |
| 描述 | 服务端在 `HandleConnectionAccepted()` 中把 `ClientSocket` 作为裸 `FSocket*` 放进 `PendingClients`，之后整个类生命周期里所有断开路径都只调用 `Close()`：包括 `ProcessMessages()` 移除客户端时的 `Clients[ClientIndex]->Close()`、显式 `Disconnect` 消息分支里的 `Client->Close()`，以及析构函数只停止 `Listener` 而不遍历 `Clients` / `PendingClients` 做额外清理。全文件没有任何 `ISocketSubsystem::DestroySocket()` 或等价释放逻辑。相比之下，同仓测试客户端 `FAngelscriptDebuggerTestClient::Disconnect()` 在 `Close()` 之后还会显式 `SocketSubsystem->DestroySocket(Socket)`，说明当前代码库对 socket 对象本身的所有权释放并不等同于单纯 `Close()`。 |
| 根因 | DebugServer 用原始 `FSocket*` 管理已接受连接，但只处理了“关闭连接”语义，没有实现与之配套的对象销毁/所有权回收。 |
| 影响 | 只要发生连接建立、显式 `Disconnect`、发送超时移除或引擎析构，服务端就会留下未销毁的 socket 对象与其相关资源。长时间 editor 调试、反复重连或恶意连接抖动都可能把这条路径放大成稳定的内存/handle 泄漏，进一步增加调试端口的拒绝服务面。现有 debugger tests 虽然频繁执行 `SendDisconnect()` / `Disconnect()`，但全部断言都集中在功能行为，没有任何用例检查服务端 socket 资源是否被正确回收。 |

---

## 分析 (2026-04-08 11:36)

### 发现 65：fully precompiled 模式会让 `ReplaceAssetDefinition` 走进空 `DebugServer` 解引用，保存脚本字面量资源可直接崩溃

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 行号 | `AngelscriptEngine.cpp:1452-1455, 2028-2034`；`AngelscriptEditorModule.cpp:327-331` |
| 描述 | `FAngelscriptEngine` 只在 `(!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName()` 时创建 `DebugServer`。但 `ReplaceScriptAssetContent()` 在 `WITH_AS_DEBUGSERVER` 分支里无条件构造 `FAngelscriptReplaceAssetDefinition` 后直接执行 `DebugServer->SendMessageToAll(...)`，没有任何空指针检查。编辑器侧 `OnLiteralAssetSaved()` 在生成 `NewContent` 后会直接调用这条路径。结果是只要当前运行落在 fully precompiled 且非 development mode，保存一个 script literal asset 就会进入空 `DebugServer` 解引用。 |
| 根因 | `ReplaceAssetDefinition` 被实现成“只要编译期启用了 debug server 宏，就默认运行时一定有 `DebugServer` 实例”，但引擎初始化逻辑实际上会在 fully precompiled/JIT 交付模式下显式跳过 server 创建。 |
| 影响 | 这条路径把“关闭调试服务器”从一个功能降级变成了编辑器崩溃条件。项目一旦使用 fully precompiled scripts，保存脚本字面量资产就可能直接终止进程，而不是安全地跳过这条调试通知。当前我对 `AngelscriptRuntime/Tests` 与 `AngelscriptTest` 做检索，没有发现任何 `ReplaceScriptAssetContent` / `ReplaceAssetDefinition` 自动化覆盖，因此该回归不会被现有测试捕获。 |

### 发现 66：`BreakFilters` / `BreakOptions` 在当前插件源码里没有实现绑定，协议握手能成功但功能默认是空壳

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` |
| 行号 | `AngelscriptRuntimeModule.h:10-11, 33-34`；`AngelscriptRuntimeModule.cpp:48-56`；`AngelscriptDebugServer.cpp:652-664, 1137-1159`；`AngelscriptDebuggerSmokeTests.cpp:90-123` |
| 描述 | `BreakFilters` / `BreakOptions` 的核心实现完全依赖 `FAngelscriptRuntimeModule::GetDebugBreakFilters()` 和 `GetDebugCheckBreakOptions()` 返回的两个 delegate。`RequestBreakFilters` 只是 `ExecuteIfBound(FilterList)` 后把结果回包；`ShouldBreakOnActiveSide()` 也只在 delegate 已绑定时才执行过滤，否则直接 `return true`。与此同时，`AngelscriptRuntimeModule.cpp` 里这两个 getter 只返回进程级静态 delegate，当前我对 `Plugins/Angelscript/Source` 做检索，命中的只有 getter 定义和这两处消费点，没有任何 `.Bind...` / `Add...` 绑定实现。结果是在当前插件源码交付物里，调试握手确实能收到 `BreakFilters` 响应，但默认只能得到空列表，客户端发来的 `BreakOptions` 也不会改变实际停点行为。 |
| 根因 | break filter 能力被设计成“等待外部模块注入逻辑”的可选扩展点，但插件自身没有提供默认实现，也没有在协议层显式声明“当前不支持 filters”。 |
| 影响 | 前端会看到一个看似完整的能力面：`RequestBreakFilters` 有响应、`BreakOptions` 也能发送；但在当前仓库默认实现下，这套功能实际上既不给出可选 filters，也不会驱动真实的停点筛选，属于典型的协议空壳。测试同样没有兜底：`AngelscriptDebuggerSmokeTests.cpp` 只验证收到了可反序列化的 `BreakFilters` payload，没有断言 filter 非空或改变 breakpoint 行为；`AngelscriptDebugProtocolTests.cpp` 也仅覆盖纯 round-trip 序列化。 |

---

## 分析 (2026-04-08 12:09)

### 发现 67：现有 `StaticJIT` 自动化全部运行在 `EditorContext`，但 editor build 会把生成的 JIT 代码整段裁掉，真实 JIT 执行路径没有任何回归覆盖

| 项目 | 内容 |
|------|------|
| 维度 | D / C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `StaticJITConfig.h:8-10`；`AngelscriptStaticJIT.cpp:87-100, 3624-3627`；`AngelscriptPrecompiledDataTests.cpp:11-21` |
| 描述 | `StaticJITConfig.h` 在 `WITH_EDITOR` 下无条件定义 `AS_SKIP_JITTED_CODE`。生成器随后把每个 `.jit.cpp` 文件包在 `#ifndef AS_SKIP_JITTED_CODE` 里，意味着 editor build 虽然会产出和编译这些文件，但函数体内容在预处理阶段被整段剔除。与此同时，仓库里当前唯一直接覆盖 `StaticJIT` 的自动化 `AngelscriptPrecompiledDataTests.cpp` 只有两个 `EAutomationTestFlags::EditorContext` 用例。也就是说，现有自动化即使通过，也只验证了 precompiled-data 的结构读写，根本执行不到真实的 JIT 代码、JIT debug hooks 或 JIT fallback 路径。 |
| 根因 | 测试体系把 `StaticJIT` 回归全部放在 editor automation 下，而代码生成体系又把 editor build 视为“只生成、不执行 JIT 代码”的环境；两者叠加后，最关键的运行时路径天然失去覆盖。 |
| 影响 | 当前仓库对 `StaticJIT` 的自动化信号会高估真实质量：生成出的 C++ 是否能按预期执行、JIT 函数里的断点/步进桥接是否工作、dynamic/native fallback 是否保持语义，都不会被现有测试发现。这使得许多只在非 editor、真正加载 transpiled code 时才出现的问题只能靠人工或发布后暴露。 |

### 发现 68：`RequestEvaluate` 失败时，V2 响应会把未初始化栈数据当成 `ValueAddress` / `ValueSize` 发给客户端

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:416-438`；`AngelscriptDebugServer.cpp:1107-1128`；`AngelscriptDebugProtocolTests.cpp:121-170` |
| 描述 | `FAngelscriptVariable` 只有 `bHasMembers` 带默认初始化，`ValueAddress` 和 `ValueSize` 没有任何默认值。`HandleMessage(RequestEvaluate)` 里先构造一个局部 `FAngelscriptVariable Var;`，只有在 `GetDebuggerValue(...)` 成功时才给这两个字段赋值；表达式不存在、frame 无效或求值失败时，这个分支会直接跳过赋值，但仍然无条件 `SendMessageToClient(..., EDebugMessageType::Evaluate, Var)`。而 `FAngelscriptVariable::operator<<` 在 `DebugAdapterVersion >= 2` 时总会把 `ValueAddress` 和 `ValueSize` 写上 wire，所以失败的 evaluate 响应会把当前栈上的未初始化内容序列化给客户端。现有协议测试只覆盖了 V1/V2 成功 round-trip，没有任何“evaluate 失败时字段应为零或省略”的用例。 |
| 根因 | `RequestEvaluate` 把“空结果”建模成“发送一个默认构造的 `FAngelscriptVariable`”，但该结构体并不是全字段零初始化；同时 V2 序列化层又无条件输出 monitor address 字段。 |
| 影响 | 这是明确的协议与安全问题：前端在表达式求值失败时，可能收到随机的 `ValueAddress` / `ValueSize`，等价于把未定义栈内容暴露到调试连接上。轻则客户端看到随机 monitorable address，重则据此设置 data breakpoint 到任意垃圾地址，进一步诱发误报或不稳定行为。由于自动化没有失败路径覆盖，这类响应污染目前不会被测试捕获。 |

### 发现 69：DebugServer V2 协议没有任何 request token，`Evaluate` / `Variables` / `CallStack` 响应只能靠顺序猜测归属，不满足 DAP 请求关联语义

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 行号 | `AngelscriptDebugServer.h:86-90, 216-225, 416-450`；`AngelscriptDebugServer.cpp:924-927, 1081-1128, 1382-1482`；`AngelscriptDebuggerTestClient.cpp:183-218, 284-309` |
| 描述 | 协议 envelope 只有 `MessageType` 和原始 `Body`，没有 `seq`、`requestId`、`request_seq` 或任何等价字段。对应地，`RequestCallStack` 的请求体是空消息，返回的 `CallStack` 只有帧列表；`RequestVariables` 只发一个 path，返回的 `Variables` 只有变量数组；`RequestEvaluate` 只发 path + frame，返回的 `Evaluate` 只有值本身。服务端发送这些响应时也不会回显请求参数或 token。测试客户端的等待逻辑进一步证明了这一点：`WaitForMessageType(...)` 只能按“消息类型”匹配，无法验证同类型并发请求的归属。 |
| 根因 | DebugServer V2 把交互建模成“typed message stream”，没有采用 DAP 那样的 request/response 封包模型，也没有在自定义消息体里补一个最小 request token。 |
| 影响 | 只要前端出现同类型请求并发、重试、乱序发送，或在等待 `Evaluate`/`Variables`/`CallStack` 期间又穿插收到 `HasStopped`、`Diagnostics`、database/asset 推送，客户端就无法从协议本身判断某条响应对应的是哪一次用户操作，只能依赖单飞和 FIFO 假设。这直接限制了 DAP 适配层的实现空间，也解释了为什么像 `RequestCallStack` 这种请求一旦被延迟到下一次暂停就会天然错配。 |

---

## 分析 (2026-04-08 12:10)

### 发现 70：DebugServer 只有一个全局暂停态，停在一个线程时会静默吞掉其他线程的 breakpoint/exception

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:129-140, 597-600`；`AngelscriptDebugServer.cpp:440-446, 471-473, 667-698`；`AngelscriptDebuggerBreakpointTests.cpp:147-149` |
| 描述 | `FStoppedMessage` 只包含 `Reason` / `Description` / `Text`，没有任何 thread 标识；`FAngelscriptDebugServer` 自身也只维护一个全局 `bIsPaused`。命中停点后，`PauseExecution()` 会把这个全局布尔置为 `true` 并阻塞在循环里。此时如果别的脚本线程再触发异常或 line breakpoint，`ProcessException()` 会在 `if (bIsPaused)` 分支直接返回，源码里还留着 `Maybe we need to do something if an exception occurs while paused?` 注释；`ProcessScriptLine()` 也会在暂停态第一时间 `return`。结果是一个线程停住期间，其他线程新出现的 exception / breakpoint 根本不会进入协议。测试侧唯一和“暂停期间并发”相关的注释也只是验证后台调试客户端线程能发 `Continue` 解锁 `PauseExecution`，没有覆盖多个脚本 stop source 并发。 |
| 根因 | 调试服务器把 stop state 建模成进程级单例，而不是 `asIScriptContext*` / thread 级状态机；协议消息同样没有为 stop event 预留 thread 维度，所以实现层只能用一个全局暂停位短路所有后续事件。 |
| 影响 | 只要项目里存在并发脚本执行、异步任务回调，或者 data breakpoint / exception 从不同线程先后到达，前端就只能看到第一个 stop，后续 stop 会被静默丢失。这样不仅会让调用栈和实际运行状态脱节，也让协议无法映射到 DAP 的 thread-scoped stopped state 语义。 |

### 发现 71：`SetDataBreakpoints` 直接信任客户端提供的原始地址和 `bCppBreakpoint`，可对任意进程地址安装硬件写监视

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:300-326, 398-413`；`AngelscriptDebugServer.cpp:134-169, 172-203, 329-354, 1061-1068, 1275-1281`；`AngelscriptDebugProtocolTests.cpp:174-214` |
| 描述 | `FAngelscriptDataBreakpoint` 在协议层直接暴露 `Address`、`AddressSize`、`bCppBreakpoint` 和 `Name`。服务端收到 `SetDataBreakpoints` 后，只是 `DataBreakpoints = BP.Breakpoints; UpdateDataBreakpoints();`，没有任何“地址是否来自 `Evaluate`/`Variables` 返回值”“地址是否落在可监视脚本值范围内”“`AddressSize` 是否属于支持集合”的校验。随后 `ApplyBreakpointsToThreadContext(...)` 会把这些原始地址直接写进 `Dr0-Dr3`，异常处理器命中后又按客户端给的 `bCppBreakpoint` 决定是否执行 `UE_DEBUG_BREAK()`。这意味着任意已连接客户端都能请求在目标进程任意写地址上安装硬件 watchpoint，并把命中结果升级成原生 C++ 断下。现有自动化只验证该结构体 round-trip 后字段能原样保留，反而证明了当前测试把这些危险字段视为“合法 payload”，没有任何负向校验。 |
| 根因 | DebugServer V2 把 data breakpoint 能力建模成“客户端完全指定底层硬件监视参数”，而不是“服务器基于自身返回的 monitorable value/token 进行受限绑定”；运行时也没有在安装前做地址来源、地址范围和尺寸白名单验证。 |
| 影响 | 这条路径让 data breakpoint 从调试辅助功能退化成了一个原始进程内存监视接口。坏客户端既可以监视与脚本无关的全局/对象地址制造高频单步异常，也可以把 `bCppBreakpoint` 置为真，在附带 native debugger 的开发环境里把任意一次普通内存写入升级成 `UE_DEBUG_BREAK()`。从 DAP 角度看，这也明显偏离了 `setDataBreakpoints` 应建立在已验证变量目标上的安全模型。 |

---

## 分析 (2026-04-08 12:06)

### 发现 72：入站收包在 partial read 时反复覆写 buffer 开头，调试消息会被拼成损坏帧

| 项目 | 内容 |
|------|------|
| 维度 | A / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:756-789` |
| 描述 | `ProcessMessages()` 读取包头和包体时，虽然维护了 `BytesReceived`，但两次 `Client->Recv(...)` 都把目标地址固定写成 `Datagram->GetData()`，没有加上 `+ BytesReceived` 偏移。结果是只要 header 或 body 不是一次性收全，第二次及之后的 `Recv()` 就会把前一次已经收到的数据从 buffer 起始位置覆写掉，而 `BytesReceived` 仍继续累计。这样最终 `*Datagram << PacketSize` / `*Datagram << MessageType` 看到的就不是按 TCP 顺序拼接的完整报文，而是“末次片段覆盖前片段”的损坏数据。 |
| 根因 | 收包循环只把 `BytesReceived` 用作退出条件和剩余长度计算，没有把它用于写入偏移；实现默认假设 `Recv()` 每次都会从当前消息的起始位置返回全部剩余字节。 |
| 影响 | 调试客户端只要在网络分片、Nagle 合包或较大 payload 下触发 partial read，DebugServer 就可能把合法消息解码成错误的 `PacketSize`、`MessageType` 或消息体内容，表现为随机协议错位、断点/步进指令误解析，甚至后续进入已有文档记录的卡死和流失步边界问题。这说明当前协议完整性不仅在“收不到完整包”时有问题，在“分多次收到完整包”时同样不成立。 |

### 发现 73：running 态的 `Continue` 会直接抹掉挂起中的 `pause` / data breakpoint 停止事件

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:338-370, 479-545, 837-845`；`AngelscriptDebuggerSteppingTests.cpp:244-282, 376-387, 484-487, 585-587`；`AngelscriptDebugProtocolTests.cpp:174-214` |
| 描述 | data breakpoint handler 命中后并不会立即发 `HasStopped`，而是先把 `DebugServer->bBreakNextScriptLine` 置为 `true`，等待下一次 `ProcessScriptLine()` 再进入 `TriggeredBreakpoints` flush 和 `PauseExecution()`。普通 `Pause` 请求也是同样的“挂起到下一条脚本行”语义。与此同时，`HandleMessage(Continue)` 不管当前是否真的处于 stopped state，都会直接执行 `bIsPaused = false; bBreakNextScriptLine = false;`。这意味着只要客户端在目标尚未真正停住前提前发出一次 `Continue`，前面已经挂起的 pause/data-breakpoint stop 就会被静默清空，`TriggeredBreakpoints` 分支也不再有机会补发停止事件。 |
| 根因 | DebugServer 把“等待下一条脚本行再停下”和“已经处于 paused、等待 resume”复用了同一个全局标志 `bBreakNextScriptLine`，但 `Continue` 没有校验当前是否真的已停住，就会无条件清掉这个挂起标志。 |
| 影响 | 前端或旁路客户端只要出现一次时序提前、重发或 running 态误发 `Continue`，服务器就可能吞掉本应到达的 `pause` / data breakpoint stop，表现成“监视点明明命中却没停住”或“点了 pause 又自己继续跑”。现有自动化没有覆盖这条行为链：集成 stepping tests 只在收到 stop 后才发 `Continue`，协议 tests 也只验证 data breakpoint 结构 round-trip，没有任何“running-state continue 不应取消挂起 stop”的负向用例。 |

### 发现 74：`ClearDataBreakpoints` 协议声明支持按 `Ids` 选择性删除，但服务端入口会无条件清空全部监视点

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:275-290`；`AngelscriptDebugServer.cpp:1069-1072, 1222-1246`；`AngelscriptDebugProtocolTests.cpp:174-214` |
| 描述 | `FAngelscriptClearDataBreakpoints` 明确把消息体定义成 `Ids` 数组，序列化格式也逐项写入这些 id，说明协议表面语义是“按指定 id 删除部分 data breakpoint”。但 `HandleMessage(EDebugMessageType::ClearDataBreakpoints)` 完全不反序列化 body，直接调用 `ClearAllDataBreakpoints()`，后者会 `DataBreakpoints.Reset()` 并重建硬件断点状态。结果是客户端即使只想删除一个已命中的监视点，服务端也会把当前会话里的全部 data breakpoint 一次性清空。现有测试只覆盖 `FAngelscriptDataBreakpoints` 的 round-trip，没有任何 `ClearDataBreakpoints` 语义测试，因此这条协议分叉没有被自动化发现。 |
| 根因 | `ClearDataBreakpoints` 的消息结构和服务端处理逻辑没有保持一致：wire format 按“选择性删除”设计，运行时实现却只保留了“全量清空”这一条简化路径。 |
| 影响 | 这会让客户端无法安全实现 DAP 风格的逐项 data breakpoint 更新，只能退化成“每次全量重发”并承担状态闪断风险。更糟的是，一旦前端按 `Ids` 语义发送局部删除，用户看到的会是其它仍应保留的 watchpoint 也一起失效，属于明确的 breakpoint correctness 问题。 |

---

## 分析 (2026-04-08 12:23)

### 发现 75：`GetAngelscriptExecutionFileAndLine` 的 JIT 分支把空指针判定写反，脚本字面量元数据在 JIT 下不是崩溃就是丢失

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 行号 | `AngelscriptEngine.cpp:5632-5697`；`Bind_UObject.cpp:659-666` |
| 描述 | `GetAngelscriptExecutionPosition()` 在 `tld->activeExecution != nullptr` 时，会先取 `debugCallStack`，只有 `DebugStack != nullptr` 才读取 `Filename`/`LineNumber`。紧接着的 `GetAngelscriptExecutionFileAndLine()` 明显想复用同一套 JIT 逻辑，却把判定写成了 `if (DebugStack == nullptr)`，并在这个分支里直接解引用 `DebugStack->Filename` 和 `DebugStack->LineNumber`。结果是 JIT 执行下这条 API 出现双重错误：当 `debugCallStack` 真的为空时会立刻空指针解引用；当 `debugCallStack` 实际有效时又走不到读取分支，只会把 `OutFilename` 置空、`OutLineNumber` 置为 `-1`。`Bind_UObject.cpp` 在创建/更新字面量 asset redirector 元数据时直接调用这条函数并把结果写进 `ScriptAssetFilename` / `ScriptAssetLineNumber`，因此 JIT 场景下元数据要么触发崩溃，要么稳定写入空位置。 |
| 根因 | 同一文件里已经存在正确的 JIT 调试栈读取实现，但 `GetAngelscriptExecutionFileAndLine()` 手工复制时把 `DebugStack` 的空值条件写反，导致“有栈时不读、无栈时解引用”的镜像错误。 |
| 影响 | 这条问题直接破坏了 JIT 覆盖脚本的源码定位能力。轻则 script asset metadata 丢失，后续依赖 `ScriptAssetFilename` / `ScriptAssetLineNumber` 的定位链路失真；重则在 `debugCallStack` 尚未建立时直接崩溃编辑器/运行时。当前我对 `AngelscriptRuntime/Tests` 与 `AngelscriptTest` 的检索没有发现任何 `GetAngelscriptExecutionFileAndLine`、`ScriptAssetFilename` 或 `ScriptAssetLineNumber` 自动化覆盖，因此这条 JIT 专属分支目前没有测试护栏。 |

---

## 分析 (2026-04-08 12:23)

### 发现 76：JIT 执行里的 `DebugBreak` / `ensure` / `check` 无法进入 DebugServer 停止态，反而会直接落到 `UE_DEBUG_BREAK()`

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 行号 | `Bind_Debugging.cpp:14-28, 78-99, 101-121, 163-198`；`AngelscriptEngine.cpp:5718-5745`；`as_context.h:262-286`；`StaticJITConfig.h:8-10`；`AngelscriptDebuggerScriptFixture.cpp:173-200`；`AngelscriptDebuggerBreakpointTests.cpp:309-322` |
| 描述 | `ASDebugBreak()` 先调用 `FAngelscriptEngine::TryBreakpointAngelscriptDebugging()`，只有它返回 `true` 才会把事件送进 DebugServer；否则就直接执行 `UE_DEBUG_BREAK()`。`ensure` / `ensureAlways` / `check` 这些 bind 在条件失败时最终也都会走到同一个 `ASDebugBreak()`。问题在于 `TryBreakpointAngelscriptDebugging()` 只检查 `asGetActiveContext()`，一旦该值为空就直接 `return false`。而 JIT 执行入口 `FScriptExecution` 会在整个执行期间把 `tld->activeExecution` 设为当前执行、同时显式把 `tld->activeContext = nullptr`。这意味着脚本实际上仍在运行、JIT 调试栈也仍然存在，但 `DebugBreak()`、失败的 `ensure` 和 `check` 在 JIT 覆盖函数里永远进不了 `PauseExecution()`，只能落到原生 `UE_DEBUG_BREAK()`。 |
| 根因 | 断点绑定层与 JIT 线程上下文模型不一致：`GetAngelscriptExecutionPosition()` 等辅助函数已经学会从 `activeExecution` 读取 JIT 位置，但 `TryBreakpointAngelscriptDebugging()` 仍把“能否停到脚本调试器”建立在 `activeContext` 非空这个解释器前提上。 |
| 影响 | 在 DebugServer V2 会话中，JIT 覆盖脚本一旦显式调用 `DebugBreak()`，或命中 `ensure` / `check`，用户看到的不会是脚本调试器里的 `HasStopped` 事件，而是直接触发 native debugger break。这既破坏了 breakpoint correctness，也让所谓的 fallback 不再安全。测试层没有护栏：现有 debugger breakpoint tests 全是 `EditorContext`，而 `StaticJITConfig.h` 又在 `WITH_EDITOR` 下定义了 `AS_SKIP_JITTED_CODE`；同时 `TriggerDebugBreak` / `TriggerEnsure` / `TriggerCheck` 只存在于 fixture 定义里，没有对应的 JIT 集成用例。 |

---

## 分析 (2026-04-08 12:40)

### 发现 77：Windows data breakpoint 更新线程在 `GetThreadContext()` 之后才设置 `ContextFlags`，读取的寄存器集没有任何已验证前提

| 项目 | 内容 |
|------|------|
| 维度 | A / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:236-247` |
| 描述 | `FUpdateDebugRegisterThread::Run()` 先把整个 `CONTEXT` 清零，然后直接调用 `GetThreadContext(ThreadToDebug, &Context)`；直到调用成功返回后，才补写 `Context.ContextFlags = CONTEXT_DEBUG_REGISTERS`。这意味着真正读取线程上下文时，代码并没有先声明“我要取 debug registers”，后续却立刻假定 `Context.Dr7` 等寄存器内容已经有效，并在其基础上继续改写。源码里紧挨着还留有 `Doesn't seem like the data returned here is the same as the one we set` 的注释，说明这里的行为本身已经和作者预期不一致。 |
| 根因 | data breakpoint 编程线程把 `CONTEXT` 的读取前置条件放错了顺序：先取上下文、后声明所需寄存器集，导致 `GetThreadContext()` 的输入契约没有被满足。 |
| 影响 | 这会让 DebugServer V2 的 data breakpoint 安装路径缺少稳定前提。轻则 `GetThreadContext()` 失败直接放弃本次更新，重则后续 `Dr7` 初始化与 `ApplyBreakpointsToThreadContext()` 都建立在未定义或不完整的寄存器快照上，使 watchpoint 命中行为依赖平台/时序偶发成功，而不是确定性的协议语义。 |

### 发现 78：`Dr7` 初始化常量把注释要求清零的高位重新置 1，所有 Windows data breakpoint 更新都会写脏保留位

| 项目 | 内容 |
|------|------|
| 维度 | A / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:248-253` |
| 描述 | 同一段 `FUpdateDebugRegisterThread::Run()` 里，注释明确写着 `Bit 10 should be 1, Bit 14,15 and 32-63 should be 0`。代码先用 `Context.Dr7 &= 0x0000000000003F00` 保留 `8-13` 位，随后却执行 `Context.Dr7 |= 0x001000000000`。这个字面量不是设置 bit 10，而是把更高位重新置 1，和注释里“32-63 应该为 0”的目标正面冲突。也就是说，即使客户端只设置 1 个合法 data breakpoint，这条初始化路径仍会在每次更新时向 `Dr7` 写入额外高位。 |
| 根因 | Debug register 初始化把“bit 10 置 1”的掩码写成了错误常量，导致实现和同一段源码里的寄存器布局注释自相矛盾。 |
| 影响 | 这不是“第 5 个断点才会越界”的边界问题，而是所有 Windows data breakpoint 更新都会命中的基础配置错误。结果可能表现为 `SetThreadContext()` 失败、硬件监视点行为不稳定，或不同机器上对同一 `SetDataBreakpoints` 请求出现不可预测差异，从而直接破坏 data breakpoint 的 correctness 和 fallback 可预期性。 |

### 发现 79：真实加载 StaticJIT/transpiled code 的运行态不会启动 `DebugServer`，JIT 路径在架构上就无法被 DebugServer V2 调试

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h` |
| 行号 | `AngelscriptEngine.cpp:1425-1455, 1550-1602`；`StaticJITConfig.h:8-10` |
| 描述 | 引擎启动时把 `bUsePrecompiledData` 定义成 `!WITH_EDITOR && !bScriptDevelopmentMode` 条件下的运行态路径；随后 `DebugServer` 只在 `(!bUsePrecompiledData || bScriptDevelopmentMode)` 时创建。与此同时，真正执行 transpiled code 也依赖这条 precompiled/JIT 路径：运行时只有在加载了 `PrecompiledData` 后才会根据 `FJITDatabase::Get().Functions` 把 `bStaticJITTranspiledCodeLoaded` 置真；而 editor build 又被 `StaticJITConfig.h` 里的 `#if WITH_EDITOR / #define AS_SKIP_JITTED_CODE` 直接跳过 JIT 函数体。生成期虽然会创建 `StaticJIT` 并 `WriteOutputCode()`，但那条路径随后立刻 `RequestExitWithStatus()`。把这些条件拼起来，当前架构里不存在“既真的执行 JIT/transpiled code，又同时开着 DebugServer”的稳定运行态。 |
| 根因 | DebugServer 的生命周期被绑定到“非 precompiled 开发态”，而 StaticJIT 的可执行产物只在“非 editor 的 precompiled 运行态”里启用；两套条件在启动流程上被设计成互斥集合。 |
| 影响 | 这意味着 DebugServer V2 的协议、断点、步进和变量观察根本无法落到真实 JIT 覆盖函数上做端到端验证。前面文档里多条 “JIT 下不会发 exception/stackTrace/step” 的问题不只是实现缺陷，更是因为当前产品配置从根上禁止了用 DebugServer 去调试真正的 JIT 运行态；现有自动化也因此只能停留在 editor/non-JIT 环境，无法对目标场景建立回归护栏。 |

### 发现 80：`BreakOptions` 只影响普通 line breakpoint，异常与 data breakpoint 完全绕过过滤器

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:440-462, 479-545, 603-605, 1137-1145`；`AngelscriptDebuggerSmokeTests.cpp:90-123`；`AngelscriptDebugProtocolTests.cpp:217-226` |
| 描述 | `BreakOptions` 消息到达后，服务器只是把客户端传来的 filters 存进成员 `BreakOptions`。后续整个 `DebugServer` 里，唯一真正消费这组 filters 的地方是 line breakpoint 分支中的 `ShouldBreakOnActiveSide()`。相对地，`ProcessException()` 在发送 `HasStopped(reason="exception")` 前完全不看 `BreakOptions`；data breakpoint 命中后走的 `ProcessScriptLine()` 触发分支也不会经过 `ShouldBreakOnActiveSide()`。也就是说，即便上层未来真的绑定了 break filter delegate，这些 filters 仍然只能筛普通断点，无法筛异常停点和 data breakpoint 停点。当前测试同样没有覆盖这条语义：smoke test 只验证能收到 `BreakFilters` 响应，protocol test 只做 `BreakFilters` round-trip。 |
| 根因 | break filter 状态被建模成全局 `BreakOptions`，但运行时只在 line breakpoint 命中点接了一次过滤钩子，没有把同一套会话过滤语义贯穿到其它 stop source。 |
| 影响 | 前端如果把 `BreakFilters`/`BreakOptions` 当成一套统一的停点过滤能力，就会得到不一致结果：被过滤掉的一侧仍可能因为 exception 或 data breakpoint 停下，而普通 line breakpoint 却不会。这会直接破坏 breakpoint correctness，也让 DAP 适配层无法把一次 `setExceptionBreakpoints`/同类过滤配置稳定映射到所有 stopped event。 |

### 发现 81：停下事件完全丢失 breakpoint id，客户端无法知道究竟命中了哪一个 line/data breakpoint

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:129-142, 227-244, 300-326, 695-699`；`AngelscriptDebugServer.cpp:479-545, 602-608`；`AngelscriptDebuggerBreakpointTests.cpp:401-416` |
| 描述 | 协议层表面上支持 breakpoint id：`FAngelscriptBreakpoint` 和 `FAngelscriptDataBreakpoint` 都带 `Id` 字段。但服务器真正保存 line breakpoint 的结构 `FFileBreakpoints` 只有 `TSet<int32> Lines`，收到 `SetBreakpoint` 后会把断点退化成“这个文件这一行是否有断点”，id 在常驻状态里直接丢失。停止事件本身也没有承载位：`FStoppedMessage` 只有 `Reason` / `Description` / `Text` 三个字符串。运行时命中 line breakpoint 时只把 `Reason` 设成 `breakpoint`；命中 data breakpoint 时也只是拼一段 `Data breakpoint (...) triggered!` 文本后调用 `PauseExecution()`。当前集成测试同样只把 stop payload 反序列化成 `FStoppedMessage` 并断言 `Reason=="breakpoint"`，没有任何“命中的是哪个 breakpoint id”语义。 |
| 根因 | DebugServer 在状态层把 line breakpoint 压缩成“按行去重”的集合，又把所有 stopped event 统一序列化成不带 breakpoint 标识的 `FStoppedMessage`，导致请求侧分配的 id 没有回传通路。 |
| 影响 | 当前端同时维护多个 line breakpoint、data breakpoint、hit count 或需要把 stop 映射回某个 UI breakpoint 条目时，只能靠行号/文本猜测命中源，而不能依赖协议本身。这使 DAP 一类需要 `hitBreakpointIds`/等价语义的适配层无法精确实现，也会让多个断点落在同一行、或多个 data breakpoint 同时命中时的 UI 状态变得不可信。 |

---

## 分析 (2026-04-08 12:50)

### 发现 82：`POWd` / `POWdi` 明明生成了 JIT 代码却返回 `false`，任何 `double Pow` 都会在 codegen 末尾触发断言

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:6470-6498`；`AngelscriptStaticJIT.cpp:3330-3336` |
| 描述 | `asBC_POWd` 和 `asBC_POWdi` 的 `Implement()` 都已经写出了 `Math::Pow(...)` 生成代码，但函数末尾仍然返回 `false`。`FAngelscriptStaticJIT::GenerateCppCode()` 对每条 bytecode 都会执行 `bool bImplemented = Bytecode.Implement(Context); check(bImplemented);`，因此脚本一旦使用 `double Pow(double,double)` 或 `double Pow(double,int)`，JIT codegen 会在写完代码后立刻因为 `check(bImplemented)` 中断，而不是成功产出 JIT 或安全回退到 VM。 |
| 根因 | 这两个 bytecode handler 的实现状态与返回值自相矛盾：正文已经按“已支持”路径生成 C++，但收尾仍保留了“未实现”返回值。 |
| 影响 | 当前 JIT 覆盖范围不仅缺失 integer `Pow`，连 double 版本也实际上不可用；任何依赖浮点幂运算的脚本函数进入 StaticJIT 生成队列后都可能在生成期失败。对 `AngelscriptRuntime/Tests` 和 `AngelscriptTest` 的检索没有发现 `POWd` / `POWdi` 相关自动化，因此这条回归目前没有测试护栏。 |

### 发现 83：Windows data breakpoint 异常处理器会吞掉未被本插件确认的 `EXCEPTION_SINGLE_STEP`

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:279-379` |
| 描述 | `DebugRegisterExceptionHandler()` 只要看到 `ExceptionCode == EXCEPTION_SINGLE_STEP` 且 `Dr6 & 0xF` 非零，就会接管异常。问题在于即使循环里没有任何一个 `ActiveDataBreakpoint` 真正满足 `Status == Keep` 且触发命中，函数仍然会继续清空 `Dr6`、更新 `ContextFlags`，最后无条件 `return EXCEPTION_CONTINUE_EXECUTION`。也就是说，只要当前线程因为别的硬件断点、外部调试器或残留 debug register 状态触发了单步异常，而 `Dr6` 低四位有任意一位被置上，这个 handler 就会把异常吃掉，不再让后续 handler/调试器看到它。 |
| 根因 | 该 handler 把“`Dr6` 指出有硬件断点命中”直接等同于“这是 Angelscript 自己的数据断点”，缺少“至少一个本插件 watchpoint 被确认处理”的传播条件。 |
| 影响 | 这会破坏 data breakpoint fallback 的隔离性：只要 DebugServer 装上了 vectored exception handler，其他 native/hardware breakpoint 或单步调试就可能被静默拦截，表现为 IDE 断点偶发失效、外部调试器收不到单步异常、或线程在本应停下的位置直接继续执行。我对 `AngelscriptRuntime/Tests` 与 `AngelscriptTest` 的检索没有发现任何 `DebugRegisterExceptionHandler` / `EXCEPTION_SINGLE_STEP` 运行时覆盖，因此这条 Windows 专属问题目前没有自动化护栏。 |

### 发现 84：data breakpoint 命中发生在 C++ 写入时不会立即停下，若后续没有脚本行回调则停止事件会彻底丢失

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:329-340`；`AngelscriptDebugServer.cpp:479-545` |
| 描述 | Windows watchpoint handler 命中后，只会把 `Breakpoint.Context` 设成当前 `asGetActiveContext()` 结果，然后按注释所说把 Angelscript data breakpoint “deferred to the next script line”，即仅设置 `bBreakNextScriptLine = true`。真正发送 `HasStopped` 的逻辑并不在异常处理器里，而是等到后续某次 `ProcessScriptLine()` 进入 `if (DataBreakpoints.Num() > 0 && bBreakNextScriptLine)` 分支后，才根据 `Breakpoint.Context == nullptr` 生成 `Data breakpoint (...) triggered in C++!` 文本并调用 `PauseExecution()`。这意味着当写入发生在 native/C++ 路径、`asGetActiveContext()` 为空时，协议既不会在写入点立即停下，也无法保证一定还有“下一条脚本行”来兑现这次停止。 |
| 根因 | data breakpoint 命中被设计成“硬件异常里只记账，暂停动作统一借脚本 line callback 完成”，但 C++ 写入场景并不天然具备后续脚本 line callback。 |
| 影响 | 调试器会把真正的写入点延迟到一条无关的后续脚本行上，最坏情况下如果写入发生在脚本已经返回之后，`HasStopped` 会永远不发送，用户只看到监视点悄悄失效。对 `AngelscriptRuntime/Tests`、`AngelscriptTest/Debugger` 和 `AngelscriptTest/Shared` 的检索没有发现任何 data breakpoint 集成测试或 test client helper，因此当前没有自动化覆盖“C++ 写入应立即或至少可靠地产生停止事件”这条语义。 |
