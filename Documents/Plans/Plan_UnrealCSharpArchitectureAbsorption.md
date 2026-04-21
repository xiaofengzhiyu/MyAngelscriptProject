# UnrealCSharp 架构与类生成机制对比吸收计划

## 背景与目标

当前仓库的主目标是把 `Plugins/Angelscript` 整理为可复用、可验证、可维护的 Unreal Angelscript 插件；`UnrealCSharp` 则是一个成熟的横向参考，用来观察另一套 Unreal 脚本插件如何拆模块、组织生成链路、衔接编辑器工作流，并把“动态类”与“静态生成代码”分成可维护的工程边界。

当前插件已经具备较完整的运行时、编辑器与测试骨架：

- `Plugins/Angelscript/Angelscript.uplugin` 当前只暴露 `AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest` 三个模块。
- `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` 会在编辑器 / commandlet 下进入 `FAngelscriptEngine::Initialize()`，由 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 内收预处理、编译、类生成、热重载与调试服务。
- `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp` 已经负责基于脚本描述符生成 `UASClass` / `UASStruct`，并在 soft reload / full reload 之间做切换。
- `Plugins/Angelscript/Source/AngelscriptEditor/Core/AngelscriptEditorModule.cpp` 已经提供 `GenerateNativeBinds()`，可以扫描 UClass 并生成 `ASRuntimeBind_*` / `ASEditorBind_*` 模块。

`UnrealCSharp` 的价值不是“把 Mono / C# 工具链原样搬进当前插件”，而是提供一套更清晰的工程参考：

- 运行时核心、编辑器入口、静态代码生成、编译、跨版本兼容、动态类生成都被拆成独立职责，而不是全部收口到单一 runtime / editor 模块里。
- 类生成被分成 **静态代理代码生成轨** 与 **动态 UType 生成轨** 两条链路，并通过 `CodeAnalysis`、依赖图与监听器把增量更新闭环做清楚。
- 编辑器端生成流水线是显式阶段化的，而不是隐含在若干工具函数和热重载分支里。

本文目标不是立即改代码，而是形成一份可执行的吸收计划，明确：

1. `UnrealCSharp` 哪些架构与功能值得当前插件吸收；
2. 哪些点只适合当作参考，不应因为语言 / 运行时差异被误抄；
3. 后续如果要拆实现任务，应如何围绕当前插件目标拆成可验证、可逐步落地的 Phase。

## 范围与边界

- 对比对象：当前 `Plugins/Angelscript` 插件与 `Reference/UnrealCSharp`。
- 纳入范围：模块拆分、生成流水线、类生成机制、反射/元数据抽象、文件监听与增量更新闭环、编辑器入口、验证与工作流可见性。
- 排除范围：Mono / .NET 运行时本身、C# 语法级 Source Generator / Weaver 细节、与当前插件目标无关的宿主项目逻辑。
- 约束：所有结论都必须回到“当前插件如何更像一个可维护插件工程”这个目标，而不是为了追求表面 parity 去照搬 `UnrealCSharp` 的语言工具链。
- 额外约束：如果某项能力天然依赖 C# 编译器、Roslyn、Mono 域管理、GCHandle 或程序集装载，则只能记为“参考思路”，不能直接记为当前插件待实现功能。

## 当前事实状态快照

### 当前插件快照

- 模块结构：`AngelscriptRuntime`、`AngelscriptEditor`、`AngelscriptTest`。
- 运行时入口：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptRuntimeModule.cpp` 中的 `StartupModule()` 与 `InitializeAngelscript()`。
- 运行时总控：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 中的 `FAngelscriptEngine::Initialize()`、`InitialCompile()`、`BindScriptTypes()`、`PerformHotReload()` 等流程。
- 类生成核心：`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.h` / `.cpp` 与 `ASClass.h` / `.cpp`、`ASStruct.h` / `.cpp`。
- 预处理与脚本描述符：`Plugins/Angelscript/Source/AngelscriptRuntime/Preprocessor/AngelscriptPreprocessor.cpp` 负责把 `.as` 文件整理成 `FAngelscriptModuleDesc`、`FAngelscriptClassDesc`、`FAngelscriptEnumDesc`、`FAngelscriptDelegateDesc` 等描述符。
- 绑定体系：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptBinds.h` 与 `Plugins/Angelscript/Source/AngelscriptRuntime/Binds/` 下大量手写绑定文件；编辑器侧通过 `GenerateNativeBinds()` 自动补生成模块。
- 热重载：`Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp` 内部使用文件扫描 / 轮询线程 / reload requirement 传播，类生成器内部再决定 `SoftReload`、`FullReloadSuggested`、`FullReloadRequired`。

