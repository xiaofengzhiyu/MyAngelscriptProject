# Preprocessor 分析

---

## 分析 (2026-04-08 02:29)

### 发现 1：重复 `import` 会破坏模块依赖哈希的一致性

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:463-483`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:3175-3188`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:4262-4281` |
| 行号 | 463-483；3175-3188；4262-4281 |
| 描述 | 预处理阶段把每条 `import` 原样追加到 `File.Module->ImportedModules`，没有任何去重。编译阶段又按 `ImportedModules` 顺序逐项导入并把 `ImportModule->CombinedDependencyHash` 逐次 `xor` 到当前模块。只要脚本里重复写两次 `import Foo.Bar;`，同一个依赖模块的哈希就会被异或两次并抵消，导致当前模块的 `CombinedDependencyHash` 偏离真实依赖集合。 |
| 根因 | `ProcessImports()` 只负责拓扑排序和替换源码中的 `import` 语句，没有把 `ImportedModules` 视为集合；`CompileModules()` 与 `CompileModule_Types_Stage1()` 也直接消费这个数组，后续没有补充去重或规范化。 |
| 影响 | 依赖哈希被污染后，`ScriptModule->SetUserData((void*)(size_t)Module->CombinedDependencyHash, 0)` 写入的模块签名会与真实依赖图不一致，JIT / 依赖变更检测会把“依赖已变化”的模块看成“未变化”。同一依赖还会被重复 `ImportIntoModule()`，额外放大编译期开销。 |

### 发现 2：`automatic import` 打开时，手写 `import` 的兼容路径完全失效

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h:61-67`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:232-239`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:3490-3510`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:439-490` |
| 行号 | 61-67；232-239；3490-3510；439-490 |
| 描述 | 配置默认打开 `bAutomaticImports = true`，但 `Preprocess()` 只有在“关闭 automatic import”时才会调用 `ProcessImports()`。而“记录 warning 并把手写 `import` 从 chunk 中抹掉”的逻辑全部都写在 `ProcessImports()` 里。因此默认配置下，预处理器会识别出 `import` 语句并存进 `File.Imports`，却既不会发出 `bWarnOnManualImportStatements` 警告，也不会真正执行“ignored”语义。 |
| 根因 | `automatic import` 模式的兼容处理被放进了一个只在手动 import 模式下才会被调用的函数里，导致调用条件与函数内部语义自相矛盾。 |
| 影响 | 旧脚本里残留的手写 `import` 无法走预期的 backwards compatibility 路径，最终会原样流入后续 chunk/compile 流程。配置项 `bWarnOnManualImportStatements` 在默认模式下等同于死配置，用户也拿不到任何迁移提示。 |

### 发现 3：hot reload 删除文件路径把 `bTreatAsDeleted` 错传成了 `bLoadAsynchronous`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp:2448-2453`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:15`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:105-113`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:149-150`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:188-189` |
| 行号 | 2448-2453；15；105-113；149-150；188-189 |
| 描述 | hot reload 预处理阶段为删除文件计算 `bTreatAsDeleted` 后，调用 `Preprocessor.AddFile(PathPair.RelativePath, PathPair.AbsolutePath, bTreatAsDeleted);`。但 `AddFile()` 的第三个参数是 `bLoadAsynchronous`，第四个参数才是 `bTreatAsDeleted`。结果删除文件不会走 `File.RawCode = ""` 的删除分支，而是被标记成异步读取文件。随后 `PerformAsynchronousLoads()` 对每个异步文件直接执行 `OpenAsyncRead()` 并无判空地调用 `AsyncReadHandle->SizeRequest(...)`。 |
| 根因 | 调用点使用了位置参数而不是具名封装，且 `AddFile()` 同时承载“异步读取”和“视为已删除”两个布尔开关，语义非常容易混淆。 |
| 影响 | 删除 `.as` 文件的 hot reload 不是稳定地生成空模块描述符，而是进入错误的异步 I/O 路径。只要平台文件层对已删除路径返回 `nullptr` handle，这里就会触发空指针解引用；即使未崩溃，也会把“删除文件”误处理成“尝试读取一个不存在的文件”。 |

### 发现 4：异步脚本读取没有真正等待完成，`ParseIntoChunks()` 会与回调并发访问同一 `FFile`

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:141-210`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:224-230`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:157-160` |
| 行号 | 141-210；224-230；157-160 |
| 描述 | `PerformAsynchronousLoads()` 发起 `SizeRequest` / `ReadRequest` 后，只在第二个 `for` 循环里对仍处于 `bLoadAsynchronous == true` 的文件执行一次 `Sleep(0.001f)`，没有任何 outer loop、event 或 completion wait。函数返回后，`Preprocess()` 立刻对所有 `Files` 执行 `ParseIntoChunks(File)`。与此同时，异步回调仍会继续写 `File.RawCode`、修改 `File.bLoadAsynchronous` 并释放读取 buffer。 |
| 根因 | 该实现把“发起异步请求”和“等待所有请求结束”混在同一个函数里，但只实现了前者，没有建立完成同步点，也没有把资源回收放在真正的 completion 后。 |
| 影响 | 预处理器会在文件内容尚未加载完成时读取 `RawCode`，形成 data race。结果包括：随机解析空文件、chunk 边界不稳定、回调晚到时覆盖正在被解析的字符串，以及 `AsyncReadHandle/Request` 长时间悬挂不释放。这个问题会直接破坏预处理结果的确定性。 |

### 发现 5：显式 `import` 排序通过复制整份 `FFile` 完成，预处理成本会随脚本规模放大

| 项目 | 内容 |
|------|------|
| 维度 | E |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h:120-160`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:232-239`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp:463-497` |
| 行号 | 120-160；232-239；463-497 |
| 描述 | 预处理器在 `ParseIntoChunks()` 之后才做显式 `import` 拓扑排序。排序容器是 `TArray<FFile> SortedFiles`，每次 `ProcessImports()` 完成一个节点就执行 `OutSortedFiles.Add(File)`，最后再整体 `Files = SortedFiles`。但 `FFile` 本身承载了 `RawCode`、`TChunkedArray<FChunk>`、`ProcessedCode`、`GeneratedCode`、`Imports`、`Delegates` 等整份文件状态，这意味着排序阶段不是在重排轻量索引，而是在复制完整预处理上下文。当前仓库递归扫描可见 590 个 `.as` 文件，这个复制路径已经处在会被感知的规模。 |
| 根因 | import 解析把“依赖图排序”和“文件内容存储”耦合在同一个值类型 `FFile` 上，没有用索引、指针或模块名列表承载排序结果。 |
| 影响 | 一旦项目切回显式 `import` 模式，预处理阶段会在真正生成 `ProcessedCode` 之前先复制大量大对象，增加内存峰值和 CPU copy 开销。随着脚本数量继续增长，这段成本会先于编译器本身成为明显瓶颈。 |

---

## 分析 (2026-04-08 02:43)

### 发现 6：`#ifdef` / `#ifndef` 把“flag 已注册”误当成“flag 为 true”

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 38-65；3259-3275 |
| 描述 | 预处理器构造函数会把 `EDITOR`、`EDITORONLY_DATA`、`RELEASE`、`TEST` 等 flag 无条件注册进 `PreprocessorFlags`，只是值可能为 `false`。但 `ParseIntoChunks()` 处理 `#ifdef` / `#ifndef` 时，只检查 `PreprocessorFlags.Find(Identifier) != nullptr` 或 `== nullptr`，完全忽略了 map 中存储的 bool 值。结果是 `#ifdef EDITOR`、`#ifdef RELEASE` 等条件在 flag 已注册但值为 `false` 的上下文里仍然会被当成 true；反过来 `#ifndef EDITOR` 会被错误判成 false。 |
| 根因 | 这套实现把两种语义混在了一起：`#if FLAG` 走 `ParsePreProc()` 会读取 flag 的布尔值，而 `#ifdef FLAG` / `#ifndef FLAG` 却退化成“是否存在该 key”。当前 `PreprocessorFlags` 又恰好预注册了大量永远存在的内置 flag，于是两条语义发生了系统性偏差。 |
| 影响 | 脚本作者一旦使用常见的 `#ifdef EDITOR` / `#ifndef RELEASE` 写法，条件编译结果会和运行上下文不一致，直接把 editor-only 或 test-only 代码错误地带入非目标环境。由于 `EditorOnlyBlockLines` 也依赖同一套栈状态，这类错判还会连带污染 editor-only 行号标记。 |

### 发现 7：宏参数括号匹配不识别字符串，`UCLASS/USTRUCT/UINTERFACE/UENUM/UMETA` 的带 `)` 文本会被提前截断

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3534-3599；4145-4167 |
| 描述 | `ParseIntoChunks()` 在解析 `UMETA(`、`UCLASS(`、`USTRUCT(`、`UINTERFACE(`、`UENUM(` 时，统一依赖 `FindScopeCloseBracket()` 找右括号，再把中间内容切成 `Arguments`。但 `FindScopeCloseBracket()` 只是对 `(` / `)` 做裸计数，没有跳过字符串字面量或注释。因此只要 metadata 字符串里合法出现 `)`，例如 `DisplayName="Do (Test)"`、`ToolTip="Accepts ) in text"`，扫描器就会把这个字符串里的 `)` 误认成宏结束位置，后半段 specifier 会被直接截掉。 |
| 根因 | 词法扫描阶段已经维护了 `bInString` / `bInComment` 状态，但独立的括号查找辅助函数没有复用这些状态机，而是退回到了纯字符计数。 |
| 影响 | 宏参数一旦被截断，后续 `ParseSpecifiers()` 拿到的就是残缺字符串，表现为 specifier 丢失、宏解析失败，甚至把后续正常源码吞进宏体。这个问题直接落在用户最常写的 metadata 文本路径上，属于高频边界条件。 |

### 发现 8：外层分支已失活时，内层 `#if` 仍会被求值并对不可达代码报错

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3257-3395；3395-3402；4328-4344 |
| 描述 | `ParseIntoChunks()` 先处理所有 `#if/#elif/#ifdef/#ifndef` 指令，再根据 `bIfDefStackIsFalse` 把当前字符替换成空格。也就是说，只要源里出现新的 `#if`，无论当前是否处在一个已经确定为 false 的外层分支里，扫描器都会立即调用 `ParsePreProc()` 求值。`ParsePreProc()` 又会对未知 flag 直接 `LineError(... "Invalid preprocessor condition")` 并置 `bHasError = true`。结果是“永远不会执行到”的分支里只要写了当前上下文未注册的 flag，整个预处理都会失败。 |
| 根因 | 这套状态机只实现了“在失活分支里把普通源码抹成空格”，却没有实现“在失活分支里跳过表达式求值，只维护嵌套层级”的语义。 |
| 影响 | 平台/目标环境专属代码无法安全地放进互斥分支。例如外层 `#if EDITOR` 已经为 true 时，`#else` 里的 `#if CONSOLE_ONLY_FLAG` 仍然会触发未知 flag 错误，直接破坏本应成功的预处理流程。这既是错误恢复缺陷，也会迫使脚本把所有可能 flag 全量注册到所有上下文。 |

### 发现 9：现有自动化测试几乎只覆盖 happy path，前述高风险分支没有任何回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 行号 | 69-220；80-240 |
| 描述 | 当前仓库里与 `FAngelscriptPreprocessor` 直接相关的测试只有 3 个基础自动化用例和 1 个 learning trace，用例内容分别是 basic parse、macro detection、手动 `import`。而且两个 import 相关测试都显式把 `bUseAutomaticImportMethod` 改成 `false`（`AngelscriptPreprocessorTests.cpp:194-197`；`AngelscriptLearningPreprocessorTraceTests.cpp:118-121`），等于主动绕开了默认的 automatic import 路径。源码检索也没有发现任何覆盖 `#ifdef/#ifndef`、循环 import、`AddFile(..., bLoadAsynchronous, ...)`、删除文件 hot reload、或非法条件表达式的测试入口。 |
| 根因 | 测试设计目前把预处理器当成“把一个正常脚本转换成 module desc”的线性 happy-path 工具来验证，没有把条件编译、I/O 边界和错误恢复当成合同的一部分。 |
| 影响 | 这使得本轮已经确认的 automatic import 兼容路径失效、`#ifdef` 真假判定错误、失活分支仍求值等问题都能长期留在主干而不被 CI 捕获；异步加载和删除文件这类高风险分支也完全没有回归网。 |

### 发现 10：模块名规范化会删除路径中所有 `.as` 片段，导致路径到模块名不是单射

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 86-89；1944-1963；3047-3049 |
| 描述 | `FilenameToModuleName()` 和 `GetModuleByFilename()` 都通过 `Replace(TEXT(".as"), TEXT(""))` 把相对路径转成模块名。这不是“去掉末尾扩展名”，而是删除整条路径里所有 `.as` 子串。由于 `FindScriptFiles()` 递归收集文件时不会限制目录名里出现点号，像 `Foo/Bar.as` 与 `Foo.as/Bar.as` 这样的两个合法相对路径，最终都会被压成同一个模块名 `Foo.Bar`。 |
| 根因 | 模块名生成使用了全局字符串替换，而不是“先解析路径，再只移除文件扩展名”的结构化规范化。 |
| 影响 | 模块描述符不再能唯一映射回源文件路径，import 语句、诊断输出和热重载查找都可能落到错误模块上。即使仓库当前没有这样的目录布局，这也是一个已经存在的命名碰撞入口，会在脚本树扩展或外部插件接入时变成隐蔽的模块一致性问题。 |

---

## 分析 (2026-04-08 02:51)

### 发现 11：脚本读取失败会被静默降级成“文件已删除”，热重载会把暂时性 I/O 故障当成删除模块处理

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | 91-137；212-230；289-304；2449-2455；1856-1868；2378-2389 |
| 描述 | `AddFile()` 同步读取脚本失败后只打印 `Treating file as deleted` warning，但不会标记错误，也不会把该文件从编译列表移除。`Preprocess()` 仍会继续对这个空 `RawCode` 执行 `ParseIntoChunks()` 并产出一个 `Code` 为空的 `FAngelscriptModuleDesc`。hot reload 路径随后照常把这些 module 送进 `CompileModules()`。`ClassGenerator` 又会把“旧模块里存在、但新模块里完全不存在的 class”视为 removed class，并升级成 `FullReloadRequired`。 |
| 根因 | 预处理器把“磁盘读取失败”和“用户真的删除了脚本文件”合并成了同一条空源码路径，缺少独立的 I/O 失败状态，也没有在 `Preprocess()` 之前建立 fail-fast。 |
| 影响 | 文件被短暂占用、同步读取超时、或热重载瞬时读失败时，系统不会报告“本次编译不可继续”，而是把现有脚本内容当成被删除处理。结果是一次临时 I/O 抖动就可能触发 class removal 分析、升级到 full reload，并清理原本仍然存在的脚本类型。 |

