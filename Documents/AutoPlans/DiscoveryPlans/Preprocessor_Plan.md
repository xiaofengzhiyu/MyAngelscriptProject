# Preprocessor 发现与方案规划

---

## 发现与方案 (2026-04-08 12:25)

### Issue-1：脚本目录扫描未处理 symlink/junction 环，可能在发现阶段无限递归

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1326-1361，1944-2014 |
| 问题 | 已验证事实：`DiscoverScriptRoots()` 仅对 `ConvertRelativePathToFull()` 返回的字符串做 `ScriptPath != RootPath` 比较，`FindAllScriptFilenames()` 之后对每个 root 直接递归 `FindScriptFiles()`，而 `FindScriptFiles()` 只基于 `FindFiles()` 返回的目录名继续向下调用自己，没有任何 `visited` 集合、物理路径 canonicalization 或 reparse-point/symlink 过滤。推断：若某个 `Script/` 子目录是指向祖先目录或另一 script root 的 symlink/junction，递归会重复进入同一物理目录，最终表现为长时间卡死、栈递归失控，或同一物理脚本被重复枚举。 |
| 根因 | 文件系统发现层把“目录身份”建模成当前字符串路径，而不是 canonical physical path；目录递归也没有建立 loop guard。 |
| 影响 | 一旦工程用 junction/symlink 复用脚本目录，预处理入口还没开始解析 `import` 就可能先在文件发现阶段失去终止性；即便没有形成严格环，也会把同一物理脚本作为多个逻辑文件送入模块系统，放大后续 module name 冲突与 hot reload 漂移。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在脚本发现层引入 canonical path 去重与目录访问环检测，把 symlink/junction 风险挡在进入 `Preprocessor.AddFile()` 之前。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.cpp` 提取 `CanonicalizeScriptPath(const FString&)`，统一执行 `ConvertRelativePathToFull`、`CollapseRelativeDirectories`、`NormalizeFilename`，并在平台能力允许时补一层“磁盘真实路径”解析。2. 把 `DiscoverScriptRoots()` 的根目录去重从原始字符串比较改成 canonical path 比较，避免同一物理 root 通过别名重复入表。3. 扩展 `FindScriptFiles()` 签名，传入 `TSet<FString>& VisitedDirectories`；每次递归前先把当前目录转成 canonical path，若已访问则记录 warning 并停止深入。4. 对被枚举出的文件也建立 canonical 去重，保证同一物理 `.as` 只生成一份 `FFilenamePair`。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` 增加 fake file-tree 用例，模拟 alias root 与目录环，断言发现结果稳定终止且无重复文件；必要时在 `Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp` 增加 trace 用例验证日志。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | canonical path 的平台差异较大，若直接依赖某个平台专有 API，可能影响现有测试环境；需要先保留“无法解析真实路径时退回标准化绝对路径”的后备路径。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增自动化用例覆盖 alias root 与目录环。2. 对真实工程执行一次脚本发现 trace，确认文件数量稳定且无重复 physical path。3. 人工构造一个指向父目录的 junction/symlink，验证发现过程不会卡死且只输出一次 warning。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-1 | Defect | 下个迭代优先修复 |

---

## 发现与方案 (2026-04-08 12:27)

### Issue-2：预处理器直接依赖全局 engine 状态和可变 hook，模块描述符缓存与增量预处理没有稳定输入边界

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 行号 | 8-30，120-180，255；38-71，214-287；123-129 |
| 问题 | 已验证事实：`FAngelscriptPreprocessor` 构造时直接读取 `FAngelscriptEngine::ShouldUseEditorScriptsForCurrentContext()`、`IsSimulatingCookedForCurrentContext()`、`UAngelscriptSettings` 默认对象来填充 `PreprocessorFlags` 与默认 specifier；`Preprocess()` 过程中再次读取 `FAngelscriptEngine::Get().ConfigSettings` 和 `ShouldUseAutomaticImportMethodForCurrentContext()`；同时 `OnProcessChunks` / `OnPostProcessCode` 是静态 multicast delegate，参数类型是非 `const FAngelscriptPreprocessor&`，而 `Files`、`PreprocessorFlags` 等内部状态又是公开成员。learning trace 测试也证明外部监听器能够在流水线中途拿到这份 live 对象。也就是说，预处理输出不仅取决于脚本内容，还取决于 ambient engine context、默认对象状态和当前进程已注册的 hook 集合。 |
| 根因 | 预处理器没有显式的 `PreprocessContext` 输入对象，也没有把扩展点限制为只读观察或受控变换；模块描述符和后续 cache key 因而无法完整表达“本次预处理用了什么上下文”。 |
| 影响 | 相同文件集在不同 editor/runtime 上下文、不同 hook 注册顺序下可能产生不同 `ProcessedCode`、`ImportedModules`、`PostInitFunctions` 与 `CodeHash`，但当前模块系统没有记录这些差异来源。这会直接阻塞增量预处理和 descriptor cache 设计，因为无法判断“文件没变但上下文变了”是否需要失效；同时单元测试也必须搭建完整 engine singleton 才能稳定覆盖预处理逻辑。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把预处理器改造成“显式上下文 + 受控扩展”的纯输入模型，为模块描述符缓存和未来增量预处理建立可哈希的合同。 |
| 具体步骤 | 1. 新增 `FAngelscriptPreprocessContext`，一次性收拢 `PreprocessorFlags`、automatic-import 模式、editor/cooked 模式、默认 property/function specifier、`UAngelscriptSettings` 快照版本。2. 让 `FAngelscriptEngine` 在 compile/hot reload 入口先构造这份 context，再把它注入 `FAngelscriptPreprocessor`，移除预处理阶段对 `FAngelscriptEngine::Get()` 与默认对象的二次读取。3. 将 `OnProcessChunks` / `OnPostProcessCode` 分裂成两类接口：只读 trace hook 保持 `const` 访问；如确有扩展需求，则改为显式 `IPreprocessPass` 注册表，并规定执行顺序、输入输出结构和版本号。4. 在 `FAngelscriptModuleDesc` 与 `FAngelscriptPrecompiledModule` 中新增 `PreprocessContextHash`，缓存命中时联合 `CodeHash` 与 context hash 校验。5. 以文件级 `ProcessedCode + ContextHash` 为键设计后续增量预处理缓存，保证仅当源码或上下文变化时才失效。6. 补充测试：同一脚本在不同 context 下产生不同 hash；同一 context 下重复运行得到相同输出；trace hook 无法再修改 `Files`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | L |
| 风险 | 现有外部扩展若依赖直接改写 `Files` 或 `GeneratedCode`，切换到受控 pass 接口后需要迁移；需要先盘点实际 hook 使用方，避免一次性破坏 editor 工具链。 |
| 前置依赖 | 建议先完成 hook 使用点盘点，确认哪些场景只需要 trace，哪些场景真的需要变更代码。 |
| 验证方式 | 1. 新增稳定性测试，验证相同 context 重跑两次得到相同 `ProcessedCode` 与 `PreprocessContextHash`。2. 新增 context 变化测试，验证 `EDITOR`/automatic-import 切换会使 hash 变化并触发 cache miss。3. 回归 learning trace，确认 hook 仍能观察阶段事件，但不能再任意修改内部 `Files`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-2 | Architecture | 在修复高风险缺陷前先设计上下文合同，随后并行落地 |

---

## 发现与方案 (2026-04-08 12:28)

### Issue-3：`ParseIntoChunks()` 过长且混合多套手写扫描器，已成为 import/include/module 边界缺陷的集中源头

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3021-4210，317-437，4145-4194 |
| 问题 | 已验证事实：`ParseIntoChunks()` 单函数覆盖约 1190 行源码，内部同时维护 `bInString`、comment 状态、`IfDefStack`、namespace 栈、macro 解析、chunk 切分、`import`/`delegate`/`namespace`/`enum`/`class` 等多个入口；同一函数内至少有 19 处分支通过 `FCString::Strncmp(&File.RawCode[ChunkEnd], ...)` 直接在原始字符流上判关键字。与它相邻的 `ParseSpecifier()` / `ParseSpecifiers()`、`FindScopeCloseBracket()`、`FindSemicolonDirectlyAfter()` 又各自维护一套括号/引号/终止符扫描逻辑。也就是说，同一份语法事实被多套局部状态机分别实现。 |
| 根因 | 预处理器没有抽出统一的 lexer/cursor 层，而是把词法状态、语句识别、chunk 构造和个别语法的补丁式 helper 全部堆在一个大函数与若干局部扫描器里，导致“新增一个边界条件”通常需要同时修改多处不共享状态的实现。 |
| 影响 | 这类结构会持续放大 `import`、`delegate`、macro 参数、条件编译等边界 bug 的修复成本。任何人要支持新的 module 语法、补 `#include`、或修一个注释/字符串相关问题，都必须先理解一条跨 1000+ 行的共享状态机；这会显著提高回归风险，也让针对预处理器的性能优化和增量化改造难以安全推进。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先抽出统一的字符游标与平衡扫描 helper，再把 `ParseIntoChunks()` 拆成按语法职责分离的小 handler；仅对简单、无嵌套的顶层关键字入口使用 regex/table-driven 匹配。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 新增 `FPreprocessorCursor` 或等价 helper，统一维护当前位置、行号、`bInString`、comment 状态、scope depth，并提供 `PeekKeyword`、`ConsumeBalancedRegion`、`ConsumeUntilTerminator` 等 API。2. 把 `ParseIntoChunks()` 分裂为 `TryHandleConditionalDirective`、`TryHandleRestriction`、`TryHandleTopLevelDeclaration`、`TryHandleReflectionMacro`、`TryHandleImportOrDelegate`、`TryHandleNamespace` 等小函数，每个函数只负责一个语法簇并返回“是否消费成功”。3. 用共享 helper 取代 `FindScopeCloseBracket()`、`FindSemicolonDirectlyAfter()` 和散落的 `while (ModuleEnd < ... && RawCode[ModuleEnd] != ';')` 逻辑，保证括号、字符串、注释、inline comment 的处理规则只有一份。4. 对 `class/struct/interface/import/default/event/delegate/enum/namespace` 这类平面关键字入口，改成表驱动 dispatch 或编译期 regex 匹配，消除 19 处裸 `Strncmp` 链；但不要再对带嵌套结构的 macro 参数做整串 regex 替换。5. 在重构前先补足回归测试，锁住当前已经确认的边界：`import` 缺失分号、delegate 后注释、metadata 内 `)`、inactive branch 内 `#if`。6. 重构后复跑这些测试，再补一组 profiling，确认拆分没有引入明显额外拷贝或扫描回退。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | L |
| 风险 | 大函数拆分会移动大量代码，若没有先锁住回归集，极易把现有语义悄悄改坏；需要坚持“小步拆分 + 每步验证”的节奏，避免一次性重写。 |
| 前置依赖 | 建议先把已有高风险边界缺陷补成自动化测试，再开始结构性重构。 |
| 验证方式 | 1. 新增并跑通针对 `import`、`delegate`、macro 参数、条件编译的回归测试。2. 对重构前后的同一脚本集比较 `ProcessedCode`、`ImportedModules`、`PostInitFunctions` 与诊断输出，确认语义等价。3. 统计预处理阶段耗时，确认拆分后没有明显回退。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-1 | Defect | 下个迭代优先修复，先封住文件发现阶段的无限递归入口 |
| P1 | Issue-2 | Architecture | 与 Issue-1 并行做设计，先补显式 context/hash 合同 |
| P2 | Issue-3 | Refactoring | 在回归测试补齐后分阶段拆分 `ParseIntoChunks()` |

---

## 发现与方案 (2026-04-08 12:35)

### Issue-4：`import` 缺少独立 resolver，路径规范化与循环检测都夹在带副作用的排序流程里

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 101-108，163-167；86-89，439-497，3497-3510；3029-3055 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中关于 `import` canonicalization、大小写敏感和循环 import 错误恢复的发现，这里补齐落地方案。已验证事实：文件路径进入模块系统时会先经过 `FilenameToModuleName()` 规范成 `Foo.Bar` 形式，但 `ParseIntoChunks()` 解析 `import` 时只是把源码原文 `TrimStartAndEnd()` 后写进 `FImport.ModuleName`，`ProcessImports()` 再用这个原始字符串做全局精确匹配；整个过程中没有任何基于当前文件目录的相对路径解析，也没有复用 `GetModuleByFilename()` 已存在的 filename-style 规范化逻辑。与此同时，循环检测和拓扑排序共用同一趟 `ProcessImports()`，它在递归返回后无论是否已经 `bHasError`，都会继续向 `ImportedModules` 写入并对 chunk 执行 `ReplaceWithBlank()`。 |
| 根因 | `import` 解析没有被建模为“先规范化、再解析依赖图、最后提交副作用”的独立阶段，而是把路径解释、模块查找、循环检测和源码改写耦合在一个 DFS 里。 |
| 影响 | 相对路径、filename-style 写法、大小写不一致写法都无法稳定映射到同一物理模块；循环 import 即使被检测到，也会在失败路径里留下被部分改写的 `ImportedModules` 与 chunk 内容。结果是 import 路径边界 bug 和循环依赖恢复问题会持续互相放大。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 抽出显式 `ImportResolver`，先做 canonicalization 和图解析，再一次性提交 `ImportedModules` 与源码替换。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.h/.cpp` 新增 `FResolvedImport` 和 `ResolveImportName(const FFile&, const FImport&)`，统一处理注释剥离、`\`/`/` 归一、`.as` 去扩展名、`./`/`../` 相对路径折叠，以及最终 `.` 分隔 module name 生成。2. 在 `AddFile()` 或 `Preprocess()` 开始阶段预构建 `ModuleIndex`：同时保存“显示名 -> 模块”和“lowercase canonical name -> 模块”两个索引，若多个文件规范化后落到同一 key，直接报冲突错误而不是继续编译。3. 重写 `ProcessImports()` 为两阶段接口：第一阶段只返回 `ResolvedImportGraph` 与 `ECycleCheckResult`，`FImportChain` 改为保存 `FFile* + FImport*`，让循环诊断落到具体 import 行；第二阶段仅在图解析成功后，才把解析后的 canonical module name 写入 `File.Module->ImportedModules` 并 blank 掉原始 import 文本。4. 为 `GetModuleByModuleName()` 增加可选的 canonical lookup helper，统一手写 `import`、hot reload 和 runtime diagnostics 的模块名解释规则。5. 在测试里补齐 `import Foo/Bar.as;`、`import ./Shared.as;`、`import ../Common/Types.as;`、`import gameplay.foo;`、`A -> B -> A` 等场景，并额外断言循环失败时 `Preprocessor.Files[*].Module->ImportedModules` 不会残留半成品状态。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | L |
| 风险 | 统一 canonicalization 后，历史上依赖非规范写法的脚本会开始在预处理阶段报错；需要先把错误消息做成“指出原始 import 文本 + 规范结果”的可迁移诊断。 |
| 前置依赖 | 建议先完成 `Issue-3` 中的 cursor/helper 拆分底座，避免在 1000+ 行状态机里继续堆新分支。 |
| 验证方式 | 1. 自动化测试覆盖相对路径、filename-style、大小写不一致和循环 import。2. 对同一脚本分别用 canonical 与 non-canonical 写法编译，确认都落到同一 `ImportedModules` 结果。3. 构造循环 import，确认诊断包含具体文件和行号，且失败后内部状态不残留部分写入。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-4 | Defect | 在 `Issue-3` 补出共享扫描 helper 后优先落地，先统一 import 解释规则与循环失败原子性 |

---

## 发现与方案 (2026-04-08 12:43)

### Issue-5：`#include` 既没有 fail-fast，也没有进入模块哈希、cache 和 hot reload 依赖图

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Reference/angelscript-v2.38.0/sdk/samples/include/bin/script.as`；`Reference/angelscript-v2.38.0/sdk/samples/include/bin/scriptinclude.as` |
| 行号 | 212-307，3256-3392；4262-4290；1-9；1-10 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中关于 `#include` 缺口的发现，这里补齐执行方案。已验证事实：`ParseIntoChunks()` 只识别 `#if/#ifdef/#ifndef/#elif/#else/#endif` 与 `#restrict usage ...`，没有任何 `#include` 分支；`Preprocess()` 最终只把当前文件的 `ProcessedCode` 折叠成 `Module->Code` 并生成 `CodeHash`；编译阶段 `CombinedDependencyHash` 也只把 `ImportedModules` 混入签名。仓库自带的 AngelScript reference sample 则明确展示了 `#include "scriptinclude.as"` 以及循环 include “只应展开一次”的预期。换句话说，当前插件既没有把 `#include` 当成受支持特性，也没有在遇到它时显式拒绝。 |
| 根因 | 当前模块系统把跨文件依赖完全建模成 module-level `import`，预处理合同中不存在“一个模块由多个源码 section 组成”的 include graph 抽象。 |
| 影响 | `#include` 语句目前会以未建模状态流向后续编译路径，既不会进入 `ImportedModules` / `CombinedDependencyHash` / precompiled cache，也不会在 hot reload 时触发依赖失效。结果不是稳定的“不支持”，而是静默漏跟踪。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 分两阶段落地：先把裸 `#include` 变成明确诊断，随后引入 include graph，把被包含文件纳入 section、hash 和热重载依赖模型。 |
| 具体步骤 | 1. 在 `ParseIntoChunks()` 或新建的 directive scanner 中优先识别 `#include`；在完整 include graph 落地前，先对所有 `#include` 发出 hard error，避免当前静默漏跟踪继续进入编译阶段。2. 新增 `FIncludeDesc` / `FResolvedInclude`，只接受字符串字面量 include，按“当前文件目录 -> script root fallback”解析相对路径，并对物理路径执行 canonicalization。3. 为每个 root module 构建 `IncludedFiles` 有向图和 `ExpandedSections` 列表；匹配 upstream sample 的“每个 include 文件只展开一次”语义，同时在解析栈上检测循环 include 并报告文件/行号链。4. 把展开后的 include 文件作为额外 `FCodeSection` 写入同一个 `FAngelscriptModuleDesc`，并新增 `IncludeDependencyHash` 或直接把每个 included section 的 hash 混入 `Module->CodeHash`，让 cache 与 `CombinedDependencyHash` 感知 include 变化。5. 扩展 hot reload：维护 `IncludedFile -> OwningModule` 反向索引，当 include 文件修改时，能够把拥有该 include graph 的模块加入重编译集合。6. 更新 `FAngelscriptPrecompiledModule` 序列化格式，保存 `IncludedFiles`/section provenance，保证 fully precompiled 路径不会再次丢失 include 依赖。7. 补充测试：相对 include、重复 include 只展开一次、循环 include 诊断、include 文件改动触发 owner module cache miss / hot reload。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | XL |
| 风险 | 一旦把 `#include` 从“静默透传”改成“显式诊断”或“真实展开”，现有依赖未声明的脚本会开始报错或改变编译顺序；需要先做 feature flag 或迁移窗口，避免一次性破坏项目脚本。 |
| 前置依赖 | 建议先完成 `Issue-3` 的 directive scanner 拆分，以及 `Issue-4` 的 canonical path 底座，减少 include/import 两套路径规则再次分叉。 |
| 验证方式 | 1. 自动化验证裸 `#include` 至少会稳定报错，不再静默穿透。2. include graph 版本下，修改被包含文件后 owner module 的 `CodeHash` / cache 命中状态必须变化。3. 复现 upstream sample 的循环 include，确认每个文件只展开一次且诊断可读。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-5 | Architecture | 在 import 规则统一后推进，先用 fail-fast 封住静默漏跟踪，再分阶段落地 include graph |

---

## 发现与方案 (2026-04-08 12:51)

### Issue-6：异步脚本读取把请求生命周期绑在栈上，hot reload 删除路径会放大成句柄泄漏和悬挂回调

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 行号 | 120-160；91-139，141-230；2253-2265，2448-2455；14-61 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中关于异步读取、删除文件 hot reload 和失败路径 use-after-free 的发现，这里补齐执行方案。已验证事实：`FFile` 直接持有 `AsyncReadHandle` / `AsyncSizeRequest` / `AsyncReadRequest` 裸指针；`PerformAsynchronousLoads()` 发起请求后只对仍在加载的文件 `Sleep(0.001f)` 一次就返回，未完成的请求既不会在该函数内等待，也不会进入统一 cleanup；callback 继续捕获并写回 `FFile&`。与此同时，`PerformHotReload()` 使用栈上的 `FAngelscriptPreprocessor Preprocessor;`，且删除文件路径仍通过 `Preprocessor.AddFile(PathPair.RelativePath, PathPair.AbsolutePath, bTreatAsDeleted);` 误把 `bTreatAsDeleted` 传进了 `bLoadAsynchronous` 位置参数。现有 hot reload 自动化也只覆盖“源码内容变更”，没有删除/读失败场景。 |
| 根因 | 预处理器没有独立的异步请求 owner，也没有“所有请求完成后才能离开作用域”的生命周期合同；删除语义又被位置参数错误路由进了这条未完成的 async 分支。 |
| 影响 | 这条路径会同时产生三类风险：1. 未完成请求在函数返回后继续持有并回写已失效的 `FFile/FString`；2. 仍处于加载中的 `IAsyncReadRequest` / `IAsyncReadFileHandle` 没有稳定释放点；3. 删除文件这种本该走空源码语义的路径，被放大成随机崩溃、堆破坏或后台句柄悬挂。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先用同步语义止血，彻底切断 hot reload 删除路径进入 async 分支；只有在建立独立 request owner 和 completion wait 之后，才允许重新引入异步读取。 |
| 具体步骤 | 1. 立即修正 `PerformHotReload()` 的调用点，改为 `Preprocessor.AddFile(PathPair.RelativePath, PathPair.AbsolutePath, false, bTreatAsDeleted)`，保证删除文件直接走空源码语义。2. 在 `FAngelscriptPreprocessor` 层增加短期护栏：默认关闭 `bLoadAsynchronous` 路径，或在没有显式 owner/context 的情况下直接退回同步 `LoadFileToString`，把当前未完成的 async 设计从生产路径摘掉。3. 如果后续确实需要 async，新增 `FScriptLoadRequestState`（`TSharedRef` 或等价 RAII owner），统一持有 `IAsyncReadFileHandle`、`IAsyncReadRequest`、buffer、completion event 和取消逻辑；callback 只捕获 shared state，不再捕获 `FFile&` 或栈上的 lambda 对象。4. 让 `PerformAsynchronousLoads()` 显式等待 outstanding request 归零，再把结果写回 `FFile.RawCode`；无论成功、失败还是取消，都通过单一路径释放 request/handle。5. 为 `FAngelscriptPreprocessor` 增加析构或 `CancelOutstandingLoads()` 清理，确保调用方提前失败返回时不会遗留后台请求。6. 新增 hot reload 测试：删除脚本文件、读取失败重试、和 async handle 返回空指针三种场景都必须稳定完成而不是崩溃。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestEngineHelper.cpp` |
| 预估工作量 | M |
| 风险 | 若项目已经依赖“预处理可异步返回”的非正式行为，切回同步会改变 reload 时序；但当前 async 语义本身并不安全，这个时序变化应作为可接受的止血成本。 |
| 前置依赖 | 无；这是最适合作为短期 hotfix 的问题，建议先于更大规模的 import/include 架构改造执行。 |
| 验证方式 | 1. 自动化复现删除脚本文件的 hot reload，确认 `Preprocess()` 返回稳定、无 crash、无挂起句柄。2. 在测试替身里让 `OpenAsyncRead()` 或 `SizeRequest()` 失败，确认 cleanup 路径会释放资源并给出明确诊断。3. 运行现有 `AngelscriptNativeScriptHotReload` 用例回归，确认同步止血没有破坏普通源码变更热重载。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-6 | Defect | 立即修复，先切断删除路径误入 async，并为未完成请求补齐 owner/cleanup 合同 |

---

## 发现与方案 (2026-04-08 12:42)

### Issue-7：模块依赖签名合同不一致，`import` 的顺序/重复语义在预处理、编译、缓存和 StaticJIT 之间失配

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp` |
| 行号 | 439-497；3175-3208，4247-4350；2713；1617-1667 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 1、60、61、62，这里补齐统一方案。已验证事实：`ProcessImports()` 会按源码顺序把每条 `import` 直接追加进 `Module->ImportedModules`；`CompileModules()` 与 `CompileModule_Types_Stage1()` 也按这个顺序导入模块。AngelScript `asCModule::ImportModule()` 会保留 first-seen 顺序，并按该顺序查找 imported module 里的类型。与此相对，当前依赖签名只做 `CodeHash ^ ImportedModule->CombinedDependencyHash`，天然丢失顺序与重复次数；precompiled cache 命中路径又在 `ApplyToModule_Stage1()` 后直接 `return`，不会执行后面的 `SetUserData((void*)(size_t)Module->CombinedDependencyHash, 0)`；`CreateFunctionId()` 最终还只取 `GetUserData()` 的低 32 位。也就是说，实际可观察的 import 语义和用于 cache/JIT 的模块签名不是同一份合同。 |
| 根因 | 模块系统没有一个单一来源的“dependency signature”抽象。预处理层存的是有序文本列表，编译层执行的是 first-match 导入语义，缓存层却把依赖压成无序 XOR，StaticJIT 再把它截断成 32 位并且在 cache-hit 分支可能完全丢失。 |
| 影响 | 只调整 `import` 顺序、删除重复 `import`、或切换到 precompiled cache 命中路径，都可能改变真实符号解析结果，却不改变或不完整改变依赖签名。结果是 `CombinedDependencyHash`、function ID、StaticJIT 生成物和 precompiled cache 命中条件会与真实模块依赖语义漂移，形成只在特定缓存路径下出现的隐蔽一致性错误。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 建立统一的 `DependencySignature` 合同，把 `import` 的 canonical 名称、去重规则、顺序语义和完整位宽一次性贯穿预处理、编译、cache 与 StaticJIT。 |
| 具体步骤 | 1. 在 `FAngelscriptModuleDesc` 中新增显式依赖签名结构，例如 `FModuleDependencySignature`，至少保存 canonical import 名列表、首次出现顺序和用于 cache 的 64-bit/128-bit signature。2. 在 `ProcessImports()` 结束后立即做“保留首次出现顺序的去重”，把 `ImportedModules` 从“原始语句列表”升级成“编译语义列表”，避免重复 `import` 继续污染后续哈希。3. 用顺序敏感的 hash 取代当前 XOR：按 canonical import 名和每个依赖模块的完整 `CombinedDependencyHash` 顺序 `HashCombine`，保证 `A,B` 与 `B,A`、`A` 与 `A,A` 得到不同签名。4. 提取 `ApplyModuleDependencySignature(asCModule*, const FAngelscriptModuleDesc&)` 之类的公共 helper，在 precompiled cache hit 和正常编译两条路径都写入同一份签名，消除早退分支漏 `SetUserData()` 的差异。5. 将 `CreateFunctionId()` 从 `(uint32)(size_t)ScriptModule->GetUserData()` 改为消费完整依赖签名；若仍需通过 `UserData` 透传，可改成独立 `FModuleSignatureRegistry` 或把 64 位拆成高低位分别混入，避免高 32 位永久丢失。6. 更新 `FAngelscriptPrecompiledModule` 序列化内容，把新的 dependency signature 一并写入并在命中 cache 时校验，确保 cache 复用条件不再只看 `CodeHash`。7. 补充回归测试：验证 import 顺序变化会改变 signature；重复 import 不会再让 hash 抵消；cache-hit 与正常编译得到相同 function ID；命中 precompiled cache 的模块也能写回完整依赖签名。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptDebugProtocolTests.cpp` |
| 预估工作量 | L |
| 风险 | 依赖签名一旦变得更严格，现有 precompiled 数据和 StaticJIT 缓存都会整体失效，需要准备一次性 cache bump 或版本升级。 |
| 前置依赖 | 建议先落地 `Issue-4` 的 import canonicalization，确保签名使用的模块名已经稳定。 |
| 验证方式 | 1. 新增自动化比较 `import A; import B;` 与 `import B; import A;` 的 signature 与 function ID，确认两者不同。2. 对 `import A; import A;` 场景断言 `ImportedModules` 去重后只保留一份，且依赖签名稳定。3. 分别走正常编译和 precompiled cache 命中路径，断言 `ScriptModule` 上记录的 dependency signature 一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-7 | Architecture | 在 `Issue-4` 统一 import canonicalization 后实施，优先修正依赖签名与 cache/JIT 合同 |

---

## 发现与方案 (2026-04-08 12:43)

### Issue-8：默认 `automatic import` 模式下，手写 `import` 的兼容清理路径完全失效

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 63-67；232-239，278-307，439-490，3490-3510，3983-3994；3173-3188 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 2，这里补齐执行方案。已验证事实：配置默认启用 `bAutomaticImports = true` 且同时暴露 `bWarnOnManualImportStatements`；预处理器虽然在 `ParseIntoChunks()` 里仍会识别并记录 `import` 语句，但 `Preprocess()` 只有在 automatic-import 关闭时才调用 `ProcessImports()`。而“写入 `ImportedModules`、把原始 `import` 文本 `ReplaceWithBlank()`、以及按配置发出 `Automatic imports are active, import statements will be ignored.` warning”的逻辑全部都在 `ProcessImports()` 里。随后 `CondenseFromChunks()` 会直接把尚未清理的 chunk 合并进 `ProcessedCode`，并作为 `Section.Code` 送入编译阶段；编译阶段在 automatic-import 模式下又完全不会再读取 `Module->ImportedModules`。也就是说，默认配置下兼容路径既不发 warning，也不真正执行“ignored”的语义。 |
| 根因 | 代码把“解析/剥离语法级 `import` 语句”和“按显式 import 语义做依赖排序”错误地绑定在同一个 `ProcessImports()` 阶段里，导致 automatic-import 模式直接绕过了整个兼容处理。 |
| 影响 | 旧脚本里残留的手写 `import` 在默认配置下会原样流入后续编译路径，`bWarnOnManualImportStatements` 退化成死配置，项目迁移到 automatic-import 时也拿不到稳定的提示和自动清理结果。该缺陷不是边缘配置，而是默认配置就会踩到的兼容性断层。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“识别并清理 module-level `import` 语句”从拓扑排序里拆出来，保证两种 import 模式都走同一条语法归一化路径。 |
| 具体步骤 | 1. 在 `Preprocess()` 中拆分出独立阶段，例如 `NormalizeImportStatements()`：它始终遍历 `File.Imports`，负责 canonicalize 文本、记录 warning、并把原始 `import` 语句从 chunk 中 blank 掉。2. 仅在显式 import 模式下，再在 `NormalizeImportStatements()` 之后调用新的 `ResolveExplicitImports()` 做依赖图排序和循环检测；automatic-import 模式则只做“warning + blank + 丢弃 dependency 元数据”。3. 把 `ProcessImports()` 现有逻辑拆成两部分：`CollectOrWarnImport()` 负责模式无关的语法清理，`ResolveExplicitImports()` 只负责 `ImportedModules` 填充和排序，避免再让 warning 路径藏在不可达分支里。4. 给 `FImport` 增加一个明确状态，例如 `bShouldIgnoreUnderAutomaticImports` 或直接记录 canonical module name，保证两种模式都能共享同一份 parser 结果，而不是再次在 `ProcessImports()` 内重新解释文本。5. 补充自动化用例：在 `SetAutomaticImportMethodForTesting(true)` 下喂入包含手写 `import` 的脚本，断言 `ProcessedCode` 中已不再保留原始 `import` 文本、warning 行为与配置一致；在 `false` 模式下断言依赖排序与 `ImportedModules` 仍然正确。6. 若担心迁移期破坏旧脚本，可先通过设置项提供 `Error/Warn/IgnoreAndStrip` 三态策略，但无论选择哪一档，都必须保证原始 module-level `import` 不再未建模地穿透到编译阶段。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果项目中已有脚本意外依赖“原始 `import` 文本继续流入编译器”的现状，修复后会改成预处理期 warning 或 error；需要提前给出迁移说明。 |
| 前置依赖 | 无；这是默认配置缺陷，适合在更大规模 import 重构前先独立修复。 |
| 验证方式 | 1. automatic-import 开启时，断言 `ProcessedCode` 不再包含 `import Foo.Bar;` 原文。2. `bWarnOnManualImportStatements` 开关分别验证 warning 有无。3. 显式 import 关闭 automatic-import 后，回归已有 import 排序与循环检测测试，确保拆分没有破坏旧语义。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-8 | Defect | 可在 import 大重构前先独立修复，先恢复默认 automatic-import 配置下的兼容行为 |

---

## 发现与方案 (2026-04-08 12:44)

### Issue-9：显式 `import` 排序通过复制整份 `FFile` 完成，放大预处理成本并继续耦合生命周期状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 120-160；235-238，439-497 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 5，这里补齐方案。已验证事实：显式 import 模式下，`Preprocess()` 会先创建 `TArray<FFile> SortedFiles`，再把每个节点交给 `ProcessImports()`，最后执行 `Files = SortedFiles`。而 `ProcessImports()` 完成一个节点时直接 `OutSortedFiles.Add(File)`，也就是按值复制整个 `FFile`。`FFile` 本身承载 `RawCode`、`ChunkedCode`、`ProcessedCode`、`GeneratedCode`、`Imports`、`Delegates`，以及异步读取句柄指针等完整生命周期状态。这意味着 import 拓扑排序不是重排轻量句柄，而是在复制整份预处理上下文。 |
| 根因 | 当前数据模型把“文件内容与处理中间状态”和“依赖图节点身份”揉进同一个值类型 `FFile`，排序层没有独立的索引/句柄表示，导致拓扑排序只能通过对象拷贝来重排。 |
| 影响 | 预处理阶段会在真正进入后续 class/macro 处理前先复制大对象，直接增加 CPU 和内存峰值；同时 async 句柄、`bImportsResolved`、`bIsResolvingImports` 这类生命周期字段也跟着被复制，使 import 解析和文件资源状态继续耦合，不利于后续修复 `Issue-6` 的异步 owner、`Issue-4` 的 resolver 拆分，以及未来的增量预处理缓存。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 import 图排序只重排“节点引用”，不再复制 `FFile` 的完整内容与资源句柄。 |
| 具体步骤 | 1. 把 `ProcessImports()` 的输出从 `TArray<FFile>` 改成 `TArray<int32>`、`TArray<FFile*>` 或独立的 `FModuleNodeRef`，仅保存拓扑顺序所需的节点引用。2. 在 `FAngelscriptPreprocessor` 内引入稳定的文件存储容器，排序结束后只按索引重建视图，或维护单独的 `SortedFileIndices` 供后续阶段遍历，避免 `Files = SortedFiles` 这种整对象覆盖。3. 将 `bImportsResolved`、`bIsResolvingImports` 移出 `FFile` 或集中到 resolver 的临时状态结构里，确保拓扑排序不再依赖/复制生命周期位。4. 为异步读取句柄建立独立 owner 后，禁止它们出现在会参与复制/排序的结构体中，彻底切断“排序 = 复制裸指针”的风险。5. 若后续要落地增量预处理，可直接把 `SortedFileIndices` 作为 dependency DAG 的稳定缓存键，避免再次把 `FFile` 作为值类型在多个阶段来回复制。6. 补充 profiling 与回归：比较改造前后在大脚本集上的预处理时间、峰值内存和 `Files.Num()` 一致性，确认排序后模块顺序不变但对象复制次数下降。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 若外部 hook 依赖 `Files` 当前的物理重排顺序，需要在 API 变更时同步说明是“视图顺序”变化还是“底层存储”变化，避免 trace 工具读取到未预期的数据布局。 |
| 前置依赖 | 建议与 `Issue-6` 的异步请求 owner 改造协同推进，避免先把排序改轻后又把句柄塞回可复制结构。 |
| 验证方式 | 1. 针对多文件显式 import 图，断言排序后的编译顺序与现状一致。2. 用 instrumentation 或 profiling 统计 `FFile` 拷贝次数/峰值内存，确认明显下降。3. 回归 async 与循环 import 测试，确认改成引用排序后不会引入悬挂引用。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-9 | Refactoring | 在 `Issue-6` 和 `Issue-4` 的底座稳定后推进，先把 import 排序从整对象复制改成轻量引用 |

---

## 发现与方案 (2026-04-08 12:48)

### Issue-10：默认 `automatic import` 模式会跳过 declared function import 的校验与绑定

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` |
| 行号 | 61-67；3497-3510；3865-3868，4057-4063，4611-4643，4646-4723；157-177 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 19，这里补齐执行方案。已验证事实：配置默认启用 `bAutomaticImports = true`；`ParseIntoChunks()` 在识别 `import` 后，只把“不包含 `(`”的语句写入 `File.Imports`，等价于直接忽略 `import int SharedValue() from "BuilderImportSource";` 这类 declared function import；编译后用于校验与绑定 imported function 的 `CheckFunctionImportsForNewModules()`、`ResolveAllDeclaredImports()` 又都被 `if (!ShouldUseAutomaticImportMethod())` 包住。测试 `AngelscriptBuilderTests.cpp` 也证明 builder 路径下 imported function 需要显式 `BindImportedFunction()` 才能变成可调用函数。也就是说，默认 automatic-import 配置会把 declared function import 留在 AngelScript module 的“未绑定占位”状态，而引擎侧没有任何后续阶段负责收口。 |
| 根因 | 当前实现把“模块级 automatic import 策略”和“语言层 declared function import 生命周期”错误地绑定到同一个开关上，导致模块依赖自动发现一旦启用，function import 的校验、诊断与绑定链路也被一并关闭。 |
| 影响 | 默认配置下，脚本里合法的 declared function import 可能在编译期不报错，但运行前也不会被绑定成真实函数；一旦脚本调用这类导入函数，故障会滞后到更晚的执行路径。与此同时，hot reload 后的 `ResolveAllDeclaredImports()` 也不会运行，模块系统无法在 source module 变更后重新绑定 imported function。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 declared function import 从 automatic/module import 开关中解耦，建立独立的校验与绑定阶段。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中引入独立判断，例如 `ShouldResolveDeclaredFunctionImports()`，默认始终为 true，除非明确关闭该语言特性；`CheckFunctionImportsForNewModules()` 与 `ResolveAllDeclaredImports()` 改为依赖这个独立开关，而不是 `ShouldUseAutomaticImportMethod()`。2. 在 `FAngelscriptPreprocessor` 侧补一个只负责“记录 declared function import 存在性”的轻量元数据结构，哪怕 automatic-import 开启，也要让后续编译/热重载阶段知道哪些模块依赖 function import 重新绑定；如果不想在预处理器里新增结构，至少要在计划文档和代码注释里明确“declared function import 仍是受支持语言特性，不能被 automatic module import 绕过”。3. 在编译完成后的统一收口点始终执行 function import 校验：先跑 `CheckFunctionImportsForNewModules()` 生成明确诊断，再在允许 swap-in 后执行 `ResolveAllDeclaredImports()`，确保新旧模块切换后 imported function 被重新绑定。4. 为 hot reload 增加 function-import dependency 追踪：当 source module 重新编译成功时，依赖它的 consumer module 至少要进入“重新解析 imported function”集合，避免只替换模块对象却不刷新函数绑定。5. 扩展自动化测试，分别覆盖 `automatic import = true/false` 两种配置下的 declared function import 编译、绑定和 hot reload 复绑行为；断言 `GetImportedFunctionCount()` 不为 0 时，编译完成后对应 import 已经被成功 `BindImportedFunction()`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果有项目实际把 automatic-import 当成“顺便禁用 declared function import”的非正式行为，修复后会改变其编译/运行期表现；需要用测试先锁定当前合法脚本的预期，再决定是否保留兼容开关。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 在 `automatic import = true` 下编译包含 declared function import 的 consumer/source 模块，断言编译完成后 imported function 已绑定且 `Entry()` 可调用。2. 在 source module hot reload 后再次调用 `ResolveAllDeclaredImports()` 路径，确认 consumer module 的 imported function 重新指向新模块实现。3. 构造“源模块缺失”与“函数签名不匹配”两种失败场景，确认错误在编译/热重载阶段就被稳定诊断，而不是拖到运行时。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P0 | Issue-10 | Defect | 立即修复，先把 declared function import 的绑定链路从 automatic module import 开关里解耦 |

---

## 发现与方案 (2026-04-08 12:50)

### Issue-11：缺失分号的 `import` 会吞并后续源码并退化成误导性的 “module not found”

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1377-1386，3497-3510；3192-3195 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 12，这里补齐执行方案。已验证事实：`ParseIntoChunks()` 解析 `import` 时，只做“从 `import` 后面一直扫到下一个 `;`”的线性搜索，没有任何“当前语句必须在本 statement 结束前看到 `;`”的语法校验；扫描到的整段文本直接写入 `ImportDesc.ModuleName`。随后 `ProcessImports()` / 编译阶段会把这个脏模块名原样带到 `Could not compile module %s: could not find module %s to import.` 诊断里。与此同时，源码清理函数 `ReplaceWithBlank()` 在传入范围超出 chunk 边界时会直接 `return`，没有补充错误。结果是 `import Foo.Bar` 这类常见笔误不会在预处理期得到“缺少分号”的明确诊断，而是把后续源码拼成一个巨大的模块名并在后面阶段报错。 |
| 根因 | `import` 语句的 parser 没有单独的 terminator 合同，也没有把“抽取模块名”和“验证语句边界”拆成两个步骤；错误恢复仍假定 `import` 的替换范围总是合法。 |
| 影响 | 一个简单的语法错误会被扭曲成模块解析失败，直接误导排障方向；更糟的是，被吞进去的后续源码会污染 `ImportedModules` 和错误消息文本，导致用户既看不到真实的缺分号位置，也很难判断哪一段源码已经被预处理器错误消费。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `import` 增加显式 statement parser，先验证 terminator，再提交模块名与源码替换。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp` 为 `import` 抽出 `ParseImportStatement()`，它必须返回 `EImportParseResult` 之类的显式结果：`Success`、`MissingSemicolon`、`MalformedImport`。2. 解析时不要再用“扫到下一个任意 `;`”的裸循环；改成基于共享 cursor/helper 的 `ConsumeUntilTerminator(';')`，并在遇到换行、chunk 边界、另一个顶层关键字或文件结束却仍未看到 `;` 时立刻 `LineError(File, LineNumber, TEXT("Import statement is missing terminating ';'."))`。3. 只有 `Success` 分支才允许写入 `File.Imports`、记录 `StartPosInChunk/EndPosInChunk`，并在后续 `ProcessImports()` 中 blank 原文；失败分支必须保证不向 `ImportedModules` 写入半成品状态。4. 将 `ReplaceWithBlank()` 的 silent return 改成“开发期 ensure + 预处理错误”，至少在 import/debug 构建中暴露出越界替换，而不是吞掉异常范围。5. 新增测试覆盖 `import Foo.Bar`、`import Foo.Bar // comment`、`import Foo.Bar\nclass X {}` 等 malformed 场景，断言错误消息明确指出缺少分号且 `ProcessedCode` / `ImportedModules` 不残留被污染的模块名。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 更严格的 parser 会把过去被“module not found”兜底吞掉的脚本改成预处理期 hard error；这是期望中的行为变化，但需要在迁移说明里明确。 |
| 前置依赖 | 建议与 `Issue-3` 的共享 cursor/helper 拆分一起做，避免再增加一套独立扫描逻辑。 |
| 验证方式 | 1. 自动化断言缺失分号时错误定位在原始 `import` 行，而不是模块第 1 行。2. 断言 malformed import 失败后 `File.Imports` 为空或不包含脏模块名，`ProcessedCode` 不会吞掉后续 class/function 定义。3. 回归正常 `import Foo.Bar;`、带相对路径 import 和循环 import，确认新 parser 不破坏已存在语义。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-11 | Defect | 在 `Issue-4` 的 import resolver 底座上优先修复，先把语法错误从模块查找阶段前移到预处理阶段 |

---

## 发现与方案 (2026-04-08 12:53)

### Issue-12：hot reload 文件账本与模块查找的大小写语义不一致，会在大小写不敏感文件系统上拆裂同一物理脚本

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 408，416-419，1426-1434；2298-2328，2392-2415，3029-3052 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 27，这里补齐执行方案。已验证事实：`FileHotReloadState`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles` 都直接以原始 `FString` / `FFilenamePair` 作为 key，`FFilenamePair` 的 hash 与 equality 也是大小写敏感的精确字符串比较；dependency check 阶段构建的 `RelativeFileToModule` 也按 `Section.RelativeFilename` 精确建表并用 `File.RelativePath` 精确查找。相比之下，`GetModuleByFilename()` 却明确用 `Section.AbsoluteFilename.Equals(..., IgnoreCase)`，并在 fallback 时调用 `MakePathRelativeTo_IgnoreCase()`。也就是说，同一份运行时代码对“文件身份”同时存在大小写敏感和大小写不敏感两套规则。 |
| 根因 | 模块系统没有统一的 canonical path 层；预处理/热重载账本保留了原始路径字符串，而按文件回查模块时又临时切换成 ignore-case 语义，导致 key 合同前后不一致。 |
| 影响 | 在 Windows 这类大小写不敏感文件系统上，只要文件变更通知、历史缓存或 `Module->Code` 里记录的路径 casing 不一致，同一物理脚本就可能被拆成两个 key。结果包括：dependency 传播找不到 owner module、删除/失败重试集合命中失败、相同脚本被重复加入 reload 集合，或某些文件变化根本不触发预期模块重编译。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为脚本路径建立统一 canonical key，并让 hot reload、预处理和模块查找全部共享同一套比较规则。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 或共享文件系统 helper 中新增 `FCanonicalScriptPath` / `CanonicalizeScriptPathKey()`，统一执行 `ConvertRelativePathToFull`、目录折叠、分隔符归一，以及在 case-insensitive 平台上统一大小写。2. 改造 `FFilenamePair`：新增 canonical absolute/relative key 字段，`GetTypeHash` 与 `operator==` 改为基于 canonical key，而不是原始字符串。3. 构建 `RelativeFileToModule`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles` 和 `FileHotReloadState` 时全部使用 canonical key；同时保留原始路径仅用于日志和诊断展示。4. 让 `GetModuleByFilename()` 也优先复用这套 canonical key，移除临时的 `IgnoreCase` 特判与重复路径归一逻辑，避免查找与账本继续分叉。5. 若后续实施 `Issue-4` 的 import canonicalization 和 `Issue-5` 的 include graph，也统一复用同一个 canonical path helper，保证 import/include/hot reload 三条链路对文件身份的定义完全一致。6. 补充回归测试：同一脚本以不同 casing 触发文件变化事件时，只生成一个 reload job；失败重试集合能命中相同物理文件；按文件路径回查模块与 dependency check 使用的是同一 key。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦 key 规范化更严格，现有某些依赖“路径原样字符串”的调试输出或临时缓存可能会改变；需要把展示层和身份层分开，避免为了保日志原文而继续沿用脆弱 key。 |
| 前置依赖 | 无；但建议和 `Issue-1` 的 script root canonicalization 共用同一 helper，避免重复实现。 |
| 验证方式 | 1. 在测试中用同一文件的不同 casing 构造两次 hot reload 事件，断言只匹配到一个模块且不会重复排队。2. 构造 compile failure 后再用不同 casing 重试，断言 `PreviouslyFailedReloadFiles` 能正确命中并清除。3. 对真实工程执行一次文件变更 trace，确认 dependency 传播、删除检测和 `GetModuleByFilename()` 返回的是同一模块。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-12 | Defect | 与 `Issue-1`、`Issue-4` 一起推进，先统一 script path canonical key，再修上层 import/reload 行为 |

---

## 发现与方案 (2026-04-08 12:55)

### Issue-13：重复 `import` 会把同一依赖重复写入模块图，并抵消 `CombinedDependencyHash`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 482；3175-3188，4280 |
| 问题 | 已验证事实：`ProcessImports()` 对每条手写 `import` 都直接执行 `File.Module->ImportedModules.Add(ImportDesc.ModuleName)`，没有任何去重或 canonical merge。编译阶段又逐项遍历 `Module->ImportedModules`，把找到的 provider 追加到 `ImportedModules`，并在 `CompileModule_Types_Stage1()` 里对每个 imported module 执行一次 `Module->CombinedDependencyHash ^= ImportModule->CombinedDependencyHash`。这意味着脚本若重复写两次相同 `import Foo.Bar;`，同一 provider 会被导入两次，依赖哈希也会被异或两次后抵消。 |
| 根因 | 预处理器把 `ImportedModules` 当成保序列表而不是依赖集合；编译阶段又直接消费这个列表，没有在 module graph 边界补去重。 |
| 影响 | 模块依赖图会出现重复边，`CombinedDependencyHash` 与真实依赖集合失真，后续 JIT / precompiled / 依赖变更检测可能把“依赖发生变化”的模块误判为未变化；同时 `ImportIntoModule()` 也会对同一 provider 重复执行，额外增加编译开销。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 import 解析完成后立刻按 canonical module key 去重，并把“源码顺序”与“依赖集合”分开建模。 |
| 具体步骤 | 1. 在 `FAngelscriptPreprocessor` 内为 `FImport` 增加 canonical key，复用 `Issue-4` 计划中的 import resolver，把不同写法但指向同一模块的 import 先归一到同一个 key。2. 在 `ProcessImports()` 提交依赖时，维护 `TSet<FString> SeenImports`；若 key 已出现，则保留第一次出现的位置用于诊断，并对重复语句发出明确 warning 或 error，但不再向 `File.Module->ImportedModules` 追加第二份。3. 将 `FAngelscriptModuleDesc::ImportedModules` 改成“唯一依赖列表”，若还需要保留源码顺序供 trace，可新增独立的 `OriginalImportStatements` 元数据而不是复用编译输入。4. 在 `CompileModules()` 前增加一层 `ensure`/验证，检测 `ImportedModules` 中是否仍有重复 key，避免未来其他入口再次把脏数据送入编译阶段。5. 补充自动化测试：同一文件重复 `import Foo.Bar;` 两次时，只导入一次 provider，`CombinedDependencyHash` 与单次 import 场景一致；对大小写或相对路径归一后命中同一模块的重复 import 也应被视为重复。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果现有项目依赖“重复 import 不报错也不告警”的宽松行为，去重后诊断会新增；需要先决定是 warning 还是 hard error，并保证不会改变合法脚本的导入顺序语义。 |
| 前置依赖 | 建议与 `Issue-4` 的 canonical import resolver 一起推进，避免先按原始字符串去重后又在 resolver 阶段引入第二套 key。 |
| 验证方式 | 1. 新增重复 import 回归测试，断言 `ImportedModules` 中同一模块只出现一次。2. 对比单次 import 与重复 import 的 `CombinedDependencyHash`，确认结果一致。3. profiling `ImportIntoModule()` 调用次数，确认重复 import 不再导致重复导入。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-13 | Defect | 接在 `Issue-4` 后修复，先保证 import resolver 产出唯一依赖集合 |

---

## 发现与方案 (2026-04-08 12:55)

### Issue-14：重复模块名冲突的 winner 不稳定，预处理阶段与激活阶段可能选中不同文件

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 1960，1971，3134-3135，2938；469 |
| 问题 | 已验证事实：脚本扫描阶段 `FindScriptFiles()` 直接按文件系统返回顺序把文件写入 `OutFilenames`，对子目录还通过 `TSet<FString>(LocalDirs)` 去重后递归，没有任何稳定排序。随后 `ProcessImports()` 在解析 `import` 时，对同名模块只取 `Files` 中第一个命中的 `OtherFile`。但进入编译和激活阶段后，`CompilingModulesByName.Add(Module->ModuleName, Module)` 与 `ActiveModules.Add(InternalModuleName, Module)` 又把同名 key 收敛成单个 map 条目，最终保留的是当前遍历顺序下的最后一次写入。也就是说，同一个 duplicate module collision，预处理期引用的是“第一个”，运行态留下的是“最后一个”，而两者都依赖不稳定的文件系统枚举顺序。 |
| 根因 | 模块系统缺少统一的 duplicate-module 合同：发现阶段不排序，预处理阶段按线性顺序挑第一个 provider，编译/激活阶段再用 map 覆盖成最后一个 provider。 |
| 影响 | 同名模块冲突不是单纯的“编译时报一个重复定义”，而是会演变成 nondeterministic provider 选择：某个 consumer 在预处理 import 图里依赖的是文件 A，最终 `ActiveModules` 里留下的却可能是文件 B。结果包括 import 行为漂移、热重载目标模块不稳定，以及同一工程在不同机器/不同枚举顺序下得到不同运行结果。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在进入预处理前就稳定文件顺序并 fail-fast 拒绝 canonical module name 冲突，禁止“先用一个、后留另一个”的双重 winner 语义。 |
| 具体步骤 | 1. 在 `FindAllScriptFilenames()` 之后，对 `FFilenamePair` 按 canonical relative path 做稳定排序，消除文件系统枚举顺序带来的 nondeterminism。2. 在 `Preprocessor.AddFile()` 或更早的发现阶段建立 `ModuleName -> SourceFile` 索引；若两个不同文件规范化后落到同一 module name，立即报编译错误，诊断里同时列出两个文件路径，而不是继续进入 `ProcessImports()`。3. 把 `ProcessImports()` 的 provider 查找从“遍历 `Files` 找第一个”改成显式 `ModuleIndex` 查询，并要求该索引只允许唯一值。4. 将 `CompilingModulesByName.Add(...)` 前的 `ensure` 升级成真正的 fail-fast 路径：一旦发现 duplicate module name，不再继续编译和 swap-in，避免 map 覆盖隐藏冲突。5. 对 hot reload 也复用同一 duplicate-module 校验，防止新增文件在 reload 轮次里悄悄覆盖旧模块。6. 新增 determinism 测试：用两个不同路径但同名模块的脚本构造冲突，断言系统稳定报错且不会因为 `AddFile()` 顺序不同而改变错误结果。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果某些项目历史上依赖“后出现的同名模块覆盖前一个”的未文档化行为，fail-fast 后会把这类脚本变成显式错误；需要在迁移说明里指出这是对 nondeterministic 行为的收敛。 |
| 前置依赖 | 建议与 `Issue-12` 的 canonical path key 一起推进，确保 duplicate 检测基于统一路径/模块名规范，而不是原始字符串。 |
| 验证方式 | 1. 人工构造两个落到同一 module name 的脚本，交换扫描顺序后仍应得到同一条 duplicate-module 错误。2. 断言一旦触发 duplicate-module 错误，`CompiledModules` 与 `ActiveModules` 都不会留下任意一方作为赢家。3. 回归普通唯一模块场景，确认排序稳定化不会改变现有 import 拓扑。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-14 | Defect | 与 `Issue-12` 并行推进，先建立唯一 module key 合同，再允许 import resolver 依赖它 |

---

## 发现与方案 (2026-04-08 12:57)

### Issue-15：`FilenameToModuleName()` 通过全局删除 `.as` 生成模块名，路径到模块名映射不是单射

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 86-88；1960-1985，3049 |
| 问题 | 已验证事实：`FilenameToModuleName()` 直接执行 `Filename.Replace(TEXT(".as"), TEXT("")).Replace(TEXT("/"), TEXT("."))`；`GetModuleByFilename()` 在按文件路径回推模块名时又重复了同一段逻辑。与此同时，`FindScriptFiles()` 会递归进入文件系统返回的所有子目录，并没有禁止目录名中出现 `.` 或 `.as` 片段。结果是模块名生成不是“去掉叶子文件扩展名”，而是把整条相对路径中的所有 `.as` 子串都删掉。像 `Foo/Bar.as` 与 `Foo.as/Bar.as` 这两条不同物理路径，最终都会被压成同一个 module name `Foo.Bar`。 |
| 根因 | 路径规范化使用了全局字符串替换，而不是“先解析路径段，再仅移除最后一个扩展名”的结构化转换；module key 的生成规则因此对目录段和文件段一视同仁。 |
| 影响 | 两个合法脚本路径可以在 module key 上发生静默碰撞，直接影响 import 查找、热重载按文件回查模块，以及 duplicate-module 诊断的准确性。更糟的是，`GetModuleByFilename()` 与 `FilenameToModuleName()` 共享同一缺陷，round-trip 查找会把这类碰撞掩盖成“查找成功”，而不是暴露出 key 已经不唯一。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 用结构化 path parser 统一生成 module key，只移除叶子 `.as` 扩展，并对非法/歧义路径给出显式诊断。 |
| 具体步骤 | 1. 提取共享 helper，例如 `BuildModuleNameFromRelativePath(const FString&)`，先统一分隔符，再按路径段拆分，只对最后一个文件名执行 `GetBaseFilename` 等价逻辑，禁止使用全局 `Replace(".as", "")`。2. 将 `FilenameToModuleName()`、`GetModuleByFilename()`、以及后续 `Issue-4/Issue-12/Issue-14` 里要新增的 import/path canonicalization 全部改为复用这同一个 helper，避免不同层再次各自实现一份规则。3. 对规范化后发生冲突的路径，直接报 duplicate-module 错误；如项目不允许目录段包含保留后缀 `.as`，则在发现阶段给出明确诊断，而不是静默把目录名改写掉。4. 顺手把 `\` 与 `/` 的处理并入同一 helper，确保 path-to-module 规则不再依赖调用方恰好传入正斜杠。5. 新增测试覆盖 `Foo/Bar.as` 与 `Foo.as/Bar.as`、带反斜杠的相对路径、以及普通无歧义路径，断言前两者会被稳定诊断或映射为不同 key，后者保持现有 module name。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | M |
| 风险 | 更严格的 module key 规则可能暴露出仓库里历史上从未被检测出的路径冲突；需要先把错误消息做清楚，包含原始相对路径与规范化结果，避免迁移成本不可控。 |
| 前置依赖 | 建议与 `Issue-14` 的 duplicate-module fail-fast 一起落地，因为两者共享同一 module key 合同。 |
| 验证方式 | 1. 为 `.as` 出现在目录名中的场景新增回归测试，确认系统不会再把两个不同路径静默映射到同一模块。2. 对正常路径回归 `GetModuleByFilename()` 与 `FilenameToModuleName()`，确认 round-trip 结果保持稳定。3. 对反斜杠路径输入补测试，确认生成的 module key 与正斜杠输入一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-15 | Defect | 在 `Issue-14` 建立 duplicate-module 校验时一并修正，避免继续基于错误 module key 做冲突判断 |

---

## 发现与方案 (2026-04-08 13:03)

### Issue-16：预处理失败语义不原子，`GetModulesToCompile()` 仍会暴露半成品模块

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 75-83，212-307，265-304，4225-4295；29-30；2081-2082 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 33、34，这里补齐执行方案。已验证事实：`Preprocess()` 在 220-223 行一开始就把 `bIsPreprocessed` 置为 `true`，但只在 `ProcessImports()` 和 `DetectClasses()` 之后做 early-out；后续 `AnalyzeClasses()`、`ProcessMacros()`、`ProcessDelegates()`、`ProcessDefaults()`、`CondenseFromChunks()`、`PostProcessRangeBasedFor()`、`PostProcessLiteralAssets()` 即使通过后段校验把 `bHasError` 置为 `true`，仍会继续执行，并在 265-287 行广播 `OnProcessChunks` / `OnPostProcessCode`，最后在 289-304 行把 `ProcessedCode` 写入 `Module->Code`。同时 `GetModulesToCompile()` 只检查 `bIsPreprocessed`，不检查成功状态；引擎初始编译路径在 2081-2082 行会在 `Preprocessor.Preprocess()` 之后无条件读取这些模块。 |
| 根因 | 预处理器没有显式区分 `NotStarted / Running / Failed / Succeeded` 生命周期，也没有把“生成 `Module->Code` 与暴露结果给外部”做成成功后一次性提交的事务边界。 |
| 影响 | 一旦中后段预处理失败，失败路径仍会改写 chunk、生成部分 `ProcessedCode`、向 hook 暴露不稳定内部状态，并让调用方拿到半成品 `FAngelscriptModuleDesc`。这会把一次预处理错误扩散成后续模块决策、日志和诊断中的二次污染，错误恢复不再原子。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为预处理器建立显式成功状态和事务式提交边界，只有在全流程无 fatal error 时才广播 hook、写入 `Module->Code` 并允许外部读取模块结果。 |
| 具体步骤 | 1. 在 `FAngelscriptPreprocessor` 中把 `bIsPreprocessed` 扩展为显式状态枚举，例如 `ENotStarted / ERunning / EFailed / ESucceeded`，并提供 `DidPreprocessSucceed()` 或等价查询接口。2. 重写 `Preprocess()` 控制流：每个阶段后统一调用 `if (bHasError) { State = EFailed; return false; }`，把 `OnProcessChunks`、`OnPostProcessCode` 和 `Module->Code` 物化逻辑放到所有验证都通过之后。3. 将 `Module->Code`、`CodeHash`、`ImportedModules` 等对外可消费结果改成“临时缓冲 + 成功提交”模式，失败时只保留诊断，不把半成品写回 module desc。4. 收紧 `GetModulesToCompile()`：若状态不是 `ESucceeded`，直接 `ensureMsgf` + 返回空数组，或改成显式 `bool TryGetModulesToCompile(...)`，杜绝失败结果泄漏。5. 修正调用方：初始编译与 hot reload 入口都必须先检查 `Preprocess()` 返回值，再决定是否读取 module 列表；删除当前 `2081-2082` 这种“失败后仍取结果”的调用顺序。6. 为 trace hook 建立失败合同：只有成功路径广播，或新增独立 `OnPreprocessFailed` 只读诊断事件，避免外部插件继续读写半成品状态。7. 补充自动化测试，覆盖“后段 macro/class 错误后不生成 `Module->Code`”“`GetModulesToCompile()` 在失败后返回空”“失败时不触发成功 hook” 三类合同。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 若现有外部工具依赖“失败时仍能读到部分 `Files/Module->Code`”的未文档化行为，收紧合同后这些工具需要迁移；但这类依赖本身就建立在不稳定状态上，应尽早清理。 |
| 前置依赖 | 建议先盘点 `OnProcessChunks` / `OnPostProcessCode` 的实际使用点，确认哪些只是 trace，哪些错误依赖了失败态半成品。 |
| 验证方式 | 1. 构造一个会在 `ProcessMacros()` 或 `ResolveSuperClass()` 阶段报错的脚本，断言 `Preprocess()` 返回 `false` 后 `Module->Code.Num() == 0`。2. 断言失败后调用 `GetModulesToCompile()` 不会再返回半成品模块。3. 在 learning trace 中注册 hook，确认成功路径仍能收到事件，失败路径不会广播成功 hook。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-16 | Defect | 在继续修 import/include 之前先收紧失败原子性，避免后续修复继续建立在半成品状态上 |

---

## 发现与方案 (2026-04-08 13:04)

### Issue-17：缺失 import 的 provider 失败后，下游模块仍会把它当成可导入占位继续编译

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 439-497；3133-3208；4247-4280 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 42，这里补齐执行方案。已验证事实：`ProcessImports()` 找不到目标 module 时既不报错也不停止，只会把 `ImportDesc.ModuleName` 原样追加进 `File.Module->ImportedModules`。随后编译阶段会先在 3133-3136 行把当前批次所有 `Module` 无条件写进 `CompilingModulesByName`，再在 3175-3188 行用这个 map 解析 import。结果是：如果上游模块 A 因缺失 import 在 3191-3204 行被标记成 `bCompileError` 并跳过 `CompileModule_Types_Stage1()`，下游模块 B 仍然能通过 `CompilingModulesByName.Find("A")` 把 A 当成“已找到的依赖”加入 `ImportedModules`。到了 4266-4280 行，B 的 Stage1 只剩一个 `ensure(ImportModule->ScriptModule != nullptr)` 保护；即使实际导入被跳过，编译路径仍把这个失败 provider 当成已解析依赖处理。 |
| 根因 | 依赖合同只检查“当前 compile batch 里是否存在同名 module desc”，没有把“该 provider 已成功完成 Stage1 并具备可导入 `ScriptModule`”建成显式状态。 |
| 影响 | 首个缺失 import 的根因模块只会报一次直接错误，但依赖它的下游模块会继续进入 Stage1/parse 流程，再产出缺类型、缺符号等二次诊断，掩盖真正故障源。与此同时，下游模块的依赖集合已经包含一个不可导入 provider，模块图与真实可用依赖集发生偏离。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“名字存在”改成“依赖已解析且 provider 可导入”的显式合同，并把 provider 失败向下游模块做早期传播。 |
| 具体步骤 | 1. 结合 `Issue-4` 的 import resolver，把“目标 module 必须存在”前移到预处理/依赖图阶段；若解析不到 provider，直接在 import 行报错并阻止该 module 进入 compile batch。2. 在 `FAngelscriptModuleDesc` 上新增明确的编译状态，例如 `Unresolved / Stage1Ready / Stage1Failed / CompileFailed`，不要再让 `CompilingModulesByName` 的命中结果隐式代表“可导入”。3. 在 Stage1 解析 imports 时，只接受 `Stage1Ready` 或等价状态的 provider；若命中的 module desc 已处于 `CompileFailed` 或 `ScriptModule == nullptr`，当前 consumer 立即报 `import dependency failed to compile`，并跳过自身 Stage1。4. 停止把失败 provider 放进 `ImportedModules`/hash 组合路径；只有在 provider 可导入时才加入 `ImportedModules` 和后续依赖签名计算。5. 若需要保留完整错误链，新增“依赖失败传播”诊断，把 consumer 的 import 行号、provider 名和 provider 首个错误摘要一起输出，减少二次排查成本。6. 补充回归测试：`A` 缺失 import、`B import A` 时，`B` 应在 import 阶段直接失败而不是继续解析；同时断言 `B` 不会执行 Stage1、不会留下脏依赖 hash。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 预估工作量 | M |
| 风险 | 更严格的依赖传播会让当前“先报一个上游错误，再报多个下游缺符号错误”的行为改变为更早失败；这会影响部分现有日志预期，但能显著提升定位质量。 |
| 前置依赖 | 建议与 `Issue-4` 的两阶段 import resolver 一起实施，避免先在编译期补状态机、再在预处理期重构依赖图。 |
| 验证方式 | 1. 构造 `A -> Missing`、`B -> A`，断言 `A` 报 import 解析错误后，`B` 也以“依赖 A 编译失败”被短路，不再产生后续缺符号噪音。2. 断言失败 provider 不会进入 `ImportedModules` 的可导入列表，也不会参与 consumer 的依赖签名。3. 回归正常链式 import，确认 `A -> B -> C` 在 provider 全部成功时仍按既有顺序编译。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-17 | Defect | 接在 `Issue-4` 后处理，先把 import 解析失败改成可传播的显式依赖状态 |

---

## 发现与方案 (2026-04-08 13:05)

### Issue-18：失活分支里的 `#restrict usage` 仍会写入模块限制，模块元数据与实际编译结果漂移

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 3256-3402；3888-3890；4523-4607 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 35，这里补齐执行方案。已验证事实：`ParseIntoChunks()` 在 3363-3390 行识别 `#restrict usage allow/disallow` 时，会立刻向 `File.Module->UsageRestrictions` 追加 restriction；而对失活分支的裁剪是在 3395-3402 行之后才把源码抹成空格，前面没有任何“当前 `IfDefStack` 是否为 false”的守卫。结果是：即使 `#restrict usage` 位于已经被外层 `#if/#else` 排除的 dead branch 中，它仍会写入模块描述符。编译阶段又会在 3888-3890 行无条件调用 `CheckUsageRestrictions()`，并在 4523-4607 行消费这些 restriction。 |
| 根因 | directive 词法识别与分支可达性裁剪顺序不一致，`#restrict usage` 被当成“总是生效的元数据指令”处理，而不是“受当前活跃条件控制的源码语句”处理。 |
| 影响 | 模块系统看到的 `UsageRestrictions` 不再代表“当前配置下真正编译进模块的限制规则”，而是代表“源文件里出现过的 restriction 文本”。脚本作者把 restriction 放进死分支也仍会触发真实限制检查，导致配置切换、平台分支和模块边界诊断同时漂移。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 `#restrict usage` 与普通源码共享同一套“仅在活跃分支生效”的合同，禁止 dead branch 元数据泄漏进 module desc。 |
| 具体步骤 | 1. 在 directive scanner 中为 `#restrict usage` 增加显式 `bBranchActive` 判断；若当前 `bIfDefStackIsFalse`，只移除原始文本或保留为空白，不向 `UsageRestrictions` 追加任何数据。2. 更稳妥的做法是引入 `FDirectiveDesc` 临时结构，把 `#if/#elif/#else/#endif/#restrict` 都先解析成带 `bActiveAtParseTime` 的记录，再在统一提交阶段只消费 active restriction。3. 为 `UsageRestrictions` 增加来源信息（源文件、行号、active 条件），后续 `CheckUsageRestrictions()` 诊断可直接回指具体 directive，也便于确认某条 restriction 是否真的来自活跃分支。4. 在 `CheckUsageRestrictions()` 前增加轻量校验，忽略来自 inactive branch 或缺失源码 section 的 restriction，避免旧缓存/半成品模块继续触发错误检查。5. 补充自动化测试：`#if EDITOR ... #restrict usage disallow Runtime.* ... #endif` 在非 editor 上不应留下 restriction；相反在活跃分支内应稳定生效。6. 若后续重构 `ParseIntoChunks()`，把此逻辑并入共享 directive/cursor helper，避免 `#if` 与 `#restrict` 再次分叉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果现有脚本意外依赖了“dead branch restriction 仍生效”的错误行为，修复后会改变部分 editor 校验结果；需要通过测试和迁移说明明确这是对分支语义的纠正。 |
| 前置依赖 | 建议与 `Issue-3` 的 directive scanner 拆分一起实施，这样可以一次性统一 `#if` 与 `#restrict` 的活动分支判定。 |
| 验证方式 | 1. 在自动化里构造 active/inactive 两组 `#restrict usage`，断言只有活跃分支会写入 `UsageRestrictions`。2. 编译同一脚本两次，仅切换 `EDITOR` 或自定义 flag，确认 restriction 生效集合随条件切换而变化。3. 回归现有 restriction 检查，确认活跃分支中的违规 import 仍会被稳定诊断。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-18 | Defect | 在 directive scanner 重构时一并修复，先保证模块元数据只来自活跃分支 |

---

## 发现与方案 (2026-04-08 13:09)

### Issue-19：fully precompiled 启动路径的 `CodeHash` 校验是缓存自校验，无法识别磁盘脚本与缓存失配

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | 2052-2052，2068-2082，4284-4290；2752-2779 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 39，这里补齐执行方案。已验证事实：`InitialCompile()` 在 `bUsePrecompiledData && !bScriptDevelopmentMode` 路径下直接调用 `PrecompiledData->GetModulesToCompile()`，不会读取脚本磁盘内容也不会运行 `Preprocessor`。`GetModulesToCompile()` 又把缓存里的 `Module.CodeHash` 原样回填到 `ModuleDesc->CodeHash`。随后 `CompileModule_Types_Stage1()` 的 precompiled 命中条件仅检查 `CompiledModule->CodeHash == Module->CodeHash`。这两个值都来自同一个 `PrecompiledScript.Cache`，并没有任何“当前磁盘脚本内容”参与比较。 |
| 根因 | fully precompiled 启动把 `CodeHash` 同时当成“缓存快照值”和“当前源码真值”，但当前路径根本没有重建后者，导致校验退化成缓存自校验。 |
| 影响 | 只要部署包里残留旧的 `PrecompiledScript.Cache`，或脚本文件被手工替换而缓存未同步更新，启动路径仍会静默接受旧缓存并继续加载。结果是模块系统、JIT function ID 和诊断都会基于陈旧脚本运行，而不是在启动时回退到重新预处理。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 precompiled module 增加独立的源码指纹合同，在 fully precompiled 启动前先用磁盘文件验证缓存是否仍然对应当前脚本集。 |
| 具体步骤 | 1. 在 `FAngelscriptModuleDesc::FCodeSection` 或等价结构上新增 `RawSourceHash` / `SourceFingerprint` 字段，`Preprocess()` 在读取 `File.RawCode` 后立即计算并保存，而不是只在最终 `ProcessedCode` 上计算 `CodeHash`。2. 扩展 `FAngelscriptPrecompiledModule` 序列化格式，保存每个源码 section 的 `RelativeFilename`、`AbsoluteFilename` 的 canonical 形式，以及对应的 `RawSourceHash`；不要再只保存聚合后的 `CodeHash`。3. 在 `FAngelscriptPrecompiledData::GetModulesToCompile()` 之前增加 `ValidateCachedSources()`：逐个读取缓存记录的脚本文件，重算 `RawSourceHash`，一旦文件缺失或哈希不一致，就将整个模块或整份缓存标记为 stale。4. 修改 `InitialCompile()`：若 `ValidateCachedSources()` 失败，直接回退到 `FindAllScriptFilenames()` + `Preprocessor.Preprocess()` 路径，而不是继续走 `GetModulesToCompile()`。5. 将 `CompileModule_Types_Stage1()` 中的 `CompiledModule->CodeHash == Module->CodeHash` 由“唯一守卫”降级为“二次确认”；真正的首要命中条件应是 `ValidateCachedSources()` 通过且 context/hash 合同满足 `Issue-2`。6. 补充缓存失效测试：先生成 cache，再修改磁盘脚本但不更新 cache，断言启动时不会继续命中旧 precompiled module。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataTests.cpp` |
| 预估工作量 | L |
| 风险 | fully precompiled 启动会新增一轮轻量文件哈希读取，启动耗时会略增；但相比继续接受陈旧缓存，这个成本可控且必要。 |
| 前置依赖 | 建议与 `Issue-2` 的 `PreprocessContextHash` 一起设计，避免只校验源码文件却遗漏上下文切换导致的缓存失效。 |
| 验证方式 | 1. 构造“cache 由旧脚本生成，磁盘脚本已更新”的场景，确认启动路径回退到重新预处理。2. 构造“cache 与磁盘一致”的场景，确认仍能命中 precompiled 以避免性能回退。3. 删除单个脚本文件后重启，确认系统报缓存失效而不是继续加载旧模块。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-19 | Defect | 在继续扩展 precompiled 复用前先补真实源码校验，封住陈旧缓存静默生效路径 |

---

## 发现与方案 (2026-04-08 13:10)

### Issue-20：precompiled 反序列化出来的 `ModuleDesc` 丢失预处理元数据，缓存启动与实时预处理走的是两套合同

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 423-461；2752-2898，1484-1495；4353-4356，4523-4563，4944-4956 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 40，这里补齐执行方案。已验证事实：`FAngelscriptPrecompiledModule` 序列化字段只包含 `CodeHash`、`ImportedModules`、`ScriptRelativeFilename`、`PostInitFunctions` 等少量信息，没有 `Code` section、`UsageRestrictions`、`EditorOnlyBlockLines`。`GetModulesToCompile()` 从缓存重建 `FAngelscriptModuleDesc` 时，也只回填 `ModuleName`、`CodeHash`、`ImportedModules`、类/属性/函数描述等字段，没有恢复任何源码 section 或这三类预处理元数据。与此同时，`CheckUsageRestrictions()` 一旦看到 `Module->Code.Num() == 0` 就直接 `return`，`ScriptCompileError(Module, ...)` 在 `Module->Code` 为空时也会退化成只报 `ModuleName`，而实时编译路径还会用 `Module->EditorOnlyBlockLines` 设置 builder。 |
| 根因 | precompiled schema 被设计成“足够恢复脚本类型系统”的最小集合，没有保持与 `Preprocessor.Preprocess()` 输出同等的 module-level 合同。 |
| 影响 | 缓存启动拿到的 `ModuleDesc` 比实时预处理路径更瘦：restriction 校验会被整轮跳过，editor-only 行信息不会恢复，错误诊断也失去源码文件定位。结果是同一模块在“实时预处理”和“fully precompiled 启动”两条路径上会表现出不同的边界约束与诊断行为。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `FAngelscriptPrecompiledModule` 补齐为“可重建等价 `ModuleDesc`”的序列化合同，而不是只保存最小类型信息。 |
| 具体步骤 | 1. 为 `FAngelscriptPrecompiledModule` 新增源码 section 元数据数组，至少保存每个 section 的 `RelativeFilename`、`AbsoluteFilename` 的 canonical 形式、以及 section 级 hash；同时新增 `UsageRestrictions` 和 `EditorOnlyBlockLines` 的序列化字段。2. 在 `FAngelscriptPrecompiledModule::InitFrom()` 中，从 `Context.ModuleDesc` 完整复制这些预处理产物，而不是只拿 `CodeHash`、`ImportedModules` 和 `Code[0].RelativeFilename`。3. 在 `GetModulesToCompile()` 中按实时路径重建 `ModuleDesc->Code`、`ModuleDesc->UsageRestrictions`、`ModuleDesc->EditorOnlyBlockLines`，确保后续 `CheckUsageRestrictions()`、`ScriptCompileError()`、diagnostic capture 看到的是完整合同。4. 将依赖 `Module->Code.Num() == 0` 的调用点梳理成“缓存路径也成立”的显式条件；若某些逻辑只需要 section provenance，不要求完整源码文本，就不要再用空 `Code` 作为“来自缓存”的隐式标记。5. 对 builder/editor-only 路径建立统一入口：无论模块来自实时编译还是 precompiled，都应能拿到同样的 editor-only 行信息和 restriction 元数据。6. 补充回归测试：从 cache 启动时 `#restrict usage` 仍会生效，`ScriptCompileError` 仍能映射到源码文件，editor-only 行信息与实时预处理一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataTests.cpp` |
| 预估工作量 | L |
| 风险 | precompiled cache 格式会发生兼容性变化，需要 bump 版本或 build identifier，避免旧缓存被新代码误读。 |
| 前置依赖 | 建议和 `Issue-19` 一起实施，因为源码 section 元数据正好可复用为缓存有效性校验输入。 |
| 验证方式 | 1. 生成 cache 后走 fully precompiled 启动，断言 `CheckUsageRestrictions()` 对带 restriction 的模块仍会触发。2. 人工制造编译/诊断错误，断言缓存路径下的 `ScriptCompileError` 仍能落到具体文件。3. 比较同一模块在实时预处理和缓存启动下的 `UsageRestrictions`、`EditorOnlyBlockLines`、`Code` section 元数据，确认一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-20 | Architecture | 在扩展 precompiled schema 时与 Issue-19 联动，先恢复与实时预处理等价的 module metadata 合同 |

---

## 发现与方案 (2026-04-08 13:12)

### Issue-21：precompiled script section 恢复时丢失 script root 身份，plugin 脚本会被映射到错误文件路径

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | 1427-1431，2046-2052，781-793；423-461；1484-1495，1417-1485 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 41，这里补齐执行方案。已验证事实：引擎初始化早期在 `bUsePrecompiledData` 场景下先把 `AllRootPaths` 设为 `DiscoverScriptRoots(true)`，只保留 project root。fully precompiled 启动时 `InitialCompile()` 直接走 `PrecompiledData->GetModulesToCompile()`，不会再像实时预处理路径那样先调用 `MakeAllScriptRoots()`。与此同时，`FAngelscriptPrecompiledModule` 只序列化单个 `ScriptRelativeFilename`，`GetScriptSection()` 又总是用 `FAngelscriptEngine::GetScriptRootDirectory()` 返回的 `AllRootPaths[0]` 去拼接脚本绝对路径。也就是说，来自 content plugin 的脚本在缓存恢复时会被强制解释成“project root 下的同名文件”。 |
| 根因 | precompiled schema 只保存“root 内相对路径”，没有保存 script root 身份；恢复脚本 section 时又依赖 `GetScriptRootDirectory()` 的单一根目录语义。 |
| 影响 | plugin 模块在 fully precompiled 路径下会拿到错误的 script section filename，进而污染调试符号、错误定位、覆盖率、以及任何依赖 section 名称回溯源码的功能。若 project root 与 plugin root 存在同名相对路径，错误还会被进一步放大成指向另一份脚本。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 precompiled section 元数据中显式保存 source root 身份，并在缓存恢复前构建完整 root registry，禁止再通过“第一个 root”猜测脚本来源。 |
| 具体步骤 | 1. 将 `FAngelscriptModuleDesc::Code` 的 section provenance 扩展为 `SourceRootKey + RelativeFilename` 或等价结构；`FAngelscriptPrecompiledModule` 序列化时保存每个 section 的 root key，而不是只保存 `ScriptRelativeFilename`。2. 在 `FAngelscriptEngine` 初始化阶段，即使准备使用 precompiled data，也要先调用 `MakeAllScriptRoots()` 构建完整 root registry，确保 project/plugin roots 都可解析。3. 重写 `FAngelscriptPrecompiledData::GetScriptSection()`：根据序列化的 `SourceRootKey` 查找对应 root，再与 `RelativeFilename` 组合出真实脚本路径；不要再调用 `GetScriptRootDirectory()` 取 `AllRootPaths[0]`。4. 对多 section 模块统一改成“按 section 恢复 script section 索引”，避免仍然只盯着 `Code[0]` 或单个 `ScriptRelativeFilename`。5. 若运行时找不到某个 `SourceRootKey`，应把缓存标记为 stale 并回退到重新预处理，而不是静默落回 project root。6. 补充测试：project root 与 plugin root 各放一个同名相对路径脚本，生成 cache 后走 fully precompiled 启动，断言两个模块的 script section 都指向各自真实文件。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataTests.cpp` |
| 预估工作量 | M |
| 风险 | cache schema 需要新增 root key 字段；若直接使用插件名称作为 root key，要确认插件重命名或挂载点变化时的兼容处理。 |
| 前置依赖 | 建议与 `Issue-20` 一起落地，因为二者都需要把完整 section provenance 写入 precompiled schema。 |
| 验证方式 | 1. 对 project script 和 plugin script 分别生成函数，断言 cache 恢复后的 script section filename 与真实文件一致。2. 构造同名相对路径脚本，确认缓存启动不会把 plugin 模块错误指向 project 文件。3. 移除某个 plugin root 后重启，确认系统识别缓存失效并回退，而不是继续用错误路径。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-21 | Defect | 在 precompiled schema 扩展时同步修复，先消除 plugin script 的错误源码映射 |

---

## 发现与方案 (2026-04-08 13:21)

### Issue-22：declared function import 没有进入统一依赖图，provider 变化后只能靠后置校验和解绑兜底

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.h` |
| 行号 | 1272-1306；2282-2415，3367-3417，4259-4280，4426-4429，4611-4724；5560-5602；255-278 |
| 问题 | 已验证事实：`FAngelscriptModuleDesc` 只持久化 `ImportedModules` 和 `PostInitFunctions`，没有任何字段保存 declared function import 依赖；`PerformHotReload()` 的依赖扩散只看 `ImportedModules` 或 `moduleDependencies`；`CompileModule_Types_Stage1()` 计算 `CombinedDependencyHash` 时也只混入 module-level `ImportedModules`。与此相对，AngelScript builder 在 `RegisterImportedFunction()` 中只是调用 `module->AddImportedFunction(...)`，而 `asCModule` 把这类信息存进独立的 `bindInformations`，真正被 reload/recompile avoidance 消费的仍是 `moduleDependencies`。因此 declared function import 既不会驱动预处理/热重载依赖传播，也不会进入依赖哈希，只能在 `CheckFunctionImportsForNewModules()` 和 `ResolveDeclaredImports()` 这类后置步骤里做“检查或解绑”。 |
| 根因 | 模块系统把 declared function import 当成编译后的绑定细节，而不是和 `import Foo.Bar;` 同等级的一等依赖边；结果是预处理器、编译器、hot reload、cache 各自消费不同容器，declared import 没有共享合同。 |
| 影响 | provider 模块改名、函数删除、签名变化或 provider 编译失败时，consumer 不会被模块依赖图自动纳入同一轮重编译/失效传播，`CombinedDependencyHash` 也无法体现这条依赖。当前实现最多在编译后对旧模块做一次校验，失败时把模块加入下一轮 reload，或在 provider compile error 场景下直接静默 `UnbindImportedFunction()`；这会把本应在依赖图中确定传播的问题，退化成滞后的后置诊断和运行时解绑状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 declared function import 提升为模块描述符里的显式依赖元数据，让预处理、热重载、依赖哈希和绑定校验都消费同一份依赖图。 |
| 具体步骤 | 1. 在 `FAngelscriptModuleDesc` 中新增 `DeclaredFunctionImports` 元数据结构，至少保存 `FromModuleName`、函数声明字符串、是否 hard dependency、以及后续 `Issue-23` 需要的源码位置。2. 在编译阶段把 imported function 信息从 `asIScriptModule` 提取并回填到 `ModuleDesc`；若愿意改 third-party，更稳妥的做法是在 `RegisterImportedFunction()` / `AddImportedFunction()` 时直接把这份元数据连同 source location 一起落到 module descriptor 或桥接结构里。3. 扩展 hot reload 依赖传播：`PerformHotReload()` 在构建 `ReverseDeps`、`MarkedModules` 和 structural-change 传播时，同时把 `DeclaredFunctionImports` 指向的 provider 视为依赖边，至少将其当作 hard dependency，确保 provider 变化时 consumer 会进入同一轮 reload 决策。4. 更新 `CompileModule_Types_Stage1()` 的依赖签名合同，把 declared function import 的 provider 哈希混入 `CombinedDependencyHash`，避免 provider 变更后仍命中旧的 recompile avoidance / precompiled cache。5. 将 `CheckFunctionImportsForNewModules()` 改造成“基于 `ModuleDesc` 依赖元数据的统一校验”，不再单纯依赖 `ScriptModule->GetImportedFunctionCount()` 临时遍历；并在 provider compile error 场景下把 consumer 标记为 dependency-invalid，显式报出“因为 provider 编译失败而无法解析 declared import”的诊断，而不是直接假设错误已经报过。6. 若 fully precompiled 路径也支持 declared function import，需同步扩展 `PrecompiledData` 的 schema，保存并恢复 `DeclaredFunctionImports`，保证缓存启动与实时编译遵守同一依赖合同。7. 补充测试：provider 删除导出函数、改签名、整体编译失败三种场景下，consumer 都必须在同一轮 reload 中被识别为受影响模块，并且 `CombinedDependencyHash` 会随 provider 变化而变化。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptNativeScriptHotReloadTests.cpp` |
| 预估工作量 | L |
| 风险 | declared function import 目前部分语义在 third-party `asCModule` 内部；如果直接改第三方结构，要控制升级成本。更稳妥的桥接做法是先在引擎层镜像依赖元数据，再决定是否向下游回灌。 |
| 前置依赖 | 建议与 `Issue-4` 的 canonical import resolver 一起设计统一 dependency key，避免 module import 和 declared function import 再各自维护一套 provider 标识。 |
| 验证方式 | 1. 热重载测试中修改 provider 的函数签名，确认 consumer 会在同一轮被加入重编译/失败集合，而不是仅在 swap-in 后被解绑。2. 对相同 consumer 分别走“provider 正常存在”和“provider compile error”路径，确认两者都会产出明确诊断。3. 对比修复前后的 `CombinedDependencyHash`，确认 declared function import 已进入依赖签名。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-22 | Architecture | 在 `Issue-4` 之后推进，先统一 dependency key，再把 declared function import 纳入同一依赖图 |

---

## 发现与方案 (2026-04-08 13:22)

### Issue-23：declared function import 在注册时丢失源码位置，错误永远退化到模块第 1 行

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_scriptfunction.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 543-548，5560-5602；65-70；1390-1420；357-395；399-407；4681-4694 |
| 问题 | 已验证事实：普通脚本函数在 builder 中会把 `scriptSectionIdx` 和 `declaredAt` 写进 `func->scriptData`；但 `RegisterImportedFunction()` 解析完 declared import 后只把签名和 `moduleName` 传给 `module->AddImportedFunction(...)`。`asCModule::AddImportedFunction()` 创建的是 `asFUNC_IMPORTED` 类型的 `asCScriptFunction`，构造函数不会分配 `scriptData`，`sBindInfo` 也只保存 `importedFunctionSignature`、`importFromModule` 和 `boundFunctionId`。到了引擎侧，`CheckFunctionImportsForNewModules()` 一旦找不到 provider/module，就只能调用 `ScriptCompileError(Module, 1, ...)`，把所有 declared function import 错误都钉在模块第 1 行。 |
| 根因 | declared function import 的 AST 路径没有沿用普通脚本函数的 source-location 建模；解析阶段拿到了 `node` 和 `file`，但落库时没有任何结构保存“这条 import 声明来自哪个 section、哪一行哪一列”。 |
| 影响 | 一个模块包含多个 declared function import 时，任何失配都会落成同一个 `Row=1` 诊断，用户无法直接定位是哪条声明出错。随着 `Issue-22` 暴露更多 declared import 依赖问题，如果不先补齐源码锚点，后续错误会继续停留在“知道模块错了，但不知道具体哪一行”的低可用状态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 declared function import 单独持久化 source location，并让所有相关诊断优先回到真实声明位置，而不是继续复用模块级兜底行号。 |
| 具体步骤 | 1. 扩展 third-party 元数据载体，推荐优先修改 `sBindInfo`：新增 `scriptSectionIdx`、`declaredAtRow`、`declaredAtColumn` 或等价压缩字段；不要依赖 `asFUNC_IMPORTED` 自动拥有 `scriptData`，因为其构造路径当前不会分配这块数据。2. 在 `RegisterImportedFunction()` 中，和普通函数一样用 `file->ConvertPosToRowCol(node->tokenPos, &row, &col)` 计算位置，并在调用 `AddImportedFunction()` 时把 section/source location 一并传下去。3. 扩展 `asCModule::AddImportedFunction()` 签名，把位置元数据写入 `sBindInfo` 或新增的 `FImportedFunctionMetadata`；保留原有 `boundFunctionId` 逻辑，避免影响绑定流程。4. 在引擎侧增加 helper，例如 `GetImportedFunctionLocation(asIScriptModule*, ImportIndex)`，`CheckFunctionImportsForNewModules()` 和后续任何 declared import 诊断都通过这个 helper 取得真实行列，再调用 `ScriptCompileError(Module, Row, Message)` 或等价接口。5. 若实施 `Issue-22` 的 `DeclaredFunctionImports` 元数据，也同步把 source location 镜像到 `FAngelscriptModuleDesc`，避免 fully precompiled / reload-only 路径丢失位置信息。6. 补充自动化：同一模块写两条 declared import，仅破坏第二条 provider，断言诊断落在第二条声明的准确行号，而不是第 1 行。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | M |
| 风险 | 修改 third-party `as_module` 的 import 元数据结构需要注意序列化/恢复路径以及上游升级冲突；如果不想扩大改动面，可先在引擎层建立 side-table，但要确保 import index 在 reload 后仍稳定可映射。 |
| 前置依赖 | 无；但若与 `Issue-22` 同时推进，建议一次性设计 `DeclaredFunctionImports` 元数据，避免位置信息再次只存在于临时 side-table。 |
| 验证方式 | 1. 构造多条 declared import 的脚本，只让其中一条失配，确认诊断行号精确落在对应声明。2. provider compile error 与 provider 缺失两种路径都应保留真实位置。3. 回归普通 import function 绑定成功场景，确认新增元数据不会改变现有绑定结果。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-23 | Defect | 可与 `Issue-22` 并行设计，优先把 diagnostics 锚点补齐，降低后续 declared import 改造的定位成本 |

---

## 发现与方案 (2026-04-08 13:24)

### Issue-24：预处理没有稳定的 module index 与中间产物缓存，显式 `import` 解析和增量预处理都被迫重复全表扫描

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 120-160；229-285，439-482；1272-1306；2069-2082，2264-2455 |
| 问题 | 已验证事实：`FFile` 把 `RawCode`、`ChunkedCode`、`ProcessedCode`、`Imports` 等中间态都放在 `FAngelscriptPreprocessor` 的临时对象里；`Preprocess()` 每次都按 `ParseIntoChunks -> ProcessImports -> Detect/Analyze/ProcessMacros/ProcessDefaults -> Condense/PostProcess` 的完整链路重跑；显式 `import` 的 `ProcessImports()` 甚至对每条依赖都线性扫描整份 `Files` 查 provider。与此同时，`FAngelscriptEngine` 在初次编译和每轮 hot reload 都重新构造新的 `FAngelscriptPreprocessor`，`FAngelscriptModuleDesc` 则只保留最终 `Code`、`CodeHash`、`ImportedModules` 等结果，不保留任何可复用的解析/依赖中间产物。额外验证：当前仓库执行 `rg --files -g '*.as'` 统计到 325 个脚本文件，说明这套“每轮重新建 Preprocessor + 显式 import 线性扫描”的成本已经具备放大量级。 |
| 根因 | 预处理器没有把 discovery、parse、resolve、transform 拆成可缓存的阶段接口；模块系统只有最终 `ModuleDesc`，没有稳定的 `ModuleIndex`、解析 fingerprint 或 graph cache，导致显式 import 查找、错误恢复和增量重用都只能围绕临时 `TArray<FFile>` 做一次性计算。 |
| 影响 | 当前实现不仅让显式 `import` 在模块数增长时持续做重复字符串比较，还直接阻断了增量预处理的落地：一旦后续实现 `Issue-4` 的 canonical resolver、`Issue-5` 的 include graph、或 `Issue-22` 的 declared import 依赖，所有新逻辑都只能继续堆在一次性 `Preprocess()` 流水线上，既难缓存，也难做精确失效。结果是热重载即使只改很少文件，也无法复用未变模块的解析中间态。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 拆出稳定的 `ModuleIndex + PreprocessArtifactCache`，把预处理从“一次性流水线”重构为可缓存、可失效、可复用的阶段化图处理。 |
| 具体步骤 | 1. 在 `Preprocessor` 层定义清晰阶段对象，例如 `FDiscoveredScript`、`FParsedScriptArtifact`、`FResolvedDependencyGraph`、`FPreprocessedModuleArtifact`；`FFile` 只作为本轮执行上下文，不再承担长期缓存职责。2. 在 `AddFile()`/`Preprocess()` 开始阶段先构建 `ModuleIndex`，至少包含 `CanonicalModuleName -> FileId`、`CanonicalPath -> FileId`、反向依赖边和文件内容 fingerprint；显式 `import` resolver 只允许通过这个索引查找 provider，彻底删除 `for (FFile& OtherFile : Files)` 式线性扫描。3. 在 `FAngelscriptEngine` 增加进程内 `PreprocessArtifactCache`，key 必须覆盖 `CanonicalRelativePath`、文件内容 hash、`PreprocessorFlags`、automatic/manual import mode、`OnProcessChunks/OnPostProcessCode` hook 版本，以及默认 property/function 配置，避免缓存遗漏影响输出正确性。4. Hot reload 时先根据变更文件的 fingerprint 失效对应 cache entry，再沿 cached dependency graph 扩散到受影响模块；未受影响且 fingerprint 未变的脚本直接复用 `FParsedScriptArtifact`/`FResolvedDependencyGraph`，跳过重新读盘和 chunk 解析。5. 将 `FAngelscriptModuleDesc` 扩展为引用 `PreprocessedModuleArtifact` 的轻量结果视图，或新增 companion cache 结构保存 `ImportedModules`、future `IncludedFiles`、declared import 依赖、editor-only 行号等预处理元数据，使 runtime compile、hot reload、precompiled schema 共用同一份 artifact 合同。6. 先做内存缓存版本验证正确性，再决定是否把一部分 artifact 序列化到 fully precompiled cache；不要一开始就把临时格式写进磁盘。7. 补充性能与正确性回归：用 import-heavy fixture 和真实 325 文件脚本树分别统计“resolver 查找次数”“重新解析文件数”“热重载耗时”，要求未变模块不再重复进入 `ParseIntoChunks()`，显式 import provider 查找从全表扫描降为索引命中。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | XL |
| 风险 | cache key 设计不完整会直接生成 stale artifact，问题会比当前“慢但正确”更难排查；必须把 flags、hooks、路径 canonicalization 和 import 模式全部纳入 fingerprint。 |
| 前置依赖 | 建议在 `Issue-3` 拆分 scanner、`Issue-4` 建立 canonical import key 后再启动，否则缓存层会把当前脆弱的语法/路径语义固化下来。 |
| 验证方式 | 1. 为 import-heavy fixture 记录 `ProcessImports()` 的 provider 查找次数，确认修复后不再与 `Files.Num()` 相乘增长。2. 热重载仅修改一个无依赖脚本时，确认只有该脚本及其受影响反向依赖重新进入 parse/resolve 阶段。3. 修改 `PreprocessorFlags`、hook 输出或 import 模式后，确认 cache 会正确失效，不会复用旧 artifact。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-24 | Architecture | 在 `Issue-3`/`Issue-4` 稳定语法与 canonical key 后推进，作为 include graph 与增量预处理的基础设施 |

---

## 发现与方案 (2026-04-08 13:32)

### Issue-25：补齐 `Preprocessor_Analysis.md` 发现 26 的执行方案，修复 `import` 尾随 block comment 被吞进模块名

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Documents/AutoPlans/Preprocessor_Analysis.md` |
| 行号 | 1368-1369，3497-3507，4363-4390；327-337 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 26，这里补齐执行方案。已验证事实：`ParseIntoChunks()` 在 3497-3507 行只是把 `import` 与 `;` 之间的原文 `TrimStartAndEnd()` 后直接写进 `ImportDesc.ModuleName`；同文件 1368-1369 行的 defaults 解析却会显式调用 `StripCommentsFromLine()`；而 4363-4390 行已经存在支持 block comment / line comment 的 `StripCommentsFromLine()` 实现。也就是说，仓库里已经有可复用的注释剥离 helper，但 `import` 词法入口没有使用它。 |
| 根因 | `import` 入口把“截取 statement 原文”和“生成可解析 module key”合并成一步，导致 comment stripping 规则没有与 defaults 或后续 canonical import resolver 共享。 |
| 影响 | `import Gameplay.Core /* shared helpers */;` 这类脚本会稳定把注释文本带进 `ImportedModules` 和最终诊断，模块查找失败信息也会继续携带脏字符串，降低 import 错误的可读性与可迁移性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 `import` 语法入口引入独立的 statement-normalization 步骤，先剥离注释再做 canonical module name 解析，避免 comment 直接污染依赖图 key。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp` 抽出 `NormalizeImportStatementText(const FString&)` 或并入 `Issue-4` 计划中的 `ResolveImportName(...)`，内部顺序固定为“截取原文 -> `StripCommentsFromLine()` -> `TrimStartAndEnd()` -> canonicalize”。2. `ParseIntoChunks()` 只负责记录 `import` 的源码范围与行号，不再直接把原始 substring 填进 `ImportDesc.ModuleName`；改为保存 `RawImportText` 或延后到 resolver 阶段再生成 canonical key。3. unresolved import、重复 import 与循环 import 的诊断统一使用剥离注释后的 canonical 名称；若需要保留原文，单独附带 `RawImportText` 供 trace 使用，不要再复用作查找 key。4. 在 `Issue-3` 的共享 cursor/helper 落地后，把 comment stripping 规则收口到同一套 lexer helper，避免 defaults、import、future include 各自维护不同的注释处理逻辑。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 新增回归：`import Gameplay.Core /* shared helpers */;`、`import Gameplay.Core/*shared*/;`、以及带 line comment 的 `import Gameplay.Core; // note`，断言三者都解析成同一个 `ImportedModules` key，且 `ProcessedCode` 里原始 import 文本仍被正确移除。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | S |
| 风险 | 若简单地在 parser 阶段直接调用 `StripCommentsFromLine()`，需要确认不会误伤未来支持的字符串字面量 import 语法；更稳妥的做法是把它纳入统一 resolver，并让测试锁住现有 module-name 语法。 |
| 前置依赖 | 建议与 `Issue-4` 的 import canonicalization 一起实现，避免先做一次局部注释剥离，随后再重写 resolver。 |
| 验证方式 | 1. 回归 analysis 中的 block-comment import 场景，确认 `ImportedModules` 只保留 `Gameplay.Core`。2. unresolved import 诊断不再包含 `/* ... */` 注释文本。3. automatic import 与 explicit import 两种模式下，`ProcessedCode` 都不再残留原始 import 行。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-25 | Defect | 与 `Issue-4` 合并实现，作为 import canonicalization 的低风险补缺 |

---

## 发现与方案 (2026-04-08 13:33)

### Issue-26：补齐 `Preprocessor_Analysis.md` 发现 57 的执行方案，修复 fully precompiled + explicit `import` 丢失拓扑顺序

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Documents/AutoPlans/Preprocessor_Analysis.md` |
| 行号 | 570；2752-2783；2046-2052，3101-3208，4266-4280；765-775 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 57，这里补齐执行方案。已验证事实：fully precompiled 启动在 2046-2052 行直接调用 `PrecompiledData->GetModulesToCompile()`，跳过 live preprocessor；而 `GetModulesToCompile()` 在 2752-2783 行只是按 `TMap<FString, FAngelscriptPrecompiledModule> Modules` 的遍历顺序恢复 `ModuleDesc`，没有任何按 `ImportedModules` 的 DAG 排序；随后 `CompileModules()` 在 3101-3208 行直接 `CompilationQueue.Append(InModules)` 并按当前顺序进入 stage1。显式 import 模式下，当前模块对同批次 provider 的查找只要求 descriptor 在 `CompilingModulesByName` 里存在；真正导入时 4266-4280 行仍依赖 provider 已经拥有 `ScriptModule`，否则只剩一条 `ensure`。 |
| 根因 | fully precompiled 路径只恢复了 module descriptor 数据，没有恢复“explicit import 需要先做拓扑排序”这条预处理合同；编译阶段又把“descriptor 已存在”误当成“provider 已 stage1-ready”。 |
| 影响 | 关闭 automatic import 并启用 fully precompiled 时，consumer 可能在 provider 之前静默进入 stage1，导致 `ImportIntoModule()` 被跳过、`CombinedDependencyHash` 仍继续组合、以及后续符号解析结果依赖于偶然的编译顺序，而不是显式 import 语义。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 explicit import 的拓扑排序抽成 live/precompiled 共用的阶段前置步骤，并把 “provider stage1-ready” 建成显式约束，而不是继续依赖 `ensure`。 |
| 具体步骤 | 1. 提取共享 helper，例如 `SortModulesByExplicitImports(TArray<TSharedRef<FAngelscriptModuleDesc>>& Modules)`，内部复用 `Issue-4` 的 canonical import key 与循环诊断规则；`Preprocess()` 和 `FAngelscriptPrecompiledData::GetModulesToCompile()` 都必须调用这同一个 helper，而不是各自维护顺序逻辑。2. `GetModulesToCompile()` 在恢复完 `ImportedModules` 后、返回前执行拓扑排序；若命中循环依赖、缺失 provider 或 duplicate module key，直接把缓存标记为 invalid/stale 并回退到实时预处理，而不是继续把脏顺序送进 `CompileModules()`。3. 在 `CompileModules()` 中新增 provider readiness 检查：显式 import 模式下，如果 `FoundModule` 还没有 `ScriptModule` 或未达到 `Stage1Ready`，当前 consumer 立即按依赖失败处理，复用 `Issue-17` 的错误传播，而不是仅靠 `ensure` 后继续编译。4. 为 fully precompiled schema 增加可选的 `ExplicitImportOrder` 或 `DependencyLevel` 元数据，只作为 trace/debug 观测值；真正的执行顺序仍应在加载时重新根据 `ImportedModules` 计算，避免旧 cache 顺序在规则变更后继续污染结果。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataTests.cpp` 和 compiler pipeline 测试中新增双路径对照：同一组 `A imports B` fixture 分别走 live preprocess 和 fully precompiled，断言两条路径的 stage1 顺序、`ImportedModules` 可见性和 `CombinedDependencyHash` 完全一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 预估工作量 | M |
| 风险 | fully precompiled 路径当前默认隐藏了这类顺序问题；修复后可能会让旧 cache 在启动时被判 stale 并回退到实时预处理，需要提前接受一次性 cache miss 成本。 |
| 前置依赖 | 建议与 `Issue-4`、`Issue-17` 同步设计，共享 import resolver 与 provider-failure 合同。 |
| 验证方式 | 1. 在 `bAutomaticImports = false` 条件下构造 `B imports A` 的 precompiled fixture，确认 fully precompiled 与 live preprocess 都先编译 `A` 再编译 `B`。2. provider 故意缺失时，fully precompiled 路径应回退或明确报依赖错误，而不是继续 stage1。3. 对比两条路径生成的 `CombinedDependencyHash` 与可见类型集合，确认完全一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-26 | Defect | 在继续扩展 precompiled 复用前先修复，确保 explicit import 在 cache 路径上仍遵守正确拓扑顺序 |

---

## 发现与方案 (2026-04-08 13:34)

### Issue-27：补齐 `Preprocessor_Analysis.md` 发现 58 的执行方案，修复 precompiled cache 仅按 `ModuleName` 建 key 的静默覆盖

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Documents/AutoPlans/Preprocessor_Analysis.md` |
| 行号 | 570；2651-2660，2758-2779；3133-3135；781-791 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 58，这里补齐执行方案。已验证事实：`FAngelscriptPrecompiledData` 在 570 行把缓存模块存进 `TMap<FString, FAngelscriptPrecompiledModule> Modules`；生成 cache 时 2651-2660 行对每个运行时模块直接执行 `Modules.FindOrAdd(ModuleName).InitFrom(*this, Module)`，后写入的同名模块会静默覆盖前者；读取 cache 时 2758-2779 行也只是遍历这个 map 恢复单份 `ModuleDesc`。相比之下，实时编译路径至少还会在 3133-3135 行对 duplicate module 打 `ensure`。 |
| 根因 | precompiled schema 把 `ModuleName` 误当成全局唯一主键，却没有把 script root、canonical relative path 或其他文件身份纳入 key，也没有在序列化时显式拒绝 duplicate module。 |
| 影响 | 一旦工程里出现 duplicate module，问题会在写 cache 时被永久压扁成 last-writer-wins：fully precompiled 启动只能看到被覆盖后的单份 descriptor，live compile 与 cache compile 观察到的模块集合将不再一致，后续 import、diagnostics、类型注册与热重载分析都会基于错误的缓存世界运行。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 precompiled module key 升级为可区分物理来源的 canonical identity，并在生成 cache 时对 duplicate module fail-fast，而不是继续静默覆盖。 |
| 具体步骤 | 1. 结合 `Issue-14` 与 `Issue-21` 的方案，为 precompiled schema 新增稳定主键，例如 `FPrecompiledModuleKey { CanonicalModuleName, SourceRootKey, CanonicalRelativeFilename }` 或等价结构；`Modules` 容器改为按该复合 key 存储，而不是裸 `FString ModuleName`。2. 在 `PrepareToFinalizePrecompiledModules()` 中先构建 `CanonicalModuleName -> [SourceIdentity...]` 检查表；若同一 canonical module name 命中多个不同物理来源，立即生成明确 diagnostics 并放弃写 cache，避免把错误状态固化进磁盘。3. `GetModulesToCompile()` 读取 cache 时，先校验复合 key 与当前 root registry 是否仍可解析；若某个 key 缺少对应 root/path，按 stale cache 处理并回退到实时预处理。4. 若产品层最终坚持“module name 必须全局唯一”，也要把这个约束前移到 cache 生成阶段，保证 precompiled 与 live compile 对 duplicate module 使用同一条 fail-fast 规则，而不是一边 `ensure`、一边 silently overwrite。5. 为调试与兼容性保留 `ModuleName` 作为展示字段，但所有查找、反序列化和冲突检测都必须改用复合 key；同时 bump cache schema/version，防止旧格式继续被新代码误读。6. 在 `Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataTests.cpp` 与 file-system 测试中新增 duplicate-module 场景，断言 cache 生成会显式失败或回退，而不是产出单份覆盖后的 cache。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 预估工作量 | M |
| 风险 | cache schema 改动会导致现有 PrecompiledScript.Cache 整体失效；但这是必要代价，否则新版本仍会在重复模块场景下读取历史脏缓存。 |
| 前置依赖 | 建议与 `Issue-14` 的 duplicate-module fail-fast、`Issue-21` 的 source root identity 一起设计，避免 runtime/live/cache 三套 key 再次分叉。 |
| 验证方式 | 1. 构造两个映射到同一 module name 的脚本，断言生成 precompiled cache 时会收到明确 duplicate-module 错误，而不是只保留其中一个。2. 删除或更换某个 plugin root 后重启，断言旧 cache 会被识别为 stale 并回退。3. 对正常唯一模块路径回归 fully precompiled 启动，确认不会引入额外误报。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-27 | Defect | 与 `Issue-14`、`Issue-21` 联动推进，先把 precompiled schema 的主键合同收紧到与运行时一致 |

---

## 发现与方案 (2026-04-08 13:41)

### Issue-28：`IsStartOfIdentifier()` 把数字边界写成 `0-1`，会把 `2-9` 后面的 `import/class/namespace` 误判成真正关键字

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3076-3087，3410-3510，3759-3764 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 21，这里补齐执行方案。已验证事实：`ParseIntoChunks()` 里所有顶层关键字入口都依赖 `IsStartOfIdentifier()` 判断当前位置是否是合法词法边界，但该 helper 在 3085 行把数字检查写成了 `PrevChar >= '0' && PrevChar <= '1'`。结果只有前导字符是 `0` 或 `1` 时才会阻止关键字识别，`2-9` 都会被当成“不是 identifier 的一部分”。同一函数里 `class`、`struct`、`interface`、`import`、`namespace` 等入口都复用了这个 helper，因此像 `foo2import Bar;`、`Version9namespace Core {}` 这类本应属于普通标识符拼接文本的片段，会被预处理器误识别成真正语法入口。 |
| 根因 | 共享词法边界 helper 存在直接的字符范围笔误，而且关键字入口没有复用更稳健的“identifier continuation”判定 API，导致一个低层 typo 同时污染多套解析分支。 |
| 影响 | 预处理器可能从无效源码片段中凭空构造 `File.Imports`、错误切换 chunk 类型或错误压栈 namespace，最终把真正的语法问题扭曲成 import/module 诊断。这类误判直接落在 `import` 和模块系统入口，会放大后续 canonicalization、循环检测和错误恢复的噪音。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先修正共享 identifier 边界判定，再用集中 helper 和回归测试锁住所有关键字入口的词法前导条件。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp` 把 `PrevChar >= '0' && PrevChar <= '1'` 修正为显式的数字全集判定，优先使用 `FChar::IsDigit(PrevChar)` 或新增 `IsIdentifierContinuation(TCHAR)` helper，避免再次出现手写范围 typo。2. 将 `IsStartOfIdentifier()` 重写为基于 `IsIdentifierContinuation()` 的单点判定，把字母、数字、下划线规则全部收口到一处，不再在 lambda 内逐段硬编码。3. 复查 `ParseIntoChunks()` 中依赖该 helper 的 `class/struct/interface/import/namespace/default/event/delegate/enum` 入口，确认它们都只在真正的 token 边界上触发；如有必要，再补一个对“后一个字符必须是空白或分隔符”的共享 helper，减少各分支再写一套局部判断。4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 增加回归样例：`foo2import Bar;`、`Value9class X {}`、`Version8namespace Core {}`，断言这些文本不会生成 `Imports`、不会切 chunk、不会污染 namespace。5. 再补一个正向样例，确认真正合法的 `import Foo.Bar;`、`class X {}` 仍能被识别，防止修复边界后把正常关键字入口一起关掉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果测试只覆盖 `import`，其他关键字入口仍可能继续依赖旧假设；需要把所有复用 `IsStartOfIdentifier()` 的入口一起扫一遍，避免修一处漏一片。 |
| 前置依赖 | 无 |
| 验证方式 | 1. 新增词法边界回归，确认 `2-9` 后缀不再触发任何关键字分支。2. 对合法 `import/class/namespace` fixture 回归，确认正常预处理结果不变。3. 复跑现有 preprocessor tests，确认修复没有引入新的误报或漏报。 |

### Issue-29：`Editor/Dev/Examples` 目录过滤与 `isEditorOnlyModule` 判定大小写敏感，lowercase 目录会绕过环境隔离

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1973-1985，4353-4356 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 68，这里补齐执行方案。已验证事实：脚本发现阶段 `FindScriptFiles()` 只在目录名精确等于 `Examples`、`Dev`、`Editor` 时才跳过子目录；编译阶段 builder 的 `isEditorOnlyModule` 也只接受 `Module->ModuleName.StartsWith("Editor.")` 或 `Contains(".Editor.")`。这两处都没有任何 `IgnoreCase` 或统一 canonicalization。结合 Windows 这类大小写不敏感文件系统，只要真实目录名写成 `editor/`、`dev/`、`examples/`，文件发现不会跳过它们，后续 module name 又保持原始大小写，builder 也不会把它们标成 editor-only。 |
| 根因 | 同一份“目录语义”被分散实现为两套 case-sensitive 字符串比较：一套在文件发现层，一套在 builder 配置层，没有共享 canonical path segment/helper。 |
| 影响 | editor/dev/example 脚本可以仅靠目录大小写差异就泄漏进 runtime 或 cooked 编译路径，导致模块可见性、条件编译环境和 editor-only warning 行为同时漂移。这正是大小写不敏感文件系统上的结构性边界缺陷，会让 import/module graph 在不同机器上表现不一致。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把脚本目录分类规则收敛成统一的 case-insensitive helper，并让发现阶段与 builder 共享同一份模块环境判定。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.cpp/.h` 提取共享 helper，例如 `GetScriptDirectoryKind(const FString& RelativePathOrModuleName)` 或 `IsEditorOrDevelopmentSegment(const FString&)`，内部统一对路径段执行分隔符归一和 `IgnoreCase` 比较。2. 用该 helper 替换 `FindScriptFiles()` 中对 `Examples`、`Dev`、`Editor` 的裸字符串判断，保证发现阶段无论目录大小写如何都得到同样的跳过语义。3. 在 `CompileModule_Types_Stage1()` 设置 `isEditorOnlyModule` 时，不再直接依赖 `ModuleName.StartsWith("Editor.")`；改成复用同一个 helper，或在 `FAngelscriptModuleDesc` 中提前写入 canonical 的 module environment 标记。4. 若项目明确要求目录大小写规范，也应在发现阶段附带 warning：指出原始目录名与 canonical 形态不一致，但编译语义仍按 canonical 结果执行，避免不同平台出现分叉。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` 增加 `editor/dev/examples` 小写目录 fixture，断言 non-editor 上下文会稳定跳过；在 compiler pipeline 或 preprocessor trace 测试中增加 `editor.Foo` / `Editor.Foo` 两种路径，断言 builder 看到的 `isEditorOnlyModule` 一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果现有工程里有人依赖“小写 `editor/` 也会进 runtime”这种错误行为，修复后会暴露为缺失模块；需要把诊断做成“指出目录与 canonical 环境分类”的迁移提示。 |
| 前置依赖 | 建议复用 `Issue-13` 的 canonical script path/key 设计，避免 discovery、hot reload、builder 再分别维护不同的大小写规则。 |
| 验证方式 | 1. 在大小写不敏感路径 fixture 中验证 `editor/dev/examples` 的多种 casing 都得到相同跳过结果。2. 编译 `editor.Foo` 与 `Editor.Foo` 模块时，断言 builder 的 `isEditorOnlyModule` 标志一致。3. 对现有正常 `Editor/Dev/Examples` 目录回归，确认没有引入额外误报。 |

### Issue-30：`AddFile()` 不归一化 Windows 风格相对路径，`\` 会直接渗入 module key 并与 `import` 语义分叉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 行号 | 15；86-99；395-401 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 69，这里补齐执行方案。已验证事实：`FAngelscriptPreprocessor::AddFile()` 的公开接口直接接收 `ScriptRelativePath`，但 `FilenameToModuleName()` 只做 `Replace(TEXT(".as"), TEXT("")).Replace(TEXT("/"), TEXT("."))`，完全不处理 `\`。这意味着任何调用方若传入 Windows 风格相对路径，例如 `Game\\Path\\Normalize.as`，`Module->ModuleName` 就会保留反斜杠。相比之下，现有 `AngelscriptFileSystemTests` 已明确验证运行时 filename lookup 会把绝对路径中的 `/` 与 `\` 视为等价输入。也就是说，runtime lookup 的路径合同已经接受双分隔符，而 preprocessor 的 path-to-module 合同没有同步。 |
| 根因 | 相对路径规范化被拆成两套实现：运行时 lookup 有 slash-normalization，preprocessor 的 `FilenameToModuleName()` 却只覆盖正斜杠，且 `AddFile()` 没有在入口层统一 canonicalize。 |
| 影响 | 只要有新的调用方、测试夹具、hot reload 工具或未来的 include/import resolver 直接向 `AddFile()` 传入 Windows 风格相对路径，模块名就会从 `Game.Path.Normalize` 漂移成 `Game\\Path\\Normalize`。后续 `import Foo.Bar`、`Editor.` 前缀判断、duplicate-module 检查和缓存 key 都会与这份模块描述符失配。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 `AddFile()` 入口统一 canonicalize relative path，并让所有 path-to-module 逻辑复用同一个分隔符归一 helper。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取共享 helper，例如 `CanonicalizeRelativeScriptPath(const FString&)` 和 `BuildModuleNameFromRelativePath(const FString&)`，内部先统一把 `\` 转成 `/`、折叠重复分隔符，再只对叶子文件名去扩展名。2. `AddFile()` 在保存 `File.RelativeFilename` 和生成 `Module->ModuleName` 前，先对 `ScriptRelativePath` 调用该 helper，确保 preprocessor 内部不再出现带反斜杠的 relative key。3. 将 `FilenameToModuleName()`、`GetModuleByFilename()`、以及 `Issue-4` 计划中的 import resolver 全部改为复用这同一个 helper，保证“按文件建模块名”和“按 import 文本找模块”遵守同一份 slash 语义。4. 在 diagnostics 中继续保留原始输入路径用于展示，但所有 module identity、duplicate check 和 cache key 都只消费 canonical relative path。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 新增直接调用 `AddFile("Game\\Path\\Normalize.as", ...)` 的回归，断言 `GetModulesToCompile()[0]->ModuleName == "Game.Path.Normalize"`；同时保留并扩展现有 `AngelscriptFileSystemTests`，验证 forward-slash 与 backslash 输入最终命中同一模块 identity。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 预估工作量 | S |
| 风险 | 统一 relative path 后，少数依赖“模块名里保留反斜杠”的外部工具可能会暴露兼容性问题；需要提前扫描是否存在这类 out-of-tree 调用。 |
| 前置依赖 | 建议与 `Issue-14` / `Issue-4` 的 canonical path 设计合并落地，避免再次引入第二套 path-to-module helper。 |
| 验证方式 | 1. 直接向 `AddFile()` 传入 backslash 相对路径，确认生成的 `ModuleName` 与 forward-slash 输入完全一致。2. `import Game.Path.Normalize;` 能命中由 backslash 相对路径生成的模块。3. 对现有 filename lookup normalization 测试回归，确认 slash 归一化合同在 runtime 与 preprocessor 两侧保持一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-29 | Defect | 与 `Issue-13`、`Issue-4` 联动优先修复，先统一大小写不敏感文件系统上的目录环境判定 |
| P1 | Issue-28 | Defect | 在继续扩展 `import/include` 语法前先修复，避免词法边界误判继续污染依赖图 |
| P2 | Issue-30 | Defect | 与 `Issue-14` / `Issue-4` 合并实现，统一 relative path 到 module key 的分隔符语义 |

---

## 发现与方案 (2026-04-08 16:32)

### Issue-31：`#` directive 识别不检查字符串和行首上下文，字符串字面量里的 `#if/#else/#endif` 会篡改预处理状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3256-3392，3881-3912，4349-4360 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 20，这里补齐执行方案。已验证事实：`ParseIntoChunks()` 在每轮循环开头只要命中 `Char == '#' && !bInComment` 就会尝试把当前位置当成真实 directive 解析，但这里既没有检查 `!bInString`，也没有检查当前 `#` 是否位于“行首仅有空白之后”的合法 directive 位置。字符串状态直到后面的 `case '\"'` 分支才维护；一旦源码里出现 `\"debug #if RELEASE\"`、`\"#else\"` 这类文本，前面的 directive 分支就会先于字符串保护触发，并调用 `KillRawLine()` 把这一行从 `#` 开始抹空。 |
| 根因 | directive lexer 直接在原始字符流上做裸前缀匹配，没有显式的 `IsDirectiveStart` 判定；词法状态机又把字符串状态更新放在 directive 识别之后，导致文本内容和真实指令共享了同一入口。 |
| 影响 | 普通字符串字面量可以意外修改 `IfDefStack`、触发 `#restrict usage`、或直接抹掉同一行后续源码，预处理输出会与真实脚本语义脱节。因为这类篡改发生在 chunk 切分之前，后续 import/module 诊断经常只会表现成误导性的二次错误，而不是直接指出字符串里的 `#` 被错当成 directive。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 directive 建立统一的 `IsDirectiveStart` 词法入口，只允许“非字符串、非注释、行首仅有空白”位置触发真实 `#...` 解析。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取共享 helper，例如 `IsDirectiveStart(const FDirectiveScanState&, int32 Pos)` 或并入 `Issue-3` 计划中的 `FPreprocessorCursor`，至少统一检查 `!bInString`、`!bInComment`、以及从本行起始到 `Pos` 之间只有空白字符。2. 把 `ParseIntoChunks()` 中 `if (Char == '#' && !bInComment)` 这条总入口改成调用该 helper，禁止 mid-line `#` 和字符串内 `#` 进入 directive 分支。3. 维护一个 `bOnlyWhitespaceSinceLineStart` 或等价状态，在 `\n` 时重置，在读到非空白字符后清零，保留现有“允许缩进后的 directive”能力，但拒绝 `Code(); #if ...` 这类非标准写法。4. 让 `KillRawLine()` 只在 `IsDirectiveStart` 已确认成功后被调用，避免对字符串文本做 destructive blanking；必要时把它升级为“按已识别 directive span 清理”，而不是对任意 `#` 粗暴抹整行。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 增加回归：字符串中包含 `#if/#else/#endif` 不应修改 `IfDefStack` 或 `ProcessedCode`，同时保留一组带缩进的真实 directive 正向样例，确认合法脚本仍会被正确裁剪。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果项目里曾经依赖“非行首 `#` 也会被当成 directive”的非标准行为，修复后会改变这些脚本的结果；但这类输入本身就与当前脚本语法不一致，应该通过明确诊断而不是继续兼容错误行为。 |
| 前置依赖 | 建议与 `Issue-3` 的 directive/cursor 拆分一起实施，但不是硬依赖。 |
| 验证方式 | 1. 新增字符串内 `#if/#else/#endif` 回归，确认不会产生任何条件分支副作用。2. 新增“行首缩进 + 真 directive”用例，确认 `#if`、`#restrict usage` 仍可正常工作。3. 对一组真实脚本比较修复前后 `ImportedModules`、`UsageRestrictions` 与 `ProcessedCode`，确认只消除误判，没有破坏合法 directive。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-31 | Defect | 在继续补 directive 边界前先修复，先封住字符串文本污染预处理状态的入口 |

---

## 发现与方案 (2026-04-08 16:33)

### Issue-32：`#ifdef/#ifndef` 把“flag 已注册”误当成“flag 为 true”，与 `#if` 的布尔语义自相矛盾

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 38-65，3259-3277，4328-4346 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 6，这里补齐执行方案。已验证事实：构造函数启动时会把 `EDITOR`、`EDITORONLY_DATA`、`COOK_COMMANDLET`、`RELEASE`、`TEST`、`WITH_SERVER_CODE` 等内置 flag 全部注册进 `PreprocessorFlags`，只是 value 可能为 `false`。但 `ParseIntoChunks()` 处理 `#ifdef/#ifndef` 时只看 `PreprocessorFlags.Find(Identifier) != nullptr` 或 `== nullptr`，完全忽略 map 中保存的布尔值；相比之下，`#if` 会走 `ParsePreProc()`，明确读取 `*bValue`。结果是 `#ifdef EDITOR` 在 runtime/cooked 上下文里也会被判成 true，`#ifndef RELEASE` 在非 shipping 环境里也会被错误判成 false。 |
| 根因 | 预处理器把“布尔型环境 flag”与“C 风格 macro 是否定义”两套语义混在了一张 `TMap<FString, bool>` 里，但只给 `#if` 路径实现了布尔求值，`#ifdef/#ifndef` 仍停留在“key 是否存在”的定义检测。 |
| 影响 | 常见写法 `#ifdef EDITOR`、`#ifndef RELEASE`、`#ifdef TEST` 会在非目标上下文里裁出错误分支，直接把 editor-only/test-only 代码带进错误模块；`EditorOnlyBlockLines`、`UsageRestrictions` 以及后续 chunk 解析也会随之被污染。因为 `#if FLAG` 与 `#ifdef FLAG` 对同一 flag 给出不同结果，脚本作者很难从表面诊断看出真正根因。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 统一简单 symbol directive 的求值语义，让 `#ifdef/#ifndef` 与现有布尔 flag 模型保持一致，而不是继续混用“已定义”和“值为 true”。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取共享 helper，例如 `EvaluateFlagCondition(const FString& Identifier, bool bNegate, bool& bOutKnown)`，集中处理“查表、读取 bool、缺失 flag”的逻辑，避免 `#if` 与 `#ifdef` 各写一套。2. 将 `#ifdef X` 改为“`X` 已知且值为 `true`”，`#ifndef X` 改为“`X` 未知或值为 `false`”；`#if X` / `#if !X` 则继续通过同一 helper 求值，保持四条路径合同一致。3. 如果团队仍需要 C 风格“只检查定义性”的语义，不要再复用 `#ifdef/#ifndef` 当前实现，而应显式新增 `defined(X)` 或等价语法，再由 parser 单独支持，避免旧 DSL 继续模糊。4. 审查 `UpdateEditorBlockLines()`、反射宏环境检查和其它依赖 `IfDefStack.Condition` 的逻辑，确认修复后这些调用点看到的是与实际活跃分支一致的 flag 条件。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 增加回归：构造 `EDITOR=false`、`RELEASE=false`、`TEST=false` 上下文，分别验证 `#ifdef`、`#ifndef` 与 `#if` / `#if !` 的结果一致；同时补一个自定义 settings flag 为 `true` 的正向样例。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果某些 out-of-tree 脚本把 `#ifdef` 当成“只看是否注册”的语义，修复后分支结果会变化；但当前内置 flag 全部预注册，这种旧语义本身已经无法正确表达 editor/runtime 差异，继续保留只会扩大歧义。 |
| 前置依赖 | 无；但建议与 `Issue-31`、`Issue-33` 一起回归 directive 合同。 |
| 验证方式 | 1. 新增 `#ifdef EDITOR` / `#if EDITOR` 与 `#ifndef RELEASE` / `#if !RELEASE` 对照测试，断言结果完全一致。2. 在 simulated cooked 或 runtime 上下文回归，确认 editor-only 代码不会再被错误保留。3. 对真实脚本比较修复前后 `EditorOnlyBlockLines` 与 `ProcessedCode`，确认只有错误分支发生变化。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-32 | Defect | 与 `Issue-31` 连续修复，先统一条件编译的基础真假语义 |

---

## 发现与方案 (2026-04-08 16:35)

### Issue-33：失活分支里的 `#if/#ifdef/#ifndef/#elif` 仍会求值，不可达代码中的未知 flag 会把整轮预处理打成失败

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3256-3402，4328-4346 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 8 和 66，这里补齐执行方案。已验证事实：`ParseIntoChunks()` 总是先处理 `#if/#ifdef/#ifndef/#elif`，直到后面的 3395-3402 行才根据 `bIfDefStackIsFalse` 把死分支源码抹成空格。因此外层分支已经判为 false 时，内层 `#if` 仍会立即调用 `ParsePreProc()` 或直接读取 flag。与此同时，`#elif` 在 3303 行先求值 `ParsePreProc(File, LineNumber, PreProc)`，3306-3314 行之后才看 `bAnyBranchTaken`；也就是说，前一个分支已经命中时，后面的不可达 `#elif UNKNOWN_FLAG` 仍会因为未知 flag 触发 `Invalid preprocessor condition`。 |
| 根因 | 条件编译状态机没有区分“维护分支嵌套结构”和“求值当前条件”这两个阶段；outer inactive branch、already-taken branch 与 truly reachable branch 共用了同一条立即求值路径。 |
| 影响 | 平台互斥代码、editor/runtime 分支和 fallback 分支都无法安全承载只在特定环境存在的 flag。一个永远不会执行到的 dead branch 也能把整个预处理打成 fatal error，迫使项目把所有平台 flag 全量注册到所有上下文，错误恢复能力显著下降。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 conditional directive 建立显式 short-circuit 规则：inactive branch 只维护栈结构，不求值条件；`#elif` 只有在前序分支未命中且外层活跃时才允许求值。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取共享 helper，例如 `IsOuterBranchActive(const TArray<FIfDef>& Stack)`、`ShouldEvaluateCurrentBranch(...)`，统一判断“当前 directive 是否真的可达”。2. 重写 `#if/#ifdef/#ifndef` 处理：如果外层已有任一分支 inactive，只向 `IfDefStack` 压入一个 `bValue=false` 的占位帧，并记录原始条件文本用于诊断，但不要调用 `ParsePreProc()` 或读取 flag value。3. 重写 `#elif`：先检查 `IfDefStack.Num()`、`bHasElse`、`bAnyBranchTaken` 和 outer branch 是否 active；只有在“本层尚未命中过分支且外层活跃”时才执行 `ParsePreProc()`，否则直接把当前 `bValue` 置为 false。4. `#else/#endif` 继续无条件维护结构，但要保证它们只改变当前层状态，不会触发新的条件求值。5. 把“未知 flag 报错”限定在 reachable branch 上；对于 unreachable branch，只保留源码文本用于后续条件栈平衡，不生成 fatal diagnostics。6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 增加两组回归：`#if EDITOR ... #else #if UNKNOWN ... #endif #endif` 在 `EDITOR=true` 时应成功；`#if EDITOR ... #elif UNKNOWN ... #endif` 也应成功且第二分支不求值。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 若实现时只修 `#elif` 而没有一起修外层 inactive branch 的 `#if/#ifdef/#ifndef`，条件编译仍会留下半套错误语义；需要把整组 directive 作为一个状态机合同一起回归。 |
| 前置依赖 | 建议与 `Issue-31`、`Issue-32` 一起落地，统一 directive lexer 和条件求值 helper。 |
| 验证方式 | 1. 新增 dead-branch unknown-flag 回归，确认 unreachable `#if/#elif` 不再触发 `Invalid preprocessor condition`。2. 对真正 reachable 的未知 flag 保留报错，确认 short-circuit 没有把合法诊断吞掉。3. 对同一脚本切换 `EDITOR`/`RELEASE` 等上下文，比较 `ProcessedCode` 与 `EditorOnlyBlockLines`，确认只有活跃分支参与输出。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-33 | Defect | 与 `Issue-31`、`Issue-32` 组成同一批修复，优先把 conditional directive 状态机做成可短路的正确语义 |

---

## 发现与方案 (2026-04-08 16:42)

### Issue-34：module-level `import` 在编译期丢失源行号，未解析依赖永远退化到模块第 1 行

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 101-108；463-490，3501-3508；1302-1303；3173-3195 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 14，这里补齐执行方案。已验证事实：`FImport` 明确保存 `FileLineNumber`，`ParseIntoChunks()` 解析 `import` 时也把当前 `LineNumber` 写进 `ImportDesc.FileLineNumber`；但 `ProcessImports()` 最终只把 `ImportDesc.ModuleName` 追加进 `Module->ImportedModules`，`FAngelscriptModuleDesc` 也只保留 `TArray<FString> ImportedModules`。到了编译阶段，`CompileModules()` 查不到 provider 时只能调用 `ScriptCompileError(Module, 1, "could not find module ... to import.")`。也就是说，预处理阶段已经拿到精确 import 行号，但跨阶段合同把它压扁成了模块级字符串，最终诊断固定落在第 1 行。 |
| 根因 | 预处理器和编译器之间的 import 元数据合同过瘦，只传递 module name，不传递 import edge 的 source location。 |
| 影响 | 同一个文件有多条 `import` 时，缺失模块、大小写不一致、相对路径解析失败等问题都会退化成“模块第 1 行出错”。这会直接放大 `Issue-4` 路径规范化和循环诊断治理的排障成本，因为用户看不到到底是哪一条 import 失败。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 module-level import 从“字符串列表”升级为带 source location 的依赖元数据，并让 unresolved import 诊断优先回到 import 原始行。 |
| 具体步骤 | 1. 在 `FAngelscriptModuleDesc` 中新增轻量依赖结构，例如 `FModuleImportRef { FString CanonicalModuleName; int32 SourceLine; FString RawImportText; }`，不要再只保存裸 `FString ImportedModules`。2. `ProcessImports()` 在完成 canonicalization 后，把 `ImportDesc.FileLineNumber` 和原始/规范化 import 文本一起写入该结构；`ImportedModules` 如需保留兼容，可改为从新结构投影生成，而不是继续作为唯一事实源。3. 修改 `CompileModules()` 的显式 import 查找逻辑，遍历 `Module->ModuleImports`，查找失败时调用 `ScriptCompileError(Module, ImportRef.SourceLine, ...)`，并把错误消息中的 module 名替换为 canonical 名或附带原始 import 文本。4. 将 `Issue-4` 计划中的循环诊断与 unresolved import 诊断统一到同一个 import edge 元数据上，避免一条边在预处理和编译阶段各维护一套行号来源。5. 对 fully precompiled 路径同步扩展 schema，至少保存 import edge 的 `SourceLine`，保证缓存启动时 unresolved import 或 dependency-invalid 诊断不会再次退化到第 1 行。6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 与 compiler pipeline 测试中增加多 import fixture，仅破坏第二条 import，断言错误准确落在第二条声明行。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦 import 元数据进入 `FAngelscriptModuleDesc` 与 cache schema，需要同步梳理所有仍假设 `ImportedModules` 是 `TArray<FString>` 的调用点，避免 live/cache 两套路径再次分叉。 |
| 前置依赖 | 建议与 `Issue-4` 一起设计，共享 canonical import key 与 edge-level 诊断合同。 |
| 验证方式 | 1. 构造单文件两条 `import`，只破坏第二条，断言错误行号等于第二条 import 所在行。2. 构造大小写不一致或相对路径 import 失败，确认诊断仍回到原始 import 行。3. fully precompiled 启动下复现同样错误，确认不再退化到模块第 1 行。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-34 | Defect | 与 `Issue-4` 同批设计，先把 import edge 诊断合同补全，再扩展更多路径规范化与缓存治理 |

---

## 发现与方案 (2026-04-08 16:44)

### Issue-35：directive lexer 把空白写死成字面量空格，tab 分隔的 `#if/#ifdef/#ifndef/#elif/#restrict` 会被静默跳过

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 225-228；3257-3366 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 67，这里补齐执行方案。已验证事实：预处理器已经定义了 `IsWhitespace()`，其中把 `'\t'` 视为合法空白；类、接口、`import` 等普通关键字入口也都用 `IsWhitespace(...)` 判断后继分隔符。但 directive 分支没有复用这套规则，而是硬编码 `FCString::Strncmp(..., "#ifdef ")`、`"#ifndef "`、`"#if "`、`"#elif "`、`"#restrict usage allow "`、`"#restrict usage disallow "` 这些带字面量空格的模式。结果只要脚本写成 `#if\tEDITOR`、`#ifdef\tTEST` 或 `#restrict usage allow\tGameplay.*`，这些行就不会进入任何 directive 分支，也不会调用 `KillRawLine()` 修改状态栈，而是以普通源码文本继续留在 `RawCode` 中。 |
| 根因 | directive lexer 维护了独立于 `IsWhitespace()` 的第二套“关键字 + 分隔符”规则，并且把分隔符错误地收窄成单个 `' '` 字符。 |
| 影响 | 使用 tab 对齐 directive 的脚本会静默失去条件编译和 usage restriction 语义：本应被裁剪的代码继续参与 chunk 解析与编译，本应生效的 `#restrict usage` 完全不入模块描述符。由于这里不是 fail-fast，而是直接漏判，问题会表现成环境漂移和错误源码进入后续 import/module 流程。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 directive 入口统一改成“匹配关键字，再消费任意 whitespace”的共享 lexer helper，不再手写带空格的 `Strncmp` 常量。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取共享 helper，例如 `TryMatchDirectiveKeyword(const TCHAR* Raw, int32 Pos, const TCHAR* Keyword, int32& OutBodyStart)`；内部先匹配关键字本体，再用 `IsWhitespace()` 或 `FChar::IsWhitespace()` 消费任意空白。2. 用该 helper 重写 `#ifdef/#ifndef/#if/#elif/#restrict usage allow/#restrict usage disallow` 的入口判断，彻底删除 `TEXT("#if ")` 这类带尾随空格的裸字符串比较。3. 把 `#else/#endif` 也纳入同一 helper 家族，至少统一校验“关键字后只能是行尾或空白”，避免未来再出现 `#endiffoo` 这类边界分叉。4. 借这次重构把 directive keyword 表整理成一张集中表，减少 `ParseIntoChunks()` 中同类 `Strncmp` 链长度；对简单平面 token 可考虑使用预编译 regex 或表驱动分派，但真正的状态变更仍保持在显式代码里。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 新增回归：`#if\tEDITOR`、`#ifdef\tTEST`、`#elif\tRELEASE`、`#restrict usage allow\tGameplay.*`，断言它们与空格版本产生相同的 `ProcessedCode`、`IfDefStack` 行为和 `UsageRestrictions`。6. 追加一个负向用例 `#iffoo` / `#restrictx`，确认 helper 不会把非 directive 前缀误识别为合法关键字。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 若只替换 `#if` 系列而漏掉 `#restrict`，directive 语义仍会部分分叉；需要把整组入口一次性收口到共享 helper。 |
| 前置依赖 | 建议与 `Issue-31`、`Issue-32`、`Issue-33` 一起实施，统一 directive lexer 与条件状态机。 |
| 验证方式 | 1. tab 版和空格版 `#if/#ifdef/#ifndef/#elif` 产生完全一致的 `ProcessedCode`。2. tab 版 `#restrict usage` 能稳定写入 `Module->UsageRestrictions`。3. `#iffoo` 等非法前缀不会被错误识别成合法 directive。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-35 | Defect | 与 `Issue-31`~`Issue-33` 同批推进，先把 directive 词法入口统一到 whitespace-aware helper 上 |

---

## 发现与方案 (2026-04-08 16:45)

### Issue-36：默认 `automatic import` 路径的真实依赖只存在于 `moduleDependencies`，模块签名与 precompiled cache 仍停留在显式 `ImportedModules` 合同

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | 1302-1303；2350-2362，3171-3208，4247-4297，4566-4578；432-456；1420-1424，2775-2779 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 中的发现 16、17，这里补齐执行方案。已验证事实：`FAngelscriptModuleDesc` 持久化的依赖元数据只有 `ImportedModules`。默认 `automatic import` 开启时，`CompileModules()` 完全跳过 `Module->ImportedModules` 查找，直接以空 `ImportedModules` 调用 `CompileModule_Types_Stage1()`；后者把 `CombinedDependencyHash` 初始化为 `CodeHash`，并且只对传入的显式 `ImportedModules` 做 `ImportIntoModule()` 与 hash 组合。与此同时，运行时另外两条路径已经承认真实依赖存在于 `asCModule::moduleDependencies`：hot reload 反向传播和 usage restriction 校验都遍历它。可 precompiled schema 在保存/恢复时依旧只序列化 `CodeHash` 与 `ImportedModules`。结果是默认路径下真正的 provider 集合只活在编译后 `moduleDependencies`，却从未回写到 module descriptor、依赖签名和 cache 命中合同。 |
| 根因 | 模块系统仍把“依赖”定义成预处理阶段的显式 `import` 列表，而默认编译模式已经切换到编译器推导出的 automatic dependency graph；两套依赖模型没有被统一到一个持久化合同。 |
| 影响 | 在默认 `automatic import` 模式下，上游模块变化若不改动 consumer 源码，consumer 的 `CombinedDependencyHash` 仍可能保持不变，precompiled cache 也可能继续仅凭 `CodeHash` 命中。这样会让真实依赖变化、缓存复用和后续签名消费长期漂移，形成“live compile 知道依赖，module desc/cache 不知道依赖”的结构性分叉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 建立统一的“resolved dependency graph”合同，把 automatic import 推导出的 `moduleDependencies` 回写到 `FAngelscriptModuleDesc` 与 precompiled schema，再由统一 helper 计算模块签名和 cache 命中条件。 |
| 具体步骤 | 1. 在 `FAngelscriptModuleDesc` 中新增持久化依赖字段，例如 `ResolvedModuleDependencies` 或 `DependencyGraphSignatureInput`，区分“源码显式 import”与“编译后真实依赖”，不要再让 `ImportedModules` 一栏承担全部语义。2. 在 automatic import 路径完成依赖发现后，从 `ScriptModule->moduleDependencies` 提取 provider 的 canonical module name 和必要的 provider fingerprint，回填到 `ModuleDesc`；该步骤应放在依赖集已经稳定的阶段，并对 live compile 与 cache-hit 路径共用。3. 提取统一 helper，例如 `RecomputeResolvedDependencySignature(FAngelscriptModuleDesc&, asCModule*)`，由它基于 `ResolvedModuleDependencies` 计算 `CombinedDependencyHash`；不要再在 `CompileModule_Types_Stage1()` 里只按显式 `ImportedModules` 做一次性 XOR。4. 扩展 `FAngelscriptPrecompiledModule` schema，保存 `ResolvedModuleDependencies` 与对应签名输入；`InitFrom()`/`GetModulesToCompile()` 都要恢复这份图，而不是继续只拷贝 `ImportedModules`。5. 修改 precompiled 命中条件：`CompiledModule->CodeHash == Module->CodeHash` 只能作为源码一致性的子条件，真正的 cache 复用必须联合校验 resolved dependency signature 或 provider fingerprints，确保 automatic import 上游变化会使 consumer cache miss。6. 将 hot reload/recompile avoidance、usage restriction、StaticJIT 等后续消费者逐步迁移到同一份 `ResolvedModuleDependencies`/signature 合同，避免再次出现“运行时看 `moduleDependencies`，缓存看 `ImportedModules`”的双轨语义。7. 补充回归：在 automatic import fixture 中只修改 provider 模块，断言 consumer 的 resolved dependency signature 和 cache 命中状态都会变化；fully precompiled 启动时恢复出的依赖图应与实时编译一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | L |
| 风险 | automatic import 的真实依赖何时算“最终稳定”需要和现有编译阶段对齐；若选择过早快照，可能遗漏后续 builder 衍生依赖，需要先用测试锁住阶段边界。 |
| 前置依赖 | 建议与 `Issue-20`、`Issue-24` 同步设计，共享可持久化的 dependency artifact 与缓存键。 |
| 验证方式 | 1. automatic import 模式下，修改 provider 但不改 consumer 源码，断言 consumer 的 resolved dependency signature 会变化。2. 生成 precompiled cache 后仅修改 provider，确认启动时 consumer 不会继续命中旧 cache。3. 比较 live compile 与 fully precompiled 启动恢复出的依赖集合，确认二者一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-36 | Architecture | 与 `Issue-20`、`Issue-24` 联动，优先统一默认 automatic import 路径的真实依赖合同与缓存失效条件 |

---

## 发现与方案 (2026-04-08 16:58)

### Issue-37：`import` 词法解析对 block comment 不感知，注释里的 `(` 或 `;` 会改变语句分类并留下半条残码

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 1377-1385，3497-3510，4363-4408 |
| 问题 | 已验证事实：module-level `import` 目前只是从 `import` 后第一个字符开始线性扫到首个 `;`，完全不跳过 block comment；随后又直接用 `ImportDesc.ModuleName.Contains("(")` 把“包含括号”的语句排除出 `File.Imports`。这意味着 `import Gameplay.Core /* (temporary) */;` 会因为注释里的 `(` 被误判成非模块 import，整个依赖边直接丢失；`import Gameplay.Core /* ; temporary */;` 又会在注释内的 `;` 提前截断 `EndPosInChunk`，后续 `ReplaceWithBlank()` 只会清掉前半句，把 `temporary */;` 残留进 `ProcessedCode`。同文件 4363-4408 行其实已经存在 `StripCommentsFromLine()`，但 import 入口完全没有复用。 |
| 根因 | 解析器用“原始 substring 是否含 `(`”来区分 module import 与 declared function import，同时用 comment-blind 的“首个 `;`”决定语句终点；import lexer 没有共享现成的注释剥离和 terminator 扫描规则。 |
| 影响 | 这是比“注释文本进入模块名”更严重的边界问题：合法的 module import 会被静默降级成未建模源码，或者只移除半条语句，把脏注释尾巴继续送进编译器。结果既会漏掉真实 `ImportedModules` 依赖，也会制造误导性的二次语法错误，直接破坏 import 解析与错误恢复的确定性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 import 解析改成 comment-aware 的完整 statement parser，在分类和 blank 源码之前先得到剥离注释后的 statement 视图。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 新增 `ParseImportStatement()` 或并入 `Issue-3` 的 cursor helper，统一提供“跳过字符串/注释后查找 statement terminator”的能力，禁止继续使用 `while (... != ';')` 的裸扫描。2. 将 `FImport` 扩展为同时保存 `RawStatementText`、`NormalizedStatementText` 和准确的 `StartPosInChunk/EndPosInChunk`；`NormalizedStatementText` 必须先经过 `StripCommentsFromLine()` 或等价 comment-aware 过滤，再做 `TrimStartAndEnd()`。3. 用 comment-free token 序列区分 module import 与 declared function import，不再依赖原始文本 `Contains("(")`；只有真正的 declared function import 语法才走排除分支。4. `ReplaceWithBlank()` 只消费 comment-aware parser 返回的 statement 终点，保证 `/* ... ; ... */` 场景也能把整条 import 完整抹除。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 新增回归：`import Gameplay.Core /* (temporary) */;` 应仍生成 `ImportedModules = Gameplay.Core`；`import Gameplay.Core /* ; temporary */;` 应完整移除源码且不残留注释尾巴；再补一条真正的 declared function import，确认不会被误收进 module import 列表。6. 把这套 comment-aware statement parser 作为 future `#include` 和 delegate terminator 解析的共享底座，避免同类 bug 在别的入口重复出现。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | import 与 declared function import 目前共享 `import` 前缀，若 parser 只做局部修补而没有把分类规则抽成统一 helper，容易在修复 comment 场景时误伤 declared function import。 |
| 前置依赖 | 建议与 `Issue-3`、`Issue-25` 合并设计，共享 comment-aware cursor 与 import normalization。 |
| 验证方式 | 1. block comment 含 `(` 的 module import 仍会进入 `ImportedModules`。2. block comment 含 `;` 的 import 会被整句 blank，不在 `ProcessedCode` 留下 `*/;` 尾巴。3. 真正的 declared function import 仍只进入 imported-function 路径，不会被 module import resolver 误收。 |

### Issue-38：hot reload 只用 `RelativePath` 标识脚本，跨 root 的同名文件会互相覆盖状态并错配模块

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 361-365，408-419；1091-1094，1949-2014，2298-2331，2859-2888 |
| 问题 | 已验证事实：`FFilenamePair` 只保存 `AbsolutePath` 和 `RelativePath`，而 `FileHotReloadState` 直接以裸 `RelativePath` 为 key。`FindAllScriptFilenames()` 遍历每个 script root 时都传入空的 `RelativeRoot`，所以 project root 与任意 content plugin 只要都存在 `Gameplay/Foo.as`，生成的 `RelativePath` 都是同一个 `Gameplay/Foo.as`。后续 `CheckForFileChanges()` 用这个 key 读写时间戳，dependency check 又在 2298-2331 行把 `Section.RelativeFilename` 建成 `RelativeFileToModule`，`DiscardModule()` 也在 1091-1094 行按 `Section.RelativeFilename` 删除 hot reload 状态。换句话说，热重载路径对“文件身份”的定义没有包含 root 身份。 |
| 根因 | 运行时 discovery 把“root 内相对路径”误当成全局唯一文件键，hot reload 账本和模块回查都没有把 `SourceRoot`/physical path 纳入 identity。 |
| 影响 | 只要 project root 和 plugin root 出现同名相对路径脚本，两个物理文件就会共享一份时间戳状态和 module lookup key：修改其中一个文件可能命中另一个模块，丢弃其中一个模块还会把另一个文件的 watch state 一并移除。结果是 hot reload 是否触发、触发到哪个模块、失败后重试哪份文件，都会偏离真实文件集。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 hot reload 文件身份升级为“root identity + canonical relative path + canonical absolute path”的复合键，禁止再用裸 `RelativePath` 跨 root 复用状态。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中新增显式脚本源标识，例如 `FScriptSourceKey { FString RootPath; FString RelativePath; }` 或等价的 canonical root key；`FFilenamePair` 扩展保存该字段。2. 修改 `FindAllScriptFilenames()` / `FindScriptFiles()`，为每个 root 生成稳定的 `SourceRootKey`，即便 `RelativePath` 相同，也能区分来自 project root 还是 plugin root。3. 把 `FileHotReloadState` 的 key 从 `FString` 改为新的复合键；`CheckForFileChanges()`、`DiscardModule()`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles` 和 dependency check 全部统一消费这份 key。4. 重写 `RelativeFileToModule` 为 `SourceKeyToModule`，不要再让 `Section.RelativeFilename` 单独承担模块回查职责；`Module->Code` section provenance 也应同步带上 `SourceRootKey`。5. 在 diagnostics 中同时展示 `RelativePath` 与 root/source 信息，避免 duplicate relative path 报错时只给出一个含糊路径。6. 在 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` 和 hot reload 场景测试中增加 project/plugin 双 root 同名脚本 fixture，断言修改/删除其中一份时只影响对应模块；再补一条 `DiscardModule()` 回归，确认不会顺手删除另一 root 的 watch state。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦 root identity 进入 `FFilenamePair` 与 section provenance，precompiled schema 和部分测试夹具也要同步升级；若 live/cache 两条路径只修其中一边，文件身份仍会再次分叉。 |
| 前置依赖 | 建议与 `Issue-21`、`Issue-27` 联动，统一 root identity 在 live hot reload、precompiled cache、debug section 回查中的合同。 |
| 验证方式 | 1. project root 与 plugin root 各放一份 `Gameplay/Foo.as`，只改 plugin 版本时，断言只重编译 plugin 模块。2. 丢弃或重载 project 版本时，plugin 版本的 `FileHotReloadState` 仍保留。3. 失败重试集合和 full-reload 队列在双 root 同名场景下仍能稳定区分两份脚本。 |

### Issue-39：轮询式 hot reload 永远发现不了脚本删除，非 watcher 路径会把已删模块继续留在依赖图里

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp` |
| 行号 | 1668-1690，2750-2755，2859-2888；59-76 |
| 问题 | 已验证事实：checker thread 启动后只会周期性调用 `CheckForFileChanges()`；`CheckForHotReload()` 也确实会消费 `FileDeletionsDetectedForReload`。但 `CheckForFileChanges()` 自身只是重新枚举当前仍存在的 `.as` 文件，然后按时间戳把它们加入 `FileChangesDetectedForReload`，完全没有扫描“这次枚举里消失了哪些旧 key”，因此不会向 `FileDeletionsDetectedForReload` 写任何内容。当前仓库里唯一会填充删除队列的实现，位于 editor 的 directory watcher 回调 `AngelscriptDirectoryWatcherInternal.cpp:59-76`。这意味着只要运行在没有 directory watcher 的轮询路径，删除 `.as` 文件就不会进入 hot reload 队列。 |
| 根因 | hot reload 轮询器只实现了“新增/修改检测”，没有把上一轮已知文件集合与本轮实际枚举结果做差；删除语义被隐式外包给 editor-only watcher。 |
| 影响 | 非 watcher 场景下，provider 脚本被删掉后，模块系统不会触发对应 module discard、空源码预处理或依赖传播；显式 `import`/automatic dependency graph 仍会继续引用旧模块，直到更晚的全量重启或其他偶然变更才暴露问题。对热重载语义而言，这等于把“模块删除”从增量编译合同里彻底漏掉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 polling 路径自己维护“已知脚本集合”并生成删除事件，使 watcher 和 checker thread 都遵守同一份 add/change/delete 合同。 |
| 具体步骤 | 1. 在 `CheckForFileChanges()` 中引入本轮 `SeenFiles` 集合，key 必须复用 `Issue-38` 的复合文件身份，而不是裸 `RelativePath`。2. 每轮扫描结束后，对 `FileHotReloadState` 的历史 key 做差集：凡是未出现在 `SeenFiles`、但上轮存在的脚本，都生成 `FFilenamePair` 删除事件并推入 `FileDeletionsDetectedForReload`。3. 删除事件入队后，要同步移除或标记对应的 `FileHotReloadState`，避免下一轮继续把已删文件当成“历史存在但无事件”。4. `CheckForHotReload()` 继续沿用现有“延迟 0.2 秒合并删除事件”逻辑，但要补测试覆盖“仅删除、无新增”的场景，确保它不会因为 `FileChangesDetectedForReload` 为空而漏掉 reload。5. 对 `PerformHotReload()` 增加一条 regression：删除 provider 脚本后，consumer module 必须被纳入受影响集合，不能继续保留旧 provider。6. 将 editor watcher 与 polling 的删除事件格式收口到同一 helper，例如 `QueueDeletedScript(const FFilenamePair&)`，避免两条路径未来再次分叉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp`，`Plugins/Angelscript/Source/AngelscriptEditor/Private/Tests/AngelscriptDirectoryWatcherTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果删除检测仍基于旧的 `RelativePath` key，会和 `Issue-38` 一样在双 root 场景下误删错误模块；这两项最好一起回归。 |
| 前置依赖 | 建议与 `Issue-38` 同步实施，复用统一的文件身份 key。 |
| 验证方式 | 1. 在启用 checker thread、关闭 directory watcher 的测试环境中删除单个脚本，确认 `FileDeletionsDetectedForReload` 非空且会触发 reload。2. 删除一个被其他模块 `import` 的 provider，确认 consumer 被纳入同一轮受影响集合。3. “删除后立即重建同名文件”仍应遵守 0.2 秒 rename window，不重复生成脏删除事件。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-37 | Defect | 先修，作为 `import` 词法解析的 correctness hotfix，避免合法 import 被注释内容静默吞掉 |
| P1 | Issue-38 | Defect | 紧随其后，统一 hot reload 的文件身份 key，先消除跨 root 同名脚本的状态覆盖 |
| P1 | Issue-39 | Defect | 与 `Issue-38` 同批落地，让 polling 路径补齐删除事件，恢复模块删除与依赖失效传播 |

---

## 发现与方案 (2026-04-08 17:18)

### Issue-40：删除或读失败脚本会以“空模块占位”继续留在模块图里，`import` 与 swap-in 都不再对缺失 provider fail-fast

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp` |
| 行号 | 91-137，289-305；2911-2938，3173-3204；41-55 |
| 问题 | 已验证事实：`AddFile()` 对 `bTreatAsDeleted` 直接把 `File.RawCode` 置空；同步读失败也只打 warning `"Treating file as deleted"`，但不会阻止后续流程。`Preprocess()` 无论 `ProcessedCode` 是否为空，都会照常生成带原始 `RelativeFilename/AbsoluteFilename` 的 `FCodeSection` 并写入 `Module->Code`。随后显式 `import` 编译路径在 `CompileModules()` 里只要 `FoundModule.IsValid()` 就把该 provider 视为“已找到”；而 `SwapInModules()` 又会把任何 `ModuleDesc` 无条件写回 `ActiveModules`，即使它只是一个空脚本占位。测试侧 `FAngelscriptClassGeneratorEmptyModuleSetupTest` 还把“空模块默认是 `SoftReload`”写成了显式合同。 |
| 根因 | 当前模块系统把“模块存在”建模成“是否有同名 `FAngelscriptModuleDesc` 条目”，而不是“源码成功读取并产出有效预处理结果”；删除/读失败语义被折叠成了一个仍可参与 import、reload、swap-in 的空描述符。 |
| 影响 | provider 脚本删除或暂时不可读后，consumer 的 `import` 不会稳定落到“找不到模块”这条主错误，而是可能继续编译并在更后面报缺类型/缺函数噪音；更糟的是，这个空模块还会覆盖旧 `ActiveModules` 条目，使模块图看起来像“provider 仍然存在，只是内容为空”，直接削弱错误恢复和 hot reload 一致性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“脚本缺失/读取失败”从普通空源码模块中分离出来，建立显式的 missing/invalid module 状态，禁止它继续参与 import 命中和正常 swap-in。 |
| 具体步骤 | 1. 在 `FAngelscriptModuleDesc` 或 `FAngelscriptPreprocessor::FFile` 上新增明确状态，例如 `EScriptSourceState { Ok, Missing, ReadFailed }`；`AddFile()` 在 `bTreatAsDeleted` 和读失败路径分别写入该状态，而不是仅靠空 `RawCode` 隐式表达。2. `Preprocess()` 在生成 `Module->Code` 前先检查该状态：`Missing/ReadFailed` 模块不要按正常脚本生成可编译 `CodeSection`，而是只保留用于诊断/清理的 provenance 元数据，并显式标记 `bCompileError` 或 `bIsMissingProvider`。3. `CompileModules()` 的显式 import 查找改成“provider 必须存在且 `SourceState == Ok`”；若命中的 descriptor 只是 missing/read-failed 占位，直接在 import 行报 `provider missing or unreadable`，并阻止 consumer 进入 stage1。4. `SwapInModules()` 在写回 `ActiveModules` 前过滤 missing/read-failed 模块：删除语义应走 discard/remove 路径，而不是用空模块覆盖旧模块；读失败语义则应保持旧模块存活并把当前 reload 标记为失败。5. 将 `FAngelscriptClassGeneratorEmptyModuleSetupTest` 改成两类测试：纯测试 scaffold 的空模块仍可单独验证 `SoftReload`，但来自 preprocessor 缺失/读失败路径的模块必须触发“不可正常 swap-in”的断言，避免错误合同继续固化。6. 在 hot reload 场景测试中分别覆盖“真实删除 provider”“暂时读失败 provider”“consumer import missing provider”三条路径，确认缺失模块不会继续进入 `ImportedModules` 的可导入集合，也不会覆盖 `ActiveModules`。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | L |
| 风险 | 若直接把所有空模块都视为 missing，可能误伤测试里手工构造的 scaffold module；需要把“测试构造的空模块”和“来自真实脚本源的空模块”在状态上明确区分。 |
| 前置依赖 | 建议与 `Issue-17`、`Issue-39` 一起落地，复用 provider failure propagation 与删除事件传播逻辑。 |
| 验证方式 | 1. 删除 provider 脚本后，consumer 应在 import 阶段得到明确的 missing-provider 诊断，而不是继续编译。2. 模拟读失败时，旧 `ActiveModules` 保持不变，当前坏模块不会覆盖进活动模块表。3. 更新后的 class-generator 测试应区分“测试 scaffold 空模块”和“真实缺失模块”，两者行为不再混淆。 |

### Issue-41：reload 差异分析只对 `removed class` 建了撤销路径，`enum/delegate` 整体删除后不会得到对称清理

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | 1575-1627，1771-1807，1856-1868，2145-2185，2378-2389 |
| 问题 | 已验证事实：`InitEnums()` 只遍历新 `ScriptModule` 里当前仍存在的 enum，`SetupModule()` 里的 delegate 处理也只遍历 `NewModule->Delegates`。`Setup()` 阶段对“旧模块里有、新模块里没有”的显式处理只有 1856-1868 行的 `RemovedClasses` 检查。执行阶段也是同样的不对称：`PerformReload()` 只对 `RemovedClasses` 调用 `FullReloadRemoveClass()`，收尾阶段也只对 `RemovedClasses` 调用 `CleanupRemovedClass()`；对 enum/delegate 没有任何 `RemovedEnums/RemovedDelegates` 等价结构或清理分支。 |
| 根因 | reload 差异模型围绕 class/struct 建模，默认假设 enum/delegate 只有“新增/变更”两类变化，没有把“整个符号消失”视为需要显式撤销的状态迁移。 |
| 影响 | 一旦脚本删除文件、读失败降级为空模块，或正常重构移除了某个 enum/delegate，reload 管线不会像 removed class 那样触发对称清理。结果是旧的 reflected enum/delegate 可能继续留在引擎侧类型表里，而新模块描述符已经不再声明它，形成 module graph 与 Unreal 类型状态漂移。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 enum/delegate 的“删除”提升为一等 reload 事件，补齐与 `RemovedClasses` 对称的分析、full-reload 和 cleanup 流程。 |
| 具体步骤 | 1. 在 `FAngelscriptClassGenerator::FModuleData` 中新增 `RemovedEnums`、`RemovedDelegates` 或等价集合，数据结构至少保存旧描述符、来源模块和触发行号。2. 在 `Setup()`/`SetupModule()` 期间对 `OldModule->Enums` 与 `OldModule->Delegates` 做差集：凡是在 `NewModule` 中找不到同名项的，都写入新集合，并把 `ReloadReq` 至少提升到 `FullReloadRequired` 或明确的 enum/delegate removal 等级。3. 参照 `FullReloadRemoveClass()` 补齐 `FullReloadRemoveEnum()`、`FullReloadRemoveDelegate()` 与必要的 `CleanupRemovedEnum/Delegate()`，确保旧 script type、反射对象和任何注册表缓存都能被显式撤销。4. 在 `PerformReload()` 的创建/链接阶段和收尾阶段，把这两类 removed-item 纳入与 class 相同的顺序化处理，而不是只处理“仍然存在的新条目”。5. 将 `Issue-40` 的 missing/read-failed module 状态接入这里：当模块因删除脚本或读失败而没有任何声明时，enum/delegate removal 也应沿同一条 full-reload 路径传播，不再仅靠 removed class 兜底。6. 新增 hot reload 回归：删除只包含 enum 的模块、只包含 delegate 的模块，以及把 enum/delegate 从混合模块中移除，断言 reload 后旧 reflected symbol 不再可见，且不会继续留在模块描述符或全局注册表中。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | L |
| 风险 | enum/delegate 的撤销语义如果实现得比 class 更弱，可能会出现“描述符删了但旧 `ScriptType` 仍存活”的半清理状态；需要在 full-reload 与 post-reload 两个阶段都做一致性校验。 |
| 前置依赖 | 建议与 `Issue-40` 同步推进，统一“模块删除/读失败 -> reflected symbol 删除”的传播路径。 |
| 验证方式 | 1. 删除只含 enum 的脚本后，旧 enum 不再能通过引擎查询命中。2. 删除只含 delegate 的脚本后，旧 delegate 的 script type 与绑定信息都被移除。3. 混合模块里仅移除 enum/delegate 时，reload requirement 会升级并走明确的撤销流程，而不是静默 soft reload。 |

### Issue-42：`namespace` 在确认 `{` 之前就直接入栈，缺失作用域时会把整段后续代码静默归到错误命名空间

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 行号 | 3159-3165，3758-3765，3821-3824，3931-3940；20-26 |
| 问题 | 已验证事实：仓库现有 namespace 正向用例明确采用 `namespace MyNamespace { ... }` 这种带花括号的语法。但 `ParseIntoChunks()` 在扫到 `namespace` 且后面是空白时，会立即执行 `NamespaceStack.Add(ReadIdentifier(...))`，根本不检查后面是否真的跟了 `{`。之后 `SubmitChunk()` 会无条件把 `NamespaceStack` 拼成 `Chunk.Namespace`，而弹栈只发生在顶层 `}` 命中时；文件结束处也只检查 `IfDefStack` 是否平衡，不检查 `NamespaceStack` 是否为空。结果是 `namespace Foo` 这种缺少 `{` 的语法错误不会在预处理阶段 fail-fast，后续 top-level chunk 会被持续打上伪造的 `Foo` 命名空间。 |
| 根因 | namespace 解析把“看到了关键字”和“确认进入 namespace block”混成了同一个状态转移，缺少 brace-aware 的提交条件和 EOF 平衡校验。 |
| 影响 | 一个本应在词法/语法边界立即报出的错误，会被降级成更隐蔽的语义漂移：类、函数、全局变量和生成代码都可能被放进错误 namespace，最后只在更晚的符号解析阶段表现成误导性的找不到符号或命名空间不匹配。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 namespace 改成“两阶段解析”：先识别 header，只有在确认后继 `{` 后才提交入栈；EOF 时再对 namespace 栈做显式平衡校验。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取 `TryParseNamespaceHeader()` 或并入 `Issue-3` 的 cursor helper，读取 `namespace` 后的 identifier，并在跳过空白/注释后强制检查下一个结构字符必须是 `{`。2. 将 `NamespaceStack.Add(...)` 延后到 `{` 已确认的分支，不再在看到关键字瞬间修改全局解析状态；若 header 不完整，立即在当前行报出“namespace missing `{`”之类的明确诊断，并阻止后续 chunk 继续继承伪造 namespace。3. 为 namespace frame 保存进入时的 `ScopeCount`，`}` 出栈时先核对它确实关闭了对应 namespace block，而不是仅凭 `IsTopLevelScope()` 粗略弹栈，避免未来 class/enum 嵌套与 namespace 交错时再次错配。4. 在文件结束处新增 `NamespaceStack` 平衡检查；若仍有未关闭 namespace，和 `IfDefStack` 一样给出缺失 `}` 的 preprocessor-level 错误，而不是把错误拖到后续编译器。5. 新增自动化：保留现有正向 namespace 用例，再补 `namespace Foo`、`namespace Foo // missing brace`、`namespace Foo { class A {} ` 这类负向样例，断言预处理阶段直接报错且 `Chunk.Namespace` 不会污染后续声明。6. 如果后续继续拆分 `ParseIntoChunks()`，把 namespace header/body 的解析状态并入统一的 scope-aware cursor，避免再次由裸 `Strncmp + ReadIdentifier` 驱动结构语法。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | M |
| 风险 | 若只在关键字处补一个局部判断而不引入 scope-aware frame，后续复杂嵌套场景仍可能靠错误的 `IsTopLevelScope()` 弹栈；最好和 parser helper 拆分一起做。 |
| 前置依赖 | 建议与 `Issue-3` 同步实施，复用共享 cursor/scope helper。 |
| 验证方式 | 1. 缺失 `{` 的 namespace 在预处理阶段直接报错，不再进入后续 chunk 分析。2. EOF 缺失 `}` 的 namespace 会得到明确的未闭合诊断。3. 现有 `Misc.Namespace` 正向用例保持通过，证明修复没有破坏合法 namespace 语义。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-40 | Defect | 先修，先把“缺失/读失败脚本仍作为 provider 存活”的错误合同切掉，恢复 import 与 swap-in 的 fail-fast |
| P1 | Issue-41 | Defect | 紧随其后，与 `Issue-40` 联动补齐 enum/delegate 的删除传播和清理 |
| P1 | Issue-42 | Defect | 在结构性缺陷收口后尽快修复，避免 namespace 语法错误继续静默污染后续 chunk 和符号解析 |

---

## 发现与方案 (2026-04-08 17:23)

### Issue-43：单个失败模块会让整轮 `#restrict usage` 校验直接短路

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3178-3204，3887-3889，4523-4566 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 43，这里补齐执行方案。已验证事实：`CompileModules()` 在显式 `import` 缺失 provider 时，会把当前模块标记为 `bCompileError` 并跳过 `CompileModule_Types_Stage1()`，留下 `ScriptModule == nullptr` 的 module desc。随后同一轮编译即使已经 `bHadCompileErrors == true`，仍会继续调用 `CheckUsageRestrictions(CompiledModules)`。而 `CheckUsageRestrictions()` 在遍历当前批次模块、`ActiveModules` 和最终检查列表时，只要遇到一个 `ScriptModule == nullptr` 或 `Module->Code.Num() == 0`，就直接 `return` 整个函数，而不是跳过当前坏模块继续检查其他模块。 |
| 根因 | restriction 校验把“某个模块当前不可检查”实现成了全局退出；调用点又没有在 compile-error 场景先筛掉失败模块或延后执行 restriction 校验。 |
| 影响 | 任何与 restriction 无关的局部失败，例如缺失 `import`、空模块占位、cache 恢复出的空 `Code`，都可以让同批次其余模块的 `#restrict usage` 合同完全失效。这样模块边界约束是否被验证，不再取决于 restriction 本身，而取决于本轮是否碰巧还有别的模块失败。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 restriction 校验改成“跳过坏模块、继续检查好模块”的容错遍历，并把失败模块从输入集合中显式过滤。 |
| 具体步骤 | 1. 在 `CheckUsageRestrictions()` 中把三处 `if (ScriptModule == nullptr) return;` / `if (Module->Code.Num() == 0) return;` 全部改为 `continue;`，必要时记录 verbose/warning，指出某个模块因编译失败或缺失源码 section 被跳过。2. 在 `CompileModules()` 的 `CheckUsageRestrictions(CompiledModules)` 调用前，先构造 `RestrictionEligibleModules`：仅包含 `!bCompileError`、`ScriptModule != nullptr`、`Code.Num() > 0` 的模块，避免把显然不可检查的 descriptor 送进校验函数。3. 若 `ActiveModules` 中允许存在 precompiled/占位/缺失源码模块，为 `FAngelscriptModuleDesc` 增加显式可校验状态或复用前面已规划的 source-state，禁止再用 `Code.Num()==0` 这种隐式条件驱动全局控制流。4. 在 restriction 诊断里补一条统计或 trace，明确输出“本轮跳过了多少个不可校验模块”，避免后续再次静默短路。5. 为 `#restrict usage` 新增回归：构造三模块场景，其中 `A` 因缺失 import 编译失败、`B` 合法、`C` 违反 `B` 的 restriction；修复后应同时看到 `A` 的 import 错误和 `C` 的 restriction 错误，而不是只看到第一类错误。6. 对 fully precompiled / 空 `Code` 路径回归，确认存在不可校验 active module 时，其余带 restriction 的模块仍然继续受检。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptBuilderTests.cpp` 或等价 compiler pipeline 测试文件 |
| 预估工作量 | M |
| 风险 | 如果历史上有人依赖“出现首个编译失败后跳过所有 restriction 错误”的非正式行为，修复后同一轮会暴露更多真实错误；但这属于诊断收口，不是语义回退。 |
| 前置依赖 | 无；可先独立修复，随后再与 precompiled metadata/empty module 方案联动收口。 |
| 验证方式 | 1. 自动化复现“一个坏模块 + 一个 restriction 违规模块”的批量编译，确认两类错误同时出现。2. 在 active module 集合里混入 `Code.Num()==0` 的缓存/占位模块，确认 restriction 校验不再整轮静默跳过。3. 回归无 restriction 的普通编译路径，确认仅控制流变化，没有引入额外误报。 |

### Issue-44：`FFile` 在预处理流水线中同时保留多份整文本文本，峰值内存与字符串复制成本被线性放大

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Refactoring |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 120-149，3150-3155，3983-4142 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 44，这里补齐执行方案。已验证事实：`FFile` 同时持有 `RawCode`、`ChunkedCode`、`ProcessedCode`、`GeneratedCode` 四套文本载体；`ParseIntoChunks()` 每次提交 chunk 时都会执行 `Chunk.Content = File.RawCode.Mid(...)`，把原始源码切片复制进每个 `FChunk`；`CondenseFromChunks()` 再把所有 `Chunk.Content` 重新拼成 `ProcessedCode`；随后 `PostProcessRangeBasedFor()` 与 `PostProcessLiteralAssets()` 又各自构造一份 `NewCode` 再整体回写 `File.ProcessedCode`。额外验证：当前仓库执行 `rg --files -g '*.as'` 统计到 325 个脚本文件，说明这条复制链已经处在会放大量级的真实工程规模上。 |
| 根因 | 预处理器当前用“每个阶段保留一份完整字符串快照”的方式表达文本变换，而不是用共享切片、区间视图或可增量提交的 token/segment 流。 |
| 影响 | 预处理阶段在真正进入 AngelScript 编译前就会反复复制整份脚本内容，抬高内存峰值和 allocator 压力；这条成本还会与 `Issue-9` 的 `FFile` 排序复制叠加，直接削弱后续增量预处理、artifact cache 和大脚本集性能优化的收益。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 `FFile` 从“多份文本快照容器”收敛为“原始源码 + 轻量变换视图”，仅在最终提交前物化一次完整 `ProcessedCode`。 |
| 具体步骤 | 1. 把 `FChunk.Content` 从 owning `FString` 改成 `SourceRange + ReplacementBuffer` 风格结构，至少先保存 `ChunkStartPos/ChunkEndPos` 指向 `RawCode`，只对发生替换或生成代码插入的 chunk 维护最小增量文本。2. 重写 `CondenseFromChunks()`，让它基于 `RawCode` 区间和 replacement/append 列表一次性流式生成 `ProcessedCode`；不要再要求每个 chunk 预先持有一份完整 substring。3. 将 `PostProcessRangeBasedFor()` 与 `PostProcessLiteralAssets()` 统一成 append-to-builder 的后处理 pass，避免每个 pass 都再创建整份 `NewCode`；如需 regex，可把 match 结果记录为 patch list，最后合并应用。4. 在 `FFile` 上增加轻量统计信息，例如 `RawCodeBytes`、`MaterializedChunkBytes`、`ProcessedCodeBytes`，为后续 profiling 提供直接指标，确认“字符串复制次数下降”而不是只感觉更快。5. 若后续落地 `Issue-24` 的 artifact cache，把“chunk 视图 + patch list”直接作为可缓存中间产物，避免 cache 命中后还得重建多份字符串。6. 补充基准与回归：用 import-heavy fixture 和真实脚本集比较预处理前后峰值内存、总分配次数、`ProcessedCode` 等价性，保证结构收敛没有改变语义输出。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | L |
| 风险 | 文本持有模型调整会触及大量位置索引和 replacement 偏移计算；如果没有先锁住 `ProcessedCode` 等价回归，容易把列号、宏替换和 import blanking 悄悄改坏。 |
| 前置依赖 | 建议先保持 `Issue-3` 的 parser/cursor 抽分方向一致，避免一边拆 lexer，一边继续加深字符串快照耦合。 |
| 验证方式 | 1. 对同一脚本集比较改造前后的 `ProcessedCode`、`ImportedModules`、`UsageRestrictions` 和诊断行号，确认语义完全一致。2. 统计预处理阶段峰值内存与总分配次数，确认明显下降。3. 回归大脚本集和 hot reload fixture，确认没有引入新的悬挂区间或 replacement 越界。 |

### Issue-45：`#restrict usage` 的 pattern 读取不识别 `//` 注释，注释尾巴会被写进模块限制

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 3363-3389，4313-4325，4349-4356，4580-4596 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 75，这里补齐执行方案。已验证事实：`#restrict usage allow/disallow` 当前通过 `ReadUntilWhitespace()` 读取 pattern，而这个 helper 只把 `'\n'`、`'\t'` 和空格当成终止符，不识别 `//` 注释起始；紧接着 `KillRawLine()` 却会在遇到第一个 `/` 时停止 blank 当前行。结果是像 `#restrict usage allow Gameplay.*//temporary` 这样的单行注释写法，会把 `Gameplay.*//temporary` 整体写进 `Restriction.Pattern`，源码里留下的却只是 `//temporary` 注释残片。后续 `CheckUsageRestrictions()` 又直接用这个脏 pattern 调 `Module->ModuleName.MatchesWildcard(...)`。 |
| 根因 | restriction directive 的“读取 pattern”和“清理原始源码”使用了两套不一致的词法边界：前者 comment-blind，后者把 `/` 当成 comment 起点。 |
| 影响 | 一条本应生效的 allow/disallow 限制会被静默污染成永远匹配不到或错误匹配的 wildcard，模块边界约束是否触发将取决于注释书写方式，而不是 restriction 本意。因为诊断里只显示 restriction 结果，不显示原始 pattern 文本，这类错误很难从现象反推到 directive 解析。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `#restrict usage` 引入 comment-aware 的 directive body parser，统一 pattern 读取与源码 blank 的终止规则。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取 `ParseRestrictionDirective()` 或并入共享 cursor helper，读取 `allow/disallow` 后的 pattern 时同时识别 `//`、`/* */`、行尾和合法空白，禁止继续使用 `ReadUntilWhitespace()` 的裸扫描。2. 解析得到的 pattern 在写入 `UsageRestrictions` 前必须先经过 comment stripping 和 `TrimStartAndEnd()`，确保最终保存的永远是纯 wildcard 文本，而不是带注释尾巴的原始子串。3. 将 `KillRawLine()` 替换为基于 parser 返回 span 的 `ReplaceWithBlank()` 或等价 helper，让源码清理范围与 pattern 读取范围共享同一条 statement 终点规则，不再出现“模块元数据吃进了注释，源码却没吃进去”的分叉。4. 若 restriction pattern 经过去注释后为空，立即在当前 directive 行报明确错误，例如 `Restriction pattern is empty after stripping comments.`，避免继续写入无意义 wildcard。5. 在 `CheckUsageRestrictions()` 的诊断里附带触发 restriction 的原始 pattern 或其规范化结果，便于排查未来类似边界问题。6. 新增回归测试：`#restrict usage allow Gameplay.*//temporary` 应只记录 `Gameplay.*`；block comment 版本同样应正确；同时保留一条无注释基线，确认匹配行为不变。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果当前项目里已经存在依赖“注释文本被并入 pattern”的偶发脚本，修复后 restriction 匹配结果会改变；需要通过诊断输出帮助用户识别这类旧脚本。 |
| 前置依赖 | 建议复用 `Issue-3` 规划中的共享 cursor/statement parser，避免 `import` 与 `#restrict usage` 再各自维护一套 comment 规则。 |
| 验证方式 | 1. `//` 与 `/* */` 注释版 restriction 只记录纯 pattern，不再携带注释尾巴。2. 同一 restriction 去掉注释前后，`CheckUsageRestrictions()` 的匹配结果完全一致。3. `ProcessedCode` 中不再残留被半截 blank 的 restriction 尾注释。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-43 | Defect | 先修，先恢复 `#restrict usage` 在批量编译失败场景下的持续校验能力 |
| P2 | Issue-45 | Defect | 紧随其后，修复 restriction parser 的注释边界，避免模块边界规则被静默污染 |
| P2 | Issue-44 | Refactoring | 在 correctness 问题收口后推进，为增量预处理和缓存改造先清掉高复制成本 |

---

## 发现与方案 (2026-04-08 17:29)

### Issue-46：重复 `import` 会把依赖哈希异或抵消，模块缓存签名与真实依赖集合失配

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 144-151；463-483；3175-3189，4262-4280 |
| 问题 | 已验证事实：`FFile` 同时保存原始 `Imports` 列表和最终 `Module->ImportedModules`，但 `ProcessImports()` 对每条 `import` 只做线性查找与 `Add()`，没有任何去重或 canonicalization；随后 `CompileModules()` 会按 `ImportedModules` 顺序收集依赖模块，`CompileModule_Types_Stage1()` 再对每个 import 的 `CombinedDependencyHash` 执行一次 `^=`。这意味着脚本里重复写两次同一条 `import Foo.Bar;` 时，同一个依赖哈希会被异或两次并抵消。 |
| 根因 | 预处理器把 `ImportedModules` 当成“出现顺序列表”而不是“规范化后的依赖集合”，编译阶段又直接把这个数组当成依赖哈希输入，没有在任一层做去重。 |
| 影响 | 当前模块的 `CombinedDependencyHash` 会偏离真实依赖图，后续依赖变更检测、artifact cache 和 JIT 相关签名都可能把“依赖已变化”的模块误判成“未变化”；同一依赖也会被重复 `ImportIntoModule()`，额外放大编译期开销。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 import 解析完成后立即生成“去重且保序”的 canonical dependency 列表，并让后续哈希与导入逻辑只消费这份列表。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 为 `ProcessImports()` 增加 per-file `TSet<FString>` 或等价结构，key 使用 `Issue-4` 规划中的 canonical module name，保证同一文件内重复 import 只保留第一次出现。2. 当检测到重复 import 时，不要静默吞掉；记录 warning，指出重复模块名和对应源文件行号，帮助脚本作者清理冗余依赖。3. 将 `File.Module->ImportedModules` 明确为“去重后的最终依赖列表”，后续 `CompileModules()`、`CompileModule_Types_Stage1()` 和任何 cache/hash 逻辑只读取这份列表，不再回看原始 `Imports`。4. 若未来支持 filename-style / relative import，重复判断必须在 canonicalization 之后执行，避免 `Foo.Bar` 与 `./Foo/Bar.as` 被视为两个不同依赖。5. 新增回归测试：构造包含重复 import 的脚本，断言 `ImportedModules` 只有一项、`ImportIntoModule()` 只发生一次、`CombinedDependencyHash` 与单次 import 场景完全一致。6. 补一条 learning trace 或 pipeline trace，验证 warning 文案稳定输出，便于后续排查缓存签名漂移。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果现有脚本意外依赖“重复 import 触发多次 side effect”的非正式行为，去重后会改变行为；需要先确认 import 仅承载模块依赖而不承载额外执行语义。 |
| 前置依赖 | 建议与 `Issue-4` 的 import canonicalization 一起落地，避免先按原始文本去重、后按规范名重新拆出重复。 |
| 验证方式 | 1. 自动化测试比较单次 import 与重复 import 的 `ImportedModules`、`CombinedDependencyHash`、最终编译结果。2. 对 duplicate import 场景启用 trace，确认只导入一次 provider 并产出 warning。3. 回归普通 import 场景，确认合法依赖顺序与诊断不变。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-46 | Defect | 与 `Issue-4` 联动优先修复，先收口 import 列表的正确性，再继续做缓存与增量预处理 |

---

## 发现与方案 (2026-04-08 17:31)

### Issue-47：默认 `automatic import` 模式下，手写 `import` 的兼容剥离和 warning 路径完全失效

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 行号 | 61-67；702-709，1292；232-239，3490-3510，482-490；194-218；118-188 |
| 问题 | 已验证事实：配置默认启用 `bAutomaticImports = true`，`ShouldUseAutomaticImportMethodForCurrentContext()` 直接返回引擎上的该开关；但 `Preprocess()` 只有在 automatic-import 关闭时才会进入 `ProcessImports()`。与此同时，手写 `import` 的两项兼容处理都只写在 `ProcessImports()` 里：一是把 `import` 文本从 chunk 里 `ReplaceWithBlank()`，二是在 automatic-import 开启时发出 `bWarnOnManualImportStatements` warning。结果是默认配置下，`ParseIntoChunks()` 虽然仍会把 `import` 收进 `File.Imports`，但既不会剥离源码中的 `import` 语句，也不会产生迁移 warning。现有 import 测试和 learning trace 也都显式把 `bUseAutomaticImportMethod` 临时改成 `false`，没有覆盖默认路径。 |
| 根因 | backward-compatibility 逻辑被放进了一个只在“非 backward-compatible 模式”才会调用的函数里，调用条件与函数内部语义相互冲突。 |
| 影响 | 旧脚本在默认 automatic-import 配置下不会得到预期的“语句被忽略并提示迁移”行为，手写 `import` 会继续残留到后续编译文本中，导致兼容策略、诊断输出和真实编译输入三者脱节；`bWarnOnManualImportStatements` 也因此在默认路径上退化成死配置。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将“识别并剥离手写 `import`”与“显式 import 依赖排序”拆成两个独立阶段，让 automatic-import 与 manual-import 共享同一套 parser，但执行不同的后续策略。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 新增独立的 `FinalizeImportStatements()` 或等价阶段，负责基于 `File.Imports` 做两件事：从源码中稳定 blank 掉 `import` 语句，以及在 automatic-import 开启时按配置发 warning。2. 将 `ProcessImports()` 收窄为“仅在 manual-import 模式下做依赖解析、循环检测和文件重排”，不再承担 warning/源码剥离职责。3. 调整 `Preprocess()` 顺序：`ParseIntoChunks()` 之后先统一执行 import finalization，再根据 `ShouldUseAutomaticImportMethodForCurrentContext()` 决定是否继续做显式 import 排序。4. 若 automatic-import 模式下决定完全忽略手写 `import`，则必须显式清空或标记 `File.Imports`，避免后续诊断、trace 或增量缓存仍把这些旧语句当成有效依赖。5. 新增测试覆盖默认 automatic-import=true 路径：断言 `import` 不再残留于 `ProcessedCode`，会按配置输出 warning，且 `ImportedModules` 不会误记录成显式依赖；保留 `automatic-import=false` 基线，确保原有 manual-import 行为不变。6. 对 learning trace 增加一条默认配置用例，明确展示 manual import 在 automatic-import 模式下被忽略而非参与排序。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 若项目里已有脚本依赖“automatic-import 开启时手写 import 仍留在源码里”的偶然行为，修复后会暴露新的编译差异；需要借助 warning 和 trace 帮助定位受影响脚本。 |
| 前置依赖 | 无；可独立落地，但最好与 `Issue-4` 的 import canonicalization 共享同一套 import statement span 信息。 |
| 验证方式 | 1. 在默认 automatic-import 配置下运行新的预处理测试，确认 `ProcessedCode` 不再含原始 `import` 行且 warning 可控。2. 在 manual-import 模式下回归现有 import 测试，确认依赖排序和 `ImportedModules` 结果不变。3. 对同一脚本分别在两种模式下比较 trace，确认差异只体现在依赖解析策略，而不是解析稳定性。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-47 | Defect | 独立优先修复，先让默认配置下的 manual-import 兼容行为与配置语义重新一致 |

---

## 发现与方案 (2026-04-08 17:34)

### Issue-48：重名模块没有 fail-fast，且文件发现顺序不稳定，导致 `import` 解析与激活模块身份随枚举顺序漂移

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 行号 | 1944-2014，2075-2079，2907-2938，3120-3136；463-470，494-497；258-273 |
| 问题 | 已验证事实：`FindScriptFiles()` 直接消费 `FindFiles()` 返回顺序，对目录还通过 `for (const FString& FoundDirectory : TSet<FString>(LocalDirs))` 递归，没有任何稳定排序；`FindAllScriptFilenames()` 之后又按这个原始顺序逐个 `Preprocessor.AddFile()`。如果两个文件最终规范化成同一 `ModuleName`，`ProcessImports()` 会在线性扫描 `Files` 时拿到第一个命中的 provider，而编译阶段只是 `ensureMsgf(!CompilingModulesByName.Contains(...))` 后继续 `Add` 到单 key map，`SwapInModules()` 也继续按模块名写入 `ActiveModules`，整个流程都没有显式拒绝 duplicate module。现有 discovery 测试只把结果塞进 `TSet` 断言成员，不校验顺序，也没有覆盖重名模块冲突。 |
| 根因 | 模块系统把“扫描顺序”当成隐式仲裁器，却没有先把文件发现顺序稳定化，也没有定义 duplicate module 的统一冲突合同。不同阶段分别使用“线性首个命中”和“单 key 覆盖式 map”，形成了彼此不一致的解析策略。 |
| 影响 | 同一个重名模块冲突在不同机器、不同文件系统枚举顺序，甚至同一机器不同阶段里，都可能落到不同 provider。结果不仅 `import` 依赖图和最终 `ActiveModules` 身份可能不一致，热重载、诊断定位和缓存命中也会随枚举顺序漂移，属于典型的 nondeterministic module-system 行为。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 duplicate module 从“顺序驱动的隐式选择”改成“预处理前显式报错”，同时把文件发现顺序稳定化，仅用于诊断可重复性而非决定赢家。 |
| 具体步骤 | 1. 在 `FindScriptFiles()`/`FindAllScriptFilenames()` 结束前，对 `LocalFiles`、`LocalDirs` 和最终 `OutFilenames` 统一做 canonical sort，消除文件系统枚举带来的 nondeterminism；`TSet` 去重后要回到排序数组再递归。2. 在 `Preprocessor.AddFile()` 或 `Preprocess()` 起始阶段建立 `ModuleName -> [RelativeFilename...]` 的多值索引，一旦某个模块名对应多个源文件，立即生成 compile/preprocess 级错误，列出所有冲突文件并终止本轮处理，而不是继续让后续阶段隐式选一个。3. 将 `ProcessImports()` 改为只接受已经通过唯一性验证的 module index，不再在线性数组中“遇到第一个就算命中”；这样 import provider 身份与后续编译阶段使用的索引保持一致。4. 对 `CompilingModulesByName` 和 `ActiveModules` 的写入增加 hard failure 或 `checkf` 级保护，确保 duplicate module 不会越过前面的验证继续污染运行时状态。5. 新增测试：构造两个不同 root 下映射到同一 module name 的脚本，分别交换发现顺序或手工调换 `AddFile()` 顺序，修复后都应稳定报同一条 duplicate-module 错误，而不是成功编译其中之一。6. 补一条 file-system trace，记录排序后的发现顺序，确保未来修改扫描逻辑时 determinism 可回归。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果现有工程里已经长期存在 duplicate module 但恰好依赖当前扫描顺序，修复后会从“偶然可用”变成显式报错；需要提供清晰的冲突文件列表和迁移指引。 |
| 前置依赖 | 建议复用 `Issue-4` 的 canonical import/module name 规则，先统一“什么叫同一个模块”，再做 duplicate 检测。 |
| 验证方式 | 1. 交换文件发现顺序或 `AddFile()` 顺序后，duplicate module 诊断保持完全一致。2. 冲突存在时 `Preprocess()`/编译流程 fail-fast，不再生成部分 `ImportedModules` 或污染 `ActiveModules`。3. 无冲突脚本集回归，确认排序只改变 determinism，不改变合法模块编译结果。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-48 | Architecture | 与 `Issue-4`、`Issue-46` 联动推进，先定义唯一模块合同，再做 import 与缓存稳定化 |

---

## 发现与方案 (2026-04-08 17:45)

### Issue-49：`automatic import` 热重载依赖扩散仍靠固定点全表扫描，模块数增长后会把 reload 前置分析放大成多轮 `O(V*E)`

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 2335-2368 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 36，这里补齐执行方案。已验证事实：`PerformHotReload()` 在 `automatic import` 路径里，先把直接命中的模块放进 `MarkedModules`，随后通过 `while (bDidMarkModules)` 反复线性扫描全部 `ActiveModules`；对每个未标记模块，再线性遍历其 `ScriptModule->moduleDependencies`，只要命中已标记依赖就把当前模块加入集合并开启下一轮。当前仓库实测 `rg --files -g "*.as"` 可见 325 个脚本文件，这意味着依赖传播成本会随着模块数和依赖边一起增长，而且发生在真正重新编译之前。 |
| 根因 | 模块系统已经有正向 `moduleDependencies`，却没有为 hot reload 建立反向依赖索引或队列式遍历入口，只能靠“全表扫描直到收敛”求出受影响模块闭包。 |
| 影响 | 当依赖链较长或模块数继续增长时，单个脚本变更也会先触发多轮全图遍历，直接拉长编辑器内 hot reload latency；这条成本与 explicit `import` 路径上的解析/排序开销叠加，会让模块系统在预处理之前就先出现明显的可感知卡顿。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 hot reload 依赖扩散从“固定点全表扫描”改成“反向依赖图 + queue/BFS”，并让索引在模块 swap-in 时增量维护。 |
| 具体步骤 | 1. 在 `FAngelscriptEngine` 中新增运行时 `ReverseModuleDependencies` 索引，key 为 provider module，value 为直接 dependent modules；首版可在 hot reload 开始前根据当前 `ActiveModules` 和 `moduleDependencies` 一次性构建。2. 将 `automatic import` 路径的传播逻辑改成 queue/BFS：先把直接命中的模块入队，再沿反向依赖边逐层扩散，每个模块只访问一次，彻底删除 `while (bDidMarkModules)` 的多轮全表扫描。3. 若 `Issue-36` 的 `ResolvedModuleDependencies` 落地，优先以持久化后的 canonical 依赖图重建 `ReverseModuleDependencies`，不要继续从 live `asCModule*` 临时推导字符串身份。4. 在 `SwapInModules()`、discard path 和 full reload cleanup 里同步维护这份反向索引，保证后续热重载不需要每轮都重新扫完整图；若短期内先接受“每轮重建一次索引”，也应把复杂度收敛到单次 `O(V+E)`。5. 为热重载 trace 增加统计字段，例如“visited modules”“reverse-edge lookups”“reload propagation ms”，方便确认新实现确实把传播成本从多轮扫描降到单轮遍历。6. 新增性能/正确性回归：构造链式依赖和扇出依赖两类 fixture，断言 provider 变化时所有 dependent modules 仍被正确标记，同时遍历次数不再随轮次倍增。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningFileSystemAndModuleTraceTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 预估工作量 | M |
| 风险 | 若反向依赖索引在 swap-in / discard 后没有同步更新，热重载会漏重编某些 consumer；因此第一阶段应优先用“每轮重建一次反向图 + BFS”验证正确性，再做长期缓存。 |
| 前置依赖 | 若已开始实施 `Issue-36` 的 resolved dependency contract，建议复用同一套 canonical 依赖身份；否则可先基于现有 `moduleDependencies` 落地算法改造。 |
| 验证方式 | 1. 针对链式依赖 `A <- B <- C <- D` 修改 `A`，确认 `B/C/D` 全部进入 `FilesToHotReload`。2. 对比改造前后的 trace，确认模块访问次数从多轮全表扫描下降为单轮 `O(V+E)` 遍历。3. 在 325 文件脚本树上记录 hot reload 前置分析耗时，确认大规模脚本集不再因依赖传播阶段产生明显倍增。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-49 | Architecture | 在 `Issue-36` 的依赖合同稳定后推进，先把 hot reload 传播算法从全表扫描收敛到反向图遍历 |

---

## 发现与方案 (2026-04-08 17:46)

### Issue-50：同步读盘失败在 `AddFile()` 内串行 `Sleep` 重试，短暂 I/O 故障会把首编译和 hot reload 直接拖进主线程卡顿

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 91-137；2072-2082；2448-2455 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 46，这里补齐执行方案。已验证事实：`AddFile()` 的同步路径在 `LoadFileToString()` 失败后，会在同一调用线程内继续重试 6 次，并按 `0.01s + 0.1s + 0.2s + 0.2s` 进行串行 `Sleep` backoff，单文件彻底放弃前最坏额外阻塞约 `0.51s`。首编译路径会在 `FindAllScriptFilenames()` 之后循环调用 `Preprocessor.AddFile(...)`，hot reload 也会对 `FilesToHotReload` 逐个调用同一入口；因此多个暂时打不开的脚本文件会把这段等待线性叠加到预处理前的主路径上。重试结束后，该路径仍只打印 warning 并落回 `"Treating file as deleted"` 语义。 |
| 根因 | 文件读取失败被实现成“同步 sleep 重试”的局部策略，而不是显式的 I/O 失败状态或由更高层统一调度的重试合同；错误恢复与主线程时延被耦合在 `AddFile()` 这个基础入口里。 |
| 影响 | 当磁盘抖动、文件被编辑器或杀毒软件短暂占用、或者 hot reload 一次命中多条坏路径时，系统会在真正开始 `ParseIntoChunks()` 之前先出现可感知卡顿；更糟的是，用户最终拿到的仍是“按删除处理”的模糊结果，既损失交互性，也失去对真实 I/O 故障的可诊断性。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `AddFile()` 从“同步重试器”收敛回“记录读取结果”的轻量入口，重试策略上移到更高层或改成显式 fail-fast。 |
| 具体步骤 | 1. 在 `FAngelscriptPreprocessor::FFile` 或 `FAngelscriptModuleDesc` 上新增明确的 source-load 状态，例如 `Success / Missing / ReadFailedTransient / ReadFailedPermanent`，不要再用 sleep 重试后的空 `RawCode` 隐式表达读取失败。2. 将 `AddFile()` 的同步分支改成一次读取尝试，最多保留一次极短的即时重试；删除当前 `for (; Tries < 6; ++Tries)` 与累计 `Sleep` backoff，避免基础入口阻塞调用线程。3. 若产品确实需要“稍后再试”，把重试提升到 compile/hot reload 调度层：按整个 batch 统一决定是否延后一次重试，而不是对每个文件各自睡眠；这样才能给出整体超时、取消和诊断。4. 与 `Issue-40` 的 source-state 方案联动：`ReadFailedTransient/Permanent` 不应再走 `"Treating file as deleted"`，而应阻止该模块进入正常 compile/swap-in 路径，并在日志与 UI 里明确区分“文件被删除”和“文件暂时不可读”。5. 为读失败路径增加节流诊断，至少输出失败文件、尝试次数、耗时和最终状态，便于定位是锁文件、权限问题还是路径错误。6. 新增自动化/trace：模拟 `LoadFileToString()` 持续失败和短暂失败两类场景，断言单文件预处理耗时不再附带 `0.51s` 级固定阻塞，且结果状态不再伪装成删除。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 若现有流程依赖“文件短暂锁住时自动等一会儿就能继续”的隐式行为，去掉串行 backoff 后会更早暴露真实 I/O 故障；需要通过更清晰的状态和一次性 batch retry 来降低迁移冲击。 |
| 前置依赖 | 建议与 `Issue-40` 的 source-state 改造配套实施，避免只去掉睡眠却继续把读失败伪装成删除。 |
| 验证方式 | 1. 模拟 1 个和多个脚本同时读失败，确认预处理前置耗时不再按 `0.51s × 文件数` 线性累加。2. 短暂失败后重试成功的场景应给出明确 trace，而不是静默 sleep。3. 永久失败场景下，日志和 module 状态明确显示 `ReadFailed`，不会再把 provider 当成“被删除的空模块”。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-50 | Defect | 与 `Issue-40` 联动处理，先把读失败从“同步卡顿 + 伪删除”收敛成显式 source-state |

---

## 发现与方案 (2026-04-08 17:47)

### Issue-51：`EDITORONLY_DATA` 不会进入 `EditorOnlyBlockLines`，预处理器与 builder 对 editor-only 语义各自为政

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp` |
| 行号 | 3092-3125，3623-3642；4353-4356；6920-6987 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 54，这里补齐执行方案。已验证事实：`ParseIntoChunks()` 中负责维护 `EditorOnlyBlockLines` 的 `UpdateEditorBlockLines()` 只把 `IfDef.Condition == "EDITOR"` 视为 editor-only block；但同一文件后面的反射宏校验又明确把 `EDITOR` 和 `EDITORONLY_DATA` 都当成合法 editor-only 条件，并据此给 `FMacro::bEditorOnly` 打标。编译阶段 `CompileModule_Types_Stage1()` 会把 `Module->EditorOnlyBlockLines` 交给 AngelScript builder，而 builder 的 `CheckEditorOnlyType/Function/Property/Global()` 又只依赖这些行号来判断当前节点是否位于 editor-only 代码里。结果是脚本若使用 `#if EDITORONLY_DATA` 包裹 editor-only 类型、函数或属性，预处理器前半段认它合法，builder 后半段却仍可能报 `Cannot use editor-only ... outside of an EDITOR block`。 |
| 根因 | 预处理器内部没有统一的 editor-only 条件模型：一条路径用硬编码字符串 `"EDITOR"` 维护 block line 元数据，另一条路径用 `"EDITOR" || "EDITORONLY_DATA"` 判断宏是否合法；builder 最终只消费前者生成的行号合同。 |
| 影响 | 合法的 `EDITORONLY_DATA` 脚本会在编译阶段得到误导性错误，开发者被迫把同一段代码改写成 `#if EDITOR` 才能通过，导致官方 flag 语义在同一预处理流水线内自相矛盾；这不仅破坏条件编译合同，也会让后续 editor-only 诊断和代码迁移变得不可信。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `FActiveIfDef` 引入统一的 editor-only 条件归一语义，让 `EditorOnlyBlockLines`、宏校验和 builder 共享同一份判断结果。 |
| 具体步骤 | 1. 在 `ParseIntoChunks()` 附近提取共享 helper，例如 `IsEditorOnlyCondition(const FActiveIfDef&)` 或把 `FActiveIfDef` 扩展为保存 `BaseCondition`、`bNegated`、`bTreatAsEditorOnlyGate`，不要再直接对 `Condition` 字符串做裸比较。2. 修改 `UpdateEditorBlockLines()`，使其同时识别 `EDITOR` 与 `EDITORONLY_DATA` 的活跃分支；若条件已被 `#else` 翻转成 `!EDITORONLY_DATA`，则应依赖结构化状态而不是字符串前缀判断，避免误把否定分支也记成 editor-only。3. 将 3623-3642 行的宏合法性检查改成复用同一 helper，收敛 `UPROPERTY/UFUNCTION`、`FMacro::bEditorOnly` 和 `EditorOnlyBlockLines` 三处语义，不再各自维护条件白名单。4. 若 builder 的报错文案仍固定写 `outside of an EDITOR block`，同步改成更准确的 `editor-only block` 或在文案中接受 `EDITORONLY_DATA`，减少修复后诊断与源码写法继续不一致。5. 为 fully precompiled/schema 路径补齐 `EditorOnlyBlockLines` 的保存与恢复，确保 `EDITORONLY_DATA` 修复不会只在实时预处理路径生效。6. 新增回归：`#if EDITORONLY_DATA` 内访问 editor-only type/property/function 应通过；对应 `#else` 分支仍应报错；并比较 `EditorOnlyBlockLines` 在 `EDITOR` 与 `EDITORONLY_DATA` 两种写法下都能正确落点。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 若现有工程里有人利用了当前错误行为，例如故意用 `EDITORONLY_DATA` 触发 builder 报错作为约束，修复后诊断集合会变化；需要用回归测试锁住真正期望的 editor-only 语义。 |
| 前置依赖 | 建议与 `Issue-32`、`Issue-33`、`Issue-35` 的条件编译修复一起审视 `FActiveIfDef` 结构，避免同一条件栈继续在多个调用点各写一套规则。 |
| 验证方式 | 1. 新增 `EDITORONLY_DATA` fixture，确认 editor-only type/function/property 在该块内不再误报。2. 对比 `EDITOR` 与 `EDITORONLY_DATA` 两种包裹写法，确认 `EditorOnlyBlockLines` 和 builder 行为一致。3. 走 fully precompiled 与实时预处理两条路径，确认两侧对 editor-only block 的诊断完全一致。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-51 | Defect | 与条件编译修复同批推进，先统一 editor-only 条件模型，再回归 builder 诊断 |

---

## 发现与方案 (2026-04-08 18:01)

### Issue-52：explicit `import` 拓扑排序会替换整份 `Files` 数组，未完成的异步读回调将结果写进失效副本

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` |
| 行号 | 141-188，232-239，439-497；61-67 |
| 问题 | 已验证事实：`PerformAsynchronousLoads()` 的 `SizeCallback` / `ReadCallback` 都按引用捕获 `Files` 容器里的 `FFile&`，回调完成后继续回写 `File.RawCode` 与 `File.bLoadAsynchronous`。一旦关闭默认的 `bAutomaticImports`，`Preprocess()` 会在 232-239 行构造 `TArray<FFile> SortedFiles`，随后直接执行 `Files = SortedFiles`。这不是简单重排索引，而是替换整份值类型数组。于是只要任一异步请求晚于这次赋值完成，回调写入的就是旧数组副本；而后续 `DetectClasses()`、`AnalyzeClasses()`、`GetModulesToCompile()` 消费的是新数组副本。 |
| 根因 | explicit `import` 排序把“文件顺序”建模成 `FFile` 值对象复制，而异步 I/O 层又把容器元素地址直接暴露给后台回调；两层之间没有 completion barrier，也没有稳定 owner。 |
| 影响 | 这会把异步读取结果静默写丢，当前预处理继续使用过期 `RawCode` / `ChunkedCode`；更坏的情况是旧数组在赋值后已经被释放，晚到回调会直接踩悬挂内存。也就是说，explicit `import` 模式不仅继承 `Issue-6` 的异步生命周期问题，还会在 `Files = SortedFiles` 这一中途赋值点把它提前放大成确定性的内存安全缺陷。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把文件排序从“复制 `FFile`”改成“重排稳定句柄/索引”，并禁止在任何 outstanding async load 存在时进入排序阶段。 |
| 具体步骤 | 1. 先加硬护栏：`Preprocess()` 在进入 explicit-import 排序前检查 outstanding load 数量；若异步请求尚未全部完成，直接 fail-fast 或强制回退到同步完成路径，绝不允许边加载边执行 `ProcessImports()`。2. 将 `SortedFiles` 从 `TArray<FFile>` 改成 `TArray<int32>`、`TArray<FFile*>` 或独立 `ModuleOrder` 列表；排序结束后仅重排索引或 `Swap` 现有元素，避免再次整值复制 `FFile`。3. 如果保留异步加载，回调不得再捕获 `FFile&`；应改为写入独立的 `FScriptLoadRequestState`，待主线程确认全部完成后再一次性提交到当前 `Files`。4. 在 `ProcessImports()`/`OutSortedFiles` 这一层同步收口：拓扑排序输出只负责模块顺序，不再承担 `FFile` 生命周期转移。5. 为 explicit-import + async 组合新增专门测试桩，强制让 callback 在排序后才完成，断言修复后 `RawCode` 最终落在当前数组、不会写进旧副本，也不会触发崩溃。6. 若短期内继续采用 `Issue-6` 的同步止血方案，也应把“排序输出改成稳定句柄”一并落地，避免未来重新启用 async 时把同一漏洞重新带回。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadScenarioTests.cpp` |
| 预估工作量 | M |
| 风险 | 若其他阶段隐式依赖 `Files` 当前的值语义复制行为，切到索引/句柄排序后需要补齐这些调用点；但继续保留值复制会长期阻塞任何安全的异步 I/O 设计。 |
| 前置依赖 | 建议与 `Issue-6` 同批处理，至少先拿到“无 outstanding async request 才能排序”的统一生命周期合同。 |
| 验证方式 | 1. 构造 explicit-import 模式下的延迟回调测试，确认排序后不会再写旧副本。2. 对同一 fixture 比较排序前后 `RawCode`、`ImportedModules` 和最终编译结果，确认仅改变内存所有权，不改变合法脚本语义。3. 在 AddressSanitizer 或等价内存检测环境下跑删除/读失败场景，确认不再出现 use-after-free。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-52 | Defect | 与 `Issue-6` 一起优先收口，先切断排序阶段对旧 `FFile` 副本的悬挂写入口 |

---

## 发现与方案 (2026-04-08 18:01)

### Issue-53：explicit `import` 的依赖语义没有进入 precompiled cache 命中条件，改了导入目标仍可能复用旧编译结果

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 289-304，482-483；1417-1424；4283-4299 |
| 问题 | 已验证事实：explicit `import` 在预处理阶段先写入 `Module->ImportedModules`，随后立刻通过 `ReplaceWithBlank()` 把源码中的 `import` 语句抹成空白。之后 `Preprocess()` 只对 `ProcessedCode` 计算 `CodeHash`。另一方面，`FAngelscriptPrecompiledModule::InitFrom()` 虽然会把 `ImportedModules` 单独序列化进 cache，但 `CompileModule_Types_Stage1()` 的命中条件仍只检查 `CompiledModule->CodeHash == Module->CodeHash`，命中后直接 `ApplyToModule_Stage1()` 并提前返回。也就是说，显式 import 的依赖集合被保存了，却没有被拿来决定 cache 是否仍然有效。 |
| 根因 | 预处理器把“源码文本哈希”和“模块依赖元数据”拆成两条平行通道，但 precompiled cache 命中路径只消费 `CodeHash`，没有把 `ImportedModules` 或它们的签名并入校验合同。 |
| 影响 | 只要脚本变更只发生在 `import` 目标或顺序，而且变更前后的 `ProcessedCode` 仍得到相同 `CodeHash`，系统就会继续加载旧的 precompiled 结果，即使当前模块描述符里的 `ImportedModules` 已经不同。结果是缓存命中的 `ScriptModule` 仍处在旧依赖环境，而运行时和诊断看到的 module desc 却已经指向新依赖，形成静默失配。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 explicit import 引入独立的 dependency fingerprint，并要求 precompiled cache 命中同时校验源码哈希与依赖签名。 |
| 具体步骤 | 1. 在 `FAngelscriptModuleDesc` 与 `FAngelscriptPrecompiledModule` 中新增 `ExplicitImportHash` 或更通用的 `DependencySignature` 字段，输入必须来自 canonicalized、去重且保序后的 `ImportedModules`，不能再依赖被 blank 掉后的 `ProcessedCode`。2. `Preprocess()` 在 `ProcessImports()` 成功后立即计算这份签名；`FAngelscriptPrecompiledModule::InitFrom()` 同步持久化，`GetModulesToCompile()` 恢复时也回填到 `ModuleDesc`。3. 修改 `CompileModule_Types_Stage1()` 的 cache 命中条件：除 `CodeHash` 外，显式 import 模式还必须比较 `CompiledModule->DependencySignature == Module->DependencySignature`；任何一项不一致都应视为 stale cache。4. 若 `Issue-7` 的统一依赖签名方案已在实施，直接复用同一套顺序敏感签名，避免 explicit/automatic import 再分出两套缓存合同。5. 为 cache 诊断补一条命中失败原因日志，明确是 `CodeHash` 变化还是 `DependencySignature` 变化，方便定位“只改 import”的 miss。6. 新增回归：生成 cache 后仅把 `import Foo.Bar;` 改成等长的 `import Baz.Quz;` 或交换两条 import 的顺序，断言 live preprocess 会让 cache miss，并得到与实时编译一致的 provider 集合。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp` |
| 预估工作量 | M |
| 风险 | cache schema 变更需要 bump 版本并处理旧缓存回退；同时修复后会让一批过去“误命中”的 cache 变成 miss，短期内可能增加一次性全量重编。 |
| 前置依赖 | 建议与 `Issue-7`、`Issue-26`、`Issue-36` 的依赖签名治理共用同一套 canonical import/dependency hash 设计。 |
| 验证方式 | 1. 先生成 precompiled cache，再只修改 import 目标或顺序，确认启动时不再误命中旧 cache。2. 比较 cache-hit 与实时编译路径的 `ImportedModules`、可见类型集合和 `CombinedDependencyHash`，确认完全一致。3. 回归无 import 变化场景，确认正常命中率不受非必要影响。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-53 | Defect | 在依赖签名改造批次中尽早处理，先让 precompiled cache 不再忽略显式 import 变化 |

---

## 发现与方案 (2026-04-08 18:01)

### Issue-54：模块身份在 preprocessor、runtime lookup 和 precompiled cache 中仍只是裸 `FString`，缺少跨层共享的 canonical identity

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | 86-103；2911-2938，3029-3051；423-438；1417-1424，2651-2659，2773-2779 |
| 问题 | 已验证事实：预处理阶段在 `AddFile()` 里直接用 `FilenameToModuleName(RelativeFilename)` 生成 `Module->ModuleName`；运行时按文件名查模块时，`GetModuleByFilename()` 又会把绝对路径相对到各个 root 后重新执行一遍 `.Replace(".as","").Replace("/", ".")`；precompiled schema 侧只持久化 `ModuleName`、`ImportedModules` 和单个 `ScriptRelativeFilename` 字符串，生成 cache 与恢复 cache 时都继续以这些裸字符串为唯一身份。整个系统没有一个共享的模块身份对象去同时承载“root 身份、canonical relative path、canonical module name、case-folded lookup key、物理文件 provenance”。 |
| 根因 | 模块系统把身份建模成若干处临时拼接出来的字符串，而不是一个跨层传递的稳定值对象；一旦路径规范化、root 区分、大小写折叠或 cache key 规则发生变化，就必须分别在 preprocessor、runtime、StaticJIT 中手工同步。 |
| 影响 | live preprocess、按文件名 lookup、hot reload 和 fully precompiled restore 很容易形成各自不同的“同一模块”判定，导致路径相关修复需要多点打补丁且极易遗漏。结果不是单点 bug，而是模块系统长期没有统一身份合同，任何涉及相对路径、root 冲突、大小写、cache key 的改动都容易再次分叉。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 引入跨层共享的 `ModuleIdentity`/`ScriptPathCanonicalizer`，把路径到模块身份的解释从零散字符串操作升级为单一真源。 |
| 具体步骤 | 1. 在 runtime core 层新增轻量值对象，例如 `FAngelscriptModuleIdentity`，字段至少包含 `RootKey`、`CanonicalRelativePath`、`CanonicalModuleName`、`LookupKeyLower`、`DisplayModuleName`，并提供统一构造入口 `BuildModuleIdentity(...)`。2. `FindAllScriptFilenames()` / `AddFile()` 在文件发现阶段就生成 identity，`FAngelscriptModuleDesc` 与 `FCodeSection` 只保存这份结构或其稳定子集，不再各处重复 `.Replace(".as")`、`/ -> .`、相对 root 计算。3. `GetModuleByFilename()`、显式 import resolver、hot reload 状态表、`ActiveModules` key、`PrecompiledData` 的 schema 都改为消费同一 identity；`ModuleName` 退化为展示字段，而非唯一主键。4. 为 cache schema 增加 identity 版本和 root/path provenance，旧缓存若缺少这些字段就强制回退到实时预处理，避免新旧身份合同混用。5. 将大小写折叠、反斜杠归一、相对路径折叠、root 区分、symlink/realpath 策略统一收口到 `ScriptPathCanonicalizer`，禁止再在业务代码里内联字符串变换。6. 为迁移期提供兼容诊断：当旧字符串 key 与新 identity 结果不一致时，输出一条明确 warning，帮助定位历史脚本或缓存数据的潜在漂移。7. 补充跨层回归：同一物理脚本分别走 live preprocess、`GetModuleByFilename()`、fully precompiled restore 和 hot reload，四条路径都必须得到完全一致的 module identity。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataTests.cpp` |
| 预估工作量 | L |
| 风险 | 这是跨层 schema 和 key 迁移，短期内会同时触碰预处理、hot reload、cache 和测试夹具；需要分阶段上线并明确旧缓存失效策略。 |
| 前置依赖 | 建议与 `Issue-4`、`Issue-21`、`Issue-27`、`Issue-38` 的修复共用同一 identity 设计，避免先补丁后再推翻。 |
| 验证方式 | 1. project root 与 plugin root 同名脚本、大小写变化路径、相对路径 import、fully precompiled restore 四类场景下，统一得到同一 `CanonicalModuleName`/identity key。2. 修改 canonicalization 规则后，只需要更新一处 helper 和测试，不再出现 live path 与 cache path 各自修一半的回归。3. 旧 cache 启动时若 identity 信息不足，必须稳定回退到实时预处理，而不是静默继续加载。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-54 | Architecture | 在路径/缓存修复进入整合阶段时优先立项，先把模块身份收敛成单一真源，再继续清理零散字符串规则 |

---

## 发现与方案 (2026-04-08 18:19)

### Issue-55：多 section 模块的编译诊断固定落到 `Code[0]`，`#include`/expanded module 会把错误锚到错误文件

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1277-1286；4342-4346；4944-4956 |
| 问题 | 已验证事实：`FAngelscriptModuleDesc` 明确定义 `TArray<FCodeSection> Code`，编译阶段也会遍历全部 section 调用 `ScriptModule->AddScriptSection(...)`。但模块级诊断入口 `ScriptCompileError(TSharedPtr<FAngelscriptModuleDesc> Module, int32 LineNumber, ...)` 在 `Module->Code.Num() != 0` 时无条件把错误写到 `Module->Code[0].AbsoluteFilename`。也就是说，模块明明已经支持“一模块多 section”，错误归档却仍然假定“这个模块只有第一个源码文件”。推断：一旦按 `Issue-5` 落地 `#include` 展开、或其他路径开始为同一模块填入多个 section，第二个及之后 section 的编译/校验错误都会被错误记到第一份文件。 |
| 根因 | 诊断合同仍然是单文件模块模型：`ScriptCompileError(Module, ...)` 只接受模块和行号，没有 section 身份；`FAngelscriptModuleDesc::Code` 虽然已经升级为数组，但错误路由没有同步升级成 section-aware。 |
| 影响 | `#include`、future expanded module、以及任何多 section cache 恢复一旦落地，用户看到的 compile error、debug diagnostics 和热重载失败文件都会系统性指向错误源码。这样不仅会误导定位，还会让 include graph 调试成本暴涨，因为真正出错的 included file 在日志里根本看不到。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把模块级诊断从“模块 + 行号”升级为“section 身份 + 行号”，禁止继续默认回落到 `Code[0]`。 |
| 具体步骤 | 1. 在 runtime core 层新增显式源码定位结构，例如 `FScriptSourceLocation { int32 SectionIndex; FString AbsoluteFilename; int32 Line; int32 Column; }`，并为 `FAngelscriptModuleDesc::FCodeSection` 提供稳定索引。2. 重载 `ScriptCompileError(...)`：新增接受 `SectionIndex` 或完整 `FScriptSourceLocation` 的版本；旧的 `ScriptCompileError(Module, LineNumber, ...)` 只作为兼容入口，并在 `Code.Num() > 1` 时记录 `ensureMsgf`/warning，强制调用方迁移。3. 让 `CompileModules()`、restriction 校验、declared import 校验、reload 诊断等所有当前只传 `Module + LineNumber` 的路径，改为携带真实 section 身份；对于 AngelScript builder 已经给出的 `scriptSectionIdx`，直接复用，不再二次猜测。4. 与 `Issue-5` 的 include graph 设计联动：`FResolvedInclude`/expanded section 必须在写入 `Module->Code` 时保留原始文件 provenance，确保 included file 的错误能直接落到自身文件。5. 在 `FAngelscriptPrecompiledModule` schema 中同步保存 section-level provenance；cache-hit 路径恢复诊断时也必须能得到同样的 `AbsoluteFilename`，避免 live/precompiled 两条路径再次分叉。6. 新增回归：构造两 section 同模块 fixture，仅让第二个 section 触发编译错误，断言 diagnostics 锚到第二个 section 的真实文件；再补一条 include 场景，确认错误不会再落到 owner module 的第一份文件。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | M |
| 风险 | 诊断 API 改成 section-aware 后，会波及多个当前只传模块行号的调用点；但若不先收紧这层合同，include/multi-section 支持一落地就会把错误定位整体打坏。 |
| 前置依赖 | 建议与 `Issue-5`、`Issue-21`、`Issue-54` 一起设计，复用 section provenance 和 canonical identity。 |
| 验证方式 | 1. 两 section 模块里只让第二份源码报错，确认 `Diagnostics` 键和值都落在第二个 `AbsoluteFilename`。2. precompiled cache hit 与实时编译两条路径都应给出相同的 section 级文件名。3. include fixture 中修改/打坏 included file 时，错误不再错误指向 owner module 的首个 section。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-55 | Defect | 在实现 `#include` / multi-section module 之前先修，先把诊断锚点从单文件假设升级为 section-aware |

---

## 发现与方案 (2026-04-08 18:20)

### Issue-56：`EditorOnlyBlockLines` 只按 `scripts[0]` 解释行号，多 section 模块的 editor-only 校验会整体错位

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp` |
| 行号 | 3111-3122；1318-1319；4353-4355；6881-6899 |
| 问题 | 已验证事实：预处理器记录 editor-only 信息时，只把它存成 `TArray<TPair<int,int>> EditorOnlyBlockLines`，元素里只有起止行号，没有 section/file 身份。编译阶段把这份数组直接传给 `builder->SetEditorOnlyBlockLinePositions(...)`。而 builder 的实现明确只取 `scripts[0]` 来把行号转换成字符区间，再把全部 block 都映射到这第一份 script 上。也就是说，只要一个模块含有第二个 section，来自该 section 的 editor-only 行号就会按第一份源码的行表解释。推断：`Issue-5` 的 include graph 一旦把 included file 展开成额外 section，`#if EDITOR` / `#if EDITORONLY_DATA` 在 include 文件里的合法区域会被错误映射，导致 builder 误报或漏报 editor-only 违规。 |
| 根因 | editor-only 元数据仍然建立在“单文件模块”假设上：preprocessor 只记录全局行号，builder 只消费第一份 script 的行表，二者之间没有任何 section-aware 的 source map。 |
| 影响 | include/multi-section 模块落地后，editor-only 类型、函数、属性和全局的诊断将不再可信。开发者可能在 owner module 首文件完全无关的行收到 editor-only 报错，或反过来让真实违规的 included file 静默漏检，直接破坏条件编译与编辑器专用 API 的边界合同。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 editor-only 元数据升级为 section-aware 区间模型，让 preprocessor、module descriptor 和 builder 对同一块源码使用同一份 section 定位。 |
| 具体步骤 | 1. 在 `FAngelscriptModuleDesc` 中把 `EditorOnlyBlockLines` 升级为带 section 身份的结构，例如 `FEditorOnlyBlockRange { int32 SectionIndex; int32 StartLine; int32 EndLine; }`，不要再只存裸行号对。2. `ParseIntoChunks()` 记录 editor-only block 时同步保存当前 `FFile` 对应的 section 索引或 source key；若未来一个模块可由多个 `FFile/FCodeSection` 组成，这份索引必须在写入 `Module->Code` 时保持稳定。3. 重写 `asCBuilder::SetEditorOnlyBlockLinePositions(...)` 接口，让它接受 section-aware range；内部按 `SectionIndex` 选择对应 `scripts[n]` 计算字符区间，而不是固定使用 `scripts[0]`。4. 对 builder 的 `IsNodeInEditorOnlyCode(...)` 和相关诊断回归，确认节点所属 `script` 与记录的 block range 使用同一 section 身份比较。5. fully precompiled/schema 路径同步保存并恢复这份 section-aware editor-only metadata，避免 live/include 修好后 cache 路径继续退化。6. 新增回归：两 section 模块中仅第二个 section 包含 `#if EDITORONLY_DATA` 包裹的 editor-only API，修复后应只在第二个 section 内通过/报错；再补 include fixture，确认 included file 的 editor-only block 不会被映射到 owner 首文件。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | L |
| 风险 | 这会改 third-party builder 接口和 precompiled schema；但如果继续保留单 section 行号模型，include graph 与 editor-only 语义无法同时正确成立。 |
| 前置依赖 | 建议与 `Issue-5`、`Issue-18`、`Issue-51`、`Issue-55` 联动，统一 section provenance 和 editor-only 诊断合同。 |
| 验证方式 | 1. 构造两 section 模块，仅第二个 section 存在 editor-only block，确认 builder 不再把行号映射到第一份脚本。2. include 文件中的 editor-only API 在合法 block 内应通过，在 block 外仍应稳定报错。3. live compile 与 precompiled cache hit 两条路径返回相同的 editor-only 诊断位置。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-56 | Defect | 与 `Issue-55` 同批优先处理，先让 multi-section 的 editor-only 元数据变成 section-aware |

---

## 发现与方案 (2026-04-08 18:21)

### Issue-57：`Module->CodeHash` 对多 section 使用 XOR 聚合，section 顺序与来源变化都不会进入缓存签名

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp` |
| 行号 | 1277-1291；289-304；4342-4346；190-205，273-303 |
| 问题 | 已验证事实：`FAngelscriptModuleDesc` 允许一个模块持有多个 `FCodeSection`；预处理阶段为每个 section 单独算 `Section.CodeHash` 后，再用 `File.Module->CodeHash ^= Section.CodeHash` 做聚合。另一方面，编译阶段会按 `Module->Code` 当前顺序逐个 `AddScriptSection(...)`，builder 再按 `scripts.PushLast(...)` 的保存顺序依次 `ParseScript(scripts[n])`、`RegisterTypesFromScript(...)`。也就是说，编译输入本身是“有序 section 列表”，但模块签名却把它压成了可交换的 XOR 集合。推断：一旦 `Issue-5` 引入 include 展开、或未来 artifact cache 恢复出多 section 模块，仅调整 section 顺序、重复/去重某个 include、或保留相同代码但更换 section provenance，都可能得到相同 `CodeHash`，却把编译器喂入了不同的源码序列。 |
| 根因 | 当前 `CodeHash` 建模的是“section 内容的无序集合”，而不是“带 provenance 的有序 section 序列”；它与 `AddScriptSection`/builder 的真实消费合同不一致。 |
| 影响 | 这会直接阻塞 include graph、增量预处理和 precompiled cache 的正确失效设计。系统可能把“section 顺序已变”“owner module 展开了不同 include 序列”“同一代码来自不同物理文件”错误地视为同一模块版本，导致 cache 命中、dependency signature 和调试 provenance 全部建立在过度压缩的哈希上。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `CodeHash` 从“section 内容 XOR”升级为“按 section 顺序和 provenance 计算的稳定 fingerprint”，让缓存合同与真实编译输入一致。 |
| 具体步骤 | 1. 在 `FAngelscriptModuleDesc::FCodeSection` 上补齐稳定 fingerprint 输入，例如 `CanonicalRelativeFilename`、`SourceRootKey`、`SectionCodeHash`，不要再只依赖裸 `Code` 文本。2. 用顺序敏感的组合方式替换当前 XOR，例如定义 `HashOrderedSectionSequence(TConstArrayView<FCodeSection>)`，按 `SectionIndex -> SourceIdentity -> SectionCodeHash` 顺序逐项 `HashCombine`/`XXH3_128`，保证 `A,B` 与 `B,A`、`A` 与 `A,A` 不再冲突。3. 将 `Module->CodeHash` 明确拆成两层：`SourceSequenceHash` 表示真实 section 输入，`ProcessedTextHash` 仅保留给单 section 文本对比；precompiled cache、future artifact cache 和 multi-section invalidation 必须消费前者。4. 与 `Issue-19` 的 raw-source 校验、`Issue-21` 的 section root identity、`Issue-55/56` 的 section-aware diagnostics 一起收敛成统一 `SectionProvenance` 结构，避免 hash、diagnostics、editor-only 元数据各自维护一套 section key。5. 修改 `FAngelscriptPrecompiledModule` schema 和命中条件：多 section 模块命中 cache 时必须联合比较顺序敏感的 section fingerprint，而不是继续只看聚合 `CodeHash`。6. 新增回归：构造同一模块的两 section fixture，仅交换 section 顺序；修复后 fingerprint 必须变化。再补一条“同样代码文本但 section 来源文件不同”的 fixture，确认 fingerprint 同样变化，避免调试/定位信息复用旧缓存。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 预估工作量 | M |
| 风险 | 新 fingerprint 会让一批旧 cache 统一失效，并可能暴露历史上被 XOR 掩盖的伪命中；但这是 include/multi-section 正式落地前必须支付的清理成本。 |
| 前置依赖 | 建议与 `Issue-5`、`Issue-19`、`Issue-21`、`Issue-24` 联动，统一 section provenance、raw-source 校验和 artifact cache 键。 |
| 验证方式 | 1. 交换同一模块两份 section 的顺序后，新的 section fingerprint 必须变化，cache 不得误命中。2. 保持代码文本不变、仅更换 section 文件来源时，fingerprint 同样应变化。3. 对单 section 普通脚本回归，确认 fingerprint 与现有 `CodeHash` 语义兼容，不引入无意义 miss。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-55 | Defect | 先修，避免 multi-section 诊断全部落到首文件 |
| P1 | Issue-56 | Defect | 与 `Issue-55` 同批，先让 editor-only 元数据具备 section 身份 |
| P1 | Issue-57 | Architecture | 在设计 include graph / artifact cache 时立即纳入，先收紧多 section 指纹合同 |

---

## 发现与方案 (2026-04-08 18:27)

### Issue-58：fully precompiled 启动下 `GetModuleByFilename()` 无法解析 plugin 脚本，断点与按文件定位模块退化成字符串兜底

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | 781-792，2046-2052，3029-3056；1484-1495，2758-2783；933-966 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 49，这里补齐执行方案。已验证事实：fully precompiled 启动时 `InitialCompile()` 直接走 `PrecompiledData->GetModulesToCompile()`，跳过 live preprocessor；`GetModulesToCompile()` 恢复出的 `ModuleDesc` 没有任何 `Code` section，而 `GetModuleByFilename()` 的第一层匹配正是遍历 `Module->Code[*].AbsoluteFilename`。当这层失败后，fallback 只能对 `AllRootPaths` 逐个做相对化并还原模块名；但 `GetScriptRootDirectory()` 明确把 `AllRootPaths[0]` 视为 project root，配合 fully precompiled 启动早期未补齐 plugin roots 的现状，来自 content plugin 的脚本文件名无法稳定还原成模块。调试器 `SetBreakpoint` / `ClearBreakpoints` 随后调用 `GetModuleByFilenameOrModuleName()`；若按文件名解析失败，就退化为以 `BP.Filename` 字符串作为 breakpoint key，而不是绑定到真实 `ModuleDesc`。 |
| 根因 | fully precompiled 路径没有恢复“按文件定位模块”所需的两份核心输入：一是 `ModuleDesc->Code` 里的文件 provenance，二是完整的 script root registry。引擎仍然暴露 `GetModuleByFilename()` 这条 API，但缓存恢复出来的模块描述符并不满足它的输入合同。 |
| 影响 | 在 fully precompiled 且脚本来自 plugin root 的场景下，任何依赖文件名解析模块的消费方都会行为退化。当前已验证的直接影响是调试器 breakpoint bookkeeping 只能依赖 `ModuleName` 或原始 filename 字符串兜底，导致 plugin 脚本的按文件设置/清理断点、按文件定位模块、以及后续基于文件名的调试入口与 project 脚本不再等价。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 让 fully precompiled 路径恢复出可用于 filename lookup 的完整 section/root provenance，并把 `GetModuleByFilename()` 收口到统一的 canonical 文件索引，而不是继续依赖空 `Code` 和 project-root fallback。 |
| 具体步骤 | 1. 以 `Issue-20` 和 `Issue-21` 为前置，把 precompiled schema 扩展为可恢复 section provenance：至少为每个 section 保存 `SourceRootKey + RelativeFilename`，并在 fully precompiled 启动前先调用 `MakeAllScriptRoots()` 建好完整 root registry。2. 在 `FAngelscriptEngine` 激活模块时新增 `CanonicalAbsoluteFilename -> ModuleDesc` 索引，来源统一取自 `Module->Code` 或等价的 section provenance；`GetModuleByFilename()` 先 canonicalize 输入路径，再查询该索引，避免每次线性扫 `ActiveModules` 且避免依赖 `Code` 是否为空。3. 将当前 `GetModuleByFilename()` 的 root-relative fallback 收窄为兼容路径：只有在索引缺失且模块 provenance 已确认不可用时才尝试；若 fully precompiled 模块缺少 provenance，应输出明确 warning 并把缓存标记为 stale，而不是静默返回空。4. 调整 `GetModuleByFilenameOrModuleName()` 与 debug server 的断点路径：当 `SetBreakpoint` / `ClearBreakpoints` 在 fully precompiled 模式下只能落到 filename 字符串兜底时，应输出可诊断日志，帮助定位“缓存模块缺少 filename lookup provenance”的真实原因，而不是继续把失败伪装成正常 fallback。5. 为调试器与文件系统测试补齐 fully precompiled + plugin fixture：断言 `GetModuleByFilenameOrModuleName()` 能用 plugin 绝对路径直接命中正确模块，断言 breakpoint key 绑定到真实 `ModuleDesc->ModuleName`，而不是原始 filename。6. 若未来引入 `Issue-54` 的 `ModuleIdentity`，则把 filename 索引直接建立在 identity 上，禁止 debug server、file-system lookup、precompiled restore 再各自维护一套路径解释规则。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Tests/AngelscriptPrecompiledDataTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | M |
| 风险 | filename 索引一旦切到 canonical absolute path，需要同步处理 live hot reload 的重命名/删除更新，否则可能把旧路径残留在索引里；另外，fully precompiled 模式下新增 stale-cache 诊断会让一部分历史缓存首次显式失效。 |
| 前置依赖 | 建议与 `Issue-20`、`Issue-21`、`Issue-54` 联动；至少先恢复 section provenance 和完整 root registry，再收口 filename lookup。 |
| 验证方式 | 1. 在 project root 和 plugin root 分别准备脚本，生成 cache 后走 fully precompiled 启动，断言 `GetModuleByFilename()` 能分别用两个绝对路径命中正确模块。2. 用 plugin fixture 跑 `AngelscriptDebuggerBreakpointTests`，断言 `SetBreakpoint` / `ClearBreakpoints` 后的 key 使用真实模块名而不是原始 filename fallback。3. 移除某个 plugin root 或故意让缓存缺少 provenance，确认系统会输出 stale-cache 诊断并回退，而不是静默解析失败。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-58 | Defect | 在 `Issue-20/21/54` 的缓存 provenance 改造批次中同步处理，避免调试与按文件定位模块继续走退化路径 |

---

## 发现与方案 (2026-04-08 23:36)

### Issue-59：脚本发现顺序未稳定化，重复模块冲突与 `import` provider 选择会随文件系统枚举顺序漂移

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 行号 | 1944-1996，2075-2078，3133-3136；463-497；258-273 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 71，这里补齐执行方案。已验证事实：`FindScriptFiles()` 直接按 `FindFiles()` 返回顺序把文件追加到 `OutFilenames`，对子目录还先走 `TSet<FString>(LocalDirs)` 去重后继续递归，整个发现阶段没有任何稳定排序；`InitialCompile()` 随后按这个原始顺序逐个 `Preprocessor.AddFile(...)`。显式 `import` 解析时，`ProcessImports()` 对每条依赖都只取 `Files` 中第一个命中的模块；而编译阶段又用 `CompilingModulesByName.Add(Module->ModuleName, Module)` 让后写入项覆盖前写入项。现有 discovery 测试只把结果收进 `TSet` 校验成员，不验证返回顺序。结果是同一个 duplicate module collision，在预处理、编译注册和热重载阶段看到的“赢家”并不稳定。 |
| 根因 | 模块系统把文件系统枚举顺序当成了隐式优先级，但 discovery、`ProcessImports()`、`CompilingModulesByName` 三处既没有共享一套稳定排序合同，也没有在 duplicate module name 出现时 fail-fast。 |
| 影响 | 相同源码树在不同机器、不同平台，甚至同平台不同枚举顺序下，`import` 命中的 provider、最终留在 `CompilingModulesByName/ActiveModules` 里的实现，以及 duplicate module 的诊断来源文件都可能漂移。这会直接破坏编译确定性，也让后续 module descriptor cache、增量预处理和 hot reload 的身份跟踪缺乏稳定输入。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在脚本发现入口建立稳定排序和统一 duplicate 决策，把“模块身份”从文件系统枚举顺序中剥离出来。 |
| 具体步骤 | 1. 在 `FindAllScriptFilenames()` 返回后、进入 `Preprocessor.AddFile(...)` 之前，对 `FFilenamePair` 做 canonical normalization 和稳定排序，排序键至少包含 `NormalizedRelativePath` 与显式 root priority，禁止继续依赖 `FindFiles()`/`TSet` 的自然顺序。2. 在预处理前新增 `ModuleName -> TArray<FFilenamePair>` 索引，统一通过 `FilenameToModuleName()` 或其后续 canonical 版本先把所有文件映射到模块名；若同一 key 对应多份脚本，直接生成结构化 duplicate-module 诊断，列出全部候选文件并终止编译，而不是让 `ProcessImports()` 取第一个、`CompilingModulesByName` 留最后一个。3. 把 `ProcessImports()` 的 provider 查找改为消费这份统一索引，`CompilingModulesByName` 和后续 `ActiveModules` 也只接受已经过 duplicate 校验的模块集合，保证预处理、编译、swap-in 看到的是同一份模块身份决策。4. 将当前 `ensureMsgf(!CompilingModulesByName.Contains(...))` 从“仅调试提示”升级为用户可见错误，并在错误消息中输出 canonical module name、两边脚本路径和 root precedence，方便迁移。5. 为 discovery/preprocessor 自动化新增可控顺序 fixture：同一模块名准备两份脚本，分别打乱 `FindFiles()` 顺序两次；修复后应得到完全相同的 duplicate 错误，且不得进入真正编译。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | M |
| 风险 | 历史上依赖“重复模块名但碰巧选中某一份脚本”的工程会在修复后直接变成硬错误；需要提供带路径列表的迁移诊断，避免用户只能看到抽象的 duplicate module 报错。 |
| 前置依赖 | 建议与 `Issue-4` 的 `ImportResolver` 设计联动，统一 canonical module identity。 |
| 验证方式 | 1. 构造同模块名双文件 fixture，交换文件发现顺序两次，断言 duplicate 诊断文本和命中的冲突文件集合完全一致。2. 修复后 `ProcessImports()`、`CompilingModulesByName`、`ActiveModules` 三处对同一模块名都只能看到单一 provider。3. 复跑现有 discovery 测试并新增顺序断言，确认 `FindAllScriptFilenames()` 结果顺序稳定。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-59 | Defect | 先修 discovery 和 duplicate gate，避免后续 import/cache 设计继续建立在不稳定输入上 |

---

## 发现与方案 (2026-04-08 23:38)

### Issue-60：`Editor/Dev/Examples` 目录与 `isEditorOnlyModule` 判定大小写敏感，case-insensitive 文件系统上会绕过脚本环境隔离

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1973-1985，4353-4356 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 68，这里补齐执行方案。已验证事实：脚本发现阶段过滤 development/editor 目录时，只接受精确大小写的 `Examples`、`Dev`、`Editor`；编译阶段给 builder 打 `isEditorOnlyModule` 标记时，也只判断 `Module->ModuleName.StartsWith("Editor.") || Contains(".Editor.")`。这两处都没有使用 `IgnoreCase`，而同一文件里 `GetModuleByFilename()` 已经对路径相对化采用 `MakePathRelativeTo_IgnoreCase()`。因此在 Windows 这类 case-insensitive 文件系统上，`editor/`、`dev/`、`examples/` 等目录名会被正常枚举进来，却不会被识别成 editor/dev/example 脚本。 |
| 根因 | “保留脚本目录名原始大小写”和“解释保留目录语义”被混在同一份字符串上，但 discovery 过滤与 builder 标记没有抽出统一的 reserved-segment 判定 helper，也没有遵循文件系统大小写语义。 |
| 影响 | 非目标环境本应跳过的脚本会进入模块图，builder 也看不到 `isEditorOnlyModule` 标志。结果是 editor/dev/examples 脚本可能在 runtime 构建中被错误编译，editor-only API 边界和错误提示同时漂移，直接破坏模块系统的环境隔离合同。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“保留目录/模块段”的识别统一成 case-insensitive helper，只对保留语义做归一化，不改变普通模块名的显示形式。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.cpp` 提取统一 helper，例如 `IsReservedScriptDirectory(const FString& Segment, EReservedScriptDir Kind)` 和 `HasModulePathSegment(const FString& ModuleName, const FString& Segment)`，内部统一使用 `Equals(..., ESearchCase::IgnoreCase)` 或按 `.` 分段后做 ignore-case 比较。2. 将 `FindScriptFiles()` 中对 `Examples`、`Dev`、`Editor` 的跳过逻辑全部改成调用该 helper，确保 `editor/`、`EDITOR/`、`Dev/` 等写法得到一致结果。3. 将 `CompileModule_Types_Stage1()` 里 `builder->isEditorOnlyModule` 的设置改成同一套 helper，禁止继续依赖 `StartsWith("Editor.")` 这种大小写敏感的裸字符串匹配。4. 若未来引入 `Issue-4`/`Issue-59` 的 canonical module identity，则把“保留目录语义”明确建模成 module metadata，而不是让后续阶段反复从显示名里猜。5. 在 discovery 和编译测试里补齐 lowercase/uppercase reserved-dir fixture：`editor/Foo.as`、`dev/Foo.as`、`examples/Foo.as`，断言 runtime 模式下 discovery 会跳过它们，editor compile 下 `isEditorOnlyModule` 会正确置位。6. 对 truly case-sensitive 平台保留兼容策略：如果同一父目录下同时存在 `Editor/` 与 `editor/`，应输出冲突诊断并要求整理脚本树，避免把保留语义和普通模块名混用。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Compiler/AngelscriptCompilerPipelineTests.cpp` |
| 预估工作量 | M |
| 风险 | 在大小写敏感平台上，忽略大小写的 reserved-dir 识别会让 `Editor` 与 `editor` 不再可共存；但这类目录本身已经承载环境语义，继续允许双写法并存只会让模块分类更加不可预测。 |
| 前置依赖 | 无；但若随后推进 `Issue-59` 的 canonical module identity，建议把 reserved-dir helper 合并进同一套路径规范化层。 |
| 验证方式 | 1. 在 Windows 风格 fixture 中创建 `editor/RuntimeLeak.as` 和 `dev/Tooling.as`，runtime discovery 应稳定跳过。2. 在 editor compile fixture 中把模块放进 `editor/` 目录，断言 `builder->isEditorOnlyModule` 为 true。3. 在 Linux/大小写敏感测试环境中构造 `Editor/` 与 `editor/` 并存场景，确认系统给出冲突诊断而不是静默选边。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-60 | Defect | 与 `Issue-59` 同批处理，先统一保留目录语义的大小写合同 |

---

## 发现与方案 (2026-04-08 23:39)

### Issue-61：不可达条件分支仍会求值，未知 flag 会把本应成功的预处理直接打成失败

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3256-3318，4328-4344 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 8 与发现 66，这里补齐执行方案。已验证事实：`ParseIntoChunks()` 处理 `#if` 时先无条件调用 `ParsePreProc(...)`，处理 `#elif` 时也在检查 `bAnyBranchTaken` 之前就先调用 `ParsePreProc(...)`。而 `ParsePreProc()` 对未知 flag 会立刻 `LineError(... \"Invalid preprocessor condition\")` 并把 `bHasError` 置为 true。结果是两类本应被短路的分支仍然会失败：1. 外层分支已经失活时，内层 `#if SOME_UNKNOWN_FLAG` 仍会求值；2. 前面的 `#if/#elif` 已经命中时，后续互斥的 `#elif SOME_UNKNOWN_FLAG` 仍会求值。 |
| 根因 | 条件编译状态机只维护了“当前栈是否生效”的结果，却没有在进入 `ParsePreProc()` 前先判断“这个表达式是否可达”；求值顺序和 reachability 判断写反了。 |
| 影响 | 平台互斥代码和 editor/runtime 分支无法安全组织。脚本作者无法把未知于当前上下文的 flag 放进不可达分支，否则整个预处理都会失败；这会显著削弱错误恢复能力，也迫使项目把本应局部可见的 flag 扩散到所有上下文。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在条件指令入口引入显式 short-circuit 判定，只对“当前可达的分支表达式”调用 `ParsePreProc()`。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp` 提取 helper，例如 `ShouldEvaluateConditionalExpression(const TArray<FIfDef>& Stack, bool bIsElif)` 或等价逻辑，统一判断“外层分支是否全部有效”和“当前 `#elif` 是否已经有分支命中”。2. 修改 `#if`/`#elif` 分支：只有 helper 返回 true 时才调用 `ParsePreProc(...)`；否则直接把当前 frame 的 `bValue` 设为 false，并仅维护栈深度、`Condition` 与 `bAnyBranchTaken` 状态，不再对不可达表达式报未知 flag。3. 对 `#ifdef/#ifndef` 也复用同一 reachability 判定，避免 outer-false 场景下仍去读取/解释 identifier。4. 保留现有“真正可达分支里的未知 flag 必须报错”语义，避免把拼写错误静默吞掉；修复目标是短路不可达分支，不是放宽语法校验。5. 在 `AngelscriptPreprocessorTests.cpp` 新增两组回归：`#if EDITOR ... #elif SOME_UNKNOWN ... #endif` 与 `#if EDITOR ... #else #if SOME_UNKNOWN ... #endif #endif`。前者在 `EDITOR=true` 下应成功，后者在外层已命中时也应成功。6. 在 learning trace 里补一条包含互斥 flag 的 fixture，验证 `HookEvents`、`ProcessedCode` 与 editor-only block 记录在修复前后保持稳定，只是错误路径被正确短路。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果 short-circuit helper 写错，容易把原本应报错的可达分支也误判成“不可达”而静默通过；因此必须用互斥分支、嵌套分支和真正拼写错误三类测试同时锁住行为。 |
| 前置依赖 | 无。建议在处理 `Issue-3` 的大函数拆分时一并把条件编译入口抽成独立 helper，降低回归风险。 |
| 验证方式 | 1. 新增自动化覆盖“已命中 `#elif` 后的未知 flag”和“外层失活时的内层未知 flag”，修复后两者都应预处理成功。2. 保留一条 truly reachable 的 `#if SOME_UNKNOWN_FLAG` 用例，确认仍然报 `Invalid preprocessor condition`。3. 对比修复前后正常脚本的 `ProcessedCode` 和 `EditorOnlyBlockLines`，确认只改变错误路径，不改变已知正确语义。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-61 | Defect | 在继续扩展条件编译/模块语法前先修，先恢复不可达分支的正确短路语义 |

---

## 发现与方案 (2026-04-08 23:52)

### Issue-62：`ParseIntoChunks()` 已经报出 fatal error 后，explicit `import` 解析仍会继续改写依赖图与 chunk

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 行号 | 228-243，439-497，3296-3353，3936-3939；69-220；80-243 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 23，这里补齐执行方案。已验证事实：`Preprocess()` 在 228-230 行先对全部文件执行 `ParseIntoChunks()`，但 232-239 行会在检查 `bHasError` 之前直接进入 explicit `import` 排序；真正的 early-out 被放到 241-243 行。与此同时，`ParseIntoChunks()` 在非法 `#elif/#else/#endif` 与缺失 `#endif` 时会立刻把 `bHasError` 置为 `true`；而 `ProcessImports()` 仍会继续递归、向 `File.Module->ImportedModules` 追加依赖、调用 `ReplaceWithBlank()` 改写 chunk，并把文件标记为 `bImportsResolved`。现有自动化只有 basic parse / macro / happy-path import 三类用例，没有覆盖“fatal directive error + import”组合。 |
| 根因 | 预处理流水线把“chunk 词法/条件编译校验”和“import resolver 提交副作用”串在同一个控制流里，但缺少位于两者之间的失败屏障；resolver 假设 parse 阶段一定成功。 |
| 影响 | 一次本应立即终止本轮预处理的 fatal parse error，会继续触发 import DFS、污染 `ImportedModules`、抹掉原始 import 源码，并制造额外的二次诊断。错误恢复因此失去原子性：失败模块不再是“只留下诊断”，而是“带着半提交依赖状态失败”。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在 parse 阶段和 import resolver 之间建立硬性失败屏障，并把 import 结果改成“先缓冲、后提交”的原子提交流程。 |
| 具体步骤 | 1. 在 `Preprocess()` 中把 `ParseIntoChunks()` 循环后的 `if (bHasError) return false;` 前移到 explicit `import` 排序之前，禁止 parse-failed 文件进入 `ProcessImports()`。2. 给 `ProcessImports()` 增加最外层护栏：若 `bHasError` 已经为 true，立即返回，不再继续递归、blank 源码或设置 `bImportsResolved`。3. 将 `ProcessImports()` 的副作用拆成两阶段：第一阶段只产出临时的拓扑顺序和 `ResolvedImports`；第二阶段仅在整个 resolver 成功后，才统一写入 `Module->ImportedModules` 并执行 `ReplaceWithBlank()`。4. 把 `bImportsResolved`/`bIsResolvingImports` 的更新限定在成功提交阶段，失败路径必须恢复到“未解析”状态，避免后续调试 hook 或未来增量 resolver 看到半成品状态。5. 在 `AngelscriptPreprocessorTests.cpp` 新增两类回归：一类用非法 `#elif/#else` + `import Foo.Bar;`，另一类用缺失 `#endif` + `import Foo.Bar;`；两者都应直接 `Preprocess()==false`，并断言 `ImportedModules` 不被填充、import 原文不会被部分 blank。6. 在 learning trace 用例补一条负向 fixture，确认 fatal parse error 时不会再触发 import 排序产生的额外 trace 噪音。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只加 `Preprocess()` 早退而不同时收口 `ProcessImports()` 的提交时机，未来其他入口仍可能在失败态下复用 resolver 并重现半提交问题；需要把“失败不提交副作用”落实成 resolver 合同，而不是单点 if 判断。 |
| 前置依赖 | 建议与 `Issue-4` 的 `ImportResolver` 两阶段化设计联动，实现时共享同一套“先解析图、后提交副作用”的接口。 |
| 验证方式 | 1. 构造 malformed directive + import fixture，断言 `Preprocess()` 在 import 排序前失败。2. 失败后检查 `File.Module->ImportedModules` 与 import 所在 chunk，确认没有新增依赖也没有被部分 blank。3. 回归正常 explicit import 链，确认成功路径的拓扑顺序与现有行为一致。 |

### Issue-63：`#else` / `#endif` 只做前缀匹配，directive 拼写错误会被静默降级成合法分支

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 行号 | 3321-3361，4349-4360；69-220 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 30，这里补齐执行方案。已验证事实：`ParseIntoChunks()` 识别 `#else` 和 `#endif` 时，只检查当前位置是否以前缀 `"#else"` / `"#endif"` 开头，并不像 `#if/#ifdef/#ifndef/#elif` 那样验证关键字后是否是空白或行结束。随后 `KillRawLine()` 会把同一行后续文本直接抹掉。结果是 `#else if FEATURE`、`#elseif FEATURE`、`#endif_typo` 这类本应报语法错的文本，会被当成合法 `#else` 或 `#endif` 处理，原始 typo 还会被清理掉。现有预处理测试没有任何 directive typo 负向样例。 |
| 根因 | directive lexer 对 `#else/#endif` 采用了裸前缀匹配，而没有复用统一的 token-boundary 判断；源码清理又发生在诊断之前，进一步掩盖了原始拼写错误。 |
| 影响 | 条件编译里的常见笔误不会 fail-fast，而是静默改变 `IfDefStack` 状态和后续源码裁剪结果。最危险的场景是把不支持的 `#else if` 误写成一条语句后，整个分支会被当成纯 `#else` 处理，影响 import/include/module 可达性且排查时看不到真实 typo。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `#else/#endif` 收口到统一的 directive keyword matcher，只有关键字边界合法时才更新条件栈，否则给出明确语法错误并保留原始文本上下文。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取共享 helper，例如 `TryMatchDirectiveKeyword(...)` 或 `IsDirectiveKeywordBoundary(...)`，统一校验“关键字后只能是空白、注释起始或行结束”。2. 用该 helper 重写 `#else` 与 `#endif` 入口，和 `#if/#ifdef/#ifndef/#elif` 共用同一套词法边界规则，禁止继续使用裸 `Strncmp("#else", 5)` / `Strncmp("#endif", 6)`。3. 对 `#else if`、`#elseif`、`#endif_typo` 这类命中前缀但边界非法的情况，直接在当前行报明确错误，例如 `Unsupported directive '#else if'; use '#elif'.` 或 `Invalid trailing tokens after #endif.`，而不是按合法 directive 更新栈状态。4. 将 `KillRawLine()` 的调用时机后移到“directive 已经被完整识别且验证通过”之后，避免在错误分支里先抹掉原始文本再报错。5. 在 `AngelscriptPreprocessorTests.cpp` 新增回归：`#else if FEATURE` 应报“使用 #elif”，`#elseif FEATURE` 与 `#endif_typo` 应报 syntax error，并断言 `ProcessedCode` 不会被当作合法分支裁剪。6. 若后续继续推进 `Issue-5` 的 `#include` 或 `Issue-35` 的 tab/空白兼容，也必须复用这同一个 matcher，避免 directive 家族继续出现边界分叉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | S |
| 风险 | 若直接把所有带 trailing token 的 `#else/#endif` 都改成 hard error，少量历史脚本里依赖这类宽松解析的写法会开始报错；但这类写法本身已经在静默改变分支语义，收紧合同属于必要的兼容性破坏。 |
| 前置依赖 | 建议与 `Issue-35` 的 directive keyword helper 同批实现，避免先修 `#else/#endif`，随后再为 `#if/#ifdef/#ifndef/#elif` 重写一套入口。 |
| 验证方式 | 1. `#else if FEATURE` 不再被当成合法 `#else`，而是稳定报出“应改用 #elif”的错误。2. `#endif_typo` 不再关闭条件栈。3. 正常 `#else` / `#endif` 和带缩进版本保持原有行为不变。 |

### Issue-64：`UPROPERTY/UFUNCTION` 在 `!FLAG` 与 `#else` 活跃分支里会被误判为非法条件

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 行号 | 47-50，3282-3287，3333-3339，3618-3643；69-220；80-243 |
| 问题 | 承接 `Documents/AutoPlans/Preprocessor_Analysis.md` 的发现 53，这里补齐执行方案。已验证事实：构造函数会把 settings 中声明的 `PreprocessorFlags` 注册进 map；`#if` 分支把原始条件文本直接存入 `IfDefStack.Condition`，而 `#else` 又通过给该字符串加上或去掉 `!` 来翻转条件。后面反射宏校验时，`UPROPERTY/UFUNCTION` 只接受 `Condition == "EDITOR"`、`Condition == "EDITORONLY_DATA"`，或 `PreprocessorFlags.Contains(Condition) && PreprocessorFlags[Condition]`。因此一旦活跃分支来自 `#if !FLAG` 或 `#else`，条件文本会变成 `!FLAG`，即使它本质上仍由一个已注册 flag 控制，也会在 3631-3632 行报 `Cannot put a UPROPERTY or UFUNCTION inside preprocessor conditions...`。现有预处理测试没有任何“条件分支内反射宏”样例。 |
| 根因 | 条件栈把“分支表达式原文”直接当成后续合法性校验的 key 使用；`#else` 通过字符串拼接表达逻辑翻转，但宏校验没有把 `!FLAG` 归一回底层 flag 身份。 |
| 影响 | 脚本无法在否定配置分支或 `#else` 分支里合法声明 `UPROPERTY/UFUNCTION`，例如“runtime 构建下暴露另一组反射 API”这种常见写法会被预处理器直接拒绝。这会把条件编译能力人为限制成“只支持正向 flag 分支”，与错误消息声称的“flags declared in configuration”合同不一致。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `IfDefStack` 从“原始字符串条件”升级为结构化条件状态，让反射宏合法性判断消费规范化后的底层 flag，而不是直接消费 `!FLAG` 文本。 |
| 具体步骤 | 1. 扩展 `FIfDef` 或等价状态结构，至少保存 `BaseCondition`、`bNegated`、`bIsElseBranch` 和当前 `bValue`，不要再只把逻辑状态编码进单个 `Condition` 字符串。2. 修改 `#if/#elif` 入口，在简单 flag 表达式场景下把 `!FLAG` 规范化成 `BaseCondition=FLAG, bNegated=true`；`#else` 只翻转布尔状态或显式标记 `bIsElseBranch`，不要再通过字符串前缀拼接表达逻辑。3. 提取共享 helper，例如 `IsAllowedReflectionCondition(const FIfDef&)`，由它统一判断某个活跃分支是否属于 `EDITOR`、`EDITORONLY_DATA` 或配置 flag 控制；`UPROPERTY/UFUNCTION` 校验、`FMacro::bEditorOnly` 和未来其它条件消费者都复用这同一个 helper。4. 若某个分支条件不是简单 flag，而是未来更复杂的表达式，helper 应明确返回“不支持承载反射宏”并给出结构化诊断，避免继续落回字符串猜测。5. 在 `AngelscriptPreprocessorTests.cpp` 增加回归：`#if !EDITOR ... UFUNCTION ... #endif` 在非 editor 上下文应通过；`#if EDITOR ... #else UPROPERTY ... #endif` 的 `#else` 分支在 runtime 上下文应通过；再保留一条 truly unsupported complex condition 的负向样例。6. 在 learning trace 中补条件宏 fixture，验证修复前后 chunk 结构和宏捕获稳定，改变的只是误报路径。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | 一旦把条件合同从字符串切到结构化状态，需要同步检查 `UpdateEditorBlockLines()`、`#restrict usage` 和其它依赖 `IfDefStack.Condition` 的调用点；如果只修反射宏入口而不统一消费者，新的状态分叉会再次出现。 |
| 前置依赖 | 建议与 `Issue-32`、`Issue-33`、`Issue-51` 联动，统一条件编译状态机和 editor-only 条件模型。 |
| 验证方式 | 1. `#if !EDITOR` 或等价否定分支内的 `UFUNCTION` 在 runtime 上下文不再误报。2. `#else` 活跃分支内的 `UPROPERTY` 能被正确捕获并写入宏元数据。3. truly unsupported 的复杂条件仍会给出明确错误，不会被静默放行。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-62 | Defect | 先修 parse 阶段到 import resolver 之间的失败屏障，恢复错误恢复原子性 |
| P1 | Issue-64 | Defect | 与条件编译状态机修复同批处理，先收口 `IfDefStack` 的结构化条件语义 |
| P2 | Issue-63 | Defect | 紧随其后，收紧 `#else/#endif` 词法边界，避免 typo 静默改写分支 |

---

## 发现与方案 (2026-04-09 00:07)

### Issue-65：`GetReturnInit()` 把首轮 `bScriptFloatIsFloat64` 永久缓存进静态表，delegate 生成代码与当前编译设置脱节

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 500-535，675-678，702-705，2009-2012；900-903，1410-1413 |
| 问题 | 已验证事实：`GetReturnInit()` 用 `static bool bHaveInitMap` + `static TMap<FString, FString> InitMap` 只在第一次调用时读取 `GetMutableDefault<UAngelscriptSettings>()->bScriptFloatIsFloat64`，然后把 `float -> " = 0.0"` 或 `" = 0.f"` 永久写死到进程级缓存；后续 delegate 包装代码生成在 675-678、702-705、2009-2012 行都会复用这份静态结果。与此同时，编译器实际使用的 AngelScript float 模式会在 engine 初始化与重建时按当前 `ConfigSettings->bScriptFloatIsFloat64` 重新写入 `asEP_FLOAT_IS_FLOAT64`。也就是说，预处理器生成代码与底层编译设置并不是同一个快照来源。 |
| 根因 | 生成代码阶段把“与 settings 相关的返回值初始化模板”做成了无版本的进程级静态缓存，没有挂到 `FAngelscriptPreprocessor` 的上下文，也没有进入任何可失效的 cache key。 |
| 影响 | 只要同一进程内切换 `bScriptFloatIsFloat64` 并重新编译，engine 已经按新设置编译脚本，delegate 包装代码却仍沿用第一次预处理时的返回初始化文本；`ProcessedCode` 与 `CodeHash` 因此不再准确反映当前有效 settings。这会直接削弱 precompiled cache、增量预处理和“相同输入得到相同输出”的合同。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 去掉 `GetReturnInit()` 的进程级静态设置缓存，把返回值初始化模板纳入显式 preprocess context 和 hash 合同。 |
| 具体步骤 | 1. 将 `GetReturnInit()` 改成显式接收当前有效 settings，或改成 `FAngelscriptPreprocessor` 的成员 helper，直接消费本轮 preprocess 的 context/engine snapshot，而不是 `static InitMap`。2. 把 `float` 初始化模板从惰性全局缓存改成每次按 `bScriptFloatIsFloat64` 计算，或把“effective float init map”预先构造到 `FAngelscriptPreprocessContext` 中，确保不同 settings 下的 `ProcessedCode` 真实变化。3. 若沿用 memoization，cache key 至少要包含 `bScriptFloatIsFloat64` 和相关数值类型设置版本，禁止再出现“第一次命中后永久复用”的无版本缓存。4. 与现有 `Issue-2` 的 context hash 设计联动，把 delegate/generated wrapper 使用到的 settings 一并纳入 `PreprocessContextHash`，避免 cache 命中继续忽略这类配置差异。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 增加双配置回归：同一份含返回值 delegate/event wrapper 的脚本，在 `bScriptFloatIsFloat64=true/false` 两次预处理后应产生不同的 `ProcessedCode` 或显式 dependency/context hash。6. 如需兼顾性能，优先把基础类型初始化模板做成 `constexpr`/局部静态无状态表，只对 `float` 这一项按 context 动态决定，避免为了解除全局缓存而回退成大范围分配。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果项目当前隐式依赖“切换 float 模式但不触发 wrapper 源码变化”的旧行为，修复后 cache miss 次数会增加；但这正是让 hash 与有效 settings 重新一致所必需的变化。 |
| 前置依赖 | 建议与 `Issue-2` 的显式 preprocess context 一起落地，避免先修单点 helper，后续再补第二套 settings 传递链。 |
| 验证方式 | 1. 在同一进程中切换 `bScriptFloatIsFloat64` 两次预处理同一脚本，确认 delegate 包装代码或 context hash 会发生变化。2. 对比修复前后 `CodeHash`/cache 命中行为，确认 settings 变化不再被静默复用旧结果。3. 回归默认单配置编译，确认普通 delegate/event wrapper 代码生成保持不变。 |

### Issue-66：escaped quote 会打乱 specifier/default 的引号状态，逗号、`)` 与 `//` 被误判成结构分隔符

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 317-437，1368-1370，1481-1482，2278-2280，4363-4410 |
| 问题 | 已验证事实：`ParseSpecifier()` / `ParseSpecifiers()` 在 368-369、421-422 行遇到任意 `\"` 都直接翻转 `bInQuotes`，完全不检查是否为转义引号；`StripCommentsFromLine()` 在 4373-4375 行也采用了同样的“见 `\"` 就切换字符串状态”的逻辑。后续 `ProcessDefaults()` 会在 1368-1370 行先对 defaults 文本调用 `StripCommentsFromLine()`，`ProcessFunctionMacro()` 与 `ProcessClassMacro()` 也分别在 1481-1482、2278-2280 行把 `Macro.Arguments` 交给 `ParseSpecifiers()`。推断：一旦 metadata/default 字符串里合法包含 escaped quote，例如 `\\\"` 后面再跟 `,`、`)` 或 `//`，解析器会提前退出字符串态，把这些本应属于字符串内容的字符当成 specifier 分隔、参数结束或注释起点。 |
| 根因 | 预处理器内部存在多套“引号状态”实现，但它们都只按单字符 `\"` 翻转状态，没有统一的 escape-aware lexer 规则。 |
| 影响 | 这类输入不会稳定报 syntax error，而是更隐蔽地变成“specifier 被截断”“后续 specifier 被拆成额外条目”“defaults 里的尾部文本被当注释抹掉”。结果是 `UCLASS/UFUNCTION/UPROPERTY` 元数据和 generated defaults 可能在预处理阶段被静默污染，后续编译错误也会落到错误位置。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 提取统一的 escape-aware quoted-string scanner，收口 `ParseSpecifiers()`、`StripCommentsFromLine()` 和后续 statement parser 的引号语义。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 新增共享 helper，例如 `IsEscapedQuote(const FString&, int32 Pos)`、`AdvanceQuotedString(...)` 或更通用的 cursor API；字符串状态只能在“未被奇数个反斜杠转义的引号”处切换。2. 用该 helper 重写 `ParseSpecifier()` / `ParseSpecifiers()` 的 `bInQuotes` 切换逻辑，确保字符串内的 `,`、`)`、`=`、`(` 不再因 escaped quote 提前泄露到外层结构语法。3. 用同一 helper 重写 `StripCommentsFromLine()`，让 `//`、`/*` 只有在真正脱离字符串后才被当成注释起点，避免 defaults 文本中的 URL、注释样式示例或带 `//` 的字符串被抹空。4. 把这套 quoted-string 语义并入 `Issue-3` 规划中的共享 cursor/statement parser，禁止未来 `import`、`#include`、restriction、macro specifier 再各自维护不同的转义规则。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 增加回归：包含 escaped quote + 逗号的 `UFUNCTION`/`UCLASS` metadata、以及包含 escaped quote + `//` 的 defaults 文本，断言 specifier 数量、最终 meta value 和 defaults 内容保持完整。6. 再补一条负向样例，验证真正未闭合的字符串仍会触发明确错误，而不是因“更宽松的 escape 处理”被静默吞掉。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只修 `ParseSpecifiers()` 而不同时修 `StripCommentsFromLine()`，defaults 与 metadata 会继续各自维护一套不同的字符串规则；需要一次性把所有 quoted-string helper 收口到同一真源。 |
| 前置依赖 | 建议与 `Issue-3` 的共享 cursor/helper 拆分一起做，避免又新增一套局部转义逻辑。 |
| 验证方式 | 1. 含 escaped quote 的 metadata 不再被错误拆成多个 specifier。2. 含 `//` 文本的 defaults 字符串在预处理后仍完整保留。3. 真正未闭合的字符串仍会被稳定诊断，证明修复没有把语法错误误放行。 |

### Issue-67：regex 后处理越过词法边界直接改写 `ProcessedCode`，字符串/注释与 `Namespace::Type` 都会被误伤

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 行号 | 4008-4142；22-26 |
| 问题 | 已验证事实：`PostProcessRangeBasedFor()` 与 `PostProcessLiteralAssets()` 都在 `CondenseFromChunks()` 之后，直接对整份 `File.ProcessedCode` 跑 `FRegexMatcher`；`RangeBasedForPattern` 只按文本形状匹配 `for (...)`，`LiteralAssetsPattern` 只接受 `asset <Name> of <Type>` 且 type 被写死为 `([A-Za-z0-9]+)`。这两条 pass 都没有任何 lexer state 去排除 string literal、line comment、block comment，也没有支持脚本里已经存在的 namespace-qualified 类型语法。仓库测试 22-26 行已经验证脚本侧支持 `MyNamespace::GetValue()`，但 range-based-for 模式在捕获左侧声明时会把 `:` 当成硬分隔符，literal asset 模式又根本不接受 `::`。 |
| 根因 | 语法级变换被放到了“整份文本正则替换”层实现，没有复用前面 chunk/parser 已经维护过的词法状态，也没有为 loop header / asset declaration 建立最小可验证的结构化 parser。 |
| 影响 | 一方面，字符串和注释里的 `for (Foo : Bar)`、`asset MyAsset of DataAsset` 文本也可能被当成真实代码改写，导致 `ProcessedCode` 被静默篡改；另一方面，真正合法的 `for (const MyNamespace::Type& Value : Values)` 或 `asset MyConfig of MyNamespace::Settings` 又会被截断或完全漏处理。结果是后处理阶段既会误改文本，又会漏改合法语法，属于高风险的源码变换缺陷。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把这两类后处理从“整串 regex 替换”升级为基于 cursor/chunk 的结构化 pass，只在已确认的代码区域和合法语法节点上执行变换。 |
| 具体步骤 | 1. 为 `range-based-for` 抽出 `ParseRangeForHeader()`，基于括号深度与 token 边界扫描 loop header，只在最外层 `:` 上分割 iterator 变量与 iterable；明确支持 `Namespace::Type`、`const`、引用和模板类型。2. 为 literal asset 抽出 `ParseLiteralAssetDecl()`，限定它只在顶层代码区域生效，并把 type 词法扩展为 identifier chain（至少支持 `::`、模板/泛型边界若脚本允许则同步支持），不再依赖 `([A-Za-z0-9]+)`。3. 这两条 pass 都要消费 `Issue-3` 规划中的 shared cursor 或 chunk 视图，显式跳过 string/comment span，禁止再直接对整份 `ProcessedCode` 运行裸 regex。4. 若短期内还保留 regex 辅助匹配，也只能把它当“候选起点筛选器”；真正提交替换前必须再经过结构化 parser 二次确认，确保命中位置确实在代码区域且语法完整。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 与 `Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` 增加回归：字符串/注释中的 `for (...)` 与 `asset ... of ...` 不应被改写；`for (const MyNamespace::Type& Value : Values)` 与 `asset MyConfig of MyNamespace::Settings` 应被正确转换。6. 为这两条 pass 增加 trace/统计，记录“扫描到的候选数、最终接受的结构化命中数”，便于后续确认重构确实缩小了误报面。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果只扩 regex 而不引入结构化确认，会继续在别的边界条件上误命中；修复必须把“词法过滤 + 结构化确认 + 替换提交”做成完整链路。 |
| 前置依赖 | 建议依赖 `Issue-3` 的 cursor/helper 底座，否则这两个后处理很容易再次各自实现一套局部词法规则。 |
| 验证方式 | 1. 注释和字符串中的匹配文本不再改变 `ProcessedCode`。2. `Namespace::Type` 的 range-based-for 与 literal asset 类型都能被正确处理。3. 对比修复前后正常 fixture 的输出，确认只改变误报/漏报路径，不影响已有合法转换。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-67 | Defect | 在 `Issue-3` 的 cursor/helper 底座可用后优先修复，先阻止后处理对字符串/注释的静默源码改写 |
| P1 | Issue-66 | Defect | 紧随其后，统一 escaped quote 语义，避免 metadata/defaults 被静默截断 |
| P2 | Issue-65 | Defect | 与 `Issue-2` 的 context/hash 合同一并收口，修复 settings 变化对生成代码与 cache 的失真 |

---

## 发现与方案 (2026-04-09 00:25)

### Issue-68：`GetModuleByFilename()` 不校验 root 边界且不先规范化输入，按文件定位模块会把别名路径与 root 外路径带进错误 fallback

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | 3029-3055，5802-5849；936-963 |
| 问题 | 已验证事实：`GetModuleByFilename()` 先仅用 `Section.AbsoluteFilename.Equals(Filename, IgnoreCase)` 做原样比较，没有对输入执行 `NormalizeFilename`、`CollapseRelativeDirectories` 或分隔符统一；直匹配失败后，又会对每个 `AllRootPaths` 调用 `MakePathRelativeTo_IgnoreCase()`。而这个 helper 只在“不同 drive”时返回 `false`，对同 drive 但根本不在该 root 下的文件，也会在 5835-5848 行生成带 `../` 的相对路径并返回 `true`。调用方随后无条件把这个结果做 `.Replace(".as","").Replace("/", ".")` 并交给 `GetModule()`。`GetModuleByFilenameOrModuleName()` 又把 filename lookup 放在 module-name fallback 之前，debug server 的 breakpoint 路径会直接消费这条结果。 |
| 根因 | 文件名解析同时缺少两层约束：1. 输入路径没有先归一化到 canonical absolute path；2. root-relative fallback 没有验证“目标文件必须真的是该 root 的后代”。结果是路径别名与 root 外路径都会被送进 path-to-module 规则。 |
| 影响 | 同一物理脚本若以 `..`、不同斜杠形式或其它别名路径进入 `GetModuleByFilename()`，直匹配可能 miss，随后落到更宽松也更不可靠的 root-relative fallback；而完全不属于任一 script root 的文件，在同 drive 条件下也会被尝试解释成模块名。这会让 `GetModuleByFilenameOrModuleName()`、debug breakpoint 绑定和按文件导航行为依赖输入路径形态，而不是稳定依赖真实模块身份。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 先把 filename lookup 收口到“canonical absolute path + 明确 root containment”的双阶段合同，再让 fallback 只处理真正位于脚本根下的文件。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.cpp` 提取共享 helper，例如 `CanonicalizeAbsoluteScriptFilename(const FString&)`，统一执行 `ConvertRelativePathToFull`、分隔符归一、`CollapseRelativeDirectories`、`NormalizeFilename`，并在 `GetModuleByFilename()` 入口先对查询路径做一次 canonicalization。2. 将 `Module->Code[*].AbsoluteFilename` 在写入 `FAngelscriptModuleDesc::FCodeSection` 时也同步保存 canonical 形式，直匹配优先比较 canonical absolute path，而不是继续用原始字符串。3. 修改 `MakePathRelativeTo_IgnoreCase()` 或在其外层新增 `TryMakePathRelativeInsideRoot(...)`：只有当 canonical target 以 canonical root 为前缀时才允许返回相对路径；若结果需要以 `../` 开头才能表达，必须视为“不属于该 root”并跳过，而不是继续转换成模块名。4. 收紧 `GetModuleByFilenameOrModuleName()`：当 filename lookup 失败或被判定为 off-root 时，应明确回退到 `ModuleName` 分支，并在调试日志里记录“按文件名未命中/被拒绝”的原因，避免静默吞掉更可靠的 module-name 输入。5. 与现有 `Issue-54` / `Issue-58` 的 canonical filename index 规划对齐：live path、debugger 和 future precompiled restore 最终都应查询同一份 `CanonicalAbsoluteFilename -> ModuleIdentity` 索引，而不是各自再做一遍 root-relative 推导。6. 新增回归：同一脚本分别用原始绝对路径、带 `..` 的绝对路径和混合分隔符路径查询，三者都应命中同一模块；再补一条 root 外文件路径，断言 `GetModuleByFilename()` 返回空且 `GetModuleByFilenameOrModuleName()` 会正确落到 `ModuleName` fallback。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Debugger/AngelscriptDebuggerBreakpointTests.cpp` |
| 预估工作量 | M |
| 风险 | 如果现有外部工具意外依赖“root 外文件也会尝试推导模块名”的宽松行为，修复后会从模糊匹配变成明确 miss；需要配合调试日志说明新的拒绝原因，避免被误解成回归。 |
| 前置依赖 | 无；但建议与 `Issue-54` 的 `ModuleIdentity` 设计复用同一 canonical path helper。 |
| 验证方式 | 1. 按文件设置断点时，原始路径与 `..` 别名路径都命中同一模块。2. 传入 root 外文件路径时，`GetModuleByFilename()` 稳定返回空，不会构造带 `../` 的伪模块名。3. `GetModuleByFilenameOrModuleName()` 在 filename miss/off-root 场景下会落到 `ModuleName` fallback，而不是因错误 filename 解析抢先命中错误模块。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-68 | Defect | 在统一 `ModuleIdentity` / canonical path 前先加 root containment 护栏，避免调试和按文件定位继续走宽松 fallback |

---

## 发现与方案 (2026-04-09 00:27)

### Issue-69：`#if/#elif` 的否定条件只接受 `!FLAG` 紧贴写法，`! FLAG` 会被误判成未知 flag

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 行号 | 3280-3287，3293-3314，4328-4346；69-220 |
| 问题 | 已验证事实：`#if` 与 `#elif` 分支先用 `ReadIdentifier()` 读取条件文本，再把结果原样传给 `ParsePreProc()`。`ParsePreProc()` 仅在首字符为 `!` 时做 `Lookup = Lookup.Mid(1)`，随后直接 `PreprocessorFlags.Find(Lookup)`，中间没有再次 `TrimStartAndEnd()` 或跳过 `!` 后的空白。因此脚本若写成 `#if ! EDITOR` 或 `#elif ! TEST`，`Lookup` 会变成带前导空格的 `" EDITOR"` / `" TEST"`，最终稳定落到 `Invalid preprocessor condition`。检索现有 `AngelscriptPreprocessorTests.cpp`，未发现任何 `#if ! ...` 或 `#elif ! ...` 的回归样例。 |
| 根因 | 简单 preprocessor 条件被实现成“整段字符串 + 首字符特判”，没有做最基本的 tokenization；一元 `!` 与 identifier 之间的可选空白没有进入语法合同。 |
| 影响 | 仅仅因为格式化风格不同，`!FLAG` 与 `! FLAG` 会得到完全不同的预处理结果。对脚本作者来说，这不是语义错误，而是词法器过度依赖紧贴写法；一旦有人手工换行/对齐条件，原本合法的否定分支就会在预处理期失败。这个缺陷还会放大 `Issue-64` 的条件归一化问题，因为一部分 `!FLAG` 场景甚至走不到后续宏合法性检查阶段。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 simple flag condition 引入最小 token parser，显式支持 `!` 后的可选空白，并把规范化结果统一回写到条件状态。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取 helper，例如 `ParseSimpleFlagCondition(const FString& Raw, FSimpleFlagCondition& OutCondition)`，输出 `Identifier`、`bNegated`、`bValidSyntax`；helper 必须在识别 `!` 后跳过任意前导空白，再读取真实 flag 名。2. 让 `#if/#elif` 都改为调用该 helper，而不是继续把 `ReadIdentifier()` 的整段结果直接传给 `ParsePreProc()`；只有 helper 成功时才进入 flag lookup，语法失败时给出更准确的错误，例如 `Expected flag name after '!'.`。3. `ParsePreProc()` 若保留，也应只接收规范化后的 `Identifier + bNegated`，不要继续对原始字符串做首字符切片；同时把 `IfDefStack.Condition` 统一规范化为 `!FLAG` 或 `FLAG`，避免相同语义因空白差异进入不同缓存/诊断路径。4. 与 `Issue-64` 联动，把这份结构化条件直接复用于反射宏合法性判断，避免修完 `! FLAG` 后又在后续阶段重新退化成字符串比较。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 新增回归：`#if !EDITOR` 与 `#if ! EDITOR` 应产生相同 `ProcessedCode`；`#elif ! TEST` 同理；再补一条负向样例 `#if !   `，断言报语法错误而不是未知 flag。6. 若后续继续扩展 preprocessor expression 能力，保留该 helper 作为 unary/token 层入口，不要再回退到整串 `Trim + 首字符判断` 的做法。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | S |
| 风险 | 若简单条件解析只局部修 `#if` 而不同步 `#elif` 与 `IfDefStack.Condition` 规范化，后续宏校验与 editor-only 记录仍会看到两套不同表示；需要一次性统一 simple flag condition 的输入格式。 |
| 前置依赖 | 建议与 `Issue-64` 的结构化条件状态一起实现，但也可以先独立修复 tokenization。 |
| 验证方式 | 1. `#if !EDITOR` 和 `#if ! EDITOR` 在同一上下文下得到完全一致的输出。2. `#elif ! TEST` 不再因为空格被误判成未知 flag。3. `#if !   ` 之类缺失标识符的写法会得到明确语法错误，而不是模糊的 `Invalid preprocessor condition`。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-69 | Defect | 可与 `Issue-64` 同批处理，先把 simple flag condition 的 tokenization 收口 |

---

## 发现与方案 (2026-04-09 00:44)

### Issue-70：`AddFile()` 到 `GetModulesToCompile()` 整条链路把“一个源文件 = 一个模块”写死，`#include` 展开与多文件模块无法自然落位

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h` |
| 行号 | 75-83，91-101，289-304；15-21 |
| 问题 | 已验证事实：`AddFile()` 在 96-101 行对每次调用都执行 `MakeShared<FAngelscriptModuleDesc>()`，并直接把这份新建 descriptor 绑定到当前 `FFile`；`Preprocess()` 在 289-304 行又只会把当前 `FFile` 产出的单个 `FCodeSection` 写回 `File.Module`；最后 `GetModulesToCompile()` 在 79-81 行只是按 `File.Module` shared-ref 去重返回结果。也就是说，当前 public API 从入口到出口都把“源文件 ownership”和“模块 ownership”绑定成 1:1。推断：一旦按 `Issue-5` 落地真正的 `#include` 展开、或未来需要把多个 source unit 组装成同一个 module descriptor，这条链路没有任何自然的聚合点，只能继续通过共享/复写 `File.Module` 做旁路。 |
| 根因 | `FAngelscriptPreprocessor` 的核心数据模型是 `FFile -> ModuleDesc` 直接拥有关系，而不是“source unit 经过解析后再归并到 module artifact”。模块系统因此缺少独立的 module 聚合层。 |
| 影响 | 这会直接阻塞两类当前已在路线图上的能力：1. `#include` 需要把多个物理文件折叠进同一模块，但现有 API 默认每个输入文件各自产生一个 descriptor；2. 增量预处理/descriptor cache 需要按 module 复用中间产物，但现有缓存边界只能围绕临时 `FFile` 做。继续在这套 1:1 模型上叠功能，只会让 include、module cache 和 diagnostics provenance 都依赖越来越脆弱的共享指针旁路。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 preprocessor 的输入模型从“文件直接产出模块”改成“source unit 先产出 artifact，再由 module assembler 归并成模块”。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.h/.cpp` 中引入独立的 `FSourceUnit`/`FParsedSourceArtifact` 结构，`AddFile()` 只负责登记源码单元，不再当场分配 `FAngelscriptModuleDesc`。2. 新增 `FModuleAssembly` 或等价聚合层，key 使用后续 `Issue-54` 规划中的 `ModuleIdentity`；显式 import、future include、generated overlay 都只向这个聚合层追加 section/metadata。3. 将 `Preprocess()` 末尾的 `Section` 提交改为“按 module identity 汇总多个 source artifact”，允许一个 module 含多份 `FCodeSection`，而不是继续默认一 file 一 module。4. 重写 `GetModulesToCompile()`，让它直接遍历 module 聚合表输出稳定顺序的模块结果，删除当前基于 `Files[*].Module` shared-ref 的间接去重。5. 为兼容现有单文件模块路径，保留一个适配层：若没有 include/聚合输入，每个 source unit 仍会自然落成单 section 模块，但底层模型已不再依赖这一假设。6. 同步更新测试：新增“两个 source unit 归并到同一 module artifact”的预处理 fixture，为后续 include 展开和多 section diagnostics 提前锁定合同。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | L |
| 风险 | 这是数据模型级改造，会触及 compile、cache 和 diagnostics 的 section provenance；若只改 preprocessor 而不让 downstream 改成按 module artifact 消费，会再次出现 live/cache 双轨语义。 |
| 前置依赖 | 建议与 `Issue-5`、`Issue-54`、`Issue-57` 联动设计，统一 include section、module identity 和多 section hash 合同。 |
| 验证方式 | 1. 新增预处理测试，验证两个 source unit 可稳定归并到同一个 module，并保留正确 section 顺序。2. 在不启用 include 的现有单文件路径下回归，确认输出模块集合与当前行为一致。3. 为未来 include prototype 预留 fixture，验证 owner module 修改/被包含文件修改都能落到同一 module artifact。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-70 | Architecture | 在继续实现 `#include` 与多 section cache 前先改数据模型，避免新功能继续堆在一 file 一 module 假设上 |

---

## 发现与方案 (2026-04-09 00:48)

### Issue-71：`FAngelscriptModuleDesc` 同时承载预处理产物、缓存恢复结果和运行时编译状态，artifact 边界被一个可变“大对象”吞掉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1272-1333；91-101，289-304；2752-2783；4284-4299 |
| 问题 | 已验证事实：`FAngelscriptModuleDesc` 在 1272-1333 行同一个 struct 内同时保存 `Code`、`ImportedModules`、`UsageRestrictions` 这类预处理产物，以及 `ScriptModule`、`PrecompiledData`、`bCompileError`、`bLoadedPrecompiledCode` 这类运行时编译状态；`AddFile()` 在 91-101 行由 preprocessor 直接创建这份 descriptor，`Preprocess()` 在 289-304 行继续把 `Code`/`CodeHash` 写进去；fully precompiled 启动又在 2752-2783 行重新构造同一 struct 的一个“瘦身版本”；正常编译/缓存命中路径还会在 4284-4299 行继续回填 `ScriptModule`、`PrecompiledData` 和成功标志。也就是说，preprocess artifact、cache artifact 和 live runtime state 不是分层流转，而是在一个可变对象上不断叠写。 |
| 根因 | 模块系统缺少“不可变预处理结果”和“可变运行时实例状态”的显式分层，导致 `FAngelscriptModuleDesc` 被同时当作 AST artifact、cache schema、中间编译上下文和激活后模块句柄来使用。 |
| 影响 | 这会持续放大三类问题：1. 失败路径会把半成品状态写进本应可缓存/可重试的 descriptor；2. fully precompiled 只能恢复这个大对象的一部分字段，于是 live/cache 两条路径天然不等价；3. 增量预处理和 descriptor cache 无法安全复用“只与源码/上下文有关”的部分，因为对象里混入了 `ScriptModule*`、compile flags 和 swap-in 结果。只要不先拆边界，后续每个 cache/diagnostics 修复都要继续和这个大对象搏斗。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将 `FAngelscriptModuleDesc` 拆成“不可变预处理 artifact”与“可变运行时编译状态”两层，缓存、预处理和编译分别只消费自己那一层。 |
| 具体步骤 | 1. 在 `Core` 层新增 `FAngelscriptPreprocessedModuleArtifact`，只保存 `ModuleIdentity`、`CodeSections`、`ImportedModules/ResolvedDependencies`、`UsageRestrictions`、editor-only 范围和 future include provenance；禁止该结构出现 `ScriptModule*`、compile flags 或 cache-hit 状态。2. 新增 `FAngelscriptCompiledModuleState` 或等价 companion，专门保存 `ScriptModule`、`PrecompiledData`、`bCompileError`、`bLoadedPrecompiledCode`、swap-in/error 标志，以及仅在编译后才存在的函数/测试运行态句柄。3. 让 `FAngelscriptPreprocessor` 的输出改为 `TArray<TSharedRef<FAngelscriptPreprocessedModuleArtifact>>`；`CompileModules()` 接收 artifact 并在内部创建/更新 runtime state，而不是直接回写 preprocessor 产物。4. `FAngelscriptPrecompiledData` 只序列化 artifact 层；cache 恢复时先重建 artifact，再由运行时决定是否生成新的 `FAngelscriptCompiledModuleState`，杜绝“从磁盘读回半个 runtime descriptor”。5. 调整 diagnostics、hot reload 和 file lookup：凡是只依赖源码 provenance 的逻辑都只读 artifact；凡是需要 `ScriptModule*` 的逻辑才进入 runtime state，避免再用空 `Code`/空 `ScriptModule` 推断对象所处阶段。6. 为迁移期提供桥接：保留旧 `FAngelscriptModuleDesc` 作为只读 façade 或别名层，逐步把调用点迁移到新结构，避免一次性改爆所有消费者。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/StaticJIT/AngelscriptPrecompiledDataTests.cpp` |
| 预估工作量 | XL |
| 风险 | 这是跨预处理、编译、cache 的结构性重构；若迁移期同时保留旧 descriptor 可写路径，最容易出现“双写不同步”。需要明确单向数据流，并尽早把旧字段改成只读桥接。 |
| 前置依赖 | 建议与 `Issue-16`、`Issue-20`、`Issue-24`、`Issue-36` 联动，统一失败语义、cache artifact 和 resolved dependency contract。 |
| 验证方式 | 1. 预处理失败后，artifact 仍可保留源码诊断信息，但 runtime state 不应被创建。2. fully precompiled 与实时预处理都应先得到等价 artifact，再进入各自 compile 路径。3. 在仅修改运行时 `ScriptModule`/swap-in 状态的场景下，artifact hash 与 cache key 不应变化，证明边界已经分离。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-71 | Architecture | 在设计 artifact cache 和 precompiled schema 前优先拆分 descriptor 边界，先把源码 artifact 与 runtime state 解耦 |

---

## 发现与方案 (2026-04-09 00:52)

### Issue-72：`FAngelscriptPreprocessor` 的 public API 直接依赖 `Engine`/`Settings`，接口层已经失去独立预处理器应有的边界

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 5-6，20-24；38-71，212-215；2069，2264 |
| 问题 | 已验证事实：`AngelscriptPreprocessor.h` 在 5-6 行直接包含 `AngelscriptEngine.h` 和 `AngelscriptSettings.h`，其 public 返回值 20-21 行又直接暴露 `TArray<TSharedRef<FAngelscriptModuleDesc>>`；实现侧构造函数在 38-71 行主动读取 `FAngelscriptEngine::*CurrentContext()` 与 `UAngelscriptSettings` 默认对象，`Preprocess()` 在 212-215 行继续从 `FAngelscriptEngine::Get()` 拉取 `ConfigSettings`；调用方 `AngelscriptEngine.cpp` 也在 2069、2264 行直接在核心编译/热重载路径上栈内构造 preprocessor。换句话说，当前 preprocessor 不是“接受上下文并产生 artifact 的组件”，而是一个直接嵌在 engine singleton 和 UObject settings 里的子过程。 |
| 根因 | public contract 没有独立的 preprocessor context / artifact header，导致 module desc、settings snapshot、engine ambient state 全都通过 `Engine.h` 这一重头依赖耦合进来。 |
| 影响 | 这会同时损害三件事：1. 独立测试必须带上 engine singleton 和默认对象，难以为路径解析、import resolver、include graph 建轻量 fixture；2. 任意 `Engine.h` / settings 变更都会把 preprocessor 及其所有消费者一起拖进重编译，编译边界过宽；3. 即使实现了 `Issue-70/71` 的 artifact 分层，只要 public API 仍强依赖 `Engine`，预处理器仍无法作为 standalone、可缓存、可替换的管线阶段复用。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 preprocessor 的 public contract 抽成独立的 context/artifact 接口层，engine 只作为一个调用方，而不是被硬编码进 preprocessor API。 |
| 具体步骤 | 1. 新建轻量头文件，例如 `Preprocessor/AngelscriptPreprocessorContracts.h`，只定义 `FAngelscriptPreprocessContext`、`FScriptSourceKey/ModuleIdentity`、`FPreprocessedModuleArtifact` 和必要的 diagnostics sink/interface；禁止该头再包含 `AngelscriptEngine.h`。2. 将 `AngelscriptPreprocessor.h` 改为只依赖 contracts 头和最少量前置声明；`GetModulesToCompile()` 之类接口改为返回 artifact/contracts 层类型，而不是直接暴露 `FAngelscriptModuleDesc`。3. 把当前构造函数里对 `FAngelscriptEngine::*CurrentContext()` 与 `UAngelscriptSettings` 默认对象的读取前移到 `AngelscriptEngine.cpp`，由 engine 先构造 `FAngelscriptPreprocessContext` 再注入 preprocessor；实现上可直接复用 `Issue-2` 规划中的 context snapshot。4. 为 diagnostics 和 hook 提供窄接口，例如 `IPreprocessDiagnosticsSink`、只读 trace observer；不要再让 public header 暴露 `FAngelscriptPreprocessor&` 和可变内部容器。5. 调整测试与工具：preprocessor 单测优先直接实例化 contracts + fake diagnostics sink，不再依赖完整 `FAngelscriptEngine`；engine 集成测试只验证“context 构造正确并把数据传给 preprocessor”。6. 在迁移期保留兼容适配器，例如 `FAngelscriptPreprocessor::CreateFromCurrentEngine()`，但明确标记为 engine-only bridge，避免新的调用点继续直接依赖 ambient state。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessorContracts.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | L |
| 风险 | public contract 一旦拆层，会影响现有 hook、测试和少量直接 include `AngelscriptPreprocessor.h` 的调用点；需要先提供兼容适配器，避免一次性打断现有工作流。 |
| 前置依赖 | 建议以 `Issue-2` 的 context snapshot 和 `Issue-71` 的 artifact/runtime state 分层为前置，否则 contracts 很容易再次把 engine 类型带回来。 |
| 验证方式 | 1. 新增不依赖 `FAngelscriptEngine::Get()` 的 preprocessor 单测，直接用 fake context 跑 import/path 解析。2. 编译级回归确认 `AngelscriptPreprocessor.h` 不再直接包含 `AngelscriptEngine.h` / `AngelscriptSettings.h`。3. engine 集成测试验证 live compile/hot reload 仍能构造正确 context 并得到与迁移前等价的预处理输出。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-72 | Architecture | 在实施 context/artifact 分层时同步拆出独立 contracts，避免新设计继续被 engine 头耦合回去 |

---

## 发现与方案 (2026-04-09 00:45)

### Issue-73：路径段里的字面 `.` 与模块层级分隔符复用同一编码，路径到模块身份在修掉 `.as` 替换后仍然不是单射

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 86-99；1958-1963，3029-3049，3133-3135 |
| 问题 | 已验证事实：`FindScriptFiles()` 在 1958-1963 行把磁盘上的相对路径原样写入 `RelativePath`，没有限制目录名或文件基名里出现字面 `.`；`AddFile()` 在 98-99 行又直接调用 `FilenameToModuleName(RelativeFilename)` 生成模块名，而 `FilenameToModuleName()` 在 86-88 行只做两件事：删除 `.as`，再把 `/` 替换成 `.`。`GetModuleByFilename()` 在 3047-3049 行重复了同一套转换；编译阶段 3133-3135 行对重复 `ModuleName` 只有一条 `ensureMsgf`，没有 fail-fast。结果是 `Gameplay.UI/Inventory.as`、`Gameplay/UI/Inventory.as`、`Gameplay/UI.Inventory.as` 这三条合法相对路径都会稳定折叠成同一个 `Gameplay.UI.Inventory`。这说明即使未来修掉 `Issue-15` 中“全局删除 `.as`”的问题，只要继续把真实路径段和模块层级都编码成 `.`，模块身份仍然不是单射。 |
| 根因 | 当前 path-to-module 规则把“物理路径分段”和“逻辑模块命名空间”压成同一个裸 `FString`，既没有对原始路径段做 escaping，也没有在进入现有 dotted-module 体系前拒绝包含 `.` 的路径段。 |
| 影响 | 物理上不同的脚本可以在预处理入口就获得同一个 `ModuleName`，随后把 `import` provider 选择、`CompilingModulesByName`、`ActiveModules`、`GetModuleByFilename()` 和 precompiled cache 一起拖进同一个身份冲突域。相比 `Issue-15` 的 `.as` 子串误删，这个问题更基础，因为它来自现有模块编码本身；如果不先收口，后续 canonical path、descriptor cache 和 include/module identity 设计仍会建立在一个非单射 key 之上。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 在现有 dotted-module 体系保留期间，先对脚本相对路径做 fail-fast 校验，禁止任何会与模块层级分隔符冲突的字面 `.` 路径段进入模块系统；长期再与 `Issue-54` 的结构化 `ModuleIdentity` 合并。 |
| 具体步骤 | 1. 在 `Preprocessor/AngelscriptPreprocessor.cpp` 与 `Core/AngelscriptEngine.cpp` 提取共享 helper，例如 `TryBuildModuleNameFromRelativePath(const FString& RelativePath, FString& OutModuleName, FString& OutValidationError)`；helper 统一做 slash-normalization、仅移除末尾扩展名、按 `/` 分段，并显式拒绝任何包含字面 `.`、空段或 `.`/`..` 特殊段的路径成分。2. `AddFile()` 不再直接调用 `FilenameToModuleName()`；若 helper 返回失败，则对该文件发出明确诊断，指出冲突段名和原始相对路径，并把该 source unit 标成预处理失败而不是继续生成模糊的重复模块。3. `GetModuleByFilename()` 与任何 filename-to-module fallback 一律复用同一 helper，避免 live lookup 继续接受一套比预处理更宽松的非法路径编码。4. 将 `CompileModules()` 中 3133-3135 行的 duplicate-module `ensureMsgf` 升级为正式 compile/preprocess 错误，诊断里同时列出冲突的两个 `RelativeFilename`，防止非法路径在非 debug 构建下继续被“后写覆盖前写”吞掉。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 与 `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` 新增回归：`Gameplay.UI/Inventory.as` 与 `Gameplay/UI/Inventory.as` 应被明确拒绝，`GetModuleByFilename()` 对这类非法路径也不得再静默映射到现有模块。6. 迁移说明同步写入相关 guide：若已有外部脚本仓库使用 dotted directory/file segment，先批量重命名为 `_` 或其它无歧义字符；只有等 `Issue-54` 的 segmented identity 落地后，才考虑重新开放更宽松的命名。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 预估工作量 | M |
| 风险 | 若仓库外已有脚本目录依赖 dotted path segment，这个修复会把过去的静默冲突变成显式编译错误；需要先给出迁移诊断，并在文档里说明短期命名约束。 |
| 前置依赖 | 无；但建议实现时与 `Issue-54` 的 `ModuleIdentity` 设计对齐，避免短期 helper 的分段规则日后再次分叉。 |
| 验证方式 | 1. 新增用例覆盖 `Gameplay.UI/Inventory.as`、`Gameplay/UI.Inventory.as` 与 `Gameplay/UI/Inventory.as` 的冲突组合，确认预处理阶段直接失败并输出明确路径诊断。2. 对正常无点号路径回归，确认现有 `Tests.Preprocessor.BasicModule` 这类模块名保持不变。3. 对非法 dotted path 调用 `GetModuleByFilename()`，确认不会再静默命中某个现有模块。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-73 | Defect | 在继续扩展 module identity 和 filename lookup 前先收紧非法路径编码，避免更多缓存/热重载逻辑建立在冲突 key 上 |

---

## 发现与方案 (2026-04-09 00:52)

### Issue-74：`#ifdef/#ifndef` 不校验 body 语法，额外 token 会被静默并入 flag 名导致错误分支而不是 syntax error

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3259-3275，4298-4310 |
| 问题 | 已验证事实：`ParseIntoChunks()` 处理 `#ifdef/#ifndef` 时，先用 `ReadIdentifier(File, ChunkEnd + 7/8)` 读取条件文本，再直接执行 `PreprocessorFlags.Find(Identifier) != nullptr` 或 `== nullptr`。而 `ReadIdentifier()` 的终止条件只有 `'\n'`、`'/'` 和 `'{'`，不会在空格或 tab 处停止。这意味着 `#ifdef EDITOR EXTRA`、`#ifndef TEST && SHIPPING`、`#ifdef    ` 这类非法写法，不会得到“directive 语法错误”的明确诊断，而是把 `EDITOR EXTRA`、`TEST && SHIPPING` 或空字符串当成完整 flag 名参与查表。结果不是 fail-fast，而是静默落到错误分支。 |
| 根因 | `#ifdef/#ifndef` 没有独立的“单个 symbol + 可选注释”的语法解析器，错误地复用了语义上更接近“读整段条件尾巴”的 `ReadIdentifier()`；同时这两条分支也没有像 `#if` 那样经过统一的条件语法校验入口。 |
| 影响 | 一个本应在预处理阶段立即暴露的 directive 拼写/粘贴错误，会被降级成普通 false/true 分支选择，进而静默污染 `ProcessedCode`、`EditorOnlyBlockLines`、`UsageRestrictions` 以及后续 import/module 解析结果。相比 `#if` 会报 `Invalid preprocessor condition`，`#ifdef/#ifndef` 当前行为更隐蔽，也更难定位。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `#ifdef/#ifndef` 引入严格的单 symbol parser，明确拒绝空 body 和 trailing token。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取共享 helper，例如 `ParseDefinedDirectiveIdentifier(const FFile&, int32 BodyPos, FString& OutIdentifier, bool& bOutHasTrailingTokens)`，只允许读取一个合法 flag token，之后仅接受空白或注释。2. 将 3259-3275 行的 `#ifdef/#ifndef` 分支改为先调用该 helper；若 body 为空或存在 trailing token，立即 `LineError(File, LineNumber, TEXT("Invalid #ifdef/#ifndef syntax."))` 并设置 `bHasError`，而不是继续查表。3. 让该 helper 与 `Issue-35` 的 directive matcher、`Issue-69` 的 simple flag condition parser 共享 token 边界规则，避免 `#if`、`#ifdef`、`#ifndef` 再出现三套不同的 body 解释语义。4. 若后续需要支持更复杂的存在性判断，不要继续放宽 `#ifdef/#ifndef`，而应显式引入 `defined(X)` 或等价语法，并统一走结构化条件解析。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 新增回归：`#ifdef EDITOR EXTRA`、`#ifndef TEST && SHIPPING`、`#ifdef    ` 都应在预处理阶段报 syntax error；正常的 `#ifdef EDITOR` 与带行尾注释的 `#ifdef EDITOR // comment` 仍应保持现有语义。6. 补一条对照测试，验证相同 malformed 文本不再默默改变 `EditorOnlyBlockLines` 或 `UsageRestrictions`，确保修复确实把问题前移到 parser 层。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果仓库外已有脚本依赖这类宽松但错误的写法，修复后会从“静默走错分支”变成显式报错；需要在错误消息里指出多余 token，帮助快速迁移。 |
| 前置依赖 | 建议与 `Issue-35`、`Issue-69` 同步收口 directive tokenization，避免新增第四套局部解析逻辑。 |
| 验证方式 | 1. malformed `#ifdef/#ifndef` 在预处理阶段直接报 syntax error，而不是改变分支结果。2. 合法 `#ifdef/#ifndef` 与带行尾注释版本仍保持原有裁剪行为。3. 对比修复前后同一 malformed fixture 的 `ProcessedCode`，确认不再出现静默走错分支。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-74 | Defect | 与 `Issue-35`、`Issue-69` 同批收口 directive body parser，先把 silent misparse 变成明确语法错误 |

---

## 发现与方案 (2026-04-09 00:59)

### Issue-75：`namespace` 头部只按“读到 `{` 前的整段文本”入栈，额外 token 会污染后续 chunk 与 generated code

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3159-3162，3758-3765，4298-4310，768，874-895，1169-1173 |
| 问题 | 已验证事实：`ParseIntoChunks()` 在识别 `namespace` 后，直接执行 `NamespaceStack.Add(ReadIdentifier(File, ChunkEnd + 10))`；而 `ReadIdentifier()` 会一直读到 `'{'`、`'/'` 或换行才停止，不会在空格处停止。因此像 `namespace Foo Bar {}` 这类 brace 存在但 header 本身非法的写法，会把 `Foo Bar` 整体压入 `NamespaceStack`。随后 `SubmitChunk()` 会把这个字符串写入 `Chunk.Namespace`，`DetectClasses()` 又继续把它传播到 `ClassDesc->Namespace`，后续 generated code 还会直接格式化成 `namespace Foo Bar { ... }` 或 `namespace Foo Bar::MyClass { ... }`。这说明当前问题不是 `Issue-42` 已覆盖的“缺少 `{`”，而是“即使有 `{`，namespace body 也没有校验只有一个合法名称 token”。 |
| 根因 | `namespace` 解析仍然把 header 当成一段未结构化字符串读取，没有独立的 token parser 去验证命名空间名的形状和尾随内容；`ReadIdentifier()` 的语义也比其名字更宽，天然会把额外 token 一并吞进来。 |
| 影响 | 非法 namespace header 不会在预处理阶段 fail-fast，而是把错误名字静默扩散到 chunk、class 描述符和 generated wrapper 代码里。最终用户看到的往往是后续生成代码或编译器的次级语法错误，而不是原始 namespace header 的根因，排障成本明显升高。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `namespace` 引入严格的 header parser，只接受单个合法 namespace token，并在 parser 层拒绝 trailing token。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取 `ParseNamespaceHeader()`，不要再直接复用 `ReadIdentifier()`；该 helper 必须依次解析 `namespace` 关键字、一个合法 namespace 名、可选空白/注释，以及必需的 `{`。2. 将 3765 行的 `NamespaceStack.Add(...)` 延后到 `ParseNamespaceHeader()` 完全成功之后，保证 malformed header 不会先污染全局解析状态。3. 对 namespace 名本身建立显式校验：短期只接受一个合法 identifier；如果未来需要支持 `Foo::Bar` 这类嵌套写法，应通过结构化 token 序列显式实现，而不是继续接受整段裸字符串。4. 对 `namespace Foo Bar {}`、`namespace Foo<T> {}`、`namespace Foo + Bar {}` 这类命中关键字但 header 非法的输入，直接在当前行报明确错误，例如 `Invalid namespace declaration; expected a single namespace identifier before '{'.`。5. 将 `Issue-42` 的缺失 `{` 校验与本条 token 形状校验合并到同一个 helper，避免 namespace 再出现“缺 brace 一套规则、extra token 另一套规则”的分叉。6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 新增回归：`namespace Foo Bar {}` 应在预处理阶段报错，且 `Chunk.Namespace` / `GeneratedCode` 不得残留 `Foo Bar`；保留 `namespace Foo {}` 的正向样例确保合法路径不受影响。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | S |
| 风险 | 若仓库外已有脚本利用当前宽松行为把非标准文本塞进 namespace header，修复后会改成显式报错；但这些脚本当前本就依赖不稳定的后续编译错误，前移诊断是净收益。 |
| 前置依赖 | 建议与 `Issue-42` 同批实现，统一 namespace header 的 brace 校验与 token 校验。 |
| 验证方式 | 1. `namespace Foo Bar {}` 在预处理阶段直接报 syntax error，不再污染 `Chunk.Namespace`。2. 生成代码中不再出现 `namespace Foo Bar {` 这类被错误拼出的 header。3. 正常 `namespace Foo {}` 与现有 namespace 正向用例保持通过。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-75 | Defect | 与 `Issue-42` 同批收口 namespace header parser，先把 header 级语法错误前移到预处理阶段 |

---

## 发现与方案 (2026-04-09 01:15)

### Issue-76：`namespace` 关键字入口绕过了统一词法边界检查，前缀标识符会被误判成真正的命名空间声明

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3076-3089，3413-3420，3758-3765 |
| 问题 | 已验证事实：`ParseIntoChunks()` 已经有 `IsStartOfIdentifier()`，且 `class` 入口会在 3413-3420 行显式调用它；但 `namespace` 入口在 3758-3765 行只检查 `Strncmp("namespace")`、后继空白、scope 与 comment/string 状态，完全没有调用词法边界 helper。结果是只要顶层文本里出现 `mynamespace Foo {}`、`foo_namespace Bar {}` 这类“前缀仍属于同一标识符、但 `namespace` 后面碰巧跟空白”的片段，预处理器就会直接 `NamespaceStack.Add(...)`，把它当成真正的命名空间声明处理。 |
| 根因 | `namespace` 分支没有复用现有的 keyword-boundary 合同，而是单独走了一条裸 `Strncmp` 路径；这让 `namespace` 的词法边界规则与 `class/struct/interface/import` 等入口发生分叉。 |
| 影响 | 一个本应作为普通标识符报语法错误的前缀文本，会被预处理器提前改写成命名空间状态，后续 `Chunk.Namespace`、`ClassDesc->Namespace` 与 generated code 都可能被错误污染。即使 `Issue-28` 修掉了 `IsStartOfIdentifier()` 的 `0-1` typo，这条分支仍会继续对字母和下划线前缀误判。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为所有关键字入口收口到统一的 `IsKeywordStart` 词法边界 helper，先修 `namespace`，再顺手审计同类裸前缀分支。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取共享 helper，例如 `IsIdentifierContinuation(TCHAR)` 与 `CanStartKeywordAtCurrentPos(...)`，统一表达“前一个字符不能是字母/数字/下划线”的规则，避免每个关键字分支各写一套边界判断。2. 将 3758-3765 行的 `namespace` 分支改为显式调用该 helper，再继续做空白、scope、comment/string 校验；不要再让 `namespace` 走裸 `Strncmp`。3. 借这次修改顺手审计同一 `case 'n'/'f'` 分支里的 `n\"` name literal、`f\"` format string 起点，至少明确它们是否也需要前缀边界；若语法要求它们只能作为独立 token，也应统一走同一 helper。4. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 新增回归：`mynamespace Foo {}`、`foo_namespace Bar {}` 这类前缀文本不得再污染 `NamespaceStack`/`Chunk.Namespace`；同时保留合法 `namespace Foo {}` 的正向样例，确保修复不影响真实命名空间声明。5. 将 `Issue-28` 的 `IsStartOfIdentifier()` 修复与本条合并验收，确保关键字入口不再一部分依赖 helper、一部分绕过 helper，避免下一次再出现“某个分支根本没走共享边界规则”的回归。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | S |
| 风险 | 若外部脚本曾依赖这类宽松误判触发 namespace 语义，修复后会从“静默进入错误 namespace”变成更早的语法错误；但这种旧行为本身不稳定，前移失败是净收益。 |
| 前置依赖 | 建议与 `Issue-28`、`Issue-42`、`Issue-75` 一起实现，统一 namespace 相关的词法边界、header 形状和 brace 校验。 |
| 验证方式 | 1. `mynamespace Foo {}` 不再向 `Chunk.Namespace` / `ClassDesc->Namespace` 写入 `Foo`。2. 合法 `namespace Foo {}` 仍按原语义通过。3. 回归 `class/import/namespace` 关键字边界测试，确认共享 helper 没有引入新的误报或漏报。 |

### Issue-77：hot reload 仍把 `RelativeFilename -> Module` 建成单值索引，future `#include` / shared section 无法把一个源码文件映射到多个 owner module

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 2298-2309，2321-2333，2388-2396 |
| 问题 | 已验证事实：hot reload 依赖分析在 2298-2309 行构建的是 `TMap<FString, FAngelscriptModuleDesc*> RelativeFileToModule`，并把每个 `Section.RelativeFilename` 直接 `Add` 成单个 module 指针；后面无论是 automatic-import 路径 2321-2333 行，还是 explicit-import 路径 2388-2396 行，收到文件变化事件时都只做一次 `RelativeFileToModule.Find(File.RelativePath)`，最多拿到一个 owner module。也就是说，当前引擎账本把“一个源码文件只属于一个 module”写死在 hot reload 入口了。 |
| 根因 | 运行时文件索引仍沿用单值映射模型，没有把源码 provenance 建模成“一个 source key 可以被多个 module section 共享”的多 owner 关系；这与 future include graph / expanded section 的目标模型不兼容。 |
| 影响 | 一旦 `Issue-5` 的 include graph 落地，或未来允许多个 module 共享同一个展开后的 source section，修改某个 include 文件时 hot reload 只能选中一个 owner module，其余 owner 会继续保留陈旧代码和陈旧依赖签名。换句话说，当前实现不仅还没支持 `#include`，而且已经在 engine 侧预埋了“shared source 只能 reload 一个 owner”的架构阻塞点。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 hot reload 的文件索引从单值 map 升级为 canonical source key 到 owner 集合的多值索引，为 include graph / multi-section module 提前铺平依赖传播路径。 |
| 具体步骤 | 1. 在 `AngelscriptEngine.cpp/.h` 引入多 owner 索引，例如 `TMap<FScriptSourceKey, TArray<FAngelscriptModuleDesc*>> SourceKeyToOwningModules`；`FScriptSourceKey` 必须复用 `Issue-38` / `Issue-54` 规划中的 root-aware canonical key，而不是继续只用裸 `RelativeFilename`。2. 构建索引时，遍历每个 `Module->Code` section，把所有 owner 都收进数组/集合，不再丢成单个指针；必要时对 owner 列表做去重，避免多 section 重复挂同一 module。3. automatic-import 路径收到文件变化后，不再只标记一个 module，而是把该 source key 的全部 owner modules 都推入 `MarkedModules`；explicit-import 路径也应把全部 owner 推入 `ModuleJobs`，保证 reverse-dependency 扩散从完整 owner 集合出发。4. 若 source key 当前没有 owner，才把它视为“新增独立脚本文件”直接加入 `FilesToHotReload`；若有多个 owner，则要把每个 owner 的所有 section 一并入队，避免 shared include 只 reload 一半。5. 为未来 include graph 预留单独的 `IncludedFileOwners` 或直接让 `Module->Code` provenance 带 `SectionKind`，这样 include 文件、主脚本文件和 generated overlay 都能复用同一套 owner 索引。6. 在 `Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp` 或 `Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp` 新增 synthetic shared-section fixture：手工构造两个 module 都引用同一 `Section.RelativeFilename`，断言文件变化事件会同时把两个 owner 纳入 reload，而不是只命中一个。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptTest/HotReload/AngelscriptHotReloadFunctionTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningHotReloadDecisionTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | multi-owner 传播会扩大某些文件变化触发的 reload 集合，短期内可能暴露此前被单值 map 掩盖的 stale-state；需要把 owner 索引的 canonical key 和 duplicate-module 诊断一起收口，避免把路径冲突误解释成 shared include。 |
| 前置依赖 | 建议复用 `Issue-38` 的复合文件身份和 `Issue-54` 的 `ModuleIdentity`，否则多 owner 索引会再次建立在脆弱的裸相对路径上。 |
| 验证方式 | 1. synthetic shared-section 测试中，同一 source key 的两个 owner 模块都会被加入 reload 集合。2. 普通单 owner 脚本回归，确认 reload 结果不变。3. include graph 落地后，修改 shared include 文件时所有 owner 模块都会重编译，且不会再出现“只更新最后一个 owner”的 stale-state。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-77 | Architecture | 在实现 `#include` / multi-section module 之前先把 hot reload 文件索引改成多 owner 模型 |
| P2 | Issue-76 | Defect | 与 `Issue-28`、`Issue-42`、`Issue-75` 同批收口 namespace 词法边界，避免分支继续绕过共享 helper |

---

## 发现与方案 (2026-04-09 01:28)

### Issue-78：`delegate/event` 在 `)` 与 `;` 之间插入注释后会被静默漏掉，既不生成 wrapper 也不记录模块元数据

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3675-3715，4170-4190，565-624 |
| 问题 | 已验证事实：`ParseIntoChunks()` 识别 `event/delegate` 后，会先用 `FindScopeCloseBracket()` 找到参数列表右括号，再调用 `FindSemicolonDirectlyAfter()` 判断声明是否以 `;` 结束。后者只接受 `'\t'/' '/'\n'/'\r'` 作为 `)` 与 `;` 之间的合法间隔，遇到 `/* ... */` 这类内联注释就直接返回 `-1`。结果是 `delegate void Foo() /* note */;`、`event void Bar() /* note */;` 不会向 `File.Delegates` 追加 `FDelegateDesc`。而后续 `ProcessDelegates()` 只遍历 `File.Delegates` 生成 wrapper 并写入 `File.Module->Delegates`，所以这一类合法声明会被静默当成普通源码漏过。 |
| 根因 | `delegate/event` 终止符判断复用了只认空白、不认注释的局部 helper，且没有与 `import`/future `#include` 共用 comment-aware statement parser。 |
| 影响 | 带注释的委托声明会丢失 generated wrapper、`Module->Delegates` 元数据和后续绑定语义，最终表现成“同一语法只因插入注释就行为变化”的不稳定预处理结果。这个边界一旦落进真实脚本，排查时看到的通常是后续编译或运行时症状，而不是原始委托声明没有被预处理识别。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 `delegate/event` 声明结束位置改成 comment-aware statement parser，不再依赖“`)` 后只能直接跟空白和 `;`”的脆弱假设。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取共享 helper，例如 `ConsumeTerminatorAfterBalancedRegion(...)` 或并入 `Issue-3` 的 `FPreprocessorCursor`，要求它显式跳过 line/block comment、字符串和合法空白后再找 `;`。2. 将 3696-3705 行的 `event/delegate` 识别改为调用该 helper，返回准确的 statement 终点；不要再继续使用 `FindSemicolonDirectlyAfter()` 的裸字符扫描。3. 若 comment-aware parser 发现 `)` 后确实不存在终止 `;`，应在当前行给出明确 syntax error，而不是简单放弃记录 `FDelegateDesc`。4. 将 `FindSemicolonDirectlyAfter()` 标记为仅供旧路径过渡使用，或直接删除并让 `import`、future `#include`、delegate 统一复用同一套 statement span 解析，避免同类 bug 再次分叉。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 新增回归：`delegate void Foo() /* note */;`、`event void Bar() /* note */;` 应仍生成 `Module->Delegates` 与 wrapper；再补一条真正缺失 `;` 的负向样例，断言会得到明确预处理错误。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | S |
| 风险 | 如果只局部修 `delegate/event`，而不把 comment-aware terminator 抽成共享 helper，后续 `import/#include` 仍会继续维护另一套结束规则；建议至少把 statement span parser 先提取出来。 |
| 前置依赖 | 无；但建议实现时与 `Issue-3` 的 cursor/helper 拆分对齐。 |
| 验证方式 | 1. 带 block comment 的 `delegate/event` 声明仍会产生 `Module->Delegates` 和 generated wrapper。2. 真正缺少 `;` 的声明在预处理阶段直接报错，而不是静默漏掉。3. 回归现有无注释委托样例，确认正向语义不变。 |

### Issue-79：预处理自动化测试没有把选定 engine 放进 context stack，`automatic import` 与上下文 flag 的覆盖依赖环境偶然状态

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 行号 | 702-709；635-643；76-90，171-196；118-121 |
| 问题 | 已验证事实：`FAngelscriptPreprocessor` 的行为依赖 `ShouldUseAutomaticImportMethodForCurrentContext()`，而这个函数在拿不到 `TryGetCurrentEngine()` 时会直接返回 `false`。仓库已经提供 `FAngelscriptEngineScope` 作为显式 context stack owner，但当前 `Preprocessor` 测试只调用 `GetEngineForPreprocessorTests()` 取一个 `Engine*` 后就直接构造 `FAngelscriptPreprocessor`，没有建立任何 `FAngelscriptEngineScope`。learning trace 甚至只在 `TryGetCurrentEngine()` 非空时才临时关闭 `bUseAutomaticImportMethod`，否则就让测试静默落回“无 current engine -> automatic import = false”的兜底路径。也就是说，这些测试并没有稳定保证“预处理器正在使用它们刚取到的那个 engine 上下文”。 |
| 根因 | 测试夹具把“拿到一个 `Engine*`”误当成“预处理器会使用这个 engine”，却没有复用 runtime 已定义好的 context stack 机制。 |
| 影响 | 预处理测试和 learning trace 对 `automatic import`、`EDITOR/RELEASE` 等 context-sensitive 语义的覆盖会随运行环境漂移：有 ambient engine 时走一套，无 ambient engine 时又走另一套。结果是未来修 `import resolver`、`context hash`、`module cache` 时，测试可能在错误上下文下依然为绿，削弱 Discovery/Plan 的验证价值。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 preprocessor 测试全部切到显式 engine scope，禁止继续依赖 ambient engine 或“拿不到 engine 就回退 false”的隐式路径。 |
| 具体步骤 | 1. 在 `Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` 或等价 test helper 中新增轻量封装，例如 `FScopedPreprocessorTestEngineContext`，内部直接持有 `FAngelscriptEngineScope`。2. 修改 `FAngelscriptPreprocessorBasicParseTest`、`FAngelscriptPreprocessorMacroDetectionTest`、`FAngelscriptPreprocessorImportTest` 和 `FAngelscriptLearningPreprocessorTraceTest`：在构造 `FAngelscriptPreprocessor` 之前先对 `GetEngineForPreprocessorTests()` / shared clone engine 建立 scope，确保 `TryGetCurrentEngine()` 命中的是测试指定 engine。3. 删除 learning trace 里 `if (CurrentEngine)` 这种可选 guard；manual-import 场景必须在显式 scope 下稳定把 `bUseAutomaticImportMethod` 改为 `false`，而不是接受“没有 current engine 时默认也是 false”的偶然等价。4. 为 test helper 新增两条自检：一条断言 scope 内 `TryGetCurrentEngine()` 恰好等于测试 engine；另一条断言 scope 外 `ShouldUseAutomaticImportMethodForCurrentContext()` 不得再被普通 preprocessor 测试直接依赖。5. 在 `Preprocessor` 自动化里补一组成对用例：同一 fixture 在 scope 内默认应走 production-like `automatic import` 语义，显式 guard 后才走 manual-import 路径，避免未来回归再次把两种模式混淆。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | S |
| 风险 | 现有测试若一直误跑在 fallback context，下修后可能会暴露之前被掩盖的真实行为差异；这是期望中的校正，不应再被视为 flaky。 |
| 前置依赖 | 无；但长远应与 `Issue-2` 的 `FAngelscriptPreprocessContext` 注入方案合并，最终让测试直接传显式 context。 |
| 验证方式 | 1. 在测试中显式断言 `TryGetCurrentEngine()` 命中预期的 clone/production engine。2. 同一 fixture 分别在默认和手动关闭 `automatic import` 的 scope 下运行，结果应稳定且可区分。3. 无论本地是否存在 ambient engine，这组测试都应得到相同结果。 |

### Issue-80：当前 preprocessor 测试与 learning trace 把 lossy dotted `ModuleName` 当成唯一身份，正在固化与 `ModuleIdentity` 路线冲突的旧合同

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 行号 | 20-27；36-48，104-105，210-218；140-143，156，186-193，215 |
| 问题 | 已验证事实：`FAngelscriptPreprocessor` 仍把 `FilenameToModuleName()` 作为 public helper 暴露；`FAngelscriptPreprocessorBasicParseTest` 和 learning trace 都直接断言相对路径必须规范化成固定 dotted `ModuleName`；两处测试 helper 也都用 `FindModuleByName(..., TEXT(\"Tests....\"))` 作为唯一定位方式。推断：这会把当前 lossy 的 path-to-name 编码继续写成测试合同，使未来 `Issue-54` 规划中的 `ModuleIdentity`、root-aware provenance 和 multi-section module 聚合即使实现正确，也会先被这些“字符串必须长这样”的断言拦住。 |
| 根因 | 测试与 trace 把 `ModuleName` 的显示字符串当成模块唯一身份在验证，而没有围绕 `RelativeFilename`、section provenance 或未来结构化 identity 建立更稳定的断言层。 |
| 影响 | `Preprocessor` 相关验证会继续鼓励业务代码保留 `.Replace(\".as\")`、`/ -> .` 这一套历史字符串合同，而不是迁移到更准确的 `ModuleIdentity`。这会增加后续 import/include/module-system 重构的测试噪音，拖慢 `Issue-54`、`Issue-70`、`Issue-77` 这类架构项落地。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 将测试主断言从“固定 dotted `ModuleName` 字符串”迁移到“source provenance / structured identity”，只保留一条兼容性测试覆盖旧显示名。 |
| 具体步骤 | 1. 在 test helper 层新增按 provenance 查找模块的入口，例如优先按 `Code[0].RelativeFilename`、未来 `ModuleIdentity.CanonicalRelativePath` 或等价 source key 定位模块，逐步替代 `FindModuleByName(...)` 的字符串匹配。2. 将 `FAngelscriptPreprocessorBasicParseTest` 和 learning trace 的主断言改成“模块包含预期 source section、该 section 的 `RelativeFilename` 正确、`ImportedModules`/`Code` 行为正确”；把 `ModuleName == \"Tests.Preprocessor.BasicModule\"` 降为单独的兼容性断言，而不是所有测试共同依赖的主身份。3. 在 learning trace 中把 `FilenameToModuleName` 记录为 `DisplayModuleName` 或 compatibility note，同时新增 provenance/identity 字段输出，避免 trace 文本继续暗示 dotted name 是唯一模块身份。4. 与 `Issue-54` 联动：一旦 `FAngelscriptModuleIdentity` 落地，测试 helper 立即切换到 identity 比较，并把旧 `FindModuleByName` 标记为仅供 legacy compatibility 使用。5. 在 `AngelscriptPreprocessor.h` 层为 `FilenameToModuleName()` 增加注释或 deprecation note，明确它是 legacy display/helper，而不是未来 cache/import/include 的唯一主键。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Shared/AngelscriptTestUtilities.h` |
| 预估工作量 | S |
| 风险 | 迁移测试断言时，如果一次性删除所有 `ModuleName` 检查，可能会丢掉对 legacy 显示名回归的可见性；因此应保留一条专门的 compatibility test，而不是完全移除。 |
| 前置依赖 | 建议与 `Issue-54` 的 `ModuleIdentity` 设计协同推进，但测试 helper 的 provenance 化可以先独立落地。 |
| 验证方式 | 1. 更新后的测试在不依赖 exact dotted name 的情况下仍能稳定定位模块。2. 保留的一条 compatibility test 仍能覆盖 `FilenameToModuleName()` 当前显示行为。3. 当后续引入 `ModuleIdentity` 或多 root provenance 时，测试应主要验证 identity/provenance，不再因显示字符串变化大面积报红。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-79 | Architecture | 先修测试上下文合同，避免后续 import/module 改动在错误上下文下被“验证通过” |
| P2 | Issue-78 | Defect | 与 `Issue-3` 的 statement parser 拆分联动修复，先补 `delegate/event` 注释边界回归 |
| P2 | Issue-80 | Architecture | 在推进 `Issue-54`/`Issue-70` 前先松开测试对 legacy dotted `ModuleName` 的绑定 |

---

## 发现与方案 (2026-04-09 01:43)

### Issue-81：`import` 语句体只按首个 `;` 截断，额外 token 会被当成模块名拖到编译阶段

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 3489-3510；3175-3195 |
| 问题 | 已验证事实：`ParseIntoChunks()` 在识别 `import` 后，只是从 `import` 后第一个字符开始线性扫到首个 `;`，把这整段原文 `TrimStartAndEnd()` 后直接写进 `FImport.ModuleName`；唯一的分类逻辑只是 `Contains("(")`，用来粗暴排除 declared function import。后续 `CompileModules()` 又把这段原样字符串拿去做 `CompilingModulesByName.Find(ImportName)` / `GetModuleByModuleName(ImportName)` 精确匹配。结果是 `import Foo Bar;`、`import Foo + Bar;`、`import "Foo.Bar";`、以及 future `Issue-4` 规划中的相对路径写法后面误带额外 token 的场景，都会被当成一个“合法但不存在的模块名”，最终只在更晚阶段报 `could not find module ... to import`。 |
| 根因 | `import` 入口当前没有语法层的“引用体”对象，只把 `;` 之前的整段 substring 当成模块身份；statement parser 与 dependency resolver 因而被迫共享一份未经验证的脏字符串。 |
| 影响 | 这会把本应在预处理阶段 fail-fast 的语法错误降级成 module lookup 错误，直接误导定位；同时也会让 malformed import 文本进入未来的 canonical resolver、dependency signature 和 cache 设计，继续扩大 module/import 合同的歧义面。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `import` 引入独立的 statement/body parser，只接受一个明确、可规范化的引用体，其余 token 一律在预处理阶段报语法错。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 新增 `ParseImportStatement()` 与 `ParseImportReference()`，不要再直接把 `import` 到 `;` 的整段 substring 当成模块名。2. 让 parser 明确区分三类结果：`ModuleImport`、`DeclaredFunctionImport`、`InvalidImportSyntax`；module import 只接受一个可规范化的引用体，允许的具体形状与 `Issue-4` 的 canonical resolver 统一。3. 对 module import body 做 trailing-token 校验：解析完唯一引用体后，只允许出现空白或注释；若还残留 identifier、运算符、引号或其它 token，立即在 `ImportDesc.FileLineNumber` 处 `LineError(...)`，并阻止该语句进入 `File.Imports`。4. 将 `FImport` 扩展为保存 `RawImportText`、`ParsedImportKind` 与 canonical key，后续 resolver / diagnostics 只消费校验后的字段，不再回看裸 `ModuleName` 字符串。5. 对已经明确判定为 `InvalidImportSyntax` 的语句，采用“记录错误后 blank 原语句 span”或等价抑制策略，避免同一条坏 import 再以第二次编译错误污染日志。6. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 新增回归：`import Foo Bar;`、`import Foo + Bar;`、`import \"Foo.Bar\";`、`import ./Shared.as extra;` 都应在预处理阶段直接失败；保留 `import Tests.Preprocessor.Shared;` 与 future relative-path 正向样例，确认合法路径不受影响。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | M |
| 风险 | 若团队已有脚本误依赖“写错 import 但在后段 lookup 才报错”的旧行为，修复后错误会前移到预处理阶段；但这是期望中的语法收口，应通过更明确的错误文案降低迁移成本。 |
| 前置依赖 | 建议与 `Issue-3` 的 statement parser 抽取和 `Issue-4` 的 canonical resolver 同步设计，避免 import body grammar 与路径规范化再分叉。 |
| 验证方式 | 1. malformed import 不再进入 `ImportedModules`，且错误直接落在 import 行。2. 合法 dotted import 与 canonical relative import 仍能正常解析。3. 同一坏 import 不再同时产生“语法错 + 找不到模块”两类重复诊断。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-81 | Defect | 在继续扩展 relative import / canonical resolver 之前先收口 import body grammar |

---

## 发现与方案 (2026-04-09 01:45)

### Issue-82：fully precompiled 恢复按 `TMap` 哈希顺序吐模块，live preprocess 与 cache 路径的模块副作用顺序会分叉

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | 570；2651-2659，2752-2928；3101-3136，4147-4153；85-95，5775-5804 |
| 问题 | 已验证事实：precompiled cache 把模块保存在 `TMap<FString, FAngelscriptPrecompiledModule> Modules` 中；保存阶段直接 `Modules.FindOrAdd(ModuleName).InitFrom(...)`，恢复阶段则 `for (auto Elem : Modules)` 迭代整个 map 生成 `ModuleDescs`。`CompileModules()` 随后又原样 `CompilationQueue.Append(InModules)`，并把该顺序继续写进 `CompiledModules`；`FAngelscriptClassGenerator::AddModule()` 按输入顺序把模块塞进 `Modules` 数组，`CallPostInitFunctions()` 又按这个数组顺序执行每个模块的 `PostInitFunctions`。同一条顺序链还被 `DiscoverUnitTests()`/`DiscoverIntegrationTests()` 直接消费。也就是说，fully precompiled 路径当前把“模块顺序”托付给 `TMap` 迭代，而 live preprocessor 路径用的却是 discovery/explicit-import 排序后的顺序。 |
| 根因 | precompiled schema 只缓存了“有哪些模块”，没有缓存“这些模块在 live preprocess 后的稳定顺序”；恢复路径因此退化成容器内部迭代顺序，而后续编译、class generator 和 post-init 又把这份顺序继续当成有意义的输入。 |
| 影响 | 即使不考虑 `Issue-26` 已覆盖的 explicit-import 拓扑，automatic-import 或无-import 模块在 live 与 fully precompiled 两条路径上仍可能以不同顺序进入 compile/class generation/post-init/test discovery。结果包括：`PostInitFunctions` 执行次序漂移、测试发现顺序不稳定、以及任何仍残留“先到先处理”语义的模块级副作用在 cache 路径上变成 nondeterministic 行为。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把 live preprocess 产出的稳定模块顺序显式写进 precompiled schema，恢复时先还原顺序，再做 explicit-import DAG 校验/重排。 |
| 具体步骤 | 1. 在 `FAngelscriptPrecompiledModule` 或 `FAngelscriptPrecompiledData` 中新增稳定顺序字段，例如 `ModuleLoadOrder` / `StableOrderIndex`，来源必须是 live preprocess/compile 入口已经确定的模块顺序，而不是 `TMap` 自然迭代。2. 保存 cache 时，不再只把模块塞进 `TMap`；同时记录一份有序 `TArray<FString>` 或按 index 排序的 side table，确保恢复时可以先重建与 live path 一致的输入序列。3. `GetModulesToCompile()` 恢复模块后，先按这份稳定顺序生成 `ModuleDescs`；若 cache 来自旧版本且缺少顺序元数据，要么直接判 stale 回退 live preprocess，要么至少退回 canonical sort，禁止继续使用 `for (auto Elem : Modules)` 的哈希顺序。4. 与 `Issue-26` 联动：explicit-import 模式在恢复稳定顺序之后，仍需复用统一的 DAG sorter 重新校验 provider-before-consumer；automatic-import 或无-import 模块则保持 recorded order，避免 live/cache 再分叉。5. 将 class generator / post-init / test discovery 的“模块顺序来自 compile 输入”合同写成显式注释与测试，避免后续维护者再把 `TMap` 或其它无序容器直接喂给 `CompileModules()`。6. 新增对照回归：同一组模块分别走 live preprocess 和 fully precompiled，比较 `CompiledModules` 名称顺序、`PostInitFunctions` 执行 trace，以及 test discovery 顺序，要求三者完全一致。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`，`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 预估工作量 | M |
| 风险 | cache schema 需要版本升级；若已有工具默认认为 precompiled 模块是“无序集合”，修复后会暴露它们对偶然顺序的隐式依赖，需要用 trace 帮助迁移。 |
| 前置依赖 | 建议与 `Issue-26`、`Issue-27`、`Issue-54` 一起收口 fully precompiled 的顺序/身份合同，避免旧 key 与新顺序元数据各走一套。 |
| 验证方式 | 1. 同一 fixture 走 live 与 fully precompiled，两条路径输出的模块顺序完全一致。2. 含 `PostInitFunctions` 的多模块样例在两条路径上执行顺序一致。3. cache 缺失顺序元数据时会明确回退或报 stale，不再静默使用 `TMap` 哈希顺序。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P1 | Issue-82 | Architecture | 在继续扩大 fully precompiled 覆盖面前先固定模块顺序合同，避免 live/cache 继续分叉 |

---

## 发现与方案 (2026-04-09 01:46)

### Issue-83：现有 discovery/preprocessor 自动化主要校验集合成员，不校验顺序合同，module-order 回归仍可静默进入主干

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Architecture |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 行号 | 258-273；98-105，204-218；149-193 |
| 问题 | 已验证事实：脚本发现测试在拿到 `Files` 后立即把 `RelativePath` 收进 `TSet<FString> FoundRelativePaths`，随后只断言成员存在；`FAngelscriptPreprocessorBasicParseTest` 只验证单模块名字与 `Code.Num()`，`FAngelscriptPreprocessorImportTest` 也只检查 `ImportedModules.Contains(...)` 和 import 文本被移除；learning trace 用例虽然会输出 `ImportedModules` 与 chunk 摘要，但没有任何顺序断言。这意味着当前最接近 discovery/import/module-order 的自动化合同都是“集合语义”或“存在性语义”，并没有把 live preprocess 顺序、fully precompiled 恢复顺序、或 `PostInitFunctions`/test discovery 顺序写成可回归的行为。 |
| 根因 | 测试设计长期把 preprocessor/discovery 当成“生成正确成员集合”的工具，而不是“生成稳定、有序的模块图输入”的组件；顺序语义因此停留在实现细节，没有进入自动化合同。 |
| 影响 | 即使 `Issue-39`、`Issue-82` 这类顺序相关修复已经落地，只要后续改动重新把 discovery、precompiled restore 或 compile 输入顺序打乱，现有测试仍可能全部为绿。结果是 module winner、post-init side effect、test discovery 次序和 live/cache parity 这些依赖顺序的行为，仍然会以 nondeterministic 方式回流。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 把“模块顺序”提升为一等测试合同，分别在 discovery、live preprocess、fully precompiled parity 三层建立显式顺序断言。 |
| 具体步骤 | 1. 在 `AngelscriptFileSystemTests.cpp` 新增专门的顺序 fixture，不再把结果先塞进 `TSet`；直接断言 `FindAllScriptFilenames()` 返回的 canonical relative path 序列与预期稳定顺序完全一致。2. 在 `AngelscriptPreprocessorTests.cpp` 增加多模块 fixture，既断言 `ImportedModules` 的内容，也断言 `GetModulesToCompile()` 返回的模块顺序与 explicit-import / canonical discovery 规则一致。3. 为 `Issue-82` 增加 live-vs-precompiled parity 测试：同一组脚本分别走实时预处理和 fully precompiled 恢复，比较 `CompiledModules` 顺序、`PostInitFunctions` 执行顺序，以及任何相关 trace 输出，要求完全一致。4. 在 learning trace 用例中增加结构化顺序输出，例如 `ModuleCompileOrder` / `DiscoveredFilesOrder`，并把至少一条关键顺序断言从“仅输出日志”升级为 `TestEqual`。5. 若某些测试确实只关心集合成员，应在名字上明确标注为 `Membership`/`Contains` 类断言，避免后续维护者误以为它们已经覆盖了顺序合同。6. 将这三层顺序测试绑定到 `Issue-39`、`Issue-82`、`Issue-24` 的实现验收里，作为后续 import/include/module-system 重构的必要护栏，而不是可选补充。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`，`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`，以及新增的 fully precompiled parity 测试文件 |
| 预估工作量 | S |
| 风险 | 顺序断言一旦加上，会把当前若干隐式 nondeterminism 立即暴露出来，短期可能导致测试先红；但这正是需要尽早显性化的问题，而不是继续让 nondeterminism 潜伏。 |
| 前置依赖 | 建议与 `Issue-39` 和 `Issue-82` 一起验收，这样测试用例可以直接固化修复后的稳定顺序，而不是给旧行为写快照。 |
| 验证方式 | 1. discovery 顺序 fixture 在重复运行、不同路径分隔符输入下都返回同一顺序。2. live preprocess 与 fully precompiled 恢复输出的模块顺序和 post-init trace 完全一致。3. 人为打乱顺序或回退到 `TMap` 迭代后，新测试会稳定报红。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-83 | Architecture | 作为 `Issue-39`/`Issue-82` 的验收护栏同步补齐，先把顺序合同写进 CI |

---

## 发现与方案 (2026-04-09 01:58)

### Issue-84：`n"..."` / `f"..."` 字面量入口不校验前导词法边界，标识符尾部会被静默改写成 name/format literal

**问题描述**

| 项目 | 内容 |
|------|------|
| 类型 | Defect |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Script/Example_Actor.as`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningTimerAndLatentTraceTests.cpp` |
| 行号 | 3749-3775；3899-3909；17；58 |
| 问题 | 已验证事实：`ParseIntoChunks()` 在遇到字符 `n` 或 `f` 时，只要下一个字符是 `"` 且当前不在 comment/string 内，就分别把 `NameLiteralStart` 或 `FormatStringStart` 设到当前位置；这里完全没有像 `class/import/namespace` 那样检查“前一个字符是否仍属于同一 identifier”。后续遇到闭合引号时，代码会无条件把 `NameLiteralStart.."` 或 `FormatStringStart.."` 这段原文替换成 `GenerateStaticName(...)` / `GenerateFormatString(...)` 的输出。与此同时，仓库里现有合法用法 `default Tags.Add(n"ExampleTag");` 与 `System::SetTimer(this, n"OnTimerCallback", 0.1f, true);` 都是独立 token 形式。推断：像 `Valuef"Score"`、`Actionn"Tag"` 这类本应作为 malformed token 留给脚本编译器报错的文本，当前会在预处理阶段被静默重写成“标识符前缀 + 生成代码”，把词法错误扭曲成另一份源码。 |
| 根因 | `n"` / `f"` 前缀字面量走的是独立分支，只校验“后继是不是引号”，没有复用 `IsStartOfIdentifier()` 或等价的 token-boundary helper；因此 prefixed literal 的起点规则和其他关键字/语法入口完全分叉。 |
| 影响 | 这会让某些本应明确失败的脚本在预处理阶段先被改写，再在后续编译中以更难理解的方式报错，甚至改变错误位置和文本。对未来继续收口 shared cursor / keyword-boundary 规则来说，这也是一个漏网分支；只修 `namespace/import/class` 的边界仍无法保证整个预处理器在 token 起点判断上行为一致。 |

**解决方案**

| 项目 | 内容 |
|------|------|
| 策略 | 为 `n"` / `f"` 前缀字面量补上与关键字一致的前导边界判断，只允许它们在真正的 token 起点触发。 |
| 具体步骤 | 1. 在 `AngelscriptPreprocessor.cpp/.h` 提取共享 helper，例如 `IsIdentifierContinuation(TCHAR)` / `CanStartStandaloneTokenAtCurrentPos(...)`，不要让 `n"` / `f"` 再绕过 `Issue-28`、`Issue-76` 正在收口的边界规则。2. 将 3749-3775 行的 name literal / format string 入口改为同时检查“前一个字符不是字母/数字/下划线”；必要时再限制前一个字符只能是行首、空白、`(`、`,`、`=`、`[`、`{` 等明确分隔符。3. 若前导边界不合法，不要设置 `NameLiteralStart/FormatStringStart`；让原始文本保持不变，交给后续脚本编译器按普通词法错误处理，而不是在预处理阶段静默生成替换代码。4. 将这套 helper 同步复用到 `namespace`、future `#include`、以及任何自定义前缀语法入口，避免预处理器继续出现“一部分 token 走共享边界，一部分 token 走裸字符判断”的分叉。5. 在 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` 新增回归：合法 `n"ExampleTag"`、`f"Score {Value}"` 仍会生成替换；`Actionn"Tag"`、`Valuef"Score"` 这类前缀不合法样例则不得被预处理器改写，`ProcessedCode` 应保留原文供后续 parser 报错。6. 若担心历史脚本误依赖当前宽松行为，先在 learning trace 中增加一次性诊断计数，统计有多少脚本命中过“非边界 prefixed literal”，再决定是否需要迁移提示。 |
| 涉及文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`，`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`，`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp` |
| 预估工作量 | S |
| 风险 | 若少量历史脚本已经误依赖“紧贴标识符也会触发 `n"` / `f"` 替换”的旧行为，修复后会从“静默改写”变成显式语法错误；但这属于期望中的收口，因为旧行为本身会隐藏真正的词法问题。 |
| 前置依赖 | 建议与 `Issue-28`、`Issue-76` 一起实现，统一 token 起点判断 helper。 |
| 验证方式 | 1. standalone `n"..."` / `f"..."` 现有正向样例输出不变。2. `Actionn"Tag"`、`Valuef"Score"` 这类非边界样例不再被预处理器改写。3. 回归 `namespace/import/class` 的边界测试，确认共享 helper 没有引入新的误报。 |

### 本轮汇总

| 优先级 | Issue 编号 | 类型 | 建议执行顺序 |
|--------|-----------|------|-------------|
| P2 | Issue-84 | Defect | 与 `Issue-28`/`Issue-76` 同批收口 token 起点规则，避免前缀字面量继续绕过共享边界 helper |