### UnrealCSharp 快照

- 模块结构：`UnrealCSharp`、`UnrealCSharpEditor`、`ScriptCodeGenerator`、`Compiler`、`UnrealCSharpCore`、`CrossVersion`、`SourceCodeGenerator`，定义见 `Reference/UnrealCSharp/UnrealCSharp.uplugin`。
- 运行时入口：`Reference/UnrealCSharp/Source/UnrealCSharp/Private/UnrealCSharp.cpp` 在 `OnUnrealCSharpCoreModuleActive()` 里触发 `FDynamicGenerator::Generator()`。
- 运行时核心：`Reference/UnrealCSharp/Source/UnrealCSharpCore/`，其中 `Dynamic/`、`Reflection/`、`Bridge/`、`Domain/`、`Binding/` 是最关键几层。
- 编辑器总控：`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp` 中的 `Generator()` 明确分为 `Solution Generator -> Code Analysis -> Code Analysis Generator -> Class / Struct / Enum / Asset Generator -> Binding Generator -> Compiler`。
- 编辑器监听：`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp` 通过 `DirectoryWatcher`、`AssetRegistry`、PIE 生命周期与编译回调驱动增量更新。
- 静态代理/绑定生成：`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp`、`FClassGenerator.cpp`、`FStructGenerator.cpp`、`FEnumGenerator.cpp`、`FAssetGenerator.cpp`、`FBindingClassGenerator.cpp`、`FBindingEnumGenerator.cpp`。
- 动态类生成：`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGenerator.cpp`、`FDynamicClassGenerator.cpp`、`FDynamicStructGenerator.cpp`、`FDynamicEnumGenerator.cpp`、`FDynamicInterfaceGenerator.cpp`、`FDynamicDependencyGraph.cpp`。
- 反射元数据入口：`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Reflection/FReflectionRegistry.cpp` 与 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Reflection/FClassReflection.h`。
- 脚本侧生成辅助：`Reference/UnrealCSharp/Script/SourceGenerator/UnrealTypeSourceGenerator.cs` 与 `Reference/UnrealCSharp/Script/CodeAnalysis/CodeAnalysis.cs`。

## UnrealCSharp 架构拆解

### 模块分层

| 模块 | 类型 | 关键职责 | 关键证据 |
| --- | --- | --- | --- |
| `UnrealCSharp` | Runtime | 运行时入口、与 core 激活联动、非编辑器下触发动态生成 | `Reference/UnrealCSharp/Source/UnrealCSharp/Private/UnrealCSharp.cpp` |
| `UnrealCSharpCore` | Runtime | Mono 反射桥、动态类生成、反射注册、类型桥接、域管理 | `Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Reflection/FReflectionRegistry.h`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Public/Dynamic/FDynamicGenerator.h` |
| `UnrealCSharpEditor` | Editor | 工具栏、内容浏览器、设置、总生成流水线、监听器闭环 | `Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp`、`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp` |
| `ScriptCodeGenerator` | Editor | 通过 UE 反射扫描生成 C# 代理类型、绑定包装与工程文件 | `Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp`、`FClassGenerator.cpp`、`FBindingClassGenerator.cpp` |
| `Compiler` | Editor | 负责与 C# 编译流程衔接 | `Reference/UnrealCSharp/Source/Compiler/Private/Compiler.cpp` |
| `CrossVersion` | Runtime | 收口不同 UE 版本差异 | `Reference/UnrealCSharp/Source/CrossVersion/Private/CrossVersion.cpp` |
| `SourceCodeGenerator` | Program | 独立程序式生成入口；当前本地快照目录存在但未展开可读源码，需要后续确认其在官方仓库中的实际职责 | `Reference/UnrealCSharp/UnrealCSharp.uplugin`、`Reference/UnrealCSharp/Source/` |

这个拆分最值得当前插件关注的点不是“模块越多越好”，而是 **把运行时生成、编辑器协调、静态代码输出、编译/重载协同拆成可单独维护的职责边界**。

### 编辑器总控流水线

`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp` 的 `Generator()` 是整套工程最清晰的架构信号：

1. `FSolutionGenerator::Generator()` 先保证工程结构与项目文件存在；
2. `FCodeAnalysis::CodeAnalysis()` 先做代码扫描，产出动态类 / Override 相关索引；
3. `FDynamicGenerator::CodeAnalysisGenerator()` 根据代码分析结果补建动态类型映射；
4. `FGeneratorCore::BeginGenerator()` 后顺序跑 `FClassGenerator`、`FStructGenerator`、`FEnumGenerator`、`FAssetGenerator`；
5. 接着运行 `FBindingClassGenerator`、`FBindingEnumGenerator`；
6. 最后 `FCSharpCompiler::Get().ImmediatelyCompile()` 完成编译，并用 `OnBeginGenerator` / `OnEndGenerator` 把外界同步起来。

