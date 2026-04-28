# UHTTool 分析

---

## 分析 (2026-04-08 02:30)

### 发现 1：skipped 统计没有复用实际生成过滤条件，诊断结果会把“本来就不该生成”的函数误报为失败

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` |
| 行号 | 27-52, 64-95 |
| 描述 | `Export()` 先调用 `AngelscriptFunctionTableCodeGenerator.Generate(factory)` 生成实际输出，但后续统计 `functionCount/reconstructedCount/skippedCount` 时走的是 `CountBlueprintCallableFunctions()`，它只检查 `IsBlueprintCallable()`，没有复用 `ShouldGenerate()` 的 `NotInAngelscript`、`BlueprintInternalUseOnly`、`UsableInAngelscript`、`CustomThunk`、特例黑名单和 `Private` header 过滤。结果是 `AS_FunctionTable_SkippedEntries.csv` 与控制台里的 skipped 数会混入“设计上就应跳过”的函数。 |
| 根因 | 生成链路和诊断链路分叉：实际代码生成在 `AngelscriptFunctionTableCodeGenerator.ShouldGenerate()` 中集中定义过滤条件，但 exporter 统计逻辑单独递归扫描 UHT 树，没有共享这一判定。 |
| 影响 | 当 direct/stub/skipped 覆盖率下降时，开发者无法从 skipped 清单区分“生成器失败”与“策略性排除”，会错误追查并污染回归基线，降低错误诊断质量。 |

证据补充：

- `ShouldGenerate()` 明确跳过 `NotInAngelscript`、`BlueprintInternalUseOnly` 且无 `UsableInAngelscript`、`CustomThunk`、`UUniversalObjectLocatorScriptingExtensions` 特例和 `Private` header：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:490-530`。
- 运行时跳过策略也使用同一套元数据语义：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:83-107`。
- 测试头 `UAngelscriptUhtCoverageTestLibrary` 同时声明了 `BlueprintInternalUseOnly + UsableInAngelscript` 与纯 `BlueprintInternalUseOnly` 两种函数，说明仓库里存在需要区分“允许导出”和“应跳过”的真实样例：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h:25-29`。

### 发现 2：UHTTool 能解析 overloaded 签名，但最终生成表仍以 `FunctionName` 单键落地，后续 overload 会被静默丢弃

| 项目 | 内容 |
|------|------|
| 维度 | A / B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | 14-22, 81-139, 449-487 |
| 描述 | 代码生成器把每条绑定记录建模为 `ClassName + FunctionName + EraseMacro`，`BuildRegistrationLine()` 生成的注册调用也只传 `FunctionName` 字符串。与此同时，签名构建器和 header resolver 明确实现了 overload 分辨逻辑；但这些签名信息在落表阶段被全部抛弃。 |
| 根因 | UHTTool 的“解析层”按完整签名思考，`AngelscriptGeneratedFunctionEntry` 和 `FAngelscriptBinds::AddFunctionEntry()` 的“存储层”却只按 `Class + Name` 建模，没有为 overloaded BlueprintCallable 建立多值容器或签名键。 |
| 影响 | 一旦同一 `UClass` 上存在多个同名 BlueprintCallable/Pure，生成的多个 `AddFunctionEntry(Class, "Name", ...)` 调用只有第一条会进入 `ClassFuncMaps`，其余 overload 会被静默吞掉，导致代码生成结果与前面的 overload 解析工作不一致。 |

证据补充：

- `AngelscriptHeaderSignatureResolver.TryBuild()` 在 `exactMatches` 中显式按参数/返回值匹配 overload，并用 `"overloaded-unresolved"` 作为失败原因，说明工具设计上明确要处理同名函数：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:70-106`。
- `AngelscriptGeneratedFunctionEntry.BuildRegistrationLine()` 只输出 `FAngelscriptBinds::AddFunctionEntry({Class}::StaticClass(), "{FunctionName}", { ... })`，没有任何签名信息：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14-22`。
- 运行时 `FAngelscriptBinds::AddFunctionEntry()` 对同一个 `Name` 只在 `!Contains(Name)` 时插入，重复键直接忽略，没有日志或断言：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:497-512`。

### 发现 3：header 解析器丢弃了细粒度失败原因，skipped CSV 最终只剩粗粒度 `"overloaded-unresolved"`

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs` |
| 行号 | 75-106, 465-506 |
| 描述 | `TryParseDeclaration()` 已经能精确区分 `function-name`、`closing-paren`、`return-type`、`parameter-count` 等错误，但 `TryBuild()` 在遍历 `publicCandidates` 时把这些失败原因全部用 `out _` 丢掉，只要没有唯一 `exactMatches`，就统一回报 `"overloaded-unresolved"` 或 `"unexported-symbol"`。 |
| 根因 | 解析层为了做 overload 匹配，只保留“是否匹配成功”的布尔结果，没有把每个 candidate 的具体失败上下文向上聚合到 exporter 的 skipped 诊断。 |
| 影响 | `AS_FunctionTable_SkippedEntries.csv` 无法区分“参数解析失败”“返回值解析失败”“名字匹配失败”和“真正的 overload 冲突”，开发者排查时必须重新手工读 header 才知道根因，错误诊断质量明显不足。 |

证据补充：

- `TryBuild()` 在调用 `TryParseDeclaration()` 时直接写成 `out _`，随后把所有未命中的情况折叠到 `failureReason = matchedUnexportedSymbol ? "unexported-symbol" : "overloaded-unresolved"`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:77-106`。
- `TryParseDeclaration()` 内部实际上已经产出更细粒度的失败标签：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:469-499`。
- 当前自动化测试只验证 skipped CSV 的 `FailureReason` 非空，不校验分类质量，也不覆盖这些细粒度原因是否能保留下来：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:678-697`。

### 发现 4：除少数特判外，header 文本解析失败会直接降级为显式签名直绑，安全边界依赖一个脆弱的源码字符串解析器

| 项目 | 内容 |
|------|------|
| 维度 | A / B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs` |
| 行号 | 43-100 |
| 描述 | `AngelscriptFunctionSignatureBuilder.TryBuild()` 只在 `failureReason` 为 `non-public`、`unexported-symbol` 或非白名单 `overloaded-unresolved` 时终止；对 `header-missing`、`class-range`、`declaration-missing` 等 header 解析失败会继续使用 UHT 元数据拼出 `ERASE_METHOD_PTR/ERASE_FUNCTION_PTR`。这意味着“是否可以安全直绑”的判定，被建立在一个依赖文件路径、class body 文本搜索和手写括号匹配的解析器是否碰巧成功之上。 |
| 根因 | 工具把 header resolver 同时承担了两个职责：一是尽量恢复精确签名，二是判断访问级别/导出可见性；但 fallback 策略没有把这两个职责拆开，导致文本解析失败时仍继续走 direct bind。 |
| 影响 | 一旦 UE5.x header 排版、宏包裹方式或 `HeaderFile.FilePath` 行为变化导致 resolver 误报 `class-range` / `declaration-missing`，生成器仍可能产出 direct-call erase macro，直到 C++ 编译或链接阶段才暴露问题，错误定位被明显后移。 |

证据补充：

- header resolver 的失败码包括 `header-missing`、`class-range`、`declaration-missing`，它确实依赖磁盘 header 和文本级 class body 扫描：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:22-45, 180-250, 253-293`。
- signature builder 对这些失败码没有单独拦截，而是继续构造显式签名并返回成功：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:47-100`。
- 当前测试只覆盖了正向的 `MinimalAPI` 函数级导出恢复，没有覆盖 `header-missing` / `class-range` / `declaration-missing` 这些负向路径：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:316-353`。

### 发现 5：增量输出管理只覆盖 `AS_FunctionTable_*.cpp`，JSON/CSV 诊断产物完全绕过了 UHT 的 output/stale 机制

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` |
| 行号 | 21-27, 43, 98-137 |
| 描述 | exporter 在 `[UhtExporter]` 上只把 `AS_FunctionTable_*.cpp` 声明为 `CppFilters`，代码生成器也只对 shard `.cpp` 调用 `factory.CommitOutput()` 和 `DeleteStaleOutputs()`。与此同时，`AS_FunctionTable_Summary.json`、`AS_FunctionTable_ModuleSummary.csv`、`AS_FunctionTable_Entries.csv`、`AS_FunctionTable_SkippedEntries.csv` 全部通过 `File.WriteAllText()` 直接写盘，没有进入 UHT 的输出跟踪。 |
| 根因 | 代码把“可编译产物”和“诊断产物”拆成了两套写盘路径，但 stale 清理和增量管理只为前者实现了一套机制。 |
| 影响 | 诊断侧文件无法享受 `CommitOutput()` 的内容比较和输出生命周期管理；当导出器行为变化、文件改名或某轮未生成这些 sidecar 时，磁盘上可能保留旧诊断结果，进而误导人工排查或自动化测试。 |

证据补充：

- exporter 只把 `AS_FunctionTable_*.cpp` 注册为 compile output：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:21-27`。
- `.cpp` shard 通过 `factory.CommitOutput()` 生成，并在 `DeleteStaleOutputs()` 中按 `AS_FunctionTable_*.cpp` 清理旧文件：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:120-123, 432-446`。
- summary/csv 则全部走 `File.WriteAllText()` 直接落盘：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:174-205, 218-265` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:98-137`。
- 自动化测试确实把这些 sidecar 当成正式诊断入口读取，但没有 freshness 校验，只验证文件存在和格式/计数关系：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:454-697`。

### 发现 6：支持模块集合依赖 `Build.cs` 路径猜测和文本解析，UE5.x API/脚本风格一变就会改写生成范围

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | 334-409 |
| 描述 | `LoadSupportedModules()` 不是从 UBT/UHT 已解析的依赖模型拿模块集合，而是先通过第一个 header 路径中的 `"/Source/AngelscriptRuntime/"` 子串反推出 `AngelscriptRuntime.Build.cs`，再逐行扫描 `DependencyModuleNames.AddRange` 和 `if (Target.bBuildEditor)` 文本，把引号字符串收集为支持模块。 |
| 根因 | 工具为了绕过缺失的高层 API，直接把模块发现建立在文件路径约定和 `Build.cs` 源码格式之上，而不是建立稳定的数据抽象。 |
| 影响 | 一旦 `Build.cs` 改成 `Add()`/辅助函数/不同缩进块，或 UHT 的 `HeaderFile.FilePath` 不再包含当前硬编码子串，UHTTool 就会漏掉模块、错误标记 editor-only，甚至直接抛出 `Unable to locate AngelscriptRuntime.Build.cs`；最终表现为函数表生成范围突变，而不是显式的兼容层告警。 |

证据补充：

- `ResolveRuntimeBuildCsPath()` 通过 header 路径里是否含有 `"/Source/AngelscriptRuntime/"` 来定位 `AngelscriptRuntime.Build.cs`，找不到就直接 `throw`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:387-409`。
- `LoadSupportedModules()` 对 `Build.cs` 的理解只覆盖 `DependencyModuleNames.AddRange`、`if (Target.bBuildEditor)` 和单独一层 `}` 结束 editor block：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:345-382`。
- 当前 `AngelscriptRuntime.Build.cs` 恰好使用了这种 `AddRange` + 简单 editor block 结构，所以现在能跑通，但这是对脚本书写风格的偶然匹配，不是稳固契约：`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:29-79`。
- 自动化测试只验证 `Engine` 与 `UMGEditor` 等代表性产物存在，没有覆盖“模块发现逻辑是否仍与 Build.cs 同步”这一边界：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:237-261, 264-314`。

---

## 分析 (2026-04-08 02:38)

### 发现 7：shard 按条目拆分但不按依赖拆分，任一模块头文件变更都会迫使该模块全部 shard 重编译

| 项目 | 内容 |
|------|------|
| 维度 | B / E |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | 81-137, 282-331, 449-487 |
| 描述 | `GenerateModule()` 先把整个模块的 header include 收集进单个 `SortedSet<string> includes`，随后每个 shard 调用 `BuildShard()` 时都原样写入这整套 include，而不是只写当前 shard 实际使用到的类头。这样虽然条目注册被切成多个 `.cpp`，但依赖图没有被切开。 |
| 根因 | shard 策略只对 `entries` 做 `MaxEntriesPerShard` 分块，没有同步维护“entry 到 include”的归属关系；`BuildShard()` 只能消费模块级 include 集合。 |
| 影响 | 在增量构建里，只要模块内任意一个参与生成的 header 发生变化，该模块所有 `AS_FunctionTable_<Module>_*.cpp` 都会因为共享 include 集而重新编译，分片对重编译范围的缩减效果基本被抵消。模块越大，问题越明显。 |

证据补充：

- `GenerateModule()` 把 `includes` 定义为模块级集合，`CollectEntries()` 遍历时不断向其中加入 header，之后每个 shard 都复用同一 `includes`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:83-86, 116-121, 449-487`。
- `BuildShard()` 没有按 `startIndex/entryCount` 过滤 include，直接遍历整个 `includes` 集输出 `#include`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:296-299`。
- 当前生成产物已经体现这一点：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_000.cpp` 与 `.../AS_FunctionTable_Engine_001.cpp` 都包含 218 条 include，且 include 列表完全一致；`Engine` 模块总共生成了 16 个 shard。

### 发现 8：skipped 诊断遍历整个 UHT session，连根本不在生成功能范围内的模块也被统计为失败

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` |
| 行号 | 35-43, 64-95 |
| 描述 | 实际代码生成会先经 `LoadSupportedModules()` 过滤，只为 `AngelscriptRuntime.Build.cs` 依赖集合里的模块生成 `AS_FunctionTable_<Module>_*.cpp`。但 exporter 的 skipped 统计没有复用这层过滤，而是直接遍历 `factory.Session.Modules` 全量模块树，因此会把不在生成功能范围内的 BlueprintCallable 也记入 `AS_FunctionTable_SkippedEntries.csv`。 |
| 根因 | 生成链路掌握“支持模块集合”，诊断链路完全不知道这个边界，导致二者对“应该尝试生成哪些模块”的定义不一致。 |
| 影响 | 当 `SkippedEntries.csv` 出现失败项时，开发者会被引向根本不会产出绑定文件的模块，误以为这些模块属于 UHTTool 的回归范围；诊断总量和失败分布因此失真。 |

证据补充：

- 生成链路只处理 `supportedModules.All` 中的模块：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:53-72, 334-384`。
- skipped 统计则无条件遍历 `factory.Session.Modules` 全量模块：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:35-43`。
- 当前 `AngelscriptRuntime.Build.cs` 的直接依赖集合只覆盖 `ApplicationCore`、`Core`、`CoreUObject`、`Engine`、`AIModule`、`UMG`、`GameplayAbilities`、`UnrealEd`、`UMGEditor` 等模块，没有 `GameplayCameras`、`MetasoundEngine`、`PythonScriptPlugin`、`Niagara`、`ControlRigEditor`：`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:30-78`。
- 但当前生成产物 `AS_FunctionTable_SkippedEntries.csv` 已实际包含这些模块的 skipped 记录，例如 `GameplayCameras` 93 条、`MetasoundEngine` 73 条、`PythonScriptPlugin` 58 条、`Niagara` 53 条、`ControlRigEditor` 52 条。

### 发现 9：summary/csv 把所有 `ERASE_NO_FUNCTION()` 都记成 `Stub`，无法区分“可反射兜底”和“彻底不可调用”

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | 100-139, 166-265, 465-477 |
| 描述 | UHTTool 在生成阶段只根据 `eraseMacro == "ERASE_NO_FUNCTION()"` 把条目标记成 `Stub`，并将 `DirectBindEntries/StubEntries` 写入 json/csv。但运行时真正消费这些 entry 时，会把一部分“无直绑指针”的条目继续走 `BindBlueprintCallableReflectiveFallback()`，最终把 `bReflectiveFallbackBound` 置为 `true`。生成侧诊断因此把“仍然可调用的反射兜底”与“最终 unresolved 的死 stub”混成一类。 |
| 根因 | 代码生成器只观察静态产物中的 erase macro，不感知运行时后续绑定阶段的 reflective fallback 结果，也没有为这种第三状态预留字段。 |
| 影响 | `AS_FunctionTable_Summary.json`、`AS_FunctionTable_ModuleSummary.csv`、`AS_FunctionTable_Entries.csv` 无法回答“哪些 stub 实际还能调用”，导致覆盖率回归只能靠运行时人工或额外测试发现；单看 UHT 诊断产物会系统性低估真实 callable coverage，并掩盖 fallback 退化。 |

证据补充：

- 生成器把 `ERASE_NO_FUNCTION()` 统一记为 `Stub`，summary/json/csv 中没有 reflective fallback 维度：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:103-113, 128-135, 166-265, 465-477`。
- `FFuncEntry` 明确保留了运行时态的 `bReflectiveFallbackBound` 标志，说明 `Stub` 之后还会继续分化：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h:384-389`。
- 运行时 `BindBlueprintCallable()` 在直绑指针为空时会调用 `BindBlueprintCallableReflectiveFallback()`，成功后把 `Entry.bReflectiveFallbackBound` 置为 `true`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:72-90` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:374-420`。
- 现有自动化测试已经承认这种第三状态存在，它单独统计 `ReflectiveCount` 和 `UnresolvedCount`，而不是把它们都视为同一种 stub：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:355-452`。

### 发现 10：summary/csv 统计的是“生成尝试”，不是最终 `ClassFuncMaps` 的生效结果，手写覆盖场景会被误报为 stub

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | 100-139, 166-265 |
| 描述 | UHTTool 的 summary/json/csv 全部基于生成时的 `entries` 列表计数，默认认为每一条 `AddFunctionEntry()` 都会生效。但运行时 `FAngelscriptBinds::AddFunctionEntry()` 对同一 `Class + Name` 的重复键会直接忽略，因此“生成文件里的 stub”并不一定代表运行时最终状态。`UAngelscriptAbilityAsyncLibrary::WaitForAttributeChanged` 就是现成样本：生成文件写出 `ERASE_NO_FUNCTION()`，但实际生效的是更早注册的手写 direct entry。 |
| 根因 | 代码生成侧把 emitted line 当成 ground truth，没有结合运行时的去重规则，也没有为“被手写条目覆盖/遮蔽”的生成项单独建模。 |
| 影响 | 分析人员查看 `AS_FunctionTable_Summary.json` 或 `AS_FunctionTable_Entries.csv` 时，会把某些函数误判为 stub/unresolved，实际上运行时仍走 direct bind；这会污染回归判断，也让 summary 与真实 `ClassFuncMaps` 状态脱节。 |

证据补充：

- summary/csv 的 direct/stub 统计全部来自 `entries` 列表和 `EraseMacro`，没有读取运行时去重结果：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:100-139, 166-265`。
- 运行时 `AddFunctionEntry()` 对重复键静默忽略，后注册项不会覆盖先注册项：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:497-512`。
- 手写 GAS 绑定在 `Bind_AngelscriptGASLibrary.cpp` 中已提前为 `WaitForAttributeChanged` 注册 direct pointer：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp:4-14`。
- 当前生成产物同时又在 `AS_FunctionTable_AngelscriptRuntime_000.cpp` 中为同名函数写出了 `ERASE_NO_FUNCTION()`，并在 `AS_FunctionTable_Entries.csv` 中把它记成 `Stub`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_AngelscriptRuntime_000.cpp:53-57`。
- 自动化测试只验证“手写条目在运行时仍为 direct bind”，没有校验 summary/csv 与该最终状态一致：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:194-235, 355-452`。

---

## 分析 (2026-04-08 02:50)