### 发现 12：缺失 `;` 的 `import` 不会报语法错，解析器会把后续源码吞进模块名并留下误导性编译错误

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1377-1385；463-483；3497-3510；3175-3195 |
| 描述 | `ParseIntoChunks()` 解析 `import` 时只做“向后找第一个 `;`”的线性扫描，没有验证这一行是否真的以 `;` 结束。若脚本写成 `import Foo.Bar` 且漏掉分号，`ModuleEnd` 会一路扫到文件末尾，把后续源码一起拼进 `ImportDesc.ModuleName`。进入 `ProcessImports()` 后，这个畸形模块名会被原样写入 `Module->ImportedModules`。若 `ImportDesc.EndPosInChunk` 已经跨出当前 chunk，`ReplaceWithBlank()` 又会因索引越界直接返回，既没有清掉坏掉的 import，也没有报告预处理语法错误。最后编译阶段只能在模块第 1 行报“could not find module ... to import”。 |
| 根因 | import 词法解析没有单独的 statement terminator 校验；后续清理逻辑还假定 `import` 范围一定落在单个 chunk 内，导致 malformed import 既不会被拒绝，也不会被稳定抹除。 |
| 影响 | 一个很常见的笔误会把真正的错误从“缺少分号”扭曲成“找不到一个包含整段源码文本的模块名”，严重误导定位。更糟的是，损坏的 `import` 语句可能继续残留在 `ProcessedCode` 中，把同一个脚本同时送进“错误的依赖元数据”和“原始源码编译”两条路径。 |

### 发现 13：import/module 查找大小写敏感，与文件路径查找的 `IgnoreCase` 语义不一致

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 86-99；469-482；311-318；3007-3015；3043-3051 |
| 描述 | 模块名来自 `FilenameToModuleName(RelativeFilename)`，会保留文件系统返回的大小写；手写 `import` 则直接把源码中的文本 trim 后写进 `ImportDesc.ModuleName`。后续无论是预处理阶段寻找 `ProcessingModule`，还是编译阶段 `GetModuleByModuleName()` / `GetModule()`，都使用大小写敏感的字符串精确匹配。相比之下，`GetModuleByFilename()` 在把绝对路径转回模块名时专门调用了 `MakePathRelativeTo_IgnoreCase()`。这说明“按文件定位模块”已经考虑了大小写不一致，但“按 import 名字定位模块”没有。 |
| 根因 | 模块系统同时承载了“来自文件路径的名称”和“来自脚本文本的名称”，但只对前者做了 case-insensitive 归一化，后者始终保留原始拼写并参与 exact-match。 |
| 影响 | 在 Windows 这类大小写不敏感文件系统上，只要脚本文件是 `Gameplay/Foo.as`，而用户写成 `import gameplay.foo;`，预处理器就会把它当成另一个模块名处理。结果是同一物理文件可以通过 filename lookup 找到，却在 import 路径上报“module not found”，形成难以理解的环境相关错误。 |

### 发现 14：unresolved import 丢失源行号，最终只会在模块第 1 行报错

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 101-108；3502-3507；463-483；3175-3195 |
| 描述 | `FImport` 结构里专门保存了 `FileLineNumber`，解析 `import` 时也把当前 `LineNumber` 写进去了。但 `ProcessImports()` 在把 import 挂到 module descriptor 上时，只保留 `ImportDesc.ModuleName`，随后编译阶段发现模块不存在时，只能调用 `ScriptCompileError(Module, 1, ...)`。也就是说，预处理阶段本来已经拿到了精确的 import 源位置，进入编译阶段后却被压扁成了“这个模块第 1 行出错”。 |
| 根因 | module descriptor 只存储 `ImportedModules` 的字符串列表，没有把 import 的 source location 一起带到编译期；错误检测被延后到编译期后，预处理阶段收集到的定位信息已经丢失。 |
| 影响 | 当一个文件包含多个 `import`，或 import 名字由宏/重构产生时，用户拿到的只是模块级报错，无法直接定位是哪一条 import 失败。前面几条 import 边界 bug 触发时，这个诊断退化会进一步放大排障成本。 |

### 发现 15：重复 module name 没有被预处理阶段拒绝，不同阶段还会选中不同的冲突文件

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | 75-83；465-472；3133-3136；2914-2938；85-95 |
| 描述 | 预处理器为每个文件都创建独立的 `FAngelscriptModuleDesc`，`GetModulesToCompile()` 的 `AddUnique()` 只按 shared ref 去重，不会按 `ModuleName` 去重。若两个文件最终得到相同的 `ModuleName`，`ProcessImports()` 在解析依赖时会线性扫描 `Files` 并 `break` 在第一个匹配项上；到了编译阶段，`CompilingModulesByName` 和 `ActiveModules` 又通过 `Add(Module->ModuleName, Module)` 把后写入的条目覆盖前一个，最多只有一条 `ensureMsgf(!Contains(...))` 作为开发期提示。`ClassGenerator` 也同样用 `ModuleIndexByName.Add()` 接收这个覆盖结果。 |
| 根因 | 模块系统缺少“module name 必须唯一”的正式约束，重复名称只在编译时做了非阻断式 `ensure`，而不是在预处理阶段转成硬错误；同时不同阶段分别采用“第一个匹配”与“最后一次写入覆盖”两套冲突决策。 |
| 影响 | 一旦出现重复 module name，import 依赖解析可能指向第一个文件，而最终激活的 `ActiveModules` / class generator 状态却来自最后一个文件，造成模块描述符、依赖图和运行时类型注册彼此不一致。这个问题会把前面已确认的路径规范化碰撞，从“命名冲突”升级成“冲突时行为不确定”。 |

---

## 分析 (2026-04-08 03:04)

### 发现 16：automatic import 默认模式下，`CombinedDependencyHash` 与 precompiled 复用都忽略真实模块依赖

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 61-67；702-709；3173-3199；4247-4299；4350；2710-2713 |
| 描述 | 默认配置打开 `bAutomaticImports = true`。在这种模式下，`CompileModules()` 不会把任何依赖模块装入 `ImportedModules` 传给 `CompileModule_Types_Stage1()`，于是 `Module->CombinedDependencyHash` 始终保持为 `Module->CodeHash`，不会混入 AngelScript 编译器后续实际解析出的 `moduleDependencies`。同一函数里，precompiled 复用条件也只检查“所有显式 imports 都是 precompiled”与 `CompiledModule->CodeHash == Module->CodeHash`。由于 automatic import 路径下显式 imports 永远为空，这个门槛会被默认放行。随后 `ScriptModule->SetUserData((void*)(size_t)Module->CombinedDependencyHash, 0)` 和 `FAngelscriptPrecompiledData::CreateFunctionId()` 又继续把这个缺失依赖信息的哈希当成模块签名使用。 |
| 根因 | 模块系统把“依赖集合”仍然建模为 preprocessor 输出的 `ImportedModules`，但默认编译路径已经切到 automatic import，真实依赖只存在于编译后 `asCModule::moduleDependencies`。`CombinedDependencyHash`、precompiled 匹配条件和函数 ID 生成都没有迁移到这套新依赖图。 |
| 影响 | 只要某个模块自身源码没变、变化只发生在 automatic import 解析出的上游依赖里，这个模块的 `CombinedDependencyHash` 和 precompiled 命中条件就会错误地保持稳定。结果是：依赖变更后本应失效的模块签名不会变化，基于 `GetUserData()` 的 function ID 也可能继续复用旧值，甚至会把本该重新构建的模块直接短路到旧的 precompiled code。对于默认开启的 automatic import 模式，这属于模块描述符与真实依赖图的系统性失配。 |

### 发现 17：precompiled data 的模块元数据格式没有存储 automatic import 依赖，启动路径会永久丢失这部分信息

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 423-467；1417-1424；2752-2783；2046-2052 |
| 描述 | `FAngelscriptPrecompiledModule` 序列化时只保存 `CodeHash` 和 `ImportedModules`，没有任何字段承载 `CombinedDependencyHash` 或 AngelScript 编译后生成的 `moduleDependencies`。保存阶段 `InitFrom()` 也只是把 `Context.ModuleDesc->CodeHash` 和 `Context.ModuleDesc->ImportedModules` 原样拷进缓存；加载阶段 `GetModulesToCompile()` 再把这些字段还原回 `FAngelscriptModuleDesc`。因此，一旦走 `PrecompiledData->GetModulesToCompile()` 的启动路径，module descriptor 从磁盘读回来时就已经丢掉了 automatic import 解析出的真实依赖信息。 |
| 根因 | precompiled schema 仍然建立在“依赖只来自 preprocessor 显式 import 列表”的旧合同之上，没有随着默认 automatic import 模式升级存储结构。 |
| 影响 | 这意味着 finding 16 不是单纯的运行时漏算，而是缓存边界上的信息丢失：即使后续编译阶段想补救，启动时读出的 module descriptor 也只知道 `CodeHash` 和显式 `ImportedModules`。在依赖分析、precompiled 复用和诊断输出之间，会长期存在“磁盘缓存里的模块元数据比真实脚本依赖图更瘦”的不一致。 |

### 发现 18：手写 `import` 不做 filename-style 规范化，`.as` 扩展名和 `/` 路径分隔都会直接失配

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 86-89；3497-3510；463-483；3007-3054 |
| 描述 | 模块名的标准形态来自 `FilenameToModuleName()` / `GetModuleByFilename()`，会把 `Foo/Bar.as` 规范成 `Foo.Bar`。但 `ParseIntoChunks()` 解析手写 `import` 时，只是把 `import` 到 `;` 之间的原始文本 `TrimStartAndEnd()` 后塞进 `ImportDesc.ModuleName`；`ProcessImports()` 和 `GetModuleByModuleName()` 随后都用这个字符串做精确匹配，没有再执行“去 `.as` + `/` 转 `.`”的规范化。结果是 `import Foo/Bar.as;`、`import Foo/Bar;` 这类 filename-style 写法会稳定找不到同一个模块，尽管引擎本身另有 filename lookup helper 能识别它们。 |
| 根因 | import 解析把脚本文本直接当成 canonical module name，而不是先经过与文件发现路径一致的标准化函数。模块系统内部实际上同时存在“按 filename 归一化查找”和“按原始 module name 精确查找”两套规则。 |
| 影响 | 这会把很自然的路径式 import 写法全部推入“module not found”错误路径，尤其容易在从文件路径思维迁移到模块名语法时踩坑。由于错误最终仍落到模块级诊断，用户看到的只是找不到模块，而不是“import 需要 canonical module name”。 |

---

## 分析 (2026-04-08 03:13)