这条流水线的另一个关键点是“产物闭环”而不是单纯“产物生成”：`FSolutionGenerator` 会生成 `.sln` / `.csproj` / 相关工程配置，`FGeneratorCore` 会维护 `GeneratorFiles` 集合并清理本轮未再产出的遗留文件，整个编辑器总控因此不只是一个按钮，而是一套带产物生命周期管理的工程化生成入口。

当前插件与之对应的能力并不是缺失，而是比较“内收”：脚本预处理、类生成、热重载、绑定调用分散在 runtime 和 editor 里，没有一个对外显式表达的统一阶段图。这是吸收 `UnrealCSharp` 最直接、最低风险的一类收益。

### 运行时动态生成层

`UnrealCSharp` 把动态类生成抽成一套独立子系统：

- `FDynamicGenerator::Generator()` 统一串起 `Enum / Struct / Interface / Class / Core` 五类动态生成器；
- `FReflectionRegistry` 负责把 Mono 类型、动态属性与 UE 侧约定统一成可查询的反射注册表；
- `FClassReflection` 提供父类、接口、属性、字段、方法、泛型参数、路径名等统一视图；
- `FDynamicGeneratorCore::GeneratorProperty()` / `GeneratorFunction()` / `GeneratorInterface()` 把字段、函数、接口里的依赖全部折叠进 `FDynamicDependencyGraph`；
- `FDynamicDependencyGraph::Generator()` 再按依赖关系决定动态类型的生成顺序；
- `FDynamicClassGenerator::Generator()` 在需要时重命名旧类、创建新类 / BlueprintGeneratedClass、重新实例化旧对象，并广播更新事件。

对当前插件最重要的启发不是“改用 Mono 反射”，而是：**把类生成前的依赖收集、生成中的阶段排序、生成后的重实例化显式拆开**。当前 `FAngelscriptClassGenerator` 已经有 `AddReloadDependency()`、`PropagateReloadRequirements()`、`EnsureReloaded()` 等逻辑，但这些规则主要服务于 reload requirement 决策，还没有形成像 `FDynamicDependencyGraph` 那样单独可视化、可调试、可测试的阶段模型。

### 监听器与增量更新闭环

`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/Listener/FEditorListener.cpp` 表明 `UnrealCSharp` 并不是只靠“重新点一下生成”工作：

- `OnPostEngineInit()` 会先触发 `CodeAnalysis` 与 `FDynamicGenerator::CodeAnalysisGenerator()`；
- `DirectoryWatcher` 监控 `.cs` 文件变更，按目录规则过滤 `Proxy` / `obj` 等生成目录；
- `OnApplicationActivationStateChanged()` 会在编辑器重新激活时把累积的文件改动送进 `FCSharpCompiler::Compile()`；
- `OnAssetAdded()` / `OnAssetRemoved()` / `OnAssetRenamed()` / `OnAssetUpdatedOnDisk()` 又把资产变化送进 `FAssetGenerator`；
- `OnCompile()` 更新 `CodeAnalysis` 输出并刷新动态文件映射；
- `OnPrePIEEnded()` 则让动态类生成器有机会在 PIE 前后修正状态。

当前插件也有热重载闭环，但主要集中在 `FAngelscriptEngine::CheckForFileChanges()`、轮询线程与 compile/reload 分支里；因此更值得吸收的是 **监听源头分层** 与 **编译/生成/资产变更的统一事件面**，而不是照搬 C# 文件监听本身。

## UnrealCSharp 类生成机制详细分析

### 一、静态代理代码生成轨

这一轨并不生成 Unreal `UClass` 本体，而是生成 C# 侧代理与绑定包装：

- `FGeneratorCore.cpp` 负责类型映射、支持范围过滤、属性/容器类型展开、输出头注释与生成文件集合维护；
- `FClassGenerator.cpp` 遍历 `TObjectIterator<UClass>`，跳过动态类、蓝图生成类、特殊类与不支持类型，然后为每个静态 UE 类生成 C# 代理；
- `FClassGenerator` 在生成代理时会拼接 namespace、父类、接口、属性 getter/setter、函数包装、`StaticClass()` 入口，并且主动跳过已由绑定系统覆盖的属性和函数；
- `FBindingClassGenerator.cpp` / `FBindingEnumGenerator.cpp` 再基于 `FBinding` 元数据生成另一层静态绑定包装，使 C# 侧能通过统一实现类访问底层导出函数；
- `UnrealTypeSourceGenerator.cs` 作为 Roslyn Source Generator，在编译时校验动态类型声明是否合法，并为动态 `UClass` / `UStruct` / `UInterface` 补 `StaticClass()` / `StaticStruct()` / `GarbageCollectionHandle` / `Equals` / `GetHashCode` 等胶水代码。

