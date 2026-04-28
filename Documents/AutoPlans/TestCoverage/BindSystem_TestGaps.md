# BindSystem 测试覆盖缺口分析

---

## 测试审查 (2026-04-08 13:07)

### 一、现有测试问题

#### Issue-1：NativeStaticClassNamespace 测试未把命名空间恢复失败视为失败

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeStaticClassNamespace` |
| 行号范围 | 433-447 |
| 问题描述 | 测试先调用 `ScriptEngine->SetDefaultNamespace("AActor")` 切换全局 namespace，结束前只调用了一次 `TestTrue(TEXT("Restore global namespace should succeed"), ScriptEngine->SetDefaultNamespace("") >= 0);`，但返回值没有并入 `bPassed`。也就是说，只要 `StaticClassFunction` 存在，`RunTest` 仍可能返回 `true`，即使 namespace 恢复失败。 |
| 影响 | 失败的清理不会让用例红灯，后续依赖默认 namespace 的绑定测试可能读取到残留全局状态，形成顺序相关的误报绿灯或串测。 |
| 修复建议 | 用 `const bool bRestored = TestTrue(...); bPassed = bHasFunction && bRestored;` 明确把恢复结果计入最终返回值；更稳妥的做法是用 `ON_SCOPE_EXIT`/局部 guard 无条件恢复 namespace，并在 guard 内对恢复结果做断言。 |

#### Issue-2：Console 绑定测试文件超出单文件规模约束

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ConsoleVariableCompat` 等 5 个用例 |
| 行号范围 | 1-509 |
| 问题描述 | 该文件当前 509 行，已经超过规则要求的单文件 300-500 行范围。文件同时覆盖 `FConsoleVariable`、命令注册、命令替换、签名报错等多个职责，辅助函数与测试脚本也全部堆在同一文件。 |
| 影响 | 继续在该文件追加测试会让 console 绑定测试越来越难维护，review 时也更难判断每类绑定是否共享了不合适的 setup/cleanup。 |
| 修复建议 | 将该文件拆成至少两个职责文件，例如 `AngelscriptConsoleVariableBindingsTests.cpp` 与 `AngelscriptConsoleCommandBindingsTests.cpp`；把通用 helper 下沉到 `Bindings/Shared/`，保证每个文件控制在 300-500 行且围绕单一主题。 |

#### Issue-3：DateTime 绑定测试依赖系统时钟且只做宽松年份断言

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.DateTimeCompat` |
| 行号范围 | 347-356 |
| 问题描述 | 用例直接调用 `FDateTime::Now()` 和 `FDateTime::UtcNow()`，随后只检查 `GetYear() < 2020` 这种宽松条件。这个断言既依赖宿主机系统时间正确，也没有验证绑定结果与 UE 原生 API 的具体语义关系。 |
| 影响 | 机器时钟异常、CI 时区配置异常或被测环境时间被篡改时会产生与绑定实现无关的失败；即使绑定把时区/时间值算错，只要年份仍大于 2020 也会被误报为通过。 |
| 修复建议 | 去掉与当前系统时间耦合的断言，改成验证确定性的 API 组合，例如 `FromUnixTimestamp(0)` / `ToUnixTimestamp()` / `Today()` 的零点语义；若必须覆盖 `Now()`/`UtcNow()`，至少比较脚本结果与同一时刻的原生 `FDateTime::Now()`/`UtcNow()` 的差值是否在合理容差内。 |

#### Issue-4：FileHelper 绑定测试写入真实文件但未清理且复用固定文件名

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.FileHelperCompat` |
| 行号范围 | 282-325 |
| 问题描述 | 用例脚本把内容写到 `FPaths::ProjectSavedDir()/AngelscriptFileHelperCompat.txt`，但 `ON_SCOPE_EXIT` 只做 `Engine.DiscardModule("ASFileHelperCompat")`，没有删除测试产物。文件名还是固定常量，重复执行时会复用同一路径。 |
| 影响 | 测试会污染 `Saved/` 目录，并在并发执行或上次残留未清理时引入串测风险；如果后续有人补了“文件已存在”分支，这个共享路径还会让结果受执行顺序影响。 |
| 修复建议 | 改成由 C++ 侧先生成唯一临时文件名（例如加 `FGuid`），通过 `FString::Printf` 注入脚本；在 `ON_SCOPE_EXIT` 里始终调用 `IFileManager::Get().Delete` 删除该文件，并额外断言写入后 `FileExists` 为真、清理后文件被移除。 |

#### Issue-5：NativeActorMethods 用例对关键返回值几乎没有有效断言

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeActorMethods` |
| 行号范围 | 44-54 |
| 问题描述 | 脚本里取了 `GetActorLocation()`、`GetActorRotation()`、`GetClass()`、`GetPathName()`、`GetFullName()` 等多个 native 绑定，但最终只检查 `!bActorType`，并且 `Path.Len() < 0`、`FullName.Len() < 0` 这两个条件永远为假，等于没有验证 `GetPathName` / `GetFullName` 的结果。`Location` 和 `Rotation` 甚至完全未参与断言。 |
| 影响 | 这些桥接函数即使返回空字符串、错误坐标或错误旋转，此用例也大概率仍然绿灯，无法证明 `Bind_AActor.cpp` / `Bind_UObject.cpp` 的返回值语义与 UE 原生 API 一致。 |
| 修复建议 | 让测试在 C++ 侧创建具名对象并设定确定性 transform，再在脚本中对 `GetActorLocation()`、`GetActorRotation()`、`GetPathName()`、`GetFullName()` 做精确比对；至少应把 `Path.IsEmpty()`/`FullName.IsEmpty()` 和预期对象名纳入断言，并移除永远为假的 `Len() < 0` 判断。 |

#### Issue-6：PlatformProcess 绑定测试依赖宿主环境能力而不是验证确定性语义

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.PlatformProcessCompat` |
| 行号范围 | 171-190 |
| 问题描述 | 用例把 `UserDir()`、`ComputerName()`、`UserName()`、`CanLaunchURL("https://example.com")` 等 API 统一降级成“非空/可用即可”的宿主环境探测，没有把脚本返回值与 C++ 原生 API 的同一结果做比对。尤其 `CanLaunchURL` 取决于机器是否配置浏览器/URL handler。 |
| 影响 | 测试结果会随 CI 镜像、无头环境、权限策略和本机配置变化，失败时很难判断是绑定坏了还是环境限制；同时即使绑定返回了错误字符串，只要不是空串也会误报通过。 |
| 修复建议 | 在 C++ 侧先读取 `FPlatformProcess` 的原生结果并注入脚本做精确比较；对环境敏感项（例如 `CanLaunchURL`）改成只验证脚本与原生 API 返回值一致，或单独拆成可跳过的环境探测测试，避免把宿主能力当作绑定正确性的判断标准。 |

#### Issue-7：Utility 绑定测试把 `PATH`/命令行存在性当成正确性断言

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.UtilityCompat` |
| 行号范围 | 109-133 |
| 问题描述 | 用例虽然在 C++ 侧预先算出了 `ExpectedTokens`、`ExpectedSwitches` 和 `ProjectName`，但对 `FCommandLine::Get()` 与 `FPlatformMisc::GetEnvironmentVariable("PATH")` 只做了 `IsEmpty()` 判断，没有验证脚本侧结果是否与原生 API 一致。 |
| 影响 | 绑定即使截断命令行、丢失环境变量内容或返回错误路径，只要字符串非空就会通过；同时 `PATH` 是否存在取决于宿主环境，属于把环境现状误当成绑定语义。 |
| 修复建议 | 在 C++ 侧读取 `FCommandLine::Get()` 和 `FPlatformMisc::GetEnvironmentVariable("PATH")` 的精确值，转义后注入脚本做 `==` 比较；若担心 `PATH` 过长，可改用测试内临时设置的专用环境变量并在 `ON_SCOPE_EXIT` 恢复。 |

#### Issue-8：GlobalVariableCompat 以单个混合烟雾测试代替了真正的 Global bindings 覆盖

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGlobalBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GlobalVariableCompat` |
| 行号范围 | 20-43 |
| 问题描述 | 当前文件只有一个用例，把 `CollisionProfile::BlockAllDynamic`、`FComponentQueryParams::DefaultComponentQueryParams`、`FGameplayTag::EmptyTag`、`FGameplayTagContainer::EmptyContainer`、`FGameplayTagQuery::EmptyQuery` 混在一个脚本入口里验证，且最终仍然只靠单个 `Result == 1` 作主断言。`Bind_CoreGlobals.cpp` 暴露的 4 个 commandlet globals 完全未覆盖。 |
| 影响 | 只要这几个常量中的任意一项没被脚本分支命中，主测试仍缺少细粒度诊断；更关键的是，名为 `GlobalBindings` 的测试文件实际上没有覆盖整组 global API，几乎不能代表该区域健康度。 |
| 修复建议 | 至少拆成 `GlobalConstantsCompat` 与 `GlobalCommandletGlobalsCompat` 两个用例；对每个 global 项单独做 `TestEqual`/`TestNull` 级别断言，避免把不相干的全局符号塞进同一个返回码脚本。 |

### 二、需要新增的测试

#### NewTest-77：为 `Bind_Console.cpp` 补齐“handler 名字不存在”错误路径回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.cpp` |
| 关联函数 | `FScriptConsoleCommand::FScriptConsoleCommand(const FString& Name, const FString& FunctionName)` 中 `NamedFunction == nullptr` 分支 |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.ConsoleCommandSignatureCompat` 只覆盖“同名函数存在但签名错误”；`Bind_Console.cpp` 89-91 行生成的 `"Could not find global function '%s' to bind as console command."` 分支完全没有行为级断言 |
| 风险评估 | 一旦函数查找逻辑回退成静默成功、错误消息串错、或错误地注册了一个空 command，当前 `Bindings` 目录不会报警；这会让 console command 的最常见配置错误缺少第一时间的自动化护栏。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ConsoleCommandMissingHandlerCompat` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleCommandErrorBindingsTests.cpp` |
| 场景描述 | 复用现有 console helper，脚本模块只定义 `int Entry()`，在 `Entry()` 内执行 `const FConsoleCommand Command(UniqueCommandName, n"MissingHandler");`，但模块中故意不声明任何 `MissingHandler` 全局函数。用 C++ 侧手动创建 `asIScriptContext` 执行 `Entry()`，捕获构造阶段抛出的脚本异常。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；C++ 侧生成唯一 `CommandName`，并在执行前 `AddExpectedError(TEXT("Could not find global function 'MissingHandler' to bind as console command."), EAutomationExpectedErrorFlags::Contains, 1)`；`ON_SCOPE_EXIT` 中调用现有 `UnregisterConsoleObjectIfPresent(CommandName)`，保证失败路径也无残留。 |
| 期望行为 | 1. `PrepareResult == asSUCCESS`。2. `ExecuteResult == asEXECUTION_EXCEPTION`，而不是宽泛的“任意失败”。3. `Context->GetExceptionString()` 必须包含 `Could not find global function 'MissingHandler' to bind as console command.`。4. `VerifyConsoleCommandMissing(*this, CommandName)` 返回 `true`，证明失败路径不会注册半残命令对象。5. 再次查找同名 command 仍为 `null`，确保失败后没有残留可执行状态。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`GetFunctionByDecl()`、`Engine.CreateContext()`、现有 `VerifyConsoleCommandMissing()` / `UnregisterConsoleObjectIfPresent()` helper |
| 优先级 | P1 |

#### NewTest-78：为完全无直测的 `Bind_Debugging.cpp` 建立 `throw` / callstack 行为回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp` |
| 关联函数 | `throw(const FString& Message)` / `GetAngelscriptCallstack()` / `FormatAngelscriptCallstack()` |
| 现有测试覆盖 | 当前 `Bindings/` 目录对 `Bind_Debugging.cpp` 为完全无直测；没有任何用例验证脚本侧抛错消息、异常函数定位和 callstack 文本格式 |
| 风险评估 | 调试绑定一旦回退，脚本运行时最关键的诊断入口会直接失明。`throw()` 若不再抛出精确异常、`GetAngelscriptCallstack()` 若丢帧或格式错乱，现有 Bindings 区域不会提供任何信号。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.DebuggingThrowAndCallstackCompat` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDebugBindingsTests.cpp` |
| 场景描述 | 构建一个小型脚本模块，包含 `int ProbeCallstack()`、`int EntryCallstack()`、`void ThrowLeaf()`、`void ThrowMiddle()`、`int EntryThrow()`。`EntryCallstack()` 调用 `ProbeCallstack()`，在 `ProbeCallstack()` 内读取 `GetAngelscriptCallstack()` 与 `FormatAngelscriptCallstack()`；`EntryThrow()` 调用 `ThrowMiddle()`，最终在 `ThrowLeaf()` 中执行 `throw("DebuggingThrowCompat")`。C++ 侧先跑 callstack happy path，再手动执行 throw 路径捕获异常上下文。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；脚本模块名固定如 `ASDebuggingCompat`；C++ 侧通过 `BuildModule()` 编译后，先用 `ExecuteIntFunction()` 运行 `int EntryCallstack()`，再用 `Engine.CreateContext()` + `Prepare/Execute` 运行 `int EntryThrow()`。 |
| 期望行为 | 1. `EntryCallstack()` 返回 `1`。2. `ProbeCallstack()` 内部断言 `Stack.Num() >= 2`，并且 `Stack[0]` / `Formatted` 至少包含 `ProbeCallstack` 与 `EntryCallstack` 两个函数名，证明脚本栈帧顺序没有丢失。3. `EntryThrow()` 的 `PrepareResult == asSUCCESS`。4. `ExecuteResult == asEXECUTION_EXCEPTION`。5. `Context->GetExceptionString()` 精确包含 `DebuggingThrowCompat`。6. `Context->GetExceptionFunction()` 非空，且声明或名称包含 `ThrowLeaf`，证明异常定位落在真正抛错帧，而不是被包装层吞掉。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`GetFunctionByDecl()`、`ExecuteIntFunction()`、`Engine.CreateContext()` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-102 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 1, NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数，仍与任务描述中的“24 个测试文件”不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数，仍与任务描述中的“126 个 Bind_*.cpp”不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 17 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 84 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“已测但缺对象身份断言”的测试 | 1 | `ObjectCastCompat` |
| 本轮新增识别为“已测但缺错误路径”的 bind 源码 | 1 | `Bind_Console.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_Debugging.cpp` |

---

## 测试审查 (2026-04-10 01:25) 真正EOF索引-实际文件末尾

本轮正文已写入前部的 `## 测试审查 (2026-04-10 01:25)` 小节；对应新增项为 `Issue-103`、`NewTest-79`。这里补真正位于文件末尾的索引与汇总，供后续轮次从 EOF 接续。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-103 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 18 | 当前仓库实物数，仍与任务描述中的“24 个测试文件”不符 |
| 其中非 `Angelscript.TestModule.Bindings.*` 命名的测试文件 | 1 | `AngelscriptActorFunctionLibraryTests.cpp` 实际承载 `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数，仍与任务描述中的“126 个 Bind_*.cpp”不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 41 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 18 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 82 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“目录内已有测试但断言未锁住原生旋转语义”的测试 | 1 | `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 4 | `Bind_FIntPoint.cpp`、`Bind_FIntVector.cpp`、`Bind_FIntVector2.cpp`、`Bind_FIntVector4.cpp` |

---

## 测试审查 (2026-04-10 01:25) 真正EOF索引-实际文件末尾

本轮正文已写入前部的 `## 测试审查 (2026-04-10 01:25)` 小节；对应新增项为 `Issue-103`、`NewTest-79`。这里补真正位于文件末尾的索引与汇总，供后续轮次从 EOF 接续。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-103 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 18 | 当前仓库实物数，仍与任务描述中的“24 个测试文件”不符 |
| 其中非 `Angelscript.TestModule.Bindings.*` 命名的测试文件 | 1 | `AngelscriptActorFunctionLibraryTests.cpp` 实际承载 `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数，仍与任务描述中的“126 个 Bind_*.cpp”不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 41 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 18 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 82 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“目录内已有测试但断言未锁住原生旋转语义”的测试 | 1 | `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 4 | `Bind_FIntPoint.cpp`、`Bind_FIntVector.cpp`、`Bind_FIntVector2.cpp`、`Bind_FIntVector4.cpp` |

---

## 测试审查 (2026-04-10 01:25)

### 一、现有测试问题

#### Issue-103：`ActorTransformRoundTrip` 只锁住位置与附着父子关系，旋转语义仍停留在脚本内自证

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptActorFunctionLibraryTests.cpp` |
| 测试名 | `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| 行号范围 | 127-367 |
| 问题描述 | `RunTransformAndSweep()` 在脚本内显式调用了 `TargetActor.SetActorRotation(FRotator(15.0, 95.0, 25.0))`，`AttachChildToActorStep()` / `AttachChildToComponentStep()` 也依赖默认 attach 规则去“snap” 子 Actor。但 C++ 侧真正落地的断言只有 sweep 的 `bool`/blocking/location，以及 `ChildActor` 的附着父对象和最终 location。它从未在原生侧核对 `ScriptMovedActor->GetActorRotation()` 是否真的变成 `SetRotation`，也没有核对 attach 后子对象 rotation 是否与父 Actor / 父 Component 的 rotation 保持预期一致。当前脚本内部的 `GetActorRotation()` 与 attach 行为即使一起漂移到同一条错误路径，只要 location 仍然看起来正确，这条测试就会继续绿灯。 |
| 影响 | 这条用例看起来像已经覆盖了 `SetActorRotation`、`GetActorRotation` 和 attach 默认 transform rule，实际只把“位置被改了、父子关系挂上了”锁住。`AActor` function-library 里更容易回退的旋转桥接和 snap 规则变化会在没有任何原生 parity 断言的情况下漏检。 |
| 修复建议 | 在 transform 阶段补原生断言：`ScriptMovedActor->GetActorRotation().Equals(SetRotation, 0.01)`，必要时再与 `NativeMovedActor->GetActorRotation()` 的 mirror baseline 对齐。两个 attach 阶段都应额外比较 `ChildActor->GetActorRotation()` 与 `ParentActor` / `ParentActor->GetRootComponent()` 的 world rotation；如果绑定故意采用非 snap 规则，就用 native lane 执行同样的 attach，再对脚本 lane 做 exact parity，而不是只看 location。 |

### 二、需要新增的测试

#### NewTest-79：为完全无直测的整数向量族建立 ctor / index / 算术 / 复制语义回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FIntPoint.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FIntVector.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FIntVector2.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FIntVector4.cpp` |
| 关联函数 | `FIntPoint` / `FIntVector` / `FIntVector2` / `FIntVector4` 的 ctor、`opAssign`、`opIndex`、`opAdd`、`opSub`、`opNeg`、`opMul`、`opDiv`、`opMulAssign`、`opDivAssign`、`GetMax`、`GetMin`、`IsZero` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.ValueTypes` 只抽样 `FVector`、`FRotator`、`FTransform`、`FText`；当前 `Bindings/` 目录对 4 个整数向量/点类型完全没有任何直测入口 |
| 风险评估 | 这些 POD 值类型在索引、赋值和整数除法上最容易出现绑定签名错位或引用返回错误，但由于完全无测试，回归只能在更高层脚本里被动发现；尤其 `opIndex` 与 `opMulAssign`/`opDivAssign` 一旦绑错，很容易表现成“脚本能编译、值却悄悄算错”。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.IntVectorValueTypesCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptIntVectorBindingsTests.cpp` |
| 场景描述 | 用一个纯脚本模块同时覆盖 4 个类型的 deterministic 运算。脚本里先验证 `FIntPoint(4, 9)` 的属性与 `Point[0]/Point[1]`，再验证 `-Point + FIntPoint(10, 20)`、`Point * 2 / 2`、`GetMax()/GetMin()`；然后验证 `FIntVector(1, 2, 3)` 的 `IsZero()`、`opIndex(2)`、`+=` / `-=`、`*=` / `/=`；对 `FIntVector2(7)` 重点验证单参数 ctor 会把两个分量都设为 `7`，以及 copy / `opIndex`；对 `FIntVector4(1, 2, 3, 4)` 验证四分量 ctor、`opNeg`、`opAdd` / `opSub`、`*` / `/` 与 `opIndex(3)`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；通过 `BuildModule()` 构建一个 `int Entry()` 脚本，不需要 world。样例值建议用 `FIntPoint(4,9)`、`FIntVector(1,2,3)`、`FIntVector2(7)`、`FIntVector4(1,2,3,4)`，避免对称数据掩盖索引错位。 |
| 期望行为 | 1. `FIntPoint(4,9)` 必须满足 `Point.X == 4 && Point.Y == 9 && Point[0] == 4 && Point[1] == 9`。2. `(-Point + FIntPoint(10,20)) == FIntPoint(6,11)`，`(Point * 2) / 2 == Point`，`GetMax()==9 && GetMin()==4`。3. `FIntVector(1,2,3)` 经 `+= FIntVector(4,5,6)` 后应为 `(5,7,9)`，再 `-= FIntVector(4,5,6)` 回到 `(1,2,3)`，`*=` / `/=` 后结果必须精确回到原值，且 `!IsZero()`。4. `FIntVector2(7)` 必须满足 `X == 7 && Y == 7`，复制后 `Copy[1] == 7`。5. `FIntVector4(1,2,3,4)` 必须满足 `(-Quad) == FIntVector4(-1,-2,-3,-4)`，`(Quad + FIntVector4(1,1,1,1)) == FIntVector4(2,3,4,5)`，`Quad[3] == 4`。6. C++ 侧最终只接受 `Result == 1`，避免继续使用“非零/非空即可”的宽松烟雾断言。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`GetFunctionByDecl()`、`ExecuteIntFunction()` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-103 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 18 | 当前仓库实物数，仍与任务描述中的“24 个测试文件”不符 |
| 其中非 `Angelscript.TestModule.Bindings.*` 命名的测试文件 | 1 | `AngelscriptActorFunctionLibraryTests.cpp` 实际承载 `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数，仍与任务描述中的“126 个 Bind_*.cpp”不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 41 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 18 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 82 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“目录内已有测试但断言未锁住原生旋转语义”的测试 | 1 | `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 4 | `Bind_FIntPoint.cpp`、`Bind_FIntVector.cpp`、`Bind_FIntVector2.cpp`、`Bind_FIntVector4.cpp` |

#### NewTest-68：为完全无直测的 `Bind_APlayerController.cpp` 建立 `SetPlayer` / `GetLocalPlayer` 语义回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_APlayerController.cpp` |
| 关联函数 | `APlayerController::SetPlayer(UPlayer)` / `APlayerController::GetLocalPlayer() const` |
| 现有测试覆盖 | 当前 `Bindings/` 目录对 `Bind_APlayerController.cpp` 为完全无直测；token direct-hit 为 0，前文 no-hit 清单仅记录了文件名，没有可执行测试规格 |
| 风险评估 | 这两个绑定虽然 surface 小，但都直接影响本地玩家上下文桥接。若 `SetPlayer()` 没有正确写入 `Player` 字段，或 `GetLocalPlayer()` 的 downcast 语义回退，脚本层会在输入、UI、split-screen 相关逻辑里拿到 `null` 或错误玩家对象，而当前 `Bindings` 区域没有任何早期告警。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.PlayerControllerLocalPlayerCompat` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptPlayerControllerBindingsTests.cpp` |
| 场景描述 | C++ 侧创建 transient `APlayerController` 与 transient `ULocalPlayer`，把两者路径注入脚本。脚本先验证初始 `Controller.GetLocalPlayer() == null`，然后执行 `Controller.SetPlayer(LocalPlayer)`，再验证 `GetLocalPlayer()` 返回同一个对象；最后再执行一次 `Controller.SetPlayer(null)`，验证 `GetLocalPlayer()` 回到 `null`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；C++ 侧通过 `NewObject<APlayerController>(GetTransientPackage())` 与 `NewObject<ULocalPlayer>(GetTransientPackage())` 创建对象，并给出稳定名称；脚本通过 `FindObject(Path)` 取得 controller/player。 |
| 期望行为 | 1. 初始 `GetLocalPlayer()` 返回 `null`。2. `SetPlayer(LocalPlayer)` 后，`GetLocalPlayer()` 非空，且 `GetPathName()` 精确等于 C++ 侧 `ULocalPlayer` 的 path。3. 再次 `SetPlayer(null)` 后，`GetLocalPlayer()` 返回 `null`。4. C++ 侧执行结束后复核 `Controller->Player == LocalPlayer` 与清空后的 `Controller->Player == nullptr`，确认绑定副作用与 UE 原生 API 一致。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`ExecuteIntFunction()`、必要时 `ON_SCOPE_EXIT` 清理模块 |
| 优先级 | P1 |

#### NewTest-69：为完全无直测的 `Bind_UPrimitiveComponent.cpp` 建立 bounds / selectable / lightmap 副作用回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UPrimitiveComponent.cpp` |
| 关联函数 | `GetBoundingBoxExtents()` / `GetBoundsOrigin()` / `GetBoundsExtent()` / `GetBoundsRadius()` / `GetbSelectable()` / `SetbSelectable(bool)` / `SetLightmapType(ELightmapType)` |
| 现有测试覆盖 | 当前 `Bindings/` 目录对 `Bind_UPrimitiveComponent.cpp` 为完全无直测；`NativeComponentMethods` 只覆盖 `UActorComponent` / `USceneComponent` surface，没有触达 primitive-specific bounds 与 selectable/lightmap 绑定 |
| 风险评估 | `Bind_UPrimitiveComponent.cpp` 直接桥接了碰撞包围盒、渲染 bounds 和 editor-facing selectable/lightmap 状态。若 lambda 绑定错读 `Bounds` 字段、`SetbSelectable()` 没写回成员、或 `SetLightmapType()` silently no-op，当前 Bindings 区域完全不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.PrimitiveComponentBoundsCompat` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptPrimitiveComponentBindingsTests.cpp` |
| 场景描述 | C++ 侧创建 transient `AActor` 与 `UBoxComponent`，设置 `BoxExtent = FVector(10,20,30)`、`RelativeLocation = FVector(100,50,25)`、`bSelectable = false`，随后调用 `UpdateComponentToWorld()` 与 `UpdateBounds()`，把组件路径以及 C++ 侧计算好的 `Bounds.Origin`、`Bounds.BoxExtent`、`Bounds.SphereRadius` 注入脚本。脚本把对象 cast 成 `UPrimitiveComponent`，依次调用 4 个 bounds getter、`GetbSelectable()`、`SetbSelectable(true)`、`SetLightmapType(ELightmapType::ForceSurface)`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；组件命名固定，脚本通过 `FindObject(ComponentPath)` 获取；C++ 侧在执行前记录原生 `GetCollisionShape().GetExtent()`、`Bounds.Origin`、`Bounds.BoxExtent`、`Bounds.SphereRadius` 作为基线。 |
| 期望行为 | 1. `GetBoundingBoxExtents()` 精确等于 `FVector(10,20,30)`。2. `GetBoundsOrigin()`、`GetBoundsExtent()`、`GetBoundsRadius()` 与 C++ 基线在小容差内一致。3. 初始 `GetbSelectable()` 为 `false`，脚本调用 `SetbSelectable(true)` 后同一脚本帧内再次读取应为 `true`。4. C++ 侧执行结束后复核 `BoxComponent->bSelectable == true`。5. C++ 侧复核 `BoxComponent->GetLightmapType() == ELightmapType::ForceSurface` 或等价内部状态，证明 `SetLightmapType()` 不是 no-op。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`ExecuteIntFunction()`、`ON_SCOPE_EXIT` 清理模块与 transient 组件 |
| 优先级 | P1 |

#### NewTest-63：为完全无直测的 `Bind_UDataTable.cpp` 建立 row round-trip / handle / category 行为回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp` |
| 关联函数 | `UDataTable::AddRow()` / `FindRow()` / `GetAllRows()` / `FDataTableRowHandle::GetRow()` / `FDataTableCategoryHandle::GetRowNames()` / `GetRows()` |
| 现有测试覆盖 | 完全无测试；当前 `Bindings/` 目录内未见任何 `DataTable` 直测入口，token 直达映射也未命中 `Bind_UDataTable.cpp` |
| 风险评估 | 该文件同时承载 `?&in/?&out` struct bridge、`FScriptArray` struct copy、row/category handle 过滤三类关键绑定。若 row struct copy、append 语义或 category 过滤回退，当前没有任何行为级信号。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.DataTableRowHandleCompat` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDataTableBindingsTests.cpp` |
| 场景描述 | 在测试文件中声明一个最小 native `USTRUCT()` `FAngelscriptBindingDataTableRow : FTableRowBase`，包含 `FName Category`、`int32 Count`、`FString Label`。C++ 侧创建 transient `UDataTable` 并设置 `RowStruct = FAngelscriptBindingDataTableRow::StaticStruct()`，把路径注入脚本。脚本侧通过 `FindObject(TablePath)` 取得表，依次调用 `AddRow()` 添加 `Alpha/Beta/Gamma` 三行，再用 `FindRow()`、`GetAllRows()`、`FDataTableRowHandle::GetRow()`、`FDataTableCategoryHandle::GetRowNames()/GetRows()` 做 round-trip 校验。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；C++ 侧创建命名表对象，例如 `BindingDataTableCompat`；category 样本固定为 `Enemy/Item/Enemy`，其中 `Alpha` 和 `Gamma` 同属 `Enemy`。 |
| 期望行为 | 1. `AddRow()` 后 `GetRowNames()` 返回 3 个精确 row name。2. `FindRow(n"Alpha", OutRow)` 返回 `true`，且 `OutRow.Category == n"Enemy"`、`OutRow.Count == 2`、`OutRow.Label == "Alpha"`。3. `GetAllRows(Rows)` 能把三行按表内容拷贝到 `TArray<FAngelscriptBindingDataTableRow>`，若数组先预塞一个 sentinel，则最终 `Num()` 应为 `1 + 3`，证明是 append 语义而不是覆盖。4. `FDataTableRowHandle{Table, n"Beta"}.GetRow(OutRow)` 返回 `true` 且得到 `Count == 7`。5. `FDataTableCategoryHandle{Table, n"Category", n"Enemy"}.GetRowNames()` 仅返回 `Alpha/Gamma`，`GetRows()` 仅返回两行且两行的 `Category` 都是 `Enemy`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`ExecuteIntFunction()`、必要时用 `ON_SCOPE_EXIT` 清理 transient `UDataTable` |
| 优先级 | P0 |

#### NewTest-64：为 `Bind_UDataTable.cpp` 补齐 struct mismatch 与 invalid handle 错误路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UDataTable.cpp` |
| 关联函数 | `GetStructType()` / `CopyStruct()` / `GetArraySubClass()` / `FDataTableRowHandle::GetRow()` / `FDataTableCategoryHandle::GetRows()` |
| 现有测试覆盖 | 完全无测试；现有 `Bindings/` 用例没有任何一次触发 row type mismatch、wrong array subtype 或 invalid handle 分支 |
| 风险评估 | 这里的失败分支要么静默返回 `false`，要么抛出 `"OutArray must be a TArray of structs."`。一旦回退成错误 copy、错误 append 或静默写脏输出，脚本侧会得到错误数据而非显式红灯。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.DataTableErrorPaths` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDataTableBindingsTests.cpp` |
| 场景描述 | 复用 `FAngelscriptBindingDataTableRow` 与同一个 transient `UDataTable`。脚本先构造一个 `FVector WrongRow` 与一个预填充 sentinel 的 `TArray<FVector>`，再分别触发 `FindRow()` 的 wrong-struct 输出、`AddRow()` 的 wrong-struct 输入、`GetAllRows()` 的 wrong-array subtype，以及空 `FDataTableRowHandle` / 空 `FDataTableCategoryHandle` 的返回分支。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；C++ 侧对 `"OutArray must be a TArray of structs."` 调用 `AddExpectedError(...Contains...)`；wrong-row sentinel 先设成非零值，用来验证失败时不会被改写。 |
| 期望行为 | 1. `Table.FindRow(n"Alpha", WrongRow)` 返回 `false`，并保持 `WrongRow` 原值不变。2. `Table.AddRow(n"Bad", WrongRow)` 后 `GetRowNames().Num()` 不增加，证明 type mismatch 是 no-op。3. `Table.GetAllRows(WrongArray)` 触发一次预期错误文本，脚本执行失败或返回错误码，且 `WrongArray.Num()` 保持初始值。4. 空 `FDataTableRowHandle` 的 `GetRow()` 返回 `false`。5. 空 `FDataTableCategoryHandle` 的 `GetRowNames()` 返回空数组，`GetRows()` 不向预填充数组追加任何新元素。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`ExecuteIntFunction()` 或 `asIScriptContext::Execute()`、`AddExpectedError()` |
| 优先级 | P1 |

#### NewTest-65：为完全无直测的 `Bind_Subsystems.cpp` 建立 `::Get()` accessor 成功/空上下文回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Subsystems.cpp` |
| 关联函数 | `UEngineSubsystem::Get()` accessor / `UGameInstanceSubsystem::Get()` accessor / `UWorldSubsystem::Get()` accessor / `ULocalPlayerSubsystem::Get(ULocalPlayer)` / `Get(APlayerController)` |
| 现有测试覆盖 | 完全无测试；`Bindings/` 目录 token 直达映射未命中 `Bind_Subsystems.cpp`，且前文未给出对应行为级测试规格 |
| 风险评估 | 该文件通过遍历所有 native subsystem class 自动生成命名空间 `Get()` 入口，任何 world-context、game-instance 或 local-player 分支回退都会同时影响整族 API。当前没有一条绑定测试能证明这些 accessor 返回的实例与 UE 原生获取路径一致。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.SubsystemGetAccessors` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSubsystemBindingsTests.cpp` |
| 场景描述 | 在测试文件中声明最小 native `UCLASS()`：`UBindingEngineSubsystem : UEngineSubsystem`、`UBindingGameInstanceSubsystem : UGameInstanceSubsystem`、`UBindingWorldSubsystem : UWorldSubsystem`、`UBindingLocalPlayerSubsystem : ULocalPlayerSubsystem`。C++ 侧使用 `FAngelscriptTestFixture` 或 `ASTEST_CREATE_ENGINE_FULL()` 建立可用 world/game instance/local player，借助 `FScopedTestWorldContextScope` 绑定当前 world context，再把各 native subsystem 的 `GetPathName()` 注入脚本。脚本分别调用 `UBindingEngineSubsystem::Get()`、`UBindingGameInstanceSubsystem::Get()`、`UBindingWorldSubsystem::Get()`、`UBindingLocalPlayerSubsystem::Get(LocalPlayer)`、`UBindingLocalPlayerSubsystem::Get(PlayerController)`。 |
| 输入/前置 | 需要一个有效 world context、game instance 和 local player；同时准备一条无 world context 分支，让 `UGameInstanceSubsystem::Get()` / `UWorldSubsystem::Get()` 返回 `null`；local-player 分支同时提供 `null` `ULocalPlayer` 与 `null` `APlayerController`。 |
| 期望行为 | 1. `UBindingEngineSubsystem::Get()` 返回非空，且 `GetPathName()` 与 C++ 侧 `GEngine->GetEngineSubsystem<UBindingEngineSubsystem>()` 一致。2. 在 `FScopedTestWorldContextScope` 内，`UGameInstanceSubsystem::Get()` 与 `UWorldSubsystem::Get()` 都返回非空，且路径分别等于 `World->GetGameInstance()->GetSubsystem<...>()` / `World->GetSubsystem<...>()`。3. `UBindingLocalPlayerSubsystem::Get(LocalPlayer)` 与 `Get(PlayerController)` 都返回同一实例。4. 脱离 world context 后，`UGameInstanceSubsystem::Get()` 与 `UWorldSubsystem::Get()` 返回 `null`。5. `UBindingLocalPlayerSubsystem::Get(null)` 与 `Get(null PlayerController)` 返回 `null`，不崩溃、不报错。 |
| 使用的 Helper | `FAngelscriptTestFixture` 或 `ASTEST_CREATE_ENGINE_FULL()`、`FScopedTestWorldContextScope`、`BuildModule()`、`ExecuteIntFunction()` |
| 优先级 | P0 |

本轮新增测试建议已记录为 `NewTest-49`、`NewTest-50`、`NewTest-51`。由于工具追加时命中了旧章节，原文保留在本文件前部，本节末尾不再重复抄写细节。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-79 |
| AntiPattern | 1 | Issue-80 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingErrorPath: 1 |
| P1 | 2 | MissingEdgeCase: 1, MissingScenario: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 全部 `.cpp` | 126 | 当前仓库实物数 |
| 其中 `Bind_*.cpp` | 123 | 用户输入“126 个 Bind_*.cpp”与当前仓库实物不符 |
| 非 `Bind_*.cpp` | 3 | `BlueprintCallableReflectiveFallback.cpp`、`UObjectInWorld.cpp`、`UObjectTickable.cpp` |
| 本轮人工复核后已见对应测试的 `Bind_*.cpp` | 41 / 123 | 完整名单已在本文件前部本轮覆盖清单中列出 |
| 本轮人工复核后完全无对应测试的 `Bind_*.cpp` | 82 / 123 | 完整名单已在本文件前部本轮覆盖清单中列出 |

---

## 测试审查 (2026-04-09 01:39) 末尾索引

### 一、定位说明

本轮新发现已登记为 `Issue-81`、`Issue-82`、`NewTest-52`、`NewTest-53`、`NewTest-54`、`NewTest-55`。

由于文档前部存在重复的“覆盖快照”锚点，本轮正文追加命中了前文旧章节；为避免重复正文，这里只补末尾索引与汇总，不重复抄写详细条目。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-81 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 3 | MissingScenario: 1, MissingErrorPath: 1, NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮再次核对，仍与任务描述中的 24 文件口径不一致 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 全部 `.cpp` | 126 | 当前目录实物统计 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 2 | `Bind_BlueprintType.cpp`、`Bind_FGuid.cpp` |
| 本轮新增识别为“完全无直测”的 bind 源码 | 2 | `Bind_UEnum.cpp`、`Bind_FQuat.cpp` |

---

## 测试审查 (2026-04-09 02:05) 末尾索引-实际EOF

### 一、定位说明

本轮新发现已登记为 `Issue-83`、`Issue-84`、`Issue-85`、`NewTest-56`、`NewTest-57`。

正文已写入文件前部的 `## 测试审查 (2026-04-09 02:05)` 小节；这里补真正位于 EOF 的索引与汇总，避免下轮再继续命中旧章节锚点。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-83 |
| FlakyRisk | 1 | Issue-84 |
| AntiPattern | 1 | Issue-85 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮再次全文复核，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 16 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中未见 token direct-hit | 84 / 123 | 这些 shard 在当前 `Bindings/` 目录内仍缺少任何直观命中入口 |

---

## 测试审查 (2026-04-09 03:09) 末尾索引-实际EOF

### 一、定位说明

本轮正文已写入文件前部的 `## 测试审查 (2026-04-09 03:09)` 小节；该小节包含 `Issue-90`、`NewTest-58`、`NewTest-59`、`NewTest-60`。

受文档内重复“覆盖快照”锚点影响，`Issue-86`、`Issue-87`、`Issue-88`、`Issue-89` 也被写入了该小节；经复核，这 4 条分别与前文 `Issue-66`、`Issue-16`、`Issue-62`、`Issue-65` 重复，仅保留原文，不计入本轮新增统计。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-90 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 2 | MissingScenario: 2 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮重新全文复核，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 2 | `Bind_Console.cpp`、`Bind_TArray.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_FMemoryReader.cpp` |

---

## 测试审查 (2026-04-09 23:33) 末尾索引-实际EOF

本轮正文已写入前文的 `## 测试审查 (2026-04-09 23:33)` 小节；对应新增项为 `Issue-91`、`Issue-92`、`NewTest-61`、`NewTest-62`。这里补真正位于 EOF 的索引与汇总，避免下轮继续命中旧锚点。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-92 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 1 | MissingScenario: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮已逐文件全文复核完成，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 1 | `Bind_TSet.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_UCollisionProfile.cpp` |

---

## 测试审查 (2026-04-09 23:48) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-09 23:48)` 小节。真正新增项为 `Issue-93`、`NewTest-63`、`NewTest-64`、`NewTest-65`；前文已保留正文，这里只补实际 EOF 汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-93 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | NoTestForSource: 2 |
| P1 | 1 | MissingErrorPath: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮已再次逐文件全文复核完成，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 16 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 84 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_UDataTable.cpp`、`Bind_Subsystems.cpp` |

---

## 测试审查 (2026-04-09 23:59) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-09 23:59)` 小节。真正新增项为 `Issue-94`、`NewTest-66`、`NewTest-67`；由于文档前部存在重复锚点，本轮正文追加命中了旧位置，这里只补实际 EOF 汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-94 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 1 | MissingScenario: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮已再次逐文件全文复核完成，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 1 | `Bind_FRandomStream.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_UGameInstance.cpp`、`Bind_ULocalPlayer.cpp` |

---

## 测试审查 (2026-04-10 00:17) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-10 00:17)` 小节。真正新增项为 `Issue-95`、`Issue-96`、`NewTest-68`、`NewTest-69`；前文已保留正文，这里只补实际 EOF 汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-96 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数；任务描述中的“24 个测试文件”与仓库不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数；任务描述中的“126 个 Bind_*.cpp”与仓库不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 17 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 84 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“已测但断言仍缺正确失败契约”的测试 | 2 | `ArrayMutationEdgeCases`、`ConsoleCommandSignatureCompat` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_APlayerController.cpp`、`Bind_UPrimitiveComponent.cpp` |

---

## 测试审查 (2026-04-10 00:28) 真正EOF索引-实际EOF

本轮正文已写入前部的 `## 测试审查 (2026-04-10 00:28)` 小节；对应新增项为 `Issue-97`、`NewTest-70`。此前补的 EOF 索引仍命中了旧锚点，这里补真正位于文件末尾的索引与汇总，供后续轮次从 EOF 接续。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-97 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 本轮已补齐对当前目录全部测试文件的全文复核；任务描述中的“24 个测试文件”与仓库实物不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物统计；任务描述中的“126 个 Bind_*.cpp”与仓库实物不符 |
| 本轮新增识别为“已测但缺 native parity 断言”的测试 | 1 | `BlueprintCallableReflectiveFallback.UMG` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_SystemTimers.cpp` |

---

## 测试审查 (2026-04-10 00:49) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-10 00:49)` 小节。真正新增项为 `Issue-98`、`Issue-99`、`NewTest-71`、`NewTest-72`、`NewTest-73`；由于文档前部存在重复锚点，本轮正文追加命中了旧位置，这里补实际 EOF 汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 2 | Issue-99 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |
| P2 | 1 | NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数；任务描述中的“24 个测试文件”与仓库不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数；任务描述中的“126 个 Bind_*.cpp”与仓库不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 17 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 84 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“shared engine 隔离错误”的测试文件 | 2 | `AngelscriptGameplayTagBindingsTests.cpp`、`AngelscriptMathAndPlatformBindingsTests.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_UUserWidget.cpp`、`Bind_UInputSettings.cpp` |

---

## 测试审查 (2026-04-10 00:58) 真正EOF索引-最终尾段

本轮正文见前部的 `## 测试审查 (2026-04-10 00:58)` 小节。真正新增项为 `Issue-100`、`Issue-101`、`NewTest-74`、`NewTest-75`、`NewTest-76`；由于文档前部存在重复锚点，本轮正文和第一次 EOF 索引都没有落在文件末尾，这里补真正位于 EOF 的索引与汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-100 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |
| P2 | 2 | NoTestForSource: 2 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数，仍与任务描述中的“24 个测试文件”不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数，仍与任务描述中的“126 个 Bind_*.cpp”不符 |
| 本轮新增识别为“已测但缺精确语义/空值断言”的测试 | 2 | `UtilityCompat`、`ObjectPtrCompat` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_FMargin.cpp`、`Bind_FAnchors.cpp` |

---

## 测试审查 (2026-04-10 01:12) 真正EOF索引

本轮正文已写入前部的 `## 测试审查 (2026-04-10 01:12)` 小节；对应新增项为 `Issue-102`、`NewTest-77`、`NewTest-78`。由于文档前部存在重复锚点，这里补真正位于文件末尾的索引与汇总，供后续轮次从 EOF 接续。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-102 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 1, NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数，仍与任务描述中的“24 个测试文件”不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数，仍与任务描述中的“126 个 Bind_*.cpp”不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 17 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 84 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“已测但缺对象身份断言”的测试 | 1 | `ObjectCastCompat` |
| 本轮新增识别为“已测但缺错误路径”的 bind 源码 | 1 | `Bind_Console.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_Debugging.cpp` |

---

## 测试审查 (2026-04-10 01:12)

### 一、现有测试问题

#### Issue-102：`ObjectCastCompat` 的 annotated cast 分支没有验证返回对象身份

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ObjectCastCompat` |
| 行号范围 | 87-97, 120-138 |
| 问题描述 | annotated 脚本分支里执行的是 `ABindingCastActor OwnerActor = Cast<ABindingCastActor>(GetOwner());`，但后续并没有把 `OwnerActor` 的名字、路径或地址与 C++ 侧创建的 owner 对齐。脚本只是在本地构造了 `FName ExpectedName = n"BindingCastOwner"`，随后拿它和另一个字面量 `FName("BindingCastOwner")` 自比；与此同时，C++ 侧 `NewObject<AActor>(GetTransientPackage(), RuntimeActorClass)` 也没有给 actor 显式命名成 `BindingCastOwner`。这意味着只要 `Cast<ABindingCastActor>(GetOwner())` 返回“某个同类对象”而非预期 owner，这条分支仍会绿灯。 |
| 影响 | `Cast<T>` 成功并不等于 cast 到了正确实例。当前用例无法证明 `Bind_UObject.cpp` / `Bind_UClass.cpp` 上的 `GetOwner()` 返回对象与 `Cast<ABindingCastActor>` 组合后保持了正确身份语义，也无法在 owner 串线或错误 outer 链接时给出定向红灯。 |
| 修复建议 | 在 C++ 侧把 `RuntimeActor` 显式创建为 `TEXT("BindingCastOwner")`，并把期望名字或 `GetPathName()` 注入脚本；脚本里把断言改成 `if (!(OwnerActor.GetName() == ExpectedName)) return 0;` 或 `if (!(OwnerActor.GetPathName() == ExpectedPath)) return 0;`。如果要更稳妥，再在 C++ 侧执行后复核 `RuntimeComponent->GetOwner() == RuntimeActor`，把“cast 成功”与“cast 到正确对象”两层语义分开锁住。 |

### 二、需要新增的测试

---

## 测试审查 (2026-04-10 00:58) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-10 00:58)` 小节。真正新增项为 `Issue-100`、`Issue-101`、`NewTest-74`、`NewTest-75`、`NewTest-76`；受文档内重复锚点影响，正文没有落在文件末尾，这里补实际 EOF 汇总，供下轮从尾部继续接续。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-100 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |
| P2 | 2 | NoTestForSource: 2 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数，仍与任务描述中的“24 个测试文件”不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数，仍与任务描述中的“126 个 Bind_*.cpp”不符 |
| 本轮新增识别为“已测但缺精确语义/空值断言”的测试 | 2 | `UtilityCompat`、`ObjectPtrCompat` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_FMargin.cpp`、`Bind_FAnchors.cpp` |

---

## 测试审查 (2026-04-10 00:58)

### 一、现有测试问题

#### Issue-100：`UtilityCompat` 只核对 `FCommandLine::Parse` 的计数，不核对 token 内容与顺序

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.UtilityCompat` |
| 行号范围 | 102-138 |
| 问题描述 | 用例在 C++ 侧先调用 `FCommandLine::Parse(TEXT("-foo Alpha Beta"), ExpectedTokens, ExpectedSwitches);`，但注入脚本后只断言 `Tokens.Num()`、`Switches.Num()` 和第一个 switch 是否等于 `"foo"`。脚本完全没有校验 `Tokens[0] == "Alpha"`、`Tokens[1] == "Beta"`，也没有把整组 tokens/switches 与原生 `FCommandLine::Parse` 的结果逐项对齐。只要绑定仍返回 2 个 token 和 1 个 switch，即使 token 顺序颠倒、内容被截断，当前测试也会绿灯。 |
| 影响 | `Bind_FCommandLine.cpp` / `Bind_FPlatformMisc.cpp` 当前只被数量级烟雾测试覆盖，`FCommandLine::Parse` 最关键的“按原顺序还原参数内容”契约没有被锁住。命令行分词一旦出现转义、空白处理或顺序回退，现有测试不会给出有效告警。 |
| 修复建议 | 在现有用例里把 `ExpectedTokens` / `ExpectedSwitches` 的具体内容一起注入脚本，至少补 `Tokens.Num()==2 && Tokens[0]=="Alpha" && Tokens[1]=="Beta"`、`Switches.Num()==1 && Switches[0]=="foo"` 的精确断言；更稳妥的做法是把 C++ 侧解析结果序列化成脚本常量数组，逐项比较脚本解析结果与原生结果完全一致。 |

#### Issue-101：`ObjectPtrCompat` 从未把已赋值的 `TObjectPtr` 清回 `null`，`opAssign(T handle_only)` 的空值语义仍未被验证

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ObjectPtrCompat` |
| 行号范围 | 27-58 |
| 问题描述 | 当前脚本只覆盖了 default ctor、从 live `UTexture2D` 构造、复制、`Get()` 和隐式转换。它验证了“空对象一开始是 `null`”，却没有在 `Assigned = Texture` 之后再执行一次 `Assigned = null` 或等价清空操作。与之对应，`Bind_BlueprintType.cpp` 为 `TObjectPtr<T>` 单独暴露了 `opAssign(T handle_only Other)` 和 `Get()` 两条路径；如果这条 `opAssign` 在 `Other == nullptr` 时保留旧指针、没有真正清空，现有测试仍会全部通过。 |
| 影响 | `TObjectPtr` 最常见的边界错误是“从有效对象回写到 `null` 后仍残留旧引用”。当前 `ObjectPtrCompat` 没有任何断言触达这条分支，意味着脚本层的空句柄回退、可空参数重置以及对象生命周期结束后的清空语义都缺少回归保护。 |
| 修复建议 | 在现有脚本末尾追加 `Assigned = null;`、`if (!(Assigned.Get() == null)) return ...;`、`TObjectPtr<UTexture2D> ClearedCopy = Assigned; if (!(ClearedCopy == null)) return ...;` 这类断言，直接锁住 `opAssign(nullptr)` 与 copy-after-clear 的结果；同时建议补一条 compile smoke，验证 `TObjectPtr<int>` 会因为 template callback 的“Subtype must be a class type”而编译失败。 |

### 二、需要新增的测试

#### NewTest-74：为 `Bind_BlueprintType.cpp` 补齐 `TObjectPtr` 的清空语义与模板拒绝路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` |
| 关联函数 | `TObjectPtr<T>::opAssign(T handle_only Other)` / `TObjectPtr<T>::Get() const` / `TObjectPtr<T>` template callback |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.ObjectPtrCompat` 只覆盖 live object ctor/copy/happy path，没有覆盖 `null` 回写，也没有覆盖 `TObjectPtr<int>` 这类非法模板实参的拒绝分支 |
| 风险评估 | 如果 `opAssign(nullptr)` 留下 stale pointer，脚本层把对象句柄清空后仍可能继续持有旧引用；如果 template callback 放过非 class subtype，错误会在更靠后的编译/运行阶段才暴露 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ObjectPtrNullAndTemplateCompat` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 场景描述 | 先编译一个 runtime 脚本：创建 `UTexture2D`，构造 `TObjectPtr<UTexture2D>`，赋值 live object 后再执行 `Assigned = null`，随后验证 `Get()`、`opEquals(null)`、copy-after-clear 都回到 `null`。然后在同一用例末尾再编译一个负例脚本 `TObjectPtr<int> Bad;`，验证模板回调拒绝非 class subtype。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；正例通过 `BuildModule()` + `ExecuteIntFunction()` 执行；负例用 `AddExpectedError("Subtype must be a class type", Contains, 1)` 后调用 `BuildModule()` 断言编译失败。 |
| 期望行为 | 1. 正例脚本中 `Assigned = null` 后，`Assigned.Get() == null`、`Assigned == null`、`TObjectPtr<UTexture2D> ClearedCopy = Assigned` 后 `ClearedCopy.Get() == null`。2. 负例脚本编译失败，并出现 template callback 的明确诊断文本 `"Subtype must be a class type"`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`ExecuteIntFunction()`、`AddExpectedError()` |
| 优先级 | P1 |

#### NewTest-75：为完全无直测的 `Bind_FMargin.cpp` 建立构造器 / 运算符 / accessor 精确回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMargin.cpp` |
| 关联函数 | `FMargin(float32)` / `FMargin(float32,float32)` / `FMargin(const FVector2D&)` / `FMargin(float32,float32,float32,float32)` / `FMargin(const FVector4&)` / `opMul` / `opAdd` / `opSub` / `GetTopLeft()` / `GetDesiredSize()` / `GetTotalSpaceAlongHorizontal()` / `GetTotalSpaceAlongVertical()` |
| 现有测试覆盖 | 当前 `Bindings/` 目录没有任何 `FMargin` 直测入口，`Bind_FMargin.cpp` 处于完全无测试状态 |
| 风险评估 | `FMargin` 是 UMG/Slate 布局里的基础值类型。若构造器参数顺序、`opMul/opAdd/opSub`、或两个 total-space helper 绑定错位，脚本布局会出现数值漂移，但当前 Bindings 区域没有任何回归信号 |
| 建议测试名 | `Angelscript.TestModule.Bindings.MarginCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptLayoutBindingsTests.cpp` |
| 场景描述 | 在同一个脚本入口中分别覆盖 5 个 ctor：`FMargin(2)`、`FMargin(3,4)`、`FMargin(FVector2D(5,6))`、`FMargin(1,2,3,4)`、`FMargin(FVector4(7,8,9,10))`。随后对 `opMul(float)`、`opMul(FMargin)`、`opAdd`、`opSub` 做 exact compare，并验证 `GetTopLeft()`、`GetDesiredSize()`、`GetTotalSpaceAlongHorizontal()`、`GetTotalSpaceAlongVertical()` 与 C++ 原生基线一致。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；C++ 侧先用原生 `FMargin` 计算 `TopLeft`、`DesiredSize`、横向/纵向总空间的基线值，再通过 `FString::Printf` 注入脚本常量；脚本用 `FVector2D::Equals(..., 0.001f)` 和精确数值断言做比较。 |
| 期望行为 | 1. `FMargin(2)` 产生四边都为 `2`。2. `FMargin(3,4)` 产生 `Left/Right == 3`、`Top/Bottom == 4`。3. `FMargin(1,2,3,4).GetTopLeft()` 等于 `(1,2)`，`GetDesiredSize()` 等于 `(4,6)`。4. `opMul(2.0f)`、`opMul(FMargin(...))`、`opAdd`、`opSub` 的结果与 C++ 原生计算完全一致。5. 横向/纵向 total-space helper 分别返回 `Left+Right` 与 `Top+Bottom`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`ExecuteIntFunction()` |
| 优先级 | P2 |

#### NewTest-76：为完全无直测的 `Bind_FAnchors.cpp` 建立 stretch 判定与 equality 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAnchors.cpp` |
| 关联函数 | `FAnchors(float32)` / `FAnchors(float32,float32)` / `FAnchors(float32,float32,float32,float32)` / `opEquals` / `IsStretchedVertical()` / `IsStretchedHorizontal()` |
| 现有测试覆盖 | 当前 `Bindings/` 目录没有任何 `FAnchors` 直测入口，`Bind_FAnchors.cpp` 为完全无测试状态 |
| 风险评估 | `FAnchors` 的构造器参数如果在 `Min/Max` 维度上绑定错位，脚本 UI 会直接得到错误锚点；`IsStretchedVertical/Horizontal` 又是布局逻辑的关键分支，当前完全没有自动化覆盖 |
| 建议测试名 | `Angelscript.TestModule.Bindings.AnchorsCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptLayoutBindingsTests.cpp` |
| 场景描述 | 覆盖 3 个 ctor：`FAnchors(0.5f)`、`FAnchors(0.25f, 0.75f)`、`FAnchors(0.0f, 0.0f, 1.0f, 1.0f)`。脚本分别验证 equality、non-stretched 与 stretched 两类判定：uniform/single-point anchors 不应 stretch，`Min != Max` 的 full-rect anchors 应同时 `IsStretchedHorizontal()` 和 `IsStretchedVertical()`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；C++ 侧先用原生 `FAnchors` 计算三组基线布尔值，再把预期 stretch 结果注入脚本；同文件可与 `MarginCompat` 共享最小 helper，保持单文件 300-500 行。 |
| 期望行为 | 1. `FAnchors(0.5f)` 等于 `FAnchors(0.5f, 0.5f, 0.5f, 0.5f)`，且横向/纵向都不 stretched。2. `FAnchors(0.25f, 0.75f)` 仍然是 point anchors，不 stretched。3. `FAnchors(0.0f, 0.0f, 1.0f, 1.0f)` 同时 stretched horizontal/vertical。4. `opEquals` 对同值 anchors 返回 `true`，对不同 `Min/Max` 组合返回 `false`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`ExecuteIntFunction()` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-100 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |
| P2 | 2 | NoTestForSource: 2 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数，仍与任务描述中的“24 个测试文件”不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数，仍与任务描述中的“126 个 Bind_*.cpp”不符 |
| 本轮新增识别为“已测但缺精确语义/空值断言”的测试 | 2 | `UtilityCompat`、`ObjectPtrCompat` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_FMargin.cpp`、`Bind_FAnchors.cpp` |

---

## 测试审查 (2026-04-10 00:49)

### 一、现有测试问题

#### Issue-98：`GameplayTag` 三个 compat 用例在 shared engine 上建模块却从不清理，测试隔离继续依赖执行顺序

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagCompat`；`Angelscript.TestModule.Bindings.GameplayTagContainerCompat`；`Angelscript.TestModule.Bindings.GameplayTagQueryCompat` |
| 行号范围 | 25-287 |
| 问题描述 | 三个用例全部使用 `ASTEST_CREATE_ENGINE_SHARE()` + `ASTEST_BEGIN_SHARE`，并分别通过 `BuildModule(...)` 创建 `ASGameplayTagCompat`、`ASGameplayTagContainerCompat`、`ASGameplayTagQueryCompat`。文件里没有任何 `Engine.DiscardModule(...)` 或 `ON_SCOPE_EXIT` 清理逻辑，等于把 3 个脚本模块和相关注册状态永久留在 shared engine 里。根据测试宏注释，`SHARE` 模式本身不会 reset；当前隔离完全依赖“后续测试刚好不碰这些模块名和注册状态”。 |
| 影响 | 这会把 `GameplayTag` 绑定测试变成顺序敏感用例：一旦后续轮次给同文件继续补 `GameplayTags::<Tag>` 常量、query helper 或额外脚本类型，残留模块就可能掩盖真实回归，或者让重复运行时的 module 生命周期问题被误报成通过。 |
| 修复建议 | 整个文件改为 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`，并在每个 `RunTest` 里补 `ON_SCOPE_EXIT { Engine.DiscardModule(TEXT("ASGameplayTagCompat")); }` 这类显式清理；若继续保留 `SHARE`，至少确保 3 个模块在 test end 时无条件 discard，避免把模块残留当成绑定语义的一部分。 |

#### Issue-99：`MathAndPlatform` 三个用例同样把脚本模块长期留在 shared engine，日志预期还会跨模块叠加

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MathExtendedCompat`；`Angelscript.TestModule.Bindings.PlatformProcessCompat`；`Angelscript.TestModule.Bindings.Logging` |
| 行号范围 | 26-259 |
| 问题描述 | 这三个用例都走 `ASTEST_CREATE_ENGINE_SHARE()` / `ASTEST_BEGIN_SHARE`，分别创建 `ASMathExtendedCompat`、`ASPlatformProcessCompat`、`ASLoggingCompat`，但文件内没有任何 `Engine.DiscardModule(...)` 或 teardown。尤其 `Logging` 还依赖 `AddExpectedError("Test error message", ...)`，却把脚本模块和日志 helper 留在 shared engine 中，后续若补日志级别断言或同名 helper，预期错误和模块状态会继续叠加。 |
| 影响 | 当前文件的失败信号不仅受宿主环境影响，还受测试执行顺序影响。shared engine 中的残留模块会让同文件后续扩展的日志/平台 helper 测试难以判断到底是绑定坏了，还是上一个模块没有被清干净；这与 `PlatformProcessCompat` 已有的环境敏感性叠加后，排障成本会更高。 |
| 修复建议 | 把整文件切到 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；每个 `RunTest` 至少加 `ON_SCOPE_EXIT` 丢弃对应模块。`Logging` 若继续依赖 expected error，还应保证脚本执行和 expected error 注册位于同一隔离作用域内，避免残留模块在下一个测试里再次触发同名日志文本。 |

### 二、需要新增的测试

#### NewTest-71：为完全无直测的 `Bind_UUserWidget.cpp` 建立 widget tree 根节点与枚举语义回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp` |
| 关联函数 | `GetRootWidget() const` / `SetRootWidget(UWidget)` / `ConstructWidget(UClass, FName)` / `GetAllWidgets(TArray<UWidget>&)` / `RemoveWidget(UWidget)` |
| 现有测试覆盖 | `Bindings/` 目录当前没有任何 `UUserWidget` 直测；`BlueprintCallableReflectiveFallback.UMG` 只验证 `UCheckBox` 上 unresolved BlueprintCallable fallback，不会触达 widget tree 相关绑定 |
| 风险评估 | `Bind_UUserWidget.cpp` 直接桥接了 script UI 最常用的 widget tree 操作。若 `ConstructWidget()` 返回错误类型、`SetRootWidget()` 没有写入 `WidgetTree->RootWidget`、`GetAllWidgets()` 漏掉 root，脚本 UI 会在运行时静默错乱，而当前 Bindings 自动化完全没有信号 |
| 建议测试名 | `Angelscript.TestModule.Bindings.UserWidgetTreeCompat` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUserWidgetBindingsTests.cpp` |
| 场景描述 | C++ 侧创建 transient `UUserWidget` 与 `UWidgetTree`，把 widget 路径注入脚本。脚本先验证初始 `GetRootWidget() == null`，再调用 `ConstructWidget(UTextBlock::StaticClass(), n"RuntimeText")` 创建 root widget，执行 `SetRootWidget()`，随后用 `GetRootWidget()` 和 `GetAllWidgets()` 校验 tree 状态，最后调用 `RemoveWidget(Root)` 并验证 tree 被清空 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_FULL()`；C++ 侧 `UUserWidget* Widget = NewObject<UUserWidget>(GetTransientPackage(), UUserWidget::StaticClass(), TEXT("BindingUserWidget"))`，并显式创建 `Widget->WidgetTree = NewObject<UWidgetTree>(Widget, TEXT("WidgetTree"))`；脚本通过 `Cast<UUserWidget>(FindObject(Path))` 获取对象 |
| 期望行为 | 1. 初始 `GetRootWidget()` 返回 `null`。2. `ConstructWidget(UTextBlock::StaticClass(), n"RuntimeText")` 返回非空，且 `GetName() == n"RuntimeText"`。3. `SetRootWidget(Root)` 后 `GetRootWidget() == Root`。4. `GetAllWidgets(Widgets)` 返回 `Num()==1`，且 `Widgets[0] == Root`。5. `RemoveWidget(Root)` 返回 `true`，随后 `GetRootWidget() == null` 且再次 `GetAllWidgets()` 为空。6. C++ 侧执行后复核 `Widget->WidgetTree->RootWidget == nullptr`，确认绑定副作用与原生语义一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL()`、`BuildModule()`、`ExecuteIntFunction()`、必要时 `ON_SCOPE_EXIT` 清理模块与 transient widget |
| 优先级 | P1 |

#### NewTest-72：为 `Bind_UUserWidget.cpp` 补齐 invalid widget class 与 missing tree 错误路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp` |
| 关联函数 | `ConstructWidget(UClass, FName)` / `SetRootWidget(UWidget)` / `RemoveWidget(UWidget)` |
| 现有测试覆盖 | 完全无测试；当前没有任何用例触发 `ensureMsgf(WidgetClass && WidgetClass->IsChildOf(UWidget::StaticClass()), ...)`，也没有覆盖 `WidgetTree == nullptr` 的 no-op / `false` 返回分支 |
| 风险评估 | 这几个错误路径一旦回退成崩溃、错误创建非 widget 对象，或在缺失 `WidgetTree` 时写脏状态，脚本 UI 会直接在编辑器里炸出难定位问题；当前完全缺少低成本回归护栏 |
| 建议测试名 | `Angelscript.TestModule.Bindings.UserWidgetTreeErrorPaths` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUserWidgetBindingsTests.cpp` |
| 场景描述 | C++ 侧准备两个 transient `UUserWidget`：一个显式带 `WidgetTree`，一个故意不创建 `WidgetTree`。脚本先在带 tree 的 widget 上调用 `ConstructWidget(AActor::StaticClass(), n"BadWidget")` 触发 invalid-class ensure；再对无 tree widget 调用 `SetRootWidget()` 和 `RemoveWidget()`，验证它们走 no-op / `false` 分支而不是写脏内部状态 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_FULL()`；对 invalid class 场景添加 `AddExpectedError(TEXT("Widget Class must be a subclass of UWidget!"), EAutomationExpectedErrorFlags::Contains, 1)`；C++ 侧同时创建一个 `UTextBlock` 作为 `SetRootWidget()` 输入对象，并把两个 widget 路径和 text block 路径注入脚本 |
| 期望行为 | 1. `ConstructWidget(AActor::StaticClass(), n"BadWidget")` 返回 `null`，且只记录一次预期错误文本。2. 对无 `WidgetTree` 的 widget 调用 `SetRootWidget(TextBlock)` 后，`GetRootWidget()` 仍为 `null`。3. 对无 `WidgetTree` 的 widget 调用 `RemoveWidget(TextBlock)` 返回 `false`。4. C++ 侧执行后复核带 tree 的 widget 没有偷偷生成 root，无 tree 的 widget 也保持 `WidgetTree == nullptr`，证明绑定错误路径与 UE 原生 API 一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL()`、`AddExpectedError(...)`、`BuildModule()`、`ExecuteIntFunction()` |
| 优先级 | P1 |

#### NewTest-73：为完全无直测的 `Bind_UInputSettings.cpp` 建立 action/axis name 与 mappings 语义回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UInputSettings.cpp` |
| 关联函数 | `GetUniqueActionName(FName)` / `GetUniqueAxisName(FName)` / `GetActionMappings() const` / `GetAxisMappings() const` / `DoesActionExist(FName)` / `DoesAxisExist(FName)` |
| 现有测试覆盖 | `Bindings/` 目录没有任何 `UInputSettings` 直测；`Bind_FInputActionKeyMapping.cpp` / `Bind_FInputAxisKeyMapping` 对应 surface 也没有被当前 Bindings 用例实际走到 |
| 风险评估 | `UInputSettings` 绑定直接影响脚本侧输入配置查询。若 name 唯一化算法、mapping 数组桥接或 `DoesActionExist/DoesAxisExist` 返回语义回退，输入系统脚本会在配置读取阶段静默拿到错误结果，而当前没有任何自动化告警 |
| 建议测试名 | `Angelscript.TestModule.Bindings.InputSettingsCompat` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInputSettingsBindingsTests.cpp` |
| 场景描述 | C++ 侧拿到 `GetMutableDefault<UInputSettings>()`，临时追加一个唯一 action mapping 和一个唯一 axis mapping，并在 `ON_SCOPE_EXIT` 恢复原始数组。脚本通过 `FindClass("UInputSettings").GetDefaultObject()` 取得 settings，对 `DoesActionExist/DoesAxisExist`、`GetActionMappings/GetAxisMappings`、`GetUniqueActionName/GetUniqueAxisName` 做原生语义比对 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；C++ 侧保存原始 `ActionMappings` / `AxisMappings`，然后追加如 `ASTestAction_XXXX` 和 `ASTestAxis_XXXX` 的唯一映射；把测试名、预期 key 名和 `GetUnique*Name()` 的原生结果一并注入脚本 |
| 期望行为 | 1. `DoesActionExist(TestAction)` 与 `DoesAxisExist(TestAxis)` 都返回 `true`。2. `GetActionMappings()` / `GetAxisMappings()` 中至少有一个条目的 `ActionName` / `AxisName` 精确等于注入的测试名，且对应按键与 C++ 侧追加的 `FKey` 一致。3. `GetUniqueActionName(TestAction)` 与 `GetUniqueAxisName(TestAxis)` 返回值必须和 C++ 原生调用结果精确一致，且不等于已存在的测试名。4. `GetUniqueActionName(NewUnusedName)` / `GetUniqueAxisName(NewUnusedName)` 的行为也应与 C++ 基线一致，防止脚本绑定把“已存在”和“未存在”两条路径混成同一实现 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`ExecuteIntFunction()`、`ON_SCOPE_EXIT` 恢复 `UInputSettings` 原始映射 |
| 优先级 | P2 |

---

## 测试审查 (2026-04-10 00:28) 真正EOF索引

本轮正文已写入前部的 `## 测试审查 (2026-04-10 00:28)` 小节；对应新增项为 `Issue-97`、`NewTest-70`。受文档内重复锚点影响，正文没有落在末尾，这里补实际 EOF 汇总，避免下轮继续命中旧位置。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-97 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 本轮已补齐对当前目录全部测试文件的全文复核；任务描述中的“24 个测试文件”与仓库实物不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物统计；任务描述中的“126 个 Bind_*.cpp”与仓库实物不符 |
| 本轮新增识别为“已测但缺 native parity 断言”的测试 | 1 | `BlueprintCallableReflectiveFallback.UMG` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_SystemTimers.cpp` |

---

## 测试审查 (2026-04-10 00:28)

### 一、现有测试问题

#### Issue-97：`ReflectiveFallback.UMG` 只在脚本内自证 setter/getter，一致性没有对齐到原生 `UCheckBox`

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.BlueprintCallableReflectiveFallback.UMG` |
| 行号范围 | 47-59 |
| 问题描述 | 脚本只做了 `SetIsChecked(true) -> GetCheckedState()` 和 `SetCheckedState(ECheckBoxState::Undetermined) -> GetCheckedState()` 两次自回读，C++ 侧最终也只断言 `Result == 1`。也就是说，这条测试证明了 reflective fallback 调用链“自洽”，但没有把脚本观察到的状态与执行后 `RuntimeObject` 的原生 `UCheckBox` 状态做任何交叉验证。若 setter/getter 一起错误地落到同一条错误路径，或对 widget 内部状态的解释与原生 API 漂移，当前用例仍可能绿灯。 |
| 影响 | `BlueprintCallableReflectiveFallback.UMG` 看起来像已经锁住了 UMG reflective fallback 的返回值语义，实际只锁住了脚本内部往返；一旦 `SetIsChecked` / `SetCheckedState` / `GetCheckedState` 与原生 `UCheckBox` 状态不同步，测试无法第一时间报警。 |
| 修复建议 | 在 `ExecuteGeneratedIntEventOnGameThread(...)` 成功后，C++ 侧追加原生断言，例如 `RuntimeObject->GetCheckedState() == ECheckBoxState::Undetermined`，并补一条 `RuntimeObject->IsChecked() == false` 来锁住 `Undetermined` 的原生语义；若希望把两次 reflective 调用分开诊断，可让脚本返回更细的阶段码，同时由 C++ 在每个阶段后读取 widget 原生状态做 parity 检查。 |

### 二、需要新增的测试

#### NewTest-70：为完全无直测的 `Bind_SystemTimers.cpp` 建立 timer lifecycle / world-context 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_SystemTimers.cpp` |
| 关联函数 | `System::SetTimer()` / `System::IsTimerPausedHandle()` / `System::PauseTimerHandle()` / `System::UnPauseTimerHandle()` / `System::ClearAndInvalidateTimerHandle()` |
| 现有测试覆盖 | `Bindings/` 目录当前没有任何 `System::SetTimer` / `FTimerHandle` 的行为级测试；只有 `Examples/` 与 `Learning/` 下的教学样例提到 timer，用例并不属于 BindSystem 回归入口，也没有锁住 pause/unpause/clear 的返回语义 |
| 风险评估 | 这组绑定全部依赖 `FAngelscriptEngine::TryGetCurrentWorldContextObject()` 和 `UKismetSystemLibrary::K2_*Timer*`。一旦 world-context 传递错误、handle by-ref 失效、pause/unpause silently no-op，脚本运行时会直接出现“计时器不触发”或“清理后仍继续回调”的高频运行时问题，而当前 Bindings 测试不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.SystemTimersLifecycleCompat` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSystemTimerBindingsTests.cpp` |
| 场景描述 | 使用 `FActorTestSpawner`/`FAngelscriptTestFixture` 建一个可 tick 的测试 world，编译 `ABindingSystemTimerActor` 脚本类。脚本类包含 `LoopCount`、`SingleCount`、`FTimerHandle LoopHandle`、`FTimerHandle SingleHandle` 四个 `UPROPERTY`，`BeginPlay()` 中分别调用 `System::SetTimer(this, n"OnLoopTimer", 0.05f, true)` 和 `System::SetTimer(this, n"OnSingleTimer", 0.05f, false)`；再暴露 `PauseLoopTimer()`、`ResumeLoopTimer()`、`ClearLoopTimer()`、`IsLoopPaused()` 四个 `UFUNCTION()` 用来包装对应的 `System::*Handle` API。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `FAngelscriptTestFixture(ProductionLike)`；配合 `FActorTestSpawner` 生成 world，并在调用脚本函数与 `TickWorld(...)` 时用 `FScopedTestWorldContextScope`/现有 scenario helper 确保 actor 是当前 world context。C++ 侧通过反射读取 `LoopCount`、`SingleCount` 和 `LoopHandle`，并用固定步长 `TickWorld(World, 0.016f, N)` 推进时间。 |
| 期望行为 | 1. `BeginPlay()` 后、首轮 tick 前，`LoopCount == 0 && SingleCount == 0`。2. 推进约 `0.12s` 后，`SingleCount == 1`，`LoopCount >= 1`。3. 调用 `PauseLoopTimer()` 后，`IsLoopPaused()` 返回 `true`，再推进若干 tick，`LoopCount` 不再增长。4. 调用 `ResumeLoopTimer()` 后，`IsLoopPaused()` 返回 `false`，继续 tick 时 `LoopCount` 恢复增长。5. 调用 `ClearLoopTimer()` 后，C++ 侧读取 `LoopHandle.IsValid() == false`，再推进足够 tick，`LoopCount` 保持不变，证明 `ClearAndInvalidateTimerHandle()` 同时清除了 world timer 和脚本侧 handle。 |
| 使用的 Helper | `FAngelscriptTestFixture`、`FActorTestSpawner`、`AngelscriptScenarioTestUtils::CompileScriptModule()`、`BeginPlayActor()`、`TickWorld()`、`FScopedTestWorldContextScope` |
| 优先级 | P0 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-97 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 本轮已补齐对当前目录全部测试文件的全文复核；任务描述中的“24 个测试文件”与仓库实物不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物统计；任务描述中的“126 个 Bind_*.cpp”与仓库实物不符 |
| 本轮新增识别为“已测但缺 native parity 断言”的测试 | 1 | `BlueprintCallableReflectiveFallback.UMG` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_SystemTimers.cpp` |


---

## 测试审查 (2026-04-10 00:17) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-10 00:17)` 小节。真正新增项为 `Issue-95`、`Issue-96`、`NewTest-68`、`NewTest-69`；前文已保留正文，这里只补实际 EOF 汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-96 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数；任务描述中的“24 个测试文件”与仓库不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数；任务描述中的“126 个 Bind_*.cpp”与仓库不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 17 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 84 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“已测但断言仍缺正确失败契约”的测试 | 2 | `ArrayMutationEdgeCases`、`ConsoleCommandSignatureCompat` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_APlayerController.cpp`、`Bind_UPrimitiveComponent.cpp` |

---

## 测试审查 (2026-04-10 00:17)

### 一、现有测试问题

#### Issue-95：`ArrayMutationEdgeCases` 只要求“抛出任意脚本异常”，没有锁住 self-alias 保护的具体失败契约

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptArrayEdgeBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ArrayMutationEdgeCases` |
| 行号范围 | 19-65, 163-174 |
| 问题描述 | `ExecuteFunctionExpectingScriptException()` 目前只断言 `PrepareResult == asSUCCESS`、`ExecuteResult == asEXECUTION_EXCEPTION`、异常字符串非空、行号大于 0。也就是说，`TriggerSelfAliasAdd()` / `TriggerSelfAliasInsert()` 只要因为任意运行时异常失败，这个测试就会通过；它并没有把失败原因锁定为 `Bind_TArray.cpp` 782 行与 983 行 self-alias 保护抛出的明确契约（`"Cannot move assign an array into itself."` / `"Cannot copy an array into itself."`）。如果后续回退成越界、空引用、错误重载或其他无关异常，当前用例仍会给绿灯。 |
| 影响 | 这条新补的边界回归本来就是为了保护 `Bind_TArray.cpp` 的 self-alias 防护；现在却只验证“某处炸了”，无法证明确实命中了正确的 guard 分支。测试一旦因为错误原因变红/变绿，诊断价值会明显下降。 |
| 修复建议 | 扩展 helper，把 `ExpectedExceptionSubstring` 作为参数传入，并对 `ScriptContext->GetExceptionString()` 做 `Contains(...)` 级断言；`TriggerSelfAliasAdd()` 期望命中 `Cannot move assign an array into itself.`，`TriggerSelfAliasInsert()` 期望命中 `Cannot copy an array into itself.`。同时把 `GetExceptionFunction()->GetName()` 或至少 `ContextLabel` 对应的函数声明纳入断言，避免 unrelated runtime exception 冒充 self-alias 保护。 |

#### Issue-96：`ConsoleCommandSignatureCompat` 只验证“执行没 finished”，没有把失败原因钉死到签名校验分支

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ConsoleCommandSignatureCompat` |
| 行号范围 | 447-503 |
| 问题描述 | 这条用例已经通过 `AddExpectedError(...)` 捕获日志片段，但行为级断言仍然只有 `ExecuteResult != asEXECUTION_FINISHED` 和“命令未注册”。对应源码 `Bind_Console.cpp` 83-96 行实际上定义了更具体的失败契约：找不到 `void <Name>(const TArray<FString>& Args)` 时应抛出 `"Global function for console command must have signature ..."`。当前测试没有检查 `Context->GetExceptionString()` 是否包含这段核心消息，也没有要求 `ExecuteResult` 精确等于 `asEXECUTION_EXCEPTION`。因此，只要 `Entry()` 因为别的运行时错误提前失败，测试仍然会通过。 |
| 影响 | 该用例名义上保护 console command 签名校验，但实际上只证明“构造期间出错了”。如果后续 `Bind_Console.cpp` 在函数查找、lambda 注册、模块解析或其他无关路径上抛出异常，测试会误报为签名校验仍正常。 |
| 修复建议 | 在保留现有 `AddExpectedError` 的同时，把 `Context->GetExceptionString()` 读出来并断言其包含稳定片段 `Global function for console command must have signature`；同时将 `bExecutionFailed` 收紧为 `ExecuteResult == asEXECUTION_EXCEPTION`。更稳妥的做法是再断言 `Context->GetExceptionLineNumber()` 指向 `const FConsoleCommand Command(...)` 这一行，证明失败发生在命令构造路径，而不是其它脚本语句。 |

### 二、需要新增的测试

---

## 测试审查 (2026-04-09 23:59) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-09 23:59)` 小节。真正新增项为 `Issue-94`、`NewTest-66`、`NewTest-67`；由于文档前部存在重复锚点，本轮正文追加命中了旧位置，这里只补实际 EOF 汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-94 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 1 | MissingScenario: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮再次逐文件全文复核完成，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 1 | `Bind_FRandomStream.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_UGameInstance.cpp`、`Bind_ULocalPlayer.cpp` |

---

## 测试审查 (2026-04-09 23:59)

### 一、现有测试问题

#### Issue-94：`NativeStaticClassNamespace` 的 `userdata` 精确断言没有并入最终结果

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeStaticClassNamespace` |
| 行号范围 | 438-447 |
| 问题描述 | 用例在拿到 `asIScriptFunction* StaticClassFunction` 后，确实调用了 `TestEqual(... static_cast<UClass*>(StaticClassFunction->GetUserData()), AActor::StaticClass())` 检查 `StaticClass()` 绑定携带的 `userdata`；但最终 `bPassed` 仍然只等于 `bHasFunction`。也就是说，只要函数存在，哪怕 `GetUserData()` 指向了错误的 `UClass`，`RunTest` 也会按成功路径返回。 |
| 影响 | 该测试名义上覆盖“native class namespace 下的 `StaticClass` 绑定”，实际上并没有把最关键的 `UClass` 关联关系锁成红灯条件。若 `Bind_UClass` / static namespace 注册把函数挂到了错误类型，当前用例会给出假绿，后续脚本通过 namespace 调 `StaticClass()` 时才会暴露真实问题。 |
| 修复建议 | 把 `TestEqual(...)` 的返回值单独保存为 `const bool bUserdataMatches = TestEqual(...);`，并并入最终返回；更稳妥的写法是把该用例改成多个显式布尔汇总，例如 `bPassed = bHasFunction && bUserdataMatches && bRestoredNamespace`，避免再出现“断言执行了但不参与最终结果”的漏计。 |

### 二、需要新增的测试

#### NewTest-66：为 `Bind_FRandomStream.cpp` 建立固定种子序列与 `Initialize(FName)` 的 parity 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FRandomStream.cpp` |
| 关联函数 | `FRandomStream(int32)` / `FRandomStream(uint32)` / `Initialize(FName)` / `GetUnsignedInt()` / `RandRange(int32, int32)` / `RandRange(float64, float64)` / `GetFraction()` / `GetCurrentSeed()` / `Reset()` |
| 现有测试覆盖 | `RandomStreamCompat` 只验证区间与“Reset 后第一值相同”，没有把脚本序列与原生 `FRandomStream` 的固定种子结果逐步对齐，也没有触达 late bind 的 `Initialize(FName)` |
| 风险评估 | `Bind_FRandomStream.cpp` 同时绑定了 `int32/uint32` ctor、`Initialize(FName)` 和多组随机数 API。若某个 overload 串绑、内部 seed 推进顺序不一致，现有测试只会看到“仍然像随机数”，无法第一时间报警。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.RandomStreamSequenceParity` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptRandomStreamBindingsTests.cpp` |
| 场景描述 | C++ 侧先用固定输入 `FRandomStream NativeIntSeed(123)`、`FRandomStream NativeUintSeed(uint32(123))`、`FRandomStream NativeNameSeed; NativeNameSeed.Initialize(FName("RandomSeedName"));` 计算原生基线：`GetUnsignedInt()`、`RandRange(1, 1000)`、`GetFraction()`、`RandRange(0.0, 10.0)`、序列执行后的 `GetCurrentSeed()`、`Reset()` 后首个 `RandRange(1, 1000)`，以及 `Initialize(FName)` 后的首个 `RandRange(1, 1000)`。脚本侧按完全相同的调用顺序执行，并额外在序列中途做一次 `FRandomStream Copy = Stream;`，验证副本与原对象的下一次 `RandRange` 完全一致。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；C++ 侧把所有基线数值通过 `FString::Printf` 注入脚本；脚本固定使用 `123`、`uint(123)` 和 `n"RandomSeedName"` 作为种子输入，避免任何宿主环境噪声。 |
| 期望行为 | 1. `FRandomStream(123)` 与 `FRandomStream(uint(123))` 的 `GetInitialSeed()` / `GetCurrentSeed()` 初值都与原生基线精确一致。2. `GetUnsignedInt()`、`RandRange(1, 1000)`、`GetFraction()`、`RandRange(0.0, 10.0)`、执行后的 `GetCurrentSeed()` 必须逐项等于 C++ 原生结果，浮点使用 `IsNearlyEqual(..., 1e-6)`。3. `Copy = Stream` 之后，`Copy.RandRange(1, 1000)` 与 `Stream.RandRange(1, 1000)` 必须返回同一个预期值，证明复制语义复制的是内部状态而不是共享状态。4. `Reset()` 后再次 `RandRange(1, 1000)` 必须回到首个原生值。5. `Initialize(FName("RandomSeedName"))` 后的当前 seed 和首个 `RandRange(1, 1000)` 必须与原生 `Initialize(FName)` 路径一致。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`、`BuildModule()`、`ExecuteIntFunction()` |
| 优先级 | P1 |

#### NewTest-67：为完全无直测的 `Bind_UGameInstance.cpp` / `Bind_ULocalPlayer.cpp` 建立 local-player 生命周期回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UGameInstance.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_ULocalPlayer.cpp` |
| 关联函数 | `UGameInstance::CreateLocalPlayer(int32, FString&, bool)` / `RemoveLocalPlayer()` / `GetNumLocalPlayers()` / `GetLocalPlayerByIndex()` / `FindLocalPlayerFromControllerId()` / `GetFirstGamePlayer()` / `GetFirstLocalPlayerController()` / `ULocalPlayer::GetWorld()` / `GetGameInstance()` / `GetControllerId()` |
| 现有测试覆盖 | `Bindings/` 目录里没有任何 `UGameInstance` / `ULocalPlayer` 行为级用例；当前只有更外围系统测试会间接依赖这些对象存在，没有直接锁住绑定返回值语义 |
| 风险评估 | local-player 创建、索引查找和移除是运行时高频路径。若 `CreateLocalPlayer` 的 out 参数、返回对象、world/game instance 关联或 `RemoveLocalPlayer` 回收语义回退，当前 Bindings 套件没有任何直接红灯。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GameInstanceLocalPlayerCompat` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameInstanceLocalPlayerBindingsTests.cpp` |
| 场景描述 | C++ 侧用 `ASTEST_CREATE_ENGINE_FULL()` 建立带有效 `UWorld` / `UGameInstance` 的测试环境，保存 `WorldPath`、`GameInstancePath` 和 `InitialLocalPlayerCount`，并用 `FScopedTestWorldContextScope(TestWorld)` 建立 world context。脚本通过 `FindObject(WorldPath)` / `FindObject(GameInstancePath)` 取得同一组对象，调用 `CreateLocalPlayer(7, OutError, false)` 创建不生成 controller 的 local player，然后依次验证 `GetNumLocalPlayers()`、`GetLocalPlayerByIndex()`、`FindLocalPlayerFromControllerId()`、`GetFirstGamePlayer()`、`ULocalPlayer::GetWorld()`、`ULocalPlayer::GetGameInstance()`、`ULocalPlayer::GetControllerId()`；最后调用 `RemoveLocalPlayer()` 并回读数量和查找结果。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_FULL()`、`FScopedTestWorldContextScope`、`ON_SCOPE_EXIT`；C++ 侧记录初始 local player 数量，并在测试失败时兜底清理 controller id 为 `7` 的 local player；脚本固定使用 `ControllerId = 7`，`bSpawnPlayerController = false`，避免引入额外 controller 生成依赖。 |
| 期望行为 | 1. `CreateLocalPlayer(7, OutError, false)` 返回非空，且 `OutError.IsEmpty()`。2. `GetNumLocalPlayers()` 必须等于 `InitialLocalPlayerCount + 1`。3. `GetLocalPlayerByIndex(InitialLocalPlayerCount)`、`FindLocalPlayerFromControllerId(7)` 与 `GetFirstGamePlayer()` 返回的对象必须与新建 local player 一致。4. `Created.GetGameInstance()` 必须等于脚本拿到的 `UGameInstance`，`Created.GetWorld()` 必须等于注入的 `UWorld`，`Created.GetControllerId()` 必须等于 `7`。5. 因为 `bSpawnPlayerController = false`，`GetFirstLocalPlayerController(World)` 应与 C++ 原生基线一致地返回 `null`。6. `RemoveLocalPlayer(Created)` 返回 `true` 后，`GetNumLocalPlayers()` 恢复到初始值，`FindLocalPlayerFromControllerId(7)` 返回 `null`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL()`、`FScopedTestWorldContextScope`、`BuildModule()`、`ExecuteIntFunction()`、`ON_SCOPE_EXIT` |
| 优先级 | P0 |

---

## 测试审查 (2026-04-09 23:48)

### 一、现有测试问题

#### Issue-93：`ObjectCastCompat` 把 `Cast<T>` 和 `n""` literal 两类兼容语法塞进同一个返回码用例，失败诊断被混在一起

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ObjectCastCompat` |
| 行号范围 | 38-50，89-97 |
| 问题描述 | plain module 一边验证 `Cast<UPackage>(Object)`，一边顺手验证 `n"Compat_Name"`；annotated module 又把 `Cast<ABindingCastActor>(GetOwner())` 和 `n"BindingCastOwner"` 放在同一个 `ReadCastCompat()` 里，而且两段脚本失败都统一 `return 0`。这样一来，`Cast<T>` bridge、`n""` compat literal、annotated/native class cast 三类不同能力被压缩成单个结果码，红灯时无法直接判断到底是哪一层回退。 |
| 影响 | 该用例名义上覆盖 `ObjectCastCompat`，实际上把不相干的 compat surface 绑在一起。任何一次失败都会落成同一个 `Result != 1`，既削弱定位效率，也容易让后续维护者误以为 `Cast<T>` 与 `FName` literal 语法已经被独立锁住。 |
| 修复建议 | 把 `Cast<T>` compat 和 `n""` literal compat 拆成两个 focused 用例，至少拆成两个独立脚本入口并分配不同失败码；若保留在同文件，也应把 plain/annotated 两段中的 `Cast` 断言与 `FName` literal 断言分离为单独 `TestEqual`，避免一个返回码同时承载多类语义。 |

### 二、需要新增的测试

---

## 测试审查 (2026-04-09 23:33)

### 一、现有测试问题

#### Issue-91：`OptionalCompat` 只读取副本值，未验证 copy / `opAssignValue` 是否错误共享底层状态

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.OptionalCompat` |
| 行号范围 | 68-87 |
| 问题描述 | 用例先执行 `TOptional<int> Copy(Empty);`，随后把 `Copy = 19; Copy.Reset();`，但整个过程中从未再次读取原始 `Empty`。这意味着测试虽然看起来覆盖了 `CopyConstruct`、`OpAssign`、`OpAssignValue`、`Reset`，实际上并没有验证 `Bind_TOptional.h` 107-170 行这些绑定是否正确维持值语义。若绑定错误地让副本和源对象共享同一底层存储，`Copy = 19` 或 `Copy.Reset()` 把 `Empty` 一并改坏，当前用例仍然会继续绿灯。 |
| 影响 | `TOptional` 最基础的值语义一旦退化成别名共享，脚本业务会在“修改副本污染源值”这类隐蔽场景下出错，而现有 `OptionalCompat` 无法提供任何诊断信号。 |
| 修复建议 | 在现有用例里补源对象回读断言：`TOptional<int> Copy(Empty); Copy = 19;` 后必须断言 `Empty.IsSet()` 且 `Empty.GetValue() == 42`；`Copy.Reset()` 后继续断言 `!Copy.IsSet()` 且 `Empty` 仍保持 `42`。同时补 `TOptional<FName> OptionalNameCopy(OptionalName)` 后修改副本，验证原始 `OptionalName` 未被污染，真正把 copy / assign 的独立性锁住。 |

#### Issue-92：`SetCompat` 修改副本却从不回读源集合，`TSet` 复制独立性实际上没有被验证

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SetCompat` |
| 行号范围 | 141-155 |
| 问题描述 | 用例在 `TSet<int> Copy = Empty;` 之后，连续对 `Copy` 执行 `Add(7)`、`Remove(4)`、`Reset()`，但从头到尾都没有再次检查源集合 `Empty`。对应 `Bind_TSet.cpp` 459-480 行的 `Assign` 需要把源集合逐元素复制到新容器；如果绑定意外做成了共享底层 `FScriptSet`、浅拷贝句柄或复用了同一缓冲区，只要 `Copy` 自己的读写结果看起来正确，当前测试就发现不了 `Empty` 已被同步改坏。 |
| 影响 | 这会把 `TSet` 的副本独立性留在裸奔状态。实际脚本里一旦出现“复制一份集合再局部修改”的常见用法，源集合被误改会造成非常隐蔽的数据污染，而自动化仍会误报覆盖充分。 |
| 修复建议 | 在 `Copy.Add(7)`、`Copy.Remove(4)`、`Copy.Reset()` 三个步骤后分别补对源集合 `Empty` 的精确断言，例如持续检查 `Empty.Num() == 1 && Empty.Contains(4) && !Empty.Contains(7)`。若要进一步锁住 `opAssign`，再显式增加 `TSet<int> Assigned; Assigned = Empty;` 场景，并在修改 `Assigned` 后验证 `Empty` 完全不变。 |

### 二、需要新增的测试

#### NewTest-61：为 `Bind_TSet.cpp` 补齐 append / copy-isolation / `Empty(slack)` 语义回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp` |
| 关联函数 | `Append(const TArray<T>&)` / `Append(const TSet<T>&)` / `opAssign(const TSet<T>&)` / `Empty(int32 Slack)` |
| 现有测试覆盖 | `SetCompat` 只覆盖 `Add` / `Contains` / `Remove` / `Reset` 的基础 happy path；`SetCompareCompat` 和 `SetForeach` 只分别覆盖 equality 与遍历，没有任何用例验证 append 与副本独立性 |
| 风险评估 | 如果 `AppendArray` / `AppendSet` 漏掉去重、`Assign` 退化成浅拷贝、或 `Empty(slack)` 在清空后错误保留脏元素，当前测试树不会给出任何直接告警 |
| 建议测试名 | `Angelscript.TestModule.Bindings.SetAppendAndCopyIsolationCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptSetBindingsAdvancedTests.cpp` |
| 场景描述 | 脚本先用 `TArray<int>` 和 `TSet<int>` 作为两个来源，验证 `Append(Array)` 与 `Append(Set)` 合并后仍按 set 语义去重；随后把结果复制到 `Copy` 和 `Assigned`，分别修改副本，确认源集合完全不变；最后调用 `Empty(8)` 并验证集合被清空但不会残留旧元素 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；构造 `SourceSet={1,4}`、`SourceArray=[4,7,7]`、`Combined` 空集合；再创建 `Copy=Combined` 与 `Assigned` 后做 `Assigned = Combined`。修改 `Copy`/`Assigned` 时使用 `Add(9)`、`Remove(1)`、`Empty(8)` 这类可观察操作 |
| 期望行为 | `Combined.Append(SourceArray)` 后必须满足 `Num()==2` 且仅含 `4/7`；再 `Combined.Append(SourceSet)` 后必须满足 `Num()==3` 且含 `1/4/7`；修改 `Copy` 和 `Assigned` 后，`Combined` 必须始终保持 `1/4/7` 不变；`Empty(8)` 后副本必须 `IsEmpty()==true` 且 `Contains(1/4/7)==false` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-62：为 `Bind_UCollisionProfile.cpp` 建立 object / trace channel 转换 round-trip 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UCollisionProfile.cpp` |
| 关联函数 | `ConvertToCollisionChannel(bool TraceType, int32 Index)` / `ConvertToObjectType(ECollisionChannel)` / `ConvertToTraceType(ECollisionChannel)` |
| 现有测试覆盖 | 完全无直测；当前 `Bindings/` 目录只在 `GlobalVariableCompat` 里顺带读取 `CollisionProfile::BlockAllDynamic` 常量，没有覆盖 `UCollisionProfile` 命名空间下的任何转换 API |
| 风险评估 | 这些转换函数是 trace/object query 与 `ECollisionChannel` 互转的公共入口；一旦 bool 路径、索引解释或 enum 映射回退，脚本碰撞查询会直接拿错 channel，而当前自动化没有任何防线 |
| 建议测试名 | `Angelscript.TestModule.Bindings.CollisionProfileChannelConversions` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCollisionProfileBindingsTests.cpp` |
| 场景描述 | C++ 侧先调用原生 `UCollisionProfile::Get()` 计算一组稳定基线，例如 `ECC_WorldStatic` 的 object query、`ECC_Visibility` 的 trace query，以及它们再经 `ConvertToCollisionChannel(false/true, Index)` 的 round-trip 结果；脚本侧对同一组 channel 调用 `UCollisionProfile::ConvertToObjectType`、`ConvertToTraceType` 与 `ConvertToCollisionChannel`，逐项与原生基线比对 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；在 C++ 侧预先求出 `WorldStaticObjectType`、`VisibilityTraceType`、`RoundTripWorldStatic`、`RoundTripVisibility` 等原生结果，并把期望 enum 数值通过 `FString::Printf` 注入脚本；优先选择引擎内建 channel，避免项目自定义配置干扰 |
| 期望行为 | 脚本侧 `ConvertToObjectType(ECC_WorldStatic)` 与原生 object query 完全一致，`ConvertToTraceType(ECC_Visibility)` 与原生 trace query 完全一致；再把两者的索引传回 `ConvertToCollisionChannel(false/true, Index)` 后，必须分别精确回到 `ECC_WorldStatic` 与 `ECC_Visibility`，不能出现 bool 分支走错或 off-by-one 映射 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P0 |

---

## 测试审查 (2026-04-09 03:09)

### 一、现有测试问题

#### Issue-86：`ConsoleCommandCompat` 只校验参数个数，无法证明 `Bind_Console.cpp` 正确转发命令参数内容与顺序

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ConsoleCommandCompat` |
| 行号范围 | 274-338 |
| 问题描述 | 用例注册的 `OnCommand(const TArray<FString>& Args)` 只把 `Args.Num()` 写进 `Output` cvar，C++ 侧也只断言最终值为 `3`。而 `Bind_Console.cpp` 108-123 行实际桥接的是整份 `TArray<FString>` 参数；如果绑定回退成“长度正确但内容丢失/顺序颠倒/字符串被截断”，当前测试仍会稳定通过。 |
| 影响 | `FConsoleCommand` 最关键的 payload marshalling 仍停留在 smoke test 级别。脚本命令一旦在参数顺序、首尾元素或字符串内容上传错，当前自动化不会第一时间报警。 |
| 修复建议 | 把脚本 handler 改成记录可观察的 payload，而不是只写 `Args.Num()`：例如额外注册两个 `FConsoleVariable`/一个字符串 sink，把 `Args[0]`、`Args.Last()` 或 `Args.Join(",")` 写出；C++ 侧执行 `["One","Two","Three"]` 后精确断言 `"One"`、`"Three"` 或 `"One,Two,Three"`。同时补一条空参数执行，确认 `Args.Num()==0` 时不会复用上次内容。 |

#### Issue-87：`ConsoleCommandReplacementCompat` 没有先验证原始 handler 生效，测不出“注册失败但 replacement 恰好成功”的假绿

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ConsoleCommandReplacementCompat` |
| 行号范围 | 345-440 |
| 问题描述 | 用例先构建 `ASConsoleCommandOriginalCompat`，再立刻构建 replacement 模块并只在 replacement 注册后执行一次命令，最终断言 `Output == 22`。它从未在 replacement 之前执行原始命令，也没有检查原 handler 是否真的注册并可调用；因此只要第二次注册路径工作正常，哪怕第一次 `FConsoleCommand` 构造根本没生效，测试仍然会通过。 |
| 影响 | 该用例名义上覆盖“替换”语义，实际只锁住了第二个 handler 的 happy path，无法证明 `Bind_Console.cpp` 在重名命令场景下经历了“原始注册成功 -> replacement 覆盖成功”这一完整状态转移。 |
| 修复建议 | 在加载 replacement 之前先执行一次原始命令，并精确断言输出为 `11`；随后加载 replacement，再次执行并断言输出切换为 `22`。若绑定设计要求 discarding replacement 后命令消失，还应在 `Engine.DiscardModule("ASConsoleCommandReplacementCompat")` 后额外执行一次并验证确实不可调用，而不是只检查对象是否缺席。 |

#### Issue-88：`ConsoleCommandSignatureCompat` 把诊断位置硬编码到 `Line 8 | Col 2`，对无关排版和错误格式变化过于敏感

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ConsoleCommandSignatureCompat` |
| 行号范围 | 447-506 |
| 问题描述 | 用例除了匹配核心报错文本外，还额外 `AddExpectedError(TEXT("int Entry() | Line 8 | Col 2"), ...)`。这个断言绑定到了内嵌脚本的当前排版和编译器日志格式，只要脚本示例多一行空行、错误定位格式从 `Line/Col` 改成别的文本，哪怕错误语义完全正确，测试也会假失败。 |
| 影响 | `Bind_Console.cpp` 对签名不匹配的报错语义本身是稳定的，但当前用例会因为无关的文本布局调整产生脆弱红灯，增加维护成本并稀释真正的绑定回归信号。 |
| 修复建议 | 期望错误只保留稳定的语义片段，例如 `"Global function for console command must have signature"` 与模块名，去掉精确 `Line/Col` 匹配；若需要定位具体脚本位置，改成从 `ExecuteResult != asEXECUTION_FINISHED` 和“命令未注册”这两个行为级断言来证明失败发生在构造路径，而不是把日志格式当作契约。 |

#### Issue-89：`ObjectEditorOnlyCompat` 只测 transient package 的负路径，几乎不能证明 `UObject::IsEditorOnly()` 绑定语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ObjectEditorOnlyCompat` |
| 行号范围 | 144-180 |
| 问题描述 | 当前脚本只有 `UPackage Package = GetTransientPackage(); if (Package.IsEditorOnly()) return 10;` 这一条负向断言。`Bind_UObject.cpp` 44 行绑定的是通用 `UObject::IsEditorOnly()`，但测试既没有构造一个明确的 editor-only 对象/包来验证正路径，也没有对比原生 `IsEditorOnly()` 结果；只要 transient package 继续返回 `false`，绑定即使在 editor-only 标记传播上回退也不会被发现。 |
| 影响 | `ObjectEditorOnlyCompat` 目前只能证明“一个非 editor-only 对象还是 false”，无法覆盖真正有风险的正路径和标志传播语义，容易把 `Bind_UObject.cpp` 的 editor-only 绑定误判为已测。 |
| 修复建议 | 在 C++ 侧创建一个临时 `UPackage` 并显式设置 `PKG_EditorOnly`，把其路径或实例传给脚本，同时保留 `GetTransientPackage()` 作为对照组；脚本侧分别断言 editor-only 包为 `true`、transient 包为 `false`，并把结果与 C++ 原生 `IsEditorOnly()` 基线做精确对齐。若项目约束不允许改包旗标，也应至少引入一个已知 editor-only 的 native 对象作为正样本。 |

#### Issue-90：`FNameArrayCompat` 只读取数组元素，未验证 `Bind_TArray.cpp` 的可写 `opIndex` 在 `FName` alias 场景下真的回写底层数组

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.FNameArrayCompat` |
| 行号范围 | 93-137 |
| 问题描述 | 当前用例只做了 `Copy = AliasValues[0]`、`ExplicitValues.Add(Copy)`、`AliasValues[1] == n"Gamma"`、`Contains(...)` 这类只读断言。它声称覆盖 “copy, index, alias, and add operations”，但从头到尾没有任何一次通过 `AliasValues[Index]` 或 `ExplicitValues[Index]` 写回新值。`Bind_TArray.cpp` 1510-1517 行绑定的是可变 `T& opIndex(...)`；如果这个 mutable index 在 `FName` alias 场景下回退成返回临时值或丢失写回，当前测试仍会稳定通过。 |
| 影响 | `FName` 字面量 `n""` 与 `TArray<FName>` 组合时，最容易回退的是“读看起来正常、写不回去”的引用语义。现有测试只有读取，没有任何护栏覆盖可写索引这一真实业务路径。 |
| 修复建议 | 在同一用例里增加可观察写回：例如先保存 `FName Copy = AliasValues[0]`，再执行 `AliasValues[0] = n"Omega"; ExplicitValues[0] = n"Sigma";`，随后断言数组元素分别变成 `Omega/Sigma`，同时 `Copy` 仍保持 `Alpha`，以区分“值拷贝仍稳定”和“opIndex 确实写回底层数组”。若脚本支持引用局部变量，可再补一条 `FName& NameRef = AliasValues[1]; NameRef = n"Delta";` 的 direct ref 写回。 |

注：本节前面追加的 `Issue-86`、`Issue-87`、`Issue-88`、`Issue-89` 经复核分别与前文 `Issue-66`、`Issue-16`、`Issue-62`、`Issue-65` 重复。受“只追加不覆盖”约束，原文保留，但本轮汇总不将这 4 条计入新增统计。

### 二、需要新增的测试

#### NewTest-58：为 `Bind_Console.cpp` 增加 original -> replacement -> unload 的完整命令生命周期回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.cpp` |
| 关联函数 | `FScriptConsoleCommand::FScriptConsoleCommand(const FString& Name, const FString& FunctionName)` / `~FScriptConsoleCommand()` |
| 现有测试覆盖 | `ConsoleCommandReplacementCompat` 只验证 replacement 后输出变成 `22`，没有覆盖 original handler 真正生效和 unload 后生命周期语义 |
| 风险评估 | 如果重名命令其实没有完成“先注册 original，再被 replacement 接管”，或 replacement unload 后残留悬空命令，当前自动化不会给出定向信号 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ConsoleCommandReplacementLifecycle` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleCommandLifecycleBindingsTests.cpp` |
| 场景描述 | 先编译 `ASConsoleCommandOriginalLifecycle` 并执行一次命令，确认输出 sink 变成 `11`；再编译同名 replacement 模块、再次执行命令，确认输出变成 `22`；最后丢弃 replacement 模块并验证命令不再可执行或已从 `IConsoleManager` 注销 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；C++ 侧生成唯一命令名和输出 cvar 名，复用现有 `ExecuteConsoleCommand` / `VerifyConsoleCommandExists` / `VerifyConsoleCommandMissing` helper；original/replacement 两个脚本 handler 分别写入 `11` 和 `22` |
| 期望行为 | original 编译后命令必须存在且执行一次后输出为 `11`；replacement 编译后再次执行必须把同一个输出 sink 更新为 `22`；`Engine.DiscardModule("ASConsoleCommandReplacementLifecycle")` 后命令必须消失或明确不可执行，不能保留野指针或继续调用旧 handler |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + 现有 `ExecuteConsoleCommand` / `VerifyConsoleVariableInt` / `VerifyConsoleCommandMissing` helper |
| 优先级 | P1 |

#### NewTest-59：为 `Bind_TArray.cpp` 补齐 `FName` 数组可写 `opIndex` 与 copy/alias 分离语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp` |
| 关联函数 | `TArray<T>::opIndex(int)` / `TArray<T>::Add(const T&)` / `TArray<T>::Contains(const T&)` |
| 现有测试覆盖 | `FNameArrayCompat` 只做读取和 `Contains`，没有任何一次通过数组索引写回 `FName` |
| 风险评估 | 如果 `opIndex` 在 `FName` alias 场景下回退成返回临时值或写回丢失，当前所有 Bindings 用例都会继续绿灯，业务脚本只有在修改数组元素时才会晚暴露 |
| 建议测试名 | `Angelscript.TestModule.Bindings.FNameArrayIndexWriteBackCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` |
| 场景描述 | 复用现有 `FName[]` / `TArray<FName>` 样本，先保存 `Copy = AliasValues[0]`，再通过 `AliasValues[0] = n"Omega"`、`ExplicitValues[0] = n"Sigma"` 或引用局部变量把新值写回数组，最后同时检查数组元素、`Contains()` 结果和旧副本 `Copy` 的稳定性 |
| 输入/前置 | 使用现有 `ASTEST_CREATE_ENGINE_SHARE` 模式即可；脚本准备 `AliasValues = {Alpha, Beta}`、`ExplicitValues = {Gamma}` 和一个 `FName Copy = AliasValues[0]`，随后执行索引写回 |
| 期望行为 | 写回后 `AliasValues[0] == n"Omega"`、`ExplicitValues[0] == n"Sigma"`、`AliasValues.Contains(n"Omega") == true`、`ExplicitValues.Contains(n"Sigma") == true`；同时 `Copy` 仍应等于 `n"Alpha"`，证明值拷贝没有意外别名化 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-60：为 `Bind_FMemoryReader.cpp` 建立 seek/skip/read 与越界报错的专用回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMemoryReader.cpp` |
| 关联函数 | `FMemoryReader(const TArray<uint8>&, bool)` / `TotalSize()` / `Tell()` / `Seek(int)` / `Skip(int)` / `ReadInt32()` / `ReadUInt16()` / `ReadBytes(int)` / `ReadAnsiString(int)` |
| 现有测试覆盖 | `Bindings/` 目录里没有任何 `FMemoryReader` 相关用例 |
| 风险评估 | 该绑定同时覆盖位置管理、二进制读取和越界错误路径；如果 `Seek/Skip` 越界检查失效、`Tell()` 不更新，或 `ReadAnsiString()` 长度处理错误，当前完全没有测试信号 |
| 建议测试名 | `Angelscript.TestModule.Bindings.MemoryReaderCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMemoryReaderBindingsTests.cpp` |
| 场景描述 | C++ 侧构造固定字节数组，脚本用 `FMemoryReader` 依次验证 `TotalSize/Tell`、`ReadUInt8/ReadUInt16/ReadInt32`、`Seek`、`Skip`、`ReadBytes` 和 `ReadAnsiString` 的确定性输出；再单独执行一条越界 `Skip` 或 `Seek`，捕获 `"Skipping past array bounds"` 错误并验证游标没有继续前进 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；字节样本可直接在脚本中声明 `TArray<uint8>`，例如 `{ 0x41, 0x42, 0x10, 0x00, 0x78, 0x56, 0x34, 0x12 }`；越界错误路径用 `AddExpectedError(TEXT("Skipping past array bounds"), EAutomationExpectedErrorFlags::Contains, 1)` 捕获，并在执行前后读出 `Tell()` |
| 期望行为 | `TotalSize()` 与样本字节数一致；顺序读取后 `Tell()` 精确递增；`Seek` 到合法位置后读取得到预期整型/字符串；`ReadBytes(Count)` 返回的字节序列与原样本完全一致；越界 `Seek/Skip` 必须触发预期错误且 `Tell()` 保持越界前的位置 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` / 手动 `asIScriptContext` 执行错误路径 |
| 优先级 | P0 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-90 |

补充说明：`Issue-86`、`Issue-87`、`Issue-88`、`Issue-89` 与前文重复，已在本轮统计中排除。

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 2 | MissingScenario: 2 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮重新全文复核，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 2 | `Bind_Console.cpp`、`Bind_TArray.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_FMemoryReader.cpp` |

### 二、覆盖方法学备注

`token direct-hit` 只能当粗筛上限，不能直接当成“已有行为级覆盖”。例如 `Bind_FString.cpp` 会因为脚本和 C++ helper 中广泛出现 `FString` 而被高频命中，`Bind_FMath.cpp` 也会因为测试辅助代码里的 `FMath::IsNearlyEqual` 被算进命中；因此 39 / 123 更适合解释为“可能有关联的 bind shard 数”，而不是“已经被充分测试的 bind shard 数”。

---

## 测试审查 (2026-04-09 02:05) 末尾索引-实际EOF

### 一、定位说明

本轮新发现已登记为 `Issue-83`、`Issue-84`、`Issue-85`、`NewTest-56`、`NewTest-57`。

由于文档前部存在重复的“覆盖快照”锚点，本轮正文命中了前文旧章节；原文已保留在文件前部，本节仅补真正位于 EOF 的索引与汇总，避免删除或覆盖已有内容。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-83 |
| FlakyRisk | 1 | Issue-84 |
| AntiPattern | 1 | Issue-85 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮再次全文复核，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 16 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中未见 token direct-hit | 84 / 123 | 这些 shard 在当前 `Bindings/` 目录内仍缺少任何直观命中入口 |

### 二、覆盖方法学备注

`token direct-hit` 只能当粗筛上限，不能直接当成“已有行为级覆盖”。例如 `Bind_FString.cpp` 会因为脚本和 C++ helper 中广泛出现 `FString` 而被高频命中，`Bind_FMath.cpp` 也会因为测试辅助代码里的 `FMath::IsNearlyEqual` 被算进命中；因此 39 / 123 更适合解释为“可能有关联的 bind shard 数”，而不是“已经被充分测试的 bind shard 数”。

---

## 测试审查 (2026-04-09 02:05)

### 一、现有测试问题

#### Issue-83：`NativeComponentMethods` 调用了 `Activate` / `Deactivate` 与 `ComponentHasTag`，却没有验证任何真实副作用

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeComponentMethods` |
| 行号范围 | 115-117, 167-168 |
| 问题描述 | 脚本里先后调用了 `Deactivate(); Activate();`，但整个用例从未检查组件激活状态是否真的发生切换；同样，`Bind_UActorComponent.cpp` 对 `ComponentHasTag()` 还专门对 `NAME_None` 做了特殊分支处理，而当前测试只在最后用 `return ComponentHasTag(NAME_None) ? 0 : 1;` 覆盖了一个负分支，完全没有验证真实 tag 命中路径。也就是说，`Activate` / `Deactivate` 即使被桥接成 no-op，或 `ComponentHasTag` 只剩“`NAME_None` 恒 false”这一特判，当前用例依旧会通过。 |
| 影响 | `Bind_UActorComponent.cpp` 中最容易因为 lambda 转发或特殊分支回退的两组 surface 仍停留在烟雾测试级别。测试名声称覆盖 “NativeComponentMethods”，但对组件激活语义和标签查询语义都没有形成可观察约束。 |
| 修复建议 | 把该用例拆成可观察的两段：1. 在 C++ 侧给 `RuntimeComponent` 预置一个真实 tag（例如 `Probe`），脚本中同时断言 `ComponentHasTag(n"Probe") == true` 与 `ComponentHasTag(NAME_None) == false`；2. 分别执行 `DeactivateOnly()` / `ActivateOnly(bool bReset)` 两个脚本入口，执行后在 C++ 侧精确断言 `RuntimeComponent->IsActive()` 的前后状态变化。若继续保留单入口，也至少要在调用前后由 C++ 读回 native active state，而不是只看一个最终返回码。 |

#### Issue-84：`SourceMetadataCompat` 把脚本源码写到固定工程路径，存在并发运行和重入污染风险

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SourceMetadataCompat` |
| 行号范围 | 202-217 |
| 问题描述 | 用例每次都把脚本写入固定路径 `Script/Automation/RuntimeSourceMetadataBindingsTest.as`，随后再用同一个模块名 `RuntimeSourceMetadataBindingsTest` 编译。虽然 `ON_SCOPE_EXIT` 会删除文件，但这仍然把测试结果绑定到了工程目录中的单个共享文件名上：一旦自动化并发执行、上次运行异常中断，或另一个用例恰好复用了同名脚本，当前测试读取到的 source metadata 就可能来自错误文件版本。 |
| 影响 | 这会把 `GetSourceFilePath()` / `GetScriptModuleName()` 的失败信号和真实绑定回归混在一起，制造只在并发、重跑或异常中断后出现的假红灯；同时也会污染 `Script/Automation/` 这类看起来像源码入口的目录，增加排查成本。 |
| 修复建议 | 把脚本落点改到 `Saved/Automation/` 或其他临时目录，并为文件名和模块名同时加上 `FGuid` 后缀；`RuntimeScript` 中的期望路径应改成运行时生成的唯一实际路径，而不是固定常量。这样既能保留 source metadata 的精确断言，也能避免跨进程/跨轮次共享同一个脚本文件。 |

#### Issue-85：`StringRemoveAtCompat` 用单个微型 happy path 代表整份 `Bind_FString.cpp`，覆盖口径明显失真

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.StringRemoveAtCompat` |
| 行号范围 | 303-316 |
| 问题描述 | 当前用例只验证了 `FString Value = "ABCDE"; Value.RemoveAt(1, 2);` 和随后一次头部删除，最终靠 `"ADE"` / `"DE"` 两个结果判定通过。问题在于 `Bind_FString.cpp` 绑定面非常大，除了 `RemoveAt` 之外还包含 `opIndex`、`Left/Right/Mid`、`Split`、`Replace`、`ParseIntoArray`、`Format/ApplyFormat` 等大量 surface；而本仓库里这个测试却是唯一专门点名字符串绑定的用例。结果就是 `Bind_FString.cpp` 在覆盖统计上看似“有专门测试”，实际上仍只是一个夹在 `UtilityBindingsTests` 里的单函数烟雾检查。 |
| 影响 | 维护者很容易把 `StringRemoveAtCompat` 误读成“字符串绑定已经有回归护栏”，从而低估 `Bind_FString.cpp` 的真实缺口；一旦 `RemoveAt` 之外的索引、切片、格式化或分隔逻辑回退，当前测试树几乎不会给出任何定向信号。 |
| 修复建议 | 不要再把 `Bind_FString.cpp` 依附在 `UtilityBindingsTests.cpp` 的单个 smoke case 上。建议新建 `AngelscriptStringBindingsTests.cpp`，把当前 `RemoveAt` happy path 保留为其中一条小用例，再补至少三组 focused 回归：1. `opIndex` / `IsValidIndex` 的边界读写；2. `Left/Right/Mid` 与 `Split/Replace` 的确定性字符串语义；3. `ParseIntoArray` / `Format` 的多参数行为与错误路径。这样才能让“字符串绑定已覆盖”具备实际含义。 |

### 二、需要新增的测试

#### NewTest-56：为 `Bind_UActorComponent.cpp` 建立 activation / tag 查询的可观察回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UActorComponent.cpp` |
| 关联函数 | `Activate(bool bReset = false)` / `Deactivate()` / `ComponentHasTag(FName Tag) const` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.NativeComponentMethods` 只走了 `NAME_None` 负分支，且没有检查激活状态是否真正改变 |
| 风险评估 | 如果组件激活绑定退化成 no-op，或 tag 查询只剩 `NAME_None` 特判，当前自动化不会报警；这会让 `UActorComponent` 这组高频 gameplay surface 在业务中静默漂移 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ComponentActivationAndTagCompat` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 场景描述 | 用 world-backed actor 或至少已注册组件构造一个带真实 tag（如 `Probe`）的 `UActorComponent`，分别通过脚本入口执行 `Deactivate()` 和 `Activate(true)`；另一个脚本入口专门验证 `ComponentHasTag(n"Probe")` 为真且 `ComponentHasTag(NAME_None)` 为假 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_FULL` 或 `ASTEST_CREATE_ENGINE_CLONE`；C++ 侧创建 actor + registered component，预置 `ComponentTags.Add("Probe")`，并记录调用前 `RuntimeComponent->IsActive()` 状态；通过 `CompileAnnotatedModuleFromMemory` 生成脚本组件类，使用 `ExecuteGeneratedIntEventOnGameThread` 调用脚本函数 |
| 期望行为 | 脚本 tag 检查返回码必须为 `1`；执行 `Deactivate()` 后 C++ 侧 `RuntimeComponent->IsActive()` 必须为 `false`，执行 `Activate(true)` 后必须恢复为 `true`；`ComponentHasTag(n"Probe")` 必须命中，`ComponentHasTag(NAME_None)` 必须保持 `false` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL` / `ASTEST_CREATE_ENGINE_CLONE` + `CompileAnnotatedModuleFromMemory` + `FindGeneratedClass` + `FindGeneratedFunction` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P1 |

#### NewTest-57：为 `Bind_FString.cpp` 建立 `IsValidIndex` / `opIndex` / substring 的专用回归，而不是继续依赖 `StringRemoveAtCompat`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FString.cpp` |
| 关联函数 | `IsValidIndex(int Index) const` / `opIndex(int32 Index)` / `Left(int Count) const` / `Right(int Count) const` / `Mid(int Start, int Count) const` / `Split(...) const` / `Replace(...) const` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.StringRemoveAtCompat` 只覆盖两个 `RemoveAt` happy path，几乎没有锁住字符串索引与切片语义 |
| 风险评估 | `Bind_FString.cpp` 的索引、切片和替换 helper 一旦在签名、返回值或边界条件上回退，当前测试树大概率仍会误报 “字符串绑定已覆盖” |
| 建议测试名 | `Angelscript.TestModule.Bindings.StringIndexAndSliceCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptStringBindingsTests.cpp` |
| 场景描述 | 用确定性样本字符串 `"ABCDE"` 覆盖索引读写与切片：先验证 `IsValidIndex(0)` / `IsValidIndex(4)` / `IsValidIndex(5)` 的边界返回值，再通过 `Value[1]` 读写验证 `opIndex`，随后检查 `Left(2)`、`Right(2)`、`Mid(1, 3)`、`Split("CD", ...)` 和 `Replace("BC", "XY")` 的精确结果 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本中分别构造原始字符串、副本和输出参数，避免多个 helper 共用同一可变字符串导致断言串扰；必要时在 C++ 侧用原生 `FString` 先计算期望结果并注入脚本 |
| 期望行为 | `IsValidIndex(0)` / `IsValidIndex(4)` 必须为 `true`，`IsValidIndex(5)` 必须为 `false`；读取 `Value[1]` 必须得到 `'B'`，写入后字符串必须精确变成 `"AZCDE"`；`Left(2)` / `Right(2)` / `Mid(1, 3)` / `Split("CD", OutLeft, OutRight)` / `Replace("BC", "XY")` 的返回值都必须与原生 `FString` 结果逐项一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_COMPILE_RUN_INT` 或 `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-83 |
| FlakyRisk | 1 | Issue-84 |
| AntiPattern | 1 | Issue-85 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮再次全文复核，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 16 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中未见 token direct-hit | 84 / 123 | 这些 shard 在当前 `Bindings/` 目录内仍缺少任何直观命中入口 |

**覆盖方法学备注**

`token direct-hit` 只是上限，不等于真正有行为级覆盖。像 `Bind_FString.cpp` 会因为脚本里到处使用 `FString` 字面量而出现大量命中，`Bind_FMath.cpp` 甚至会因为测试 C++ helper 里的 `FMath::IsNearlyEqual` 被算进命中；因此本轮 39/123 更适合当“可能有关联”的粗筛，而不是“已经被充分测试”的结论。

---

## 测试审查 (2026-04-09 01:39) 末尾索引

### 一、定位说明

本轮新发现已登记为 `Issue-81`、`Issue-82`、`NewTest-52`、`NewTest-53`、`NewTest-54`、`NewTest-55`。

由于文档前部存在重复的“覆盖快照”锚点，本轮正文追加命中了前文旧章节；为避免重复正文，这里只补末尾索引与汇总，不重复抄写详细条目。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-81 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 3 | MissingScenario: 1, MissingErrorPath: 1, NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮再次核对，仍与任务描述中的 24 文件口径不一致 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 全部 `.cpp` | 126 | 当前目录实物统计 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 2 | `Bind_BlueprintType.cpp`、`Bind_FGuid.cpp` |
| 本轮新增识别为“完全无直测”的 bind 源码 | 2 | `Bind_UEnum.cpp`、`Bind_FQuat.cpp` |

---

## 测试审查 (2026-04-09 01:39)

### 一、现有测试问题

#### Issue-81：`NativeStaticTypeGlobal` 只抽样 `__StaticType_AActor`，没有真正覆盖自动生成 static type globals 的命名面

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeStaticTypeGlobal` |
| 行号范围 | 453-492 |
| 问题描述 | 当前脚本只验证了一个 symbol：`__StaticType_AActor`。但 `Bind_BlueprintType.cpp:697-701` 会为每个已绑定类型生成 `const TSubclassOf<UObject> __StaticType_<TypeName>` 全局变量，这里面既包含 `A*`、`U*` 原生类，也包含运行期生成的 script class。现在的测试只证明了 “某一个 actor 类型的 global 存在且可用”，没有覆盖 `USceneComponent` 这类 `U*` 名称、也没有覆盖 generated class 的 global 生成与解析。若 static type global 的命名规则、注册时机或 prefix 处理只在非 `AActor` 类型上回退，当前用例仍会稳定绿灯。 |
| 影响 | `Bind_BlueprintType.cpp` 的 `__StaticType_*` 生成逻辑表面上有测试，实际上只锁住了最简单的一条样本路径；对 `U*` 类型和 generated type 的 global 漂移不会有直接报警。 |
| 修复建议 | 在现有用例里至少再加两类样本：1. 原生 `U*` 类型，例如 `__StaticType_USceneComponent`，并精确对齐到 `USceneComponent::StaticClass()`；2. 先 `CompileAnnotatedModuleFromMemory` 生成一个 script actor/class，再在 follow-up plain module 中读取 `__StaticType_<GeneratedType>`，断言它与 `FindClass("<GeneratedType>")`、`GetDefaultObject()` 结果一致。这样才能证明 static type globals 对不同命名前缀和 generated class 都成立。 |

#### Issue-82：`GuidCompat` 只验证 canonical 成功路径，`Parse`/`ParseExact` 的失败语义和 `FGuid` 访问器仍是空白

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GuidCompat` |
| 行号范围 | 41-71 |
| 问题描述 | 现有用例只做了 `ExplicitGuid.ToString(EGuidFormats::DigitsWithHyphens)` -> `FGuid::Parse(...)` / `FGuid::ParseExact(..., DigitsWithHyphens, ...)` 的成功 round-trip。`Bind_FGuid.cpp:27-31, 51-76` 还暴露了 `FGuid(const FString&)` ctor、`opIndex()`、`ParseExact` 的 format-sensitive 分支，以及 `Parse` / `ParseExact` 返回 `false` 时的 `OutGuid` 语义，但当前测试完全没有触发。若绑定错误地接受非法字符串、wrong-format 输入仍返回成功，或者在失败时改写输出 guid，当前用例都不会报警。 |
| 影响 | `FGuid` 绑定目前只锁住了最理想的 happy path；字符串构造、format mismatch、invalid parse 和下标访问这些更容易在桥接时出错的 surface 仍处于假覆盖状态。 |
| 修复建议 | 把当前用例扩成 deterministic 的正反两组断言：保留成功 round-trip，同时补 `FGuid Parsed = ExplicitGuid; bool bParsed = FGuid::Parse("not-a-guid", Parsed);` 后断言 `!bParsed` 且 `Parsed == ExplicitGuid`，再补 `ParseExact(GuidString, EGuidFormats::Digits, Parsed)` 的 wrong-format failure；另外增加 `FGuid FromString(GuidString);` 和 `ExplicitGuid[0..3]` 的逐项断言，把 ctor / `opIndex` 也纳入覆盖，而不是只测一条成功格式。 |

### 二、需要新增的测试

#### NewTest-52：为 `Bind_BlueprintType.cpp` 增加跨类型前缀与 generated class 的 static type globals 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` |
| 关联函数 | `BindStaticClass()` 生成的 `const TSubclassOf<UObject> __StaticType_<TypeName>` globals |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.NativeStaticTypeGlobal` 只验证 `__StaticType_AActor` |
| 风险评估 | 如果 `__StaticType_*` 的命名规则、注册时机或 generated class 支持只在 `U*` 类型和 script type 上回退，当前测试仍会给出全绿，类即值语法的核心入口会静默漂移 |
| 建议测试名 | `Angelscript.TestModule.Bindings.StaticTypeGlobalsMultiTypeCompat` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptStaticTypeBindingsTests.cpp` |
| 场景描述 | 同一用例中验证三类 global：原生 `AActor`、原生 `USceneComponent`，以及先通过 `CompileAnnotatedModuleFromMemory` 生成的 script actor/class；随后在 follow-up plain module 中读取对应 `__StaticType_*` symbol，并和 `FindClass()` / `StaticClass()` / `GetDefaultObject()` 做逐项对齐 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；C++ 侧先编译一个具名 script class（例如 `ABindingStaticTypeGenerated`），并拿到 `FindGeneratedClass()` 结果；plain module 脚本里分别读取 `__StaticType_AActor`、`__StaticType_USceneComponent`、`__StaticType_ABindingStaticTypeGenerated` |
| 期望行为 | 三个 static type global 都必须 `IsValid()`；其 `Get()` 结果分别精确等于 `AActor::StaticClass()`、`USceneComponent::StaticClass()` 和生成类 `UClass*`；`GetDefaultObject()` 必须非空且 generated class 的 `GetName()` / `IsChildOf(AActor::StaticClass())` 与 native 基线一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `CompileAnnotatedModuleFromMemory` + `FindGeneratedClass` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-53：为 `Bind_FGuid.cpp` 建立 invalid-parse / format-mismatch / indexer 的精确回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGuid.cpp` |
| 关联函数 | `FGuid(const FString&)` / `Parse()` / `ParseExact()` / `opIndex()` / `ToString(EGuidFormats)` |
| 现有测试覆盖 | `GuidCompat` 只覆盖 `DigitsWithHyphens` 成功 round-trip 和 `NewGuid()` happy path |
| 风险评估 | `FGuid` 绑定一旦在非法字符串处理、format-sensitive parse、string ctor 或下标访问上回退，当前自动化不会第一时间报警，问题会延迟到更高层脚本才暴露 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GuidParseFailureAndIndexCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGuidBindingsTests.cpp` |
| 场景描述 | 使用固定 guid `FGuid(0x1, 0x2, 0x3, 0x4)`，分别验证 `ToString(DigitsWithHyphens)` / `ToString(Digits)` 的成功解析、wrong-format `ParseExact` 失败、完全非法字符串 `Parse` 失败且不改写 `OutGuid`、字符串 ctor round-trip，以及四个 32-bit 槽位的 `opIndex()` 访问 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；C++ 侧先生成 `DigitsWithHyphens` 与 `Digits` 两种字符串，以及失败路径前的哨兵 guid；把这些期望值注入脚本，脚本中对 `Parsed` 初始化为哨兵值后再执行 `Parse` / `ParseExact` |
| 期望行为 | 成功路径 `Parse` / `ParseExact` 必须返回 `true` 且结果与原始 guid 精确相等；`ParseExact(DigitsWithHyphens, EGuidFormats::Digits)` 和 `Parse("not-a-guid", OutGuid)` 必须返回 `false` 且 `OutGuid` 保持哨兵值不变；`FGuid(GuidString)` 与原始 guid 相等；`Guid[0]..Guid[3]` 必须分别等于 `1,2,3,4` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-54：为完全无直测的 `Bind_UEnum.cpp` 建立 enum lookup parity 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnum.cpp` |
| 关联函数 | `GetNameByIndex()` / `GetIndexByName()` / `GetNameByValue()` / `GetValueByName()` / `GetIndexByNameString()` / `GetDisplayNameTextByValue()` / `IsValidEnumValue()` / `GenerateEnumPrefix()` |
| 现有测试覆盖 | `Bindings/` 目录完全没有 `UEnum` 的行为级测试 |
| 风险评估 | enum 名称查找、值查找和显示名访问是脚本反射层的常用基础设施；如果 `Bind_UEnum.cpp` 任一 lookup surface 回退，当前 Bindings 套件不会给出任何直接红灯 |
| 建议测试名 | `Angelscript.TestModule.Bindings.EnumLookupCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnumBindingsTests.cpp` |
| 场景描述 | C++ 侧使用 `StaticEnum<EGuidFormats>()` 取得真实 `UEnum`、其 `GetPathName()` 以及若干基线结果；脚本侧通过 `FindObject(Path)` + `Cast<UEnum>` 获取同一对象，验证按 index/name/value 查询、display name、validity 和 prefix 结果都与 native 基线一致，并补 missing-name 路径 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；C++ 侧预先计算例如 `DigitsWithHyphens`、`Digits` 的 index/value/name/display text、`GenerateEnumPrefix()` 和一个确定不存在的枚举项查找结果，再把 enum path 与这些基线字符串/数值注入脚本 |
| 期望行为 | `UEnum` 对象必须非空；脚本侧 `GetNameByValue()` / `GetValueByName()` / `GetIndexByNameString()` / `GetDisplayNameTextByValue()` / `GenerateEnumPrefix()` 必须与原生基线精确相等；missing name 路径的返回值必须与 C++ 原生 `UEnum` 调用一致，不能伪造成功 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-55：为完全无直测的 `Bind_FQuat.cpp` 建立 deterministic rotation / interpolation 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FQuat.cpp` |
| 关联函数 | `FQuat(FVector Axis, float64 AngleRad)` / `Normalize()` / `Inverse()` / `RotateVector()` / `UnrotateVector()` / `ToAxisAndAngle()` / `FQuat::Identity` / `FQuat::Slerp()` / `FQuat::MakeFromEuler()` / `Rotator()` |
| 现有测试覆盖 | `Bindings/` 目录没有任何 `FQuat` 专项测试，现有 value-type smoke 也未触达 quaternion surface |
| 风险评估 | quaternion 是 transform、camera、动画和插值路径的底层基础；一旦构造器、旋转向量、插值或 axis-angle 转换桥接出错，当前 Bindings 套件没有直接护栏 |
| 建议测试名 | `Angelscript.TestModule.Bindings.QuatRotationCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptQuatBindingsTests.cpp` |
| 场景描述 | 使用固定 axis-angle `FQuat(FVector::UpVector, PI / 2)` 和固定 euler 输入，分别验证 identity、normalize、rotate/unrotate、inverse、axis-angle 输出、`Rotator()` round-trip 以及 `FQuat::Slerp()` 的半程结果，并与 C++ 原生 `FQuat` 基线逐项对齐 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；C++ 侧先构造原生 `FQuat QuarterTurn`、`FQuat Target`、`FVector RotatedForward`、`FVector AxisOut`、`double AngleOut`、`FQuat Slerped = FQuat::Slerp(FQuat::Identity, QuarterTurn, 0.5)` 等确定性基线，再通过 `FString::Printf` 注入脚本 |
| 期望行为 | `FQuat::Identity.IsIdentity()` 为真；`QuarterTurn.RotateVector(FVector::ForwardVector)`、`Inverse().RotateVector(RotatedVector)`、`ToAxisAndAngle()`、`MakeFromEuler(...).Rotator()`、`Slerp(...)` 结果都必须与原生基线在容差内一致；`Normalize()` 后 `IsNormalized()` 必须为真 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-81 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 3 | MissingScenario: 1, MissingErrorPath: 1, NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮再次核对，仍与任务描述中的 24 文件口径不一致 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 全部 `.cpp` | 126 | 当前目录实物统计 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 2 | `Bind_BlueprintType.cpp`、`Bind_FGuid.cpp` |
| 本轮新增识别为“完全无直测”的 bind 源码 | 2 | `Bind_UEnum.cpp`、`Bind_FQuat.cpp` |

#### NewTest-49：为 `Bind_TSet.cpp` 补齐 foreach 的空集合与“恰好访问一次”回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp` |
| 关联函数 | `opForBegin()` / `opForNext()` / `opForValue()` / `TSet<T>::Iterator()` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.SetForeach` 只在 `{2,5}` 上验证 `Sum == 7` |
| 风险评估 | 如果 foreach surface 重复访问、跳过元素、或在空集合起始状态上回退，当前测试几乎不会报警，`TSet` 遍历 bug 只能在业务脚本里晚发现 |
| 建议测试名 | `Angelscript.TestModule.Bindings.SetForeachExactVisit` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 场景描述 | 先验证空 `TSet<int>` 的 foreach 不会进入循环体；再对 `{2, 5, 11}` 执行 foreach，把访问到的元素写入新的 `TSet<int> Visited`，并单独统计访问次数 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本中准备 `TSet<int> EmptyValues` 与 `TSet<int> Values`，后者依次 `Add(2)`、`Add(5)`、`Add(11)`，循环体内把 `Value` 加入 `Visited` 并 `VisitCount += 1` |
| 期望行为 | 空集合路径 `VisitCount == 0`；非空路径 `Visited.Num() == 3`、`Visited.Contains(2)`、`Visited.Contains(5)`、`Visited.Contains(11)`、`VisitCount == 3`；不允许出现漏访、重访或 phantom value |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-50：为 `Bind_TMap.cpp` 增加 missing-key `Find` 与 `FindOrAdd` 引用返回回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp` |
| 关联函数 | `Find()` / `FindOrAdd(const K&)` / `FindOrAdd(const K&, const V&)` |
| 现有测试覆盖 | `MapCompat` 仅覆盖 `Find("Alpha")` 和 `FindOrAdd("Alpha"/"Beta")` 的成功路径 |
| 风险评估 | `Find()` 失败时若错误改写 `out` 参数，或 `FindOrAdd()` 回退成返回临时副本而不是 map 内部存储，当前测试不会发现；这会直接破坏脚本侧最基础的 map 读写契约 |
| 建议测试名 | `Angelscript.TestModule.Bindings.MapFindFailureAndFindOrAddRefCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 场景描述 | 脚本先构造 `TMap<FName, int> Values`，验证 missing key 的 `Find()` 返回 `false` 且不改写哨兵值，再验证两种 `FindOrAdd` overload 返回的是可写回 map 的真实引用 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本中设定 `int MissingValue = 99;` 后调用 `Values.Find(FName("Missing"), MissingValue)`；随后执行 `int& Gamma = Values.FindOrAdd(FName("Gamma")); Gamma = 33;` 与 `int& Delta = Values.FindOrAdd(FName("Delta"), 11); Delta = 12;` |
| 期望行为 | `Find("Missing", MissingValue)` 必须返回 `false` 且 `MissingValue == 99`；`Find("Gamma", OutValue)` 返回 `true` 且 `OutValue == 33`；`Find("Delta", OutValue)` 返回 `true` 且 `OutValue == 12`；map 最终 `Num() == 2` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-51：把 `Bind_FTransform.cpp` 从混合 smoke 中拆出来，建立 deterministic transform 语义回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTransform.cpp` |
| 关联函数 | `TransformPosition()` / `TransformPositionNoScale()` / `InverseTransformPosition()` / `GetRelativeTransform()` / `SetTranslation()` / `SetScale3D()` / `Equals()` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.ValueTypes` 只顺带调用一次 `Transform.TransformPosition(FVector::ForwardVector)` |
| 风险评估 | `Bind_FTransform.cpp` 绑定了大量 ctor、inverse、relative、scale 和 matrix 相关 surface；当前若其中某个 overload 或返回值语义回退，`ValueTypes` 这种混合烟雾测试既难定位也很容易漏检 |
| 建议测试名 | `Angelscript.TestModule.Bindings.TransformDeterministicCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTransformBindingsTests.cpp` |
| 场景描述 | 使用固定旋转/平移/缩放的 `FTransform`，分别验证位置变换、无缩放变换、逆变换、relative transform 和 setter 更新语义，并把结果与 C++ 原生 `FTransform` 基线精确对齐 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；C++ 侧先构造 `FTransform Baseline(FRotator(0.0, 90.0, 0.0), FVector(10.0, 20.0, 30.0), FVector(2.0, 3.0, 4.0))`，计算 `TransformPosition(FVector(1,0,0))`、`TransformPositionNoScale(FVector(1,0,0))`、`InverseTransformPosition(...)`、`GetRelativeTransform(Other)` 等期望值，再通过 `FString::Printf` 注入脚本 |
| 期望行为 | 脚本侧 `TransformPosition()`、`TransformPositionNoScale()`、`InverseTransformPosition()` 的结果必须与原生基线逐分量近似相等；`SetTranslation()` / `SetScale3D()` 后 `GetTranslation()` / `GetScale3D()` 必须精确匹配；`GetRelativeTransform(Other).Equals(ExpectedRelative, Tolerance)` 为真 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-79 |
| AntiPattern | 1 | Issue-80 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingErrorPath: 1 |
| P1 | 2 | MissingEdgeCase: 1, MissingScenario: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 全部 `.cpp` | 126 | 当前仓库实物数 |
| 其中 `Bind_*.cpp` | 123 | 用户输入“126 个 Bind_*.cpp”与当前仓库实物不符 |
| 非 `Bind_*.cpp` | 3 | `BlueprintCallableReflectiveFallback.cpp`、`UObjectInWorld.cpp`、`UObjectTickable.cpp` |
| 本轮人工复核后已见对应测试的 `Bind_*.cpp` | 41 / 123 | 口径为 `Bindings/` 目录内存在行为级或 focused compat 入口 |
| 本轮人工复核后完全无对应测试的 `Bind_*.cpp` | 82 / 123 | 仅 `Bindings/` 目录口径；不计其他目录的 compile/parity smoke |

**已见对应测试的 `Bind_*.cpp`（41）**

```text
Bind_AActor.cpp
Bind_BlueprintCallable.cpp
Bind_BlueprintType.cpp
Bind_CollisionProfile.cpp
Bind_Console.cpp
Bind_Delegates.cpp
Bind_FApp.cpp
Bind_FCommandLine.cpp
Bind_FDateTime.cpp
Bind_FFileHelper.cpp
Bind_FGameplayTag.cpp
Bind_FGuid.cpp
Bind_FMath.cpp
Bind_FName.cpp
Bind_FNumberFormattingOptions.cpp
Bind_FParse.cpp
Bind_FPaths.cpp
Bind_FPlane.cpp
Bind_FPlatformMisc.cpp
Bind_FPlatformProcess.cpp
Bind_FRandomStream.cpp
Bind_FRotator.cpp
Bind_FString.cpp
Bind_FText.cpp
Bind_FTimespan.cpp
Bind_FTransform.cpp
Bind_FVector.cpp
Bind_FVector2D.cpp
Bind_FVector2f.cpp
Bind_Hash.cpp
Bind_Logging.cpp
Bind_SoftObjectPath.cpp
Bind_TArray.cpp
Bind_TMap.cpp
Bind_TOptional.cpp
Bind_TSet.cpp
Bind_TSoftObjectPtr.cpp
Bind_UActorComponent.cpp
Bind_UObject.cpp
Bind_UPackage.cpp
Bind_USceneComponent.cpp
```

**完全无对应测试的 `Bind_*.cpp`（82）**

```text
Bind_AngelscriptGASLibrary.cpp
Bind_APlayerController.cpp
Bind_AssetRegistry.cpp
Bind_AVolume.cpp
Bind_BlueprintEvent.cpp
Bind_ConfigEnums.cpp
Bind_CoreGlobals.cpp
Bind_Debugging.cpp
Bind_Deprecations.cpp
Bind_FAnchors.cpp
Bind_FAngelscriptDelegateWithPayload.cpp
Bind_FAngelscriptGameThreadScopeWorldContext.cpp
Bind_FBodyInstance.cpp
Bind_FBox.cpp
Bind_FBox3f.cpp
Bind_FBoxSphereBounds.cpp
Bind_FBoxSphereBounds3f.cpp
Bind_FCollisionQueryParams.cpp
Bind_FCollisionShape.cpp
Bind_FColor.cpp
Bind_FCpuProfilerTraceScoped.cpp
Bind_FFormatArgumentValue.cpp
Bind_FGameplayAbilitySpec.cpp
Bind_FGameplayAttribute.cpp
Bind_FGameplayEffectSpec.cpp
Bind_FGameplayTagBlueprintPropertyMap.cpp
Bind_FGenericPlatformMisc.cpp
Bind_FGeometry.cpp
Bind_FHitResult.cpp
Bind_FInputActionKeyMapping.cpp
Bind_FInputActionValue.cpp
Bind_FInputBindingHandle.cpp
Bind_FInstancedStruct.cpp
Bind_FIntPoint.cpp
Bind_FIntVector.cpp
Bind_FIntVector2.cpp
Bind_FIntVector4.cpp
Bind_FLatentActionInfo.cpp
Bind_FLinearColor.cpp
Bind_FMargin.cpp
Bind_FMemoryReader.cpp
Bind_FMessageDialog.cpp
Bind_FOverlapResult.cpp
Bind_FPlane4f.cpp
Bind_FPlatformApplicationMisc.cpp
Bind_FQuat.cpp
Bind_FQuat4f.cpp
Bind_FRotator3f.cpp
Bind_FSphere.cpp
Bind_FSphere3f.cpp
Bind_FStringTableRegistry.cpp
Bind_FTransform3f.cpp
Bind_FunctionLibraryMixins.cpp
Bind_FVector3f.cpp
Bind_FVector4.cpp
Bind_FVector4f.cpp
Bind_InputEvents.cpp
Bind_Json.cpp
Bind_JsonObjectConverter.cpp
Bind_LandscapeProxy.cpp
Bind_Primitives.cpp
Bind_Stats.cpp
Bind_Subsystems.cpp
Bind_SystemTimers.cpp
Bind_UAssetManager.cpp
Bind_UCollisionProfile.cpp
Bind_UDataTable.cpp
Bind_UEnhancedInputComponent.cpp
Bind_UEnum.cpp
Bind_UFXSystemComponent.cpp
Bind_UGameInstance.cpp
Bind_UInputSettings.cpp
Bind_ULocalPlayer.cpp
Bind_UPoseableMeshComponent.cpp
Bind_UPrimitiveComponent.cpp
Bind_UProjectileMovementComponent.cpp
Bind_USkeletalMeshComponent.cpp
Bind_USkinnedMeshComponent.cpp
Bind_UStruct.cpp
Bind_UUserWidget.cpp
Bind_UWorld.cpp
Bind_WorldCollision.cpp
```

#### NewTest-1：补齐 `Bind_CoreGlobals.cpp` 的 commandlet globals 对应测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_CoreGlobals.cpp` |
| 关联函数 | `IsRunningCommandlet()` / `IsRunningCookCommandlet()` / `IsRunningDLCCookCommandlet()` / `GetRunningCommandletClass()` |
| 现有测试覆盖 | `AngelscriptGlobalBindingsTests.cpp` 仅覆盖 `CollisionProfile::BlockAllDynamic` 和几个静态默认值，未触达 commandlet globals |
| 风险评估 | 全局函数如果绑定到错误的原生符号、返回默认值或 `UClass` 转换错误，当前不会有任何用例报警；`GlobalBindingsTests` 继续维持单个烟雾测试的意义很弱 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GlobalCommandletGlobalsCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGlobalBindingsTests.cpp` |
| 场景描述 | 在 C++ 侧读取当前进程的 commandlet 状态和运行中的 commandlet class，再把这些期望值注入脚本，验证 script globals 与原生 API 完全一致 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 建立干净引擎；在 C++ 侧缓存 `::IsRunningCommandlet()`、`::IsRunningCookCommandlet()`、`::IsRunningDLCCookCommandlet()`、`::GetRunningCommandletClass()` 的结果，并把布尔值/类名转义进脚本 |
| 期望行为 | 脚本四个全局函数的返回值与 C++ 侧期望完全相等；当原生类为 `nullptr` 时脚本返回 `null`，否则 `GetName()` 与原生类名一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-2：补齐 `Bind_AActor.cpp` 的 world-backed actor helper 覆盖

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` |
| 关联函数 | `SpawnActor<T>()` / `GetAllActorsOfClass()` / `GetActorOfClass()` / `GetActorOfClassWithTag()` / `GetActorComponentByClass()` |
| 现有测试覆盖 | `AngelscriptNativeEngineBindingsTests.cpp` 只覆盖少量 `AActor`/`UObject` 实例方法，没有触达任何 world 查询或 spawn helper |
| 风险评估 | `Bind_AActor.cpp` 后半段的大量全局 helper 完全可能参数顺序错、输出类型推导错或 tag/class 过滤错，而当前测试仍然全绿 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ActorLookupAndSpawnCompat` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 场景描述 | 在测试 world 中生成两个不同类 Actor，并给其中一个设置 tag 和组件；脚本分别调用 actor spawn、按类查询、按 tag 查询、按类取组件等 helper |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_FULL` 与 `FScopedTestWorldContextScope` 创建 world；C++ 侧先 spawn `AActor`/`ACameraActor`，设置 `FName("TargetTag")`，并给目标 Actor 挂一个 `USceneComponent` |
| 期望行为 | `SpawnActor<T>()` 返回非空且类正确；`GetAllActorsOfClass()` 数量与原生 `UGameplayStatics`/world 查询一致；`GetActorOfClassWithTag()` 返回带 tag 的那个 Actor；`GetActorComponentByClass()` 返回名字匹配的组件 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL` + `FScopedTestWorldContextScope` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P0 |

#### NewTest-3：为 `Bind_WorldCollision.cpp` 增加同步 trace/sweep/overlap 语义测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` |
| 关联函数 | `LineTraceSingleByChannel()` / `LineTraceMultiByChannel()` / `SweepSingleByChannel()` / `OverlapAnyTestByChannel()` / `ComponentOverlapMultiByChannel()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 该文件绑定了 30+ 个同步 collision 查询接口，参数转发、`OutHit`/`OutOverlaps` 输出、默认 query params 任一出错都不会被发现 |
| 建议测试名 | `Angelscript.TestModule.Bindings.WorldCollision.SyncQueries` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionBindingsTests.cpp` |
| 场景描述 | 在测试 world 中放置一个阻挡碰撞的 primitive component，脚本分别执行命中与未命中的 line trace、sweep、overlap，并把 `FHitResult` / `FOverlapResult` 关键信息返回给 C++ 校验 |
| 输入/前置 | `ASTEST_CREATE_ENGINE_FULL` + `FScopedTestWorldContextScope`；C++ 侧创建带 `BlockAllDynamic` profile 的 `UBoxComponent`，放在确定位置，并记录原生 world 查询结果作为基线 |
| 期望行为 | 命中路径返回 `true` 且 `OutHit.GetActor()`/`OutHit.GetComponent()`/命中数量与原生查询一致；未命中路径返回 `false` 且 `OutHits`/`OutOverlaps` 为空；`ComponentOverlapMultiByChannel()` 返回的数量与原生 `UWorld` 调用一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL` + `FScopedTestWorldContextScope` + `BuildModule` |
| 优先级 | P0 |

#### NewTest-4：为 `Bind_WorldCollision.cpp` 增加 async trace delegate 与句柄查询测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_WorldCollision.cpp` |
| 关联函数 | `AsyncLineTraceByChannel()` / `AsyncOverlapByChannel()` / `QueryTraceData()` / `QueryOverlapData()` / `IsTraceHandleValid()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | async trace 额外经过 delegate 包装、`UserData` 透传和 `FTraceHandle`/`FTraceDatum` 转换，任何一层出错都会导致回调静默失效或查询结果错误 |
| 建议测试名 | `Angelscript.TestModule.Bindings.WorldCollision.AsyncTraceCallbacks` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldCollisionBindingsTests.cpp` |
| 场景描述 | 脚本发起 async line trace/overlap，并把 delegate 回调接收到的 `UserData`、命中数量和 handle 状态写回脚本可读字段；C++ 侧驱动 world/tick 直到回调完成 |
| 输入/前置 | 复用同步 collision 测试 world；提供一个脚本对象承载回调函数，调用 `AsyncLineTraceByChannel(..., Delegate, 77)` 与 `AsyncOverlapByChannel(..., Delegate, 88)` |
| 期望行为 | 返回的 `FTraceHandle` 初始有效；回调各触发一次；回调里收到的 `UserData` 分别是 `77/88`；`QueryTraceData()`/`QueryOverlapData()` 返回 `true` 且命中数组非空；完成后 `IsTraceHandleValid()` 语义与原生 API 一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL` + `FScopedTestWorldContextScope` + 现有 tick/wait helper（无则补一个轻量 world tick helper） |
| 优先级 | P0 |

#### NewTest-5：为 `Bind_Json.cpp` 增加 Json round-trip 与字段访问测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp` |
| 关联函数 | `Json::ParseString()` / `FJsonObject::SetStringField()` / `SetNumberField()` / `SetBoolField()` / `CreateObjectField()` / `SetArrayField()` / `SaveToString()` / `FJsonValue::TryGet*()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | `Bind_Json.cpp` 绑定了对象、数组、值类型、序列化和类型转换的整套 API；当前若任一 getter/setter 或 parse/save 映射错误，不会有任何回归信号 |
| 建议测试名 | `Angelscript.TestModule.Bindings.JsonObjectRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptJsonBindingsTests.cpp` |
| 场景描述 | 脚本构造包含 string/number/bool/object/array 的嵌套 JSON，序列化后再用 `Json::ParseString()` 解析回对象，并逐字段读取 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本中创建 `FJsonObject Root`、`FJsonArray Values` 和嵌套 child object，必要时把序列化结果返回给 C++ 做辅助核对 |
| 期望行为 | `SaveToString(false)` 结果非空；重新解析后 `GetStringField("Name")`、`GetNumberField("Score")`、`GetBoolField("Enabled")`、`GetArrayField("Values").Num()`、`TryGetObjectField("Child", ...)` 全部返回预期；`Json::ValueTypeToString(EJsonType::Array)` 返回 `"Array"` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-6：为 `Bind_Json.cpp` 增加类型错误、越界和迭代修改保护测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Json.cpp` |
| 关联函数 | `FJsonObject::GetStringField()` / `FJsonArray::GetValueAt()` / `FJsonObject::Iterator()` / `RemoveField()` / `SetField()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 该文件内部显式抛出 `"Json Value of type..."`、`"Array index is out of bounds"`、`"FJsonObject is being modified during for loop iteration"` 等错误；这些错误路径完全未验证，最容易在后续重构时失效 |
| 建议测试名 | `Angelscript.TestModule.Bindings.JsonErrorPaths` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptJsonBindingsTests.cpp` |
| 场景描述 | 脚本先把 number 字段按 string 读取、再访问越界数组索引、最后在 `for`/iterator 遍历期间插入新字段，分别触发三条预期错误 |
| 输入/前置 | 使用 `AddExpectedError` 订阅错误文本；脚本构造 `{"Score":3.5,"Values":[1]}` 后执行错误访问，再单独构造迭代期间修改 map 的脚本 |
| 期望行为 | 运行失败并命中预期错误文本；失败后对象内容保持原状，至少 `HasField("Injected") == false`；越界 `GetValueAt(1)` 不应返回有效值 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AddExpectedError` + 自定义执行上下文检查 |
| 优先级 | P1 |

#### NewTest-7：补齐 `Bind_Delegates.cpp` 的 signature overload 与 null error path

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp` |
| 关联函数 | `BindUFunction(UObject, FName, UDelegateFunction)` / `AddUFunction(UObject, FName, UDelegateFunction)` / `__DelegateSignature()` / `UnbindObject()` |
| 现有测试覆盖 | `AngelscriptFileAndDelegateBindingsTests.cpp` 只覆盖 `BindUFunction`/`AddUFunction`/`Unbind`/`Clear` 的 happy path，没有覆盖 signature overload、`__DelegateSignature` 或 null 输入报错 |
| 风险评估 | 绑定层里显式抛了 `"Null object passed to BindUFunction"`、`"Null signature passed to BindUFunction"` 等错误；如果 overload 绑错签名或异常路径失效，现有测试不会发现 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ScriptDelegateSignatureAndErrors` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 场景描述 | 先用 `__DelegateSignature()` 获取 delegate 的 `UDelegateFunction`，再调用 signature overload 完成 bind/add；随后分别传 `null` object 和 `null` signature 触发预期错误，并验证 delegate 状态不被污染 |
| 输入/前置 | 使用现有 `UAngelscriptNativeScriptTestObject` 作为接收对象；`AddExpectedError` 监听 null object / null signature 文本；script 中准备 single + multicast delegate 各一份 |
| 期望行为 | 正常路径下 `GetFunctionName()` 与 `GetUObject()` 正确，`__DelegateSignature()` 返回的函数名与 native delegate signature 一致；错误路径下执行失败并命中预期日志，delegate 仍保持 `!IsBound()` 或绑定前状态；`UnbindObject()` 后 multicast delegate 彻底解绑 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AddExpectedError` + `BuildModule` |
| 优先级 | P1 |

#### NewTest-8：补齐 `Bind_FFileHelper.cpp` 的写入标志与失败路径

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FFileHelper.cpp` |
| 关联函数 | `SaveStringToFile()` / `LoadFileToString()` / `EFileWrite::NoReplaceExisting` / `EFileWrite::Append` / `EFileRead::Silent` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.FileHelperCompat` 只覆盖单次 save/load happy path，没有覆盖 flags，也没有覆盖缺失文件或已有文件冲突 |
| 风险评估 | `uint32` flags 绑定、枚举映射和底层 `IFileManager` 转发一旦出错，当前 happy path 仍可通过，无法发现真正常用的 append / no-replace 行为回归 |
| 建议测试名 | `Angelscript.TestModule.Bindings.FileHelperFlagsAndErrorPaths` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 场景描述 | 使用唯一临时文件路径，先写入初始内容，再用 `NoReplaceExisting` 尝试覆盖、用 `Append` 追加，并验证加载不存在文件时返回 `false` |
| 输入/前置 | C++ 侧用 `FGuid` 生成两个唯一文件路径（现存文件 / 缺失文件），在 `ON_SCOPE_EXIT` 中删除；script 依次调用 `SaveStringToFile("A", Path)`、`SaveStringToFile("B", Path, ..., uint32(EFileWrite::NoReplaceExisting))`、`SaveStringToFile("C", Path, ..., uint32(EFileWrite::Append))`、`LoadFileToString(Missing, MissingPath, ..., uint32(EFileRead::Silent))` |
| 期望行为 | 初次写入成功；`NoReplaceExisting` 返回 `false` 且文件内容仍是 `"A"`；append 后文件内容变为 `"AC"`；读取缺失文件返回 `false` 且不会抛额外错误 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ON_SCOPE_EXIT` |
| 优先级 | P1 |

#### NewTest-9：为 `Bind_AssetRegistry.cpp` 增加基础查询与 `FAssetData` 语义测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AssetRegistry.cpp` |
| 关联函数 | `AssetRegistry::HasAssets()` / `GetAssetsByPath()` / `GetAssetByObjectPath()` / `GetAllAssets()` / `FAssetData::GetSoftObjectPath()` / `GetObjectPathString()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 该文件直接暴露 AssetRegistry 的大量公共查询函数和 `FTopLevelAssetPath`/`FAssetData` 辅助方法；如果路径构造、`OutAssetData` 输出或 `FAssetData` 包装错了，脚本侧会出现静默空结果 |
| 建议测试名 | `Angelscript.TestModule.Bindings.AssetRegistryQueryCompat` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetRegistryBindingsTests.cpp` |
| 场景描述 | 使用已知 engine 资产路径（例如 `/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial`）和 `/Engine/EngineMaterials` 包路径，脚本执行 `HasAssets`、`GetAssetsByPath`、`GetAssetByObjectPath` 并读取 `FAssetData` 的 object path/soft path |
| 输入/前置 | 在 C++ 侧先用原生 `IAssetRegistry` 查询同一路径得到期望布尔值与对象路径，再把路径和期望注入脚本；测试运行于 editor context |
| 期望行为 | `HasAssets`、`GetAssetsByPath` 数量、`GetAssetByObjectPath` 成功与否都与原生结果一致；返回的 `FAssetData.GetSoftObjectPath().ToString()` 与 `GetObjectPathString()` 至少匹配目标资产路径；`FTopLevelAssetPath` 从字符串构造后 `IsValid()` 且 `ToString` 语义与原生一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + C++ 原生 AssetRegistry 基线比对 |
| 优先级 | P1 |

#### NewTest-10：补齐 `Bind_FAngelscriptDelegateWithPayload.cpp` 的 payload 执行与签名错误路径

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FAngelscriptDelegateWithPayload.cpp` |
| 关联函数 | `BindUFunction()` / `BindWithPayload()` / `ExecuteIfBound()` |
| 现有测试覆盖 | `AngelscriptFileAndDelegateBindingsTests.cpp` 只覆盖普通 `FScriptDelegate` / `FMulticastScriptDelegate`，完全没有覆盖 `FAngelscriptDelegateWithPayload` |
| 风险评估 | 该绑定同时涉及 payload type-id 到 `UScriptStruct`/boxed primitive 的转换、函数签名校验和 `ProcessEvent` 调用；目前任何 payload 复制或类型判定回归都不会被发现 |
| 建议测试名 | `Angelscript.TestModule.Bindings.DelegateWithPayloadCompat` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 场景描述 | 先绑定一个接收单个 `int32` 或 `UScriptStruct` 参数的 `UFUNCTION`，用 `BindWithPayload` 注入 payload 并执行；再分别用无参函数、错误参数类型和无效对象触发预期错误 |
| 输入/前置 | 准备一个原生测试对象，包含 `UFUNCTION void SetIntFromPayload(int32 Value)` 与一个不兼容签名的 `UFUNCTION void WrongPayloadSignature()`；使用 `AddExpectedError` 监听 `"Invalid payload type"`、`"Invalid object passed to BindUFunction."` 和 `"Specified function is not compatible with delegate function."` |
| 期望行为 | 正常路径下 `ExecuteIfBound()` 调用后对象上的值被更新为 payload；错误路径下执行失败并命中预期错误，delegate 保持未绑定或旧绑定状态，不应污染后续执行 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AddExpectedError` + `BuildModule` |
| 优先级 | P1 |
---

## 测试审查 (2026-04-08 13:08)

### 一、现有测试问题

#### Issue-2：Iterator 测试把唯一断言结果丢弃后仍直接返回 `true`

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptIteratorBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SetIteratorCompat`；`Angelscript.TestModule.Bindings.MapIteratorCompat` |
| 行号范围 | 20-70；73-127 |
| 问题描述 | 两个 `RunTest` 在脚本执行成功后都只调用了一次 `TestEqual(...)`，随后无条件 `return true;`。这意味着用例本身没有把最终断言结果纳入显式通过条件，测试是否失败完全依赖 `TestEqual` 的副作用记录，而不是当前函数的返回路径。 |
| 影响 | 当前写法让断言契约变得脆弱：后续一旦把 `TestEqual` 替换成普通比较、追加新的清理断言，或者有人沿用同样模板扩展更多迭代器测试，就容易出现“核心语义没过但 `RunTest` 仍返回成功”的模式扩散。 |
| 修复建议 | 统一改成 `const bool bPassed = TestEqual(...); return bPassed;`，或仿照其他文件先保存 `bPassed` 再在 `ASTEST_END_SHARE_CLEAN` 后返回；如果后续需要继续扩断言，直接把 `Result` 拆成多个 `TestEqual/TestTrue`，不要再依赖单个哨兵值加无条件 `return true;`。 |

#### Issue-3：ObjectPtr/SoftObjectPtr 测试在共享引擎里编译模块却没有任何模块清理

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ObjectPtrCompat`；`Angelscript.TestModule.Bindings.SoftObjectPtrCompat` |
| 行号范围 | 18-81；84-192 |
| 问题描述 | 两个用例都使用 `ASTEST_CREATE_ENGINE_SHARE()` + `ASTEST_BEGIN_SHARE`，而共享宏在 `AngelscriptTestMacros.h` 中明确说明“reused across tests, no reset”。这两个 `RunTest` 里既没有 `Engine.DiscardModule(...)`，也没有 `ON_SCOPE_EXIT` 清理，只是在共享引擎里持续编译 `ASObjectPtrCompat` / `ASSoftObjectPtrCompat` 模块。 |
| 影响 | 模块、脚本类型和相关反射状态会残留到后续绑定测试里，形成顺序相关的串测风险；对象指针绑定又正好依赖 `UObject`/`SoftObjectPath` 这类全局注册面，一旦后面有同名模块或额外 object bind 测试，很容易出现“单跑通过、整套跑出问题”的假绿。 |
| 修复建议 | 这类纯 compat smoke 更适合改成 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`，让每个用例起始时自动 reset 共享引擎；如果要继续保留 `SHARE`，至少在每个测试里增加 `ON_SCOPE_EXIT { Engine.DiscardModule(TEXT("ASObjectPtrCompat")); }` / `ASSoftObjectPtrCompat`，并把最终断言结果纳入返回值，避免模块和断言两头都漏。 |

#### Issue-4：CoreMisc 三个 compat 用例都复用了脏共享引擎并漏掉模块销毁

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GuidCompat`；`Angelscript.TestModule.Bindings.PathsCompat`；`Angelscript.TestModule.Bindings.NumberFormattingOptionsCompat` |
| 行号范围 | 26-96；99-174；177-230 |
| 问题描述 | 三个测试都走 `ASTEST_CREATE_ENGINE_SHARE()` / `ASTEST_BEGIN_SHARE`，但文件内没有任何 `DiscardModule` 或 reset 逻辑。结合共享宏“no reset”的定义，这意味着 `ASGuidCompat`、`ASPathsCompat`、`ASNumberFormattingOptionsCompat` 会持续留在同一个共享引擎里，测试隔离完全依赖运行顺序碰巧不冲突。 |
| 影响 | 这类基础绑定 smoke 一旦和别的 compat 文件一起执行，脚本模块、自动生成的 Saved/Automation 文件以及潜在的类型注册都可能跨测试残留，后续如果扩展 `FGuid` / `FPaths` / `FNumberFormattingOptions` 相关测试，调试成本会显著上升。 |
| 修复建议 | 把这三个用例统一切到 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 或 `FAngelscriptTestFixture` 的 `SharedClone` 模式；若担心 reset 成本，就至少给每个用例补 `ON_SCOPE_EXIT` 里的 `Engine.DiscardModule(...)`，并保持模块名与清理名一一对应。 |

#### Issue-5：ClassBindings 在共享引擎里保留生成类和查询模块，直接削弱类查找测试的隔离性

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ClassLookupCompat`；`Angelscript.TestModule.Bindings.TSubclassOfCompat`；`Angelscript.TestModule.Bindings.TSoftClassPtrCompat`；`Angelscript.TestModule.Bindings.StaticClassCompat`；`Angelscript.TestModule.Bindings.NativeStaticTypeGlobal` |
| 行号范围 | 39-96；98-184；186-274；276-420；453-496 |
| 问题描述 | 除了已单独记录的 `NativeStaticClassNamespace` 外，这个文件的其余 5 个用例全部使用 `ASTEST_CREATE_ENGINE_SHARE()`，但没有任何 `DiscardModule`/reset。尤其 `StaticClassCompat` 还在共享引擎里编译 `ASAnnotatedStaticClassCompat` 生成 `ABindingStaticClassActor`，随后再编译 `ASGeneratedStaticClassQuery` 用 `FindClass("ABindingStaticClassActor")` 查询同一份残留生成类。测试成功依赖“前一步刚刚把类留在引擎里”，而不是隔离环境中的稳定查找契约。 |
| 影响 | 这会把“类查找是否正确”与“共享引擎里是否还留着上一段生成类状态”绑在一起，导致单跑和整套跑的观察面不同；后续如果再补 rename/discard/full reload 测试，当前残留类更容易污染 `FindClass`、`GetAllClasses`、`__StaticClass` 这组核心绑定的结果。 |
| 修复建议 | `ClassLookupCompat`、`TSubclassOfCompat`、`TSoftClassPtrCompat`、`NativeStaticTypeGlobal` 应至少改为 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 并补 `ON_SCOPE_EXIT` 销毁各自模块；`StaticClassCompat` 更适合切到 `ASTEST_CREATE_ENGINE_FULL()`/`CLONE()` 或 `FAngelscriptTestFixture` 的隔离模式，并在结束时显式丢弃 `ASStaticClassCompat`、`ASAnnotatedStaticClassCompat`、`ASGeneratedStaticClassQuery`，避免把生成类残留误当成被测行为的一部分。 |

#### Issue-6：CompatBindings 通过共享引擎保留 annotated script class，`Cast<T>` 回归用例存在跨测试污染

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ObjectCastCompat`；`Angelscript.TestModule.Bindings.ObjectEditorOnlyCompat`；`Angelscript.TestModule.Bindings.TimespanCompat`；`Angelscript.TestModule.Bindings.DateTimeCompat` |
| 行号范围 | 29-142；144-183；185-276；278-383 |
| 问题描述 | 四个 compat 用例全部采用 `ASTEST_CREATE_ENGINE_SHARE()`，文件内没有任何 `Engine.DiscardModule(...)`。其中 `ObjectCastCompat` 还额外调用 `CompileAnnotatedModuleFromMemory(...)` 生成 `ABindingCastActor` 和 `UBindingCastComponent`，再实例化对象并执行反射调用，但测试结束后既不 discard annotated module，也不 reset 共享引擎。 |
| 影响 | 生成类、函数和普通 script module 会继续停留在共享引擎中，使后续对象转换、时间类型或 UObject 绑定测试处在被前序 compat 用例污染过的环境里；这种状态泄漏尤其容易把“generated class 还能被找到”误当成 bind 行为正确。 |
| 修复建议 | 把整文件切到 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()` 并给每个用例补 `ON_SCOPE_EXIT` 对应模块清理；`ObjectCastCompat` 还应在 isolated engine 中运行 annotated module 路径，或使用专门 fixture 在 teardown 时统一销毁生成类相关模块，避免和后续 `Bind_UObject` / `Bind_UClass` 测试共享同一个类表。 |

#### Issue-7：`ScriptDelegateCompat` 只验证了绑定元数据，没有真正执行 delegate/event

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ScriptDelegateCompat` |
| 行号范围 | 35-105 |
| 问题描述 | 脚本里只检查了 `IsBound()`、`GetFunctionName()`、`Unbind()`、`Clear()`，但对 `FNativeCallback` 没有调用 `Execute(...)` 验证返回值，对 `FNativeEvent` 也没有 `Broadcast(...)` 验证 side effect。当前测试因此只能证明“名字绑上了”，却不能证明 `BindUFunction`/`AddUFunction` 的真实调用链、参数传递和解绑语义是正确的。 |
| 影响 | 只要 delegate 绑定表面状态还能成立，哪怕内部调用走错 overload、参数桥接错误、或 raw helper 与公开 helper 语义分叉，这个用例也会继续绿灯；尤其在 `Bind_Delegates.cpp` 已有内部 `BindUFunction`/`AddUFunction` surface 的前提下，这种只看元数据的测试很难挡住行为回归。 |
| 修复建议 | 不要再用 `GetDefaultObject()` 做纯元数据 smoke，改成 `NewObject<UAngelscriptNativeScriptTestObject>(GetTransientPackage())` 创建可观察实例：1. `Single.BindUFunction(...)` 后执行 `Single.Execute(7, "Alpha")`，断言返回 `12`（对应 `NativeIntStringEvent` 的 `Value + Label.Len()` 语义）。2. `Multi.AddUFunction(...)` 后执行 `Multi.Broadcast(7, "Alpha")`，再断言 `NameCounts[FName("Alpha")] == 7`。3. `Multi.Unbind(...)` 后再次 broadcast，断言 `NameCounts` 不再变化，真正覆盖 bind/unbind/execute 三条路径。 |

#### Issue-8：`FileHelperCompat` 向固定 Saved 路径写文件却不做删除，重复运行会持续留脏数据

| 项目 | 内容 |
|------|------|
| 问题类型 | MissingCleanup |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.FileHelperCompat` |
| 行号范围 | 278-327 |
| 问题描述 | 用例在脚本里把内容写到 `FPaths::ProjectSavedDir()/AngelscriptFileHelperCompat.txt`，但 C++ 侧 `ON_SCOPE_EXIT` 只做了 `Engine.DiscardModule("ASFileHelperCompat")`，没有删除这个文件。文件名还是固定值，不是唯一临时路径。 |
| 影响 | 测试每跑一次都会在 `Saved/` 下留下同名文件，既污染工作区，也让后续 rerun 更难判断当前结果来自本次写入还是上次残留；一旦后面扩展 `LoadFileToString`/追加写入/错误路径测试，这个固定文件更容易形成跨用例串扰。 |
| 修复建议 | 在 C++ 侧把目标文件路径提到测试代码里，使用 `FGuid` 生成唯一临时文件名，并在 `ON_SCOPE_EXIT` 同时执行 `IFileManager::Get().Delete(*Filename, false, true, true)`；如果仍保留固定文件名，也至少在用例开始和结束各清一次，避免历史残留干扰判断。 |

#### Issue-9：`PlatformProcessCompat` 把 `CanLaunchURL` 写死成必须为 `true`，对执行环境做了未声明的平台假设

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.PlatformProcessCompat` |
| 行号范围 | 160-215 |
| 问题描述 | 脚本里直接断言 `FPlatformProcess::CanLaunchURL("https://example.com") == true`。但这个结果依赖宿主 OS、CI 沙箱策略、是否有默认浏览器关联以及平台实现本身，测试并没有像前面 `FCommandLine::Parse` 一样先在 C++ 侧取 UE 原生结果，再验证脚本绑定是否与 native API 一致。 |
| 影响 | 该用例在受限构建机、无桌面环境或平台实现返回 `false` 的情况下会报假红，而且失败点来自环境差异，不是 BindSystem 回归；相反，如果脚本绑定始终返回常量 `true`，当前测试在“恰好支持 URL launch 的机器”上也不一定能暴露语义偏差。 |
| 修复建议 | 在 C++ 侧先计算 `const bool bExpectedCanLaunch = FPlatformProcess::CanLaunchURL(TEXT("https://example.com"));`，把期望值注入脚本后比较“脚本结果 == native 结果”；更稳妥的做法是把该断言拆成一个专门用例，只验证绑定返回值与 C++ 原生 API 一致，而不是对环境能力本身做 hard assert。 |

#### Issue-10：`Logging` 用例只观察到了 `Error()`，`Log` / `LogDisplay` / `Warning` 三条绑定几乎没有断言

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.Logging` |
| 行号范围 | 218-258 |
| 问题描述 | 用例在脚本里依次调用 `Log(...)`、`LogDisplay(...)`、`Warning(...)`、`Error(...)`，但 C++ 侧只通过 `AddExpectedError("Test error message")` 观察 `Error()`，最后再断言 `Entry()` 返回 `1`。也就是说，前三个 logging helper 即使被错误路由、完全 no-op 或输出级别错乱，测试依然会通过。 |
| 影响 | `Bind_Logging.cpp` 中最常用的普通日志路径没有任何自动化护栏，当前测试只能挡住 “Error 消息没发出” 这一种回归，无法验证 `Log` / `LogDisplay` / `Warning` 与 UE 原生日志语义是否一致。 |
| 修复建议 | 在测试里挂一个自定义 `FOutputDevice` 或使用现有 automation log 捕获器，分别断言四条消息都被写入且文本完整；同时保留 `AddExpectedError` 只处理 error 级别，避免 error 断言与普通日志捕获互相混淆。这样每个 logging helper 都有独立可见的断言，而不是共享一个“脚本没崩”信号。 |

#### Issue-11：NativeEngine 绑定测试在共享引擎中保留 generated class，并留下运行时 Actor/Component 实例

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeActorMethods`；`Angelscript.TestModule.Bindings.NativeComponentMethods`；`Angelscript.TestModule.Bindings.ComponentDestroyCompat` |
| 行号范围 | 28-90；93-211；214-281 |
| 问题描述 | 三个用例都使用 `ASTEST_CREATE_ENGINE_SHARE()`，却没有 `DiscardModule`、没有 reset，也没有销毁自己创建的 `OuterActor` / `RuntimeComponent`。同时每个用例都会通过 `CompileAnnotatedModuleFromMemory(...)` 生成新的 script class，再把实例挂到 transient package 或 actor 上运行。 |
| 影响 | generated class、反射函数以及运行时对象会跨测试残留，后续与 `Bind_AActor`、`Bind_UActorComponent`、`Bind_USceneComponent` 相关的绑定测试可能在已经被污染过的 class/object registry 上运行；尤其 `ComponentDestroyCompat` 把组件留在 `IsBeingDestroyed()` 状态后不做 teardown，更容易把生命周期残留带给下一条测试。 |
| 修复建议 | 这类 native bridge 测试应优先改成 `ASTEST_CREATE_ENGINE_FULL()` 或 `CLONE()`，保证 script class 生命周期和运行时对象都在隔离引擎里结束；若必须保留共享引擎，至少在 `ON_SCOPE_EXIT` 中 `Engine.DiscardModule(...)` 对应模块，并显式 `DestroyComponent()` / `RemoveOwnedComponent()` / `MarkAsGarbage()` 清理创建的 actor/component，避免把 class registry 和对象状态留给后续用例。 |

#### Issue-12：`NativeActorMethods` 里对 `GetPathName` / `GetFullName` 的检查是永真式，两个绑定实际没有被验证

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeActorMethods` |
| 行号范围 | 28-90 |
| 问题描述 | 脚本把 `GetPathName()` 和 `GetFullName()` 的结果保存到 `Path` / `FullName` 后，只检查 `Path.Len() < 0 || FullName.Len() < 0`。`FString::Len()` 不可能小于 0，因此这两条判断永远为假，最终真正生效的只剩 `IsA(RuntimeType)` 这一项。 |
| 影响 | 即使 `Bind_AActor.cpp` / `Bind_UObject.cpp` 里的 `GetPathName`、`GetFullName` 绑定返回空串、错误对象名或完全错误的字符串格式，此用例仍会稳定通过，导致“NativeActorMethods 已覆盖路径/全名绑定”的结论失真。 |
| 修复建议 | 至少把判断改成 `Path.IsEmpty()` / `FullName.IsEmpty()`；更稳妥的是在 C++ 侧拿同一个 `RuntimeActor` 的 `GetPathName()` / `GetFullName()` 原生结果，与脚本侧返回值做一一比对。若现有 helper 只支持 `int` 返回，可让脚本把字符串写回 `UAngelscriptNativeScriptTestObject` 或临时测试 UObject，再在 C++ 侧断言 exact match。 |

#### Issue-13：`ReflectiveFallback.AIModule` 调用了 `SetSensingInterval` 却从不验证结果，第二个 fallback 调用形同虚设

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.BlueprintCallableReflectiveFallback.AIModule` |
| 行号范围 | 97-155 |
| 问题描述 | `RunReflectiveFallback()` 里先调用 `SetPeripheralVisionAngle(42.0f)` 并通过 `GetPeripheralVisionAngle()` 做了回读校验，但紧接着又调用了 `SetSensingInterval(0.25f)` 后直接 `return 1;`。也就是说，这个用例只真正验证了一个 unresolved AIModule 方法，另一个 fallback 调用即使被忽略、绑定到错误函数或 silently no-op，也不会被发现。 |
| 影响 | 当前文件看起来像覆盖了两条 reflective fallback 调用，实际只锁住了 `SetPeripheralVisionAngle`。`SetSensingInterval` 所对应的 fallback 路径如果回退，测试仍然会给出假绿，掩盖 `BlueprintCallableReflectiveFallback.cpp` 的真实覆盖空洞。 |
| 修复建议 | 在脚本里补 `float CurrentInterval = GetSensingInterval(); if (CurrentInterval < 0.249f || CurrentInterval > 0.251f) return 20;` 之类的回读断言；如果 `UPawnSensingComponent` 没有公开 getter，就在 C++ 侧执行完函数后直接读取 `RuntimeObject->SensingInterval` 或对应 accessor，与脚本设定值比对，确保第二条 fallback 调用也有独立断言。 |

### 二、需要新增的测试

当前按 `Bind_*.cpp` 口径统计，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 里的现有 16 个测试文件只对 123 个 bind 源文件中的 37 个形成了明确测试入口，另外 86 个 `Bind_*.cpp` 仍未看到任何对应测试。本轮只追加优先级最高的新测试建议。

#### NewTest-1：为 `Bind_TArray.cpp` 补齐 `Reserve` / `SetNum` / `RemoveSwap` / self-alias 边界回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TArray.cpp` |
| 关联函数 | `Reserve` / `SetNum` / `RemoveSwap` / `Add` / `Insert` / `Remove` |
| 现有测试覆盖 | 有测试但缺少 `Reserve`、`SetNum`、`RemoveSwap`、self-alias 场景 |
| 风险评估 | 现有 `AngelscriptEngineBindingsTests.cpp` 和 `AngelscriptContainerBindingsTests.cpp` 只锁住了 `AddUnique`、`Insert`、`RemoveSingle`、`Remove`、`Reset`、iterator happy path；一旦 `Reserve` 清空数组、`SetNum` 暴露未初始化值、`RemoveSwap` 返回值错误或 self-alias 读到失效地址，现有测试不会报警。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ArrayMutationEdgeCases` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptArrayEdgeBindingsTests.cpp` |
| 场景描述 | 在同一个脚本入口里覆盖非空数组 `Reserve` 后元素仍保留、`SetNum(5)` 新增槽位默认为 `0`、`RemoveSwap(Needle)` 返回实际删除数量，以及 `Values.Add(Values[0])` / `Values.Insert(Values[0], 0)` 的 self-alias 保护。 |
| 输入/前置 | `int[] Values = {1, 2, 1, 3};`；另备一个 `int[] Reserved = {4, 5};` 和 `int[] ZeroExtended = {9};`。 |
| 期望行为 | `Reserved.Reserve(16)` 后 `Reserved.Num() == 2 && Reserved[0] == 4 && Reserved[1] == 5`；`ZeroExtended.SetNum(4)` 后新槽位全为 `0`；`Values.RemoveSwap(1)` 返回 `2` 且剩余元素集合为 `{2,3}`；self-alias 调用在修复后应抛出明确脚本错误，而不是返回错误结果或崩溃。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_COMPILE_RUN_INT`；self-alias 分支用手动 `asIScriptContext` 捕获执行失败 |
| 优先级 | P0 |

#### NewTest-2：把 `Bind_Delegates.cpp` 的真实调用路径补成可观察回归，而不是只测 `IsBound`

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Delegates.cpp` |
| 关联函数 | `BindUFunction` / `AddUFunction` / `Unbind` / `Clear` / delegate invoke |
| 现有测试覆盖 | 有测试但只覆盖 bind/unbind 元数据，不覆盖 execute/broadcast |
| 风险评估 | 当前 `ScriptDelegateCompat` 即使面对错误的参数桥接、返回值语义回退或 multicast no-op 也可能继续通过，导致 delegate bind surface 只在“名字挂上了”这一层有保护。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ScriptDelegateExecuteCompat` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 场景描述 | 用 `UAngelscriptNativeScriptTestObject` 的真实原生方法做单播执行和多播广播：先执行 `FNativeCallback`，再广播 `FNativeEvent`，最后验证 `Unbind` 后再次广播不再改写接收对象。 |
| 输入/前置 | 在 C++ 侧创建 `UAngelscriptNativeScriptTestObject` 实例；脚本中绑定 `NativeIntStringEvent` 与 `SetIntStringFromDelegate`，参数统一使用 `(7, "Alpha")`。 |
| 期望行为 | `Single.Execute(7, "Alpha")` 返回 `12`；`Multi.Broadcast(7, "Alpha")` 后接收对象 `NameCounts[FName("Alpha")] == 7`；`Multi.Unbind(...)` 后再次 broadcast，`NameCounts` 保持不变；`Single.Clear()` 后 `IsBound()` 为 `false`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `FAngelscriptTestFixture` / 现有 `UAngelscriptNativeScriptTestObject` 测试辅助类 |
| 优先级 | P1 |

#### NewTest-3：给 `Bind_Logging.cpp` 增加四种日志级别的独立捕获断言

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Logging.cpp` |
| 关联函数 | `Log` / `LogDisplay` / `Warning` / `Error` |
| 现有测试覆盖 | 有测试但只验证 `Error()` 触发 expected error |
| 风险评估 | `Log`、`LogDisplay`、`Warning` 三条最常用路径如果改成 no-op、落错 category 或丢文本，当前自动化仍会给出绿灯。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.LoggingVerbosityRouting` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 场景描述 | 在测试里挂一个临时 `FOutputDevice` 捕获脚本发出的四条消息，分别验证普通 log、display、warning、error 都能到达日志系统。 |
| 输入/前置 | 脚本依次输出 `"LogMessage"`、`"DisplayMessage"`、`"WarningMessage"`、`"ErrorMessage"`；C++ 侧注册捕获器并保留 `AddExpectedError("ErrorMessage")`。 |
| 期望行为 | `Entry()` 返回 `1`；捕获器中能找到四条精确文本；`ErrorMessage` 既命中 expected error，又不会吞掉其余三条日志；若任一 helper 不输出，测试直接失败。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + 自定义 `FOutputDevice` 捕获器 |
| 优先级 | P1 |

#### NewTest-4：为完全无测的 `Bind_AssetRegistry.cpp` 建立 null/error-path 与 `FTopLevelAssetPath` round-trip 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AssetRegistry.cpp` |
| 关联函数 | `FTopLevelAssetPath` constructor / `AssetRegistry::GetAssetsByClass` / `GetBlueprintCDOsByParentClass` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 该文件暴露了大量 editor/runtime 查询 API 和一个显式 `Throw("A null Class was passed to GetBlueprintCDOsByParentClass.")` 错误路径，但当前没有任何测试锁住 constructor、查询返回值或 null 输入行为。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.AssetRegistryTopLevelPathAndNullParent` |
| 测试类型 | Integration |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptAssetRegistryBindingsTests.cpp` |
| 场景描述 | 先验证 `FTopLevelAssetPath(AActor::StaticClass())`、`FTopLevelAssetPath(Path.ToString())` 的 `IsValid`/`opEquals` round-trip；再执行 `AssetRegistry::GetAssetsByClass(FTopLevelAssetPath(UBlueprint::StaticClass()), Assets)` 验证查询调用本身可用；最后单独运行一个脚本入口调用 `GetBlueprintCDOsByParentClass(null, OutAssets)`，断言进入预期错误路径。 |
| 输入/前置 | `FTopLevelAssetPath` 使用 `AActor::StaticClass()`；查询类使用 `UBlueprint::StaticClass()`；错误路径额外准备空 `TArray<UObject>`。 |
| 期望行为 | round-trip path 有效且相等；`GetAssetsByClass(...)` 返回 `true` 且不会崩溃，返回数组中每个 `FAssetData` 的 `GetObjectPathString()` 非空；null parent 场景记录 `"A null Class was passed to GetBlueprintCDOsByParentClass."`，执行失败且 `OutAssets.Num() == 0`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AddExpectedError` + 手动 `asIScriptContext` 执行 |
| 优先级 | P0 |

#### NewTest-5：为完全无测的 `Bind_FColor.cpp` / `Bind_FLinearColor.cpp` 建立颜色 round-trip 与运算语义回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FColor.cpp` |
| 关联函数 | `FColor` constructor / `ToHex` / `FromHex` / `ReinterpretAsLinear` / `FLinearColor` arithmetic / `ToFColor` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | `Bind_FColor.cpp` 和 `Bind_FLinearColor.cpp` 暴露了构造、全局常量、颜色转换和大量算术运算，但当前 Bindings 目录里没有任何颜色相关回归，等于两整组基础值类型直接裸奔。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ColorRoundTripCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptColorBindingsTests.cpp` |
| 场景描述 | 同时验证 `FColor::FromHex("#FF0000FF")` / `ToHex()` round-trip、`FColor::Red` 与字面构造相等、`FLinearColor(FColor::Red).ToFColor(true)` 回到红色，以及 `FLinearColor` 的 `opMulAssign` / `opDivAssign` / `HSVToLinearRGB` / `LinearRGBToHSV` 不偏离预期。 |
| 输入/前置 | `FColor Red(255, 0, 0, 255)`、`FColor::Red`、`FLinearColor(0.25f, 0.5f, 0.75f, 1.0f)`。 |
| 期望行为 | `FromHex`/`ToHex` 前后颜色一致；`FColor::Red == FColor(255,0,0,255)`；`FLinearColor(FColor::Red).ToFColor(true) == FColor::Red`；`LinearRGBToHSV().HSVToLinearRGB()` 与原值近似相等；乘除赋值链会修改原对象而不是临时副本。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 5 | Issue-7 |
| MissingCleanup | 3 | Issue-6 |
| BadIsolation | 3 | Issue-5 |
| FlakyRisk | 1 | Issue-9 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | MissingEdgeCase: 1, NoTestForSource: 1 |
| P1 | 3 | MissingScenario: 2, NoTestForSource: 1 |

**覆盖快照**

| 指标 | 数量 |
|------|------|
| `Bindings/` 现有测试文件 | 16 |
| 明确对应测试入口的 `Bind_*.cpp` | 37 / 123 |
| 未看到任何对应测试的 `Bind_*.cpp` | 86 / 123 |

未看到任何对应测试的 `Bind_*.cpp` 本轮统计包括：
`Bind_AngelscriptGASLibrary.cpp`、`Bind_APlayerController.cpp`、`Bind_AssetRegistry.cpp`、`Bind_AVolume.cpp`、`Bind_BlueprintCallable.cpp`、`Bind_BlueprintEvent.cpp`、`Bind_ConfigEnums.cpp`、`Bind_CoreGlobals.cpp`、`Bind_Debugging.cpp`、`Bind_Deprecations.cpp`、`Bind_FAnchors.cpp`、`Bind_FAngelscriptDelegateWithPayload.cpp`、`Bind_FAngelscriptGameThreadScopeWorldContext.cpp`、`Bind_FBodyInstance.cpp`、`Bind_FBox.cpp`、`Bind_FBox3f.cpp`、`Bind_FBoxSphereBounds.cpp`、`Bind_FBoxSphereBounds3f.cpp`、`Bind_FCollisionShape.cpp`、`Bind_FColor.cpp`、`Bind_FCpuProfilerTraceScoped.cpp`、`Bind_FFormatArgumentValue.cpp`、`Bind_FGameplayAbilitySpec.cpp`、`Bind_FGameplayAttribute.cpp`、`Bind_FGameplayEffectSpec.cpp`、`Bind_FGameplayTagBlueprintPropertyMap.cpp`、`Bind_FGenericPlatformMisc.cpp`、`Bind_FGeometry.cpp`、`Bind_FHitResult.cpp`、`Bind_FInputActionKeyMapping.cpp`、`Bind_FInputActionValue.cpp`、`Bind_FInputBindingHandle.cpp`、`Bind_FInstancedStruct.cpp`、`Bind_FIntPoint.cpp`、`Bind_FIntVector.cpp`、`Bind_FIntVector2.cpp`、`Bind_FIntVector4.cpp`、`Bind_FLatentActionInfo.cpp`、`Bind_FLinearColor.cpp`、`Bind_FMargin.cpp`、`Bind_FMemoryReader.cpp`、`Bind_FMessageDialog.cpp`、`Bind_FOverlapResult.cpp`、`Bind_FPlane.cpp`、`Bind_FPlane4f.cpp`、`Bind_FPlatformApplicationMisc.cpp`、`Bind_FQuat.cpp`、`Bind_FQuat4f.cpp`、`Bind_FRotator3f.cpp`、`Bind_FSphere.cpp`、`Bind_FSphere3f.cpp`、`Bind_FStringTableRegistry.cpp`、`Bind_FTransform3f.cpp`、`Bind_FunctionLibraryMixins.cpp`、`Bind_FVector2D.cpp`、`Bind_FVector2f.cpp`、`Bind_FVector3f.cpp`、`Bind_FVector4.cpp`、`Bind_FVector4f.cpp`、`Bind_InputEvents.cpp`、`Bind_Json.cpp`、`Bind_JsonObjectConverter.cpp`、`Bind_LandscapeProxy.cpp`、`Bind_Primitives.cpp`、`Bind_Stats.cpp`、`Bind_Subsystems.cpp`、`Bind_SystemTimers.cpp`、`Bind_UAssetManager.cpp`、`Bind_UCollisionProfile.cpp`、`Bind_UDataTable.cpp`、`Bind_UEnhancedInputComponent.cpp`、`Bind_UEnum.cpp`、`Bind_UFXSystemComponent.cpp`、`Bind_UGameInstance.cpp`、`Bind_UInputSettings.cpp`、`Bind_ULocalPlayer.cpp`、`Bind_UPackage.cpp`、`Bind_UPoseableMeshComponent.cpp`、`Bind_UPrimitiveComponent.cpp`、`Bind_UProjectileMovementComponent.cpp`、`Bind_USkeletalMeshComponent.cpp`、`Bind_USkinnedMeshComponent.cpp`、`Bind_UStruct.cpp`、`Bind_UUserWidget.cpp`、`Bind_UWorld.cpp`、`Bind_WorldCollision.cpp`。

---

## 测试审查 (2026-04-08 13:20)

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-5（13:07 节） |
| BadIsolation | 1 | Issue-1（13:07 节） |
| AntiPattern | 1 | Issue-2（13:07 节） |
| FlakyRisk | 2 | Issue-6（13:07 节） |
| MissingCleanup | 1 | Issue-4（13:07 节） |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 4 | NoTestForSource: 2, MissingScenario: 1, MissingErrorPath: 1 |
| P1 | 6 | NoTestForSource: 3, MissingErrorPath: 2, MissingEdgeCase: 1 |

**覆盖快照（本轮人工复核）**

| 项目 | 数量 | 说明 |
|------|------|------|
| 已完整读取的现有测试文件 | 16 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 当前实际可见文件数 |
| 已扫描的 `Bind_*.cpp` | 123 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 当前实际文件数 |
| 已见直接对应测试的 `Bind_*.cpp` | 41 | 按文件名/符号对应人工复核，包含 `Bind_BlueprintType.cpp`、`Bind_Delegates.cpp` 这类“文件名未直接出现在测试里但能从被测符号确认”的部分覆盖 |
| 当前未见直接对应测试的 `Bind_*.cpp` | 82 | 以本轮已读 16 个测试文件为基线，未发现明确直接覆盖证据 |

已见直接对应测试的 `Bind_*.cpp`（41）：
`Bind_AActor.cpp; Bind_BlueprintCallable.cpp; Bind_BlueprintType.cpp; Bind_CollisionProfile.cpp; Bind_Console.cpp; Bind_Delegates.cpp; Bind_FApp.cpp; Bind_FCommandLine.cpp; Bind_FDateTime.cpp; Bind_FFileHelper.cpp; Bind_FGameplayTag.cpp; Bind_FGuid.cpp; Bind_FMath.cpp; Bind_FName.cpp; Bind_FNumberFormattingOptions.cpp; Bind_FParse.cpp; Bind_FPaths.cpp; Bind_FPlane.cpp; Bind_FPlatformMisc.cpp; Bind_FPlatformProcess.cpp; Bind_FRandomStream.cpp; Bind_FRotator.cpp; Bind_FString.cpp; Bind_FText.cpp; Bind_FTimespan.cpp; Bind_FTransform.cpp; Bind_FVector.cpp; Bind_FVector2D.cpp; Bind_FVector2f.cpp; Bind_Hash.cpp; Bind_Logging.cpp; Bind_SoftObjectPath.cpp; Bind_TArray.cpp; Bind_TMap.cpp; Bind_TOptional.cpp; Bind_TSet.cpp; Bind_TSoftObjectPtr.cpp; Bind_UActorComponent.cpp; Bind_UObject.cpp; Bind_UPackage.cpp; Bind_USceneComponent.cpp`

当前未见直接对应测试的 `Bind_*.cpp`（82）：
`Bind_AngelscriptGASLibrary.cpp; Bind_APlayerController.cpp; Bind_AssetRegistry.cpp; Bind_AVolume.cpp; Bind_BlueprintEvent.cpp; Bind_ConfigEnums.cpp; Bind_CoreGlobals.cpp; Bind_Debugging.cpp; Bind_Deprecations.cpp; Bind_FAnchors.cpp; Bind_FAngelscriptDelegateWithPayload.cpp; Bind_FAngelscriptGameThreadScopeWorldContext.cpp; Bind_FBodyInstance.cpp; Bind_FBox.cpp; Bind_FBox3f.cpp; Bind_FBoxSphereBounds.cpp; Bind_FBoxSphereBounds3f.cpp; Bind_FCollisionQueryParams.cpp; Bind_FCollisionShape.cpp; Bind_FColor.cpp; Bind_FCpuProfilerTraceScoped.cpp; Bind_FFormatArgumentValue.cpp; Bind_FGameplayAbilitySpec.cpp; Bind_FGameplayAttribute.cpp; Bind_FGameplayEffectSpec.cpp; Bind_FGameplayTagBlueprintPropertyMap.cpp; Bind_FGenericPlatformMisc.cpp; Bind_FGeometry.cpp; Bind_FHitResult.cpp; Bind_FInputActionKeyMapping.cpp; Bind_FInputActionValue.cpp; Bind_FInputBindingHandle.cpp; Bind_FInstancedStruct.cpp; Bind_FIntPoint.cpp; Bind_FIntVector.cpp; Bind_FIntVector2.cpp; Bind_FIntVector4.cpp; Bind_FLatentActionInfo.cpp; Bind_FLinearColor.cpp; Bind_FMargin.cpp; Bind_FMemoryReader.cpp; Bind_FMessageDialog.cpp; Bind_FOverlapResult.cpp; Bind_FPlane4f.cpp; Bind_FPlatformApplicationMisc.cpp; Bind_FQuat.cpp; Bind_FQuat4f.cpp; Bind_FRotator3f.cpp; Bind_FSphere.cpp; Bind_FSphere3f.cpp; Bind_FStringTableRegistry.cpp; Bind_FTransform3f.cpp; Bind_FunctionLibraryMixins.cpp; Bind_FVector3f.cpp; Bind_FVector4.cpp; Bind_FVector4f.cpp; Bind_InputEvents.cpp; Bind_Json.cpp; Bind_JsonObjectConverter.cpp; Bind_LandscapeProxy.cpp; Bind_Primitives.cpp; Bind_Stats.cpp; Bind_Subsystems.cpp; Bind_SystemTimers.cpp; Bind_UAssetManager.cpp; Bind_UCollisionProfile.cpp; Bind_UDataTable.cpp; Bind_UEnhancedInputComponent.cpp; Bind_UEnum.cpp; Bind_UFXSystemComponent.cpp; Bind_UGameInstance.cpp; Bind_UInputSettings.cpp; Bind_ULocalPlayer.cpp; Bind_UPoseableMeshComponent.cpp; Bind_UPrimitiveComponent.cpp; Bind_UProjectileMovementComponent.cpp; Bind_USkeletalMeshComponent.cpp; Bind_USkinnedMeshComponent.cpp; Bind_UStruct.cpp; Bind_UUserWidget.cpp; Bind_UWorld.cpp; Bind_WorldCollision.cpp`

---

## 测试审查 (2026-04-08 13:25)

### 一、现有测试问题

#### Issue-14：`AngelscriptEngineBindingsTests.cpp` 的 5 个用例在共享引擎里编译模块却不做任何模块清理

| 项目 | 内容 |
|------|------|
| 问题类型 | BadIsolation |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ValueTypes`、`Angelscript.TestModule.Bindings.FNameArrayCompat`、`Angelscript.TestModule.Bindings.TArrayMutationCompat`、`Angelscript.TestModule.Bindings.ForeachCompat`、`Angelscript.TestModule.Bindings.TArrayIteratorCompat` |
| 行号范围 | 35-37, 95-97, 164-166, 228-230, 318-320 |
| 问题描述 | 这 5 个用例全部使用 `ASTEST_CREATE_ENGINE_SHARE()`，并在共享引擎上 `BuildModule(...)` 创建 `ASBindingValueTypes`、`ASFNameArrayCompat`、`ASTArrayMutationCompat`、`ASForeachCompat`、`ASTArrayIteratorCompat`，但文件内没有任何 `Engine.DiscardModule(...)` 或 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`。模块和注册出来的脚本类型会一直留在同一个 shared engine 里。 |
| 影响 | 当前文件里的测试顺序一旦变化，或者后续别的绑定测试复用同名 module / 类型名，就可能读到前一个用例留下的编译结果而误报绿灯；同时 shared engine 中累计的模块也会让失败定位变得更困难。 |
| 修复建议 | 把这 5 个用例统一切到 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`，并在每个 `RunTest` 里用 `ON_SCOPE_EXIT` 调用对应的 `Engine.DiscardModule(...)`；如果仍想保留 shared engine，加一个本文件专用 helper 负责按模块名编译和清理，避免继续出现“共享引擎 + 无 discard”的模式。 |

#### Issue-15：`HashCompat` 只验证“同一输入重复调用一致”，没有校验绑定结果是否等于原生哈希语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.HashCompat` |
| 行号范围 | 48-69 |
| 问题描述 | 用例对 `Hash::CityHash32`、`Hash::CityHash64`、`Hash::CityHash64WithSeed`、`Hash::CityHash64WithSeeds` 的检查全部是“同一个字符串调用两次结果一致”，外加一条 `Hash64A != SeededA`。它没有把脚本侧结果与 C++ 原生 `Hash::*` 输出做任何比对，也没有验证不同输入是否得到不同结果。只要绑定稳定地返回某个错误常量、错误算法结果，甚至把两个 64-bit seeded overload 绑到同一个实现，这个用例都可能继续通过。 |
| 影响 | `Bind_Hash.cpp` 一旦出现签名绑错、seed 参数丢失或返回值截断，当前测试无法证明脚本语义与 UE 原生 API 一致，只能证明“脚本层某个实现自洽”。这会让基础 hash helper 的错误长期以假绿形式存在。 |
| 修复建议 | 在 C++ 侧先计算 `Hash::CityHash32(TEXT("Alpha"))`、`Hash::CityHash64(TEXT("Alpha"))`、带 seed 的原生期望值，并把这些常量注入脚本做精确 `==` 断言；再补一组不同输入/不同 seed 的负向断言，例如 `"Alpha"` vs `"Beta"`、`123` vs `456`，确保四个 overload 真正区分输入与 seed。 |

#### Issue-16：`ConsoleCommandReplacementCompat` 没有验证 original command 真正生效，无法证明“替换”而非“最终注册成功”

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ConsoleCommandReplacementCompat` |
| 行号范围 | 363-440 |
| 问题描述 | 用例先编译 `ASConsoleCommandOriginalCompat`，让命令把输出变量设为 `11`，随后立刻编译 `ASConsoleCommandReplacementCompat`，把同名命令改为写 `22`。但测试从头到尾没有在 replacement 之前执行 original command，也没有断言 original command 已经注册且输出过 `11`。因此它并没有证明“新模块替换了旧命令”，只证明最终存在一个能把值写成 `22` 的命令。 |
| 影响 | 如果 original command 根本没有注册成功，或者 replacement 实际上是第一次注册该命令，这个测试仍会绿灯。`Bind_Console.cpp` 的“同名命令替换旧绑定”语义因此没有被真正锁住。 |
| 修复建议 | 在编译 replacement 之前，先调用一次 `VerifyConsoleCommandExists(*this, CommandName)` 和 `ExecuteConsoleCommand(*this, CommandName, {})`，断言输出变量先变成 `11`；然后再编译 replacement、再次执行并断言值变成 `22`。必要时把“original 生效”和“replacement 接管”拆成两个独立 `TestEqual`，避免再次把关键状态变化压成单个尾部结果。 |

#### Issue-17：`MathExtendedCompat` 对多组 deterministic 数学绑定只做“非零/范围”烟雾检查，断言强度明显不足

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MathExtendedCompat` |
| 行号范围 | 46-108 |
| 问题描述 | 这条用例一次性覆盖了 `VRand`、`VRandCone`、`GetReflectionVector`、`VInterpTo`、`RInterpTo`、`FInterpTo`、`RandomRotator`、`CubicInterp`、`CubicInterpDerivative` 等大量绑定，但很多关键断言只是“结果不为零”“结果大于 0”或“结果在范围内”。例如 `GetReflectionVector(FVector(1,0,0), FVector(-1,0,0))` 理应精确回到 `(-1,0,0)`，测试却只检查 `!IsNearlyZero()`；`VInterpTo`/`RInterpTo`/`FInterpTo` 也没有与原生 `FMath::*` 的具体结果做任何比对。 |
| 影响 | 只要绑定返回一个“看起来合理但其实错误”的向量、旋转或标量，这个用例仍可能稳定通过。`Bind_FMath.cpp` 中最容易发生参数顺序错误、单位错误或返回值截断的 helper 因此没有真正被锁住。 |
| 修复建议 | 对 deterministic helper 改成精确期望断言：在脚本里直接比对 `GetReflectionVector(...) == FVector(-1,0,0)`、`LinePlaneIntersection(...) == FVector::ZeroVector`，并在 C++ 侧预先算出 `VInterpTo`/`RInterpTo`/`FInterpTo`/`CubicInterp*` 的原生结果后注入脚本做 `Equals`/`IsNearlyEqual`；随机 helper 则只保留“范围/归一化”类断言，避免把两类检查混在同一个烟雾脚本里。 |

#### Issue-18：`RandomStreamCompat` 没有校验脚本随机序列与原生 `FRandomStream` 是否一致

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.RandomStreamCompat` |
| 行号范围 | 238-267 |
| 问题描述 | 当前脚本只检查 `GetInitialSeed()==123`、`Reset()` 后两次 `RandRange(1,1000)` 相等、`GetFraction()` 落在 `[0,1]`、`RandRange(0.0,10.0)` 落在区间内，以及 `ToString()` 非空。它没有把脚本得到的随机值、当前 seed、`GenerateNewSeed()` 后的变化与 C++ 侧原生 `FRandomStream` 的同一调用序列做任何对齐。 |
| 影响 | 如果 `Bind_FRandomStream.cpp` 把某个 overload 绑错、内部状态推进顺序有误，或者 `GetCurrentSeed()` / `GenerateNewSeed()` 的桥接不一致，只要脚本还能生成“看起来像随机数”的值，这个用例仍可能通过。 |
| 修复建议 | 在 C++ 侧构造 `FRandomStream NativeStream(123)`，预先记录第一次 `RandRange(1,1000)`、`Reset()` 后的第二次值、`GetFraction()`、`RandRange(0.0,10.0)` 和 `GenerateNewSeed()` 后的 `GetCurrentSeed()`，把这些精确期望注入脚本做 `==` / `IsNearlyEqual` 断言；同时补一条 copy 之后两个 stream 继续前进应产生相同下一值的检查，锁住 copy 语义。 |

### 二、需要新增的测试

#### NewTest-11：为 `Bind_FCollisionShape.cpp` 增加 shape factory / getter / mutator 行为回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCollisionShape.cpp` |
| 关联函数 | `SetBox()` / `SetSphere()` / `SetCapsule()` / `GetExtent()` / `GetBox()` / `GetSphereRadius()` / `GetCapsuleRadius()` / `GetCapsuleHalfHeight()` / `FCollisionShape::MakeBox()` / `MakeSphere()` / `MakeCapsule()` |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` 仅在 `WorldCollisionParity` 里把 `FCollisionShape::MakeSphere(10.0f)` 当作编译 smoke 使用；`Bindings/` 目录没有任何行为级断言 |
| 风险评估 | `ShapeType`、半径/半高、`FVector` 到 `FVector3f` 的转换和 namespace factory 任一绑错时，当前只会留下“类型能编译”的假绿，真正的 collision 参数语义不会报警 |
| 建议测试名 | `Angelscript.TestModule.Bindings.CollisionShapeCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCollisionValueBindingsTests.cpp` |
| 场景描述 | 脚本先验证默认构造的 `FCollisionShape` 是 line / nearly-zero，再依次调用 `SetBox(FVector(10,20,30))`、`SetSphere(15)`、`SetCapsule(7,12)` 与 namespace `MakeBox/MakeSphere/MakeCapsule`，检查每一步的 shape kind 和 getter 结果 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；脚本内固定使用 `FVector(10.0, 20.0, 30.0)`、`15.0f`、`7.0f/12.0f` 作为输入，并额外读取 `FCollisionShape::MinBoxExtent()`、`MinSphereRadius()`、`MinCapsuleRadius()`、`MinCapsuleAxisHalfHeight()` |
| 期望行为 | 默认对象 `IsLine()==true`；`SetBox` 后 `IsBox()==true` 且 `GetBox()`/`GetExtent()` 等于输入半尺寸；`SetSphere` 后 `IsSphere()==true` 且 `GetSphereRadius()==15.0f`；`SetCapsule` 后 `IsCapsule()==true` 且 `GetCapsuleRadius()==7.0f`、`GetCapsuleHalfHeight()==12.0f`；三个 `Make*` factory 产出的 getter 结果与直接 `Set*` 一致；四个 `Min*` 常量都大于 `0` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-12：为 `Bind_FHitResult.cpp` / `Bind_FOverlapResult.cpp` 增加字段与对象访问器回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FHitResult.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FOverlapResult.cpp` |
| 关联函数 | `FHitResult(const FVector&, const FVector&)` / `FaceIndex` / `ElementIndex` / `Item` / `MyItem` / `TraceStart` / `TraceEnd` / `ImpactPoint` / `ImpactNormal` / `BoneName` / `MyBoneName` / `FOverlapResult::SetActor()` / `GetActor()` / `SetComponent()` / `GetComponent()` / `SetBlockingHit()` / `GetbBlockingHit()` |
| 现有测试覆盖 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` 仅对 `FHitResult` 做了一个 compile+execute smoke，覆盖少量数字字段；`FOverlapResult` 只在 world collision parity 片段中作为数组元素出现，没有任何直接断言 |
| 风险评估 | `FHitResult` 的向量/名字字段、`FOverlapResult` 的 actor/component handle 与 blocking flag 如果绑错，当前测试树几乎不会给出定位明确的回归信号 |
| 建议测试名 | `Angelscript.TestModule.Bindings.CollisionResultAccessorsCompat` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCollisionValueBindingsTests.cpp` |
| 场景描述 | 脚本中构造一个 `FHitResult(FVector(-1,0,0), FVector(1,0,0))`，给公开字段写入确定性向量/名称/索引；再创建 transient `AActor` 与 `USceneComponent`，通过 `FOverlapResult.SetActor/SetComponent/SetBlockingHit` 回写并立即用 getter 读回 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；脚本通过 `NewObject(GetTransientPackage(), AActor::StaticClass())` 与 `NewObject(Actor, USceneComponent::StaticClass())` 创建对象，字段值固定为 `FaceIndex=1`、`ElementIndex=2`、`Item=3`、`MyItem=4`、`BoneName=n"Bone"`、`MyBoneName=n"MyBone"` |
| 期望行为 | `FHitResult` 的 `TraceStart/TraceEnd/ImpactPoint/ImpactNormal/BoneName/MyBoneName` 读回值与写入完全一致，四个整数之和为 `10`；`FOverlapResult.GetActor()` 与 `GetComponent()` 返回刚设置的对象；`SetBlockingHit(true/false)` 后 `GetbBlockingHit()` 同步变化；`ItemIndex` 可被写成固定值并正确读回 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `ASTEST_COMPILE_RUN_INT` |
| 优先级 | P1 |

#### NewTest-13：为完全无测的 `Bind_FPlatformApplicationMisc.cpp` 建立 clipboard round-trip 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FPlatformApplicationMisc.cpp` |
| 关联函数 | `FPlatformApplicationMisc::ClipboardCopy()` / `ClipboardPaste()` |
| 现有测试覆盖 | 在 `Plugins/Angelscript/Source/AngelscriptTest/` 下未检索到 `FPlatformApplicationMisc`、`ClipboardCopy` 或 `ClipboardPaste` 的任何命中 |
| 风险评估 | copy/paste 封装如果把入参/出参桥接错、字符串编码丢失或压根没有调用平台层，当前完全没有自动化会报警；这会直接影响工具脚本和编辑器辅助脚本最常见的 clipboard 操作 |
| 建议测试名 | `Angelscript.TestModule.Bindings.PlatformApplicationMiscClipboardCompat` |
| 测试类型 | Integration |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 场景描述 | C++ 侧先读取并缓存当前 clipboard 内容，再生成一个 `FGuid` 唯一 token；脚本调用 `ClipboardCopy(Token)` 后立刻 `ClipboardPaste(Loaded)`，同时 C++ 在脚本执行后再次读取系统 clipboard 做双向核对 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；`ON_SCOPE_EXIT` 中恢复原 clipboard 内容，避免污染开发机/CI 环境；token 形如 `AngelscriptClipboard_<Guid>` |
| 期望行为 | 脚本侧 `Loaded == Token`；C++ 侧 `FPlatformApplicationMisc::ClipboardPaste()` 读回的内容也等于 `Token`；cleanup 后 clipboard 恢复到测试开始前的原值 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` + `ON_SCOPE_EXIT` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 4 | Issue-17 |
| BadIsolation | 1 | Issue-14 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingScenario: 2 |
| P2 | 1 | NoTestForSource: 1 |

**覆盖快照（按本轮 `Bindings/` 目录直读口径修正）**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮已完整读取全部 16 个现有测试文件 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `.cpp` 总数 | 126 | 其中 `Bind_*.cpp` 123 个，另有 `BlueprintCallableReflectiveFallback.cpp`、`UObjectInWorld.cpp`、`UObjectTickable.cpp` 3 个 |
| `Bindings/` 口径下已见直接测试入口的源码 `.cpp` | 42 / 126 | 41 个 `Bind_*.cpp` + `BlueprintCallableReflectiveFallback.cpp` |
| `Bindings/` 口径下未见直接测试入口的源码 `.cpp` | 84 / 126 | 含 `UObjectInWorld.cpp`、`UObjectTickable.cpp` 等非 `Bind_*.cpp` helper |

补充说明：
本轮额外 spot-check 发现，`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp` 已对 `FCollisionShape`、`FHitResult`、`FOverlapResult`、`FCollisionQueryParams` 提供 compile/parity smoke，因此它们更准确地属于“`Bindings/` 侧缺少行为级回归”，而不是“全仓完全无测试”。

#### Issue-24：`NumberFormattingOptionsCompat` 没有验证任何 setter 的可观察语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NumberFormattingOptionsCompat` |
| 行号范围 | 177-230 |
| 问题描述 | 脚本里连续调用了 `SetAlwaysSign`、`SetUseGrouping`、`SetMinimumIntegralDigits`、`SetMaximumIntegralDigits`、`SetMinimumFractionalDigits`、`SetMaximumFractionalDigits`，但后续唯一断言只是 `Options.IsIdentical(Copy)`、`Options.GetTypeHash() == Copy.GetTypeHash()` 和 `DefaultWithGrouping()` 不等于 `DefaultNoGrouping()`。也就是说，只要 copy 构造和默认工厂仍然工作，这 6 个 setter 就算全部 no-op、串绑到错误字段，当前用例也会稳定通过。 |
| 影响 | `Bind_FNumberFormattingOptions.cpp` 暴露的核心配置 API 实际没有被锁住，后续有人改坏 setter 链式返回、字段写入或数值边界时，这个测试仍会给出假绿。 |
| 修复建议 | 在脚本里把设置后的 `FNumberFormattingOptions` 传给 `FText::AsNumber` 做可观察断言，例如验证 `SetAlwaysSign(true)` 后正数带 `+`，`SetUseGrouping(false)` 后不出现分组逗号，`SetMinimumIntegralDigits(2)` / `SetMaximumFractionalDigits(3)` 对格式化输出产生精确影响；同时补一条链式调用返回同一对象的断言，避免只测 copy/hash。 |

#### Issue-25：`HashCompat` 只验证“同输入结果稳定”，没有验证绑定是否调用了正确原生函数

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.HashCompat` |
| 行号范围 | 37-95 |
| 问题描述 | 用例当前只检查 `CityHash32/64`、`CityHash64WithSeed`、`CityHash64WithSeeds` 对同一字符串重复调用时结果一致，以及无 seed 与有 seed 的一个结果不相等。`Bind_Hash.cpp` 实际同时绑定了 `FString` 和 `TArray<int8>` 两组 overload，但测试完全没有把脚本结果和 C++ 原生 `CityHash*` 返回值做精确比对。这样一来，即使脚本侧误绑到了错误的 hash 入口、错误长度计算，甚至每个函数都稳定返回一个固定常量，只要“重复调用相同输入仍然相等”，现有用例就会通过。 |
| 影响 | `Bind_Hash.cpp` 的字符串编码长度、seed 透传和 overload 分派都没有真正被验证，hash 回归会被当前 smoke test 大面积漏报。 |
| 修复建议 | 在 C++ 侧先计算 `"Alpha"` 和一个 `TArray<int8>` 样本的 `CityHash32/64/64WithSeed/64WithSeeds` 精确期望值，再注入脚本逐项 `==` 比对；至少要同时覆盖 `FString` 与 `TArray<int8>` 两套 overload，并增加“不同 seed 结果应不同但仍等于原生 API”这类双重断言。 |

#### Issue-26：`SoftPathCompat` 把路径 API 降级成“非空即可”，没有验证 round-trip 与解析语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SoftPathCompat` |
| 行号范围 | 108-184 |
| 问题描述 | 用例构造了 `FSoftObjectPath` / `FSoftClassPath`，但对 `GetAssetName()`、`GetLongPackageName()`、`Copy.ToString()` 的检查几乎都是 `IsEmpty()`/字符串自等式；`Bind_SoftObjectPath.cpp` 已经绑定的 `GetAssetPath()`、`ResolveObject()`、`TryLoad()`、`ResolveClass()`、`TryLoadClass()` 完全没被触达。即使 `GetAssetName()` 少了类型前缀、`GetLongPackageName()` 指向错误 package，或者 class/object path 的 round-trip 被改坏，只要仍返回某个非空字符串，这条测试就会通过。 |
| 影响 | `Bind_SoftObjectPath.cpp` 暴露的大部分关键语义仍处于无效覆盖状态，当前用例无法证明 path 解析、对象解析和 class 解析与 UE 原生 API 一致。 |
| 修复建议 | 在脚本里把 `AActor::StaticClass().GetPathName()` 的精确字符串拆成 package 和 asset name 做 `==` 比对，并补 `ResolveClass()` / `TryLoadClass()` 返回 `AActor::StaticClass()` 的断言；若保留 object path 场景，至少应验证 `FSoftObjectPath(AActor::StaticClass()).ResolveObject()` 与原生类对象一致，而不是只检查字段非空。 |

#### Issue-27：`MapCompareCompat` 只测“多一个 key 就不相等”，没有验证 value 差异是否参与比较

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerCompareBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MapCompareCompat` |
| 行号范围 | 89-144 |
| 问题描述 | 当前脚本先验证相同 key/value 的 `Left == Right`，随后只通过给 `Right` 额外加入 `Gamma` 来制造不相等。这样只能证明 `opEquals` 至少看到了 key 集合大小变化，却没有验证“相同 key 但不同 value”是否也会被识别。若 `Bind_TMap.cpp` 的 `opEquals` 回归成只比较 key 集合、不比较映射值，现有测试仍会稳定绿灯。 |
| 影响 | `TMap<K,V>::opEquals` 的核心语义没有被完整锁住，value 比较逻辑一旦退化，当前容器比较测试不会报警。 |
| 修复建议 | 在同一个用例里再补一组 `Right[FName("Alpha")] = 99` 的分支，断言 `Left != Right`；同时增加“两个空 map 相等”“同 key/value 但插入顺序不同仍相等”两条断言，把 `opEquals` 的最小契约补完整。 |

#### Issue-28：`MathExtendedCompat` 用大量“范围内/非零即可”的断言掩盖了确定性数学语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MathExtendedCompat` |
| 行号范围 | 26-157 |
| 问题描述 | 这个用例名义上覆盖了二十多个 `Math::` 绑定，但其中不少函数只做了宽松 smoke 检查，例如 `ClampAngle(370, -180, 180)` 只要求结果落在区间内、`GetReflectionVector` 只要求非零、`VInterpTo`/`RInterpTo`/`FInterpTo` 只要求“有点变化”、`RandomRotator(false)` 只要求“不接近零”。这些函数里很多本来是确定性输入对应确定性输出的 API，当前断言不足以证明脚本绑定和原生实现语义一致。 |
| 影响 | `Bind_FMath.cpp` 即使把参数顺序写错、返回错误分量，或者把某些 helper 绑定到“任何非零都看起来合理”的错误实现，当前测试仍可能全部通过，导致数学绑定回归长期潜伏。 |
| 修复建议 | 把随机 API 和确定性 API 分开：`ClampAngle`、`GetReflectionVector`、`LinePlaneIntersection`、`FindDeltaAngleDegrees`、`UnwindDegrees`、`ToDirectionAndLength` 这类函数改成精确值或带容差的精确比较；随机 API 则与 C++ 同时刻原生调用结果做可验证的不变量断言，避免把“非零即可”当成正确性标准。 |

#### Issue-29：`PathsCompat` 对多个确定性路径 helper 只检查“非空”，无法发现错误拼接或错误基路径

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.PathsCompat` |
| 行号范围 | 99-175 |
| 问题描述 | 用例对 `CombinePaths`、`ConvertRelativePathToFull`、`GetPath` 这些确定性 API 只做了 `IsEmpty()` 级别检查。`Bind_FPaths.cpp` 明确暴露了两组 `ConvertRelativePathToFull` overload 与 `CombinePaths`/`GetPath`，但当前没有任何断言验证拼接结果是否包含预期目录、基路径 overload 是否与原生 `FPaths` 保持一致。即使 `CombinePaths(ProjectDir, "Script/Test.as")` 漏掉分隔符、`ConvertRelativePathToFull(ProjectDir, Relative)` 忽略 `ProjectDir` 参数，只要仍返回某个非空字符串，测试就会通过。 |
| 影响 | `Bind_FPaths.cpp` 的字符串拼接和 overload 转发错误很容易被这条 smoke test 漏掉，路径语义回归不会被自动化及时发现。 |
| 修复建议 | 在 C++ 侧先计算 `ExpectedCombined = FPaths::Combine(ProjectDir, TEXT("Script/Test.as"))`、`ExpectedFull = FPaths::ConvertRelativePathToFull(ProjectDir, Relative)`，再把期望值注入脚本做 `==` 比对；同时补 `GetPath(ExpectedCombined)` 与 `ProjectDir / "Script"` 的精确匹配断言，避免只看“非空”。 |

### 二、需要新增的测试

#### NewTest-11：为 `Bind_FNumberFormattingOptions.cpp` 补齐 setter 到格式化输出的语义回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FNumberFormattingOptions.cpp` |
| 关联函数 | `SetAlwaysSign()` / `SetUseGrouping()` / `SetMinimumIntegralDigits()` / `SetMaximumIntegralDigits()` / `SetMinimumFractionalDigits()` / `SetMaximumFractionalDigits()` |
| 现有测试覆盖 | `AngelscriptCoreMiscBindingsTests.cpp` 只验证 copy/hash/default factory，不验证 setter 对结果的实际影响 |
| 风险评估 | setter 即使 no-op、串绑错误字段或链式返回错误对象，当前测试也不会报警 |
| 建议测试名 | `Angelscript.TestModule.Bindings.NumberFormattingOptionsFormattingCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` |
| 场景描述 | 在 C++ 侧先用原生 `FText::AsNumber` 计算两组格式化期望值，再让脚本用相同输入和 `FNumberFormattingOptions` 组合输出字符串，逐项比对 |
| 输入/前置 | `double Sample = 1234.5;`；一组 options 开启 `AlwaysSign`、关闭 grouping、最少 2 位整数/1 位小数、最多 3 位小数；另一组保留 grouping 作为对照 |
| 期望行为 | 脚本侧 `FText::AsNumber(Sample, Options).ToString()` 与 C++ 侧预先算出的期望字符串完全一致；关闭 grouping 后不出现分组分隔符，开启 `AlwaysSign` 后正数带显式正号，最小/最大小数位限制生效 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-12：为 `Bind_Hash.cpp` 建立字符串与字节数组 overload 的 known-answer 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Hash.cpp` |
| 关联函数 | `CityHash32()` / `CityHash64()` / `CityHash64WithSeed()` / `CityHash64WithSeeds()` |
| 现有测试覆盖 | 只有“重复调用相同输入结果一致”的 smoke test，没有 exact-value 校验，也没有 `TArray<int8>` overload 覆盖 |
| 风险评估 | 字符串长度计算、seed 透传、overload 分派或底层函数选错时，现有测试大概率仍然绿灯 |
| 建议测试名 | `Angelscript.TestModule.Bindings.HashKnownAnswerCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 场景描述 | 在 C++ 侧对 `"Alpha"` 和固定字节数组 `{0,1,2,3,4}` 先计算四组原生 CityHash 结果，再让脚本分别走 `FString` 和 `TArray<int8>` overload 返回值并逐项比对 |
| 输入/前置 | 预先在 C++ 侧计算 `Expected32/Expected64/Expected64Seed/Expected64Seeds`；seed 使用 `123` 与 `(1, 2)`；字节数组通过脚本字面量构造 |
| 期望行为 | 脚本侧四个 hash 函数对 `FString` 与 `TArray<int8>` 两种输入都与 C++ 原生结果完全一致；不同 seed 的结果既不同于无 seed 结果，也与对应原生期望值严格匹配 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-13：为 `Bind_SoftObjectPath.cpp` 增加 class/object 解析与 path round-trip 断言

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_SoftObjectPath.cpp` |
| 关联函数 | `GetAssetPath()` / `ResolveObject()` / `TryLoad()` / `ResolveClass()` / `TryLoadClass()` |
| 现有测试覆盖 | `SoftPathCompat` 只验证 `GetAssetName` / `GetLongPackageName` 非空和 `ToString()` round-trip，未覆盖解析 API |
| 风险评估 | path 构造看似成功但解析回错误对象/错误类时，当前自动化不会发现；`GetAssetPath()` 若绑定错字段也没有护栏 |
| 建议测试名 | `Angelscript.TestModule.Bindings.SoftPathResolveCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 场景描述 | 以 `AActor::StaticClass()` 为基准，在 C++ 侧准备精确 path 字符串和 `FTopLevelAssetPath`，脚本分别构造 `FSoftObjectPath` 与 `FSoftClassPath` 后执行 round-trip 和解析 |
| 输入/前置 | C++ 侧缓存 `AActor::StaticClass()->GetPathName()`、`FSoftObjectPath(AActor::StaticClass())`、`FSoftClassPath(AActor::StaticClass())` 的原生期望值，并转义进脚本 |
| 期望行为 | `GetAssetName()`、`GetLongPackageName()` 与 C++ 期望完全一致；`GetAssetPath()` 与 `FTopLevelAssetPath(ExpectedPath)` 相等；`FSoftObjectPath(...).ResolveObject()` 与 `TryLoad()` 返回 `AActor::StaticClass()`；`FSoftClassPath(...).ResolveClass()` 与 `TryLoadClass()` 返回 `AActor::StaticClass()` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-14：为 `Bind_TMap.cpp` 的 `opEquals` 增加“同 key 不同 value”与空 map 语义测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp` |
| 关联函数 | `opEquals(const TMap<K,V>&)` |
| 现有测试覆盖 | 只覆盖相等 map 和“多一个 key”导致不相等，未覆盖同 key 不同 value |
| 风险评估 | 若 `opEquals` 回归成只比较 key 集合、不比较映射值，当前测试不会报警 |
| 建议测试名 | `Angelscript.TestModule.Bindings.MapCompareValueCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerCompareBindingsTests.cpp` |
| 场景描述 | 构造三个 map：完全相同的 `Left/Right`、同 key 但 `Alpha` 值不同的 `DifferentValue`、以及空 map；分别验证相等与不相等分支 |
| 输入/前置 | `Left = { Alpha:2, Beta:5 }`；`Right = { Beta:5, Alpha:2 }`；`DifferentValue = { Alpha:99, Beta:5 }`；`Empty = {}` |
| 期望行为 | `Left == Right` 为 `true`；`Left == DifferentValue` 为 `false`；`Left == Empty` 为 `false`；`Empty == TMap<FName,int>()` 为 `true` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-15：为 `Bind_FPaths.cpp` 增加 exact path round-trip 与 overload 语义测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FPaths.cpp` |
| 关联函数 | `CombinePaths()` / `ConvertRelativePathToFull()` / `GetPath()` / `GetExtension()` / `GetBaseFilename()` |
| 现有测试覆盖 | `PathsCompat` 只验证若干返回值非空，没有把脚本结果与 C++ 原生 `FPaths` 精确对齐 |
| 风险评估 | overload 如果忽略 base path、丢分隔符或返回错误目录，现有测试仍然可能全部通过 |
| 建议测试名 | `Angelscript.TestModule.Bindings.PathsExactCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` |
| 场景描述 | 在 C++ 侧计算 `ProjectDir + "Script/Test.as"` 的精确 `Combine/ConvertRelativePathToFull/GetPath/GetExtension/GetBaseFilename` 期望值，脚本重复相同调用并逐项比对 |
| 输入/前置 | `Relative = "Script/Test.as"`；C++ 侧预先计算 `ExpectedCombined`、`ExpectedFullFromRelative`、`ExpectedFullFromBase`、`ExpectedPathOnly`、`ExpectedExtension`、`ExpectedBase` 并转义进脚本 |
| 期望行为 | 脚本侧每个返回值都与对应 C++ 期望完全一致；`DirectoryExists(ProjectDir)` 为 `true`，`FileExists(ProjectDir)` 为 `false` 保留为辅助断言而不是主断言 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-16：把 `Bind_FMath.cpp` 的确定性 helper 从随机 smoke 里拆成精确语义回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FMath.cpp` |
| 关联函数 | `ClampAngle()` / `FindDeltaAngleDegrees()` / `UnwindDegrees()` / `GetReflectionVector()` / `VInterpTo()` / `RInterpTo()` / `FInterpTo()` |
| 现有测试覆盖 | `MathExtendedCompat` 只要求结果落区间或非零，未锁住精确语义 |
| 风险评估 | 参数顺序、法线方向、插值速率或返回分量一旦绑定错误，当前测试仍可能通过 |
| 建议测试名 | `Angelscript.TestModule.Bindings.MathDeterministicCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptMathAndPlatformBindingsTests.cpp` |
| 场景描述 | 在 C++ 侧先计算一组确定性数学 helper 的期望结果，再让脚本用同样输入调用绑定函数并比较精确值或容差内结果 |
| 输入/前置 | `ClampAngle(370,-180,180)`、`FindDeltaAngleDegrees(10,20)`、`UnwindDegrees(370)`、`GetReflectionVector(FVector(1,0,0), FVector(-1,0,0))`、`VInterpTo(FVector::ZeroVector, FVector(100,0,0), 0.1f, 5.0f)`、`RInterpTo(FRotator::ZeroRotator, FRotator(0,90,0), 0.1f, 5.0f)`、`FInterpTo(0,100,0.1f,5.0f)` |
| 期望行为 | 脚本侧输出与 C++ 原生结果逐项一致；向量/旋转使用明确容差；`GetReflectionVector` 必须等于 `FVector(-1,0,0)` 而不是仅仅“非零” |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 6 | Issue-24 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 6 | MissingScenario: 5, MissingEdgeCase: 1 |

**补充说明**

| 项目 | 内容 |
|------|------|
| 本轮重点 | 继续优先清理现有绑定测试里的弱断言，暂未扩张 `NoTestForSource` 清单；源码覆盖快照沿用 `2026-04-08 13:20` 节的 41/82 基线。 |
| 编号说明 | 本轮追加内容对应 `Issue-24` 到 `Issue-29`、`NewTest-11` 到 `NewTest-16`；同一 `13:25` 时间戳下更早的 `Issue-14` 到 `Issue-23` 为前序已记录内容。 |

---

## 测试审查 (2026-04-08 13:37)

### 一、现有测试问题

#### Issue-30：`GameplayTagQueryCompat` 创建了 `MatchTag` 查询却从未验证其匹配语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagQueryCompat` |
| 行号范围 | 232-260 |
| 问题描述 | 用例构造了 `FGameplayTagQuery MatchTag = FGameplayTagQuery::MakeQuery_MatchTag(ValidTag);`，但之后只检查 `!MatchTag.IsEmpty()`，并没有像 `MatchAny` / `MatchAll` / `MatchAnyExact` / `MatchAllExact` 那样把它真正传给 `Tags.MatchesQuery(...)` 做正负向验证。也就是说，`MakeQuery_MatchTag` 这条专门的 query builder 即使返回错误查询或 `MatchesQuery` 对该 query 类型失效，只要对象不是空查询，当前测试就会绿灯。 |
| 影响 | `Bind_FGameplayTag.cpp` 中 `MakeQuery_MatchTag` 的核心语义没有被锁住，GameplayTag query 绑定很容易在“能构造对象但匹配行为错误”的状态下漏检。 |
| 修复建议 | 在现有脚本里补两条精确断言：`Tags.MatchesQuery(MatchTag)` 必须为 `true`，而空容器或不含该 tag 的容器对 `MatchTag` 必须为 `false`；同时把 `ValidTag.GetTagName()`/`ToString()` 与请求的 tag 名做 `==` 比对，避免 query 构造建立在“拿到某个有效 tag 即可”的宽松前提上。 |

### 二、需要新增的测试

#### NewTest-17：为 `Bind_FGameplayTag.cpp` 补齐 `RequestGameplayTag` exact-match 与 `MakeQuery_MatchTag` 行为回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp` |
| 关联函数 | `FGameplayTag::RequestGameplayTag()` / `GetTagName()` / `ToString()` / `FGameplayTagQuery::MakeQuery_MatchTag()` / `FGameplayTagContainer::MatchesQuery()` |
| 现有测试覆盖 | `GameplayTagCompat` 只验证“请求后 tag 有效”，`GameplayTagQueryCompat` 只验证 `MatchTag` 非空，没有验证 exact tag identity 和 query 命中语义 |
| 风险评估 | 只要绑定返回了一个“有效但不是请求目标”的 tag，或 `MatchTag` query builder 生成了错误表达式，当前自动化都可能继续绿灯 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GameplayTagExactQueryCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 场景描述 | C++ 侧先取一个已注册 tag 名并转义进脚本；脚本请求该 tag 后立即校验 `GetTagName()` 与 `ToString()` 的 exact match，再用 `MakeQuery_MatchTag(ValidTag)` 对“包含该 tag 的容器”和“空容器”分别执行 `MatchesQuery` |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN()`；C++ 侧通过 `UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, false)` 取一个已注册 tag 名，必要时额外构造空 `FGameplayTagContainer` 作为负样本 |
| 期望行为 | `RequestGameplayTag(FName(TagName), true)` 返回的 tag 满足 `GetTagName() == FName(TagName)` 且 `ToString() == TagName`；`Tags.MatchesQuery(MatchTag)` 为 `true`；空容器 `EmptyTags.MatchesQuery(MatchTag)` 为 `false`；`RequestGameplayTag(NAME_None, false)` 仍保持返回 `FGameplayTag::EmptyTag` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-30 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

**补充说明**

| 项目 | 内容 |
|------|------|
| 本轮重点 | 闭合文档末尾已存在的 `13:37` 空节，并补一条 `GameplayTagQuery` 的新弱断言与对应补测建议 |
| 覆盖口径 | 沿用前一节已修正的 `126 .cpp = 123 个 Bind_*.cpp + 3 个非 Bind helper` 统计口径，不重复展开列表 |

#### Issue-30：`GameplayTagQueryCompat` 用单标签输入验证所有 query factory，无法区分 exact / all / any 语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagQueryCompat` |
| 行号范围 | 191-287 |
| 问题描述 | 用例只构造了一个 `Tags.AddTag(ValidTag)` 的单元素容器，然后依次调用 `FGameplayTagQuery::MakeQuery_MatchAnyTags`、`MakeQuery_MatchAllTags`、`MakeQuery_ExactMatchAnyTags`、`MakeQuery_ExactMatchAllTags`、`MakeQuery_MatchTag`，最后只验证 `Tags.MatchesQuery(...)` 全部为真、`MatchNone` 为假。对于单标签输入，这几种 query 在结果上天然收敛，无法证明 `Bind_FGameplayTag.cpp` 中 6 个 factory 的 exact / all / any 绑定语义真的不同；哪怕其中一个 factory 误绑到另一个实现，当前测试也很可能照样通过。 |
| 影响 | `FGameplayTagQuery` 的关键区分语义没有被锁住，后续如果 `MatchAll` / `ExactMatchAll` / `MatchAny` 绑定到错误原生函数，自动化仍可能持续绿灯，无法反映 query 行为回归。 |
| 修复建议 | 把 query 测试拆成至少两组输入：一组使用父子层级 tag 或两个不同 tag 的容器区分 `HasAll` 与 `HasAny`，另一组使用“查询集合大于目标集合”的输入区分 exact 与非 exact；在脚本中分别对 `MatchAny`、`MatchAll`、`ExactMatchAny`、`ExactMatchAll`、`MatchTag` 写出不同的预期真值，而不是只检查“都不为空且同一个容器都能匹配”。 |

#### Issue-31：`GameplayTagContainerCompat` 只在相同单标签容器上做正向断言，测不出 exact 与层级匹配差异

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagContainerCompat` |
| 行号范围 | 98-188 |
| 问题描述 | 用例只构造了 `Tags` 和 `Others` 两个都只含 `ValidTag` 的容器，然后断言 `HasTag`、`HasTagExact`、`HasAny`、`HasAnyExact`、`HasAll`、`HasAllExact` 全都返回 `true`。在这种输入下，`exact` 与非 `exact`、`all` 与 `any` 的结果天然一致，根本无法检验 `Bind_FGameplayTag.cpp` 中这些方法是否真的绑定到了各自的原生 API。 |
| 影响 | 一旦 `HasAnyExact` / `HasAllExact` 误绑成普通 `HasAny` / `HasAll`，或者层级匹配语义被破坏，当前测试仍会稳定绿灯，无法覆盖最容易出错的 tag containment 语义。 |
| 修复建议 | 用两级 tag 关系或两标签查询把语义拉开，例如目标容器包含 `Parent.Child`，对照容器分别使用 `Parent`、`Parent.Child`、`{Parent, Other}`；分别断言 `HasTag` 为真但 `HasTagExact` 为假，`HasAny` 为真但 `HasAll` 为假，`HasAnyExact` / `HasAllExact` 与非 exact 的结果不同，同时补 `RemoveTag` 删除不存在 tag 返回 `false` 的负向断言。 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-31 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |

**补充说明**

| 项目 | 内容 |
|------|------|
| 本节状态 | `2026-04-08 13:37` 节在补录过程中连续追加；以上统计按当前文件末尾已存在条目重新收尾 |
| 覆盖口径 | 继续沿用前文修正后的 `126 .cpp = 123 个 Bind_*.cpp + 3 个非 Bind helper` 口径，不重复展开列表 |

#### Issue-32：`TArrayIteratorCompat` 声称覆盖 iterator copy/assignment，但副本变量根本没有参与断言

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.TArrayIteratorCompat` |
| 行号范围 | 316-397 |
| 问题描述 | 脚本中先遍历完 `ConstIt`，随后又写了 `TArrayConstIterator<int> AliasIt = ConstIt;`，看起来是在验证 `Bind_TArray.cpp` 暴露的 copy ctor / `opAssign` 语义；但 `AliasIt` 在后续完全没有被读取，最终 `AliasSum` 统计的是一个全新的 `MutableAliasIt`。换句话说，测试名义上覆盖了 iterator alias/copy，实际只覆盖了“重新创建一个新的 mutable iterator 再遍历一遍”。 |
| 影响 | `TArrayIterator<T>` / `TArrayConstIterator<T>` 的复制构造、赋值以及复制后 `CanProceed`/当前位置同步语义如果回归，当前测试不会报警；这正是 `Bind_TArray.cpp` 1579-1595 行专门注册的 API。 |
| 修复建议 | 让副本真正参与断言：在 `ConstIt` 前进一次后复制出 `AliasIt`，分别验证两个 iterator 的当前位置一致但互不干扰；或者在 `MutableIt` 前进到中间位置后复制，断言副本 `Proceed()` 返回剩余相同元素序列并且 `CanProceed` 在耗尽后一起转为 `false`。同时把 `TestEqual` 结果并入最终返回值，避免再次出现“脚本 return 1 即绿灯”的烟雾覆盖。 |

#### Issue-33：`SoftObjectPtrCompat` 用已加载瞬态对象跑完全流程，`IsPending` 与 path-only 语义实际上没有被验证

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SoftObjectPtrCompat` |
| 行号范围 | 84-193 |
| 问题描述 | 用例从 `NewObject<UTexture2D>` 创建已加载对象后，围绕这个 live object 依次验证 `Constructed`、`FromPath`、`AssignedFromPath`、`Assigned`、`Copy`。这样 `Bind_TSoftObjectPtr.cpp` 暴露的 `IsPending()`、`Get()` 对“仅有 path、尚未加载对象”这一核心语义完全没被触达，脚本里唯一关于 pending 的判断只是 `if (Constructed.IsPending()) return 80;`，本质上只证明“已加载对象不是 pending”。 |
| 影响 | 如果 `TSoftObjectPtr<T>` 的 path 构造、`opAssign(const FSoftObjectPath&)` 或 `Get()` 在未加载资源场景下返回了错误状态，当前测试不会发现；而 `IsPending()` 恰恰是 soft 引用与普通裸指针的关键差异。 |
| 修复建议 | 把测试拆成“已加载对象”和“未加载 path”两段：第一段保留当前 live object 校验，第二段使用一个存在但未加载的 asset path 或专门构造的 path-only soft reference，断言 `IsNull()==false`、`IsValid()==false`、`IsPending()==true`、`Get()==null`；若测试环境不方便实际加载资产，也至少要用 `FSoftObjectPath` 输入验证 `Reset()` 前后状态迁移以及 path-only 指针的 `ToSoftObjectPath()/ToString()` round-trip。 |

---

## 测试审查 (2026-04-08 13:43)

### 一、现有测试问题

#### Issue-34：`TSoftClassPtrCompat` 只覆盖已加载 class happy path，没有验证 subtype 校验与 path-only 语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.TSoftClassPtrCompat` |
| 行号范围 | 186-274 |
| 问题描述 | 用例只围绕 `AActor::StaticClass()` / `ACameraActor::StaticClass()` 这类已加载 class 做构造、赋值、`Get()`、`Reset()` 断言，脚本里的两条核心路径甚至重复检查了同一个条件：`Constructed.Get() == AActor::StaticClass()` 在 216 和 218 行被断言了两次。与此同时，`Bind_TSoftObjectPtr.cpp` 574-578 行为 `opAssign(UClass)` 专门做的 subtype rejection、以及 `TSoftClassPtr<T>` 作为 soft reference 的 path-only / pending 行为，当前测试完全没有触达。 |
| 影响 | 如果 `TSoftClassPtr<T>` 错误接受了不继承 `T` 的 `UClass`，或者 path-only soft class 的 `IsNull` / `IsValid` / `Get()` 语义退化，现有用例仍会稳定通过；重复的 `Get()` 断言还掩盖了本应覆盖更多 surface 的空间。 |
| 修复建议 | 用一个负向分支替换重复断言：对 `TSoftClassPtr<AActor>` 执行 `Assigned = UTexture2D::StaticClass()` 并通过 `AddExpectedError` 验证抛出 `"Provided class is does not inherit from TSoftClassPtr subtype."`；再补一条 path-only soft class 场景，断言 `IsNull()==false`、`IsValid()==false`、`Get()==null`，从而真正覆盖 `TSoftClassPtr` 与 `TSubclassOf` 的差异。 |

#### Issue-35：`SourceMetadataCompat` 对模块名和类型声明只做宽松断言，无法锁住 metadata accessor 的精确输出

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SourceMetadataCompat` |
| 行号范围 | 186-274 |
| 问题描述 | 这个用例已经拿到了确定性的脚本文件内容和模块名，但对 `Type.GetScriptModuleName()` 只做了 `Contains("RuntimeSourceMetadataBindingsTest")`，对 `Type.GetScriptTypeDeclaration()` 只检查“非空即可”。这意味着即便绑定返回的是带额外前后缀的模块名、错误格式的 type declaration，甚至是某个无关但非空的声明字符串，测试也依然会通过。 |
| 影响 | `Bind_UClass` / source metadata accessor 的输出格式一旦回归，当前测试很难在第一时间发现；特别是 IDE/调试器依赖这些 accessor 的精确字符串时，宽松断言会把真实回归误报成绿灯。 |
| 修复建议 | 既然脚本源码由测试内联生成，就应该把期望值也内联固定下来：把 `Contains(...)` 改成 `== "RuntimeSourceMetadataBindingsTest"`，把 `GetScriptTypeDeclaration()` 精确比对为 `class UBindingSourceMetadataCarrier : UObject` 或当前绑定实际输出格式；同时保留已有 `GetScriptFunctionDeclaration() == "int ComputeValue()"` 的精确断言，形成统一标准。 |

---

## 测试审查 (2026-04-08 13:52)

### 一、现有测试问题

#### Issue-36：`ForeachCompat` 名义上覆盖两种遍历语法，但脚本里实际只执行了 range-for

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ForeachCompat` |
| 行号范围 | 226-310 |
| 问题描述 | 用例标题和最终断言都宣称在验证“`foreach` 和 range-for compatibility syntax”，但脚本里的 `SumForeach` 与 `SumCompat` 两段循环实际都写成了 `for (int Value : AliasValues)`，见 252-260 行；文件中没有任何一段真正使用 `foreach (...)` 语法。结果是这条测试只把同一种遍历形式跑了两遍，根本没有覆盖 legacy `foreach` parser / bind surface。 |
| 影响 | 如果 `Bind_TArray.cpp` 对应的 `foreach` 兼容语法回归，而 range-for 仍然正常，这条用例会继续绿灯；测试名还会误导后续维护者以为两条语法路径都已被锁住。 |
| 修复建议 | 把 `SumForeach` 那段改成真正的 `foreach (int Value : AliasValues)`，或再补一段 `foreach (int Value, int Index : AliasValues)` 来覆盖 index 形态；同时保留现有 `for (int Value : AliasValues)` 作为 range-for 对照，并给两段遍历分别设置独立断言，避免再次出现“同一语法跑两遍”的伪覆盖。 |

#### Issue-37：`SetIteratorCompat` / `MapIteratorCompat` 只靠聚合值判断 iterator 行为，无法证明游标推进和 key/value 配对正确

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptIteratorBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SetIteratorCompat`；`Angelscript.TestModule.Bindings.MapIteratorCompat` |
| 行号范围 | 20-125 |
| 问题描述 | `SetIteratorCompat` 只在 `{2,5}` 上累计 `Sum == 7`，`MapIteratorCompat` 只验证 `Sum == 7 && KeyCount == 2`。这两条断言都没有检查空容器初始 `CanProceed == false`，也没有证明每个元素只被访问一次；`MapIteratorCompat` 还没有验证 `It.GetKey()` 与 `It.GetValue()` 始终来自同一个当前槽位。对于 iterator 来说，重复访问、跳项、`Proceed()`/`CanProceed` 状态错位，乃至 key/value 错配，只要最终聚合结果碰巧一致，就可能被当前测试放过。 |
| 影响 | `Bind_TSet.cpp` / `Bind_TMap.cpp` 的 iterator surface 一旦在游标推进、拷贝后位置同步、或 map 键值配对上回归，现有自动化的诊断信号会非常弱；尤其 `TMapIterator` 的错误更可能以“值总和看起来对，但 key/value 关系已经错了”的形式漏检。 |
| 修复建议 | 两个测试都应改成“精确访问轨迹”断言：先补空容器 `!Iterator().CanProceed`；再使用 3 个非对称元素构造样本，把迭代结果写入新的 `TSet`/`TMap` 后与期望集合做 exact compare。`MapIteratorCompat` 还应在每次 `Proceed()` 后按 `GetKey()` 分支校验对应 `GetValue()`，必要时再增加 iterator copy/assignment 场景，验证副本与原 iterator 的剩余元素集合一致。 |

#### Issue-38：`ParseCompat` 只验证单条成功路径，没有锁住“返回 `false` 且保持输出不变”的失败语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ParseCompat` |
| 行号范围 | 164-221 |
| 问题描述 | 用例只在一条格式完全正确的字符串 `\"Count=12 Ratio=3.5 Name=Alpha Enabled=true\"` 上验证 `FParse::Value` / `FParse::Bool` 的 happy path，没有任何 missing key、格式错误或输出哨兵值场景。`Bind_FParse.cpp` 当前只转发 4 个 overload，本就没有额外保护层；如果某个绑定在失败路径上错误地改写了输出引用，或者 `Bool` / `Value` 的返回值语义偏离原生 API，当前测试仍会全部通过。 |
| 影响 | `FParse` 常用于 command line、config 和文本协议解析，失败路径语义本身就是 API 契约的一部分。现有测试只锁住“能读到值”，却没有锁住“读不到时怎么失败”，会让业务脚本更容易在错误输入上出现静默状态污染。 |
| 修复建议 | 在现有用例里增加带哨兵值的负向断言：例如预先把 `MissingCount` 设为 `99`、`bEnabled` 设为 `true`，然后对缺失 key 和 `Enabled=maybe` 这类错误输入分别调用 `FParse::Value` / `FParse::Bool`，断言返回 `false` 且输出仍保持原值；这样才能证明绑定与 UE 原生 API 的失败语义一致，而不是只做“成功时能取到值”的烟雾测试。 |

### 二、需要新增的测试

#### NewTest-18：为 `Bind_FParse.cpp` 补齐失败路径与输出保持语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FParse.cpp` |
| 关联函数 | `FParse::Value(const FString&, const FString&, int&)` / `FParse::Value(..., float32&)` / `FParse::Value(..., FString&)` / `FParse::Bool(...)` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.ParseCompat` 只覆盖单条格式正确输入，完全没有缺失 key、格式错误和哨兵输出值场景 |
| 风险评估 | 如果绑定在 parse 失败时错误改写输出引用，或 `Bool` / `Value` 的返回值与 UE 原生 API 不一致，当前自动化不会报警；业务脚本会在错误输入上悄悄带着脏状态继续执行 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ParseFailureSemantics` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptUtilityBindingsTests.cpp` |
| 场景描述 | 对缺失 key、错误 bool 文本和缺失 string/value 三类输入分别调用 `FParse`，验证失败返回值与输出引用保持语义 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本中准备 `MissingSource = "Other=1"`、`BadBoolSource = "Enabled=maybe"`，并把 `MissingCount = 99`、`Ratio = 3.25f`、`Name = "Keep"`、`bEnabled = true` 作为哨兵初值 |
| 期望行为 | `FParse::Value(MissingSource, "Count=", MissingCount)` 返回 `false` 且 `MissingCount` 仍为 `99`；缺失 `Ratio=` 与 `Name=` 时同样返回 `false` 且保留原值；`FParse::Bool(BadBoolSource, "Enabled=", bEnabled)` 返回 `false` 且 `bEnabled` 仍为 `true` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-19：为 `Bind_BlueprintType.cpp` 增加 `TWeakObjectPtr` 生命周期回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` |
| 关联函数 | `TWeakObjectPtr<T>` 的默认构造 / 拷贝构造 / `opAssign(T)` / `Get()` / `IsValid()` / `IsStale()` / `IsExplicitlyNull()` |
| 现有测试覆盖 | `AngelscriptObjectBindingsTests.cpp` 只覆盖 `TObjectPtr` 与 `TSoftObjectPtr`，`TWeakObjectPtr` surface 当前完全没有直接测试 |
| 风险评估 | `TWeakObjectPtr` 是 `Bind_BlueprintType.cpp` 中单独暴露的一整组模板 API；如果 `IsStale`、`IsExplicitlyNull` 或 handle 转换语义出错，当前仓库不会有任何回归信号 |
| 建议测试名 | `Angelscript.TestModule.Bindings.WeakObjectPtrCompat` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 场景描述 | 脚本先验证默认空 `TWeakObjectPtr`，再接收一个由 C++ 创建的瞬态 `UTexture2D` 并保存为 script global；随后 C++ 释放强引用并触发 GC，再回到脚本检查 stale 语义 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本模块提供 `StoreWeak(UTexture2D Texture)` 与 `CheckWeakState(int Phase)`；C++ 侧创建瞬态 `UTexture2D`，先执行 live-object 检查，再把强引用清空并调用 `CollectGarbage(RF_NoFlags)` |
| 期望行为 | 默认态 `IsValid()==false`、`IsExplicitlyNull()==true`、`Get()==null`；绑定 live object 后 `IsValid()==true`、`IsStale()==false`、`Get()` 指向该纹理；GC 后 `IsValid()==false`、`IsStale()==true`、`IsExplicitlyNull()==false`、`Get()==null` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` + `CollectGarbage` |
| 优先级 | P1 |

#### NewTest-20：为 `Bind_TSet.cpp` 补齐空迭代器与副本剩余元素语义

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp` |
| 关联函数 | `TSet<T>::Iterator()` / `TSetIterator<T>.Proceed()` / `CanProceed` / copy ctor / `opAssign` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.SetIteratorCompat` 只在 `{2,5}` 上累计求和，没有覆盖空容器或 iterator 副本 |
| 风险评估 | iterator 初始状态、耗尽状态或副本当前位置一旦回归，当前测试很可能继续绿灯；尤其 `CanProceed` 与 `Proceed()` 的配合错误在聚合求和断言下很难暴露 |
| 建议测试名 | `Angelscript.TestModule.Bindings.SetIteratorEdgeCases` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptIteratorBindingsTests.cpp` |
| 场景描述 | 先验证空 `TSet<int>` 的 iterator 起始即不可前进；再对 `{2,9,17}` 创建 iterator，前进一步后复制出副本，分别消费两个 iterator 的剩余元素并比较精确成员集 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本中构造 `Empty` 与 `Values = {2, 9, 17}`，执行 `int First = It.Proceed(); TSetIterator<int> Alias = It;`，随后把 `It` 与 `Alias` 的剩余元素各写入一个新的 `TSet<int>` |
| 期望行为 | `Empty.Iterator().CanProceed` 为 `false`；复制后两个 iterator 的剩余元素集合完全一致，`Num()==2`，且都包含除 `First` 外的两个值；两个 iterator 在耗尽后 `CanProceed` 都为 `false` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P2 |

#### NewTest-21：为 `Bind_TMap.cpp` 增加 iterator 键值配对与空 map 语义测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp` |
| 关联函数 | `TMap<K,V>::Iterator()` / `TMapIterator<K,V>.Proceed()` / `GetKey()` / `GetValue()` / `CanProceed` / copy ctor |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.MapIteratorCompat` 只验证 `Sum==7 && KeyCount==2`，没有验证 `GetKey()` 与 `GetValue()` 的对应关系 |
| 风险评估 | 如果 iterator 在推进时把 key/value 错位、重复槽位或漏掉槽位，聚合求和与 key 计数仍可能碰巧通过；这会让 `TMap` 遍历回归长期潜伏 |
| 建议测试名 | `Angelscript.TestModule.Bindings.MapIteratorPairingCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptIteratorBindingsTests.cpp` |
| 场景描述 | 先验证空 `TMap<FName,int>` 的 iterator 起始状态；再对 `{ Alpha:2, Beta:9, Gamma:17 }` 进行遍历，每次 `Proceed()` 后根据 `GetKey()` 精确断言 `GetValue()`；前进一步后复制副本，并比较两个 iterator 的剩余 key/value 集合 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本构造三项 map，准备两个 `TMap<FName,int>` 用于记录原 iterator 和副本 iterator 的剩余访问结果 |
| 期望行为 | `Empty.Iterator().CanProceed` 为 `false`；对每个访问到的 key，`Alpha/Beta/Gamma` 分别严格对应 `2/9/17`；原 iterator 与副本 iterator 的剩余记录 map 完全一致，最终都覆盖全部原始键值对且耗尽后 `CanProceed` 为 `false` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-37 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingErrorPath: 1, MissingScenario: 2 |
| P2 | 1 | MissingEdgeCase: 1 |

**补充说明**

| 项目 | 内容 |
|------|------|
| 当前测试目录快照 | 本轮按 `2026-04-08 13:52` 的仓库状态复核，`Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 当前实际存在 16 个 `.cpp` 测试文件，不是外部描述中的 24 个 |
| 源码覆盖快照 | 沿用前文已验证的 `41` 个已见直接对应测试 / `82` 个完全无直接测试 `Bind_*.cpp` 口径，本轮未发现需要改写该基线的数据 |
| 本轮新增的覆盖结论 | 在“已存在直接测试”的文件里，进一步确认 `Bind_BlueprintType.cpp` 的 `TWeakObjectPtr`、`Bind_FParse.cpp` 的失败路径、`Bind_TSet.cpp` / `Bind_TMap.cpp` 的 iterator 边界与精确配对仍处于明显欠测状态 |

---

## 测试审查 (2026-04-08 23:35)

### 一、现有测试问题

#### Issue-56：ReflectiveFallback Eligibility 只覆盖 6 个 eligibility 结果中的 2 个，且没有校验 `ShouldBind...` 与判定函数一致

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.BlueprintCallableReflectiveFallback.Eligibility` |
| 行号范围 | 228-252 |
| 问题描述 | `BlueprintCallableReflectiveFallback.h` 当前定义了 `Eligible`、`RejectedNullFunction`、`RejectedMissingOwningClass`、`RejectedInterfaceClass`、`RejectedCustomThunk`、`RejectedTooManyArguments` 六种 eligibility 结果，但现有用例只对 interface class 和 custom thunk 做 `EvaluateReflectiveFallbackEligibility()` 断言。它既没有覆盖唯一的可绑定分支 `Eligible`，也没有覆盖 `nullptr`、无 owning class、参数过多这些明确的拒绝路径；同时 `ShouldBindBlueprintCallableReflectiveFallback()` 完全没被断言与 eligibility 结果保持一致。 |
| 影响 | eligibility 矩阵只要在剩余 4 条路径上回归，或者 `ShouldBind...` 和 `Evaluate...` 的布尔/枚举语义发生漂移，当前测试仍会全绿，无法为 reflective fallback 的准入规则提供完整护栏。 |
| 修复建议 | 扩展同一测试文件，至少补一条正常 `BlueprintCallable` 函数的 `Eligible` 断言，并对 `nullptr`、`GetOuterUClass()==nullptr` 的临时函数对象、以及 17 个以上参数的测试函数分别验证 `RejectedNullFunction`、`RejectedMissingOwningClass`、`RejectedTooManyArguments`；每个样例同时断言 `ShouldBindBlueprintCallableReflectiveFallback(Function)` 是否与期望 eligibility 完全一致。 |

#### Issue-57：MapDebuggerCompat 只覆盖顶层 `Num` 和单个 `[Alpha]` 查找，未验证 empty/missing/pair-member 分支

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerCompareBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MapDebuggerCompat` |
| 行号范围 | 217-263 |
| 问题描述 | `Bind_TMap.cpp` 的 debugger surface 既有顶层 `GetDebuggerValue()` 对空 map 返回 `Empty`、非空 map 返回 `Num = N` 的分支，也有 `GetDebuggerScope()` 暴露 pair 级别 `Key`/`Value` 成员、`GetDebuggerMember()` 处理不存在 key 时返回 `false` 的分支。当前用例只验证了非空 summary、`Num` 成员和一个 `[Alpha]` happy path，既没有验证 empty map 的摘要，也没有验证缺失 key 的 false 分支，更没有覆盖 pair 级 `Key`/`Value` 子成员。 |
| 影响 | `Bind_TMap.cpp` 的 debugger 回归很容易以“顶层 Num 还对，但 empty summary、pair scope 或 missing-key lookup 坏了”的形式漏检；调试器 API 面对回归时会缺少精确报警。 |
| 修复建议 | 在现有测试里先补空 `FScriptMap` 的 `GetDebuggerValue()` 断言，确认 `Value.Value == "Empty"`；再对不存在的 `[Gamma]` 查找断言返回 `false`；最后通过 `GetDebuggerScope()` 取出某个 pair 项，将 `Usage.TypeIndex` 带入 `GetDebuggerMember()`，精确验证 `Key` 为 `Alpha`、`Value` 为 `2`。 |

### 二、需要新增的测试

#### NewTest-22：补齐 `BlueprintCallableReflectiveFallback.cpp` 的 eligibility 矩阵与 `ShouldBind...` 一致性

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp` |
| 关联函数 | `EvaluateReflectiveFallbackEligibility()` / `ShouldBindBlueprintCallableReflectiveFallback()` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.BlueprintCallableReflectiveFallback.Eligibility` 仅覆盖 `RejectedInterfaceClass` 与 `RejectedCustomThunk` |
| 风险评估 | 其余 4 条 eligibility 分支一旦回归，或者 `ShouldBind...` 与枚举判断不再同步，当前自动化不会报警，reflective fallback 的准入规则会悄悄漂移。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.BlueprintCallableReflectiveFallback.EligibilityMatrix` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp` |
| 场景描述 | 复用现有 eligibility 测试文件，增加一个 C++ 用例同时验证 `Eligible`、`RejectedNullFunction`、`RejectedMissingOwningClass`、`RejectedTooManyArguments` 四条未覆盖分支，并对每个样例同时检查 `ShouldBindBlueprintCallableReflectiveFallback()` 返回值。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；在测试文件内声明一个 `UBlueprintFunctionLibrary` 样例类，提供一个普通 `BlueprintCallable` static UFUNCTION 和一个 17 参数的 `BlueprintCallable` static UFUNCTION；另外构造 `nullptr` 与 `NewObject<UFunction>(GetTransientPackage())` 作为无 owning class 样例。 |
| 期望行为 | 普通函数返回 `Eligible` 且 `ShouldBind... == true`；`nullptr` 返回 `RejectedNullFunction` 且 `ShouldBind... == false`；transient `UFunction` 返回 `RejectedMissingOwningClass` 且 `ShouldBind... == false`；17 参数函数返回 `RejectedTooManyArguments` 且 `ShouldBind... == false`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + 直接 C++ 调用 eligibility helper |
| 优先级 | P1 |

#### NewTest-23：为 `TOptional` 增加 unset `GetValue()` 抛错回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TOptional.h` |
| 关联函数 | `FAngelscriptOptionalBinds::GetValue_Template<T>()` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.OptionalCompat` 只验证 `Get(Default)`、`GetValue()` 的 happy path，没有触发 unset optional 的错误路径 |
| 风险评估 | 如果 `GetValue()` 在 unset 状态下不再抛错、错误文本变化、或执行继续读未初始化内存，当前测试都不会报警，`TOptional` 的核心防呆语义处于裸奔状态。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.OptionalGetValueUnsetError` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 场景描述 | 编译一个最小脚本，在 `TOptional<int> Empty;` 上直接调用 `Empty.GetValue()`，并把脚本错误当成预期结果捕获。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN` 与现有 `BuildModule` / `ExecuteIntFunction` 模式；在执行前调用 `AddExpectedError(TEXT("GetValue() called on Optional when not set! Check the optional with IsSet() first."), EAutomationExpectedErrorFlags::Contains, 1)`，并用 `ON_SCOPE_EXIT` 清理 `ASOptionalGetValueUnsetError` module。 |
| 期望行为 | 模块编译成功；执行阶段记录一次预期错误；`ExecuteIntFunction(...)` 返回 `false` 或等价的脚本执行失败信号，且不会把未初始化值当成成功结果返回。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` + `AddExpectedError` |
| 优先级 | P1 |

#### NewTest-24：为 `Bind_TMap.cpp` 增加 debugger empty/missing/pair-member 分支测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp` |
| 关联函数 | `GetDebuggerValue()` / `GetDebuggerScope()` / `GetDebuggerMember()` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.MapDebuggerCompat` 只覆盖非空 summary、`Num` 成员和一个 `[Alpha]` 查找 |
| 风险评估 | empty map 摘要、缺失 key 返回值、以及 pair 级 `Key` / `Value` 成员任一回归都不会被现有自动化捕获，debugger surface 的退化会长期潜伏。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.MapDebuggerMembersCompat` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerCompareBindingsTests.cpp` |
| 场景描述 | 先对空 `TMap<FName,int>` 验证 summary 为 `Empty` 且缺失 key 查询失败；再构造一个 key type 不走 string identifier 的 map（例如 `TMap<FVector,int>`），通过 `GetDebuggerScope()` 拿到 pair 项并继续读取其 `Key` / `Value` 子成员。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE`；复用 `FMapOperations` 手工构造一个空 `FScriptMap` 和一个带单条记录的 pair-map，准备 `FDebuggerValue` / `FDebuggerScope` 容器接收结果。 |
| 期望行为 | 空 map 的 `GetDebuggerValue()` 返回 `true` 且 `Value.Value == "Empty"`；`GetDebuggerMember(&Map, TEXT("[Gamma]"), ...)` 返回 `false`；pair-map 的子成员读取中，`Key` 与插入的 key 完全一致，`Value` 等于插入值，且作用域中仍包含 `Num` 成员。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE` + `FMapOperations` + `GetDebuggerValue/GetDebuggerScope/GetDebuggerMember` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-56 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 2 |
| P2 | 1 | MissingScenario: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮继续按完整直读口径复核，未发现新增/缺失文件 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `.cpp` 总数 | 126 | 其中 `Bind_*.cpp` 123 个，另有 `BlueprintCallableReflectiveFallback.cpp`、`UObjectInWorld.cpp`、`UObjectTickable.cpp` 3 个 |
| `Bindings/` 口径下已见直接测试入口的源码 `.cpp` | 42 / 126 | 本轮未发现需要改写既有 direct-hit 基线的新证据 |
| `Bindings/` 口径下未见直接测试入口的源码 `.cpp` | 84 / 126 | 本轮新增聚焦在“已有测试但断言弱”的区域，未扩张无测试清单 |

---

## 测试审查 (2026-04-08 23:50)

### 一、现有测试问题

#### Issue-59：`SetCompareCompat` 只验证“同集合”与“数量变化”，测不出同尺寸异成员的比较错误

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerCompareBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SetCompareCompat` |
| 行号范围 | 32-85 |
| 问题描述 | 当前脚本只覆盖两种输入：`Left={1,4}` 与 `Right={4,1}` 的相等路径，以及 `Right.Add(7)` 后数量从 2 变 3 的不等路径。对应源码 `Bind_TSet.cpp:483-492` 的 `OpEquals` 实际走 `Ops->IsPermutation(SetA, SetB)`，核心风险在“同样大小但成员不同”时是否真的逐元素比较。现有用例没有构造 `Left.Num()==Right.Num()` 但元素不同的负样本，因此如果绑定退化成只比 `Num()`、只比首个槽位，甚至把 `IsPermutation` 误换成更弱的判断，这条测试仍可能通过。 |
| 影响 | `TSet<T>::opEquals` 是容器绑定的基础语义之一。当前测试无法拦住最常见的实现回归形式，即“数量相同但内容不同仍被判等”，会让依赖 set compare 的脚本分支长期假绿。 |
| 修复建议 | 在现有脚本里补一条同尺寸负样本，例如把 `Right` 重建为 `{1,9}` 或 `{4,7}`，并断言 `Left == Right` 必须为 `false`；再追加一条“删除一个元素后补另一个元素”的场景，确保比较逻辑真的基于元素集合而不是插入顺序或数量。最终保持每个失败分支返回不同错误码，避免把多个 compare 语义压成一个尾部 `Result == 1`。 |

#### Issue-60：`MapForeach` 只校验 value 总和和 key 数量，无法证明 key/value 配对与逐项访问正确

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MapForeach` |
| 行号范围 | 390-442 |
| 问题描述 | 脚本只向 `Values` 写入 `Alpha->2`、`Beta->5` 两项，然后在 `foreach (int Value, FName Key : Values)` 中累计 `Sum` 与 `KeyCount`，最后只断言 `Sum == 7 && KeyCount == 2`。但 `Bind_TMap.cpp:1250-1285` 的 `foreach` surface 实际同时依赖 `opForValue`、`opForKey`、`opForNext` 和 iterator 边界检查。当前断言没有验证 `Alpha` 对应的值一定是 `2`、`Beta` 对应的值一定是 `5`，也没有证明每个槽位只访问一次。只要错误实现碰巧产出两个合法 key 且值总和仍为 7，这条用例就会假绿。 |
| 影响 | `TMap` 的 `foreach` 是脚本最常用的遍历入口之一。若 key/value 错位、重复访问、漏访问或 `opForKey`/`opForValue` 绑错，当前测试无法给出有效红灯，等于把最关键的遍历语义降成了一个聚合烟雾检查。 |
| 修复建议 | 把脚本改成精确配对断言：在循环内按 `Key` 分支检查 `Alpha -> 2`、`Beta -> 5`，并把访问到的键写入一个 `TSet<FName>` 或计数 map，最后断言恰好访问了两项且没有重复；最好再把样本扩成 3 组非对称键值对，降低“错误实现凑巧同和”的逃逸概率。 |

#### Issue-61：`GameplayTagCompat` 计算了 tag 标识符却完全没验证 `GameplayTags::<Tag>` 全局常量

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GameplayTagCompat` |
| 行号范围 | 25-95 |
| 问题描述 | 用例先把 `AllTags.First().ToString()` 规整成 `GameplayTagIdentifier`，但后续脚本只调用 `FGameplayTag::RequestGameplayTag(FName("%s"), true)`，从头到尾没有使用这个 identifier。与此同时，`Bind_FGameplayTag.cpp:45-178` 会通过 `Bind_AddNewGameplayTag()` / `Bind_AddNewGameplayTags()` 在 `GameplayTags` namespace 下为每个项目 tag 生成 `const FGameplayTag <SanitizedName>` 全局变量。也就是说，测试已经拿到了验证这些 globals 所需的确定性输入，却完全没有触达这块绑定 surface。 |
| 影响 | 如果 `GameplayTags::<SanitizedName>` 的名字清洗规则、全局变量注册或父 tag 扩展逻辑回归，当前 `GameplayTagCompat` 仍会绿灯；这会让项目级 tag constants 这一整层脚本入口在没有任何自动化护栏的情况下漂移。 |
| 修复建议 | 直接复用已生成的 `GameplayTagIdentifier`，在脚本中新增 `FGameplayTag GlobalFromNamespace = GameplayTags::<Identifier>;`，并精确断言它与 `RequestGameplayTag(FName(TagName), true)` 返回值相等；若当前 tag 名包含层级或非法字符，还应额外断言 sanitize 后的 identifier 仍能正确解析到同一 tag，真正锁住 `Bind_AddNewGameplayTag()` 的命名规则。 |

#### Issue-62：`ConsoleCommandSignatureCompat` 把脚本行列号写死进期望错误，格式微调就会造成无关红灯

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ConsoleCommandSignatureCompat` |
| 行号范围 | 448-506 |
| 问题描述 | 用例在 `AddExpectedError(...)` 中硬编码了 `TEXT("int Entry() | Line 8 | Col 2")`，而底层 `Bind_Console.cpp:93-95` 真正稳定的契约只是错误文本 `"Global function for console command must have signature ..."`。当前测试把嵌入脚本的空行、缩进和字符串布局也当成了 API 契约，只要脚本正文为了可读性增删一行，哪怕绑定行为完全没变，期望错误都会失配。 |
| 影响 | 这条测试会对无关的格式整理、注释插入或脚本片段重排异常敏感，制造与绑定逻辑无关的脆弱红灯；长期看会降低错误路径测试的可维护性，迫使后续修改者去“迁就行号”而不是聚焦真正的契约。 |
| 修复建议 | 保留对核心错误文本和模块名的 `AddExpectedError`，但把行列号断言改成更稳定的片段匹配，例如只匹配 `int Entry()` 或 `ASConsoleCommandSignatureCompat`；如果确实要验证 source location，应该先把脚本内容提升为具名常量并由测试代码计算期望行号，而不是把 `8:2` 这种魔法数字直接写死在断言里。 |

### 二、需要新增的测试

#### NewTest-25：补齐 `Bind_FGameplayTag.cpp` 的 `GameplayTags::<Tag>` namespace globals 与 sanitize 命名规则

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTag.cpp` |
| 关联函数 | `Bind_AddNewGameplayTag()` / `Bind_AddNewGameplayTags()` / `GameplayTags::<SanitizedTagName>` globals |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.GameplayTagCompat` 只验证 `RequestGameplayTag()`，完全没有引用 `GameplayTags` namespace 下自动生成的全局常量 |
| 风险评估 | 只要 tag 名 sanitize、全局变量注册或 parent tag 扩展逻辑回归，脚本里的项目级 tag constants 就会静默失效，而当前测试仍会全绿 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GameplayTagNamespaceGlobals` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGameplayTagBindingsTests.cpp` |
| 场景描述 | C++ 侧从 `UGameplayTagsManager` 选择一个已注册 tag，并按 `Bind_AddNewGameplayTag()` 相同规则把 tag 名 sanitize 成脚本 identifier；脚本同时读取 `GameplayTags::<Identifier>` 与 `FGameplayTag::RequestGameplayTag(FName(TagName), true)`，验证两者完全相等 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；C++ 侧优先选择 sanitize 后仍是合法 script identifier 的 tag 名，并把 `TagName` 与 `SanitizedIdentifier` 注入脚本；若该 tag 有 parent，还应一并注入 parent 名称做附加断言 |
| 期望行为 | `GameplayTags::<Identifier>.IsValid()` 为 `true`；其 `GetTagName()` / `ToString()` 与 `RequestGameplayTag(...)` 返回值完全一致；若存在 parent tag，则 `GameplayTags::<ParentIdentifier>` 也能解析且与 `ValidTag.GetGameplayTagParents()` 中对应 parent 一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-26：为 `Bind_TSet.cpp` 增加同尺寸异成员的 equality 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSet.cpp` |
| 关联函数 | `FAngelscriptSetBinds::OpEquals()` / `TSet<T>::opEquals` |
| 现有测试覆盖 | `SetCompareCompat` 只覆盖“完全相同”与“数量不同”两条路径，没有覆盖 `Num()` 相同但成员不同的 false 分支 |
| 风险评估 | 如果 `opEquals` 退化成只比较数量或只比较部分槽位，当前测试仍会全绿，set compare 的核心语义会悄悄漂移 |
| 建议测试名 | `Angelscript.TestModule.Bindings.SetCompareSameSizeMismatch` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerCompareBindingsTests.cpp` |
| 场景描述 | 脚本构造 `Left={1,4}`、`Reordered={4,1}`、`DifferentSameSize={1,9}` 三组集合，分别验证重排仍相等而同尺寸异成员必须不相等 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；沿用现有 `BuildModule` / `ExecuteIntFunction` 模式，保持集合元素为易诊断的非对称整数 |
| 期望行为 | `Left == Reordered` 为 `true`；`Left == DifferentSameSize` 为 `false`；若再构造 `Copy=Left`，则 `Copy == Left` 仍为 `true`，确保新增负样本不会破坏现有 happy path |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-27：为 `Bind_TMap.cpp` 增加 `foreach` 键值配对与重复访问防线

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TMap.cpp` |
| 关联函数 | `opForKey()` / `opForValue()` / `opForNext()` |
| 现有测试覆盖 | `MapForeach` 只验证 `Sum == 7 && KeyCount == 2`，没有验证每个 key 对应的 value、也没有验证不会重复访问 |
| 风险评估 | `foreach` 的 key/value 错位、漏访问或重复访问只要碰巧满足聚合结果，就会完全漏检，`TMap` 遍历语义缺少实质护栏 |
| 建议测试名 | `Angelscript.TestModule.Bindings.MapForeachKeyValuePairing` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 场景描述 | 脚本构造三项非对称 map，例如 `Alpha->2`、`Beta->9`、`Gamma->17`，在 `foreach (int Value, FName Key : Values)` 中按 key 分支精确断言 value，并记录每个 key 的访问次数 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本中准备 `TMap<FName,int> SeenCounts` 或 `TSet<FName> SeenKeys` 来记录访问轨迹 |
| 期望行为 | 对 `Alpha/Beta/Gamma` 分别命中 `2/9/17`；循环结束后恰好访问 3 个唯一 key、每个 key 只访问一次；空 `TMap<FName,int>` 的 `foreach` 体不会执行，访问计数保持 0 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-28：为完全无直接测试的 `Bind_UWorld.cpp` 建立 world context / globals 行为回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UWorld.cpp` |
| 关联函数 | `__WorldContext()` / `GetCurrentWorld()` / `UWorld::IsGameWorld()` / `GetPersistentLevel()` / `GetGameInstance()` / `WorldType` / `GFrameNumber` |
| 现有测试覆盖 | `Bindings/` 目录下完全没有直接覆盖 `Bind_UWorld.cpp` 的测试 |
| 风险评估 | world context、全局 world 获取与核心世界状态是大量脚本 helper 的前置依赖；一旦这些绑定回归，当前 Bindings 自动化不会给出任何直接红灯 |
| 建议测试名 | `Angelscript.TestModule.Bindings.WorldContextAndGlobalsCompat` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptWorldBindingsTests.cpp` |
| 场景描述 | 在受控测试 world 中执行脚本，读取 `__WorldContext()`、`GetCurrentWorld()`、`WorldType`、`GetPersistentLevel()`、`GetGameInstance()` 和 `GFrameNumber`，并与 C++ 侧同一时刻缓存的原生值逐项比对 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_FULL` + `FScopedTestWorldContextScope`；C++ 侧缓存 `UWorld* TestWorld`、`UObject* ContextObject`、`ULevel* PersistentLevel`、`UGameInstance* GameInstance`、`EWorldType::Type WorldType` 与当前 `GFrameNumber`，必要时把对象名或路径注入脚本用于诊断 |
| 期望行为 | `__WorldContext()` 与测试 scope 提供的 context object 一致；`GetCurrentWorld()` 与 `TestWorld` 一致；`WorldType`、`GetPersistentLevel()`、`GetGameInstance()` 与原生 API 完全一致；读取到的 `GFrameNumber` 等于 C++ 侧基线值。若后续采纳 Issue-58 的只读修复，应在同文件补一条 `GFrameNumber = 123;` 编译失败断言 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_FULL` + `FScopedTestWorldContextScope` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-29：为 `Bind_FCollisionQueryParams.cpp` 增加 ignored-lists、bitfield 与 response container 行为回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FCollisionQueryParams.cpp` |
| 关联函数 | `FCollisionQueryParams::AddIgnoredActor()` / `GetIgnoredActors()` / `AddIgnoredComponent()` / `GetIgnoredComponents()` / `FCollisionObjectQueryParams::AddObjectTypesToQuery()` / `GetQueryBitfield()` / `FCollisionResponseContainer::SetResponse()` / `CreateMinContainer()` |
| 现有测试覆盖 | `Bindings/` 目录里没有行为级用例；当前只在其他目录见到 compile/parity smoke，且 `GlobalVariableCompat` 仅顺带碰到 `FComponentQueryParams::DefaultComponentQueryParams.ShapeCollisionMask.Bits` |
| 风险评估 | ignored actor/component 列表、对象查询 bitfield 与 response container 是 collision 查询链路的核心输入；若数组复制、bitfield 构造或 response 映射错误，现有 Bindings 测试几乎不会报警 |
| 建议测试名 | `Angelscript.TestModule.Bindings.CollisionQueryParamsBehaviour` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCollisionParamsBindingsTests.cpp` |
| 场景描述 | C++ 侧创建一个瞬态 `AActor` 和 `UPrimitiveComponent`，脚本构造 `FCollisionQueryParams` / `FComponentQueryParams` / `FCollisionObjectQueryParams` / `FCollisionResponseContainer`，依次添加 ignored actor/component、调整 object query channel 和 response，再把结果回传给 C++ 校验 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；C++ 侧缓存 actor unique ID、component unique ID 以及原生 `FCollisionObjectQueryParams` / `FCollisionResponseContainer` 的基线结果；脚本中给 `TraceTag`、`OwnerTag`、`bTraceComplex`、`ShapeCollisionMask.Bits` 等字段赋确定值 |
| 期望行为 | `GetIgnoredActors()` / `GetIgnoredComponents()` 返回数组包含刚加入的 ID；`ClearIgnoredActors()` / `ClearIgnoredComponents()` 后数组清空；`GetQueryBitfield()` 与原生 `FCollisionObjectQueryParams` 基线一致；`SetResponse()` / `CreateMinContainer()` 后各 channel 响应与原生结果一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` + C++ 原生基线对比 |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-61 |
| AntiPattern | 1 | Issue-62 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 4 | MissingScenario: 3, MissingEdgeCase: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 2026-04-08 仓库实物数；与任务描述中的 24 文件清单不一致，本轮按仓库现状逐文件直读 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `.cpp` 总数 | 126 | 其中 `Bind_*.cpp` 123 个，非 `Bind_*.cpp` helper 3 个 |
| 已见直接对应测试的 `Bind_*.cpp` | 41 / 123 | 沿用前文人工 direct-hit 基线；本轮新增的是质量问题与补测建议，不改已验证映射 |
| 当前未见直接对应测试的 `Bind_*.cpp` | 82 / 123 | 本轮新增优先补测 `Bind_UWorld.cpp` 与 `Bind_FCollisionQueryParams.cpp`，其余未直测清单沿用前文 |

---

## 测试审查 (2026-04-09 00:07)

### 一、现有测试问题

#### Issue-63：`SourceMetadataCompat` 把脚本函数行号硬编码成 `6`，无关排版调整也会触发假失败

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptFileAndDelegateBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SourceMetadataCompat` |
| 行号范围 | 223-245 |
| 问题描述 | 运行时脚本里直接把 `if (Func.GetSourceLineNumber() != 6) return 60;` 写死，见 243 行。这个期望值取决于内联 `Script` 文本当前恰好把 `int ComputeValue()` 放在第 6 行；只要后续为了可读性给嵌入脚本增删空行、注释或属性，这条断言就会失败，即使 `GetSourceLineNumber()` 绑定本身仍然正确。 |
| 影响 | 测试会对无关的脚本格式整理异常敏感，制造与绑定逻辑无关的红灯；维护者在调整脚本示例排版时必须同步手改魔法数字，降低 `SourceMetadataCompat` 的稳定性和可维护性。 |
| 修复建议 | 不要把 `6` 写死在脚本断言里。更稳妥的做法是在 C++ 侧根据 `Script` 文本动态计算 `int ComputeValue()` 所在行号，再把该数字注入运行时查询脚本；如果当前只想锁住“有正确行号信息”，也至少改成先断言 `Func.GetSourceLineNumber() > 0`，并把精确行号校验迁移到由测试代码生成的期望值，而不是手写魔法数字。 |

#### Issue-64：`ComponentDestroyCompat` 只验证未注册组件的销毁标记，测不出 `DestroyComponent()` 的真实清理语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ComponentDestroyCompat` |
| 行号范围 | 214-278 |
| 问题描述 | 用例只创建了一个 transient `AActor` 和未注册的 `UActorComponent`，调用前仅做 `OuterActor->AddOwnedComponent(RuntimeComponent)`，没有 `RegisterComponent()`、没有放入 world，也没有附着层级。脚本调用 `DestroyComponent()` 后，C++ 侧唯一行为断言是 `RuntimeComponent->IsBeingDestroyed()`。这只能证明组件进了“正在销毁”状态，却没有验证 `DestroyComponent()` 对 owner/component 列表、注册状态和生命周期清理的真实语义。 |
| 影响 | 如果 `Bind_UActorComponent.cpp` 的 `DestroyComponent()` 绑定在 registered component 路径上漏掉了 `UnregisterComponent()`、没有从 owner 的组件集合移除，或只做了部分状态标记，当前用例仍会稳定通过，因为未注册 transient 组件本来就不会覆盖这些关键行为。 |
| 修复建议 | 把该用例升级为 world-backed 场景：使用 `ASTEST_CREATE_ENGINE_FULL` + `FScopedTestWorldContextScope` 创建测试 world，生成并 `RegisterComponent()` 一个真实挂到 actor 上的组件；脚本调用 `DestroyComponent()` 后，除 `IsBeingDestroyed()` 外，还要断言 `!IsRegistered()`、owner 的 `GetComponentsByClass()`/组件列表里不再包含该实例，并验证二次调用不会产生额外副作用。这样才能真正锁住 `DestroyComponent()` 的绑定语义，而不是只测一个销毁标记。 |

### 二、需要新增的测试

#### NewTest-30：为 `Bind_FInputActionValue.cpp` 建立 `opMulAssign` 链式赋值回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputActionValue.cpp` |
| 关联函数 | `FInputActionValue::operator*=` / `operator+=` / `GetAxis1D()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 当前 `opMulAssign` 声明与 native 返回值语义已经在发现文档里暴露出分叉风险；没有直接测试时，链式赋值退化成 by-value 临时对象也不会有自动化红灯。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.InputActionValueMulAssignCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp` |
| 场景描述 | 脚本构造 `FInputActionValue Value(2.0f)`，执行 `(Value *= 0.5f) *= 0.5f`，再对结果做一次 `+=`，验证返回引用和原对象都按 UE 原生语义被更新。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本中准备 `FInputActionValue Value(2.0f)`、`FInputActionValue Delta(1.0f)`，依次调用 `opMulAssign` 和 `opAddAssign`。 |
| 期望行为 | 第一次链式执行后 `Value.GetAxis1D()` 精确等于 `0.5f`；随后 `Value += Delta` 后 `GetAxis1D()` 等于 `1.5f`；`Value.IsNonZero()` 为 `true`。如果 `opMulAssign` 仍按 by-value 暴露，链式分支会停在 `1.0f`，测试必须红灯。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-31：为 `Bind_UEnhancedInputComponent.cpp` 建立 `const` 句柄清理接口的编译约束测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UEnhancedInputComponent.cpp` |
| 关联函数 | `ClearActionEventBindings()` / `ClearActionValueBindings()` / `ClearDebugKeyBindings()` / `ClearActionBindings()` / `HasBindings()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 这四个 mutating API 当前被绑定成 `const`；如果没有编译级回归，脚本就能通过只读 `UEnhancedInputComponent` 修改内部绑定状态，const-correctness 回退会长期潜伏。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.EnhancedInputComponentConstCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp` |
| 场景描述 | 先编译一个只读路径脚本，声明 `const UEnhancedInputComponent Comp` 并尝试调用 `Comp.ClearActionBindings()`；再编译一个可写路径脚本，对非 `const` 组件调用 `ClearActionBindings()` 与 `HasBindings()`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；只读脚本配合 `AddExpectedError` 订阅“无法在 const 对象上调用非 const 方法”类错误；可写路径脚本只需成功编译并执行空 `Entry()`。 |
| 期望行为 | `const` 路径必须编译失败并命中期望错误；可写路径必须编译成功，且 `HasBindings()` 这类真正只读方法在 `const`/非 `const` 两种上下文都可用。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `AddExpectedError` |
| 优先级 | P0 |

#### NewTest-32：为 `Bind_FInputBindingHandle.cpp` 建立 `FInputDebugKeyBinding::Execute` 签名回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInputBindingHandle.cpp` |
| 关联函数 | `FInputBindingHandle::GetHandle()` / `FEnhancedInputActionEventBinding::Execute()` / `FEnhancedInputActionValueBinding::GetValue()` / `FInputDebugKeyBinding::Execute()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 当前 `FInputDebugKeyBinding::Execute(const FInputActionValue&)` 的 owner type 已在源码分析里暴露出 copy-paste 风险；如果没有 compile smoke，这类签名错绑只会在后续重编译或业务脚本首次调用时暴露。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.InputDebugKeyBindingExecuteCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEnhancedInputBindingsTests.cpp` |
| 场景描述 | 编译一个只做签名解析的脚本函数，参数包含 `FEnhancedInputActionEventBinding&`、`FEnhancedInputActionValueBinding&`、`FInputDebugKeyBinding&`、`const FInputActionInstance&`、`const FInputActionValue&`，在函数体内调用三类 binding 的 `GetHandle()`/`Execute()`/`GetValue()`。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本无需真正构造 Enhanced Input 对象，只需把相关类型作为参数传入编译器做重载解析，并保留一个 `int Entry() { return 1; }` 作为执行入口。 |
| 期望行为 | 模块必须稳定编译成功，且 `Entry()` 执行返回 `1`；如果 `FInputDebugKeyBinding::Execute` 仍绑定到错误 owner type，编译阶段就应红灯。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-33：为 `Bind_FGameplayEffectSpec.cpp` 增加空 `UGameplayEffect` 构造保护测试

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayEffectSpec.cpp` |
| 关联函数 | `FGameplayEffectSpec(const UGameplayEffect InDef, const FGameplayEffectContextHandle& InEffectContext, float32 Level = -1.f)` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 该构造当前直接 placement-new 原生 `FGameplayEffectSpec`；如果脚本传入 `null` effect，最坏会直接撞上 GAS 内部 `check(Def)`，现有 `Bindings/` 自动化不会有任何提前预警。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GameplayEffectSpecNullDefGuard` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGASValueBindingsTests.cpp` |
| 场景描述 | 脚本构造一个空 `UGameplayEffect` 句柄和默认 `FGameplayEffectContextHandle`，尝试执行 `FGameplayEffectSpec Spec(NullEffect, Context, 1.0f)`，验证 binding 层把错误前移成脚本错误而不是进程断言。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；C++ 侧通过 `AddExpectedError` 订阅 `"GameplayEffect was null."` 或等价明确错误文本；脚本入口只执行这一次构造。 |
| 期望行为 | 执行必须以脚本错误方式失败并命中期望错误；测试进程不得触发原生 `check` 崩溃。修复后如 binding 选择构造一个默认 spec 作为占位，还应补充断言 `Spec.Def == null`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `AddExpectedError` + 手动执行 context/`ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-34：为 `Bind_FGameplayTagBlueprintPropertyMap.cpp` 增加 `Initialize` 空参数防线

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FGameplayTagBlueprintPropertyMap.cpp` |
| 关联函数 | `Initialize(UObject Owner, UAbilitySystemComponent ASC)` / `ApplyCurrentTags()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 当前 binding 直接暴露原生 `Initialize()`；空 `Owner`/`ASC` 只会在 GAS 日志里报错并提前返回，脚本层看起来像“调用成功”，失败重绑还可能保留旧注册状态。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.GameplayTagBlueprintPropertyMapInitializeNullGuards` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptGASValueBindingsTests.cpp` |
| 场景描述 | 在同一个测试里分别执行 `Map.Initialize(null, null)` 与 `Map.Initialize(this, null)` 两条负向路径，验证 binding 会向脚本抛出显式错误，而不是只留下 GAS 日志。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；`AddExpectedError` 分别订阅 `"GameplayTagBlueprintPropertyMap.Initialize received a null Owner."` 和 `"GameplayTagBlueprintPropertyMap.Initialize received a null AbilitySystemComponent."`；脚本先声明 `FGameplayTagBlueprintPropertyMap Map` 再调用 `Initialize(...)`。 |
| 期望行为 | 两条负向路径都必须以脚本错误结束；执行失败后不应继续进入 `ApplyCurrentTags()`，也不应把失败初始化伪装成成功返回。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `AddExpectedError` + 手动执行 context/`ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-63 |
| WeakAssertion | 1 | Issue-64 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 3 | NoTestForSource: 3 |
| P1 | 2 | NoTestForSource: 2 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 2026-04-09 仓库实物数；与任务描述中的 24 文件清单不一致，本轮按仓库现状逐文件完整直读 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `.cpp` 总数 | 126 | 其中 `Bind_*.cpp` 123 个，非 `Bind_*.cpp` 支撑文件 3 个 |
| 已见直接对应测试入口的运行时 bind `.cpp` | 42 / 126 | 沿用前文 direct-hit 基线，本轮未发现需要改写该基线的新证据 |
| 当前未见直接对应测试入口的运行时 bind `.cpp` | 84 / 126 | 本轮新增建议优先聚焦 `Bind_FInputActionValue.cpp`、`Bind_UEnhancedInputComponent.cpp`、`Bind_FInputBindingHandle.cpp`、`Bind_FGameplayEffectSpec.cpp`、`Bind_FGameplayTagBlueprintPropertyMap.cpp` |

---

## 测试审查 (2026-04-09 00:20)

### 一、现有测试问题

#### Issue-65：`ObjectEditorOnlyCompat` 只验证 transient package 的 false 分支，几乎测不出 `IsEditorOnly()` 绑定是否正确

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ObjectEditorOnlyCompat` |
| 行号范围 | 144-159 |
| 问题描述 | 用例脚本只做了一件事：`UPackage Package = GetTransientPackage(); if (Package.IsEditorOnly()) return 10;`。这里既没有构造任何 editor-only object，也没有把脚本结果与 C++ 原生 `UObject::IsEditorOnly()` 基线值做对比。对于 `Bind_UObject.cpp` 里这个 trivial 绑定来说，如果脚本桥接错误地永远返回 `false`，当前测试在 transient package 上仍会稳定通过。 |
| 影响 | 该用例名义上覆盖了 `IsEditorOnly()`，实际只锁住了一个“本来就应为 false”的负路径，无法发现 true 分支丢失、对象类型判断错误或桥接退化成常量 `false` 的回归。 |
| 修复建议 | 把测试升级成 true/false 双路径校验：在 C++ 侧准备一个明确非 editor-only 的对象和一个明确 editor-only 的对象，并把两者路径注入脚本后分别调用 `IsEditorOnly()`；同时把 C++ 原生返回值作为基线注入脚本做精确比较。若当前最容易构造的是 package，可在 C++ 侧创建新 package 并设置 editor-only flag，再让脚本通过 `FindObject()` 取回对象验证。 |

#### Issue-66：`ConsoleCommandCompat` 只验证参数个数，命令参数内容与顺序桥接仍然是盲区

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ConsoleCommandCompat` |
| 行号范围 | 295-338 |
| 问题描述 | `OnCommand(const TArray<FString>& Args)` 里唯一的可观察行为是 `Output.SetInt(Args.Num());`，随后 C++ 侧只断言输出变量等于 `3`。这证明了 command delegate 被调用，但没有验证 `Bind_Console.cpp` 中 `Context->SetArgAddress(0, (void*)&Args)` 传进脚本的字符串内容、顺序和逐项完整性是否正确。只要数组长度没丢，参数被重排、截断或元素内容错误，当前测试都会继续通过。 |
| 影响 | Console command 最关键的 `Args` marshalling 语义没有自动化护栏；后续如果 `TArray<FString>` 桥接在空格、特殊字符、顺序保留或字符串生命周期上回退，这个用例仍会给出假绿。 |
| 修复建议 | 把 `OnCommand` 的观测值从“参数个数”升级为“参数序列内容”。例如让脚本把 `Args` 用稳定分隔符拼成 `One|Two Words|Three=Value` 写回 `FConsoleVariable` 或 native sink，然后在 C++ 侧精确断言序列和值顺序；同时补一个空参数执行分支，验证 `Args.Num()==0` 时脚本收到的确实是空数组而不是残留旧值。 |

#### Issue-67：`ReflectiveFallback.GameplayTags` 对容器类 fallback 只验数量不验内容，第二、第三个调用仍是烟雾测试

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptBlueprintCallableReflectiveFallbackTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.BlueprintCallableReflectiveFallback.GameplayTags` |
| 行号范围 | 186-199 |
| 问题描述 | 该用例先用 `%sGetTagName(ValidTag)` 做了一次精确比对，这是有效断言；但后面 `FGameplayTagContainer Container = %sMakeGameplayTagContainerFromTag(ValidTag); if (%sGetNumGameplayTagsInContainer(Container) != 1) return 30;` 只检查容器数量。也就是说，`MakeGameplayTagContainerFromTag()` 如果插入了错误 tag、重复 tag，甚至 fallback 错绑到“总是返回单元素容器”的错误函数，只要 `GetNumGameplayTagsInContainer()` 仍返回 `1`，测试就会通过。 |
| 影响 | 当前用例名义上覆盖了三个 reflective fallback 调用，实际上只有 `GetTagName()` 被语义锁住；另外两个 GameplayTags library 入口仍停留在“调用过且没崩”的级别，无法阻止返回值语义漂移。 |
| 修复建议 | 在脚本里把容器内容也纳入断言：至少补 `Container.HasTagExact(ValidTag)`、`Container.Num()==1`、`Container.First()==ValidTag` 这类精确检查，并把 `%sGetNumGameplayTagsInContainer(Container)` 与 `Container.Num()` 做一致性比对。这样才能同时锁住 `MakeGameplayTagContainerFromTag()` 和 `GetNumGameplayTagsInContainer()` 两条 reflective fallback 语义。 |

### 二、需要新增的测试

#### NewTest-35：为 `Bind_UObject.cpp` 补齐 `IsEditorOnly()` 的 true/false 对照测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 关联函数 | `UObject::IsEditorOnly()` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.ObjectEditorOnlyCompat` 只在 `GetTransientPackage()` 上验证 false 分支 |
| 风险评估 | 如果绑定退化成“永远返回 false”或无法识别 editor-only flag，当前 Bindings 自动化不会报警 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ObjectEditorOnlyParity` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` |
| 场景描述 | C++ 侧创建两个可被脚本 `FindObject()` 取回的对象：一个保持默认 non-editor-only，另一个显式设置 editor-only 状态；脚本分别对两者调用 `IsEditorOnly()`，并与 C++ 侧同一时刻读取到的原生返回值比较 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；C++ 侧创建唯一命名对象并缓存其路径与 `IsEditorOnly()` 结果，必要时对 editor-only 对象设置对应 flag；脚本用注入的路径字符串查找对象 |
| 期望行为 | non-editor-only 对象脚本返回 `false`；editor-only 对象脚本返回 `true`；两条路径都与 C++ 原生结果完全一致，且 `FindObject()` 不返回 `null` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-36：为 `Bind_Console.cpp` 增加 console command 参数内容与顺序桥接测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.cpp` |
| 关联函数 | `FScriptConsoleCommand::FScriptConsoleCommand(const FString& Name, const FString& FunctionName)` / `FConsoleCommand` 构造绑定 |
| 现有测试覆盖 | `ConsoleCommandCompat` 只验证 delegate 被调用且 `Args.Num()==3` |
| 风险评估 | `TArray<FString>` 参数如果被重排、截断、转义错误或复用旧数组，当前测试仍然会通过 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ConsoleCommandArgumentMarshalling` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleCommandArgumentBindingsTests.cpp` |
| 场景描述 | 注册一个脚本 console command，把收到的 `Args` 按顺序拼成稳定字符串并写回测试输出；C++ 侧分别以空参数和包含空格/等号的参数数组执行命令，验证脚本看到的内容与顺序完全一致 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；准备唯一 command 名和输出 sink，执行两轮命令：`{}` 与 `{\"One\", \"Two Words\", \"Three=Value\"}` |
| 期望行为 | 空参数执行后输出为约定的空序列标记；第二轮执行后输出精确等于 `One|Two Words|Three=Value`；discard module 后命令被注销，后续查找返回 `null` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + 现有 `ExecuteConsoleCommand`/`VerifyConsoleCommandMissing` helper（无则提到 `Bindings/Shared/`） |
| 优先级 | P1 |

#### NewTest-37：为完全无直测的 `Bind_JsonObjectConverter.cpp` 建立 USTRUCT round-trip 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_JsonObjectConverter.cpp` |
| 关联函数 | `FJsonObjectConverter::UStructToJsonObjectString()` / `AppendUStructToJsonObjectString()` / `JsonObjectStringToUStruct()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | JSON converter 绑定如果把 type-erased `?&in` / `?&out` 路径接错、序列化字段丢失或反序列化失败，当前 Bindings 测试不会提供任何回归信号 |
| 建议测试名 | `Angelscript.TestModule.Bindings.JsonObjectConverterRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptJsonObjectConverterBindingsTests.cpp` |
| 场景描述 | 脚本构造一个 `FVector Original(1.0, 2.0, 3.0)`，先用 `UStructToJsonObjectString()` 序列化，再用 `JsonObjectStringToUStruct()` 反序列化到 `FVector Parsed`；随后再把 `FRotator(10,20,30)` 追加到同一 JSON 字符串，验证 append 结果包含两组字段 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本内准备 `FVector Original`、`FVector Parsed`、`FRotator ExtraRotation` 和 `FString Json` |
| 期望行为 | `UStructToJsonObjectString(Original, Json, 0, 0, 0, false)` 返回 `true` 且 `Json` 非空；`JsonObjectStringToUStruct(Json, Parsed)` 返回 `true` 且 `Parsed.Equals(Original)`；`AppendUStructToJsonObjectString(ExtraRotation, Json, 0, 0, 0, false)` 返回 `true` 且追加后的字符串同时包含 `X`/`Y`/`Z` 与 `Pitch`/`Yaw`/`Roll` 关键字段 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-38：为 `Bind_JsonObjectConverter.cpp` 增加非 `USTRUCT` 与非法 JSON 的失败语义测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_JsonObjectConverter.cpp` |
| 关联函数 | `FJsonObjectConverter::UStructToJsonObjectString()` / `JsonObjectStringToUStruct()` |
| 现有测试覆盖 | 完全无测试 |
| 风险评估 | 当前 source 内显式处理“不是合法 USTRUCT”和“无法解析 JSON”两条失败路径；若失败时返回值、日志或 out 参数语义回退，没有任何自动化保护 |
| 建议测试名 | `Angelscript.TestModule.Bindings.JsonObjectConverterErrorPaths` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptJsonObjectConverterBindingsTests.cpp` |
| 场景描述 | 脚本先把 `int PlainValue = 7` 传给 `UStructToJsonObjectString()`，再把非法 JSON 字符串传给 `JsonObjectStringToUStruct()` 解析到带哨兵值的 `FVector Parsed`，验证两条路径都以 `false` 返回而不是崩溃或写脏输出 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本中令 `FString Json = "Seed"`、`FVector Parsed(9.0, 9.0, 9.0)` 作为失败前哨兵值 |
| 期望行为 | `UStructToJsonObjectString(PlainValue, Json, ...)` 返回 `false` 且 `Json` 仍保持初始值；`JsonObjectStringToUStruct("{", Parsed, ...)` 返回 `false` 且 `Parsed` 仍等于 `(9,9,9)`；整个执行路径不触发 crash |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-66 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 3 | MissingScenario: 2, MissingErrorPath: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮继续按仓库实物逐文件完整直读 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `.cpp` 总数 | 126 | 其中 `Bind_*.cpp` 123 个，非 `Bind_*.cpp` 支撑文件 3 个 |
| 已见直接对应测试入口的运行时 bind `.cpp` | 42 / 126 | 本轮未发现足以改写既有 direct-hit 基线的新证据 |
| 当前未见直接对应测试入口的运行时 bind `.cpp` | 84 / 126 | 本轮新增优先聚焦 `Bind_JsonObjectConverter.cpp`，并补充 `Bind_UObject.cpp`、`Bind_Console.cpp` 的关键遗漏场景 |

---

## 测试审查 (2026-04-09 00:32)

### 一、现有测试问题

#### Issue-68：`ArrayForeach` 只校验总和与索引和，锁不住 value/index 配对和访问顺序

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ArrayForeach` |
| 行号范围 | 297-332 |
| 问题描述 | 用例只把 `TArray<int>{1,2,5}` 遍历后的 `Sum == 8` 与 `IndexSum == 3` 当作成功条件。这个聚合断言无法证明 `foreach (Value, Index)` 每一步给出的 value/index 来自同一槽位，也无法证明访问顺序仍然是 `[(1,0),(2,1),(5,2)]`。如果绑定把索引和值错配、重排访问次序，甚至重复访问一个槽位但恰好保持相同的总和，当前测试仍可能绿灯。 |
| 影响 | `Bind_TArray.cpp` 上 `foreach` 相关绑定一旦在 value/index 对齐、顺序稳定性或单次访问语义上回退，`ArrayForeach` 不能提供精确告警，导致问题只能在更高层脚本逻辑里晚发现。 |
| 修复建议 | 把断言改成“精确访问轨迹”而不是聚合值：在脚本里把每次遍历得到的 `Value` 和 `Index` 编码进 `TArray<FString>` 或 `FString`，最终断言精确等于 `1@0|2@1|5@2`；同时补一个空数组分支，验证 `foreach` 在 `Num()==0` 时不会错误进入循环。 |

#### Issue-69：`OptionalTypeCompareCompat` 使用运行时内部 API 直接比较，无法证明脚本侧 `TOptional` 比较绑定真的可用

| 项目 | 内容 |
|------|------|
| 问题类型 | WrongHelper |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerCompareBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.OptionalTypeCompareCompat` |
| 行号范围 | 146-214 |
| 问题描述 | 这个用例没有编译或执行任何脚本，而是直接构造 `FAngelscriptTypeUsage`、`FOptionalOperations` 和 `FAngelscriptOptional`，再在 C++ 里调用 `CanCompare()` / `IsValueEqual()`。它验证的是运行时内部比较器实现，不是 `Bind_TOptional.h` 暴露给脚本的 `==` / `!=` surface。即使脚本层忘记注册比较操作符、绑定签名错误，或者脚本比较路径与内部 helper 脱节，这个测试也仍然会通过。 |
| 影响 | 当前文件名义上在做 Bindings 审查，实际却把 `TOptional` compare 的核心断言放在运行时内部 helper 上，导致“脚本可比较”这一用户可见契约没有自动化护栏。 |
| 修复建议 | 若目标是验证绑定层，应追加一个真正的脚本测试，例如编译 `TOptional<int> Left(7); TOptional<int> Right(7); return (Left == Right && Left != TOptional<int>()) ? 1 : 0;`，并覆盖 unset/set、相同值/不同值两组场景；现有这条内部 API 测试如仍有价值，应移到 `AngelscriptRuntime/Tests/` 或显式改名，避免被误认为脚本绑定回归。 |

#### Issue-70：`ClassLookupCompat` 只验证 `AActor` happy path，`ClassLookup` 标题下的大部分 bind surface 仍是空白

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ClassLookupCompat` |
| 行号范围 | 47-92 |
| 问题描述 | 整个用例只做了两件事：`FindClass("AActor")` 不为 `null`，以及 `GetAllClasses()` 结果里能找到同一个 `AActor`。它没有验证 `UClass::FindClass(...)`、`GetAllSubclassesOf(...)`、查找 miss 时返回 `null`、以及返回类是否与 UE 原生类表保持一致。对于 `Bind_UObject.cpp` 里这一整组 class lookup helper 来说，当前断言基本停留在“全局类表里确实有个 AActor”。 |
| 影响 | 只要最基础的 `AActor` happy path 还活着，`ClassLookupCompat` 就会一直给绿灯；而命名空间查找、子类枚举、miss/null 处理、脚本类 tombstone 过滤等更容易回退的路径完全没有被现有用例锁住。 |
| 修复建议 | 把当前 smoke 拆成多条 focused 断言：保留 `FindClass("AActor")` 基础路径，同时补 `UClass::FindClass("Actor")`、`GetAllSubclassesOf(AActor::StaticClass(), OutClasses)`、`FindClass("DefinitelyMissingType") == null` 等脚本用例，并在 C++ 侧用原生 `FindObject` / `TObjectIterator<UClass>` 结果做精确对照。 |

#### Issue-71：`NativeComponentMethods` 先手动清空输出数组，掩盖了 `GetComponentsByClass` 是否会错误累积旧元素

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.NativeComponentMethods` |
| 行号范围 | 151-165 |
| 问题描述 | 用例在调用 `GetOwner().GetComponentsByClass(SceneComponents);` 之前，先把 `SceneComponents` 填入一个元素再立即 `Empty()`；而 `AllComponents` 路径更是直接从空数组开始。这样写只能验证“空数组输入时能取到一个组件”，却完全测不到 bind 是否像原生 `GetAllActorsOfClass()` 一样先 reset 输出数组，还是把结果追加到已有内容后面。`Bind_AActor.cpp` / `Bind_UActorComponent.cpp` 上最容易出现的 out-array 累积 bug，当前用例被自己预先 `Empty()` 掩盖掉了。 |
| 影响 | 如果 `GetComponentsByClass` 在脚本侧回退成 append 语义，业务里复用输出数组会悄悄积累陈旧元素，但当前 `NativeComponentMethods` 仍会稳定通过，无法为 out-array 契约提供保护。 |
| 修复建议 | 把两个数组路径都改成“带哨兵元素输入”：先往 `SceneComponents` / `AllComponents` 里塞一个不会出现在真实结果里的占位组件或重复元素，然后调用 `GetComponentsByClass(...)` 并断言结果只剩当前 owner 的真实组件、`Num()==1`、且不存在旧哨兵。这样才能锁住“调用后输出数组被覆盖而不是累积”的真实语义。 |

### 二、需要新增的测试

#### NewTest-39：为 `Bind_FInstancedStruct.cpp` 增加 `Make / Get / GetMutable / Reset` round-trip 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInstancedStruct.cpp` |
| 关联函数 | `FInstancedStruct::Make(const ?&in)` / `InitializeAs(const ?&in)` / `Contains(const UScriptStruct)` / `Get(?&out)` / `GetMutable(const UScriptStruct)` / `Reset()` / `GetScriptStruct()` |
| 现有测试覆盖 | 完全无测试；`Bindings/` 和 `Script/` 全局搜索 `FInstancedStruct` 均未命中 |
| 风险评估 | `FInstancedStruct` 绑定同时承载 wildcard 类型推导、struct 深拷贝和可变引用返回；如果 `Make` 初始化错误、`GetMutable` 没有真正回写底层内存，或者 `Reset()`/`Contains()` 语义漂移，当前没有任何 Bindings 自动化会报警 |
| 建议测试名 | `Angelscript.TestModule.Bindings.InstancedStructRoundTrip` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInstancedStructBindingsTests.cpp` |
| 场景描述 | 脚本先用 `FVector Seed(1,2,3)` 构造 `FInstancedStruct Value = FInstancedStruct::Make(Seed)`，随后验证类型、取值、可变引用写回和 reset 后状态 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本中构造 `FVector Seed(1.0, 2.0, 3.0)`，调用 `Value.Contains(FVector::StaticStruct())`、`Value.Get(Copy)`、`Value.GetMutable(FVector::StaticStruct()).Z = 9.0f`、`Value.Reset()` |
| 期望行为 | `Value.IsValid()` 初始为 `true`；`Contains(FVector::StaticStruct())` 为 `true`；`Get(Copy)` 后 `Copy.Equals(FVector(1,2,3), 0.001f)`；`GetMutable(...).Z = 9.0f` 后再次 `Get(Copy)` 得到 `(1,2,9)`；`Reset()` 后 `!Value.IsValid()` 且 `!Value.Contains(FVector::StaticStruct())` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-40：为 `Bind_FInstancedStruct.cpp` 增加 empty/mismatch 错误路径回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FInstancedStruct.cpp` |
| 关联函数 | `FInstancedStruct::Get(const UScriptStruct)` / `Get(?&out)` / `InitializeAs(const ?&in)` |
| 现有测试覆盖 | 完全无测试；source 内 `"Source is empty or not valid"` 与 `"Mismatching types"` 两条错误文本没有任何回归 |
| 风险评估 | `FInstancedStruct` 的高风险点正是空值读取和类型不匹配；如果这些分支不再抛脚本错误，而是返回垃圾引用或静默继续执行，会直接把类型错误扩大成后续随机内存问题 |
| 建议测试名 | `Angelscript.TestModule.Bindings.InstancedStructErrorPaths` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptInstancedStructBindingsTests.cpp` |
| 场景描述 | 一条脚本在空 `FInstancedStruct` 上执行 `Get(...)`；另一条脚本先存入 `FVector`，再尝试按 `FRotator` 读取，分别验证明确错误 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；对空值路径调用 `AddExpectedError(TEXT("Source is empty or not valid"), EAutomationExpectedErrorFlags::Contains, 1)`；对错型路径调用 `AddExpectedError(TEXT("Mismatching types"), EAutomationExpectedErrorFlags::Contains, 1)` |
| 期望行为 | 空值读取必须以脚本错误结束，不得继续返回默认 struct；错型读取必须命中 `"Mismatching types"` 错误；两条路径都不应 crash，也不应把 out 参数写成半初始化值 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `AddExpectedError` + 手动执行 context/`ExecuteIntFunction` |
| 优先级 | P0 |

#### NewTest-41：为 `Bind_FStringTableRegistry.cpp` 建立 `LOCTABLE_*` 内存表 round-trip 回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FStringTableRegistry.cpp` |
| 关联函数 | `LOCTABLE_NEW()` / `LOCTABLE_SETSTRING()` / `LOCTABLE_SETMETA()` / `LOCTABLE()` |
| 现有测试覆盖 | 完全无测试；`Bindings/` 和 `Script/` 全局搜索 `LOCTABLE` / `StringTableRegistry` 均未命中 |
| 风险评估 | String table 绑定包含全局注册、文本查找和元数据写入三类 side effect；如果 table id、namespace、key 或 metadata 透传错位，当前既不会有脚本红灯，也不会有注册表级回归提示 |
| 建议测试名 | `Angelscript.TestModule.Bindings.StringTableRegistryLocTableCompat` |
| 测试类型 | Scenario |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptStringTableBindingsTests.cpp` |
| 场景描述 | 脚本创建唯一 `TableId` 的内存 string table，写入一条 source string 和一条 metadata，再通过 `LOCTABLE()` 取回 `FText`；C++ 侧随后直接查询 `FStringTableRegistry` 验证注册结果 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；C++ 侧生成唯一 `FName TableId` 并在 `ON_SCOPE_EXIT` 调用 `FStringTableRegistry::Get().UnregisterStringTable(TableId)`；脚本执行 `LOCTABLE_NEW(TableId, "AS.Test.Namespace")`、`LOCTABLE_SETSTRING(TableId, "Greeting", "Hello")`、`LOCTABLE_SETMETA(TableId, "Greeting", n"Comment", "Doc")` |
| 期望行为 | 脚本侧 `LOCTABLE(TableId, "Greeting").ToString()` 精确等于 `"Hello"`；C++ 侧 `FindStringTable(TableId)` 非空；`GetSourceString("Greeting", OutSource)` 得到 `"Hello"`；`GetMetaData("Greeting", "Comment")` 得到 `"Doc"` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` + C++ 侧 `FStringTableRegistry` 验证 |
| 优先级 | P1 |

#### NewTest-42：为 `Bind_FLatentActionInfo.cpp` 增加构造器与属性透传回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FLatentActionInfo.cpp` |
| 关联函数 | `FLatentActionInfo(int32,int32,FName,UObject)` ctor / `Linkage` / `UUID` / `ExecutionFunction` / `CallbackTarget` |
| 现有测试覆盖 | 完全无测试；`Bindings/` 和 `Script/` 全局搜索 `FLatentActionInfo` 未命中 |
| 风险评估 | 这个 bind 直接手写字段赋值；如果构造参数顺序、`FName` 透传或 `CallbackTarget` 句柄绑定错位，所有依赖 latent action 的脚本都会静默带着错误参数进入后续 async 节点 |
| 建议测试名 | `Angelscript.TestModule.Bindings.LatentActionInfoCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptLatentActionInfoBindingsTests.cpp` |
| 场景描述 | 脚本使用 `GetTransientPackage()` 作为 callback target 构造 `FLatentActionInfo Info(3, 77, n"FinishAction", Package)`，然后逐字段校验并复制一份对照默认构造值 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本构造 `UPackage Package = GetTransientPackage()`、`FLatentActionInfo Info(3, 77, n"FinishAction", Package)`、`FLatentActionInfo Copy = Info`、`FLatentActionInfo DefaultInfo` |
| 期望行为 | `Info.Linkage == 3`、`Info.UUID == 77`、`Info.ExecutionFunction == n"FinishAction"`、`Info.CallbackTarget == Package`；`Copy` 四个字段与 `Info` 一致；`DefaultInfo.Linkage == 0`、`DefaultInfo.UUID == 0`、`DefaultInfo.ExecutionFunction.IsNone()`、`DefaultInfo.CallbackTarget == null` |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P2 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-70 |
| WrongHelper | 1 | Issue-69 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |
| P1 | 1 | NoTestForSource: 1 |
| P2 | 1 | NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 2026-04-09 仓库实物数，继续与任务描述中的 24 文件清单不一致 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 本轮按 `Bind_*.cpp` 单独统计 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 全部 `.cpp` | 126 | 其中 3 个是非 `Bind_*.cpp` 支撑文件 |
| `Bind_*.cpp` 在 `Bindings/` 测试里的 token direct-hit | 37 / 123 | 保守字符串命中口径，会低估 `Bind_Delegates.cpp` 这类“文件名与测试符号不一致”的已测区域 |
| `Bind_*.cpp` 在 `Bindings/` 测试里未见 token direct-hit | 86 / 123 | 本轮新增聚焦 `Bind_FInstancedStruct.cpp`、`Bind_FStringTableRegistry.cpp`、`Bind_FLatentActionInfo.cpp` 三个完全无命中的 bind |

---

## 测试审查 (2026-04-09 00:47)

### 一、现有测试问题

#### Issue-72：`ObjectPtrCompat` / `SoftObjectPtrCompat` 丢弃最终断言结果后仍无条件返回 `true`

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptObjectBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ObjectPtrCompat`；`Angelscript.TestModule.Bindings.SoftObjectPtrCompat` |
| 行号范围 | 78-81；189-192 |
| 问题描述 | 两个 `RunTest` 在脚本执行完成后都只是调用 `TestEqual(...)`，随后直接 `return true;`。这使最终通过条件不再由当前函数显式维护，而是完全依赖 `TestEqual` 的副作用记录。只要前面的执行路径没有提前 `return false`，函数返回值永远是成功。 |
| 影响 | 这类绑定兼容测试的最后一道语义断言被弱化成“写日志式”校验，后续若有人把 `TestEqual` 改成条件分支外的软检查，或在同一函数尾部再加清理逻辑，失败传播就更容易被继续吞掉。 |
| 修复建议 | 参考同文件中 `Module == nullptr` 等早退风格，把 `TestEqual` 结果保存为 `const bool bPassed = TestEqual(...);`，在 `ASTEST_END_SHARE` 之后返回 `bPassed`；若想同时保留更多尾部断言，就显式用 `bPassed &= TestEqual(...)` 汇总，而不是固定 `return true`。 |

#### Issue-73：`ConsoleVariableExistingCompat` 没有验证“复用现有 cvar 对象”这一核心契约

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ConsoleVariableExistingCompat` |
| 行号范围 | 221-268 |
| 问题描述 | 用例在 222 行先保存了 `IConsoleVariable* ExistingVariable = RegisterConsoleVariable(...)`，脚本里也声明自己在测 “Should reuse existing native cvar”，但执行后只做了 `VerifyConsoleVariableInt(..., 21)`。这只能证明同名条目的值变成了 `21`，不能证明绑定没有把原条目卸掉再重新注册一个新 cvar。 |
| 影响 | 如果 `Bind_Console.cpp` 未来回退成“同名时替换对象而不是复用对象”，当前用例依然会绿灯；而这类回退会悄悄丢失原生 cvar 的对象身份、flags、回调和外部持有指针，属于测试目标与断言对象不一致。 |
| 修复建议 | 在执行后显式取回 `IConsoleVariable* CurrentVariable = IConsoleManager::Get().FindConsoleVariable(*ExistingName);`，先 `TestEqual(TEXT("Existing cvar pointer should be reused"), CurrentVariable, ExistingVariable)`，再校验值变为 `21`；若担心接口返回包装层，可额外断言原 help text / flags 保持不变，确保不是“删旧建新”。 |

#### Issue-74：`GuidCompat` 用 `GetTypeHash() != 0` 充当有效性断言，锁住了并不存在的哈希契约

| 项目 | 内容 |
|------|------|
| 问题类型 | FlakyRisk |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCoreMiscBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.GuidCompat` |
| 行号范围 | 67-71 |
| 问题描述 | 用例对 `FGuid NewGuid = FGuid::NewGuid();` 的第二条断言是 `if (NewGuid.GetTypeHash() == 0) return 110;`。但 `GetTypeHash()` 的职责只是提供散列值，并没有“合法对象哈希绝不为 0”的公开语义；把非零哈希当成正确性条件，等于把实现细节写成了测试契约。 |
| 影响 | 未来如果哈希实现调整、平台差异导致某些合法 `FGuid` 恰好散列到 `0`，测试会在绑定完全正确时产生无关红灯；反过来，这条断言也没有验证任何脚本侧 `FGuid` 绑定独有语义。 |
| 修复建议 | 把这条检查替换成确定性的语义断言，例如 `NewGuid != FGuid()`、`FGuid::Parse(NewGuid.ToString(...), Parsed)` 后 `Parsed == NewGuid`，或把 `NewGuid` 放进 `TSet<FGuid>`/`TMap<FGuid, ...>` 验证 hash-based container round-trip，而不是直接假设 hash 值非零。 |

#### Issue-75：`TSubclassOfCompat` 完全跳过了模板子类校验失败分支

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.TSubclassOfCompat` |
| 行号范围 | 117-160 |
| 问题描述 | 用例只覆盖了 `AActor::StaticClass()`、`ACameraActor::StaticClass()` 这些合法输入的 happy path，验证了构造、赋值、隐式转换和 `GetDefaultObject()`；但 `Bind_TSubclassOf.h` 的 `ImplicitConstruct()` / `SetClass()` 明确在不匹配模板子类时会 `FAngelscriptEngine::Throw("Class set to TSubclassOf<> was not a child of templated class.")` 并把值重置为 `nullptr`。当前测试对这条唯一显式错误路径完全零覆盖。 |
| 影响 | 一旦 `TSubclassOf<T>` 的子类过滤回退，脚本就可能把 `UTexture2D::StaticClass()` 之类的无关 `UClass` 塞进 `TSubclassOf<AActor>` 而不报错；现有用例仍会因为合法路径保持可用而持续绿灯，无法发现模板约束失效。 |
| 修复建议 | 在现有文件里补一个负向分支或新增 focused 测试：对 `TSubclassOf<AActor>` 执行 `Assigned = UTexture2D::StaticClass()` 和 `TSubclassOf<AActor> Invalid(UTexture2D::StaticClass())`，用 `AddExpectedError(TEXT("Class set to TSubclassOf<> was not a child of templated class."), ...)` 锁住错误文本，并断言失败后 `!Assigned.IsValid()`、`Assigned.Get() == null`。 |

### 二、需要新增的测试

#### NewTest-43：为 `Bind_TSubclassOf.h` 补齐不兼容 `UClass` 的错误路径回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_TSubclassOf.h` |
| 关联函数 | `FAngelscriptSubclassOfHelpers::ImplicitConstruct()` / `SetClass()` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.TSubclassOfCompat` 只覆盖 `AActor` / `ACameraActor` 的合法输入，没有触发 `Class set to TSubclassOf<> was not a child of templated class.` |
| 风险评估 | 模板子类保护一旦失效，脚本可以把无关 `UClass` 静默写进 `TSubclassOf<AActor>`；后续 `GetDefaultObject()`、`IsChildOf()` 和工厂调用都会在更远的位置暴露错误。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.TSubclassOfRejectsUnrelatedClass` |
| 测试类型 | Unit |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptClassBindingsTests.cpp` |
| 场景描述 | 先验证 `TSubclassOf<AActor> Value = null;` 的清空语义；再执行 `TSubclassOf<AActor> Invalid(UPackage::StaticClass())` 和 `Value = UPackage::StaticClass()` 两条非法路径，分别锁定 implicit ctor 与 `opAssign(UClass)` 的错误处理。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；对两条非法路径各调用一次 `AddExpectedError(TEXT("Class set to TSubclassOf<> was not a child of templated class."), EAutomationExpectedErrorFlags::Contains, 1)`；脚本中同时准备 `TSubclassOf<AActor> Value` 和一个合法 `AActor::StaticClass()` 基线值。 |
| 期望行为 | `Value = null` 后 `!Value.IsValid()`、`Value.Get() == null`、`Value.GetDefaultObject() == null`；两条非法路径都必须导致执行失败并命中预期错误文本，不能静默接受 `UPackage::StaticClass()`。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `AddExpectedError` + `BuildModule` + 手动 `asIScriptContext` 执行检查 |
| 优先级 | P0 |

#### NewTest-44：把 `ConsoleVariableExistingCompat` 补成“复用现有对象”而不是“同名值变了”回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Console.cpp` |
| 关联函数 | `FConsoleVariable` 四个 ctor overload 在同名 cvar 已存在时的复用路径 |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.ConsoleVariableExistingCompat` 只验证同名条目的值更新为 `21`，未验证 `IConsoleVariable*` 身份是否被复用 |
| 风险评估 | 如果绑定回退成“先卸掉旧 cvar 再注册新对象”，值断言仍会通过，但外部持有的 native 指针、flags、help text 和回调都会悄悄丢失。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ConsoleVariableExistingIdentityCompat` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptConsoleBindingsTests.cpp` |
| 场景描述 | C++ 侧先注册一个 native int cvar 并保存 `IConsoleVariable* ExistingVariable`；脚本用相同名字构造 `FConsoleVariable` 并写入新值；执行后同时检查“值更新”和“对象未被替换”。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；预注册唯一名字的 native cvar，记录 `ExistingVariable` 指针、初始 help text 与 flags；脚本执行 `FConsoleVariable ExistingVar(Name, 99, "Should not replace native cvar"); ExistingVar.SetInt(21);`。 |
| 期望行为 | `IConsoleManager::Get().FindConsoleVariable(*Name)` 返回的指针必须与 `ExistingVariable` 完全相同；`GetInt()` 为 `21`；原 help text / flags 保持 native 注册时的值，不允许“同名删旧建新”。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` + 现有 `VerifyConsoleVariableInt`/新增 `VerifyConsoleVariableIdentity` helper |
| 优先级 | P1 |

#### NewTest-45：为 `ObjectCastCompat` 补齐 null handle 与 interface cast 的直接绑定回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingEdgeCase |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UObject.cpp` |
| 关联函数 | `UObject::opCast(?& Address) const` |
| 现有测试覆盖 | `Angelscript.TestModule.Bindings.ObjectCastCompat` 只验证 `Cast<UPackage>(FindObject(...))` 和一个生成类 success path，没有覆盖 `null` 输入，也没有覆盖 interface cast success/fail |
| 风险评估 | `opCast` 是所有 `UObject` 向下转型共享的基础入口；当前若 null cast 崩溃、interface cast 错误接受或错误拒绝，现有 Bindings 测试不会直接报警。 |
| 建议测试名 | `Angelscript.TestModule.Bindings.ObjectCastNullAndInterfaceCompat` |
| 测试类型 | Scenario |
| 测试文件 | 追加到 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` |
| 场景描述 | 第一段脚本验证 `UObject NullObject = null; UPackage NullPackage = Cast<UPackage>(NullObject);` 必须稳定得到 `null`；第二段脚本复用 `Plugins/Angelscript/Source/AngelscriptTest/Interface/AngelscriptInterfaceCastTests.cpp` 里的 `ScenarioInterfaceCastSuccess` / `UIDamageableCastOk` 模式，在 Bindings 目录下建立一个 success/fail 对照 cast。 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；null cast 场景用 plain module；interface cast 场景用 `CompileAnnotatedModuleFromMemory` 编译一对 `AActor` + script interface 类型，分别准备实现接口和未实现接口的对象。 |
| 期望行为 | null cast 场景执行成功且 `NullPackage == null`，不能 crash；interface success 路径返回已实现接口的对象；interface fail 路径返回 `null`；三条路径都不应额外污染日志为通过条件。 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `CompileAnnotatedModuleFromMemory` + `ExecuteGeneratedIntEventOnGameThread` |
| 优先级 | P0 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 3 | Issue-75 |
| FlakyRisk | 1 | Issue-74 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | MissingErrorPath: 1, MissingEdgeCase: 1 |
| P1 | 1 | MissingScenario: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 2026-04-09 仓库实物数，仍与任务描述中的 24 文件清单不一致 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 全部 `.cpp` | 126 | 当前目录实物统计 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 本轮 direct-hit 统计口径 |
| `Bind_*.cpp` 在 `Bindings/` 测试里的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对当前 16 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试里未见 token direct-hit | 84 / 123 | 仍有大量 bind surface 完全缺少 `Bindings/` 侧直测入口 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 全部 `.h` | 24 | 其中本轮额外识别到一个 header-level gap：`Bind_TSubclassOf.h`，不计入上面的 `.cpp` direct-hit 口径 |

---

## 测试审查 (2026-04-09 01:06)

### 一、现有测试问题

#### Issue-76：`DateTimeCompat` 对格式化 API 只做非空断言，无法锁住 `Bind_FDateTime.cpp` 的字符串语义

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.DateTimeCompat` |
| 行号范围 | 338-345 |
| 问题描述 | 用例先构造了确定性的 `FDateTime Constructed(2024, 12, 25, 14, 30, 15, 0)`，但对 `Constructed.ToIso8601()`、`Constructed.ToString()`、`Constructed.ToString("%%Y-%%m-%%d")` 的校验都只是 `IsEmpty()`。`Bind_FDateTime.cpp` 明确绑定了 `ToIso8601()`、`ToString()`、`ToString(const FString&)`，其中 format overload 还是自定义 lambda 转发；如果绑定把 format 字符串传错、丢了时间部分、或把 `ToIso8601()` 误绑到普通 `ToString()`，当前测试仍然会稳定通过。 |
| 影响 | `Bind_FDateTime.cpp` 的字符串格式化与 round-trip surface 处于假覆盖状态，后续格式化回归很可能只在上层脚本文本比较时晚发现，而不是在绑定回归测试阶段直接报警。 |
| 修复建议 | 既然输入时间是确定性的，就应该把输出也锁成确定值：例如断言 `Constructed.ToIso8601()` 等于原生 `FDateTime(2024, 12, 25, 14, 30, 15, 0).ToIso8601()`，`Constructed.ToString("%%Y-%%m-%%d") == "2024-12-25"`，并补一条 `FDateTime::ParseIso8601(Constructed.ToIso8601(), Parsed)` / `FDateTime::Parse(Constructed.ToString(), Parsed)` 的 round-trip 断言，验证返回值和解析结果都与原生 API 一致。 |

#### Issue-77：`TimespanCompat` 用单条 happy-path 烟雾测试覆盖 `FTimespan`，大部分已绑定 surface 仍未被真正验证

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptCompatBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.TimespanCompat` |
| 行号范围 | 196-249 |
| 问题描述 | 当前脚本只覆盖了 `Zero()`、`FromSeconds()`、`FromHours()`、三参数构造、`opAdd/opSub/opMul/opDiv`、`opCmp/opEquals`、`GetTotalDays()` 和一个 `ToString().IsEmpty()` 检查。对 `Bind_FTimespan.cpp` 已暴露的 `int64 Ticks` ctor、`(Days,Hours,Minutes,Seconds)` / `(Days,Hours,Minutes,Seconds,FractionNano)` ctor、`opSub()` unary minus、`opAddAssign/opSubAssign/opMulAssign/opDivAssign`、`opMod/opModAssign`、`Ratio()`、`MaxValue()/MinValue()`、`GetDuration()`、`GetFraction*()`、`GetTicks()`、`ToString(const FString)` 全部没有断言。测试名却仍然声称 “Timespan compat operations should behave as expected”。 |
| 影响 | `Bind_FTimespan.cpp` 里最容易在重载转发、自定义 lambda 和边界值上出错的 surface 基本处于无保护状态；一旦这些 API 回退，当前文件仍会因为少数 happy path 存活而持续给出假绿。 |
| 修复建议 | 把 `FTimespan` 覆盖拆成至少两条 focused 用例：一条锁构造/格式化/比较，另一条锁高级算子与 fraction/ticks 语义。对确定性输出改成 exact assert，例如用 `FTimespan(1,2,3,4,500000000).GetFractionMilli()`、`FTimespan::Ratio(...)`、`ToString("%d.%h:%m:%s")` 和 `MaxValue().opCmp(MinValue())` 做精确比对，而不是继续停留在“非零/非空/大于零”。 |

### 二、需要新增的测试

#### NewTest-46：为 `Bind_FDateTime.cpp` 补齐 parse / http / iso8601 round-trip 与 out 参数语义测试

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingErrorPath |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FDateTime.cpp` |
| 关联函数 | `Parse()` / `ParseHttpDate()` / `ParseIso8601()` / `GetDate(int& OutYear, int& OutMonth, int& OutDay)` / `ToHttpDate()` / `GetHour12()` / `GetMillisecond()` / `GetTicks()` |
| 现有测试覆盖 | `DateTimeCompat` 只覆盖 ctor、`DaysInMonth/Year`、`Now/UtcNow/Today` 和若干字符串“非空”检查，没有任何 parse 或 out-ref 验证 |
| 风险评估 | `Bind_FDateTime.cpp` 里除了简单直绑，还有 `ToString(const FString&)` / `ParseIso8601()` 这类自定义转发点；一旦 bool 返回值、out 参数写入或字符串 round-trip 偏离原生 API，当前不会有任何精确回归信号 |
| 建议测试名 | `Angelscript.TestModule.Bindings.DateTimeParseRoundTripCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptDateTimeBindingsTests.cpp` |
| 场景描述 | C++ 侧先对固定输入 `"2024-12-25T14:30:15Z"`、`"Wed, 25 Dec 2024 14:30:15 GMT"`、以及 `FDateTime(2024, 12, 25, 14, 30, 15, 123)` 的 `ToHttpDate()` / `ToIso8601()` 计算原生基线；脚本侧分别执行 `ParseIso8601`、`ParseHttpDate`、`Parse`、`GetDate(out...)` 和 `GetTicks()`，再与原生基线逐项对齐 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；在 C++ 侧预先求出每组字符串的原生 `bool` 返回值、解析后的 `FDateTime`、`Year/Month/Day`、`Hour12`、`Millisecond`、`Ticks`，通过 `FString::Printf` 注入脚本；再准备一条明显非法的日期字符串作为 failure path，对比脚本与原生 API 的返回值/输出结果是否一致 |
| 期望行为 | `ParseIso8601` / `ParseHttpDate` / `Parse` 对固定合法输入都必须与 C++ 原生 API 得到相同的 `bool` 和 `FDateTime`；`GetDate(out...)` 写出的 `Year/Month/Day`、`GetHour12()`、`GetMillisecond()`、`GetTicks()` 必须与原生值精确相等；非法输入时脚本侧返回值必须与原生 `FDateTime::Parse*` 一致，不能静默伪造成功结果 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-47：为 `Bind_FTimespan.cpp` 增加 advanced operators / fraction / min-max 的精确语义回归

| 项目 | 内容 |
|------|------|
| 新增原因 | MissingScenario |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FTimespan.cpp` |
| 关联函数 | `FTimespan(int64 Ticks)` / `FTimespan(int32 Days, int32 Hours, int32 Minutes, int32 Seconds)` / `FTimespan(int32 Days, int32 Hours, int32 Minutes, int32 Seconds, int32 FractionNano)` / `opSub()` / `opAddAssign()` / `opSubAssign()` / `opMulAssign()` / `opDivAssign()` / `opMod()` / `opModAssign()` / `Ratio()` / `GetDuration()` / `GetFraction*()` / `GetTicks()` / `MaxValue()` / `MinValue()` / `ToString(const FString)` |
| 现有测试覆盖 | `TimespanCompat` 仅覆盖少量 happy path，未触达 ticks ctor、fraction getters、mod/ratio、assign operators、min/max 和 format overload |
| 风险评估 | `Bind_FTimespan.cpp` 同时绑定多组重载和自定义 `ToString(format)` lambda；这些 surface 一旦在参数顺序、返回值或边界处理上回退，现有测试几乎不会报警 |
| 建议测试名 | `Angelscript.TestModule.Bindings.TimespanAdvancedCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTimespanBindingsTests.cpp` |
| 场景描述 | 用确定性的 ticks 和带纳秒分数的构造输入，分别验证 ctor、fraction getters、`GetTicks()`、一元负号、`GetDuration()`、`%` / `%=`、`Ratio()`、`+=/-=/\*=//=`、`MaxValue()/MinValue()` 比较，以及 `ToString(const FString)` 的格式化输出 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；脚本中构造 `FTimespan TicksBased(900000000)`、`FTimespan Detailed(1, 2, 3, 4, 500000000)`、`FTimespan Divisor = FTimespan::FromMinutes(30.0)`、`FTimespan Dividend = FTimespan::FromMinutes(95.0)` 等固定样本，并在 C++ 侧预先用原生 `FTimespan` 计算期望 ticks、fraction、ratio 和格式化字符串 |
| 期望行为 | `TicksBased.GetTicks()`、`Detailed.GetFractionMilli()/GetFractionNano()/GetFractionTicks()`、`(-Dividend).GetDuration()`、`FTimespan::Ratio(Dividend, Divisor)`、`Dividend % Divisor`、`Dividend %= Divisor`、`MaxValue().opCmp(MinValue()) > 0`、`Detailed.ToString("%d.%h:%m:%s")` 都必须与原生基线精确一致；assign operators 更新后的 ticks 也必须逐步匹配预期值 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

#### NewTest-48：为 `Bind_FFormatArgumentValue.cpp` 建立 ordered / named text-format 构造器回归

| 项目 | 内容 |
|------|------|
| 新增原因 | NoTestForSource |
| 关联源码 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_FFormatArgumentValue.cpp` |
| 关联函数 | `FFormatArgumentValue(int32)` / `(uint32)` / `(int64)` / `(uint64)` / `(float32)` / `(float64)` / `(const FText&)` / `(ETextGender)` |
| 现有测试覆盖 | 完全无测试；`Bindings/` 目录和当前缺口文档里都还没有针对 `FFormatArgumentValue` 构造器分派的专门回归 |
| 风险评估 | 这些 ctor 全靠 overload 绑定；一旦类型宽化、符号分派或 late-bind 的 `FText` / `ETextGender` 构造器接错，脚本侧 `FText::Format` 很容易出现错值、错类型甚至只在特定参数组合下失效，而当前不会有任何报警 |
| 建议测试名 | `Angelscript.TestModule.Bindings.FormatArgumentValueCompat` |
| 测试类型 | Unit |
| 测试文件 | 新建 `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptTextFormattingBindingsTests.cpp` |
| 场景描述 | C++ 侧先用原生 `FFormatArgumentValue` 组装一组 ordered arguments 和一组 named arguments，分别计算 `FText::Format` 的期望字符串；脚本侧用相同的 `int32/uint32/int64/uint64/float32/float64/FText` 输入构造 `TArray<FFormatArgumentValue>` 与 `TMap<FString, FFormatArgumentValue>`，再执行 `FText::Format` 并对比结果。若工程支持 gender-aware 格式串，再额外加入一条 `ETextGender` 构造器的原生对照 |
| 输入/前置 | 使用 `ASTEST_CREATE_ENGINE_SHARE_CLEAN`；固定输入例如 `-7`、`42u`、`9000000000ll`、`15ull`、`3.25f`、`6.5`、`FText::FromString("Alpha")`，以及一条包含有序占位符和命名占位符的格式串；C++ 侧先算出 ordered/named 两个期望文本，再通过 `FString::Printf` 注入脚本 |
| 期望行为 | 脚本侧 ordered / named `FText::Format(...).ToString()` 必须与原生基线精确一致；每种数值类型都要以对应 ctor 构造，不能通过隐式转换绕开被测重载；若启用 gender-aware 格式串，脚本侧 `ETextGender` 构造结果也必须与原生 `FFormatArgumentValue(ETextGender::...)` 的格式化文本一致 |
| 使用的 Helper | `ASTEST_CREATE_ENGINE_SHARE_CLEAN` + `BuildModule` + `ExecuteIntFunction` |
| 优先级 | P1 |

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-77 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 3 | MissingErrorPath: 1, MissingScenario: 1, NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮已逐文件全文审查完成，继续与任务描述中的 24 文件口径不一致 |
| 本轮新增明确断言不足的已测 bind surface | 2 | `Bind_FDateTime.cpp`、`Bind_FTimespan.cpp` |
| 本轮新增明确“完全无直测入口”的 bind `.cpp` | 1 | `Bind_FFormatArgumentValue.cpp` |

---

## 测试审查 (2026-04-09 01:13)

### 一、现有测试问题

#### Issue-78：`SetForeach` 只累计求和，无法证明 `Bind_TSet.cpp` 的 foreach 每个元素恰好访问一次

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.SetForeach` |
| 行号范围 | 352-364 |
| 问题描述 | 用例只在 `{2, 5}` 上执行 `foreach (int Value : Values)`，最终断言仅为 `Sum == 7`。`Bind_TSet.cpp` 的 foreach surface 实际经过 `opForBegin` / `opForNext` / `opForValue` 与 `Iterator()` 相关逻辑；当前断言既没有覆盖空集合 `CanProceed == false`，也没有证明每个元素只访问一次。若实现错误地重复访问同一槽位、跳过一个元素后重复另一个元素，或者在迭代期间把旧值残留重新返回，只要累计结果仍然碰巧为 `7`，测试就会继续通过。 |
| 影响 | `Bind_TSet.cpp` 696-723 行的 foreach/value surface 仍处于假覆盖状态，后续 `TSet` 遍历在元素去重、空容器起始状态、或单次访问语义上的回退不会被当前测试及时发现。 |
| 修复建议 | 把脚本样本改成 3 个非对称值，例如 `{2, 5, 11}`，在 foreach 中把访问到的值写入新的 `TSet<int> Visited` 并单独统计访问次数；断言 `Visited.Num() == 3`、`Visited.Contains(2/5/11)`、`VisitCount == 3`，同时补一段空 `TSet<int>` 的 foreach 不应执行循环体的断言，避免继续只用求和这种碰撞概率很高的聚合结果做判断。 |

#### Issue-79：`MapCompat` 完全跳过 missing-key 与引用返回语义，`Bind_TMap.cpp` 的关键错误路径没有被验证

| 项目 | 内容 |
|------|------|
| 问题类型 | WeakAssertion |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptContainerBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.MapCompat` |
| 行号范围 | 202-257 |
| 问题描述 | 当前脚本只验证了 `Find("Alpha")` 的成功路径，以及 `FindOrAdd("Alpha")` / `FindOrAdd("Beta", 11)` 的 happy path。它没有构造任何 missing-key 场景，因此测不到 `Bind_TMap.cpp:905-918` 中 `Find()` 在 `Index == INDEX_NONE` 时应直接返回 `false` 且保持 `OutValue` 不变的契约；同时也没有验证 `Bind_TMap.cpp:852-903` 返回的是 map 内部存储引用，而不是临时副本。若 `Find()` 失败时意外改写输出参数，或 `FindOrAdd()` 回退成返回临时值，当前测试都会持续绿灯。 |
| 影响 | `TMap` 最容易在 out 参数、默认构造插入和引用返回上出错，但 `MapCompat` 现有断言只覆盖“有这个 key 就能拿到值”。这会让 `Bind_TMap.cpp` 的失败路径和引用语义在回归时缺少第一时间的自动化信号。 |
| 修复建议 | 在现有用例里补两段 deterministic 断言：1. `int MissingValue = 99; bool bFound = Empty.Find(FName("Missing"), MissingValue);` 后断言 `!bFound && MissingValue == 99`；2. 使用 `int& AddedRef = Empty.FindOrAdd(FName("Gamma")); AddedRef = 33;` 或等价脚本写法，再通过 `Find("Gamma", Value)` 验证 map 中实际保存的是 `33`。这样可以同时锁住 `Find` 的失败契约和 `FindOrAdd` 的真实引用语义。 |

#### Issue-80：`ValueTypes` 把多个不相干 bind family 塞进单个 smoke test，失败诊断与覆盖口径都过于粗糙

| 项目 | 内容 |
|------|------|
| 问题类型 | AntiPattern |
| 测试文件 | `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptEngineBindingsTests.cpp` |
| 测试名 | `Angelscript.TestModule.Bindings.ValueTypes` |
| 行号范围 | 42-67 |
| 问题描述 | 单个 `Entry()` 同时覆盖 `FName`、`FVector`、`FRotator`、`FTransform`、`FText` 和基础数值表达式，只通过一个返回码 `Result == 1` 汇总。它实际只锁住了 `FVector + FVector::OneVector`、`FVector::RightVector.Rotation()`、`FTransform::TransformPosition()`、`FText::FromString()/ToString()` 这些极少数 happy path，却让人误以为 `Angelscript.TestModule.Bindings.ValueTypes` 已经代表了这一整片基础值类型绑定的健康度。按照项目规则，这种“一个用例覆盖多个不相干功能”的写法本身就是测试反模式。 |
| 影响 | 当 `Bind_FVector.cpp`、`Bind_FRotator.cpp`、`Bind_FTransform.cpp`、`Bind_FText.cpp` 其中任何一个 surface 回退时，失败信号只会表现成一个混合 smoke 变红，无法快速定位是哪一类值类型出错；反过来，只要这几个极少数 happy path 还活着，同文件内大量未触达的绑定也会继续被误判成“已测”。 |
| 修复建议 | 把 `ValueTypes` 拆成按 bind family 分组的 focused 用例，例如 `VectorValueCompat`、`RotatorCompat`、`TransformCompat`、`TextCompat`，每条测试只保留同一责任域内的 2-4 组 deterministic 断言，并尽量把脚本返回值与 C++ 原生基线做精确对齐。这样既能把单文件规模控制在规范内，也能避免用一个混合烟雾测试替代多组真正的绑定回归。 |

### 二、需要新增的测试
本轮新增测试建议已记录为 `NewTest-49`、`NewTest-50`、`NewTest-51`。由于工具追加时命中了旧章节，原文保留在本文件前部，本节末尾不再重复抄写细节。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-79 |
| AntiPattern | 1 | Issue-80 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | MissingErrorPath: 1 |
| P1 | 2 | MissingEdgeCase: 1, MissingScenario: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 全部 `.cpp` | 126 | 当前仓库实物数 |
| 其中 `Bind_*.cpp` | 123 | 用户输入“126 个 Bind_*.cpp”与当前仓库实物不符 |
| 非 `Bind_*.cpp` | 3 | `BlueprintCallableReflectiveFallback.cpp`、`UObjectInWorld.cpp`、`UObjectTickable.cpp` |
| 本轮人工复核后已见对应测试的 `Bind_*.cpp` | 41 / 123 | 完整名单已在本文件前部本轮覆盖清单中列出 |
| 本轮人工复核后完全无对应测试的 `Bind_*.cpp` | 82 / 123 | 完整名单已在本文件前部本轮覆盖清单中列出 |

---

## 测试审查 (2026-04-09 01:39) 末尾索引-实际EOF

### 一、定位说明

本轮新发现已登记为 `Issue-81`、`Issue-82`、`NewTest-52`、`NewTest-53`、`NewTest-54`、`NewTest-55`。

前文已存在对应正文条目；这里仅补实际 EOF 索引，避免重复抄写详细内容。

### 本轮汇总

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-81 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 3 | MissingScenario: 1, MissingErrorPath: 1, NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮再次核对，仍与任务描述中的 24 文件口径不一致 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 全部 `.cpp` | 126 | 当前目录实物统计 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 2 | `Bind_BlueprintType.cpp`、`Bind_FGuid.cpp` |
| 本轮新增识别为“完全无直测”的 bind 源码 | 2 | `Bind_UEnum.cpp`、`Bind_FQuat.cpp` |

---

## 测试审查 (2026-04-09 02:05) 末尾索引-EOF-MARK

本轮对应正文见前文的 `## 测试审查 (2026-04-09 02:05)`；本次实际 EOF 索引登记为 `Issue-83`、`Issue-84`、`Issue-85`、`NewTest-56`、`NewTest-57`。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-83 |
| FlakyRisk | 1 | Issue-84 |
| AntiPattern | 1 | Issue-85 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingScenario: 1 |
| P2 | 1 | MissingEdgeCase: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮再次全文复核，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 16 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中未见 token direct-hit | 84 / 123 | 这些 shard 在当前 `Bindings/` 目录内仍缺少任何直观命中入口 |

---

## 测试审查 (2026-04-09 03:09) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-09 03:09)`。其中真正新增项为 `Issue-90`、`NewTest-58`、`NewTest-59`、`NewTest-60`；`Issue-86`、`Issue-87`、`Issue-88`、`Issue-89` 与前文重复，不计入本轮统计。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-90 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 2 | MissingScenario: 2 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮重新全文复核，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 2 | `Bind_Console.cpp`、`Bind_TArray.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_FMemoryReader.cpp` |

---

## 测试审查 (2026-04-09 23:33) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-09 23:33)` 小节。真正新增项为 `Issue-91`、`Issue-92`、`NewTest-61`、`NewTest-62`；前文已保留正文，这里只补实际 EOF 汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-92 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 1 | MissingScenario: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮已逐文件全文复核完成，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 1 | `Bind_TSet.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_UCollisionProfile.cpp` |

---

## 测试审查 (2026-04-09 23:48) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-09 23:48)` 小节。真正新增项为 `Issue-93`、`NewTest-63`、`NewTest-64`、`NewTest-65`；前文已保留正文，这里只补实际 EOF 汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| AntiPattern | 1 | Issue-93 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 2 | NoTestForSource: 2 |
| P1 | 1 | MissingErrorPath: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮已再次逐文件全文复核完成，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 16 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 84 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_UDataTable.cpp`、`Bind_Subsystems.cpp` |

---

## 测试审查 (2026-04-09 23:59) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-09 23:59)` 小节。真正新增项为 `Issue-94`、`NewTest-66`、`NewTest-67`；由于文档前部存在重复锚点，本轮正文追加命中了旧位置，这里只补实际 EOF 汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-94 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |
| P1 | 1 | MissingScenario: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 16 | 本轮已再次逐文件全文复核完成，当前仓库实物数仍小于任务描述中的 24 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前目录实物统计；任务描述中的 “126 个 Bind_*.cpp” 与仓库实物不符 |
| 本轮新增识别为“已测但缺关键场景”的 bind 源码 | 1 | `Bind_FRandomStream.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_UGameInstance.cpp`、`Bind_ULocalPlayer.cpp` |

---

## 测试审查 (2026-04-10 00:17) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-10 00:17)` 小节。真正新增项为 `Issue-95`、`Issue-96`、`NewTest-68`、`NewTest-69`；前文已保留正文，这里只补实际 EOF 汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-96 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 2 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数；任务描述中的“24 个测试文件”与仓库不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数；任务描述中的“126 个 Bind_*.cpp”与仓库不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 17 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 84 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“已测但断言仍缺正确失败契约”的测试 | 2 | `ArrayMutationEdgeCases`、`ConsoleCommandSignatureCompat` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_APlayerController.cpp`、`Bind_UPrimitiveComponent.cpp` |

---

## 测试审查 (2026-04-10 00:28) 真正EOF索引-最终尾段

本轮正文已写入前部的 `## 测试审查 (2026-04-10 00:28)` 小节；对应新增项为 `Issue-97`、`NewTest-70`。这里补真正位于文件末尾的索引与汇总，供后续轮次从 EOF 接续。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-97 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P0 | 1 | NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 本轮已补齐对当前目录全部测试文件的全文复核；任务描述中的“24 个测试文件”与仓库实物不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物统计；任务描述中的“126 个 Bind_*.cpp”与仓库实物不符 |
| 本轮新增识别为“已测但缺 native parity 断言”的测试 | 1 | `BlueprintCallableReflectiveFallback.UMG` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_SystemTimers.cpp` |

---

## 测试审查 (2026-04-10 00:49) 真正EOF索引

本轮正文见前部的 `## 测试审查 (2026-04-10 00:49)` 小节。真正新增项为 `Issue-98`、`Issue-99`、`NewTest-71`、`NewTest-72`、`NewTest-73`；由于文档前部存在重复锚点，本轮正文追加命中了旧位置，这里补实际 EOF 汇总。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| BadIsolation | 2 | Issue-99 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | NoTestForSource: 1, MissingErrorPath: 1 |
| P2 | 1 | NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数；任务描述中的“24 个测试文件”与仓库不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数；任务描述中的“126 个 Bind_*.cpp”与仓库不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 17 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 84 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“shared engine 隔离错误”的测试文件 | 2 | `AngelscriptGameplayTagBindingsTests.cpp`、`AngelscriptMathAndPlatformBindingsTests.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_UUserWidget.cpp`、`Bind_UInputSettings.cpp` |
---

## 测试审查 (2026-04-10 00:58) 真正EOF索引-实际文件末尾

本轮正文见前部的 `## 测试审查 (2026-04-10 00:58)` 小节。真正新增项为 `Issue-100`、`Issue-101`、`NewTest-74`、`NewTest-75`、`NewTest-76`；这里是实际位于文件末尾的索引。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 2 | Issue-100 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | MissingEdgeCase: 1 |
| P2 | 2 | NoTestForSource: 2 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数，仍与任务描述中的“24 个测试文件”不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数，仍与任务描述中的“126 个 Bind_*.cpp”不符 |
| 本轮新增识别为“已测但缺精确语义/空值断言”的测试 | 2 | `UtilityCompat`、`ObjectPtrCompat` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 2 | `Bind_FMargin.cpp`、`Bind_FAnchors.cpp` |

---

## 测试审查 (2026-04-10 01:12) 真正EOF索引-实际文件末尾

本轮正文已写入前部的 `## 测试审查 (2026-04-10 01:12)` 小节；对应新增项为 `Issue-102`、`NewTest-77`、`NewTest-78`。这里补真正位于文件末尾的索引与汇总，供后续轮次从 EOF 接续。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-102 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 2 | MissingErrorPath: 1, NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 17 | 当前仓库实物数，仍与任务描述中的“24 个测试文件”不符 |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数，仍与任务描述中的“126 个 Bind_*.cpp”不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 39 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 17 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 84 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“已测但缺对象身份断言”的测试 | 1 | `ObjectCastCompat` |
| 本轮新增识别为“已测但缺错误路径”的 bind 源码 | 1 | `Bind_Console.cpp` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 1 | `Bind_Debugging.cpp` |
---

## 测试审查 (2026-04-10 01:25) 真正EOF索引-实际文件末尾

本轮正文已写入前部的 `## 测试审查 (2026-04-10 01:25)` 小节；对应新增项为 `Issue-103`、`NewTest-79`。这里补真正位于文件末尾的索引与汇总，供后续轮次从 EOF 接续。

**现有测试问题统计**

| 问题类型 | 数量 | 最严重的 |
|----------|------|---------|
| WeakAssertion | 1 | Issue-103 |

**新增测试建议统计**

| 优先级 | 数量 | 新增原因分布 |
|--------|------|-------------|
| P1 | 1 | NoTestForSource: 1 |

**覆盖快照**

| 项目 | 数量 | 说明 |
|------|------|------|
| `Plugins/Angelscript/Source/AngelscriptTest/Bindings/` 实际测试 `.cpp` | 18 | 当前仓库实物数，仍与任务描述中的“24 个测试文件”不符 |
| 其中非 `Angelscript.TestModule.Bindings.*` 命名的测试文件 | 1 | `AngelscriptActorFunctionLibraryTests.cpp` 实际承载 `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` `Bind_*.cpp` | 123 | 当前仓库实物数，仍与任务描述中的“126 个 Bind_*.cpp”不符 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token direct-hit | 41 / 123 | 以去掉 `Bind_` 前缀后的文件名 token 对 18 个测试文件全文做字符串命中统计 |
| `Bind_*.cpp` 在 `Bindings/` 测试中的 token no-hit | 82 / 123 | 当前 `Bindings/` 目录内仍缺少任何直观命中入口的 bind shard 数量 |
| 本轮新增识别为“目录内已有测试但断言未锁住原生旋转语义”的测试 | 1 | `Angelscript.TestModule.FunctionLibraries.ActorTransformRoundTrip` |
| 本轮新增识别为“完全无直测入口”的 bind 源码 | 4 | `Bind_FIntPoint.cpp`、`Bind_FIntVector.cpp`、`Bind_FIntVector2.cpp`、`Bind_FIntVector4.cpp` |
