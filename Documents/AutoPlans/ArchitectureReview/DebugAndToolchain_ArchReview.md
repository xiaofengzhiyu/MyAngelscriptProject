# DebugAndToolchain 架构与扩展性分析

---

## 架构分析 (2026-04-08 13:59)

### Arch-DT1：调试协议把 transport、session 与 capability 压进单一 Runtime 类，阻碍多 IDE 前端扩展

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试协议分层、多前端支持、远程调试扩展面 |
| 当前设计 | `DebugServer V2` 直接在 `FAngelscriptDebugServer` 中同时承担 raw TCP transport、消息编解码、调试会话状态、资产数据库广播和 Windows 硬件 data breakpoint 后端。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:20-23` 定义了进程级 `DebugAdapterVersion`；`.../AngelscriptDebugServer.h:25-80` 把 diagnostics、debug database、breakpoints、assets、create blueprint 都放进同一 `EDebugMessageType`；`.../AngelscriptDebugServer.cpp:52-108` 使用长度前缀二进制 envelope，而不是 DAP/CDP 这类标准 JSON 消息；`.../AngelscriptDebugServer.cpp:405-408` 用 `FTcpListener(FIPv4Address::Any, Port)` 监听所有网卡；`.../AngelscriptDebugServer.cpp:712-818` 在一个循环里同时管理 client 生命周期、收包、发包、`PingAlive` 和 callstack 请求；`.../AngelscriptDebugServer.cpp:897-913` 在 `StartDebugging` 时写入全局 `bIsDebugging` 与 `DebugAdapterVersion`；`.../AngelscriptDebugServer.h:433-437` 变量消息是否携带 `ValueAddress/ValueSize` 取决于这个全局版本号。 |
| 优点 | 当前实现已经具备远程 attach 的网络入口，且 `ClientsThatWantDebugDatabase` 与 `BroadcastDebugDatabase()` 允许多个只读消费者同时订阅符号/诊断流。 |
| 不足 | 多前端并不是按 session 隔离的，而是按进程共享状态：最后一个 `StartDebugging` 会覆盖 `DebugAdapterVersion`，`bIsPaused`、`BreakOptions`、断点集合和 data breakpoint 状态也都是全局单例；这使“一个 IDE 调试、另一个 IDE 仅浏览”勉强可行，但“多种 IDE 前端并存”与“标准协议桥接”都需要改动核心类，而不是新增 adapter。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 先抽象 `V8Inspector` / `V8InspectorChannel` 接口，再把消息直接转发给 `v8_inspector::V8InspectorSession`；对外暴露 `/json/list`、`/json/version` 和 `webSocketDebuggerUrl`，复用 Chrome DevTools Protocol 现成前端。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-73`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:60-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:315-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-589` | 把“Runtime 调试语义”和“前端协议”解耦，transport 可以换，前端可以复用标准工具。 |
| UnLua | 不内建 IDE transport，而是提供 `GetStackVariables`、`GetLuaCallStack`、`PrintCallStack` 这类 runtime 诊断原语；系统错误时直接遍历 Lua env 打印堆栈。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-721`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:190-205` | 先把“取变量/取堆栈”的模型做成可复用 API，再决定外部 IDE 如何消费，避免把 transport 绑死在核心运行时。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把现有 `DebugServer V2` 重构为“session 核心 + transport adapter + 平台断点后端”三层，同时保留现有 V2 协议作为默认兼容适配器。 |
| 具体步骤 | 1. 新增 `Debugging/AngelscriptDebugSession.h/.cpp`，把 `bIsPaused`、`BreakOptions`、断点集合、变量/调用栈查询、`GoToDefinition` 等从 `FAngelscriptDebugServer` 挪入 session 对象，并把 `DebugAdapterVersion` 改成每个 session 的 capability。 2. 保留当前二进制消息格式，但把 socket 收发和 `EDebugMessageType` 分发迁到 `Debugging/Transports/AngelscriptDebugTransportV2.h/.cpp`；`FAngelscriptDebugServer` 只负责 accept 和 transport 生命周期。 3. 新增 `IDebugProtocolAdapter` 抽象，先实现 `V2` adapter，后续再增量接入 `DAP` 或 `CDP bridge`，优先映射 `continue/step/stackTrace/scopes/variables/evaluate` 五组核心能力。 4. 把 `Windows` data breakpoint 逻辑从 `AngelscriptDebugServer.cpp` 拆到 `Debugging/Breakpoints/HardwareDataBreakpointBackend_Windows.cpp`，非 Windows 平台先返回“不支持 hardware watchpoint”，避免协议层继续依赖 vectored exception handler。 5. 在连接建立时增加 capability/discovery 包，明确声明 `remote attach`、`data breakpoint`、`asset database`、`create blueprint` 是否可用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugSession.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugTransportV2.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Breakpoints/HardwareDataBreakpointBackend_Windows.*` |
| 预估工作量 | L |
| 架构风险 | 需要重新梳理 `PauseExecution()`、`ProcessScriptLine()` 与 socket 生命周期的调用顺序；若 session 与 transport 边界切分不干净，容易引入“停住了但前端未收到事件”的时序回归。 |
| 兼容性 | 对现有脚本用户无行为变化；对现有 VS Code 扩展保持 `V2` transport 不变即可向后兼容。新 adapter 与 capability 包应作为增量添加，旧前端收到未知消息时也不应失败。 |
| 验证方式 | 1. 现有 `AngelscriptDebugProtocolTests.cpp` 保持通过。 2. 新增双客户端集成测试：一个客户端只订阅 `DebugDatabase`，另一个执行 `StartDebugging`/单步，验证 capability 不串台。 3. 局域网远程连接验证 `FIPv4Address::Any` 监听路径仍然可用。 4. Windows 上验证 hardware data breakpoint；非 Windows 上验证 capability 正确降级。 |

### Arch-DT2：StaticJIT 具备调用点级 fallback，但函数级覆盖率与工具链产物没有统一合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | StaticJIT 覆盖率、fallback 策略、JIT/UHT/文档/导航工具链合同 |
| 当前设计 | JIT 在“单个系统调用”层面有 `custom -> native -> pointer -> dynamic` 的回退阶梯，但在“整函数生成”层面仍假设所有选中的 bytecode 最终都可实现；与此同时，`PrecompiledData`、`AS_FunctionTable_*` 和 `Docs/angelscript/generated/*.hpp` 各自产生自己的工件与版本信息，没有统一 manifest。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp:18-33` 用 `UnimplementedBytecode` 作为缺省 opcode 占位；`.../AngelscriptStaticJIT.cpp:3204-3380` 在 `GenerateCppCode()` 中遍历所有指令并对 `Bytecode.Implement(Context)` 执行 `check(bImplemented)`；`.../AngelscriptBytecodes.cpp:109-168` 对系统调用按 `CustomCall`、`NativeCall`、`PointerCall`、`DynamicCall` 逐级退化；`.../StaticJITBinds.cpp:530-535`、`.../StaticJITBinds.cpp:703-707`、`.../StaticJITBinds.cpp:901-935` 说明 native/custom form 只在 `bGeneratePrecompiledData` 开启时登记；`.../PrecompiledData.cpp:2376-2458` 在运行时统一回填函数、系统函数指针、类型、全局变量和属性偏移，并可校验 property/type offset；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1550-1556` 只要 `PrecompiledDataGuid` 与编入二进制的 JIT 信息不一致，就整包禁用 transpiled code；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-79`、`...:166-206` 额外输出 `AS_FunctionTable_Summary.json` / CSV；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:675-755` 与 `.../AngelscriptEngine.cpp:2224-2227` 则通过 `dump-as-doc` 单独输出 `Docs/angelscript/generated/*.hpp`。 |
| 优点 | 调用点 fallback 很细：即使某个系统函数不能全 native，也还能退到 pointer/dynamic call；`PrecompiledData` 与 `PropertyOffset` 校验也让静态生成代码具备运行时重定位能力。 |
| 不足 | 目前“可不可以 JIT”缺少显式能力清单。UHT 已经能报告 `Direct/Stub/Skipped`，但 JIT 无法报告“哪些函数是 fully-jitted、哪些只是 hybrid fallback、哪些根本 interpreter-only”；一旦函数被加入 `FunctionsToGenerate`，不支持的 bytecode 更像生成期断言而不是受控降级。更重要的是，UHT summary、JIT `DataGuid`、文档 `.hpp` 三套产物之间没有共同版本号，IDE/CI 很难判断某次构建到底更新了哪一层事实。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | Editor 常驻生成器与 commandlet 共用同一种 IntelliSense 工件目录和文件格式：Editor 监听资产事件增量写入，命令行则全量重建到同一 `Intermediate/IntelliSense`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:143-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:29-132` | Editor 增量链和批处理链共享统一 artifact contract，CI 与本地 IDE 不需要各自猜测输出格式。 |
| puerts | Inspector 不是“启动一个私有 socket 就完了”，而是同时暴露 `/json/list`、`/json/version` 和 `webSocketDebuggerUrl` 这类自描述入口，让工具先发现能力再连接协议会话。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:330-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477` | 工具链入口应先有一个可发现、可校验、可复用的 manifest，再谈具体前端如何消费。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `StaticJIT + UHT + Docs + DebugDatabase` 建立统一的 `ToolchainManifest`，同时把 JIT 覆盖率从“断言失败”升级为“可记录、可降级、可诊断”的函数级能力模型。 |
| 具体步骤 | 1. 在 `FAngelscriptBytecode` 或 `FStaticJITContext` 层新增 `CanImplement/FailureReason` 记录，把 `GenerateCppCode()` 中的 `check(bImplemented)` 改为分类：`FullyJitted`、`HybridFallback`、`InterpreterOnly`，并记录首个不支持 opcode 或外部引用原因。 2. 在 `AS_JITTED_CODE` 下新增 `AngelscriptJitManifest.json`，写入 `PrecompiledDataGuid`、每个 `FunctionId` 的 module、fallback mode、unsupported reason、是否使用 property offset relocation；继续保留 `AngelscriptJitInfo.jit.cpp` 供二进制注册。 3. 让 `AngelscriptUHTTool` 在生成 `AS_FunctionTable_Summary.json` / CSV 时，额外输出同 schema 的 `toolchain.uht` 节点，至少包含 `Direct/Stub/Skipped` 统计和 skipped reason。 4. 让 `FAngelscriptDocs::DumpDocumentation()` 在保留 `.hpp` 输出的同时生成轻量索引文件，或把文档输出路径与时间戳登记进同一个 manifest；首次只做附加文件，不改现有 `.hpp` 消费方。 5. 在 `FAngelscriptEngine` 载入 precompiled data 时，除现有 `PrecompiledDataGuid` 全局比对外，增加 manifest 级日志：明确是“整个 JIT 失效”还是“仅若干函数回退解释器”。 6. 后续再让 IDE 扩展或 `SendDebugDatabase()` 读取该 manifest，把“无定义跳转/JIT 未覆盖/UHT stub”显示成可诊断状态，而不是让用户只看到某处功能退化。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` |
| 预估工作量 | L |
| 架构风险 | 若过早把“unsupported opcode”直接静默回退，可能掩盖真正的生成缺陷；需要把 manifest 记录与日志质量先做足，避免把当前显式断言变成难追踪的性能退化。 |
| 兼容性 | 对脚本运行时语义应保持兼容，缺少 manifest 时回到现有行为即可；现有 `AS_FunctionTable_*`、`.hpp` 文档和 `AngelscriptJitInfo.jit.cpp` 都继续保留，新 manifest 仅作附加工件。 |
| 验证方式 | 1. 人工构造一个含未实现 opcode 的脚本函数，验证构建不崩溃且 manifest 正确标记为 `InterpreterOnly`。 2. 构造只触发 `DynamicCall` 的系统函数，验证 manifest 标记 `HybridFallback`。 3. 校验 `PrecompiledDataGuid` 不匹配时仍会禁用过期 JIT，并输出 manifest 对应诊断。 4. 校验 UHT summary、JIT manifest、Docs 索引在同一次构建中写出相同版本标识。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT1 | 调试协议分层、多 IDE 前端与远程调试扩展 | 结构性重构 + adapter 抽象 | 高 |
| P1 | Arch-DT2 | StaticJIT 覆盖率合同、fallback 可诊断性、工具链统一 manifest | 扩展点新增 + 构建管线收敛 | 高 |

---

## 架构分析 (2026-04-08 14:10)

### Arch-DT3：`DebugServer` 把调试会话与 Editor workspace 写操作绑进同一协议面，限制远程调试与多前端治理

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试域与 Editor/workspace 域的职责边界 |
| 当前设计 | `FAngelscriptDebugServer` 不只负责断点、单步、变量与调用栈，还承担 asset 浏览、asset 数据库推送、`CreateBlueprint`、脚本资产内容回写通知，以及 `GoToDefinition` 的 Editor 导航入口。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:25-80` 把 `FindAssets`、`AssetDatabase*`、`CreateBlueprint`、`ReplaceAssetDefinition` 与常规调试消息放进同一 `EDebugMessageType`；`.../AngelscriptDebugServer.h:581-692` 同一个类同时公开 `SendDebugDatabase()`、`SendAssetDatabase()`、`SendCallStack()` 和 `GoToDefinition()`；`.../AngelscriptDebugServer.cpp:1161-1190` 收到 `FindAssets`/`CreateBlueprint` 后直接广播 `FAngelscriptRuntimeModule::GetDebugListAssets()` 与 `GetEditorCreateBlueprint()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:19-21,45-47` 明确把这些能力定义成 Runtime 暴露给 Editor 的委托；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:396-409` 再把这些委托绑定到 `ShowAssetListPopup()` / `ShowCreateBlueprintPopup()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:2078-2203` 把 `AssetRegistry` 订阅、全量资产枚举和增量推送也做在 `DebugServer`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2028-2034` 还会把 `ReplaceAssetDefinition` 广播给全部已连接 client。 |
| 优点 | 当前 VS Code 类前端几乎只连一个 socket，就能同时拿到调试上下文、符号数据库、资产浏览和一部分 Editor 工作流能力，集成成本低。 |
| 不足 | 调试协议面天然变成“可调试 + 可浏览 + 可触发 Editor 行为”的复合 RPC。这样一来，headless/runtime-only 场景也要背负 Editor 导向的消息面；远程连接默认暴露的不是纯调试能力，而是带有 workspace 副作用的能力集合；未来要支持第二种 IDE 前端时，也必须一起兼容这些 Editor 行为，而不是只对接 `breakpoint/stack/variable` 核心语义。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | Inspector 层只定义 `V8Inspector` / `V8InspectorChannel` 抽象，并把 websocket 消息转发给 `v8_inspector::V8InspectorSession`；对外只暴露 `/json/list`、`/json/version` 和 websocket 调试入口，没有把 Editor 资产操作混入协议。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-73`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:104-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-539` | 把“调试协议”控制在 debug domain 内，workspace/Editor 行为不经由同一会话层承载。 |
| UnLua | Runtime 侧只提供 `GetStackVariables`、`GetLuaCallStack`、`PrintCallStack` 这类调试原语；Editor/IDE 相关的 IntelliSense 生成则放在 `UnLuaEditor` 模块，由资产事件与 commandlet 驱动。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:613-732`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:248-279` | 先把 runtime debug 能力沉淀成可复用 API，再让 Editor/tooling 单独订阅，职责边界更清晰。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留当前 `V2` socket 与消息号，但把“调试会话”与“workspace/editor 服务”拆成两个逻辑域，`DebugServer` 退回为编排者而不是一体化实现。 |
| 具体步骤 | 1. 新增 `Debugging/Services/IAngelscriptDebugSessionService.h`，收敛 `Pause/Continue/Step/CallStack/Variables/Evaluate/DataBreakpoints`；新增 `Toolchain/Workspace/IAngelscriptWorkspaceService.h`，收敛 `FindAssets`、`AssetDatabase`、`CreateBlueprint`、`ReplaceAssetDefinition`、`GoToDefinition`。 2. 将 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1161-1190` 和 `:2078-2203` 的 workspace 分支迁到独立 service；`FAngelscriptDebugServer` 只做消息分派，并在 `WITH_EDITOR` 且 service 可用时再转发。 3. 在 `RequestDebugDatabase` 连接建立后增加 capability 声明，至少区分 `supportsDebugSession`、`supportsWorkspaceBrowse`、`supportsBlueprintCreation`、`supportsSourceNavigation`、`supportsAssetWriteBack`；旧前端继续走默认值。 4. 对非 Editor / 远程部署默认关闭写操作 capability，只保留只读调试能力；如果旧 client 仍发来 `CreateBlueprint` 等消息，则返回 `Diagnostics` 提示“不支持当前 capability”，不直接失败断连。 5. 将 `ReplaceAssetDefinition` 从“调试会话广播”改成“workspace 增量事件”，保留旧消息号作为兼容 alias，一段时间内同时推送新旧路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Services/IAngelscriptDebugSessionService.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/Workspace/IAngelscriptWorkspaceService.h`，新增/调整 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 预估工作量 | M |
| 架构风险 | 现有前端可能默认假设“连上调试端口就一定能创建资产/拉资产列表”。如果 capability 宣告与兼容 alias 不同时落地，容易出现旧 client 静默失效。 |
| 兼容性 | 物理传输层和现有 `V2` 消息号可保持不变；拆分的是服务边界而不是立刻替换协议。旧前端仍可通过兼容分派运行，只是 capability 更明确。 |
| 验证方式 | 1. Editor 模式下验证旧前端仍可单步、查看变量、列资产并创建 Blueprint。 2. 非 Editor 或远程专用构建下验证仅保留调试能力，`CreateBlueprint`/`FindAssets` 收到 capability 降级提示。 3. 增加自动化测试，分别覆盖“仅 debug service 存在”和“两类 service 同时存在”的消息分派。 4. 验证 `ReplaceAssetDefinition` 兼容 alias 阶段不会重复触发前端写回。 |

### Arch-DT4：IDE 语义仍依赖 `WITH_EDITOR` 下的进程内 cache，缺少可被远程/命令行复用的持久化 symbol contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 文档、导航、DebugDatabase 与 UHT 产物是否共享同一份可持久化符号合同 |
| 当前设计 | 现在的 IDE 语义主要来自 `FAngelscriptDocs` 的进程内静态缓存；这些缓存只在 `WITH_EDITOR` 宏开启时由 bind 宏填充，而 `DebugServer` 与 `GoToDefinition` 再在运行时读取这份内存状态。与此同时，`UHTTool` 会落盘 `AS_FunctionTable_*` sidecar，但它并不输出同一套文档/导航 schema。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h:4-11` 中 `AS_DOC`、`SCRIPT_BIND_DOCUMENTATION`、`SCRIPT_GLOBAL_DOCUMENTATION` 在非 Editor 构建里直接变成空宏；`.../AngelscriptDocs.cpp:26-29` 用 `FunctionId`、`TypeId`、`GlobalVariableId`、`(TypeId, PropertyOffset)` 维护静态 `TMap`；`.../AngelscriptDocs.cpp:118-125` 的导航桥只有 `LookupAngelscriptFunction(int FunctionId) -> UFunction*`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1575-1617` 与 `:1751-1756` 在 `SendDebugDatabase()` 时直接从这些 cache 和 `UFunction` 取 `doc/keywords/meta/property doc`；`.../AngelscriptDebugServer.cpp:1335-1369` 的 `GoToDefinition()` 只有在 `LookupAngelscriptFunction()` 找到 `UFunction*` 或 UE 反射类型时才能走 `FSourceCodeNavigation`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:34-44` 其实已经具备按 `UASFunction` 的脚本源文件/行号打开文件的末端能力，说明缺的不是“最后一公里”，而是可复用的符号入口；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:675-755` 只会把结果落成 `Docs/angelscript/generated/*.hpp` 文本快照；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:528,2224-2227` 也说明这条导出链依赖 `dump-as-doc` 的 Editor 进程；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14-22,37-44,174-206,244-264` 则只输出 `ClassName/FunctionName/EntryKind/EraseMacro` 这类函数表 sidecar，没有同 schema 的 `doc/meta/source` 字段。 |
| 优点 | 在 Editor 内的 live session 里，`DebugDatabase` 读取的是当前已加载引擎状态，能较准确反映真实 reflection 与绑定结果，不容易读到过期磁盘缓存。 |
| 不足 | 远程 game/server 或 commandlet 场景下，很难复用同等级的 IDE 语义，因为 `WITH_EDITOR` 关闭后 bind-time 文档宏不再填充 cache；`UHTTool` 也无法补齐 `DebugDatabase` 需要的文档/导航字段。结果是：live debug、离线 docs、UHT sidecar 各自成立，但没有一份既能离线消费、又能在运行时增量覆盖的统一 symbol contract。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | Editor 常驻生成器与 commandlet 把 IntelliSense 工件都写到同一棵 `Intermediate/IntelliSense` 目录，文件名直接使用类型名，且只有内容变化时才覆写。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:47`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:162-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:25-27`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:117-133` | 把 IDE 语义先沉淀成可落盘、可复用的稳定工件，再由 Editor 与命令行共享，避免语义只活在某个进程内。 |
| puerts | Inspector 在真正进入 websocket 会话前，先通过 `/json/list`、`/json/version` 向工具公开机器可读的 discovery contract 和 `webSocketDebuggerUrl`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:330-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477` | 前端消费的不是“某些运行时内存刚好存在什么”，而是一份显式、可发现、可版本化的 contract。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `Docs/*.hpp`、`DebugDatabase` 和 `AS_FunctionTable_*` 的前提下，新增一份磁盘持久化的 `AngelscriptSymbolIndex`，把 Editor-only cache 变成可被 runtime、IDE、CI 共同复用的统一输入。 |
| 具体步骤 | 1. 新增 `Core/AngelscriptSymbolIndex.h/.cpp`，定义稳定 `SymbolId` 与记录结构，至少包含 `kind`、`module`、`namespace/type/member signature`、`doc`、`meta`、`sourcePath/sourceLine`、`uobjectPath`、`propertyOffset` 等字段；`FunctionId/TypeId` 只作为运行时回链字段，不再是唯一身份。 2. 让 `FAngelscriptDocs` 在保留现有 `TMap` 的同时，把 `AddUnrealDocumentation*()` 收敛到 `SymbolIndex` builder；`WITH_EDITOR` 仅影响“能否补充 `UFunction*`/metadata”，不影响稳定 `SymbolId` 的生成。 3. 让 `DumpDocumentation()` 在继续输出 `Docs/angelscript/generated/*.hpp` 的同时，追加写出 `Docs/angelscript/generated/AngelscriptSymbolIndex.json`；如果是 game/server 进程，则允许从上一次构建产物加载该 index。 4. 扩展 `SendDebugDatabase()`：优先从 `AngelscriptSymbolIndex` 取 `doc/meta/source`，Editor live session 再覆盖进程内更新值；同时给协议增加可选 `symbolId` 字段。 5. 扩展 `GoToDefinition()`：新前端优先传 `symbolId`，旧前端仍可传 `TypeName + SymbolName`；当没有 `UFunction*` 但 index 里有 `sourcePath/sourceLine` 时，直接走现有 `AngelscriptSourceCodeNavigation` 或 `OpenFile()` 末端。 6. 让 `AngelscriptUHTTool` 在 `AS_FunctionTable_Summary.json` / `AS_FunctionTable_Entries.csv` 中追加 `symbolId` 或输出并列 JSON，至少让函数表 sidecar 可以与 symbol index 做稳定 join。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSymbolIndex.*`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 预估工作量 | L |
| 架构风险 | `SymbolId` 的 canonical 规则一旦定义不稳，会在 overload、默认参数、namespace 重命名或热重载重建后造成索引漂移；需要先把 ID 生成规则固定住，再逐步接入更多消费者。 |
| 兼容性 | 现有 `.hpp` docs dump、`AS_FunctionTable_*` sidecar 和 `DebugServer V2` 字段都可以原样保留；新增的是附加 JSON 与可选 `symbolId` 字段。旧前端忽略新字段即可继续工作。 |
| 验证方式 | 1. 在 Editor 与 commandlet 两条链路分别生成 `AngelscriptSymbolIndex.json`，比对同一符号的 `SymbolId` 是否稳定。 2. 在非 Editor 运行时加载 index，验证 `SendDebugDatabase()` 仍能输出文档和 source 信息。 3. 构造仅有脚本源位置、没有 `UFunction*` 的符号，验证 `GoToDefinition()` 能通过 index 打开脚本文件。 4. 校验 `AS_FunctionTable_Entries.csv` 追加 `symbolId` 后，可以与 symbol index 做一对一 join。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT3 | 调试域与 Editor/workspace 域解耦 | 结构性重构 + capability 收敛 | 高 |
| P1 | Arch-DT4 | IDE 语义持久化、remote/headless 可复用的 symbol contract | 新增统一索引工件 + 渐进式协议扩展 | 高 |

---

## 架构分析 (2026-04-08 14:23)

### Arch-DT5：`DebugDatabase` / `AssetDatabase` 采用 game-thread 全量快照推送，缺少 revision 协商与异步背压

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 远程调试可扩展性、多前端同步成本、主线程负载 |
| 当前设计 | `DebugServer` 在 `FAngelscriptEngine::Tick()` 的主线程路径里同步处理 socket 收发；`RequestDebugDatabase` 会立刻重建并发送整份 `DebugDatabase`，随后再枚举并发送整份 `AssetDatabase`。后续刷新也不是 delta，而是对每个订阅客户端重复执行一次全量快照发送。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2833-2835` 每 tick 直接调用 `DebugServer->Tick()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:756-789` 在 `ProcessMessages()` 中循环 `Recv` 直到拿到完整包体；`.../AngelscriptDebugServer.cpp:822-826` 收到 `RequestDebugDatabase` 后立即 `SendDebugDatabase(Client)` 并补发 diagnostics；`.../AngelscriptDebugServer.cpp:1485-1490` 的 `BroadcastDebugDatabase()` 对每个订阅者都重新执行 `SendDebugDatabase()`；`.../AngelscriptDebugServer.cpp:1493-2049` 的 `SendDebugDatabase()` 每次都重新遍历类型、方法、全局变量和枚举并分块发送 JSON，最后只用 `DebugDatabaseFinished` 作为结束标记；`.../AngelscriptDebugServer.cpp:2049-2052` 在同一调用链末尾继续 `SendAssetDatabase(Client)` 并记录耗时；`.../AngelscriptDebugServer.cpp:2159-2203` 的 `SendAssetDatabase()` 每次都 `EnumerateAllAssets()` 发送全量快照；`.../AngelscriptDebugServer.cpp:2845-2859` 的 `TrySendingMessages()` 也在当前线程同步 `Client->Send()`。 |
| 优点 | 语义简单，客户端只要连上并发一次 `RequestDebugDatabase` 就能获得一致的符号、诊断和资产视图，不需要维护本地缓存协议。 |
| 不足 | 这套模型把“网络 I/O”、“快照序列化”和“全量扫描”都压在主线程 tick 上，而且没有 `revision`、`etag`、`if-none-match` 这类协商字段。项目规模变大或同时连接多个前端时，成本会按客户端数线性放大；远程调试和高延迟链路也更容易把编辑器/运行时 tick 直接拖慢。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | Inspector 先通过 `/json/list`、`/json/version` 暴露 discovery contract，再由 websocket 会话用 `dispatchProtocolMessage()` / `sendResponse()` / `sendNotification()` 处理增量消息，消息转发与 HTTP 发现入口都是显式分层的。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:104-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:150-157`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:330-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:529-540` | 把“发现/版本协商”和“会话消息流”拆开，前端先知道自己连接的是哪个 revision/capability，再决定是否拉全量数据。 |
| UnLua | IntelliSense 工具链不在 runtime 会话里全量回传，而是通过 Editor 资产事件增量更新落盘工件；写文件前还会先比较内容，只在变化时覆写。commandlet 与 Editor 共用同一输出目录。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:47-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:25-27`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:117-133` | 大体量 IDE 数据优先沉淀成可复用 artifact，并按变化增量刷新，避免每个客户端都触发一轮全量重建。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `DebugDatabase` / `AssetDatabase` 增加 immutable snapshot + revision 协商层，把主线程职责收敛成“交换快照指针和分发消息”，而不是“为每个客户端现算整份数据库”。 |
| 具体步骤 | 1. 新增 `Debugging/Toolchain/AngelscriptDebugSnapshotService.h/.cpp`，维护 `SymbolRevision`、`AssetRevision`、`DiagnosticsRevision` 与对应的不可变快照缓存；`BroadcastDebugDatabase()` 改为刷新 revision，而不是直接对每个 client 重跑 `SendDebugDatabase()`。 2. 扩展 `FAngelscriptRequestDebugDatabase`，增加可选 `KnownSymbolRevision` / `KnownAssetRevision` 字段；旧客户端不填则保持现有“全量快照”路径，新客户端可只请求缺失的部分。 3. 将 `SendDebugDatabase()` 的 JSON 构建和 `SendAssetDatabase()` 的全量枚举迁到后台任务，主线程只在快照完成后交换引用；保留现有 `DebugDatabase` / `AssetDatabase` 消息号，新增可选 `DebugDatabaseDelta` / `AssetDatabaseDelta` 作为增量扩展。 4. 把 `DebugDatabaseSettings.Version` 扩为真正的 capability/revision 握手，至少声明 `supportsDeltaSync`、`symbolRevision`、`assetRevision`、`diagnosticsRevision`。 5. 对 `TrySendingMessages()` 增加队列长度与大包统计，当客户端持续落后时优先丢弃过期的中间 snapshot，只保留最新 revision，避免 backlog 把旧数据一股脑推完。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Toolchain/AngelscriptDebugSnapshotService.*` |
| 预估工作量 | L |
| 架构风险 | 如果 snapshot 的失效条件定义不完整，前端会读到旧符号或旧资产列表；同时主线程和后台任务之间也要谨慎处理 UObject/AssetRegistry 的线程边界。 |
| 兼容性 | 现有 `V2` 客户端可以继续发送旧版 `RequestDebugDatabase`，服务端检测不到 revision 字段时直接走全量快照。新增 delta 消息和 capability 字段都应作为向后兼容扩展，不要求一次性升级前端。 |
| 验证方式 | 1. 构造两个 IDE 客户端同时订阅，验证 revision 相同时不会重复全量构建。 2. 在大型项目下记录 `SendDebugDatabase()` 前后主线程耗时，确认全量序列化已从 tick 热路径移出。 3. 远程高延迟链路下验证慢客户端只会收到最新 revision，而不是无限累积旧 snapshot。 4. 回归现有客户端，验证未携带 revision 的旧协议仍能完整获得 `DebugDatabase + AssetDatabase + Diagnostics`。 |

### Arch-DT6：StaticJIT 已按 module 产出文件，但有效性判断仍是单一全局 bundle，分片没有变成真实失效边界

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | StaticJIT 分片粒度、构建失效边界、回退范围 |
| 当前设计 | `StaticJIT` 在代码输出阶段会按 script module 生成 `*.as.jit.hpp` 与 shared header，并进一步拼成若干 `AngelscriptJitCode_*.jit.cpp` unity 文件；但编译进二进制的有效性信息只有一份全局 `FStaticJITCompiledInfo(PrecompiledDataGuid)`。这意味着“输出看起来是分片的”，而“可不可以启用”仍然是整包判断。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:66-75` 的 `CompileFunction()` 只把函数加入 `FunctionsToGenerate`，没有记录模块级 fingerprint；`.../AngelscriptStaticJIT.cpp:118-137` 的 `GetFile()` 会按 module 生成 `*.as.jit.hpp`；`.../AngelscriptStaticJIT.h:410-416` 中 `FGenerateFunction` 只保存符号名和声明，`FunctionsToGenerate` 也只是函数到生成数据的映射；`.../AngelscriptStaticJIT.cpp:3476-3497` 在 `WriteOutputCode()` 中先遍历全部 script modules / functions 再统一生成；`.../AngelscriptStaticJIT.cpp:3582-3677` 会输出每个 module 的 `.as.jit.hpp`，再按行数把它们合并成 `AngelscriptJitCode_%d.jit.cpp`；`.../AngelscriptStaticJIT.cpp:3683-3695` 最终只写出单个 `AngelscriptJitInfo.jit.cpp`，其中只包含一份 `PrecompiledData->DataGuid`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp:19-31` 还显式要求“Only one angelscript static JIT info can be compiled in!”；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1550-1555` 一旦这份全局 `PrecompiledDataGuid` 不匹配，就直接 `FJITDatabase::Get().Clear()`，清空整包 transpiled code。 |
| 优点 | 当前设计实现成本低，分片输出能够缓解单个超大翻译单元的编译压力；全局 GUID 也让“该不该启用静态 JIT”有一个非常直接的安全闸门。 |
| 不足 | 分片只存在于文件系统和 unity 组织层，不存在于运行时有效性层。任何一个 module 的 precompiled data 变化，都会导致整个 JIT bundle 被关闭；构建系统和 CI 也看不到“哪个 module 真的脏了、哪个 unity file 只是被顺带重编”。换句话说，当前 fallback 粒度不是“按 module/函数回退”，而是“按整个编译进二进制的 JIT 包回退”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense 输出天然按类型文件分片，Editor 与 commandlet 共用同一目录，而且只有文件内容变化时才覆写，分片既是输出组织方式，也是实际的增量边界。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:162-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:29-114` | 分片应当成为稳定的 artifact contract，而不只是最终再拼回一个“大包”。 |
| puerts | Inspector 的 discovery 数据把 `webSocketDebuggerUrl`、`Protocol-Version` 等 bundle 元信息先暴露给前端，再进入具体会话；消费者能先判断自己接入的是哪一个实例和协议版本。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:330-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:459-477` | 生成物或调试 bundle 需要先有可发现的元数据，工具链才能做选择性消费，而不是只能整包启停。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先保留现有全局 `PrecompiledDataGuid` 兜底，再旁路增加 `per-module / per-bundle` JIT manifest，把“文件分片”提升为“可诊断、可选择性失效”的真实边界。 |
| 具体步骤 | 1. 扩展 `FGenerateFunction` 与 `FJITFile`，在生成阶段记录 `ModuleName`、包含的 `FunctionId`、shared header 依赖、生成行数和输入摘要；`WriteOutputCode()` 同步写出 `AS_JITTED_CODE/AngelscriptJitModuleSummary.json`。 2. 新增 `FStaticJITCompiledBundleInfo` 注册结构，让每个 `AngelscriptJitCode_%d.jit.cpp` 或每个 module shard 都能在编译期注册自己的 `BundleGuid / ModuleNames / FunctionCount`；旧的 `FStaticJITCompiledInfo` 保留，继续提供全局兜底校验。 3. 在 runtime 比对时，若缺少新 bundle 元数据则继续走当前“全局 mismatch -> Clear()”路径；若 bundle 元数据齐全，则先标记不匹配的 module/bundle，并只对这些 bundle 禁用 JIT 注册，匹配的 bundle 继续可用。 4. 参考现有 `AngelscriptUHTTool` 的摘要思路，把 module/shard 统计输出到 CI，可直接看到“哪个 module 导致 JIT 重新生成、哪个 unity file 只是重排”。 5. 第二阶段再考虑把 bundle manifest 接到前端或日志里，让调试器能明确显示“当前函数为什么没走 JIT，是 bundle mismatch 还是函数自身 fallback”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 预估工作量 | L |
| 架构风险 | 如果 bundle 划分规则不稳定，unity file 重排就会造成 GUID 抖动，反而让缓存命中率更差；同时 runtime 也需要安全地支持“部分 bundle 不可用、其余 bundle 继续启用”的混合状态。 |
| 兼容性 | 第一阶段仅新增 manifest 和 bundle 注册信息，不改现有全局 GUID 语义；缺少新元数据时仍然保持当前全局回退行为，因此对已有脚本和二进制兼容。第二阶段再逐步启用“按 bundle 禁用”的更细粒度逻辑。 |
| 验证方式 | 1. 只修改单个 script module，验证 `AngelscriptJitModuleSummary.json` 只标记对应 module/bundle 变化。 2. 人工制造一个 bundle GUID mismatch，确认第一阶段仍按旧逻辑全局回退，第二阶段可只禁用不匹配 bundle。 3. 比较前后增量构建日志，验证 module/shard 摘要能解释为什么某些 `AngelscriptJitCode_*.jit.cpp` 被重编。 4. 回归运行时，确认混合状态下未命中的函数会回退解释器/动态调用，而不会破坏已有 JIT 函数。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT5 | 调试数据库同步模型、远程调试背压、主线程负载 | 结构性重构 + 协议扩展 | 高 |
| P1 | Arch-DT6 | StaticJIT 分片有效性、构建失效边界、bundle 级回退 | 构建管线收敛 + 渐进式 runtime 细化 | 高 |

---

## 架构分析 (2026-04-08 14:30)

### Arch-DT7：StaticJIT 已维护独立 debug callstack，但 `DebugServer` 仍主要消费解释器 `asCContext`，导致编译态执行与调试态观察分叉

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | JIT 执行态与调试执行态是否共用同一状态模型 |
| 当前设计 | `StaticJIT` 在 `Execution.debugCallStack` 上维护一套独立的 `FScopeJITDebugCallstack`，并提供 `GetAngelscriptExecutionPosition()` / `GetAngelscriptExecutionFileAndLine()` 之类 helper；但 `DebugServer` 的 `SendCallStack()`、`StepOver/StepOut`、变量路径解析仍然直接依赖 `asGetActiveContext()` 和 `Context->GetLineNumber()` 的解释器栈。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h:193-216` 定义了 `FScopeJITDebugCallstack` 并把 frame 链挂到 `Execution.debugCallStack`；`.../StaticJITHeader.h:337-340` 只有在 `AS_JIT_DEBUG_CALLSTACKS` 下才写入 `SCRIPT_DEBUG_CALLSTACK_FRAME` / `SCRIPT_DEBUG_CALLSTACK_LINE`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5634-5661` 用 `tld->activeExecution->debugCallStack` 获取当前 JIT 位置；`.../AngelscriptEngine.cpp:5665-5698` 又单独实现了 `GetAngelscriptExecutionFileAndLine()`，说明 JIT 位置信息存在旁路 helper；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:862-887` 的 `StepOver/StepOut` 直接以 `asGetActiveContext()` 计算停靠帧；`.../AngelscriptDebugServer.cpp:1441-1477` 的 `SendCallStack()` 也直接从 `Context->GetFunction(i)` / `Context->GetLineNumber(i, ...)` 组装协议栈；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp:104-119` 则表明 JIT 异常只通过 `FAngelscriptEngine::HandleExceptionFromJIT()` 走文本异常路径。 |
| 优点 | JIT 侧并不是完全“黑盒”：源码位置、函数名和 `ThisObject` 已经能在 `Execution.debugCallStack` 上被记录，为后续统一调试模型留下了切入点。 |
| 不足 | 当前前端看到的主调试模型仍是解释器栈，而不是“执行引擎无关的统一栈”。这会让 JIT 异常、JIT 单步、JIT 变量查看只能走旁路 helper 或文本日志，而不能自然落到 `CallStack` / `Variables` / `Evaluate` 主协议上。更直接的信号是 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5671-5675` 的 JIT file/line helper 只在 `DebugStack == nullptr` 分支里解引用 `DebugStack`，说明这条桥接路径并不在当前调试主链上持续受测。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | Inspector channel 直接连接到 `v8_inspector::V8InspectorSession`，websocket 收到的协议消息被原样派发到同一个 V8 runtime 上；pause message loop 也继续驱动同一个 inspector client，而不是再维护第二套“调试态栈”。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-116`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:282-308`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:572-589` | 标准协议之所以容易扩展，不只是因为用 CDP，而是因为“执行态”和“调试态”共享同一个 runtime truth。 |
| UnLua | `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 都直接基于 `lua_State*` 和 Lua 官方 debug API `lua_getstack` / `lua_getinfo` / `lua_getlocal` / `lua_getupvalue` 取数据，没有再造第二套旁路栈模型。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-669`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:672-730` | 先保证 callstack、locals、upvalues 都来自同一执行状态，再讨论 IDE 如何消费，扩展成本会明显更低。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把解释器 `asCContext` 栈与 JIT `Execution.debugCallStack` 收敛到统一的 `ExecutionStateProvider` 抽象里，先统一 callstack/source location，再增量补变量与单步。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/IAngelscriptExecutionStateProvider.h`，定义 `TryGetCallStack()`、`TryGetCurrentLocation()`、`TryGetThisObject()`、`TryGetTopFrameVariables()` 等接口。 2. 先实现 `InterpreterExecutionStateProvider`，把当前 `SendCallStack()`、`StepOver/StepOut` 和变量解析逻辑搬进 provider；再实现 `JitExecutionStateProvider`，读取 `tld->activeExecution->debugCallStack` 与 `FScopeJITDebugCallstack`。 3. 将 `FAngelscriptDebugServer::SendCallStack()` 和 `TryBreakpointAngelscriptDebugging()` 改为优先查询 provider；当 provider 判断当前处于 JIT 执行且只能提供 top frame 时，仍沿用现有 `V2` `CallStack` shape，但通过 diagnostics/capability 明确声明 `supportsJitDeepVariables=false`。 4. 把 `AS_JIT_DEBUG_CALLSTACKS` 从“编译时默默决定是否有调试帧”升级为显式 capability，并在 Debug build 默认打开；如果 Shipping 仍需关闭，也要让前端能收到 `supportsJitCallstack=false`。 5. 修复并补测 `GetAngelscriptExecutionFileAndLine()` 的 JIT 分支，确保 helper 与 provider 共用同一实现，避免继续出现旁路逻辑漂移。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/IAngelscriptExecutionStateProvider.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/InterpreterExecutionStateProvider.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/JitExecutionStateProvider.*` |
| 预估工作量 | L |
| 架构风险 | JIT 优化后的执行现场不一定能一次性提供和解释器等价的 locals/temps 视图；如果强行承诺所有 frame 都可看变量，容易做出不稳定的假能力。 |
| 兼容性 | 对现有脚本和现有 `V2` 前端可保持兼容：消息体仍然沿用 `CallStack` / `Variables`，新增的只是 provider 选路和可选 capability；前端即便不理解新 capability，也还能使用现有解释器路径。 |
| 验证方式 | 1. 构造一个走 StaticJIT 的脚本函数，在异常路径下验证 `CallStack` 仍返回正确 `source/line`。 2. 构造“解释器帧调用 JIT 帧”与“JIT 帧调用解释器帧”的混合链，验证栈顺序稳定。 3. 为 `GetAngelscriptExecutionFileAndLine()` 增加单测，覆盖 `debugCallStack == nullptr` 与非空两条分支。 4. 对 `RequestVariables` 增加回归测试：JIT top frame 至少能返回 `this` / 明确“不支持更深变量”，而不是 silent failure。 |

### Arch-DT8：远程调试端点默认对任意客户端开放，但协议缺少 admission 与 discovery contract，难以安全扩展到真正的 remote/debug proxy 场景

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 远程调试接入治理、发现机制、零副作用协商 |
| 当前设计 | `DebugServer` 直接在 `0.0.0.0:<Port>` 上开启 raw TCP listener，连接一旦建立就进入消息读取；`RequestDebugDatabase` 是空消息，`StartDebugging` 只有 `DebugAdapterVersion` 一个字段，服务端收到后立即切换 `bIsDebugging`、清空断点并把该 socket 加入调试客户端列表。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:402-410` 构造函数直接 `FTcpListener(FIPv4Endpoint(FIPv4Address::Any, Port))` 并接受所有连接；`.../AngelscriptDebugServer.cpp:395-399` 的 `HandleConnectionAccepted()` 只记录日志后就把 socket 入队；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:177-181` 显示 `FAngelscriptRequestDebugDatabase` 没有任何协商字段；`.../AngelscriptDebugServer.h:103-115` 显示 `FStartDebuggingMessage` 只有 `DebugAdapterVersion`；`.../AngelscriptDebugServer.cpp:822-827` 收到 `RequestDebugDatabase` 就立即推送数据库与 diagnostics；`.../AngelscriptDebugServer.cpp:897-913` 收到 `StartDebugging` 后立刻置 `bIsDebugging=true`、写全局 `DebugAdapterVersion`、清空断点并加入 `ClientsThatAreDebugging`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:536-552` 的 `DebugDatabaseSettings` 虽然有 `Version = 5`，但它是 server->client 的事实播报，不是 attach 前的准入协商。 |
| 优点 | 对本机单 IDE 调试非常省事，不需要额外配置或外部服务，局域网 attach 也几乎零门槛。 |
| 不足 | “能远程连上”并不等于“能被远程安全治理”。当前端点没有 `client id`、`session label`、`auth token`、`requested capability`、`engine instance id` 或“只读浏览 vs 真正 attach”区分，代理层/第二 IDE/CI 工具无法在不产生副作用的前提下先发现服务能力，再决定是否建立调试会话。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 在真正进入 websocket 调试会话前，先用 HTTP `GET /json/list`、`GET /json/version` 提供 discovery contract，并显式返回 `webSocketDebuggerUrl`；只有 websocket `OnOpen()` 之后才创建 `V8InspectorChannel`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525` | 先 discovery，再 attach，会话建立前不必修改 runtime 状态，远程工具也更容易做代理、列表和权限控制。 |
| UnLua | Debug 能力以本地 `lua_State*` helper 形式暴露；IDE 语义产物则通过 Editor 监听与 commandlet 落到 `Intermediate/IntelliSense`，没有把 IntelliSense/hover 元数据暴露成开放网络端点。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:23-29`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:109-123` | 把“可远程操纵 runtime”的面收窄，把“IDE 索引/补全事实”尽量变成本地 artifact，能显著降低远程接入治理复杂度。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `V2` socket 的同时补一层轻量 `admission + discovery` 协商面，默认把 legacy 行为收缩到 localhost。 |
| 具体步骤 | 1. 新增 `FAngelscriptDebugHello` / `FAngelscriptDebugHelloAck` 消息，至少包含 `ClientName`、`RequestedMode(readonly/debug)`、`RequestedCapabilities`、`InstanceId`、可选 `AuthToken`；只有 `HelloAck` 成功后才允许 `StartDebugging`。 2. 为 `UAngelscriptSettings` 增加 `DebugServerBindAddress`、`bAllowRemoteDebug`、`DebugServerToken`、`bAllowLegacyLocalhostAttach`；默认绑定 `127.0.0.1`，只有显式开启时才使用 `0.0.0.0`。 3. 让 `RequestDebugDatabase` 在 legacy 路径继续可用，但新路径必须先声明 `readonly` 或 `debug` 模式；只读客户端不得触发 `ClearAllBreakpoints()`、`Pause/Continue/Step*`。 4. 追加一个极小 discovery 面，优先复用 puerts 的思路：暴露 `session id / protocol version / capability list / attach url`；若暂时不想引入 HTTP，也至少在第一个包上返回机器可读的 `HelloAck`。 5. 在服务端日志里记录 `ClientName + Mode + RemoteAddress + SessionId`，并为拒绝原因单独打点，方便后续排查代理/多 IDE 场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Public/AngelscriptSettings.h`，可能新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugDiscovery.*` |
| 预估工作量 | M |
| 架构风险 | 需要同时维护 legacy `V2` 与新协商面一段时间；如果切换策略太激进，现有 VS Code 扩展会先失联。 |
| 兼容性 | 可以保持向后兼容：默认允许 `localhost` legacy client 继续直接发 `RequestDebugDatabase/StartDebugging`；只有远程地址或显式开启新模式时才要求 `Hello`/token。对脚本用户无语义变化。 |
| 验证方式 | 1. 现有本机 `V2` 客户端不升级时仍能连接并完成完整调试。 2. 远程地址未携带 token 时只能拿到拒绝原因，不能进入 `StartDebugging`。 3. discovery/hello 返回的 capability 与 `DebugDatabaseSettings` 保持一致。 4. 同时接入一个 `readonly` client 和一个 `debug` client，验证只读端不会触发断点清空或全局调试状态切换。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT7 | JIT 执行状态与调试状态统一 | 结构性收敛 + 调试能力补齐 | 高 |
| P1 | Arch-DT8 | 远程调试 admission/discovery contract | 协议扩展 + 运维治理 | 高 |

---

## 架构分析 (2026-04-08 14:44)

### Arch-DT9：`DebugServer V2` 的协议演进仍是“手写 `FArchive` 契约”，缺少可生成的 schema 层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试协议 schema 治理、前后端协同演进、多 IDE 前端兼容成本 |
| 当前设计 | 协议语义散落在 `EDebugMessageType`、大量内联 `operator<<` 和 `HandleMessage()` 分支里；可选字段主要依赖 `Ar.IsSaving() \|\| !Ar.AtEnd()` 以及全局 `AngelscriptDebugServer::DebugAdapterVersion`，而不是独立 schema 或 capability 描述。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:25` 定义单字节 `EDebugMessageType`；`.../AngelscriptDebugServer.h:103`、`:227`、`:416`、`:536` 分别在消息体内手写字段兼容逻辑，其中 `FAngelscriptVariable` 直接读全局 `DebugAdapterVersion` 决定是否序列化 `ValueAddress/ValueSize`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:52` 只定义了“长度前缀 + message type + body”的二进制 envelope；`.../AngelscriptDebugServer.cpp:820` 到 `:1159` 通过巨型 `if/else` 做分派；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp:9` 到 `:19` 用全局覆盖器切换协议版本，`.../AngelscriptDebugProtocolTests.cpp:121` 到 `:170` 只验证单消息 round-trip。 |
| 优点 | UE 原生 `FArchive` 序列化路径足够轻量，当前 `V2` 前端已经有明确回归测试，新增一个消息类型的本地实现成本不高。 |
| 不足 | 协议事实源不是一个可枚举 schema，而是一组分散的 C++ 约定。前端一旦要支持 mixed-version、只读模式、代理转发或第二种 IDE，就必须同时追踪枚举号、字段顺序、全局版本副作用和服务端分派逻辑；这会让“协议扩展”退化成“同步改多处 C++ 与客户端实现”，而不是在一个合同上增量演进。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `V8InspectorChannel` 只负责把文本协议消息转发给 `v8_inspector::V8InspectorSession`，HTTP discovery 再单独暴露 `/json/list`、`/json/version` 和 `webSocketDebuggerUrl`；协议字段和消息语义由 Chrome DevTools Protocol / V8 Inspector 负责，不由插件手写一套字段顺序。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-65`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-539` | 把“协议合同”外部化后，插件主要维护 runtime bridge，而不是长期维护一套私有 wire schema。 |
| UnLua | Runtime 侧只暴露 `GetStackVariables`、`GetLuaCallStack`、`PrintCallStack` 这组调试原语，直接基于 `lua_State` 和 Lua 官方 debug API 取值；协议面被压到最小，没有再复制一层 wire schema。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-730` | 如果暂时不复用标准协议，至少也应先把 runtime 调试原语与 wire schema 解耦，减少前端兼容面。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为现有 `V2` 二进制协议补一个独立的 `schema + codec` 层：消息号和编码保持兼容，但字段定义、可选字段版本和 capability 收敛到单一事实源。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugProtocolSchema.json`，把 `message type`、字段顺序、`optionalSince`、`requiresCapability`、`deprecatedAfter` 统一声明出来。 2. 新增 `Debugging/Protocol/AngelscriptDebugProtocolCodec.*`，由 schema 生成或驱动 C++ codec；第一阶段只包裹现有 `FArchive` 读写，不立刻改 transport。 3. 把 `FAngelscriptVariable`、`FAngelscriptBreakpoint`、`FAngelscriptDebugDatabaseSettings` 等消息里的 `Ar.AtEnd()` / 全局版本判断迁到 codec 层，`message struct` 自身退回纯数据定义。 4. 让客户端 TypeScript 侧从同一个 schema 生成类型和 golden payload，现有 VS Code 扩展继续沿用 `V2` message id 与 body layout。 5. 把自动化测试从“单消息 round-trip”扩展为“schema golden case + mixed-version decode case + unknown capability degrade case”，避免每次扩字段都手写一组测试分支。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugProtocolSchema.json`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugProtocolCodec.*` |
| 预估工作量 | M |
| 架构风险 | 若新 codec 与旧内联序列化长期并存，协议事实源会再次分裂；因此第一阶段就需要把“谁是真正合同”定清楚，避免生成层只变成旁路文档。 |
| 兼容性 | 可以完全向后兼容：保留现有 `EDebugMessageType` 编号和 `V2` 二进制 body 顺序，新 schema 先只作为生成与校验来源；旧客户端无需立即升级。 |
| 验证方式 | 1. 现有 `AngelscriptDebugProtocolTests.cpp` 全部保持通过。 2. 新增 golden payload 测试，验证旧 `V2` payload 与 schema 生成结果字节级一致。 3. 用 `DebugAdapterVersion=1/2` 和 capability 开关跑 mixed-version 解码测试，确认字段降级行为与当前客户端一致。 4. 对照 VS Code 扩展现有消息日志，验证新增 codec 不改变已发布消息的线协议。 |

### Arch-DT10：`Docs`、`DebugDatabase`、`GoToDefinition`、`UHT` 各自重建符号身份，缺少统一 `symbol graph`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 工具链单一事实源、源码定位闭环、离线与在线工具的符号一致性 |
| 当前设计 | 当前至少存在四条彼此独立的符号提取路径：`FAngelscriptDocs` 用 `FunctionId/TypeId/PropertyOffset` 静态缓存文档和 `UFunction*`；`SendDebugDatabase()` 重新遍历 `asIScriptEngine` 生成 JSON；`GoToDefinition()` 再按 `TypeName/SymbolName` 扫描 runtime；`AngelscriptUHTTool` 另行遍历 `UhtModule/UhtFunction` 产出 `AS_FunctionTable_*` 与 summary。脚本源码位置又存放在 `UASFunction::GetSourceFilePath()/GetSourceLineNumber()` 这条单独链路上。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:19` 到 `:29`、`:31` 到 `:54`、`:118` 到 `:125` 用静态 `TMap` 保存文档和 `UFunction*`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1545` 到 `:1617` 用 `FunctionId -> LookupAngelscriptFunction` 给 `DebugDatabase` 注入 `doc/keywords/meta`，`.../AngelscriptDebugServer.cpp:1707` 到 `:2047` 又整段遍历 object/global symbol 重建 JSON 树；`.../AngelscriptDebugServer.cpp:1288` 到 `:1374` 的 `GoToDefinition()` 先按 namespace/type/method 查 `asIScriptFunction`，再尝试 `LookupAngelscriptFunction()` 导航到 `UFunction`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:407` 到 `:755` 又单独遍历 engine 写 `Docs/angelscript/generated/*.hpp`；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14` 到 `:21`、`:51` 到 `:139`、`:174` 到 `:206`、`:449` 到 `:487` 则把 `ClassName + FunctionName` 重新编码成 `AS_FunctionTable_*.cpp`、summary JSON/CSV；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:34` 到 `:44` 明确只有拿到 `UASFunction` 才会 `OpenFile()`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1535` 到 `:1558` 已经持有脚本函数的真实文件/行号。 |
| 优点 | 每条链都可以独立演进：`UHT` 不需要等 runtime 启动，`Docs` 可由 `dump-as-doc` 批处理生成，`DebugDatabase` 可在 IDE attach 时即时反映当前 engine 状态。 |
| 不足 | 这些链路使用的 identity 不一致：有的以 `FunctionId` 为主，有的以 `ClassName + FunctionName` 为主，有的以 `TypeName/SymbolName` 字符串查找，有的依赖 `UFunction*` 或 `UASFunction`。结果是“符号信息存在，但不在同一个 graph 上”：例如纯脚本函数已经有 `GetSourceFilePath()/GetSourceLineNumber()`，但 `GoToDefinition()` 只有在 `LookupAngelscriptFunction()` 找到 `UFunction` 时才会真正走导航；离线 `.hpp`、在线 `DebugDatabase` 和 UHT summary 也很难共享一份稳定的 `symbol id`。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | Editor 监听资产事件时把 IntelliSense 产物写到 `Intermediate/IntelliSense`，commandlet 全量导出时也写到同一目录和同一文件粒度；保存前都先比对旧内容，保证 artifact contract 稳定。与此同时，runtime 调试原语直接从同一个 `lua_State` 读取 callstack 和 locals。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:143-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-112`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-730` | 在线调试真相和离线 IDE 工件可以是两条 lane，但每条 lane 内都应有单一 owner 与稳定 artifact contract。 |
| puerts | Inspector channel 只把前端消息送进一个绑定到 `Isolate` 的 `V8InspectorSession`，调试会话始终指向同一个 runtime truth，而不是在 transport 层再建一份独立符号模型。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:58-65`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525` | 先明确“符号与源码位置的 owner 是谁”，transport 和前端消费层只读取这份 truth，而不是各自重建。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入统一的 `AngelscriptSymbolGraph` 作为工具链事实源，把 `Docs`、`DebugDatabase`、`GoToDefinition`、`UHT summary` 变成同一图的不同投影。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptSymbolGraph.*`，定义稳定的 `SymbolId`、`Kind(function/type/property/global/enum)`、`SourceLocation(path,line,module)`、`BindingState(direct/stub/script-only)`、`Documentation`、`Keywords`、`UnrealPath`。 2. 第一阶段只实现 runtime builder：从 `asIScriptEngine`、`FAngelscriptDocs`、`FAngelscriptType`、`UASFunction/UASClass` 收集数据，显式把脚本函数 `SourceLocation` 收进 graph；`SendDebugDatabase()` 与 `DumpDocumentation()` 改为读取 graph，而不是各自重走 engine。 3. 把 `GoToDefinition()` 改为先查 graph：若 `UFunction` 可用则继续走 `FSourceCodeNavigation::NavigateToFunction()`；若是纯脚本函数且只有 `SourceLocation`，则直接调用 `OpenFile(path, line)`，补齐当前缺失的 script-only 导航闭环。 4. 第二阶段让 `AngelscriptUHTTool` 输出 `AS_SymbolGraph_UHT.json` 或同 schema 节点，把 `AS_FunctionTable_*` 的 `direct/stub` 状态并入同一 `SymbolId`；现有 `AS_FunctionTable_Summary.json`、CSV 继续保留。 5. 第三阶段再让 VS Code 扩展、`DebugDatabase`、文档导出和诊断面都消费同一 graph，这样“纯脚本函数”“UHT stub”“无源码位置”“JIT 覆盖状态”都能挂在同一个符号节点上展示。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptSymbolGraph.*` |
| 预估工作量 | L |
| 架构风险 | `UHT` 阶段看不到 runtime-only script symbol，runtime 阶段又看不到全部 UHT reconstruction reason；因此 graph 需要支持“分阶段补全”，不能假设一次构建就拥有全量事实。 |
| 兼容性 | 可以增量落地：第一阶段只新增 `SymbolGraph` 和 script-only `GoToDefinition` fallback，不改现有 `DebugDatabase` JSON shape、`.hpp` 文档格式和 `AS_FunctionTable_*` 文件名；旧前端在 graph 缺失时仍走当前路径。 |
| 验证方式 | 1. 对同一批 symbol 比对 `DebugDatabase`、`.hpp` 文档和 `SymbolGraph`，确认名称、文档、参数和继承关系一致。 2. 构造一个没有 `UFunction` 映射的纯脚本函数，验证 `GoToDefinition()` 能直接打开 `.as` 文件和正确行号。 3. 让 `UHT` 输出和 runtime graph 共享同一 `SymbolId`，检查 `AS_FunctionTable_Summary.json` 中的 `direct/stub` 能正确映射到 `DebugDatabase` 节点。 4. 重跑 `dump-as-doc` 与 IDE attach，确认 graph 缺失时仍会回退到旧实现，不影响现有工作流。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT9 | 调试协议 schema 治理、前后端协同演进 | 协议合同收敛 + 代码生成 | 高 |
| P1 | Arch-DT10 | 工具链统一 symbol graph、script-only 导航闭环 | 工具链收敛 + 导航能力补齐 | 高 |

---

## 架构分析 (2026-04-08 14:59)

### Arch-DT11：调试宿主已经支持多 `FAngelscriptEngine` 生命周期，但 `DebugServer` 仍按进程级单实例治理，无法把 attach 精确路由到 engine instance

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 多引擎实例调试、PIE/多 GameInstance 并存、远程 attach 路由 |
| 当前设计 | `FAngelscriptEngine` 生命周期已经允许 context stack、`GameInstanceSubsystem` 和 clone/shared-state 并存，但 `DebugServer` 的关键调试状态仍被压到进程级单例或 shared-state 单指针上，attach 没有 `engine instance` 维度。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:718-733` 说明当前 engine 解析是动态的：先看 `FAngelscriptEngineContextStack`，再看 `UAngelscriptGameInstanceSubsystem::GetCurrent()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp:17-29`、`94-118` 说明每个 `GameInstanceSubsystem` 都可能持有自己的 `PrimaryEngine`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:145-152`、`922-940` 说明 owned/shared-state 里只保存一个 `DebugServer` 指针，并会在 clone 之间共享；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1452-1456` 仍是 `new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort)` 的单端口创建；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:50`、`897-903` 把 `DebugAdapterVersion` 维持为进程级静态量；`.../AngelscriptDebugServer.cpp:111-117`、`402-416`、`419-425` 又把 Windows hardware breakpoint 后端绑到单个 `GActiveDebugServer` 和单个 vectored exception handler。 |
| 优点 | 当前主线对“一个主 engine + 一个 IDE attach”非常直接；clone 共享 `DebugServer` 也避免了同一 shared-state 被多个 socket 各自重新初始化。 |
| 不足 | 一旦出现多 `GameInstance`、多 PIE world、测试 clone 或未来多 engine host，当前调试入口无法表达“我要 attach 到哪一个 engine instance”。更具体地说，socket 连接、`DebugAdapterVersion`、Windows data breakpoint backend 和 shared-state 里的 `DebugServer` 指针都默认全进程只有一份，这会让“并存”退化为“最后一个 owner 覆盖前一个 owner”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | Inspector 会话不是裸全局状态，而是显式绑定 `CtxGroupID`；连接建立后按 websocket handle 创建独立 `V8InspectorChannelImpl`，再把 channel 保存在 `V8InspectorChannels` 映射里。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-100`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:236-238`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:372-377`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525` | 先把“会话属于哪个 runtime/context group”建模出来，transport 才能安全扩展到多实例。 |
| UnLua | `ULuaEnvLocator_ByGameInstance` 直接维护 `TMap<UGameInstance, FLuaEnv>`；`FLuaEnv::GetAll()` / `FindEnv()` 则让调试和诊断可以按 env 聚合，而不是假设只存在一个 Lua VM。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnvLocator.h:37-50`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:57-61`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/LuaEnv.h:161-180`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:145-149`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:198-205` | 运行时实例和调试实例应先一一对应，再谈统一入口和错误汇总。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有端口和 `V2` 协议之上补一层 `engine instance routing`，把“监听端口”与“具体调试宿主”拆开。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 与 `FAngelscriptOwnedSharedState` 中增加稳定的 `EngineInstanceId`，初始化时写入 world/game instance 标签；clone 共享 shared-state 时保留同一 `EngineInstanceId`。 2. 新增 `Debugging/AngelscriptDebugHostRegistry.*`，把 `EngineInstanceId -> DebugHostDescriptor(DebugServer*, label, world path, capability)` 注册成全局表，监听端口只负责 discovery/route，不直接等于某个 host。 3. 扩展 attach/discovery 消息，允许客户端显式声明 `EngineInstanceId`；旧客户端不带该字段时继续落到“primary engine”兼容路径。 4. 把 `DebugAdapterVersion` 从静态进程变量挪到 session/host 维度；`ClientsThatAreDebugging` 也按 host 维护，避免不同 engine attach 串台。 5. Windows data breakpoint 后端从单个 `GActiveDebugServer` 升级为“按 thread 或 execution 反查 owner host”的注册表；第一阶段即便仍只有一个硬件 breakpoint handler，也要先让 handler 能定位 `EngineInstanceId`，而不是只认最后一个 `FAngelscriptDebugServer*`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameInstanceSubsystem.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugHostRegistry.*` |
| 预估工作量 | L |
| 架构风险 | Windows hardware breakpoint 的异常回调目前天然偏“进程唯一 owner”；如果 owner 反查规则不严谨，最容易出现断点命中后暂停到错误的 engine instance。 |
| 兼容性 | 可以向后兼容：不要求立即改现有 VS Code 扩展；缺少 `EngineInstanceId` 的旧客户端默认 attach 到 primary host。现有单 engine 项目不会有行为变化。 |
| 验证方式 | 1. 构造两个 `GameInstanceSubsystem`/clone 并存的场景，验证 discovery 能列出两个 `EngineInstanceId`。 2. 同时让两个客户端 attach 到不同实例，验证 `DebugAdapterVersion`、断点和暂停状态互不串台。 3. Windows 上验证 hardware data breakpoint 命中后能回到正确的 host。 4. 旧客户端不升级时仍能 attach primary engine 并完成单步。 |

### Arch-DT12：`UHT`、`dump-as-doc`、DirectoryWatcher、debug socket 与 `code --goto` 各自拥有刷新入口，工具链没有统一编排器

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 工具链统一管线、刷新失效边界、本地 IDE 与远程调试的职责划分 |
| 当前设计 | 当前至少有五条独立 toolchain lane：`UHT` 生成 `AS_FunctionTable_*`；Editor `DirectoryWatcher` 负责脚本重载；`dump-as-doc` 单独导出 `Docs/angelscript/generated/*.hpp`；`DebugServer` 驱动部分 workspace/write-back 流程；`AngelscriptSourceCodeNavigation` 则直接 shell-out 到 VS Code。它们共享同一插件目标，却没有统一的 refresh coordinator 或本地 manifest。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:366-381` 在 Editor 启动时对全部 script root 注册 `DirectoryWatcher`；`.../AngelscriptEditorModule.cpp:121-128`、`339-348` 说明 literal asset 保存还依赖 `HasAnyDebugServerClients()`，没有 IDE client 时直接拒绝；`.../AngelscriptEditorModule.cpp:396-412` 又把 asset list / create blueprint 反向挂回 debug delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2223-2227` 只有在 `-dump-as-doc` 路径才执行 `FAngelscriptDocs::DumpDocumentation()` 并退出进程；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:407-430`、`675-755` 负责把文档单独落到 `Docs/angelscript/generated/*.hpp`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:95-115` 直接通过 `FPlatformMisc::OsExecute(..., "code", "--goto ...")` 打开 IDE；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-79`、`166-215`、`292-352` 则独立读取 `Runtime.Build.cs`、输出 `AS_FunctionTable_*.cpp` 和 summary JSON/CSV。 |
| 优点 | 每条链都贴合自己的时机：`UHT` 生成适合编译期，`dump-as-doc` 适合批处理，Editor watcher 适合本地增量，debug socket 则能服务 live session。 |
| 不足 | 问题不在“链路多”，而在“没有 owner”。现在没有一个组件能回答“当前工具链是否新鲜、哪条产物过期、没有 IDE client 时哪些能力应退化为本地流程”。结果是部分本地工作流被 debug client 反向卡住，部分产物只能靠命令行生成，导航又直接写死 `code`，很难平滑扩展到第二种 IDE 或 headless CI。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | Editor 常驻的 `FUnLuaIntelliSenseGenerator` 负责资产事件增量更新，commandlet 全量重建时复用同一输出目录与同一 `SaveFile()` 去重写策略。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:143-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-99`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:117-134` | 增量链和批处理链可以不同，但必须由同一个 artifact owner 统筹输出目录、覆盖规则和 freshness。 |
| puerts | Inspector 只暴露 discovery 和调试会话，不负责 Editor 写回或 IDE 工件生成；HTTP `/json/list`、`/json/version` 与 websocket message loop 都保持只读调试职责。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-540` | 调试 transport 应服务 runtime 观察，而不是反过来拥有本地工具链写操作。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增本地 `ToolchainCoordinator`，把现有多条 lane 变成“多个 producer + 一个协调者 + 多个 consumer”的统一编排。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Source/AngelscriptEditor/Toolchain/AngelscriptToolchainCoordinator.*`，集中维护 `SymbolIndexRevision`、`DocsRevision`、`FunctionTableRevision`、`WorkspaceRevision` 和本地 manifest。 2. 让 `DirectoryWatcher`、`dump-as-doc`、literal asset save、`UHT` sidecar 写入都改为“通知 coordinator 某类事实发生变化”，而不是直接各自落盘/广播；第一阶段 coordinator 仍可调用旧实现完成实际工作。 3. 从 `DebugServer` 抽离本地 workspace 写入职责：没有 IDE client 时，literal asset 保存也应能通过 coordinator 更新本地 artifact；有 client 时再把变化作为可选增量事件推给前端。 4. 为 `AngelscriptSourceCodeNavigation` 增加 `ISourceEditorLauncher` 抽象，先由 coordinator 提供“优选 IDE/工作区”决策；现有 VS Code `code --goto` 路径保留为默认实现。 5. 让 `AngelscriptUHTTool` 在继续输出 `AS_FunctionTable_*` 和 summary 的同时，顺手落一个 coordinator 可读取的 fragment manifest；`dump-as-doc` 和 runtime attach 再只读这一份本地状态，而不是各自猜测另一条链何时完成。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，新增 `Plugins/Angelscript/Source/AngelscriptEditor/Toolchain/AngelscriptToolchainCoordinator.*`，新增 `Plugins/Angelscript/Source/AngelscriptEditor/Toolchain/ISourceEditorLauncher.h` |
| 预估工作量 | L |
| 架构风险 | 如果 coordinator 只做“转发中心”而不真正接管 revision/freshness，最终会形成第六条旁路；因此第一阶段就必须先定义清楚 manifest 与 lane owner。 |
| 兼容性 | 可以渐进式接入：旧 `dump-as-doc` 参数、旧 `DebugServer` 消息和现有 VS Code 打开方式都继续保留，只是内部改为委托给 coordinator。对现有脚本用户无语义变化。 |
| 验证方式 | 1. 关闭 VS Code 扩展后保存 literal asset，验证本地 toolchain 仍能更新对应 artifact。 2. 分别走 UHT、Editor watcher 和 `dump-as-doc` 三条路径，确认最终 manifest 的 revision 单调递增且不会互相覆盖。 3. 将默认 IDE launcher 替换为测试实现，验证 `GoToDefinition` / source navigation 不再硬依赖 `code` 命令。 4. 回归旧调试客户端，确认 coordinator 接管后 `ReplaceAssetDefinition`、asset list、create blueprint 仍可按旧消息工作。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT11 | 多引擎实例调试路由、PIE/clone 并存 attach | 结构性重构 + host registry | 高 |
| P1 | Arch-DT12 | 工具链统一编排、本地与远程职责分离 | 工具链收敛 + coordinator 抽象 | 高 |

---

## 架构分析 (2026-04-08 16:44)

### Arch-DT13：`StaticJIT` 的 native-form 元数据仍靠 `GetPreviousBind()` 临场旁听，JIT 可编译性没有独立的持久化合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | JIT native form 元数据采集、bind toolchain 独立性、可扩展覆盖率 |
| 当前设计 | 现在 `StaticJIT` 判断某个 system/native call 能否生成更优调用，不是读取独立 sidecar 或 bind manifest，而是在 `as-generate-precompiled-data` 模式下借 `FAngelscriptBinds::GetPreviousBind()` 把“刚刚注册的上一个函数”临时塞进全局 `GScriptNativeForms`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:299-305` 的 `GetPreviousBind()` 直接依赖 `PreviouslyBoundFunction` 这份 ambient state；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:432` 只有在每次绑定完成后才更新这个状态；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp:27-29,55-59` 用进程级 `GScriptNativeForms` 维护 `asIScriptFunction* -> FScriptFunctionNativeForm*`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp:112-116,530-534,703-707,1021-1025` 等 `BindNative*` / `BindUFunction` / `BindTemplateInstantiatedCall` / `BindEventFunctionExecute` 都在 `!FAngelscriptEngine::bGeneratePrecompiledData` 时直接 `return`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:522,872,1433-1447,1469` 说明这条采集链只有在 `as-generate-precompiled-data` 模式、创建 `PrecompiledData + StaticJIT` 并加载 `Binds.Cache` 时才会走；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:56-87,123-141` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp:27-31` 只持久化 class/struct/method/property bind 基本信息，虽然 `FAngelscriptMethodBind` 有 `bTrivial`，但没有 `CustomForm/Header/NativeSymbol/TemplateConstraint` 这类 JIT 生成必需事实；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14-22,166-206` 输出的也只是 `AddFunctionEntry(...)` 与 summary/CSV，而不是 JIT-native-form contract。 |
| 优点 | 现有实现改动少，bind 作者继续调用 `BindNative*` 即可；JIT 生成期能直接拿到当次运行里解析后的 `asIScriptFunction*`，不需要额外 symbol resolve。 |
| 不足 | JIT 元数据不是“工具链事实”，而是“某次特殊运行模式下顺手录下来的瞬时状态”。这会让新增 native/custom form 的扩展点绑定到运行时注册顺序、`GetPreviousBind()` 语义和 `bGeneratePrecompiledData` 开关上；CI、headless 分析器或未来独立 JIT exporter 无法只靠持久化工件判断某个 bind 为何只能走 `PointerCall/DynamicCall`。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense 由显式 `FUnLuaIntelliSenseGenerator` 负责产出，Editor 初始化后注册资产事件，`Export()` 明确生成内容，`SaveFile()` 只在内容变化时落盘。工具链事实由固定输出目录拥有，而不是寄生在某次运行时状态上。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:143-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233` | JIT 可编译性也应有独立 producer 和持久化 artifact，而不是只在“生成预编译数据”模式里临场收集。 |
| puerts | Inspector 抽象先定义 `V8Inspector` / `V8InspectorChannel`，会话事实由显式 channel 对象持有；连接建立时为每个 websocket 创建 channel，再转发协议消息。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-65`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525` | 关键元数据/会话状态应有显式 owner，对外可发现；不应长期依赖“最近一次注册对象”这类 ambient state。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `StaticJIT` native-form 采集从“precompiled-data 运行模式旁听 bind 注册”升级为 `BindDatabase + optional manifest` 的显式合同，同时保留当前 `GScriptNativeForms` 作为兼容 fallback。 |
| 具体步骤 | 1. 扩展 `FAngelscriptMethodBind`，新增 `JitBindingKind(native/custom/ufunction/template/helper)`、`NativeSymbol`、`CustomHeader`、`TemplateConstraintFlags`、`bSupportsPointerCall`、`bRequiresCustomGenerator` 等字段；旧 `Serialize()` 后追加版本化字段。 2. 新增 `FAngelscriptJitBindingRecorder`，让现有 `BindNative*` / `BindUFunction` / `BindTemplateInstantiatedCall` 继续使用 `GetPreviousBind()`，但不再只写 `GScriptNativeForms`，而是同步把稳定记录写入 `FAngelscriptBindDatabase`。 3. 让 `FAngelscriptBindDatabase::Save()` 旁路写出 `Binds.Cache.Jit` 或在主缓存中追加新段；`Load(..., bGeneratingPrecompiledData)` 先恢复这些记录，再由 `StaticJIT` 按 `UnrealPath + Declaration + ScriptName` 或 resolved function id 建立运行时索引。 4. 让 `AngelscriptFunctionTableCodeGenerator.cs` 在 summary/CSV 中追加 `jitBindingKind` / `hasDirectJitForm`，至少把“函数表直连”与“JIT native form 可用”分成两个维度，而不是继续混在运行时里推断。 5. 第二阶段再让 `AngelscriptStaticJIT` 优先消费持久化记录；缺记录时才回退到现有 `GScriptNativeForms`，这样旧缓存与旧 bind 宏都不必一次性迁移。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 预估工作量 | L |
| 架构风险 | `asIScriptFunction*` 与运行时 `FunctionId` 都不是天然跨进程稳定键；如果 canonical key 设计不好，旧缓存会把错误的 JIT form 关联到错误函数。模板实例与 generated accessor 也需要明确 canonical 规则。 |
| 兼容性 | 可增量落地。第一阶段仅新增持久化字段和 summary，不移除 `GScriptNativeForms`，缺少新字段时继续走当前 `bGeneratePrecompiledData` 收集路径。现有 bind 宏与现有 `Binds.Cache` 消费方都可保持可用。 |
| 验证方式 | 1. 为一组代表性 `BindNative*` / `BindUFunction` / template bind 生成 `Binds.Cache(.Jit)`，验证重新启动后无需再次执行“generate precompiled data”也能恢复同样的 JIT binding record。 2. 对 `AS_FunctionTable_Summary.json` 与新 JIT summary 做 join，确认 direct bind 但无 JIT native form 的条目能被单独诊断。 3. 构造一个只支持 `DynamicCall` 的 bind，验证 summary 能明确给出 `jitBindingKind` 与 fallback reason。 4. 回归当前 `as-generate-precompiled-data` 路径，确认旧 `GScriptNativeForms` fallback 仍能产出相同 JIT code。 |

### Arch-DT14：`PauseExecution()` 依赖 GameThread 内嵌 socket pump，暂停控制与 transport 调度耦合过深

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试暂停控制、多客户端协作、替换 transport/front-end 的扩展成本 |
| 当前设计 | 当前“脚本暂停”不是一个独立 pause controller，而是在 `PauseExecution()` 里把 `GameThread` 直接拉进 `while (bIsPaused)` 循环，循环体持续 `ProcessMessages()` 读取所有 socket、处理 `Continue/Step*` 并顺手回 `CallStack`。换句话说，暂停态本身就是一个嵌套的网络消息泵。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:667-695` 的 `PauseExecution()` 在发出 `HasStopped` 后进入 `while (bIsPaused) { ProcessMessages(); FPlatformProcess::Sleep(0); }`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:702-708` 的 `SleepForCommunicate()` 也通过反复 `ProcessMessages()` 实现“睡眠时仍能通信”；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:712-818` 的 `ProcessMessages()` 同时承担 accept、recv、send、ping 和 paused callstack 请求；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:837-895` 中 `Continue/StepIn/StepOver/StepOut` 都是直接改写 `bIsPaused` / `bBreakNextScriptLine` / `ConditionBreakFrame`，且 `StepOver/StepOut` 立即读取 `asGetActiveContext()`；`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp:147-149` 的测试注释明确写着“`PauseExecution` blocks the GameThread but its `ProcessMessages()` loop reads ALL client sockets, so the monitor's Continue message unblocks the pause from the thread pool.” |
| 优点 | 单前端、本机调试路径非常直接，不需要额外线程或控制器对象；当前断点/单步 tests 也已经围绕这套语义建立。 |
| 不足 | 一旦要接第二种 IDE 前端、代理层、websocket bridge 或独立 debug worker，这种“暂停 = 在执行线程里轮询全部 transport”就会成为硬耦合点。控制消息只有在暂停循环还在跑、且 `asGetActiveContext()` 仍可读时才有完整语义，这使 transport 替换、异步 command queue 和 pause snapshot 都变得困难。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | pause loop 虽然也会在 `runMessageLoopOnPause()` 中持续 `Tick()`，但 websocket 连接早已按 `Handle -> V8InspectorChannel` 建立独立 channel，`OnReceiveMessage()` 只负责把消息 dispatch 到对应 session，而不是在暂停循环里再复用一套全局 socket 状态机。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:572-589` | 即便仍有 pause message loop，也应该先把 transport/session 边界立起来，让“暂停控制”不等于“扫全局 socket 列表”。 |
| UnLua | 不内建网络 pause loop，而是把 `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 直接暴露为基于 `lua_State*` 的本地调试原语。调试态观察与 transport 没有互相嵌套。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-721` | 先把 pause 后的执行状态和调试原语独立出来，再决定消息如何到达，扩展第二前端或本地/远程混合模式时更稳。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入独立的 `PauseController + CommandQueue`，把“暂停态控制”从 `ProcessMessages()` 的全局 socket pump 中剥离出来；现有 `V2` 消息不变，只改内部调度模型。 |
| 具体步骤 | 1. 新增 `Debugging/Execution/AngelscriptPauseController.*`，定义 `EnterPause(FStoppedMessage, Snapshot)`、`WaitForCommand()`、`ApplyCommand(Continue/StepIn/StepOver/StepOut)` 和 `LeavePause()`；`FAngelscriptDebugServer` 在断点命中时只负责创建 snapshot 并进入 controller。 2. 将 `Continue/Step*` 从“直接改全局字段”改为“入队 `FDebugControlCommand`”；`ProcessMessages()` 只做 transport 收包与 command enqueue，不再在 paused loop 里直接执行 stepping 语义。 3. 第一阶段先让 snapshot 只承载 `StopReason`、top frame/source location、可选 `asCContext*` 引用；`RequestCallStack` 仍可回落到当前路径，但 paused controller 应优先从 snapshot 返回冻结视图，减少对 `asGetActiveContext()` 时序的硬依赖。 4. 将 `PauseExecution()` 的 `while (bIsPaused)` 改为等待 controller/event，而不是反复 `ProcessMessages()`；如果仍需兼容单线程实现，也应拆成 `DrainIncomingCommands()` 与 `ApplyPauseState()` 两个明确阶段。 5. 第二阶段再增加 `IDebugTransportPump` 抽象，让 raw TCP `V2`、未来 websocket/CDP bridge 或本地 in-process front-end 都统一把命令送进同一个 controller。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Execution/AngelscriptPauseController.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/IDebugTransportPump.h`，`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 预估工作量 | L |
| 架构风险 | pause snapshot 若过早冻结，`Evaluate/Variables` 可能读到过期 locals；若仍引用 live `asCContext*`，则需要明确 controller 的 thread ownership，避免把今天的硬耦合只是换个文件名继续保留。 |
| 兼容性 | 对现有 `V2` 前端可完全向后兼容：消息号、payload 和暂停/继续行为保持不变。变动只在服务端内部，从“paused loop 里直接扫 socket”改成“socket 收到命令后入队给 pause controller”。 |
| 验证方式 | 1. 保留现有 breakpoint/stepping 自动化测试，但把 `AngelscriptDebuggerBreakpointTests.cpp:147-149` 那种“依赖第二客户端线程来唤醒 paused GameThread”的假设逐步去掉。 2. 新增 paused-state 测试：一个客户端触发 pause，另一个客户端延迟发送 `Continue` / `StepOver`，验证 controller 队列仍能正确消费。 3. 在高延迟/断连场景下验证 paused controller 不会因 socket pump 阻塞而卡死。 4. 为未来 bridge 预留一个 fake transport，实现同一套 pause tests 无需真实 TCP 也能通过，证明 transport 已与 pause 控制解耦。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT13 | JIT native-form 元数据采集、bind toolchain 独立合同 | 工具链收敛 + 持久化 contract 新增 | 高 |
| P1 | Arch-DT14 | pause 控制与 transport 调度解耦、多前端调试控制 | 结构性重构 + pause controller 抽象 | 高 |

---

## 架构分析 (2026-04-08 16:55)

### Arch-DT15：调试数据模型仍是“Path DSL + 当前活动上下文”，难以桥接标准前端的对象引用语义

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试协议对象模型、暂停态快照稳定性、多 IDE 前端适配 |
| 当前设计 | `DebugServer V2` 的变量、求值和栈帧访问并没有显式 `frameId/scopeId/valueId`；客户端发来的核心查询参数是 `Path` 字符串，服务端再把这个字符串解释成“当前活动 `asGetActiveContext()` 上的某个 frame/scope/value”。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:103-115` 的 `FStartDebuggingMessage` 只有 `DebugAdapterVersion`；`.../AngelscriptDebugServer.h:196-224` 的 `FAngelscriptCallFrame/FAngelscriptCallStack` 只有 `Name/Source/LineNumber/ModuleName`，没有稳定 frame handle；`.../AngelscriptDebugServer.h:416-438` 的 `FAngelscriptVariable` 只有 `Name/Value/Type/bHasMembers` 与可选 `ValueAddress/ValueSize`，没有 `variableReference`；`.../AngelscriptDebugServer.cpp:1081-1128` 处理 `RequestVariables/RequestEvaluate` 时直接从 payload 读取 `FString Path`；`.../AngelscriptDebugServer.cpp:2282-2375` 的 `ResolveDebuggerFrame()/GetDebuggerValue()` 先从 `Path` 前缀解析 frame，再基于 `asGetActiveContext()` 和 `%local%/%this%/%module%` 魔法前缀解析对象；`.../AngelscriptDebugServer.cpp:2692-2823` 的 `GetDebuggerScope()` 继续依赖当前活动 context 动态展开 locals、`this` 和 module globals。 |
| 优点 | 客户端实现成本低，VS Code 适配器不需要维护复杂对象缓存；`Path` 同时能表达 locals、`this`、module globals 和 Blueprint frame 特例。 |
| 不足 | 调试对象身份不是显式合同，而是“某次暂停时当前活动 context 的即时解析结果”。这会让跨请求缓存、延迟求值、远程代理重放、DAP `variablesReference` 映射以及“第二个 IDE 前端只读查看同一暂停快照”都很脆弱，因为服务端每次都重新读 live context，而不是消费一个稳定 snapshot。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 连接建立后先按 websocket handle 创建独立 `V8InspectorChannel`，再把文本协议消息直接分派给 `v8_inspector::V8InspectorSession`；执行上下文也通过 `CtxGroupID` 显式注册给 Inspector。这里的对象身份与暂停态语义由标准 Inspector session 持有，而不是插件自定义一套 `Path` 语法。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-65`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-116`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:300-308`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525` | 先建立“每连接一个 session、每上下文一个显式 identity”的模型，再让前端协议消费这个模型；对象引用不应靠客户端自拼字符串。 |
| UnLua | 调试原语直接显式参数化 `lua_State* L` 和 `StackLevel`：`GetStackVariables(lua_State* L, int32 StackLevel, ...)`、`GetLuaCallStack(lua_State* L)`、`PrintCallStack(lua_State* L)` 都要求调用者先提供明确的运行时上下文。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-724` | 即便不引入标准协议，也应让“要看哪个栈帧/哪个上下文”成为显式 API 合同，而不是隐含在字符串解析和全局 active context 里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `Path` 请求兼容层的前提下，引入 `StoppedSnapshotId + FrameId + ScopeId + ValueId` 的显式调试对象模型，把 live context 查询逐步收敛到 snapshot 驱动。 |
| 具体步骤 | 1. 新增 `FDebugStoppedSnapshot` 与 `FDebugObjectId`，在 `HasStopped` 时生成一次快照，至少记录 `SnapshotId`、frame 列表、frame kind（script/BP/C++）、source location 和可选 `asCContext*`/JIT debug frame bridge。 2. 为 `CallStack`、`Variables`、`Evaluate` 增加 `V3` payload：`CallFrame` 返回 `FrameId`，`RequestScopes(FrameId)` 返回 `ScopeId`，`RequestVariables(ScopeId/ValueId)` 返回子值与新的 `ValueId`；旧 `Path` 协议继续保留为 adapter。 3. 把 `GetDebuggerValue()/GetDebuggerScope()` 内部拆成两层：第一层只根据 `FrameId/ScopeId/ValueId` 解析对象身份，第二层才在需要时访问 live memory；这样 DAP bridge、远程代理和测试 fake transport 都能复用同一对象模型。 4. `ResolveDebuggerFrame()` 改为在生成 `CallStack` 快照时一次性完成 frame 编号，不再让每次变量/求值请求都重新扫描 `asGetActiveContext()` 和 Blueprint stack。 5. capability 握手里新增 `supportsObjectHandles`，只有新前端启用 ID 模型；旧前端保持 `Path` 行为不变。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Model/AngelscriptDebugSnapshot.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Model/AngelscriptDebugObjectId.h`，`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebugProtocolTests.cpp` |
| 预估工作量 | L |
| 架构风险 | 若 snapshot 只缓存 identity、不缓存值，延迟请求仍会读到 live locals；若 snapshot 缓存整值，则要控制暂停态内存占用和对象生命周期。JIT/解释器/BP 混合栈也需要统一 frame identity 规则。 |
| 兼容性 | 可完全增量实施。旧 `DebugAdapterVersion` 和现有 `Path` 语义继续保留；新 `FrameId/ScopeId/ValueId` 仅在 capability 宣告后启用，不影响现有 VS Code 扩展。 |
| 验证方式 | 1. 保留现有 `RoundTrip` 协议测试，再新增 `V3` 对象句柄测试，验证同一暂停态下多次 `RequestVariables` 返回稳定 `ScopeId/ValueId`。 2. 新增“延迟求值”测试：暂停后先请求栈，再等待若干 message pump tick，再用 `FrameId` 请求 locals，确认仍命中同一 snapshot。 3. 新增双客户端只读测试：一个客户端负责单步，另一个客户端只消费 `FrameId/ScopeId`，确认不会因 `Path` 重新解析而串台。 4. 为未来 DAP bridge 添加 fake adapter，用 `FrameId -> variablesReference` 映射跑通最小变量窗链路。 |

### Arch-DT16：工具链产物没有统一的可丢弃 artifact root，文档、生成代码和 IDE 入口分散在不同生命周期里

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 工具链产物归属、输出路径卫生、CI/本地 IDE 可重放性 |
| 当前设计 | 当前工具链既有通过 `IUhtExportFactory` 管理的 `AS_FunctionTable_*` 生成产物，也有直接写入 `<Project>/Docs/angelscript/generated` 的文档头文件，还有完全不经过产物层、直接 `OsExecute("code")` 的 VS Code 导航入口。三类输出各自有 owner，但没有统一 artifact root 和生命周期约束。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:528` 通过命令行参数启用 `dump-as-doc`；`.../AngelscriptDocs.cpp:675-755` 直接把每个 class 文档写到 `FPaths::ProjectDir()/Docs/angelscript/generated/*.hpp`，并用 `FFileHelper::SaveStringToFile()` 覆盖文件；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:120-121` 用 `factory.MakePath(...).cpp` + `CommitOutput()` 生成 `AS_FunctionTable_*`，`...:174-206` 再额外写 `AS_FunctionTable_Summary.json`/CSV，`...:432-445` 只对这一路输出做 stale cleanup；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:96-115` 直接 shell 到 `code --goto` 打开文件；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:706-720` 也把“Open Angelscript workspace”固定成打开 `<Project>/Script` 的 VS Code 菜单。 |
| 优点 | 当前团队如果默认使用 VS Code + checked-in docs，这套路径非常直接；UHT 侧已经至少具备 shard、summary 和 stale cleanup，说明部分工件管理是成体系的。 |
| 不足 | 产物生命周期不一致：UHT 输出是“可再生并可清理”的，docs 输出是“直接写入项目文档目录”的，导航入口则完全依赖本机 `code` 命令。结果是 CI 很难只清理一个工具链根目录重建全部事实，其他 IDE 也难以消费同一套 artifact，而 docs 目录还会承受生成物写回带来的仓库噪音。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | Editor 常驻生成器和 commandlet 共享同一输出根 `.../Intermediate/IntelliSense`；两边都复用 `SaveFile()`，并且只有内容变化时才落盘。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-186`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:21-26`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:117-130` | 先统一 artifact root，再统一增量写入策略；本地监听和批处理命令只是同一产物合同的两个 producer。 |
| puerts | Inspector discovery 完全走运行时 `/json/list`、`/json/version` 与 websocket 会话，没有把调试发现能力写成 repo 内生成文件。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:330-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477` | 运行时调试 discovery 可以和本地 IDE 文件产物分离，避免工具链既承担会话发现又承担文件输出。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增统一的 `ToolchainArtifactRoot`（例如 `Intermediate/AngelscriptToolchain`），把 docs、UHT summary、navigation index 和后续 manifest 都先写到这一个可重建目录；保留现有 `Docs/angelscript/generated` 与 VS Code 菜单作为兼容发布层。 |
| 具体步骤 | 1. 新增 `FAngelscriptToolchainPaths` 或 `Toolchain/AngelscriptArtifactManifest.*`，统一定义 `Intermediate/AngelscriptToolchain/{Docs,UHT,Symbols,Navigation}` 子目录，并记录 revision/time/hash。 2. 修改 `FAngelscriptDocs::DumpDocumentation()`，先把 `.hpp` 和轻量索引写入 `Intermediate/AngelscriptToolchain/Docs`，只有在显式 `publish-docs` 或兼容模式下，才镜像到 `Docs/angelscript/generated`；同时像 UnLua 一样增加“内容未变化不写盘”。 3. 让 `AngelscriptFunctionTableCodeGenerator.cs` 在继续调用 `factory.CommitOutput()` 的同时，额外写一份 artifact manifest fragment，明确 `AS_FunctionTable_*`、summary、CSV 的相对路径与 revision；stale cleanup 也纳入同一 manifest。 4. 将 `AngelscriptSourceCodeNavigation` 和 `ASOpenCode` 菜单改为依赖 `ISourceEditorLauncher + NavigationIndex`：默认实现仍调用 `code`，但如果本机没有 VS Code 或用户选择 Rider/VS，仍可消费同一 `NavigationIndex`。 5. 在 `RequestDebugDatabase` 或未来 toolchain coordinator 中优先读取 `ToolchainArtifactRoot` 的 manifest，而不是假定 docs/UHT/导航各自产物已经各就各位。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，新增 `Plugins/Angelscript/Source/AngelscriptEditor/Toolchain/AngelscriptToolchainPaths.*`，新增 `Plugins/Angelscript/Source/AngelscriptEditor/Toolchain/AngelscriptArtifactManifest.*`，新增 `Plugins/Angelscript/Source/AngelscriptEditor/Toolchain/ISourceEditorLauncher.h` |
| 预估工作量 | M |
| 架构风险 | docs 从项目文档目录迁到 `Intermediate` 后，依赖旧路径的脚本或 IDE 配置可能失效；如果兼容层处理不好，短期内可能出现“两份 docs 不一致”的窗口。 |
| 兼容性 | 可以渐进迁移。第一阶段只新增 `Intermediate/AngelscriptToolchain` 与 manifest，不删除旧 `Docs/angelscript/generated`，也不移除 `code` launcher；旧工作流继续可用，新工具链逐步切换到 artifact root。 |
| 验证方式 | 1. 在全新工作区执行一次 UHT + `dump-as-doc`，确认 `Intermediate/AngelscriptToolchain` 能独立重建全部产物。 2. 反复执行相同输入，确认 docs/index 在内容不变时不重复写盘。 3. 删除 `Docs/angelscript/generated` 后验证 IDE 导航和调试数据库仍能通过 manifest 工作。 4. 切换一个非 VS Code 的 `ISourceEditorLauncher` 测试实现，确认 `GoToDefinition` 不再硬依赖 `code` 命令。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT15 | 调试对象模型、暂停快照稳定性、标准前端桥接 | 协议扩展 + snapshot 抽象 | 高 |
| P1 | Arch-DT16 | 工具链产物归属、输出路径卫生、IDE 无关性 | 工具链收敛 + artifact root 新增 | 高 |

---

## 架构分析 (2026-04-08 17:07)

### Arch-DT17：`DebugDatabase`、`Docs`、`UHT` 与导航各自重建语义事实，缺少统一 symbol graph，导致工具链只能靠重复遍历与全量推送维持一致

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试数据库、文档生成、代码生成与导航之间的语义来源是否统一 |
| 当前设计 | `RequestDebugDatabase` 是空请求，服务端每次都从 live `ScriptEngine` 即时拼装一份 JSON symbol graph；`DumpDocumentation()`、`AngelscriptUHTTool` 和 `SourceCodeNavigation` 再各自从不同数据源重建同一批类型/函数/属性事实。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:177-193` 中 `FAngelscriptRequestDebugDatabase` 没有任何筛选字段，而 `.../AngelscriptDebugServer.h:536-555` 的 `DebugDatabaseSettings` 只有固定 `Version = 5`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1493-1505` 进入 `SendDebugDatabase()` 后先发送 settings，`...:1545-1705`、`...:1707-2047` 再用本地 `GetDecl` 与 live 遍历重建 types / methods / globals / enums，并以 `DebugDatabaseFinished` 结束一次全量快照；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:416-430`、`...:516-673` 重新定义一套 `GetDecl` 与遍历逻辑，`...:675-755` 再把结果写成 `Docs/angelscript/generated/*.hpp`；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-45` 与 `.../AngelscriptFunctionTableCodeGenerator.cs:51-76`、`...:166-206` 又基于 `factory.Session.Modules` 额外走一遍 UHT 语义树并写出 `AS_FunctionTable_*` 与 summary/CSV；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:15-43`、`...:96-133` 则直接依赖 `UASFunction` 路径和 `FAngelscriptModuleDesc` 做导航，没有复用任何离线产物。 |
| 优点 | 当前每条链路都能按自己的需求裁剪输出格式：调试器拿 JSON，docs 拿 `.hpp`，UHT 拿 shard/CSV，导航直接跳文件，局部实现简单。 |
| 不足 | 同一语义事实被四套代码分别推导，导致 drift 风险和重复成本都很高。更关键的是，因为没有 canonical symbol graph，调试器无法按 revision/delta 增量同步，docs/UHT 也很难声明“本次构建的事实版本”与 runtime `DebugDatabase` 是否一致。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 用同一个 `FUnLuaIntelliSenseGenerator` 作为 Editor 常驻 producer，commandlet 也复用同一个输出根与同类 `SaveFile()` 语义；资产增量更新与批处理导出不是两套 schema。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Public/UnLuaIntelliSenseGenerator.h:22-65`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`.../UnLuaIntelliSenseGenerator.cpp:148-186`；`.../UnLuaIntelliSenseGenerator.cpp:222-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:29-114`；`.../UnLuaIntelliSenseCommandlet.cpp:117-133` | 先统一 symbol producer，再允许 Editor / commandlet 作为不同触发方式复用同一 contract。 |
| puerts | Inspector discovery 被限制在 `V8Inspector`/`V8InspectorChannel` 和 `/json/list`、`/json/version` 这些极小的协议面内；每个 websocket 连接对应一个 channel/session，运行时 discovery 不去承担额外的离线语义导出职责。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-73`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-117`；`.../V8InspectorImpl.cpp:319-345`；`.../V8InspectorImpl.cpp:452-539` | discovery/protocol 层应该只消费 canonical state，而不是自己再生成一套平行语义事实。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增一层统一的 `ToolchainSymbolGraph`，把 `DebugDatabase`、docs、UHT 摘要和导航都改成“消费同一符号图的不同视图”，而不是继续各自遍历引擎状态。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime` 或独立 `Toolchain` 目录新增 `FAngelscriptToolchainSymbolGraph` / `FAngelscriptSymbolNode`，统一表达 `Type`、`Function`、`Property`、`Namespace`、`SourceLocation`、`Doc`、`Keywords`、`BindingCoverage` 等字段，并附 `Revision`。 2. 先把 `SendDebugDatabase()` 改成“从 graph 序列化”为现有 JSON schema，保留 `DebugDatabase` / `DebugDatabaseFinished` 消息不变；同时扩展 `FAngelscriptRequestDebugDatabase`，新增可选 `KnownRevision`、`RequestedDomains`，旧客户端不给字段时仍走全量快照。 3. 把 `DumpDocumentation()` 的类型/函数/属性收集阶段替换成读取 graph，只保留 `.hpp` 渲染层；把 `AngelscriptFunctionTableExporter` 的 `Direct/Stub/Skipped` 结果并入 graph 的 `BindingCoverage` overlay，而不是再形成第四套事实源。 4. 为 `SourceCodeNavigation` 增加 `NavigationIndex` 读取层，让 `GoToDefinition` 和编辑器跳转优先使用 graph 中的 `SourceLocation`，只在 graph 不可用时回退到当前 `UASFunction` / `FAngelscriptModuleDesc` 路径。 5. 在 graph 旁边新增轻量 manifest，写出 `Revision`、producer 时间戳与各视图输出路径，让 CI、IDE 扩展和 `RequestDebugDatabase` 都能先验证“是不是同一批事实”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptToolchainSymbolGraph.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptToolchainManifest.*` |
| 预估工作量 | L |
| 架构风险 | 如果 graph 一开始就试图覆盖所有调试、文档、导航和 UHT 字段，改造面会过大；应先用“graph 只承载共享核心字段，现有视图继续保留私有扩展”的方式分阶段收敛。 |
| 兼容性 | 可增量实施。第一阶段只增加 graph 和 manifest，不删现有 `.hpp`、`DebugDatabase` JSON、`AS_FunctionTable_*` 或直接文件跳转路径；旧 IDE/VS Code 扩展仍可按当前协议工作。 |
| 验证方式 | 1. 同一轮脚本编译后同时导出 `DebugDatabase`、docs 和 UHT summary，校验三者的 `Revision` 一致。 2. 构造一个只改 `ToolTip`/`ScriptKeywords` 的变更，确认 graph revision 递增且 docs/debug database 同步更新。 3. 构造一个只改 `BlueprintCallable` 绑定覆盖率的变更，确认 graph 的 `BindingCoverage` 更新，而 docs/导航节点保持稳定。 4. 在 graph 缺失时验证旧路径仍能生成 `DebugDatabase` 和 `.hpp`，证明兼容 fallback 生效。 |

### Arch-DT18：StaticJIT 的调用点 fallback 够细，但启用/失效边界仍是单一 `PrecompiledDataGuid + FJITDatabase`，导致任何 ABI 漂移都会整体禁用 transpiled code

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | StaticJIT 覆盖范围、fallback 边界、失效粒度是否能按模块/函数收束 |
| 当前设计 | 生成期和调用点层面已经有较细 fallback：系统调用会在 `Custom -> Native -> Pointer -> Dynamic` 间退化，脚本函数调用也会在 `DirectCall` 与 `DynamicCall` 间切换；但到了加载期，JIT 是否可用仍由全局 `PrecompiledDataGuid` 与全局 `FJITDatabase` 决定，一处 mismatch 就整包关闭 transpiled code。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:66-75` 的 `CompileFunction()` 只是把函数塞进全局 `FunctionsToGenerate`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp:148-168` 显示系统函数调用按 `CustomCall`、`NativeCall`、`PointerCall`、`DynamicCall` 逐级回退，`.../AngelscriptBytecodes.cpp:1177-1194` 说明脚本函数调用也能在目标未生成时退回 `WriteDynamicCall()`；但 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h:74-80` 的 `FStaticJITCompiledInfo` 只有一个 `PrecompiledDataGuid`，`.../StaticJITHeader.cpp:25-35` 还用 `checkf(ActiveInfo == nullptr, "Only one angelscript static JIT info can be compiled in!")` 把 compiled info 限定为单实例；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h:18-42` 与 `.../AngelscriptStaticJIT.cpp:24-39` 把所有 JIT 函数和 lookups 收进全局 singleton `FJITDatabase`，`Clear()` 会清空全部条目；`.../AngelscriptStaticJIT.cpp:3582-3607` 明明已经按 module 写 `FJITFile`，但 `...:3683-3695` 最终只额外生成一个全局 `AngelscriptJitInfo.jit.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1550-1556` 在 `PrecompiledDataGuid` 不匹配时直接 `FJITDatabase::Get().Clear()`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2381-2434` 则在 finalize 阶段一次性回填所有 function/type/global/property lookup。 |
| 优点 | 这套设计安全且直接：局部调用点能尽量使用 native/custom form，而一旦编译期和运行期 ABI 不一致，又能通过全局关停避免半失效的 JIT 代码混入运行时。 |
| 不足 | 失效粒度明显粗于生成粒度。即便改动只影响单个 module、单个 property offset 或少数函数签名，当前也会把整个 JIT corpus 一次性清空；这让 `StaticJIT` 虽然在函数内支持 hybrid fallback，却无法在模块/函数启用层面做 selective fallback。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense 输出按 `ModuleName/FileName.lua` 组织，资产增量更新只改受影响文件，`SaveFile()` 也只在内容变化时写盘；commandlet 沿用同样的文件粒度。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:143-167`；`.../UnLuaIntelliSenseGenerator.cpp:222-245`；`.../UnLuaIntelliSenseGenerator.cpp:248-290`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:63-99`；`.../UnLuaIntelliSenseCommandlet.cpp:117-133` | 虽然不是 JIT，但它证明工具链失效可以按 module/file 粒度收束，而不必一处变化就重建全部产物。 |
| puerts | `V8Inspector` 以 `CtxGroupID` 和 websocket handle 建立独立 session/channel，活跃状态保存在 `V8InspectorChannels[Handle]`，不是全局单会话模型。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:299-308`；`.../V8InspectorImpl.cpp:372-379`；`.../V8InspectorImpl.cpp:500-555` | 这里借鉴的是“把运行时有效性绑定到最小单元”的模式，而不是功能对等的 JIT 方案；当前 StaticJIT 也应把启停边界从全局 singleton 往 unit/session 级收缩。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有全局 `PrecompiledDataGuid` 作为最终保险丝，但新增 per-module/per-function 的 `JIT Unit Manifest` 和选择性注册路径，让 mismatch 先局部降级，再全局兜底。 |
| 具体步骤 | 1. 在 `WriteOutputCode()` 中基于已有 `FJITFile` 单元新增 `CompiledUnitInfo` 输出，至少记录 `ModuleName`、覆盖的 `FunctionId`、引用的 `Type/Property/Global` hash、生成时 `PrecompiledDataGuid`；继续保留现有 `AngelscriptJitInfo.jit.cpp` 作为全局兜底信息。 2. 扩展 `FJITDatabase`，新增 `RegisterUnit(UnitId, Functions, Lookups)`、`DisableUnit(UnitId)`、`GetDisabledUnits()`，避免 `Clear()` 成为唯一控制面。 3. 在 `FAngelscriptEngine` 载入 precompiled data 时，先按 unit 比较 manifest 与运行时引用签名；仅把匹配的 unit 注册进 `FJITDatabase`，不匹配 unit 标记为 `InterpreterOnly` 并记录原因。 4. `PrepareToFinalizePrecompiledModules()` 保持现有全局 finalize 路径作为 fallback，但优先走 unit-scoped lookup patching；只有 manifest 缺失或 compare 失败过多时才回退到当前整包禁用。 5. 将 unit 级诊断写入日志和 toolchain manifest，后续让 debug/IDE 端可看到“哪些 module/function 退回解释器”，而不只是看到一条全局 warning。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITUnitManifest.*` |
| 预估工作量 | L |
| 架构风险 | unit 级 compare 若定义不稳，会出现“本应禁用却误判为可用”的高风险路径；必须先让 manifest 覆盖所有影响 ABI 的引用，再允许 selective enable。 |
| 兼容性 | 可渐进迁移。没有 `CompiledUnitInfo` 的旧 JIT 产物仍按当前全局 `PrecompiledDataGuid` 逻辑处理；只有新产物才启用 unit 级 compare 与局部降级。对脚本语义保持向后兼容，变化主要体现在性能与诊断粒度。 |
| 验证方式 | 1. 人工制造单个 module 的 property offset 变化，验证只有该 unit 被禁用，其余 unit 仍保留 `FJITDatabase` 条目。 2. 构造一个只影响系统函数 native form 的变化，验证调用点仍可在 unit 内回退到 `DynamicCall`，不会触发全局关停。 3. 用旧版 `AngelscriptJitInfo.jit.cpp` 产物启动，确认仍走现有整包禁用路径。 4. 在 state dump 或日志中核对启用/禁用的 unit 列表与实际运行的 transpiled function 数量一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT17 | 调试数据库、文档、UHT 与导航的统一语义来源 | 工具链收敛 + canonical symbol graph 新增 | 高 |
| P1 | Arch-DT18 | StaticJIT 启停粒度、局部降级与全局失效边界 | JIT 激活合同扩展 + selective fallback | 高 |

---

## 架构分析 (2026-04-08 17:20)

### Arch-DT19：`DebugServer V2` 的 transport 只保证“消息入队”，没有保证“整包送达”，remote attach 在慢链路下缺少完整性护栏

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 远程调试链路完整性、慢链路/高延迟下的协议稳定性、多 IDE 前端的 transport 健壮性 |
| 当前设计 | `V2` 仍是自定义 length-prefix 二进制包，但 send queue 只保存整包 `Buffer`，不记录已发送偏移；暂停后脚本线程还会反复调用 `ProcessMessages()`，把“恢复执行”押在同一条 socket pump 上。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:86-93` 定义 envelope 只有 `MessageType + Body`；`.../AngelscriptDebugServer.h:648-687` 的 `SendMessageToAll/SendMessageToClient` 直接把完整 `Buffer` 放进 `QueuedSends`；`.../AngelscriptDebugServer.h:703-711` 的 `FQueuedMessage` 只有 `Buffer` 和 `FirstTry`，没有 `BytesSent` 或 continuation state；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:724-748` 只要队首消息 10 秒内没送完就断开 client；`.../AngelscriptDebugServer.cpp:667-699` 的 `PauseExecution()` 在暂停期间循环 `ProcessMessages()`；`.../AngelscriptDebugServer.cpp:2845-2859` 的 `TrySendingMessages()` 调一次 `Client->Send()` 就直接 `Queue.RemoveAt(0)`，没有根据 `BytesSent` 保留剩余字节。 |
| 优点 | 实现非常轻，局域网+单前端场景里延迟低，现有 VS Code 扩展也不需要维护复杂的 ACK/重传逻辑。 |
| 不足 | 按 `FSocket::Send` 可能出现 partial send 的常见 socket 语义推断，当前实现一旦遇到慢链路、代理转发或发送窗口收缩，就可能把半包当整包移出队列，导致后续 envelope 边界错位；而暂停态又依赖同一 transport 继续泵消息，这会把“网络偶发抖动”放大成“调试会话失效或脚本线程长时间停住”。这类风险在本机回环上不明显，但对远程调试和多 IDE 前端桥接是硬边界。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 每个 websocket 连接创建一个 `V8InspectorChannelImpl` 和对应 `V8InspectorSession`；消息发送走 `Server.send(..., websocketpp::frame::opcode::TEXT)`，由 websocket 层维护 frame 边界，插件侧不手写 partial-send 状态机。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-555` | 把“整帧可靠送达”和“session 生命周期”下沉到成熟 transport 层，插件只关心调试语义。 |
| UnLua | 调试原语直接在进程内操作 `lua_State`：`GetStackVariables()` 用 `lua_getlocal/lua_getupvalue` 取值，`GetLuaCallStack()` 用 `lua_getstack/lua_getinfo` 组装堆栈，系统错误时 `UnLuaModule` 直接遍历所有 env 打印 callstack。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-721`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaModule.cpp:190-209` | 如果 runtime 调试原语本身可独立复用，transport 就可以做薄，甚至由宿主 IDE/桥接层自行选择更稳妥的传输方式。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把现有 `V2` transport 做成“可正确处理 partial send 的可靠二进制传输”，再决定是否额外桥接到 DAP/CDP；第一阶段不改消息号和 body layout，只修 transport 语义。 |
| 具体步骤 | 1. 将 `FQueuedMessage` 扩成 `Buffer + SendOffset + MessageKind + EnqueueRevision`，`TrySendingMessages()` 只有在 `SendOffset == Buffer.Num()` 时才出队；若 `Send()` 返回 `BytesSent < Remaining`，保留剩余部分，下次继续发送。 2. 为 `ProcessMessages()` / `PauseExecution()` 引入独立的 transport pump 或最小化的 `IDebugTransportPump`，暂停态等待的是“session state 改变”，而不是在脚本线程里持续承担所有 socket I/O。 3. 给 transport 增加 `LastProgressTime`、`ConsecutiveWouldBlockCount`、`DroppedBytes`、`DisconnectReason` 统计，替换当前“10 秒未清空队首就断开”的黑盒策略；对只读订阅 client 和控制 client 分开配额。 4. 在保持现有 `EDebugMessageType` 不变的前提下，为新前端额外加一个轻量 `TransportHello` 或 `Capabilities` 首包，声明 `supportsReliablePartialSend`, `supportsReadonlyMirror`, `supportsProgressiveSnapshot`；旧前端无感。 5. 在自动化测试里加入 fake socket / throttled socket，显式覆盖 `BytesSent < Buffer.Num()`、pause 状态下断线恢复、两个 client 一个慢一个快的混合场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/IAngelscriptDebugTransportPump.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugSocketPump.*` |
| 预估工作量 | M |
| 架构风险 | transport 变成“真正可靠发送”后，慢 client 的 backlog 会更真实地暴露出来，需要同时定义丢包/降级策略，否则只是把原先的 silent truncation 变成长期积压。 |
| 兼容性 | 线协议可保持完全兼容：消息号、包头和 body 顺序都不变；变化只在 server 内部如何管理 send queue。现有 VS Code 扩展不需要同步升级。 |
| 验证方式 | 1. 新增单元测试，模拟 `Send()` 每次只发送前 16/64/256 字节，验证 client 仍能正确解包完整 envelope。 2. 在暂停态运行断点/单步 smoke test，验证慢 client 不会让 `PauseExecution()` 永久卡住。 3. 用两个 client 做混合测试：一个持续拉取 `DebugDatabase`，另一个执行单步；验证控制会话不被镜像订阅拖慢。 4. 在局域网和高延迟链路上记录 `DisconnectReason` 与 backlog 指标，确认不再出现无解释断连。 |

### Arch-DT20：`StaticJIT`、预编译缓存、文档导出和 UHT 生成目前是互斥执行相位，JIT debug metadata 没有进入统一工具链闭环

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | StaticJIT 的真实运行形态、JIT fallback 的验证环境、UHT/Docs/Debug 是否属于同一条工具链管线 |
| 当前设计 | 当前所谓 `StaticJIT` 本质更接近 AOT transpile 相位：`CompileFunction()` 只在生成模式里收集函数并输出 C++；Editor 构建用 `AS_SKIP_JITTED_CODE` 直接跳过生成代码的编译使用，预编译运行时又默认不开 `DebugServer`。JIT 代码虽然会写入调用栈行号，但这份 metadata 没有与 docs/UHT/debugger 共用一个 revision/profile。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h:8-10` 在 `WITH_EDITOR` 下直接 `#define AS_SKIP_JITTED_CODE`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:66-79` 的 `CompileFunction()` 只有 `bGenerateOutputCode` 分支，其他路径 `check(false)`；`.../AngelscriptStaticJIT.cpp:337-344` 会把 `ScriptFunction->GetLineNumber(...)` 写入 `SCRIPT_DEBUG_CALLSTACK_LINE`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h:49,337-339` 也说明非 Shipping 仍保留 JIT callstack 宏；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1427-1457` 显示 `bUsePrecompiledData` 只在非 Editor、非 development 场景启用，且 `DebugServer` 仅在 `!bUsePrecompiledData || bScriptDevelopmentMode` 时创建；`.../AngelscriptEngine.cpp:1445-1446` 在生成预编译/JIT 时强制 `asEP_BUILD_WITHOUT_LINE_CUES` 并挂上 `StaticJIT`；`.../AngelscriptEngine.cpp:1573-1589` 在生成 JIT 代码和 `PrecompiledScript.Cache` 后直接走 `bForcedExit`；`.../AngelscriptEngine.cpp:2224-2227` 的 `dump-as-doc` 也单独导出文档后退出；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-79,166-206` 则在另一个编译期相位写出 `AS_FunctionTable_*` summary/CSV。 |
| 优点 | 每个相位都很清晰：UHT 负责函数表、generate run 负责 `AS_JITTED_CODE` 和 `PrecompiledScript.Cache`、`dump-as-doc` 负责 `.hpp` 文档，发布态也能把动态工具面压到最小。 |
| 不足 | 这套切法让“可调试的开发态”和“真实使用 JIT/precompiled 的运行态”天然分离。结果是：JIT 覆盖率与 fallback 多数只能在离线生成或非 Editor 运行时暴露，而日常 IDE/debug workflow 主要发生在另一套 profile；即便 JIT 已经有 `SCRIPT_DEBUG_CALLSTACK_LINE`，它也没有和 `DebugServer`、docs、UHT summary 形成同一份可验证合同。工具链 producer 因此各自输出、各自退出，缺少一次构建/验证能贯通 JIT + Docs + UHT + 导航的统一 profile。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | Inspector 在 runtime context 创建时直接完成 `contextCreated()`、HTTP discovery、websocket accept；调试 attach 依附同一运行期，不需要切到另一条“代码生成后退出”的专用相位。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:282-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-589` | 把“优化执行”和“可观察调试”放在同一 runtime profile 上，至少能让 attach、callstack、transport 验证发生在真实运行环境。 |
| UnLua | runtime 调试原语对任何 `lua_State` 都可用；同时 Editor 常驻 `FUnLuaIntelliSenseGenerator` 与 commandlet 共享同一 `Intermediate/IntelliSense` 输出根和 `SaveFile()` 语义，不需要把 IDE 工件拆成互斥相位。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-721`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55,148-186,222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:21-27,117-133` | 可以允许不同 trigger（Editor、commandlet、runtime）复用同一工具链合同，而不是让每个 trigger 都变成互斥的专用产线。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把当前“生成即退出”的多相位流程抽象成显式 `ToolchainProfile`：保留现有产物与行为，但新增一个可组合的 `PrecompiledValidate/ReadonlyInspect` 验证剖面，让 JIT metadata、docs 和 UHT summary 至少共享同一 revision。 |
| 具体步骤 | 1. 新增 `FAngelscriptToolchainProfile` 或等价配置层，明确区分 `EditorInspect`、`PrecompiledGenerate`、`PrecompiledValidate`、`ShippingRuntime` 四类 profile；第一阶段只是把现有分支条件从隐式布尔组合收敛成显式 profile，不改默认行为。 2. 在 `PrecompiledGenerate` 保持当前 `WriteOutputCode()` + `PrecompiledScript.Cache` 路径，但额外输出 `JitDebugMap.json` 或 manifest fragment，把 `PrecompiledDataGuid`、函数 `FunctionId`、`SCRIPT_DEBUG_FILENAME`、`SCRIPT_DEBUG_CALLSTACK_LINE`、fallback mode、module 名写出来。 3. 新增一个 `PrecompiledValidate` commandlet/standalone automation 模式：允许加载 `PrecompiledScript.Cache` 和已编译 JIT，不开启热重载/写操作，但允许只读 `DebugServer` 或最小 callstack/diagnostics 端点，用来验证真实 precompiled+JIT 运行时是否仍可诊断。 4. 让 `dump-as-doc` 与 `AngelscriptFunctionTableCodeGenerator` 把输出路径和 revision 追加进同一 toolchain manifest；不要求它们同进程执行，但要求产物在 revision 上可 join。 5. 在 CI 中加入“生成 -> 验证 -> 导出 docs/UHT 摘要”的串联任务，显式检查 manifest 是否同时包含 `jit/docs/uht/navigation` 四类节点；Editor 本地工作流继续沿用旧路径，只把 manifest 当附加产物。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptToolchainProfile.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptToolchainManifest.*` |
| 预估工作量 | L |
| 架构风险 | 如果过早尝试让 `PrecompiledValidate` 完全等价于开发态调试，会把当前简单 profile 搞复杂；第一阶段应坚持“只读观察 + revision 对齐”，不要同时引入热重载、资产写回和 Editor 交互。 |
| 兼容性 | 完全可以增量实施：现有 `as-generate-precompiled-data`、`dump-as-doc`、UHT exporter 和默认 `DebugServer` 行为都保留；新增的是 profile 声明、manifest 和验证模式，不会打断当前脚本用户流程。 |
| 验证方式 | 1. 跑一次 `PrecompiledGenerate`，确认仍生成 `AS_JITTED_CODE`、`PrecompiledScript.Cache`，同时多出 `JitDebugMap/manifest`。 2. 用 `PrecompiledValidate` 启动并 attach 只读 client，确认至少能拿到 callstack/diagnostics 而不会进入热重载路径。 3. 对比同一轮构建的 `PrecompiledDataGuid`、JIT manifest revision、`AS_FunctionTable_Summary.json` 和 docs manifest revision，确认它们能 join 到同一版本。 4. 构造一个 fallback 到 `DynamicCall` 的函数，验证 generate/profile/validate 三个阶段对同一 `FunctionId` 给出一致诊断。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT19 | remote transport 完整性、partial send、pause 态稳定性 | transport 可靠性修复 + 会话泵分层 | 高 |
| P1 | Arch-DT20 | JIT/预编译/Docs/UHT 的相位切分与统一验证 profile | 工具链编排收敛 + 验证剖面新增 | 高 |

---

## 架构分析 (2026-04-08 17:30)

### Arch-DT21：`DebugServer V2` 没有 request correlation 与错误合同，单前端串行假设被硬编码进协议面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试协议的请求-响应相关性、失败可观测性、并发 IDE/代理扩展能力 |
| 当前设计 | `V2` envelope 只编码 `MessageType + Body`，没有 `RequestId/CorrelationId/Status/ErrorCode`；`Variables`、`Evaluate`、`DebugDatabaseFinished` 这类响应也不回带请求身份，`GoToDefinition` 甚至没有任何回执。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:82-93` 的 `FAngelscriptDebugMessageEnvelope` 只有 `MessageType` 和 `Body`；`.../AngelscriptDebugServer.h:177-193` 的 `FAngelscriptRequestDebugDatabase` 为空消息体，`.../AngelscriptDebugServer.h:416-449` 的 `FAngelscriptVariable/FAngelscriptVariables` 也只有值数据，没有 request token 或状态字段；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:822-826` 收到 `RequestDebugDatabase` 后直接推送 `SendDebugDatabase()` 与 diagnostics；`.../AngelscriptDebugServer.cpp:1081-1128` 对 `RequestVariables/RequestEvaluate` 无论查找是否成功都只回传裸 `Variables/Evaluate` payload；`.../AngelscriptDebugServer.cpp:1130-1135` 的 `GoToDefinition` 是 fire-and-forget；`.../AngelscriptDebugServer.cpp:2046-2049` 以单个 `DebugDatabaseFinished` 空消息结束流式发送，但没有 revision/request 绑定；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp:39-77` 当前测试也只覆盖 payload round-trip，没有覆盖多请求复用、错误响应或并发相关性。 |
| 优点 | 协议面非常轻，当前 VS Code 扩展按“同一时刻只挂一类请求”的串行使用方式时，实现和调试成本都低。 |
| 不足 | 一旦同一 client 想并发请求两个 `RequestVariables`、在 `DebugDatabase` 流式发送期间再穿插 `Evaluate`，或在远程代理中做 request fan-out/fan-in，服务端返回的消息就只能靠“收到的顺序”和“前端当前期待的消息类型”猜测归属。失败语义也被压扁成空结果、日志或断连，协议自身无法稳定表达“这个请求失败了，为什么失败”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 每个 websocket 连接创建独立 `V8InspectorChannelImpl` / `V8InspectorSession`，消息入口是 `dispatchProtocolMessage()`，出口明确区分 `sendResponse()` 与 `sendNotification()`；同时通过 `/json/list`、`/json/version` 先暴露 discovery 信息。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-157`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:282-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-539` | 即使协议细节交给 CDP，架构上也保留了“请求响应”和“事件通知”的明确边界，便于多前端、代理与标准工具链复用。 |
| UnLua | 不自建 transport，但把调试能力暴露成有明确返回值的同步 API：`GetStackVariables()` 返回 `bool` 表示是否成功，`GetLuaCallStack()` 明确返回完整栈描述，`PrintCallStack()` 只是本地输出辅助。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-721`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:724-732` | 即使没有网络协议，也先把“成功/失败”和“返回数据”定义清楚，避免上层工具只能靠空结果猜测失败原因。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `V2` message type 和 body 结构的前提下，追加一个可选的 `request/response meta-layer`，把请求身份、错误状态和事件通知从裸 payload 中分离出来。 |
| 具体步骤 | 1. 新增 `FDebugMessageHeader`，至少包含 `ProtocolVersion`、`RequestId`、`Flags(IsResponse/IsEvent/IsError)`、`Revision`；第一阶段只对新前端启用，旧前端默认 `RequestId=0` 并继续走当前分支。 2. 为 `RequestVariables`、`RequestEvaluate`、`RequestCallStack`、`RequestDebugDatabase`、`GoToDefinition` 增加对应 response header；`GoToDefinition` 至少回一个 `Ack/Failure`，而不是纯 fire-and-forget。 3. 新增轻量 `FAngelscriptDebugError` 或在现有 response 上追加 `Status/ErrorCode/ErrorMessage`，把“找不到路径”“当前不在暂停态”“source navigation 不可用”“capability 不支持”从空结果升级为结构化失败。 4. `DebugDatabaseFinished` 改成带 `RequestId + SymbolRevision` 的完成事件；后续若引入 delta sync，可继续复用同一头部，不必再改 body。 5. 自动化测试新增 multiplex 场景：同一 client 连续发两个 `RequestVariables`、在 `DebugDatabase` 流式发送中插入 `Evaluate`、以及一个失败的 `GoToDefinition`，验证响应可稳定归属且失败有码。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果直接把 header 强塞进现有 envelope，而不做 version/capability 协商，会立刻打破现有扩展；第一阶段必须由 adapter 或 optional header 承担兼容层。 |
| 兼容性 | 可以完全增量实施。旧 VS Code 扩展继续按 `V2` 裸 payload 使用；新前端通过 capability 或更高 `ProtocolVersion` 启用 `RequestId/Status`。现有消息号不需要改名。 |
| 验证方式 | 1. 保持现有 round-trip 测试通过。 2. 新增多请求相关性测试，确保响应 `RequestId` 与发起方一致。 3. 构造错误路径请求，验证前端可收到结构化错误而不是空 payload。 4. 用代理或录包工具验证 `DebugDatabaseFinished` 与对应请求能正确配对。 |

### Arch-DT22：`StaticJIT` 的 hybrid fallback 以“每个 callsite 即席重建解释器上下文”为核心，缺少函数级降级预算

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | StaticJIT 的函数级覆盖策略、script-to-script fallback 成本、JIT/解释器桥接边界 |
| 当前设计 | `CompileFunction()` 只是把函数加入 `FunctionsToGenerate`；真正到 bytecode lowering 时，若目标脚本函数已在 `FunctionsToGenerate` 中就走 direct call，否则回退到 `WriteDynamicCall()`，而这条 fallback 路径会为每个降级调用点构造新的 `FAngelscriptContext` 并重新 `Prepare()` 目标函数。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:66-75` 的 `CompileFunction()` 只做收集，不建立显式 plan；`.../AngelscriptStaticJIT.cpp:3204-3336` 的 `GenerateCppCode()` 在逐指令生成阶段才决定每条 bytecode 如何落地，且任何未实现 bytecode 最终仍是 `check(bImplemented)`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp:1177-1194` 对脚本函数调用先看 `FunctionsToGenerate.Find(RealFunction)`，找不到就 `WriteDynamicCall(Context)`；`.../AngelscriptBytecodes.cpp:1485-1515` 的 dynamic path 直接创建 `FAngelscriptContext CallContext(CallFunction->GetEngine())` 并 `Prepare(CallFunction)`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3492-3497` 也说明函数是在统一遍历 `FunctionsToGenerate` 时批量生成，降级信息没有提前变成独立工件。 |
| 优点 | 当前 fallback 的正确性优先级很高。即使某个 callsite 不能 direct-JIT，也还能继续执行，避免把整个 caller 直接打回解释器。 |
| 不足 | 这会把函数级质量判断推迟到 callsite 级“临场决策”：一个函数即使包含多个高成本降级点，也仍会被当成“已 JIT”产物输出，运行时反复跨过 `FAngelscriptContext + Prepare()` 这条重桥。由于没有函数级降级预算、callsite reason 和显式 plan，开发者很难判断某个 hybrid function 到底是“只偶尔掉一处慢路径”，还是“外壳被 JIT 了，主体仍大量走解释器”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | Inspector 不是每次消息都重建一套运行时，而是在 `contextCreated()` 后长期持有 `V8InspectorSession`，后续消息都进入这个稳定 session/context group。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-116`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:282-308`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525` | 跨边界桥接更适合依附长期存在的 execution/session handle，而不是每次交互都新建临时桥。 |
| UnLua | 调试取值直接基于现有 `lua_State` 和 stack frame：`lua_getstack`、`lua_getinfo`、`lua_getlocal`、`lua_getupvalue` 在 live runtime 上拉取信息，没有再构造第二套执行上下文。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-669`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:672-721` | 即使做复杂检查，也优先复用已有运行时句柄；这个模式对 JIT/解释器桥接同样成立。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `FunctionsToGenerate` 之前增加显式 `JITPlan`，把“某函数是否值得 hybrid”与“某个 callsite 如何桥接解释器”分成两个决策层；并把 script fallback 从“每次创建新 context”收敛成可复用 bridge。 |
| 具体步骤 | 1. 新增 `FStaticJITPlan` / `FStaticJITCallSitePlan`，在 `AnalyzeScriptFunction()` 之后、`GenerateCppCode()` 之前先分类每个 callsite：`DirectJit`、`NativeFastPath`、`InterpreterBridge`、`InterpreterOnly`。 2. 在 plan 阶段统计每个函数的降级 callsite 数、是否包含未实现 opcode、是否跨 module/import 边界；若超出阈值，就把整个函数标成 `InterpreterOnly` 或 `HybridBridgeHeavy`，避免继续输出“外壳 JIT、内部频繁掉桥”的函数。 3. 为 `InterpreterBridge` 新增可复用的 `FScriptExecutionBridge` 或 thread-local prepared-call 缓存，把 `CallFunction` 解析、参数布局和必要的 `Prepare()` 元数据尽量前移；旧 `WriteDynamicCall()` 保留为兼容 fallback。 4. 将每个函数的 plan 决策与每个降级 callsite 的原因写入附加工件，至少包含 `FunctionId`、`CallSiteIndex`、`FallbackKind`、`Reason`、`EstimatedBridgeCost`；这一层先作为 JIT 自有 sidecar，不要求一次性并入更大的 manifest 体系。 5. 自动化验证里补“高降级密度函数”的策略测试，确保 planner 能把它们整体判回解释器，而不是继续生成低收益 hybrid code。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITPlan.*` |
| 预估工作量 | L |
| 架构风险 | 若 planner 阈值过于激进，可能把目前还能带来收益的 hybrid 函数过早打回解释器；第一阶段应先只做 plan 记录和日志，再逐步启用“按预算整体降级”。 |
| 兼容性 | 可渐进迁移。现有 `WriteDynamicCall()` 和当前 hybrid 行为继续保留为默认兜底；只有当新 planner 可用且 capability 打开时，才启用“整体降级”与 bridge cache。脚本语义不变，变化主要体现在性能与可诊断性。 |
| 验证方式 | 1. 构造一个包含多个 non-direct script call 的函数，验证 planner 能输出 callsite 分类。 2. 构造降级点超阈值的函数，验证其被整体标记为 `InterpreterOnly` 或 `HybridBridgeHeavy`。 3. 回归现有 `WriteDynamicCall()` 路径，确认 planner 关闭时行为不变。 4. 用 profiling/日志比较 bridge cache 开启前后 `Prepare()` 次数和 hybrid function 命中率。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT21 | 调试协议 request/response 相关性、错误合同、多前端并发 | 协议扩展 + 兼容 adapter | 高 |
| P1 | Arch-DT22 | StaticJIT hybrid fallback 成本、函数级降级预算、解释器桥接边界 | JIT planner 新增 + bridge 收敛 | 高 |

---

## 架构分析 (2026-04-08 17:40)

### Arch-DT23：普通断点的持久化模型只保存“模块 + 行号”，热重载后缺少按源码 revision 重绑定的能力

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 普通断点的持久化合同、热重载后的重绑定、远程前端的一致性 |
| 当前设计 | `DebugServer` 在收到 `SetBreakpoint` 时只把请求归一化成某个 `CodeLine`，随后把断点状态保存为 `FFileBreakpoints::Lines`；编译完成后只重新挂接 `Module`，不重新计算源码锚点。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:227-253` 的 `FAngelscriptBreakpoint` 只携带 `Filename`、`LineNumber`、`Id`、`ModuleName`；`.../AngelscriptDebugServer.h:695-720` 的持久化结构只有 `Module + TSet<int32> Lines`，运行时 section cache 还是 `TMap<const char*, TSharedPtr<FFileBreakpoints>>`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:955-1045` 在设置断点时调用 `FindNextLineWithCode()` 做一次性行号吸附，然后仅把 `CodeLine` 放进 `Lines`；`.../AngelscriptDebugServer.cpp:576-602` 命中断点时按 `Section` 指针和 `Line` 查表；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2499-2502` 编译后只调用 `DebugServer->ReapplyBreakpoints()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1197-1219` 的 `ReapplyBreakpoints()` 仅重新查 `Module` 并设置 `hasBreakPoints`；`.../AngelscriptDebugServer.cpp:1222-1238` 只有 `ClearAllBreakpoints()` 才会清空 `SectionBreakpoints`。 |
| 优点 | 状态结构很轻，老前端只要发送文件和行号就能工作，断点设置流程也不依赖额外 manifest。 |
| 不足 | 断点缺少 `SectionPath`、`ResolvedLine`、`SourceRevision` 这类稳定锚点。脚本热重载后，旧断点最多只会重新指向“同名模块上的同一数字行”，而不会重新验证该行是否仍代表同一语义位置；`SectionBreakpoints` 还用原始 `const char*` 做 key，使 section cache 天然不是 reload-stable 身份。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 每个连接创建独立 `V8InspectorSession`，所有前端消息都直接交给 `dispatchProtocolMessage()`；同时用 `/json/list`、`/json/version` 暴露 discovery 入口。这里可以据接入方式推断，脚本 identity 与断点生命周期主要交给 `V8 Inspector / CDP` 语义管理，而不是在插件里自建 `filename -> lines` 缓存。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:300-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477` | 断点绑定应有独立的脚本身份层和 discovery 层，而不是把“用户意图”和“当前命中的行号”压成同一份状态。 |
| UnLua | 调试信息始终从 live runtime 拉取：`GetStackVariables()` 通过 `lua_getstack`、`lua_getinfo`、`lua_getlocal`、`lua_getupvalue` 读当前栈帧，`GetLuaCallStack()` 每帧输出实时 `source` 和 `currentline`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-669`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:672-685` | 即使不引入标准协议，也应先让“当前源码位置”成为可重建的 runtime 事实，再由前端决定如何展示或持久化。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“用户请求的断点意图”和“当前编译产物中的实际绑定结果”拆开，新增 revision-aware 的断点绑定记录，并在每次 compile/reload 后重跑绑定。 |
| 具体步骤 | 1. 新增 `FBreakpointBindingRecord`，把 `RequestedFile`、`RequestedLine`、`BreakpointId` 与 `ResolvedModule`、`ResolvedSection`、`ResolvedLine`、`SourceRevision` 分开存；现有 `FAngelscriptBreakpoint` 保留原字段，再增量追加可选字段。 2. 编译管线在模块完成后产出轻量 `SourceRevision`，至少可由相对脚本路径列表加内容 hash 组成；`ReapplyBreakpoints()` 改为 `RebindBreakpointsForReload()`，先清空 `SectionBreakpoints`，再针对最新 revision 重新调用 `FindNextLineWithCode()`。 3. 如果断点因代码移动被吸附到新行，则继续复用现有 `SetBreakpoint` changed message 回传新行号；如果本轮无法解析，则把状态标成 `PendingUnbound`，而不是直接把语义丢失成“还留在旧数字行”。 4. `CallStack` 和后续 `DebugDatabase` 增量事件也追加同一组 `ResolvedSection/SourceRevision`，让多 IDE 前端、代理或录包工具能确认自己看到的是哪一版脚本。 5. 自动化测试补三类场景：同文件前插空行、函数被拆分到新 section、compile 后 section 指针变化但模块名不变，验证断点能重绑且 `SectionBreakpoints` 不残留旧 cache。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/BreakpointBindingRecord.*` |
| 预估工作量 | M |
| 架构风险 | 如果把 revision 粒度做得过粗，会出现“模块没变但 section 已重排”仍无法正确重绑；如果做得过细，又可能让普通 compile 产生过多断点刷新事件。 |
| 兼容性 | 可以完全增量实施。旧前端继续只发 `Filename + LineNumber`；新字段只作为可选能力附加。即使旧前端不理解 `PendingUnbound`，也仍可退回当前行为。 |
| 验证方式 | 1. 现有协议 round-trip 测试继续通过。 2. 新增热重载断点测试：设置断点、插入若干非代码行、重编译，验证断点回写到新行。 3. 新增 section 重建测试：确认 reload 后 `SectionBreakpoints` 被清空并按新 section 重新缓存。 4. 录制双前端会话，验证不同前端都能看到同一 `SourceRevision`。 |

### Arch-DT24：`StaticJIT` 的源码锚点被压缩成 `ModuleName + LineNumber`，JIT 执行态无法向导航和 IDE 前端提供精确 source identity

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | JIT 调试元数据的源码精度、JIT 与导航/文档/IDE 工具链的一致性 |
| 当前设计 | `StaticJIT` 为每个执行帧保存一份轻量 `FScopeJITDebugCallstack`，但其中只有 `Filename`、`FunctionName`、`LineNumber`；生成阶段又把 `Filename` 固化成 `File.ModuleName`，因此 JIT 栈帧天然只有模块级身份，没有真实 section/file 身份。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h:49` 把 `AS_JIT_DEBUG_CALLSTACKS` 定义为 `!UE_BUILD_SHIPPING`；`.../StaticJITHeader.h:194-218` 的 `FScopeJITDebugCallstack` 只存 `Filename`、`FunctionName`、`LineNumber`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:337-343` 的 `DebugLineNumber()` 只更新行号；`.../AngelscriptStaticJIT.cpp:3612-3618` 在生成文件头时把 `SCRIPT_DEBUG_FILENAME` 固定为 `File.ModuleName`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:5106-5118`、`.../AngelscriptEngine.cpp:5637-5644` 读取 JIT 栈时也只输出 `DebugStack->Filename` 和 `LineNumber`；对照解释器路径，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1467-1474` 会用 `Context->GetLineNumber(..., &SectionName)` 把 `SectionName` 和 `ModuleName` 分开回传；同时 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1436-1446` 在生成预编译/JIT 时又强制 `asEP_BUILD_WITHOUT_LINE_CUES = 1`。 |
| 优点 | 当前 JIT 调试元数据非常便宜，不需要把完整 line cue 继续带入运行时，也不会明显放大生成代码体积。 |
| 不足 | 一旦一个模块包含多个脚本 section，或未来需要把 JIT 执行位置映射回真实文件，`ModuleName + LineNumber` 就不够用了。它既不能稳定对齐 `GoToDefinition`、`Docs`、`DebugDatabase`，也不能支撑 JIT-aware 的远程调试前端；`UE_BUILD_SHIPPING` 下整套 JIT callstack 还会被编译掉，导致 release-style 验证路径没有任何精确 source anchor。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `GetLuaCallStack()` 对每一帧都直接输出 `ar.source` 和 `ar.currentline`，`GetStackVariables()` 也总是围绕 live 栈帧做查询，源码身份是 frame-level 的，而不是 module-level 的。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:672-685`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-669` | 即使运行时信息做得很轻，也应保证每个调试帧都能回到真实 source，而不是只回模块名。 |
| puerts | `contextCreated()` 后通过 `V8InspectorSession` 持续承载调试消息，并用 `/json/list`、`/json/version` 暴露可发现入口。这里可以据接入方式推断，前端消费的是 `Inspector/CDP` 的脚本 URL 语义，而不是插件自造的 module-only 字段。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:300-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477` | JIT/source identity 应先变成可发现、可关联的稳定对象，再由不同 IDE 前端映射成各自 UI。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 维持当前轻量 JIT 栈帧结构，但把真实源码锚点外提成 `JitSourceMap`，让运行时保留紧凑 `SourceId`，工具链再把它解析回 section/file。 |
| 具体步骤 | 1. 在 `GenerateCppCode()` 阶段利用 `asCScriptFunction::GetLineNumber(programPosition, &SectionIdx)` 与 `GetScriptSectionName()` 所暴露的 section 信息，为每个 `FunctionId` 生成 `BCOffset -> {SectionPath, Line}` 的紧凑表，而不是只写 `SCRIPT_DEBUG_CALLSTACK_LINE`。 2. 将 `FScopeJITDebugCallstack` 扩展为 `SourceId + LineNumber + FunctionId`，保留旧 `Filename` 字段作为兼容 fallback；生成代码把 `SCRIPT_DEBUG_FILENAME` 从模块名改成稳定 `SourceId` 或按 section 切换的常量。 3. 在 `AS_JITTED_CODE` 旁新增 `JitSourceMap.json` 或等价 sidecar，并把 `SourceId -> SectionPath/ModuleName` 与 `FunctionId` 的映射登记进去；`GetAngelscriptExecutionPosition()`、后续 debug adapter、日志格式化优先读这份映射，只有缺失时才退回旧 `ModuleName`。 4. 为 `PrecompiledValidate` 或非 Editor 的只读诊断 profile 增加 `bKeepCompactJitSourceAnchors` 开关，允许在不恢复完整 line cues 的前提下继续保留最小 source identity。 5. 让 `DebugDatabase` 或未来 manifest 在同一 revision 下引用 `JitSourceMap`，这样 `GoToDefinition`、docs、coverage、JIT callstack 就能 join 到同一套源码身份。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/JitSourceMap.*` |
| 预估工作量 | L |
| 架构风险 | 如果把 source map 做成每条 bytecode 都单独持久化，生成产物会明显膨胀；需要优先做 section 切换点压缩，而不是逐指令原样导出。 |
| 兼容性 | 可分阶段落地。第一阶段只新增 `JitSourceMap` sidecar 与 `SourceId`，旧前端继续读取现有 `Filename`；没有 sidecar 的旧构建仍按当前模块级定位工作。 |
| 验证方式 | 1. 构造一个含多个 script section 的模块，比较解释器 `CallStack` 与 JIT `GetAngelscriptExecutionPosition()` 是否都能回到真实 section 路径。 2. 生成预编译/JIT 后检查 `JitSourceMap` 与 `FunctionId`、`PrecompiledDataGuid` 是否一致。 3. 在关闭 sidecar 的兼容模式下回归旧行为，确认不破坏现有生成链。 4. 新增日志/调试测试，验证 `FormatAngelscriptCallstack()` 不再只输出模块名。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT23 | 普通断点的 revision-aware 重绑定、reload 后源码锚点稳定性 | 调试状态模型细化 + 重绑定服务新增 | 高 |
| P2 | Arch-DT24 | StaticJIT 的 source identity、JIT 与导航/IDE 的精确对齐 | JIT 元数据扩展 + sidecar source map | 中高 |

---

## 架构分析 (2026-04-08 17:51)

### Arch-DT25：`DebugDatabase` 的线协议会随 `WITH_EDITOR` 与旧版 VS Code 兼容分支变形，调试前端合同实际上被单一客户端历史锁定

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试数据库 wire contract 的稳定性、多 IDE 前端兼容性、自定义协议向 DAP/CDP bridge 的可移植性 |
| 当前设计 | `RequestDebugDatabase` 本身没有协商字段，server 只先发一个固定 `Version = 5` 的 `DebugDatabaseSettings`，随后把实际 schema 直接按当前 build 条件拼出来；同一个 `DebugDatabase` 消息在 Editor / 非 Editor、以及“是否兼容旧版 VS Code 扩展”的分支下会输出不同字段形状。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:177-182` 的 `FAngelscriptRequestDebugDatabase` 为空消息，客户端无法声明自己想要的 schema/domain；`.../AngelscriptDebugServer.h:537-553` 的 `FAngelscriptDebugDatabaseSettings` 只有 5 个语言设置位和固定 `Version = 5`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1499-1505` 只是先发这份 settings；`.../AngelscriptDebugServer.cpp:1599-1617` 把 `keywords/meta` 放在 `#if WITH_EDITOR` 分支里，说明同一函数描述在不同构建里字段集不同；`.../AngelscriptDebugServer.cpp:1731-1756` 对 property payload 还保留了 “TODO: Remove the Flags check ... updated vscode extension versions” 的历史兼容分支，说明 schema 演进直接受某个特定前端版本约束；`.../AngelscriptDebugServer.cpp:2037-2049` 以任意 `UnsentCount > 10` 分块发送并用裸 `DebugDatabaseFinished` 结束，没有在完成消息里附 schema revision 或 domain 信息。 |
| 优点 | 这套做法对当前 VS Code 扩展很省事，server 不需要额外 negotiation，就能把“当前进程里能拿到的最大信息量”直接推过去。 |
| 不足 | 协议层表达的是“今天这台进程此刻愿意给什么”，而不是“所有前端都能稳定理解的合同”。结果是多 IDE 前端、录包代理、DAP/CDP bridge 甚至 CI 录制都无法只靠 `Version = 5` 判断字段是否存在，只能硬编码 build 分支和历史兼容细节。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 先通过 `/json/list`、`/json/version` 暴露 discovery contract，再在每个 websocket 连接上创建独立 `V8InspectorChannel`，把文本协议直接交给 `V8InspectorSession`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:453-477`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:502-525` | 前端先知道“这是哪个协议、有哪些入口”，再进入会话；协议合同不依赖某个 IDE 私下记住 build 宏分支。 |
| UnLua | 不把 IDE 事实塞进 runtime socket，而是让 Editor 生成器和 commandlet 都写入同一 `Intermediate/IntelliSense` 目录，并且只在内容变化时落盘。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:43-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:224-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:108-129` | 把 IDE contract 做成稳定产物后，不同 producer 只是更新同一份事实，不需要在 wire payload 里夹带特定客户端历史兼容。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `DebugDatabase` / `DebugDatabaseFinished` 消息号，但把 payload 切成“稳定 core schema + 可选 editor overlay”，并让客户端在请求时显式声明自己接受哪些 domain。 |
| 具体步骤 | 1. 扩展 `FAngelscriptRequestDebugDatabase`，新增可选 `KnownSchemaVersion`、`RequestedDomains`、`bAcceptEditorOverlay`；旧客户端继续发送空体，server 保持当前行为。 2. 将 `SendDebugDatabase()` 输出拆成两层：`core` 固定包含 `name/return/args/properties/methods` 等基础字段，`editor overlay` 才承载 `keywords/meta/deprecated/tooltips/source augmentation`；即使在非 Editor 构建，也继续返回 `core schemaVersion`，而不是默默变形。 3. 在 `DebugDatabaseSettings` 或首个 chunk 中显式附带 `SchemaVersion`、`AvailableDomains`、`ProducerProfile(runtime/editor-cache/disk-index)`，替换今天“Version = 5 + 靠字段缺失猜测”的做法。 4. 将 `.../AngelscriptDebugServer.cpp:1753-1755` 这类旧版 VS Code 兼容注释迁到 codec/schema 层，用显式 `fieldSince/fieldDeprecatedAfter` 管理，而不是继续把历史前端行为焊死在业务序列化里。 5. 自动化测试新增三组 golden case：`EditorCore+Overlay`、`RuntimeCoreOnly`、`LegacyEmptyRequest`，验证新旧请求都能得到可预期的 schema。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptDebugDatabaseSchema.*`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 如果一步把当前扩展依赖的隐式字段全部改成显式 domain，旧前端容易在没有升级前“看起来连上了但 metadata 少了一半”；因此第一阶段必须保留 legacy 空请求路径。 |
| 兼容性 | 完全可增量实施。旧 VS Code 扩展继续走空 `RequestDebugDatabase`，server 仍可回当前 payload；新前端才启用 domain negotiation 和 `SchemaVersion`。 |
| 验证方式 | 1. 录制现有 VS Code 扩展的 `RequestDebugDatabase` 会话，确认空请求路径字节级保持兼容。 2. 在 `WITH_EDITOR` 与非 Editor 两种 profile 下分别抓包，验证 `SchemaVersion/AvailableDomains` 正确反映字段可用性。 3. 为新前端做最小 smoke test，只请求 `core` domain，确认即使没有 `keywords/meta` 也能稳定建索引。 4. 增加 regression test，覆盖 property payload 的旧版兼容路径与新 schema path。 |

### Arch-DT26：`UHT` 的 `direct/stub` 覆盖率与 `StaticJIT` 的 native-form 覆盖率没有共同键，工具链无法解释“能绑定但为何不能 JIT”

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | UHT 代码生成覆盖率、StaticJIT native-form 覆盖率、工具链可对账性 |
| 当前设计 | `AngelscriptUHTTool` 用 `ClassName + FunctionName + EraseMacro` 统计 `direct/stub` 覆盖率，而 `StaticJIT` 则把 native/custom form 记录在 `GScriptNativeForms` 里，并用 `GetPreviousBind()` 返回的运行时 `asIScriptFunction*` 做 key；两条链路既不共享稳定 ID，也不在同一 profile 下产生产物。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14-22` 定义的 `AngelscriptGeneratedFunctionEntry` 只有 `ClassName`、`FunctionName`、`EraseMacro`；`.../AngelscriptFunctionTableCodeGenerator.cs:81-118` 与 `:167-206` 按 `EraseMacro == "ERASE_NO_FUNCTION()"` 统计 `directBindEntries/stubEntries` 并写出 summary；`.../AngelscriptFunctionTableCodeGenerator.cs:453-476` 说明 UHT 这一侧的决策单位是 header/UHT function；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp:27-40` 的 JIT 侧则维护 `GScriptNativeForms`，key 是 `asIScriptFunction*`；`.../StaticJITBinds.cpp:112-117`、`...:530-534`、`...:703-707`、`...:901-905`、`...:1021-1025` 都只在 `FAngelscriptEngine::bGeneratePrecompiledData` 开启时登记 native form；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:299-305` 与 `:432-433` 说明这个登记还依赖 ambient `GetPreviousBind()` 状态。 |
| 优点 | 两条链路都针对各自阶段做了最省成本的实现：UHT 可以快速生成 shard 和 summary，JIT 可以在生成预编译数据时顺手采集 native form。 |
| 不足 | 这会让“函数表是 direct 还是 stub”与“JIT 最终是 custom/native/pointer/dynamic”变成两本对不上账的账本。开发者和 CI 都无法回答一个关键问题：某个函数明明在 `AS_FunctionTable_Summary.json` 里被记成 direct，为什么 JIT 还是退到了 `PointerCall/DynamicCall`；反过来，某个 stub 又是否真的阻碍了 JIT。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | Editor 监听器与 commandlet 复用同一 `Intermediate/IntelliSense` 输出合同，`SaveFile()` 规则一致，因此“增量生成”和“批量生成”不会制造两套无法 join 的身份体系。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:43-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:224-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:108-129` | 先定义稳定 artifact identity，再允许不同阶段去更新这份事实；覆盖率与产物之间才能真正对账。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `UHT direct/stub` 与 `StaticJIT native-form` 新增共同的 `BindingCoverageId`，先做“可对账”，再做“统一展示”。 |
| 具体步骤 | 1. 扩展 `AngelscriptGeneratedFunctionEntry`，新增稳定 `BindingCoverageId`，建议由 `Module + Class + Function + SignatureHash` 组成；summary/CSV/shard 注释都输出这个 ID。 2. 为 `FAngelscriptBinds::AddFunctionEntry()` 增加可选 `BindingCoverageId` 旁路，优先把这个 ID 传递到运行时绑定层；`GetPreviousBind()` 继续保留为 legacy fallback。 3. 在 `StaticJITBinds` 新增 `RegisterNativeForm(BindingCoverageId, NativeFormKind, bTrivial, CaptureProfile)`，把今天基于 `asIScriptFunction*` 的 native-form 事实旁写成 sidecar；第一阶段仍只在 `bGeneratePrecompiledData` 路径采集，但输出必须能与 UHT summary join。 4. 生成新的 `AS_FunctionTable_Coverage.json` 或等价 sidecar，把 `uhtBindingKind(direct/stub)`、`jitNativeFormKind(custom/native/pointer/dynamic/none)`、`capturedInProfile` 和 `fallbackReason` 放到同一条记录里。 5. 在 CI 和 IDE 侧优先消费这份 coverage sidecar，直接把“UHT direct 但 JIT 无 native form”标成可诊断状态，而不是让开发者手工比对两个文件夹。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptBindingCoverage.*` |
| 预估工作量 | M |
| 架构风险 | `BindingCoverageId` 的 canonical 规则如果对 overload、模板实例或接口函数处理不稳，会把“同名不同签名”错误合并；第一阶段必须先把 signature hash 规则和 legacy fallback 打稳。 |
| 兼容性 | 可增量实施。现有 `AS_FunctionTable_*`、`Summary.json/CSV`、`GetPreviousBind()` 和 `GScriptNativeForms` 全部保留；新 `BindingCoverageId` 与 coverage sidecar 只是附加信息。 |
| 验证方式 | 1. 对同一轮构建同时生成 `AS_FunctionTable_Summary.json` 与新的 coverage sidecar，验证每个 UHT entry 都能 join 到唯一 `BindingCoverageId`。 2. 构造一个 `directBind` 但缺 native form 的函数，确认 coverage sidecar 能明确显示 `uht=direct, jit=dynamic`。 3. 构造一个 interface/stub 函数，确认 coverage sidecar 不会误报为 JIT 缺陷。 4. 回归旧生成链，确认没有 `BindingCoverageId` 的旧产物仍可被当前 runtime 正常使用。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT25 | `DebugDatabase` schema 稳定性、客户端锁定、多 IDE 协商 | 协议合同细化 + domain negotiation | 高 |
| P1 | Arch-DT26 | UHT 覆盖率与 JIT 覆盖率对账、统一 binding coverage 身份 | 工具链合同新增 + sidecar 收敛 | 高 |

---

## 架构分析 (2026-04-08 18:02)

### Arch-DT27：`GoToDefinition` 被建模成“让宿主进程代前端打开本地 IDE”的反向 RPC，无法成为多 IDE 的通用导航合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 定义跳转合同、remote 调试、前端自主导航能力 |
| 当前设计 | `GoToDefinition` 不是“查询定义位置并返回 `SourceLocation`”，而是 client 发来 `TypeName + SymbolName` 后，由宿主进程立即解析并直接调用 `FSourceCodeNavigation`；Editor 侧最终固定 shell-out 到本机 `code --goto`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1130-1135` 收到 `EDebugMessageType::GoToDefinition` 后直接执行 `GoToDefinition(GoTo)`，没有回包；`.../AngelscriptDebugServer.cpp:1335-1369` 在解析到 `UFunction`、`FProperty`、`UClass` 后直接调用 `FSourceCodeNavigation::NavigateToFunction/Property/Class()` 并 `return`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:34-43` 会把 `UASFunction` 直接转成 `OpenFile(Path, Line)`；`.../AngelscriptSourceCodeNavigation.cpp:96-115` 的 `OpenModule/OpenFile` 最终都是 `FPlatformMisc::OsExecute(nullptr, TEXT("code"), ...)`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:706-717` 连 Tools 菜单也把工作区入口写死为 “Open Angelscript workspace (VS Code)” 与 `<Project>/Script`。 |
| 优点 | 对“Editor 与 VS Code 在同一台机器上”的当前主流程非常直接，几乎不需要前端自己实现 source mapping 或 IDE adapter。 |
| 不足 | 这让协议语义变成“请求远端机器替我执行本地 UI 动作”，而不是“给我定义位置”。结果是 remote attach 时导航动作会落在宿主机而不是请求端；新 IDE 前端无法复用同一条消息来渲染自己的 symbol peek / tab / split-view；同时失败语义也被隐藏在 `OsExecute` 与 `FSourceCodeNavigation` 内部，协议层既没有 location，也没有 error payload。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | Inspector 只暴露 `V8Inspector` / `V8InspectorChannel` 接口；收到 websocket 文本后转发给 `V8InspectorSession`，再把 session 产出的文本消息发回前端，同时用 `/json/list`、`/json/version` 做 discovery，没有任何“替前端打开本地 IDE”逻辑。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-65`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:104-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-540` | 调试/导航合同应优先返回可消费的数据对象，UI 动作由前端自己决定。 |
| UnLua | Runtime 调试基元是 `GetStackVariables()`、`GetLuaCallStack()` 这类纯数据 API；即使 `PrintCallStack()` 也是本地调试辅助函数，而不是跨进程 IDE 导航命令。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-685`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:724-730` | 先把 runtime 能回答的事实抽成数据接口，再决定本地 IDE 如何消费，协议面才不会被某个编辑器绑死。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增“definition resolution”数据合同，把本地 `OpenFile/code --goto` 降为 Editor-only consumer；旧 `GoToDefinition` 继续保留为兼容别名。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 新增 `FResolveDefinitionRequest` / `FResolvedDefinition`，字段至少包含 `SymbolId`（可选）、`TypeName`、`SymbolName`、`Path`、`Line`、`Column`、`Kind`、`bResolved`、`ErrorMessage`。 2. 把 `DebugServer.cpp:1288-1374` 的解析逻辑抽到独立 `Toolchain/Navigation/AngelscriptDefinitionResolver.*`，其返回值是结构化 location，而不是直接触发 `FSourceCodeNavigation`。 3. 保留现有 `GoToDefinition` 消息号和行为给旧前端，但内部改成“先 resolve，再在 `WITH_EDITOR` 且启用本地 launcher 时调用 `ISourceEditorLauncher`”；新前端改用 `ResolveDefinition -> ResolvedDefinition`。 4. 将 `AngelscriptSourceCodeNavigation.cpp` 中的 `code --goto` 包进 `ISourceEditorLauncher` 默认实现，便于后续接 Rider/VS 或纯 headless consumer。 5. 在 `AngelscriptDebugProtocolTests.cpp` 增加 `ResolveDefinition` 成功/失败、script-only location、无本地 launcher 三组测试，避免新合同再退回 fire-and-forget。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/Navigation/AngelscriptDefinitionResolver.*`，新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/ISourceEditorLauncher.*`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 一旦把 location 合同公开，纯脚本符号、`UFunction` 符号与热重载类版本的归一规则就需要稳定下来；否则新前端会比旧的“直接打开 VS Code”更早暴露 source identity 不一致问题。 |
| 兼容性 | 可完全增量实施。旧 VS Code 扩展仍可继续发送 `GoToDefinition`，server 继续保留当前本地打开行为；新前端才使用 `ResolveDefinition` 响应。对现有脚本运行时语义无影响。 |
| 验证方式 | 1. 本地 Editor + VS Code 回归：旧 `GoToDefinition` 仍能打开对应文件。 2. 远程/无 GUI smoke test：新 client 能拿到 `Path + Line`，但宿主机不会尝试 `OsExecute("code")`。 3. 构造一个解析失败的 symbol，确认会返回结构化 `ErrorMessage`，而不是静默无事发生。 4. 将 `ISourceEditorLauncher` 替换为测试实现，验证协议层与具体 IDE 解耦。 |

### Arch-DT28：asset literal 写回仍把 IDE extension 当作唯一落盘者，workspace 变更没有本地 authoritative log

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | workspace 写回的 authoritative store、离线工具链、写回事务性 |
| 当前设计 | `OnLiteralAssetSaved()` 先检查是否存在 debug client；若没有就直接弹窗拒绝。若有，则把资产内容转成 `TArray<FString>` 后调用 `ReplaceScriptAssetContent()`，而 runtime 只会把 `ReplaceAssetDefinition` 广播给全部 client，本地没有持久化的 operation log、revision 或 ack。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:121-128` 在没有 debug client 时直接报 “Visual Studio Code extension must be running to save a script literal curve”；`.../AngelscriptEditorModule.cpp:131-331` 在内存里生成 `NewContent` 后直接调用 `FAngelscriptEngine::Get().ReplaceScriptAssetContent(Curve->GetName(), NewContent)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2017-2025` 用 `HasAnyDebugServerClients()` 判断是否有 socket client，`.../AngelscriptEngine.cpp:2028-2035` 的 `ReplaceScriptAssetContent()` 只构造消息并 `SendMessageToAll(EDebugMessageType::ReplaceAssetDefinition, Message)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:568-577` 的 `FAngelscriptReplaceAssetDefinition` 只有 `AssetName + Lines`，没有 `OperationId`、`Revision`、`SourceHash` 或 ack 字段。 |
| 优点 | 当前实现几乎零额外管线成本，直接复用现有 VS Code extension 做“资产 UI -> 脚本源码”回写，Editor 侧逻辑很薄。 |
| 不足 | 这让 live IDE client 变成事实上的唯一持久化落点。没有 client 时资产无法保存；有多个 client 时也没有 authoritative owner 与冲突检测；client 断连或应用失败时，宿主侧既没有 pending operation log，也无法在 commandlet/CI 中重放同一批 workspace 变更。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FUnLuaIntelliSenseGenerator` 在 Editor 初始化后订阅 `AssetRegistry` 事件，统一把结果写入插件本地 `Intermediate/IntelliSense`；`SaveFile()` 只在内容变化时落盘。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:248-290` | 工具链事实先有本地 authoritative artifact，再决定 IDE 是否消费或刷新。 |
| UnLua | 同一套 `IntelliSense` 输出还能通过 `UUnLuaIntelliSenseCommandlet` 在无 IDE、无常驻 Editor 交互的情况下全量重建。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-112`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:117-133` | Editor 增量链与 batch/offline 链共享同一 artifact contract，避免把 live socket 变成唯一生产者。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先为 workspace 写回引入本地 `OperationLog`，再把 debug socket 降为“可选同步通道”而不是唯一落盘者。 |
| 具体步骤 | 1. 新增 `Toolchain/Workspace/AngelscriptWorkspaceOperationLog.*`，把 `AssetName`、`Lines`、`OperationId`、`Timestamp`、`ExpectedSourceRevision` 记录到 `Intermediate/AngelscriptToolchain/Workspace/PendingOps.json` 或分片文件。 2. 修改 `OnLiteralAssetSaved()`：始终先写本地 operation log，再决定是否通过 socket 推送给 client；没有 client 时不再拒绝保存，只是把 op 标记为 `PendingExternalApply`。 3. 保留现有 `ReplaceAssetDefinition` 给旧扩展消费，同时增量新增 `WorkspaceWritebackAck` 或等价事件，允许新前端在成功应用后回写 `OperationId + AppliedRevision`；旧前端没有 ack 时也不阻塞保存。 4. 新增 Editor utility 或 commandlet `-as-apply-workspace-ops`，在无 IDE extension 时也能重放 pending op，把脚本源码或中间 artifact 更新到最新状态。 5. 在后续 toolchain manifest 中只登记 operation log revision 与 apply 状态，而不是继续假定“只要有人连着 VS Code，一切就会自动落盘”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/Workspace/AngelscriptWorkspaceOperationLog.*`，新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Commandlets/AngelscriptWorkspaceApplyCommandlet.*` 或等价工具入口 |
| 预估工作量 | M |
| 架构风险 | 一旦引入 pending op，本地脚本源码与队列状态可能发生漂移；第一阶段至少需要 `ExpectedSourceRevision` 或内容 hash，避免 commandlet 重放覆盖掉后来的人手工修改。 |
| 兼容性 | 可增量实施。旧 VS Code 扩展继续消费 `ReplaceAssetDefinition`；差别只是宿主侧现在总会先记录本地 op，且无 client 时不再直接拒绝保存。旧项目不会因为新增 log 文件而改变脚本运行时行为。 |
| 验证方式 | 1. 在没有任何 debug client 的情况下保存 literal curve，确认会生成 pending op，而不是弹窗失败。 2. 连接旧 VS Code 扩展后再次保存，确认 legacy `ReplaceAssetDefinition` 仍正常发送。 3. 运行 `-as-apply-workspace-ops`，确认离线也能把 pending op 应用到脚本工作区。 4. 模拟 source revision 不匹配，确认工具会报告冲突而不是静默覆盖。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT27 | `GoToDefinition` 的反向 RPC 语义、remote 导航、多 IDE 前端 | 协议合同新增 + 本地 launcher 抽象 | 高 |
| P1 | Arch-DT28 | asset literal/workspace 写回的 authoritative store、离线重放、写回事务性 | 工具链持久化新增 + operation log | 高 |

---

## 架构分析 (2026-04-08 18:16)

### Arch-DT29：`DebugServer V2` 的 transport 没有 trust boundary，remote attach 与 workspace 写操作默认共享同一权限面

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | remote 调试暴露面、连接鉴权、调试域与写操作域的权限隔离 |
| 当前设计 | `DebugServer` 在 socket 接入层默认信任所有连入 peer：监听地址直接使用 `FIPv4Address::Any`，连接建立后没有 challenge/auth，`RequestDebugDatabase` 还是空消息；与此同时，同一协议面又承载 `CreateBlueprint` 与 `ReplaceAssetDefinition` 这类会触发 Editor/workspace 副作用的消息。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:395-399` 在 `HandleConnectionAccepted()` 中只记录来源地址并把 socket 入队；`.../AngelscriptDebugServer.cpp:402-408` 用 `FTcpListener(FIPv4Endpoint(FIPv4Address::Any, Port))` 监听所有网卡；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:25-80` 把 `RequestDebugDatabase`、`StartDebugging`、`CreateBlueprint`、`ReplaceAssetDefinition` 放进同一 `EDebugMessageType`；`.../AngelscriptDebugServer.h:103-116` 的 `FStartDebuggingMessage` 只有 `DebugAdapterVersion`；`.../AngelscriptDebugServer.h:177-183` 的 `FAngelscriptRequestDebugDatabase` 完全没有鉴权字段；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1161-1188` 收到 `CreateBlueprint` 后直接广播到 Editor 委托；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2028-2035` 的 `ReplaceScriptAssetContent()` 会把写回消息广播给全部 debug client。 |
| 优点 | 当前 VS Code 扩展接入极简，不需要额外 discovery/auth 流程；同一端口即可完成 attach、符号同步、资产浏览和部分 Editor 操作。 |
| 不足 | 这使“能连上调试端口”几乎等价于“能读取符号数据库并触发一部分工作区副作用”。一旦要真正支持 remote 调试、云端设备调试或多 IDE 前端治理，当前 transport 没有办法把只读调试、调试控制、workspace 写操作拆成不同信任等级。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `FJsEnvImpl` 在真实 JS runtime `Context` 上创建 inspector，但对外暴露的是 `/json/list`、`/json/version` 和 websocket inspector session；每个连接都会新建 `V8InspectorChannelImpl`，协议面只负责转发 CDP/Inspector 消息，不承载 Editor/workspace 写操作。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/JsEnvImpl.cpp:619-625`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-540` | 先把网络暴露面收敛为 debug-only discovery/session，再考虑前端如何消费，不把 workspace 副作用默认挂在 transport 上。 |
| UnLua | 调试基元是 `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 这类进程内 API；源码里没有把这些能力包装成对外 socket 服务。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-732` | 先沉淀可复用的 runtime 事实接口，再按需增加 transport；这样 debug 原语不会天然继承 remote 写权限。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 `V2` 线协议兼容性的前提下，为 transport 增加显式 trust boundary：默认 loopback、可选鉴权、按 domain 授权。 |
| 具体步骤 | 1. 新增 `Debugging/Transports/AngelscriptDebugTransportSettings.*`，把 `ListenAddress`、`bAllowRemote`、`AuthToken`、`AllowedDomains` 从硬编码拆到配置；默认监听 `127.0.0.1`，只有显式配置时才开放非 loopback。 2. 在 `V2` 上增量加入 `Hello/Auth` 握手，字段至少包含 `ProtocolVersion`、`ClientName`、`RequestedDomains`、`Token/HMAC`；旧 client 仅在 loopback 且未设置 token 时允许走 legacy 空握手。 3. 在 server 内部新增 capability mask，把能力拆成 `debug.read`、`debug.control`、`workspace.read`、`workspace.write`；`CreateBlueprint`、`ReplaceAssetDefinition` 默认要求 `workspace.write`，remote 连接初始只给 `debug.read`。 4. 为拒绝的握手或越权消息返回结构化 `Diagnostics`/错误码，并记录 `ClientEndpoint + Domains + Result` 审计日志，避免“连上但为何不可用”不可诊断。 5. 等新握手稳定后，再让更高层的 `RequestDebugDatabase`/`StartDebugging`/workspace 服务读取 capability，而不是继续依赖“只要 socket 连上就全开”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugTransportSettings.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transports/AngelscriptDebugHandshake.*`，必要时补 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorSettings.*` 或等价配置入口 |
| 预估工作量 | M |
| 架构风险 | transport 引入握手后，旧前端若在非 loopback 环境下仍按空消息直连，会表现为“以前能连、现在被拒绝”；需要把 loopback legacy path 和错误回包一起补上。 |
| 兼容性 | 可增量实施。旧 VS Code 扩展在本机 loopback 场景下可继续不带 token 连接；只有跨主机 remote 调试或显式开放非 loopback 时，才要求新握手与 capability。 |
| 验证方式 | 1. 本机 loopback 下回归现有 VS Code 扩展，确认 legacy `RequestDebugDatabase`/`StartDebugging` 仍可用。 2. 在非 loopback 地址上使用未鉴权 client 连接，确认 server 会拒绝并返回明确诊断。 3. 使用带 token 的新 client 连接，验证 `debug.read` 可用但默认不能执行 `CreateBlueprint`/`ReplaceAssetDefinition`。 4. 审计日志中应能看到 `ClientEndpoint`、请求 domain 和授权结果。 |

### Arch-DT30：`StaticJIT` 把 dispatch/devirtualization assumption 隐式写进生成过程，缺少可追踪的 assumption ledger

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | StaticJIT 的 dispatch 假设、devirtualization 可追踪性、扩展/热重载后的失效诊断 |
| 当前设计 | `StaticJIT` 会在生成时扫描 override 图、尝试把虚函数和 imported function 去虚化，并直接修改 live `asCScriptFunction` 的 `asTRAIT_FINAL`；但这些“为什么能 direct call”“为什么被视为 final”“import 当时绑定到了谁”的假设不会被单独输出为工件。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3389-3403` 遍历 `virtualFunctionTable` 并把基类函数加入 `FunctionsWithVirtualOverrides`；`.../AngelscriptStaticJIT.cpp:3415-3439` 的 `DevirtualizeFunction()` 会在没有 override 时返回原函数，或对 `asFUNC_IMPORTED` 直接取 `Engine->importedFunctions[..]->boundFunctionId` 的当前绑定；`.../AngelscriptStaticJIT.cpp:3442-3449` 的 `IsFunctionAlwaysJIT()` 在 `bAllowComprehensiveJIT` 下把虚函数视为“总能 JIT”；`.../AngelscriptStaticJIT.cpp:3453-3463` 的 `AnalyzeScriptFunction()` 会对“不在 override 集合里”的函数直接 `SetTrait(asTRAIT_FINAL, true)`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp:1177-1194` 则依据这些前置决策在 `WriteDirectCall()` 与 `WriteDynamicCall()` 之间切换，但不会把原因写出到 sidecar。 |
| 优点 | 当前策略非常激进，能尽量把 script-to-script 调用压成 direct call，并提前给 precompiled runtime 打上 `final`，提升 dispatch 性能。 |
| 不足 | 问题不在“能不能优化”，而在“优化假设不可追踪”。一旦后续扩展新增 override、import 绑定变化，或某次回归只在特定 dispatch 形态下出现，工具链无法回答某个函数当初为什么被视为 final、为什么走了 direct call、又是哪个假设失效导致回退。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 在导出 IntelliSense 工件前，`CollectTypes()` 会显式过滤 `SKEL_`、`PLACEHOLDER-CLASS`、`REINST_`、`TRASHCLASS_`、`HOTRELOADED_` 这类临时/热重载类型，只把 canonical 类型写进稳定产物。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:188-219` | 持久化工件不应静默吸收临时运行态假设，而应先把“哪些状态被认为 canonical”明文化。 |
| UnLua | 相同 generator 无论由 Editor 事件还是 commandlet 触发，最终都落到同一 `Intermediate/IntelliSense` 和 `SaveFile()` 语义，方便追溯某份 artifact 是按什么规则生成的。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:47-53`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:117-133` | 一旦某种假设需要进入工件，就要让它成为可落盘、可对账、可重放的显式合同，而不是只活在生成过程里。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 JIT 的 dispatch 决策补一层显式 `DispatchAssumptionManifest`，把“被 final 的原因”和“被 devirtualize 到哪个目标”从隐式生成逻辑提升为可验证工件。 |
| 具体步骤 | 1. 在 `AnalyzeScriptFunction()`/`DevirtualizeFunction()` 附近新增 `FDispatchAssumptionRecord`，至少记录 `FunctionId`、`AssumptionKind(FinalNoOverrides/ImportedBinding/VirtualAlwaysJit)`、`ReferencedFunctionId/BoundId`、`ModuleName`、`Reason`。 2. 生成期不再只靠 `ScriptFunction->traits.SetTrait(asTRAIT_FINAL, true)` 这种就地副作用，而是先把“可视为 final”写进 assumption manifest；运行时只有在 precompiled data 与 assumption 校验通过后才应用相关优化标记。 3. 在 `AS_JITTED_CODE` 旁新增 `DispatchAssumptions.json` 或把它并入现有/未来 JIT manifest，方便 IDE、日志和 CI 回答“某个 direct call 的前提是什么”。 4. 加载 precompiled data 时增加 assumption 验证：若 override 图或 imported binding 与 manifest 不一致，只禁用受影响函数的 direct-JIT 路径，并输出具体失效原因，而不是继续让问题隐身在 `DynamicCall` 回退里。 5. 自动化测试补两类场景：新增 derived override 后 base 函数不再被视为 final；修改 imported binding 后 assumption 失效并触发定向降级。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/DispatchAssumptions.*` |
| 预估工作量 | M |
| 架构风险 | 如果 assumption 校验粒度设计得过粗，可能又退回今天这种“整包禁用”效果；第一阶段应只做记录与定向日志，确认键值稳定后再启用 selective downgrade。 |
| 兼容性 | 可增量实施。现有 direct call / dynamic fallback 逻辑继续保留；新增的 assumption manifest 先作为附加工件与校验日志，不改变旧产物消费方。 |
| 验证方式 | 1. 生成一轮 JIT 后检查 `DispatchAssumptions`，确认包含 `final` 与 imported binding 决策。 2. 构造一个新增 override 的脚本类，验证 base 函数的 assumption 会失效并退回安全路径。 3. 构造 imported function 重绑定场景，验证 manifest 能指出旧 `BoundId` 与当前绑定不一致。 4. 回归现有 precompiled/JIT 运行时，确认没有 assumption manifest 时仍保持当前行为。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P0 | Arch-DT29 | remote 调试 trust boundary、连接鉴权、读写域权限分离 | transport 安全边界新增 + capability 治理 | 高 |
| P1 | Arch-DT30 | StaticJIT 的 dispatch assumption、devirtualization 可追踪性、定向失效诊断 | JIT assumption ledger 新增 + selective validation | 高 |

---

## 架构分析 (2026-04-08 18:30)

### Arch-DT31：`DataBreakpoint` 仍把 raw address 当作协议事实，watchpoint 后端无法演进成可移植的 remote / multi-IDE 能力

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | data breakpoint 的身份模型、后端可替换性、remote 调试可移植性 |
| 当前设计 | 当前 watchpoint 合同直接把进程内内存地址暴露给前端，再由 Windows-only hardware breakpoint 后端把这些地址写入 `DR0-DR3`；协议里没有“符号级 watch target”这一层。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:300-328` 的 `FAngelscriptDataBreakpoint` 直接序列化 `Address/AddressSize/HitCount`；`.../AngelscriptDebugServer.h:330-395` 的 active mirror 也继续以 `Address` 和 `ContextPtr` 为核心；`.../AngelscriptDebugServer.h:416-439` 的 `FAngelscriptVariable` 把 `ValueAddress/ValueSize` 作为变量窗输出字段；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1089-1128` 在 `RequestVariables/RequestEvaluate` 中直接把 `Value.GetAddressToMonitor()` 回给客户端；`.../AngelscriptDebugServer.cpp:630-649` 在栈帧弹出时仅按地址范围把 watchpoint 标记为 `Remove_OutOfScope`；`.../AngelscriptDebugServer.cpp:111-203`、`279-341`、`405-415` 又把实际后端绑定到 `GActiveDebugServer`、`SuspendThread/ResumeThread`、`Dr0-Dr3` 和单个 vectored exception handler。 |
| 优点 | 对本机 Windows 调试会话来说，这套设计足够直接，能捕捉脚本和部分 C++ 侧的真实内存写入，命中后再延迟到下一条 script line 统一暂停。 |
| 不足 | 地址身份只在“当前进程 + 当前线程 + 当前对象/栈布局”里成立：它天生不稳定、不可持久化，也不适合被第二个前端复用。非 Windows 后端没有对等抽象；remote client 也无法安全地产生或验证这些地址。结果是 watchpoint 能力不是“调试语义”，而是“本机实现细节”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 每个 websocket 连接都会创建独立 `V8InspectorChannelImpl` 和 `V8InspectorSession`，协议消息分派到 session 对象，而不是让前端直接操作底层 runtime 内存地址。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-116`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-555` | 把“前端看到的调试对象”与“底层 runtime/backend 如何实现”隔开，watch/evaluate/inspect 先有 session 语义，再落到具体后端。 |
| UnLua | 调试 API 明确要求调用者提供 `lua_State*` 和 `StackLevel`，`GetStackVariables()` / `GetLuaCallStack()` 暴露的是符号化的栈上下文，而不是原始地址协议。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-91`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-722` | 即使不接标准协议，也应先把“看哪个上下文、监视哪个符号”定义成显式 API，再决定是否能解析成底层地址或别的 backend 句柄。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 raw-address `V2` 路径兼容性的前提下，新增“符号级 watch target + 可替换 backend”两层抽象，让 address 退化为 backend 内部细节。 |
| 具体步骤 | 1. 新增 `Debugging/Watchpoints/IAngelscriptDataBreakpointBackend.h`，把 `ResolveTarget`、`Apply`、`Clear`、`PollTriggered` 抽成接口；先实现 `HardwareDataBreakpointBackend_Windows` 包装现有 `DR0-DR3 + VEH` 逻辑。 2. 新增 `FAngelscriptWatchTargetDescriptor`，至少包含 `SnapshotId`、`FrameId/ScopeId`、`ExpressionPath`、`OptionalObjectId`、`ExpectedType`；新前端通过 descriptor 请求 watchpoint，server 在暂停态解析为 backend-specific handle。 3. 保留 `FAngelscriptVariable.ValueAddress/ValueSize` 和现有 `SetDataBreakpoints` 作为 legacy capability，仅在 loopback/legacy client 下继续开放；新 capability 改为 `supportsSymbolicWatchpoints` 与 `supportsRawAddressWatchpoints` 分离声明。 4. 为非 Windows 或 remote profile 增量增加 `PollingDiffBackend` 或 `ReadonlyUnsupportedBackend`：前者只覆盖一小类可稳定取值的对象/属性，后者明确返回“不支持写入级 watchpoint”，避免今天这种只有 platform 宏、没有协议语义的隐式缺失。 5. 将 `ProcessScriptStackPop()` 的 out-of-scope 逻辑迁到 descriptor/backend 层：栈变量 watchpoint 由 snapshot 生命周期失效，堆对象 watchpoint 则由对象 identity 或 weak handle 决定失效，而不是继续只比地址区间。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Watchpoints/IAngelscriptDataBreakpointBackend.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Watchpoints/AngelscriptWatchTargetDescriptor.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Watchpoints/HardwareDataBreakpointBackend_Windows.*`，可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Watchpoints/PollingDiffBackend.*` |
| 预估工作量 | L |
| 架构风险 | 符号级 watchpoint 一旦解析策略定义不稳，容易出现“看起来设置成功，但命中条件与 legacy address 版本不一致”的新型调试歧义；第一阶段应把 capability 和诊断信息做清楚。 |
| 兼容性 | 完全可增量实施。旧 VS Code 扩展继续发送 `Address/AddressSize`，现有 Windows 本机能力不变；只有新前端才启用 symbolic watchpoint。 |
| 验证方式 | 1. Windows loopback 下回归现有 raw-address data breakpoint，用例保持通过。 2. 新增 symbolic watchpoint 集成测试：locals、`this` 成员、module global 三类目标都能解析并命中。 3. 构造栈变量离开作用域场景，验证新协议返回“snapshot expired/out of scope”，而不是仅暴露地址失效。 4. 在非 Windows 或 remote profile 下验证 capability 正确降级且返回结构化诊断。 |

### Arch-DT32：`StaticJIT` 的 bytecode lowering 通过 C++ static constructor 注入全局 singleton，JIT 扩展面仍是隐式编译时副作用

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | JIT backend 注册机制、profile 化能力、opcode 扩展面 |
| 当前设计 | 现有 bytecode lowering 目录并没有显式 registry/bootstrap 阶段，而是依赖 `FAngelscriptBytecodeImpl<T, Instr>::bRegistered = Register()` 这类 static initialization，把 lowering 单例偷偷塞进进程级 `TMap`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.h:16-67` 定义了全局 `GetBytecodeMap()`，并通过模板静态成员 `bRegistered` 自动分配 `new T()` 后注册到 map；`.../AngelscriptBytecodes.cpp:18-32` 用进程级 `InstructionToBytecodeMap` 和 `UnimplementedBytecode` 作为默认查找路径；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3204-3336` 在 `GenerateCppCode()` 中直接按 opcode 查全局 map 并执行 `Bytecode.Implement(Context)`；`.../AngelscriptStaticJIT.cpp:3453-3463` 的 `AnalyzeScriptFunction()` 还会直接修改 live `ScriptFunction->traits`，说明 JIT 生成不是纯 registry + pure emit，而是全局状态和 live engine 状态混杂。 |
| 优点 | 对当前内建 lowering 集来说，这种设计几乎没有初始化 ceremony，新增一个 built-in opcode lowering 的局部代码量很小。 |
| 不足 | 可用 lowering 集合是隐式的，既没有 profile 边界，也没有 capability 声明。结果是：实验性 lowering、平台专用 lowering、测试替身 backend、部分 opcode 白名单/黑名单，都只能通过重新编译核心文件来达成；工具链也无法回答“这一轮构建到底支持哪些 opcode/backend”，只能等生成时撞上 `UnimplementedBytecode` 或 `check(bImplemented)`。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | Inspector 会在 `OnOpen()` 时显式创建 `V8InspectorChannelImpl`，在 `OnClose()` 时显式销毁；session 生命周期与 transport owner 清晰可见，不依赖隐藏的 static 注册副作用。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-116`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-555` | 扩展面应该绑定到显式 owner / lifecycle 上，这样 capability、测试替身和多实现共存才有落点。 |
| UnLua | IntelliSense producer 通过 `CollectTypes()`、`SaveFile()`、`DeleteFile()` 显式执行收集和落盘，Editor 与 commandlet 只是不同 driver，而不是靠 static init 挂出一组全局 side effect。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:188-245` | 生产者链路显式化后，才能稳定表达“本轮 producer 是谁、输出了什么、删除了什么”。JIT backend registry 也需要同样的显式注册面。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入显式 `FStaticJITBackendRegistry` / `IJitLoweringBackend`，把今天的 static-init lowering 集迁到可按 profile 构造、可声明 capability、可被测试替换的 registry 对象上。 |
| 具体步骤 | 1. 新增 `StaticJIT/Backends/StaticJITBackendRegistry.h/.cpp`，定义 `RegisterOpcode(Instr, BackendId, LoweringFactory)`、`FindOpcode(Instr)`、`GetSupportedOpcodes()`；第一阶段 registry 内仍可持有 singleton lowering 实例，但 owner 变成显式对象。 2. 把 `FAngelscriptBytecodeImpl<T, Instr>::bRegistered` 迁成 `RegisterBuiltInBytecodes(FStaticJITBackendRegistry&)` 这种显式函数，由 `FAngelscriptStaticJIT::WriteOutputCode()` 在选择好 `ToolchainProfile` 后调用。 3. 为 `Experimental/PlatformSpecific` lowering 预留二级注册点，例如 `RegisterExtensionBackends()`；没有扩展时行为与今天完全相同。 4. 扩展现有 JIT manifest，增加 `backendProfile`、`supportedOpcodes`、`backendOverrides` 字段，让 CI 和 IDE 能看到“这轮生成能力面”而不是只看到成败。 5. 在测试里允许注入最小 registry 或 fake backend，直接验证某个 opcode 的选择逻辑、fallback 和错误信息，不再依赖把整套 built-in lowering 全部链接进测试进程。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/Backends/StaticJITBackendRegistry.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/Backends/IJitLoweringBackend.h` |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段就强行把所有 lowering 改成可热插拔对象，回归面会很大；更稳妥的路径是先显式化 owner 和 manifest，再逐步引入可选 backend。 |
| 兼容性 | 可增量实施。built-in lowering 仍然全部注册，生成结果保持不变；新增 registry 只是把 today implicit capability 改成 explicit contract。 |
| 验证方式 | 1. 保持现有 built-in registry 时，JIT 产物与未改造前字节级一致。 2. 构造一个仅注册部分 opcode 的 fake registry，验证 manifest 会正确报告缺失 capability。 3. 增加测试 backend 覆盖某一个 opcode，确认无需改动核心 `AngelscriptBytecodes.cpp` 即可替换 lowering。 4. 在不同 `ToolchainProfile` 下生成两次 manifest，确认 `supportedOpcodes/backendProfile` 的差异可被稳定观测。 |

### Arch-DT33：`FAngelscriptDocs` 是 append-only 进程级 cache，module discard / hot reload 没有对应失效边界

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 文档/导航缓存的生命周期、hot reload 正确性、工具链事实失效边界 |
| 当前设计 | `FAngelscriptDocs` 当前更像一个全局 append-only cache：`AddUnrealDocumentation*()` 只往静态 `TMap` 里加条目，`LookupAngelscriptFunction()` 直接返回缓存的 `UFunction*`，但模块 discard / swap 路径没有对应的 docs invalidation。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:19-39`、`41-53` 定义并填充进程级静态 `UnrealDocumentation/UnrealTypeDocumentation/GlobalVariableDocumentation/UnrealPropertyDocumentation`；`.../AngelscriptDocs.cpp:118-125` 的导航桥直接返回缓存的 `UFunction*`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1026-1120` 的 `DiscardModule()` 明确会清理 `ModulesByScriptModule`、`ActiveClassesByName`、diagnostics、file reload 队列等状态，但这段路径里没有任何 docs store 清理；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1575-1617` 的 `SendDebugDatabase()` 继续依赖 `FAngelscriptDocs::GetUnrealDocumentation()` 与 `LookupAngelscriptFunction()` 取 `doc/keywords/meta`。 |
| 优点 | live Editor session 中，读文档和回链 `UFunction` 的开销很低；对当前单进程、单前端常见工作流来说实现也足够直接。 |
| 不足 | 一旦模块被 discard / swap，这些静态 cache 不会按 module revision 收缩。这样会出现两个问题：一是 stale 文档/元数据可能继续出现在后续 `DebugDatabase` 里；二是 `LookupAngelscriptFunction()` 持有的 raw `UFunction*` 与当前活动 module revision 之间没有显式一致性边界。即使未来补了统一 symbol graph，这个 append-only cache 仍然会是本地正确性隐患。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense generator 会显式过滤 `REINST_`、`HOTRELOADED_` 等临时类型，并在 asset 删除时调用 `DeleteFile()` 移除过期工件，而不是让产物集合只增不减。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:188-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:248-258` | toolchain facts 必须有可见的 prune/invalidate 机制，尤其在 hot reload / transient type 环境里，不能只靠 append。 |
| puerts | Inspector channel 在连接关闭时立即 `delete` 并从 `V8InspectorChannels` 中移除，session 生命周期与 owner 明确，不让失效会话长期滞留在全局表里。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:551-555` | 即便缓存必须存在，也应有与 owner 生命周期对齐的移除点，而不是单向累积。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在更大的 symbol graph / manifest 改造之前，先把 `FAngelscriptDocs` 从 append-only 静态表升级成带 `Revision` 与 `ModuleOwner` 的可失效 store，堵住 hot reload 正确性缺口。 |
| 具体步骤 | 1. 新增 `Core/AngelscriptDocsStore.h/.cpp`，记录 `SymbolId/FunctionId`、`ModuleName`、`BuildRevision`、`Tooltip/Category` 以及 `TWeakObjectPtr<UFunction>` 或等价弱引用，而不是直接把 raw `UFunction*` 长期挂在静态表里。 2. 保留 `FAngelscriptDocs::AddUnrealDocumentation*()` 公开 API，但内部改为写入当前 revision buffer；`BeginBuildRevision()` / `CommitBuildRevision()` / `AbortBuildRevision()` 负责原子切换。 3. 在 `DiscardModule()`、module swap 或全量 reload 完成点调用 `InvalidateModuleDocs(ModuleName, Revision)`，先把对应条目标记失效；如果没有新 revision 接手，则从 store 中彻底 prune。 4. `LookupAngelscriptFunction()` 先检查 owner revision 和 weak pointer 是否仍有效，再决定是否回退到旧行为；`SendDebugDatabase()` 对失效条目只输出仍然可信的文本字段，不再盲目附带过期 `meta/ufunction` 信息。 5. 第二阶段再让这份 docs store 成为更大 `SymbolGraph` 的一个 producer，而不是继续并行维护多份 cache。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocsStore.*` |
| 预估工作量 | M |
| 架构风险 | 如果 `ModuleName/Revision` 的归属规则定义得不清楚，容易把仍然有效的共享符号也一并 prune 掉；第一阶段应先覆盖脚本 module 自有符号，再逐步纳入 shared/bound symbols。 |
| 兼容性 | 可完全增量实施。现有 `FAngelscriptDocs` API 和 `DebugServer` 字段都不需要立即改名；变化主要发生在内部 store 与 invalidation 路径。 |
| 验证方式 | 1. 构造 module discard + reload 场景，确认 `GetUnrealDocumentationCount()` 不再只增不减。 2. 让某个脚本函数在 reload 后改名或移除，验证旧文档和旧 `meta` 不会继续出现在新一轮 `DebugDatabase` 中。 3. 对失效 `UFunction` 弱引用做单元测试，确认 `LookupAngelscriptFunction()` 会安全返回空而不是保留陈旧指针。 4. 回归 `dump-as-doc` 和 live `RequestDebugDatabase`，确认正常符号仍能输出完整 tooltip/category/meta。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT31 | data breakpoint 身份模型、watchpoint backend 可替换性、remote 可移植性 | 调试模型升级 + backend 抽象 | 高 |
| P2 | Arch-DT32 | StaticJIT opcode/backend 注册机制、profile 化 capability | 扩展点新增 + registry 显式化 | 中 |
| P1 | Arch-DT33 | Docs/导航缓存的 invalidation 边界、hot reload 正确性 | 生命周期治理 + 可失效 store | 高 |

---

## 架构分析 (2026-04-08 23:34)

### Arch-DT34：`DebugServer V2` 的协议合同是 `FArchive` 二进制与 JSON 字符串的混合体，版本演进点分散，难以承载多 IDE 前端

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试协议的 schema 演进、跨前端互操作、非 UE 客户端接入成本 |
| 当前设计 | `DebugServer V2` 的 wire contract 不是单一格式，而是“长度前缀二进制 envelope + `FArchive` 序列化消息体 + 某些消息内再嵌 JSON 字符串 + 若干局部版本号”的混合体。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:52-109` 用 `int32 MessageLength + uint8 MessageType + raw body` 组织 envelope；`.../AngelscriptDebugServer.h:95-193`、`196-220` 说明大部分消息直接依赖 `FArchive& operator<<`；`.../AngelscriptDebugServer.h:433-437` 里 `FAngelscriptVariable` 是否带 `ValueAddress/ValueSize` 取决于全局 `AngelscriptDebugServer::DebugAdapterVersion`；`.../AngelscriptDebugServer.h:507-553` 里 `FAngelscriptAssetDatabase`、`FAngelscriptFindAssets`、`FAngelscriptDebugDatabaseSettings` 又各自维护 `Version = 1/5`；`.../AngelscriptDebugServer.cpp:897-907` 在 `StartDebugging` 时把 `DebugAdapterVersion` 写入全局；`.../AngelscriptDebugServer.cpp:1499-1515`、`2036-2047` 则把调试数据库先拼成 JSON 字符串 `DB.Database`，再通过二进制消息分块发送，并用 `DebugDatabaseFinished` 作为结束标记。 |
| 优点 | 当前前端与插件共演进时实现简单，二进制消息在 UE 侧序列化成本低；`DebugDatabase` 用 JSON 字符串也让补全库 schema 比纯 `FArchive` struct 更灵活。 |
| 不足 | 协议演进点被分散到 envelope、全局 adapter version、各消息体内部的 `Version` 常量、JSON payload schema、以及 `Finished` 分块语义上。结果是任何新前端都必须同时实现 UE 风格二进制解码、理解条件字段，并对 `DebugDatabase` 再单独走一套 JSON 解析逻辑；这比对接 DAP/CDP 一类标准文本协议更难，也使多 IDE 前端很难共享一套通用 client。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | Inspector channel 直接收发 `std::string` 协议文本，`V8InspectorSession` 负责理解 CDP 消息；服务端还公开 `/json/list` 与 `/json/version`，前端可以先发现能力再建立 websocket 会话。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:330-344`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-555` | 把“transport + 文本协议 + capability discovery”做成统一合同，前端不需要理解宿主引擎私有序列化细节。 |
| UnLua | Runtime 侧只暴露 `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 这类调试原语 API，并没有在核心层引入私有 wire schema。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-732` | 先稳定 runtime debug primitive，再让 IDE/transport 自由选择消费方式，可以降低协议耦合面。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `V2` 二进制协议兼容性的前提下，引入独立的 codec/discovery 层，把今天分散的版本逻辑收敛成单一协商合同。 |
| 具体步骤 | 1. 新增 `Debugging/Protocols/IAngelscriptDebugCodec.h`，定义 `SerializeMessage`、`TryDeserializeMessage`、`DescribeCapabilities`；第一阶段仅实现 `LegacyBinaryV2Codec`，内部复用现有 `FArchive` 序列化。 2. 将 `DebugAdapterVersion` 从进程级全局值改为连接级 capability 对象，至少显式声明 `variableAddressFields`、`debugDatabaseSchemaVersion`、`assetDatabaseSchemaVersion`、`supportsWorkspaceActions`，并在 `StartDebugging` 应答里一次性返回。 3. 把 `FAngelscriptDebugDatabaseSettings`、`FAngelscriptAssetDatabase` 等内嵌 `Version` 常量迁到统一的 `ProtocolDescriptor`，旧消息体保留字段顺序不变，只把版本来源集中。 4. 为新前端增量提供一个文本 discovery 入口，例如 `/json/list` 或 `/debug/describe`，先输出 codec 名称、schema version 与 websocket/TCP 连接信息；后续若做 DAP/CDP bridge，可直接挂在这个发现层之下。 5. 对 `DebugDatabase` 保留现有 JSON schema，但改为由 codec 负责分块与完成标记；旧前端继续看到 `DebugDatabase` + `DebugDatabaseFinished`，新前端则可拿到一个显式的 stream metadata。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocols/IAngelscriptDebugCodec.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocols/LegacyBinaryV2Codec.*`，可选新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocols/DebugProtocolDescriptor.*` |
| 预估工作量 | M |
| 架构风险 | 若 codec 边界划分不清，容易把现有 `SendMessageToClient()`、`HandleMessage()` 的时序回归引入协议层；第一阶段应只做“同语义重包装”，不要同时改 session 状态机。 |
| 兼容性 | 可完全增量实施。现有 VS Code 扩展继续走 `LegacyBinaryV2Codec`；新增 discovery/capability 只对新前端生效，旧客户端仍可维持今天的 `StartDebugging`/`DebugServerVersion` 流程。 |
| 验证方式 | 1. 回归 `AngelscriptDebugProtocolTests.cpp`，确认 legacy codec 行为不变。 2. 增加 golden test：同一组逻辑消息分别经 legacy codec 与新 descriptor 输出，确认 capability/version 信息一致。 3. 用一个最小非 UE 客户端验证只靠公开 descriptor 就能完成 `RequestDebugDatabase`、`RequestVariables`、`GoToDefinition` 的接入。 |

### Arch-DT35：`StaticJIT`、`UHT`、`Docs`、`GoToDefinition` 由多条 flag 驱动的独立生产链拼接而成，没有统一的 toolchain snapshot

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | JIT 覆盖面分析、工具链阶段划分、离线产物与在线导航的一致性 |
| 当前设计 | 当前调试与工具链不是一条统一管线，而是至少四条独立 lane：`as-generate-precompiled-data` 驱动 `StaticJIT + PrecompiledData`、`dump-as-doc` 驱动 docs dump、`UHT` exporter 生成 `AS_FunctionTable_*` sidecar、运行时 `DebugServer`/`SourceCodeNavigation` 再临时构造 live 数据。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:522-528` 把 `as-generate-precompiled-data` 与 `dump-as-doc` 解析为独立 flag；`.../AngelscriptEngine.cpp:1433-1447` 只有在 `bGeneratePrecompiledData` 时才创建 `PrecompiledData`、`StaticJIT` 并 `SetJITCompiler()`；`.../AngelscriptEngine.cpp:1573-1589` 在生成模式下 `WriteOutputCode()`、保存 `PrecompiledScript.Cache` 后直接退出进程；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:66-79` 说明 `CompileFunction()` 只在生成模式登记 `FunctionsToGenerate`，否则直接 `check(false)`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp:112-116`、`157-160`、`530-535`、`703-707`、`901-905` 等处都表明 native-form 元数据只有在 `bGeneratePrecompiledData` 为真时才登记；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp:130-168` 运行时调用点 fallback 仍是 `custom -> native -> pointer -> dynamic`，但它依赖生成阶段积累的 native-form 能力；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1550-1556` 与 `.../StaticJIT/PrecompiledData.cpp:525-580` 表明运行时只按 `PrecompiledDataGuid` 整包接收或整包清空 JIT 数据，然后退回 bytecode；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:407-755` 与 `.../AngelscriptEngine.cpp:2224-2227` 另起一条 docs dump lane；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1499-2047`、`1288-1375` 与 `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:34-115` 又在 live session 中单独拼 `DebugDatabase` 和导航；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:174-205`、`337-445` 与 `.../AngelscriptFunctionTableExporter.cs:101-161` 则再额外写出 JSON/CSV/CPP sidecar；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:121-128` 甚至要求某些编辑器写回流程必须有 live debug client 才能继续。 |
| 优点 | 每条 lane 都相对简单且可独立运行：JIT 生成适合离线批处理，`DebugServer` 适合 live IDE，UHT sidecar 适合编译期统计，docs dump 适合离线快照。 |
| 不足 | 缺点是没有单一事实源来回答“当前这次构建的脚本符号、绑定覆盖率、JIT 覆盖率、导航目标、文档快照是否一致”。尤其 `StaticJIT` 的 native-form 注册被绑在 `bGeneratePrecompiledData` 阶段，意味着你无法在非生成模式下独立观察 JIT coverage；而 live 导航与部分编辑器行为又依赖 debug 会话，离线 IDE/front-end 很难直接复用已有 sidecar。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FUnLuaIntelliSenseGenerator` 在 Editor 常驻监听资产事件，`UUnLuaIntelliSenseCommandlet` 在批处理模式全量导出，但二者共享 `CollectTypes`/`SaveFile` 风格的同一类产物与输出目录语义。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-56`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:188-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:248-290`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:29-134` | Editor 增量链和 commandlet 全量链共享 artifact contract，工具链不会因为运行模式不同而切成多份 schema。 |
| puerts | Inspector runtime lane 聚焦在 session/channel 与 `/json/list`、`/json/version` 发现入口，没有把离线 codegen、文档导出、资产写回再混进同一协议栈。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:315-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-555` | 把 live debug lane 与离线工具链 lane 明确分开，便于后续替换任一前端或生成器。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 引入统一的 `ToolchainSnapshot`/`ToolchainCoordinator`，把 `StaticJIT`、`UHT`、`Docs`、`DebugDatabase`、`GoToDefinition` 的共有事实抽成可复用生产物，再让不同 lane 各自消费。 |
| 具体步骤 | 1. 新增 `Toolchain/AngelscriptToolchainCoordinator.h/.cpp` 与 `Toolchain/AngelscriptSymbolSnapshot.*`，统一描述 `functions/types/globals/docs/source locations/UHT bind coverage/JIT coverage/precompiled guid`。 2. 让 `AngelscriptFunctionTableCodeGenerator.cs` 与 `AngelscriptFunctionTableExporter.cs` 继续产出现有 JSON/CSV/CPP，但额外写出同一 schema 的 `uht` 节点；第一阶段只做 sidecar，不改旧输出名。 3. 让 `FAngelscriptDocs::DumpDocumentation()` 与 `FAngelscriptDebugServer::SendDebugDatabase()` 都改为读取 `ToolchainSnapshot`，不再各自重建一套类/函数/属性图；`GoToDefinition()` 也优先走 snapshot 中的 source location，再回退到 live engine lookup。 4. 将 `StaticJITBinds` 的 native-form 注册从 `bGeneratePrecompiledData` 全局门槛中拆出来，至少允许在“分析模式”下生成轻量 coverage 记录；保留真正写 `AS_JITTED_CODE` 与 `PrecompiledScript.Cache` 的老路径不变。 5. 在 `FAngelscriptEngine` 引入显式 `ToolchainMode`，把今天的 `as-generate-precompiled-data`、`dump-as-doc`、future `emit-symbol-snapshot`、`analyze-jit-coverage` 统一映射到模式枚举；旧命令行 flag 继续作为兼容 alias。 6. 把编辑器里依赖 live debug client 的写回动作改成“优先用 snapshot，只有必须远程同步时才要求 client 在线”，避免 IDE 扩展成为本地工具链的硬前置。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/*` |
| 预估工作量 | L |
| 架构风险 | 如果第一阶段就强行把所有 lane 全部改成只读 snapshot，容易放大热重载、Editor-only symbol、live asset write-back 的边界问题；更稳妥的路径是先把 snapshot 做成附加 producer，再逐步替换 consumer。 |
| 兼容性 | 可增量实施。现有 `dump-as-doc`、`AS_FunctionTable_*`、`DebugDatabase`、`PrecompiledScript.Cache`、`AS_JITTED_CODE` 都继续保留；新 snapshot 与 `ToolchainMode` 只作为新增能力，旧 flag 仍能驱动旧流程。 |
| 验证方式 | 1. 同一次构建中比对 `AS_FunctionTable_Summary.json`、snapshot、`DebugDatabase`、docs dump 的函数/类型数量与 source location，一致才算通过。 2. 在非 `bGeneratePrecompiledData` 模式下运行 JIT coverage analysis，确认能看见 native/custom/pointer/dynamic 的覆盖统计而不必真正产出 `AS_JITTED_CODE`。 3. 断开 VS Code 扩展后回归本地导航与文档导出，确认 snapshot 可支撑离线 `GoToDefinition`/编辑器写回的只读部分。 4. 保持旧命令行 `-as-generate-precompiled-data`、`-dump-as-doc` 行为不变，验证兼容路径。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT34 | 调试协议 schema 演进、codec/discovery 抽象、多 IDE 前端接入成本 | 协议合同收敛 + codec 抽象 | 高 |
| P1 | Arch-DT35 | StaticJIT/UHT/Docs/Navigation 多车道工具链、统一 snapshot 与模式划分 | 工具链收敛 + 共享事实源 | 高 |

---

## 架构分析 (2026-04-08 23:44)

### Arch-DT36：插件模块拓扑仍把 runtime、debug transport、Editor workspace 与工具链入口压进同一发布单元，缺少可独立演进的 packaging boundary

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 模块边界、依赖拓扑、runtime/debug/editor/toolchain 的发布隔离 |
| 当前设计 | 插件当前只有 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块；其中 `AngelscriptRuntime` 同时承担脚本 runtime、`DebugServer` transport、`StaticJIT`、文档缓存和 Editor 反向委托出口，`AngelscriptEditor` 主要作为对 `AngelscriptRuntime` 的消费层。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `Runtime + Editor + Test` 三个模块；`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:45-79` 让 runtime 常驻依赖 `Networking`、`Sockets`、`AssetRegistry`、`Projects`，并在 editor target 下继续吸收 `UnrealEd` / `EditorSubsystem`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:19-21,45-47` 与 `.../AngelscriptRuntimeModule.cpp:120-133` 把 `GetDebugListAssets()`、`GetEditorCreateBlueprint()` 这类 editor-facing delegate 定义在 runtime module；`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs:12-40` 说明 editor module 直接依赖 `AngelscriptRuntime`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:367-409` 再由 editor 消费这些 runtime delegate；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1433-1455` 则在 runtime 初始化内直接创建 `StaticJIT` 与 `FAngelscriptDebugServer`。 |
| 优点 | 当前安装和启动路径简单，`AngelscriptRuntime` 一处初始化即可把脚本执行、调试和部分工具链入口全部带起来。 |
| 不足 | 这种 packaging 让“改调试 transport”“替换 IDE 前端”“抽出 headless toolchain”“裁掉 shipping 构建里的工具依赖”都变成改 `AngelscriptRuntime` 本体，而不是替换独立模块；同时 runtime 还要暴露 editor/workspace delegate，长期会继续放大依赖泄漏和编译面。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 在插件级别显式拆成 `UnLua` runtime、`UnLuaEditor` editor、`UnLuaDefaultParamCollector` program；runtime 依赖面相对收敛，而 `DirectoryWatcher`、`Networking`、`Sockets` 等 tooling 依赖留在 editor module。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/UnLua.Build.cs:48-66`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/UnLuaEditor.Build.cs:59-82` | 先在 packaging 层把 runtime 与 tooling/editor 分开，后续无论扩 IDE、改产物还是做 commandlet，边界都更清晰。 |
| puerts | 插件把 runtime 继续细分为 `WasmCore`、`JsEnv`、`Puerts`，并把 `DeclarationGenerator`、`PuertsEditor`、`ParamDefaultValueMetas` 作为独立 editor/program 模块；Inspector 运行时能力留在 `JsEnv`，而 codegen 不是它的内嵌副作用。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-345` | 把 runtime bridge 与 editor/program producer 拆成独立发布单元后，任一子系统都能单独演进，不必牵动整个语言 runtime。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持现有模块名兼容的前提下，先把 debug transport 与 toolchain/editor 责任从 `AngelscriptRuntime` 中拆成增量模块。 |
| 具体步骤 | 1. 新增 `AngelscriptDebugRuntime` 模块，迁入 `Debugging/` 与 socket/transport 相关依赖；`AngelscriptRuntime` 只保留调试原语接口或 service locator，不再直接链接 `Networking` / `Sockets`。 2. 新增 `AngelscriptToolchainEditor` 模块，承接 `DirectoryWatcher`、`SourceCodeNavigation`、`ASOpenCode`、literal asset 保存守卫、`GetDebugListAssets()` / `GetEditorCreateBlueprint()` 等 editor/workspace 逻辑。 3. 将 `FAngelscriptRuntimeModule` 中 editor-facing delegate 改成中性的 `IAngelscriptWorkspaceBridge` / `IAngelscriptSourceNavigationService` 注册点；runtime 不再拥有“弹窗/创建 Blueprint”这类 editor 语义。 4. 更新 `Angelscript.uplugin`：保留现有 `AngelscriptRuntime`、`AngelscriptEditor` 对外入口，新增模块默认在 editor 自动加载；旧 include 和旧模块名通过 forwarding header / forwarding registration 兼容一段时间。 5. 第二阶段再考虑把 `StaticJIT` 与 `Docs` producer 也继续拆到 `AngelscriptToolchainRuntime` 或 `AngelscriptToolchainProgram`，避免 future snapshot/coordinator 继续塞回 runtime 本体。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`，`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`，`Plugins/Angelscript/Source/AngelscriptEditor/AngelscriptEditor.Build.cs`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptDebugRuntime/*`，新增 `Plugins/Angelscript/Source/AngelscriptToolchainEditor/*` |
| 预估工作量 | L |
| 架构风险 | 模块拆分会触发 `Build.cs`、include path、loading phase 和循环依赖整理；如果第一阶段就把 API 一次性全部搬空，容易引入 editor 启动顺序和模块加载回归。 |
| 兼容性 | 可增量实施。现有脚本语义不变，现有 `AngelscriptRuntime` 公开 API 可以通过 forwarding 层继续保留；旧 IDE 扩展也无需立即感知模块拆分。 |
| 验证方式 | 1. `Editor` 目标构建后确认 `AngelscriptRuntime` 不再直接链接 `Networking/Sockets/DirectoryWatcher` 这类 tooling 依赖。 2. 回归现有 VS Code 调试、资产浏览、Create Blueprint、目录监听与普通脚本运行。 3. 在非 editor/runtime-only 目标下验证插件仍能初始化脚本执行，而不必加载 editor/workspace 模块。 4. 对新增模块做加载顺序 smoke test，确认 `PostDefault/PostEngineInit` 不会出现服务尚未注册就被调用的时序问题。 |

### Arch-DT37：`StaticJIT` 和文档导出仍寄生在 `FAngelscriptEngine` 启动路径里通过 flag 触发退出，缺少 first-class 的 Program/Commandlet 工具宿主

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | headless toolchain 宿主、build-graph 参与度、CI/命令行可复用性 |
| 当前设计 | 除了 `AngelscriptUHTTool` 这条 `UhtExporter` 管线外，`StaticJIT` 生成与 `dump-as-doc` 仍然通过启动 `FAngelscriptEngine`、跑完整个初始编译流程、生成产物后直接 `RequestExit()` 的方式完成；插件自身没有对应的 `Program` 模块或 `Commandlet` 宿主。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 没有 `Program` 模块；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1425-1455` 在 engine 初始化里解析 `bGeneratePrecompiledData`，创建 `PrecompiledData`、`StaticJIT` 并 `SetJITCompiler()`，同时还在同一路径里决定是否创建 `DebugServer`；`.../AngelscriptEngine.cpp:1573-1608` 在初始编译后执行 `StaticJIT->WriteOutputCode()`、保存 `PrecompiledScript.Cache`，然后 `RequestExitWithStatus(false, 0)`；`.../AngelscriptEngine.cpp:2223-2227` 的 `dump-as-doc` 也是在 Editor 进程里调用 `FAngelscriptDocs::DumpDocumentation()` 后立刻退出；与之相对，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-27,35-43` 和 `.../AngelscriptFunctionTableCodeGenerator.cs:334-337,432-460` 已经是显式 `UhtExporter` + `AddExternalDependency` 驱动的 build-graph 参与者。 |
| 优点 | 直接复用 live `FAngelscriptEngine` 语义，JIT/docs 产物与真实绑定/编译路径天然一致，早期实现成本低。 |
| 不足 | 现在的 headless 工具链宿主是“启动 runtime，再在中途转成工具”，而不是 first-class tool host。结果是 CI/IDE 自动化很难只声明输入输出并增量运行某个 producer；debug server、runtime 初始化和工具导出生命周期也被绑在一起，后续要补 snapshot/coordinator 时没有干净落点。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 插件显式提供 `Program` 模块；`UUnLuaIntelliSenseCommandlet::Main()` 作为 headless 宿主生成静态导出，并可按参数调用 `FUnLuaIntelliSenseGenerator` 复用 editor producer，同一 contract 同时服务批处理和编辑器常驻更新。 | `Reference/UnLua/Plugins/UnLua/UnLua.uplugin:23-40`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:29-134`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55` | 工具链应该先有独立宿主，再让 editor/runtime 复用同一 producer，而不是让运行时启动流程兼职当工具进程。 |
| puerts | 插件在 packaging 上把 `DeclarationGenerator` 和 `ParamDefaultValueMetas` 做成独立 editor/program 模块，而 Inspector 继续留在 `JsEnv` runtime；说明 codegen/toolchain 与 runtime debug bridge 可以由不同宿主承担。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-345` | 先把 runtime lane 和 toolchain lane 的宿主拆开，才能让构建系统、IDE 和调试前端各自按需调用。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新增 first-class `Program/Commandlet` 工具宿主，把今天挂在 `FAngelscriptEngine` 启动流程里的 docs/JIT/snapshot 任务抽成可复用 job。 |
| 具体步骤 | 1. 新增 `AngelscriptToolchainProgram` 模块，或在 editor 侧新增 `UAngelscriptToolchainCommandlet`，支持 `emit-docs`、`emit-precompiled-data`、`emit-jit`、`emit-symbol-snapshot`、`analyze-jit-coverage` 等子命令。 2. 将 `FAngelscriptEngine.cpp:1433-1455`、`:1573-1608`、`:2223-2227` 里的工具分支抽成 `Toolchain/AngelscriptToolchainCoordinator.*` 或 `ToolchainJobs/*`，由 runtime、commandlet、测试共享，而不是继续在 `Initialize/InitialCompile` 尾部直接 `RequestExit()`. 3. 保留现有 `-as-generate-precompiled-data`、`-dump-as-doc` 作为兼容 alias，但内部改为调用新的 coordinator/job；第一阶段即便仍跑在 editor 进程，也不要再把 job 逻辑散落在 runtime 启动函数里。 4. 让 `AngelscriptUHTTool` 继续做 `AS_FunctionTable_*` exporter，但额外把其输出登记到同一个 toolchain manifest，由新的 program host 汇总 docs/JIT/UHT 三类产物。 5. 在 CI/BuildGraph 中新增显式节点，声明输入为脚本根目录、`Runtime.Build.cs`、UHT 反射输出，声明输出为 `PrecompiledScript.Cache`、`AS_JITTED_CODE/*`、docs/snapshot；避免通过“启动 editor 然后等它自退”来驱动工具链。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptToolchainProgram/*` 或新增 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Commandlets/*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/*`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 预估工作量 | M |
| 架构风险 | 如果抽取 job 时仍隐式依赖 `FAngelscriptEngine` 内部状态顺序，可能会出现“commandlet 能跑但结果与 runtime 初始化不一致”的回归；第一阶段需要用 golden artifact 对比兜底。 |
| 兼容性 | 可增量实施。旧命令行 flag 与现有 UHT exporter 都可继续工作；对脚本作者和现有 IDE 扩展无协议层破坏，只是工具链触发宿主从 runtime 启动副作用改为显式 job。 |
| 验证方式 | 1. 用旧 `-dump-as-doc` / `-as-generate-precompiled-data` 与新 commandlet/program 各跑一次，比较 docs、`PrecompiledScript.Cache`、`AS_JITTED_CODE` 产物的一致性。 2. 验证新宿主能在不启动 debug client 的情况下完成 docs/JIT/snapshot 导出。 3. 确认 `AngelscriptUHTTool` 的 summary/CSV 仍能写出，并被新 manifest 正确收录。 4. 在 CI 中做增量改动实验，只改脚本或只改 `Runtime.Build.cs`，确认 toolchain job 的触发范围可被单独观察。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT36 | runtime/debug/editor/toolchain 的模块发布边界 | 模块拆分 + 依赖拓扑收敛 | 高 |
| P1 | Arch-DT37 | headless toolchain 宿主、Program/Commandlet 化、build-graph 参与度 | 工具宿主新增 + 执行链重构 | 高 |

---

## 架构分析 (2026-04-08 23:53)

### Arch-DT38：`AngelscriptUHTTool` 通过文本回读 `AngelscriptRuntime.Build.cs` 推断生成范围，扩展点仍依赖实现细节而非显式合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | UHT toolchain 的模块发现合同、增量失效边界、第三方扩展接入 |
| 当前设计 | `AngelscriptFunctionTableCodeGenerator` 不是消费显式的 toolchain module manifest，而是先定位 `AngelscriptRuntime.Build.cs`，再用正则和若干状态位直接扫描 `DependencyModuleNames.AddRange` / `if (Target.bBuildEditor)` 文本块，推断哪些模块参与 `AS_FunctionTable_*` 生成。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:334-384` 的 `LoadSupportedModules()` 通过 `QuotedStringPattern`、`inDependencyBlock`、`inEditorBlock` 逐行解析 Build.cs；`.../AngelscriptFunctionTableCodeGenerator.cs:387-409` 的 `ResolveRuntimeBuildCsPath()` 甚至是从 UHT session 中找到第一个 `AngelscriptRuntime` header，再反推出 `AngelscriptRuntime.Build.cs` 路径；`.../AngelscriptFunctionTableCodeGenerator.cs:337` 和 `:459` 虽然把 `Build.cs` 与 header 都纳入 `AddExternalDependency`，但合同本身仍是“当前 Build.cs 的文本形状”；`.../AngelscriptFunctionTableCodeGenerator.cs:53-74` 说明整个 `Generate()` 流程都依赖这份推断结果去决定哪些 module 生成 shard、哪些文件会被 stale cleanup。 |
| 优点 | 这条链能把 `Build.cs` 和 header 变化自动纳入 UHT 增量依赖，不需要维护第二份“支持模块列表”，现阶段对仓库内自用流程比较省事。 |
| 不足 | 真正的扩展合同其实是隐式的：只有当依赖声明仍保持“直接写在 `DependencyModuleNames.AddRange` 文本块里”时，UHTTool 才能看懂。一旦 `Build.cs` 改成 helper 方法、变量拼装、条件函数或未来的外部配置文件，生成范围就会依赖解析器是否恰好认得那种写法；第三方要扩展 toolchain 也只能去适配这套文本启发式，而不是对接一个稳定 schema。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `FUnLuaIntelliSenseGenerator` 通过 `Initialize()` 明确注册 `OnAssetAdded/Removed/Renamed/Updated`，`UUnLuaIntelliSenseCommandlet::Main()` 则显式调用 `Generator->Initialize(); Generator->UpdateAll();`；生成范围来自同一个 producer API，而不是通过反向解析另一个构建脚本文本去猜。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-52`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:58-96`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:108-111` | toolchain producer 的输入边界最好是显式 API 或显式数据文件，而不是 build-script 形状。 |
| puerts | 插件级别直接把 `DeclarationGenerator`、`ParamDefaultValueMetas` 声明为独立 `Editor` / `Program` 模块；toolchain lane 的存在性是 `.uplugin` 元数据的一部分，不需要靠运行时模块的 `Build.cs` 文本推断。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:29-35` | 先把“谁是 toolchain producer”变成显式元数据，后续再让各 producer 声明自己的输入输出。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `UHT` 生成链补一层显式 `ToolchainModuleManifest`，让 `Build.cs` 文本解析退回兼容 fallback，而不再是唯一事实源。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Config/AngelscriptToolchainModules.json` 或等价 manifest，至少声明 `allModules`、`editorOnlyModules`、`producer=UHTFunctionTable`、可选 `reason/owner`；`AngelscriptFunctionTableCodeGenerator` 先读 manifest。 2. 保留当前 `LoadSupportedModules()` 作为兼容 fallback，但只在 manifest 缺失时启用，并在日志里明确标记 “heuristic Build.cs parser in use”。 3. 在 `AS_FunctionTable_Summary.json` 中追加 `moduleDiscoverySource(manifest/buildcs-fallback)` 与 `unsupportedModuleReason`，让 IDE/CI 能区分“模块没有 BlueprintCallable 项”与“模块根本没被纳入生成范围”。 4. 第二阶段再考虑把 manifest 的产出前移到共享的 C# helper 或 BuildGraph 节点，而不是继续把 `QuotedStringPattern` 当长期合同；迁移期间保留一个 `verify-toolchain-modules` 校验命令，对比 manifest 与旧 parser 的结果。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，新增 `Plugins/Angelscript/Config/AngelscriptToolchainModules.json` 或等价 manifest，必要时补 `Plugins/Angelscript/Angelscript.uplugin` / BuildGraph 配置入口 |
| 预估工作量 | M |
| 架构风险 | 迁移期会暂时形成 “manifest + Build.cs fallback” 双事实源；若没有对账校验，容易出现 manifest 过期但 parser 还能生成的灰区。 |
| 兼容性 | 可完全增量实施。manifest 缺失时继续走现有 `Build.cs` 解析；现有 `AS_FunctionTable_*`、summary、CSV 输出都不必改名。 |
| 验证方式 | 1. 在 manifest 缺失时回归现有生成链，确认输出字节级不变。 2. 提供 manifest 后，比较 `moduleSummaries` 与 fallback parser 结果一致。 3. 人为把 `AngelscriptRuntime.Build.cs` 改成 helper 方法包装 `DependencyModuleNames`，验证新合同仍能稳定生成，而旧 parser 仅作为告警 fallback。 4. 在 summary 中检查 `moduleDiscoverySource` 与 `unsupportedModuleReason` 是否正确落盘。 |

### Arch-DT39：`AS_FunctionTable_*` 通过匿名 `FBind` 静态副作用注入运行时，生成 shard 的 producer identity 在装载时被丢失

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | UHT 生成 shard 的注册生命周期、可观测性、扩展式装载 |
| 当前设计 | `BuildShard()` 为每个 UHT shard 生成 `AS_FORCE_LINK const FAngelscriptBinds::FBind ...` 静态对象，并只传入 `BindOrder` 与 lambda，不传 `BindName`；运行时再把这些匿名注册项收进全局 bind 数组统一排序执行。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:302-330` 生成 `AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_<Module>_<Shard>`，并固定使用 `(int32)FAngelscriptBinds::EOrder::Late + 50`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:424-472` 中 `FBind` 构造函数会立刻调用 `RegisterBinds(...)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:151-158` 显示只传 `BindOrder` 的重载会回退成 `RegisterBinds(NAME_None, ...)`，随后由 `MakeUnnamedBindName()` 生成 `UnnamedBind_%d`；`.../AngelscriptBinds.cpp:144-145,195-207` 再把这些 bind 放进全局数组排序执行；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1915-1921` 的 `BindScriptTypes()` 只是统一调用 `FAngelscriptBinds::CallBinds(...)`，并不知道某个执行项来自哪个 `AS_FunctionTable_*` shard。 |
| 优点 | 现有方案复用已有 bind 生命周期，UHT 产物只要被编译/链接进来就能自动生效，不需要额外 registry 文件或显式 startup 代码。 |
| 不足 | 这不仅是 static side effect，更是 identity 丢失：生成 shard 在真正执行前就被降格成 `UnnamedBind_N + order=Late+50`。结果是运行时无法稳定回答“哪一个 module/shard 注册了哪些 generated entries”，也很难做精确禁用、顺序治理、冲突定位或第三方 generator 并存；未来若要把 function-table 变成可插拔 producer，首先缺的就是一个显式 registry。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | 生成器把 producer identity 直接体现在 `ModuleName/FileName.lua` 路径和显式 `SaveFile()` / `DeleteFile()` 调用中；commandlet 与 Editor 监听器都通过显式方法驱动，而不是把生成动作藏进匿名静态注册。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:143-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:117-129` | producer 身份应当在 artifact 和执行链里都可见，便于追踪、禁用和增量替换。 |
| puerts | `.uplugin` 里把 `DeclarationGenerator` 与 `ParamDefaultValueMetas` 作为具名模块暴露，toolchain producer 的身份先在 packaging 元数据层可见，而不是只剩“某个匿名静态对象被链接进来了”。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:29-35` | 生成链若要可演进，至少要先有稳定的 producer identity 和装载边界。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先给 UHT shard 补上显式名字与 registry，再逐步把匿名静态 `FBind` 迁到可枚举、可治理的 generated-bind 装载面。 |
| 具体步骤 | 1. 第一阶段最小改动：修改 `BuildShard()`，改用 `FBind(FName BindName, int32 BindOrder, ...)` 重载，把 shard 名明确生成为 `AS_FunctionTable_<Module>_<Shard>`，不再落到 `UnnamedBind_N`。 2. 在 summary/CSV 外新增 `AS_FunctionTable_Index.json` 或等价 sidecar，登记 `bindName`、`moduleName`、`shardIndex`、`entryCount`、`editorOnly`、`order`。 3. 第二阶段让每个 shard 生成显式 `Register_AS_FunctionTable_<Module>_<Shard>()` 函数，再由聚合文件或 `FAngelscriptGeneratedBindRegistry` 统一调用；`FBind` 静态对象只作为 legacy fallback。 4. 扩展 `FAngelscriptBinds::GetBindInfoList()` / `FAngelscriptBindExecutionObservation`，把 generated shard 的 `bindName/module/shard` 暴露给测试和诊断 UI，支持按 shard/module 精确禁用或排查。 5. 后续若引入第三方 function-table producer，也要求它们通过同一 registry 注册，而不是继续各自发明新的静态副作用入口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，必要时补 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.*` 或等价测试入口 |
| 预估工作量 | M |
| 架构风险 | 如果第二阶段过早移除静态 `FBind` 路径，可能遇到链接保活、聚合文件顺序或 editor-only shard 装载时机回归；因此应先“具名化”，再“显式 registry 化”。 |
| 兼容性 | 可增量实施。第一阶段只是把匿名 bind 改成具名 bind，对脚本语义无影响；旧 shard 即使继续走静态 `FBind`，也可与新 registry 并存一段时间。 |
| 验证方式 | 1. 回归 `BindScriptTypes()` 后的生成绑定数量，确认具名化前后注册条目一致。 2. 检查 `GetBindInfoList()` 或 bind 观察测试，确认能看到 `AS_FunctionTable_<Module>_<Shard>` 而不是 `UnnamedBind_*`。 3. 禁用单个 generated shard，验证只影响对应模块/分片，不波及其他 bind。 4. 若启用第二阶段 registry，再对比 legacy 静态路径与显式 registry 路径的执行顺序和注册结果一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT38 | UHT 模块发现合同、Build.cs 文本耦合、toolchain 扩展接入 | 显式 manifest 新增 + fallback 收敛 | 高 |
| P1 | Arch-DT39 | UHT generated shard 的 producer identity、装载可观测性、registry 化 | 生成注册面重构 + 诊断能力增强 | 高 |

---

## 架构分析 (2026-04-09 00:09)

### Arch-DT40：`TEST/SHIPPING` 关闭 `AS_JIT_VERIFY_PROPERTY_OFFSETS`，而这恰好削弱了 precompiled/JIT 真实消费态的 ABI 哨兵

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | packaged JIT 的 ABI 校验层、property/type layout 安全网、真实运行态可诊断性 |
| 当前设计 | `PrepareToFinalizePrecompiledModules()` 会无条件把函数指针、system function 指针、类型指针、全局变量和 property offset 回填到运行时对象，但 property/type layout 的深校验只在 `AS_JIT_VERIFY_PROPERTY_OFFSETS` 为真时编译进去；`TEST/SHIPPING` 默认关闭这层校验，运行时主要只剩 `PrecompiledDataGuid` 的整包比对。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h:12-13` 将 `AS_JIT_VERIFY_PROPERTY_OFFSETS` 定义为 `!UE_BUILD_SHIPPING && !UE_BUILD_TEST`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1427-1428` 说明 `bUsePrecompiledData` 仅在非 commandlet、非 editor、非 development-like profile 下启用；`.../AngelscriptEngine.cpp:1551-1556` 对编入二进制的 JIT 仅按 `PrecompiledDataGuid` 做整包匹配，不匹配就 `FJITDatabase::Clear()`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2429-2434` 无条件把 `PropertyOffsetLookups` 回填到运行时指针；`.../PrecompiledData.cpp:2443-2477` 的 property/type/align 校验则完全包在 `#if AS_JIT_VERIFY_PROPERTY_OFFSETS` 内。 |
| 优点 | `TEST/SHIPPING` 不必承担逐字段 `checkf` 成本，steady-state 启动路径更轻，开发态又仍保留了严格校验。 |
| 不足 | 真实消费 precompiled/JIT 的 profile 里，fine-grained ABI 哨兵反而最弱。推断：一旦出现“整包 `PrecompiledDataGuid` 仍一致，但少数 native type size / alignment / property offset 发生漂移”的情况，运行时更可能继续接受已回填指针与偏移，而不是给出按 unit/function 的定向降级与诊断。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | Inspector session 直接绑定在真实 `v8::Context` 上：`contextCreated()` 后创建 `V8Inspector`，连接建立时创建 `V8InspectorChannelImpl`，后续消息始终在真实 runtime context 上 `dispatchProtocolMessage()`，而不是切到一个只在开发态存在的验证 profile。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:282-308`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525` | 关键正确性/观察能力应尽量挂在真实消费 profile 上，并提供 packaged-safe 的轻量模式，而不是只留在 development-only 深校验里。 |
| UnLua | `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 都直接基于 live `lua_State` 取值，调试原语不是“只在另一条验证构建链里存在”的附属能力。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-721` | 真正进入运行态之后仍保留最小可观察面，比“启动时做完校验就彻底失去细粒度哨兵”更利于扩展远程诊断。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 development-only 深校验，但为 packaged JIT 增加 always-on、低成本的 `AbiSignature`/`LayoutSignature` 校验层，把今天的“整包匹配”补成“整包匹配 + 轻量布局哨兵”。 |
| 具体步骤 | 1. 在生成 `PrecompiledScript.Cache` 或 `AS_JITTED_CODE` 时，额外写出每个 compiled unit / function 依赖的 `TypeSize`、`TypeAlignment`、`PropertyOffset` 摘要；第一阶段可直接复用 `VerifyPropertyOffsets/VerifyTypeSizes/VerifyTypeAlignments` 已经收集的事实，不改现有生成结果。 2. 在 `PrepareToFinalizePrecompiledModules()` 中保留当前 raw pointer/offset patching，但在 packaged profile 下也执行 cheap hash compare；如果签名不一致，则只禁用受影响 unit/function 的 JIT 注册并输出定向日志，而不是继续假设整包可用。 3. development/debug profile 继续保留现有 `checkf` 深校验，形成 “strict verify + packaged-safe verify” 双层模型。 4. 把校验结果写入 JIT manifest 或 load-time diagnostics，至少能回答 `哪个 FunctionId / Type / Property 导致 unit 降级`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 预估工作量 | M |
| 架构风险 | 签名粒度过粗会放过真实 ABI 漂移，过细又可能在不同 target/toolchain 间产生误报；第一阶段应只覆盖真正参与 JIT relocation 的类型、属性与对齐信息。 |
| 兼容性 | 完全可增量实施。没有 `AbiSignature` 的旧产物继续走当前路径；有 sidecar 的新产物才启用 cheap verify。现有 `PrecompiledDataGuid` 与 `AS_JIT_VERIFY_PROPERTY_OFFSETS` 行为都可保留。 |
| 验证方式 | 1. 在 `TEST/SHIPPING` 风格 profile 下构造一个仅影响单个 property offset 或 type alignment 的变更，确认运行时给出定向 disable，而不是静默继续。 2. 维持现有 `PrecompiledDataGuid` 不变的兼容场景，确认 cheap verify 仍能发现布局漂移。 3. development/debug profile 下回归现有 `checkf` 路径，保证严格校验不退化。 4. 缺少 sidecar 的旧构建仍应保持今天的整包行为。 |

### Arch-DT41：`PrecompiledData` 已经携带 rich symbol，但 optimized lane 在启动后主动抹除它们，缺少独立 `DebugSymbols` 交付物

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | optimized runtime 的语义元数据保留、只读远程诊断、IDE/toolchain 对 precompiled 产物的复用 |
| 当前设计 | `PrecompiledData` 序列化时已经保存了函数名、参数名、默认值、变量信息、`DeclaredAt` 和 `LineNumbers` 等 rich symbol，但非 development-like 路径会打开 `bMinimizeMemoryUsage`，创建函数时跳过参数名/默认值，启动后又清空 method/property table、enum value、`defaultArgs`、`parameterNames`，随后直接释放 `PrecompiledData` 与 `FJITDatabase`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h:79-103` 的 `FAngelscriptPrecompiledFunction` 明确保存了 `ParameterNames`、`ParameterDefaultArgs`、`VariableInfoProgramPos`、`DeclaredAt`、`LineNumbers`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:501-521` 在 `Create()` 中仅在 `!Context.bMinimizeMemoryUsage` 时回填 `parameterNames/defaultArgs`；`.../PrecompiledData.cpp:563-579` 仍会把 `VariableInfo*`、`declaredAt`、`lineNumbers` 恢复进 `scriptData`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1547-1548` 在非 script development mode 下设置 `PrecompiledData->bMinimizeMemoryUsage = true`；`.../AngelscriptEngine.cpp:1597-1602` 启动后立即调用 `ClearUnneededRuntimeData()`、删除 `PrecompiledData` 并 `FJITDatabase::Clear()`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2481-2548` 又进一步清空 `propertyTable/methodTable/enumValues/defaultArgs/parameterNames`。 |
| 优点 | 运行时 steady-state 内存足迹更小，不必长期保留大体量符号与默认参数数据。 |
| 不足 | 当前系统其实已经在 bootstrap 期拿到了构建轻量只读调试符号所需的大部分事实，但这些事实要么被有条件跳过回填，要么在启动后立即抹除。推断：后续若要在 optimized build 支持只读远程调试、crash symbolization、离线 `GoToDefinition` 或多 IDE front-end 共享索引，当前运行态已经没有稳定 owner，只能重新跑外部工具链重建。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense 产物被持久化到 `Intermediate/IntelliSense`，Editor 常驻监听器与 commandlet 共用同一 `SaveFile()/DeleteFile()` 语义；与此同时，runtime 侧仍可用 `GetStackVariables()` / `GetLuaCallStack()` 查询 live 调试信息。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:29-132`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-721` | 即使 runtime 保持精简，也可以把 rich symbol 作为独立 artifact 持久化，而不是在 bootstrap 后彻底丢失。 |
| puerts | `V8InspectorClientImpl` 在真实 runtime context 中创建 discovery/session，连接期内一直保留 `V8InspectorSession` 和 channel owner，不需要在启动后重新生成一份平行调试模型。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:282-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-555` | 把调试/索引所需事实挂到显式 owner 或 sidecar 上，才能在 runtime 已进入稳态后继续提供只读观察能力。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 `bMinimizeMemoryUsage + ClearUnneededRuntimeData()` 路径的前提下，新增独立 `DebugSymbols`/`SymbolPack` 交付物，把今天只在 bootstrap 期短暂存在的 rich symbol 提前落盘。 |
| 具体步骤 | 1. 基于 `FAngelscriptPrecompiledFunction` 现有字段新增 `PrecompiledScript.DebugSymbols.cache` 或 `AngelscriptDebugSymbols.json`，至少包含 `FunctionId`、signature、parameter names/defaults、`DeclaredAt`、`LineNumbers`、`VariableInfo*`、source section identity；第一阶段只序列化 `PrecompiledData` 已有事实，不额外引入 Editor-only 文档字段。 2. 在 `as-generate-precompiled-data` 路径里于 `ClearUnneededRuntimeData()` 之前生成 sidecar；runtime 继续按今天的方式清内存，不改变 steady-state footprint。 3. 新增轻量 loader/API，让未来 `PrecompiledValidate`、只读 `DebugServer`、crash symbolizer 或 IDE indexer 能在不恢复完整 `PrecompiledData` 的情况下按 `FunctionId` 查询 source/signature。 4. 若后续需要，再把 `FAngelscriptDocs` 或 `DebugDatabase` 中可稳定 join 的文本字段作为 overlay 附加到同一 sidecar，但首阶段不要把 Editor-only `meta/keywords` 强塞进 optimized lane。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptDebugSymbols.*` |
| 预估工作量 | M |
| 架构风险 | sidecar 的 `FunctionId`/`PrecompiledDataGuid` 需要与主缓存严格绑定；若 join key 设计不稳，可能出现“符号包与运行时函数不匹配”的新型错误。第一阶段应坚持从同一轮 `PrecompiledData` 直接派生，而不是二次扫描 engine。 |
| 兼容性 | 完全向后兼容。没有 sidecar 的旧 precompiled 产物继续按今天行为运行；sidecar 只为新工具和只读诊断提供附加能力，不改变脚本执行语义。 |
| 验证方式 | 1. 生成一轮 `PrecompiledScript.Cache`，确认同步产出 `DebugSymbols` sidecar，并且字段可与 `FunctionId`、`DeclaredAt`、`LineNumbers` 对账。 2. 在 optimized profile 启动后，确认 `ClearUnneededRuntimeData()` 已执行，但 sidecar loader 仍能根据 `FunctionId` 还原函数签名和源码位置。 3. 缺少 sidecar 时回归现有路径，确保运行行为不变。 4. 用 sidecar 驱动一个最小只读工具，验证无需恢复完整 `PrecompiledData` 也能做 source lookup。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT40 | packaged precompiled/JIT 的 ABI 校验空窗、布局漂移诊断 | 运行时校验层增强 + unit 级降级 | 高 |
| P2 | Arch-DT41 | optimized lane 的 rich symbol 保留、只读远程诊断、独立 debug symbol artifact | sidecar 新增 + 运行时保守保留策略 | 中高 |

---

## 架构分析 (2026-04-09 00:18)

### Arch-DT42：调试 attach 仍是“晚绑定 + 破坏性启动”，启动期脚本与重连语义缺少稳定 bootstrap policy

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | attach 时机、启动期断点、重连恢复、多前端接管 |
| 当前设计 | `DebugServer` 虽然会在 development-like profile 初始化时创建，但真正进入调试态要等客户端发送 `StartDebugging`；在此之前脚本行回调不会因调试而持续开启。更关键的是，`StartDebugging` 会立即清空现有断点，而 `Pause` 也只是把 `bBreakNextScriptLine` 置真，等待下一条脚本行命中。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1452-1455` 说明 server 在 engine 初始化时创建；`.../AngelscriptEngine.cpp:5434-5460` 说明行回调是否常驻取决于 `DebugServer->bIsDebugging` / `bBreakNextScriptLine`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:822-913` 中 `Pause` 只是设置 `bBreakNextScriptLine = true`，而 `StartDebugging` 会 `bIsDebugging = true`、写入 `DebugAdapterVersion`、`BreakOptions.Empty()` 并 `ClearAllBreakpoints()`；`.../AngelscriptDebugServer.cpp:465-626` 的 `ProcessScriptLine()` 只有在后续脚本行回调真正进入时才会把 `bBreakNextScriptLine` 转成 `PauseExecution()`；`.../AngelscriptDebugServer.cpp:667-691` 的 `PauseExecution()` 则发生在命中后，而不是 attach 时。 |
| 优点 | 单 IDE、本机联调路径非常直接，客户端只要连上后发 `StartDebugging`/`Pause` 就能接管当前开发态进程。 |
| 不足 | attach 是“时机敏感”的：如果客户端在启动期脚本执行后才连上，就天然错过那段执行；如果客户端重连或切换前端，`StartDebugging` 还会把已有断点集合清空。结果是远程 attach、断线重连、controller/observer 双前端等场景缺少稳定的 bootstrap 语义，协议拆层之后仍然会漏掉最关键的早期执行窗口。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `V8Inspector` 在 runtime context 建立时先执行 `contextCreated()`，HTTP discovery 和 websocket accept 随后开启；真正的前端连接只在 `OnOpen()` 时创建 `V8InspectorChannel`，pause loop 也只是服务已存在的 inspector session，而不是 attach 时重写 runtime 执行策略。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:299-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:572-589` | 把“runtime 已可被调试”与“某个前端此刻接入”分开，attach 可以变成 session 建立，而不是执行期开关。 |
| UnLua | 调试原语直接以 `lua_State*` 为输入暴露 `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()`，不依赖一个 transport 级 `StartDebugging` 才能先把 runtime 切到可观察状态。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-724` | 先把“取当前堆栈/locals”的能力做成稳定 runtime 原语，再决定 attach/IDE 如何消费，能减少接入时机对调试语义的影响。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为调试引入显式 `bootstrap policy` 和可重放的 session state，把 attach 从“破坏性启动动作”改成“接管既有调试宿主”。 |
| 具体步骤 | 1. 新增 `FAngelscriptDebugBootstrapPolicy`（例如 `ListenOnly`、`PauseOnFirstScriptLine`、`WaitForDebugger`、`ReadonlyInspect`），由设置或命令行控制是否在首段脚本执行前就 arm line callback。 2. 在 `DebugServer` 或未来 session 层新增 `PendingBreakpointStore` / `PendingDebugSession`，将断点集合与 attach session 解耦；`StartDebugging` 默认不再无条件 `ClearAllBreakpoints()`，只有显式 `bResetBreakpoints` 时才清。 3. 扩展握手或 `FStartDebuggingMessage`，增加可选 `pauseOnStart`、`resumePolicy`、`reattachSessionId`；旧前端不带这些字段时继续沿用当前行为。 4. 对 `Pause` 增加 “startup pending stop” 语义：当脚本尚未进入下一条可断行时，server 记录一个 pending stop request，而不是仅靠“等下一次行回调”这一条路径。 5. 在测试层补三类场景：启动期脚本首行暂停、断线后重连恢复断点、一个只读观察端与一个控制端并存接管。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugBootstrapPolicy.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptPendingDebugSession.*` |
| 预估工作量 | M |
| 架构风险 | 如果过早把 bootstrap policy 与 line callback 常驻化，可能放大 development profile 的运行时开销；第一阶段应先只引入 `PauseOnFirstScriptLine/WaitForDebugger` 这类显式 opt-in 模式。 |
| 兼容性 | 可增量实施。旧前端继续使用现有 `StartDebugging`/`Pause` 语义；只有新字段或新 policy 打开时才启用启动期暂停、断点重放和 reattach。 |
| 验证方式 | 1. 构造启动即执行脚本的用例，验证 `PauseOnFirstScriptLine` 能在首个可断行停住。 2. 模拟前端断线后重连，确认断点不会因第二次 `StartDebugging` 被清空。 3. 一个 client 只读订阅、另一个 client 发 `StartDebugging`，验证 attach 行为不再破坏共享断点状态。 4. 旧 VS Code 扩展不升级时仍能按现有路径继续单步与查看变量。 |

### Arch-DT43：`DumpDocumentation()` 以裸字符串作为唯一 key，把 type/namespace/enum 压扁到同一平面，离线 docs 天然存在命名冲突

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 文档工件身份、namespace/type 分层、离线导航与索引稳定性 |
| 当前设计 | `DumpDocumentation()` 用 `TMap<FString, FDocClass>` 收集所有 docs，object type 用 `TypeName` 作为 key，namespace/global bucket 用 `NSName` 作为 key，enum 甚至直接按 `EnumType->GetName()` 建桶；最终所有结果都扁平落到 `Docs/angelscript/generated/<ClassName>.hpp`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:410-430` 定义了 `TMap<FString, FDocClass> Classes`；`.../AngelscriptDocs.cpp:519-527` 对 object type 直接 `Classes.FindOrAdd(TypeName)`；`.../AngelscriptDocs.cpp:599-603` 对全局函数按 `Func->GetNamespace()` 建桶；`.../AngelscriptDocs.cpp:615-625` 对 enum 则用 `EnumType->GetName()` 建桶并把枚举值塞进去；`.../AngelscriptDocs.cpp:629-637` 又把 namespace/global bucket 直接 `Classes.FindOrAdd(NSName)`；`.../AngelscriptDocs.cpp:682-755` 最终使用 `ClassDoc.ClassName + ".hpp"` 作为平面文件名写入 `Docs/angelscript/generated`。 |
| 优点 | 在类型名和 namespace 名都比较规整、且不存在重名的中小型项目里，这种实现足够简单，导出的 `.hpp` 也易于人工浏览。 |
| 不足 | 这里的 identity 已经塌缩了：同名 type、namespace、enum 会共用同一个 `Classes` key；不同 module/namespace 下的同名短类型也会竞争同一个 `<ClassName>.hpp`。这不仅影响 docs，可一旦后续 symbol index、导航清单或 IDE 工具继续复用这些路径，冲突会被放大成“索引错误指向/覆盖上一份工件”的系统性问题。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense 导出先取 `Package->GetName()` 作为 `ModuleName`，再取类型名作为 `FileName`，最终通过 `SaveFile(ModuleName, FileName, Content)` 和 `DeleteFile(ModuleName, FileName)` 写到 `OutputDir/ModuleName/FileName.lua`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245` | 工件身份至少应包含“所属模块/包 + 局部名”，而不是只靠一个展示字符串；删除/重命名也才能精确命中。 |
| UnLua | 类型收集阶段还会过滤 `SKEL_`、`REINST_`、`HOTRELOADED_` 等临时类型，避免把 transient identity 也落成长期 artifact。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:188-205` | 一旦某条身份会进入磁盘工件，就应该先明确它是否稳定、是否可被长期引用。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 给 docs dump 引入稳定 `DocSymbolId` 与分层输出路径，把 type/namespace/enum 的 artifact identity 从“显示名”升级为“kind + scope + name”。 |
| 具体步骤 | 1. 将 `TMap<FString, FDocClass>` 改为 `TMap<FDocNodeKey, FDocClass>` 或等价结构，`FDocNodeKey` 至少包含 `Kind(type/namespace/enum)`、`Namespace`、`ModuleName`、`DisplayName`。 2. 将 enum 收集从今天的“按 `EnumType->GetName()` 假装 namespace bucket”改成独立 `EnumDoc` 节点；global/namespace 与 object type 也不再共享同一个 `Classes` map key。 3. 将输出路径改成分层目录，例如 `Docs/angelscript/generated/{types,namespaces,enums}/...` 或 `Intermediate/AngelscriptToolchain/Docs/...`，文件名使用 sanitize 后的 qualified name；同时生成 `DocsIndex.json`，显式记录 `DocSymbolId -> relativePath`。 4. 保留旧的平面 `<ClassName>.hpp` 作为兼容 alias，但只在名字唯一时生成；一旦发生冲突，就写 deterministic suffix 并在 index/日志中标明。 5. 增加回归样例：type 与 namespace 同名、不同 namespace 的同名 type、同名 enum，以及热重载后 transient type 被过滤的场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptDocsIndex.*` |
| 预估工作量 | M |
| 架构风险 | 如果直接切掉旧平面文件名，现有依赖 `Docs/angelscript/generated/<Name>.hpp` 的脚本或工具可能短期失效；因此应先“双写 + index”，再逐步让消费方切到稳定 `DocSymbolId`。 |
| 兼容性 | 可增量实施。第一阶段继续保留旧路径，只增加分层路径与 `DocsIndex.json`；只有发生名称冲突时才需要工具读取 index 进行精确跳转。 |
| 验证方式 | 1. 构造两个不同 namespace 下同名 type，确认生成两个不同 `DocSymbolId` 与不同输出路径，不再互相覆盖。 2. 构造一个 namespace 与一个 type 同名的场景，确认 docs dump 不再合并成同一个 `FDocClass`。 3. 删除或重命名某个符号后，确认 index 和兼容 alias 都能正确更新，不留下错误旧文件。 4. 回归现有依赖平面路径的工具，确认在“无重名”场景下仍可正常读取旧 `.hpp`。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT42 | 调试 attach 时机、启动期暂停、断线重连/多前端接管 | session bootstrap 新增 + 断点状态解耦 | 高 |
| P1 | Arch-DT43 | docs 工件身份、namespace/type/enum 分层、离线索引稳定性 | artifact identity 重构 + 兼容双写 | 高 |

---

## 架构分析 (2026-04-09 00:27)

### Arch-DT44：`DumpDocumentation()` 的离线导出仍是“只写不收”，`Docs/angelscript/generated` 缺少 authoritative 清理边界

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 离线 docs 工件生命周期、陈旧文件回收、工具链快照可信度 |
| 当前设计 | `DumpDocumentation()` 在 `WITH_EDITOR` 下遍历当轮 `Classes` 并直接把结果写到 `ProjectDir/Docs/angelscript/generated/<ClassName>.hpp`；调用点 `-dump-as-doc` 写完后立刻退出进程，但写出阶段没有 manifest、没有输出目录扫描、也没有删除“本轮未再生成”的旧文件。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:675-755` 的整个写出阶段只有 `for (auto It : Classes)` 和 `FFileHelper::SaveStringToFile(Content, *Filename)`，目标路径固定为 `FPaths::ProjectDir() / "/Docs/angelscript/generated" / ClassDoc.ClassName + ".hpp"`；同一段代码里没有 `FindFiles`、`Delete`、`MakeDirectory` 或 manifest 读写。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2223-2228` 则表明这条链路由 `RuntimeConfig.bDumpDocumentation` 触发，写完马上 `FPlatformMisc::RequestExit(false)`。 |
| 优点 | 实现极简，适合一次性人工导出和临时浏览，不需要额外的清理协议或索引文件。 |
| 不足 | 文档目录会逐步积累“已删除/已改名符号”的旧 `.hpp`，而调用方又缺少 authoritative 清单判断哪些文件仍然有效。结果是离线 docs 更像半手工缓存而不是可信快照；一旦 IDE、脚本导航或后续索引管线读取这个目录，就可能把陈旧文件当成当前事实。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense 产物固定写到插件 `Intermediate/IntelliSense`，并且 `SaveFile()` 只在内容变化时覆写；资产删除/重命名时，`OnAssetRemoved()` / `OnAssetRenamed()` 会精确调用 `DeleteFile()` 删除旧工件。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:260-279` | 离线 IDE 工件必须有明确 owner、稳定输出根和删除语义，不能只追加或覆写“当前还记得的文件”。 |
| puerts | Inspector discovery 不依赖磁盘快照，而是在启动时准备 `JSONList` / `JSONVersion`，并在 `/json/list`、`/json/version` 请求时按当前 runtime 状态即时返回。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477` | 即便是工具前端消费的数据，也应优先来自可即时验证的 authoritative contract，而不是无法判断新旧的残留文件集合。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 docs dump 增加显式 manifest 和清理阶段，把 `Docs/angelscript/generated` 从“写出目录”升级为“可验证快照”。 |
| 具体步骤 | 1. 新增 `AngelscriptDocsManifest.json`，至少记录 `DocSymbolId`、`relativePath`、`contentHash`、`generatedAt` 和生成命令版本；首阶段可放在 `Intermediate/AngelscriptToolchain/Docs/`，避免和手写文档混在一起。 2. 让 `DumpDocumentation()` 先生成“本轮应存在文件集”，再对比上一次 manifest 删除未再生成的旧文件；保留 `SaveStringToFile()`，但增加内容 hash 比对，避免无变化重写。 3. 第一阶段继续镜像输出当前 `Docs/angelscript/generated/*.hpp` 路径作为兼容副本，同时把 manifest 指向新的 authoritative 根目录；旧路径只作为 mirror，不再作为唯一真相来源。 4. 为 rename/delete 增加最小回归：同一符号改名后旧文件必须被清理，新文件路径必须可由 manifest 唯一定位。 5. 后续如需 IDE/索引消费，优先读取 manifest 而不是直接扫目录。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptDocsManifest.*` |
| 预估工作量 | M |
| 架构风险 | 如果过早把 authoritative 根从 `Docs/` 直接切到 `Intermediate/`，现有读取固定路径的工具会短期失效；因此第一阶段应该“双写 + manifest”，先让消费者切到 manifest，再考虑收缩 legacy mirror。 |
| 兼容性 | 完全可增量实施。现有 `.hpp` 路径可继续保留；新增 manifest 和清理逻辑只会删除“本轮已明确不再存在”的陈旧生成文件，不影响脚本运行时语义。 |
| 验证方式 | 1. 构造一个已导出的类型后再重命名或删除，确认旧 `.hpp` 会在下一次 dump 后被移除。 2. 在无源码变化的情况下连续执行两次导出，确认未变化文件不会被重写。 3. 让一个简单索引脚本只读取 manifest，验证其不再依赖目录枚举顺序或残留文件。 4. 回归现有读取 `Docs/angelscript/generated` 的工具，确认 mirror 模式下仍能工作。 |

### Arch-DT45：`StaticJIT` 的 module/header/unity shard 仍依赖 `TMap` 遍历与 encounter-order 命名，生成结果缺少 deterministic build contract

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | JIT 生成工件的确定性、增量编译稳定性、模块级 artifact identity |
| 当前设计 | `StaticJIT` 用 `TMap<asIScriptModule*, TSharedPtr<FJITFile>> JITFiles` 和 `TMap<FString, TSharedPtr<FSharedHeader>> SharedHeaders` 收集中间结果；模块第一次被看到时由 `GetUniqueModuleName()` 结合 `UsedUniqueNames` 生成名字，随后直接按 `TMap` 迭代顺序写 `.as.jit.hpp` 与 `AngelscriptJitCode_N.jit.cpp`，unity shard 又按“累计行数是否超过阈值”决定切分。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h:406-472` 定义了 `JITFiles`、`SharedHeaders` 和 `UsedUniqueNames`；`.../AngelscriptStaticJIT.cpp:118-143` 在 `GetFile()` 首次遇到模块时 `JITFiles.Add(Module, File)`；`.../AngelscriptStaticJIT.cpp:226-247` 的 `GetUniqueModuleName()` 通过 `UsedUniqueNames` 检测碰撞并按遇到顺序追加 `__%d`；`.../AngelscriptStaticJIT.cpp:3515-3523` 与 `:3582-3680` 则直接 `for (auto ModuleElem : JITFiles)` 遍历写 header/module/unity 文件，并用 `LinesInCombinedFile > 200000` 与 `CombinedFileIndex` 决定 `AngelscriptJitCode_%d.jit.cpp` 的切分。 |
| 优点 | 当前实现已经具备旧文件清理和 changed-only 写回能力，单次生成流程完整，而且不需要额外的 shard planner。 |
| 不足 | 工件 identity 仍然是“遍历副产物”而不是稳定合同：模块/共享头/combined unity file 的分组会受到模块首次出现顺序、名字碰撞顺序和单文件行数变化影响。结果是很小的脚本变动也可能让后续 `AngelscriptJitCode_N.jit.cpp` 重新分桶，放大编译缓存失效和 diff 噪音，并阻碍外部工具把 JIT 产物稳定地映射回 module 级事实。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense 输出路径由 `ModuleName + FileName` 显式决定，`SaveFile()` 仅在内容变化时覆写；删除/重命名则通过同一 identity 精确清理旧文件。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:260-279` | 对外可消费的工件应由显式 identity 驱动，而不是由运行时容器遍历顺序隐式决定。 |
| puerts | Inspector discovery 直接暴露固定 `id`、`title` 和 `webSocketDebuggerUrl`；前端依赖的是稳定 target contract，而不是 server 内部对象枚举顺序。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:330-344`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:459-477` | 调试/工具链产物只要会被外部消费，就应该优先给出显式、稳定的 identity，而不是让消费方跟随内部遍历顺序漂移。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `StaticJIT` 输出从“遍历时顺手命名”改为“先规划 identity，再写文件”，为 module/header/unity shard 引入确定性的 planner 和 manifest。 |
| 具体步骤 | 1. 在写文件前把 `JITFiles` 和 `SharedHeaders` 投影为按稳定 key 排序的数组；稳定 key 至少包含完整 `ModuleName` 或 header filename，而不是 `TMap` 当前遍历顺序。 2. 将 `GetUniqueModuleName()` 的碰撞处理改为 deterministic slug，例如 `SanitizedModuleName + short hash(full module name)`；避免同名模块仅因遇到顺序不同而生成不同 `__1/__2` 后缀。 3. 新增 `AngelscriptJitManifest.json`，记录 `moduleName -> headerFile -> unityShard` 映射、内容 hash 与 `PrecompiledDataGuid`；后续 CI、IDE 或差异分析都以 manifest 为 join key。 4. 把 unity file 切分从“顺序累积行数”升级为“基于已排序模块列表和固定预算的 shard planner”；同一模块不变化时，尽量保持落在同一 shard。 5. 首阶段保留现有 `AngelscriptJitInfo.jit.cpp` 与绝大多数 `.as.jit.hpp` 名称，允许 unity file 发生一次性重排；等 manifest 稳定后再决定是否统一命名风格。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptJitManifest.*` |
| 预估工作量 | M |
| 架构风险 | 如果稳定 key 选得不对，可能导致真正不同的模块/header 被错误合并到同一 identity；因此第一阶段应保守地使用“完整模块名 + hash”而不是尝试做过度人类可读的缩写。 |
| 兼容性 | 对脚本执行语义零影响，但生成文件名和 unity shard 归属可能在首轮引入一次性全量重编。保持 `AngelscriptJitInfo.jit.cpp` 不变，并尽量复用原有 `.as.jit.hpp` 名称，可把外部影响降到最小。 |
| 验证方式 | 1. 对同一输入连续生成两次，确认 manifest 与所有 JIT 文件 byte-identical。 2. 人工打乱模块编译顺序或构造同名不同命名空间模块，确认稳定 slug 和 shard 归属不变。 3. 仅改动单个模块代码，确认只影响该模块文件及其所在 unity shard，而不是让后续所有 `AngelscriptJitCode_N.jit.cpp` 连锁改名。 4. 用 manifest 对照 `PrecompiledDataGuid` 和 module 名称，验证外部工具可稳定追踪某个 module 的 JIT 产物。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT44 | docs 离线导出的 authoritative 清理边界、陈旧文件治理 | artifact lifecycle 收敛 + manifest 新增 | 高 |
| P2 | Arch-DT45 | StaticJIT shard/unity 输出的确定性与增量稳定性 | 生成规划器增强 + manifest 新增 | 中高 |

---

## 架构分析 (2026-04-09 00:34)

### Arch-DT46：`DebugServer` 的 transport 仍在脚本执行线程内阻塞收包，远程调试延迟会直接反压运行时

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试 transport 调度模型、远程调试稳定性、多前端并发下的运行时隔离 |
| 当前设计 | `FAngelscriptDebugServer` 把 socket 收包、消息分发、暂停等待和脚本执行期断点处理放在同一线程 lane；一旦进入暂停态或脚本行回调，网络收包仍由当前执行线程同步完成。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:395-408` 仅创建 `FTcpListener` 并接受原始 TCP 连接；`.../AngelscriptDebugServer.cpp:440-448` 在 `ProcessException()` 中直接调用 `ProcessMessages()`；`.../AngelscriptDebugServer.cpp:621-627` 命中断点后进入 `PauseExecution()`；`.../AngelscriptDebugServer.cpp:667-695` 在暂停循环里持续 `ProcessMessages()`；`.../AngelscriptDebugServer.cpp:702-709` 的 `SleepForCommunicate()` 也走同一路径；`.../AngelscriptDebugServer.cpp:712-789` 对每个 client 先读包长，再用 `while (BytesReceived < PacketSize) { Client->Recv(...) }` 同步读完整包。 |
| 优点 | 单线程时序简单，调试命令与脚本停顿点天然顺序一致，不需要额外的跨线程状态同步。 |
| 不足 | transport backpressure 会直接进入脚本执行路径：慢客户端、分片 TCP 包或高 RTT 远程连接，都会让 `PauseExecution()` / `ProcessException()` / `SleepForCommunicate()` 在当前执行线程上等待网络收完再继续；这会放大远程调试抖动，也让多个 IDE 前端共享同一个阻塞点。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `V8Inspector` 先抽象 `V8InspectorChannel`，再由 websocketpp/asio 负责 `http/open/message/close` 事件；收到 websocket 文本消息后才把 payload 转发给 `V8InspectorSession`，transport 与调试语义分离。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-74`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-122`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:315-328`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-526` | 先把网络 IO 留在独立事件层，再把协议消息投递给会话对象，避免运行时调试语义直接承担 socket 收包阻塞。 |
| UnLua | 运行时只暴露 `GetStackVariables`、`GetLuaCallStack`、`PrintCallStack` 这类本地调试原语，不在核心调试 API 内承担 IDE transport。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-732` | 先把“取栈/取变量”做成纯 runtime API，再由外层工具选择如何传输，能避免网络波动污染 VM 执行路径。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `DebugServer V2` 消息格式，但把 socket IO 从脚本执行线程剥离成独立 transport pump，Runtime 只消费已经解包的命令队列。 |
| 具体步骤 | 1. 新增 `Debugging/Transport/AngelscriptDebugTransportPump.*`，负责 listener、accept、非阻塞收包、长度前缀 envelope 解包和 client 断连检测；它只产出 `FDebugTransportCommand` 队列，不直接改 `bIsPaused` 或断点状态。 2. `FAngelscriptDebugServer` 保留为调试 session 编排层，在 `Tick()`、`ProcessException()`、`ProcessScriptLine()` 的安全点只 drain 已完成的命令队列；`ProcessMessages()` 的同步 `Recv` 循环改为 transport pump 内部实现。 3. 将 `PauseExecution()` 的 `while (bIsPaused) { ProcessMessages(); Sleep(0); }` 改为等待 transport event/command queue，避免暂停态递归执行 socket 收包。 4. 第一阶段继续复用当前二进制 `EDebugMessageType` 和 `SerializeDebugMessageEnvelope()`；如后续要接 WebSocket / DAP / CDP bridge，只新增 transport adapter，不改 session 逻辑。 5. 为 `HasStopped` / `HasContinued` / `PingAlive` 增加异步出站队列，避免慢 client 把广播路径重新拉回主线程。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transport/AngelscriptDebugTransportPump.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transport/AngelscriptDebugTransportQueue.*` |
| 预估工作量 | L |
| 架构风险 | 最大风险是把 socket IO 挪走后，`Pause/Continue/Step` 的时序会从“立即在当前栈帧处理”变成“命令队列在下一个安全点处理”；如果 queue drain 点选得不对，可能出现 UI 已显示按钮成功、但脚本端晚一拍生效的体感回归。 |
| 兼容性 | 完全可增量实施。现有 VS Code 扩展和 `DebugServer V2` 协议不需要改包格式；变化只在插件内部线程模型。极端情况下可保留 `LegacyInlinePump` 开关作为回退。 |
| 验证方式 | 1. 构造分片 TCP 包和高 RTT 远程连接，验证 `PauseExecution()` 不再因客户端缓慢发包而长时间卡住。 2. 两个 client 并发连接，一个持续收 `DebugDatabase`，另一个执行单步，确认慢消费者不会拖慢 stepping。 3. 回归 `AngelscriptDebugProtocolTests.cpp`，确认 wire format 保持兼容。 4. 在脚本异常、断点、`SleepForCommunicate()` 三条路径下验证命令处理时序一致。 |

### Arch-DT47：工具链没有 authoritative symbol graph，`UHT` / `Docs` / `DebugDatabase` / `StaticJIT` 仍在各自重建同一批事实

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 工具链统一事实源、JIT/文档/调试数据库协同、扩展新模块时的演进成本 |
| 当前设计 | 目前至少有四套独立的符号/能力枚举器：`AngelscriptUHTTool` 从 `UHT session` 重建函数表；`DumpDocumentation()` 从 live script engine 枚举 docs；`SendDebugDatabase()` 再枚举一次 live script engine 生成 IDE schema；`StaticJIT` 则维护自己的 `FunctionsToGenerate` 集合。它们之间共享的只有零散 `FAngelscriptDocs` 查表，而不是统一 symbol graph。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h:16-36` 公开的是 docs lookup API，而不是统一符号模型；`.../AngelscriptDocs.cpp:407-756` 的 `DumpDocumentation()` 自己遍历 object types、global functions、global properties 和 enums 再写 `.hpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1493-2034` 的 `SendDebugDatabase()` 再次遍历 types / methods / globals / enums 组装 JSON；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:334-385` 通过解析 `AngelscriptRuntime.Build.cs` 文本决定支持模块，再在 `...:449-515` 里遍历 `UhtClass/UhtFunction` 重建可生成函数；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h:416` 定义私有 `FunctionsToGenerate`；`.../AngelscriptStaticJIT.cpp:66-75`、`...:3491-3497`、`...:3604-3607` 则说明 JIT 覆盖集合只存在于自身生成流程里。 |
| 优点 | 各链路可以独立推进，某个子系统加字段时不必等待“全工具链统一重构”才能落地；这也是当前功能快速增长的原因之一。 |
| 不足 | 扩展成本被转嫁成长期漂移风险：新增模块、过滤规则、符号字段或 JIT 能力时，需要分别修改 `Build.cs` 解析、UHT exporter、runtime docs、runtime debug database 和 JIT 内部集合。尤其 `LoadSupportedModules()` 通过扫描 `Build.cs` 文本推断模块范围，意味着一个格式调整都可能改变工具链输入，而这些变化不会自动回流到 docs/debug/JIT 视图。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | Editor 常驻生成器和 commandlet 虽然入口不同，但都复用 `GenerateIntelliSense` 产物与 `SaveFile()` 写出约定；离线工件目录、文件命名和删除语义是一套共享 contract。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:143-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-99` | 工具链可以有多个入口，但应该共享同一套“符号如何落成 artifact”的 contract，而不是每个入口各自重建 shape。 |
| puerts | 调试工具链入口被压缩成 `V8Inspector` / `V8InspectorChannel` 接口和 `CreateV8Inspector(int32_t Port, void* InContextPtr)` 工厂，外层组件依赖显式契约而不是去解析构建脚本或窥探内部容器。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-74` | 扩展点先收敛为显式 schema/factory，再让不同工具消费该 schema，能避免“每条链都各自猜状态”的演进方式。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 新建统一的 `AngelscriptSymbolGraph` 作为 authoritative contract，让 `UHT`、`DebugDatabase`、`Docs`、`StaticJIT` 都变成这个 graph 的不同 producer / view，而不是平行的事实源。 |
| 具体步骤 | 1. 新增 shared schema，例如 `Toolchain/AngelscriptSymbolGraph.json` 或等价 C++/C# DTO，节点至少覆盖 `module/type/function/property/enum`，并允许附加 facet：`docs`、`uhtBinding`、`debug`、`jitCapability`。 2. 第一阶段让 `AngelscriptUHTTool` 在保持 `AS_FunctionTable_*.cpp`、summary JSON/CSV 不变的前提下，额外输出 `AngelscriptSymbolGraph.native.json`；同时把 `LoadSupportedModules()` 从 `Build.cs` 文本扫描迁到显式 manifest，旧解析逻辑仅作为兼容 fallback。 3. 第二阶段让 `SendDebugDatabase()` 和 `DumpDocumentation()` 优先消费 graph 中的 native/reflected slice，只对 script-only 符号做 live merge；这样 IDE schema 与离线 docs 就不再各自重写一套枚举/过滤逻辑。 4. 第三阶段让 `StaticJIT` 把 `FunctionsToGenerate`、`FullyJitted/HybridFallback/InterpreterOnly` 结果写回同一 graph 的 `jitCapability` facet，而不是继续只存在于进程内 map；这会把 JIT 覆盖率自然接入现有工具链。 5. 所有现有输出继续保留为 derived views：`AS_FunctionTable_*` 继续给编译器，`DebugDatabase` 继续给 IDE 前端，`.hpp` docs 继续给离线浏览；差别只在它们都从同一事实源派生。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptSymbolGraph.*`，新增 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptSymbolGraphWriter.cs` |
| 预估工作量 | XL |
| 架构风险 | 统一 graph 的最大风险不是实现量，而是错误地把“live script-only 信息”和“native/reflected 静态信息”混成一个时序不清的模型；第一阶段必须明确 graph 只先承载 authoritative native/reflected slice，script-only 数据继续在 runtime merge。 |
| 兼容性 | 可以完全分阶段推进。旧 `.cpp` / `.csv` / `.hpp` / `DebugDatabase` 输出全部保留；缺少 graph 时继续走当前实现。真正的兼容点只有 `Build.cs` 解析切换，需要先双轨运行一个版本周期。 |
| 验证方式 | 1. 对同一源码快照生成一次 `symbol graph`、`DebugDatabase`、`.hpp docs`、`AS_FunctionTable_Summary.json`，校对类型/函数/属性总量是否一致。 2. 仅调整 `AngelscriptRuntime.Build.cs` 格式不改依赖，确认新 graph/UHT 输出不发生语义变化。 3. 对一个新增 `BlueprintCallable` 函数，验证 graph、UHT shard、docs 和 debug database 四处都在同一轮构建内出现。 4. 给一段已 JIT 的脚本和一段 fallback 脚本分别跑生成，确认 `jitCapability` facet 与运行时实际行为一致。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT46 | 调试 transport 调度模型、远程调试 backpressure、多前端稳定性 | 结构性重构 + IO lane 解耦 | 高 |
| P1 | Arch-DT47 | 工具链 authoritative symbol graph、UHT/docs/debug/JIT 统一事实源 | 管线收敛 + 共享 schema 新增 | 高 |

---

## 架构分析 (2026-04-09 00:46)

### Arch-DT48：`DebugDatabase` 仍是异构 JSON 树 + tuple payload，前端合同被历史 VS Code shape 锁死

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试数据库 schema 可演进性、符号实体建模、多 IDE 前端兼容 |
| 当前设计 | `SendDebugDatabase()` 不是输出显式的 typed symbol stream，而是反复拼装一个平面的 `FJsonObject Root`，把 type、namespace、enum、global property、property tuple、method object 全部塞进同一棵树；随后按 `UnsentCount > 10` 分块发送若干 `DebugDatabase` JSON 字符串。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1507-1517` 以 `FJsonObject Root` 为唯一容器，序列化后立即清空再继续拼下一批；`.../AngelscriptDebugServer.cpp:1748-1760` 把 property 编码成数组 `[type, flags?, doc?]`，并保留“旧版 VS Code 认为第二项必须是 string”的兼容注释；`.../AngelscriptDebugServer.cpp:1919-2041` 先把 global function/global property/enum 全并入 `NSFunctions`，再用 `Root->SetObjectField(TEXT("__") + NS.Key, NSDesc)` 把 namespace 与 enum 容器都投成同一种根节点对象；其中 enum value 继续用 property tuple 表示，真正的 enum 只靠 `isEnum=true` 区分。`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:196-255` 与 `.../AngelscriptDebugServer.h:536-554` 也说明断点、调用栈、数据库设置分别各带自己的最小字段集，但没有统一的 `SymbolId` / `Kind` / `SchemaVersionedEntry` 合同。 |
| 优点 | 对现有 VS Code 扩展足够直接，服务端只要把一棵 JSON 树推完，客户端按约定的 root key 与数组位置解析即可。 |
| 不足 | schema 语义被“字段位置”和“字符串前缀”隐式承载，而不是显式类型承载。type 与 namespace/enum 共用 root namespace，property/global/enum value 共用 tuple shape，chunk 也没有 domain/revision 元信息；一旦要接第二种 IDE 前端、离线索引器或 schema 演进，服务端只能继续叠兼容分支，而不是新增一种稳定 consumer。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 不自己设计一棵混合 JSON 树，而是直接复用 `V8InspectorSession` 的标准协议面；通道明确区分 `sendResponse()` 与 `sendNotification()`，HTTP discovery 再单独提供 `/json/list`、`/json/version`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:150-157`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:330-344`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477` | 先把“消息类型”和“发现入口”显式化，前端不需要靠 tuple 位置和命名约定猜语义。 |
| UnLua | IntelliSense 不走一棵全局根对象，而是按 `ModuleName/FileName.lua` 输出稳定工件；更新与删除都命中同一身份。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:248-290` | 即使不走标准协议，也应让符号实体有稳定 identity，而不是所有语义都压成同一棵混合树。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `DebugDatabase` JSON 给旧前端，但在服务端内部先建立显式的 typed symbol packet/schema，再把 legacy 树作为派生视图输出。 |
| 具体步骤 | 1. 在 `Toolchain` 或 `Debugging` 下新增 `FAngelscriptDebugSymbolEntry` / `FAngelscriptDebugSymbolChunk`，显式包含 `Revision`、`Kind(type/namespace/enum/property/function/global)`、`SymbolId`、`ContainerId`、`Name`、`Signature`、`Doc`、`Flags`、`SourceLocation`。 2. 将 `SendDebugDatabase()` 的收集阶段从“直接写 `FJsonObject`”改为“先收集 typed entry，再由 serializer 生成 legacy JSON root”；legacy serializer 继续输出今天的 `__namespace` 与 property tuple，保证旧扩展不变。 3. 新增轻量 discovery/协商消息，例如 `DebugDatabaseSchemaInfo`，至少声明 `schemaVersion`、`revision`、`supportsTypedChunks`、`supportsLegacyTree`、`availableDomains(types,namespaces,assets)`；旧客户端不认识该消息时继续只消费 legacy `DebugDatabase`。 4. 把 today 的 `UnsentCount > 10` 分块逻辑升级为显式 chunk：`TypeChunk`、`NamespaceChunk`、`AssetChunk` 或统一 `DebugSymbolChunk`，每块都带 `ChunkIndex/ChunkCount/Revision`，避免新前端只能靠 merge root object 推断完整性。 5. 将 `FAngelscriptDebugDatabaseSettings` 保留为兼容消息，但后续只承载真正的 workspace setting；schema/capability 改由新 discovery 消息描述，避免继续把“语义形状”和“运行时布尔设置”混在一起。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugSchema.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptDebugSymbolSerializer.*` |
| 预估工作量 | M |
| 架构风险 | 最大风险是新旧 schema 双轨期会同时存在 typed entry 和 legacy tree 两套序列化路径；如果没有 golden test，很容易出现“新 schema 正确、旧 VS Code 树偷偷变形”的回归。 |
| 兼容性 | 可增量实施。旧客户端继续接收原有 `DebugDatabase` / `DebugDatabaseFinished`，新客户端才订阅 typed chunk；迁移完成前，legacy root key 与 tuple 位置保持不变。 |
| 验证方式 | 1. 为现有 `DebugDatabase` 输出建立 golden snapshot，确保引入 typed serializer 后字节级或语义级保持兼容。 2. 新增 typed chunk regression test，验证 `type`、`namespace`、`enum`、`property` 四类 entry 都带正确 `Kind` 与 `ContainerId`。 3. 用一个最小非 VS Code client 只消费 typed chunk，验证它不需要理解 `__namespace` 和 property tuple 也能建索引。 4. 在一轮发送中同时抓取 discovery、typed chunk、legacy tree，确认三者 `Revision` 一致。 |

### Arch-DT49：`StaticJIT` 的运行时链接仍靠 `static ctor + global lookup patching`，compiled unit 没有显式 import owner

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | StaticJIT 运行时链接模型、import ownership、定向失效与可诊断性 |
| 当前设计 | 当前 JIT 生成物不是通过显式的 compiled-unit import table 接入运行时，而是依赖 `FStaticJITFunction` / `FJitRef_*` 一组静态对象在模块加载时把函数与待回填 slot 注册进全局 `FJITDatabase`；随后 `PrepareToFinalizePrecompiledModules()` 再一次性遍历这些全局数组，把 raw reference 就地改写成运行时地址。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h:18-42` 的 `FJITDatabase` 只有全局 `Functions`、`FunctionLookups`、`SystemFunctionPointerLookups`、`GlobalVarLookups`、`TypeInfoLookups`、`PropertyOffsetLookups`，没有 `UnitId -> Imports` 归属；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp:19-35` 的 `FStaticJITCompiledInfo` 明确要求 “Only one angelscript static JIT info can be compiled in!”；`.../StaticJITHeader.cpp:38-87` 中 `FStaticJITFunction` 与 `FJitRef_Function/Type/GlobalVar/PropertyOffset` 的构造函数都会直接把 slot 地址塞进 `FJITDatabase`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2376-2458` 的 `PrepareToFinalizePrecompiledModules()` 依次遍历这些全局数组并原地回填函数、system function pointer、type、global、property offset；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3683-3695` 最后也只写出一个全局 `AngelscriptJitInfo.jit.cpp`，没有每个 generated unit 的 import/ownership sidecar。 |
| 优点 | 启动路径简单，生成代码只要链接进来，静态构造就能把所有函数和引用挂到全局数据库，再由 precompiled data 统一补全。 |
| 不足 | 问题不在“能不能补全”，而在“补全后找不到 owner”。`FJITDatabase` 只知道有多少 slot 需要 patch，不知道这些 slot 属于哪个 `.jit.cpp`、哪个 `FunctionId`、哪个 module；一旦某个 property/type/global 回填失败，系统只能做全局或半全局处理，很难把问题精确归责到单个 compiled unit，也难以把 JIT 失效原因回流给 IDE/CI。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 每个前端连接都会显式创建 `V8InspectorChannelImpl`，并在断开时从 `V8InspectorChannels` map 删除；session owner 与生命周期是可见、可追踪、可精确释放的。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-555` | 关键运行时对象应当有显式 owner，而不是只存在于匿名全局数组中。 |
| UnLua | IntelliSense 工件以 `ModuleName/FileName.lua` 为 identity，`SaveFile()` 和 `DeleteFile()` 都精确命中该工件；增量刷新和删除都知道自己在修改哪一个 producer 的输出。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245` | 即使最终仍落到文件系统或全局目录，也要把 producer/consumer 边界显式化，便于定向失效与诊断。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持现有 `FJitRef_*` 兼容路径的前提下，为每个 JIT compiled unit 补一层显式 `import table + owner metadata`，让 patching 和失效都能按 unit/FunctionId 追踪。 |
| 具体步骤 | 1. 新增 `FStaticJITCompiledUnitInfo` 和 `JitUnitManifest`，字段至少包含 `UnitId`、`ModuleName`、`Functions`、`SourceFiles`、`Imports(Function/Type/Global/PropertyOffset)`、`PrecompiledDataGuid`。 2. 生成阶段在 `WriteOutputCode()` 写 module/unity file 时，同时输出 `UnitId -> included files -> function ids` 以及每个 `FJitRef_*` 的 owner；第一阶段可先写 JSON sidecar，不立刻改动现有 generated C++ 结构。 3. 将 `FJITDatabase` 扩展为既能保留旧的全局数组，也能注册 `RegisterUnit(UnitId, Functions, LookupSlots)`；`PrepareToFinalizePrecompiledModules()` 优先按 unit patch，并在日志中输出 `UnitId/FunctionId/ImportKind`，只有 manifest 缺失时才回退到今天的全局数组路径。 4. 让 load-time 失效从“无法解释的全局 patching”升级为“禁用某个 unit 或某个 function group”，并把原因回写到 JIT manifest 或 runtime diagnostics，方便后续 `DebugDatabase` / CI / review 工具消费。 5. 保留 `AngelscriptJitInfo.jit.cpp` 作为 legacy 全局兜底信息，但允许新路径同时输出多个 `CompiledUnitInfo`；这样旧构建产物继续能跑，新构建才逐步获得显式 ownership。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/JitUnitManifest.*` |
| 预估工作量 | L |
| 架构风险 | 如果一开始就强制新生成物完全放弃旧的 `FJitRef_*` 机制，回归面会很大；更稳妥的路径是先补 sidecar 与 per-unit registry，再逐步把旧全局数组降为 fallback。 |
| 兼容性 | 可向后兼容。旧 `AngelscriptJitInfo.jit.cpp`、旧 generated `.jit.cpp` 和全局 patching 流程继续保留；只有新生成的 unit manifest 才启用 per-unit patching 与定向诊断。 |
| 验证方式 | 1. 生成一轮 JIT 后校验 `JitUnitManifest` 中的 `UnitId -> FunctionId -> Imports` 与实际 `.jit.cpp` include 关系一致。 2. 人工制造一个 property/type import 不匹配场景，验证只有对应 unit 被禁用，而不是整个 `FJITDatabase` 一起清空。 3. 回归旧构建产物，确认缺少 unit manifest 时仍能走现有全局 patching。 4. 在日志或 manifest 中检查失效原因是否能定位到具体 `UnitId/ImportKind/FunctionId`。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT48 | `DebugDatabase` schema 实体建模、typed symbol contract、多 IDE 前端兼容 | 协议合同收敛 + typed serializer 新增 | 高 |
| P1 | Arch-DT49 | StaticJIT 运行时链接模型、import ownership、定向失效诊断 | 生成物 manifest 新增 + per-unit registry 收敛 | 高 |

---

## 架构分析 (2026-04-09 00:56)

### Arch-DT50：`AngelscriptDebugServer.h` 把 socket、wire schema 与调试语义一起暴露为 public surface，扩展前端前先放大编译边界

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试架构的 public API 边界、编译依赖泄漏、桥接层可替换性 |
| 当前设计 | `AngelscriptDebugServer.h` 同时承担三层职责：低层 transport 依赖、`V2` wire schema、以及 runtime 调试/导航能力入口。结果是任何想复用调试语义的代码，首先就要吞下 `Sockets/TcpListener/FArchive` 与整套消息体定义。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:3-13` 直接在 public header 引入 `Sockets.h`、`TcpListener.h`、`UdpSocketReceiver.h`、`MemoryWriter.h`、`MemoryReader.h`；`.../AngelscriptDebugServer.h:25-93` 在同一 header 里定义 `EDebugMessageType`、`FAngelscriptDebugMessageEnvelope` 与 envelope codec 声明；`.../AngelscriptDebugServer.h:520-579` 继续公开 `FindAssets`、`DebugDatabaseSettings`、`CreateBlueprint`、`ReplaceAssetDefinition` 等消息体；`.../AngelscriptDebugServer.h:581-693` 的 `FAngelscriptDebugServer` 又同时公开 `GetDebuggerValue()`、`GetDebuggerScope()`、`GoToDefinition()`、`HandleMessage()`、`SendMessageToClient()` 与 `SendCallStack()`，类成员直接持有 `FTcpListener`、`FSocket` 和 client 列表。 |
| 优点 | 单头文件集中度很高，当前 `V2` 前端、runtime 和测试都能直接围绕这一份定义工作，不需要额外的 facade/adapter 层。 |
| 不足 | 这种“全都公开”会把 transport 选择、wire 编码和调试语义绑成一个编译单元边界。后续无论是做 `DAP/CDP` bridge、命令行诊断工具、无网络本地调试面板，还是只想单元测试 `CallStack/Variables/Evaluate` 语义，都很难只依赖轻量 runtime API，而不把 socket 和 `V2` 编码一并带进来。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | public surface 只暴露 `V8Inspector` / `V8InspectorChannel` 抽象和 `CreateV8Inspector()` 工厂；具体 websocket/http 细节留在 `.cpp` 内部。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-73`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:315-328`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-555` | 先把“可被外部依赖的最小契约”收窄成接口与工厂，transport 和协议实现细节不要进入公共编译面。 |
| UnLua | public header 暴露的是 `FLuaDebugValue`、`FLuaVariable`、`GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 这类 runtime 调试原语；没有把 socket 或 IDE 协议类型做进同一头文件。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:26-94` | 即使暂时没有标准协议，也可以先让 public API 停留在“执行状态与调试数据”这一层，而不是连 transport 一起公开。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 public 调试面从 `socket + V2 protocol + runtime semantics` 拆成“runtime facade”与“协议/transport 适配层”，让现有 `V2` 只成为其中一个 consumer。 |
| 具体步骤 | 1. 新增 `Debugging/Public/AngelscriptDebugRuntimeTypes.h`，承载 `CallFrame`、`VariableSnapshot`、`StopReason`、`SourceLocation` 这类与 transport 无关的数据结构；不再包含 `Sockets.h`。 2. 新增 `Debugging/Public/IAngelscriptDebugRuntimeFacade.h`，只暴露 `TryGetCallStack`、`TryGetScope`、`TryEvaluate`、`TryResolveDefinition`、`ApplyBreakCommand` 等语义接口。 3. 将 `EDebugMessageType`、`FAngelscriptDebugMessageEnvelope`、`SerializeDebugMessageEnvelope()` 与 `SendMessageToClient()` 迁到 `Debugging/Protocol/V2/`；将 `FTcpListener`/`FSocket` 与 client pump 迁到 `Debugging/Transport/Tcp/`。 4. 保留现有 `FAngelscriptDebugServer` 作为组合根：内部持有 `RuntimeFacade + V2Protocol + TcpTransport`，对外继续提供旧构造路径；旧 header 先变成兼容转发层，阶段性保留原 include 路径。 5. 为 `RuntimeFacade` 增加脱离 TCP 的单元测试，让 `Variables/Evaluate/GoToDefinition` 能在不创建 socket 的情况下验证。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Public/AngelscriptDebugRuntimeTypes.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Public/IAngelscriptDebugRuntimeFacade.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/V2/*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Transport/Tcp/*` |
| 预估工作量 | M |
| 架构风险 | 如果第一步就直接删除旧 header 上的消息体与模板发送函数，现有调试测试和扩展会一起受影响；因此需要先做“新 facade + 旧 header 转发”的双轨期。 |
| 兼容性 | 可增量实施。现有 `V2` wire format、现有 VS Code 扩展和现有 include 路径都可以先保留；变化只是在内部把 public compile surface 收窄。 |
| 验证方式 | 1. 现有 `AngelscriptDebugProtocolTests` 保持通过。 2. 新增一个不创建 `FSocket` 的 facade 测试，直接验证 `CallStack/Variables/Evaluate`。 3. 让一个最小实验性 bridge 只依赖 `IAngelscriptDebugRuntimeFacade` 编译，确认不需要再包含 `Sockets.h` 和 `FTcpListener`。 4. 对比拆分前后的依赖图，确认新 public header 不再把 transport 头传递到所有 consumer。 |

### Arch-DT51：调试前端协议所有权仍内建在核心插件里，协议演进与 IDE 生态被迫跟随 Runtime 发版

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试前端所有权、IDE 生态复用、工具链交付边界 |
| 当前设计 | 当前插件不仅内建私有 `DebugServer V2`，还把 IDE/workspace 假设一并放进核心插件：协议版本、前端适配版本、导航与工作区入口都与 runtime/editor 模块一起演进。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:15` 固定 `DEBUG_SERVER_VERSION 2`；`.../AngelscriptDebugServer.h:25-80` 的 `EDebugMessageType` 既有 `StartDebugging/Step*`，也有 `GoToDefinition`、`DebugDatabaseSettings`、`CreateBlueprint`、`ReplaceAssetDefinition`；`.../AngelscriptDebugServer.h:103-115` 的 `FStartDebuggingMessage` 只有 `DebugAdapterVersion`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:897-913` 收到 `StartDebugging` 后直接写入全局 `DebugAdapterVersion`、切换 `bIsDebugging` 并清空断点；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:708-717` 菜单文本直接写成 “Open Angelscript workspace (VS Code)”；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:96-115` 导航末端也固定为 `FPlatformMisc::OsExecute(..., "code", ...)`。 |
| 优点 | 当前仓库可以提供一条完整、可控、可回归测试的官方工作流，单一前端路径的接入和问题定位都相对直接。 |
| 不足 | 前端协议、IDE 集成和 runtime 一起发版，意味着每新增一种前端形态都更像“修改核心插件”而不是“增加桥接器”。这会抬高接入 Chrome DevTools、DAP、CLI inspector、headless CI 诊断器等形态的门槛，也让 VS Code/workspace 约束长期滞留在核心模块边界里。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 直接复用 V8 Inspector / Chrome DevTools 协议生态：启动时准备 `/json/version`、`/json/list` 与 `webSocketDebuggerUrl`，连接建立后再为每个 websocket 创建 `V8InspectorChannelImpl`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:330-344`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525` | 让前端协议尽量复用成熟生态，核心运行时只负责提供 debug target，不自己长期背一整套 IDE 协议。 |
| UnLua | 文档层明确把前端调试器所有权交给外部生态：推荐安装 `LuaPanda` / `LuaHelper`，把 `LuaPanda.lua` 放进项目并在脚本里显式 `require("LuaPanda").start("127.0.0.1",8818)`；运行时本身只提供 `GetStackVariables()`、`GetLuaCallStack()` 这类本地调试原语。 | `Reference/UnLua/Docs/CN/Debugging.md:1-14`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-724` | 即使不走标准协议，也可以把“前端生态”视为外部 bridge，把核心插件聚焦在 runtime observability，而不是独占 IDE 协议所有权。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把“调试后端”与“前端 bridge”分成两个可独立发版的层次：核心插件只维护 runtime debug backend，`V2`/VS Code 成为默认 bridge，而不是唯一形态。 |
| 具体步骤 | 1. 新增 `IDebugFrontendBridge` 或等价注册点，桥接器负责 handshake、前端 capability 映射、导航/工作区 UI 集成；核心 runtime 只暴露 facade 与 capability model。 2. 将现有 `DebugServer V2` 实现重命名或包入 `AngelscriptDebugBridgeV2`，继续作为仓库内默认 bridge；其消息号和行为先保持兼容。 3. 将 `Open Angelscript workspace (VS Code)` 菜单、`code --goto` launcher 和其他 IDE 假设迁到 bridge/editor-integration 层；runtime/debug backend 不再直接知道具体 IDE 品牌。 4. 为后续 `CDP` 或 `DAP` bridge 预留最小能力映射：`pause/continue/stackTrace/scopes/variables/evaluate/source navigation` 六类核心能力先统一收口，`CreateBlueprint`、asset write-back 继续作为扩展 capability。 5. 在配置层增加 `PreferredDebugBridge` 与 `EnabledDebugBridges`，允许项目按需只启用 `V2`、只启用 `CDP bridge` 或并行启用多个 bridge；旧配置默认仍落在 `V2`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Frontend/IDebugFrontendBridge.h`，新增 `Plugins/Angelscript/Source/AngelscriptEditor/DebugBridgeV2/*` |
| 预估工作量 | M |
| 架构风险 | 双轨期最容易出现“新 bridge 能用、旧 `V2` 细节被无意改坏”的兼容回归；需要把旧 `V2` 先收成 golden behavior，再逐步外移。 |
| 兼容性 | 完全可增量实施。现有 VS Code 路径继续保留并作为默认 bridge；只有新项目或新工具显式启用其他 bridge 时，才进入新的协议适配层。 |
| 验证方式 | 1. 保持现有 `V2` 客户端和 VS Code 工作区菜单行为不变。 2. 加一个最小“只读 bridge”原型，只消费 `CallStack/Variables`，验证不需要改 runtime backend 即可接入。 3. 在关闭 VS Code bridge 的情况下，确认 runtime 仍能启动、仍能提供基础 debug capability，不因缺少 `code` 命令而失效。 4. 对 `PreferredDebugBridge` 做回归测试，确认切换 bridge 不会影响脚本执行语义。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT50 | 调试 public API 边界、transport/wire 泄漏、bridge 可替换性 | 头文件分层 + facade 抽取 | 高 |
| P1 | Arch-DT51 | 调试前端协议所有权、IDE 生态复用、bridge 独立发版 | bridge packaging 新增 + 前端外移 | 高 |

---

## 架构分析 (2026-04-09 01:04)

### Arch-DT52：调试验证层仍停留在 payload round-trip，新增 IDE/bridge 前缺少 session 级 conformance harness

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试协议的验证架构、多前端扩展前的回归护栏 |
| 当前设计 | 当前测试主要验证 `FArchive` payload 序列化和 envelope framing；真正影响 IDE 兼容性的 session 语义仍埋在 `FAngelscriptDebugServer` 的运行时状态机里，没有前端无关的会话级 golden contract。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp:25-35` 的 `RoundTripMessage()` 只做内存写回读；`.../AngelscriptDebugProtocolTests.cpp:39-77,79-245` 覆盖的也是 `StartDebugging`、`Variables`、`DataBreakpoints`、`DatabaseSettings` 等消息体 round-trip；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp:43-230` 只验证单包、多包、截断包与非法长度 framing；但真正的调试会话语义在 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:548-627` 的断点/单步暂停判定、`:667-699` 的 `PauseExecution()` 事件序列与阻塞循环、`:897-913` 的 `StartDebugging` 全局状态切换里。 |
| 优点 | 现有测试对 `V2` 二进制协议的基础兼容性有直接保护，改动 payload 字段或 envelope 逻辑时比较容易发现回归。 |
| 不足 | 当协议扩展到第二种 IDE bridge 时，当前测试无法回答“一个前端真正依赖的 session 序列是否仍成立”，例如 `StartDebugging -> HasStopped -> RequestCallStack -> Variables -> HasContinued` 是否稳定、断点命中与暂停循环是否仍按同样时序工作。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 核心只暴露 `V8Inspector` / `V8InspectorChannel` 抽象；连接建立后为每个 websocket 创建独立 channel，并通过 `/json/list`、`/json/version`、`webSocketDebuggerUrl` 接入 Chrome DevTools 现成前端。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-73`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-555` | 复用标准前端后，session 语义不再完全依赖私有协议测试；标准前端本身就是一层高价值 conformance consumer。 |
| UnLua | Runtime 只提供 `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 这类调试原语；前端调试集成则在文档中显式委托给 `LuaPanda` / `LuaHelper`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-731`；`Reference/UnLua/Docs/CN/Debugging.md:1-14` | 即使不内建完整协议，也先把“可测试的 runtime 观察原语”沉淀好，再把 IDE/session contract 交给外部 bridge 或工具。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留现有 round-trip/framing 测试的前提下，补一层前端无关的 `DebugSessionTranscript` conformance harness，把 `V2` 先收成 golden session contract，后续 DAP/CDP bridge 共用同一组语义测试。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增 `Debugging/AngelscriptDebugSessionTranscript.h/.cpp`，定义记录项，例如 `HasStopped/CallStack/Variables/Evaluate/HasContinued/Diagnostics`。 2. 为 `FAngelscriptDebugServer` 抽一个最小测试注入口，例如可替换的 `IDebugClientSink` 或 fake transport，让测试不必真的开 `FSocket`，就能驱动 `HandleMessage()`、`ProcessScriptLine()` 和 `PauseExecution()`。 3. 新增 transcript 级场景测试：`StartDebugging -> SetBreakpoint -> hit breakpoint -> RequestCallStack -> RequestVariables -> Continue`，以及 `Pause -> Evaluate -> Continue`、`Disconnect during paused state` 等关键时序。 4. 把现有 `AngelscriptDebugProtocolTests.cpp` 与 `AngelscriptDebugTransportTests.cpp` 保留为低层兼容护栏；未来新增 `DAP` 或 `CDP bridge` 时，要求先跑通同一组 transcript tests，而不是各自手写一套期望。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugSessionTranscript.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugging/AngelscriptDebugSessionConformanceTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 若测试注入口切分不当，容易把 runtime session 逻辑反向耦合到测试实现；需要坚持“新增 sink/fake transport，不改现有 `V2` wire format”的边界。 |
| 兼容性 | 向后兼容。现有 `V2` 协议、现有客户端与现有 round-trip 测试都保留；新增 harness 只是把既有行为冻结成更高层的回归合同。 |
| 验证方式 | 1. 现有 `Angelscript.CppTests.Debug.Protocol.*` 与 `Angelscript.CppTests.Debug.Transport.*` 继续通过。 2. 新增 transcript tests，验证断点命中、变量请求、继续执行的事件序列稳定。 3. 额外做一个 loopback 集成测试，确认真实 socket 下的事件顺序与 transcript harness 一致。 |

### Arch-DT53：UHT 已具备 dependency-aware 增量合同，但 Docs/StaticJIT/PrecompiledData 仍是进程副作用 producer，工具链缺少统一 build receipt

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 工具链 producer 的调度合同、产物新鲜度判断、统一编排入口 |
| 当前设计 | 当前工具链已经分化成两种 producer：`AngelscriptUHTTool` 是 build-graph 参与者，具备显式依赖和 stale cleanup；而 `Docs`、`StaticJIT`、`PrecompiledData` 仍通过进程启动参数在 `FAngelscriptEngine` 初始化路径里顺手生成，并以 `RequestExit()` 结束流程，没有统一 receipt 记录这几类产物是否属于同一次 toolchain run。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-77` 生成后会 `DeleteStaleOutputs()` 并写 summary/CSV；`.../AngelscriptFunctionTableCodeGenerator.cs:334-445` 通过 `AddExternalDependency(buildCsPath)`、解析 `AngelscriptRuntime.Build.cs`、清理旧 shard 构成显式增量合同；`.../AngelscriptFunctionTableCodeGenerator.cs:449-479` 还会把每个 header 纳入 external dependency。相比之下，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:514-529` 只是从命令行读取 `as-generate-precompiled-data` 与 `dump-as-doc`；`.../AngelscriptEngine.cpp:1433-1447` 在初始化期挂上 `PrecompiledData + StaticJIT`；`.../AngelscriptEngine.cpp:1575-1608` 写出 `AS_JITTED_CODE` 和 `PrecompiledScript.Cache` 后直接 `RequestExitWithStatus`；`.../AngelscriptEngine.cpp:2224-2228` 在 Editor 下 `DumpDocumentation()` 后也直接退出；具体 docs 落盘在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:675-755`，JIT 产物则在 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3499-3703` 写入 `FPaths::RootDir()/AS_JITTED_CODE`。 |
| 优点 | UHT 这条链已经有比较成熟的 build-time 合同，JIT/docs/precompiled 也都各自具备可独立触发的生产路径，便于单点调试。 |
| 不足 | 现在缺的不是“有没有产物”，而是“这些产物是否属于同一轮构建”的 authoritative receipt。CI、review 工具和未来 IDE 很难知道 `AS_FunctionTable_Summary.json`、`Docs/angelscript/generated/*.hpp`、`AS_JITTED_CODE/*`、`PrecompiledScript.Cache` 是否基于同一份脚本/同一轮 UHT/同一组配置生成。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | Editor 常驻 generator 与 commandlet 共享同一个 `Intermediate/IntelliSense` 输出根；Editor 侧通过资产事件增量刷新，commandlet 侧通过 `UpdateAll()` 做全量重建，但 `SaveFile/DeleteFile` 的落盘合同保持一致。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:58-90`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:21-27`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:101-133` | 关键不是“全量还是增量”，而是所有 producer 都共享一个可识别的 artifact root 和一致的更新语义，外部工具更容易判断新鲜度。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 不先推翻现有输出路径，而是先新增统一 `ToolchainReceipt`，把 UHT/docs/JIT/precompiled 四类 producer 的输入、输出、版本和完成状态收敛到一份 machine-readable receipt，再逐步把执行入口统一起来。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Intermediate/Toolchain/<Platform>/<Target>/AngelscriptToolchainReceipt.json` 或等价路径，schema 至少包含 `runId`、`scriptRoots`、`buildConfig`、`uht`、`docs`、`precompiled`、`staticJit` 四个 section。 2. 在 `AngelscriptFunctionTableCodeGenerator.cs` 写完 `AS_FunctionTable_Summary.json`/CSV 后，同时写入或更新 receipt 的 `uht` section，记录 summary 路径、module count、direct/stub 统计与 external dependency hash。 3. 在 `FAngelscriptDocs::DumpDocumentation()`、`FAngelscriptStaticJIT::WriteOutputCode()`、`PrecompiledData->Save()` 完成后分别补写 receipt，对应记录输出根、`DataGuid`、生成时间、输入脚本根与命令行开关。 4. 新增一个轻量 `AngelscriptToolchainCommandlet` 或 `Tools/RunToolchain.ps1`，第一阶段只负责顺序触发现有入口并校验 receipt 完整性，不急着迁出 `FAngelscriptEngine` 里的生成逻辑。 5. 后续再让 IDE/debug database/review 工具消费 receipt，以 machine-readable 方式判断“该跳转索引、文档快照和 JIT 产物是否来自同一轮 build”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptToolchainReceipt.*`，新增 `Tools/RunToolchain.ps1` 或 `Plugins/Angelscript/Source/AngelscriptEditor/Private/Commandlets/AngelscriptToolchainCommandlet.cpp` |
| 预估工作量 | M |
| 架构风险 | 多 producer 在不同进程/不同触发路径更新同一份 receipt 时，需要先定义原子写入和 section 合并策略；否则容易把“统一 receipt”变成新的竞态点。 |
| 兼容性 | 向后兼容。现有 `dump-as-doc`、`as-generate-precompiled-data`、UHT exporter 和现有输出目录都可先保持不变；receipt 只是附加元数据，不改变现有消费方。 |
| 验证方式 | 1. 运行一次标准 UHT build，确认 receipt 的 `uht` section 与 `AS_FunctionTable_Summary.json` 一致。 2. 分别执行 docs dump、precompiled/JIT 生成，确认 receipt 能累计四类 section，而不是相互覆盖。 3. 人工制造 stale docs 或 stale JIT 场景，确认外部脚本仅靠 receipt 就能判定“不属于同一轮 toolchain run”。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT52 | 调试协议验证层级、session conformance、未来多 bridge 回归护栏 | 测试基建新增 + transcript contract 固化 | 高 |
| P1 | Arch-DT53 | 工具链 producer 调度合同、artifact freshness、统一 build receipt | receipt 新增 + 编排入口收敛 | 高 |

---

## 架构分析 (2026-04-09 23:59)

### Arch-DT54：`UHT` 覆盖报表把“作用域外模块”与“作用域内失败”混为同一类 skipped，导致工具链优先级失真

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `UHT` 生成范围合同、coverage 诊断口径、工具链报表可操作性 |
| 当前设计 | `AngelscriptFunctionTableCodeGenerator` 只为“支持模块”生成 `AS_FunctionTable_*`，但 `AngelscriptFunctionTableExporter` 的 skipped 统计却遍历整个 `UHT session`；结果是报表里的 `skipped` 同时混入了“本来就不在生成作用域内”的模块和“在作用域内但签名重建失败”的函数。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-78` 先 `LoadSupportedModules(factory)`，只对 `supportedModules.All` 里的模块调用 `GenerateModule(...)`；`.../AngelscriptFunctionTableCodeGenerator.cs:334-385` 说明支持范围来自 `AngelscriptRuntime.Build.cs` 推断。与之相对，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:27-53` 在 `Export()` 里遍历 `factory.Session.Modules` 全量模块；`.../AngelscriptFunctionTableExporter.cs:65-97` 的 `CountBlueprintCallableFunctions(...)` 只要 `TryBuild(...)` 失败就记入 `skippedEntries`；`.../AngelscriptFunctionTableExporter.cs:99-161` 写出的 `AS_FunctionTable_SkippedEntries.csv` / `AS_FunctionTable_SkippedReasonSummary.csv` 也没有 `scope status` 字段。 |
| 优点 | 当前做法能一次性看到整个 `UHT session` 里的 `BlueprintCallable/Pure` 面积，对“插件尚未覆盖哪些 UE 模块”有全景观察价值。 |
| 不足 | 报表语义不再等于“绑定失败清单”，而是“全量观察结果”。这会直接影响后续优先级判断：CI、BindGap audit、人工清理 backlog 都无法仅凭 `skipped` 判断“这是应该修的能力缺口”，还是“当前版本并未声明支持的模块”。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | commandlet 的导出范围是显式的：先取 `GetExportedReflectedClasses()`、`GetExportedNonReflectedClasses()`、`GetExportedEnums()`、`GetExportedFunctions()`，必要时才通过 `BP=1` 额外触发 `Generator->UpdateAll()`；输出始终写入同一 `IntelliSense` contract。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-111`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:117-133` | 先把 producer scope 定义清楚，再统计覆盖率；“导出范围”和“导出失败”不混在一个计数里。 |
| puerts | toolchain producer 身份在 `.uplugin` 中显式声明为 `DeclarationGenerator`、`ParamDefaultValueMetas` 等独立模块，而不是从运行时模块的文本依赖间接猜测。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48` | 先把“谁属于 toolchain scope”变成显式元数据，后续报表才能有稳定边界。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `UHT` 报表拆成“作用域声明”和“作用域内失败”两层，保留全景观察能力，但不再让 skipped 指标承担两种含义。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator` 输出 sidecar 时，追加 `moduleScopeStatus`，至少区分 `supported`、`supportedEditorOnly`、`excludedByManifest`、`observedOnly`。 2. 修改 `AngelscriptFunctionTableExporter.Export()`：在遍历 `factory.Session.Modules` 前先读取同一份 scope manifest，对 `CountBlueprintCallableFunctions(...)` 增加 `scopeStatus` 入参。 3. 将 today 的 `AS_FunctionTable_SkippedEntries.csv` 扩展为含 `ScopeStatus` 列；同时新增 `AS_FunctionTable_OutOfScopeEntries.csv` 或在 JSON summary 中单独落 `outOfScopeObservedCount`，让 CI/文档默认只消费 `supported` 作用域内的 skipped。 4. `WriteSkippedReasonSummaryCsv(...)` 继续保留旧文件名，但改为默认只统计 `supported` 作用域；如需全景统计，再额外输出 `ObservedReasonSummary`。 5. 在控制台摘要里显式打印 `supported module count / observed-only module count / in-scope skipped count`，避免人工只盯着一个 `skipped` 总数做决策。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，新增 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableScopeManifest.cs` 或等价 DTO |
| 预估工作量 | S |
| 架构风险 | 若第一阶段直接改变现有 CSV 含义而不保留兼容列，现有脚本或审查流程可能把“作用域收缩”误判为“覆盖率突然改善”。 |
| 兼容性 | 可增量实施。保留现有 `AS_FunctionTable_SkippedEntries.csv` 与 `SkippedReasonSummary.csv` 文件名，只新增 `ScopeStatus` 列和并列 summary；旧消费方仍能继续读取原有列。 |
| 验证方式 | 1. 构造一个明确不在支持清单里的模块，确认它只出现在 `observed/excluded` 统计，不进入 `in-scope skipped`。 2. 构造一个支持模块中的真实签名失败，确认它仍进入 `skipped` 主报表。 3. 对比升级前后的 `generatedFileCount` 与 shard 输出，确认改动只影响报表合同，不影响 `AS_FunctionTable_*` 生成结果。 |

### Arch-DT55：`DumpDocumentation()` 仍靠手写字符串拼接伪 `C++` 头文件，缺少结构化 emitter 与语法校验层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 文档工件的语法合同、生成器可维护性、IDE/索引消费稳定性 |
| 当前设计 | `DumpDocumentation()` 直接在一个循环里用 `FString::Printf` 拼接 block comment、`class`、`enum`、属性声明和函数声明，然后立即 `SaveStringToFile()`；它生成的是“看起来像 `C++` header 的文本”，但中间没有 AST、没有 comment escaping、也没有导出后验证。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:682-755` 先构造 `Filename`，随后直接 `Content += FString::Printf(TEXT("/* Class: %s \\n %s */ \\n class %s"), ...)`、`Content += FString::Printf(TEXT("\\n/* Variable: %s \\n %s */\\n"), ...)`、`Content += FString::Printf(TEXT("\\n/* Function: %s \\n %s */\\n"), ...)`、`Content += FString::Printf(TEXT("/* Enum: %s \\n %s */ \\n enum %s { %s \\n}"), ...)`，最后 `FFileHelper::SaveStringToFile(Content, *Filename)`；整个导出路径里没有任何 escaping、parser check 或 syntax validation。 |
| 优点 | 实现简单，人工直接打开 `.hpp` 就能浏览；对当前以“离线阅读”为主的文档使用方式，落地成本很低。 |
| 不足 | 这套 contract 只有“可阅读”保证，没有“可被机器稳定解析”保证。只要 tooltip、category 或 declaration 中出现注释终止符、换行组合、保留字冲突或后续 schema 扩展，导出的 `.hpp` 就可能从“可索引工件”退化成“格式化字符串快照”，而问题直到 IDE/脚本或 review 工具消费时才暴露。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense 文本不是在 commandlet 里临时手拼，而是通过统一的 `UnLua::IntelliSense::Get(...)` / `GetUE(...)` emitter 生成；注释文本还会先走 `EscapeComments(...)`，然后才由 commandlet 或 editor generator 统一 `SaveFile()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:29-35`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:42-72`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:282-298`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:438-458`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:165-172`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233` | 先有结构化 emitter 和 escaping 规则，再有文件落盘；Editor 与批处理只负责驱动，不负责手写语法。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先把 docs 导出从“直接拼字符串”升级为“结构化 doc stub emitter + 轻量校验”，同时保留现有 `.hpp` 作为兼容镜像。 |
| 具体步骤 | 1. 新增 `Core/Docs/AngelscriptDocStubEmitter.*`，定义最小 `DocNode/ClassNode/FunctionNode/PropertyNode/EnumNode` 与 `EscapeDocText()/EscapeIdentifier()` 规则；`DumpDocumentation()` 先构建节点，再由 emitter 产出 legacy `.hpp`。 2. 将 today 的 `FString::Printf("/* ... */")` 直写逻辑迁到 emitter 内部，统一处理注释转义、空文档、换行规范和声明拼接，不再在 `DumpDocumentation()` 主循环里散落格式细节。 3. 在写 `.hpp` 的同时新增轻量 `DocsIndex.json` 或 `DocStubManifest.json`，记录 `DocSymbolId -> path -> hash`；让机器消费方优先读 manifest，而不是直接依赖文本解析。 4. 为 docs dump 增加 `validate-doc-stubs` 阶段：第一阶段可以只是检查未转义 `*/`、不平衡花括号和空文件；后续再视需要引入更严格的 parser/linter。 5. 保留现有输出路径和文件扩展名，但当 validator 失败时，把错误回写到日志与 manifest，而不是继续静默产出不可信工件。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/Docs/AngelscriptDocStubEmitter.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/Docs/AngelscriptDocStubValidator.*` |
| 预估工作量 | M |
| 架构风险 | 若一开始就尝试把 legacy `.hpp` 彻底替换成新格式，现有依赖该路径的阅读脚本和人工流程会受影响；更稳妥的是先“同路径双重保障”，只提高生成质量和校验能力。 |
| 兼容性 | 向后兼容。现有 `Docs/angelscript/generated/*.hpp` 继续存在；新增的是 emitter、manifest 和 validator。旧消费方仍可继续读 `.hpp`，新消费方可逐步转向 manifest。 |
| 验证方式 | 1. 构造包含多行 tooltip、`@`、`*/`、引号和特殊标识符的文档样例，确认生成文件不会破坏语法边界。 2. 在无源码变化时重复导出，确认 hash 不变且不重写文件。 3. 故意注入一个不安全 comment 内容，确认 validator 会给出结构化错误而不是静默写盘。 4. 回归现有 `.hpp` 阅读流程，确认常规符号的可读性不下降。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT54 | `UHT` coverage 报表口径、scope vs skipped 语义分离 | 报表合同收敛 + scope manifest 新增 | 高 |
| P2 | Arch-DT55 | docs stub 生成合同、comment escaping、语法校验 | emitter 新增 + 导出验证补强 | 中 |

---

## 架构分析 (2026-04-10 00:13)

### Arch-DT56：`DebugServer` 的扩展机制仍是“闭合枚举 + 巨型分派函数 + 特例委托”，新工具能力无法作为独立 domain 增量接入

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试协议扩展点、工具命令接入方式、多前端能力演进 |
| 当前设计 | 当前 `DebugServer` 虽然已经暴露了若干 Editor/工具能力，但扩展路径不是“注册一个新 domain/service”，而是继续往 `EDebugMessageType`、消息体定义和 `HandleMessage()` 主分派里追加分支；只有 `BreakFilters`、`FindAssets`、`CreateBlueprint` 这些少数功能通过特例委托外接。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:25-80` 把调试、资产、蓝图创建、数据断点统一塞进单个 `EDebugMessageType`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:820-1195` 用一条 `if/else` 长分派直接处理所有消息；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h:11-21,33-47` 只为 `GetDebugBreakFilters()`、`GetDebugListAssets()`、`GetEditorCreateBlueprint()` 暴露特定委托，而没有通用的调试/工具服务注册接口；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1147-1180` 也显示这些“可扩展”能力仍需在核心 `HandleMessage()` 中写死分支后再转发到委托。 |
| 优点 | 当前实现对仓库内默认前端最直接，新增一条仓库内自用命令时改动集中，调试和 Editor workflow 可以快速连通。 |
| 不足 | 一旦要新增第二种 IDE 前端、外部工具命令、只读诊断域或项目私有工具域，就必须改动 core header、wire enum、主分派、测试和前端兼容逻辑；扩展面是“修改核心协议”，不是“注册新能力”。这会让 `DebugServer` 长期承担所有工具命令的编排责任，难以演进成多前端、多桥接器并存的体系。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `V8Inspector` / `V8InspectorChannel` 先把扩展面收敛成最小接口；连接建立后，websocket 文本消息直接转发给 `v8_inspector::V8InspectorSession`，新增前端能力主要依赖标准 Inspector/CDP domain，而不是继续扩写插件自定义枚举。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-73`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:104-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-525` | 把扩展点设计成“可注册的 session/domain 消费面”，而不是“继续往核心协议枚举里加项”。 |
| UnLua | 核心插件不把 IDE 集成做成内建枚举协议，而是文档中明确要求外部 `LuaPanda` / `LuaHelper` 接管前端桥接；runtime 只稳定暴露 `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 这些调试原语。 | `Reference/UnLua/Docs/CN/Debugging.md:5-14`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94` | 即使不走标准协议，也应把“runtime 调试原语”和“前端/工具扩展”分层，让新增工具能力优先通过桥接层接入。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 `V2` wire format 的前提下，引入 `DebugDomain`/`ToolDomain` 注册面，把 today 的 closed enum 扩展路径改成“内建 domain + 可选 domain”并存。 |
| 具体步骤 | 1. 新增 `Debugging/Domains/IAngelscriptDebugDomain.h`，定义最小接口，例如 `GetDomainName()`、`AdvertiseCapabilities()`、`HandleLegacyMessage()`、`HandleDomainRequest()`；核心只内建 `core.debug`。 2. 把当前 `BreakFilters`、`FindAssets`、`CreateBlueprint`、`ReplaceAssetDefinition` 从“写死在 `HandleMessage()`”改成内建 domain service，例如 `workspace.assets`、`editor.blueprint`；`DebugServer` 只做查找与转发。 3. 在 `FAngelscriptRuntimeModule` 上新增注册入口或使用 `IModularFeature`，允许项目侧/插件侧注册额外 domain，而不是继续新增特定委托。 4. 第一阶段继续保留 `EDebugMessageType` 和现有消息体，把它们映射到内建 domain；旧客户端完全不变，新客户端再增量使用 `DomainName + Command` 请求。 5. 为连接建立增加 `AvailableDomains`/`Capabilities` 声明，让只读前端、CI 诊断器或项目私有工具只订阅自己需要的 domain。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Domains/IAngelscriptDebugDomain.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Domains/Builtin/*` |
| 预估工作量 | M |
| 架构风险 | 若第一阶段就把现有消息彻底迁成新 domain 请求，现有前端会立刻失配；必须先做“legacy message -> domain service”的兼容层，而不是直接改线协议。 |
| 兼容性 | 可增量实施。旧 `V2` 客户端继续按 `EDebugMessageType` 工作；新 domain 机制作为附加能力出现，不破坏现有脚本、现有 VS Code 扩展和现有测试。 |
| 验证方式 | 1. 保留现有 `AngelscriptDebugProtocolTests.cpp` 通过。 2. 新增一个 fake domain，验证不改 `HandleMessage()` 主体即可接入并收到 capability 广播。 3. 用旧客户端回归 `BreakFilters`、`FindAssets`、`CreateBlueprint`，确认 legacy 消息仍可正常工作。 4. 用新客户端只请求 `core.debug`，确认不会意外获得 workspace/editor 域能力。 |

### Arch-DT57：toolchain producer 的“可发现性”仍然高度不对称，只有 `UHT` 是显式扩展点，`Docs/JIT/PrecompiledData` 还不是可注册的 producer

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | toolchain producer 注册面、扩展性、第三方/项目级工具接入 |
| 当前设计 | 当前插件里，`AngelscriptUHTTool` 已经是一个独立的 `UHT` 扩展产线，但 `DumpDocumentation()`、`StaticJIT->WriteOutputCode()`、`PrecompiledData->Save()` 仍只是 `FAngelscriptEngine` 启动流程里的命令行分支；插件层没有统一的 `toolchain producer` 注册接口来声明“谁能生产什么 artifact”。 |
| 源码证据 | `Plugins/Angelscript/Angelscript.uplugin:18-33` 只声明 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块，没有可见的 toolchain producer 模块；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj:1-15` 显示 `AngelscriptUHTTool` 作为独立 `DotNET/UnrealBuildTool` 插件输出，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.cs:1-5` 则说明这个 lane 极度轻量且独立；相比之下，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:514-529` 只是用 `dump-as-doc`、`as-generate-precompiled-data` flag 决定是否进入 docs/JIT/precompiled 路径，`.../AngelscriptEngine.cpp:1573-1588` 与 `:2224-2227` 分别在初始化末尾直接执行 `WriteOutputCode()`、`Save()`、`DumpDocumentation()` 后退出；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:726-730` 也明确写着 legacy editor generator 只是调试旧输出，主路径已经是 `AngelscriptUHTTool`，说明当前 toolchain 已经天然分裂成“一个显式 producer + 多个隐式 producer”。 |
| 优点 | `UHT` 这条线已经具备独立产线和 build-time 集成能力，而 docs/JIT/precompiled 继续复用 live engine 语义，短期内实现成本较低。 |
| 不足 | 现在要新增一个 producer，例如 `symbol index`、`source navigation snapshot`、`JIT coverage report` 或项目自定义 sidecar，缺少“注册一个 producer”这条正式路径，通常只能继续往 `FAngelscriptEngine` 的 flag/初始化尾部追加逻辑。长期看，这会把工具链扩展能力锁死在核心 runtime，而不是开放给仓库内其他模块或项目侧插件。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `.uplugin` 直接把 `DeclarationGenerator`、`ParamDefaultValueMetas` 声明为独立 module；`FParamDefaultValueMetasModule` 还显式 `RegisterModularFeature("ScriptGenerator", this)`，并声明 `GetGeneratedCodeModuleName()`、`ShouldExportClassesForModule()`、`Initialize()`、`FinishExport()` 这些 producer 合同。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:29-54`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:115-129` | producer 身份和接入协议都应是显式元数据/接口，而不是只靠 runtime flag 触发。 |
| UnLua | `FUnLuaIntelliSenseGenerator` 作为显式 producer 订阅 `AssetRegistry`，`UUnLuaIntelliSenseCommandlet::Main()` 再调用 `Initialize()` / `UpdateAll()` 复用同一个 producer；写盘也统一经过 `SaveFile()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:58-90`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:29-114`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:117-133` | 一个 producer 可以有多个 driver，但 producer 本身必须先成为可被发现、可被复用的正式对象。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `UHTTool`、docs、JIT、precompiled 路径之上增加统一的 `IAngelscriptToolchainProducer` 注册面，把“谁生产 artifact”从 runtime 分支逻辑提升成显式 contract。 |
| 具体步骤 | 1. 新增 `Toolchain/IAngelscriptToolchainProducer.h`，或用 `IModularFeature` 暴露同等接口；至少声明 `ProducerName`、`TriggerModes`、`Inputs`、`Outputs`、`Run()`。 2. 第一阶段把现有 `AngelscriptUHTTool` summary 收敛为一个 `UhtProducer` 适配层，同时为 `DumpDocumentation()`、`StaticJIT->WriteOutputCode()`、`PrecompiledData->Save()` 包装出 `DocsProducer`、`StaticJitProducer`、`PrecompiledProducer`，内部仍调用当前实现。 3. 在新的 toolchain host 或 commandlet 中按注册表枚举 producer，支持 `Run --producer DocsProducer`、`Run --producer StaticJitProducer` 这类精确调用；现有 `-dump-as-doc`、`-as-generate-precompiled-data` 先改成调用这个 host。 4. 让 producer 在 receipt/manifest 中写入自己的 `ProducerName`、版本、输入输出路径和完成状态，为未来项目私有 producer 预留 join 键。 5. 第二阶段允许项目侧插件注册额外 producer，例如 `NavigationIndexProducer` 或 `ProjectSpecificDocOverlayProducer`，避免继续改动核心 runtime。 |
| 涉及文件 | `Plugins/Angelscript/Angelscript.uplugin`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/IAngelscriptToolchainProducer.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/Producers/*`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 预估工作量 | M |
| 架构风险 | 如果 producer 接口一开始定义得过宽，容易把当前隐式依赖的 engine 状态直接泄漏到接口层；第一阶段应只收敛最小输入输出合同，不急着一次性统一所有执行上下文。 |
| 兼容性 | 向后兼容。现有 `UHT` exporter、现有命令行 flag、现有 editor 菜单入口都可以继续存在；producer registry 先作为附加编排层，不要求立即迁移所有外部调用方。 |
| 验证方式 | 1. 让 `DocsProducer`、`StaticJitProducer`、`PrecompiledProducer` 通过新注册面各跑一次，确认产物与旧入口一致。 2. 回归 `AngelscriptUHTTool` 输出，确认 summary/CSV 未回归。 3. 新增一个最小 fake producer，验证无需修改 `FAngelscriptEngine` 即可被 host 枚举和执行。 4. 检查 receipt/manifest 中能稳定看到每个 producer 的名字、输出路径和完成状态。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT56 | `DebugServer` 扩展机制、domain 化接入、工具命令演进路径 | 扩展点重构 + capability/domain 注册面新增 | 高 |
| P2 | Arch-DT57 | toolchain producer 注册面、可发现性、项目侧扩展能力 | producer registry 新增 + toolchain contract 收敛 | 中 |

---

## 架构分析 (2026-04-10 00:21)

### Arch-DT58：`FAngelscriptEngine` 直接操作 `FAngelscriptDebugServer` 具体状态，bridge 无法成为一等调试后端

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | runtime 调试回调抽象、后端可替换性、多 IDE bridge 接入面 |
| 当前设计 | `Engine` 并不是依赖“调试运行时接口”，而是直接创建并驱动 `FAngelscriptDebugServer` 这个具体类；line callback、exception、手动 break、diagnostics 与 workspace 写回都直接读写它的字段或调用它的具体方法。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1455` 直接 `new FAngelscriptDebugServer(this, RuntimeConfig.DebugServerPort)`；`.../AngelscriptEngine.cpp:2028-2035` 的 `ReplaceScriptAssetContent()` 直接构造 `FAngelscriptReplaceAssetDefinition` 并调用 `SendMessageToAll(EDebugMessageType::ReplaceAssetDefinition, ...)`；`.../AngelscriptEngine.cpp:5429-5444` 的 `UpdateLineCallbackState()` 直接读取 `DebugServer->bIsDebugging`、`DebugServer->DataBreakpoints.Num()`、`DebugServer->bBreakNextScriptLine`；`.../AngelscriptEngine.cpp:5308-5309`、`:5536`、`:5562` 分别直接调用 `ProcessException()`、`ProcessScriptLine()`、`ProcessScriptStackPop()`；`.../AngelscriptEngine.cpp:5718-5744` 的 `TryBreakpointAngelscriptDebugging()` 直接检查 `bIsDebugging/bIsPaused` 后调用 `PauseExecution()`。 |
| 优点 | 当前路径非常直接，调试停点、脚本行回调和外部 socket 会话之间几乎没有额外抽象成本。 |
| 不足 | 这意味着未来即便只是新增一个 `DAP bridge`、`CDP bridge`、只读诊断器或本地无 socket 的测试后端，也必须模拟 `FAngelscriptDebugServer` 这整个 concrete shape，而不是实现一组稳定的 runtime hook。协议适配问题因此被放大成 runtime 内核重耦合问题。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | runtime 侧先暴露最小 `V8Inspector` / `V8InspectorChannel` 抽象；websocket/HTTP、`/json/list` 与 CDP 消息转发都藏在 `V8InspectorClientImpl` 里，宿主只依赖抽象 inspector 生命周期。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-73`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:372-372`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-529`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:572-592` | 先把 runtime 依赖面压到抽象接口，transport/协议桥再作为后端实现接入。 |
| UnLua | runtime 只稳定提供 `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 这类调试原语；它不要求 VM 去认识某个具体 IDE server 实现。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-724` | 先把“调试观察/控制原语”沉淀为 runtime API，再决定前端桥接和传输层如何实现。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在 `Engine` 与外部调试前端之间新增 `runtime debug backplane` 抽象，让 `FAngelscriptDebugServer` 退化为默认后端，而不是唯一内核对象。 |
| 具体步骤 | 1. 新增 `Debugging/IAngelscriptDebugRuntimeBackend.h`，最小接口先只覆盖 `ShouldEnableLineCallbacks()`、`OnScriptLine()`、`OnStackPop()`、`OnException()`、`RequestPause()`、`PublishDiagnostics()`、`PublishWorkspaceEvent()`。 2. 在 `FAngelscriptEngine` 中把 today 对 `DebugServer` 的直接字段读取收敛为接口调用，例如把 `UpdateLineCallbackState()` 改成问 backend capability，而不是直接检查 `bIsDebugging/DataBreakpoints/bBreakNextScriptLine`。 3. 让 `FAngelscriptDebugServer` 实现该接口，并把 `ReplaceScriptAssetContent()` 里的 `EDebugMessageType::ReplaceAssetDefinition` 发送逻辑下沉到 server backend；`Engine` 只发布语义化 workspace 事件。 4. 第一阶段保留 `DebugServer` 作为默认实现，不改现有 socket 协议；同时允许测试或未来 bridge 注入一个 fake/local backend，验证 runtime 不再绑定 concrete class。 5. 第二阶段再让 `DAP/CDP bridge`、只读 CI 诊断器或本地 transcript harness 直接实现同一接口，而不是继续围绕 `FAngelscriptDebugServer` 打补丁。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/IAngelscriptDebugRuntimeBackend.*` |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段把接口定义得过宽，容易只是把 `FAngelscriptDebugServer` 的全部细节原样搬到新接口里；应先坚持最小 runtime 语义面，而不是复制 wire/protocol 细节。 |
| 兼容性 | 可增量实施。现有 `V2` socket、现有 VS Code 扩展和现有 `DebugServer` 行为都可以保持不变；新增接口只是把 runtime 对 concrete server 的依赖改为对抽象语义面的依赖。 |
| 验证方式 | 1. 回归现有 `Angelscript.CppTests.Debug.Protocol.*` 与运行时调试用例，确认默认 `DebugServer` backend 行为不变。 2. 新增一个 fake backend，验证无需创建 socket server 也能驱动 `UpdateLineCallbackState()`、`TryBreakpointAngelscriptDebugging()` 和 exception 路径。 3. 让旧客户端继续连接真实 `DebugServer`，确认断点、变量、诊断和 workspace 写回仍可工作。 |

### Arch-DT59：`StaticJIT` 用 `asIJITCompiler` 外壳承载 offline transpile producer，生成态与运行时 resolver 合同并不诚实

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `StaticJIT` 的接口边界、生成态 producer 与运行时 resolver 分层、后续扩展真实 JIT/validate lane 的可行性 |
| 当前设计 | `FAngelscriptStaticJIT` 继承 `asIJITCompiler`，但它并不实现“运行时即时编译器”语义；生成态的 `CompileFunction()` 只是把函数收集进 `FunctionsToGenerate`，真正的产物生成在 `WriteOutputCode()`；运行时则完全绕过 `asIJITCompiler`，改由 `FJITDatabase + PrecompiledData` 决定某个函数是否挂上已编译入口。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h:393-489` 的 `FAngelscriptStaticJIT` 同时持有 `PrecompiledData`、`FunctionsToGenerate`、`ComputedOffsets`、`SharedHeaders` 等生成态状态；`.../AngelscriptStaticJIT.cpp:66-79` 的 `CompileFunction()` 只有 `bGenerateOutputCode` 分支会 `FunctionsToGenerate.Add(...)`、`*OutJITFunction = nullptr`，否则直接 `check(false)`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1438-1455` 只在预编译生成 lane 把 `StaticJIT` 挂到 `Engine->SetJITCompiler(StaticJIT)`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3453-3470` 与 `:3470-3507` 又把 `AnalyzeScriptFunction()`、`GenerateCppCode()`、文件写出全部放进 `WriteOutputCode()`；与之分离的运行时消费路径在 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp:38-46` 通过 `FStaticJITFunction` 把编译后的入口注册进 `FJITDatabase`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:485-540` 则在 `FAngelscriptPrecompiledFunction::Create()` 里优先查 `JITDatabase.Functions.Find(Id)`，找不到就 `AllocateScriptFunctionData()` 回退解释器。 |
| 优点 | 当前实现能在不改 AngelScript core load path 的前提下完成“离线收集函数 -> 生成 C++ -> 运行时按 `FunctionId` 挂接”这条链，短期落地成本低。 |
| 不足 | 问题不在功能是否可用，而在合同是否诚实：`asIJITCompiler` 在这里其实只是“生成态收集钩子”，不是通用 JIT backend。于是后续若想加 `compile-on-miss`、只读 validate lane、第二种 codegen backend、或独立 JIT coverage 分析，就必须继续沿着一个名义上是 runtime JIT、实际上是 offline producer 的类扩写。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 代码生成器不是伪装成 runtime 接口，而是明确作为 `ScriptGenerator` modular feature 接入，显式声明 `GetGeneratedCodeModuleName()`、`ShouldExportClassesForModule()`、`Initialize()`、`FinishExport()` 这类 producer 生命周期。 | `Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:31-53`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:115-129` | offline producer 应该有自己的显式合同，而不是借用一个语义并不匹配的 runtime 接口。 |
| UnLua | `IntelliSense` 生成器通过 `Initialize()`、`UpdateAll()`、`SaveFile()`、`DeleteFile()` 形成独立 producer，对外由 commandlet 或 editor driver 复用；运行时调试 API 仍单独放在 `UnLuaDebugBase`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-58`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-236`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:110-117` | 生成态 producer 和运行时 consumer/调试 API 分层清楚后，commandlet、editor、本地验证都能复用同一 producer，而不会把 runtime 接口语义拉歪。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 today 的 `StaticJIT` 拆成“最小 `asIJITCompiler` 收集适配器 + 显式 static transpile producer + 运行时 compiled-function resolver”三层，先让接口语义诚实，再谈新 backend/validate lane。 |
| 具体步骤 | 1. 新增 `StaticJIT/FAngelscriptJitCollectAdapter.*`，它是唯一继承 `asIJITCompiler` 的薄适配层，职责只保留“记录被 AngelScript 请求编译的 `FunctionId/ScriptFunction`”，不再持有 `ComputedOffsets/SharedHeaders/ExternalReferenceNames` 等生成状态。 2. 把 today `FAngelscriptStaticJIT` 里的生成态成员与 `WriteOutputCode()` 拆到 `StaticJIT/FAngelscriptStaticTranspileProducer.*`；`Engine` 在 `-as-generate-precompiled-data` 路径下创建 `CollectAdapter + Producer` 组合，而不是把所有语义都塞进 `asIJITCompiler` 实现。 3. 为运行时消费新增 `StaticJIT/ICompiledFunctionResolver.h` 或等价 facade，把 `FJITDatabase` 查询、`PrecompiledData` 挂接、fallback 到 `AllocateScriptFunctionData()` 的逻辑收进 resolver；`FAngelscriptPrecompiledFunction::Create()` 只依赖 resolver，不直接知道 `FJITDatabase` 内部结构。 4. 第一阶段保持现有 `FStaticJITFunction` 注册、`AngelscriptJitInfo.jit.cpp`、`PrecompiledScript.Cache` 和 `FJITDatabase` 数据结构不变，只做 owner 重组；第二阶段再引入 `ValidateOnlyResolver`、`CompileOnMissResolver` 或替代 backend。 5. 让 `toolchain receipt/manifest` 记录 `collector`、`producer`、`resolver` 三层版本与输出，避免后续继续把“生成工具”和“运行时加载器”混成同一个概念。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/FAngelscriptJitCollectAdapter.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/FAngelscriptStaticTranspileProducer.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/ICompiledFunctionResolver.*` |
| 预估工作量 | M |
| 架构风险 | 如果过早重写 `FJITDatabase` 或 `PrecompiledData` 结构，回归面会很大；第一阶段应只做职责切分和 facade，引擎可见行为保持不变。 |
| 兼容性 | 可增量实施。现有生成命令、现有 `AS_JITTED_CODE`、现有 `PrecompiledScript.Cache` 与现有运行时挂接路径都可以保留；变化主要是内部 owner 和接口边界，不要求用户升级脚本或重新理解工作流。 |
| 验证方式 | 1. 用旧入口生成一次 `AS_JITTED_CODE + PrecompiledScript.Cache`，再用拆分后的新 owner 跑一次，比较产物与 `FJITDatabase` 注册结果一致。 2. 回归 `PrecompiledData` 加载路径，确认 resolver 命中时仍挂上相同 `jitFunction`，未命中时仍按旧逻辑回退解释器。 3. 新增一个 fake `CollectAdapter`/fake `Resolver` 测试，验证无需真的走整套生成链也能分别单测收集与加载语义。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT58 | runtime 调试回调抽象、后端可替换性、多 IDE bridge 接入 | 抽象层新增 + 调试 backplane 解耦 | 高 |
| P2 | Arch-DT59 | `StaticJIT` 接口边界、offline producer 与 runtime resolver 分层 | 合同收敛 + owner 重组 | 中高 |

---

## 架构分析 (2026-04-10 00:32)

### Arch-DT60：`DebugServer V2` 的消息 codec 仍散落在消息体 `operator<<` 中，协议测试已漏掉实际存在的非对称分支

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试协议 codec 的对称性、消息合同的可验证性、后续 bridge/测试 harness 可复用性 |
| 当前设计 | `V2` 的消息体编码仍直接写在各个 `FDebugMessage` 结构体的 `operator<<` 中；协议测试只覆盖少数消息，未覆盖 `CallStack/CallFrame` 这类实际带条件字段的 payload。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:196-212` 中 `FAngelscriptCallFrame::operator<<` 只在 `Frame.ModuleName.IsSet()` 时写出 `ModuleName`，但没有对应的 load-side presence 判定；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1435-1474` 的 `SendCallStack()` 在 `DebugAdapterVersion >= 1` 时会主动给 Blueprint/C++/script frame 设置 `Frame.ModuleName`，说明这不是死字段，而是 live wire contract；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp:39-77,79-245` 当前只覆盖 `StartDebugging`、`DebugServerVersion`、`Breakpoint`、`Variables`、`DataBreakpoints`、`BreakFilters`、`DebugDatabaseSettings` 的 round-trip，没有 `CallFrame/CallStack/GoToDefinition` 对称性测试。 |
| 优点 | 手写 `FArchive` codec 让新增一个消息结构体的局部实现成本很低，现有已测试的消息在同一前后端组合里迭代速度快。 |
| 不足 | 现在的风险已经不是抽象层面的“schema 分散”，而是具体 codec 已经出现了“写入条件”和“读取条件”不成对的结构。按源码推断，只要后续 transcript harness、C++ bridge 或更严格的 golden round-trip 直接复用 `FAngelscriptCallFrame::operator<<`，`ModuleName` 字段就不具备稳定的双向合同；这会让 bridge 扩展和协议重构继续依赖“客户端刚好手写兼容”，而不是可验证的共享 codec。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 插件不手写 call frame/variable 的私有字段顺序，而是把消息文本直接交给 `v8_inspector::V8InspectorSession`；HTTP discovery 与 websocket session 也只是 transport/bridge。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.h:46-65`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:92-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-539` | 协议字段的 authoritative source 应尽量离开业务消息结构体，bridge 只负责转发与会话编排。 |
| UnLua | runtime 只暴露 `GetStackVariables(lua_State*, StackLevel, ...)`、`GetLuaCallStack(lua_State*)`、`PrintCallStack(lua_State*)` 这类同步调试原语，没有再维护一套独立的 wire codec。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-732` | 如果暂时不切标准协议，至少也应让“runtime 调试原语”和“wire codec”分层，避免协议正确性只能靠零散消息测试兜底。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留 `V2` 线协议不变，但把易漂移的条件字段从消息体 `operator<<` 收敛到显式 codec/helper，并补足现在缺失的 golden round-trip 护栏。 |
| 具体步骤 | 1. 在 `Debugging/Protocol/V2/` 下新增 `AngelscriptDebugProtocolV2Codec.*` 或最小 `SerializeCallFrameV2/DeserializeCallFrameV2` helper，先接管 `FAngelscriptCallFrame`、`FAngelscriptCallStack`、`FAngelscriptGoToDefinition` 这三类当前未受 round-trip 测试保护的消息。 2. 对 `CallFrame` 明确写死 `ModuleName` 的 V2 条件，而不是继续依赖 `TOptional::IsSet()` 这种只看 save-side 状态的分支；第一阶段只改 C++ codec 实现，不改 `V2` 字节布局。 3. 在 `AngelscriptDebugProtocolTests.cpp` 新增 `CallFrame.RoundTrip`、`CallStack.RoundTrip`、`GoToDefinition.RoundTrip` 和 `Envelope.CallStack.WithModuleName` 四组测试，直接复用现有 `RoundTripMessage()` / envelope helper。 4. 再补一个 transcript/golden case：同一 `CallStack` 同时含 Blueprint frame、script frame、`(C++)` frame，验证 `ModuleName`、`Source`、`LineNumber` 的组合在 codec 往返后稳定。 5. 等 codec 收敛后，再让未来 `DAP/CDP bridge` 或 fake transport 只依赖 codec/helper，不再直接复用消息体内部的零散 `operator<<` 分支。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/V2/AngelscriptDebugProtocolV2Codec.*` |
| 预估工作量 | S |
| 架构风险 | 主要风险是修 codec 时误改 `V2` 线字节布局，导致已发布前端解析失败；因此第一阶段必须以“字节级兼容”为约束，只把对称性和测试补齐，不顺手改消息 shape。 |
| 兼容性 | 向后兼容。现有 VS Code 前端继续看到同样的 `CallStack`/`CallFrame` 线协议；变化只在插件内部的 codec owner 与测试覆盖。 |
| 验证方式 | 1. 新增 `CallFrame/CallStack` round-trip 测试，特别覆盖 `ModuleName` 非空与空字符串两种场景。 2. 对同一 `CallStack` 抓取 `SerializeDebugMessageEnvelope()` 前后的字节流，确认改造前后完全一致。 3. 让旧客户端附着一次真实断点命中，确认 `ModuleName`、`Source`、`LineNumber` 显示不回退。 |

### Arch-DT61：`AngelscriptUHTTool` 仍由 `generator` 与 `exporter/reporter` 两套决策引擎并行判定，`summary`、`entries` 与 `skipped` 不是同一条事实链

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | UHT 生成链的 authoritative 决策对象、报表口径一致性、后续 GAP 关闭的可追踪性 |
| 当前设计 | `Export()` 先调用 `AngelscriptFunctionTableCodeGenerator.Generate(factory)` 产出真正的 `AS_FunctionTable_*` 和 direct/stub summary，然后又独立遍历整份 `UHT session` 重新计算 `reconstructed/skipped`；两条链共享 `TryBuild()`，但不共享同一个 decision ledger。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:27-44` 先执行 `AngelscriptFunctionTableCodeGenerator.Generate(factory)`，随后立刻再遍历 `factory.Session.Modules` 统计 `reconstructedCount/skippedCount`；`.../AngelscriptFunctionTableExporter.cs:65-97` 的 `CountBlueprintCallableFunctions(...)` 仅以 `TryBuild(...)` 成功/失败来决定 `reconstructed` 或 `skipped`；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-78` 只对 `supportedModules.All` 里的模块生成真实 shard 与 summary；`.../AngelscriptFunctionTableCodeGenerator.cs:81-113` 则以 `EraseMacro == "ERASE_NO_FUNCTION()"` 统计 `DirectBindEntries/StubEntries`；更关键的是 `.../AngelscriptFunctionTableCodeGenerator.cs:465-477` 对 `Interface/NativeInterface` 直接强制 `ERASE_NO_FUNCTION()`，并不把这个“forced stub”决策回流给 exporter 的 `reconstructed/skipped` 统计。 |
| 优点 | 当前实现已经能同时产出编译需要的 `.cpp`、机器可读的 summary/CSV 和一个更偏诊断视角的 `SkippedEntries.csv`，对人工排查很直接。 |
| 不足 | 结构问题在于“同一个函数为什么是 `Direct/Stub/Skipped`”没有单一 owner。按源码推断，至少 `Interface/NativeInterface` 这类 forced-stub 决策、以及支持范围过滤，天然可能让 `summary/entries` 与 `skipped/reconstructed` 描述不同一事实；这会把未来的 Bind GAP 收敛、对比脚本、CI 门禁和文档解释继续建立在多套口径之上。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `ParamDefaultValueMetas` 作为单一 `ScriptGenerator` modular feature 注册；同一个 producer 对外声明 `ShouldExportClassesForModule()`、`Initialize()`、`ExportClass()`、`FinishExport()`，生成范围和落盘责任都在一条生命周期里。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:29-37`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:29-55`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:64-129` | “谁应该生成、为什么生成、最终写了什么”应由同一个 producer 决策对象负责，而不是 generator/reporter 分两次推导。 |
| UnLua | `FUnLuaIntelliSenseGenerator` 自己维护初始化、全量刷新、增量更新、保存和删除；commandlet 需要 Blueprint IntelliSense 时直接复用 `Generator->Initialize(); Generator->UpdateAll();`，而不是再复制一套决策器。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:188-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:248-290`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:107-112` | driver 可以有多个，但“导出决策”和“输出 contract”最好仍由单一 producer 维护。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `AngelscriptFunctionTable` 建一个共享的 `FunctionTableDecisionLedger`，让 `generator`、`summary`、`entries`、`skipped` 都从同一批 decision records 派生。 |
| 具体步骤 | 1. 在 `AngelscriptUHTTool` 新增 `AngelscriptFunctionTableDecision.cs` / `AngelscriptFunctionTableDecisionLedger.cs`，字段至少包含 `ModuleName`、`ClassName`、`FunctionName`、`ScopeStatus`、`DecisionKind(Direct/ForcedStub/Skipped/OutOfScope)`、`FailureReason`、`ShardIndex?`。 2. 让 `CollectEntries()` 不再直接写 `ERASE_NO_FUNCTION()` 后就结束，而是先生成 `DecisionKind=ForcedStub`、`FailureReason=interface-class` 等结构化决策；`GenerateModule()` 只根据 ledger 渲染 `.cpp` 与 `Entries.csv`。 3. 将 `CountBlueprintCallableFunctions(...)` 改为读取同一 ledger 生成 `reconstructed/skipped` 视图，而不是再次递归 `UHT session`；控制台输出、`SkippedEntries.csv`、`SkippedReasonSummary.csv` 与 `Summary.json` 统一从 ledger 派生。 4. 兼容阶段保留现有文件名，但为 JSON/CSV 追加 `DecisionKind`、`FailureReason`、`ScopeStatus` 三列/字段，让现有消费方不必立刻迁移。 5. 第二阶段再把 ledger 接到更高层 `SymbolGraph/CapabilityLedger`，使 `UHT forced stub`、runtime fallback、docs/debug database 缺失原因可以按同一 `SymbolKey` 对账。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`，新增 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableDecision.cs`，新增 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableDecisionLedger.cs` |
| 预估工作量 | M |
| 架构风险 | 风险不是生成结果，而是迁移期 summary/CSV 字段会新增，如果下游脚本把“固定列数”写死，短期需要先做附加列而不是改名或删列。 |
| 兼容性 | 可增量实施。现有 `AS_FunctionTable_*`、`AS_FunctionTable_Summary.json`、`AS_FunctionTable_Entries.csv`、`AS_FunctionTable_SkippedEntries.csv` 都继续保留，先只附加新字段和统一口径，不改文件名。 |
| 验证方式 | 1. 选择一个 `Interface/NativeInterface` 函数，验证 ledger 明确记录 `ForcedStub`，同时 `Entries.csv`、summary 和 skipped 报表都引用同一决策。 2. 选择一个普通签名失败函数，验证它在 ledger 中是 `Skipped`，不会再同时出现在 `Direct/Stub` 统计的另一套口径里。 3. 对比迁移前后的 `.cpp` shard 输出，确认真正的生成结果不变，只是 sidecar/报表口径收敛。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT60 | 调试协议 codec 对称性、`CallStack` 合同测试覆盖、bridge 可复用性 | 协议实现收敛 + golden 测试新增 | 高 |
| P1 | Arch-DT61 | `UHT` 生成链 authoritative 决策对象、summary/entries/skipped 口径统一 | producer 收敛 + decision ledger 新增 | 高 |

---

## 架构分析 (2026-04-10 00:41)

### Arch-DT62：`DebugServer` 的生产收包路径仍绕过 `TryDeserializeDebugMessageEnvelope()`，transport helper、自动化测试与线上 socket pump 不是同一条实现链

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试 transport 的单一真实实现、协议测试与线上行为一致性 |
| 当前设计 | `V2` 线协议已经提炼出 `SerializeDebugMessageEnvelope()` / `TryDeserializeDebugMessageEnvelope()`，出站队列和 transport 自动化测试都围绕这套 helper；但 live 入站路径仍在 `ProcessMessages()` 中手写 `Recv + 读包长 + 读 message type + 直接分发`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:92-93` 声明了 envelope helper；`.../AngelscriptDebugServer.cpp:52-108` 实现了长度校验、partial buffer 保留和多 envelope 递进消费；`.../AngelscriptDebugServer.h:648-687` 的 `SendMessageToAll()` / `SendMessageToClient()` 已统一通过 `SerializeDebugMessageEnvelope()` 组包；但 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:712-795` 的 `ProcessMessages()` 仍直接 `Recv` 两段数据、手动 `*Datagram << MessageType` 后调用 `HandleMessage()`，完全不复用 `TryDeserializeDebugMessageEnvelope()`；`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp:43-231` 已对 helper 做了 `SingleEnvelope/MultipleEnvelopes/TruncatedEnvelope/InvalidLength/EmptyBodyEnvelope` 测试；而 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp:25-35,79-245` 仍只是对消息体 `operator<<` 做内存 round-trip。 |
| 优点 | 当前已经具备一套明确的 envelope 合同和自动化测试基线，这意味着重构 live transport 时不需要重新设计 wire bytes。 |
| 不足 | 现在的问题不是“缺少 helper”，而是 helper 还不是 authoritative runtime path。生产代码与测试代码分别维护两套入站语义后，`TruncatedEnvelope`、多包粘连、错误长度诊断到底以哪一套为准并不清晰；未来加 `websocket/DAP bridge` 或 transcript harness 时，也会继续面对“复用 helper 还是模仿 `ProcessMessages()`”的分叉。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `websocketpp` 负责 frame 边界与收发，live path 由 `Server.set_message_handler(...) -> OnReceiveMessage() -> channel->DispatchProtocolMessage(...)` 直达 `V8InspectorSession`；出站同样统一走 `OnSendMessage() -> Server.send(...)`。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:319-328`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-540` | transport 的真实接收链和协议会话链是一条实现，不会出现“测试 helper 一套、线上收包另一套”。 |
| UnLua | 不提供独立 wire transport；调试能力直接暴露为 `GetStackVariables(lua_State*, ...)`、`GetLuaCallStack(lua_State*)`、`PrintCallStack(lua_State*)` 本地 API。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-732` | 如果不复用标准 transport，就更应该避免内部再出现两套 parser；runtime 事实链最好只有一条。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 让 `TryDeserializeDebugMessageEnvelope()` 成为唯一的入站 envelope 解析点，把 live socket pump 降为“收字节并喂给 helper”的薄层。 |
| 具体步骤 | 1. 在 `FAngelscriptDebugServer` 或后续独立 transport pump 中新增 per-client `ReceiveBuffer`，`Recv` 到的原始字节只负责 append，不再在 `ProcessMessages()` 里手写“先读 4 字节再读整包”的双阶段循环。 2. 每次 append 后循环调用 `TryDeserializeDebugMessageEnvelope(ReceiveBuffer, Envelope, bHasEnvelope, &Error)`；只有 `bHasEnvelope` 为真时才把 `Envelope.MessageType + Envelope.Body` 交给 `HandleMessage()`。 3. 将 `HandleMessage()` 的签名逐步收敛为接收 `FAngelscriptDebugMessageEnvelope` 或最少接收 `TArray<uint8> Body`，避免未来 transport adapter 还要伪造 `FArrayReaderPtr`。 4. 第一阶段保持 `SerializeDebugMessageEnvelope()`、`EDebugMessageType` 和现有字节布局完全不变，只替换生产收包 owner；第二阶段再把当前 `AngelscriptDebugTransportTests.cpp` 的用例提升为“同一 receive loop 的 transcript 测试”，不再只测裸 helper。 5. 如后续接 `websocket` 或 `DAP bridge`，也统一产出同一 `FAngelscriptDebugMessageEnvelope` 抽象，而不是复制一套新 parser。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 预估工作量 | M |
| 架构风险 | 最大风险是替换 live 收包后误改了客户端断线、partial packet 或 `ProcessMessages()` 调度时机；因此第一阶段必须把 wire bytes 和 `HandleMessage()` 语义保持不变，只调整“谁负责切 envelope”。 |
| 兼容性 | 向后兼容。旧前端仍使用同一 `V2` 字节协议；变化只在插件内部把入站 parser 收敛到现有 helper，不要求客户端升级。 |
| 验证方式 | 1. 在现有 `Debug.Transport.*` 基础上新增“真实 receive loop”测试，验证同一段字节流经 live parser 与 helper 结果一致。 2. 构造粘包、截断、非法长度和单字节到达场景，确认 runtime 不再依赖“socket 刚好一次返回整包”。 3. 回归旧 VS Code 前端的 `StartDebugging/Pause/Continue/CallStack` 基本流程，确认字节级兼容。 |

### Arch-DT63：`AngelscriptUHTTool` 仍是“empty module + static utility”组合，缺少可复用的 generation session，新增 toolchain facet 只能继续复制遍历

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 工具链 producer 生命周期、一次运行内的共享上下文、后续扩展 facet 的接入成本 |
| 当前设计 | `AngelscriptUHTTool` 当前只有静态 exporter / static codegen helper，没有 run-scoped session object 来承接“本轮支持模块、已收集函数、输出文件、报表和扩展 emitter”；而 `Docs` 又在 C++ runtime 里走另一条静态 monolith。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.cs:1-4` 基本为空壳；`.../AngelscriptFunctionTableExporter.cs:21-54` 的 `Export(IUhtExportFactory factory)` 直接用局部变量计数、调用 `AngelscriptFunctionTableCodeGenerator.Generate(factory)` 后再单独遍历 `factory.Session.Modules`；`.../AngelscriptFunctionTableCodeGenerator.cs:51-79` 的 `Generate(factory)`、`:334-385` 的 `LoadSupportedModules(factory)`、`:432-447` 的 `DeleteStaleOutputs(...)`、`:449-487` 的 `CollectEntries(...)` 全是 static utility；与此同时，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2224-2227` 仍通过 `RuntimeConfig.bDumpDocumentation` 直接触发 `FAngelscriptDocs::DumpDocumentation(Engine)` 并退出进程，而 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:675-755` 又是另一段静态遍历 + 落盘逻辑。 |
| 优点 | 现有实现直接、低仪式感，`AS_FunctionTable_*` 与 summary/CSV 已经稳定产出，短期维护成本低。 |
| 不足 | 问题不是“没有输出”，而是没有一个本轮 run 的 owner。缺少 session object 后，`supportedModules`、`entries`、`generatedPaths`、`skippedEntries`、summary 与未来的 `symbol graph/navigation index/jit coverage` 都无法自然挂到同一次遍历上；新增一个 facet 时，最容易的做法就又是加一段新的 static 扫描或再走一次不同语言侧的遍历。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 在 `.uplugin` 中把 `ParamDefaultValueMetas` 声明成独立 `Program` module，并在 `FParamDefaultValueMetasModule` 中显式实现 `Initialize()`、`ExportClass()`、`FinishExport()`，把 `OutputDir`、`GeneratedFileContent`、`Finished` 放进同一个 producer 生命周期。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:29-37`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:29-63`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:64-129` | toolchain producer 最好先有 run/session owner，再谈输出哪些文件。 |
| UnLua | `FUnLuaIntelliSenseGenerator` 是可复用对象，维护 `Initialize()`、`UpdateAll()`、`SaveFile()`、`DeleteFile()`；commandlet 通过 `Generator->Initialize(); Generator->UpdateAll();` 复用同一个 producer，而不是再复制一套导出逻辑。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:33-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:58-107`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:107-112` | 不同 driver 可以共存，但它们应复用同一个 generation session / producer，而不是各自重建事实。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 先在 `AngelscriptUHTTool` 内引入 run-scoped `GenerationSession`，把 static utility 收拢成一个可复用 producer；现有输出文件名和内容顺序保持不变。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableGenerationSession.cs` 或更通用的 `AngelscriptToolchainRunContext.cs`，字段至少持有 `Factory`、`SupportedModules`、`Entries/DecisionRecords`、`GeneratedPaths`、`SkippedEntries`、`ModuleSummaries`。 2. 将 `Export(factory)` 改为：创建 session，执行 `Initialize(factory)`、`Collect()`、`EmitCppShards()`、`EmitReports()`、`Finish()`；把今天 `Generate()`、`LoadSupportedModules()`、`DeleteStaleOutputs()`、`CountBlueprintCallableFunctions()` 的局部状态逐步迁入 session。 3. 第一阶段只做 owner 重组，不改 `AS_FunctionTable_*.cpp`、`AS_FunctionTable_Summary.json`、`AS_FunctionTable_Entries.csv`、`AS_FunctionTable_Skipped*.csv` 的文件名与排序；用 golden diff 保证输出稳定。 4. 在 session 上增加最小 emitter 扩展点，例如 `IAngelscriptToolchainEmitter` 或简单的 `EmitReceipt()`，先落一份 `AS_FunctionTable_RunReceipt.json`，记录本轮输入 `Build.cs/header dependencies`、输出文件和统计摘要。 5. 第二阶段再让 `Docs`、`NavigationIndex`、`JitCoverage` 读取这份 receipt 或同 schema sidecar，而不是继续通过 runtime flag 或独立 static 遍历自建 owner。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，新增 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableGenerationSession.cs`，后续联动 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp` |
| 预估工作量 | M |
| 架构风险 | 主要风险是把 static helper 改成 session 后打破当前输出顺序或 stale cleanup 行为；因此第一阶段必须先做“内部 owner 重组 + 输出不变”，不要同时改 schema。 |
| 兼容性 | 可增量实施。现有 UHT 生成文件、现有 docs dump 和现有消费脚本都可继续工作；新增 session 与 receipt 只是提供统一 owner，不要求立即迁移下游。 |
| 验证方式 | 1. 对 session 化前后的 `AS_FunctionTable_*` 与 summary/CSV 做逐文件 diff，确认内容与排序一致。 2. 验证 stale cleanup 仍只删除本轮未生成的旧 shard。 3. 生成一次 `RunReceipt`，检查其中的输入依赖、输出路径与控制台统计能一一对上。 4. 作为下一阶段试点，让一个只读 `Docs` 或 `NavigationIndex` emitter 复用该 session 的 receipt，而不是再额外扫描一次同批模块。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT62 | transport helper/测试/线上收包链路分叉 | transport owner 收敛 + 真实链路测试统一 | 高 |
| P2 | Arch-DT63 | `UHT` 缺少 run-scoped generation session，toolchain facet 无法共享一次遍历 | producer 生命周期收敛 + receipt 新增 | 中高 |

---

## 架构分析 (2026-04-10 00:52)

### Arch-DT64：toolchain 输出路径仍以宿主工程进程环境为中心，不适合作为独立插件的可复用交付合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 插件中心化交付、toolchain 输出根、宿主工程耦合 |
| 当前设计 | `StaticJIT`、`Docs`、`UHT` 三条产线各自决定输出根：JIT 直接写 `FPaths::RootDir()/AS_JITTED_CODE`，文档直接写 `FPaths::ProjectDir()/Docs/angelscript/generated`，只有 `UHT` 产线通过 `IUhtExportFactory::MakePath()` 接受外部输出目录。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3499-3507` 直接把 `GenDir` 设为 `FPaths::RootDir() / "AS_JITTED_CODE"`；`.../AngelscriptStaticJIT.cpp:3588-3590`、`:3683-3695` 继续把 module 文件与 `AngelscriptJitInfo.jit.cpp` 都写进这个根；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:682-755` 把每个文档文件写到 `FPaths::ProjectDir() / "/Docs/angelscript/generated"`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:522-529`、`:2224-2227` 说明 docs 导出由 runtime 命令行 flag 触发并在当前宿主进程里直接执行；相对地，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:120-121`、`:174-205` 已经通过 `factory.MakePath(...)` 与 `summaryPath = factory.MakePath(...)` 接受外部路径策略。 |
| 优点 | 当前实现落地直接，本地单工程开发时几乎不需要额外配置，运行一次就能在固定位置看到产物。 |
| 不足 | 对“插件是主交付物、工程只是宿主”的目标来说，这套路径合同不稳定：同一插件在不同宿主工程、不同 worktree、不同 CI 工作目录下会把产物散落到不同根；JIT 和 docs 也无法像 `UHT` 一样被外部 build orchestration 显式重定向。结果是工具链扩展首先要碰路径硬编码，而不是只新增 producer。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense producer 在初始化时显式把输出根锚定到插件目录 `FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense"`，后续 `SaveFile/DeleteFile` 都围绕这一个 plugin-scoped root 工作。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245` | 先把 artifact root 变成插件自己的稳定边界，再谈增量更新和多 driver 复用。 |
| puerts | `ScriptGenerator` modular feature 在 `Initialize(...)` 中直接接收 `OutputDirectory`，`FinishExport()` 只向这个外部注入的目录落盘。输出根不是 runtime 自己猜出来的，而是由宿主 build 流程提供。 | `Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:29-54`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:53-62`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:115-129` | producer 最好依赖显式 path policy，而不是把当前进程的 project/root 目录当成长期合同。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `Docs`、`StaticJIT`、`PrecompiledData` 补一个与 `UHT factory.MakePath()` 对齐的统一 `ToolchainPathPolicy`，默认以插件根为中心，宿主工程路径只作为可选 publish/mirror 层。 |
| 具体步骤 | 1. 新增 `Toolchain/AngelscriptToolchainPathPolicy.*` 或等价配置对象，最少暴露 `PluginBaseDir`、`PluginIntermediateDir`、`HostProjectDir`、`PublishDocsDir`、`PublishJitDir`、`Profile`。 2. 将 `FAngelscriptStaticJIT::WriteOutputCode()` 的 `FPaths::RootDir()/AS_JITTED_CODE` 改为从 path policy 取 `PluginIntermediateDir/JIT`；第一阶段仍可在成功写入后镜像一份到旧 `AS_JITTED_CODE` 兼容路径。 3. 将 `FAngelscriptDocs::DumpDocumentation()` 的 `FPaths::ProjectDir()/Docs/angelscript/generated` 改为 `PluginIntermediateDir/Docs` authoritative 根；仅在显式 `publish-docs` 或 legacy 模式下再镜像到旧目录。 4. 让 runtime 侧 toolchain job 也接受类似 `factory.MakePath()` 的 override，使命令行、commandlet、CI 可以像 `UHT` 一样精确声明输出目录，而不是依赖当前工作区根。 5. 在 manifest/receipt 中追加 `artifactRootKind(plugin-intermediate/host-publish/uht-factory)` 与实际路径，后续 review/CI 才能知道当前产物属于哪一层。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptToolchainPathPolicy.*`，联动 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` 的 manifest 输出 |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段直接切断旧 `AS_JITTED_CODE` / `Docs/angelscript/generated`，现有脚本和人工工作流会立刻丢路径；应先 authoritative root 收敛，再保留镜像路径过渡。 |
| 兼容性 | 可增量实施。旧 docs/JIT 目录继续保留为兼容发布层；新路径策略只新增 plugin-scoped authoritative 根，不要求用户立刻改现有命令或 IDE 习惯。 |
| 验证方式 | 1. 在同一插件下切换两个不同宿主工程运行 docs/JIT 导出，确认 authoritative 根都落在 `Plugins/Angelscript/Intermediate/...` 而不是随宿主漂移。 2. 回归旧 `AS_JITTED_CODE` 和 `Docs/angelscript/generated` 读取方，确认镜像层仍可用。 3. 在 CI 中显式指定自定义输出根，验证运行结果不再依赖当前工作目录。 4. 核对 manifest 中的 path policy 字段，确认 `UHT/docs/jit` 三类 producer 能汇总到同一套路径合同。 |

### Arch-DT65：源码身份仍混用本机绝对路径、module 名和 IDE 命令，缺少可远程消费的 `SourceUri` 合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 远程调试、多 IDE 前端、源码定位合同 |
| 当前设计 | 当前源码定位既不是统一的 `SourceLocation`/`SourceUri`，也不是标准协议对象，而是三套并存事实：Editor 导航直接拿本机绝对路径并执行 `code --goto`；`UASFunction` 暴露的是宿主机器文件路径；JIT callstack 里又把 `SCRIPT_DEBUG_FILENAME` 简化成 module 名。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp:34-45` 对 `UASFunction` 直接调用 `GetSourceFilePath()/GetSourceLineNumber()` 并 `OpenFile(...)`；`.../AngelscriptSourceCodeNavigation.cpp:95-115` 的 `OpenModule/OpenFile` 最终都是 `FPlatformMisc::OsExecute(nullptr, "code", "--goto ...")`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp:1535-1545` 的 `UASFunction::GetSourceFilePath()` 直接返回 `Module->Code[0].AbsoluteFilename`；`.../ASClass.cpp:1548-1558` 的行号也直接来自本地 script data；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3612-3622` 又把 `SCRIPT_DEBUG_FILENAME` 生成成 `File.ModuleName`，不是 section/file path；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp:706-717` 还把 workspace 入口固定为 `<Project>/Script` 的 VS Code 菜单。 |
| 优点 | 在“Editor、脚本工作区、VS Code 都在同一台机器上”的单机开发流里，这种设计简单直接，本地跳转速度快。 |
| 不足 | 这套合同无法稳定支撑远程或多 IDE 前端：同一个符号在不同通道里分别表现为绝对路径、module 名、`TypeName/SymbolName` 或本地 `code` 命令；前端无法判断这些身份是否指向同一源码，也无法对不同宿主/worktree 做 path remap。即使后续补了 symbol graph，没有统一 `SourceUri`，桥接层仍然只能猜测如何把服务端位置翻译成客户端工作区位置。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | runtime 调试原语直接返回 Lua debug API 的源码身份：`GetLuaCallStack()` 读取 `lua_getinfo(..., "nSl")` 后把 `ar.source` 与 `ar.currentline` 返回；而 IDE 连接由外部 `LuaPanda/LuaHelper` 负责建立，不在插件里直接 shell 本机编辑器。 | `Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:672-686`；`Reference/UnLua/Docs/CN/Debugging.md:5-7` | 先把“源码身份”作为 runtime-neutral 数据返回，再把具体 IDE/路径映射交给外部前端或 bridge。 |
| puerts | Inspector 只暴露标准 debug target：`/json/list`、`/json/version` 和 `webSocketDebuggerUrl`；插件对外提供的是可连接 target，而不是本机 `OpenFile` 行为。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:330-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477` | 调试核心只需要给出稳定 target/source contract，具体编辑器跳转由标准前端生态处理。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有 `GoToDefinition`、`UASFunction`、JIT debug map 之下新增统一 `SourceUri + PathMapper` 层，把本机 `code --goto` 降为一个 Editor-only consumer。 |
| 具体步骤 | 1. 新增 `Toolchain/Navigation/AngelscriptSourceUri.h/.cpp` 与 `IAngelscriptPathMapper`，统一描述 `sourceUri`、`displayPath`、`line`、`column`、`workspaceId`；第一阶段支持 `file://` 与 `as://module/<module>/section/<name>` 两类 scheme。 2. 让 `UASFunction::GetSourceFilePath()/GetSourceLineNumber()` 的上层 consumer 不再直接假定“绝对路径可本机打开”，而是先构造 `ResolvedSourceLocation`；`AngelscriptSourceCodeNavigation` 仅作为把 `ResolvedSourceLocation` 映射到本地 `code --goto` 的默认实现。 3. 将 `GoToDefinition` / `ResolvedDefinition`、未来 `SymbolGraph`、`DebugDatabase` 和 `JitSourceMap` 统一改为输出 `sourceUri`；旧 `Path`/`ModuleName` 字段继续保留给 legacy 前端。 4. 将 `SCRIPT_DEBUG_FILENAME` 的使用收敛到兼容层，新 JIT sidecar 记录 `SourceUri` 与 module/section/path 的映射；这样 JIT、解释器、docs、导航可以在同一个 join key 上对齐。 5. 在设置或 manifest 中增加 path mapping 表，允许远程前端把 `file://Server/...` 或 `as://module/...` 映射到本地工作区路径；未配置 mapping 时，Editor 仍可退回当前本机 `code --goto` 行为。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptSourceCodeNavigation.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/Navigation/AngelscriptSourceUri.*`，后续联动 `Debugging` / `SymbolGraph` / `JitSourceMap` 相关文件 |
| 预估工作量 | M |
| 架构风险 | 如果直接把所有 consumer 一次性切到 `SourceUri`，旧 VS Code 扩展和本地导航路径会同时受影响；应先双写 `sourceUri + legacy path/module`，再逐步让前端迁移。 |
| 兼容性 | 可增量实施。旧前端继续读 `Path/ModuleName` 并沿用 `code --goto`；新前端才消费 `sourceUri + path mappings`。对现有脚本语义无影响。 |
| 验证方式 | 1. 为同一脚本函数同时抓取解释器 callstack、JIT 调试信息和 `GoToDefinition` 结果，确认三者都能 join 到同一个 `sourceUri`。 2. 在两台机器或两个不同 worktree 下配置 path mapping，验证同一 `sourceUri` 能映射到各自本地工作区。 3. 回归旧 VS Code 路径，确认未配置 `sourceUri` consumer 时仍会走现有 `code --goto`。 4. 新增一个只消费 `ResolvedDefinition.sourceUri` 的 fake 前端，验证无需本机 `code` 命令也能完成定义解析。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT64 | toolchain 输出根的宿主工程耦合、插件中心化交付路径 | 路径策略收敛 + producer path policy 新增 | 高 |
| P1 | Arch-DT65 | 源码身份合同、远程/多 IDE path mapping、`SourceUri` 统一 | 协议扩展 + 导航层解耦 | 高 |

---

## 架构分析 (2026-04-10 01:00)

### Arch-DT66：`Docs/Debug/JIT/UHT` 各自产生自己的输入作用域，缺少可对账的 `ScopeDescriptor`

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | producer 作用域合同、跨产线缺失原因可解释性、IDE/CI 对账能力 |
| 当前设计 | 现在并不存在“这一轮工具链到底覆盖了哪些脚本、模块、函数”的统一描述。runtime 编译按 script root 和 `Dev/Examples/Editor` 目录过滤；`Docs/DebugDatabase` 只看当前 live script engine 已加载的符号；`StaticJIT` 只看生成模式下 AngelScript 主动请求编译的函数；`UHT` 则只看 `supportedModules` 与 `BlueprintCallable` 候选。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1427-1433` 在确定 runtime profile 后才 `DiscoverScriptRoots(...)`；`.../AngelscriptEngine.cpp:1973-2013` 会按 `bSkipDevelopmentScripts/bSkipEditorScripts` 跳过 `Examples/Dev/Editor` 目录。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:516-617` 与 `.../AngelscriptDocs.cpp:629-672` 的 docs 导出直接枚举当前 engine 中已存在的 object type、global function、global property 和 enum。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:66-75` 只有 `CompileFunction()` 被调用时才把函数放进 `FunctionsToGenerate`；`.../AngelscriptStaticJIT.cpp:3476-3497` 最终只对这批函数做分析和生成。<br>`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-79` 先读 `LoadSupportedModules(factory)`，只对 `supportedModules.All` 中的模块生成；`.../AngelscriptFunctionTableCodeGenerator.cs:490-510` 还会继续按 `IsSupportedHeader/IsBlueprintCallable/metadata` 再筛一次函数。 |
| 优点 | 每条产线都能围绕自己的目标最小化输入面，例如 runtime 编译避免无关脚本，`UHT` 只处理对 function table 有意义的函数。 |
| 不足 | 缺点是“缺失”没有统一语义。对同一个 symbol，IDE/CI 现在很难区分它是 `out-of-scope`、`not loaded`、`not JIT-requested`、`not BlueprintCallable`，还是单纯生成失败；这会让跨产线对账继续依赖人工猜测，而不是 machine-readable scope。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | commandlet 先显式拿 `GetExportedReflectedClasses/GetExportedNonReflectedClasses/GetExportedEnums/GetExportedFunctions`，只有命令行 `BP=1` 时才再追加 Blueprint IntelliSense；scope 是 API 明确给出的，而不是从当前进程碰巧加载了什么去反推。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-112` | 先把 producer 输入面做成显式集合，再让不同 driver 决定是否追加可选 slice。 |
| puerts | `.uplugin` 直接把 `DeclarationGenerator`、`ParamDefaultValueMetas` 声明成独立 toolchain module；`ShouldExportClassesForModule(...)` 再显式限制只处理 `EngineRuntime/GameRuntime`。 | `Reference/puerts/unreal/Puerts/Puerts.uplugin:15-48`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:46-62` | toolchain scope 最好先成为插件元数据和 producer API 的一部分，而不是运行时副作用。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `runtime compile/docs/debug/jit/uht` 增加统一的 `ScopeDescriptor`，先解释“这一轮谁在 scope 内”，再解释“scope 内为什么失败”。 |
| 具体步骤 | 1. 新增 `Toolchain/AngelscriptToolchainScopeDescriptor.*` 和 sidecar `AngelscriptToolchainScope.json`，字段至少包含 `producerId`、`scopeId`、`scopeKind(script-roots/live-engine/jit-request-set/uht-module-set)`、`includedRoots`、`includedModules`、`skipRules`、`profile`。 2. 在 `FAngelscriptEngine` 完成 `DiscoverScriptRoots()` 与 `FindAllScriptFilenames()` 过滤后，把 runtime 侧实际 script roots 和 `Dev/Editor` 跳过规则写入 descriptor；`Docs/DebugDatabase` 只附加引用同一个 `scopeId`，不再默默假定“当前 live engine 就代表全部 scope”。 3. 让 `AngelscriptUHTTool` 在现有 summary/CSV 之外追加 `uhtScope` 节点，明确 `supportedModules`、`editorOnlyModules`、`header filters`、`function eligibility`；原有 `ShouldGenerate()` 结果继续保留，但附带 `scopeId/scopeStatus`。 4. 让 `StaticJIT` 在 `FunctionsToGenerate` 收集结束后输出 `jitScope`，至少记录 `requestedFunctionIds`、`requestingModule`、`excludedReason(not-requested/not-supported)`；不改变当前生成结果，只增加可解释层。 5. 后续再让 `DebugDatabase`、JIT manifest、UHT summary、docs manifest 都引用同一个 `scopeId`，这样 IDE 才能稳定回答“这个 symbol 为什么不在某条产线里”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptToolchainScopeDescriptor.*` |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段试图把所有 producer 立即切到一个绝对统一的 scope 计算器，容易把各自产线已有的过滤语义打乱；更稳妥的做法是先“各自继续计算，但统一写出 descriptor”，第二阶段再谈收敛。 |
| 兼容性 | 可增量实施。现有 docs/JIT/UHT/debug 输出格式都可保持不变，新加的是解释性 sidecar 和 `scopeId/scopeStatus` 字段；旧消费方忽略这些字段即可。 |
| 验证方式 | 1. 选一个 `Dev` 或 `Editor` 目录下脚本，验证 runtime scope 会把它标成 `excludedByProfile`，而 docs/debug 不再把“没加载到”与“生成失败”混写。 2. 选一个不在 `supportedModules` 的 `BlueprintCallable` 函数，验证它只出现在 `uhtScope=observed/excluded`，不计入 in-scope skipped。 3. 选一个未被 `StaticJIT` 请求编译的脚本函数，验证 `jitScope` 能明确标为 `not-requested`。 4. 用同一轮构建的 `scopeId` 对账 docs/debug/jit/uht，确认缺失原因可机器化解释。 |

### Arch-DT67：产物写盘仍缺少统一 `publish` 阶段，toolchain 只能靠各 producer 各写各的

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | artifact publish 合同、run-level 一致性、增量写盘治理 |
| 当前设计 | 仓库里已经同时存在至少三套写盘语义：`UHT` shard 走 `factory.CommitOutput(...)`，但 summary/CSV/skipped report 又直接 `File.WriteAllText(...)`；`Docs` 直接把 `.hpp` 覆盖写到目标目录；`StaticJIT` 既有“读旧内容后 changed-only 写”的文件，也有始终直接覆盖的 `AngelscriptJitInfo.jit.cpp`，最后再按目录差集删旧文件。它们没有共享的 `stage -> publish -> cleanup` 边界。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:120-121` 的 shard 生成使用 `factory.CommitOutput(...)`，但 `.../AngelscriptFunctionTableCodeGenerator.cs:174-205`、`218-242`、`244-260` 的 summary/CSV 全是直接 `File.WriteAllText(...)`；`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:99-160` 的 skipped report 也是直接 `File.WriteAllText(...)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:675-755` 对每个 docs 文件直接 `FFileHelper::SaveStringToFile(...)`。<br>`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:3552-3578`、`.../AngelscriptStaticJIT.cpp:3588-3672` 对 shared header/module/unity file 采用 changed-only 写法，但 `.../AngelscriptStaticJIT.cpp:3683-3703` 的 `AngelscriptJitInfo.jit.cpp` 仍是直接写出后再做目录差集删除。 |
| 优点 | 当前实现简单直接，而且部分路径已经具备 changed-only 写盘或 stale cleanup，短期内可工作。 |
| 不足 | 问题在于 run-level 一致性没有 owner。一次中断或失败可能留下“新 shard + 旧 summary + 旧 docs + 半新的 JIT 目录”这种混合状态；未来新增一个 producer，也几乎只能再复制一遍 `MakeDirectory/LoadOld/Save/Delete` 逻辑，而不是接入统一 publish 层。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `ParamDefaultValueMetas` 明确分成 `Initialize()`、`ExportClass()`、`FinishExport()` 三段，先在内存里累计内容，最后由 `FinishExport()` 统一做 changed-only 写盘。 | `Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:53-127` | 即使产物只有一个文件，也先把“收集”和“最终 publish”分开，producer 才有稳定 owner。 |
| UnLua | `IntelliSense` generator 把输出根固定到插件 `Intermediate/IntelliSense`，所有增量写入和删除都统一经过 `SaveFile()` / `DeleteFile()`。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:42-55`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245` | 不同 driver 可以共存，但写盘语义最好只经过一层统一 API。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在现有文件名和目录不变的前提下，先补一个共享 `ArtifactPublisher`，把 `Docs/JIT/UHT report` 的 publish 语义从“各写各的”收回到一条统一生命周期。 |
| 具体步骤 | 1. 新增 `Toolchain/AngelscriptArtifactPublisher.*`，或在 `AngelscriptUHTTool` 侧补对应 C# helper，统一暴露 `BeginRun()`、`StageText()`、`PublishIfChanged()`、`DeleteStale()`、`CommitRun()`、`AbortRun()`。 2. 保留 `factory.CommitOutput(...)` 处理 `AS_FunctionTable_*.cpp`，但把 summary/CSV/skipped report 改为同样先 stage 再 publish；让 `DeleteStaleOutputs(...)` 只读 publisher 的 staged manifest，而不是自己扫目录猜。 3. 让 `DumpDocumentation()` 和 `StaticJIT::WriteOutputCode()` 先把所有文本产物写入 staging area 或内存 manifest，只有在整轮生成成功后才统一 commit；legacy 目录继续作为最终 publish 目标，不要求用户改路径。 4. 在 publisher manifest 中记录 `runId`、`producerId`、`artifactKind`、`relativePath`、`contentHash`、`publishState(staged/committed/deleted)`；后续 `receipt/manifest`、IDE 和 CI 都只认这一份 authoritative publish 记录。 5. 第二阶段再把旧的 changed-only 细节、镜像路径和 stale cleanup 逐步删掉，避免每个 producer 永久维持一套私有写盘逻辑。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptArtifactPublisher.*` 或对应 C# helper |
| 预估工作量 | M |
| 架构风险 | 如果一开始就要求所有 producer 同时切到 staging/publish，改动面会过大；第一阶段应先做“publisher 代理层”，内部仍调用现有写盘逻辑，但把 manifest 和 commit 时机集中。 |
| 兼容性 | 完全可以向后兼容。现有文件名、目录和消费方都不必变化；publisher 只改变内部写盘流程和附加 manifest，不改变最终 artifact 位置。 |
| 验证方式 | 1. 人工在 UHT summary 或 JIT 生成中途注入失败，确认不会留下“部分新文件 + 部分旧文件”的混合 publish 结果。 2. 连续跑两次完全相同输入，确认 publisher manifest 不变，且不会无谓重写 unchanged artifact。 3. 回归现有 `AS_FunctionTable_*`、docs `.hpp`、`AS_JITTED_CODE` 目录，确认文件名和内容保持兼容。 4. 让一个外部脚本只读取 publisher manifest，就能判断本轮 run 是否完整提交。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT66 | 跨 producer 输入作用域缺少统一 `ScopeDescriptor`，缺失原因不可对账 | 合同新增 + scope 语义显式化 | 高 |
| P1 | Arch-DT67 | toolchain 缺少统一 `publish` 生命周期，artifact 仍由各 producer 分散写盘 | producer 生命周期收敛 + publish 层新增 | 高 |

---

## 架构分析 (2026-04-10 01:09)

### Arch-DT68：`DebugServer` 已区分 observer/debugger client 列表，但没有 `controller lease`，多前端控制权仍是未建模状态

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 多 IDE 前端并存、只读观察者隔离、调试控制权治理 |
| 当前设计 | `FAngelscriptDebugServer` 虽然分别维护了 `ClientsThatWantDebugDatabase` 与 `ClientsThatAreDebugging`，但 `Pause/Continue/Step*`、断点与 data breakpoint 写操作并不校验调用方是否持有控制权；全局暂停态、断点集合和 data breakpoint 仍由任意已连接 client 直接改写。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:585-600` 定义了 `ClientsThatWantDebugDatabase`、`ClientsThatAreDebugging` 以及全局 `bIsPaused/bIsDebugging/BreakOptions`。`.../AngelscriptDebugServer.cpp:822-826` 收到 `RequestDebugDatabase` 时只把 client 加入只读订阅列表。`.../AngelscriptDebugServer.cpp:828-895` 的 `Pause/Continue/StepIn/StepOver/StepOut` 直接修改全局暂停控制字段，没有检查 `Client` 是否在 `ClientsThatAreDebugging`。`.../AngelscriptDebugServer.cpp:897-913` 的 `StartDebugging` 会清空断点并把 client 加入 `ClientsThatAreDebugging`。`.../AngelscriptDebugServer.cpp:928-1068` 的 `ClearBreakpoints/SetBreakpoint/SetDataBreakpoints` 同样不做角色校验。`.../AngelscriptDebugServer.cpp:724-748` 则在“最后一个 debugging client 断开”时直接清空全局调试状态。 |
| 优点 | 单一 VS Code 前端路径足够直接，接入成本低；调试数据库订阅与真实调试 attach 至少在数据结构上已经被初步区分。 |
| 不足 | “能连接”不等于“有控制权”。当前 observer client 理论上也能发送控制消息；第二个 debugger client 会在 `StartDebugging` 时清空第一个 client 的断点；控制端掉线后的接管、只读镜像、显式 takeover 都没有合同，多前端场景只能依赖隐式时序。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 每个 websocket 连接都有独立 `V8InspectorChannelImpl` 与 `V8InspectorSession`；server 用 `Handle -> Channel` map 持有会话，在 `OnOpen()` 创建、`OnReceiveMessage()` 按连接路由、`OnClose()` 精确销毁。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:60-116`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:238-251`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:500-555` | 调试连接首先要有显式 session owner；控制消息必须先归属到某个连接/会话，而不是直接打到进程级全局状态。 |
| UnLua | 调试器所有权明确外置：文档要求用户在脚本里显式 `require("LuaPanda").start(...)` 或接入 `LuaHelper`，插件 runtime 只暴露 `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 这类本地调试原语。 | `Reference/UnLua/Docs/CN/Debugging.md:2-14`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:84-94` | 把“调试数据原语”和“谁拥有控制端”分开建模，避免任何能接触 transport 的 client 都天然拥有暂停/单步权限。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保持 `V2` 线协议兼容的前提下，为连接增加显式 `controller lease` 与 `observer` 角色，让“读”和“改”不再共享同一权限面。 |
| 具体步骤 | 1. 新增 `FDebugClientSession`，至少持有 `SessionId`、`Role(observer/controller)`、`ClientName`、`LeaseEpoch`、`LastSeenTime`；`FAngelscriptDebugServer` 的 socket 列表继续保留，但控制逻辑改为先查 session。 2. 将 `RequestDebugDatabase` 默认建模为 `observer` attach；新增或扩展 `StartDebugging` 语义为“申请 controller lease”，单机兼容路径下第一个旧 client 仍自动获得 lease。 3. 在 `HandleMessage()` 中对 `Pause/Continue/Step*`、`ClearBreakpoints/SetBreakpoint`、`SetDataBreakpoints`、`StopDebugging` 增加 `RequireControllerLease(Client)` 守卫；observer client 收到结构化 `Diagnostics` 拒绝，而不是静默修改全局状态。 4. 为 takeover 增加可选握手字段，如 `CanTakeOver`、`TakeOverSessionId`、`RequestedRole`；第一阶段没有这些字段的旧前端只在“当前无人持有 lease”时继续按老行为工作。 5. 将 `ClientsThatWantDebugDatabase`、`ClientsThatAreDebugging` 逐步收敛为 session role 派生视图；disconnect 时释放 lease，但不自动删除 observer 订阅。 6. 为后续多 IDE 前端增加最小 lease 日志，记录 `SessionId + ClientName + Role + RemoteAddress + TakeOverReason`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugSessionLease.*` |
| 预估工作量 | M |
| 架构风险 | 如果 lease 只做“字段存在”而不真正接管写操作校验，结果会变成又一层旁路状态；同时 takeover 语义若设计得过于激进，可能影响当前一机双窗口的临时调试习惯。 |
| 兼容性 | 可增量实施。现有单前端工作流保持不变；旧客户端在无人持有 lease 时仍可正常调试。变化主要体现在多 client 并存时行为从“未定义”变成“显式拒绝或显式接管”。 |
| 验证方式 | 1. 现有单客户端调试回归必须保持通过。 2. 新增双客户端集成测试：observer client 只订阅 `DebugDatabase`，对 `Pause/SetBreakpoint` 应收到拒绝诊断。 3. 新增双 debugger client 场景，验证第二个 client 在未 takeover 前不能清空第一个 client 的断点。 4. 断开 controller client 后验证 lease 被释放，observer client 仍可继续接收只读广播。 |

### Arch-DT69：`StaticJIT` 的显式请求集与隐式 `comprehensive` 虚调闭包不是同一份合同

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | StaticJIT 覆盖边界、虚调用 raw-JIT lane 安全性、fallback 策略可解释性 |
| 当前设计 | JIT 根集合只来自 `asIJITCompiler::CompileFunction()` 被动收到的编译请求，但代码生成阶段又无条件打开 `bAllowComprehensiveJIT`，对部分虚调用直接发射 `jitFunction_Raw` 路径；“哪些函数明确在生成集里”与“哪些调用点假定目标一定有 raw JIT”并不是同一份显式合同。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:1438-1446` 创建 `FAngelscriptStaticJIT` 并通过 `Engine->SetJITCompiler(StaticJIT)` 挂到编译流程；`.../AngelscriptEngine.cpp:1568-1579` 说明 `InitialCompile()` 之后才统一 `WriteOutputCode()`。`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:66-75` 的 `CompileFunction()` 只把收到回调的函数加入 `FunctionsToGenerate`。`.../AngelscriptStaticJIT.cpp:3472-3474` 在写出阶段又强制 `bAllowComprehensiveJIT = true`。`.../AngelscriptStaticJIT.cpp:3491-3497` 只对 `FunctionsToGenerate` 做分析和生成。`.../AngelscriptStaticJIT.cpp:3442-3449` 的 `IsFunctionAlwaysJIT()` 在 `bAllowComprehensiveJIT` 下把虚函数视为“总能 JIT”。`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp:1177-1194` 因此会在 `GenerateData == nullptr` 但 `IsFunctionAlwaysJIT()` 为真时走 `WriteDirectCall()`。`.../AngelscriptBytecodes.cpp:1453-1458` 最终直接发射 `CallRealFunction->jitFunction_Raw` 调用，而不是先做 runtime probe 或退回 `WriteDynamicCall()`。 |
| 优点 | 当虚调目标的 raw JIT 确实齐全时，这条 lane 能绕过解释器上下文重建，比统一 `DynamicCall` 更高效。 |
| 不足 | 但源码里没有显式 closure plan 或校验层来证明这个前提始终成立。按源码推断，是否“所有潜在虚调目标都已拥有 `jitFunction_Raw`”依赖 AngelScript 编译请求恰好覆盖到它们；一旦覆盖不完整，当前设计也没有把“为什么仍敢走 raw lane”记录为 machine-readable 事实。这里的风险不是一定出错，而是覆盖与 fallback 语义并不自描述。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | IntelliSense commandlet 先显式拿 `GetExportedReflectedClasses()`、`GetExportedNonReflectedClasses()`、`GetExportedEnums()`、`GetExportedFunctions()` 作为导出集合，只有命令行 `BP=1` 时才追加 Blueprint slice。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-112` | 即使不是 JIT，producer 也先明确“输入闭包是什么”，而不是把闭包隐含在运行时回调里。 |
| puerts | `ParamDefaultValueMetas` 作为独立 `ScriptGenerator` producer，先用 `ShouldExportClassesForModule()` 明确范围，再通过 `Initialize()`/`ExportClass()`/`FinishExport()` 走完整生命周期。 | `Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:46-62`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:64-129` | 生成器若依赖某个 transitive set，最好把集合和生命周期显式化，方便校验、诊断和增量演进。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 为 `StaticJIT` 增加显式 `closure plan` 与 guarded raw-call lane，把“显式根集合”和“隐式虚调假设”收敛到同一份可验证合同。 |
| 具体步骤 | 1. 新增 `FJitClosureRecord` / `AngelscriptJitClosurePlan.json`，至少记录 `FunctionId`、`ClosureKind(RequestedRoot/ExpandedVirtualTarget/ImportedBinding)`、`CallsiteMode(DirectSymbol/VirtualRaw/DynamicFallback)`、`Reason`。 2. 在 `WriteOutputCode()` 前增加 pre-pass：对每个待生成函数扫描虚调/导入调度位点，显式写出“依赖哪些目标必须拥有 raw JIT”；第一阶段先只落 sidecar，不改现有产物命名。 3. 将 `AngelscriptBytecodes.cpp:1453-1458` 的 raw 调用改为 guarded lane：先检查 `CallRealFunction->jitFunction_Raw != nullptr`，命中时走 raw call，缺失时退回现有 `WriteDynamicCall()` 语义；这样 comprehensive 模式从“隐式硬假设”升级为“有 runtime guard 的快路径”。 4. 在生成或验证阶段统计 closure miss，并把 miss reason 回写到 JIT manifest/diagnostics，区分 `not-requested`、`target-not-generated`、`target-has-no-raw-entry`。 5. 后续再让 `PrecompiledValidate` 或相关自动化读取同一份 closure plan，对“哪些调用点依赖 comprehensive raw lane”做专门回归，而不是只看整包 `PrecompiledDataGuid`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptJitClosurePlan.*` |
| 预估工作量 | M |
| 架构风险 | 给 hot virtual call 增加 runtime guard 会带来一条额外分支；如果只加 guard 不加 closure diagnostics，可能把真实覆盖缺口静默变成性能退化，因此必须同时补 plan 与日志。 |
| 兼容性 | 可增量实施。现有 `CompileFunction()` 根集合、`AS_JITTED_CODE` 目录和 `PrecompiledScript.Cache` 路径都不需要改变；第一阶段只是新增 sidecar 与 raw-lane guard。对脚本语义应保持兼容，最多把部分原本“隐式假定可 raw-call”的路径安全降级到 dynamic fallback。 |
| 验证方式 | 1. 构造一个虚调目标未进入 `FunctionsToGenerate` 的场景，验证生成代码不会盲目直跳 `jitFunction_Raw`，而会回落到 `DynamicCall`。 2. 对一个已经完整覆盖的虚调调用，验证仍命中 raw lane，且 closure plan 标记为 `ExpandedVirtualTarget`。 3. 对同一轮生成同时检查 `FunctionsToGenerate`、closure plan 与最终 `JIT manifest`，确认三者能解释“为什么某调用点是 raw/direct/fallback”。 4. 回归现有 precompiled/JIT 运行流程，确认未消费新 sidecar 的旧路径依然可运行。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT68 | 多前端调试控制权、observer/controller 隔离、lease 治理 | 会话治理新增 + 权限面收敛 | 高 |
| P1 | Arch-DT69 | StaticJIT 覆盖边界、comprehensive 虚调闭包、guarded fallback | 覆盖合同新增 + 代码生成安全降级 | 高 |

---

## 架构分析 (2026-04-10 01:17)

### Arch-DT70：`HasStopped` 仍是自由文本三元组，停止原因模型无法承载 data breakpoint / exception / filter 的结构化扩展

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | 调试停止事件合同、DAP/CDP bridge 可映射性、多前端诊断一致性 |
| 当前设计 | 当前所有停止事件都复用同一个 `EDebugMessageType::HasStopped`，payload 只有 `FStoppedMessage { Reason, Description, Text }` 三个字符串；异常、data breakpoint、手动暂停、单步、普通断点都被压成这一个自由文本模型。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:25-43` 只有单一 `HasStopped/HasContinued` 消息号；`.../AngelscriptDebugServer.h:129-140` 的 `FStoppedMessage` 只定义 `Reason/Description/Text` 三个字段。`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:455-462` 在脚本异常时只写 `Reason="exception"` 和异常文本；`.../AngelscriptDebugServer.cpp:533-539` 在 data breakpoint 触发时同样写 `Reason="exception"`，只是把描述塞进 `Text`；`.../AngelscriptDebugServer.cpp:623-626` 的普通断点和单步只写 `Reason="breakpoint"` 或 `Reason="step"`；`.../AngelscriptDebugServer.cpp:667-698` 的 `PauseExecution()` 对所有停止路径都广播同一种 `HasStopped`，恢复时也只发通用 `HasContinued`。 |
| 优点 | 现有 VS Code 前端接入简单，停止事件序列化成本低，旧客户端几乎不需要理解额外 schema。 |
| 不足 | stop reason 是“显示字符串”而不是“机器可读语义”：data breakpoint 与真正异常在 wire 上都叫 `exception`；命中的 breakpoint id、watchpoint id、break filter、source location、是否可继续、stop sequence 等都不存在。后续若要接 DAP/CDP bridge，多前端只能在桥接层猜测停止原因，而不是稳定映射。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | 插件并不重新定义一套“停止事件三元组”，而是把前端协议文本直接送进 `V8InspectorSession`，再把 `sendResponse()/sendNotification()` 产出的文本消息原样经 websocket 发回；同时通过 `/json/list`、`/json/version` 暴露标准 target 发现入口。按源码推断，`paused`/`continued`/exception reason 等停止语义由 CDP/V8 inspector 负责，而不是由 UE 插件再做二次压缩。 | `Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:104-117`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:150-157`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:330-345`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:452-477`；`Reference/puerts/unreal/Puerts/Source/JsEnv/Private/V8InspectorImpl.cpp:529-539` | 先把停止事件建模成结构化协议域，再让 transport 只做转发，这样前端桥接不需要倒推 `Text` 字符串。 |
| UnLua | 调试接入明确依赖外部 `LuaPanda` / `LuaHelper`，文档要求用户在脚本里显式 `require("LuaPanda").start(...)`；插件 runtime 自己只暴露 `GetStackVariables()`、`GetLuaCallStack()`、`PrintCallStack()` 这类调试原语，不把 IDE 停止事件 schema 硬编码进插件协议。 | `Reference/UnLua/Docs/CN/Debugging.md:2-14`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Public/UnLuaDebugBase.h:75-94`；`Reference/UnLua/Plugins/UnLua/Source/UnLua/Private/UnLuaDebugBase.cpp:614-722` | 先把 runtime debug primitive 和 frontend stop/event schema 分离，插件核心只提供事实，不把 IDE 事件形状写死。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在保留 legacy `HasStopped + FStoppedMessage` 的前提下，增量加入结构化 `StopEvent` 合同，把停止原因从自由文本升级为机器可读语义。 |
| 具体步骤 | 1. 新增 `Debugging/Protocol/FAngelscriptStopEvent`，字段至少包含 `StopId`、`StopKind(step/breakpoint/exception/dataBreakpoint/manualPause/filter)`、`PrimaryMessage`、`HitBreakpointIds`、`HitDataBreakpointIds`、`BreakFilters`、`SourceUri/ModuleName/Line`、`bCanContinue`。 2. 在 `ProcessException()`、data breakpoint 命中分支、普通断点/单步分支里先构造 `FAngelscriptStopEvent`，再由一个 `LegacyStopSerializer` 映射回当前 `FStoppedMessage`；旧客户端继续消费 `Reason/Text`。 3. 将 `PauseExecution()` 的广播路径扩成“先发结构化 stop，再按 capability 决定是否同时发 legacy stop”；`HasContinued` 追加可选 `StopId`，让 bridge 能知道恢复的是哪一次停止。 4. 为后续 `DAP/CDP bridge` 预留适配层：`breakpoint` 映射命中断点数组，`dataBreakpoint` 映射 watchpoint，`exception` 单独保留异常类型和文本，不再复用字符串常量。 5. 把 transcript 级测试补到 stop reason 维度，覆盖 exception、data breakpoint、manual pause、step、普通断点五条路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/Protocol/AngelscriptStopEvent.*`，联动 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/Debugger/` |
| 预估工作量 | M |
| 架构风险 | 如果第一阶段直接让旧 `HasStopped` 退役，现有 VS Code 客户端会立即失配；必须先做双轨输出，让结构化 stop 只在 capability 握手后启用。 |
| 兼容性 | 完全可增量实施。legacy `FStoppedMessage` 与 `HasContinued` 可保持原样，新前端才读取 `StopEvent`；旧脚本与现有前端不需要同步升级。 |
| 验证方式 | 1. 回归现有单客户端调试流程，确认旧 `HasStopped` 字段不变。 2. 新增 exception/data breakpoint transcript 测试，验证两者在结构化 stop 中不再同为 `exception`。 3. 接一个最小 fake bridge，把结构化 stop 映射到 DAP/CDP 样式对象，确认不需要解析 `Text` 文本。 4. 断点恢复后核对 `StopId` 与 `HasContinued` 对应关系，避免多前端并发时 stop/continue 串台。 |

### Arch-DT71：UHT 已有私有 `FunctionSignatureBuilder`，但 `Docs` / `GoToDefinition` / 报表仍在 overload 粒度丢失函数身份

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | overload identity、工具链共享签名合同、导航与覆盖报表的 join key |
| 当前设计 | 当前插件内部已经存在一套较完整的 UHT 函数签名构造器，但它只用于生成 `ERASE_*` macro；与此同时，`Docs` 只生成展示用 declaration 文本，`GoToDefinition` 请求只带 `TypeName + SymbolName`，`AS_FunctionTable_Entries.csv` / summary 也只落 `ClassName/FunctionName/EntryKind`。同名 overload 在多条产线之间没有共享的 canonical signature key。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h:453-462` 的 `FAngelscriptGoToDefinition` 只有 `TypeName` 与 `SymbolName`；`.../AngelscriptDebugServer.cpp:1297-1315` 对 namespace/global function 仅按 `GetName()` 线性查找第一个匹配项，`.../AngelscriptDebugServer.cpp:1319-1330` 对类型方法也只按名字找第一个 `Method`。`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp:352-404` 会把函数 declaration 作为展示文本生成，且 `.../AngelscriptDocs.cpp:358-360` 主动把 `&inout` 归一成 `&`、把 `@` 去掉；`.../AngelscriptDocs.cpp:416-430` 与 `:442-470` 还有另一套 `GetDecl()`/`AddFunction()` 类型格式化逻辑。相对地，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:8-38`、`.../AngelscriptFunctionSignatureBuilder.cs:43-100`、`.../AngelscriptFunctionSignatureBuilder.cs:109-127` 已能构造 `OwningType + FunctionName + ReturnType + ParameterTypes + IsStatic/IsConst` 的签名模型；但 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:37-44`、`.../AngelscriptFunctionTableCodeGenerator.cs:128-135`、`.../AngelscriptFunctionTableCodeGenerator.cs:244-264` 落盘时又只保留 `ClassName, FunctionName, EntryKind, EraseMacro, ShardIndex`。 |
| 优点 | 现有实现简单，展示文本、导航请求、UHT 直绑宏各自都能独立工作，不需要先引入额外 schema。 |
| 不足 | overload identity 在边界处被压扁了：导航只能“按名字取第一个匹配”，报表不能稳定区分同名不同签名函数，未来若把 JIT 覆盖率、`BindingCoverage`、docs、definition resolver 对账到同一符号，也没有可复用的签名主键。更糟的是三条产线的文本归一规则并不一致，`@`、`&inout`、`const`、property decl 的处理都各自为政。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| UnLua | `UnLua::IntelliSense` 把 `Get(const UField*)`、`Get(const FProperty*)`、`GetTypeName(...)` 做成共享格式化核心；Editor 常驻生成器与 commandlet 都复用这同一组 helper 生成符号文本和文件名，而不是每个 driver 自己拼 declaration。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:42-53`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:55-123`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:254-279`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:300-329`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/Commandlets/UnLuaIntelliSenseCommandlet.cpp:55-99` | 先抽一层 authoritative formatter / symbol text builder，再让不同 driver 复用，才能避免同一符号在多条产线里被不同规则重新命名。 |
| puerts | `ParamDefaultValueMetas` 作为独立 `ScriptGenerator` producer，把“是否导出”“如何遍历类”“何时写盘”收敛到单一生命周期里，避免不同消费者各自重建一套生成事实。 | `Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:29-55`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:64-129` | 即使短期不做完整 `symbol graph`，也应先让签名与导出决策有一个单一 owner，而不是散落在 docs/navigation/UHT 多处。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 把 `AngelscriptFunctionSignatureBuilder` 提升为跨 `Docs` / `Navigation` / `UHT report` / 后续 `JIT manifest` 共享的 `CanonicalSignature` 服务，并给 legacy 文本输出附加稳定 `SignatureKey`。 |
| 具体步骤 | 1. 新增 `Toolchain/AngelscriptCanonicalSignature.*`（Runtime）与 `AngelscriptCanonicalSignature.cs`（UHT）共享 schema，字段至少包含 `OwningType`、`FunctionName`、`ParameterTypes`、`ReturnType`、`IsStatic`、`IsConst`、`DisplayDeclaration`、`SignatureKey`；第一阶段可以先复用 `AngelscriptFunctionSignatureBuilder` 现有字段，不改 `ERASE_*` 宏生成规则。 2. 扩展 `FAngelscriptGoToDefinition` 新请求或并列 `ResolveDefinition` 消息，使其可选携带 `SignatureKey` / `DisplayDeclaration`；server 解析时先按 `SignatureKey` 匹配 overload，再回退旧的 `TypeName + SymbolName`。 3. 让 `FAngelscriptDocs::DumpDocumentation()` 在保留现有 declaration 文本的同时，把每个函数的 `SignatureKey` 和原始 canonical signature 写入 sidecar 或内存索引；展示文本仍可继续做 `@`、`&inout` 的可读性格式化，但不再充当 join key。 4. 让 `AS_FunctionTable_Entries.csv` / summary JSON 追加 `SignatureKey`、`CanonicalParameterTypes`、`CanonicalReturnType` 字段；旧列顺序保留，新列追加即可。 5. 第二阶段再让后续 `BindingCoverage` / `JIT coverage manifest` / docs index 统一使用同一个 `SignatureKey`，把“能绑定”“能导航”“能 JIT”收敛到 overload 粒度。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptDocs.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Toolchain/AngelscriptCanonicalSignature.*`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`，`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 预估工作量 | M |
| 架构风险 | `SignatureKey` 规则一旦定义不稳，会在默认参数、`const`、接口函数或 header fallback 上产生漂移；必须先冻结 canonical 规则，再逐步让更多消费者接入。 |
| 兼容性 | 可增量实施。旧 `GoToDefinition(TypeName, SymbolName)`、现有 docs 文本和 CSV 列都可以保留；新加的是附加 key 和可选字段，旧前端忽略即可。 |
| 验证方式 | 1. 构造同类同名不同参数的 overload，验证新的 `SignatureKey` 能稳定区分，definition resolver 不再命中“第一个同名函数”。 2. 比对 `Docs` sidecar、`AS_FunctionTable_Entries.csv` 与 runtime resolver，确认同一 overload 的 `SignatureKey` 一致。 3. 回归旧 `GoToDefinition` 客户端，确认未携带 `SignatureKey` 时仍按现有行为工作。 4. 为后续 JIT/coverage 试点接一个只读 consumer，验证无需再解析 declaration 文本就能按 overload 粒度 join。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT70 | 停止事件 stop reason 合同、exception/data breakpoint 可区分性、桥接标准前端 | 协议模型新增 + legacy adapter 保留 | 高 |
| P1 | Arch-DT71 | overload 粒度函数身份、共享 canonical signature、导航/报表/JIT join key | 事实源收敛 + key 合同新增 | 高 |

---

## 架构分析 (2026-04-10 01:27)

### Arch-DT72：`StaticJIT` 把字节码分析、lowering 决策与 `C++` 文本发射耦合在同一上下文里，缺少可复用的 `plan/IR` 层

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | `StaticJIT` 可扩展性、替代 backend 演进、validate/analysis lane 复用 |
| 当前设计 | `FStaticJITContext` 同时承担 bytecode 扫描、stack/jump 分析、lowering 选择和 `C++` 文本累积；`GenerateCppCode()` 在同一轮遍历里直接让每条 `FAngelscriptBytecode::Implement(Context)` 写 `Line(...)`，最后 `WriteOutFunction()` 把字符串数组直接刷进 `FJITFile::Content`。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h:57-180` 定义 `FStaticJITContext`，同时持有 `Instructions`、`FunctionHead/FunctionContent/FunctionFoot`、`LocalVariables`、`StateVars`；`.../AngelscriptStaticJIT.h:319-360` 的 `Line()`/`Format()` 明确把输出模型定义成字符串行缓冲。`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:304-307` 直接把文本塞进 `FunctionContent`；`.../AngelscriptStaticJIT.cpp:3204-3337` 在解析 bytecode、做 `PrePass`/stack offset 之后，立刻调用 `Bytecode.Implement(Context)` 并 `check(bImplemented)`；`.../AngelscriptStaticJIT.cpp:1500-1526` 的 `WriteOutFunction()` 只按顺序吐出这些字符串；`.../AngelscriptStaticJIT.cpp:3470-3608` 再把函数文本、extern declaration 和 module 头拼成最终 `*.jit.cpp`。 |
| 优点 | 当前路径很直接，lowering 逻辑和最终输出没有二次映射损耗；对现有单一 `C++` emitter 来说，调试一个 opcode 时只需要看一个上下文对象。 |
| 不足 | 但这意味着“分析结果”不会独立存在。后续若要增加 `validate-only`、`coverage-only`、第二种 emitter、平台特化 backend，或仅仅想让 CI 看到“某个调用点为什么走 `DirectCall/DynamicCall`”，都得继续嵌进 `Context.Line()` 与 `Bytecode.Implement()` 的副作用里，而不是复用一个稳定的中间合同。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `ParamDefaultValueMetas` 先通过 `ShouldExportClassesForModule()`、`Initialize()`、`ExportClass()` 收集语义事实，把生成内容累积在 `GeneratedFileContent`，最后才在 `FinishExport()` 统一落盘。 | `Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:46-55`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:64-129` | 即使最终仍输出文本，也先把“收集/决策”和“写文件”拆成显式生命周期，后续更容易加验证、替代 emitter 和额外 sidecar。 |
| UnLua | `UnLua::IntelliSense::Get(...)` 系列函数负责 authoritative 文本构造与 escaping，`FUnLuaIntelliSenseGenerator::Export()` 只决定 `ModuleName/FileName`，`SaveFile()` 统一处理持久化与 changed-only 写盘。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:29-35`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:42-53`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-233` | 先抽“语义到表示”的 owner，再让 generator 只做 driver/persistence，能显著降低每次新增输出视图时对核心遍历逻辑的侵入。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 在不改变现有 `AS_JITTED_CODE` 输出的前提下，先引入内部 `JitPlan` 层，把“分析/lowering 事实”从 `C++` 文本发射中解耦出来。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/Plan/AngelscriptJitPlan.*`，定义 `FStaticJITFunctionPlan`、`FStaticJITInstructionPlan`、`FStaticJITLoweringRecord`，字段至少包含 `FunctionId`、`Opcode`、`StackOffsetBefore`、`JumpTargets`、`SelectedCallMode`、`ReferencedFunction/Type/Property`、`DebugLine`。 2. 将 `GenerateCppCode()` 拆成 `BuildFunctionPlan()` 与 `EmitFunctionCpp()` 两步；第一阶段允许 `InstructionPlan` 暂时持有过渡性的 `EmittedLines`，但 plan owner 必须先独立出来。 3. 新增 `StaticJIT/Emitters/AngelscriptStaticJitCppEmitter.*`，把 today `WriteOutFunction()`、`File->Content.Add(...)` 的逻辑迁入 emitter；要求第一阶段对现有 `*.jit.cpp` 产物做到 golden diff 一致。 4. 让 `WriteOutputCode()` 在保留现有文件名和目录的同时，额外写出轻量 `AngelscriptJitPlanSummary.json` 或并列 manifest，至少能回答每个 `FunctionId` 的 lowering/fallback 选择。 5. 第二阶段再允许 `ValidateOnlyEmitter`、`CoverageEmitter` 或平台扩展 emitter 读取同一 `JitPlan`，而不是继续把新需求加进 `Context.Line()` 副作用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/Plan/AngelscriptJitPlan.*`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/Emitters/AngelscriptStaticJitCppEmitter.*` |
| 预估工作量 | L |
| 架构风险 | 若第一阶段同时重写所有 opcode emitter，回归面会过大；更稳妥的路径是先让 `JitPlan` 成为单一 owner，再通过 golden diff 逐步迁出 `WriteOutFunction()`。 |
| 兼容性 | 对现有脚本与 JIT 产物消费者可以保持兼容。第一阶段不改 `AngelscriptJitInfo.jit.cpp`、`*.as.jit.h`、`AngelscriptJitCode_*.jit.cpp` 的对外形状，只新增内部 owner 和附加 summary。 |
| 验证方式 | 1. 对改造前后 `AS_JITTED_CODE` 做逐文件 golden diff，确认 `CppEmitter` 输出稳定。 2. 新增最小 `JitPlan` 单测，覆盖一个 `DirectCall` 和一个 `DynamicCall` 的 lowering 记录。 3. 在不生成 `C++` 文件的 `validate-only` 测试模式下跑一次 plan 构建，验证可以产出函数级摘要而不触发 `SaveStringToFile()`。 4. 回归现有 precompiled/JIT 运行流程，确认 `JitPlan` 引入后 runtime fallback 行为不变。 |

### Arch-DT73：`ComputedOffsets/SharedHeaders` 把 ABI/layout 事实埋进生成头文件，缺少可复用的 machine-readable layout sidecar

**当前架构现状**

| 项目 | 内容 |
|------|------|
| 关注点 | JIT layout 合同、ABI 可诊断性、toolchain 复用 |
| 当前设计 | `GetComputedOffsets()` 递归推导 type/property 的 size、alignment 与 offset；一旦不能完全 hardcode，就把表达式写进 `SharedHeaders` 的 `constexpr SIZE_T ...` 文本里，并在 `ComputedPropertyOffset` 中只保留变量名。运行时 `PrecompiledData` 再通过 `FJITDatabase` 的 lookup/verify 数组回填或校验这些布局，但没有对应的显式 sidecar。 |
| 源码证据 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h:431-472` 定义 `FComputedPropertyOffset`、`FComputedTypeOffsets` 与 `FSharedHeader`，其中 `ComputedOffsetVariable/ComputedAlignmentVariable` 只是文本符号名。`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp:2818-3197` 的 `GetComputedOffsets()` 会递归生成 `sizeof(...)`/`alignof(...)`/`Align(...)` 表达式，并把 `constexpr SIZE_T ...` 直接追加到 `Header->Content`；`.../AngelscriptStaticJIT.cpp:3526-3578` 再把这些 shared header 写到磁盘。与此同时，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp:81-101` 只把 `PropertyOffsetLookups` 与 `VerifyPropertyOffsets/VerifyTypeSizes/VerifyTypeAlignments` 挂进全局 `FJITDatabase`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp:2429-2477` 在 finalize 时要么原地回填 offset，要么在验证宏开启时 `checkf`。 |
| 优点 | 这种做法对 `C++` 编译器友好，`constexpr` 表达式天然能复用宿主编译器的布局规则，也避免了再维护一套自定义 layout 求值器。 |
| 不足 | 但 layout 真相只存在于“生成头文件文本 + 运行时临时数组”两处。CI、IDE、`DebugDatabase`、后续 `JitManifest` 或 review 工具都无法直接读取“这个 type 的 size/align/property offset 是 hardcoded 还是 computed、依赖了哪些 shared header、哪条 property 触发了 fallback”；一旦 packaged profile 只剩 `checkf` 或整包降级，也很难把失败精确映射回 toolchain 产物。 |

**参考插件对比**

| 参考插件 | 做法 | 源码位置 | 可借鉴点 |
|----------|------|----------|---------|
| puerts | `ParamDefaultValueMetas` 把默认参数元数据显式积累到 `GeneratedFileContent`，并在 `FinishExport()` 写出独立 `InitParamDefaultMetas.inl`；元数据不是只藏在运行时对象或编译后副作用里。 | `Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:53-62`；`Reference/puerts/unreal/Puerts/Source/ParamDefaultValueMetas/Private/ParamDefaultValueMetasModule.cpp:80-129` | 即使最终消费方仍是编译后的代码，也值得同步落一份可检查、可对账的显式 artifact。 |
| UnLua | `IntelliSense::Get(...)` 先生成每个 type 的文本表示，generator 再按 `ModuleName/FileName.lua` 独立持久化，并通过 `SaveFile()/DeleteFile()` 管理 freshness；语义事实不会只留在进程内临时结构。 | `Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:55-72`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSense.cpp:75-119`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:148-166`；`Reference/UnLua/Plugins/UnLua/Source/UnLuaEditor/Private/UnLuaIntelliSenseGenerator.cpp:222-245` | 结构化事实与最终文本文件可以并存；这样别的工具链环节不必反向解析 generated code 才能复用知识。 |

**改进方案**

| 项目 | 内容 |
|------|------|
| 改进策略 | 保留现有 `constexpr` header 方案，但并列产出一份 `layout facts` sidecar，把 JIT 依赖的 ABI/layout 事实转成可读、可对账、可复用的工具链产物。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/Layout/AngelscriptJitLayoutFacts.*`，定义 `FJitTypeLayoutFact`、`FJitPropertyLayoutFact`，字段至少包含 `TypeRef/PropertyRef`、`TypeName`、`LayoutKind(hardcoded/computed)`、`SizeExpr`、`AlignmentExpr`、`OffsetExpr/OffsetValue`、`ContainingSharedHeader`、`AdditionalHeaders`、`DependsOnTypes`。 2. 在 `GetComputedOffsets()` 内部并列填充这些 fact record；现有 `Header->Content.Add(...)` 保持不变，但不再让 `ComputedOffsetVariable` 成为唯一可见输出。 3. 在 `WriteOutputCode()` 结束时写出 `AS_JITTED_CODE/AngelscriptJitLayoutFacts.json` 或按 shared header 分片的 sidecar；内容变化检测可复用现有 changed-only 写盘策略。 4. 将 `PrecompiledData.cpp:2429-2477` 的 finalize/verify 结果回写到 manifest 或日志时，优先引用 `TypeRef/PropertyRef -> layout fact`，让失败可以精确定位到哪个 type/property/header dependency。 5. 第二阶段再让 `SymbolGraph`、`JitManifest` 或 review/CI 工具读取同一份 layout facts，回答“为什么这个 type 只能 computed、为什么某 property offset 在 packaged profile 被禁用”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，新增 `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/Layout/AngelscriptJitLayoutFacts.*` |
| 预估工作量 | M |
| 架构风险 | 主要风险是 layout fact 的稳定 key 设计不当，导致跨轮构建无法对账；第一阶段应优先复用已有 `TypeRef/PropertyRef`，避免另起一套脆弱的字符串主键。 |
| 兼容性 | 完全可增量实施。现有 shared header、`FJITDatabase` 回填和 runtime 语义都不需要改变；新增 sidecar 只是让布局事实变得可消费。 |
| 验证方式 | 1. 对同一批类型同时检查 generated shared header 与 `AngelscriptJitLayoutFacts.json`，确认 `SizeExpr/OffsetExpr` 一一对应。 2. 人为制造一个 property layout 变化，验证 packaged/development 两种路径都能把失败定位到具体 `TypeName/PropertyName`，而不只是整包禁用。 3. 回归现有 `PrecompiledData` finalize 流程，确认缺少 sidecar 时仍按旧逻辑工作。 4. 让一个只读工具读取 layout facts，验证无需解析 `*.as.jit.h` 也能回答某 type/property 的布局来源。 |

### 本轮汇总

| 优先级 | Arch 编号 | 关注点 | 改进类型 | 预估收益 |
|--------|----------|--------|---------|---------|
| P1 | Arch-DT72 | `StaticJIT` 缺少 `plan/IR` 层、难以扩展 validate/alt-emitter/backend | 内部 owner 重组 + emitter 分层 | 高 |
| P2 | Arch-DT73 | layout/ABI 事实只藏在 shared header 与 runtime lookup 中 | 新增 sidecar + 诊断事实外显 | 中 |