### 发现 11：header resolver 把函数名后必须紧跟 `(` 当成硬约束，合法的 `FunctionName (` 声明会被误判为 `declaration-missing`

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs` |
| 行号 | 35-40, 367-395, 469-473 |
| 描述 | `FindCandidates()` 只搜索 `functionName + "("`，`TryParseDeclaration()` 也只接受 `function.SourceName + "("`。UE 现有头文件已经存在函数名和左括号之间插入空格的合法声明风格，这会让 resolver 直接看不到候选声明，把本来可解析的函数降级成 `declaration-missing`。 |
| 根因 | 文本解析器把一种代码格式约定编码成了语法规则，没有在函数名和 `(` 之间容忍空白字符。 |
| 影响 | 当前仓库里这类函数仍会被 `AngelscriptFunctionSignatureBuilder` 的 UHT 元数据 fallback 生成为 direct bind，因此问题不会显式暴露在 skipped CSV；但 header-based 解析、导出可见性检查和失败诊断会被静默绕过，UE5.x 头文件格式稍有变化就会扩大这条盲区。 |

证据补充：

- `FindCandidates()` 的 marker 被写死为 `functionName + "("`，`TryParseDeclaration()` 也同样只接受 `function.SourceName + "("`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:367-371, 469-473`。
- 支持模块 `UMG` 的真实头文件已经包含这种空格风格：`Runtime/UMG/Public/Components/ListView.h` 中 `SetScrollIntoViewAlignment` 声明为 `UMG_API void SetScrollIntoViewAlignment (EScrollIntoViewAlignment NewScrollIntoViewAlignment);`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Components/ListView.h:121-123`。
- 该函数最终仍被生成为 direct entry，说明当前是“resolver 失效但 fallback 掩盖”的状态，而不是测试能察觉的显式失败：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:4510`。
- 自动化测试只校验 `RunBehaviorTree`、`ReportPerceptionEvent` 等样本，没有覆盖“函数名与 `(` 之间存在空格”这一已存在于 UE 头文件中的格式边界：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:701-725`。

### 发现 12：访问级别判定没有展开 `GENERATED_UCLASS_BODY()`，大量实际 public 的 Blueprint API 被误判为 `non-public` 并降级成 stub

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs` |
| 行号 | 42-46, 362-395, 438-463 |
| 描述 | resolver 在原始头文件文本里用 `FindAccessSpecifier()` 追踪 `public:` / `protected:` / `private:`，默认访问级别是 `private`。对于使用 `GENERATED_UCLASS_BODY()` 的 UCLASS，源码正文里经常没有显式 `public:`，真正把后续 UFUNCTION 变成 public 的是 `.generated.h` 展开的宏代码。由于 resolver 完全不展开这些宏，像 `UAIBlueprintHelperLibrary` 这样的标准 BlueprintFunctionLibrary 会被整批误判为 `non-public`。 |
| 根因 | 访问级别分析建立在未展开的 header 文本上，但 UE 的类可见性经常由 `GENERATED_*` 宏在生成头里注入；当前实现既不读取 `.generated.h`，也不对这些宏做任何语义建模。 |
| 影响 | 这不是单纯的 skipped 统计误差。`AngelscriptFunctionSignatureBuilder` 会在 `non-public` 时直接返回失败，`CollectEntries()` 随后把这些函数写成 `ERASE_NO_FUNCTION()`，导致本来可直绑的 public Blueprint API 被系统性降级成 stub，直接损伤代码生成正确性和 callable coverage。 |

证据补充：

- resolver 在 `publicCandidates.Count == 0` 时直接回报 `non-public`；访问级别完全来自 `FindAccessSpecifier()` 对裸文本 `public:` / `protected:` / `private:` 的线性扫描：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:42-46, 390-391, 438-463`。
- `UAIBlueprintHelperLibrary` 的源码里确实没有显式 `public:`，但多个 BlueprintCallable/Pure 函数位于 `private:` 之前，当前 skipped 产物把它们记成了 `non-public`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/Blueprint/AIBlueprintHelperLibrary.h:25-32, 49-53, 91-99` 与 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:7-19`。
- 同一个类的 `.generated.h` 明确把 `GENERATED_UCLASS_BODY()` 展开为带 `public:` 的代码块，说明这些 API 在真实 C++ 语义下并非私有：`../../UnrealEngine/UERelease/Engine/Intermediate/Build/Win64/UnrealEditor/Inc/AIModule/UHT/AIBlueprintHelperLibrary.generated.h:53-84`。
- 误判已经进入最终产物：`UAIBlueprintHelperLibrary::GetAIController` 和 `SimpleMoveToActor` 在 `AS_FunctionTable_Entries.csv` 中被记成 `Stub,ERASE_NO_FUNCTION()`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:4106, 4117`。
- 现有自动化测试没有任何针对 `GENERATED_UCLASS_BODY()` / legacy generated body 访问级别恢复的回归样本：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:19-725`。

### 发现 13：候选扫描没有跳过 inline 函数体，函数调用表达式会被误当成声明并污染 overload/export 诊断

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs` |
| 行号 | 35-40, 362-395, 401-435 |
| 描述 | `FindCandidates()` 在整个 class body 上裸搜 `functionName + "("`，不区分这是声明位置还是 inline 函数体里的调用表达式；随后 `FindDeclarationStart()` 把最近的 `{` 当成声明起点，`FindDeclarationEnd()` 把最近的 `;` 当成声明终点。结果是像 `return GetAIPerceptionComponent();` 这样的调用语句，会被抽成一个伪“声明候选”。 |
| 根因 | 文本扫描器没有维护“当前是否位于函数体/语句块内部”的状态，导致标识符搜索命中了 class body 内部的实现代码；而声明起止判定又把 `{` / `;` 视为通用边界，进一步把语句片段包装成了合法候选。 |
| 影响 | 伪候选会抬高 `candidates/publicCandidates` 数量，并把 `matchedUnexportedSymbol`、`overloaded-unresolved` 等诊断引向错误分支。当前 `AAIController::GetAIPerceptionComponent` 已因此被判成 `unexported-symbol` 并落成 stub，直接影响生成结果而不只是诊断文案。 |

证据补充：

- `FindCandidates()` 只基于 `IndexOf(functionName + "(")` 命中位置截取候选；`FindDeclarationStart()`/`FindDeclarationEnd()` 分别把最近的 `{` / `;` 当边界，没有任何“跳过函数体”的逻辑：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:362-395, 401-435`。
- `AAIController` 在同一个 class body 中先定义了 inline wrapper `GetPerceptionComponent() override { return GetAIPerceptionComponent(); }`，后面才声明真正的 BlueprintPure `GetAIPerceptionComponent()`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/AIController.h:389-393, 438-443`。
- 按当前解析算法抽取得到的第一个“候选”实际就是语句片段 `return GetAIPerceptionComponent()`；后两个才是真实声明。这是我基于当前 `AngelscriptHeaderSignatureResolver` 逻辑对 `AIController.h` 做同构脚本复现得到的结果，说明问题来自现有实现而非推测。
- 错误已经落到产物里：`AAIController::GetAIPerceptionComponent` 在 skipped CSV 中被标成 `unexported-symbol`，并在 entry CSV / 生成 `.cpp` 中写成 `ERASE_NO_FUNCTION()`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:4`、`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:4084`、`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_AIModule_000.cpp:36`。
- 现有测试只验证少量代表性 direct-bind 样本，没有覆盖“inline wrapper 体内存在同名调用表达式”的解析边界：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:264-725`。

### 发现 14：overload 精确匹配完全忽略成员函数 `const` 性，并把返回类型里的 `const` 也擦掉，`const`/非 `const` 重载无法被唯一区分

| 项目 | 内容 |
|------|------|
| 维度 | A / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs` |
| 行号 | 70-106, 171-178, 465-506 |
| 描述 | `TryBuild()` 判定 `exactMatches` 时只比较参数个数、`AreTypesEquivalent()` 和 `NormalizeTypeText(expectedReturnType)`；成员函数本身的 `const` 性完全不参与匹配。与此同时，`NormalizeTypeText()` 会把所有 `const ` 文本都删掉。这样一来，只要同名重载差异落在“方法是 `const` 还是非 `const`”或“返回 `const T*` vs `T*`”上，resolver 就没有能力唯一锁定真正的 UFUNCTION。 |
| 根因 | 解析器虽然在 `AngelscriptFunctionSignature` 中保留了 `IsConst` 字段，但 overload 匹配阶段没有使用这个字段；同时返回类型归一化过度激进，把有语义差异的 `const` 也抹平了。 |
| 影响 | 这会把原本可判定的同名重载推入 `overloaded-unresolved`/错误分支，或者与其他候选叠加后转成 stub。`AAIController::GetAIPerceptionComponent` 正是现成样本：BlueprintPure 版本和旁边的 `const` helper 版本在当前匹配规则下无法靠签名区分。 |

证据补充：

- `TryBuild()` 的 `exactMatches` 逻辑只看参数类型和归一化后的返回类型，没有比较 `parsedSignature.IsConst` 或声明侧 `const`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:75-95`。
- `NormalizeTypeText()` 会无条件删除 `const `、`&` 和全部空白：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:171-178`。
- `TryParseDeclaration()` 给 `signature.IsConst` 赋值时也不是读 declaration 后缀，而是直接复用当前 UHT function 的 `FunctionFlags`，因此不同候选声明不会拥有各自独立的 `const` 信息：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:485-504`。
- `AAIController` 里真实存在这一组重载：UFUNCTION 的 `UAIPerceptionComponent* GetAIPerceptionComponent()` 与紧随其后的 `const UAIPerceptionComponent* GetAIPerceptionComponent() const`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/AIController.h:438-443`。
- 当前产物中该函数确实没有恢复成 direct bind，而是被写成 stub：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:4084`。

### 发现 15：UHT flags 判定依赖 `enum.ToString().Contains(...)`，与 UE5.x UHT 的强类型按位 API 脱节

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs` |
| 行号 | 63-64, 485-502 |
| 描述 | UHTTool 在多个关键分支上不是按位读取 `FunctionFlags` / `FunctionExportFlags`，而是把 enum 转成字符串后做 `Contains("Static")`、`Contains("Const")`、`Contains("BlueprintCallable")`、`Contains("CustomThunk")` 之类的文本匹配。这个实现把工具行为绑定到了 UE5.x UHT 当前的枚举字符串化格式，而不是枚举本身的稳定语义。 |
| 根因 | 代码没有复用 EpicGames.UHT 已提供的 `HasAnyFlags()` / `HasAllFlags()` 扩展方法和强类型字段，直接把调试/序列化友好的 `ToString()` 输出拿来当逻辑分支输入。 |
| 影响 | 只要 UHT 升级后调整 flag 命名、组合字符串格式或新增包含相同子串的新枚举值，UHTTool 就会在没有编译错误的前提下静默改变生成范围和签名判断；这正是与 UE5.x UHT API 的高风险适配边界。 |

证据补充：

- 当前工具至少 6 处直接使用 `ToString().Contains(...)` 判定 UHT flags：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:63-64, 485-502`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:130-133`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:514`、`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:55-62`。
- UE 自己的 UHT API 明确把这些 flags 暴露为强类型字段，并提供 `UhtFunctionExportFlagsExtensions.HasAnyFlags/HasAllFlags` 等按位辅助方法，说明官方契约并不是“解析 `ToString()` 文本”：`../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtFunction.cs:20-21, 157-196, 253-266`。
- 在同一份 `UhtFunction.cs` 里，Epic 自己校验 `EFunctionFlags.Static`、`EFunctionFlags.Const`、`UhtFunctionExportFlags.CustomThunk` 时都使用按位 API，而不是字符串匹配：`../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtFunction.cs:904-910, 1093-1137, 1347-1350`。
- 当前自动化测试全部围绕生成产物结果，没有任何测试故意约束“flag 读取方式必须兼容 UHT API 变动”，因此这类漂移只能在升级 UE 后以产物变化的形式被动暴露：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:19-725`。

---

## 分析 (2026-04-08 03:18)

### 发现 16：candidate 扫描把非 `UFUNCTION` 的同名 helper overload 一并纳入匹配，合法的 Blueprint overload 会被误降级成 `overloaded-unresolved`

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs` |
| 行号 | 35-42, 77-105, 362-395 |
| 描述 | `FindCandidates()` 只按 `functionName + "("` 在整个 class body 里抓同名声明，不要求候选附近存在对应的 `UFUNCTION` 宏，也不与 UHT 已经解析出来的 reflected declaration 建立一一对应关系。结果是当一个 BlueprintCallable overload 旁边还有未反射的 plain C++ helper overload 时，resolver 仍会把它们一起送进 overload 判定，当前产物里 `UBlackboardComponent::ClearValue`、`UBlackboardComponent::GetLocationFromEntry` 和 `UBehaviorTreeComponent::SetDynamicSubtree` 都因此落成 `ERASE_NO_FUNCTION()`。 |
| 根因 | 候选收集层只使用“同名文本命中”定义候选集合，没有把 `UFUNCTION` 标记位置、UHT symbol identity 或 reflected declaration 范围纳入过滤条件。 |
| 影响 | 一批本来已经由 UHT 明确标记为 BlueprintCallable 的 engine API 会被错误归类为 overload 失败，直接损伤函数表生成正确性，并把诊断噪声引入 `SkippedEntries.csv`。 |

证据补充：

- `FindCandidates()` 在 class body 中裸搜 `functionName + "("`，随后 `TryBuild()` 对全部 `publicCandidates` 做统一匹配，没有任何“只保留 reflected declaration”的步骤：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:35-42, 77-105, 362-395`。
- `UBlackboardComponent` 的真实头文件里，只有第一条 overload 带 `UFUNCTION(BlueprintCallable)`，后面的同名 `FBlackboard::FKey` helper overload 没有 `UFUNCTION`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/BehaviorTree/BlackboardComponent.h:168-179`。
- `UBehaviorTreeComponent` 也存在同样结构：两条 `SetDynamicSubtree` 里只有双参数版本带 `UFUNCTION(BlueprintCallable)`，三参数 helper overload 只是 plain C++ declaration：`../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/BehaviorTree/BehaviorTreeComponent.h:275-280`。
- 当前产物已经把这些函数落成 overload 失败并写成 stub：`AS_FunctionTable_SkippedEntries.csv` 中 `SetDynamicSubtree` 与 `ClearValue` 分别位于第 35、36 行；`AS_FunctionTable_Entries.csv` 中对应 stub 位于第 4183、4185 行。
- 推断：上述 engine 函数的 stub 直接由“非 `UFUNCTION` 同名 overload 污染候选集合”触发。依据是这些头文件里与目标函数同名、且足以改变 resolver 候选集的额外声明，正是这些未反射的 helper overload，而当前实现不会过滤它们。

### 发现 17：interface 专用 stub 策略没有同步进入 skipped 诊断，已生成的 interface stub 在 `SkippedEntries.csv` 中完全不可见

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | `AngelscriptFunctionTableExporter.cs:29-43, 64-95`; `AngelscriptFunctionTableCodeGenerator.cs:465-479` |
| 描述 | 生成阶段对 `Interface` / `NativeInterface` 直接执行 `eraseMacro = "ERASE_NO_FUNCTION()"`，但 exporter 的 skipped 统计只记录 `AngelscriptFunctionSignatureBuilder.TryBuild()` 失败的函数，没有为这条 interface 特判留下任何诊断出口。结果是当前函数表里已经存在大量 interface stub，但 `AS_FunctionTable_SkippedEntries.csv` 对它们全部静默。 |
| 根因 | `CollectEntries()` 和 `CountBlueprintCallableFunctions()` 分属两条独立链路，前者实现了 interface 强制 stub 规则，后者却没有复刻同一条判定，也没有把“生成阶段主动降级”为一种 `FailureReason`。 |
| 影响 | interface 覆盖率下降时，`SkippedEntries.csv` 和控制台 reconstructed/skipped 统计都无法指出真正的 stub 原因，人工排查会误以为这些接口函数不存在问题，从而削弱错误诊断质量。 |

证据补充：

- 生成器在 interface class 上不看 `TryBuild()` 结果，直接写入 `ERASE_NO_FUNCTION()`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:465-479`。
- exporter 的统计逻辑只在 `TryBuild()` 返回 `false` 时追加 skipped row，没有任何 interface 专用分支：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:29-43, 64-95`。
- 当前产物里已验证存在大量 interface stub，例如 `UCameraLensEffectInterface::GetParticleComponents` 位于 `AS_FunctionTable_Entries.csv:883`，`UNavMovementInterface::StopActiveMovement` 位于 `AS_FunctionTable_Entries.csv:2963`。
- 我对当前产物做了按类名统计：`UCameraLensEffectInterface`、`UInterface_AssetUserData`、`ULevelInstanceInterface`、`UNavMovementInterface`、`UTypedElementWorldInterface` 共计 `44` 条 entry 已写成 stub，而 `AS_FunctionTable_SkippedEntries.csv` 中这 5 个 class 的匹配行数为 `0`。这是我基于仓库内生成产物执行的实际统计结果。
- 现有自动化测试没有任何针对这些 interface class 的校验；在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 中搜索 `NavMovementInterface`、`CameraLensEffectInterface`、`Interface_AssetUserData` 均无命中。

### 发现 18：interface 绑定模型把 owning type 固定为 `U...` wrapper，导致带原生实现的 `I...` Blueprint API 被整体放弃 direct bind

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | `AngelscriptFunctionSignatureBuilder.cs:90-97`; `AngelscriptFunctionTableCodeGenerator.cs:14-22, 465-479` |
| 描述 | `AngelscriptFunctionSignatureBuilder` 总是把 `OwningType` 设成 `classObj.SourceName`，而注册行也固定用 `{ClassName}::StaticClass()`。对 interface 来说，这个 `SourceName` 是 `UNavMovementInterface` 这类 `U...` wrapper，但真正的 native 方法实现位于 `INavMovementInterface`。生成器因此只能通过“一律写 stub”回避无效的 `ERASE_METHOD_PTR(UNavMovementInterface, ...)`；像 `INavMovementInterface::StopActiveMovement`、`StopMovementKeepPathing` 这种已有 `UE_API` 实现的 BlueprintCallable 函数，也被整体排除在 direct bind 之外。 |
| 根因 | UHTTool 的签名/entry 数据模型没有表达“反射拥有者是 `U...`，但 native 方法定义者是 `I...`”这一 UE interface 双类型结构；一旦进入 interface 路径，只能用 blanket stub 规避类型不匹配。 |
| 影响 | 这不是单纯的统计口径问题，而是实打实丢掉了可直绑的 native API。interface 上已有导出实现的方法会被强制降级到 stub/reflective 路径，直接降低生成结果的 direct coverage，并把 UE interface 适配边界硬编码成“永不直绑”。 |

证据补充：

- `AngelscriptFunctionSignatureBuilder` 构造签名时直接把 `OwningType` 设为 `classObj.SourceName`，`BuildRegistrationLine()` 也据此生成 `{ClassName}::StaticClass()` 注册调用：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:90-97` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14-22`。
- 生成器随后在 interface class 上无条件改写成 `ERASE_NO_FUNCTION()`，说明当前模型没有能力安全表达 interface 的 direct pointer：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:465-479`。
- `INavMovementInterface` 的 `StopActiveMovement` 与 `StopMovementKeepPathing` 在头文件里就是 `UFUNCTION(BlueprintCallable)` + `UE_API virtual`，并且在 engine `.cpp` 中存在原生实现：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/NavMovementInterface.h:127-134` 与 `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/NavMovementInterface.cpp:9-23`。
- 当前函数表仍把这两个函数写成 stub：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:2963-2964`。
- `ICameraLensEffectInterface` 也提供了 `ENGINE_API` 的 BlueprintCallable 虚函数声明，但生成产物同样把 `GetParticleComponents` / `GetPrimaryParticleComponent` 落成 stub：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Camera/CameraLensEffectInterface.h:21-31` 与 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:883-884`。

---

## 分析 (2026-04-08 03:24)

### 发现 19：resolver 只在 class body 内找声明，完全忽略 header 末尾的 out-of-class `inline AActor::Foo()` 定义，导致可直绑函数被批量误判为 `unexported-symbol`

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs` |
| 行号 | 29-35, 49-56, 109-117, 253-314, 362-398 |
| 描述 | `TryBuild()` 先用 `TryFindClassBody()` 锁定 class body，再让 `FindCandidates()` 只在 `classBodyStart..classBodyEnd` 范围内搜索 `functionName + "("`。这意味着像 `AActor::GetActorForwardVector`、`K2_GetActorLocation`、`HasAuthority` 这类“在类内只给 declaration、真正的 `inline` 定义写在 header 末尾”的 BlueprintCallable 函数，resolver 永远看不到后面的 `inline AActor::Foo()` 实现，只会拿到 class body 内那条没有 `_API` 的 declaration，并在 `HasLinkableExport()` 中把它判成 `unexported-symbol`。 |
| 根因 | link visibility 判断依赖“候选 declaration 本身是否带 `_API` 或是否是 inline definition”，但候选收集范围被硬限制在 class body 内，没有把同一 header 里 class 外的 `AActor::Foo` inline 定义纳入匹配。 |
| 影响 | 一批实际上已经在 header 中提供可链接定义的 engine Blueprint API 会被系统性降级成 `ERASE_NO_FUNCTION()`，直接损伤函数表生成正确性；同时 `SkippedEntries.csv` 还会把这类误判伪装成“符号未导出”，把排查方向带偏。 |

证据补充：

- `TryFindClassBody()` 只返回 class body 的起止位置，`FindCandidates()` 也只在这个区间内做 `IndexOf(functionName + "(")` 搜索：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:29-35, 253-293, 362-398`。
- `IsLinkVisible()` 只有在候选 declaration 自带 `_API`，或候选文本本身包含 `inline` / `FORCEINLINE` / `{` 时才认为可链接；它不会额外搜索 header 其他位置的 `AActor::FunctionName` 定义：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:109-117, 295-314`。
- `Actor.h` 的 class body 中，`GetActorForwardVector` / `GetActorUpVector` / `GetActorRightVector` 只有普通 declaration，没有 `ENGINE_API`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:1594-1604`。
- 同一头文件后半段又提供了真正可链接的 out-of-class inline 定义，包括 `K2_GetActorLocation`、`GetActorForwardVector`、`GetActorEnableCollision`、`HasAuthority` 和 `GetRemoteRole`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:4907-4966, 4981-4984`。
- 当前生成产物已经把这些函数误降级为 stub，并在 skipped CSV 中统一记成 `unexported-symbol`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:511-523` 与 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:14-88`。
- 对 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 执行 `rg "GetActorForwardVector|K2_GetActorLocation|HasAuthority|GetRemoteRole"` 无命中，说明现有测试没有覆盖这条 out-of-class inline 可见性边界。

### 发现 20：UHTTool 只读取 UHT 静态元数据，无法感知运行时 bind pass 动态打上的 `NotInAngelscript`，summary/csv 会把“明确禁止暴露”的函数误记为 direct

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:490-515`; `Bind_USceneComponent.cpp:171-176`; `Bind_UUserWidget.cpp:312-317` |
| 描述 | `ShouldGenerate()` 只看 UHT session 里的静态 `function.MetaData`，而运行时多个手写 bind pass 又会在 editor 启动时用 `SetMetaData(TEXT("NotInAngelscript"), TEXT("true"))` 二次禁用某些 BlueprintCallable。当前产物里 `USceneComponent::GetSocketQuaternion` 和 `UUserWidget::GetIsVisible` 已被 runtime 明确标记为“不自动绑定”，但 UHTTool 仍把它们写成 direct entry，并计入 summary/csv 的 direct coverage。 |
| 根因 | UHTTool 的生成阶段发生在 UHT exporter，上游只看编译时元数据；运行时排除策略则散落在 C++ bind pass 中，通过 `FindObject<UFunction>` + `SetMetaData()` 动态修改。两条链路没有共享“最终应该暴露哪些 BlueprintCallable”的统一来源。 |
| 影响 | 诊断产物会系统性高估 direct coverage，并把“运行时明确不该暴露”的函数伪装成成功生成；开发者如果只看 `AS_FunctionTable_Entries.csv` / `AS_FunctionTable_Summary.json`，会得出与最终绑定面不一致的结论。与此同时，UHT 还会为这些死条目生成额外注册代码，制造无意义的增量噪声。 |

证据补充：

- `ShouldGenerate()` 的过滤条件只覆盖静态 `MetaData.ContainsKey("NotInAngelscript")` / `BlueprintInternalUseOnly` / `CustomThunk` 等编译时信息，没有任何运行时扩展入口：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:490-515`。
- `Bind_USceneComponent.cpp` 在 `WITH_EDITOR` 下显式查找 `/Script/Engine.SceneComponent:GetSocketQuaternion` 并写入 `NotInAngelscript`，注释直接说明是为了阻止自动绑定：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_USceneComponent.cpp:171-176`。
- `Bind_UUserWidget.cpp` 同样把 `/Script/UMG.UserWidget:GetIsVisible` 动态标成 `NotInAngelscript`，理由是它与 `IsVisible()` 的 property accessor 语义冲突：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UUserWidget.cpp:312-317`。
- 运行时真正消费函数表时，会先调用 `FAngelscriptBinds::ShouldSkipBlueprintCallableFunction()`，而该函数明确把 `NotInAngelscript` 当成硬跳过条件：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:26-31` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:83-107`。
- 同一个运行时 skip 函数里还额外硬编码排除了 `UActorComponent::GetOwner`，说明“最终暴露面”本来就不等于 UHT 静态元数据：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:109-114`。但当前 UHT 产物仍把它写成 direct entry，且 skipped CSV 中没有对应记录：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:412`。
- 尽管如此，当前 UHT 产物仍把这两个函数写成 direct entry：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:3344` 与 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:4783`。对 `AS_FunctionTable_SkippedEntries.csv` 执行类限定搜索 `^Engine,USceneComponent,GetSocketQuaternion,` 和 `^UMG,UUserWidget,GetIsVisible,` 均无命中，说明这些运行时禁用函数在 UHT 诊断侧被完整算进了 direct 路径。

---

## 分析 (2026-04-08 03:46)

### 发现 21：UHT 诊断按原始 `UFunction` 名记账，但运行时按脚本名去重，`SkippedEntries.csv` 会把“已由手写绑定覆盖”的 API 继续报成失败

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:14-22`; `Helper_FunctionSignature.h:85-120, 268-315`; `Bind_BlueprintCallable.cpp:62-69`; `BlueprintCallableReflectiveFallback.cpp:230-252`; `Bind_AActor.cpp:27-33` |
| 描述 | UHTTool 在生成和 CSV 诊断里始终使用 `classObj.SourceName` 与 `function.SourceName`。运行时真正绑定时，却会先把 `K2_` / `BP_` / `AS_` 前缀剥掉、应用 `ScriptName` 元数据，并把带 `ScriptMixin` 的静态函数重映射到脚本接收者类型；随后再用归一化后的脚本名/声明做 `IsScriptDeclarationAlreadyBound()` 去重。两条链路的“函数身份”定义不一致，导致当前 `SkippedEntries.csv` 会把脚本层已经存在的 API 继续记成失败。 |
| 根因 | UHT 导出层只围绕 native pointer 表做建模，没有复用运行时 `FAngelscriptFunctionSignature` 的脚本命名、namespace/mixin 归属和重复声明判定规则。 |
| 影响 | 开发者无法从 `AS_FunctionTable_Entries.csv` / `AS_FunctionTable_SkippedEntries.csv` 直接判断脚本 API 是否真的缺失。像 `K2_GetActorLocation` 这类条目会被报成 `unexported-symbol`，但最终脚本里的 `GetActorLocation()` 其实已由手写绑定提供；同类问题还会把 `ScriptName` overload、`ScriptMixin` 成员函数和原始 `UFunction` 名混在一起，降低错误诊断质量并制造假阳性回归。 |

证据补充：

- `BuildRegistrationLine()` 和 `AS_FunctionTable_Entries.csv` 只记录原始 `FunctionName`，完全不包含脚本名、namespace 或 mixin 接收者：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:14-22, 125-135, 244-265`。
- 运行时签名层会把 `K2_` / `BP_` / `AS_` 前缀剥掉，并应用 `ScriptName`；对带 `ScriptMixin` 的静态函数，还会把第一参数移除并把 `ClassName` 改成 mixin 目标类型：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:85-120, 268-315`。
- 绑定阶段在真正注册前会调用 `IsScriptDeclarationAlreadyBound()`，只要脚本类型上已经存在同名方法或同声明方法就直接返回，不再消费当前 UHT entry：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:62-69` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:230-252`。
- `AActor` 已经有手写 `GetActorLocation()` / `GetActorRotation()` 绑定：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AActor.cpp:27-33`。但当前 UHT 产物仍把 `K2_GetActorLocation`、`K2_GetActorRotation` 记成 stub，并在 skipped CSV 中报 `unexported-symbol`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:85-86` 与 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:521-522`。
- `UAngelscriptComponentLibrary` 通过 `UCLASS(meta = (ScriptMixin = "USceneComponent"))` 和函数级 `ScriptName = "SetWorldLocationAndRotation"` 把静态 helper 暴露成 `USceneComponent` 成员 overload：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h:7-8, 183-192`。但当前 UHT CSV 仍把 `SetWorldLocationAndRotation` 和 `SetWorldLocationAndRotationQuat` 当成 `UAngelscriptComponentLibrary` 的两个原始函数分别记账，其中前者还是 stub：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:5806-5807` 与 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:133`。
- 现有 `AngelscriptGeneratedFunctionTableTests.cpp` 只验证 raw CSV 行和计数关系；对 `K2_` 别名、`ScriptName`、`ScriptMixin` 与手写绑定去重均无覆盖。我对该测试文件执行 `rg 'K2_|ScriptName|ScriptMixin|SetWorldLocationAndRotationQuat|SetWorldLocationAndRotation'` 无命中。

---

## 分析 (2026-04-08 03:58)

### 发现 22：UHTTool 会为 `BlueprintCallable + BlueprintNativeEvent/RPC` 生成死表项，但 runtime 实际走的是 `BindBlueprintEvent()` 路径

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:490-515`; `AngelscriptFunctionTableExporter.cs:55-61`; `Bind_BlueprintType.cpp:747-754, 1305-1314, 1397-1406` |
| 描述 | UHTTool 的入口条件只要看到 `BlueprintCallable/BlueprintPure` 就会生成 `FAngelscriptBinds::AddFunctionEntry()`，不会排除同时带 `FUNC_BlueprintEvent` 或 `FUNC_NetFuncFlags` 的函数。但 runtime 真正绑定时，凡是命中 `FUNC_BlueprintEvent | FUNC_NetFuncFlags` 都优先走 `BindBlueprintEvent()`，不会消费 `ClassFuncMaps` 里的 generated function table entry。结果是当前产物里已经存在一批“生成了 direct entry，但运行时完全不会走这条表”的死记录。 |
| 根因 | UHTTool 的筛选条件只复用了“是不是 BlueprintCallable/Pure”，没有复用 runtime 在 `Bind_BlueprintType.cpp` 里的分流规则；导出层与绑定层对“哪些函数应该进入 generated function table”的定义不一致。 |
| 影响 | `AS_FunctionTable_Entries.csv`、`AS_FunctionTable_Summary.json` 和 shard `.cpp` 会把这些事件/RPC 误记为 direct coverage，并额外生成无效注册代码；开发者看到 direct entry 会误以为 UHT 表覆盖了这类 API，但真实执行仍走 event/RPC 绑定路径。现有统计至少被当前仓库中的 3 条 entry 污染。 |

证据补充：

- UHTTool 只用 `BlueprintCallable/BlueprintPure` 判定入口：`IsBlueprintCallable()` 只检查 `function.FunctionFlags` 里的这两个标志，`ShouldGenerate()` 直接复用它，没有排除 `BlueprintEvent` 或 RPC：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:55-61` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:490-515`。
- runtime 绑定顺序明确把 `FUNC_BlueprintEvent | FUNC_NetFuncFlags` 放在 `FUNC_BlueprintCallable | FUNC_BlueprintPure` 之前；命中前者时直接 `BindBlueprintEvent()`，不会走 `BindBlueprintCallable()`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:747-754, 1305-1314, 1397-1406`。
- 当前支持模块里已经有这类函数，并且 UHT 产物确实把它们落成 direct entry：
  - `AGameModeBase::GetDefaultPawnClassForController` 是 `BlueprintCallable, BlueprintNativeEvent`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/GameModeBase.h:83-84`；CSV 中对应 direct entry 位于 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:207`。
  - `AGameModeBase::PlayerCanRestart` 是 `BlueprintCallable, BlueprintNativeEvent`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/GameModeBase.h:436-437`；CSV 中对应 direct entry 位于 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:213`。
  - `APlayerController::ClientSetHUD` 是 `BlueprintCallable, Reliable, Client` RPC：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerController.h:1184-1185`；CSV 中对应 direct entry 位于 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:315`。
- 现有测试没有覆盖这条分流边界。我对 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 执行 `rg 'ClientSetHUD|GetDefaultPawnClassForController|PlayerCanRestart'` 无命中，说明当前自动化没有验证“event/RPC 不应被记为 generated function table direct coverage”。

### 发现 23：头文件过滤只排除 `/Private/`，`Testing/` 下的 automation helper 也会被当成常规 runtime API 写进函数表和覆盖统计

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Low |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/LatentAutomationCommandClientExecutor.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:517-529`; `LatentAutomationCommandClientExecutor.h:16-18, 66-78`; `IntegrationTest.cpp:526-555, 765-782` |
| 描述 | `IsSupportedHeader()` 目前只排除 `/Private/` 和一个硬编码 engine 头，不区分 `Testing/` / `Tests/` 这类 automation scaffolding。结果是 `Source/AngelscriptRuntime/Testing/LatentAutomationCommandClientExecutor.h` 里的 7 个 `BlueprintCallable` assert helper，会和正常产品 API 一样进入 `AS_FunctionTable_Entries.csv`，并计入 `AngelscriptRuntime` 模块的 direct coverage。 |
| 根因 | UHTTool 的“支持 header”判定只围绕可包含性做最小过滤，没有引入任何“诊断统计是否应排除 automation-only 头文件”的层次；因此源码树下的 test harness 只要不是 `Private` 就会被纳入函数表。 |
| 影响 | `AngelscriptRuntime` 模块的 shard 内容和 coverage 统计会被 automation helper 污染；修改测试辅助类也会触发常规 runtime 函数表重生成，降低增量构建信噪比。 |

证据补充：

- `IsSupportedHeader()` 的过滤条件只有 `/Private/` 和 `InstancedSkinnedMeshComponent.h` 特例，没有任何 `Testing/` / `Tests/` 排除：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:517-529`。
- `ALatentAutomationCommandClientExecutor` 的头文件位于 `Source/AngelscriptRuntime/Testing/`，类注释明确写着“Executes a ULatentAutomationCommand on client”，其对外暴露的 7 个 `BlueprintCallable` 入口全部是 assert/fail helper：`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/LatentAutomationCommandClientExecutor.h:16-18, 66-78`。
- 当前生成产物已经把这 7 个 automation helper 全部写成 direct entry：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:5637-5643`。
- 该类在仓库中的主要调用点集中在 integration test scaffolding：`IntegrationTest.cpp` 会生成/查找 `ALatentAutomationCommandClientExecutor` 来驱动 client latent automation command，而真正的 integration test runner 位于紧随其后的 `#if WITH_DEV_AUTOMATION_TESTS` 块内：`Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp:526-555, 765-782, 782-979`。
- 推断：这些 entry 更像 automation support surface，而不是应进入常规 runtime coverage 的产品 API。依据是文件目录、类注释、方法命名（`AssertTrue/AssertFalse/Fail`）以及调用点都指向 integration test 基础设施。
- 现有 `AngelscriptGeneratedFunctionTableTests.cpp` 没有任何针对 `Testing/` 目录过滤的校验。我对该文件执行 `rg 'LatentAutomationCommandClientExecutor|Automation Command Client Executor'` 无命中。

---

## 分析 (2026-04-08 04:07)

### 发现 24：resolver 不展开 `GENERATED_UCLASS_BODY()` 注入的 `public:`，会把整类 public Blueprint API 系统性误判为 `non-public`

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs` |
| 行号 | 42-43, 390, 438-459 |
| 描述 | `FindAccessSpecifier()` 只在 header 原文里顺序扫描字面量 `public:` / `protected:` / `private:`。对于使用 `GENERATED_UCLASS_BODY()` 的 UE 类，源码通常不再显式写 `public:`，而是依赖宏展开把后续成员切到 public 区域。resolver 不展开该宏，于是 `FindCandidates()` 找到函数声明后，`candidate.IsPublic` 仍保持默认 `private`，最终把原本 public 的 `BlueprintCallable/BlueprintPure` 全部折叠成 `non-public`。 |
| 根因 | 访问级别判定建立在“未预处理的 header 文本”上，只识别显式 access specifier，没有把 UE 生成宏带来的访问级别变化纳入模型。 |
| 影响 | 当前已有整批可直绑的 public API 被错误排除出函数表，直接损伤代码生成正确性；同时 `SkippedEntries.csv` 把它们伪装成访问控制问题，误导错误诊断。现有自动化也没有覆盖这类 legacy macro class。 |

证据补充：

- resolver 在 `publicCandidates.Count == 0` 时直接返回 `failureReason = "non-public"`，而 `isPublic` 完全来自 `FindAccessSpecifier(header, classBodyStart, nameIndex) == "public"` 的字面量扫描：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:42-43, 390, 438-459`。
- `UAIBlueprintHelperLibrary`、`UHorizontalBox`、`UOverlay` 都使用 `GENERATED_UCLASS_BODY()`，源码中紧随其后的 Blueprint API 没有显式 `public:`，但声明本身显然是产品级 public API：`../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/Blueprint/AIBlueprintHelperLibrary.h:28-94`、`../../UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Components/HorizontalBox.h:23-27`、`../../UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Components/Overlay.h:20-30`。
- 当前产物已经出现对应误判：`UAIBlueprintHelperLibrary` 有 16 条 BlueprintCallable/Pure 全部落入 `non-public`，包括 `GetAIController`、`SimpleMoveToActor`；`UHorizontalBox::AddChildToHorizontalBox`、`UOverlay::AddChildToOverlay`、`UOverlay::ReplaceOverlayChildAt` 也都被记成 `non-public`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:7-22, 3720, 3739-3740`。
- 对 `AS_FunctionTable_Entries.csv` 执行类限定搜索 `^AIModule,UAIBlueprintHelperLibrary,`、`^UMG,UHorizontalBox,`、`^UMG,UOverlay,` 均无命中，说明这些条目没有以 direct/stub 任一形式进入生成表。
- 现有 `AngelscriptGeneratedFunctionTableTests.cpp` 对 `AIBlueprintHelperLibrary`、`GetAIController`、`SimpleMoveToActor`、`UHorizontalBox`、`AddChildToHorizontalBox`、`UOverlay`、`AddChildToOverlay` 的 `rg` 搜索均无命中，说明当前测试没有覆盖 `GENERATED_UCLASS_BODY()` 访问级别边界。

---

## 分析 (2026-04-08 04:09)

### 发现 25：interface 类 BlueprintCallable 会被强制写成永久死 stub，summary/csv 把这类“设计上不可绑定”的条目也记入 coverage

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:105, 133, 466-476`; `Bind_BlueprintCallable.cpp:35-47, 74-76`; `BlueprintCallableReflectiveFallback.cpp:267-269, 374-382` |
| 描述 | 生成器遇到 `UhtClassType.Interface/NativeInterface` 时，不尝试解析签名，直接把 entry 写成 `ERASE_NO_FUNCTION()`；运行时消费这些 entry 时，如果没有 direct pointer，只能进入 `BindBlueprintCallableReflectiveFallback()`，而 fallback 又明确拒绝 `CLASS_Interface`。因此这类条目不是“暂时 unresolved”，而是在当前架构下永远不会变成 callable。 |
| 根因 | UHTTool 仍把 interface Blueprint API 视为 generated function table 的一部分，但 runtime 的 BlueprintCallable 绑定路径并没有对应的 interface 实现策略，只留下了“生成 stub -> 绑定失败”的死链路。 |
| 影响 | `AS_FunctionTable_Entries.csv`、`AS_FunctionTable_Summary.json` 和 shard `.cpp` 会稳定包含一批永远不可绑定的 dead entry，系统性拉低 stub coverage、污染 unresolved 基线，并制造无意义的注册代码和增量噪声。 |

证据补充：

- 生成器在 `classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface` 时直接把 `eraseMacro` 设为 `ERASE_NO_FUNCTION()`，同时 csv 侧把所有 `ERASE_NO_FUNCTION()` 统一归类为 `Stub`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:105, 133, 466-476`。
- runtime 的 `BindBlueprintCallable()` 只要发现 entry 没有 direct native pointer，就会尝试 `BindBlueprintCallableReflectiveFallback()`；而 fallback eligibility 在 owning class 带 `CLASS_Interface` 时立即返回 `RejectedInterfaceClass`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:35-47, 74-76` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:267-269, 374-382`。
- `Bind_BlueprintType.cpp` 对 BlueprintCallable 的统一分流没有 interface 专用分支，仍然直接调用 `BindBlueprintCallable()`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:754, 1312-1314, 1404-1406`。
- 当前产物已经包含多组这类永久 stub，例如 `UCameraLensEffectInterface::{GetParticleComponents, GetPrimaryParticleComponent}`、`ULevelInstanceInterface::{GetWorldAsset, IsLoaded, LoadLevelInstance, UnloadLevelInstance, GetLoadedLevel}`、`UNavMovementInterface::{RequestDirectMove, RequestPathMove, GetVelocityForNavMovement, StopActiveMovement}`、`UTypedElementWorldInterface::{GetWorldTransform, SetWorldTransform, DeleteElement, PromoteElement}`，其 `EntryKind` 全部是 `Stub` 且 `EraseMacro` 全为 `ERASE_NO_FUNCTION()`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` 中对应类的导出记录。
- 这些接口头本身确实把相关方法声明成 `BlueprintCallable`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Camera/CameraLensEffectInterface.h:24-29`、`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Public/LevelInstance/LevelInstanceInterface.h:38-88`、`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/NavMovementInterface.h:50-124`、`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Public/Elements/Interfaces/TypedElementWorldInterface.h:304-404`。
- 现有 `AngelscriptGeneratedFunctionTableTests.cpp` 对 `UCameraLensEffectInterface`、`ULevelInstanceInterface`、`UNavMovementInterface`、`UTypedElementWorldInterface` 及其代表性函数名的 `rg` 搜索均无命中，说明当前自动化没有区分“暂时 stub”与“按设计永远不会成功的 interface stub”。

---

## 分析 (2026-04-08 04:11)

### 发现 26：interface 强制 stub 与 exporter 统计完全分叉，`SkippedEntries.csv` 对这批确定失败项报 0 条

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:466-476`; `AngelscriptFunctionTableExporter.cs:40, 64-82, 94` |
| 描述 | generator 对 interface class 采用硬编码分支，直接产出 `ERASE_NO_FUNCTION()` stub；但 exporter 的 `CountBlueprintCallableFunctions()` 完全不知道这条规则，仍然只以 `AngelscriptFunctionSignatureBuilder.TryBuild()` 是否成功来统计 `reconstructed/skipped`。结果是当前已经落成 `Stub` 的 interface 条目，在 `SkippedEntries.csv` 中一个都没有。 |
| 根因 | “实际生成什么 entry” 与 “诊断里哪些算 skipped” 走了两套不同判定：前者内建了 interface special-case，后者没有复用 generator 的决策结果。 |
| 影响 | `SkippedEntries.csv` 和控制台 `reconstructed/skipped` 数对 interface 类是失真的，无法反映这批确定 unresolved 的 entry；开发者看 skipped 报表时会错误低估真实 stub 根因，测试也无法用该文件监控 interface coverage 退化。 |

证据补充：

- generator 在 `CollectEntries()` 中遇到 `UhtClassType.Interface/NativeInterface` 会直接跳过签名构建，强制写 `ERASE_NO_FUNCTION()`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:466-476`。
- exporter 遍历同一批 BlueprintCallable 时，只要 `AngelscriptFunctionSignatureBuilder.TryBuild()` 返回 true 就计入 `reconstructedCount`；只有返回 false 才进入 `skippedEntries`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:40, 64-82, 94`。
- 当前产物里，`UCameraLensEffectInterface` 2 条、`ULevelInstanceInterface` 6 条、`UNavMovementInterface` 11 条、`UTypedElementWorldInterface` 22 条，共 41 条 interface entry 已全部写入 `AS_FunctionTable_Entries.csv`，且全是 `Stub/ERASE_NO_FUNCTION()`。
- 对同一组类在 `AS_FunctionTable_SkippedEntries.csv` 执行限定搜索，`skippedRows=0`；唯一带 `Interface` 字样的 skipped 记录来自无关的 `UCameraLensEffectInterfaceClassSupportLibrary`，不是上述 interface stub：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:765-768`。
- 现有 `AngelscriptGeneratedFunctionTableSkippedCsvOutputTest` 只验证 skipped csv 有 4 列且 `FailureReason` 非空，没有任何“Entries.csv 中的强制 stub 是否也应在 skipped 诊断出现”的一致性校验：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:670-697`。

---

## 分析 (2026-04-08 04:12)

### 发现 27：`GENERATED_UCLASS_BODY()` 访问级别误判不会把 public API 移出函数表，而是静默降级为 `Stub`

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:42-43, 390, 438-459`; `AngelscriptFunctionTableCodeGenerator.cs:470-476` |
| 描述 | 对 `GENERATED_UCLASS_BODY()` 类，resolver 会把后续成员误判成 `non-public`；但 generator 并不会因此丢弃该函数，而是把 `TryBuild()` 失败的条目统一降级成 `ERASE_NO_FUNCTION()`。所以当前真实产物不是“函数缺失”，而是“一批本该可直接绑定的 public Blueprint API 被静默写成了 stub”。 |
| 根因 | 访问级别解析错误先把 public 函数误报成 `non-public`，随后 generator 把这类失败无差别映射成 stub，没有为“public 解析误判”提供单独的恢复或告警路径。 |
| 影响 | direct coverage 被系统性低估，`Entries.csv`/`Summary.json` 会把这类 public API 记成 stub，开发者如果只看报表，会误以为这些函数缺少可调用实现而不是 header 访问级别解析失真。 |

证据补充：

- resolver 的 `publicCandidates.Count == 0 -> non-public` 路径见：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:42-43, 390, 438-459`。
- generator 对 `TryBuild()` 失败的普通类函数没有区分失败原因，统一走 `eraseMacro = "ERASE_NO_FUNCTION()"`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:470-476`。
- 当前产物里，`UAIBlueprintHelperLibrary` 有 15 条、`UHorizontalBox` 1 条、`UOverlay` 2 条 entry 已进入 `AS_FunctionTable_Entries.csv`，但全部被记成 `Stub/ERASE_NO_FUNCTION()`，包括 `GetAIController`、`SimpleMoveToActor`、`AddChildToHorizontalBox`、`AddChildToOverlay`、`ReplaceOverlayChildAt`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:4106-4120, 4457, 4555-4556`。
- 同一批函数在 `AS_FunctionTable_SkippedEntries.csv` 中的失败原因仍是 `non-public`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:8-22, 3720, 3739-3740`。其中 `UAIBlueprintHelperLibrary` 比 entries 多出的 1 条是 `CreateMoveToProxyObject`，该函数还带 `BlueprintInternalUseOnly`，说明 skipped 诊断与实际生成范围本来就未完全对齐。
- 这些 API 在头文件中都是 `GENERATED_UCLASS_BODY()` 之后的常规 public Blueprint 入口：`../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/Blueprint/AIBlueprintHelperLibrary.h:28-94`、`../../UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Components/HorizontalBox.h:23-27`、`../../UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Components/Overlay.h:20-30`。

---

## 分析 (2026-04-08 04:24)

### 发现 28：resolver 把 `BlueprintProtected` 函数一律按 `non-public` 丢弃，但 runtime 明明已经有受保护脚本方法的落地路径

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:37-43, 438-459`; `Helper_FunctionSignature.h:262, 417-445` |
| 描述 | resolver 在 `TryBuild()` 中先把 `FindCandidates()` 的结果收窄成 `candidate.IsPublic`，只要没有 public 候选就直接回报 `non-public`。这条规则没有为显式 `meta=(BlueprintProtected="true")` 留任何例外，导致一批“应该以 protected 方式暴露给脚本派生类”的 BlueprintCallable 被整批降级成 `ERASE_NO_FUNCTION()`。 |
| 根因 | UHTTool 的访问控制模型只有“public 才可生成”这一档位，没有把 Unreal 已经显式暴露出来的 `BlueprintProtected` 语义接到生成链路；而 runtime 的签名与注册层实际上已经支持把函数标成 `protected`。 |
| 影响 | 当前函数表会错误丢失原生 protected Blueprint API，既损伤代码生成正确性，也让 `SkippedEntries.csv` 把这些函数伪装成普通访问控制失败。由于 runtime 已支持 `ScriptFunction->SetProtected(true)`，这不是能力缺失，而是 UHTTool 前置筛选把可落地路径提前截断。 |

证据补充：

- resolver 只保留 `candidate.IsPublic`，没有读取任何 `BlueprintProtected` 元数据；当 `publicCandidates.Count == 0` 时直接返回 `failureReason = "non-public"`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:37-43, 438-459`。
- runtime 绑定层已经显式读取 `BlueprintProtected` 元数据，并在生成脚本函数后调用 `ScriptFunction->SetProtected(true)`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:26, 54, 262, 417-445`。
- `UUserWidget` 在 protected 区域显式声明了 8 个 `BlueprintCallable + BlueprintProtected` 输入相关 API，包括 `ListenForInputAction`、`RegisterInputComponent`、`SetInputActionPriority`、`SetInputActionBlocking`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Blueprint/UserWidget.h:1660-1701`。
- `ALevelScriptActor` 也在 protected utility surface 上显式标了 `BlueprintProtected`：`RemoteEvent` 与 `SetCinematicMode` 都是 `BlueprintCallable, meta=(BlueprintProtected="true")`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/LevelScriptActor.h:32-45`。
- 当前产物里，这 10 个函数都被 `SkippedEntries.csv` 记成 `non-public`，并在 `Entries.csv` 中落成 `Stub/ERASE_NO_FUNCTION()`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv` 中 `UUserWidget::{ListenForInputAction, StopListeningForInputAction, StopListeningForAllInputActions, RegisterInputComponent, UnregisterInputComponent, IsListeningForInputAction, SetInputActionPriority, SetInputActionBlocking}` 与 `ALevelScriptActor::{RemoteEvent, SetCinematicMode}` 的记录；对应 `Entries.csv` 中同名条目均为 `Stub`。
- 现有自动化只在 representative coverage 中确认 `UUserWidget` 这类类级 map 存在，没有任何针对 `BlueprintProtected` native function 的 direct/protected 绑定断言；对 `AngelscriptGeneratedFunctionTableTests.cpp` 执行 `ListenForInputAction|RegisterInputComponent|BlueprintProtected` 搜索只有 `UUserWidget` 的代表类常量命中，没有覆盖这些具体 API。

### 发现 29：自动化测试把生成目录硬编码成 `Win64/UnrealEditor`，对其他 target/platform 的 UHT 输出布局没有任何回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D / B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` |
| 行号 | `AngelscriptGeneratedFunctionTableTests.cpp:242, 459, 594, 669, 706`; `AngelscriptFunctionTableCodeGenerator.cs:120, 174, 220, 246, 434`; `AngelscriptFunctionTableExporter.cs:100` |
| 描述 | UHTTool 的 shard、summary 和 csv 全部通过 `factory.MakePath(...)` 生成，输出目录天然依赖当前 UHT target/host 布局；但现有 5 个自动化测试把读取目录硬编码为 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT`。结果是测试只验证“Win64 Editor 这一种路径形态”，而不是验证工具实际声明的输出契约。 |
| 根因 | 测试层没有复用 exporter/code generator 的路径生成机制，也没有通过当前 target、platform 或 `MakePath` 等价逻辑推导产物位置，而是把某次本地构建的中间目录结构写死进断言。 |
| 影响 | 这套回归测试无法覆盖非 `UnrealEditor` target、非 `Win64` host，或 UE5.x UHT/Intermediate 路径布局变化；一旦路径约定变化，测试要么直接误报“文件不存在”，要么继续只盯着旧目录，失去对真实产物的验证价值。 |

证据补充：

- code generator/exporter 对所有正式产物都通过 `factory.MakePath(...)` 决定落盘位置，包括 shard `.cpp`、`Summary.json`、`ModuleSummary.csv`、`Entries.csv` 和 `SkippedEntries.csv`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:120, 174, 220, 246, 434` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:100`。
- 自动化测试则在 5 处直接把生成目录写死为 `Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT`：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:242, 459, 594, 669, 706`。
- 这些硬编码目录分别驱动 editor guard、summary json、csv、skipped csv 和 macro-qualified direct binding 断言，说明当前绝大多数产物级验证都被单一路径假设绑死：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:237-245, 454-462, 589-597, 664-672, 701-708`。
- 现有测试全部注册在 `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter` 下，本身就只跑 editor automation；再叠加目录硬编码后，`WITH_EDITOR` shard 与 runtime shard 之外的 target 组合完全没有覆盖。

### 发现 30：`RepresentativeCoverageTest` 只校验“类上至少有一个条目”，无法发现单类内部的大面积函数回归

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | 275-309 |
| 描述 | `RepresentativeCoverageTest` 对每个代表类只断言两件事：`ClassFuncMaps` 中存在该类，以及 `FunctionMap->Num() > 0`。这意味着只要类上还剩任意一条 generated entry，测试就会通过，哪怕同一个类内部已经丢掉了一整组关键 BlueprintCallable。 |
| 根因 | 测试把“类级存在性”误当成“类级覆盖有效性”，没有为代表类建立任何函数级基线或关键 API 白名单。 |
| 影响 | 单类内部发生大面积 direct->stub、protected API 全丢、特定子域整体退化时，代表覆盖测试不会报警；它只能抓“整个类完全没有条目”这种最粗粒度故障。 |

证据补充：

- `RepresentativeCoverageTest` 把 `UUserWidget` 列为代表类之一，但最终断言仅是 `FunctionMap->Num() > 0`：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:288, 307`。
- 当前 `UUserWidget` 在 `Entries.csv` 中共有 64 条 generated entry，其中 12 条已经退化成 `Stub`，包括 `ListenForInputAction`、`RegisterInputComponent`、`SetInputActionPriority`、`SetInputActionBlocking` 等输入 API：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` 中 `ClassName == UUserWidget` 的导出记录。
- 同一类在 `SkippedEntries.csv` 里已经出现 8 条 `non-public` 和 4 条 `unexported-symbol`，说明它并不是“健康代表样本”，但 representative test 仍会因为其余 52 条 direct entry 而通过。
- 这意味着像发现 28 这种“类还在、但某个受保护子域整体失效”的回归，不会被现有 representative coverage 断言捕获。

---

## 分析 (2026-04-08 04:35)

### 发现 31：`BlueprintAuthorityOnly` 在 UHTTool native 绑定链路中完全丢失，服务器专用 Blueprint API 会被当成普通脚本函数暴露

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h` |
| 行号 | `Helper_FunctionSignature.h:260-262, 321-331, 379-393, 414-458`; `AngelscriptPreprocessor.cpp:1412, 1641-1643`; `AngelscriptEngine.h:990-991, 1043-1044`; `AngelscriptClassGenerator.cpp:3474-3475`; `Actor.h:721-722, 3172-3177` |
| 描述 | native BlueprintCallable 绑定路径只把 `WorldContext`、`DeterminesOutputType`、`NotAngelscriptProperty`、`BlueprintProtected`、deprecated 和 editor-only 等语义补回到脚本函数，却完全不读取也不传播 `BlueprintAuthorityOnly`。当前 `AActor::SetNetDormancy` 与 `AActor::FlushNetDormancy` 这类显式 `BlueprintAuthorityOnly` 的 engine API 已被 UHTTool 生成为普通 direct entry，没有任何 authority-only 标记。 |
| 根因 | UHTTool 生成的 function table 只传递 `Class + FunctionName + EraseMacro`，消费侧 `FAngelscriptFunctionSignature` 也没有对应字段或 DB 持久化位来承接 `FUNC_BlueprintAuthorityOnly`。与此同时，脚本源码路径却单独维护了 `bBlueprintAuthorityOnly` 元数据，说明 native 绑定链路和脚本生成链路已经发生语义分叉。 |
| 影响 | 脚本侧会把 Blueprint 语义上“仅服务器可调用”的原生函数暴露成普通可调用 API，既缺少编译期/运行期提示，也可能让客户端脚本误调用网络权威函数，造成行为偏差和排查困难。现有 UHT summary/csv 还会把这些条目算作正常 direct coverage，进一步掩盖语义丢失。 |

证据补充：

- `FAngelscriptFunctionSignature::InitFromFunction()` 只读取 `NotAngelscriptProperty`、`ScriptTrivial`、`BlueprintProtected`、deprecated 等元数据；`WriteToDB()` 和 `ModifyScriptFunction()` 也只持久化/回填这些字段，没有任何 `BlueprintAuthorityOnly` 分支：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:260-262, 321-331, 379-393, 414-458`。
- 当前脚本源码链路明确存在 `BlueprintAuthorityOnly` 概念：preprocessor 会把 spec 解析到 `FunctionDesc->bBlueprintAuthorityOnly`，`FAngelscriptFunctionDesc` 也保存该位，并在类生成阶段回写到 `FUNC_BlueprintAuthorityOnly`：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:1412, 1641-1643`、`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:990-991, 1043-1044`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp:3474-3475`。
- engine 头文件里，`AActor::SetReplicates`、`AActor::SetNetDormancy`、`AActor::FlushNetDormancy` 都显式标了 `BlueprintAuthorityOnly`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:721-722, 3172-3177`。
- 当前函数表产物已经把其中两项写成普通 direct entry，没有任何 authority-only 侧信号：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:11, 115` 分别对应 `FlushNetDormancy` 与 `SetNetDormancy`。`SetReplicates` 则因其他解析问题落成 `Stub`，但同样没有 authority-only 维度。
- 对 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg "BlueprintAuthorityOnly|SetNetDormancy|FlushNetDormancy|SetReplicates"` 无命中，说明当前自动化没有覆盖“native generated binding 是否保留 authority-only 语义”这条边界。

### 发现 32：非编辑器 `AS_USE_BIND_DB` 路径不会保存普通 BlueprintCallable 的 `ScriptName/K2_` 重命名，cooked API 面会与 editor 分叉

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h` |
| 行号 | `AngelscriptEngine.h:17`; `Bind_BlueprintType.cpp:705-755`; `Bind_BlueprintCallable.cpp:50-52, 62-68, 87-90, 149-150`; `Helper_FunctionSignature.h:85-120, 336-349, 379-393`; `Actor.h:1935-1937, 2044-2045` |
| 描述 | runtime 在 non-editor 下定义 `AS_USE_BIND_DB (!WITH_EDITOR)`，普通 native BlueprintCallable 会走 `InitFromDB()` 重放签名；但 `WriteToDB()` 只对 `FUNC_BlueprintEvent` 持久化 `ScriptName`。结果是所有依赖 `ScriptName` 元数据、或依赖 `K2_/BP_/AS_` 前缀剥离的 generated BlueprintCallable，在 cooked/非 editor 绑定时都会退回原始 `UFunction` 名。像 `K2_DestroyActor -> DestroyActor`、`K2_AttachToActor -> AttachToActor` 这类 editor 下正常简化的 API，会在 DB 路径重新暴露成原始 `K2_*` 名。 |
| 根因 | 脚本命名规范化逻辑只存在于 `InitFromFunction()`，而 DB 序列化层没有为普通 BlueprintCallable 保存 `ScriptName`。non-editor 又强制走 DB 重放，导致“editor 实时推导的脚本名”与“cooked 回放的脚本名”使用了两套不同来源。 |
| 影响 | editor 与 cooked 的脚本 API 面会发生真实分叉：文档、预编译缓存、运行时查找和用户脚本都可能在 `DestroyActor`/`AttachToActor` 与 `K2_DestroyActor`/`K2_AttachToActor` 之间失配。现有 editor 自动化无法发现这类问题，因为它根本不覆盖 `AS_USE_BIND_DB` 路径。 |

证据补充：

- `AS_USE_BIND_DB` 在 runtime 里直接定义成 `!WITH_EDITOR`；`Bind_BlueprintType` 在该模式下从 `FAngelscriptBindDatabase` 读取方法，并把每个 `DBFunc` 分发到 `BindBlueprintCallable()`/`BindBlueprintEvent()`：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:17` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:705-755`。
- `Bind_BlueprintCallable()` 在 DB 模式下只调用 `Signature.InitFromDB(...)`；而写 DB 的地方都复用 `Signature.WriteToDB(DBBind)`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:50-52, 62-68, 87-90, 149-150`。
- `GetScriptNameForFunction()` 明确会应用 `ScriptName` 元数据，并对普通 BlueprintCallable 剥离 `K2_` / `BP_` / `AS_` 前缀：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:85-120`。但 `InitFromDB()` 只在 `DBBind.ScriptName` 非空时使用保存值，否则直接退回 `InFunction->GetName()`；`WriteToDB()` 又只在 `Function->HasAnyFunctionFlags(FUNC_BlueprintEvent)` 时才写入 `DBBind.ScriptName`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:336-349, 379-393`。
- engine 头文件里，`K2_DestroyActor` 与 `K2_AttachToActor` 都显式声明了脚本名重写：`DestroyActor` 与 `AttachToActor`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:1935-1937, 2044-2045`。
- 当前 UHT 产物仍以原始 native 名记账：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:81, 83` 分别是 `K2_AttachToActor` 与 `K2_DestroyActor`。这与 DB 路径对 `UnrealPath` 的查找方式一起，意味着 non-editor 回放默认会继续沿用原始 `K2_*` 名。
- 现有测试只在 editor 环境下验证 `K2_DestroyActor` 这类原始 `UFunction` 名能进入 `ClassFuncMaps`，没有任何 non-editor / bind-db 断言来验证最终脚本名：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp:512-566`。

### 发现 33：`AS_USE_BIND_DB` 下 generated stub 永远进不了 reflective fallback，非 editor 构建会丢失整条兜底路径

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptEngine.h:17`; `Bind_BlueprintCallable.cpp:50-52, 76-90`; `Helper_FunctionSignature.h:41, 336-349`; `BlueprintCallableReflectiveFallback.cpp:374-389`; `AngelscriptGeneratedFunctionTableTests.cpp:355-452` |
| 描述 | non-editor 模式启用 `AS_USE_BIND_DB` 后，`Bind_BlueprintCallable()` 会用 `Signature.InitFromDB(..., false)` 恢复签名；这一步把 `bAllTypesValid` 直接设成 `false`，也不会填充 `ArgumentTypes`。但同一函数在 direct pointer 缺失时又照常调用 `BindBlueprintCallableReflectiveFallback()`，而 fallback 入口第一条类型校验就是 `if (!Signature.bAllTypesValid || Signature.ArgumentTypes.Num() > MaxArgs) return false;`。结果是 generated function table 中所有依赖 reflective fallback 的 stub，在非 editor/DB 路径上都会被硬性拒绝。 |
| 根因 | DB 重放路径为了省略类型重建，把 `InitFromDB()` 的 `bInitTypes` 传成 `false`；与此同时，reflective fallback 仍然依赖完整的 `FAngelscriptFunctionSignature` 类型信息。二者接口契约互相矛盾。 |
| 影响 | editor 下还能通过 reflective fallback 变成 callable 的 generated stub，在非 editor 构建中会系统性退化成 unresolved。这样不仅让 cooked 行为与 editor 分叉，也使当前 `Summary.json`/`Entries.csv` 对 fallback coverage 的预期在 shipping 路径上失效。 |

证据补充：

- runtime 把 `AS_USE_BIND_DB` 定义为 `!WITH_EDITOR`，说明这条路径正是 non-editor/cooked 默认配置：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:17`。
- `Bind_BlueprintCallable()` 在 DB 模式下调用 `Signature.InitFromDB(InType, Function, DBBind, /* bInitTypes= */ false)`；当 entry 没有 direct native pointer 时，仍继续调用 `BindBlueprintCallableReflectiveFallback(...)`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:50-52, 76-90`。
- `InitFromDB()` 直接执行 `bAllTypesValid = bInitTypes;`，因此这里会把 `bAllTypesValid` 设成 `false`，且不会填充 `ArgumentTypes`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:41, 336-349`。
- reflective fallback 入口对这两个字段有硬门槛：`if (!Signature.bAllTypesValid || Signature.ArgumentTypes.Num() > BlueprintCallableReflectiveFallbackMaxArgs) return false;`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:374-389`。
- 现有 reflective fallback 统计测试只在 `EditorContext` 下运行，并明确要求 `ReflectiveCount > 0`；它完全没有覆盖 non-editor / bind-db 路径，因此不会暴露这条 cooked 回归：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:355-452`。

---

## 分析 (2026-04-08 10:40)

### 发现 34：`UnsafeDuringActorConstruction` 在 UHT native 绑定链路中完全丢失，默认语句可直接暴露 Blueprint 明确标记为危险的原生函数

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/PrimitiveComponent.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h` |
| 行号 | `Helper_FunctionSignature.h:18-27, 52-58, 237-265, 336-390, 414-457`; `PrimitiveComponent.h:1604-1609, 1850-1855, 2767-2769`; `AngelscriptComponentLibrary.h:225-235` |
| 描述 | native BlueprintCallable 绑定链路只保留 `WorldContext`、`DeterminesOutputType`、`NotAngelscriptProperty`、`BlueprintProtected`、deprecated 和 editor-only 等少数元数据，没有任何 `UnsafeDuringActorConstruction` 读取或回填逻辑。当前 `UPrimitiveComponent::AddImpulse`、`WakeRigidBody`、`GetMass` 这类在 engine 头文件中明确标记 `UnsafeDuringActorConstruction="true"` 的函数，已经被 UHTTool 直接写成普通 `ERASE_AUTO_METHOD_PTR(...)` entry。 |
| 根因 | `FAngelscriptFunctionSignature` 的 native 侧模型没有为 `UnsafeDuringActorConstruction` 预留字段，`WriteToDB()` / `InitFromDB()` / `ModifyScriptFunction()` 也没有对应持久化或运行期限制；UHTTool 生成表时因此把这类函数与普通 BlueprintCallable 完全等同处理。 |
| 影响 | 脚本默认语句或构造期逻辑可以直接调用 Blueprint 明确标为“构造期不安全”的原生 API，且没有任何 trait、编译期提示或运行期保护。这会让 Angelscript 行为面与 Blueprint 安全边界分叉，并增加组件尚未完成初始化时触发物理/碰撞副作用的风险。 |

证据补充：

- `FAngelscriptFunctionSignature` 当前只读取 `WorldContext`、`DeterminesOutputType`、`NotAngelscriptProperty`、`BlueprintProtected`、deprecated 和 editor-only；源码中没有任何 `UnsafeDuringActorConstruction` 常量、字段或处理分支：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:18-27, 52-58, 237-265, 336-390, 414-457`。
- engine 头文件里，`UPrimitiveComponent::AddImpulse`、`WakeRigidBody`、`GetMass` 都显式声明了 `meta=(UnsafeDuringActorConstruction="true")`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/PrimitiveComponent.h:1604-1609, 1850-1855, 2767-2769`。
- 当前生成产物已经把这 3 个函数写成普通 direct entry：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:3126, 3157, 3279`。
- 插件自身已经承认“构造期调用某些原生 Blueprint API 需要额外保护”：`UAngelscriptComponentLibrary::AttachToComponent()` 在 `FUObjectThreadContext::Get().IsInConstructor` 时会主动 `Throw(...)`，阻止默认语句中调用：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h:225-235`。
- 对 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg 'UnsafeDuringActorConstruction|AddImpulse|GetMass|WakeRigidBody|AttachToComponent'` 无命中，说明当前自动化没有覆盖“UHT native 生成函数是否保留构造期安全边界”这条语义。

### 发现 35：`AS_USE_BIND_DB` 的数据库模式没有 `BlueprintProtected` 字段，即使前置生成修复，cooked 仍会把 protected native API 降格为 public

| 项目 | 内容 |
|------|------|
| 维度 | A / B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h` |
| 行号 | `AngelscriptBindDatabase.h:56-86`; `AngelscriptEngine.h:17`; `Bind_BlueprintCallable.cpp:50-52, 67, 88, 150`; `Helper_FunctionSignature.h:54, 262, 336-349, 379-393, 417-445` |
| 描述 | `FAngelscriptFunctionSignature` 在 editor 路径会读取 `BlueprintProtected` 并在 `ModifyScriptFunction()` 中调用 `ScriptFunction->SetProtected(true)`，但 non-editor 的 `AS_USE_BIND_DB` 模式完全不保存这项语义。`FAngelscriptMethodBind` 结构体没有 `bBlueprintProtected` 字段，`WriteToDB()` 与 `InitFromDB()` 也都没有相应读写。结果是只要某个 protected native Blueprint API 最终进入 bind DB，cooked 回放就会丢掉 protected 访问控制。 |
| 根因 | runtime 把“editor 实时从 `UFunction` 读取元数据”和“non-editor 从 bind DB 回放”的两条路径做成了不同 schema，但 DB schema 只覆盖了 `ScriptName`、`WorldContextArgument`、`DeterminesOutputTypeArgument`、`bNotAngelscriptProperty`、`bTrivial` 等字段，没有同步纳入 `BlueprintProtected`。 |
| 影响 | 推断：即便前面的 header 解析/生成问题修掉，shipping 仍会把本应仅对子类开放的 native API 暴露成普通 public script method，导致 editor 与 cooked 行为继续分叉。当前代码已经有 `SetProtected(true)` 落地点，因此这是缺字段造成的 cooked 语义回退，而不是能力不存在。 |

证据补充：

- runtime 在 non-editor 下强制启用 bind DB：`#define AS_USE_BIND_DB (!WITH_EDITOR)`，`Bind_BlueprintCallable()` 在该模式下直接走 `Signature.InitFromDB(...)`，而 editor 路径会在绑定前后多次调用 `Signature.WriteToDB(DBBind)`：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:17` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:50-52, 67, 88, 150`。
- `FAngelscriptFunctionSignature` 明确拥有 `bBlueprintProtected`，editor 路径会从 `UFunction` 读取该元数据，并在 `ModifyScriptFunction()` 中调用 `ScriptFunction->SetProtected(true)`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:54, 262, 417-445`。
- 但 `FAngelscriptMethodBind` 的持久化 schema 只包含 `Declaration`、`UnrealPath`、`ClassName`、`ScriptName`、`WorldContextArgument`、`DeterminesOutputTypeArgument`、`bStaticInUnreal`、`bStaticInScript`、`bGlobalScope`、`bNotAngelscriptProperty`、`bTrivial`，完全没有 `bBlueprintProtected`：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h:56-86`。
- 对应地，`WriteToDB()` / `InitFromDB()` 也都没有读写 `bBlueprintProtected`；non-editor 回放后，该字段会停留在默认值 `false`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:336-349, 379-393`。
- engine runtime 模块里真实存在待保护的 native Blueprint API，包括 `AActor::{GetInstigator, GetInstigatorController, MakeNoise}`、`ALevelScriptActor::{RemoteEvent, SetCinematicMode}`、`UUserWidget::{ListenForInputAction, RegisterInputComponent, SetInputActionPriority ...}`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:1418, 1432, 4369`、`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/LevelScriptActor.h:32, 44`、`../../UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Blueprint/UserWidget.h:1660-1700`。当前 `Entries.csv` 中它们仍是 stub，这说明 cooked 缺字段暂时被前置生成问题遮蔽，而不是已经被正确处理：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:38-39, 97, 241-242, 4791, 4805`。
- 对 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg 'BlueprintProtected|AS_USE_BIND_DB|GetInstigator|GetInstigatorController|MakeNoise'` 无命中，说明当前测试既没有覆盖 protected native 函数，也没有覆盖 non-editor bind-db 回放下的访问控制保真。

---

## 分析 (2026-04-08 10:56)

### 发现 36：`Summary.json` / `ModuleSummary.csv` 会把支持集合里的零产出模块整批吞掉，导致“整模块失踪”没有任何显式诊断

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:66-71, 81-90, 166-215, 218-265`; `AngelscriptRuntime.Build.cs:30-78`; `AngelscriptGeneratedFunctionTableTests.cpp:459-666` |
| 描述 | `Generate()` 只有在 `GenerateModule()` 返回非空 summary 时才把模块写入 `moduleSummaries`；而 `GenerateModule()` 只要 `entries.Count == 0` 就直接返回 `null`。结果是所有“在 `supportedModules.All` 里、但本轮没有任何 entry”的模块，会同时从 `AS_FunctionTable_Summary.json` 与 `AS_FunctionTable_ModuleSummary.csv` 中完全消失，而不是以 `totalEntries=0` 的方式留下可诊断记录。 |
| 根因 | 生成器把“该模块没有可生成条目”和“该模块不在支持范围内”混成了同一种 `null` 路径，summary schema 只覆盖非空模块，没有零值行。 |
| 影响 | 一旦某个原本应有绑定的模块因为 `ShouldGenerate()`、模块过滤、header 解析或 UE5.x API 适配变化而整批掉成零产出，诊断文件不会出现醒目的 `0 entry` 行，而是直接像这个模块从未纳入生成功能范围一样消失。人工排查和自动化都无法据此区分“真正不支持”与“支持模块回归为空”。 |

证据补充：

- `Generate()` 只在 `moduleSummary != null` 时把模块计入 `generatedFileCount` 和 `moduleSummaries`；`GenerateModule()` 一旦 `entries.Count == 0` 就 `return null`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:66-71, 81-90`。
- `WriteGenerationSummary()` 与 `WriteModuleSummaryCsv()` 都只序列化 `moduleSummaries`，因此零产出模块不会落到 `Summary.json` 或 `ModuleSummary.csv`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:166-215, 218-265`。
- 当前 `AngelscriptRuntime.Build.cs` 通过 `DependencyModuleNames.AddRange` / `if (Target.bBuildEditor)` 文本实际声明了 `32` 个支持模块（含 `AngelscriptRuntime` 本身）；这是我按生成器同样的解析规则对 `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:30-78` 执行实际统计得到的数字。
- 当前生成产物 `AS_FunctionTable_ModuleSummary.csv` 只包含 `14` 个模块；缺失的 `18` 个支持模块分别是 `ApplicationCore`、`Core`、`CoreOnline`、`CoreUObject`、`DeveloperSettings`、`EditorSubsystem`、`InputCore`、`Json`、`JsonUtilities`、`NetCore`、`Networking`、`PhysicsCore`、`Projects`、`Slate`、`SlateCore`、`Sockets`、`StructUtils`、`TraceLog`。这些模块在 summary 中没有任何 `0 entry` 记录，只是直接消失。
- 我进一步对 `Intermediate/Build/Win64/AngelscriptProjectEditor/Development/AngelscriptProjectEditor.uhtmanifest` 做了实际比对：上述 `32` 个支持模块里有 `24` 个确实进入了本次 project editor 的 UHT session，其中 `10` 个模块同时“在 session 内”且“缺失于 `ModuleSummary.csv`”，分别是 `CoreOnline`、`CoreUObject`、`DeveloperSettings`、`EditorSubsystem`、`InputCore`、`JsonUtilities`、`NetCore`、`PhysicsCore`、`Slate`、`SlateCore`。这部分才是已验证的“零产出被 summary 吞掉”样本；其余 `8` 个模块则是支持集合存在、但本次 session 本身未载入。
- 当前自动化只校验“summary 内部数字自洽”和 “module csv 行数 == summary.modules.Num()”，并未校验 `modules` 数组是否覆盖整个支持模块集合：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:459-666`。这意味着“支持模块整批掉成零产出”仍可通过现有测试。

---

## 分析 (2026-04-08 11:18)

### 发现 37：`Entries.csv` 的 `ShardIndex` 比真实 shard 文件编号整体偏移 `+1`，诊断列无法直接定位到生成文件

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:120-135`; `AngelscriptGeneratedFunctionTableTests.cpp:629-665` |
| 描述 | generator 写 shard 文件时使用零基文件编号 `AS_FunctionTable_<Module>_<shardIndex:D3>.cpp`，但写 `AS_FunctionTable_Entries.csv` 时却把同一值改成了 `shardIndex + 1`。结果是 CSV 中的 `ShardIndex` 不能直接映射回真实文件名；按照列值去找 `_001.cpp`、`_002.cpp` 会系统性偏到下一片。 |
| 根因 | 代码同时维护了“文件系统编号”和“展示编号”两套 shard 编号，却只在 CSV 中保留了后者，没有输出真实文件名，也没有在 schema 或测试里声明这是 1-based ordinal。 |
| 影响 | 当开发者或自动化脚本想根据 `Entries.csv` 追到具体 `AS_FunctionTable_*.cpp` 时，会稳定打开错误 shard，降低错误诊断效率；多 shard 模块越大，这个错位越容易误导定位。 |

证据补充：

- `GenerateModule()` 生成文件路径时使用 `factory.MakePath($"AS_FunctionTable_{module.ShortName}_{shardIndex:D3}", ".cpp")`，但同一循环里写 CSV 时把列值设成了 `shardIndex + 1`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:120-135`。
- 当前产物里，`AActor::ActorHasTag` 的 CSV 行写成 `ShardIndex=1`，但真实注册代码位于 `AS_FunctionTable_Engine_000.cpp`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:2` 与 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_000.cpp:223`。
- 同样地，`APawn::AddControllerPitchInput` 的 CSV 行写成 `ShardIndex=2`，而真实代码位于 `AS_FunctionTable_Engine_001.cpp`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:258` 与 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_001.cpp:223`。
- 现有 `CsvOutputTest` 只校验 CSV header、总行数和单条样本是否为 `Direct`，没有任何“`ShardIndex` 必须能定位回真实 shard 文件”的断言：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:629-665`。

### 发现 38：flag 判定大量依赖 `Enum.ToString().Contains(...)`，把 UHTTool 的 UE5.x 兼容边界绑在枚举文本输出上

| 项目 | 内容 |
|------|------|
| 维度 | B / E |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtFunction.cs`, `../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtProperty.cs`, `../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/UhtFunctionParser.cs` |
| 行号 | `AngelscriptFunctionSignatureBuilder.cs:95-97, 130-133`; `AngelscriptFunctionTableCodeGenerator.cs:514`; `AngelscriptHeaderSignatureResolver.cs:63-64, 485, 502`; `UhtFunction.cs:160-182, 836-857`; `UhtProperty.cs:131-153, 1297-1305`; `UhtFunctionParser.cs:370-385` |
| 描述 | UHTTool 当前用 `FunctionFlags.ToString().Contains("Static"/"Const")`、`FunctionExportFlags.ToString().Contains("CustomThunk")` 这类字符串匹配来决定函数是否 `Static`、`Const`、`CustomThunk`。`EpicGames.UHT` 自身却已经公开并大量使用 `HasAnyFlags()` / `HasAllFlags()` 这类 typed flag API。也就是说，插件没有站在 UHT 的位标志契约上，而是站在 `Enum.ToString()` 的文本表现上。 |
| 根因 | 实现层为了快速取 flag，复用了字符串搜索 helper，而没有消费 `EpicGames.UHT` 已提供的 typed flag 扩展与既有用法。 |
| 影响 | 已验证事实：这会在全量导出过程中反复把枚举转成字符串；当前这轮产物里已经生成 `6043` 条 `Entries.csv` 记录和 `3886` 条 `SkippedEntries.csv` 记录，说明这些判定处在数千函数规模的热路径上。推断：一旦 UE5.x 调整 flag 命名、组合文本或 `ToString()` 表现，UHTTool 的过滤与签名判定会静默漂移，且编译期不会给出任何 API 失配信号。 |

证据补充：

- 当前 UHTTool 一共有 6 处 `ToString().Contains(...)` flag 判定落点，分别覆盖 `Static`、`Const` 和 `CustomThunk`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:132`; `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:514`; `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:63-64, 485, 502`。
- `EpicGames.UHT` 已明确提供 typed flag 扩展：`UhtFunctionExportFlagsExtensions.HasAnyFlags()` 位于 `UhtFunction.cs:160-182`，`UhtPropertyExportFlagsExtensions.HasAnyFlags()` 位于 `UhtProperty.cs:131-153`。
- UHT 自己的解析/验证代码也在按 typed flag 使用这些 API，而不是比较枚举字符串，例如 `UhtFunctionParser` 对 `CustomThunk`、`Net`、`BlueprintEvent` 的判断：`../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/UhtFunctionParser.cs:370-385`；`UhtFunction` 验证逻辑对 `FunctionFlags` / `FunctionExportFlags` 的判断：`../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtFunction.cs:836-857`；`UhtProperty` 对 `ConstParm` / `OutParm` / `ReferenceParm` 的判断：`../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtProperty.cs:1297-1305`。
- 当前这轮生成产物里，`AS_FunctionTable_Entries.csv` 有 `6043` 条记录，`AS_FunctionTable_SkippedEntries.csv` 有 `3886` 条记录。这是我基于仓库内实际导出文件执行的统计结果，说明这些字符串化 flag 判定已经运行在数千函数规模上，而不是只在少量特例路径里出现。

---

## 分析 (2026-04-08 11:32)

### 发现 39：单候选快路径只校验“同名且 public”，当前 `3230` 条 `ERASE_AUTO_*` entry 没有经过签名一致性验证

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:49-67, 70-105`; `AngelscriptFunctionSignatureBuilder.cs:8-38`; `AngelscriptGeneratedFunctionTableTests.cpp:647-665, 752-776` |
| 描述 | 当 `TryBuild()` 只找到 `1` 个 public candidate 时，它不会调用 `TryParseDeclaration()`，也不会把 header 声明的参数/返回值与 UHT AST 做任何比对，而是直接构造 `UseExplicitSignature = false` 的 `AngelscriptFunctionSignature`。随后 `BuildEraseMacro()` 会把这类条目生成为 `ERASE_AUTO_METHOD_PTR/ERASE_AUTO_FUNCTION_PTR`，summary/csv 仍统一把它们记成普通 `Direct`。 |
| 根因 | 解析链路把“单候选”当成可直接信任的快路径，选择把正确性校验交给 C++ 模板推导；真正做参数/返回值匹配的逻辑只在 overload 分支才启用。 |
| 影响 | 当前大多数 direct entry 不会在 UHT 阶段暴露“header 声明已经与 UHT 认知漂移”的问题，而是继续以 `Direct` 身份进入产物；一旦 UE5.x 改动签名细节、宏展开或声明形态，回归会被后移到 C++ 编译或运行期绑定，`SkippedEntries.csv` 与现有自动化都给不出早期告警。 |

证据补充：

- 单候选分支只检查 `HasLinkableExport()`，然后直接返回 `ReturnType=""`、`ParameterTypes=Array.Empty<string>()`、`UseExplicitSignature=false` 的签名对象；只有进入 `exactMatches` 的 overload 分支才会构造 `expectedParameterTypes/expectedReturnType` 并调用 `TryParseDeclaration()`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:49-67, 70-105`。
- `AngelscriptFunctionSignature.BuildEraseMacro()` 在 `UseExplicitSignature == false` 时固定输出 `ERASE_AUTO_METHOD_PTR/ERASE_AUTO_FUNCTION_PTR`，不会把任何显式参数包或返回类型写进生成结果：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:17-38`。
- 我对当前 `AS_FunctionTable_Entries.csv` 做了实际统计，`6043` 条 entry 里有 `3230` 条使用 `ERASE_AUTO_*`。这些 auto entry 不是只覆盖简单 getter；例如 `AActor::GetActorBounds`、`GetActorEyesViewPoint`、`GetAttachedActors` 都被记成 `Direct + ERASE_AUTO_METHOD_PTR(...)`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:13, 15, 27`。
- 现有自动化只验证“direct/stub 分类”和个别样本行存在，不验证 auto path 是否仍与 header/UHT 签名保持一致；`CsvOutputTest` 与 `MacroQualifiedDirectBindingsTest` 甚至明确接受 `ERASE_AUTO_*` 或 `ERASE_METHOD_PTR` 任一形式：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:647-665, 752-776`。

### 发现 40：生成产物测试直接信任 `Intermediate` 目录，当前工作区已经出现“源码要求新 CSV，但本目录没有产出”的漂移

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptFunctionTableExporter.cs:43-44`; `AngelscriptGeneratedFunctionTableTests.cpp:132-147, 706-749` |
| 描述 | exporter 源码现在明确会写 `AS_FunctionTable_SkippedReasonSummary.csv`，而对应测试也把这个文件视为必需产物；但测试实现只是从 `Plugins/Angelscript/Intermediate/.../UHT` 直接读现成文件，不会触发 UHT 导出，也不会校验这些 sidecar 是否与当前源码同一轮生成。当前工作区已经出现漂移：测试目标目录里没有 `AS_FunctionTable_SkippedReasonSummary.csv`，只有另一个 `.worktrees/main-merge-final` 工作树里残留着同名文件。 |
| 根因 | 测试把 `Intermediate` 目录当成可信事实来源，却没有“先重新生成 UHT 输出”或“至少比较源码/产物时间戳”的 freshness guard；sidecar 产物是否存在，取决于本地历史构建状态，而不是当前 `AngelscriptFunctionTableExporter.cs`。 |
| 影响 | 新增或修改诊断产物时，自动化结果会被本地/CI 的中间目录状态污染：有的环境会因为旧产物缺失而误报失败，有的环境又会因为残留旧文件而掩盖当前 exporter 已经退化。对增量构建和诊断文件回归的测试可信度都会被削弱。 |

证据补充：

- exporter 当前源码无条件调用 `WriteSkippedEntriesCsv(factory, skippedEntries)` 与 `WriteSkippedReasonSummaryCsv(factory, skippedEntries)`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:43-44`。
- `FAngelscriptGeneratedFunctionTableSkippedReasonSummaryCsvOutputTest` 直接拼接 `Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedReasonSummary.csv` 路径，并用 `LoadNonEmptyFileLines()` 读取；测试本身不触发任何 UHT 导出动作，只要求该文件已经存在：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:132-147, 706-749`。
- 我在当前工作区执行 `Test-Path Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedReasonSummary.csv` 得到 `False`；递归搜索同名文件时，唯一命中是 `.worktrees/main-merge-final/Plugins/Angelscript/Intermediate/.../AS_FunctionTable_SkippedReasonSummary.csv`，而不是当前工作区的 `Intermediate` 目录。
- 当前源码与现有 sidecar 的时间戳也已分叉：`AngelscriptFunctionTableExporter.cs` 最后修改时间是 `2026-04-08 10:39:32`，当前工作区里的 `AS_FunctionTable_SkippedEntries.csv` 最后修改时间是 `2026-04-08 01:05:23`。这说明测试正在消费早于当前 exporter 源码的旧产物，而不是与源码同步的一轮导出结果。 

---

## 分析 (2026-04-08 11:47)

### 发现 41：`Direct` 统计只看是否拿到函数指针，不看函数体是否已经退化成业务桩实现，当前报表把 `return nullptr` 的 API 记成成功覆盖

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | `AngelscriptAbilityAsyncLibrary.h:59-67`; `AngelscriptFunctionTableCodeGenerator.cs:98-135, 166-205` |
| 描述 | `UAngelscriptAbilityAsyncLibrary::WaitGameplayTagQueryOnActor()` 在源码里已经被硬编码成 `return nullptr;`，真实异步逻辑还停留在注释掉的调用上；但 UHTTool 只要能为它构造 `ERASE_*` 宏，就把它计入 `DirectBindEntries`，并在 `AS_FunctionTable_Entries.csv` / `AS_FunctionTable_Summary.json` 中当成成功覆盖。 |
| 根因 | 代码生成器的健康度模型只区分“是否生成出 `ERASE_NO_FUNCTION()`”，没有检查 BlueprintCallable 函数体是否已经退化成显式桩实现、占位返回或已知未完成路径。 |
| 影响 | 当前 direct coverage 会被真实不可用的 API 污染。开发者如果只看 UHT 报表，会误以为 `WaitGameplayTagQueryOnActor` 已进入可靠可调用集合；一旦以后继续把更多 BlueprintCallable 先以占位实现落地，summary/csv 会继续把“空实现”统计成成功生成，削弱诊断价值。 |

证据补充：

- 真实源码里该函数直接注释掉原始调用并返回 `nullptr`：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h:59-67`。
- 生成器统计 direct/stub 时只检查 `entry.EraseMacro == "ERASE_NO_FUNCTION()"`；任何能生成出 `ERASE_AUTO_*` / `ERASE_*` 的条目都会进入 `directBindEntries`，随后写入 summary/json/csv：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:98-135, 166-205`。
- 当前产物已把该函数写成 `Direct,ERASE_AUTO_FUNCTION_PTR(...)`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:5650`。
- 我对 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg 'WaitGameplayTagQueryOnActor'` 无命中，说明现有自动化没有针对这个已知占位实现建立任何回归约束。 

### 发现 42：summary 自动化把 UHT 诊断模型锁死成 `Direct/Stub` 两态，后续一旦修正为真实三态统计，测试会反向拦截修复

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | `AngelscriptGeneratedFunctionTableTests.cpp:369-426, 518-583`; `AngelscriptFunctionTableCodeGenerator.cs:166-236` |
| 描述 | runtime 统计测试已经明确承认 `Direct`、`ReflectiveFallback`、`Unresolved` 三种最终状态；但 `SummaryOutputTest` 与 `CsvOutputTest` 仍强制要求 `totalGeneratedEntries == totalDirectBindEntries + totalStubEntries`，并要求 `directBindRate + stubRate == 1.0`。这意味着只要 UHTTool 把 summary/csv 修正成能表达反射兜底或其他第三状态，现有测试就会首先失败。 |
| 根因 | 诊断产物 schema 和测试断言都沿用了最初的二元模型，没有跟 runtime 已存在的三态绑定结果一起演进。 |
| 影响 | 当前自动化不仅没能发现 summary 模型失真，反而把这个失真固化成“必须通过”的契约。后续要修正 `Summary.json` / `ModuleSummary.csv` 的语义时，工程师必须先改测试才能落地真实统计，增加了修复成本，也削弱了测试对架构回归的指示意义。 |

证据补充：

- runtime 统计测试显式分开统计 `DirectCount`、`ReflectiveCount`、`UnresolvedCount`，并要求 `ReflectiveCount > 0` 与 `UnresolvedCount > 0` 同时成立：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:369-426`。
- 但 summary 测试随后又把模型压回两态，硬性断言 `TotalGeneratedEntries == TotalDirectBindEntries + TotalStubEntries`，并要求 `DirectBindRate + StubRate == 1.0`；模块级断言也使用同一套二元约束：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:518-583`。
- 对应的 UHT 产物 schema 目前确实只输出 `totalDirectBindEntries` / `totalStubEntries` 与 `directBindRate` / `stubRate`，没有任何第三状态字段：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:166-236`。

### 发现 43：skipped CSV 测试没有按 CSV 规则解析，任何带逗号或引号的详细 `FailureReason` 都会被误判成坏行

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` |
| 行号 | `AngelscriptGeneratedFunctionTableTests.cpp:690-735`; `AngelscriptFunctionTableExporter.cs:129-166` |
| 描述 | exporter 写 `AS_FunctionTable_SkippedEntries.csv` 和 `AS_FunctionTable_SkippedReasonSummary.csv` 时已经实现了标准 CSV escaping，会在字段包含逗号、引号或换行时自动加引号并转义；但对应测试仍使用 `ParseIntoArray(..., TEXT(\",\"), false)` 直接按逗号切列，并硬断言“每行必须正好 4 列 / 2 列”。只要后续把 `FailureReason` 提升成更详细的文本，这两条测试就会先于功能本身报错。 |
| 根因 | 测试把当前样本数据里“failure reason 恰好都是单词 token”当成了格式契约，没有复用 exporter 自己定义的 CSV 语义。 |
| 影响 | 这会直接阻碍错误诊断质量改进。只要把 skipped reason 扩展成更有信息量的文本，哪怕输出仍是合法 CSV，自动化也会误报失败，迫使开发者为了保测试而继续维持过于粗糙的 failure reason。 |

证据补充：

- exporter 在写两份 skipped CSV 时都会对字段调用 `EscapeCsv()`；该函数显式处理逗号、引号和换行：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:129-166`。
- 但测试读取 `SkippedEntries.csv` 与 `SkippedReasonSummary.csv` 时都直接用 `ParseIntoArray(Columns, TEXT(\",\"), false)` 拆列，再断言列数固定为 `4` 或 `2`：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:690-735`。
- 这不是理论上的 writer/reader 风格不一致。当前 exporter 之所以实现 `EscapeCsv()`，正说明字段内容本来就允许超出“无逗号 token”这一窄假设；一旦把 `FailureReason` 从现有单词标签扩展成更细粒度文本，现有测试解析方式就会与 exporter 自身契约冲突。 

---

## 分析 (2026-04-08 11:56)

### 发现 44：`FindCandidates()` 会把 inline wrapper 函数体里的同名转发调用当成第二个候选声明，批量制造伪 `overloaded-unresolved`

| 项目 | 内容 |
|------|------|
| 维度 | A / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs` |
| 行号 | 362-385 |
| 描述 | `FindCandidates()` 在整个 class body 上直接搜索 `FunctionName + "("`，没有过滤 inline 函数体、表达式语句或 brace depth。结果是 wrapper 函数体里对底层 API 的同名转发调用，也会被当成第二个“声明候选”。当转发调用与真实声明参数个数一致时，`TryBuild()` 会得到两个 `exactMatches`，最后把并不存在 overload 冲突的函数误报为 `overloaded-unresolved`，并在生成表里降级成 `ERASE_NO_FUNCTION()`。 |
| 根因 | header resolver 采用文本级全量扫描，而不是“只在 declaration 区域找候选”；`FindDeclarationStart()` / `FindDeclarationEnd()` 也没有利用 brace depth 排除函数体内部语句。 |
| 影响 | 当前仓库里已经有一批本来可以 direct bind 的 inline wrapper 被系统性打成 stub，直接降低生成正确性。更糟的是，部分场景又被手写 bind 覆盖或运行时已有条目掩盖，导致自动化看起来通过，但 UHT 产物本身已经退化。 |

证据补充：

- `FindCandidates()` 只靠 `header.IndexOf(functionName + "(")` 在整个 class body 里找文本命中，然后把命中区间交给 `FindDeclarationStart()` / `FindDeclarationEnd()`；这套逻辑没有任何“当前是否位于函数体内部”的判断：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:362-385`。
- `TryBuild()` 会把所有 public candidate 都送进 `TryParseDeclaration()`，只要 `exactMatches.Count != 1` 就统一落成 `overloaded-unresolved`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:70-106`。
- 当前源码里存在多个会触发该误判的真实样本，而且它们都在函数体里出现了同名转发调用：
  - `UAngelscriptAbilityAsyncLibrary::WaitForAttributeChanged()` 的函数体直接调用 `UAbilityAsync_WaitAttributeChanged::WaitForAttributeChanged(...)`：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h:18-26`。
  - `UGameplayTagQueryMixinLibrary::GetDescription()` 的函数体直接调用 `GameplayTagQuery.GetDescription()`：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h:19-38`。
  - `UAngelscriptFQuatLibrary::MakeFromX/MakeFromXY/...` 的函数体直接调用 `FRotationMatrix::MakeFromX/MakeFromXY/...`：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h:631-692`。
- 这些函数在当前导出产物里都已被误打成 `overloaded-unresolved + Stub`，不是理论风险：
  - `WaitForAttributeChanged`：`AS_FunctionTable_SkippedEntries.csv:73` 与 `AS_FunctionTable_Entries.csv:5647`。
  - `UGameplayTagQueryMixinLibrary::GetDescription`：`AS_FunctionTable_SkippedEntries.csv:213` 与 `AS_FunctionTable_Entries.csv:5977`。
  - `UAngelscriptFQuatLibrary::MakeFromX`：`AS_FunctionTable_SkippedEntries.csv:145` 与 `AS_FunctionTable_Entries.csv:5823`。
- 我对当前 `AS_FunctionTable_SkippedEntries.csv` 做了实际统计，`AngelscriptRuntime` 模块里共有 `103` 条 `overloaded-unresolved`；其中 `UAngelscriptComponentLibrary` 就有 `17` 条，`UAngelscriptFQuatLibrary` 有 `9` 条，`UAngelscriptFQuat4fLibrary` 有 `9` 条，`UGameplayTagQueryMixinLibrary` 有 `3` 条，已经表现出明显的批量模式，而不是零散个例。
- 现有自动化没有把这类 UHT 误降级拦下来。相反，`WaitForAttributeChanged` 的测试只验证运行时最终还能从 `ClassFuncMaps` 里拿到手写 direct bind 指针，等于允许 UHT 产物继续保持 `Stub`：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:206-226, 431-455` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_AngelscriptGASLibrary.cpp:4-14`。

### 发现 45：exporter 完全没有使用 UHT 的 task API，当前导出在数千函数规模上仍串行做两轮 header 解析

| 项目 | 内容 |
|------|------|
| 维度 | B / E |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | `AngelscriptFunctionTableExporter.cs:27-44`; `AngelscriptFunctionTableCodeGenerator.cs:51-79, 449-487` |
| 描述 | 当前 exporter 入口是纯串行实现：先执行 `Generate(factory)` 完整走一轮模块遍历和 `TryBuild()`，然后立刻再遍历一次 `factory.Session.Modules` 调 `CountBlueprintCallableFunctions()`，第二轮再次调用 `AngelscriptFunctionSignatureBuilder.TryBuild()` 收集 skipped 诊断。整个过程既没有利用 `IUhtExportFactory.CreateTask()`，也没有像 UE5 自带 exporter 那样按 module/header 分片并发。 |
| 根因 | UHTTool 把“生成 shard”和“产出诊断”都堆在单个同步导出函数里实现，没有接入 UE5.x UHT 已提供的 task 调度接口。 |
| 影响 | 在当前仓库规模下，这条路径已经不是小开销：现有产物里有 `6043` 条 generated entry 和 `3886` 条 skipped row，说明导出要在数千函数量级上重复做 header 解析与签名重建。随着支持模块继续增加，UHTTool 会比同一轮 UHT 里的其他 exporter 更容易成为串行瓶颈，拉长全量导出时间。 |

证据补充：

- 插件 exporter 的主入口只做同步调用：先 `AngelscriptFunctionTableCodeGenerator.Generate(factory)`，再同步 `foreach (UhtModule module in factory.Session.Modules)` 做第二轮 `CountBlueprintCallableFunctions()`，没有任何 task 创建：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:27-44`。
- 第一轮生成内部同样是同步模块遍历，`CollectEntries()` 递归到每个函数后直接调用 `AngelscriptFunctionSignatureBuilder.TryBuild()`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-79, 449-487`。
- UE5.x UHT 的官方 exporter API 已明确暴露 `IUhtExportFactory.CreateTask(...)` 用于并发导出：`../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtExport.cs:21-47`。
- UE 自带 exporter 也确实在使用这条能力，而不是串行硬跑：
  - `UhtJsonExporter` 按 module 调 `Factory.CreateTask(...)`，最后 `Task.WaitAll(...)`：`../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Exporters/Json/UhtJsonExporter.cs:31-55`。
  - `UhtCodeGenerator` 在 `Session.GoWide` 时对模块初始化做 `Parallel.ForEach`，随后按 header/module 建 task 并 `Task.WaitAll(...)`：`../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Exporters/CodeGen/UhtCodeGenerator.cs:155-241`。
- 我对当前工作区里的实际导出产物做了统计，`AS_FunctionTable_Entries.csv` 有 `6043` 条记录，`AS_FunctionTable_SkippedEntries.csv` 有 `3886` 条记录。这意味着当前 exporter 不是在处理几十个样本，而是在一个已经具备并行价值的规模上继续走单线程双遍扫描。

### 发现 46：access control 已经被 UHT AST 解析成 `FunctionFlags`，resolver 仍回退到脆弱的 header 文本扫描

| 项目 | 内容 |
|------|------|
| 维度 | B / C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/UhtClassParser.cs`, `../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/UhtFunctionParser.cs` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:438-459`; `UhtClassParser.cs:36-45`; `UhtFunctionParser.cs:324-344` |
| 描述 | UHT 在解析阶段已经把函数访问级别写进 `UhtFunction.FunctionFlags`，并且对 legacy `GENERATED_UCLASS_BODY()` 还专门记录了 `GeneratedBodyAccessSpecifier`。但 `AngelscriptHeaderSignatureResolver` 完全不消费这些 AST 结果，而是重新在 sanitized header 文本里顺序扫描字面量 `public:` / `protected:` / `private:` 来决定 `candidate.IsPublic`。这让工具主动绕开了 UE5.x UHT 已经提供的稳定语义层，把访问控制重新降级成一套脆弱的字符串规则。 |
| 根因 | 实现层没有把 UHT AST 当成访问控制的权威来源，而是继续使用自写 header parser 承担 `public/protected/private` 语义判定。 |
| 影响 | 当前导出里 `non-public` 已经是最大的失败类别，共 `2359` 条。也就是说，大量生成正确性和诊断结果都建立在这套文本扫描上，而不是建立在 UHT 已验证的访问标志上。只要源码使用 legacy generated-body、宏调整 access、或更复杂的 class 布局，这条链路就会继续制造误报。 |

证据补充：

- UHT 在 class 解析时就保存了 generated-body 的 access 语义；遇到 legacy body 宏时，`topScope.AccessSpecifier` 会被切到 `UhtAccessSpecifier.Public`，并存入 `GeneratedBodyAccessSpecifier`：`../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/UhtClassParser.cs:36-45`。
- UHT 在函数解析时也会把当前访问级别直接编码进 `function.FunctionFlags`，分别设置 `EFunctionFlags.Public / Protected / Private`：`../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/UhtFunctionParser.cs:324-344`。
- 但 resolver 当前并不读这些 flags，而是自己从 header 文本重新跑 `FindAccessSpecifier()`；其实现只是从 `classBodyStart` 顺序扫描裸字符串 `public:` / `protected:` / `private:`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:438-459`。
- 这不是冷门路径。我对当前 `AS_FunctionTable_SkippedEntries.csv` 做了实际统计，`non-public` 有 `2359` 条，已经是第一大失败原因，明显说明“访问控制判定”是当前 UHTTool 的主导故障面之一，而不是边缘分支。
- 推断：只要后续继续沿用文本扫描，而不是直接消费 `UhtFunction.FunctionFlags` / `GeneratedBodyAccessSpecifier`，前面文档里已经出现的 `GENERATED_UCLASS_BODY()`、`BlueprintProtected`、inline body 干扰等问题都会持续以不同变体重复出现。这里的推断依据是 UHT 已经提供了更稳定的语义源，而 resolver 仍绕过它。 

---

## 分析 (2026-04-08 12:06)

### 发现 47：editor-only 过滤只做到模块级，运行时模块里的 `#if WITH_EDITOR` BlueprintCallable 会被错误写进普通 shard

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptLevelStreamingLibrary.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameplayCueUtils.h`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:282-331, 334-384, 449-515`; `AngelscriptLevelStreamingLibrary.h:13-19`; `AngelscriptGameplayCueUtils.h:84-122`; `AngelscriptGeneratedFunctionTableTests.cpp:242-266` |
| 描述 | UHTTool 当前只通过 `LoadSupportedModules()` 识别“整个模块是否 editor-only”，然后在 `BuildShard()` 里决定整份 `.cpp` 是否包 `#if WITH_EDITOR`。但 `ShouldGenerate()` 和 `CollectEntries()` 完全不检查 `FUNC_EditorOnly`、`EditorOnly` metadata 或 header 中的 `#if WITH_EDITOR` 区域。结果是运行时模块 `AngelscriptRuntime` 中那些只在 editor 编译的 BlueprintCallable，仍会被写进 `EditorOnly=false` 的普通 shard。 |
| 根因 | editor-only 语义被错误地建模成“模块属性”，而不是“函数/类属性”；生成器没有消费 UHT 已解析的函数级 editor-only 信息。 |
| 影响 | 生成结果已经把 `UAngelscriptGameplayCueUtils::FindCueLoadedClassInEditor()` 和 `UAngelscriptLevelStreamingLibrary::GetShouldBeVisibleInEditor()` 写进未加 `#if WITH_EDITOR` 的 `AS_FunctionTable_AngelscriptRuntime_001.cpp`。这会让普通运行时 shard 在非编辑器构建里直接引用 editor-only 符号，边界正确性依赖于构建环境的偶然兼容；同时 `Entries.csv` 还把它们标成 `EditorOnly=false`，诊断信息也会误导排查。 |

证据补充：

- `BuildShard()` 只有在 `editorOnly` 为真时才给整份 shard 加 `#if WITH_EDITOR`，而这个布尔值完全来自 `LoadSupportedModules()` 对 `AngelscriptRuntime.Build.cs` editor block 的模块级推断：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:282-331, 334-384`。
- `CollectEntries()` / `ShouldGenerate()` 只检查 `BlueprintCallable/Pure`、`NotInAngelscript`、`BlueprintInternalUseOnly`、`CustomThunk` 等条件，没有任何 `EditorOnly` / `FUNC_EditorOnly` 过滤：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:449-515`。
- 真实源码里这两个函数都明确位于 `#if WITH_EDITOR` 块内：
  - `UAngelscriptLevelStreamingLibrary::GetShouldBeVisibleInEditor()`：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptLevelStreamingLibrary.h:13-19`。
  - `UAngelscriptGameplayCueUtils::FindCueLoadedClassInEditor()`：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameplayCueUtils.h:84-122`。
- 当前生成产物已经把它们写进未加 `#if WITH_EDITOR` 的 runtime shard：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_AngelscriptRuntime_001.cpp:1-64`。其中 `FindCueLoadedClassInEditor` 位于第 `48` 行，`GetShouldBeVisibleInEditor` 位于第 `64` 行。
- `AS_FunctionTable_Entries.csv` 也已把这两个条目标成 `ModuleName=AngelscriptRuntime, EditorOnly=false`，说明诊断层同样丢失了函数级 editor-only 语义。
- 现有自动化只验证“`UMGEditor` 模块输出要包 `#if WITH_EDITOR`、`Engine` 模块输出不要包”，没有覆盖“运行时模块内部混有 editor-only 函数”的场景：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:242-266`。

### 发现 48：`SkippedEntries.csv` 不是 `Stub` 的原因集，当前至少有 160 个 `ERASE_NO_FUNCTION()` 条目完全没有对应失败诊断

| 项目 | 内容 |
|------|------|
| 维度 | A / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Animation/AnimData/IAnimationDataController.h` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:465-479`; `AngelscriptFunctionTableExporter.cs:65-97`; `BlueprintCallableReflectiveFallback.cpp:261-282`; `IAnimationDataController.h:43-120` |
| 描述 | 生成器把一部分函数直接写成 `ERASE_NO_FUNCTION()`，但 exporter 的 skipped 诊断只在 `AngelscriptFunctionSignatureBuilder.TryBuild()` 返回失败时记录一行。结果是“最终产物是 stub”与“CSV 里能看到失败原因”并不是一一对应关系。当前工作区里共有 `2649` 条 `Stub` entry，其中至少 `160` 条在 `AS_FunctionTable_SkippedEntries.csv` 里完全没有对应记录。 |
| 根因 | 代码生成链路和诊断链路使用了不同的状态机：生成链路把 `Interface/NativeInterface` 等情况在 `CollectEntries()` 里直接降级为 `ERASE_NO_FUNCTION()`；诊断链路却只观察签名构建是否失败，没有复用生成阶段的强制 stub 分支。 |
| 影响 | 当开发者追查 `Entries.csv` / `Summary.json` 里的 stub 覆盖率时，会遇到一批“确认失败但没有原因”的函数，必须回头人工读源码猜测是 interface、reflective fallback 不可用，还是别的硬限制。现有 skipped 诊断因此无法解释全部生成失败面，错误诊断质量被系统性削弱。 |

证据补充：

- `CollectEntries()` 在 `classObj.ClassType == UhtClassType.Interface || classObj.ClassType == UhtClassType.NativeInterface` 时直接把条目写成 `ERASE_NO_FUNCTION()`，不会给这些条目附带任何 reason：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:465-479`。
- exporter 侧的 `CountBlueprintCallableFunctions()` 只在 `TryBuild(...) == false` 时才写 `skippedEntries`；它没有任何“如果最终会生成 stub 也记录失败原因”的逻辑：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:65-97`。
- 运行时 reflective fallback 也明确拒绝 interface class，说明这类 stub 不是“暂时没直绑、以后还能自动兜底”的普通情况，而是一个确定性的拒绝类别：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:261-282`。
- 真实样本里，`IAnimationDataController` 把大量方法声明成 `UFUNCTION(BlueprintCallable)`，其对应 `UINTERFACE` 是 `UAnimationDataController`：`../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Animation/AnimData/IAnimationDataController.h:43-120`。
- 当前导出产物已经把这一整类接口函数写成没有 reason 的 stub，例如 `AS_FunctionTable_Entries.csv` 第 `573-582` 行是：
  - `Engine,false,UAnimationDataController,AddAttribute,Stub,ERASE_NO_FUNCTION(),3`
  - `Engine,false,UAnimationDataController,AddBoneCurve,Stub,ERASE_NO_FUNCTION(),3`
  - `Engine,false,UAnimationDataController,GetModelInterface,Stub,ERASE_NO_FUNCTION(),3`
  但 `AS_FunctionTable_SkippedEntries.csv` 里对 `UAnimationDataController` / `UAnimationDataModel` 的检索结果为 `0` 行。
- 我对当前产物做了实际比对：`2649` 条 stub 中有 `160` 条没有任何对应 skipped 记录；按模块看，`Engine` 就有 `101` 条，`AssetRegistry` 有 `28` 条，`EnhancedInput` 有 `26` 条，说明这不是孤例。

---

## 分析 (2026-04-08 12:27)

### 发现 49：header resolver 只缓存原始文本，不缓存 class range / candidate 结果，当前已经在大头文件上形成按函数数重复全表扫描的 O(n²) 热点

| 项目 | 内容 |
|------|------|
| 维度 | E / B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:18-35, 180-250, 253-293, 362-390`; `AngelscriptFunctionTableExporter.cs:27-44`; `AngelscriptFunctionTableCodeGenerator.cs:51-79, 449-487` |
| 描述 | resolver 现在唯一的缓存是 `SanitizedHeaderCache`，只避免重复 `File.ReadAllText()`。但每次 `TryBuild()` 仍然会重新做 `TryFindClassBody()` 的整头扫描，再在 class body 上重新跑 `FindCandidates()`。而这个过程会在生成阶段和 exporter 统计阶段各跑一遍。对于 BlueprintCallable 极多的大头文件，这已经形成稳定的按“函数数 × 头文件大小 × 2 遍”重复扫描。 |
| 根因 | 实现只缓存了“头文件字符串”，没有把 class body range、函数候选区间或按类分组的解析结果抽象出来复用；同时 exporter 第二遍诊断再次重复调用同一套解析逻辑。 |
| 影响 | 当前规模下这已经是可量化的导出热点。`UKismetMathLibrary.h` 单文件就有 `1472` 条 generated+skipped 记录，header 大小 `256,265` bytes；按现有双遍流程估算，仅这个类对应的 resolver 文本扫描量就约 `719.49 MiB`。随着支持模块继续扩大，UHTTool 的时间开销会更明显地被少数巨型 Blueprint library 放大。 |

证据补充：

- `TryBuild()` 每次调用都会先取 sanitized header，然后立刻重新执行 `TryFindClassBody()` 与 `FindCandidates()`，这两步都没有任何 class 级缓存：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:18-35, 180-250, 253-293, 362-390`。
- 生成阶段会在 `CollectEntries()` 中对每个候选函数调用一次 `AngelscriptFunctionSignatureBuilder.TryBuild()`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-79, 449-487`。
- exporter 诊断阶段又会在 `CountBlueprintCallableFunctions()` 中对同一批 `UFunction` 再调用一遍 `TryBuild()`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:27-44, 57-88`。
- 当前产物里 `UKismetMathLibrary` 已有 `734` 条 generated entry 和 `738` 条 skipped row，总计 `1472` 条；对应头文件 `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetMathLibrary.h` 长度为 `256,265` bytes、`4583` 行。我按“每遍每函数至少重扫一遍 class/header 文本”做实际估算，双遍累计扫描量约 `719.49 MiB`。
- 次一级热点也已经不小：`UKismetSystemLibrary` 在当前产物中有 `551` 条 generated+skipped 记录，其头文件大小 `150,148` bytes，双遍估算扫描量约 `157.80 MiB`。这说明问题不是 `KismetMathLibrary` 的孤例，而是当前算法在“大而多函数”的 Blueprint library 上的结构性成本。 

### 发现 50：手写 `File.WriteAllText` / `File.Delete` 绕过了 UHT 的 `NoOutput` 与 reference/verify 语义，诊断运行也会真实改写输出目录

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtSession.cs` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:174-205, 218-265, 432-445`; `AngelscriptFunctionTableExporter.cs:99-160`; `UhtSession.cs:380-459, 533-550` |
| 描述 | 代码生成器和 exporter 对 summary/json/csv 一律直接 `File.WriteAllText()`，并且还手写了 `DeleteStaleOutputs()` 直接 `File.Delete()` 旧 shard。这样一来，只要 exporter 被执行，这些路径就会无条件改写真实输出目录，完全不受 UHT `CommitOutput()` 的 `NoOutput`、reference/verify、`FailIfGeneratedCodeChanges` 和统一 cull 流程保护。 |
| 根因 | 插件同时维护了“两套输出系统”：`.cpp` shard 部分沿用 UHT factory，诊断文件和 stale 清理则直接操作文件系统，没有复用 UHT export factory 的保存/验证语义。 |
| 影响 | 在 `NoOutput`、reference/verify 或其他“只比较不落盘”的 UHT 运行模式下，插件仍会改动工作区，甚至删除旧 `AS_FunctionTable_*.cpp`。这会污染验证结果、让 CI/工具链出现意外脏文件，并削弱增量构建与生成代码验证的可预测性。 |

证据补充：

- UHT 官方输出路径会先处理 reference/verify 目录，再在真正保存时显式检查 `!Session.NoOutput`；输出目录 cull 也同样受 `!Session.NoOutput` 保护：`J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtSession.cs:380-459, 533-550`。
- 当前插件只有 shard `.cpp` 走 `factory.CommitOutput(...)`；summary/json/csv 则全部直接 `File.WriteAllText(...)`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:174-205, 218-265` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:99-160`。
- stale shard 清理也不是交给 UHT 的 exporter cull，而是插件自己在导出过程中直接枚举并删除 `AS_FunctionTable_*.cpp`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:432-445`。
- 这不是语义上的小差异。UHT 自带 `SaveIfChanged()` 明确支持 reference/verify 比较和 `FailIfGeneratedCodeChanges` 冲突输出；当前 direct write/delete 路径完全绕过了这些保护，所以插件输出行为与 UE5.x exporter 约定已经分叉。 

### 发现 51：resolver 的静态 header cache 不是线程安全实现，当前代码实际上还没准备好接入 UHT 的 task 并行导出

| 项目 | 内容 |
|------|------|
| 维度 | B / E |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtExport.cs` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:14, 180-250`; `UhtExport.cs:35-47` |
| 描述 | resolver 用 `static readonly Dictionary<string, string> SanitizedHeaderCache` 缓存 header 文本，并在 `GetSanitizedHeader()` 里执行无锁的 `TryGetValue()` + `Add()`。这个实现只在当前 exporter 纯串行时勉强安全；一旦按 UE5.x 官方方式用 `IUhtExportFactory.CreateTask()` 并发按 module/header 导出，就会把最核心的共享状态暴露给多线程竞争。 |
| 根因 | 代码把 header 文本缓存做成了进程级静态字典，但没有引入任何同步机制，也没有把 cache 作用域限制到单次导出任务或单线程上下文。 |
| 影响 | 当前 UHTTool 不只是“还没并行化”，而是连并行化前提都不满足。后续如果按官方 API 接入 task 并发，最直接的结果就是 cache 竞争、重复 `Add`、乃至 `Dictionary` 并发访问异常；这会把性能改造变成新的稳定性回归点。 |

证据补充：

- `SanitizedHeaderCache` 是全局静态 `Dictionary<string, string>`，`GetSanitizedHeader()` 在 miss 时直接 `SanitizedHeaderCache.Add(headerPath, sanitizedHeader)`，全程没有 `lock`、`ConcurrentDictionary` 或其他同步：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:14, 180-250`。
- UE5.x UHT 官方 exporter API 明确提供了 `CreateTask(...)` 用于并行导出任务：`J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtExport.cs:35-47`。
- 推断：只要把当前单线程 `Generate()` / `CountBlueprintCallableFunctions()` 改成按 module 或 header 建 task，这个静态 cache 就会被多个任务共享访问。这里的推断依据是 cache 的作用域是 `static`，而 `CreateTask()` 的设计目标正是让同一 exporter 在 `Session.GoWide` 时并发执行多个导出动作。
- 这也解释了为什么“并行化支持”在当前实现里不是简单把 `foreach` 改成 task 就能完成：resolver 自身先要从共享可变状态改成线程安全或任务局部状态，否则修复 finding 45 时会引入新的崩溃面。 

---

## 分析 (2026-04-08 12:41)

### 发现 52：生成侧没有复用 runtime 的硬编码 skip 规则，`UActorComponent::GetOwner` 被写成 direct entry 但 editor 绑定阶段永远直接返回

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:490-515`; `AngelscriptBinds.cpp:83-114`; `Bind_BlueprintCallable.cpp:26-30` |
| 描述 | UHT 生成侧 `ShouldGenerate()` 只复用了 metadata/header/custom thunk 过滤，没有复用 runtime `ShouldSkipBlueprintCallableFunction()` 里的 `UActorComponent::GetOwner` 特判。当前产物已经把 `UActorComponent::GetOwner` 写进 `AS_FunctionTable_Entries.csv`，并标成 `Direct`；但 editor 路径下 `Bind_BlueprintCallable()` 在真正绑定前会先调用 `ShouldSkipBlueprintCallableFunction()`，遇到这个函数直接 `return`。 |
| 根因 | 代码生成链路与 runtime 绑定链路各自维护“应该跳过哪些 BlueprintCallable”的判定，生成侧没有共享 runtime 的硬编码例外列表。 |
| 影响 | `Summary.json` / `Entries.csv` 会把这类函数算进成功覆盖，但 editor 实际运行时不会把它们注册进脚本引擎，导致生成诊断与真实可调用面再次分叉；同时现有测试只验证 `UActorComponent` 这个类“至少有一个条目”，不会发现 `GetOwner` 这种单函数死条目。 |

证据补充：

- 生成侧 `ShouldGenerate()` 不检查 `FUNC_Native` 之外的 runtime skip 特判，也没有 `UActorComponent::GetOwner` 分支：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:490-515`。
- runtime 侧 `ShouldSkipBlueprintCallableFunction()` 明确把 `UActorComponent::GetOwner` 列为硬编码跳过项：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:83-114`。
- editor 绑定路径在查表后、真正注册前会先执行这层 skip 判定，命中后直接返回：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:26-30`。当前 `AS_USE_BIND_DB` 定义为 `(!WITH_EDITOR)`，所以这正是当前 editor/UHT 分析路径实际使用的分支：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h:17`。
- 当前导出产物已经把它写成 direct entry：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:412` 为 `Engine,false,UActorComponent,GetOwner,Direct,"ERASE_METHOD_PTR(UActorComponent, GetOwner, () const, ERASE_ARGUMENT_PACK(AActor*))",2`。
- 真实源码里 `UActorComponent::GetOwner()` 确实是 BlueprintCallable 原生函数，因此不会被前面的 metadata 过滤挡住：`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/ActorComponent.h:521-525,1534`。
- 现有生成测试对 `UActorComponent` 的断言只有“类存在且条目数 > 0”，没有校验这个 runtime skip 特例是否被错误记入 direct 覆盖：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:284-314`。

### 发现 53：UHT 自动绑定路径没有给生成函数打 `asTRAIT_GENERATED_FUNCTION`，脚本编译依赖和调试器都会把 6043 条 UHT glue 当成普通用户函数

| 项目 | 内容 |
|------|------|
| 维度 | B / D / E |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | `Bind_BlueprintCallable.cpp:61-131`; `Helper_FunctionSignature.h:414-457`; `Bind_BlueprintType.cpp:1185-1188,1246-1249,1441-1444`; `Bind_UStruct.cpp:1298-1301,1359-1362`; `as_builder.cpp:6865-6871`; `as_compiler.cpp:470-472,594-596,18200-18203`; `AngelscriptDebugServer.cpp:1442-1445,1773-1777` |
| 描述 | `Bind_BlueprintCallable()` 在 UHT 表驱动绑定成功后只调用 `Signature.ModifyScriptFunction()`，而该后处理只设置 `WorldContext`、`BlueprintProtected`、`Deprecated`、`EditorOnly` 等属性，完全没有设置 `asTRAIT_GENERATED_FUNCTION`。对比之下，`Bind_BlueprintType.cpp` 和 `Bind_UStruct.cpp` 的其它 generated accessor 路径都会显式打这个 trait。结果是 UHT 自动生成的 BlueprintCallable glue 在 Angelscript runtime 里被当成普通用户函数，而不是 generated glue。 |
| 根因 | UHT 自动绑定链路复用了一个面向“普通 BlueprintCallable 元数据修饰”的 `ModifyScriptFunction()`，但没有补上 generated bind 路径需要的 trait 标记；同仓库其它 generated bind 路径已经各自手工设置该 trait，说明这一语义本来就需要显式维护。 |
| 影响 | 这不是单纯的调试标签缺失。`as_builder.cpp` / `as_compiler.cpp` 会用 `asTRAIT_GENERATED_FUNCTION` 决定依赖是否记为 hard dependency、是否执行 editor-only type/function 检查；`AngelscriptDebugServer.cpp` 也会用它过滤 generated frame。当前 `AS_FunctionTable_Summary.json` 显示 UHT 一次会生成 `6043` 条条目、`32` 个 shard，所以这个缺口会系统性放大脚本增量编译的依赖噪声，并把大量 UHT glue 暴露进调试器可见面。 |

证据补充：

- UHT 自动绑定成功后只调用 `Signature.ModifyScriptFunction(FunctionId)`，没有任何 `SetTrait(asTRAIT_GENERATED_FUNCTION, true)`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:61-131`。
- `ModifyScriptFunction()` 当前只写 `asTRAIT_USES_WORLDCONTEXT`、`asTRAIT_DEPRECATED`、`asTRAIT_EDITOR_ONLY` 等属性，没有 generated trait：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:414-457`。
- 同仓库其它 generated bind 路径已经把这件事当成必需步骤：
  - `Bind_BlueprintType.cpp` 在多个 generated accessor 分支里显式 `ScriptFunction->traits.SetTrait(asTRAIT_GENERATED_FUNCTION, true)`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp:1185-1188,1246-1249,1441-1444`。
  - `Bind_UStruct.cpp` 也在对应 generated accessor 路径里设置同一 trait：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp:1298-1301,1359-1362`。
- Angelscript 编译器确实依赖这个 trait 改变行为：
  - `as_builder.cpp` 在 `bValueDependenciesAreHard` 模式下，对非 generated function 记 hard value dependency：`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp:6865-6871`。
  - `as_compiler.cpp` 只有在“不是 generated function”时才执行 `CheckEditorOnlyType()` / `CheckEditorOnlyFunction()`：`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp:470-472,594-596,18200-18203`。
- 调试器同样依赖这个 trait 过滤 generated glue：`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp:1442-1445,1773-1777`。
- 当前 UHT 产物规模已经足以把这个问题放大成系统性噪声，而不是边角行为：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Summary.json:1-8` 显示 `totalGeneratedEntries = 6043`、`totalShardCount = 32`。
- 我对 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 执行 `rg --line-number "GENERATED_FUNCTION" ...` 的结果为 `0` 行，说明当前没有自动化直接校验 UHT 绑定后的 generated trait 语义。

### 发现 54：UHT shard 明明已经有稳定的 `Bind_AS_FunctionTable_<Module>_<Shard>` 标识，但注册时被降成全局递增的 `UnnamedBind_n`，无法稳定地按 shard/module 做禁用与观测

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:301-309`; `AngelscriptBinds.h:442-474`; `AngelscriptBinds.cpp:121-181,198-210`; `AngelscriptBindExecutionObservation.cpp:48-60`; `AngelscriptBindConfigTests.cpp:368-410` |
| 描述 | `BuildShard()` 生成的 C++ 里已经带有稳定且可读的符号名 `Bind_AS_FunctionTable_<Module>_<Shard>`，但它调用的是 `FAngelscriptBinds::FBind(int32 BindOrder, TFunction<void()>)` 这个“无名字”构造函数。runtime 会把这类 bind 统一改写成全局自增的 `UnnamedBind_<n>`。这意味着 UHT 的 32 个 shard 在启动观测、禁用配置、状态 dump 里都失去了 module/shard 语义，而且名字会随着其它 unnamed bind 的注册顺序漂移。 |
| 根因 | 代码生成器只把稳定标识写进了 C++ 变量名，没有把它作为 `BindName` 传给 `FAngelscriptBinds::FBind`；而 runtime 的 fallback 命名策略本来只是 unnamed bind 的兼容兜底，不是为大规模 UHT shard 设计的稳定标识层。 |
| 影响 | 当某个生成 shard 在启动时崩溃、污染 `ClassFuncMaps`、或需要通过 `DisabledBindNames` 做 bisect 时，用户看到的只会是 `UnnamedBind_n`，且这个 `n` 会因其它 unnamed bind 的增减而变化，无法稳定映射回 `AS_FunctionTable_<Module>_<Shard>.cpp`。这会直接削弱 UHTTool 的错误诊断和回归定位能力。 |

证据补充：

- 代码生成器当前确实产出稳定的 shard 符号名，但调用的是无 `BindName` 参数的构造函数：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:301-309`。实际生成产物也是 `AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_AS_FunctionTable_AIModule_000((int32)FAngelscriptBinds::EOrder::Late + 50, []()`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_AIModule_000.cpp:33`。
- `FAngelscriptBinds::FBind(int32 BindOrder, ...)` 会走 `RegisterBinds(NAME_None, ...)`，runtime 再用 `MakeUnnamedBindName()` 把它重写成 `UnnamedBind_<n>`：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h:457-467` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:121-152`。
- `MakeUnnamedBindName()` 的实现是进程级单调递增计数器 `NextUnnamedBindId++`，说明名字稳定性取决于“此前已经注册过多少 unnamed bind”，而不是取决于 shard 本身：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:136-140`。
- 启动观测和禁用逻辑都只看 `BindName`：
  - `GetBindInfoList()` / `GetAllRegisteredBindNames()` 把 `BindName` 暴露给配置与状态查询：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:154-181`。
  - `CallBinds()` 与 `AngelscriptBindExecutionObservation` 也只记录 `BindName`：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:198-210` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.cpp:48-60`。
- 当前测试也承认 unnamed bind 只会拿到 `UnnamedBind_n` 这种自增名称，并没有任何“稳定映射回来源文件”的语义：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp:368-410`。
- 这不是一两个 bind 的小问题。当前 UHT summary 已经显示 `totalShardCount = 32`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Summary.json:1-8`。也就是说，当前启动路径里至少有 32 个 UHT shard 会一起落到这种不稳定的 unnamed 命名策略上。

### 发现 55：`DevelopmentOnly` Blueprint API 在 UHT 自动绑定路径里丢失 compile-out 语义，shipping 脚本面会比 Blueprint 更宽

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Logging.cpp`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:490-515`; `Bind_BlueprintCallable.cpp:73-143`; `BlueprintCallableReflectiveFallback.cpp:277-290,374-411`; `Helper_FunctionSignature.h:414-457`; `AngelscriptBinds.cpp:464-523`; `Bind_Logging.cpp:8-204`; `KismetSystemLibrary.h:129-130,546-577` |
| 描述 | UHT 生成链路和 BlueprintCallable runtime 绑定链路都没有处理 `meta=(DevelopmentOnly)`。结果是这类 Blueprint 节点虽然在 UE 里带有“开发期专用”的语义，但进入 Angelscript UHT 自动绑定后，只会被当成普通 native/stub 函数注册；既不会被 `compileOutType` 剪掉，也不会继承现有 `bForceConstWithinDevelopmentOnlyFunctions` 行为。 |
| 根因 | 当前自动绑定路径只保留了 `WorldContext`、`BlueprintProtected`、`Deprecated`、`EditorOnly`、`DeterminesOutputType` 等元数据，没有把 `DevelopmentOnly` 映射到 runtime 现有的 `CompileOutInTest` / `CompileOutIfNoLog` / `CompileOutAsCheck` 机制。 |
| 影响 | 在 shipping、test 或 simulated cooked 环境下，手写脚本 API 会按预期 compile-out，但 UHT 自动收录的 `DevelopmentOnly` Blueprint API 仍会留在脚本可调用面内。这样会让 cooked/script 行为与 Blueprint 语义分叉，最直接的受影响面就是调试、日志和 debug draw 相关函数。 |

证据补充：

- 生成侧 `ShouldGenerate()` 没有任何 `DevelopmentOnly` 过滤或标记逻辑：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:490-515`。
- runtime 自动绑定成功后只会进入 `Signature.ModifyScriptFunction()`；该函数当前只处理 hidden world context、`BlueprintProtected`、`Deprecated`、`EditorOnly` 和 `DeterminesOutputType`，没有任何 `DevelopmentOnly` 对应的 trait 或 `compileOutType` 写入：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h:414-457`。
- 如果函数表项没有 direct native pointer，`BindBlueprintCallable()` 会直接尝试 `BindBlueprintCallableReflectiveFallback()`；该 fallback 的 eligibility 只拒绝 `null`、interface、`CustomThunk` 和 `>16` 参数函数，没有 `DevelopmentOnly` 分支：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp:73-84` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp:277-290,374-411`。
- 引擎里已经有大量 `DevelopmentOnly` BlueprintCallable/Pure 函数。`UKismetSystemLibrary::RaiseScriptError`、`LogString`、`PrintString`、`PrintText` 都带 `meta=(DevelopmentOnly)`：`J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h:129-130,546-577`。
- 当前 UHT 产物已经把这些函数写进自动函数表，其中 `RaiseScriptError`、`LogString`、`PrintString`、`PrintText` 都在 `AS_FunctionTable_Entries.csv` 里占有 stub entry：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:2678,2701-2702,2705`。
- 对比之下，手写 logging/debugging 绑定早就明确走 compile-out 语义：`Bind_Logging.cpp` 对日志/打印 API 统一调用 `FAngelscriptBinds::CompileOutIfNoLog(...)`，`Bind_Debugging.cpp` 对 `ensure` / `check` 调用 `CompileOutAsEnsure(...)` / `CompileOutAsCheck(...)`：`Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Logging.cpp:8-204` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Debugging.cpp:61-165`。
- 这些 compile-out helper 最终会在 shipping/test/simulated cooked 场景写 `Function->compileOutType`，并在开启 `bForceConstWithinDevelopmentOnlyFunctions` 时额外设置 `asTRAIT_FORCE_CONST_ARGUMENT_EXPRESSIONS`：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp:464-523`。
- 我对 `Plugins/Angelscript/Source/AngelscriptTest` 和 `Plugins/Angelscript/Source/AngelscriptRuntime/Tests` 执行 `rg --line-number 'DevelopmentOnly|CompileOutIfNoLog|CompileOutInTest|CompileOutAsCheck|PrintString|RaiseScriptError' ...` 的结果为 `0` 行，说明当前没有自动化覆盖 UHT 自动绑定下的 `DevelopmentOnly` 语义回归。

### 发现 56：模块级 JSON/CSV 没有做稳定排序，输出顺序直接泄露 `.uhtmanifest` 外部顺序，轻微依赖调整就会重排整段产物

| 项目 | 内容 |
|------|------|
| 维度 | B / E / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtSession.cs`, `Intermediate/Build/Win64/AngelscriptProjectEditor/Development/AngelscriptProjectEditor.uhtmanifest`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:56-75,166-205,218-264`; `UhtSession.cs:807,1082,2305-2320`; `AngelscriptProjectEditor.uhtmanifest:12,1715,2047,2265,2345,2387,2802,4918,5161,7478,8874,16015,16184,24197`; `AS_FunctionTable_ModuleSummary.csv:2-15`; `AS_FunctionTable_Entries.csv:2-20` |
| 描述 | 条目内部虽然按 `ClassName/FunctionName` 排过序，但模块级输出完全没有本地归一化。`Generate()` 直接按 `factory.Session.Modules` 顺序追加 `moduleSummaries` 和 `csvEntries`，后续 `Summary.json`、`ModuleSummary.csv`、`Entries.csv` 都原样写出。这样一来，产物顺序不是由内容决定，而是由 `.uhtmanifest` 的模块排列决定。 |
| 根因 | 代码生成器只对“模块内条目”做排序，没有对“模块列表”做排序；同时 UE5.x UHT `Session.Modules` 本质上就是把 manifest 里的 `Modules` 列表按原顺序拷进 `_modules`。 |
| 影响 | 只要 `.uhtmanifest` 的模块顺序因为 target、插件依赖、Build.cs 依赖声明位置或 UBT 输出差异发生变化，即使单个模块的生成内容完全没变，`ModuleSummary.csv` 和 `Entries.csv` 也会出现大段重排。对当前 `6043` 行 entry 规模来说，这会放大 diff 噪声、削弱缓存命中和 review 可读性，也让“是否真的有语义变化”更难判断。 |

证据补充：

- `Generate()` 直接遍历 `factory.Session.Modules`，把命中的 module summary 依次 `Add()` 到 `moduleSummaries`，并把 entry 依次追加到 `csvEntries`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:56-75`。
- 写文件时没有再做模块级排序：`WriteGenerationSummary()` 直接把 `moduleSummaries.Select(...)` 序列化进 `Summary.json`，`WriteModuleSummaryCsv()` 和 `WriteEntryCsv()` 也都是按当前列表顺序顺写：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:166-205,218-264`。
- UE5.x UHT 侧 `Session.Modules` 只是 `_modules` 的只读视图，而 `_modules` 在 `StepPrepareModules()` 里按 `Manifest.Modules` 顺序逐个 `Add`：`J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtSession.cs:807,1082,2305-2320`。
- 当前实际 manifest 顺序就已经不是任何“内容排序”意义上的自然序。相关模块在 `AngelscriptProjectEditor.uhtmanifest` 中依次出现为 `Engine -> Landscape -> AIModule -> NavigationSystem -> GameplayTasks -> GameplayTags -> UMG -> EngineSettings -> AssetRegistry -> UnrealEd -> UMGEditor -> GameplayAbilities -> EnhancedInput -> AngelscriptRuntime`：`Intermediate/Build/Win64/AngelscriptProjectEditor/Development/AngelscriptProjectEditor.uhtmanifest:12,1715,2047,2265,2345,2387,2802,4918,5161,7478,8874,16015,16184,24197`。
- 当前导出的 `AS_FunctionTable_ModuleSummary.csv` 也逐字复现了这条 manifest 顺序，而不是按模块名、条目数或 shard 数排序：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv:2-15`。
- `AS_FunctionTable_Entries.csv` 也因此先整块写完 `Engine`，再整块写 `Landscape/AIModule/...`；文件开头就是 `Engine,false,AActor,...` 的连续行，说明条目排序被模块顺序包了一层外部顺序：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:2-20`。
- 现有测试只校验 CSV/JSON 头和行数一致性，没有任何断言要求模块顺序或 entry 顺序稳定：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp:623-645`。

---

## 分析 (2026-04-08 13:07)

### 发现 57：`SkippedEntries.csv` 的统计范围大于实际生成范围，coverage 诊断把未支持模块也错误计入“跳过”

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv` |
| 行号 | `AngelscriptFunctionTableExporter.cs:27-53,65-95`; `AngelscriptFunctionTableCodeGenerator.cs:51-76,334-390,490-515`; `AngelscriptRuntime.Build.cs:28-79`; `AS_FunctionTable_SkippedEntries.csv:2-3,68,3839-3886`; `AS_FunctionTable_ModuleSummary.csv:4,12-14` |
| 描述 | 生成链路只对 `LoadSupportedModules()` 解析出的依赖模块执行 `GenerateModule()`，但 exporter 统计 skipped/reconstructed 时却直接遍历 `factory.Session.Modules` 的全部模块，并对所有 BlueprintCallable/Pure 函数调用 `AngelscriptFunctionSignatureBuilder.TryBuild()`。结果是 `AS_FunctionTable_SkippedEntries.csv` 里混入了大量根本不在生成范围内的模块，例如 `ACLPlugin`、`AndroidPermission`、`VariantManager`；这些模块既不在 `AngelscriptRuntime.Build.cs` 的依赖列表里，也不会出现在 `AS_FunctionTable_ModuleSummary.csv` 中。 |
| 根因 | `AngelscriptFunctionTableExporter.Export()` 的 coverage 统计路径没有复用 `AngelscriptFunctionTableCodeGenerator.ShouldGenerate()` / `supportedModules.All.Contains(...)` 这套真实生成过滤条件，而是把“UHT session 里所有 BlueprintCallable 函数”直接当成候选集。 |
| 影响 | 当前 skip CSV 和 console 里的 `skippedCount` 不能被解释为“生成范围内有多少函数因为签名问题被降级/跳过”，而只是“整个 UHT session 有多少 Blueprint API 无法重建 direct bind”。这会把未支持模块、未纳入插件依赖面的函数也算成 coverage 债务，误导排障优先级和回归判断。 |

证据补充：

- 实际生成路径先调用 `LoadSupportedModules()`，随后在 `Generate()` 中明确用 `if (!supportedModules.All.Contains(module.ShortName)) continue;` 过滤模块；函数级又要通过 `ShouldGenerate()` 才会入表：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:51-76,490-515`。
- `LoadSupportedModules()` 的来源只是 `AngelscriptRuntime.Build.cs` 里的依赖列表。当前文件中可见的模块包含 `AIModule`、`EnhancedInput`、`GameplayAbilities`，editor 分支还额外加入 `UMGEditor`，但没有 `ACLPlugin`、`AndroidPermission`、`VariantManager`：`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:28-79`。
- 与此相对，exporter 的统计代码在 `Export()` 中直接遍历 `factory.Session.Modules`，把每个模块都喂给 `CountBlueprintCallableFunctions()`；后者只检查 `IsBlueprintCallable()`，完全不看 supported module/filter：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:27-53,56-95`。
- 当前实际产物已经证明了这种范围漂移：
  - `AS_FunctionTable_SkippedEntries.csv` 第 `2-3` 行就出现了未支持模块 `ACLPlugin`，第 `68` 行出现了 `AndroidPermission`，第 `3839-3886` 行整段是 `VariantManager`：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:2-3,68,3839-3886`。
  - `AS_FunctionTable_ModuleSummary.csv` 只包含实际生成模块，例如 `AIModule`、`UMGEditor`、`GameplayAbilities`、`EnhancedInput`，并不包含上述未支持模块：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv:4,12-14`。
- 这说明 skipped CSV 的统计口径和真实生成口径已经分叉，不能直接拿来衡量“支持模块里的 direct-bind 覆盖质量”。

### 发现 58：header resolver 把 `ScriptName` 后的反射名当成 C++ 符号名查找，导致别名重载只能靠白名单，成批退化为 `ERASE_NO_FUNCTION()`

| 项目 | 内容 |
|------|------|
| 维度 | B / C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:18-35,75-105,362-397,467-506`; `AngelscriptFunctionSignatureBuilder.cs:39-60,83-107`; `AngelscriptComponentLibrary.h:44-53,65-74,113-123,162-171,183-192,204-213`; `RuntimeFloatCurveMixinLibrary.h:29-51,68-70`; `AS_FunctionTable_SkippedEntries.csv:123,125,129-130,133-134`; `AS_FunctionTable_Entries.csv:5781,5784,5800,5802,5806,5808,6023-6024`; `AngelscriptBindConfigTests.cpp:706-751,781-843` |
| 描述 | resolver 用 `function.SourceName` 直接在 header 里搜索 `functionName + "("`，并在解析阶段继续用同一个名字做 `IndexOf(function.SourceName + "(")`。这对“反射导出名”和“真实 C++ 符号名”一致的函数没问题，但一旦 `UFUNCTION(meta=(ScriptName="..."))` 把多个不同 C++ 符号折叠到同一个反射名，resolver 就会只看到原名声明，看不到 `...Quat` / `..._64` 这类真实声明，最终把第二个及后续别名函数判成 `overloaded-unresolved`。builder 对这种情况又只给 `URuntimeFloatCurveMixinLibrary::{GetNumKeys, GetTimeRange}` 两个名字开白名单，导致同模式的其它函数成批退化成 stub。 |
| 根因 | 代码生成链把 UHT 暴露的 `SourceName` 当成了 header 中可直接查找的 C++ 标识符，没有维护“反射名”与“真实声明名”的分离；同时 `overloaded-unresolved` 分支被硬编码成极小白名单，而不是复用 UHT 已有的参数/返回类型信息做通用映射。 |
| 影响 | 插件自己大量用 `ScriptName` 暴露的 helper API 会在 UHT 生成表中失去 direct bind，只能落到 `ERASE_NO_FUNCTION()` 再依赖 runtime reflective fallback。这样既降低调用性能，也让 `AS_FunctionTable_*` 的覆盖数据对这类 API 长期保持“假阴性”。 |

证据补充：

- resolver 的关键查找路径完全基于 `function.SourceName`：
  - `TryBuild()` 直接把 `function.SourceName` 传给 `FindCandidates()`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:18-35`。
  - `FindCandidates()` 用 `string marker = functionName + "("` 做纯文本搜索：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:362-397`。
  - `TryParseDeclaration()` 继续要求 declaration 里能找到 `function.SourceName + "("`，否则直接失败：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:467-506`。
- builder 虽然已经具备从 UHT property 构造显式 `ERASE_FUNCTION_PTR` 的能力，但只要 failure reason 是 `overloaded-unresolved`，就会在白名单外直接 `return false`；白名单目前只有 `URuntimeFloatCurveMixinLibrary::{GetNumKeys, GetTimeRange}` 两个名字：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:39-60,83-107`。
- `UAngelscriptComponentLibrary` 正好大量使用这个模式。例如：
  - `SetRelativeRotation` / `SetRelativeRotationQuat` 都导出为 `ScriptName = "SetRelativeRotation"`：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h:44-53`。
  - `SetRelativeLocationAndRotation` / `SetRelativeLocationAndRotationQuat` 都导出为 `ScriptName = "SetRelativeLocationAndRotation"`：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h:65-74`。
  - `AddRelativeRotation` / `AddRelativeRotationQuat`、`SetWorldRotation` / `SetWorldRotationQuat`、`SetWorldLocationAndRotation` / `SetWorldLocationAndRotationQuat`、`AddWorldRotation` / `AddWorldRotationQuat` 也是同一模式：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h:113-123,162-171,183-192,204-213`。
- 这些函数当前确实都被 UHTTool 记成 `overloaded-unresolved`，并最终在 entry 表里落成 `Stub,ERASE_NO_FUNCTION()`：
  - skipped CSV：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:123,125,129-130,133-134`。
  - entry CSV：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:5781,5784,5800,5802,5806,5808`。
- 对比之下，同样依赖 `ScriptName` 别名的 `URuntimeFloatCurveMixinLibrary::GetTimeRange_Double` 这条路径因为被白名单特判，`GetNumKeys` 和 `GetTimeRange` 目前能成功生成 direct entry：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/RuntimeFloatCurveMixinLibrary.h:29-51,68-70` 与 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:6023-6024`。
- 当前自动化只覆盖了这两个白名单样本：
  - `FAngelscriptOverloadResolutionCoverageTest` 只验证 `UAngelscriptUhtOverloadCoverageLibrary::ResolveCoverageOverload` 这种“一个 reflected 函数 + 一个未反射 overload”的场景：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp:706-751`。
  - `FAngelscriptInlineDefinitionCoverageTest` / `FAngelscriptInlineOutRefCoverageTest` 只覆盖 `GetNumKeys` 和 `GetTimeRange`：`Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp:781-843`。
  - 我对 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg --line-number 'SetRelativeRotation|SetRelativeLocationAndRotation|AddRelativeRotation|SetWorldRotation|SetWorldLocationAndRotation|AddWorldRotation|SinCos|Modf|Wrap' ...`，结果为 `0` 行，说明目前没有测试覆盖这类 `ScriptName` alias 重载的 UHT 回归。

### 发现 59：exporter 的 skipped 统计没有复用函数级过滤规则，`BlueprintInternalUseOnly` / `CustomThunk` 这类“本来就不应生成”的 API 仍被误报为失败项

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Blueprint/WidgetBlueprintLibrary.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Blueprint/AsyncTaskDownloadImage.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Animation/WidgetAnimationPlayCallbackProxy.h` |
| 行号 | `AngelscriptFunctionTableExporter.cs:27-53,56-95`; `AngelscriptFunctionTableCodeGenerator.cs:490-515`; `AS_FunctionTable_SkippedEntries.csv:3702,3797,3801`; `WidgetBlueprintLibrary.h:33,40`; `AsyncTaskDownloadImage.h:22`; `WidgetAnimationPlayCallbackProxy.h:27,41,55,70` |
| 描述 | 实际生成路径在 `ShouldGenerate()` 里明确排除了 `NotInAngelscript`、未带 `UsableInAngelscript` 的 `BlueprintInternalUseOnly`、以及 `CustomThunk` 函数；但 exporter 的 skipped/reconstructed 统计并不调用 `ShouldGenerate()`，只要函数是 BlueprintCallable/Pure 就会尝试重建签名并在失败时写入 `SkippedEntries.csv`。结果是一些按设计就不该出现在生成表里的 Blueprint-only helper 仍然被统计成 “non-public/unexported/skip”。 |
| 根因 | `CountBlueprintCallableFunctions()` 和真实代码生成链共享了 `AngelscriptFunctionSignatureBuilder.TryBuild()`，却没有共享 `ShouldGenerate()` 的函数级过滤规则，造成“候选集”和“生成集”不一致。 |
| 影响 | 当前 skip 统计会把被插件策略主动排除的 Blueprint helper 误报成 UHT 失败项，污染 coverage 指标，也让工程师难以区分“需要修 direct bind”的 API 与“本来就不应导出到 Angelscript”的 API。 |

证据补充：

- 函数级真实过滤逻辑在 `ShouldGenerate()` 中非常明确：命中 `NotInAngelscript`、`BlueprintInternalUseOnly && !UsableInAngelscript` 就直接 `return false`，最后还会排除 `CustomThunk`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:490-515`。
- 统计路径却没有复用这套规则。`Export()` 直接遍历所有 module/type，`CountBlueprintCallableFunctions()` 只要 `IsBlueprintCallable(function)` 就尝试 `TryBuild()`，失败就写 skipped CSV：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:27-53,56-95`。
- 支持模块 `UMG` 中已经能看到实证样本：
  - `UAsyncTaskDownloadImage::DownloadImage` 在引擎头里带 `meta=(BlueprintInternalUseOnly="true")`：`J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Blueprint/AsyncTaskDownloadImage.h:22`；但当前仍被记进 skipped CSV：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:3702`。
  - `UWidgetBlueprintLibrary::Create` 和另一个 drag/drop helper 也带 `BlueprintInternalUseOnly="true"`：`J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Blueprint/WidgetBlueprintLibrary.h:33,40`；其中 `Create` 当前同样出现在 skipped CSV：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:3801`。
  - `UWidgetAnimationPlayCallbackProxy` 的工厂函数在引擎头中整组标了 `BlueprintInternalUseOnly="true"`：`J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Animation/WidgetAnimationPlayCallbackProxy.h:27,41,55,70`；当前 `CreatePlayAnimationProxyObject` 也被记成 skipped：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv:3797`。
- 这些函数不属于“UHT 明明应该生成但失败了”的范畴，而是“策略上就不该进生成集”的函数；把它们和真正的签名重建失败项混在同一份 skipped 诊断里，会直接降低错误诊断质量。

### 发现 60：支持模块与 editor-only 判定没有使用 UHT 已解析的模块元数据，而是重新正则解析 `AngelscriptRuntime.Build.cs`，把 UBT 语义降级成脆弱文本协议

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtModule.cs` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:334-428`; `AngelscriptRuntime.Build.cs:30-79`; `UhtModule.cs:24-31,78-79,88-114,189-194,227-230` |
| 描述 | UHT session 里已经存在 `UhtModule.Module`、`Module.BaseDirectory`、`Module.ModuleType`、`Headers` 等结构化模块信息，但 `LoadSupportedModules()` 没有复用这些元数据，而是先通过 `TryFindFirstHeaderPath()` 从某个 header 反推 `AngelscriptRuntime.Build.cs` 路径，再用 `QuotedStringPattern`、`line.StartsWith("if (Target.bBuildEditor)")`、`line.Contains("DependencyModuleNames.AddRange")`、`line == "}"` 这组纯文本规则去重建支持模块集合和 editor-only 集合。当前实现等于把 UBT/UHT 已经解析过的构建语义重新降级成了一个依赖源文件书写格式的文本协议。 |
| 根因 | 代码生成器没有把 `UhtModule` 暴露的结构化模块元数据当成真源，而是选择了“扫描 Build.cs 字符串”这条旁路。 |
| 影响 | 已验证事实是当前支持模块/EditorOnly 判定依赖 `Build.cs` 的具体书写格式。推断：一旦 UE5.x 对 `ModuleRules` 写法、`Build.cs` 模板、或 UHT session 暴露的模块布局做重构，即使语义完全不变，这里的解析也可能漂移或直接失效，进而影响生成范围与 `#if WITH_EDITOR` 包裹策略。 |

证据补充：

- `LoadSupportedModules()` 当前的完整逻辑是：
  - 先调用 `ResolveRuntimeBuildCsPath()`，通过 `TryFindFirstHeaderPath(module.ScriptPackage, ...)` 找到某个 header，再从 header 路径里做字符串截断来拼出 `AngelscriptRuntime.Build.cs`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:334-428`。
  - 之后按行扫描 Build.cs，只认 `DependencyModuleNames.AddRange`、`if (Target.bBuildEditor)`、`line == "}"` 和双引号字符串：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:350-378`。
- 这个文本协议和当前 `AngelscriptRuntime.Build.cs` 的写法强绑定。当前文件恰好是 `PublicDependencyModuleNames.AddRange(...)` / `PrivateDependencyModuleNames.AddRange(...)`，editor 分支也是裸 `if (Target.bBuildEditor)`，所以解析能工作：`Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs:30-79`。
- 但 UHT 自身已经提供了更稳定的结构化来源：
  - `UhtModule` 直接持有 `UHTManifest.Module Module`：`J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtModule.cs:24-31`。
  - `UhtModule` 暴露 `Headers`、`Module.ModuleType`、`Module.BaseDirectory`、`IsPlugin` 等信息：`J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtModule.cs:78-79,88-114`。
  - `PrepareHeaders()` 也是基于 `Module.ClassesHeaders/PublicHeaders/InternalHeaders/PrivateHeaders` 这些 manifest 级结构构建 header 集合，而不是反向去猜 Build.cs：`J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtModule.cs:189-194,227-230`。
- 当前测试没有覆盖这条解析边界。我对 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg --line-number 'LoadSupportedModules|ResolveRuntimeBuildCsPath|TryFindFirstHeaderPath|AngelscriptRuntime.Build.cs|Target\\.bBuildEditor|DependencyModuleNames\\.AddRange' ...`，唯一命中是 `AngelscriptTest.Build.cs` 自身，没有 UHT 自动化测试直接约束这套 Build.cs 文本解析行为。

### 发现 61：UHT flag 判定大量依赖 `ToString().Contains(...)`，没有使用 UHT 原生的 `HasAnyFlags` / `HasExactFlags`，UE5.x 升级时容易静默漂移

| 项目 | 内容 |
|------|------|
| 维度 | B / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/UhtFunctionParser.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtClass.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/Properties/UhtStructProperty.cs` |
| 行号 | `AngelscriptFunctionTableExporter.cs:56-63`; `AngelscriptFunctionSignatureBuilder.cs:90-96,116-132`; `AngelscriptHeaderSignatureResolver.cs:63-64,142-147,485-502,534-539`; `UhtFunctionParser.cs:370-385`; `UhtClass.cs:1861-1862,1977,2006-2011`; `UhtStructProperty.cs:214-227,250` |
| 描述 | UHTTool 在多个关键分支里都不是用强类型 flag API 判定函数/属性语义，而是先 `ToString()` 再做 `Contains("BlueprintCallable")`、`Contains("BlueprintPure")`、`Contains("Static")`、`Contains("Const")`、`Contains("CustomThunk")`、`Contains("ConstParm")`。这意味着工具逻辑绑定的是“枚举字符串如何格式化”，而不是“flag 位是否设置”。 |
| 根因 | 生成器/签名解析器没有跟随 UHT 自身的 flag 访问模式，选择了字符串匹配这一层更脆弱的表示。 |
| 影响 | 已验证事实是当前逻辑依赖枚举名字符串。推断：如果 UE5.x 调整 flag 名、`ToString()` 输出格式、组合 flag 的文本拼接方式，或未来新增包含相同子串的 flag，UHTTool 的筛选和签名推断会静默改变，而不是在编译期暴露错误。 |

证据补充：

- 当前 UHTTool 的字符串 flag 判定分布很广：
  - `IsBlueprintCallable()` 先取 `function.FunctionFlags.ToString()`，再用 `Contains("BlueprintCallable")` / `Contains("BlueprintPure")` 决定是否进入导出候选集：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs:56-63`。
  - `AngelscriptFunctionSignatureBuilder` 用 `HasFunctionFlag()` 包装 `function.FunctionFlags.ToString().Contains(flagName)` 来判断 `Static` / `Const`，同时对 `ConstParm` 也走 `property.PropertyFlags.ToString().Contains("ConstParm")`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs:90-96,116-132`。
  - `AngelscriptHeaderSignatureResolver` 在多个位置重复同样的字符串判定：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:63-64,142-147,485-502,534-539`。
  - `ShouldGenerate()` 对 `CustomThunk` 的排除也是 `function.FunctionExportFlags.ToString().Contains("CustomThunk")`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:514`。
- 对比之下，UHT 自己在同一套类型系统里始终使用强类型 flag API：
  - `UhtFunctionParser` 用 `function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.CustomThunk)`、`function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.Native | EFunctionFlags.Net)`：`J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/UhtFunctionParser.cs:370-385`。
  - `UhtClass` 用 `HasAnyFlags` / `HasExactFlags` 判断 `BlueprintCallable`、`BlueprintPure`、`BlueprintEvent` 等语义：`J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtClass.cs:1861-1862,1977,2006-2011`。
  - 属性系统同样使用 `PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm | EPropertyFlags.EditorOnly | EPropertyFlags.Net ...)`：`J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/Properties/UhtStructProperty.cs:214-227,250`。
- 当前测试集中没有任何用例直接约束“flag 文本格式变化时，UHTTool 仍能保持同样筛选语义”；也就是说，这类漂移一旦发生，更可能表现为 coverage/生成结果变化，而不是被单元测试立即拦截。

---

## 分析 (2026-04-08 13:21)

### 发现 62：`TryParseDeclaration()` 对非 `void` 候选根本不解析 header 返回类型，`return-type` 校验在 wrapper 误判链路里实际上失效

| 项目 | 内容 |
|------|------|
| 维度 | A / B / D |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptFrameTimeMixinLibrary.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:70-106,139-150,465-506,531-542`; `AngelscriptFrameTimeMixinLibrary.h:13-16`; `AngelscriptMathLibrary.h:360-363`; `AS_FunctionTable_SkippedEntries.csv:176-178`; `AS_FunctionTable_Entries.csv:5870,5881,5885` |
| 描述 | resolver 在 overload 匹配时先用 UHT AST 计算 `expectedReturnType`，但 `TryParseDeclaration()` 对所有非 `void` 候选又直接从同一个 `UhtProperty` 重新构造 `returnType`，完全不读取候选声明前缀。结果是像 `return Target.AsSeconds();`、`return FVector::PointPlaneProject(...);` 这类函数体内调用表达式，只要参数个数能对上，就不会因为“返回类型根本不是声明前缀”而被排除；`return-type` 失败原因事实上只会覆盖 `void` 函数。 |
| 根因 | `TryParseDeclaration()` 把“解析候选声明文本”和“复用 UHT 已知类型信息”混在了一起。对非 `void` 候选，所谓的返回类型校验退化成了“拿 UHT 返回类型和同一个 UHT 返回类型做自比较”。 |
| 影响 | 这会放大 header 文本扫描误判的破坏面。对于返回值非 `void` 的 inline wrapper，同名调用语句更容易被当成合法候选并进入 `exactMatches`，最终把本可 direct bind 的函数打成 `overloaded-unresolved` 或 `Stub`。同时，`return-type` 这个诊断标签对最关键的非 `void` 场景几乎没有监控价值，测试也无法覆盖到这条失效路径。 |

证据补充：

- `TryBuild()` 在进入 overload 匹配前先根据 `function.ReturnProperty` 构造 `expectedReturnType`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:70-73`。
- `TryParseDeclaration()` 对非 `void` 候选并不解析 `prefix`，而是再次走 `BuildReturnTypeFromTokens(returnProperty)`；只有 `void` 函数才会调用 `CleanReturnType(prefix)`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:484-493,531-542`。
- 因此 `failureReason = "return-type"` 实际只在 `string.IsNullOrWhiteSpace(returnType)` 时触发，而这对非 `void` 分支基本不可达：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs:489-493`。
- 当前仓库里已经有会踩中这条漏洞的真实 wrapper：
  - `UAngelscriptFrameTimeMixinLibrary::AsSeconds()` 的函数体是 `return Target.AsSeconds();`，函数名与调用名相同，但候选前缀并不是有效声明返回类型：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptFrameTimeMixinLibrary.h:13-16`。
  - `UAngelscriptFVectorMixinLibrary::PointPlaneProject()` 的函数体是 `return FVector::PointPlaneProject(Vector, PlaneBase, PlaneNormal);`，同样会在 class body 里生成一个“调用表达式候选”：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h:360-363`。
- 当前导出产物已经把这类函数记成 `overloaded-unresolved + Stub`：`UAngelscriptFVector3fMixinLibrary::PointPlaneProject`、`UAngelscriptFVectorMixinLibrary::PointPlaneProject`、`UAngelscriptFrameTimeMixinLibrary::AsSeconds` 分别位于 `AS_FunctionTable_SkippedEntries.csv:176-178`，对应 `AS_FunctionTable_Entries.csv:5870,5881,5885`。
- 我对 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 与 `.../AngelscriptBindConfigTests.cpp` 执行 `rg --line-number 'return-type|PointPlaneProject|AsSeconds' ...`，结果为 `0` 行，说明当前自动化没有直接约束这条“非 void 返回类型校验失效”的回归边界。

---

## 分析 (2026-04-08 13:25)

### 发现 63：`WITH_EDITORONLY_DATA` 包裹的空操作 wrapper 仍被 summary/entry 统计成普通 `Direct`，覆盖率会把“有函数指针”误当成“有运行效果”

| 项目 | 内容 |
|------|------|
| 维度 | C / D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:100-139,465-477`; `AngelscriptComponentLibrary.h:239-244`; `AS_FunctionTable_Entries.csv:5811` |
| 描述 | `UAngelscriptComponentLibrary::SetbVisualizeComponent()` 被声明成普通 `BlueprintCallable`，UHTTool 因而为它生成了 `ERASE_AUTO_FUNCTION_PTR(...)` 并在 `Entries.csv` 里记成 `Direct`。但该函数体真正的赋值语句被完整包在 `#if WITH_EDITORONLY_DATA` 里，离开 editor-only-data 配置后函数仍然存在，却变成了静默空操作。 |
| 根因 | 生成器只基于 `BlueprintCallable/Pure`、metadata 和签名构建结果来判断 `Direct/Stub`，完全不分析函数体里的预处理守卫；当前健康度模型默认“拿到可调用指针”就等于“该 API 在目标配置里有有效行为”。 |
| 影响 | 对跨 target 的诊断来说，这会系统性高估有效 coverage。脚本层能看到 `SetbVisualizeComponent()` 这样的 direct entry，但在 non-editor-only-data 配置下调用不会产生任何效果；summary/json/csv 仍把它记为成功 direct bind，导致行为退化既不会出现在 skipped，也不会出现在 stub 统计里。 |

证据补充：

- `CollectEntries()` 对普通类函数只要 `TryBuild()` 成功就写入具体 `ERASE_*` 宏，随后 `GenerateModule()` 仅按 `eraseMacro == "ERASE_NO_FUNCTION()"` 区分 `Stub/Direct`：`Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs:100-139,465-477`。
- `UAngelscriptComponentLibrary::SetbVisualizeComponent()` 的唯一副作用 `Component->bVisualizeComponent = bVisualize;` 完全位于 `#if WITH_EDITORONLY_DATA` 块内；在不定义该宏的目标里，这个函数体会退化成空实现：`Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h:239-244`。
- 当前导出产物已经把它记成普通 direct entry，而不是 target-sensitive 条目：`Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv:5811`。
- 我对 `Plugins/Angelscript/Source/AngelscriptTest` 执行 `rg --line-number 'SetbVisualizeComponent|GetShouldRenderSelected|GetShouldShowForSelectedSubcomponents' ...`，结果为 `0` 行，说明当前自动化没有覆盖这类“函数存在但非 editor 目标下语义为空”的边界。
