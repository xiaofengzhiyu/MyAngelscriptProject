# DebuggingAndJIT 发现与方案

---

## 发现与方案 (2026-04-08 12:34)

### Issue-1：JIT virtual dispatch 在 callee 缺失 JIT entry 时会直接调用空 `jitFunction_Raw`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:1163-1194, 1448-1458`；`AngelscriptStaticJIT.cpp:3415-3449, 3470-3475`；`PrecompiledData.cpp:527-540`；`as_scriptfunction.h:361-364`；`ASClass.cpp:1647-1665, 1682-1690` |
| 问题 | `FCallScriptFunction::MakeCall()` 对 virtual method 在 `RealFunction == nullptr && IsFunctionAlwaysJIT(ScriptFunction)` 时会进入 `WriteDirectCall(Context, nullptr, nullptr)`。而 `WriteOutputCode()` 在生成期把 `bAllowComprehensiveJIT` 固定为 `true`，`IsFunctionAlwaysJIT()` 因此会把所有 virtual method 视为“总能直接走 JIT”。`WriteDirectCall()` 随后生成 `CallRealFunction = CallObjectType->virtualFunctionTable[vfTableIdx]` 并立刻调用 `CallRealFunction->jitFunction_Raw`，没有任何 null/bounds/fallback 检查。与之相对，运行时加载 precompiled function 时只有在 `FJITDatabase::Functions` 命中该函数 id 时才会写入 `jitFunction_Raw`；否则该字段保持 `0`，源码定义也明确默认值为 `0`。同仓 `ASClass.cpp` 的 virtual 调用包装器已经体现出正确预期：它会先检查 `RealFunction->jitFunction_Raw`，为空时回退到 `FAngelscriptGameThreadContext` 执行。 |
| 根因 | StaticJIT 的 virtual call codegen 把“生成期希望 comprehensive JIT 覆盖”误实现成“运行期无条件假设 virtual callee 一定有 raw JIT entry”，没有复用解释器或现有 ClassGenerator 的 null-guard + VM fallback 语义。 |
| 影响 | 只要出现部分 JIT bundle、丢失单个 callee entry、GUID 匹配但函数级 JIT 覆盖不完整，或者后续演进让某个 override 回退到 VM，这条 virtual dispatch 就会直接调用空函数指针，结果不是安全 fallback，而是崩溃或未定义行为。它还让“caller 已 JIT、callee 未 JIT”这一最常见的渐进覆盖场景无法成立。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 virtual dispatch 生成代码改成“先解析真实函数，再按 `jitFunction_Raw != nullptr` 选择 JIT 或 VM fallback”，彻底移除对 comprehensive coverage 的运行期假设。 |
| 具体步骤 | 1. 在 `StaticJIT/AngelscriptBytecodes.cpp` 为 virtual call 提取一个统一 helper，先校验 `vfTableIdx` 边界、`CallRealFunction != nullptr`，再判断 `CallRealFunction->jitFunction_Raw` 是否可用。 2. 若 raw JIT entry 存在，保留当前 direct raw call；若不存在，则复用现有 `WriteDynamicCall()` 路径，按真实 `CallRealFunction` 构造 `FAngelscriptContext` 执行，保证与 `ASClass.cpp` 的 fallback 语义一致。 3. 对空对象、越界 vtable slot、空 `CallRealFunction` 的分支补齐与解释器一致的异常行为，至少对齐 `as_context.cpp:946-962` 的 null/invalid virtual dispatch 处理，而不是直接解引用。 4. 在 `PrecompiledData` 侧补一个显式统计或日志，当 virtual callee 没有 JIT entry 而触发 VM fallback 时输出一次可追踪信号，避免再次静默退化。 5. 添加自动化：构造一个 JIT caller 调 virtual override 的用例，并人为让 callee 不注册到 `FJITDatabase`，验证调用仍返回正确结果且不会崩溃。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 预估工作量 | M |
| 风险 | 改动 virtual call codegen 会影响大量脚本调用路径；若 fallback 分支没有完整复制参数封送逻辑，可能引入返回值或 reference 参数回归。 |
| 前置依赖 | 需要先明确测试环境能在非 `EditorContext` 下执行真实 JIT 代码；否则至少先补一个可在当前 harness 下验证 codegen 文本或 fallback 选择的测试。 |
| 验证方式 | 1. 新增回归测试覆盖“caller JIT、callee VM fallback”的 virtual override 场景。 2. 运行一组已有 precompiled/JIT 测试，确认完整覆盖函数路径仍走 raw JIT。 3. 人工验证缺失 callee JIT entry 时不再崩溃，并能得到与解释执行一致的返回值/异常。 |

### Issue-2：`asBC_CALLBND` 在生成期静态绑定 imported function，JIT 代码不会观察运行期 rebind / unbind

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:1169-1194, 1485-1504, 3803-3812`；`AngelscriptStaticJIT.cpp:3424-3433, 3470-3475`；`AngelscriptEngine.cpp:4616-4643` |
| 问题 | `asBC_CALLBND` 明确把 imported call 建模为 `LookupType::BindIdArg`，`WriteDynamicCall()` 也为这种调用生成了正确的运行时逻辑：每次执行都重新读取 `importedFunctions[..]->boundFunctionId`，若当前未绑定则抛 `SCRIPT_UNBOUND_EXCEPTION()`。但 `FCallScriptFunction::MakeCall()` 在进入 `WriteDynamicCall()` 之前，会先调用 `Context.JIT->DevirtualizeFunction(ScriptFunction)`；而 `DevirtualizeFunction()` 对 `asFUNC_IMPORTED` 且 `bAllowDevirtualize=true` 的情况，会直接把 imported signature 替换成“生成期当前绑定的真实函数”。`WriteOutputCode()` 又把 `bAllowDevirtualize` 固定成 `true`。结果是只要 imported function 在生成期刚好绑定到了一个可 JIT 的目标，codegen 就会走 direct call，把 `boundFunctionId` 的运行时查询整条短路掉。另一方面，运行期脚本引擎会在模块装载/热重载时持续 `UnbindImportedFunction()` / `BindImportedFunction()` 更新这些 imported binding。 |
| 根因 | StaticJIT 把 imported call 和普通 script call 复用了同一套“尽量早 devirtualize”策略，却忽略了 imported binding 本身就是运行期可变状态，语义上必须保留每次调用时的动态查表。 |
| 影响 | 一旦 imported function 在 JIT 产物生成之后被重新绑定到新实现，或在模块切换时被临时解绑，已生成的 JIT caller 仍会继续调用旧目标，甚至绕过本应抛出的 `unbound function` 异常。这会把模块热重载、依赖模块替换和运行期脚本切换变成行为分叉点：解释执行看到的是新绑定，JIT 执行看到的却是旧绑定。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 对 `LookupType::BindIdArg` 禁用生成期 devirtualization，强制保留运行期 `boundFunctionId` 查表语义。 |
| 具体步骤 | 1. 在 `FCallScriptFunction::MakeCall()` 中增加条件：当 `LookupType == ELookupType::BindIdArg` 时，直接跳过 `DevirtualizeFunction()`，始终走 `WriteDynamicCall()`。 2. 如果仍希望保留优化空间，最多只缓存“signature function 引用”，不能缓存当前 `boundFunctionId` 的解析结果。 3. 为 imported call 单独补一个 helper，确保 `boundFunctionId == -1` 时稳定抛 `SCRIPT_UNBOUND_EXCEPTION()`，并与解释器 `as_context.cpp` 的导入函数行为对齐。 4. 增加回归测试：先让 imported function 绑定到实现 A，生成/加载 JIT caller，再把同一 imported function 重新绑定到实现 B 或显式解绑，验证 JIT caller 立即观察到新绑定或 unbound 异常。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` 或新增 JIT 运行时集成测试文件 |
| 预估工作量 | M |
| 风险 | 如果现有生成器或运行时暗含“imported call 在 packaged 模式下永不变化”的假设，禁用这条优化后可能暴露一些此前被掩盖的模块依赖问题；同时需要确认性能回退是否可接受。 |
| 前置依赖 | 需要一个能在测试中显式触发 imported rebind / unbind 的 fixture，最好复用现有模块编译/切换能力。 |
| 验证方式 | 1. 新增 imported call rebind/unbind 回归测试，分别比对解释器与 JIT 结果。 2. 验证未重新绑定时仍能正确执行，不引入功能回归。 3. 人工在模块热重载后对比 JIT caller 与 VM caller 的实际调用目标是否一致。 |

### Issue-3：脚本调用目标解析在解释器、StaticJIT 与 `ASClass` wrapper 中各写一套，调用语义已经发生结构性漂移

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `as_context.cpp:934-973`；`AngelscriptBytecodes.cpp:1163-1195, 1448-1504`；`AngelscriptStaticJIT.cpp:3415-3449`；`ASClass.cpp:1647-1665, 1682-1690` |
| 问题 | 同一类“脚本函数到底该调用谁、何时走 raw JIT、何时回退 VM、何时抛 null/unbound 异常”的决策，目前散落在三套独立实现里。解释器 `as_context.cpp` 会在执行时检查 virtual object、vtable slot 和 imported `boundFunctionId`，失败时抛 `TXT_NULL_POINTER_ACCESS` / `TXT_UNBOUND_FUNCTION`。`ASClass.cpp` 的 wrapper 会先尝试 `jitFunction_Raw`，为空时回退到 `FAngelscriptGameThreadContext`。而 `StaticJIT` 这边又额外引入了 `DevirtualizeFunction()`、`IsFunctionAlwaysJIT()`、`WriteDirectCall()` 和 `WriteDynamicCall()` 四段自定义策略，且与前两套规则并不一致。Issue-1 与 Issue-2 都是这种分裂实现的直接结果：一个路径修过的 guard/fallback，不会自动出现在另外两条路径上。 |
| 根因 | 调用目标解析没有被抽象成单一的 runtime contract，而是随着不同执行后端逐步复制粘贴、局部优化，最终形成“解释器规则一套、Class wrapper 一套、JIT codegen 一套”的并行体系。 |
| 影响 | 这会把任何后续改动都放大成多点修复任务：新增一种调用模式、修一个 null/unbound/fallback 缺陷、或调整 imported/virtual 语义时，都必须同时修改并验证多套实现，否则语义继续分叉。当前已经出现了真实缺陷，后续继续在这套结构上补丁式修复，回归概率会持续升高。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 抽出统一的“脚本调用目标解析 + fallback 策略”层，让 StaticJIT 与 `ASClass` wrapper 调同一套 helper，而不是继续复制解释器语义。 |
| 具体步骤 | 1. 在 runtime 层定义一个统一 helper，例如 `ResolveScriptCallTarget(...)` / `InvokeScriptFunctionWithFallback(...)`，明确输入为 `ScriptFunction`、对象指针、调用类型（direct / virtual / imported），输出为“raw JIT target / VM context target / 规范异常”。 2. 让 `ASClass.cpp` 与 StaticJIT 生成代码都调用这组 helper，至少把 virtual slot 边界检查、imported `boundFunctionId` 查询、`jitFunction_Raw` 空值回退、unbound/null exception 处理收敛到一处。 3. 对 `FCallScriptFunction` 做减法：它只负责参数封送与代码生成，不再自己决定 imported/virtual 的最终目标。 4. 建立行为一致性测试矩阵，按 direct、virtual、imported 三类调用分别比较“解释器执行”“Class wrapper 调用”“JIT caller 调用”的结果、异常和 fallback 路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` 或新增一致性测试文件 |
| 预估工作量 | L |
| 风险 | 抽象层下沉到 runtime helper 后，可能暴露当前各路径对参数封送、返回值寄存器和异常时序的隐含差异；如果一次性替换范围过大，回归面会比较广。 |
| 前置依赖 | 建议先落地 Issue-1 与 Issue-2 的最小修复，再以这些已确认缺陷为回归基准推进收敛重构。 |
| 验证方式 | 1. 新增一致性测试矩阵，覆盖 direct/virtual/imported + JIT hit/miss + bound/unbound 组合。 2. 对比修复前后生成代码，确认 `StaticJIT` 不再单独内联另一套目标解析策略。 3. 回归现有脚本调用与 precompiled/JIT 测试，确保性能优化仍然只改变路径，不改变结果。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-1 | Defect | 立即修复，先堵住 JIT virtual dispatch 的崩溃/未定义行为 |
| P1 | Issue-2 | Defect | 在 P0 之后修复，恢复 imported call 的运行期绑定语义 |
| P1 | Issue-3 | Architecture | 以 Issue-1/2 的回归用例为护栏，下一迭代收敛三套调用解析实现 |

---

## 发现与方案 (2026-04-08 12:45)

### Issue-4：脚本断点按 `ModuleName + Line` 聚合，模块内多文件会发生误命中和误清除

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptDebugServer.h:695-699, 719-720`；`AngelscriptDebugServer.cpp:589-600, 602-609, 938-946, 967-1005`；`AngelscriptEngine.h:1277-1287`；`AngelscriptEngine.cpp:4344-4356` |
| 问题 | `FAngelscriptDebugServer::FFileBreakpoints` 只保存 `Lines`，没有保存 section/filename 维度；`Breakpoints` 主表也按 `ModuleName` 聚合。`SetBreakpoint` 在已解析 module 时用 `ModuleDesc->ModuleName` 作为 key，并在整个 `FoundModule->scriptFunctions` 上无 section 过滤地做 `FindNextLineWithCode(WantedLine)`。运行期 `ProcessScriptLine()` 看到新的 `Section` 时，又把 `SectionBreakpoints[Section]` 指向同一个 module 级 `BreakpointStore`。与此同时，引擎编译同一个 module 时会把 `Module->Code` 中的多个 `FCodeSection` 依次 `AddScriptSection(...)` 到同一个 `asCModule`。这意味着只要同一 module 含有两个脚本文件，文件 A 上设置的 `Line 42` 会被文件 B 共享；`ClearBreakpoints` 也会通过同一个 module key 把该模块内其它文件的 `Lines` 一并清空。 |
| 根因 | 断点存储和代码行解析都丢失了“文件/section”这一维度，把 DAP 的 file-scoped line breakpoint 错建模成了 module-scoped line set。 |
| 影响 | 多文件 module 下会出现三类错误：1. 文件 A 的断点在文件 B 的同号行误命中；2. 文件 A 请求的“最近可执行行”可能被文件 B 的函数抢占，服务端回写错误行号；3. 清除某个文件的断点会连带清空同 module 其它文件的断点。该问题直接破坏 breakpoint correctness，且一旦项目启用预处理把多个源文件合进同一 module，会稳定复现。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把断点权威存储从“module -> line set”改成“section/filename -> line set”，只把 `module->hasBreakPoints` 保留为派生状态。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 重构 `FFileBreakpoints`，显式保存 `CanonicalFilename`，并把 `Breakpoints` 主表改成按 canonical filename 索引；`SectionBreakpoints` 继续做运行期缓存，但其 value 必须指向对应 section 自己的 breakpoint store，而不是 module 级共享对象。 2. 在 `SetBreakpoint` 中，module 已解析时先根据 `BP.Filename` 从 `ModuleDesc->Code` 建立“目标 section 属于该 module”的映射，再仅在该 section 的函数/行号范围内寻找最近可执行行；如果目标文件在该 module 中不存在，返回删除通知或错误，而不是退化到模块内任意函数。 3. 在 `ProcessScriptLine` 中，`SectionBreakpoints.FindOrAdd(Section)` 未命中时，应通过 `Section` 对应的 canonical filename 回查 file-scoped breakpoint store；若该 section 当前没有断点，不得创建或复用共享 module store。 4. 在 `ClearBreakpoints` 中只清空目标 filename 的 `Lines`，同时重新计算该 module 是否仍有其它文件断点，再决定是否把 `Module->hasBreakPoints` 置回 `false`。 5. 为运行期缓存增加一次性失效策略：模块重编译/热重载后清空 `SectionBreakpoints`，避免旧 `const char* Section` 指针继续映射到已过期 store。 6. 增加集成测试，构造一个包含两个 `FCodeSection` 的同名 module，在两个文件放置相同行号，验证设置/清除文件 A 断点不会影响文件 B。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` |
| 预估工作量 | M |
| 风险 | 改动断点主索引后，前面已记录的 filename fallback / reapply 路径也会一起受影响；如果兼容层处理不好，可能让单文件 module 的现有断点测试回归。 |
| 前置依赖 | 无，但建议与 Analysis 中已记录的 filename fallback 重绑问题一并设计，避免两次重构同一套索引。 |
| 验证方式 | 1. 新增“同 module 双文件同号行”回归测试，分别验证命中与清除隔离。 2. 回归现有 `AngelscriptDebuggerBreakpointTests` 与 `AngelscriptDebuggerSteppingTests`，确认单文件 fixture 行为不变。 3. 人工在带预处理/多 section 的实际脚本模块上验证 DAP 前端回写的 breakpoint line 与目标文件一致。 |

### Issue-5：补足 Analysis 发现 54 的修复方案：live socket 收包路径与已测试 envelope parser 分裂

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:64-109, 756-794`；`AngelscriptDebugTransportTests.cpp:18-231` |
| 问题 | 仓库已经有一套经过单测覆盖的 envelope parser：`TryDeserializeDebugMessageEnvelope()` 会保留 partial bytes、拒绝非法长度，并按 `[int32 length][uint8 type][body]` 消费缓冲区。但真实网络收包的 `ProcessMessages()` 没有复用它，而是自己手写了另一套 framing：先同步读 4 字节 `PacketSize`，再直接读 `PacketSize` 个字节，最后手动取 `MessageType`。这导致生产路径和测试路径长期分叉。当前不仅 partial read / zero-byte / invalid length 的行为与 transport tests 不一致，后续任何 envelope 协议演进也都必须手动改两处，否则测试覆盖天然失真。 |
| 根因 | transport 层缺少单一入口，生产代码把“socket 读字节”和“解析协议帧”耦合在一个函数里，绕过了已有的 buffer-based parser。 |
| 影响 | 当前 `DebugServer` 的协议健壮性问题会以结构性方式反复出现：测试修的是 `TryDeserializeDebugMessageEnvelope()`，线上跑的是另一套逻辑。只要收到截断包、非法长度、背靠背多包或未来新增 envelope 字段，live socket 路径都可能再次出现与单测结论不一致的解析错位、静默丢包或连接污染。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把收包逻辑改成“每个 client 维护一个 receive buffer，统一交给 `TryDeserializeDebugMessageEnvelope()` 解析”，删除 `ProcessMessages()` 里的手写 framing。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 为每个 `FSocket*` 增加 `ReceiveBuffer` 状态，例如 `TMap<FSocket*, TArray<uint8>> QueuedReceives`，明确把 socket I/O 和协议解析分层。 2. 在 `ProcessMessages()` 中只负责 `Recv()` 新字节并 append 到该 client 的 `ReceiveBuffer`；每次 append 后循环调用 `TryDeserializeDebugMessageEnvelope()`，只在 `bHasEnvelope == true` 时派发 `HandleMessage()`。 3. 当 parser 返回 `false` 或 message length 非法时，立即记录协议错误并断开该 client，同时清理其 `ReceiveBuffer`、`QueuedSends`、调试会话状态，避免残留坏字节污染后续流。 4. 把现有 manual header/body read、`PacketSize` 本地变量和第二套长度校验从 `ProcessMessages()` 删除，避免未来再次漂移。 5. 补一层小 helper，例如 `PumpClientReceiveBuffer(FSocket* Client, TArray<uint8>& Buffer)`，让 transport tests 可以直接驱动生产解析路径，而不是只测独立 parser。 6. 新增回归用例，覆盖 partial header、partial body、invalid length、背靠背多 envelope、以及合法消息前面跟一段坏头部后应立刻断开连接的场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 预估工作量 | M |
| 风险 | 改成 buffered receive 后，如果客户端清理与 timeout 逻辑没有同步更新，可能引入新的 stale buffer 或误断开；同时要注意避免在游戏线程上无限循环消费大 buffer。 |
| 前置依赖 | 无。该改动应优先于其它协议层修复，因为它决定后续消息体校验和错误处理的统一入口。 |
| 验证方式 | 1. 让 `AngelscriptDebugTransportTests` 直接覆盖生产消费 helper，而不是只测独立 parser。 2. 新增一个 fake socket / chunk feeder 场景，验证 partial read 与 invalid length 在 live path 上的行为与 unit test 一致。 3. 人工用调试客户端发送坏包，确认服务器会明确断开该连接而不是进入错位解析。 |

### Issue-6：补足 Analysis 中 JIT fallback 调试缺口的修复方案：precompiled VM fallback 继承了无 line cue bytecode

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_bytecode.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` |
| 行号 | `AngelscriptEngine.cpp:1418-1446, 5429-5460`；`PrecompiledData.cpp:371-409, 527-547`；`as_bytecode.cpp:1827-1844`；`as_context.cpp:2411-2419` |
| 问题 | precompiled/JIT 生成阶段会把 `asEP_BUILD_WITHOUT_LINE_CUES` 置为 `1`，随后 `asCByteCode::Line()` 生成的 `asBC_LINE` 指令会被标记成 `size = 0`，不再转换成运行期可触发 line callback 的 `asBC_SUSPEND`。`FAngelscriptPrecompiledFunction::InitFrom()` 又把这份 bytecode 原样拷进 `ByteCode`，而 `Create()` 在 `FJITDatabase` 未命中时会直接 `AllocateScriptFunctionData()` 并把缓存 bytecode 恢复给解释器执行。与此同时，`UpdateLineCallbackState()` 与 `as_context.cpp` 的逐行调试链路明确依赖 line cue / `asBC_SUSPEND`。结果是函数一旦从 precompiled/JIT bundle 回退到 VM，并不是“解释执行但仍可调试”，而是“解释执行且函数内部没有逐行断点/步进入口”。 |
| 根因 | StaticJIT fallback 复用了为 packaged 性能优化而去掉 line cue 的同一份 bytecode 产物，没有把“可执行 fallback”和“可调试 fallback”分开建模。 |
| 影响 | 当前 fallback 语义不完整：只保证功能执行，不保证 DebugServer V2 的 breakpoint、step、pause-next-line 在回退函数内部仍然成立。对于部分 JIT bundle、development mode、JIT miss 或未来灰度 rollout，这会把“JIT caller -> VM callee”场景变成调试黑洞，直接削弱 fallback 作为安全兜底的价值。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 precompiled function 保留一份带 line cue 的 fallback bytecode，确保 JIT miss 时恢复的是“可调试 VM artifact”，而不是 stripped bytecode。 |
| 具体步骤 | 1. 调整 `AngelscriptEngine.cpp` 中 precompiled/JIT 生成顺序，不要在捕获 fallback bytecode 之前全局打开 `asEP_BUILD_WITHOUT_LINE_CUES`；如果 JIT 生成必须依赖该开关，至少把“保存 precompiled fallback bytecode”和“生成 JIT 输出”拆成两个明确阶段。 2. 在 `FAngelscriptPrecompiledFunction` 中显式区分 `FallbackByteCode` 与 JIT 相关 metadata；`InitFrom()` 只保存带 line cue 的 VM bytecode，JIT entry 仍然单独走 `FJITDatabase`。 3. `Create()` 检测到 `JITFunctions == nullptr` 或 development mode 时，恢复 `FallbackByteCode`，并增加断言/统计确认这份 bytecode 里仍包含 line cue 对应的可暂停指令，而不是静默接受 stripped artifact。 4. 若包体大小是顾虑，可在保存阶段只对“可能发生 JIT miss 的函数”保留 fallback line cue，或引入构建配置开关，但默认策略必须优先保证调试正确性。 5. 为 DebugServer 增加端到端回归：构造一个 precompiled function，人为让其缺失 JIT entry 后以 VM fallback 运行，并验证函数内部 line breakpoint、`StepOver`、`Pause` 都仍然生效。 6. 同步补充运行时可观测性，在 JIT miss 进入 VM fallback 时记录一次带函数 id/decl 的日志或 counter，方便确认线上是否仍频繁走这条受调试保护的路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 预估工作量 | L |
| 风险 | 保留 line cue 会增加 precompiled artifact 体积，并可能改变 packaged 运行时的 suspend/checkpoint 分布；如果生成顺序处理不好，还可能影响现有 StaticJIT 产物的 hash 与兼容性。 |
| 前置依赖 | 建议先确定 Issue-1/Issue-2 的 caller/callee fallback 语义，再落地这一条，避免调用级 fallback 与字节码级 fallback 分别演化。 |
| 验证方式 | 1. 新增“JIT miss -> VM fallback 仍可 step/break”集成测试。 2. 对比修复前后 precompiled bytecode，确认 fallback artifact 保留 line cue。 3. 回归现有 precompiled/JIT 测试，确认完全命中 JIT 的路径性能与行为不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-4 | Defect | 先修复 file-scoped breakpoint 被 module-scoped 共享的问题，避免继续产生命中/清除误判 |
| P1 | Issue-5 | Architecture | 紧接着统一 live socket 收包与 envelope parser，为后续协议修复建立单一入口 |
| P1 | Issue-6 | Architecture | 在调用级 fallback 语义稳定后修复 VM fallback 的可调试性，闭合 JIT/解释执行切换链路 |

---

## 发现与方案 (2026-04-08 12:54)

### Issue-7：补足 Analysis 发现 72 的修复方案：`ProcessMessages()` 在 partial read 时会覆写收包缓冲区开头

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `724-789` |
| 问题 | `ProcessMessages()` 读取包头和包体时虽然维护了 `BytesReceived`，但两处 `Client->Recv(...)` 都把写入地址固定成 `Datagram->GetData()`，没有使用 `+ BytesReceived` 偏移。源码 `756-789` 显示 header/body 两个循环都会在多次 `Recv()` 时从 buffer 起始位置反复覆写已有内容，而 `BytesReceived` 仍继续累加。结果是只要合法消息分多片到达，最终 `*Datagram << PacketSize` 和后续消息体反序列化看到的都不是按 TCP 顺序拼接的完整帧。 |
| 根因 | live socket 路径手写了逐段收包逻辑，但“已接收字节数”只用于退出条件和剩余长度计算，没有参与写入偏移，也没有把 TCP partial read 视为协议层的一等场景。 |
| 影响 | 调试客户端在网络分片、较大 payload 或 Nagle 合包下会把本来合法的 `Pause`、`SetBreakpoint`、`Variables` 等消息解码成损坏帧，表现为随机 message type 错位、消息体解析异常，甚至进一步触发已有文档记录的卡死和会话污染。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先修正当前收包循环的写入偏移与失败退出条件止血，再把 live socket 路径完全收敛到统一的 buffered envelope parser。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp:767-772` 与 `784-789` 两个循环中，把 `Recv` 目标地址改成 `Datagram->GetData() + BytesReceived`，并显式检查 `bool bRecvOk` 与 `BytesRead > 0`，任一条件不满足时立即中止当前 client 的报文读取。 2. 中止读取时不要继续使用半包 `Datagram`，而是走统一的 client cleanup helper，清理该连接的发送队列、callstack 请求和调试会话状态，避免坏包残留。 3. 以本条修复为临时止血，在随后落地现有 `Issue-5` 时删除这套手写 header/body 拼装，改成“append 到 per-client receive buffer + `TryDeserializeDebugMessageEnvelope()`”的单入口实现，避免两套 framing 再次漂移。 4. 在 `AngelscriptDebugTransportTests.cpp` 增加 live-path 风格回归：按 `2+2` 字节拆 header、按 `N` 段拆 body、以及连续两个 back-to-back envelope 都必须能被正确还原；同一组用例要同时验证非法/中断读取会触发连接清理，而不是继续消费损坏帧。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只做偏移热修而不尽快收敛到统一 parser，后续协议字段演进仍会继续维护两套逻辑；此外，过早在错误分支上直接断开 client，需要确认不会误伤正常的非阻塞短读场景。 |
| 前置依赖 | 无。该问题应在继续扩展协议字段前优先修复。 |
| 验证方式 | 1. 新增 chunked receive 回归测试，验证 header/body 分段到达时仍能正确解码。 2. 用 fake socket 或测试桩模拟 `Recv` 失败和 0-byte read，确认服务器会清理该 client 而不是继续消费半包。 3. 回归现有 transport/protocol/debugger tests，确认单包正常路径无行为回归。 |

### Issue-8：补足 Analysis 发现 64 的修复方案：异常断开和显式 `Disconnect` 只 `Close()` 不 `DestroySocket()`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 行号 | `AngelscriptDebugServer.h:585-588, 709-721`；`AngelscriptDebugServer.cpp:395-399, 419-432, 724-747, 1191-1194`；`AngelscriptDebuggerTestClient.cpp:76-85` |
| 问题 | `FAngelscriptDebugServer` 用原始 `FSocket*` 保存 `PendingClients`、`Clients` 和多个会话数组。连接被超时/断线移除时，代码只执行 `Client->Close()` 后就把指针从容器里删除；显式 `Disconnect` 分支也是同样只有 `Close()`。析构函数 `419-432` 只停止并释放 `Listener`，没有遍历并销毁任何已接受 client socket。对照仓库内测试客户端的正确做法，`Disconnect()` 会先 `Close()`，再通过 `ISocketSubsystem::DestroySocket(Socket)` 释放对象。 |
| 根因 | DebugServer 把 client 连接视为“可关闭的句柄”，但数据结构和所有权实现其实保存的是需要显式销毁的原始 `FSocket*`；类内缺少统一的 socket release helper，导致所有退出路径都停留在半完成状态。 |
| 影响 | 只要客户端异常断开、发送超时被移除、发送 `Disconnect`，或者引擎销毁时仍有活动连接，服务端就会遗留未销毁的 socket 对象及其底层资源。长时间 editor 调试、自动化重复建连、或恶意抖动连接都会把这条路径放大成稳定的 handle / memory 泄漏，并进一步增加调试端口的拒绝服务面。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `FSocket*` 生命周期建立单一释放入口，在所有断开、超时和析构路径统一执行“摘除状态 + `Close()` + `DestroySocket()`”。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h/.cpp` 新增私有 helper，例如 `ReleaseClientSocket(FSocket* Client, bool bRemoveFromSessionState)`，内部统一执行：从 `Clients`、`ClientsThatWantDebugDatabase`、`ClientsThatAreDebugging`、`QueuedSends`、`CallstackRequests` 等所有容器摘除该指针；必要时更新 `bIsDebugging` / `bIsPaused` / breakpoints；最后调用 `Client->Close()` 和 `ISocketSubsystem::Get(...)->DestroySocket(Client)`。 2. 把 `724-747` 的断线/超时移除分支和 `1191-1194` 的 `Disconnect` 分支都改为调用该 helper，而不是各自手写半套清理。 3. 在析构函数中停止 `Listener` 后，显式 drain `PendingClients` 并遍历剩余 `Clients` 调用同一 helper，确保引擎退出时不会遗留 socket。 4. 为避免双重释放，helper 内要先判重并把容器摘除放在 `DestroySocket` 之前；若存在由 `PendingClients` 直接进入销毁路径的情况，也要统一通过同一 helper 处理。 5. 增加测试 seam：对 `FAngelscriptDebugServer` 暴露一个仅测试使用的 socket-destroy callback 或统计计数，允许 `AngelscriptDebugTransportTests` 验证 `Disconnect`、超时移除和 server 析构都会触发销毁，而不是只能观察功能行为。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 release helper 没有统一处理所有状态容器，容易引入 double-remove 或残留 stale socket；另外需要确认 `DestroySocket()` 的线程与 socket subsystem 使用约束，避免在 listener 回调线程和游戏线程之间重复销毁。 |
| 前置依赖 | 无，但建议与 `Issue-7` 的 client cleanup helper 设计合并，避免为错误断开路径再写一套清理逻辑。 |
| 验证方式 | 1. 新增 transport 层测试，覆盖显式 `Disconnect`、发送超时移除和 server 析构三种路径，断言 socket destroy 计数与期望一致。 2. 在长时间重连压力测试中观察进程 handle / socket 数不再单调上涨。 3. 回归现有 debugger smoke / protocol tests，确认连接关闭后不会留下 stale queue 或额外崩溃。 |

### Issue-9：补足 Analysis 发现 76 的修复方案：JIT 下 `DebugBreak` / `ensure` / `check` 会绕过 DebugServer 直接落到 native break

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h` |
| 行号 | `Bind_Debugging.cpp:14-28, 78-198`；`AngelscriptEngine.cpp:5632-5676, 5718-5745`；`as_context.h:262-279`；`StaticJITConfig.h:8-9` |
| 问题 | `ASDebugBreak()` 在 `Bind_Debugging.cpp:24-28` 里先调用 `FAngelscriptEngine::TryBreakpointAngelscriptDebugging()`，返回 `false` 就直接 `UE_DEBUG_BREAK()`。但 `TryBreakpointAngelscriptDebugging()` 在 `5729-5731` 明确要求 `asGetActiveContext() != nullptr`，否则立即返回 `false`。与此相对，JIT 执行入口 `FScriptExecution` 在 `as_context.h:266-279` 会把 `tld->activeExecution` 设为当前执行，同时显式把 `tld->activeContext = nullptr`。同一个引擎文件里的 `GetAngelscriptExecutionPosition()` / `GetAngelscriptExecutionFileAndLine()` 又已经证明 JIT 运行时真正的源码定位信息来自 `activeExecution->debugCallStack`。结果是脚本仍在 JIT 中运行、位置数据也仍存在，但 `DebugBreak()`、失败的 `ensure` 和 `check` 会被错误地当成“没有脚本上下文”，直接绕过 DebugServer。 |
| 根因 | 调试停点入口仍按解释器模型判断“是否处于脚本执行”，把 `activeContext` 作为唯一真源；而 JIT 运行时已经把当前执行状态迁移到 `activeExecution`，两条线程上下文模型没有在 breakpoint bind 层收敛。 |
| 影响 | 同一段脚本在解释执行时能进入 DebugServer 停止态，在 JIT 覆盖后却会直接触发 native debugger break 或仅打印日志，`DebugBreak` / `ensure` / `check` 语义发生分叉。这不仅破坏调试正确性，也让 JIT fallback/灰度 rollout 场景失去一致的诊断手段。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“当前是否存在可调试脚本执行”从 `activeContext` 单点判断升级为统一的 execution target 解析，同时让 DebugServer 在 JIT 活动帧上也能停住并返回基础调用栈。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.cpp` 抽一个统一 helper，例如 `GetActiveScriptDebugTarget()`，优先返回 `tld->activeContext`，否则在 `tld->activeExecution != nullptr` 且存在 `debugCallStack` 时返回 JIT 执行目标。`TryBreakpointAngelscriptDebugging()` 改为依赖这个 helper，而不是硬编码 `asGetActiveContext()`. 2. 保持 `PauseExecution()` 作为统一停点入口，只把 loop-detection reset 保持为“有 `asCContext` 时才执行”的可选逻辑；对 JIT 执行目标同样发送 `HasStopped`，不要再落到 `UE_DEBUG_BREAK()`. 3. 为了让停点后的 debugger 可用，补一条最小 JIT call stack 序列化路径：当 `asGetActiveContext()==nullptr` 且存在 `activeExecution->debugCallStack` 时，让 `SendCallStack()` 直接从 `FScopeJITDebugCallstack` 链生成至少一帧 script frame，避免 JIT stop 之后前端拿到空栈。 4. 在 `Bind_Debugging.cpp` 保持 `ASDebugBreak()`、`ensure*`、`check*` 继续共用同一入口，避免后续三套 bind 再次分叉；需要时可把 stop reason 分成 `breakpoint` 与 `exception` 两类，但决策必须集中在同一 helper。 5. 新增 JIT 集成夹具：复用 `AngelscriptDebuggerScriptFixture.cpp:173-200` 里的 `TriggerDebugBreak` / `TriggerEnsure` / `TriggerCheck`，在启用 JIT 的非 `EditorContext` 运行态验证这三条路径都会产生 `HasStopped`，且不会触发 native break。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` |
| 预估工作量 | L |
| 风险 | 一旦允许 JIT 在 DebugServer 中停住，现有 `SendCallStack`、步进和变量查看路径会暴露更多只支持 `asCContext` 的假设；如果只修 stop 事件不补最小栈信息，前端体验仍然不可用。 |
| 前置依赖 | 需要一个可执行 JIT 且允许调试连接的运行态测试入口；若当前测试基建仍只覆盖 `EditorContext`，需先补专用 harness。 |
| 验证方式 | 1. 新增非 editor/JIT 运行态集成测试，分别调用 `TriggerDebugBreak`、`TriggerEnsure(false, ...)`、`TriggerCheck(false, ...)`，断言都收到 `HasStopped` 且不会触发 native break。 2. 停住后请求 `CallStack`，确认至少包含当前 JIT script frame。 3. 回归现有解释器态 debugger tests，确认原有 `DebugBreak`/`ensure`/`check` 行为不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-7 | Defect | 先修复 partial read 覆写，恢复协议基础完整性 |
| P1 | Issue-8 | Defect | 随后统一 client cleanup 与 socket 销毁，堵住异常断开泄漏 |
| P1 | Issue-9 | Defect | 在连接与收包路径稳定后补齐 JIT 显式停点链路 |

---

## 发现与方案 (2026-04-08 13:03)

### Issue-10：发送队列把 `partial send` 当作完整成功，DebugServer V2 出站 message 会被截断

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:703-709`；`AngelscriptDebugServer.cpp:2845-2858`；`AngelscriptDebugTransportTests.cpp:18-41, 43-231` |
| 问题 | 出站队列 `FQueuedMessage` 只保存整包 `Buffer` 和 `FirstTry`，没有记录已发送偏移。`TrySendingMessages()` 对队首消息调用一次 `Client->Send(...)` 后，只要返回值是 `true` 就立即 `Queue.RemoveAt(0)`，完全不校验 `BytesSent` 是否等于 `Msg.Buffer.Num()`。这和 DebugServer 当前 envelope 传输模型不兼容：同一文件里单测只覆盖了 `SerializeDebugMessageEnvelope()` / `TryDeserializeDebugMessageEnvelope()` 的 buffer 级行为，没有任何 live send 场景去验证 `partial send` 重试。结果是只要 socket 在背压、非阻塞或较大包下发生合法的 `partial send`，剩余字节会被服务端直接丢弃，客户端收到的就是截断帧。 |
| 根因 | 发送队列把 `FSocket::Send()` 的布尔返回误建模成“整包发送完成”，缺少 per-message `SendOffset` 状态，也没有把 TCP `partial send` 视为协议层的一等正常路径。 |
| 影响 | `CallStack`、`Variables`、`DebugDatabase` 等较大响应包在网络抖动下会随机变成损坏 envelope，客户端随后不是解析失败就是把残缺包尾错位到下一帧。这样即便入站 parser 已做长度校验，协议完整性仍会在出站侧被破坏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把发送队列改成显式跟踪“已发送到哪一个字节”，只有整个 envelope 发送完成后才出队。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 扩展 `FQueuedMessage`，新增 `int32 BytesSent = 0` 或同等含义字段，保留 `FirstTry` 作为超时基线。 2. 修改 `TrySendingMessages()`：调用 `Client->Send(Msg.Buffer.GetData() + Msg.BytesSent, Msg.Buffer.Num() - Msg.BytesSent, BytesSentThisCall)`，仅在 `BytesSentThisCall > 0` 时累加偏移；只有 `Msg.BytesSent == Msg.Buffer.Num()` 才 `RemoveAt(0)`。 3. 对 `Send()` 返回 `false`、`BytesSentThisCall == 0`、或 `BytesSentThisCall` 超过剩余长度的异常情况统一走 client cleanup，而不是保留一个永远无法前进的损坏队首。 4. 把发送逻辑抽成可测试 helper，例如“消费一条 queued envelope 到 socket adapter”，避免 transport tests 只能测序列化而测不到真实出站行为。 5. 在 `AngelscriptDebugTransportTests.cpp` 增加 fake socket / test seam，至少覆盖“首包只发前 N 字节、第二次补尾”“连续两个 envelope 串行发送”“send 返回 0 或 false 触发连接清理”三类场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 cleanup 条件过于激进，非阻塞短暂背压可能被误判成坏连接；如果队列偏移管理和现有 10 秒超时逻辑不同步，也可能引入“明明在缓慢前进却被提前踢掉”的新问题。 |
| 前置依赖 | 无，但建议与既有 `Issue-5` / `Issue-7` 的入站 parser 收敛一起设计，统一形成 per-client buffered transport 层。 |
| 验证方式 | 1. 新增 `partial send` 回归测试，断言单个 envelope 会在多次 `Send()` 后完整到达且不丢尾部。 2. 人工压低 socket send buffer 或用测试桩强制 `BytesSent < Buffer.Num()`，确认客户端仍能完整解析 `CallStack` / `Variables` 大包。 3. 回归现有 `AngelscriptDebugTransportTests`，确保原有 envelope 序列化/反序列化语义不变。 |

### Issue-11：DebugServer V2 把会话能力和调试状态建模成全局变量，多客户端会互相污染

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:20-22, 196-213, 416-437, 581-604, 671-687, 719-721`；`AngelscriptDebugServer.cpp:812-816, 897-923, 924-927, 1137-1145, 1382-1482`；`AngelscriptDebugProtocolTests.cpp:7-23, 121-170` |
| 问题 | 当前 DebugServer 没有 `FSocket* -> session state` 抽象。协议版本用 namespace 级全局 `AngelscriptDebugServer::DebugAdapterVersion` 保存，而 `FAngelscriptVariable::operator<<` 和 `SendCallStack()` 又直接读取这个全局值来决定 `ValueAddress/ValueSize` 与 `ModuleName` 是否进入 payload。与此同时，`StartDebugging` 会全局写 `bIsDebugging`、覆盖 `DebugAdapterVersion`、清空 `BreakOptions`、并执行 `ClearAllBreakpoints()`；`StopDebugging` 也会直接全局停调试并清空断点。`RequestCallStack` 只是把 socket 塞进单个 `CallstackRequests` 数组，暂停后统一 flush。连现有 protocol tests 都通过 `FScopedDebugAdapterVersionOverride` 直接改全局变量来切换 v1/v2 序列化。换言之，多个 client 并存时，服务器并不是“多个独立调试会话”，而是“多个 socket 共享一份可变全局状态”。 |
| 根因 | DebugServer 的 transport 层和 session 层没有分离，协议能力、break filters、断点状态和延迟请求都挂在服务器单例上，而不是挂在具体连接上。 |
| 影响 | 这会形成系统性的 DAP/session 语义漂移：1. 不同版本 client 会互相改变 `Variables` / `CallStack` 的二进制布局；2. 一个 client 的 `StartDebugging` / `StopDebugging` / `BreakOptions` 会重置其他 client 的断点与过滤器；3. 某个 client 在运行态发起的 `RequestCallStack` 可能在之后任意一次全局暂停时收到响应。该问题不是单条消息的 bug，而是整个 DebugServer V2 会话模型不隔离。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 引入显式 `FDebugClientSession`，把协议协商与调试状态从 server-global 拆到 per-client，再由服务器维护只读的全局派生状态。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 新增 `FDebugClientSession` 结构，至少持有 `DebugAdapterVersion`、`bIsDebugging`、`BreakOptions`、`PendingCallstackRequest`、以及该 client 自己声明的 breakpoint/data-breakpoint 集合；用 `TMap<FSocket*, FDebugClientSession>` 替代当前散落的 `ClientsThatAreDebugging`、`CallstackRequests` 和全局 capability 字段。 2. 把 `SendMessageToClient()` 扩成 session-aware 序列化入口，`Variables`、`CallStack`、后续任何带版本分支的 message 都从目标 session 读取 negotiated version，而不是读取 `AngelscriptDebugServer::DebugAdapterVersion`。 3. 把 `StartDebugging` / `StopDebugging` / `BreakOptions` 改成只更新当前 session；服务器额外维护一份“所有 active session 断点的并集”作为运行时 line callback 判定输入，而不是直接把某个 client 的请求写进全局权威状态。 4. `RequestCallStack` 在 paused 时应立即向请求 session 回包；若当前未 paused，就显式返回空栈/错误或拒绝，而不是塞进全局等待数组。 5. 保留必要的 server-global 派生标志，例如“是否存在任意 active debugging session”，但这些标志必须由 session map 重新计算，不能再被单个消息直接覆盖。 6. 扩充测试：增加双 client 场景，一个用 adapter v1、一个用 v2，同时请求 `Variables` / `CallStack`，并验证 `BreakOptions`、`StartDebugging`、`StopDebugging` 不会相互清空状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` |
| 预估工作量 | L |
| 风险 | 运行时 line breakpoint 判定当前依赖全局 `Breakpoints` / `BreakpointCount` 快速路径；若 session 并集与 per-client 原始状态切分不清，容易在性能和语义之间引入新的分叉。 |
| 前置依赖 | 建议先完成 `Issue-10` 的 transport 修复，确保 session 层不会再建立在损坏的 envelope 之上。 |
| 验证方式 | 1. 新增多 client 集成测试，验证 v1/v2 `Variables` 序列化互不污染。 2. 验证 client A 改 `BreakOptions` 或 `StopDebugging` 不会清空 client B 的断点和调试状态。 3. 验证 `RequestCallStack` 只对发起请求且处于有效 stop state 的 session 返回结果，不再漂移到后续任意暂停。 |

### Issue-12：`asBC_POWd` / `asBC_POWdi` 的 JIT handler 同时存在错误返回值和错误类型契约，`double Pow` 无法可靠生成

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:6470-6498`；`AngelscriptStaticJIT.cpp:3330-3336`；`AngelscriptStaticJIT.h:73-83`；`as_compiler.cpp:16704-16728`；`as_context.cpp:3982-4009`；`AngelscriptPrecompiledDataTests.cpp:13-21` |
| 问题 | `asBC_POWd` 和 `asBC_POWdi` 的 handler 已经分别生成了 `Math::Pow(...)` 代码，但函数末尾仍然 `return false`。`GenerateCppCode()` 对每条 bytecode 都执行 `check(bImplemented)`，因此脚本一旦包含这两条 bytecode，codegen 会在写完输出后立刻断言失败。更糟的是 `asBC_POWdi` 还把第 3 个操作数断言成 `EVariableType::Double`，但同一仓库的 compiler 明确只在“左值 `double`、右值 integer”时发出 `asBC_POWdi`，解释器执行路径也把第 2 个输入按 `int` 读取。也就是说，就算先把错误返回值修掉，现有 `POWdi` 类型断言仍会对合法字节码触发 `check`。当前 `AngelscriptPrecompiledDataTests.cpp` 只有两个 precompiled round-trip 用例，没有任何脚本级 JIT bytecode 覆盖能提前挡住这类回归。 |
| 根因 | 这两个 handler 是半完成状态：正文已经按“已支持”写出 JIT 表达式，但收尾仍保留“未实现”返回值；`POWdi` 还复制了 `POWd` 的 `Double/Double/Double` 断言，没有同步 AngelScript 对“double + int 指数”的真实字节码契约。 |
| 影响 | `double Pow(double,double)` 与 `double Pow(double,int)` 在解释执行下是合法且可运行的，但进入 StaticJIT 生成后会直接断言中止，而不是产生与解释器一致的结果或安全 fallback。这是明确的 JIT/解释器行为分叉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把 `POWd` / `POWdi` 修成真正的已实现状态，再用脚本级回归测试锁住 JIT 与解释器的一致性。 |
| 具体步骤 | 1. 在 `AngelscriptBytecodes.cpp` 把 `asBC_POWd` 与 `asBC_POWdi` 的 `return false` 改成 `return true`，使其与 `asBC_POWf` 的“已生成代码即返回成功”语义一致。 2. 修正 `asBC_POWdi` 的操作数断言，令结果槽和底数保持 `Double`，指数槽改为与 compiler / interpreter 一致的 integer 表示，即 `EVariableType::DWord`，继续通过 `VArg_SignedVar(2)` 生成 `(double)int` 转换。 3. 若生成器仍希望处理溢出语义，应显式对齐解释器 `HUGE_VAL -> TXT_POW_OVERFLOW` 的契约；若当前阶段先不做异常桥接，至少不能继续把合法 `double Pow` 误判成“未实现”。 4. 在 Runtime tests 新增最小脚本级用例，分别覆盖 `double ** double` 和 `double ** int`，验证 `CompileFunction()` / `GenerateCppCode()` 不再断言，并且 JIT 结果与解释执行一致。 5. 后续可以把所有 `POW*` handler 放进一个小型 consistency matrix，避免再次出现“float 版本正确、double 版本半完成、integer 版本直接 `check(false)`”的分裂状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` 或新增 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果补上 `return true` 后没有同步验证异常语义，`double Pow` 可能从“生成期断言”转成“运行期 silently differs”；因此测试必须同时比对正常值和 overflow 行为。 |
| 前置依赖 | 无。该问题可以独立修复，但建议顺手补上 dedicated StaticJIT bytecode 测试入口。 |
| 验证方式 | 1. 新增 `double ** double` / `double ** int` 回归测试，确认 JIT codegen 不再触发 `check(bImplemented)`。 2. 对比解释执行与 JIT 执行结果，确保普通值一致。 3. 若补了 overflow 处理，再增加大指数场景，确认异常或返回值与解释器保持一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-10 | Defect | 先修复出站 `partial send`，恢复协议传输的基本完整性 |
| P1 | Issue-12 | Defect | 随后修复 `double Pow` JIT correctness，堵住确定性的 codegen 断言 |
| P1 | Issue-11 | Architecture | 在 transport 稳定后推进 per-client session 拆分，补齐 DebugServer V2 的会话隔离 |

---

## 发现与方案 (2026-04-08 13:11)

### Issue-13：补足 Analysis 发现 77/78 的修复方案：Windows data breakpoint 寄存器编程路径同时违反 `GetThreadContext()` 契约并写错 `Dr7` 基础位

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:221-253`、`134-172` |
| 问题 | `FUpdateDebugRegisterThread::Run()` 先把 `CONTEXT` 清零并直接调用 `GetThreadContext(ThreadToDebug, &Context)`，直到调用成功之后才在 `247` 行设置 `Context.ContextFlags = CONTEXT_DEBUG_REGISTERS`。这违反了 Windows API “调用前先声明要读取哪些寄存器”的基本契约，导致后续 `Dr0-Dr7` 快照没有任何已验证前提。紧接着初始化 `Dr7` 时又先执行 `Context.Dr7 &= 0x0000000000003F00`，再在 `251` 行写入字面量 `0x001000000000`；同段注释却明确要求“bit 10 应为 1，bit 14/15 和 32-63 应为 0”。该常量实际把高位重新置 1，而不是设置 bit 10。后面的 `ApplyBreakpointsToThreadContext()` 只会在这个错误基底上继续叠加本地 enable 与长度/类型位。 |
| 根因 | data breakpoint register programming 依赖手写魔法数字和分散初始化，没有把 `CONTEXT` 读取前置条件与 `Dr7` 保留位规则封装成单一 helper，因此同一函数里同时出现了“调用顺序错”和“位掩码错”两类底层契约错误。 |
| 影响 | 当前 Windows data breakpoint 安装不是“个别边界情况下不稳定”，而是所有更新路径都建立在不可信寄存器快照和错误 `Dr7` 基底之上。结果可能表现为 `GetThreadContext`/`SetThreadContext` 失败、watchpoint 偶发不触发、或不同机器上对同一 `SetDataBreakpoints` 请求出现不可预测差异。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 debug-register 初始化收敛成可验证的单一 helper：先设置 `ContextFlags` 再取上下文，并用命名位常量替换错误魔法数字。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 提取诸如 `InitializeDebugRegisterContext(CONTEXT&)` 的 helper，调用顺序固定为：`memset` 后立即设置 `Context.ContextFlags = CONTEXT_DEBUG_REGISTERS`，然后再执行 `GetThreadContext()`。 2. 用具名常量替换 `0x0000000000003F00` 和 `0x001000000000`，按 Intel/Windows `Dr7` 语义明确保留/清零位；至少把“bit 10=1、14/15=0、32-63=0”写成可读表达式，而不是继续散落魔法数。 3. 让 `ApplyBreakpointsToThreadContext()` 只负责写 `Dr0-Dr3` 和每个槽位的 enable/len/rw 位，不再隐式依赖调用者已经构造了正确 `Dr7` 基底；必要时把“清槽位 + 写槽位”也并入统一 helper。 4. 增加 Windows 专用单元测试或最小可测 seam，至少验证“初始化后 `ContextFlags` 正确”“空 breakpoint 列表时 `Dr7` 只保留允许位”“写入 1/2/4/8-byte watchpoint 时只修改对应槽位与长度位”。如果直接调用 Win32 API 难以单测，可把位运算提取成纯函数后在 Runtime tests 中覆盖。 5. 在 `SetDataBreakpoints` 集成路径增加一次日志/断言，当 `GetThreadContext()` 或 `SetThreadContext()` 失败时回包明确错误，不再只是写日志后静默退化。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` 或新增 Windows 专用 data breakpoint 测试文件 |
| 预估工作量 | M |
| 风险 | 如果 `Dr7` 初始化 helper 与异常处理器里对 `ContextRecord->Dr7` 的重写逻辑不一致，可能修掉安装路径后又在命中路径上重新写回脏状态；因此两处位运算必须一起审计。 |
| 前置依赖 | 无，但建议紧接着处理同文件的 `DebugRegisterExceptionHandler()` 传播条件，避免修好寄存器编程后仍被错误异常处理链抵消。 |
| 验证方式 | 1. 运行 Windows data breakpoint 自动化，验证合法 watchpoint 可稳定安装且不再报 `GetThreadContext` / `SetThreadContext` 错误。 2. 用提取出的纯函数测试断言 `Dr7` 初始化结果满足注释声明的位布局。 3. 人工在同一脚本上重复安装/清除 data breakpoint，确认跨多次更新寄存器值稳定、命中行为一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-13 | Defect | 先修复 Windows data breakpoint 的底层寄存器编程契约，再处理命中后的异常传播与停止事件 |

---

## 发现与方案 (2026-04-08 13:13)

### Issue-14：补足 Analysis 发现 83/84 的修复方案：data breakpoint 异常处理链把“命中识别”和“停止交付”耦合在一起，既会吞掉外部 `EXCEPTION_SINGLE_STEP`，也会丢失 C++ 写入停点

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 行号 | `AngelscriptDebugServer.cpp:272-379`、`465-545`；`AngelscriptDebugServer.h:129-142, 300-398` |
| 问题 | `DebugRegisterExceptionHandler()` 只要看到 `EXCEPTION_SINGLE_STEP` 且 `Dr6 & 0xF` 非零，就会进入本插件路径；即便循环内没有任何一个 `ActiveDataBreakpoint` 真正完成“本插件 watchpoint 命中”的确认，函数最后仍会清 `Dr6`、更新 `ContextFlags` 并无条件 `return EXCEPTION_CONTINUE_EXECUTION`。与此同时，真正向调试客户端发 `FStoppedMessage` 的逻辑根本不在异常处理器里，而是被延迟到后续 `ProcessScriptLine()` 的 `bBreakNextScriptLine` 分支。该分支又把 data breakpoint 停止统一伪装成 `Reason = "exception"`，并且只有在“后面还有脚本 line callback”时才会被执行。结果是：1. 外部调试器或别的硬件断点产生的 `EXCEPTION_SINGLE_STEP` 可能被本插件静默吞掉；2. watchpoint 若发生在 `asGetActiveContext() == nullptr` 的 C++ 写入路径上，只要后续没有新的脚本行，`HasStopped` 就永远不会送达。 |
| 根因 | 当前设计把“异常线程里识别硬件命中”“脚本层暂停”“协议层发送 stopped event”串成了单通道，并且把停止交付强绑定到下一次脚本 line callback。该模型既没有“只有我真的处理了自己的 watchpoint 才拦截异常”的传播条件，也没有“无脚本行时如何可靠交付 stop”的后备路径。 |
| 影响 | data breakpoint 现在不是单纯的 message 文案问题，而是调试语义不可靠：外部 native/hardware debugger 可能被干扰，C++ 写入场景的监视点可能完全不停止，客户端看到的 stopped reason 还会与真实命中源不一致。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 拆分成两阶段流水线：异常处理器只做“确认是否为本插件命中并记录事件”，停止与协议交付改由安全上下文统一消费。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer` 中新增一个无堆分配的 pending-data-breakpoint 记录结构，例如固定容量 ring buffer 或 `TAtomic` 驱动的单生产者槽位，字段至少包含 `BreakpointId`、`Status`、`bCppBreakpoint`、`asCContext*`/是否有脚本上下文。异常处理器只能写这份轻量记录，不能继续承担完整 stop 交付。 2. 修改 `DebugRegisterExceptionHandler()`：只有当至少一个 `ActiveDataBreakpoint` 从“未触发”转成“本次由本插件确认触发”时，才清理 `Dr6`、重写 `Dr7` 并返回 `EXCEPTION_CONTINUE_EXECUTION`；否则直接 `EXCEPTION_CONTINUE_SEARCH`，让外部调试器或其他 handler 继续处理。 3. 在 `Tick()` 或独立的 `PumpPendingDataBreakpointStops()` 中主动消费 pending 事件；对存在脚本上下文的命中，可以在保持当前暂停模型的前提下立即生成 stopped event；对 `Context == nullptr` 的 C++ 写入，必须脱离“等下一条脚本行”的假设，直接从 pending 队列发出 stop，而不是把语义寄托在未来可能不会发生的 line callback 上。 4. 把 data breakpoint 的停止原因与普通异常分开编码：至少将 `FStoppedMessage.Reason` 改成 `breakpoint`，`Text`/`Description` 再标注 data breakpoint 名称；如果后续协议要支持精确 breakpoint id，可在这个新流水线上扩展，但不要继续把 watchpoint 伪装成 `exception`。 5. 收敛 `bBreakNextScriptLine` 的职责，只让它表示“脚本上下文内需要在下一行安全暂停”，不再承担所有 data breakpoint 的总开关；这样 C++ 写入和脚本写入可以走同一套 pending-event 消费模型，只在暂停时机上区分。 6. 增加两类回归测试：一类用测试桩验证“无本插件命中时 handler 返回 `EXCEPTION_CONTINUE_SEARCH`”；另一类构造 `Context == nullptr` 的 C++ 写入 data breakpoint，验证即使后续没有新的脚本行，也能收到 stopped event。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 或新增 Windows data breakpoint 集成测试 |
| 预估工作量 | L |
| 风险 | 一旦把 stop 交付从 `ProcessScriptLine()` 抽出来，当前单线程暂停假设、`PauseExecution()` 的线程归属以及多命中合并策略都会暴露出来；如果没有一起设计消费顺序，可能引入重复 stopped event 或新的竞态。 |
| 前置依赖 | 建议先完成 `Issue-13` 的寄存器编程修复，确保 pending-event 流水线建立在正确的硬件断点安装之上。 |
| 验证方式 | 1. 新增回归测试，验证外部/伪造的非本插件 `EXCEPTION_SINGLE_STEP` 不会被 `DebugRegisterExceptionHandler()` 吞掉。 2. 新增 `Context == nullptr` 的 data breakpoint 用例，断言没有后续脚本 line callback 时仍能收到 stopped event。 3. 回归现有脚本内 data breakpoint 场景，确认仍可正常停住且不会连续发出多次错误 `exception` stop。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-14 | Architecture | 在修好寄存器安装后，立刻重构 data breakpoint 的异常传播与停止交付链 |

---

## 发现与方案 (2026-04-08 13:14)

### Issue-15：补足 Analysis 发现 79 的修复方案：真实 JIT/precompiled 运行态与 `DebugServer` 启动条件被设计成互斥，DebugServer V2 无法端到端覆盖目标场景

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h` |
| 行号 | `AngelscriptEngine.cpp:1425-1456, 1513-1608`；`StaticJITConfig.h:4-10` |
| 问题 | 引擎启动时先把 `bUsePrecompiledData` 定义成 `!bGeneratePrecompiledData && !RuntimeConfig.bIgnorePrecompiledData && !RuntimeConfig.bRunningCommandlet && !WITH_EDITOR && !bScriptDevelopmentMode`，也就是“非 editor、非开发态、消费 precompiled cache 的运行态”。随后 `DebugServer` 却只在 `(!bUsePrecompiledData || bScriptDevelopmentMode)` 时创建。与此同时，真实 JIT/transpiled code 的使用依赖 `bUsePrecompiledData` 路径去加载 `PrecompiledData` 并在 `1594` 行设置 `bStaticJITTranspiledCodeLoaded`；而 editor build 又在 `StaticJITConfig.h:8-10` 通过 `AS_SKIP_JITTED_CODE` 直接裁掉 JIT 函数体。生成模式虽然会创建 `StaticJIT` 并调用 `WriteOutputCode()`，但 `1575-1608` 随后立即 `RequestExitWithStatus()`，并不是可连接调试器的长期运行态。换言之，当前产品代码里不存在“真正执行 JIT/transpiled code，同时 `DebugServer` 仍然在线”的稳定配置。 |
| 根因 | 启动流程把“是否消费 precompiled/JIT 产物”和“是否允许调试”绑成了互斥开关，缺少一个显式的 runtime debug mode 去承载“连接 DebugServer，但脚本执行仍走 precompiled/JIT”的场景。 |
| 影响 | 这不是单个 breakpoint/step/evaluate 缺陷，而是整个 DebugServer V2 在目标 JIT 运行态上缺少可执行载体。只要这层架构不拆开，JIT 调试相关修复就无法做端到端验证，测试也只能停留在 editor/non-JIT 环境里。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“消费 precompiled/JIT 产物”和“启动 DebugServer”从同一个布尔条件里拆开，引入显式的 precompiled-runtime debugging 模式与对应测试 harness。 |
| 具体步骤 | 1. 在 runtime config 中增加独立开关，例如 `bEnableDebugServerInPrecompiledRuntime`，仅受 `WITH_AS_DEBUGSERVER` 和非 shipping 白名单控制；`DebugServer` 的创建条件改为“允许调试服务器”而不是“当前不是 precompiled 模式”。 2. 保持 `bUsePrecompiledData` 继续决定脚本加载/JIT 使用策略，但不要再借它顺便关闭 `DebugServer`。换言之，允许出现“precompiled data + transpiled code 已加载 + debug socket 已监听”的组合态。 3. 为这条组合态补齐最小调试元数据需求：如果 fully precompiled / JIT runtime 仍缺少 line cue、stack map 或 source map，需要在 `PrecompiledData`/transpiled bundle 中携带专用 debug metadata，而不是退回到 editor-only line callback 假设。 4. 新增专用测试入口，优先选非 editor 的自动化 target、standalone harness 或 commandlet-like host：它必须加载 `PrecompiledScript*.Cache`、保留 `FJITDatabase` 注册、同时允许测试 client 连接 `DebugServer`。 5. 在这条 harness 上建立至少一组 smoke tests，覆盖“连接会话”“命中 JIT 函数中的 breakpoint/exception”“请求 call stack”三条最小闭环；没有这组测试，后续任何 JIT 调试修复都没有回归护栏。 6. 对 shipping/test/dev 构建做能力分级：如果出于安全或体积考虑不允许某些配置打开 `DebugServer`，应显式记录并在启动日志中说明，而不是让当前互斥条件继续以隐式方式生效。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptManager.h/.cpp` 或 runtime config 定义文件、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` / 相关 debug metadata 文件、`Plugins/Angelscript/Source/AngelscriptTest/` 下新增非 editor/JIT 调试 harness |
| 预估工作量 | XL |
| 风险 | 一旦允许 precompiled runtime 暴露 `DebugServer`，会立刻暴露当前 JIT 调用栈、变量查看、步进、异常回报等一整批只支持解释器上下文的缺口；如果没有分阶段 rollout，很容易把一个架构开关变成大面积功能回归。 |
| 前置依赖 | 建议先完成 `Issue-13` 和 `Issue-14` 这类 data breakpoint 基础设施修复，再用新的运行态 harness 验证 JIT 调试链路。 |
| 验证方式 | 1. 启动非 editor、消费 `PrecompiledScript*.Cache` 的测试宿主，确认 `DebugServer` 仍会监听端口且 `bStaticJITTranspiledCodeLoaded == true`。 2. 用测试 client 连入后在 JIT 覆盖函数中触发断点/异常，验证能收到 `HasStopped` 与 `CallStack`。 3. 对比同一脚本在解释器态与 precompiled/JIT runtime 下的调试事件，确认至少最小闭环一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-15 | Architecture | 作为下一阶段基础设施任务，先打通真实 JIT 运行态的可调试宿主与自动化 harness |

---

## 发现与方案 (2026-04-08 13:19)

### Issue-16：补足 Analysis 发现 6 的修复方案：顶层脚本帧上的 `StepOut` 会错误退化成“下一行立即再停一次”

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:550-560, 877-895`；`AngelscriptDebuggerSteppingTests.cpp:557-655`；`AngelscriptDebuggerScriptFixture.cpp:97-112` |
| 问题 | `StepOut` 处理分支在没有 caller frame 时，仍然先把 `bBreakNextScriptLine = true`，然后仅把 `ConditionBreakFrame` / `ConditionBreakFunction` 清空。之后 `ProcessScriptLine()` 在 `550-560` 的步进分支里会把“没有条件”解释成“当前行就允许停下”，于是顶层函数里执行 `StepOut` 时，不会继续运行到脚本返回，而是像 `StepIn` 一样在下一条脚本行立即再次进入 `bIsPaused`。现有自动化 `FAngelscriptDebuggerSteppingStepOutTest` 只覆盖 `RunScenario() -> Inner()` 的嵌套调用路径，fixture 也只有这一种两层栈结构，因此顶层 `StepOut` 语义没有任何回归护栏。 |
| 根因 | 当前步进状态被压缩成 `bBreakNextScriptLine + 可选条件函数/帧` 两个字段，但“顶层 `StepOut` 应继续运行直到当前脚本上下文退出”并不是“无条件下一行停下”的同义语义，缺少单独状态表达。 |
| 影响 | 在入口函数、事件处理函数或任何没有 caller 的脚本帧上执行 `StepOut`，调试器都会得到错误的停点行为。前端看到的是“已经 step out，但仍停在同一函数的下一行”，无法可靠运行到函数返回或脚本结束。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为步进状态引入显式的 `TopLevelStepOut` 语义，不再用“空条件 + `bBreakNextScriptLine`”冒充顶层 `StepOut`。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h/.cpp` 把当前步进控制重构成显式状态，例如 `EStepMode { None, StepIn, StepOver, StepOut, StepOutTopLevel }`，并记录 `StepOriginContext` / `StepOriginFunction` 或等价标识，避免再通过 `ConditionBreakFrame == -1` 表示多种不同语义。 2. `HandleMessage(EDebugMessageType::StepOut)` 在 `GetCallstackSize() >= 2` 时继续保留现有“等待返回到 caller”逻辑；当栈深 `< 2` 时，不要设置“下一行停下”，而是切到 `StepOutTopLevel`，表示“继续运行直到当前脚本上下文结束、抛异常、或命中显式 breakpoint”。 3. 在 `ProcessScriptLine()` 中对 `StepOutTopLevel` 单独分支：只要当前仍处在起始脚本上下文/函数，就忽略 line callback；不要因为条件为空而再次置 `bIsPaused = true`。 4. 在 `ProcessScriptStackPop()` 或脚本执行结束的统一通知点上清理 `StepOutTopLevel` 状态，确保顶层函数返回后不会把旧的步进状态泄漏到下一次脚本执行。若当前架构没有“执行完成”回调，需要补一个轻量 helper 专门用于复位 step state。 5. 为避免再次出现语义纠缠，把 `StepIn` / `StepOver` / `StepOut` 的停下判定统一收敛到单个 helper，例如 `ShouldPauseForStep(...)`，让“是否仍在原 frame/caller frame/顶层 context”都走同一套显式枚举，而不是散落在 `bBreakNextScriptLine` 旁边。 6. 新增顶层 `StepOut` 回归：使用一个没有嵌套调用的 fixture，在首行命中断点后立即发送 `StepOut`，断言执行会直接完成且中间不会再收到额外 `HasStopped(reason="step")`；同时保留现有嵌套 `StepOut` 用例，验证返回到 caller 之后仍会在调用点后停下。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerScriptFixture.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 预估工作量 | M |
| 风险 | 步进状态从两个字段重构成显式模式后，`StepOver` 与 data breakpoint 共享的 `bBreakNextScriptLine` 路径会暴露更多耦合；如果拆分不彻底，可能修掉顶层 `StepOut` 后又引入其它步进回归。 |
| 前置依赖 | 无，但建议与 `Issue-14` 的 data breakpoint 停止链路审计一起检查，避免两个特性继续共用同一布尔状态。 |
| 验证方式 | 1. 新增“顶层 `StepOut` 直达函数返回”自动化，断言不会出现多余的第二次 step stop。 2. 回归现有 `AngelscriptDebuggerSteppingStepOutTest`，确认嵌套 `Inner() -> RunScenario()` 仍会在 caller 返回点停下。 3. 回归 `StepIn` / `StepOver` 测试，确认显式 step mode 重构没有改变其它步进命令的既有行为。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-16 | Defect | 先修复顶层 `StepOut` 语义，避免调试器把“退出当前帧”错误执行成“下一行立即再停” |

---

## 发现与方案 (2026-04-08 13:21)

### Issue-17：补足 Analysis 发现 7 的修复方案：JIT 运行时异常只写日志，不会向 DebugServer V2 发送 `exception` 停止事件

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h` |
| 行号 | `StaticJITHeader.cpp:104-161`；`AngelscriptEngine.cpp:5087-5126, 5300-5317`；`AngelscriptDebugServer.cpp:440-462`；`StaticJITHeader.h:193-218`；`as_context.h:251-286` |
| 问题 | JIT 异常辅助函数 `SetException` / `SetNullPointerException` / `SetDivByZeroException` 等在 `StaticJITHeader.cpp:104-161` 中统一调用 `FAngelscriptEngine::HandleExceptionFromJIT(...)`，但这个入口在 `AngelscriptEngine.cpp:5314-5317` 只是简单转发到 `LogAngelscriptException(ExceptionString)`。解释器路径的 `LogAngelscriptException(asIScriptContext*)` 则会在 `5305-5310` 调用 `DebugServer->ProcessException(Context)`，后者才真正发出 `HasStopped(reason="exception")`。与此同时，运行时其实已经持有足够的 JIT 调试元数据：`FScriptExecution` 会把当前执行挂到 `tld->activeExecution`，`FScopeJITDebugCallstack` 持续记录 `Filename` / `FunctionName` / `LineNumber`，`GetStackTrace()` 也明确能从这条链重建 JIT 栈。也就是说，JIT 异常不是“没有上下文可用”，而是“异常上报链路根本没有接到 DebugServer”。 |
| 根因 | 解释器异常和 JIT 异常走了两条独立的上报路径，只有解释器路径保留了 `asIScriptContext* -> ProcessException()` 这段协议桥接；JIT 路径在进入引擎层时就把上下文降级成纯字符串，导致调试服务器再也无法知道“当前确实发生了一个可停下的脚本异常”。 |
| 影响 | 任何被 StaticJIT 覆盖的脚本函数一旦命中空指针、除零、越界、unbound function 等运行时错误，前端都不会收到 `HasStopped(reason="exception")`。同一段脚本在解释执行下能自动停住，在 JIT 路径下却只写日志或直接继续清理返回，异常调试语义发生确定性分叉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 JIT 异常从“只传字符串”升级成“携带执行上下文的统一脚本异常事件”，让 DebugServer 对解释器/JIT 使用同一套停止入口。 |
| 具体步骤 | 1. 改造 `FAngelscriptEngine::HandleExceptionFromJIT(...)` 签名，至少接收 `FScriptExecution&` 或等价的 JIT 执行描述对象，而不是只接收 `const ANSICHAR*`；`StaticJITHeader.cpp` 的所有异常 helper 都改为把 `Execution` 一并传下去。 2. 在引擎层定义统一异常载荷，例如 `FScriptExceptionEvent`，字段至少包含 `ExceptionText`、执行后端（Interpreter/JIT）、以及可选的 `asIScriptContext*` / `FScriptExecution*` / JIT top frame 信息。解释器 `LogAngelscriptException(asIScriptContext*)` 和新的 JIT 入口都产出同一种事件，而不是继续各走各的 side effect。 3. 在 `DebugServer` 中新增 `ProcessException` 的后端无关版本，例如 `ProcessScriptException(const FScriptExceptionEvent&)`；它负责统一生成 `FStoppedMessage{ Reason = "exception" }`，并在需要时把 JIT top frame 的 `Filename/LineNumber` 缓存在 session/debug target 中，供后续 `RequestCallStack` 与变量查看复用。 4. 如果 JIT 异常可能发生在非 game thread，不要直接跨线程调用 `PauseExecution()`；应把 `FScriptExceptionEvent` 投递到 game thread 或 `DebugServer::Tick()` 可消费的待处理队列，再由主线程执行停止与消息发送，避免引入新的线程竞争。 5. 与已有 `Issue-9` 的 JIT debug target 抽象合并设计，避免再单独发明一套“JIT callstack 临时缓存”；异常停点、`DebugBreak`/`ensure`/`check` 和未来 JIT breakpoint 都应该共享同一份 `activeExecution` 解析逻辑。 6. 补齐自动化：新增一个会在 JIT 覆盖函数里稳定触发异常的夹具，例如空指针访问或除零，用调试测试客户端断言能收到 `HasStopped(reason="exception")`，并且停止后的 call stack 顶帧指向触发行；同时保留解释器态同用例，验证两条后端结果一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下新增 JIT exception 集成测试 |
| 预估工作量 | L |
| 风险 | 一旦把 JIT 异常真正送进 DebugServer，会同时暴露 `CallStack`、变量解析、步进恢复等仍依赖 `asCContext` 的老假设；如果只修停点消息不修后续调试目标解析，前端仍可能在停住后看到空栈或错误变量。 |
| 前置依赖 | 建议与 `Issue-9` 和 `Issue-15` 的 JIT 调试宿主/调试目标抽象一起推进，避免为异常链路再建一套一次性桥接代码。 |
| 验证方式 | 1. 新增 JIT 异常集成测试，断言异常发生时收到 `HasStopped(reason="exception")`。 2. 停住后立即请求 `CallStack`，确认顶部帧对应 JIT `debugCallStack` 记录的函数与行号。 3. 对比解释器态与 JIT 态同一异常脚本，确认两者都会停下且异常文本一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-17 | Defect | 在显式 `DebugBreak` 链路之后尽快补齐 JIT 异常停点，消除解释器/JIT 的异常调试分叉 |

---

## 发现与方案 (2026-04-08 13:23)

### Issue-18：补足 Analysis 发现 10 的修复方案：`SetDataBreakpoints` 超出硬件上限后会静默降级，客户端永远不知道哪几个监视点真的生效

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:713-716`；`AngelscriptDebugServer.cpp:1061-1068, 1248-1263`；`AngelscriptDebugProtocolTests.cpp:174-227` |
| 问题 | 服务端在 `SetDataBreakpoints` 分支里直接把客户端发来的整组断点写进 `DataBreakpoints`，随后 `RebuildActiveDataBreakpoints()` 只复制 `min(DataBreakpoints.Num(), DATA_BREAKPOINT_HARDWARE_LIMIT)` 条，其中上限被硬编码为 `4`。也就是说，第 5 个及之后的 data breakpoint 不会进入任何硬件寄存器槽位，但协议层既没有返回“超限被拒绝”的响应，也没有逐项 verified/reason 字段告诉客户端实际生效集合。现有测试也只覆盖两个 data breakpoint 的二进制 round-trip，与“超限时服务端应如何反馈”完全脱节。 |
| 根因 | DebugServer 把硬件限制实现成运行时内部细节，却没有把“请求集合”和“实际已安装集合”建模成两个协议概念，导致超限只能在服务器内部静默截断。 |
| 影响 | 前端可以成功发送任意长度的 data breakpoint 列表，却无法知道哪些监视点已经被服务端接受、哪些只是停留在权威数组里尚未装入硬件、哪些根本因为硬件上限而暂不可用。用户看到的是“界面上全都加上了 watchpoint，但只有前四个可能触发”，这会直接误导调试结论。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `SetDataBreakpoints` 改成“请求 -> 服务端裁剪/分配 -> 明确回包已接受与已拒绝项”的双阶段协议，不再让超限行为静默发生。 |
| 具体步骤 | 1. 在 `DebugServer` 中显式区分 `RequestedDataBreakpoints` 与 `InstalledDataBreakpoints`：前者保存客户端意图，后者表示当前已装入 `Dr0-Dr3` 的前四项，避免继续让 `DataBreakpoints` 同时承担“用户请求”和“硬件现状”两种含义。 2. 为协议增加明确的安装结果回包。优先方案是新增 `EDebugMessageType::SetDataBreakpointsResult`，负载包含每个 `Id` 的 `bVerified`、`InstalledSlot`、`Reason`；如果必须复用现有消息类型，也要通过版本化结构扩展 `FAngelscriptDataBreakpoint`，不能继续只回传原始请求。 3. `HandleMessage(EDebugMessageType::SetDataBreakpoints)` 在调用 `RebuildActiveDataBreakpoints()` 之后，立即生成结果回包：前四个可安装项标记为 verified，其余项返回明确失败原因，例如 `hardware data breakpoint limit is 4`；不要等到运行时命中或超时才让客户端自己猜。 4. 当某个已安装 data breakpoint 被移除、出作用域或命中后禁用时，重新从 `RequestedDataBreakpoints` 里挑选下一个待安装项补位，并再次向客户端广播新的安装结果，确保 UI 能反映“某个原先未安装的 watchpoint 现在已经进入硬件槽位”。 5. 把 `ClearDataBreakpoints` 的语义从“仅删除已触发/移除项”扩展到“同时更新剩余请求的安装状态”，避免客户端删除一个已安装项后，排队中的第 5 项升格为已安装却没有任何确认通知。 6. 补齐自动化：新增一个至少包含 5 条 data breakpoint 的协议/集成测试，断言服务端会明确报告只有前四条 verified；再删除其中一条，验证第 5 条会被补位安装并收到新的结果消息。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下 data breakpoint 集成测试 |
| 预估工作量 | M |
| 风险 | 引入新的结果回包会牵动现有调试客户端实现；如果版本协商处理不好，旧客户端可能忽略新消息或误解析扩展结构，因此需要和 `DebugServerVersion` / adapter version 一起做兼容门控。 |
| 前置依赖 | 建议建立在 `Issue-11` 的 per-session 协议状态之上，这样不同客户端的 data breakpoint 安装结果不会再彼此污染。 |
| 验证方式 | 1. 新增 5 条以上 data breakpoint 的协议测试，确认服务端会显式回报 verified/overflow 状态。 2. 删除一个已安装项后，再验证排队项能被补位并收到更新通知。 3. 回归现有 1-4 条 data breakpoint 场景，确认在未超限时协议与命中行为保持不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-18 | Architecture | 在 data breakpoint 基础链路稳定后补齐安装结果回包，消除超限静默降级 |

---

## 发现与方案 (2026-04-08 13:31)

### Issue-19：补足 Analysis 发现 75 的修复方案：`GetAngelscriptExecutionFileAndLine()` 的 JIT 分支把空指针判定写反

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h` |
| 行号 | `AngelscriptEngine.cpp:5632-5697`；`Bind_UObject.cpp:659-666`；`as_context.h:251-286`；`StaticJITHeader.h:194-217` |
| 问题 | 同一文件里的 `GetAngelscriptExecutionPosition()` 在 JIT 路径上先取 `tld->activeExecution->debugCallStack`，并在 `DebugStack == nullptr` 时立即返回空字符串；但紧接着的 `GetAngelscriptExecutionFileAndLine()` 却在 `DebugStack == nullptr` 分支里直接解引用 `DebugStack->Filename` 和 `DebugStack->LineNumber`。这形成了确定性的镜像错误：当 JIT 调试栈为空时会空指针解引用；当调试栈实际存在时又落入 `5680-5681` 的默认分支，把输出强制写成空文件和 `-1`。`Bind_UObject.cpp` 的 literal asset redirector 元数据正是直接调用这条 API 并写入 `ScriptAssetFilename` / `ScriptAssetLineNumber`。 |
| 根因 | 引擎已经有一份正确的 JIT 位置读取逻辑，但 `GetAngelscriptExecutionFileAndLine()` 手工复制该逻辑时把 `DebugStack` 的空值条件写反，且没有先为输出参数建立安全默认值。 |
| 影响 | JIT 覆盖函数下，源码定位结果会在“崩溃”和“稳定写空元数据”之间二选一，而不会得到正确的文件/行号。任何依赖 `GetAngelscriptExecutionFileAndLine()` 的调用者都会和解释器路径产生行为分叉，literal asset 的源码追踪尤其直接受影响。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把 JIT 分支修成与 `GetAngelscriptExecutionPosition()` 同一语义，再把两条 API 的位置提取收敛到单一 helper，避免再次出现镜像分支错误。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.cpp` 进入 `GetAngelscriptExecutionFileAndLine()` 时先统一初始化 `OutFilename = TEXT(\"\")`、`OutLineNumber = -1`，让空上下文路径天然安全。 2. 把 `5672-5676` 的条件改成“`DebugStack != nullptr` 时读取文件和行号”，`DebugStack == nullptr` 时直接保留默认值返回，不再解引用空指针。 3. 进一步抽出一个内部 helper，例如 `TryGetActiveScriptSourceLocation(FString& OutFilename, int32& OutLineNumber)`，让 `GetAngelscriptExecutionPosition()` 和 `GetAngelscriptExecutionFileAndLine()` 共用同一份 JIT/Interpreter 分支，彻底消除复制粘贴造成的逻辑漂移。 4. 对 `Bind_UObject.cpp` 这类调用点补一个最小防御：当返回文件为空或行号 `< 0` 时，不要写入伪造的 metadata 值，而是保留为空并可选记录一次诊断，避免把错误位置信息固化到资产元数据。 5. 新增回归测试，优先选择纯 C++ 可控路径：直接构造 `FScriptExecution` 与 `FScopeJITDebugCallstack`，调用 `GetAngelscriptExecutionFileAndLine()`，断言在有调试栈时返回正确文件/行号、无调试栈时返回空字符串和 `-1` 且不崩溃；如果现有测试框架更适合集成验证，再追加一个 literal asset metadata 场景，确认 JIT 下写出的 `ScriptAssetFilename` / `ScriptAssetLineNumber` 与脚本位置一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增或扩展位置解析回归测试 |
| 预估工作量 | S |
| 风险 | 如果 helper 抽取时没有完全对齐解释器路径的默认值语义，可能把现有“空上下文返回空字符串/`-1`”的约定改坏；另外需要确认 metadata 调用点对“无位置信息”场景是否已有上层容错。 |
| 前置依赖 | 无。该修复应在继续扩展 JIT 调试元数据之前优先落地。 |
| 验证方式 | 1. 新增纯 C++ 回归测试，覆盖“有 JIT 调试栈”和“无 JIT 调试栈”两条路径。 2. 人工或自动化验证 literal asset metadata 在 JIT 覆盖函数内能写出正确 `ScriptAssetFilename` / `ScriptAssetLineNumber`。 3. 回归解释器态调用，确认非 JIT 路径行为不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-19 | Defect | 尽快修复，先恢复 JIT 源码定位 API 的基本正确性并堵住潜在空指针崩溃 |

---

## 发现与方案 (2026-04-08 13:33)

### Issue-20：补足 Analysis 发现 32 的修复方案：暂停态收到 `StopDebugging` 或断线时仍会错误补发 `HasContinued`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:667-699, 724-742, 915-923`；`AngelscriptDebuggerSmokeTests.cpp:16-133`；`AngelscriptDebuggerBreakpointTests.cpp:147-278` |
| 问题 | `PauseExecution()` 进入暂停后会阻塞在 `while (bIsPaused)`，但循环退出后无条件发送 `HasContinued`。这和实际退出原因不一致，因为让 `bIsPaused` 变成 `false` 的不仅有真正的 `Continue`/step 命令，还包括 `StopDebugging` 分支直接把 `bIsPaused = false`，以及断线清理在最后一个调试客户端移除时同样把 `bIsPaused = false`。结果是当前端在停点期间结束会话，或客户端异常断开导致会话被撤销时，协议流仍会伪造一条 `continued` 事件，仿佛目标在当前调试会话控制下恢复了运行。现有 smoke/breakpoint tests 只验证功能结果，没有任何事件序列断言能拦住这条错误消息。 |
| 根因 | `PauseExecution()` 只用一个布尔量表示“是否仍在暂停”，却没有记录“暂停为什么结束”；消息发送层因此把所有退出路径都错误折叠成“继续执行”。 |
| 影响 | 调试前端会把“结束会话”“最后一个调试客户端断开”误判成“目标继续运行”，从而污染状态机、错误解锁 UI，并可能在后续仍按活动会话去发 `stackTrace` / `variables` / `continue` 请求。这是直接的 DAP 生命周期语义错误。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 给暂停循环增加显式退出原因，只在“确实恢复执行”时发送 `HasContinued`，而不是把所有解锁路径都当成 continue。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h/.cpp` 为暂停循环引入显式状态，例如 `EPauseExitReason { None, Continue, Step, StopDebugging, LastClientDisconnected, Shutdown }`，进入 `PauseExecution()` 前重置为 `None`。 2. 把 `Continue`、`StepIn`、`StepOver`、`StepOut` 这些真正会恢复脚本执行的消息分支改成先设置 `PauseExitReason = Continue/Step`，再把 `bIsPaused = false`；`StopDebugging` 分支和 `724-742` 的断线清理分支则分别设置为 `StopDebugging` / `LastClientDisconnected`。 3. 修改 `PauseExecution()` 尾部逻辑：只有 `PauseExitReason` 属于 `Continue` 或 `Step` 时才发送 `HasContinued`；若退出原因是 stop/disconnect/shutdown，则直接返回，不再伪造继续事件。 4. 为避免旧值泄漏到下一次停点，`PauseExecution()` 返回前必须清空 `PauseExitReason`，并在任何非正常退出分支上同步清理 `CallstackRequests` 等仅对暂停态有意义的临时状态。 5. 如果后续按 `Issue-11` 推进 per-session 会话模型，这个退出原因也要下沉到 session 维度，避免一个 client 的 `StopDebugging` 仍影响其他 client 的事件流；本次热修至少先把 server-global 语义修正确。 6. 补齐自动化：新增“命中 breakpoint 后发送 `StopDebugging`”回归，断言客户端只收到 `HasStopped`，不会再收到额外 `HasContinued`；再保留一个正常 `Continue`/step 用例，确认真正恢复执行时仍会收到 `HasContinued`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | S |
| 风险 | 当前 `PauseExecution()` 是全局暂停模型；若只补退出原因而不梳理多客户端/多线程语义，仍可能在复杂场景里出现“谁有权结束暂停”的歧义。因此本修复应保持聚焦，只解决消息正确性，不顺手改变会话所有权模型。 |
| 前置依赖 | 无，但后续应与 `Issue-11` 的 session state 重构并轨，避免再次出现全局暂停态污染。 |
| 验证方式 | 1. 新增 breakpoint 集成测试，在停点期间发送 `StopDebugging`，断言不会收到 `HasContinued`。 2. 增加断线场景回归，验证最后一个调试客户端异常断开时同样不会广播 `HasContinued`。 3. 回归正常 `Continue` / step 流程，确认真实恢复执行时仍然发送 `HasContinued`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-20 | Defect | 在 transport/session 重构之前先修复事件序列，避免前端被伪造的 `continued` 状态误导 |

---

## 发现与方案 (2026-04-08 13:34)

### Issue-21：补足 Analysis 发现 63 的修复方案：发送队列用固定 10 秒首包年龄判死连接，慢客户端在大包同步时会被误踢

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:703-707`；`AngelscriptDebugServer.cpp:724-747, 1485-1515, 2159-2203, 2845-2858`；`AngelscriptDebugTransportTests.cpp:18-231` |
| 问题 | 客户端清理分支除了检查 `GetConnectionState() != SCS_Connected`，还把“队首消息第一次发送尝试距离现在超过 10 秒”直接视为应断开的坏连接。`FQueuedMessage` 只保存 `FirstTry`，`TrySendingMessages()` 首次发送时设一次后就不再更新。与此同时，`RequestDebugDatabase` 会立刻触发 `SendDebugDatabase()` 下发 `DebugDatabaseSettings` + 多段 `DebugDatabase`，随后还会进入 `SendAssetDatabase()` 发送 `AssetDatabaseInit`、按 50 个资源分批的 `AssetDatabase`，最后才到 `AssetDatabaseFinished`。这意味着只要客户端读包稍慢、网络短时背压、或初始化数据库本身需要超过 10 秒，服务端就会在仍处于 `SCS_Connected` 的情况下主动关闭该连接。现有 transport tests 只覆盖 envelope buffer 级序列化/反序列化，没有任何“慢读客户端 + 长时间出站队列”场景。 |
| 根因 | DebugServer 把“首包年龄超过 10 秒”错误地等价成“对端已死”，既没有区分“完全没有前进”和“正在缓慢前进”，也没有把大量初始化消息纳入 transport backpressure 设计。 |
| 影响 | 首次接入、全量 debug database 同步、asset registry 大量变更或弱网络远端调试时，客户端会被随机误踢。前端表面上看到的是 database 初始化中途断流、后续 breakpoint/variables/stackTrace 请求全部失效，但根因其实是服务端自己的发送策略把慢客户端错杀了。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用“无进展超时 + 队列上限”替代“固定 10 秒首包年龄”，让 transport 层按真实 backpressure 行为判断坏连接。 |
| 具体步骤 | 1. 在 `FQueuedMessage` 或 per-client transport state 中把 `FirstTry` 升级为“最近一次成功前进时间”，例如 `LastProgressTime`，并额外维护 `TotalQueuedBytes` 或等价指标；任何成功发送字节、整包出队、或入站消息处理都要刷新该时间。 2. 修改 `724-728` 的移除条件：不要再仅凭 `Queue[0].FirstTry < Now - 10` 断开客户端。新的判定应至少要求“连接状态异常”或“累计无进展时间超过阈值且排队字节超过上限”；阈值与上限最好提成具名常量，避免继续散落 magic number。 3. 将 `TrySendingMessages()` 与 `Issue-10` 的 partial-send 修复合并设计，确保“成功发出部分字节”也算进展，而不是因为一条大消息需要多轮发送就被算作 10 秒未动。 4. 对 `RequestDebugDatabase` / `SendAssetDatabase` 这类大流量路径增加轻量节流或回压保护：例如每 tick 只推进有限数量的 queued envelopes，或当 `TotalQueuedBytes` 超过阈值时暂停继续生成新的 database chunk，避免服务端自己无限加压再把对端踢掉。 5. 为 transport 层引入可测试的时间来源，例如 `NowSeconds` callback 或 test seam，这样 `AngelscriptDebugTransportTests` 可以在不真实等待 10 秒的情况下覆盖“短暂背压不会断开、长期无进展且队列爆炸才断开”的分支。 6. 新增回归测试：构造 fake socket 先连续多轮返回 `EWOULDBLOCK`/0 进展再恢复，验证服务端不会因短时背压断线；再构造“持续无进展 + 队列超限”场景，确认真正的坏连接仍会被清理。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 预估工作量 | M |
| 风险 | 若只放宽超时而不引入队列上限，真正不消费数据的客户端会改成慢性内存膨胀；若节流策略插入位置不对，也可能打乱现有 database/asset 初始化消息顺序。 |
| 前置依赖 | 建议与 `Issue-10` 的 partial-send 状态机一并实现，因为“是否有发送进展”必须建立在正确的字节偏移跟踪之上。 |
| 验证方式 | 1. 新增慢客户端 transport 测试，验证大包同步期间不会被固定 10 秒误断开。 2. 新增真正无进展且队列超限的负向测试，确认坏连接仍会被回收。 3. 人工在大型项目上触发 `RequestDebugDatabase`，观察 `DebugDatabaseFinished` / `AssetDatabaseFinished` 能稳定送达，不再随机中途掉线。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-21 | Defect | 在 transport 修复阶段一并处理，避免大包同步和弱网络场景继续随机掉线 |

---

## 发现与方案 (2026-04-08 13:39)

### Issue-22：补足 Analysis 发现 25/68 的修复方案：`RequestEvaluate` 在截断请求和失败响应上都会把未初始化数据发到 wire

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:1107-1128`；`AngelscriptDebugServer.h:416-437`；`AngelscriptDebugProtocolTests.cpp:121-170` |
| 问题 | `HandleMessage(RequestEvaluate)` 先读 `FString Path`，然后把未初始化的局部变量 `DefaultFrame` 直接交给 `*Datagram << DefaultFrame`。这个分支从头到尾没有检查 `Datagram->IsError()`、`Datagram->AtEnd()` 或 body 长度，因此只要客户端发来“只有表达式字符串、缺少 frame int32”的截断消息，`DefaultFrame` 就会带着未定义栈值进入 `GetDebuggerValue(...)`。同一分支里返回包 `FAngelscriptVariable Var` 也没有全字段初始化；当求值失败时，代码仍然无条件 `SendMessageToClient(..., Evaluate, Var)`。而 `FAngelscriptVariable::operator<<` 在 V2 协议下总会序列化 `ValueAddress` 和 `ValueSize`，这两个字段在失败路径上从未赋值。当前 protocol tests 只覆盖成功的 `FAngelscriptVariable` V1/V2 round-trip，没有任何 `RequestEvaluate` body 截断或失败回包字段归零的负向用例。 |
| 根因 | 协议处理层把 `RequestEvaluate` 当成“总能读出完整 body、总能构造合法返回值”的 happy-path 实现：请求解析缺少字段级校验，响应结构体又不是零初始化对象。 |
| 影响 | 恶意或损坏客户端可以通过一个 envelope 级合法、body 级截断的 `RequestEvaluate` 把服务器带入未定义 frame 解析；即使只是普通求值失败，V2 客户端也会收到未初始化的 `ValueAddress` / `ValueSize`。这会造成错误 frame 求值、随机 monitor address、协议数据泄露以及后续 data breakpoint 建立在垃圾地址上的连锁错误。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `RequestEvaluate` 改成“先验证 body 完整性，再发送显式初始化的结果对象”，彻底消除未初始化 frame 和未初始化 V2 字段。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 把 `DefaultFrame` 改为显式初始化，例如 `int32 DefaultFrame = 0;`，并在读取 `Path`、`DefaultFrame` 之后立即检查 `Datagram->IsError()` 与 `Datagram->AtEnd()`；任何字段缺失或多余尾字节都按 malformed request 处理，不进入 `GetDebuggerValue(...)`。 2. 为 `RequestEvaluate` 抽一个小 helper，例如 `bool TryReadEvaluateRequest(FArchive& Ar, FString& OutPath, int32& OutFrame)`，把 body-level 校验集中到一处，避免 `RequestVariables`、后续协议扩展继续复制同样的裸读模式。 3. 把 `FAngelscriptVariable` 变成稳定的零初始化对象：至少为 `ValueAddress`、`ValueSize` 提供 in-class 默认值，或在 `RequestEvaluate`/`RequestVariables` 分支上统一使用值初始化 `FAngelscriptVariable Var{}`，确保失败路径发回的 V2 字段恒为 `0`。 4. 明确失败语义。短期最小修复可以保持复用 `Evaluate` 响应类型，但要求失败时 `Name/Value/Type` 为空、`bHasMembers=false`、`ValueAddress=0`、`ValueSize=0`；中期更稳妥的方案是为 `Evaluate` 增加 `bSuccess/ErrorText` 或单独的 error message type，避免客户端只能靠空值猜测失败。 5. 扩充测试：在 `AngelscriptDebugProtocolTests.cpp` 新增 body-level 反序列化负例，覆盖“缺失 DefaultFrame”“多余尾字节”“V2 失败结果字段归零”；如果现有 protocol unit tests 不方便直接走 `HandleMessage`，就给 `RequestEvaluate` 的解析 helper 做 direct test，并在 `AngelscriptTest/Debugger` 增加一个真实客户端发送截断 body 的集成回归。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h` |
| 预估工作量 | S |
| 风险 | 如果只在服务端静默吞掉 malformed request 而不给客户端任何可观测信号，前端仍会把“协议错误”和“正常 evaluate miss”混在一起；因此最小修复至少也要把失败结果做成确定值，而不是继续靠默认构造碰运气。 |
| 前置依赖 | 无。该修复可以独立落地，并为后续 request token / 错误响应扩展打基础。 |
| 验证方式 | 1. 新增负向测试，发送缺失 `DefaultFrame` 的 `RequestEvaluate` body，确认服务端不会读取未初始化 frame，也不会崩溃或返回随机值。 2. 在 V2 协议下验证 evaluate 失败响应的 `ValueAddress` / `ValueSize` 恒为 `0`。 3. 回归现有成功 evaluate/variables 场景，确认合法请求的返回值与地址语义不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-22 | Defect | 尽快修复，先堵住 body 截断和失败回包把未初始化值暴露给客户端的协议漏洞 |

---

## 发现与方案 (2026-04-08 13:41)

### Issue-23：补足 Analysis 发现 8 的修复方案：无效 `debugger frame` 会被静默改写成 frame 0

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:2282-2375, 2692-2700, 1081-1128` |
| 问题 | `ResolveDebuggerFrame()` 先根据当前可见 script/blueprint 栈构造 `ResolvedFrames`，但当 `DebuggerFrame` 越界时直接返回 `0`。`GetDebuggerValue()` 无条件把这个返回值写回 `Frame` 并继续解析表达式；`RequestEvaluate` 会把客户端提供的 `DefaultFrame` 走这条路径，`RequestVariables` 则允许通过 `"{Frame}:Path"` 语法把 frame 编进 `Path`。这意味着一旦前端持有了过期 frame id，或在步进/异常后栈深变化，服务端不会返回“frame 无效”，而是静默把请求重定向到顶层 frame 继续求值。 |
| 根因 | frame 解析层把“未找到目标 frame”编码成了一个合法 frame index，导致上层无法区分“显式请求顶层 frame”和“请求了不存在的 frame”。 |
| 影响 | 这是典型的 silent wrong-answer defect：客户端拿到的 `Evaluate` / `Variables` 响应结构完全合法，但值来自错误栈帧。用户会在 watch、hover、locals 面板里看到“似乎正常、实际上错帧”的结果，DAP 侧也无法据此报告 `invalid frameId`，调试可信度会被直接破坏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 frame 解析接口改成“可失败”的 contract，禁止再把越界 frame 折叠成 frame 0。 |
| 具体步骤 | 1. 将 `ResolveDebuggerFrame()` 改为显式失败返回，例如 `TOptional<int32> TryResolveDebuggerFrame(int32 DebuggerFrame)` 或 `bool ResolveDebuggerFrame(int32 DebuggerFrame, int32& OutResolvedFrame)`；当 `ResolvedFrames` 不包含目标 index 时返回失败，而不是返回 `0`。 2. 修改 `GetDebuggerValue()` 与 `GetDebuggerScope()`：只有 frame 解析成功才继续 `ParseExpression()` 和变量枚举；失败时立即返回 `false`，并把 `InOutFrame` 置为 `INDEX_NONE` 或保留原始请求值，避免上层误以为已经成功重写。 3. 在 `RequestEvaluate` / `RequestVariables` 分支上补显式失败语义。最小修复可以继续复用原消息类型，但要求 invalid frame 时返回确定的空结果且附带可判别的错误文本；更稳妥的做法是为这两类响应增加 `bSuccess/ErrorText`，让客户端能够把“frame 无效”与“表达式不存在/成员为空”区分开。 4. 把 scope path 里的 frame 解析与 `DefaultFrame` 解析统一收口到同一个 helper，避免未来只修 `RequestEvaluate` 而漏掉 `RequestVariables("{Frame}:...")` 这条隐式入口。 5. 补齐自动化：新增一个至少两帧的调试 fixture，在第一次停点后缓存较深 frame id，继续步进到栈收缩，再用旧 frame id 分别发 `RequestEvaluate` 与 `RequestVariables`，断言服务端返回明确 invalid-frame 结果，而不是顶层 frame 的值。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 预估工作量 | M |
| 风险 | 如果只把无效 frame 改成“返回空结果”而不补错误标识，客户端仍然难以区分“真的没有子成员”和“frame 已失效”；因此协议层最好同步增加可判别失败信号。 |
| 前置依赖 | 无，但建议和 `Issue-22` 的请求解析/失败响应统一设计，避免连续两次重构 `Evaluate` / `Variables` 返回格式。 |
| 验证方式 | 1. 新增回归测试，使用过期 frame id 发 `RequestEvaluate`，确认不会再返回 frame 0 的值。 2. 对 `RequestVariables("{Frame}:")` 做同样的负例验证，确认不会静默展示顶层 locals。 3. 回归现有 stepping/breakpoint tests，确认合法 frame id 的 callstack 深度与变量查看行为保持不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-23 | Defect | 在 `Evaluate/Variables` 协议修复阶段一并处理，先消除错帧但看似成功的静默错误 |

---

## 发现与方案 (2026-04-08 13:43)

### Issue-24：补足 Analysis 发现 39 的修复方案：JIT 停点没有可供 `Evaluate` / `Variables` 使用的 frame materialization

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `StaticJITHeader.h:193-218`；`AngelscriptEngine.cpp:5087-5121`；`AngelscriptDebugServer.cpp:2348-2603, 2692-2835, 1081-1128` |
| 问题 | `FScopeJITDebugCallstack` 只保存 `Filename`、`FunctionName`、`ThisObject`、`LineNumber` 和链表指针，没有 locals、type id、module/frame id 或 bytecode PC。引擎 `GetStackTrace()` 也只是把这条链转成文本栈。相比之下，DebugServer 的 `GetDebuggerValue()` / `GetDebuggerScope()` 在 script frame 分支里完全依赖 `asCContext`：locals 通过 `GetVarCount/GetAddressOfVar`，`this` 通过 `GetThisPointer/GetThisTypeId`，module scope 通过 `GetFunction(Frame)->GetModule()` 解析。源码里没有任何一条路径会把 `activeExecution->debugCallStack` 转换成这些 API 可消费的 frame/value 模型。结果是即使前面的问题把 JIT `HasStopped` 和 `CallStack` 补齐，停住之后的 `Evaluate` / `Variables` 仍然没有数据来源。 |
| 根因 | JIT 调试信息目前只被设计成“文本定位辅助”，没有被设计成“可观察调试状态”；DebugServer 又把变量查看严格绑定在解释器 `asCContext` 上，缺少 JIT frame 到 debugger frame 的 materialization/deopt 层。 |
| 影响 | 当前架构无法形成真实的 JIT 调试闭环：JIT 函数最多只能停住和显示一层调用栈文本，locals、`this`、module variables、member evaluation 仍然不可用。继续只修 stop/callstack 而不补这层 materialization，会让前端表现成“能停但看不到变量”，与解释器路径形成长期能力分叉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 JIT 停点上引入明确的 frame materialization/deopt 层，把当前执行位置转成 DebugServer 可复用的 debugger frame，而不是继续让 `Evaluate` / `Variables` 直接依赖 `asCContext`。 |
| 具体步骤 | 1. 为 StaticJIT 增加 debugger safe-point 元数据，而不是只保留 `FScopeJITDebugCallstack` 文本链。每个可停点位置至少要记录：脚本函数 id、bytecode offset 或源码行到 bytecode 的映射、`this` 类型/地址、以及 live locals 的名称/类型/存储槽描述。生成这些元数据的入口应放在 `AngelscriptStaticJIT.cpp` / `AngelscriptBytecodes.cpp` 的 codegen 路径，与现有 `SCRIPT_DEBUG_CALLSTACK_LINE` 同步产出。 2. 在运行时新增 `MaterializeDebuggerTargetFromJIT(FScriptExecution&)` 一类 helper：当 JIT 因 breakpoint/exception/DebugBreak 停下时，根据 safe-point 元数据构造一个临时 debugger target。优先方案是 materialize 成可查询的 `FDebuggerFrameSnapshot` 集合；如果当前代码基线更适合复用现有 API，也可以把 safe-point 映射成临时 `asCContext` / frame snapshot，但必须是明确设计的一层，不是散落在 `SendCallStack`、`GetDebuggerValue` 里的 ad-hoc 特判。 3. 把 `GetDebuggerValue()`、`GetDebuggerScope()`、`SendCallStack()` 统一改造成 backend-neutral：先解析“当前 active debug target 是 Interpreter 还是 JITMaterialized”，再分别走对应 frame 访问器。解释器态继续使用现有 `asCContext`；JIT 态则从 materialized frame 读取 locals、`this`、module 和 member address。 4. 对于当前尚未能 materialize 的 JIT 情况，不允许继续默默返回空 scope 或错误帧。需要有显式的 unsupported/fallback 信号，让客户端知道“当前 JIT frame 暂不支持 variables/evaluate”，避免形成 silent wrong-answer。 5. 把这项工作与 `Issue-15`、`Issue-17`、`Issue-23` 串成一个完整链路：只有在真实 JIT 运行态可启动 DebugServer、JIT stop 能进入统一 debug target、frame 解析不再静默错帧的前提下，这里的 materialized locals 才有意义。 6. 新增非 editor/JIT 集成测试：在同一脚本源码行分别用解释器和 JIT 路径停下，比较 `%local%`、`this`、模块变量和一个成员表达式的 `RequestVariables` / `RequestEvaluate` 结果，要求类型、值和可展开成员数一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下新增 JIT variables/evaluate 集成测试 |
| 预估工作量 | XL |
| 风险 | 这是跨 codegen、runtime 和 debugger 的架构改造。若 safe-point 元数据设计不足，后续 step、异常、data breakpoint 和 getter evaluation 都会被迫再次重构；若一次性追求完整 deopt 而没有先定义最小 materialized frame contract，开发周期会失控。 |
| 前置依赖 | 建议先完成 `Issue-15` 和 `Issue-17`，至少让真实 JIT 运行态能够稳定进入 DebugServer 停止态，再推进变量 materialization。 |
| 验证方式 | 1. 新增 JIT 集成测试，在停点后请求 `%local%` / `%this%` / `%module%`，确认不再返回空结果或错误帧。 2. 同一脚本在解释器态和 JIT 态的 `Evaluate` / `Variables` 结果做逐字段比对，确认语义一致。 3. 回归已有解释器态 debugger tests，确保 backend-neutral 抽象没有影响现有 `asCContext` 路径。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-24 | Architecture | 在 JIT stop/callstack 基线稳定后立即规划，不然 JIT 调试仍然只能“停住但看不到变量” |
---

## 发现与方案 (2026-04-08 13:53)

### Issue-25：`BindAssetRegistry()` 用裸 `this` 注册长期 delegate，`DebugServer` 销毁后会留下 use-after-free 路径

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 行号 | `AngelscriptDebugServer.cpp:419-432, 2078-2157`；`AngelscriptDebugServer.h:737-738` |
| 问题 | `FAngelscriptDebugServer::~FAngelscriptDebugServer()` 只移除 vectored exception handler 并销毁 `Listener`，没有解除任何 `AssetRegistry` 订阅。与之相对，`BindAssetRegistry()` 会在 `2093-2134` 用 `AddLambda([this]...)` 绑定 `OnAssetAdded`、`OnAssetRemoved`、`OnAssetRenamed`，并在 `2140-2150` 用 `AddLambda([this, BindAssetRegistryChanges]...)` 绑定 `OnFilesLoaded`。类里唯一的状态位只有 `bAssetRegistryBound`，没有保存 `FDelegateHandle`，也没有任何 `Remove`/`RemoveAll` 调用。只要 `SendAssetDatabase()` 触发过 `BindAssetRegistry()`，后续 asset registry 事件就会继续回调已经销毁的 `FAngelscriptDebugServer`，lambda 内部还会直接访问 `ClientsThatWantDebugDatabase` 并调用 `SendMessageToClient(...)`。 |
| 根因 | `DebugServer` 把 asset database 推送实现成一次性“注册后永不解绑”的 lambda，但该对象本身是可销毁的运行时实例，生命周期与 `AssetRegistry` 全局委托不一致。 |
| 影响 | 这不是普通的重复订阅问题，而是明确的生命周期破坏：一旦引擎关闭、测试销毁 clone/full engine、或 runtime 重新创建 `DebugServer` 后 asset registry 再发事件，旧 delegate 就会在悬空 `this` 上执行，结果可能是 use-after-free、向已释放 socket 容器写队列、甚至把后续 asset 事件随机打进新的 `DebugServer` 状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 asset registry 绑定改成“可解绑的显式订阅”，由 `FAngelscriptDebugServer` 持有 `FDelegateHandle` 并在析构/停用时统一移除。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 新增四个 `FDelegateHandle` 成员，例如 `OnAssetAddedHandle`、`OnAssetRemovedHandle`、`OnAssetRenamedHandle`、`OnFilesLoadedHandle`，并增加私有 helper：`BindAssetRegistry()` 只负责注册一次，`UnbindAssetRegistry()` 负责按 handle 精确移除。 2. 将 `2078-2157` 的 `AddLambda([this]...)` 改成保存返回的 delegate handle；`OnFilesLoaded` 回调在触发后若只需要一次，应在回调体内先移除自己的 handle，再调用 `BindAssetRegistryChanges()`，避免 asset registry 尚未 ready 时重复叠加注册。 3. 在 `~FAngelscriptDebugServer()` 中，在销毁 `Listener` 和 socket 之前先调用 `UnbindAssetRegistry()`；这样即使 asset registry 随后继续广播，也不会再命中已析构对象。 4. 若 `DebugServer` 未来支持 session 级关闭但对象仍常驻，也要在“没有任何订阅者且不再需要 asset push”时允许主动解绑，而不是把全局 editor 事件永久挂在 server 上。 5. 为测试增加一个 seam：暴露绑定计数或仅测试可见的 `IsAssetRegistryBoundForTesting()`，再在 transport/protocol 测试里构造“请求 asset database -> 销毁 debug server -> 触发 asset registry 事件”的回归，断言不会再访问已释放对象，也不会发生重复广播。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` 或新增 asset-registry 生命周期测试文件 |
| 预估工作量 | M |
| 风险 | 若只在析构解绑而不处理 `OnFilesLoaded` 的一次性注册，仍可能在 asset registry 加载阶段多次叠加 asset change delegate；同时需要确认回调移除发生时 `AssetRegistry` 模块仍然可安全访问。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增回归测试，先请求 asset database，再销毁 `DebugServer`，随后模拟 asset registry 广播，确认不会崩溃也不会再有消息入队。 2. 运行重复创建/销毁 engine 的测试场景，确认 delegate 数量不会随轮次增长。 3. 回归现有 `RequestDebugDatabase`/asset database 流程，确认正常订阅时仍能收到增量资产更新。 |

### Issue-26：`FJITDatabase::Clear()` 会永久抹掉进程级 StaticJIT 注册表，后续 engine 实例无法再次命中已编进二进制的 JIT 函数

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptStaticJIT.cpp:24-39`；`StaticJITHeader.cpp:19-47`；`PrecompiledData.cpp:527-540`；`AngelscriptEngine.h:112-136`；`AngelscriptEngine.cpp:1533-1556, 1591-1602` |
| 问题 | `FStaticJITFunction` 与各类 `FJitRef_*` 的静态构造函数会在进程启动时把 compiled JIT entry 和 lookup 指针注册进 `FJITDatabase::Get()`（`StaticJITHeader.cpp:38-87`）。但 engine 初始化末尾只要 `PrecompiledData != nullptr`，就会在 `1591-1602` 无条件执行 `FJITDatabase::Clear()`，把 `Functions`、`FunctionLookups`、`GlobalVarLookups`、`TypeInfoLookups`、`PropertyOffsetLookups` 全部清空。后续 `FAngelscriptPrecompiledFunction::Create()` 又严格依赖 `FJITDatabase::Get().Functions.Find(Id)` 决定是否给 `jitFunction` / `jitFunction_Raw` 赋值；一旦查不到，就退回 bytecode 解释执行（`PrecompiledData.cpp:527-540`）。与此同时，`FAngelscriptEngine` 明确支持 `Full`、`Clone` 和 `CreateForTesting(...)` 多实例创建（`AngelscriptEngine.h:112-136`），而 `FStaticJITCompiledInfo` 只允许一个全局 compiled info 常驻（`StaticJITHeader.cpp:19-35`），没有任何“Clear 之后重新注册 compiled entries”的入口。 |
| 根因 | StaticJIT 编译产物被建模成进程级静态注册表，但 engine 初始化收尾又把同一个全局注册表当成“本轮加载完就可释放的临时状态”直接清空，两种生命周期模型互相冲突。 |
| 影响 | 第一份 engine 在当前进程里还能使用编进二进制的 JIT entry；只要它走完初始化尾声，第二份 `Full`/`Clone`/test engine` 再加载同一份 precompiled data 时，`Functions.Find(Id)` 就会稳定 miss，行为静默退化到 VM fallback。结果是“同进程第一个 engine 有 JIT、第二个 engine 没 JIT”，性能、调试行为与覆盖统计全部分叉，而且 `bStaticJITTranspiledCodeLoaded` 这类状态也会随实例顺序变化。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 compiled JIT registry 从“可被 engine 初始化清空的可变表”拆成“进程级只读注册表 + engine 级运行时解析缓存”，禁止再对静态注册内容做破坏性 `Clear()`。 |
| 具体步骤 | 1. 重新划分 `FJITDatabase` 职责。`Functions` 以及由静态构造函数填充的 compiled lookup 表必须视为 process-lifetime registry，不再由 `FAngelscriptEngine::InitializeForTesting()` 在 `1591-1602` 直接 `Clear()`；若确实需要清理每轮加载产生的解析结果，应新增 engine-owned runtime cache，而不是复用同一张表。 2. 最小修复方案是在 `FJITDatabase` 内拆出两层容器，例如 `CompiledFunctions/CompiledLookups` 与 `ResolvedFunctions/ResolvedLookups`：静态构造函数只写前者；`PrecompiledData` 解析阶段只重建后者；engine 销毁时只清后者。 3. 如果当前改动面不允许立刻拆层，至少要提供显式 `RebuildFromCompiledInfo()` 路径：`Clear()` 后在下一份 engine 初始化前根据 `FStaticJITCompiledInfo::Get()` 和静态注册对象重新填回 `Functions`/lookup 表，避免第二份 engine 永久 miss。 4. `bStaticJITTranspiledCodeLoaded` 的判定要改成读取“当前 engine 是否实际命中了本轮可用的 compiled entries”，不能继续依赖一个可能被前序 engine 清空过的全局 `Functions.Num() > 0`。 5. 新增跨实例回归：在同一进程里顺序创建两份 engine（优先 `CreateTestingFullEngine` + `CreateCloneFrom` 或两次 `CreateForTesting`），两次都加载同一份 precompiled/JIT bundle，断言第二份 engine 仍能给同一 `FunctionId` 恢复 `jitFunction_Raw`，而不是退回 VM。 6. 若出于热重载或开发态需要仍保留某种 `Clear`，必须把 API 改名成只清 runtime-resolved state 的语义，例如 `ResetResolvedLookups()`，避免未来再次把 process registry 一并擦掉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增跨 engine JIT 恢复测试 |
| 预估工作量 | L |
| 风险 | 这会触碰 JIT 全局状态边界；若拆层时没有区分“静态编进来的入口”和“本轮 precompiled data 解析出来的引用解析结果”，容易引入另一类 stale pointer 问题。 |
| 前置依赖 | 建议先梳理 `FJITDatabase` 中哪些字段是静态编译期注册、哪些字段是 runtime 解析产物；没有这层边界就不要直接改 `Clear()`。 |
| 验证方式 | 1. 新增同进程双 engine 回归，确认第二份 engine 仍能命中 `jitFunction` / `jitFunction_Raw`。 2. 在修复前后比对 `bStaticJITTranspiledCodeLoaded` 与实际函数命中结果，确认状态位不再受实例顺序污染。 3. 回归现有 precompiled/JIT 测试，确认第一份 engine 的正常加载行为不变。 |

### Issue-27：`ClearDataBreakpoints` 请求体携带 `Ids`，但服务端入口会无条件清空全部监视点

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:275-290`；`AngelscriptDebugServer.cpp:479-545, 1069-1071, 1241-1285`；`AngelscriptDebugProtocolTests.cpp:174-215` |
| 问题 | 协议结构 `FAngelscriptClearDataBreakpoints` 明确定义了 `TArray<int32> Ids`，反序列化也会完整读取这些 id（`275-290`）。运行期在 data breakpoint 触发或出作用域时，服务端自己也会把被移除项的 `Breakpoint.Id` 填进 `ClearMessage.Ids`，再通过 `SendMessageToAll(EDebugMessageType::ClearDataBreakpoints, ClearMessage)` 广播 selective clear（`479-545`）。但客户端主动发来的 `EDebugMessageType::ClearDataBreakpoints` 在 `1069-1071` 被直接降格成 `ClearAllDataBreakpoints()`，完全不看 `Ids` 内容；`ClearAllDataBreakpoints()` 又会 `DataBreakpoints.Reset()` 并立即重写硬件寄存器（`1241-1285`）。现有 protocol tests 只覆盖 `SetDataBreakpoints` round-trip（`174-215`），没有任何 selective clear 回归。 |
| 根因 | 同一消息类型被实现成了两套彼此冲突的语义：出站路径把它当成“按 id 删除部分 watchpoint”，入站路径却把它当成“总是清空全部 watchpoint”。 |
| 影响 | 当前协议不是简单的“功能少一点”，而是会做错事：前端若只想删除一个 data breakpoint，服务端会把其它仍应保留的监视点一并清掉。由于 `UpdateDataBreakpoints()` 会立刻重写 `Dr0-Dr3`，错误影响是即时的，用户看到的会是其它 watchpoint 无声失效，调试结论取决于删除顺序而不是实际配置。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 统一 `ClearDataBreakpoints` 的语义：按 `Ids` selective clear，空 `Ids` 才表示 clear all。 |
| 具体步骤 | 1. 在 `HandleMessage(EDebugMessageType::ClearDataBreakpoints)` 中先反序列化 `FAngelscriptClearDataBreakpoints Msg`；若 `Msg.Ids.Num() == 0`，保留现有 `ClearAllDataBreakpoints()` 语义；否则只删除 `DataBreakpoints` 中 `Id` 命中的项，再调用 `UpdateDataBreakpoints()`。 2. 把 selective clear 逻辑抽成一个统一 helper，例如 `RemoveDataBreakpointsByIds(const TArray<int32>& Ids, FAngelscriptClearDataBreakpoints* OutRemoved = nullptr)`，让主动请求路径和 `479-545` 的触发后自动移除路径都复用同一套删除/补位/寄存器刷新代码，避免再次出现一边 selective、一边 clear-all 的分叉。 3. 删除后如果没有任何 id 命中，不要静默全清；应保持现状不变，并可选回一个空的 `ClearDataBreakpoints` ack 或 debug log，方便前端知道这是 no-op。 4. 若与 `Issue-18` 的 installed/requested breakpoint 分层一起推进，selective clear 必须先删 requested set，再重建 installed set，确保删除一个已安装项时排队中的第 5 项可以补位，而不是继续丢失状态。 5. 补充 protocol tests：新增 `FAngelscriptClearDataBreakpoints` round-trip 覆盖非空 `Ids`；再加一个运行时/集成测试，先设置至少 3 个 data breakpoint，只发送其中 1 个 id 的 clear 请求，断言其余两个仍然保留并继续命中。 6. 同步审计 `SendMessageToAll(ClearDataBreakpoints, ClearMessage)` 的消费方，如果客户端目前也把这条消息当 clear-all 处理，需要一起修正为 selective remove，否则 server/client 两端仍会继续语义错位。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下 data breakpoint 集成测试 |
| 预估工作量 | M |
| 风险 | 如果当前前端适配层一直把 `ClearDataBreakpoints` 当“全清”使用，那么服务端改成 selective 之后需要确认空 `Ids` 仍保留旧行为，避免兼容性回归。 |
| 前置依赖 | 无；但若后续要做 `Issue-18` 的安装结果回包，建议共用同一套 requested/installed 状态重建逻辑。 |
| 验证方式 | 1. 新增 selective clear 回归，删除单个 id 后确认其它 data breakpoint 仍可触发。 2. 验证空 `Ids` 请求仍会执行 clear all，保持旧客户端兼容。 3. 回归现有 data breakpoint round-trip 与命中测试，确认删除路径不会引入寄存器残留。 |

---

## 发现与方案 (2026-04-08 16:34)

### Issue-28：补足 Analysis 发现 2 的修复方案：`Recv()` 失败或返回 0 字节时，live socket 收包循环会把调试线程卡死在半包读取中

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:724-747, 756-789` |
| 问题 | `ProcessMessages()` 只在进入收包前用 `GetConnectionState()` 做一次客户端剔除，但真正读取 header/body 的两个 `while (BytesReceived < ...)` 循环里完全忽略 `Client->Recv(...)` 的返回值，只把 `BytesRead` 累加到 `BytesReceived`。一旦对端在收 header 或 body 的中途异常断开，或 `Recv()` 因网络错误返回 `false`/`0` 字节，这两个循环里的 `BytesReceived` 就不会再前进，代码会永久停在 `767-772` 或 `784-789`。因为 `PauseExecution()` 在暂停期间会反复调用 `ProcessMessages()`（`667-699`），这个卡死不只是收包线程局部问题，而是会把整个调试会话锁在 paused/tick 循环里，后续清理逻辑 `724-747` 也永远执行不到。 |
| 根因 | live transport 仍是“同步阻塞读到完整一帧”为止”的旧实现，没有把 `Recv()` 的失败/零字节语义纳入状态机，也没有复用前面已经存在的 envelope parser 增量消费模型。 |
| 影响 | 调试客户端在半包期间异常断开、网络抖动返回短读或 socket error 时，目标进程会卡在 DebugServer 的收包循环中。表现上既可能是调试器永远不再响应，也可能是脚本线程被停在 `PauseExecution()` 里无法继续，形成实际死锁；同时该 client 不会走到正常移除路径，关联的 session 状态和发送队列也会滞留。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 live socket 收包改成“按 client 维护累积缓冲 + 增量解析”的非阻塞状态机，并把 `Recv()` 失败/零字节统一视为断线清理信号。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 为每个 client 增加持久化 receive buffer，例如 `TMap<FSocket*, TArray<uint8>> PendingReceives`，不再在每次 `HasPendingData()` 循环里临时 new 一个 `FArrayReader` 并要求一次读满整帧。 2. 在 `ProcessMessages()` 中每次 `Recv()` 时显式检查返回值和 `BytesRead`：`Recv()==false`、`BytesRead<=0`、或累计字节数没有增长时，立即把该 client 标记为 disconnect，跳出收包循环，并复用现有移除路径做 `ClientsThatAreDebugging`/`ClientsThatWantDebugDatabase`/`QueuedSends` 清理。 3. 把 `756-792` 的手写 framing 删除，改成“读到多少 append 多少”，随后循环调用 `TryDeserializeDebugMessageEnvelope()`；只有在 `bOutHasEnvelope=true` 时才构造 message body 并进入 `HandleMessage()`。这样 partial header/body 都只会留在缓冲里等待下次补齐，而不是 busy-loop。 4. 对非法长度包和解析错误，直接断开该 client 并销毁其 socket，避免把坏流继续留在 `PendingReceives` 里污染后续消息；这里可以与现有 `Issue-5`/`Issue-7` 的 envelope parser 修复共用同一条错误处理路径。 5. 新增 transport 回归测试：模拟先发半个 header、半个 body，再让 `Recv()` 返回 0 或 false，验证 `ProcessMessages()` 不会卡死，client 会被及时移除；再加一个 partial frame 跨两次 tick 补齐的正向用例，确认不会误断线。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 预估工作量 | M |
| 风险 | 收包改成增量状态机后，会同时触碰已有发送/断线清理路径；如果 client 生命周期和缓冲区清理没有同步，容易引入新的 stale socket 键或重复销毁。 |
| 前置依赖 | 无；但建议与已记录的 envelope parser 问题一并落地，避免 transport 层同时维护两套 framing 逻辑。 |
| 验证方式 | 1. 新增“半包后断开”和“短读后恢复”的 transport 测试，确认 `ProcessMessages()` 能返回且 client 被清理。 2. 在暂停态人工断开调试器，确认目标线程不会永久卡在 `PauseExecution()`。 3. 回归现有 `AngelscriptDebugTransportTests`，确认合法 envelope 的收发语义不变。 |

### Issue-29：补足 Analysis 发现 70 的修复方案：`DebugServer` 的全局 `bIsPaused`/`bBreakNextScriptLine` 会吞掉其它线程的 breakpoint 和 exception

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptDebugServer.h:597-605, 721`；`AngelscriptDebugServer.cpp:440-472, 550-626, 667-699, 812-816, 832-891`；`AngelscriptEngine.cpp:5429-5443, 5535-5536, 5718-5744` |
| 问题 | 当前 DebugServer 把暂停和步进状态都建模成全局变量：`bIsPaused`、`bBreakNextScriptLine`、`ConditionBreakFrame`、`ConditionBreakFunction`。`ProcessException()` 在任何线程命中异常时，只要看到 `bIsPaused` 已经为真就直接 `return`（`440-445`）；`ProcessScriptLine()` 也在相同条件下提前返回（`471-472`）。而 `PauseExecution()` 一旦被某个线程调用，会把全局 `bIsPaused` 置真并在本线程里 busy-wait `while (bIsPaused)`（`667-695`），期间继续处理网络消息但不会为别的线程建立独立 stop state。引擎侧 `UpdateLineCallbackState()`/`AngelscriptLineCallback()` 也只把 line callback 开关与单个 `DebugServer` 全局状态绑定（`5429-5443`, `5535-5536`）。结果是线程 A 停下后，线程 B 后续命中的 line breakpoint、step target 或 exception 都会被静默吞掉，而不是形成新的 stopped event 或待处理 stop。 |
| 根因 | 调试器状态机按照“进程里同一时刻只会有一个 script thread 需要暂停”的单线程假设设计，没有把 thread/context 维度纳入 session 状态，也没有实现 stopped event 的 thread-scoped ownership。 |
| 影响 | 这不仅是体验问题，而是调试正确性问题：多线程脚本、异步回调或 gameplay task 场景下，某个线程先停住后，其他线程的真实异常和断点会被直接漏掉。协议层也因此无法满足 DAP 这类以 thread 为基本单元的 stopped/continue 语义，后续再补线程列表或按线程继续时会被当前全局状态机整体阻塞。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把暂停模型从全局单例改成 thread-scoped stop state：每个 script thread/context 独立记录是否 paused、为什么停、有哪些待处理 step/continue 请求。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 引入显式的线程暂停结构，例如 `FThreadDebugState`，至少包含 `ThreadId`/`ContextKey`、`bIsPaused`、`PendingStopMessage`、`BreakNextLine`、`ConditionBreakFrame`、`ConditionBreakFunction`，并用 `TMap<uint32, FThreadDebugState>` 或等价结构按线程索引，而不是继续复用全局 bool。 2. 把 `ProcessScriptLine()`、`ProcessException()`、data breakpoint 命中处理和 `TryBreakpointAngelscriptDebugging()` 全部改成先定位“当前触发线程”的 state，再只对该线程做 pause/step 判定；其它线程如果在已有 paused thread 存在时再次命中，应登记为 pending stop，而不是直接 `return` 丢弃。 3. 重写 `PauseExecution()` 为 thread-aware 版本，例如 `PauseExecutionForThread(Context, StopMessage)`：只阻塞当前线程对应的执行流，并把 `HasStopped` payload 扩展为至少包含 thread identity；`Continue`/`StepIn`/`StepOver`/`StepOut` 则只清理目标线程 state，而不是全局清零。 4. `UpdateLineCallbackState()` 需要从“任意线程是否有全局 break 需求”重新计算，而不是读取单个 `bBreakNextScriptLine`；只要任一线程存在 pending step/data breakpoint/pause 请求，就保持 line callback 打开。 5. 在协议兼容层上分阶段推进：如果现有 wire format 暂时没有 thread id 字段，先在服务端内部把 secondary stop 排队，保证不丢事件；随后再扩展 `HasStopped`/`RequestCallStack`/`Continue` 等消息携带 thread identity，对齐 DAP 的 thread-scoped 语义。 6. 增加多线程回归测试：构造两个独立 script context/worker thread，让线程 A 先停在 line breakpoint，线程 B 随后触发 exception 或另一个 breakpoint，验证第二个事件不会被吞掉，而是能作为独立 stop 被观测和继续。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下新增多线程调试测试 |
| 预估工作量 | L |
| 风险 | 这是状态机级重构，影响 `Pause`/`Continue`/step/data breakpoint/exception 全路径；如果协议扩展和服务端状态拆分不同步，容易出现“服务端已区分线程、客户端仍按全局理解”的兼容错位。 |
| 前置依赖 | 建议先明确当前调试协议是否允许扩展 thread id 字段；若暂时不能改 wire format，至少先完成服务端内部 pending-stop 队列，避免继续丢事件。 |
| 验证方式 | 1. 新增双线程 breakpoint/exception 回归，确认线程 A paused 时线程 B 的 stop 不会被吞掉。 2. 验证 `Continue` 只释放目标线程，未继续的线程仍保持 paused 或 pending 状态。 3. 人工对接 DAP 适配层，确认 stopped event 与 call stack 能稳定对应到具体线程。 |

### Issue-30：补足 Analysis 发现 62 的修复方案：`SCRIPT_CALL_NATIVE` fallback 没有复用解释器的 null object / `WorldContext` 安全检查

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:996-1008`；`StaticJITHeader.cpp:169-301`；`as_context.cpp:5152-5198` |
| 问题 | StaticJIT 在 native call 无法直接生成时，会走 `MakeDynamicCall()` 并直接发出 `SCRIPT_CALL_NATIVE(ScriptFunc, ...)`（`996-1008`）。这条 fallback 最终进入 `FStaticJITFunction::ScriptCallNative()`。其中 generic path 只对 `ICC_GENERIC_METHOD` 做了 null object 检查（`183-191`）；而更常见的 `sysFunc->caller.IsBound()` 路径在 `223-230` 只把 object 指针塞进 `FunctionArgs`，没有复制解释器 `CallFunctionCaller()` 在 `5183-5194` 做的 null object 拦截。更关键的是，解释器在调用 native function 前还会做 `descr->traits.GetTrait(asTRAIT_USES_WORLDCONTEXT)` 的 `WorldContext`/`BlueprintThreadSafe` 保护（`5164-5180`），JIT fallback 路径完全没有对应逻辑。也就是说，同一个 native function，解释执行会抛脚本异常或安全返回，而 JIT miss fallback 却可能直接把空 `this` 或无效 `WorldContext` 送进 native caller。 |
| 根因 | `SCRIPT_CALL_NATIVE` 被实现成了另一套手写的 native bridge，只复制了参数封送和调用过程，没有复用解释器 `CallFunctionCaller()` 的前置校验契约，因此“fallback 到 VM/native bridge”在语义上并不等价于解释器执行。 |
| 影响 | 这条问题直接破坏“JIT miss 只影响性能、不影响结果”的前提。命中场景包括：1. JIT miss 后调用 instance native method，空对象本应抛 `TXT_NULL_POINTER_ACCESS`，现在可能把 `nullptr` 传进 `MethodCaller` 造成 native 崩溃；2. `WorldContext` 受限函数在解释器里会被拒绝，在 JIT fallback 下却可能继续执行，产生与解释模式不一致的副作用甚至 crash。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 native call 的前置校验抽成共享 helper，并让解释器与 `SCRIPT_CALL_NATIVE` fallback 走同一套 guard 逻辑，确保 JIT miss 与解释执行语义一致。 |
| 具体步骤 | 1. 在运行时层抽出一个共享 preflight helper，例如 `ValidateNativeCallPreconditions(asCScriptFunction* Descr, asDWORD* StackArgs, FNativeCallPreflightResult& Out)`，统一处理 `asTRAIT_USES_WORLDCONTEXT`、`GIsInAngelscriptThreadSafeFunction`、`GIsAngelscriptWorldContextAvailable`、以及 `ICC_THISCALL`/`ICC_GENERIC_METHOD` 的 null object 检查。 2. 让 `as_context.cpp::CallFunctionCaller()` 和 `StaticJITHeader.cpp::ScriptCallNative()` 都先调用该 helper：解释器路径继续映射为 `SetInternalException(...)`；JIT 路径则映射为 `FStaticJITFunction::SetException(...)` 并设置 `Execution.bExceptionThrown`，保证上层 `MakeDynamicCall()` 现有的 `ExceptionCleanupAndReturn()` 能按脚本异常路径退出。 3. 对 `sysFunc->caller.IsBound()` 分支补齐与解释器完全一致的 object pointer 校验，不允许再把空 `this` 直接推进 `MethodCaller`。 4. 如果短期内不适合抽共享 helper，最小修复也必须先把 `as_context.cpp:5164-5198` 的 guard 逐字对齐到 `ScriptCallNative()` 两个分支里，并在代码注释中显式声明两边必须同步，避免再次漂移。 5. 增加两组回归：一组构造空对象调用 native instance method，验证解释器与 JIT fallback 都得到相同的 null pointer 脚本异常；另一组在 `WITH_EDITOR` 下调用需要 `WorldContext` 的 native function，故意让 `GIsAngelscriptWorldContextAvailable` 为 false，验证两条路径都拒绝执行且错误文本一致。 6. 再补一组“真实 JIT miss”集成用例，强制某个 native call 走 `MakeDynamicCall()`，确认不是只修到了 helper 单测，而是真正堵住了 StaticJIT 产物与解释执行的语义分叉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增 native fallback 一致性测试 |
| 预估工作量 | M |
| 风险 | 需要同时触碰 runtime third-party bridge 和 StaticJIT bridge；若两边异常文本或状态位映射不一致，可能修掉崩溃却引入新的调试/日志差异。 |
| 前置依赖 | 需要先选定一个稳定的 JIT miss fixture，确保测试能可靠经过 `SCRIPT_CALL_NATIVE` fallback，而不是被其它 native fast path 短路。 |
| 验证方式 | 1. 新增 null object / invalid `WorldContext` 一致性测试，比较解释器与 JIT fallback 的异常类型、文本和返回值。 2. 人工在带真实 JIT miss 的脚本上验证不再调用空 `this` native method。 3. 回归现有 StaticJIT/native bind 测试，确认已可直接生成的 native fast path 不受影响。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-30 | Defect | 立即修复，先恢复 JIT fallback 与解释执行的一致安全语义 |
| P1 | Issue-28 | Defect | 在 P0 之后修复，避免异常断开把调试会话和脚本线程卡死 |
| P1 | Issue-29 | Architecture | 作为下一阶段主重构，按 thread-scoped state 收敛 DebugServer 的暂停模型 |

---

## 发现与方案 (2026-04-08 16:47)

### Issue-31：Windows data breakpoint 的 active mirror 在异常处理器与 game thread 之间无快照发布，存在真实竞态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.h:330-395, 714-716`；`AngelscriptDebugServer.cpp:272-379, 1248-1282` |
| 问题 | `DebugRegisterExceptionHandler()` 是通过 `AddVectoredExceptionHandler` 注册的异步异常入口，会直接读取 `DebugServer->ActiveDataBreakpoints[i]`，并消费其中的 `bCppBreakpoint`、`HitCount/Status/ContextPtr`，随后又把整组 `ActiveDataBreakpoints` 交给 `ApplyBreakpointsToThreadContext(...)` 重写 `Dr0-Dr7`。与此同时，game thread 上的 `RebuildActiveDataBreakpoints()` 会先把 `ActiveDataBreakpointCount` 置 `0`，再对同一数组逐槽执行 `CopyFrom()` / `Reset()`；`CopyFrom()` 又是按字段写入，只有 `HitCount`、`bTriggered`、`Status`、`ContextPtr` 是原子，`Id`、`Address`、`AddressSize`、`bCppBreakpoint` 都是普通成员。整个过程中没有锁、没有 generation、也没有 immutable snapshot 发布。 |
| 根因 | 当前实现把“供异常处理器消费的 active watchpoint 集”和“game thread 上可变的配置镜像”复用了同一块可原地修改的内存，但 Windows VEH 可以在任意指令边界打断当前线程，代码没有建立任何发布/读取协议来保证 handler 看到的是同一版本的完整 slot。 |
| 影响 | 推断：当 `SetDataBreakpoints()`、`ClearAllDataBreakpoints()`、out-of-scope 清理或命中后重建与 `EXCEPTION_SINGLE_STEP` 交错时，handler 可能读到混合版本的 slot，例如旧地址配新 `AddressSize` / `bCppBreakpoint`，或者在 `ActiveDataBreakpointCount` 已清零、slot 尚未完全重建时回写 debug registers。结果会表现为 watchpoint 误命中、错把普通 AS watchpoint升级成 `UE_DEBUG_BREAK()`、或把错误地址重新写回 `Dr0-Dr3`。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 active watchpoint 状态改成只读快照发布模型，异常处理器永远只读取完整发布的 snapshot，不再与 game thread 共享可原地修改数组。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 新增 `FActiveDataBreakpointSnapshot`，包含固定 4 槽的配置字段 `Id/Address/AddressSize/bCppBreakpoint/InitialHitCount`、一个 `Count` 和 generation；把 handler 运行期需要回写的 `HitCount/Triggered/Status/Context` 单独拆到 `FActiveDataBreakpointRuntimeState`，避免继续把“配置”与“命中状态”混写在一个 struct。 2. `RebuildActiveDataBreakpoints()` 不再原地改 `ActiveDataBreakpoints[]`；改为先在栈上或备用 buffer 完整构造一个 snapshot，再通过单次原子发布（例如双缓冲索引或原子指针交换）让 `DebugRegisterExceptionHandler()` 看到新版本。发布前不要修改当前正在使用的 snapshot。 3. `DebugRegisterExceptionHandler()` 进入后先抓取当前 snapshot 指针/版本，只基于这份只读快照判断 `Address`、`AddressSize`、`bCppBreakpoint`，并把命中结果写入与 slot 对齐的 runtime-state 数组；禁止再读取会被 game thread 原地修改的配置字段。 4. `ApplyBreakpointsToThreadContext(...)` 与 `FUpdateDebugRegisterThread` 统一改成接收同一份 snapshot，而不是一个时候用 `DataBreakpoints`，另一个时候用 `ActiveDataBreakpoints`；这样 CPU debug registers 与 handler 判定使用完全相同的配置版本。 5. `SyncActiveDataBreakpointsToAuthoritativeState()` 只回收 runtime-state 中的 `HitCount/Triggered/Status/Context`，不要再把 `Address/AddressSize/bCppBreakpoint` 从 handler 所见数组反拷回 `DataBreakpoints`，避免配置字段反向污染权威状态。 6. 增加 stress 回归：在测试桩里循环 `SetDataBreakpoints` / `ClearDataBreakpoints` / out-of-scope 清理，同时模拟 `EXCEPTION_SINGLE_STEP` 读取快照，断言 handler 观察到的每个 slot 始终来自同一 generation，且不会出现“Count 已变、slot 配置半旧半新”的组合。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` 或新增 data breakpoint 并发测试文件 |
| 预估工作量 | L |
| 风险 | 这是 data breakpoint 内部状态机重构；如果 snapshot 与 runtime-state 的职责划分不清，容易把命中计数、自动移除和客户端回包再拆成两套不同步状态。 |
| 前置依赖 | 建议与已记录的 selective clear / 超限反馈方案一起设计 `Requested`、`Installed`、`ActiveSnapshot` 三层状态，避免再次重构 data breakpoint 权威模型。 |
| 验证方式 | 1. 新增并发压力测试，反复更新 watchpoint 配置并触发 handler，确认 `Dr0-Dr3` 与 handler 读取的 slot 始终对应同一 generation。 2. 人工在 Windows 上高频命中/删除 data breakpoint，确认不再出现偶发误命中、错误 `UE_DEBUG_BREAK()` 或寄存器残留。 3. 回归现有 data breakpoint round-trip 与命中测试，确认 snapshot 化后协议行为不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-31 | Architecture | 在 data breakpoint 后续修复前优先收敛线程安全模型，避免继续在竞态上叠加功能修改 |

---

## 发现与方案 (2026-04-08 16:52)

### Issue-32：`SetBreakpoint` 对截断消息体零校验，缺失 `LineNumber` 时会把未初始化栈值当成断点行号

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`../UERelease/Engine/Source/Runtime/Core/Public/Serialization/ArrayReader.h` |
| 行号 | `AngelscriptDebugServer.h:227-255`；`AngelscriptDebugServer.cpp:955-1059`；`ArrayReader.h:33-46` |
| 问题 | `FAngelscriptBreakpoint` 的 `LineNumber` 没有默认初始化，`operator<<` 又会无条件尝试从 archive 读取 `Filename` 和 `LineNumber`。`HandleMessage(EDebugMessageType::SetBreakpoint)` 在 `*Datagram << BP;` 之后没有检查 `Datagram->IsError()`、`Datagram->AtEnd()` 或任何字段完整性，就立刻把 `BP.LineNumber` 当成 `WantedLine` 参与最近可执行行搜索、断点存储和回包。与此同时，`FArrayReader::Serialize()` 在越界读取时只会 `SetError()`，不会清零或改写目标内存。也就是说，只要客户端发来 envelope 合法、body 截断的 `SetBreakpoint`，服务端就会带着一个未定义的 `LineNumber` 继续改断点状态。 |
| 根因 | DebugServer 目前只在少数消息上做 body-level 防御，`SetBreakpoint` 这种会修改全局调试状态的入口仍然走“裸反序列化 + 继续执行”的 happy-path；底层 `FArrayReader` 只设置错误标志，不会帮调用方初始化缺失字段。 |
| 影响 | 恶意或损坏客户端可以用一条缺少 `LineNumber` 的 `SetBreakpoint` 包，在服务器上植入随机行号断点、触发错误的“最近可执行行”改写，甚至把断点错误落到其它函数。因为这条路径还会回写 `SetBreakpoint` ack，前端可能得到一个结构完全合法、语义却已经偏离请求的响应。现有自动化只覆盖正常 `SendSetBreakpoint(...)` 和断点命中/清除流程，没有任何 malformed-body 负向护栏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为所有 state-mutating message 建立统一的严格反序列化 helper；`SetBreakpoint` 缺任一必填字段时直接拒绝处理，不允许未初始化字段进入断点逻辑。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 抽取统一 helper，例如 `template<typename T> bool TryReadStrictMessage(FArrayReader& Ar, T& OutMsg)`；流程固定为“值初始化对象 -> `Ar << OutMsg` -> 检查 `Ar.IsError()` -> 检查 `Ar.AtEnd()` -> 返回是否合法”。这样所有消息都从零初始化对象开始，不再依赖局部栈垃圾值。 2. 先把 `SetBreakpoint`、`ClearBreakpoints`、`SetDataBreakpoints`、`BreakOptions`、`StartDebugging`、`RequestVariables`、`RequestEvaluate` 这些会改状态或读取调试数据的入口切到该 helper；对版本兼容需要保留 optional 字段的消息，helper 仍要求“必填字段完整、没有非预期尾字节”。 3. `FAngelscriptBreakpoint` 本身也补安全默认值，例如把 `LineNumber` 初始化为 `INDEX_NONE`；但这只能作为兜底，真正的拒绝条件仍应放在 `TryReadStrictMessage(...)`，避免默认值被误当成有效请求。 4. `HandleMessage(SetBreakpoint)` 在解析失败时不得继续触碰 `Breakpoints`、`BreakpointCount` 或 module `hasBreakPoints`；可选地向客户端回一个明确的 error log / diagnostics，至少在服务端日志里记录 malformed request。 5. 把本次修复扩展到 `FAngelscriptClearDataBreakpoints` 和 `FAngelscriptDataBreakpoints` 的计数字段，统一限制数组长度与 message body 剩余字节，避免继续散落出“先 `SetNum()` 再发现 body 不够”的同类问题。 6. 增加 protocol/transport 负向测试：构造一个只包含 `Filename`、缺少 `LineNumber` 的 `SetBreakpoint` body，断言服务端不会新增断点、不会回写随机 `ChangedBP`；再加一组“多余尾字节”测试，确认严格解析会拒绝协议漂移，而不是默默吞掉尾巴。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | M |
| 风险 | 严格 body 校验会暴露旧客户端历史上发送的宽松/多余字段消息；如果兼容策略没先梳理清楚，可能让某些“以前误打误撞能工作”的请求被正确拒绝。 |
| 前置依赖 | 无；但建议和已记录的 `RequestEvaluate` / `BreakOptions` body 校验修复共用同一套 helper，一次性收敛协议读取入口。 |
| 验证方式 | 1. 新增截断 `SetBreakpoint` 负向测试，确认服务端不新增断点、不发送随机行号 ack。 2. 验证正常 `SetBreakpoint` / `ClearBreakpoints` / `RequestEvaluate` 路径仍可工作，避免严格解析误伤合法消息。 3. 人工向调试端口发送带尾字节的旧包，确认服务器明确拒绝并记录错误，而不是继续修改状态。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-32 | Defect | 与现有 body-level 校验修复一并处理，优先堵住会直接改断点状态的 malformed request |

---

## 发现与方案 (2026-04-08 16:58)

### Issue-33：补足 Analysis 发现 73 的修复方案：running 态 `Continue` 会直接抹掉挂起中的 `pause` / data breakpoint stop

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:338-370, 479-545, 837-845` |
| 问题 | data breakpoint 异常处理器命中后不会立即发 `HasStopped`，而是只把 `bBreakNextScriptLine` 设为 `true`，等下一次 `ProcessScriptLine()` 再 flush `TriggeredBreakpoints` 并进入 `PauseExecution()`。普通 `Pause` 请求也是同一模型：仅设置 `bBreakNextScriptLine`，并不立刻进入 paused state。与此同时，`HandleMessage(EDebugMessageType::Continue)` 无条件执行 `bIsPaused = false; bBreakNextScriptLine = false;`。结果是只要客户端在真正停下之前先发出一次 `Continue`，之前已经挂起的 pause / data breakpoint stop 就会被直接清空。 |
| 根因 | 当前状态机把“下一条脚本行需要兑现的挂起 stop”和“已经处于暂停、等待恢复”复用了同一组全局布尔状态，没有显式区分 pending-stop 与 paused-stop 两个生命周期阶段。 |
| 影响 | 调试前端、重试逻辑或旁路客户端只要在 running 态误发一次 `Continue`，就能把本应到达的 `pause` / data breakpoint stop 静默吞掉。用户看到的会是“watchpoint 明明命中却没停住”或“点了 pause 又自己继续跑”，这直接破坏 stopped event 的可靠性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 pending-stop 与 paused-state 拆成显式状态机；`Continue` 只允许结束已进入的 stop，不允许清除尚未兑现的挂起停点。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h/.cpp` 增加独立的 pending-stop 结构，例如 `FPendingStopRequest { EStopSource Source; FString Text; bool bRequiresNextLineCallback; }`，至少覆盖 `PauseRequested`、`DataBreakpointTriggered`、`StepRequested` 三类来源；不要再复用 `bBreakNextScriptLine` 表示所有“将来要停”的语义。 2. `HandleMessage(Pause)` 改为登记 `PendingStopRequest{ Source = PauseRequested }`，并保持 line callback 打开；Windows data breakpoint handler 也只负责登记 `PendingStopRequest{ Source = DataBreakpointTriggered, ... }`。二者都不应被 `Continue` 直接擦除。 3. `ProcessScriptLine()` 在进入行回调时，先检查是否存在待兑现的 pending stop；若存在，则将其原子地转成真正的 `bIsPaused = true` + `PauseExecution(...)`，并在发送 `HasStopped` 之前清空 pending 队列中对应项，防止重复停下。 4. `HandleMessage(Continue)` 与 step 命令增加 stopped-state 门槛：只有当前确实处于 `bIsPaused == true` 且拥有 active stop owner 时才允许 resume；若仅存在 pending stop 而尚未进入 paused，`Continue` 应返回 no-op 或显式拒绝，不得清掉 pending 请求。 5. 与 `Issue-29` 的线程态重构保持兼容：pending-stop 至少应按 thread/context 维度存储，避免未来把全局 bool 拆掉时再次返工；本次最小修复也要先把 `PendingStop` 抽象独立出来，而不是继续往 `bBreakNextScriptLine` 上叠条件。 6. 增加两组回归：一组在 running 态先触发 data breakpoint，再在下一条脚本行到来前发送 `Continue`，断言 stop 仍会如期送达；另一组对普通 `Pause` 做同样验证，确认提前 `Continue` 不会把 pause 请求吞掉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` 或 `AngelscriptDebuggerSteppingTests.cpp` |
| 预估工作量 | M |
| 风险 | 这条修复会显式暴露当前 `Pause`、step 与 data breakpoint 共用 `bBreakNextScriptLine` 的耦合；如果只局部打补丁而不把 pending-stop 单独抽出来，很容易修掉一种 stop source 又继续吞另一种。 |
| 前置依赖 | 无；但若随后推进 `Issue-29` 的 thread-scoped state，本条新增的 pending-stop 结构应直接下沉到 thread state 中复用。 |
| 验证方式 | 1. 新增“running 态提前 `Continue`”负向测试，确认 pending data breakpoint stop 不再被清空。 2. 对 `Pause` 做同样的提前 `Continue` 回归，确认 pause 请求最终仍会产生 `HasStopped(reason="pause")`。 3. 回归正常 stopped-state `Continue` / step 流程，确认真正的 resume 仍会发送 `HasContinued` 且不会卡住。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-33 | Defect | 先修复 pending-stop 被提前 `Continue` 吞掉的问题，恢复 pause/data breakpoint 的基础可靠性 |

---

## 发现与方案 (2026-04-08 17:03)

### Issue-34：`CallStack` frame 序列化格式不对称，`ModuleName` 会错位污染后续帧且 BP/C++ 帧发送未初始化 `LineNumber`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h` |
| 行号 | `AngelscriptDebugServer.h:196-214`；`AngelscriptDebugServer.cpp:1402-1437, 1451-1476`；`AngelscriptDebugProtocolTests.cpp:39-77`；`AngelscriptDebuggerTestClient.h:43-55` |
| 问题 | `FAngelscriptCallFrame::operator<<` 总会序列化 `Name`、`Source`、`LineNumber`，但只在 `ModuleName.IsSet()` 时追加 `ModuleName`，读取侧又没有任何 `Ar.AtEnd()` / version / presence-bit 判断，默认构造出来的 `ModuleName` 为 unset，因此同一套反序列化逻辑根本不会消费该字段。与此同时，`SendCallStack()` 在 `DebugAdapterVersion >= 1` 时会给 BP、C++、script 三类 frame 全部写入 `ModuleName`，但只在 script frame 路径设置 `Frame.LineNumber`；BP/C++ 路径发送的是未初始化行号。测试客户端 `DeserializeMessage<T>()` 直接复用同一套 `operator<<` 读取消息，而当前 protocol tests 只覆盖 `StartDebugging`、`Breakpoint`、`Variables`、`DataBreakpoints`、`BreakFilters`、`DatabaseSettings`，没有任何 `CallStack` round-trip 护栏。 |
| 根因 | `CallStack` 协议在已有 wire format 上临时塞入了可选 `ModuleName`，但没有同步引入版本化或显式 presence 位；非脚本 frame 也没有补齐必填字段默认值，导致消息结构既不自描述也不对称。 |
| 影响 | 只要 `DebugAdapterVersion >= 1` 且调用栈里有多帧，前一帧遗留的 `ModuleName` 字节就可能被下一帧当成 `Name`/`Source`/`LineNumber` 继续读取，`stackTrace` 结果会从第二帧开始错位。即使只有单帧，BP/C++ frame 也会把随机 `LineNumber` 发给客户端，直接破坏 DAP `stackTrace` 的正确性和可重复性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `FAngelscriptCallFrame` 改成对称、版本化的 wire format，并为所有非脚本 frame 显式写入稳定的行号哨兵值。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 重写 `FAngelscriptCallFrame::operator<<`：短期按当前协议约束，参考 `FAngelscriptVariable` 的做法，在 `AngelscriptDebugServer::DebugAdapterVersion >= 1` 时无条件读写一个 `FString ModuleName`，不要再依赖 `TOptional::IsSet()` 作为 wire 条件；更稳妥的做法是同时补一个显式 `bool bHasModuleName`，为未来协议继续演进留出空间。 2. 把 `FAngelscriptCallFrame::LineNumber` 初始化为 `-1`，并在 `SendCallStack()` 的 BP/C++ frame 分支显式写入 `Frame.LineNumber = -1`，禁止未初始化栈值进入协议。 3. 审计所有 `CallStack` 消费方，尤其是 [`AngelscriptDebuggerTestClient.h`](J:/UnrealEngine/AngelscriptProject/Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h) 的 `DeserializeMessage<T>()` 路径，确认修复后同仓测试客户端与生产服务器使用完全一致的读写规则。 4. 如果随后推进 `Issue-11` 的 per-session 版本隔离，本条 `CallFrame` 版本判断应一起下沉到 session-aware 序列化入口，避免继续依赖全局 `DebugAdapterVersion`。 5. 在 `AngelscriptDebugProtocolTests.cpp` 新增 `CallStack` round-trip 覆盖，至少包含两帧以上、混合 script/BP/C++ frame、`DebugAdapterVersion` v0/v1/v2 三组场景，断言 `ModuleName`、`LineNumber` 和帧顺序都稳定。 6. 再补一个 debugger 集成用例，在真实暂停态请求 `CallStack`，断言顶层脚本帧之外如果存在 C++/BP frame，其 `LineNumber` 为 `-1` 或其它明确哨兵，而不是随机值。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | M |
| 风险 | `CallStack` 是现有调试前端的核心消息；如果修复时没有明确处理旧客户端的兼容窗口，可能让已发布的 adapter 暂时无法读取新格式。 |
| 前置依赖 | 无；但若短期仍保留全局 `DebugAdapterVersion`，需要与 `Issue-11` 的会话隔离方案协调，避免两次改同一套序列化入口。 |
| 验证方式 | 1. 新增 `CallStack` round-trip 单测，覆盖多帧和 mixed frame。 2. 用测试客户端在 `StartDebugging(2)` 后请求真实 `CallStack`，确认第二帧开始不再错位。 3. 回归现有 breakpoint/stepping tests，确认脚本帧 `ModuleName` 和 `LineNumber` 保持正确。 |

### Issue-35：调试会话的入站授权与出站广播都忽略 session 成员关系，未握手或仅订阅 database 的连接也能控制并观测调试会话

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h` |
| 行号 | `AngelscriptDebugServer.h:586-588, 648-668`；`AngelscriptDebugServer.cpp:724-747, 801-807, 822-826, 828-1160` |
| 问题 | 服务器虽然维护了 `Clients`、`ClientsThatWantDebugDatabase` 和 `ClientsThatAreDebugging` 三套集合，但 `HandleMessage()` 在处理 `Pause`、`Continue`、`StepIn/Over/Out`、`SetBreakpoint`、`ClearBreakpoints`、`SetDataBreakpoints`、`RequestVariables`、`RequestEvaluate`、`BreakOptions` 等分支时，完全不检查 `Client` 是否已进入 `ClientsThatAreDebugging`。反过来，`SendMessageToAll()` 又始终遍历最宽泛的 `Clients`，因此 `PauseExecution()` 发出的 `HasStopped` / `HasContinued`，以及 data breakpoint 自动移除时的 `ClearDataBreakpoints`，都会广播给所有连接，而不仅是正在调试的会话。源码里 `ClientsThatAreDebugging` 目前只用于断线清理和 ping 广播。 |
| 根因 | DebugServer 只把 session 集合当作“统计和心跳”数据，而没有把它们提升为协议授权和消息路由的权威来源；连接建立与调试会话建立被错误地视为同一层级。 |
| 影响 | 任何能连上端口的旁路客户端，即使从未发送 `StartDebugging`，也能暂停/继续别人、增删断点、读取暂停态变量与表达式。与此同时，只想订阅 debug database 的工具也会收到别人的 `HasStopped` / `HasContinued` / `ClearDataBreakpoints`。这既破坏 DAP/debug session 的隔离语义，也扩大了调试端口的攻击面。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 立即把“已连接 socket”和“已建立 debug session 的 socket”分层，所有控制/读取请求都要求 active debugging session，所有调试事件也只发给对应 session 集合。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 增加统一的 session gate helper，例如 `bool IsDebuggingClient(FSocket* Client)` 和 `bool RejectIfNotDebuggingClient(FSocket* Client, EDebugMessageType MessageType)`；`Pause`、`Continue`、`Step*`、`StopDebugging`、`Set/ClearBreakpoint`、`Set/ClearDataBreakpoints`、`RequestCallStack`、`RequestVariables`、`RequestEvaluate`、`BreakOptions`、`GoToDefinition` 等分支进入前都先过这层 gate。 2. `RequestDebugDatabase`、`PingAlive`、握手相关消息保留为非 debugging-client 可用，其余消息若来自未握手 socket，应记录 warning，并返回 no-op 或显式 error，而不是继续改全局状态。 3. 把 [`AngelscriptDebugServer.h`](J:/UnrealEngine/AngelscriptProject/Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h) 中的 `SendMessageToAll()` 拆成至少两类：`SendMessageToDebuggingClients()` 和 `SendMessageToDatabaseSubscribers()`；`HasStopped`、`HasContinued`、`CallStack`、`Variables`、`Evaluate`、`ClearDataBreakpoints` 只允许走 debugging-clients 广播。 4. 若短期还未完成 `Issue-11` 的 per-session 权威状态重构，也至少要保证“只有 active debugging sessions 能发控制消息/收调试事件”；数据库订阅者和未握手连接不能再跨层观察或操纵会话。 5. 把 disconnect/timeout 清理统一到 session-aware helper 中，确保 client 从 `ClientsThatAreDebugging` 摘除后不会再收到任何 stop/continue 广播，也不会保留在后续待发送队列中。 6. 增加双 client 回归：client A 只发 `RequestDebugDatabase` 不 `StartDebugging`，client B 正常调试；验证 A 不能成功发送控制消息，且 A 不会收到 B 的 `HasStopped` / `HasContinued`。再补一组“未握手 socket 直接发 `Pause`/`SetBreakpoint`”的负例，确认服务器只记录拒绝，不改变运行时状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只加入站 gate 而不同时修出站广播，数据库订阅者仍会被 stop 事件污染；反之只改广播而不改入站授权，则旁路客户端仍可篡改会话状态。 |
| 前置依赖 | 无；但与 `Issue-11` 的 session map 重构存在明显协同，建议共享同一套 session 权威来源，避免先做临时布尔判断后再全部推翻。 |
| 验证方式 | 1. 新增未握手/仅 database client 的负向集成测试，确认控制消息被拒绝。 2. 让一个 database-only client 与一个真正调试 client 并存，确认前者不再收到 `HasStopped` / `HasContinued` / `ClearDataBreakpoints`。 3. 回归单 client debugger tests，确认正常握手后的控制链路不受影响。 |

### Issue-36：`BreakOptions` 直接信任 `FilterCount` 并先 `SetNum()` 后校验，畸形消息可触发超大分配或断言

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:466-480`；`AngelscriptDebugServer.cpp:1137-1146, 777-789`；`AngelscriptDebuggerSmokeTests.cpp:90-123`；`AngelscriptDebugProtocolTests.cpp:217-227` |
| 问题 | `FAngelscriptBreakOptions::operator<<` 先从 archive 读取 `int32 FilterCount`，在加载侧立即执行 `Msg.Filters.SetNum(FilterCount)`，没有任何 `FilterCount < 0`、上限值、剩余 payload 长度或白名单校验。`HandleMessage(BreakOptions)` 也在 `*Datagram << Options;` 后直接把结果写进运行时 `BreakOptions`。虽然 `ProcessMessages()` 把整包长度限制在 `<= 1MB`，但这个限制并不能阻止客户端在合法长度包里塞入畸形 `FilterCount`，让服务器先按该计数扩容，再去读取实际上不存在的字符串数组。现有 smoke/protocol tests 只验证能收到 `BreakFilters` 响应和纯 round-trip，不覆盖任何 malformed `BreakOptions`。 |
| 根因 | 调试协议把来自 socket 的计数字段当成可信输入，采用了“先分配容器、再尝试读取内容”的反序列化顺序；`BreakOptions` 入口也没有复用任何严格 body 校验 helper。 |
| 影响 | 恶意或损坏客户端可以用一条 `BreakOptions` 包把服务器带进超大内存分配、负长度断言或后续字符串反序列化崩溃路径，形成明确的拒绝服务面。由于这条路径在当前插件源码里属于默认启用的握手后消息，风险不是理论上的。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `BreakOptions` 建立“先验证计数与剩余字节，再分配容器”的严格解析路径，并把 filter 值限制到已公布集合内。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 抽出专门的 `TryReadBreakOptions(FArchive& Ar, FAngelscriptBreakOptions& OutOptions, int32 MaxFilterCount)` helper，先读取 `FilterCount` 到局部变量，验证其 `>= 0`、不超过一个明确上限，再逐个读取字符串；只有全部成功后才 `SetNum()` / 赋值到 `OutOptions.Filters`。 2. 把 `HandleMessage(EDebugMessageType::BreakOptions)` 切到该 helper；解析失败时立即拒绝请求，不改写现有 `BreakOptions`，并记录协议错误。 3. 将允许的 filter 名称限制到 `RequestBreakFilters` 实际公布的集合，再统一补上默认 `break:any`，禁止客户端通过任意字符串向 `BreakOptions` 注入未知标识。 4. 与 `Issue-32` 的严格消息反序列化方案收口：`BreakOptions` 应作为首批迁移对象之一，避免 helper 只修 `SetBreakpoint` 一类消息而漏掉计数字段 DoS 入口。 5. 在协议测试中新增 malformed-body 负例，覆盖 `FilterCount = -1`、超大 `FilterCount`、声明 3 个 filter 但 body 只有 1 个字符串、以及带未知 filter 名称的请求；断言服务器保持原有 `BreakOptions` 不变。 6. 在 smoke/integration 层补一组真实 socket 负例，验证坏 `BreakOptions` 不会让 session 崩溃，也不会影响后续合法 breakpoint/continue 流程。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只做计数校验而不做 filter 白名单，仍然会保留“未知 filter 名称 silently accepted”的协议漂移；如果校验规则与前端公布能力不一致，也可能误拒合法旧客户端。 |
| 前置依赖 | 无；但建议与 `Issue-32` 共享同一套 strict-read 基础设施，避免再次出现每种消息单独手写计数字段校验。 |
| 验证方式 | 1. 新增 `BreakOptions` 负向协议测试，确认畸形计数字段不会触发分配/崩溃。 2. 发送未知 filter 名称，确认服务器拒绝或忽略且保留原配置。 3. 回归正常 `RequestBreakFilters` + `BreakOptions` 流程，确认合法 filter 仍然生效。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-34 | Defect | 先修 `CallStack` wire format，对齐 `stackTrace` 的基础正确性和兼容性 |
| P1 | Issue-35 | Architecture | 紧随其后收紧 session gate 与事件路由，堵住未握手/旁路客户端的控制面 |
| P1 | Issue-36 | Defect | 与严格反序列化改造一并处理，优先封死 `BreakOptions` 的计数字段 DoS 入口 |

---

## 发现与方案 (2026-04-08 17:17)

### Issue-37：补足 Analysis 发现 49 的修复方案：data breakpoint 更新线程每次都会泄漏 duplicated thread handle

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:206-218, 221-232, 1275-1282` |
| 问题 | `GetThreadAgnosticCurrentThreadHandle()` 每次都会调用 `DuplicateHandle(...)` 返回新的真实线程句柄；`UpdateDataBreakpoints()` 每次设置、清空或重建 watchpoint 都会用该句柄创建 `FUpdateDebugRegisterThread`。但 `FUpdateDebugRegisterThread` 只在析构时 `WaitForCompletion()`，从未对 `ThreadToDebug` 调用 `CloseHandle()`，文件内也不存在其它回收路径。 |
| 根因 | duplicated thread handle 的所有权被隐式挂在 `FUpdateDebugRegisterThread` 上，但这个辅助类型只管理工作线程生命周期，没有管理被复制出来的 Windows 句柄生命周期。 |
| 影响 | data breakpoint 反复安装、移除、命中后重建或变量出作用域时，进程会稳定累积内核线程句柄。长时间调试后这会演变成真实的 handle leak，最坏情况下让后续 `DuplicateHandle`、`CreateThread` 或其它调试基础设施失败。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 duplicated thread handle 变成显式拥有的 RAII 资源，并把关闭动作收敛到唯一析构路径。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 为 Windows 句柄增加小型 RAII 包装，例如 `FScopedWinHandle` / `TUniquePtr<void, FCloseHandleDeleter>`，专门负责 `CloseHandle()`。 2. 把 `GetThreadAgnosticCurrentThreadHandle()` 的返回值改成由 `FUpdateDebugRegisterThread` 显式接管所有权；无论 `Run()` 早退、`SetThreadContext()` 失败还是正常完成，析构都必须释放 `ThreadToDebug`。 3. 在 `FUpdateDebugRegisterThread` 构造函数里补 `FRunnableThread::Create(...) == nullptr` 的失败分支，避免“线程没创建成功，但 duplicated handle 已经泄漏”。 4. 如果后续仍要保留“每次更新都现复制一次句柄”的模式，至少保证复制和释放成对出现；更稳妥的版本是把 game thread 的真实句柄缓存为 `FAngelscriptDebugServer` 成员，在 server 构造/析构时成对创建与销毁，避免每次 `UpdateDataBreakpoints()` 都重复 `DuplicateHandle`。 5. 为测试增加 Windows 专用 seam：要么抽象 `DuplicateHandle/CloseHandle` 为可注入 helper 统计调用次数，要么在集成测试里用 `GetProcessHandleCount()` 比较大量 `SetDataBreakpoints/ClearDataBreakpoints` 前后的句柄数，确保不再线性增长。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 预估工作量 | S |
| 风险 | 若在 worker 线程真正结束前提前关闭 `ThreadToDebug`，会把 `SuspendThread/GetThreadContext/SetThreadContext` 变成 use-after-close；因此必须把句柄关闭放在 `WaitForCompletion()` 之后或用严格 RAII 保证析构顺序。 |
| 前置依赖 | 无 |
| 验证方式 | 1. Windows 下循环执行 `SetDataBreakpoints` / `ClearDataBreakpoints` / out-of-scope 清理，确认进程 handle count 不再持续增长。 2. 人工验证 `GetThreadContext` / `SetThreadContext` 在修复后仍能稳定成功，不引入 watchpoint 安装回归。 3. 回归现有 `AngelscriptDebugProtocolTests` 与任何 data breakpoint 集成用例，确认协议行为不变。 |

### Issue-38：补足 Analysis 发现 51 的修复方案：同一轮 data breakpoint flush 会连续触发多次 `PauseExecution`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:479-545, 667-699` |
| 问题 | `ProcessScriptLine()` 会先把所有 data breakpoint 命中的文案收集到 `TriggeredBreakpoints`，随后对该数组逐项执行 `PauseExecution(&Msg)`。而 `PauseExecution()` 每次都会发送一轮 `HasStopped`，阻塞等待客户端 `Continue`，再补发一轮 `HasContinued`。因此只要同一条写操作命中两个 watchpoint，或一次 stack pop 同时让多个 watchpoint out-of-scope，服务器就在未执行下一条脚本指令前制造多轮 stop/continue 循环。 |
| 根因 | data breakpoint flush 的最小单元被建模成“每条命中文案 = 一次完整暂停”，而不是“同一轮 flush = 一次暂停，内部可携带多个命中明细”。 |
| 影响 | 调试前端会表现为“按一次 continue 仍停在原地，还要继续多次才能真正前进”。这不仅破坏 continue/step 语义，还会让用户操作次数依赖命中数量和顺序，导致 watchpoint 行为不可预测。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把一次 data breakpoint flush 压缩成单个 stop event，多个命中只作为同一停点的聚合明细。 |
| 具体步骤 | 1. 在 `ProcessScriptLine()` 中保留“扫描所有 `DataBreakpoints`、同步状态、收集待清除 `Ids`”的循环，但把 `TriggeredBreakpoints` 改成聚合载荷，而不是后面逐项 `PauseExecution()`。 2. 扫描结束后若本轮至少有一个命中，只构造一次 `FStoppedMessage` 并调用一次 `PauseExecution()`；`Text`/`Description` 可以先用换行拼接所有命中文案，等后续协议支持更丰富 payload 时再扩展结构化字段。 3. 该 stop 的 `Reason` 应与已追踪的 data breakpoint stop-reason 修复保持一致，不要继续因为“有多个命中”就隐式制造多次 `pause`/`continue`。 4. `ClearDataBreakpoints` 的发送时机保持在单次暂停之后统一处理，确保“命中后禁用/移除”的 watchpoint 仍只广播一次状态变化，而不是跟随每条文案重复广播。 5. 如果后续推进 thread-scoped pending-stop 方案，本条聚合应直接挂到同一个 pending-stop payload 上，避免修复 stop storm 后又被新的 per-thread 队列拆回多次停点。 6. 增加两组回归：一组让同一写操作命中两个已安装 watchpoint，断言只收到一次 `HasStopped`；另一组让两个栈上 watchpoint在同一次 `ProcessScriptStackPop()` 中失效，确认也只需要一次 `Continue` 就能前进。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果聚合 stop 时只保留第一条命中文案，会让其它同时命中的 watchpoint 变成静默信息丢失；因此至少要保留全部命中的文本或 id 明细。 |
| 前置依赖 | 建议与已追踪的 data breakpoint stop-reason 规范化方案一起落地，避免一次修 stop storm、一次再改 stopped payload。 |
| 验证方式 | 1. 新增多 watchpoint 同时命中回归，确认服务器只发送一次 `HasStopped` 和一次后续 `HasContinued`。 2. 验证单 watchpoint 场景行为保持不变，不引入额外停点丢失。 3. 人工在 watchpoint + out-of-scope 场景下验证“按一次 continue 就前进一次”的交互恢复正常。 |

### Issue-39：补足 Analysis 发现 59 的修复方案：precompiled/JIT cache 入口只校验 `BuildIdentifier`，坏引用会在深层解析中 `checkf` 崩溃而不是安全 fallback

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h` |
| 行号 | `AngelscriptEngine.cpp:1513-1556`；`PrecompiledData.cpp:1732-1805, 1972-2143, 2227-2286, 2330-2347, 2376-2477, 2642-2689`；`StaticJITConfig.h:12-13` |
| 问题 | 引擎在 `bUsePrecompiledData` 路径下只要 cache 文件存在，就直接 `PrecompiledData->Load(Filename)`；唯一入口级有效性判断是 `IsValidForCurrentBuild()`，而它只比较 `BuildIdentifier`。随后真正解析这些引用时，`GetTypeInfo()`、`GetFunction()`、`GetFunctionId()`、`GetGlobalVariable()` 以及 `PrepareToFinalizePrecompiledModules()` 遇到缺失模块、缺失类型、缺失函数、缺失全局或 layout 漂移时，都会 `checkf` 直接终止。更糟的是 `GetPropertyOffset()` 在 `PropertyReferences.Find(...)` 失败时不会拒绝该 cache，而是直接回退到编译期旧 offset。 |
| 根因 | precompiled/JIT cache 的设计把“build 配置匹配”误当成“引用图和 ABI 也安全”，缺少独立的 runtime validation/fallback 阶段；错误处理一部分是 fatal assertion，一部分又是静默继续使用旧数据，语义不一致。 |
| 影响 | build id 相同但内容截断、局部损坏、热更残留、脚本/类型/属性引用漂移的 cache，不会在装载入口被识别为无效，而是会在后续模块恢复或 `PrepareToFinalizePrecompiledModules()` 时直接崩溃；即使没有立刻崩，`GetPropertyOffset()` 也可能把旧 offset 带进 JIT 路径，造成 JIT 与解释执行结果分叉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在启用 precompiled/JIT bundle 之前增加显式 runtime validation，并把所有“坏 cache”统一收敛到“记录原因后禁用当前 bundle，回退正常编译/VM”这一条路径。 |
| 具体步骤 | 1. 在 `FAngelscriptPrecompiledData` 中新增显式验证入口，例如 `ValidateForCurrentRuntime(FPrecompiledValidationReport&)`，在 `AngelscriptEngine.cpp:1533-1556` 将 cache 交给 engine 使用之前执行；验证内容至少覆盖：模块存在性、类型/函数/全局引用可解析、property reference 可映射到当前 `byteOffset`、以及 `PrepareToFinalizePrecompiledModules()` 现有的 size/alignment/property-offset 一致性检查。 2. 将 `GetTypeInfo/GetFunction/GetFunctionId/GetGlobalVariable/GetPropertyOffset` 拆成 “`TryGet*` 返回 `bool`/nullable + 错误信息” 与 “内部断言版” 两层；runtime validation 和 cache 激活路径使用前者，禁止在用户运行态直接 `checkf`。 3. `GetPropertyOffset()` 在 `PropertyReferences.Find(...) == nullptr` 时不得再静默返回旧 offset；这应直接记为 validation failure，并禁用当前 precompiled/JIT bundle，而不是让 JIT 继续拿编译期偏移访问运行期对象。 4. 当 validation 失败时，在 `AngelscriptEngine.cpp` 统一执行：记录详细 warning、丢弃 `PrecompiledData`、关闭当前 bundle 的 transpiled/JIT 激活、并回退到普通脚本编译/解释执行；不要进入后续 `PrepareToFinalizePrecompiledModules()` 或留下半激活的 `FJITDatabase` 状态。 5. `AS_JIT_VERIFY_PROPERTY_OFFSETS` 下现有 `checkf` 校验要改成“写入 validation report 并触发 fallback”；Shipping/Test 构建也要复用同一套验证，而不是因为宏关闭就失去 layout 漂移护栏。 6. 增加负向测试：构造截断 cache、缺失 type/function/global reference、缺失 property reference、以及 property/type layout 漂移四类样例，断言引擎会禁用该 cache 并继续启动，而不是崩溃或静默使用旧 offset。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 预估工作量 | L |
| 风险 | 这条改造会触碰 precompiled data 恢复链路的核心 lookup API；如果 validation 与激活阶段没有严格分层，容易一边试图“安全校验”，一边又在旧调用点触发原有 `checkf`。 |
| 前置依赖 | 建议与已记录的 `Issue-26` 一起设计，统一梳理 `FJITDatabase` 的“静态注册表”和“本轮激活状态”，避免 validation 失败后留下污染的全局 JIT 解析结果。 |
| 验证方式 | 1. 新增损坏/缺失引用 cache 回归，确认引擎记录 warning 后回退正常编译或 VM，不再断言退出。 2. 新增 property/type layout 漂移回归，确认不再静默使用旧 offset，JIT 与解释执行结果保持一致。 3. 回归正常 `PrecompiledScript*.Cache` 装载场景，确认合法 bundle 仍可成功激活并命中 JIT entry。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-39 | Defect | 先修 precompiled/JIT cache 的 validation/fallback，避免坏 cache 在 packaged 路径直接崩溃或静默用旧 offset |
| P1 | Issue-37 | Defect | 然后修 data breakpoint 更新线程的 handle leak，防止长时间调试把句柄资源耗尽 |
| P1 | Issue-38 | Defect | 最后修多 watchpoint 同轮 flush 的 stop storm，恢复 continue/step 的基本交互正确性 |

---

## 发现与方案 (2026-04-08 17:29)

### Issue-40：`PrecompiledData` 只保留模块首个 script section，multi-file module 的 VM fallback 会把源码定位串到第一份文件

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp` |
| 行号 | `PrecompiledData.cpp:527-579, 1417-1485, 1488-1496, 1535-1542`；`AngelscriptEngine.cpp:4343-4345`；`as_scriptfunction.cpp:1518-1523` |
| 问题 | 引擎编译一个 module 时会把 `Module->Code` 里的每个 `FCodeSection` 都 `AddScriptSection(...)` 到同一个 `asCModule`。但 `FAngelscriptPrecompiledModule::InitFrom()` 只把 `Context.ModuleDesc->Code[0].RelativeFilename` 写进单个 `ScriptRelativeFilename`；`ApplyToModule_Stage1()` 又把这个单值挂到 `Context.ScriptRelativeFilename`；随后每个没有命中 JIT entry 的函数在 `FAngelscriptPrecompiledFunction::Create()` 中都会统一执行 `scriptData->scriptSectionIdx = Context.GetScriptSection()`。`GetScriptSection()` 本身只按这一个 `ScriptRelativeFilename` 解析 section index。`asCScriptFunction::GetScriptSectionName()` 则直接用 `scriptSectionIdx` 回查源码 section 名称。结果是只要一个 precompiled module 含有多个源码文件，所有走 VM bytecode 的函数都会被标记成模块第一份文件的 section。 |
| 根因 | precompiled 恢复链把“module 有多个 script sections”错误压缩成了“module 只有一个 `ScriptRelativeFilename`”，函数级源码归属信息在序列化时已经丢失。 |
| 影响 | 这会让 multi-file module 下的 VM fallback、fully precompiled bytecode 执行、异常定位和任何依赖 `GetScriptSectionName()` / `scriptSectionIdx` 的调试信息全部串文件。用户看到的现象不是简单的行号偏差，而是函数明明来自文件 B，却在 call stack、异常消息或调试定位里被报告成文件 A。JIT coverage 不完整时，这会直接制造“JIT 命中函数定位正常，JIT miss fallback 函数定位错误”的结果分叉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 script section 从 module 级单值提升为 function 级持久化字段，恢复每个函数自己的源码 section。 |
| 具体步骤 | 1. 在 `FAngelscriptPrecompiledFunction` 中新增持久化字段保存函数原始 section，例如相对文件名或已解析的 section key；序列化时从 `asCScriptFunction::GetScriptSectionName()` / `scriptData->scriptSectionIdx` 提取，而不是依赖 module 级 `Code[0]`。 2. 在 `FAngelscriptPrecompiledFunction::Create()` 中移除 `Context.GetScriptSection()` 这条 module 级默认路径，改为根据函数自己的持久化 section 解析并写入 `scriptData->scriptSectionIdx`。 3. `FAngelscriptPrecompiledModule` 保留 module 级 `ScriptRelativeFilename` 仅用于确实只有 module 级语义的对象；不要再让全体函数共享它。 4. 为向后兼容旧 cache，加载旧版本 precompiled 数据时要显式标记“缺少函数级 section 元数据”，此时宁可禁用该 bundle 的调试定位或整体回退重编译，也不要继续把所有函数静默绑到首文件。 5. 增加回归测试：构造一个包含两个 `FCodeSection` 的 module，在两个文件各放一个函数，强制其中一个函数走 VM fallback，断言 `GetScriptSectionName()`、异常位置和 call stack source 都落在正确文件。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 预估工作量 | M |
| 风险 | 需要给 precompiled 数据格式加版本迁移；如果兼容策略处理不好，旧 cache 可能在加载时被误判为可用并继续输出错误定位。 |
| 前置依赖 | 无，但建议和已记录的 `Issue-4` 一并验证 multi-file module 的调试一致性，避免修完 breakpoint store 后 VM fallback 仍然串文件。 |
| 验证方式 | 1. 新增 multi-file precompiled module 回归，验证每个函数恢复后的 `GetScriptSectionName()` 与原文件一致。 2. 人工制造 JIT miss / VM fallback，确认异常与 call stack 不再统一指向 `Code[0]`。 3. 回归单文件 module，确认 section 恢复逻辑不影响现有 packaged/JIT 场景。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-40 | Defect | 先修函数级 script section 恢复，避免 multi-file module 在 VM fallback 时整体串文件 |

---

## 发现与方案 (2026-04-08 17:35)

### Issue-41：补足 Analysis 发现 71 的修复方案：`SetDataBreakpoints` 直接信任原始 `Address/AddressSize`，会把任意地址和非法尺寸写进硬件监视寄存器

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:134-168, 172-202, 1061-1067, 1091-1098, 1118-1124`；`AngelscriptType.h:727-745`；`Bind_UStruct.cpp:288-294` |
| 问题 | `HandleMessage(SetDataBreakpoints)` 收到请求后直接 `DataBreakpoints = BP.Breakpoints; UpdateDataBreakpoints();`，没有校验地址来源、地址范围或尺寸。随后 `ApplyBreakpointsToThreadContext(...)` 会把 `Address` 直接写入 `Dr0-Dr3`，并仅对 `AddressSize` 的 `1/2/4/8` 做分支编码；其它值会静默落到默认的 `BreakpointSettings = 0x1`，等价于 1-byte write watchpoint。与此同时，`Variables/Evaluate` 返回给客户端的 `ValueSize` 又来自 `FDebuggerValue::GetAddressToMonitorValueSize()`，默认会透传 `Usage.GetValueSize()`；结构体类型的 `GetValueSize()` 明确返回 `UStruct` 的真实 `PropertiesSize`，并不限制在 8 字节以内。 |
| 根因 | 协议把 data breakpoint 建模成“客户端直接下发硬件 watchpoint 参数”，服务端既没有 server-side capability/token，也没有在安装前把尺寸/地址约束收敛到 x86 debug register 的真实支持集合。 |
| 影响 | 这会同时带来两类错误：1. 任意客户端都能请求监视任意进程地址；2. 即使地址来自合法 `Variables/Evaluate`，像 `FVector`、自定义 struct 这类 `ValueSize > 8` 的值也会被静默降成 1-byte 监视，命中语义与 UI 显示的“整个值被监视”完全不一致。结果不是确定性失败，而是误报、漏报和对首字节写入的伪命中。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 data breakpoint 从“客户端指定裸地址”改成“服务器签发 monitor token 并严格验证尺寸/范围”，同时显式拒绝 x86 不支持的监视宽度。 |
| 具体步骤 | 1. 为 `Variables/Evaluate` 增加 server-issued monitor token，例如 `{Address, Size, ScopeKind, Generation, bCppAllowed}` 的签名/opaque id，`SetDataBreakpoints` 只接受 token，不再接受客户端自由填写的 `Address` 与 `bCppBreakpoint`。 2. 在服务端新增 `ValidateDataBreakpointRequest(...)`，至少强制 `AddressSize` 属于 `{1,2,4,8}`、地址非空、地址与 token/当前调试帧匹配；不满足时返回显式 reject，而不是继续写 `Dr7`。 3. 对 `ValueSize > 8` 的 monitorable 值统一走“拒绝并回包原因”策略，短期不要静默拆成多个硬件断点；否则需要额外解决跨槽位聚合命中与 4 槽上限管理。 4. `bCppBreakpoint` 必须改成服务器侧推导属性，而不是客户端可写字段；只有明确来自允许的 native monitor source 时，命中后才允许走 C++ break 语义。 5. 在协议层增加 per-breakpoint ack/status 返回，明确告诉客户端哪些 data breakpoint 被接受、哪些因为 `unsupported size` / `invalid token` / `out of scope` 被拒绝。 6. 增加回归测试：发送任意地址、发送 `AddressSize=12/16`、以及对 struct value 建立 data breakpoint，断言服务端返回 reject 且不会修改硬件 watchpoint 状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerDataBreakpointTests.cpp` |
| 预估工作量 | L |
| 风险 | 这会引入协议变更；如果客户端和服务端不能同步升级，需要保留一段兼容期并在旧协议上明确禁用裸地址模式。 |
| 前置依赖 | 建议与已记录的 `Issue-18`、`Issue-31` 一起设计，统一解决“超出硬件上限的反馈”和“active mirror 的线程安全发布”。 |
| 验证方式 | 1. 协议测试覆盖非法尺寸与伪造地址 token，确认服务端明确拒绝。 2. 集成测试覆盖 struct value / array element 等 `ValueSize > 8` 场景，确认不会静默退化成 1-byte watchpoint。 3. Windows 实机验证合法 1/2/4/8-byte token 仍能正常安装并命中。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-41 | Defect | 先封住任意地址和非法尺寸写入硬件寄存器的入口，再继续修 data breakpoint 细节语义 |

---

## 发现与方案 (2026-04-08 17:37)

### Issue-42：补足 Analysis 发现 69 的修复方案：DebugServer V2 没有 request correlation，`Evaluate` / `Variables` / `CallStack` 无法满足 DAP 的请求归属语义

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 行号 | `AngelscriptDebugServer.h:86-90, 216-225, 416-450`；`AngelscriptDebugServer.cpp:924-927, 1081-1128, 1382-1482`；`AngelscriptDebuggerTestClient.cpp:183-218, 284-309` |
| 问题 | 当前 envelope 只有 `MessageType + Body`，没有任何 `requestId` / `seq` / `request_seq`。对应消息体里，`CallStack` 只带帧列表，`Variables` 只带变量数组，`Evaluate` 只带值本身；服务端发送这些响应时不会回显请求参数，也不会附带关联 token。测试客户端的等待逻辑也印证了这一点：`WaitForMessageType(...)` 只能按消息类型阻塞，`SendRequestCallStack/Variables/Evaluate` 也都没有请求编号。像 `RequestCallStack` 甚至会先把 socket 放进 `CallstackRequests`，等未来某次暂停时再统一发送。 |
| 根因 | 协议层把所有交互都设计成“同类型消息的顺序流”，缺少最小的 request/response contract；这让服务端内部的延迟发送、广播消息和同类型并发请求都无法在 wire 上被可靠区分。 |
| 影响 | 只要客户端出现并发 `Evaluate/Variables/CallStack`、重试、乱序或等待期间插入 `HasStopped`/`Diagnostics`/database push，协议本身就无法判断哪条响应对应哪次请求。DAP 适配层因此只能强依赖单飞与 FIFO 假设，像 call stack 延迟到下一次暂停再发这种实现也会天然错配到错误请求。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 DebugServer V2 升级成显式的 request/response/event 模型，所有可应答消息都必须携带 per-client correlation id。 |
| 具体步骤 | 1. 在协议层新增统一消息头，例如 `{uint32 SequenceId, uint32 CorrelationId, uint8 Flags}`，或直接在自定义体里引入 `RequestId/ResponseTo/Success/ErrorText`；规则必须明确区分 request、response、event。 2. `RequestCallStack`、`RequestVariables`、`RequestEvaluate`、`GoToDefinition`、`FindAssets`、`CreateBlueprint`、`RequestDebugDatabase` 等所有“请求后期望某种结果”的消息都改成带 `RequestId` 的 request body；对应 response 复用同一个 `RequestId` 回传。 3. 服务端内部把 `CallstackRequests` 从 `TArray<FSocket*>` 重构成按 client + requestId 挂起的队列，例如 `TArray<FDeferredRequestCallstack>`；暂停后发送 call stack 时要回到原请求 id，而不是只按 socket flush。 4. `HasStopped`、`HasContinued`、`Diagnostics`、database/asset push 保持为 event，但 event 与 response 必须在协议字段上可判别，避免客户端继续按“只看 message type”推断归属。 5. 测试客户端与正式适配器统一改成按 `RequestId` 等待响应，而不是 `WaitForMessageType(...)`；只有 event 才允许按类型订阅。 6. 通过 `DEBUG_SERVER_VERSION` 或新增 capability 握手声明协议升级；旧客户端若不支持 correlation id，应被显式降级到单飞模式或拒绝连接，而不是混用新旧语义。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 预估工作量 | L |
| 风险 | 这是协议层 breaking change；如果迁移期兼容策略不清晰，现有 IDE 集成和测试客户端会全部断连。 |
| 前置依赖 | 建议与已记录的 `Issue-35` 一起推进，把 session membership 和 request correlation 一并收口；否则即使有 request id，旁路客户端仍可能收到不属于自己的 event/response。 |
| 验证方式 | 1. 新增协议测试，模拟同一客户端并发发送两个 `RequestEvaluate` 和一个 `RequestVariables`，验证三条响应都能按 `RequestId` 正确归属。 2. 新增“请求期间插入 `HasStopped`/`Diagnostics` event”测试，确认客户端不会把 event 误当 response。 3. 集成测试覆盖“先请求 call stack，后在下一次暂停才返回”的延迟路径，确认返回包仍携带原始 request id。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-42 | Architecture | 先补 request correlation，给后续 DAP 兼容和异步响应修复建立协议地基 |

---

## 发现与方案 (2026-04-08 17:49)

### Issue-43：补足 Analysis 发现 38 的修复方案：StaticJIT 只更新 `debugCallStack`，没有把逐行停点/步进桥接回 `DebugServer`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `StaticJITHeader.h:194-217, 338-340`；`AngelscriptStaticJIT.cpp:337-343, 373-380`；`AngelscriptEngine.cpp:5429-5461, 5463-5562` |
| 问题 | JIT 代码生成当前只做两件调试相关工作：函数入口通过 `SCRIPT_DEBUG_CALLSTACK_FRAME*` 压入 `FScopeJITDebugCallstack`，逐条 bytecode 通过 `SCRIPT_DEBUG_CALLSTACK_LINE` 更新 `DebugCallstack.LineNumber`。与此同时，`DebugServer` 的 line breakpoint、`Pause` 落到下一脚本行、`StepIn/StepOver/StepOut`、以及 `ProcessScriptStackPop()` 的调用，全都依赖 `UpdateLineCallbackState()` 打开解释器 `asCContext` line callback，然后由 `AngelscriptLineCallback()` / `AngelscriptStackPopCallback()` 调用 `DebugServer->ProcessScriptLine(Context)` / `ProcessScriptStackPop(Context, ...)`。源码里没有任何 JIT 路径会把 `FScriptExecution` 的 safe point 回调到这两条入口。结果是 JIT 覆盖函数虽然会更新文本式 `debugCallStack`，却不会参与 `DebugServer` 的逐行停点判定。 |
| 根因 | StaticJIT 把“记录当前位置”与“参与调试控制流”拆成了两套完全独立的机制：JIT 侧只维护 `debugCallStack`，解释器侧才拥有 line callback / stack-pop callback，而两者之间没有 backend-neutral 的 safe-point bridge。 |
| 影响 | 只要执行进入 JIT 覆盖函数，`DebugServer` 的 line breakpoint、步进、以及依赖“下一条脚本行”兑现的暂停语义就会退化成只在解释器帧上生效。前面已记录的 JIT `CallStack`、`exception`、`DebugBreak` 问题即使单点修掉，这条缺口不补，用户仍会得到“看得见 JIT 帧，但断不住/步不动”的分叉行为。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 JIT 引入与解释器 line callback 对齐的 backend-neutral safe-point 入口，让 `SCRIPT_DEBUG_CALLSTACK_LINE` 不再只是改行号，而是能驱动统一的停点判定。 |
| 具体步骤 | 1. 在 runtime/debugging 层抽出统一 helper，例如 `ProcessScriptSafePoint(...)` / `ShouldPauseAtScriptLocation(...)`，输入至少包含执行后端（Interpreter/JIT）、脚本函数标识、source filename、line number、以及可选的 `asCContext*` 或 `FScriptExecution*`。这层只负责判定 breakpoint/step/pending-pause 是否应在当前 safe point 停下，不再把规则散落在 `ProcessScriptLine()` 私有实现里。 2. 保留现有解释器 `AngelscriptLineCallback()` 路径，但把 `DebugServer->ProcessScriptLine(Context)` 内部可复用的停点决策下沉到新 helper；`ProcessScriptLine()` 变成“从 `asCContext` 提取 safe-point 信息后调用统一 helper”。 3. 扩展 `StaticJITHeader.h` / `AngelscriptStaticJIT.cpp`：`SCRIPT_DEBUG_CALLSTACK_LINE` 在更新 `DebugCallstack.LineNumber` 后，同时调用轻量 JIT safe-point hook，例如 `SCRIPT_DEBUG_SAFEPOINT(Execution, FunctionId, LineNumber)`；该 hook 必须先做快速门禁，只有 `DebugServer`、coverage、debug values 或 pending stop 实际启用时才进入较重逻辑，避免把每条 JIT 指令都变成无条件高开销回调。 4. 为 JIT safe-point 增加 source/section 映射输入，不要只传裸行号；否则会把当前已追踪的 multi-file/module breakpoint 问题复制到 JIT 路径。优先复用 `ScriptFunction` 自身的 section 信息，或在 codegen 时把 canonical filename 常量一并生成出来。 5. `ProcessScriptStackPop()` 相关语义不要继续只挂在解释器 callback 上；若 JIT 局部变量/栈上 watchpoint 也要支持 out-of-scope 清理，需要同步设计 JIT frame-exit hook，或在第一阶段显式声明“JIT locals data breakpoint 暂不支持”，不要默默漏掉。 6. 增加非 editor/JIT 集成测试：在真正命中 `FJITDatabase` 的函数内部设置 line breakpoint，再分别验证 `StepOver`、`StepIn`、`Pause` 都能在 JIT 函数内收到 `HasStopped`；同一套 fixture 需要和解释器态做对照，确保停点行号一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下新增 JIT stepping/breakpoint 集成测试 |
| 预估工作量 | XL |
| 风险 | 一旦把 safe point 带入 JIT，每条可执行行都可能引入额外分支与同步点；如果门禁做得不够激进，容易造成 packaged/JIT 热路径性能回退。另一方面，如果不把解释器与 JIT 的停点判定收敛到同一 helper，又会引入第二套语义漂移。 |
| 前置依赖 | 建议与 `Issue-15` 的 precompiled/JIT 调试 harness 和 `Issue-24` 的 JIT frame materialization 协同设计；前者提供可验证宿主，后者保证停住后不只是能停，还能继续做变量/栈查看。 |
| 验证方式 | 1. 新增 JIT line breakpoint / stepping 集成测试，确认停点发生在 JIT 函数内部而不是返回解释器后。 2. 对照解释器态运行同一 fixture，验证 source filename 与 line number 一致。 3. 关闭全部调试功能后跑性能回归或至少确认生成代码里的 safe-point hook 能被快速门禁短路。 |

### Issue-44：补足 Analysis 发现 33 的修复方案：`StepIn` / `StepOver` / `StepOut` 没有 stopped-state 门槛，running 态请求会在下一条任意脚本行全局生效

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:847-895, 465-628`；`AngelscriptDebuggerTestClient.cpp:266-282`；`AngelscriptDebuggerSteppingTests.cpp:390-650` |
| 问题 | `HandleMessage()` 收到 `StepIn` / `StepOver` / `StepOut` 时，会直接设置 `bBreakNextScriptLine = true`、清空或重设 `ConditionBreakFrame/ConditionBreakFunction`，但整个分支从不检查当前是否真的处于 `bIsPaused == true` 的 stopped state。随后 `ProcessScriptLine()` 只要看到 `bBreakNextScriptLine`，就会在下一次满足条件的 line callback 上进入 `step` 停止。这意味着 client 只要在 running 态、重放旧命令、或在还没收到 `HasStopped` 时误发 step，请求也会被服务器当成有效控制。现有测试客户端 `SendStepIn/SendStepOver/SendStepOut()` 只是裸发消息，stepping tests 也全部建立在“收到 stop 后再发 step”的正向路径上，没有任何 running-state 负例。 |
| 根因 | DebugServer 把 step 命令实现成“改写下一条脚本行的全局暂停条件”，而不是“恢复当前已停止目标并附带 step intent”；协议层缺少 stopped-state gate，也缺少对 active stop owner 的校验。 |
| 影响 | 一旦前端有重试、乱序、旧 stop 状态残留或多个 client 并存，step 命令就可能在错误时机生效，把脚本拖到下一条任意脚本行上停住。`StepOver` / `StepOut` 在 `asGetActiveContext()==nullptr` 时还会退化成清空条件后的“下一行即停”，进一步放大误停概率。这直接破坏 DAP 对 `next/stepIn/stepOut` 必须基于 stopped thread 的基本语义。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 step 从“全局布尔开关”改成“只能作用于 active stopped target 的 resume mode”，running 态 step 必须被拒绝或显式 no-op。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer` 内引入显式的 paused ownership 状态，例如 `FActiveStopState { bool bPaused; uint32 Generation; uint32 ThreadId; EStopReason Reason; }` 或最小版本的 `bHasActiveStop + StopGeneration`；`StepIn/StepOver/StepOut/Continue` 进入前先校验当前确实存在 active stop，而不是只看消息类型。 2. 将 `HandleMessage(EDebugMessageType::Step*)` 改成两阶段：先验证 `bIsPaused == true` 且请求方属于当前 debugging session，再记录 `ResumeMode = StepIn/StepOver/StepOut`，最后清除 paused 状态；若当前未 paused，则直接拒绝并记录诊断，不得改写 `bBreakNextScriptLine`。 3. 把现有 `bBreakNextScriptLine + ConditionBreak*` 下沉成“resume intent”的执行细节，只允许 `PauseExecution()` 退出时设置；running 态消息分支不再直接触碰这些字段。 4. 若暂时还没有 thread-aware stop 模型，至少先把 gate 做在 server-global active stop 上，避免现在这种任何时刻都能注入 step 的行为；等后续落地 `Issue-29` 时，再把 stop ownership 下沉到 thread/session 维度。 5. 在协议层补一个最小反馈机制，优先方案是新增 error/diagnostic event，明确告诉 client “step requires stopped state”；即便短期不扩协议，也要在日志里留下可追踪 warning，避免无声 no-op。 6. 增加负向测试：脚本 running 时直接发送 `StepOver`/`StepOut`，断言后续不会平白出现 `HasStopped(reason="step")`；再保留现有正向 stepping tests，确认 paused state 内的 step 行为不回归。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp` |
| 预估工作量 | M |
| 风险 | 某些现有 adapter 可能默认把 step 当“尽快在下一行停下”的弱语义使用，修复后会从“偶发有效”变成“明确拒绝/no-op”。如果没有同时补充客户端诊断，使用者可能把行为变化误解为新 bug。 |
| 前置依赖 | 建议与 `Issue-35` 的 session gate 和 `Issue-29` 的 thread-aware pause state 协调设计；本条可以先做最小 stopped-state gate，但不要再把新语义继续绑死在 server-global bool 上。 |
| 验证方式 | 1. 新增 running-state `StepIn/StepOver/StepOut` 负向测试，确认不会在下一条任意脚本行意外停下。 2. 回归现有 stepping tests，确认 paused state 内的正常步进行为保持不变。 3. 若新增 error/diagnostic 反馈，验证 client 能收到明确的“step requires stopped state”信号。 |

### Issue-45：补足 Analysis 发现 80 的修复方案：`BreakOptions` 只影响普通 line breakpoint，异常与 data breakpoint 完全绕过过滤器

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:440-462, 479-545, 602-605, 652-664, 1137-1145`；`AngelscriptDebuggerSmokeTests.cpp:90-123`；`AngelscriptDebugProtocolTests.cpp:217-226` |
| 问题 | `HandleMessage(EDebugMessageType::BreakOptions)` 只是把 client 下发的 filter 名称写入成员 `BreakOptions`。后续真正消费这组配置的地方只有 line breakpoint 命中分支里的 `ShouldBreakOnActiveSide()`。相比之下，解释器异常路径 `ProcessException()` 会直接构造 `Reason = "exception"` 并 `PauseExecution()`；data breakpoint flush 也会直接拼文案后暂停，两个路径都不会查询 `BreakOptions`。现有测试同样只覆盖 `BreakFilters` 的握手和纯 round-trip，没有任何运行时断言去验证 filter 是否对 exception/data breakpoint 生效。 |
| 根因 | break filter 被实现成 line breakpoint 的局部钩子，而不是统一的 stop-decision pipeline。协议层虽然向 client 暴露了一套会话级 `BreakOptions`，运行时却没有把它贯穿到所有 stop source。 |
| 影响 | 前端如果把 `BreakOptions` 当成一套统一的“本侧/对侧/异常/data breakpoint”过滤能力，就会得到不一致结果：普通断点可能被 filter 掉，异常和 data breakpoint 却照样停下。对于需要把 DAP `setExceptionBreakpoints` 或类似 break filter 语义映射到 DebugServer V2 的适配层，这是明确的协议不一致。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `BreakOptions` 从 line-breakpoint 特判升级为统一的 stop-source filter，在所有可能生成 `HasStopped` 的路径上走同一套判定。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 新增统一 helper，例如 `ShouldEmitStop(EStopSource Source, const FStopContext& Context)`；输入至少包含 stop source（LineBreakpoint / Exception / DataBreakpoint / Pause / Step）、world-side 信息和可选文案。 2. 现有 `ShouldBreakOnActiveSide()` 不再由 line breakpoint 分支私有调用，而是被 `ShouldEmitStop(...)` 复用；`ProcessException()` 与 data breakpoint flush 在调用 `PauseExecution()` 前都必须经过这层统一判定。 3. 明确 filter 语义边界：`Pause`、`Continue`、`Step*` 这类显式用户控制不应被 `BreakOptions` 吞掉；line breakpoint、exception、data breakpoint 才是 filter 作用对象。把这一点写进注释和测试，避免以后再次语义漂移。 4. 如果当前 delegate 只接受 `WorldContext + FilterNames`，需要为它补 stop-source 维度，例如新增 `break:exception`、`break:data` 一类标准 filter name，或扩展 delegate 签名，让上层能区分是哪类 stop 在询问过滤。 5. `RequestBreakFilters` 返回的 filter 列表也要和运行时真实可用的 stop source 对齐；不要继续只暴露 line-breakpoint 能力。 6. 新增集成测试：构造一个会触发 exception 的脚本和一个 data breakpoint 场景，在装入特定 `BreakOptions` 后断言它们与普通 line breakpoint 一样会被统一过滤或统一放行。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下新增 exception/data breakpoint filter 测试 |
| 预估工作量 | M |
| 风险 | 一旦把 filter 作用面扩到 exception/data breakpoint，现有 client 若依赖当前“只有 line breakpoint 被过滤”的非一致行为，表现会发生变化。若 filter name 设计不清晰，还可能把显式用户控制和自动 stop source 混在一起。 |
| 前置依赖 | 建议与 `Issue-42` 的 request/response 模型和 `Issue-35` 的 session 状态收口一起考虑，避免 filter 仍然停留在 server-global 维度。 |
| 验证方式 | 1. 新增 break filter 运行时测试，覆盖 line breakpoint、exception、data breakpoint 三类 stop source。 2. 验证 `Pause` / `Step*` 不受 `BreakOptions` 影响，保持显式控制优先。 3. 回归 `BreakFilters` handshake/protocol tests，确认 filter 列表与实际行为一致。 |

### Issue-46：补足 Analysis 发现 81 的修复方案：stopped event 丢失 `breakpoint id`，client 无法把停点映射回具体 breakpoint 条目

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:129-142, 227-255, 275-327, 695-699`；`AngelscriptDebugServer.cpp:486-545, 621-626, 1028-1045`；`AngelscriptDebuggerBreakpointTests.cpp:401-430` |
| 问题 | 协议层请求对象 `FAngelscriptBreakpoint` 和 `FAngelscriptDataBreakpoint` 都带 `Id`，但服务端常驻状态并没有保留 line breakpoint 的 id 维度：`FFileBreakpoints` 只有 `TSet<int32> Lines`，`SetBreakpoint()` 成功后也只是把 `CodeLine` 塞进集合。停止事件本身的 `FStoppedMessage` 也只有 `Reason/Description/Text` 三个字符串，没有 `BreakpointIds` 或等价字段。data breakpoint 虽然在权威数组里保留了 `Id`，但真正停下时同样只是发一条没有 id 的 `FStoppedMessage`。现有 breakpoint 集成测试也只断言 `StopMessage->Reason == "breakpoint"`，并不验证命中的是哪个 id。 |
| 根因 | DebugServer 的 stop model 只把“是否因为 breakpoint 停下”建模成枚举字符串，没有把“命中了哪些 breakpoint 实体”纳入权威状态或 wire format。line breakpoint 存储层进一步把请求级 id 压缩成按行去重集合，导致 id 在进入运行态后立刻丢失。 |
| 影响 | client 同时维护多个断点、同一行多个 breakpoint、或需要做 DAP `hitBreakpointIds` 映射时，只能靠行号和文案猜测命中源，无法可靠更新 UI 条目、hit count、条件断点状态或 watchpoint 状态。随着后续 session 化和 request correlation 推进，这个缺口会越来越明显。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 line/data breakpoint 建立保留 `Id` 的权威状态，并把 stopped payload 扩展成可携带命中 breakpoint id 列表的结构化消息。 |
| 具体步骤 | 1. 将 `FFileBreakpoints` 从 `TSet<int32> Lines` 重构为保留请求级 id 的结构，例如 `TMap<int32, TArray<int32>> LineToBreakpointIds`，或显式的 `FResolvedBreakpoint { int32 Line; TArray<int32> Ids; }`；不要再在服务端把 line breakpoint 压扁成“只看这一行是否存在断点”。 2. 扩展 `FStoppedMessage` 或新增 `FBreakpointStoppedMessage`，至少包含 `TArray<int32> BreakpointIds`；`Reason` 仍保留给 `breakpoint/step/exception`，但命中实体必须用结构化字段表达，不再藏在 `Text` 里。 3. line breakpoint 命中时，从重构后的权威状态中取回该行对应的所有 id 写入 stopped payload；data breakpoint flush 时直接把 `Breakpoint.Id` 或本轮聚合命中的 id 集写进去。`Step`、`Pause`、`exception` 等非 breakpoint stop 则发送空数组。 4. 与 `Issue-11` 的 per-session breakpoint state 协同设计：如果不同 session 允许在同一行放不同 id，stopped payload 里的 id 集必须按触发 session 过滤，不能再走当前 server-global 并集。 5. 同步更新测试客户端与协议测试，确保 stopped payload 的新字段会被严格反序列化；不要重演当前 `CallStack` optional field 那种“服务端写了、客户端没读”的错位问题。 6. 增加回归测试：一个用例验证 line breakpoint 命中后 stopped payload 带回原始 id；另一个用例验证 data breakpoint 命中时也能返回对应 id。若后续支持同一行多断点，再补聚合 id 场景。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | M |
| 风险 | stopped payload 扩字段是协议变更；如果客户端/测试端不一起升级，很容易出现“服务端开始写 `BreakpointIds`，旧客户端继续按旧格式读”的兼容问题。另一方面，若先做 id 保留、后做 session 化，可能需要二次迁移权威状态。 |
| 前置依赖 | 建议与 `Issue-11` 的 session state 重构和 `Issue-42` 的 request correlation 一起规划，统一明确“breakpoint id 的作用域”和“stopped event 属于哪个 session/request”。 |
| 验证方式 | 1. 新增协议 round-trip 与集成测试，确认 line/data breakpoint 命中都会返回正确 `BreakpointIds`。 2. 回归现有 breakpoint/stepping tests，确认 `Reason` 字段保持原有语义。 3. 若保留旧协议兼容层，验证旧 client 会被明确降级或拒绝，而不是静默错读 stopped payload。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-43 | Architecture | 先补 JIT safe-point 到 `DebugServer` 的桥接，否则 JIT 函数内的逐行断点和步进没有落点 |
| P1 | Issue-44 | Defect | 然后加 stopped-state gate，堵住 running 态 step 误触发的协议错误 |
| P2 | Issue-45 | Architecture | 再把 `BreakOptions` 提升为统一 stop-source filter，收敛 break filter 语义 |
| P2 | Issue-46 | Architecture | 最后扩 stopped payload 的 breakpoint id 语义，为 DAP 适配和多断点 UI 补齐协议能力 |

---

## 发现与方案 (2026-04-08 17:59)

### Issue-47：补足 Analysis 发现 77 的修复方案：Windows data breakpoint 更新线程在声明 `CONTEXT_DEBUG_REGISTERS` 之前就读取线程上下文

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:236-253` |
| 问题 | `DataBreakpoint_Windows::FUpdateDebugRegisterThread::Run()` 先把 `CONTEXT` 清零并调用 `GetThreadContext(ThreadToDebug, &Context)`，直到返回成功后才在 `247` 行补写 `Context.ContextFlags = CONTEXT_DEBUG_REGISTERS`。Win32 的 `GetThreadContext()` 约定要求调用前就通过 `ContextFlags` 声明要读取哪一组寄存器；当前实现却在读完之后才声明“我要 debug registers”。同一段源码还紧跟着注释 `Doesn't seem like the data returned here is the same as the one we set`，说明作者已经观察到读回内容与预期不一致。 |
| 根因 | data breakpoint 更新线程把 `CONTEXT` 的输入契约顺序写反了，把“请求调试寄存器快照”放到了 `GetThreadContext()` 之后，导致后续 `Dr7` 读改写建立在没有已验证前提的上下文上。 |
| 影响 | `SetDataBreakpoints` 的 Windows 安装路径缺少稳定基础：轻则 `GetThreadContext()` 失败并放弃更新，重则 `ApplyBreakpointsToThreadContext()` 和后续 `SetThreadContext()` 使用未定义或不完整的 `Dr0-Dr7` 状态，表现为 watchpoint 偶发失效、安装结果依赖机器/时序、或后续维护者继续在错误前提上叠加修复。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 debug register 读改写流程收敛成一个严格遵守 `CONTEXT_DEBUG_REGISTERS` 契约的 helper，先声明要读的寄存器集，再读取、修改并写回。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 抽出小 helper，例如 `bool TryReadDebugRegisterContext(HANDLE Thread, CONTEXT& OutContext)`；进入 helper 后先 `FMemory::Memzero(OutContext)`，再立刻设置 `OutContext.ContextFlags = CONTEXT_DEBUG_REGISTERS`，最后调用 `GetThreadContext()`。 2. `FUpdateDebugRegisterThread::Run()` 改为只通过该 helper 获取快照，禁止继续在 `GetThreadContext()` 返回后才补写 `ContextFlags`。 3. 若 `GetThreadContext()` 失败，除了现有日志外还要把这次 data breakpoint rebuild 标记为失败，并保持 authoritative `DataBreakpoints` 不被误认为已同步到硬件寄存器；不要继续落到 `ApplyBreakpointsToThreadContext()`。 4. 把 `SetThreadContext()` 之前的 `Context.ContextFlags |= CONTEXT_DEBUG_REGISTERS` 保留为显式写回声明，形成“读前设置一次、写前再确认一次”的对称流程。 5. 在同一文件补注释，明确 `CONTEXT.ContextFlags` 是 `GetThreadContext()` 的输入条件，不是输出元数据，防止后续维护时再次把顺序改回去。 6. 增加 Windows 专项测试 seam，允许 transport/protocol 之外的单测用 fake `GetThreadContext` 验证调用顺序；如果当前 harness 无法 mock Win32 API，至少新增一个仅 Windows 运行的诊断断言，在开发构建里记录“读取前 `ContextFlags` 必须已是 `CONTEXT_DEBUG_REGISTERS`”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增或扩展 Windows data breakpoint 测试 |
| 预估工作量 | S |
| 风险 | 这条修复会把原本“偶发还能工作”的未定义行为收紧成确定语义，可能暴露后续 `Dr7` 初始化或命中处理里的其它隐藏问题；但这是必须先清理的前置基础。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 在 Windows 上反复设置/清除 data breakpoint，确认 `GetThreadContext()` 不再因为缺失 `ContextFlags` 前置条件而失败。 2. 增加诊断或 mock 用例，验证 `GetThreadContext()` 调用前 `Context.ContextFlags` 已经被设置为 `CONTEXT_DEBUG_REGISTERS`。 3. 回归现有 data breakpoint 功能路径，确认修复后 watchpoint 命中行为更稳定而非退化。 |

### Issue-48：补足 Analysis 发现 78 的修复方案：`Dr7` 初始化常量把保留高位写脏，所有 Windows data breakpoint 更新都会带入错误寄存器位

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:248-253` |
| 问题 | `FUpdateDebugRegisterThread::Run()` 在初始化 `Dr7` 时先执行 `Context.Dr7 &= 0x0000000000003F00`，注释明确说明“bit 10 should be 1, bit 14,15 and 32-63 should be 0”。但下一行却写入 `Context.Dr7 |= 0x001000000000`。这个字面量不是 `1ull << 10`，而是把更高位重新置 `1`，与同一段注释声明的“32-63 应清零”直接矛盾。也就是说，即使客户端只请求 1 个合法 data breakpoint，这条初始化路径仍会在每次更新时污染 `Dr7` 的保留高位。 |
| 根因 | debug register 固定位的掩码使用了错误常量，代码把“bit 10 固定为 1”的要求实现成了“写入某个高位 magic number”，且没有任何命名常量或静态断言保护位号。 |
| 影响 | 这会把每一次 Windows data breakpoint 更新都变成“向 `Dr7` 写入自相矛盾的状态”。后果可能表现为 `SetThreadContext()` 失败、硬件监视点行为不稳定、不同机器对同一 `SetDataBreakpoints` 请求结果不一致，或后续异常处理读取到被污染的状态位。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用具名位常量重写 `Dr7` 初始化，显式保证只保留允许继承的低位并设置架构规定的固定位，禁止再使用难以审计的 magic literal。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 为 `Dr7` 固定位定义具名常量，例如 `constexpr DWORD64 DR7_LE_CONTROL_MASK = 0x3F00ull;`、`constexpr DWORD64 DR7_FIXED_ONE_BIT10 = (1ull << 10);`，并用这些常量替换 `0x0000000000003F00` 与 `0x001000000000`。 2. 初始化流程改成 `Context.Dr7 &= DR7_LE_CONTROL_MASK; Context.Dr7 |= DR7_FIXED_ONE_BIT10;`，确保高位不会被重新置上。 3. 在 `ApplyBreakpointsToThreadContext(...)` 之后增加开发期断言或日志检查，例如验证 `(Context.Dr7 & ~AllowedMask) == 0`，其中 `AllowedMask` 只包含 `0-31` 中实际允许使用的控制位；这样以后再写错掩码会立刻暴露。 4. 把当前注释升级为“位图布局 + 具名常量含义”的并列说明，明确哪些位由初始化负责、哪些位由 `ApplyBreakpointsToThreadContext()` 写入，避免后续维护者再次在两处重复改 magic number。 5. 若项目支持 Windows 专项测试，补一个最小 `Dr7` 组装单测，输入 1-4 个 data breakpoint 请求后验证输出寄存器只包含预期控制位，不含任何 32-63 位脏数据。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增或扩展 Windows data breakpoint 测试 |
| 预估工作量 | S |
| 风险 | 如果现有行为长期依赖了这个错误高位的偶发副作用，修正后可能暴露真正的 watchpoint 编程缺陷；但继续保留错误 literal 会让后续任何调试都建立在错误寄存器状态上。 |
| 前置依赖 | 建议与 `Issue-47` 一起实施，先确保 `GetThreadContext()`/`SetThreadContext()` 的基本前提正确，再验证 `Dr7` 组装结果。 |
| 验证方式 | 1. 在开发构建中打印或断言修复后的 `Dr7`，确认高位 `32-63` 保持为 `0`。 2. 在 Windows 上设置 1-4 个 data breakpoint，验证 `SetThreadContext()` 不再因错误寄存器字面量失败。 3. 回归 data breakpoint 命中测试，确认修复后命中行为稳定且不再依赖机器差异。 |

### Issue-49：补足 Analysis 发现 79 的修复方案：真实执行 StaticJIT/transpiled code 的运行态在架构上就不会启动 `DebugServer`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h` |
| 行号 | `AngelscriptEngine.cpp:1425-1456, 1573-1602`；`StaticJITConfig.h:8-10` |
| 问题 | 引擎启动时把 `bUsePrecompiledData` 定义成 `!WITH_EDITOR && !bScriptDevelopmentMode` 的 packaged 运行态路径；紧接着 `DebugServer` 只在 `(!bUsePrecompiledData || bScriptDevelopmentMode)` 时创建。与此同时，真正执行 transpiled/JIT code 的路径恰恰依赖 precompiled data：`bStaticJITTranspiledCodeLoaded` 只有在加载完 precompiled data 并发现 `FJITDatabase::Get().Functions.Num() > 0` 后才会置真。再叠加 `StaticJITConfig.h` 在 `WITH_EDITOR` 下直接 `#define AS_SKIP_JITTED_CODE`，就得到一个明确结论：editor 态虽然能开 `DebugServer`，但会跳过 JIT；packaged/precompiled 态虽然可能真的执行 transpiled code，却不会启动 `DebugServer`。 |
| 根因 | `DebugServer` 生命周期被绑在“开发态/非 precompiled”条件上，而 `StaticJIT` 可执行产物被绑在“非 editor 的 precompiled 运行态”条件上，两者在启动配置上被设计成互斥集合。 |
| 影响 | 这不是单个实现 bug，而是验证闭环缺口：当前仓库里没有稳定产品配置能让 `DebugServer V2` 连接到真正执行中的 JIT 函数。前面已有的 JIT `exception`、`DebugBreak`、`safe-point`、`CallStack` 等问题即使逐条修掉，也无法在目标运行态做端到端回归，DAP 合规性只能停留在推断和局部单测。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“是否允许远程调试”和“是否使用 precompiled/JIT”从互斥启动条件里拆开，提供一个明确支持 JIT + DebugServer 共存的运行配置。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.cpp` 引入独立的 runtime capability 开关，例如 `bEnableDebugServerInPrecompiledMode` 或从命令行/ini 读取 `Angelscript.DebugServerAllowPrecompiledRuntime`，不要再直接用 `!bUsePrecompiledData` 作为 `DebugServer` 创建条件。 2. 将 `1452-1456` 的 server 启动逻辑改成“按显式配置决定是否启动”，并在日志里清楚打印当前组合：`DebugServer=On/Off`、`PrecompiledData=On/Off`、`StaticJITLoaded=On/Off`，避免继续靠宏和隐式条件推断。 3. 保留默认安全策略：Shipping 正式包仍可默认关闭远程调试，但 Development/Test/专用 QA 包必须能启用“precompiled + DebugServer”组合，这样才能验证真实 JIT 运行态。 4. 与 `AS_SKIP_JITTED_CODE` 的 editor 限制分开处理：editor 继续按现有策略跳过 JIT 没问题，但需要新增一条非 editor 自动化或专用 test harness，保证 DebugServer 连接到真的 JIT 函数，而不是永远只在解释器路径上测试协议。 5. 把前面已记录的 `Issue-9`、`Issue-43` 等 JIT 调试修复都挂到这条 harness 上统一回归，形成最小矩阵：解释器 + DebugServer、precompiled VM fallback + DebugServer、真正 JIT hit + DebugServer。 6. 若担心安全面扩大，在 capability 握手里增加显式 banner 或 build flag，确保只有受控构建能开放该端口，而不是让所有 packaged 包默认暴露调试入口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h`、`Plugins/Angelscript/Source/AngelscriptTest/` 下新增非 editor / precompiled-JIT 调试 harness |
| 预估工作量 | L |
| 风险 | 一旦允许 packaged/precompiled 运行态启动 `DebugServer`，会暴露更多此前只存在于理论上的 JIT 调试缺口；如果没有同时限制构建类型或端口策略，可能扩大运行时攻击面。 |
| 前置依赖 | 建议与 `Issue-43` 的 JIT safe-point 桥接、`Issue-9` 的 JIT `DebugBreak` 停点修复协同推进；否则即使 server 能启动，JIT 内部仍然缺少可用停点入口。 |
| 验证方式 | 1. 新增非 editor/Development 运行态测试或手工 smoke，确认同一进程内同时满足 `DebugServer != nullptr` 且 `bStaticJITTranspiledCodeLoaded == true`。 2. 在该配置下执行命中 `FJITDatabase` 的脚本函数，验证至少能建立连接并请求基本 `CallStack`。 3. 回归现有 editor/debug 开发路径，确认默认配置不被这条 capability 改造破坏。 |

### Issue-50：补足 Analysis 发现 58 的修复方案：同一客户端重复发送 `RequestDebugDatabase` 会被重复订阅，后续数据库与资产流按重复次数多次下发

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:822-826, 1485-1490, 2093-2147` |
| 问题 | `HandleMessage(RequestDebugDatabase)` 每收到一次请求就直接 `ClientsThatWantDebugDatabase.Add(Client)`，没有任何去重或“已订阅”检查。后续 `BroadcastDebugDatabase()`、`OnAssetAdded/Removed/Renamed()` 和 `OnFilesLoaded()` 又都直接遍历这个 `TArray`，并对每个元素分别调用 `SendDebugDatabase()` 或 `SendAssetDatabase()`。因此同一 socket 只要因为前端重试、超时重发或误操作多发一次 `RequestDebugDatabase`，之后的全量 database 和资产增量都会按重复次数被多次发送。 |
| 根因 | debug database 订阅状态被建模成允许重复元素的 `TArray<FSocket*>`，但协议语义实际是幂等的“打开/保持订阅”；实现没有使用 `AddUnique`、set 语义或 per-session 订阅标志。 |
| 影响 | 客户端会看到重复的 `DebugDatabaseFinished` / `AssetDatabaseInit` / `AssetDatabaseFinished` 边界和重复资产条目。轻则前端多做无意义重建，重则把这些消息当线性流处理后出现资产列表抖动、重复增量应用或 UI 状态错乱；重复订阅次数足够大时，还会放大出站带宽和队列压力。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `RequestDebugDatabase` 改成幂等订阅：同一 client 只保留一个订阅槽位，重复请求至多触发一次即时重发，不得长期放大后续推送。 |
| 具体步骤 | 1. 将 `ClientsThatWantDebugDatabase` 从裸 `TArray<FSocket*>` 升级为具备去重语义的容器；最小改动可先把 `Add(Client)` 改成 `AddUnique(Client)`，更稳妥的方案是在后续 session state 重构中把它收进 per-client 订阅标志。 2. 明确重复 `RequestDebugDatabase` 的语义：推荐“如果已经订阅，则立即重发一次当前 debug database/asset snapshot，但不新增长期订阅项”；这样既保留客户端主动 resync 的能力，又不会让后续每次广播都重复 N 份。 3. 把 `BroadcastDebugDatabase()`、asset registry 三个 lambda 和 `OnFilesLoaded()` 全部改为遍历唯一 subscriber 集合；若短期继续保留 `TArray`，至少在每次广播前先去重或使用 `Algo::SortUnique` 得到稳定快照，避免旧脏数据继续放大。 4. 断线和 `Disconnect` 清理路径要确保只需移除一次订阅，不再依赖 `RemoveSwap` 多次清同一 socket；否则 dedupe 前后会出现清理语义不一致。 5. 增加协议/集成测试：同一测试 client 连续两次发送 `RequestDebugDatabase`，随后触发一次 `BroadcastDebugDatabase()` 或一条 asset registry 增量，断言只收到一份完整更新流。 6. 如果后续落地 `Issue-42` 的 request correlation，可以把“重复请求触发一次即时 resync”做成带 `RequestId` 的 response，而持续订阅仍保持单槽位 event 流，避免再把 request 和 subscription 混在一起。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` 或新增 debug database/asset stream 测试 |
| 预估工作量 | S |
| 风险 | 如果某些现有客户端把“重复发送 `RequestDebugDatabase`”当成强制全量刷新手段，单纯去重会改变它们的恢复逻辑；因此最好保留“重复请求触发一次即时 resync”的短期兼容语义。 |
| 前置依赖 | 无；但若近期会推进 `Issue-11` 的 session state 重构，建议直接把订阅标志并入 session，避免先用 `AddUnique`，后续再二次迁移。 |
| 验证方式 | 1. 新增回归测试，单客户端连续两次发送 `RequestDebugDatabase` 后触发一次 database/asset 更新，确认只收到一份后续推送。 2. 验证重复请求仍可按设计得到一次即时 resync 或明确 no-op，不影响客户端恢复流程。 3. 在大项目上观察出站队列和 database 更新量，确认不会因重复订阅而线性放大。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-50 | Defect | 先修 `RequestDebugDatabase` 订阅去重，低成本收敛一条稳定放大量流的协议缺陷 |
| P2 | Issue-47 | Defect | 与既有 `Issue-13` 同源，执行时并入 Windows data breakpoint 寄存器编程修复 |
| P2 | Issue-48 | Defect | 与既有 `Issue-13` 同源，执行时并入 `Dr7` 位掩码校正，不单独拆工单 |
| P2 | Issue-49 | Architecture | 与既有 `Issue-15` 同源，执行时并入真实 JIT runtime harness 架构项 |

---

## 发现与方案 (2026-04-08 18:18)

### Issue-51：`Continue` 后同一源码行的断点会被长期忽略，单行循环/回跳场景无法再次命中

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:586-618, 837-845` |
| 问题 | 命中普通 line breakpoint 时，`ProcessScriptLine()` 会把 `IgnoreBreakLine` / `IgnoreBreakSection` 设成当前停下的位置（`606-608`），用于避免 `Continue` 后立刻在同一个 safe point 再次停下。但 `Continue` 分支只清 `bIsPaused`、`bBreakNextScriptLine` 和 `ConditionBreak*`，不会清或消费这组 ignore 状态（`837-845`）。随后只要下一次 line callback 仍落在同一源码行，`586-587` 就会把它视为“应忽略的旧断点”；更关键的是，ignore 状态只在 `!bWasIgnored && !bIsPaused` 时才被清空（`615-618`）。结果是当控制流持续回到同一行，例如单行 `while/for` 循环、同一行的尾递归回跳，或多条 bytecode 共用一行号时，这个断点会一直被压制，而不是只跳过一次。 |
| 根因 | 当前实现把“继续执行后跳过当前停点一次”的需求错误实现成了“只要后续 callback 还在同一行就一直忽略”，因为 ignore token 没有 one-shot 语义，且清理条件反而依赖本次没有命中 ignore。 |
| 影响 | 这是明确的 breakpoint correctness 缺陷。用户在单行循环、压缩写法或多个语句共线的脚本里按 `Continue` 后，会看到断点再也打不回来，直到执行先经过别的源码行；前端表现就是“偶发失效的断点”，而且越是热点循环越稳定复现。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `IgnoreBreakLine/Section` 改成严格的一次性 ignore token，只跳过恢复执行后的首个同位点 callback，随后立刻失效。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h/.cpp` 为 ignore 状态引入显式 one-shot 语义，例如 `bHasPendingBreakpointIgnore` + `IgnoredSection/IgnoredLine`，或等价的 `FSingleUseBreakpointIgnore` 结构；不要再让“是否忽略过”隐含在 `Line == IgnoreBreakLine` 比较里。 2. `Continue` / line-breakpoint 停下分支仍可记录当前停点位置，但 `ProcessScriptLine()` 一旦消费到匹配的 ignore token，就必须立即清空该 token；即使下一次 callback 仍是同一行，也要允许重新命中断点。 3. 若当前 line granularity 仍不足以区分“同一 safe point 重入”和“同一源码行的新一次访问”，建议把 ignore key 扩成 `Section + Line + Function`，必要时再加 bytecode position / instruction pointer，避免多函数共线或同一行多停点再次串扰。 4. 把 ignore 清理逻辑从 `if (!bWasIgnored && !bIsPaused)` 拆出来，改成显式状态机：`Ignored -> Consumed -> Cleared`，不要再通过布尔组合推导生命周期。 5. 在 `AngelscriptDebuggerBreakpointTests.cpp` 新增回归夹具，构造一个断点位于单行循环或同一行回跳的脚本：首次命中后发送 `Continue`，验证同一源码行在下一轮访问时仍能再次停下，而不是永久被忽略。 6. 同时回归现有“`Continue` 后不要立刻在同一停点重入”的场景，确认 one-shot ignore 仍能避免原始的瞬时重复停点。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 ignore key 仍只按 `Section + Line` 比较，修完 one-shot 后可能暴露“同一行多个独立 safe point”会更频繁停下的问题；因此最好同步评估是否需要纳入函数或 bytecode 位置维度。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增单行循环/回跳断点回归，验证 `Continue` 后同一行可以再次命中。 2. 回归现有 breakpoint/stepping 测试，确认不会退回到“恢复执行后立刻原地重停”的旧问题。 3. 人工在包含多条语句共线的脚本上验证，同一行的断点命中次数与实际执行次数一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-51 | Defect | 优先修复，直接恢复 `Continue` 后单行循环/回跳场景的断点正确性 |

---

## 发现与方案 (2026-04-08 18:18)

### Issue-52：data breakpoint 命中被错误编码成 `exception` 停止原因，协议语义与 DAP 适配都会被带偏

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 行号 | `AngelscriptDebugServer.cpp:533-538, 623-626`；`AngelscriptDebugServer.h:129-139` |
| 问题 | data breakpoint flush 路径在拼装 `FStoppedMessage` 时，无论是普通命中还是 hit-count/out-of-scope 触发后的停下，都会把 `Msg.Reason` 固定写成 `exception`（`533-538`）。同一个文件里，普通 line breakpoint / step 停下则明确使用 `breakpoint` 或 `step`（`623-626`）。而协议结构 `FStoppedMessage` 只有 `Reason/Description/Text` 三个字符串字段（`129-139`），没有其它 stop-kind 兜底字段；也就是说，一旦 data breakpoint 触发，线上唯一可判别的 stop 类型就被服务端定性成了 `exception`。 |
| 根因 | data breakpoint 命中路径复用了“拼一段说明文本然后暂停”的异常模板，但没有把 watchpoint 这种 breakpoint stop source 视为独立协议语义，导致 `Reason` 被直接写错。 |
| 影响 | 这会把客户端和 DAP 适配层带到错误分支：watchpoint 停下会被当成脚本异常，前端可能展示错误图标/文案、走异常专属 UI/统计逻辑，后续若再接入 `data breakpoint` / `hitBreakpointIds` 等 DAP 语义，也无法从当前 payload 正确映射。它还会与已有的 `BreakOptions`、`BreakpointIds`、session 事件流修复形成新的语义冲突。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 data breakpoint 引入明确的 stopped reason，不再借用 `exception`；同时让所有 stop source 通过统一映射层产出协议原因。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 抽一个小 helper，例如 `BuildStoppedMessage(EStopSource Source, const FString& Text, ...)`，集中把 `line breakpoint`、`step`、`pause`、`exception`、`data breakpoint` 映射到稳定的 `Reason` 字符串，避免各分支手写字面量。 2. 将 `ProcessScriptLine()` 中 data breakpoint flush 的 `Msg.Reason = TEXT(\"exception\")` 改成独立值；若当前 V2 客户端已经按 DAP 语义适配，优先使用 `data breakpoint`，否则至少使用 `breakpoint` 并把更细分类别写入 `Description`，不要继续伪装成异常。 3. 审计 `ProcessException()`、普通 line breakpoint、`PauseExecution(nullptr)` 和 JIT `DebugBreak` 等其它停点入口，统一通过同一 helper 产出原因，形成单一 stop-source contract。 4. 将这条修复与既有 `Issue-46` 的 `BreakpointIds` 扩展一起设计：data breakpoint stopped payload 除了正确的 `Reason` 外，还应能带回触发的 data breakpoint id，避免只修 reason 不修实体标识。 5. 在 `AngelscriptDebugProtocolTests.cpp` 或 `AngelscriptTest/Debugger` 增加回归，至少验证 data breakpoint 命中后返回的 `Reason` 不再是 `exception`；若短期没有现成 data breakpoint 集成夹具，可先给 stopped-message builder 做单测，再补端到端。 6. 如果需要兼容旧客户端，通过 `DebugServerVersion` / adapter version 做门控：旧 client 可继续收到 `breakpoint`，新 client 再升级到 `data breakpoint`，但两者都不应再收到 `exception`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下 data breakpoint 集成测试 |
| 预估工作量 | S |
| 风险 | `Reason` 是协议兼容面；如果直接改字符串而客户端硬编码旧值，可能出现 UI 回归。因此最好通过版本协商或兼容映射逐步切换。 |
| 前置依赖 | 建议与 `Issue-46` 的 stopped payload 扩展协同实施，但不是硬依赖 |
| 验证方式 | 1. 新增回归测试，命中 data breakpoint 时断言 `Reason` 不再等于 `exception`。 2. 回归普通异常与 line breakpoint 场景，确认它们仍分别返回 `exception` / `breakpoint`。 3. 在 DAP 适配层或测试 client 侧人工验证 watchpoint 停下的 UI/日志分支已切到 breakpoint/data-breakpoint 语义。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-52 | Defect | 在 stopped payload 协议修复阶段一并处理，先纠正 watchpoint 的 stop reason |

---

## 发现与方案 (2026-04-08 18:18)

### Issue-53：`Pow` 的 JIT 路径跳过 `TXT_POW_OVERFLOW` 异常契约，至少 `asBC_POWf` 已经与解释执行静默分叉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:6455-6495`；`as_context.cpp:3963-4013`；`StaticJITHeader.h:65-72`；`StaticJITHeader.cpp:108-119` |
| 问题 | 解释器执行 `asBC_POWf` / `asBC_POWd` / `asBC_POWdi` 时，会在 `FMath::Pow(...)` 后显式检查结果是否等于 `HUGE_VAL`，命中后调用 `SetInternalException(TXT_POW_OVERFLOW)` 并中止当前脚本指令（`3963-4013`）。相比之下，StaticJIT 的三个 handler 都带着同一句注释 `We skip checking for HUGE_VAL, no point`，直接把 `Math::Pow(...)` 写进生成代码（`6459-6495`）。这意味着 `asBC_POWf` 在当前版本里已经是稳定的语义分叉：JIT 会继续返回溢出结果，而解释器会抛 `Overflow in exponent operation`。同时，`StaticJITHeader` 现有 `EJITException::Overflow` 只映射到 `TXT_DIVIDE_OVERFLOW`，并没有 `PowOverflow` 对应项（`65-72`, `108-119`），说明当前 JIT 异常模型本身也没法表达这条解释器契约。 |
| 根因 | `Pow` 的 JIT handler 只复用了数值计算本体，没有把 AngelScript 对 exponent overflow 的异常语义一起迁移；而共享的 JIT exception 枚举又把 `Overflow` 狭义绑定成了除法溢出，缺少可复用的 `TXT_POW_OVERFLOW` 通路。 |
| 影响 | 这是实打实的 JIT/VM 结果不一致，不是性能或日志差异。脚本一旦依赖 `Pow` overflow 触发异常、进入 `catch`、或者让调试器在异常处停下，JIT 路径就会静默改写控制流和最终结果。更糟的是，这条问题今天已经影响 `asBC_POWf`，而 `Issue-12` 修完 `POWd/POWdi` 可生成之后，double 版本也会立刻落入同样的静默分叉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `Pow` 系列 bytecode 建立统一的 overflow-emission helper，同时扩展 JIT exception 模型以表达 `TXT_POW_OVERFLOW`，保证 JIT 与解释器在正常值和异常值上都一致。 |
| 具体步骤 | 1. 在 `AngelscriptBytecodes.cpp` 提取 `EmitPowWithOverflowCheck(...)` 一类 helper，供 `asBC_POWf`、`asBC_POWd`、`asBC_POWdi` 共用；helper 先计算 `Math::Pow(...)`，再按解释器契约检查 `== HUGE_VAL`，命中时走统一异常分支，而不是直接把结果留在寄存器/栈槽里继续执行。 2. 在 `StaticJITHeader.h/.cpp` 为 JIT 异常模型新增 `PowOverflow`，或提供能直接发 `TXT_POW_OVERFLOW` 的专用入口；不要复用当前映射到 `TXT_DIVIDE_OVERFLOW` 的 `EJITException::Overflow`，否则错误文本会继续错。 3. `asBC_POWf` 先独立修复，因为它当前已经 `return true` 并在运行态静默分叉；`asBC_POWd` / `asBC_POWdi` 则与既有 `Issue-12` 合并实现，在改正 `return false` 与类型断言后立刻复用同一 overflow helper。 4. 生成代码时要对齐解释器的时序：先把结果写入目标槽，再在同一条 bytecode 的异常检查里中止；不要把 `PowOverflow` 变成“结果已提交但脚本仍继续跑”或“抛异常前未更新目标槽”的第三种语义。 5. 在测试层增加最小脚本回归，至少覆盖 `float ** float` 的 overflow 场景，验证解释执行与 JIT 都得到 `TXT_POW_OVERFLOW`；随后在 `Issue-12` 修复后补上 `double ** double` 与 `double ** int` 的同类测试。 6. 把 `Pow` 纳入现有 JIT/VM consistency matrix：同一脚本分别在解释器与 JIT 下执行，比较返回值、异常文本和调试停点结果，避免未来再出现“数值算对了但异常语义漏掉”的半实现状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增或扩展 StaticJIT `Pow` 一致性测试 |
| 预估工作量 | M |
| 风险 | 若 overflow 检查条件与解释器不完全一致，可能把“原本合法的大结果”误判成异常，或继续漏报真实 overflow；因此必须用解释器做逐案对照，而不是自创新规则。 |
| 前置依赖 | `asBC_POWd` / `asBC_POWdi` 的完整运行态验证依赖既有 `Issue-12` 先修复 codegen 返回值与操作数契约；`asBC_POWf` 可先独立落地 |
| 验证方式 | 1. 新增 `float Pow` overflow 回归，确认解释器与 JIT 都抛 `TXT_POW_OVERFLOW`。 2. 在 `Issue-12` 修复后补 `double Pow` / `double Pow(int)` overflow 用例，确认不再出现“VM 抛异常、JIT 返回值”的分叉。 3. 回归普通非 overflow `Pow` 场景，确认新增检查不改变正常数值结果。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-53 | Defect | 先修 `asBC_POWf` 的运行态语义分叉，再与 `Issue-12` 合并补齐 double 版本 |

---

## 发现与方案 (2026-04-08 18:35)

### Issue-54：JIT caller 走 `WriteDynamicCall()` 时会吞掉 VM callee 的脚本异常，外层 `Execution.bExceptionThrown` 永远不会置位

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:1478-1480, 1503-1569`；`AngelscriptStaticJIT.cpp:1430-1498, 3340-3376`；`StaticJITHeader.cpp:104-161`；`AngelscriptEngine.cpp:4912-4922`；`ASClass.cpp:1584-1595`；`as_context.cpp:1006-1007` |
| 问题 | `WriteDynamicCall()` 为 JIT miss / fallback callee 构造独立的 `FAngelscriptContext CallContext`，执行后只检查 `CallContext->m_status != asEXECUTION_FINISHED`，然后直接走 `ExceptionCleanupAndReturn(..., false)` 返回（`1503-1569`）。但 `ExceptionCleanupAndReturn(..., false)` 生成的 cleanup label 只做对象析构和 `ReturnEmptyValue()`，不会补 `SCRIPT_*_EXCEPTION()`，也不会设置 `Execution.bExceptionThrown`（`1430-1498`, `3340-3376`）。与之相对，StaticJIT 里唯一会把异常桥接回 JIT 执行状态的通路，是 `FStaticJITFunction::SetException*()` / `FAngelscriptEngine::Throw()`，两者都会显式设置 `Execution.bExceptionThrown`（`104-161`, `4912-4922`）。`ASClass.cpp` 的 VM fallback wrapper 至少还会在 `Context->Execute()` 之后检查 `m_status == asEXECUTION_EXCEPTION`（`1584-1595`），而 `WriteDynamicCall()` 既不把 `CallContext` 的异常文本转发进 `Throw()`，也不设置当前 `Execution` 的异常位。 |
| 根因 | StaticJIT 把“JIT caller 回退到独立 `asCContext` 执行 callee”实现成了纯局部的 VM 调用，没有建立“嵌套 VM 异常 -> 当前 `FScriptExecution` 异常状态”的桥接层；cleanup path 又默认“没有显式 JIT exception 就只是提前 return”。 |
| 影响 | 这会制造确定性的 JIT/解释执行分叉：1. callee 在 VM fallback 中抛出的脚本异常不会向外层 JIT caller 传播，上一层 direct caller 的 `if (Execution.bExceptionThrown)` 护栏永远看不到它；2. `catch`、异常停点和上层控制流会被静默改写；3. 当前函数直接 `ReturnEmptyValue()` 后，外层调用者可能继续读取默认值或上一次残留寄存器值，而不是得到规范异常。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 `WriteDynamicCall()` 的 VM fallback 路径上显式桥接嵌套 `CallContext` 的异常状态，把它提升成当前 `FScriptExecution` 的正式脚本异常，而不是只做清理后静默返回。 |
| 具体步骤 | 1. 在 runtime 层新增一个统一 helper，例如 `PropagateNestedContextExceptionToExecution(FScriptExecution& Execution, asCContext* NestedContext)` 或 `FAngelscriptEngine::ThrowNestedContextException(...)`，职责固定为：读取 `NestedContext->GetExceptionString()` / 位置元数据，把异常文本转成当前 `Execution` 的正式异常，并设置 `Execution.bExceptionThrown = true`。 2. 修改 `WriteDynamicCall()` 生成代码：`CallContext->Execute()` 返回后，若 `CallContext->m_status == asEXECUTION_EXCEPTION`，先调用上述 helper，再进入 `ExceptionCleanupAndReturn(..., false)`；若是 `ABORTED/SUSPENDED/ERROR` 等非 finished 状态，也要明确映射成稳定异常，而不是继续一律 silent return。 3. 保证桥接 helper 最终复用 `FAngelscriptEngine::Throw()` 或与其等价的单一入口，避免再次出现一条“JIT 自身异常会置位、嵌套 VM 异常不会置位”的分裂通路。 4. 同步审计 `ASClass.cpp` 的 VM fallback wrapper，决定是否也统一改为复用同一 helper；这样 `UASFunction` fallback 与 StaticJIT fallback 的异常传播语义可以彻底收敛。 5. 回归 direct-call parent path，确认上层 `if (Execution.bExceptionThrown)` 现在能正确短路，不再把 VM callee 异常当成成功返回继续执行。 6. 新增最小回归：构造“caller 已 JIT、callee 被强制走 `WriteDynamicCall()` 且内部抛异常”的脚本，分别验证异常文本、`catch` 行为和 DebugServer stop reason 都与纯解释执行一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp` 或 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增 JIT fallback exception 一致性测试 |
| 预估工作量 | M |
| 风险 | 如果只补 `bExceptionThrown` 而不同步转发异常文本/位置信息，行为会从“静默吞掉”变成“抛了一个缺少上下文的异常”；同时要避免对 `ABORTED` / `SUSPENDED` 等状态误映射成错误异常类型。 |
| 前置依赖 | 建议与已记录的 JIT exception 上报问题协同设计，但不是硬依赖；本条至少要先把异常传播正确性修回来。 |
| 验证方式 | 1. 新增“JIT caller -> VM callee throws”回归，断言 `Execution.bExceptionThrown` 会向上传播，且外层调用不会继续执行后续语句。 2. 对比解释器态与 JIT fallback 态同一脚本，确认异常文本、`catch` 命中和返回值一致。 3. 若 DebugServer 在场，验证异常仍会按修复后的统一链路产生 `HasStopped(reason="exception")`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-54 | Defect | 立即修复，先恢复 JIT fallback 对脚本异常的正确传播 |

---

## 发现与方案 (2026-04-08 18:36)

### Issue-55：`HandleMessage()` 把协议解析、会话鉴权、paused-state 校验和业务副作用混写在单个 if/else 链里，已成为 DebugServer V2 协议漂移的结构性根因

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.h:25-80`；`AngelscriptDebugServer.cpp:820-1195` |
| 问题 | 当前 DebugServer V2 的消息处理没有独立的 dispatch/metadata 层，`HandleMessage()` 把所有消息都塞进一条 370 多行的 if/else 链：每个分支各自决定怎样 `*Datagram << Msg`、是否需要 debugging session、是否要求 paused state、以及要不要立即改全局状态。`RequestDebugDatabase`、`Pause/Continue/Step*`、`Start/StopDebugging`、`Set/ClearBreakpoint`、`Set/ClearDataBreakpoints`、`RequestVariables/Evaluate`、`BreakOptions`、`Disconnect` 都在同一函数里直接操作成员变量，但代码里没有统一的“消息元数据”去声明 `requires_debug_session`、`requires_paused_state`、`strict_body_parse`、`response/event routing` 或 `min adapter version`。同时，这条链也没有默认 reject 分支，未知 `MessageType` 会被静默吃掉而不是记录协议错误。 |
| 根因 | 协议层从一开始按“小而快的分支判断”堆起来，没有抽象出消息级 contract；随着 V2 能力增多，解析、鉴权、会话状态和路由规则只能在每个分支里各自手写，形成高耦合单点。 |
| 影响 | 这不是单个分支的代码风格问题，而是协议正确性的放大器：1. 新增或修复一条消息时，必须人工记住同时补齐严格反序列化、session gate、paused-state gate、response/event 路由和版本门控，否则同类 bug 会在另一分支重现；2. 由于没有统一 default reject，未知/过期消息会静默 no-op，客户端很难区分“消息不支持”和“服务器成功但无结果”；3. DAP 对 request/response/event、thread-scoped stop、能力协商等要求很难分阶段接入，因为当前结构没有可挂元数据的位置。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `HandleMessage()` 重构成表驱动 dispatch：每种消息先过统一 metadata 校验，再进入各自的业务 handler；解析、鉴权和状态门槛不再分散在 if/else 分支里。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h/.cpp` 定义统一的消息描述结构，例如 `FDebugMessageSpec { EDebugMessageType Type; bool bRequiresDebugSession; bool bRequiresPausedState; bool bStrictParse; int32 MinAdapterVersion; EMessageRoute Route; HandlerFn Handler; }`，并用静态表或 `TMap` 为每个 `EDebugMessageType` 注册一份 spec。 2. 将 `HandleMessage()` 精简成固定流程：查 spec -> 校验消息类型是否已注册 -> 走统一 `TryReadStrictMessage(...)`/专用 parser -> 校验 session/paused/version gate -> 调用业务 handler；未知消息类型必须明确记录 warning 或断开连接，不再 silent no-op。 3. 把当前散落在各分支里的共性规则收口到 spec 层：`Pause/Continue/Step*` 标记为 `bRequiresDebugSession=true`；`Step*`、`Continue`、`RequestVariables/Evaluate/CallStack` 进一步标记 `bRequiresPausedState=true`；database push/握手消息走独立 route。 4. 为 response/event 建立统一路由枚举，例如 `ToRequestingClient`、`ToDebuggingSessions`、`ToDatabaseSubscribers`，替换当前“有时 `SendMessageToClient`，有时 `SendMessageToAll`”的手工选择，避免再出现旁路 client 收到 stop event 或 response 对错对象发送。 5. 通过静态断言或单测把 enum 与 dispatch table 绑死，例如 `for each EDebugMessageType must have exactly one spec`，避免以后新增枚举值却忘记注册 handler。 6. 迁移顺序建议先覆盖高风险消息：`Start/StopDebugging`、`Pause/Continue/Step*`、`Set/ClearBreakpoint`、`Set/ClearDataBreakpoints`、`RequestCallStack`、`RequestVariables/Evaluate`；等这批走通后再迁移 database/asset/editor 辅助消息。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 预估工作量 | L |
| 风险 | 这是协议中枢重构；如果在迁移阶段同时改 wire format，很容易把“dispatch 架构调整”和“协议字段变更”耦在一起，放大回归面。应优先保持 wire format 不变，只先收敛入口规则。 |
| 前置依赖 | 无；但若后续推进 request correlation 或 session state 重构，最好直接把 metadata 层设计成可承载这些扩展。 |
| 验证方式 | 1. 新增 dispatch 层单测，遍历每个已知 `EDebugMessageType`，断言都能命中唯一 spec。 2. 补负向协议测试：未知 message type、无 session 发送控制消息、running 态发送 `Step*`，都应得到明确拒绝而非 silent no-op。 3. 回归现有 debugger/database 测试，确认迁移前后 wire format 与单 client 正常流程不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-55 | Architecture | 在 P0 缺陷止血后尽快推进，先收敛协议入口再继续叠加 V2 语义 |

---

## 发现与方案 (2026-04-08 18:37)

### Issue-56：`WriteDirectCall()`、`WriteDynamicCall()` 和 `ASClass` wrapper 各自维护一套 ABI 封送规则，JIT 调用修复已经演变成多点同步工程

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:1202-1483, 1485-1589`；`ASClass.cpp:610-660, 1584-1693` |
| 问题 | 当前脚本调用 ABI 规则在至少三处被手写复制。`WriteDirectCall()` 自己根据 `DataType.IsReferenceType()/IsObject()/GetSizeInMemoryBytes()` 拼 `ArgumentString/ArgumentTypeString/ReturnTypeString`，并用多段 `switch` 选择 `byte/dword/qword/float/double` 寄存器（`1202-1483`）。`WriteDynamicCall()` 又用另一套条件分支把同一批参数映射成 `SetArgAddress/SetArgByte/Word/DWord/QWord`，然后再单独把返回值搬回 `l_valueRegister` / `l_objectRegister`（`1485-1589`）。与此同时，`ASClass.cpp` 的 UObject wrapper 在 `Context->Execute()` 后还维护了第三套返回值复制逻辑，按 `ParmBehavior` 分别走 `GetReturnObject/GetReturnAddress/GetReturnByte/...`（`610-660`），不同签名 wrapper 里又继续散落多份专门实现（`1584-1693` 只是其中一段）。这意味着“参数分类”“返回值寄存器选择”“异常后的返回值可用性”并没有一个共享的 ABI 真源。 |
| 根因 | 调用代码生成历史上按“direct call 能跑就行”“dynamic fallback 单独补”“UObject wrapper 再单独特判”逐步堆叠，没有抽出统一的 call ABI 描述层；每条路径都直接编码了对象/引用/POD/浮点/return-on-stack 的分支树。 |
| 影响 | 这已经不是简单的代码重复，而是回归放大器：1. 每次修 direct/raw JIT 与 VM fallback 语义差异时，都必须人工同步多套封送代码；2. 新增一种返回类型、调整寄存器约定、修 exception/fallback 语义时，很容易只改到其中一处；3. 现有 JIT 缺陷之所以修起来高风险，就是因为调用目标解析和 ABI 封送被拆散在多个实现里，测试也只能靠矩阵式补洞。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 抽出共享的 `ScriptCall ABI` 描述层，把“参数/返回值如何在 VM 栈、JIT 寄存器和 `asCContext` API 间映射”变成数据驱动计划，再分别由 direct、dynamic 和 wrapper emitter 消费。 |
| 具体步骤 | 1. 在 `StaticJIT` 或更下层 runtime 中定义统一的 ABI 规划结构，例如 `FScriptCallABIPlan`，至少包含：对象参数类别、每个参数的 `EABIValueKind`（Address / Byte / Word / DWord / QWord / Float / Double / ReturnOnStack）、返回值来源寄存器、以及异常后返回值是否可读。 2. 把 `WriteDirectCall()` 里当前的 `DataType -> LiteralType/VMType/RegisterState` 决策下沉成“先构建 plan，再遍历 plan 生成 direct/raw JIT call”；`WriteDynamicCall()` 则复用同一个 plan 生成 `SetArg*` 和返回值回填代码，不再各自维护一套 size/object/reference 分支树。 3. `ASClass.cpp` 的 wrapper 返回值复制同样改成消费同一份 plan 或其运行时等价物，而不是继续按 `ParmBehavior` 手工列出另一套 `GetReturn*` 映射；这样 VM wrapper 与 StaticJIT fallback 才能保证返回值 ABI 一致。 4. 先把 plan 范围收敛到当前最常见的几类：对象/引用、1/2/4/8-byte POD、float/double、return-on-stack；其余特殊类型暂时通过 `Unsupported` 标识显式拒绝，避免半迁移时又默默回到手写分支。 5. 在测试层建立 ABI matrix，至少覆盖：`void`、`float`、`double`、`byte/word/dword/qword`、object handle、reference、return-on-stack value type，再分别跑 direct/raw JIT、dynamic fallback 和 `ASClass` wrapper，确保三条路径返回值与异常语义一致。 6. 迁移顺序建议先把 `WriteDirectCall()` 与 `WriteDynamicCall()` 收口，因为它们当前在同一文件且最容易一起验证；待 JIT 路径稳定后，再把 `ASClass.cpp` wrapper 挂到同一 plan。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h/.cpp` 或新建 ABI helper 文件、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增 call ABI consistency 测试 |
| 预估工作量 | L |
| 风险 | 这是执行路径核心重构；如果一开始就把所有罕见类型都并入 plan，容易把 refactor 变成大爆炸。应先用最小 ABI 子集收口主路径，再逐步吞并边缘情况。 |
| 前置依赖 | 建议先修复本轮 `Issue-54` 的异常传播缺陷，再以该回归为护栏推进 ABI plan 重构。 |
| 验证方式 | 1. 新增 ABI consistency matrix，对比 direct/raw JIT、dynamic fallback、`ASClass` wrapper 的参数与返回值行为。 2. 针对本轮异常传播场景补一条“callee throws”用例，确认 refactor 后三条路径都不会再分叉。 3. 回归现有 precompiled/JIT/native call 测试，确认性能优化不改变 ABI 结果。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-56 | Refactoring | 在 P0/P1 语义修复后推进，作为后续 JIT 调用链收敛的降风险工程 |

---

## 发现与方案 (2026-04-08 18:45)

### Issue-57：补足 Analysis 发现 9 的修复方案：`SendCallStack()` 完全绕过 JIT `debugCallStack`，JIT 停点返回的 `CallStack` 会缺失当前脚本帧

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:1382-1482`；`as_context.h:262-285`；`AngelscriptEngine.cpp:5087-5126`；`StaticJITHeader.h:194-217`；`AngelscriptStaticJIT.cpp:337-344, 374-379` |
| 问题 | `FAngelscriptDebugServer::SendCallStack()` 只读取 `asGetActiveContext()` 并遍历解释器 callstack；当 `Context == nullptr` 时，它会直接发送空 `CallStack`。但 JIT 执行入口 `FScriptExecution` 在构造时会把 `tld->activeExecution` 设为当前执行，并显式把 `tld->activeContext` 置为 `nullptr`。同时，引擎自己的 `GetStackTrace()` 已经证明 JIT 栈信息真实存在于 `activeExecution->debugCallStack`：它优先遍历 `FScopeJITDebugCallstack` 链，再退回 `prevContext`。StaticJIT 生成代码也持续用 `SCRIPT_DEBUG_CALLSTACK_FRAME` / `SCRIPT_DEBUG_CALLSTACK_LINE` 写入这条链。结果是 DebugServer V2 的 `CallStack` 路径与运行时已有的 JIT 调试栈完全脱节，只要停点落在 JIT 覆盖函数里，协议返回就会缺失当前脚本顶帧，严重时直接变成空栈。 |
| 根因 | `stackTrace` 序列化仍然按“解释器上下文就是唯一调试真源”的旧模型实现，没有把 `activeExecution` / `debugCallStack` 纳入统一 debug target 抽象。 |
| 影响 | JIT 场景下前端看到的调用栈会与真实执行位置脱节，后续 frame 选择、变量查看和异常定位都会基于错误栈顶继续工作。这也是明显的 DAP 合规性缺口：服务端已经有 JIT 栈元数据，却没有把它映射到协议 `CallStack` 响应。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `CallStack` 的数据源从“仅解释器 `asCContext`”升级为“统一脚本执行目标”，优先序列化 `activeExecution->debugCallStack`，再补上 `prevContext` / Blueprint / C++ 帧。 |
| 具体步骤 | 1. 在 `Debugging/AngelscriptDebugServer.cpp` 提取统一 helper，例如 `BuildDebuggerCallStack(FAngelscriptCallStack& OutStack)`，先读取 `asCThreadManager::GetLocalData()`；若 `activeExecution != nullptr`，先遍历 `FScriptExecution::debugCallStack` 生成 JIT 帧，再按 `prevContext` 继续补解释器和 Blueprint/C++ 帧。 2. 不要让 `SendCallStack()` 直接依赖 `asGetActiveContext()`；它应该与 `FAngelscriptEngine::GetStackTrace()` 共用同一套“JIT first, VM second”的栈源选择逻辑，避免今后两处再分叉。 3. 为 JIT 帧补齐协议字段：`Frame.Name` 使用 `FunctionName`，`Frame.Source` 使用 `Filename`，`Frame.LineNumber` 使用 `LineNumber`，`ModuleName` 至少提供稳定占位值或从 owning script function/filename 映射回模块名，不能继续让 JIT 帧整段缺席。 4. 如果当前 paused 状态可能同时存在 `activeExecution` 与 `prevContext`，要明确序列化顺序并与现有前端预期保持一致，推荐把 JIT 顶帧放在最前，再拼接外围 VM / Blueprint / C++ 帧。 5. 与已有 `Issue-24`、`Issue-43` 联动设计：`CallStack` 一旦开始暴露 JIT 帧，后续 `Evaluate` / `Variables` 也必须能识别这些 frame id，避免“栈能看见但 frame 不可求值”的半修状态。 6. 增加集成测试，构造一个已 JIT 的脚本函数触发停点或异常，断言 `CallStack` 顶帧来自该 JIT 函数，并且 `Source/LineNumber` 与 `SCRIPT_DEBUG_CALLSTACK_LINE` 提供的位置一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` 或 `Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下新增 JIT callstack 集成测试 |
| 预估工作量 | M |
| 风险 | 一旦把 JIT 帧暴露给协议，现有 frame 编号、变量求值和 step 逻辑里的“解释器帧假设”会立即暴露；如果不同步定义 frame id 语义，前端可能看到栈顶正确了，但点开变量仍落到旧 VM 帧。 |
| 前置依赖 | 建议与 `Issue-24`、`Issue-43` 协同设计，但本条可以先独立修复 `CallStack` 序列化缺口。 |
| 验证方式 | 1. 新增“JIT 停点返回 `CallStack`”回归，断言顶帧函数名、文件和行号正确。 2. 对比同一脚本在解释执行与 JIT 执行下的 `CallStack`，确认只是后端不同，不再出现 JIT 空栈或缺顶帧。 3. 回归现有 debugger smoke/protocol tests，确保纯解释器态帧顺序和 wire format 不被破坏。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-57 | Architecture | 在 JIT 调试链修复中优先推进，先让协议 `CallStack` 能看到真实 JIT 顶帧 |

---

## 发现与方案 (2026-04-08 18:48)

### Issue-58：补足 Analysis 发现 55/56 的修复方案：`Variables` / `Evaluate` 会自动执行用户 getter，且 getter 失败后仍会伪装成成功取值

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:1081-1128, 2589-2607, 2692-2842`；`AngelscriptType.cpp:639-745`；`Bind_UStruct.cpp:480-507, 513-540, 606-617` |
| 问题 | `RequestVariables` 和 `RequestEvaluate` 会分别走 `GetDebuggerScope()` / `GetDebuggerValue()`。这两条路径并不只是读取当前内存：`Bind_UStruct.cpp` 会在变量展开时主动枚举零参数、`readonly` 且名称以 `Get` 开头的方法，并立即调用 `GetDebuggerValueFromFunction()` 把结果塞进 scope；全局变量解析也会把缺失成员回退成 `Get<Name>` accessor。更严重的是，`GetDebuggerValueFromFunction()` 在 `Context->Execute()` 之后从不检查执行结果或异常状态，直接读取 return slot 并生成 `FDebuggerValue`。因此只要 getter 有副作用、抛异常、`abort` 或返回未初始化引用，调试器都会在“仅仅展开变量/悬停求值”的过程中执行用户代码，并把失败路径伪装成一个看似成功的 `Variables` / `Evaluate` 响应。 |
| 根因 | 调试求值链把 getter 执行视为“安全的被动读取”，采用黑名单式豁免而不是显式纯函数契约；同时协议层没有把执行失败建模成一等结果，导致 `Execute()` 的返回状态在中途被直接丢弃。 |
| 影响 | 这会同时破坏正确性和可调试性：1. 单纯展开 locals/watch 面板就可能改变游戏或脚本状态；2. getter 抛异常后客户端拿到的不是错误，而是零值/默认构造值，用户会误以为这是脚本真实结果；3. 一旦 getter 内部再次触发脚本调用或断点，还可能把 DebugServer 带入重入式状态，进一步放大协议错乱。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“被动内存读取”和“主动执行用户代码”彻底分层：默认 `Variables` / 隐式求值不再自动跑 getter；任何允许执行 getter 的路径都必须显式检查 `Execute()` 结果，并把失败作为正式协议结果返回。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.cpp` 为调试求值增加显式模式，例如 `EDebuggerEvaluationPolicy { MemoryOnly, AllowSafeGetterExecution }`；`RequestVariables`、locals 展开、hover 这类被动查看默认只走 `MemoryOnly`，不得再自动调用 `Get<Name>` accessor。 2. 把 `Bind_UStruct.cpp` 当前枚举 `readonly Get*` 方法并自动加入 scope 的逻辑改成受策略或显式 metadata 控制，例如仅当 getter 带 `DebuggerSafe`/等价白名单标记时才允许显示为派生字段；否则只暴露真实 property 和显式可监视地址。 3. 重写 `GetDebuggerValueFromFunction()` 的执行契约：保存 `const int ExecuteResult = Context->Execute();`，只有在 `ExecuteResult == asEXECUTION_FINISHED` 且上下文没有异常时，才允许读取 return value；`asEXECUTION_EXCEPTION`、`ABORTED`、`SUSPENDED` 等都必须立刻返回失败，而不是继续消费 return slot。 4. 为协议补一个可观察的失败通路。最小修复可以先让 `Evaluate` / `Variables` 在失败时返回带错误文本的占位值；更稳妥的做法是 version-gate 一个显式 `bSuccess/ErrorText` 字段，避免客户端把 `<evaluation failed>` 当成真实值。 5. 黑名单策略改为“默认禁止，按白名单放行”：当前 `DebuggerBlacklistAutomaticFunctionEvaluation*` 只能阻止已知坏函数，不能证明其余 getter 无副作用。建议增加 `DebuggerWhitelistAutomaticFunctionEvaluation` 或 metadata 驱动的 allowlist，并把现有黑名单保留为兼容层。 6. 增加回归测试：a) 一个 getter 每次调用都会累加计数，验证 `RequestVariables` / scope 展开不会改变计数；b) 一个 getter 主动抛异常，验证 `Evaluate` 返回失败而不是默认值；c) 一个显式标记为安全的 getter 在 allowlist 下仍可被求值，证明新策略不会把所有派生字段都打死。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下新增 getter evaluation 回归测试 |
| 预估工作量 | M |
| 风险 | 一旦禁止自动 getter 执行，前端能看到的字段数量会变少；如果没有同时设计 allowlist 或失败提示，用户会感觉“有些变量突然不见了”。因此要把协议提示和文档说明一起补上，避免把安全修复做成静默功能退化。 |
| 前置依赖 | 如果采用新的 `Evaluate` 错误载荷，建议与已记录的 `Issue-42` request correlation 一起规划，避免将来再次改协议。 |
| 验证方式 | 1. 新增“getter 有副作用”回归，确认展开变量不会再执行它。 2. 新增“getter 抛异常”回归，确认客户端拿到明确失败信号而不是默认值。 3. 回归现有 `GetDebuggerValue` 相关单测，确认纯内存型 debugger value 不受影响。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-58 | Defect | 在协议/JIT 栈修复之后优先处理，先阻止调试器读变量时继续改写或伪造运行时状态 |

---

## 发现与方案 (2026-04-08 18:49)

### Issue-59：Windows data breakpoint 只对单个 current thread 编程，跨线程写入会被静默漏掉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:206-218, 221-257, 1061-1067, 1275-1285` |
| 问题 | `SetDataBreakpoints` 收到客户端请求后会直接进入 `UpdateDataBreakpoints()`。该函数没有维护“所有被调试线程”的集合，而是每次都调用 `GetThreadAgnosticCurrentThreadHandle()` 复制一个 `GetCurrentThread()` 的真实句柄，然后把这一个 `HANDLE` 交给 `FUpdateDebugRegisterThread`，最终只对这一个线程执行 `SuspendThread/GetThreadContext/SetThreadContext`。源码中不存在任何枚举其它线程、为新线程 replay watchpoint、或在会话期间同步多个线程 debug register 的路径。这意味着当前 data breakpoint 的硬件编程范围被硬编码成“调用 `UpdateDataBreakpoints()` 的那个线程”，而不是整个 debug target。 |
| 根因 | watchpoint 生命周期被设计成“单线程 current-thread 操作”，没有单独的线程注册表或 thread-scoped debug target 模型；协议层也没有暴露 thread identity 来约束或声明这一限制。 |
| 影响 | 这会把 data breakpoint 的正确性直接限制在单线程场景：1. 被监视地址如果在其它脚本线程、异步任务或 native worker thread 上被写入，DebugServer 不会收到任何命中；2. 一旦未来某条更新路径在不同线程上调用 `UpdateDataBreakpoints()`，watchpoint 还可能悄悄迁移到新线程，导致旧线程上的监视点失效；3. 对 DAP/IDE 而言，当前协议表面上提供的是“进程内 watchpoint”，实际却是“单线程硬件断点”，这个能力边界既没有协商，也没有错误反馈。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把 data breakpoint 的线程归属建模成显式概念，再决定是“真正支持多线程 watchpoint”还是“公开声明只支持指定线程”；无论哪种，都不能继续让 current-thread 假设隐式存在。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer` 或 `FAngelscriptEngine` 中引入 thread registry，例如 `TMap<uint32, FTrackedDebugThread>`，记录所有可能执行脚本或需要接受 watchpoint 的线程句柄/线程 id；不要再在 `UpdateDataBreakpoints()` 里临时复制一次 `GetCurrentThread()` 就结束。 2. 把 `UpdateDataBreakpoints()` 改成遍历 registry，对每个 tracked thread 都执行一次 `FUpdateDebugRegisterThread`；如果某些线程不允许挂硬件断点，要显式返回失败或把对应 breakpoint 标记为未验证，而不是静默忽略。 3. 在线程生命周期上补注册/注销钩子。最小落地点可以挂在脚本执行入口/出口或 `FScriptExecution` 建立处，把真正参与脚本执行的线程纳入 registry；若需要覆盖 native 写入线程，则必须有更高层的 target-thread 策略，而不是无限假设 game thread 足够。 4. 如果短期内无法支持多线程 watchpoint，至少要把当前行为公开成协议能力：`SetDataBreakpoints` 只绑定某个目标线程，并在 `BreakFilters/Capabilities` 或 `SetDataBreakpoints` 回执里显式返回 thread-bound 限制，避免客户端误以为它在监视整个进程。 5. 与已记录的 `Issue-29`、`Issue-31` 协同设计，统一 data breakpoint 的 thread-scoped stop state、active mirror 发布和命中上报；否则即便把寄存器写到多个线程，也仍会被全局 `bIsPaused`/共享 mirror 逻辑吞掉。 6. 增加多线程回归测试：主线程安装 data breakpoint，worker thread 写入被监视地址，验证服务器能稳定收到命中；再回归主线程写入场景，确认引入 registry 后不会把原有行为打坏。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 或脚本执行线程注册点、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下新增多线程 data breakpoint 测试 |
| 预估工作量 | L |
| 风险 | 一旦 watchpoint 真正扩展到多个线程，当前全局 paused-state、stop routing 和 `EXCEPTION_SINGLE_STEP` 处理都会立刻承压；如果不和线程态重构联动，容易出现“寄存器装上了，但第二个线程的命中仍被第一线程的 paused 状态吞掉”的半修状态。 |
| 前置依赖 | 建议与 `Issue-29`、`Issue-31` 同步推进；若只能分阶段实现，第一阶段至少先补协议层的 thread-bound 能力声明。 |
| 验证方式 | 1. 新增 worker-thread 写入命中回归，确认 data breakpoint 不再只在安装线程生效。 2. 验证重新设置/清除 breakpoint 不会把 watchpoint 从旧线程“迁移丢失”到新线程。 3. 对接实际调试器，确认客户端能获知 thread-bound 或 multi-thread watchpoint 的真实能力边界。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-59 | Architecture | 在 data breakpoint 体系继续修补前优先定清线程模型，否则后续命中与 stop 语义都会建立在错误前提上 |

---

## 发现与方案 (2026-04-08 23:42)

### Issue-60：补足 Analysis 发现 37 的修复方案：`StaticNames` / `StaticNamesByIndex` 跨 engine 生命周期残留，precompiled/JIT 二次加载会撞上全局脏状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | `AngelscriptEngine.cpp:69-70, 1197-1250, 1562-1563`；`AngelscriptEngine.h:578-583`；`AngelscriptPreprocessor.cpp:2048-2053`；`PrecompiledData.cpp:2436-2440, 2662-2663` |
| 问题 | `FAngelscriptEngine::StaticNames` 和 `StaticNamesByIndex` 被定义成进程级静态容器，preprocessor 的 `GenerateStaticName()` 会持续往两张表里 `Emplace/Add` 新条目，但引擎销毁路径 `1197-1250` 只清理 instance 成员，没有任何 `Empty()` / `Reset()`。另一方面，precompiled load 在 `PrepareToFinalizePrecompiledModules()` 里显式 `check(FAngelscriptEngine::StaticNames.Num() == 0)` 后再把缓存中的 static names 重新 append；save 路径也直接把当前全局 `StaticNames` 序列化回 cache。结果是 static-name 编码既不是 engine-local，也不是 shared-state-local，而是依赖整个进程之前跑过什么脚本会话。 |
| 根因 | static-name 索引承担了 preprocessor、precompiled data 和运行时 `FName` 绑定三方共享状态，却被建模成 process-global 静态表，没有绑定到 `FAngelscriptOwnedSharedState` 或任何 engine teardown 流程。 |
| 影响 | 同进程先经历一次脚本编译、再创建需要加载 precompiled data 的 engine 时，会直接命中 `check(Num() == 0)`；即使未触发断言，新的 static-name 索引也会继承旧会话残留，导致 precompiled/JIT 产物与当前 engine 的 name table 偶然耦合，破坏多 engine / full destroy / 再初始化场景的可重复性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 static-name 表从 process-global 状态下沉到 shared-state 生命周期，并为 load/save/teardown 建立显式 reset contract。 |
| 具体步骤 | 1. 在 `Core/AngelscriptEngine.h/.cpp` 定义统一的 static-name state 容器，例如挂到 `FAngelscriptOwnedSharedState` 的 `TArray<FName> StaticNames` 和 `TMap<FName, int32> StaticNamesByIndex`，禁止继续直接依赖 namespace/static 全局。 2. 把 `Preprocessor/AngelscriptPreprocessor.cpp:2048-2053` 的 `GenerateStaticName()` 改成经由当前 engine/shared-state 访问这两张表，确保 clone engine 共享同一份 state，但 full destroy 后不会残留到下一轮进程内 engine。 3. 在 `ReleaseOwnedSharedStateResources(...)` 或等价 teardown 路径里显式 `Reset/Empty` static-name state；如果短期无法立刻完成下沉，至少先补一个 `ResetStaticNames()`，在 engine 销毁和 precompiled load 前强制清空，去掉当前只能 `check` 的脆弱前提。 4. 修改 `PrecompiledData.cpp:2436-2440` 的导入逻辑：不要再依赖“外部保证全局表为空”，而是先初始化/清空目标 static-name state，再导入 cache 内 names，同时同步重建反向索引 `StaticNamesByIndex`。 5. 审计 save 路径 `2662-2663`，保证序列化来源是当前 shared-state 的 static-name 表，而不是进程里历史残留；必要时给 precompiled data 增加 guid/hash 校验，避免不同 engine 会话混写同一套 names。 6. 在 `AngelscriptMultiEngineTests.cpp` 新增回归：第一轮 engine 生成若干 static names 并销毁；第二轮 engine 加载 precompiled data，断言不会触发 `check(Num() == 0)`，且相同脚本得到稳定的 static-name 索引。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` |
| 预估工作量 | M |
| 风险 | static-name 表如果同时被 clone engine、preprocessor 和 precompiled import 访问，下沉生命周期时容易引入“应该共享还是应该隔离”的语义变化；若 shared-state 归属划分错误，可能让 clone engine 之间的 static-name 索引再次分叉。 |
| 前置依赖 | 无，但建议与多 engine/shared-state 生命周期相关修复一起评审，避免再引入新的 process-global 缓存。 |
| 验证方式 | 1. 新增 multi-engine 回归，覆盖 full destroy 后再次加载 precompiled data。 2. 对比修复前后同一脚本的 static-name 索引，确认不再依赖历史会话残留。 3. 回归 precompiled save/load 与普通解释编译路径，确认 static-name 解析结果保持一致。 |

### Issue-61：补足 Analysis 发现 19 的修复方案：`GScriptNativeForms` 只增不减，native form 会跨引擎生命周期泄漏并保留 stale `asIScriptFunction*`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp` |
| 行号 | `StaticJITBinds.cpp:27, 29-60, 112-116, 156-160, 199-203, 330-334, 368-372, 401-405, 439-443, 530-534, 592-596, 703-707, 777-781, 860-864, 901-905, 931-935, 961-965, 991-995, 1021-1025` |
| 问题 | `StaticJITBinds.cpp` 把 native form 注册在进程级静态 `TMap<asIScriptFunction*, FScriptFunctionNativeForm*> GScriptNativeForms` 中。`GetNativeForm()` 只做 `Find`；所有 `BindNative*` / `BindTemplateInstantiatedCall` / `BindPushArg*` / delegate bind 入口都会 `new` 一个派生 `FScriptFunctionNativeForm` 后直接 `Add` 到这张表。源码里没有任何 `delete`、`Remove`、`Reset` 或 teardown 路径，说明表值永久存活，表键也永久保留旧 `asIScriptFunction*`。 |
| 根因 | native form 注册表被实现成 process-global cache，且 value 使用裸指针，生命周期没有绑定到 `FAngelscriptEngine`、`FAngelscriptOwnedSharedState` 或某次 precompiled/JIT 生成会话。 |
| 影响 | 长时间 editor/automation 进程会稳定泄漏 `FScriptFunctionNativeForm` 对象；更危险的是，旧 engine 销毁后 map key 变成 stale `asIScriptFunction*`，如果后续分配复用同一地址，JIT 可能把过期 native form 绑定到新的脚本函数，造成错误 codegen 或错误 custom/native call，且表现为静默错绑而不是显式失败。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 native form registry 改成有明确所有权和 teardown 的 engine-scoped/shared-state-scoped资源，禁止继续让裸指针长期悬挂在进程级静态表里。 |
| 具体步骤 | 1. 将 `GScriptNativeForms` 改为拥有型容器，例如 `TMap<asIScriptFunction*, TUniquePtr<FScriptFunctionNativeForm>>`，并把它从 file-static 下沉到 `FAngelscriptOwnedSharedState`、`FAngelscriptStaticJIT` 或专门的 `FNativeFormRegistry`，使其生命周期与当前 script engine 一致。 2. 所有 `BindNative*` / `BindTemplateInstantiatedCall` 等入口改为向该 registry 注册 owned form，不再直接 `new` 后塞进全局静态表。 3. 在 engine/shared-state teardown 中显式 `Reset()` registry；如果短期仍需保留静态入口，至少新增统一 `ClearNativeFormsForCurrentEngine()`，在 bind 重新注册、precompiled generation 结束和 engine 销毁时调用。 4. 若 registry 仍可能跨多个 engine 共存，key 不能继续只用裸 `asIScriptFunction*`；应改成带 engine 作用域的 key，例如 `{EnginePtr, FunctionId}`，或确保 registry 完全 engine-local 后再允许使用 raw pointer。 5. 为避免再次遗漏释放路径，抽一个统一注册 helper，例如 `RegisterNativeForm(asIScriptFunction*, TUniquePtr<FScriptFunctionNativeForm>)`，集中处理重复注册、替换旧值和统计诊断。 6. 增加回归：重复创建/销毁 engine 或重复执行 bind 注册后，断言 registry 大小不会单调增长；再补一个“销毁后重建 engine 仍能正确解析 native form”的 JIT 用例，避免 stale key 复用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 或 shared-state 挂载点、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptMultiEngineTests.cpp` 或新增 StaticJIT registry 生命周期测试 |
| 预估工作量 | M |
| 风险 | 若 registry 生命周期选得过短，可能让仍在使用的 JIT 代码失去 native form；若选得过长，又会把当前泄漏问题换个位置保留。必须先明确 native form 只在 codegen 阶段使用，还是运行期 fallback 也依赖它。 |
| 前置依赖 | 建议先确认 native form 在运行期是否仍会被查询；若仅用于 codegen，可大胆收窄到 generation session 生命周期。 |
| 验证方式 | 1. 新增多轮 engine create/destroy 压力测试，确认 native form registry 在轮次间归零。 2. 统计 registry size 或进程对象数，确认不再线性增长。 3. 回归 JIT custom/native call 生成路径，确认迁移所有权后 native form 解析结果不变。 |

### Issue-62：补足 Analysis 发现 36 的修复方案：`RequestEvaluate` 成功后主动清空地址，V2 `ValueAddress` 对大多数表达式恒为 `0`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h` |
| 行号 | `AngelscriptDebugServer.cpp:1107-1128, 2679-2688`；`AngelscriptType.h:627-630, 727-744` |
| 问题 | `RequestEvaluate` 在 `GetDebuggerValue(...)` 成功后，会把 `Value.GetAddressToMonitor()` 和 `Value.GetAddressToMonitorValueSize()` 写入返回包，看起来支持 V2 monitorable value；但 `GetDebuggerValue()` 在普通 evaluate 路径末尾（`OutInnerValues == nullptr`）又会无条件执行 `CurrentValue.Address = nullptr; CurrentValue.ClearLiteral();`。`FDebuggerValue::GetAddressToMonitor()` 的默认实现如果没有显式 `AddressToMonitor`，就退回 `GetNonTemporaryAddress()`，而后者最终依赖 `Address`。结果是绝大多数局部变量、成员和全局变量在成功 evaluate 之后都会被自己清掉地址，wire 上的 `ValueAddress` 变成 `0`。 |
| 根因 | 当前实现把“避免把内部求值地址长期暴露出去”和“向 V2 客户端导出可监视地址”耦合在同一收尾逻辑里，缺少独立的 monitor-address 生命周期。 |
| 影响 | 客户端即使拿到正确的 `Value` / `Type`，也很难把 evaluate 结果继续用于 data breakpoint、增量 watch 或地址级调试；V2 协议表面上暴露了 `ValueAddress/ValueSize`，实际在最常见的 evaluate 成功路径上却退化成空值，直接削弱了 DebugServer V2 的能力闭环。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 evaluate 的“显示值”和“可监视地址”拆成两个独立生命周期，允许安全 scrub 内部地址，但保留显式 monitor metadata。 |
| 具体步骤 | 1. 在 `Core/AngelscriptType.h` 为 `FDebuggerValue` 明确区分“内部求值地址”和“导出给 debugger 的 monitor address”；最小改动可以在 `GetDebuggerValue()` 结束前先调用 `CurrentValue.SetAddressToMonitor(CurrentValue.GetAddressToMonitor(), CurrentValue.GetAddressToMonitorValueSize())`，随后再清理 `Address`/literal。 2. 更稳妥的实现是新增专门的 finalize helper，例如 `FinalizeForDebuggerResponse(bool bPreserveMonitorAddress)`：当用于 `Evaluate`/`Variables` 响应时，仅 scrub 不该外泄的执行期指针，但保留 `AddressToMonitor` 和 size；当用于纯内部递归求值时再走完全清空。 3. `RequestEvaluate` 分支改为只在成功且返回值具有稳定可监视地址时填充 `Var.ValueAddress/ValueSize`；temporary expression、纯 literal 或无法安全监视的结果应继续显式返回 `0`，不要伪造地址。 4. 与已记录的 `Issue-40` 配套：即便开始正确返回 `ValueAddress`，后续 data breakpoint 仍应走 server-issued token/合法尺寸校验，不能因为本条修复就继续信任客户端自由回传原始地址。 5. 若 `RequestVariables` 也复用了相同 finalize 路径，顺手统一它与 `Evaluate` 的 monitor-address 语义，避免一个能返回地址、另一个仍然清空。 6. 增加集成测试：对一个稳定局部变量、一个对象成员和一个临时表达式分别执行 `RequestEvaluate`，断言前两者在 V2 下返回非零 `ValueAddress`，临时值仍为 `0`；同时回归 `Variables` 展开，确认地址语义一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptType.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp` |
| 预估工作量 | S |
| 风险 | 如果不区分“稳定地址”和“临时值地址”，修复后可能把短生命周期地址暴露给客户端，进而让 data breakpoint 指向失效内存；因此必须和 `temporary value` / `AddressToMonitor` 语义一起收口。 |
| 前置依赖 | 最好结合 `Issue-40` 的 data breakpoint token 设计一起评审，但本条可以先独立修正 `Evaluate` 自身丢地址的问题。 |
| 验证方式 | 1. 在 V2 协议下新增 `Evaluate` 集成测试，验证稳定变量返回非零 `ValueAddress/ValueSize`。 2. 验证 temporary/literal 表达式仍然返回 `0` 地址，不引入悬空 monitor。 3. 回归现有 evaluate 成功/失败路径，确认值文本与类型输出不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-60 | Architecture | 优先处理，先去掉会直接阻断二次 precompiled/JIT 加载的全局静态名状态污染 |
| P2 | Issue-61 | Defect | 在 P1 之后处理，避免 native form registry 继续跨引擎泄漏并保留 stale 键 |
| P2 | Issue-62 | Defect | 随后处理，恢复 DebugServer V2 `Evaluate` 到 data breakpoint/watch 的地址能力闭环 |

---

## 发现与方案 (2026-04-08 23:58)

### Issue-63：`SetBreakpoint` 在函数前空白行或文件尾部无代码行上会安装永远不会命中的死断点

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:955-1019`；`as_scriptfunction.cpp:883-927`；`AngelscriptDebuggerBreakpointTests.cpp:324-431` |
| 问题 | `HandleMessage(SetBreakpoint)` 通过遍历 `FoundModule->scriptFunctions` 并调用 `Func->FindNextLineWithCode(WantedLine)` 来寻找“最近可执行行”。但 `FindNextLineWithCode()` 在普通函数路径中只要 `line < declaredAt` 就直接返回 `-1`，也就是用户把断点放在函数声明前的空白/注释行时，后面的那个函数根本不会成为候选。更糟的是，`BestLine == -1` 且这是新断点（`BP.Id == -1`）时，代码会落到 `CodeLine = WantedLine`，随后把这个没有任何 bytecode 会命中的行号写入 `Active->Lines` 并递增 `BreakpointCount`。 |
| 根因 | file-scoped breakpoint 被错误复用了 function-scoped `FindNextLineWithCode()` 语义；当请求行落在任意函数 `declaredAt` 之前或落在最后一个可执行行之后时，服务端没有单独的“文件级向后吸附 / 明确拒绝”分支，而是直接把原始请求行当成真实断点行。 |
| 影响 | DAP/IDE 前端在空白行、注释行、函数签名前一行或文件尾部点击断点时，会看到服务端 `BreakpointCount` 增加、模块 `hasBreakPoints` 被置真，但脚本执行永远不会停下。这是明确的 breakpoint correctness 缺陷，也会让 UI 错误显示“断点已生效”。当前自动化只覆盖“断点正好落在可执行行”的正向路径，没有这类边界回归。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把断点行解析改成真正的 file-scoped“向后吸附到同文件最近可执行行，否则显式拒绝”，禁止再把 `WantedLine` 直接写成死断点。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 提取 helper，例如 `ResolveBreakpointLineInSection(const asCModule&, const FString& Filename, int32 WantedLine)`，先按目标 filename/section 过滤候选函数，再做“最近可执行行”解析。 2. 对每个候选函数不要直接调用 `FindNextLineWithCode(WantedLine)`；若 `WantedLine < declaredAt`，应改为计算该函数首个可执行行作为候选值，而不是让 AngelScript 的 function-local helper 直接返回 `-1`。 3. 当目标文件内不存在 `>= WantedLine` 的可执行行时，新断点必须回写未验证/删除通知，不能再落到 `CodeLine = WantedLine`；同时如果本次没有真实断点落地，不得提前把 `FoundModule->hasBreakPoints` 留在 `true`。 4. 只在 `CodeLine` 真正解析成功后再把行号写入 `Active->Lines`、递增 `BreakpointCount` 并设置模块 `hasBreakPoints`。 5. 新增调试器集成测试，至少覆盖两类边界：a) 断点放在函数前空白行，期望服务端回写到函数首个可执行行；b) 断点放在文件尾部无代码行，期望服务端显式拒绝而不是制造 inert breakpoint。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | M |
| 风险 | 若只修正单文件场景而不同时处理已有的 multi-section module 映射，可能再次与 `Issue-4` 的 section 归属问题打架；断点吸附规则也需要与客户端的 relocated/unverified 语义保持一致。 |
| 前置依赖 | 建议与 `Issue-4` 的 section/file 维度重构一并设计，至少共享同一个“按文件解析可执行行”的 helper。 |
| 验证方式 | 1. 新增“函数前空白行吸附”和“文件尾部无代码行拒绝”回归测试。 2. 回归现有 `Debugger.Breakpoint.HitLine`、`Debugger.Breakpoint.ClearThenResume`，确认精确可执行行场景不回归。 3. 人工用 DAP 客户端在注释行和空白行点击断点，确认前端看到的是 relocated/unverified，而不是无提示的死断点。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-63 | Defect | 先修复断点行解析的死断点问题，避免前端显示“已设置”但运行时永不命中 |

---

## 发现与方案 (2026-04-08 23:59)

### Issue-64：`asBC_DIVi64` 生成的 C++ 守卫条件少一个 `]`，任何 `int64` 除法都会打坏 StaticJIT 输出

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptTypeTests.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:5808-5824`；`AngelscriptStaticJIT.cpp:3328-3337`；`AngelscriptTypeTests.cpp:78-92` |
| 问题 | `asBC_DIVi64::Implement()` 在生成 divide-by-zero guard 时写出了 `Context.Line("  if (divider == 0) [[unlikely]");`，字符串里少了一个 `]`。`FAngelscriptStaticJIT::GenerateCppCode()` 会把 `Bytecode.Implement()` 产出的文本直接写进输出函数体，并只检查 `bImplemented`，不会对生成出来的 C++ 再做语法校验。结果是只要某个脚本函数包含 `int64` 除法并进入 StaticJIT 生成路径，输出文件里就会出现非法属性语法，后续 C++ 编译阶段直接失败。仓库里虽然已有 `int64` 基础类型测试，但没有任何一条 StaticJIT/Precompiled 自动化覆盖 `int64` 除法或模运算 codegen。 |
| 根因 | 字节码实现把 C++ attribute 文本硬编码成裸字符串，缺少对生成片段的最小语法回归；当前生成流水线又只验证“这个 bytecode handler 有没有返回 true”，没有验证“它写出的代码能不能编译”。 |
| 影响 | 这不是运行时偶发分支，而是稳定的 build breaker：脚本一旦使用 `int64` 除法并被收进 transpiled bundle，StaticJIT 生成出的 `.cpp` 就会包含语法错误，阻断后续编译/打包链路。由于 `asBC_MODi64` 紧邻它复用了同一类文本模式，说明当前 codegen 对这类手写字符串没有任何护栏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先修正 `asBC_DIVi64` 的字面字符串，再补最小 codegen 语法回归，避免类似文本错误继续潜伏到 C++ 编译阶段才暴露。 |
| 具体步骤 | 1. 把 `AngelscriptBytecodes.cpp:5811` 的生成文本改成 `[[unlikely]]`，并顺手审计相邻 `DIVi64/MODi64`、`DIVi/MODi` 等分支的 attribute 字符串是否一致。 2. 在 StaticJIT 测试侧新增最小 fixture，构造包含 `int64` 除法和 `%` 的脚本函数，驱动 `GenerateCppCode()` 或等价生成入口，断言输出文本至少包含合法的 `if (divider == 0) [[unlikely]]` 片段。 3. 若当前 harness 能跑生成后编译，增加一条真正的 compile smoke test；若暂时只能测文本，则至少把关键 guard 文本和函数声明做字符串断言，防止再次出现单字符拼写错误。 4. 对所有 `Context.Line("...[[unlikely]]...")`、异常宏和分支模板提取成小 helper 或常量，例如 `EmitUnlikelyIf(Context, "divider == 0")`，减少今后继续手写 attribute 字符串的表面积。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` 或新增 StaticJIT codegen 测试文件 |
| 预估工作量 | S |
| 风险 | 如果只改这一处而不补生成文本回归，类似的单字符错误仍可能在其它 bytecode handler 中继续潜伏到构建阶段才暴露。 |
| 前置依赖 | 无。 |
| 验证方式 | 1. 新增 `int64` 除法/取模的 StaticJIT codegen 回归，确认生成文本可通过语法检查。 2. 若环境允许，实际编译一份包含 `int64` 除法脚本的 transpiled 输出，确认不再因为 `[[unlikely]` 报编译错误。 3. 回归现有 `int64` 基础类型测试，确认修复不改变数值语义。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-64 | Defect | 在继续扩充 JIT 覆盖前先修掉，避免 `int64` 除法直接打坏 transpiled C++ 构建 |

---

## 发现与方案 (2026-04-09 00:00)

### Issue-65：Windows data breakpoint 的寄存器编码在两处复制 `switch/case`，已经适合改成表驱动 helper

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:134-204, 234-257, 272-340, 1275-1283` |
| 问题 | `ApplyBreakpointsToThreadContext(...)` 目前有两个重载：一个接 `TArray<FAngelscriptDataBreakpoint>`，用于 `UpdateDataBreakpoints()` 的安装路径；另一个接 `FAngelscriptActiveDataBreakpoint*`，用于异常处理器命中后的寄存器重写。两份实现都重复维护了同一套 `switch (RegisterToModify)` 写 `Dr0-Dr3`、`switch (AddressSize)` 组装 `Dr7` 长度位、以及 `0x1 << (RegisterToModify * 2)` 的 enable 规则。也就是说，同一套硬件 breakpoint 编码规则现在被散落在两段几乎逐行复制的逻辑里。 |
| 根因 | data breakpoint 代码把“权威状态容器不同”误写成了“编码算法也各自拷一份”，没有先抽象出统一的 slot 视图和纯编码 helper。 |
| 影响 | 这类 duplication 已经让安装路径和异常路径形成两个维护面：后续只要调整支持的 `AddressSize`、修正 `Dr7` 位布局、增加校验或扩展 slot 元数据，就必须同时修改两段 `switch/case`。任何一边漏改，都会造成“初次安装写入的是一套编码，异常命中后 re-arm 回去的是另一套编码”，问题表现将是 data breakpoint 偶发失效、只在部分路径复现，调试成本很高。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 抽出单一的寄存器布局编码层，用表驱动 slot 描述替代两份重复 `switch/case`。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 定义轻量 slot 视图，例如 `FHardwareBreakpointSlotView { bool bEnabled; uint64 Address; uint8 AddressSize; }`，再分别提供从 `FAngelscriptDataBreakpoint` 和 `FAngelscriptActiveDataBreakpoint` 投影成该视图的 adapter。 2. 提取纯 helper，例如 `ApplyBreakpointSlotsToContext(TConstArrayView<FHardwareBreakpointSlotView> Slots, CONTEXT& Context)`；唯一负责清 `Dr7` 的可写位、写 `Dr0-Dr3`、设置 local enable，以及把 `{1,2,4,8}` 映射到对应长度编码。 3. 用表驱动消除 `switch (RegisterToModify)`：可以使用 `DWORD64* Registers[] = { &Context.Dr0, &Context.Dr1, &Context.Dr2, &Context.Dr3 };` 按索引写地址，避免以后再复制四个 case。 4. 用显式查表消除 `switch (AddressSize)`，例如 `TMap<uint8, DWORD64>` 或 `constexpr` helper `EncodeBreakpointLength(uint8 Size)`；非法尺寸直接返回失败/断言，而不是继续散落默认值。 5. 让 `FUpdateDebugRegisterThread::Run()` 和 `DebugRegisterExceptionHandler()` 都只调用同一个 helper，确保安装路径与命中后 re-arm 路径永远使用同一套编码。 6. 为纯编码 helper 补最小单元测试，覆盖 1/2/4/8-byte slot、空 slot、以及多槽组合，确认 `Dr0-Dr3` 和 `Dr7` 的输出一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` 或新增 Windows 专用编码测试文件 |
| 预估工作量 | M |
| 风险 | 若在重构时把“权威状态同步”和“寄存器编码”再次耦合到一起，可能让异常路径重新读到不该访问的运行时字段；因此 helper 只应消费只读 slot 视图，不应顺手修改命中状态。 |
| 前置依赖 | 无，但建议与后续 data breakpoint snapshot/thread-safety 改造共用同一套 slot 抽象，避免二次返工。 |
| 验证方式 | 1. 新增纯编码测试，比较安装路径和异常 re-arm 路径对同一 slot 输入的 `CONTEXT` 输出完全一致。 2. 回归现有 Windows data breakpoint 集成场景，确认抽象后命中与清除行为不变。 3. 人工审阅修改后源码，确认 `Dr0-Dr3` 和 `AddressSize` 规则只剩单一实现。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-65 | Refactoring | 在修复当前 data breakpoint correctness 缺陷时同步收口编码逻辑，避免后续寄存器规则继续多点漂移 |

---

## 发现与方案 (2026-04-09 00:07)

### Issue-66：`StartDebugging` / `StopDebugging` / 断线清理不会重置瞬时停点状态，新会话会继承旧会话的 pending step 与 ignore marker

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.h:597-600, 729-733`；`AngelscriptDebugServer.cpp:550-573, 606-608, 724-742, 897-922, 1222-1238` |
| 问题 | DebugServer 把瞬时执行状态保存在成员 `bBreakNextScriptLine`、`bIsPaused`、`IgnoreBreakSection`、`IgnoreBreakLine`、`ConditionBreakFunction`、`ConditionBreakFrame` 中。它们会在 `Pause` / `Step*` / data breakpoint 路径里被置位，并在 line callback 命中时驱动 `step` 停止或“忽略同一 safe point 再次停下”。但会话边界上的清理路径并没有重置这些字段：最后一个 client 断线时只做 `bIsDebugging = false; bIsPaused = false; ClearAllBreakpoints();`；`StartDebugging` 和 `StopDebugging` 也只是调用 `ClearAllBreakpoints()`。而 `ClearAllBreakpoints()` 只清脚本断点表、`SectionBreakpoints`、`BreakpointCount` 和 data breakpoints，没有碰任何上述瞬时状态。结果是旧会话留下的 pending pause/step 与 ignore marker 会泄漏到后续会话。 |
| 根因 | 断点权威状态和“当前这次调试流程里的瞬时执行状态”被拆在不同成员里，但 session reset 只清理了前者，没有统一的 transient-state reset。 |
| 影响 | 这会产生跨会话 correctness 偏差：1. 会话 A 在 running 态发出 `Pause` / `StepIn` / `StepOver` / `StepOut` 后，如果在真正停下前 `StopDebugging` 或直接断线，会话 B 下一次进入脚本行回调时可能立刻收到一条伪造的 `step` 停止；2. 会话 A 若正停在某个 line breakpoint 上，`IgnoreBreakLine` / `IgnoreBreakSection` 会保持为上次停点，新的会话在同一 safe point 上可能把本应命中的首个 breakpoint 静默跳过。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 引入统一的 `ResetTransientDebugExecutionState()`，把 session 边界与正常 resume 明确区分，保证旧会话的 pending stop / ignore marker 不会泄漏到新会话。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h/.cpp` 新增私有 helper，例如 `ResetTransientDebugExecutionState(bool bResetPausedState)`，统一重置 `bBreakNextScriptLine=false`、`ConditionBreakFrame=-1`、`ConditionBreakFunction=nullptr`、`IgnoreBreakLine=-1`、`IgnoreBreakSection=nullptr`，并按调用场景决定是否同时清 `bIsPaused`。 2. 在 `StartDebugging` 进入新会话前先调用该 helper，再清 breakpoint/data breakpoint 权威状态，确保新 session 从干净的 execution-state 起步。 3. 在 `StopDebugging`、最后一个调试 client 断线、以及后续统一的 client release helper 中都调用同一个 reset helper；不要只依赖 `ClearAllBreakpoints()`，因为它本身不覆盖这些瞬时字段。 4. 将 `ClearAllBreakpoints()` 保持为“权威断点状态清理”，不要继续隐式承载 transient stop 语义；若必须在某些 teardown 路径里两者同时发生，就由更高层 session-reset helper 先后显式调用，避免以后再次遗漏。 5. 与已记录的 `Issue-11`、`Issue-20`、`Issue-29` 协同设计时，把这组字段直接下沉到 per-session / per-thread stop state，彻底消除“server-global 残留状态跨会话复用”的可能；本条先做最小热修，至少堵住当前跨会话泄漏。 6. 补两组回归：a) client A 发送 `Pause` 或 `StepOver` 后，在真正 `HasStopped` 之前 `StopDebugging`/断线，client B 重新 `StartDebugging` 后首个脚本 safe point 不应无故收到 `HasStopped(reason=\"step\")`；b) client A 停在某个 line breakpoint 后结束会话，client B 在同一脚本位置重新进入时，该 breakpoint 仍应正常命中，不应被旧 `IgnoreBreak*` 状态吞掉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSteppingTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果把 transient reset 粗暴塞进 `ClearAllBreakpoints()`，可能误伤同一会话内本该保留的 pause/step 语义；因此必须把“会话重置”和“断点表清空”分成两层显式 API。 |
| 前置依赖 | 无；但若近期会推进 `Issue-11` 的 session state 重构，建议共用同一个 reset 入口，避免短期热修后再次分叉。 |
| 验证方式 | 1. 新增跨会话回归，验证旧会话挂起的 `Pause` / `Step*` 不会污染新会话的首个 safe point。 2. 新增“停在 breakpoint 后结束会话再重连”的回归，确认同一行的首个 breakpoint 不会因旧 `IgnoreBreak*` 被吞掉。 3. 回归现有 stepping/breakpoint smoke tests，确认正常 `Continue` / step 语义不受影响。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-66 | Defect | 在继续推进 session/state 重构前先修复，避免新调试会话继承旧会话的瞬时停点状态 |

---

## 发现与方案 (2026-04-09 00:14)

### Issue-67：补足 Analysis 发现 58 的修复方案：`ClearAllAngelscriptDataBreakpointsFromHandler()` 不会真正撤销当前线程硬件断点，也不会同步客户端状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:338-370, 388-392, 479-545, 1241-1285` |
| 问题 | `ClearAllAngelscriptDataBreakpointsFromHandler()` 只是把全局标志 `GClearDataBreakpoints` 置 `true`。真正进入 `DebugRegisterExceptionHandler()` 后，如果本轮命中过 data breakpoint，代码会先在 `340` 行把当前 `ActiveDataBreakpoints` 重新编码回 `ExceptionInfo->ContextRecord`，随后才在 `357-367` 的 `GClearDataBreakpoints` 分支里把 active mirror 的 `bTriggered/Status` 改成“待移除”，并把 `bBreakNextScriptLine` 清成 `false`。这条分支之后没有第二次重写当前异常上下文的 `Dr0-Dr3/Dr7`，也没有触发 `UpdateDataBreakpoints()`。而正常能把 active 状态同步回 `DataBreakpoints`、广播 `ClearDataBreakpoints` 并真正重编程寄存器的路径，恰恰依赖 `ProcessScriptLine()` 里的 `DataBreakpoints.Num() > 0 && bBreakNextScriptLine` 条件；这里又把 `bBreakNextScriptLine` 主动关掉了。 |
| 根因 | handler 内的“紧急清除”只覆盖了 active mirror 的软件标志，没有复用正常 data breakpoint 生命周期里的“权威状态同步 -> debug register 重编程 -> 客户端广播”闭环；同时它还切断了唯一会触发该闭环的 line-callback 路径。 |
| 影响 | 这条 immediate-window 逃生口在最需要可靠的时候反而可能失效：用户以为自己已经清掉了正在 spam 的 watchpoint，但当前线程的硬件 debug registers 仍可能保持 armed，后续写入继续触发 `EXCEPTION_SINGLE_STEP`；客户端也收不到任何 `ClearDataBreakpoints` 同步，UI 会继续显示旧 watchpoint 存在。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“异常上下文里立即撤销硬件寄存器”和“游戏线程上清空权威状态并广播客户端”拆成两阶段，避免再把紧急清除绑在下一条脚本行上。 |
| 具体步骤 | 1. 在 `Debugging/AngelscriptDebugServer.cpp` 新增一个无堆分配 helper，例如 `ClearBreakpointRegistersInContext(CONTEXT& Context)`，专门把 `Dr0-Dr3` 清零、清掉 `Dr7` 的 local enable/len/type 位，并设置 `Context.ContextFlags |= CONTEXT_DEBUG_REGISTERS`。不要在 handler 里继续依赖“先把 active array 重新编码回上下文，再等后续某处清掉”。 2. 在 `GClearDataBreakpoints` 分支命中时，立即调用该 helper 覆盖 `ExceptionInfo->ContextRecord`，确保当前异常返回后本线程不再携带任何 armed hardware watchpoint。 3. 与即时寄存器清除分开，再新增一个原子化的 deferred flag，例如 `GPendingForceClearDataBreakpoints`；handler 里只负责置位，不做任何分配、广播或容器遍历。 4. 在 `Tick()` 或 `ProcessMessages()` 开头消费该 deferred flag：先收集当前 `DataBreakpoints` 的 `Ids`，向 debugging clients 发送一次 `ClearDataBreakpoints`，再调用 `ClearAllDataBreakpoints()` 清空权威状态并统一走 `UpdateDataBreakpoints()`，保证其它线程/后续写入也完成硬件寄存器撤销。 5. `GClearDataBreakpoints` 分支不要再靠 `bBreakNextScriptLine=false` 来“阻止再次停下”；真正的防抖应该来自寄存器已被立即清空，以及权威状态在 game thread 上被统一清空。若担心旧 pending stop 残留，可在消费 deferred clear 时同步调用本轮新增的 transient-state reset helper，而不是把副作用继续藏在 handler 里。 6. 增加 Windows 专项回归：a) 用测试 seam 或 fake `CONTEXT` 验证 `GClearDataBreakpoints` 路径会立刻清掉 `Dr7`，不需要等下一条脚本行；b) 验证 deferred clear 会在没有后续 line callback 的情况下仍然把 `DataBreakpoints` 清空并广播 `ClearDataBreakpoints`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` 或新增 Windows data breakpoint 专项测试文件 |
| 预估工作量 | M |
| 风险 | handler 运行在异常上下文里，不能做堆分配或复杂容器操作；如果把“立即清寄存器”和“同步权威状态”混在一起，容易引入新的 re-entrancy 或 allocator 风险。 |
| 前置依赖 | 无；但如果后续推进 `Issue-29/31` 的 snapshot 与 thread-aware data breakpoint 重构，建议共用同一套“即时禁用寄存器 + 延迟清权威状态”框架。 |
| 验证方式 | 1. Windows 专项测试验证 `ClearAllAngelscriptDataBreakpointsFromHandler()` 后当前异常上下文的 `Dr7` 已清空。 2. 验证即使后续没有新的脚本行，权威 `DataBreakpoints` 也会在下一次 `Tick/ProcessMessages` 被清空，并向客户端发送 `ClearDataBreakpoints`。 3. 回归正常 data breakpoint 命中、HitCount 用尽、变量出作用域路径，确认新 deferred clear 不会破坏既有自动移除逻辑。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-67 | Defect | 在继续依赖 Windows data breakpoint 调试前先修复，确保紧急逃生口能真正解除 spam watchpoint |

---

## 发现与方案 (2026-04-09 00:15)

### Issue-68：`BreakOptions` 只过滤 line breakpoint，exception 和 data breakpoint 会绕过同一套 stop filter

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:440-462, 479-545, 602-605, 652-665, 1137-1145`；`AngelscriptDebugServer.h:603-604`；`AngelscriptDebuggerSmokeTests.cpp:90-123`；`AngelscriptDebugProtocolTests.cpp:217-226` |
| 问题 | 服务端收到 `BreakOptions` 后，会把 filter 只存进全局 `BreakOptions` 数组（`1137-1145`）。真正消费这组 filter 的地方只有 line breakpoint 命中分支里的 `ShouldBreakOnActiveSide()`（`602-605`，实现见 `652-665`）。相对地，`ProcessException()` 在发送 `HasStopped(reason="exception")` 前完全不检查 `BreakOptions`（`440-462`）；data breakpoint 触发后也直接在 `479-545` 里构造 `FStoppedMessage` 并 `PauseExecution()`，同样没有走 `ShouldBreakOnActiveSide()`。现有测试只验证能收到 `BreakFilters` 响应和协议 round-trip（`SmokeTests.cpp:90-123`，`DebugProtocolTests.cpp:217-226`），没有任何一条覆盖“设置 filter 后 exception/data breakpoint 也应遵守同一侧过滤”的运行时语义。 |
| 根因 | stop filter 被实现成 line-breakpoint 路径里的局部钩子，而不是统一的 stopped-event 出口策略；协议状态、异常停点、data breakpoint 停点各走一套独立分支，导致 `BreakOptions` 语义天然分叉。 |
| 影响 | 一旦上层用 `BreakOptions`/`BreakFilters` 区分 Server/Client/Any 侧，line breakpoint 会按配置过滤，exception 和 data breakpoint 却仍然在被禁用的一侧停下。这样前端无法把一次 filter 配置稳定映射到所有 stopped event，DAP 适配也无法可靠实现统一 break filter 语义。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 stop filter 收敛到统一的 stopped-event 决策层，所有停点来源在发送 `HasStopped` 前都走同一套 `BreakOptions` 判定。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h/.cpp` 提取统一 helper，例如 `ShouldEmitStoppedEvent(EAngelscriptStopSource Source, const FStoppedMessage& Msg, asIScriptContext* Context)`；内部先复用现有 `ShouldBreakOnActiveSide()`，再根据 `Source` 预留 exception/data breakpoint 的扩展位，不再让 line breakpoint 单独内嵌过滤逻辑。 2. 把 `ProcessException()`、line breakpoint 命中分支、以及 `ProcessScriptLine()` 里的 data breakpoint stop 全部改成先调用该 helper，再决定是否 `PauseExecution()`；不允许再有任何 stop source 直接绕过统一 filter。 3. 重新定义 data breakpoint 的 stop 分类，不要继续把它硬编码成 `Reason="exception"`；至少要让统一 helper 能区分 `LineBreakpoint`、`Exception`、`DataBreakpoint` 三类来源，否则后续 filter 语义无法继续细化。 4. 如果当前 filter delegate 只接受 `BreakOptions + WorldContext`，先在 helper 层把“来源类型”编码进临时 `FName` option 集合或新增重载 delegate，保证旧 line breakpoint 行为不变的同时，为 exception/data breakpoint 提供等价判定入口。 5. 在 `AngelscriptDebuggerSmokeTests` 或新增 debugger 集成测试里补三组用例：同一 filter 配置下分别命中 line breakpoint、脚本 exception、data breakpoint，验证三者都只会在允许的一侧停下。 6. 在 `AngelscriptDebugProtocolTests` 增加最小行为测试或 test seam，确认设置 `BreakOptions` 后，统一 helper 至少会被 exception/data breakpoint 路径调用一次，避免未来又回到“协议有 filter，运行时只有一条路径消费”的分裂状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只把 line breakpoint 的旧逻辑搬进 helper，而不同时澄清 data breakpoint 的 stop reason/category，后续客户端仍可能无法区分“被 filter 掉的异常”和“被错误分类成 exception 的 watchpoint”。 |
| 前置依赖 | 建议与现有 data breakpoint stop reason 修正项协同设计，但没有硬性前置依赖。 |
| 验证方式 | 1. 新增 break filter 集成测试，分别覆盖 line breakpoint、脚本 exception、data breakpoint 三类停点来源。 2. 人工验证切换 `Server/Client/Any` filter 时，三类 stop event 的行为保持一致。 3. 回归现有 `BreakFilters` handshake / round-trip 测试，确认协议兼容性不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-68 | Architecture | 作为 break filter 语义收敛项尽快处理，避免协议 filter 只对 line breakpoint 生效 |

---

## 发现与方案 (2026-04-09 00:17)

### Issue-69：`HasStopped` 不携带 breakpoint id，line/data breakpoint 命中后客户端无法知道究竟触发了哪一个断点

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:129-142, 227-244, 300-326, 695-699`；`AngelscriptDebugServer.cpp:479-545, 589-609, 955-1019`；`AngelscriptDebuggerBreakpointTests.cpp:401-416` |
| 问题 | 协议层表面上已经有 breakpoint id：`FAngelscriptBreakpoint` 和 `FAngelscriptDataBreakpoint` 都带 `Id` 字段（`227-244`, `300-326`）。但 stop payload `FStoppedMessage` 只有 `Reason/Description/Text` 三个字符串（`129-142`），没有任何 breakpoint id 承载位。运行时 line breakpoint 命中时，服务器只检查 `ActiveBreakpoints->Lines.Contains(Line)`（`589-609`），而 `FFileBreakpoints` 的常驻状态也只有 `TSet<int32> Lines`（`695-699`），请求侧传进来的 `BP.Id` 在命中路径里已经无从找回。data breakpoint 虽然在权威状态里保留了 `Id`，但触发后 `ProcessScriptLine()` 只拼文本并发出 `FStoppedMessage`（`479-545`），同样不会把 `Breakpoint.Id` 发回客户端。现有 breakpoint 集成测试也只断言 `HasStopped.Reason == "breakpoint"`（`BreakpointTests.cpp:401-416`），没有任何“命中了哪个 breakpoint id”的协议护栏。 |
| 根因 | DebugServer 的权威断点状态和 stop 事件模型没有围绕“breakpoint identity”设计：line breakpoint 在服务端被压缩成按行去重集合，stopped event 则被压缩成纯文本消息，两端都没有为 id 建立回传通路。 |
| 影响 | 当前端同时维护多个 line breakpoint、data breakpoint、hit-count breakpoint，或需要把 stop 映射回具体 UI 条目时，只能靠行号和文本猜测命中源。DAP 适配层无法可靠实现 `hitBreakpointIds` 或等价语义；多个断点落在同一行、同名 data breakpoint 同时存在、以及命中后需要精确更新 verified/hit-count 状态的场景都会变得不可信。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 breakpoint id 成为 stop pipeline 的一等公民：权威状态保留 id，`HasStopped` 明确携带命中 id 列表，并保持旧客户端兼容。 |
| 具体步骤 | 1. 扩展 `FStoppedMessage`，新增可选字段例如 `TArray<int32> BreakpointIds`；序列化采用现有协议常用的“尾部可选字段”模式或按 `DebugAdapterVersion` 协商，保证旧客户端仍能读取已有三个字符串字段。 2. 重构 `FFileBreakpoints`，不要再只保存 `TSet<int32> Lines`；至少改成 `TMap<int32, TArray<int32>>` 或 `TMultiMap<int32, int32>`，把“这一行有哪些 breakpoint id”保留下来。若当前 line-breakpoint 修复正在推进 filename/section 维度重构，可把 id 一并并入同一个权威结构，避免二次迁移。 3. 在 `SetBreakpoint` / `ClearBreakpoints` 路径里按 `BP.Id` 增删对应行的 id 集合；命中 line breakpoint 时，从当前行提取所有匹配 id 填入 `FStoppedMessage.BreakpointIds`，不再只发送 `Reason="breakpoint"`。 4. data breakpoint 触发时，直接把 `Breakpoint.Id` 写进同一个 `BreakpointIds` 列表；如果一次触发多个 watchpoint，就按命中顺序或稳定排序返回多个 id，而不是只把名称拼进 `Text`。 5. 与现有 data breakpoint stop reason/分类修正协同，让 line breakpoint 与 data breakpoint 都能复用同一套 `BreakpointIds` 输出，而 exception 类 stopped event 则保持该字段为空，避免再靠 `Text` 猜来源。 6. 新增两层回归：a) protocol round-trip 测试验证新字段在 v2/新版本客户端下可读，在旧版本下不会破坏反序列化；b) debugger 集成测试分别命中 line breakpoint 与 data breakpoint，断言 `HasStopped` 返回预期 id。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下新增 data breakpoint 停止测试 |
| 预估工作量 | M |
| 风险 | 如果直接修改 `FStoppedMessage` 而不做版本协商或尾部可选字段兼容，现有客户端会在反序列化时读错包体；同时 line breakpoint 权威状态一旦并入 id，还需要与已记录的 filename/section 维度修复保持数据结构一致。 |
| 前置依赖 | 最好与既有 line breakpoint 存储重构项和 data breakpoint stop-reason 修正项协同设计，但可以先独立落地协议字段和最小 id 通路。 |
| 验证方式 | 1. 新增 protocol 测试，验证带 `BreakpointIds` 的 `FStoppedMessage` 对新旧客户端都能正确反序列化。 2. 新增 debugger 集成测试，分别命中 line breakpoint 与 data breakpoint，断言 stopped event 返回的 id 与请求设置的 id 一致。 3. 人工验证多个断点落在同一行时，客户端能收到完整 id 列表而不是只靠文本猜测。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-69 | Architecture | 在 break filter 与 stop 分类收敛后跟进，补齐 DAP/IDE 所需的 breakpoint identity 回传能力 |

---

## 发现与方案 (2026-04-09 00:18)

### Issue-70：`SetDataBreakpoints` 超过 4 个请求时会静默丢弃尾部条目，协议没有任何 verified/reject 反馈

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.h:713-716`；`AngelscriptDebugServer.cpp:1061-1068, 1248-1286`；`AngelscriptDebugProtocolTests.cpp:174-214` |
| 问题 | 服务端把 hardware watchpoint 上限硬编码为 `DATA_BREAKPOINT_HARDWARE_LIMIT = 4`（`713-716`）。但收到 `SetDataBreakpoints` 时，`HandleMessage()` 会先把客户端发来的整份列表原样赋给 `DataBreakpoints`（`1061-1067`），随后 `RebuildActiveDataBreakpoints()` 只复制 `min(DataBreakpoints.Num(), 4)` 条到 `ActiveDataBreakpoints`（`1248-1264`），`UpdateDataBreakpoints()` 也只会把这 4 条写进调试寄存器（`1275-1286`）。第 5 条及之后的请求既不会返回拒绝，也不会从 `DataBreakpoints` 里显式标成“未验证”；客户端只能看到请求发送成功，却永远收不到命中。现有 protocol tests 也只覆盖两条 data breakpoint 的 round-trip（`174-214`），没有任何容量上限或服务器确认语义的护栏。 |
| 根因 | 运行时实现受 4 个硬件寄存器槽位约束，但协议层没有把“请求列表”与“实际验证/安装结果”区分开；服务端选择了静默截断，而不是显式回执。 |
| 影响 | 一旦前端一次性设置超过 4 个监视点，用户会误以为它们全部生效，实际只有前 4 个可能触发。断点顺序会影响结果，DAP/IDE 也无法把未安装项显示成 unverified/rejected，属于明确的协议正确性缺口。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `SetDataBreakpoints` 改成“请求 -> 验证 -> 回执”三段式协议，超出硬件上限的条目必须显式拒绝，不能再静默丢弃。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 新增 data breakpoint 结果消息，例如 `FAngelscriptDataBreakpointResult { int32 Id; bool bVerified; FString Message; }` 与对应的 `SetDataBreakpointsResult` 容器；若当前 wire format 不方便直接扩展，可先 version-gate 一个新消息类型，但目标是给每个请求条目单独回执。 2. `HandleMessage(EDebugMessageType::SetDataBreakpoints)` 中不要再直接 `DataBreakpoints = BP.Breakpoints`；先逐条验证请求，把前 4 条可安装项收进新的 authoritative 列表，其余条目立即生成 `bVerified=false`、`Message="hardware limit 4 exceeded"` 的结果。 3. `UpdateDataBreakpoints()` 返回或记录本次硬件编程是否成功；只有真正写入成功的条目才回执 `bVerified=true`。若 `GetThreadContext()` / `SetThreadContext()` 失败，要把本次所有待安装条目标成失败，而不是继续让客户端以为已生效。 4. `RebuildActiveDataBreakpoints()` 只消费已验证通过的 authoritative 列表，不再让“请求层面存在、硬件层面不存在”的尾部条目长期留在 `DataBreakpoints` 中制造假象。 5. 若短期内必须兼容旧客户端，至少增加诊断日志和调试事件，把超上限条目明确告知请求方；但这只能作为过渡，最终仍应提供 per-breakpoint verified/reject 回执，对齐 DAP `setDataBreakpoints` 语义。 6. 增加两组回归：a) 发送 5 条 data breakpoint，请求结果必须显示前 4 条 verified、最后 1 条 rejected；b) 故意制造硬件编程失败，断言客户端能收到失败回执，而不是静默进入半安装状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 下新增 data breakpoint 容量/验证集成测试 |
| 预估工作量 | M |
| 风险 | 如果只加回执、不同时清理 authoritative `DataBreakpoints` 里的未安装尾部条目，后续 clear/reapply/sync 逻辑仍会围绕一份“看起来存在、其实没装上”的假状态继续工作。 |
| 前置依赖 | 与既有 data breakpoint 线程/寄存器稳定性修复相互增强，但无硬性前置依赖。 |
| 验证方式 | 1. 新增 protocol 测试，验证超过 4 条请求时会收到逐项 verified/reject 结果。 2. 新增 debugger 集成测试，确认第 5 条 watchpoint 不会再静默失效，而是被客户端明确显示为 rejected/unverified。 3. 回归现有 1-4 条 data breakpoint 场景，确认正常容量内行为不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-70 | Defect | 在 data breakpoint 协议与回执能力收口时一并处理，先消除“请求成功但硬件没装上”的静默失败 |

---

## 发现与方案 (2026-04-09 00:27)

### Issue-71：`BreakFilters/BreakOptions` 在当前源码里只是协议空壳，客户端能协商能力却永远得不到真实过滤行为

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:652-665, 1137-1159`；`AngelscriptRuntimeModule.cpp:48-57`；`AngelscriptRuntimeModule.h:10-11, 33-34`；`AngelscriptDebuggerSmokeTests.cpp:90-123`；`AngelscriptDebugProtocolTests.cpp:217-227` |
| 问题 | `RequestBreakFilters` 会无条件回一条 `BreakFilters` 消息，但消息内容完全来自 `FAngelscriptRuntimeModule::GetDebugBreakFilters().ExecuteIfBound(FilterList)`；若 delegate 未绑定，`FilterList` 为空，协议仍然宣称这项能力存在。另一方面，`BreakOptions` 虽然会把客户端传入的 filter 名字写进 `BreakOptions` 数组（`1137-1145`），但运行时唯一消费入口 `ShouldBreakOnActiveSide()` 在 `GetDebugCheckBreakOptions()` 未绑定时直接 `return true`（`652-665`），等价于所有过滤条件都失效。`AngelscriptRuntimeModule.cpp` 只定义了两个静态 delegate accessors，没有任何默认绑定；对 `Plugins/Angelscript` 的 repo-wide 检索也只找到定义、调用点和测试，没有找到任何 `Bind`/`BindLambda`/`BindRaw` 对这两个 delegate 的实际注册。现有 smoke/protocol tests 也只验证“能收到 `BreakFilters` 响应”和“消息能 round-trip”，没有验证过滤器真的改变停点行为。 |
| 根因 | DebugServer 把 `BreakFilters/BreakOptions` 设计成“由外部模块自行注入”的可选扩展，但没有同时提供默认实现、能力协商降级或未绑定告警，最终把一个未接线的扩展直接暴露成稳定协议能力。 |
| 影响 | 客户端会误以为服务端支持 break filter：握手能拿到 `BreakFilters` 响应，发送 `BreakOptions` 也不会报错，但运行时停点行为保持不变。对 DAP/IDE 来说，这属于明确的 capability lie；前端若依赖该能力实现只在 server/client 一侧停下，最终得到的是“配置成功、行为无变化”的 silent failure。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 break filter 从“可选裸 delegate”升级为显式能力：要么提供真实默认实现并绑定，要么在握手里明确声明不支持，不能继续把空壳暴露给客户端。 |
| 具体步骤 | 1. 在 `AngelscriptRuntimeModule.h/.cpp` 为 `GetDebugBreakFilters()` 和 `GetDebugCheckBreakOptions()` 增加成对的注册入口与状态查询，例如 `bool HasDebugBreakFilterProvider()`；不要再让 `DebugServer` 只能靠 `ExecuteIfBound()` 被动猜测。 2. 修改 `RequestBreakFilters` 路径：若 provider 未注册，就不要再回一个“看似成功但为空”的能力响应；应改成显式的 `NotSupported`/capability bit，或者至少返回 `DebugServerVersion` 扩展字段表明 `bSupportsBreakFilters=false`。 3. 修改 `BreakOptions` 与 `ShouldBreakOnActiveSide()`：当 provider 未注册时，服务端应记录一次 warning，并把这条配置视为 unsupported/no-op；不要继续 `return true` 然后让客户端误以为过滤器已生效。 4. 若产品确实需要该能力，优先在 runtime/editor 初始化阶段绑定一份默认 provider，至少把当前 `break:any`、`break:server`、`break:client` 这类约定收敛成可验证实现；避免每个宿主项目都重复接线。 5. 把握手测试扩成行为测试：不仅要拿到 `BreakFilters` payload，还要验证 provider 已注册时返回非空 filters、未注册时明确报告 unsupported；同时增加 line breakpoint/exception/data breakpoint 的过滤行为回归，证明 `BreakOptions` 不再是空设置。 6. 与已记录的 `Issue-68` 联动：先解决“能力未接线”问题，再推进“异常/data breakpoint 也遵守过滤器”的 stop-source 收敛，否则客户端仍会面对一半空壳、一半语义分叉的双重问题。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerSmokeTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接把“未注册 provider”改成 no-op/unsupported，而某些外部工具已经默默依赖当前空响应行为，短期可能暴露兼容性变化；因此最好通过版本位或 capability flag 渐进切换。 |
| 前置依赖 | 无；但若要一次性补齐真实过滤行为，建议同步评审当前 server/client side 判定来源，避免默认 provider 先把错误语义固化下来。 |
| 验证方式 | 1. 新增 handshake 测试，分别覆盖“provider 已注册”和“provider 未注册”两种状态，确认服务端不会再把空壳能力伪装成已支持。 2. 新增运行时过滤测试，设置 `BreakOptions` 后实际命中 breakpoint/exception/data breakpoint，验证行为与过滤器配置一致。 3. repo-wide 检查确保 break filter provider 的注册点有自动化覆盖，不再依赖人工接线。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-71 | Architecture | 在 break filter 相关语义继续扩展前先修正能力协商，避免客户端继续基于空壳能力做错误决策 |

---

## 发现与方案 (2026-04-09 00:27)

### Issue-72：`GoToDefinition` 协议只有请求没有结果通道，服务端把“跳转定义”错误实现成宿主进程本地副作用

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h` |
| 行号 | `AngelscriptDebugServer.h:25-80, 453-463`；`AngelscriptDebugServer.cpp:1130-1136, 1288-1375`；`AngelscriptDebuggerTestClient.h:68-80` |
| 问题 | 协议枚举里只有单向的 `EDebugMessageType::GoToDefinition`，没有任何配套 response/event 类型（`25-80`）；消息体 `FAngelscriptGoToDefinition` 也只包含请求参数 `TypeName/SymbolName`（`453-463`）。服务端收到请求后只是反序列化参数并直接调用 `GoToDefinition(GoTo)`（`1130-1136`），而具体实现会在宿主 Unreal 进程里调用 `FSourceCodeNavigation::NavigateToFunction/Property/Class(...)`（`1288-1375`）。测试客户端头文件同样只暴露了 `StartDebugging/Continue/Step/CallStack/Variables/Evaluate/Breakpoint` 等 helper，没有任何 `SendGoToDefinition` 或等待定义结果的 API（`68-80`）。这说明当前设计根本不是“把定义位置返回给远端调试器”，而是“远端发一个 RPC，命令服务端本地 IDE 自己跳转”。 |
| 根因 | DebugServer 把一个本应遵守 request/response 语义的查询型协议，误建模成了 editor-only side effect，缺少可序列化的定义位置结果和与请求关联的返回通道。 |
| 影响 | 远端客户端、DAP 适配层和自动化测试都无法可靠消费 `GoToDefinition`：客户端既拿不到文件/行号，也无法区分“未找到定义”“找到但无法打开”“成功跳转”。在 headless、非 editor、远程调试或多客户端场景下，这条消息退化成 silent no-op 或错误地影响服务端本地 IDE，会直接阻断标准 DAP `gotoTargets/definition` 一类能力接入。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `GoToDefinition` 改成真正的查询协议：服务端解析符号并回传可序列化的位置结果，本地 IDE 跳转只保留为可选附加行为。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 新增结果消息类型，例如 `EDebugMessageType::GoToDefinitionResult`，负载至少包含 `RequestId`、`bFound`、`Filename`、`LineNumber`、`Column`、`DisplayName` 或等价字段；若当前还没落地统一 request correlation，至少先给这条消息单独加 `RequestId`，不要再让客户端只能靠 FIFO 猜对应关系。 2. 把 `GoToDefinition(const FAngelscriptGoToDefinition&)` 重构成纯解析 helper，例如 `bool TryResolveDefinition(const FAngelscriptGoToDefinition&, FResolvedDefinition& OutResult)`；内部继续复用现有函数/属性/类型查找逻辑，但不再直接调用 `FSourceCodeNavigation`。 3. `HandleMessage(EDebugMessageType::GoToDefinition)` 改为“解析 -> 回包”：找到定义就向请求方发 `GoToDefinitionResult`，未找到也要明确回 `bFound=false` 和错误文本；不允许 silent no-op。 4. 若仍需保留当前 editor 本地跳转行为，应把它变成明确的可选后处理，例如仅在本地工具模式或 capability flag 打开时额外调用 `FSourceCodeNavigation`；远端 debug session 默认只接收结果，不应再劫持宿主进程 UI。 5. 扩展测试客户端 API，增加 `SendGoToDefinition(...)` 与 `WaitForTypedMessage<FAngelscriptGoToDefinitionResult>(...)`，让自动化可以验证请求/响应闭环，而不是只能观察宿主进程副作用。 6. 与已记录的 `Issue-42` request correlation 统一设计，避免先给 `GoToDefinition` 单独发明一套 request id，再在全协议升级时重复改 wire format。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.h`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接把现有 editor-side navigation 删除而不提供兼容层，当前本地工具链可能丢失“请求后直接打开 IDE”的便利行为；因此最好把本地跳转降级成可选模式，而不是一次性砍掉。 |
| 前置依赖 | 最好与 request correlation 升级协同，但可以先以单消息局部 `RequestId` 形式落地最小闭环。 |
| 验证方式 | 1. 新增 protocol 测试，验证 `GoToDefinition` 请求会收到结构化结果，而不是只触发本地副作用。 2. 在 editor 环境下验证“本地跳转模式”打开时仍可自动导航，同时远端客户端也能收到同一份解析结果。 3. 在 non-editor/headless 测试环境下发起请求，确认服务端不会再静默吞掉消息，而是稳定返回 `bFound=false` 或结构化位置。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-72 | Architecture | 在扩展 DAP 请求关联与 editor 辅助协议时一并处理，先把定义跳转从本地副作用改成可返回结果的查询协议 |

---

## 发现与方案 (2026-04-09 00:27)

### Issue-73：现有 `StaticJIT` 自动化没有任何一条真正创建或执行 JIT 运行态，`DebuggingAndJIT` 缺陷几乎没有测试护栏

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h` |
| 行号 | `AngelscriptPrecompiledDataTests.cpp:13-21, 23-27, 67-71`；`AngelscriptEngine.cpp:859-920, 1425-1456`；`AngelscriptEngine.h:64-80`；`StaticJITConfig.h:8-10` |
| 问题 | 当前仓库里带 `Angelscript.CppTests.StaticJIT.*` 名字的自动化只有 `AngelscriptPrecompiledDataTests.cpp` 两条（`13-21`），而它们都在 `EditorContext` 下运行，并且都使用默认构造的 `FAngelscriptEngineConfig Config;` + `CreateForTesting(..., Clone)`（`23-27`, `67-71`）。默认 config 里 `bGeneratePrecompiledData=false`、`bIsEditor=false`（`AngelscriptEngine.h:64-80`）；更关键的是，测试专用初始化 `InitializeForTesting()` 会硬编码 `bUsePrecompiledData=false`，且从头到尾既不创建 `PrecompiledData`/`StaticJIT`，也不执行 `Engine->SetJITCompiler(StaticJIT)`（`859-920`）。相比之下，真正的运行时初始化只有在 `bGeneratePrecompiledData` 路径里才会构造 `StaticJIT`、关闭 line cues 并把 JIT compiler 装到脚本引擎上（`1425-1446`）。再加上 editor 构建里 `StaticJITConfig.h` 直接定义 `AS_SKIP_JITTED_CODE`（`8-10`），即便生成了 transpiled bundle，editor 测试二进制也不会执行真实 JIT 函数体。结果是现在所谓的 `StaticJIT` 自动化实际上只覆盖 precompiled data 结构体 round-trip/diff，不覆盖真实 codegen、fallback、JIT 执行或调试协议联动。 |
| 根因 | 测试 harness 复用了“轻量测试引擎 + EditorContext”路径，但这条路径在设计上就绕开了 `StaticJIT` 构造、precompiled runtime 初始化和真实 JIT 执行；测试命名与实际覆盖范围发生了结构性错位。 |
| 影响 | 当前 `DebuggingAndJIT` 子系统里的高风险缺陷，例如 JIT/VM 结果不一致、异常与调试协议分叉、virtual/imported fallback 错误、真实 transpiled bundle 装载问题，都无法通过现有自动化提前暴露。开发者看到 `StaticJIT` 测试通过，实际上只证明了序列化数据对象没坏，几乎不能说明运行时 JIT 是对的。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 新建一条“真实 JIT 运行态”测试通路，明确区分 `PrecompiledData` 结构测试与 `StaticJIT` 执行测试，避免继续让 EditorContext 伪装成 JIT 覆盖。 |
| 具体步骤 | 1. 把现有 `AngelscriptPrecompiledDataTests.cpp` 明确降级为“precompiled data structure tests”，保留 round-trip/diff 责任，但不要再把它们当成 JIT runtime 护栏；必要时重命名测试路径或注释，降低误导性。 2. 在 runtime/tests 或 `AngelscriptTest/Debugger` 下新增专门的 JIT harness：它必须走与真实运行态一致的初始化分支，显式创建 `PrecompiledData`、`StaticJIT`，并调用 `Engine->SetJITCompiler(StaticJIT)`；不能继续复用当前 `InitializeForTesting()` 的轻量路径。 3. 这条 harness 需要分成两层：a) 生成层，验证目标脚本函数能成功进入 transpiled bundle；b) 执行层，在非 `AS_SKIP_JITTED_CODE` 的测试 target 上实际运行已注册到 `FJITDatabase` 的函数，并与解释执行结果逐项比对。 4. 为 editor 默认自动化之外再提供一个专用 test target/commandlet/workflow，例如 non-editor Development/Test 目标，专门运行 JIT 集成测试；否则 `AS_SKIP_JITTED_CODE` 会继续让 editor CI 通过但真实 JIT 仍无人覆盖。 5. 优先把已知高风险路径做成回归矩阵：virtual dispatch fallback、imported rebind、JIT exception -> DebugServer、`DebugBreak/ensure/check`、以及至少一条“解释器与 JIT 返回值一致”的算术/对象访问脚本。 6. 在测试报告或命名上明确区分三种状态：`PrecompiledData-Only`、`StaticJIT-Codegen`、`StaticJIT-Executed`；没有跑到最后一级时，不允许再对外声称“JIT tests passed”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITConfig.h`、`Plugins/Angelscript/Source/AngelscriptTest/Debugger/` 或 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增 JIT 集成测试与 harness 文件、对应测试 target/运行脚本 |
| 预估工作量 | L |
| 风险 | 新引入 non-editor/JIT 测试 target 会增加 CI 成本和环境复杂度；如果没有把测试产物生成、bundle 装载和运行时执行三层边界拆清楚，容易把 harness 自身问题误判成 JIT 缺陷。 |
| 前置依赖 | 最好与现有 `Issue-79`/JIT 调试运行态互斥问题一并规划，确保测试 target 既能执行 JIT，又能在需要时连上 DebugServer。 |
| 验证方式 | 1. 新增一条最小 JIT 执行测试，确认它在 non-editor target 下实际命中 `FJITDatabase` 注册函数，而不是回到解释器。 2. 用同一脚本分别跑解释器与 JIT，断言返回值、异常和源码定位一致。 3. CI/本地报告中明确标出 `StaticJIT-Executed` 阶段已运行，防止再次被 EditorContext 结构测试冒充。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-73 | Architecture | 在继续修补 JIT 运行时行为之前先建立真实执行护栏，否则后续修复都无法被自动化证明 |

---

## 发现与方案 (2026-04-09 00:40)

### Issue-74：补足 Analysis 发现 47 的修复方案：`FStaticJITCompiledInfo` 把 transpiled bundle 锁死为进程级单例

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `StaticJITHeader.cpp:19-35`；`AngelscriptStaticJIT.cpp:3683-3693`；`AngelscriptEngine.cpp:1550-1556` |
| 问题 | `GetActiveCompiledInfo()` 只保留一个进程级 `ActiveInfo`，而 `FStaticJITCompiledInfo` 构造函数会在第二次注册时直接 `checkf(ActiveInfo == nullptr, "Only one angelscript static JIT info can be compiled in!")`。与此同时，`WriteOutputCode()` 每次生成 transpiled 产物都会额外写出 `AngelscriptJitInfo.jit.cpp`，强制链接一个静态 `JitInfo(FGuid(...))`；运行时又只会通过 `FStaticJITCompiledInfo::Get()` 读回这一份 GUID，并在 `AngelscriptEngine.cpp:1550-1556` 与当前 `PrecompiledData->DataGuid` 做单次比较。源码没有任何“枚举多个 compiled bundle”“按 GUID 选中其中一份”“不匹配 bundle 静默失活”的路径。 |
| 根因 | bundle 元数据和激活逻辑都被建模成进程级单例：编译期只能注册一个 `FStaticJITCompiledInfo`，运行期也只认识一个全局 `PrecompiledDataGuid`。 |
| 影响 | 这会把 `StaticJIT` 的扩展边界直接卡死在“一进程一份 transpiled bundle”。一旦未来把 Angelscript plugin 拆成多来源 bundle、按模块分片生成、或与其它宿主共享进程，结果不是按 GUID 逐 bundle fallback，而是在静态初始化期直接断言。当前 fallback 机制只覆盖“单 bundle 不匹配”，不覆盖“存在多 bundle”这一更现实的交付演进场景。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 compiled bundle 元数据从进程级单例升级为可枚举 registry，运行时按 `PrecompiledData->DataGuid` 精确激活匹配 bundle，其余 bundle 保持休眠而不是断言。 |
| 具体步骤 | 1. 在 `StaticJITHeader.cpp/.h` 用拥有明确查询接口的 registry 替换 `GetActiveCompiledInfo()` 单例，例如 `TMap<FGuid, const FStaticJITCompiledInfo*>` 或只读数组注册表；`FStaticJITCompiledInfo` 构造时改为“注册到 registry”，不再对第二份 bundle `checkf`。 2. 让 `AngelscriptJitInfo.jit.cpp` 生成的静态对象除了 `PrecompiledDataGuid` 外，再携带 bundle 名称/来源标识或最小索引信息，方便日志和冲突诊断；不要继续把 bundle identity 压缩成唯一活动指针。 3. 修改 `AngelscriptEngine.cpp:1550-1556` 的激活路径：运行时应通过 `PrecompiledData->DataGuid` 在 registry 中查找匹配 bundle，只激活对应 bundle 的 compiled entries；查不到时禁用当前 bundle 的 transpiled code 并记录 warning，而不是影响其它 bundle。 4. 审计 `FJITDatabase` 与 `FStaticJITFunction` 的注册方式，必要时为 `Functions` / lookup 表补 `BundleGuid` 维度，避免多个 bundle 的 `FunctionId`、type/property lookup 混入同一无标签全局表。 5. 为旧的单 bundle 行为保留兼容路径：当 registry 里只有一份 bundle 时，现有加载与匹配逻辑应保持结果不变，避免把架构修复变成单 bundle 回归。 6. 增加两类验证：a) 静态/单元层验证同一进程可注册两份不同 `FStaticJITCompiledInfo` 而不触发断言；b) 运行时验证加载 bundle A 的 cache 时只激活 A，对 bundle B 保持休眠，反之亦然。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增 multi-bundle JIT 激活测试 |
| 预估工作量 | L |
| 风险 | 如果只放开 `FStaticJITCompiledInfo` 单例而不同时给 `FJITDatabase` 增加 bundle 维度，多个 bundle 仍可能在全局 lookup 表里互相污染。修复必须把“元数据注册”和“函数/lookup 激活”一起设计。 |
| 前置依赖 | 建议与已记录的 `Issue-24` 一并评审，确保 `FJITDatabase` 的 process-lifetime registry 改造不会再次假设“只存在一份 compiled bundle”。 |
| 验证方式 | 1. 新增多 bundle 注册测试，确认第二份 `FStaticJITCompiledInfo` 不再触发断言。 2. 新增按 `DataGuid` 选择 bundle 的运行时测试，确认匹配 bundle 可用、不匹配 bundle 被安全禁用。 3. 回归现有单 bundle precompiled/JIT 路径，确认正常项目不会因 registry 化改造失去现有功能。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-74 | Architecture | 在继续推进多来源 JIT 交付前先解除单 bundle 单例限制，避免第二份 transpiled 产物直接触发静态初始化断言 |

---

## 发现与方案 (2026-04-09 00:41)

### Issue-75：补足 Analysis 发现 65 的修复方案：fully precompiled 模式下 `ReplaceScriptAssetContent()` 会解引用空 `DebugServer`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp` |
| 行号 | `AngelscriptEngine.cpp:1452-1456, 2017-2035`；`AngelscriptEditorModule.cpp:331` |
| 问题 | 引擎只在 `(!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName()` 时创建 `DebugServer`，因此 fully precompiled 且非 development mode 下 `DebugServer` 会保持空指针。`HasAnyDebugServerClients()` 已经显式为这种情况写了 `if (DebugServer == nullptr) return false;`，说明运行时允许 server 缺席。但 `ReplaceScriptAssetContent()` 在同一文件里却没有沿用这条约束：它在 `WITH_AS_DEBUGSERVER` 分支下无条件构造 `FAngelscriptReplaceAssetDefinition` 并直接调用 `DebugServer->SendMessageToAll(...)`。编辑器侧 `OnLiteralAssetSaved()` 又会在保存 literal asset 后直接进入这条路径。 |
| 根因 | `ReplaceAssetDefinition` 通知链把“编译期启用了 DebugServer 宏”错误等同成“运行时一定存在 `DebugServer` 实例”，没有和 fully precompiled 模式下的 server 创建条件保持一致。 |
| 影响 | 这会把 fully precompiled 模式下原本只是“没有调试推送”的功能降级，升级成保存脚本字面量资源时的确定性空指针崩溃。也就是说，当前问题不是“这条调试通知失效”，而是“保存一个资源就可能直接终止编辑器/宿主进程”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `ReplaceAssetDefinition` 广播改成显式的可选调试能力：运行时没有 `DebugServer` 或没有调试客户端时必须安全 no-op，而不是直接解引用。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.cpp` 为这条通知提取统一 helper，例如 `BroadcastReplaceAssetDefinitionIfAvailable(...)`；入口第一行就检查 `DebugServer != nullptr`，并优先复用 `HasAnyDebugServerClients()` 或等价逻辑，缺席时直接返回。 2. `ReplaceScriptAssetContent()` 改为只通过该 helper 广播，禁止直接访问 `DebugServer->SendMessageToAll(...)`。这样即使未来又新增其它 asset-definition 推送入口，也会自动继承同一套判空语义。 3. 若当前产品要求“没有调试客户端时也记录一次状态”，应把这份状态落到 engine-owned cache，而不是继续依赖 `DebugServer` 作为唯一通道；调试连接建立后再按需补发。 4. `AngelscriptEditorModule.cpp` 保持继续调用 `ReplaceScriptAssetContent()`，不要把判空逻辑散落回 editor 调用方；运行时 core 应自己保证这条 API 在任何配置下都安全。 5. 与已记录的 `Issue-15` 协同时，把“precompiled 模式可否创建 DebugServer”和“没有 DebugServer 时 API 必须安全 no-op”分开处理：即便未来允许 precompiled runtime 打开调试服务器，这里的空指针护栏仍应保留。 6. 增加回归：构造 `DebugServer == nullptr` 的 engine 配置，直接调用 `ReplaceScriptAssetContent()`，断言不会崩溃；再补一条有 mock/fake debug server 的正向测试，确认有连接时仍会发送 `ReplaceAssetDefinition`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`（若新增 helper 声明）、`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptEditorModule.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/` 下新增 asset-definition 广播回归 |
| 预估工作量 | S |
| 风险 | 如果只在 editor 调用点加判空，而不在 runtime core API 里集中收口，后续新增调用方仍会再次踩中同一空指针问题。修复应放在 `FAngelscriptEngine` 这一层。 |
| 前置依赖 | 无；这是独立的空指针热修。 |
| 验证方式 | 1. 新增无 `DebugServer` 配置下调用 `ReplaceScriptAssetContent()` 的回归，确认不会崩溃。 2. 新增有 mock/fake debug server 的正向测试，确认 `ReplaceAssetDefinition` 仍能正常广播。 3. 人工在 fully precompiled 配置下保存一个 literal asset，确认流程只是不再发送调试通知，而不是触发崩溃。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-75 | Defect | 作为 fully precompiled 路径的热修尽快处理，先消除保存 literal asset 时的确定性空指针崩溃 |

---

## 发现与方案 (2026-04-09 00:42)

### Issue-76：补足 Analysis 发现 61 的修复方案：fully precompiled 模式下的 `CodeHash` 校验是在拿 cache 对比 cache

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | `AngelscriptEngine.cpp:2046-2052, 4283-4303`；`PrecompiledData.cpp:1417-1421, 2758-2776`；`AngelscriptPreprocessor.cpp:289-301` |
| 问题 | fully precompiled 路径下，`InitialCompile()` 在 `PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode` 时直接把 `ModulesToCompile` 替换成 `PrecompiledData->GetModulesToCompile()`。而 `GetModulesToCompile()` 重建 `FAngelscriptModuleDesc` 时，会把 cache 里保存的 `Module.CodeHash` 原样写回 `ModuleDesc->CodeHash`。后续 `CompileModule_Properties_Stage1()` 虽然在 `4289-4290` 写着“Check if file content hashes are the same or not”，实际比较的是 `CompiledModule->CodeHash == Module->CodeHash`。这两个值在 fully precompiled 分支里都来自同一份 cache。相比之下，真正基于当前源码计算 `CodeHash` 的逻辑只出现在 preprocessor：它会对 `File.ProcessedCode` 做 `XXH64`，再异或进 `File.Module->CodeHash`。 |
| 根因 | precompiled data 不只缓存 bytecode，还缓存并回放了 preprocessor 产出的 module descriptor；但 fully precompiled 分支仍沿用了一个看似验证“当前脚本文件”的 `CodeHash` guard，实际上没有任何磁盘源码或当前处理结果参与输入。 |
| 影响 | 这会让旧 `PrecompiledScript*.Cache` 在 fully precompiled 模式下获得错误的“已通过源码 hash 校验”背书。只要外层 `BuildIdentifier` 没把它拦下，源码已经变化、删除或新增的情况都可能继续实例化旧 cache，而日志还会暗示系统已经核对过当前文件内容。这不仅削弱 stale-cache 诊断，还会让后续 JIT/VM 行为分叉更难被定位。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 fully precompiled 模式下的内容真实性校验改成两段式：有源码时重算真实 source hash，无源码时显式承认“无法做源码比对”，移除当前自我比较式 guard。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.cpp` 新增独立验证入口，例如 `ValidatePrecompiledModuleSources(...)`，不要再复用 `CompiledModule->CodeHash == Module->CodeHash` 这条自我比较。该入口应在 `PrecompiledData->GetModulesToCompile()` 之后、真正 `ApplyToModule_Stage1()` 之前执行。 2. 当脚本源文件在当前机器上可访问时，按 `PrecompiledData` 保存的 module/section 清单重新读取源码，并复用 `AngelscriptPreprocessor.cpp:289-301` 的同一 hash 规则计算 fresh `CodeHash`；只有 fresh hash 与 cache 中的 `CompiledModule->CodeHash` 一致，才允许激活该 module 的 precompiled/JIT 产物。 3. 当运行环境本来就不携带源码时，不要继续保留这条假验证。应显式记录 `bSourceHashValidationAvailable=false` 或等价状态，并在日志中说明“当前仅依赖 build/cache identity，不做源码内容校验”；避免把无验证状态伪装成已验证。 4. 若完全重跑 preprocessor 成本过高，可先落一个轻量模式：只根据 cache 中已有的 section 路径和原始文本 hash 重新读文件并求哈希，不做完整类型展开；关键是 fresh hash 必须来自当前文件系统，而不是再次回放 cache。 5. 将当前 `4289-4290` 的 misleading 注释和比较逻辑一起改掉。若短期内无法实现真实 source-backed validation，就直接删掉这条比较，并把诊断信息改成“fully precompiled 模式未做源码比对”，不要继续输出误导性语义。 6. 补两组回归：a) 源码可访问时，修改一个脚本文件但保留旧 cache，验证 fully precompiled 模式会拒绝该 module/cache；b) 源码不可访问的 packaged-like 场景，验证系统会明确报告“跳过源码校验”，而不是谎称 hash 已匹配。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` 或新增 fully-precompiled source-validation 测试 |
| 预估工作量 | M |
| 风险 | 如果直接在 fully precompiled 启动路径里重跑完整 preprocessor，可能抵消当前模式的启动收益；因此需要把“真实性校验”限制为最小必要的源文件重哈希，而不是无差别重编译。另一方面，若 packaged runtime 本就不带源码，则必须清晰区分“无法校验”和“校验通过”，不能继续混淆。 |
| 前置依赖 | 无；但建议与已记录的 `Issue-39` 一起审视 precompiled cache 激活前的整体验证链，避免一边修 build-identifier gate、一边保留这条自我比较。 |
| 验证方式 | 1. 新增 stale-cache 回归：修改脚本源码后保留旧 cache，确认 fully precompiled 模式不再因为 cache-vs-cache 比较而误通过。 2. 新增源码缺席场景测试，确认系统会明确标记“未做源码校验”。 3. 回归合法 cache 场景，确认真实 source-backed hash 一致时仍能正常激活 precompiled/JIT 产物。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-76 | Defect | 在继续依赖 fully precompiled cache 作为交付形态前先修正这条伪验证，避免旧 cache 被误当成已核对的当前源码 |

---

## 发现与方案 (2026-04-09 01:03)

### Issue-77：DebugServer V2 收包循环在 partial `Recv()` 时总是从缓冲区起始地址重写，TCP 半包会直接破坏消息帧

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h` |
| 行号 | `AngelscriptDebugServer.cpp:764-788, 791-794`；`AngelscriptDebugServer.cpp:64-109` |
| 问题 | `ProcessMessages()` 读取包头和包体时，循环里都调用 `Client->Recv(Datagram->GetData(), Datagram->Num() - BytesReceived, BytesRead)`。`BytesReceived` 虽然在递增，但写入指针始终是 `Datagram->GetData()`，没有偏移到 `+ BytesReceived`。这意味着只要 TCP 把一个 4-byte header 或 payload 拆成多次 `Recv()`，后续分片就会覆盖前面已经收到的字节，最终 `PacketSize`、`MessageType` 和 body 内容都会被拼坏。与之相对，同文件开头已经存在经过测试的 `TryDeserializeDebugMessageEnvelope()`，它会按偏移读取 header/payload，并在消费完成后从缓冲区头部移除完整 envelope。 |
| 根因 | live socket 收包路径手写了第二套 framing，但实现时只累计了“已接收字节数”，没有把这个偏移真正用于目标缓冲区地址，导致 partial-read 场景下发生覆盖写。 |
| 影响 | 调试客户端只要触发正常的 TCP 半包传输，就可能让服务端把合法消息解码成错误的 `PacketSize` 或错误的 `MessageType`，轻则静默丢包，重则误入错误分支、提前断开连接，或者在读取错误 body 时把调试状态推入未定义路径。这不是恶意输入才会触发的问题，而是所有真实网络环境都可能出现的基础协议正确性缺陷。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 删除这套手写的双循环收包逻辑，统一复用已有 envelope parser；若短期无法重构，至少先修正 `Recv()` 的写入偏移。 |
| 具体步骤 | 1. 在 `FAngelscriptDebugServer` 为每个 `FSocket*` 增加持久化 receive buffer，例如 `TMap<FSocket*, TArray<uint8>> PendingReceives`，每次 `Recv()` 都把新字节 append 到 buffer 尾部，而不是尝试一次拼完整包。 2. 在 `ProcessMessages()` 中改成“读尽当前 socket 可用字节 -> 反复调用 `TryDeserializeDebugMessageEnvelope()` 取出完整 envelope -> 对每个 envelope 调 `HandleMessage()`”的模式，直接复用 `AngelscriptDebugServer.cpp:64-109` 已有的长度校验、partial 保留和错误报告逻辑。 3. 如果需要热修而不是一次性重构，至少先把两处 `Client->Recv(...)` 改成写入 `Datagram->GetData() + BytesReceived`，并同时检查 `Recv()` 返回值/`BytesRead`，避免覆盖写和零进展死循环叠加。 4. 当 `TryDeserializeDebugMessageEnvelope()` 返回 invalid length 等错误时，记录具体客户端与错误文本，并主动断开该连接，避免污染后续消息流。 5. 增加 transport 集成测试，显式把同一 envelope 切成 `2+2` header、`1+n` payload、多个 envelope 黏包三类输入，验证服务端都能正确恢复出消息序列。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只修写入偏移而不统一到 envelope parser，live path 与测试 path 仍会继续双轨演进，后续 framing 修复还会再次分叉。 |
| 前置依赖 | 无；这是 DebugServer V2 协议正确性的基础热修。 |
| 验证方式 | 1. 新增 partial-read 集成测试，验证分片 header/body 不再破坏 `PacketSize` 与 `MessageType`。 2. 回归现有 `AngelscriptDebugTransportTests`，确认单包、截断包和非法长度包行为不变。 3. 本地用 test client 按字节切分发送 `StartDebugging`、`SetBreakpoint` 等消息，确认服务端能稳定解析。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-77 | Defect | 立即修复，先恢复 DebugServer V2 在真实 TCP 半包场景下的基础协议正确性 |

---

## 发现与方案 (2026-04-09 00:57)

### Issue-78：`ScriptCallNative` / `CallFunctionCaller` 用固定 `FunctionArgs[32]` 封送 native 参数，effective arg count 可直接越界写栈

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp` |
| 行号 | `StaticJITHeader.cpp:213-299`；`as_context.cpp:5160-5264`；`as_scriptfunction.cpp:647-675, 1463-1465` |
| 问题 | `FStaticJITFunction::ScriptCallNative()` 和解释器 `asCContext::CallFunctionCaller()` 都在栈上分配了固定 `void* FunctionArgs[32]`，随后让 `ArgIndex` 按 `this` 指针、`passFirstParamMetaData`、每个参数各增长一次，遇到 `ttQuestion` 参数还会额外再增长一次（`StaticJITHeader.cpp:223-244, 267-287`；`as_context.cpp:5185-5212, 5234-5255`）。整个过程中没有任何 `ArgIndex < 32` 或 `RequiredSlots <= 32` 校验。与此同时，参数描述是按 `parameterTypes.GetLength()` 动态生成和暴露的，`CalculateParameterOffsets()` 只根据参数列表长度计算偏移，`GetParamCount()` 也直接返回 `parameterTypes.GetLength()`（`as_scriptfunction.cpp:647-675, 1463-1465`），当前这条调用桥并没有自己的 32 槽上限契约。结果是只要某个 native/system function 的 effective arg slot 数超过 32，这两个 bridge 都会把 `FunctionArgs` 写出边界。 |
| 根因 | native bridge 把参数封送实现成了两份拷贝代码，并共享了一个没有来源说明的固定 32 槽局部数组；实现里既没有前置容量计算，也没有在写入时做边界保护，导致“描述层允许的参数规模”和“桥接层可承受的参数规模”完全脱节。 |
| 影响 | 这是明确的内存安全缺陷，不是单纯的语义不一致。触发条件并不限于“32 个脚本参数”，因为 instance method、`passFirstParamMetaData` 和 `ttQuestion` 都会继续消耗 slot；一旦越界，最直接的后果就是栈内存破坏，轻则把后续 native 调用参数封送错位，重则覆写返回地址附近的局部变量并导致崩溃。更糟的是，解释器路径和 JIT native fallback 路径都复制了同样的问题，所以无论是否启用 StaticJIT，都缺少安全护栏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把 effective arg slot 计算显式化并在两条调用桥上做 fail-fast，再把固定数组替换成共享的动态参数缓冲构建逻辑，彻底移除 32 槽隐式假设。 |
| 具体步骤 | 1. 在 `as_context.cpp` 附近提取共享 helper，例如 `static int CountNativeCallArgSlots(asCScriptFunction* Descr, asSSystemFunctionInterface* SysFunc)`，明确计算 `this`、metadata、普通参数和 `ttQuestion` 额外槽位，总数定义为 `RequiredSlots`。 2. 在 `asCContext::CallFunctionCaller()` 与 `FStaticJITFunction::ScriptCallNative()` 的入口都先调用该 helper；短期热修至少要在 `RequiredSlots > 32` 时立刻走脚本异常路径，解释器侧用 `SetInternalException(...)`，JIT 侧用 `SetException(...)` 并返回，禁止继续写栈。 3. 随后把两处 `void* FunctionArgs[32]` 改为按 `RequiredSlots` 分配的连续缓冲，并抽出共享的 `BuildNativeCallArgs(...)` helper 统一填充顺序，避免今后只改一侧又重新漂移。若第三方层不适合直接引入 UE 容器，就使用 AngelScript 自身容器或 `TArray<void*, TInlineAllocator<32>>` 的等价实现，但必须保证 `Num()==RequiredSlots` 后再写入。 4. 为避免把“新上限”再次藏成魔法数字，在 helper 注释里明确写出 slot 组成公式，并在 debug build 加 `checkf(WriteIndex < RequiredSlots, ...)` 保护每一次写入。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Native/` 新增一组 native call bridge 边界回归，例如注册一个高参数量的 global/native function，以及一个带 `this` 或 metadata 的 method，验证解释器执行与 `SCRIPT_CALL_NATIVE` fallback 都不会崩溃；在热修阶段应断言得到明确脚本异常，在完成动态缓冲改造后再把期望升级为成功调用。 6. 复跑现有 `Native`/`ASSDK` 调用约定测试，确认常见 `thiscall`、reference、`ttQuestion` 路径没有因为共享 helper 改写而发生 ABI 回归。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Native/` 下新增 native bridge 边界测试 |
| 预估工作量 | M |
| 风险 | 如果只在单侧加检查而不抽共享 helper，解释器与 StaticJIT fallback 很快会再次分叉；如果直接改成动态缓冲但没有补足 `this` / metadata / `ttQuestion` 的顺序测试，也可能引入更隐蔽的 ABI 错位。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增自动化用例，构造 `RequiredSlots == 32` 和 `RequiredSlots > 32` 两组 native call，验证前者成功、后者在热修阶段返回明确脚本异常而不是崩溃。 2. 对同一组函数同时走解释器执行和 `SCRIPT_CALL_NATIVE` fallback，确认两条路径对超限行为的报错文本和中止时机一致。 3. 回归 `Plugins/Angelscript/Source/AngelscriptTest/Native/AngelscriptASSDKCallingConvTests.cpp` 与相关 `NativeExecution` 用例，确认常规调用约定未退化。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-78 | Defect | 在继续扩展 native/system function 绑定面之前先修复，避免高参数量调用把解释器和 JIT fallback 一起拖入栈破坏 |

---

## 发现与方案 (2026-04-09 01:13)

### Issue-79：补足 Analysis 发现 30 的修复方案：`check(false); return true;` 会把未实现 bytecode 静默伪装成“已成功生成”

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`J:/UnrealEngine/UEAS2/Engine/Source/Runtime/Core/Public/Misc/AssertionMacros.h` |
| 行号 | `AngelscriptBytecodes.cpp:3969-3977, 6398-6431`；`AngelscriptStaticJIT.cpp:3333-3336, 3492-3497`；`AssertionMacros.h:224-232, 312-314` |
| 问题 | `asBC_DestructScript`、`asBC_SetListSize`、`asBC_PshListElmnt`、`asBC_SetListType` 这四个 handler 都直接写成 `check(false); return true;`。`GenerateCppCode()` 对每条 bytecode 的唯一契约是 `bool bImplemented = Bytecode.Implement(Context); check(bImplemented);`，随后继续生成整函数；而 `WriteOutputCode()` 又会对 `FunctionsToGenerate` 中的每个函数无条件执行 analyze + generate。也就是说，这几条 bytecode 在 `DO_CHECK=1` 时会靠断言中止，但在 `DO_CHECK=0` 时，`check(expr)` 会退化成 `CA_ASSUME(expr)`，handler 仍返回 `true`，JIT 管线会把“没有生成任何等价语义”的分支当成成功实现。 |
| 根因 | StaticJIT 把“未实现 bytecode”的失败语义托付给 `check`，却没有建立独立于断言宏的 capability/fallback 协议；更糟的是，部分 handler 还把失败路径写成了“断言后返回成功”。 |
| 影响 | 在非 `DO_CHECK` 构建里，这不是安全失败，而是静默错误代码生成。只要脚本函数包含上述 bytecode，JIT 产物就可能与 VM 解释结果不一致，表现为列表初始化、脚本对象析构等语义被直接跳过，属于会污染运行结果的确定性 miscompile。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“unsupported bytecode”从断言语义改成显式 capability/fallback 语义：handler 只能返回“已实现”或“要求整函数回退 VM”，不能再出现伪成功。 |
| 具体步骤 | 1. 先在 `AngelscriptBytecodes.cpp` 清理这类伪成功 handler：`asBC_DestructScript`、`asBC_SetListSize`、`asBC_PshListElmnt`、`asBC_SetListType` 不得再 `return true`；短期热修应统一改成显式 `return false`，并补注释说明“需要整函数回退 VM”，不要继续依赖 `check(false)` 表达失败。 2. 在 `AngelscriptStaticJIT.cpp` 建立函数级 fallback 协议。例如为 `FGenerateFunction` 增加 `bRequiresVMFallback`/`FirstUnsupportedBytecode` 字段；当任一 bytecode `Implement()` 返回 `false` 时，立刻标记当前函数不生成 JIT entry，停止继续产出半成品代码，并从最终 bundle 注册中排除该函数。 3. 将 `GenerateCppCode()` 的失败处理从“只断言”改成“先记录 unsupported 原因，再决定 debug build 是否 `checkf`”；即使保留开发期断言，也必须在逻辑上先保证 release/test 构建会走 VM fallback，而不是继续把函数当成成功生成。 4. 在 `WriteOutputCode()` 前增加轻量预扫描，或在 `AnalyzeScriptFunction()` 阶段汇总函数所含 unsupported bytecode，提前把整函数标记为 `NoJIT`，避免 codegen 过程写出一半后再撤销。 5. 为避免同类回归，统一审计 `AngelscriptBytecodes.cpp` 中所有 `check(false); return true;`/`check(false);` 组合，建立一个明确列表；没有真实语义实现的 handler 一律不得继续返回成功。 6. 新增回归测试，至少覆盖两层：a) codegen 层验证含 `SetListSize` 或 `DestructScript` 的函数不会注册 `jitFunction_Raw`，而是保留 VM bytecode；b) 运行层对比同一脚本在 JIT-enabled 构建下仍与解释执行结果一致，确认 fallback 生效而不是静默跳过语义。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` 或新增 `AngelscriptStaticJITCodegenTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只把 `return true` 改成 `return false`，但不补函数级 fallback，现有生成流程仍会在 `check(bImplemented)` 处中断，无法转化成稳定的 release/test 行为；修复必须把 handler 契约和函数级 fallback 一起收口。 |
| 前置依赖 | 无；这是 JIT/VM 语义一致性的基础修复。 |
| 验证方式 | 1. 新增包含 list initialization / script destruction 的脚本用例，确认开启 StaticJIT 后函数不会生成错误 `jitFunction_Raw`。 2. 在支持 JIT 执行的测试 target 上对比同一脚本的 JIT-enabled 结果与纯 VM 结果，确认返回值、副作用和析构时序一致。 3. 至少补一条自动化断言：unsupported bytecode 只会导致整函数回退 VM，不会再因为 `DO_CHECK` 宏差异而出现“开发版断言、发布版静默错编译”的分叉。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-79 | Defect | 立即修复，先堵住 unsupported bytecode 在非 `DO_CHECK` 构建下的静默错编译 |

---

## 发现与方案 (2026-04-09 01:16)

### Issue-80：补足 Analysis 发现 5 的修复方案：函数级 JIT miss 会被全局 `bStaticJITTranspiledCodeLoaded` 静默掩盖

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `PrecompiledData.cpp:527-540`；`AngelscriptEngine.h:143`；`AngelscriptEngine.cpp:77, 1591-1602`；`AngelscriptPrecompiledDataTests.cpp:13-20, 23-112` |
| 问题 | `FAngelscriptPrecompiledFunction::Create()` 对每个函数单独做 `FJITDatabase::Get().Functions.Find(Id)`；命中才写入 `jitFunction/jitFunction_ParmsEntry/jitFunction_Raw`，否则直接 `AllocateScriptFunctionData()` 回退到 VM bytecode。整个分支没有日志、计数器或结果对象去记录“这个函数本来走 precompiled/JIT 路径，但这次 miss 了”。与此相对，engine 侧唯一公开状态 `bStaticJITTranspiledCodeLoaded` 只是一个全局 bool，并且只按 `FJITDatabase::Get().Functions.Num() > 0` 赋值。只要数据库里存在任意一个 JIT entry，运行时就会宣称“已加载 transpiled code”，即便同一 bundle 里的其它函数正在批量静默回退 VM。当前 `AngelscriptPrecompiledDataTests.cpp` 也只有 2 条结构性测试，既不构造 hit/miss 混合场景，也不验证任何函数是否真的拿到了 `jitFunction_Raw`。 |
| 根因 | JIT 激活结果在实现上是“函数级查表、函数级 fallback”，但状态表达和测试口径却退化成“进程里是否存在任意 JIT function”。fallback 机制没有权威的 activation report，自然也没有可回归的观测点。 |
| 影响 | 这会让部分 bundle 丢函数、GUID 匹配但注册不全、或后续生成链路退化时全部变成 silent degradation：性能下降、JIT/VM 混合执行比例变化、以及已记录的 line-cue/debug fallback 问题都会被掩盖在一个仍然为 `true` 的全局 loaded 标志后面。结果不是安全可观测的 fallback，而是“功能看起来还能跑，但没人知道哪些函数已经掉回解释执行”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 JIT 激活从布尔状态升级为函数级 activation report，并让 engine 对外暴露“命中多少、miss 多少、为何 miss”的真实结果。 |
| 具体步骤 | 1. 在 `PrecompiledData.h/.cpp` 或 `AngelscriptEngine.h/.cpp` 定义显式 activation 结果模型，例如 `EStaticJITActivationResult { JITHit, MissingEntry, DevelopmentModeFallback }` 与 `FStaticJITActivationSummary { HitCount, MissCount, MissedFunctionIds... }`；禁止继续只靠 `bHasJITFunctions` 这种局部临时变量表达最终结果。 2. 修改 `FAngelscriptPrecompiledFunction::Create()`：每次函数创建时都向 summary/report 记录一次结果。命中时记录 `JITHit`；`Functions.Find(Id)` miss 或 development mode 强制 VM 时记录具体原因，然后再进入 `AllocateScriptFunctionData()`。 3. `FAngelscriptEngine` 在初始化尾声不要再把 `bStaticJITTranspiledCodeLoaded` 绑定到 `Functions.Num() > 0`。最小修复应改成“本轮实际 `JITHitCount > 0`”；更稳妥的是同时保留 summary 供日志、测试和调试 UI 查询。 4. 当 `HitCount > 0 && MissCount > 0`，或 `MissCount > 0` 且当前明确期望激活 transpiled bundle 时，启动日志必须打印汇总和首批 miss 的函数标识（至少 `FunctionId`/declaration），把 silent fallback 变成可诊断事件。 5. 若不希望长期暴露全量函数列表，可把详细 miss 列表挂在开发/测试构建，Shipping 至少保留计数和 bundle 级 warning；关键要求是不再把“部分函数 miss”伪装成完整 JIT loaded。 6. 新增自动化覆盖两层：a) 低层单测/回归直接构造“一个 `Id` 在 `FJITDatabase` 命中、另一个 miss”的 precompiled-function create 路径，断言 summary 正确累计 hit/miss，且 miss 函数只恢复 VM bytecode；b) engine 层测试验证 `bStaticJITTranspiledCodeLoaded` 与 `HitCount` 一致，而不是与全局 `Functions.Num()` 一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` 或新增 `AngelscriptStaticJITActivationTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只补日志而不建立权威 summary，后续测试仍然无法稳定断言行为；反过来，如果直接把 summary 做成 engine-global 单例，又会和 `Issue-74` 的多 engine 生命周期问题重新耦合，因此状态应明确归属当前 engine 初始化周期。 |
| 前置依赖 | 无，但建议与 `Issue-74` 的 `FJITDatabase` 生命周期修复协同推进，避免一边修状态表达、一边继续让多实例清空注册表污染结果。 |
| 验证方式 | 1. 新增 hit/miss 混合回归，确认 miss 函数会落到 VM，同时 activation summary 记录正确。 2. 回归当前完全命中 bundle，确认 `HitCount == 预期函数数` 时不产生误报。 3. 验证 `bStaticJITTranspiledCodeLoaded` 只在本轮实际至少一个函数命中 JIT 时为真，并在 `MissCount > 0` 时输出可追踪诊断，而不是继续静默。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-80 | Architecture | 在 `Issue-79` 后处理，先把 JIT fallback 切换结果做成可观测、可回归的权威状态 |

---

## 发现与方案 (2026-04-09 01:27)

### Issue-81：`asBC_ClrHi` 的 `float/double` JIT 分支漏写分号，命中时会直接生成非法 C++

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:6007-6037`；`as_context.cpp:3567-3584` |
| 问题 | `asBC_ClrHi::Implement()` 在 `FloatRegister` 和 `DoubleRegister` 两个分支里生成的是 `memcpy((void*)&l_byteRegister, (void*)&l_floatRegister, 1)` / `memcpy((void*)&l_byteRegister, (void*)&l_doubleRegister, 1)`，字符串末尾都缺少 `;`。同一个 handler 明确列出了 `FloatRegister` / `DoubleRegister` case，说明这不是死代码；而解释器 `asBC_ClrHi` 的职责是清掉 bool 高位脏字节，属于真实会进入 codegen 的 bytecode。结果是只要 JIT 生成路径在 `ClrHi` 时处于 float/double value register 状态，输出的 transpiled C++ 就会在这两行处直接语法错误。 |
| 根因 | `asBC_ClrHi` 仍采用手写字符串拼接生成 C++ 语句，但缺少最小的 codegen 语法回归；单个语句级 typo 可以一路带到后续 C++ 编译阶段才暴露。 |
| 影响 | 这不是运行时 fallback 问题，而是确定性的生成失败。任何命中该路径的脚本函数都会把整个 StaticJIT/transpiled 构建打断，表现为 C++ 编译报错，而不是安全回退到解释执行。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先修正文案级 codegen typo，再补一个专门覆盖 `asBC_ClrHi` register-state 分支的最小语法回归。 |
| 具体步骤 | 1. 在 `AngelscriptBytecodes.cpp:6021` 和 `6025` 把生成文本改成带分号的完整语句。 2. 顺手审计同文件里所有 `Context.Line("memcpy(...")` 和类似“函数调用语句直接手写字符串”的分支，优先把这类语句收成小 helper，例如 `EmitMemcpyByteTruncate(...)`，减少再次漏分号的表面积。 3. 在 StaticJIT 测试侧新增最小 codegen fixture，强制 `asBC_ClrHi` 分别在 `FloatRegister` 和 `DoubleRegister` 状态下生成输出，并断言文本包含合法的 `memcpy(...);` 语句。 4. 如果当前 harness 能跑生成后编译，追加一条真正的 compile smoke；否则至少对产出文本做字符串断言，避免同类 typo 再次潜伏到 C++ 编译阶段才暴露。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` 或新增 `AngelscriptStaticJITCodegenTests.cpp` |
| 预估工作量 | S |
| 风险 | 直接改两条字符串风险很低；真正的风险在于如果不补测试，后续其它手写 codegen 语句仍会以相同方式回归。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 `ClrHi` codegen 回归，覆盖 `FloatRegister` / `DoubleRegister` 两个分支。 2. 若可行，执行一次实际 transpiled C++ 编译 smoke，确认不再因 `memcpy` 语句缺分号失败。 3. 回归已有 StaticJIT/precompiled 测试，确认修复未改变运行语义。 |

### Issue-82：补足 Analysis 发现 29 的修复方案：`DebugDatabaseSettings` / `AssetDatabase` 写了 `Version` 字段却没有版本化解码

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `AngelscriptDebugServer.h:507-554`；`AngelscriptDebugServer.cpp:1493-1505, 2159-2205` |
| 问题 | `FAngelscriptAssetDatabase` 与 `FAngelscriptDebugDatabaseSettings` 都先把 `Version` 写进 wire，但读取时仍然无条件按“当前最新字段集合”继续反序列化，没有任何 `Version` 分支、`Ar.AtEnd()` 兼容逻辑或 unsupported-version 拒绝路径。相比之下，同一头文件里的 `FAngelscriptFindAssets` 已经展示了正确的兼容写法：它会在尾字段前检查 `Ar.AtEnd()`。而 `SendDebugDatabase()` / `SendAssetDatabase()` 又把这两条消息放在真实 debug database 握手路径上，因此这不是闲置结构体，而是每次数据库同步都会走到的协议面。 |
| 根因 | 协议设计已经意识到需要版本号，但实现层把 `Version` 当成注释式元数据，没有把它纳入解码决策，也没有建立统一的 versioned-message 模板。 |
| 影响 | 只要未来对这两条消息增删字段，旧客户端与新服务端就会在第一处结构变化后直接错位，而不是像 `StartDebugging` / `Breakpoint` / `FindAssets` 那样优雅跳过未知尾字段。数据库握手本身还是大流量初始化链路，一旦这里错位，后续 `DebugDatabase` / `AssetDatabase` 同步都可能建立在错误前提上。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把这两条消息改成真正的 versioned decode contract：显式读取版本、按版本解析已知前缀、对未知未来版本给出清晰失败信号。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.h` 为 `FAngelscriptAssetDatabase` 与 `FAngelscriptDebugDatabaseSettings` 重写 `operator<<`：先读 `Version` 到局部变量，再按 `switch (Version)` 或“`>= 最小版本` 逐步读取字段”的方式解码，而不是无条件消费全部字段。 2. 对未来版本号不要 silent misparse；若 `Version` 超出当前支持范围，应让 reader 进入明确错误路径，调用方据此断开该消息流或记录兼容性诊断。 3. 将这套模式抽成统一 helper，例如 `SerializeVersionedMessage(Ar, CurrentVersion, LambdaPerVersion)`，避免 `CallStack`、`Variables`、`DatabaseSettings` 继续各自发明版本兼容规则。 4. `SendDebugDatabase()` 与 `SendAssetDatabase()` 保持发送当前版本，但在握手日志或 capability 中明确暴露当前消息版本，便于客户端做双向兼容判断。 5. 在协议测试里补跨版本回归：至少覆盖“旧 payload 按当前 reader 读取”“当前 payload 被降级 reader 读取并优雅拒绝/降级”两类场景，不再只做同版本 round-trip。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` 或新增 `AngelscriptDebugProtocolTests.cpp` 覆盖 |
| 预估工作量 | M |
| 风险 | 若直接修改 wire format 却没有版本门控，现有适配器可能立刻失配；必须把“旧版如何读新版、旧版是否显式拒绝”定义清楚。 |
| 前置依赖 | 建议与 `Issue-42` 的 request/response correlation 和 `Issue-55` 的消息 dispatch 表一并评审，但本条可以先独立修复基础版本化契约。 |
| 验证方式 | 1. 新增 `DebugDatabaseSettings` / `AssetDatabase` 的版本兼容单测，覆盖旧版、当前版和未知未来版。 2. 用测试客户端实际请求 database/asset 流，确认当前版本仍可完整初始化。 3. 构造带额外尾字段的 future payload，确认当前实现会明确拒绝或安全降级，而不是静默错位。 |

### Issue-83：补足 Analysis 发现 20 的修复方案：模板实例的 native form 解析按槽位对齐，方法顺序一变就会绑错目标

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 行号 | `StaticJITBinds.cpp:29-52`；`AngelscriptPrecompiledDataTests.cpp:13-112` |
| 问题 | `FScriptFunctionNativeForm::GetNativeForm()` 在处理 template instantiation 时，不按函数声明、参数签名或 function id 去找 template base type 上对应的 native form，而是先求“当前函数在 `constructors` / `methods` 里位于第几个槽位”，再把同一索引拿去读 `templateBaseType->beh.constructors[...]` 或 `templateBaseType->methods[...]`。源码注释直接写明了这个前提是 “the methods are always in the same order”。这意味着一旦模板基类与某个实例化类型因为新增 bind、条件编译或特化导致方法顺序漂移，JIT 就会把错误的 native form 复用到另一个函数上。 |
| 根因 | native form 解析为了省去签名级映射构建，采用了“模板基类与实例化类型的方法表严格同构”的脆弱假设，把 identity 问题错误简化成了位置问题。 |
| 影响 | 这类错误不会表现为显式失败，而是 silent wrong-call：JIT/custom/native call 可能调用到错误的构造函数、析构函数或成员方法，最终得到错误结果或错误副作用。由于现有 StaticJIT 自动化仍主要停留在 precompiled round-trip / diff，没有模板 native-form 对齐回归，这条风险目前几乎没有测试护栏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把模板实例 native form 解析从“按槽位索引”改成“按稳定签名键查找”，找不到精确匹配时宁可拒绝复用并回退，也不能继续错绑。 |
| 具体步骤 | 1. 在 `StaticJITBinds.cpp` 为 native form registry 增加稳定 lookup key，例如 `{FunctionName, ReturnType, ParamTypeIds, TraitFlags}` 或直接复用 `GetDeclaration(true, true, true, false)` 生成的 canonical declaration；template base type 与实例化类型都通过这套 key 建映射。 2. `GetNativeForm()` 处理 template instantiation 时，不再用 `IndexOf()` 求槽位；改成先为 base type 建 `SignatureKey -> NativeForm` 索引，再用当前实例化函数的 key 查 exact match。 3. 对 constructors/destructors 保留专门分支，但也应使用显式的 ctor/dtor signature key，而不是继续依赖 `beh.constructors[ConstructorIndex]` 同位。 4. 若 exact match 缺失，返回 `nullptr` 并让上层走已有的 VM/native fallback 或显式 unsupported 诊断，禁止继续猜测“同一位置大概就是同一个函数”。 5. 把这条映射逻辑与 `Issue-61` 的 native form registry 生命周期改造一起收口，避免一边修 lookup key、一边继续让 registry 持有 stale `asIScriptFunction*`。 6. 新增模板回归：构造一个 template base type 与实例化类型在 methods/constructors 顺序不完全一致的夹具，验证修复前会错绑、修复后能命中正确 native form 或显式回退；同时保留原有顺序一致场景，确认正常 fast path 不退化。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITBinds.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` 或新增模板 native-form 回归测试 |
| 预估工作量 | M |
| 风险 | 如果 canonical signature 选型不稳定，可能把现有可复用路径误判成 mismatch；因此 key 必须与 AngelScript 层真实重载决议一致，不能只靠裸函数名。 |
| 前置依赖 | 建议与 `Issue-61` 的 native form registry 生命周期修复协同推进，但本条可先独立修正 lookup 策略。 |
| 验证方式 | 1. 新增模板 native-form 回归，覆盖“顺序一致”和“顺序漂移”两组场景。 2. 对同一模板方法分别走解释器/native fallback 与 JIT/custom call，确认调用目标和结果一致。 3. 回归现有 precompiled/JIT 测试，确保普通非模板 native form 解析不受影响。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-81 | Defect | 立即修复，先堵住 `asBC_ClrHi` 命中后直接生成非法 C++ 的确定性构建失败 |
| P1 | Issue-83 | Architecture | 在 native form 生命周期重构前优先处理，避免模板实例 JIT custom/native call 静默绑错目标 |
| P2 | Issue-82 | Architecture | 紧随其后补齐 database/asset 握手的版本化解码契约，避免协议升级时直接错位 |

---

## 发现与方案 (2026-04-09 01:49)

### Issue-84：accepted client socket 未显式切换为 `non-blocking` / `no-delay`，DebugServer 传输层依赖未声明的 socket 默认值

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptDebuggerTestClient.cpp`、`J:/UnrealEngine/UEAS2/Engine/Source/Runtime/Networking/Public/Common/TcpListener.h`、`J:/UnrealEngine/UEAS2/Engine/Source/Runtime/Sockets/Private/BSDSockets/SocketsBSD.cpp` |
| 行号 | `AngelscriptDebugServer.cpp:395-399, 724-797, 2845-2858`；`AngelscriptDebuggerTestClient.cpp:48-55`；`TcpListener.h:167-181`；`SocketsBSD.cpp:149-160` |
| 问题 | `HandleConnectionAccepted()` 只记录日志并把 `ClientSocket` 入 `PendingClients`，后续 `ProcessMessages()` / `TrySendingMessages()` 便直接在同一 `FSocket*` 上做 `HasPendingData()`、`Recv()` 和 `Send()`。全文件没有任何 `SetNonBlocking(true)` 或 `SetNoDelay(true)`。对照同仓测试客户端，连接建立后会立刻显式设置这两个选项。引擎 `FTcpListener` 与 `FSocketBSD::Accept()` 也只是把 `accept()` 返回的新 socket 原样交给 delegate/工厂，没有替插件补 transport mode。源码事实是：当前插件没有为 accepted socket 建立显式 I/O 模式保证。推断：如果某个平台或 socket 后端返回的是 blocking socket，或默认保留 Nagle 聚包，`Tick()` 所在线程就可能在 `Recv()` / `Send()` 上发生阻塞，或者在 stop/step/variables 这种小包交互上引入额外延迟。 |
| 根因 | DebugServer 没有在“拿到 accepted socket 的所有权”这一唯一安全边界上收口传输层初始化，整个协议栈默认假设底层 socket 已经天然适合轮询式调试消息处理。 |
| 影响 | 这会让 DebugServer V2 的传输正确性依赖平台默认值而不是插件自己的契约。最坏情况下，game thread 会被 blocking `Recv()` / `Send()` 卡住；即使不彻底阻塞，未禁用 Nagle 也会放大小包往返延迟，并与已记录的 partial-read / partial-send / 超时剔除问题相互放大。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 accepted-socket 入口统一执行 transport mode 初始化，禁止后续收发链继续依赖未声明的默认值。 |
| 具体步骤 | 1. 在 `AngelscriptDebugServer.cpp` 提取统一 helper，例如 `bool InitializeAcceptedClientSocket(FSocket* ClientSocket)`，内部至少执行 `SetNonBlocking(true)` 与 `SetNoDelay(true)`；任何一步失败都要记录带 endpoint/description 的日志，并拒绝把该 socket 放入 `PendingClients`。 2. `HandleConnectionAccepted()` 改成先调用该 helper，再 `PendingClients.Enqueue(ClientSocket)`；失败时仿照 `FTcpListener` 的失败路径，统一走 `Close()` + `DestroySocket()`，不要把“半初始化 socket”留给后续逻辑。 3. 若未来按 `Issue-8` 引入统一 `ReleaseClientSocket()`，这里也应复用同一销毁 helper，避免 accept 失败、显式断开、超时移除三条路径再次分叉。 4. 将 socket transport mode 视为 session 初始化的一部分，与后续 `Issue-11` 的 per-session state 一起建模；不要继续把“可读写行为正常”建立在裸 `FSocket*` 上。 5. 增加 transport 测试 seam：允许用 fake socket 或 injected adapter 断言 accepted socket 会被设置为 non-blocking/no-delay；如果当前 harness 还不能观察 socket option，至少补一条测试覆盖“初始化失败时不会进入 `PendingClients`，且会触发销毁”。 6. 若后续要支持平台差异化选项，也应把差异封装在 helper 内，而不是散落到 `ProcessMessages()` / `TrySendingMessages()` 里临时兜底。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugTransportTests.cpp` |
| 预估工作量 | S |
| 风险 | 若只在个别平台或个别路径补 `SetNonBlocking`，仍会留下跨平台行为分叉；必须把 accepted-socket 初始化收敛成单一入口。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 transport 单测或 fake-socket seam，验证 accepted socket 会显式设置 `non-blocking` / `no-delay`。 2. 回归现有 debugger test client 连接流程，确认正常连接与收发行为不退化。 3. 在慢网或受限 send/recv 条件下做 smoke，确认 DebugServer 不会因 transport mode 未初始化而出现卡住或异常抖动。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-84 | Architecture | 优先在继续修协议边界前落地，先把 accepted socket 的传输契约收口成显式初始化 |

---

## 发现与方案 (2026-04-09 01:50)

### Issue-85：`SCRIPT_DEBUG_FILENAME` 被固定写成 `ModuleName`，JIT 栈即使接入 DebugServer 也会持续报告伪源码路径

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | `AngelscriptStaticJIT.h:45-49`；`AngelscriptStaticJIT.cpp:122-128, 3612-3621`；`StaticJITHeader.h:194-209, 338-340`；`AngelscriptEngine.cpp:4339-4345, 5108-5117, 5668-5678` |
| 问题 | `FJITFile` 同时保存了 `Filename` 和 `ModuleName`，但 codegen 在 `3612-3621` 处定义 `SCRIPT_DEBUG_FILENAME` 时，实际写入的是 `File.ModuleName`，不是 `File.Filename`，更不是脚本 section 的真实路径。随后 `SCRIPT_DEBUG_CALLSTACK_FRAME*` 宏会把这个值塞进 `FScopeJITDebugCallstack::Filename`，而引擎 `GetStackTrace()` 与 `GetAngelscriptExecutionFileAndLine()` 的 JIT 分支又把 `DebugStack->Filename` 当成源码文件来源直接向外暴露。相比之下，解释器路径在编译模块时明确通过 `AddScriptSection(TCHAR_TO_ANSI(*Section.AbsoluteFilename), ...)` 保留真实 section filename。结果是：同一 module 下所有 JIT 函数都会共享同一个 module 级“伪文件名”，多文件 module 更无法区分真实来源。 |
| 根因 | StaticJIT 把“用于生成产物文件名的 module 标识”和“调试时需要回报的 source filename”混成了一个字段，并在 `SCRIPT_DEBUG_FILENAME` 这一单一路径上把 module name 错当成源码路径传播到整个 JIT 调试栈。 |
| 影响 | 这不是已有“JIT `CallStack` 目前缺顶帧/缺协议接入”问题的重复，而是更底层的调试元数据错误：即使后续把 JIT `CallStack`、异常、`GetAngelscriptExecutionFileAndLine()` 全部接通，当前实现仍会持续把 module name 当成 source path 回给上层。对多文件 module 来说，两个 section 即便在同一行号上停住，也无法靠 JIT 栈区分真实文件，source 定位、资产元数据和未来 DAP `stackTrace/source` 都会被带偏。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 JIT 调试元数据拆成“真实 source filename”和“可选 module name”两条独立字段，禁止继续复用 module-level 字符串伪装源码路径。 |
| 具体步骤 | 1. 在 StaticJIT codegen 路径上停止用 `File.ModuleName` 定义 `SCRIPT_DEBUG_FILENAME`。最小可行修复应为每个生成函数解析其真实 section/canonical source filename，并为该函数生成独立的 `static const char*` 符号传给 `SCRIPT_DEBUG_CALLSTACK_FRAME*`。 2. 将 `FScopeJITDebugCallstack` 从单一 `Filename` 字段扩展为至少 `SourceFilename` + `ModuleName` 两个概念；若短期不想改 wire format，也至少要先保证内部调试栈保存真实文件路径，再由协议层按需附带 module 名。 3. `GetStackTrace()`、`GetAngelscriptExecutionFileAndLine()` 以及未来 `SendCallStack()` 的 JIT frame 构建，都应统一消费 `SourceFilename`，不能继续把 module name 当作文件名输出。 4. 若当前 codegen 在某些 safe point 上无法直接拿到 section filename，就应新增函数级或 safe-point 级 debug metadata 表，显式记录“脚本函数 -> canonical source filename”的映射；不要继续依赖当前 `FJITFile` 这种 module 粒度容器。 5. 与已有 JIT `CallStack`/`exception` 修复协同推进时，要明确区分 `Frame.Source` 与 `Frame.ModuleName` 的职责：前者用于真实 source 定位，后者才是可选的 module 维度补充信息。 6. 增加回归测试：构造同一 module 下两个不同脚本文件、相同行号的最小夹具，验证 JIT 顶帧输出能稳定区分真实文件，而不是都回成同一个 module 名。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/StaticJITHeader.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITCodegenTests.cpp` 或等价新增回归测试 |
| 预估工作量 | M |
| 风险 | 若只在 `GetStackTrace()` 做字符串替换，而不修正 JIT 栈元数据源头，后续 `CallStack`、异常和资产元数据仍会继续读取错误字段，问题会换个出口反复出现。 |
| 前置依赖 | 无；但建议与已有 JIT `CallStack` 接入工作同步设计，避免刚把 JIT 栈暴露出来就继续输出错误文件名。 |
| 验证方式 | 1. 新增多文件 module 的 JIT 定位回归，断言顶帧 `Source`/filename 是真实脚本文件路径。 2. 回归 `GetStackTrace()` 与 `GetAngelscriptExecutionFileAndLine()` 的 JIT 路径，确认输出不再等于 module name。 3. 在后续 JIT `CallStack` 集成测试中额外断言 `Frame.Source` 与 `Frame.ModuleName` 分工正确，不再混用。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-85 | Defect | 紧随 JIT `CallStack` 接入工作推进，先纠正 JIT 栈最基础的 source filename 元数据 |

---

## 发现与方案 (2026-04-09 01:51)

### Issue-86：`asBC_RET` 的 reference-return 校验写成永真式，JIT 返回路径已失去寄存器状态护栏

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` |
| 行号 | `AngelscriptBytecodes.cpp:2598-2605, 2694-2702, 2715-2745`；`AngelscriptStaticJIT.cpp:666-685, 706-712, 755-762` |
| 问题 | `asBC_RET::Implement()` 在 `!Context.bReturnOnStack && Context.bReturnIsReference` 分支里，先执行 `check(Context.ValueRegisterState != ValueRegister || Context.ValueRegisterState != Indeterminate);`，然后无条件生成 `return ({0})l_valueRegister;`。这条 `check` 是永真式，因为任意状态都不可能同时等于 `ValueRegister` 和 `Indeterminate`，因此它永远不会失败。对照同文件 `float/double/int` 返回分支的写法，后者都使用了 `== Expected || == Indeterminate` 的真实状态断言；更早的 `asBC_SaveReturnValue` 还会在 reference-return 场景主动调用 `Context.MaterializeValueRegister()`，说明这条路径本来就依赖“引用返回前必须回到 `ValueRegister`”这一 invariant。现在 `RET` 端的护栏已经完全失效。 |
| 根因 | reference-return 分支把状态断言写成了错误的布尔组合，导致 `RET` 末端不再验证自己是否真的拿到了可返回的 `l_valueRegister`，原本应与 `SaveReturnValue` 形成前后呼应的 invariant 被静默打断。 |
| 影响 | 当前这条问题未必在所有 reference-return 场景下立刻表现成错误返回，因为 `asBC_SaveReturnValue` 还在前面做一次 materialize；但这恰恰说明 `RET` 端的校验更重要。只要未来某条 bytecode 路径绕过或回归破坏了 `SaveReturnValue` 的前置 materialization，这里既不会在 debug build 报错，也不会在 release build 修正状态，而是直接把当下的 `l_valueRegister` 返回出去，形成难以察觉的 silent miscompile。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 不再依赖前序 bytecode“碰巧已经 materialize”的隐含前提，把 reference-return 的寄存器准备与断言收敛成单一 helper。 |
| 具体步骤 | 1. 将 `2700` 处的永真式改成真实 invariant：至少应是“`Context.ValueRegisterState == ValueRegister || Context.ValueRegisterState == Indeterminate`”；更稳妥的修复是在断言前先判断状态，若不是 `ValueRegister` 则调用 `Context.MaterializeValueRegister()`，再断言结果。 2. 抽一个专门 helper，例如 `EnsureReferenceReturnRegisterReady(FStaticJITContext& Context)`，让 `asBC_SaveReturnValue` 与 `asBC_RET` 共同使用，避免未来再次出现“前半段 materialize、后半段断言漂移”的双轨逻辑。 3. 审计 `bReturnIsReference` / `bReturnIsPointer` / `bReturnOnStack` 的所有 return 分支，确认每一种最终都只有一个权威“返回前寄存器准备”入口；不要继续把相同 invariant 分散在多个 bytecode handler 里手写。 4. 若担心 `RET` 阶段再 materialize 会掩盖更早的问题，可在 helper 中区分“恢复状态”和“记录诊断”：debug/test 构建在发生 late materialization 时输出一次明确 warning 或 `checkf`，release 构建仍保证生成正确代码。 5. 补一个最小 codegen 回归：构造 reference-return 函数并人为让 `RET` 前状态处于非 `ValueRegister` 分支，断言修复后会先 materialize 再返回；同时保留正常路径，确认输出不退化。 6. 顺手扫描 `AngelscriptBytecodes.cpp` 中其它状态断言，尤其是涉及 `EValueRegisterState` 的布尔组合，避免再留下类似“永真/永假条件把 codegen invariant 变成摆设”的问题。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptBytecodes.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.h`、`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptStaticJITCodegenTests.cpp` 或等价新增回归测试 |
| 预估工作量 | S |
| 风险 | 如果只把布尔表达式改对，但不把 reference-return 的准备逻辑收敛成 helper，未来仍可能再次出现“`SaveReturnValue` 和 `RET` 两边假设不一致”的隐性漂移。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增 reference-return codegen 回归，验证非 `ValueRegister` 状态在 `RET` 前会被 materialize 或被明确捕获。 2. 回归普通值返回、指针返回和 return-on-stack 对象返回，确认没有因为 helper 收口而改变既有输出。 3. 以 debug/test 构建跑一轮相关 JIT codegen，用断言或日志确认这条 invariant 真正恢复可观测。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-84 | Architecture | 先收口 accepted socket 的 transport mode，避免后续协议修复继续建立在未声明的底层默认值上 |
| P1 | Issue-85 | Defect | 紧接着修正 JIT 调试元数据的真实 source filename，避免后续 JIT `CallStack` 接入后继续输出伪路径 |
| P2 | Issue-86 | Defect | 作为 JIT 返回路径 invariant 热修跟进，先恢复 reference-return 的状态护栏，再继续扩大 codegen 覆盖面 |