这条链路的关键点是：`UnrealCSharp` 先把 **“如何生成语言侧代理代码”** 做成独立子系统，再把 **“如何在运行时创建新的 Unreal 类型”** 放到另一条轨道上。当前插件虽然已经有 `GenerateNativeBinds()`，但“扫描 -> 过滤 -> 分组 -> 输出 -> 缓存模块名”主要堆在 `AngelscriptEditorModule.cpp` 一处，后续如果要继续扩展自动绑定，最应该吸收的是这层分工方式，而不是 C# 代理文件格式本身。

### 二、动态 UType 生成轨

这一轨直接面向 UE 侧动态类型：

- `FReflectionRegistry.cpp` 在初始化时把 `OverrideAttribute`、`UClassAttribute`、`UStructAttribute`、`UEnumAttribute`、`UPropertyAttribute`、`UFunctionAttribute`、`UInterfaceAttribute` 以及大量 Blueprint / config / metadata 属性统一注册进反射表；
- `FClassReflection.h` 对 Mono 类型给出统一抽象，外部不需要直接到处操作 `MonoClass*` / `MonoReflectionType*`；
- `FDynamicGenerator.cpp` 统一控制 full generator、code analysis generator、file-change generator 三种入口；
- `FDynamicGeneratorCore.cpp` 把属性、函数、接口里引用到的类、枚举、结构体、接口依赖全部抽出来，推入 `FDynamicDependencyGraph`；
- `FDynamicDependencyGraph.cpp` 用显式节点/依赖图驱动生成顺序，并区分强依赖与 soft reference；
- `FDynamicClassGenerator.cpp` 真正创建或重建 `UClass` / `UBlueprintGeneratedClass`，必要时给旧类重命名、触发 reinstance、修复默认子对象与构造器绑定。

这条机制说明 `UnrealCSharp` 的“类生成”其实分为两个层级：

1. 代码分析与元数据解析：决定有哪些动态类型、哪些 override、哪些文件归属到哪些动态类型；
2. 依赖图与运行时生成：按有向依赖顺序创建真正的 `UClass` / `UStruct` / `UEnum` / `UInterface`。

当前插件已经有 `FAngelscriptPreprocessor` 与 `FAngelscriptClassGenerator`，但二者之间更多是“脚本描述符 -> 直接生成 / reload”，而不是先产出一个独立可复用的“生成计划 / 依赖图 / 文件-类型索引”。这正是最值得吸收的结构差异。

### 三、代码分析与增量文件映射

`Reference/UnrealCSharp/Script/CodeAnalysis/CodeAnalysis.cs` 和 `Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGeneratorCore.cpp` 共同组成了增量机制的关键补丁层：

- `CodeAnalysis.cs` 会扫描 `.cs` 文件，提取动态类、动态结构体、动态枚举、动态接口以及 `Override` 相关信息，最后写出 JSON 索引；
- `FDynamicGeneratorCore::BeginCodeAnalysisGenerator()` / `EndCodeAnalysisGenerator()` 会加载 / 清理这些 JSON 数据；
- `FDynamicGenerator::SetCodeAnalysisDynamicFilesMap()` / `GetDynamicType()` 会利用索引，把“哪个文件变了”映射回“应该重建哪个动态类型”；
- `FEditorListener::OnCompile()` / `OnDirectoryChanged()` 则把增量变更和代码分析重新串回整个生成链。

这里真正可吸收的不是 JSON 这种具体格式，而是 **“先维护文件-类型索引，再基于索引决定局部重编译 / 重生成”** 的思路。当前插件在热重载时更偏文件扫描和模块级 reload requirement 传播，未来完全可以为 `.as` 文件建立类似的 `module -> class/struct/enum/delegate -> output` 映射缓存。

### 四、生成触发入口与产物形态

`UnrealCSharp` 的生成能力不是只挂在一个 editor 菜单上，而是分布在多个可复用入口：

