# UHTTool 发现与方案

---

## 发现与方案 (2026-04-08 12:30)

### Issue-49：固定 256 条的全局排序分片会放大单点改动，触发后续 shard 级联重写

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | 92-121 |
| 问题 | `GenerateModule()` 先对整个模块的 `entries` 做全局字典序排序，再按固定 `MaxEntriesPerShard = 256` 用 `startIndex = shardIndex * MaxEntriesPerShard` 切片写 `AS_FunctionTable_<Module>_<NNN>.cpp`。这意味着只要在排序靠前的位置插入或删除 1 条 entry，后面所有 shard 的内容边界都会整体平移。当前产物已经能看到这种脆弱边界：`AS_FunctionTable_Entries.csv:252-258` 在 `ALocationVolume::Unload` 与 `APawn::AddControllerPitchInput` 之间跨过 shard 边界；`AS_FunctionTable_Entries.csv:508-514` 又在 `UAnimMontage::GetGroupName` 与 `GetNumSections` 之间跨过下一处边界。对应生成文件中，`AS_FunctionTable_Engine_000.cpp:476-478` 以 `ALocationVolume` 收尾，而 `AS_FunctionTable_Engine_001.cpp:223-236` 立即从 `APawn` 开始。 |
| 根因 | 分片策略按“排序后的全局序号”建模，而不是按稳定的类级分区或持久化 shard 映射建模；`CommitOutput()` 虽然能避免内容未变时重写文件，但当前切片方式会让大量后续 shard 内容同步变化。 |
| 影响 | 对 `Engine` 这种当前已有 `16` 个 shard 的大模块，只要前部类新增或删除少量 BlueprintCallable，后续多个 `AS_FunctionTable_Engine_*.cpp` 都会被重写并重新编译，增量构建会退化成接近整模块重建。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把分片从“全局固定序号切片”改成“稳定的类级分区 + 持久化 shard 映射”，把改动影响面限制在局部 shard。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.cs` 中抽出 `ShardPlanner`，先按 `ClassName` 聚合 entry，保证同一类的函数不跨 shard。 2. 新增 `AS_FunctionTable_ShardManifest.json`，记录 `ModuleName/ClassName -> ShardId` 的稳定映射；下次生成时优先复用已有映射，只给新类分配新 shard 或局部重排受影响 shard。 3. `BuildShard()` 改为消费 `ShardPlan`，文件名使用稳定 `ShardId`，而不是重新从 0 开始按当前位置编号。 4. 当单个类条目数超过阈值时，只允许该类内部按二级块拆分，并把二级块编号写入 manifest，避免牵连其他类。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 新增“早序类新增函数时，未受影响 shard 哈希保持不变”的回归测试或脚本化校验。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Intermediate/Build/.../UHT/AS_FunctionTable_ShardManifest.json` |
| 预估工作量 | L |
| 风险 | 首次引入 manifest 会改变 shard 命名与产物布局，现有依赖 `AS_FunctionTable_<Module>_<NNN>.cpp` 序号的测试/脚本需要同步迁移。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 在测试覆盖头中给排序靠前的类新增 1 个 `BlueprintCallable`。 2. 重新运行 UHT 导出。 3. 比较 `git diff` 或文件哈希，确认只有目标类所在 shard、manifest 和汇总 sidecar 变化，其他 `AS_FunctionTable_Engine_*.cpp` 保持不变。 4. 完整编译 `AngelscriptRuntime`，确认生成代码仍可通过。 |

### Issue-50：生成产物没有任何 schema/provenance 版本字段，无法判断 sidecar 是否与当前导出器兼容

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:166-206, 218-265`; `AngelscriptFunctionTableExporter.cs:99-160`; `AngelscriptGeneratedFunctionTableTests.cpp:465-721` |
| 问题 | 当前 `AS_FunctionTable_Summary.json` 只输出计数字段，`AS_FunctionTable_ModuleSummary.csv`、`AS_FunctionTable_Entries.csv`、`AS_FunctionTable_SkippedEntries.csv`、`AS_FunctionTable_SkippedReasonSummary.csv` 也只有数据列，没有 `schemaVersion`、`generatorVersion`、输入哈希或构建上下文。现有产物头部可直接验证这一点：`AS_FunctionTable_Summary.json:1-18` 只有 `totalGeneratedEntries`、`totalDirectBindEntries`、`modules` 等计数；`AS_FunctionTable_ModuleSummary.csv:1-5` 与 `AS_FunctionTable_SkippedEntries.csv:1-5` 也只有纯列头。与此同时，测试侧直接按固定文件名和固定字段读取这些产物，例如 `TryGetNumberField("totalGeneratedEntries")`、`LoadNonEmptyFileLines("AS_FunctionTable_ModuleSummary.csv")`、`LoadNonEmptyFileLines("AS_FunctionTable_SkippedEntries.csv")`，没有任何“版本匹配”或“来源匹配”校验。 |
| 根因 | UHTTool 把 sidecar 当作一次性调试输出，而不是有契约、有生命周期的正式导出接口；设计上没有定义 schema 演进策略，也没有把输入集指纹写回产物。 |
| 影响 | 一旦后续修正 shard 规划、统计模型、字段命名或列结构，旧 sidecar 仍会被当前测试和人工排查路径静默消费，无法从文件本身判断“这是旧 schema”还是“这是新逻辑下的真实结果”。这会让版本迁移、回归定位和跨分支比较都缺少最基本的兼容性边界。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 给所有生成产物补齐统一的 schema/provenance 元数据，并把测试改成先校验版本再校验内容。 |
| 具体步骤 | 1. 新增 `AngelscriptGeneratedArtifactMetadata` 记录，至少包含 `schemaVersion`、`generatorVersion`、`engineMajorMinor`、`buildCsHash`、`supportedModuleHash`、`generatedAtUtc`、`sourceInputFingerprint`。 2. 在 `WriteGenerationSummary()` 中把这组 metadata 作为 JSON 顶层字段写入 `AS_FunctionTable_Summary.json`。 3. 为 CSV sidecar 新增配套 `AS_FunctionTable_Metadata.json`，或在 CSV 第一行前写 `# schemaVersion=...` 这类机器可解析注释，并在读取端统一解析。 4. 对 shard `.cpp` 也输出版本宏，例如 `#define AS_FUNCTIONTABLE_SCHEMA_VERSION 2`，让后续编译期日志和问题定位能回溯生成版本。 5. 更新 `AngelscriptGeneratedFunctionTableTests.cpp`，先断言版本/指纹存在且与当前源码一致，再继续做字段级内容校验。 6. 规定 breaking schema change 必须提升 `schemaVersion`，并在 `DeleteStaleOutputs()` 或 sidecar 写盘阶段清理旧版本产物。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 加入 metadata 后，现有测试与下游脚本会因为字段或注释增加而需要同步更新；如果 `sourceInputFingerprint` 选取过宽，可能造成不必要的 sidecar 失效。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行一次 UHT 导出，确认所有 sidecar 都带有统一 metadata。 2. 手动提升 `schemaVersion` 或修改列结构，确认旧产物会被测试显式拒绝，而不是被继续解析。 3. 在同一源码树上重复导出两次，确认 metadata 中除时间戳外保持稳定。 |

### Issue-51：输出契约和渲染逻辑散落在多处手写字符串中，schema 演进需要跨 generator/exporter/测试同步改字面量

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:218-331`; `AngelscriptFunctionTableExporter.cs:99-172`; `AngelscriptGeneratedFunctionTableTests.cpp:481-721` |
| 问题 | 生成侧当前至少有 5 处独立维护输出格式：`WriteModuleSummaryCsv()`、`WriteEntryCsv()`、`WriteSkippedEntriesCsv()`、`WriteSkippedReasonSummaryCsv()` 和 `BuildShard()` 都在手写 header/字段顺序/日志文本；`EscapeCsv()` 还在 `CodeGenerator` 与 `Exporter` 中各复制了一份（`AngelscriptFunctionTableCodeGenerator.cs:272-280`，`AngelscriptFunctionTableExporter.cs:164-172`）。测试侧又单独把这些字段名和文件名硬编码成 `totalGeneratedEntries`、`totalDirectBindEntries`、`AS_FunctionTable_ModuleSummary.csv`、`AS_FunctionTable_SkippedEntries.csv` 等字符串（`AngelscriptGeneratedFunctionTableTests.cpp:481-505, 629-721`）。 |
| 根因 | UHTTool 没有把“产物 schema”和“文本渲染”抽象成单独组件，而是让 generator、exporter、测试分别持有一份字符串级契约。 |
| 影响 | 后续一旦要增加 `schemaVersion`、调整列顺序、补充第三状态或统一错误消息格式，工程师必须跨 C# 生成器、C# exporter 和 C++ 测试同时修改多组字面量；任何漏改都会形成部分漂移。当前代码已经出现 duplicated `EscapeCsv()` 和多套 header 常量，说明这条维护成本不是理论风险。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 抽离统一的输出 schema 与渲染层，减少手写字符串散布点，并把测试改成按 header/metadata 驱动解析。 |
| 具体步骤 | 1. 新增 `AngelscriptGeneratedOutputSchema.cs`，集中定义 JSON 字段名、CSV 列名、日志前缀和 failure reason 常量。 2. 新增 `AngelscriptGeneratedOutputWriter.cs`，提供共享 `WriteCsv<T>()`、`EscapeCsv()` 和 `RenderShard(ShardModel)`；`BuildShard()` 改为消费模板模型，不再直接拼接几十行 `AppendLine()`。 3. 将 shard C++ 模板提取为嵌入式文本模板或 C# raw string 模板，统一 `#include`、`FAngelscriptBinds::AddFunctionEntry(...)` 和日志尾部格式。 4. `CodeGenerator` 与 `Exporter` 全部走同一套 writer，删除重复的 `EscapeCsv()` 和散落的列头字面量。 5. 更新 `AngelscriptGeneratedFunctionTableTests.cpp`，先读取 header 行构建列索引，再按列名取值，避免把列顺序写死。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptGeneratedOutputSchema.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptGeneratedOutputWriter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果一次性同时重构 JSON、CSV 和 C++ emitter，回归面会较大；应先锁定 schema，再逐步替换 writer。 |
| 前置依赖 | 建议先完成 Issue-50 的 schema/provenance 设计，避免 writer 抽象在下一轮字段升级时再次返工。 |
| 验证方式 | 1. 比较重构前后产物内容，确认除新增 metadata/格式修正外没有语义差异。 2. 运行生成产物相关自动化测试，确认 header 驱动解析仍能通过。 3. 人工检查 `CodeGenerator`/`Exporter` 中不再存在重复 `EscapeCsv()` 或散落的 CSV 头部字符串。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-49 | Defect | 先修复，优先阻断大模块 shard 级联重写带来的增量构建退化 |
| P1 | Issue-50 | Architecture | 第二步落地，为后续 schema 升级和 stale sidecar 检测建立版本边界 |
| P2 | Issue-51 | Refactoring | 在 Issue-50 明确 schema 后实施，收拢输出契约与模板渲染逻辑 |

---

## 发现与方案 (2026-04-08 12:40)

### Issue-52：`public:` / `protected:` 前缀会遮蔽函数级 `*_API` 宏，导致可链接函数被误判为 `unexported-symbol`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/AIController.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:295-360, 401-413`; `AIController.h:294-303`; `AS_FunctionTable_SkippedEntries.csv:4-6` |
| 问题 | `FindDeclarationStart()` 只会向前回溯到上一个 `;/{/}`，不会把 `public:` / `protected:` 访问标签排除出 declaration。随后 `IsLinkVisible()` 先调用 `StripLeadingMacroInvocations()`，但这个函数只会剥离形如 `TOKEN(...)` 的前缀宏，遇到 `public:` 会直接停止。结果是 `declarationPrefix` 的第一个 `(`` 往往落在 `UFUNCTION(...)` 上，函数级 `AIMODULE_API` 等导出宏根本不会进入 `ApiMacroPattern` 检测。当前产物已经出现这个误判：`AAIController::GetAIPerceptionComponent`、`GetFocalPoint`、`UseBlackboard` 在源码中都位于 `public:` 区域，且后两者显式带有 `AIMODULE_API`，但 `AS_FunctionTable_SkippedEntries.csv:4-6` 仍把它们全部标为 `unexported-symbol`。 |
| 根因 | export 可见性检测把“声明正文提取”和“前缀宏剥离”建立在脆弱的原始文本切片上，没有先归一化访问标签与 `UFUNCTION` 前缀；一旦 declaration 不是以宏调用直接起始，`HasLinkableExport()` 就会在错误的前缀区间上做判断。 |
| 影响 | 这会把本来可 direct-bind 的函数系统性降级成 stub/skipped，直接降低生成覆盖率，并把 failure reason 误导成“符号未导出”。当前 `unexported-symbol` 已有 `1262` 条，`AAIController` 这种显式函数级导出函数已经出现误报，说明问题不只影响个别 header。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 export 判定前先标准化 declaration 前缀，显式剥离访问标签和 `UFUNCTION`/deprecated 宏，再对真实函数声明头做 API 可见性分析。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 新增 `NormalizeDeclarationPrefix()`，先去掉 `public:` / `protected:` / `private:` 标签，再循环剥离 `UFUNCTION(...)`、`UE_DEPRECATED(...)`、`UE_DEPRECATED_FORGAME(...)` 等前缀宏。 2. 让 `IsLinkVisible()` 基于标准化后的“函数声明头”计算 `openParenIndex`，保证 `ApiMacroPattern` 看到的是真正位于返回类型前的 `*_API` 宏，而不是前置反射宏的 `(`。 3. 将 `FindDeclarationStart()` 调整为遇到访问标签时从标签后开始，而不是把整段 `public:` 块一并纳入 declaration。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加回归测试，明确断言 `AAIController::GetFocalPoint` 或同类“`public:` + `UFUNCTION` + 函数级 `*_API`”样本必须生成 direct entry，不能再落入 `SkippedEntries.csv`。 5. 导出后对比 `AS_FunctionTable_SkippedEntries.csv`，确认这些样本的 reason 从 `unexported-symbol` 消失，并检查对应 `.cpp` shard 中已生成 `ERASE_AUTO_METHOD_PTR` / `ERASE_METHOD_PTR` 行。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 declaration 归一化规则写得过宽，可能把真正属于函数签名一部分的宏或限定符一起剥掉，影响已有的 `MinimalAPI` / inline 检测路径。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 不再包含 `AAIController,GetFocalPoint,unexported-symbol`、`AAIController,UseBlackboard,unexported-symbol`。 3. 检查对应 `AS_FunctionTable_AIModule_*.cpp` 里已出现这几个函数的 direct 绑定行。 4. 运行 `AngelscriptGeneratedFunctionTableMacroQualifiedDirectBindingsTest` 并补充新的回归测试，确认旧样例和新样例同时通过。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-52 | Defect | 优先修复，先消除函数级 `*_API` 被误判成 `unexported-symbol` 的覆盖率损失 |

---

## 发现与方案 (2026-04-08 12:45)

### Issue-53：`void` 返回类型走文本清洗分支时会夹带 `public:`/反射宏，导致本可命中的 Blueprint overload 被误判成 `overloaded-unresolved`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/BehaviorTree/BlackboardComponent.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/BehaviorTree/BehaviorTreeComponent.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:484-529`; `BlackboardComponent.h:177-179`; `BehaviorTreeComponent.h:275-280`; `AS_FunctionTable_SkippedEntries.csv:35-36` |
| 问题 | `TryParseDeclaration()` 对非 `void` 函数会直接使用 UHT 的 `ReturnProperty`，但对 `void` 函数会退回 `CleanReturnType(prefix)` 走文本清洗。`CleanReturnType()` 只删除 `virtual/static/inline/FORCEINLINE/*_API` 等 token，并不会去掉 `public:` / `protected:` 或完整的 `UFUNCTION(...)` 前缀残留。这样一来，像 `UBlackboardComponent::ClearValue(const FName&)` 与 `UBehaviorTreeComponent::SetDynamicSubtree(FGameplayTag, UBehaviorTree*)` 这类位于 `public:` 区域、前面紧跟 `UFUNCTION(...)` 的 `void` BlueprintCallable，解析到的“返回类型”会被污染成类似 `public: void` 的文本，最终无法和期望的 `void` 匹配。当前导出结果已经体现为 `AS_FunctionTable_SkippedEntries.csv:35-36` 的 `SetDynamicSubtree`、`ClearValue` 均被标成 `overloaded-unresolved`，而这两个 header 中都存在一个 Blueprint 版本 + 一个非 Blueprint overload。 |
| 根因 | `TryParseDeclaration()` 对 `void` 返回缺少结构化来源，只能依赖原始声明前缀；而该前缀在当前实现里没有先剥离访问标签和反射宏，导致 overload 匹配把“脏返回类型文本”拿去和期望的 `void` 比较。 |
| 影响 | 任何 `void` 返回、且同名存在辅助 overload 的 BlueprintCallable，都可能被误归类为“无法解析重载”，实际效果是 direct bind 覆盖率下降、skipped reason 失真，并把问题伪装成复杂的 overload 冲突，而不是简单的声明前缀污染。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 `void` 返回类型和非 `void` 一样走结构化判定路径，彻底摆脱对脆弱文本前缀的依赖。 |
| 具体步骤 | 1. 在 `TryParseDeclaration()` 中新增显式分支：当 `function.ReturnProperty == null` 时，直接把 `returnType` 固定设为 `"void"`，不要再调用 `CleanReturnType(prefix)`。 2. 如果后续仍需要从文本前缀推导限定符，只把该逻辑用于 `isStatic`、`isConst`、link visibility 辅助判断，禁止再参与 `void` 返回值匹配。 3. 对 `CleanReturnType()` 做降权或拆分，把它改成“仅清洗声明限定符”的辅助函数，而不是参与语义正确性的唯一来源。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加回归测试，明确断言 `UBlackboardComponent::ClearValue` 与 `UBehaviorTreeComponent::SetDynamicSubtree` 这类 `void` + overload 样本必须生成 direct binding，而不是留在 `SkippedEntries.csv`。 5. 导出后检查 `AS_FunctionTable_AIModule_*.cpp`，确认这两个函数的注册行已经生成，且 `SkippedEntries.csv` 中对应 `overloaded-unresolved` 记录消失。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果其它逻辑隐式依赖 `CleanReturnType()` 的现有输出格式，需要同时检查 `TryParseDeclaration()` 的调用方是否还有文本型返回值比较分支。 |
| 前置依赖 | 建议与 Issue-52 一并修复，共享 declaration 前缀归一化测试样本。 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 不再包含 `UBehaviorTreeComponent,SetDynamicSubtree,overloaded-unresolved` 与 `UBlackboardComponent,ClearValue,overloaded-unresolved`。 3. 检查 `AS_FunctionTable_AIModule_*.cpp` 中已生成这两个函数的 direct 绑定。 4. 跑新增自动化测试，确认 `void` overload 样本不会回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-53 | Defect | 与 Issue-52 打包处理，优先移除 `void` Blueprint overload 的误判路径 |

---

## 发现与方案 (2026-04-08 12:47)

### Issue-54：非 `void` overload 解析直接复用 UHT `ReturnProperty`，会把模板 helper overload 伪装成与真实 UFUNCTION 同签名

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerController.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:70-105, 484-504, 531-543`; `PlayerController.h:1191-1199`; `AS_FunctionTable_SkippedEntries.csv:553` |
| 问题 | `TryParseDeclaration()` 在 `function.ReturnProperty is UhtProperty` 时，不管候选 declaration 实际写了什么返回类型，都会强制使用 `BuildReturnTypeFromTokens(returnProperty)`。这在普通单声明场景下问题不大，但一旦同名 helper overload 存在，就会把不同返回类型的候选也伪装成“与 UFUNCTION 返回类型一致”。当前 `APlayerController` 正好有一个 BlueprintCallable `AHUD* GetHUD() const;`，后面紧跟一个模板便捷封装 `template<class T> T* GetHUD() const`。resolver 会把这两个候选都解析成返回 `AHUD*`、参数为空的签名，最终 `exactMatches.Count` 变成 2，产物里把 `GetHUD` 记成 `overloaded-unresolved`。`AS_FunctionTable_SkippedEntries.csv:553` 已经记录了这个误判。 |
| 根因 | overload 匹配阶段把 UHT 元数据当成候选声明自身的真相来源，而不是只把它当“期望签名”；结果候选之间本该由文本返回类型区分的差异，被提前抹平了。 |
| 影响 | 带模板 helper、便捷 cast wrapper 或其他同名非 UFUNCTION overload 的 Blueprint API，会被错误归类为 overload 冲突并丢失 direct bind。`GetHUD()` 这种零参数访问器正是典型受害者，而且这种“UFUNCTION + template helper”在 UE 代码风格里并不罕见。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 overload 判定阶段保留候选声明自己的返回类型信息，避免用 UHT `ReturnProperty` 覆盖候选间本应存在的差异。 |
| 具体步骤 | 1. 将 `TryParseDeclaration()` 拆成两层：一层从 declaration 文本解析真实 `ReturnType/ParameterTypes/qualifiers`，另一层再把该结果与 UHT 期望签名比较。 2. 对非 `void` 函数，不再直接使用 `BuildReturnTypeFromTokens(returnProperty)` 覆盖候选返回类型；改为同时保留 `parsedReturnType` 与 `expectedReturnType`，并在 `exactMatches` 比较时逐项对比。 3. 在 `FindCandidates()` 或候选模型里增加 `IsTemplateDeclaration` / `HasUFunctionMarker` 标记，对 `template<...>` helper overload 降低优先级或直接排除，避免它们参与 Blueprint UFUNCTION 的 direct-bind 匹配。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 新增 `APlayerController::GetHUD` 回归测试，断言它必须生成 direct binding，而不是留在 skipped CSV。 5. 顺带补一个“同名模板 helper 不得制造假 overload 命中”的纯解析单元测试，锁住后续回归。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接排除所有 template 候选，需要确认不会误伤少数确实由宏/模板展开出的合法声明；更稳妥的做法是让文本返回类型参与比较，而不是只靠黑名单。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 不再包含 `APlayerController,GetHUD,overloaded-unresolved`。 3. 检查 `AS_FunctionTable_Engine_*.cpp` 中已生成 `GetHUD` 的 direct 绑定行。 4. 运行新增自动化测试，确认模板 helper overload 不再污染匹配结果。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-54 | Defect | 在修完 Issue-52 / 53 后处理，清除模板 helper 造成的假 overload 冲突 |

---

## 发现与方案 (2026-04-08 12:51)

### Issue-55：UHT 输出侧绕过 `CommitOutput()` / `NoOutput` 语义，`Verify` 与 stale cull 边界失效

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtSession.cs` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:174-205, 218-265, 432-445`; `AngelscriptFunctionTableExporter.cs:99-160`; `UhtSession.cs:397-459, 533-550` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 50。当前 summary / csv sidecar 全部直接走 `File.WriteAllText()`，stale shard 也由 `DeleteStaleOutputs()` 直接 `File.Delete()`；但 UE5 UHT 官方输出路径会在 `Verify` 模式下先比较 reference，再在 `!Session.NoOutput` 时才真正写盘，并由统一 cull 逻辑清理旧文件。UHTTool 现在绕过了这套契约，只要 exporter 被调用，就可能真实改写 `Intermediate/.../UHT/`。 |
| 根因 | 插件把 `.cpp` shard 与 sidecar 维护成两套输出通道：前者使用 `factory.CommitOutput()`，后者直接操作文件系统；同时又用手写 stale 删除替代 UHT 自带的 output cull。 |
| 影响 | 在 `NoOutput`、reference/verify、`FailIfGeneratedCodeChanges` 等模式下，UHTTool 仍会落盘或删盘，导致验证流程出现脏文件、冲突文件保护失效、增量构建缓存被意外污染，并且与 UE5.x UHT exporter 约定分叉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把所有 `AS_FunctionTable_*` 产物统一纳入 `IUhtExportFactory` 输出生命周期，停止手写文件写入和 stale 删除。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableExporter.cs` 的 `[UhtExporter]` 声明中补齐 `OtherFilters = ["AS_FunctionTable_*.json", "AS_FunctionTable_*.csv"]`，让 sidecar 也进入 UHT output/cull 集合。 2. 在 `AngelscriptFunctionTableCodeGenerator.cs` 抽出统一的 `CommitArtifactOutput(IUhtExportFactory factory, string logicalName, string extension, string content)`，将 `WriteGenerationSummary()`、`WriteModuleSummaryCsv()`、`WriteEntryCsv()`、`WriteSkippedEntriesCsv()`、`WriteSkippedReasonSummaryCsv()` 全部改成 `factory.CommitOutput(...)`。 3. 删除 `DeleteStaleOutputs()` 及其调用点，把 `.cpp` stale 清理完全交给 UHT `CullOutputDirectory()`；如果确实需要保留自定义清理逻辑，也必须显式受 `!factory.Session.NoOutput` 和非 verify/reference 模式保护。 4. 把输出路径注册与 sidecar 写盘集中到一个 `GeneratedArtifactWriter`/helper 中，避免后续再次回退到 `File.WriteAllText()`。 5. 补一条 exporter 级验证脚本或测试流程，覆盖 `NoOutput` 与 verify 模式下“允许比较、不允许改写工作区”的行为。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦 `OtherFilters` 范围写得过宽，UHT cull 可能删除同目录中不属于当前 exporter 的调试文件；需要确认 `AS_FunctionTable_*` 前缀只被本 exporter 使用。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 在同一工作区先记录 `Intermediate/.../UHT/` 下 `AS_FunctionTable_*` 文件时间戳与哈希。 2. 以 `NoOutput` 和 verify/reference 模式分别运行一次 UHT exporter，确认目录内容不被直接改写。 3. 人为修改一个 sidecar 后再运行 exporter，确认差异通过 UHT 的 compare/conflict 语义暴露，而不是被静默覆盖。 4. 删除一个旧 shard 文件后重新生成，确认 stale 文件由 UHT cull 清理，且不再依赖手写 `File.Delete()`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-55 | Architecture | 优先处理，先把 UHTTool 拉回 UE5 UHT 官方输出契约，避免 verify / no-output 模式继续失真 |

---

## 发现与方案 (2026-04-08 12:52)

### Issue-56：生成侧没有复用 runtime skip 规则，`UActorComponent::GetOwner` 被错误计入 direct 覆盖

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/ActorComponent.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:490-515`; `AngelscriptBinds.cpp:83-117`; `Bind_BlueprintCallable.cpp:26-31`; `AngelscriptEngine.h:17`; `ActorComponent.h:518-527, 1534-1538`; `AS_FunctionTable_Entries.csv:412`; `AngelscriptGeneratedFunctionTableTests.cpp:269-318` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 52。UHT 生成侧 `ShouldGenerate()` 只按 metadata / header / `CustomThunk` 过滤，当前产物仍把 `UActorComponent::GetOwner` 写成 `Direct`。但 runtime `ShouldSkipBlueprintCallableFunction()` 明确把这个函数列为跳过项，而 editor 路径下 `Bind_BlueprintCallable()` 会在真正绑定前直接返回；`AS_USE_BIND_DB` 也在 `WITH_EDITOR` 下关闭，所以这是当前自动化与 UHT 生成路径实际会命中的分支。现有 `RepresentativeCoverageTest` 只断言 `UActorComponent` “至少有一个条目”，不会发现 `GetOwner` 这种死 direct entry。 |
| 根因 | UHTTool 与 runtime 各自维护“哪些 BlueprintCallable 应进入函数表”的规则，跨语言硬编码特例没有共享来源，导致生成覆盖率和真实绑定覆盖率发生漂移。 |
| 影响 | `AS_FunctionTable_Summary.json`、`AS_FunctionTable_Entries.csv` 会把本来永远不会被 runtime 绑定的函数计入 direct 覆盖，误导回归判断；后续若 runtime 再新增 skip 特例，UHTTool 会继续产生同类假阳性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 统一 UHT 生成侧与 runtime 绑定侧的 eligibility 规则，至少先消除 `GetOwner` 这类已知漂移，再把特例来源收敛到共享名单。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.cs` 把 `ShouldGenerate()` 拆成“元数据过滤”和“runtime-compatible skip 过滤”两层，先补齐 `UActorComponent::GetOwner` 的等价判定，确保它不再进入 generated entry 列表。 2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/` 下新增一个跨语言可消费的 skip rule 清单，例如 `GeneratedFunctionSkipRules.json` 或等价的简单文本资源，让 C# 生成器与 C++ runtime 都从同一份数据读取硬编码特例，而不是各写一套。 3. 让 `WriteGenerationSummary()` / `WriteEntryCsv()` 只统计最终真正允许进入函数表的 entry；必要时给 sidecar 增加 `SkippedByPolicy` 统计，区分“策略排除”与“签名失败”。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 新增负向回归：断言 `UActorComponent::GetOwner` 不出现在 `AS_FunctionTable_Entries.csv`，同时 `ClassFuncMaps` 中也不会把它作为 generated entry 暴露。 5. 顺带补一条一致性校验，比较 UHT 侧 policy skip 列表与 runtime 侧读取结果，防止未来再分叉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/GeneratedFunctionSkipRules.json`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果共享规则只覆盖少量硬编码特例，而没有把 metadata / native / interface 等既有过滤层一起建模清楚，后续仍可能出现“部分共享、部分重复”的双轨语义。 |
| 前置依赖 | 建议先完成 Issue-55，让 sidecar 变更也走统一输出契约，便于验证 policy 调整没有被旧文件污染。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `AS_FunctionTable_Entries.csv` 不再包含 `UActorComponent,GetOwner,Direct`。 2. 运行 editor 自动化，确认 `ClassFuncMaps` 中不存在由 generated function table 引入的 `GetOwner` entry。 3. 比较 `Summary.json` / `Entries.csv` 变更，确认 direct 总数下降且变化只来自 policy 修正。 4. 人为在共享 skip rule 清单中添加一个测试函数，验证 UHT 与 runtime 两侧都会同步跳过。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-56 | Defect | 在 Issue-55 之后立即处理，先消除“生成成功但 runtime 永远不绑定”的假 direct 覆盖 |

---

## 发现与方案 (2026-04-08 12:53)

### Issue-57：UHT 自动绑定没有设置 `asTRAIT_GENERATED_FUNCTION`，运行时把整批生成 glue 当成普通用户函数

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Summary.json` |
| 行号 | `Bind_BlueprintCallable.cpp:72-150`; `Helper_FunctionSignature.h:414-458`; `FunctionCallers.h:384-389`; `Bind_BlueprintType.cpp:1183-1188, 1244-1248, 1440-1441`; `Bind_UStruct.cpp:1296-1300, 1357-1361`; `as_builder.cpp:6865-6872`; `as_compiler.cpp:468-473, 593-597, 18200-18203`; `AngelscriptDebugServer.cpp:1442-1446, 1773-1778`; `AS_FunctionTable_Summary.json:1-8` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 53。`Bind_BlueprintCallable()` 在消费 UHT 生成的 `FFuncEntry` 后只调用 `Signature.ModifyScriptFunction()`，后者只写 `WorldContext`、`Deprecated`、`EditorOnly` 等属性，从不设置 `asTRAIT_GENERATED_FUNCTION`。而同仓库其它 generated accessor 路径已经显式打这个 trait。结果是当前 summary 里 `6043` 条 UHT 生成条目在脚本编译器、依赖跟踪和调试器眼里都被当成普通用户函数。 |
| 根因 | UHT function table 只把 `FuncPtr`、`Caller` 和 `bReflectiveFallbackBound` 塞进 `FFuncEntry`，没有保留“该条目来自 UHT 生成 glue”的来源语义；绑定阶段因此无法像其它 generated path 那样补齐 generated trait。 |
| 影响 | `as_builder` 会把这批函数按普通函数记 hard dependency，`as_compiler` 会额外执行 editor-only type/function 检查，`AngelscriptDebugServer` 也不会把它们从 generated frame 中剔除。后果是脚本增量编译噪声放大、调试栈污染、generated glue 与手写脚本的边界不清。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 给 `FFuncEntry` 和绑定流程补上“来源于 UHT 自动生成”的显式元数据，并在绑定成功后统一设置 `asTRAIT_GENERATED_FUNCTION`。 |
| 具体步骤 | 1. 在 `FunctionCallers.h` 为 `FFuncEntry` 新增来源字段，例如 `EGeneratedBindingOrigin` 或 `bool bGeneratedFromUhtTable`，不要继续只靠“是否来自某个调用路径”做隐式推断。 2. 在 `AngelscriptBinds.h` 增加 `AddGeneratedFunctionEntry(...)` 或扩展现有 `AddFunctionEntry(...)` 的入参，让 UHT 生成出来的 `AS_FunctionTable_<Module>_<Shard>.cpp` 明确写入该来源标记；手写 bind 保持默认 `false`。 3. 在 `Bind_BlueprintCallable.cpp` 的 direct bind 与 reflective fallback 两条成功路径上，统一调用新的 `MarkGeneratedScriptFunctionTrait(FunctionId, Entry)` helper；该 helper 内部先设置 `asTRAIT_GENERATED_FUNCTION`，再复用 `ModifyScriptFunction()` 现有的 `WorldContext` / `EditorOnly` / `Deprecated` 修饰。 4. 复查 `Bind_BlueprintType.cpp`、`Bind_UStruct.cpp` 的 generated trait 逻辑，抽成共享 helper，避免 generated trait 再出现多处手写分叉。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加回归测试，直接从脚本引擎解析一个代表性的 UHT 生成函数，断言 `asTRAIT_GENERATED_FUNCTION` 为真；同时补一条调试可见性验证，确认 generated frame 不再暴露到调试栈。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/FunctionCallers.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_UStruct.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | trait 语义一旦补齐，现有依赖分析、调试显示和 editor-only 检查结果会发生可见变化；需要确认没有下游逻辑意外依赖“UHT glue 被当成普通函数”的旧行为。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行 editor 自动化，新增测试应能拿到代表性 UHT 生成函数并断言 `asTRAIT_GENERATED_FUNCTION` 为真。 2. 手动检查 `as_builder` / `as_compiler` 相关回归场景，确认 generated glue 不再制造额外 hard dependency 或 editor-only 误报。 3. 打开调试器或相关自动化，确认 generated function frame 不再出现在普通脚本调用栈中。 4. 复查其它 generated bind 路径，确认最终都通过同一个 helper 设置 generated trait。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-57 | Architecture | 与 Issue-56 并行规划，尽快补齐 UHT 生成 glue 的 runtime 语义边界 |

---

## 发现与方案 (2026-04-08 13:00)

### Issue-58：UHT shard 已有稳定文件名却注册成 `UnnamedBind_n`，禁用配置与执行观测无法稳定映射回具体生成文件

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/AngelscriptBindExecutionObservation.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_AIModule_000.cpp`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Summary.json` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:302-306`; `AngelscriptBinds.h:440-467`; `AngelscriptBinds.cpp:138-158, 161-181, 195-214`; `AngelscriptBindExecutionObservation.cpp:48-63`; `AngelscriptBindConfigTests.cpp:368-414`; `AS_FunctionTable_AIModule_000.cpp:33`; `AS_FunctionTable_Summary.json:7` |
| 问题 | 代码生成器明明已经为每个 shard 生成了稳定的符号名 `Bind_AS_FunctionTable_<Module>_<Shard>`，但 `BuildShard()` 仍调用 `FAngelscriptBinds::FBind(int32 BindOrder, ...)` 的无名构造，当前产物 `AS_FunctionTable_AIModule_000.cpp:33` 就是这个形态。runtime 随后在 `RegisterBinds(int32, ...)` 中把它降级为 `NAME_None`，再由 `MakeUnnamedBindName()` 生成进程内自增的 `UnnamedBind_<n>`。`GetAllRegisteredBindNames()`、`GetBindInfoList()`、`CallBinds()` 与执行观测都只暴露 `BindName`，现有自动化也只验证 unnamed bind 的向后兼容行为。当前 summary 显示一次生成已有 `32` 个 shard，这意味着整批 UHT 产物在配置、观测和 bisect 时都失去了 module/shard 语义。 |
| 根因 | UHTTool 只把稳定标识写进了 C++ 变量名，没有把同一标识作为 runtime `BindName` 传入 `FAngelscriptBinds::FBind`；runtime 的 unnamed fallback 本来只是兼容兜底，却被 UHT 大批量生成产物当成正式命名层使用。 |
| 影响 | 当某个 shard 需要通过 `DisabledBindNames` 禁用、通过观测快照定位执行顺序，或和 `AS_FunctionTable_<Module>_<Shard>.cpp` 做问题对照时，日志与测试里只会看到不稳定的 `UnnamedBind_n`。只要别处新增或删除 unnamed bind，这些名字就会整体漂移，直接削弱回归定位和增量 bisect 的可操作性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 UHT 生成器把 shard 文件名对应的稳定标识直接注册成 bind name，彻底退出 `UnnamedBind_n` 回退路径。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.cs` 新增统一的 shard bind name 生成逻辑，例如 `AS_FunctionTable_<Module>_<Shard>`，并让 `BuildShard()` 改用 `FAngelscriptBinds::FBind(FName(TEXT("...")), (int32)FAngelscriptBinds::EOrder::Late + 50, [](){ ... })`。 2. 保持现有 `Bind_AS_FunctionTable_<Module>_<Shard>` 变量名与新的 runtime bind name 一致，避免“变量名稳定、配置名不稳定”的双轨状态。 3. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 或 `AngelscriptBindConfigTests.cpp` 增加回归测试，断言所有 `AS_FunctionTable_*` shard 注册后都不会出现在 `UnnamedBind_` 前缀下，且可以通过精确 bind name 被单独禁用。 4. 在执行观测或调试输出侧补一条面向 UHT shard 的断言或过滤规则，确认 snapshot 中能直接看到 `AS_FunctionTable_<Module>_<Shard>`，不再需要人工反查 unnamed 自增序号。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果已有外部配置或脚本误依赖 `UnnamedBind_n`，切换到稳定命名后需要同步迁移；不过这类依赖本身就是脆弱的，宜一次清理。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新生成 UHT shard，确认 `AS_FunctionTable_*.cpp` 中改为使用带 `BindName` 的 `FBind` 构造。 2. 在运行时枚举 `FAngelscriptBinds::GetAllRegisteredBindNames()`，确认存在 `AS_FunctionTable_AIModule_000` 等稳定名字，且不再出现对应的 `UnnamedBind_n`。 3. 用 `DisabledBindNames` 精确禁用单个 shard，确认观测快照里只缺失该 shard 的执行记录。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-58 | Architecture | 优先处理，先恢复 UHT shard 到稳定可配置的 bind identity |

---

## 发现与方案 (2026-04-08 13:01)

### Issue-59：`DevelopmentOnly` Blueprint API 进入 UHT 自动绑定后丢失 compile-out 语义，shipping/test 脚本面会比 Blueprint 更宽

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_Logging.cpp`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:490-515`; `Bind_BlueprintCallable.cpp:72-150`; `Helper_FunctionSignature.h:397-458`; `AngelscriptBinds.cpp:464-529`; `Bind_Logging.cpp:6-115`; `KismetSystemLibrary.h:129-130, 546-577`; `AS_FunctionTable_Entries.csv:2678, 2701-2705` |
| 问题 | UHT 生成链路当前只按 `BlueprintCallable/Pure`、`NotInAngelscript`、`BlueprintInternalUseOnly`、`CustomThunk` 等条件决定是否收录，完全不处理 `meta=(DevelopmentOnly)`。绑定阶段 `Bind_BlueprintCallable()` 成功后只调用 `Signature.ModifyScriptFunction()`，而该后处理目前只写 `WorldContext`、`Deprecated`、`EditorOnly` 等属性，没有任何 compile-out 逻辑。与之对比，手写日志绑定已经显式走 `CompileOutIfNoLog()`。引擎头里 `UKismetSystemLibrary::RaiseScriptError`、`LogString`、`PrintString`、`PrintText` 都明确标了 `DevelopmentOnly`，但当前 `AS_FunctionTable_Entries.csv` 仍把它们作为普通 `Stub` 条目导出。 |
| 根因 | UHT 自动绑定把 Blueprint 元数据只当成“签名修饰信息”处理，没有把 `DevelopmentOnly` 映射到 runtime 现有的 `compileOutType` 语义；因此自动绑定路径与手写 bind 路径在开发期 API 的生命周期规则上已经分叉。 |
| 影响 | 在 `UE_BUILD_TEST`、`UE_BUILD_SHIPPING` 或 editor 模拟 cooked 场景下，Blueprint 侧本应视为开发期专用的 API 仍可能继续暴露在 Angelscript 可调用面内，造成脚本与 Blueprint 语义不一致。最直接的受影响对象就是调试、打印和错误上报类函数，这些函数一旦在 cooked 脚本中继续可见，会放大行为分叉和运行时噪声。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `DevelopmentOnly` 从“被收录的普通 metadata”提升为 runtime 绑定策略，让 UHT 自动绑定与手写 bind 在 compile-out 语义上统一。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 新增 `IsFunctionDevelopmentOnly()` / `bDevelopmentOnly` 判定，直接读取 `Function->HasMetaData(TEXT("DevelopmentOnly"))`。 2. 在 `AngelscriptBinds.cpp` 新增语义明确的 helper，例如 `CompileOutDevelopmentOnly(int FunctionId)`，内部复用现有 `CompileOutInTest` 的 `CompileOutEntirely` 与 `bForceConstWithinDevelopmentOnlyFunctions` 行为，不再让调用方误用“只在 test 生效”的旧命名。 3. 在 `Bind_BlueprintCallable.cpp` 的 direct bind 与 reflective fallback 两条成功路径上，先完成 `Signature.ModifyScriptFunction(FunctionId)`，再根据 `bDevelopmentOnly` 调用新的 compile-out helper，确保两条绑定路径行为一致。 4. 评估 `AS_FunctionTable_Entries.csv` / summary 是否需要增加 `PolicyKind` 或 `DevelopmentOnly` 列，避免 sidecar 继续把这类条目伪装成普通 `Direct/Stub`。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加回归用例，至少覆盖 `UKismetSystemLibrary::PrintString` 或 `RaiseScriptError`，断言其脚本函数在 shipping/test 语义下会被 compile out，而不是作为普通可调用函数保留。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 预估工作量 | M |
| 风险 | 如果直接把所有 `DevelopmentOnly` 自动绑定都 compile out，需要确认少数项目是否曾经错误依赖旧行为；同时 sidecar 统计口径会变化，现有基线需要一次更新。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `PrintString`/`PrintText`/`RaiseScriptError` 仍可被识别，但其 runtime 绑定会落入新的 `DevelopmentOnly` compile-out 策略。 2. 在 editor 模拟 cooked 或相应自动化场景下检查脚本函数元数据，确认这些函数的 `compileOutType` 已被设置，而不是保持普通 callable 状态。 3. 与手写 `Bind_Logging.cpp` 路径做对照，确认开发期 API 在自动绑定与手写绑定之间不再分叉。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-59 | Defect | 与 Issue-57 一起优先处理，先统一 UHT 自动绑定与 runtime 对开发期 API 的生命周期语义 |

---

## 发现与方案 (2026-04-08 13:02)

### Issue-60：header resolver 已经拿到细粒度解析失败码，但 exporter 最终把它们压扁成三类粗粒度 reason，诊断信息不可执行

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:75-106, 465-506`; `AngelscriptFunctionTableExporter.cs:75-87, 99-160`; `AngelscriptGeneratedFunctionTableTests.cpp:683-748` |
| 问题 | `TryParseDeclaration()` 已能区分 `function-name`、`closing-paren`、`return-type`、`parameter-count` 等细粒度失败原因，但 `TryBuild()` 在遍历候选 declaration 时直接把这些结果用 `out _` 丢掉，只保留“是否 exact match”这个布尔结果，最终把所有失败折叠成 `unexported-symbol` 或 `overloaded-unresolved`。exporter 又把这个压扁后的 reason 直接写进 `AS_FunctionTable_SkippedEntries.csv` / `AS_FunctionTable_SkippedReasonSummary.csv`。现有自动化对此没有任何质量门槛，只验证 reason 非空和汇总计数一致。当前我对现有 skipped 产物做分组统计，实际只剩 `non-public 2359`、`unexported-symbol 1262`、`overloaded-unresolved 265` 三类输出，这说明解析层已经掌握的信息在导出层几乎全部丢失。 |
| 根因 | 诊断模型没有把“候选声明解析失败”当成一等输出对象，而是把 resolver 仅当作 yes/no 的签名恢复器使用；与此同时，exporter 和测试也没有为更细的 failure schema 留出字段与断言。 |
| 影响 | 当前任何一个 `overloaded-unresolved` 都可能混杂真实 overload 冲突、括号匹配失败、返回类型污染、参数个数解析错误等多种根因。开发者看到 skipped CSV 后仍需重新读 header 和 resolver 代码手工还原现场，错误消息不能直接指导修复，也无法稳定追踪“这轮修的是哪一类解析失败”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 resolver 的候选级失败信息提升为结构化诊断输出，统一导出到 CSV/summary，并让测试开始校验 reason schema，而不是只检查“非空字符串”。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 新增结构化结果类型，例如 `CandidateParseResult` / `SignatureResolutionFailure`，至少保存 `FailureCode`、`IsPublic`、`HasLinkableExport`、`CandidateTextSnippet`、`ParameterCount` 等字段。 2. 让 `TryBuild()` 在没有唯一 exact match 时返回“主失败码 + 候选统计”，不要继续把所有 parse failure 折叠成 `overloaded-unresolved`；例如可区分 `parse-parameter-count`、`parse-return-type`、`parse-closing-paren`、`ambiguous-overload`、`unexported-symbol`。 3. 扩展 `AngelscriptFunctionTableExporter.cs` 的 skipped CSV schema，新增 `FailureCode`、`FailureDetail` 或 `CandidateSummary` 列，并同步更新 skipped reason summary 的聚合键。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 为已知样本建立断言，不再只要求 reason 非空，而是要求特定场景产出稳定的 failure code，并验证 summary 聚合能区分这些新类别。 5. 统一控制台日志与 CSV 的 reason 命名约定，避免后续再出现 `overloaded-unresolved` 这种混合“业务冲突 + 解析异常”的模糊标签。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 调整 skipped CSV schema 后，现有读取脚本和测试需要同步迁移；如果 failure code 设计得过细且不稳定，后续 UE 头文件小改动会制造新的诊断噪声。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 为已知失败样本分别制造 `parameter-count`、`return-type`、`closing-paren` 场景，确认 skipped CSV 能输出不同 failure code，而不是统一落到 `overloaded-unresolved`。 2. 运行更新后的自动化，确认测试会对具体 reason schema 断言，而不是只接受任意非空字符串。 3. 重新统计 skipped reason summary，确认类别数明显高于当前的 3 类，并且每类都能追溯到明确的修复动作。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-60 | Refactoring | 在继续修 header parser 之前先落地，先把 skipped 诊断提升到可直接指导修复的粒度 |

---

## 发现与方案 (2026-04-08 13:10)

### Issue-61：`GENERATED_UCLASS_BODY()` 的 `public:` 宏展开没有被恢复，整类 public Blueprint API 会被批量误判成 `non-public`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/Blueprint/AIBlueprintHelperLibrary.h`, `../../UnrealEngine/UERelease/Engine/Intermediate/Build/Win64/UnrealEditor/Inc/AIModule/UHT/AIBlueprintHelperLibrary.generated.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:42-47, 438-457`; `AIBlueprintHelperLibrary.h:25-31, 44-45, 49-61, 91-95`; `AIBlueprintHelperLibrary.generated.h:53-84`; `AS_FunctionTable_SkippedEntries.csv:7-22`; `AS_FunctionTable_Entries.csv:4106-4120` |
| 问题 | resolver 只在原始 header 文本里顺序搜索字面量 `public:` / `protected:` / `private:`，默认访问级别是 `private`。`UAIBlueprintHelperLibrary` 源头文件只写了 `GENERATED_UCLASS_BODY()`，没有显式 `public:`，但其 `.generated.h` 在 legacy generated body 中明确插入了两段 `public:`。当前 UHT 产物已经证明这条路径失真：`CreateMoveToProxyObject`、`GetAIController`、`SimpleMoveToActor`、`UnlockAIResourcesWithAnimation` 等整类 API 全部落入 `non-public`，并在 entry 表里被写成 `ERASE_NO_FUNCTION()` stub。 |
| 根因 | UHTTool 自己重扫源码文本判断访问级别，没有消费 UHT 已解析出的访问语义，也没有把 `GENERATED_UCLASS_BODY()` / `GENERATED_BODY_LEGACY` 的宏展开结果纳入判定。 |
| 影响 | 任何仍依赖 legacy generated body 的 BlueprintFunctionLibrary 或 Widget 类都会整类失去 direct bind，coverage 会系统性偏低，failure reason 也会误导成“非 public”，而不是“宏展开未恢复”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用 UHT AST 的访问级别语义替代 header 文本扫描，把 `GENERATED_*BODY` 访问级别恢复成稳定语义输入。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 给 `CandidateDeclaration` 增加 `EffectiveAccess`，不要再只存 `IsPublic` 布尔值。 2. 新增 `GetEffectiveAccessSpecifier(UhtClass classObj, UhtFunction function, int nameIndex, string header, int classBodyStart)`，优先读取 `function.FunctionFlags` 里的 `Public/Protected/Private`；如果函数 flag 缺失，再回退到 `classObj.GeneratedBodyAccessSpecifier` 与文本扫描组合判定。 3. `TryBuild()` 中把 `publicCandidates` 的筛选改成“`Public` 可 direct bind，`Protected + BlueprintProtected` 可进入后续专门处理”，不要在宏未展开时直接折叠成 `non-public`。 4. 为 legacy body 单独补一个快速路径：当 class 使用 `GeneratedBodyLegacy` 时，把 class body 起始访问级别初始化为 `classObj.GeneratedBodyAccessSpecifier`，避免 `FindAccessSpecifier()` 从 `private` 起步。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加 `UAIBlueprintHelperLibrary`、`UHorizontalBox`、`UOverlay` 这类 `GENERATED_UCLASS_BODY()` 样本，断言其代表性 API 不再出现在 `SkippedEntries.csv` 的 `non-public` 分组中，并且 entry CSV 改为 direct 绑定。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接完全信任 `FunctionFlags` 而不保留文本回退，可能影响当前依赖 header 细节的候选过滤路径；需要先用回归样本覆盖 public/protected/legacy body 三种访问级别。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 中 `UAIBlueprintHelperLibrary,*` 不再批量落入 `non-public`。 3. 检查 `AS_FunctionTable_Entries.csv` 中 `GetAIController`、`SimpleMoveToActor`、`UnlockAIResourcesWithAnimation` 由 `Stub` 变为 `Direct`。 4. 运行新增自动化，确认 `GENERATED_UCLASS_BODY()`、普通显式 `public:`、`BlueprintProtected` 三类样本同时通过。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-61 | Defect | 优先修复，先消除 `GENERATED_UCLASS_BODY()` 类的整类误判与批量 stub |

---

## 发现与方案 (2026-04-08 13:11)

### Issue-62：resolver 只在 class body 内找候选，漏掉 header 末尾的 `inline Class::Function()` 定义，导致可直绑 API 被误判成 `unexported-symbol`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:29-38, 35, 52-56, 109-117, 295-314, 362-399`; `Actor.h:1570-1571, 1595-1596, 1941, 4907-4925, 4964-4984`; `AS_FunctionTable_SkippedEntries.csv:512,519-521`; `AS_FunctionTable_Entries.csv:16,54,62,85` |
| 问题 | `TryBuild()` 先用 `TryFindClassBody()` 锁定 class body，再让 `FindCandidates()` 只在 `classBodyStart..classBodyEnd` 范围内搜索声明。像 `AActor::K2_GetActorLocation`、`GetActorForwardVector`、`HasAuthority`、`GetRemoteRole` 这类函数，在 class body 里只有不带 `_API` 的 declaration，真正可链接的 `inline AActor::Foo()` 定义写在 header 末尾。当前 resolver 永远看不到这些 qualified inline definition，于是 `IsLinkVisible()` 只能基于类内 declaration 做判断，最终把它们全部写成 `unexported-symbol`，并在 entry 表里降级为 `ERASE_NO_FUNCTION()`。 |
| 根因 | 当前候选模型把“声明定位”和“链接可见性判定”都局限在 class body 内，没有第二阶段去扫描同一 header 中的 out-of-class qualified inline definition。 |
| 影响 | `Engine` 这类大量使用尾部 inline definition 的头文件会稳定丢失 direct bind 覆盖率；更糟的是 failure reason 会伪装成“未导出符号”，掩盖真实根因是候选搜索范围过窄。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 class-body 搜索之后增加“qualified inline definition 补扫”阶段，用类内 declaration 判访问级别，用类外 definition 判链接可见性和精确签名。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 新增 `FindQualifiedDefinitions(string header, string className, string functionName, int classBodyStart, int classBodyEnd)`，在 class body 外搜索 `ClassName::FunctionName(`，并要求前缀包含 `inline` / `FORCEINLINE` / `constexpr` 或函数体 `{ ... }`。 2. `TryBuild()` 在 class-body 候选全部 `unexported-symbol` 或 `declaration-missing` 时，继续用 qualified definition 参与匹配，而不是直接返回失败。 3. 访问级别仍从 class 内 declaration 或 UHT AST 获取，避免把 class 外 definition 误当成 public 判据；链接可见性则改为“definition 可见即可通过”。 4. `TryParseDeclaration()` 对 qualified definition 解析时要允许前缀包含 `AActor::` 这类限定名，并复用现有参数/返回类型比对逻辑。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加 `AActor::GetActorForwardVector`、`K2_GetActorLocation`、`HasAuthority`、`GetRemoteRole` 的回归样本，断言这些函数从 `SkippedEntries.csv` 消失并生成 direct entry。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 qualified-definition 扫描条件过宽，可能误把其它类、模板特化或宏生成的同名定义当成候选；需要把匹配严格限制在同一 header、同一 `ClassName::FunctionName`，并保留参数/返回类型精确比对。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 中 `AActor,GetActorForwardVector,unexported-symbol`、`AActor,K2_GetActorLocation,unexported-symbol`、`AActor,HasAuthority,unexported-symbol`、`AActor,GetRemoteRole,unexported-symbol` 已消失。 3. 检查 `AS_FunctionTable_Entries.csv` 对应条目从 `Stub` 变为 `Direct`。 4. 运行新增自动化，确认 class 内普通 declaration 与 class 外 inline definition 组合场景稳定通过。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-62 | Defect | 紧随 Issue-61，优先恢复 `Engine` 头文件尾部 inline definition 的 direct bind 覆盖 |

---

## 发现与方案 (2026-04-08 13:13)

### Issue-63：shard 只切函数条目不切 include 依赖，任一模块头文件改动仍会触发该模块全部 shard 重编译

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_000.cpp`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_001.cpp`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Summary.json` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:83-86, 115-121, 282-299, 449-487`; `AS_FunctionTable_Engine_000.cpp:1-20`; `AS_FunctionTable_Engine_001.cpp:1-20`; `AS_FunctionTable_Summary.json:11-18` |
| 问题 | `GenerateModule()` 先把整个模块的头文件都塞进单个 `SortedSet<string> includes`，再把这份全集原样传给每一个 shard 的 `BuildShard()`。当前产物已经体现出这种依赖爆炸：`Engine` 模块当前有 `16` 个 shard，而我对 `AS_FunctionTable_Engine_000.cpp` 和 `AS_FunctionTable_Engine_001.cpp` 做实际比对，两个文件的 include 数都为 `218`，`Compare-Object` 差异数为 `0`，说明分片只切了注册语句，没有切开编译依赖。 |
| 根因 | 生成器把 include 收集建模成“模块级全局集合”，`AngelscriptGeneratedFunctionEntry` 又没有记录自身头文件来源，导致 shard 规划阶段无法为每个 shard 计算最小 include 闭包。 |
| 影响 | 即使 Issue-49 解决了 shard 文件名稳定性，只要 `Engine` 模块内任意一个参与生成的头文件变化，所有 `AS_FunctionTable_Engine_*.cpp` 仍会因为共享同一套 `#include` 依赖而重编译，增量构建收益被大幅抵消。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 include 依赖从“模块级全集”下沉到“entry/shard 级闭包”，让 shard 的编译依赖与其函数集合保持一致。 |
| 具体步骤 | 1. 扩展 `AngelscriptGeneratedFunctionEntry`，新增 `IncludePath` 或 `OwningHeaderPath` 字段；在 `CollectEntries()` 中按函数所属 class/header 写入，而不是只把 include 塞进外部 `SortedSet`。 2. 如果采用 Issue-49 的 `ShardPlan`，在建 shard 时同时聚合当前 shard 所含 entry 的 include 集合，生成 `ShardIncludes`；如果暂未引入 manifest，也要在现有 `startIndex..entryCount` 切片后按 entry 子集回算 include。 3. `BuildShard()` 改为只消费当前 shard 的 `ShardIncludes`，保留 `CoreMinimal.h` / `AngelscriptBinds.h` 等公共前置 include，其余模块头按 shard 真实需要输出。 4. 对单类 entry 很多的场景，允许把 class 内多个函数继续复用同一个 header include，避免过度细分；但禁止把无关 class 的头继续复制到所有 shard。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 或脚本化校验中增加“修改某个早序类头文件时，仅对应 shard 的依赖命中该头”检查；至少对 `Engine` 模块验证两个 shard 的 include 列表不再完全一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptGeneratedOutputWriter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 include 裁剪过度，某些间接依赖当前恰好靠别的头“顺带编过”的 shard 会开始编译失败；需要通过全量编译和最小 include 回归校验把隐式依赖显式补齐。 |
| 前置依赖 | 建议与 Issue-49 一起实施，这样可以同时稳定 shard 内容边界与编译依赖边界。 |
| 验证方式 | 1. 重新生成 UHT shard。 2. 比较 `AS_FunctionTable_Engine_000.cpp` 与 `AS_FunctionTable_Engine_001.cpp` 的 include 列表，确认不再是 `218/218` 且完全相同。 3. 人工修改某个只被单个 shard 使用的头文件，检查受影响的编译单元数量明显下降。 4. 完整编译 `AngelscriptRuntime`，确认 include 裁剪后没有新的缺头编译错误。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-63 | Defect | 与 Issue-49 配套实施，优先把 shard 编译依赖缩到局部范围 |

---

## 发现与方案 (2026-04-08 13:20)

### Issue-64：`FindCandidates()` 会把 inline wrapper 函数体里的同名调用当成声明候选，批量制造伪 `overloaded-unresolved`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/GameplayTagQueryMixinLibrary.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:362-435`; `AngelscriptAbilityAsyncLibrary.h:18-26`; `GameplayTagQueryMixinLibrary.h:33-38`; `AngelscriptMathLibrary.h:631-691`; `AS_FunctionTable_SkippedEntries.csv:73,145-147,213`; `AS_FunctionTable_Entries.csv:5647,5823-5825,5977` |
| 问题 | `FindCandidates()` 在整个 class body 内直接搜索 `functionName + "("`，没有区分“声明区域”和“函数体内部表达式”。对 inline wrapper 而言，这会同时命中真实声明和函数体里的同名转发调用。随后 `FindDeclarationStart()` 把最近的 `{` 当起点，`FindDeclarationEnd()` 又把最近的 `;` 当终点，于是 `return UAbilityAsync_WaitAttributeChanged::WaitForAttributeChanged(...)`、`return GameplayTagQuery.GetDescription()`、`return FRotationMatrix::MakeFromX(...).ToQuat()` 这类语句都会被抽成伪候选。当前产物已经出现批量误判：`UAngelscriptAbilityAsyncLibrary::WaitForAttributeChanged`、`UGameplayTagQueryMixinLibrary::GetDescription`、`UAngelscriptFQuatLibrary::MakeFromX/MakeFromXY/MakeFromXZ` 都在 skipped CSV 中被标成 `overloaded-unresolved`，并在 entries CSV 中退化成 `Stub,ERASE_NO_FUNCTION()`。 |
| 根因 | header resolver 采用“全文本命中后反向/正向截断”的候选模型，但没有维护 class body 内部的 brace depth，也没有在候选阶段排除 `.` / `->` / `::` 调用表达式；因此函数体中的普通语句会和真实声明共享同一条提取路径。 |
| 影响 | 这会系统性打击插件内部大量 inline Blueprint wrapper 的 direct bind 覆盖率。当前 `AngelscriptRuntime` 模块里仅 `overloaded-unresolved` 就集中分布在 `UAngelscriptComponentLibrary 17`、`UAngelscriptFQuatLibrary 9`、`UAngelscriptFQuat4fLibrary 9`、`UGameplayTagQueryMixinLibrary 3` 等类上，说明它已经是成批错误，不是个别头文件噪声。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把候选提取从“类体全文本扫描”改成“仅在声明层扫描”，显式跳过函数体内部的调用表达式。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 重写 `FindCandidates()`：按字符线性扫描 `classBodyStart..classBodyEnd`，维护 `braceDepth`，只有 `braceDepth == 0` 时才允许识别新的函数候选。 2. 对命中的 `functionName` 增加前缀过滤，若前一个非空白 token 属于 `.`、`->`、`::`、`return`、`=` 等表达式上下文，则直接跳过，不再交给 `FindDeclarationStart()` / `FindDeclarationEnd()`。 3. 让 `FindDeclarationEnd()` 同时识别“声明以 `;` 结束”和“inline definition 以匹配的 `}` 结束”两种情形，避免在函数体内部第一个语句分号处提前截断。 4. 给 `CandidateDeclaration` 增加 `CandidateKind` 或 `BraceDepthAtMatch` 调试字段，并在 `Issue-60` 的细粒度诊断落地时把“statement-body-candidate”单独暴露出来，避免未来再次被折叠成泛化的 `overloaded-unresolved`。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 新增回归样本，至少覆盖 `WaitForAttributeChanged`、`GetDescription`、`MakeFromX` 与 `SetRelativeRotation` 这类“inline wrapper + 函数体同名调用”场景，要求它们生成 direct entry，而不是继续落入 skipped/stub。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 brace-depth 规则写得过窄，可能误伤类内合法的 inline definition、lambda 默认值或宏包裹声明；需要通过“声明候选”和“函数体语句”双向样本一起锁住边界。 |
| 前置依赖 | 建议与 Issue-60 配套实施，这样修复后可以直接看到新的候选过滤诊断，而不是继续被粗粒度 reason 掩盖。 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 中 `WaitForAttributeChanged`、`GetDescription`、`MakeFromX/MakeFromXY/MakeFromXZ` 的 `overloaded-unresolved` 记录已消失。 3. 检查 `AS_FunctionTable_Entries.csv` 中对应条目从 `Stub` 变为 `Direct`。 4. 运行新增自动化，确认 inline wrapper 的 direct-bind 回归能在没有手写 bind 兜底的情况下被独立拦截。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-64 | Defect | 优先修复，先消除 inline wrapper 被函数体同名调用污染后的批量假冲突 |

---

## 发现与方案 (2026-04-08 13:22)

### Issue-65：UHTTool 大量用 `ToString().Contains(...)` 判定 flags，把 UE5.x 语义层降级成脆弱的字符串协议

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/UhtFunctionParser.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtClass.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/Properties/UhtStructProperty.cs` |
| 行号 | `AngelscriptFunctionTableExporter.cs:56-63`; `AngelscriptFunctionSignatureBuilder.cs:90-132`; `AngelscriptHeaderSignatureResolver.cs:58-64, 485-539`; `AngelscriptFunctionTableCodeGenerator.cs:514`; `UhtFunctionParser.cs:324-385`; `UhtClass.cs:1861-1862, 1927-1928, 1977, 2006-2011`; `UhtStructProperty.cs:214-250` |
| 问题 | 当前 UHTTool 的关键语义判断仍依赖枚举文本格式：`IsBlueprintCallable()` 用 `function.FunctionFlags.ToString().Contains("BlueprintCallable"/"BlueprintPure")`；`SignatureBuilder` / `HeaderResolver` 用 `Contains("Static")`、`Contains("Const")` 和 `PropertyFlags.ToString().Contains("ConstParm")` 推导限定符；`ShouldGenerate()` 又用 `FunctionExportFlags.ToString().Contains("CustomThunk")` 过滤导出。与之对比，UE5 自身的 UHT 解析与校验在同一类型系统里统一使用 `HasAnyFlags` / `HasExactFlags`。这意味着 UHTTool 当前绑定的是“枚举如何格式化成字符串”，不是“flag 位是否被设置”。 |
| 根因 | 实现层没有把 UHT 强类型 flag API 当成权威入口，而是把 `ToString()` 的输出文本当成可长期依赖的协议。 |
| 影响 | 这是明显的 UE5.x 适配边界问题。只要 Epic 调整 flag 名、`ToString()` 拼接格式、组合 flag 的显示方式，或引入带相同子串的新枚举名，UHTTool 的候选筛选、`Static/Const` 判定、`CustomThunk` 过滤和 `ConstParm` 返回类型修正都可能静默漂移，而且不会在编译期暴露。当前仓库内至少已经有 `6` 处 `FunctionFlags/FunctionExportFlags.ToString().Contains(...)` 和 `3` 处 `PropertyFlags.ToString()` 相关判定，影响面不是单点。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 统一切换到 UHT 原生强类型 flag API，把语义绑定回编译期可检查的接口层。 |
| 具体步骤 | 1. 在 `AngelscriptUHTTool` 内新增统一 helper，例如 `HasAnyFunctionFlags(UhtFunction function, EFunctionFlags flags)`、`HasAnyFunctionExportFlags(UhtFunction function, UhtFunctionExportFlags flags)`、`HasAnyPropertyFlags(UhtProperty property, EPropertyFlags flags)`，禁止业务代码再直接碰 `ToString().Contains(...)`。 2. 用这些 helper 重写 `IsBlueprintCallable()`、`HasFunctionFlag()`、`BuildReturnType()` / `BuildReturnTypeFromTokens()` 中的 `ConstParm` 修正、`HeaderResolver` 的 `isStatic/isConst` 判定，以及 `ShouldGenerate()` 的 `CustomThunk` 过滤。 3. 把访问控制相关逻辑与 Issue-61 的修复合流，直接基于 `EFunctionFlags.Public/Protected/Private` 做有效访问级别判定，不再让字符串文本承担 access 语义。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加最小回归样本，至少覆盖 `BlueprintCallable/Pure`、`Static`、`Const`、`ConstParm`、`CustomThunk` 五类 flag 语义；要求这些测试依赖导出结果或 helper 返回值，而不是依赖字符串内容。 5. 增加一条静态约束：对 `Plugins/Angelscript/Source/AngelscriptUHTTool` 执行 `rg "ToString\\(\\)\\.Contains\\("` 必须为零，把这类脆弱调用从代码库层面禁掉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果某些分支当前无意中依赖了 `ToString()` 的宽松匹配，切到强类型 API 后可能会暴露既有的分类误差；需要用回归样本先锁住当前期望行为，再清理偏差。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 替换后重新运行 UHT 导出与现有自动化，确认 `BlueprintCallable/Pure`、`Static/Const`、`ConstParm`、`CustomThunk` 的行为不发生非预期回归。 2. 对 `Plugins/Angelscript/Source/AngelscriptUHTTool` 执行 `rg "ToString\\(\\)\\.Contains\\("`，确认结果为 `0`。 3. 后续 UE5.x 升级时，如果相关 API 变更，应表现为编译错误或 helper 层改动，而不是产物静默漂移。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-65 | Architecture | 在继续兼容 UE5.x UHT API 前先处理，先把 flag 语义从字符串协议收回到强类型 API |

---

## 发现与方案 (2026-04-08 13:23)

### Issue-66：模块级 sidecar 没有稳定排序，`.uhtmanifest` 顺序轻微变化就会重排整段 `Summary/CSV`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtSession.cs`, `Intermediate/Build/Win64/AngelscriptProjectEditor/Development/AngelscriptProjectEditor.uhtmanifest`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:51-75, 166-264`; `UhtSession.cs:807, 1082, 2305-2320`; `AngelscriptProjectEditor.uhtmanifest:12,1715,2047,2265,2345,2387,2802,4918,5161,7478,8874,16015,16184,24197`; `AS_FunctionTable_ModuleSummary.csv:2-15`; `AS_FunctionTable_Entries.csv:2-25` |
| 问题 | 代码生成器只对“模块内 entry”做排序，却没有对“模块列表”做任何本地归一化。`Generate()` 直接按 `factory.Session.Modules` 顺序追加 `moduleSummaries` 与 `csvEntries`，后续 `Summary.json`、`ModuleSummary.csv`、`Entries.csv` 又原样写出。UE5 的 `Session.Modules` 本质上只是 `_modules` 对 `Manifest.Modules` 的视图，而 `_modules` 在 `StepPrepareModules()` 中按 manifest 顺序逐个 `Add`。当前产物已经完全复现了这种外部顺序：manifest 是 `Engine -> Landscape -> AIModule -> ... -> AngelscriptRuntime`，`AS_FunctionTable_ModuleSummary.csv` 与 `AS_FunctionTable_Entries.csv` 也按同一顺序整段展开。 |
| 根因 | sidecar 写盘阶段把“UHT session 遍历顺序”当成了稳定输出顺序，没有在最终序列化前按内容键重新排序。 |
| 影响 | 只要 `.uhtmanifest` 的模块顺序因为 target、依赖声明位置或插件装载顺序发生变化，即使单个模块的 entry 内容完全没变，`AS_FunctionTable_ModuleSummary.csv`、`AS_FunctionTable_Entries.csv` 乃至 `Summary.json.modules` 也会出现整段重排。对当前数千行级别的 sidecar，这会制造大面积 diff 噪声，放大不必要的文件改写，并削弱增量 review 与缓存命中。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 sidecar 序列化前做稳定排序，把模块级输出顺序从 `.uhtmanifest` 外部顺序解耦出来。 |
| 具体步骤 | 1. 在 `WriteGenerationSummary()` 前构造只读有序视图：`moduleSummaries` 按 `ModuleName` 升序排序，`csvEntries` 按 `ModuleName -> ClassName -> FunctionName -> ShardIndex` 排序；排序只影响 sidecar，不改变 shard `.cpp` 的生成逻辑。 2. `Summary.json` 的 `modules` 数组、`WriteModuleSummaryCsv()` 与 `WriteEntryCsv()` 全部改用同一份稳定排序后的序列，保证三份 sidecar 的顺序契约一致。 3. 如需保留原始 manifest 顺序用于调试，新增单独字段如 `sourceModuleOrderIndex` 或 `manifestOrder`，不要继续把它当主输出顺序。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加顺序稳定性断言，至少验证 `ModuleSummary.csv` 第一列按字典序排列，`Entries.csv` 中模块块与类块也是稳定有序的。 5. 与 Issue-55 联动，把排序后的 sidecar 统一交给 `CommitOutput()`，避免“顺序抖动 + 直接写盘”叠加放大文件改写。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果有外部脚本意外依赖当前 manifest 顺序，排序后需要一次性迁移；但这种依赖本身没有稳定契约，宜尽早收口。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 调整 `.uhtmanifest` 中相关模块顺序或模拟依赖顺序变化后重新运行 UHT。 2. 确认 `AS_FunctionTable_ModuleSummary.csv` 与 `AS_FunctionTable_Entries.csv` 只在真实内容变化时发生 diff，不再因模块顺序变化整段重排。 3. 运行新增自动化，确认 sidecar 排序规则被固定下来。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-66 | Defect | 在处理完高优先级正确性问题后尽快收口，先减少 sidecar 的顺序噪声与无意义改写 |

---

## 发现与方案 (2026-04-08 13:27)

### Issue-67：`ScriptName` 别名重载仍按 `SourceName` 搜索声明，导致脚本别名函数批量落成 `Stub`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:18-40, 362-397, 467-506`; `AngelscriptComponentLibrary.h:43-53, 65-74, 113-123, 161-171, 183-213`; `AS_FunctionTable_SkippedEntries.csv:123-135`; `AS_FunctionTable_Entries.csv:5777-5785, 5799-5809` |
| 问题 | `TryBuild()` 与 `TryParseDeclaration()` 都把 `function.SourceName` 当成唯一候选名去搜索和解析声明：`FindCandidates(header, ..., function.SourceName)` 只查 `functionName + "("`，后续又要求 declaration 内必须出现 `function.SourceName + "("`。这对 `UFUNCTION(meta=(ScriptName=\"...\"))` 场景是错误的。`UAngelscriptComponentLibrary` 里 `SetRelativeRotationQuat`、`SetRelativeLocationAndRotationQuat`、`AddRelativeRotationQuat`、`SetWorldRotationQuat`、`SetWorldLocationAndRotationQuat`、`AddWorldRotationQuat` 都显式用 `ScriptName` 折叠到对应的非 `Quat` 名称，但当前导出产物仍把 `SetRelativeRotation`、`SetRelativeLocationAndRotation`、`AddRelativeRotation`、`SetWorldRotation`、`SetWorldLocationAndRotation`、`AddWorldRotation` 记成 `overloaded-unresolved + Stub`，同时把 `...Quat` 版本单独记成 `Direct`。这说明 UHTTool 既没有按脚本暴露名解析正确候选，也没有把别名函数并入脚本名维度的 direct bind。 |
| 根因 | header resolver 与函数表生成器都把“C++ 源符号名”和“脚本暴露名”混为一谈，没有从 `MetaData["ScriptName"]` 建立“反射可见名 -> 实际声明名”的映射。 |
| 影响 | 插件内部大量靠 `ScriptName` 合并重载族的 Blueprint API 会系统性丢失 direct bind，退化成 `ERASE_NO_FUNCTION()` 再依赖 runtime reflective fallback；同时 `Entries.csv` 会把实际脚本别名函数拆成一个 `Stub` 名和一个脚本侧不可见的 `...Quat` `Direct` 名，覆盖率与诊断都会失真。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“脚本暴露名”和“真实 C++ 声明名”拆成两个维度建模，按暴露名分组、按声明名解析，再统一回写到脚本名函数表。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 新增 `GetDeclarationCandidateNames(UhtFunction function)`，优先收集 `function.SourceName` 与 `function.MetaData["ScriptName"]` 相关的真实声明名；对 `ScriptName` 场景，候选搜索应允许匹配真实声明名 `SetRelativeRotationQuat(`，而不是只搜脚本暴露名 `SetRelativeRotation(`。 2. 将 `CandidateDeclaration` 扩展为同时记录 `DeclarationName` 与 `ExposedFunctionName`，`TryParseDeclaration()` 改为接收“当前正在尝试的声明名”，不要再硬编码 `function.SourceName + "("`。 3. 在 `AngelscriptFunctionSignatureBuilder.cs` 和 `AngelscriptFunctionTableCodeGenerator.cs` 中引入统一 helper，例如 `GetExposedFunctionName(UhtFunction function)`；`entries.Add(...)` 时写入脚本暴露名，而不是盲目写 `function.SourceName`。 4. 对同一 `ExposedFunctionName` 的多个真实声明名执行精确签名匹配，只允许唯一匹配的那个声明产出 direct bind；未命中的别名重载不再额外生成一个脚本侧不可见的 `...Quat` 表项。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加 `UAngelscriptComponentLibrary` 回归样本，至少覆盖 `SetRelativeRotation`、`SetRelativeLocationAndRotation`、`AddRelativeRotation`、`SetWorldRotation`、`SetWorldLocationAndRotation`、`AddWorldRotation` 六组 `ScriptName` alias；断言脚本名条目生成 `Direct`，且 `Entries.csv` 不再残留单独的 `...Quat` script-facing 记录。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果现有 runtime 某些路径意外依赖 `SourceName` 作为 key，改成脚本暴露名后可能影响手写 bind 与 UHT bind 的去重逻辑；需要同步核对 `AddFunctionEntry()` 的查表键语义。 |
| 前置依赖 | 建议与 Issue-64 一起实施，先把函数体伪候选噪声清掉，再处理 alias 重载的精确匹配。 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 中 `UAngelscriptComponentLibrary,SetRelativeRotation,overloaded-unresolved`、`SetRelativeLocationAndRotation`、`AddRelativeRotation`、`SetWorldRotation`、`SetWorldLocationAndRotation`、`AddWorldRotation` 已消失。 3. 检查 `AS_FunctionTable_Entries.csv` 中对应脚本名条目从 `Stub` 变为 `Direct`，且不再新增脚本侧不可见的重复 alias 噪声。 4. 运行新增自动化，确认 `ScriptName` alias 的 direct bind 覆盖被稳定锁住。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-67 | Defect | 紧随 Issue-64，优先恢复 `ScriptName` alias 重载在脚本暴露名维度的 direct bind |

---

## 发现与方案 (2026-04-08 13:28)

### Issue-68：exporter 的 skipped 统计没有复用真实生成过滤规则，把本就不应导出的 Blueprint helper 误报成失败

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Blueprint/AsyncTaskDownloadImage.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Blueprint/WidgetBlueprintLibrary.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Animation/WidgetAnimationPlayCallbackProxy.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv` |
| 行号 | `AngelscriptFunctionTableExporter.cs:27-44, 65-95`; `AngelscriptFunctionTableCodeGenerator.cs:490-515`; `AsyncTaskDownloadImage.h:21-23`; `WidgetBlueprintLibrary.h:32-40`; `WidgetAnimationPlayCallbackProxy.h:26-31, 40-45, 54-59, 69-72`; `AS_FunctionTable_SkippedEntries.csv:3702, 3797, 3801` |
| 问题 | 真实生成链在 `ShouldGenerate()` 里明确排除了 `BlueprintInternalUseOnly && !UsableInAngelscript`、`NotInAngelscript` 和 `CustomThunk` 等函数，但 exporter 的 `CountBlueprintCallableFunctions()` 只要看到 `BlueprintCallable/Pure` 就直接调用 `TryBuild()`，失败后写入 `SkippedEntries.csv`。结果是一些按设计根本不应该进入函数表的 Blueprint helper 被误报成 UHT 失败：`UAsyncTaskDownloadImage::DownloadImage`、`UWidgetBlueprintLibrary::Create`、`UWidgetAnimationPlayCallbackProxy::CreatePlayAnimationProxyObject` 在引擎头里都带 `BlueprintInternalUseOnly=\"true\"`，当前却分别出现在 `SkippedEntries.csv` 的 `unexported-symbol` / `non-public` 记录里。 |
| 根因 | exporter 和 code generator 没有共享统一的“可生成函数”判定入口，导致统计候选集大于真实生成集。 |
| 影响 | 当前 `AS_FunctionTable_SkippedEntries.csv` 和 `AS_FunctionTable_SkippedReasonSummary.csv` 不能被解释为“生成范围内的失败项”，而是混入了大量策略上本就应排除的 Blueprint helper。这样会污染 skipped 总量、误导优先级，并让开发者在无意义的 false positive 上浪费排查时间。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 exporter 和 generator 收敛到同一套 eligibility 规则，先定义“哪些函数应该进入 UHT 函数表”，再统计其中真正失败的项。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.cs` 抽出公共 helper，例如 `ShouldProcessFunctionForFunctionTable(UhtClass classObj, UhtFunction function)`，覆盖 `IsSupportedHeader`、`BlueprintCallable/Pure`、`NotInAngelscript`、`BlueprintInternalUseOnly`、`UsableInAngelscript`、`CustomThunk` 与现有特例白名单。 2. `GenerateModule()` 与 `CountBlueprintCallableFunctions()` 都改为调用这同一个 helper，禁止两条链路继续各自维护筛选逻辑。 3. exporter 需要新增一类可选统计字段，例如 `ExcludedByPolicyCount` / `ExcludedReasonSummary`，把“策略排除”与“签名重建失败”彻底分开，不再把前者塞进 `SkippedEntries.csv`。 4. 更新 `WriteSkippedEntriesCsv()` / `WriteSkippedReasonSummaryCsv()` 的 schema，只输出真实生成范围内的失败项；若仍需诊断被排除函数，单独新增 `AS_FunctionTable_ExcludedEntries.csv`。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加回归样本，明确断言 `DownloadImage`、`Create`、`CreatePlayAnimationProxyObject` 这类 `BlueprintInternalUseOnly` API 不会再出现在 skipped CSV 中。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果现有外部脚本已经把 `SkippedEntries.csv` 当成“全量 BlueprintCallable 无法重建表”，schema 收口后需要同步迁移这些消费者；因此最好通过新增 `ExcludedEntries.csv` 提供平滑过渡。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 不再包含 `UAsyncTaskDownloadImage,DownloadImage`、`UWidgetBlueprintLibrary,Create`、`UWidgetAnimationPlayCallbackProxy,CreatePlayAnimationProxyObject`。 3. 如果新增了 `ExcludedEntries.csv`，确认这些函数转移到该文件并带有明确的 `BlueprintInternalUseOnly` 排除原因。 4. 比较 `SkippedReasonSummary.csv`，确认统计只反映真实生成范围内的失败原因。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-68 | Defect | 在高优先级正确性问题后尽快处理，先把 skipped 诊断从 false positive 中清理出来 |

---

## 发现与方案 (2026-04-08 13:30)

### Issue-69：支持模块与 editor-only 判定重新解析 `AngelscriptRuntime.Build.cs`，把 UE5.x UHT 结构化模块语义降级成脆弱文本协议

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtModule.cs` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:334-428`; `AngelscriptRuntime.Build.cs:30-79`; `UhtModule.cs:24-31, 78-114, 189-194, 227-230` |
| 问题 | `LoadSupportedModules()` 没有使用 UHT session 已经提供的结构化模块信息，而是先通过 `ResolveRuntimeBuildCsPath()` 从某个 header 反推 `AngelscriptRuntime.Build.cs` 路径，再按文本规则重扫 `DependencyModuleNames.AddRange`、`if (Target.bBuildEditor)`、`line == \"}\"` 和引号字符串来推断支持模块与 editor-only 集合。当前之所以“能跑”，只是因为 `AngelscriptRuntime.Build.cs` 恰好仍保持 `AddRange(new string[])` 和裸 `if (Target.bBuildEditor)` 这种书写风格。与之对比，UE5.x UHT 自身已经在 `UhtModule` 上暴露了 `Module`、`Module.BaseDirectory`、`Module.ModuleType`、`Headers` 以及基于 manifest 的 header 集合准备流程。 |
| 根因 | UHTTool 没有把 UHT/UBT 已解析的模块元数据当作权威来源，而是旁路回退到 `Build.cs` 源码文本解析。 |
| 影响 | 这会把 UHTTool 的模块边界适配绑死在 `Build.cs` 的具体书写格式上。只要后续 UE5.x、插件自身或团队风格把依赖声明改成辅助方法、局部变量拼接、不同条件块结构，甚至只是调整 `Build.cs` 路径推导方式，UHTTool 就可能漏模块、错判 editor-only、或直接抛出 `Unable to locate AngelscriptRuntime.Build.cs`。这正是 UHT API 适配边界设计缺失，而不是普通实现细节。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 以 UHT manifest/session 提供的结构化模块元数据为主，删除 `Build.cs` 文本协议依赖，只把 `Build.cs` 保留为可选 provenance 输入。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.cs` 新增 `BuildSupportedModuleSet(IUhtExportFactory factory)`，优先从 `factory.Session.Modules` / `UhtModule.Module` 提取模块短名、模块类型与 header 基目录，不再通过 `ResolveRuntimeBuildCsPath()` 反推路径。 2. 将“是否 editor-only”判定改为基于 `UhtModule.Module.ModuleType` 或 manifest 中的模块类别，而不是 `if (Target.bBuildEditor)` 文本块；需要额外保留插件私有白名单时，也应放到显式配置结构里，而不是继续扫源码行。 3. 删除 `QuotedStringPattern`、`LoadSupportedModules()` 中的 `inDependencyBlock/inEditorBlock` 状态机，以及 `ResolveRuntimeBuildCsPath()` / `TryFindFirstHeaderPath()` 这套 header 反推 `Build.cs` 路径的逻辑。 4. 如果仍希望把 `Build.cs` 变更纳入增量依赖，可保留 `factory.AddExternalDependency(buildCsPath)` 这一行为，但其作用仅限于 provenance/hash，不再参与模块语义计算。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加回归校验，至少断言 `AIModule`、`UMG`、`GameplayAbilities`、`UnrealEd`、`UMGEditor` 这些当前已支持模块仍被正确分类，同时补一条“无须依赖 `Build.cs` 文本格式”的结构化 smoke test。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果当前某些模块策略实际上依赖 `Build.cs` 里的人为分组顺序，切到结构化元数据后会暴露这些隐式规则；需要在迁移前先把“支持模块集合”和“editor-only 集合”的期望结果固化到测试里。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重构后重新运行 UHT 导出，确认 `AS_FunctionTable_ModuleSummary.csv` 中支持模块集合与当前期望一致。 2. 人工调整 `AngelscriptRuntime.Build.cs` 的依赖声明排版或提取辅助函数，确认 UHTTool 输出不再受文本格式影响。 3. 运行新增自动化，确认 editor-only 模块仍被正确包裹进 `#if WITH_EDITOR`，且不再出现 `Unable to locate AngelscriptRuntime.Build.cs` 这类路径推导错误。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-69 | Architecture | 在继续扩展 UE5.x 兼容范围前先处理，先把模块边界从 `Build.cs` 文本协议收回到 UHT 结构化语义 |

---

## 发现与方案 (2026-04-08 13:39)

### Issue-70：exporter 仍把生成与 skipped 诊断串在单线程双遍流程里，无法接入 UE5.x `CreateTask()` 并发模型

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtExport.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtSession.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Exporters/CodeGen/UhtCodeGenerator.cs`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Summary.json`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv` |
| 行号 | `AngelscriptFunctionTableExporter.cs:27-44`; `AngelscriptFunctionTableCodeGenerator.cs:51-79, 449-487`; `UhtExport.cs:21-47`; `UhtSession.cs:235-263`; `UhtCodeGenerator.cs:155-241` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 45。当前 `Export()` 先同步执行 `Generate(factory)`，再同步遍历 `factory.Session.Modules` 调 `CountBlueprintCallableFunctions()`；两轮都会触发 `AngelscriptFunctionSignatureBuilder.TryBuild()` 和 header resolver。与之对比，UE5.x UHT 已经在 `IUhtExportFactory` 上提供 `CreateTask(...)`，官方 `UhtCodeGenerator` 也在 `Session.GoWide` 下按 module/header 建 task 并 `Task.WaitAll(...)`。我对当前产物实际统计得到 `6043` 条 generated entry 和 `3886` 条 skipped row，说明 UHTTool 仍在数千函数规模上串行跑双遍解析。 |
| 根因 | exporter 把“收集模块结果”“生成 shard”“产出 skipped 诊断”全部耦合在一个同步入口里，没有先抽象出可独立调度的 module-level 扫描结果，也没有对接 UHT 已提供的 task 调度能力。 |
| 影响 | 随着支持模块继续扩大，UHTTool 会越来越成为 UHT 流程里的串行瓶颈；同时它也把自己锁死在非 `GoWide` 模式，和 UE5.x 官方 exporter 的并发执行边界分叉，后续适配新 UHT API 时风险会持续升高。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把导出流程拆成“按 module 扫描一次、聚合多种产物”的任务化架构，再用 `CreateTask()` 并发执行模块扫描。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableExporter.cs` 新增 module 级中间模型，例如 `AngelscriptModuleScanResult`，统一承载该模块的 `entries`、`includes`、`skipped`、`summary` 与诊断统计，禁止 generator/exporter 各自重复扫一遍。 2. 把当前 `Generate()` 中的模块循环拆成 `ScanModule(factory, module, supportedModules, ...)`，并在 `Export()` 里按 module 调 `factory.CreateTask(...)` 生成扫描任务；任务内部只做只读 UHT 访问与 task-local 结果构建。 3. 所有 task 完成后再在主线程做确定性聚合：稳定排序、写 shard、写 sidecar、写 skipped summary，避免并发直接操作共享 `List` / `HashSet`。 4. 把 `CountBlueprintCallableFunctions()` 删除或改造成“消费 `AngelscriptModuleScanResult` 的聚合器”，确保 skipped 诊断不再触发第二轮 `TryBuild()`。 5. 参照 `UhtCodeGenerator` 的 `Session.GoWide` 行为补充一条 exporter 级 smoke test，至少覆盖 `GoWide=false/true` 两种模式下输出完全一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | 一旦并发化后仍保留现有静态可变状态，容易把性能改造变成稳定性回归；同时任务聚合顺序若不固定，会再次制造 sidecar diff 抖动。 |
| 前置依赖 | 建议先完成 Issue-72，先把 resolver cache 从非线程安全静态状态收口。 |
| 验证方式 | 1. 在 `Session.GoWide=true` 下运行 UHT exporter，确认输出与串行模式字节级一致。 2. 记录导出前后 wall-clock 时间，确认在当前 `6043 + 3886` 规模下有可见下降。 3. 检查 `AS_FunctionTable_Summary.json`、`Entries.csv`、`SkippedEntries.csv` 的排序和计数没有因为 task 聚合发生漂移。 4. 运行自动化测试，确认 module 级 task 化后没有引入新的竞态异常。 |

### Issue-71：resolver 只缓存 sanitized 文本，不缓存 class range / candidate 结果，巨型 Blueprint library 上形成稳定 O(n²) 扫描热点

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetMathLibrary.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:18-35, 180-250, 253-293, 362-399`; `AngelscriptFunctionTableExporter.cs:27-44, 65-90`; `AngelscriptFunctionTableCodeGenerator.cs:51-79, 449-487` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 49。`SanitizedHeaderCache` 只避免重复 `File.ReadAllText()`，但每次 `TryBuild()` 仍会重新做 `TryFindClassBody()` 和 `FindCandidates()`。这套流程在生成阶段与 exporter 诊断阶段各跑一遍，导致同一 header 被按函数数反复整段扫描。我对当前产物和源码做实测：`KismetMathLibrary.h` 体积 `256265` bytes、`4583` 行，当前有 `734` 条 generated entry 和 `738` 条 skipped row；`KismetSystemLibrary.h` 体积 `150148` bytes、`2386` 行，当前有 `259` 条 generated entry 和 `292` 条 skipped row；插件自己的 `AngelscriptMathLibrary.h` 也有 `87` 个 `BlueprintCallable`。这些大头文件现在都在被 resolver 重复全文扫描。 |
| 根因 | 当前缓存粒度停留在“整份 header 字符串”，没有把 class body range、声明候选索引、按函数名分组的 candidate lookup 或 resolution result 提升为可复用数据结构；同时 exporter 第二轮诊断再次调用同一套解析链。 |
| 影响 | 即使不做并发改造，UHTTool 也会把大量 CPU 时间浪费在同一 header 的重复线性扫描上；而一旦后续支持模块继续扩大，巨型 Blueprint library 会稳定放大导出耗时，削弱增量构建和日常回归的反馈速度。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 resolver 从“按函数即时文本扫描”改成“每个 header/class 解析一次、后续按索引复用”的结构化缓存模型。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 新增每轮导出的解析上下文，例如 `HeaderResolutionContext` / `ResolvedHeaderInfo`，至少缓存 `SanitizedText`、`ClassBodyRange`、`ClassDeclaration`、`Dictionary<string, List<CandidateDeclaration>>`。 2. 将 `TryFindClassBody()` 和 `FindCandidates()` 前移到“首次访问 class 时”执行，后续 `TryBuild()` 只按 `function.SourceName` 从候选索引读取，而不是再次全文 `IndexOf(...)`。 3. 在 `AngelscriptFunctionTableExporter.cs` 与 `AngelscriptFunctionTableCodeGenerator.cs` 之间共享同一个解析上下文，或直接让 exporter 复用 `Issue-70` 的 module 级扫描结果，禁止第二轮再次重跑 class 解析。 4. 给缓存增加轻量 instrumentation，例如 `ClassBodyScanCount` / `CandidateLookupHit` 计数，并在调试日志或测试里断言“同一 class 每轮最多解析一次”。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 或脚本化性能 smoke test 中加入一个大头文件基准，至少锁住 `UKismetMathLibrary` 与 `UKismetSystemLibrary` 的解析调用次数不再随函数数线性膨胀。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果缓存键只按 header path 建模、忽略 class 名或运行轮次，可能把不同 class 或不同导出阶段的结果串用；同时 instrumentation 若直接写全局状态，也会和 Issue-72 的线程安全目标冲突。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 在导出日志或测试 instrumentation 中确认同一 `UClass` 的 `TryFindClassBody()` / `FindCandidates()` 不再按函数数重复触发。 2. 对 `KismetMathLibrary`、`KismetSystemLibrary` 重新运行导出，确认 wall-clock 时间明显下降且输出内容不变。 3. 回归检查 `AS_FunctionTable_Entries.csv` 与 `SkippedEntries.csv`，确认缓存重构没有改变 direct/stub/skipped 语义。 |

### Issue-72：`SanitizedHeaderCache` 是无锁静态 `Dictionary`，一旦接入 `CreateTask()` 并发就会把 resolver 变成竞态点

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtExport.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Utils/UhtSession.cs` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:14, 180-250`; `UhtExport.cs:35-47`; `UhtSession.cs:235-263` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 51。resolver 当前把 header 文本缓存放在进程级 `static readonly Dictionary<string, string> SanitizedHeaderCache` 中，`GetSanitizedHeader()` 命中 miss 时直接执行 `TryGetValue()` + `Add()`，没有任何同步。与此同时，UE5.x UHT 的 exporter API 明确支持通过 `CreateTask(...)` 在 `Session.GoWide` 下并发执行导出任务。也就是说，当前实现不只是“还没并行化”，而是并行化前提本身就不成立。 |
| 根因 | 缓存的生命周期和并发模型没有一起设计：header cache 既是全局共享可变状态，又没有 `lock`、`ConcurrentDictionary`、`Lazy<T>` 或 task-local context 这类并发保护。 |
| 影响 | 只要后续把模块扫描或 header 解析迁到 `CreateTask()`，这个静态 cache 就可能出现重复 `Add`、并发访问异常、甚至缓存内容竞争；届时性能优化会直接演变成稳定性回归，还会让问题定位落到最核心的 resolver 上。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 去掉进程级无锁静态缓存，把解析缓存收口到“单次导出上下文 + 线程安全容器”的组合模型。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 新增显式上下文对象，例如 `HeaderResolutionContext`，由 `Export()` 或 `Generate()` 每轮创建并显式传入；不要再让 resolver 依赖隐式静态状态。 2. 如果上下文仍需跨 task 共享，使用 `ConcurrentDictionary<string, Lazy<ResolvedHeaderInfo>>` 或等价的 `GetOrAdd` 模式，确保同一路径只解析一次，且不会因并发 miss 发生重复构造。 3. 把当前 `SanitizedHeaderCache` 扩展成不可变值对象 `ResolvedHeaderInfo`，统一承载 sanitized text、class range 与 candidate index，避免以后再新增第二个静态字典。 4. 在 `AngelscriptFunctionTableExporter.cs` 中为并发/串行模式都走同一套上下文传递路径，确保 future task 化与当前串行实现使用同一缓存契约。 5. 增加一个并发 stress test 或最小化 exporter 测试，至少在同一进程内并行解析同一 header 多次，验证不会抛出异常、不会产生重复构造，也不会改变解析结果。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | resolver 方法签名会从“全静态 helper”演进成“显式上下文传递”，改动面会波及 generator/exporter；如果只把 `Dictionary` 替换成 `ConcurrentDictionary<string, string>` 而不收口生命周期，仍会保留跨轮次污染和难以测试的问题。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增并发测试，使用多个 task 同时解析同一 header，确认不会抛出 `Dictionary` 相关异常。 2. 在 `GoWide=true` 的导出模式下反复运行，确认输出稳定且缓存命中行为符合预期。 3. 检查 resolver 中不再存在无锁的进程级可变 `Dictionary`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-72 | Architecture | 先处理，先把 resolver cache 收口到线程安全/显式上下文，再谈并发化 |
| P1 | Issue-70 | Architecture | 紧随 Issue-72，按 module 任务化导出流程并消除同步双遍扫描 |
| P2 | Issue-71 | Refactoring | 在并发边界稳定后实施，继续消除巨型 header 的重复全文扫描热点 |

---

## 发现与方案 (2026-04-08 13:55)

### Issue-73：`GENERATED_UCLASS_BODY()` 注入的 `public:` 对 raw header 解析不可见，legacy UClass 会被系统性误判成 `non-public`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/Blueprint/AIBlueprintHelperLibrary.h`, `../../UnrealEngine/UERelease/Engine/Intermediate/Build/Win64/UnrealEditor/Inc/AIModule/UHT/AIBlueprintHelperLibrary.generated.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:438-456`; `AIBlueprintHelperLibrary.h:25-31, 33-97`; `AIBlueprintHelperLibrary.generated.h:53-83`; `AS_FunctionTable_SkippedEntries.csv:7-22` |
| 问题 | `FindAccessSpecifier()` 只在原始 header 文本里线性搜索字面量 `public:` / `protected:` / `private:`。但 `UAIBlueprintHelperLibrary` 在源码里只有 `GENERATED_UCLASS_BODY()`，直到 `97` 行才出现显式 `private:`，`30-95` 行的 16 个 `UFUNCTION` 前都没有显式 `public:`。对应 generated header 却在 `FID_..._GENERATED_BODY_LEGACY` 和 `..._INCLASS` 宏里两次注入 `public:`（`59`, `79`, `83` 行）。当前 resolver 看不到这些注入访问级别，于是把该类的 16 个 BlueprintCallable/Pure 全部记成 `non-public`；现有导出物 `AS_FunctionTable_SkippedEntries.csv:7-22` 已完整体现这组误报。 |
| 根因 | 访问级别推断建立在“未展开的原始 header 文本”上，没有模拟 `GENERATED_UCLASS_BODY()` / `GENERATED_UINTERFACE_BODY()` / `GENERATED_IINTERFACE_BODY()` 这类 legacy UE 宏对 access section 的注入效果。 |
| 影响 | 任何仍在使用 legacy generated-body 宏、且在首个显式 access label 之前声明 BlueprintCallable/Pure 的类，都会被批量降级成 `non-public`。这不是零星误判；`UAIBlueprintHelperLibrary` 一类的 Blueprint function library 会整类失去 direct-bind 覆盖。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 access 判定从“裸文本搜索”升级成“识别 generated-body 宏语义的 access state machine”，至少正确模拟 legacy 宏注入的 `public:`。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 新增 `FindAccessSpecifierWithGeneratedBodyAwareness()`，在扫描类体时把 `GENERATED_UCLASS_BODY()`、`GENERATED_UINTERFACE_BODY()`、`GENERATED_IINTERFACE_BODY()` 视为切换到 `public`，而 `GENERATED_BODY()` / `GENERATED_BODY_LEGACY()` 只在确认对应 generated header 片段含 `public:` 时才更新状态。 2. 为避免继续硬编码宏名，增加一个小型 `GeneratedBodyAccessPolicy` 表，把“宏名 -> 注入 access 行为”集中管理。 3. 若 `HeaderFile` 能定位到对应 `*.generated.h`，优先解析该文件里的 `..._GENERATED_BODY...` 片段，以 generated header 为准校验 access 注入行为；找不到时再回退到宏策略表。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 添加回归样本，至少断言 `UAIBlueprintHelperLibrary::GetAIController`、`SpawnAIFromClass` 不再落入 `SkippedEntries.csv` 的 `non-public`。 5. 重新导出并对比 `AS_FunctionTable_SkippedEntries.csv`，确认 `UAIBlueprintHelperLibrary` 这 16 条记录被移除，同时对应 `AS_FunctionTable_AIModule_*.cpp` 里出现 direct/stub 条目。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果把 `GENERATED_BODY()` 一概视为 `public`，会把现代 `class` 中默认私有的真实非公开 UFUNCTION 误判成可见；因此必须区分 legacy 宏和现代宏，不能简单“一刀切”。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 不再包含 `UAIBlueprintHelperLibrary` 的 16 条 `non-public`。 3. 检查 `AS_FunctionTable_AIModule_*.cpp` 已生成 `GetAIController`、`SimpleMoveToActor` 等注册行。 4. 运行新增自动化，确认 legacy generated-body 类与显式 `public:` 类都能保持正确 access 判定。 |

### Issue-74：`FindMatchingParen()` 不识别字符串字面量，`UE_DEPRECATED(...)` 文案里的 `)` 会把宏截断在半路，导致直绑函数被误判成 `unexported-symbol`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerController.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:316-356, 678-696`; `PlayerController.h:493-521`; `AS_FunctionTable_Entries.csv:324-326, 367-369`; `AS_FunctionTable_SkippedEntries.csv:552, 555` |
| 问题 | `StripLeadingMacroInvocations()` 依赖 `FindMatchingParen()` 跳过 `UE_DEPRECATED(...)`、`UFUNCTION(...)` 等前缀宏，但 `FindMatchingParen()` 只是逐字符数 `(` / `)`，完全不跳过字符串字面量。`APlayerController` 当前就有现成样本：`GetDeprecatedInputYawScale` / `PitchScale` 的 `UE_DEPRECATED` 文案不含额外 `)`，因此仍被生成为 direct entry；而 `GetDeprecatedInputRollScale`、`SetDeprecatedInputRollScale` 的文案分别是 `instead.)")`，字符串内部多了一个 `)`，结果这两条在 `AS_FunctionTable_Entries.csv:325, 368` 退化成 `ERASE_NO_FUNCTION()`，并在 `AS_FunctionTable_SkippedEntries.csv:552, 555` 被误报为 `unexported-symbol`。源码层这 6 个函数的声明结构完全一致，差异只在 deprecated message 文本，说明解析器确实被字符串里的 `)` 干扰了。 |
| 根因 | 宏匹配和括号配对逻辑没有词法层概念，把字符串字面量中的 `)`、转义字符和真实语法括号混为一谈；一旦宏参数文本本身含括号，前缀裁剪就会在错误位置截断。 |
| 影响 | 任何 `UE_DEPRECATED`、`UE_DEPRECATED_FORGAME`、`UPARAM` 或其他 function-like UE 宏，只要参数字符串里出现 `(` / `)`，都可能让 header resolver 误裁剪 declaration 前缀，进一步把 `*_API` 可见性、返回类型或 overload 解析带偏。当前已验证它会把本应 direct-bind 的 `APlayerController` API 降级成 stub。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把括号匹配升级成“忽略字符串/字符字面量与转义”的词法扫描器，并让所有宏裁剪入口复用同一实现。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 新增 `FindMatchingParenLexical()`，在扫描时维护 `inString`、`inCharLiteral`、`escapeNext` 状态，保证字符串中的 `)` 不影响 depth。 2. 用该实现替换 `StripLeadingMacroInvocations()`、`StripLeadingUparam()` 当前调用的 `FindMatchingParen()`；如果 `FindDeclarationEnd()` / `FindMatchingBrace()` 后续也会扫描 inline body，优先同步切到同一套 lexical helper，避免再出现“字符串里的 brace 把 class range 带偏”的同类问题。 3. 为 `UE_DEPRECATED` 新增最小回归样本，至少覆盖 message 中包含 `)`、`(`、转义引号三种情况。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 中增加对 `APlayerController::GetDeprecatedInputRollScale` 与 `SetDeprecatedInputRollScale` 的断言，要求它们和 Yaw/Pitch 版本一样生成 direct entry。 5. 重新导出后比较 `Entries.csv` / `SkippedEntries.csv`，确认 RollScale 两条从 `Stub + unexported-symbol` 变成 direct entry。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 括号扫描 helper 被 `StripLeadingMacroInvocations()`、`StripLeadingUparam()` 等多处共用，若状态机实现有误，可能把现有可工作的宏裁剪路径一起带坏；需要用回归样本覆盖常见 UE 宏组合。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 不再包含 `APlayerController,GetDeprecatedInputRollScale,unexported-symbol` 和 `APlayerController,SetDeprecatedInputRollScale,unexported-symbol`。 3. 检查 `AS_FunctionTable_Entries.csv` 中这两条已不再是 `ERASE_NO_FUNCTION()`。 4. 回归核对同组的 Yaw/Pitch 函数仍保持 direct entry，没有引入新的退化。 |
---

## 发现与方案 (2026-04-08 14:23)

### Issue-75：函数级 `#if WITH_EDITOR` BlueprintCallable 被错误写进 runtime shard，生成输出会直接引用 editor-only 符号

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptLevelStreamingLibrary.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptGameplayCueUtils.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_AngelscriptRuntime_001.cpp`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:282-331, 449-515`; `AngelscriptLevelStreamingLibrary.h:13-19`; `AngelscriptGameplayCueUtils.h:84-122`; `AS_FunctionTable_AngelscriptRuntime_001.cpp:41-64`; `AS_FunctionTable_Entries.csv:5898, 5914`; `AngelscriptGeneratedFunctionTableTests.cpp:242-266` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 47。当前生成器只把 `editorOnly` 建模成模块级布尔值，`BuildShard()` 仅在整个模块被标记为 editor-only 时才包 `#if WITH_EDITOR`。但 `CollectEntries()` / `ShouldGenerate()` 完全不检查函数级 editor-only 语义，导致运行时模块中的 editor-only API 被直接写进普通 shard。已验证的两个样本是 `UAngelscriptGameplayCueUtils::FindCueLoadedClassInEditor()` 与 `UAngelscriptLevelStreamingLibrary::GetShouldBeVisibleInEditor()`：源码都明确位于 `#if WITH_EDITOR` 块内，但当前生成产物 `AS_FunctionTable_AngelscriptRuntime_001.cpp` 仍在未加 guard 的 runtime shard 中输出对应 `AddFunctionEntry(...)`，`Entries.csv` 也把它们记成 `EditorOnly=false`。 |
| 根因 | UHTTool 把 editor-only 边界错误地收缩为“模块属性”，没有消费 UHT AST 或 header 结构里已经存在的函数级 editor-only 信息。 |
| 影响 | 这不是单纯的统计偏差，而是生成代码正确性问题。非编辑器目标编译这些 shard 时会直接看到 editor-only 符号引用，正确性依赖构建环境偶然兼容；同时 sidecar 会继续把这类条目伪装成 runtime 可用 API，误导增量回归与问题定位。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 editor-only 语义下沉到 entry 级别，在收集阶段就标记并分流 editor-only 函数，禁止它们落入普通 runtime shard。 |
| 具体步骤 | 1. 扩展 `AngelscriptGeneratedFunctionEntry` / `AngelscriptGeneratedFunctionCsvEntry`，新增 `bEditorOnly` 或等价字段；在 `CollectEntries()` 中基于 `UhtFunction` flag、metadata 或显式 header guard 信息为每个 entry 计算该值，而不是继续复用模块级 `editorOnly`。 2. `GenerateModule()` 改为先按 `bEditorOnly` 对 entries 分桶，再分别生成 runtime shard 与 editor-only shard；只有含 editor-only entry 的 shard 才包 `#if WITH_EDITOR`。 3. `WriteEntryCsv()` 与 `WriteGenerationSummary()` 改为输出 entry/shard 真实的 editor-only 状态，避免 `Entries.csv` 继续把函数级 editor-only API 记成 `false`。 4. 若 UHT AST 已能直接暴露函数级 editor-only flag，优先使用该结构化来源；只有拿不到时才回退到 header 文本 guard 扫描，避免再次把边界绑到字符串搜索。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 新增回归样本，明确断言 `FindCueLoadedClassInEditor` 与 `GetShouldBeVisibleInEditor` 只能出现在 `#if WITH_EDITOR` 包裹的 shard 或专用 editor-only shard 中，且 `Entries.csv` 的 `EditorOnly` 字段与之保持一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果仅靠 header 文本扫描判断 `#if WITH_EDITOR`，仍可能遗漏经其他宏间接展开的 editor-only 条件；因此迁移时应优先验证 UHT 是否已经暴露函数级 flag，并用测试锁住显式 guard 样本。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `AS_FunctionTable_AngelscriptRuntime_001.cpp` 不再在未加 guard 的上下文中注册 `FindCueLoadedClassInEditor` 与 `GetShouldBeVisibleInEditor`。 2. 检查 `AS_FunctionTable_Entries.csv`，确认这两个条目要么被标记为 `EditorOnly=true`，要么被移入专用 editor-only shard。 3. 在非编辑器目标执行一次相关编译或最小 smoke build，确认不会再因 runtime shard 引用 editor-only 符号而出错。 4. 运行新增自动化，确认现有 `UMGEditor` 模块级 guard 行为与新的函数级 guard 行为同时成立。 |

### Issue-76：`TryParseDeclaration()` 对非 `void` 候选不校验声明返回类型，函数体里的同名调用会把可直绑函数误打成 `overloaded-unresolved`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptFrameTimeMixinLibrary.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptMathLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:70-106, 465-542`; `AngelscriptFrameTimeMixinLibrary.h:13-16`; `AngelscriptMathLibrary.h:360-363, 452-454`; `AS_FunctionTable_SkippedEntries.csv:176-178`; `AS_FunctionTable_Entries.csv:5870, 5881, 5885` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 62。`TryBuild()` 先根据 UHT AST 计算 `expectedReturnType`，但 `TryParseDeclaration()` 对所有非 `void` 候选又直接从同一个 `UhtProperty` 构造 `returnType`，完全不读取候选声明前缀。这样一来，类体内像 `return Target.AsSeconds();`、`return FVector::PointPlaneProject(...);` 这样的函数体语句，只要参数个数恰好能对上，就不会因为“前缀根本不是声明返回类型”而被排除。当前已经有实证：`UAngelscriptFrameTimeMixinLibrary::AsSeconds`、`UAngelscriptFVectorMixinLibrary::PointPlaneProject`、`UAngelscriptFVector3fMixinLibrary::PointPlaneProject` 全部在 `SkippedEntries.csv` 中被记为 `overloaded-unresolved`，并在 `Entries.csv` 退化成 `Stub,ERASE_NO_FUNCTION()`。 |
| 根因 | 候选声明解析把“利用 UHT 已知返回类型生成签名”和“验证 header 文本候选是否真的是函数声明”混在了一起，导致非 `void` 路径的返回类型校验退化成自比较。 |
| 影响 | 这会系统性污染非 `void` inline wrapper 的 direct-bind 覆盖率，把本可直绑的 wrapper API 误降级成 stub。与此同时，`return-type` 失败原因在最重要的非 `void` 场景几乎失效，后续再看 skipped 诊断也无法快速定位真实根因。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将“候选声明是否合法”与“最终签名如何生成”彻底分离，对所有候选先做真实的 header 前缀解析，再与 UHT 期望类型比对。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 抽出统一的 `ParseDeclarationPrefix(...)` 或等价 helper，对 `void` 和非 `void` 路径都从候选文本前缀解析返回类型，而不是对非 `void` 直接回填 `BuildReturnTypeFromTokens(returnProperty)`。 2. `TryParseDeclaration()` 必须先从候选文本得到 `candidateReturnType`，再用现有 `NormalizeTypeText(...)` 与 `expectedReturnType` 比较；只有比较通过后，才允许用 UHT AST 参与最终 signature 构建。 3. 对包含函数体的 inline definition，解析时应在 `)` 后遇到 `{` 即停止参与“声明前缀”提取，避免 `return ...;` 这类函数体文本继续污染候选。 4. 保留现有 `const` / `&` / 空白归一化规则，避免修复返回类型误判时把合法的 `const T&` 与 `T` 差异放大成新回归。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 新增 `AsSeconds`、`PointPlaneProject`、`PointPlaneProject(FVector3f)` 三个回归样本，要求它们从 `SkippedEntries.csv` 移除并重新生成 direct entry。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果返回类型归一化过于严格，可能把目前依赖 `NormalizeTypeText()` 容忍的 `const`、引用或 typedef 差异误判成新失败；修复时必须复用既有归一化逻辑而不是重新发明一套比较规则。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `AS_FunctionTable_SkippedEntries.csv` 不再包含 `AsSeconds` 与两个 `PointPlaneProject` 的 `overloaded-unresolved`。 2. 检查 `AS_FunctionTable_Entries.csv`，确认这三条从 `Stub,ERASE_NO_FUNCTION()` 变为 direct entry。 3. 运行新增自动化，确认非 `void` wrapper 的 direct-bind 恢复，同时现有 `void` 函数解析结果不回退。 4. 复查 skipped reason summary，确认 `return-type`/`overloaded-unresolved` 的分布更符合真实根因。 |

### Issue-77：resolver 把 `FunctionName (` 当成不存在，合法空白风格会静默绕过 header 解析与导出可见性校验

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Components/ListView.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:35-40, 362-398, 465-477`; `AngelscriptFunctionSignatureBuilder.cs:43-60`; `ListView.h:118-123`; `AS_FunctionTable_Entries.csv:4510` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 11。`FindCandidates()` 只搜索 `functionName + "("`，`TryParseDeclaration()` 也只接受 `function.SourceName + "("`。但 UE 现有头文件已经包含合法的空白风格，例如 `UListView::SetScrollIntoViewAlignment` 在 `ListView.h` 中被声明为 `SetScrollIntoViewAlignment (EScrollIntoViewAlignment ...)`。按当前实现，这类声明会被 resolver 直接漏掉；随后 `AngelscriptFunctionSignatureBuilder.TryBuild()` 又会在 `declaration-missing` 等失败原因上回退到 UHT 元数据拼签名，因此当前 `Entries.csv` 仍把它生成成 direct entry，只是退化成显式 `ERASE_METHOD_PTR(...)` 路径，而不是单候选应走的 `ERASE_AUTO_METHOD_PTR(...)`。 |
| 根因 | 文本解析器把“函数名后立刻跟左括号”误当成语法规则，没有把空格、制表符和换行这类合法空白纳入 token 级扫描。 |
| 影响 | 当前已验证事实是：合法 header 风格会让 resolver 静默失效，而 fallback 会把这个失效掩盖掉。推断：一旦同类格式出现在依赖 header 可见性检查、`MinimalAPI` 边界或特殊 overload 判定的函数上，工具会在没有任何明确诊断的情况下跳过本该执行的校验，进一步放大为错误直绑或误分类。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把函数名匹配从字面量子串搜索升级成“标识符后允许任意空白，再匹配 `(`”的词法扫描，并让候选发现与声明解析共用同一定位结果。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 新增统一 helper，例如 `TryFindFunctionNameToken(...)`，先做 whole-word 名称匹配，再跳过任意空白字符定位真实 `(`；禁止继续用 `functionName + "("` 这类硬编码 marker。 2. `FindCandidates()` 与 `TryParseDeclaration()` 都改为消费同一套 `nameIndex/openParenIndex` 结果，避免一个地方支持空白、另一个地方仍按旧规则失败。 3. 保留现有 whole-word 检查，确保 `SetScrollIntoViewAlignmentExtra` 之类更长标识符不会被误匹配；同时要显式支持空格、制表符与换行三种常见排版。 4. 为避免 fallback 再次掩盖 resolver 失效，新增一个可观察断言：单一 public 候选在修复后必须重新走 auto path。对 `UListView::SetScrollIntoViewAlignment`，期望 `Entries.csv` 或生成 shard 从 `ERASE_METHOD_PTR(...)` 恢复为 `ERASE_AUTO_METHOD_PTR(UListView, SetScrollIntoViewAlignment)`。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加回归样本，至少覆盖 `FunctionName (` 风格和普通 `FunctionName(` 风格各一例，锁住两条路径都经由 resolver 成功解析。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果仅用宽松正则替换字面量搜索，容易把注释、字符串或更长标识符中的同名片段误收进候选；因此实现应坚持 token 级扫描而不是简单 `Regex` 贪婪匹配。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `SetScrollIntoViewAlignment` 仍保留 direct entry。 2. 检查对应条目从显式 `ERASE_METHOD_PTR(...)` 回到 `ERASE_AUTO_METHOD_PTR(UListView, SetScrollIntoViewAlignment)`，证明 resolver 已成功识别单候选声明。 3. 运行新增自动化，确认带空白和不带空白两种声明风格都能稳定通过。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-75 | Defect | 先处理，先阻止 runtime shard 继续直接引用 editor-only 符号 |
| P1 | Issue-76 | Defect | 紧随其后，恢复 `AsSeconds` / `PointPlaneProject` 这类非 `void` wrapper 的 direct bind 正确性 |
| P2 | Issue-77 | Defect | 在高优先级正确性问题后处理，补齐 resolver 对合法空白风格的语法容忍度 |

---

## 发现与方案 (2026-04-08 16:50)

### Issue-78：`Entries.csv` 的 `ShardIndex` 比真实 shard 文件名整体偏移 `+1`，sidecar 不能直接定位生成文件

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Engine_000.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:120-135`; `AS_FunctionTable_Entries.csv:1-3`; `AS_FunctionTable_Engine_000.cpp:221-224`; `AngelscriptGeneratedFunctionTableTests.cpp:638-665` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 37。生成器写 shard 文件时使用零基文件名 `AS_FunctionTable_<Module>_<shardIndex:D3>.cpp`，但写 `Entries.csv` 时却把同一个索引记成 `shardIndex + 1`。当前产物可直接验证这条偏移：`Entries.csv` 第 2 行把 `AActor::ActorHasTag` 记成 `ShardIndex=1`，而真实注册代码位于 `AS_FunctionTable_Engine_000.cpp:223`。这意味着 CSV 列值不能直接映射回真实产物文件。 |
| 根因 | 代码同时维护了“文件系统编号”和“展示编号”两套 shard 标识，却只在 CSV schema 中保留了后者，没有输出真实文件名，也没有在测试或文档中声明该列是 1-based ordinal。 |
| 影响 | 任何依赖 `Entries.csv` 回查具体 shard 的脚本、人工排查和后续自动化都会系统性偏到下一片，sidecar 在最常用的定位链路上提供了错误索引。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 sidecar 直接输出可回溯到真实文件的 shard 标识，消除 0-based 文件名与 1-based CSV 列值的歧义。 |
| 具体步骤 | 1. 将 `AngelscriptGeneratedFunctionCsvEntry` 的 `ShardIndex` 改成真实零基 shard 编号，或新增 `ShardFileName` / `ShardId` 字段并把 `AS_FunctionTable_<Module>_<NNN>.cpp` 全名写入 CSV。 2. 若为了人工阅读必须保留 1-based ordinal，新增单独的 `ShardOrdinal` 列，不再复用 `ShardIndex`。 3. 同步更新 `WriteEntryCsv()` 的表头与写盘逻辑，保证列名和语义一一对应。 4. 更新 `AngelscriptGeneratedFunctionTableTests.cpp`，新增“CSV 行必须能定位到真实 shard 文件”的断言，例如用 `AActor::ActorHasTag` 样本验证 `Entries.csv` 记录与 `AS_FunctionTable_Engine_000.cpp` 一致。 5. 如有依赖旧列语义的脚本，提供一次兼容迁移说明或短期双写窗口。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S |
| 风险 | 改动 CSV schema 后，现有消费 `ShardIndex` 的本地脚本或分析工具需要同步迁移。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `Entries.csv` 中 `AActor::ActorHasTag` 的 shard 标识能直接映射到 `AS_FunctionTable_Engine_000.cpp`。 3. 运行更新后的 CSV 自动化，确认不会再出现整列 `+1` 偏移。 |

### Issue-79：生成产物测试直接信任 `Intermediate` 目录，当前工作区已经出现 sidecar 缺失与源码时间线漂移

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv` |
| 行号 | `AngelscriptFunctionTableExporter.cs:43-44`; `AngelscriptGeneratedFunctionTableTests.cpp:706-749` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 40。exporter 源码当前明确会无条件写 `AS_FunctionTable_SkippedEntries.csv` 和 `AS_FunctionTable_SkippedReasonSummary.csv`，但测试只会直接读取 `Plugins/Angelscript/Intermediate/.../UHT` 下已有文件，不会触发重新导出，也不会校验这些文件是否与当前源码同一轮生成。当前工作区已出现实际漂移：`AS_FunctionTable_SkippedReasonSummary.csv` 在目标目录中不存在，而 `AngelscriptFunctionTableExporter.cs` 的最后修改时间是 `2026-04-08 10:39:32`，现有 `AS_FunctionTable_SkippedEntries.csv` 的最后修改时间却仍停留在 `2026-04-08 01:05:23`。 |
| 根因 | 测试把 `Intermediate` 目录当成可信事实来源，却没有“先生成当前 sidecar”或“至少验证产物时间戳/指纹匹配源码”的 freshness guard。 |
| 影响 | 自动化结论会被本地历史构建状态污染。最直接的后果是：源码已经要求新 sidecar 或新 schema，测试却仍可能消费旧产物，既可能产生假失败，也可能在真正缺少当前产物时给出误导性的通过/定位结果。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 sidecar 测试从“读取现成 `Intermediate` 文件”改成“先验证 freshness，再读取当前轮导出产物”。 |
| 具体步骤 | 1. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加统一 helper，例如 `ValidateGeneratedArtifactFreshness()`，至少校验 `AS_FunctionTable_Summary.json`、`AS_FunctionTable_SkippedEntries.csv`、`AS_FunctionTable_SkippedReasonSummary.csv` 全部存在。 2. 该 helper 进一步比较关键源码文件与 sidecar 的时间戳或 metadata 指纹；最小集合应包含 `AngelscriptFunctionTableExporter.cs`、`AngelscriptFunctionTableCodeGenerator.cs`。 3. 若任一必需产物缺失或早于关键源码，测试应以明确错误消息失败，例如提示“请先重新运行 UHT 导出”，而不是继续解析旧文件。 4. 更稳妥的做法是在专用测试入口中先触发一次 UHT 导出或调用现有生成脚本，再执行 CSV/JSON 断言，彻底切断对历史 `Intermediate` 状态的依赖。 5. 与 Issue-50 的 schema/provenance 元数据联动，把 `schemaVersion` 和 `sourceInputFingerprint` 作为 freshness 判定首选依据，时间戳仅作为后备方案。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 预估工作量 | M |
| 风险 | 如果只靠时间戳判断，跨机器同步或批量 checkout 可能产生误报；因此应优先引入 metadata 指纹，时间戳只作为兜底。 |
| 前置依赖 | 无；若采用指纹方案，建议与 Issue-50 一起落地。 |
| 验证方式 | 1. 在当前工作区直接运行相关测试，确认会因为缺少 `AS_FunctionTable_SkippedReasonSummary.csv` 或产物过旧而给出显式 freshness 失败，而不是继续消费旧 sidecar。 2. 重新执行一次 UHT 导出后再跑测试，确认 freshness 校验通过。 3. 人工修改 `AngelscriptFunctionTableExporter.cs` 但不重新导出，验证测试会再次拒绝旧产物。 |

### Issue-80：`WITH_EDITORONLY_DATA` 包裹的空操作 wrapper 仍被 sidecar 统计成普通 `Direct`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:100-113, 128-135, 465-477`; `AngelscriptComponentLibrary.h:239-244`; `AS_FunctionTable_Entries.csv:5811` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 63。当前生成器只按 `eraseMacro == "ERASE_NO_FUNCTION()"` 区分 `Direct/Stub`，完全不检查 inline wrapper 的目标配置行为。`UAngelscriptComponentLibrary::SetbVisualizeComponent()` 就是现成样本：`Entries.csv:5811` 把它记成 `Direct,ERASE_AUTO_FUNCTION_PTR(...)`，但源码里唯一副作用 `Component->bVisualizeComponent = bVisualize;` 被完整包在 `#if WITH_EDITORONLY_DATA` 中；离开该配置后，这个函数依然存在，却退化成静默空实现。 |
| 根因 | UHTTool 把“拿到可调用函数指针”直接等同于“该 API 在当前目标上有有效行为”，没有为 body 内的配置守卫建立任何 target-sensitive 语义分类。 |
| 影响 | 生成报表会系统性高估有效 coverage。脚本面看到的是普通 `Direct` API，但在非 `WITH_EDITORONLY_DATA` 目标里调用可能没有任何效果；这类行为退化既不会落入 `Stub`，也不会在 skipped 诊断中留下痕迹。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为生成条目补充 target-sensitive 可用性分类，把“有函数指针但被配置守卫掏空”的 wrapper 从普通 `Direct` 中分离出来。 |
| 具体步骤 | 1. 扩展 `AngelscriptGeneratedFunctionEntry` / `AngelscriptGeneratedFunctionCsvEntry`，新增 `TargetConstraint` 或 `BehaviorAvailability` 字段，至少能表达 `Unrestricted`、`RequiresEditorOnlyData` 两类状态。 2. 在 `CollectEntries()` 后引入轻量 body classifier：仅对有 inline function body 的样本扫描声明文本；当整个有效语句块都被单一 `#if WITH_EDITORONLY_DATA` 包裹时，将该 entry 标记为 `RequiresEditorOnlyData`。 3. `WriteEntryCsv()` 与 `WriteGenerationSummary()` 增加对应列/统计，把这类条目从普通 `Direct` 覆盖率中剥离，必要时单独统计为 `GuardedDirect`。 4. 若 runtime 已有可复用的 compile-out 或 trait 标记机制，则同步把该 target constraint 传递到绑定层，避免脚本 API 继续伪装成无条件可用。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加 `SetbVisualizeComponent` 回归，明确断言它不能再以普通 `Direct` 身份出现在 sidecar；同时补一个无配置守卫的普通 direct 样本，确保不会误伤。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果启发式 body 扫描写得过宽，可能把“部分语句受 guard 保护、但函数仍有有效行为”的 wrapper 误判成受限条目；因此第一版应只命中“全部副作用都在单一 guard 内”的严格模式。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `SetbVisualizeComponent` 不再以普通 `Direct` 身份出现在 `Entries.csv`。 2. 检查 summary/json/csv 已新增 target constraint 或单独统计列。 3. 运行新增自动化，确认 `WITH_EDITORONLY_DATA` 空实现样本会被识别，而普通 direct 样本保持不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-79 | Defect | 先处理，先让 sidecar 测试停止消费缺失或过期的 `Intermediate` 产物 |
| P2 | Issue-80 | Defect | 第二步处理，修正跨 target 下“空实现仍算 direct”带来的 coverage 失真 |
| P2 | Issue-78 | Defect | 第三步处理，修复 `Entries.csv` 与真实 shard 文件之间的索引映射错误 |

---

## 发现与方案 (2026-04-08 17:06)

### Issue-81：`AS_USE_BIND_DB` 路径会把 generated stub 的 reflective fallback 整批锁死，cooked/非 editor 行为与 editor 分叉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptEngine.h:17`; `Bind_BlueprintCallable.cpp:50-52, 72-90`; `Helper_FunctionSignature.h:41, 336-349`; `BlueprintCallableReflectiveFallback.cpp:374-389`; `AngelscriptGeneratedFunctionTableTests.cpp:361-426` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 33。当前 non-editor 默认启用 `AS_USE_BIND_DB`，`Bind_BlueprintCallable()` 在该路径下先执行 `Signature.InitFromDB(..., /* bInitTypes= */ false)`，随后当 `Entry->FuncPtr` 为空时仍尝试 `BindBlueprintCallableReflectiveFallback(...)`。但 `InitFromDB()` 会把 `bAllTypesValid` 直接设成 `false`，且不填充 `ArgumentTypes`；fallback 入口又硬性要求 `Signature.bAllTypesValid` 为真。结果是所有 editor 下依赖 reflective fallback 变成 callable 的 generated stub，一到 cooked/非 editor 就会被系统性拒绝。 |
| 根因 | bind-db 回放为了省掉类型重建，把“签名元数据恢复”和“fallback 所需类型信息恢复”绑成了互斥开关；而 reflective fallback 代码路径并没有接受这种“只恢复 declaration、不恢复类型”的半成品签名。 |
| 影响 | 同一份 `AS_FunctionTable_*` 产物在 editor 与 cooked 下会出现真实行为分叉：editor 可调用、cooked unresolved。现有 reflective fallback 自动化只在 `EditorContext` 下运行，无法暴露这条回归，导致生成覆盖率与 shipping 行为持续失真。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 bind-db 回放拆成“轻量元数据恢复”和“按需类型补水”两段，让 direct path 继续轻量，fallback path 必须拿到完整类型信息。 |
| 具体步骤 | 1. 在 `Bind_BlueprintCallable.cpp` 先根据 `Entry->FuncPtr.IsBound()` 计算 `bNeedsFallbackTypeInfo`；`AS_USE_BIND_DB` 路径下不要再无条件传 `false`，而是对“没有 direct native pointer”的条目改为调用新的 `Signature.HydrateTypesFromFunction()` 或 `InitFromDB(..., true)`。 2. 在 `Helper_FunctionSignature.h` 把 `InitFromDB()` 拆成“恢复 `Declaration/ClassName/ScriptName` 等 DB 字段”和“从 `UFunction` 反射恢复 `ArgumentTypes/ReturnType`”两层；`bAllTypesValid` 只允许由真实类型映射结果决定，不允许继续被调用模式硬编码成 `false`。 3. 若担心 non-editor 启动成本，把类型补水限制在 `!Entry->FuncPtr.IsBound()` 的 stub/fallback 候选上；禁止 direct path 为了这个修复付出全量成本。 4. 为 bind-db 结果补一条显式诊断：当条目命中 fallback 路径却因为类型补水失败被放弃时，在日志或 sidecar 中落一类单独 reason，例如 `db-fallback-type-hydration-failed`，不要继续静默退成 unresolved。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 新增一条强制走 bind-db 的 smoke test，或提供可在 editor 自动化中切换的测试入口；至少锁住一个当前 editor 下 `bReflectiveFallbackBound == true` 的样本在 bind-db 路径中仍然可调用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接把所有 bind-db 条目都改成类型补水，non-editor 启动时间可能明显上升；如果只在 fallback 候选上补水，则需要保证 `Entry->FuncPtr` 状态在绑定前已稳定可用。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 在可切换 `AS_USE_BIND_DB` 的测试入口下重新运行 generated binding 自动化，确认仍能得到 `ReflectiveCount > 0`。 2. 选取一个当前 editor 依赖 reflective fallback 的代表函数，在 bind-db 路径下断言 `bReflectiveFallbackBound` 仍可置真，而不是直接 unresolved。 3. 比较修复前后 non-editor 日志，确认不再出现“因 `bAllTypesValid=false` 直接拒绝 fallback”的静默路径。 |

### Issue-82：`Summary.json` / `ModuleSummary.csv` 会吞掉零产出模块，整模块回归为空时没有任何显式诊断

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Intermediate/Build/Win64/AngelscriptProjectEditor/Development/AngelscriptProjectEditor.uhtmanifest`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_ModuleSummary.csv` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:66-71, 81-90, 166-265`; `AngelscriptRuntime.Build.cs:30-78`; `AngelscriptGeneratedFunctionTableTests.cpp:459-666`; `AS_FunctionTable_ModuleSummary.csv:1-15` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 36。`GenerateModule()` 只要 `entries.Count == 0` 就返回 `null`，`Generate()` 也只把非空 summary 写进 `moduleSummaries`。结果是“本轮在 UHT session 内、且属于支持集合，但生成结果为 0 条”的模块，会从 `Summary.json` 和 `ModuleSummary.csv` 里直接消失。按当前生成器同样的 `Build.cs` 解析规则实测，支持集合共有 `32` 个模块，而当前 `ModuleSummary.csv` 只有 `14` 行；再和 `AngelscriptProjectEditor.uhtmanifest` 交叉比对后，`CoreOnline`、`CoreUObject`、`DeveloperSettings`、`EditorSubsystem`、`InputCore`、`JsonUtilities`、`NetCore`、`PhysicsCore`、`Slate`、`SlateCore` 这 `10` 个模块已在本轮 session 中，但在 summary 里完全没有 `0 entry` 记录。 |
| 根因 | 生成器把“支持模块本轮产出为零”和“该模块不在本轮处理范围内”复用了同一条 `null` 路径，导致 sidecar schema 无法表达零值模块。 |
| 影响 | 只要某个原本应有绑定的模块因为过滤条件、header 解析或 UE 版本差异整批掉成 `0`，当前 sidecar 不会留下醒目的异常行，而是伪装成“该模块从未参与生成”。这会显著拉低回归定位速度，也让自动化无法对“整模块失踪”建立断言。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“零产出”建模成显式状态而不是 `null`，让 summary 既能覆盖本轮处理模块全集，也能区分 `0 entry` 与“未进入 session”。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.cs` 让 `GenerateModule()` 始终返回 `AngelscriptModuleGenerationSummary`；当 `entries.Count == 0` 时返回 `TotalEntries=0`、`DirectBindEntries=0`、`StubEntries=0`、`ShardCount=0`，而不是 `null`。 2. `Generate()` 先构造 `sessionSupportedModules = factory.Session.Modules.Where(module => supportedModules.All.Contains(module.ShortName))`，对这组模块无条件写入 summary；`generatedFileCount` 仍只累计 `ShardCount > 0` 的真实产物。 3. 在 `Summary.json` 与 `ModuleSummary.csv` 中新增显式列/字段，例如 `InUhtSession=true`、`HasGeneratedEntries`、`ZeroEntryReason` 或 `GenerationStatus`，避免继续靠“是否存在行”隐式表达状态。 4. 对“支持集合中存在、但本轮 session 不存在”的模块单独增加一组摘要字段或 sidecar，例如 `SupportedButNotInSessionModules`，防止把 build 依赖和 session 装载状态混为一谈。 5. 更新 `AngelscriptGeneratedFunctionTableTests.cpp`，新增一条模块覆盖断言：summary 中的模块集合必须覆盖 `sessionSupportedModules`；并对当前已验证样本至少断言 `CoreUObject` 与 `SlateCore` 在存在于 session 时会以 `0 entry` 行出现。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果直接把 `Build.cs` 支持集合全部写入 summary，而不区分本轮 session 是否真的加载该模块，sidecar 会引入新的假阳性；因此必须先按 `factory.Session.Modules` 做一次交集。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `ModuleSummary.csv` 不再只有 `14` 行，而会额外出现当前已在 session 中但 `0` 产出的模块行。 2. 检查 `Summary.json`，确认新增了 `zeroEntryModuleCount` 或等价状态字段。 3. 人为让某个已有条目的模块通过过滤临时掉成 `0`，验证 sidecar 会出现显式 `0 entry` 记录，而不是整模块消失。 |

### Issue-83：skipped CSV 自动化没有按 CSV 规则解析，细粒度 `FailureReason` 一旦带逗号或引号就会被误判成坏行

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` |
| 行号 | `AngelscriptGeneratedFunctionTableTests.cpp:686-748`; `AngelscriptFunctionTableExporter.cs:124-166` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 43。exporter 写 `AS_FunctionTable_SkippedEntries.csv` 与 `AS_FunctionTable_SkippedReasonSummary.csv` 时已经实现了标准 CSV escaping：字段含逗号、引号或换行时会自动加引号并转义。但当前测试仍用 `ParseIntoArray(..., TEXT(\",\"), false)` 直接按逗号拆列，并硬断言“每行必须正好 4 列 / 2 列”。这意味着只要后续把 `FailureReason` 从当前单词 token 升级成更可执行的文本，例如带样本签名、候选数量或宏名，自动化就会先于功能本身误报失败。 |
| 根因 | writer 明确采用了合法 CSV 语义，而 reader/test 却把“当前样本恰好不含逗号”当成了格式契约，读写两端没有复用同一套解析规则。 |
| 影响 | 这会反向阻碍 `Issue-60` 一类细粒度错误消息落地。工程师为了保测试，很容易继续把 `FailureReason` 压缩成粗粒度 token，导致 skipped 诊断长期停留在“可写盘但不可执行”的状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让测试与 exporter 共享同一套 CSV 语义，优先复用 UE 自带 `FCsvParser`，彻底移除按逗号手工切列的脆弱断言。 |
| 具体步骤 | 1. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 引入 `Serialization/Csv/CsvParser.h`，新增通用 helper，例如 `ParseCsvRows(const FString& FileContents)`，统一用于 `SkippedEntries.csv` 与 `SkippedReasonSummary.csv`。 2. 用 `FCsvParser` 返回的行/列替换现有 `ParseIntoArray(..., TEXT(\",\"), false)`，并把“列数必须固定”的断言建立在解析后的 CSV row 上，而不是原始文本分割结果上。 3. 为了锁住 writer/reader 契约，在测试中新增一条带引号和逗号的合成 `FailureReason` 样本，或在导出侧临时注入可控 reason 文本，确认 exporter 写出的合法 CSV 会被测试正确读回。 4. 如果后续 `FailureReason` 要升级成结构化文本，优先在 exporter 中继续复用现有 `EscapeCsv()`，测试则只校验列头、行数和非空关键列，不再假设 reason 必须是单个 token。 5. 顺手抽一个小型 CSV 读取 helper 给 `ModuleSummary.csv` / `Entries.csv` 测试复用，避免同类手工分割逻辑在其它测试里继续扩散。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果测试只改 reader、不补带逗号/引号的回归样本，未来仍可能在别处重新引入手工切列逻辑；需要把代表性样本一起加入自动化。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 在测试中构造一个包含逗号和引号的 `FailureReason` 样本，确认 `FCsvParser` 读取后列数仍正确。 2. 重新运行 generated function table 相关自动化，确认现有 CSV 断言保持通过。 3. 人为把某条 skipped reason 改成更详细的文本，验证测试不会再因为合法 CSV 的引号/逗号而失败。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-81 | Defect | 先处理，先恢复 cooked/bind-db 路径对 generated stub 的 reflective fallback 完备性 |
| P2 | Issue-82 | Defect | 第二步处理，先让零产出模块在 summary 中显式可见，避免整模块静默失踪 |
| P2 | Issue-83 | Defect | 第三步处理，为细粒度 `FailureReason` 和 skipped CSV schema 演进清障 |

---

## 发现与方案 (2026-04-08 17:26)

### Issue-84：单候选快路径完全跳过签名核验，`ERASE_AUTO_*` 大面量直绑没有任何 UHT 侧一致性保护

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:49-67, 70-105`; `AngelscriptFunctionSignatureBuilder.cs:17-37`; `AngelscriptGeneratedFunctionTableTests.cpp:647-665, 752-776`; `AS_FunctionTable_Entries.csv:9-15` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 39。`TryBuild()` 只要看到 `1` 个 public candidate，就直接构造 `ReturnType=""`、`ParameterTypes=Array.Empty<string>()`、`UseExplicitSignature=false` 的签名对象并返回，完全不调用 `TryParseDeclaration()` 做参数/返回值核验。后续 `BuildEraseMacro()` 就固定生成 `ERASE_AUTO_METHOD_PTR/ERASE_AUTO_FUNCTION_PTR`。我对当前 `Entries.csv` 实际统计，现有 `6043` 条 generated entry 中有 `3230` 条使用 `ERASE_AUTO_*`；而现有测试对 `RunBehaviorTree`、`ReportPerceptionEvent` 也只断言“保留 direct path”，明确接受 `ERASE_AUTO_*` 或显式 `ERASE_*` 任一种结果。 |
| 根因 | resolver 把“单候选”当成可直接信任的快路径，默认把签名正确性校验后移到 C++ 模板推导或后续编译阶段；生成侧和测试侧都没有暴露“这条 direct bind 是否真的经过 header/UHT 一致性验证”。 |
| 影响 | 只要 UE5.x 头文件声明、宏展开或 typedef 细节与 UHT AST 出现漂移，UHTTool 就可能继续把函数写成 `Direct + ERASE_AUTO_*`，`SkippedEntries.csv` 和现有自动化都不会在生成阶段给出预警，问题会被后移到 C++ 编译或运行期绑定。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 取消“单候选即盲信”的快路径，让 auto path 也先经过显式签名核验，并把核验来源写进 sidecar。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 抽出统一的 `ValidateCandidateAgainstUhtSignature(...)`，把当前 overload 分支里的 `expectedParameterTypes` / `expectedReturnType` 比对逻辑复用到单候选分支。 2. 单候选场景也必须先跑 `TryParseDeclaration()`；只有当 declaration 与 UHT AST 完全匹配时，才允许保留 `UseExplicitSignature=false` 并生成 `ERASE_AUTO_*`。 3. 若 declaration 解析失败、或虽是单候选但无法完成可靠核验，则改为显式 `ERASE_METHOD_PTR/ERASE_FUNCTION_PTR` 路径，并新增细粒度 reason，例如 `single-candidate-parse-failed`，不要继续静默生成 blind auto entry。 4. 扩展 `AngelscriptGeneratedFunctionCsvEntry` 和 `WriteEntryCsv()`，新增 `ResolutionMode` 列，至少区分 `ValidatedAuto`、`ExplicitDirect`、`Stub` 三种模式，让 sidecar 能直观看出哪些条目真的经过核验。 5. 更新 `AngelscriptGeneratedFunctionTableTests.cpp`：`RunBehaviorTree`、`ReportPerceptionEvent` 这类代表样本不再只断言“还是 direct”，而要断言其 `ResolutionMode=ValidatedAuto`；同时补一个应落 `ExplicitDirect` 的复杂样本，防止 auto path 再次无约束扩散。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦单候选也做严格核验，部分当前的 `ERASE_AUTO_*` 可能回落成显式签名或 `Stub`，会带来较大产物 diff；需要通过 `ResolutionMode` 把“真实修正”与“意外回退”区分开。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `Entries.csv` 新增 `ResolutionMode` 列。 2. 统计 `ValidatedAuto` 数量，确认它与 `ERASE_AUTO_*` 行数一致，不再存在“auto 但未核验”的条目。 3. 对测试样本人为制造一个 declaration/UHT 不一致场景，确认结果会落到 `ExplicitDirect` 或明确 failure，而不是继续生成 `ERASE_AUTO_*`。 4. 运行更新后的 generated function table 自动化，确认新旧 direct 样本都能稳定通过。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-84 | Defect | 作为 resolver 核心护栏尽快处理，先阻止 blind `ERASE_AUTO_*` 继续扩大诊断盲区 |

---

## 发现与方案 (2026-04-08 17:27)

### Issue-85：overload 精确匹配抹平成员函数 `const` 语义，`const`/非 `const` 同名候选无法被唯一锁定

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/AIController.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:75-95, 171-178, 484-504`; `AIController.h:440-443`; `AS_FunctionTable_Entries.csv:4084` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 14。当前 overload 匹配只比较参数类型和 `NormalizeTypeText(expectedReturnType)`，而 `NormalizeTypeText()` 会无条件删除 `const ` 和 `&`；同时 `TryParseDeclaration()` 给候选签名填 `IsConst` 时也不是读 declaration 尾部，而是直接复用当前 `UhtFunction` 的 `FunctionFlags`。结果是 `AAIController` 里紧挨着的两条候选 `UAIPerceptionComponent* GetAIPerceptionComponent()` 与 `const UAIPerceptionComponent* GetAIPerceptionComponent() const`，在 resolver 眼里缺少能够唯一分辨的维度。当前导出物里 `GetAIPerceptionComponent` 已经仍是 `Stub,ERASE_NO_FUNCTION()`；已验证事实是这组 `const` 重载确实存在，推断是：在 Issue-52 的 `public:` / `*_API` 误判修掉后，这组 `const` 歧义会继续成为 direct bind 的下一个阻塞点。 |
| 根因 | resolver 虽然保留了 `AngelscriptFunctionSignature.IsConst` 字段，但匹配阶段完全不使用它；同时类型归一化过度激进，把本应参与 overload 区分的 `const` 语义一起抹平。 |
| 影响 | `const`/非 `const` 同名候选会被错误折叠成“看起来一样”的签名，导致可判定的 direct bind 继续落入 `overloaded-unresolved` 或 `Stub`。这类问题在修复前置的导出可见性问题后会持续残留，形成二阶回归。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让候选签名保留 declaration 自身的 `const` 语义，并在 exact match 阶段把成员函数 `const` 与返回类型 `const` 一起纳入判定。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 增加 declaration 尾部限定符解析，例如 `ParseTrailingQualifiers(...)`，在 `TryParseDeclaration()` 中从 `)` 之后读取候选自己的 `const` / `volatile` / `final` 信息，不再用 `function.FunctionFlags` 代填 `isConst`。 2. 将当前单一的 `NormalizeTypeText()` 拆成更细粒度的比较 helper；参数类型可继续做空白归一化，但返回类型和 pointee/reference qualifier 不能再一刀切删除全部 `const` / `&`。 3. 在 `TryBuild()` 的 `exactMatches` 判断中新增 `parsedSignature.IsConst == expectedIsConst` 检查，并对返回类型使用“保留语义差异”的比较规则。 4. 若修复后仍出现多候选并列，新增专门的 failure code，例如 `const-overload-ambiguous`，不要继续折叠进泛化的 `overloaded-unresolved`。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加 `AAIController::GetAIPerceptionComponent` 回归样本；该样本建议与 Issue-52 一起验证，确保 `public:`/API 宏清洗和 `const` overload 判定同时通过。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦停止无差别擦除 `const` / `&`，部分当前“侥幸匹配”的候选可能会显性暴露成新歧义；需要通过新的 failure code 区分“真实发现问题”和“修复导致误伤”。 |
| 前置依赖 | 建议与 Issue-52 联动处理；否则现有 `unexported-symbol` 会先遮蔽这条 `const` 重载问题。 |
| 验证方式 | 1. 先落地 Issue-52 后重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_Entries.csv` 中 `AAIController,GetAIPerceptionComponent` 从 `Stub` 变为 `Direct`。 3. 检查 `AS_FunctionTable_SkippedEntries.csv` 不再留下该函数的 `unexported-symbol` 或新的 `const-overload-ambiguous`。 4. 运行新增自动化，确认 `const`/非 `const` 同名样本不会再次回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-85 | Defect | 在 Issue-52 之后立即处理，避免 `const` 重载继续卡住 direct bind 恢复 |

---

## 发现与方案 (2026-04-08 17:28)

### Issue-86：`AS_USE_BIND_DB` 路径没有持久化普通 BlueprintCallable 的 `ScriptName`，cooked 会把 `DestroyActor`/`AttachToActor` 回放成原始 `K2_*`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:85-120, 336-349, 379-393`; `Bind_BlueprintCallable.cpp:50-52`; `AngelscriptBindDatabase.h:56-86, 123-129`; `AngelscriptEngine.h:17`; `Actor.h:1935-1937, 2044-2045`; `AS_FunctionTable_Entries.csv:81-83` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 32。`GetScriptNameForFunction()` 在 editor 实时绑定路径会读取 `meta=(ScriptName="...")`，并对普通 BlueprintCallable 自动剥离 `K2_` / `BP_` / `AS_` 前缀；但 non-editor 默认启用 `AS_USE_BIND_DB (!WITH_EDITOR)`，此时 `Bind_BlueprintCallable()` 直接走 `Signature.InitFromDB(...)`。问题在于 `WriteToDB()` 只在 `FUNC_BlueprintEvent` 时才保存 `DBBind.ScriptName`，`InitFromDB()` 遇到空值则退回 `InFunction->GetName()`。当前源码里 `AActor::K2_DestroyActor` 明确声明 `ScriptName = "DestroyActor"`，`K2_AttachToActor` 也声明 `ScriptName = "AttachToActor"`，但现有 `Entries.csv:81-83` 仍以原始 `K2_AttachToActor`、`K2_DestroyActor` 记账。已验证事实是 DB 回放默认不会持久化这些规范化后的脚本名；推断是在 cooked/bind-db 路径下，最终脚本 API 面会重新暴露成原始 `K2_*` 名。 |
| 根因 | bind-db schema 把 `ScriptName` 错误地视为“BlueprintEvent 才需要的附加信息”，而不是所有 generated native binding 的基础契约字段；editor 的实时命名规范化与 non-editor 的 DB 回放因此走成了两套不同来源。 |
| 影响 | editor 与 cooked 的脚本 API 面会发生分叉：editor 下可用的 `DestroyActor` / `AttachToActor`，在 non-editor 可能只剩 `K2_DestroyActor` / `K2_AttachToActor`。这会让自动化、文档和用户脚本在 cooked 才暴露找不到函数的问题，且当前 sidecar 也没有显式区分“script-facing 名”和“native UFunction 名”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“最终脚本暴露名”升级为 bind-db 的正式字段，对所有 BlueprintCallable/Pure/Event 统一持久化，并为旧 DB 提供兼容回填。 |
| 具体步骤 | 1. 在 `FAngelscriptFunctionSignature::WriteToDB()` 中去掉 `FUNC_BlueprintEvent` 限制，统一为所有 native BlueprintCallable/Pure/Event 写入解析后的 `ScriptName`。 2. 在 `InitFromDB()` 中将空 `DBBind.ScriptName` 视为 legacy 数据，回退到 `GetScriptNameForFunction(InFunction)`，不要再直接使用 `InFunction->GetName()`；这样老 DB 至少能重新走一遍前缀剥离/`ScriptName` 元数据计算。 3. 给 `FAngelscriptBindDatabase` 增加显式 schema/version 字段或文件头版本号；当发现旧版本 DB 缺失 script-facing 名时，强制触发重建，避免旧缓存长期把 `K2_*` 带进 cooked。 4. 扩展 sidecar：在 `AS_FunctionTable_Entries.csv` 或专门 metadata 文件中同时输出 `UnrealFunctionName` 与 `ScriptFacingName`，避免继续只记录 native 名，导致 generated diagnostics 无法直接反映 cooked API 面。 5. 在测试中新增 bind-db round-trip 覆盖：至少锁住 `K2_DestroyActor -> DestroyActor`、`K2_AttachToActor -> AttachToActor` 两个样本，要求 editor 实时路径与 `InitFromDB()` 回放路径得到相同的脚本名。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | bind-db 文件格式升级会影响已有缓存与预编译数据；如果没有显式版本迁移，旧 DB 会继续把错误脚本名静默带入 non-editor。 |
| 前置依赖 | 无；若同时推进 sidecar metadata/versioning，建议与 Issue-50 联动统一版本号策略。 |
| 验证方式 | 1. 先清空旧 bind-db 缓存并重建一次。 2. 在 editor 路径与强制 bind-db 回放路径分别绑定 `K2_DestroyActor`、`K2_AttachToActor`，确认最终注册的脚本函数名都为 `DestroyActor` / `AttachToActor`。 3. 检查导出的 `Entries.csv` 或新增 metadata，确认同时包含 `UnrealFunctionName=K2_DestroyActor` 与 `ScriptFacingName=DestroyActor`。 4. 运行新增自动化，确认 cooked/non-editor 不再回退到原始 `K2_*` API 面。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-86 | Defect | 尽快处理，先消除 editor 与 bind-db/cooked 的脚本命名分叉 |

---

## 发现与方案 (2026-04-08 17:50)

### Issue-87：resolver 已按完整签名识别 overload，但生成与 runtime 仍按 `Class + FunctionName` 单键落表，后续 overload 会被静默吞掉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:70-106`; `AngelscriptFunctionTableCodeGenerator.cs:14-22, 449-479`; `AngelscriptBinds.h:497-512` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 2。resolver 已经在 overload 路径里按 `expectedParameterTypes + expectedReturnType` 做 `exactMatches` 精确匹配，说明工具设计上明确想恢复同名 BlueprintCallable 的正确签名；但 code generator 最终只把 entry 建模为 `ClassName + FunctionName + EraseMacro`，`BuildRegistrationLine()` 也只把裸 `FunctionName` 传给 `FAngelscriptBinds::AddFunctionEntry(...)`。runtime 侧 `AddFunctionEntry()` 又把每个类的表存成 `TMap<FString, FFuncEntry>`，并在 `Contains(Name)` 时直接忽略后续插入。已验证事实是：解析层与存储层对 overload 的建模已经发生结构性分叉。 |
| 根因 | UHTTool 只在“候选解析阶段”保留了完整签名，但在“产物 schema”和“runtime 注册容器”阶段把键退化回单一函数名，没有为同名多签名函数预留正式表示。 |
| 影响 | 当前这条缺口被 Issue-52/53/54/85/67 一类误判部分掩盖，因为很多 overload 还没被恢复成 direct entry。推断：一旦前面的解析问题修掉，生成 shard 会开始产生多个同名 `AddFunctionEntry(Class, "Name", ...)` 调用，而 runtime 只会保留第一条，后续 overload 将被静默吞掉，导致“解析层修好了，脚本侧仍只看到一个 overload”的二阶回归。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 overload 签名升级为生成产物和 runtime 注册的正式一等键，禁止再用裸 `FunctionName` 承载多签名函数。 |
| 具体步骤 | 1. 在 `AngelscriptGeneratedFunctionEntry` / `AngelscriptGeneratedFunctionCsvEntry` 中新增 `SignatureKey` 或结构化 `ParameterTypes + ReturnType + IsConst + IsStatic` 字段，`CollectEntries()` 不再只保留 `FunctionName`。 2. 扩展 `BuildRegistrationLine()`，让 shard 生成代码调用新的 runtime API，例如 `AddFunctionEntry(UClass*, FString ScriptName, FGeneratedFunctionSignature Signature, FFuncEntry Entry)`；禁止继续只传裸名字。 3. 在 `AngelscriptBinds.h/.cpp` 把 `TMap<FString, FFuncEntry>` 升级为能表达 overload 的容器，例如 `TMap<FString, TArray<FFuncEntry>>` 或 `TMap<FString, TMap<FString, FFuncEntry>>`，其中第二层 key 必须是稳定的签名键，而不是插入顺序。 4. 让 script-facing 查找路径按“名字 + 参数形状”解析 overload；若当前调用层暂时只支持单名查找，则至少先在注册阶段显式 `ensureMsgf`/日志告警重复键，阻止静默吞掉后续 entry。 5. 同步更新 `Entries.csv` / `Summary.json` / 测试，使 sidecar 能直接显示同名多签名条目，而不是继续把它们压平到单一 `FunctionName` 行。 6. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 新增 overload round-trip 回归：选择一个修复后应同时保留两个签名的样本，断言生成产物包含两条不同 `SignatureKey`，runtime 也能同时查询到两条绑定，而不是只剩第一条。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | runtime 绑定表、脚本查找逻辑和 sidecar schema 都会一起变更；如果只修改生成器、不修改 runtime 查找层，会把“静默丢条目”变成“生成了但永远查不到”的新不一致。 |
| 前置依赖 | 建议与 Issue-67、Issue-52、Issue-53、Issue-54、Issue-85 联动推进；这些问题修复后才会真正释放出 overload 产物。 |
| 验证方式 | 1. 选择一个当前因解析误判而未恢复的 overload 样本，先修复前置问题再重新导出。 2. 检查生成 shard / `Entries.csv`，确认同一 `FunctionName` 下存在多条不同 `SignatureKey` 记录。 3. 运行 runtime 自动化，确认按不同参数形状都能命中对应绑定，而不是只保留第一条。 4. 人为制造重复裸名注册，确认 runtime 至少会输出显式错误，而不是继续静默吞掉后续 entry。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-87 | Architecture | 在继续恢复 overload direct bind 之前尽快处理，避免前置修复落地后 runtime 仍静默丢弃后续签名 |

---

## 发现与方案 (2026-04-08 17:51)

### Issue-88：overload 类型比对没有去掉 `class` / `struct` 前置声明关键字，UE 头文件常见写法会被误判成 `overloaded-unresolved`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/GameplayStatics.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:153-178, 545-600`; `GameplayStatics.h:588-592`; `AS_FunctionTable_SkippedEntries.csv:932`; `AS_FunctionTable_Entries.csv:1419` |
| 问题 | overload 比对当前只在 `NormalizeTypeText()` 里删除 `const`、`&` 和空白，并不会去掉 C++ 前置声明关键字 `class` / `struct`。与此同时，`ParseParameterTypes()` 只是剥掉参数名，像 `class UParticleSystem* EmitterTemplate` 这样的声明会原样保留成 `class UParticleSystem*`。`UGameplayStatics::SpawnEmitterAttached` 正是当前产物里的实证：头文件两个同名候选都写成 `class UParticleSystem*`、`class USceneComponent*` 参数，但 `SkippedEntries.csv:932` 仍把它记成 `overloaded-unresolved`，`Entries.csv:1419` 也退化成 `Stub,ERASE_NO_FUNCTION()`。 |
| 根因 | resolver 把 UHT 期望类型文本和 header 声明类型文本拿来做字符串级正规化比较，但正规化规则没有覆盖 UE 代码里极常见的 forward-declaration 语法糖。 |
| 影响 | 任何同名 overload 里只要参数或返回类型使用了 `class UFoo*` / `struct FBar` 这类写法，真实可区分的 Blueprint 候选都可能被错误排除。当前 `SpawnEmitterAttached` 已经是直接受害者；推断：随着 Issue-52/53/54/85 修复更多 overload 样本，这类前置声明关键字会持续制造新的假 `overloaded-unresolved`。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把类型比较从“弱字符串清洗”升级成“先移除 C++ 声明修饰词，再做语义等价比较”，至少先补齐 `class` / `struct` 关键字。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 新增 `NormalizeDeclarationTypeText()`，在现有 `const` / `&` / 空白清洗前，显式移除参数和返回类型中的 `class `、`struct `、`enum ` 前缀，并保留 `TSubclassOf<class UUserWidget>` 这类模板内部关键字处理。 2. `ParseParameterTypes()` / `StripTrailingIdentifier()` 返回的候选类型不要直接进入 `AreTypesEquivalent()`；先经过新的 declaration-specific normalizer，再与 UHT `AppendFullDecl()` 结果比较。 3. 对返回类型同步应用同一规则，避免后续出现 `class UTexture*` 返回值在 overload 精确匹配里继续失真。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加 `UGameplayStatics::SpawnEmitterAttached` 回归测试，明确断言该函数必须从 `SkippedEntries.csv` 消失并恢复 direct entry。 5. 额外补一个模板参数样本，例如 `TSubclassOf<class UUserWidget>` 或 `TArray<class UPrimitiveComponent*>`，锁住模板内部前置声明关键字也会被统一归一。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果一刀切地删除所有 `class` / `struct` token，可能误伤模板实参或注释化片段中的普通标识符；实现时应限定为 token 级关键字剥离，而不是全局字符串替换。 |
| 前置依赖 | 无；但建议与 Issue-89 一起验证，因为 `SetSkinnedAssetAndUpdate` 这类样本同时受前置声明关键字和宏字符串扫描影响。 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 不再包含 `UGameplayStatics,SpawnEmitterAttached,overloaded-unresolved`。 3. 检查 `AS_FunctionTable_Entries.csv` 中 `SpawnEmitterAttached` 从 `Stub` 变为 `Direct`。 4. 运行新增自动化，确认普通声明、带 `class` / `struct` 前置声明、以及模板内部前置声明三种写法都能稳定匹配。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-88 | Defect | 优先处理，先清掉 UE 常见前置声明写法导致的假 `overloaded-unresolved` |

---

## 发现与方案 (2026-04-08 17:52)

### Issue-89：候选扫描会命中 `UE_DEPRECATED` 等宏字符串里的 `FunctionName()` 文案，凭空制造 phantom overload

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/SkinnedMeshComponent.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:180-250, 362-398`; `SkinnedMeshComponent.h:1120-1130`; `AS_FunctionTable_SkippedEntries.csv:2464`; `AS_FunctionTable_Entries.csv:3588` |
| 问题 | `GetSanitizedHeader()` 当前只删除注释，不会屏蔽字符串字面量；随后 `FindCandidates()` 又直接在整个 class body 里做 `header.IndexOf(functionName + "(")`。这意味着只要 UE 宏参数字符串里出现 `FunctionName()`，scanner 就会把文案当成真实 declaration 命中。`USkinnedMeshComponent::SetSkinnedAssetAndUpdate` 是当前实证：class body 里真正的 UFUNCTION 声明只有 `1130` 这一处，但前面的 `UE_DEPRECATED(5.1, "Use USkeletalMeshComponent::SetSkinnedAssetAndUpdate() instead.")` 文案在 `1120` 就已经包含同名 `SetSkinnedAssetAndUpdate()`。当前导出结果因此把它记成 `overloaded-unresolved`，并在 `Entries.csv:3588` 退化成 `Stub`。 |
| 根因 | resolver 只做了“去注释”，没有建立字符串/字符字面量屏蔽层；候选发现阶段因此把宏文案、metadata 字符串和真实声明放在同一文本平面上搜索。 |
| 影响 | 这不是和 Issue-74 重复的括号匹配问题，而是更早一步的“候选集污染”问题。任何 `UE_DEPRECATED`、`DeprecationMessage`、`DisplayName`、`ToolTip` 等字符串，只要写入了同名函数加括号的文案，都可能把单声明函数伪装成多候选 overload，进而触发错误的 `overloaded-unresolved` 或把 auto path 降成 stub。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 header 预处理阶段补齐字符串/字符字面量屏蔽，并让候选扫描只在“可解析声明文本”上运行。 |
| 具体步骤 | 1. 在 `GetSanitizedHeader()` 中新增 `inStringLiteral` / `inCharLiteral` / `isEscaped` 状态机，像处理注释一样把字符串和字符字面量内容替换成等长空格，同时保留换行位置。 2. `FindCandidates()` 禁止继续直接对原始 sanitized 文本做 `IndexOf(functionName + "(")`；改为仅在“非注释、非字符串、非函数体表达式”的 token 流上做搜索。 3. 为避免和 Issue-74 重复返工，把 `FindMatchingParen()`、`FindDeclarationEnd()` 也统一改成复用同一个 tokenized/sanitized buffer，而不是各自重新假设字符串不存在。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加 `SetSkinnedAssetAndUpdate` 回归，明确断言它不能因为 `UE_DEPRECATED` 文案里的同名文本而进入 `SkippedEntries.csv`。 5. 再补一个纯解析负样本：构造带 `DeprecationMessage="Use Foo() instead"` 的测试声明，验证 resolver 只返回真实 `Foo` 声明，不会返回文案伪候选。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果字符串屏蔽只处理普通双引号，漏掉转义引号、字符字面量或 UE 宏里跨行字符串拼接，仍会留下候选污染；应把状态机一次做到 declaration parser 共用。 |
| 前置依赖 | 无；建议与 Issue-74、Issue-88 联合验证，因为三者都落在“UE 宏 + 文本解析”边界。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `AS_FunctionTable_SkippedEntries.csv` 不再包含 `USkinnedMeshComponent,SetSkinnedAssetAndUpdate,overloaded-unresolved`。 2. 检查 `AS_FunctionTable_Entries.csv`，确认该函数从 `Stub` 变为 direct entry。 3. 运行新增自动化，确认带 `UE_DEPRECATED` / `DeprecationMessage` 文案的样本不会再制造 phantom candidate。 4. 回归检查 Issue-74 相关样本，确认字符串屏蔽后括号匹配和候选搜索都稳定。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-89 | Defect | 与 Issue-74、Issue-88 连续处理，先切断 UE 宏字符串对候选集的污染 |

---

## 发现与方案 (2026-04-08 18:11)

### Issue-90：resolver 完全忽略 UHT 已提供的源码锚点，仍以整类全文扫描重建声明边界

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtFunction.cs`, `../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtType.cs`, `../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtClass.cs` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:28-35, 253-293, 362-398`; `AngelscriptFunctionTableCodeGenerator.cs:449-487`; `UhtFunction.cs:274-277`; `UhtType.cs:873-881`; `UhtClass.cs:373-381` |
| 问题 | `TryBuild()` 先把整份 header 读成字符串，再让 `TryFindClassBody()` 扫整头文件、`FindCandidates()` 扫整个 class body 的 `functionName + "("`；这条链路完全没有使用 UHT 已经解析好的 `MacroLineNumber`、`LineNumber`、`DefineScope`、`GeneratedBodyLineNumber`、`GeneratedBodyAccessSpecifier`。当前实现等于在 UHT AST 旁边又复制了一套“从磁盘源码反推声明位置”的 parser，导致候选搜索既不知道 UHT 真正命中的宏行，也不知道函数所属的 define scope。 |
| 根因 | UHTTool 与 UE5.x UHT API 的适配边界放错了位置：它把 UHT 产出的结构化源码锚点当成无关信息，继续把 raw header 文本扫描当成主数据源。 |
| 影响 | 这会同时放大三类问题：1. 正确性上，任意同名字符串、inline wrapper、宏文案和 out-of-class definition 都会进入同一候选平面；2. 架构上，`WITH_EDITOR` / tests / 其他 define scope 只能靠额外的文本猜测补救，无法直接继承 UHT 的分支判定；3. 性能上，同一类的每个函数都要重复全文扫描，巨型 header 会持续成为导出热点。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 resolver 改成“UHT 锚点驱动的局部解析器”，先消费 `LineNumber/MacroLineNumber/DefineScope`，只在锚点附近做最小文本补偿。 |
| 具体步骤 | 1. 在 `AngelscriptUHTTool` 中新增 `HeaderSourceMap`，一次性把 header 文本建立成 `line -> byte offset` 索引，避免后续每个函数都从头扫描。 2. 为每个 `UhtFunction` 构造 `FunctionSourceAnchor`，至少包含 `HeaderPath`、`MacroLineNumber`、`LineNumber`、`DefineScope`、`OwningClassGeneratedBodyLineNumber`。 3. `TryBuild()` 改为先在 `MacroLineNumber` 附近的有限窗口内搜索 `UFUNCTION(...)` 与声明行；只有锚点窗口失败时，才退回 class-level fallback，并把这条 fallback 单独记成诊断。 4. 访问级别判定优先消费 `GeneratedBodyAccessSpecifier` 与 `FunctionFlags`，不要再让 raw header 扫描决定 `public/protected/private`。 5. 生成阶段把 `DefineScope` 透传到 entry/shard 元数据，后续 `WITH_EDITOR`、tests 或其它 compile directive 的 guard 直接基于 UHT scope 落盘，而不是继续从 header 文本反推。 6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加“同名调用语句远离 `MacroLineNumber` 不得成为候选”和“函数 define scope 与生成 guard 一致”的回归样本。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | 如果锚点窗口过窄，可能漏掉 class 外 inline definition 或跨多行复杂声明；因此必须保留受控 fallback，并把 fallback 命中率暴露到 sidecar/测试里。 |
| 前置依赖 | 无；但建议与 Issue-60、Issue-75 一起收口，统一把细粒度 reason 和 function-level guard 建立在同一套 UHT 锚点模型上。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 resolver 日志或测试 instrumentation 中不再出现“每个函数一次整类全文扫描”的模式。 2. 复查当前已知的 wrapper/宏文案样本，确认它们的候选数明显下降且只落在宏行附近。 3. 检查 function-level guard 产物，确认 entry 的 scope/guard 与 `DefineScope` 一致。 4. 对巨型 header 做一次性能对比，确认 wall-clock 时间下降且产物不变。 |

### Issue-91：参数/返回类型与限定符语义在 resolver 和 builder 中被重复实现，修一次要手工同步多处

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:119-178, 484-542`; `AngelscriptFunctionSignatureBuilder.cs:68-133`; `AngelscriptFunctionTableExporter.cs:56-63` |
| 问题 | 当前“函数语义”至少被拆成三套实现：`BuildExpectedParameterType()` 与 `BuildParameterType()` 做的是同一件事；`BuildExpectedReturnType()`、`BuildReturnTypeFromTokens()`、`BuildReturnType()` 都在各自重复 `ConstParm` 补齐；`Static/Const/BlueprintCallable` 判定也分别散落在 resolver、builder、exporter。也就是说，同一个 `UhtFunction/UhtProperty` 的语义，在 UHTTool 内被多次手写展开，而且这些 helper 之间没有共享契约。 |
| 根因 | 代码把“类型/限定符/flag 的规范化”当成局部实现细节，没有抽出统一的语义层；resolver、builder、exporter 因而各自持有一份近似但不完全相同的规则。 |
| 影响 | 这会把后续修复成本放大成“多点同步”：例如 `class/struct` 前置声明归一、`const` overload 判定、typed flag API 替换、`void` 返回污染修复，都必须手工检查多份 helper 是否一起更新。任何漏改，都会让 overload 匹配、显式签名 fallback、CSV 统计再次漂移。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 抽出单一的函数语义层，让 resolver/builder/exporter 全部消费同一套 `canonical type + qualifiers + flags` 结果。 |
| 具体步骤 | 1. 新增 `AngelscriptFunctionSemantics.cs` 或等价组件，集中提供 `BuildCanonicalPropertyType(UhtProperty)`, `BuildCanonicalReturnType(UhtProperty)`, `GetFunctionQualifiers(UhtFunction)`, `IsBlueprintCallableOrPure(UhtFunction)`。 2. 把 `BuildExpectedParameterType()`、`BuildParameterType()`、`BuildExpectedReturnType()`、`BuildReturnTypeFromTokens()`、`BuildReturnType()` 合并为共享 helper，禁止 resolver 与 builder 再维护平行版本。 3. 让 `NormalizeTypeText()` 的 declaration-side 清洗与 UHT-side canonicalization 进入同一语义层，避免未来新增 `class/struct/enum`、`const`、`&` 规则时只修一边。 4. `AngelscriptFunctionTableExporter.IsBlueprintCallable()` 与 `AngelscriptFunctionSignatureBuilder.HasFunctionFlag()` 一并改成共享 typed helper，删除散落的 `ToString().Contains(...)` 入口。 5. 为语义层新增 golden test matrix，覆盖 `ConstParm`、`void`、`class UFoo*`、`TSubclassOf<...>`、`Static/Const`、`BlueprintCallable/Pure` 等组合，锁住三条调用链得到同一结果。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSemantics.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 语义层如果一次吸收过多行为，容易把“UHT canonical type”和“header declaration text normalizer”混成新的大杂烩；实现时要把 AST 侧 canonicalization 与 declaration 侧 normalizer 明确分层。 |
| 前置依赖 | 无；建议与 Issue-65、Issue-88 同批处理，避免类型归一规则刚抽出又立刻返工。 |
| 验证方式 | 1. 运行新增 golden tests，确认 resolver/builder/exporter 对同一函数得到相同 canonical 结果。 2. 对 `Plugins/Angelscript/Source/AngelscriptUHTTool` 执行 `rg "BuildExpectedReturnType|BuildReturnTypeFromTokens|BuildParameterType|BuildExpectedParameterType|HasFunctionFlag|FunctionFlags\\.ToString\\(\\)\\.Contains"`，确认重复 helper 已显著收敛。 3. 重新运行 UHT 导出，确认产物在预期修复外没有额外漂移。 |

### Issue-92：所有 UHT shard 共享同一 `BindOrder`，runtime 排序没有稳定次键，32 个生成 shard 的执行顺序并不确定

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_AIModule_000.cpp`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Summary.json` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:302-306`; `AngelscriptBinds.h:455-467`; `AngelscriptBinds.cpp:120-148, 161-183, 195-214`; `AS_FunctionTable_AIModule_000.cpp:33`; `AS_FunctionTable_Summary.json:1-8` |
| 问题 | 每个生成 shard 都用同一个 `FAngelscriptBinds::EOrder::Late + 50` 注册，当前产物 `AS_FunctionTable_AIModule_000.cpp:33` 就是这个写法；而 runtime `FBindFunction::operator<` 只比较 `BindOrder`，`GetSortedBindArray()` 直接 `Sort()` 后供 `GetBindInfoList()` 和 `CallBinds()` 消费。当前 summary 已显示一次导出有 `32` 个 shard，这意味着整批 UHT 产物在 runtime 看来只有同一个 primary order，没有稳定 secondary key。 |
| 根因 | 代码生成器只关心“把 shard 放到 Late 阶段”，没有给 shard 间顺序建立正式契约；runtime 排序层也没有在 `BindOrder` 相等时继续按 `BindName` 或显式 sort key 排序。 |
| 影响 | 当前就会出现执行观测/状态 dump 顺序不稳定的问题；一旦后续修复 overload、script-facing alias 或 shard manifest 后出现跨 shard 同名/同类冲突，`AddFunctionEntry()` 的“先到先赢”行为会依赖未定义的同阶排序结果，直接把问题从“可重现缺陷”升级成“偶发顺序缺陷”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 UHT shard 建立稳定次序契约，让 runtime 排序从“单一 `BindOrder`”升级为“`BindOrder + StableShardKey`”。 |
| 具体步骤 | 1. 先完成 Issue-69，让每个 shard 都有稳定 `BindName`；如果短期内不做 Issue-69，就在 `FBindFunction` 里增加显式 `StableSortKey`，由生成器写入。 2. 修改 `FBindFunction::operator<`，在 `BindOrder` 相等时继续比较 `BindName` 或 `StableSortKey`，禁止同阶条目再依赖容器/排序实现的偶然顺序。 3. `BuildShard()` 生成的每个 `FBind` 都要把 shard 文件名同构的 key 带入 runtime，例如 `AS_FunctionTable_Engine_000`。 4. 在 `AngelscriptBindConfigTests.cpp` 或等价自动化中新增稳定性断言：重复初始化/重置 bind 状态后，`GetBindInfoList()` 返回的 UHT shard 顺序必须完全一致。 5. 若未来确实需要 shard 间有显式先后顺序，再额外引入保留区间，例如 `Late + 50 + ShardOrdinal`，但要把数值区间写成常量并加测试，避免与手写 bind 的 `Late + 100/+150` 约定相撞。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接改数值 `BindOrder`，需要确认不会打破现有手写 bind 的相对先后；更稳妥的是先补稳定次键，再决定是否需要扩大数值顺序空间。 |
| 前置依赖 | 建议先完成 Issue-69，避免“名字稳定了但排序仍不稳定”的半修状态。 |
| 验证方式 | 1. 连续多次初始化 bind 状态并调用 `GetBindInfoList()`，确认 `AS_FunctionTable_*` shard 的顺序完全一致。 2. 运行执行观测测试，确认 snapshot 中 UHT shard 的执行顺序稳定，不再随 unnamed/同阶 bind 数量漂移。 3. 在构造重复 script-facing key 的测试样本后，确认冲突结果由显式排序契约决定，而不是依赖未定义的同阶顺序。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-90 | Architecture | 优先处理，先把 resolver 从 raw header 全文扫描收回到 UHT 源码锚点模型 |
| P2 | Issue-92 | Architecture | 第二步处理，建立 shard 的稳定执行顺序契约，避免后续修复引入顺序型回归 |
| P2 | Issue-91 | Refactoring | 在 Issue-90 收敛解析入口后实施，统一类型/flag 语义层，降低后续修复的多点同步成本 |

---

## 发现与方案 (2026-04-08 18:22)

### Issue-93：候选签名匹配忽略模板/const overload 的真实声明文本，导致 Blueprint getter 被误判成 `overloaded-unresolved`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerController.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerState.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/StaticMesh.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:70-105,153-178,415-505`; `PlayerController.h:1192-1199`; `PlayerState.h:191-197`; `StaticMesh.h:1108-1117`; `AS_FunctionTable_SkippedEntries.csv:553,556,2536` |
| 问题 | resolver 在 overload 过滤阶段只比较参数个数、`NormalizeTypeText()` 归一后的类型文本，以及 `TryParseDeclaration()` 回填出的 `parsedSignature`。但 `TryParseDeclaration()` 对非 `void` 候选直接复用 UHT 的 `ReturnProperty`，`isConst/isStatic` 也直接复用 UHT flags，而 `NormalizeTypeText()` 又无条件删除 `const` 与 `&`。这样一来，模板 helper 和 const/mutable overload 的真实声明差异会被全部抹平。当前已有实证：`APlayerController::GetHUD()` 的 Blueprint 声明与紧随其后的 `template<class T> T* GetHUD() const` 同时存在，`APlayerState::GetPawn()` 也是同样模式；`UStaticMesh::GetStaticMaterials()` 则同时存在 mutable/const 两个零参 overload。现有产物中，这三个 Blueprint API 都已落到 `AS_FunctionTable_SkippedEntries.csv` 的 `overloaded-unresolved`。 |
| 根因 | 候选声明匹配没有真正解析“候选自身”的返回类型、`const` 限定和模板前缀，而是把 UHT 侧的唯一真值套用到所有候选上；同时 `FindDeclarationEnd()` 只看 `parenDepth`，遇到 inline body 的第一个 `;` 就截断，进一步放大了模板 helper 与 inline getter 的误判概率。 |
| 影响 | 任何“Blueprint 声明 + 模板 helper”或“Blueprint const getter + mutable overload”组合，都可能被错误归类为 `overloaded-unresolved`，最终从 `Direct` 退化为 `Stub`。这会直接降低 getter 类 API 的直绑覆盖率，并让 skipped reason 把简单的候选区分失败伪装成复杂 overload 冲突。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把候选匹配改成“候选声明真实语义”比对：显式过滤模板 helper，保留 const/ref 差异，并修正 inline declaration 边界。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 增加 `CandidateDeclaration` 元数据，至少记录 `IsTemplateCandidate`、`HasInlineBody`、`DeclarationEnd`，并让 `FindDeclarationEnd()` 同时跟踪 `braceDepth`，对 inline body 以匹配 `}` 或最终 `;` 为止，不能在函数体第一条语句的 `;` 处截断。 2. 在 `TryParseDeclaration()` 中真正从 `prefix`/尾部 token 解析候选返回类型与 `const` 限定，禁止对每个候选继续复用 UHT 的 `ReturnProperty` 和 `FunctionFlags`。 3. 对 `template<...>` 候选新增硬过滤：若 UHT 当前函数不是模板声明，则模板 helper 默认不进入 `exactMatches`。 4. 将 `NormalizeTypeText()` 拆成两层：参数兼容性可以继续做宽松归一，但 overload 判定必须保留 `const`、引用和值类别，避免 `TArray<FStaticMaterial>&` 与 `const TArray<FStaticMaterial>&` 被压成同一个键。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 新增回归样本，至少覆盖 `GetHUD`、`GetPawn`、`GetStaticMaterials` 三类模式，并断言它们不能再写入 `SkippedEntries.csv`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果一刀切过滤模板候选或收紧 const/ref 比对，可能让少数依赖宽松匹配的历史样本从 direct 变成 skipped；需要用现有 representative coverage 与新增 getter 回归一起兜底。 |
| 前置依赖 | 无；但建议与 Issue-90 同步设计，避免后续切到 UHT 锚点模型时再次重写候选定位逻辑。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `APlayerController,GetHUD`、`APlayerState,GetPawn`、`UStaticMesh,GetStaticMaterials` 不再出现在 `AS_FunctionTable_SkippedEntries.csv`。 2. 检查 `AS_FunctionTable_Entries.csv` 与对应 shard `.cpp`，确认三者从 `Stub` 回到 direct 绑定。 3. 运行新增自动化，覆盖模板 helper、const/mutable getter、inline getter 三类样本，确认 resolver 只保留真实 Blueprint 候选。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-93 | Defect | 优先修复，先恢复模板 helper / const getter 场景下被误降级的 direct bindings |

---

## 发现与方案 (2026-04-08 18:24)

### Issue-94：补充 `UHTTool_Analysis` 发现 5 的落地方案：summary/skipped sidecar 未纳入 `CommitOutput` 生命周期，增量导出仍会重复写盘并遗留旧诊断

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:121,174-206,218-265,432-446`; `AngelscriptFunctionTableExporter.cs:99-160` |
| 问题 | 目前只有 shard `.cpp` 通过 `factory.CommitOutput()` 写入，且 stale 清理也只枚举 `AS_FunctionTable_*.cpp`。与之并行的 `AS_FunctionTable_Summary.json`、`AS_FunctionTable_ModuleSummary.csv`、`AS_FunctionTable_Entries.csv`、`AS_FunctionTable_SkippedEntries.csv`、`AS_FunctionTable_SkippedReasonSummary.csv` 全都直接走 `File.WriteAllText()`。这意味着即使内容完全不变，sidecar 仍会在每次 UHT 运行时重写时间戳；一旦文件改名、禁用某个 sidecar 或切换 schema，旧 JSON/CSV 也不会被 `DeleteStaleOutputs()` 清理。 |
| 根因 | UHTTool 把 compile output 和诊断 sidecar 分成了两条完全不同的写盘路径，但只为 `.cpp` 路径接入了 UHT 的内容比较与 stale 生命周期管理。 |
| 影响 | 增量构建和诊断消费方都会受影响：1. 相同输入重复导出时，sidecar 仍然 churn，增加不必要的文件监控、缓存失效和 CI diff 噪音；2. 当导出策略升级或 sidecar 名称变化时，磁盘上会残留旧文件，人工排查和自动化都有机会读取到过期诊断。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把所有 `AS_FunctionTable_*` sidecar 收敛到统一的 `CommitOutput + stale manifest` 生命周期，禁止诊断文件绕过 UHT 输出管理。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.cs` / `AngelscriptFunctionTableExporter.cs` 抽出共享 `CommitAuxiliaryOutput(IUhtExportFactory factory, string stem, string extension, string content, HashSet<string> generatedPaths)`，内部统一调用 `factory.CommitOutput()`，并把产物路径加入 `generatedPaths`。 2. 将 `WriteGenerationSummary()`、`WriteModuleSummaryCsv()`、`WriteEntryCsv()`、`WriteSkippedEntriesCsv()`、`WriteSkippedReasonSummaryCsv()` 全部改成走这个共享入口，删除直接 `File.WriteAllText()`。 3. 扩展 `DeleteStaleOutputs()`：除了 `AS_FunctionTable_*.cpp`，还要清理同目录下不在 `generatedPaths` 内的 `AS_FunctionTable_*.json` 与 `AS_FunctionTable_*.csv`。 4. 若 `CommitOutput()` 只适用于 compile output，则新增一个显式 sidecar manifest，例如 `AS_FunctionTable_OutputManifest.json`，由导出器写入本轮实际 sidecar 列表，并据此做 stale 清理。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加 sidecar 生命周期回归，至少验证“删除某个 sidecar 写入逻辑后，旧文件会在下一轮导出中被清理”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 sidecar 也纳入 compile-output 目录生命周期，需要确认不会被 UHT/UBT 当成待编译源文件误处理；实现前要先验证 `CommitOutput()` 对 `.json/.csv` 的行为。 |
| 前置依赖 | 无；若后续落地 Issue-50 的 schema/provenance 版本字段，建议复用同一 sidecar manifest，一次性完成 stale/version 清理。 |
| 验证方式 | 1. 在不改源码的前提下连续运行两次 UHT 导出，确认 sidecar 文件内容和时间戳不再无条件变化。 2. 人为移除一个 sidecar 的写入分支后再导出，确认旧文件会被自动删除。 3. 检查现有自动化与脚本路径，确认它们仍能从同一目录读取到最新 sidecar。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-94 | Defect | 与 Issue-49 并行处理，先把非 `.cpp` 产物纳入增量生命周期，减少诊断侧全量 churn |

---

## 发现与方案 (2026-04-08 18:25)

### Issue-95：补充 `UHTTool_Analysis` 发现 60 的落地方案：支持模块与 EditorOnly 边界仍靠解析 `Build.cs` 文本，UE5.x 适配点放在了错误层级

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`, `../../UnrealEngine/UERelease/Engine/Source/Programs/Shared/EpicGames.UHT/Types/UhtModule.cs` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:48,334-428`; `AngelscriptRuntime.Build.cs:30-79`; `UhtModule.cs:29-31,88-114,189-194,227-231` |
| 问题 | `LoadSupportedModules()` 当前先从某个 header 路径反推 `AngelscriptRuntime.Build.cs`，再用 `QuotedStringPattern`、`line.Contains(\"DependencyModuleNames.AddRange\")` 和 `line.StartsWith(\"if (Target.bBuildEditor)\")` 去重建“支持模块集合 + editor-only 集合”。这条链路直接依赖 `Build.cs` 的源码排版和书写风格。与之对比，UHT 侧已经把模块作为结构化对象暴露出来：`UhtModule.Module`、`Module.ModuleType`、`Module.BaseDirectory` 以及 `PrepareHeaders()` 使用的 `PublicHeaders/InternalHeaders/PrivateHeaders` 全部是稳定元数据，而不是源代码字符串。 |
| 根因 | UHTTool 把“该生成哪些模块”的适配边界放在 `Build.cs` 文本层，而不是放在 UBT/UHT 已经产出的 manifest 或结构化模块元数据层。 |
| 影响 | 只要 `AngelscriptRuntime.Build.cs` 改成辅助函数、变量拼接、不同的 `if` 结构，或者 UE5.x 调整 `ModuleRules` 模板，这里的模块发现就可能静默漂移。最坏情况下，函数表覆盖范围会在没有显式错误的前提下变化，直接影响生成结果、Editor guard 与模块统计。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“支持模块/EditorOnly”从源码文本协议升级为显式 manifest 协议，由 UBT 生成、由 UHTTool 消费。 |
| 具体步骤 | 1. 在 `AngelscriptRuntime.Build.cs` 中新增一段明确的 manifest 导出逻辑，把最终 `PublicDependencyModuleNames`、`PrivateDependencyModuleNames` 与 `Target.bBuildEditor` 才加入的模块写成 `AngelscriptFunctionTableModules.json`。 2. `AngelscriptFunctionTableCodeGenerator.LoadSupportedModules()` 改为优先读取这个 manifest，并通过 `factory.AddExternalDependency()` 跟踪它，而不是再去解析 `Build.cs` 源码。 3. manifest 中至少包含 `allModules`、`editorOnlyModules`、`generatedByBuildCsVersion` 三组字段；缺失或版本不匹配时再 fallback 到受限模式，并输出明确错误，禁止静默退回文本猜测。 4. 利用 `UhtModule.Module.ModuleType` 与 `Module.BaseDirectory` 做校验：如果 manifest 声称某模块是 runtime，但 UHT 看到的是 editor-only module type，直接报错，防止配置漂移。 5. 为 `LoadSupportedModules()` 增加自动化样本，至少覆盖“Editor 目标包含 `UMGEditor`、非 Editor 目标不包含它”与“manifest 缺失时给出明确失败信息”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | `Build.cs` 写 manifest 需要确认输出目录与多 target 并行构建不冲突；manifest 路径必须带 target/config 维度，避免不同目标互相覆盖。 |
| 前置依赖 | 无；若后续推进 Issue-90，可把 module manifest 一并纳入 UHT 锚点/metadata 体系，统一导出器对 UBT/UHT 元数据的消费入口。 |
| 验证方式 | 1. 在 Editor 与非 Editor 目标各运行一次导出，确认支持模块集合与 `editorOnly` 标记符合 manifest。 2. 人为调整 `Build.cs` 的排版或提取 helper 函数后再导出，确认产物不再因为源码格式变化而漂移。 3. 删除或损坏 manifest，确认 UHTTool 给出显式失败或受控 fallback 日志，而不是静默改变覆盖范围。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-95 | Architecture | 在增量输出问题收口后处理，先把模块边界从 `Build.cs` 文本解析迁移到显式 manifest |

---

## 发现与方案 (2026-04-08 18:33)

### Issue-96：补充 `UHTTool_Analysis` 发现 22 的落地方案：`BlueprintNativeEvent/RPC` 仍被写进函数表，生成产物会把 runtime 永不消费的条目误记成 `Direct`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/GameModeBase.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerController.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:490-515`; `AngelscriptFunctionTableExporter.cs:56-88`; `Bind_BlueprintType.cpp:747-754, 1305-1314, 1397-1405`; `GameModeBase.h:82-84, 435-437`; `PlayerController.h:1183-1185`; `AS_FunctionTable_Entries.csv:207, 213, 315` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 22。当前 UHT 生成侧只要命中 `BlueprintCallable/BlueprintPure` 就会进入 `ShouldGenerate()` 和 exporter 统计，完全不排除同时带 `BlueprintNativeEvent` 或 RPC flag 的函数；但 runtime 在 `Bind_BlueprintType.cpp` 中会先检查 `FUNC_BlueprintEvent | FUNC_NetFuncFlags`，命中后直接走 `BindBlueprintEvent()`，不会消费 `ClassFuncMaps` 里的 generated function table entry。结果是 `AGameModeBase::GetDefaultPawnClassForController`、`AGameModeBase::PlayerCanRestart`、`APlayerController::ClientSetHUD` 这类函数当前都在 `Entries.csv` 中被记成 `Direct`，但这条 direct 路径在运行时实际上是死的。 |
| 根因 | UHTTool 的“是否生成函数表 entry”判定只复用了 `BlueprintCallable/Pure` 的入口条件，没有复用 runtime 在 `Bind_BlueprintType.cpp` 中已经稳定存在的事件/RPC 分流语义。 |
| 影响 | 这会同时污染三层结果：1. `AS_FunctionTable_Entries.csv` 和 `Summary.json` 把永不消费的条目算进 direct 覆盖；2. shard `.cpp` 继续生成无效 `AddFunctionEntry(...)` 注册代码；3. 后续排查时工程师会被 sidecar 误导，以为这些事件/RPC 已经由 UHT 函数表覆盖，而真实执行路径仍在 `BindBlueprintEvent()`。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 generated function table 的候选集收紧到“runtime 真正会走 `BindBlueprintCallable()` 的函数”，把 `BlueprintEvent/RPC` 从生成层与统计层同时剔除。 |
| 具体步骤 | 1. 在 `AngelscriptUHTTool` 中新增共享 helper，例如 `ShouldRouteToGeneratedFunctionTable(UhtFunction function)`，显式排除 `FUNC_BlueprintEvent`、`FUNC_NetFuncFlags`，并与现有 `BlueprintCallable/Pure`、`CustomThunk` 过滤组合成统一入口。 2. `AngelscriptFunctionTableCodeGenerator.ShouldGenerate()` 与 `AngelscriptFunctionTableExporter.CountBlueprintCallableFunctions()` 全部改用这同一个 helper，禁止继续让 generator/exporter 分别维护“候选函数”定义。 3. 为避免完全丢失诊断，可新增 `AS_FunctionTable_ExcludedEntries.csv` 或 `ExcludedByRuntimeRoute` 统计，把这类函数单独记为“走 event/RPC 绑定链”，不要继续塞进 `Direct/Stub/Skipped` 三态。 4. 若 runtime 仍需要这些函数的 script-facing 可见性信息，则在 sidecar 中额外输出 `BindingRoute` 列，至少区分 `GeneratedFunctionTable` 与 `BindBlueprintEvent`，防止报表继续混淆两条路径。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加负向回归，明确断言 `GetDefaultPawnClassForController`、`PlayerCanRestart`、`ClientSetHUD` 不再出现在 generated entries CSV 中；同时补一条 runtime 断言，确认它们仍会通过 `BindBlueprintEvent()` 暴露，而不是被错误删除。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只在生成侧删除这些条目、却不补 sidecar 的 route 说明，现有消费者可能误以为函数“消失”而不是“改走 event/RPC 路径”；因此 schema 变更要和诊断文案一起落地。 |
| 前置依赖 | 建议与 Issue-56 共用统一的 runtime route/policy helper，避免再次出现“生成侧一套、runtime 一套”的候选集漂移。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `AS_FunctionTable_Entries.csv` 不再包含 `GetDefaultPawnClassForController`、`PlayerCanRestart`、`ClientSetHUD` 的 `Direct` 记录。 2. 检查对应 shard `.cpp`，确认不再生成这三条 `AddFunctionEntry(...)`。 3. 运行 runtime 自动化或最小 smoke，确认这三条函数仍通过 `BindBlueprintEvent()` 路径对脚本可见。 4. 比较 summary/sidecar，确认 direct 总数下降且变化仅来自事件/RPC 路由修正。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-96 | Defect | 优先处理，先把 runtime 永不消费的 event/RPC 条目从 generated coverage 中剥离 |

---

## 发现与方案 (2026-04-08 18:35)

### Issue-97：补充 `UHTTool_Analysis` 发现 31 的落地方案：`BlueprintAuthorityOnly` 语义在 UHT/native 绑定链路中完全丢失，服务器专用 API 被当成普通脚本函数暴露

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:42-58, 250-265, 336-393, 414-458`; `AngelscriptPreprocessor.cpp:1412, 1641-1643`; `AngelscriptEngine.h:990-991, 1043-1045`; `AngelscriptClassGenerator.cpp:3474-3475`; `Actor.h:721-722, 3172-3177`; `AS_FunctionTable_Entries.csv:11, 115` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 31。当前 native BlueprintCallable 绑定链路只回填 `WorldContext`、`DeterminesOutputType`、`BlueprintProtected`、deprecated、editor-only 等少数语义，没有任何 `BlueprintAuthorityOnly` 字段、DB 持久化位或 `ModifyScriptFunction()` 回写逻辑。与之对比，插件自己的脚本源码链路已经明确解析并保存 `bBlueprintAuthorityOnly`，最终在类生成阶段回写 `FUNC_BlueprintAuthorityOnly`。现有 engine API 中，`AActor::SetReplicates`、`SetNetDormancy`、`FlushNetDormancy` 都显式带 `BlueprintAuthorityOnly`，但当前 `Entries.csv` 已把后两者记成普通 `Direct`。 |
| 根因 | UHT/native 绑定模型与脚本源码生成模型已经在权限语义上分叉：前者的 `FAngelscriptFunctionSignature` schema 根本没有 authority-only 位，后者却完整跟踪了这项 specifier。 |
| 影响 | 客户端脚本侧会把 Blueprint 明确标记为“仅服务器可调用”的原生函数视为普通 API，既缺少编译期提示，也缺少运行期 gate。这样不仅会让 Angelscript 与 Blueprint 的网络权限模型分叉，还会让 sidecar 把这类危险暴露继续算作正常 direct 覆盖。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `BlueprintAuthorityOnly` 提升为 UHT/native 绑定的正式语义字段，和脚本源码链路保持同一套 authority-only 契约。 |
| 具体步骤 | 1. 在 `FAngelscriptFunctionSignature` 中新增 `bBlueprintAuthorityOnly`，并在 `InitFromFunction()` 里通过 `Function->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly)` 或等价 metadata 读取该位；`WriteToDB()` / `InitFromDB()` 同步持久化，避免 cooked 路径再次丢语义。 2. 在 `ModifyScriptFunction()` 中把 authority-only 映射到 script function 的显式 trait 或自定义 metadata；如果 runtime 已有网络权限检查入口，则在此处挂接统一 gate，而不是让调用点各自猜测。 3. 在脚本编译期或绑定期新增 authority-only 约束：至少对 non-authority 上下文给出明确错误/警告，禁止客户端静默调用 `SetNetDormancy`、`FlushNetDormancy` 这类函数。 4. 扩展 `AS_FunctionTable_Entries.csv` / summary schema，增加 `AuthorityOnly` 或 `InvocationConstraint` 列，避免 sidecar 继续把它们伪装成无约束 `Direct`。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/` 新增回归，至少覆盖 `AActor::SetNetDormancy` 与 `FlushNetDormancy`：断言它们仍会生成 entry，但 entry/运行时函数都带 authority-only 语义，且客户端上下文不能像普通函数一样通过。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只在 editor 实时路径补 authority-only、却不同时升级 bind-db schema，cooked/non-editor 仍会继续丢这项语义；因此这次修复必须把 DB 回放一起纳入范围。 |
| 前置依赖 | 建议与 Issue-86、Issue-35 同批设计，统一收口 bind-db 对 native function 语义字段的持久化模型。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `Entries.csv` 中 `SetNetDormancy`、`FlushNetDormancy` 带有新的 authority-only 标记，而不是裸 `Direct`。 2. 在 editor 和强制 bind-db 回放两条路径各跑一次网络权限测试，确认客户端上下文不能把这些函数当成普通脚本 API 调用。 3. 复查脚本源码生成链与 native 绑定链，确认两边对 `BlueprintAuthorityOnly` 的保存/回放字段一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-97 | Defect | 紧随 Issue-96，先补回服务器权限语义，避免 network authority API 继续裸露到普通脚本面 |

---

## 发现与方案 (2026-04-08 18:36)

### Issue-98：补充 `UHTTool_Analysis` 发现 34 的落地方案：`UnsafeDuringActorConstruction` 在 UHT/native 绑定链路中完全丢失，构造期危险 API 被当成普通函数暴露

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Components/PrimitiveComponent.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/FunctionLibraries/AngelscriptComponentLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:42-58, 250-265, 336-393, 414-458`; `PrimitiveComponent.h:1608-1609, 1854-1855, 2767-2769`; `AngelscriptComponentLibrary.h:223-235`; `AS_FunctionTable_Entries.csv:3126, 3157, 3279` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 34。当前 native BlueprintCallable 绑定链路只保留 `WorldContext`、`DeterminesOutputType`、`BlueprintProtected`、deprecated、editor-only 等少量语义，完全没有 `UnsafeDuringActorConstruction` 字段或回写逻辑。引擎头里 `UPrimitiveComponent::AddImpulse`、`WakeRigidBody`、`GetMass` 都显式带 `meta=(UnsafeDuringActorConstruction=\"true\")`，但当前 `Entries.csv` 已把它们作为普通 `Direct` 写入函数表。与此同时，插件自己的 `UAngelscriptComponentLibrary::AttachToComponent()` 已经在默认语句/构造期显式抛错，说明项目本身承认“构造期危险函数需要额外 gate”，只是 UHT 自动绑定路径没有继承这条安全边界。 |
| 根因 | `FAngelscriptFunctionSignature` 的 native schema 没有为 `UnsafeDuringActorConstruction` 预留字段，导致 UHT 导出的 entry 无法把 Blueprint 已声明的构造期风险继续传递到脚本函数元数据或运行期校验。 |
| 影响 | 脚本默认语句或对象构造期逻辑可以直接调用 Blueprint 明确标红的原生 API，触发物理/碰撞等副作用，而 sidecar 仍把这些条目算作普通成功覆盖。这会让 Angelscript 的构造期安全模型明显宽于 Blueprint，并增加难以定位的初始化时序问题。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `UnsafeDuringActorConstruction` 升级成 native generated binding 的正式约束字段，在脚本函数元数据和运行时入口上同时施加构造期保护。 |
| 具体步骤 | 1. 在 `FAngelscriptFunctionSignature` 中新增 `bUnsafeDuringActorConstruction`，从 `Function->HasMetaData(TEXT(\"UnsafeDuringActorConstruction\"))` 读取，并在 `WriteToDB()` / `InitFromDB()` 中持久化，避免 cooked 路径再次丢语义。 2. 为 script function 增加统一 trait 或 invocation constraint，`ModifyScriptFunction()` 在生成函数注册后写入该约束；若现有 trait 不足，则新增一个 runtime-visible 标记供调用层读取。 3. 在 native BlueprintCallable 调用入口增加构造期 gate：当 `FUObjectThreadContext::Get().IsInConstructor` 且目标函数带该约束时，统一抛出清晰错误，而不是依赖每个 helper 手工防御。 4. 扩展 sidecar，给 `Entries.csv` 增加 `InvocationConstraint` / `UnsafeDuringActorConstruction` 列，避免 `AddImpulse`、`WakeRigidBody`、`GetMass` 继续显示成普通 `Direct`。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/` 增加回归，至少覆盖 `AddImpulse` 与 `GetMass`：断言它们仍会生成 entry，但在构造期上下文触发明确保护；并保留一个普通物理 API 作为对照，验证不会误伤。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只在 runtime 调用层加保护、却不把约束写进 sidecar 和 DB，诊断与 cooked 行为仍会继续漂移；同时需要确认新 gate 不会和已有手写 `AttachToComponent()` 之类的局部防御产生双重报错。 |
| 前置依赖 | 建议与 Issue-97、Issue-59 一起设计统一的 `InvocationConstraint`/metadata 传播模型，避免 `AuthorityOnly`、`DevelopmentOnly`、`UnsafeDuringActorConstruction` 各自新增一套平行字段。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `Entries.csv` 中 `AddImpulse`、`WakeRigidBody`、`GetMass` 带有新的构造期风险标记。 2. 在构造期/默认语句测试中调用这些函数，确认会得到明确错误而不是静默执行。 3. 在普通运行期再次调用，确认行为仍与当前 direct bind 一致。 4. 强制走 bind-db 回放路径复验一次，确认该约束不会在 cooked/non-editor 再次丢失。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-98 | Defect | 与 Issue-97 联动处理，先把构造期危险 API 的安全边界补回到 native generated binding |

---

## 发现与方案 (2026-04-08 18:50)

### Issue-99：`WriteCoverageDiagnostics()` 原地重排 `moduleSummaries`，同一轮导出的 `Summary/ModuleSummary` 与 `Entries.csv` 会天然失去顺序一致性

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | 142-206, 218-262 |
| 问题 | `WriteCoverageDiagnostics()` 在 `142-150` 行直接对传入的 `moduleSummaries` 做原地 `Sort`，按 `StubEntries` 降序重排。随后 `WriteGenerationSummary()` 在 `166-206` 行直接复用这个已经被重排的列表写 `AS_FunctionTable_Summary.json` 与 `AS_FunctionTable_ModuleSummary.csv`；但 `WriteEntryCsv()` 在 `244-262` 行写的是先前按模块遍历顺序追加的 `csvEntries`，没有做对应重排。也就是说，同一轮导出里，summary/module csv 的模块顺序由 `WriteCoverageDiagnostics()` 决定，而 `Entries.csv` 的模块块顺序仍由 `Generate()` 的遍历顺序决定。 |
| 根因 | 诊断输出阶段复用了可变的业务数据列表，把“控制台展示排序”直接施加到了后续序列化输入上；代码没有区分 `diagnostic view` 与 `serialization view`。 |
| 影响 | sidecar 之间失去最基本的顺序契约。消费方如果按“`Summary.json.modules[i]` 对应 `ModuleSummary.csv` 第 `i` 行，再对应 `Entries.csv` 第 `i` 个模块块”做关联，会在同一轮导出内得到互相矛盾的顺序；后续任何 stub 数量变化还会继续放大 diff 噪音。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把控制台诊断排序与正式 sidecar 序列化彻底解耦，禁止日志函数再原地修改导出模型。 |
| 具体步骤 | 1. 将 `WriteCoverageDiagnostics()` 的入参改成 `IReadOnlyList<AngelscriptModuleGenerationSummary>`，函数内部复制一份本地列表后再按 `StubEntries` 排序打印。 2. 在 `Generate()` 中保留一份明确命名的 `serializationModuleSummaries`，后续所有写盘函数只消费这份只读视图，不允许再被日志阶段修改。 3. 若后续确实需要对 sidecar 做稳定排序，单独新增 `BuildOrderedSerializationViews()`，同时返回 `orderedModules` 与 `orderedCsvEntries`，不要继续让 `WriteCoverageDiagnostics()` 承担任何序列化语义。 4. 给 `WriteGenerationSummary()` 和 `WriteEntryCsv()` 增加注释或 helper 名，显式声明“这里消费的是序列化顺序，而不是控制台展示顺序”。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加跨文件一致性断言：`Summary.json` 的模块数组、`ModuleSummary.csv` 的模块行、`Entries.csv` 的模块块顺序必须由同一份 ordered view 驱动。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果已有外部脚本无意中依赖当前“summary 按 stub 排序、entries 按遍历顺序”的异常状态，修复后需要同步调整；不过这种依赖本身不具备稳定契约，应一次性清理。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行一轮 UHT 导出。 2. 从 `Summary.json`、`ModuleSummary.csv`、`Entries.csv` 读取模块顺序，确认三者完全一致。 3. 人为修改某个模块的 stub 数，再次导出，确认只有内容变化的模块数据发生 diff，不会再因为 `WriteCoverageDiagnostics()` 的原地排序导致 cross-file 顺序漂移。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-99 | Defect | 尽快处理，先切断日志排序对正式 sidecar 顺序的副作用 |

---

## 发现与方案 (2026-04-08 18:52)

### Issue-100：exporter 控制台统计把 `UhtModule` 说成 `packages`，又把 shard 数说成 `module files`，诊断口径与真实产物不一致

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Low |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Summary.json` |
| 行号 | `AngelscriptFunctionTableExporter.cs:27-53`; `AngelscriptFunctionTableCodeGenerator.cs:54-78, 166-215`; `AS_FunctionTable_Summary.json:1-8` |
| 问题 | `Export()` 在 `37-40` 行遍历的是 `factory.Session.Modules`，但日志模板在 `46-53` 行写成了 `visited {0} packages`。同一个日志里，`generatedFileCount` 来自 `AngelscriptFunctionTableCodeGenerator.Generate(factory)`；而 `Generate()` 在 `54-78` 行累计的是每个模块的 `ShardCount`，`WriteGenerationSummary()` 也把同一个值写成 `totalShardCount`（`185` 行）。当前 summary 里已经明确有 `moduleCount = 14`、`totalShardCount = 32`，但 exporter 日志却会把这个 `32` 说成 `module files`。 |
| 根因 | exporter 侧沿用了早期的变量命名和日志文案，没有随着“单模块多 shard”设计演进同步更新统计语义。 |
| 影响 | 运行导出时，控制台会把“访问了多少 module”“实际写了多少 shard 文件”混成错误口径。排查增量构建、对比 `Summary.json`、或人工核对产物规模时，开发者会收到误导性的统计信息。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 统一 exporter 与 generator 的统计命名，让控制台日志、summary 字段和实际产物维度一一对应。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableExporter.cs` 中将 `packageCount` 重命名为 `visitedModuleCount`，将 `generatedFileCount` 重命名为 `generatedShardFileCount`。 2. 把日志模板改成类似 `visited {0} modules ... wrote {5} shard files across {6} generated modules`，并把“生成了多少模块”显式从 `Summary.json.moduleCount` 或 `moduleSummaries.Count` 传入，而不是继续复用 shard 数。 3. 在 `AngelscriptFunctionTableCodeGenerator.Generate()` 返回值或配套结果对象中同时暴露 `GeneratedModuleCount` 与 `GeneratedShardFileCount`，避免调用方再靠变量名猜语义。 4. 若短期内不改返回类型，至少在 exporter 日志中额外读取 `AS_FunctionTable_Summary.json` 或返回辅助结构，保证 `moduleCount`/`totalShardCount` 两个维度都能被打印出来。 5. 补一条最小自动化或日志快照断言，确认 exporter 输出包含 `modules` 与 `shard files` 两个独立字段。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S |
| 风险 | 若有外部脚本解析旧日志文案，需要同步更新匹配规则；不过相较于继续保留错误口径，这类兼容成本可接受。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 运行一次 exporter。 2. 核对控制台日志，确认它明确区分 `visited modules`、`generated modules` 与 `shard files`。 3. 将日志里的模块数与 `Summary.json.moduleCount`、shard 数与 `Summary.json.totalShardCount` 逐项比对，确认完全一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P3 | Issue-100 | Defect | 在核心正确性问题之后处理，先把控制台统计口径纠正到与 summary 一致 |

---

## 发现与方案 (2026-04-08 18:54)

### Issue-101：生成产物自动化只校验计数和表头，不校验 `Summary/ModuleSummary/Entries` 的模块身份与顺序一致性，顺序型回归会静默漏检

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs` |
| 行号 | `AngelscriptGeneratedFunctionTableTests.cpp:465-590, 594-665`; `AngelscriptFunctionTableCodeGenerator.cs:142-206, 218-262` |
| 问题 | `SummaryOutputTest` 在 `529-590` 行只校验 `modules` 数组存在、各模块计数相加正确，但不验证 `modules[i].moduleName` 与 `ModuleSummary.csv` 第 `i` 行是否同名。`CsvOutputTest` 在 `623-645` 行也只校验 CSV 行数与表头，不校验 `ModuleSummary.csv` 的模块行、`Entries.csv` 的模块块、以及 `Summary.json.modules` 之间是否共享同一顺序和同一模块集合。与此同时，生成器当前确实把 `moduleSummaries` 与 `csvEntries` 走了两条不同顺序链路（`142-206`、`218-262` 行），说明这种顺序/身份漂移不是理论风险。 |
| 根因 | 自动化把三个 sidecar 当成互不相关的独立文件在验，只关心“数量对不对”，没有把“同一个模块在三份产物里必须有同一身份和同一顺序契约”建成断言。 |
| 影响 | 一旦出现顺序漂移、模块重排、或某份文件遗漏/重复模块，现有测试仍然可能全部通过。这样下游分析脚本、人工 diff 和回归排查会先在消费端爆炸，而不是在自动化阶段被挡住。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 sidecar 验证从“单文件计数检查”升级为“跨文件身份与顺序一致性检查”。 |
| 具体步骤 | 1. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加一个共享 helper，把 `Summary.json.modules`、`ModuleSummary.csv`、`Entries.csv` 分别解析成 `ModuleName -> summary/rows` 结构，而不是只读行数。 2. 新增断言：`ModuleSummary.csv` 的模块序列必须与 `Summary.json.modules` 的 `moduleName` 序列完全一致。 3. 再新增断言：`Entries.csv` 中每个模块块的首次出现顺序必须与前两者一致，且每个模块块的行数要等于该模块 `totalEntries`。 4. 为避免继续依赖脆弱字符串扫描，优先复用真正的 CSV 解析 helper，而不是 `Contains()` 或 `ParseIntoArray(TEXT(\",\"))` 这种简化逻辑。 5. 将 `Issue-99` 对应的顺序样本加入自动化：人为调高某个模块的 stub 数后，测试应能直接指出哪一份 sidecar 顺序漂移，而不是只做总数通过。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S |
| 风险 | 测试会比现在更严格，短期内可能暴露一批历史遗留的 sidecar 顺序问题；但这是期望行为，应尽早让自动化对真实契约负责。 |
| 前置依赖 | 无；但建议与 Issue-99 同批处理，否则新断言会先把当前顺序漂移直接打红。 |
| 验证方式 | 1. 运行更新后的自动化，确认现有三份 sidecar 在模块顺序和模块集合上完全一致。 2. 人为制造一个只影响 summary/module csv 顺序的回归，确认测试会精准失败。 3. 再人为制造一个 `Entries.csv` 少写/多写某模块块的回归，确认测试能指出具体模块而不是只报总数不等。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-101 | Refactoring | 与 Issue-99 配套处理，先把跨文件一致性断言补上，避免顺序型回归继续漏检 |

---

## 发现与方案 (2026-04-08 19:07)

### Issue-102：补充 `UHTTool_Analysis` 发现 29 的落地方案：生成产物自动化把 UHT 输出目录硬编码成 `Win64/UnrealEditor`，没有跟随 `factory.MakePath(...)` 的真实布局

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs` |
| 行号 | `AngelscriptGeneratedFunctionTableTests.cpp:245-247, 462-464, 597-599, 672-674, 709-711, 755-757`; `AngelscriptFunctionTableCodeGenerator.cs:120, 174, 220, 246, 434`; `AngelscriptFunctionTableExporter.cs:101` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 29。UHTTool 生成 shard `.cpp`、`Summary.json`、`ModuleSummary.csv`、`Entries.csv`、`SkippedEntries.csv` 以及 stale 清理目录时，全部通过 `factory.MakePath(...)` 推导真实输出位置；但 `AngelscriptGeneratedFunctionTableTests.cpp` 在 6 处直接把读取目录写死为 `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT`。测试当前验证的是“某个本地 Win64 Editor 布局”，不是工具声明的输出契约。 |
| 根因 | 测试层没有复用 UHTTool 的路径决策结果，也没有任何 sidecar/manifest 暴露“本轮导出的真实 output root”，导致验证逻辑只能复制一条硬编码目录字符串。 |
| 影响 | 一旦 target 名、platform 名、UE5.x UHT 中间目录布局或 `MakePath` 规则发生变化，自动化要么误报文件不存在，要么继续盯着旧目录读取陈旧产物；这会让与 UHT API 的适配边界失去回归保护。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 UHTTool 增加单一的 output layout 契约，让 generator/exporter 与自动化测试从同一份已解析路径信息读取产物。 |
| 具体步骤 | 1. 在 `AngelscriptUHTTool` 中新增统一的输出布局描述，例如 `AngelscriptFunctionTableOutputManifest`，至少记录 `OutputDirectory`、`SummaryPath`、`ModuleSummaryPath`、`EntriesPath`、`SkippedEntriesPath` 与 shard 文件列表，并由现有 `factory.MakePath(...)` 结果填充。 2. 让 `AngelscriptFunctionTableCodeGenerator.Generate()` 在提交所有输出后同步写出 `AS_FunctionTable_OutputManifest.json`，或返回一个包含这些路径的结果对象，禁止测试继续猜目录。 3. 将 `AngelscriptGeneratedFunctionTableTests.cpp` 中 6 处 `Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT` 常量替换为共享 helper：先读取 manifest，再按 manifest 中的相对路径或 output root 组装各文件路径。 4. 为避免 manifest 自己再次固化本机绝对路径，优先写相对于 `Plugins/Angelscript` 或生成根目录的相对路径，并在测试 helper 中统一解析。 5. 增加一条专门的路径契约回归：给 helper 喂入非 `Win64/UnrealEditor` 形态的样例 output root，断言 summary/csv/shard 路径推导仍然正确，不再依赖硬编码目录片段。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | manifest 一旦设计成新的外部契约，后续字段调整需要兼容已有测试与脚本；通过只暴露最小必要路径集合、且尽量输出相对路径，可以把 schema 漂移风险压到最低。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `AS_FunctionTable_OutputManifest.json` 与实际生成的 summary/csv/shard 文件一一对应。 2. 运行 `AngelscriptGeneratedFunctionTableTests`，确认所有产物级测试通过且不再包含 `Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT` 硬编码。 3. 额外运行路径 helper 的样例测试，确认换成非默认 target/platform 目录形态后，路径推导仍成立。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-102 | Architecture | 在继续扩展 UHT 产物自动化之前先收口输出路径契约，避免测试长期绑定本地目录布局 |

---

## 发现与方案 (2026-04-08 19:09)

### Issue-103：补充 `UHTTool_Analysis` 发现 23 的落地方案：头文件过滤只排除 `/Private/`，`Testing/` 下的 automation helper 也会被写进正式函数表和覆盖统计

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Low |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/LatentAutomationCommandClientExecutor.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Testing/IntegrationTest.cpp`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_AngelscriptRuntime_001.cpp` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:517-530`; `LatentAutomationCommandClientExecutor.h:16-18, 66-78`; `IntegrationTest.cpp:526-555, 765-782`; `AS_FunctionTable_Entries.csv:5637-5643`; `AS_FunctionTable_AngelscriptRuntime_001.cpp:39-43` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 23。`IsSupportedHeader()` 现在只排除 `/Private/` 和一个 engine 特例头，完全不区分 `Testing/` / `Tests/` 这类 automation scaffolding。结果是 `ALatentAutomationCommandClientExecutor` 这种位于 `Source/AngelscriptRuntime/Testing/`、类注释明确写着“Executes a ULatentAutomationCommand on client”的测试辅助类，其 `Fail`、`AssertTrue`、`AssertFalse` 等 7 个 `BlueprintCallable` 入口全部被写进 `AS_FunctionTable_Entries.csv`，并被 `AS_FunctionTable_AngelscriptRuntime_001.cpp` 正式 `#include`。 |
| 根因 | UHTTool 目前只有“这个头文件能不能参与生成”的可包含性过滤，没有“这个头文件是否属于正式 runtime script surface”的发布策略层。 |
| 影响 | automation helper 会污染 `AngelscriptRuntime` 模块的 direct coverage、summary 与 shard 内容；同时这类测试脚手架头文件的修改还会触发正式函数表的重新生成，降低增量构建和诊断报表的信噪比。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在生成入口新增“发布策略”过滤层，把 `Testing/` automation scaffolding 从正式 runtime function table 与统计口径中剥离。 |
| 具体步骤 | 1. 将当前 `IsSupportedHeader()` 拆成两层：第一层继续负责最小可包含性过滤；第二层新增 `ShouldPublishHeaderToFunctionTable()` 或等价策略函数，专门判断该头文件是否属于正式 script-facing API。 2. 在新策略中默认排除 `Source/.../Testing/`、`Source/.../Tests/`、automation scaffolding 目录，并提供一个显式 opt-in 机制，例如 metadata 或集中 allowlist，避免误伤少数确实需要保留的测试暴露面。 3. `ShouldGenerate()`、include 收集、`Entries.csv`/summary 统计都统一复用这同一策略，禁止出现“entry 被过滤了，但 shard include 还在”的半过滤状态。 4. 若需要保留可追踪性，把这类条目写入现有 skipped diagnostics，新增 `SkippedReason = TestInfrastructureFiltered` 或独立 `ExcludedEntries.csv`，不要继续把它们计入 `Direct/Stub` 覆盖。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加负向回归，明确断言 `ALatentAutomationCommandClientExecutor::{AssertTrue, AssertFalse, Fail}` 不再出现在 `Entries.csv`，且生成 shard 不再 `#include "Testing/LatentAutomationCommandClientExecutor.h"`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果目录过滤做得过宽，可能把少数仅用于开发态但确实希望对脚本开放的 helper 一起隐藏；通过显式 opt-in 或 skip reason 记录可以把误伤风险降到可审计范围内。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `AS_FunctionTable_Entries.csv` 不再包含 `ALatentAutomationCommandClientExecutor` 的 7 条 direct entry。 2. 检查对应 shard `.cpp`，确认不再包含 `Testing/LatentAutomationCommandClientExecutor.h`。 3. 运行 `AngelscriptGeneratedFunctionTableTests`，确认新增负向断言通过，且 skipped/diagnostic 输出能明确说明这些条目是被测试基础设施过滤，而不是静默丢失。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P3 | Issue-103 | Defect | 在核心正确性问题之后处理，把 automation-only 头文件先从正式 coverage 口径中剥离 |

---

## 发现与方案 (2026-04-08 19:10)

### Issue-104：补充 `UHTTool_Analysis` 发现 41 的落地方案：`Direct` 统计只看是否拿到函数指针，无法区分真实实现与占位实现

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptAbilityAsyncLibrary.h:58-68`; `AngelscriptFunctionTableCodeGenerator.cs:100-112, 127-135, 166-196`; `AS_FunctionTable_Entries.csv:5650` |
| 问题 | 对应 `Documents/AutoPlans/UHTTool_Analysis.md` 的发现 41。`UAngelscriptAbilityAsyncLibrary::WaitGameplayTagQueryOnActor()` 的真实源码已经把原始逻辑注释掉并直接 `return nullptr;`，但生成器只要能构造出非 `ERASE_NO_FUNCTION()` 的 `EraseMacro`，就会把条目计入 `directBindEntries`，并在 `Entries.csv` 中写成 `Direct`。当前 `AS_FunctionTable_Entries.csv:5650` 已经把该函数记为 `Direct,ERASE_AUTO_FUNCTION_PTR(...)`，把“能生成函数指针”与“函数真的有业务行为”混成同一个健康度结论。 |
| 根因 | UHTTool 的诊断模型只编码了 binding route 是否存在，没有任何 `ImplementationStatus`、`KnownPlaceholder` 或等价字段来表达 native 函数体已经退化成占位实现。 |
| 影响 | direct coverage 会被占位 API 污染，报表消费者会误以为 `WaitGameplayTagQueryOnActor` 已进入可靠可调用集合；同类“先暴露 BlueprintCallable、后补真实实现”的函数以后也会继续被统计成成功覆盖，削弱 summary/csv 的决策价值。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将“绑定路径”与“实现状态”拆成两个正交维度，把占位实现从普通 `Direct` 覆盖中单独标识出来。 |
| 具体步骤 | 1. 扩展 `AngelscriptGeneratedFunctionEntry` 与 sidecar schema，新增 `ImplementationStatus` 或 `BehaviorQuality` 字段；`BindingRoute` 继续描述 `Direct/Stub`，新字段则区分 `Implemented`、`Placeholder`、`KnownUnimplemented`。 2. 为 native 函数引入显式的 placeholder 来源，不依赖脆弱的 C++ 函数体字符串扫描；优先使用 declaration metadata 或一份集中策略表，首批至少登记 `UAngelscriptAbilityAsyncLibrary::WaitGameplayTagQueryOnActor`。 3. 生成 `Entries.csv`、`ModuleSummary.csv`、`Summary.json` 时把 placeholder 条目单独计数，例如新增 `placeholderEntries`，并从 `totalDirectBindEntries` 或 direct rate 中剥离，避免继续伪装成健康覆盖。 4. 若 runtime 仍允许这类函数保持可见，则在 diagnostics 中明确写出 `BindingRoute=Direct` 且 `ImplementationStatus=Placeholder`；若不希望对脚本开放，则与 skip policy 联动，把这类条目转入明确的 `Excluded/Placeholder` 口径。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加回归，明确断言 `WaitGameplayTagQueryOnActor` 不再作为普通 `Direct` 贡献到 summary，总表中必须能看到新的 placeholder 状态或计数字段。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityAsyncLibrary.h`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 placeholder 来源只靠集中表维护，后续新占位函数仍可能漏标；因此应优先选择靠近声明的显式 metadata，并让自动化对 placeholder 数量建立快照约束。 |
| 前置依赖 | 建议与 Issue-42 对 summary 两态模型的修正同步进行，避免旧测试继续把 `Direct/Stub` 两态当成唯一合法 schema。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `Entries.csv` 中 `WaitGameplayTagQueryOnActor` 带有新的 placeholder 状态，而不是普通 `Direct`。 2. 检查 `Summary.json` / `ModuleSummary.csv`，确认该函数不再计入健康 direct coverage，总表新增 placeholder 计数字段。 3. 运行 `AngelscriptGeneratedFunctionTableTests`，确认新增断言能在未来再次把 `return nullptr` 占位函数误记为普通 `Direct` 时直接失败。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-104 | Defect | 在 coverage/diagnostics 可信度修复中优先处理，把占位实现先从普通 `Direct` 覆盖中拆出来 |

---

## 发现与方案 (2026-04-08 23:45)

### Issue-105：`ScriptNoExport` 元数据被完全忽略，已明确禁止脚本导出的 deprecated 节点仍被写成 `Direct`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:490-515`; `Bind_BlueprintCallable.cpp:17-31, 95-139`; `Helper_FunctionSignature.h:85-120, 251-258`; `Actor.h:1993-2050`; `AS_FunctionTable_Entries.csv:7, 79-80` |
| 问题 | `ShouldGenerate()` 只排除 `NotInAngelscript`、`BlueprintInternalUseOnly`、`CustomThunk` 等条件，完全不检查 `meta=(ScriptNoExport)`。运行时侧也没有补这层语义：`GetScriptNameForFunction()` 只把 `ScriptName == "-"` 当成“不绑定”，否则继续按 `ScriptName` 或 `K2_/BP_/AS_` 规则计算脚本名；`Bind_BlueprintCallable()` 随后直接把 `FFuncEntry` 里的 native pointer 通过 `BindMethodDirect/BindGlobalFunction` 绑定进脚本引擎。当前引擎头里 `AActor::K2_AttachRootComponentTo`、`K2_AttachRootComponentToActor`、`DetachRootComponentFromParent` 都显式带 `ScriptNoExport`，但 `AS_FunctionTable_Entries.csv:7,79-80` 仍把它们记成 `Direct`。 |
| 根因 | UHT 生成链路和 runtime 脚本名推导链路都把 `ScriptNoExport` 当成“无关 metadata”，没有建立“禁止进入 script surface”的统一硬边界。 |
| 影响 | 已废弃、仅用于 Blueprint 兼容的 legacy 节点会重新进入 Angelscript 可调用面，并被 summary/csv 误算为正常 direct 覆盖。推断：在 editor 实时绑定路径里，`K2_AttachRootComponentTo*` 还会因 `K2_` 前缀剥离而进一步伪装成普通脚本 API，扩大泄漏面。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `ScriptNoExport` 提升为生成侧、runtime 侧和 bind-db 回放侧共同遵守的硬过滤规则，禁止这类函数进入任何 generated function table 契约。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.cs` 抽出共享 eligibility helper，并把 `function.MetaData.ContainsKey("ScriptNoExport")` 纳入与 `NotInAngelscript` 同级的硬排除条件；`CountBlueprintCallableFunctions()` 也必须复用这同一 helper，避免 exporter 再把它们误写进 skipped diagnostics。 2. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp` 的 `ShouldSkipBlueprintCallableFunction()` 中补上 `ScriptNoExport` 判定，并把 `Bind_BlueprintCallable.cpp:26-31` 的 skip 检查移出 `#if !AS_USE_BIND_DB`，确保 bind-db/cooked 回放也会执行这条策略。 3. 在 `Helper_FunctionSignature.h` 的 `GetScriptNameForFunction()` 中将 `ScriptNoExport` 视为显式排除，直接返回 `"-"` 或触发受控 early-out，防止其它非 UHT 绑定路径继续为这类函数计算脚本名。 4. 在 sidecar 中新增 `ExcludedByPolicy` 口径，或单独写 `AS_FunctionTable_ExcludedEntries.csv`，把 `ScriptNoExport` 条目转入可审计的排除清单，而不是继续计入 `Direct/Stub/Skipped`。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加负向回归，明确断言 `K2_AttachRootComponentTo`、`K2_AttachRootComponentToActor`、`DetachRootComponentFromParent` 不再出现在 `Entries.csv`，并在 runtime smoke 里确认这些函数不会被 generated table 暴露。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 少数历史脚本可能已经错误依赖这些 deprecated 节点；如果仓库里存在手写兼容绑定，需要用显式 allowlist 保留，而不是让 generated table继续静默放行。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `AS_FunctionTable_Entries.csv` 不再包含 `K2_AttachRootComponentTo`、`K2_AttachRootComponentToActor`、`DetachRootComponentFromParent`。 2. 若新增 `ExcludedEntries.csv`，确认这三条带有 `ScriptNoExport` 排除原因。 3. 在 editor 与强制 bind-db 回放两条路径分别执行 smoke test，确认这些函数都不会通过 generated function table 进入脚本引擎。 |

### Issue-106：`BlueprintCosmetic` 语义没有进入 generated binding 契约，UI/视口函数被当成普通 `Direct` 暴露

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Blueprint/UserWidget.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:260-266, 414-458`; `Bind_BlueprintCallable.cpp:95-139`; `UserWidget.h:344-361`; `AS_FunctionTable_Entries.csv:4771-4772, 4808` |
| 问题 | native generated binding 当前只保留 `NotAngelscriptProperty`、`ScriptTrivial`、`BlueprintProtected`、deprecated、editor-only 等少量语义；`FAngelscriptFunctionSignature` 没有任何 `BlueprintCosmetic` 字段，`ModifyScriptFunction()` 也没有对应 trait 或 compile-out 逻辑。与此同时，`Bind_BlueprintCallable()` 会把 entry 的 native pointer 直接绑定成 `BindMethodDirect/BindGlobalFunction`，不会再经过 `UFunction` 事件分发。当前 `UUserWidget::AddToViewport`、`AddToPlayerScreen`、`RemoveFromViewport` 在引擎头里都明确标了 `BlueprintCosmetic`，但 `AS_FunctionTable_Entries.csv:4771-4772,4808` 仍把它们当普通 `Direct`。 |
| 根因 | UHTTool 与 runtime 的语义持久化模型只覆盖少数现有 trait，没有把 `FUNC_BlueprintCosmetic` 纳入 generated binding 的正式契约字段；一旦走 direct native pointer，Blueprint 原生的 cosmetic 约束就被绕开了。 |
| 影响 | script-facing API 面会把仅应在 cosmetic/client 场景使用的 UI 函数伪装成普通 callable。推断：在 dedicated server 或非 cosmetic 代码路径里，这会造成 Angelscript 与 Blueprint 语义分叉，且 sidecar 当前无法提示调用者这些 entry 带有执行边界。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 `BlueprintCosmetic` 作为一等语义字段写入 generated binding、bind-db 和 sidecar，并在脚本调用层补齐与 Blueprint 一致的执行边界。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 和 `FAngelscriptMethodBind` 中新增 `bBlueprintCosmetic`，在 `InitFromFunction()` 中读取 `Function->HasAnyFunctionFlags(FUNC_BlueprintCosmetic)`，并在 `WriteToDB()` / `InitFromDB()` 中完整持久化。 2. 为脚本函数新增可见 trait 或约束字段；若现有 `traits` 不足，扩展 `as_scriptfunction.h` 增加 `asTRAIT_BLUEPRINT_COSMETIC`，否则至少通过 `compileOutType` 或 caller metadata 把 cosmetic 语义传到调用层。 3. 在 `Bind_BlueprintCallable.cpp` 的 direct bind 成功路径上，`Signature.ModifyScriptFunction()` 除现有 `deprecated/editor-only` 外，再写入 `BlueprintCosmetic` 标记；对 dedicated server 不应执行的函数，复用 `FAngelscriptBinds` 现有 compile-out/invocation-guard 基础设施实现一致的运行时保护。 4. 扩展 `AS_FunctionTable_Entries.csv` / `Summary.json`，新增 `InvocationConstraint` 或 `FunctionSemanticFlags` 列，让 `AddToViewport`、`RemoveFromViewport` 这类条目不再显示成无条件 `Direct`。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加回归，至少覆盖 `UUserWidget::AddToViewport`、`AddToPlayerScreen`、`RemoveFromViewport`：断言 sidecar 带有 cosmetic 标记，并在 dedicated server 或等价受限上下文中验证保护逻辑生效。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦补齐 cosmetic 保护，现有脚本里对 UI/view 层 API 的越界调用会被显式暴露；需要在变更说明里提示这不是新限制，而是此前丢失的 Blueprint 语义被恢复。 |
| 前置依赖 | 建议与 Issue-97、Issue-98 同步设计，统一 native generated binding 的“执行约束语义”承载方式，避免 `AuthorityOnly`、`UnsafeDuringActorConstruction`、`BlueprintCosmetic` 再各自长一套字段。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `Entries.csv` 中 `AddToViewport`、`AddToPlayerScreen`、`RemoveFromViewport` 带有新的 cosmetic 约束标记。 2. 在 editor 与 dedicated server 或等价受限上下文中分别运行回归，确认前者仍可正常调用，后者会被 trait/guard 正确阻断。 3. 复查 bind-db 回放路径，确认 `bBlueprintCosmetic` 不会在 non-editor/cooked 再次丢失。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-105 | Defect | 优先处理，先阻断 `ScriptNoExport` deprecated 节点重新泄漏进 generated script surface |
| P2 | Issue-106 | Architecture | 在执行约束语义统一治理时处理，把 `BlueprintCosmetic` 纳入 generated binding 正式契约 |

---

## 发现与方案 (2026-04-08 23:57)

### Issue-107：函数级 `ScriptMethod` 元数据没有进入 generated binding，实例方法 alias 仍按原始 static `UFunction` 身份写表

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Public/Elements/Framework/EngineElementsLibrary.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:16-24, 48-54, 85-92, 254-313, 336-391, 414-457`; `Bind_BlueprintCallable.cpp:51-55, 101-138`; `AngelscriptBindDatabase.h:56-86`; `AngelscriptUhtCoverageTestTypes.h:31-32`; `AngelscriptBindConfigTests.cpp:632-644`; `EngineElementsLibrary.h:37-39, 50-52, 61-63, 72-74`; `KismetSystemLibrary.h:2223-2248`; `AS_FunctionTable_Entries.csv:1167-1170, 2509-2510` |
| 问题 | `FAngelscriptFunctionSignature` 当前只声明并读取 `ScriptName`、`WorldContext`、`DeterminesOutputType`、`ScriptGlobalScope`、类级 `ScriptMixin`、`ScriptTrivial`、`NotAngelscriptProperty`、`BlueprintProtected` 等少量元数据，没有任何函数级 `ScriptMethod`/`ScriptMethodMutable`/`ScriptMethodSelfReturn` 字段或解析分支；bind-db schema 也完全没有对应持久化位。`Bind_BlueprintCallable()` 因而只能在 `bStaticInScript` 与 `bStaticInUnreal` 两个现有分支之间选择，无法让函数级 `ScriptMethod` 把“第一个参数”转换成脚本接收者。仓库自己的覆盖样例已经把这条契约写进测试：`UAngelscriptUhtCoverageTestLibrary::GetCoverageValue` 明确带 `meta=(ScriptMethod)`，而 `FAngelscriptScriptMethodMetadataCoverageTest` 断言它应绑定成脚本成员方法、去掉首参并保留 const 成员声明。与此同时，实际引擎头里 `UEngineElementsLibrary::K2_AcquireEditorObjectElementHandle` 等 4 个函数都声明了 `ScriptMethod="AcquireEditorElementHandle"`，`UKismetSystemLibrary::GetPrimaryAssetIdList` / `IsValidPrimaryAssetId` / `Conv_PrimaryAssetIdToString` 也都依赖 `ScriptMethod` 来表达真实脚本名字；但当前 `Entries.csv` 仍把它们写成原始 static `UFunction` 身份，例如 `UEngineElementsLibrary,K2_AcquireEditorObjectElementHandle` 与 `UKismetSystemLibrary,Conv_PrimaryAssetIdToString`。 |
| 根因 | native generated binding 的 schema 只实现了类级 mixin/namespace 重写，没有实现函数级 script-surface 重写层；结果是“Blueprint 原始 `UFunction` 身份”和“脚本真正可见的实例方法身份”被混成同一个模型。 |
| 影响 | 当前 generated table 与 sidecar 不能正确表达 `ScriptMethod` API：脚本用户应看到的实例方法 alias，会在 diagnostics 中表现为 raw static `K2_`/`Conv_` 函数名；去重、覆盖率、失败原因和后续 operator/autocast 扩展都会建立在错误身份上。已验证事实是 `AcquireEditorElementHandle` 这一类 API 已经被错误记账；推断：即使后续单独修好某些 header 解析 stub，脚本表面仍会继续漂移，因为 binding mode 本身就没有函数级 `ScriptMethod` 契约。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 runtime signature、bind-db 和 UHT sidecar 中补齐函数级 `ScriptMethod` 契约，把“脚本接收者/脚本名字”和“原始 `UFunction` 名称”显式拆成两个维度。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 中新增函数级 `ScriptMethod` 承载字段，例如 `ScriptMethodName`、`bScriptMethodMutable`、`bScriptMethodSelfReturn` 或等价的 `BindingKind` 枚举；不要再把函数级实例方法重写继续塞进类级 `ScriptMixin` 语义。 2. 在 `InitFromFunction()` 中读取 `ScriptMethod` 元数据：若 metadata 为空值，则回落到 `GetScriptNameForFunction()` 的结果；若 metadata 带显式名字，则以 metadata 为准。随后把首个参数验证为可作为脚本接收者的 object/reference/value type，成功时移除该参数、把 `ClassName` 改成接收者脚本类型、将 `bStaticInScript` 置为 `false`，并根据首参 const 性与 `ScriptMethodMutable` 共同决定最终声明是否为 const 成员。 3. 将上述字段写入 `FAngelscriptMethodBind`，同步更新 `WriteToDB()` / `InitFromDB()` 与序列化顺序，确保 cooked / `AS_USE_BIND_DB` 回放不再丢失 `ScriptMethod` 语义。 4. 在 `Bind_BlueprintCallable.cpp` 中为函数级 `ScriptMethod` 复用当前 class-level mixin 的 `BindMethodDirect(... asCALL_CDECL_OBJFIRST)` 路径，但绑定目标类、脚本声明与参数列表均来自新的函数级 metadata 结果，而不是继续沿用原始 static library 身份。 5. 扩展 `AngelscriptFunctionTableCodeGenerator.cs` 的 sidecar schema，至少同时输出 `SourceClassName` / `SourceFunctionName` 与 `ScriptOwnerClass` / `ScriptFunctionName` / `BindingKind`；禁止 `Entries.csv` 再把 `K2_AcquireEditorObjectElementHandle` 这种 source-level 名称伪装成最终 script-facing 身份。 6. 在 `AngelscriptBindConfigTests.cpp` 复用现有 `GetCoverageValue` 样例，补齐真正的回归断言；再在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加 engine 侧样本，至少覆盖 `K2_AcquireEditorObjectElementHandle` 与 `Conv_PrimaryAssetIdToString`，确认前者的 script-facing 身份变为 `UObject.AcquireEditorElementHandle(...)`，后者即便暂时仍是 stub，也必须在 sidecar 中显示 `ToString` 而不是 raw `Conv_` 名称。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦 sidecar 开始区分 source-level 与 script-facing 身份，现有依赖 `ClassName,FunctionName` 原始列的外部脚本/测试可能需要迁移；但继续让原始 `UFunction` 名称冒充最终脚本名字，会让后续 operator/autocast 治理继续建立在错误基线之上。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `AS_FunctionTable_Entries.csv` 对 `UEngineElementsLibrary` 的 4 个 `K2_AcquireEditor*` 函数新增 script-facing 列，并显示 `AcquireEditorElementHandle`，而不是只保留 raw `K2_` 名称。 2. 运行 `AngelscriptBindConfigTests`，确认 `FunctionLevelScriptMethodUsesFirstParameterAsMixin` 覆盖样例通过，且 `FAngelscriptFunctionSignature` 对 `GetCoverageValue` 不再暴露首参。 3. 检查 `bind-db` 回放路径，确认 `ScriptMethod` 语义在 `AS_USE_BIND_DB` 下仍保留，不会在 non-editor/cooked 再次退回 static raw 名称。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-107 | Defect | 紧接着执行，先把函数级 `ScriptMethod` 契约补进 generated binding，避免 sidecar 和真实 script surface 持续分叉 |

---

## 发现与方案 (2026-04-08 00:29)

### Issue-108：`Latent` / `LatentInfo` 元数据完全没有进入 generated binding 契约，latent Blueprint 节点被当成普通 `Stub` 条目处理

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptAbilityTaskLibrary.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/GameplayStatics.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:16-29, 42-55, 222-249, 340-341, 414-457`; `Bind_BlueprintCallable.cpp:72-90, 100-151`; `BlueprintCallableReflectiveFallback.cpp:254-288, 374-420`; `AngelscriptAbilityTaskLibrary.h:557-560`; `KismetSystemLibrary.h:665-712`; `GameplayStatics.h:305-314`; `AS_FunctionTable_Entries.csv:1376,1429,2520-2522,2691,2710,5726` |
| 问题 | 当前 native generated binding 只保存 `WorldContext`、`DeterminesOutputType`、`BlueprintProtected`、deprecated、editor-only 等少量语义，没有任何 `Latent`、`LatentInfo` 或 latent callback 相关字段。与此同时，引擎与插件内已经有大量 latent BlueprintCallable：`UKismetSystemLibrary::Delay` / `DelayUntilNextTick` / `DelayUntilNextFrame` / `RetriggerableDelay` / `MoveComponentTo` 都带 `meta=(Latent, LatentInfo="LatentInfo")`，`UGameplayStatics::LoadStreamLevel` / `UnloadStreamLevel` 也依赖 `FLatentActionInfo`，插件自己的 `UAngelscriptAbilityTaskLibrary::WaitDelay` 则把能力任务等待节点直接暴露成 BlueprintCallable。现有产物已经把这批 API 当成普通函数表条目输出，例如 `Delay`、`MoveComponentTo`、`LoadStreamLevel`、`UnloadStreamLevel`、`WaitDelay` 全都进入 `AS_FunctionTable_Entries.csv`，只是统一记成 `Stub,ERASE_NO_FUNCTION()`。 |
| 根因 | UHTTool 与 runtime 绑定链路把 latent 节点视为“没有直绑指针的普通函数”，但 schema 中没有任何字段承载 latent continuation 语义；`Bind_BlueprintCallable()` 与 reflective fallback eligibility 也都没有为 `Latent` / `FLatentActionInfo` 建立专门分支。 |
| 影响 | 当前 sidecar 会把 latent 节点伪装成普通 `Stub`，开发者无法从产物上区分“可通过 reflective fallback 正常调用的同步函数”和“需要 latent continuation / task adapter 的异步节点”。更严重的是，runtime 若未来直接放开这类 fallback，也没有地方表达 callback target、resume 点或脚本挂起策略，latent API 的可用性边界会继续停留在隐式行为。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `Latent` / `LatentInfo` 升级成 generated binding 的正式语义字段，显式区分“受支持的脚本 await/task 适配 latent 节点”和“必须排除的普通 latent Blueprint 节点”。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 新增 `bLatent`、`LatentInfoArgument`、`LatentWorldContextArgument` 或等价字段，从 `Function->HasMetaData(TEXT("Latent"))` 与 `Function->GetMetaData(TEXT("LatentInfo"))` 读取语义，并在 `WriteToDB()` / `InitFromDB()` 中持久化。 2. 在 `as_scriptfunction.h` 或等价 runtime metadata 层新增 latent 标记与 latent-info 参数索引；`ModifyScriptFunction()` 需要把这些字段写回脚本函数，不能继续只写 `determinesOutputTypeArgumentIndex`。 3. 把当前所有 latent BlueprintCallable 分成两类：一类是有现成脚本异步模型可承接的节点，例如 ability task / async proxy，应通过专门 wrapper 或 `await`/task 适配层暴露；另一类是依赖 `FLatentActionInfo` continuation 的原生 latent 节点，应在 `ShouldGenerate()` 或 `BindBlueprintCallable()` 中显式排除，并写入 `ExcludedEntries.csv` 或新增 `SkippedReason = latent-unsupported`。 4. `Bind_BlueprintCallable.cpp` 与 `BlueprintCallableReflectiveFallback.cpp` 都要新增 latent gate，禁止没有 latent runtime 适配的函数继续走普通 direct/fallback 路径。 5. 在 `AS_FunctionTable_Entries.csv` / `Summary.json` 中增加 `BindingConstraint` 或 `AsyncModel` 字段，让 `Delay`、`MoveComponentTo`、`LoadStreamLevel`、`WaitDelay` 不再显示成无差别 `Stub`。 6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 新增回归：至少锁住 `UKismetSystemLibrary::Delay`、`UGameplayStatics::LoadStreamLevel`、`UAngelscriptAbilityTaskLibrary::WaitDelay` 三类样本，分别验证“latent-unsupported 被显式标出”和“允许脚本等待的能力任务节点有专门语义标记”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | latent 节点牵涉脚本挂起/恢复模型；如果先简单“一刀切排除”，会减少当前函数表数量；如果直接允许 fallback 调用，则可能把 continuation 责任无声转嫁给脚本调用者，因此必须先定义清晰支持矩阵。 |
| 前置依赖 | 建议与 Issue-97 / Issue-98 / Issue-106 统一设计 generated binding 的 `InvocationConstraint` / `AsyncModel` 承载方式，避免执行约束与异步语义继续分裂。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `Delay`、`MoveComponentTo`、`LoadStreamLevel`、`UnloadStreamLevel`、`WaitDelay` 在 sidecar 中带有明确 latent 状态，而不是普通 `Stub`。 2. 对“暂不支持”的 latent 节点，检查 `SkippedEntries.csv` 或新增 `ExcludedEntries.csv` 中出现受控 reason。 3. 对“明确支持”的节点，运行对应脚本/自动化，确认调用后能按设计进入等待或能力任务流程，而不是普通同步函数行为。 4. 复查 bind-db 回放路径，确认 latent 字段不会在 non-editor/cooked 再次丢失。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-108 | Architecture | 先定义 latent 支持矩阵，再决定哪些节点应转 wrapper，哪些应显式排除 |

---

## 发现与方案 (2026-04-08 00:35)

### Issue-109：`DynamicOutputParam` 完全没有进入 generated binding / compiler 契约，动态输出数组节点会退化成静态基类类型

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/GameplayStatics.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Blueprint/WidgetBlueprintLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:18, 43, 237-249, 301-304, 340-341, 385-386, 417-457`; `AngelscriptBindDatabase.h:56-86`; `as_scriptfunction.h:421-423`; `as_compiler.cpp:16021-16030`; `AngelscriptDebugServer.cpp:1690-1695`; `GameplayStatics.h:97-107, 125-126`; `WidgetBlueprintLibrary.h:302-313`; `AS_FunctionTable_Entries.csv:87,1329-1331,4926-4927` |
| 问题 | 运行时签名模型当前只保存 `DeterminesOutputTypeArgument`，完全没有 `DynamicOutputParam` 字段。编译器侧也只在 `func->determinesOutputTypeArgumentIndex` 命中时用 `ApplyDeterminesOutputType()` 覆盖 `func->returnType`，不会改写任何输出参数类型。可 UE 头文件已经大量把两者配套使用：`UGameplayStatics::GetAllActorsOfClass` / `GetAllActorsWithInterface` / `GetAllActorsOfClassWithTag` 都声明了 `meta=(DeterminesOutputType="...", DynamicOutputParam="OutActors")`，`UWidgetBlueprintLibrary::GetAllWidgetsOfClass` / `GetAllWidgetsWithInterface` 也同样依赖 `FoundWidgets` 动态输出。现有 UHT 产物已把这些 API 正式写进函数表，例如 `GetAllActorsOfClass`、`GetAllActorsWithInterface`、`GetAllWidgetsOfClass`、`GetAllWidgetsWithInterface` 都出现在 `AS_FunctionTable_Entries.csv`。 |
| 根因 | generated binding 只实现了“动态返回类型”这一半契约，没有实现“动态输出参数类型”这一半；bind DB、script function runtime state 和 debug/introspection 输出都没有为 `DynamicOutputParam` 预留索引或类型传播逻辑。 |
| 影响 | 这会让 Blueprint 里本应根据 `ActorClass` / `WidgetClass` / `Interface` 推导出 `TArray<Derived>` 的节点，在 Angelscript 里退化成静态 `TArray<AActor>` / `TArray<UUserWidget>` / `TArray<UActorComponent>` 视图。结果要么是调用点失去类型推断与成员可见性，要么脚本作者被迫自己做二次转换；debug server 也只会暴露 `outputTypeIndex`，无法告诉 IDE/工具“哪个 out 参数是动态类型容器”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `DynamicOutputParam` 补成与 `DeterminesOutputType` 对等的一等契约，从 UFunction metadata 一路传到 bind DB、script function、编译器和调试协议。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 中新增 `int8 DynamicOutputParamArgument = -1` 或等价结构，读取 `Function->GetMetaData(TEXT("DynamicOutputParam"))`，按参数名定位对应 out 参数；static->mixin 参数移除时也要同步修正该索引。 2. 在 `FAngelscriptMethodBind` 和 `WriteToDB()` / `InitFromDB()` 中持久化该字段，避免 cooked / `AS_USE_BIND_DB` 路径再次丢语义。 3. 在 `as_scriptfunction.h` 新增 `dynamicOutputParamArgumentIndex`，并在 `ModifyScriptFunction()` 写回；`AngelscriptDebugServer.cpp` 需要同时输出 `outputTypeIndex` 与 `dynamicOutputParamIndex`。 4. 扩展 `as_compiler.cpp`：当函数同时带 `determinesOutputTypeArgumentIndex` 与 `dynamicOutputParamArgumentIndex` 时，不仅覆写返回类型，还要覆写对应 out/ref 容器参数的元素类型，例如把 `array<AActor@>&` 变成 `array<MyActor@>&`。 5. 对纯 `DynamicOutputParam` 但无返回值的函数，新增针对 out 参数的类型推导路径，不能继续把 `ApplyDeterminesOutputType()` 只绑定到返回值。 6. 在 `AS_FunctionTable_Entries.csv` / `Summary.json` 中增加 `DynamicOutputParam` 列或 `FunctionSemanticFlags`，避免当前 sidecar 把这类节点与普通 `void` 函数混在一起。 7. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加 generated binding 回归，至少覆盖 `UGameplayStatics::GetAllActorsOfClass`、`UWidgetBlueprintLibrary::GetAllWidgetsOfClass` 和 `AActor::K2_GetComponentsByClass`：断言脚本里 `TArray<Derived>` 推导成立，而不是退化成基类数组。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_compiler.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | 编译器层引入动态 out 参数推导会影响调用匹配与 IDE/debug 展示；如果只修 editor 路径、不修 bind DB，会再次形成 cooked/editor 语义分叉。 |
| 前置依赖 | 建议与 Issue-107、Issue-108 一起统一 `FunctionSemanticFlags` / 索引元数据的 sidecar schema，避免每补一个 metadata 都单独加一列。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `GetAllActorsOfClass`、`GetAllWidgetsOfClass`、`K2_GetComponentsByClass` 的 sidecar 带有 `DynamicOutputParam` 标记。 2. 在脚本测试中以 `TArray<UMyWidget>` / `TArray<UMyActorComponent>` / `TArray<AMyActor>` 作为输出容器调用这些 API，确认编译与运行都通过。 3. 检查 debug server 返回的函数描述，确认不仅有 `outputTypeIndex`，还有对应的动态输出参数索引。 4. 在 bind-db 回放路径复测同一脚本，确认 cooked/non-editor 不会退回基类数组类型。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-109 | Architecture | 紧随 latent 之后处理，先补齐动态输出参数类型推导，否则大量集合查询节点仍会丢失脚本泛型能力 |

---

## 发现与方案 (2026-04-08 00:42)

### Issue-110：`BlueprintAutocast` 元数据在 UHT/native 绑定链路中完全丢失，隐式转换节点被降级成普通静态函数

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_InputEvents.cpp`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Blueprint/WidgetBlueprintLibrary.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/GameplayTags/Classes/BlueprintGameplayTagLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:251-323, 340-349, 414-457`; `Bind_BlueprintCallable.cpp:100-151`; `Bind_InputEvents.cpp:22-26`; `WidgetBlueprintLibrary.h:315-328`; `KismetSystemLibrary.h:333-358`; `BlueprintGameplayTagLibrary.h:287-305`; `AS_FunctionTable_Entries.csv:2507,2515,4279,4285-4286,4933-4936` |
| 问题 | UE 头文件里存在大量 `BlueprintAutocast` 函数，例如 `UWidgetBlueprintLibrary::GetInputEventFromKeyEvent` / `GetInputEventFromAnalogInputEvent`、`UKismetSystemLibrary::Conv_InterfaceToObject` / `Conv_SoftObjRefToSoftObjPath`、`UBlueprintGameplayTagLibrary::Conv_ObjectToGameplayTagAssetInterface` / `GetDebugStringFromGameplayTag`。但 native generated binding 当前完全不读取 `BlueprintAutocast`：`FAngelscriptFunctionSignature` 只会生成普通 `Declaration`，`Bind_BlueprintCallable()` 也只会按“普通全局函数 / namespace 函数 / 成员函数”三种路径注册。与之对比，仓库已经有明确的脚本隐式转换表示，例如 `Bind_InputEvents.cpp` 手写把 `FName -> FKey` 绑定成 `FKey opImplConv() const`。现有 UHT 产物却仍把这类 UE autocast 节点写成普通函数表项，例如 `GetInputEventFromKeyEvent`、`GetInputEventFromCharacterEvent` 是 direct entry，`Conv_InterfaceToObject`、`Conv_SoftObjRefToSoftObjPath`、`Conv_ObjectToGameplayTagAssetInterface` 则是普通 stub。 |
| 根因 | generated binding schema 没有 `bBlueprintAutocast` 或等价字段，也没有把“普通 BlueprintCallable 函数”提升成 Angelscript `opImplConv` / conversion behaviour 的分支；因此 UHT 导出的函数只能保留原始 `Get...` / `Conv_...` 名字。 |
| 影响 | 这会让 Blueprint 里本应通过隐式转换/短箭头节点工作的 API，在 Angelscript 里退化成必须手工调用的静态函数，脚本表面与 Blueprint 语义持续分叉。更糟的是，当前 sidecar 没有任何 autocast 标记，开发者无法分辨哪些 `Conv_` / `Get...From...` 节点理论上应是转换行为，哪些只是普通 helper。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `BlueprintAutocast` 纳入 generated binding 契约，统一映射到 Angelscript 的 `opImplConv` / `opConv` 表示，而不是继续保留原始 helper 函数身份。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 新增 `bBlueprintAutocast`，通过 `Function->HasMetaData(TEXT("BlueprintAutocast"))` 读取，并在 `WriteToDB()` / `InitFromDB()` 中持久化。 2. 设计 `BuildAutocastDeclaration()`：当函数满足 autocast 约束时，把声明改写成脚本转换行为，例如零或单参数 static library helper 需要重写为接收者类型上的 `opImplConv()` / `opConv()`；不能继续沿用原始 `Conv_*` / `Get*From*` 名字。 3. 在 `Bind_BlueprintCallable.cpp` 为 autocast 增加专门路径，优先复用现有 mixin/object-first 绑定设施，把首参数类型提升为脚本接收者；若函数不满足合法转换签名，则输出明确 reason `autocast-unsupported-signature`，不要静默当普通函数绑定。 4. 对 direct 与 reflective fallback 两条路径都补 autocast 语义，避免 direct 能转行为、stub 却退回普通 helper。 5. 扩展 `AS_FunctionTable_Entries.csv` / `Summary.json`，新增 `BindingKind=Autocast` 或 `FunctionSemanticFlags`，让 `GetInputEventFromKeyEvent`、`Conv_InterfaceToObject` 这类条目不再显示成普通 `Direct/Stub`。 6. 在测试中增加 engine 样本，至少覆盖 `UWidgetBlueprintLibrary::GetInputEventFromKeyEvent`、`UKismetSystemLibrary::Conv_InterfaceToObject`、`UBlueprintGameplayTagLibrary::Conv_ObjectToGameplayTagAssetInterface`：断言脚本里存在对应 `opImplConv`，并验证隐式转换能通过编译。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 预估工作量 | M |
| 风险 | 自动把 `Conv_*` 重写成 conversion behaviour 需要严格校验签名；如果误把普通 helper 当成 autocast，会污染类型系统并引入模糊重载。 |
| 前置依赖 | 建议与 Issue-107 一起设计统一的“script-facing 身份改写”层，避免 `ScriptMethod`、`BlueprintAutocast` 各自长一套平行改名逻辑。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `GetInputEventFromKeyEvent`、`Conv_InterfaceToObject`、`Conv_ObjectToGameplayTagAssetInterface` 在 sidecar 中带有 `Autocast` 语义，而不是普通 `Direct/Stub`。 2. 编写脚本回归，直接验证 `FKeyEvent` 到 `FInputEvent`、`FScriptInterface` 到 `UObject`、`UObject` 到 `IGameplayTagAssetInterface` 的隐式转换是否可编译。 3. 对一条不满足 autocast 签名的普通 helper 做负向测试，确认不会被误注册成 `opImplConv`。 4. 复查 bind-db 回放路径，确认 cooked/non-editor 仍保留 autocast 语义。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-110 | Architecture | 在 `ScriptMethod` 身份改写层稳定后处理，把 Blueprint 隐式转换统一映射为 `opImplConv/opConv` |

---

## 发现与方案 (2026-04-09 00:41)

### Issue-111：`BlueprintThreadSafe` / `NotBlueprintThreadSafe` 没有进入 generated binding 契约，native UFUNCTION 与脚本类函数的线程安全语义已经分叉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/ASClass.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/GameplayStatics.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptEngine.h:1012,1050`; `AngelscriptClassGenerator.cpp:118-119,527-563,3343-3345`; `ASClass.cpp:1772-1778,1924-1928,1931-1935,1961-1968`; `as_context.cpp:5165-5177`; `PrecompiledData.cpp:468-477,2962-2968`; `Helper_FunctionSignature.h:16-32,33-55,222-249,260-323,414-457`; `AngelscriptBindDatabase.h:56-86`; `Bind_BlueprintCallable.cpp:50-151`; `as_scriptfunction.h:421-427`; `AngelscriptFunctionTableCodeGenerator.cs:14-17,37-40,244-260,465-479`; `GameplayStatics.h:1509-1537`; `AS_FunctionTable_Entries.csv:1,1343-1382` |
| 问题 | runtime 已经为脚本类函数建立了完整的 thread-safe 执行语义：`FAngelscriptFunctionDesc` 有 `bThreadSafe`，脚本类生成阶段会读取 `BlueprintThreadSafe` / `NotBlueprintThreadSafe`，并在 `UASFunction` 与 `UASFunction_NotThreadSafe` 两条执行路径之间分流；`as_context.cpp` 还会在 `BlueprintThreadSafe` 函数里拦截 `WorldContext` 调用，`StaticJIT/PrecompiledData.cpp` 也会序列化 `bThreadSafe`。但 native generated binding 这条链路没有任何等价字段：`FAngelscriptFunctionSignature` 只承载 `WorldContextArgument` / `DeterminesOutputTypeArgument`，`FAngelscriptMethodBind` 也没有 thread-safe 字段，`Bind_BlueprintCallable.cpp` 绑定完成后只调用 `ModifyScriptFunction()` 回写 world-context、deprecated、editor-only 等元数据，UHT sidecar 也只输出 `ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex`。引擎头文件里已经存在真实样本，`UGameplayStatics::GetKeyValue`、`ParseOption`、`HasOption`、`GetIntOption` 都带 `meta=(BlueprintThreadSafe)`，但当前导出的 `AS_FunctionTable_Entries.csv` 仍只把它们记成普通 `Direct` 条目。 |
| 根因 | thread-safe 语义目前只在“脚本类生成器 -> `FAngelscriptFunctionDesc` -> `UASFunction` / JIT / precompiled data”这条链上存在，UHTTool 和 native generated binding 从未把它当成一等契约来建模；结果是 editor 导出、bind-db 回放、native direct/stub 绑定都没有地方保存或传播这组 metadata。 |
| 影响 | native UFUNCTION 即使在 UE 头文件中明确声明 `BlueprintThreadSafe`，进入 Angelscript generated binding 后也会丢失身份，sidecar、cooked 回放和 runtime 诊断都无法区分“普通 callable”与“线程安全 callable”。这不只是文档缺失问题；当前引擎已经用 `GIsInAngelscriptThreadSafeFunction` + `asTRAIT_USES_WORLDCONTEXT` 约束线程安全函数的调用边界，但 native generated binding 没有接入这套语义，最终会让脚本类函数和 native UFUNCTION 在同一套执行模型里表现出不同的安全边界。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 thread-safe metadata 升级为 generated binding 的正式字段，并让 direct/stub/bind-db/sidecar 与脚本类函数共享同一套 thread-safe 判定与运行时约束。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 引入显式线程安全字段，建议使用 `EAngelscriptThreadSafety` 或 `bThreadSafe + bExplicitNotThreadSafe`，同时读取函数级 `BlueprintThreadSafe` / `NotBlueprintThreadSafe` 以及 outer class 上的 `BlueprintThreadSafe`，规则必须与 `AngelscriptClassGenerator.cpp:527-563` 保持一致，而不是再造一套近似逻辑。 2. 将该字段写入 `FAngelscriptMethodBind`，同步扩展 `WriteToDB()` / `InitFromDB()`；同时在 `as_scriptfunction.h` 增加 thread-safe 标记位或新增 `asTRAIT_BLUEPRINT_THREADSAFE`，让 `ModifyScriptFunction()` 能把 thread-safe 元数据写回脚本函数对象，而不是只写 `hiddenArgumentIndex` / `determinesOutputTypeArgumentIndex`。 3. 在 native 调用桥接层接入这组元数据：direct bind、reflective fallback 和 bind-db 回放路径都要在进入 native 调用前设置与清理 `GIsInAngelscriptThreadSafeFunction`，从而让 `as_context.cpp:5165-5177` 的 world-context 约束同样适用于 generated binding，而不是只保护脚本类函数。 4. 扩展 `AngelscriptFunctionTableCodeGenerator.cs` 的 sidecar schema，为 `AS_FunctionTable_Entries.csv` / `Summary.json` 增加 `ThreadSafetyMode` 或等价列，至少能区分 `None / BlueprintThreadSafe / NotBlueprintThreadSafe / InheritClass`，禁止继续把所有条目都压扁成普通 `Direct/Stub`。 5. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 中加入 engine 样本，锁住 `UGameplayStatics::GetKeyValue`、`ParseOption`、`HasOption`、`GetIntOption` 的导出结果；同时在 runtime 绑定测试中增加负向验证，确认 thread-safe native 函数内调用 `WorldContext` API 时会触发与脚本类函数相同的保护。 6. 若短期内不准备在 runtime 落地完整 thread-safe 执行保护，至少要先把该信息写进 sidecar 与 bind-db，并在导出报告中显式标注“metadata 已识别但 runtime 尚未执行”的状态，避免继续无声丢失。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_context.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 预估工作量 | L |
| 风险 | 如果只在 sidecar 增加 `ThreadSafetyMode` 而不修改 runtime 调用桥接，导出结果会比实际执行语义更“正确”，但运行时仍然不受保护；反过来，如果直接把 class-level `BlueprintThreadSafe` 无差别传播到所有 native 函数，又可能把本应由 `NotBlueprintThreadSafe` 逃逸的函数错误收紧，因此必须复用脚本类现有判定规则。 |
| 前置依赖 | 建议与 Issue-97 / Issue-106 这类 sidecar 语义扩展项统一设计 `FunctionSemanticFlags` 或版本化 schema，避免每补一个 metadata 就再加一组平行列。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `AS_FunctionTable_Entries.csv` 中 `UGameplayStatics::GetKeyValue`、`ParseOption`、`HasOption`、`GetIntOption` 带有明确 thread-safe 标记，而不是只有 `Direct`。 2. 在 `AS_USE_BIND_DB` 与非 `AS_USE_BIND_DB` 两条路径下重复导出/加载，确认 thread-safe 信息不会在 bind-db 回放时丢失。 3. 运行新增自动化测试，确认 thread-safe native 函数进入 runtime 后会设置对应 guard，并在其内部访问 `WorldContext` API 时复现与 `as_context.cpp:5165-5177` 一致的报错边界。 4. 复查 JIT / precompiled data 路径，确认 native generated binding 不会再成为唯一缺失 `bThreadSafe` 语义的分支。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-111 | Architecture | 优先处理；先统一 thread-safe metadata 的建模与回放，再继续扩展 native generated binding 的执行约束 |

---

## 发现与方案 (2026-04-09 00:41)

### Issue-112：`ScriptOperator` 元数据完全没有进入 generated binding，运算符节点在函数表里被降级成普通 helper 名称

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetMathLibrary.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:16-32,33-55,251-333,414-457`; `Bind_BlueprintCallable.cpp:50-151`; `AngelscriptBindDatabase.h:56-86`; `AngelscriptFunctionTableCodeGenerator.cs:14-17,37-40,244-260,465-479`; `KismetMathLibrary.h:922-958,1368-1428`; `KismetSystemLibrary.h:2227-2240`; `AS_FunctionTable_Entries.csv:1630,1640,1792,2072,2544,2632,2693` |
| 问题 | 我对 `Plugins/Angelscript/Source/AngelscriptRuntime` 与 `Plugins/Angelscript/Source/AngelscriptUHTTool` 执行 `rg --line-number "ScriptOperator"`，结果为 `0` 行；当前 generated binding 链路根本不读取这组 metadata。`FAngelscriptFunctionSignature` 只识别 `ScriptName`、`WorldContext`、`DeterminesOutputType`、`ScriptMixin` 等字段，`FAngelscriptMethodBind` 也没有 operator 相关存储位，`Bind_BlueprintCallable.cpp` 仅按“namespace global / mixin member / native member”三类普通函数注册。与之对比，引擎头文件已经大量使用 `ScriptOperator`：`UKismetMathLibrary::Add_IntPointIntPoint`、`Add_VectorVector`、`Multiply_VectorVector`、`EqualEqual_VectorVector` 分别声明了 `ScriptOperator="+;+="`、`"*;*="`、`"=="`；`UKismetSystemLibrary::IsValidPrimaryAssetId` 则声明 `ScriptMethod="IsValid", ScriptOperator="bool"`，`EqualEqual_PrimaryAssetId` / `NotEqual_PrimaryAssetId` 也分别声明 `"=="` / `"!="`。但当前 `AS_FunctionTable_Entries.csv` 只保留原始 helper 名称，并把上述样本全部导出为普通 `Stub` 条目。 |
| 根因 | UHTTool 当前只把 native BlueprintCallable 当成“函数声明 + erase macro”问题来处理，没有为 operator identity 建模；`ScriptOperator` 既没有进入 signature parser，也没有进入 bind-db 或 sidecar schema，因此 generated binding 永远只能暴露 `Add_*` / `EqualEqual_*` / `IsValid*` 这类源码级 helper 名称。 |
| 影响 | 即使后续把 `ScriptMethod` 或 direct bind 细节修好，缺少 `ScriptOperator` 仍会让大量数学、资产标识和基础 value type API 无法以 Angelscript 运算符表面暴露。开发者看到的是 `Add_VectorVector(...)`、`EqualEqual_PrimaryAssetId(...)` 这种 Blueprint helper，而不是 `A + B`、`A == B`、`if (PrimaryAssetId)` 这样的 script-facing 语法契约；sidecar 也无法告诉调用方“这是 operator，不是普通函数”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `ScriptOperator` 提升为 generated binding 的一等语义，并建立从 UE metadata 到 Angelscript operator declaration 的规范映射层。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 新增 `ScriptOperatorSpec` / `BindingKind` / `OperatorTokens` 等字段，从 `Function->GetMetaData(TEXT("ScriptOperator"))` 读取原始值；不要继续只保留 `ScriptName`。 2. 新建统一映射 helper，例如 `BuildOperatorDeclarations(...)`，把 UE 常见取值映射到 Angelscript 运算符声明：`+` -> `opAdd`，`+=` -> `opAddAssign`，`*` -> `opMul`，`*=` -> `opMulAssign`，`==` -> `opEquals`，`neg` -> `opNeg`，`bool` -> `opImplConv/opConv`。对于 `"+;+="`、`"*;*="` 这类多 token 元数据，要一次生成主 operator 与 assignment operator 两个 script-facing 声明。 3. 将 operator 语义写入 `FAngelscriptMethodBind` 并持久化到 bind-db；禁止 cooked / `AS_USE_BIND_DB` 路径在 editor 外再次退回 raw helper 名。 4. 在 `Bind_BlueprintCallable.cpp` 为 operator 新增专门注册路径，优先复用现有 mixin/object-first 基础设施，把首参提升为接收者类型；不满足合法 arity 或返回类型约束的函数必须给出明确 reason，例如 `script-operator-unsupported-signature`，不能静默当普通函数保留。 5. 扩展 `AngelscriptFunctionTableCodeGenerator.cs` 的 sidecar schema，至少新增 `BindingKind`、`OperatorTokens`、`ScriptFacingName` 三列，让 `Entries.csv` 能区分 `Operator` 与普通 `Direct/Stub`。 6. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 与 native binding 回归中增加最小样本，至少覆盖 `Add_VectorVector`、`EqualEqual_VectorVector`、`Multiply_VectorVector`、`IsValidPrimaryAssetId`、`EqualEqual_PrimaryAssetId`、`NotEqual_PrimaryAssetId`，断言它们不再只暴露 raw helper 名，而是生成对应 operator surface。 7. 与 Issue-107、Issue-110 统一规划命名改写顺序：`ScriptMethod` 解决“成员方法身份”，`BlueprintAutocast` 解决“隐式转换”，`ScriptOperator` 解决“运算符身份”；三者共享一套 `ScriptFacingIdentity` 模型，但必须分开建模，避免把 `operator bool` 混成普通 autocast。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Bindings/AngelscriptNativeEngineBindingsTests.cpp` |
| 预估工作量 | L |
| 风险 | operator 映射一旦做得过宽，会把普通 helper 误注册成 `opAdd/opEquals` 并污染重载解析；尤其 `ScriptOperator="bool"` 与 `BlueprintAutocast` 的边界必须先定义清楚，否则同一函数可能被重复注册成 conversion/operator。 |
| 前置依赖 | 建议复用 Issue-107 的 script-facing identity 重构结果；如果 `ScriptMethod` 尚未稳定，先引入独立 `BindingKind` 层，再把 operator/method/autocast 汇总到统一 schema。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `Entries.csv` 不再只把 `Add_VectorVector`、`EqualEqual_VectorVector`、`IsValidPrimaryAssetId` 记成普通 `Stub`，而是带有明确的 operator 语义列。 2. 在脚本侧新增编译回归，直接验证 `VectorA + VectorB`、`VectorA == VectorB`、`if (PrimaryAssetId)` 这类表达式能通过编译并命中对应 native 绑定。 3. 对一条带 `ScriptMethod` 但不带 `ScriptOperator` 的样本做负向测试，确认不会被误判成 operator。 4. 在 bind-db 回放路径重复验证，确认 operator 信息不会在 cooked/non-editor 丢失。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-112 | Architecture | 在 `ScriptMethod` 身份改写稳定后立即处理；否则数学/值类型 API 仍然无法恢复到 operator 语法面 |

---

## 发现与方案 (2026-04-09 00:41)

### Issue-113：`CallableWithoutWorldContext` 只在 editor/runtime 元数据层生效，function table sidecar 与 bind-db 没有序列化这条世界上下文约束

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptUhtCoverageTestTypes.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/VisualLogger/VisualLoggerKismetLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:223-235,340-341,385-386,417-429`; `AngelscriptBindDatabase.h:56-86`; `AngelscriptFunctionTableCodeGenerator.cs:14-17,37-40,244-260`; `AngelscriptBindConfigTests.cpp:239-240,647-703`; `AngelscriptUhtCoverageTestTypes.h:34-38`; `KismetSystemLibrary.h:561,576,599,644`; `VisualLoggerKismetLibrary.h:14-18,25-32,95-96`; `AS_FunctionTable_Entries.csv:2502,2701-2702,4003,4007` |
| 问题 | 当前 runtime/editor 路径已经能识别“可隐藏 world-context 参数但不设置 `asTRAIT_USES_WORLDCONTEXT`”这条语义：`FAngelscriptCallableWithoutWorldContextMetadataTest` 明确断言 `CallableWithoutWorldContext` 仍然隐藏参数，但不会被标记成 required world-context 函数。然而这条语义没有进入 generated binding 的持久化契约。`FAngelscriptFunctionSignature` 与 `FAngelscriptMethodBind` 只保存 `WorldContextArgument`，`ModifyScriptFunction()` 仍然是在 editor 编译分支里临时读取 `Function->HasMetaData(NAME_OptionalWorldContext)` 决定是否设置 trait；`AS_FunctionTable_Entries.csv` 的 schema 也只有 `ModuleName,EditorOnly,ClassName,FunctionName,EntryKind,EraseMacro,ShardIndex`，没有任何 world-context requirement 列。引擎真实 API 已大量使用这组 metadata，例如 `UKismetSystemLibrary::PrintString`、`PrintText`、`CollectGarbage` 以及 `UVisualLoggerKismetLibrary::LogText`、`LogLocation` 都声明了 `CallableWithoutWorldContext`，但当前函数表产物只把它们压成普通 `Stub` 条目。 |
| 根因 | generated binding 目前把 world-context 语义收缩成“是否存在一个 hidden argument index”，没有把“required world context”和“callable without world context”建成不同的状态；optionality 只能依赖 editor 下 live `UFunction` metadata 的临时判断，bind-db 和 sidecar 都没有等价存储位。 |
| 影响 | 已验证事实是当前函数表与 bind-db 都无法从产物本身区分 required/optional world-context。推断：只要后续要在 sidecar、cooked 回放、IDE/调试协议或 Issue-111 的 thread-safe/world-context 联动检查里复用这条语义，就会再次退回“必须依赖 live `UFunction` metadata 才能知道 optionality”的隐式耦合，generated binding 也就无法成为自洽的契约边界。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 world-context 约束从单一参数索引升级为显式模式枚举，并把该模式写入 bind-db、sidecar 和脚本函数元数据。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 引入 `EAngelscriptWorldContextMode` 或等价字段，至少区分 `None / Required / CallableWithoutWorldContext`；构造签名时继续保留 `WorldContextArgument`，但 optionality 不再依赖临时 `HasMetaData(NAME_OptionalWorldContext)` 分支。 2. 在 `FAngelscriptMethodBind` 中持久化该模式，并同步扩展 `WriteToDB()` / `InitFromDB()`；禁止 bind-db 只保存参数索引而丢失约束级别。 3. 调整 `ModifyScriptFunction()`：隐藏 world-context 参数与设置 `asTRAIT_USES_WORLDCONTEXT` 必须拆开，前者按 `WorldContextArgument` 处理，后者按 `WorldContextMode == Required` 处理；这样 editor 与非 editor 路径都能消费统一的序列化结果，而不是只在 `#if WITH_EDITOR` 里临时查 metadata。 4. 扩展 `AngelscriptFunctionTableCodeGenerator.cs` 的 sidecar schema，增加 `WorldContextMode` 或 `BindingConstraint` 列，让 `PrintString`、`CollectGarbage`、`LogText` 这类 API 不再与真正强依赖 world-context 的函数混成同一类。 5. 将现有 `FAngelscriptCallableWithoutWorldContextMetadataTest` 扩展为双路径验证：不仅检查 editor 下 `ModifyScriptFunction()` 的 trait 行为，还要通过 bind-db 或生成产物验证该模式已被持久化。 6. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 补 engine 样本，至少锁住 `PrintString`、`PrintText`、`CollectGarbage` 与 `LogText`；断言 sidecar 能明确显示 `CallableWithoutWorldContext`，而不是只剩 `Stub/Direct`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果直接把内部元数据名 `OptionalWorldContext` 暴露到 sidecar，会让 schema 与 UHT/BlueprintGraph 内部实现耦合过深；更稳妥的方式是对外统一输出 canonical `WorldContextMode`，内部再映射到具体 metadata。 |
| 前置依赖 | 建议与 Issue-111 共用 `FunctionSemanticFlags` / sidecar 版本化方案，避免 world-context mode 与 thread-safe mode 各自长一套平行字段。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `Entries.csv` 或 `Summary.json` 能区分 `Required` 与 `CallableWithoutWorldContext`。 2. 扩展现有自动化，确认 `CallableWithoutWorldContext` 在 bind-db 回放后仍然保持“隐藏参数但不设置 required trait”的行为。 3. 对 `PrintString` / `CollectGarbage` 与一条真正 required world-context 的样本做对照，确认两者在 sidecar 与脚本函数元数据上不再共享同一种 world-context 状态。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-113 | Architecture | 放在 thread-safe/world-context 统一语义层之后处理，优先补齐序列化与 sidecar，可避免后续工具链继续依赖隐式 live metadata |

---

## 发现与方案 (2026-04-09 00:57)

### Issue-114：`AutoCreateRefTerm` 元数据完全没有进入 generated binding 契约，Blueprint 可省略的临时 ref/container 参数在脚本侧会被错误收窄成必填

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/AudioMixer/Public/Quartz/AudioMixerClockHandle.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | `Helper_FunctionSignature.h:178-246,336-394`; `AngelscriptBindDatabase.h:56-87`; `KismetSystemLibrary.h:1265-1296`; `AudioMixerClockHandle.h:64-68,73-75,94-95,111-148`; `AS_FunctionTable_Entries.csv:2672,2721`; `Plugins/Angelscript/Source/AngelscriptTest/: rg 'AutoCreateRefTerm' => 0` |
| 问题 | `FAngelscriptFunctionSignature::InitFromFunction()` 当前只会读取 `CPP_Default_*`、`WorldContext` 和 `DeterminesOutputType`，完全没有解析 `AutoCreateRefTerm`；`FAngelscriptMethodBind` 也没有任何字段可以持久化这组参数名/索引，`InitFromDB()` 只能回放 `Declaration + WorldContextArgument + DeterminesOutputTypeArgument`。这意味着 Blueprint 用 `AutoCreateRefTerm` 允许省略的临时参数，在 generated/native binding 里不会被补成脚本默认值，也不会被标成“可隐式构造的可选 ref term”。引擎实际 API 已大量依赖这条语义，例如 `UKismetSystemLibrary::LineTraceSingle` / `SphereTraceSingle` 用它让 `ActorsToIgnore` 可省略，`UQuartzClockHandle::ResetTransport`、`NotifyOnQuantizationBoundary`、`SetBeatsPerMinute`、`GetBeatProgressPercent` 用它让 delegate、boundary、phase offset 能走临时项；当前函数表产物已经把其中一批条目纳入正式输出，例如 `AS_FunctionTable_Entries.csv:2672` 的 `LineTraceSingle` 与 `AS_FunctionTable_Entries.csv:2721` 的 `SphereOverlapActors`。 |
| 根因 | UHT/native 绑定链路把“参数默认值”建模成纯 `CPP_Default_*` 文本协议，没有把 `AutoCreateRefTerm` 视为一类独立的可调用契约；因此 editor 实时绑定、bind-db 回放和 sidecar schema 都缺少表达这条语义的状态位。 |
| 影响 | 已验证事实是当前 generated binding/bind-db 都没有 `AutoCreateRefTerm` 的存储位。推断：这些 API 在脚本侧会比 Blueprint 暴露出更窄的调用面，调用者必须手工传入空数组、空 delegate 或默认 boundary，才能覆盖 Blueprint 原本允许省略的场景；一旦后续只依赖 bind-db/cooked 回放，这种参数面收窄不会由 live metadata 自动补救。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `AutoCreateRefTerm` 提升为显式参数语义，统一写入签名构建、bind-db 和 sidecar，而不是继续把它当成“也许会有一个 `CPP_Default_*`”的偶然副产物。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 新增 `AutoCreateRefTermArguments`（按参数索引或参数名存储），从 `Function->GetMetaData("AutoCreateRefTerm")` 解析出目标参数集合。 2. 为这些参数增加 `BuildSyntheticDefaultArgument()`：对 `TArray` / `TSet` / `TMap` / delegate / struct ref 等 Blueprint 常见可临时构造类型生成稳定的脚本默认表达式；若某类参数当前无法安全生成默认文本，则在构建阶段显式记录 `unsupported-auto-create-ref-term`，不要静默降格成普通必填参数。 3. 扩展 `FAngelscriptMethodBind` 序列化格式，持久化 `AutoCreateRefTermArguments` 或等价位集；同步更新 `WriteToDB()` / `InitFromDB()`，确保 cooked 路径不依赖 live `UFunction` metadata。 4. 在 `FAngelscriptFunctionSignature::InitFromDB()` / `ModifyScriptFunction()` 消费这组信息，使 bind-db 回放得到的声明与 editor 直绑一致；禁止 editor 路径和 cooked 路径对是否可省略 ref term 出现分叉。 5. 扩展 `AngelscriptFunctionTableCodeGenerator.cs` 的 `Entries.csv` schema，增加 `AutoCreateRefTermArgs` 或 `OptionalArgumentPolicy` 列，避免 trace/quartz 这类 API 在 sidecar 里被误看成普通必填签名。 6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/` 新增覆盖样本：一条使用 `AutoCreateRefTerm="ActorsToIgnore"` 的 trace API，一条使用 `AutoCreateRefTerm="Delegate"` 的 Quartz API，分别验证 editor 直绑和 bind-db/cooked 回放都允许省略该参数。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 不同类型的“空临时项”默认表达式并不统一；如果直接用字符串拼接默认值，可能把部分复杂类型默认化错误。需要优先把生成逻辑落在 `FAngelscriptTypeUsage` / `FAngelscriptType` 能回答的类型能力上，避免为每个函数做 ad-hoc 特判。 |
| 前置依赖 | 建议与 Issue-50 的 schema/version 字段一起推进，避免 `Entries.csv` 新增 `AutoCreateRefTermArgs` 后再次做破坏式格式迁移。 |
| 验证方式 | 1. 对 `UKismetSystemLibrary::LineTraceSingle`、`SphereOverlapActors` 以及 `UQuartzClockHandle::ResetTransport` / `SetBeatsPerMinute` 生成脚本声明快照，确认 `ActorsToIgnore`、delegate、boundary 相关参数在脚本侧可省略。 2. 运行 editor 路径与 bind-db/cooked 路径各一次，比较生成的函数声明和调用行为一致。 3. 检查 `Entries.csv` 新列已标出这些参数，而不是继续把它们当成普通必填签名。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 保持 `rg 'AutoCreateRefTerm'` 至少命中新增回归测试，不再是 `0`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-114 | Defect | 在 world-context / determines-output-type 之后优先处理参数语义层，先补齐 `AutoCreateRefTerm` 的声明与 bind-db 契约，避免脚本 API 面继续比 Blueprint 更窄 |

---

## 发现与方案 (2026-04-09 01:00)

### Issue-115：`DefaultToSelf` / `HidePin` 没有进入 generated binding 契约，Blueprint 的隐式 `self` 上下文节点在脚本侧被降格成显式静态 helper

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/BehaviorTree/BTFunctionLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv`, `Plugins/Angelscript/Source/AngelscriptTest/` |
| 行号 | `Helper_FunctionSignature.h:178-246,251-333,336-394`; `AngelscriptBindDatabase.h:56-87`; `AngelscriptClassGenerator.cpp:116,3435-3441`; `BTFunctionLibrary.h:34-109`; `AS_FunctionTable_Entries.csv:4147-4168`; `Plugins/Angelscript/Source/AngelscriptTest/: rg 'DefaultToSelf|HidePin' => 0` |
| 问题 | 当前 native/generated 绑定路径只识别 `CPP_Default_*`、`WorldContext`、`DeterminesOutputType` 等少数参数语义，完全没有解析 `DefaultToSelf` / `HidePin`。因此 Blueprint 上本来以“隐藏 self 参数”形式暴露的节点，在脚本侧会退化成普通静态 helper。`UBTFunctionLibrary` 是已进入当前函数表的直接样本：`GetBlackboardValueAsObject`、`SetBlackboardValueAsObject`、`ClearBlackboardValue` 等 API 都在 header 中标了 `Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner")`，但当前 `Entries.csv:4147-4168` 仍把它们记成普通 `ERASE_AUTO_FUNCTION_PTR(UBTFunctionLibrary::...)` 条目，没有任何“隐式 self”或“隐藏参数”描述。更进一步，插件自己的脚本类生成链路已经把 mixin 首参写成 `DefaultToSelf` 元数据：`AngelscriptClassGenerator.cpp:3435-3441` 在生成函数时显式执行 `NewFunction->SetMetaData(NAME_Function_DefaultToSelf, *MixinArgumentName)`；native/generated 绑定却没有等价消费路径，说明两套函数来源的参数契约已经分叉。 |
| 根因 | 参数语义模型目前只覆盖“默认文本”和少量特殊索引，没有为“隐藏参数 + 默认 self 注入”建立独立状态；UHT function table、runtime signature 和 bind-db 都缺少表达 `DefaultToSelf` / `HidePin` 的字段。 |
| 影响 | 这不会让函数完全不可调用，但会把大量 Blueprint 上下文节点降级成更笨重的显式静态调用，破坏与 Blueprint/脚本类生成函数的一致性。推断：如果后续继续推进 `ScriptMethod`、`ScriptMixin`、Blueprint helper parity 或 IDE 提示，这组节点会持续表现为“功能能调，但调用面与文档/UI 都不一致”的长期结构性噪音。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为“隐藏参数 + 默认 self 注入”建立统一参数策略模型，并让 native/generated 绑定与脚本类生成函数共享同一套语义。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 新增 `ImplicitSelfArgument` / `HiddenArgumentPolicy` 结构，至少记录参数索引、原参数名，以及来源是 `DefaultToSelf` 还是其他隐藏参数规则。 2. 在 `InitFromFunction()` 解析 `DefaultToSelf` 与 `HidePin`：当两者同时命中同一参数，且参数类型与当前脚本接收者可建立稳定映射时，将该参数从公开声明中隐藏，并为它生成统一的默认注入表达式，例如复用 world-context 现有机制扩展出 `__Self()` 或等价内部占位。 3. 扩展 `FAngelscriptMethodBind` 的序列化格式，把这组隐藏参数策略写入 bind-db，避免 cooked 路径重新退化成显式参数。 4. 在 `ModifyScriptFunction()` 或等价绑定后处理阶段消费该策略，确保 editor 实时绑定、bind-db 回放以及 UHT sidecar 对“这是隐式 self 参数”给出一致结果。 5. 扩展 `AngelscriptFunctionTableCodeGenerator.cs` 的 sidecar schema，增加 `HiddenArgumentPolicy` / `ImplicitSelfArgument` 列，让 `BTFunctionLibrary` 这类节点不再与普通静态 helper 混成同一种签名。 6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/` 新增回归样本，至少覆盖 `UBTFunctionLibrary::GetBlackboardValueAsObject`、`SetBlackboardValueAsObject` 与一条插件内 mixin 生成函数，确认 native/generated 与 script-generated 两条路径对隐式 self 的表现一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | “默认 self” 不是对所有函数都安全，尤其是静态函数首参类型与当前脚本接收者不存在一一映射时。如果规则写得过宽，容易把本应显式传参的 helper 错误改造成实例方法。 |
| 前置依赖 | 建议与 Issue-107 的 `ScriptMethod` 语义治理一起设计，避免 `ScriptMethod`、`ScriptMixin`、`DefaultToSelf` 三套“首参转接收者”的规则继续并行演化。 |
| 验证方式 | 1. 对 `UBTFunctionLibrary::GetBlackboardValueAsObject`、`SetBlackboardValueAsObject`、`ClearBlackboardValue` 生成脚本声明快照，确认 `NodeOwner` 不再作为公开必填参数暴露。 2. 对比 script-generated mixin 函数与 native/generated BT helper 的声明和调用方式，确认两条路径在“隐式 self”上不再分叉。 3. 运行 bind-db/cooked 路径，确认隐藏参数策略可回放，不会只在 editor 里生效。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 保持 `rg 'DefaultToSelf|HidePin'` 至少命中新增长度回归测试，不再是 `0`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-115 | Architecture | 放在 `AutoCreateRefTerm` 之后处理，同步收敛“隐藏参数/隐式 self”策略，优先消除 native/generated 与 script-generated 函数的参数契约分叉 |

---

## 发现与方案 (2026-04-09 01:02)

### Issue-116：`DeprecatedFunction` 语义只在 editor 直绑路径生效，bind-db / cooked 回放会丢失 deprecated trait 与 message

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`, `../../UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:57-60,264-266,336-394,414-458`; `AngelscriptBindDatabase.h:56-87`; `AngelscriptEngineParityTests.cpp:631-643`; `KismetSystemLibrary.h:741-795,820-821`; `AS_FunctionTable_Entries.csv:2638-2665` |
| 问题 | `FAngelscriptFunctionSignature` 目前只在 `#if WITH_EDITOR` 下持有 `bDeprecated` 和 `DeprecationMessage`，并且 `ModifyScriptFunction()` 也只在 `#if WITH_EDITOR` 分支里写 `asTRAIT_DEPRECATED` 与 `deprecationMessage`。与此同时，`FAngelscriptMethodBind` 的持久化字段完全没有 deprecated 相关位，`WriteToDB()` / `InitFromDB()` 也不会保存或恢复这条语义。结果是 editor 直绑还能依赖 live `UFunction` metadata 设置 deprecated trait，但 bind-db / cooked 回放路径会天然丢失它。引擎实际样本并不缺：`UKismetSystemLibrary::K2_ClearTimerDelegate`、`K2_PauseTimerDelegate`、`K2_UnPauseTimerDelegate`、`K2_IsTimerActiveDelegate`、`K2_GetTimerElapsedTimeDelegate`、`K2_ClearTimerHandle` 都在 header 中显式带 `DeprecatedFunction` 和 `DeprecationMessage`，且这些函数已经被 UHT 函数表纳入当前输出（`AS_FunctionTable_Entries.csv:2638-2665`）；但现有测试只校验 `UFunction` 元数据本身存在，并没有校验脚本函数 trait 在 bind-db/cooked 路径是否仍然保留。 |
| 根因 | generated/native 绑定把 deprecated 视为 editor 附带信息，而不是脚本函数契约的一部分；因此该语义既没有进入 bind-db 序列化，也没有进入非 editor 的 `ModifyScriptFunction()` 路径。 |
| 影响 | 已验证事实是 deprecated 只在 editor 代码块里写回脚本函数。推断：同一 native API 在 editor 与 cooked 构建下会出现不同的脚本可见语义，editor 下能提示 deprecated message，cooked 下却退化成普通函数；这会让脚本迁移和兼容性提示只在开发环境可见，无法形成一致的运行时/预编译契约。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 deprecated 语义从 editor 专属元数据提升为可序列化的脚本函数契约，确保 editor 与 bind-db/cooked 共用同一回放路径。 |
| 具体步骤 | 1. 将 `FAngelscriptFunctionSignature` 中的 `bDeprecated` / `DeprecationMessage` 从 `#if WITH_EDITOR` 条件编译块中解耦，至少保留为 bind-db 可写入、可回放的稳定字段。 2. 扩展 `FAngelscriptMethodBind`，增加 `bDeprecated` 与 `DeprecationMessage` 的序列化位，并同步更新 `WriteToDB()` / `InitFromDB()`。 3. 调整 `ModifyScriptFunction()`：deprecated trait/message 的写回不应再放在 `#if WITH_EDITOR` 里，只保留 editor-only trait 的分支在 editor 编译块；这样 cooked 回放也能设置 `asTRAIT_DEPRECATED`。 4. 扩展 `AngelscriptFunctionTableCodeGenerator.cs` 的 sidecar schema，增加 `FunctionSemanticFlags` 或 `DeprecatedMessage` 摘要，让生成产物能直接看见哪些 entry 已是 deprecated。 5. 为 `K2_ClearTimerDelegate`、`K2_ClearTimerHandle` 或插件内 deprecated Blueprint helper 新增双路径测试：editor 直绑和 bind-db/cooked 回放都必须保留 deprecated trait 与 message。 6. 将现有 `AngelscriptEngineParityTests.cpp` 从“只看 UFunction metadata”扩展到“同时看脚本函数 traits”，避免未来再次出现 metadata 在源头存在、但生成契约里丢失的情况。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptBindConfigTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptEngineParityTests.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | deprecated message 属于用户可见文本；如果直接把完整 message 大量写入 sidecar / bind-db，可能抬高产物体积并引入 schema 兼容成本。可以对 sidecar 只写 `bDeprecated + message hash/summary`，而在 bind-db 保留完整 message。 |
| 前置依赖 | 建议与 Issue-50 的 schema/version 化一起做，保证新增 deprecated 字段有清晰的版本边界。 |
| 验证方式 | 1. 重新导出并回放 bind-db，检查 `K2_ClearTimerDelegate`、`K2_ClearTimerHandle` 等样本在非 editor 路径下仍带 `asTRAIT_DEPRECATED`。 2. 读取脚本函数对象，确认 deprecation message 与 UFunction metadata 一致，而不是只在 editor 环境有值。 3. 扩展自动化后，在 editor 与 cooked/DB 两条路径分别跑一次，确认 deprecated trait 不再分叉。 4. 检查 sidecar 新字段，确保 deprecated 样本能被直接识别。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-116 | Architecture | 放在参数语义层问题之后处理，优先消除 editor 与 bind-db/cooked 在 deprecated trait 上的契约分叉 |

---

## 发现与方案 (2026-04-09 01:25)

### Issue-117：`ParseParameterTypes()` 会截断“参数更多但前缀相同”的候选声明，把更长 overload 误判成 exact match

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/UMG/Public/Components/PanelWidget.h`, `J:/UnrealEngine/UERelease/Engine/Source/Editor/SubobjectDataInterface/Public/SubobjectDataSubsystem.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:495-500, 545-561`; `PanelWidget.h:58-66`; `SubobjectDataSubsystem.h:232-255`; `AS_FunctionTable_SkippedEntries.csv:3633, 3742`; `AS_FunctionTable_Entries.csv:4561` |
| 问题 | `TryParseDeclaration()` 只检查 `ParseParameterTypes()` 返回的数量是否等于 `function.ParameterProperties.Span.Length`。但 `ParseParameterTypes()` 在 `parameterIndex >= function.ParameterProperties.Span.Length` 时不是判失败，而是直接 `break`。这会把“同名前缀参数相同、但后面还有额外参数”的更长 overload 截断成期望前缀。当前已有两个实证：`UPanelWidget` 同时声明了 `AddChild(UWidget* Content)` 和 `AddChild(UWidget* Content, UPanelSlot* SlotTemplate)`，现有产物把 `UPanelWidget::AddChild` 记成 `overloaded-unresolved` 并在 `Entries.csv:4561` 退化成 `Stub`；`USubobjectDataSubsystem` 同时声明了 3 参 `DeleteSubobjects(...)` 与 5 参 `DeleteSubobjects(..., FSubobjectDataHandle& OutComponentToSelect, UBlueprint* BPContext = nullptr, bool bForce = false)`，当前 `SkippedEntries.csv:3633` 也把它记成 `overloaded-unresolved`。 |
| 根因 | 参数解析逻辑把“预期参数个数”当成了解析循环的停止条件，而不是候选声明是否合法的校验条件；因此更长 overload 的尾部参数会被静默吞掉。 |
| 影响 | 任何 Blueprint UFUNCTION 旁边只要存在“共享相同前缀参数、但多出 trailing helper 参数”的 plain C++ overload，都可能被错误折叠成候选冲突，直接降低 direct bind 覆盖，并把真实根因伪装成泛化的 `overloaded-unresolved`。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让候选参数解析独立于预期参数个数，完整解析后再做严格 arity 校验，禁止更长 overload 被前缀截断。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 重写 `ParseParameterTypes()`：先完整遍历 `rawParameters`，不要再在 `parameterIndex >= expectedLength` 时 `break`；若候选参数数大于或小于期望值，返回显式失败。 2. 将 `TryParseDeclaration()` 的参数数校验改成“两阶段”：先比较 `rawParameters.Count` 与 `function.ParameterProperties.Span.Length`，只在完全相等时才逐项比较类型。 3. 为这类场景新增更具体的 failure code，例如 `parameter-count-extra` 或 `candidate-has-extra-parameters`，避免继续和真实 overload 歧义混在 `overloaded-unresolved`。 4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 新增回归，至少覆盖 `UPanelWidget::AddChild` 与 `USubobjectDataSubsystem::DeleteSubobjects`，锁住“前缀相同但参数更多”的 helper overload 不会再污染匹配。 5. 重新导出后复查 `AS_FunctionTable_Entries.csv` 与 `AS_FunctionTable_SkippedEntries.csv`：`UPanelWidget::AddChild` 应恢复 direct entry，`USubobjectDataSubsystem::DeleteSubobjects` 不应再因为参数截断被归类为 `overloaded-unresolved`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果一次性把参数个数校验收紧，可能把当前依赖“宽松前缀匹配”误打误撞通过的少数样本暴露出来；需要配合更细粒度 failure code 和回归样本一起落地。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 不再包含 `UPanelWidget,AddChild,overloaded-unresolved` 与 `USubobjectDataSubsystem,DeleteSubobjects,overloaded-unresolved`。 3. 检查 `AS_FunctionTable_Entries.csv` 中 `UPanelWidget::AddChild` 从 `Stub,ERASE_NO_FUNCTION()` 变为 direct entry。 4. 运行新增自动化，确认“多 trailing 参数 overload”样本稳定通过，而参数真正不匹配的样本仍会被拒绝。 |

### Issue-118：缺少 `EpicGames.UHT` 兼容 facade，UE5.x UHT API 变更会同时打穿 generator/exporter/resolver/builder 四层

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:9-10, 51-86, 334-460`; `AngelscriptFunctionTableExporter.cs:6-8, 27-96`; `AngelscriptHeaderSignatureResolver.cs:6, 18-150, 465-573`; `AngelscriptFunctionSignatureBuilder.cs:4, 43-133`; `AngelscriptUHTTool.ubtplugin.csproj:41-51` |
| 问题 | 当前 `UHTTool` 的四个核心源码文件都直接 `using EpicGames.UHT.*`，并散落使用 `IUhtExportFactory`、`UhtModule.ScriptPackage`、`UhtClass.HeaderFile`、`UhtProperty.TypeTokens`、`AppendFullDecl(...)`、`UhtFunctionType`、`UhtClassType` 等内部类型与成员；工程文件也直接引用 `EpicGames.UHT.dll` / `UnrealBuildTool.dll`。这意味着工具没有自己的“语义边界层”，而是把生成策略、签名解析、模块发现和输出写盘全部绑在原始 UHT AST 表面。 |
| 根因 | 架构上没有先把 `EpicGames.UHT` 读成一套本地稳定模型，而是让 generator/exporter/resolver/builder 各自直接消费引擎内部类型。 |
| 影响 | 一旦 UE5.x 调整 `UhtModule`/`UhtFunction`/`UhtProperty` 的成员形态、flag API、header/source 位置信息或 export factory 契约，修复工作会同时散落到多个文件；同一个版本迁移难以被限制在单一兼容层，测试也很难在不加载完整引擎程序集的情况下覆盖这条边界。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 `UHTTool` 前面补一层本地 compatibility facade，把对 `EpicGames.UHT` 的直接依赖收敛到单一适配入口。 |
| 具体步骤 | 1. 新增 `Plugins/Angelscript/Source/AngelscriptUHTTool/UhtCompat/` 目录，定义本地只读模型，例如 `CompatModule`、`CompatClass`、`CompatFunction`、`CompatProperty`、`CompatExportContext`。 2. 新增唯一的 `UhtCompatReader.cs` / `IUhtCompatReader`，负责把 `IUhtExportFactory` 与 `Uht*` AST 转换成上述本地模型；这里集中处理 `HeaderFile`、`ScriptPackage`、`TypeTokens`、`AppendFullDecl()`、typed flags 等引擎 API 差异。 3. 让 `AngelscriptFunctionTableCodeGenerator.cs`、`AngelscriptFunctionTableExporter.cs`、`AngelscriptHeaderSignatureResolver.cs`、`AngelscriptFunctionSignatureBuilder.cs` 改为只依赖本地 compatibility model，不再直接出现 `UhtModule` / `UhtClass` / `UhtFunction` / `UhtProperty`。 4. 在 compat 层补一个最小 API-surface smoke test，至少锁住当前工具实际依赖的成员：`Module.ShortName`、`Module.ModuleType`、`HeaderFile.FilePath`、`FunctionFlags`、`FunctionExportFlags`、`ParameterProperties`、`ReturnProperty`、`AppendFullDecl()`、`TypeTokens`、`GetModuleShortestIncludePath(...)`。 5. 将今后所有 UE5.x 适配改为“先改 compat reader，再跑工具层回归”，禁止直接在四个业务文件里各自追着引擎 API 改。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/UhtCompat/UhtCompatReader.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/UhtCompat/CompatModel.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | 首次引入 compat 层会触碰四个核心文件，短期改动面较大；如果 facade 设计得过薄，后续仍会把 `Uht*` 细节泄露回业务层。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新编译 `AngelscriptUHTTool`，确认四个业务文件不再直接引用 `EpicGames.UHT.Types/Utils/Tables`。 2. 运行新增 compat smoke test，确认当前引擎版本的关键 API surface 完整可读。 3. 重新运行 UHT 导出，确认产物只在兼容层重构预期范围内变化。 |

### Issue-119：`.csproj` 直接钉死引擎 `Binaries\\DotNET\\UnrealBuildTool\\*.dll` 布局，却没有任何 UHTTool 级引用/API smoke 校验

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj`, `Tools/Shared/UnrealCommandUtils.ps1` |
| 行号 | `AngelscriptUHTTool.ubtplugin.csproj:2, 41-51`; `UnrealCommandUtils.ps1:872-876` |
| 问题 | `AngelscriptUHTTool.ubtplugin.csproj` 直接导入 `$(EngineDir)\\Source\\Programs\\Shared\\UnrealEngine.csproj.props`，并把 `EpicGames.Build.dll`、`EpicGames.Core.dll`、`EpicGames.UHT.dll`、`UnrealBuildTool.dll` 的 `HintPath` 全部硬编码到 `$(EngineDir)\\Binaries\\DotNET\\UnrealBuildTool\\...`。与此同时，现有通用工具链只在 `UnrealCommandUtils.ps1:874-876` 检查 `UnrealBuildTool.dll` 文件存在，并没有任何 UHTTool 级 smoke 去验证 `EpicGames.UHT.dll` 是否存在、也没有检查当前源码实际依赖的类型/成员是否仍可解析。 |
| 根因 | 构建层只假设“程序集位于某个固定目录”即可，没有为 `AngelscriptUHTTool` 建立独立的引用校验与 API-surface 预检步骤。 |
| 影响 | 当 UE5.x 调整 DotNET 输出目录、拆分程序集、或仅仅改掉 `EpicGames.UHT` 中某个被当前源码使用的类型成员时，失败会直接在 MSBuild/C# 编译阶段以一串低层错误暴露出来，而不是提前给出“引用路径失效”或“API surface 漂移”的可执行诊断；这会显著增加升级排障成本。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `AngelscriptUHTTool` 增加独立的引用与 API-surface smoke 校验，把“路径失效”和“API 漂移”前移成可读诊断。 |
| 具体步骤 | 1. 新增 `Tools/Diagnostics/VerifyUhtToolReferences.ps1`，读取 `AgentConfig.ini` / `EngineRoot` 后显式检查 `EpicGames.Build.dll`、`EpicGames.Core.dll`、`EpicGames.UHT.dll`、`UnrealBuildTool.dll` 是否存在。 2. 在该脚本中通过最小反射检查当前源码依赖的关键类型和成员，例如 `EpicGames.UHT.Types.UhtModule`、`UhtFunction`、`UhtProperty.AppendFullDecl(...)`、`UhtProperty.TypeTokens`、`IUhtExportFactory.GetModuleShortestIncludePath(...)`；缺任一成员时直接输出清晰错误并中止。 3. 将该 smoke 脚本接入 `Tools/RunBuild.ps1` 或专用的 UHTTool 构建入口，在真正执行 `dotnet build` 前先跑一次，避免把低层编译报错当成第一现场。 4. 在 `AngelscriptUHTTool.ubtplugin.csproj` 或相邻 `Directory.Build.props` 中补充注释/属性，把这些程序集依赖集中声明为“受支持的 engine binary surface”，避免未来继续散落隐式假设。 5. 在 `Documents/Guides/Build.md` 增加一条 UHTTool 适配检查说明，明确 UE 升级后先跑 `VerifyUhtToolReferences.ps1`，再做真正编译。 |
| 涉及文件 | `Tools/Diagnostics/VerifyUhtToolReferences.ps1`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj`, `Tools/RunBuild.ps1`, `Documents/Guides/Build.md` |
| 预估工作量 | S |
| 风险 | 反射 smoke 只覆盖被显式列出的 API surface；如果检查集维护不全，仍可能遗漏某些较晚才触发的编译问题，因此需要把检查项与 compat 层的实际依赖列表绑定。 |
| 前置依赖 | 建议与 Issue-118 联动，把 smoke 检查项收敛到 compat facade 的公开依赖面，而不是继续按业务文件分散维护。 |
| 验证方式 | 1. 人为改错一个 `HintPath` 或临时重命名 `EpicGames.UHT.dll`，确认 smoke 脚本会在 `dotnet build` 前给出明确失败。 2. 在脚本里模拟缺失关键成员，确认输出能精确指出是哪一个类型/方法漂移。 3. 将脚本接入构建入口后，重新跑一次 UHTTool 构建，确认正常路径不引入额外噪声。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-117 | Defect | 优先修复，先消除“多 trailing 参数 overload 被前缀截断”的直接覆盖率损失 |
| P1 | Issue-118 | Architecture | 第二步处理，先把 `EpicGames.UHT` 依赖收敛到兼容层，为后续 UE5.x 适配降爆炸半径 |
| P2 | Issue-119 | Architecture | 在兼容层方案确定后补齐构建前 smoke 校验，把程序集/API 漂移前移成可执行诊断 |

---

## 发现与方案 (2026-04-09 01:39)

### Issue-120：resolver 把非 `UFUNCTION` 的同名 helper overload 一并纳入候选，合法 Blueprint 声明会被误判成 `overloaded-unresolved`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/BehaviorTree/BlackboardComponent.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/BehaviorTree/BehaviorTreeComponent.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:35-42, 75-105`; `BlackboardComponent.h:168-179`; `BehaviorTreeComponent.h:275-280`; `AS_FunctionTable_SkippedEntries.csv:35-37`; `AS_FunctionTable_Entries.csv:4183,4185-4186` |
| 问题 | `TryBuild()` 当前把 `FindCandidates()` 返回的全部 `publicCandidates` 都送进 overload 精确匹配，但 `FindCandidates()` 只是按同名文本抓取候选，不要求候选附近存在 `UFUNCTION`，也不把 UHT 已经识别出的 reflected declaration 作为筛选条件。结果是 `UBlackboardComponent::GetLocationFromEntry`、`ClearValue` 与 `UBehaviorTreeComponent::SetDynamicSubtree` 这类“一个 BlueprintCallable 声明 + 一个 plain C++ helper overload”的组合，被一起当成 overload 候选；当前产物已经把它们统一记成 `overloaded-unresolved`，并在 entry 表中降级成 `Stub,ERASE_NO_FUNCTION()`。 |
| 根因 | header resolver 的候选集合定义停留在“class body 里出现了同名 token”，没有建立“UHT reflected function -> 对应源码 declaration”这一层绑定，因此未反射 helper overload 也会污染 direct-bind 判定。 |
| 影响 | 这会让一批本来已经被 UHT 明确标成 BlueprintCallable 的函数仍然无法进入 direct path，直接降低函数表正确性；同时 `SkippedEntries.csv` 会把根因伪装成泛化的 overload 失败，而不是明确指出“候选集中混入了未反射声明”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让候选发现优先锚定 reflected declaration，只允许与目标 `UFUNCTION` 相邻或同一声明块内的候选进入匹配，未反射 helper overload 不再参与 direct-bind 选择。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 为 `CandidateDeclaration` 增加 `HasAdjacentUFunction`、`DeclarationStartIndex` 等字段；`FindCandidates()` 在回溯 declaration 起点时，同时检查候选前一个非空白声明块是否包含 `UFUNCTION(...)`。 2. 在 `TryBuild()` 中把候选分成 `reflectedCandidates` 和 `nonReflectedCandidates` 两组；优先只对 `reflectedCandidates` 做 `HasLinkableExport()` 与签名比对，只有在一个 reflected 候选都找不到时，才允许退回现有宽松文本候选逻辑并输出新 reason，例如 `reflected-declaration-missing`。 3. 对 `UFUNCTION` 后紧跟多个同名 reflected overload 的场景，继续保留现有参数/返回值精确匹配；但若额外同名声明没有 `UFUNCTION`，应直接从候选集中排除，而不是继续折叠成 `overloaded-unresolved`。 4. 在 `AngelscriptFunctionTableExporter.cs` 中增加更细粒度 failure code，把“未反射 helper overload 被过滤”与“真实 reflected overload 歧义”分开统计，避免后续继续混入同一 reason。 5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 新增回归，至少锁住 `UBlackboardComponent::GetLocationFromEntry`、`ClearValue` 与 `UBehaviorTreeComponent::SetDynamicSubtree` 三个样本，要求它们从 `SkippedEntries.csv` 消失并恢复 direct entry。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 某些 UE 头文件可能先写辅助宏、再写 `UFUNCTION`，如果“邻近 `UFUNCTION`”判定做得过窄，可能把真实 reflected 声明误过滤；因此需要保留受控 fallback，并用 engine/plugin 双样本锁住边界。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_SkippedEntries.csv` 不再包含 `UBehaviorTreeComponent,SetDynamicSubtree,overloaded-unresolved`、`UBlackboardComponent,ClearValue,overloaded-unresolved`、`UBlackboardComponent,GetLocationFromEntry,overloaded-unresolved`。 3. 检查 `AS_FunctionTable_Entries.csv` 中这三条从 `Stub,ERASE_NO_FUNCTION()` 变为 direct entry。 4. 运行新增自动化，确认真正的 reflected overload 歧义仍会被拒绝，而 plain helper overload 不再污染候选集。 |

### Issue-121：interface 绑定模型只认识 `U...` wrapper，带原生实现的 `I...` Blueprint API 被整体放弃 direct bind

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/NavMovementInterface.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Private/NavMovementInterface.cpp`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Camera/CameraLensEffectInterface.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptFunctionSignatureBuilder.cs:90-97`; `AngelscriptFunctionTableCodeGenerator.cs:14-22, 465-479`; `NavMovementInterface.h:127-134`; `NavMovementInterface.cpp:9-25`; `CameraLensEffectInterface.h:21-31`; `AS_FunctionTable_Entries.csv:883-884, 2963-2964` |
| 问题 | `AngelscriptFunctionSignatureBuilder` 在构造 signature 时始终把 `OwningType` 设成 `classObj.SourceName`，而生成注册行也固定用 `{ClassName}::StaticClass()`。对 interface 而言，这个 `SourceName` 是 `UNavMovementInterface`、`UCameraLensEffectInterface` 这类反射 wrapper，但真正带实现的 native 方法在 `INavMovementInterface`、`ICameraLensEffectInterface` 上。由于当前模型无法表达“反射拥有者是 `U...`，native 方法声明者是 `I...`”，生成器只能在 `UhtClassType.Interface/NativeInterface` 分支里一律写 `ERASE_NO_FUNCTION()`；即使 `INavMovementInterface::StopActiveMovement`、`StopMovementKeepPathing` 已有 `UE_API` 声明和 `.cpp` 实现，当前 entry 表仍全部落成 stub。 |
| 根因 | UHTTool 的 entry/signature 数据结构把“注册到哪个 `UClass`”和“method pointer 属于哪个 native type”绑成了同一个 `ClassName/OwningType` 字段，没有给 UE interface 的双类型模型预留独立维度。 |
| 影响 | 这会把 interface 适配边界硬编码成“永不直绑”。凡是可以安全拿到 `I...` method pointer 的 BlueprintCallable interface API，都会被系统性降级到 stub/reflective 路径，直接损伤 direct coverage，也让 UE5.x interface 适配长期停留在 blanket fallback。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 interface entry 拆成“注册宿主 `UClass`”与“native owning type `I...`”两个独立字段，为可实现的 interface 方法恢复 direct bind。 |
| 具体步骤 | 1. 扩展 `AngelscriptFunctionSignature` 与 `AngelscriptGeneratedFunctionEntry`，新增 `RegistrationClassName` 与 `NativeOwningType`；普通类两者相同，interface 则分别为 `UNavMovementInterface` / `INavMovementInterface`。 2. 在 `AngelscriptFunctionSignatureBuilder.cs` 中新增 `ResolveNativeOwningType(UhtClass classObj)`：当 `classObj.ClassType` 是 `Interface` 或 `NativeInterface` 时，从 `U...` 名推导对应 `I...`，并在必要时通过 header/AST 校验该 native interface 真实存在。 3. 调整 `BuildEraseMacro()` 与 `BuildRegistrationLine()`：method pointer 生成基于 `NativeOwningType`，而 `AddFunctionEntry(...)` 继续注册到 `RegistrationClassName::StaticClass()`。 4. 删除 `CollectEntries()` 里对 interface 的 blanket `ERASE_NO_FUNCTION()` 分支，改成“能解析 `I...` 实现则 direct，不能解析才 stub，并输出明确 reason `interface-native-type-missing` / `interface-unimplemented`”。 5. 在 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp` 的 interface 拒绝逻辑旁补一条注释/断言，明确 interface direct bind 是生成层职责，避免未来继续把“interface 一律 fallback”当成既定前提。 6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 新增回归，至少覆盖 `UNavMovementInterface::{StopActiveMovement, StopMovementKeepPathing}` 与 `UCameraLensEffectInterface::{GetParticleComponents, GetPrimaryParticleComponent}`，要求它们不再统一落成 stub。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | 并非所有 interface `UFUNCTION` 都有可用的 `I...` 默认实现；如果实现层没有区分“默认实现可直绑”和“纯虚/必须反射分发”，可能把无实现接口误生成为 direct pointer。 |
| 前置依赖 | 建议与 Issue-120 一起推进，先把 interface 候选解析与 failure code 细化，再落 interface direct-bind 恢复，避免继续把 interface 问题混入泛化 stub。 |
| 验证方式 | 1. 重新运行 UHT 导出。 2. 检查 `AS_FunctionTable_Entries.csv` 中 `UNavMovementInterface,StopActiveMovement`、`StopMovementKeepPathing` 以及 `UCameraLensEffectInterface,GetParticleComponents`、`GetPrimaryParticleComponent` 不再是 `Stub,ERASE_NO_FUNCTION()`。 3. 检查对应 shard `.cpp` 已生成基于 `INavMovementInterface` / `ICameraLensEffectInterface` 的 direct erase macro。 4. 运行新增自动化，确认有默认实现的 interface 函数恢复 direct，而纯虚无默认实现的样本仍被安全拒绝。 |

### Issue-122：`SkippedEntries.csv` 不是完整的 stub 原因集，当前至少有 `160` 条 `ERASE_NO_FUNCTION()` 条目完全没有诊断原因

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Animation/AnimData/IAnimationDataController.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv` |
| 行号 | `AngelscriptFunctionTableExporter.cs:65-97`; `AngelscriptFunctionTableCodeGenerator.cs:465-479`; `BlueprintCallableReflectiveFallback.cpp:261-282`; `IAnimationDataController.h:43-120`; `AS_FunctionTable_Entries.csv:573-582`; `AS_FunctionTable_SkippedEntries.csv:1-3886` |
| 问题 | 当前 exporter 只有在 `AngelscriptFunctionSignatureBuilder.TryBuild(...)` 返回失败时才向 `SkippedEntries.csv` 追加一行，但生成器实际还会在 `CollectEntries()` 中主动把一批函数直接写成 `ERASE_NO_FUNCTION()`。这导致 “最终是 stub” 与 “CSV 里有失败原因” 不是一一对应关系。我对当前产物做了实际比对：`AS_FunctionTable_Entries.csv` 里共有 `2649` 条 `Stub`，其中至少 `160` 条在 `AS_FunctionTable_SkippedEntries.csv` 没有任何同 `ModuleName/ClassName/FunctionName` 记录；按模块分布，`Engine` 就有 `101` 条，`AssetRegistry` 有 `28` 条，`EnhancedInput` 有 `26` 条。`UAnimationDataController` 是可直接复现的样本，其 `AddAttribute`、`AddBoneCurve`、`GetModelInterface` 已写成 `Stub,ERASE_NO_FUNCTION()`，但 skipped CSV 对 `UAnimationDataController` / `UAnimationDataModel` 的匹配结果为 `0` 行。 |
| 根因 | 生成链路和诊断链路维护了两套状态机：生成链路有 interface 强制 stub 等主动降级分支，诊断链路却只观察“签名构建是否失败”，没有记录“生成阶段为什么决定写 stub”。 |
| 影响 | 当前 `SkippedEntries.csv` 不能解释全部失败面。开发者在追查 `Summary.json` / `Entries.csv` 的 stub 覆盖率时，会遇到一批“确认失败但无原因”的条目，被迫手工回读源码和 runtime fallback 规则；这会明显拖慢缺陷定位，也让自动化无法基于 reason 做优先级收敛。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 统一生成链路与诊断链路的失败状态，把所有 `ERASE_NO_FUNCTION()` 的来源都落成可枚举的 stub reason，而不是只记录 builder fail。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.cs` 为 `AngelscriptGeneratedFunctionEntry` 增加 `StubReason` 或 `ResolutionFailure` 字段；凡是写出 `ERASE_NO_FUNCTION()` 的分支都必须同步填原因，例如 `interface-forced-stub`、`signature-build-failed`、`policy-excluded`、`runtime-reflective-fallback-impossible`。 2. 调整 `CollectEntries()`：不要再把 `TryBuild(...)` 的 `failureReason` 丢弃成 `out string? _`；要把失败原因带回 entry 模型，同时把 interface blanket stub 也编码成明确 reason。 3. 让 `AngelscriptFunctionTableExporter.cs` 改为消费生成阶段产出的统一结果，而不是再独立跑一遍 `CountBlueprintCallableFunctions()`；`SkippedEntries.csv` 应该只枚举 `EntryKind=Stub` 且带稳定 reason 的条目，做到和 `Entries.csv` 一一可追溯。 4. 在 sidecar schema 中新增 `StubReason` 列，必要时再保留旧 `FailureReason` 作为兼容列；`SkippedReasonSummary.csv` 的聚合键改为新的统一 reason。 5. 为 `BlueprintCallableReflectiveFallback.cpp` 里这种确定性拒绝类别建立映射表，例如 `RejectedInterfaceClass -> interface-forced-stub`，避免 runtime 规则和 UHT 侧 reason 再次分叉。 6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加闭环断言：每一条 `Entries.csv` 中的 `Stub` 都必须能在 `SkippedEntries.csv` 找到同键原因；并以 `UAnimationDataController` 为样本，锁住 interface stub 不再是“无原因失败”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦把所有 stub reason 显式化，现有依赖 `SkippedEntries.csv` 行数或 `FailureReason` 枚举集合的测试/脚本都需要同步迁移；如果映射粒度定义不稳，短期内 reason 数量会有一次明显重排。 |
| 前置依赖 | 建议与 Issue-121 联动，优先明确 interface stub 的最终建模，再把 `interface-forced-stub` 等 reason 固化进统一 schema。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `Entries.csv` 中每一条 `Stub` 都能在 `SkippedEntries.csv` 找到同键原因，不再出现“有 stub 但无 reason”的缺口。 2. 复查当前统计，`MissingReasonCount` 应从 `160` 降到 `0`。 3. 检查 `UAnimationDataController::{AddAttribute, AddBoneCurve, GetModelInterface}` 等样本已出现在 `SkippedEntries.csv`，并带稳定的 interface/stub reason。 4. 运行新增自动化，确认未来新增 stub 分支时如果没有填 reason，会直接测试失败。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-120 | Defect | 先修复，优先把未反射 helper overload 从候选集剥离，恢复被误杀的 direct bind |
| P1 | Issue-121 | Architecture | 第二步处理，为 UE interface 建立 `U...`/`I...` 双类型绑定模型，关闭“永不直绑”边界 |
| P2 | Issue-122 | Defect | 在前两项后落地，补齐所有 stub 的统一 reason 闭环，提升诊断可执行性 |

---

## 发现与方案 (2026-04-09 02:07)

### Issue-123：生成 entry 模型固定为“一源函数对应一个脚本表面”，无法承载 `ScriptMethod` / `ScriptOperator` / `BlueprintAutocast` 的一对多注册

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/TimeManagement/Public/TimeManagementBlueprintLibrary.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetMathLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:14-21, 37-44, 244-260`; `TimeManagementBlueprintLibrary.h:70-82`; `KismetMathLibrary.h:999-1015, 3586-3598`; `AS_FunctionTable_Entries.csv:1640,2507,4934` |
| 问题 | 当前 `AngelscriptGeneratedFunctionEntry` 只保存 `ClassName + FunctionName + EraseMacro`，`BuildRegistrationLine()` 也只会生成一条 `FAngelscriptBinds::AddFunctionEntry(..., "FunctionName", ...)`。`Entries.csv` 同样只有单个 `FunctionName` 列，没有任何 `BindingKind`、`ScriptFacingName` 或 surface id。可引擎头里已经存在明确的一对多 script surface 需求：`UTimeManagementBlueprintLibrary::Add_FrameNumberFrameNumber` 同时带 `ScriptMethod`、`ScriptMethodSelfReturn` 与 `ScriptOperator="+;+="`，`UKismetMathLibrary::Add_VectorVector` / `Multiply_VectorVector` 也是同类模式，`BlueprintAutocast` 函数又要求把原始 helper 暴露成 conversion surface。当前产物已经直接暴露了模型上限：`AS_FunctionTable_Entries.csv:1640` 仍只记 `Add_VectorVector`，`AS_FunctionTable_Entries.csv:2507` 只记 `Conv_InterfaceToObject`，`AS_FunctionTable_Entries.csv:4934` 只记 `GetInputEventFromKeyEvent`，没有任何能力表达“同一个 source function 需要多个 script-facing registration”。 |
| 根因 | UHTTool 目前把“源码中的 `UFUNCTION` 身份”和“脚本最终可见的绑定表面”当成同一个实体，生成层没有抽象出独立的 `surface`/`binding projection`。 |
| 影响 | 这会把后续 `ScriptMethod`、`ScriptOperator`、`BlueprintAutocast` 的修复全部逼成 emitter 特判。即使单个语义被局部补上，sidecar、runtime 注册接口和 diff 工具仍然会继续假设“一行就是一个函数”，最终很难稳定支持多 registration、也无法把 source identity 与 script identity 分开追踪。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把生成层从“函数记录”重构为“源函数 + 多个 script surface”，再让 `ScriptMethod` / `ScriptOperator` / `BlueprintAutocast` 作为 surface 生产者挂进去。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.cs` 中新增两层模型：`GeneratedSourceFunction` 保存 `SourceClassName`、`SourceFunctionName`、`EraseMacro`、原始 `UFunction` 元数据；`GeneratedBindingSurface` 保存 `SurfaceKind`、`RegistrationClassName`、`ScriptFacingName`、`DeclarationKey`、`OperatorTokens`、`BindingRoute`。 2. `CollectEntries()` 不再直接往 `entries` 塞最终行，而是先构建 source-level record；随后新增 `BuildBindingSurfaces(...)`，普通函数默认产出 1 个 `Method/Function` surface，`ScriptMethod`、`ScriptOperator`、`BlueprintAutocast` 等元数据在这里扩展成多个 surface。 3. `BuildRegistrationLine()` 改为消费 `GeneratedBindingSurface`，运行时注册接口也同步升级为接受 `SurfaceKind` / `ScriptFacingName` / `DeclarationKey`，避免继续把原始 `FunctionName` 当成唯一 key。 4. `Entries.csv` 与 `Summary.json` 增加稳定 `SurfaceId`、`SourceFunctionName`、`ScriptFacingName`、`SurfaceKind`、`DeclarationKey` 字段；禁止继续把多个 script-facing surface 压扁到一条 source row。 5. 将 `ScriptMethod`、`BlueprintAutocast`、`ScriptOperator` 三类问题后续都改成只产出 surface，不再各自直接改 shard 渲染逻辑，避免多处 emitter 分叉。 6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加一条 surface 级回归：至少覆盖一个 `ScriptMethod + ScriptOperator` 样本和一个 `BlueprintAutocast` 样本，断言同一 source function 可以生成多个 `SurfaceId`，且 sidecar 与 runtime 查询结果一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | 一旦 runtime 的注册 key 从单字符串升级为 surface-aware 结构，现有手写 bind、overload 查找和 sidecar 解析脚本都要同步迁移；如果过渡层设计不好，短期内会出现 “旧 API 能注册、新 API 才能查询” 的双轨状态。 |
| 前置依赖 | 建议先完成 Issue-76 的 overload 容器改造，避免“同名多签名”和“同源多 surface”两种扩展同时压在旧 `TMap<FString, FFuncEntry>` 上。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 sidecar 能同时列出 `SourceFunctionName` 与多个 `SurfaceId/ScriptFacingName`。 2. 对 `ScriptOperator="+;+="` 样本验证会生成至少两个 surface，而不是一条 raw helper 行。 3. 对 `BlueprintAutocast` 样本验证 `SurfaceKind=Autocast` 与普通 helper surface 可区分。 4. 运行新增自动化，确认 runtime 能按 `SurfaceId` 或等价声明键查询到所有 surface，而不是只保留第一条。 |

### Issue-124：当前输出契约只有函数级平面行，没有参数级 schema，参数语义只能继续列爆炸或完全不可见

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/AudioMixer/Public/AudioMixerBlueprintLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:37-44, 244-260`; `AngelscriptEngine.h:790-926`; `ScriptEditorPrompts.cpp:199-236, 283-320, 360-377`; `AudioMixerBlueprintLibrary.h:279-299`; `AS_FunctionTable_Entries.csv:1,3649-3652` |
| 问题 | 当前 `AngelscriptGeneratedFunctionCsvEntry` 只有 `ModuleName, EditorOnly, ClassName, FunctionName, EntryKind, EraseMacro, ShardIndex` 7 个字段，`WriteEntryCsv()` 也只写函数级平面行。可 runtime 侧早已把参数/属性建模成独立对象：`FAngelscriptPropertyDesc` 保存 `Meta`、`bAdvancedDisplay` 等字段，`FAngelscriptArgumentDesc` 保存 `ArgumentName`、`DefaultValue`、`Type`、in/out ref 方向。编辑器侧 `ScriptEditorPrompts` 也明确按参数遍历 `FProperty` 并读取默认值、隐藏参数。实际 engine 头文件同样已经在参数层携带复杂契约，例如 `StartRecordingOutput` / `StopRecordingOutput` / `PauseRecordingOutput` / `ResumeRecordingOutput` 都依赖 `AdvancedDisplay` 改写参数表面，但当前 `AS_FunctionTable_Entries.csv:3649-3652` 只能看出函数存在，完全看不出哪些参数被折叠、默认值是什么、是否有隐藏或角色化参数。 |
| 根因 | UHTTool 一开始只把 function table 当成“是否能注册 + 用哪个 erase macro”的导出任务，没有把参数面视为正式 schema 的一部分。 |
| 影响 | 这会让所有参数级 parity 问题都缺少统一落点。函数是否存在可以被 `Entries.csv` 观察到，但参数契约变化无法被 sidecar、测试或 diff 工具发现；后续每补一条参数语义，都只能继续往函数行上追加平行列，最终形成 schema 膨胀和验证盲区并存。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将函数级概要与参数级明细拆开，新增独立的 parameter manifest，把参数角色、默认值和展示语义集中收口。 |
| 具体步骤 | 1. 保留 `AS_FunctionTable_Entries.csv` 作为函数级概要，但新增 `AS_FunctionTable_Parameters.json` 或等价 JSON sidecar，以稳定 `EntryId/SurfaceId` 关联参数数组。 2. 在 UHTTool 内新增 `GeneratedBindingParameter` 模型，至少保存 `SourceIndex`、`ScriptIndex`、`UnrealName`、`ScriptName`、`TypeText`、`DefaultValue`、`Direction`、`bHidden`、`bAdvancedDisplay`、`SemanticRoles`。 3. `CollectEntries()` 后追加 `CollectParameterSurfaces(...)`：优先直接从 `UFunction` 参数 `FProperty` 和 metadata 抽取已有事实，不要把参数细节继续塞进 `Entries.csv` 的新列。 4. 将 world-context、dynamic output、auto-create-ref-term、default-to-self、callable-without-world-context 这类参数语义统一编码进 `SemanticRoles` 或等价结构，后续问题只扩展 JSON schema，不再重复改 CSV 表头。 5. `ScriptEditorPrompts` 与调试/诊断路径优先读取这一份参数 manifest 进行可观测验证，至少能对比“sidecar 记录的默认值/隐藏参数策略”与运行时 `UFunction` 实际参数面是否一致。 6. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 新增参数 schema 回归：至少覆盖 `StartRecordingOutput` / `StopRecordingOutput` 这种 `AdvancedDisplay` 样本，并补一条带默认值/隐藏参数的样本，断言参数 manifest 能稳定反映这些差异。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptEditor/Public/EditorMenuExtensions/ScriptEditorPrompts.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | 新增参数清单会增加 sidecar 体积，也会让现有只解析 CSV 的本地脚本需要升级到“CSV + JSON”双输入；如果 `EntryId` 设计不稳定，会把本来要消除的 diff 噪声重新带回来。 |
| 前置依赖 | 建议与 Issue-50 的 `schemaVersion/provenance` 一起设计，先把 `EntryId` 与版本边界固定下来，再发布参数 manifest。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认新增参数 manifest 与 `Entries.csv` 通过稳定 `EntryId` 对齐。 2. 对 `StartRecordingOutput` / `StopRecordingOutput` 样本检查 manifest，确认 `AdvancedDisplay` 参数索引与默认值信息可见。 3. 扩展自动化，确保参数默认值、隐藏参数和参数角色的变化会直接体现在 sidecar diff 中，而不是只能靠人工读头文件。 4. 对同一源码树连续导出两次，确认参数 manifest 除时间戳外保持稳定。 |

### Issue-125：`Keywords` / `CompactNodeTitle` / `DisplayName` 没有进入 generated binding 的工具链契约，调试与搜索只能看到裸 C++ helper 名

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/ (对该目录执行 rg 'Keywords|CompactNodeTitle|DisplayName|ScriptKeywords' 结果为 0 行)`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Camera/CameraLensEffectInterface.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/TimeManagement/Public/TimeManagementBlueprintLibrary.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/AudioMixer/Public/AudioMixerBlueprintLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptDebugServer.cpp:33-38, 1599-1608`; `CameraLensEffectInterface.h:97-105`; `TimeManagementBlueprintLibrary.h:70-82`; `AudioMixerBlueprintLibrary.h:279-287`; `AS_FunctionTable_Entries.csv:885,1640,3649,4934` |
| 问题 | UHTTool 当前完全不读取 `Keywords`、`CompactNodeTitle`、`DisplayName` 这类 Blueprint discoverability metadata。runtime 调试输出也只认 `ScriptKeywords`：`AngelscriptDebugServer.cpp` 在 `1600-1602` 只读取 `UnrealFunction->GetMetaData(NAME_ScriptKeywords)`，并不会回退到原生 `Keywords`。结果是大量 generated binding 在工具链里只能以 raw helper 名出现。已验证样本包括：`GetInterfaceClass` 明明带 `CompactNodeTitle="."` 与 `Keywords="class, get, toclass, getclass, spawn, object"`，但当前 entries 仍只有 `GetInterfaceClass`；`Add_FrameNumberFrameNumber` / `Add_VectorVector` 这类函数明明带 `DisplayName`、`CompactNodeTitle` 与 `Keywords="+ add plus"`，当前 sidecar 仍只保留 `Add_VectorVector`；`StopRecordingOutput` 明明在 header 中声明 `DisplayName = "Finish Recording Output"`，当前 entries 仍只写 `StartRecordingOutput` / `StopRecordingOutput` 这类 raw 名字。 |
| 根因 | 生成链路把这组 metadata 当成 Blueprint 节点 UI 信息，而不是脚本调试、搜索和 sidecar 的正式 discoverability 契约；runtime 调试层又只消费自定义 `ScriptKeywords`，没有为 native metadata 提供 bridge。 |
| 影响 | 即使函数最终可调用，开发者在 debug server、搜索面板或 sidecar 里仍然只能按 C++ helper 名检索。对 operator、autocast、script-method 这类已经需要“从 source name 脱钩”的函数来说，这会明显放大可发现性噪声，也让“当前脚本真正看到的是什么”更难排查。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 建立统一的 discoverability metadata 层，把 native `Keywords` / `CompactNodeTitle` / `DisplayName` 归一成脚本工具链可消费的 canonical 字段。 |
| 具体步骤 | 1. 在 UHTTool 侧新增 `GeneratedDiscoverabilityMetadata`，至少包含 `DisplayLabel`、`CompactLabel`、`SearchKeywords`、`SourceDisplayName`；读取顺序优先使用 `DisplayName` / `CompactNodeTitle` / `Keywords`，若不存在再回退到 `ScriptName` 或 raw `FunctionName`。 2. 在 runtime 绑定后处理阶段新增 bridge：对 native/generated binding，把 `Keywords` 归一到 canonical metadata，必要时补写 `ScriptKeywords` 或让 debug server 直接优先读 canonical 字段、其次回退 `Keywords`，不要继续只认 `ScriptKeywords`。 3. 扩展 debug server 输出 schema，新增 `displayName`、`compactTitle`、`keywords` 字段；`NAMES_InformedMeta` 不应再只剩 delegate 辅助标签。 4. sidecar 层为每个 `EntryId/SurfaceId` 输出 discoverability 字段，至少让 `Entries.csv` 或配套 JSON 能显示 `Finish Recording Output`、`+ add plus`、`.` 这类信息，而不是只剩 raw helper 名。 5. 对 script-defined 函数与 native generated binding 统一 discoverability 源，避免一条路径写 `ScriptKeywords`、另一条路径只保留 UE 原生 metadata。 6. 在 `Plugins/Angelscript/Source/AngelscriptTest/` 增加工具链回归：至少覆盖 `GetInterfaceClass`、`Add_VectorVector` 或同类 `Keywords="+ add plus"` 样本，以及 `StopRecordingOutput` 的 `DisplayName` 样本，断言 debug server / sidecar 都能看到 canonical discoverability 字段。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | `DisplayName` 与 `CompactNodeTitle` 是展示字段而不是稳定标识符；如果把它们误当注册 key，会制造新的重名/本地化问题。因此它们只能作为 discoverability metadata 输出，不能替代 `ScriptFacingName`。 |
| 前置依赖 | 建议复用 Issue-123 的 `SurfaceId/ScriptFacingName` 设计，把 discoverability metadata 绑定到 script surface，而不是继续绑在 source function 上。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `GetInterfaceClass`、`Add_VectorVector`、`GetInputEventFromKeyEvent`、`StopRecordingOutput` 的 sidecar 中存在 `displayName/compactTitle/keywords` 或等价字段。 2. 调试器/metadata 导出回归中验证 `keywords` 不再只在 `ScriptKeywords` 样本里出现，native `Keywords` 也能被看到。 3. 对 `DisplayName` 样本确认展示字段变为 `Finish Recording Output`，但实际注册 key 仍保持稳定脚本名，不会被 UI 字段替换。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-123 | Architecture | 先做基础模型改造，把“一源函数对应多个 script surface”的能力建起来，再承接后续 `ScriptMethod` / `Operator` / `Autocast` |
| P1 | Issue-124 | Architecture | 第二步补参数级 schema，避免后续参数语义继续以列爆炸方式分散落地 |
| P2 | Issue-125 | Defect | 在前两项落地后补 discoverability metadata，统一 debug/server/sidecar 的展示与搜索契约 |

---

## 发现与方案 (2026-04-09 02:29)

### Issue-126：`MustImplement` 参数约束未进入 generated binding 契约

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerController.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Camera/PlayerCameraManager.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Camera/CameraLensEffectInterface.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:16-31,33-55,178-249,321-332`; `AngelscriptBindDatabase.h:56-87`; `Bind_BlueprintType.cpp:1557-1562,1591-1595,1599-1611,1710-1725`; `PlayerController.h:1218-1219`; `PlayerCameraManager.h:769,776-777`; `CameraLensEffectInterface.h:115-116`; `AS_FunctionTable_Entries.csv:286,316,888` |
| 问题 | 当前 generated binding 只保存参数类型、参数名、默认值、`WorldContext` 与 `DeterminesOutputType`，没有任何参数级接口约束字段。`FAngelscriptMethodBind` 持久化结构也只序列化 `Declaration`、`ClassName`、`ScriptName`、`WorldContextArgument`、`DeterminesOutputTypeArgument` 等函数级信息。与此同时，`TSubclassOf` 绑定实现只记录并比较 `MetaClass`，`CreateProperty()` 只 `SetMetaClass(SubClass)`，`MatchesProperty()` 也只检查 `ClassProp->MetaClass == AssociatedClass`。但引擎头里已经存在真实的 `UPARAM(meta=(MustImplement=\"CameraLensEffectInterface\")) TSubclassOf<AActor>` 契约，样本包括 `APlayerController::ClientSpawnGenericCameraLensEffect`、`APlayerCameraManager::{FindGenericCameraLensEffect, AddGenericCameraLensEffect}` 与 `UCameraLensEffectInterfaceClassSupportLibrary::SetInterfaceClass`。当前产物仍把这些函数正常导出为 `Direct` 或 `Stub` 行，却没有任何 sidecar/runtime 元数据表达 `MustImplement` 约束。 |
| 根因 | UHTTool 与 runtime signature/bind-db 模型把参数信息压缩成“类型文本 + 默认值 + 少量固定槽位元数据”，没有建立参数级约束 schema；后续 `TSubclassOf` 绑定层又把类型约束收缩成 `MetaClass` 单一维度，导致 `MustImplement` 这类 UE 参数契约在生成链路中被整体丢失。 |
| 影响 | 脚本层会把这些 API 误看成“任意 `TSubclassOf<AActor>` 都合法”，从而允许传入未实现 `CameraLensEffectInterface` 的类，破坏与 Blueprint 节点一致的参数合法性约束。更严重的是，这个缺口既不体现在 `Entries.csv`，也不体现在 bind-db 或运行时类型匹配逻辑里，后续即使补 editor/debug 工具也无法知道哪一个参数应做接口校验。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 generated binding 补齐参数级约束 schema，把 `MustImplement` 从 header metadata 采集到 sidecar、bind-db 和 runtime 类型校验链路。 |
| 具体步骤 | 1. 在 `Helper_FunctionSignature.h` 为参数增加独立约束模型，例如 `FAngelscriptArgumentConstraint`，至少包含 `MustImplementInterfacePath`、`ConstraintKind`、`ArgumentName`、`ArgumentIndex`。 2. 在 `InitFromFunction()` 遍历 `FProperty` 时读取参数 metadata，显式采集 `MustImplement`，并把接口名解析为稳定对象路径或脚本可比较标识；不要继续只保存默认值。 3. 扩展 `FAngelscriptMethodBind` 与对应序列化逻辑，把参数约束持久化进 bind-db，避免 editor 构建和运行时重建时再次丢失。 4. 为 `AS_FunctionTable_Entries.csv` 配套的参数清单或 JSON sidecar 增加约束字段，至少让 `ClientSpawnGenericCameraLensEffect`、`AddGenericCameraLensEffect`、`SetInterfaceClass` 的目标参数能被外部工具观测到 `MustImplement=CameraLensEffectInterface`。 5. 在 `Bind_BlueprintType.cpp` 的 `TSubclassOf` 绑定路径补接口约束参与的匹配与校验逻辑：创建 `FClassProperty` 时保留 `MetaClass` 之外的接口约束信息；参数传递和 `MatchesProperty()` 比较时同时验证“输入 class 是否实现指定 interface”，不能再只看 `MetaClass`。 6. 如果 reflective fallback 仍会消费这些函数，需同步让 fallback 参数检查也读取同一份约束数据，避免 direct/fallback 两条路径出现不同的参数合法性。 7. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加回归，至少锁住上述三个 camera-lens 样本，验证 sidecar 出现参数约束字段；再补一个运行时负例，确认未实现 `CameraLensEffectInterface` 的 `AActor` 子类不会通过绑定层校验。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBindDatabase.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintType.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/BlueprintCallableReflectiveFallback.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | 参数约束一旦进入 bind-db 和 sidecar，就需要定义稳定的序列化格式；如果只补 `MustImplement` 而不先抽象成通用参数约束模型，后续 `ObjectMustImplement`、`AllowedClasses`、`DisallowedClasses` 等元数据仍会重复走一次结构升级。 |
| 前置依赖 | 建议复用 Issue-124 的参数级 schema 方向；若 Issue-124 暂未落地，则至少先为 bind-db/runtime 增加最小可用的参数约束数组，避免继续把 `MustImplement` 塞进函数级平面字段。 |
| 验证方式 | 1. 重新运行 UHT 导出，检查 camera-lens 样本的参数 sidecar 中出现 `MustImplement=CameraLensEffectInterface`。 2. 检查 bind-db 序列化后重载仍能恢复该约束，不会在 editor 重启后消失。 3. 新增自动化分别验证 direct 和 fallback 路径：传入未实现接口的 class 时必须拒绝，传入合法 class 时行为保持不变。 4. 复查 `Bind_BlueprintType.cpp` 生成的 `TSubclassOf` C++ form 和属性匹配逻辑，确认不再只依赖 `MetaClass` 单一条件。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-126 | Defect | 优先修复，先把 `MustImplement` 这类参数约束纳入 generated binding 契约，避免继续输出错误可调用面 |

---

## 发现与方案 (2026-04-09 02:30)

### Issue-127：`NativeMakeFunc` / `NativeBreakFunc` 未进入 generated binding 的节点类型契约

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/GameplayStatics.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetMathLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `Helper_FunctionSignature.h:16-31,33-55,321-332,414-458`; `Bind_BlueprintCallable.cpp:100-139`; `AngelscriptFunctionTableCodeGenerator.cs:37-44,125-135,244-265`; `GameplayStatics.h:1078-1102`; `KismetSystemLibrary.h:345-366`; `KismetMathLibrary.h:3038-3043,3931-3936`; `AS_FunctionTable_Entries.csv:1306,1378,1662,1672,1989,2488-2489,2689-2690` |
| 问题 | 当前 generated binding 只把函数输出为普通 `ClassName + FunctionName + EntryKind + EraseMacro` 行，并在 runtime 侧按普通全局函数或成员函数注册。`FAngelscriptFunctionSignature` 没有任何 `NativeMakeFunc` / `NativeBreakFunc` 相关字段，`ModifyScriptFunction()` 也只处理 `WorldContext`、`DeterminesOutputType`、deprecated、editor-only 等固定语义。引擎头里却已经存在大量明确的 make/break 节点契约，例如 `UGameplayStatics::{BreakHitResult, MakeHitResult}`、`UKismetSystemLibrary::{MakeSoftObjectPath, BreakSoftObjectPath, MakeTopLevelAssetPath, BreakTopLevelAssetPath}`、`UKismetMathLibrary::{MakeColor, MakeTransform, BreakTransform}`。当前产物只是把这些函数压平成普通 `Direct` 或 `Stub` 行，例如 `BreakHitResult`、`MakeHitResult` 被标成 `Direct`，`MakeTransform`、`BreakTransform`、`MakeSoftObjectPath`、`BreakSoftObjectPath` 等被标成 `Stub`，但 sidecar/runtime 都没有任何字段表达它们是“构造节点”还是“拆解节点”。 |
| 根因 | UHTTool 和 runtime 绑定层都把 Blueprint 节点视作普通 callable surface，缺少独立的节点类型建模；因此 make/break 语义既没有进入生成模型，也没有进入注册接口和 sidecar schema。 |
| 影响 | 现有产物会把 Blueprint 的结构构造/拆解语义压扁成普通 helper 函数，导致工具链无法区分“普通 `void BreakX(...)`”与“值对象分解节点”，也无法区分“普通静态工厂”与“make node”。这会直接削弱调试、检索、后续 sugar 生成和版本比较的准确性，并让未来修复只能继续在 emitter 或 runtime 某个分支里追加特判。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 generated binding 中引入显式 `BindingKind/SurfaceKind`，把 `NativeMakeFunc` 与 `NativeBreakFunc` 作为正式节点类型建模，而不是继续压平为普通函数行。 |
| 具体步骤 | 1. 在 UHTTool 生成模型中新增 `BindingKind` 或等价字段，至少区分 `Regular`、`NativeMake`、`NativeBreak` 三类；采集来源直接读取 `UFunction` metadata，而不是靠函数名猜测。 2. 扩展 `AngelscriptFunctionTableCodeGenerator.cs` 的 entry/CSV/JSON schema，至少为现有 `Entries.csv` 增加稳定的 `BindingKind` 列，或在配套 sidecar 中输出该字段，禁止继续把 make/break 节点与普通 callable 完全混在同一平面记录里。 3. 在 `Helper_FunctionSignature.h` 为 runtime 签名模型补节点类型字段，并在 `ModifyScriptFunction()` 或等价注册后处理阶段把该语义写入脚本函数 traits / metadata，确保调试器和后续工具可读取。 4. 调整 `Bind_BlueprintCallable.cpp` 或其调用者，让注册接口能感知 `BindingKind`；短期内即使调用语义仍复用普通函数调用，也必须把“这是 make node / break node”的事实带进运行时契约，而不是在注册时丢失。 5. 在第二阶段基于该字段实现更贴近 Blueprint 的 sugar：`NativeMakeFunc` 生成 value-type/factory surface，`NativeBreakFunc` 生成 decompose 或 `TryBreak` 风格 surface，但这一步应建立在第一阶段已有稳定 `BindingKind` 之上。 6. 为 `AS_FunctionTable_Entries.csv` 中的 `BreakHitResult`、`MakeHitResult`、`MakeTransform`、`BreakTransform`、`MakeSoftObjectPath`、`BreakSoftObjectPath` 增加回归测试，锁住它们至少具备正确的 `BindingKind`，而不是继续作为无类型普通函数存在。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | 该问题与 Issue-123 的多 surface 模型存在耦合；如果没有先定义稳定的 `BindingKind`/`SurfaceId` 边界，就直接在 emitter 里补 make/break 特判，短期可能可用，但长期会把普通函数、operator、autocast、make/break 四套语义继续散落在不同分支。 |
| 前置依赖 | 建议与 Issue-123 联动，由同一套 `surface` 模型承载 `BindingKind`；若暂不推进 Issue-123，也必须先补最小 `BindingKind` 字段，避免 make/break 继续完全不可观测。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认上述样本在 sidecar 中出现稳定的 `BindingKind=NativeMake/NativeBreak`。 2. 检查 runtime 注册后可通过调试或 metadata 导出区分 make/break 节点与普通 callable。 3. 对 `BreakHitResult` / `MakeHitResult` 保持 direct path，对 `MakeTransform` / `BreakTransform` 等当前 stub 样本至少要求节点类型信息仍可见，不因是否 direct 而丢失。 4. 新增自动化确保未来新增 `NativeMakeFunc` / `NativeBreakFunc` 样本时，不会再无声退化为普通函数记录。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-127 | Architecture | 在 `surface`/schema 改造起步阶段同步处理，先把 make/break 节点类型变成正式契约，再决定脚本层 sugar |

---

## 发现与方案 (2026-04-09 02:32)

### Issue-128：`ExpandEnumAsExecs` / `ExpandBoolAsExecs` 分支契约未进入 generated binding schema

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `J:/UnrealEngine/UERelease/Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h`, `J:/UnrealEngine/UERelease/Engine/Source/Editor/BlueprintGraph/Private/K2Node_CallFunction.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Camera/CameraLensEffectInterface.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/DataTableFunctionLibrary.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `ObjectMacros.h:1653-1658`; `K2Node_CallFunction.cpp:495-508`; `Helper_FunctionSignature.h:16-31,33-55,178-249,414-458`; `Bind_BlueprintCallable.cpp:100-139`; `AngelscriptFunctionTableCodeGenerator.cs:37-44,125-135,244-265`; `CameraLensEffectInterface.h:101-116`; `DataTableFunctionLibrary.h:35-36`; `AS_FunctionTable_Entries.csv:886-888,1120` |
| 问题 | UE 自身已经把 `ExpandEnumAsExecs` 与 `ExpandBoolAsExecs` 定义为函数级元数据，其中 `ObjectMacros.h` 明确把 `ExpandBoolAsExecs` 标成 `ExpandEnumAsExecs` 的同义项，`K2Node_CallFunction.cpp` 也直接用这两个 metadata 决定是否为函数创建 exec pins，并读取具体的参数名。可当前 Angelscript 生成链完全没有对应 schema：`FAngelscriptFunctionSignature` 没有分支字段，`ModifyScriptFunction()` 不处理任何 exec-expansion 语义，`Entries.csv` 也只有 `ModuleName, EditorOnly, ClassName, FunctionName, EntryKind, EraseMacro, ShardIndex` 7 列。实证样本包括 `UCameraLensEffectInterfaceClassSupportLibrary::{IsInterfaceClassValid, IsInterfaceValid, SetInterfaceClass}` 和 `UDataTableFunctionLibrary::EvaluateCurveTableRow`，它们在引擎头里都带 `ExpandEnumAsExecs=...`，但当前产物只留下普通 `Stub` 行，完全看不出哪个参数承载分支结果。对 `Plugins/Angelscript/Source/{AngelscriptUHTTool,AngelscriptRuntime,AngelscriptEditor,AngelscriptTest}` 执行 `rg 'ExpandEnumAsExecs|ExpandBoolAsExecs'` 的结果为 `0` 行，进一步证明这组 metadata 当前没有进入插件源码消费链。 |
| 根因 | generated binding 只建模“函数能否注册”和“对应哪个 erase macro”，没有把 Blueprint 节点的控制流契约视为正式 schema；因此与 exec 分支相关的 metadata 在 UHT 导出后被整体丢弃。 |
| 影响 | 即使函数本身存在，sidecar、调试器和后续脚本工具仍无法知道“哪个返回值/输出参数会展开成执行分支”。这会让 generated binding 在 Blueprint parity 上长期缺少一层关键契约，也让未来无论做 IDE 提示、自动包装还是行为对比，都只能重新回到头文件挖 metadata。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 generated binding 增加显式的 exec-expansion 描述，把分支来源参数及其 expansion kind 收进 schema，再决定是否提供脚本层 sugar。 |
| 具体步骤 | 1. 在 UHTTool 生成模型中新增 `ExecExpansionKind` 与 `BranchResultArgumentName/Index` 字段，至少支持 `None`、`Enum`、`Bool` 三种状态；数据来源直接读取 `ExpandEnumAsExecs` / `ExpandBoolAsExecs` metadata。 2. 在 `Helper_FunctionSignature.h` 或配套参数 manifest 中保存这组信息，避免 runtime 侧只能看到普通参数列表却不知道哪个参数承担执行分支角色。 3. 扩展 `AngelscriptFunctionTableCodeGenerator.cs` 的 sidecar 输出，把 `BranchResultArgument` 放入参数清单或 JSON schema；不要继续把这类结构信息挤进现有 7 列 CSV 平面表。 4. 在 runtime 注册后处理阶段把 `ExecExpansionKind` 作为 canonical metadata 挂到 script function，至少让调试、诊断、未来编辑器桥接能读取到同一份信息。 5. 第一阶段只要求“契约可观测”；第二阶段如果团队希望脚本层更接近 Blueprint，可基于该字段生成 `Try...` 或枚举分支 helper surface，但不得在没有 schema 的前提下直接做语法糖特判。 6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` 增加回归，至少锁住 `IsInterfaceClassValid`、`SetInterfaceClass`、`EvaluateCurveTableRow` 三个样本，要求导出 sidecar 能指出 `Result` 或 `OutResult` 是分支参数。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Helper_FunctionSignature.h`, `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/Bind_BlueprintCallable.cpp`, `Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果在没有参数级 schema 的情况下只增加函数级布尔开关，后续仍然无法稳定定位是哪一个参数驱动分支；因此这项工作要么复用 Issue-124 的参数 manifest，要么最少提供一个稳定的参数引用键。 |
| 前置依赖 | 建议与 Issue-124 的参数级 schema 一起推进；若 Issue-124 暂缓，必须先定义 `BranchResultArgumentName` 的稳定输出字段。 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 camera-lens 与 data-table 样本的 sidecar 中出现 `ExecExpansionKind` 和 `BranchResultArgument`。 2. 检查调试或 metadata 导出路径可读到这两个字段，而不是仍然只看到普通函数记录。 3. 新增自动化确保未来引入新的 `ExpandEnumAsExecs` / `ExpandBoolAsExecs` 样本时，不会再静默丢失分支参数信息。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-128 | Architecture | 在参数 schema 落地时一并补上，把 exec 分支元数据从“完全丢失”提升为“稳定可观测” |

---

## 发现与方案 (2026-04-09 02:40)

### Issue-129：`PURE_VIRTUAL(...)` BlueprintCallable 会被误判成 `unexported-symbol`，宏生成的 inline/pure-virtual 语义完全没有进入可见性判定

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Misc/CoreMiscDefines.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/MediaAssets/Public/MediaSource.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Interchange/Core/Public/InterchangeTranslatorBase.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Engine/TimecodeProvider.h`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_SkippedEntries.csv`, `Plugins/Angelscript/Intermediate/Build/Win64/UnrealEditor/Inc/AngelscriptRuntime/UHT/AS_FunctionTable_Entries.csv` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:295-314, 316-359`; `CoreMiscDefines.h:100-103`; `MediaSource.h:56-65`; `InterchangeTranslatorBase.h:89-94`; `TimecodeProvider.h:70-94`; `AS_FunctionTable_SkippedEntries.csv:2561-2562, 3057-3058, 3142-3143`; `AS_FunctionTable_Entries.csv:3916-3917` |
| 问题 | `IsLinkVisible()` 目前只把 `*_API`、`inline/FORCEINLINE/constexpr` 或声明文本里直接出现的 `{` 视为“可链接”。但 UE 的 `PURE_VIRTUAL(func, ...)` 并不是普通注释宏；`CoreMiscDefines.h:100-103` 明确表明它在默认配置下会展开成带函数体的 `{ LowLevelFatalError(...); __VA_ARGS__ }`，在 `CHECK_PUREVIRTUALS` 下则展开成 `=0;`。当前 resolver 没有识别这条语义，导致 raw header 中形如 `virtual FString GetUrl() const PURE_VIRTUAL(...);`、`virtual EInterchangeTranslatorAssetType GetSupportedAssetTypes() const PURE_VIRTUAL(...);`、`virtual FQualifiedFrameTime GetQualifiedFrameTime() const PURE_VIRTUAL(...);` 的 BlueprintCallable 统一被判成 `unexported-symbol`。现有产物已经体现为 `UMediaSource::{GetUrl,Validate}`、`UInterchangeTranslatorBase::{GetSupportedAssetTypes,GetSupportedFormats}`、`UTimecodeProvider::{GetQualifiedFrameTime,GetSynchronizationState}` 均进入 `SkippedEntries.csv`，其中 `UTimecodeProvider` 对应 entry 已退化成 `ERASE_NO_FUNCTION()`。 |
| 根因 | 可见性判定完全基于“未经宏展开的声明文本”，只看 API 宏和显式 `{`，没有把 `PURE_VIRTUAL` 这类 UE 自带的“函数体/纯虚占位宏”纳入 declaration normalization。 |
| 影响 | 一批真实存在于 runtime API 面的 BlueprintCallable 抽象接口会被系统性降级成 `Stub`，sidecar 还会误导开发者把根因理解成“未导出符号”。这不仅损伤 direct-bind 覆盖率，也让后续 UE 宏兼容工作继续遗漏最典型的 engine 宏之一。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 resolver 增加 `PURE_VIRTUAL` 专项识别，把它视为 UE 语义层的“inline/virtual stub 声明”，而不是普通未导出文本。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 新增 `ContainsPureVirtualStub()` 或更通用的 `ContainsUeInlineBodyMacro()`，识别 declaration 尾部的 `PURE_VIRTUAL(`。 2. 修改 `IsLinkVisible()`：当 declaration 含 `PURE_VIRTUAL(` 时，不再直接走 `unexported-symbol`，而是把该候选归类为“宏提供函数体/可取地址的 virtual declaration”。 3. 为 `CHECK_PUREVIRTUALS` 边界补一个显式策略分支：优先通过一个最小 compile smoke test 验证 `ERASE_METHOD_PTR(Class, Func, ...)` 对 `PURE_VIRTUAL` 成员在当前 toolchain 下可编译；若该配置不允许取地址，则把 failure reason 改成新的 `pure-virtual-policy`，不要继续伪装成 `unexported-symbol`。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加回归样本，至少锁住 `UMediaSource::GetUrl`、`UInterchangeTranslatorBase::GetSupportedAssetTypes`、`UTimecodeProvider::GetQualifiedFrameTime` 三个函数，断言它们不再因为 `PURE_VIRTUAL` 落入 `unexported-symbol`。 5. 如果最终策略允许 direct bind，则检查对应 `AS_FunctionTable_<Module>_*.cpp` 中已经生成 `ERASE_METHOD_PTR` / `ERASE_AUTO_METHOD_PTR`；如果策略要求跳过，也必须在 skipped CSV 中输出专门 reason，便于后续分析。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | `PURE_VIRTUAL` 在 `CHECK_PUREVIRTUALS` 配置下会展开成 `=0;`，因此不能只凭字符串替换就盲目宣称“必然可直绑”；需要用 compile smoke test 固化当前编译配置下的真实可编译性。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 重新运行 UHT 导出，确认 `SkippedEntries.csv` 不再把上述样本记录为 `unexported-symbol`。 2. 检查 `Entries.csv` 与对应 shard `.cpp`，确认样本要么转成 direct bind，要么被新的专门 reason 标识。 3. 运行新增自动化，确保未来再遇到 `PURE_VIRTUAL` BlueprintCallable 时不会重新退回旧分类。 |

### Issue-130：`AngelscriptRuntime.Build.cs` 被当成整文件外部依赖，注释或非依赖配置改动也会触发整轮 UHT 导出重跑

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptRuntime/AngelscriptRuntime.Build.cs` |
| 行号 | `AngelscriptFunctionTableCodeGenerator.cs:51-78, 166-205, 334-382`; `AngelscriptRuntime.Build.cs:20-27, 81-87` |
| 问题 | `LoadSupportedModules()` 先把 `AngelscriptRuntime.Build.cs` 整个注册成 `factory.AddExternalDependency(buildCsPath)`，随后用 `File.ReadAllLines(buildCsPath)` 对整份文件做逐行文本扫描，只提取 `DependencyModuleNames.AddRange(...)` 里的字符串模块名。当前 `Build.cs` 明确包含大量与模块集合无关的内容，例如 `OptimizeCode` 配置、第三方 include path、注释掉的 `PluginPath` 样例和注释说明。由于依赖粒度停留在“整文件”，这些非语义位置的改动也会让 UHT 认为导出输入发生变化，进而重新执行 `Generate()` 的整轮模块遍历、header 解析和 summary/CSV 写盘。 |
| 根因 | 增量判断建立在 `Build.cs` 原始文本文件上，而不是建立在“解析后的支持模块集合/EditorOnly 集合”这种语义化 fingerprint 上。 |
| 影响 | 任何对 `AngelscriptRuntime.Build.cs` 的注释整理、格式化、include path 调整或非依赖开关修改，都会触发整轮 UHTTool 重跑；在当前实现里，这意味着所有模块都会再次走 `CollectEntries()` / `HeaderSignatureResolver`，而 sidecar 还会被重新写盘。增量构建因此对无关改动过于敏感，开发者会感受到不必要的全量导出成本。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `Build.cs` 依赖从“整文件文本”收敛成“稳定的模块语义 fingerprint”，只在支持模块集合真实变化时触发 UHT function-table 重新生成。 |
| 具体步骤 | 1. 在 `LoadSupportedModules()` 中先把解析结果归一化成稳定结构，例如 `allModules/editorOnlyModules` 的排序列表，并计算 `SupportedModulesFingerprint`。 2. 将这个 fingerprint 写入独立 sidecar，例如 `AS_FunctionTable_ModuleSet.json`，内容只包含模块集合与 editor-only 集合，不包含 `Build.cs` 其余文本。 3. 下一轮导出时先读取旧 fingerprint；若新旧集合一致，则短路 `Generate()` 中后续的 shard/summary/skipped 生成流程，避免整轮 header 解析。 4. 长期方案仍应落实已有的 UHT/UBT 元数据适配计划，用结构化模块依赖来源替代 `Build.cs` 文本扫描；但在完全切走前，至少不要再把整份 `Build.cs` 当作增量边界。 5. 为 `Build.cs` 变更增加两类回归：一类只改注释或 `OptimizeCode` 之类非依赖配置，要求 fingerprint 不变且本轮生成被短路；另一类真实增删依赖模块，要求 fingerprint 更新并只对受影响模块生成/清理输出。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableExporter.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 fingerprint 只覆盖模块名而忽略 editor-only 边界或未来的生成策略开关，可能把真实需要重跑的场景错误短路；因此 fingerprint 字段必须与生成范围一一对应。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 仅修改 `AngelscriptRuntime.Build.cs` 注释或 `OptimizeCode` 分支，重新运行导出，确认 function-table 生成被短路且输出文件时间戳不变。 2. 真实新增或删除一个依赖模块，确认 fingerprint 变化并触发对应模块产物更新。 3. 记录前后运行日志，确认“visited packages / reconstructed / skipped”这类整轮统计不会再因无关 `Build.cs` 改动重复出现。 |

### Issue-131：resolver 没有统一的 declaration normalization 阶段，特殊 UE 宏兼容规则散落成多套半解析逻辑

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Engine/Classes/Kismet/KismetMathLibrary.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/AIModule/Classes/AIController.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/MediaAssets/Public/MediaSource.h`, `J:/UnrealEngine/UERelease/Engine/Source/Runtime/Core/Public/Misc/CoreMiscDefines.h` |
| 行号 | `AngelscriptHeaderSignatureResolver.cs:15-16, 295-359, 438-463, 509-528, 664-697`; `KismetMathLibrary.h:188-191`; `AIController.h:294-303`; `MediaSource.h:56-65`; `CoreMiscDefines.h:100-103` |
| 问题 | 当前 header resolver 没有单一的“声明归一化”阶段，而是把宏处理拆成多套彼此独立的字符串规则：`StripLeadingMacroInvocations()` 只处理前缀全大写宏，`CleanReturnType()` 只在 `void` 路径删除部分 token，`StripLeadingUparam()` 只认参数级 `UPARAM`，`FindAccessSpecifier()` 只扫字面量 `public:/protected:/private:`，`IsLinkVisible()` 又单独依赖 API 宏和显式 `{`。这些规则既不共享 token stream，也不共享宏目录，结果同一种 UE 宏会在不同阶段以不同方式“半生不熟”地被处理。源码中的真实样本已经说明这一点：`GENERATED_UCLASS_BODY()` 让 `UKismetMathLibrary` 的 public API 被误判成 `non-public`，`public:` + `UFUNCTION` + 函数级 `AIMODULE_API` 会遮蔽导出宏，`PURE_VIRTUAL(...)` 又完全不会进入 link-visible 判定。 |
| 根因 | 工具没有把“UE 声明宏/访问标签/后缀宏/参数宏”建模成统一的解析层，而是让多个 helper 各自消费原始字符串并重复实现局部语义。 |
| 影响 | 每新增一种 UE 宏写法，就需要在多个 helper 上做定点补丁；修复一个问题后，另一个阶段仍可能继续误判。当前已经连续暴露出 `GENERATED_UCLASS_BODY`、`public:`、`UE_DEPRECATED`、`PURE_VIRTUAL` 多类缺陷，说明这不是单点 bug，而是解析架构没有建立稳定适配边界。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 resolver 前面新增统一的 `DeclarationNormalizer`/`DeclarationTokenStream`，把 UE 宏和访问标签先归一化成结构化语义，再让可见性、签名和参数解析共用同一份结果。 |
| 具体步骤 | 1. 在 `AngelscriptHeaderSignatureResolver.cs` 旁新增 `AngelscriptDeclarationNormalizer.cs`，输入 raw declaration 文本，输出结构化结果：`LeadingAttributes`、`AccessLevel`、`ApiVisibility`、`BodyKind(None/Inline/PureVirtual)`、`NormalizedPrefix`、`NormalizedParameters`、`TrailingQualifiers`。 2. 把 `GENERATED_UCLASS_BODY`、`GENERATED_BODY` 注入的默认访问级别，`UFUNCTION/UPARAM/UE_DEPRECATED` 这类前缀宏，`PURE_VIRTUAL` 这类后缀体宏，全部收进一份集中式 `UeDeclarationMacroCatalog`，不要继续分散在多个 `if/Regex` 中。 3. 让 `FindAccessSpecifier()`、`IsLinkVisible()`、`CleanReturnType()`、`StripLeadingUparam()` 这些 helper 改为消费 `NormalizedDeclaration`，删除重复字符串清洗逻辑。 4. 为 normalizer 建立 golden tests，至少覆盖 `GENERATED_UCLASS_BODY`、`public:` + 函数级 `*_API`、`UE_DEPRECATED("... ) ...")`、`PURE_VIRTUAL(...)`、`UPARAM(DisplayName="X (Roll)")` 五类样本。 5. 在 `TryBuild()` 中优先使用结构化结果，只有完全无法归一化的 declaration 才回退到保守 `Stub`/专门 failure reason，而不是让不同 helper 各自静默失败。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptHeaderSignatureResolver.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptDeclarationNormalizer.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | L |
| 风险 | 这是解析主干重构，若一次性替换全部 helper 而没有 golden cases，容易把旧的偶然兼容样本一起打坏；必须先建立样本库，再逐步切换调用点。 |
| 前置依赖 | 建议先锁定 Issue-129 的 `PURE_VIRTUAL` 行为预期，再把该语义纳入 normalizer；否则重构过程中容易重复返工。 |
| 验证方式 | 1. 新增 declaration normalizer 单测，固定上述五类样本的结构化输出。 2. 重新运行 UHT 导出，确认 `GENERATED_UCLASS_BODY`、函数级 `*_API`、`PURE_VIRTUAL` 样本不再回归。 3. 检查 resolver 代码，确认旧的分散 helper 已被削减为少量薄封装，而不是继续维持多套字符串协议。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-129 | Defect | 先处理，优先补齐 `PURE_VIRTUAL` 的可见性/策略判定，减少当前真实 API 被误记为 `unexported-symbol` |
| P2 | Issue-130 | Defect | 第二步处理，把 `Build.cs` 的整文件依赖收敛成模块集合 fingerprint，缩小无关改动触发的全量导出 |
| P1 | Issue-131 | Architecture | 与 Issue-129 并行设计，先搭 declaration normalizer 样本库，再逐步收拢特殊 UE 宏解析逻辑 |

---

## 发现与方案 (2026-04-09 03:00)

### Issue-132：`AngelscriptUHTTool` 独立工程没有受控的 `EngineDir` 解析入口，当前构建实际依赖本机未跟踪补丁文件

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj.props`, `Documents/Guides/Build.md` |
| 行号 | `AngelscriptUHTTool.ubtplugin.csproj:1-2, 41-52`; `AngelscriptUHTTool.ubtplugin.csproj.props:1-5`; `Build.md:12-20, 229-235` |
| 问题 | 仓库标准构建约定明确要求“先读取 `AgentConfig.ini`，再只通过 `Tools\\RunBuild.ps1` 执行构建”，`Build.md:14-20,229-235` 已把这条约束写死；但 `AngelscriptUHTTool.ubtplugin.csproj` 仍直接依赖外部 `$(EngineDir)`，用它导入 `UnrealEngine.csproj.props` 并定位 `EpicGames.Build/Core/UHT` 与 `UnrealBuildTool.dll`。我对 `Tools` 与 `Documents/Guides` 执行 `rg 'AngelscriptUHTTool\\.ubtplugin\\.csproj|dotnet build.*AngelscriptUHTTool|EngineDir'`，没有找到任何受控的 UHTTool 专用构建入口或 `/p:EngineDir=` 传递逻辑；当前工作区唯一能把这个工程补齐到可编译状态的，是未跟踪文件 `AngelscriptUHTTool.ubtplugin.csproj.props`，其中把 `EngineDir` 直接写成了本机路径 `J:\\UnrealEngine\\UERelease\\Engine`。`git status --short` 也确认该文件当前是 `??` 未跟踪状态。 |
| 根因 | UHTTool 独立 `.csproj` 没有接入仓库统一的 `AgentConfig.ini -> EngineRoot` 解析链，而是把引擎定位责任外包给调用者或本机临时 props。 |
| 影响 | 新 worktree、CI、其他开发机或切换到不同 `EngineRoot` 时，UHTTool 构建不可复现；更糟的是，如果本机 props 指向另一套 UE 分支，工程会静默对着错误的 UHT/UBT API 面编译，直到运行时或后续增量导出才暴露版本错配。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 UHTTool 补齐受控的 `EngineDir` 解析入口，彻底移除“靠本机未跟踪 props 补路径”的隐式工作流。 |
| 具体步骤 | 1. 在 `Tools/` 下新增专用入口，例如 `Tools/RunUhtToolBuild.ps1`，复用 `Tools/Shared/UnrealCommandUtils.ps1` 读取 `AgentConfig.ini [Paths] EngineRoot`，并显式向 `dotnet build Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj` 传递 `/p:EngineDir=<EngineRoot>\\Engine`。 2. 在 `AngelscriptUHTTool.ubtplugin.csproj` 中加入缺失时的 fail-fast 提示，例如在 `Import` 前检查 `$(EngineDir)` 非空，不再允许默默依赖本地 props。 3. 删除对 `AngelscriptUHTTool.ubtplugin.csproj.props` 这类本机补丁文件的工作流依赖；若必须保留本地覆盖，改成文档明确说明的可选用户文件，并确保它不再是默认入口。 4. 在 `Documents/Guides/Build.md` 增补一节 UHTTool 构建说明，明确“UHTTool 只能通过 `Tools/RunUhtToolBuild.ps1` 或等价受控入口构建”，与现有 `RunBuild.ps1` 规范保持一致。 5. 在该入口里打印最终使用的 `EngineRoot/EngineDir` 与关键 DLL 路径，防止未来再次出现“实际上编的是另一套 UE”但日志无感知的情况。 |
| 涉及文件 | `Tools/RunUhtToolBuild.ps1`, `Tools/Shared/UnrealCommandUtils.ps1`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj`, `Documents/Guides/Build.md` |
| 预估工作量 | M |
| 风险 | 如果已有个别开发机依赖本地 props 的旧习惯，切到统一入口后会先暴露环境配置问题；但这是应当显式暴露的正确失败方式。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 删除本机 `AngelscriptUHTTool.ubtplugin.csproj.props` 后，通过新入口重新构建，确认仍能成功解析 `EngineDir`。 2. 在一个没有该本地 props 的新 worktree 上执行同一入口，确认行为一致。 3. 人工把 `AgentConfig.ini` 指到另一套 `EngineRoot`，确认日志会明确显示新的解析结果，而不是继续沿用旧本机路径。 |

### Issue-133：UHTTool 的 `Debug/Development/Release` 与不同引擎构建共用同一个 `OutputPath`，增量边界会被旧 DLL/PDB 污染

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj` |
| 行号 | `AngelscriptUHTTool.ubtplugin.csproj:5-18, 20-34` |
| 问题 | 工程同时声明了 `Debug;Release;Development` 三种配置，但 `OutputPath` 固定写死到 `..\\..\\Binaries\\DotNET\\UnrealBuildTool\\Plugins\\AngelscriptUHTTool\\`，且 `AppendTargetFrameworkToOutputPath=false`。与此同时，配置差异是真实存在的：基线 `DebugType=pdbonly`，`Debug|AnyCPU` 又切成 `DebugType=full` 并开启 `DEBUG` 常量。也就是说，不同配置会产出不同内容，却被强行写进同一个目录。我对当前输出目录执行 `Get-ChildItem`，结果只看到单份根级 `AngelscriptUHTTool.dll` 和 `AngelscriptUHTTool.pdb`，没有任何按配置或引擎隔离的子目录。该目录中还直接堆放了 `EpicGames.UHT.dll`、`UnrealBuildTool.dll` 等引擎依赖副本，说明不同 `EngineDir` 构建也会争用同一缓存目录。 |
| 根因 | 工程输出身份只由固定 `AssemblyName + OutputPath` 决定，没有把 `Configuration`、`EngineDir` 或 UE binary surface 版本纳入输出键。 |
| 影响 | 1. `Debug` 构建会覆盖 `Development`/`Release` 产物，PDB 与调试符号状态不再可信。 2. 切换到另一套 `EngineRoot` 或 UE 分支后，旧的 `EpicGames.*`/`UnrealBuildTool.*` 副本仍可能留在同一路径，污染后续增量判断与加载结果。 3. 任何 “为什么本次 UHTTool 行为和当前源码/当前配置不一致” 的排查，都会先撞上共享输出目录造成的旧二进制残留。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 UHTTool 输出目录升级为“配置 + 引擎指纹”隔离目录，禁止不同配置和不同引擎 surface 共用同一份二进制缓存。 |
| 具体步骤 | 1. 将 `OutputPath` 改为带配置维度的路径，例如 `..\\..\\Intermediate\\DotNET\\AngelscriptUHTTool\\$(Configuration)\\`；如需长期缓存，再追加 `$(EngineDir)` 派生的短哈希或 UE version stamp。 2. 保留最终“可供 UBT/UHT 装载”的发布位置，但把它变成显式 promote/copy 步骤，而不是所有配置直接写同一目录。 3. 在专用构建入口中计算 `EngineDirFingerprint`，并把该值传给 `.csproj`，确保不同 `EngineRoot` 至少落到不同子目录。 4. 为输出目录增加 clean/cull 步骤：当 `Configuration` 或 `EngineDirFingerprint` 切换时，先清掉上一套目录，避免旧 `EpicGames.*` 依赖残留。 5. 在构建日志中打印 `Configuration`、最终 `OutputPath`、`EngineDirFingerprint`，让二进制来源可回溯。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptUHTTool.ubtplugin.csproj`, `Tools/RunUhtToolBuild.ps1` |
| 预估工作量 | S |
| 风险 | 若 UBT/UHT 当前假定固定插件二进制位置，改成隔离目录后需要补一层发布/复制逻辑；否则工具能编出来但加载不到。 |
| 前置依赖 | 建议先完成 Issue-132，先把 `EngineDir` 解析受控化，再把它纳入输出目录指纹。 |
| 验证方式 | 1. 分别构建 `Debug` 与 `Development`，确认产物落在两个不同目录，不再互相覆盖。 2. 再对另一套 `EngineRoot` 构建同一配置，确认会生成新的 engine-stamped 输出目录。 3. 检查旧目录中的 `AngelscriptUHTTool.dll`、`EpicGames.UHT.dll` 不会被新构建复用或覆盖。 |

### Issue-134：生成 shard 的 C++ 文本没有任何语法级自检，缺分号或括号失衡只能等后续整轮 Unreal 编译才暴露

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 行号 | `AngelscriptFunctionSignatureBuilder.cs:17-37`; `AngelscriptFunctionTableCodeGenerator.cs:19-22, 282-325`; `AngelscriptGeneratedFunctionTableTests.cpp:74-147, 459-748` |
| 问题 | 当前 generator 会把 `ReturnType`、`ParameterTypes`、`FunctionName` 和 `EraseMacro` 直接拼进 `ERASE_METHOD_PTR(...)` / `ERASE_FUNCTION_PTR(...)` 与 `FAngelscriptBinds::AddFunctionEntry(...)` 文本，再由 `BuildShard()` 用 `AppendLine()` 逐行手工拼整份 `.cpp`。这条链路没有任何语法级 guard：提交前不做括号/花括号/引号平衡检查，也没有最小 compile smoke。现有自动化同样没有覆盖这条边界，`CountGeneratedBindingRegistrations()` 和 `FindGeneratedBindingLine()` 只是在文本里数 `AddFunctionEntry(` 和查子串；后续 summary/csv 测试也只校验 JSON/CSV 字段与计数关系。也就是说，只要未来某次 signature/模板改动把 `EraseMacro` 拼坏成“少一个 `)`/`;`”，第一现场不会出在 UHTTool，而会延迟到整轮 Unreal C++ 编译。 |
| 根因 | 生成器把 emitted C++ 当成纯字符串处理，而不是当成需要最小结构校验的正式编译产物；测试侧也只验证“有没有生成某行”，不验证“生成的这行是否仍是合法 C++”。 |
| 影响 | 这会把 `缺失分号`、`括号不配对`、引号闭合错误之类最基础的生成缺陷后移到下游编译阶段，反馈回路长且定位成本高。随着 Issue-51 的模板化重构、Issue-76/88/117 一类签名修复继续推进，生成文本复杂度会上升，这条缺口会越来越容易被触发。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 `CommitOutput()` 之前引入生成文本结构校验，并用专门自动化把“文本存在”升级为“文本最少语法成立”。 |
| 具体步骤 | 1. 在 `AngelscriptFunctionTableCodeGenerator.cs` 新增 `ValidateGeneratedShard(StringBuilder builder)`，至少校验圆括号、花括号、双引号、`#if/#endif` 数量平衡，并确认每条 `AddFunctionEntry(...)` 行以 `;` 结束。 2. 将 `BuildShard()` 从“直接返回 `StringBuilder`”改成返回结构化结果，例如 `GeneratedShardArtifact { FilePath, Content, ValidationErrors }`；有错误时在 UHT 阶段直接 fail-fast，而不是继续写盘。 3. 为 `AngelscriptFunctionSignature.BuildEraseMacro()` 增加针对显式签名路径的自检 helper，至少覆盖 `ERASE_METHOD_PTR` / `ERASE_FUNCTION_PTR` 的参数包和返回值包闭合。 4. 在 `AngelscriptGeneratedFunctionTableTests.cpp` 增加一组专门的 shard syntax 回归，不再只查字符串存在；最小版本可以扫描所有 `AS_FunctionTable_*.cpp` 并复用同一套平衡检查，进阶版本则加一个 compile-only smoke target。 5. 将这套 validator 设计成模板 writer 也能复用的公共层，避免 Issue-51 完成后又回到“模板渲染完直接写盘、没有任何静态检查”。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionSignatureBuilder.cs`, `Plugins/Angelscript/Source/AngelscriptUHTTool/AngelscriptFunctionTableCodeGenerator.cs`, `Plugins/Angelscript/Source/AngelscriptTest/Core/AngelscriptGeneratedFunctionTableTests.cpp` |
| 预估工作量 | M |
| 风险 | 语法 validator 如果只做简单字符计数，容易误伤宏或字符串字面量场景；实现时要复用 Issue-74/89 那套字符串与转义状态机，避免 validator 自己变成新的噪声源。 |
| 前置依赖 | 建议与 Issue-51 的模板 writer 重构一起设计，但不必等待其全部完成后再落地最小 validator。 |
| 验证方式 | 1. 人工制造一个缺 `;` 或少 `)` 的坏 shard 样本，确认 validator 在 UHT 阶段直接报错。 2. 正常导出一轮，确认所有 `AS_FunctionTable_*.cpp` 都能通过新 validator。 3. 运行新增自动化，确认测试不再只接受“包含目标子串”的文件，而是要求 shard 文本满足最小语法约束。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-132 | Architecture | 先处理，先把 `EngineDir` 解析拉回仓库受控入口，消除本机未跟踪 props 依赖 |
| P1 | Issue-133 | Defect | 第二步处理，拆开 `Debug/Development/Release` 与不同 `EngineRoot` 的共享输出目录，修复二进制级增量污染 |
| P2 | Issue-134 | Architecture | 在继续推进模板化 emitter 与签名修复前补上，避免生成文本缺陷继续后移到整轮 Unreal 编译 |