### 发现 19：`automatic import` 默认模式完全跳过 declared function import 的校验与绑定

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Internals/AngelscriptBuilderTests.cpp` |
| 行号 | 3490-3510；3865-3872；4057-4064；4611-4643；157-177 |
| 描述 | 预处理器解析 `import` 时，只把不包含 `(` 的语句当成模块依赖写入 `File.Imports`，等价于把 `import int SharedValue() from "BuilderImportSource";` 这类 declared function import 排除在 preprocessor 依赖模型之外。后续编译阶段，`CheckFunctionImportsForNewModules()` 和 swap-in 之后的 `ResolveAllDeclaredImports()` 又都被 `if (!ShouldUseAutomaticImportMethod())` 包住。当前默认配置 `bAutomaticImports = true` 时，这两条路径都不会执行。而 `ResolveDeclaredImports()` 是当前代码里唯一调用 `BindImportedFunction()` 的地方。测试 `AngelscriptBuilderTests.cpp:157-177` 也证明 imported function 在绑定前只会以未解析条目存在。 |
| 根因 | 代码把“模块 import 解析策略”和“declared function import 的校验/绑定”错误地挂在了同一个 `automatic import` 开关下，导致默认模块系统切到 automatic import 后，语言层的 function import 生命周期直接失联。 |
| 影响 | 默认配置下，脚本模块里的 declared function import 不会在编译后被校验，也不会在 swap-in 后被绑定成可调用函数。引擎内已不存在把这类 import 从 `GetImportedFunctionCount()` 状态推进到 `BindImportedFunction()` 状态的执行路径，属于默认路径上的功能缺失。 |

### 发现 20：字符串字面量里的 `#if/#ifdef/#else` 会被误当成真实预处理指令

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3256-3393；3881-3898；4349-4360 |
| 描述 | `ParseIntoChunks()` 每次循环一开始就先处理 `#ifdef/#ifndef/#if/#elif/#else/#endif`，判断条件只有 `Char == '#' && !bInComment`。这里完全没有排除 `bInString`。同一函数里，字符串状态直到后面的 `case '\"'` 才维护，所以只要脚本里有 `"debug #if RELEASE"`、`"#else"` 之类文本，扫描到 `#` 时即使已经处在字符串内部，也会进入真实的预处理分支。随后 `KillRawLine()` 会把这一行从 `#` 开始一直抹成空格，条件栈也会被同步修改。 |
| 根因 | 预处理指令识别被放在词法状态机之前执行，而且只复用了 comment 状态，没有把 string literal 当成不可解析区域。 |
| 影响 | 任意包含 `#if` 文本的字符串都可能破坏源码：轻则把字符串尾部和本行剩余代码抹掉，重则把 `IfDefStack` 推入错误状态，最终触发错误的条件裁剪、伪造的 `#endif` 匹配失败或后续 chunk 切分错乱。 |

### 发现 21：`IsStartOfIdentifier()` 只屏蔽前导数字 `0-1`，`2-9` 后面的关键字会被误判成独立语法

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3076-3089；3413-3495；3758-3765 |
| 描述 | 预处理器用 `IsStartOfIdentifier()` 判断当前位置是不是一个关键字的合法起点，但它检查数字边界时写成了 `PrevChar >= '0' && PrevChar <= '1'`。结果只有前一个字符是 `0` 或 `1` 时才会阻止关键字识别，`2-9` 都会被当成“不是 identifier 的一部分”。后面所有依赖这个 helper 的语法入口都会受影响，包括 `class`、`struct`、`interface`、`import`、`namespace`。例如顶层出现 `foo2import Bar;` 这类普通标识符拼接文本时，`import` 片段会被错误识别成真正的 import 语句。 |
| 根因 | 词法边界辅助函数里存在直接的字符范围笔误，导致“前一个字符是否仍属于 identifier”这个基础判断失真。 |
| 影响 | 这会把带数字后缀的普通标识符误拆成关键字入口，直接污染 `File.Imports`、chunk 类型切换和 namespace 栈。对 `import` 路径来说，最坏情况是从一段原本普通的顶层代码中凭空构造出错误模块名，后续再触发误导性的 import 相关诊断。 |

### 发现 22：循环 `import` 诊断丢失了真正的边信息，无法指出是哪一条语句形成闭环

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 101-107；163-167；439-452；4225-4235 |
| 描述 | 预处理器其实在 `FImport` 里保存了 `FileLineNumber`，但循环依赖检测链 `FImportChain` 只记录 `FFile*` 和 `Previous`。一旦 `ProcessImports()` 命中闭环，它只能对当前文件调用多次 `FileWideError()`，输出 `Detected circular import of module ...` 和一串模块名。整个链条里既没有 import 语句所在的行号，也没有“是 A 文件第几行 import 了 B”这种边级信息；所有诊断都被压成当前文件第 1 行。 |
| 根因 | 循环检测把链路建模成“访问过哪些文件”，而不是“沿着哪一条 import 边走到这里”；已有的 `FImport.FileLineNumber` 没有进入诊断链数据结构。 |
| 影响 | 当一个模块包含多条 import、或多个文件名/模块名相近时，用户只能看到抽象模块链，无法直接定位哪条 import 需要改。循环依赖已经是高排障成本问题，这种诊断退化会显著放大恢复时间。 |

### 发现 23：`ParseIntoChunks()` 已经报出 fatal error 后，explicit import 模式仍会继续改写依赖图和源码

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 228-243；3296-3300；3326-3353；3936-3939；439-497 |
| 描述 | `ParseIntoChunks()` 里多处会在遇到非法 `#elif/#else/#endif` 或缺失 `#endif` 时立刻 `bHasError = true`。但 `Preprocess()` 在完成整轮 chunk 解析后，并不会先因为 `bHasError` 提前返回；只要当前上下文是 explicit import 模式，它仍然会进入 `ProcessImports()`。而 `ProcessImports()` 会继续递归解析依赖、向 `Module->ImportedModules` 追加模块名，并用 `ReplaceWithBlank()` 改写 chunk 中的源码。真正的 early-out 被放到了 import 排序之后。 |
| 根因 | 预处理阶段把“chunk 词法/条件编译错误”和“import 拓扑排序”当成同一批工作处理，但失败短路点只放在后者结束之后。 |
| 影响 | 一次已经足以中止本轮预处理的 fatal parse error，仍会触发额外的 import 递归和源码改写，制造二次副作用。结果是诊断噪声增多，`ModuleDesc` 会留下部分填充过的 `ImportedModules`，错误恢复路径也失去“在第一个致命错误处停下”的确定性。 |

### 发现 24：explicit import 解析对每条依赖都线性扫描全部文件，当前仓库规模下已经进入可感知的 `O(imports * files)` 区间

| 项目 | 内容 |
|------|------|
| 维度 | E |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 232-239；463-474 |
| 描述 | explicit import 模式下，`Preprocess()` 会对 `Files` 中每个文件都调用一次 `ProcessImports()`。而 `ProcessImports()` 在处理每条 `FImport` 时，又通过 `for (FFile& OtherFile : Files)` 线性扫描整张文件表寻找目标模块，没有任何 `ModuleName -> FFile` lookup map。当前仓库递归扫描实测有 590 个 `.as` 文件，这意味着 import 解析已经不是常数级元数据操作，而是在一张数百节点图上反复做全表搜索。 |
| 根因 | import 拓扑排序实现直接复用了 `TArray<FFile> Files` 作为查找容器，没有为模块名建立索引，也没有在排序前预构建依赖图。 |
| 影响 | 项目一旦切回 explicit import 模式，import 越多、文件越多，预处理阶段的时间就越接近 `O(imports * files)`。这部分成本和前面已经记录的整对象复制叠加后，会让 import 解析本身变成明显瓶颈，而不是一个可以忽略的前置步骤。 |

---

## 分析 (2026-04-08 03:28)

### 发现 25：循环 `import` 命中 fatal error 后，`ProcessImports()` 仍继续改写模块描述符和 chunk 内容

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 232-243；439-497 |
| 描述 | `ProcessImports()` 在检测到循环依赖时会设置 `bHasError = true` 并 `return`。但上层递归调用点 `ProcessImports(*ProcessingModule, OutSortedFiles, &Chain);` 返回后，没有检查 `bHasError`，而是继续把当前 `ImportDesc.ModuleName` 追加进 `File.Module->ImportedModules`，再对 chunk 执行 `ReplaceWithBlank()`，最后还会把当前 `FFile` 标记为 `bImportsResolved = true` 并塞进 `OutSortedFiles`。`Preprocess()` 直到整个 import 排序结束后，才在 241-243 行统一 early-out。 |
| 根因 | import 递归只在最内层闭环检测处设置了全局错误标志，却没有把“子调用已经 fatal”作为当前层的短路条件；`ProcessImports()` 也没有在退出前恢复 `bIsResolvingImports` 或阻止成功路径收尾逻辑继续执行。 |
| 影响 | 循环依赖虽然最终会让 `Preprocess()` 返回失败，但失败前已经留下被部分改写的 `ImportedModules` 和被抹空的 `import` 语句，错误恢复路径不是原子失败。后续诊断、调试 hook、以及任何在失败后检查 `Files/ModuleDesc` 状态的代码，看到的都是“带副作用的半成品依赖图”，而不是触发 fatal 前的原始状态。 |

### 发现 26：`import` 语句不会剥离内联 block comment，注释文本会被当成模块名的一部分

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 1368-1369；3497-3507；4363-4390 |
| 描述 | 预处理器解析 `import` 时，只是从 `import` 后面一路扫到第一个 `;`，然后把这段原文 `TrimStartAndEnd()` 后写进 `ImportDesc.ModuleName`。这条路径不会像 default 语句那样调用 `StripCommentsFromLine()`。因此 `import Gameplay.Core /* shared helpers */;` 这类写法会把 `/* shared helpers */` 一并保留在模块名里，后续 `ProcessImports()` / `GetModuleByModuleName()` 都会按这个脏字符串做精确匹配。 |
| 根因 | `import` 词法解析把“读取 statement 原文”和“抽取 canonical module name”合并成了一步，既没有剥离注释，也没有做 token 级规范化；仓库里现成的 `StripCommentsFromLine()` 只被 defaults 处理复用。 |
| 影响 | 只要脚本作者在 `import` 末尾加入 block comment 解释依赖来源，模块查找就会稳定走到 `module not found` 路径，而不是解析成原本的模块名。这属于 import 路径解析的边界条件缺陷，错误消息还会把整段注释文本一起带进模块名，进一步误导排查。 |

### 发现 27：hot reload 的文件账本按大小写精确匹配，但模块按 filename 查找时又使用 `IgnoreCase`

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 408；416-419；1426-1434；2299-2324；2391-2415；3029-3049 |
| 描述 | 热重载阶段的 `FileHotReloadState`、`PreviouslyFailedReloadFiles`、`QueuedFullReloadFiles` 和 `FilesToHotReload` 都建立在 `FFilenamePair` 或 `RelativeFilename` 的精确字符串匹配上：`FFilenamePair` 的 hash/equality 直接比较 `AbsolutePath` 与 `RelativePath` 原文，`RelativeFileToModule` 也用 `Section.RelativeFilename` 做大小写敏感 key。可同一份运行时代码里，`GetModuleByFilename()` 查找模块时却专门对绝对路径使用 `Equals(..., IgnoreCase)`，并在把路径转回模块名时调用 `MakePathRelativeTo_IgnoreCase()`。 |
| 根因 | 模块系统内部同时存在两套路径语义：预处理/热重载账本沿用原始字符串，按文件定位模块时又显式切换成 case-insensitive 规则，但两边没有统一的 canonicalization 层。 |
| 影响 | 在 Windows 这类大小写不敏感文件系统上，只要文件变更事件、历史缓存和 `Module->Code` 中记录的路径出现 casing 差异，热重载就可能把同一物理文件看成两个不同 key：`RelativeFileToModule.Find(File.RelativePath)` 命不中旧模块，删除文件也可能因为 `AlreadyDeletedFiles.Contains(PathPair)` 失配而走错分支。结果是依赖传播、失败重试和删除检测都可能偏离真实模块。 |

### 发现 28：当前插件没有把 `#include` 纳入任何预处理、依赖跟踪或模块哈希合同

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Reference/angelscript-v2.38.0/sdk/tests/test_feature/bin/scripts/TestExecuteScript.as`；`Reference/angelscript-v2.38.0/sdk/samples/include/bin/script.as`；`Reference/angelscript-v2.38.0/sdk/samples/include/bin/scriptinclude.as` |
| 行号 | 289-304；3256-3393；4343-4345；11；2；5 |
| 描述 | `ParseIntoChunks()` 对 `#` 开头的指令只识别 `#if/#ifdef/#ifndef/#elif/#else/#endif` 和 `#restrict usage ...`，没有任何 `#include` 分支。之后 `Preprocess()` 计算 `CodeHash` 时只哈希当前文件自己的 `ProcessedCode`，编译阶段再把这些 section 原样送入 `AddScriptSection()`。仓库检索 `Plugins/Angelscript/Source/AngelscriptRuntime` 与 `Plugins/Angelscript/Source/AngelscriptTest` 也没有发现 `SetIncludeCallback` / `IncludeCallback` 实现。相比之下，仓库自带的 AngelScript reference 样例明确使用了 `#include "include.as"`、`#include "scriptinclude.as"` 和循环 include。 |
| 根因 | 当前插件的脚本编译合同是“每个 `.as` 文件先转成一个独立 `FAngelscriptModuleDesc`，依赖只通过 module import 建模”，并没有为 include file 提供展开层、去重层或依赖边记录层。 |
| 影响 | `#include` 语句既不会进入 `ImportedModules` / hot reload 依赖传播，也不会影响 `Module->CodeHash` 与 `CombinedDependencyHash`。这意味着 include 依赖对模块系统是不可见的：即便脚本作者沿用 AngelScript reference 的 include 写法，插件也无法把被包含文件纳入预处理、缓存失效和重载一致性模型。 |

---

## 分析 (2026-04-08 03:37)

### 发现 29：反射宏条件校验拒绝 `!FLAG` 与 `#else` 分支，导致合法的配置开关写法无法通过预处理

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3282-3287；3333-3339；3618-3644 |
| 描述 | `#if` 分支会把原始条件文本直接存进 `IfDefStack.Condition`，所以 `#if !FEATURE` 进入栈后的条件就是 `!FEATURE`；`#else` 还会在 3336-3339 行显式给条件名加上或移除 `!`。但当扫描到 `UPROPERTY` / `UFUNCTION` 时，合法性校验只接受 `Condition == "EDITOR"`、`Condition == "EDITORONLY_DATA"`，或 `PreprocessorFlags.Contains(Condition) && PreprocessorFlags[Condition]`。这意味着只要反射宏位于一个已激活的 `#if !FEATURE`、`#elif !FEATURE` 或 `#else` 分支里，即使 `FEATURE` 本身是配置里声明过的 flag，也会稳定触发 `Cannot put a UPROPERTY or UFUNCTION...` 错误。 |
| 根因 | 条件栈记录的是“当前分支表达式文本”，而反射宏校验却把它当成“未经变换的 flag 名”来查表，没有对 `!FLAG`、`#else` 翻转后的条件做归一化。 |
| 影响 | 脚本无法用常见的 negated feature flag 或 `#else` 分支切换反射成员，例如“非编辑器构建下暴露另一组 `UFUNCTION`”这种写法会在预处理阶段被直接拒绝。模块系统对配置 flag 的支持因此只剩下正向分支，条件编译能力与错误消息宣称的“flags declared in configuration”不一致。 |

### 发现 30：`#else` / `#endif` 只做前缀匹配，directive 拼写错误会被静默当成合法分支处理

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3321-3344；3345-3360；4349-4360 |
| 描述 | `ParseIntoChunks()` 识别 `#else` 与 `#endif` 时，只检查当前位置是否以这两个字面量开头，没有验证后续字符是否为行结束或空白。于是 `#else if FEATURE`、`#elseif FEATURE`、`#endif_typo` 这类本应报语法错的文本，都会命中对应分支。紧接着 `KillRawLine()` 会把整行内容抹掉，使 trailing token 完全消失，最终条件栈按“纯 `#else`”或“纯 `#endif`”被更新。 |
| 根因 | 这两个 directive 的词法匹配使用了裸前缀比较，而 `#if/#ifdef/#ifndef/#elif` 都要求后面跟空白字符。`KillRawLine()` 又会把同一行剩余文本直接擦除，掩盖掉原始拼写错误。 |
| 影响 | 条件编译里的常见笔误不会得到诊断，而是被静默降级成另一条合法 directive，直接改变源码裁剪结果。最危险的场景是把不支持的 `#else if` 误写当成 `#else`，导致后续整段代码在错误分支下参与编译，排查时还看不到原始 typo。 |

### 发现 31：delegate/event 声明在 `)` 与 `;` 之间插入注释后会完全绕过预处理生成链

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 565-725；3688-3715；4170-4194 |
| 描述 | delegate/event 识别阶段会先用 `FindScopeCloseBracket()` 找到参数列表结尾，再调用 `FindSemicolonDirectlyAfter()` 查找紧随其后的 `;`。但这个 helper 只接受空格、制表符和换行，不接受 `/*comment*/` 或 `// comment`。因此 `delegate void FOnDone() /* note */;`、`event void FOnDone() // note` 这样的声明不会写入 `File.Delegates`。随后 `ProcessDelegates()` 也就不会为它生成包装 struct、不会把声明抹空、不会向 `Module->Delegates` 注册元数据。 |
| 根因 | delegate 词法分析假设 `)` 和 `;` 之间只能出现 whitespace，没有复用预处理器其余位置已经具备的 comment-aware 扫描逻辑。 |
| 影响 | 一个合法但带注释的 delegate/event 声明会直接掉出插件自己的 delegate 生成流程，表现为缺失生成代码、缺失 delegate 元数据，或把原始 `delegate/event` 语法原样留给后续编译阶段处理。对使用注释解释回调语义的脚本来说，这是一个隐藏很深的声明边界条件。 |

### 发现 32：`EditorOnlyBlockLines` 只识别 `EDITOR`，`EDITORONLY_DATA` 块不会写入模块的 editor-only 行号元数据

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 41；62；70；3092-3126；3625；3639；4353-4356 |
| 描述 | 预处理器初始化时明确注册了 `EDITORONLY_DATA` flag，并且反射宏合法性检查也把 `EDITORONLY_DATA` 与 `EDITOR` 并列视为 editor-only 条件。但 `UpdateEditorBlockLines()` 扫描条件栈时只检查 `IfDef.Condition == "EDITOR"`，完全忽略 `EDITORONLY_DATA`。结果是：`#if EDITORONLY_DATA` / `#endif` 包裹的源码虽然会按条件编译参与裁剪，却不会在 `File.Module->EditorOnlyBlockLines` 里留下任何范围记录。编译阶段 4355 行又把这个数组原样交给 `SetEditorOnlyBlockLinePositions()`。 |
| 根因 | editor-only 行号元数据的构建逻辑和其余预处理规则没有共享同一套“哪些条件属于 editor-only”判定，导致 `EDITORONLY_DATA` 在不同子路径中被解释成两种不同语义。 |
| 影响 | 模块描述符里的 editor-only 行号表会系统性漏掉 `EDITORONLY_DATA` 区块，进而让 builder 基于这份元数据发出的 editor-only 诊断与真实源码条件不一致。这属于模块元数据与预处理结果的直接失配，后续任何依赖 `EditorOnlyBlockLines` 的检查都会看到一份偏瘦的视图。 |

---

## 分析 (2026-04-08 03:50)

### 发现 33：后半段预处理报错后不会 fail-fast，hook 与模块代码生成仍会继续运行

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 行号 | 245-307；1462-1719；2350-2855；29-30；123-129 |
| 描述 | `Preprocess()` 只在 `ProcessImports()` 和 `DetectClasses()` 之后检查 `bHasError` 并提前返回。后面的 `AnalyzeClasses()`、`ProcessMacros()`、`ProcessDelegates()`、`ProcessDefaults()`、`CondenseFromChunks()`、`PostProcessRangeBasedFor()`、`PostProcessLiteralAssets()` 即使已经通过 `MacroError()` / `LineError()` 把 `bHasError` 置为 `true`，也仍会继续执行，最后还会广播 `OnProcessChunks` 与 `OnPostProcessCode`，并把 `ProcessedCode` 写入 `Module->Code`。learning trace 测试也证明这两个 hook 被设计成外部可观察扩展点。 |
| 根因 | `Preprocess()` 的错误短路点只覆盖了 very early 阶段，没有把“后续阶段已经出现 fatal 诊断”纳入统一的 fail-fast 合同；静态 hook 广播和模块描述符落盘逻辑也没有额外的 `bHasError` 守卫。 |
| 影响 | 一旦 class/macro/default 解析在中后段出错，失败路径仍会继续改写 chunk、生成额外代码、更新 `PostInitFunctions`/`CodeHash`，并向外部 hook 暴露一份不稳定的 `FAngelscriptPreprocessor` 状态。最终 `Preprocess()` 虽然返回 `false`，但失败前的副作用已经发生，错误恢复不再是原子性的。 |

### 发现 34：`GetModulesToCompile()` 把“执行过预处理”和“预处理成功”混为一谈，失败后仍会暴露半成品模块

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 行号 | 75-83；220-223；2081-2082；145-150 |
| 描述 | `Preprocess()` 一开始就在 220-223 行把 `bIsPreprocessed` 设为 `true`，而 `GetModulesToCompile()` 的唯一前置条件只是这个布尔值。它既不检查 `bHasError`，也不要求调用方确认 `Preprocess()` 返回值为 `true`。结果是，只要执行过一次 `Preprocess()`，哪怕中途失败，外部仍然可以拿到 `File.Module` 数组。引擎初始化路径甚至会在 `bSuccess = Preprocessor.Preprocess();` 之后无条件执行 `ModulesToCompile = Preprocessor.GetModulesToCompile();`。 |
| 根因 | 预处理器 API 把“生命周期状态”与“结果有效性”折叠成了同一个 `bIsPreprocessed` 标志，没有单独建模 `Succeeded/Failed` 状态，也没有在结果访问器里做有效性约束。 |
| 影响 | 失败路径里构造出的半成品 `FAngelscriptModuleDesc` 不再局限于预处理器内部，而是会通过公开 API 向外泄漏。调用方只要漏掉一次返回值判断，就可能基于空 `Code`、残缺 `ImportedModules` 或部分生成的元数据继续决策，放大后续模块系统的不一致性。 |

### 发现 35：失活分支里的 `#restrict usage` 仍会写入模块限制，模块元数据会偏离实际编译结果

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 3256-3393；3395-3402；3888-3890；4522-4607 |
| 描述 | `ParseIntoChunks()` 处理 `#restrict usage allow/disallow` 的逻辑和 `#if/#else` 一样，发生在 `bIfDefStackIsFalse` 把死分支源码抹成空格之前，而且这两个分支本身没有任何“当前是否处在 inactive branch”判断。结果是：即使某条 `#restrict usage` 位于已经被外层 `#if` 排除的代码路径里，它仍会被记录到 `File.Module->UsageRestrictions`。编译阶段 3888-3890 行又会无条件对这些 restriction 调用 `CheckUsageRestrictions()`。 |
| 根因 | 预处理器把“directive 词法识别”和“分支可达性裁剪”拆成了两个顺序阶段，但 `#restrict usage` 被放在前者处理，却没有像普通源码那样受 `bIfDefStackIsFalse` 约束。 |
| 影响 | 模块描述符中的使用限制不再代表“当前配置下真正编译进模块的约束”，而是代表“源码里出现过的约束文本”。脚本作者即使把某条 restriction 放在死分支里，也仍然可能在后续 `CheckUsageRestrictions()` 中得到真实生效的限制错误，形成明显的配置漂移。 |

### 发现 36：automatic import 的 hot reload 依赖扩散采用固定点全表扫描，规模上来后会把每次变更放大成多轮 `O(V*E)` 遍历

| 项目 | 内容 |
|------|------|
| 维度 | E |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 2315-2375 |
| 描述 | automatic import 模式下，hot reload 为了找“依赖已修改模块的所有模块”，先把直接命中的 module 放进 `MarkedModules`，然后进入 `while (bDidMarkModules)`。每一轮都线性扫描全部 `ActiveModules`，对每个未标记模块再线性检查其 `moduleDependencies`，只要新标记了任何一个模块就再来一轮。这个实现没有预构建 reverse graph，也没有 queue/BFS。按本轮仓库实测，`rg --files -g "*.as"` 可见 325 个脚本文件；随着模块数增长，这段固定点求解会在每次热重载里重复扫整张依赖图。 |
| 根因 | automatic import 依赖传播直接复用了运行时 `moduleDependencies`，但没有为“从变更节点反向找到所有依赖者”建立一次性的反向邻接表，导致算法只能靠多轮全表收敛。 |
| 影响 | 依赖链越长、模块越多，单次文件改动触发的热重载前置分析就越接近多轮 `O(V*E)`。这条成本出现在真正编译之前，会直接拉长编辑器内的 reload latency，且与 explicit import 路径上已记录的排序/查找开销是叠加关系。 |

---

## 分析 (2026-04-08 04:02)

### 发现 37：轮询式 hot reload 不会检测脚本删除，非编辑器开发模式会永久保留已删模块

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptEditor/Private/AngelscriptDirectoryWatcherInternal.cpp` |
| 行号 | 1615-1619；2729-2778；2859-2895；408；52-86 |
| 描述 | 非编辑器开发模式下，运行时会打开 `bUseHotReloadCheckerThread`，明确走“轮询扫描磁盘”的 hot reload 路径。`CheckForHotReload()` 虽然会消费 `FileDeletionsDetectedForReload`，但轮询端 `CheckForFileChanges()` 只重新枚举当前仍然存在的 `.as` 文件，并在“首次见到文件”或“时间戳变化”时把它加入 `FileChangesDetectedForReload`；它既不会遍历 `FileHotReloadState` 查找“这次扫描里消失的 key”，也不会自己填充 `FileDeletionsDetectedForReload`。对比之下，删除事件入队逻辑只存在于 editor 的 `AngelscriptDirectoryWatcherInternal.cpp:59-76`。这意味着一旦离开 editor watcher，删除 `.as` 文件根本不会进入 hot reload 队列。 |
| 根因 | 轮询式 hot reload 只实现了“新增/修改文件”的正向扫描，没有实现基于上一轮状态的删除 diff；删除事件模型被耦合到了 editor-only 的 DirectoryWatcher。 |
| 影响 | 在 `bScriptDevelopmentMode && !RuntimeConfig.bIsEditor` 的运行模式里，删除脚本文件不会触发 `Preprocessor.AddFile(..., bTreatAsDeleted)` 这条删除处理路径，旧模块、旧类和旧依赖会继续留在运行时，直到进程重启或发生别的 reload 机会。这让“删除脚本”在一整类支持的开发场景里变成了 silent stale-state。 |

### 发现 38：hot reload 状态表在脚本收集边界就丢失了 script root 身份，跨 root 同名脚本无法独立跟踪

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h` |
| 行号 | 539-566；1944-1963；1999-2014；408；1089-1094；2880-2888 |
| 描述 | 运行时默认会把工程 `Project/Script` 和每个 enabled content plugin 的 `PluginBaseDir/Script` 都加入脚本根列表。但 `FindAllScriptFilenames()` 对每个 root 都传入 `RelativeRoot = ""`，于是 `FindScriptFiles()` 产出的 `FFilenamePair.RelativePath` 只保留“root 内部相对路径”，不再包含 root 身份。后续 `FileHotReloadState` 又只按这个 `RelativePath` 建 `TMap<FString, FHotReloadState>`，`CheckForFileChanges()` 也只用它做 `Find/Add`，`DiscardModule()` 删除状态时同样只传 `Section.RelativeFilename`。结果是：如果 project root 和某个 plugin root 各自都有 `Gameplay/Foo.as`，两份物理文件会共用同一个 hot reload 状态 key，哪怕失败重试和 full-reload 队列仍然用包含 `AbsolutePath` 的 `FFilenamePair` 区分它们。 |
| 根因 | 脚本文件收集阶段把“来自哪个 script root”这层身份信息提前抹掉了，而 hot reload 状态表使用的 key 又比其他账本更窄，只保留了 `RelativePath`。 |
| 影响 | 一旦出现跨 root 的同名相对路径脚本，两个文件的时间戳状态会互相覆盖；丢弃其中一个模块还会把另一个文件的 `FileHotReloadState` 一并移除。这样即使编译队列、失败队列还能靠 `AbsolutePath` 分开记录，热重载扫描阶段也已经先把两个脚本混成了一个实体，后续是否重载、何时重载都会偏离真实文件集。 |

---

## 分析 (2026-04-08 04:14)

### 发现 39：precompiled 启动路径里的 `CodeHash` 校验是在把缓存与自己比较，无法发现脚本和缓存已失配

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | 2046-2052；2773-2779；4284-4294 |
| 描述 | `InitialCompile()` 在 `bUsePrecompiledData && !bScriptDevelopmentMode` 路径下完全跳过 `Preprocessor` 和脚本磁盘读取，直接用 `PrecompiledData->GetModulesToCompile()` 生成 `ModulesToCompile`。而 `GetModulesToCompile()` 又把 `FAngelscriptPrecompiledModule.CodeHash` 原样拷回 `ModuleDesc->CodeHash`。随后 `CompileModule_Types_Stage1()` 再用同一个 cache 条目里的 `CompiledModule->CodeHash` 去比较 `Module->CodeHash`。这条所谓“校验”没有任何当前源码或当前预处理结果参与，比较双方都来自同一份缓存。 |
| 根因 | precompiled 复用门槛建立在“cache module vs. 由同一 cache 反序列化出来的 module descriptor”这组自引用数据上，而不是建立在“cache module vs. 当前脚本内容”之上。 |
| 影响 | 只要进入 fully precompiled 启动模式，运行时就不会验证磁盘上的 `.as` 文件是否仍与缓存一致。部署了陈旧 `PrecompiledScript.Cache`、脚本文件被手工替换、或缓存与构建产物错配时，系统仍会静默接受旧 cache 并继续加载，`CodeHash` 守卫在这条路径上等同于失效。 |

### 发现 40：precompiled 反序列化不会重建 `Code` / `UsageRestrictions` / `EditorOnlyBlockLines`，缓存启动拿到的 module descriptor 天生比预处理路径更瘦

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 1277-1319；425-461；1419-1485；2773-2927；4528-4564；4953-4956 |
| 描述 | `FAngelscriptModuleDesc` 正常包含 `Code` section 列表、`UsageRestrictions` 和 `EditorOnlyBlockLines`。但 `FAngelscriptPrecompiledModule` 的序列化字段只覆盖 `CodeHash`、`ImportedModules`、delegate 名称、`ScriptRelativeFilename`、`PostInitFunctions` 等少量元数据，没有 `Code` section，也没有 editor-only / restriction 元数据。`GetModulesToCompile()` 从 cache 重建 `ModuleDesc` 时，确实只回填了这些字段，结果是缓存启动拿到的 `ModuleDesc->Code` 为空，`UsageRestrictions` / `EditorOnlyBlockLines` 也保持默认空值。这个空壳随后会直接进入 `CheckUsageRestrictions()` 与 `ScriptCompileError(Module, ...)`。 |
| 根因 | precompiled schema 把 module descriptor 压缩成了“足够恢复脚本类型系统”的最小集合，但没有保持与预处理路径同等的 module-level 元数据合同。 |
| 影响 | 这会造成两类已验证的行为漂移。第一，`CheckUsageRestrictions()` 在遍历模块时一遇到 `Module->Code.Num() == 0` 就 `return`，因此缓存启动会把整轮 restriction 校验直接短路掉。第二，任何基于 `ScriptCompileError(Module, ...)` 的诊断都会因为 `Code` 为空而退化成只报 `ModuleName`，丢失文件级定位。即使不考虑更多调用点，precompiled 与实时预处理已经暴露出两套不等价的 module descriptor 语义。 |

### 发现 41：precompiled 的 script section 解析固定拼到 project root，plugin 脚本会被写进错误的文件名槽位

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | 781-794；1430-1431；1485；1488-1495；2046-2052 |
| 描述 | 引擎初始化早期只把 `AllRootPaths` 设成 `DiscoverScriptRoots(true)` 返回的单个 project root。若随后走 fully precompiled 路径，`InitialCompile()` 不会再调用 `MakeAllScriptRoots()` 扩展 plugin script roots。与此同时，cache 里对每个模块只保存了一个 `ScriptRelativeFilename`，`GetScriptSection()` 又总是用 `GetScriptRootDirectory()` 返回的“第一个 root”去拼出 section 路径。对于来自 content plugin 的脚本，这里得到的并不是实际文件绝对路径，而是“project root + plugin 内相对路径”的伪路径。 |
| 根因 | precompiled line-info 恢复逻辑把 script section 的根目录硬编码成了单一 project root，却没有把模块所属 script root 一并持久化到 cache。 |
| 影响 | plugin 脚本在 precompiled 模式下会把 `scriptSectionIdx` 绑定到错误文件名，后续任何依赖 AngelScript script section 名称的调用栈、断点、诊断或 profile 统计都会把 plugin 代码指向 project `Script/` 下的同名伪路径。这让 cache 启动模式下的源码定位与真实仓库布局发生系统性偏移。 |

---

## 分析 (2026-04-08 10:33)

### 发现 42：缺失 import 不会在预处理阶段拦截，依赖它的下游模块还能把失败模块当成可导入占位继续编译

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp；Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp |
| 行号 | 439-497；3132-3135；3175-3195；4247-4278 |
| 描述 | ProcessImports() 只在当前 Files 集合里查找 ImportDesc.ModuleName，找不到时既不报错也不设置 HasError，仍然把名字追加到 File.Module->ImportedModules。进入编译阶段后，CompileModules() 又会先把当前批次所有 module desc 放进 CompilingModulesByName，再逐个解析 imports。这样一来，只要上游模块 A 因“缺失 import”在 3191-3195 行被标成 CompileError，下游模块 B 仍然能通过 CompilingModulesByName.Find("A") 找到 A，并跳过“找不到模块”分支。到了 CompileModule_Types_Stage1()，B 对这个失败依赖唯一的保护只剩 nsure(ImportModule->ScriptModule != nullptr)；若指针为空，ImportIntoModule() 不会执行，但当前模块也不会被标记成 import error。 |
| 根因 | 预处理阶段把 ImportedModules 当成纯字符串清单，没有把“目标模块必须存在且成功进入 Stage1”建成依赖合同；编译阶段又把“名字在当前 compile batch 中出现”误当成“这个依赖已经可导入”。 |
| 影响 | 真正的根因模块会先报一次缺失 import，但依赖它的下游模块随后会在缺少导入类型的情况下继续进入 parse/generate 阶段，产出二次编译错误，掩盖首个故障。与此同时，下游模块的 CombinedDependencyHash 还会对一个未成功装配的依赖继续做异或，模块描述符与真实可用依赖集发生偏离。 |

### 发现 43：单个失败模块会让整轮 UsageRestrictions 校验直接短路

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp；Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp |
| 行号 | 3187-3208；3881-3889；4525-4564 |
| 描述 | CompileModules() 即使已经知道 HadCompileErrors == true，仍会在 3886 行调用 CheckUsageRestrictions(CompiledModules)。而 CheckUsageRestrictions() 在遍历当前批次模块和 ActiveModules 时，只要遇到一个 ScriptModule == nullptr 或 Code.Num() == 0 的模块，就直接整函数 eturn。前一条已验证的缺失 import 路径正好会在 3191-3195 行把模块标成 CompileError 并跳过 CompileModule_Types_Stage1()，从而留下 ScriptModule == nullptr 的 module desc。结果不是“跳过坏模块，继续检查剩余模块”，而是“整轮 restriction 校验被第一处失败模块熔断”。 |
| 根因 | restriction 校验函数把“某个模块当前不可检查”实现成了全局 eturn，同时调用点又没有在 compile-error 场景下跳过或筛除失败模块。 |
| 影响 | 一次与 restriction 无关的 import/编译失败，就足以让同批次所有其他模块的 #restrict usage 合同完全不被验证。对于依赖 restriction 做模块边界约束的工程，这会把“一个局部失败”扩大成“整轮边界校验失效”，属于明显的模块描述符消费不一致。 |

### 发现 44：预处理阶段会同时保留同一脚本文本的多份拷贝，峰值内存随脚本规模线性放大

| 项目 | 内容 |
|------|------|
| 维度 | E |
| 严重度 | Medium |
| 文件 | Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h；Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp |
| 行号 | 120-149；3150-3184；3983-4142 |
| 描述 | FFile 同时持有 RawCode、TChunkedArray<FChunk>、ProcessedCode 和 GeneratedCode。ParseIntoChunks() 每次 SubmitChunk() 都会执行 Chunk.Content = File.RawCode.Mid(...)，把原始源码切片复制进每个 chunk；CondenseFromChunks() 又把所有 Chunk.Content 重新拼成 ProcessedCode；后续 PostProcessRangeBasedFor() 与 PostProcessLiteralAssets() 还会各自构造一份 NewCode 再回写 File.ProcessedCode。本轮对当前仓库的 g --files -g '*.as' 实测命中 325 个脚本文件，说明这不是只影响小样例的理论路径。 |
| 根因 | 预处理器选择了“按阶段保留完整字符串快照”的实现方式，而不是用共享切片、索引区间或 in-place token stream 表示文本变换。 |
| 影响 | 在真正开始 AngelScript 编译之前，预处理阶段就会把整份脚本语料复制多轮，抬高内存峰值和 allocator 压力。随着脚本数量和单文件长度增加，这条成本会与前面已记录的显式 import 全量复制叠加，先把 preprocessing 变成明显瓶颈。 |

### 发现 45：#restrict usage 与失败模块短路路径没有任何自动化回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp；Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp；Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp；Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp |
| 行号 | 3364-3389；3881-3889；4525-4564；70-220；82-225 |
| 描述 | 当前预处理相关自动化入口总共只有 4 个：3 个 Preprocessor.* 测试和 1 个 Learning.Runtime.Preprocessor。源码检索 Plugins/Angelscript/Source/AngelscriptTest 没有任何 #restrict、estrict usage、UsageRestrictions 或 CheckUsageRestrictions 命中。这意味着仓库里既没有正向用例验证 restriction 元数据会被记录并执行，也没有负向用例验证“某个模块失败时，其余模块的 restriction 校验仍然继续”。 |
| 根因 | 现有测试面把预处理器主要当成 chunk/macro/import 的基础 happy-path 工具来验证，没有把模块边界约束和失败路径下的 restriction 消费合同纳入回归集。 |
| 影响 | 本轮已确认的两条 restriction 相关缺陷都缺少自动化守卫：一条是失活分支里的 #restrict usage 仍会生效，另一条是单个失败模块会让整轮 restriction 校验直接短路。后续即使有人修改 import / compile error 恢复逻辑，也很难从 CI 上第一时间发现边界约束已经被破坏。 |

### 发现 46：同步文件读取失败会在主编译路径上串行睡眠重试，单文件最坏额外阻塞约 0.51 秒

| 项目 | 内容 |
|------|------|
| 维度 | E |
| 严重度 | Medium |
| 文件 | Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp；Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp |
| 行号 | 91-137；2075-2078；2448-2455 |
| 描述 | AddFile() 的同步读取分支会在失败后重试 6 次，并按  .01s + 0.1s + 0.2s + 0.2s 串行 Sleep，单个文件在彻底放弃前就会额外阻塞约  .51s，还不算每次 LoadFileToString() 本身的 I/O 时间。引擎的首编译和 hot reload 都是在循环里逐个调用 Preprocessor.AddFile(...)，这意味着多个暂时打不开的脚本文件会把这段等待线性叠加到主编译路径上。 |
| 根因 | 文件读取失败被实现成“调用线程内同步重试 + sleep backoff”，而不是被提升为可上报的 I/O 失败状态，或迁移到独立的异步/批处理恢复策略。 |
| 影响 | 当磁盘抖动、文件被编辑器/杀毒软件短暂占用、或 hot reload 批量遇到坏路径时，预处理阶段会在真正开始 parse 之前先卡住调用线程。更糟的是，重试结束后这条路径仍会回落到已记录的“Treating file as deleted”分支，所以用户既付出了明显卡顿，又拿不到一个明确的 fail-fast 结果。 |

---

## 分析 ('+$date+')

### 勘误：上一节 42-46 的少量标识符被 PowerShell 转义污染

| 位置 | 勘误 |
|------|------|
| 发现 42 | 文中的 `HasError`、`CompileError`、`ensure` 分别应为 `bHasError`、`bCompileError`、`ensure`；“当前 `Files` 集合”“`CompilingModulesByName`”“`CompileModules()`”“`CompileModule_Types_Stage1()`”“`ImportIntoModule()`”“`CombinedDependencyHash`”均为代码标识符。 |
| 发现 43 | 文中的 `HadCompileErrors`、`CompileError`、`return` 分别应为 `bHadCompileErrors`、`bCompileError`、`return`；“`CheckUsageRestrictions(CompiledModules)`”“`ScriptModule == nullptr`”“`Code.Num() == 0`”“`#restrict usage`”均为代码文本。 |
| 发现 44 | 文中的脚本统计命令应为 `rg --files -g '*.as'`；“`FFile`”“`RawCode`”“`TChunkedArray<FChunk>`”“`ProcessedCode`”“`GeneratedCode`”“`SubmitChunk()`”“`Chunk.Content = File.RawCode.Mid(...)`”“`CondenseFromChunks()`”“`PostProcessRangeBasedFor()`”“`PostProcessLiteralAssets()`”“`NewCode`”均为代码标识符。 |
| 发现 45 | 文中的 “`#restrict`”“`restrict usage`”“`UsageRestrictions`”“`CheckUsageRestrictions`”“`Preprocessor.*`”“`Learning.Runtime.Preprocessor`” 均为原始代码/测试名。 |
| 发现 46 | 文中的时间常量应为 `0.01s` 与 `0.51s`；“`AddFile()`”“`LoadFileToString()`”“`Preprocessor.AddFile(...)`”均为代码标识符。 |

---

## 分析 (2026-04-08 10:58)

### 发现 47：异步读取回调捕获了 `Preprocessor` 内部 `FFile&`，失败路径会在对象销毁后继续写悬挂内存

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Critical |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 141-230；2069-2082；2264-2268；2448-2455 |
| 描述 | `PerformAsynchronousLoads()` 的 `SizeCallback` 和 `ReadCallback` 都以引用方式捕获循环变量 `File`，随后异步线程会继续写 `File.RawCode`、`File.bLoadAsynchronous` 和请求句柄状态。可这个 `File` 实际属于 `FAngelscriptPreprocessor::Files`，而 `FAngelscriptPreprocessor` 在首编译和 hot reload 中都是栈对象。由于 `Preprocess()` 在 225-230 行发起异步读取后没有等待完成就继续返回，调用方只要离开当前作用域，整个 `Preprocessor` 和其中的 `Files` 数组就会被销毁，但晚到的回调仍会落到原来的 `FFile&` 上。当前仓库唯一启用异步读取的入口又正好是 hot reload 删除文件误传 `bTreatAsDeleted` 的路径，所以这不是理论上的 API 风险，而是现有错误路径会直接触发的悬挂回调。 |
| 根因 | 异步 I/O 生命周期被绑定在 `FAngelscriptPreprocessor` 的栈内存上，却没有 completion wait、引用计数或 owner pin；回调只捕获裸引用，调用方也没有为 `Preprocessor` 建立“直到所有请求结束才可析构”的约束。 |
| 影响 | 这条路径不只是前面已记录的 data race，而是明确的 use-after-free：删除脚本或其他异步读取失败场景下，预处理器返回后后台回调仍可能写入已经释放的 `FFile` / `FString` 存储，表现为随机崩溃、堆破坏，或把后续复用到同一内存块的对象内容一并污染。 |

### 发现 48：explicit `import` 排序会在 `Preprocess()` 中途替换整份 `Files` 数组，未完成的异步回调会写回已经失效的旧副本

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` |
| 行号 | 141-188；232-239；63 |
| 描述 | 当前配置默认开启 `bAutomaticImports`，但项目一旦切回 explicit `import` 模式，`Preprocess()` 会在 232-239 行把解析后的 `Files` 整体替换成 `SortedFiles`。与此同时，异步 `SizeCallback` / `ReadCallback` 捕获的仍是最初那批 `Files` 元素的引用。于是回调根本不需要等到 `Preprocessor` 析构，只要在 `Files = SortedFiles` 之后才完成，就已经是在写旧数组里的 `FFile` 对象，而后续 `DetectClasses()`、`AnalyzeClasses()`、`GetModulesToCompile()` 消费的却是新数组副本。 |
| 根因 | explicit `import` 拓扑排序用“复制整个 `FFile` 值对象并替换容器”的方式重排文件，而异步读取层又把容器元素地址暴露给了后台回调；两者之间没有任何生命周期隔离或同步点。 |
| 影响 | 这会把异步读取结果写丢到旧副本里，当前 `Files` 数组继续使用过期的 `RawCode` / `ChunkedCode` 状态，最坏情况下还会在 `Preprocess()` 尚未返回时就触发悬挂写。也就是说，explicit `import` 模式不仅有前面记录的排序性能问题，还和当前异步 I/O 实现发生了直接的内存安全冲突。 |

### 发现 49：fully precompiled 启动时，plugin 脚本会失去按文件名定位模块的能力

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Debugging/AngelscriptDebugServer.cpp` |
| 行号 | 1430-1431；2046-2052；2769-2783；3029-3056；933-966 |
| 描述 | 引擎在 fully precompiled 启动前只把 `AllRootPaths` 设成 project root，随后 `InitialCompile()` 又直接走 `PrecompiledData->GetModulesToCompile()`，不会补扫 plugin script roots。与此同时，`GetModulesToCompile()` 反序列化出来的 `FAngelscriptModuleDesc` 只回填 `ModuleName`、`CodeHash`、`ImportedModules`、`PostInitFunctions` 等字段，没有任何 `Code` section。这样一来，`GetModuleByFilename()` 的第一层“遍历 `Module->Code[*].AbsoluteFilename` 精确匹配”在 fully precompiled 模式下天然失效；第二层 fallback 又只能用 project root 去把绝对路径转模块名，因此来自 content plugin 的脚本文件名永远推不回正确模块。调试器的 `SetBreakpoint` / `ClearBreakpoints` 正是先走这条 `GetModuleByFilenameOrModuleName()` 路径。 |
| 根因 | precompiled 启动路径同时丢掉了两层 filename lookup 依赖的关键数据：一层是 `Module->Code` 里的绝对文件名，另一层是完整的 script root 列表。模块系统仍然暴露 `GetModuleByFilename()`，但 fully precompiled descriptor 并不满足它的输入合同。 |
| 影响 | 在 fully precompiled 且脚本位于 plugin root 的场景下，任何按文件名找模块的消费方都会得到 `null`。当前仓库里已验证的直接影响是调试器 breakpoint bookkeeping 会退化到以文件名字符串做兜底 key，而不是绑定到真实 `ModuleDesc`；这会让 plugin 脚本的断点设置/清理依赖额外的 `ModuleName` 字段才能工作，按文件路径单独操作时行为与 project 脚本不一致。 |

---

## 分析 (2026-04-08 11:20)

### 发现 50：regex 式后处理直接扫描整份 `ProcessedCode`，字符串和注释里的 `for (...)` / `asset ... of ...` 也会被改写

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 4008-4086；4089-4142 |
| 描述 | `PostProcessRangeBasedFor()` 和 `PostProcessLiteralAssets()` 都在 `CondenseFromChunks()` 之后，对整份 `File.ProcessedCode` 直接运行 `FRegexMatcher`。`RangeBasedForPattern` 只靠正则匹配 `for (...)` 形状，`LiteralAssetsPattern` 只靠正则匹配 `asset <Name> of <Type>` 形状，两者都没有任何 lexer state 来跳过 string literal、line comment 或 block comment。命中后它们会把匹配区间替换成新生成的代码，并最终整串回写 `File.ProcessedCode`。 |
| 根因 | 这两个后处理步骤建立在“文本正则替换”等价于语法级变换”的假设上，没有复用 `ParseIntoChunks()` 已经具备的 `bInString` / `bInComment` 词法状态，也没有把变换限定到可验证的 chunk 类型。 |
| 影响 | 只要脚本里出现字符串常量或注释文本，如日志/文档字符串中的 `for (Foo : Bar)`，或注释里的 `asset MyAsset of DataAsset`，后处理器就可能把本来只是文本的数据改写成真实代码片段，直接破坏 string 内容、注释边界和最终编译输入。这属于预处理后期的静默源码篡改，定位难度很高。 |

### 发现 51：range-based-for 正则把 `:` 当成唯一分隔符，带 `Namespace::Type` 的循环变量类型会被错误改写

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Saved/Automation/ASMiscNamespace_4261E86D4B24DA47CD2EE58A6F1B659F.as` |
| 行号 | 3759-3765；4008-4059；1 |
| 描述 | 预处理器前面已经显式识别 `namespace` 语法，仓库里的脚本样本也实际使用了 `MyNamespace::GetValue()`。但 `PostProcessRangeBasedFor()` 的 `RangeBasedForPattern` 把 range-based-for 左侧声明捕获成 `([^:;{]*)`，等价于“在遇到第一个 `:` 前都算变量声明”。这会把 `for (MyNamespace::Type Value : Values)`、`for (const MyNamespace::Type& Value : Values)` 这类合法写法在第一个 namespace 分隔符处截断；随后 `StoreType`/变量名重写逻辑只会基于被截断的前缀生成替换代码。 |
| 根因 | range-based-for 后处理把 `:` 视为唯一可靠的 foreach 分隔符，却没有考虑脚本语言本身已经支持 `::` namespace qualifier。正则层缺少“忽略成对冒号”的语法知识。 |
| 影响 | 一旦脚本在 foreach 左侧使用 namespaced type，预处理器生成的替换代码就会变成语法错误或错误类型声明，导致本来合法的 range-based-for 无法通过编译。这是当前语法能力与后处理器文本重写规则之间的直接不一致。 |

### 发现 52：`literal asset` 只接受纯字母数字 type 名，namespaced type 永远不会进入生成路径

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Saved/Automation/ASMiscNamespace_4261E86D4B24DA47CD2EE58A6F1B659F.as` |
| 行号 | 3759-3765；4089-4133；1 |
| 描述 | `PostProcessLiteralAssets()` 的 `LiteralAssetsPattern` 把 type 捕获写死成 `([A-Za-z0-9]+)`，只允许纯字母数字。可同一套脚本语法已经支持 `namespace`，仓库样本也实际出现了 `MyNamespace::GetValue()`。这意味着一旦用户写 `asset MyConfig of MyNamespace::Settings` 这类与现有语法一致的声明，正则根本不会命中，后续既不会生成 getter，也不会向 `PostInitFunctions` 注册初始化函数。 |
| 根因 | `literal asset` 后处理把 type 名当成“无 namespace、无更复杂限定符的简单标识符”处理，语法约束明显窄于脚本语言其余部分。 |
| 影响 | `asset` 语法能接受的类型集合与脚本语言真实支持的类型引用语法不一致。项目一旦把 settings/data asset 类型放进 namespace，这条声明会静默失效，表现为预处理阶段没有任何报错，但资产 getter 和 post-init 行为都不会生成。 |

---

## 分析 (2026-04-08 11:32)

### 发现 53：`UPROPERTY/UFUNCTION` 允许的配置 flag 在取反分支里会被误判为非法条件

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 47-50；3282-3287；3333-3339；3623-3633 |
| 描述 | 预处理器允许把 `UPROPERTY()`/`UFUNCTION()` 放进“`EDITOR` 或配置里声明的 preprocessor flag”控制的条件块里。构造函数会把 `UAngelscriptSettings::PreprocessorFlags` 逐个注册进 `PreprocessorFlags`。但宏校验阶段判断合法性的代码只接受 `IfDefStack[i].Condition` 原样命中 `PreprocessorFlags` 且值为 `true`。一旦脚本写成 `#if !MYFLAG`，或先 `#if MYFLAG` 后进入 `#else` 分支，条件字符串都会变成 `!MYFLAG`，随后 3625-3632 行就会把本来仍由同一个已注册 flag 控制的分支报成 `Cannot put a UPROPERTY or UFUNCTION inside preprocessor conditions...`。 |
| 根因 | 条件栈把“逻辑表达式文本”直接当成了宏合法性判断键值；`#else` 通过给 `Condition` 加/去 `!` 来翻转分支，却没有在后续校验时把 `!FLAG` 规范化回底层 flag 名。 |
| 影响 | 配置 flag 的否定分支与 `#else` 分支无法承载反射宏，脚本作者只能把 `UPROPERTY/UFUNCTION` 塞进 flag 的正向分支，或复制一份额外 flag 来绕过限制。这不是语言层面的真实约束，而是预处理器条件规范化缺失导致的误报，会直接破坏按 build/config 开关裁剪 API 的脚本写法。 |

---

## 分析 (2026-04-08 11:33)

### 发现 54：`EDITORONLY_DATA` 分支不会进入 `EditorOnlyBlockLines`，编译器会把合法 editor-only 代码误判成越界访问

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp` |
| 行号 | 3092-3125；3625-3642；4353-4356；6920-6987 |
| 描述 | `ParseIntoChunks()` 里负责生成 `EditorOnlyBlockLines` 的 `UpdateEditorBlockLines()` 只看 `IfDef.Condition == "EDITOR"`，完全忽略同一文件其他地方已经等价看待的 `EDITORONLY_DATA`。可在 3625-3642 行，预处理器又明确把 `EDITORONLY_DATA` 当成 editor-only 条件，用它来允许反射宏并给 `FMacro::bEditorOnly` 打标。后续编译阶段 4353-4356 行把 `EditorOnlyBlockLines` 送进 AngelScript builder；builder 的 `CheckEditorOnlyType/Function/Property/Global()` 则只靠这些行号判断“是否处在 editor-only 代码里”，否则直接报 `Cannot use editor-only ... outside of an EDITOR block`。 |
| 根因 | 预处理器内部对 editor-only 条件没有统一模型：宏分析路径把 `EDITOR` 与 `EDITORONLY_DATA` 视为同类，行号跟踪路径却只记录 `EDITOR`。builder 最终消费的是后者，因此两套语义在编译阶段重新分叉。 |
| 影响 | 只要脚本把 editor-only 类型、函数或属性放进 `#if EDITORONLY_DATA` 这样的合法块里，builder 仍可能把这些节点当成“位于普通代码”并抛出错误。结果是模块作者在同一套官方 flag 语义下会得到自相矛盾的行为：反射宏路径认定它是 editor-only，真正的类型/函数使用检查却认定它不是。 |

---

## 分析 (2026-04-08 11:34)

### 发现 55：declared function import 完全游离在模块依赖图之外，provider 变更后 consumer 只会被静默解绑

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.h` |
| 行号 | 1302-1306；2315-2351；3370-3417；3865-3869；4426-4429；4611-4643；4646-4705；5595-5602；255-278 |
| 描述 | 模块描述符只记录 module-level `ImportedModules` 与 `PostInitFunctions`，没有任何字段保存 declared function import。AngelScript builder 在解析 `import` 函数声明时，只调用 `module->AddImportedFunction(...)` 把信息塞进 `bindInformations`，而 `asCModule` 里真正驱动 hot reload、restriction 校验和 recompile avoidance 的依赖容器是独立的 `moduleDependencies`。引擎侧这几条路径也全部只遍历 `ImportedModules` 或 `moduleDependencies`。结果是 declared function import 对模块系统完全不可见。更糟的是，编译结束前 `CheckFunctionImportsForNewModules()` 只检查本轮 `CompiledModules`；swap-in 后 `ResolveAllDeclaredImports()` 虽然会遍历全部 `ActiveModules`，但对未参与本轮编译的 consumer，只会在找不到 provider/function 时静默 `UnbindImportedFunction()`，并显式假设“错误已经由前一个检查报过”。 |
| 根因 | 这套实现把“函数导入绑定”当成编译完成后的附属步骤，而没有把它建成和 module import 一样的一等依赖边。依赖传播、reload 决策、restriction 检查、错误展示分别消费不同容器，declared import 没有进入其中任何一个共享依赖模型。 |
| 影响 | provider 模块改名、移除函数、变更签名或热重载失败时，依赖它的 consumer 不会因为 declared import 被自动纳入依赖重编译/校验集合。最终行为是：consumer 可能继续保留旧脚本模块，只在 `ResolveAllDeclaredImports()` 后失去绑定，而且没有对应诊断落到这个未重编译模块上。这会把导入函数的问题从“编译期确定性失败”退化成“运行时某处调用才暴露的解绑状态”，明显削弱错误恢复与模块系统一致性。 |

---

## 分析 (2026-04-08 11:35)

### 发现 56：declared function import 在创建时就丢失了源码位置，后续所有错误都退化到模块第 1 行

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_builder.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 543-548；5595-5602；1394-1417；4681-4694 |
| 描述 | 普通脚本函数在 builder 里会记录 `scriptSectionIdx` 和 `declaredAt`，因此后续诊断能落到精确行列。相比之下，declared function import 解析完 `moduleName` 后直接调用 `AddImportedFunction(...)`；`asCModule::AddImportedFunction()` 创建 `asCScriptFunction` 时只填名称、签名和 traits，从未写入 `scriptSectionIdx`/`declaredAt`。引擎随后在 `CheckFunctionImportsForNewModules()` 中对“找不到源模块/签名”的错误统一调用 `ScriptCompileError(Module, 1, ...)`。 |
| 根因 | import function 这条语法路径没有沿用普通函数的位置信息建模；解析 AST 时拿到了 `node` 和 `file`，但在导入函数对象落库时把这些 source location 元数据全部丢弃，只能在更高层用固定行号兜底。 |
| 影响 | 一旦 declared import 失配，用户拿到的永远是模块级第 1 行错误，而不是实际 `import` 声明所在位置。对包含多个 imported function 的模块，这会显著放大定位成本，也会让前一条已确认的“consumer 被静默解绑”问题更难追根，因为日志本身缺少最关键的源码锚点。 |

---

## 分析 (2026-04-08 11:45)

### 发现 57：fully precompiled + explicit `import` 模式不会恢复拓扑顺序，consumer 可能在 provider 之前静默编译

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptSettings.h` |
| 行号 | 570-592；2752-2790；2046-2052；3101-3209；4247-4281；61-67 |
| 描述 | 正常预处理路径在 explicit `import` 模式下会先通过 `ProcessImports()` 做拓扑排序，再把有序 `Files` 交给编译器。fully precompiled 启动却直接跳过预处理，改由 `FAngelscriptPrecompiledData::GetModulesToCompile()` 从 `TMap<FString, FAngelscriptPrecompiledModule> Modules` 逐项恢复 `ModuleDesc`，没有任何按 `ImportedModules` 的排序步骤。`CompileModules()` 随后直接 `CompilationQueue.Append(InModules)`，并按 `CurrentCompileList` 原顺序进入 `CompileModule_Types_Stage1()`。在 explicit `import` 路径里，当前模块对同批次依赖的查找只是从 `CompilingModulesByName` 取到 `ModuleDesc`；真正导入时仍要求 `ImportModule->ScriptModule != nullptr`。如果 provider 还没跑到自己的 stage1，这里只会触发一个 `ensure`，然后继续编译当前 consumer。 |
| 根因 | cache 反序列化路径只恢复了 module descriptor 数据，没有恢复“显式 import 需要先按依赖 DAG 排序”这条预处理合同；编译阶段也把“找到 descriptor”与“依赖模块已完成 stage1、可安全导入”混为一谈。 |
| 影响 | 只要项目关闭 `bAutomaticImports` 且启用 fully precompiled 启动，显式 import 依赖就可能以错误顺序进入 stage1。结果不是稳定 fail-fast，而是 consumer 在缺少 provider `ScriptModule` 的情况下继续编译，导入集、`CombinedDependencyHash` 和实际可见符号表之间会出现静默失配；是否暴露成编译错误，取决于后续代码刚好访问了哪些 imported symbol。 |

---

## 分析 (2026-04-08 11:45)

### 发现 58：precompiled cache 以 `ModuleName` 为唯一 key，同名模块会在序列化阶段被静默压扁成一份

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 570-592；2651-2660；2758-2775；2046-2052；3134-3135 |
| 描述 | `FAngelscriptPrecompiledData` 把所有缓存模块存进 `TMap<FString, FAngelscriptPrecompiledModule> Modules`。生成 cache 时，`PrepareToFinalizePrecompiledModules()` 对每个引擎模块执行 `Modules.FindOrAdd(ModuleName).InitFrom(*this, Module)`；同一个 `ModuleName` 后写入的模块会直接覆盖先前条目，没有任何诊断。读取 cache 时，`GetModulesToCompile()` 也只是遍历这个 map 逐项恢复 `ModuleDesc`，因此 fully precompiled 启动天然只能看到“每个 module name 一份 descriptor”。相比之下，实时编译路径至少还会在 `CompilingModulesByName.Contains(Module->ModuleName)` 上打一个 `ensure`。 |
| 根因 | precompiled schema 把 `ModuleName` 当成全局唯一主键，但 cache 生成阶段没有验证这个前提，也没有保存更细粒度的文件/root 身份。于是序列化容器本身就把“多个物理脚本映射到同一 module name”的情况压缩成了 last-writer-wins。 |
| 影响 | 只要工程里出现同名模块，问题就不再只是运行时“行为不确定”，而会在生成 cache 的那一刻被固化成单份数据。fully precompiled 启动随后会静默丢失被覆盖的整份模块描述符，使 live compile 与 cache compile 暴露出两套不同的模块集合；断点、依赖图、类型注册和后续热重载都会基于这份被压扁后的 cache 状态运行。 |

---

## 分析 (2026-04-08 11:58)

### 发现 59：explicit `import` 的语义不会进入 precompiled cache 命中条件，导入目标变化可能继续复用旧编译结果

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | 482-483；289-304；1417-1424；4285-4299 |
| 描述 | explicit `import` 在预处理阶段会先写入 `Module->ImportedModules`，随后立刻用 `ReplaceWithBlank()` 把源码里的 `import` 语句抹成空白；后面的 `CodeHash` 只对 `ProcessedCode` 做 `XXH64`。precompiled cache 虽然会把 `ImportedModules` 单独序列化，但命中条件仍然只检查 `CompiledModule->CodeHash == Module->CodeHash`，命中后直接 `ApplyToModule_Stage1()` 并提前 `return`。这意味着 explicit import 的真实语义并没有进入缓存失效判定，它只以“被抹空后的空白布局”间接影响 `CodeHash`。 |
| 根因 | 这套设计把“导入依赖元数据”和“源码哈希”拆成了两个平行通道，但 cache 复用路径只消费后者。`ProcessImports()` 先擦掉 import 文本、`InitFrom()` 再单独保存 `ImportedModules`，最终却没有在 cache 命中时把两者联合校验。 |
| 影响 | 只要脚本改动仅发生在 import 目标或顺序，而改动后的空白占位仍与旧文件形成相同 `CodeHash`，系统就会继续加载旧的 precompiled stage1/2/3 结果，尽管当前 `ImportedModules` 已经不同。结果是模块描述符上的依赖集合已更新，真正装进 `ScriptModule` 的类型/函数布局却可能仍来自旧依赖环境，形成缓存命中与模块依赖不一致的静默失配。 |

### 发现 60：模块签名把 `ImportedModules` 当成无序集合，但实际符号解析按导入顺序 first-match，顺序变化不会反映到依赖哈希

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ThirdParty/angelscript/source/as_module.cpp` |
| 行号 | 482-497；4264-4281；1617-1634；1641-1667 |
| 描述 | 预处理器会按脚本里出现的顺序把 module import 逐条追加到 `Module->ImportedModules`；编译阶段也按这个顺序调用 `ImportIntoModule()`。AngelScript 的 `asCModule::ImportModule()` 会保持这个顺序，并把依赖拍平成 `importedModules` 列表；后续 `GetType()` 查找外部类型时按 `importedModules` 顺序遍历，命中第一个就返回。也就是说，导入顺序本身就是可观察语义，多个上游模块暴露同名类型时会出现 first-match 行为。与此相对，`CombinedDependencyHash` 只是把每个依赖模块的 hash 逐次 `xor` 进去，天然对顺序不敏感。 |
| 根因 | 模块系统同时使用了两套不一致的依赖模型：可执行路径把 imports 当成有序列表，签名路径却把它们压扁成了无序异或集合。两边没有统一的“顺序是否构成语义”的合同。 |
| 影响 | 当脚本只调整 import 顺序时，当前模块实际可见的类型/函数绑定可能变化，但 `CombinedDependencyHash`、基于它生成的 function ID，以及后续依赖签名比较都可能保持不变。结果是模块系统会把“导入优先级已改变”的脚本错误地视为“依赖未变化”，给 JIT、缓存复用和热重载一致性留下隐蔽错判。 |

### 发现 61：命中 precompiled cache 的模块不会把 `CombinedDependencyHash` 写回 `ScriptModule`，后续 function ID 退化为缺失依赖签名的版本

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/AngelscriptStaticJIT.cpp` |
| 行号 | 4262-4280；4290-4299；4348-4350；2708-2713；3212-3213 |
| 描述 | `CompileModule_Types_Stage1()` 会先计算当前模块的 `CombinedDependencyHash`，但一旦命中 precompiled cache，就在 `ApplyToModule_Stage1()` 之后直接 `return`，完全跳过后面的 `ScriptModule->SetUserData((void*)(size_t)Module->CombinedDependencyHash, 0)`。而 `FAngelscriptPrecompiledData::CreateFunctionId()` 恰好把 `ScriptModule->GetUserData()` 作为“模块签名”混入稳定 function ID；`FAngelscriptStaticJIT::GenerateCppCode()` 又直接使用这套 function ID。结果是：从 precompiled cache 载入的模块虽然内存里已经算出了 `CombinedDependencyHash`，但 `ScriptModule` 上看到的仍是空 user data。 |
| 根因 | cache-hit 早退路径和常规编译路径没有共用一段“完成模块签名回填”的尾处理逻辑，导致 `SetUserData()` 只存在于 non-precompiled 分支。 |
| 影响 | 只要模块是从 precompiled code 命中的，后续基于 `CreateFunctionId()` 的静态 JIT 生成和预编译引用映射都会丢掉依赖签名这部分输入，把“同模块名 + 同函数声明”的函数过度视为同一身份。这样一来，依赖变化后的 ID 稳定性、JIT 产物复用和跨轮缓存关联都会偏离常规编译路径，形成 only-precompiled 才会出现的模块签名漂移。 |

### 发现 62：`CreateFunctionId()` 只消费 `CombinedDependencyHash` 的低 32 位，高位熵被永久丢弃

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT/PrecompiledData.cpp` |
| 行号 | 1289-1291；4350；2708-2725 |
| 描述 | 模块描述符把依赖签名定义成 `int64 CombinedDependencyHash`，并在编译结束时通过 `SetUserData((void*)(size_t)Module->CombinedDependencyHash, 0)` 挂到 `ScriptModule` 上。可 `CreateFunctionId()` 读回这份签名时，直接执行 `HashCombine(Id, (uint32)(size_t)ScriptModule->GetUserData())`，也就是无条件只取低 32 位。函数注释还明确说这里要“Generate a consistent Guid for the function”。因此当前实现并不是把 64 位哈希再做一次折叠混合，而是彻底忽略高 32 位。 |
| 根因 | 模块签名用的是 64 位 `CombinedDependencyHash`，function ID 通道却沿用了 32 位 `HashCombine` 输入，没有把高位一起混入。类型定义和消费路径之间缺少一致的位宽合同。 |
| 影响 | 依赖变化如果只落在 `CombinedDependencyHash` 的高 32 位，`CreateFunctionId()` 就完全感知不到；不同依赖图也会更容易落到相同的 function ID 前缀，再由 `while (ProcessedIdToFunction.Contains(Id)) ++Id` 走 insertion-order 兜底。结果是本应稳定反映模块依赖变化的 function ID 会对一半签名熵视而不见，削弱 StaticJIT / precompiled 引用表的跨轮确定性。 |

---

## 分析 (2026-04-08 12:18)

### 发现 63：删除或读失败脚本会以“同名空模块”继续留在模块图里，`import` 不再对缺失模块 fail-fast

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | 105-107，135-137，289-304，463-483；2907-2938，3175-3195，4247-4280；1856-1866 |
| 描述 | 预处理器把 `bTreatAsDeleted` 和同步读取失败都收敛到同一条空源码路径：`File.RawCode = ""` 或 warning 后继续处理。即便 `ProcessedCode` 为空，`Preprocess()` 仍会给该文件生成带原始 `RelativeFilename/AbsoluteFilename` 的 `FCodeSection`，保留原有 `ModuleName`。随后 `ProcessImports()` 只按模块名在当前 `Files` 集合里查找目标模块；只要这个空模块描述符存在，consumer 的 `import` 就会被视为“已解析”。编译阶段 `CompileModules()` 也只要求 `FoundModule.IsValid()`，`CompileModule_Types_Stage1()` 还会为这个空模块创建真实 `ScriptModule`，最后 `SwapInModules()` 无条件把它重新写回 `ActiveModules`。 |
| 根因 | 模块系统把“模块存在”建模成“是否有一个同名 `FAngelscriptModuleDesc` / `ScriptModule` 条目”，而不是“源码是否成功读取并产出了有效内容”。预处理、编译和 swap-in 三层之间没有“空源码代表删除模块，应从模块图移除”的一致合同。 |
| 影响 | 这会把“provider 脚本已删除/不可读”的语义扭曲成“provider 仍然存在，只是一个空模块”。结果是显式 `import` 不再稳定命中 `could not find module ... to import`，而是可能变成后续缺失符号错误，或在 consumer 根本没直接用到导出符号时静默通过。更糟的是，`ClassGenerator` 只对 removed class 升级 `FullReloadRequired`；纯 globals / enums / delegates 模块删除时，没有对应的移除判定来阻止这个空模块被正常 swap-in。 |

---

## 分析 (2026-04-08 12:19)

### 发现 64：现有自动化把“空模块是合法 soft reload”写成了测试合同，会反向保护前述空模块语义

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/ClassGenerator/ClassGeneratorTests.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | 41-55；1856-1866 |
| 描述 | `FAngelscriptClassGeneratorEmptyModuleSetupTest` 直接手工构造一个只有 `ModuleName + ScriptModule` 的空 `FAngelscriptModuleDesc`，然后断言 `Generator.Setup()` 必须返回 `SoftReload`，且 `WantsFullReload/NeedsFullReload` 都为 false。与此同时，运行时代码里唯一会把“旧模块里有东西，新模块里没有东西”升级成 `FullReloadRequired` 的检查只覆盖 removed class。也就是说，当前自动化不是缺少这条边界，而是明确把“空模块可正常进入 soft reload 分析”设成了预期行为。 |
| 根因 | 测试设计把“空模块”当成一个中性的 class-generator scaffold，而没有把它与预处理删除路径、读取失败路径、或 import/module graph 中的缺失 provider 语义联系起来验证。结果是 class generator 的最小 happy path 与模块系统的错误恢复语义发生了脱节。 |
| 影响 | 这会反向固化前一条已验证的问题：一旦有人尝试把“空源码模块应视为删除并从模块图移除”落到代码上，当前测试会首先报红，CI 反而会鼓励继续保留空模块 stub 语义。对迭代修复来说，这不是单纯的覆盖缺口，而是一个会把错误恢复行为锁死的错误测试合同。 |

---

## 分析 (2026-04-08 12:21)

### 发现 65：reload 分析只显式处理 `removed class`，`enum/delegate` 被整个模块删除时不会进入对称的移除路径

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` |
| 行号 | 1575-1627，1771-1807，1856-1866，2145-2229，2380-2388 |
| 描述 | `ClassGenerator` 对枚举和委托的分析都是“只看新模块里还存在的条目”：`InitEnums()` 只遍历 `ScriptModule->GetEnumCount()`，`InitDelegates()` 只遍历 `NewModule->Delegates`，然后最多把同名旧描述符挂到 `OldEnum/OldDelegate`。整个 `Setup()` 阶段里，唯一显式遍历“旧模块里有、新模块里没有”的分支只有 1856-1866 行的 removed class 检查。后续 `PerformReload()` 也只处理 `ModuleData.Enums`、`ModuleData.Delegates` 和 `RemovedClasses`，并且只有 `RemovedClasses` 会走 `FullReloadRemoveClass()` / `CleanupRemovedClass()`。 |
| 根因 | reload 差异模型是围绕 class/struct ABI 变化建立的，类之外的 reflected symbol 没有建立对称的 removed-item 数据结构与清理流程。结果是 `FAngelscriptModuleDesc` 在“新增/修改 enum、delegate”时有比较逻辑，在“整个条目消失”时却没有对应状态机。 |
| 影响 | 一旦模块因为删除文件、读失败降级为空模块、或正常重构而移除了 `enum` / `delegate`，reload 分析不会像 removed class 那样要求 full reload，也不会触发等价的清理路径。对纯 enum/delegate 模块来说，这意味着整模块可以沿 soft-reload/正常 swap-in 继续推进，但旧的 reflected enum/delegate 对象没有经过对称撤销流程，模块描述符与 Unreal 侧类型状态容易发生漂移。 |

---

## 分析 (2026-04-08 12:34)

### 发现 66：已命中分支后的 `#elif` 仍会求值，互斥配置里的未知 flag 也会把预处理打成失败

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3290-3315，4328-4344 |
| 描述 | `ParseIntoChunks()` 处理 `#elif` 时，会先调用 `ParsePreProc(File, LineNumber, PreProc)`，之后才检查 `IfDefStack.Last().bAnyBranchTaken`。这意味着即使前面的 `#if`/`#elif` 分支已经为 true，后面本应不可达的 `#elif SOME_OTHER_FLAG` 仍然会立即求值；一旦 `SOME_OTHER_FLAG` 没有注册，`ParsePreProc()` 就会直接报 `Invalid preprocessor condition` 并把 `bHasError` 置为 true。 |
| 根因 | `#elif` 的实现把“是否需要计算当前分支条件”和“当前分支条件的求值”写反了顺序。状态机只在求值完成后才根据 `bAnyBranchTaken` 决定是否丢弃本分支结果，没有先对不可达 `#elif` 做 short-circuit。 |
| 影响 | 互斥平台分支无法安全表达。例如 `#if EDITOR ... #elif CONSOLE_ONLY ... #endif` 在 `EDITOR` 已经为 true 的上下文里，仍会因为 `CONSOLE_ONLY` 未注册而失败。这样一来，预处理器既不能正确跳过不可达分支，也破坏了错误恢复路径的基本 fail-closed 语义。 |

### 发现 67：directive lexer 只接受关键字后的字面量空格，tab 分隔的 `#if/#ifdef/#ifndef/#elif/#restrict` 会被整体漏判

| 项目 | 内容 |
|------|------|
| 维度 | C |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 3259-3293，3363-3382 |
| 描述 | 预处理器识别 `#ifdef/#ifndef/#if/#elif` 和 `#restrict usage allow/disallow` 时，匹配条件分别是 `"#ifdef "`、`"#ifndef "`、`"#if "`、`"#elif "`、`"#restrict usage allow "`、`"#restrict usage disallow "` 这些带固定空格的字面量前缀。后续的 `ReadIdentifier()` / `ReadUntilWhitespace()` 本身能处理 `\t`，但只有在前缀已经匹配成功后才会被调用。因此像 `#if\tEDITOR`、`#ifdef\tMYFLAG`、`#restrict usage allow\tGame.*` 这样的常见“tab 作为空白”写法，会完全绕过 directive 分支，原始 `#...` 文本直接留在源码里。 |
| 根因 | directive 词法分析把“关键字结束”硬编码成了单个 `' '` 字符，而不是复用 `IsWhitespace()` 或更通用的 token 边界判断。结果是同一套预处理器对空白字符的接受范围前后不一致。 |
| 影响 | 一旦脚本作者或格式化工具把 directive 改成 tab 分隔，预处理行为会从“条件裁剪/记录模块元数据”退化成“把整条 `#...` 文本送进后续编译”。这既会让条件分支失效，也会让 `#restrict usage` 这类模块元数据指令静默失去作用，属于明确的语法兼容边界缺陷。 |

### 发现 68：`Editor/Dev/Examples` 目录过滤和 `isEditorOnlyModule` 判定都大小写敏感，小写目录会绕过环境隔离

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 86-89；1973-1985；2001-2013；4353-4356 |
| 描述 | 模块名规范化只保留原始路径大小写：`FilenameToModuleName()` 只是把 `.as` 删掉并把 `/` 改成 `.`。与此同时，脚本扫描阶段跳过 development/editor 目录时，只用 `FoundDirectory == "Examples" / "Dev" / "Editor"` 做精确比较；编译阶段给 builder 打 `isEditorOnlyModule` 标记时，也只接受 `ModuleName.StartsWith("Editor.")` 或 `Contains(".Editor.")`。因此若脚本树中实际目录名是 `editor/`、`dev/`、`examples/` 这类大小写不同但在 Windows 上完全合法的名字，它们既不会在非目标环境下被过滤掉，也不会被标成 editor-only module。 |
| 根因 | 路径发现、模块名生成和 editor-only 标记消费的是同一份目录语义，但三处实现都采用了 case-sensitive 字符串比较，没有像同文件里的 `MakePathRelativeTo_IgnoreCase()` / `Equals(..., IgnoreCase)` 那样明确考虑大小写不敏感文件系统。 |
| 影响 | 在 Windows 这类 case-insensitive 文件系统上，单纯的目录大小写差异就可能把 editor/dev/example 脚本错误地带进运行时模块图，还会让 builder 看不到 `isEditorOnlyModule` 标志。结果是脚本可见性、条件编译语义和模块环境分类会同时漂移，属于模块系统边界与路径规范化不一致。 |

### 发现 69：`Preprocessor` 的路径转模块名不归一化 `\`，与运行时 filename lookup 的反斜杠兼容语义脱节

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.h`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 行号 | 15；86-89；395-407 |
| 描述 | `FAngelscriptPreprocessor::AddFile()` 的公开接口直接接收 `ScriptRelativePath` 字符串，但 `FilenameToModuleName()` 只做两件事：删除 `.as`，再把 `/` 改成 `.`。这里完全没有处理 Windows 风格的 `\`。相比之下，现有 `AngelscriptFileSystemTests` 明确验证了 runtime filename lookup 会把绝对路径中的 `/` 与 `\` 视为等价输入。也就是说，运行时“按文件找模块”已经接受双分隔符，预处理阶段“按路径生成模块名”却没有相同的归一化合同。 |
| 根因 | 路径兼容逻辑被拆散在不同层：lookup 路径有单独的 slash-normalization 语义，preprocessor 的 path-to-module 语义却只覆盖 `/`，没有统一的标准化入口。 |
| 影响 | 对任何直接调用 `AddFile()` 并传入 Windows-style 相对路径的调用方，`ModuleName` 都会保留 `\`，进而与手写 `import Foo.Bar;`、`StartsWith("Editor.")`、以及其他基于 `.` 分段的模块规则失配。结果是模块描述符与后续模块系统消费端看到的命名空间结构并不一致。 |

### 发现 70：现有自动化没有覆盖 script-side directive 语法边界，也没有覆盖目录大小写导致的模块分类漂移

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp` |
| 行号 | 69-219；95-220；389-407 |
| 描述 | 当前 `Preprocessor` 自动化只覆盖 3 类 fixture：basic parse、macro detection、手写 `import`；learning trace 也只是同一路径的 import+macro happy path。对 `Plugins/Angelscript/Source/AngelscriptTest/Preprocessor` 与 learning trace 文件做源码检索，除测试框架自身的 `#if WITH_DEV_AUTOMATION_TESTS` 外，没有任何 script fixture 命中 `#if/#elif/#ifdef/#ifndef/#restrict`。`AngelscriptFileSystemTests` 的 normalization 用例也只验证绝对路径 lookup 能接受反斜杠，没有任何预处理入口或目录 casing 场景。 |
| 根因 | 现有测试把 preprocessor 当成“正常脚本转 module desc”的线性 happy-path 组件在验证，没有把 directive 词法边界和目录命名规范当成模块系统合同的一部分。 |
| 影响 | 本轮新增的 66-69 号问题都缺少直接回归保护：`#elif` short-circuit、tab 分隔 directive、`editor/dev/examples` 目录大小写，以及 `AddFile()` 的反斜杠路径输入，都不会在 CI 中自动暴露。后续即使有人调整 import/path 代码，也很容易在这些边界上重新退化。 |

---

## 分析 (2026-04-08 12:48)

### 发现 71：脚本发现顺序未做稳定化，重复模块冲突的“赢家”会随文件系统枚举顺序漂移

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 1944-1989；2075-2082；463-470；496-497；2938；3134-3135 |
| 描述 | `FindScriptFiles()` 直接按 `IFileManager::FindFiles()` 返回顺序把 `LocalFiles` 追加到 `OutFilenames`，对子目录还先转成 `TSet<FString>(LocalDirs)` 再递归，整个过程没有任何稳定排序。初始编译阶段又按这个原始顺序逐个 `Preprocessor.AddFile(...)`。后续 explicit `import` 排序里，`ProcessImports()` 对重名模块只取 `Files` 中第一个命中项；编译和 swap-in 阶段则分别用 `CompilingModulesByName.Add(Module->ModuleName, Module)` 与 `ActiveModules.Add(InternalModuleName, Module)` 让后写入项覆盖前写入项。也就是说，同一个 duplicate module collision，预处理阶段选的是“当前枚举顺序里的第一个”，编译/激活阶段保留下来的却是“当前枚举顺序里的最后一个”，而这个顺序本身并不稳定。 |
| 根因 | 模块系统把文件发现顺序当成了隐式输入，但扫描阶段既没有对文件/目录做 canonical sort，也没有在进入 `ProcessImports()`、`CompilingModulesByName`、`ActiveModules` 之前显式拒绝或规范化冲突模块名。 |
| 影响 | 这会把已有的重复模块名缺陷升级成 nondeterministic 行为：不同机器、不同平台，甚至同平台不同枚举顺序下，某个重名模块到底由哪份脚本提供并不稳定。结果不仅是诊断会漂移，import 解析命中的 provider、最终留在 `ActiveModules` 里的实现，以及热重载时被追踪的模块身份都可能随扫描顺序改变。 |

### 发现 72：现有 discovery/preprocessor 自动化主动忽略文件顺序，重复模块冲突没有任何回归保护

| 项目 | 内容 |
|------|------|
| 维度 | D |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptTest/FileSystem/AngelscriptFileSystemTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Preprocessor/AngelscriptPreprocessorTests.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Learning/Runtime/AngelscriptLearningPreprocessorTraceTests.cpp` |
| 行号 | 258-273；302-310；90-98；194-205；137-149 |
| 描述 | 当前覆盖 `FindAllScriptFilenames()` 的 discovery 测试在拿到 `Files` 后，会把 `RelativePath` 全部塞进 `TSet<FString> FoundRelativePaths` 再断言集合成员，完全不检查返回顺序。另一方面，`Preprocessor` 测试与 learning trace 都是手工按固定顺序调用 `Preprocessor.AddFile(...)`，且 fixture 只包含唯一模块名的 happy path。也就是说，测试既没有构造 duplicate module name，也没有验证 `FindScriptFiles()` 的输出顺序是否稳定，更没有覆盖“扫描顺序变化时预处理/编译选中的冲突文件是否一致”。 |
| 根因 | 现有自动化把文件发现当成“集合语义”，把预处理当成“给定固定输入顺序后的纯函数”来验证，没有把文件系统枚举顺序和模块冲突解析视为模块系统合同的一部分。 |
| 影响 | 发现 71 这种顺序相关缺陷在 CI 中基本不可见：只要发现的文件集合不变，现有 discovery 测试就会继续通过；只要手工 `AddFile()` 顺序没变，preprocessor 测试也不会暴露冲突 winner 漂移。后续即使有人修改文件扫描或模块查找代码，也很难第一时间发现 determinism 已经退化。 |

---

## 分析 (2026-04-08 12:56)

### 发现 73：显式 `import` 解析对每条依赖都线性扫描全部 `Files`，依赖图排序成本随脚本数和 import 数相乘增长

| 项目 | 内容 |
|------|------|
| 维度 | E |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` |
| 行号 | 439-482 |
| 描述 | `ProcessImports()` 为每一条 `FImport` 都重新遍历整个 `Files` 数组去找 `ImportDesc.ModuleName` 对应的 `ProcessingModule`，找到后再递归解析。这里没有任何 `ModuleName -> FFile` 索引缓存，因此显式 import 模式下的依赖排序成本至少是 `O(文件数 × import 数)`。当前工程执行 `rg --files -g '*.as'` 的结果是 325 个脚本文件，这意味着只要脚本树切回手写 import 模式，预处理阶段就会把一次依赖图遍历放大成大量重复字符串比较。 |
| 根因 | `ProcessImports()` 同时承担“拓扑排序”和“按模块名查文件”两件事，但实现上直接复用 `TArray<FFile> Files` 做顺序扫描，没有在预处理开始前建立稳定的查找表。 |
| 影响 | 随着模块数和 import 数增长，显式 import 模式的预处理耗时会先于真正的编译工作膨胀，尤其是在频繁 hot reload 或测试构造大量小模块时更明显。这个瓶颈还会叠加 finding 5 中的整对象复制成本，使 import-heavy 工程的预处理阶段更早触顶。 |

### 发现 74：`namespace` 关键字一出现就直接压栈，却从不校验后续是否真的进入 `{}` 作用域

| 项目 | 内容 |
|------|------|
| 维度 | A |
| 严重度 | High |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptTest/Angelscript/AngelscriptMiscTests.cpp` |
| 行号 | 3159-3165，3759-3765，3821-3824，3928-3933；22-26 |
| 描述 | 仓库现有 namespace 用例明确采用 `namespace MyNamespace { ... }` 这种带花括号的语法。但预处理器在扫到 `namespace` 关键字时，只要后面跟空白就立即 `NamespaceStack.Add(ReadIdentifier(...))`，没有检查后面是否真的出现 `{`。之后 `SubmitChunk()` 会无条件把 `NamespaceStack` 拼进 `Chunk.Namespace`，而弹栈仅发生在 `}` 命中且 `IsTopLevelScope()` 为 true 时；文件结束处也只校验 `IfDefStack`，完全不校验 `NamespaceStack`。结果是像 `namespace Foo` 这种缺少 `{` 的语法错误不会在预处理阶段报错，后续所有 top-level chunk 都会被静默打上 `Foo` 命名空间。 |
| 根因 | namespace 解析被实现成“看到关键字就修改状态机”，而不是“确认进入 namespace block 后再提交状态”；同时 EOF 校验遗漏了 namespace 栈的平衡性检查。 |
| 影响 | 一个本应立即报语法错的缺失 `{` 会被降级成静默语义漂移：全文件后续声明、生成代码和类/函数命名空间都会被错误地归到伪造 namespace 下，最终只能在更后面的编译阶段以误导性的 symbol 解析错误暴露。这个问题直接削弱了预处理器的错误恢复能力。 |

### 发现 75：`#restrict usage` 的 pattern 读取不识别 `//` 注释，注释文本会被写进 `UsageRestrictions`

| 项目 | 内容 |
|------|------|
| 维度 | B |
| 严重度 | Medium |
| 文件 | `Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp`；`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` |
| 行号 | 3363-3389，4313-4325，4348-4355；4579-4588 |
| 描述 | `#restrict usage allow/disallow` 通过 `ReadUntilWhitespace()` 读取 pattern，但这个 helper 只把 `\\n`、`\\t` 和空格视为终止符，不识别 `//` 注释起始。于是像 `#restrict usage allow Game.*//temporary` 这样的合法单行注释写法，会把 `Game.*//temporary` 整体存进 `Restriction.Pattern`。紧接着 `KillRawLine()` 却在遇到 `/` 时就停止抹行，因此源码里保留下来的只是 `//temporary` 注释，而模块描述符里已经写入了带注释尾巴的错误 wildcard。后续 `CheckUsageRestrictions()` 又直接用这个字符串做 `MatchesWildcard()`。 |
| 根因 | restriction 指令的“读取 pattern”与“清理原始源码”使用了两套不一致的词法边界：前者不识别 `/`，后者把 `/` 当成 comment 起点。 |
| 影响 | 这会把一条本应正常生效的模块限制静默变成永远匹配不到的脏 pattern，或者把 allow/disallow 语义扭曲成错误的目标集合。由于注释文本不会体现在最终诊断里，用户看到的只会是 restriction mysteriously 不生效，而不是明确的预处理语法错误。 |