- `UnrealCSharp.Editor.Generator`、`UnrealCSharp.Editor.Compile`、`UnrealCSharp.Editor.SolutionGenerator`、`UnrealCSharp.Editor.CodeAnalysis` 等控制台命令让各阶段可以独立触发；
- cook 场景下，`UnrealCSharpEditor.cpp` 会在 `OnFilesLoaded()` 后自动调用 `Generator()`，说明它把“生成结果必须对打包可见”当成一等需求；
- 最终产物不只是一份动态类型状态，而是包括工程文件、C# 包装代码、绑定实现代码、动态/override 索引、编译产物以及清理后的生成目录。

对当前插件来说，最值得吸收的不是“也要生成 `.csproj`”，而是：**生成入口、生成产物、清理规则、打包期行为都要被视为同一条工作流的一部分**。当前 `GenerateNativeBinds()` 更像一次性工具函数，后续如果要继续演进自动绑定 / 生成能力，需要把这四类入口统一纳入设计。

## 对比结论总览

| 能力域 | UnrealCSharp | 当前插件 | 结论 |
| --- | --- | --- | --- |
| 模块职责拆分 | 运行时 / core / editor / generator / compiler / cross-version 拆分明确 | `Runtime + Editor + Test` 边界较粗，生成与热重载逻辑内收 | **高优先吸收：先拆职责，再决定是否拆模块** |
| 编辑器生成总控 | `Generator()` 显式阶段化、可观测 | `GenerateNativeBinds()` 与 runtime compile/reload 分散 | **高优先吸收：补统一生成流水线与阶段事件** |
| 类生成前依赖收集 | `ReflectionRegistry + ClassReflection + DependencyGraph` | `Preprocessor + ClassGenerator` 已有，但依赖阶段内收 | **高优先吸收：引入显式依赖图/生成计划层** |
| 文件变更到类型映射 | `CodeAnalysis` + JSON + `GetDynamicType()` | 主要靠文件扫描、模块名与 reload propagation | **高优先吸收：补 manifest / 索引缓存** |
| 资产与监听闭环 | `DirectoryWatcher + AssetRegistry + Compile + PIE` 联动 | 热重载更多集中在 runtime 线程扫描 | **中高优先吸收：把监听源头显式化** |
| 生成产物生命周期 | 显式维护生成文件集合并清理遗留输出，同时把工程文件与编译产物纳入生成流程 | 自动绑定产物生成后缺少同等级生命周期管理入口 | **中高优先吸收：把生成结果从“一次性输出”升级为“受管产物”** |
| 触发入口丰富度 | 菜单、控制台命令、目录监听、资产更新、cook 场景都能进入生成链 | 主要依赖编辑器模块工具函数与 runtime 热重载路径 | **中优先吸收：补独立命令入口与非交互场景支持** |
| 静态代理 / 绑定生成 | 独立 `ScriptCodeGenerator` 负责类/结构/枚举/资产/绑定输出 | `GenerateNativeBinds()` 已能扫 UClass 生成绑定模块 | **中优先吸收：重构当前 bind generation 分层，而非照抄 C# 代理格式** |
| 跨版本兼容层 | 有 `CrossVersion` 模块 | 当前插件多靠源码内条件处理 | **中优先吸收：如果 UE 版本分支继续增多，应收口到 shim 层** |
| 动态类 authoring 模型 | C# attribute + partial class + Roslyn/Mono | `.as` 预处理描述符 + AngelScript VM | **仅保留参考：不应照搬语法/编译器模型** |
| Mono Domain / GCHandle / Weaver | 直接服务于 C# 运行时 | 当前插件不存在同构运行时 | **不建议吸收** |
| `SourceCodeGenerator` Program | 独立程序式生成入口 | 当前插件暂无独立程序生成需求 | **谨慎参考：先确认是否真需要离线生成工具** |

## 优先吸收建议

### 高优先

- 为当前插件建立显式的“生成阶段图”，把脚本预处理、类生成、自动绑定、热重载判定、验证入口用统一事件面串起来。
- 为 `AngelscriptClassGenerator` 增加独立的依赖图 / 生成计划层，而不是只在 reload requirement 传播里隐式维护依赖。
- 为 `.as` 文件建立 module/type/output 的 manifest 或 cache，降低热重载时对全量扫描和经验性判定的依赖。
- 把 editor 侧监听源拆清：文件变更、资产变更、PIE 生命周期、编译完成回调应当是显式信号，而不是都落在 runtime 热重载轮询里。

### 中优先

- 把当前 `GenerateNativeBinds()` 的职责拆细，至少分离“扫描过滤”“模块分组”“源码输出”“缓存记录”“后续验证”五层。
- 评估是否需要引入独立的 `Generator` / `Compiler` **内部支撑层**；默认目标是在现有三模块内部提炼可测试组件，而不是镜像 `UnrealCSharp` 新增同名公共插件模块。
- 如果目标 UE 版本继续扩张，考虑增加类似 `CrossVersion` 的兼容层，而不是把条件分支散落在 runtime/editor/generator 各处。

### 仅作参考或明确不吸收

- `Mono` 域管理、Roslyn `SourceGenerator`、`Fody` Weaver、GCHandle 与程序集编译链是语言运行时特有设计，不应作为当前插件的直接待办。
- C# attribute 驱动的动态类 authoring 与 partial class 机制不可直接等价成 AngelScript 语法功能，最多只能借鉴“元数据先行、生成分层”的思想。

## 分阶段执行计划

### Phase 1：固定对比边界与生成术语

> 目标：先把 `UnrealCSharp` 的“模块边界、生成阶段、动态类术语、非目标项”固化为当前项目内部语义，避免后续实现时把 C# 运行时特性误翻译成普通插件待办。

- [ ] **P1.1** 固定 `UnrealCSharp` 模块职责与生成阶段术语表
  - 以 `Reference/UnrealCSharp/UnrealCSharp.uplugin`、`Reference/UnrealCSharp/Source/UnrealCSharpEditor/Private/UnrealCSharpEditor.cpp`、`Reference/UnrealCSharp/Source/UnrealCSharpCore/Private/Dynamic/FDynamicGenerator.cpp`、`Reference/UnrealCSharp/Source/ScriptCodeGenerator/Private/FGeneratorCore.cpp` 为证据，整理一份术语表，至少覆盖 `Core`、`Generator`、`Compiler`、`CodeAnalysis`、`DynamicClass`、`BindingGenerator`、`CrossVersion` 这些词在该参考项目里的确切含义。
  - 这一步的重点不是再抄一次 README，而是建立后续任务的统一词汇，避免后面有人把 `SourceGenerator`、`CodeAnalysisGenerator`、`DynamicGenerator` 当成一个东西。
  - 输出应进入项目文档或 backlog 入口，而不是停留在一次性的聊天结论里。
- [ ] **P1.1** 📦 Git 提交：`[P3] Docs: capture UnrealCSharp architecture terminology`

- [ ] **P1.2** 固定当前插件的一一映射与非目标项
  - 以 `Plugins/Angelscript/Source/AngelscriptRuntime/Core/AngelscriptEngine.cpp`、`Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator/AngelscriptClassGenerator.cpp`、`Plugins/Angelscript/Source/AngelscriptEditor/Core/AngelscriptEditorModule.cpp` 为证据，把当前插件中对应 `runtime compile`、`class generation`、`native bind generation`、`hot reload` 的落点列清楚。
  - 同时显式写出哪些 `UnrealCSharp` 能力是当前插件不应直接追的，例如 `Mono Domain`、Roslyn Source Generator、程序集编译、GCHandle 管理等，后续任何实现计划都不应把这些项重新列回主 backlog。
  - 这一步完成后，后续所有任务都能明确地区分“边界优化”与“语言本体差异”。
- [ ] **P1.2** 📦 Git 提交：`[P3] Docs: map Angelscript architecture against UnrealCSharp`

### Phase 2：先做低风险架构吸收

> 目标：优先吸收不改变语言模型、但能显著改善可维护性的工程边界，包括显式阶段图、缓存索引、监听事件面和生成日志。

- [ ] **P2.1** 为当前插件定义统一的生成流水线与阶段事件
  - 参考 `UnrealCSharpEditor.cpp` 的阶段顺序，把当前插件的脚本根发现、预处理、编译、类生成、自动绑定、热重载检查、测试发现拆成一张显式阶段图，并确定每个阶段的输入、输出、错误面和可观测日志点。
  - 第一步不要求新增模块，但至少要把 `AngelscriptRuntime` / `AngelscriptEditor` 中当前内收的流程收束成统一编排入口，避免后续所有工具都各自启动一段半重叠的逻辑。
  - 需要同步考虑 commandlet、editor、test 三条调用路径，确保不会因为新编排入口而重新耦死宿主项目模块。
- [ ] **P2.1** 📦 Git 提交：`[P3] Refactor: define staged Angelscript generation pipeline`

- [ ] **P2.2** 为 `.as` 文件建立显式 manifest / 索引缓存
  - 参考 `CodeAnalysis.cs` 与 `FDynamicGenerator::SetCodeAnalysisDynamicFilesMap()`，为当前插件建立最小可用的 `file -> module -> class/struct/enum/delegate -> generated output` 映射缓存，用于 hot reload、诊断与测试定位。
  - 这一步不要求照抄 JSON 格式，但要求结果可持久化、可调试、可在测试中断言；否则后续依赖图与增量重编译只能继续依赖经验性扫描。
  - 缓存应明确区分项目脚本根与插件脚本根，避免把多脚本根场景退化成全量重新编译。
- [ ] **P2.2** 📦 Git 提交：`[P3] Feat: add Angelscript script manifest cache`

- [ ] **P2.3** 重整文件变更与资产变更监听面
  - 参考 `FEditorListener.cpp`，把当前插件内的热重载触发源分层为：脚本文件变更、资产变化、PIE 生命周期、编译完成通知、测试发现时机；这些信号应有统一汇流点，而不是只靠 runtime 热重载线程感知。
  - 当前实现仍可以继续使用轮询作为保底路径，但 editor 场景下应优先让变更事件变成显式输入，并为后续 UI/命令入口留下挂点。
  - 需要同步梳理失败重试策略与诊断输出，避免监听源一增多就让 reload 行为更不可预测。
- [ ] **P2.3** 📦 Git 提交：`[P3] Refactor: separate Angelscript reload event sources`

- [ ] **P2.4** 为自动绑定与生成结果建立受管产物生命周期
  - 参考 `FGeneratorCore::GeneratorFiles` 与遗留文件清理思路，把当前自动绑定输出从“写完即结束”升级成“生成前记录、生成后比对、删除过时产物、必要时刷新缓存入口”的受管目录流程。
  - 这一步需要同时考虑 editor 交互入口、commandlet/cook 场景与失败恢复路径，避免因为产物残留导致后续构建结果与源码状态不一致。
  - 输出层最好同时暴露独立命令入口，便于在非交互场景下验证生成结果而不依赖编辑器按钮。
- [ ] **P2.4** 📦 Git 提交：`[P3] Feat: manage generated bind artifacts lifecycle`

### Phase 3：处理中风险类生成吸收项

> 目标：围绕 `AngelscriptClassGenerator` 引入更清晰的依赖计划层和后处理阶段，但不改变当前插件仍然以内置 AngelScript VM 和脚本描述符为核心这一事实。

- [ ] **P3.1** 为类生成增加独立依赖图 / 生成计划层
  - 参考 `FDynamicGeneratorCore` 与 `FDynamicDependencyGraph`，把当前 `FAngelscriptClassGenerator` 内部的 `AddReloadDependency()`、`PropagateReloadRequirements()`、`EnsureReloaded()` 之上的隐式依赖关系显式提出来，形成单独可测试的数据结构。
  - 目标不是替换现有 class generator，而是让“为什么必须 full reload”“为什么这个类必须先生成”“哪些引用只是 soft dependency”变得可观察、可断言、可序列化。
  - 这一步完成后，才适合继续优化 reload 粒度，否则任何增量策略都会继续被隐藏状态拖慢。
- [ ] **P3.1** 📦 Git 提交：`[P6] Refactor: extract class generation dependency graph`

- [ ] **P3.2** 把自动绑定生成从编辑器大文件中拆出独立支撑层
  - 参考 `ScriptCodeGenerator` 的分层方式，把当前 `GenerateNativeBinds()` 中的扫描、过滤、分组、Build.cs 输出、源码输出、模块缓存记录拆成更小的可测试组件。
  - 第一步明确**不新增**对标 `ScriptCodeGenerator` / `Compiler` 的公共插件模块；目标是在现有 editor/runtime 边界内完成职责下沉，并让后续如果真需要 commandlet / program 工具时已有逻辑可平移，而不是从 `AngelscriptEditorModule.cpp` 手工剥离。
  - 同时应明确 editor-only 与 runtime-safe 的边界，避免自动绑定的实现继续反向污染 runtime 头文件依赖。
- [ ] **P3.2** 📦 Git 提交：`[P6] Refactor: split native bind generation responsibilities`

- [ ] **P3.3** 评估是否需要 `CrossVersion` 风格兼容层
  - 检查当前插件中 UE 版本条件分支主要散落在哪些 runtime / editor / generator 文件，判断是否已经达到需要单独兼容层的复杂度阈值。
  - 如果尚未达到，就只做“条件分支盘点 + 迁移触发条件”文档，不要为了模仿 `UnrealCSharp` 而新增空模块；如果已经达到，则应优先把与类生成、自动绑定、编辑器集成强相关的差异收进去。
  - 这一项必须保持克制，避免把“未来可能会有更多版本差异”提前变成空洞架构。
- [ ] **P3.3** 📦 Git 提交：`[P6] Docs: evaluate version-shim boundary for generator code`

### Phase 4：把吸收项接回验证与工作流

> 目标：确保前面吸收的架构边界不会停留在“写得更漂亮”，而是能被测试、文档和使用入口消费。

- [ ] **P4.1** 为生成流水线与类生成依赖补测试入口
  - 在 `Plugins/Angelscript/Source/AngelscriptTest/` 下按主题给生成流水线、manifest/cache、类生成依赖图、自动绑定输出至少各补一层测试，不要把这些检查全部塞回 broad catch-all 场景。
  - 重点验证：阶段顺序是否稳定、缓存命中是否正确、增量 reload 是否能根据映射缩小范围、依赖图是否能解释 full reload / soft reload 的判定。
  - 如果某项能力当前只能在 editor 下验证，应明确写出测试层级与运行入口，不要把 editor-only 行为混装成 runtime smoke test。
- [ ] **P4.1** 📦 Git 提交：`[P6] Test: cover staged generation and dependency planning`

- [ ] **P4.2** 更新构建 / 测试 / 工作流文档
  - 如果最终引入新的生成阶段入口、manifest/cache、监听策略或自动绑定支撑层，必须同步更新 `Documents/Guides/Build.md`、`Documents/Guides/Test.md` 以及相关架构说明，避免“结构改善了，但使用入口更隐蔽”。
  - 文档里应明确说明哪些项来自 `UnrealCSharp` 的横向借鉴、哪些仍然保留当前插件独有路径，防止后续维护者误以为插件正在朝“第二个 UnrealCSharp”演化。
  - 如果某些参考点最终被判定为不吸收，也应显式记录原因，减少重复开题成本。
- [ ] **P4.2** 📦 Git 提交：`[P6] Docs: document adopted UnrealCSharp-inspired workflow changes`

## 验收标准

1. 文档明确给出 `UnrealCSharp` 的模块拆分、编辑器生成流水线、动态类生成机制与代码分析/增量更新闭环，而不是只停留在 README 级别概述。
2. 每一类对比结论都被明确归类为：高优先吸收、中优先吸收、仅作参考、不建议吸收。
3. 每个吸收项都能指出当前插件的对应落点，而不是抽象地说“可以参考它的架构”。
4. 所有执行项都围绕“插件化、可维护、可验证”的当前目标组织，没有把 Mono / C# 工具链误记为直接待办。
5. 后续如果进入实现阶段，可以直接按本文的 Phase 与编号拆任务，而不需要重新做一轮 UnrealCSharp 架构调研。

## 风险与注意事项

### 风险 1：把语言运行时差异误判成插件缺口

`UnrealCSharp` 的 `SourceGenerator`、`CodeAnalysis`、Mono 反射、程序集编译与 GCHandle 管理都强依赖 C# 运行时；如果不先写清楚边界，后续非常容易把这些语言特有设计误记成当前插件“缺一个模块就能补上”的普通待办。

### 风险 2：为了模仿结构而过早拆模块

`UnrealCSharp` 的模块多，不代表当前插件必须立刻照着拆。当前最急迫的是拆职责、补阶段、补缓存与验证，而不是先创建一批空模块把复杂度搬到更多目录里。

本计划默认**不以新增公共插件模块为目标**；只有在现有 `AngelscriptRuntime` / `AngelscriptEditor` / `AngelscriptTest` 内部职责已经清晰、且确有验证过的维护收益时，才允许单独评估是否需要额外支撑层。

### 风险 3：把静态绑定生成和动态类生成混成一类问题

`UnrealCSharp` 很清楚地区分“生成 C# 代理 / 绑定代码”和“运行时创建 UE 动态类型”；当前插件如果继续把自动绑定、脚本类生成、热重载判定都揉在一起，后续任何性能优化或问题定位都会继续模糊。

### 风险 4：忽略当前插件已经具备的动态类基础

当前插件并不是“没有类生成”，而是已经有 `FAngelscriptClassGenerator`、`UASClass`、`UASStruct`、软/全 reload 机制。后续吸收应建立在已有基础之上，优先补“结构清晰度”和“可验证性”，而不是推倒重来。

### 风险 5：本地 `SourceCodeGenerator` 参考快照不完整

`UnrealCSharp.uplugin` 声明了 `SourceCodeGenerator`，但当前本地 `Reference/UnrealCSharp/Source/SourceCodeGenerator/` 未展开可读实现文件；后续如果要进一步研究独立程序式生成入口，需要再次对照官方仓库或文档确认该部分真实职责，不应只凭本地快照下结论。

